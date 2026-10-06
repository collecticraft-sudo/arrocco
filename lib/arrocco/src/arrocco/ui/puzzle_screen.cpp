// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the puzzle screen. See puzzle_screen.h.
#include "arrocco/ui/puzzle_screen.h"

#include <cstdio>
#include <cstring>

#include <Adafruit_GFX.h>

#include "arrocco/ui/board_view.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

using chess::Color;
using chess::Move;
using chess::Square;
using Pending = PuzzleSession::Pending;
using Verdict = PuzzleSession::Verdict;

constexpr int16_t kCenterX = arrocco::kScreenW / 2;
constexpr int kSlotPickerBack = 5;     // the picker's last menu slot, as "Back" everywhere
// A wrong move sounds lower and longer than anything else the board plays.
constexpr uint16_t kWrongHz = 330;
constexpr uint16_t kWrongMs = 150;

const char* const kLevelNames[kPuzzleLevelCount] = {str::kPuzzleLevel1, str::kPuzzleLevel2, str::kPuzzleLevel3,
                                                    str::kPuzzleLevel4};

const char* colorName(Color c) { return c == Color::White ? str::kWhiteLabel : str::kBlackLabel; }

// An X across the middle of the square, on a white disc.
void drawCross(Adafruit_GFX& gfx, const Rect& r) {
  const int16_t cx = r.cx();
  const int16_t cy = r.cy();
  const int16_t h = kPuzzleCrossHalf;
  gfx.fillCircle(cx, cy, kPuzzleCrossDisc, kWhite);
  for (int16_t t = static_cast<int16_t>(-kPuzzleCrossStroke / 2); t < kPuzzleCrossStroke - kPuzzleCrossStroke / 2;
       ++t) {
    gfx.drawLine(static_cast<int16_t>(cx - h + t), static_cast<int16_t>(cy - h), static_cast<int16_t>(cx + h + t),
                 static_cast<int16_t>(cy + h), kBlack);
    gfx.drawLine(static_cast<int16_t>(cx - h + t), static_cast<int16_t>(cy + h), static_cast<int16_t>(cx + h + t),
                 static_cast<int16_t>(cy - h), kBlack);
  }
}

}  // namespace

// ---- the store -------------------------------------------------------------------------------

void PuzzleScreen::restore() {
  wasOnScreen_ = false;
  storedSize_ = ctx_.platform.loadBlob(kPuzzleProgressKey, stored_, sizeof stored_);
  if (storedSize_ == 0) return;
  PuzzleProgress progress;
  bool packChanged = false;
  // Anything else than progress stays in the store, untouched, until a level is picked.
  if (decodePuzzleProgress(stored_, storedSize_, progress, packChanged) != ProgressCheck::Ok) return;
  wasOnScreen_ = (progress.flags & kPuzzleOnScreen) != 0;
  session_.setProgress(progress);
}

void PuzzleScreen::persist(bool screenIsCurrent) {
  if (!session_.hasLevel()) return;
  PuzzleProgress progress = session_.progress();
  // The puzzle screen is on the glass, its board or the picker over it: the picker does not
  // count as leaving (the board never sleeps on it with the board kept, boardOnScreen()).
  if (screenIsCurrent && session_.loaded()) progress.flags |= kPuzzleOnScreen;
  const size_t size = encodePuzzleProgress(progress, scratch_, sizeof scratch_);
  if (size == 0 || (size == storedSize_ && memcmp(scratch_, stored_, size) == 0)) return;
  // A write that fails is tried again after the next refresh: the bytes still differ.
  if (!ctx_.platform.storeBlob(kPuzzleProgressKey, scratch_, size)) return;
  memcpy(stored_, scratch_, size);
  storedSize_ = size;
}

// ---- entering, and the opponent's moves -----------------------------------------------------------

