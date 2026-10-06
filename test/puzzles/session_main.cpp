// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the puzzle mode's logic, on the real pack:
//   - the rating (ui/puzzle_progress.h): Glicko-1 against values computed independently,
//     its limits, and a simulated solver of known strength that it has to find, from a
//     level picked right and from one picked 500 points wrong;
//   - the choice of the next puzzle: every bucket's walk is a permutation of the bucket,
//     no puzzle comes back before its bucket has been seen, seeds give other orders;
//   - the progress blob: round trip, every bit flipped, every length, every field;
//   - the session (ui/puzzle_session.h): EVERY puzzle of the pack solved through it, move
//     by move, with the opponent's replies; and on samples a wrong try, the hint, the
//     solution, skip, retry, a different mate, the wrong promotion piece, and a power cut
//     in the middle of a puzzle that must neither lose the ply nor count the score twice.
// Plain checks with messages; every failure is printed, the exit code is non-zero if any failed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "arrocco/chess/move.h"
#include "arrocco/chess/position.h"
#include "arrocco/puzzles/puzzles.h"
#include "arrocco/ui/puzzle_progress.h"
#include "arrocco/ui/puzzle_session.h"
#include "arrocco/ui/saved_game.h"

namespace puzzles = arrocco::puzzles;
using arrocco::chess::Color;
using arrocco::chess::Move;
using arrocco::chess::MoveList;
using arrocco::chess::Position;
using arrocco::chess::Undo;
using arrocco::ui::PuzzleProgress;
using arrocco::ui::PuzzleRating;
using arrocco::ui::PuzzleScore;
using arrocco::ui::PuzzleSession;
using arrocco::ui::ProgressCheck;
namespace ui = arrocco::ui;

namespace {

long long g_checks = 0;
long long g_failures = 0;

#define CHECK(cond, ...)                                       \
  do {                                                         \
    ++g_checks;                                                \
    if (!(cond)) {                                             \
      ++g_failures;                                            \
      if (g_failures <= 40) {                                  \
        std::printf("FAIL line %d: ", __LINE__);               \
        std::printf(__VA_ARGS__);                              \
        std::printf("   [%s]\n", #cond);                       \
      }                                                        \
    }                                                          \
  } while (0)

void section(const char* name, long long before) {
  std::printf("ok   %-34s %lld checks\n", name, g_checks - before);
}

// ---- helpers --------------------------------------------------------------------------------

using Verdict = PuzzleSession::Verdict;
using Pending = PuzzleSession::Pending;

PuzzleProgress levelProgress(int level, uint32_t entropy = 7) {
  PuzzleProgress p;
  ui::startAtLevel(p, level, entropy);
  return p;
}

// A session showing puzzle `index` at its start, no animation (resume() at ply 0).
bool openAt(PuzzleSession& s, PuzzleProgress p, int index) {
  p.current = static_cast<uint16_t>(index);
  p.plies = 0;
  p.flags = 0;
  s.setProgress(p);
  return s.resume();
}

int solution(int index, Move* out) { return puzzles::solutionMoves(index, out, puzzles::kMaxSolutionPlies); }

bool sameSan(const Position& before, Move m, const char* san) {
  char want[arrocco::chess::kSanBufferSize];
  before.toSan(m, want);
  return std::strcmp(want, san) == 0;
}

// Plays the rest of the line from the solver's turn, flushing every reply. Returns the
// last verdict (Solved when it all went through).
Verdict finishLine(PuzzleSession& s) {
  Verdict last = Verdict::Ignored;
  for (int guard = 0; guard < 16 && s.solversTurn(); ++guard) {
    Move line[puzzles::kMaxSolutionPlies];
    const int n = solution(s.index(), line);
    const int ply = s.progress().plies;
    if (ply >= n) break;
    last = s.play(line[ply]);
    if (last == Verdict::Correct) s.playPending();
    if (last != Verdict::Correct) break;
  }
  return last;
}

// A legal move of the session's board that the puzzle refuses, or none().
Move wrongMoveFor(const PuzzleSession& s) {
  MoveList legal;
  s.board().generateLegalMoves(legal);
  for (Move m : legal) {
    PuzzleSession probe = s;
    if (probe.play(m) == Verdict::Wrong) return m;
  }
  return Move::none();
}

// xorshift32: a fixed-seed coin for the simulated solvers.
struct Rng {
  uint32_t state;
  double unit() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state >> 8) / 16777216.0;
  }
};

// ---- the rating -------------------------------------------------------------------------------

void testExpected() {
  const long long before = g_checks;
  CHECK(std::fabs(ui::expectedSolve(1500, 1500) - 0.5) < 1e-12, "equal ratings: one chance in two");
  CHECK(std::fabs(ui::expectedSolve(1900, 1500) - 10.0 / 11.0) < 1e-9, "400 points above: ten to one");
  for (int a = 100; a <= 3000; a += 150)
    for (int b = 600; b <= 2000; b += 100)
      CHECK(std::fabs(ui::expectedSolve(a, b) + ui::expectedSolve(b, a) - 1.0) < 1e-12, "symmetry %d %d", a, b);
  section("rating: expected score", before);
}

