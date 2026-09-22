// SPDX-License-Identifier: GPL-3.0-or-later
// Adversarial tests for the Lichess client: everything the happy-path tests do NOT do.
//
// The rule these tests enforce is the one the client is built on: the board is the SERVER'S
// board. Anything the wire says that we cannot reproduce exactly must end as a refusal or a
// re-sync, never as a guess applied to the position the user is looking at. The other rule is
// that no payload, however malformed, may write past a fixed array or cost us a move without
// saying so.
#include <cstdio>
#include <cstring>

#include "arrocco/chess/game.h"
#include "arrocco/lichess/client.h"
#include "arrocco/lichess/json.h"
#include "arrocco/lichess/messages.h"
#include "arrocco/lichess/ndjson.h"
#include "fake_transport.h"

using namespace arrocco::lichess;
using arrocco::chess::Color;
using arrocco_test::FakeTransport;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond, msg)                                                \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAIL line %d: %s   [%s]\n", __LINE__, (msg), #cond); \
    }                                                                   \
  } while (0)

#define CHECK_STR(actual, expected, msg)                                                         \
  do {                                                                                           \
    ++g_checks;                                                                                  \
    if (std::strcmp((actual), (expected)) != 0) {                                                \
      ++g_failures;                                                                              \
      std::printf("FAIL line %d: %s   got \"%s\"  expected \"%s\"\n", __LINE__, (msg), (actual), \
                  (expected));                                                                   \
    }                                                                                            \
  } while (0)

#define CHECK_INT(actual, expected, msg)                                                    \
  do {                                                                                      \
    ++g_checks;                                                                             \
    const long a__ = static_cast<long>(actual);                                             \
    const long e__ = static_cast<long>(expected);                                           \
    if (a__ != e__) {                                                                       \
      ++g_failures;                                                                         \
      std::printf("FAIL line %d: %s   got %ld  expected %ld\n", __LINE__, (msg), a__, e__); \
    }                                                                                       \
  } while (0)

const char* const kAccount =
    "{\"id\":\"collecticraft\",\"username\":\"Collecticraft\",\"perfs\":{\"blitz\":{\"games\":3}},"
    "\"createdAt\":1610000000000}";

const char* const kGameFull =
    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"variant\":{\"key\":\"standard\"},"
    "\"clock\":{\"initial\":180000,\"increment\":2000},\"speed\":\"blitz\",\"rated\":false,"
    "\"white\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\"},\"black\":{\"aiLevel\":1},"
    "\"initialFen\":\"startpos\",\"state\":{\"type\":\"gameState\",\"moves\":\"\",\"wtime\":180000,"
    "\"btime\":180000,\"winc\":2000,\"binc\":2000,\"status\":\"started\"}}\n";

struct Fixture {
  FakeTransport transport;
  arrocco::chess::Game game;
  LichessClient client{transport, game};

  void pump(int times = 3) {
    for (int i = 0; i < times; ++i) client.poll();
  }
  int eventStreamId() const { return transport.openStreamId(0); }
  int gameStreamId() const { return transport.openStreamId(1); }
};

Fixture* connected() {
  Fixture* f = new Fixture();
  f->transport.answer(200, kAccount);
  f->client.begin();
  f->pump();
  return f;
}

Fixture* playing() {
  Fixture* f = connected();
  f->transport.answer(200, "{\"id\":\"abcd1234\",\"rated\":false,\"status\":\"created\"}");
  f->client.startAiGame(1, 180, 2, Color::White);
  f->pump();
  f->transport.push(f->gameStreamId(), kGameFull);
  f->pump();
  return f;
}

// ---------------------------------------------------------------------- the ndjson reader

