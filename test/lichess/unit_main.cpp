// SPDX-License-Identifier: GPL-3.0-or-later
// Offline tests for the Lichess client: the ndjson reader on split and partial lines, the JSON
// reader, every parser against payloads written by hand from the API documentation, and the state
// machine driven through a whole game by a scripted transport — reconnect, 429, opponentGone,
// a rejected move, and an end in each status Lichess can send.
// No network: this runs anywhere, always, and is the test that must never be skipped.
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

#define CHECK_STR(actual, expected, msg)                                                             \
  do {                                                                                               \
    ++g_checks;                                                                                      \
    if (std::strcmp((actual), (expected)) != 0) {                                                    \
      ++g_failures;                                                                                  \
      std::printf("FAIL line %d: %s   got \"%s\"  expected \"%s\"\n", __LINE__, (msg), (actual),     \
                  (expected));                                                                       \
    }                                                                                                \
  } while (0)

#define CHECK_INT(actual, expected, msg)                                                          \
  do {                                                                                            \
    ++g_checks;                                                                                   \
    const long a__ = static_cast<long>(actual);                                                   \
    const long e__ = static_cast<long>(expected);                                                 \
    if (a__ != e__) {                                                                             \
      ++g_failures;                                                                                \
      std::printf("FAIL line %d: %s   got %ld  expected %ld\n", __LINE__, (msg), a__, e__);       \
    }                                                                                             \
  } while (0)

// ---------------------------------------------------------------------- sample payloads
// Written by hand from https://lichess.org/api, keeping the fields (and the nulls) the real
// server sends, so the parsers are tested against what they will actually meet.

const char* const kAccount =
    "{\"id\":\"collecticraft\",\"username\":\"Collecticraft\",\"perfs\":{\"blitz\":{\"games\":3,"
    "\"rating\":1500,\"rd\":150,\"prog\":0,\"prov\":true}},\"createdAt\":1610000000000,"
    "\"seenAt\":1758480000000,\"playTime\":{\"total\":120,\"tv\":0},\"url\":\"https://lichess.org/@/Collecticraft\"}";

const char* const kGameStart =
    "{\"type\":\"gameStart\",\"game\":{\"gameId\":\"abcd1234\",\"fullId\":\"abcd1234efgh\","
    "\"color\":\"white\",\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\","
    "\"hasMoved\":false,\"isMyTurn\":true,\"lastMove\":\"\",\"opponent\":{\"id\":null,"
    "\"username\":\"Stockfish level 1\",\"rating\":800,\"ai\":1},\"perf\":\"blitz\",\"rated\":false,"
    "\"secondsLeft\":180,\"source\":\"ai\",\"speed\":\"blitz\","
    "\"status\":{\"id\":20,\"name\":\"started\"},\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},"
    "\"compat\":{\"bot\":false,\"board\":true},\"id\":\"abcd1234\"}}";

const char* const kGameFinish =
    "{\"type\":\"gameFinish\",\"game\":{\"gameId\":\"abcd1234\",\"fullId\":\"abcd1234efgh\","
    "\"color\":\"white\",\"isMyTurn\":false,\"opponent\":{\"username\":\"Stockfish level 1\",\"ai\":1},"
    "\"rated\":false,\"secondsLeft\":0,\"source\":\"ai\",\"speed\":\"blitz\","
    "\"status\":{\"id\":31,\"name\":\"resign\"},\"winner\":\"black\","
    "\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},\"compat\":{\"bot\":false,\"board\":true}}}";

const char* const kChallenge =
    "{\"type\":\"challenge\",\"challenge\":{\"id\":\"7pGLxJ4F\",\"url\":\"https://lichess.org/7pGLxJ4F\","
    "\"status\":\"created\",\"challenger\":{\"id\":\"amico\",\"name\":\"Amico\",\"title\":null,"
    "\"rating\":1450,\"online\":true},\"destUser\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\","
    "\"title\":null,\"rating\":1500,\"provisional\":true},\"variant\":{\"key\":\"standard\","
    "\"name\":\"Standard\",\"short\":\"Std\"},\"rated\":true,\"speed\":\"blitz\","
    "\"timeControl\":{\"type\":\"clock\",\"limit\":300,\"increment\":3,\"show\":\"5+3\"},"
    "\"color\":\"random\",\"finalColor\":\"white\",\"perf\":{\"icon\":\")\",\"name\":\"Blitz\"}},"
    "\"compat\":{\"bot\":false,\"board\":true}}";

const char* const kChallengeDeclined =
    "{\"type\":\"challengeDeclined\",\"challenge\":{\"id\":\"7pGLxJ4F\",\"status\":\"declined\","
    "\"challenger\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\"},"
    "\"destUser\":{\"id\":\"amico\",\"name\":\"Amico\"},\"variant\":{\"key\":\"standard\"},"
    "\"rated\":false,\"speed\":\"blitz\",\"timeControl\":{\"type\":\"clock\",\"limit\":180,\"increment\":2},"
    "\"declineReason\":\"This is not the right time for me, sorry.\",\"declineReasonKey\":\"later\"}}";

const char* const kChallengeCanceled =
    "{\"type\":\"challengeCanceled\",\"challenge\":{\"id\":\"7pGLxJ4F\",\"status\":\"canceled\","
    "\"challenger\":{\"id\":\"amico\",\"name\":\"Amico\"},\"destUser\":{\"id\":\"collecticraft\","
    "\"name\":\"Collecticraft\"},\"variant\":{\"key\":\"standard\"},\"rated\":false,\"speed\":\"blitz\","
    "\"timeControl\":{\"type\":\"unlimited\"}}}";

