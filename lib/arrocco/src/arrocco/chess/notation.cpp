// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — text notations for moves: UCI (engine, Lichess) and SAN (move list, PGN).
// Parsing never trusts the text: whatever comes out is one of the legal moves of the position.
#include "arrocco/chess/geometry.h"
#include "arrocco/chess/position.h"

namespace arrocco::chess {

namespace {

bool isSpace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }
bool endsToken(char ch) { return ch == '\0' || isSpace(ch); }
bool isFileChar(char ch) { return ch >= 'a' && ch <= 'h'; }
bool isRankChar(char ch) { return ch >= '1' && ch <= '8'; }
bool isPromotionPiece(PieceType t) {
  return t == PieceType::Knight || t == PieceType::Bishop || t == PieceType::Rook || t == PieceType::Queen;
}
char toLower(char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; }

const char* skipSpaces(const char* p) {
  while (isSpace(*p)) ++p;
  return p;
}

}  // namespace

// ------------------------------------------------------------------ UCI

void Move::toUci(char* out) const {
  if (out == nullptr) return;
  if (isNone()) {
    out[0] = out[1] = out[2] = out[3] = '0';
    out[4] = '\0';
    return;
  }
  int n = 0;
  out[n++] = fileChar(from());
  out[n++] = rankChar(from());
  out[n++] = fileChar(to());
  out[n++] = rankChar(to());
  if (isPromotion()) out[n++] = toLower(letterOf(promotion()));
  out[n] = '\0';
}

Move Position::parseUci(const char* text) const {
  if (text == nullptr) return Move::none();
  text = skipSpaces(text);
  // squareFromName() stops at the first bad character, so this never reads past the NUL.
  const Square from = squareFromName(text);
  if (from == kNoSquare) return Move::none();
  const Square to = squareFromName(text + 2);
  if (to == kNoSquare) return Move::none();

  PieceType promotion = PieceType::None;
  if (!endsToken(text[4])) {
    promotion = pieceTypeFromLetter(text[4]);
    if (!isPromotionPiece(promotion) || !endsToken(text[5])) return Move::none();
  }

  MoveList moves;
  generateLegalMovesFrom(from, moves);
  for (const Move m : moves) {
    if (m.promotion() != promotion) continue;
    if (m.to() == to) return m;
    // Chess960-style castling, "king takes own rook": Lichess documents it for every variant.
    if (m.isCastling() && to == castlingSpecForKingTarget(m.to()).rookFrom) return m;
  }
  return Move::none();
}

// ------------------------------------------------------------------ SAN

void Position::toSan(Move m, char* out) const {
  if (out == nullptr) return;
  int n = 0;

  {
    MoveList legal;
    generateLegalMoves(legal);
    if (!legal.contains(m)) {
      out[0] = out[1] = '?';
      out[2] = '\0';
      return;
    }

    const Square from = m.from();
    const Square to = m.to();
    if (m.isCastling()) {
      out[n++] = 'O'; out[n++] = '-'; out[n++] = 'O';
      if (fileOf(to) < fileOf(from)) { out[n++] = '-'; out[n++] = 'O'; }
    } else {
      const PieceType type = board_[from].type();
      const bool capture = isCapture(m);
      if (type == PieceType::Pawn) {
        if (capture) out[n++] = fileChar(from);
      } else {
        out[n++] = letterOf(type);
        // Disambiguate against the other LEGAL moves of the same kind of piece to the same square
        // (a pinned twin does not count): file if that settles it, else rank, else both.
        bool hasTwin = false, twinOnSameFile = false, twinOnSameRank = false;
        for (const Move other : legal) {
          if (other.to() != to || other.from() == from || board_[other.from()].type() != type) continue;
          hasTwin = true;
          if (fileOf(other.from()) == fileOf(from)) twinOnSameFile = true;
          if (rankOf(other.from()) == rankOf(from)) twinOnSameRank = true;
        }
        if (hasTwin) {
          if (!twinOnSameFile) {
            out[n++] = fileChar(from);
          } else if (!twinOnSameRank) {
            out[n++] = rankChar(from);
          } else {
            out[n++] = fileChar(from);
            out[n++] = rankChar(from);
          }
        }
      }
      if (capture) out[n++] = 'x';
      out[n++] = fileChar(to);
      out[n++] = rankChar(to);
      if (m.isPromotion()) {
        out[n++] = '=';
        out[n++] = letterOf(m.promotion());
      }
    }
  }

  // '+' or '#': look at the position after the move.
  Position after(*this);
  Undo undo;
  after.make(m, undo);
  if (after.inCheck()) out[n++] = after.hasLegalMoves() ? '+' : '#';
  out[n] = '\0';
}

