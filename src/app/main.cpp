// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco - the game firmware. Runs arrocco::ui::ChessApp (lib/arrocco, the same code
// as the Mac simulator) on the XIAO ESP32-S3 Plus through Esp32Platform.
//
// The loop task has 8 KB of stack: the app (~25 KB, it holds the Game) and the
// platform are static, the frame buffer (48,000 bytes) is panel::display, also static.
//
// Power (power.h): after cfg::kSleepAfterIdleMs with no touch, no serial command and
// nothing running, the board shows its sleep screen and goes into deep sleep. A touch
// wakes it, and a wake is a reboot through setup(). The radio is off unless a network
// job needs it (net_wifi.h).
//
// Lichess (arrocco/ui/lichess_state.h): the online game's board and client live in PSRAM,
// allocated in setup() like the engine's hash; the screens reach the network only through
// net::transport() and DeviceAccount (lichess_link.h), whose work runs on the network tasks.
#include <Arduino.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

#include <new>

#include "arrocco/ui/app.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/lichess_state.h"
#include "config.h"
#include "esp32_engine.h"
#include "esp32_platform.h"
#include "gauge.h"
#include "gt911.h"
#include "i2cbus.h"
#include "lichess_link.h"
#include "log.h"
#include "net_console.h"
#include "net_http.h"
#include "net_token.h"
#include "net_wifi.h"
#include "panel.h"
#include "power.h"
#include "touch_policy.h"
#include "touch_tune.h"

