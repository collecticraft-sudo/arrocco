// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the solver's puzzle progress: a rating that follows the results, the
// choice of the next puzzle, and the blob that keeps both through a power cut.
//
// THE RATING is Glicko-1 (Glickman 1995) with each puzzle as a rating period of one
// game, which is how Lichess rates its puzzles too. The opponent is the puzzle, rated
// by Lichess on the same scale; its own deviation (at most 80 in the pack) is left out.
//   E   = 1 / (1 + 10^((puzzle - rating) / 400))     the chance of solving it
//   RD' = 1 / sqrt(1/RD^2 + q^2 E (1 - E))           q = ln 10 / 400
//   R'  = R + q RD'^2 (score - E)
// The score is 1 for a puzzle solved, 1/2 when a hint was used, 0 for a wrong move,
// the solution, or a skip. RD (the deviation) is the system's doubt: high at the start,
// so the first results move the rating by 100 points or more and a level picked too
// high or too low is corrected within a few puzzles; it then shrinks with every puzzle
// down to kPuzzleDeviationMin, where a puzzle moves the rating by about 10 points and
// the rating keeps following a player who improves.
//
// THE CHOICE: the pack is sorted by rating, so a 100-point bucket of it is a run of
// consecutive indices (about 250 puzzles in the pack of today). The next puzzle comes
// from the bucket of the target rating, which is the solver's own rating while it is
// still uncertain (that is where a result says the most) and 100 below it afterwards,
// where about two puzzles in three get solved. Each bucket is walked in a fixed
// shuffled order of its own, started at a place drawn once per board, and the progress
// keeps how far each walk has gone: no puzzle comes back until its whole bucket has
// been seen, and no list of seen puzzles has to be kept.
//
// THE BLOB (Platform::loadBlob / storeBlob, key kPuzzleProgressKey), version 1,
// little-endian, 98 bytes:
//   offset size
//    0      4  magic "ARPZ"
//    4      1  version, 1
//    5      1  flags of the puzzle on the board (PuzzleFlag), no other bit set
//    6      2  rating, kPuzzleRatingFloor .. kPuzzleRatingCeiling
//    8      2  deviation, kPuzzleDeviationMin .. kPuzzleDeviationMax
//   10      2  puzzles scored since the level was picked
//   12      2  ... solved (a hint allowed), at most the number scored
//   14      2  ... solved in a row, at most the number solved
//   16      2  rating change of the last puzzle scored, signed
//   18      4  the board's seed for the order of the buckets, never 0
//   22      4  fingerprint of the pack the indices below belong to (packFingerprint)
//   26      2  index of the puzzle on the board, 0xFFFF = none
//   28      1  solution plies played on it, 0 .. kMaxSolutionPlies
//   29      1  number of buckets, kPuzzleBuckets
//   30     64  puzzles taken so far from each bucket, 2 bytes each
//   94      4  CRC-32 of every byte before it (ui::crc32, the one zlib computes)
// A blob that fails a check is not progress, and the board starts as if it had none. A
// blob whose fingerprint is not this firmware's pack (a regenerated pack) keeps the
// rating and the counts, which belong to the player, and drops the puzzle on the board
// and the walks, which belong to the old pack.
#pragma once
#include <cstddef>
#include <cstdint>

