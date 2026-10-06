// SPDX-License-Identifier: GPL-3.0-or-later
#include "arrocco/lichess/client.h"

#include "arrocco/lichess/json.h"

namespace arrocco::lichess {
namespace {

bool sameText(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

void copyText(char* out, int outSize, const char* text) {
  if (out == nullptr || outSize <= 0) return;
  int i = 0;
  if (text != nullptr) {
    for (; text[i] != '\0' && i + 1 < outSize; ++i) out[i] = text[i];
  }
  out[i] = '\0';
}

// Wrap-safe: the ESP32's millis() rolls over after 49 days and a chess clock may well be on.
bool elapsed(uint32_t now, uint32_t since, uint32_t limit) {
  return static_cast<uint32_t>(now - since) >= limit;
}

int clampInt(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

// Builds a path or a form body into a fixed array; refuses to write past it.
class Builder {
 public:
  Builder(char* out, int size) : out_(out), size_(size) {
    if (size_ > 0) out_[0] = '\0';
  }
  Builder& text(const char* value) {
    if (value == nullptr) return *this;
    for (; *value != '\0'; ++value) {
      if (length_ + 1 >= size_) {
        ok_ = false;
        return *this;
      }
      out_[length_++] = *value;
    }
    out_[length_] = '\0';
    return *this;
  }
  Builder& number(int32_t value) {
    char digits[12];
    int n = 0;
    uint32_t magnitude = value < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value))
                                   : static_cast<uint32_t>(value);
    do {
      digits[n++] = static_cast<char>('0' + magnitude % 10);
      magnitude /= 10;
    } while (magnitude != 0 && n < static_cast<int>(sizeof digits));
    if (value < 0) {
      if (length_ + 1 >= size_) {
        ok_ = false;
        return *this;
      }
      out_[length_++] = '-';
    }
    while (n > 0) {
      if (length_ + 1 >= size_) {
        ok_ = false;
        return *this;
      }
      out_[length_++] = digits[--n];
    }
    out_[length_] = '\0';
    return *this;
  }
  bool ok() const { return ok_; }

 private:
  char* out_;
  int size_;
  int length_ = 0;
  bool ok_ = true;
};

// A Lichess id or username as it may appear in a path: letters, digits, '-' and '_' only.
// Anything else and we do not build the request at all — no escaping, no surprises.
bool safeId(const char* id) {
  if (id == nullptr || *id == '\0') return false;
  for (int i = 0; id[i] != '\0'; ++i) {
    const char c = id[i];
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                         c == '-' || c == '_';
    if (!allowed) return false;
    if (i >= 40) return false;
  }
  return true;
}

bool safeUci(const char* uci) {
  if (uci == nullptr) return false;
  int n = 0;
  while (uci[n] != '\0') ++n;
  if (n < 4 || n > 5) return false;
  for (int i = 0; i < 4; i += 2) {
    if (uci[i] < 'a' || uci[i] > 'h') return false;
    if (uci[i + 1] < '1' || uci[i + 1] > '8') return false;
  }
  if (n == 5) {
    const char p = uci[4];
    if (p != 'q' && p != 'r' && p != 'b' && p != 'n') return false;
  }
  return true;
}

// Walks a space-separated UCI move list without copying it.
class MoveTokens {
 public:
  MoveTokens(const char* text, int length)
      : p_(text == nullptr ? nullptr : text), end_(text == nullptr ? nullptr : text + length) {}
  // Copies the next token into `out`. False at the end of the list AND when a token does not fit:
  // a token longer than a UCI move is not a move, and handing back its first seven characters
  // would have replayed a DIFFERENT move onto the board. The caller checks truncated().
  bool next(char* out, int outSize) {
    if (p_ == nullptr || outSize <= 0) return false;
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t')) ++p_;
    if (p_ >= end_) return false;
    int n = 0;
    bool fits = true;
    while (p_ < end_ && *p_ != ' ' && *p_ != '\t') {
      if (n + 1 < outSize) {
        out[n++] = *p_;
      } else {
        fits = false;
      }
      ++p_;
    }
    out[n] = '\0';
    if (!fits) {
      truncated_ = true;
      return false;
    }
    return n > 0;
  }
  bool truncated() const { return truncated_; }