void testUpdate() {
  const long long before = g_checks;
  // Computed with Python's math module from the formulas in puzzle_progress.h, then rounded.
  struct Case {
    uint16_t rating, deviation;
    int puzzle;
    PuzzleScore score;
    uint16_t wantRating, wantDeviation;
  };
  const Case cases[] = {
      {1500, 350, 1500, PuzzleScore::Solved, 1675, 247}, {1500, 350, 1500, PuzzleScore::Missed, 1325, 247},
      {1500, 350, 1500, PuzzleScore::Hinted, 1500, 247}, {1100, 200, 1130, PuzzleScore::Missed, 1021, 173},
      {1100, 200, 1130, PuzzleScore::Solved, 1194, 173}, {1400, 60, 1300, PuzzleScore::Solved, 1407, 60},
      {1400, 60, 1300, PuzzleScore::Missed, 1387, 60},   {1400, 60, 1300, PuzzleScore::Hinted, 1397, 60},
      {800, 200, 600, PuzzleScore::Missed, 659, 179},    {2000, 100, 1900, PuzzleScore::Solved, 2019, 96},
      {100, 60, 600, PuzzleScore::Missed, 100, 60},      {2990, 60, 1999, PuzzleScore::Solved, 2990, 60},
      {3000, 60, 600, PuzzleScore::Solved, 3000, 60},
  };
  for (const Case& c : cases) {
    PuzzleRating r;
    r.rating = c.rating;
    r.deviation = c.deviation;
    const PuzzleRating out = ui::updatedRating(r, c.puzzle, c.score);
    CHECK(out.rating == c.wantRating && out.deviation == c.wantDeviation, "%u/%u vs %d score %d: got %u/%u, want %u/%u",
          c.rating, c.deviation, c.puzzle, static_cast<int>(c.score), out.rating, out.deviation, c.wantRating,
          c.wantDeviation);
  }
  // Everywhere: solving is worth more than a hint, a hint more than a miss; nobody goes the
  // wrong way; the doubt never grows and never leaves its range.
  for (int rating = ui::kPuzzleRatingFloor; rating <= ui::kPuzzleRatingCeiling; rating += 50) {
    for (int dev = ui::kPuzzleDeviationMin; dev <= ui::kPuzzleDeviationMax; dev += 10) {
      for (int puzzle = 600; puzzle <= 2000; puzzle += 100) {
        PuzzleRating r;
        r.rating = static_cast<uint16_t>(rating);
        r.deviation = static_cast<uint16_t>(dev);
        const PuzzleRating s = ui::updatedRating(r, puzzle, PuzzleScore::Solved);
        const PuzzleRating h = ui::updatedRating(r, puzzle, PuzzleScore::Hinted);
        const PuzzleRating m = ui::updatedRating(r, puzzle, PuzzleScore::Missed);
        CHECK(s.rating >= h.rating && h.rating >= m.rating, "order %d/%d vs %d", rating, dev, puzzle);
        CHECK(s.rating >= rating && m.rating <= rating, "direction %d/%d vs %d", rating, dev, puzzle);
        CHECK(s.deviation <= dev && s.deviation >= ui::kPuzzleDeviationMin, "deviation %d/%d", rating, dev);
        CHECK(s.rating >= ui::kPuzzleRatingFloor && m.rating >= ui::kPuzzleRatingFloor &&
                  s.rating <= ui::kPuzzleRatingCeiling,
              "range %d/%d vs %d", rating, dev, puzzle);
      }
    }
  }
  // The target: the rating itself while uncertain, kPuzzleComfortOffset below once settled.
  PuzzleRating r;
  r.rating = 1200;
  r.deviation = ui::kPuzzleSettledDeviation;
  CHECK(ui::targetRating(r) == 1200, "uncertain: puzzles at the rating");
  r.deviation = ui::kPuzzleSettledDeviation - 1;
  CHECK(ui::targetRating(r) == 1200 - ui::kPuzzleComfortOffset, "settled: a little below");
  r.rating = ui::kPuzzleRatingFloor;
  CHECK(ui::targetRating(r) == 0, "never a negative target");
  section("rating: Glicko-1 update", before);
}

void testScoreAndLevels() {
  const long long before = g_checks;
  for (int level = 0; level < ui::kPuzzleLevelCount; ++level) {
    PuzzleProgress p;
    p.drawn[11] = 42;
    p.played = 9;
    ui::startAtLevel(p, level, 1234);
    CHECK(p.started && p.rating.rating == ui::kPuzzleLevelRatings[level] &&
              p.rating.deviation == ui::kPuzzleStartDeviation,
          "level %d: its rating, the start deviation", level);
    CHECK(p.played == 0 && p.solved == 0 && p.streak == 0 && p.lastChange == 0, "level %d: counts restart", level);
    CHECK(p.seed != 0 && p.current == ui::kNoPuzzle && p.plies == 0 && p.flags == 0, "level %d: seed, no puzzle", level);
    CHECK(p.drawn[11] == 42, "level %d: the walks go on", level);
    const uint32_t seed = p.seed;
    ui::startAtLevel(p, level, 999);
    CHECK(p.seed == seed, "level %d: the seed is drawn once", level);
  }
  CHECK(levelProgress(0, 1).seed != levelProgress(0, 2).seed, "another moment, another seed");
  CHECK(levelProgress(-3).rating.rating == ui::kPuzzleLevelRatings[0] &&
            levelProgress(99).rating.rating == ui::kPuzzleLevelRatings[ui::kPuzzleLevelCount - 1],
        "levels out of range are clamped");

  PuzzleProgress p = levelProgress(1);
  ui::scorePuzzle(p, 1100, PuzzleScore::Solved);
  ui::scorePuzzle(p, 1100, PuzzleScore::Hinted);
  CHECK(p.played == 2 && p.solved == 2 && p.streak == 2, "solved and hinted both count as solved");
  const uint16_t beforeMiss = p.rating.rating;
  ui::scorePuzzle(p, 1100, PuzzleScore::Missed);
  CHECK(p.played == 3 && p.solved == 2 && p.streak == 0, "a miss ends the streak");
  CHECK(p.lastChange == static_cast<int16_t>(p.rating.rating - beforeMiss) && p.lastChange < 0,
        "lastChange is what the miss cost (%d)", p.lastChange);
  section("progress: levels and scores", before);
}

