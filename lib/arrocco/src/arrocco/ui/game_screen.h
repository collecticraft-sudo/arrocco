// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the game screen: two players on one board, or one player against the
// engine. Tap a piece, tap a target; promotion and resign/draw popups over the board;
// the clock ticks from onTick().
//
// The engine never blocks the screen. When it is its turn, startEngine() hands the
// position to arrocco::Engine and the side panel says "Thinking..."; onTick() polls
// take() and plays the move with the one present() the refresh policy allows. Undo,
// New game and Menu abort the search; the resign/draw and new-game popups only hold the
// answer back until they are closed, because a move that repainted the board under an
// open dialog would take the dialog away with it.
//
// Against the engine the human resigns for their own colour, whoever is to move, and
// there is no draw by agreement: the engine never agreed to anything. "New game" in an
// unfinished game with moves in it asks first, and so does "Menu", in its own way: the
// clock stops while the game is off the glass, and "Resume game" starts it again.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"
#include "arrocco/ui/screen.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

class GameScreen final : public Screen {
 public:
  explicit GameScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;

 private:
  enum class Mode : uint8_t { Play, Promotion, Confirm, ConfirmNewGame };
  enum Button : int { kNewGame = 0, kUndo, kFlip, kResignDraw, kMenu };

  Action onSquare(chess::Square s);
  void startEngine();                   // if it is the engine's turn and nothing is in the way
  void stopEngine();                    // abort and forget whatever it was going to answer
  Action playEngineMove(const char* uci);
  Action onButton(int slot);
  Action onPromotionTap(int16_t x, int16_t y);
  Action onConfirmTap(int16_t x, int16_t y);
  Action onNewGameTap(int16_t x, int16_t y);
  Action newGame(uint32_t now);
  void select(chess::Square s);
  void deselect();
  Action playMove(chess::Move m);
  Action endGame();
  Action flagFall(chess::Color side);   // `side` ran out of time
  chess::Color resigningSide() const;   // the human against the engine, else the side to move
  Dialog confirmDialogNow() const;

  Context& ctx_;
  Mode mode_ = Mode::Play;
  chess::Square selected_ = chess::kNoSquare;
  chess::SquareSet targets_;
  bool engineWaiting_ = false;          // a search was started and its answer is still wanted
  char engineFen_[kEngineFenSize] = {};       // scratch for startEngine(): never on the stack,
  char engineMoves_[kEngineMovesSize] = {};   // the loop task has 8 KB of it
  chess::Square promotionFrom_ = chess::kNoSquare;
  chess::Square promotionTo_ = chess::kNoSquare;
  uint32_t shownClock_[2] = {0, 0};   // seconds on the glass, per side
};

}  // namespace arrocco::ui
