// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — legal move generation.
//
// Method: generate pseudo-legal moves piece by piece, then drop those that leave the own king
// attacked. The expensive test (play the move on a scratch board, look at the king) is skipped
// when it cannot fail: the king is not in check, the piece is not pinned, and the move is
// neither a king move nor an en-passant capture. Correctness is proven by perft (test/chess).
#include "arrocco/chess/geometry.h"
#include "arrocco/chess/position.h"

namespace arrocco::chess {

namespace {
constexpr int kPawnCaptureSides[2] = {-1, +1};  // file offsets: towards the a-file, towards the h-file
}  // namespace

// What the legality filter needs, computed once per generation.
struct Position::LegalityContext {
  explicit LegalityContext(const Position& position)
      : scratch(position), inCheck(position.inCheck()), pinned(position.pinnedPieces()) {}
  Position scratch;  // a private copy, so that generation can stay const on the real position
  bool inCheck;
  SquareSet pinned;
};

// ------------------------------------------------------------------ pseudo-legal moves

void Position::generatePawnMoves(Square from, MoveList& out) const {
  const Color us = sideToMove_;
  const int forward = (us == Color::White) ? 8 : -8;
  const int startRank = (us == Color::White) ? 1 : 6;
  const int lastRankBeforePromotion = (us == Color::White) ? 6 : 1;
  const bool promotes = rankOf(from) == lastRankBeforePromotion;

  auto addPawnMove = [&](Square to) {
    if (!promotes) {
      out.add(Move(from, to));
      return;
    }
    // Queen first: it is what the player wants nine times out of ten.
    out.add(Move(from, to, Move::Kind::Promotion, PieceType::Queen));
    out.add(Move(from, to, Move::Kind::Promotion, PieceType::Rook));
    out.add(Move(from, to, Move::Kind::Promotion, PieceType::Bishop));
    out.add(Move(from, to, Move::Kind::Promotion, PieceType::Knight));
  };

  // setFen() refuses pawns on the first and last rank, so `from + forward` is always on the board.
  const Square ahead = static_cast<Square>(from + forward);
  if (board_[ahead].isNone()) {
    addPawnMove(ahead);
    const Square twoAhead = static_cast<Square>(ahead + forward);
    if (rankOf(from) == startRank && board_[twoAhead].isNone()) out.add(Move(from, twoAhead));
  }

  for (const int side : kPawnCaptureSides) {
    const int file = fileOf(from) + side;
    if (file < 0 || file > 7) continue;
    const Square target = static_cast<Square>(ahead + side);
    const Piece victim = board_[target];
    if (victim && victim.color() != us) {
      addPawnMove(target);
    } else if (target == enPassantSquare_) {
      out.add(Move(from, target, Move::Kind::EnPassant));
    }
  }
}

void Position::generateCastling(MoveList& out) const {
  const Color us = sideToMove_;
  const Color them = opposite(us);
  for (const CastlingSpec& spec : kCastlingSpecs) {
    if (spec.color != us || (castlingRights_ & spec.right) == 0) continue;
    const int towardsRook = (spec.rookFrom > spec.kingFrom) ? +1 : -1;

    bool pathIsEmpty = true;
    for (int s = spec.kingFrom + towardsRook; s != spec.rookFrom; s += towardsRook)
      if (board_[s]) pathIsEmpty = false;
    if (!pathIsEmpty) continue;

    // The king may not castle out of, through, or into check. Tested here in full, so a
    // generated castling move is already legal.
    bool kingPathIsSafe = true;
    for (int s = spec.kingFrom;; s += towardsRook) {
      if (isAttacked(static_cast<Square>(s), them)) kingPathIsSafe = false;
      if (s == spec.kingTo) break;
    }
    if (kingPathIsSafe) out.add(Move(spec.kingFrom, spec.kingTo, Move::Kind::Castling));
  }
}

void Position::generatePseudoLegalFrom(Square from, MoveList& out) const {
  const Piece piece = board_[from];
  if (piece.isNone() || piece.color() != sideToMove_) return;
  const Color us = sideToMove_;
  const PieceType type = piece.type();

  if (type == PieceType::Pawn) {
    generatePawnMoves(from, out);
    return;
  }

  if (type == PieceType::Knight || type == PieceType::King) {
    const Step* steps = (type == PieceType::Knight) ? kKnightSteps : kKingSteps;
    for (int i = 0; i < 8; ++i) {
      const int f = fileOf(from) + steps[i].file, r = rankOf(from) + steps[i].rank;
      if (!isOnBoard(f, r)) continue;
      const Square to = makeSquare(f, r);
      if (board_[to].isNone() || board_[to].color() != us) out.add(Move(from, to));
    }
    if (type == PieceType::King) generateCastling(out);
    return;
  }

  // Sliders: the bishop uses the diagonal half of the direction table, the rook the other half.
  const int firstDir = (type == PieceType::Bishop) ? kFirstDiagonal : kFirstOrthogonal;
  const int lastDir = (type == PieceType::Rook) ? kFirstDiagonal : kDirectionCount;
  for (int dir = firstDir; dir < lastDir; ++dir) {
    int to = from;
    for (int n = kStepsToEdge.steps[from][dir]; n > 0; --n) {
      to += kDirectionOffset[dir];
      const Piece blocker = board_[to];
      if (blocker.isNone()) {
        out.add(Move(from, static_cast<Square>(to)));
        continue;
      }
      if (blocker.color() != us) out.add(Move(from, static_cast<Square>(to)));
      break;
    }
  }
}

// ------------------------------------------------------------------ legality

// Own pieces that stand alone between the own king and an enemy slider aiming at it.
SquareSet Position::pinnedPieces() const {
  SquareSet pinned;
  const Color us = sideToMove_;
  const Square king = kingSquare_[indexOf(us)];
  for (int dir = 0; dir < kDirectionCount; ++dir) {
    const PieceType lineSlider = isDiagonal(dir) ? PieceType::Bishop : PieceType::Rook;
    Square candidate = kNoSquare;
    int s = king;
    for (int n = kStepsToEdge.steps[king][dir]; n > 0; --n) {
      s += kDirectionOffset[dir];
      const Piece piece = board_[s];
      if (piece.isNone()) continue;
      if (piece.color() == us) {
        if (candidate != kNoSquare) break;  // two own pieces on the ray: neither is pinned
        candidate = static_cast<Square>(s);
        continue;
      }
      if (candidate != kNoSquare && (piece.type() == lineSlider || piece.type() == PieceType::Queen)) {
        pinned.add(candidate);
      }
      break;
    }
  }
  return pinned;
}

// Plays only the piece movement of `m` on the board (no clocks, no hash), asks whether the own
// king is attacked, and puts everything back. Promotions are tested as plain pawn moves: what
// the pawn becomes cannot matter for the safety of its own king.
bool Position::leavesKingSafe(Move m) {
  const Color us = sideToMove_;
  const Square from = m.from();
  const Square to = m.to();
  const Piece moving = board_[from];
  const Square victimSquare = m.isEnPassant() ? makeSquare(fileOf(to), rankOf(from)) : to;
  const Piece victim = board_[victimSquare];

  board_[victimSquare] = Piece();
  board_[from] = Piece();
  board_[to] = moving;

  const Square king = (moving.type() == PieceType::King) ? to : kingSquare_[indexOf(us)];
  const bool safe = !isAttacked(king, opposite(us));

  board_[to] = Piece();
  board_[from] = moving;
  board_[victimSquare] = victim;  // last: for a normal capture this is `to` again
  return safe;
}

// Assumes enPassantSquare_ is set and consistent with the board.
bool Position::hasLegalEnPassantCapture() {
  const Color us = sideToMove_;
  const Piece ownPawn(us, PieceType::Pawn);
  const int fromRank = (us == Color::White) ? 4 : 3;
  const int epFile = fileOf(enPassantSquare_);
  for (const int side : kPawnCaptureSides) {
    const int file = epFile + side;
    if (file < 0 || file > 7) continue;
    const Square from = makeSquare(file, fromRank);
    if (board_[from] == ownPawn && leavesKingSafe(Move(from, enPassantSquare_, Move::Kind::EnPassant))) return true;
  }
  return false;
}

void Position::keepLegalOnly(MoveList& moves, LegalityContext& context) const {
  const Square king = kingSquare_[indexOf(sideToMove_)];
  int kept = 0;
  for (int i = 0; i < moves.size(); ++i) {
    const Move m = moves[i];
    bool legal;
    if (m.isCastling()) {
      legal = true;  // generateCastling() has already checked everything
    } else if (context.inCheck || m.isEnPassant() || m.from() == king || context.pinned.contains(m.from())) {
      legal = context.scratch.leavesKingSafe(m);
    } else {
      legal = true;
    }
    if (legal) moves.set(kept++, m);
  }
  moves.truncate(kept);
}

// ------------------------------------------------------------------ public generation API

int Position::generateLegalMoves(MoveList& out) const {
  out.clear();
  for (Square s = 0; s < 64; ++s) generatePseudoLegalFrom(s, out);
  LegalityContext context(*this);
  keepLegalOnly(out, context);
  return out.size();
}

int Position::generateLegalMovesFrom(Square from, MoveList& out) const {
  out.clear();
  if (from >= 64) return 0;
  generatePseudoLegalFrom(from, out);
  if (out.empty()) return 0;
  LegalityContext context(*this);
  keepLegalOnly(out, context);
  return out.size();
}

SquareSet Position::legalTargetsFrom(Square from) const {
  MoveList moves;
  generateLegalMovesFrom(from, moves);
  SquareSet targets;
  for (const Move m : moves) targets.add(m.to());
  return targets;
}

bool Position::hasLegalMoves() const {
  // Piece by piece, to stop at the first legal move instead of generating them all.
  LegalityContext context(*this);
  MoveList moves;
  for (Square s = 0; s < 64; ++s) {
    moves.clear();
    generatePseudoLegalFrom(s, moves);
    keepLegalOnly(moves, context);
    if (!moves.empty()) return true;
  }
  return false;
}

bool Position::isLegal(Move m) const {
  if (m.isNone()) return false;
  MoveList moves;
  generateLegalMovesFrom(m.from(), moves);
  return moves.contains(m);
}

Move Position::findLegalMove(Square from, Square to, PieceType promotion) const {
  MoveList moves;
  generateLegalMovesFrom(from, moves);
  for (const Move m : moves)
    if (m.to() == to && m.promotion() == promotion) return m;
  return Move::none();
}

bool Position::isPromotionMove(Square from, Square to) const {
  return !findLegalMove(from, to, PieceType::Queen).isNone();
}

bool Position::isCapture(Move m) const {
  if (m.isNone()) return false;
  if (m.isEnPassant()) return true;
  return !m.isCastling() && !pieceAt(m.to()).isNone();
}

// ------------------------------------------------------------------ perft

uint64_t perft(Position& position, int depth) {
  if (depth <= 0) return 1;
  MoveList moves;
  position.generateLegalMoves(moves);
  if (depth == 1) return static_cast<uint64_t>(moves.size());  // moves are legal: no need to play them
  uint64_t nodes = 0;
  for (const Move m : moves) {
    Undo undo;
    position.make(m, undo);
    nodes += perft(position, depth - 1);
    position.unmake(undo);
  }
  return nodes;
}

}  // namespace arrocco::chess