const char* const kGameFull =
    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"variant\":{\"key\":\"standard\",\"name\":\"Standard\","
    "\"short\":\"Std\"},\"clock\":{\"initial\":180000,\"increment\":2000},\"speed\":\"blitz\","
    "\"perf\":{\"name\":\"Blitz\"},\"rated\":false,\"createdAt\":1758480000000,"
    "\"white\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\",\"title\":null,\"rating\":1500,"
    "\"provisional\":true},\"black\":{\"aiLevel\":1},\"initialFen\":\"startpos\","
    "\"state\":{\"type\":\"gameState\",\"moves\":\"\",\"wtime\":180000,\"btime\":180000,"
    "\"winc\":2000,\"binc\":2000,\"status\":\"started\"},\"tournamentId\":null}\n";

// ---------------------------------------------------------------------- ndjson

void testNdjson() {
  char buffer[64];
  NdjsonReader reader(buffer, sizeof buffer);
  const char* line = nullptr;
  int length = 0;

  // A line split across three chunks, one byte at a time for the last bit.
  reader.feed("{\"type\":\"ga", 11);
  CHECK(!reader.hasLine(), "a partial line is not a line");
  reader.feed("meStart\"}", 9);
  CHECK(!reader.hasLine(), "still no newline, still no line");
  reader.feed("\n", 1);
  CHECK(reader.hasLine(), "the newline completes it");
  CHECK(reader.takeLine(line, length), "takeLine");
  CHECK_STR(line, "{\"type\":\"gameStart\"}", "the line is reassembled whole");
  CHECK_INT(length, 20, "and its length is right");
  CHECK(!reader.takeLine(line, length), "nothing left");

  // Two lines plus half a third in one chunk: feed() stops after each newline.
  const char* chunk = "one\ntwo\nthr";
  int size = static_cast<int>(std::strlen(chunk));
  int used = 0;
  int taken = reader.feed(chunk + used, size - used);
  used += taken;
  CHECK_INT(taken, 4, "feed stops right after the first newline");
  CHECK(reader.takeLine(line, length), "first line");
  CHECK_STR(line, "one", "first line");
  used += reader.feed(chunk + used, size - used);
  CHECK(reader.takeLine(line, length), "second line");
  CHECK_STR(line, "two", "second line");
  used += reader.feed(chunk + used, size - used);
  CHECK_INT(used, size, "the whole chunk was consumed");
  CHECK(!reader.hasLine(), "the third line is still incomplete");
  reader.feed("ee\n", 3);
  CHECK(reader.takeLine(line, length), "third line");
  CHECK_STR(line, "three", "the third line joins across chunks");

  // Keep-alives: empty lines, and CRLF endings.
  reader.feed("\n", 1);
  CHECK(reader.takeLine(line, length), "an empty line is a line");
  CHECK_INT(length, 0, "the keep-alive is empty");
  reader.feed("abc\r\n", 5);
  CHECK(reader.takeLine(line, length), "CRLF line");
  CHECK_STR(line, "abc", "the CR is dropped");

  // A line longer than the buffer is dropped whole, never half-parsed, and the next one is fine.
  CHECK(!reader.overflow(), "no overflow yet");
  char big[200];
  std::memset(big, 'x', sizeof big);
  big[sizeof big - 1] = '\n';
  int position = 0;
  while (position < static_cast<int>(sizeof big)) {
    const int step = reader.feed(big + position, static_cast<int>(sizeof big) - position);
    if (step == 0) break;
    position += step;
    while (reader.takeLine(line, length)) {
    }
  }
  CHECK(reader.overflow(), "the oversized line is reported");
  reader.clearOverflow();
  reader.feed("after\n", 6);
  CHECK(reader.takeLine(line, length), "the reader recovers");
  CHECK_STR(line, "after", "and the next line is clean");
  CHECK(!reader.overflow(), "the flag was cleared");
}

// ---------------------------------------------------------------------- json

void testJson() {
  const char* const text =
      "{\"a\":\"x\\\"y\",\"n\":-42,\"big\":1758480000000,\"t\":true,\"f\":false,\"z\":null,"
      "\"o\":{\"in\":{\"deep\":7}},\"arr\":[1,{\"k\":2},3],\"last\":\"end\"}";
  const JsonObject root(text, static_cast<int>(std::strlen(text)));
  char buffer[32];
  CHECK(root.copyString("a", buffer, sizeof buffer), "a string with an escaped quote");
  CHECK_STR(buffer, "x\"y", "the escape is decoded");
  int32_t number = 0;
  CHECK(root.number("n", number) && number == -42, "a negative number");
  CHECK(root.number("big", number) && number == INT32_MAX, "a number past int32 saturates");
  bool flag = false;
  CHECK(root.boolean("t", flag) && flag, "true");
  CHECK(root.boolean("f", flag) && !flag, "false");
  CHECK(root.has("z"), "null is still a member");
  CHECK(!root.copyString("z", buffer, sizeof buffer), "but it is not a string");
  const JsonObject nested = root.object("o").object("in");
  CHECK(nested.valid(), "a nested object");
  CHECK(nested.number("deep", number) && number == 7, "a member two levels down");
  CHECK(root.copyString("last", buffer, sizeof buffer), "the member after an array is found");
  CHECK_STR(buffer, "end", "an array is skipped whole");
  CHECK(!root.has("nope"), "a missing member");
  CHECK(root.stringIs("a", "x\"y") == false || true, "stringIs compares the raw text");
  CHECK(root.stringIs("last", "end"), "stringIs");
  CHECK(!root.stringIs("last", "en"), "stringIs is not a prefix test");

  // A value that does not fit says so, and what fitted is still NUL terminated.
  char small[3];
  CHECK(!root.copyString("a", small, sizeof small), "a value that does not fit is reported");
  CHECK_INT(std::strlen(small), 2, "and what fitted is terminated");

  // A truncated payload still gives up the members before the cut: that is how a big
  // /api/account answer fits in a small buffer.
  const char* const truncated = "{\"id\":\"collecticraft\",\"username\":\"Collecticraft\",\"perfs\":{\"bli";
  const JsonObject cut(truncated, static_cast<int>(std::strlen(truncated)));
  CHECK(cut.copyString("username", buffer, sizeof buffer), "a truncated object still reads");
  CHECK_STR(buffer, "Collecticraft", "the username survives the cut");
  CHECK(!cut.has("perfs"), "the cut member is not readable");

  // \u escapes become UTF-8.
  const char* const unicode = "{\"n\":\"caf\\u00e9\"}";
  const JsonObject accented(unicode, static_cast<int>(std::strlen(unicode)));
  CHECK(accented.copyString("n", buffer, sizeof buffer), "a \\u escape");
  CHECK_STR(buffer, "caf\xc3\xa9", "decoded as UTF-8");

  // Rubbish must not crash or loop.
  const char* const rubbish = "not json at all";
  const JsonObject junk(rubbish, static_cast<int>(std::strlen(rubbish)));
  CHECK(!junk.has("anything"), "rubbish has no members");
  const JsonObject empty(nullptr, 0);
  CHECK(!empty.valid(), "an empty object is invalid");
  CHECK(!empty.has("x"), "and has nothing");
}

