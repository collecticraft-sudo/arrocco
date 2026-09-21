// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — board geometry shared by the move generator and the attack test:
// ray directions, distance to the edge, knight/king steps, and the four castling moves as data.
// Internal to the library: the UI never needs this header.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

namespace arrocco::chess {

// ---------------------------------------------------------------- rays

// Directions 0-3 are rook lines, 4-7 bishop lines.
constexpr int kDirectionCount = 8;
constexpr int kFirstOrthogonal = 0, kFirstDiagonal = 4;
inline constexpr int8_t kDirectionOffset[kDirectionCount] = {+8, -8, +1, -1, +9, +7, -7, -9};  // N S E W NE NW SE SW
constexpr bool isDiagonal(int direction) { return direction >= kFirstDiagonal; }

// How many squares lie between a square and the edge of the board in each direction.
// 512 bytes, computed at compile time; replaces all the "did I wrap around the board" arithmetic.
struct StepsToEdge {
  uint8_t steps[64][kDirectionCount];
};

constexpr StepsToEdge makeStepsToEdge() {
  StepsToEdge table{};
  for (int s = 0; s < 64; ++s) {
    const int north = 7 - (s >> 3), south = s >> 3, east = 7 - (s & 7), west = s & 7;
    const int perDirection[kDirectionCount] = {
        north, south, east, west,
        north < east ? north : east, north < west ? north : west,
        south < east ? south : east, south < west ? south : west};
    for (int d = 0; d < kDirectionCount; ++d) table.steps[s][d] = static_cast<uint8_t>(perDirection[d]);
  }
  return table;
}

inline constexpr StepsToEdge kStepsToEdge = makeStepsToEdge();

// ---------------------------------------------------------------- single steps

struct Step {
  int8_t file, rank;
};
inline constexpr Step kKnightSteps[8] = {{+1, +2}, {+2, +1}, {+2, -1}, {+1, -2}, {-1, -2}, {-2, -1}, {-2, +1}, {-1, +2}};
inline constexpr Step kKingSteps[8] = {{0, +1}, {+1, +1}, {+1, 0}, {+1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, +1}};

// ---------------------------------------------------------------- castling

// Everything about one castling move. The generator derives the rest from it: the squares
// between king and rook must be empty, the squares the king crosses (both ends included)
// must not be attacked.
struct CastlingSpec {
  CastlingRight right;
  Color color;
  Square kingFrom, kingTo, rookFrom, rookTo;
};

inline constexpr CastlingSpec kCastlingSpecs[4] = {
    {kWhiteKingside,  Color::White, E1, G1, H1, F1},
    {kWhiteQueenside, Color::White, E1, C1, A1, D1},
    {kBlackKingside,  Color::Black, E8, G8, H8, F8},
    {kBlackQueenside, Color::Black, E8, C8, A8, D8},
};

// A castling Move stores the king's target square; this finds the matching spec.
constexpr const CastlingSpec& castlingSpecForKingTarget(Square kingTo) {
  return kingTo == G1 ? kCastlingSpecs[0] : kingTo == C1 ? kCastlingSpecs[1]
       : kingTo == G8 ? kCastlingSpecs[2] : kCastlingSpecs[3];
}

// Rights that survive when a move starts from or lands on `s` (a king or rook home square
// being left, or a rook being captured at home). All four for any other square.
constexpr uint8_t castlingRightsKeptAfterTouching(Square s) {
  switch (s) {
    case E1: return static_cast<uint8_t>(kAllCastling & ~(kWhiteKingside | kWhiteQueenside));
    case H1: return static_cast<uint8_t>(kAllCastling & ~kWhiteKingside);
    case A1: return static_cast<uint8_t>(kAllCastling & ~kWhiteQueenside);
    case E8: return static_cast<uint8_t>(kAllCastling & ~(kBlackKingside | kBlackQueenside));
    case H8: return static_cast<uint8_t>(kAllCastling & ~kBlackKingside);
    case A8: return static_cast<uint8_t>(kAllCastling & ~kBlackQueenside);
    default: return kAllCastling;
  }
}

}  // namespace arrocco::chess
