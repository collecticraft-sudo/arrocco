// SPDX-License-Identifier: GPL-3.0-or-later
// The touch replay rules (src/app/touch_policy.h), one situation at a time: a tap made
// while the panel refreshed is delivered only when the board under it is the one the
// user saw, and only when it looks like a finger.
#include <cstdio>
#include <cstring>

#include "touch_policy.h"

namespace {

using touch_policy::DuringRefresh;
using touch_policy::judge;
using touch_policy::Origin;
using touch_policy::Reason;
using touch_policy::Snapshot;
using touch_policy::Verdict;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL %s\n", what);
  }
}

constexpr uint16_t kSlop = 16;

// The game screen, move 12 played, nothing open.
constexpr Snapshot kGame{true, 12, 12, false, false};
// A real tap: one finger, seen four times, steady, on a square, lifted before the end.
constexpr DuringRefresh kTap{true, true, 4, 3, true, false};

DuringRefresh with(DuringRefresh t, void (*change)(DuringRefresh&)) {
  change(t);
  return t;
}

Snapshot after(Snapshot s, void (*change)(Snapshot&)) {
  change(s);
  return s;
}

void expect(const char* what, touch_policy::Judgement j, Verdict verdict, Reason reason) {
  char line[160];
  std::snprintf(line, sizeof line, "%s: verdict %d reason %d", what, static_cast<int>(j.verdict),
                static_cast<int>(j.reason));
  check(j.verdict == verdict && j.reason == reason, line);
}

}  // namespace

int main() {
  std::printf("== touch replay rules\n");

  // Nobody touched the glass: nothing to judge, whatever happened.
  expect("nothing seen", judge(Origin::Tick, false, true, kGame, kGame, DuringRefresh{}, kSlop), Verdict::Nothing,
         Reason::None);

  // The clock repainted (tick, nothing else changed): the tap counts.
  expect("clock repaint, tap", judge(Origin::Tick, false, true, kGame, kGame, kTap, kSlop), Verdict::Replay,
         Reason::None);
  expect("clock repaint, finger still down",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.held = true; }), kSlop),
         Verdict::Replay, Reason::None);

  // Piece, then target: the selection refresh after a tap on a square.
  expect("selection marks, second tap", judge(Origin::Tap, true, true, kGame, kGame, kTap, kSlop), Verdict::Replay,
         Reason::None);

  // The engine played (tick, one more ply): the squares now mean something else.
  expect("engine move",
         judge(Origin::Tick, false, true, kGame, after(kGame, [](Snapshot& s) { ++s.plies; ++s.cursor; }), kTap, kSlop),
         Verdict::Discard, Reason::PositionChanged);
  // The tap played a move: the next tap belongs to a position nobody has seen yet.
  expect("move played",
         judge(Origin::Tap, true, true, kGame, after(kGame, [](Snapshot& s) { ++s.plies; ++s.cursor; }), kTap, kSlop),
         Verdict::Discard, Reason::PositionChanged);
  // Undo / review moved the cursor only.
  expect("review step", judge(Origin::Tap, true, true, kGame, after(kGame, [](Snapshot& s) { --s.cursor; }), kTap, kSlop),
         Verdict::Discard, Reason::PositionChanged);
  // The flag fell: game over.
  expect("game over", judge(Origin::Tick, false, true, kGame, after(kGame, [](Snapshot& s) { s.over = true; }), kTap, kSlop),
         Verdict::Discard, Reason::PositionChanged);
  expect("over already",
         judge(Origin::Tick, false, true, after(kGame, [](Snapshot& s) { s.over = true; }),
               after(kGame, [](Snapshot& s) { s.over = true; }), kTap, kSlop),
         Verdict::Discard, Reason::PositionChanged);

  // Another screen, before or after.
  expect("screen change",
         judge(Origin::Tap, true, true, kGame, after(kGame, [](Snapshot& s) { s.gameScreen = false; }), kTap, kSlop),
         Verdict::Discard, Reason::ScreenChanged);
  expect("menu repaint",
         judge(Origin::Tick, false, true, after(kGame, [](Snapshot& s) { s.gameScreen = false; }),
               after(kGame, [](Snapshot& s) { s.gameScreen = false; }), kTap, kSlop),
         Verdict::Discard, Reason::ScreenChanged);

  // Popups: opened by the tap, closed by it, or open all along.
  expect("promotion popup opened",
         judge(Origin::Tap, true, true, kGame, after(kGame, [](Snapshot& s) { s.popup = true; }), kTap, kSlop),
         Verdict::Discard, Reason::Popup);
  expect("popup closed",
         judge(Origin::Tap, true, true, after(kGame, [](Snapshot& s) { s.popup = true; }), kGame, kTap, kSlop),
         Verdict::Discard, Reason::Popup);
  expect("popup open during a clock repaint",
         judge(Origin::Tick, false, true, after(kGame, [](Snapshot& s) { s.popup = true; }),
               after(kGame, [](Snapshot& s) { s.popup = true; }), kTap, kSlop),
         Verdict::Discard, Reason::Popup);

  // The refresh came from a button (Flip, Resign/Draw...).
  expect("button refresh", judge(Origin::Tap, false, true, kGame, kGame, kTap, kSlop), Verdict::Discard,
         Reason::TriggerOffBoard);

  // Not on the squares: buttons can move, the squares cannot.
  expect("tap on a button",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.onBoard = false; }), kSlop),
         Verdict::Discard, Reason::NotBoard);

  // What may be panel noise rather than a finger.
  expect("one report only",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.frames = 1; }), kSlop),
         Verdict::Discard, Reason::Noise);
  expect("wandering",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.maxDev = 40; }), kSlop),
         Verdict::Discard, Reason::Noise);
  expect("just within the slop",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.maxDev = 16; }), kSlop),
         Verdict::Replay, Reason::None);
  expect("two fingers or a palm",
         judge(Origin::Tick, false, true, kGame, kGame, with(kTap, [](DuringRefresh& t) { t.single = false; }), kSlop),
         Verdict::Discard, Reason::Noise);

  // A finger that was already down (or being ignored) when the refresh began.
  expect("not idle", judge(Origin::Tick, false, false, kGame, kGame, kTap, kSlop), Verdict::Discard, Reason::NotIdle);
  // The first screen: whatever touches it is the touch that woke the board.
  expect("boot", judge(Origin::Boot, false, true, kGame, kGame, kTap, kSlop), Verdict::Discard, Reason::Boot);

  // Every reason has words for the log.
  const Reason reasons[] = {Reason::NotIdle, Reason::Boot, Reason::Noise, Reason::NotBoard, Reason::ScreenChanged,
                            Reason::PositionChanged, Reason::Popup, Reason::TriggerOffBoard};
  for (const Reason r : reasons) check(std::strlen(touch_policy::reasonText(r)) > 0, "reason text");
  check(std::strlen(touch_policy::reasonText(Reason::None)) == 0, "no reason, no text");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
