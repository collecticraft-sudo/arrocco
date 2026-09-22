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
//   * a move rejected by the server (not your turn, illegal): reported, board untouched;
//   * opponentGone, with the seconds after which claim-victory becomes possible;
//   * a stream that replays a game which is already over: seen as over, not as playable.
//
// No heap, no STL, no printf. About 7 KB of buffers, all members: put a LichessClient in a
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
  static constexpr int kMaxChallenges = 4;
  // A gameState line of a 680-ply game still fits; past that the line is dropped, reported
  // through lastError(), and the board is left alone rather than half-applied.
  static constexpr int kGameLineSize = 4096;
  static constexpr int kEventLineSize = 1536;
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

  // ---------------------------------------------------------------- starting a game
  // level 1..8, clock in seconds. UNRATED by definition. color: Color::White/Black, or
  // randomColor for the server to choose.
  // The clock must be 3+0 or slower (limit + 40 * increment >= 180 s): Lichess marks anything
  // faster as bullet, and the board API will then neither play it NOR abort it. Both calls
  // refuse a faster clock rather than create a game that cannot be ended.
  bool startAiGame(int level, int clockLimitSeconds, int clockIncrementSeconds, chess::Color color,
                   bool randomColor = false);
  bool challengeUser(const char* user, bool rated, int clockLimitSeconds, int clockIncrementSeconds,
                     chess::Color color, bool randomColor = true);
  bool acceptChallenge(const char* id);
  bool declineChallenge(const char* id);
  bool cancelChallenge(const char* id);
  // Follow a game we already know about (an id from gameStart, or one we started).
  bool followGame(const char* id);
  // Whether a gameStart for an unknown game is followed by itself. On by default: it is how the
  // event stream's replay of an ongoing game resumes play after a reboot.
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
  bool moveInFlight() const { return pending_ == Pending::Move; }
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
    None, Account, ChallengeAi, ChallengeUser, ChallengeAction, Move, Resign, Abort, DrawYes, DrawNo, ClaimVictory
  };

  bool startRequest(Pending kind, Method method, const char* path, const char* body);
  void pumpRequest(uint32_t now);
  void pumpEventStream(uint32_t now);
  void pumpGameStream(uint32_t now);
  void feedLines(NdjsonReader& reader, const char* data, int size, bool isEvent);
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
  bool challengeActionRequest(const char* id, const char* action);

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

  int eventStream_ = Transport::kNoStream;
  int gameStream_ = Transport::kNoStream;
  // Declared before the readers on purpose: NdjsonReader's constructor already writes into them.
  char eventLineBuffer_[kEventLineSize] = {};
  char gameLineBuffer_[kGameLineSize] = {};
  NdjsonReader eventReader_;
  NdjsonReader gameReader_;

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