// One line torn into three arbitrary pieces, including a tear inside a JSON string and one
// immediately before the newline.
void testLineSplitAcrossThreeReads() {
  char buffer[256];
  NdjsonReader reader(buffer, sizeof buffer);
  const char* const whole =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"status\":\"started\"}\n";
  const int total = static_cast<int>(std::strlen(whole));

  for (int firstCut = 1; firstCut < total - 1; ++firstCut) {
    for (int secondCut = firstCut + 1; secondCut < total; ++secondCut) {
      reader.reset();
      const int cuts[3] = {firstCut, secondCut - firstCut, total - secondCut};
      int at = 0;
      for (int piece = 0; piece < 3; ++piece) {
        int used = 0;
        while (used < cuts[piece]) {
          const int taken = reader.feed(whole + at + used, cuts[piece] - used);
          if (taken == 0) break;
          used += taken;
        }
        at += cuts[piece];
      }
      const char* line = nullptr;
      int length = 0;
      if (!reader.hasLine()) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL: no line for cuts %d/%d\n", firstCut, secondCut);
        continue;
      }
      reader.takeLine(line, length);
      ++g_checks;
      if (length != total - 1 || std::strncmp(line, whole, static_cast<size_t>(length)) != 0) {
        ++g_failures;
        std::printf("FAIL: wrong line for cuts %d/%d: \"%s\"\n", firstCut, secondCut, line);
      }
    }
  }
  CHECK(!reader.overflow(), "nothing overflowed while tearing a line to pieces");
}

// A line longer than the buffer must be dropped WHOLE. The danger is a half-line being parsed:
// "moves":"e2e4 e7e5 g1f3 -- cut here -- would be a shorter, legal-looking move list.
void testOversizedLineIsDroppedWhole() {
  char buffer[64];
  NdjsonReader reader(buffer, sizeof buffer);
  const char* const huge =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1\","
      "\"status\":\"started\"}\n";
  const char* const next = "{\"type\":\"gameState\",\"moves\":\"e2e4\"}\n";

  int at = 0;
  const int size = static_cast<int>(std::strlen(huge));
  while (at < size) {
    const int taken = reader.feed(huge + at, size - at);
    if (taken == 0) break;
    at += taken;
    const char* line = nullptr;
    int length = 0;
    while (reader.takeLine(line, length)) {
      ++g_checks;
      ++g_failures;
      std::printf("FAIL: a truncated line was handed out: \"%s\"\n", line);
    }
  }
  CHECK(reader.overflow(), "the oversized line is reported, not swallowed");
  CHECK(!reader.hasLine(), "and no piece of it is waiting");

  // The stream recovers exactly at the next newline.
  reader.clearOverflow();
  at = 0;
  const int nextSize = static_cast<int>(std::strlen(next));
  while (at < nextSize) {
    const int taken = reader.feed(next + at, nextSize - at);
    if (taken == 0) break;
    at += taken;
  }
  const char* line = nullptr;
  int length = 0;
  CHECK(reader.takeLine(line, length), "the next line arrives");
  CHECK_STR(line, "{\"type\":\"gameState\",\"moves\":\"e2e4\"}", "and it is whole");
  CHECK(!reader.overflow(), "with no leftover overflow");
}

// The buffer must never be written past, whatever the input. A guard byte says so.
void testReaderNeverWritesPastItsBuffer() {
  char space[80];
  std::memset(space, '#', sizeof space);
  const int capacity = 32;
  NdjsonReader reader(space, capacity);
  char blob[512];
  for (int i = 0; i < static_cast<int>(sizeof blob); ++i) {
    blob[i] = static_cast<char>('a' + (i % 26));
  }
  blob[100] = '\n';
  blob[300] = '\n';
  int at = 0;
  while (at < static_cast<int>(sizeof blob)) {
    const int taken = reader.feed(blob + at, static_cast<int>(sizeof blob) - at);
    if (taken == 0) break;
    at += taken;
    const char* line = nullptr;
    int length = 0;
    while (reader.takeLine(line, length)) {
      CHECK(length < capacity, "a line handed out always fits the buffer");
    }
  }
  bool guardsIntact = true;
  for (int i = capacity; i < static_cast<int>(sizeof space); ++i) {
    if (space[i] != '#') guardsIntact = false;
  }
  CHECK(guardsIntact, "nothing was written past the capacity the reader was given");
}

// ---------------------------------------------------------------------- the JSON reader

