// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — the client: one state machine over one Transport, driven by poll().
//
// It owns no board. The caller hands it the arrocco::chess::Game the UI already draws, and the
// client keeps that Game equal to the server's: every move on it comes from the "moves" string of
// a gameState, never from a guess. Our own moves are POSTed and then applied when the server
// echoes them back, so a move the server refuses cannot desynchronise the board — there is
// nothing to undo.
//
// What it copes with, because Lichess and a chess clock on Wi-Fi will do all of it:
//   * a game stream that dies mid-game: reopen, re-read "moves", replay only what is missing;
//   * more than 20 s of silence (the keep-alive is every ~7 s): treat as dead, reconnect;
//   * HTTP 429: stop asking for at least a minute, then retry the move that was refused;
//   * a move that cannot go out (slot busy, network down, request timed out): kept and sent again
//     a moment later, and dropped without a word once the board shows it was played after all,
//     so a slow network can neither lose a move nor play it twice;
//   * a move rejected by the server (not your turn, illegal): reported, board untouched;
//   * opponentGone, with the seconds after which claim-victory becomes possible;
//   * a stream that replays a game which is already over: seen as over, not as playable;
//   * a challenge to a friend that Lichess would let expire after 20 s: sent on a stream that
//     keeps it alive until the friend answers, when the transport can do that.
//
// No heap, no STL, no printf. About 9 KB of buffers, all members: put a LichessClient in a
// long-lived object, never on the 8 KB Arduino loop stack.
#pragma once
#include <stdint.h>

#include "arrocco/chess/game.h"
#include "arrocco/lichess/messages.h"
#include "arrocco/lichess/ndjson.h"
#include "arrocco/lichess/transport.h"

namespace arrocco::lichess {

enum class ClientState : uint8_t {
  Offline,      // begin() has not been called, or end() was
  Connecting,   // asking /api/account who we are
  Idle,         // account known, event stream open, no game
  Challenging,  // a challenge is out: waiting for the game to start
  Playing,      // following a game
  Finished,     // the game we followed is over; the event stream stays open
  Failed        // see lastError(); call begin() again to retry
};

const char* clientStateName(ClientState state);

// How the last request ended, for a UI that wants more than a string.
enum class ClientError : uint8_t { None, Network, Http, RateLimited, MoveRejected, Protocol, Desynchronised };

class LichessClient {
 public:
  // Lichess asks for at least a full minute after a 429; one second of margin.
  static constexpr uint32_t kRateLimitBackoffMs = 61000;
  // The keep-alive is every ~7 s, so 20 s of silence means the connection is gone.
  static constexpr uint32_t kSilenceTimeoutMs = 20000;
  static constexpr uint32_t kReconnectDelayMs = 2000;
  static constexpr uint32_t kRequestTimeoutMs = 20000;
  // A move that could not go out is sent again after this long (never in a tight loop: on the
  // simulator a dead proxy fails at once, and every poll would knock on it again).
  static constexpr uint32_t kMoveRetryDelayMs = 2000;
  // The event stream's gameFinish can overtake the game stream's last gameState (they are two
  // connections): the board then waits this long for that state, which carries the last move,
  // before it takes the end from the event alone.
  static constexpr uint32_t kFinishGraceMs = 5000;
  static constexpr int kMaxChallenges = 4;
  // A gameState line of a 680-ply game still fits; past that the line is dropped, reported
  // through lastError(), and the board is left alone rather than half-applied.
  static constexpr int kGameLineSize = 4096;
  static constexpr int kEventLineSize = 1536;
  // The first line of a kept-alive challenge is the challenge itself, the size of an event line.
  static constexpr int kChallengeLineSize = 1536;
  static constexpr int kResponseSize = 1024;

  LichessClient(Transport& transport, chess::Game& game);

  // ---------------------------------------------------------------- lifecycle
  void begin();  // verify the account, then open the event stream
  void end();    // close everything; the Game is left as it is
  void poll();   // call often (every UI tick); never blocks

  ClientState state() const { return state_; }
  ClientError error() const { return error_; }
  const char* lastError() const { return lastError_; }
  const char* username() const { return username_; }
  // Bumped on every change the user could see, so the UI knows when to present() once.
  uint32_t revision() const { return revision_; }
  bool busy() const { return pending_ != Pending::None; }
  // Milliseconds still to wait after a 429 (0 when not rate limited).
  uint32_t backoffRemainingMs() const;
  // The HTTP status of the last answer that was not a success (401 for a token Lichess no longer
  // knows, 404 for a user that does not exist), 0 after a success or when there was no answer.
  int lastHttpStatus() const { return httpStatus_; }

