// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the game clock, pure logic on top of Platform::millis() values so it is
// testable without a platform. Time keeps running through a blocking present(): the
// remaining time is computed from the moment the running side was started, whenever
// it is asked for, so a long refresh is simply charged to the side to move.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

namespace arrocco::ui {

enum class ClockPreset : uint8_t { Off, Blitz5, Rapid10, Rapid15Inc10, Classic30, Count };

struct ClockSpec {
  uint32_t baseMs;
  uint32_t incrementMs;
};
constexpr ClockSpec clockSpec(ClockPreset p) {
  switch (p) {
    case ClockPreset::Blitz5:       return ClockSpec{5u * 60000u, 0};
    case ClockPreset::Rapid10:      return ClockSpec{10u * 60000u, 0};
    case ClockPreset::Rapid15Inc10: return ClockSpec{15u * 60000u, 10000u};
    case ClockPreset::Classic30:    return ClockSpec{30u * 60000u, 0};
    default:                        return ClockSpec{0, 0};
  }
}

// Repaint cadence on e-ink: the shown value changes every kSlowStepMs normally and
// every kFastStepMs once fewer than kFastBelowMs remain.
constexpr uint32_t kClockSlowStepMs = 10000;
constexpr uint32_t kClockFastStepMs = 1000;
constexpr uint32_t kClockFastBelowMs = 20000;

class GameClock {
 public:
  void reset(ClockPreset preset);  // both sides at the base time, nobody running
  bool enabled() const { return enabled_; }
  bool running() const { return running_; }
  chess::Color runningSide() const { return side_; }

  void start(chess::Color side, uint32_t now);   // `side` starts spending time
  void stop(uint32_t now);                        // charge what ran, then nobody runs
  // A move was played by the running side: charge it, add its increment, start the other side.
  void moveMade(uint32_t now);
  // After a take-back: charge the running side, then `side` runs (no time is given back).
  void switchTo(chess::Color side, uint32_t now);

  uint32_t remainingMs(chess::Color side, uint32_t now) const;
  bool timedOut(chess::Color side, uint32_t now) const { return enabled_ && remainingMs(side, now) == 0; }

  // The whole seconds the panel should show for `remainingMs`, rounded UP to the
  // cadence step: 05:00 stays on the glass until 04:50 is really due.
  static uint32_t shownSeconds(uint32_t remainingMs);
  // "mm:ss" into `out` (at least 6 chars).
  static void format(uint32_t seconds, char* out, int outSize);

 private:
  void charge(uint32_t now);
  uint32_t remaining_[2] = {0, 0};
  uint32_t incrementMs_ = 0;
  uint32_t startedMs_ = 0;
  chess::Color side_ = chess::Color::White;
  bool running_ = false;
  bool enabled_ = false;
};

}  // namespace arrocco::ui
