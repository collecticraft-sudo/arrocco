// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — a pretend lichess.org inside the simulator: no network, no account, no
// token. arrocco-sim --fake-lichess runs the real Lichess screens against it.
//
// It is both seams at once: the Transport the client talks to (requests and ndjson streams, with
// the same shapes the real API sends) and the Account (Wi-Fi and login) the screens ask. It plays
// a small but honest Lichess: games with real rules (arrocco::chess), clocks that run, a
// Stockfish that answers with a legal move, friends who accept, invitations, draws, 429s, a 401,
// streams that drop, requests that are slow or lost.
//
// Two ways to drive it:
//   - by itself (the default, for trying it out in the browser): it starts as a board fresh from
//     the factory, no network stored and no account; the phone joins the setup network 8 s
//     after the portal opens, the login is approved 6 s after the code appears, the computer
//     answers 0.8 s after each move, a friend accepts 3 s after the challenge, and one
//     invitation from "amico" is waiting;
//   - from a script: "lichess auto off" stops all of that, and the lines below do it by hand.
//
// The script's lines (protocol.h), each answered by nothing, or by an error event:
//   lichess auto on|off             everything automatic, on or off
//   lichess wifi none|off|online|failed|portal|join    the board's network; join = the phone saved one
//   lichess login approve|refuse    the phone finishes the login
//   lichess linked 0|1              a token is stored, or not
//   lichess token good|bad          bad: Lichess answers 401 to everything
//   lichess next STATUS             the next request is answered with this HTTP status (429, 500...)
//   lichess delay MS                every request takes this long (0 = at once)
//   lichess offline 0|1             requests fail and streams do not open, as with no internet
//   lichess drop event|game|challenge   that stream breaks
//   lichess opp UCI                 the opponent plays UCI
//   lichess invite ID USER LIMIT INC RATED   a challenge to us (LIMIT, INC in seconds, RATED 0|1)
//   lichess withdraw ID             the challenger withdraws it
//   lichess friend accept|decline   the friend answers our challenge
//   lichess draw offer|accept|decline   the opponent offers a draw, or answers ours
//   lichess gone SECONDS | lichess back  the opponent left (claim after SECONDS) / came back
//   lichess end STATUS [white|black]    the game ends, as Lichess says
//   lichess state                   emits {"ev":"lichess",...}: what the fake saw (see stateJson)
#pragma once

#include <arrocco/chess/game.h>
#include <arrocco/lichess/account.h>
#include <arrocco/lichess/transport.h>
#include <arrocco/platform.h>

#include <string>
#include <vector>

namespace arrocco_sim {

class FakeLichess final : public arrocco::lichess::Transport, public arrocco::lichess::Account {
 public:
  explicit FakeLichess(arrocco::Platform& platform);

  // ---- the script: false = not a line of ours; `error` says what was wrong with it
  bool command(const char* line, std::string& error);
  std::string stateJson();

  // ---- Transport
  uint32_t millis() const override;
  bool beginRequest(arrocco::lichess::Method method, const char* path, const char* body, char* out,
                    int outSize) override;
  arrocco::lichess::RequestState requestState() const override;
  int responseStatus() const override { return status_; }
  int responseLength() const override { return length_; }
  void endRequest() override;
  int openStream(const char* path) override;
  int readStream(int id, char* out, int outSize) override;
  void closeStream(int id) override;
  bool supportsPostStreams() const override { return true; }
  int openPostStream(const char* path, const char* body) override;
  int lastStreamStatus() const override { return streamStatus_; }
  const char* lastError() const override { return error_.c_str(); }

  // ---- Account
  arrocco::lichess::WifiStatus wifiStatus() override;
  const char* wifiDetail() override;
  const char* portalName() override { return "Arrocco-51A7"; }
  const char* portalAddress() override { return "http://4.3.2.1"; }
  void openPortal() override;
  void closePortal() override;
  void wantNetwork() override;
  void retryNetwork() override;
  bool linked() override;
  bool beginLogin(char* url, int urlSize) override;
  arrocco::lichess::LoginStatus loginStatus() override;
  const char* loginError() override { return loginError_.c_str(); }
  void cancelLogin() override;
  void unlink() override;

