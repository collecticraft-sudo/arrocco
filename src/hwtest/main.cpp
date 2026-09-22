// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco hwtest - hardware-validation firmware.
//
// Proves, with nothing but the e-paper screen and the USB serial log, that the
// panel, the touch controller, the buzzer and the optional fuel gauge are wired
// and working. One screen refresh per user-visible event, every refresh timed.
//
// Serial keys (fallback when touch is dead): b t p s = screens, r = partial
// refresh, f = full refresh, i = report, ? = help.
#include <Arduino.h>
#include <esp_system.h>

#include "config.h"
#include "gauge.h"
#include "gt911.h"
#include "i2cbus.h"
#include "log.h"
#include "panel.h"
#include "screens.h"

namespace {

bool s_dirty = false;    // something visible changed: one partial refresh is owed
bool s_wantFull = false; // ... and it has to be a full one
bool s_down = false;     // a DOWN was logged and its UP is still owed
bool s_blocked = false;  // ignore the touch in progress until the finger lifts
bool s_buzzerOk = false;
uint8_t s_failStreak = 0;
uint32_t s_rejected = 0;
uint32_t s_lastPollMs = 0;
uint32_t s_lastFrameMs = 0;
uint32_t s_lastActivityMs = 0;
uint32_t s_lastRetryMs = 0;
uint32_t s_lastGaugeMs = 0;
uint32_t s_lastHostCheckMs = 0;
screens::TouchPoint s_touch = {false, 0, 0, 0, 0};

// ---------- boot report ----------

const char* resetReasonText(esp_reset_reason_t reason) {
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
    default: return "other";
  }
}

void bootReport() {
  const esp_reset_reason_t reason = esp_reset_reason();
  logLine("==== %s ==== built %s %s", cfg::kTitle, __DATE__, __TIME__);
  logLine("BOOT  reset reason %d: %s", static_cast<int>(reason), resetReasonText(reason));
  logLine("BOOT  chip %s rev %u, %u cores, %lu MHz, SDK %s", ESP.getChipModel(), ESP.getChipRevision(),
          ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()), ESP.getSdkVersion());
  const unsigned long flashMb = static_cast<unsigned long>(ESP.getFlashChipSize() / (1024UL * 1024UL));
  const unsigned long psramKb = static_cast<unsigned long>(ESP.getPsramSize() / 1024UL);
  logLine("BOOT  flash %lu MB, PSRAM %lu KB (free %lu KB), free heap %lu KB", flashMb, psramKb,
          static_cast<unsigned long>(ESP.getFreePsram() / 1024UL),
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL));
  if (flashMb != 16) logLine("BOOT  WARNING expected 16 MB of flash: is this a XIAO ESP32-S3 *Plus*?");
  if (psramKb == 0) logLine("BOOT  WARNING no PSRAM found: is this a XIAO ESP32-S3 *Plus*?");
  logLine("BOOT  pins: EPD RST %u CS %u BUSY %u DC %u SCK %u MOSI %u | I2C SDA %u SCL %u | "
          "touch INT %u RST %u | buzzer %u",
          cfg::kEpdRst, cfg::kEpdCs, cfg::kEpdBusy, cfg::kEpdDc, cfg::kEpdSck, cfg::kEpdMosi,
          cfg::kI2cSda, cfg::kI2cScl, cfg::kTouchInt, cfg::kTouchRst, cfg::kBuzzer);
  logLine("BOOT  touch flags: SWAP_XY %d, MIRROR_X %d, MIRROR_Y %d; full refresh every %u partials",
          TOUCH_SWAP_XY, TOUCH_MIRROR_X, TOUCH_MIRROR_Y, cfg::kFullEvery);
}

