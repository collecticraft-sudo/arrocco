// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Zobrist keys for position hashing (threefold repetition).
//
// The table is generated AT COMPILE TIME from a fixed seed (SplitMix64), so it is identical
// on every build and every platform, needs no random source at runtime and sits in flash
// (.rodata, 793 keys = 6.2 KB). Hashes are therefore stable enough to be written in tests,
// but they are NOT Polyglot-compatible.
#pragma once
#include <cstdint>

#include "arrocco/chess/types.h"

namespace arrocco::chess::zobrist {

struct Keys {
  uint64_t piece[2][6][64];   // [colour][type - Pawn][square]
  uint64_t castling[16];      // indexed by the CastlingRight mask
  uint64_t enPassantFile[8];  // only when an en-passant capture is really legal
  uint64_t blackToMove;
};

namespace detail {

constexpr uint64_t splitMix64(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

constexpr Keys makeKeys() {
  Keys k{};
  uint64_t state = 0x4152524F43434FULL;  // "ARROCCO" in ASCII: any fixed seed will do
  for (int c = 0; c < 2; ++c)
    for (int t = 0; t < 6; ++t)
      for (int s = 0; s < 64; ++s) k.piece[c][t][s] = splitMix64(state);

  // One key per single right; a mask's key is the XOR of its rights, so that losing one
  // right changes the hash in the same way whatever the other rights are.
  uint64_t right[4] = {};
  for (int i = 0; i < 4; ++i) right[i] = splitMix64(state);
  for (int mask = 0; mask < 16; ++mask) {
    uint64_t key = 0;
    for (int i = 0; i < 4; ++i)
      if (mask & (1 << i)) key ^= right[i];
    k.castling[mask] = key;
  }

  for (int f = 0; f < 8; ++f) k.enPassantFile[f] = splitMix64(state);
  k.blackToMove = splitMix64(state);
  return k;
}

}  // namespace detail

inline constexpr Keys kKeys = detail::makeKeys();

inline uint64_t pieceKey(Piece p, Square s) {
  return kKeys.piece[indexOf(p.color())][indexOf(p.type()) - 1][s];
}

}  // namespace arrocco::chess::zobrist