void PuzzleScreen::enter() {
  deselect();
  wrong_ = Move::none();
  hintSquare_ = chess::kNoSquare;
  pendingArmed_ = false;
  pickerFromBoard_ = false;
  mode_ = session_.hasLevel() && session_.ensureLoaded() ? Mode::Board : Mode::Picker;
}

Action PuzzleScreen::onTick(uint32_t now) {
  if (mode_ != Mode::Board || session_.pending() == Pending::None) return Action::none();
  // The first tick after the refresh that showed the position before the move: from now.
  if (!pendingArmed_) {
    pendingArmed_ = true;
    pendingDueMs_ = now + kPuzzleOpponentDelayMs;
    return Action::none();
  }
  if (static_cast<int32_t>(now - pendingDueMs_) < 0) return Action::none();
  return opponentMoves();
}

Action PuzzleScreen::opponentMoves() {
  pendingArmed_ = false;
  session_.playPending();
  ctx_.play(Sound::Move);
  // A move just landed: the natural pause where a Full refresh may clear the ghosting.
  return Action::pauseRepaint();
}

// ---- drawing ------------------------------------------------------------------------------------

bool PuzzleScreen::flipped() const {
  // The solver at the bottom, unless the owner asked for the board the other way up.
  return (session_.solver() == Color::Black) != ctx_.settings.flipByDefault;
}

void PuzzleScreen::draw(Adafruit_GFX& gfx) {
  if (mode_ == Mode::Picker) {
    drawPicker(gfx);
  } else {
    drawBoardScreen(gfx);
  }
}

void PuzzleScreen::drawPicker(Adafruit_GFX& gfx) {
  drawCentered(gfx, Font::Bold24, str::kPuzzleTitle, kCenterX, static_cast<int16_t>(kMenuTitleBaseline - 12));
  char subtitle[96];
  const PuzzleProgress& p = session_.progress();
  if (pickerFromBoard_) {
    snprintf(subtitle, sizeof subtitle, str::kPuzzlePickAgainFmt, p.rating.rating, p.played);
  } else {
    snprintf(subtitle, sizeof subtitle, "%s", str::kPuzzlePickFirst);
  }
  drawCentered(gfx, Font::Sans9, subtitle, kCenterX, kMenuSubtitleBaseline);
  for (int level = 0; level < kPuzzleLevelCount; ++level) {
    char note[24];
    snprintf(note, sizeof note, str::kPuzzleLevelNoteFmt, kPuzzleLevelRatings[level]);
    drawButton(gfx, menuButtonRect(level), Font::Bold12, kLevelNames[level], true, note);
  }
  drawButton(gfx, menuButtonRect(kSlotPickerBack), Font::Bold12, str::kBack);
  drawCentered(gfx, Font::Sans9, str::kPuzzleCredit, kCenterX, kMenuFooterBaseline);
}

void PuzzleScreen::drawBoardScreen(Adafruit_GFX& gfx) {
  const chess::Position& pos = session_.board();
  BoardMarks marks;
  marks.selected = selected_;
  marks.targets = targets_;
  const Move last = session_.shownMove();
  if (!last.isNone()) {
    marks.lastFrom = last.from();
    marks.lastTo = last.to();
  }
  marks.checkedKing = pos.checkedKingSquare();
  drawBoard(gfx, pos, marks, flipped());
  if (!wrong_.isNone()) drawCross(gfx, squareRect(wrong_.to(), flipped()));
  drawSideColumn(gfx);
  if (mode_ == Mode::Promotion) drawPromotionPopup(gfx, session_.solver());
}

