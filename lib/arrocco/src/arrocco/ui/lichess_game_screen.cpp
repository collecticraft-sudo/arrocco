// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the online game. See lichess_game_screen.h.
#include "arrocco/ui/lichess_game_screen.h"

#include <cstring>

#include <Adafruit_GFX.h>

#include "arrocco/ui/board_view.h"
#include "arrocco/ui/layout.h"
#include "arrocco/ui/lichess_page.h"
#include "arrocco/ui/lichess_state.h"
#include "arrocco/ui/side_panel.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

using chess::Color;
using chess::Move;
using chess::Square;
using lichess::ClientState;
using lichess::GameStatus;
using lichess::Winner;

// The side buttons: [end] [draw] / [draw 2] [Flip] / [ ] [Menu].
enum Slot : int { kSlotEnd = 0, kSlotDraw, kSlotDraw2, kSlotFlip, kSlotFree, kSlotMenu };
// In review: < > / Flip Result / Play again Lichess, like the offline game-over screen.
enum ReviewSlot : int { kRevPrev = 0, kRevNext, kRevFlip, kRevResult, kRevAgain, kRevLichess };

constexpr uint32_t kReconnectNoteMs = 3000;   // a stream back within this is not worth a word
constexpr int16_t kNameWidth = 120;           // a clock label: the column is 144 px, the arrow after it

uint32_t mix(uint32_t h, const char* text) {
  if (text == nullptr) return (h ^ 0x01u) * 16777619u;
  for (; *text != '\0'; ++text) h = (h ^ static_cast<uint8_t>(*text)) * 16777619u;
  return (h ^ 0xFFu) * 16777619u;
}

uint32_t mix(uint32_t h, uint32_t value) {
  for (int i = 0; i < 4; ++i) h = (h ^ ((value >> (8 * i)) & 0xFFu)) * 16777619u;
  return h;
}

// The game-over popup: the offline one's box and buttons, with the online labels.
Dialog overDialog(const char* result, const char* reason) {
  Dialog d = gameOverDialog(result, reason);
  d.labels[0] = str::kButtonNewGame;   // another game like this one
  d.labels[1] = str::kGameOverReview;
  d.labels[2] = str::kLichessTitle;
  return d;
}

// "Resign the game?" / "Abort the game?": the new-game question's box over the board.
Dialog askDialog(bool abort) {
  Dialog d = newGameDialog(kBoardRect.cx());
  d.title = abort ? str::kOnlineAbortTitle : str::kOnlineResignTitle;
  d.subtitle = str::kOnlineEndsForGood;
  d.labels[0] = abort ? str::kOnlineAbort : str::kOnlineResign;
  d.labels[1] = str::kCancel;
  return d;
}

}  // namespace

LichessState& LichessGameScreen::st() const { return *ctx_.lichess; }

const char* LichessGameScreen::modeName() const {
  switch (mode_) {
    case Mode::Play:      return "play";
    case Mode::Promotion: return "promotion";
    case Mode::Confirm:   return "confirm";
    case Mode::Over:      return "over";
    case Mode::Review:    return "review";
  }
  return "?";
}

void LichessGameScreen::enter() {
  LichessState& s = st();
  s.game.goToLatest();
  deselect();
  sent_ = Move::none();
  sentAtPly_ = -1;
  refused_[0] = '\0';
  lostSinceMs_ = 0;
  reconnecting_ = false;
  opponentNoted_ = false;
  mode_ = s.client.state() == ClientState::Finished ? Mode::Over : Mode::Play;
  // Our side at the bottom, unless the owner asked for the board the other way round.
  flipped_ = (s.client.ourColor() == Color::Black) != ctx_.settings.flipByDefault;
  noteOpponent();
  snapshot();
}

void LichessGameScreen::deselect() {
  selected_ = chess::kNoSquare;
  targets_.clear();
}

bool LichessGameScreen::pending() const {
  return !sent_.isNone() && st().game.plyCount() == sentAtPly_;
}

