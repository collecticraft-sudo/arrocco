// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the ONE piece renderer. Today a placeholder set drawn by code (a letter
// in a disc); the CollectiCraft bitmap set will replace the body of drawPiece() and
// nothing else has to change.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

class Adafruit_GFX;

namespace arrocco::ui {

// Draws `piece` in the 56 px square whose top-left corner is (x, y). Always paints a
// white halo first, so black pieces stay readable on the dithered dark squares.
void drawPiece(Adafruit_GFX& gfx, int16_t x, int16_t y, chess::Piece piece);

}  // namespace arrocco::ui
