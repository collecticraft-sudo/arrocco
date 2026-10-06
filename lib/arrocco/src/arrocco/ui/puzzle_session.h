// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — one puzzle at a time, as the solver lives it: the opponent's move that
// sets it up, the solver's moves and the replies, a wrong try, the hint, the solution,
// skip and retry, and the score each puzzle leaves on the progress (puzzle_progress.h).
//
// No drawing and no clock in here. PuzzleScreen draws board() with shownMove() marked and
// decides WHEN a pending opponent move appears (playPending(), a moment after the refresh
// that showed the position before it): everything the board shows comes out of this
// object, so the native test can walk every puzzle of the pack through it.
//
// When the score is decided, once per puzzle:
//   - the first wrong move, the first move the solution plays, or a skip: Missed;
//   - the line played to the end by the solver otherwise: Solved, or Hinted after a hint.
// Whatever happens next on the same puzzle (more tries, Retry) changes nothing.
#pragma once
#include <cstdint>

#include "arrocco/chess/move.h"
#include "arrocco/chess/position.h"
#include "arrocco/puzzles/puzzles.h"
#include "arrocco/ui/puzzle_progress.h"

namespace arrocco::ui {

class PuzzleSession {
 public:
  enum class Pending : uint8_t {
    None,
    Setup,   // the puzzle has just appeared: the opponent's blunder is still to come
    Reply,   // the solver's move stands: the opponent's answer is still to come
  };
  enum class Verdict : uint8_t {
    Ignored,   // not the solver's turn, or not a legal move of board()
    Correct,   // the line goes on: Pending::Reply
    Solved,    // the line is over
    Wrong,     // not the move: the board stays as it was, wrongMove() says what was tried
  };

  // ---- the progress
  const PuzzleProgress& progress() const { return progress_; }
  // Replaces the progress (the one read back at boot) and unloads the puzzle.
  void setProgress(const PuzzleProgress& progress);
  bool hasLevel() const { return progress_.started; }
  // A fresh start at a picker level (startAtLevel), then a new puzzle for it.
  bool pickLevel(int level, uint32_t entropy);

  // ---- which puzzle
  bool loaded() const { return cursor_.valid(); }
  // What opening the screen does: the puzzle already loaded, else the one the progress
  // names (resume), else a new one (next).
  bool ensureLoaded();
  // The puzzle the progress names, at the ply it reached. False, and nothing loaded, if
  // it names none or it does not load.
  bool resume();
  // Scores a skip if the puzzle on the board was not scored yet, then draws the next one
  // and starts it with Pending::Setup. False only for an empty pack.
  bool next();
  // The same puzzle from its start position, its score kept: practice.
  void retry();

  // ---- what the board shows
  const chess::Position& board() const { return shown_; }
  chess::Move shownMove() const { return shownMove_; }   // the move to mark, or none()
  const char* shownSan() const { return shownSan_; }      // ... in SAN, "" when none
  Pending pending() const { return pending_; }
  void playPending();                                      // the opponent's move now stands

  const puzzles::Puzzle& puzzle() const { return cursor_.puzzle(); }
  int index() const { return cursor_.index(); }
  chess::Color solver() const { return cursor_.solver(); }
  bool solversTurn() const { return pending_ == Pending::None && cursor_.valid() && cursor_.solversTurn(); }
  bool done() const { return cursor_.valid() && cursor_.solved(); }
  // The opponent's move on the board is the blunder that set the puzzle up.
  bool atStart() const { return cursor_.valid() && cursor_.plyIndex() == 0; }
  uint8_t flags() const { return progress_.flags; }
  bool scored() const { return (progress_.flags & kPuzzleScored) != 0; }

  // ---- the solver's actions
  // A legal move of board(). Wrong leaves the position alone (wrongMove() until the next action).
  Verdict play(chess::Move m);
  chess::Move wrongMove() const { return wrong_; }
  // The move to find now (marks the puzzle hinted), or none() when no move is expected.
  chess::Move hint();
  // Plays the solver's next move for them (and scores a miss). False if no move is expected.
  bool solutionStep();
  // The solver's move standing on the board was played by the solution.
  bool lastBySolution() const { return bySolution_; }

 private:
  bool load(int index);
  void unload();
  void score(PuzzleScore s);
  // Plays the solver's move `m`, already known to be correct, and shows it: with the
  // opponent's answer still to come (Pending::Reply), or alone at the end of the line.
  void advance(chess::Move m);
  void showStart();            // the start position, the blunder marked

  PuzzleProgress progress_;
  puzzles::Cursor cursor_;
  chess::Position shown_;
  chess::Move shownMove_;
  chess::Move blunder_;
  chess::Move wrong_;
  char shownSan_[chess::kSanBufferSize] = {};
  char blunderSan_[chess::kSanBufferSize] = {};
  char replySan_[chess::kSanBufferSize] = {};   // the pending reply's
  Pending pending_ = Pending::None;
  bool bySolution_ = false;
};

}  // namespace arrocco::ui