void testNumbersAndBounds() {
  int32_t value = 0;
  const char* const big = "{\"pos\":99999999999,\"neg\":-99999999999,\"zero\":-0,\"frac\":12.9}";
  const JsonObject o(big, static_cast<int>(std::strlen(big)));
  CHECK(o.number("pos", value) && value == INT32_MAX, "a huge positive number saturates high");
  CHECK(o.number("neg", value) && value == INT32_MIN,
        "a huge NEGATIVE number saturates LOW, it does not come back positive");
  CHECK(o.number("zero", value) && value == 0, "minus zero");
  CHECK(o.number("frac", value) && value == 12, "a fraction truncates");

  // Every prefix of a real payload: a reader must never read past what it was given.
  const char* const payload =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":178000,\"status\":\"started\","
      "\"winner\":\"white\"}";
  const int size = static_cast<int>(std::strlen(payload));
  char name[64];
  for (int cut = 0; cut <= size; ++cut) {
    const JsonObject partial(payload, cut);
    partial.copyString("status", name, sizeof name);
    partial.number("wtime", value);
    partial.has("winner");
  }
  CHECK(true, "every prefix of a payload parses without reading past its end");

  // Rubbish of every shape.
  const char* const junk[] = {"",          "{",       "}",        "{\"a\"",  "{\"a\":",
                              "{\"a\":\"", "[1,2,3]", "{\"a\":{", "not json", "{\"a\":\"\\"};
  for (const char* text : junk) {
    const JsonObject o2(text, static_cast<int>(std::strlen(text)));
    o2.copyString("a", name, sizeof name);
    o2.number("a", value);
    o2.object("a");
  }
  CHECK(true, "malformed JSON is refused without crashing");
}

void testUtf8AndTruncation() {
  char name[kNameSize];
  // A UTF-8 opponent name, both as raw bytes and as \u escapes.
  const char* const raw = "{\"username\":\"J\xc3\xb8rgen \xe2\x99\x9ePlayer\"}";
  const JsonObject o(raw, static_cast<int>(std::strlen(raw)));
  CHECK(o.copyString("username", name, sizeof name), "a UTF-8 username");
  CHECK_STR(name, "J\xc3\xb8rgen \xe2\x99\x9ePlayer", "byte for byte");

  const char* const escaped = "{\"username\":\"caf\\u00e9 \\u265e\"}";
  const JsonObject e(escaped, static_cast<int>(std::strlen(escaped)));
  CHECK(e.copyString("username", name, sizeof name), "escaped UTF-16");
  CHECK_STR(name, "caf\xc3\xa9 \xe2\x99\x9e", "decoded to UTF-8");

  // A multi-byte character that does not fit must not be written half.
  char tiny[6];
  std::memset(tiny, '#', sizeof tiny);
  const char* const wide = "{\"n\":\"\\u265e\\u265e\\u265e\"}";
  const JsonObject w(wide, static_cast<int>(std::strlen(wide)));
  CHECK(!w.copyString("n", tiny, sizeof tiny), "a value that does not fit is reported");
  CHECK_INT(std::strlen(tiny), 3, "and it stops on a character boundary");
  CHECK_INT(tiny[5], '#', "the last byte of the array is untouched");

  // A lone surrogate half becomes a '?', never a stray byte.
  const char* const half = "{\"n\":\"\\ud83d x\"}";
  const JsonObject h(half, static_cast<int>(std::strlen(half)));
  CHECK(h.copyString("n", name, sizeof name), "a lone surrogate still reads");
  CHECK_STR(name, "? x", "as a question mark");
}

// ---------------------------------------------------------------------- the message parsers

