// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - when a tap made while the panel was refreshing may still count.
//
// The touch controller is polled during every refresh (gt911::refreshPoll), so the
// firmware knows what the glass saw while the image was changing. Such a tap used to be
// thrown away, always: the rule is never to act on a screen the user did not see. But
// two refreshes leave the board exactly as the user saw it:
//   - the game's own repaints (the clock, every second below 20 s): screen, position and
//     popups unchanged, only digits moved;
//   - the selection marks after a tap on the board ("piece, then target"): the squares
//     are the same squares, only the frame and the dots appeared.
// A tap on the board during one of those means what it meant when the finger went down,
// and is delivered once the refresh is over. Anything else is dropped as before.
//
// E-paper refreshes are electrically noisy right under the touch layer, so a touch must
// also look like a finger: one finger, seen at least kMinFrames times, not wandering.
//
// Portable on purpose: test/touch runs these rules on the Mac.
#pragma once
#include <cstdint>

namespace touch_policy {

// What started the refresh, as main.cpp saw it.
enum class Origin : uint8_t {
  Boot,  // the first screen
  Tap,   // a tap the app acted on
  Tick,  // the app's own work: clock, engine move, battery footer
};

// The part of the app's state a refresh can change under a finger.
struct Snapshot {
  bool gameScreen;  // the game screen is the one shown
  int plies;        // game().plyCount()
  int cursor;       // game().currentPly(): the position on the board
  bool over;
  bool popup;       // promotion or resign/draw popup over the board
};

// What the glass reported during the refresh (gt911::RefreshTouch, mapped to the screen).
struct DuringRefresh {
  bool seen;        // a report with a finger
  bool single;      // one finger, never a large-area report
  uint8_t frames;   // reports with a finger
  uint16_t maxDev;  // how far the later reports strayed from the first one, in pixels
  bool onBoard;     // the first report lands on the 8x8 squares
  bool held;        // still down when the refresh ended
};

enum class Verdict : uint8_t {
  Nothing,  // nobody touched the glass during the refresh
  Replay,   // deliver it: a tap, or (held) a touch that goes on
  Discard,  // drop it: the screen under it changed, or it may be noise
};

enum class Reason : uint8_t {
  None,
  NotIdle,          // the finger was already ours, or already being ignored
  Boot,             // the first screen after power-on or a wake
  Noise,            // too short, wandering, or more than one finger
  NotBoard,         // not on the squares (buttons can move, the squares cannot)
  ScreenChanged,
  PositionChanged,  // a move was played or taken back: the squares mean something else
  Popup,            // a popup was open, or opened
  TriggerOffBoard,  // the refresh came from a button, not from a square
};

struct Judgement {
  Verdict verdict;
  Reason reason;
};

constexpr uint8_t kMinFrames = 2;

// triggerOnBoard: for Origin::Tap, the tap that caused the refresh went down on a square.
// idleBefore: no touch was in progress, or being ignored, when the refresh started.
inline Judgement judge(Origin origin, bool triggerOnBoard, bool idleBefore, const Snapshot& before,
                       const Snapshot& after, const DuringRefresh& t, uint16_t slop) {
  if (!t.seen) return {Verdict::Nothing, Reason::None};
  if (!idleBefore) return {Verdict::Discard, Reason::NotIdle};
  if (origin == Origin::Boot) return {Verdict::Discard, Reason::Boot};
  if (!t.single || t.frames < kMinFrames || t.maxDev > slop) return {Verdict::Discard, Reason::Noise};
  if (!t.onBoard) return {Verdict::Discard, Reason::NotBoard};
  if (!before.gameScreen || !after.gameScreen) return {Verdict::Discard, Reason::ScreenChanged};
  if (before.plies != after.plies || before.cursor != after.cursor || before.over || after.over)
    return {Verdict::Discard, Reason::PositionChanged};
  if (before.popup || after.popup) return {Verdict::Discard, Reason::Popup};
  if (origin == Origin::Tap && !triggerOnBoard) return {Verdict::Discard, Reason::TriggerOffBoard};
  return {Verdict::Replay, Reason::None};
}

inline const char* reasonText(Reason r) {
  switch (r) {
    case Reason::None: return "";
    case Reason::NotIdle: return "a touch was already in progress";
    case Reason::Boot: return "first screen";
    case Reason::Noise: return "too short, wandering or two fingers (could be panel noise)";
    case Reason::NotBoard: return "not on the squares";
    case Reason::ScreenChanged: return "the screen changed under it";
    case Reason::PositionChanged: return "the position changed under it";
    case Reason::Popup: return "a popup is involved";
    case Reason::TriggerOffBoard: return "the refresh came from a button";
  }
  return "";
}

}  // namespace touch_policy
