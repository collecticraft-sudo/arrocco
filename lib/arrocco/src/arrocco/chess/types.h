// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — basic value types: colours, pieces, squares, square sets.
// Everything here is a plain value that fits in a register; nothing allocates.
#pragma once
#include <cstdint>

namespace arrocco::chess {

// ---------------------------------------------------------------- colours

enum class Color : uint8_t { White = 0, Black = 1 };

constexpr Color opposite(Color c) { return c == Color::White ? Color::Black : Color::White; }
constexpr int indexOf(Color c) { return static_cast<int>(c); }

// ---------------------------------------------------------------- pieces

enum class PieceType : uint8_t { None = 0, Pawn = 1, Knight = 2, Bishop = 3, Rook = 4, Queen = 5, King = 6 };

constexpr int indexOf(PieceType t) { return static_cast<int>(t); }

// Conventional material value in pawns (king = 0). For the "+3" next to the captured pieces.
constexpr int materialValue(PieceType t) {
  switch (t) {
    case PieceType::Pawn:   return 1;
    case PieceType::Knight: return 3;
    case PieceType::Bishop: return 3;
    case PieceType::Rook:   return 5;
    case PieceType::Queen:  return 9;
    default:                return 0;
  }
}

// English piece letter used by SAN and (upper case) by FEN: 'P','N','B','R','Q','K'; '?' for None.
constexpr char letterOf(PieceType t) {
  switch (t) {
    case PieceType::Pawn:   return 'P';
    case PieceType::Knight: return 'N';
    case PieceType::Bishop: return 'B';
    case PieceType::Rook:   return 'R';
    case PieceType::Queen:  return 'Q';
    case PieceType::King:   return 'K';
    default:                return '?';
  }
}

// Inverse of letterOf, case-insensitive. Returns None for anything else.
constexpr PieceType pieceTypeFromLetter(char ch) {
  switch (ch) {
    case 'P': case 'p': return PieceType::Pawn;
    case 'N': case 'n': return PieceType::Knight;
    case 'B': case 'b': return PieceType::Bishop;
    case 'R': case 'r': return PieceType::Rook;
    case 'Q': case 'q': return PieceType::Queen;
    case 'K': case 'k': return PieceType::King;
    default:            return PieceType::None;
  }
}

// A coloured piece, or "no piece". One byte: type in bits 0-2, colour in bit 3, 0 = empty square.
class Piece {
 public:
  constexpr Piece() : code_(0) {}
  constexpr Piece(Color c, PieceType t)
      : code_(t == PieceType::None ? uint8_t{0}
                                   : static_cast<uint8_t>(static_cast<uint8_t>(t) | (static_cast<uint8_t>(c) << 3))) {}

  constexpr bool isNone() const { return code_ == 0; }
  constexpr explicit operator bool() const { return code_ != 0; }
  constexpr PieceType type() const { return static_cast<PieceType>(code_ & 7); }
  constexpr Color color() const { return static_cast<Color>(code_ >> 3); }  // meaningless when isNone()
  constexpr bool is(Color c, PieceType t) const { return *this == Piece(c, t); }
  constexpr uint8_t code() const { return code_; }

  // FEN letter: upper case for White, lower case for Black, ' ' for no piece.
  constexpr char fenChar() const {
    if (isNone()) return ' ';
    const char upper = letterOf(type());
    return color() == Color::White ? upper : static_cast<char>(upper - 'A' + 'a');
  }
  static constexpr Piece fromFenChar(char ch) {
    const PieceType t = pieceTypeFromLetter(ch);
    if (t == PieceType::None) return Piece();
    return Piece((ch >= 'a' && ch <= 'z') ? Color::Black : Color::White, t);
  }

  constexpr bool operator==(Piece other) const { return code_ == other.code_; }
  constexpr bool operator!=(Piece other) const { return code_ != other.code_; }

