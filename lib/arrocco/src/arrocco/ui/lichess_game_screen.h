// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the online game: the board and the side panel of the offline game, with the
// server deciding what is on the board.
//
// A tapped move goes to Lichess and the board does NOT play it: it shows the move as sent (a
// frame on the piece, a dot where it goes, "Sending Nf3...") until the server's game state comes
// back with it, and only then moves the piece. The client (lichess/client.h) keeps that move
// until Lichess accepts or refuses it, sends it again after a 429, a busy slot or a network
// failure, and drops it the moment the board shows it was played after all: a slow network can
// neither lose it nor play it twice. While a move is out, the board takes no other.
//
// The opponent's moves arrive on the game stream, one refresh each. The clocks are the server's,
// counted down here between two updates with the offline clock's cadence (every 10 s, every
// second below 20 s). A stream that drops is reopened by the client; after 3 s without it the
// panel says "Reconnecting...", and the board catches up from the gameFull that comes back.
//
// Buttons: Abort (until both sides have moved) or Resign, both asked first; Offer / Accept and
// Decline draw against a person; Claim win when the opponent left and the wait is over; Flip;
// Menu, which goes to the Lichess menu while the game goes on (its "Back to the game" returns).
// The end: the result over the board with New game (another game like this one), Review and
// Lichess; Deep refreshes at the start and at the end, as offline.
#pragma once
#include <cstdint>

#include "arrocco/chess/move.h"
#include "arrocco/chess/types.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class LichessState;

class LichessGameScreen final : public Screen {
 public:
  explicit LichessGameScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;

  // For the firmware's touch policy and tick pacing (ChessApp's accessors).
  bool popupOpen() const { return mode_ == Mode::Promotion || mode_ == Mode::Confirm || mode_ == Mode::Over; }
  bool clockTicking() const;
  // For the tests: "play", "promotion", "confirm", "over" or "review".
  const char* modeName() const;

 private:
  enum class Mode : uint8_t { Play, Promotion, Confirm, Over, Review };
  enum class Ask : uint8_t { Resign, Abort };

  // What the side panel says, worked out once for draw() and for onTick()'s "has it changed?".
  struct View {
    char headline[40] = {};
    char subline[72] = {};
    char names[2][24] = {};
    const char* buttons[6] = {};
    uint32_t seconds[2] = {0, 0};
    bool running[2] = {false, false};
    bool clock = false;
  };

  LichessState& st() const;
  void compose(View& view, Adafruit_GFX* gfx) const;
  uint32_t hashOf(const View& view) const;
  void snapshot();
  void deselect();
  bool pending() const;            // a move of ours is out and the board has not shown it yet
  Action onSquare(chess::Square s);
  Action send(chess::Move m);
  Action onButton(int slot);
  Action onPromotionTap(int16_t x, int16_t y);
  Action onConfirmTap(int16_t x, int16_t y);
  Action onOverTap(int16_t x, int16_t y);
  Action onReviewTap(int16_t x, int16_t y);
  Action toLichess(bool again);
  void noteOpponent();
  const char* resultText() const;
  const char* reasonText() const;
  bool timeTrouble() const;        // our clock under a minute: no Full refresh, even at a pause

  Context& ctx_;
  Mode mode_ = Mode::Play;
  Ask ask_ = Ask::Resign;
  bool flipped_ = false;
  chess::Square selected_ = chess::kNoSquare;
  chess::SquareSet targets_;
  chess::Square promotionFrom_ = chess::kNoSquare;
  chess::Square promotionTo_ = chess::kNoSquare;
  chess::Move sent_ = chess::Move::none();   // the move we sent, shown until the board has it
  int sentAtPly_ = -1;
  char refused_[72] = {};          // why the last move was refused, until the next one
  uint32_t shownHash_ = 0;
  int shownPlies_ = 0;
  uint32_t shownRevision_ = 0;
  uint32_t lostSinceMs_ = 0;       // the game stream went quiet at this time (0 = it is fine)
  bool reconnecting_ = false;      // ... for more than kReconnectNoteMs: the panel says so
  bool opponentNoted_ = false;
  char shownGameId_[16] = {};      // the game the orientation belongs to: Menu and back keep a Flip
  chess::Color orientedFor_ = chess::Color::White;  // the colour the board was turned for
  bool userFlipped_ = false;       // Flip was tapped: the board stays as the user turned it
};

}  // namespace arrocco::ui