void PuzzleScreen::drawSideColumn(Adafruit_GFX& gfx) {
  const chess::Position& pos = session_.board();
  const Color solver = session_.solver();
  const Color opponent = chess::opposite(solver);
  const uint8_t flags = session_.flags();
  const char* head = "";
  char sub[48] = "";

  // ---- what is happening: whose move it is, or how the puzzle went
  if (session_.pending() == Pending::Setup) {
    head = str::kPuzzleNew;
    snprintf(sub, sizeof sub, str::kPuzzleAboutToMoveFmt, colorName(opponent));
  } else if (session_.pending() == Pending::Reply) {
    if (session_.lastBySolution()) {
      head = str::kPuzzleSolutionHead;
      snprintf(sub, sizeof sub, str::kPuzzleSolutionFmt, colorName(solver), session_.shownSan());
    } else {
      head = str::kPuzzleCorrect;
      snprintf(sub, sizeof sub, str::kPuzzleYouPlayedFmt, session_.shownSan());
    }
  } else if (session_.done()) {
    head = str::kPuzzleSolved;
    const char* how = pos.isCheckmate() ? str::kPuzzleSolvedMate : str::kPuzzleSolvedWell;
    if (session_.lastBySolution()) {
      head = str::kPuzzleSolutionHead;
      how = str::kPuzzleSolutionEnd;
    } else if ((flags & kPuzzleHelped) != 0) {
      head = str::kPuzzleSolvedPlain;
      how = str::kPuzzleSolvedHelped;
    } else if ((flags & kPuzzleMissed) != 0) {
      head = str::kPuzzleSolvedPlain;
      how = str::kPuzzleSolvedLate;
    } else if ((flags & kPuzzleHinted) != 0) {
      how = str::kPuzzleSolvedHint;
    } else if ((flags & kPuzzleRetried) != 0) {
      how = str::kPuzzleSolvedPractice;
    }
    snprintf(sub, sizeof sub, "%s", how);
  } else if (!wrong_.isNone()) {
    head = str::kPuzzleWrong;
    char san[chess::kSanBufferSize];
    pos.toSan(wrong_, san);
    snprintf(sub, sizeof sub, str::kPuzzleTryAgainFmt, san);
  } else {
    head = solver == Color::White ? str::kWhiteToMove : str::kBlackToMove;
    if (hintSquare_ != chess::kNoSquare && selected_ == hintSquare_) {
      snprintf(sub, sizeof sub, "%s", str::kPuzzleHintLine);
    } else if (session_.shownSan()[0] != '\0') {
      snprintf(sub, sizeof sub, str::kPuzzlePlayedFmt, colorName(opponent), session_.shownSan());
    }
  }
  drawText(gfx, Font::Bold18, kSideInnerX, kTurnBaseline, head);
  drawText(gfx, Font::Sans12, kSideInnerX, kLastMoveBaseline, sub);
  gfx.drawFastHLine(kSideInnerX, kDivider1Y, kSideInnerW, kBlack);

  // ---- the puzzle: its theme (for a child, the goal: "Mate in 2"), rating and Lichess id
  char line[48];
  const puzzles::Puzzle& puzzle = session_.puzzle();
  drawText(gfx, Font::Bold12, kSideInnerX, kPuzzleThemeBaseline, puzzles::themeName(puzzle.theme));
  snprintf(line, sizeof line, str::kPuzzleRatingFmt, puzzle.rating);
  drawText(gfx, Font::Sans9, kSideInnerX, kPuzzleRatingBaseline, line);
  snprintf(line, sizeof line, str::kPuzzleIdFmt, puzzle.id);
  drawText(gfx, Font::Sans9, kSideInnerX, kPuzzleIdBaseline, line);
  gfx.drawFastHLine(kSideInnerX, kDivider2Y, kSideInnerW, kBlack);

  // ---- the solver: rating, what this puzzle did to it, the counts
  const PuzzleProgress& p = session_.progress();
  drawText(gfx, Font::Sans9, kSideInnerX, kPuzzleYourLabelBaseline, str::kPuzzleYourRating);
  snprintf(line, sizeof line, "%u", static_cast<unsigned>(p.rating.rating));
  drawText(gfx, Font::Bold18, kSideInnerX, kPuzzleYourRatingBaseline, line);
  if (session_.scored()) {
    const int16_t x = static_cast<int16_t>(kSideInnerX + textWidth(gfx, Font::Bold18, line) + 14);
    snprintf(line, sizeof line, "%+d", static_cast<int>(p.lastChange));
    drawText(gfx, Font::Bold12, x, kPuzzleYourRatingBaseline, line);
  }
  if (p.played == 0) {
    snprintf(line, sizeof line, "%s", str::kPuzzleStatsNone);
  } else {
    const int used = snprintf(line, sizeof line, str::kPuzzleStatsFmt, p.solved, p.played);
    if (p.streak >= 2 && used > 0 && static_cast<size_t>(used) < sizeof line)
      snprintf(line + used, sizeof line - static_cast<size_t>(used), str::kPuzzleStreakFmt, p.streak);
  }
  drawText(gfx, Font::Sans9, kSideInnerX, kPuzzleStatsBaseline, line);
  if (p.rating.deviation >= kPuzzleSettledDeviation)
    drawText(gfx, Font::Sans9, kSideInnerX, kPuzzleNoteBaseline, str::kPuzzleProvisional);

  // ---- the buttons: Retry and Next once the line is over
  const bool over = session_.done();
  drawButton(gfx, sideButtonRect(kHintOrRetry), Font::Bold12, over ? str::kButtonRetry : str::kButtonHint);
  if (!over) drawButton(gfx, sideButtonRect(kSolution), Font::Bold12, str::kButtonSolution);
  drawButton(gfx, sideButtonRect(kSkipOrNext), Font::Bold12,
             over || session_.scored() ? str::kButtonNextPuzzle : str::kButtonSkip);
  drawButton(gfx, sideButtonRect(kLevel), Font::Bold12, str::kButtonLevel);
  drawButton(gfx, sideButtonRect(kMenu), Font::Bold12, str::kButtonMenu);
}

