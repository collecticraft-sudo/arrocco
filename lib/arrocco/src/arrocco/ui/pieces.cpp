// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — placeholder piece set: white piece = white disc with a black ring and a
// black letter; black piece = black disc with a white letter. See pieces.h.
#include "arrocco/ui/pieces.h"

#include <Adafruit_GFX.h>

#include "arrocco/ui/layout.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {
constexpr int16_t kDiscRadius = 20;
constexpr int16_t kHaloRadius = kDiscRadius + 2;
}  // namespace

void drawPiece(Adafruit_GFX& gfx, int16_t x, int16_t y, chess::Piece piece) {
  if (piece.isNone()) return;
  const bool white = piece.color() == chess::Color::White;
  const int16_t cx = static_cast<int16_t>(x + kSquare / 2);
  const int16_t cy = static_cast<int16_t>(y + kSquare / 2);

  gfx.fillCircle(cx, cy, kHaloRadius, kWhite);
  if (white) {
    gfx.drawCircle(cx, cy, kDiscRadius, kBlack);
    gfx.drawCircle(cx, cy, static_cast<int16_t>(kDiscRadius - 1), kBlack);
  } else {
    gfx.fillCircle(cx, cy, kDiscRadius, kBlack);
  }
  const char letter[2] = {chess::letterOf(piece.type()), '\0'};
  setFont(gfx, Font::Bold18);
  gfx.setTextColor(white ? kBlack : kWhite);
  drawCentered(gfx, Font::Bold18, letter, cx, cy);
  gfx.setTextColor(kBlack);
}

}  // namespace arrocco::ui
