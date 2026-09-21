// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Position: setup, FEN, hashing, make/unmake, attacks, draws by material.
// Move generation is in movegen.cpp, text notations in notation.cpp.
#include "arrocco/chess/position.h"

#include "arrocco/chess/geometry.h"
#include "arrocco/chess/zobrist.h"

namespace arrocco::chess {

namespace {

bool isSpace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }
bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

const char* skipSpaces(const char* p) {
  while (isSpace(*p)) ++p;
  return p;
}

// Reads a non-negative decimal number, clamped so that it cannot overflow the 16-bit clocks.
const char* readNumber(const char* p, int& value) {
  value = 0;
  while (isDigit(*p)) {
    if (value < 30000) value = value * 10 + (*p - '0');
    ++p;
  }
  if (value > 30000) value = 30000;
  return p;
}

// Appends to a bounded buffer; remembers overflow instead of writing past the end.
struct TextWriter {
  char* out;
  int capacity;
  int length = 0;
  bool overflow = false;

  void put(char ch) {
    if (length + 1 < capacity) out[length++] = ch;
    else overflow = true;
  }
  void putNumber(int value) {
    char digits[8];
    int n = 0;
    do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value > 0 && n < 8);
    while (n > 0) put(digits[--n]);
  }
};

}  // namespace

// ------------------------------------------------------------------ board primitives

void Position::clear() {
  for (Piece& p : board_) p = Piece();
  kingSquare_[0] = kingSquare_[1] = kNoSquare;
  sideToMove_ = Color::White;
  castlingRights_ = kNoCastling;
  enPassantSquare_ = kNoSquare;
  halfmoveClock_ = 0;
  fullmoveNumber_ = 1;
  hash_ = 0;
}

void Position::putPiece(Square s, Piece p) {
  board_[s] = p;
  hash_ ^= zobrist::pieceKey(p, s);
  if (p.type() == PieceType::King) kingSquare_[indexOf(p.color())] = s;
}

void Position::removePiece(Square s) {
  hash_ ^= zobrist::pieceKey(board_[s], s);
  board_[s] = Piece();
}

void Position::movePiece(Square from, Square to) {
  const Piece p = board_[from];
  removePiece(from);
  putPiece(to, p);
}

// ------------------------------------------------------------------ setup and FEN

void Position::setStartpos() {
  const bool ok = setFen(kStartposFen);
  (void)ok;  // the built-in FEN cannot fail
}

bool Position::setFen(const char* fen) {
  if (fen == nullptr) return false;
  Position next{Uninitialised{}};  // parse into a scratch so that a bad FEN leaves *this untouched
  next.clear();

  // 1. Piece placement, rank 8 first.
  const char* p = skipSpaces(fen);
  int file = 0, rank = 7;
  int kings[2] = {0, 0};
  int pieces[2] = {0, 0};
  for (;; ++p) {
    const char ch = *p;
    if (ch == '/') {
      if (file != 8 || rank == 0) return false;
      file = 0;
      --rank;
    } else if (ch >= '1' && ch <= '8') {
      file += ch - '0';
      if (file > 8) return false;
    } else if (Piece::fromFenChar(ch)) {
      if (file > 7) return false;
      const Piece piece = Piece::fromFenChar(ch);
      if (piece.type() == PieceType::Pawn && (rank == 0 || rank == 7)) return false;
      if (piece.type() == PieceType::King) ++kings[indexOf(piece.color())];
      ++pieces[indexOf(piece.color())];
      next.putPiece(makeSquare(file, rank), piece);
      ++file;
    } else {
      break;
    }
  }
  if (rank != 0 || file != 8) return false;
  if (*p != '\0' && !isSpace(*p)) return false;
  if (kings[0] != 1 || kings[1] != 1) return false;
  if (pieces[0] > 16 || pieces[1] > 16) return false;

  // 2. Side to move.
  p = skipSpaces(p);
  if (*p == 'w' || *p == 'b') {
    next.sideToMove_ = (*p == 'b') ? Color::Black : Color::White;
    ++p;
    if (*p != '\0' && !isSpace(*p)) return false;
  } else if (*p != '\0') {
    return false;
  }

  // 3. Castling rights. A right is kept only if king and rook really are on their home squares,
  // because the move generator trusts the rights.
  p = skipSpaces(p);
  uint8_t rights = kNoCastling;
  if (*p == '-') {
    ++p;
  } else {
    for (; *p != '\0' && !isSpace(*p); ++p) {
      switch (*p) {
        case 'K': rights |= kWhiteKingside; break;
        case 'Q': rights |= kWhiteQueenside; break;
        case 'k': rights |= kBlackKingside; break;
        case 'q': rights |= kBlackQueenside; break;
        default: return false;
      }
    }
  }
  if (*p != '\0' && !isSpace(*p)) return false;
  for (const CastlingSpec& spec : kCastlingSpecs) {
    if ((rights & spec.right) == 0) continue;
    if (!next.board_[spec.kingFrom].is(spec.color, PieceType::King) ||
        !next.board_[spec.rookFrom].is(spec.color, PieceType::Rook)) {
      rights = static_cast<uint8_t>(rights & ~spec.right);
    }
  }
  next.castlingRights_ = rights;

  // 4. En-passant square (validated and normalised below, once the side to move is known).
  p = skipSpaces(p);
  Square epSquare = kNoSquare;
  if (*p == '-') {
    ++p;
  } else if (*p != '\0') {
    epSquare = squareFromName(p);
    if (epSquare == kNoSquare) return false;
    p += 2;
  }
  if (*p != '\0' && !isSpace(*p)) return false;

  // 5. and 6. Clocks.
  p = skipSpaces(p);
  if (isDigit(*p)) {
    int value = 0;
    p = readNumber(p, value);
    next.halfmoveClock_ = static_cast<uint16_t>(value);
    p = skipSpaces(p);
    if (isDigit(*p)) {
      p = readNumber(p, value);
      next.fullmoveNumber_ = static_cast<uint16_t>(value < 1 ? 1 : value);
    }
  }

  // The side that has just moved cannot have left its king in check: such a position would let
  // the generator capture a king.
  const Color waiting = opposite(next.sideToMove_);
  if (next.isAttacked(next.kingSquare(waiting), next.sideToMove_)) return false;

  next.enPassantSquare_ = epSquare;
  next.normaliseEnPassantSquare();
  next.hash_ = next.computeHash();
  *this = next;
  return true;
}

