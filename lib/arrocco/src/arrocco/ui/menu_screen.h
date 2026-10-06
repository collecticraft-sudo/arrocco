// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the main menu, the clock picker it leads to, and the settings screen.
// All three share the single-column button geometry of layout.h.
#pragma once
#include <cstdint>

#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class MenuScreen final : public Screen {
 public:
  explicit MenuScreen(Context& ctx) : ctx_(ctx) {}
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;   // repaints the footer when the power state changes

 private:
  Context& ctx_;
  int shownBattery_ = -2;
  bool shownUsb_ = false;
  uint32_t shownAtMs_ = 0;      // when the footer was last painted
};

// Picking a clock starts the game. With an unfinished game waiting behind "Resume game",
// it asks first ("Start a new game?") over the picker, and only "New game" replaces it.
class ClockPickerScreen final : public Screen {
 public:
  explicit ClockPickerScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override { pendingSlot_ = -1; }
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  Action start(int slot);
  Context& ctx_;
  int pendingSlot_ = -1;        // the clock picked while the question is open; -1 = no question
};

class SettingsScreen final : public Screen {
 public:
  explicit SettingsScreen(Context& ctx) : ctx_(ctx) {}
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  Context& ctx_;
};

// Play vs engine: pick a colour and a level, then go on to the clock picker, which is
// what actually starts the game — exactly as "Two players" does.
class EngineSetupScreen final : public Screen {
 public:
  explicit EngineSetupScreen(Context& ctx) : ctx_(ctx) {}
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  Context& ctx_;
};

}  // namespace arrocco::ui
