// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the side column (x = 480..799): headline, last move, the two clocks,
// the last ten moves in two columns, the material difference and six button slots.
#pragma once
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/chess/types.h"
#include "arrocco/ui/layout.h"

class Adafruit_GFX;

namespace arrocco::ui {

struct ClockView {
  bool shown = false;                       // false = the clock block stays blank
  uint32_t seconds[2] = {0, 0};             // [White], [Black]
  bool running[2] = {false, false};         // an arrow marks the running side
};

struct SidePanelView {
  const char* headline = nullptr;           // "White to move", "Checkmate", ...
  const char* subline = nullptr;            // "1. e4", "Check!", "Move 3 of 7", ...
  ClockView clock;
  const char* buttons[kSideButtonSlots] = {};  // nullptr = empty slot
};

void drawSidePanel(Adafruit_GFX& gfx, const chess::Game& game, const SidePanelView& view);

// "1. e4" / "1... e5" for ply `index` of the game (0-based) into `out` (at least 16 chars).
void formatMove(const chess::Game& game, int index, char* out, int outSize);

}  // namespace arrocco::ui
