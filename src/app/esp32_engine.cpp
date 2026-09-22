// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware — Esp32Engine: arrocco::Engine on a FreeRTOS task. See esp32_engine.h.
//
// The search is C code that stops only when it sees a flag, so abort() cannot kill it:
// it raises the flag AND bumps a generation counter. The task finishes whenever the
// engine lets it and then drops its answer, because the generation has moved on. That
// is what makes Undo, New game and Menu safe to press while it is thinking.
//
// Everything the task reads is copied under the mutex before the search starts, so the
// UI task may go on touching its own copy of the position while the search runs.
#include "esp32_engine.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include <ct800_searcher.h>

namespace arrocco_app {
namespace {

constexpr uint32_t kTaskStack = 48u * 1024u;   // measured need is far below; this is the margin
constexpr BaseType_t kTaskCore = 0;            // core 1 runs Arduino's loop task, the panel and touch
// Idle priority on purpose: at any higher priority a search with no node-rate ceiling
// would starve IDLE0 and the task watchdog would fire after five seconds. At the idle
// priority the two round-robin, IDLE0 yields immediately, and the watchdog is fed.
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY;

int64_t engineNowMs() { return esp_timer_get_time() / 1000; }

void engineSleepMs(int32_t ms) {
  if (ms <= 0) return;
  TickType_t ticks = pdMS_TO_TICKS(ms);
  vTaskDelay(ticks > 0 ? ticks : 1);
}

class Esp32Engine final : public arrocco::Engine {
 public:
  bool begin() {
    if (task_ != nullptr) return true;
    mutex_ = xSemaphoreCreateMutex();
    job_ = xSemaphoreCreateBinary();
    if (mutex_ == nullptr || job_ == nullptr) return false;

    // Internal RAM first: it is about twice as fast as PSRAM for the random access a
    // transposition table does. 640 KB of it does not exist on this board with the
    // frame buffer and the Game already in .bss, so in practice PSRAM wins — the try
    // costs nothing and a future build with a smaller hash would take it.
    const size_t bytes = ct800::kHashMinBytes;
    hash_ = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    where_ = "internal";
    if (hash_ == nullptr) {
      hash_ = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      where_ = "PSRAM";
    }
    if (hash_ == nullptr) {
      where_ = "none";
      snprintf(info_, sizeof info_, "no hash memory (%u KB wanted)",
               static_cast<unsigned>(bytes / 1024u));
      return false;
    }
    if (!searcher_.begin(hash_, bytes, engineNowMs, engineSleepMs)) {
      heap_caps_free(hash_);
      hash_ = nullptr;
      snprintf(info_, sizeof info_, "hash refused by the engine");
      return false;
    }
    snprintf(info_, sizeof info_, "%s, %u KB, %u entries per table", where_,
             static_cast<unsigned>(bytes / 1024u),
             static_cast<unsigned>(searcher_.hashEntries()));

    // CT800 seeds its noise/book RNG inside init(), when this clock still reads about
    // zero — the same seed every power-on, so levels 1-4 would replay the same "random"
    // game for ever. esp_random() is the hardware entropy source and is free here.
    searcher_.seed(esp_random());

    if (xTaskCreatePinnedToCore(taskEntry, "ct800", kTaskStack, this, kTaskPriority, &task_,
                                kTaskCore) != pdPASS) {
      task_ = nullptr;
      heap_caps_free(hash_);        // begin() may be retried: do not leak the 760 KB
      hash_ = nullptr;
      snprintf(info_, sizeof info_, "search task could not be created");
      return false;
    }
    return true;
  }

  const char* info() const { return info_; }

  void start(const char* fen, const char* uciMoves, int level, uint32_t timeMs) override {
    if (task_ == nullptr) return;
    abort();
    lock();
    ++generation_;
    copyInto(fen_, sizeof fen_, fen);
    copyInto(moves_, sizeof moves_, uciMoves);
    level_ = arrocco::clampEngineLevel(level);
    timeMs_ = timeMs;
    jobPending_ = true;
    busy_ = true;
    haveResult_ = false;
    unlock();
    // A binary semaphore: when a token is already waiting the give simply fails, and
    // that is right. The token says "look at the job", jobPending_ says which one, and
    // the task always reads the newest. A failed give never loses a search.
    xSemaphoreGive(job_);
  }

  bool thinking() const override {
    if (task_ == nullptr) return false;
    lock();
    const bool busy = busy_;
    unlock();
    return busy;
  }

