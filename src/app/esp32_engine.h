// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware — the arrocco::Engine of the device: CT800 on a FreeRTOS task.
//
// The Arduino loop task has 8 KB of stack and the search needs far more, so the engine
// gets a task of its own: 48 KB of stack, pinned to core 0 (the UI, the panel and the
// touch controller live on core 1), at the idle priority so that the core's IDLE0 task
// keeps running and keeps feeding the 5 s task watchdog.
//
// The transposition tables are claimed ONCE, in begin(): internal RAM first, PSRAM if
// they do not fit — which, at about 640 KB, is what always happens on this board. No
// allocation ever happens per move.
#pragma once

#include <arrocco/engine.h>

namespace arrocco_app {

// Claims the hash memory and starts the search task. Call it from setup(), once, after
// Arduino has brought up PSRAM. False means no engine: log it and leave the menu entry
// greyed by not calling ChessApp::setEngine().
bool engineBegin();

// The one engine object. Valid from the first call, whether begin() succeeded or not.
arrocco::Engine* engine();

// Where the hash ended up and how big it is, for the boot log: "PSRAM, 640 KB,
// 32768 entries per table". Valid after engineBegin().
const char* engineHashInfo();

// The low-water mark of the search task's stack, in free bytes, across every search so
// far; 0 before the first one. The 48 KB above is a reasoned guess until a real board
// reports this, which is why the STAT line prints it.
uint32_t engineStackFreeBytes();

}  // namespace arrocco_app
