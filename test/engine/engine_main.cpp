// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco — native tests for the CT800 port (lib/ct800/port).
//
// Everything here goes through ct800::Searcher, the same class the firmware and the
// simulator drive, and every move it returns is judged by arrocco::chess::Position:
// the rules library is the referee, the engine is the thing under test.
//
//   make -C test/engine test
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "arrocco/chess/game.h"
#include "arrocco/chess/position.h"
#include "arrocco/engine.h"
#include "ct800_searcher.h"

using arrocco::chess::Move;
using arrocco::chess::MoveList;
using arrocco::chess::Position;

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
  if (condition) {
    printf("  ok  [%d] %s\n", g_checks, text);
  } else {
    printf("FAIL  [%d] %s\n", g_checks, text);
    ++g_failures;
  }
}

int64_t nowMs() {
  using Clock = std::chrono::steady_clock;
  static const Clock::time_point start = Clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void sleepMs(int32_t ms) {
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---- the referee --------------------------------------------------------------------------

// Can the side to move force mate within `plies`? Plain alpha-beta-free recursion on the
// rules library: at these depths (3 plies) it costs nothing and it owes the engine nothing.
bool forcedMateIn(const Position& pos, int plies) {
  if (plies <= 0) return false;
  MoveList moves;
  Position work = pos;
  work.generateLegalMoves(moves);
  if (moves.empty()) return false;
  for (int i = 0; i < moves.size(); ++i) {
    Position after = pos;
    arrocco::chess::Undo undo;
    after.make(moves[i], undo);
    if (after.isCheckmate()) return true;
    if (plies == 1) continue;
    // Every reply of the opponent must run into a mate one move sooner.
    MoveList replies;
    after.generateLegalMoves(replies);
    if (replies.empty()) continue;  // stalemate: not a mate
    bool allLose = true;
    for (int j = 0; j < replies.size() && allLose; ++j) {
      Position next = after;
      arrocco::chess::Undo undo2;
      next.make(replies[j], undo2);
      if (!forcedMateIn(next, plies - 2)) allLose = false;
    }
    if (allLose) return true;
  }
  return false;
}

ct800::Limits limitsFor(int level, uint32_t timeMs) {
  const arrocco::EngineLevel& l = arrocco::kEngineLevels[arrocco::clampEngineLevel(level)];
  ct800::Limits limits;
  limits.timeMs = timeMs != 0 ? timeMs : l.timeMs;
  limits.maxDepth = l.maxDepth;
  limits.cpuSpeed = 100;
  limits.maxNps = l.maxNps;
  limits.noisePercent = l.noisePercent;
  limits.useBook = l.useBook;
  return limits;
}

// The one Searcher this process may have (see ct800_searcher.h).
ct800::Searcher g_searcher;
alignas(8) unsigned char g_hash[ct800::kHashMinBytes];

// Asks for a move and hands back what the RULES make of it in `judge` — which is the
// position after `moves`, not the one `fen` describes. Move::none() means the engine
// gave nothing, or gave something the rules refuse.
Move askIn(const Position& judge, const char* fen, const char* moves, int level, uint32_t timeMs,
           char out[6], uint64_t* nodes = nullptr, int64_t* spent = nullptr) {
  strcpy(out, "0000");
  if (!g_searcher.setPosition(fen, moves)) {
    printf("      (the engine refused the position)\n");
    return Move::none();
  }
  if (!g_searcher.thinkTimed(limitsFor(level, timeMs), out, nodes, spent)) return Move::none();
  return judge.parseUci(out);
}

// The common case: no move list, so the judge is the FEN itself.
Move askFrom(const char* fen, int level, uint32_t timeMs, char out[6]) {
  Position pos;
  if (!pos.setFen(fen)) {
    printf("      (bad test FEN: %s)\n", fen);
    strcpy(out, "0000");
    return Move::none();
  }
  return askIn(pos, fen, "", level, timeMs, out);
}

// ---- the tests ------------------------------------------------------------------------------

void testMateInOne() {
  const char* kFen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";
  Position pos;
  pos.setFen(kFen);
  check(forcedMateIn(pos, 1), "mate in 1: the referee agrees the position is one");

  char uci[6];
  const Move m = askFrom(kFen, 7, 1000, uci);
  check(!m.isNone(), "mate in 1: the engine answered with a legal move (%s)", uci);
  if (m.isNone()) return;
  arrocco::chess::Undo undo;
  pos.make(m, undo);
  check(pos.isCheckmate(), "mate in 1: %s is mate", uci);
}

void testMateInTwo() {
  // White Kg1 Ra1 Rb6, Black Kh8 pawn d5. The ladder: 1.Rb7 (or 1.Ra7) takes the whole
  // seventh rank away and threatens Ra8#, and neither 1...Kg8 nor 1...d4 changes that.
  // No mate in 1 exists: after Ra8+ or Rb8+ the king simply walks to h7.
  const char* kFen = "7k/8/1R6/3p4/8/8/8/R5K1 w - - 0 1";
  Position pos;
  check(pos.setFen(kFen), "mate in 2: the FEN parses");
  check(!forcedMateIn(pos, 1), "mate in 2: the referee sees no mate in 1");
  check(forcedMateIn(pos, 3), "mate in 2: the referee sees a forced mate in 2");

  char uci[6];
  const Move m = askFrom(kFen, 7, 2000, uci);
  check(!m.isNone(), "mate in 2: the engine answered with a legal move (%s)", uci);
  if (m.isNone()) return;
  Position after = pos;
  arrocco::chess::Undo undo;
  after.make(m, undo);
  // After the engine's move White mates on the next move, whatever Black does.
  MoveList replies;
  after.generateLegalMoves(replies);
  bool allMated = !replies.empty();
  for (int i = 0; i < replies.size() && allMated; ++i) {
    Position next = after;
    arrocco::chess::Undo undo2;
    next.make(replies[i], undo2);
    if (!forcedMateIn(next, 1)) allMated = false;
  }
  check(allMated, "mate in 2: %s forces mate on the next move against every reply", uci);
}

void testNeverIllegal() {
  // 40 moves of self play at the weakest level - the one most likely to produce
  // nonsense, because its evaluation is pure noise. A game that ends before the 80th
  // ply (this level walks into mate readily) is replaced by a fresh one, so the count
  // is 80 plies of engine answers however many games that takes.
  arrocco::chess::Game game;
  char moves[arrocco::kEngineMovesSize];
  char uci[6];
  char fen[arrocco::chess::kFenBufferSize];
  int plies = 0;
  int games = 1;
  bool allLegal = true;

  while (plies < 80 && allLegal) {
    if (game.isOver()) {
      game.position().toFen(fen, sizeof fen);
      printf("      game %d ended after %d plies: %s\n", games, game.plyCount(), fen);
      game.newGame();
      ++games;
      continue;
    }
    if (game.uciMoveList(moves, sizeof moves) < 0) break;
    const Move m = askIn(game.position(), arrocco::chess::kStartposFen, moves, 0, 120, uci);
    if (m.isNone() || !game.position().isLegal(m)) {
      game.position().toFen(fen, sizeof fen);
      printf("      the engine returned '%s', which is not legal in %s\n", uci, fen);
      allLegal = false;
      break;
    }
    game.play(m);
    ++plies;
  }
  check(allLegal, "self play at level 1: %d plies over %d game(s), every move legal by the rules",
        plies, games);
  check(plies >= 80, "self play at level 1: it reached 40 moves of engine answers (%d plies)",
        plies);
  game.position().toFen(fen, sizeof fen);
  printf("      last position: %s\n", fen);
}

void testAbort() {
  const char* kFen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";
  check(g_searcher.setPosition(kFen, ""), "abort: the engine took the position");

  ct800::Limits limits = limitsFor(7, 30000);   // half a minute of thinking to interrupt
  limits.useBook = false;
  std::atomic<int64_t> returnedAt{0};
  char uci[6];
  std::thread worker([&] {
    g_searcher.think(limits, uci);
    returnedAt.store(nowMs());
  });

  sleepMs(300);                                  // let it get properly started
  check(g_searcher.thinking(), "abort: the search is running after 300 ms");
  const int64_t abortedAt = nowMs();
  g_searcher.abortSearch();
  worker.join();
  const int64_t took = returnedAt.load() - abortedAt;
  printf("      abort -> think() returned in %lld ms\n", static_cast<long long>(took));
  check(took >= 0 && took < 400, "abort: think() came back %lld ms later (budget ~200 ms)",
        static_cast<long long>(took));
  check(took < 30000, "abort: it did not simply use up its 30 s");
}

void testStrongMoveAndRepeat() {
  // A queen en prise to a pawn. Anything but exd5 is a blunder at this level.
  const char* kFen = "4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1";
  char first[6];
  char second[6];
  const Move a = askFrom(kFen, 7, 1000, first);
  const Move b = askFrom(kFen, 7, 1000, second);
  check(!a.isNone(), "repeatability: first answer is legal (%s)", first);
  check(!b.isNone(), "repeatability: second answer is legal (%s)", second);
  check(strcmp(first, "e4d5") == 0, "reasonable: level 8 takes the hanging queen (%s)", first);
  check(strcmp(first, second) == 0, "repeatability: same position, same level, same time -> %s / %s",
        first, second);
}

void testNodeRate() {
  const char* kFen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";
  char uci[6];
  uint64_t nodes = 0;
  int64_t spent = 0;
  ct800::Limits limits = limitsFor(7, 3000);
  limits.useBook = false;
  check(g_searcher.setPosition(kFen, ""), "node rate: the engine took the position");
  const bool found = g_searcher.thinkTimed(limits, uci, &nodes, &spent);
  check(found, "node rate: a move came back (%s)", uci);
  const unsigned long long nps = spent > 0 ? (nodes * 1000ull) / static_cast<uint64_t>(spent) : 0ull;
  printf("      %llu nodes in %lld ms = %llu nodes/s, hash %zu entries per table (%zu KB total)\n",
         static_cast<unsigned long long>(nodes), static_cast<long long>(spent), nps,
         g_searcher.hashEntries(), sizeof g_hash / 1024u);
  check(nps > 1000ull, "node rate: %llu nodes/s is a search, not a stall", nps);
}

void testLevelsAreOrdered() {
  bool ok = true;
  for (int i = 1; i < arrocco::kEngineLevelCount; ++i) {
    const arrocco::EngineLevel& lower = arrocco::kEngineLevels[i - 1];
    const arrocco::EngineLevel& higher = arrocco::kEngineLevels[i];
    if (higher.noisePercent > lower.noisePercent) ok = false;
    if (higher.maxDepth < lower.maxDepth) ok = false;
    if (higher.timeMs < lower.timeMs) ok = false;
  }
  check(ok, "levels: noise falls, depth and time rise, from Beginner to Expert");
  check(arrocco::kEngineLevels[0].noisePercent == 100,
        "levels: the bottom level's evaluation is pure noise (what makes it losable)");
}


// ---- the position round trip: what the UI hands over must arrive intact ------------------

// Every engine reply is judged in the position the ENGINE was given, which is also the
// position the UI would be in. A castling right or an en passant square lost in the FEN
// round trip shows up here as a move the rules refuse, or as a move the rules allow but
// that the engine could not have found without that right.
void testCastlingAndEnPassantSurvive() {
  char uci[6];

  // Black has just played d7-d5; White's c5 pawn can take it en passant on d6.
  {
    const char* kFen = "4k3/8/8/2Pp4/8/8/6PP/4K2R w K d6 0 1";
    Position judge;
    check(judge.setFen(kFen), "en passant: the FEN parses");
    // The rules library drops an en passant square that is not really capturable, so if
    // it kept c6 the capture is legal; assert that first, then that the engine sees it.
    check(judge.enPassantSquare() != arrocco::chess::kNoSquare,
          "en passant: the rules kept the d6 target");
    char back[arrocco::chess::kFenBufferSize];
    check(judge.toFen(back, sizeof back) > 0 && strstr(back, " d6 ") != nullptr,
          "en passant: toFen() writes the square the engine is given (%s)", back);
    const Move m = askIn(judge, back, "", 7, 500, uci);
    check(!m.isNone(), "en passant: the engine's reply from the round-tripped FEN is legal (%s)", uci);
  }

  // White to move with only the king side right left: the engine must be ABLE to castle.
  // 0-0 is not forced, so assert the weaker, sufficient thing: the rules generated it
  // from the FEN the UI would write, and the engine's answer is legal in that position.
  {
    const char* kFen = "r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R w Kkq - 6 9";
    Position judge;
    check(judge.setFen(kFen), "castling: the FEN parses");
    char back[arrocco::chess::kFenBufferSize];
    judge.toFen(back, sizeof back);
    check(strstr(back, " Kkq ") != nullptr,
          "castling: the rights the UI writes are exactly the ones read (%s)", back);
    check(!judge.findLegalMove(arrocco::chess::squareFromName("e1"),
                               arrocco::chess::squareFromName("g1")).isNone(),
          "castling: 0-0 is legal for White and 0-0-0 is not (right Q is gone)");
    check(judge.findLegalMove(arrocco::chess::squareFromName("e1"),
                              arrocco::chess::squareFromName("c1")).isNone(),
          "castling: the engine is not offered the right White does not have");
    const Move m = askIn(judge, back, "", 6, 400, uci);
    check(!m.isNone(), "castling: the engine's reply is legal in that exact position (%s)", uci);
  }
}

// The two ways the game can end under the engine's feet. CT800 returns COMP_MATE /
// COMP_STALE, the glue turns both into "no move", and "0000" is what the seam promises.
void testNoMoveWhenTheGameIsOver() {
  char uci[6];
  const struct { const char* fen; const char* what; } kCases[] = {
      {"7k/5KQ1/8/8/8/8/8/8 b - - 0 1", "checkmated"},
      {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", "stalemated"},
  };
  for (const auto& c : kCases) {
    Position judge;
    check(judge.setFen(c.fen), "%s: the FEN parses", c.what);
    check(!judge.hasLegalMoves(), "%s: the rules agree there is no move", c.what);
    check(g_searcher.setPosition(c.fen, ""), "%s: the engine took the position", c.what);
    const bool found = g_searcher.think(limitsFor(5, 300), uci);
    check(!found, "%s: think() reports no move", c.what);
    check(strcmp(uci, "0000") == 0, "%s: and writes \"0000\", not a stale move (%s)", c.what, uci);
  }
}

// After a promotion and after a take-back the engine is handed a DIFFERENT position from
// the one it last searched. This plays both transitions through the same move list the UI
// builds and insists every reply is legal in the position that was actually sent.
void testPromotionAndTakeBack() {
  char uci[6];
  arrocco::chess::Game game;
  // 1.a4 b5 2.axb5 a6 3.bxa6 Nc6 4.a7 Rb8 — White to move, a8 empty, a7-a8=Q available.
  const char* kLine[] = {"a2a4", "b7b5", "a4b5", "a7a6", "b5a6", "b8c6", "a6a7", "a8b8"};
  for (const char* mv : kLine) {
    if (!game.playUci(mv)) { check(false, "promotion: the rules refused %s", mv); return; }
  }
  char moves[arrocco::kEngineMovesSize];
  check(game.uciMoveList(moves, sizeof moves) > 0, "promotion: the UI can write the whole line");
  check(game.position().isPromotionMove(arrocco::chess::squareFromName("a7"),
                                        arrocco::chess::squareFromName("a8")),
        "promotion: a7-a8 is a promotion in the position the engine gets");

  // Play the promotion the way the UI does, then ask the engine in the NEW position.
  const Move promo = game.position().findLegalMove(arrocco::chess::squareFromName("a7"),
                                                   arrocco::chess::squareFromName("a8"),
                                                   arrocco::chess::PieceType::Queen);
  check(!promo.isNone() && game.play(promo), "promotion: a7-a8=Q played");
  check(game.uciMoveList(moves, sizeof moves) > 0, "promotion: the line still fits the buffer");
  Move m = askIn(game.position(), nullptr, moves, 6, 500, uci);
  check(!m.isNone(), "promotion: the reply right after a promotion is legal (%s)", uci);

  // Now take the promotion back and ask again: the engine must answer the position it is
  // GIVEN, not the one it searched a moment ago.
  check(game.takeBack(), "take back: the promotion came off the board");
  check(game.uciMoveList(moves, sizeof moves) >= 0, "take back: the shorter line is written");
  m = askIn(game.position(), nullptr, moves, 6, 500, uci);
  check(!m.isNone(), "take back: the reply after Undo is legal in the position sent (%s)", uci);
  check(game.position().isLegal(m), "take back: and the rules confirm it move by move (%s)", uci);
}

// Seeding. Without a seed the engine is reproducible, which is what these tests rely on;
// with two different seeds the weak levels must NOT play the same game, or every match
// against "Beginner" would be the same match.
void testSeedChangesTheWeakLevels() {
  char line[2][256] = {{0}, {0}};
  char uci[6];
  for (int run = 0; run < 2; ++run) {
    g_searcher.seed(run == 0 ? 1u : 0xC0FFEEu);
    arrocco::chess::Game game;
    char moves[arrocco::kEngineMovesSize];
    for (int ply = 0; ply < 6; ++ply) {
      if (game.uciMoveList(moves, sizeof moves) < 0) break;
      const Move m = askIn(game.position(), nullptr, moves, 0, 120, uci);
      if (m.isNone() || !game.play(m)) break;
      strncat(line[run], uci, 8);
      strncat(line[run], " ", 2);
    }
  }
  check(strcmp(line[0], line[1]) != 0,
        "seed: two seeds, two different level-1 games (%s| %s)", line[0], line[1]);
}

}  // namespace

int main() {
  printf("== CT800 port\n");
  if (!g_searcher.begin(g_hash, sizeof g_hash, nowMs, sleepMs)) {
    printf("FAIL: the searcher refused %zu bytes of hash memory\n", sizeof g_hash);
    return 1;
  }
  printf("   hash: %zu KB, %zu entries per transposition table\n", sizeof g_hash / 1024u,
         g_searcher.hashEntries());

  testLevelsAreOrdered();
  testMateInOne();
  testMateInTwo();
  testStrongMoveAndRepeat();
  testNodeRate();
  testAbort();
  testCastlingAndEnPassantSurvive();
  testNoMoveWhenTheGameIsOver();
  testPromotionAndTakeBack();
  testSeedChangesTheWeakLevels();
  testNeverIllegal();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