// ---- input ------------------------------------------------------------------------------------------

void PuzzleScreen::select(Square s) {
  selected_ = s;
  targets_ = session_.board().legalTargetsFrom(s);
  wrong_ = Move::none();     // a new try: the X of the last one goes
}

void PuzzleScreen::deselect() {
  selected_ = chess::kNoSquare;
  targets_.clear();
}

void PuzzleScreen::wrongBeep() {
  if (ctx_.settings.sound) ctx_.platform.beep(kWrongHz, kWrongMs);
}

Action PuzzleScreen::onTap(int16_t x, int16_t y) {
  switch (mode_) {
    case Mode::Picker:    return onPickerTap(x, y);
    case Mode::Promotion: return onPromotionTap(x, y);
    case Mode::Board:     return onBoardTap(x, y);
  }
  return Action::none();
}

Action PuzzleScreen::onPickerTap(int16_t x, int16_t y) {
  const int slot = menuButtonAt(x, y);
  if (slot >= 0 && slot < kPuzzleLevelCount) {
    // A new puzzle for the new level, its blunder still to come (Pending::Setup).
    if (!session_.pickLevel(slot, ctx_.platform.millis())) return Action::none();
    deselect();
    wrong_ = Move::none();
    hintSquare_ = chess::kNoSquare;
    pendingArmed_ = false;
    pickerFromBoard_ = false;
    mode_ = Mode::Board;
    // The whole screen changes, from the picker to the board.
    return Action::repaint(Refresh::Full);
  }
  if (slot != kSlotPickerBack) return Action::none();
  if (pickerFromBoard_ && session_.loaded()) {
    pickerFromBoard_ = false;
    mode_ = Mode::Board;
    return Action::repaint(Refresh::Full);
  }
  return Action::go(ScreenId::Menu);
}

Action PuzzleScreen::onBoardTap(int16_t x, int16_t y) {
  const Square s = squareAt(x, y, flipped());
  const int slot = sideButtonAt(x, y);
  if (session_.pending() != Pending::None) {
    // The opponent is about to move. Menu leaves (the move is made all the same); any
    // other button or square makes the opponent move now, and means nothing more.
    if (slot == kMenu) return onButton(slot);
    if (s != chess::kNoSquare || (slot >= kHintOrRetry && slot <= kLevel)) return opponentMoves();
    return Action::none();
  }
  if (s != chess::kNoSquare) return onSquare(s);
  if (slot >= 0) return onButton(slot);
  return Action::none();
}

