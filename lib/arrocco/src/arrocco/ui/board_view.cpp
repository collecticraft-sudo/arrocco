// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — board drawing. See board_view.h.
#include "arrocco/ui/board_view.h"

#include <Adafruit_GFX.h>

#include "arrocco/ui/pieces.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

using chess::Square;

// Screen column/row (0..7 from the top-left) of a square.
int columnOf(Square s, bool flipped) { return flipped ? 7 - chess::fileOf(s) : chess::fileOf(s); }
int rowOf(Square s, bool flipped) { return flipped ? chess::rankOf(s) : 7 - chess::rankOf(s); }

// 1-bit panel: "dark" is a 25 % diagonal hatch. Connected lines survive the fast partial
// waveform better than isolated dots (docs/ricerca/gxepd2.json).
void hatchSquare(Adafruit_GFX& gfx, const Rect& r) {
  for (int16_t py = 0; py < r.h; ++py)
    for (int16_t px = 0; px < r.w; ++px)
      if (((r.x + px + r.y + py) & 3) == 0)
        gfx.drawPixel(static_cast<int16_t>(r.x + px), static_cast<int16_t>(r.y + py), kBlack);
}

void drawCornerMarks(Adafruit_GFX& gfx, const Rect& r) {
  const int16_t c = kLastMoveCorner;
  const int16_t x1 = static_cast<int16_t>(r.x + r.w - 1);
  const int16_t y1 = static_cast<int16_t>(r.y + r.h - 1);
  gfx.fillTriangle(r.x, r.y, static_cast<int16_t>(r.x + c), r.y, r.x, static_cast<int16_t>(r.y + c), kBlack);
  gfx.fillTriangle(x1, r.y, static_cast<int16_t>(x1 - c), r.y, x1, static_cast<int16_t>(r.y + c), kBlack);
  gfx.fillTriangle(r.x, y1, static_cast<int16_t>(r.x + c), y1, r.x, static_cast<int16_t>(y1 - c), kBlack);
  gfx.fillTriangle(x1, y1, static_cast<int16_t>(x1 - c), y1, x1, static_cast<int16_t>(y1 - c), kBlack);
}

void drawRing(Adafruit_GFX& gfx, int16_t cx, int16_t cy, int16_t radius, int16_t width) {
  for (int16_t i = 0; i < width; ++i) gfx.drawCircle(cx, cy, static_cast<int16_t>(radius - i), kBlack);
}

void drawSquare(Adafruit_GFX& gfx, const chess::Position& pos, const BoardMarks& marks, Square s,
                bool flipped) {
  const Rect r = squareRect(s, flipped);
  if (!chess::isLightSquare(s)) hatchSquare(gfx, r);

  const chess::Piece piece = pos.pieceAt(s);
  if (s == marks.lastFrom || s == marks.lastTo) drawCornerMarks(gfx, r);
  if (s == marks.selected)
    for (int16_t inset = 1; inset <= kSelectFrame; ++inset)
      gfx.drawRect(static_cast<int16_t>(r.x + inset), static_cast<int16_t>(r.y + inset),
                   static_cast<int16_t>(r.w - 2 * inset), static_cast<int16_t>(r.h - 2 * inset), kBlack);

  drawPiece(gfx, r.x, r.y, piece);

  if (marks.targets.contains(s)) {
    if (piece.isNone()) {
      gfx.fillCircle(r.cx(), r.cy(), static_cast<int16_t>(kTargetDotRadius + 2), kWhite);
      gfx.fillCircle(r.cx(), r.cy(), kTargetDotRadius, kBlack);
    } else {
      drawRing(gfx, r.cx(), r.cy(), kTargetRingRadius, kTargetRingWidth);
    }
  }
  if (s == marks.checkedKing) drawRing(gfx, r.cx(), r.cy(), kCheckRingRadius, 2);
}

// Coordinates live in the 16 px gutters: ranks on the left, files under the board. The
// classic 5x7 font doubled is 10x14 px, the largest that fits them.
void drawCoordinates(Adafruit_GFX& gfx, bool flipped) {
  gfx.setFont(nullptr);
  gfx.setTextSize(2);
  gfx.setTextColor(kBlack);
  for (int i = 0; i < 8; ++i) {
    const int row = flipped ? i : 7 - i;        // where rank i+1 is drawn
    const int column = flipped ? 7 - i : i;     // where file 'a'+i is drawn
    gfx.setCursor(2, static_cast<int16_t>(kBoardY + row * kSquare + (kSquare - 14) / 2));
    gfx.write(static_cast<uint8_t>('1' + i));
    gfx.setCursor(static_cast<int16_t>(kBoardX + column * kSquare + (kSquare - 10) / 2),
                  static_cast<int16_t>(kBoardY + kBoardPx + 2));
    gfx.write(static_cast<uint8_t>('a' + i));
  }
  gfx.setTextSize(1);
}

}  // namespace

Rect squareRect(Square s, bool flipped) {
  return Rect{static_cast<int16_t>(kBoardX + columnOf(s, flipped) * kSquare),
              static_cast<int16_t>(kBoardY + rowOf(s, flipped) * kSquare), kSquare, kSquare};
}

Square squareAt(int16_t x, int16_t y, bool flipped) {
  if (!kBoardRect.contains(x, y)) return chess::kNoSquare;
  const int column = (x - kBoardX) / kSquare;
  const int row = (y - kBoardY) / kSquare;
  const int file = flipped ? 7 - column : column;
  const int rank = flipped ? row : 7 - row;
  return chess::makeSquare(file, rank);
}

void drawBoard(Adafruit_GFX& gfx, const chess::Position& pos, const BoardMarks& marks, bool flipped) {
  for (Square s = 0; s < 64; ++s) drawSquare(gfx, pos, marks, s, flipped);
  gfx.drawRect(static_cast<int16_t>(kBoardX - 1), static_cast<int16_t>(kBoardY - 1),
               static_cast<int16_t>(kBoardPx + 2), static_cast<int16_t>(kBoardPx + 2), kBlack);
  drawCoordinates(gfx, flipped);
}

chess::PieceType promotionPiece(int choice) {
  switch (choice) {
    case 1:  return chess::PieceType::Rook;
    case 2:  return chess::PieceType::Bishop;
    case 3:  return chess::PieceType::Knight;
    default: return chess::PieceType::Queen;
  }
}

void drawPromotionPopup(Adafruit_GFX& gfx, chess::Color side) {
  drawBox(gfx, kPromotionBox);
  drawCentered(gfx, Font::Bold18, str::kPromoteTitle, kPromotionBox.cx(), kPromotionTitleBaseline);
  for (int i = 0; i < kPromotionChoices; ++i) {
    const Rect r = promotionButtonRect(i);
    gfx.drawRoundRect(r.x, r.y, r.w, r.h, 8, kBlack);
    gfx.drawRoundRect(static_cast<int16_t>(r.x + 1), static_cast<int16_t>(r.y + 1),
                      static_cast<int16_t>(r.w - 2), static_cast<int16_t>(r.h - 2), 7, kBlack);
    // The piece as it will appear on the board, centred in the button.
    drawPiece(gfx, static_cast<int16_t>(r.cx() - kSquare / 2), static_cast<int16_t>(r.cy() - kSquare / 2),
              chess::Piece(side, promotionPiece(i)));
  }
}

int promotionChoiceAt(int16_t x, int16_t y) {
  for (int i = 0; i < kPromotionChoices; ++i)
    if (promotionButtonRect(i).contains(x, y)) return i;
  return -1;
}

}  // namespace arrocco::ui