bool LichessGameScreen::clockTicking() const {
  const lichess::LichessClient& c = st().client;
  return c.inGame() && c.hasClock() && st().game.plyCount() >= 2;
}

bool LichessGameScreen::timeTrouble() const {
  const lichess::LichessClient& c = st().client;
  return c.hasClock() && c.clockMsNow(c.ourColor()) < static_cast<int32_t>(kNoFullBelowClockMs);
}

// The opponent of a game against a person goes on the recent list (written from onTick), and is
// who "Play again" challenges.
void LichessGameScreen::noteOpponent() {
  LichessState& s = st();
  if (opponentNoted_) return;
  const char* name = s.client.opponentName();
  if (s.client.opponentAiLevel() >= 0) {
    if (name[0] == '\0') return;    // the gameFull has not said yet
    s.lastOpponent[0] = '\0';
    opponentNoted_ = true;
    return;
  }
  if (!validUsername(name)) return;
  formatText(s.lastOpponent, sizeof s.lastOpponent, "%s", name);
  s.rememberOpponent(name);
  opponentNoted_ = true;
}

// ---- what the panel says ------------------------------------------------------------------------

const char* LichessGameScreen::resultText() const {
  const lichess::LichessClient& c = st().client;
  if (c.status() == GameStatus::Aborted) return str::kOnlineAborted;
  if (c.winner() == Winner::None) return str::kDraw;
  const bool weWon = (c.winner() == Winner::White) == (c.ourColor() == Color::White);
  return weWon ? str::kOnlineYouWon : str::kOnlineYouLost;
}

const char* LichessGameScreen::reasonText() const {
  const LichessState& s = st();
  switch (s.client.status()) {
    case GameStatus::Mate:       return str::kByCheckmate;
    case GameStatus::Resign:     return str::kByResignation;
    case GameStatus::Stalemate:  return str::kByStalemate;
    case GameStatus::Outoftime:
    case GameStatus::NoStart:    return str::kByTimeout;
    case GameStatus::Timeout:    return str::kOnlineByGone;
    case GameStatus::Aborted:    return "";
    case GameStatus::Draw: {
      // Lichess says "draw" for every draw; the board can tell the ones the rules made.
      const chess::Position& pos = s.game.position();
      if (s.game.repetitionCount() >= 3) return str::kByRepetition;
      if (pos.isFiftyMoveDraw()) return str::kByFiftyMove;
      if (pos.isInsufficientMaterial()) return str::kByMaterial;
      return str::kByAgreement;
    }
    default:                     return str::kOnlineByLichess;
  }
}