  // ---------------------------------------------------------------- starting a game
  // level 1..8, clock in seconds. UNRATED by definition. color: Color::White/Black, or
  // randomColor for the server to choose.
  // The clock must be 3+0 or slower (limit + 40 * increment >= 180 s): Lichess marks anything
  // faster as bullet, and the board API will then neither play it NOR abort it. Both calls
  // refuse a faster clock rather than create a game that cannot be ended.
  bool startAiGame(int level, int clockLimitSeconds, int clockIncrementSeconds, chess::Color color,
                   bool randomColor = false);
  // With a transport that supportsPostStreams() the challenge goes out on a stream that keeps it
  // alive (keepAliveStream=true) until the friend accepts or declines, or until
  // cancelOutgoing(); otherwise as a plain request, which Lichess expires after 20 s.
  bool challengeUser(const char* user, bool rated, int clockLimitSeconds, int clockIncrementSeconds,
                     chess::Color color, bool randomColor = true);
  // Withdraws the challenge we sent: closes its stream and, once its id is known, cancels it.
  void cancelOutgoing();
  // The id of the challenge we sent or accepted and whose game has not started yet ("" if none).
  const char* outgoingChallengeId() const { return pendingChallengeId_; }
  bool acceptChallenge(const char* id);
  bool declineChallenge(const char* id);
  bool cancelChallenge(const char* id);
  // Follow a game we already know about (an id from gameStart, or one we started).
  bool followGame(const char* id);
  // Whether a gameStart for an unknown game is followed by itself. On by default: it is how the
  // event stream's replay of an ongoing game resumes play after a reboot. Only real-time games
  // the Board API can play are followed that way; a correspondence game, or one whose event says
  // compat.board false, is left alone unless it is the one we challenged or accepted.
  void setAutoFollow(bool on) { autoFollow_ = on; }

  // ---------------------------------------------------------------- in the game
  bool sendMove(const char* uci);
  bool sendMove(chess::Move move);
  bool resign();
  bool abortGame();      // only legal before both sides have moved
  bool offerDraw();      // draw/yes
  bool declineDraw();    // draw/no
  bool claimVictory();   // after opponentGone

  bool inGame() const { return state_ == ClientState::Playing; }
  const char* gameId() const { return gameId_; }
  chess::Color ourColor() const { return ourColor_; }
  bool ourTurn() const;
  bool rated() const { return rated_; }
  const char* opponentName() const { return opponent_; }
  int opponentAiLevel() const { return opponentAiLevel_; }  // -1 = a human
  GameStatus status() const { return status_; }
  Winner winner() const { return winner_; }
  bool opponentGone() const { return opponentGone_; }
  int32_t claimWinInSeconds() const { return claimWinInSeconds_; }
  bool drawOfferedToUs() const;
  bool drawOfferedByUs() const;
  bool moveInFlight() const { return pending_ == Pending::Move; }
  // The move sent with sendMove() that the server has neither accepted nor refused yet: in
  // flight, or waiting to go out again. "" when there is none.
  const char* queuedMove() const { return queuedMove_; }
  // The game stream is open and has said something since it was (re)opened: the board is live.
  bool gameConnected() const { return gameStream_ != Transport::kNoStream && gameHeard_; }
  bool eventConnected() const { return eventStream_ != Transport::kNoStream && eventHeard_; }
  // Seconds left before claimVictory() is allowed, counted from the opponentGone message (0 when
  // it is allowed now, -1 when the opponent is not gone).
  int32_t claimWinRemainingSeconds() const;
  bool lastMoveRejected() const { return moveRejected_; }
  int plyCount() const { return appliedPlies_; }

  // Clocks. clockMs() is what the server last said; clockMsNow() counts the side to move down
  // from there, which is what a clock display wants between two gameStates.
  int32_t clockMs(chess::Color side) const { return side == chess::Color::White ? wtime_ : btime_; }
  int32_t incrementMs(chess::Color side) const { return side == chess::Color::White ? winc_ : binc_; }
  int32_t clockMsNow(chess::Color side) const;
  bool hasClock() const { return hasClock_; }

  // Pending challenges seen on the event stream (ours and other people's).
  int challengeCount() const { return challengeCount_; }
  const ChallengeInfo& challengeAt(int index) const;

  // For tests: the ndjson readers' overflow flags, and the raw transport.
  bool lineOverflow() const { return eventReader_.overflow() || gameReader_.overflow(); }

 private:
  enum class Pending : uint8_t {
    None, Account, ChallengeAi, ChallengeUser, ChallengeAction, AcceptChallenge, Move, Resign, Abort, DrawYes, DrawNo,
    ClaimVictory
  };

