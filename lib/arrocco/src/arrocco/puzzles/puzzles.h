// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco puzzles — the reader for the offline Lichess puzzle pack.
//
// Everything here is portable C++17 with no heap, no STL and no exceptions: the
// pack lives in flash (puzzle_data.cpp) and is decoded straight out of it, so the
// ESP32 spends nothing but stack on it.  The only state a caller keeps is a
// Cursor (about 300 bytes), which is meant to be a member of a screen.
//
// A Lichess puzzle is stored the way Lichess publishes it: the position BEFORE
// the opponent's blunder, and a move list whose FIRST move is that blunder.  So
//   setupPosition(i)  is the position before the blunder,
//   blunderMove(i)    is the opponent's mistake,
//   startPosition(i)  is what the solver is shown  (setup + blunder),
//   solutionMoves(i)  is the rest: solver, opponent, solver, ...
// The solver always moves first in startPosition(), so the side to move there is
// the colour the player takes.
//
// Typical use (no UI here — this is only the model):
//   puzzles::Cursor cursor;
//   cursor.begin(puzzles::nth(filter, n));
//   draw(cursor.position());
//   if (cursor.isCorrect(move)) cursor.play(move);   // plays the reply too
//   if (cursor.solved()) celebrate();
#pragma once
#include <cstdint>

#include "arrocco/chess/move.h"
#include "arrocco/chess/position.h"
#include "arrocco/chess/types.h"
#include "arrocco/puzzles/theme_data.h"

namespace arrocco::puzzles {

// 5 Lichess id characters plus the NUL: "0008Q" -> lichess.org/training/0008Q
constexpr int kIdBufferSize = 6;
// The pack keeps at most 12 plies per puzzle, the blunder included.
constexpr int kMaxSolutionPlies = 11;

struct Puzzle {
  char id[kIdBufferSize];  // NUL-terminated Lichess puzzle id
  uint16_t rating;         // Lichess Glicko rating of the puzzle
  ThemeId theme;           // the single label we kept, see themeName()
  uint8_t solutionPlies;   // plies after startPosition(): solver, opponent, solver, ...
  bool endsInMate;         // the line finishes with checkmate
};

// ------------------------------------------------------------------ the pack

int count();
uint16_t formatVersion();
uint32_t packBytes();  // flash the pack occupies: index + blob

// false (and `out` untouched) if `index` is out of range.
bool byIndex(int index, Puzzle& out);

// ------------------------------------------------------------------ positions

// The position published by Lichess: before the opponent blunders.
bool setupPosition(int index, chess::Position& out);
// The opponent's blunder, legal in setupPosition(). Move::none() on error.
chess::Move blunderMove(int index);

// The position the solver is shown. Its sideToMove() is the colour the player takes.
bool startPosition(int index, chess::Position& out);
// FEN of that position; returns the length, or 0 if `outSize` is too small
// (chess::kFenBufferSize is always enough).
int startFen(int index, char* out, int outSize);

// The solution in order, starting with the solver's first move.  Returns how many
// moves were written (0 on error); pass a buffer of kMaxSolutionPlies.
int solutionMoves(int index, chess::Move* out, int outSize);

// ------------------------------------------------------------------ selection
//
// The pack is sorted by rating, so a rating band is a contiguous run and costs a
// binary search; a theme costs a linear scan over 12-byte records, which is a few
// microseconds even on the ESP32.

struct Filter {
  uint16_t minRating = 0;
  uint16_t maxRating = 0xFFFF;
  ThemeId theme = ThemeId::Count;  // Count means "any theme"
  bool matesOnly = false;
};

// First index whose rating is >= `rating`; count() when every puzzle is easier.
int lowerBoundByRating(uint16_t rating);

// Index of the first match at or after `fromIndex`, or -1.  Walk a filter with
//   for (int i = puzzles::next(f, 0); i >= 0; i = puzzles::next(f, i + 1)) ...
int next(const Filter& filter, int fromIndex);
int countMatching(const Filter& filter);
// The n-th match (0-based), or -1 if there are fewer.  Handy for "puzzle 7 of 42".
int nth(const Filter& filter, int n);

bool matches(const Filter& filter, int index);

// ------------------------------------------------------------------ solving

// Steps a player through one puzzle.  No allocation: keep one as a member.
class Cursor {
 public:
  Cursor() = default;

  // Loads the puzzle and places the board on its start position.  false if the
  // index is out of range or the pack does not decode (which the test rules out).
  bool begin(int index);
  void restart();  // back to the start position of the same puzzle

  bool valid() const { return index_ >= 0; }
  int index() const { return index_; }
  const Puzzle& puzzle() const { return puzzle_; }

  // The board as it stands now.
  const chess::Position& position() const { return position_; }
  // The colour the player takes.
  chess::Color solver() const { return solver_; }

  int plyIndex() const { return ply_; }        // solution plies already played
  int totalPlies() const { return total_; }
  bool solved() const { return ply_ >= total_; }
  bool solversTurn() const { return !solved() && (ply_ & 1) == 0; }

  // The move the player is meant to find now, or Move::none() when solved.
  // This is the hint / "show solution" move.
  chess::Move expected() const;

  // Is `m` accepted here?  The stored move always is; when the stored move is
  // mate, any other mating move is accepted too (Lichess scores those as solved).
  bool isCorrect(chess::Move m) const;

  // Plays `m` if isCorrect(m), then immediately plays the opponent's reply when
  // the line continues.  Returns false and changes nothing if `m` is wrong.
  bool play(chess::Move m);

  // What the last successful play() put on the board, for highlighting.
  chess::Move lastSolverMove() const { return lastSolver_; }
  chess::Move lastReply() const { return lastReply_; }  // none() if the line ended

 private:
  int index_ = -1;
  Puzzle puzzle_{};
  chess::Position start_;
  chess::Position position_;
  chess::Move moves_[kMaxSolutionPlies];
  int total_ = 0;
  int ply_ = 0;
  chess::Color solver_ = chess::Color::White;
  chess::Move lastSolver_;
  chess::Move lastReply_;
};

}  // namespace arrocco::puzzles