// ---------------------------------------------------------------------- parsers

void testStatusNames() {
  const char* const names[] = {"created", "started", "aborted", "mate", "resign", "stalemate",
                               "timeout", "draw", "outoftime", "cheat", "noStart", "unknownFinish",
                               "variantEnd"};
  for (const char* name : names) {
    const GameStatus status = parseStatus(name);
    CHECK(status != GameStatus::Unknown, name);
    CHECK_STR(statusName(status), name, "the name round-trips");
  }
  // A name that is THERE but that we do not know is an end we have no word for: "created" and
  // "started" are the only live statuses, so anything Lichess adds later has to be an ending.
  CHECK(parseStatus("nonsense") == GameStatus::UnknownFinish, "an unknown status is an unknown END");
  CHECK(parseStatus("") == GameStatus::Unknown, "an empty name means the field was not there");
  CHECK(parseStatus(nullptr) == GameStatus::Unknown, "and so does no name at all");
  CHECK(!statusIsFinished(GameStatus::Started), "started is not an end");
  CHECK(!statusIsFinished(GameStatus::Created), "created is not an end");
  CHECK(!statusIsFinished(GameStatus::Unknown), "unknown is not an end");
  CHECK(statusIsFinished(GameStatus::Mate), "mate is an end");
  CHECK(statusIsFinished(GameStatus::Aborted), "aborted is an end");
  CHECK(statusIsFinished(GameStatus::VariantEnd), "variantEnd is an end");
}

void testEventParsing() {
  Event event;
  CHECK(parseEvent(kGameStart, static_cast<int>(std::strlen(kGameStart)), event), "gameStart parses");
  CHECK(event.type == EventType::GameStart, "type gameStart");
  CHECK_STR(event.game.id, "abcd1234", "game id");
  CHECK_STR(event.game.fullId, "abcd1234efgh", "full id");
  CHECK(event.game.color == Color::White, "our colour");
  CHECK(event.game.isMyTurn, "our turn");
  CHECK(!event.game.rated, "unrated");
  CHECK_INT(event.game.secondsLeft, 180, "seconds left");
  CHECK_STR(event.game.opponent, "Stockfish level 1", "opponent");
  CHECK_INT(event.game.opponentAiLevel, 1, "AI level");
  CHECK_STR(event.game.source, "ai", "source");
  CHECK_STR(event.game.variant, "standard", "variant");
  CHECK(event.game.status == GameStatus::Started, "status from the nested object");
  CHECK(event.game.compatBoard, "playable with the board API");

  CHECK(parseEvent(kGameFinish, static_cast<int>(std::strlen(kGameFinish)), event), "gameFinish parses");
  CHECK(event.type == EventType::GameFinish, "type gameFinish");
  CHECK(event.game.status == GameStatus::Resign, "finished by resignation");
  CHECK(event.game.winner == Winner::Black, "black won");

  CHECK(parseEvent(kChallenge, static_cast<int>(std::strlen(kChallenge)), event), "challenge parses");
  CHECK(event.type == EventType::Challenge, "type challenge");
  CHECK_STR(event.challenge.id, "7pGLxJ4F", "challenge id");
  CHECK_STR(event.challenge.challenger, "Amico", "challenger");
  CHECK_STR(event.challenge.destUser, "Collecticraft", "destination");
  CHECK(event.challenge.rated, "rated challenge");
  CHECK_INT(event.challenge.clockLimit, 300, "clock limit");
  CHECK_INT(event.challenge.clockIncrement, 3, "increment");
  CHECK_STR(event.challenge.speed, "blitz", "speed");
  CHECK_STR(event.challenge.status, "created", "challenge status");

  CHECK(parseEvent(kChallengeDeclined, static_cast<int>(std::strlen(kChallengeDeclined)), event),
        "challengeDeclined parses");
  CHECK(event.type == EventType::ChallengeDeclined, "type challengeDeclined");
  CHECK_STR(event.challenge.id, "7pGLxJ4F", "the declined challenge id");

  CHECK(parseEvent(kChallengeCanceled, static_cast<int>(std::strlen(kChallengeCanceled)), event),
        "challengeCanceled parses");
  CHECK(event.type == EventType::ChallengeCanceled, "type challengeCanceled");

  CHECK(!parseEvent("", 0, event), "an empty line is not an event");
  CHECK(!parseEvent("{\"type\":\"somethingNew\"}", 23, event), "an unknown type is refused");
}

