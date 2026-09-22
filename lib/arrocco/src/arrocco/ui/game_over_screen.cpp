// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — game over screen. See game_over_screen.h.
#include "arrocco/ui/game_over_screen.h"

#include <cstdio>

#include <Adafruit_GFX.h>

#include "arrocco/ui/board_view.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/side_panel.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

using chess::GameEndReason;
using chess::GameResult;

void GameOverScreen::enter() {
  reviewing_ = false;
  ctx_.game.goToLatest();
}

const char* GameOverScreen::resultText() const {
  switch (ctx_.game.result()) {
    case GameResult::WhiteWins: return str::kWhiteWins;
    case GameResult::BlackWins: return str::kBlackWins;
    case GameResult::Draw:      return str::kDraw;
    default:                    return "";
  }
}

const char* GameOverScreen::reasonText() const {
  switch (ctx_.game.reason()) {
    case GameEndReason::Checkmate:            return str::kByCheckmate;
    case GameEndReason::Stalemate:            return str::kByStalemate;
    case GameEndReason::FiftyMove:            return str::kByFiftyMove;
    case GameEndReason::ThreefoldRepetition:  return str::kByRepetition;
    case GameEndReason::InsufficientMaterial: return str::kByMaterial;
    case GameEndReason::Resignation:          return str::kByResignation;
    case GameEndReason::Timeout:              return str::kByTimeout;
    case GameEndReason::Agreement:            return str::kByAgreement;
    default:                                  return "";
  }
}

void GameOverScreen::draw(Adafruit_GFX& gfx) {
  const chess::Game& game = ctx_.game;
  const chess::Position& pos = game.position();

  BoardMarks marks;
  const chess::Move last = game.lastMove();
  if (!last.isNone()) {
    marks.lastFrom = last.from();
    marks.lastTo = last.to();
  }
  marks.checkedKing = pos.checkedKingSquare();
  drawBoard(gfx, pos, marks, ctx_.flipped);

  SidePanelView view;
  view.headline = resultText();
  char subline[32];
  if (reviewing_ && game.currentPly() == 0) {
    snprintf(subline, sizeof subline, str::kReviewStartFmt, game.plyCount());
  } else if (reviewing_) {
    char shown[16];
    formatMove(game, game.currentPly() - 1, shown, sizeof shown);
    snprintf(subline, sizeof subline, str::kReviewFmt, shown, game.currentPly(), game.plyCount());
  } else {
    snprintf(subline, sizeof subline, "%s", reasonText());
  }
  view.subline = subline;
  if (ctx_.clock.enabled()) {
    view.clock.shown = true;
    const uint32_t now = ctx_.platform.millis();
    for (int side = 0; side < 2; ++side)
      view.clock.seconds[side] = GameClock::shownSeconds(ctx_.clock.remainingMs(static_cast<chess::Color>(side), now));
  }
  if (reviewing_) {
    view.buttons[kPrev] = str::kButtonPrev;
    view.buttons[kNext] = str::kButtonNext;
    view.buttons[kFlip] = str::kButtonFlip;
    view.buttons[kResult] = str::kButtonResult;
    view.buttons[kNewGame] = str::kButtonNewGame;
    view.buttons[kMenu] = str::kButtonMenu;
  }
  drawSidePanel(gfx, game, view);

  if (!reviewing_) drawDialog(gfx, gameOverDialog(resultText(), reasonText()));
}

Action GameOverScreen::onTap(int16_t x, int16_t y) {
  return reviewing_ ? onReviewTap(x, y) : onOverlayTap(x, y);
}

Action GameOverScreen::newGame() {
  ctx_.startNewGame(ctx_.platform.millis());
  return Action::go(ScreenId::Game, Refresh::Deep);
}

Action GameOverScreen::onOverlayTap(int16_t x, int16_t y) {
  const Dialog d = gameOverDialog(resultText(), reasonText());
  switch (dialogButtonAt(d, x, y)) {
    case 0: return newGame();
    case 1: reviewing_ = true; return Action::repaint();
    case 2: return Action::go(ScreenId::Menu);
    default: return Action::none();
  }
}

Action GameOverScreen::onReviewTap(int16_t x, int16_t y) {
  chess::Game& game = ctx_.game;
  switch (sideButtonAt(x, y)) {
    // Browsing changes the whole board and nobody is on the clock: a natural pause.
    case kPrev:    return game.stepBack() ? Action::pauseRepaint() : Action::none();
    case kNext:    return game.stepForward() ? Action::pauseRepaint() : Action::none();
    case kFlip:    ctx_.flipped = !ctx_.flipped; return Action::repaint(Refresh::Full);
    case kResult:  reviewing_ = false; game.goToLatest(); return Action::repaint();
    case kNewGame: return newGame();
    case kMenu:    game.goToLatest(); return Action::go(ScreenId::Menu);
    default:       return Action::none();
  }
}

}  // namespace arrocco::ui