void LichessGameScreen::compose(View& v, Adafruit_GFX* gfx) const {
  const LichessState& s = st();
  const lichess::LichessClient& c = s.client;
  const chess::Game& game = s.game;
  const chess::Position& pos = game.position();
  const Color us = c.ourColor();
  const bool human = c.opponentAiLevel() < 0;

  // Who is who, over the clocks.
  char opponent[24];
  if (!human) {
    formatText(opponent, sizeof opponent, str::kOnlineStockfishFmt, c.opponentAiLevel());
  } else if (gfx != nullptr) {
    fitText(*gfx, Font::Sans9, c.opponentName(), kNameWidth, opponent, sizeof opponent);
  } else {
    formatText(opponent, sizeof opponent, "%s", c.opponentName());
  }
  formatText(v.names[chess::indexOf(us)], sizeof v.names[0], "%s", str::kOnlineYou);
  formatText(v.names[chess::indexOf(chess::opposite(us))], sizeof v.names[0], "%s", opponent);

  const bool over = mode_ == Mode::Over || mode_ == Mode::Review || c.state() == ClientState::Finished;
  if (over) {
    formatText(v.headline, sizeof v.headline, "%s", resultText());
    if (mode_ == Mode::Review) {
      char shown[16];
      if (game.currentPly() == 0) {
        formatText(v.subline, sizeof v.subline, str::kReviewStartFmt, game.plyCount());
      } else {
        formatMove(game, game.currentPly() - 1, shown, sizeof shown);
        formatText(v.subline, sizeof v.subline, str::kReviewFmt, shown, game.currentPly(), game.plyCount());
      }
    } else {
      formatText(v.subline, sizeof v.subline, "%s", reasonText());
    }
  } else {
    // Whose move: ours, or the opponent's by name when the name fits the headline.
    if (c.ourTurn()) {
      formatText(v.headline, sizeof v.headline, "%s", str::kOnlineYourMove);
    } else {
      formatText(v.headline, sizeof v.headline, str::kOnlineToMoveFmt, opponent);
      if (gfx != nullptr && textWidth(*gfx, Font::Bold18, v.headline) > kSideInnerW)
        formatText(v.headline, sizeof v.headline, "%s", str::kOnlineTheirMove);
    }

    // The subline: the most urgent thing first.
    char san[chess::kSanBufferSize + 4] = {};
    if (pending()) pos.toSan(sent_, san);
    const uint32_t wait = c.backoffRemainingMs();
    const int32_t claim = c.claimWinRemainingSeconds();
    if (pending() && wait > 0) {
      formatText(v.subline, sizeof v.subline, str::kOnlineWaitFmt, san, waitTensOfSeconds(wait));
    } else if (pending()) {
      formatText(v.subline, sizeof v.subline, str::kOnlineSendingFmt, san);
    } else if (refused_[0] != '\0') {
      formatText(v.subline, sizeof v.subline, str::kOnlineRefusedFmt, refused_);
    } else if (reconnecting_) {
      formatText(v.subline, sizeof v.subline, "%s", str::kOnlineReconnecting);
    } else if (c.error() == lichess::ClientError::Desynchronised) {
      formatText(v.subline, sizeof v.subline, "%s", c.lastError());
    } else if (c.drawOfferedToUs()) {
      formatText(v.subline, sizeof v.subline, "%s", str::kOnlineDrawToUs);
    } else if (c.drawOfferedByUs()) {
      formatText(v.subline, sizeof v.subline, "%s", str::kOnlineDrawByUs);
    } else if (claim > 0) {
      // In steps of ten seconds: a countdown repainted every second would be thirty refreshes.
      formatText(v.subline, sizeof v.subline, str::kOnlineGoneFmt, static_cast<int>((claim + 9) / 10) * 10);
    } else if (claim == 0) {
      formatText(v.subline, sizeof v.subline, "%s", str::kOnlineGoneNow);
    } else if (game.currentPly() == 0) {
      formatText(v.subline, sizeof v.subline, "%s", str::kNoMovesYet);
    } else {
      char last[32];
      formatMove(game, game.currentPly() - 1, last, 20);
      if (pos.inCheck()) {
        const size_t used = std::strlen(last);
        formatText(last + used, sizeof last - used, "  %s", str::kCheck);
      }
      formatText(v.subline, sizeof v.subline, "%s", last);
    }
  }

  // The server's clocks, counted down here between two of its updates.
  v.clock = c.hasClock();
  if (v.clock) {
    for (int side = 0; side < 2; ++side) {
      const Color colour = static_cast<Color>(side);
      const int32_t ms = c.clockMsNow(colour);
      v.seconds[side] = GameClock::shownSeconds(ms > 0 ? static_cast<uint32_t>(ms) : 0u);
      v.running[side] = !over && c.inGame() && game.plyCount() >= 2 && pos.sideToMove() == colour;
    }
  }

  // The buttons.
  if (mode_ == Mode::Review) {
    v.buttons[kRevPrev] = str::kButtonPrev;
    v.buttons[kRevNext] = str::kButtonNext;
    v.buttons[kRevFlip] = str::kButtonFlip;
    v.buttons[kRevResult] = str::kButtonResult;
    v.buttons[kRevAgain] = str::kButtonNewGame;
    v.buttons[kRevLichess] = str::kLichessTitle;
    return;
  }
  if (over) return;   // the popup has the buttons
  v.buttons[kSlotEnd] = game.plyCount() < 2 ? str::kOnlineAbort : str::kOnlineResign;
  if (human) {
    if (c.drawOfferedToUs()) {
      v.buttons[kSlotDraw] = str::kOnlineAcceptDraw;
      v.buttons[kSlotDraw2] = str::kOnlineDeclineDraw;
    } else if (!c.drawOfferedByUs() && game.plyCount() >= 2) {
      v.buttons[kSlotDraw] = str::kOnlineOfferDraw;
    }
  }
  if (c.claimWinRemainingSeconds() == 0) v.buttons[kSlotDraw2] = str::kOnlineClaimWin;
  v.buttons[kSlotFlip] = str::kButtonFlip;
  v.buttons[kSlotMenu] = str::kButtonMenu;
}