void testUnknownFieldsAndStatuses() {
  // Fields we have never heard of must be skipped, not confuse the ones we want.
  const char* const line =
      "{\"type\":\"gameState\",\"newThing\":{\"a\":[1,2,{\"b\":\"}\"}],\"c\":null},"
      "\"moves\":\"e2e4\",\"anotherNewThing\":[[]],\"wtime\":1000,\"btime\":2000,"
      "\"status\":\"started\",\"yetMore\":true}";
  GameMessage m;
  CHECK(parseGameMessage(line, static_cast<int>(std::strlen(line)), m), "unknown fields are skipped");
  CHECK(m.state.status == GameStatus::Started, "the status still reads");
  CHECK_INT(m.state.wtime, 1000, "and the clock");
  CHECK_INT(m.state.movesLength, 4, "and the moves");

  // A status Lichess has not invented yet. Calling it "not finished" would leave a finished game
  // looking playable and the client reopening its stream for ever, so it counts as an end.
  const char* const future =
      "{\"type\":\"gameState\",\"moves\":\"e2e4\",\"status\":\"quantumCollapse\",\"winner\":\"black\"}";
  GameMessage f;
  CHECK(parseGameMessage(future, static_cast<int>(std::strlen(future)), f), "an unknown status parses");
  CHECK(f.state.status == GameStatus::UnknownFinish, "an unknown status is an unknown END");
  CHECK(statusIsFinished(f.state.status), "and counts as finished");
  CHECK(f.state.winner == Winner::Black, "the winner still reads");

  // Longer than the array it is copied into: it must still count as an end, not vanish.
  const char* const verbose =
      "{\"type\":\"gameState\",\"moves\":\"\",\"status\":"
      "\"aStatusNameLongerThanAnyBufferWeGaveIt\",\"winner\":\"white\"}";
  GameMessage v;
  CHECK(parseGameMessage(verbose, static_cast<int>(std::strlen(verbose)), v), "an over-long status parses");
  CHECK(statusIsFinished(v.state.status), "an over-long unknown status is still an end");

  // A missing status is NOT an end: it means the field was not there.
  const char* const silent = "{\"type\":\"gameState\",\"moves\":\"e2e4\",\"wtime\":1}";
  GameMessage s;
  CHECK(parseGameMessage(silent, static_cast<int>(std::strlen(silent)), s), "a state with no status");
  CHECK(s.state.status == GameStatus::Unknown, "is Unknown");
  CHECK(!statusIsFinished(s.state.status), "and not an end");

  // An unknown message type is ignored, not mistaken for another.
  const char* const alien = "{\"type\":\"somethingNew\",\"moves\":\"e2e4\"}";
  GameMessage a;
  CHECK(!parseGameMessage(alien, static_cast<int>(std::strlen(alien)), a), "an unknown type is refused");
  CHECK(a.type == GameMessageType::Unknown, "and stays Unknown");
  Event event;
  CHECK(!parseEvent(alien, static_cast<int>(std::strlen(alien)), event), "the same on the event stream");
}

void testIdsAndNamesAreNeverTruncated() {
  // An id that does not fit must be REFUSED. Eleven characters of a twelve-character id is a
  // different game: we would stream it and POST our moves into it.
  const char* const longId =
      "{\"type\":\"gameStart\",\"game\":{\"gameId\":\"abcdefghijklmnopqrst\",\"color\":\"white\"}}";
  Event e;
  CHECK(!parseEvent(longId, static_cast<int>(std::strlen(longId)), e), "an over-long game id is refused");
  CHECK_STR(e.game.id, "", "and nothing half of it is kept");

  const char* const longFullId =
      "{\"type\":\"gameFull\",\"id\":\"abcdefghijklmnopqrst\",\"state\":{\"moves\":\"\"}}";
  GameMessage m;
  CHECK(!parseGameMessage(longFullId, static_cast<int>(std::strlen(longFullId)), m),
        "the same for a gameFull id");

  const char* const longChallenge =
      "{\"type\":\"challenge\",\"challenge\":{\"id\":\"abcdefghijklmnopqrst\",\"status\":\"created\"}}";
  Event c;
  CHECK(!parseEvent(longChallenge, static_cast<int>(std::strlen(longChallenge)), c),
        "and for a challenge id");

  // A normal id is of course fine.
  const char* const fine =
      "{\"type\":\"gameStart\",\"game\":{\"gameId\":\"3vg0BBBd\",\"color\":\"black\"}}";
  Event ok;
  CHECK(parseEvent(fine, static_cast<int>(std::strlen(fine)), ok), "a real id parses");
  CHECK_STR(ok.game.id, "3vg0BBBd", "whole");
  CHECK(ok.game.color == Color::Black, "and our colour with it");
}

// ---------------------------------------------------------------------- the state machine

// The server sends a move list that is NOT an extension of ours. The board must become the
// server's, never stay ours.
void testServerDisagreementRebuilds() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 3, "three plies");

  // A different second move: everything after it is a different game.
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5 g1f3 d7d6\",\"wtime\":1,"
                    "\"btime\":1,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 4, "the board is rebuilt to the server's four plies");
  CHECK_STR(f->game.sanAt(1), "c5", "and the server's second move is the one on it");
  CHECK(f->client.error() != ClientError::Desynchronised, "this is a re-sync, not a failure");

  // A takeback: the server now has FEWER moves than we do.
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "we follow the server back down");
  CHECK_STR(f->game.sanAt(1), "c5", "onto its line");
  delete f;
}

// The same gameState twice, and a gameState that repeats a move we already have.
void testDuplicateStates() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  const char* const state =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":178000,\"btime\":179000,"
      "\"status\":\"started\"}\n";
  f->transport.push(id, state);
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "two plies");
  f->transport.push(id, state);
  f->transport.push(id, state);
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "the same state three times is still two plies");
  CHECK(f->client.error() != ClientError::Desynchronised, "and no desync");
  delete f;
}