namespace {

constexpr char kTitle[] = "Arrocco 0.2";
constexpr uint32_t kTickMs = 50;
// The GT911 is read when its INT line has moved (every report pulses it), at most every
// kTouchPollMs - and every kTouchPollIdleMs anyway, so a quiet INT wire still works,
// just slower. With no finger on the glass that is 10 reads a second instead of 100.
constexpr uint32_t kTouchPollMs = 10;
constexpr uint32_t kTouchPollIdleMs = 100;
constexpr uint32_t kStatMs = 30000;
constexpr uint32_t kLoopRestMs = 4; // the loop has nothing finer than 10 ms to do
// Cold boot: a serial monitor gets up to kUsbHostWaitMs to open the port, but only when a
// USB host is there at all (frames seen within kUsbSeenWithinMs). On battery or a charger
// that used to cost 2 s at every boot; after a wake from deep sleep there is no wait.
constexpr uint32_t kUsbHostWaitMs = 2000;
constexpr uint32_t kUsbSeenWithinMs = 600;
// In a timed game the clock repaints up to once a second (below 20 s). A tap is usually
// followed by another within a second - piece, then target - so the app's own refreshes
// wait kTickQuietAfterTouchMs after a touch, and never more than kTickMaxDeferMs in all,
// rather than land on the second tap.
constexpr uint32_t kTickQuietAfterTouchMs = 700;
constexpr uint32_t kTickMaxDeferMs = 1500;
constexpr uint8_t kMaxRefreshesPerCall = 4; // a replayed tap refreshes again: bounded chain

Esp32Platform s_platform;
arrocco::ui::ChessApp s_app(s_platform); // .bss: never on a task stack
                                         // the engine is attached in setup(): it needs PSRAM
DeviceAccount s_lichessAccount;
arrocco::ui::LichessState* s_lichess = nullptr; // in PSRAM, from setup()

// Touch state machine (mirrors hwtest): Down on the first report with one finger, Move
// while it stays down and the point moved, Up on the release report or when the reports
// stop; palm or several fingers cancel the gesture until everything lifts.
bool s_down = false;     // the app has seen a Down and is owed its Up
bool s_blocked = false;  // the touch in progress is ignored until it lifts
int16_t s_x = 0;         // where the app last saw the finger
int16_t s_y = 0;
int16_t s_downX = 0;     // where it went down: the app taps there
int16_t s_downY = 0;
uint32_t s_downMs = 0;
uint32_t s_lastPollMs = 0;
uint32_t s_lastEdges = 0;
uint32_t s_lastFrameMs = 0;
uint32_t s_lastTouchMs = 0; // last report with a finger, or last Up
uint32_t s_lastRetryMs = 0;
uint32_t s_lastTickMs = 0;
uint32_t s_lastStatMs = 0;
uint32_t s_lastHostCheckMs = 0;
uint32_t s_consoleLines = 0;
uint8_t s_failStreak = 0;
uint32_t s_events = 0;

struct TouchStats {
  uint32_t downs;
  uint32_t taps;      // Ups close to their Down
  uint32_t moved;     // Ups too far from their Down to be a tap
  uint32_t cancelled; // palm or several fingers
  uint32_t timedOut;  // no release report: the reports simply stopped
  uint32_t rejected;  // coordinates outside the panel
  uint32_t heldAfterRefresh;
  uint32_t replayed;  // made during a refresh, delivered after it
  uint32_t dropped;   // made during a refresh, thrown away
};
TouchStats s_touch = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// What the app call in progress may refresh, so that a touch made during that refresh can
// be judged (touch_policy.h). Set just before calling into the app.
struct RefreshCause {
  touch_policy::Origin origin;
  touch_policy::Snapshot before;
  bool triggerOnBoard;
  bool idleBefore;
};
RefreshCause s_cause = {touch_policy::Origin::Boot, {false, 0, 0, false, false}, false, true};

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

// The board on the glass, the offline game's or the online one's: a tap made during a refresh
// is replayed only when this board did not change under it (touch_policy.h).
touch_policy::Snapshot snapshot() {
  const arrocco::chess::Game& game = s_app.boardGame();
  return touch_policy::Snapshot{s_app.boardShown(), game.plyCount(), game.currentPly(), game.isOver(),
                                s_app.boardPopupOpen()};
}

void dispatch(arrocco::TouchEvent::Type type, int16_t x, int16_t y, uint32_t now) {
  // An Up is the only event the app acts on: the refresh it may start belongs to it.
  if (type == arrocco::TouchEvent::Up)
    s_cause = RefreshCause{touch_policy::Origin::Tap, snapshot(),
                           arrocco::ui::kBoardRect.contains(s_downX, s_downY), true};
  arrocco::TouchEvent e;
  e.type = type;
  e.x = x;
  e.y = y;
  e.ms = now;
  ++s_events;
  s_platform.markEvent(now);
  s_app.onTouch(e);
}

void touchUp(uint32_t now, const char* note) {
  if (s_down) {
    s_down = false;
    s_lastTouchMs = now;
    const int dx = abs(s_x - s_downX);
    const int dy = abs(s_y - s_downY);
    const bool tap = dx <= arrocco::ui::kTapSlop && dy <= arrocco::ui::kTapSlop;
    if (tap) ++s_touch.taps;
    else ++s_touch.moved;
    logLine("TOUCH up (%d,%d)%s after %lu ms: %s %d px", s_x, s_y, note,
            static_cast<unsigned long>(now - s_downMs), tap ? "a tap, moved" : "NOT a tap, moved",
            dx > dy ? dx : dy);
    dispatch(arrocco::TouchEvent::Up, s_x, s_y, now);
  }
  s_blocked = false;
}

// Ends a gesture that must not become a tap: the app sees an Up far away from the Down.
void cancelGesture(uint32_t now) {
  if (s_down) {
    s_down = false;
    s_x = -1000;
    s_y = -1000;
    dispatch(arrocco::TouchEvent::Up, s_x, s_y, now);
  }
}

// A tap made during a refresh that left the board as the user saw it (touch_policy.h):
// the app gets it now, at the point where the finger went down.
void replay(int16_t x, int16_t y, bool held, uint32_t now) {
  ++s_touch.replayed;
  ++s_touch.downs;
  logLine("TOUCH a touch during the refresh at (%d,%d) still means the same: %s", x, y,
          held ? "taken, the finger is still down" : "taken as a tap");
  s_blocked = false;
  s_down = true;
  s_x = s_downX = x;
  s_y = s_downY = y;
  s_downMs = now;
  s_lastFrameMs = now;
  s_lastTouchMs = now;
  power::noteActivity(now);
  dispatch(arrocco::TouchEvent::Down, x, y, now);
  if (!held) touchUp(now, " (replayed)");
}

// After each present(): what the glass saw while the panel refreshed is replayed, dropped
// with a reason in the log, or - a finger still on the glass that is not replayed -
// ignored until it lifts. Returns false when there was no refresh to look at.
bool handleRefresh(uint32_t now) {
  bool held = false;
  if (!s_platform.takeRefreshNotice(held)) return false;
  const RefreshCause cause = s_cause;
  const gt911::RefreshTouch rt = gt911::lastRefreshTouch();
  int16_t x = 0;
  int16_t y = 0;
  const bool inPanel = rt.seen && mapTouch(rt.x, rt.y, x, y);
  const touch_policy::DuringRefresh during = {rt.seen && inPanel, rt.single, rt.frames, rt.maxDev,
                                              inPanel && arrocco::ui::kBoardRect.contains(x, y), held};
  const touch_policy::Judgement j =
      touch_policy::judge(cause.origin, cause.triggerOnBoard, cause.idleBefore, cause.before, snapshot(),
                          during, static_cast<uint16_t>(arrocco::ui::kTapSlop));
  if (j.verdict == touch_policy::Verdict::Replay) {
    replay(x, y, held, now);
    return true;
  }
  if (j.verdict == touch_policy::Verdict::Discard) {
    ++s_touch.dropped;
    logLine("TOUCH a touch during the refresh at (%d,%d), %u report(s), dropped: %s", x, y, rt.frames,
            touch_policy::reasonText(j.reason));
  }
  if (held) {
    if (!s_blocked) {
      ++s_touch.heldAfterRefresh;
      logLine("TOUCH finger still down after the refresh: ignored until release");
    }
    cancelGesture(now);
    s_blocked = true;
    s_lastFrameMs = now;
  } else {
    touchUp(now, " (lifted during the refresh)");
  }
  return true;
}

void handleRefreshes() {
  for (uint8_t i = 0; i < kMaxRefreshesPerCall && handleRefresh(millis()); ++i) {
  }
}

void pollTouch(uint32_t now) {
  if (!gt911::ready()) return;
  // The finger lifted without a release report: the reports simply stopped. No I2C needed.
  if ((s_down || s_blocked) && now - s_lastFrameMs > cfg::kTouchSilenceMs) {
    if (s_down) ++s_touch.timedOut;
    touchUp(now, " (no release report, timed out)");
  }
  const uint32_t edges = gt911::intEdges();
  if (now - s_lastPollMs < kTouchPollMs) return;
  if (edges == s_lastEdges && now - s_lastPollMs < kTouchPollIdleMs) return;
  s_lastPollMs = now;
  s_lastEdges = edges;

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
  if (!frame.fresh) return;
  s_lastFrameMs = now;
  power::noteActivity(now);
  if (frame.count == 0) {
    touchUp(now, "");
    return;
  }
  s_lastTouchMs = now;
  if (frame.palm || frame.count > 1) { // palm or several fingers: cancel until everything lifts
    if (!s_blocked) {
      ++s_touch.cancelled;
      logLine("TOUCH gesture cancelled (%s, count %u): ignored until release",
              frame.palm ? "large area" : "multi-touch", frame.count);
    }
    cancelGesture(now);
    s_blocked = true;
    return;
  }
  if (s_blocked) return;
  int16_t x = 0;
  int16_t y = 0;
  if (!mapTouch(frame.x, frame.y, x, y)) {
    ++s_touch.rejected;
    logLine("TOUCH rejected raw=(%u,%u): outside the panel (%lu so far)", frame.x, frame.y,
            static_cast<unsigned long>(s_touch.rejected));
    return;
  }
  if (!s_down) {
    s_x = s_downX = x;
    s_y = s_downY = y;
    s_down = true;
    s_downMs = now;
    ++s_touch.downs;
    logLine("TOUCH down (%d,%d) raw=(%u,%u) size %u", x, y, frame.x, frame.y, frame.size);
    dispatch(arrocco::TouchEvent::Down, x, y, now);
    return;
  }
  if (x != s_x || y != s_y) {
    s_x = x;
    s_y = y;
    dispatch(arrocco::TouchEvent::Move, x, y, now);
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
  s_lastEdges = gt911::intEdges();
}

bool tickDue(uint32_t now) {
  if (now - s_lastTickMs < kTickMs) return false;
  // A clock on the glass, offline or online: the app's repaints wait for the second tap.
  if (s_app.clockTicking() && now - s_lastTouchMs < kTickQuietAfterTouchMs && now - s_lastTickMs < kTickMaxDeferMs)
    return false;
  return true;
}

power::Blockers blockers(uint32_t now) {
  const arrocco::ui::GameClock& clock = s_app.clock();
  power::Blockers b;
  b.touching = s_down || s_blocked;
  b.engineThinking = arrocco_app::engine()->thinking();
  // Counting down: running, and not yet past zero (a clock left running in the menu stops
  // mattering once its flag would have fallen).
  b.clockRunning = clock.enabled() && clock.running() && !clock.timedOut(clock.runningSide(), now);
  b.network = net::wifiBusy();
  b.cannotWake = !gt911::canWake();
  b.gameOnScreen = s_app.currentScreen() == arrocco::ui::ScreenId::Game && !s_app.game().isOver();
  return b;
}

void statusReport() {
  const panel::Stats& st = panel::stats();
  char headline[64];
  gt911::headline(headline, sizeof(headline));
  logLine("STAT  heap %lu KB, PSRAM %lu KB | refreshes %lu partial (last %lu ms) %lu full (last %lu ms), "
          "panel %s, powered %d | %s, frames %lu, INT edges %lu, i2c errors %lu | %s | events %lu, presents %lu"
          " | engine stack free %lu B",
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL),
          static_cast<unsigned long>(ESP.getFreePsram() / 1024UL),
          static_cast<unsigned long>(st.partialTotal), static_cast<unsigned long>(st.lastPartialMs),
          static_cast<unsigned long>(st.fullTotal), static_cast<unsigned long>(st.lastFullMs),
          st.responding ? "responding" : "NOT RESPONDING", st.powered ? 1 : 0, headline,
          static_cast<unsigned long>(gt911::frames()), static_cast<unsigned long>(gt911::intEdges()),
          static_cast<unsigned long>(i2cbus::errors()), gauge::text(), static_cast<unsigned long>(s_events),
          static_cast<unsigned long>(s_platform.presents()),
          static_cast<unsigned long>(arrocco_app::engineStackFreeBytes()));
  logLine("STAT  %s | %s", net::netStatusLine(), power::statusText(millis()));
  logLine("STAT  %s | taps %lu, replayed %lu, dropped during refreshes %lu", net::netStackLine(),
          static_cast<unsigned long>(s_touch.taps), static_cast<unsigned long>(s_touch.replayed),
          static_cast<unsigned long>(s_touch.dropped));
}

bool touchStatsCommand(const char* verb, char*) {
  if (strcmp(verb, "touch-stats") != 0) return false;
  logLine("TOUCH since boot: %lu down, %lu taps, %lu moved too far for a tap, %lu cancelled (palm or two"
          " fingers), %lu ended by silence, %lu outside the panel",
          static_cast<unsigned long>(s_touch.downs), static_cast<unsigned long>(s_touch.taps),
          static_cast<unsigned long>(s_touch.moved), static_cast<unsigned long>(s_touch.cancelled),
          static_cast<unsigned long>(s_touch.timedOut), static_cast<unsigned long>(s_touch.rejected));
  logLine("TOUCH during refreshes: %lu replayed, %lu dropped, %lu fingers still down after one | %lu reports,"
          " %lu INT edges, %lu I2C errors",
          static_cast<unsigned long>(s_touch.replayed), static_cast<unsigned long>(s_touch.dropped),
          static_cast<unsigned long>(s_touch.heldAfterRefresh), static_cast<unsigned long>(gt911::frames()),
          static_cast<unsigned long>(gt911::intEdges()), static_cast<unsigned long>(i2cbus::errors()));
  return true;
}

void touchStatsHelp() { logLine("CMD   touch-stats"); }

// USB-CDC: waits for a serial monitor only when a USB host is there at all, and never
// after a deep-sleep wake (a touch is waiting for the first screen).
void waitForUsbHost(bool wokeFromSleep) {
  if (wokeFromSleep) return;
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < kUsbHostWaitMs) {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
    if (!HWCDC::isPlugged() && millis() - t0 >= kUsbSeenWithinMs) break; // battery, or a charger
#endif
    delay(10);
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  power::begin(); // why we started; the touch RST pin released from its sleep hold
  waitForUsbHost(power::wokeFromSleep());
  Serial.setTxTimeoutMs(Serial ? 100 : 0); // no host (battery, power bank): never block on the log
  Serial.setDebugOutput(true);
  logLine("==== %s ==== built %s %s", kTitle, __DATE__, __TIME__);
  power::logBoot();
  logLine("BOOT  %s, flash %lu MB, PSRAM %lu KB, free heap %lu KB, touch flags SWAP_XY %d MIRROR_X %d MIRROR_Y %d",
          ESP.getChipModel(), static_cast<unsigned long>(ESP.getFlashChipSize() / (1024UL * 1024UL)),
          static_cast<unsigned long>(ESP.getPsramSize() / 1024UL),
          static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL), TOUCH_SWAP_XY, TOUCH_MIRROR_X, TOUCH_MIRROR_Y);