// A simulated solver of strength `strength` (Lichess puzzle scale) works through `n`
// puzzles drawn the board's way, starting at `level`. Returns the rating it ends at.
int simulate(int strength, int level, int n, uint32_t seed) {
  PuzzleProgress p = levelProgress(level, seed);
  Rng rng{seed * 2654435761u + 1};
  for (int i = 0; i < n; ++i) {
    const int index = ui::drawPuzzle(p);
    puzzles::Puzzle puzzle;
    if (!puzzles::byIndex(index, puzzle)) return -1;
    const bool solved = rng.unit() < ui::expectedSolve(strength, puzzle.rating);
    ui::scorePuzzle(p, puzzle.rating, solved ? PuzzleScore::Solved : PuzzleScore::Missed);
  }
  return p.rating.rating;
}

void testConvergence() {
  const long long before = g_checks;
  struct Who {
    const char* name;
    int strength;
    int level;   // the level picked: right, or 500 points off
  };
  const Who people[] = {
      {"a beginner child, picked Beginner", 650, 0},
      {"a beginner child, picked Casual", 600, 1},
      {"a casual player, picked Casual", 1150, 1},
      {"a club player, picked Club", 1450, 2},
      {"a strong club player, picked Club", 1900, 2},
      {"a strong club player, picked Strong", 1850, 3},
  };
  for (const Who& who : people) {
    int within100 = 0;
    long sum = 0;
    const int runs = 200;
    for (uint32_t seed = 1; seed <= static_cast<uint32_t>(runs); ++seed) {
      const int rating = simulate(who.strength, who.level, 40, seed);
      sum += rating;
      if (std::abs(rating - who.strength) <= 100) ++within100;
    }
    const int mean = static_cast<int>(sum / runs);
    std::printf("     %-38s strength %4d: after 40 puzzles %4d on average, %3d%% within 100\n", who.name,
                who.strength, mean, within100 * 100 / runs);
    CHECK(std::abs(mean - who.strength) <= 80, "%s: the mean rating after 40 puzzles is %d", who.name, mean);
    CHECK(within100 * 100 / runs >= 60, "%s: %d of %d runs within 100 points", who.name, within100, runs);
  }
  section("rating: simulated solvers converge", before);
}

// ---- the choice ------------------------------------------------------------------------------------

int bucketOfIndex(int index) {
  puzzles::Puzzle p;
  return puzzles::byIndex(index, p) ? p.rating / ui::kPuzzleBucketWidth : -1;
}

void testBuckets() {
  const long long before = g_checks;
  int sizes[ui::kPuzzleBuckets] = {};
  for (int i = 0; i < puzzles::count(); ++i) {
    const int b = bucketOfIndex(i);
    if (b >= 0 && b < ui::kPuzzleBuckets) ++sizes[b];
  }
  int first = -1;
  int last = -1;
  for (int b = 0; b < ui::kPuzzleBuckets; ++b) {
    if (sizes[b] == 0) continue;
    if (first < 0) first = b;
    last = b;
  }
  CHECK(first == 6 && last == 19, "the pack spans buckets %d..%d (600..1999)", first, last);
  CHECK(ui::bucketFor(0) == first && ui::bucketFor(450) == first && ui::bucketFor(-50) == first,
        "below the pack: its easiest bucket");
  CHECK(ui::bucketFor(2500) == last && ui::bucketFor(99999) == last, "above the pack: its hardest bucket");
  for (int rating = 600; rating < 2000; rating += 37)
    CHECK(ui::bucketFor(rating) == rating / 100, "rating %d: bucket %d", rating, ui::bucketFor(rating));

  // Every walk is a permutation of its bucket, and starts over after it.
  const uint32_t seeds[] = {1u, 2u, 0xDEADBEEFu, 0x80000001u, 123456789u};
  static bool seen[4096];
  for (uint32_t seed : seeds) {
    for (int b = 0; b < ui::kPuzzleBuckets; ++b) {
      if (sizes[b] == 0) {
        CHECK(ui::puzzleInWalk(seed, b, 0) == -1, "bucket %d is empty", b);
        continue;
      }
      std::memset(seen, 0, sizeof seen);
      bool inBucket = true;
      bool distinct = true;
      for (int k = 0; k < sizes[b]; ++k) {
        const int index = ui::puzzleInWalk(seed, b, static_cast<uint32_t>(k));
        if (index < 0 || index >= puzzles::count() || bucketOfIndex(index) != b) {
          inBucket = false;
          continue;
        }
        if (seen[index]) distinct = false;
        seen[index] = true;
      }
      CHECK(inBucket && distinct, "seed %08x bucket %d: %d draws, all different, all in the bucket", seed, b,
            sizes[b]);
      CHECK(ui::puzzleInWalk(seed, b, static_cast<uint32_t>(sizes[b])) == ui::puzzleInWalk(seed, b, 0),
            "seed %08x bucket %d: the walk starts over", seed, b);
    }
  }
  // Two boards do not see the same puzzles in the same order.
  int sameStart = 0;
  for (int b = first; b <= last; ++b)
    if (ui::puzzleInWalk(1u, b, 0) == ui::puzzleInWalk(2u, b, 0) &&
        ui::puzzleInWalk(1u, b, 1) == ui::puzzleInWalk(2u, b, 1))
      ++sameStart;
  CHECK(sameStart == 0, "two seeds start %d buckets the same way", sameStart);

  // drawPuzzle: a whole bucket without a repeat, at the rating's bucket.
  PuzzleProgress p = levelProgress(1);   // 1100, uncertain: puzzles at 1100..1199
  std::memset(seen, 0, sizeof seen);
  bool fresh = true;
  bool right = true;
  for (int k = 0; k < sizes[11]; ++k) {
    const int index = ui::drawPuzzle(p);
    if (index < 0 || seen[index]) fresh = false;
    if (index >= 0) seen[index] = true;
    if (bucketOfIndex(index) != 11) right = false;
  }
  CHECK(fresh && right && p.drawn[11] == sizes[11], "%d puzzles at 1100 before one comes back", sizes[11]);
  section("choice: buckets and walks", before);
}