// A move in the server's list that our rules refuse. We must say so and NOT invent a board.
void testIllegalMoveFromServer() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 e1e8\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  CHECK(f->client.error() == ClientError::Desynchronised, "an impossible move is a desync");
  CHECK(!f->client.ourTurn(), "and we refuse to move while desynchronised");
  CHECK_INT(f->game.plyCount(), 2, "only the moves we could replay are on the board");

  // And it recovers when the server goes back to something we can follow.
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3 b8c6\",\"wtime\":1,"
                    "\"btime\":1,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 4, "a list we can replay puts us back in sync");
  CHECK(f->client.error() != ClientError::Desynchronised, "the desync is cleared");
  CHECK(f->client.ourTurn(), "and we can move again");
  delete f;
}

// A "move" too long for the token buffer must not be truncated into a different, legal move.
void testMoveTokenNeverTruncated() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  // "e2e4e7e5g1f3" would become "e2e4e7e" and then, cut to a legal prefix, a move we never saw.
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5e7e5e7e5\",\"wtime\":1,"
                    "\"btime\":1,\"status\":\"started\"}\n");
  f->pump();
  CHECK(f->client.error() == ClientError::Desynchronised, "an over-long token is a desync");
  CHECK(f->game.plyCount() <= 1, "and no invented move reached the board");
  CHECK(!f->client.ourTurn(), "we do not play on a board we do not trust");
  delete f;
}

// A stream that dies exactly between two moves: the reconnect must not lose or double a ply.
void testStreamDiesBetweenMoves() {
  Fixture* f = playing();
  int id = f->gameStreamId();
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "two plies before the wire goes");

  f->transport.kill(id);
  f->pump();
  CHECK(f->client.inGame(), "we are still in the game");
  f->transport.advance(3000);
  f->pump();
  id = f->gameStreamId();
  CHECK(id > 0, "the game stream was reopened");

  // Lichess replays gameFull, with two more moves played while we were away.
  f->transport.push(id,
                    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"clock\":{\"initial\":180000,"
                    "\"increment\":2000},\"rated\":false,\"white\":{\"name\":\"Collecticraft\"},"
                    "\"black\":{\"aiLevel\":1},\"initialFen\":\"startpos\",\"state\":"
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3 b8c6\",\"wtime\":1,"
                    "\"btime\":1,\"status\":\"started\"}}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 4, "four plies after the replay, not six");
  CHECK_STR(f->game.sanAt(0), "e4", "and the game is the same game");
  CHECK_STR(f->game.sanAt(3), "Nc6", "up to the moves we missed");
  CHECK(f->client.error() != ClientError::Desynchronised, "no desync");
  delete f;
}

// A gameFull replayed twice in a row (Lichess does this on a fast reconnect).
void testGameFullReplayedTwice() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  f->transport.push(id, kGameFull);
  f->transport.push(id, kGameFull);
  f->pump();
  CHECK_INT(f->game.plyCount(), 0, "a gameFull with an empty move list rewinds to it");
  CHECK(f->client.inGame(), "and we are still playing");
  CHECK(f->client.error() != ClientError::Desynchronised, "without a desync");
  delete f;
}

// A correspondence game: no clock at all. Nothing may divide by it or invent one.
void testNoClock() {
  Fixture* f = connected();
  f->transport.answer(200, "{\"id\":\"abcd1234\",\"rated\":false,\"status\":\"created\"}");
  f->client.startAiGame(1, 180, 2, Color::White);
  f->pump();
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"rated\":false,"
                    "\"white\":{\"name\":\"Collecticraft\"},\"black\":{\"aiLevel\":1},"
                    "\"initialFen\":\"startpos\",\"daysPerTurn\":2,\"state\":{\"type\":\"gameState\","
                    "\"moves\":\"\",\"status\":\"started\"}}\n");
  f->pump();
  CHECK(f->client.inGame(), "a game with no clock still plays");
  CHECK(!f->client.hasClock(), "and says it has no clock");
  CHECK_INT(f->client.clockMs(Color::White), 0, "the clock reads zero");
  CHECK_INT(f->client.clockMsNow(Color::White), 0, "and counting down does nothing");
  f->transport.advance(60000);
  CHECK_INT(f->client.clockMsNow(Color::Black), 0, "still nothing a minute later");
  delete f;
}

