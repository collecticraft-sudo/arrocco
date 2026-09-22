// SPDX-License-Identifier: GPL-3.0-or-later
// GENERATED FILE - do not edit.  Rebuild with:
//   python3 tools/make_puzzle_pack.py <lichess_db_puzzle.csv.zst>
// Puzzles from the Lichess puzzle database, CC0 1.0 Universal.
#pragma once
#include <cstdint>

namespace arrocco::puzzles::data {

// Bumped whenever the byte layout below changes; puzzles.cpp static_asserts on it.
constexpr uint16_t kFormatVersion = 1;

constexpr int kCount = 3500;
constexpr int kIndexStride = 12;
constexpr uint32_t kIndexBytes = 42000;
constexpr uint32_t kBlobBytes = 96741;
constexpr uint32_t kTotalBytes = kIndexBytes + kBlobBytes;

// One 12-byte record per puzzle, sorted by rating then by Lichess id:
//   [0..4]  Lichess puzzle id, 5 ASCII chars, no terminator
//   [5]     theme id in bits 0-5, "ends in mate" in bit 6
//   [6..7]  rating, little-endian uint16
//   [8]     number of stored moves (the blunder plus the solution)
//   [9..11] offset into kBlob, little-endian uint24
extern const uint8_t kIndex[kIndexBytes];

// Variable-length records:
//   [0..7]           occupancy bitmap, little-endian uint64, bit n = square n (0 = a1)
//   [8..]            one nibble per occupied square in ascending square order, low nibble first
//                    0-5 = white P N B R Q K, 6-11 = black P N B R Q K,
//                    12/13 = a white/black pawn that has just double-pushed (gives the e.p. square)
//   next byte        side to move in bit 0 (1 = black), castling rights in bits 1-4 (K Q k q)
//   then 2 bytes/move  little-endian uint16: from | to<<6 | promotion<<12
//                    promotion 0 = none, 1 = N, 2 = B, 3 = R, 4 = Q
extern const uint8_t kBlob[kBlobBytes];

}  // namespace arrocco::puzzles::data
