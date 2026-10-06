// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the puzzle session. See puzzle_session.h.
#include "arrocco/ui/puzzle_session.h"

#include <cstring>

namespace arrocco::ui {

using chess::Move;
using chess::Position;
using chess::Undo;

namespace {

void copySan(char* out, const char* san) {
  strncpy(out, san, chess::kSanBufferSize - 1);
  out[chess::kSanBufferSize - 1] = '\0';
}

}  // namespace

void PuzzleSession::setProgress(const PuzzleProgress& progress) {
  progress_ = progress;
  // Whether the board was on the glass is the screen's to say, refresh by refresh.
  progress_.flags = static_cast<uint8_t>(progress_.flags & ~kPuzzleOnScreen);
  unload();
}

bool PuzzleSession::pickLevel(int level, uint32_t entropy) {
  startAtLevel(progress_, level, entropy);
  unload();          // the puzzle on the board goes without a score: the rating starts again
  return next();
}

void PuzzleSession::unload() {
  cursor_ = puzzles::Cursor();
  shown_.setStartpos();
  shownMove_ = Move::none();
  blunder_ = Move::none();
  wrong_ = Move::none();
  shownSan_[0] = '\0';
  blunderSan_[0] = '\0';
  replySan_[0] = '\0';
  pending_ = Pending::None;
  bySolution_ = false;
}

bool PuzzleSession::ensureLoaded() {
  if (loaded()) {
    playPending();   // left while the opponent was about to move: it has moved
    wrong_ = Move::none();
    return true;
  }
  return resume() || next();
}

bool PuzzleSession::load(int index) {
  unload();
  if (index < 0 || !cursor_.begin(index) || !puzzles::setupPosition(index, shown_)) {
    unload();
    return false;
  }
  blunder_ = puzzles::blunderMove(index);
  shown_.toSan(blunder_, blunderSan_);
  pending_ = Pending::Setup;   // shown_ is the position before the blunder, nothing marked
  progress_.current = static_cast<uint16_t>(index);
  progress_.plies = 0;
  progress_.flags = 0;
  return true;
}

bool PuzzleSession::resume() {
  const uint16_t index = progress_.current;
  const uint8_t plies = progress_.plies;
  const uint8_t flags = progress_.flags;
  if (index == kNoPuzzle || !load(index)) {
    unload();
    progress_.current = kNoPuzzle;
    progress_.plies = 0;
    progress_.flags = 0;
    return false;
  }
  showStart();
  // Back where it was: the stored line replayed up to the ply it had reached. A different
  // mate the solver found is not stored, so a finished mate comes back as the pack's own.
  while (cursor_.plyIndex() < plies && cursor_.solversTurn()) {
    const Move m = cursor_.expected();
    const Position before = cursor_.position();
    before.toSan(m, shownSan_);
    if (!cursor_.play(m)) break;
    shownMove_ = m;
    if (!cursor_.lastReply().isNone()) {
      Position middle = before;
      Undo undo;
      middle.make(m, undo);
      middle.toSan(cursor_.lastReply(), shownSan_);
      shownMove_ = cursor_.lastReply();
    }
  }
  shown_ = cursor_.position();
  progress_.flags = flags;
  progress_.plies = static_cast<uint8_t>(cursor_.plyIndex());
  return true;
}

void PuzzleSession::showStart() {
  shown_ = cursor_.position();
  shownMove_ = blunder_;
  copySan(shownSan_, blunderSan_);
  pending_ = Pending::None;
}

bool PuzzleSession::next() {
  playPending();
  if (loaded() && !scored()) score(PuzzleScore::Missed);   // a skip
  const int index = drawPuzzle(progress_);
  if (index < 0) return false;
  if (load(index)) return true;
  // Unreachable with the pack the test checks; a puzzle that does not load is passed over.
  for (int tries = 0; tries < 8; ++tries) {
    const int other = drawPuzzle(progress_);
    if (other >= 0 && load(other)) return true;
  }
  return false;
}

void PuzzleSession::retry() {
  if (!loaded()) return;
  cursor_.restart();
  wrong_ = Move::none();
  bySolution_ = false;
  showStart();
  // The attempt starts again; the score stays. Retried marks the practice, once there
  // is a score for it to leave alone.
  const bool wasScored = scored();
  progress_.flags = static_cast<uint8_t>(progress_.flags & ~kPuzzleAttemptFlags);
  if (wasScored) progress_.flags |= kPuzzleRetried;
  progress_.plies = 0;
}

void PuzzleSession::playPending() {
  if (pending_ == Pending::None) return;
  if (pending_ == Pending::Setup) {
    showStart();
    return;
  }
  pending_ = Pending::None;
  shown_ = cursor_.position();
  shownMove_ = cursor_.lastReply();
  copySan(shownSan_, replySan_);
}

void PuzzleSession::score(PuzzleScore s) {
  scorePuzzle(progress_, cursor_.puzzle().rating, s);
  progress_.flags |= kPuzzleScored;
}

void PuzzleSession::advance(Move m) {
  const Position before = cursor_.position();
  before.toSan(m, shownSan_);
  cursor_.play(m);
  progress_.plies = static_cast<uint8_t>(cursor_.plyIndex());
  shownMove_ = m;
  if (cursor_.lastReply().isNone()) {
    shown_ = cursor_.position();
    pending_ = Pending::None;
    return;
  }
  shown_ = before;
  Undo undo;
  shown_.make(m, undo);
  shown_.toSan(cursor_.lastReply(), replySan_);
  pending_ = Pending::Reply;
}

PuzzleSession::Verdict PuzzleSession::play(Move m) {
  playPending();
  wrong_ = Move::none();
  if (!solversTurn() || m.isNone() || !shown_.isLegal(m)) return Verdict::Ignored;
  if (!cursor_.isCorrect(m)) {
    wrong_ = m;
    progress_.flags |= kPuzzleMissed;
    if (!scored()) score(PuzzleScore::Missed);
    return Verdict::Wrong;
  }
  bySolution_ = false;
  advance(m);
  if (!cursor_.solved()) return Verdict::Correct;
  if (!scored()) score((progress_.flags & kPuzzleHinted) != 0 ? PuzzleScore::Hinted : PuzzleScore::Solved);
  return Verdict::Solved;
}

Move PuzzleSession::hint() {
  playPending();
  wrong_ = Move::none();
  if (!solversTurn()) return Move::none();
  progress_.flags |= kPuzzleHinted;
  return cursor_.expected();
}

bool PuzzleSession::solutionStep() {
  playPending();
  wrong_ = Move::none();
  if (!solversTurn()) return false;
  progress_.flags |= kPuzzleHelped;
  if (!scored()) score(PuzzleScore::Missed);
  bySolution_ = true;
  advance(cursor_.expected());
  return true;
}

}  // namespace arrocco::ui
