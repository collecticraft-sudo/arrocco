// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the pretend lichess.org. See fake_lichess.h.
#include "fake_lichess.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace arrocco_sim {

namespace {

using arrocco::chess::Color;
using arrocco::lichess::LoginStatus;
using arrocco::lichess::Method;
using arrocco::lichess::RequestState;
using arrocco::lichess::Transport;
using arrocco::lichess::WifiStatus;

constexpr char kUser[] = "Tester";
constexpr char kUserId[] = "tester";
constexpr char kNetwork[] = "Casa";
constexpr char kLoginUrl[] = "http://192.168.1.50/login";
constexpr uint32_t kKeepAliveMs = 7000;       // what Lichess does on its streams
constexpr uint32_t kAutoPortalMs = 8000;      // the phone joins and saves a network
constexpr uint32_t kAutoLoginMs = 6000;       // the phone signs in and authorises
constexpr uint32_t kConnectMs = 1500;         // joining the stored network
constexpr uint32_t kExchangeMs = 700;         // the board swaps the code for a token
constexpr uint32_t kAiReplyMs = 800;
constexpr uint32_t kFriendAcceptMs = 3000;
constexpr size_t kLogMax = 64;

bool startsWith(const std::string& s, const char* prefix) { return s.compare(0, std::strlen(prefix), prefix) == 0; }

std::string formValue(const std::string& body, const char* key) {
  const std::string k = std::string(key) + "=";
  size_t at = 0;
  while (at <= body.size()) {
    const size_t end = body.find('&', at);
    const std::string pair = body.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (startsWith(pair, k.c_str())) return pair.substr(k.size());
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return "";
}

std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<unsigned char>(c) < 0x20) continue;
    out += c;
  }
  return out + "\"";
}

const char* colorName(Color c) { return c == Color::White ? "white" : "black"; }

const char* wifiName(WifiStatus w) {
  switch (w) {
    case WifiStatus::NoNetwork:  return "none";
    case WifiStatus::Off:        return "off";
    case WifiStatus::Connecting: return "connecting";
    case WifiStatus::Online:     return "online";
    case WifiStatus::Failed:     return "failed";
    case WifiStatus::Portal:     return "portal";
  }
  return "?";
}

const char* loginName(LoginStatus l) {
  switch (l) {
    case LoginStatus::Idle:       return "idle";
    case LoginStatus::Waiting:    return "waiting";
    case LoginStatus::Exchanging: return "exchanging";
    case LoginStatus::Done:       return "done";
    case LoginStatus::Failed:     return "failed";
  }
  return "?";
}

std::string speedOf(int limit, int increment) {
  const int total = limit + 40 * increment;
  if (total < 180) return "bullet";
  if (total < 480) return "blitz";
  if (total < 1500) return "rapid";
  return "classical";
}

}  // namespace

FakeLichess::FakeLichess(arrocco::Platform& platform) : platform_(platform) {
  // Interactive: one invitation waits, as if a friend had sent it before the board came on.
  incoming_.push_back(Challenge{"inv00001", "amico", 600, 5, false});
}

uint32_t FakeLichess::millis() const { return platform_.millis(); }

// ---- the passage of time ------------------------------------------------------------------------

void FakeLichess::update() const { const_cast<FakeLichess*>(this)->step(platform_.millis()); }

void FakeLichess::step(uint32_t now) {
  // The board's network.
  if (wifiDue_ && static_cast<int32_t>(now - wifiDueMs_) >= 0) {
    wifiDue_ = false;
    if (wifi_ == WifiStatus::Portal) {          // the phone saved a network
      networkStored_ = true;
      wifi_ = WifiStatus::Connecting;
      wifiDue_ = true;
      wifiDueMs_ = now + kConnectMs;
    } else if (wifi_ == WifiStatus::Connecting) {
      wifi_ = WifiStatus::Online;
    }
  }
  // The login.
  if (loginDue_ && static_cast<int32_t>(now - loginDueMs_) >= 0) {
    loginDue_ = false;
    if (login_ == LoginStatus::Waiting) {
      login_ = LoginStatus::Exchanging;
      loginDue_ = true;
      loginDueMs_ = now + kExchangeMs;
    } else if (login_ == LoginStatus::Exchanging) {
      login_ = LoginStatus::Done;
      linked_ = true;
      tokenGood_ = true;
    }
  }
  // The request in flight answers once its delay is over.
  if (request_.active && state_ == RequestState::Busy && now - request_.startMs >= delayMs_) {
    if (request_.fail) {
      state_ = RequestState::Failed;
    } else {
      length_ = static_cast<int>(request_.body.size());
      if (request_.out != nullptr && request_.outSize > 0) {
        if (length_ > request_.outSize - 1) length_ = request_.outSize - 1;
        std::memcpy(request_.out, request_.body.data(), static_cast<size_t>(length_));
        request_.out[length_] = '\0';
      }
      status_ = request_.status;
      state_ = RequestState::Done;
    }
  }
  // The game: the computer's answer, a friend's acceptance, a flag that falls.
  if (aiDue_ && static_cast<int32_t>(now - aiDueMs_) >= 0) {
    aiDue_ = false;
    if (active_ && !over_ && aiToMove()) {
      arrocco::chess::MoveList legal;
      game_.position().generateLegalMoves(legal);
      if (!legal.empty()) {
        char uci[arrocco::chess::kUciBufferSize];
        legal[(game_.plyCount() * 7 + 3) % legal.size()].toUci(uci);
        play(uci, now);
      }
    }
  }
  if (outgoingOpen_ && outgoingAlive_ && auto_ && static_cast<int32_t>(now - friendDueMs_) >= 0) acceptOutgoing(now);
  if (active_ && !over_ && game_.plyCount() >= 2) {
    const bool whiteToMove = game_.position().sideToMove() == Color::White;
    const int32_t left = whiteToMove ? wtime_ : btime_;
    if (static_cast<int64_t>(now - lastMoveMs_) >= left) {
      (whiteToMove ? wtime_ : btime_) = 0;
      endGame("outoftime", whiteToMove ? "black" : "white");
    }
  }
  // Keep-alives on the streams that have them.
  for (Stream& s : streams_) {
    if (!s.open || s.dead || s.kind == Kind::Challenge) continue;
    if (now - s.keepAliveMs >= kKeepAliveMs) {
      s.keepAliveMs = now;
      s.data += "\n";
    }
  }
}