// Keeps the en-passant square only if it is consistent with the board AND some capture onto it
// is legal. Anything else would either corrupt the board (capturing a pawn that is not there)
// or make two identical positions look different to the repetition rule.
void Position::normaliseEnPassantSquare() {
  const Square ep = enPassantSquare_;
  enPassantSquare_ = kNoSquare;
  if (ep == kNoSquare) return;
  const Color us = sideToMove_;
  const Color them = opposite(us);
  const int expectedRank = (us == Color::White) ? 5 : 2;
  if (rankOf(ep) != expectedRank) return;
  const Square pushedTo = static_cast<Square>(us == Color::White ? ep - 8 : ep + 8);
  const Square pushedFrom = static_cast<Square>(us == Color::White ? ep + 8 : ep - 8);
  if (!board_[pushedTo].is(them, PieceType::Pawn)) return;
  if (board_[ep] || board_[pushedFrom]) return;
  enPassantSquare_ = ep;
  if (!hasLegalEnPassantCapture()) enPassantSquare_ = kNoSquare;
}

int Position::toFen(char* out, int outSize) const {
  if (out == nullptr || outSize < 1) return 0;
  TextWriter w{out, outSize};

  for (int rank = 7; rank >= 0; --rank) {
    int emptyRun = 0;
    for (int file = 0; file < 8; ++file) {
      const Piece piece = board_[makeSquare(file, rank)];
      if (piece.isNone()) {
        ++emptyRun;
        continue;
      }
      if (emptyRun > 0) w.put(static_cast<char>('0' + emptyRun));
      emptyRun = 0;
      w.put(piece.fenChar());
    }
    if (emptyRun > 0) w.put(static_cast<char>('0' + emptyRun));
    if (rank > 0) w.put('/');
  }

  w.put(' ');
  w.put(sideToMove_ == Color::White ? 'w' : 'b');

  w.put(' ');
  if (castlingRights_ == kNoCastling) w.put('-');
  if (castlingRights_ & kWhiteKingside) w.put('K');
  if (castlingRights_ & kWhiteQueenside) w.put('Q');
  if (castlingRights_ & kBlackKingside) w.put('k');
  if (castlingRights_ & kBlackQueenside) w.put('q');

  w.put(' ');
  if (enPassantSquare_ == kNoSquare) {
    w.put('-');
  } else {
    w.put(fileChar(enPassantSquare_));
    w.put(rankChar(enPassantSquare_));
  }

  w.put(' ');
  w.putNumber(halfmoveClock_);
  w.put(' ');
  w.putNumber(fullmoveNumber_);

  out[w.length] = '\0';
  if (w.overflow) {
    out[0] = '\0';
    return 0;
  }
  return w.length;
}

// ------------------------------------------------------------------ hashing and comparison

