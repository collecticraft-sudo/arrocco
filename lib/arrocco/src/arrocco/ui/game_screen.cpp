// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — game screen logic. See game_screen.h.
#include "arrocco/ui/game_screen.h"

#include <cstdio>
#include <cstring>

#include <Adafruit_GFX.h>

#include "arrocco/ui/board_view.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/side_panel.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

using chess::Color;
using chess::Move;
using chess::Square;

void GameScreen::enter() {
  mode_ = Mode::Play;
  deselect();
  ctx_.game.goToLatest();
}

void GameScreen::select(Square s) {
  selected_ = s;
  targets_ = ctx_.game.position().legalTargetsFrom(s);
}

void GameScreen::deselect() {
  selected_ = chess::kNoSquare;
  targets_.clear();
}

const char* GameScreen::resignLabel() const {
  return ctx_.game.position().sideToMove() == Color::White ? str::kWhiteResigns : str::kBlackResigns;
}

// ---- drawing ------------------------------------------------------------------------------

void GameScreen::draw(Adafruit_GFX& gfx) {
  const chess::Game& game = ctx_.game;
  const chess::Position& pos = game.position();

  BoardMarks marks;
  marks.selected = selected_;
  marks.targets = targets_;
  const Move last = game.lastMove();
  if (!last.isNone()) {
    marks.lastFrom = last.from();
    marks.lastTo = last.to();
  }
  marks.checkedKing = pos.checkedKingSquare();
  drawBoard(gfx, pos, marks, ctx_.flipped);

  SidePanelView view;
  view.headline = pos.sideToMove() == Color::White ? str::kWhiteToMove : str::kBlackToMove;
  char lastText[32];
  if (game.currentPly() == 0) {
    view.subline = str::kNoMovesYet;
  } else {
    formatMove(game, game.currentPly() - 1, lastText, 20);
    if (pos.inCheck()) {
      // "12... Qxf7+  Check!" fits the 12 pt line; the ring on the king says it too.
      const size_t used = strlen(lastText);
      snprintf(lastText + used, sizeof lastText - used, "  %s", str::kCheck);
    }
    view.subline = lastText;
  }

  const GameClock& clock = ctx_.clock;
  if (clock.enabled()) {
    const uint32_t now = ctx_.platform.millis();
    view.clock.shown = true;
    for (int side = 0; side < 2; ++side) {
      const Color c = static_cast<Color>(side);
      shownClock_[side] = GameClock::shownSeconds(clock.remainingMs(c, now));
      view.clock.seconds[side] = shownClock_[side];
      view.clock.running[side] = clock.running() && clock.runningSide() == c;
    }
  }

  view.buttons[kNewGame] = str::kButtonNewGame;
  view.buttons[kUndo] = str::kButtonUndo;
  view.buttons[kFlip] = str::kButtonFlip;
  view.buttons[kResignDraw] = str::kButtonResignDraw;
  view.buttons[kMenu] = str::kButtonMenu;
  drawSidePanel(gfx, game, view);

  if (mode_ == Mode::Promotion) drawPromotionPopup(gfx, pos.sideToMove());
  if (mode_ == Mode::Confirm) drawDialog(gfx, confirmDialog(resignLabel()));
}

// ---- input ----------------------------------------------------------------------------------

Action GameScreen::onTap(int16_t x, int16_t y) {
  if (mode_ == Mode::Promotion) return onPromotionTap(x, y);
  if (mode_ == Mode::Confirm) return onConfirmTap(x, y);
  const Square s = squareAt(x, y, ctx_.flipped);
  if (s != chess::kNoSquare) return onSquare(s);
  const int slot = sideButtonAt(x, y);
  if (slot >= 0) return onButton(slot);
  return Action::none();
}