// ---- JSON as Lichess writes it ---------------------------------------------------------------------

std::string FakeLichess::accountJson() const {
  return std::string("{\"id\":\"") + kUserId + "\",\"username\":\"" + kUser +
         "\",\"perfs\":{\"rapid\":{\"games\":12,\"rating\":1500}},\"url\":\"https://lichess.org/@/" + kUserId + "\"}";
}

std::string FakeLichess::challengeJson(const Challenge& c, bool toUs) const {
  const std::string challenger = toUs ? c.user : kUser;
  const std::string dest = toUs ? kUser : c.user;
  char clock[96];
  std::snprintf(clock, sizeof clock, "{\"type\":\"clock\",\"limit\":%d,\"increment\":%d,\"show\":\"%d+%d\"}", c.limit,
                c.increment, c.limit / 60, c.increment);
  return "{\"id\":" + quoted(c.id) + ",\"url\":\"https://lichess.org/" + c.id +
         "\",\"status\":\"created\",\"challenger\":{\"id\":" + quoted(challenger) + ",\"name\":" + quoted(challenger) +
         "},\"destUser\":{\"id\":" + quoted(dest) + ",\"name\":" + quoted(dest) +
         "},\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},\"rated\":" + (c.rated ? "true" : "false") +
         ",\"speed\":\"" + speedOf(c.limit, c.increment) + "\",\"timeControl\":" + clock +
         ",\"color\":\"random\",\"compat\":{\"bot\":false,\"board\":true}}";
}

std::string FakeLichess::gameStartJson() const {
  char buffer[640];
  const bool ourTurn = game_.position().sideToMove() == us_;
  std::string opponent = ai_ ? "{\"id\":null,\"username\":\"Stockfish level " + std::to_string(aiLevel_) +
                                   "\",\"ai\":" + std::to_string(aiLevel_) + "}"
                             : "{\"id\":" + quoted(opponent_) + ",\"username\":" + quoted(opponent_) + "}";
  std::snprintf(buffer, sizeof buffer,
                "{\"gameId\":\"%s\",\"fullId\":\"%sxxxx\",\"color\":\"%s\",\"isMyTurn\":%s,\"opponent\":%s,"
                "\"rated\":%s,\"speed\":\"%s\",\"source\":\"%s\",\"status\":{\"id\":%d,\"name\":\"%s\"}%s%s%s,"
                "\"secondsLeft\":%d,\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},"
                "\"compat\":{\"bot\":false,\"board\":true}}",
                gameId_.c_str(), gameId_.c_str(), colorName(us_), ourTurn ? "true" : "false", opponent.c_str(),
                rated_ ? "true" : "false", speedOf(limit_, increment_).c_str(), ai_ ? "ai" : "friend",
                over_ ? 30 : 20, statusName_.c_str(), winner_.empty() ? "" : ",\"winner\":\"", winner_.c_str(),
                winner_.empty() ? "" : "\"", static_cast<int>((us_ == Color::White ? wtime_ : btime_) / 1000));
  return buffer;
}

std::string FakeLichess::movesText() const {
  std::string text;
  char uci[arrocco::chess::kUciBufferSize];
  for (int i = 0; i < game_.plyCount(); ++i) {
    game_.moveAt(i).toUci(uci);
    if (!text.empty()) text += ' ';
    text += uci;
  }
  return text;
}

