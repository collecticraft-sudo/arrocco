// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — arrocco-sim: runs one arrocco::App on SimPlatform and speaks
// the line protocol of protocol.h on stdin/stdout.
//
// Two threads. The stdin thread only timestamps lines and queues them. The main
// thread is the firmware's loop(): deliver input, call tick(), repeat — and it is
// the only one that touches the App, the platform and stdout. The arrival timestamps
// are what makes "the panel ignores touches while it refreshes" possible: present()
// blocks the main thread, and anything stamped before it returned is discarded.
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "app_factory.h"
#include "protocol.h"
#include "sim_platform.h"

namespace {

using arrocco_sim::SimPlatform;
using Clock = SimPlatform::Clock;

constexpr size_t kLineMax = 96;
constexpr size_t kQueueSize = 512;
constexpr auto kTickPeriod = std::chrono::milliseconds(10);   // like a 100 Hz loop()

struct InputLine {
  char text[kLineMax];
  Clock::time_point arrival;
};

// Fixed-size ring between the stdin thread and the main thread.
class InputQueue {
public:
  void push(const char* text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == kQueueSize) return;   // a stuck main thread must not grow memory
    InputLine& slot = lines_[(head_ + count_) % kQueueSize];
    snprintf(slot.text, sizeof slot.text, "%s", text);
    slot.arrival = Clock::now();
    ++count_;
    ready_.notify_one();
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    ready_.notify_one();
  }

  // False when nothing arrived before the deadline (no deadline: wait for ever).
  // 'closed' reports end of input once the queue has drained.
  bool pop(InputLine& out, const Clock::time_point* deadline, bool& closed) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto somethingToDo = [this] { return count_ > 0 || closed_; };
    if (deadline) {
      ready_.wait_until(lock, *deadline, somethingToDo);
    } else {
      ready_.wait(lock, somethingToDo);
    }
    if (count_ == 0) {
      closed = closed_;
      return false;
    }
    out = lines_[head_];
    head_ = (head_ + 1) % kQueueSize;
    --count_;
    closed = false;
    return true;
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  InputLine lines_[kQueueSize];
  size_t head_ = 0;
  size_t count_ = 0;
  bool closed_ = false;
};

// Decides which touch samples the App gets to see. The GT911 is not polled while the
// panel refreshes, so a finger that lands during a refresh never existed for the
// firmware: the whole gesture (down, moves, up) is dropped. A finger that was already
// down keeps its Up, delivered once the refresh is over, otherwise the App would be
// left believing the finger is still there.
class TouchGate {
public:
  enum class Verdict { Deliver, Drop, DropAndCount };

  Verdict judge(arrocco::TouchEvent::Type type, bool duringRefresh) {
    switch (type) {
      case arrocco::TouchEvent::Down:
        if (duringRefresh) {
          gestureLive_ = false;
          return Verdict::DropAndCount;
        }
        gestureLive_ = true;
        return Verdict::Deliver;
      case arrocco::TouchEvent::Move:
        return (gestureLive_ && !duringRefresh) ? Verdict::Deliver : Verdict::Drop;
      case arrocco::TouchEvent::Up:
        if (!gestureLive_) return Verdict::Drop;
        gestureLive_ = false;
        return Verdict::Deliver;
    }
    return Verdict::Drop;
  }

private:
  bool gestureLive_ = false;
};

InputQueue g_queue;

void readStdin(SimPlatform* platform) {
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    size_t length = strcspn(line, "\r\n");
    line[length] = '\0';
    if (length == 0) continue;
    if (strcmp(line, "quit") == 0) break;
    g_queue.push(line);
  }
  // End of input means whoever started us is gone (or said quit): never outlive it.
  // Order matters: close() is what lets main() return and destroy the platform.
  platform->requestStop();
  g_queue.close();
}

int16_t clampToScreen(long value, int16_t limit) {
  if (value < 0) return 0;
  if (value >= limit) return static_cast<int16_t>(limit - 1);
  return static_cast<int16_t>(value);
}

struct Session {
  SimPlatform& platform;
  arrocco::App& app;
  arrocco_sim::Emitter& out;
  TouchGate gate;
  uint32_t ignoredTouches = 0;
};

