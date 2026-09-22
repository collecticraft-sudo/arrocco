// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco — the arrocco::Engine seam under attack, on the one implementation a Mac can
// run: sim/host/host_engine.cpp. The firmware's Esp32Engine answers the same contract
// with FreeRTOS primitives instead of std::thread, so what fails here would fail there.
//
// What it insists on, in the words of arrocco/engine.h:
//   - take() hands the move over ONCE;
//   - a search that was aborted never hands anything over, however late it finishes;
//   - an aborted search does not make the next one wait for it;
//   - start() while thinking answers the position of the LAST start(), not the first.
// The third and fourth are the ones that bite on the device: the search is C code that
// stops only when it notices a flag, so "abort" cannot mean "cancel".
//
//   make -C test/engine test
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <thread>

#include "arrocco/chess/game.h"
#include "arrocco/chess/position.h"
#include "host_engine.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

void check(bool condition, const char* fmt, ...) {
  char text[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof text, fmt, args);
  va_end(args);
  ++g_checks;
  printf(condition ? "  ok  [%d] %s\n" : "FAIL  [%d] %s\n", g_checks, text);
  if (!condition) ++g_failures;
}

void waitMs(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

int64_t nowMs() {
  using Clock = std::chrono::steady_clock;
  static const Clock::time_point start = Clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// A middlegame, so that the opening book cannot answer instead of the search.
const char* kOutOfBook = "r1bq1rk1/pp2ppbp/2np1np1/8/2BNP3/2N1B3/PPP2PPP/R2Q1RK1 w - - 4 9";

}  // namespace

int main() {
  printf("== the engine seam\n");
  arrocco::Engine* e = arrocco_sim::hostEngine();
  if (e == nullptr) {
    printf("FAIL: the host engine did not start\n");
    return 1;
  }

  char uci[6];
  check(!e->thinking(), "seam: thinking() is false before the first start()");
  check(!e->take(uci), "seam: take() before any search gives nothing");

  // ---- take() hands the move over exactly once
  e->start(nullptr, "e2e4 e7e5", 5, 300);
  while (e->thinking()) waitMs(5);
  check(e->take(uci), "seam: take() gives the move after the search finished (%s)", uci);
  check(!e->take(uci), "seam: take() a second time gives nothing");

  // ---- abort() racing a search that is about to return
  // Aborting immediately after start() is the nasty window: on the device the worker may
  // not have picked the job up yet, so bumping a generation it has not read is not enough
  // on its own. 400 rounds, and not one move may escape.
  bool escaped = false;
  int rounds = 0;
  for (; rounds < 400 && !escaped; ++rounds) {
    e->start(kOutOfBook, "", 7, 2000);
    e->abort();
    if (e->take(uci)) escaped = true;
  }
  check(!escaped, "seam: %d start()+abort() pairs, never a move handed out", rounds);
  waitMs(120);
  check(!e->take(uci), "seam: and none arrives in the 120 ms afterwards either");

  // ---- an aborted search must not delay the next one
  e->start(kOutOfBook, "", 7, 3000);
  waitMs(30);
  e->abort();
  const int64_t t0 = nowMs();
  e->start(kOutOfBook, "", 4, 250);
  while (e->thinking()) waitMs(2);
  const int64_t spent = nowMs() - t0;
  check(e->take(uci), "seam: the search after an abort produces a move (%s)", uci);
  printf("      a 250 ms search, asked right after aborting a 3000 ms one, took %lld ms\n",
         static_cast<long long>(spent));
  check(spent < 1500, "seam: it did not have to wait out the aborted search (%lld ms)",
        static_cast<long long>(spent));

  // ---- start() while thinking answers the LAST position asked for
  e->start(nullptr, "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6", 7, 3000);
  waitMs(40);
  e->start(nullptr, "d2d4 d7d5 c2c4", 5, 400);   // supersedes it
  while (e->thinking()) waitMs(5);
  check(e->take(uci), "seam: a start() that supersedes another still yields a move (%s)", uci);
  arrocco::chess::Game second;
  bool built = true;
  for (const char* mv : {"d2d4", "d7d5", "c2c4"}) built = built && second.playUci(mv);
  check(built, "seam: the rules built the second line");
  check(!second.position().parseUci(uci).isNone(),
        "seam: and the move is legal in the SECOND position, not the first (%s)", uci);

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
