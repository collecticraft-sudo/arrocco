// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — menu, clock picker and settings screens. See menu_screen.h.
#include "arrocco/ui/menu_screen.h"

#include <cstdio>

#include <Adafruit_GFX.h>

#include "arrocco/ui/layout.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

constexpr int16_t kCenterX = arrocco::kScreenW / 2;

// Menu slots (top to bottom). Slot 0 only exists while a game is in progress.
enum MenuSlot : int { kSlotResume = 0, kSlotTwoPlayers, kSlotEngine, kSlotPuzzles, kSlotLichess, kSlotSettings };
// Clock picker slots.
enum ClockSlot : int { kSlotOff = 0, kSlot5, kSlot10, kSlot15Inc10, kSlot30, kSlotClockBack };
// Settings slots.
enum SettingsSlot : int { kSlotFlip = 0, kSlotSound, kSlotRefresh, kSlotSettingsBack = 5 };

constexpr ClockPreset kClockBySlot[] = {ClockPreset::Off, ClockPreset::Blitz5, ClockPreset::Rapid10,
                                        ClockPreset::Rapid15Inc10, ClockPreset::Classic30};
constexpr const char* kClockLabels[] = {str::kClockOff, str::kClock5, str::kClock10, str::kClock15Inc10,
                                        str::kClock30};

void drawTitle(Adafruit_GFX& gfx, const char* title, const char* subtitle) {
  drawCentered(gfx, Font::Bold24, title, kCenterX, static_cast<int16_t>(kMenuTitleBaseline - 12));
  if (subtitle != nullptr) drawCentered(gfx, Font::Sans9, subtitle, kCenterX, kMenuSubtitleBaseline);
}

}  // namespace

// ---- menu -----------------------------------------------------------------------------

void MenuScreen::draw(Adafruit_GFX& gfx) {
  drawTitle(gfx, str::kAppTitle, str::kAppSubtitle);
  if (ctx_.gameInProgress()) drawButton(gfx, menuButtonRect(kSlotResume), Font::Bold12, str::kMenuResume);
  drawButton(gfx, menuButtonRect(kSlotTwoPlayers), Font::Bold12, str::kMenuTwoPlayers);
  drawButton(gfx, menuButtonRect(kSlotEngine), Font::Bold12, str::kMenuEngine, false, str::kComingSoon);
  drawButton(gfx, menuButtonRect(kSlotPuzzles), Font::Bold12, str::kMenuPuzzles, false, str::kComingSoon);
  drawButton(gfx, menuButtonRect(kSlotLichess), Font::Bold12, str::kMenuLichess, false, str::kComingSoon);
  drawButton(gfx, menuButtonRect(kSlotSettings), Font::Bold12, str::kMenuSettings);

  // Footer: what the platform knows about power, as of this refresh.
  shownBattery_ = ctx_.platform.batteryPercent();
  shownUsb_ = ctx_.platform.usbPowered();
  char line[40];
  if (shownBattery_ < 0) {
    snprintf(line, sizeof line, "%s", str::kBatteryNoGauge);
  } else {
    snprintf(line, sizeof line, str::kBatteryFmt, shownBattery_);
  }
  drawText(gfx, Font::Sans9, 16, kMenuFooterBaseline, line);
  if (shownUsb_) {
    const int16_t w = textWidth(gfx, Font::Sans9, str::kUsbPowered);
    drawText(gfx, Font::Sans9, static_cast<int16_t>(arrocco::kScreenW - 16 - w), kMenuFooterBaseline,
             str::kUsbPowered);
  }
}

Action MenuScreen::onTap(int16_t x, int16_t y) {
  switch (menuButtonAt(x, y)) {
    case kSlotResume:
      return ctx_.gameInProgress() ? Action::go(ScreenId::Game) : Action::none();
    case kSlotTwoPlayers:
      return Action::go(ScreenId::ClockPicker);
    case kSlotSettings:
      return Action::go(ScreenId::Settings);
    default:
      return Action::none();   // greyed buttons and empty space: nothing to show
  }
}

Action MenuScreen::onTick(uint32_t now) {
  (void)now;
  if (ctx_.platform.batteryPercent() != shownBattery_ || ctx_.platform.usbPowered() != shownUsb_)
    return Action::repaint();
  return Action::none();
}

// ---- clock picker -------------------------------------------------------------------------

void ClockPickerScreen::draw(Adafruit_GFX& gfx) {
  drawTitle(gfx, str::kClockTitle, nullptr);
  for (int slot = kSlotOff; slot <= kSlot30; ++slot)
    drawButton(gfx, menuButtonRect(slot), Font::Bold12, kClockLabels[slot]);
  drawButton(gfx, menuButtonRect(kSlotClockBack), Font::Bold12, str::kBack);
}

Action ClockPickerScreen::onTap(int16_t x, int16_t y) {
  const int slot = menuButtonAt(x, y);
  if (slot < 0) return Action::none();
  if (slot == kSlotClockBack) return Action::go(ScreenId::Menu);
  ctx_.settings.clockPreset = kClockBySlot[slot];
  ctx_.startNewGame(ctx_.platform.millis());
  // Deep is reserved for game start and game end: the one moment a 3 s flash is welcome.
  return Action::go(ScreenId::Game, Refresh::Deep);
}

// ---- settings -----------------------------------------------------------------------------------

void SettingsScreen::draw(Adafruit_GFX& gfx) {
  drawTitle(gfx, str::kSettingsTitle, nullptr);
  const Settings& s = ctx_.settings;
  drawButton(gfx, menuButtonRect(kSlotFlip), Font::Bold12, s.flipByDefault ? str::kSettingFlipOn : str::kSettingFlipOff);
  drawButton(gfx, menuButtonRect(kSlotSound), Font::Bold12, s.sound ? str::kSettingSoundOn : str::kSettingSoundOff);
  drawButton(gfx, menuButtonRect(kSlotRefresh), Font::Bold12,
             s.fewerFlashes ? str::kSettingRefreshFewer : str::kSettingRefreshNormal);
  drawButton(gfx, menuButtonRect(kSlotSettingsBack), Font::Bold12, str::kBack);
}

Action SettingsScreen::onTap(int16_t x, int16_t y) {
  Settings& s = ctx_.settings;
  switch (menuButtonAt(x, y)) {
    case kSlotFlip:    s.flipByDefault = !s.flipByDefault; return Action::repaint();
    case kSlotSound:   s.sound = !s.sound; return Action::repaint();
    case kSlotRefresh: s.fewerFlashes = !s.fewerFlashes; return Action::repaint();
    case kSlotSettingsBack: return Action::go(ScreenId::Menu);
    default: return Action::none();
  }
}

}  // namespace arrocco::ui