 private:
  const char* p_;
  const char* end_;
  bool truncated_ = false;
};

// Lichess classifies a game by limit + 40 * increment. Under 180 s that is bullet or faster, and
// such a game gets compat.board = false: the Board API then refuses to stream it, to move in it
// and even to ABORT it — a game started that way cannot be ended through this API at all, and
// sits "started" until Lichess itself tidies it up. Learned the hard way, so we refuse first.
constexpr int kMinBoardTotalSeconds = 180;

bool boardPlayable(int limitSeconds, int incrementSeconds) {
  return limitSeconds + 40 * incrementSeconds >= kMinBoardTotalSeconds;
}

// What challengeAt() hands back for an index nobody should have asked for.
const ChallengeInfo kNoChallenge{};

chess::GameResult resultOf(Winner w) {
  switch (w) {
    case Winner::White: return chess::GameResult::WhiteWins;
    case Winner::Black: return chess::GameResult::BlackWins;
    default: return chess::GameResult::Draw;
  }
}

}  // namespace

const char* clientStateName(ClientState state) {
  switch (state) {
    case ClientState::Offline: return "offline";
    case ClientState::Connecting: return "connecting";
    case ClientState::Idle: return "idle";
    case ClientState::Challenging: return "challenging";
    case ClientState::Playing: return "playing";
    case ClientState::Finished: return "finished";
    case ClientState::Failed: return "failed";
  }
  return "?";
}

LichessClient::LichessClient(Transport& transport, chess::Game& game)
    : transport_(transport),
      game_(game),
      eventReader_(eventLineBuffer_, kEventLineSize),
      gameReader_(gameLineBuffer_, kGameLineSize),
      challengeReader_(challengeLineBuffer_, kChallengeLineSize) {}

// ---------------------------------------------------------------------- lifecycle

void LichessClient::begin() {
  end();
  clearError();
  state_ = ClientState::Connecting;
  bump();
  if (!startRequest(Pending::Account, Method::Get, "/api/account", nullptr)) {
    state_ = ClientState::Failed;
    bump();
  }
}

void LichessClient::end() {
  if (pending_ != Pending::None) {
    transport_.endRequest();
    pending_ = Pending::None;
  }
  closeGameStream();
  closeEventStream();
  closeChallengeStream();
  state_ = ClientState::Offline;
  username_[0] = '\0';
  gameId_[0] = '\0';
  opponent_[0] = '\0';
  pendingChallengeId_[0] = '\0';
  queuedMove_[0] = '\0';
  moveRetryArmed_ = false;
  httpStatus_ = 0;
  challengeCount_ = 0;
  status_ = GameStatus::Unknown;
  winner_ = Winner::None;
  opponentGone_ = false;
  claimWinInSeconds_ = -1;
  moveRejected_ = false;
  desynchronised_ = false;
  hasClock_ = false;
  appliedPlies_ = 0;
  backoffMs_ = 0;
  bump();
}

void LichessClient::poll() {
  const uint32_t now = transport_.millis();
  pumpRequest(now);
  // A move that did not get through goes out again: after the backoff when the rate limiter
  // refused it, a couple of seconds later when the slot was busy or the network failed. Unless
  // the board moved on meanwhile: then the server has it (the POST that "failed" did arrive),
  // and sending it again would only earn a "Not your turn".
  if (queuedMove_[0] != '\0' && pending_ == Pending::None && state_ == ClientState::Playing) {
    if (game_.plyCount() != queuedAtPly_) {
      queuedMove_[0] = '\0';
      moveRetryArmed_ = false;
      bump();
    } else if (!inBackoff(now) && (!moveRetryArmed_ || elapsed(now, moveRetryMs_, kMoveRetryDelayMs))) {
      char again[chess::kUciBufferSize];
      copyText(again, sizeof again, queuedMove_);
      moveRetryArmed_ = false;
      sendMove(again);
    }
  }
  pumpEventStream(now);
  pumpGameStream(now);
  pumpChallengeStream();
}

void LichessClient::armMoveRetry() {
  moveRetryArmed_ = true;
  moveRetryMs_ = transport_.millis();
}

// ---------------------------------------------------------------------- requests

bool LichessClient::inBackoff(uint32_t now) const {
  return backoffMs_ != 0 && !elapsed(now, backoffStartMs_, backoffMs_);
}

uint32_t LichessClient::backoffRemainingMs() const {
  if (backoffMs_ == 0) return 0;
  const uint32_t gone = transport_.millis() - backoffStartMs_;
  return gone >= backoffMs_ ? 0 : backoffMs_ - gone;
}

bool LichessClient::startRequest(Pending kind, Method method, const char* path, const char* body) {
  if (pending_ != Pending::None) {
    setError(ClientError::Protocol, "another request is still running");
    return false;
  }
  const uint32_t now = transport_.millis();
  if (inBackoff(now)) {
    setError(ClientError::RateLimited, "rate limited: waiting before the next request");
    return false;
  }
  if (!transport_.beginRequest(method, path, body, response_, kResponseSize)) {
    setTransportError(ClientError::Network, "cannot reach lichess.org");
    return false;
  }
  pending_ = kind;
  requestStartMs_ = now;
  return true;
}

void LichessClient::pumpRequest(uint32_t now) {
  if (pending_ == Pending::None) return;
  const RequestState rs = transport_.requestState();
  if (rs == RequestState::Busy) {
    if (elapsed(now, requestStartMs_, kRequestTimeoutMs)) {
      const Pending kind = pending_;
      pending_ = Pending::None;
      transport_.endRequest();
      httpStatus_ = 0;
      setError(ClientError::Network, "the request timed out");
      if (kind == Pending::Account) state_ = ClientState::Failed;
      if (kind == Pending::Move) armMoveRetry();  // it may have arrived: poll() checks first
      if (kind == Pending::AcceptChallenge) pendingChallengeId_[0] = '\0';
      bump();
    }
    return;
  }
  const Pending kind = pending_;
  pending_ = Pending::None;
  if (rs == RequestState::Idle) {  // the transport lost it: treat as a network failure
    httpStatus_ = 0;
    setError(ClientError::Network, "the request was dropped");
    if (kind == Pending::Account) state_ = ClientState::Failed;
    if (kind == Pending::Move) armMoveRetry();
    if (kind == Pending::AcceptChallenge) pendingChallengeId_[0] = '\0';
    bump();
    return;
  }
  if (rs == RequestState::Failed) {
    transport_.endRequest();
    httpStatus_ = 0;
    setTransportError(ClientError::Network, "cannot reach lichess.org");
    if (kind == Pending::Account) state_ = ClientState::Failed;
    if (kind == Pending::Move) armMoveRetry();
    if (kind == Pending::AcceptChallenge) pendingChallengeId_[0] = '\0';
    bump();
    return;
  }
  const int httpStatus = transport_.responseStatus();
  int length = transport_.responseLength();
  if (length < 0) length = 0;
  if (length >= kResponseSize) length = kResponseSize - 1;
  transport_.endRequest();
  handleResponse(kind, httpStatus, length);
}

void LichessClient::rateLimited(Pending kind) {
  backoffStartMs_ = transport_.millis();
  backoffMs_ = kRateLimitBackoffMs;
  // A refused move was never played: queuedMove_ still holds it and poll() sends it again
  // as soon as the minute is over.
  const bool willRetry = kind == Pending::Move && queuedMove_[0] != '\0';
  setError(ClientError::RateLimited, willRetry ? "rate limited: the move goes out again in a minute"
                                               : "rate limited: waiting a minute");
  bump();
}

// A stream Lichess refuses with 429 is the same offence as a refused request, and reopening it
// two seconds later (the plain reconnect delay) is exactly how a token gets itself blocked.
// inBackoff() already gates BOTH stream reopens and requests, so setting the backoff is enough.
bool LichessClient::streamRateLimited() {
  if (transport_.lastStreamStatus() != 429) return false;
  backoffStartMs_ = transport_.millis();
  backoffMs_ = kRateLimitBackoffMs;
  setError(ClientError::RateLimited, "rate limited: waiting a minute before reconnecting");
  bump();
  return true;
}

void LichessClient::handleResponse(Pending kind, int httpStatus, int length) {
  httpStatus_ = (httpStatus >= 200 && httpStatus < 300) ? 0 : httpStatus;
  if (httpStatus == 429) {
    rateLimited(kind);
    return;
  }
  // A move sent again after a timeout whose first POST did arrive: the board already shows it,
  // and the "Not your turn" that comes back is about a move that WAS played. Nothing to report.
  if (kind == Pending::Move && game_.plyCount() != queuedAtPly_) {
    httpStatus_ = 0;
    moveRejected_ = false;
    queuedMove_[0] = '\0';
    if (error_ == ClientError::Network || error_ == ClientError::MoveRejected) clearError();
    bump();
    return;
  }
  if (httpStatus < 200 || httpStatus >= 300) {
    char detail[kErrorSize] = {};
    if (!parseErrorField(response_, length, detail, sizeof detail) || detail[0] == '\0') {
      Builder b(detail, sizeof detail);
      b.text("lichess answered HTTP ").number(httpStatus);
    }
    if (kind == Pending::Move) {
      moveRejected_ = true;
      queuedMove_[0] = '\0';
      setError(ClientError::MoveRejected, detail);
    } else {
      if (kind == Pending::AcceptChallenge) pendingChallengeId_[0] = '\0';   // no game is coming
      setError(ClientError::Http, detail);
      if (kind == Pending::Account) state_ = ClientState::Failed;
      if (kind == Pending::ChallengeAi || kind == Pending::ChallengeUser) {
        if (state_ == ClientState::Challenging) state_ = ClientState::Idle;
      }
    }
    bump();
    return;
  }

  const JsonObject body(response_, length);
  switch (kind) {
    case Pending::Account: {
      if (!body.copyString("username", username_, sizeof username_) || username_[0] == '\0') {
        setError(ClientError::Protocol, "/api/account did not say who we are");
        state_ = ClientState::Failed;
        bump();
        return;
      }
      clearError();
      state_ = ClientState::Idle;
      eventRetryArmed_ = false;
      bump();
      pumpEventStream(transport_.millis());
      return;
    }
    case Pending::ChallengeAi: {
      // POST /api/challenge/ai answers with the game itself: we can follow it at once.
      char id[kGameIdSize] = {};
      if (!body.copyString("id", id, sizeof id) || id[0] == '\0') {
        setError(ClientError::Protocol, "the AI game has no id");
        state_ = ClientState::Idle;
        bump();
        return;
      }
      clearError();
      followGame(id);
      return;
    }
    case Pending::ChallengeUser: {
      // Either {"id":...} or {"challenge":{"id":...}} depending on the endpoint's mood.
      if (!body.copyString("id", pendingChallengeId_, sizeof pendingChallengeId_)) {
        const JsonObject challenge = body.object("challenge");
        if (challenge.valid()) challenge.copyString("id", pendingChallengeId_, sizeof pendingChallengeId_);
      }
      clearError();
      state_ = ClientState::Challenging;
      bump();
      return;
    }
    case Pending::Move:
      moveRejected_ = false;
      queuedMove_[0] = '\0';
      moveRetryArmed_ = false;
      clearError();
      bump();
      return;
    case Pending::ChallengeAction:
    case Pending::AcceptChallenge:
    case Pending::Resign:
    case Pending::Abort:
    case Pending::DrawYes:
    case Pending::DrawNo:
    case Pending::ClaimVictory:
      clearError();
      bump();
      return;
    case Pending::None:
      return;
  }
}

// ---------------------------------------------------------------------- streams

void LichessClient::closeEventStream() {
  if (eventStream_ != Transport::kNoStream) {
    transport_.closeStream(eventStream_);
    eventStream_ = Transport::kNoStream;
  }
  eventReader_.reset();
  eventHeard_ = false;
}

void LichessClient::closeGameStream() {
  if (gameStream_ != Transport::kNoStream) {
    transport_.closeStream(gameStream_);
    gameStream_ = Transport::kNoStream;
  }
  gameReader_.reset();
  gameHeard_ = false;
}

void LichessClient::closeChallengeStream() {
  if (challengeStream_ != Transport::kNoStream) {
    transport_.closeStream(challengeStream_);
    challengeStream_ = Transport::kNoStream;
  }
  challengeReader_.reset();
  challengeAnswered_ = false;
  cancelWanted_ = false;
  challengeHeard_ = false;
}

void LichessClient::feedLines(NdjsonReader& reader, const char* data, int size, Lines kind) {
  int used = 0;
  while (used < size) {
    const int taken = reader.feed(data + used, size - used);
    used += taken;
    const char* line = nullptr;
    int length = 0;
    while (reader.takeLine(line, length)) {
      if (length == 0) continue;  // keep-alive
      if (kind == Lines::Event) {
        handleEventLine(line, length);
      } else if (kind == Lines::Game) {
        handleGameLine(line, length);
        // The final gameState closes this stream from inside handleGameLine, and closeGameStream()
        // resets the very reader we are feeding. Anything left in the chunk belongs to a game that
        // is over: stop, instead of replaying it onto a board the user is already looking at.
        if (gameStream_ == Transport::kNoStream) return;
      } else {
        handleChallengeLine(line, length);
        // The answer closes the challenge stream, and with it this reader.
        if (challengeStream_ == Transport::kNoStream) return;
      }
    }
    if (taken == 0 && !reader.hasLine()) break;  // nothing more can be done with this chunk
  }
  if (reader.overflow()) {
    reader.clearOverflow();
    setError(ClientError::Protocol, kind == Lines::Event  ? "an event line was too long"
                                    : kind == Lines::Game ? "a game line was too long"
                                                          : "the challenge line was too long");
    if (kind == Lines::Game) desynchronised_ = true;
    bump();
  }
}

void LichessClient::pumpEventStream(uint32_t now) {
  if (state_ == ClientState::Offline || state_ == ClientState::Failed) return;
  if (username_[0] == '\0') return;  // begin() has not finished

  if (eventStream_ == Transport::kNoStream) {
    if (inBackoff(now)) return;
    if (eventRetryArmed_ && !elapsed(now, eventRetryMs_, kReconnectDelayMs)) return;
    eventStream_ = transport_.openStream("/api/stream/event");
    eventRetryArmed_ = true;
    eventRetryMs_ = now;
    if (eventStream_ == Transport::kNoStream) {
      if (!streamRateLimited()) setTransportError(ClientError::Network, "cannot open the event stream");
      return;
    }
    eventReader_.reset();
    eventActivityMs_ = now;
    eventHeard_ = false;
    return;
  }

  for (int guard = 0; guard < 8; ++guard) {
    const int n = transport_.readStream(eventStream_, chunk_, static_cast<int>(sizeof chunk_));
    if (n < 0) {
      closeEventStream();
      eventRetryArmed_ = true;
      eventRetryMs_ = now;
      setTransportError(ClientError::Network, "the event stream closed: reconnecting");
      bump();
      return;
    }
    if (n == 0) break;
    eventActivityMs_ = now;
    if (!eventHeard_) {
      eventHeard_ = true;
      bump();
    }
    feedLines(eventReader_, chunk_, n, Lines::Event);
  }

  if (elapsed(now, eventActivityMs_, kSilenceTimeoutMs)) {
    closeEventStream();
    eventRetryArmed_ = true;
    eventRetryMs_ = now;
    setError(ClientError::Network, "the event stream went silent: reconnecting");
    bump();
  }
}

void LichessClient::pumpGameStream(uint32_t now) {
  if (gameId_[0] == '\0' || state_ != ClientState::Playing) return;
  if (finishSeen_ && elapsed(now, finishSeenMs_, kFinishGraceMs)) {
    finishSeen_ = false;
    finishGame(finishStatus_, finishWinner_);
    return;
  }

  if (gameStream_ == Transport::kNoStream) {
    if (inBackoff(now)) return;
    if (gameRetryArmed_ && !elapsed(now, gameRetryMs_, kReconnectDelayMs)) return;
    Builder path(path_, sizeof path_);
    path.text("/api/board/game/stream/").text(gameId_);
    if (!path.ok()) {
      setError(ClientError::Protocol, "the game id is too long");
      return;
    }
    gameStream_ = transport_.openStream(path_);
    gameRetryArmed_ = true;
    gameRetryMs_ = now;
    if (gameStream_ == Transport::kNoStream) {
      if (!streamRateLimited()) setTransportError(ClientError::Network, "cannot open the game stream");
      return;
    }
    gameReader_.reset();
    gameActivityMs_ = now;
    gameHeard_ = false;
    return;
  }

  for (int guard = 0; guard < 8; ++guard) {
    const int n = transport_.readStream(gameStream_, chunk_, static_cast<int>(sizeof chunk_));
    if (n < 0) {
      closeGameStream();
      gameRetryArmed_ = true;
      gameRetryMs_ = now;
      setTransportError(ClientError::Network, "the game stream closed: reconnecting");
      bump();
      return;
    }
    if (n == 0) break;
    gameActivityMs_ = now;
    if (!gameHeard_) {
      gameHeard_ = true;
      // Back after a reconnect: the "reconnecting" that was on the screen is over.
      if (error_ == ClientError::Network) clearError();
      bump();
    }
    feedLines(gameReader_, chunk_, n, Lines::Game);
    if (state_ != ClientState::Playing) return;  // the game ended while we were reading
  }

  if (elapsed(now, gameActivityMs_, kSilenceTimeoutMs)) {
    closeGameStream();
    gameRetryArmed_ = true;
    gameRetryMs_ = now;
    setError(ClientError::Network, "the game stream went silent: reconnecting");
    bump();
  }
}

// Our kept-alive challenge. No silence timeout here: Lichess has nothing to say on it until the
// friend answers, and the event stream (keep-alive every 7 s) already watches the connection.
void LichessClient::pumpChallengeStream() {
  if (challengeStream_ == Transport::kNoStream) return;
  for (int guard = 0; guard < 8; ++guard) {
    const int n = transport_.readStream(challengeStream_, chunk_, static_cast<int>(sizeof chunk_));
    if (n < 0) {
      // Over without an answer: refused before it said anything ("No such user"), or lost with
      // its connection afterwards.
      const bool answered = challengeAnswered_;
      const bool heard = challengeHeard_;
      closeChallengeStream();
      if (!answered && state_ == ClientState::Challenging) {
        if (heard) {
          setTransportError(ClientError::Network, "the challenge was lost with the connection");
        } else {
          Builder why(lastError_, sizeof lastError_);
          const char* detail = transport_.lastError();
          why.text(detail != nullptr && detail[0] != '\0' ? detail : "Lichess refused the challenge");
          error_ = ClientError::Http;
        }
        outgoingEnded(error_, lastError_);
      }
      return;
    }
    if (n == 0) return;
    feedLines(challengeReader_, chunk_, n, Lines::Challenge);
    if (challengeStream_ == Transport::kNoStream) return;
  }
}

// The kept-alive challenge says two things: the challenge itself first, then {"done":...}.
void LichessClient::handleChallengeLine(const char* line, int length) {
  const JsonObject root(line, length);
  char done[kWordSize] = {};
  if (root.copyString("done", done, sizeof done)) {
    challengeAnswered_ = true;
    if (sameText(done, "accepted")) {
      // The game has the challenge's id. The event stream will say gameStart too; following it
      // from here means a hiccup on that stream cannot leave the board waiting.
      char id[kGameIdSize] = {};
      copyText(id, sizeof id, pendingChallengeId_);
      closeChallengeStream();
      if (id[0] != '\0' && state_ == ClientState::Challenging) followGame(id);
      return;
    }
    closeChallengeStream();
    if (state_ == ClientState::Challenging) {
      outgoingEnded(ClientError::Http, sameText(done, "declined") ? "the challenge was declined"
                                                                   : "the challenge was cancelled");
    }
    return;
  }
  char id[kChallengeIdSize] = {};
  bool found = root.copyString("id", id, sizeof id) && id[0] != '\0';
  if (!found) {
    const JsonObject challenge = root.object("challenge");
    found = challenge.valid() && challenge.copyString("id", id, sizeof id) && id[0] != '\0';
  }
  if (!found || !safeId(id)) return;
  challengeHeard_ = true;
  if (cancelWanted_) {
    closeChallengeStream();
    challengeActionRequest(id, "cancel");
    return;
  }
  copyText(pendingChallengeId_, sizeof pendingChallengeId_, id);
}

// Our challenge is over without a game: back to Idle, with the reason on the screen.
void LichessClient::outgoingEnded(ClientError kind, const char* text) {
  char why[kErrorSize];
  copyText(why, sizeof why, text);
  pendingChallengeId_[0] = '\0';
  state_ = ClientState::Idle;
  setError(kind, why);
  bump();
}

// ---------------------------------------------------------------------- the event stream

void LichessClient::handleEventLine(const char* line, int length) {
  Event event;
  if (!parseEvent(line, length, event)) return;
  switch (event.type) {
    case EventType::GameStart: {
      const bool ours = pendingChallengeId_[0] != '\0' && sameText(pendingChallengeId_, event.game.id);
      if (state_ == ClientState::Playing && !sameText(gameId_, event.game.id)) return;  // another game: ignore
      // A game the Board API refuses (compat.board false: bullet, or a variant) would only be
      // reopened every two seconds for ever, and a correspondence game is not a reason to take
      // the screen over. Our own challenge, or the one we accepted, is followed whatever it is.
      const bool boardCanPlay = !(event.game.compatKnown && !event.game.compatBoard);
      const bool realTime = !sameText(event.game.speed, "correspondence");
      if (state_ != ClientState::Playing && (ours || (autoFollow_ && boardCanPlay && realTime))) {
        closeChallengeStream();
        pendingChallengeId_[0] = '\0';
        copyText(opponent_, sizeof opponent_, event.game.opponent);
        opponentAiLevel_ = event.game.opponentAiLevel;
        rated_ = event.game.rated;
        ourColor_ = event.game.color;
        followGame(event.game.id);
      }
      return;
    }
    case EventType::GameFinish: {
      if (!sameText(gameId_, event.game.id) || state_ != ClientState::Playing) return;
      const GameStatus endStatus = event.game.status == GameStatus::Unknown ? status_ : event.game.status;
      const Winner endWinner = event.game.winner != Winner::None ? event.game.winner : winner_;
      if (gameConnected()) {
        // The game stream is live: its last gameState, with the last move on it, is on its way.
        // Ending here would close that stream first and leave the move off the board.
        finishSeen_ = true;
        finishSeenMs_ = transport_.millis();
        finishStatus_ = endStatus;
        finishWinner_ = endWinner;
        return;
      }
      finishGame(endStatus, endWinner);
      return;
    }
    case EventType::Challenge:
      noteChallenge(event.challenge);
      bump();
      return;
    case EventType::ChallengeCanceled:
    case EventType::ChallengeDeclined:
      forgetChallenge(event.challenge.id);
      if (sameText(pendingChallengeId_, event.challenge.id)) {
        pendingChallengeId_[0] = '\0';
        closeChallengeStream();
        if (state_ == ClientState::Challenging) state_ = ClientState::Idle;
        setError(ClientError::Http, event.type == EventType::ChallengeDeclined ? "the challenge was declined"
                                                                               : "the challenge was cancelled");
      }
      bump();
      return;
    case EventType::Unknown:
      return;
  }
}

void LichessClient::noteChallenge(const ChallengeInfo& info) {
  for (int i = 0; i < challengeCount_; ++i) {
    if (sameText(challenges_[i].id, info.id)) {
      challenges_[i] = info;
      return;
    }
  }
  if (challengeCount_ < kMaxChallenges) challenges_[challengeCount_++] = info;
}

void LichessClient::forgetChallenge(const char* id) {
  for (int i = 0; i < challengeCount_; ++i) {
    if (!sameText(challenges_[i].id, id)) continue;
    for (int j = i + 1; j < challengeCount_; ++j) challenges_[j - 1] = challenges_[j];
    --challengeCount_;
    return;
  }
}

const ChallengeInfo& LichessClient::challengeAt(int index) const {
  if (index < 0 || index >= challengeCount_) return kNoChallenge;
  return challenges_[index];
}

// ---------------------------------------------------------------------- the game stream

void LichessClient::handleGameLine(const char* line, int length) {
  GameMessage message;
  if (!parseGameMessage(line, length, message)) return;
  switch (message.type) {
    case GameMessageType::GameFull: {
      const GameFullInfo& full = message.full;
      if (gameId_[0] == '\0') copyText(gameId_, sizeof gameId_, full.id);
      rated_ = full.rated;
      hasClock_ = full.clockInitial >= 0;
      // Who we are in this game. A reconnect re-reads it, so a stale colour cannot survive.
      if (username_[0] != '\0') {
        if (sameText(full.white, username_)) {
          ourColor_ = chess::Color::White;
        } else if (sameText(full.black, username_)) {
          ourColor_ = chess::Color::Black;
        }
      }
      const bool weArePlayingWhite = ourColor_ == chess::Color::White;
      copyText(opponent_, sizeof opponent_, weArePlayingWhite ? full.black : full.white);
      opponentAiLevel_ = weArePlayingWhite ? full.blackAiLevel : full.whiteAiLevel;
      copyText(initialFen_, sizeof initialFen_, full.initialFen);
      // A gameFull always restates the whole game: rebuild from its initial position and let
      // syncMoves() replay the move list. This is what makes a reconnect safe.
      resetGameToInitial();
      applyState(full.state);
      return;
    }
    case GameMessageType::GameState:
      applyState(message.state);
      return;
    case GameMessageType::OpponentGone:
      opponentGone_ = message.gone;
      claimWinInSeconds_ = message.gone ? message.claimWinInSeconds : -1;
      goneAtMs_ = transport_.millis();
      bump();
      return;
    case GameMessageType::ChatLine:  // we never send chat and have nowhere to show it
    case GameMessageType::Unknown:
      return;
  }
}

void LichessClient::resetGameToInitial() {
  if (initialFen_[0] == '\0' || sameText(initialFen_, "startpos")) {
    game_.newGame();
  } else if (!game_.setFromFen(initialFen_)) {
    setError(ClientError::Protocol, "lichess sent a position we cannot read");
    game_.newGame();
  }
  appliedPlies_ = 0;
}

void LichessClient::syncMoves(const char* moves, int length) {
  char token[8];
  char have[chess::kUciBufferSize];

  // How many of the server's moves this Game already has, in the same order.
  int matching = 0;
  {
    MoveTokens tokens(moves, length);
    while (matching < game_.plyCount() && tokens.next(token, sizeof token)) {
      game_.moveAt(matching).toUci(have);
      if (!sameText(have, token)) break;
      ++matching;
    }
  }
  // Our line and the server's diverge (a reconnect onto another game, a takeback): start again.
  if (matching < game_.plyCount()) {
    resetGameToInitial();
    matching = 0;
  }

  MoveTokens tokens(moves, length);
  int index = 0;
  while (tokens.next(token, sizeof token)) {
    if (index++ < matching) continue;
    if (!game_.playUci(token)) {
      desynchronised_ = true;
      setError(ClientError::Desynchronised, "cannot replay the server's move list");
      bump();
      return;
    }
  }
  // A token that did not fit `token` was refused rather than truncated, so the replay stopped
  // short: the board is NOT the server's board and must not be treated as if it were.
  if (tokens.truncated()) {
    desynchronised_ = true;
    setError(ClientError::Desynchronised, "the server's move list has something that is not a move");
    bump();
    return;
  }
  appliedPlies_ = game_.plyCount();
  desynchronised_ = false;
  // The board is the server's again, so the message that said otherwise has to go: without this
  // the UI kept showing "cannot replay the server's move list" for the rest of the game.
  if (error_ == ClientError::Desynchronised) clearError();
}

void LichessClient::applyState(const GameStateInfo& info) {
  if (!info.valid) return;
  const int before = game_.plyCount();
  syncMoves(info.moves, info.movesLength);
  wtime_ = info.wtime;
  btime_ = info.btime;
  winc_ = info.winc;
  binc_ = info.binc;
  clockAtMs_ = transport_.millis();
  whiteDrawOffer_ = info.whiteDrawOffer;
  blackDrawOffer_ = info.blackDrawOffer;
  status_ = info.status;
  winner_ = info.winner;
  if (game_.plyCount() != before) moveRejected_ = false;
  // The server has our move (the board shows it): nothing is left to send. A POST still in
  // flight keeps its slot; its answer finds the board moved on and says nothing.
  if (queuedMove_[0] != '\0' && game_.plyCount() != queuedAtPly_ && pending_ != Pending::Move) {
    queuedMove_[0] = '\0';
    moveRetryArmed_ = false;
  }
  bump();
  if (statusIsFinished(info.status)) finishGame(info.status, info.winner);
}

void LichessClient::finishGame(GameStatus endStatus, Winner endWinner) {
  finishSeen_ = false;
  status_ = endStatus;
  winner_ = endWinner;
  queuedMove_[0] = '\0';
  switch (endStatus) {
    case GameStatus::Resign:
      game_.declareResult(resultOf(endWinner), chess::GameEndReason::Resignation);
      break;
    case GameStatus::Timeout:
    case GameStatus::Outoftime:
    case GameStatus::NoStart:
      game_.declareResult(resultOf(endWinner), chess::GameEndReason::Timeout);
      break;
    case GameStatus::Draw:
      game_.declareResult(chess::GameResult::Draw, chess::GameEndReason::Agreement);
      break;
    default:
      // Mate and stalemate the rules already saw; aborted, cheat, variantEnd and the unknowns
      // have no reason in Game: status() and winner() are what the UI shows.
      break;
  }
  closeGameStream();
  if (state_ == ClientState::Playing || state_ == ClientState::Challenging) state_ = ClientState::Finished;
  bump();
}

// ---------------------------------------------------------------------- commands

bool LichessClient::startAiGame(int level, int clockLimitSeconds, int clockIncrementSeconds,
                                chess::Color color, bool randomColor) {
  if (state_ != ClientState::Idle && state_ != ClientState::Finished) {
    setError(ClientError::Protocol, "not ready to start a game");
    return false;
  }
  const int limit = clampInt(clockLimitSeconds, 0, 10800);
  const int increment = clampInt(clockIncrementSeconds, 0, 60);
  if (!boardPlayable(limit, increment)) {
    setError(ClientError::Protocol, "too fast for the Lichess board API: 3+0 or slower");
    return false;
  }
  Builder body(body_, sizeof body_);
  body.text("level=").number(clampInt(level, 1, 8));
  body.text("&clock.limit=").number(limit);
  body.text("&clock.increment=").number(increment);
  body.text("&variant=standard");
  if (!randomColor) body.text(color == chess::Color::White ? "&color=white" : "&color=black");
  if (!body.ok()) {
    setError(ClientError::Protocol, "the request does not fit");
    return false;
  }
  gameId_[0] = '\0';
  pendingChallengeId_[0] = '\0';
  opponent_[0] = '\0';      // the gameFull names this game's opponent
  opponentAiLevel_ = -1;
  status_ = GameStatus::Unknown;
  winner_ = Winner::None;
  opponentGone_ = false;
  claimWinInSeconds_ = -1;
  if (!randomColor) ourColor_ = color;
  if (!startRequest(Pending::ChallengeAi, Method::Post, "/api/challenge/ai", body_)) return false;
  state_ = ClientState::Challenging;
  bump();
  return true;
}

bool LichessClient::challengeUser(const char* user, bool isRated, int clockLimitSeconds,
                                  int clockIncrementSeconds, chess::Color color, bool randomColor) {
  if (state_ != ClientState::Idle && state_ != ClientState::Finished) {
    setError(ClientError::Protocol, "not ready to start a game");
    return false;
  }
  if (!safeId(user)) {
    setError(ClientError::Protocol, "that is not a Lichess username");
    return false;
  }
  Builder path(path_, sizeof path_);
  path.text("/api/challenge/").text(user);
  const int limit = clampInt(clockLimitSeconds, 0, 10800);
  const int increment = clampInt(clockIncrementSeconds, 0, 60);
  if (!boardPlayable(limit, increment)) {
    setError(ClientError::Protocol, "too fast for the Lichess board API: 3+0 or slower");
    return false;
  }
  const bool keepAlive = transport_.supportsPostStreams();
  Builder body(body_, sizeof body_);
  body.text(isRated ? "rated=true" : "rated=false");
  body.text("&clock.limit=").number(limit);
  body.text("&clock.increment=").number(increment);
  body.text("&variant=standard");
  if (!randomColor) body.text(color == chess::Color::White ? "&color=white" : "&color=black");
  if (keepAlive) body.text("&keepAliveStream=true");
  if (!path.ok() || !body.ok()) {
    setError(ClientError::Protocol, "the request does not fit");
    return false;
  }
  gameId_[0] = '\0';
  pendingChallengeId_[0] = '\0';
  opponent_[0] = '\0';      // until the game says who it is (the name we typed is not the spelling)
  opponentAiLevel_ = -1;
  if (!keepAlive) return startRequest(Pending::ChallengeUser, Method::Post, path_, body_);

  if (inBackoff(transport_.millis())) {
    setError(ClientError::RateLimited, "rate limited: waiting before the next request");
    return false;
  }
  closeChallengeStream();
  challengeStream_ = transport_.openPostStream(path_, body_);
  if (challengeStream_ == Transport::kNoStream) {
    if (!streamRateLimited()) setTransportError(ClientError::Network, "cannot send the challenge");
    return false;
  }
  challengeReader_.reset();
  challengeAnswered_ = false;
  clearError();
  state_ = ClientState::Challenging;
  bump();
  return true;
}

void LichessClient::cancelOutgoing() {
  // Whatever the stream already brought first: its id, or an answer that came before the tap.
  if (state_ == ClientState::Challenging) pumpChallengeStream();
  if (state_ == ClientState::Playing) return;   // accepted before the cancel: the game is on
  char id[kChallengeIdSize] = {};
  copyText(id, sizeof id, pendingChallengeId_);
  pendingChallengeId_[0] = '\0';
  if (state_ == ClientState::Challenging) state_ = ClientState::Idle;
  clearError();
  bump();
  if (id[0] == '\0' && challengeStream_ != Transport::kNoStream) {
    // Withdrawn before Lichess said its id. Closing the stream would only let it lapse in 20 s,
    // time enough for the friend to accept it: the stream stays until the id comes, and the
    // challenge is cancelled then (handleChallengeLine).
    cancelWanted_ = true;
    return;
  }
  closeChallengeStream();
  // With its id known it is cancelled outright, so the friend's screen stops offering it.
  if (id[0] != '\0') challengeActionRequest(id, "cancel");
}

bool LichessClient::challengeActionRequest(const char* id, const char* action, Pending kind) {
  if (!safeId(id)) {
    setError(ClientError::Protocol, "that is not a challenge id");
    return false;
  }
  Builder path(path_, sizeof path_);
  path.text("/api/challenge/").text(id).text("/").text(action);
  if (!path.ok()) {
    setError(ClientError::Protocol, "the request does not fit");
    return false;
  }
  return startRequest(kind, Method::Post, path_, nullptr);
}

// The game that starts from it is ours: followed even when auto-follow would leave it alone.
bool LichessClient::acceptChallenge(const char* id) {
  const bool sent = challengeActionRequest(id, "accept", Pending::AcceptChallenge);
  if (sent) copyText(pendingChallengeId_, sizeof pendingChallengeId_, id);
  return sent;
}

bool LichessClient::declineChallenge(const char* id) {
  const bool sent = challengeActionRequest(id, "decline");
  if (sent) forgetChallenge(id);
  return sent;
}

bool LichessClient::cancelChallenge(const char* id) {
  const bool sent = challengeActionRequest(id, "cancel");
  if (sent) forgetChallenge(id);
  return sent;
}

bool LichessClient::followGame(const char* id) {
  if (!safeId(id)) {
    setError(ClientError::Protocol, "that is not a game id");
    return false;
  }
  closeGameStream();
  closeChallengeStream();
  pendingChallengeId_[0] = '\0';
  copyText(gameId_, sizeof gameId_, id);
  initialFen_[0] = '\0';
  queuedMove_[0] = '\0';
  moveRetryArmed_ = false;
  finishSeen_ = false;
  appliedPlies_ = 0;
  status_ = GameStatus::Unknown;
  winner_ = Winner::None;
  opponentGone_ = false;
  claimWinInSeconds_ = -1;
  moveRejected_ = false;
  desynchronised_ = false;
  whiteDrawOffer_ = blackDrawOffer_ = false;
  gameRetryArmed_ = false;
  state_ = ClientState::Playing;
  bump();
  pumpGameStream(transport_.millis());
  return true;
}

bool LichessClient::ourTurn() const {
  if (state_ != ClientState::Playing || desynchronised_) return false;
  if (statusIsFinished(status_)) return false;
  return game_.position().sideToMove() == ourColor_;
}

bool LichessClient::drawOfferedToUs() const {
  return ourColor_ == chess::Color::White ? blackDrawOffer_ : whiteDrawOffer_;
}

bool LichessClient::drawOfferedByUs() const {
  return ourColor_ == chess::Color::White ? whiteDrawOffer_ : blackDrawOffer_;
}

int32_t LichessClient::claimWinRemainingSeconds() const {
  if (!opponentGone_ || claimWinInSeconds_ < 0) return -1;
  const uint32_t gone = (transport_.millis() - goneAtMs_) / 1000u;
  return gone >= static_cast<uint32_t>(claimWinInSeconds_) ? 0 : claimWinInSeconds_ - static_cast<int32_t>(gone);
}

bool LichessClient::sendMove(const char* uci) {
  if (state_ != ClientState::Playing || gameId_[0] == '\0') {
    setError(ClientError::Protocol, "there is no game to move in");
    return false;
  }
  if (!safeUci(uci)) {
    setError(ClientError::Protocol, "that is not a move");
    return false;
  }
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/move/").text(uci);
  if (!path.ok()) {
    setError(ClientError::Protocol, "the request does not fit");
    return false;
  }
  // Kept until the server accepts or refuses it, so it can go out again exactly as it was: after
  // a 429, after a network failure, or when another request still holds the slot.
  const bool again = queuedMove_[0] != '\0' && sameText(queuedMove_, uci);
  if (!again) queuedAtPly_ = game_.plyCount();
  copyText(queuedMove_, sizeof queuedMove_, uci);
  moveRejected_ = false;
  // Another request of ours holds the slot (a draw offer, say): the move simply waits for it,
  // and poll() sends it the moment the slot is free. Nothing has gone wrong, so nothing is said.
  if (pending_ != Pending::None) {
    bump();
    return true;
  }
  if (!startRequest(Pending::Move, Method::Post, path_, nullptr)) {
    // Queued all the same: poll() sends it once the backoff or the network allows.
    if (error_ != ClientError::RateLimited) armMoveRetry();
    bump();
    return true;
  }
  return true;
}

bool LichessClient::sendMove(chess::Move move) {
  char uci[chess::kUciBufferSize];
  move.toUci(uci);
  return sendMove(uci);
}

bool LichessClient::resign() {
  if (gameId_[0] == '\0') {
    setError(ClientError::Protocol, "there is no game to resign");
    return false;
  }
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/resign");
  if (!path.ok()) return false;
  return startRequest(Pending::Resign, Method::Post, path_, nullptr);
}

bool LichessClient::abortGame() {
  if (gameId_[0] == '\0') {
    setError(ClientError::Protocol, "there is no game to abort");
    return false;
  }
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/abort");
  if (!path.ok()) return false;
  return startRequest(Pending::Abort, Method::Post, path_, nullptr);
}

bool LichessClient::offerDraw() {
  if (gameId_[0] == '\0') return false;
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/draw/yes");
  if (!path.ok()) return false;
  return startRequest(Pending::DrawYes, Method::Post, path_, nullptr);
}

bool LichessClient::declineDraw() {
  if (gameId_[0] == '\0') return false;
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/draw/no");
  if (!path.ok()) return false;
  return startRequest(Pending::DrawNo, Method::Post, path_, nullptr);
}

bool LichessClient::claimVictory() {
  if (gameId_[0] == '\0') return false;
  Builder path(path_, sizeof path_);
  path.text("/api/board/game/").text(gameId_).text("/claim-victory");
  if (!path.ok()) return false;
  return startRequest(Pending::ClaimVictory, Method::Post, path_, nullptr);
}

// ---------------------------------------------------------------------- clocks and errors

int32_t LichessClient::clockMsNow(chess::Color side) const {
  int32_t left = clockMs(side);
  if (!hasClock_ || statusIsFinished(status_) || state_ != ClientState::Playing) return left;
  if (game_.position().sideToMove() != side) return left;
  if (game_.plyCount() < 2) return left;  // the clock only starts on the second move
  const uint32_t gone = transport_.millis() - clockAtMs_;
  if (static_cast<int64_t>(gone) >= left) return 0;
  left -= static_cast<int32_t>(gone);
  return left;
}

void LichessClient::setError(ClientError kind, const char* text) {
  error_ = kind;
  copyText(lastError_, sizeof lastError_, text);
}

// The same, plus whatever the transport can say about it ("This game cannot be played with the
// Board API."). That sentence is the difference between "it keeps reconnecting" and knowing why.
void LichessClient::setTransportError(ClientError kind, const char* text) {
  Builder message(lastError_, sizeof lastError_);
  message.text(text);
  const char* detail = transport_.lastError();
  if (detail != nullptr && detail[0] != '\0') message.text(": ").text(detail);
  error_ = kind;
}

void LichessClient::clearError() {
  error_ = ClientError::None;
  lastError_[0] = '\0';
}

}  // namespace arrocco::lichess
