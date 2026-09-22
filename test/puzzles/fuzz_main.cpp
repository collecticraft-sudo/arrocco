// SPDX-License-Identifier: GPL-3.0-or-later
// Attacks the puzzle reader with corrupt data.
//
// pack_main.cpp proves the reader is right about the real pack.  This one proves it
// cannot be talked into reading outside it.  The fixture is four real puzzles put on
// pages of their own with unreadable guard pages either side (see
// fuzz/arrocco/puzzles/puzzle_data.h), and every byte of index and blob is given
// every possible value in turn while the reader is asked for everything it can
// produce.  The contract under test is narrow:
//
//   * no call ever reads outside kIndex/kBlob — a byte too far is a SIGSEGV on the
//     guard page, in a plain build, with no sanitizer needed,
//   * no call ever writes past the buffer the caller handed it — the walls of 0xAB
//     around every output buffer say so,
//   * a call either fails cleanly or returns something self-consistent.
//
// `make sanitize` runs the same thing under UndefinedBehaviorSanitizer as a second
// opinion; see the Makefile for AddressSanitizer, which is far slower and, with the
// guard pages already in place, adds little here.
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "arrocco/chess/position.h"
#include "arrocco/puzzles/puzzle_data.h"
#include "arrocco/puzzles/puzzles.h"

using arrocco::chess::Move;
using arrocco::chess::Position;
using arrocco::puzzles::Cursor;
using arrocco::puzzles::Filter;
using arrocco::puzzles::Puzzle;
using arrocco::puzzles::ThemeId;
namespace puzzles = arrocco::puzzles;

// The storage the shadowed header declares. Both are installed by fence() below.
namespace arrocco::puzzles::data {
const uint8_t* kIndex = nullptr;
const uint8_t* kBlob = nullptr;
}  // namespace arrocco::puzzles::data

namespace {

// ------------------------------------------------------------------ guard pages
//
// Each array is given three pages: an unreadable one, a readable one holding the
// data, and another unreadable one.  Reading a single byte outside the readable
// page is a segmentation fault, which is exactly what we want a reader bug to be.
//
// A small array cannot sit against both edges of its page at once, so the sweep is
// run twice: flush against the end, where one byte too far hits the upper guard,
// and flush against the start, where one byte too early hits the lower guard.
struct Fence {
  uint8_t* base = nullptr;   // first of the three pages
  uint8_t* data = nullptr;   // where the array currently starts
  size_t page = 0;
  uint32_t size = 0;