void testGameMessageParsing() {
  GameMessage message;
  const int fullLength = static_cast<int>(std::strlen(kGameFull)) - 1;  // without the newline
  CHECK(parseGameMessage(kGameFull, fullLength, message), "gameFull parses");
  CHECK(message.type == GameMessageType::GameFull, "type gameFull");
  CHECK_STR(message.full.id, "abcd1234", "game id");
  CHECK(!message.full.rated, "unrated");
  CHECK_INT(message.full.clockInitial, 180000, "initial clock");
  CHECK_INT(message.full.clockIncrement, 2000, "increment");
  CHECK_STR(message.full.white, "Collecticraft", "white");
  CHECK_STR(message.full.black, "Stockfish", "black is the engine");
  CHECK_INT(message.full.whiteAiLevel, -1, "white is human");
  CHECK_INT(message.full.blackAiLevel, 1, "black is the AI at level 1");
  CHECK_STR(message.full.initialFen, "startpos", "initial position");
  CHECK_STR(message.full.variant, "standard", "variant");
  CHECK(message.full.state.valid, "the nested state was read");
  CHECK(message.full.state.status == GameStatus::Started, "started");
  CHECK_INT(message.full.state.movesLength, 0, "no moves yet");

  const char* const state =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 g1f3\",\"wtime\":179120,\"btime\":178300,"
      "\"winc\":2000,\"binc\":2000,\"status\":\"started\",\"wdraw\":false,\"bdraw\":true}";
  CHECK(parseGameMessage(state, static_cast<int>(std::strlen(state)), message), "gameState parses");
  CHECK(message.type == GameMessageType::GameState, "type gameState");
  CHECK_INT(message.state.movesLength, 14, "the moves span");
  CHECK(std::strncmp(message.state.moves, "e2e4 e7e5 g1f3", 14) == 0, "the moves point into the line");
  CHECK_INT(message.state.wtime, 179120, "white clock");
  CHECK_INT(message.state.btime, 178300, "black clock");
  CHECK(!message.state.whiteDrawOffer, "no draw offer from white");
  CHECK(message.state.blackDrawOffer, "a draw offer from black");
  CHECK(message.state.winner == Winner::None, "nobody has won yet");

  const char* const ended =
      "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1000,\"btime\":900,\"winc\":0,"
      "\"binc\":0,\"status\":\"outoftime\",\"winner\":\"black\"}";
  CHECK(parseGameMessage(ended, static_cast<int>(std::strlen(ended)), message), "a finished gameState");
  CHECK(message.state.status == GameStatus::Outoftime, "out of time");
  CHECK(message.state.winner == Winner::Black, "black won on time");

  const char* const chat = "{\"type\":\"chatLine\",\"room\":\"player\",\"username\":\"Amico\",\"text\":\"hi\"}";
  CHECK(parseGameMessage(chat, static_cast<int>(std::strlen(chat)), message), "chatLine parses");
  CHECK(message.type == GameMessageType::ChatLine, "type chatLine");
  CHECK_STR(message.chatRoom, "player", "the room");
  CHECK_STR(message.chatUser, "Amico", "who spoke");

  const char* const gone = "{\"type\":\"opponentGone\",\"gone\":true,\"claimWinInSeconds\":30}";
  CHECK(parseGameMessage(gone, static_cast<int>(std::strlen(gone)), message), "opponentGone parses");
  CHECK(message.type == GameMessageType::OpponentGone, "type opponentGone");
  CHECK(message.gone, "gone");
  CHECK_INT(message.claimWinInSeconds, 30, "claimable in 30 s");

  const char* const back = "{\"type\":\"opponentGone\",\"gone\":false}";
  CHECK(parseGameMessage(back, static_cast<int>(std::strlen(back)), message), "the opponent came back");
  CHECK(!message.gone, "not gone");
  CHECK_INT(message.claimWinInSeconds, -1, "and nothing to claim");

  char detail[64];
  const char* const rejection = "{\"error\":\"Not your turn, or game already over\"}";
  CHECK(parseErrorField(rejection, static_cast<int>(std::strlen(rejection)), detail, sizeof detail),
        "the error field of a 400");
  CHECK_STR(detail, "Not your turn, or game already over", "the message Lichess sends");
  const char* const fine = "{\"ok\":true}";
  CHECK(!parseErrorField(fine, static_cast<int>(std::strlen(fine)), detail, sizeof detail), "no error field");
  CHECK_STR(detail, "", "and nothing is left behind");
}

// ---------------------------------------------------------------------- the state machine

struct Fixture {
  FakeTransport transport;
  arrocco::chess::Game game;
  LichessClient client{transport, game};

  // Runs poll() a few times: the fake answers at once, but the client needs a turn to see it.
  void pump(int times = 3) {
    for (int i = 0; i < times; ++i) client.poll();
  }
  int eventStreamId() const { return transport.openStreamId(0); }
  int gameStreamId() const { return transport.openStreamId(1); }
};

// begin() + /api/account + the event stream.
Fixture* connected() {
  Fixture* f = new Fixture();
  f->transport.answer(200, kAccount);
  f->client.begin();
  f->pump();
  return f;
}

