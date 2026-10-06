// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — puzzle rating, choice and progress blob. See puzzle_progress.h.
#include "arrocco/ui/puzzle_progress.h"

#include <cmath>
#include <cstring>

#include "arrocco/puzzles/puzzle_data.h"
#include "arrocco/puzzles/puzzles.h"
#include "arrocco/ui/saved_game.h"   // crc32()

namespace arrocco::ui {

namespace {

constexpr uint8_t kMagic[4] = {'A', 'R', 'P', 'Z'};

// Byte offsets, as in the table of puzzle_progress.h.
constexpr size_t kOffVersion = 4;
constexpr size_t kOffFlags = 5;
constexpr size_t kOffRating = 6;
constexpr size_t kOffDeviation = 8;
constexpr size_t kOffPlayed = 10;
constexpr size_t kOffSolved = 12;
constexpr size_t kOffStreak = 14;
constexpr size_t kOffLastChange = 16;
constexpr size_t kOffSeed = 18;
constexpr size_t kOffPack = 22;
constexpr size_t kOffCurrent = 26;
constexpr size_t kOffPlies = 28;
constexpr size_t kOffBuckets = 29;
constexpr size_t kOffDrawn = 30;
static_assert(kOffDrawn + 2 * kPuzzleBuckets + 4 == kPuzzleProgressBytes, "the table and the size agree");

// ln(10) / 400: Glicko's q.
constexpr double kQ = 0.0057564627324851142;

// Steps for the walks: (j * step) mod n visits every j once when step and n are coprime,
// and a prime is coprime with every n it does not divide.
constexpr uint32_t kSteps[] = {97, 89, 83, 79, 73, 71, 67, 61, 59, 53, 47, 43, 41, 37, 31, 29, 23, 19, 17,
                               13, 11, 7, 5, 3, 2};
constexpr int kStepCount = static_cast<int>(sizeof kSteps / sizeof kSteps[0]);

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

// MurmurHash3's finaliser: every input bit reaches every output bit.
uint32_t mix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

// First index of bucket `bucket` and how many puzzles it holds.
int bucketStart(int bucket) {
  const long rating = static_cast<long>(bucket) * kPuzzleBucketWidth;
  return puzzles::lowerBoundByRating(rating > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(rating));
}

int bucketSize(int bucket) {
  if (bucket < 0 || bucket >= kPuzzleBuckets) return 0;
  const int end = bucket + 1 == kPuzzleBuckets ? puzzles::count() : bucketStart(bucket + 1);
  return end - bucketStart(bucket);
}

uint16_t clampU16(long value, uint16_t low, uint16_t high) {
  return static_cast<uint16_t>(value < low ? low : (value > high ? high : value));
}

}  // namespace

// ---- the rating ---------------------------------------------------------------------------------

double expectedSolve(int rating, int puzzleRating) {
  return 1.0 / (1.0 + std::pow(10.0, (puzzleRating - rating) / 400.0));
}

PuzzleRating updatedRating(PuzzleRating player, int puzzleRating, PuzzleScore score) {
  const double expected = expectedSolve(player.rating, puzzleRating);
  const double actual = score == PuzzleScore::Solved ? 1.0 : (score == PuzzleScore::Hinted ? 0.5 : 0.0);
  const double rd = player.deviation;
  // 1 / RD'^2 = 1 / RD^2 + 1 / d^2, with d^2 = 1 / (q^2 E (1 - E)) for one game.
  const double variance = 1.0 / (1.0 / (rd * rd) + kQ * kQ * expected * (1.0 - expected));
  const double rating = player.rating + kQ * variance * (actual - expected);
  PuzzleRating out;
  out.rating = clampU16(std::lround(rating), kPuzzleRatingFloor, kPuzzleRatingCeiling);
  out.deviation = clampU16(std::lround(std::sqrt(variance)), kPuzzleDeviationMin, kPuzzleDeviationMax);
  return out;
}

int targetRating(PuzzleRating player) {
  const int target = player.rating - (player.deviation < kPuzzleSettledDeviation ? kPuzzleComfortOffset : 0);
  return target < 0 ? 0 : target;
}

// ---- the progress -----------------------------------------------------------------------------------

void startAtLevel(PuzzleProgress& progress, int level, uint32_t entropy) {
  const int i = level < 0 ? 0 : (level >= kPuzzleLevelCount ? kPuzzleLevelCount - 1 : level);
  progress.started = true;
  progress.rating.rating = kPuzzleLevelRatings[i];
  progress.rating.deviation = kPuzzleStartDeviation;
  progress.played = 0;
  progress.solved = 0;
  progress.streak = 0;
  progress.lastChange = 0;
  if (progress.seed == 0) progress.seed = mix32(entropy ^ 0x41525A50u) | 1u;   // never 0: 0 = not drawn
  progress.current = kNoPuzzle;
  progress.plies = 0;
  progress.flags = 0;
}

void scorePuzzle(PuzzleProgress& progress, int puzzleRating, PuzzleScore score) {
  const PuzzleRating before = progress.rating;
  progress.rating = updatedRating(before, puzzleRating, score);
  progress.lastChange = static_cast<int16_t>(progress.rating.rating - before.rating);
  if (progress.played < 0xFFFF) ++progress.played;
  if (score == PuzzleScore::Missed) {
    progress.streak = 0;
  } else {
    if (progress.solved < progress.played) ++progress.solved;
    if (progress.streak < progress.solved) ++progress.streak;
  }
}

int bucketFor(int rating) {
  int home = rating / kPuzzleBucketWidth;
  if (home < 0) home = 0;
  if (home >= kPuzzleBuckets) home = kPuzzleBuckets - 1;
  // The nearest bucket that has puzzles; at equal distance the easier one.
  for (int distance = 0; distance < kPuzzleBuckets; ++distance) {
    if (bucketSize(home - distance) > 0) return home - distance;
    if (bucketSize(home + distance) > 0) return home + distance;
  }
  return -1;
}

int puzzleInWalk(uint32_t seed, int bucket, uint32_t k) {
  const int n = bucketSize(bucket);
  if (n <= 0) return -1;
  const uint32_t size = static_cast<uint32_t>(n);
  const uint32_t h = mix32(seed ^ (static_cast<uint32_t>(bucket) * 0x9E3779B9u));
  // The step, from one of the first eight primes on (per board and bucket), skipping any
  // that divides the bucket's size.
  uint32_t step = 1;
  for (int i = static_cast<int>(h >> 29); i < kStepCount; ++i) {
    if (size % kSteps[i] != 0) {
      step = kSteps[i];
      break;
    }
  }
  const uint32_t offset = h % size;
  const uint32_t j = k % size;
  return bucketStart(bucket) + static_cast<int>((j * step + offset) % size);
}

int drawPuzzle(PuzzleProgress& progress) {
  const int bucket = bucketFor(targetRating(progress.rating));
  if (bucket < 0) return -1;
  const int index = puzzleInWalk(progress.seed, bucket, progress.drawn[bucket]);
  ++progress.drawn[bucket];   // wraps after 65536: the walk simply starts over
  return index;
}

uint32_t packFingerprint() {
  // Computed once: 42 KB of FNV-1a is well under a millisecond, but it never changes.
  static uint32_t cached = 0;
  if (cached != 0) return cached;
  uint32_t h = 0x811C9DC5u;
  for (uint32_t i = 0; i < puzzles::data::kIndexBytes; ++i) {
    h ^= puzzles::data::kIndex[i];
    h *= 0x01000193u;
  }
  cached = h == 0 ? 1u : h;
  return cached;
}

// ---- the blob ---------------------------------------------------------------------------------------

size_t encodePuzzleProgress(const PuzzleProgress& progress, uint8_t* out, size_t capacity) {
  if (out == nullptr || capacity < kPuzzleProgressBytes) return 0;
  memcpy(out, kMagic, sizeof kMagic);
  out[kOffVersion] = kPuzzleProgressVersion;
  out[kOffFlags] = progress.flags;
  put16(out + kOffRating, progress.rating.rating);
  put16(out + kOffDeviation, progress.rating.deviation);
  put16(out + kOffPlayed, progress.played);
  put16(out + kOffSolved, progress.solved);
  put16(out + kOffStreak, progress.streak);
  put16(out + kOffLastChange, static_cast<uint16_t>(progress.lastChange));
  put32(out + kOffSeed, progress.seed);
  put32(out + kOffPack, packFingerprint());
  put16(out + kOffCurrent, progress.current);
  out[kOffPlies] = progress.plies;
  out[kOffBuckets] = static_cast<uint8_t>(kPuzzleBuckets);
  for (int b = 0; b < kPuzzleBuckets; ++b) put16(out + kOffDrawn + 2 * b, progress.drawn[b]);
  put32(out + kPuzzleProgressBytes - 4, crc32(out, kPuzzleProgressBytes - 4));
  return kPuzzleProgressBytes;
}

ProgressCheck decodePuzzleProgress(const uint8_t* data, size_t size, PuzzleProgress& progress,
                                   bool& packChanged) {
  packChanged = false;
  if (data == nullptr || size < kOffDrawn + 4) return ProgressCheck::TooShort;
  if (memcmp(data, kMagic, sizeof kMagic) != 0) return ProgressCheck::NotProgress;
  if (data[kOffVersion] != kPuzzleProgressVersion) return ProgressCheck::OtherVersion;
  // The CRC sits in the last four bytes whatever the header says: checked before any size.
  if (get32(data + size - 4) != crc32(data, size - 4)) return ProgressCheck::BadCrc;
  if (data[kOffBuckets] != kPuzzleBuckets || size != kPuzzleProgressBytes) return ProgressCheck::BadLayout;

  PuzzleProgress p;
  p.started = true;
  p.flags = data[kOffFlags];
  p.rating.rating = get16(data + kOffRating);
  p.rating.deviation = get16(data + kOffDeviation);
  p.played = get16(data + kOffPlayed);
  p.solved = get16(data + kOffSolved);
  p.streak = get16(data + kOffStreak);
  p.lastChange = static_cast<int16_t>(get16(data + kOffLastChange));
  p.seed = get32(data + kOffSeed);
  p.current = get16(data + kOffCurrent);
  p.plies = data[kOffPlies];
  if (p.rating.rating < kPuzzleRatingFloor || p.rating.rating > kPuzzleRatingCeiling ||
      p.rating.deviation < kPuzzleDeviationMin || p.rating.deviation > kPuzzleDeviationMax ||
      p.solved > p.played || p.streak > p.solved || p.seed == 0)
    return ProgressCheck::BadField;

  if (get32(data + kOffPack) != packFingerprint()) {
    // Another pack: what was said about its puzzles means nothing here.
    packChanged = true;
    p.current = kNoPuzzle;
    p.plies = 0;
    p.flags = 0;
  } else {
    if ((p.flags & ~kPuzzleKnownFlags) != 0 || p.plies > puzzles::kMaxSolutionPlies) return ProgressCheck::BadField;
    if (p.current == kNoPuzzle ? (p.plies != 0 || p.flags != 0) : p.current >= puzzles::count())
      return ProgressCheck::BadField;
    for (int b = 0; b < kPuzzleBuckets; ++b) p.drawn[b] = get16(data + kOffDrawn + 2 * b);
  }
  progress = p;
  return ProgressCheck::Ok;
}

}  // namespace arrocco::ui
