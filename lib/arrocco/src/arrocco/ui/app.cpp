// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — ChessApp: screen switching, tap detection, refresh policy. See app.h.
#include "arrocco/ui/app.h"

#include <Adafruit_GFX.h>

#include "arrocco/ui/layout.h"

namespace arrocco::ui {

namespace {

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

}  // namespace

// ---- Context ----------------------------------------------------------------------------

void Context::startNewGame(uint32_t now) {
  started = true;
  game.newGame();
  clock.reset(settings.clockPreset);
  clock.start(chess::Color::White, now);
  flipped = settings.flipByDefault;
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

ChessApp::ChessApp(Platform& platform)
    : platform_(platform),
      ctx_(platform, game_),
      menu_(ctx_),
      clockPicker_(ctx_),
      settings_(ctx_),
      game_screen_(ctx_),
      gameOver_(ctx_) {}

Screen& ChessApp::current() {
  switch (screenId_) {
    case ScreenId::ClockPicker: return clockPicker_;
    case ScreenId::Settings:    return settings_;
    case ScreenId::Game:        return game_screen_;
    case ScreenId::GameOver:    return gameOver_;
    default:                    return menu_;
  }
}

void ChessApp::begin() {
  screenId_ = ScreenId::Menu;
  current().enter();
  present(kBootRefresh);
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

// Every visible change goes through here: redraw the whole buffer, push it once.
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
