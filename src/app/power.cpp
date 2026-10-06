// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.h"

#include <esp_rtc_time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <stdlib.h>
#include <string.h>

#include <arrocco/ui/sleep_screen.h>

#include "config.h"
#include "gt911.h"
#include "log.h"
#include "net_wifi.h"
#include "panel.h"

namespace power {
namespace {

constexpr uint32_t kRtcMagic = 0xA770CC05u;
constexpr uint32_t kCheckEveryMs = 1000;      // the idle check needs no finer grain
constexpr uint32_t kSleepAfterMinMs = 10000;  // below this the board could hardly be used

// RTC memory: survives deep sleep, cleared at power-on. The idle timeout set with
// 'sleep-after' lives here too, so a test value holds across several sleeps.
struct RtcState {
  uint32_t magic;
  uint32_t sleeps;       // deep sleeps entered since power-on
  uint32_t touchWakes;   // ... ended by the touch INT line
  uint32_t otherWakes;   // ... ended by anything else
  uint64_t sleptAtUs;    // RTC clock when the last one began
  uint32_t lastAsleepS;  // how long the last one lasted
  uint32_t sleepAfterMs; // idle time before sleeping, 0 = never
};
RTC_DATA_ATTR RtcState s_rtc = {0, 0, 0, 0, 0, 0, 0};

esp_reset_reason_t s_reset = ESP_RST_UNKNOWN;
esp_sleep_wakeup_cause_t s_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
bool s_woke = false;
uint32_t s_lastActivityMs = 0;
uint32_t s_lastCheckMs = 0;
uint8_t s_reported = 0xFF; // blocker bits last logged; 0xFF = none logged since the timer restarted
Blockers s_last = {false, false, false, false, false};
char s_status[96] = "";

uint8_t blockerBits(const Blockers& b) {
  return static_cast<uint8_t>((b.touching ? 1u : 0u) | (b.engineThinking ? 2u : 0u) | (b.clockRunning ? 4u : 0u) |
                              (b.network ? 8u : 0u) | (b.cannotWake ? 16u : 0u));
}

void describe(uint8_t bits, char* out, size_t len) {
  static const char* const kNames[5] = {"finger on the glass", "engine thinking", "game clock running",
                                        "network on", "touch could not wake the board"};
  size_t used = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < 5; ++i) {
    if (!(bits & (1u << i))) continue;
    const int n = snprintf(out + used, len - used, "%s%s", used ? ", " : "", kNames[i]);
    if (n < 0 || static_cast<size_t>(n) >= len - used) break;
    used += static_cast<size_t>(n);
  }
  if (used == 0) snprintf(out, len, "nothing");
}

// Signed: an activity stamped a moment after `now` (the caller's clock is a little
// older) must count as no idle time at all, not as 49 days of it.
int32_t idleMs(uint32_t now) { return static_cast<int32_t>(now - s_lastActivityMs); }

const char* causeText(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0: return "a touch (INT LOW, ext0)";
    case ESP_SLEEP_WAKEUP_TIMER: return "the timer";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "nothing recorded (reset button during the sleep?)";
    default: return "another wake source";
  }
}

const char* resetText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software reset";
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT (weak USB cable or battery?)";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_USB: return "USB reset";
    case ESP_RST_EXT: return "reset pin";
    default: return "other";
  }
}

