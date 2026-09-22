// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the ONE piece renderer: the CollectiCraft set, two 1-bit bitmaps per
// piece. To change how the pieces look, redraw them in assets/pieces and regenerate
// piece_bitmaps.h; nothing here or above needs to change.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

class Adafruit_GFX;

namespace arrocco::ui {

// Draws `piece` in the 56 px square whose top-left corner is (x, y).
void drawPiece(Adafruit_GFX& gfx, int16_t x, int16_t y, chess::Piece piece);

// The same in two passes, for callers that need to slip something between them. The halo
// is the white silhouette that keeps a black piece readable on a dithered dark square;
// it reaches 2 px past the piece, so it would wipe out a marker drawn beforehand. Draw
// the halo, then the marker, then the ink, and the marker sits behind the piece instead.
void drawPieceHalo(Adafruit_GFX& gfx, int16_t x, int16_t y, chess::Piece piece);
void drawPieceInk(Adafruit_GFX& gfx, int16_t x, int16_t y, chess::Piece piece);

}  // namespace arrocco::ui