// An empty keep-alive line is life; twenty seconds of nothing is death.
void testKeepAliveVersusDeadStream() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  const int opens = f->transport.streamOpenCalls();

  // Keep-alives every seven seconds for a minute, on BOTH streams: they must survive.
  for (int i = 0; i < 9; ++i) {
    f->transport.advance(7000);
    f->transport.push(id, "\n");
    f->transport.push(f->eventStreamId(), "\n");
    f->pump();
  }
  CHECK_INT(f->transport.streamOpenCalls(), opens, "keep-alives keep the streams alive");
  CHECK_INT(f->gameStreamId(), id, "the same stream");

  // Then nothing at all.
  f->transport.advance(21000);
  f->pump();
  CHECK(f->transport.streamOpenCalls() > opens || f->gameStreamId() != id,
        "twenty seconds of silence closes it");
  delete f;
}

// A 429 while a stream is being opened costs a full minute, not a two-second retry.
void testRateLimitedStream() {
  Fixture* f = playing();
  f->transport.kill(f->gameStreamId());
  f->pump();
  f->transport.refuseOpenStream(429);
  f->transport.advance(3000);
  f->pump();
  const int opensAfterRefusal = f->transport.streamOpenCalls();
  CHECK(f->client.error() == ClientError::RateLimited, "a 429 on a stream is a rate limit");
  CHECK(f->client.backoffRemainingMs() > 55000, "and it buys Lichess a whole minute");

  // Ten seconds of polling must not touch the network again.
  for (int i = 0; i < 10; ++i) {
    f->transport.advance(1000);
    f->pump();
  }
  CHECK_INT(f->transport.streamOpenCalls(), opensAfterRefusal,
            "no reconnect attempt during the backoff");

  f->transport.refuseOpenStream(0);
  f->transport.advance(62000);
  f->pump();
  CHECK(f->transport.streamOpenCalls() > opensAfterRefusal, "and it tries again once the minute is up");
  delete f;
}

// An ordinary refusal (not 429) still reconnects at the normal pace.
void testNonRateLimitedStreamFailureStillRetries() {
  Fixture* f = playing();
  f->transport.kill(f->gameStreamId());
  f->pump();
  f->transport.refuseOpenStream(503);
  f->transport.advance(3000);
  f->pump();
  const int opens = f->transport.streamOpenCalls();
  CHECK(f->client.error() == ClientError::Network, "a 503 is a network failure, not a rate limit");
  CHECK_INT(f->client.backoffRemainingMs(), 0, "with no minute-long backoff");
  f->transport.advance(3000);
  f->pump();
  CHECK(f->transport.streamOpenCalls() > opens, "and it retries after the reconnect delay");
  delete f;
}

// HTTP 5xx on a request: reported, and it must not fail the client for good.
void testServerErrors() {
  Fixture* f = playing();
  f->transport.answer(500, "<html>Internal Server Error</html>");
  CHECK(f->client.sendMove("e2e4"), "the move goes out");
  f->pump();
  CHECK(f->client.lastMoveRejected(), "a 500 on a move is a rejection");
  CHECK(f->client.error() == ClientError::MoveRejected, "reported as such");
  CHECK(std::strstr(f->client.lastError(), "500") != nullptr, "with the status in the message");
  CHECK_INT(f->game.plyCount(), 0, "and the board is untouched");
  CHECK(f->client.inGame(), "we are still in the game");

  // 502 on a resign: an error, but the client survives to try again.
  f->transport.answer(502, "");
  CHECK(f->client.resign(), "resign goes out");
  f->pump();
  CHECK(f->client.error() == ClientError::Http, "a 502 is an HTTP error");
  CHECK(!f->client.busy(), "and the request slot is free again");
  f->transport.answer(200, "{\"ok\":true}");
  CHECK(f->client.resign(), "so resign can be sent again");
  f->pump();
  CHECK(f->client.error() == ClientError::None, "and this time it works");
  delete f;
}

// The final gameState and a stray line in the SAME chunk: the stray line must not be replayed
// onto a board the user is already looking at.
void testNothingIsAppliedAfterTheGameEnds() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n"
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"resign\",\"winner\":\"white\"}\n"
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6\","
                    "\"wtime\":1,\"btime\":1,\"status\":\"started\"}\n");
  f->pump();
  CHECK(f->client.state() == ClientState::Finished, "the game is over");
  CHECK(f->client.status() == GameStatus::Resign, "by resignation");
  CHECK(f->client.winner() == Winner::White, "white won");
  CHECK_INT(f->game.plyCount(), 3, "the board stopped at the last move of the game");
  CHECK(!f->client.ourTurn(), "and it is nobody's turn");
  delete f;
}