uint32_t LichessGameScreen::hashOf(const View& v) const {
  uint32_t h = 2166136261u;
  h = mix(h, v.headline);
  h = mix(h, v.subline);
  h = mix(h, v.names[0]);
  h = mix(h, v.names[1]);
  for (const char* b : v.buttons) h = mix(h, b);
  h = mix(h, v.seconds[0]);
  h = mix(h, v.seconds[1]);
  h = mix(h, static_cast<uint32_t>((v.running[0] ? 1u : 0u) | (v.running[1] ? 2u : 0u) | (v.clock ? 4u : 0u)));
  h = mix(h, static_cast<uint32_t>(mode_));
  return h;
}

void LichessGameScreen::snapshot() {
  View v;
  compose(v, &ctx_.platform.gfx());
  shownHash_ = hashOf(v);
  shownPlies_ = st().game.plyCount();
  shownRevision_ = st().client.revision();
}

// ---- drawing ----------------------------------------------------------------------------------------------

void LichessGameScreen::draw(Adafruit_GFX& gfx) {
  const LichessState& s = st();
  const chess::Game& game = s.game;
  const chess::Position& pos = game.position();

  BoardMarks marks;
  marks.selected = selected_;
  marks.targets = targets_;
  if (pending()) {
    // The move on its way: its piece framed, a dot (or a ring) where it is going.
    marks.selected = sent_.from();
    marks.targets.clear();
    marks.targets.add(sent_.to());
  }
  const Move last = game.lastMove();
  if (!last.isNone()) {
    marks.lastFrom = last.from();
    marks.lastTo = last.to();
  }
  marks.checkedKing = pos.checkedKingSquare();
  drawBoard(gfx, pos, marks, flipped_);

  View v;
  compose(v, &gfx);
  SidePanelView panel;
  panel.headline = v.headline;
  panel.subline = v.subline;
  panel.clock.shown = v.clock;
  for (int side = 0; side < 2; ++side) {
    panel.clock.seconds[side] = v.seconds[side];
    panel.clock.running[side] = v.running[side];
    panel.clockLabels[side] = v.names[side];
  }
  for (int slot = 0; slot < kSideButtonSlots; ++slot) panel.buttons[slot] = v.buttons[slot];
  drawSidePanel(gfx, game, panel);

  if (mode_ == Mode::Promotion) drawPromotionPopup(gfx, pos.sideToMove());
  if (mode_ == Mode::Confirm) drawDialog(gfx, askDialog(ask_ == Ask::Abort));
  if (mode_ == Mode::Over) drawDialog(gfx, overDialog(resultText(), reasonText()));
}

// ---- ticks: what the server says --------------------------------------------------------------------------