 private:
  enum class Kind { Event, Game, Challenge };
  struct Stream {
    bool open = false;
    bool dead = false;            // readStream() answers -1 once `data` is drained
    Kind kind = Kind::Event;
    std::string path;
    std::string data;
    size_t taken = 0;
    uint32_t keepAliveMs = 0;
  };
  struct Challenge {
    std::string id;
    std::string user;
    int limit = 600;
    int increment = 0;
    bool rated = false;
  };
  struct Request {
    bool active = false;
    bool fail = false;
    uint32_t startMs = 0;
    int status = 0;
    std::string body;
    char* out = nullptr;
    int outSize = 0;
  };

  void update() const;            // what time alone does: replies, accepts, keep-alives, the clocks
  void step(uint32_t now);
  void respond(arrocco::lichess::Method method, const std::string& path, const std::string& body, int& status,
               std::string& reply);
  std::string accountJson() const;
  std::string challengeJson(const Challenge& c, bool toUs) const;
  std::string gameStartJson() const;
  std::string gameStateJson() const;
  std::string gameFullJson() const;
  void pushEvent(const std::string& line);
  void pushGame(const std::string& line);
  void startGame(const std::string& id, bool ai, int level, const std::string& opponent, arrocco::chess::Color us,
                 int limit, int increment, bool rated);
  bool play(const char* uci, uint32_t now);   // either side; updates the clocks
  void endGame(const char* status, const char* winner);
  void scheduleAi(uint32_t now);
  bool aiToMove() const;
  void acceptOutgoing(uint32_t now);
  void declineOutgoing();
  int stream(Kind kind) const;     // the open stream of that kind, -1 if none
  std::string movesText() const;

  arrocco::Platform& platform_;
  bool auto_ = true;

  // the account
  arrocco::lichess::WifiStatus wifi_ = arrocco::lichess::WifiStatus::NoNetwork;   // a new board
  bool networkStored_ = false;
  uint32_t wifiDueMs_ = 0;        // the next automatic Wi-Fi step (portal joined, network up)
  bool wifiDue_ = false;
  arrocco::lichess::LoginStatus login_ = arrocco::lichess::LoginStatus::Idle;
  uint32_t loginDueMs_ = 0;
  bool loginDue_ = false;
  std::string loginError_;
  bool linked_ = false;
  bool tokenGood_ = true;
  std::string detail_;

  // the transport
  Request request_;
  arrocco::lichess::RequestState state_ = arrocco::lichess::RequestState::Idle;
  int status_ = 0;
  int length_ = 0;
  int nextStatus_ = 0;
  uint32_t delayMs_ = 0;
  bool offline_ = false;
  int streamStatus_ = 0;
  std::string error_;
  std::vector<Stream> streams_;

  // Lichess
  std::vector<Challenge> incoming_;
  Challenge outgoing_;
  bool outgoingOpen_ = false;     // our challenge exists on Lichess
  bool outgoingAlive_ = false;    // ... and its connection is open: without it nobody accepts
  uint32_t friendDueMs_ = 0;
  int counter_ = 0;
  // the one game
  bool active_ = false;           // a game exists (running or over)
  bool over_ = false;
  std::string gameId_;
  bool ai_ = false;
  int aiLevel_ = 1;
  std::string opponent_;
  arrocco::chess::Color us_ = arrocco::chess::Color::White;
  bool rated_ = false;
  int limit_ = 600, increment_ = 0;
  int32_t wtime_ = 0, btime_ = 0;
  uint32_t lastMoveMs_ = 0;
  std::string statusName_ = "started";
  std::string winner_;
  bool wdraw_ = false, bdraw_ = false;
  bool aiDue_ = false;
  uint32_t aiDueMs_ = 0;
  arrocco::chess::Game game_;

  // what the screens did, for the scripts
  std::vector<std::string> log_;  // "POST /api/board/game/x/move/e2e4 -> 200"
  int portalOpens_ = 0;
  int logins_ = 0;
};

}  // namespace arrocco_sim