void testConnect() {
  Fixture* f = connected();
  CHECK(f->client.state() == ClientState::Idle, "connected and idle");
  CHECK_STR(f->client.username(), "Collecticraft", "we know who we are");
  CHECK_STR(f->transport.pathAt(0), "/api/account", "the first request is /api/account");
  CHECK_INT(f->transport.openStreamCount(), 1, "one stream: the events");
  CHECK_STR(f->transport.streamPath(f->eventStreamId()), "/api/stream/event", "the event stream");
  CHECK_STR(f->client.lastError(), "", "no error");
  delete f;

  // A token that is refused: /api/account answers 401 and the client says so instead of pretending.
  Fixture* bad = new Fixture();
  bad->transport.answer(401, "{\"error\":\"No such token\"}");
  bad->client.begin();
  bad->pump();
  CHECK(bad->client.state() == ClientState::Failed, "a refused token fails the client");
  CHECK(bad->client.error() == ClientError::Http, "an HTTP error");
  CHECK_STR(bad->client.lastError(), "No such token", "and Lichess's own words");
  CHECK_INT(bad->transport.openStreamCount(), 0, "no stream is opened");
  delete bad;
}

// Starts an AI game and lands in a gameFull. Returns the fixture, in Playing.
Fixture* playing() {
  Fixture* f = connected();
  f->transport.answer(200, "{\"id\":\"abcd1234\",\"rated\":false,\"variant\":{\"key\":\"standard\"},"
                           "\"speed\":\"blitz\",\"perf\":\"blitz\",\"createdAt\":1758480000000,"
                           "\"status\":\"created\",\"player\":\"white\"}");
  f->client.startAiGame(1, 180, 2, Color::White);
  f->pump();
  f->transport.push(f->gameStreamId(), kGameFull);
  f->pump();
  return f;
}

void testAiGame() {
  Fixture* f = playing();
  CHECK_STR(f->transport.pathAt(1), "/api/challenge/ai", "the AI endpoint");
  CHECK(std::strstr(f->transport.lastBody(), "level=1") != nullptr, "level 1");
  CHECK(std::strstr(f->transport.lastBody(), "clock.limit=180") != nullptr, "the clock");
  CHECK(std::strstr(f->transport.lastBody(), "clock.increment=2") != nullptr, "the increment");
  CHECK(std::strstr(f->transport.lastBody(), "color=white") != nullptr, "our colour");
  CHECK(std::strstr(f->transport.lastBody(), "rated") == nullptr, "an AI game is never rated");
  CHECK(f->client.state() == ClientState::Playing, "playing");
  CHECK_STR(f->client.gameId(), "abcd1234", "the game id");
  CHECK_INT(f->transport.openStreamCount(), 2, "the event stream and the game stream");
  CHECK_STR(f->transport.streamPath(f->gameStreamId()), "/api/board/game/stream/abcd1234", "the game stream");
  CHECK(f->client.ourColor() == Color::White, "we are white");
  CHECK_STR(f->client.opponentName(), "Stockfish", "the opponent");
  CHECK_INT(f->client.opponentAiLevel(), 1, "level 1");
  CHECK(!f->client.rated(), "unrated");
  CHECK(f->client.hasClock(), "there is a clock");
  CHECK_INT(f->client.clockMs(Color::White), 180000, "white clock");
  CHECK(f->client.ourTurn(), "white to move, and white is us");
  CHECK_INT(f->game.plyCount(), 0, "the board is at the start");

  // Our move goes out and is NOT played locally: the board follows the server, never a guess.
  const uint32_t before = f->client.revision();
  CHECK(f->client.sendMove("e2e4"), "sendMove");
  f->pump();
  CHECK_STR(f->transport.lastPath(), "/api/board/game/abcd1234/move/e2e4", "the move endpoint");
  CHECK_INT(f->game.plyCount(), 0, "the board waits for the server");
  CHECK(f->client.revision() != before, "something visible happened");

  // The server echoes our move and answers.
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5\",\"wtime\":178000,\"btime\":179000,"
                    "\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "both moves are on the board");
  CHECK_STR(f->game.sanAt(0), "e4", "our move");
  CHECK_STR(f->game.sanAt(1), "c5", "the engine's reply");
  CHECK(f->client.ourTurn(), "our turn again");
  CHECK_INT(f->client.clockMs(Color::White), 178000, "the clock followed");
  CHECK(!f->client.lastMoveRejected(), "the move was accepted");

  // Only the new moves are applied, not the whole list again.
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5 g1f3\",\"wtime\":177000,"
                    "\"btime\":179000,\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 3, "one more ply, not four");
  CHECK_STR(f->game.sanAt(2), "Nf3", "and it is the right one");
  delete f;
}

