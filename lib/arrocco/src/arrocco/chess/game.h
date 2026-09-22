// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Game: a Position plus the line of moves that led to it.
//
// Everything that needs HISTORY lives here: stepping back and forth through the game, threefold
// repetition, the captured pieces, the move list for the engine, PGN.
//
// A Game holds ONE line of up to kMaxGamePlies plies in a fixed array (no heap). Two indices walk it:
//   plyCount()    how many plies the line has;
//   currentPly()  how many of them have been played on position(): the position being SHOWN.
// Normally they are equal ("at the latest position"). stepBack()/stepForward() move currentPly()
// only, so browsing loses nothing. Playing a move while browsing truncates the line at the shown
// position and continues from there, as every chess GUI does.
//
// Size: about 25 KB (24 bytes per ply). Make it a static or a member of a long-lived object;
// never a local variable on the 8 KB Arduino loop task.
#pragma once
#include <cstdint>

#include "arrocco/chess/move.h"
#include "arrocco/chess/position.h"
#include "arrocco/chess/types.h"

namespace arrocco::chess {

constexpr int kMaxGamePlies = 1024;
// Enough for uciMoveList() of a full history: "e7e8q" plus a separator (or the final NUL) per ply.
constexpr int kUciMoveListBufferSize = kMaxGamePlies * 6;
// Enough for toPgn() of a full history as long as the six caller tags total under 1 KB.
constexpr int kPgnBufferSize = 14 * 1024;
constexpr int kMaxCapturedPieces = 15;  // everything but the king

enum class GameResult : uint8_t { Ongoing, WhiteWins, BlackWins, Draw };

enum class GameEndReason : uint8_t {
  None,
  // Found by Game from the rules:
  Checkmate,
  Stalemate,
  FiftyMove,
  ThreefoldRepetition,
  InsufficientMaterial,
  // Declared by the application (declareResult):
  Resignation,
  Timeout,
  Agreement
};

// Why the last play()/playUci()/playSan() returned false.
enum class PlayError : uint8_t {
  None,         // the last play succeeded
  IllegalMove,  // not a legal move of the shown position (or unreadable text)
  GameOver,     // the application declared a result: clearDeclaredResult() or takeBack() first
  HistoryFull   // kMaxGamePlies reached: the move was NOT played, nothing changed
};

// "1-0", "0-1", "1/2-1/2" or "*".
const char* pgnResultToken(GameResult result);

// The pieces of one colour that have been captured, most valuable first (Q R B N P).
struct CapturedList {
  uint8_t count = 0;
  PieceType types[kMaxCapturedPieces] = {};

  int material() const {  // in pawns
    int sum = 0;
    for (int i = 0; i < count; ++i) sum += materialValue(types[i]);
    return sum;
  }
};

// PGN seven-tag roster minus Result (which Game knows). nullptr or "" is written as "?".
struct PgnTags {
  const char* event = nullptr;
  const char* site = nullptr;
  const char* date = nullptr;  // "2026.09.22"; unknown is "????.??.??"
  const char* round = nullptr;
  const char* white = nullptr;
  const char* black = nullptr;
};

class Game {
 public:
  using Result = GameResult;
  using Reason = GameEndReason;

  Game() { newGame(); }

  // ------------------------------------------------------------ setup

  void newGame();  // standard starting position, empty history

  // Starts a game from any position. Returns false and changes NOTHING if Position::setFen rejects it.
  bool setFromFen(const char* fen);

  const Position& startPosition() const { return start_; }
  bool startedFromStartpos() const { return startedFromStartpos_; }

  // ------------------------------------------------------------ the shown position

  const Position& position() const { return position_; }
  int plyCount() const { return count_; }     // length of the line
  int currentPly() const { return cursor_; }  // plies played on position(): 0 = start position
  bool atLatest() const { return cursor_ == count_; }
  bool atStart() const { return cursor_ == 0; }

  // The move that produced position() — the one to highlight — or Move::none() at the start.
  Move lastMove() const { return cursor_ > 0 ? history_[cursor_ - 1].undo.move : Move::none(); }

  // ------------------------------------------------------------ playing

  // Plays a legal move of position(). While browsing, the plies after currentPly() are dropped first
  // (and with them a declared result). Returns false, changing nothing, when the move is refused:
  // see lastError(). Draws that the rules merely allow to CLAIM (threefold, fifty moves) and
  // insufficient material do not block play(): result() reports them and the application decides
  // whether to stop — a Lichess game, for one, goes on until the server says otherwise.
  bool play(Move m);
  bool playUci(const char* text);  // "e2e4", "e7e8q", castling "e1g1" or "e1h1"
  bool playSan(const char* text);  // "Nf3", "exd8=Q+", "0-0"; see Position::parseSan
  PlayError lastError() const { return lastError_; }

