// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — ChessApp: the arrocco::App the firmware and the simulator run. Owns the
// Game (static storage, ~25 KB), the screens, the settings and the refresh policy.
//
// Refresh policy ("few flashes", docs/decisioni.md): ONE present() per user-visible
// event, never two for one tap. Partial normally; Full when the screen changes, or at
// a natural pause (a move was just played) once partialsSinceFull reached the
// threshold; Deep at game start and game end. panelOff() after kPanelOffAfterMs
// without touches, from tick().
//
// Stack: the firmware calls this from an 8 KB loop task. Nothing here holds a large
// local; the frame buffer lives in the platform, the Game in this object.
#pragma once
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/platform.h"
#include "arrocco/ui/game_over_screen.h"
#include "arrocco/ui/game_screen.h"
#include "arrocco/ui/menu_screen.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class ChessApp final : public arrocco::App {
 public:
  // `engine` may be null: the build then has no engine, and the menu entry says so.
  // It must outlive the app, and the app is the only thing that ever calls it.
  explicit ChessApp(Platform& platform, Engine* engine = nullptr);

  // Attaches the engine after construction, which is what the firmware needs: the app
  // is a static object, but the engine cannot claim its PSRAM or start its task before
  // setup() has run. Call it before begin(); a null pointer greys the menu entry.
  void setEngine(Engine* engine) { ctx_.engine = engine; }

  void begin() override;
  void onTouch(const TouchEvent& e) override;
  void tick() override;

  // For tests and the simulator's status line.
  ScreenId currentScreen() const { return screenId_; }
  uint8_t partialsSinceFull() const { return partialsSinceFull_; }
  const chess::Game& game() const { return game_; }

 private:
  Screen& current();
  void apply(const Action& action);
  Refresh resolve(const Action& action) const;
  void present(Refresh kind);
  uint8_t fullThreshold() const;

  Platform& platform_;
  chess::Game game_;
  Context ctx_;
  MenuScreen menu_;
  ClockPickerScreen clockPicker_;
  SettingsScreen settings_;
  EngineSetupScreen engineSetup_;
  GameScreen game_screen_;
  GameOverScreen gameOver_;
  ScreenId screenId_ = ScreenId::Menu;

  bool touchDown_ = false;
  int16_t downX_ = 0;
  int16_t downY_ = 0;
  uint32_t downMs_ = 0;
  uint32_t lastActivityMs_ = 0;   // last touch or present()
  bool panelOffSent_ = true;
  uint8_t partialsSinceFull_ = 0;
};

}  // namespace arrocco::ui