std::string FakeLichess::gameStateJson() const {
  char buffer[256];
  std::snprintf(buffer, sizeof buffer,
                "\"wtime\":%d,\"btime\":%d,\"winc\":%d,\"binc\":%d,\"status\":\"%s\"%s%s%s,\"wdraw\":%s,"
                "\"bdraw\":%s",
                static_cast<int>(wtime_), static_cast<int>(btime_), increment_ * 1000, increment_ * 1000,
                statusName_.c_str(), winner_.empty() ? "" : ",\"winner\":\"", winner_.c_str(),
                winner_.empty() ? "" : "\"", wdraw_ ? "true" : "false", bdraw_ ? "true" : "false");
  return "{\"type\":\"gameState\",\"moves\":\"" + movesText() + "\"," + buffer + "}";
}

std::string FakeLichess::gameFullJson() const {
  const std::string me = std::string("{\"id\":\"") + kUserId + "\",\"name\":\"" + kUser + "\",\"rating\":1500}";
  const std::string them = ai_ ? "{\"aiLevel\":" + std::to_string(aiLevel_) + "}"
                               : "{\"id\":" + quoted(opponent_) + ",\"name\":" + quoted(opponent_) + ",\"rating\":1450}";
  char clock[96];
  std::snprintf(clock, sizeof clock, "{\"initial\":%d,\"increment\":%d}", limit_ * 1000, increment_ * 1000);
  return "{\"type\":\"gameFull\",\"id\":" + quoted(gameId_) +
         ",\"variant\":{\"key\":\"standard\",\"name\":\"Standard\",\"short\":\"Std\"},\"clock\":" + clock +
         ",\"speed\":\"" + speedOf(limit_, increment_) + "\",\"perf\":{\"name\":\"Rapid\"},\"rated\":" +
         (rated_ ? "true" : "false") + ",\"createdAt\":1759000000000,\"white\":" + (us_ == Color::White ? me : them) +
         ",\"black\":" + (us_ == Color::White ? them : me) + ",\"initialFen\":\"startpos\",\"state\":" +
         gameStateJson() + "}";
}

int FakeLichess::stream(Kind kind) const {
  for (size_t i = 0; i < streams_.size(); ++i)
    if (streams_[i].open && !streams_[i].dead && streams_[i].kind == kind) return static_cast<int>(i);
  return -1;
}

void FakeLichess::pushEvent(const std::string& line) {
  const int i = stream(Kind::Event);
  if (i >= 0) streams_[static_cast<size_t>(i)].data += line + "\n";
}

void FakeLichess::pushGame(const std::string& line) {
  for (Stream& s : streams_) {
    if (s.open && !s.dead && s.kind == Kind::Game && s.path == "/api/board/game/stream/" + gameId_) s.data += line + "\n";
  }
}

// ---- the game ---------------------------------------------------------------------------------------------

void FakeLichess::startGame(const std::string& id, bool ai, int level, const std::string& opponent, Color us, int limit,
                            int increment, bool rated) {
  active_ = true;
  over_ = false;
  gameId_ = id;
  ai_ = ai;
  aiLevel_ = level;
  opponent_ = opponent;
  us_ = us;
  rated_ = rated;
  limit_ = limit;
  increment_ = increment;
  wtime_ = btime_ = limit * 1000;
  lastMoveMs_ = platform_.millis();
  statusName_ = "started";
  winner_.clear();
  wdraw_ = bdraw_ = false;
  game_.newGame();
  pushEvent("{\"type\":\"gameStart\",\"game\":" + gameStartJson() + "}");
  scheduleAi(platform_.millis());
}

bool FakeLichess::aiToMove() const { return ai_ && game_.position().sideToMove() != us_; }

void FakeLichess::scheduleAi(uint32_t now) {
  if (!auto_ || !active_ || over_ || !aiToMove()) return;
  aiDue_ = true;
  aiDueMs_ = now + kAiReplyMs;
}

bool FakeLichess::play(const char* uci, uint32_t now) {
  if (!active_ || over_) return false;
  const Color mover = game_.position().sideToMove();
  if (!game_.playUci(uci)) return false;
  // The clock starts with Black's first move, as on Lichess: no time is charged before it.
  if (game_.plyCount() > 2) {
    int32_t& left = mover == Color::White ? wtime_ : btime_;
    const uint32_t spent = now - lastMoveMs_;
    left = static_cast<int64_t>(spent) >= left ? 0 : left - static_cast<int32_t>(spent);
    left += increment_ * 1000;
  }
  lastMoveMs_ = now;
  // A move answers a draw offer with "no".
  if (mover == Color::White) bdraw_ = false;
  else wdraw_ = false;
  if (game_.isOver() && (game_.reason() == arrocco::chess::GameEndReason::Checkmate ||
                         game_.reason() == arrocco::chess::GameEndReason::Stalemate)) {
    const bool mate = game_.reason() == arrocco::chess::GameEndReason::Checkmate;
    endGame(mate ? "mate" : "stalemate", mate ? colorName(mover) : nullptr);
    return true;
  }
  pushGame(gameStateJson());
  scheduleAi(now);
  return true;
}

