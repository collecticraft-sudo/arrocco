// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco - CT800 port: ct800::Searcher, a blocking, single-threaded C++ face
// for the CT800 engine.
//
// think() runs the search on the calling thread and returns when it has a move:
// it never starts a thread of its own. Whoever wants a search that does not
// block the UI gives it a thread (the FreeRTOS task in src/app/esp32_engine.cpp,
// the std::thread in sim/host/host_engine.cpp) and drives it through the
// arrocco::Engine seam.
//
// ONE Searcher may exist. The CT800 board, piece lists, move stack and hash
// tables are file scope globals of the upstream C sources, so a second instance
// would silently share the first one's position. The class is not a singleton
// on purpose (a test wants to construct it where it likes), but constructing
// two and using both is a bug; constructing a second one AFTER the first is
// gone is fine.
//
// No heap: begin() takes the hash memory from the caller and nothing in the
// engine allocates. No exceptions, no RTTI, no iostream.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ct800_glue.h"

namespace ct800 {

// Smallest block begin() accepts. Below it the engine's "hashfull" report would
// read past the table, so it is a hard floor, not a preference.
constexpr size_t kHashMinBytes = CT800_HASH_MIN_BYTES;

// What one strength step asks of the search. arrocco/engine.h owns the table of
// these; the Searcher only applies them.
struct Limits {
  uint32_t timeMs = 1000;   // thinking time budget
  int maxDepth = 42;        // hard ply cap, 1..42
  int cpuSpeed = 100;       // duty cycle in percent, 10..100
  uint64_t maxNps = 0;      // node rate ceiling, 0 = none
  int noisePercent = 0;     // static evaluation blurring, 0..100
  bool useBook = true;      // the built-in opening book
};

class Searcher {
 public:
  // The clock the engine measures and sleeps with. Both are required: `nowMs`
  // must be monotonic in milliseconds, `sleepMs` may return early but must not
  // busy-wait (it is what keeps a throttled search off the CPU).
  using NowFn = int64_t (*)();
  using SleepFn = void (*)(int32_t);

  Searcher() = default;
  Searcher(const Searcher&) = delete;
  Searcher& operator=(const Searcher&) = delete;

  // Carves the two transposition tables out of `hashMemory`, which the caller
  // owns and must keep alive for as long as the Searcher is used. `bytes` must
  // be at least kHashMinBytes. Returns false if it is smaller, if either clock
  // function is null, or if the memory is null.
  bool begin(void* hashMemory, size_t bytes, NowFn nowMs, SleepFn sleepMs) {
    if (nowMs == nullptr || sleepMs == nullptr) return false;
    ct800_glue_set_clock(nowMs, sleepMs);
    ready_ = ct800_glue_init(hashMemory, bytes) == 0;
    return ready_;
  }

  bool ready() const { return ready_; }

  // Entries per transposition table (there are two), after begin().
  size_t hashEntries() const { return ct800_glue_hash_entries(); }

  // Seeds the opening-book choice and the evaluation noise of the weak levels. begin()
  // seeds from the clock, which reads about zero on a device that has just booted, so a
  // front end that wants a different game every power-on calls this with real entropy.
  // Tests that want the engine reproducible simply never call it.
  void seed(uint32_t value) { ct800_glue_seed(value); }

  // The position to search. `fen` may be null or "" for the standard starting
  // position; `uciMoves` may be null or "" for none, else "e2e4 e7e5 g1f3".
  // Returns false when the FEN or a move is rejected, and think() must then not
  // be called until a later setPosition() succeeds.
  bool setPosition(const char* fen, const char* uciMoves) {
    if (!ready_) return false;
    positioned_ = ct800_glue_set_position(fen, uciMoves) == 0;
    return positioned_;
  }

  // 0 = White to move, 1 = Black, in the position set up last.
  int sideToMove() const { return ct800_glue_side_to_move(); }

  // Searches, blocking, and writes the move as UCI text into bestUci (6 bytes).
  // Returns false when there is no move (mate, stalemate, or an abort that came
  // before the first iteration finished); bestUci then holds "0000".
  bool think(const Limits& limits, char bestUci[6]) {
    return thinkTimed(limits, bestUci, nullptr, nullptr);
  }

  // The same, reporting the nodes searched and the milliseconds spent. Either
  // pointer may be null.
  bool thinkTimed(const Limits& limits, char bestUci[6], uint64_t* nodes, int64_t* spentMs) {
    if (bestUci != nullptr) {
      bestUci[0] = '0'; bestUci[1] = '0'; bestUci[2] = '0'; bestUci[3] = '0'; bestUci[4] = '\0';
    }
    if (!ready_ || !positioned_) return false;
    return ct800_glue_think(static_cast<int32_t>(limits.timeMs), limits.maxDepth, limits.cpuSpeed,
                            limits.maxNps, limits.noisePercent, limits.useBook ? 1 : 0, bestUci,
                            nodes, spentMs) != 0;
  }

  // Asks a running think() to stop. Safe from another task or thread: it stores
  // one flag, which the search polls and which cuts short its throttle sleeps.
  // think() then returns with whatever it had (or false, very early on). The
  // flag is cleared by the next think().
  void abortSearch() { ct800_glue_abort(); }

  // True between the moment think() enters the engine and the moment it leaves.
  bool thinking() const { return ct800_glue_busy() != 0; }

  // Drains the engine's UCI "info" text (a 512 byte ring, oldest dropped) into
  // `out`; returns the number of characters written. Only for tests and the
  // simulator: nothing prints on the device.
  int drainLog(char* out, int outSize) { return ct800_glue_log_drain(out, outSize); }

 private:
  bool ready_ = false;
  bool positioned_ = false;
};

}  // namespace ct800