// UTF-8 in the names that come off the wire, all the way through to the accessors.
void testUtf8OpponentName() {
  Fixture* f = connected();
  f->transport.answer(200, "{\"id\":\"abcd1234\",\"status\":\"created\"}");
  f->client.startAiGame(1, 180, 2, Color::White);
  f->pump();
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"rated\":false,"
                    "\"clock\":{\"initial\":180000,\"increment\":2000},"
                    "\"white\":{\"name\":\"Collecticraft\"},"
                    "\"black\":{\"id\":\"jorgen\",\"name\":\"J\\u00f8rgen-\\u265e\"},"
                    "\"initialFen\":\"startpos\",\"state\":{\"type\":\"gameState\",\"moves\":\"\","
                    "\"wtime\":1,\"btime\":1,\"status\":\"started\"}}\n");
  f->pump();
  CHECK_STR(f->client.opponentName(), "J\xc3\xb8rgen-\xe2\x99\x9e", "a UTF-8 opponent name survives");
  CHECK(f->client.ourColor() == Color::White, "and we are still white");
  delete f;
}

// A gameState torn into one-byte reads, interleaved with keep-alives.
void testByteAtATime() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  f->transport.setChunkSize(1);
  f->transport.push(id, "\n");
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3\",\"wtime\":177000,"
                    "\"btime\":179000,\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->transport.push(id, "\n");
  f->pump(400);
  CHECK_INT(f->game.plyCount(), 3, "a line read one byte at a time still arrives whole");
  CHECK(!f->client.lineOverflow(), "and nothing overflowed");
  delete f;
}

// A gameState far longer than the line buffer: dropped whole, reported, and recovered from,
// because every gameState restates the entire game.
void testOversizedGameLineRecovers() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  char monster[LichessClient::kGameLineSize + 512];
  int n = 0;
  const char* const head = "{\"type\":\"gameState\",\"moves\":\"";
  for (const char* p = head; *p != '\0'; ++p) monster[n++] = *p;
  while (n < static_cast<int>(sizeof monster) - 64) {
    const char* const filler = "e2e4 ";
    for (const char* p = filler; *p != '\0'; ++p) monster[n++] = *p;
  }
  const char* const tail = "\",\"status\":\"started\"}\n";
  for (const char* p = tail; *p != '\0'; ++p) monster[n++] = *p;
  monster[n] = '\0';
  f->transport.push(id, monster);
  f->pump(20);
  CHECK(f->client.error() == ClientError::Protocol || f->client.error() == ClientError::Desynchronised,
        "an over-long game line is reported");
  CHECK_INT(f->game.plyCount(), 0, "and nothing half of it reached the board");

  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,\"btime\":1,"
                    "\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "the next state puts the whole game back");
  CHECK(f->client.ourTurn(), "and we can play again");
  delete f;
}

// The event stream: garbage, half-objects and unknown types must not move the state machine.
void testEventStreamGarbage() {
  Fixture* f = connected();
  const int id = f->eventStreamId();
  f->transport.push(id,
                    "not json at all\n"
                    "{\"type\":\"gameStart\"}\n"                 // no game object
                    "{\"type\":\"gameStart\",\"game\":{}}\n"     // no id
                    "{\"type\":\"challenge\"}\n"                 // no challenge object
                    "{\"type\":\"\"}\n"
                    "{}\n"
                    "[]\n"
                    "\n"
                    "{\"type\":\"gameStart\",\"game\":{\"gameId\":\"\"}}\n");
  f->pump(10);
  CHECK(f->client.state() == ClientState::Idle, "none of that started a game");
  CHECK_STR(f->client.gameId(), "", "and no game id was invented");
  CHECK_INT(f->client.challengeCount(), 0, "and no challenge");
  delete f;
}