void handleLine(Session& s, const InputLine& input) {
  const char* text = input.text;
  char word[8];
  long a = 0;
  long b = 0;
  double f = 0.0;
  char extra = 0;

  if (sscanf(text, "touch %7s %ld %ld %c", word, &a, &b, &extra) == 3) {
    arrocco::TouchEvent e{};
    if (strcmp(word, "down") == 0) {
      e.type = arrocco::TouchEvent::Down;
    } else if (strcmp(word, "move") == 0) {
      e.type = arrocco::TouchEvent::Move;
    } else if (strcmp(word, "up") == 0) {
      e.type = arrocco::TouchEvent::Up;
    } else {
      s.out.error("unknown touch phase", text);
      return;
    }
    // In virtual time nothing blocks, so nothing can arrive "during" a refresh.
    const bool duringRefresh = !s.platform.virtualTime() && input.arrival < s.platform.busyUntil();
    switch (s.gate.judge(e.type, duringRefresh)) {
      case TouchGate::Verdict::Deliver:
        e.x = clampToScreen(a, arrocco::kScreenW);
        e.y = clampToScreen(b, arrocco::kScreenH);
        // A deferred Up is stamped "now": the firmware would notice the finger gone
        // on its first poll after the refresh, not before.
        e.ms = duringRefresh ? s.platform.millis() : s.platform.millisAt(input.arrival);
        s.app.onTouch(e);
        break;
      case TouchGate::Verdict::DropAndCount:
        s.out.touchIgnored(++s.ignoredTouches);
        break;
      case TouchGate::Verdict::Drop:
        break;
    }
    return;
  }

  if (strcmp(text, "tick") == 0 || sscanf(text, "tick %ld %c", &a, &extra) == 1) {
    if (a < 0) a = 0;
    if (a > 86400000L) a = 86400000L;
    s.platform.advanceVirtualTime(static_cast<uint32_t>(a));
    s.app.tick();
    return;
  }
  if (sscanf(text, "set battery %ld %c", &a, &extra) == 1) {
    s.platform.setBatteryPercent(static_cast<int>(a < -1 ? -1 : (a > 100 ? 100 : a)));
    s.platform.reportState();
    return;
  }
  if (sscanf(text, "set usb %ld %c", &a, &extra) == 1) {
    s.platform.setUsbPowered(a != 0);
    s.platform.reportState();
    return;
  }
  if (sscanf(text, "set scale %lf %c", &f, &extra) == 1) {
    s.platform.setLatencyScale(f);
    s.platform.reportState();
    return;
  }
  if (strcmp(text, "frame") == 0) {
    s.platform.resendLastFrame();
    return;
  }
  s.out.error("unknown command", text);
}

}  // namespace

int main(int argc, char** argv) {
  bool virtualTime = false;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--virtual-time") == 0) {
      virtualTime = true;
    } else {
      fprintf(stderr,
              "usage: arrocco-sim [--virtual-time]\n"
              "Runs the Arrocco app on a simulated 800x480 e-ink panel and speaks a line\n"
              "protocol on stdin/stdout (see sim/host/protocol.h). Normally started by\n"
              "sim/server.py; sim/run.sh does everything.\n");
      return strcmp(argv[i], "--help") == 0 ? 0 : 2;
    }
  }

  // Frames are ~64 KB lines; give stdio a buffer that holds one.
  static char stdoutBuffer[1 << 17];
  setvbuf(stdout, stdoutBuffer, _IOFBF, sizeof stdoutBuffer);

  static arrocco_sim::Emitter out;
  static SimPlatform platform(out, virtualTime);
  arrocco::App* app = arrocco_sim::createApp(platform);
  if (!app) {
    fprintf(stderr, "arrocco-sim: createApp() returned no app\n");
    return 1;
  }

  out.hello(arrocco_sim::appName(), virtualTime);
  platform.reportState();

  std::thread reader(readStdin, &platform);

  Session session{platform, *app, out, TouchGate{}, 0};
  app->begin();

  auto nextTick = Clock::now();
  for (;;) {
    InputLine input;
    bool closed = false;
    // Virtual time: the script owns the clock, so only explicit "tick" lines tick.
    if (g_queue.pop(input, virtualTime ? nullptr : &nextTick, closed)) {
      handleLine(session, input);
    } else if (closed) {
      break;
    }
    if (!virtualTime && Clock::now() >= nextTick) {
      app->tick();
      nextTick = Clock::now() + kTickPeriod;
    }
  }

  // The loop only ends on end of input, which is the reader's last act.
  reader.join();
  out.bye();
  return 0;
}