Action PuzzleScreen::onSquare(Square s) {
  if (!session_.solversTurn()) return Action::none();   // the line is over: the board is a picture
  const chess::Position& pos = session_.board();
  const chess::Piece piece = pos.pieceAt(s);
  const bool own = !piece.isNone() && piece.color() == pos.sideToMove();
  if (selected_ == chess::kNoSquare) {
    if (!own) return Action::none();          // nothing to pick up: no refresh at all
    select(s);
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  if (s == selected_) {
    deselect();
    return Action::repaint();
  }
  if (targets_.contains(s)) {
    if (pos.isPromotionMove(selected_, s)) {
      promotionFrom_ = selected_;
      promotionTo_ = s;
      mode_ = Mode::Promotion;
      return Action::repaint();
    }
    return tryMove(pos.findLegalMove(selected_, s));
  }
  if (own) {
    select(s);
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  deselect();
  return Action::repaint();
}

Action PuzzleScreen::onPromotionTap(int16_t x, int16_t y) {
  const int choice = promotionChoiceAt(x, y);
  if (choice < 0) {                            // anywhere else: give up the move
    mode_ = Mode::Board;
    deselect();
    return Action::repaint();
  }
  return tryMove(session_.board().findLegalMove(promotionFrom_, promotionTo_, promotionPiece(choice)));
}

Action PuzzleScreen::tryMove(Move m) {
  deselect();
  mode_ = Mode::Board;
  wrong_ = Move::none();
  hintSquare_ = chess::kNoSquare;
  switch (session_.play(m)) {
    case Verdict::Correct:
      pendingArmed_ = false;
      ctx_.play(Sound::Move);
      return Action::pauseRepaint();
    case Verdict::Solved:
      ctx_.play(Sound::GameOver);
      return Action::pauseRepaint();
    case Verdict::Wrong:
      wrong_ = m;
      wrongBeep();
      return Action::repaint();
    case Verdict::Ignored:
      break;
  }
  return Action::repaint();
}

Action PuzzleScreen::onButton(int slot) {
  const bool over = session_.done();
  switch (slot) {
    case kHintOrRetry: {
      if (over) {
        session_.retry();
        deselect();
        wrong_ = Move::none();
        hintSquare_ = chess::kNoSquare;
        return Action::pauseRepaint();      // the whole line goes back: a natural pause
      }
      const Move m = session_.hint();
      if (m.isNone() || (selected_ == m.from() && hintSquare_ == m.from())) return Action::none();
      select(m.from());
      hintSquare_ = m.from();
      return Action::repaint();
    }
    case kSolution:
      if (over || !session_.solutionStep()) return Action::none();
      deselect();
      wrong_ = Move::none();
      hintSquare_ = chess::kNoSquare;
      pendingArmed_ = false;
      ctx_.play(Sound::Move);
      return Action::pauseRepaint();
    case kSkipOrNext:
      session_.next();
      deselect();
      wrong_ = Move::none();
      hintSquare_ = chess::kNoSquare;
      pendingArmed_ = false;
      // Every square may change: a natural pause, where ghosting can be cleared.
      return Action::pauseRepaint();
    case kLevel:
      deselect();
      wrong_ = Move::none();
      hintSquare_ = chess::kNoSquare;
      mode_ = Mode::Picker;
      pickerFromBoard_ = true;
      return Action::repaint(Refresh::Full);
    case kMenu:
      deselect();
      wrong_ = Move::none();
      hintSquare_ = chess::kNoSquare;
      return Action::go(ScreenId::Menu);
    default:
      return Action::none();
  }
}

}  // namespace arrocco::ui
