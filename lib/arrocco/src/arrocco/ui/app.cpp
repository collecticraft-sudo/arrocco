// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — ChessApp: screen switching, tap detection, refresh policy. See app.h.
#include "arrocco/ui/app.h"

#include <cstring>

#include <Adafruit_GFX.h>

#include "arrocco/ui/layout.h"

namespace arrocco::ui {

namespace {

using chess::Color;

// Beeps: select high and short, move lower, game over twice.
constexpr uint16_t kSelectHz = 1200;
constexpr uint16_t kSelectMs = 30;
constexpr uint16_t kMoveHz = 880;
constexpr uint16_t kMoveMs = 40;
constexpr uint16_t kGameOverHz = 660;
constexpr uint16_t kGameOverMs = 80;

// The very first paint after power-up: a Deep clears whatever the panel still shows.
constexpr Refresh kBootRefresh = Refresh::Deep;

int16_t absDiff(int16_t a, int16_t b) { return static_cast<int16_t>(a > b ? a - b : b - a); }

// What of the context goes into the saved game: the game being played, never the setup.
SavedGame savedFrom(const Context& ctx) {
  SavedGame saved;
  saved.vsEngine = ctx.vsEngine;
  saved.engineLevel = static_cast<uint8_t>(clampEngineLevel(ctx.engineLevel));
  saved.humanSide = ctx.humanSide;
  saved.engineColor = ctx.engineColor;
  saved.flipped = ctx.flipped;
  saved.clockPreset = ctx.clock.preset();
  saved.clockMs[0] = ctx.clock.bankedMs(Color::White);
  saved.clockMs[1] = ctx.clock.bankedMs(Color::Black);
  return saved;
}

}  // namespace

// ---- Context ----------------------------------------------------------------------------

void Context::startNewGame(uint32_t now) {
  started = true;
  game.newGame();
  clock.reset(settings.clockPreset);
  clock.start(chess::Color::White, now);
  flipped = settings.flipByDefault;
  if (engine != nullptr) engine->abort();     // a search left over from the game before
  if (!vsEngine) return;

  // The draw has to be a draw, and the only entropy a board with no radio has is the
  // millisecond its owner touched the glass. xorshift32 on that, carried over between
  // games so that two quick starts in a row do not give the same colour twice.
  switch (humanSide) {
    case HumanSide::White: engineColor = chess::Color::Black; break;
    case HumanSide::Black: engineColor = chess::Color::White; break;
    case HumanSide::Random: {
      uint32_t x = rand_ ^ now ^ 0x9E3779B9u;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      rand_ = x;
      engineColor = (x & 1u) ? chess::Color::Black : chess::Color::White;
      break;
    }
  }
  // Playing Black means looking at the board from Black's side, unless the owner
  // asked for the opposite in the settings.
  flipped = (engineColor == chess::Color::White) != settings.flipByDefault;
}

void Context::startSetUpGame(uint32_t now) {
  vsEngine = setup.vsEngine;
  engineLevel = setup.engineLevel;
  humanSide = setup.humanSide;
  startNewGame(now);
}

void Context::resumeClock(uint32_t now) {
  // Only a clock restored at boot stands still in a game in progress: everywhere else it
  // runs from the start of the game to its end, menu included, and must keep its time.
  if (!clock.enabled() || clock.running() || !gameInProgress()) return;
  clock.start(game.sideOfPly(game.plyCount()), now);
}

void Context::play(Sound s) {
  if (!settings.sound) return;
  switch (s) {
    case Sound::Select: platform.beep(kSelectHz, kSelectMs); break;
    case Sound::Move:   platform.beep(kMoveHz, kMoveMs); break;
    case Sound::GameOver:
      platform.beep(kGameOverHz, kGameOverMs);
      platform.beep(kGameOverHz, kGameOverMs);
      break;
  }
}

// ---- ChessApp -----------------------------------------------------------------------------

ChessApp::ChessApp(Platform& platform, Engine* engine)
    : platform_(platform),
      ctx_(platform, game_, engine),
      menu_(ctx_),
      clockPicker_(ctx_),
      settings_(ctx_),
      engineSetup_(ctx_),
      game_screen_(ctx_),
      gameOver_(ctx_) {}

Screen& ChessApp::current() {
  switch (screenId_) {
    case ScreenId::ClockPicker: return clockPicker_;
    case ScreenId::Settings:    return settings_;
    case ScreenId::EngineSetup: return engineSetup_;
    case ScreenId::Game:        return game_screen_;
    case ScreenId::GameOver:    return gameOver_;
    default:                    return menu_;
  }
}

void ChessApp::begin() {
  restoreGame();
  screenId_ = ScreenId::Menu;
  current().enter();
  present(kBootRefresh);
}

// The game is only offered while it can still be played: a finished one, a blob that is
// not a saved game (saved_game.h), or an engine game on a boot whose engine did not start
// leaves the board as after a first start. The blob stays in the store until the next
// game started replaces it, so the engine game comes back on a boot that has the engine.
void ChessApp::restoreGame() {
  storedSize_ = platform_.loadBlob(kSavedGameKey, stored_, sizeof stored_);
  if (storedSize_ == 0) return;
  SavedGame saved;
  if (decodeSavedGame(stored_, storedSize_, game_, saved) != SaveCheck::Ok || game_.isOver() ||
      (saved.vsEngine && !ctx_.engineAvailable())) {
    game_.newGame();
    return;
  }
  ctx_.started = true;
  ctx_.vsEngine = saved.vsEngine;
  ctx_.engineLevel = saved.engineLevel;
  ctx_.humanSide = saved.humanSide;
  ctx_.engineColor = saved.engineColor;
  ctx_.flipped = saved.flipped;
  // The setup screens open on the choices of the game that came back, and "New game"
  // starts another one like it.
  ctx_.setup.vsEngine = saved.vsEngine;
  ctx_.setup.engineLevel = saved.engineLevel;
  ctx_.setup.humanSide = saved.humanSide;
  ctx_.settings.clockPreset = saved.clockPreset;
  // Stopped: Context::resumeClock() starts it when the game is resumed from the menu.
  ctx_.clock.restore(saved.clockPreset, saved.clockMs[0], saved.clockMs[1]);
}

// Nothing is written before a game exists on this power cycle, so a store this firmware
// cannot read (or a finished game) stays as it is until the next game replaces it.
void ChessApp::saveGame() {
  if (!ctx_.started) return;
  const size_t size = encodeSavedGame(game_, savedFrom(ctx_), scratch_, sizeof scratch_);
  if (size == 0 || (size == storedSize_ && memcmp(scratch_, stored_, size) == 0)) return;
  // A write that fails is tried again after the next refresh: the bytes still differ.
  if (!platform_.storeBlob(kSavedGameKey, scratch_, size)) return;
  memcpy(stored_, scratch_, size);
  storedSize_ = size;
}

uint8_t ChessApp::fullThreshold() const {
  return ctx_.settings.fewerFlashes ? kPartialsBeforeFullFewer : kPartialsBeforeFull;
}

// A Partial at a natural pause becomes a Full once ghosting has built up.
Refresh ChessApp::resolve(const Action& action) const {
  if (action.kind == Action::Kind::Repaint && action.refresh == Refresh::Partial && action.pause &&
      partialsSinceFull_ >= fullThreshold())
    return Refresh::Full;
  return action.refresh;
}

void ChessApp::apply(const Action& action) {
  switch (action.kind) {
    case Action::Kind::None:
      return;
    case Action::Kind::Repaint:
      present(resolve(action));
      return;
    case Action::Kind::Go:
      screenId_ = action.next;
      current().enter();
      present(action.refresh);
      return;
  }
}

// Every visible change goes through here: redraw the whole buffer, push it once, and then
// (the glass already shows it) keep whatever changed about the game.
void ChessApp::present(Refresh kind) {
  if (kind == Refresh::Partial) {
    if (partialsSinceFull_ < 255) ++partialsSinceFull_;
  } else {
    partialsSinceFull_ = 0;
  }
  Adafruit_GFX& gfx = platform_.gfx();
  gfx.setTextWrap(false);
  gfx.setTextSize(1);
  gfx.setTextColor(kBlack);
  gfx.fillScreen(kWhite);
  current().draw(gfx);
  platform_.present(kind);
  // present() blocks: read the clock again rather than reuse an earlier value.
  lastActivityMs_ = platform_.millis();
  panelOffSent_ = false;
  saveGame();
}

void ChessApp::onTouch(const TouchEvent& e) {
  lastActivityMs_ = e.ms;
  switch (e.type) {
    case TouchEvent::Down:
      touchDown_ = true;
      downX_ = e.x;
      downY_ = e.y;
      downMs_ = e.ms;
      break;
    case TouchEvent::Move:
      break;
    case TouchEvent::Up: {
      if (!touchDown_) break;
      touchDown_ = false;
      // A tap is a Down and an Up close together; the Down point is the one meant.
      if (absDiff(e.x, downX_) <= kTapSlop && absDiff(e.y, downY_) <= kTapSlop)
        apply(current().onTap(downX_, downY_));
      break;
    }
  }
}

void ChessApp::tick() {
  const uint32_t now = platform_.millis();
  // Never start a refresh under a finger: it would swallow the tap. A finger that never
  // lifts (a missed Up from the touch controller) must not freeze the clock forever.
  if (touchDown_) {
    if (now - downMs_ < kTouchHoldMaxMs) return;
    touchDown_ = false;
  }
  const Action action = current().onTick(now);
  if (action.kind != Action::Kind::None) {
    apply(action);
    return;
  }
  if (!panelOffSent_ && now - lastActivityMs_ >= kPanelOffAfterMs) {
    panelOffSent_ = true;
    platform_.panelOff();
  }
}

}  // namespace arrocco::ui
