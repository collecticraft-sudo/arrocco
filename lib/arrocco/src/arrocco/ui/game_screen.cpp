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
  // Coming back from the menu (or straight from the clock picker with the engine on
  // White): whoever is to move has to be asked again, because Menu aborted the search.
  startEngine();
}

// ---- the engine ---------------------------------------------------------------------------

void GameScreen::startEngine() {
  engineWaiting_ = false;
  if (mode_ != Mode::Play || !ctx_.engineToMove()) return;
  const chess::Game& game = ctx_.game;

  // The engine wants the whole line, for the repetition rule. A game too long for the
  // buffer gets the current position on its own instead: it loses the history, not the
  // move. Both buffers are members, never locals: the loop task has 8 KB of stack.
  bool wholeLine = true;
  engineFen_[0] = '\0';
  engineMoves_[0] = '\0';
  if (game.uciMoveList(engineMoves_, sizeof engineMoves_) < 0) {
    engineMoves_[0] = '\0';
    wholeLine = false;
  }
  const chess::Position& from = wholeLine ? game.startPosition() : game.position();
  // "" means the standard starting position to every implementation of the seam.
  if (!(wholeLine && game.startedFromStartpos()) &&
      from.toFen(engineFen_, sizeof engineFen_) <= 0)
    return;

  ctx_.engine->start(engineFen_, engineMoves_, ctx_.engineLevel, 0);
  engineWaiting_ = true;
}

void GameScreen::stopEngine() {
  if (!engineWaiting_) return;
  engineWaiting_ = false;
  ctx_.engine->abort();
}

// The answer arrived. An engine that hands over something the rules refuse must not be
// able to stall the board, so the first legal move is played instead and the game goes
// on; test/engine checks that this never has to happen.
Action GameScreen::playEngineMove(const char* uci) {
  chess::Move m = ctx_.game.position().parseUci(uci);
  if (m.isNone()) {
    chess::MoveList legal;
    ctx_.game.position().generateLegalMoves(legal);
    if (legal.empty()) return Action::pauseRepaint();   // the rules already ended the game
    m = legal[0];
  }
  return playMove(m);
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
  char thinkingText[40];
  if (engineWaiting_) {
    const int level = clampEngineLevel(ctx_.engineLevel);
    view.headline = str::kEngineThinking;
    snprintf(thinkingText, sizeof thinkingText, str::kEngineLevelLineFmt, level + 1,
             kEngineLevels[level].name);
    view.subline = thinkingText;
  } else if (game.currentPly() == 0) {
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
  // While the engine has the move, its pieces are not the player's to push around.
  if (ctx_.engineToMove()) return Action::none();
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
  // Ask the engine BEFORE the repaint: one present() then shows the move just played
  // and "Thinking..." together, instead of flashing the panel twice for one tap.
  startEngine();
  // A finished move is the natural pause where a Full refresh is allowed.
  return Action::pauseRepaint();
}

Action GameScreen::endGame() {
  stopEngine();
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
  // Cancel, or a tap outside the dialog. The engine was never stopped: if its answer
  // is already in, the next tick plays it.
  return Action::repaint();
}

Action GameScreen::onButton(int slot) {
  const uint32_t now = ctx_.platform.millis();
  switch (slot) {
    case kNewGame:
      stopEngine();
      ctx_.startNewGame(now);
      mode_ = Mode::Play;
      deselect();
      startEngine();                 // the engine may have White
      return Action::repaint(Refresh::Deep);
    case kUndo: {
      stopEngine();
      if (!ctx_.game.takeBack()) return Action::none();
      // Against the engine, one Undo undoes one MOVE of the player's: keep going back
      // until it is the player's turn again (or the line runs out).
      if (ctx_.vsEngine) {
        while (ctx_.game.position().sideToMove() == ctx_.engineColor && ctx_.game.plyCount() > 0) {
          if (!ctx_.game.takeBack()) break;
        }
      }
      deselect();
      ctx_.clock.switchTo(ctx_.game.position().sideToMove(), now);
      startEngine();                 // only fires if the undo landed on the engine's turn
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
      stopEngine();                  // nothing may repaint over the menu
      deselect();
      return Action::go(ScreenId::Menu);
    default:
      return Action::none();
  }
}

Action GameScreen::onTick(uint32_t now) {
  // The engine first: its move is the only thing here that changes the position.
  // While a popup is open the answer stays in the engine until the popup closes.
  if (engineWaiting_ && mode_ == Mode::Play && !ctx_.engine->thinking()) {
    char uci[6];
    if (ctx_.engine->take(uci)) {
      engineWaiting_ = false;
      return playEngineMove(uci);
    }
    engineWaiting_ = false;          // aborted, or nothing to give: do not poll for ever
  }

  const GameClock& clock = ctx_.clock;
  if (!clock.enabled() || !clock.running()) return Action::none();
  const Color side = clock.runningSide();
  if (clock.timedOut(side, now)) return flagFall(side);
  if (GameClock::shownSeconds(clock.remainingMs(side, now)) != shownClock_[chess::indexOf(side)])
    return Action::repaint();
  return Action::none();
}

}  // namespace arrocco::ui
