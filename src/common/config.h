// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco hwtest - pins, screen geometry and tunables. Everything adjustable lives here.
#pragma once
#include <Arduino.h>

// Touch axis corrections, applied to the raw GT911 coordinates: swap first, then mirror.
// The Touch screen works out which ones this panel needs and prints them.
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 0
#endif
#ifndef TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 0
#endif

namespace cfg {

constexpr char kTitle[] = "Arrocco hwtest 0.3";

// --- pins: GPIO numbers, XIAO silk names in the comments ---
constexpr uint8_t kEpdRst = 1;    // D0
constexpr uint8_t kEpdCs = 2;     // D1
constexpr uint8_t kEpdBusy = 3;   // D2
constexpr uint8_t kEpdDc = 4;     // D3
constexpr uint8_t kI2cSda = 5;    // D4  touch + MAX17048 (FTS02 header pin A4)
constexpr uint8_t kI2cScl = 6;    // D5  (FTS02 header pin A5)
constexpr uint8_t kTouchRst = 43; // D6  UART0 TX: the ROM boot banner wiggles it at reset.
                                  //     Harmless: the GT911 reset sequence is run afterwards.
constexpr uint8_t kBuzzer = 44;   // D7  KY-006 "S" pin
constexpr uint8_t kEpdSck = 7;    // D8
constexpr uint8_t kTouchInt = 8;  // D9  (later: deep-sleep wake pin)
constexpr uint8_t kEpdMosi = 9;   // D10 (the panel has no MISO)
constexpr uint32_t kI2cHz = 100000; // only 10k pull-ups on the bus: stay at 100 kHz

// --- screen geometry (docs/decisioni.md) ---
constexpr int16_t kScreenW = 800;
constexpr int16_t kScreenH = 480;
constexpr uint8_t kRotation = 0;
constexpr int16_t kBoardX = 16;   // 8 x 56 px squares, 16 px gutters for the coordinates
constexpr int16_t kBoardY = 16;
constexpr int16_t kSquare = 56;
constexpr int16_t kSideX = 480;   // diagnostics column: x = 480..799
constexpr uint8_t kDarkSquareLevel = 4; // Bayer level of the dark squares, n/16 black (4 = 25 %)

// --- refresh policy ---
constexpr uint16_t kFullEvery = 16;          // partial refreshes before a full one is due
constexpr uint16_t kFullForceAt = 32;        // ... but never more than this many in a row
constexpr uint32_t kFullIdleMs = 1500;       // the due full refresh waits for this much quiet
constexpr uint32_t kPowerOffIdleMs = 3000;   // panel high voltage off after this much quiet
constexpr uint32_t kRefreshTooFastMs = 100;  // BUSY wait shorter than this: nothing was refreshed
constexpr uint32_t kRefreshTooSlowMs = 8000; // whole refresh slower than this: BUSY timed out

// --- touch ---
constexpr uint32_t kTouchPollMs = 8;
constexpr uint32_t kTouchSilenceMs = 200;    // no frames for this long = finger lifted
constexpr uint32_t kTouchRetryMs = 2000;     // begin() retry period while the chip is absent
constexpr uint32_t kTouchFlushMs = 40;       // listening window after each refresh
constexpr uint8_t kTouchLostAfter = 10;      // consecutive I2C failures before giving up polling

constexpr uint32_t kGaugePollMs = 10000;

} // namespace cfg