void FakeLichess::endGame(const char* status, const char* winner) {
  if (!active_ || over_) return;
  over_ = true;
  statusName_ = status;
  winner_ = winner != nullptr ? winner : "";
  aiDue_ = false;
  pushGame(gameStateJson());
  pushEvent("{\"type\":\"gameFinish\",\"game\":" + gameStartJson() + "}");
  // Lichess ends the game stream after the last state.
  for (Stream& s : streams_)
    if (s.open && s.kind == Kind::Game) s.dead = true;
}

// The friend accepts: the challenger plays White here (the board asks for a random colour).
void FakeLichess::acceptOutgoing(uint32_t now) {
  (void)now;
  if (!outgoingOpen_) return;
  outgoingOpen_ = false;
  outgoingAlive_ = false;
  for (Stream& s : streams_) {
    if (s.open && s.kind == Kind::Challenge) {
      s.data += "{\"done\":\"accepted\"}\n";
      s.dead = true;
    }
  }
  startGame(outgoing_.id, false, 0, outgoing_.user, Color::White, outgoing_.limit, outgoing_.increment,
            outgoing_.rated);
}

void FakeLichess::declineOutgoing() {
  if (!outgoingOpen_) return;
  outgoingOpen_ = false;
  outgoingAlive_ = false;
  for (Stream& s : streams_) {
    if (s.open && s.kind == Kind::Challenge) {
      s.data += "{\"done\":\"declined\"}\n";
      s.dead = true;
    }
  }
  pushEvent("{\"type\":\"challengeDeclined\",\"challenge\":" + challengeJson(outgoing_, false) + "}");
}

// ---- requests ---------------------------------------------------------------------------------------------------

void FakeLichess::respond(Method method, const std::string& path, const std::string& body, int& status,
                          std::string& reply) {
  const uint32_t now = platform_.millis();
  status = 200;
  reply = "{\"ok\":true}";
  if (!linked_ || !tokenGood_) {
    status = 401;
    reply = "{\"error\":\"No such token\"}";
    return;
  }
  if (method == Method::Get && path == "/api/account") {
    reply = accountJson();
    return;
  }
  const std::string game = "/api/board/game/" + gameId_ + "/";
  if (method == Method::Post && path == "/api/challenge/ai") {
    // No colour asked for: Lichess draws it, and here it always draws Black for us.
    const std::string colour = formValue(body, "color");
    const Color us = colour == "white" ? Color::White : Color::Black;
    const int level = std::atoi(formValue(body, "level").c_str());
    const int limit = std::atoi(formValue(body, "clock.limit").c_str());
    const int increment = std::atoi(formValue(body, "clock.increment").c_str());
    char id[16];
    std::snprintf(id, sizeof id, "fakeAi%02d", ++counter_ % 100);
    startGame(id, true, level < 1 ? 1 : level, "", us, limit, increment, false);
    reply = std::string("{\"id\":\"") + id + "\",\"rated\":false,\"variant\":{\"key\":\"standard\"},\"speed\":\"" +
            speedOf(limit, increment) + "\",\"status\":\"started\",\"player\":\"" + colorName(us) + "\"}";
    return;
  }
  if (method == Method::Post && startsWith(path, "/api/challenge/")) {
    const std::string rest = path.substr(std::strlen("/api/challenge/"));
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) {
      const std::string id = rest.substr(0, slash);
      const std::string action = rest.substr(slash + 1);
      if (action == "cancel" && outgoingOpen_ && outgoing_.id == id) {
        outgoingOpen_ = false;
        outgoingAlive_ = false;
        for (Stream& s : streams_)
          if (s.open && s.kind == Kind::Challenge) {
            s.data += "{\"done\":\"canceled\"}\n";
            s.dead = true;
          }
        pushEvent("{\"type\":\"challengeCanceled\",\"challenge\":" + challengeJson(outgoing_, false) + "}");
        return;
      }
      for (size_t i = 0; i < incoming_.size(); ++i) {
        if (incoming_[i].id != id) continue;
        const Challenge c = incoming_[i];
        incoming_.erase(incoming_.begin() + static_cast<long>(i));
        if (action == "accept") {
          // The challenger asked for a random colour: Lichess gives us Black here.
          startGame(c.id, false, 0, c.user, Color::Black, c.limit, c.increment, c.rated);
        }
        return;
      }
      status = 404;
      reply = "{\"error\":\"Challenge not found\"}";
      return;
    }
    status = 400;   // a plain-request challenge: the board always keeps them alive instead
    reply = "{\"error\":\"use keepAliveStream\"}";
    return;
  }
  if (method == Method::Post && active_ && startsWith(path, game.c_str())) {
    const std::string action = path.substr(game.size());
    if (over_) {
      status = 400;
      reply = "{\"error\":\"Not your turn, or game already over\"}";
      return;
    }
    if (startsWith(action, "move/")) {
      const std::string uci = action.substr(5);
      if (game_.position().sideToMove() != us_ || !play(uci.c_str(), now)) {
        status = 400;
        reply = "{\"error\":\"Not your turn, or game already over\"}";
      }
      return;
    }
    if (action == "resign") {
      endGame("resign", colorName(arrocco::chess::opposite(us_)));
      return;
    }
    if (action == "abort") {
      if (game_.plyCount() >= 2) {
        status = 400;
        reply = "{\"error\":\"This game cannot be aborted\"}";
        return;
      }
      endGame("aborted", nullptr);
      return;
    }
    if (action == "draw/yes") {
      const bool offeredToUs = us_ == Color::White ? bdraw_ : wdraw_;
      if (offeredToUs) {
        endGame("draw", nullptr);
        return;
      }
      if (!ai_) {   // the computer never takes a draw: the offer simply lapses
        (us_ == Color::White ? wdraw_ : bdraw_) = true;
        pushGame(gameStateJson());
      }
      return;
    }
    if (action == "draw/no") {
      (us_ == Color::White ? bdraw_ : wdraw_) = false;
      pushGame(gameStateJson());
      return;
    }
    if (action == "claim-victory") {
      endGame("timeout", colorName(us_));
      return;
    }
  }
  status = 404;
  reply = "{\"error\":\"Not found\"}";
}