// More pending challenges than the array holds: the extras are dropped, the array is not.
void testChallengeOverflow() {
  Fixture* f = connected();
  const int id = f->eventStreamId();
  const char* const ids[] = {"aaaa1111", "bbbb2222", "cccc3333", "dddd4444", "eeee5555", "ffff6666"};
  for (const char* challengeId : ids) {
    char line[256];
    std::snprintf(line, sizeof line,
                  "{\"type\":\"challenge\",\"challenge\":{\"id\":\"%s\",\"status\":\"created\","
                  "\"challenger\":{\"name\":\"Amico\"},\"rated\":false,\"speed\":\"blitz\"}}\n",
                  challengeId);
    f->transport.push(id, line);
  }
  f->pump(10);
  CHECK_INT(f->client.challengeCount(), LichessClient::kMaxChallenges,
            "the list stops at its capacity");
  for (int i = 0; i < f->client.challengeCount(); ++i) {
    CHECK(f->client.challengeAt(i).id[0] != '\0', "and every entry is a real challenge");
  }
  CHECK_STR(f->client.challengeAt(-1).id, "", "an index below zero is empty, not a crash");
  CHECK_STR(f->client.challengeAt(99).id, "", "and so is one past the end");
  delete f;
}

// Commands with hostile arguments must build no request at all.
void testHostileArguments() {
  Fixture* f = playing();
  const int requests = f->transport.requestCount();
  CHECK(!f->client.sendMove("e2e4; DROP"), "a move with punctuation is refused");
  CHECK(!f->client.sendMove("z9z9"), "a move off the board is refused");
  CHECK(!f->client.sendMove(""), "an empty move is refused");
  CHECK(!f->client.sendMove("e2e4k"), "an impossible promotion is refused");
  CHECK(!f->client.acceptChallenge("../../api/account"), "a path in a challenge id is refused");
  CHECK(!f->client.acceptChallenge("id with spaces"), "spaces are refused");
  CHECK(!f->client.acceptChallenge(""), "an empty id is refused");
  CHECK_INT(f->transport.requestCount(), requests, "and not one of them reached the network");

  // A username that is not a username.
  Fixture* g = connected();
  const int before = g->transport.requestCount();
  CHECK(!g->client.challengeUser("bob/../../admin", false, 300, 0, Color::White), "a path is refused");
  CHECK(!g->client.challengeUser("", false, 300, 0, Color::White), "an empty name is refused");
  CHECK_INT(g->transport.requestCount(), before, "nothing was sent");
  delete g;
  delete f;
}

// The bullet trap: a clock the Board API cannot abort or resign must never be created.
void testBulletClockRefused() {
  Fixture* f = connected();
  const int requests = f->transport.requestCount();
  CHECK(!f->client.startAiGame(1, 60, 0, Color::White), "60+0 is refused");
  CHECK(!f->client.startAiGame(1, 120, 1, Color::White), "2+1 is refused");
  CHECK(!f->client.startAiGame(1, 0, 0, Color::White), "no clock at all is refused");
  CHECK(!f->client.challengeUser("amico", false, 60, 0, Color::White), "and so is a bullet challenge");
  CHECK_INT(f->transport.requestCount(), requests, "not one of them was sent");
  CHECK(std::strstr(f->client.lastError(), "3+0") != nullptr, "and the message says what to use");

  f->transport.answer(200, "{\"id\":\"abcd1234\"}");
  CHECK(f->client.startAiGame(1, 180, 0, Color::White), "3+0 is allowed");
  CHECK(f->transport.requestCount() > requests, "and goes out");
  delete f;
}

}  // namespace

int main() {
  testLineSplitAcrossThreeReads();
  testOversizedLineIsDroppedWhole();
  testReaderNeverWritesPastItsBuffer();
  testNumbersAndBounds();
  testUtf8AndTruncation();
  testUnknownFieldsAndStatuses();
  testIdsAndNamesAreNeverTruncated();
  testServerDisagreementRebuilds();
  testDuplicateStates();
  testIllegalMoveFromServer();
  testMoveTokenNeverTruncated();
  testStreamDiesBetweenMoves();
  testGameFullReplayedTwice();
  testNoClock();
  testKeepAliveVersusDeadStream();
  testRateLimitedStream();
  testNonRateLimitedStreamFailureStillRetries();
  testServerErrors();
  testNothingIsAppliedAfterTheGameEnds();
  testUtf8OpponentName();
  testByteAtATime();
  testOversizedGameLineRecovers();
  testEventStreamGarbage();
  testChallengeOverflow();
  testHostileArguments();
  testBulletClockRefused();

  std::printf("lichess attack: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
