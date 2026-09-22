// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the board: squares, coordinates, pieces, markers, and the promotion
// popup drawn over it. Pure drawing and geometry; no game state of its own.
#pragma once
#include <cstdint>

#include "arrocco/chess/position.h"
#include "arrocco/chess/types.h"
#include "arrocco/ui/layout.h"

class Adafruit_GFX;

namespace arrocco::ui {

// What to draw on top of the position.
struct BoardMarks {
  chess::Square selected = chess::kNoSquare;
  chess::SquareSet targets;                     // legal targets: dot on empty, ring on capture
  chess::Square lastFrom = chess::kNoSquare;    // corner marks
  chess::Square lastTo = chess::kNoSquare;
  chess::Square checkedKing = chess::kNoSquare; // ring around the king in check
};

// Screen rectangle of a square; `flipped` puts Black at the bottom.
Rect squareRect(chess::Square s, bool flipped);
// The square under a screen point, or kNoSquare outside the board.
chess::Square squareAt(int16_t x, int16_t y, bool flipped);

void drawBoard(Adafruit_GFX& gfx, const chess::Position& pos, const BoardMarks& marks, bool flipped);

// Promotion popup: four big buttons (Q R B N) over the board.
void drawPromotionPopup(Adafruit_GFX& gfx, chess::Color side);
int promotionChoiceAt(int16_t x, int16_t y);          // 0..3, or -1
chess::PieceType promotionPiece(int choice);          // Queen, Rook, Bishop, Knight

}  // namespace arrocco::ui