void statusReport() {
  const panel::Stats& st = panel::stats();
  char headline[32];
  gt911::headline(headline, sizeof(headline));
  logLine("STAT  %s | %s | INT edges %lu, frames %lu, I2C errors %lu, rejected %lu", headline,
          gt911::hint(), static_cast<unsigned long>(gt911::intEdges()),
          static_cast<unsigned long>(gt911::frames()), static_cast<unsigned long>(i2cbus::errors()),
          static_cast<unsigned long>(s_rejected));
  static const char* const kBusyPin[4] = {"not sampled", "driven HIGH (ok)", "FLOATING (FPC?)", "LOW after reset"};
  logLine("STAT  refreshes: %lu partial (last %lu ms), %lu full (last %lu ms), last BUSY wait %lu ms, since full %u/%u",
          static_cast<unsigned long>(st.partialTotal), static_cast<unsigned long>(st.lastPartialMs),
          static_cast<unsigned long>(st.fullTotal), static_cast<unsigned long>(st.lastFullMs),
          static_cast<unsigned long>(st.lastBusyMs), st.partialSinceFull, cfg::kFullEvery);
  logLine("STAT  panel %s, BUSY pin at boot: %s", st.responding ? "responding" : "NOT RESPONDING",
          kBusyPin[static_cast<uint8_t>(st.busyPin)]);
  logLine("STAT  %s | free heap %lu KB, free PSRAM %lu KB | screen %s", gauge::text(),
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL),
          static_cast<unsigned long>(ESP.getFreePsram() / 1024UL), screens::name(screens::current()));
}

// ---------- sound ----------

void playTone(uint32_t hz, uint32_t ms) {
  if (!s_buzzerOk) return;
  ledcWriteTone(cfg::kBuzzer, hz);
  delay(ms);
  ledcWriteTone(cfg::kBuzzer, 0); // duty 0: the pin rests LOW, the piezo draws nothing
}

void playSound(screens::Action action) {
  static const uint16_t kScale[8] = {523, 587, 659, 698, 784, 880, 988, 1047}; // C5..C6
  logLine("SOUND %s on GPIO%u%s", action == screens::Action::Beep ? "beep" : action == screens::Action::Scale ? "scale" : "sweep",
          cfg::kBuzzer, s_buzzerOk ? "" : " - LEDC attach failed, nothing played");
  if (action == screens::Action::Beep) playTone(880, 80);
  if (action == screens::Action::Scale)
    for (uint8_t i = 0; i < 8; ++i) playTone(kScale[i], 120);
  if (action == screens::Action::Sweep)
    for (uint32_t hz = 500; hz <= 4000; hz += 250) playTone(hz, 100);
}

// ---------- touch ----------

// Raw GT911 coordinates -> screen pixels. false = outside the panel: not a coordinate at all.
bool mapTouch(uint16_t rawX, uint16_t rawY, screens::TouchPoint& out) {
  const uint16_t limitX = TOUCH_SWAP_XY ? cfg::kScreenH : cfg::kScreenW;
  const uint16_t limitY = TOUCH_SWAP_XY ? cfg::kScreenW : cfg::kScreenH;
  if (rawX >= limitX || rawY >= limitY) return false;
  int16_t x = static_cast<int16_t>(TOUCH_SWAP_XY ? rawY : rawX);
  int16_t y = static_cast<int16_t>(TOUCH_SWAP_XY ? rawX : rawY);
  if (TOUCH_MIRROR_X) x = static_cast<int16_t>(cfg::kScreenW - 1 - x);
  if (TOUCH_MIRROR_Y) y = static_cast<int16_t>(cfg::kScreenH - 1 - y);
  out = screens::TouchPoint{true, rawX, rawY, x, y};
  return true;
}

void logTouch(const char* what, const char* note) {
  char sq[3];
  screens::squareAt(s_touch.x, s_touch.y, sq);
  logLine("TOUCH %-4s raw=(%u,%u) mapped=(%d,%d) square=%s INT edges=%lu%s", what, s_touch.rawX,
          s_touch.rawY, s_touch.x, s_touch.y, sq, static_cast<unsigned long>(gt911::intEdges()), note);
}

