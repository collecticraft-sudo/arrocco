// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Position: one chess position and everything that can be asked of it.
//
// A Position is a small value (about 80 bytes): copy it freely, keep one per screen, compare two.
// It knows the rules (legal moves, check, mate, draws that depend on the position alone) and the
// notations (FEN, UCI, SAN). What needs the HISTORY of a game — stepping back and forth,
// threefold repetition, the list of captured pieces — lives in Game (game.h).
//
// Standard chess only: no Chess960.
//
// Stack use: generating moves needs one MoveList (0.5 KB) from the caller; toSan/parseSan/
// parseUci/isLegal/findLegalMove put one more MoveList and one Position copy on the stack
// (about 0.7 KB). All safe on the 8 KB Arduino loop task.
#pragma once
#include <cstdint>

#include "arrocco/chess/move.h"
#include "arrocco/chess/types.h"

namespace arrocco::chess {

constexpr int kFenBufferSize = 96;  // longest possible FEN is 92 chars + NUL
constexpr const char* kStartposFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// What make() must remember so that unmake() can restore the position exactly. 16 bytes.
struct Undo {
  uint64_t hash;           // hash of the position BEFORE the move
  Move move;
  uint16_t halfmoveClock;  // before the move
  Piece captured;          // none, or the captured piece (the pawn, for en passant)
  uint8_t castlingRights;  // before the move
  Square enPassantSquare;  // before the move
};
static_assert(sizeof(Undo) == 16, "Game keeps 1024 of these: keep it at 16 bytes");

class Position {
 public:
  Position() { setStartpos(); }

  // ------------------------------------------------------------ setup

  void setStartpos();

  // Reads a FEN. Fields after the piece placement may be missing ("w - - 0 1" is assumed), so the
  // 4-field FENs found in perft tables and puzzle files work too.
  // Returns false and leaves this position UNCHANGED if the text is not a FEN (clocks that are not
  // numbers and anything after the sixth field included) or describes a position the rules cannot
  // handle: not exactly one king each, pawns on rank 1 or 8, more than 16 pieces a side, or the
  // side NOT to move being in check.
  // Two fields are normalised rather than rejected:
  //   - a castling right whose king or rook is not on its home square is dropped;
  //   - the en-passant square is kept only when an en-passant capture is really legal (see
  //     enPassantSquare()). So "... b KQkq e3 0 1" after 1.e4 reads back as "... b KQkq - 0 1".
  bool setFen(const char* fen);

  // Writes the FEN (NUL-terminated) and returns its length, or 0 if outSize is too small.
  // kFenBufferSize is always enough.
  int toFen(char* out, int outSize) const;

  // ------------------------------------------------------------ state

  Color sideToMove() const { return sideToMove_; }
  Piece pieceAt(Square s) const { return s < 64 ? board_[s] : Piece(); }
  Square kingSquare(Color c) const { return kingSquare_[indexOf(c)]; }

  // Mask of CastlingRight bits. A right means "king and rook have not moved", not "castling is
  // legal right now" (ask the move generator for that).
  uint8_t castlingRights() const { return castlingRights_; }
  bool hasCastlingRight(CastlingRight right) const { return (castlingRights_ & right) != 0; }

  // The square a pawn would capture TO en passant, or kNoSquare.
  // Set only when at least one en-passant capture is LEGAL (as Lichess and python-chess do), not
  // after every double push. This is what makes repetition detection follow the FIDE rule: two
  // positions differ only if the possible moves differ.
  Square enPassantSquare() const { return enPassantSquare_; }

  int halfmoveClock() const { return halfmoveClock_; }    // plies since the last capture or pawn move
  int fullmoveNumber() const { return fullmoveNumber_; }  // starts at 1, incremented after Black moves

  // Zobrist hash of placement + side to move + castling rights + en-passant square (not the clocks).
  // Maintained incrementally by make/unmake; equal hashes = same position for the repetition rule.
  uint64_t hash() const { return hash_; }
  uint64_t computeHash() const;  // the same value recomputed from scratch (for tests and self-checks)

  // Same placement, side, castling rights and en-passant square (clocks ignored): FIDE's "same position".
  bool samePositionAs(const Position& other) const;
  // Everything equal, clocks and hash included.
  bool operator==(const Position& other) const;
  bool operator!=(const Position& other) const { return !(*this == other); }

  int pieceCount(Color c, PieceType t) const;
  int materialOf(Color c) const;  // in pawns: P=1 N=3 B=3 R=5 Q=9

  // ------------------------------------------------------------ moves

  // All legal moves into the caller's list (cleared first). Returns the count.
  int generateLegalMoves(MoveList& out) const;

  // Legal moves of the piece on `from` (none if empty or the opponent's). A promotion shows up
  // as four moves with the same from/to. Returns the count.
  int generateLegalMovesFrom(Square from, MoveList& out) const;