// ---- the blob ----------------------------------------------------------------------------------------

void reseal(uint8_t* blob) {
  const uint32_t crc = ui::crc32(blob, ui::kPuzzleProgressBytes - 4);
  for (int i = 0; i < 4; ++i) blob[ui::kPuzzleProgressBytes - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

bool sameProgress(const PuzzleProgress& a, const PuzzleProgress& b) {
  if (a.started != b.started || a.rating.rating != b.rating.rating || a.rating.deviation != b.rating.deviation ||
      a.played != b.played || a.solved != b.solved || a.streak != b.streak || a.lastChange != b.lastChange ||
      a.seed != b.seed || a.current != b.current || a.plies != b.plies || a.flags != b.flags)
    return false;
  for (int i = 0; i < ui::kPuzzleBuckets; ++i)
    if (a.drawn[i] != b.drawn[i]) return false;
  return true;
}

void testBlob() {
  const long long before = g_checks;
  PuzzleProgress p = levelProgress(2, 77);
  p.rating.rating = 1523;
  p.rating.deviation = 91;
  p.played = 300;
  p.solved = 201;
  p.streak = 4;
  p.lastChange = -13;
  p.current = 2345;
  p.plies = 2;
  p.flags = ui::kPuzzleHinted | ui::kPuzzleScored | ui::kPuzzleOnScreen;
  for (int b = 0; b < ui::kPuzzleBuckets; ++b) p.drawn[b] = static_cast<uint16_t>(b * 1000 + 7);

  uint8_t blob[ui::kPuzzleProgressBytes + 8];
  CHECK(ui::encodePuzzleProgress(p, blob, ui::kPuzzleProgressBytes - 1) == 0, "too small a buffer: nothing");
  const size_t size = ui::encodePuzzleProgress(p, blob, sizeof blob);
  CHECK(size == ui::kPuzzleProgressBytes && size == 98, "98 bytes (%zu)", size);
  CHECK(std::memcmp(blob, "ARPZ\x01", 5) == 0, "magic and version");
  uint8_t again[ui::kPuzzleProgressBytes];
  ui::encodePuzzleProgress(p, again, sizeof again);
  CHECK(std::memcmp(blob, again, size) == 0, "the same progress, the same bytes");

  PuzzleProgress back;
  bool changed = true;
  CHECK(ui::decodePuzzleProgress(blob, size, back, changed) == ProgressCheck::Ok && !changed, "decodes");
  CHECK(sameProgress(back, p), "round trip, every field");

  // Every bit of it matters.
  int refused = 0;
  for (size_t byte = 0; byte < size; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      uint8_t bad[ui::kPuzzleProgressBytes];
      std::memcpy(bad, blob, size);
      bad[byte] ^= static_cast<uint8_t>(1u << bit);
      PuzzleProgress out = levelProgress(0);
      const PuzzleProgress untouched = out;
      if (ui::decodePuzzleProgress(bad, size, out, changed) != ProgressCheck::Ok && sameProgress(out, untouched))
        ++refused;
    }
  }
  CHECK(refused == static_cast<int>(size) * 8, "every flipped bit refused, progress untouched (%d of %zu)", refused,
        size * 8);
  // Every other length.
  for (size_t n = 0; n < size + 8; ++n) {
    if (n == size) continue;
    uint8_t longer[ui::kPuzzleProgressBytes + 8] = {};
    std::memcpy(longer, blob, n < size ? n : size);
    if (n > size) {
      std::memcpy(longer, blob, size);
      const uint32_t crc = ui::crc32(longer, n - 4);
      for (int i = 0; i < 4; ++i) longer[n - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    PuzzleProgress out;
    CHECK(ui::decodePuzzleProgress(longer, n, out, changed) != ProgressCheck::Ok, "length %zu refused", n);
  }
  PuzzleProgress out;
  CHECK(ui::decodePuzzleProgress(nullptr, size, out, changed) == ProgressCheck::TooShort, "no data");

  // Each check behind the CRC, with a CRC that matches.
  struct Edit {
    const char* what;
    size_t offset;
    uint8_t value;
    ProgressCheck want;
  };
  const Edit edits[] = {
      {"magic", 0, 'X', ProgressCheck::NotProgress},
      {"version 2", 4, 2, ProgressCheck::OtherVersion},
      {"an unknown flag", 5, 0x80, ProgressCheck::BadField},
      {"rating 0", 6, 0, ProgressCheck::BadField},  // with byte 7 cleared below
      {"deviation 0", 8, 0, ProgressCheck::BadField},
      {"more solved than played", 13, 0xFF, ProgressCheck::BadField},
      {"plies 12", 28, 12, ProgressCheck::BadField},
      {"31 buckets", 29, 31, ProgressCheck::BadLayout},
  };
  for (const Edit& e : edits) {
    uint8_t bad[ui::kPuzzleProgressBytes];
    std::memcpy(bad, blob, size);
    bad[e.offset] = e.value;
    if (e.offset == 6 || e.offset == 8) bad[e.offset + 1] = 0;
    reseal(bad);
    CHECK(ui::decodePuzzleProgress(bad, size, out, changed) == e.want, "%s: refused as it should", e.what);
  }
  {
    PuzzleProgress q = p;
    q.current = static_cast<uint16_t>(puzzles::count());
    ui::encodePuzzleProgress(q, again, sizeof again);
    CHECK(ui::decodePuzzleProgress(again, size, out, changed) == ProgressCheck::BadField, "an index past the pack");
    q = p;
    q.current = ui::kNoPuzzle;
    ui::encodePuzzleProgress(q, again, sizeof again);
    CHECK(ui::decodePuzzleProgress(again, size, out, changed) == ProgressCheck::BadField,
          "no puzzle, yet plies and flags about it");
    q.plies = 0;
    q.flags = 0;
    ui::encodePuzzleProgress(q, again, sizeof again);
    CHECK(ui::decodePuzzleProgress(again, size, out, changed) == ProgressCheck::Ok, "no puzzle on the board");
    q = p;
    q.seed = 0;
    ui::encodePuzzleProgress(q, again, sizeof again);
    CHECK(ui::decodePuzzleProgress(again, size, out, changed) == ProgressCheck::BadField, "seed 0");
    q = p;
    q.streak = static_cast<uint16_t>(q.solved + 1);
    ui::encodePuzzleProgress(q, again, sizeof again);
    CHECK(ui::decodePuzzleProgress(again, size, out, changed) == ProgressCheck::BadField, "a streak past the solved");
  }
  // Another pack: the player's numbers stay, the pack's are dropped.
  {
    uint8_t other[ui::kPuzzleProgressBytes];
    std::memcpy(other, blob, size);
    other[22] ^= 0x5A;
    reseal(other);
    PuzzleProgress kept;
    CHECK(ui::decodePuzzleProgress(other, size, kept, changed) == ProgressCheck::Ok && changed, "another pack: Ok, changed");
    bool walksGone = true;
    for (int b = 0; b < ui::kPuzzleBuckets; ++b)
      if (kept.drawn[b] != 0) walksGone = false;
    CHECK(kept.rating.rating == 1523 && kept.rating.deviation == 91 && kept.played == 300 && kept.solved == 201 &&
              kept.streak == 4 && kept.seed == p.seed,
          "another pack: rating, counts and seed kept");
    CHECK(kept.current == ui::kNoPuzzle && kept.plies == 0 && kept.flags == 0 && walksGone,
          "another pack: its puzzle and walks dropped");
  }
  CHECK(ui::packFingerprint() == ui::packFingerprint() && ui::packFingerprint() != 0, "the fingerprint is stable");
  section("progress blob", before);
}

// ---- the session --------------------------------------------------------------------------------

void testEveryPuzzle() {
  const long long before = g_checks;
  PuzzleProgress progress = levelProgress(1);
  int solvedAll = 0;
  int mates = 0;
  for (int i = 0; i < puzzles::count(); ++i) {
    PuzzleSession s;
    if (!openAt(s, progress, i)) {
      CHECK(false, "puzzle %d does not open", i);
      continue;
    }
    Position start;
    puzzles::startPosition(i, start);
    CHECK(s.board() == start && s.shownMove() == puzzles::blunderMove(i) && s.atStart(),
          "puzzle %d: the start position, the blunder marked", i);
    Position setup;
    puzzles::setupPosition(i, setup);
    CHECK(sameSan(setup, puzzles::blunderMove(i), s.shownSan()), "puzzle %d: the blunder's SAN", i);
    CHECK(s.solversTurn() && s.solver() == start.sideToMove() && !s.done() && !s.scored(), "puzzle %d: solver to move", i);

    Move line[puzzles::kMaxSolutionPlies];
    const int n = solution(i, line);
    Position pos = start;
    Verdict v = Verdict::Ignored;
    const uint16_t ratingBefore = s.progress().rating.rating;
    bool steps = true;
    for (int k = 0; k < n; k += 2) {
      v = s.play(line[k]);
      Position afterSolver = pos;
      Undo undo;
      afterSolver.make(line[k], undo);
      if (k + 1 < n) {
        steps &= v == Verdict::Correct && s.pending() == Pending::Reply && s.board() == afterSolver &&
                 s.shownMove() == line[k] && sameSan(pos, line[k], s.shownSan()) && !s.solversTurn();
        s.playPending();
        Position afterReply = afterSolver;
        afterReply.make(line[k + 1], undo);
        steps &= s.pending() == Pending::None && s.board() == afterReply && s.shownMove() == line[k + 1] &&
                 sameSan(afterSolver, line[k + 1], s.shownSan()) && s.progress().plies == k + 2;
        pos = afterReply;
      } else {
        steps &= v == Verdict::Solved && s.board() == afterSolver && s.shownMove() == line[k] &&
                 s.pending() == Pending::None && s.done() && !s.solversTurn();
      }
    }
    CHECK(steps, "puzzle %d: every move and reply shown as it should", i);
    CHECK(v == Verdict::Solved && s.scored() && s.progress().plies == n, "puzzle %d: solved", i);
    const PuzzleRating want = ui::updatedRating(progress.rating, s.puzzle().rating, PuzzleScore::Solved);
    CHECK(s.progress().rating.rating == want.rating && s.progress().played == progress.played + 1 &&
              s.progress().solved == progress.solved + 1,
          "puzzle %d: scored once, as solved (%u -> %u)", i, ratingBefore, s.progress().rating.rating);
    CHECK(s.play(line[0]) == Verdict::Ignored, "puzzle %d: nothing more to play", i);
    if (s.puzzle().endsInMate) {
      ++mates;
      CHECK(s.board().isCheckmate(), "puzzle %d: ends in mate", i);
    }
    if (v == Verdict::Solved) ++solvedAll;
  }
  std::printf("     %d puzzles solved through the session, %d of them by mate\n", solvedAll, mates);
  CHECK(solvedAll == puzzles::count(), "all %d solved", puzzles::count());
  section("session: every puzzle of the pack", before);
}

void testSetupAndSkip() {
  const long long before = g_checks;
  PuzzleSession s;
  s.setProgress(levelProgress(0, 3));
  CHECK(!s.loaded() && s.ensureLoaded() && s.loaded(), "nothing on the board: ensureLoaded draws one");
  CHECK(s.pending() == Pending::Setup && !s.solversTurn() && s.shownMove().isNone(), "it appears before the blunder");
  Position setup;
  puzzles::setupPosition(s.index(), setup);
  CHECK(s.board() == setup, "the position Lichess published");
  CHECK(s.play(puzzles::blunderMove(s.index())) == Verdict::Ignored || s.pending() == Pending::None,
        "a move during the setup is never the opponent's");
  s.setProgress(levelProgress(0, 3));
  s.ensureLoaded();
  const int first = s.index();
  s.playPending();
  Position start;
  puzzles::startPosition(first, start);
  CHECK(s.board() == start && s.shownMove() == puzzles::blunderMove(first), "then the blunder stands, marked");

  // Skip an untouched puzzle: a miss. Skip a scored one: nothing.
  const PuzzleProgress p0 = s.progress();
  CHECK(s.next() && s.index() != first && s.pending() == Pending::Setup, "Skip: the next puzzle appears");
  puzzles::Puzzle skipped;
  puzzles::byIndex(first, skipped);
  const PuzzleRating want = ui::updatedRating(p0.rating, skipped.rating, PuzzleScore::Missed);
  CHECK(s.progress().played == 1 && s.progress().solved == 0 && s.progress().rating.rating == want.rating,
        "a skip is scored as a miss (%u -> %u)", p0.rating.rating, s.progress().rating.rating);
  CHECK(s.progress().flags == 0 && s.progress().plies == 0 && s.progress().current == s.index(),
        "the new puzzle starts clean");
  s.playPending();
  finishLine(s);
  CHECK(s.done() && s.progress().played == 2, "the next one solved");
  const PuzzleProgress p1 = s.progress();
  s.next();
  CHECK(s.progress().played == 2 && s.progress().rating.rating == p1.rating.rating, "Next after a solved one: no score");

  // ensureLoaded on a puzzle left with its reply pending: the reply has been played.
  s.playPending();
  Move line[puzzles::kMaxSolutionPlies];
  const int n = solution(s.index(), line);
  if (n >= 3) {
    CHECK(s.play(line[0]) == Verdict::Correct && s.pending() == Pending::Reply, "a correct first move");
    s.ensureLoaded();
    CHECK(s.pending() == Pending::None && s.shownMove() == line[1], "coming back: the reply stands");
  }
  section("session: setup move and skip", before);
}

void testWrongHintSolution() {
  const long long before = g_checks;
  int wrongTried = 0;
  int hinted = 0;
  int shown = 0;
  for (int i = 0; i < puzzles::count(); i += 11) {
    // ---- a wrong try first, then the line: one miss, and no more
    {
      PuzzleSession s;
      openAt(s, levelProgress(2), i);
      const Move bad = wrongMoveFor(s);
      if (!bad.isNone()) {
        ++wrongTried;
        const Position board = s.board();
        const PuzzleRating want = ui::updatedRating(s.progress().rating, s.puzzle().rating, PuzzleScore::Missed);
        CHECK(s.play(bad) == Verdict::Wrong, "puzzle %d: the wrong move is refused", i);
        CHECK(s.board() == board && s.wrongMove() == bad && s.solversTurn(), "puzzle %d: the board stays", i);
        CHECK(s.scored() && (s.flags() & ui::kPuzzleMissed) && s.progress().played == 1 &&
                  s.progress().rating.rating == want.rating,
              "puzzle %d: scored as a miss at once", i);
        CHECK(s.play(bad) == Verdict::Wrong && s.progress().played == 1, "puzzle %d: again: no second score", i);
        const uint16_t after = s.progress().rating.rating;
        CHECK(finishLine(s) == Verdict::Solved && s.done(), "puzzle %d: then solved", i);
        CHECK(s.progress().rating.rating == after && s.progress().played == 1 && s.progress().solved == 0,
              "puzzle %d: solving it now changes nothing", i);
        CHECK(s.wrongMove().isNone(), "puzzle %d: the X goes with the next move", i);
      }
    }
    // ---- the hint: the first move's piece, and half a point
    {
      PuzzleSession s;
      openAt(s, levelProgress(2), i);
      Move line[puzzles::kMaxSolutionPlies];
      solution(i, line);
      const Move h = s.hint();
      CHECK(h == line[0] && (s.flags() & ui::kPuzzleHinted) && !s.scored(), "puzzle %d: the hint is the move", i);
      const PuzzleRating want = ui::updatedRating(s.progress().rating, s.puzzle().rating, PuzzleScore::Hinted);
      CHECK(finishLine(s) == Verdict::Solved, "puzzle %d: solved after the hint", i);
      CHECK(s.progress().rating.rating == want.rating && s.progress().solved == 1 && s.progress().streak == 1,
            "puzzle %d: half a point", i);
      CHECK(s.hint().isNone(), "puzzle %d: no hint once it is over", i);
      ++hinted;
    }
    // ---- the solution, move by move to the end
    {
      PuzzleSession s;
      openAt(s, levelProgress(2), i);
      Move line[puzzles::kMaxSolutionPlies];
      const int n = solution(i, line);
      const PuzzleRating want = ui::updatedRating(s.progress().rating, s.puzzle().rating, PuzzleScore::Missed);
      int steps = 0;
      bool shownRight = true;
      while (s.solutionStep()) {
        shownRight &= s.lastBySolution() && s.shownMove() == line[2 * steps];
        ++steps;
        s.playPending();
      }
      CHECK(steps == (n + 1) / 2 && shownRight, "puzzle %d: %d solution steps (%d plies)", i, steps, n);
      CHECK(s.done() && s.lastBySolution() && (s.flags() & ui::kPuzzleHelped) && s.progress().played == 1 &&
                s.progress().rating.rating == want.rating,
            "puzzle %d: shown to the end, scored once as a miss", i);
      ++shown;
    }
  }
  std::printf("     %d wrong tries, %d hints, %d solutions\n", wrongTried, hinted, shown);
  CHECK(wrongTried > 300 && hinted > 300 && shown > 300, "enough puzzles sampled");

  // A solution step after the solver found the first move: helped, and the score was
  // already decided by... nothing yet, so it is a miss now.
  for (int i = 0; i < puzzles::count(); ++i) {
    Move line[puzzles::kMaxSolutionPlies];
    if (solution(i, line) < 3) continue;
    PuzzleSession s;
    openAt(s, levelProgress(1), i);
    s.play(line[0]);
    s.playPending();
    CHECK(!s.scored() && s.solutionStep() && s.scored() && s.progress().solved == 0,
          "puzzle %d: the solution half-way still costs the puzzle", i);
    break;
  }
  section("session: wrong try, hint, solution", before);
}

void testRetry() {
  const long long before = g_checks;
  for (int i = 5; i < puzzles::count(); i += 97) {
    PuzzleSession s;
    openAt(s, levelProgress(1), i);
    finishLine(s);
    const PuzzleProgress solved = s.progress();
    s.retry();
    Position start;
    puzzles::startPosition(i, start);
    CHECK(s.board() == start && s.shownMove() == puzzles::blunderMove(i) && s.solversTurn() && !s.done(),
          "puzzle %d: Retry, back at the start", i);
    CHECK(s.progress().plies == 0 && s.flags() == (ui::kPuzzleScored | ui::kPuzzleRetried),
          "puzzle %d: the score stays, the attempt is new (flags %02x)", i, s.flags());
    const Move bad = wrongMoveFor(s);
    if (!bad.isNone()) s.play(bad);
    CHECK(finishLine(s) == Verdict::Solved, "puzzle %d: solved again", i);
    CHECK(s.progress().rating.rating == solved.rating.rating && s.progress().played == solved.played &&
              s.progress().streak == solved.streak,
          "puzzle %d: practice changes nothing", i);
  }
  section("session: retry", before);
}

void testPowerCut() {
  const long long before = g_checks;
  int cuts = 0;
  for (int i = 0; i < puzzles::count(); i += 13) {
    Move line[puzzles::kMaxSolutionPlies];
    const int n = solution(i, line);
    if (n < 3) continue;
    // Half-way, after a hint and a wrong try: everything about it must come back.
    PuzzleSession a;
    openAt(a, levelProgress(3, 99), i);
    a.hint();
    a.play(line[0]);
    a.playPending();
    const Move bad = wrongMoveFor(a);
    if (!bad.isNone()) a.play(bad);
    uint8_t blob[ui::kPuzzleProgressBytes];
    const size_t size = ui::encodePuzzleProgress(a.progress(), blob, sizeof blob);
    PuzzleProgress back;
    bool changed = false;
    CHECK(ui::decodePuzzleProgress(blob, size, back, changed) == ProgressCheck::Ok, "puzzle %d: the blob reads", i);
    PuzzleSession b;
    b.setProgress(back);
    CHECK(b.ensureLoaded() && b.index() == i && b.pending() == Pending::None, "puzzle %d: the same puzzle", i);
    CHECK(b.board() == a.board() && b.shownMove() == a.shownMove() && std::strcmp(b.shownSan(), a.shownSan()) == 0,
          "puzzle %d: the same position, the reply marked", i);
    CHECK(b.flags() == a.flags() && b.progress().plies == 2 && b.solversTurn(), "puzzle %d: the same attempt", i);
    const uint16_t rating = b.progress().rating.rating;
    const uint16_t played = b.progress().played;
    CHECK(finishLine(b) == Verdict::Solved, "puzzle %d: solved after the cut", i);
    if (bad.isNone()) {
      // No wrong move to try there (a single legal move): scored at the end, after the hint.
      CHECK(played == 0 && b.progress().played == 1 && b.progress().solved == 1, "puzzle %d: scored once, hinted", i);
    } else {
      CHECK(b.progress().played == played && played == 1 && b.progress().rating.rating == rating,
            "puzzle %d: the miss from before the cut is not counted twice", i);
    }
    ++cuts;
  }
  CHECK(cuts > 100, "%d power cuts", cuts);

  // A puzzle the pack no longer has (another pack, or a corrupt blob that kept its CRC): a new one.
  PuzzleProgress p = levelProgress(1);
  p.current = static_cast<uint16_t>(puzzles::count() + 5);
  p.plies = 4;
  PuzzleSession s;
  s.setProgress(p);
  CHECK(!s.resume() && !s.loaded() && s.progress().current == ui::kNoPuzzle, "an index past the pack: not resumed");
  CHECK(s.ensureLoaded() && s.loaded() && s.pending() == Pending::Setup && s.progress().played == 0,
        "... a new puzzle instead, and nothing scored");
  // setProgress forgets whether the board was on the glass: that is the screen's to say.
  p = levelProgress(1);
  p.current = 10;
  p.flags = ui::kPuzzleOnScreen | ui::kPuzzleHinted;
  s.setProgress(p);
  CHECK(s.flags() == ui::kPuzzleHinted, "the on-screen flag is not the session's");
  section("session: power cuts", before);
}

void testMatesAndPromotions() {
  const long long before = g_checks;
  int otherMates = 0;
  int promotions = 0;
  for (int i = 0; i < puzzles::count(); ++i) {
    Move line[puzzles::kMaxSolutionPlies];
    const int n = solution(i, line);
    PuzzleSession s;
    openAt(s, levelProgress(1), i);
    // Up to the last solver move.
    for (int k = 0; k + 1 < n; k += 2) {
      s.play(line[k]);
      s.playPending();
    }
    const int last = n - 1;
    MoveList legal;
    s.board().generateLegalMoves(legal);
    for (Move m : legal) {
      if (m == line[last]) continue;
      Position after = s.board();
      Undo undo;
      after.make(m, undo);
      PuzzleSession probe = s;
      const Verdict v = probe.play(m);
      if (after.isCheckmate() && s.puzzle().endsInMate) {
        ++otherMates;
        CHECK(v == Verdict::Solved && probe.done() && probe.board() == after && probe.progress().solved == 1,
              "puzzle %d: another mate is a solution too", i);
      } else if (m.from() == line[last].from() && m.to() == line[last].to() && m.isPromotion()) {
        ++promotions;
        CHECK(v == Verdict::Wrong, "puzzle %d: promoting to the wrong piece is wrong", i);
      }
    }
  }
  std::printf("     %d other mates accepted, %d wrong promotion pieces refused\n", otherMates, promotions);
  CHECK(otherMates > 0 && promotions > 0, "both happen in the pack");
  section("session: other mates, promotions", before);
}

void testPickLevel() {
  const long long before = g_checks;
  PuzzleSession s;
  s.setProgress(levelProgress(1));
  s.ensureLoaded();
  s.playPending();
  const int first = s.index();
  CHECK(!s.scored(), "an untouched puzzle");
  CHECK(s.pickLevel(3, 5), "a new level");
  CHECK(s.progress().rating.rating == ui::kPuzzleLevelRatings[3] && s.progress().played == 0,
        "the old puzzle went without a score");
  CHECK(s.loaded() && s.index() != first && s.pending() == Pending::Setup, "a new puzzle for the new level");
  puzzles::Puzzle p;
  puzzles::byIndex(s.index(), p);
  CHECK(p.rating / 100 == ui::kPuzzleLevelRatings[3] / 100, "at the new level (%u)", p.rating);
  section("session: a new level", before);
}

}  // namespace

int main() {
  std::printf("== puzzle mode: pack of %d puzzles\n", puzzles::count());
  testExpected();
  testUpdate();
  testScoreAndLevels();
  testConvergence();
  testBuckets();
  testBlob();
  testEveryPuzzle();
  testSetupAndSkip();
  testWrongHintSolution();
  testRetry();
  testPowerCut();
  testMatesAndPromotions();
  testPickLevel();
  std::printf("%lld checks, %lld failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