  bool create(uint32_t bytes) {
    size = bytes;
    page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    void* raw = ::mmap(nullptr, page * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (raw == MAP_FAILED) return false;
    base = static_cast<uint8_t*>(raw);
    // The outer pages are poison; only the middle one may ever be touched.
    if (::mprotect(base, page, PROT_NONE) != 0) return false;
    if (::mprotect(base + page * 2, page, PROT_NONE) != 0) return false;
    place(true);
    return true;
  }

  // atEnd: the array's last byte is the page's last byte (catches over-reads).
  // otherwise its first byte is the page's first byte (catches under-reads).
  void place(bool atEnd) {
    data = atEnd ? base + page * 2 - size : base + page;
  }
};

Fence gIndexFence;
Fence gBlobFence;

// Negative control.  If mprotect quietly did nothing, every "the reader stayed
// inside the pack" result below would be vacuous, so prove the guard really kills a
// process: read one byte of it in a child and require the child to die by signal.
bool guardPageKills(const Fence& fence, bool upper) {
  const pid_t child = ::fork();
  if (child < 0) return false;
  if (child == 0) {
    // Let the fault kill the child outright.  A sanitizer build installs its own
    // SIGSEGV/SIGBUS handler, which would turn this into a page of diagnostics
    // about a fault we caused on purpose.
    ::signal(SIGSEGV, SIG_DFL);
    ::signal(SIGBUS, SIG_DFL);
    const volatile uint8_t* poison = upper ? fence.base + fence.page * 2 : fence.base;
    const uint8_t taken = *poison;  // must not survive this
    ::_exit(taken == 0 ? 40 : 41);
  }
  int status = 0;
  if (::waitpid(child, &status, 0) != child) return false;
  return WIFSIGNALED(status);
}

long long gChecks = 0;
long long gFailures = 0;
const char* gContext = "";

void check(bool condition, const char* what) {
  ++gChecks;
  if (condition) return;
  ++gFailures;
  if (gFailures <= 25) std::printf("FAIL [%s] %s\n", gContext, what);
  if (gFailures == 26) std::printf("... further failures suppressed\n");
}

// ------------------------------------------------------------------ the fixture
//
// Four real puzzles lifted out of the pack, re-encoded in format 1 and sorted by
// rating, so the selection calls have something sane to chew on:
//   #0 Haik8  600 backRankMate  mate, 5 plies
//   #1 qoHd9  601 mateIn1       mate, 1 ply   (the shortest line the pack allows)
//   #2 l9wPm  625 mateIn2       mate, 3 plies
//   #3 JjjlV  633 promotion     no mate, 3 plies, ends h2h1q
const uint8_t kGoldenIndex[arrocco::puzzles::data::kIndexBytes] = {
    0x48, 0x61, 0x69, 0x6B, 0x38, 0x41, 0x58, 0x02, 0x06, 0x00, 0x00, 0x00,
    0x71, 0x6F, 0x48, 0x64, 0x39, 0x42, 0x59, 0x02, 0x02, 0x1F, 0x00, 0x00,
    0x6C, 0x39, 0x77, 0x50, 0x6D, 0x43, 0x71, 0x02, 0x04, 0x39, 0x00, 0x00,
    0x4A, 0x6A, 0x6A, 0x6C, 0x56, 0x11, 0x79, 0x02, 0x04, 0x54, 0x00, 0x00,
};
const uint8_t kGoldenBlob[arrocco::puzzles::data::kBlobBytes] = {
    0x34, 0x68, 0x82, 0x81, 0x13, 0x82, 0xF0, 0x40, 0x33, 0x95, 0x08, 0x00,
    0x10, 0x16, 0x60, 0x96, 0x66, 0xB8, 0x01, 0x0D, 0x01, 0x82, 0x0E, 0xCB,
    0x0E, 0xFA, 0x0E, 0x34, 0x0F, 0x3B, 0x0F, 0xA4, 0xC7, 0x36, 0x40, 0x12,
    0x24, 0xE7, 0x4A, 0x25, 0x03, 0x02, 0x00, 0x00, 0x10, 0x48, 0x78, 0x67,
    0x66, 0x66, 0x96, 0xB9, 0x00, 0x15, 0x09, 0xFB, 0x00, 0x90, 0x76, 0x88,
    0x03, 0x08, 0x09, 0x25, 0x42, 0x53, 0x00, 0xA4, 0x00, 0x00, 0x06, 0x72,
    0x68, 0xB6, 0x09, 0x01, 0xBE, 0x03, 0x0C, 0x0F, 0xAB, 0x0E, 0xBC, 0x0E,
    0x00, 0x20, 0xA0, 0x40, 0x40, 0x00, 0x20, 0x00, 0x65, 0xB6, 0x36, 0x00,
    0x75, 0x05, 0xD7, 0x03, 0xD5, 0x04, 0xCF, 0x41,
};

// Puts the golden bytes back and re-points the reader at them.  Called before every
// single corruption, so each one is tested on its own.
void restore() {
  std::memcpy(gIndexFence.data, kGoldenIndex, sizeof(kGoldenIndex));
  std::memcpy(gBlobFence.data, kGoldenBlob, sizeof(kGoldenBlob));
  arrocco::puzzles::data::kIndex = gIndexFence.data;
  arrocco::puzzles::data::kBlob = gBlobFence.data;
}

uint8_t* indexBytes() { return gIndexFence.data; }
uint8_t* blobBytes() { return gBlobFence.data; }

// ------------------------------------------------------------------ guarded buffers
//
// Every output buffer sits between two walls of 0xAB.  If a reader ever writes one
// byte too many, the wall says so even in a plain build.
constexpr int kWall = 32;
constexpr uint8_t kWallByte = 0xAB;

struct FenBuffer {
  uint8_t before[kWall];
  char text[arrocco::chess::kFenBufferSize];
  uint8_t after[kWall];

