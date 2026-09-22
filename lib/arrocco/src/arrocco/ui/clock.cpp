// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — game clock logic. See clock.h.
#include "arrocco/ui/clock.h"

#include <cstdio>

namespace arrocco::ui {

using chess::Color;
using chess::indexOf;
using chess::opposite;

void GameClock::reset(ClockPreset preset) {
  const ClockSpec spec = clockSpec(preset);
  enabled_ = spec.baseMs > 0;
  remaining_[0] = remaining_[1] = spec.baseMs;
  incrementMs_ = spec.incrementMs;
  running_ = false;
  side_ = Color::White;
  startedMs_ = 0;
}

void GameClock::start(Color side, uint32_t now) {
  if (!enabled_) return;
  side_ = side;
  startedMs_ = now;
  running_ = true;
}

void GameClock::charge(uint32_t now) {
  if (!running_) return;
  const uint32_t spent = now - startedMs_;
  uint32_t& left = remaining_[indexOf(side_)];
  left = spent >= left ? 0 : left - spent;
  startedMs_ = now;
}

void GameClock::stop(uint32_t now) {
  charge(now);
  running_ = false;
}

void GameClock::moveMade(uint32_t now) {
  if (!enabled_) return;
  charge(now);
  if (remaining_[indexOf(side_)] > 0) remaining_[indexOf(side_)] += incrementMs_;
  start(opposite(side_), now);
}

void GameClock::switchTo(Color side, uint32_t now) {
  if (!enabled_) return;
  charge(now);
  start(side, now);
}

uint32_t GameClock::remainingMs(Color side, uint32_t now) const {
  const uint32_t left = remaining_[indexOf(side)];
  if (!running_ || side != side_) return left;
  const uint32_t spent = now - startedMs_;
  return spent >= left ? 0 : left - spent;
}

uint32_t GameClock::shownSeconds(uint32_t remainingMs) {
  const uint32_t step = remainingMs < kClockFastBelowMs ? kClockFastStepMs : kClockSlowStepMs;
  const uint32_t rounded = ((remainingMs + step - 1) / step) * step;
  return rounded / 1000;
}

void GameClock::format(uint32_t seconds, char* out, int outSize) {
  snprintf(out, static_cast<size_t>(outSize), "%02u:%02u", static_cast<unsigned>((seconds / 60) % 100),
           static_cast<unsigned>(seconds % 60));
}

}  // namespace arrocco::ui