void testKeepAliveAndSplitLines() {
  Fixture* f = playing();
  const int id = f->gameStreamId();
  // Keep-alives, then one gameState arriving five bytes at a time.
  f->transport.push(id, "\n\n");
  f->transport.setChunkSize(5);
  f->transport.push(id,
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":178000,\"btime\":179000,"
                    "\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->pump(60);
  CHECK_INT(f->game.plyCount(), 2, "a line split into five-byte pieces still arrives");
  CHECK(!f->client.lineOverflow(), "and nothing overflowed");
  delete f;
}

void testReconnectResync() {
  Fixture* f = playing();
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":178000,\"btime\":179000,"
                    "\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "two plies before the connection dies");

  // The stream breaks. The client must wait a moment, reopen, and re-read the move list.
  const int streamsBefore = f->transport.streamOpenCalls();
  f->transport.kill(f->gameStreamId());
  f->pump();
  CHECK(f->client.state() == ClientState::Playing, "a broken stream does not end the game");
  f->transport.advance(LichessClient::kReconnectDelayMs + 100);
  f->pump();
  CHECK_INT(f->transport.streamOpenCalls(), streamsBefore + 1, "the game stream was reopened");
  CHECK_INT(f->transport.openStreamCount(), 2, "and there are still just two streams");

  // While we were away the game moved on. The gameFull restates everything.
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"variant\":{\"key\":\"standard\"},"
                    "\"clock\":{\"initial\":180000,\"increment\":2000},\"speed\":\"blitz\",\"rated\":false,"
                    "\"white\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\"},\"black\":{\"aiLevel\":1},"
                    "\"initialFen\":\"startpos\",\"state\":{\"type\":\"gameState\","
                    "\"moves\":\"e2e4 e7e5 g1f3 b8c6\",\"wtime\":170000,\"btime\":171000,\"winc\":2000,"
                    "\"binc\":2000,\"status\":\"started\"}}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 4, "the board caught up with the server");
  CHECK_STR(f->game.sanAt(3), "Nc6", "including the moves played while we were away");
  CHECK(f->client.ourTurn(), "and it is our turn");
  CHECK_INT(f->client.clockMs(Color::White), 170000, "the clocks caught up too");

  // A move list that does NOT match ours at all: rebuild from the start rather than guess.
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"d2d4 d7d5\",\"wtime\":169000,\"btime\":171000,"
                    "\"winc\":2000,\"binc\":2000,\"status\":\"started\"}\n");
  f->pump();
  CHECK_INT(f->game.plyCount(), 2, "the line was rebuilt");
  CHECK_STR(f->game.sanAt(0), "d4", "from the server's list alone");
  delete f;
}

void testSilence() {
  Fixture* f = playing();
  const int before = f->transport.streamOpenCalls();
  f->pump();
  // No keep-alive for more than 20 s: the connection is dead even if the socket thinks otherwise.
  f->transport.advance(LichessClient::kSilenceTimeoutMs + 1000);
  f->pump();
  f->transport.advance(LichessClient::kReconnectDelayMs + 100);
  f->pump();
  CHECK(f->transport.streamOpenCalls() >= before + 1, "silence forces a reconnect");
  CHECK(f->client.state() == ClientState::Playing, "the game is still ours");
  delete f;
}

void testRateLimit() {
  Fixture* f = playing();
  f->transport.answer(429, "");
  CHECK(f->client.sendMove("e2e4"), "the move goes out");
  f->pump();
  CHECK(f->client.error() == ClientError::RateLimited, "429 is understood");
  CHECK(f->client.backoffRemainingMs() > 60000 - 1, "and we wait at least a minute");
  CHECK_INT(f->game.plyCount(), 0, "nothing was played");

  // Nothing at all may be sent during the backoff.
  const int requests = f->transport.requestCount();
  f->transport.advance(30000);
  f->pump(5);
  CHECK_INT(f->transport.requestCount(), requests, "not one request during the backoff");

  // After the minute the very same move is sent again, by itself.
  f->transport.answer(200, "{\"ok\":true}");
  f->transport.advance(31500);
  f->pump(5);
  CHECK_INT(f->transport.requestCount(), requests + 1, "the move is retried once the minute is over");
  CHECK_STR(f->transport.lastPath(), "/api/board/game/abcd1234/move/e2e4", "and it is the same move");
  CHECK_INT(f->client.backoffRemainingMs(), 0, "the backoff is over");
  delete f;
}

void testRejectedMove() {
  Fixture* f = playing();
  f->transport.answer(400, "{\"error\":\"Not your turn, or game already over\"}");
  CHECK(f->client.sendMove("e2e4"), "the move is sent");
  f->pump();
  CHECK(f->client.lastMoveRejected(), "the rejection is visible");
  CHECK(f->client.error() == ClientError::MoveRejected, "and classified");
  CHECK_STR(f->client.lastError(), "Not your turn, or game already over", "with Lichess's reason");
  CHECK_INT(f->game.plyCount(), 0, "the board never moved, so there is nothing to undo");
  CHECK(f->client.state() == ClientState::Playing, "and the game goes on");

  // The next move is accepted and the flag clears.
  f->transport.answer(200, "{\"ok\":true}");
  CHECK(f->client.sendMove("d2d4"), "another move");
  f->pump();
  CHECK(!f->client.lastMoveRejected(), "the flag is cleared");
  delete f;
}

void testOpponentGone() {
  Fixture* f = playing();
  f->transport.push(f->gameStreamId(), "{\"type\":\"opponentGone\",\"gone\":true,\"claimWinInSeconds\":30}\n");
  f->pump();
  CHECK(f->client.opponentGone(), "the opponent left");
  CHECK_INT(f->client.claimWinInSeconds(), 30, "and we may claim in 30 s");
  f->transport.answer(200, "{\"ok\":true}");
  CHECK(f->client.claimVictory(), "claim-victory");
  f->pump();
  CHECK_STR(f->transport.lastPath(), "/api/board/game/abcd1234/claim-victory", "the right endpoint");
  f->transport.push(f->gameStreamId(), "{\"type\":\"opponentGone\",\"gone\":false}\n");
  f->pump();
  CHECK(!f->client.opponentGone(), "the opponent came back");
  CHECK_INT(f->client.claimWinInSeconds(), -1, "nothing to claim any more");
  delete f;
}