uint64_t Position::computeHash() const {
  uint64_t h = 0;
  for (Square s = 0; s < 64; ++s)
    if (board_[s]) h ^= zobrist::pieceKey(board_[s], s);
  h ^= zobrist::kKeys.castling[castlingRights_];
  if (enPassantSquare_ != kNoSquare) h ^= zobrist::kKeys.enPassantFile[fileOf(enPassantSquare_)];
  if (sideToMove_ == Color::Black) h ^= zobrist::kKeys.blackToMove;
  return h;
}

bool Position::samePositionAs(const Position& other) const {
  if (sideToMove_ != other.sideToMove_ || castlingRights_ != other.castlingRights_ ||
      enPassantSquare_ != other.enPassantSquare_) {
    return false;
  }
  for (Square s = 0; s < 64; ++s)
    if (board_[s] != other.board_[s]) return false;
  return true;
}

bool Position::operator==(const Position& other) const {
  return samePositionAs(other) && halfmoveClock_ == other.halfmoveClock_ &&
         fullmoveNumber_ == other.fullmoveNumber_ && hash_ == other.hash_;
}

int Position::pieceCount(Color c, PieceType t) const {
  const Piece wanted(c, t);
  int n = 0;
  for (Square s = 0; s < 64; ++s)
    if (board_[s] == wanted && wanted) ++n;
  return n;
}

int Position::materialOf(Color c) const {
  int total = 0;
  for (Square s = 0; s < 64; ++s)
    if (board_[s] && board_[s].color() == c) total += materialValue(board_[s].type());
  return total;
}

SquareSet changedSquares(const Position& a, const Position& b) {
  SquareSet changed;
  for (Square s = 0; s < 64; ++s)
    if (a.pieceAt(s) != b.pieceAt(s)) changed.add(s);
  return changed;
}

// ------------------------------------------------------------------ make / unmake

void Position::make(Move m, Undo& undo) {
  undo.hash = hash_;
  undo.move = m;
  undo.halfmoveClock = halfmoveClock_;
  undo.castlingRights = castlingRights_;
  undo.enPassantSquare = enPassantSquare_;

  const Color us = sideToMove_;
  const Square from = m.from();
  const Square to = m.to();
  const PieceType movingType = board_[from].type();
  Piece captured = board_[to];

  // Take the old castling and en-passant state out of the hash; the new state goes back in below.
  hash_ ^= zobrist::kKeys.castling[castlingRights_];
  if (enPassantSquare_ != kNoSquare) hash_ ^= zobrist::kKeys.enPassantFile[fileOf(enPassantSquare_)];
  enPassantSquare_ = kNoSquare;

  switch (m.kind()) {
    case Move::Kind::Normal:
      if (captured) removePiece(to);
      movePiece(from, to);
      break;
    case Move::Kind::Promotion:
      if (captured) removePiece(to);
      removePiece(from);
      putPiece(to, Piece(us, m.promotion()));
      break;
    case Move::Kind::EnPassant: {
      const Square victim = makeSquare(fileOf(to), rankOf(from));
      captured = board_[victim];
      removePiece(victim);
      movePiece(from, to);
      break;
    }
    case Move::Kind::Castling: {
      const CastlingSpec& spec = castlingSpecForKingTarget(to);
      captured = Piece();
      movePiece(spec.kingFrom, spec.kingTo);
      movePiece(spec.rookFrom, spec.rookTo);
      break;
    }
  }
  undo.captured = captured;

  if (movingType == PieceType::Pawn || captured) halfmoveClock_ = 0;
  else if (halfmoveClock_ < UINT16_MAX) ++halfmoveClock_;
  if (us == Color::Black && fullmoveNumber_ < UINT16_MAX) ++fullmoveNumber_;

  // A king or rook leaving its home square, or a rook captured on it, ends the right for good.
  castlingRights_ = static_cast<uint8_t>(castlingRights_ & castlingRightsKeptAfterTouching(from) &
                                         castlingRightsKeptAfterTouching(to));
  hash_ ^= zobrist::kKeys.castling[castlingRights_];

  sideToMove_ = opposite(us);
  hash_ ^= zobrist::kKeys.blackToMove;

  // A double push creates an en-passant square only if the opponent can really capture there.
  // The cheap neighbour test comes first: the legality test is needed only a few times per game.
  if (movingType == PieceType::Pawn && (to - from == 16 || from - to == 16)) {
    const Piece enemyPawn(sideToMove_, PieceType::Pawn);
    const bool westNeighbour = fileOf(to) > 0 && board_[to - 1] == enemyPawn;
    const bool eastNeighbour = fileOf(to) < 7 && board_[to + 1] == enemyPawn;
    if (westNeighbour || eastNeighbour) {
      enPassantSquare_ = static_cast<Square>((from + to) / 2);
      if (!hasLegalEnPassantCapture()) enPassantSquare_ = kNoSquare;
    }
    if (enPassantSquare_ != kNoSquare) hash_ ^= zobrist::kKeys.enPassantFile[fileOf(enPassantSquare_)];
  }
}