 private:
  uint8_t code_;
};

// ---------------------------------------------------------------- squares

// 0 = a1, 1 = b1, ... 7 = h1, 8 = a2, ... 63 = h8. The UI decides which way up to draw them.
using Square = uint8_t;
constexpr Square kNoSquare = 64;

constexpr bool isValidSquare(Square s) { return s < 64; }
constexpr int fileOf(Square s) { return s & 7; }   // 0 = file a ... 7 = file h
constexpr int rankOf(Square s) { return s >> 3; }  // 0 = rank 1 ... 7 = rank 8
constexpr Square makeSquare(int file, int rank) { return static_cast<Square>(rank * 8 + file); }
constexpr bool isOnBoard(int file, int rank) {
  return static_cast<unsigned>(file) < 8u && static_cast<unsigned>(rank) < 8u;
}
constexpr bool isLightSquare(Square s) { return ((fileOf(s) + rankOf(s)) & 1) != 0; }  // a1 is dark
constexpr char fileChar(Square s) { return static_cast<char>('a' + fileOf(s)); }
constexpr char rankChar(Square s) { return static_cast<char>('1' + rankOf(s)); }

// "e4" -> square. Reads exactly two characters; returns kNoSquare if they are not a square.
constexpr Square squareFromName(const char* name) {
  if (name == nullptr) return kNoSquare;
  const char f = name[0];
  if (f < 'a' || f > 'h') return kNoSquare;
  const char r = name[1];
  if (r < '1' || r > '8') return kNoSquare;
  return makeSquare(f - 'a', r - '1');
}

// Named squares, so call sites read like chess: chess::E1, chess::H8.
// clang-format off
enum : Square {
  A1, B1, C1, D1, E1, F1, G1, H1,
  A2, B2, C2, D2, E2, F2, G2, H2,
  A3, B3, C3, D3, E3, F3, G3, H3,
  A4, B4, C4, D4, E4, F4, G4, H4,
  A5, B5, C5, D5, E5, F5, G5, H5,
  A6, B6, C6, D6, E6, F6, G6, H6,
  A7, B7, C7, D7, E7, F7, G7, H7,
  A8, B8, C8, D8, E8, F8, G8, H8
};
// clang-format on

// A set of squares as a 64-bit mask (bit n = square n). Used for "dots on the legal targets"
// and "which squares must be redrawn". Iterate with: for (Square s = 0; s < 64; ++s) if (set.contains(s)) ...
class SquareSet {
 public:
  constexpr SquareSet() : bits_(0) {}
  constexpr explicit SquareSet(uint64_t bits) : bits_(bits) {}

  constexpr bool contains(Square s) const { return s < 64 && ((bits_ >> s) & 1u) != 0; }
  constexpr bool empty() const { return bits_ == 0; }
  constexpr uint64_t bits() const { return bits_; }
  constexpr int count() const {
    int n = 0;
    for (uint64_t b = bits_; b != 0; b &= b - 1) ++n;
    return n;
  }
  constexpr void add(Square s) { if (s < 64) bits_ |= uint64_t{1} << s; }
  constexpr void remove(Square s) { if (s < 64) bits_ &= ~(uint64_t{1} << s); }
  constexpr void clear() { bits_ = 0; }

  constexpr bool operator==(SquareSet other) const { return bits_ == other.bits_; }
  constexpr bool operator!=(SquareSet other) const { return bits_ != other.bits_; }
  constexpr SquareSet operator|(SquareSet other) const { return SquareSet(bits_ | other.bits_); }

 private:
  uint64_t bits_;
};

// ---------------------------------------------------------------- castling rights

// Bit mask, same order as the FEN field "KQkq".
enum CastlingRight : uint8_t {
  kNoCastling     = 0,
  kWhiteKingside  = 1,
  kWhiteQueenside = 2,
  kBlackKingside  = 4,
  kBlackQueenside = 8,
  kAllCastling    = 15
};

}  // namespace arrocco::chess
