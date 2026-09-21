// SPDX-License-Identifier: GPL-3.0-or-later
// Perft on the six standard positions: the move generator is right only if every count matches.
#include <chrono>
#include <cstdio>
#include "arrocco/chess/position.h"

using arrocco::chess::Position;

struct Case { const char* name; const char* fen; int depth; unsigned long long nodes; };

static const Case kCases[] = {
  {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL},
  {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL},
  {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624ULL},
  {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333ULL},
  {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL},
  {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL},
};

int main() {
  int failures = 0;
  unsigned long long total = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (const Case& c : kCases) {
    Position p;
    if (!p.setFen(c.fen)) { std::printf("FAIL %-11s FEN rejected\n", c.name); failures++; continue; }
    unsigned long long n = arrocco::chess::perft(p, c.depth);
    total += n;
    bool ok = n == c.nodes;
    std::printf("%s %-11s d%d  got %llu  expected %llu\n", ok ? "ok  " : "FAIL", c.name, c.depth, n, c.nodes);
    if (!ok) failures++;
  }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("%llu nodes in %.2f s = %.1f Mnps\n", total, s, total / s / 1e6);
  return failures ? 1 : 0;
}