Action GameScreen::onSquare(Square s) {
  const chess::Position& pos = ctx_.game.position();
  const chess::Piece piece = pos.pieceAt(s);
  const bool own = !piece.isNone() && piece.color() == pos.sideToMove();

  if (selected_ == chess::kNoSquare) {
    if (!own) return Action::none();          // nothing to pick up: no refresh at all
    select(s);
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  if (s == selected_) {
    deselect();
    return Action::repaint();
  }
  if (targets_.contains(s)) {
    if (pos.isPromotionMove(selected_, s)) {
      promotionFrom_ = selected_;
      promotionTo_ = s;
      mode_ = Mode::Promotion;
      return Action::repaint();
    }
    return playMove(pos.findLegalMove(selected_, s));
  }
  if (own) {
    select(s);                                 // reselect another own piece
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  deselect();
  return Action::repaint();
}

Action GameScreen::playMove(Move m) {
  const uint32_t now = ctx_.platform.millis();
  deselect();
  mode_ = Mode::Play;
  if (m.isNone()) return Action::repaint();
  // The flag fell while the finger was still on the glass: the move is not played.
  const Color mover = ctx_.game.position().sideToMove();
  if (ctx_.clock.timedOut(mover, now)) return flagFall(mover);
  if (!ctx_.game.play(m)) return Action::repaint();
  ctx_.clock.moveMade(now);
  ctx_.play(Sound::Move);
  if (ctx_.game.isOver()) return endGame();
  // A finished move is the natural pause where a Full refresh is allowed.
  return Action::pauseRepaint();
}

Action GameScreen::endGame() {
  ctx_.clock.stop(ctx_.platform.millis());
  ctx_.play(Sound::GameOver);
  return Action::go(ScreenId::GameOver, Refresh::Deep);
}

Action GameScreen::flagFall(Color side) {
  ctx_.game.declareResult(side == Color::White ? chess::GameResult::BlackWins : chess::GameResult::WhiteWins,
                          chess::GameEndReason::Timeout);
  return endGame();
}

Action GameScreen::onPromotionTap(int16_t x, int16_t y) {
  const int choice = promotionChoiceAt(x, y);
  if (choice < 0) {                            // anywhere else: give up the move
    mode_ = Mode::Play;
    deselect();
    return Action::repaint();
  }
  const Move m = ctx_.game.position().findLegalMove(promotionFrom_, promotionTo_, promotionPiece(choice));
  return playMove(m);
}

Action GameScreen::onConfirmTap(int16_t x, int16_t y) {
  const Dialog d = confirmDialog(resignLabel());
  const int button = dialogButtonAt(d, x, y);
  mode_ = Mode::Play;
  chess::Game& game = ctx_.game;
  if (button == 0) {
    const bool whiteResigns = game.position().sideToMove() == Color::White;
    game.declareResult(whiteResigns ? chess::GameResult::BlackWins : chess::GameResult::WhiteWins,
                       chess::GameEndReason::Resignation);
    return endGame();
  }
  if (button == 1) {
    game.declareResult(chess::GameResult::Draw, chess::GameEndReason::Agreement);
    return endGame();
  }
  return Action::repaint();                    // Cancel, or a tap outside the dialog
}

Action GameScreen::onButton(int slot) {
  const uint32_t now = ctx_.platform.millis();
  switch (slot) {
    case kNewGame:
      ctx_.startNewGame(now);
      mode_ = Mode::Play;
      deselect();
      return Action::repaint(Refresh::Deep);
    case kUndo: {
      if (!ctx_.game.takeBack()) return Action::none();
      deselect();
      ctx_.clock.switchTo(ctx_.game.position().sideToMove(), now);
      return Action::repaint();
    }
    case kFlip:
      ctx_.flipped = !ctx_.flipped;
      // Every square changes: a Full refresh here costs one flash and no ghosting.
      return Action::repaint(Refresh::Full);
    case kResignDraw:
      deselect();
      mode_ = Mode::Confirm;
      return Action::repaint();
    case kMenu:
      deselect();
      return Action::go(ScreenId::Menu);
    default:
      return Action::none();
  }
}

Action GameScreen::onTick(uint32_t now) {
  const GameClock& clock = ctx_.clock;
  if (!clock.enabled() || !clock.running()) return Action::none();
  const Color side = clock.runningSide();
  if (clock.timedOut(side, now)) return flagFall(side);
  if (GameClock::shownSeconds(clock.remainingMs(side, now)) != shownClock_[chess::indexOf(side)])
    return Action::repaint();
  return Action::none();
}

}  // namespace arrocco::ui
