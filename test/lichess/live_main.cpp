// SPDX-License-Identifier: GPL-3.0-or-later
// The LIVE test: the real lichess.org, through sim/server.py's proxy, with the real account.
//
// It checks /api/account, starts an UNRATED game against the Lichess AI at level 1 with a short
// clock, plays real moves chosen by our own rules library until the game ends or twelve plies
// have been played, then RESIGNS and waits for the server to confirm the game is over.
//
// It is started by live_run.py, which owns the proxy and skips everything when no token is there.
// This program never sees the token: it only ever talks to 127.0.0.1.
//
// Whatever goes wrong, the game is resigned before this returns: the guard is armed before the
// challenge goes out and runs on every path out of main().
#include <cstdio>
#include <cstring>
#include <ctime>

#include "arrocco/chess/game.h"
#include "arrocco/lichess/client.h"
#include "lichess_transport.h"

using arrocco::chess::Color;
using arrocco::chess::Move;
using arrocco::chess::MoveList;
using arrocco_sim::ProxyTransport;
using namespace arrocco::lichess;

namespace {

constexpr int kMaxPlies = 12;
constexpr int kAiLevel = 1;
// 3+2 blitz. It must NOT be faster: Lichess sets compat.board=false on bullet games, and then
// the Board API refuses to stream them, to move in them, and even to abort them — a game started
// with a 60 s clock cannot be ended through this API at all. 180 + 40*2 = 260 s is blitz.
constexpr int kClockSeconds = 180;
constexpr int kIncrement = 2;
constexpr uint32_t kConnectTimeoutMs = 30000;
constexpr uint32_t kGameTimeoutMs = 120000;
constexpr uint32_t kResignTimeoutMs = 20000;

int g_failures = 0;

void fail(const char* what) {
  ++g_failures;
  std::printf("FAIL: %s\n", what);
}

void say(const char* format, const char* argument) {
  std::printf(format, argument);
  std::fflush(stdout);
}

// Sleeps a little between polls: this is a real network, not a fake one.
void rest() {
  timespec pause{};
  pause.tv_sec = 0;
  pause.tv_nsec = 40 * 1000 * 1000;  // 40 ms
  nanosleep(&pause, nullptr);
}

struct Live {
  ProxyTransport transport;
  arrocco::chess::Game game;
  LichessClient client{transport, game};

  // Polls until `done` or the deadline. Returns whether `done` came true.
  template <typename Predicate>
  bool pollUntil(Predicate done, uint32_t timeoutMs) {
    const uint32_t start = transport.millis();
    while (static_cast<uint32_t>(transport.millis() - start) < timeoutMs) {
      client.poll();
      if (done()) return true;
      rest();
    }
    client.poll();
    return done();
  }
};

// A legal move of the position on the board, chosen by our own rules library. Deterministic, and
// varied enough that the engine has a real game to answer.
Move chooseMove(const arrocco::chess::Game& game) {
  MoveList moves;
  const int count = game.position().generateLegalMoves(moves);
  if (count <= 0) return Move::none();
  // A capture when there is one, otherwise a move that changes with the ply so the game develops.
  for (int i = 0; i < count; ++i) {
    const Move candidate = moves[i];
    if (!game.position().pieceAt(candidate.to()).isNone()) return candidate;
  }
  return moves[(game.plyCount() * 7 + 3) % count];
}

// GET /api/account/playing straight through the transport, so the test can PROVE what it claims:
// that the game it created is not still sitting there open. False when the call itself failed.
bool ongoingGames(Live& live, char* out, int outSize) {
  if (outSize > 0) out[0] = '\0';
  if (!live.transport.beginRequest(Method::Get, "/api/account/playing", nullptr, out, outSize)) {
    return false;
  }
  const uint32_t start = live.transport.millis();
  while (live.transport.requestState() == RequestState::Busy) {
    if (static_cast<uint32_t>(live.transport.millis() - start) > kResignTimeoutMs) break;
    rest();
  }
  const bool ok = live.transport.requestState() == RequestState::Done &&
                  live.transport.responseStatus() == 200;
  live.transport.endRequest();
  return ok;
}

}  // namespace