  bool startRequest(Pending kind, Method method, const char* path, const char* body);
  void pumpRequest(uint32_t now);
  void pumpEventStream(uint32_t now);
  void pumpGameStream(uint32_t now);
  enum class Lines : uint8_t { Event, Game, Challenge };
  void feedLines(NdjsonReader& reader, const char* data, int size, Lines kind);
  void pumpChallengeStream();
  void handleChallengeLine(const char* line, int length);
  void closeChallengeStream();
  void outgoingEnded(ClientError kind, const char* text);
  void handleResponse(Pending kind, int status, int length);
  void handleEventLine(const char* line, int length);
  void handleGameLine(const char* line, int length);
  void applyState(const GameStateInfo& info);
  void syncMoves(const char* moves, int length);
  void resetGameToInitial();
  void finishGame(GameStatus endStatus, Winner endWinner);
  void closeGameStream();
  void closeEventStream();
  void noteChallenge(const ChallengeInfo& info);
  void forgetChallenge(const char* id);
  void setError(ClientError kind, const char* text);
  void setTransportError(ClientError kind, const char* text);
  void clearError();
  void rateLimited(Pending kind);
  // A stream Lichess refused with 429 costs the same minute a refused request does. True when
  // that is what happened, so the caller stops calling it a network failure.
  bool streamRateLimited();
  void bump() { ++revision_; }
  bool inBackoff(uint32_t now) const;
  bool challengeActionRequest(const char* id, const char* action, Pending kind = Pending::ChallengeAction);
  void armMoveRetry();

  Transport& transport_;
  chess::Game& game_;

  ClientState state_ = ClientState::Offline;
  ClientError error_ = ClientError::None;
  Pending pending_ = Pending::None;
  uint32_t revision_ = 0;
  uint32_t requestStartMs_ = 0;
  uint32_t backoffStartMs_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t eventActivityMs_ = 0;
  uint32_t gameActivityMs_ = 0;
  uint32_t eventRetryMs_ = 0;   // when the event stream may be reopened
  uint32_t gameRetryMs_ = 0;
  bool eventRetryArmed_ = false;
  bool gameRetryArmed_ = false;
  bool eventHeard_ = false;     // a line (keep-alives included) arrived since the stream opened
  bool gameHeard_ = false;
  uint32_t moveRetryMs_ = 0;    // when a queued move that failed may go out again
  bool moveRetryArmed_ = false;
  int queuedAtPly_ = 0;         // the plies on the board when the queued move was sent
  int httpStatus_ = 0;
  uint32_t goneAtMs_ = 0;       // when the opponentGone message arrived
  bool finishSeen_ = false;     // gameFinish arrived while the game stream was still talking
  uint32_t finishSeenMs_ = 0;
  GameStatus finishStatus_ = GameStatus::Unknown;
  Winner finishWinner_ = Winner::None;

  int eventStream_ = Transport::kNoStream;
  int gameStream_ = Transport::kNoStream;
  int challengeStream_ = Transport::kNoStream;  // our kept-alive challenge, while it waits
  bool challengeAnswered_ = false;              // its {"done":...} line arrived
  bool cancelWanted_ = false;                   // withdrawn before its id was known: cancel on arrival
  bool challengeHeard_ = false;                 // its first line came: Lichess took the challenge
  // Declared before the readers on purpose: NdjsonReader's constructor already writes into them.
  char eventLineBuffer_[kEventLineSize] = {};
  char gameLineBuffer_[kGameLineSize] = {};
  char challengeLineBuffer_[kChallengeLineSize] = {};
  NdjsonReader eventReader_;
  NdjsonReader gameReader_;
  NdjsonReader challengeReader_;

  char username_[kNameSize] = {};
  char gameId_[kGameIdSize] = {};
  char opponent_[kNameSize] = {};
  char initialFen_[chess::kFenBufferSize] = {};
  char pendingChallengeId_[kChallengeIdSize] = {};
  char queuedMove_[chess::kUciBufferSize] = {};  // a move a 429 refused: sent again after the backoff
  char lastError_[kErrorSize] = {};

  chess::Color ourColor_ = chess::Color::White;
  GameStatus status_ = GameStatus::Unknown;
  Winner winner_ = Winner::None;
  int opponentAiLevel_ = -1;
  int appliedPlies_ = 0;
  int32_t wtime_ = 0, btime_ = 0, winc_ = 0, binc_ = 0;
  uint32_t clockAtMs_ = 0;
  bool hasClock_ = false;
  bool rated_ = false;
  bool autoFollow_ = true;
  bool opponentGone_ = false;
  int32_t claimWinInSeconds_ = -1;
  bool whiteDrawOffer_ = false, blackDrawOffer_ = false;
  bool moveRejected_ = false;
  bool desynchronised_ = false;

  int challengeCount_ = 0;
  ChallengeInfo challenges_[kMaxChallenges];

  char path_[96] = {};
  char body_[192] = {};
  char response_[kResponseSize] = {};
  char chunk_[512] = {};
};

}  // namespace arrocco::lichess