  void arm() {
    std::memset(before, kWallByte, sizeof(before));
    std::memset(after, kWallByte, sizeof(after));
    std::memset(text, 0, sizeof(text));
  }
  bool intact() const {
    for (int i = 0; i < kWall; ++i)
      if (before[i] != kWallByte || after[i] != kWallByte) return false;
    return true;
  }
};

struct MoveBuffer {
  uint8_t before[kWall];
  Move moves[puzzles::kMaxSolutionPlies];
  uint8_t after[kWall];

  void arm() {
    std::memset(before, kWallByte, sizeof(before));
    std::memset(after, kWallByte, sizeof(after));
    for (int i = 0; i < puzzles::kMaxSolutionPlies; ++i) moves[i] = Move::none();
  }
  bool intact() const {
    for (int i = 0; i < kWall; ++i)
      if (before[i] != kWallByte || after[i] != kWallByte) return false;
    return true;
  }
};

FenBuffer gFen;
MoveBuffer gMoves;

// How many of the 256 values each byte is given.  1 means all of them, which is
// what a plain build does in about a second.  Under the sanitizers the same sweep
// costs a few hundred times more, so `make sanitize` thins it out with
// FUZZ_STRIDE: every byte position is still visited, with a coprime stride so the
// values it gets differ from byte to byte and the whole space is still covered
// across the sweep.  Setting it never skips a byte, only some of its values.
int gStride = 1;

int strideFromEnvironment() {
  const char* text = std::getenv("FUZZ_STRIDE");
  if (text == nullptr) return 1;
  int value = 0;
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') return 1;
    value = value * 10 + (*c - '0');
    if (value > 255) return 255;
  }
  return value < 1 ? 1 : value;
}

// The values at the edges are the ones that break decoders, so they are always
// tried; `salt` shifts which of the rest a thinned sweep picks.
bool useValue(int value, int salt) {
  if (gStride == 1) return true;
  if (value <= 4 || value >= 251) return true;
  return ((value + salt) % gStride) == 0;
}

// Asks the reader for everything it can produce about `index`, whatever the data
// currently says.  Returns nothing: the point is that it must not crash, must not
// overrun a caller buffer, and must stay self-consistent.
void probe(int index) {
  Puzzle puzzle;
  const bool described = puzzles::byIndex(index, puzzle);
  if (described) {
    check(puzzle.id[5] == '\0', "byIndex always NUL-terminates the id");
    check(puzzle.solutionPlies <= puzzles::kMaxSolutionPlies,
          "byIndex never reports more plies than the reader can hold");
  }

  Position setup;
  const bool haveSetup = puzzles::setupPosition(index, setup);
  const Move blunder = puzzles::blunderMove(index);
  check(!haveSetup || described, "a decodable position implies a decodable record");
  check(blunder.isNone() || haveSetup, "a blunder implies a setup position");
  if (haveSetup && !blunder.isNone()) check(setup.isLegal(blunder), "the blunder is legal in the setup");

  Position start;
  const bool haveStart = puzzles::startPosition(index, start);
  check(!haveStart || haveSetup, "a start position implies a setup position");

  gFen.arm();
  const int fenLength = puzzles::startFen(index, gFen.text, arrocco::chess::kFenBufferSize);
  check(gFen.intact(), "startFen stays inside the buffer it was given");
  check(fenLength == 0 || haveStart, "a FEN implies a start position");
  if (fenLength > 0) {
    check(fenLength < arrocco::chess::kFenBufferSize, "the FEN length fits the buffer");
    check(gFen.text[fenLength] == '\0', "the FEN is NUL-terminated where it says");
    Position again;
    check(again.setFen(gFen.text), "the FEN the reader emits parses back");
  }

  // kFenBufferSize is only the size that always works; a buffer too small for the
  // FEN that actually came out has to be refused, and refused untouched.
  for (int size = 0; size <= 8; ++size) {
    gFen.arm();
    check(puzzles::startFen(index, gFen.text, size) == 0, "startFen refuses a buffer too small");
    check(gFen.intact(), "a refused startFen stays inside the buffer");
  }
  check(puzzles::startFen(index, nullptr, arrocco::chess::kFenBufferSize) == 0,
        "startFen refuses a null buffer");

  gMoves.arm();
  const int moveCount = puzzles::solutionMoves(index, gMoves.moves, puzzles::kMaxSolutionPlies);
  check(gMoves.intact(), "solutionMoves stays inside the buffer it was given");
  check(moveCount >= 0 && moveCount <= puzzles::kMaxSolutionPlies, "the ply count is in range");
  check(moveCount == 0 || haveStart, "a solution implies a start position");
  check(puzzles::solutionMoves(index, nullptr, puzzles::kMaxSolutionPlies) == 0,
        "solutionMoves refuses a null buffer");
  if (moveCount > 0) {
    Position walk = start;
    for (int k = 0; k < moveCount; ++k) {
      check(walk.isLegal(gMoves.moves[k]), "every decoded solution move is legal where it lands");
      arrocco::chess::Undo undo;
      walk.make(gMoves.moves[k], undo);
    }
  }

  // The cursor has to survive the same data and finish, or refuse to start.
  Cursor cursor;
  if (cursor.begin(index)) {
    check(cursor.valid() && cursor.index() == index, "a started cursor knows its index");
    check(cursor.totalPlies() >= 0 && cursor.totalPlies() <= puzzles::kMaxSolutionPlies,
          "the cursor line length is in range");
    check(cursor.solver() == cursor.position().sideToMove(), "the solver is the side to move");
    int guard = 0;
    while (!cursor.solved() && guard++ <= puzzles::kMaxSolutionPlies) {
      const Move want = cursor.expected();
      check(!want.isNone(), "an unsolved cursor has a move to expect");
      check(!cursor.play(Move::none()), "the cursor refuses a null move");
      check(cursor.play(want), "the cursor accepts the move it expects");
    }
    check(cursor.solved(), "the cursor reaches the end of its own line");
    check(cursor.expected().isNone(), "a solved cursor expects nothing");
    cursor.restart();
    check(cursor.plyIndex() == 0, "restart() rewinds a fuzzed cursor");
  } else {
    check(!cursor.valid(), "a cursor that refused to start is invalid");
  }

  // Selection must never walk off either end.
  Filter filter;
  check(puzzles::next(filter, puzzles::count()) == -1, "next() past the end finds nothing");
  check(puzzles::nth(filter, puzzles::count()) == -1, "nth() past the end finds nothing");
  check(puzzles::matches(filter, index) == (index >= 0 && index < puzzles::count()),
        "matches() agrees with the index range");
}

// Every corruption the reader might meet, run against whichever page edge the
// fences are currently flush with.
void corruptionSweep() {
  // ---------------------------------------------------------------- truncation
  //
  // The blob offset is the field that decides how far the reader walks.  Point it
  // at every byte of the blob and past its end: a record that does not fit has to
  // be refused, never half-read.
  gContext = "truncated blob";
  for (int offset = 0; offset <= 0xFFFFFF; offset += (offset < 512 ? 1 : 4093)) {
    restore();
    indexBytes()[9] = static_cast<uint8_t>(offset & 0xFF);
    indexBytes()[10] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    indexBytes()[11] = static_cast<uint8_t>((offset >> 16) & 0xFF);
    probe(0);
  }
  // The same for the stored move count, which decides how many 2-byte words follow.
  gContext = "bad move count";
  for (int moveCount = 0; moveCount < 256; ++moveCount) {
    restore();
    indexBytes()[8] = static_cast<uint8_t>(moveCount);
    probe(0);
  }
  // And for the occupancy bitmap, which decides how many nibbles follow it.
  gContext = "bad occupancy";
  for (int byte = 0; byte < 8; ++byte) {
    for (int value = 0; value < 256; ++value) {
      if (!useValue(value, byte)) continue;
      restore();
      blobBytes()[byte] = static_cast<uint8_t>(value);
      probe(0);
    }
  }

  // ---------------------------------------------------------------- full sweep
  //
  // Every byte of both arrays, every value, one at a time, with the whole reader
  // interface exercised on the record it belongs to.
  gContext = "byte sweep";
  for (uint32_t at = 0; at < arrocco::puzzles::data::kIndexBytes; ++at) {
    for (int value = 0; value < 256; ++value) {
      if (!useValue(value, static_cast<int>(at))) continue;
      restore();
      indexBytes()[at] = static_cast<uint8_t>(value);
      probe(static_cast<int>(at) / arrocco::puzzles::data::kIndexStride);
    }
  }
  for (uint32_t at = 0; at < arrocco::puzzles::data::kBlobBytes; ++at) {
    for (int value = 0; value < 256; ++value) {
      if (!useValue(value, static_cast<int>(at))) continue;
      restore();
      blobBytes()[at] = static_cast<uint8_t>(value);
      for (int i = 0; i < 4; ++i) probe(i);
    }
  }

}

}  // namespace

