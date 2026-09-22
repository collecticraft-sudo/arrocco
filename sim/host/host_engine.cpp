// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — HostEngine: arrocco::Engine on one std::thread. See host_engine.h.
//
// One worker thread, created once and parked on a condition variable. start() hands it
// a job and returns at once; the UI thread keeps drawing and answering taps.
//
// Aborting a search cannot be "cancel the thread": the search is inside C code that
// only checks a flag. So abort() raises that flag AND bumps a generation counter. The
// worker finishes whenever the engine lets it, then throws its answer away because the
// generation moved on. Nothing the UI can do leaves a stale move behind.
#include "host_engine.h"

#include <string.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

#include <ct800_searcher.h>

namespace arrocco_sim {
namespace {

// The engine's clock. Real milliseconds, monotonic, zero at the first call so that the
// value stays small whatever the machine's uptime.
int64_t engineNowMs() {
  using Clock = std::chrono::steady_clock;
  static const Clock::time_point start = Clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void engineSleepMs(int32_t ms) {
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class HostEngine final : public arrocco::Engine {
 public:
  bool begin() {
    if (!searcher_.begin(hash_, sizeof hash_, engineNowMs, engineSleepMs)) return false;
    // begin() seeds the engine's noise/book RNG from engineNowMs(), which returns 0 on
    // its very first call: without this the simulator would replay the same "random"
    // game at levels 1-4 every launch. Same reason as the device's esp_random().
    searcher_.seed(static_cast<uint32_t>(std::random_device{}()));
    worker_ = std::thread(&HostEngine::run, this);
    return true;
  }

  ~HostEngine() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      quit_ = true;
    }
    searcher_.abortSearch();
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void start(const char* fen, const char* uciMoves, int level, uint32_t timeMs) override {
    abort();  // whatever was running is no longer wanted
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    copyInto(fen_, sizeof fen_, fen);
    copyInto(moves_, sizeof moves_, uciMoves);
    level_ = arrocco::clampEngineLevel(level);
    timeMs_ = timeMs;
    jobPending_ = true;
    busy_ = true;
    haveResult_ = false;
    wake_.notify_one();
  }

  bool thinking() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_;
  }

  bool take(char bestUci[6]) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!haveResult_) return false;
    haveResult_ = false;
    memcpy(bestUci, result_, sizeof result_);
    return true;
  }

  void abort() override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!busy_ && !haveResult_ && !jobPending_) return;
      ++generation_;      // the answer in flight belongs to a search nobody wants now
      jobPending_ = false;
      busy_ = false;
      haveResult_ = false;
    }
    searcher_.abortSearch();
  }

 private:
  static void copyInto(char* dst, size_t size, const char* src) {
    if (src == nullptr) {
      dst[0] = '\0';
      return;
    }
    size_t n = strlen(src);
    if (n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }

  void run() {
    for (;;) {
      char fen[arrocco::kEngineFenSize];
      char moves[arrocco::kEngineMovesSize];
      arrocco::EngineLevel level{};
      uint32_t timeMs = 0;
      uint32_t generation = 0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return quit_ || jobPending_; });
        if (quit_) return;
        jobPending_ = false;
        generation = generation_;
        memcpy(fen, fen_, sizeof fen);
        memcpy(moves, moves_, sizeof moves);
        level = arrocco::kEngineLevels[level_];
        timeMs = timeMs_ != 0 ? timeMs_ : level.timeMs;
      }

      char best[6] = "0000";
      ct800::Limits limits;
      limits.timeMs = timeMs;
      limits.maxDepth = level.maxDepth;
      limits.cpuSpeed = 100;
      limits.maxNps = level.maxNps;
      limits.noisePercent = level.noisePercent;
      limits.useBook = level.useBook;
      if (searcher_.setPosition(fen[0] != '\0' ? fen : nullptr, moves)) searcher_.think(limits, best);

      std::lock_guard<std::mutex> lock(mutex_);
      if (generation != generation_) continue;   // aborted or superseded while searching
      memcpy(result_, best, sizeof result_);
      haveResult_ = true;
      busy_ = false;
    }
  }

  // The engine's transposition tables. On the host this may simply be .bss.
  alignas(8) unsigned char hash_[ct800::kHashMinBytes];
  ct800::Searcher searcher_;

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  bool quit_ = false;
  bool jobPending_ = false;
  bool busy_ = false;
  bool haveResult_ = false;
  uint32_t generation_ = 0;
  char fen_[arrocco::kEngineFenSize] = {};
  char moves_[arrocco::kEngineMovesSize] = {};
  int level_ = 0;
  uint32_t timeMs_ = 0;
  char result_[6] = "0000";
};

}  // namespace

arrocco::Engine* hostEngine() {
  static HostEngine engine;
  static const bool ok = engine.begin();
  return ok ? &engine : nullptr;
}

}  // namespace arrocco_sim
