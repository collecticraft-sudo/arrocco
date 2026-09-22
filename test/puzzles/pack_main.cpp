// SPDX-License-Identifier: GPL-3.0-or-later
// Validates the whole offline puzzle pack against our own rules library.
//
// Every single puzzle is checked: the packed position decodes to exactly what the
// Lichess row said, every stored move is legal where it is played, the colours
// alternate, mate puzzles really mate, no puzzle is over before it starts, and the
// Cursor walks each solution to the end.  Nothing here trusts the generator: the
// reference rows in puzzle_reference.h are the raw CSV fields.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "arrocco/chess/position.h"
#include "arrocco/puzzles/puzzle_data.h"
#include "arrocco/puzzles/puzzles.h"
#include "puzzle_reference.h"

using arrocco::chess::Color;
using arrocco::chess::Move;
using arrocco::chess::Position;
using arrocco::chess::Undo;
using arrocco::puzzles::Cursor;
using arrocco::puzzles::Filter;
using arrocco::puzzles::Puzzle;
using arrocco::puzzles::ThemeId;
namespace puzzles = arrocco::puzzles;
namespace reference = arrocco::puzzles::test_reference;

namespace {

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

// Splits the reference "e2e4 e7e5 ..." field. Returns the token count.
int splitUci(const char* text, char tokens[][8], int maxTokens) {
  int n = 0;
  int at = 0;
  while (*text != '\0' && n < maxTokens) {
    if (*text == ' ') {
      if (at != 0) {
        tokens[n][at] = '\0';
        ++n;
        at = 0;
      }
    } else if (at < 7) {
      tokens[n][at++] = *text;
    }
    ++text;
  }
  if (at != 0 && n < maxTokens) {
    tokens[n][at] = '\0';
    ++n;
  }
  return n;
}

int compareU64(const void* a, const void* b) {
  const unsigned long long x = *static_cast<const unsigned long long*>(a);
  const unsigned long long y = *static_cast<const unsigned long long*>(b);
  return x < y ? -1 : (x > y ? 1 : 0);
}

unsigned long long gHashes[reference::kRowCount];
unsigned long long gIds[reference::kRowCount];
int gBandCount[16];
int gThemeCount[64];

}  // namespace