void Position::unmake(const Undo& undo) {
  const Move m = undo.move;
  const Square from = m.from();
  const Square to = m.to();
  sideToMove_ = opposite(sideToMove_);
  const Color us = sideToMove_;

  // The piece helpers keep updating hash_, which is simply overwritten at the end: one code path
  // for moving pieces is worth more than a few XORs.
  switch (m.kind()) {
    case Move::Kind::Normal:
      movePiece(to, from);
      if (undo.captured) putPiece(to, undo.captured);
      break;
    case Move::Kind::Promotion:
      removePiece(to);
      putPiece(from, Piece(us, PieceType::Pawn));
      if (undo.captured) putPiece(to, undo.captured);
      break;
    case Move::Kind::EnPassant:
      movePiece(to, from);
      putPiece(makeSquare(fileOf(to), rankOf(from)), undo.captured);
      break;
    case Move::Kind::Castling: {
      const CastlingSpec& spec = castlingSpecForKingTarget(to);
      movePiece(spec.kingTo, spec.kingFrom);
      movePiece(spec.rookTo, spec.rookFrom);
      break;
    }
  }

  castlingRights_ = undo.castlingRights;
  enPassantSquare_ = undo.enPassantSquare;
  halfmoveClock_ = undo.halfmoveClock;
  if (us == Color::Black && fullmoveNumber_ > 1) --fullmoveNumber_;
  hash_ = undo.hash;
}

// ------------------------------------------------------------------ attacks

bool Position::isAttacked(Square s, Color by) const {
  if (s >= 64) return false;
  const int file = fileOf(s);
  const int rank = rankOf(s);

  // Pawns: a white pawn attacks upwards, so it attacks `s` from the rank below.
  const int pawnRank = (by == Color::White) ? rank - 1 : rank + 1;
  if (pawnRank >= 0 && pawnRank < 8) {
    const Piece pawn(by, PieceType::Pawn);
    if (file > 0 && board_[makeSquare(file - 1, pawnRank)] == pawn) return true;
    if (file < 7 && board_[makeSquare(file + 1, pawnRank)] == pawn) return true;
  }

  const Piece knight(by, PieceType::Knight);
  for (const Step& step : kKnightSteps) {
    const int f = file + step.file, r = rank + step.rank;
    if (isOnBoard(f, r) && board_[makeSquare(f, r)] == knight) return true;
  }

  const Piece king(by, PieceType::King);
  for (const Step& step : kKingSteps) {
    const int f = file + step.file, r = rank + step.rank;
    if (isOnBoard(f, r) && board_[makeSquare(f, r)] == king) return true;
  }

  // Sliders: walk each ray until the first piece; it attacks if it slides along that kind of ray.
  for (int dir = 0; dir < kDirectionCount; ++dir) {
    const PieceType lineSlider = isDiagonal(dir) ? PieceType::Bishop : PieceType::Rook;
    int t = s;
    for (int n = kStepsToEdge.steps[s][dir]; n > 0; --n) {
      t += kDirectionOffset[dir];
      const Piece piece = board_[t];
      if (piece.isNone()) continue;
      if (piece.color() == by && (piece.type() == lineSlider || piece.type() == PieceType::Queen)) return true;
      break;
    }
  }
  return false;
}

// ------------------------------------------------------------------ insufficient material

bool Position::isInsufficientMaterial() const {
  int knights = 0;
  int bishopsOnLight = 0, bishopsOnDark = 0;
  for (Square s = 0; s < 64; ++s) {
    switch (board_[s].type()) {
      case PieceType::Pawn:
      case PieceType::Rook:
      case PieceType::Queen:
        return false;
      case PieceType::Knight:
        ++knights;
        break;
      case PieceType::Bishop:
        if (isLightSquare(s)) ++bishopsOnLight; else ++bishopsOnDark;
        break;
      default:
        break;
    }
  }
  // Only kings and bishops: a mate needs bishops on both colours.
  if (knights == 0) return bishopsOnLight == 0 || bishopsOnDark == 0;
  // A lone knight cannot mate; knight + anything else can (at least with the opponent's help).
  return knights == 1 && bishopsOnLight + bishopsOnDark == 0;
}

}  // namespace arrocco::chess