void printStatus() {
  const uint32_t now = millis();
  char blocked[96];
  describe(blockerBits(s_last), blocked, sizeof(blocked));
  const int32_t idle = idleMs(now);
  logLine("SLEEP after %lu s idle%s | idle now %ld s | %s | would be held back by: %s",
          static_cast<unsigned long>(s_rtc.sleepAfterMs / 1000UL), s_rtc.sleepAfterMs ? "" : " (off)",
          static_cast<long>(idle > 0 ? idle / 1000 : 0), statusText(now), blocked);
  logLine("SLEEP since power-on: %lu sleeps, %lu touch wakes, %lu other | this boot: %s%s | INT %s,"
          " the touch can wake the board: %s",
          static_cast<unsigned long>(s_rtc.sleeps), static_cast<unsigned long>(s_rtc.touchWakes),
          static_cast<unsigned long>(s_rtc.otherWakes), s_woke ? "wake by " : resetText(s_reset),
          s_woke ? causeText(s_cause) : "", gt911::intHigh() ? "HIGH (idle)" : "LOW (a report waits)",
          gt911::canWake() ? "yes" : "NO");
  if (s_woke)
    logLine("SLEEP the last sleep lasted %lu s", static_cast<unsigned long>(s_rtc.lastAsleepS));
}

} // namespace

void begin() {
  s_reset = esp_reset_reason();
  s_woke = (s_reset == ESP_RST_DEEPSLEEP);
  s_cause = s_woke ? esp_sleep_get_wakeup_cause() : ESP_SLEEP_WAKEUP_UNDEFINED;
  if (s_rtc.magic != kRtcMagic || s_reset == ESP_RST_POWERON)
    s_rtc = RtcState{kRtcMagic, 0, 0, 0, 0, 0, cfg::kSleepAfterIdleMs};
  if (s_woke) {
    const uint64_t nowUs = esp_rtc_get_time_us(); // the RTC timer runs on through deep sleep
    s_rtc.lastAsleepS =
        nowUs > s_rtc.sleptAtUs ? static_cast<uint32_t>((nowUs - s_rtc.sleptAtUs) / 1000000ULL) : 0;
    if (s_cause == ESP_SLEEP_WAKEUP_EXT0) ++s_rtc.touchWakes;
    else ++s_rtc.otherWakes;
  }
  // The touch RST pin may still be latched HIGH from the sleep, GPIO8 still an RTC IO.
  gt911::releaseSleepHold();
}

void logBoot() {
  if (s_woke) {
    logLine("WAKE  from deep sleep #%lu after %lu s, woken by %s (since power-on: %lu touch wakes, %lu other)",
            static_cast<unsigned long>(s_rtc.sleeps), static_cast<unsigned long>(s_rtc.lastAsleepS),
            causeText(s_cause), static_cast<unsigned long>(s_rtc.touchWakes),
            static_cast<unsigned long>(s_rtc.otherWakes));
  } else {
    logLine("BOOT  reset reason: %s | deep sleep after %lu s with nothing to do", resetText(s_reset),
            static_cast<unsigned long>(s_rtc.sleepAfterMs / 1000UL));
  }
}

bool wokeFromSleep() { return s_woke; }

void noteActivity(uint32_t now) { s_lastActivityMs = now; }

void service(uint32_t now, BlockerQuery query) {
  if (now - s_lastCheckMs < kCheckEveryMs) return;
  s_lastCheckMs = now;
  const Blockers blockers = query(now);
  s_last = blockers;
  const uint32_t after = s_rtc.sleepAfterMs;
  const int32_t idle = idleMs(now);
  if (after == 0 || idle < static_cast<int32_t>(after)) {
    s_reported = 0xFF;
    return;
  }
  const uint8_t why = blockerBits(blockers);
  if (why != 0) {
    if (why != s_reported) {
      char text[96];
      describe(why, text, sizeof(text));
      logLine("SLEEP due (idle %ld s) but held back by: %s", static_cast<long>(idle / 1000), text);
      s_reported = why;
    }
    return;
  }
  sleepNow("nothing touched and nothing running");
}