int main() {
  std::printf("== pack: %d puzzles, format %u, %u bytes (index %u + blob %u)\n", puzzles::count(),
              static_cast<unsigned>(puzzles::formatVersion()),
              static_cast<unsigned>(puzzles::packBytes()),
              static_cast<unsigned>(arrocco::puzzles::data::kIndexBytes),
              static_cast<unsigned>(arrocco::puzzles::data::kBlobBytes));

  // ---------------------------------------------------------------- the pack as a whole
  gContext = "pack";
  char fenScratch[arrocco::chess::kFenBufferSize];
  check(puzzles::formatVersion() == 1, "format version is 1");
  check(puzzles::count() == reference::kRowCount, "count matches the reference rows");
  check(puzzles::count() == 3500, "count is 3500");
  check(arrocco::puzzles::data::kIndexBytes == 42000u, "index array is 42000 bytes");
  check(arrocco::puzzles::data::kBlobBytes == 96741u, "blob array is 96741 bytes");
  check(puzzles::packBytes() == 138741u, "pack is 138741 bytes");
  check(puzzles::packBytes() < 300u * 1024u, "pack is under the 300 KB budget");
  check(sizeof(arrocco::puzzles::data::kIndex) == arrocco::puzzles::data::kIndexBytes,
        "kIndex really is kIndexBytes long");
  check(sizeof(arrocco::puzzles::data::kBlob) == arrocco::puzzles::data::kBlobBytes,
        "kBlob really is kBlobBytes long");
  Puzzle scratch;
  Position scratchPosition;
  check(!puzzles::byIndex(-1, scratch), "byIndex rejects -1");
  check(!puzzles::byIndex(puzzles::count(), scratch), "byIndex rejects count()");
  check(!puzzles::startPosition(-1, scratchPosition), "startPosition rejects -1");
  check(!puzzles::setupPosition(puzzles::count(), scratchPosition), "setupPosition rejects count()");
  check(puzzles::blunderMove(-1).isNone(), "blunderMove rejects -1");
  check(puzzles::startFen(-1, fenScratch, sizeof(fenScratch)) == 0, "startFen rejects -1");
  Cursor deadCursor;
  check(!deadCursor.begin(-1), "the cursor rejects -1");
  check(!deadCursor.valid(), "an unopened cursor is invalid");

  int forcedFirstMove = 0;
  int withEnPassant = 0;
  int withCastlingRights = 0;
  int withPromotion = 0;
  // The pack stores no move-kind bits: a move is 12 bits of from/to plus the
  // promotion piece, and the reader has to rediscover castling and en passant by
  // asking the position.  These count the moves that came back with the kind set,
  // so the corpus is asserted below to actually exercise all three.
  int castlingMoves = 0;
  int enPassantMoves = 0;
  uint16_t previousRating = 0;

  char idBuffer[32];

  for (int i = 0; i < puzzles::count(); ++i) {
    const reference::Row& row = reference::kRows[i];
    std::snprintf(idBuffer, sizeof(idBuffer), "#%d %s", i, row.id);
    gContext = idBuffer;

    // ------------------------------------------------------------ index record
    Puzzle puzzle;
    check(puzzles::byIndex(i, puzzle), "byIndex succeeds");
    check(std::strcmp(puzzle.id, row.id) == 0, "id matches the reference");
    check(std::strlen(puzzle.id) == 5, "id is 5 characters");
    bool idSane = true;
    for (int c = 0; c < 5; ++c) {
      const char ch = puzzle.id[c];
      if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
        idSane = false;
      }
    }
    check(idSane, "id is alphanumeric");
    check(puzzle.rating == row.rating, "rating matches the reference");
    check(puzzle.rating >= 600 && puzzle.rating < 2000, "rating is inside 600..1999");
    check(puzzle.rating >= previousRating, "the pack is sorted by rating");
    previousRating = puzzle.rating;
    check(static_cast<uint8_t>(puzzle.theme) == row.theme, "theme matches the reference");
    check(puzzle.theme < ThemeId::Count, "theme is a real enumerator");
    check(puzzles::themeName(puzzle.theme)[0] != '\0', "theme has a label");
    check(puzzles::themeKey(puzzle.theme)[0] != '\0', "theme has a Lichess key");
    check(puzzle.endsInMate == (row.mate != 0), "mate flag matches the reference");

    gBandCount[(puzzle.rating - 600) / 100] += 1;
    gThemeCount[static_cast<int>(puzzle.theme)] += 1;
    gIds[i] = (static_cast<unsigned long long>(static_cast<unsigned char>(row.id[0])) << 32) |
              (static_cast<unsigned long long>(static_cast<unsigned char>(row.id[1])) << 24) |
              (static_cast<unsigned long long>(static_cast<unsigned char>(row.id[2])) << 16) |
              (static_cast<unsigned long long>(static_cast<unsigned char>(row.id[3])) << 8) |
              static_cast<unsigned long long>(static_cast<unsigned char>(row.id[4]));

    // ------------------------------------------------------------ setup position
    Position expected;
    check(expected.setFen(row.fen), "the reference FEN parses");
    Position packed;
    check(puzzles::setupPosition(i, packed), "setupPosition decodes");
    check(packed.samePositionAs(expected),
          "the packed position is the reference position (placement, side, castling, e.p.)");
    check(packed.sideToMove() == expected.sideToMove(), "side to move survives packing");
    check(packed.castlingRights() == expected.castlingRights(), "castling rights survive packing");
    check(packed.enPassantSquare() == expected.enPassantSquare(), "e.p. square survives packing");
    if (packed.enPassantSquare() != arrocco::chess::kNoSquare) ++withEnPassant;
    if (packed.castlingRights() != 0) ++withCastlingRights;

    // ------------------------------------------------------------ the move list
    char tokens[16][8];
    const int tokenCount = splitUci(row.moves, tokens, 16);
    check(tokenCount >= 2 && tokenCount <= 12, "2..12 plies are stored");
    check(puzzle.solutionPlies == tokenCount - 1, "solutionPlies is the move list minus the blunder");
    check(puzzle.solutionPlies <= puzzles::kMaxSolutionPlies, "solutionPlies fits kMaxSolutionPlies");

    char uci[arrocco::chess::kUciBufferSize];
    const Move blunder = puzzles::blunderMove(i);
    check(!blunder.isNone(), "the blunder decodes");
    check(expected.isLegal(blunder), "the blunder is legal in the setup position");
    blunder.toUci(uci);
    check(std::strcmp(uci, tokens[0]) == 0, "the blunder is the first reference move");
    if (blunder.isCastling()) ++castlingMoves;
    if (blunder.isEnPassant()) ++enPassantMoves;

    // ------------------------------------------------------------ start position
    Position start;
    check(puzzles::startPosition(i, start), "startPosition decodes");
    Position manual = expected;
    Undo undo;
    manual.make(blunder, undo);
    check(start.samePositionAs(manual), "startPosition == setup + blunder");
    check(!start.isCheckmate(), "the puzzle is not already mate");
    check(!start.isStalemate(), "the puzzle is not already stalemate");
    check(start.hasLegalMoves(), "the solver has a move");
    check(!start.isInsufficientMaterial(), "there is material to play with");
    check(!start.isFiftyMoveDraw(), "the puzzle is not a 50-move draw");
    // The reader hands every position back with a zero halfmove clock, so the check
    // above can only ever pass.  The reference FEN still carries the real clock:
    // read it there and make sure dropping it was actually safe, i.e. that the
    // whole line finishes before the 50-move rule could have bitten.
    {
      const char* field = row.fen;
      int spaces = 0;
      while (*field != '\0' && spaces < 4) {
        if (*field == ' ') ++spaces;
        ++field;
      }
      check(spaces == 4, "the reference FEN has a halfmove clock");
      int clock = 0;
      for (; *field >= '0' && *field <= '9'; ++field) clock = clock * 10 + (*field - '0');
      check(clock + tokenCount < 100, "the line ends before the 50-move rule, so the clock is droppable");
    }

    char fen[arrocco::chess::kFenBufferSize];
    const int fenLength = puzzles::startFen(i, fen, sizeof(fen));
    check(fenLength > 0 && fenLength == static_cast<int>(std::strlen(fen)), "startFen writes a FEN");
    Position reparsed;
    check(reparsed.setFen(fen), "startFen round-trips through setFen");
    check(reparsed.samePositionAs(start), "the round-tripped FEN is the same position");
    check(puzzles::startFen(i, fen, 4) == 0, "startFen refuses a short buffer");

    const Color solver = start.sideToMove();

    // ------------------------------------------------------------ the solution
    Move solution[puzzles::kMaxSolutionPlies];
    const int solutionCount = puzzles::solutionMoves(i, solution, puzzles::kMaxSolutionPlies);
    check(solutionCount == tokenCount - 1, "solutionMoves returns every ply");
    check(puzzles::solutionMoves(i, solution, 0) == 0, "solutionMoves refuses a short buffer");

    Position walk = start;
    for (int k = 0; k < solutionCount; ++k) {
      check(walk.sideToMove() == ((k & 1) == 0 ? solver : arrocco::chess::opposite(solver)),
            "the side to move alternates, solver first");
      check(walk.isLegal(solution[k]), "the solution move is legal here");
      solution[k].toUci(uci);
      check(std::strcmp(uci, tokens[k + 1]) == 0, "the solution move is the reference move");
      if (solution[k].isPromotion()) ++withPromotion;
      if (solution[k].isCastling()) ++castlingMoves;
      if (solution[k].isEnPassant()) ++enPassantMoves;
      walk.make(solution[k], undo);
    }
    check(walk.sideToMove() != solver || solutionCount == 0,
          "the line ends after one of the opponent's turns");
    check(puzzle.endsInMate == walk.isCheckmate(), "the mate flag says what really happens");

    if (puzzle.theme == ThemeId::MateIn1) {
      check(solutionCount == 1, "mateIn1 is one ply");
      check(walk.isCheckmate(), "mateIn1 really mates");
    }
    if (puzzle.theme == ThemeId::MateIn2) {
      check(solutionCount == 3, "mateIn2 is three plies");
      check(walk.isCheckmate(), "mateIn2 really mates");
    }
    if (puzzle.theme == ThemeId::MateIn3) {
      check(solutionCount == 5, "mateIn3 is five plies");
      check(walk.isCheckmate(), "mateIn3 really mates");
    }
    if (puzzle.theme == ThemeId::BackRankMate || puzzle.theme == ThemeId::SmotheredMate) {
      check(walk.isCheckmate(), "a named mate pattern really mates");
    }
    if (puzzle.endsInMate) {
      check((solutionCount & 1) == 1, "a mate is delivered by the solver");
    }

    gHashes[i] = start.hash();

    // ------------------------------------------------------------ the cursor
    Cursor cursor;
    check(cursor.begin(i), "the cursor opens the puzzle");
    check(cursor.valid() && cursor.index() == i, "the cursor knows its puzzle");
    check(cursor.solver() == solver, "the cursor agrees on the solver's colour");
    check(cursor.totalPlies() == solutionCount, "the cursor knows the line length");
    check(cursor.position().samePositionAs(start), "the cursor starts on the start position");
    check(!cursor.solved(), "a fresh cursor is not solved");
    check(cursor.solversTurn(), "the solver moves first");
    check(std::strcmp(cursor.puzzle().id, row.id) == 0, "the cursor carries the puzzle record");

    // A legal move that is not the solution must be rejected.
    arrocco::chess::MoveList legal;
    start.generateLegalMoves(legal);
    check(legal.size() > 0, "the start position has legal moves");
    if (legal.size() == 1) ++forcedFirstMove;
    Move wrong = Move::none();
    for (int m = 0; m < legal.size(); ++m) {
      if (!cursor.isCorrect(legal[m])) {
        wrong = legal[m];
        break;
      }
    }
    if (!wrong.isNone()) {
      check(!cursor.play(wrong), "a wrong move is refused");
      check(cursor.plyIndex() == 0, "a refused move does not move the board");
      check(cursor.position().samePositionAs(start), "a refused move leaves the board alone");
    }
    check(!cursor.isCorrect(Move::none()), "the null move is not a solution");

    for (int k = 0; k < solutionCount; k += 2) {
      check(cursor.solversTurn(), "it is the solver's turn");
      check(cursor.expected() == solution[k], "the hint is the solution move");
      check(cursor.isCorrect(solution[k]), "the solution move is accepted");
      check(cursor.play(solution[k]), "the solution move is played");
      check(cursor.lastSolverMove() == solution[k], "the cursor remembers the solver's move");
      if (k + 1 < solutionCount) {
        check(cursor.lastReply() == solution[k + 1], "the cursor played the reply");
        check(cursor.plyIndex() == k + 2, "two plies went by");
      } else {
        check(cursor.lastReply().isNone(), "there is no reply after the last move");
      }
    }
    check(cursor.solved(), "the line solves the puzzle");
    check(cursor.expected().isNone(), "a solved puzzle has no next move");
    check(!cursor.solversTurn(), "a solved puzzle is nobody's turn");
    check(cursor.position().samePositionAs(walk), "the cursor ended where the replay ended");
    check(!cursor.play(solution[0]), "a solved puzzle refuses more moves");

    cursor.restart();
    check(cursor.plyIndex() == 0 && !cursor.solved(), "restart() rewinds");
    check(cursor.position().samePositionAs(start), "restart() puts the start position back");
  }

  // ---------------------------------------------------------------- uniqueness
  gContext = "uniqueness";
  std::qsort(gIds, reference::kRowCount, sizeof(gIds[0]), compareU64);
  int duplicateIds = 0;
  for (int i = 1; i < reference::kRowCount; ++i)
    if (gIds[i] == gIds[i - 1]) ++duplicateIds;
  check(duplicateIds == 0, "every Lichess id appears once");

  std::qsort(gHashes, reference::kRowCount, sizeof(gHashes[0]), compareU64);
  int duplicatePositions = 0;
  for (int i = 1; i < reference::kRowCount; ++i) {
    if (gHashes[i] != gHashes[i - 1]) continue;
    ++duplicatePositions;
    for (int a = 0; a < reference::kRowCount; ++a) {
      Position p;
      if (puzzles::startPosition(a, p) && p.hash() == gHashes[i])
        std::printf("   duplicate start position: #%d %s (%s | %s)\n", a, reference::kRows[a].id,
                    reference::kRows[a].fen, reference::kRows[a].moves);
    }
  }
  check(duplicatePositions == 0, "every start position appears once");

  // ---------------------------------------------------------------- selection
  gContext = "selection";
  check(puzzles::lowerBoundByRating(0) == 0, "lowerBoundByRating(0) is the first puzzle");
  check(puzzles::lowerBoundByRating(60000) == puzzles::count(), "an absurd rating lands past the end");
  int bandTotal = 0;
  for (int band = 0; band < 14; ++band) {
    const uint16_t low = static_cast<uint16_t>(600 + band * 100);
    const uint16_t high = static_cast<uint16_t>(low + 99);
    Filter filter;
    filter.minRating = low;
    filter.maxRating = high;
    const int found = puzzles::countMatching(filter);
    check(found == gBandCount[band], "countMatching agrees with the walk over the band");
    check(found == 250, "the band holds 250 puzzles");
    bandTotal += found;
    const int first = puzzles::next(filter, 0);
    check(first == puzzles::lowerBoundByRating(low), "next() starts at the lower bound");
    check(puzzles::nth(filter, 0) == first, "nth(0) is the first match");
    check(puzzles::nth(filter, found - 1) >= 0, "the last match exists");
    check(puzzles::nth(filter, found) == -1, "there is no match past the end");
  }
  check(bandTotal == puzzles::count(), "the bands cover the whole pack");

  int themeTotal = 0;
  for (int t = 0; t < static_cast<int>(ThemeId::Count); ++t) {
    Filter filter;
    filter.theme = static_cast<ThemeId>(t);
    const int found = puzzles::countMatching(filter);
    check(found == gThemeCount[t], "countMatching agrees with the walk over the theme");
    check(found > 0, "every theme is represented");
    themeTotal += found;
    for (int idx = puzzles::next(filter, 0); idx >= 0; idx = puzzles::next(filter, idx + 1)) {
      check(puzzles::matches(filter, idx), "next() only returns matches");
    }
  }
  check(themeTotal == puzzles::count(), "the themes cover the whole pack");

  Filter mates;
  mates.matesOnly = true;
  const int mateCount = puzzles::countMatching(mates);
  check(mateCount > 0, "there are mate puzzles");
  for (int idx = puzzles::next(mates, 0); idx >= 0; idx = puzzles::next(mates, idx + 1)) {
    Puzzle p;
    puzzles::byIndex(idx, p);
    check(p.endsInMate, "matesOnly only returns mates");
  }

  Filter empty;
  empty.minRating = 3000;
  empty.maxRating = 3100;
  check(puzzles::next(empty, 0) == -1, "an empty band has no first match");
  check(puzzles::countMatching(empty) == 0, "an empty band counts zero");
  check(puzzles::nth(empty, 0) == -1, "an empty band has no n-th match");

  // ---------------------------------------------------------------- coverage
  //
  // The three move kinds the encoding does not store have to survive the trip, so
  // the pack must contain at least one of each for the checks above to mean
  // anything.  A regenerated pack that quietly lost them fails here.
  gContext = "coverage";
  check(withPromotion > 0, "some solution promotes a pawn");
  check(castlingMoves > 0, "some line castles");
  check(enPassantMoves > 0, "some line captures en passant");
  check(withEnPassant > 0, "some start position carries an e.p. square");
  check(withCastlingRights > 0, "some start position carries castling rights");

  // ---------------------------------------------------------------- report
  std::printf("\nrating bands\n");
  for (int band = 0; band < 14; ++band)
    std::printf("  %4d-%4d  %4d\n", 600 + band * 100, 699 + band * 100, gBandCount[band]);
  std::printf("themes\n");
  for (int t = 0; t < static_cast<int>(ThemeId::Count); ++t)
    std::printf("  %-20s %4d\n", puzzles::themeKey(static_cast<ThemeId>(t)), gThemeCount[t]);
  std::printf("mate puzzles %d, e.p. positions %d, castling rights %d, promotions %d, "
              "forced first move %d\n",
              mateCount, withEnPassant, withCastlingRights, withPromotion, forcedFirstMove);
  std::printf("move kinds rediscovered by the reader: %d castling, %d en passant\n",
              castlingMoves, enPassantMoves);
  std::printf("%lld checks, %lld failures\n", gChecks, gFailures);
  return gFailures != 0 ? 1 : 0;
}