bool FakeLichess::beginRequest(Method method, const char* path, const char* body, char* out, int outSize) {
  update();
  if (request_.active) return false;
  if (path == nullptr) return false;
  request_ = Request();
  request_.active = true;
  request_.startMs = platform_.millis();
  request_.out = out;
  request_.outSize = outSize;
  if (out != nullptr && outSize > 0) out[0] = '\0';
  status_ = 0;
  length_ = 0;
  state_ = RequestState::Busy;
  const std::string p = path;
  std::string line = std::string(method == Method::Post ? "POST " : "GET ") + p + " -> ";
  if (offline_) {
    request_.fail = true;
    error_ = "offline: the network did not come up";
    line += "lost";
  } else if (nextStatus_ != 0) {
    // Refused before Lichess looked at it: nothing happens to the game.
    request_.status = nextStatus_;
    request_.body = nextStatus_ == 429 ? "{\"error\":\"Too many requests. Try again later.\"}"
                                       : "{\"error\":\"Something went wrong\"}";
    line += std::to_string(nextStatus_);
    nextStatus_ = 0;
  } else {
    // Lichess acts on a request when it arrives; a slow network only delays the answer.
    respond(method, p, body != nullptr ? body : "", request_.status, request_.body);
    line += std::to_string(request_.status);
  }
  log_.push_back(line);
  if (log_.size() > kLogMax) log_.erase(log_.begin());
  step(platform_.millis());
  return true;
}

RequestState FakeLichess::requestState() const {
  update();
  return request_.active ? state_ : RequestState::Idle;
}

void FakeLichess::endRequest() {
  request_ = Request();
  state_ = RequestState::Idle;
  status_ = 0;
  length_ = 0;
}

// ---- streams --------------------------------------------------------------------------------------------------

int FakeLichess::openStream(const char* path) {
  update();
  streamStatus_ = 0;
  if (path == nullptr) return kNoStream;
  const std::string p = path;
  if (offline_) {
    error_ = "offline: the network did not come up";
    log_.push_back("STREAM " + p + " -> lost");
    return kNoStream;
  }
  if (nextStatus_ == 429) {
    nextStatus_ = 0;
    streamStatus_ = 429;
    error_ = "HTTP 429 Too many requests";
    log_.push_back("STREAM " + p + " -> 429");
    return kNoStream;
  }
  if (!linked_ || !tokenGood_) {
    streamStatus_ = 401;
    error_ = "HTTP 401 No such token";
    log_.push_back("STREAM " + p + " -> 401");
    return kNoStream;
  }
  Stream s;
  s.open = true;
  s.path = p;
  s.keepAliveMs = platform_.millis();
  if (p == "/api/stream/event") {
    s.kind = Kind::Event;
    // On connect Lichess replays what is going on: the games and the open challenges.
    if (active_ && !over_) s.data += "{\"type\":\"gameStart\",\"game\":" + gameStartJson() + "}\n";
    for (const Challenge& c : incoming_) s.data += "{\"type\":\"challenge\",\"challenge\":" + challengeJson(c, true) + "}\n";
  } else if (active_ && p == "/api/board/game/stream/" + gameId_) {
    s.kind = Kind::Game;
    s.data += gameFullJson() + "\n";
    if (over_) s.dead = true;
  } else {
    streamStatus_ = 404;
    error_ = "HTTP 404 No such game";
    log_.push_back("STREAM " + p + " -> 404");
    return kNoStream;
  }
  log_.push_back("STREAM " + p + " -> 200");
  for (size_t i = 0; i < streams_.size(); ++i) {
    if (!streams_[i].open) {
      streams_[i] = s;
      return static_cast<int>(i) + 1;
    }
  }
  streams_.push_back(s);
  return static_cast<int>(streams_.size());
}