  // The squares to mark with a dot when the player taps `from`.
  SquareSet legalTargetsFrom(Square from) const;

  bool hasLegalMoves() const;

  // True if `m` (kind and promotion piece included) is exactly one of the legal moves.
  bool isLegal(Move m) const;

  // The legal move from `from` to `to`, or Move::none(). For a promotion pass the chosen piece;
  // with PieceType::None a promotion is NOT matched (ask isPromotionMove first and show the picker).
  Move findLegalMove(Square from, Square to, PieceType promotion = PieceType::None) const;

  // True if moving from `from` to `to` is a legal pawn promotion: time to show the piece picker.
  bool isPromotionMove(Square from, Square to) const;

  // For a move of THIS position: does it capture (en passant included), and which piece type moves.
  bool isCapture(Move m) const;
  PieceType movedPieceType(Move m) const { return pieceAt(m.from()).type(); }

  // Plays a move. `m` MUST be legal in this position (come from the generator or pass isLegal):
  // nothing is checked here. Fills `undo` for unmake().
  void make(Move m, Undo& undo);
  // Takes back the move recorded in `undo`. Undos must be consumed in reverse order of make().
  void unmake(const Undo& undo);

  // ------------------------------------------------------------ status

  bool isAttacked(Square s, Color by) const;  // is `s` attacked by any piece of colour `by`
  bool inCheck() const { return isAttacked(kingSquare(sideToMove_), opposite(sideToMove_)); }
  // The king to highlight: the side to move's king square if it is in check, else kNoSquare.
  Square checkedKingSquare() const { return inCheck() ? kingSquare(sideToMove_) : kNoSquare; }

  bool isCheckmate() const { return inCheck() && !hasLegalMoves(); }
  bool isStalemate() const { return !inCheck() && !hasLegalMoves(); }
  // 100 plies without capture or pawn move. (Checkmate on that very move still wins: test mate first.)
  bool isFiftyMoveDraw() const { return halfmoveClock_ >= 100; }
  // No sequence of legal moves can mate: K v K, K + one minor v K, or only bishops left and all of
  // them on squares of one colour (K+B v K+B with same-coloured bishops is the usual case).
  bool isInsufficientMaterial() const;

  // ------------------------------------------------------------ notation

  // UCI / long algebraic -> legal move of this position, or Move::none() if malformed or illegal.
  // Castling is accepted both as the king's move ("e1g1") and as king-takes-own-rook ("e1h1"),
  // because Lichess may send either. The promotion letter may be upper or lower case.
  Move parseUci(const char* text) const;

  // SAN of a legal move of this position: "Nbd2", "exd6", "e8=Q+", "O-O-O", "Qh4#".
  // `out` must hold kSanBufferSize chars. Writes "??" if the move is not legal here.
  void toSan(Move m, char* out) const;

  // SAN -> legal move of this position, or Move::none() if it matches no legal move or more than one.
  // Reads up to the first space or NUL. Tolerant on decoration: trailing "+", "#", "!", "?" are
  // ignored and so is "e.p.", "0-0" is accepted for "O-O", "e8Q" for "e8=Q", "ed5" for "exd5",
  // over-specified moves ("Ng1f3") too. Piece letters must be upper case: "bxc3" is a pawn, "Bxc3" a bishop.
  Move parseSan(const char* text) const;

 private:
  struct LegalityContext;
  struct Uninitialised {};
  explicit Position(Uninitialised) {}  // setFen() fills a scratch position field by field

  void clear();
  void putPiece(Square s, Piece p);
  void removePiece(Square s);
  void movePiece(Square from, Square to);

  void generatePseudoLegalFrom(Square from, MoveList& out) const;
  void generatePawnMoves(Square from, MoveList& out) const;
  void generateCastling(MoveList& out) const;
  void keepLegalOnly(MoveList& moves, LegalityContext& context) const;
  SquareSet pinnedPieces() const;
  bool leavesKingSafe(Move m);        // edits board_ and restores it
  bool hasLegalEnPassantCapture();    // same
  void normaliseEnPassantSquare();

  Piece board_[64];
  Square kingSquare_[2];
  Color sideToMove_;
  uint8_t castlingRights_;
  Square enPassantSquare_;
  uint16_t halfmoveClock_;
  uint16_t fullmoveNumber_;
  uint64_t hash_;
};

// The squares whose content differs between two positions: what to redraw / highlight.
// After a normal move: from and to. En passant: three squares. Castling: four.
SquareSet changedSquares(const Position& a, const Position& b);

// Counts the leaf nodes of the legal move tree `depth` plies deep. The standard way to prove a move
// generator correct (see test/chess); also a handy on-device self-test: perft(startpos, 3) == 8902.
uint64_t perft(Position& position, int depth);

}  // namespace arrocco::chess