namespace arrocco::ui {

constexpr char kPuzzleProgressKey[] = "puzzles";     // Platform::loadBlob / storeBlob
constexpr uint8_t kPuzzleProgressVersion = 1;
constexpr int kPuzzleBuckets = 32;                    // 100-point buckets, ratings 0 .. 3199
constexpr int kPuzzleBucketWidth = 100;
constexpr size_t kPuzzleProgressBytes = 30 + 2 * kPuzzleBuckets + 4;   // 98
constexpr uint16_t kNoPuzzle = 0xFFFF;

// ---- the rating ---------------------------------------------------------------------------

constexpr uint16_t kPuzzleRatingFloor = 100;
constexpr uint16_t kPuzzleRatingCeiling = 3000;
constexpr uint16_t kPuzzleDeviationMin = 60;     // about 10 points a puzzle once settled
constexpr uint16_t kPuzzleDeviationMax = 350;    // Glicko's "nothing known yet"
constexpr uint16_t kPuzzleStartDeviation = 250;  // after picking a level: a guess, not nothing
// Below this deviation the rating is settled: the puzzles then come from kPuzzleComfortOffset
// below it, and the side column stops saying it is still finding the level.
constexpr uint16_t kPuzzleSettledDeviation = 150;
constexpr int kPuzzleComfortOffset = 100;

// The starting levels the picker offers, as ratings: beginner, casual, club, strong club.
constexpr int kPuzzleLevelCount = 4;
inline constexpr uint16_t kPuzzleLevelRatings[kPuzzleLevelCount] = {800, 1100, 1400, 1700};

struct PuzzleRating {
  uint16_t rating = kPuzzleLevelRatings[1];
  uint16_t deviation = kPuzzleDeviationMax;
};

// What one puzzle was worth to its solver.
enum class PuzzleScore : uint8_t {
  Missed,   // a wrong move, the solution, or a skip: 0
  Hinted,   // solved after a hint: 1/2
  Solved,   // solved alone: 1
};

// The chance (0 .. 1) that a solver rated `rating` solves a puzzle rated `puzzleRating`.
double expectedSolve(int rating, int puzzleRating);

// The rating after one puzzle (Glicko-1, see the top of this file), clamped and rounded.
PuzzleRating updatedRating(PuzzleRating player, int puzzleRating, PuzzleScore score);

// The rating the next puzzle should have.
int targetRating(PuzzleRating player);

// ---- the progress -----------------------------------------------------------------------------

// What is known about the puzzle on the board, beyond its index and how far it got. The
// first three describe the attempt in progress, and Retry clears them; Scored and Retried
// stay with the puzzle until the next one.
enum PuzzleFlag : uint8_t {
  kPuzzleHinted = 1u << 0,    // a hint was shown
  kPuzzleMissed = 1u << 1,    // a wrong move was tried
  kPuzzleHelped = 1u << 2,    // the solution played a move
  kPuzzleScored = 1u << 3,    // the rating has counted it (once per puzzle, whatever comes after)
  kPuzzleRetried = 1u << 4,   // started again after it was scored: practice, no rating
  kPuzzleOnScreen = 1u << 5,  // the puzzle screen was on the glass at the last refresh (wake from sleep)
  kPuzzleAttemptFlags = kPuzzleHinted | kPuzzleMissed | kPuzzleHelped,
  kPuzzleKnownFlags = 0x3F,
};

struct PuzzleProgress {
  bool started = false;        // a level was picked: there is something to keep
  PuzzleRating rating;
  uint16_t played = 0;         // puzzles scored since the level was picked
  uint16_t solved = 0;         // ... solved, a hint allowed
  uint16_t streak = 0;         // ... solved in a row
  int16_t lastChange = 0;      // what the last puzzle scored did to the rating
  uint32_t seed = 0;           // the board's own order of the buckets; 0 = not drawn yet
  uint16_t drawn[kPuzzleBuckets] = {};   // puzzles taken so far from each bucket
  uint16_t current = kNoPuzzle;          // the puzzle on the board
  uint8_t plies = 0;                     // solution plies played on it
  uint8_t flags = 0;                     // PuzzleFlag bits about it
};

// A fresh start at picker level `level` (0 .. kPuzzleLevelCount-1): the rating, its
// doubt and the counts start again, the walks through the pack go on where they were
// (a new start does not mean seeing the same puzzles again), the puzzle on the board is
// dropped without a score. `entropy` seeds the board's order the first time.
void startAtLevel(PuzzleProgress& progress, int level, uint32_t entropy);

// Counts one puzzle rated `puzzleRating`: rating, counts, streak, last change.
void scorePuzzle(PuzzleProgress& progress, int puzzleRating, PuzzleScore score);

// Index of the next puzzle for this progress, taken from its bucket's walk (which moves
// on by one), or -1 for an empty pack. Does not touch `current`: the caller loads it.
int drawPuzzle(PuzzleProgress& progress);

// The k-th puzzle (k counted from 0, wrapping) of bucket `bucket`'s walk for `seed`, or
// -1 for an empty bucket. drawPuzzle() is this plus the choice of the bucket.
int puzzleInWalk(uint32_t seed, int bucket, uint32_t k);

// Which bucket the next puzzle comes from: the target's, or the nearest one that has puzzles.
int bucketFor(int rating);

// FNV-1a over the pack's index: changes whenever the puzzles or their order do.
uint32_t packFingerprint();

// ---- the blob ---------------------------------------------------------------------------------

enum class ProgressCheck : uint8_t {
  Ok,
  TooShort,      // not the size of a version 1 blob, at least not its header
  NotProgress,   // no magic
  OtherVersion,
  BadCrc,
  BadLayout,     // the size and the bucket count disagree
  BadField,      // a value out of range
};

// Writes `progress` into `out`; returns kPuzzleProgressBytes, or 0 if `capacity` is too small.
size_t encodePuzzleProgress(const PuzzleProgress& progress, uint8_t* out, size_t capacity);

// Checks `data` and fills `progress` (started = true) on Ok; on anything else `progress`
// is left untouched. `packChanged` tells an Ok blob from another pack: its puzzle and
// walks were dropped.
ProgressCheck decodePuzzleProgress(const uint8_t* data, size_t size, PuzzleProgress& progress,
                                   bool& packChanged);

}  // namespace arrocco::ui