Move Position::parseSan(const char* text) const {
  if (text == nullptr) return Move::none();
  text = skipSpaces(text);

  // Copy the token: the longest decorated SAN we accept is well under 15 characters.
  char token[16];
  int len = 0;
  while (!endsToken(text[len])) {
    if (len >= 15) return Move::none();
    token[len] = text[len];
    ++len;
  }
  // Check, mate and annotation marks carry no information we need.
  while (len > 0 && (token[len - 1] == '+' || token[len - 1] == '#' || token[len - 1] == '!' || token[len - 1] == '?')) {
    --len;
  }
  if (len < 2) return Move::none();

  MoveList legal;
  generateLegalMoves(legal);

  // Castling, with letter O or digit 0.
  if (token[0] == 'O' || token[0] == '0') {
    bool wellFormed = (len == 3 || len == 5);
    for (int i = 0; wellFormed && i < len; ++i) {
      const bool expectDash = (i % 2) == 1;
      wellFormed = expectDash ? token[i] == '-' : (token[i] == 'O' || token[i] == '0');
    }
    if (!wellFormed) return Move::none();
    const int kingTargetFile = (len == 3) ? 6 : 2;
    for (const Move m : legal)
      if (m.isCastling() && fileOf(m.to()) == kingTargetFile) return m;
    return Move::none();
  }

  // Promotion: "=Q", or a bare trailing piece letter ("e8Q") as some PGN writers do.
  PieceType promotion = PieceType::None;
  if (len >= 4 && token[len - 2] == '=') {
    promotion = pieceTypeFromLetter(token[len - 1]);
    if (!isPromotionPiece(promotion)) return Move::none();
    len -= 2;
  } else if (len >= 3 && isRankChar(token[len - 2]) && token[len - 1] >= 'A' && token[len - 1] <= 'Z') {
    promotion = pieceTypeFromLetter(token[len - 1]);
    if (!isPromotionPiece(promotion)) return Move::none();
    len -= 1;
  }

  // Destination square: always the last two characters of what is left.
  if (len < 2) return Move::none();
  const Square to = (isFileChar(token[len - 2]) && isRankChar(token[len - 1]))
                        ? makeSquare(token[len - 2] - 'a', token[len - 1] - '1')
                        : kNoSquare;
  if (to == kNoSquare) return Move::none();
  len -= 2;

  // What is left, in this order: piece letter, origin file, origin rank, capture mark. All optional.
  int i = 0;
  PieceType type = PieceType::Pawn;
  if (i < len && token[i] >= 'A' && token[i] <= 'Z') {
    type = pieceTypeFromLetter(token[i]);
    if (type == PieceType::None) return Move::none();
    ++i;
  }
  int fromFile = -1, fromRank = -1;
  if (i < len && isFileChar(token[i])) fromFile = token[i++] - 'a';
  if (i < len && isRankChar(token[i])) fromRank = token[i++] - '1';
  if (i < len && (token[i] == 'x' || token[i] == ':')) ++i;
  if (i != len) return Move::none();

  // Exactly one legal move must fit. Matching against legal moves is what resolves the cases
  // where SAN omits the origin because the other candidate is pinned.
  Move found = Move::none();
  for (const Move m : legal) {
    if (m.isCastling() || m.to() != to || m.promotion() != promotion) continue;
    if (board_[m.from()].type() != type) continue;
    if (fromFile >= 0 && fileOf(m.from()) != fromFile) continue;
    if (fromRank >= 0 && rankOf(m.from()) != fromRank) continue;
    if (!found.isNone()) return Move::none();  // ambiguous
    found = m;
  }
  return found;
}

}  // namespace arrocco::chess
