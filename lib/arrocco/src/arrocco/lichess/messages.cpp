// SPDX-License-Identifier: GPL-3.0-or-later
#include "arrocco/lichess/messages.h"

#include "arrocco/lichess/json.h"

namespace arrocco::lichess {
namespace {

bool same(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

struct StatusName {
  GameStatus status;
  const char* name;
};

// The order Lichess numbers them in; "unknown" is ours, for "the field was not there".
constexpr StatusName kStatusNames[] = {
    {GameStatus::Created, "created"},   {GameStatus::Started, "started"},
    {GameStatus::Aborted, "aborted"},   {GameStatus::Mate, "mate"},
    {GameStatus::Resign, "resign"},     {GameStatus::Stalemate, "stalemate"},
    {GameStatus::Timeout, "timeout"},   {GameStatus::Draw, "draw"},
    {GameStatus::Outoftime, "outoftime"}, {GameStatus::Cheat, "cheat"},
    {GameStatus::NoStart, "noStart"},   {GameStatus::UnknownFinish, "unknownFinish"},
    {GameStatus::VariantEnd, "variantEnd"},
};

Winner winnerFromName(const char* name) {
  if (same(name, "white")) return Winner::White;
  if (same(name, "black")) return Winner::Black;
  return Winner::None;
}

// Reads a "winner" member when it is there (it is absent, or null, while the game runs).
Winner readWinner(const JsonObject& object) {
  char name[kWordSize] = {};
  if (!object.copyString("winner", name, sizeof name)) return Winner::None;
  return winnerFromName(name);
}

// A player slot of gameFull: {"id","name",...} for a human, {"aiLevel":3} for Stockfish.
void readPlayer(const JsonObject& game, const char* key, char* name, int nameSize, int& aiLevel) {
  aiLevel = -1;
  name[0] = '\0';
  const JsonObject player = game.object(key);
  if (!player.valid()) return;
  int32_t level = 0;
  if (player.number("aiLevel", level)) {
    aiLevel = static_cast<int>(level);
    // A readable name for the UI, without a printf: "Stockfish level N" is built by the caller.
    const char* kAi = "Stockfish";
    int i = 0;
    for (; kAi[i] != '\0' && i + 1 < nameSize; ++i) name[i] = kAi[i];
    name[i] = '\0';
    return;
  }
  if (!player.copyString("name", name, nameSize)) player.copyString("id", name, nameSize);
}

void readStateFields(const JsonObject& state, GameStateInfo& out) {
  out = GameStateInfo();
  if (!state.valid()) return;
  JsonValue moves;
  if (state.find("moves", moves) && moves.type == JsonValue::Type::String) {
    out.moves = moves.raw + 1;              // past the opening quote
    out.movesLength = moves.size - 2;       // without the quotes
    if (out.movesLength < 0) out.movesLength = 0;
  }
  state.number("wtime", out.wtime);
  state.number("btime", out.btime);
  state.number("winc", out.winc);
  state.number("binc", out.binc);
  // Not `if (copyString(...))`: a status too long for the array made copyString return false and
  // the whole field was dropped, so an end looked like "still running". Use whatever landed —
  // a prefix of an unknown name is still an unknown name, and parseStatus calls that an end.
  char status[kStatusNameSize] = {};
  state.copyString("status", status, sizeof status);
  if (status[0] != '\0') out.status = parseStatus(status);
  out.winner = readWinner(state);
  state.boolean("wdraw", out.whiteDrawOffer);
  state.boolean("bdraw", out.blackDrawOffer);
  out.valid = true;
}

}  // namespace

GameStatus parseStatus(const char* name) {
  if (name == nullptr || name[0] == '\0') return GameStatus::Unknown;  // the field was not there
  for (const StatusName& entry : kStatusNames) {
    if (same(entry.name, name)) return entry.status;
  }
  // A status Lichess sent and we do not know. "created" and "started" are the only live ones, so
  // this is an end we have no name for — the safe guess, because the other one leaves a finished
  // game looking playable and the client reopening its stream for ever.
  return GameStatus::UnknownFinish;
}

const char* statusName(GameStatus status) {
  for (const StatusName& entry : kStatusNames) {
    if (entry.status == status) return entry.name;
  }
  return "unknown";
}

bool statusIsFinished(GameStatus status) {
  return status != GameStatus::Unknown && status != GameStatus::Created && status != GameStatus::Started;
}

bool parseEvent(const char* line, int length, Event& out) {
  out = Event();
  const JsonObject root(line, length);
  // "challengeCanceled" is 17 characters: kWordSize would truncate it and lose the event.
  char type[32] = {};
  if (!root.copyString("type", type, sizeof type)) return false;

  if (same(type, "gameStart") || same(type, "gameFinish")) {
    out.type = same(type, "gameStart") ? EventType::GameStart : EventType::GameFinish;
    const JsonObject game = root.object("game");
    if (!game.valid()) return false;
    EventGame& g = out.game;
    // An id that does not FIT must not be used: a truncated game id is a different game, and we
    // would open its stream and POST our moves into it. copyString says so; believe it.
    if (!game.copyString("gameId", g.id, sizeof g.id)) {
      g.id[0] = '\0';
      return false;
    }
    if (!game.copyString("fullId", g.fullId, sizeof g.fullId)) g.fullId[0] = '\0';
    char colour[kWordSize] = {};
    if (game.copyString("color", colour, sizeof colour)) {
      g.color = same(colour, "black") ? chess::Color::Black : chess::Color::White;
    }
    game.boolean("isMyTurn", g.isMyTurn);
    game.boolean("rated", g.rated);
    game.number("secondsLeft", g.secondsLeft);
    game.copyString("speed", g.speed, sizeof g.speed);
    game.copyString("source", g.source, sizeof g.source);
    const JsonObject variant = game.object("variant");
    if (variant.valid()) variant.copyString("key", g.variant, sizeof g.variant);
    const JsonObject opponent = game.object("opponent");
    if (opponent.valid()) {
      if (!opponent.copyString("username", g.opponent, sizeof g.opponent)) {
        opponent.copyString("id", g.opponent, sizeof g.opponent);
      }
      int32_t level = 0;
      if (opponent.number("ai", level)) g.opponentAiLevel = level;
    }
    // "status" is {"id":20,"name":"started"} here, a plain string in the game stream.
    char statusName[kStatusNameSize] = {};
    const JsonObject status = game.object("status");
    if (status.valid()) {
      status.copyString("name", statusName, sizeof statusName);
    } else {
      game.copyString("status", statusName, sizeof statusName);
    }
    if (statusName[0] != '\0') g.status = parseStatus(statusName);
    g.winner = readWinner(game);
    const JsonObject compat = game.object("compat");
    if (compat.valid()) compat.boolean("board", g.compatBoard);
    return g.id[0] != '\0';
  }

  if (same(type, "challenge") || same(type, "challengeCanceled") || same(type, "challengeDeclined")) {
    out.type = same(type, "challenge")          ? EventType::Challenge
               : same(type, "challengeCanceled") ? EventType::ChallengeCanceled
                                                 : EventType::ChallengeDeclined;
    const JsonObject challenge = root.object("challenge");
    if (!challenge.valid()) return false;
    ChallengeInfo& c = out.challenge;
    if (!challenge.copyString("id", c.id, sizeof c.id)) {  // a truncated id is somebody else's
      c.id[0] = '\0';
      return false;
    }
    challenge.copyString("status", c.status, sizeof c.status);
    challenge.copyString("speed", c.speed, sizeof c.speed);
    challenge.boolean("rated", c.rated);
    const JsonObject variant = challenge.object("variant");
    if (variant.valid()) variant.copyString("key", c.variant, sizeof c.variant);
    const JsonObject challenger = challenge.object("challenger");
    if (challenger.valid()) {
      if (!challenger.copyString("name", c.challenger, sizeof c.challenger)) {
        challenger.copyString("id", c.challenger, sizeof c.challenger);
      }
    }
    const JsonObject dest = challenge.object("destUser");
    if (dest.valid()) {
      if (!dest.copyString("name", c.destUser, sizeof c.destUser)) {
        dest.copyString("id", c.destUser, sizeof c.destUser);
      }
    }
    const JsonObject control = challenge.object("timeControl");
    if (control.valid()) {
      control.number("limit", c.clockLimit);
      control.number("increment", c.clockIncrement);
      control.number("daysPerTurn", c.days);
    }
    return c.id[0] != '\0';
  }

  out.type = EventType::Unknown;
  return false;
}

bool parseGameMessage(const char* line, int length, GameMessage& out) {
  out = GameMessage();
  const JsonObject root(line, length);
  // Long enough for the longest "type" Lichess sends, "opponentGone" included.
  char type[32] = {};
  if (!root.copyString("type", type, sizeof type)) return false;

  if (same(type, "gameFull")) {
    out.type = GameMessageType::GameFull;
    GameFullInfo& f = out.full;
    if (!root.copyString("id", f.id, sizeof f.id)) {  // same rule as the event stream
      f.id[0] = '\0';
      return false;
    }
    root.boolean("rated", f.rated);
    root.copyString("speed", f.speed, sizeof f.speed);
    const JsonObject variant = root.object("variant");
    if (variant.valid()) variant.copyString("key", f.variant, sizeof f.variant);
    const JsonObject clock = root.object("clock");
    if (clock.valid()) {
      clock.number("initial", f.clockInitial);
      clock.number("increment", f.clockIncrement);
    }
    readPlayer(root, "white", f.white, sizeof f.white, f.whiteAiLevel);
    readPlayer(root, "black", f.black, sizeof f.black, f.blackAiLevel);
    if (!root.copyString("initialFen", f.initialFen, sizeof f.initialFen)) {
      f.initialFen[0] = '\0';
    }
    readStateFields(root.object("state"), f.state);
    out.state = f.state;
    return f.id[0] != '\0';
  }

  if (same(type, "gameState")) {
    out.type = GameMessageType::GameState;
    readStateFields(root, out.state);
    return out.state.valid;
  }

  if (same(type, "chatLine")) {
    out.type = GameMessageType::ChatLine;
    root.copyString("room", out.chatRoom, sizeof out.chatRoom);
    root.copyString("username", out.chatUser, sizeof out.chatUser);
    return true;
  }

  if (same(type, "opponentGone")) {
    out.type = GameMessageType::OpponentGone;
    root.boolean("gone", out.gone);
    if (!root.number("claimWinInSeconds", out.claimWinInSeconds)) out.claimWinInSeconds = -1;
    return true;
  }

  out.type = GameMessageType::Unknown;
  return false;
}

bool parseErrorField(const char* body, int length, char* out, int outSize) {
  if (out != nullptr && outSize > 0) out[0] = '\0';
  const JsonObject root(body, length);
  return root.copyString("error", out, outSize);
}

}  // namespace arrocco::lichess