Action LichessGameScreen::onTick(uint32_t now) {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  s.saveFriends(ctx_.platform);
  noteOpponent();

  const ClientState state = c.state();
  if (state != ClientState::Playing && state != ClientState::Finished) {
    // The session went away under the game (the Lichess menu ended it): nothing to show here.
    return Action::go(ScreenId::Lichess);
  }
  if (mode_ == Mode::Over || mode_ == Mode::Review) return Action::none();

  if (state == ClientState::Finished) {
    mode_ = Mode::Over;
    deselect();
    sent_ = Move::none();
    s.game.goToLatest();
    ctx_.play(Sound::GameOver);
    snapshot();
    return Action::repaint(Refresh::Deep);   // game end: the one moment a long refresh is welcome
  }

  // The connection, as the panel tells it: only a drop that lasts is news.
  if (c.gameConnected()) {
    lostSinceMs_ = 0;
    reconnecting_ = false;
  } else if (lostSinceMs_ == 0) {
    lostSinceMs_ = now == 0 ? 1 : now;
  } else if (now - lostSinceMs_ >= kReconnectNoteMs) {
    reconnecting_ = true;
  }

  // The move we sent: refused by the server, or played (the board moved on).
  if (!sent_.isNone()) {
    if (s.game.plyCount() != sentAtPly_) {
      sent_ = Move::none();
    } else if (c.lastMoveRejected()) {
      formatText(refused_, sizeof refused_, "%s", c.lastError());
      sent_ = Move::none();
    }
  }

  const int plies = s.game.plyCount();
  View v;
  compose(v, &ctx_.platform.gfx());
  const uint32_t hash = hashOf(v);
  if (plies != shownPlies_) {
    // A move reached the board: the opponent's, or ours confirmed. A promotion popup or a
    // selection made on the position before it means nothing any more.
    if (mode_ == Mode::Promotion) mode_ = Mode::Play;
    deselect();
    refused_[0] = '\0';
    ctx_.play(Sound::Move);
    shownHash_ = hashOf(v);
    shownPlies_ = plies;
    shownRevision_ = c.revision();
    return timeTrouble() ? Action::repaint() : Action::pauseRepaint();
  }
  if (hash != shownHash_) {
    shownHash_ = hash;
    shownRevision_ = c.revision();
    return Action::repaint();
  }
  return Action::none();
}

// ---- taps -------------------------------------------------------------------------------------------------------

Action LichessGameScreen::onTap(int16_t x, int16_t y) {
  switch (mode_) {
    case Mode::Promotion: return onPromotionTap(x, y);
    case Mode::Confirm:   return onConfirmTap(x, y);
    case Mode::Over:      return onOverTap(x, y);
    case Mode::Review:    return onReviewTap(x, y);
    case Mode::Play:      break;
  }
  const Square sq = squareAt(x, y, flipped_);
  if (sq != chess::kNoSquare) return onSquare(sq);
  const int slot = sideButtonAt(x, y);
  if (slot >= 0) return onButton(slot);
  return Action::none();
}

