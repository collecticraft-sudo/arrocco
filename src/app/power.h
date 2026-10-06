// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - deep sleep, and waking up from it.
//
// After cfg::kSleepAfterIdleMs with no touch and no serial command, and only when nothing
// is running (no engine search, no game clock counting down, no network job, no finger on
// the glass), the board draws its sleep screen, hibernates the panel, leaves the GT911
// powered and scanning, and enters deep sleep. The GT911's next report pulls INT (GPIO8)
// LOW and wakes it through ext0.
//
// A wake from deep sleep is a reset: setup() runs again and the board boots normally,
// with three differences - no 2 s wait for a serial monitor, no reset of the GT911 (see
// gt911::resume), and a WAKE line in the log. Whatever the user was doing is gone unless
// the app saved it; this module does not save or restore any game.
#pragma once
#include <Arduino.h>

namespace power {

// First thing in setup(), right after Serial.begin(): reads why the chip started,
// updates the counters kept in RTC memory, gives the touch pins back. Logs nothing
// (the serial monitor may not be there yet): logBoot() does.
void begin();
void logBoot();
bool wokeFromSleep();  // this boot is a deep-sleep wake, whatever woke it

// A touch or a serial command: the idle time starts again.
void noteActivity(uint32_t now);

// What keeps the board awake even when it has been idle long enough.
struct Blockers {
  bool touching;        // a finger is on the glass, or is being ignored until it lifts
  bool engineThinking;  // the engine is searching
  bool clockRunning;    // a game clock is counting down
  bool network;         // the radio is on: a request, a stream, the portal or a login
  bool cannotWake;      // the touch controller could not wake the board: never sleep
};

// Call every loop. Once a second it asks `query` what is running; when the idle time is
// up and nothing blocks it, it goes to sleep and does not return. Logs once, each time
// the reason it waits changes.
using BlockerQuery = Blockers (*)(uint32_t now);
void service(uint32_t now, BlockerQuery query);

// The sleep itself: sleep screen, panel hibernate, radio off, pins prepared, ext0 on INT.
[[noreturn]] void sleepNow(const char* why);

// Serial commands: sleep-status, sleep-now, sleep-after <seconds>.
bool consoleCommand(const char* verb, char* rest);
void consoleHelp();

// For the STAT line: "sleep in 182 s" / "sleep due, waiting: clock" / "sleep off".
const char* statusText(uint32_t now);

} // namespace power
