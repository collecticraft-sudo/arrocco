// SPDX-License-Identifier: GPL-3.0-or-later
// The e-paper panel: one persistent full frame buffer, timed refreshes, power policy.
//
// Rules for this panel (GDEY075T7, usePartialUpdateWindow = false):
//  - draw with Adafruit GFX straight into `display`, then call refresh() ONCE per event;
//  - never call setPartialWindow() / firstPage() / nextPage(): they change the buffer stride;
//  - a "partial" refresh is a whole-screen differential refresh (~450 ms).
#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>

namespace panel {

using Epd = GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>; // 48,000-byte buffer

enum class Kind : uint8_t { Partial, Full };
enum class BusyPin : uint8_t { Unknown, IdleHigh, Floating, StuckLow }; // sampled once, after the reset

struct Stats {
  uint32_t lastPartialMs;
  uint32_t lastFullMs;
  uint32_t lastBusyMs;       // part of the last refresh spent waiting on BUSY
  uint16_t partialSinceFull; // real partial refreshes since the last full one
  uint32_t partialTotal;
  uint32_t fullTotal;
  bool powered;              // panel high voltage still on (after a partial refresh)
  bool responding;           // false when BUSY never went active, or a wait timed out
  BusyPin busyPin;
};

extern Epd display;

void begin();
// Pushes the buffer, times it and logs one line. drawMs is only reported.
uint32_t refresh(Kind kind, uint32_t drawMs);
bool fullDue();  // kFullEvery partial refreshes reached
void powerOff(); // call after a few idle seconds; the next refresh pays ~140 ms power-on
const Stats& stats();

} // namespace panel
