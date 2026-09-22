// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the Screen interface, the Action a screen returns, the Settings, and the
// Context every screen shares (platform, game, clock). The App owns all of them.
#pragma once
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/platform.h"
#include "arrocco/ui/clock.h"

class Adafruit_GFX;

namespace arrocco::ui {

enum class ScreenId : uint8_t { Menu, ClockPicker, Settings, Game, GameOver };

struct Settings {
  bool flipByDefault = false;
  bool sound = true;
  bool fewerFlashes = false;               // refresh policy: normal / fewer flashes
  ClockPreset clockPreset = ClockPreset::Off;
};

// What a screen wants after handling an event. At most ONE present() follows.
struct Action {
  enum class Kind : uint8_t { None, Repaint, Go };
  Kind kind = Kind::None;
  Refresh refresh = Refresh::Partial;
  bool pause = false;          // Repaint at a natural pause: may be upgraded to Full
  ScreenId next = ScreenId::Menu;

  static Action none() { return Action(); }
  static Action repaint(Refresh r = Refresh::Partial) {
    Action a;
    a.kind = Kind::Repaint;
    a.refresh = r;
    return a;
  }
  static Action pauseRepaint() {
    Action a = repaint();
    a.pause = true;
    return a;
  }
  static Action go(ScreenId next, Refresh r = Refresh::Full) {
    Action a;
    a.kind = Kind::Go;
    a.refresh = r;
    a.next = next;
    return a;
  }
};

enum class Sound : uint8_t { Select, Move, GameOver };

// Shared by every screen. The Game (~25 KB) lives in the App, in static storage.
struct Context {
  Context(Platform& p, chess::Game& g) : platform(p), game(g) {}
  Platform& platform;
  chess::Game& game;
  Settings settings;
  GameClock clock;
  bool flipped = false;        // orientation of the game being played
  bool gameInProgress() const { return game.plyCount() > 0 && !game.isOver(); }
  void startNewGame(uint32_t now);
  void play(Sound s);          // honours settings.sound
};

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void enter() {}                                     // it just became the current screen
  virtual void draw(Adafruit_GFX& gfx) = 0;                   // the whole 800x480 buffer is white
  virtual Action onTap(int16_t x, int16_t y) = 0;
  virtual Action onTick(uint32_t now) { (void)now; return Action::none(); }
};

}  // namespace arrocco::ui
