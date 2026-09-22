// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco — the engine seam: what the UI knows about a chess engine.
//
// The UI never blocks. It asks start() for a move, then keeps drawing and
// answering taps; ChessApp::tick() polls thinking()/take() and plays the move
// when it arrives. Everything about HOW the search runs — a FreeRTOS task on
// the device, a std::thread in the simulator — lives behind this interface:
//   src/app/esp32_engine.cpp      the firmware
//   sim/host/host_engine.cpp      the Mac simulator
// Both drive the same ct800::Searcher (lib/ct800/port).
//
// The strength levels are here, not in the implementations, so that the device
// and the simulator play exactly the same game. They are the one place where a
// UI string and an engine parameter meet, which is why this header includes
// arrocco/ui/strings.h: the names belong to the UI, the numbers do not.
#pragma once
#include <cstdint>

#include "arrocco/ui/strings.h"

namespace arrocco {

// What one level asks of the search.
//
// CT800 has no Elo setting: strength comes out of three knobs.
//   maxNps        node rate ceiling. The engine sleeps to keep under it, so it
//                 is the honest way to make it think less. CT800's own Elo
//                 table bottoms out at 300 nodes/s (MIN_NODES) around 1850 Elo:
//                 below that, the rate alone cannot weaken it any further.
//   noisePercent  how much of the static evaluation is replaced by random
//                 noise. At 100 the evaluation IS the noise: the engine still
//                 sees forced mates and whatever tactics fit inside its search,
//                 but it has no idea which quiet position is better. This is
//                 what makes the bottom levels lose to a beginner, and CT800's
//                 own Elo mapping uses it the same way (100 % at 1000 Elo,
//                 30 % at 1500, 0 % from 1900 up).
//   maxDepth      hard ply cap. Below START_DEPTH (2) the engine would return
//                 nothing, so 3 is the lowest level used here.
// timeMs is the move time; the search uses all of it (fixed time per move), so
// it is also how long the e-ink panel shows "Thinking..." before the move.
struct EngineLevel {
  const char* name;
  uint32_t timeMs;
  uint32_t maxNps;       // 0 = no ceiling
  uint8_t maxDepth;      // plies, 3..42
  uint8_t noisePercent;  // 0..100
  bool useBook;          // the built-in opening book
};

constexpr int kEngineLevelCount = 8;

// Eight steps. The first four are meant to be losable by a beginner, and get
// there by blurring the evaluation rather than by pretending to be slow; the
// last two are the engine roughly as its author shipped it, within the time an
// e-ink board can spend without feeling dead.
constexpr EngineLevel kEngineLevels[kEngineLevelCount] = {
    {ui::str::kLevel1,  400u,   300u,  3, 100, false},
    {ui::str::kLevel2,  500u,   300u,  4,  85, false},
    {ui::str::kLevel3,  700u,   300u,  5,  65, false},
    {ui::str::kLevel4,  900u,   400u,  6,  45, true},
    {ui::str::kLevel5, 1200u,   800u,  8,  25, true},
    {ui::str::kLevel6, 2000u,  3000u, 12,  10, true},
    {ui::str::kLevel7, 3000u, 12000u, 20,   0, true},
    {ui::str::kLevel8, 5000u,      0u, 42,   0, true},
};

// The buffers start() is given, and that an implementation has to copy before its
// worker can use them. kEngineMovesSize holds 255 plies of "e7e8q " plus the NUL:
// past that the UI hands over the current position as a FEN instead, and the engine
// simply loses the repetition history of a game that long.
constexpr int kEngineFenSize = 96;
constexpr int kEngineMovesSize = 1536;

constexpr int clampEngineLevel(int level) {
  return level < 0 ? 0 : (level >= kEngineLevelCount ? kEngineLevelCount - 1 : level);
}

// The asynchronous engine the UI talks to. Every method is called from the UI
// thread only; the implementation is what crosses over to its worker.
class Engine {
 public:
  virtual ~Engine() = default;

  // Starts a search from `fen` (null or "" = the standard starting position)
  // after `uciMoves` ("e2e4 e7e5", null or "" for none), at `level`
  // (0..levelCount()-1). `timeMs` of 0 means the level's own move time.
  // A search already running is aborted first. After this, thinking() is true
  // until the move is ready or abort() is called.
  virtual void start(const char* fen, const char* uciMoves, int level, uint32_t timeMs) = 0;

  // True from start() until the worker has finished (whether it found a move or
  // was aborted). False before the first start().
  virtual bool thinking() const = 0;

  // Takes the finished move, once. Returns false when nothing is ready — while
  // thinking, after an abort, or when it has already been taken. On true,
  // bestUci holds a UCI move ("e2e4", "e7e8q") or "0000" when the search found
  // none (mate, stalemate, or aborted before its first iteration).
  virtual bool take(char bestUci[6]) = 0;

  // Asks the search to stop and throws away whatever it produces. Returns
  // immediately; thinking() may stay true for a few milliseconds more, and the
  // discarded move is never handed out by take().
  virtual void abort() = 0;

  virtual int levelCount() const { return kEngineLevelCount; }
  virtual const char* levelName(int level) const {
    return kEngineLevels[clampEngineLevel(level)].name;
  }
};

}  // namespace arrocco
