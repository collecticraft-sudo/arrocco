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
};

class ClockPickerScreen final : public Screen {
 public:
  explicit ClockPickerScreen(Context& ctx) : ctx_(ctx) {}
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  Context& ctx_;
};

class SettingsScreen final : public Screen {
 public:
  explicit SettingsScreen(Context& ctx) : ctx_(ctx) {}
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;

 private:
  Context& ctx_;
};

}  // namespace arrocco::ui
