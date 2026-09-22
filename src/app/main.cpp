// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco - the game firmware. Runs arrocco::ui::ChessApp (lib/arrocco, the same code
// as the Mac simulator) on the XIAO ESP32-S3 Plus through Esp32Platform.
//
// The loop task has 8 KB of stack: the app (~25 KB, it holds the Game) and the
// platform are static, the frame buffer (48,000 bytes) is panel::display, also static.
#include <Arduino.h>
#include <SPI.h>

#include "arrocco/ui/app.h"
#include "config.h"
#include "esp32_platform.h"
#include "gauge.h"
#include "gt911.h"
#include "i2cbus.h"
#include "log.h"
#include "panel.h"

namespace {

constexpr char kTitle[] = "Arrocco 0.1";
constexpr uint32_t kTickMs = 50;
constexpr uint32_t kTouchPollMs = 10; // ~100 Hz
constexpr uint32_t kStatMs = 30000;

Esp32Platform s_platform;
arrocco::ui::ChessApp s_app(s_platform); // .bss: never on a task stack

// touch state machine (mirrors hwtest): Down on the first fresh frame with one finger,
// Move while it stays down and the point moved, Up on the release frame or when the
// reports stop; palm or several fingers cancel the gesture until everything lifts.
bool s_down = false;
bool s_blocked = false;
int16_t s_x = 0;
int16_t s_y = 0;
uint32_t s_lastPollMs = 0;
uint32_t s_lastFrameMs = 0;
uint32_t s_lastRetryMs = 0;
uint32_t s_lastTickMs = 0;
uint32_t s_lastStatMs = 0;
uint32_t s_lastHostCheckMs = 0;
uint8_t s_failStreak = 0;
uint32_t s_rejected = 0;
uint32_t s_events = 0;

// Raw GT911 coordinates -> screen pixels: swap first, then mirror (config.h flags).
// false = outside the panel: not a coordinate at all.
bool mapTouch(uint16_t rawX, uint16_t rawY, int16_t& outX, int16_t& outY) {
  const uint16_t limitX = TOUCH_SWAP_XY ? cfg::kScreenH : cfg::kScreenW;
  const uint16_t limitY = TOUCH_SWAP_XY ? cfg::kScreenW : cfg::kScreenH;
  if (rawX >= limitX || rawY >= limitY) return false;
  int16_t x = static_cast<int16_t>(TOUCH_SWAP_XY ? rawY : rawX);
  int16_t y = static_cast<int16_t>(TOUCH_SWAP_XY ? rawX : rawY);
  if (TOUCH_MIRROR_X) x = static_cast<int16_t>(cfg::kScreenW - 1 - x);
  if (TOUCH_MIRROR_Y) y = static_cast<int16_t>(cfg::kScreenH - 1 - y);
  outX = x;
  outY = y;
  return true;
}

void dispatch(arrocco::TouchEvent::Type type, uint32_t now) {
  arrocco::TouchEvent e;
  e.type = type;
  e.x = s_x;
  e.y = s_y;
  e.ms = now;
  ++s_events;
  s_platform.markEvent(now);
  s_app.onTouch(e);
}

void touchUp(uint32_t now, const char* note) {
  if (s_down) {
    logLine("TOUCH up (%d,%d)%s", s_x, s_y, note);
    s_down = false;
    dispatch(arrocco::TouchEvent::Up, now);
  }
  s_blocked = false;
}

// Ends a gesture that must not become a tap: the app sees an Up far away from the Down.
void cancelGesture(uint32_t now) {
  if (s_down) {
    s_down = false;
    s_x = -1000;
    s_y = -1000;
    dispatch(arrocco::TouchEvent::Up, now);
  }
}

// After a refresh the controller was flushed: a finger still on the glass is ignored
// until it lifts; one that lifted meanwhile has its release frame already discarded.
void afterRefresh(uint32_t now) {
  bool held = false;
  if (!s_platform.takeRefreshNotice(held)) return;
  if (held) {
    if (!s_blocked) logLine("TOUCH finger still down after the refresh: ignored until release");
    cancelGesture(now);
    s_blocked = true;
    s_lastFrameMs = now;
  } else {
    touchUp(now, " (lifted during the refresh)");
  }
}

void pollTouch(uint32_t now) {
  if (!gt911::ready() || now - s_lastPollMs < kTouchPollMs) return;
  s_lastPollMs = now;

  gt911::Frame frame;
  if (!gt911::read(frame)) { // I2C failure: the frame is unusable, never a coordinate
    if (++s_failStreak >= cfg::kTouchLostAfter) {
      gt911::markLost();
      logLine("TOUCH controller lost: retrying begin() every %lu ms", static_cast<unsigned long>(cfg::kTouchRetryMs));
      cancelGesture(now);
      s_blocked = false;
      s_lastRetryMs = now;
    }
    return;
  }
  s_failStreak = 0;

  if (!frame.fresh) { // the chip reports continuously while touched: silence = finger lifted
    if ((s_down || s_blocked) && now - s_lastFrameMs > cfg::kTouchSilenceMs)
      touchUp(now, " (no release frame, timed out)");
    return;
  }
  s_lastFrameMs = now;
  if (frame.count == 0) {
    touchUp(now, "");
    return;
  }
  if (frame.palm || frame.count > 1) { // palm or several fingers: cancel until everything lifts
    if (!s_blocked) logLine("TOUCH gesture cancelled (%s, count %u): ignored until release",
                            frame.palm ? "large area" : "multi-touch", frame.count);
    cancelGesture(now);
    s_blocked = true;
    return;
  }
  if (s_blocked) return;
  int16_t x = 0;
  int16_t y = 0;
  if (!mapTouch(frame.x, frame.y, x, y)) {
    ++s_rejected;
    logLine("TOUCH rejected raw=(%u,%u): outside the panel (%lu so far)", frame.x, frame.y,
            static_cast<unsigned long>(s_rejected));
    return;
  }
  if (!s_down) {
    s_x = x;
    s_y = y;
    s_down = true;
    logLine("TOUCH down (%d,%d) raw=(%u,%u)", x, y, frame.x, frame.y);
    dispatch(arrocco::TouchEvent::Down, now);
    return;
  }
  if (x != s_x || y != s_y) {
    s_x = x;
    s_y = y;
    dispatch(arrocco::TouchEvent::Move, now);
  }
}

// While the chip is absent it is NOT polled (each failed transaction would log an
// error): begin() is simply retried every two seconds. The app keeps running.
void retryTouch(uint32_t now) {
  if (gt911::ready() || now - s_lastRetryMs < cfg::kTouchRetryMs) return;
  s_lastRetryMs = now;
  const gt911::Status before = gt911::info().status;
  gt911::begin(false);
  if (gt911::info().status == before) return;
  char headline[64];
  gt911::headline(headline, sizeof(headline));
  logLine("TOUCH status changed: %s %s", headline, gt911::hint());
  if (gt911::ready()) i2cbus::scan();
  s_failStreak = 0;
}

void statusReport() {
  const panel::Stats& st = panel::stats();
  char headline[64];
  gt911::headline(headline, sizeof(headline));
  logLine("STAT  heap %lu KB, PSRAM %lu KB | refreshes %lu partial (last %lu ms) %lu full (last %lu ms), "
          "panel %s, powered %d | %s, frames %lu, i2c errors %lu | %s | events %lu, presents %lu",
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL),
          static_cast<unsigned long>(ESP.getFreePsram() / 1024UL),
          static_cast<unsigned long>(st.partialTotal), static_cast<unsigned long>(st.lastPartialMs),
          static_cast<unsigned long>(st.fullTotal), static_cast<unsigned long>(st.lastFullMs),
          st.responding ? "responding" : "NOT RESPONDING", st.powered ? 1 : 0, headline,
          static_cast<unsigned long>(gt911::frames()), static_cast<unsigned long>(i2cbus::errors()),
          gauge::text(), static_cast<unsigned long>(s_events),
          static_cast<unsigned long>(s_platform.presents()));
}

} // namespace