int FakeLichess::openPostStream(const char* path, const char* body) {
  update();
  streamStatus_ = 0;
  if (path == nullptr) return kNoStream;
  const std::string p = path;
  log_.push_back("POST-STREAM " + p);
  if (offline_) {
    error_ = "offline: the network did not come up";
    return kNoStream;
  }
  if (!linked_ || !tokenGood_) {
    streamStatus_ = 401;
    error_ = "No such token";
    return kNoStream;
  }
  if (!startsWith(p, "/api/challenge/")) {
    streamStatus_ = 404;
    error_ = "Not found";
    return kNoStream;
  }
  const std::string user = p.substr(std::strlen("/api/challenge/"));
  if (startsWith(user, "nobody")) {
    streamStatus_ = 400;
    error_ = "No such user";
    return kNoStream;
  }
  const std::string form = body != nullptr ? body : "";
  outgoing_ = Challenge();
  char id[16];
  std::snprintf(id, sizeof id, "chal%04d", ++counter_ % 10000);
  outgoing_.id = id;
  outgoing_.user = user;
  outgoing_.limit = std::atoi(formValue(form, "clock.limit").c_str());
  outgoing_.increment = std::atoi(formValue(form, "clock.increment").c_str());
  outgoing_.rated = formValue(form, "rated") == "true";
  outgoingOpen_ = true;
  outgoingAlive_ = true;
  friendDueMs_ = platform_.millis() + kFriendAcceptMs;
  Stream s;
  s.open = true;
  s.kind = Kind::Challenge;
  s.path = p;
  s.data = "{\"challenge\":" + challengeJson(outgoing_, false) + ",\"socketVersion\":0}\n";
  pushEvent("{\"type\":\"challenge\",\"challenge\":" + challengeJson(outgoing_, false) + "}");
  for (size_t i = 0; i < streams_.size(); ++i) {
    if (!streams_[i].open) {
      streams_[i] = s;
      return static_cast<int>(i) + 1;
    }
  }
  streams_.push_back(s);
  return static_cast<int>(streams_.size());
}

int FakeLichess::readStream(int id, char* out, int outSize) {
  update();
  if (id < 1 || id > static_cast<int>(streams_.size()) || out == nullptr || outSize <= 0) return -1;
  Stream& s = streams_[static_cast<size_t>(id - 1)];
  if (!s.open) return -1;
  const size_t left = s.data.size() - s.taken;
  if (left == 0) return s.dead ? -1 : 0;
  const size_t n = left < static_cast<size_t>(outSize) ? left : static_cast<size_t>(outSize);
  std::memcpy(out, s.data.data() + s.taken, n);
  s.taken += n;
  if (s.taken == s.data.size()) {
    s.data.clear();
    s.taken = 0;
  }
  return static_cast<int>(n);
}

void FakeLichess::closeStream(int id) {
  if (id < 1 || id > static_cast<int>(streams_.size())) return;
  Stream& s = streams_[static_cast<size_t>(id - 1)];
  // Closing the connection that keeps a challenge alive lets it lapse: nobody can accept it any
  // more, though a cancel that follows still finds it.
  if (s.open && s.kind == Kind::Challenge) outgoingAlive_ = false;
  s = Stream();
}

// ---- the account ------------------------------------------------------------------------------------------------

WifiStatus FakeLichess::wifiStatus() {
  update();
  return wifi_;
}

const char* FakeLichess::wifiDetail() {
  update();
  switch (wifi_) {
    case WifiStatus::NoNetwork:  detail_ = "radio off, no network stored"; break;
    case WifiStatus::Off:        detail_ = std::string("radio off, '") + kNetwork + "' stored"; break;
    case WifiStatus::Connecting: detail_ = std::string("Joining '") + kNetwork + "'"; break;
    case WifiStatus::Online:     detail_ = std::string(kNetwork) + " - 192.168.1.50"; break;
    case WifiStatus::Failed:     detail_ = std::string("wrong password (") + kNetwork + ")"; break;
    case WifiStatus::Portal:     detail_ = std::string("Join '") + portalName() + "', then open any page"; break;
  }
  return detail_.c_str();
}

void FakeLichess::openPortal() {
  ++portalOpens_;
  wifi_ = WifiStatus::Portal;
  wifiDue_ = auto_;
  wifiDueMs_ = platform_.millis() + kAutoPortalMs;
}

void FakeLichess::closePortal() {
  if (wifi_ != WifiStatus::Portal) return;
  wifi_ = networkStored_ ? WifiStatus::Off : WifiStatus::NoNetwork;
  wifiDue_ = false;
}

