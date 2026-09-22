// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — side column drawing. See side_panel.h.
#include "arrocco/ui/side_panel.h"

#include <cstdio>

#include <Adafruit_GFX.h>

#include "arrocco/ui/clock.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

using chess::Color;

void drawClockBlock(Adafruit_GFX& gfx, const ClockView& clock) {
  if (!clock.shown) return;
  char text[8];
  const int16_t xs[2] = {kSideInnerX, kClockColumn2X};
  const char* labels[2] = {str::kWhiteLabel, str::kBlackLabel};
  for (int side = 0; side < 2; ++side) {
    drawText(gfx, Font::Sans9, xs[side], kClockLabelBaseline, labels[side]);
    GameClock::format(clock.seconds[side], text, sizeof text);
    drawText(gfx, Font::Bold18, xs[side], kClockBaseline, text);
    if (clock.running[side]) {
      // A small filled triangle after the label: the side whose time is running.
      const int16_t tx = static_cast<int16_t>(xs[side] + textWidth(gfx, Font::Sans9, labels[side]) + 10);
      const int16_t ty = static_cast<int16_t>(kClockLabelBaseline - 10);
      gfx.fillTriangle(tx, ty, tx, static_cast<int16_t>(ty + 10), static_cast<int16_t>(tx + 8),
                       static_cast<int16_t>(ty + 5), kBlack);
    }
  }
}

// The last kMoveListRows * 2 moves up to the SHOWN position, five per column.
void drawMoveList(Adafruit_GFX& gfx, const chess::Game& game) {
  const int plies = game.currentPly();
  const int moves = (plies + 1) / 2;
  const int slots = kMoveListRows * 2;
  const int firstMove = moves > slots ? moves - slots + 1 : 1;
  char line[24];
  for (int i = 0; i < slots; ++i) {
    const int moveNumber = firstMove + i;
    if (moveNumber > moves) break;
    const int whitePly = (moveNumber - 1) * 2;
    const char* white = whitePly < plies ? game.sanAt(whitePly) : "";
    const char* black = whitePly + 1 < plies ? game.sanAt(whitePly + 1) : "";
    snprintf(line, sizeof line, "%d. %s %s", moveNumber, white, black);
    const int16_t x = i < kMoveListRows ? kSideInnerX : kMoveListColumn2X;
    const int16_t y = static_cast<int16_t>(kMoveListBaseline0 + (i % kMoveListRows) * kMoveListRowH);
    drawText(gfx, Font::Sans9, x, y, line);
  }
}

// Material difference from the captured pieces: what Black took minus what White took.
void drawMaterial(Adafruit_GFX& gfx, const chess::Game& game) {
  const int whiteUp = game.capturedPieces(Color::Black).material() -
                      game.capturedPieces(Color::White).material();
  char line[32];
  if (whiteUp == 0) {
    snprintf(line, sizeof line, "%s", str::kMaterialEven);
  } else {
    snprintf(line, sizeof line, str::kMaterialFmt, whiteUp > 0 ? str::kWhiteLabel : str::kBlackLabel,
             whiteUp > 0 ? whiteUp : -whiteUp);
  }
  drawText(gfx, Font::Sans9, kSideInnerX, kMaterialBaseline, line);
}

}  // namespace

void formatMove(const chess::Game& game, int index, char* out, int outSize) {
  if (index < 0 || index >= game.plyCount()) {
    out[0] = '\0';
    return;
  }
  const bool white = game.sideOfPly(index) == Color::White;
  snprintf(out, static_cast<size_t>(outSize), "%d%s %s", game.moveNumberOfPly(index), white ? "." : "...",
           game.sanAt(index));
}

void drawSidePanel(Adafruit_GFX& gfx, const chess::Game& game, const SidePanelView& view) {
  gfx.setTextColor(kBlack);
  if (view.headline != nullptr) drawText(gfx, Font::Bold18, kSideInnerX, kTurnBaseline, view.headline);
  if (view.subline != nullptr) drawText(gfx, Font::Sans12, kSideInnerX, kLastMoveBaseline, view.subline);
  gfx.drawFastHLine(kSideInnerX, kDivider1Y, kSideInnerW, kBlack);
  drawClockBlock(gfx, view.clock);
  gfx.drawFastHLine(kSideInnerX, kDivider2Y, kSideInnerW, kBlack);
  drawMoveList(gfx, game);
  drawMaterial(gfx, game);
  for (int slot = 0; slot < kSideButtonSlots; ++slot)
    if (view.buttons[slot] != nullptr) drawButton(gfx, sideButtonRect(slot), Font::Bold12, view.buttons[slot]);
}

}  // namespace arrocco::ui
