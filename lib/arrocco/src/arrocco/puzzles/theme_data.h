// SPDX-License-Identifier: GPL-3.0-or-later
// GENERATED FILE - do not edit.  Rebuild with:
//   python3 tools/make_puzzle_pack.py <lichess_db_puzzle.csv.zst>
// Puzzles from the Lichess puzzle database, CC0 1.0 Universal.
#pragma once
#include <cstdint>

namespace arrocco::puzzles {

// The one label a puzzle carries.  Generated from the theme table in
// tools/make_puzzle_pack.py; the numeric values are what puzzle_data.cpp stores.
enum class ThemeId : uint8_t {
  SmotheredMate,
  BackRankMate,
  MateIn1,
  MateIn2,
  MateIn3,
  DoubleCheck,
  Fork,
  Pin,
  Skewer,
  DiscoveredAttack,
  Deflection,
  Attraction,
  Clearance,
  Interference,
  TrappedPiece,
  HangingPiece,
  Sacrifice,
  Promotion,
  AdvancedPawn,
  Zugzwang,
  DefensiveMove,
  QuietMove,
  Count
};

// English label, or "" for a value outside the enum.  Never null.
const char* themeName(ThemeId theme);

// The Lichess theme name the label came from (for looking a puzzle up).
const char* themeKey(ThemeId theme);

}  // namespace arrocco::puzzles
