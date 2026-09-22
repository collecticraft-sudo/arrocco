// SPDX-License-Identifier: GPL-3.0-or-later
// GENERATED FILE - do not edit.  Rebuild with:
//   python3 tools/make_puzzle_pack.py <lichess_db_puzzle.csv.zst>
// Puzzles from the Lichess puzzle database, CC0 1.0 Universal.
#include "arrocco/puzzles/theme_data.h"

namespace arrocco::puzzles {
namespace {

const char* const kNames[] = {
    "Smothered mate",
    "Back-rank mate",
    "Mate in 1",
    "Mate in 2",
    "Mate in 3",
    "Double check",
    "Fork",
    "Pin",
    "Skewer",
    "Discovered attack",
    "Deflection",
    "Attraction",
    "Clearance",
    "Interference",
    "Trapped piece",
    "Hanging piece",
    "Sacrifice",
    "Promotion",
    "Advanced pawn",
    "Zugzwang",
    "Defensive move",
    "Quiet move",
};

const char* const kKeys[] = {
    "smotheredMate",
    "backRankMate",
    "mateIn1",
    "mateIn2",
    "mateIn3",
    "doubleCheck",
    "fork",
    "pin",
    "skewer",
    "discoveredAttack",
    "deflection",
    "attraction",
    "clearance",
    "interference",
    "trappedPiece",
    "hangingPiece",
    "sacrifice",
    "promotion",
    "advancedPawn",
    "zugzwang",
    "defensiveMove",
    "quietMove",
};

}  // namespace

const char* themeName(ThemeId theme) {
  const unsigned i = static_cast<unsigned>(theme);
  return i < static_cast<unsigned>(ThemeId::Count) ? kNames[i] : "";
}

const char* themeKey(ThemeId theme) {
  const unsigned i = static_cast<unsigned>(theme);
  return i < static_cast<unsigned>(ThemeId::Count) ? kKeys[i] : "";
}

}  // namespace arrocco::puzzles
