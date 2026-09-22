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
// Engine setup slots.
enum EngineSlot : int { kSlotSide = 0, kSlotLevel, kSlotEngineStart, kSlotEngineBack = 5 };

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
  drawButton(gfx, menuButtonRect(kSlotEngine), Font::Bold12, str::kMenuEngine, ctx_.engineAvailable(),
             ctx_.engineAvailable() ? nullptr : str::kEngineMissing);
  drawButton(gfx, menuButtonRect(kSlotPuzzles), Font::Bold12, str::kMenuPuzzles, false, str::kComingSoon);
  drawButton(gfx, menuButtonRect(kSlotLichess), Font::Bold12, str::kMenuLichess, false, str::kComingSoon);
  drawButton(gfx, menuButtonRect(kSlotSettings), Font::Bold12, str::kMenuSettings);

  // Footer: what the platform knows about power, as of this refresh.
  shownBattery_ = ctx_.platform.batteryPercent();
  shownUsb_ = ctx_.platform.usbPowered();
  shownAtMs_ = ctx_.platform.millis();
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
      ctx_.vsEngine = false;
      return Action::go(ScreenId::ClockPicker);
    case kSlotEngine:
      if (!ctx_.engineAvailable()) return Action::none();
      return Action::go(ScreenId::EngineSetup);
    case kSlotSettings:
      return Action::go(ScreenId::Settings);
    default:
      return Action::none();   // greyed buttons and empty space: nothing to show
  }
}

// The footer follows the power state, but a gauge that jitters by a point must not
// keep the panel flashing: USB changes repaint at once, the percentage only when it moved
// by kBatteryRepaintStep and at most every kBatteryRepaintMinMs.
Action MenuScreen::onTick(uint32_t now) {
  if (ctx_.platform.usbPowered() != shownUsb_) return Action::repaint();
  const int battery = ctx_.platform.batteryPercent();
  if (battery == shownBattery_ || now - shownAtMs_ < kBatteryRepaintMinMs) return Action::none();
  if (battery < 0 || shownBattery_ < 0) return Action::repaint();     // the gauge came or went
  const int diff = battery > shownBattery_ ? battery - shownBattery_ : shownBattery_ - battery;
  return diff >= kBatteryRepaintStep ? Action::repaint() : Action::none();
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

// ---- play vs engine: colour and level --------------------------------------------------

namespace {

const char* humanSideLabel(HumanSide side) {
  switch (side) {
    case HumanSide::White:  return str::kEngineSideWhite;
    case HumanSide::Black:  return str::kEngineSideBlack;
    default:                return str::kEngineSideRandom;
  }
}

}  // namespace

void EngineSetupScreen::draw(Adafruit_GFX& gfx) {
  drawTitle(gfx, str::kEngineTitle, str::kEngineHint);
  drawButton(gfx, menuButtonRect(kSlotSide), Font::Bold12, humanSideLabel(ctx_.humanSide));

  char level[48];
  const int index = clampEngineLevel(ctx_.engineLevel);
  snprintf(level, sizeof level, str::kEngineLevelFmt, index + 1, kEngineLevels[index].name);
  drawButton(gfx, menuButtonRect(kSlotLevel), Font::Bold12, level);

  drawButton(gfx, menuButtonRect(kSlotEngineStart), Font::Bold12, str::kEngineStart);
  drawButton(gfx, menuButtonRect(kSlotEngineBack), Font::Bold12, str::kBack);
}

Action EngineSetupScreen::onTap(int16_t x, int16_t y) {
  switch (menuButtonAt(x, y)) {
    case kSlotSide:
      ctx_.humanSide = static_cast<HumanSide>((static_cast<int>(ctx_.humanSide) + 1) % 3);
      return Action::repaint();
    case kSlotLevel:
      ctx_.engineLevel = (clampEngineLevel(ctx_.engineLevel) + 1) % kEngineLevelCount;
      return Action::repaint();
    case kSlotEngineStart:
      if (!ctx_.engineAvailable()) return Action::none();
      // The clock picker starts the game; it only has to know which kind of game.
      ctx_.vsEngine = true;
      return Action::go(ScreenId::ClockPicker);
    case kSlotEngineBack:
      return Action::go(ScreenId::Menu);
    default:
      return Action::none();
  }
}

}  // namespace arrocco::ui
