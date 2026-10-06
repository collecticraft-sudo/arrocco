// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — saved game format. See saved_game.h.
#include "arrocco/ui/saved_game.h"

#include <cstring>

#include "arrocco/engine.h"

namespace arrocco::ui {

namespace {

using chess::GameEndReason;
using chess::GameResult;

constexpr uint8_t kMagic[4] = {'A', 'R', 'S', 'G'};

// Byte offsets of the fixed header, as in the table of saved_game.h.
constexpr size_t kOffVersion = 4;
constexpr size_t kOffMode = 5;
constexpr size_t kOffFlags = 6;
constexpr size_t kOffLevel = 7;
constexpr size_t kOffHumanSide = 8;
constexpr size_t kOffClockPreset = 9;
constexpr size_t kOffResult = 10;
constexpr size_t kOffReason = 11;
constexpr size_t kOffWhiteMs = 12;
constexpr size_t kOffBlackMs = 16;
constexpr size_t kOffPlies = 20;
constexpr size_t kOffFenLength = 22;
static_assert(kOffFenLength + 1 == kSavedGameHeaderBytes, "the FEN follows the header");

constexpr uint8_t kModeTwoPlayers = 0;
constexpr uint8_t kModeEngine = 1;
constexpr uint8_t kFlagFlipped = 1u << 0;
constexpr uint8_t kFlagEngineBlack = 1u << 1;
constexpr uint8_t kKnownFlags = kFlagFlipped | kFlagEngineBlack;

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

// Only what the players can declare is stored: checkmate, stalemate and the draws the
// rules find come back by themselves when the moves are replayed.
bool isDeclaredReason(GameEndReason reason) {
  return reason == GameEndReason::Resignation || reason == GameEndReason::Timeout ||
         reason == GameEndReason::Agreement;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

size_t encodeSavedGame(const chess::Game& game, const SavedGame& saved, uint8_t* out, size_t capacity) {
  char fen[chess::kFenBufferSize] = {};
  size_t fenLength = 0;
  if (!game.startedFromStartpos()) {
    const int written = game.startPosition().toFen(fen, sizeof fen);
    if (written <= 0) return 0;
    fenLength = static_cast<size_t>(written);
  }
  const int plies = game.plyCount();
  const size_t size = kSavedGameHeaderBytes + fenLength + 2 * static_cast<size_t>(plies) + 4;
  if (out == nullptr || size > capacity) return 0;

  // A result is "declared" when the game reports one of the reasons only players give.
  const bool declared = isDeclaredReason(game.reason());
  uint8_t flags = 0;
  if (saved.flipped) flags |= kFlagFlipped;
  if (saved.engineColor == chess::Color::Black) flags |= kFlagEngineBlack;

  memcpy(out, kMagic, sizeof kMagic);
  out[kOffVersion] = kSavedGameVersion;
  out[kOffMode] = saved.vsEngine ? kModeEngine : kModeTwoPlayers;
  out[kOffFlags] = flags;
  out[kOffLevel] = saved.engineLevel;
  out[kOffHumanSide] = static_cast<uint8_t>(saved.humanSide);
  out[kOffClockPreset] = static_cast<uint8_t>(saved.clockPreset);
  out[kOffResult] = static_cast<uint8_t>(declared ? game.result() : GameResult::Ongoing);
  out[kOffReason] = static_cast<uint8_t>(declared ? game.reason() : GameEndReason::None);
  put32(out + kOffWhiteMs, saved.clockMs[0]);
  put32(out + kOffBlackMs, saved.clockMs[1]);
  put16(out + kOffPlies, static_cast<uint16_t>(plies));
  out[kOffFenLength] = static_cast<uint8_t>(fenLength);
  uint8_t* p = out + kSavedGameHeaderBytes;
  memcpy(p, fen, fenLength);
  p += fenLength;
  for (int i = 0; i < plies; ++i, p += 2) put16(p, game.moveAt(i).raw());
  put32(p, crc32(out, size - 4));
  return size;
}

SaveCheck decodeSavedGame(const uint8_t* data, size_t size, chess::Game& game, SavedGame& saved) {
  game.newGame();
  if (data == nullptr || size < kSavedGameMinBytes) return SaveCheck::TooShort;
  if (memcmp(data, kMagic, sizeof kMagic) != 0) return SaveCheck::NotASave;
  if (data[kOffVersion] != kSavedGameVersion) return SaveCheck::OtherVersion;
  // The CRC sits in the last four bytes whatever the header says, so it is checked
  // before any size written inside is believed.
  if (get32(data + size - 4) != crc32(data, size - 4)) return SaveCheck::BadCrc;

  const size_t plies = get16(data + kOffPlies);
  const size_t fenLength = data[kOffFenLength];
  if (plies > static_cast<size_t>(chess::kMaxGamePlies) || fenLength > kSavedGameMaxFen ||
      kSavedGameHeaderBytes + fenLength + 2 * plies + 4 != size)
    return SaveCheck::BadLayout;

  const uint8_t mode = data[kOffMode];
  const uint8_t flags = data[kOffFlags];
  const uint8_t humanSide = data[kOffHumanSide];
  const uint8_t preset = data[kOffClockPreset];
  const uint8_t result = data[kOffResult];
  const uint8_t reason = data[kOffReason];
  const uint32_t whiteMs = get32(data + kOffWhiteMs);
  const uint32_t blackMs = get32(data + kOffBlackMs);
  if (mode > kModeEngine || (flags & ~kKnownFlags) != 0 || data[kOffLevel] >= kEngineLevelCount ||
      humanSide > static_cast<uint8_t>(HumanSide::Random) ||
      preset >= static_cast<uint8_t>(ClockPreset::Count) ||
      result > static_cast<uint8_t>(GameResult::Draw) ||
      reason > static_cast<uint8_t>(GameEndReason::Agreement))
    return SaveCheck::BadField;
  const bool declared = isDeclaredReason(static_cast<GameEndReason>(reason));
  // A reason the rules find never comes from the blob, and a result needs its reason.
  if (reason != 0 && !declared) return SaveCheck::BadField;
  if (declared != (result != static_cast<uint8_t>(GameResult::Ongoing))) return SaveCheck::BadField;
  // No clock, no times: encodeSavedGame() writes zeros (bankedMs() of a clock that is off).
  if (clockSpec(static_cast<ClockPreset>(preset)).baseMs == 0 && (whiteMs != 0 || blackMs != 0))
    return SaveCheck::BadField;

  const uint8_t* p = data + kSavedGameHeaderBytes;
  if (fenLength > 0) {
    char fen[chess::kFenBufferSize];
    memcpy(fen, p, fenLength);
    fen[fenLength] = '\0';
    if (!game.setFromFen(fen)) return SaveCheck::BadStart;
    p += fenLength;
  }
  for (size_t i = 0; i < plies; ++i, p += 2) {
    // play() takes only an exact legal move of the position: kind and promotion included.
    if (!game.play(chess::Move::fromRaw(get16(p)))) {
      game.newGame();
      return SaveCheck::IllegalMove;
    }
  }
  if (declared && !game.declareResult(static_cast<GameResult>(result), static_cast<GameEndReason>(reason))) {
    game.newGame();
    return SaveCheck::BadResult;
  }

  saved.vsEngine = mode == kModeEngine;
  saved.engineLevel = data[kOffLevel];
  saved.humanSide = static_cast<HumanSide>(humanSide);
  saved.engineColor = (flags & kFlagEngineBlack) ? chess::Color::Black : chess::Color::White;
  saved.flipped = (flags & kFlagFlipped) != 0;
  saved.clockPreset = static_cast<ClockPreset>(preset);
  saved.clockMs[0] = whiteMs;
  saved.clockMs[1] = blackMs;
  return SaveCheck::Ok;
}

}  // namespace arrocco::ui