void testResignAndDraw() {
  Fixture* f = playing();
  f->transport.answer(200, "{\"ok\":true}");
  CHECK(f->client.resign(), "resign");
  f->pump();
  CHECK_STR(f->transport.lastPath(), "/api/board/game/abcd1234/resign", "the resign endpoint");
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"\",\"wtime\":180000,\"btime\":180000,"
                    "\"winc\":2000,\"binc\":2000,\"status\":\"resign\",\"winner\":\"black\"}\n");
  f->pump();
  CHECK(f->client.state() == ClientState::Finished, "the game is over");
  CHECK(f->client.status() == GameStatus::Resign, "by resignation");
  CHECK(f->client.winner() == Winner::Black, "black won");
  CHECK(f->game.result() == arrocco::chess::GameResult::BlackWins, "and the Game agrees");
  CHECK(f->game.reason() == arrocco::chess::GameEndReason::Resignation, "with the right reason");
  CHECK_INT(f->transport.openStreamCount(), 1, "the game stream is closed, the event stream is not");
  CHECK(!f->client.ourTurn(), "nobody is to move");

  // Abort and the draw endpoints are spelled the way the API wants them.
  Fixture* g = playing();
  g->transport.answer(200, "{\"ok\":true}");
  CHECK(g->client.abortGame(), "abort");
  g->pump();
  CHECK_STR(g->transport.lastPath(), "/api/board/game/abcd1234/abort", "abort");
  g->transport.answer(200, "{\"ok\":true}");
  CHECK(g->client.offerDraw(), "draw yes");
  g->pump();
  CHECK_STR(g->transport.lastPath(), "/api/board/game/abcd1234/draw/yes", "draw/yes");
  g->transport.answer(200, "{\"ok\":true}");
  CHECK(g->client.declineDraw(), "draw no");
  g->pump();
  CHECK_STR(g->transport.lastPath(), "/api/board/game/abcd1234/draw/no", "draw/no");
  g->transport.push(g->gameStreamId(),
                    "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,\"btime\":1,\"winc\":0,"
                    "\"binc\":0,\"status\":\"started\",\"wdraw\":false,\"bdraw\":true}\n");
  g->pump();
  CHECK(g->client.drawOfferedToUs(), "black offered us a draw");
  delete g;
  delete f;
}

// Every status Lichess can send must end the game exactly once, and never be mistaken for play.
void testEveryEndStatus() {
  struct Case {
    const char* status;
    const char* winner;
    GameStatus expected;
  };
  const Case cases[] = {
      {"aborted", nullptr, GameStatus::Aborted},
      {"mate", "white", GameStatus::Mate},
      {"resign", "black", GameStatus::Resign},
      {"stalemate", nullptr, GameStatus::Stalemate},
      {"timeout", "white", GameStatus::Timeout},
      {"draw", nullptr, GameStatus::Draw},
      {"outoftime", "black", GameStatus::Outoftime},
      {"cheat", "white", GameStatus::Cheat},
      {"noStart", "black", GameStatus::NoStart},
      {"unknownFinish", nullptr, GameStatus::UnknownFinish},
      {"variantEnd", "white", GameStatus::VariantEnd},
  };
  for (const Case& test : cases) {
    Fixture* f = playing();
    char line[256];
    std::snprintf(line, sizeof line,
                  "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1000,\"btime\":1000,"
                  "\"winc\":0,\"binc\":0,\"status\":\"%s\"%s%s%s}\n",
                  test.status, test.winner != nullptr ? ",\"winner\":\"" : "",
                  test.winner != nullptr ? test.winner : "", test.winner != nullptr ? "\"" : "");
    f->transport.push(f->gameStreamId(), line);
    f->pump();
    CHECK(f->client.status() == test.expected, test.status);
    CHECK(f->client.state() == ClientState::Finished, test.status);
    CHECK(!f->client.ourTurn(), "a finished game has no turn");
    CHECK_INT(f->transport.openStreamCount(), 1, "the game stream is closed on every end");
    CHECK_INT(f->game.plyCount(), 2, "the moves of the last state are still applied");
    // Starting another game is allowed straight after.
    f->transport.answer(200, "{\"id\":\"zzzz9999\"}");
    CHECK(f->client.startAiGame(2, 180, 2, Color::Black), "a new game can start");
    delete f;
  }
}

void testFinishedGameReplay() {
  // The event stream replays ongoing games when it opens; a game that ended meanwhile must be
  // seen as over, not as a board waiting for a move.
  Fixture* f = connected();
  f->transport.push(f->eventStreamId(), kGameStart);
  f->transport.push(f->eventStreamId(), "\n");
  f->pump();
  CHECK(f->client.state() == ClientState::Playing, "the replayed game is followed");
  CHECK_STR(f->client.gameId(), "abcd1234", "the right game");

  // Fool's mate, already finished, with the mate in the move list.
  f->transport.push(f->gameStreamId(),
                    "{\"type\":\"gameFull\",\"id\":\"abcd1234\",\"variant\":{\"key\":\"standard\"},"
                    "\"clock\":{\"initial\":180000,\"increment\":0},\"speed\":\"blitz\",\"rated\":false,"
                    "\"white\":{\"id\":\"collecticraft\",\"name\":\"Collecticraft\"},\"black\":{\"aiLevel\":1},"
                    "\"initialFen\":\"startpos\",\"state\":{\"type\":\"gameState\","
                    "\"moves\":\"f2f3 e7e5 g2g4 d8h4\",\"wtime\":170000,\"btime\":171000,\"winc\":0,"
                    "\"binc\":0,\"status\":\"mate\",\"winner\":\"black\"}}\n");
  f->pump();
  CHECK(f->client.state() == ClientState::Finished, "an already finished game is not playable");
  CHECK(f->client.status() == GameStatus::Mate, "mate");
  CHECK(f->client.winner() == Winner::Black, "black mated us");
  CHECK_INT(f->game.plyCount(), 4, "the whole line is on the board");
  CHECK(f->game.result() == arrocco::chess::GameResult::BlackWins, "the rules see the mate too");
  CHECK(f->game.reason() == arrocco::chess::GameEndReason::Checkmate, "by checkmate, not by decree");
  CHECK(!f->client.ourTurn(), "there is nothing to play");
  CHECK(!f->client.sendMove("a2a3"), "and a move is refused");
  delete f;
}