Action LichessGameScreen::onSquare(Square sq) {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  // The board takes a move only when it is ours to make and no other is on its way.
  if (!c.ourTurn() || pending() || c.queuedMove()[0] != '\0') return Action::none();
  const chess::Position& pos = s.game.position();
  const chess::Piece piece = pos.pieceAt(sq);
  const bool own = !piece.isNone() && piece.color() == pos.sideToMove();

  if (selected_ == chess::kNoSquare) {
    if (!own) return Action::none();
    selected_ = sq;
    targets_ = pos.legalTargetsFrom(sq);
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  if (sq == selected_) {
    deselect();
    return Action::repaint();
  }
  if (targets_.contains(sq)) {
    if (pos.isPromotionMove(selected_, sq)) {
      promotionFrom_ = selected_;
      promotionTo_ = sq;
      mode_ = Mode::Promotion;
      return Action::repaint();
    }
    return send(pos.findLegalMove(selected_, sq));
  }
  if (own) {
    selected_ = sq;
    targets_ = pos.legalTargetsFrom(sq);
    ctx_.play(Sound::Select);
    return Action::repaint();
  }
  deselect();
  return Action::repaint();
}

// One refresh: the move shown as sent. The board moves when the server says so (onTick).
Action LichessGameScreen::send(Move m) {
  LichessState& s = st();
  deselect();
  mode_ = Mode::Play;
  if (m.isNone()) return Action::repaint();
  refused_[0] = '\0';
  if (!s.client.sendMove(m)) {
    formatText(refused_, sizeof refused_, "%s", s.client.lastError());
    return Action::repaint();
  }
  sent_ = m;
  sentAtPly_ = s.game.plyCount();
  ctx_.play(Sound::Select);
  snapshot();
  return Action::repaint();
}

Action LichessGameScreen::onPromotionTap(int16_t x, int16_t y) {
  const int choice = promotionChoiceAt(x, y);
  if (choice < 0) {
    mode_ = Mode::Play;
    deselect();
    return Action::repaint();
  }
  const Move m = st().game.position().findLegalMove(promotionFrom_, promotionTo_, promotionPiece(choice));
  return send(m);
}

Action LichessGameScreen::onButton(int slot) {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  View v;
  compose(v, &ctx_.platform.gfx());
  if (slot < 0 || slot >= kSideButtonSlots || v.buttons[slot] == nullptr) return Action::none();
  const char* label = v.buttons[slot];
  if (label == str::kOnlineResign || label == str::kOnlineAbort) {
    ask_ = label == str::kOnlineAbort ? Ask::Abort : Ask::Resign;
    deselect();
    mode_ = Mode::Confirm;
    return Action::repaint();
  }
  bool sent = true;
  if (label == str::kOnlineOfferDraw || label == str::kOnlineAcceptDraw) sent = c.offerDraw();
  else if (label == str::kOnlineDeclineDraw) sent = c.declineDraw();
  else if (label == str::kOnlineClaimWin) sent = c.claimVictory();
  else if (label == str::kButtonFlip) {
    flipped_ = !flipped_;
    return Action::repaint(Refresh::Full);   // every square changes: one flash, no ghost
  } else if (label == str::kButtonMenu) {
    deselect();
    return toLichess(false);
  } else {
    return Action::none();
  }
  if (!sent) formatText(refused_, sizeof refused_, "%s", c.lastError());
  snapshot();
  return Action::repaint();
}

Action LichessGameScreen::onConfirmTap(int16_t x, int16_t y) {
  LichessState& s = st();
  const int button = dialogButtonAt(askDialog(ask_ == Ask::Abort), x, y);
  mode_ = Mode::Play;
  if (button == 0) {
    // The end comes back on the game stream, like everything else that changes the game.
    const bool sent = ask_ == Ask::Abort ? s.client.abortGame() : s.client.resign();
    if (!sent) formatText(refused_, sizeof refused_, "%s", s.client.lastError());
  }
  snapshot();
  return Action::repaint();
}

Action LichessGameScreen::onOverTap(int16_t x, int16_t y) {
  switch (dialogButtonAt(overDialog(resultText(), reasonText()), x, y)) {
    case 0: return toLichess(true);
    case 1:
      mode_ = Mode::Review;
      snapshot();
      return Action::repaint();
    case 2: return toLichess(false);
    default: return Action::none();
  }
}

Action LichessGameScreen::onReviewTap(int16_t x, int16_t y) {
  chess::Game& game = st().game;
  switch (sideButtonAt(x, y)) {
    case kRevPrev:    return game.stepBack() ? Action::pauseRepaint() : Action::none();
    case kRevNext:    return game.stepForward() ? Action::pauseRepaint() : Action::none();
    case kRevFlip:    flipped_ = !flipped_; return Action::repaint(Refresh::Full);
    case kRevResult:
      game.goToLatest();
      mode_ = Mode::Over;
      snapshot();
      return Action::repaint();
    case kRevAgain:   return toLichess(true);
    case kRevLichess: return toLichess(false);
    default:          return Action::none();
  }
}

Action LichessGameScreen::toLichess(bool again) {
  LichessState& s = st();
  s.game.goToLatest();
  if (again) s.playAgain = true;
  return Action::go(ScreenId::Lichess);
}

}  // namespace arrocco::ui