// Says it once per change: the verdict needs a few polled touches before it can exist.
void reportWireFault() {
  static gt911::WireFault s_reported = gt911::WireFault::None;
  const gt911::WireFault fault = gt911::wireFault();
  if (fault == s_reported) return;
  s_reported = fault;
  if (fault == gt911::WireFault::IntMissing)
    logLine("TOUCH FAIL: %lu touch reports polled but 0 edges on GPIO%u: INT WIRE MISSING (FTS02 A0 -> D9)",
            static_cast<unsigned long>(gt911::frames()), cfg::kTouchInt);
  if (fault == gt911::WireFault::RstMissing)
    logLine("TOUCH FAIL: INT works (%lu edges) but the address ignored our resets: RST WIRE MISSING "
            "(FTS02 A3 -> D6)", static_cast<unsigned long>(gt911::intEdges()));
}

void touchUp(const char* note) {
  if (s_down) logTouch("UP", note);
  if (s_down || s_blocked) reportWireFault();
  s_down = false;
  s_blocked = false;
}

void touchDown() {
  s_down = true;
  logTouch("DOWN", ""); // logged BEFORE any hit test
  const screens::Action action = screens::onTouchDown(s_touch);
  if (action == screens::Action::FullRefresh) s_wantFull = true;
  if (action == screens::Action::Beep || action == screens::Action::Scale || action == screens::Action::Sweep)
    playSound(action);
  s_dirty = true; // every touch-down is visible: the side column shows its coordinates
}

void pollTouch(uint32_t now) {
  if (!gt911::ready() || now - s_lastPollMs < cfg::kTouchPollMs) return;
  s_lastPollMs = now;

  gt911::Frame frame;
  if (!gt911::read(frame)) { // I2C failure: the frame is unusable, never a coordinate
    if (++s_failStreak >= cfg::kTouchLostAfter) {
      gt911::markLost();
      touchUp(" (touch controller lost)");
      s_lastRetryMs = now;
      s_dirty = true;
    }
    return;
  }
  s_failStreak = 0;

  if (!frame.fresh) { // the chip reports continuously while touched: silence = finger lifted
    if ((s_down || s_blocked) && now - s_lastFrameMs > cfg::kTouchSilenceMs) touchUp(" (no release frame, timed out)");
    return;
  }
  s_lastFrameMs = now;
  if (frame.count == 0) {
    touchUp("");
    return;
  }
  s_lastActivityMs = now;
  if (frame.palm || frame.count > 1) { // palm or several fingers: cancel until everything lifts
    if (!s_blocked) logLine("TOUCH gesture cancelled (%s, count %u): ignored until release",
                            frame.palm ? "large area" : "multi-touch", frame.count);
    s_blocked = true;
    return;
  }
  if (s_blocked) return;
  if (!mapTouch(frame.x, frame.y, s_touch)) {
    ++s_rejected;
    logLine("TOUCH rejected raw=(%u,%u): outside the panel (%lu so far)", frame.x, frame.y,
            static_cast<unsigned long>(s_rejected));
    return;
  }
  if (!s_down) touchDown();
}

// While the chip is absent it is NOT polled (each failed transaction would log an
// error): begin() is simply retried every two seconds.
void retryTouch(uint32_t now) {
  if (gt911::ready() || now - s_lastRetryMs < cfg::kTouchRetryMs) return;
  s_lastRetryMs = now;
  const gt911::Status before = gt911::info().status;
  gt911::begin(false);
  if (gt911::info().status == before) return;
  if (gt911::ready()) i2cbus::scan();
  s_failStreak = 0;
  s_dirty = true; // the status line changed: show it
}

// ---------- rendering: the only place that refreshes the panel ----------