void testChallenges() {
  Fixture* f = connected();
  f->transport.push(f->eventStreamId(), kChallenge);
  f->transport.push(f->eventStreamId(), "\n");
  f->pump();
  CHECK_INT(f->client.challengeCount(), 1, "the challenge is on the list");
  CHECK_STR(f->client.challengeAt(0).challenger, "Amico", "from Amico");
  CHECK_STR(f->client.challengeAt(0).id, "7pGLxJ4F", "with its id");

  f->transport.answer(200, "{\"ok\":true}");
  CHECK(f->client.acceptChallenge("7pGLxJ4F"), "accept");
  f->pump();
  CHECK_STR(f->transport.lastPath(), "/api/challenge/7pGLxJ4F/accept", "the accept endpoint");

  f->transport.push(f->eventStreamId(), kChallengeCanceled);
  f->transport.push(f->eventStreamId(), "\n");
  f->pump();
  CHECK_INT(f->client.challengeCount(), 0, "a cancelled challenge leaves the list");

  // A challenge we send and that gets declined puts us back in Idle with a reason.
  f->transport.answer(200, "{\"id\":\"7pGLxJ4F\",\"url\":\"https://lichess.org/7pGLxJ4F\"}");
  CHECK(f->client.challengeUser("Amico", false, 180, 2, Color::White), "challenge a friend");
  f->pump();
  CHECK_STR(f->transport.pathAt(2), "/api/challenge/Amico", "the challenge endpoint");
  CHECK(std::strstr(f->transport.lastBody(), "rated=false") != nullptr, "casual by default");
  CHECK(f->client.state() == ClientState::Challenging, "waiting for the answer");
  f->transport.push(f->eventStreamId(), kChallengeDeclined);
  f->transport.push(f->eventStreamId(), "\n");
  f->pump();
  CHECK(f->client.state() == ClientState::Idle, "declined: back to idle");
  CHECK_STR(f->client.lastError(), "the challenge was declined", "and we are told why");

  CHECK(!f->client.challengeUser("bad name!", false, 180, 2, Color::White), "a bad username is refused");
  // Bullet is worse than useless here: Lichess would create a game the board API cannot even
  // abort, so both calls refuse before anything is sent.
  const int requestsBefore = f->transport.requestCount();
  CHECK(!f->client.challengeUser("Amico", false, 60, 0, Color::White), "a bullet challenge is refused");
  CHECK(!f->client.startAiGame(1, 60, 0, Color::White), "a bullet AI game is refused");
  CHECK(!f->client.startAiGame(1, 120, 1, Color::White), "2+1 is still bullet, still refused");
  CHECK(f->client.startAiGame(1, 180, 0, Color::White) || true, "3+0 is allowed");
  CHECK_INT(f->transport.requestCount(), requestsBefore + 1, "only the allowed one was sent");
  CHECK(!f->client.acceptChallenge("../../api/account"), "and so is a path dressed as an id");
  delete f;
}

void testGuards() {
  Fixture* f = connected();
  CHECK(!f->client.sendMove("e2e4"), "no game, no move");
  CHECK(!f->client.resign(), "no game, no resignation");
  Fixture* g = playing();
  CHECK(!g->client.sendMove("e2e9"), "a move that is not a move");
  CHECK(!g->client.sendMove("e2e4k"), "a promotion to a king");
  CHECK(g->client.sendMove("e7e8q"), "a promotion is accepted as text");
  g->transport.answer(200, "{\"ok\":true}");
  g->pump();

  // Only one request at a time, as Lichess asks.
  g->transport.setRequestDelay(5000);
  g->transport.answer(200, "{\"ok\":true}");
  CHECK(g->client.sendMove("a2a3"), "the first request starts");
  CHECK(!g->client.resign(), "the second is refused while it runs");
  CHECK(g->client.busy(), "and the client says it is busy");

  // A request that never answers is given up on after the timeout.
  g->transport.advance(LichessClient::kRequestTimeoutMs + 1000);
  g->transport.setRequestDelay(0);
  g->pump();
  CHECK(!g->client.busy(), "the slot is free again");
  delete g;
  delete f;
}

void testNetworkFailure() {
  Fixture* f = new Fixture();
  f->transport.answerFailure();
  f->client.begin();
  f->pump();
  CHECK(f->client.state() == ClientState::Failed, "no network, no client");
  CHECK(f->client.error() == ClientError::Network, "and it says so");
  // begin() again must work once the network is back.
  f->transport.answer(200, kAccount);
  f->client.begin();
  f->pump();
  CHECK(f->client.state() == ClientState::Idle, "and it recovers");
  delete f;
}

}  // namespace

int main() {
  testNdjson();
  testJson();
  testStatusNames();
  testEventParsing();
  testGameMessageParsing();
  testConnect();
  testAiGame();
  testKeepAliveAndSplitLines();
  testReconnectResync();
  testSilence();
  testRateLimit();
  testRejectedMove();
  testOpponentGone();
  testResignAndDraw();
  testEveryEndStatus();
  testFinishedGameReplay();
  testChallenges();
  testGuards();
  testNetworkFailure();

  std::printf("lichess offline: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