  // True when the line is kMaxGamePlies long: play() at the latest position will be refused.
  bool isHistoryFull() const { return count_ >= kMaxGamePlies; }

  // Takes back the LAST ply of the line for good (jumping to the latest position first) and clears a
  // declared result. Returns false if there is nothing to take back.
  bool takeBack();

  // ------------------------------------------------------------ browsing

  bool stepBack();     // false at the start
  bool stepForward();  // false at the latest position
  void goToStart();
  void goToLatest();
  bool goToPly(int ply);  // 0..plyCount(); false (nothing changes) if out of range

  // ------------------------------------------------------------ the line

  // Ply indices are 0-based: 0 is the first move played, plyCount() - 1 the last.
  Move moveAt(int index) const;        // Move::none() if out of range
  const char* sanAt(int index) const;  // "" if out of range; valid until the line changes
  // Whose move ply `index` is, and its move number as printed ("12." / "12...").
  Color sideOfPly(int index) const;
  int moveNumberOfPly(int index) const;

  // ------------------------------------------------------------ result

  // State of the LATEST position of the line (not of the one being browsed). A declared result wins
  // over a draw the rules only offer, never over checkmate or stalemate.
  GameResult result() const { return endedByRule() || !hasDeclaredResult() ? ruleResult_ : declaredResult_; }
  GameEndReason reason() const { return endedByRule() || !hasDeclaredResult() ? ruleReason_ : declaredReason_; }
  bool isOver() const { return result() != GameResult::Ongoing; }

  // For the application: Resignation, Timeout or Agreement, with a result other than Ongoing.
  // Returns false (nothing changes) for any other reason, or when checkmate/stalemate already
  // ended the game.
  bool declareResult(GameResult result, GameEndReason reason);
  void clearDeclaredResult();

  // How many times the SHOWN position has occurred up to currentPly(), itself included (1 = new).
  // Same position = same placement, side to move, castling rights and en-passant possibilities;
  // Position only keeps an en-passant square when a capture onto it is legal, so its hash already
  // follows the FIDE definition.
  int repetitionCount() const;

  // ------------------------------------------------------------ for the UI and the engine

  // Pieces of colour `c` captured between startPosition() and the shown position (so White's list
  // is what Black has taken). What was already missing in a FEN start is not listed.
  // A promoted piece that gets captured is listed as what it was when captured.
  CapturedList capturedPieces(Color c) const;

  // "e2e4 e7e5 g1f3" — the moves from startPosition() to the SHOWN position, for
  // "position startpos moves ..." / "position fen <startPosition> moves ...".
  // Returns the length (0 for no moves), or -1 if `outSize` is too small: then `out` holds "",
  // never a partial list. kUciMoveListBufferSize is always enough.
  int uciMoveList(char* out, int outSize) const;

  // PGN of the whole line: tag pairs (plus SetUp/FEN when not from the standard position),
  // movetext wrapped at 80 columns, result token. Returns the length, or 0 if `outSize` is too
  // small (then `out` holds ""). See kPgnBufferSize.
  int toPgn(char* out, int outSize, const PgnTags& tags = PgnTags()) const;

 private:
  struct Ply {
    Undo undo;                  // the move, what it captured, and the hash BEFORE it
    char san[kSanBufferSize];
  };
  static_assert(sizeof(Ply) == 24, "Game keeps 1024 of these");

  void reset(const Position& start, bool fromStartpos);
  void updateRuleResult();                   // needs position_ at the latest position
  bool endedByRule() const {                 // no moves left: nothing the application says can change that
    return ruleReason_ == GameEndReason::Checkmate || ruleReason_ == GameEndReason::Stalemate;
  }
  bool hasDeclaredResult() const { return declaredReason_ != GameEndReason::None; }
  uint64_t hashAfterPlies(int plies) const;  // 0 = start position ... count_ = latest
  int countRepetitions(int plies, int halfmoveClock) const;

  Position start_;
  Position position_;  // start_ plus the first cursor_ plies
  uint64_t latestHash_ = 0;
  uint16_t count_ = 0;
  uint16_t cursor_ = 0;
  bool startedFromStartpos_ = true;
  GameResult ruleResult_ = GameResult::Ongoing;
  GameEndReason ruleReason_ = GameEndReason::None;
  GameResult declaredResult_ = GameResult::Ongoing;
  GameEndReason declaredReason_ = GameEndReason::None;
  PlayError lastError_ = PlayError::None;
  Ply history_[kMaxGamePlies];
};

}  // namespace arrocco::chess