  // Before anything can open a TLS session: mbedTLS keeps a fixed 16 KB input and 16 KB
  // output record buffer per session and this moves the lot to PSRAM. Once a session
  // exists the buffers are already placed and the call comes too late.
  net::mbedtlsUsePsram();

  s_platform.begin();
  SPI.begin(cfg::kEpdSck, -1, cfg::kEpdMosi, -1); // panel::begin() repeats it; the second call is a no-op
  panel::begin();
  // After a deep sleep the GT911 has been running all along: no reset (it would recalibrate
  // with the waking finger on the glass), no address test, no bus scan.
  const bool resumed = power::wokeFromSleep() && gt911::resume(true);
  if (!resumed) {
    // Reset + probe first, scan second: the scan then runs on the bus orientation that works.
    if (!gt911::begin(true))
      logLine("TOUCH not available: retrying every %lu ms; the app runs but cannot be tapped",
              static_cast<unsigned long>(cfg::kTouchRetryMs));
    i2cbus::scan();
  }
  // Every refresh now polls the touch controller while it waits on the panel.
  panel::setBusyHook(gt911::refreshPoll);
  gauge::poll(true);

  // The engine claims its hash and starts its search task here, not at static init:
  // PSRAM is only usable once Arduino has brought it up. Without it the board still
  // plays two-player games, and the menu entry stays greyed.
  if (arrocco_app::engineBegin()) {
    s_app.setEngine(arrocco_app::engine());
    logLine("ENGI  CT800 V1.46 ready: hash %s", arrocco_app::engineHashInfo());
  } else {
    logLine("ENGI  no engine: %s ('Play vs engine' stays greyed)", arrocco_app::engineHashInfo());
  }

