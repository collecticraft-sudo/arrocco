// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Move (16-bit value) and MoveList (fixed array of 256).
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

namespace arrocco::chess {

// Buffer sizes, terminating NUL included.
constexpr int kUciBufferSize = 6;  // "e7e8q"
constexpr int kSanBufferSize = 8;  // "Qh4xe1#"

// One move in 16 bits: from (6) | to (6) | promotion piece (2) | kind (2).
//
// The kind says what else happens besides "the piece on `from` goes to `to`":
//   Normal     nothing (a capture is recognised by the target square being occupied)
//   Promotion  the pawn is replaced by promotion()
//   EnPassant  the captured pawn is NOT on `to` but beside `from`
//   Castling   `from`/`to` are the KING's squares (e1g1, e1c1, e8g8, e8c8), the rook follows
//
// A Move does not know the position it belongs to. Build moves by asking a Position
// (generateLegalMoves, findLegalMove, parseUci, parseSan): those always set the kind correctly.
// The default-constructed Move is "no move" (a1a1 never occurs in a game).
class Move {
 public:
  enum class Kind : uint8_t { Normal = 0, Promotion = 1, EnPassant = 2, Castling = 3 };

  constexpr Move() : bits_(0) {}
  constexpr Move(Square from, Square to, Kind kind = Kind::Normal, PieceType promotion = PieceType::None)
      : bits_(static_cast<uint16_t>((from & 63u) | ((to & 63u) << 6) |
                                    (kind == Kind::Promotion ? (promotionBits(promotion) << 12) : 0u) |
                                    (static_cast<unsigned>(kind) << 14))) {}

  static constexpr Move none() { return Move(); }
  static constexpr Move fromRaw(uint16_t bits) { Move m; m.bits_ = bits; return m; }

  constexpr bool isNone() const { return bits_ == 0; }
  constexpr Square from() const { return static_cast<Square>(bits_ & 63u); }
  constexpr Square to() const { return static_cast<Square>((bits_ >> 6) & 63u); }
  constexpr Kind kind() const { return static_cast<Kind>(bits_ >> 14); }
  constexpr bool isPromotion() const { return kind() == Kind::Promotion; }
  constexpr bool isEnPassant() const { return kind() == Kind::EnPassant; }
  constexpr bool isCastling() const { return kind() == Kind::Castling; }
  // Knight, Bishop, Rook or Queen for a promotion; PieceType::None for every other move.
  constexpr PieceType promotion() const {
    return isPromotion() ? static_cast<PieceType>(((bits_ >> 12) & 3u) + static_cast<unsigned>(PieceType::Knight))
                         : PieceType::None;
  }
  constexpr uint16_t raw() const { return bits_; }

  constexpr bool operator==(Move other) const { return bits_ == other.bits_; }
  constexpr bool operator!=(Move other) const { return bits_ != other.bits_; }

  // Long algebraic / UCI text: "e2e4", "e7e8q", castling as the king move "e1g1"; "0000" for no move.
  // `out` must hold kUciBufferSize chars.
  void toUci(char* out) const;

 private:
  static constexpr unsigned promotionBits(PieceType t) {
    // Anything that is not N/B/R becomes a queen, so a careless caller still gets a legal promotion.
    return (t == PieceType::Knight || t == PieceType::Bishop || t == PieceType::Rook)
               ? static_cast<unsigned>(t) - static_cast<unsigned>(PieceType::Knight)
               : 3u;
  }
  uint16_t bits_;
};

// No legal chess position has more than 218 moves; 256 leaves room and keeps the index in a byte.
constexpr int kMaxMoves = 256;

// Caller-provided move buffer (about 0.5 KB: fine on the stack, even on the 8 KB Arduino loop task).
// Use it like an array: `for (int i = 0; i < list.size(); ++i) list[i]`, or `for (Move m : list)`.
class MoveList {
 public:
  int size() const { return size_; }
  bool empty() const { return size_ == 0; }
  void clear() { size_ = 0; }
  Move operator[](int i) const { return Move::fromRaw(raw_[i]); }
  // Silently drops moves beyond the capacity: only reachable from absurd hand-made FENs,
  // and dropping is better than writing past the array.
  void add(Move m) { if (size_ < kMaxMoves) raw_[size_++] = m.raw(); }
  void set(int i, Move m) { raw_[i] = m.raw(); }
  void truncate(int newSize) { if (newSize >= 0 && newSize < size_) size_ = newSize; }
  bool contains(Move m) const {
    for (int i = 0; i < size_; ++i) if (raw_[i] == m.raw()) return true;
    return false;
  }

  class Iterator {
   public:
    explicit Iterator(const uint16_t* p) : p_(p) {}
    Move operator*() const { return Move::fromRaw(*p_); }
    Iterator& operator++() { ++p_; return *this; }
    bool operator!=(Iterator other) const { return p_ != other.p_; }
   private:
    const uint16_t* p_;
  };
  Iterator begin() const { return Iterator(raw_); }
  Iterator end() const { return Iterator(raw_ + size_); }

 private:
  // Raw 16-bit values rather than Move objects: a MoveList is created for every generation
  // and must not pay for zero-filling 512 bytes it is about to overwrite.
  uint16_t raw_[kMaxMoves];
  int size_ = 0;
};

}  // namespace arrocco::chess
