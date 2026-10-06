// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — ChessApp: the arrocco::App the firmware and the simulator run. Owns the
// Game (static storage, ~25 KB), the screens, the settings and the refresh policy.
//
// Refresh policy ("few flashes", docs/decisioni.md): ONE present() per user-visible
// event, never two for one tap. Partial normally; Full when the screen changes, or at
// a natural pause (a move was just played) once partialsSinceFull reached the
// threshold, unless the side to move is down to its last minute; Deep at game start and
// game end. panelOff() after kPanelOffAfterMs without touches, from tick().
//
// Power cuts (saved_game.h): after every present() the game is encoded again and, when
// the bytes differ from what the store already holds, written with Platform::storeBlob().
// So a move costs one refresh and then one small write, in that order, and a tap that
// changes nothing worth keeping (a selection, a clock repaint) writes nothing. begin()
// reads the store back: an unfinished game becomes the menu's "Resume game".
//
// Stack: the firmware calls this from an 8 KB loop task. Nothing here holds a large
// local; the frame buffer lives in the platform, the Game in this object.
#pragma once
#include <cstddef>
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/platform.h"
#include "arrocco/ui/game_over_screen.h"
#include "arrocco/ui/game_screen.h"
#include "arrocco/ui/menu_screen.h"
#include "arrocco/ui/puzzle_screen.h"
#include "arrocco/ui/saved_game.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class ChessApp final : public arrocco::App {
 public:
  // `engine` may be null: the build then has no engine, and the menu entry says so.
  // It must outlive the app, and the app is the only thing that ever calls it.
  explicit ChessApp(Platform& platform, Engine* engine = nullptr);

  // Attaches the engine after construction, which is what the firmware needs: the app
  // is a static object, but the engine cannot claim its PSRAM or start its task before
  // setup() has run. Call it before begin(); a null pointer greys the menu entry, and
  // begin() then does not offer a saved game against the engine.
  void setEngine(Engine* engine) { ctx_.engine = engine; }

  // Call before begin(): a touch woke the board from a sleep that began on the game screen
  // (or on a puzzle, see boardOnScreen()). begin() then goes straight back to the saved game,
  // or to the puzzle, instead of the menu.
  void wakeIntoGame() { wakeIntoGame_ = true; }

  void begin() override;
  void onTouch(const TouchEvent& e) override;
  void tick() override;

  // For tests and the simulator's status line.
  ScreenId currentScreen() const { return screenId_; }
  uint8_t partialsSinceFull() const { return partialsSinceFull_; }
  const chess::Game& game() const { return game_; }
  // For the firmware's sleep and touch policy (src/app/main.cpp): no deep sleep while the
  // game clock counts down, and no tap replayed after a refresh while a popup covers the board.
  const GameClock& clock() const { return ctx_.clock; }
  bool gamePopupOpen() const { return game_screen_.popupOpen(); }
  // A board someone may be thinking about is on the glass: an unfinished game, or a puzzle.
  // The sleep then keeps it in view, and the wake comes back to it (src/app/power.h).
  bool boardOnScreen() const {
    return (screenId_ == ScreenId::Game && !game_.isOver()) || (screenId_ == ScreenId::Puzzle && puzzle_.showsBoard());
  }

 private:
  Screen& current();
  void apply(const Action& action);
  Refresh resolve(const Action& action) const;
  bool inTimeTrouble() const;    // the side to move has under kNoFullBelowClockMs left
  void present(Refresh kind);
  uint8_t fullThreshold() const;
  void restoreGame();    // begin(): the game a power cut interrupted, if the store has one
  void saveGame();       // after present(): writes the game when it changed

  Platform& platform_;
  chess::Game game_;
  Context ctx_;
  MenuScreen menu_;
  ClockPickerScreen clockPicker_;
  SettingsScreen settings_;
  EngineSetupScreen engineSetup_;
  GameScreen game_screen_;
  GameOverScreen gameOver_;
  PuzzleScreen puzzle_;
  ScreenId screenId_ = ScreenId::Menu;
  bool wakeIntoGame_ = false;

  bool touchDown_ = false;
  int16_t downX_ = 0;
  int16_t downY_ = 0;
  uint32_t downMs_ = 0;
  uint32_t lastActivityMs_ = 0;   // last touch or present()
  bool panelOffSent_ = true;
  uint8_t partialsSinceFull_ = 0;

  // The saved game: what the store holds (as written, or as read at boot) and a scratch
  // buffer to encode into. Members, not locals: 2 KB each, and the loop task has 8 KB.
  uint8_t stored_[kSavedGameMaxBytes] = {};
  size_t storedSize_ = 0;
  uint8_t scratch_[kSavedGameMaxBytes] = {};
};

}  // namespace arrocco::ui
