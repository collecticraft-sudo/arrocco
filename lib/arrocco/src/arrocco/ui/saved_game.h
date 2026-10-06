// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the saved game: what lets a game go on after a power cut (an empty
// battery, the switch, a pulled cable, or the reboot that ends a deep sleep).
//
// ChessApp writes it through Platform::storeBlob() after every refresh that changed it
// (a move, a take-back, a new game or the end of one, a flip, a clock that stops) and
// reads it back once, at boot; docs/decisioni.md ("Gioco") says what the board then
// offers. The game is kept as its start position and its moves rather than as a
// position: replaying them through the rules gives back everything that needs the
// history (Undo, the move list, threefold repetition) and proves on the way that the
// blob describes a real game.
//
// Format version 1, little-endian: 27 bytes, plus the start FEN, plus two bytes a ply.
//   offset size
//    0      4  magic "ARSG"
//    4      1  version, 1
//    5      1  mode: 0 two players, 1 against the engine
//    6      1  flags: bit 0 board flipped, bit 1 the engine has Black; no other bit set
//    7      1  engine level, 0 .. kEngineLevelCount-1
//    8      1  human side as chosen on the setup screen: 0 White, 1 Black, 2 drawn
//    9      1  clock preset (ClockPreset), 0 = no clock
//   10      1  result the players declared (GameResult), 0 = none
//   11      1  ... and how (GameEndReason: resignation, timeout or agreement), 0 = none
//   12      4  White's time left at the last move, ms (GameClock::bankedMs); 0, no clock
//   16      4  Black's time left at the last move, ms
//   20      2  plies in the line, 0 .. kMaxGamePlies
//   22      1  length N of the start FEN, 0 = the standard starting position
//   23      N  the start FEN, no terminator
//   23+N   2P  the moves, chess::Move::raw() each
//   end     4  CRC-32 of every byte before it (the one zlib computes)
// A blob that fails any check is not a saved game: the board then starts as if nothing
// had been stored. That covers a torn write, a flipped bit, and a format from another
// firmware version, older or newer: there is nothing to migrate yet, so none is read.
#pragma once
#include <cstddef>
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/ui/clock.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

constexpr char kSavedGameKey[] = "game";       // Platform::loadBlob / storeBlob
constexpr uint8_t kSavedGameVersion = 1;
constexpr size_t kSavedGameHeaderBytes = 23;   // everything before the FEN
constexpr size_t kSavedGameMaxFen = chess::kFenBufferSize - 1;
constexpr size_t kSavedGameMinBytes = kSavedGameHeaderBytes + 4;
constexpr size_t kSavedGameMaxBytes =
    kSavedGameHeaderBytes + kSavedGameMaxFen + 2 * static_cast<size_t>(chess::kMaxGamePlies) + 4;

// Everything about a game that chess::Game does not hold.
struct SavedGame {
  bool vsEngine = false;
  uint8_t engineLevel = 0;                    // index into kEngineLevels
  HumanSide humanSide = HumanSide::White;     // as chosen: Random stays Random
  chess::Color engineColor = chess::Color::Black;
  bool flipped = false;
  ClockPreset clockPreset = ClockPreset::Off;
  uint32_t clockMs[2] = {0, 0};               // [White], [Black]: GameClock::bankedMs()
};

// Why decodeSavedGame() turned a blob down, in the order the checks run.
enum class SaveCheck : uint8_t {
  Ok,
  TooShort,      // smaller than the smallest saved game
  NotASave,      // no magic: something else, or erased flash
  OtherVersion,  // written in another format version
  BadCrc,        // a torn write or a flipped bit
  BadLayout,     // the sizes inside do not add up to the size of the blob
  BadField,      // a value out of range
  BadStart,      // a start position the rules refuse
  IllegalMove,   // a move that is not legal where it stands
  BadResult      // a declared result the rules refuse
};

// Writes the whole line of `game` (wherever it is being browsed) and `saved` into `out`.
// Returns the size, or 0 if `capacity` is too small; kSavedGameMaxBytes always fits.
// The same game always gives the same bytes, so comparing blobs tells a change.
size_t encodeSavedGame(const chess::Game& game, const SavedGame& saved, uint8_t* out, size_t capacity);

// Checks `data` and replays its moves into `game`. On Ok, `game` is the saved game at its
// latest position, finished or not (the caller decides what to offer), and `saved` holds
// the rest. On anything else `game` is a new game and `saved` is left untouched.
SaveCheck decodeSavedGame(const uint8_t* data, size_t size, chess::Game& game, SavedGame& saved);

// CRC-32 as zlib computes it: reflected polynomial 0xEDB88320, all ones in and out.
uint32_t crc32(const uint8_t* data, size_t size);

}  // namespace arrocco::ui
