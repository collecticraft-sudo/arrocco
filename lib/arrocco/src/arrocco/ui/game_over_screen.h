// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — game over: the final position with the result overlay (New game /
// Review / Menu). Review hides the overlay and adds < > buttons that browse the game.
#pragma once
#include <cstdint>

#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class GameOverScreen final : public Screen {
 public:
  explicit GameOverScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  enum Button : int { kPrev = 0, kNext, kFlip, kResult, kNewGame, kMenu };

  Action onOverlayTap(int16_t x, int16_t y);
  Action onReviewTap(int16_t x, int16_t y);
  Action newGame();
  const char* resultText() const;
  const char* reasonText() const;

  Context& ctx_;
  bool reviewing_ = false;
};

}  // namespace arrocco::ui