void setup() {
  Serial.begin(115200);
  for (uint32_t t = millis(); !Serial && millis() - t < 2000;) delay(10); // USB-CDC: wait for the host, 2 s at most
  Serial.setTxTimeoutMs(Serial ? 100 : 0); // no host (battery, power bank): never block on the log
  Serial.setDebugOutput(true);
  logLine("==== %s ==== built %s %s", kTitle, __DATE__, __TIME__);
  logLine("BOOT  %s, flash %lu MB, PSRAM %lu KB, free heap %lu KB, touch flags SWAP_XY %d MIRROR_X %d MIRROR_Y %d",
          ESP.getChipModel(), static_cast<unsigned long>(ESP.getFlashChipSize() / (1024UL * 1024UL)),
          static_cast<unsigned long>(ESP.getPsramSize() / 1024UL),
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL), TOUCH_SWAP_XY, TOUCH_MIRROR_X, TOUCH_MIRROR_Y);

  s_platform.begin();
  SPI.begin(cfg::kEpdSck, -1, cfg::kEpdMosi, -1); // panel::begin() repeats it; the second call is a no-op
  panel::begin();
  // Reset + probe first, scan second: the scan then runs on the bus orientation that works.
  if (!gt911::begin(true))
    logLine("TOUCH not available: retrying every %lu ms; the app runs but cannot be tapped",
            static_cast<unsigned long>(cfg::kTouchRetryMs));
  i2cbus::scan();
  gauge::poll(true);

  const uint32_t now = millis();
  s_lastRetryMs = now;
  s_lastStatMs = now;
  s_lastHostCheckMs = now;
  s_platform.markEvent(now);
  s_app.begin(); // draws and presents the first screen
  afterRefresh(millis());
  s_platform.beep(880, 60); // the first screen is on the panel
  logLine("READY %s", gauge::text());
}

void loop() {
  // Every call below may end in a refresh (0.5-1.5 s, blocking): the clock is read again
  // after each one. A stale `now` would time out the touch silence at once, or measure
  // the beep gap from before the refresh and cut the next beep short.
  pollTouch(millis()); // a tap ends in present()
  afterRefresh(millis());
  retryTouch(millis());

  uint32_t now = millis();
  if (now - s_lastTickMs >= kTickMs) {
    s_lastTickMs = now;
    s_platform.markEvent(now);
    s_app.tick(); // clocks, deferred refreshes, panelOff()
    afterRefresh(millis());
  }

  s_platform.service(); // beeps and gauge, on its own fresh clock

  now = millis();
  if (now - s_lastHostCheckMs >= 1000) { // never block on a serial port nobody reads (as hwtest)
    s_lastHostCheckMs = now;
    Serial.setTxTimeoutMs(Serial ? 100 : 0);
  }
  if (now - s_lastStatMs >= kStatMs) {
    s_lastStatMs = now;
    statusReport();
  }
  delay(1);
}