void FakeLichess::wantNetwork() {
  update();
  if (wifi_ == WifiStatus::Off || wifi_ == WifiStatus::Failed) {
    if (wifi_ == WifiStatus::Failed && !auto_) return;   // a script decides when it works again
    wifi_ = WifiStatus::Connecting;
    wifiDue_ = true;
    wifiDueMs_ = platform_.millis() + kConnectMs;
  }
}

void FakeLichess::retryNetwork() {
  update();
  if (wifi_ != WifiStatus::Failed) return;
  wifi_ = WifiStatus::Connecting;
  wifiDue_ = true;
  wifiDueMs_ = platform_.millis() + kConnectMs;
}

bool FakeLichess::linked() {
  update();
  return linked_;
}

bool FakeLichess::beginLogin(char* url, int urlSize) {
  update();
  ++logins_;
  if (url == nullptr || urlSize <= 0) return false;
  url[0] = '\0';
  if (wifi_ != WifiStatus::Online) {
    login_ = LoginStatus::Failed;
    loginError_ = "the board is not online";
    return false;
  }
  std::snprintf(url, static_cast<size_t>(urlSize), "%s", kLoginUrl);
  login_ = LoginStatus::Waiting;
  loginError_.clear();
  loginDue_ = auto_;
  loginDueMs_ = platform_.millis() + kAutoLoginMs;
  return true;
}

LoginStatus FakeLichess::loginStatus() {
  update();
  return login_;
}

void FakeLichess::cancelLogin() {
  login_ = LoginStatus::Idle;
  loginDue_ = false;
  loginError_.clear();
}

void FakeLichess::unlink() {
  linked_ = false;
  login_ = LoginStatus::Idle;
}

// ---- the script -----------------------------------------------------------------------------------------------------