  bool take(char bestUci[6]) override {
    if (task_ == nullptr) return false;
    lock();
    const bool have = haveResult_;
    if (have) {
      memcpy(bestUci, result_, sizeof result_);
      haveResult_ = false;
    }
    unlock();
    return have;
  }

  void abort() override {
    if (task_ == nullptr) return;
    lock();
    const bool anything = busy_ || haveResult_ || jobPending_;
    if (anything) {
      ++generation_;
      // Clearing this is what makes an abort stick when the task has NOT yet picked the
      // job up. Bumping the generation alone does not: the task latches the generation
      // when it starts, so it would latch the NEW one, search a position nobody wants
      // any more for the whole move time, and publish the answer as if it were fresh.
      jobPending_ = false;
      busy_ = false;
      haveResult_ = false;
    }
    unlock();
    if (anything) searcher_.abortSearch();
  }

  // Smallest number of free bytes the search task's stack has ever had, or 0 before the
  // first search. Reported in the firmware's STAT line: 48 KB is a guess until a real
  // board says otherwise.
  uint32_t stackFreeBytes() const {
    if (task_ == nullptr) return 0;
    lock();
    const uint32_t free = stackFree_;
    unlock();
    return free;
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

  void lock() const { xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() const { xSemaphoreGive(mutex_); }

  static void taskEntry(void* self) { static_cast<Esp32Engine*>(self)->run(); }

  void run() {
    for (;;) {
      xSemaphoreTake(job_, portMAX_DELAY);

      arrocco::EngineLevel level{};
      uint32_t timeMs = 0;
      uint32_t generation = 0;
      lock();
      if (!jobPending_) {          // aborted between start() and here: do not search at all
        unlock();
        continue;
      }
      jobPending_ = false;
      generation = generation_;
      level = arrocco::kEngineLevels[level_];
      timeMs = timeMs_ != 0 ? timeMs_ : level.timeMs;
      // Copied, not aliased: start() may rewrite fen_/moves_ while the search runs, and
      // reading them from the search would be a plain data race on a torn string.
      // jobFen_/jobMoves_ are members so that 1.6 KB stays out of the task stack.
      memcpy(jobFen_, fen_, sizeof jobFen_);
      memcpy(jobMoves_, moves_, sizeof jobMoves_);
      unlock();
      const char* fen = (jobFen_[0] != '\0') ? jobFen_ : nullptr;

      ct800::Limits limits;
      limits.timeMs = timeMs;
      limits.maxDepth = level.maxDepth;
      limits.cpuSpeed = 100;
      limits.maxNps = level.maxNps;
      limits.noisePercent = level.noisePercent;
      limits.useBook = level.useBook;

      char best[6] = "0000";
      if (searcher_.setPosition(fen, jobMoves_)) searcher_.think(limits, best);

      const uint32_t freeWords = uxTaskGetStackHighWaterMark(nullptr);

      lock();
      const uint32_t freeBytes = freeWords * sizeof(StackType_t);
      if (stackFree_ == 0 || freeBytes < stackFree_) stackFree_ = freeBytes;
      if (generation == generation_) {
        memcpy(result_, best, sizeof result_);
        haveResult_ = true;
        busy_ = false;
      }
      unlock();
    }
  }

  ct800::Searcher searcher_;
  void* hash_ = nullptr;
  const char* where_ = "none";
  char info_[64] = "not started";

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  SemaphoreHandle_t job_ = nullptr;

  bool jobPending_ = false;
  bool busy_ = false;
  bool haveResult_ = false;
  uint32_t generation_ = 0;
  uint32_t stackFree_ = 0;
  char fen_[arrocco::kEngineFenSize] = {};
  char moves_[arrocco::kEngineMovesSize] = {};
  char jobFen_[arrocco::kEngineFenSize] = {};      // the task's own copy: see run()
  char jobMoves_[arrocco::kEngineMovesSize] = {};
  int level_ = 0;
  uint32_t timeMs_ = 0;
  char result_[6] = "0000";
};

Esp32Engine s_engine;   // .bss: the 1.6 KB of position buffers never touch a task stack

}  // namespace

bool engineBegin() { return s_engine.begin(); }

arrocco::Engine* engine() { return &s_engine; }

const char* engineHashInfo() { return s_engine.info(); }

uint32_t engineStackFreeBytes() { return s_engine.stackFreeBytes(); }

}  // namespace arrocco_app
