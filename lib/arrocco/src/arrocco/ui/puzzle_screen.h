// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the puzzle screen: the offline Lichess puzzles, with a rating of the
// solver's own that picks them (puzzle_progress.h) and a session that plays them
// (puzzle_session.h). Two faces on one ScreenId:
//
//   the level picker  the first visit (nothing stored yet) and the "Level" button: four
//                     starting levels, a full screen in the style of the menus;
//   the board         the puzzle seen from the solver's side, and the side column: whose
//                     move it is and what just happened, the puzzle's theme, rating and
//                     Lichess id, the solver's rating and counts, and the buttons
//                     Hint / Solution, Skip (Next once the puzzle is over) / Level, Menu,
//                     with Retry in place of Hint once the line has been played.
//
// The e-paper rhythm is the game's: one refresh per tap. A tap on a piece draws the
// selection frame and the legal-move dots; a move is one refresh, and the opponent's
// move after it is another, kPuzzleOpponentDelayMs after the first is on the glass, like
// the engine's answer in a game. A new puzzle appears the same way: first the position
// Lichess published, then the opponent's blunder, marked as a last move. A tap while the
// opponent is about to move makes it move at once (and does nothing else).
//
// A wrong move is not played: the board stays, an X marks the square the piece was sent
// to, and the side column says so. Hint selects the piece that has to move, so that the
// dots show where it can go. Solution plays the next move of the line for the solver,
// one move per tap. The score of every puzzle and what to keep are in puzzle_session.h.
//
// Power cuts: ChessApp calls restore() at boot and persist() after every present(). The
// progress (rating, counts, the walks through the pack, the puzzle on the board and how
// far it got) is written when it changed, after the refresh that shows the change, under
// its own key. Nothing is stored before a level has been picked.
#pragma once
#include <cstddef>
#include <cstdint>

#include "arrocco/chess/types.h"
#include "arrocco/ui/puzzle_progress.h"
#include "arrocco/ui/puzzle_session.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class PuzzleScreen final : public Screen {
 public:
  explicit PuzzleScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;

  // ---- for ChessApp
  void restore();                          // begin(): the progress kept in the store
  // After every present(): writes the progress if it changed. screenIsCurrent: this screen
  // is the one on the glass, which the progress keeps for a wake from sleep (wasOnScreen()).
  void persist(bool screenIsCurrent);
  // The puzzle board is what this screen shows (not the level picker).
  bool showsBoard() const { return mode_ != Mode::Picker && session_.loaded(); }
  // restore() found that this screen was on the glass at the last refresh before the board
  // went off: a wake from a sleep that kept a board in view comes back here, not to the menu.
  bool wasOnScreen() const { return wasOnScreen_; }

  // For tests and the simulator.
  const PuzzleSession& session() const { return session_; }

 private:
  enum class Mode : uint8_t { Picker, Board, Promotion };
  // The side buttons, in the game screen's grid (layout.h): slot = row * 2 + column.
  enum Button : int { kHintOrRetry = 0, kSolution = 1, kSkipOrNext = 2, kLevel = 3, kMenu = 4 };

  bool flipped() const;
  void select(chess::Square s);
  void deselect();
  Action onPickerTap(int16_t x, int16_t y);
  Action onBoardTap(int16_t x, int16_t y);
  Action onPromotionTap(int16_t x, int16_t y);
  Action onSquare(chess::Square s);
  Action onButton(int slot);
  Action tryMove(chess::Move m);
  Action opponentMoves();               // the pending opponent move goes on the board
  void drawPicker(Adafruit_GFX& gfx);
  void drawBoardScreen(Adafruit_GFX& gfx);
  void drawSideColumn(Adafruit_GFX& gfx);
  void wrongBeep();

  Context& ctx_;
  PuzzleSession session_;
  Mode mode_ = Mode::Picker;
  bool pickerFromBoard_ = false;        // the picker was opened with "Level": Back returns to the board
  chess::Square selected_ = chess::kNoSquare;
  chess::SquareSet targets_;
  chess::Square hintSquare_ = chess::kNoSquare;   // the piece the hint selected
  chess::Move wrong_;                   // the wrong try on the glass, X-marked
  chess::Square promotionFrom_ = chess::kNoSquare;
  chess::Square promotionTo_ = chess::kNoSquare;
  bool pendingArmed_ = false;           // the refresh before the opponent's move is over
  uint32_t pendingDueMs_ = 0;           // ... and the move appears at this time

  // What the store holds, and a buffer to encode into: members, the loop task has 8 KB.
  uint8_t stored_[kPuzzleProgressBytes] = {};
  size_t storedSize_ = 0;
  uint8_t scratch_[kPuzzleProgressBytes] = {};
  bool wasOnScreen_ = false;
};

}  // namespace arrocco::ui
