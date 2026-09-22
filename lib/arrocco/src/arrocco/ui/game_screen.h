// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the game screen: two players on one board. Tap a piece, tap a target;
// promotion and resign/draw popups over the board; the clock ticks from onTick().
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class GameScreen final : public Screen {
 public:
  explicit GameScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;

 private:
  enum class Mode : uint8_t { Play, Promotion, Confirm };
  enum Button : int { kNewGame = 0, kUndo, kFlip, kResignDraw, kMenu };

  Action onSquare(chess::Square s);
  Action onButton(int slot);
  Action onPromotionTap(int16_t x, int16_t y);
  Action onConfirmTap(int16_t x, int16_t y);
  void select(chess::Square s);
  void deselect();
  Action playMove(chess::Move m);
  Action endGame();
  Action flagFall(chess::Color side);   // `side` ran out of time
  const char* resignLabel() const;

  Context& ctx_;
  Mode mode_ = Mode::Play;
  chess::Square selected_ = chess::kNoSquare;
  chess::SquareSet targets_;
  chess::Square promotionFrom_ = chess::kNoSquare;
  chess::Square promotionTo_ = chess::kNoSquare;
  uint32_t shownClock_[2] = {0, 0};   // seconds on the glass, per side
};

}  // namespace arrocco::ui