int main() {
  Live live;
  std::printf("live: proxy at %s\n", live.transport.address());

  if (!live.transport.proxyHasToken()) {
    std::printf("SKIP: the proxy has no Lichess token (%s)\n", live.transport.lastTransportError());
    return 0;
  }

  // ---------------------------------------------------------------- who are we
  // The event stream replays whatever is already going on. A test must not adopt a game it did
  // not start: it plays its own, and leaves anything else alone.
  live.client.setAutoFollow(false);
  live.client.begin();
  if (!live.pollUntil([&] { return live.client.state() != ClientState::Connecting; }, kConnectTimeoutMs) ||
      live.client.state() == ClientState::Failed) {
    std::printf("FAIL: cannot reach lichess: %s\n", live.client.lastError());
    return 1;
  }
  if (live.client.username()[0] == '\0') {
    fail("/api/account did not give a username");
    return 1;
  }
  say("live: account is %s\n", live.client.username());

  // ---------------------------------------------------------------- an unrated game vs the AI
  // From the moment the challenge goes out a game may exist, so the guard that ends it is armed
  // BEFORE anything can fail. It runs on every way out of main().
  struct Guard {
    Live& live;
    ~Guard() {
      if (live.client.gameId()[0] == '\0') return;
      if (live.client.state() == ClientState::Finished) return;
      std::printf("live: resigning %s\n", live.client.gameId());
      live.client.resign();
      live.pollUntil([&] { return !live.client.busy(); }, kResignTimeoutMs);
      live.pollUntil([&] { return live.client.state() == ClientState::Finished; }, kResignTimeoutMs);
      if (live.client.state() != ClientState::Finished) {
        std::printf("WARNING: game https://lichess.org/%s MAY STILL BE OPEN (%s)\n",
                    live.client.gameId(), live.client.lastError());
      }
    }
  } guard{live};

  if (!live.client.startAiGame(kAiLevel, kClockSeconds, kIncrement, Color::White)) {
    std::printf("FAIL: cannot start the AI game: %s\n", live.client.lastError());
    return 1;
  }
  const bool started = live.pollUntil(
      [&] { return live.client.gameId()[0] != '\0' && live.client.plyCount() >= 0 &&
                   (live.client.state() == ClientState::Playing || live.client.state() == ClientState::Finished) &&
                   live.client.status() != GameStatus::Unknown; },
      kConnectTimeoutMs);
  if (!started || live.client.gameId()[0] == '\0') {
    std::printf("FAIL: no game started: %s (state %s, transport: %s)\n", live.client.lastError(),
                clientStateName(live.client.state()), live.transport.lastTransportError());
    return 1;
  }
  say("live: game https://lichess.org/%s\n", live.client.gameId());
  std::printf("live: we are %s against %s (level %d), rated=%s\n",
              live.client.ourColor() == Color::White ? "white" : "black", live.client.opponentName(),
              live.client.opponentAiLevel(), live.client.rated() ? "YES" : "no");
  if (live.client.rated()) fail("an AI game must never be rated");

  // ---------------------------------------------------------------- play
  const uint32_t playStart = live.transport.millis();
  int sent = 0;
  while (live.client.state() == ClientState::Playing && live.game.plyCount() < kMaxPlies &&
         static_cast<uint32_t>(live.transport.millis() - playStart) < kGameTimeoutMs) {
    live.client.poll();
    if (!live.client.ourTurn() || live.client.busy()) {
      rest();
      continue;
    }
    const Move move = chooseMove(live.game);
    if (move.isNone()) break;
    char uci[arrocco::chess::kUciBufferSize];
    move.toUci(uci);
    const int before = live.game.plyCount();
    if (!live.client.sendMove(move)) {
      std::printf("FAIL: sendMove(%s): %s\n", uci, live.client.lastError());
      ++g_failures;
      break;
    }
    ++sent;
    // Wait for the server to play it back onto our board: that, and only that, moves the board.
    const bool echoed = live.pollUntil(
        [&] { return live.game.plyCount() > before || live.client.state() != ClientState::Playing ||
                     live.client.lastMoveRejected(); },
        15000);
    if (live.client.lastMoveRejected()) {
      std::printf("FAIL: lichess refused %s: %s\n", uci, live.client.lastError());
      ++g_failures;
      break;
    }
    if (!echoed) {
      fail("the server never echoed our move");
      break;
    }
    std::printf("live: played %s, board is at ply %d\n", uci, live.game.plyCount());
    std::fflush(stdout);
  }

  char moves[512] = {};
  live.game.uciMoveList(moves, sizeof moves);
  std::printf("live: %d moves sent, %d plies on the board: %s\n", sent, live.game.plyCount(), moves);
  if (sent == 0) fail("no move was ever sent");
  if (live.game.plyCount() < 2 && live.client.state() == ClientState::Playing) {
    fail("the game never got going");
  }

  // ---------------------------------------------------------------- resign and check it took
  if (live.client.state() == ClientState::Playing) {
    if (!live.client.resign()) {
      std::printf("FAIL: resign: %s\n", live.client.lastError());
      ++g_failures;
    }
    live.pollUntil([&] { return !live.client.busy(); }, kResignTimeoutMs);
    if (!live.pollUntil([&] { return live.client.state() == ClientState::Finished; }, kResignTimeoutMs)) {
      fail("the server never confirmed the resignation");
    }
  }

  const bool over = live.client.state() == ClientState::Finished && statusIsFinished(live.client.status());
  std::printf("live: final status %s, winner %s, state %s\n", statusName(live.client.status()),
              live.client.winner() == Winner::White  ? "white"
              : live.client.winner() == Winner::Black ? "black"
                                                      : "none",
              clientStateName(live.client.state()));
  if (!over) fail("the game is not over");

  // ---------------------------------------------------------------- leave no open game
  // Our own state machine saying "finished" is not proof; the server's list of ongoing games is.
  char ourId[kGameIdSize] = {};
  for (int i = 0; i + 1 < static_cast<int>(sizeof ourId) && live.client.gameId()[i] != '\0'; ++i) {
    ourId[i] = live.client.gameId()[i];
  }
  char playing[4096] = {};
  if (!ongoingGames(live, playing, static_cast<int>(sizeof playing))) {
    std::printf("live: WARNING could not read /api/account/playing (%s)\n",
                live.transport.lastTransportError());
  } else if (ourId[0] != '\0' && std::strstr(playing, ourId) != nullptr) {
    std::printf("FAIL: https://lichess.org/%s IS STILL OPEN after the resignation\n", ourId);
    ++g_failures;
  } else {
    std::printf("live: /api/account/playing no longer lists %s\n", ourId);
    // Anything else open is not ours to end, but the person running this should hear about it.
    const char* scan = std::strstr(playing, "\"gameId\":\"");
    while (scan != nullptr) {
      char other[kGameIdSize] = {};
      const char* id = scan + 10;
      int n = 0;
      while (*id != '"' && *id != '\0' && n + 1 < static_cast<int>(sizeof other)) other[n++] = *id++;
      other[n] = '\0';
      std::printf("live: NOTE another game is still open and was left alone: https://lichess.org/%s\n",
                  other);
      scan = std::strstr(scan + 10, "\"gameId\":\"");
    }
  }

  std::printf("live: %s\n", g_failures == 0 ? "OK" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}
