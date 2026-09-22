// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — the objects the two streams send, as fixed-size structs, and the parsers
// that fill them from one ndjson line. No heap, no STL, bounded copies everywhere.
//
// What is NOT copied: the moves string of a gameState. It can be a couple of kilobytes and it is
// used once, right where it is read, so GameStateInfo::moves points INTO the line buffer the
// parser was given. Do not keep a GameStateInfo past the next takeLine().
#pragma once
#include <stdint.h>

#include "arrocco/chess/position.h"
#include "arrocco/chess/types.h"

namespace arrocco::lichess {

constexpr int kGameIdSize = 12;    // Lichess game ids are 8 characters
constexpr int kFullIdSize = 16;    // full ids are 12
constexpr int kNameSize = 32;      // usernames are at most 30
constexpr int kChallengeIdSize = 12;
constexpr int kWordSize = 16;      // speed, variant key, source, chat room
// Room for a status name Lichess has not invented yet: the longest today is "unknownFinish".
constexpr int kStatusNameSize = 32;
constexpr int kErrorSize = 96;

// The `status` of a game, as Lichess spells it.
enum class GameStatus : uint8_t {
  Unknown,
  Created,
  Started,
  Aborted,
  Mate,
  Resign,
  Stalemate,
  Timeout,
  Draw,
  Outoftime,
  Cheat,
  NoStart,
  UnknownFinish,
  VariantEnd
};

enum class Winner : uint8_t { None, White, Black };

// Unknown ONLY for a name that is null or empty, i.e. "the field was not there". A name that is
// there but that we do not know is UnknownFinish: the only two live statuses are "created" and
// "started", so anything else Lichess adds later is an end. Guessing the other way would leave a
// finished game looking playable and the client reconnecting to it for ever.
GameStatus parseStatus(const char* name);
const char* statusName(GameStatus status);
// Created and Started mean "still going"; everything else is an end, Unknown included as "not yet".
bool statusIsFinished(GameStatus status);

// ---------------------------------------------------------------------- the event stream

enum class EventType : uint8_t { Unknown, GameStart, GameFinish, Challenge, ChallengeCanceled, ChallengeDeclined };

struct EventGame {
  char id[kGameIdSize] = {};
  char fullId[kFullIdSize] = {};
  char opponent[kNameSize] = {};
  char speed[kWordSize] = {};
  char source[kWordSize] = {};
  char variant[kWordSize] = {};
  int32_t secondsLeft = 0;
  int32_t opponentAiLevel = -1;  // -1 = a human
  chess::Color color = chess::Color::White;  // OUR colour in that game
  GameStatus status = GameStatus::Unknown;
  Winner winner = Winner::None;
  bool isMyTurn = false;
  bool rated = false;
  bool compatBoard = false;
};

struct ChallengeInfo {
  char id[kChallengeIdSize] = {};
  char challenger[kNameSize] = {};
  char destUser[kNameSize] = {};
  char variant[kWordSize] = {};
  char speed[kWordSize] = {};
  char status[kWordSize] = {};
  int32_t clockLimit = -1;      // seconds; -1 when there is no clock
  int32_t clockIncrement = -1;  // seconds
  int32_t days = -1;            // correspondence; -1 otherwise
  bool rated = false;
};

struct Event {
  EventType type = EventType::Unknown;
  EventGame game;
  ChallengeInfo challenge;
};

// False when the line is not an object or carries no "type": keep-alives included.
bool parseEvent(const char* line, int length, Event& out);

// ---------------------------------------------------------------------- the game stream

enum class GameMessageType : uint8_t { Unknown, GameFull, GameState, ChatLine, OpponentGone };

struct GameStateInfo {
  const char* moves = nullptr;  // points into the caller's line buffer, NOT NUL-terminated
  int movesLength = 0;
  int32_t wtime = 0, btime = 0, winc = 0, binc = 0;  // milliseconds
  GameStatus status = GameStatus::Unknown;
  Winner winner = Winner::None;
  bool whiteDrawOffer = false;
  bool blackDrawOffer = false;
  bool valid = false;
};

struct GameFullInfo {
  char id[kGameIdSize] = {};
  char white[kNameSize] = {};
  char black[kNameSize] = {};
  char speed[kWordSize] = {};
  char variant[kWordSize] = {};
  char initialFen[chess::kFenBufferSize] = {};  // "startpos" or a FEN
  int32_t clockInitial = -1;                    // milliseconds; -1 when correspondence
  int32_t clockIncrement = -1;
  int whiteAiLevel = -1;  // -1 = a human
  int blackAiLevel = -1;
  bool rated = false;
  GameStateInfo state;
};

struct GameMessage {
  GameMessageType type = GameMessageType::Unknown;
  GameFullInfo full;    // GameFull only
  GameStateInfo state;  // GameState, and a copy of full.state for GameFull
  char chatRoom[kWordSize] = {};
  char chatUser[kNameSize] = {};
  bool gone = false;              // OpponentGone
  int32_t claimWinInSeconds = -1;
};

bool parseGameMessage(const char* line, int length, GameMessage& out);

// The "error" field Lichess puts in a 4xx body ({"error":"Not your turn"}), for lastError().
// Copies "" and returns false when the body has none.
bool parseErrorField(const char* body, int length, char* out, int outSize);

}  // namespace arrocco::lichess