  // Network last: nothing here turns the radio on (net_wifi.h); the console is ready.
  net::httpBegin();
  net::wifiBegin();
  net::consoleBegin();
  net::consoleAddCommands(power::consoleCommand, power::consoleHelp);
  net::consoleAddCommands(touch_tune::consoleCommand, touch_tune::consoleHelp);
  net::consoleAddCommands(touchStatsCommand, touchStatsHelp);

  // Lichess: the online game's board (25 KB) and its client (9 KB) go to PSRAM. Without it the
  // menu entry stays greyed, as it does for the engine.
  void* lichessMemory = heap_caps_malloc(sizeof(arrocco::ui::LichessState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (lichessMemory) {
    s_lichess = new (lichessMemory) arrocco::ui::LichessState(net::transport(), s_lichessAccount);
    s_app.setLichess(s_lichess);
    logLine("LICH  ready: %u bytes in PSRAM, token %s", static_cast<unsigned>(sizeof(arrocco::ui::LichessState)),
            net::tokenPresent() ? "present" : "absent");
  } else {
    logLine("LICH  no PSRAM for it: 'Lichess' stays greyed");
  }

  const uint32_t now = millis();
  s_lastRetryMs = now;
  s_lastStatMs = now;
  s_lastHostCheckMs = now;
  s_lastTickMs = now;
  s_lastEdges = gt911::intEdges();
  s_platform.markEvent(now);
  s_cause = RefreshCause{touch_policy::Origin::Boot, snapshot(), false, true};
  if (power::wakeIntoGame()) s_app.wakeIntoGame(); // the board fell asleep on a game: back to it
  s_app.begin(); // draws and presents the first screen
  handleRefreshes();
  s_platform.beep(880, 60); // the first screen is on the panel
  power::noteActivity(millis());
  logLine("READY %s", gauge::text());
}

void loop() {
  // Every call below may end in a refresh (0.5-1.5 s, blocking): the clock is read again
  // after each one. A stale `now` would time out the touch silence at once, or measure
  // the beep gap from before the refresh and cut the next beep short.
  pollTouch(millis()); // a tap ends in present()
  handleRefreshes();
  retryTouch(millis());

  uint32_t now = millis();
  if (tickDue(now)) {
    s_lastTickMs = now;
    s_cause = RefreshCause{touch_policy::Origin::Tick, snapshot(), false, !s_down && !s_blocked};
    s_platform.markEvent(now);
    s_app.tick(); // clocks, the engine's move, deferred refreshes, panelOff()
    handleRefreshes();
  }

  s_platform.service(); // beeps and gauge, on its own fresh clock

  // WiFi state machine, captive portal and the serial commands. Microseconds while the
  // radio is off, which is most of the time. The one exception is WebServer::handleClient(),
  // which the Arduino core writes with blocking reads and a 5 s client timeout: the server
  // only exists while the setup portal is open or while an OAuth login is coming back, as
  // net_wifi.h explains. Everything to do with Lichess runs on its own tasks.
  net::wifiService();
  net::consoleService();
  if (net::consoleLines() != s_consoleLines) { // someone is typing commands: not idle
    s_consoleLines = net::consoleLines();
    power::noteActivity(millis());
  }

  now = millis();
  if (now - s_lastHostCheckMs >= 1000) { // never block on a serial port nobody reads (as hwtest)
    s_lastHostCheckMs = now;
    Serial.setTxTimeoutMs(Serial ? 100 : 0);
  }
  if (now - s_lastStatMs >= kStatMs) {
    s_lastStatMs = now;
    statusReport();
  }
  power::service(millis(), blockers); // may not return: deep sleep
  delay(kLoopRestMs);
}