void sleepNow(const char* why) {
  const uint32_t t0 = millis();
  logLine("SLEEP %s: sleep screen, then deep sleep until a touch", why);
  // From here no touch report may be read: one made during the last refresh must stay
  // pending, so that INT keeps pulsing and wakes the board straight away.
  panel::setBusyHook(nullptr);
  net::wifiRadioOff("going to sleep");

  const arrocco::ui::SleepScreen screen = arrocco::ui::drawSleepScreen(panel::display);
  panel::refresh(screen == arrocco::ui::SleepScreen::Picture ? panel::Kind::Full : panel::Kind::Partial, 0);
  panel::hibernate();
  gt911::prepareForDeepSleep();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_err_t err = esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(cfg::kTouchInt), 0);
  // ext0 and the RTC pull-up on INT both live in the RTC peripheral domain: keep it on.
  if (err == ESP_OK) err = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  if (err != ESP_OK) {
    // Asleep with no way to wake up would need the power switch: restart instead.
    logLine("SLEEP cannot arm the touch wake-up (%s): restarting instead", esp_err_to_name(err));
    Serial.flush();
    delay(20);
    esp_restart();
  }
  // The ROM prints its boot banner on UART0 TX, which is GPIO43 - the touch RST line -
  // at every reset, deep-sleep wakes included. The hold keeps the pin HIGH through the
  // sleep; this keeps the ROM quiet at the wake, so the GT911 is never reset by text.
  esp_deep_sleep_disable_rom_logging();

  ++s_rtc.sleeps;
  s_rtc.sleptAtUs = esp_rtc_get_time_us();
  logLine("SLEEP #%lu: down after %lu ms (%s)%s", static_cast<unsigned long>(s_rtc.sleeps),
          static_cast<unsigned long>(millis() - t0),
          screen == arrocco::ui::SleepScreen::Picture ? "picture" : "note over the last screen",
          gt911::intHigh() ? "" : " - INT is LOW, a touch is waiting: the board wakes again at once");
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
}

bool consoleCommand(const char* verb, char* rest) {
  if (strcmp(verb, "sleep-status") == 0) {
    printStatus();
    return true;
  }
  if (strcmp(verb, "sleep-now") == 0) {
    if (!gt911::canWake()) {
      logLine("SLEEP refused: the touch controller could not wake the board (not working, or its INT "
              "line never proven)");
      return true;
    }
    sleepNow("asked on the serial console");
  }
  if (strcmp(verb, "sleep-after") == 0) {
    char* end = nullptr;
    const long seconds = strtol(rest, &end, 10);
    if (rest[0] == '\0' || end == rest || seconds < 0 || seconds > 86400L ||
        (seconds > 0 && static_cast<uint32_t>(seconds) * 1000UL < kSleepAfterMinMs)) {
      logLine("SLEEP usage: sleep-after <seconds> (0 = never sleep, otherwise %lu..86400)",
              static_cast<unsigned long>(kSleepAfterMinMs / 1000UL));
      return true;
    }
    s_rtc.sleepAfterMs = static_cast<uint32_t>(seconds) * 1000UL;
    noteActivity(millis());
    if (seconds == 0) logLine("SLEEP off until the next power-on (sleep-after %u restores it)",
                              static_cast<unsigned>(cfg::kSleepAfterIdleMs / 1000UL));
    else logLine("SLEEP after %ld s with nothing to do, until the next power-on", seconds);
    return true;
  }
  return false;
}

void consoleHelp() { logLine("CMD   sleep-status | sleep-now | sleep-after <seconds, 0 = never>"); }

const char* statusText(uint32_t now) {
  const uint32_t after = s_rtc.sleepAfterMs;
  const int32_t idle = idleMs(now);
  if (after == 0) {
    snprintf(s_status, sizeof(s_status), "sleep off");
  } else if (idle < static_cast<int32_t>(after)) {
    snprintf(s_status, sizeof(s_status), "sleep in %ld s",
             static_cast<long>((static_cast<int32_t>(after) - (idle > 0 ? idle : 0)) / 1000));
  } else {
    char text[64];
    describe(blockerBits(s_last), text, sizeof(text));
    snprintf(s_status, sizeof(s_status), "sleep due, held back by: %s", text);
  }
  return s_status;
}

} // namespace power