void render() {
  if (!s_wantFull && panel::stats().partialSinceFull >= cfg::kFullForceAt) {
    logLine("PANEL %u partial refreshes in a row and never a pause: this one is full", cfg::kFullForceAt);
    s_wantFull = true;
  }
  const panel::Kind kind = s_wantFull ? panel::Kind::Full : panel::Kind::Partial;
  const uint32_t t0 = millis();
  screens::draw(s_touch);
  panel::refresh(kind, millis() - t0);
  s_dirty = false;
  s_wantFull = false;

  // Touches made while the screen was updating are discarded, never replayed.
  if (gt911::flush()) {
    if (!s_blocked) logLine("TOUCH finger still down after the refresh: ignored until release");
    s_blocked = true;
    s_lastFrameMs = millis();
  } else {
    touchUp(" (lifted during the refresh)");
  }
  s_lastActivityMs = millis();
}

void pollSerialKeys() {
  while (Serial.available() > 0) {
    const int key = Serial.read();
    switch (key) {
      case 'b': screens::show(screens::Id::Board); s_dirty = true; break;
      case 't': screens::show(screens::Id::Touch); s_dirty = true; break;
      case 'p': screens::show(screens::Id::Panel); s_dirty = true; break;
      case 's': screens::show(screens::Id::Sound); s_dirty = true; break;
      case 'r': s_dirty = true; break;
      case 'f': s_dirty = true; s_wantFull = true; break;
      case 'i': bootReport(); statusReport(); break;
      case '?': logLine("KEYS  b/t/p/s = Board/Touch/Panel/Sound, r = partial refresh, f = full, i = report"); break;
      default: break; // CR, LF and anything else
    }
    s_lastActivityMs = millis();
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  for (uint32_t t = millis(); !Serial && millis() - t < 2000;) delay(10); // USB-CDC: wait for the host, 2 s at most
  Serial.setTxTimeoutMs(Serial ? 100 : 0); // no host (battery, power bank): never block on the log
  Serial.setDebugOutput(true);             // core log_e / log_w lines come out of the same port
  bootReport();

  s_buzzerOk = ledcAttach(cfg::kBuzzer, 1000, 10);
  if (s_buzzerOk) ledcWriteTone(cfg::kBuzzer, 0);
  else logLine("SOUND WARNING ledcAttach(GPIO%u) failed", cfg::kBuzzer);
  playTone(880, 80); // one beep = the firmware runs, whatever the screen does

  panel::begin();
  // Reset + probe first, scan second: the scan then runs on the bus orientation
  // that works and sees the GT911 out of reset.
  if (!gt911::begin(true)) logLine("TOUCH not available: retrying every %lu ms; serial keys still work ('?')",
                                   static_cast<unsigned long>(cfg::kTouchRetryMs));
  i2cbus::scan();
  gauge::poll(true);

  screens::begin();
  s_wantFull = true; // the first screen goes up with a full refresh
  render();
  playTone(880, 60); // two beeps = the first screen has been sent to the panel
  delay(60);
  playTone(1320, 60);
  statusReport();
  logLine("READY tap the screen, or press '?' here for the serial keys");
}

void loop() {
  const uint32_t now = millis();
  pollSerialKeys();
  pollTouch(now);
  retryTouch(now);

  if (now - s_lastGaugeMs >= cfg::kGaugePollMs) { // shown at the next refresh, never worth one of its own
    s_lastGaugeMs = now;
    gauge::poll(false);
  }
  if (now - s_lastHostCheckMs >= 1000) { // never block on a serial port nobody reads
    s_lastHostCheckMs = now;
    Serial.setTxTimeoutMs(Serial ? 100 : 0);
  }

  const uint32_t idleMs = millis() - s_lastActivityMs; // fresh millis(): activity may be newer than `now`
  const bool touching = s_down || s_blocked;
  if (s_dirty) {
    render(); // one refresh per event
  } else if (panel::fullDue() && !touching && idleMs >= cfg::kFullIdleMs) {
    logLine("PANEL %u partial refreshes reached: full refresh in this pause", cfg::kFullEvery);
    s_wantFull = true;
    render();
  } else if (panel::stats().powered && !touching && idleMs >= cfg::kPowerOffIdleMs) {
    panel::powerOff();
  }
  delay(2);
}