int main() {
  gStride = strideFromEnvironment();
  if (!gIndexFence.create(arrocco::puzzles::data::kIndexBytes) ||
      !gBlobFence.create(arrocco::puzzles::data::kBlobBytes)) {
    std::printf("could not fence the fixture pages\n");
    return 1;
  }
  restore();

  // ---------------------------------------------------------------- the fences
  gContext = "guard pages";
  check(guardPageKills(gIndexFence, false), "the index lower guard page is unreadable");
  check(guardPageKills(gIndexFence, true), "the index upper guard page is unreadable");
  check(guardPageKills(gBlobFence, false), "the blob lower guard page is unreadable");
  check(guardPageKills(gBlobFence, true), "the blob upper guard page is unreadable");

  // ---------------------------------------------------------------- the fixture
  gContext = "fixture";
  check(puzzles::count() == 4, "the fuzz fixture holds four puzzles");
  check(puzzles::formatVersion() == 1, "the fixture is format 1");
  check(puzzles::packBytes() == 48u + 104u, "the fixture is 152 bytes");

  struct Expected {
    const char* id;
    uint16_t rating;
    ThemeId theme;
    int plies;
    bool mate;
  };
  const Expected kExpected[4] = {
      {"Haik8", 600, ThemeId::BackRankMate, 5, true},
      {"qoHd9", 601, ThemeId::MateIn1, 1, true},
      {"l9wPm", 625, ThemeId::MateIn2, 3, true},
      {"JjjlV", 633, ThemeId::Promotion, 3, false},
  };
  for (int i = 0; i < 4; ++i) {
    Puzzle puzzle;
    check(puzzles::byIndex(i, puzzle), "the fixture record decodes");
    check(std::strcmp(puzzle.id, kExpected[i].id) == 0, "the fixture id is right");
    check(puzzle.rating == kExpected[i].rating, "the fixture rating is right");
    check(puzzle.theme == kExpected[i].theme, "the fixture theme is right");
    check(puzzle.solutionPlies == kExpected[i].plies, "the fixture ply count is right");
    check(puzzle.endsInMate == kExpected[i].mate, "the fixture mate flag is right");
    probe(i);
  }

  // A one-ply line is the degenerate case the Cursor has to get right.
  gContext = "single move";
  {
    Cursor cursor;
    check(cursor.begin(1), "the one-ply puzzle opens");
    check(cursor.totalPlies() == 1, "it really is one ply");
    check(cursor.solversTurn(), "it is the solver's turn at the start");
    const Move only = cursor.expected();
    check(!only.isNone(), "the single move exists");
    check(!cursor.solved(), "it is not solved before the move");
    check(cursor.play(only), "the single move is accepted");
    check(cursor.solved(), "one move solves it");
    check(cursor.lastReply().isNone(), "there is no reply after the last move");
    check(!cursor.play(only), "a solved cursor refuses to move again");
    check(cursor.expected().isNone(), "a solved cursor expects nothing");
  }

  // The promotion has to come back as a promotion: the pack stores no move-kind
  // bits, the reader has to rediscover them through parseUci.
  gContext = "move kinds";
  {
    Move line[puzzles::kMaxSolutionPlies];
    const int n = puzzles::solutionMoves(3, line, puzzles::kMaxSolutionPlies);
    check(n == 3, "the promotion puzzle has three plies");
    if (n == 3) {
      check(line[2].isPromotion(), "the last ply decodes as a promotion");
      check(line[2].promotion() == arrocco::chess::PieceType::Queen, "it promotes to a queen");
      char uci[arrocco::chess::kUciBufferSize];
      line[2].toUci(uci);
      check(std::strcmp(uci, "h2h1q") == 0, "the promotion round-trips to its UCI text");
    }
  }

  // ---------------------------------------------------------------- bad indices
  gContext = "bad index";
  {
    const int kBad[8] = {-1, -2, -1000, 4, 5, 1000, 0x7FFFFFFF, -0x7FFFFFFF - 1};
    for (int i = 0; i < 8; ++i) {
      Puzzle puzzle;
      Position position;
      check(!puzzles::byIndex(kBad[i], puzzle), "byIndex refuses an index out of range");
      check(!puzzles::setupPosition(kBad[i], position), "setupPosition refuses it");
      check(!puzzles::startPosition(kBad[i], position), "startPosition refuses it");
      check(puzzles::blunderMove(kBad[i]).isNone(), "blunderMove refuses it");
      gFen.arm();
      check(puzzles::startFen(kBad[i], gFen.text, arrocco::chess::kFenBufferSize) == 0,
            "startFen refuses it");
      check(gFen.intact(), "a refused startFen writes nothing");
      gMoves.arm();
      check(puzzles::solutionMoves(kBad[i], gMoves.moves, puzzles::kMaxSolutionPlies) == 0,
            "solutionMoves refuses it");
      check(gMoves.intact(), "a refused solutionMoves writes nothing");
      Cursor cursor;
      check(!cursor.begin(kBad[i]), "the cursor refuses it");
      check(!cursor.valid(), "and stays invalid");
      Filter filter;
      check(!puzzles::matches(filter, kBad[i]), "matches() refuses it");
      check(puzzles::next(filter, kBad[i]) == (kBad[i] < 0 ? 0 : -1),
            "next() clamps a negative start and gives up past the end");
    }
  }

  // ---------------------------------------------------------------- empty filters
  gContext = "empty filter";
  {
    Filter none;
    none.minRating = 3000;
    none.maxRating = 3100;
    check(puzzles::next(none, 0) == -1, "a band with nothing in it has no first match");
    check(puzzles::countMatching(none) == 0, "and counts zero");
    check(puzzles::nth(none, 0) == -1, "and has no n-th match");
    check(puzzles::nth(none, -1) == -1, "a negative n is refused");

    Filter inverted;  // min above max: the empty set, not an infinite walk
    inverted.minRating = 2000;
    inverted.maxRating = 100;
    check(puzzles::next(inverted, 0) == -1, "an inverted band matches nothing");
    check(puzzles::countMatching(inverted) == 0, "an inverted band counts zero");

    Filter absent;
    absent.theme = ThemeId::Zugzwang;  // not in the fixture
    check(puzzles::next(absent, 0) == -1, "a theme nobody carries matches nothing");
    check(puzzles::countMatching(absent) == 0, "and counts zero");

    Filter bogus;
    bogus.theme = static_cast<ThemeId>(63);  // a value the enum never takes
    check(puzzles::countMatching(bogus) == 0, "a theme outside the enum matches nothing");

    Filter mates;
    mates.matesOnly = true;
    check(puzzles::countMatching(mates) == 3, "three of the four fixture puzzles mate");

    check(puzzles::lowerBoundByRating(0) == 0, "lowerBoundByRating(0) is the first");
    check(puzzles::lowerBoundByRating(0xFFFF) == puzzles::count(), "an absurd rating lands past the end");
    check(puzzles::lowerBoundByRating(601) == 1, "the lower bound finds an exact rating");
    check(puzzles::lowerBoundByRating(602) == 2, "and steps over a rating nobody has");
  }

  // ---------------------------------------------------------------- corruption
  //
  // Once with the arrays flush against the end of their page, so a single byte
  // read past the end lands on the upper guard, and once flush against the start,
  // so a single byte read before the beginning lands on the lower guard.
  for (int pass = 0; pass < 2; ++pass) {
    gIndexFence.place(pass == 0);
    gBlobFence.place(pass == 0);
    restore();
    corruptionSweep();
  }

  restore();
  if (gStride != 1) std::printf("(thinned sweep: FUZZ_STRIDE=%d)\n", gStride);
  std::printf("%lld checks, %lld failures\n", gChecks, gFailures);
  return gFailures != 0 ? 1 : 0;
}