bool FakeLichess::command(const char* text, std::string& error) {
  if (text == nullptr || std::strncmp(text, "lichess ", 8) != 0) return false;
  update();
  char verb[16] = {};
  char a[48] = {};
  char b[48] = {};
  long n1 = 0, n2 = 0, n3 = 0;
  const char* args = text + 8;
  if (std::sscanf(args, "%15s", verb) != 1) {
    error = "lichess: what?";
    return true;
  }
  const std::string v = verb;
  const uint32_t now = platform_.millis();
  if (v == "auto" && std::sscanf(args, "%*s %47s", a) == 1) {
    auto_ = std::strcmp(a, "on") == 0;
    if (!auto_) {
      wifiDue_ = loginDue_ = aiDue_ = false;
      incoming_.clear();   // the waiting invitation is part of the demo, not of a script
    } else {
      scheduleAi(now);
    }
  } else if (v == "wifi" && std::sscanf(args, "%*s %47s", a) == 1) {
    const std::string w = a;
    wifiDue_ = false;
    if (w == "none") {
      wifi_ = WifiStatus::NoNetwork;
      networkStored_ = false;
    } else if (w == "off") {
      wifi_ = WifiStatus::Off;
      networkStored_ = true;
    } else if (w == "online") {
      wifi_ = WifiStatus::Online;
      networkStored_ = true;
    } else if (w == "failed") {
      wifi_ = WifiStatus::Failed;
      networkStored_ = true;
    } else if (w == "portal") {
      openPortal();
    } else if (w == "join") {
      if (wifi_ != WifiStatus::Portal) {
        error = "lichess wifi join: the portal is not open";
        return true;
      }
      networkStored_ = true;
      wifi_ = WifiStatus::Connecting;
      wifiDue_ = true;
      wifiDueMs_ = now + kConnectMs;
    } else {
      error = "lichess wifi: none|off|online|failed|portal|join";
    }
  } else if (v == "login" && std::sscanf(args, "%*s %47s", a) == 1) {
    if (login_ != LoginStatus::Waiting) {
      error = "lichess login: no code on the screen";
      return true;
    }
    if (std::strcmp(a, "approve") == 0) {
      login_ = LoginStatus::Exchanging;
      loginDue_ = true;
      loginDueMs_ = now + kExchangeMs;
    } else {
      login_ = LoginStatus::Failed;
      loginDue_ = false;
      loginError_ = "refused on Lichess";
    }
  } else if (v == "linked" && std::sscanf(args, "%*s %ld", &n1) == 1) {
    linked_ = n1 != 0;
  } else if (v == "token" && std::sscanf(args, "%*s %47s", a) == 1) {
    tokenGood_ = std::strcmp(a, "good") == 0;
  } else if (v == "next" && std::sscanf(args, "%*s %ld", &n1) == 1) {
    nextStatus_ = static_cast<int>(n1);
  } else if (v == "delay" && std::sscanf(args, "%*s %ld", &n1) == 1) {
    delayMs_ = n1 < 0 ? 0 : static_cast<uint32_t>(n1);
  } else if (v == "offline" && std::sscanf(args, "%*s %ld", &n1) == 1) {
    offline_ = n1 != 0;
  } else if (v == "drop" && std::sscanf(args, "%*s %47s", a) == 1) {
    const Kind kind = std::strcmp(a, "event") == 0 ? Kind::Event : std::strcmp(a, "game") == 0 ? Kind::Game
                                                                                                : Kind::Challenge;
    bool any = false;
    for (Stream& s : streams_)
      if (s.open && s.kind == kind && !s.dead) {
        s.dead = true;
        any = true;
      }
    if (!any) error = std::string("lichess drop: no open ") + a + " stream";
  } else if (v == "opp" && std::sscanf(args, "%*s %47s", a) == 1) {
    if (!active_ || over_ || game_.position().sideToMove() == us_) {
      error = "lichess opp: it is not the opponent's turn";
      return true;
    }
    if (!play(a, now)) error = std::string("lichess opp: not a legal move: ") + a;
  } else if (v == "invite" && std::sscanf(args, "%*s %47s %47s %ld %ld %ld", a, b, &n1, &n2, &n3) == 5) {
    Challenge c{a, b, static_cast<int>(n1), static_cast<int>(n2), n3 != 0};
    incoming_.push_back(c);
    pushEvent("{\"type\":\"challenge\",\"challenge\":" + challengeJson(c, true) + "}");
  } else if (v == "withdraw" && std::sscanf(args, "%*s %47s", a) == 1) {
    bool found = false;
    for (size_t i = 0; i < incoming_.size(); ++i) {
      if (incoming_[i].id != a) continue;
      pushEvent("{\"type\":\"challengeCanceled\",\"challenge\":" + challengeJson(incoming_[i], true) + "}");
      incoming_.erase(incoming_.begin() + static_cast<long>(i));
      found = true;
      break;
    }
    if (!found) error = std::string("lichess withdraw: no invitation ") + a;
  } else if (v == "friend" && std::sscanf(args, "%*s %47s", a) == 1) {
    if (!outgoingOpen_ || !outgoingAlive_) {
      error = "lichess friend: no challenge of ours is open";
      return true;
    }
    if (std::strcmp(a, "accept") == 0) acceptOutgoing(now);
    else declineOutgoing();
  } else if (v == "draw" && std::sscanf(args, "%*s %47s", a) == 1) {
    if (!active_ || over_) {
      error = "lichess draw: no game";
      return true;
    }
    const std::string d = a;
    bool& ours = us_ == Color::White ? wdraw_ : bdraw_;
    bool& theirs = us_ == Color::White ? bdraw_ : wdraw_;
    if (d == "offer") {
      theirs = true;
      pushGame(gameStateJson());
    } else if (d == "accept" && ours) {
      endGame("draw", nullptr);
    } else if (d == "decline" && ours) {
      ours = false;
      pushGame(gameStateJson());
    } else {
      error = "lichess draw: offer, or accept/decline an offer of ours";
    }
  } else if (v == "gone" && std::sscanf(args, "%*s %ld", &n1) == 1) {
    pushGame("{\"type\":\"opponentGone\",\"gone\":true,\"claimWinInSeconds\":" + std::to_string(n1) + "}");
  } else if (v == "back") {
    pushGame("{\"type\":\"opponentGone\",\"gone\":false}");
  } else if (v == "end" && std::sscanf(args, "%*s %47s", a) == 1) {
    const int got = std::sscanf(args, "%*s %*s %47s", b);
    endGame(a, got == 1 ? b : nullptr);
  } else if (v == "state") {
    // Nothing to change: main.cpp emits stateJson() for it.
  } else {
    error = std::string("lichess: unknown line: ") + text;
  }
  step(platform_.millis());
  return true;
}

std::string FakeLichess::stateJson() {
  update();
  std::string json = "{\"ev\":\"lichess\",\"wifi\":\"" + std::string(wifiName(wifi_)) + "\",\"linked\":" +
                     (linked_ ? "true" : "false") + ",\"login\":\"" + loginName(login_) +
                     "\",\"portal_opens\":" + std::to_string(portalOpens_) + ",\"logins\":" + std::to_string(logins_) +
                     ",\"game\":" + quoted(active_ ? gameId_ : "") + ",\"moves\":" + quoted(movesText()) +
                     ",\"status\":" + quoted(active_ ? statusName_ : "") + ",\"winner\":" + quoted(winner_) +
                     ",\"us\":\"" + colorName(us_) + "\",\"outgoing\":" + quoted(outgoingOpen_ ? outgoing_.id : "") +
                     ",\"incoming\":[";
  for (size_t i = 0; i < incoming_.size(); ++i) json += (i ? "," : "") + quoted(incoming_[i].id);
  json += "],\"streams\":[";
  bool first = true;
  for (const Stream& s : streams_) {
    if (!s.open) continue;
    json += (first ? "" : ",") + quoted(s.path);
    first = false;
  }
  json += "],\"log\":[";
  for (size_t i = 0; i < log_.size(); ++i) json += (i ? "," : "") + quoted(log_[i]);
  json += "]}";
  return json;
}

}  // namespace arrocco_sim
