// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the saved game (lib/arrocco/src/arrocco/ui/saved_game.*): every kind of game
// comes back exactly as it was written, and a blob that is torn, flipped, from another
// version or simply made up is refused without ever leaving a half-built game behind.
// Plain checks with messages; every failure is printed, the exit code is non-zero if any failed.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "arrocco/chess/game.h"
#include "arrocco/ui/clock.h"
#include "arrocco/ui/saved_game.h"

using namespace arrocco::chess;
using arrocco::ui::ClockPreset;
using arrocco::ui::GameClock;
using arrocco::ui::HumanSide;
using arrocco::ui::SavedGame;
using arrocco::ui::SaveCheck;
using arrocco::ui::decodeSavedGame;
using arrocco::ui::encodeSavedGame;
using arrocco::ui::kSavedGameMaxBytes;
using arrocco::ui::kSavedGameMinBytes;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond, msg)                                                              \
  do {                                                                                \
    ++g_checks;                                                                       \
    if (!(cond)) {                                                                    \
      ++g_failures;                                                                   \
      std::printf("FAIL line %d: %s   [%s]\n", __LINE__, (msg), #cond);               \
    }                                                                                 \
  } while (0)

// Header offsets, as documented in saved_game.h: the tests write them by hand.
constexpr size_t kOffVersion = 4;
constexpr size_t kOffMode = 5;
constexpr size_t kOffFlags = 6;
constexpr size_t kOffLevel = 7;
constexpr size_t kOffHumanSide = 8;
constexpr size_t kOffClockPreset = 9;
constexpr size_t kOffResult = 10;
constexpr size_t kOffReason = 11;
constexpr size_t kOffWhiteMs = 12;
constexpr size_t kOffPlies = 20;
constexpr size_t kOffFenLength = 22;
constexpr size_t kOffFen = 23;

// Games are 25 KB: static, as the firmware keeps them.
Game g_original;
Game g_decoded;

struct Blob {
  uint8_t bytes[kSavedGameMaxBytes + 64];
  size_t size = 0;
};

Blob g_blob;
Blob g_other;

const char* checkName(SaveCheck c) {
  switch (c) {
    case SaveCheck::Ok:           return "Ok";
    case SaveCheck::TooShort:     return "TooShort";
    case SaveCheck::NotASave:     return "NotASave";
    case SaveCheck::OtherVersion: return "OtherVersion";
    case SaveCheck::BadCrc:       return "BadCrc";
    case SaveCheck::BadLayout:    return "BadLayout";
    case SaveCheck::BadField:     return "BadField";
    case SaveCheck::BadStart:     return "BadStart";
    case SaveCheck::IllegalMove:  return "IllegalMove";
    case SaveCheck::BadResult:    return "BadResult";
  }
  return "?";
}

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// Writes a correct CRC over a blob the test has edited: the checks behind the CRC must
// hold on their own, for the day a corrupt blob happens to carry a matching one.
void reseal(Blob& b) { put32(b.bytes + b.size - 4, arrocco::ui::crc32(b.bytes, b.size - 4)); }

bool playLine(Game& game, const char* line) {
  char move[8];
  for (const char* p = line; *p != '\0';) {
    while (*p == ' ') ++p;
    int n = 0;
    while (*p != '\0' && *p != ' ' && n < 7) move[n++] = *p++;
    move[n] = '\0';
    if (n == 0) break;
    if (!game.playUci(move)) {
      std::printf("     could not play %s\n", move);
      return false;
    }
  }
  return true;
}

bool sameGame(const Game& a, const Game& b) {
  if (a.plyCount() != b.plyCount() || a.startedFromStartpos() != b.startedFromStartpos()) return false;
  if (!(a.startPosition() == b.startPosition())) return false;
  for (int i = 0; i < a.plyCount(); ++i) {
    if (a.moveAt(i) != b.moveAt(i) || std::strcmp(a.sanAt(i), b.sanAt(i)) != 0) return false;
  }
  return a.result() == b.result() && a.reason() == b.reason() && a.repetitionCount() == b.repetitionCount();
}

bool sameSaved(const SavedGame& a, const SavedGame& b) {
  return a.vsEngine == b.vsEngine && a.engineLevel == b.engineLevel && a.humanSide == b.humanSide &&
         a.engineColor == b.engineColor && a.flipped == b.flipped && a.clockPreset == b.clockPreset &&
         a.clockMs[0] == b.clockMs[0] && a.clockMs[1] == b.clockMs[1];
}

bool isNewGame(const Game& game) {
  return game.plyCount() == 0 && game.startedFromStartpos() && game.position() == Position() &&
         game.result() == GameResult::Ongoing;
}

// A SavedGame no decoder would produce: proof that a refused blob leaves it alone.
SavedGame sentinel() {
  SavedGame s;
  s.vsEngine = true;
  s.engineLevel = 6;
  s.humanSide = HumanSide::Random;
  s.engineColor = Color::White;
  s.flipped = true;
  s.clockPreset = ClockPreset::Classic30;
  s.clockMs[0] = 0xDEADBEEFu;
  s.clockMs[1] = 0x12345678u;
  return s;
}

// Encodes g_original with `saved` into g_blob, decodes it into g_decoded, and checks that
// both halves came back and that the bytes are canonical (encoding again gives the same).
void roundTrip(const SavedGame& saved, const char* what) {
  g_blob.size = encodeSavedGame(g_original, saved, g_blob.bytes, sizeof g_blob.bytes);
  CHECK(g_blob.size >= kSavedGameMinBytes && g_blob.size <= kSavedGameMaxBytes, what);
  SavedGame back;
  const SaveCheck check = decodeSavedGame(g_blob.bytes, g_blob.size, g_decoded, back);
  if (check != SaveCheck::Ok) std::printf("     %s: decode said %s\n", what, checkName(check));
  CHECK(check == SaveCheck::Ok, what);
  CHECK(sameGame(g_original, g_decoded), what);
  CHECK(sameSaved(saved, back), what);
  CHECK(g_decoded.atLatest(), "the decoded game stands at its latest position");
  g_other.size = encodeSavedGame(g_decoded, back, g_other.bytes, sizeof g_other.bytes);
  CHECK(g_other.size == g_blob.size && std::memcmp(g_other.bytes, g_blob.bytes, g_blob.size) == 0,
        "encoding the decoded game gives the same bytes");
}

// ------------------------------------------------------------------ the CRC

void testCrc() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(arrocco::ui::crc32(check, sizeof check) == 0xCBF43926u, "CRC-32 check value of \"123456789\"");
  CHECK(arrocco::ui::crc32(check, 0) == 0u, "CRC-32 of nothing");
  const uint8_t zero = 0;
  CHECK(arrocco::ui::crc32(&zero, 1) == 0xD202EF8Du, "CRC-32 of one zero byte (as zlib)");
}

// ------------------------------------------------------------------ round trips

void testEmptyGame() {
  g_original.newGame();
  SavedGame saved;
  roundTrip(saved, "a new game with no move");
  CHECK(g_blob.size == kSavedGameMinBytes, "27 bytes: no FEN, no move");
  CHECK(std::memcmp(g_blob.bytes, "ARSG", 4) == 0 && g_blob.bytes[kOffVersion] == 1, "magic and version");
}

void testRichGame() {
  // Both castlings, an en passant, captures, a check, and a clock with increment that
  // has run for a while: 15 + 10, the human Black against level 5, board flipped.
  g_original.newGame();
  CHECK(playLine(g_original, "e2e4 d7d5 e4e5 f7f5 e5f6 g7f6 f1e2 b8c6 g1f3 d8d6 e1g1 c8d7 d2d4 e8c8 c2c4 d5c4 "
                             "e2c4 d6b4"),
        "a line with both castlings and an en passant");
  CHECK(g_original.moveAt(4).isEnPassant() && g_original.moveAt(10).isCastling() && g_original.moveAt(13).isCastling(),
        "the special moves are there");
  SavedGame saved;
  saved.vsEngine = true;
  saved.engineLevel = 4;
  saved.humanSide = HumanSide::Black;
  saved.engineColor = Color::White;
  saved.flipped = true;
  saved.clockPreset = ClockPreset::Rapid15Inc10;
  saved.clockMs[0] = 912345;
  saved.clockMs[1] = 897654;
  roundTrip(saved, "castling, en passant, clock, flipped, vs engine");
  CHECK(g_blob.size == kSavedGameMinBytes + 2 * 18, "two bytes a ply");

  // The history is really there: Undo, the repetition count and the captures follow it.
  CHECK(g_decoded.takeBack() && g_original.takeBack() && g_decoded.position() == g_original.position(),
        "Undo after the decode takes back the same move");
  CHECK(g_decoded.capturedPieces(Color::White).count == g_original.capturedPieces(Color::White).count &&
            g_decoded.capturedPieces(Color::Black).count == g_original.capturedPieces(Color::Black).count,
        "the captured pieces follow");

  // Random stays Random: the drawn colour is in engineColor, the choice in humanSide.
  saved.humanSide = HumanSide::Random;
  saved.engineColor = Color::Black;
  saved.flipped = false;
  roundTrip(saved, "the colour drawn at random");
}

void testPromotions() {
  // From a position, with Black to move after the first ply: both underpromotions and a
  // capture-promotion, and a FEN start that must come back with its move numbers.
  g_original.newGame();
  CHECK(g_original.setFromFen("1n2k3/P7/8/8/8/8/6p1/4K2R w K - 0 37"), "promotion position");
  CHECK(playLine(g_original, "a7b8n g2h1r e1e2 h1h2 e2d3 e8e7"), "capture-promotions to a knight and to a rook");
  CHECK(g_original.moveAt(0).promotion() == PieceType::Knight && g_original.moveAt(1).promotion() == PieceType::Rook,
        "the promotion pieces are in the moves");
  SavedGame saved;
  roundTrip(saved, "promotions from a FEN start");
  CHECK(!g_decoded.startedFromStartpos() && g_decoded.moveNumberOfPly(0) == 37, "the FEN start and its move number");
  CHECK(g_blob.bytes[kOffFenLength] > 0, "the FEN is stored");

  // A queen promotion with check, Black to move in the FEN.
  g_original.newGame();
  CHECK(g_original.setFromFen("7k/8/8/8/8/8/p7/2K5 b - - 0 50") && playLine(g_original, "a2a1q c1c2 h8g7"),
        "a FEN with Black to move");
  roundTrip(saved, "Black moves first");
  CHECK(g_decoded.sideOfPly(0) == Color::Black && g_decoded.moveNumberOfPly(0) == 50, "Black's move 50 first");
}

void testFinishedGames() {
  SavedGame saved;
  // By the rules: these come back from the moves alone.
  g_original.newGame();
  CHECK(playLine(g_original, "f2f3 e7e5 g2g4 d8h4") && g_original.reason() == GameEndReason::Checkmate,
        "fool's mate");
  roundTrip(saved, "checkmate");
  CHECK(g_decoded.isOver() && g_decoded.result() == GameResult::BlackWins, "the mate is back");
  CHECK(g_blob.bytes[kOffResult] == 0 && g_blob.bytes[kOffReason] == 0, "a rule's result is not stored");

  g_original.newGame();
  CHECK(playLine(g_original, "e2e3 a7a5 d1h5 a8a6 h5a5 h7h5 h2h4 a6h6 a5c7 f7f6 c7d7 e8f7 d7b7 d8d3 b7b8 d3h7 b8c8 "
                             "f7g6 c8e6") &&
            g_original.reason() == GameEndReason::Stalemate,
        "Loyd's stalemate");
  roundTrip(saved, "stalemate");

  g_original.newGame();
  CHECK(playLine(g_original, "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8") &&
            g_original.reason() == GameEndReason::ThreefoldRepetition,
        "threefold repetition");
  roundTrip(saved, "threefold");

  // Declared by the players: stored, and declared again after the replay.
  struct Declared {
    GameResult result;
    GameEndReason reason;
    const char* what;
  };
  const Declared kDeclared[] = {{GameResult::BlackWins, GameEndReason::Resignation, "White resigned"},
                                {GameResult::WhiteWins, GameEndReason::Timeout, "Black ran out of time"},
                                {GameResult::Draw, GameEndReason::Agreement, "a draw agreed"}};
  for (const Declared& d : kDeclared) {
    g_original.newGame();
    CHECK(playLine(g_original, "e2e4 e7e5 g1f3") && g_original.declareResult(d.result, d.reason), d.what);
    saved.clockPreset = d.reason == GameEndReason::Timeout ? ClockPreset::Blitz5 : ClockPreset::Off;
    saved.clockMs[0] = d.reason == GameEndReason::Timeout ? 120000 : 0;
    saved.clockMs[1] = 0;
    roundTrip(saved, d.what);
    CHECK(g_decoded.isOver() && g_decoded.result() == d.result && g_decoded.reason() == d.reason, d.what);
  }
}

void testBrowsingAndChanges() {
  g_original.newGame();
  CHECK(playLine(g_original, "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6"), "a queen's gambit");
  SavedGame saved;
  Blob& at = g_other;
  at.size = encodeSavedGame(g_original, saved, at.bytes, sizeof at.bytes);
  static Blob browsing;
  CHECK(g_original.goToPly(2), "browse back to ply 2");
  browsing.size = encodeSavedGame(g_original, saved, browsing.bytes, sizeof browsing.bytes);
  CHECK(browsing.size == at.size && std::memcmp(browsing.bytes, at.bytes, at.size) == 0,
        "browsing does not change the blob: the whole line is saved");
  g_original.goToLatest();

  static Blob before;
  before = at;
  CHECK(g_original.playUci("c1g5"), "one more move");
  at.size = encodeSavedGame(g_original, saved, at.bytes, sizeof at.bytes);
  CHECK(at.size == before.size + 2, "one more move, two more bytes");
  CHECK(g_original.takeBack(), "take it back");
  at.size = encodeSavedGame(g_original, saved, at.bytes, sizeof at.bytes);
  CHECK(at.size == before.size && std::memcmp(at.bytes, before.bytes, at.size) == 0,
        "taken back: the very same bytes as before the move");
  saved.flipped = true;
  at.size = encodeSavedGame(g_original, saved, at.bytes, sizeof at.bytes);
  CHECK(at.size == before.size && std::memcmp(at.bytes, before.bytes, at.size) != 0, "a flip changes the blob");
}

void testRepetitionAcrossTheCut() {
  // Two of the three occurrences before the power cut, the third one after it.
  g_original.newGame();
  CHECK(playLine(g_original, "g1f3 g8f6 f3g1 f6g8"), "the start position, twice");
  SavedGame saved;
  roundTrip(saved, "half a repetition");
  CHECK(g_decoded.repetitionCount() == 2 && !g_decoded.isOver(), "two occurrences so far");
  CHECK(playLine(g_decoded, "g1f3 g8f6 f3g1 f6g8") && g_decoded.reason() == GameEndReason::ThreefoldRepetition,
        "the third one after the decode is a threefold repetition");
}

void testLongestGame() {
  // 1024 plies from a FEN: the largest blob there can be must fit kSavedGameMaxBytes.
  g_original.newGame();
  CHECK(g_original.setFromFen("r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 4 4"), "four knights");
  static const char* const kShuffle[] = {"c3b1", "c6b8", "b1c3", "b8c6"};
  int played = 0;
  while (played < kMaxGamePlies && g_original.playUci(kShuffle[played % 4])) ++played;
  CHECK(played == kMaxGamePlies && g_original.isHistoryFull(), "a full history");
  SavedGame saved;
  saved.clockPreset = ClockPreset::Classic30;
  saved.clockMs[0] = 1;
  saved.clockMs[1] = 0xFFFFFFFFu;
  roundTrip(saved, "1024 plies, extreme clock values");
  CHECK(g_blob.size <= kSavedGameMaxBytes, "the largest game fits kSavedGameMaxBytes");
  CHECK(encodeSavedGame(g_original, saved, g_other.bytes, g_blob.size - 1) == 0, "one byte short: refused");
  CHECK(encodeSavedGame(g_original, saved, nullptr, sizeof g_other.bytes) == 0, "no buffer: refused");
}

// ------------------------------------------------------------------ refusals

void expectRefused(const Blob& b, SaveCheck expected, const char* what) {
  SavedGame untouched = sentinel();
  g_decoded.newGame();
  CHECK(g_decoded.playUci("e2e4"), "the decoder is handed a game with a move in it");
  const SaveCheck got = decodeSavedGame(b.bytes, b.size, g_decoded, untouched);
  if (got != expected) std::printf("     %s: got %s, expected %s\n", what, checkName(got), checkName(expected));
  CHECK(got == expected, what);
  CHECK(isNewGame(g_decoded), "a refused blob leaves a new game, never half of one");
  CHECK(sameSaved(untouched, sentinel()), "and the rest untouched");
}

// A valid blob of a short engine game with a clock, to break in every way.
void makeReference() {
  g_original.newGame();
  CHECK(playLine(g_original, "e2e4 c7c5 g1f3 d7d6"), "a Sicilian");
  SavedGame saved;
  saved.vsEngine = true;
  saved.engineLevel = 2;
  saved.clockPreset = ClockPreset::Rapid10;
  saved.clockMs[0] = 590000;
  saved.clockMs[1] = 585000;
  g_blob.size = encodeSavedGame(g_original, saved, g_blob.bytes, sizeof g_blob.bytes);
}

void testTornAndFlipped() {
  makeReference();
  static Blob b;
  for (size_t n = 0; n < g_blob.size; ++n) {   // every torn write: each prefix
    b = g_blob;
    b.size = n;
    SavedGame untouched = sentinel();
    const SaveCheck got = decodeSavedGame(b.bytes, b.size, g_decoded, untouched);
    CHECK(got != SaveCheck::Ok && isNewGame(g_decoded) && sameSaved(untouched, sentinel()), "a prefix is refused");
  }
  b = g_blob;
  b.bytes[b.size] = 0;
  ++b.size;
  SavedGame scratch;
  CHECK(decodeSavedGame(b.bytes, b.size, g_decoded, scratch) != SaveCheck::Ok, "a byte too many is refused");
  int flips = 0;
  for (size_t i = 0; i < g_blob.size; ++i) {    // every single bit flipped, CRC included
    for (int bit = 0; bit < 8; ++bit) {
      b = g_blob;
      b.bytes[i] = static_cast<uint8_t>(b.bytes[i] ^ (1u << bit));
      SavedGame untouched = sentinel();
      const SaveCheck got = decodeSavedGame(b.bytes, b.size, g_decoded, untouched);
      const SaveCheck expected = i < 4 ? SaveCheck::NotASave : (i == kOffVersion ? SaveCheck::OtherVersion : SaveCheck::BadCrc);
      CHECK(got == expected && isNewGame(g_decoded) && sameSaved(untouched, sentinel()), "a flipped bit is refused");
      ++flips;
    }
  }
  CHECK(flips == static_cast<int>(g_blob.size) * 8, "all the bits were flipped once");
  CHECK(decodeSavedGame(nullptr, 100, g_decoded, scratch) == SaveCheck::TooShort, "no data");
}

void testHeader() {
  makeReference();
  static Blob b;
  b = g_blob;
  std::memcpy(b.bytes, "ARSH", 4);
  reseal(b);
  expectRefused(b, SaveCheck::NotASave, "another magic, even with a good CRC");
  const uint8_t kVersions[] = {0, 2, 3, 0x7F, 0xFF};
  for (uint8_t v : kVersions) {
    b = g_blob;
    b.bytes[kOffVersion] = v;
    reseal(b);
    expectRefused(b, SaveCheck::OtherVersion, "another version, even with a good CRC");
  }
  static uint8_t erased[64];
  std::memset(erased, 0xFF, sizeof erased);
  b.size = sizeof erased;
  std::memcpy(b.bytes, erased, sizeof erased);
  expectRefused(b, SaveCheck::NotASave, "erased flash (all ones)");
  std::memset(b.bytes, 0, sizeof erased);
  expectRefused(b, SaveCheck::NotASave, "all zeros");
}

void testFieldsBehindTheCrc() {
  makeReference();
  struct Edit {
    size_t offset;
    uint8_t value;
    SaveCheck expected;
    const char* what;
  };
  const Edit kEdits[] = {
      {kOffMode, 2, SaveCheck::BadField, "a mode that does not exist"},
      {kOffFlags, 4, SaveCheck::BadField, "an unknown flag"},
      {kOffFlags, 0x80, SaveCheck::BadField, "the top flag"},
      {kOffLevel, 8, SaveCheck::BadField, "engine level 9 of 8"},
      {kOffLevel, 0xFF, SaveCheck::BadField, "engine level 256"},
      {kOffHumanSide, 3, SaveCheck::BadField, "a fourth colour"},
      {kOffClockPreset, 5, SaveCheck::BadField, "a clock preset past the last"},
      {kOffResult, 4, SaveCheck::BadField, "a result past Draw"},
      {kOffReason, 9, SaveCheck::BadField, "a reason past Agreement"},
      {kOffReason, 1, SaveCheck::BadField, "checkmate stored as declared"},
      {kOffReason, 4, SaveCheck::BadField, "threefold stored as declared"},
      {kOffReason, 6, SaveCheck::BadField, "a resignation with no result"},
      {kOffResult, 1, SaveCheck::BadField, "a result with no reason"},
      {kOffPlies, 5, SaveCheck::BadLayout, "one ply more than the bytes hold"},
      {kOffPlies, 3, SaveCheck::BadLayout, "one ply fewer"},
      {kOffFenLength, 1, SaveCheck::BadLayout, "a FEN length the size does not have"},
  };
  static Blob b;
  for (const Edit& e : kEdits) {
    b = g_blob;
    b.bytes[e.offset] = e.value;
    reseal(b);
    expectRefused(b, e.expected, e.what);
  }

  // Times on a clock that is off.
  b = g_blob;
  b.bytes[kOffClockPreset] = 0;
  reseal(b);
  expectRefused(b, SaveCheck::BadField, "times on a game with no clock");
  b.bytes[kOffClockPreset] = 0;
  put32(b.bytes + kOffWhiteMs, 0);
  put32(b.bytes + kOffWhiteMs + 4, 0);
  reseal(b);
  SavedGame back;
  CHECK(decodeSavedGame(b.bytes, b.size, g_decoded, back) == SaveCheck::Ok && back.clockPreset == ClockPreset::Off,
        "and with zeros the same game is fine");

  // The layout adds up, but the plies are more than a game can have.
  static Blob big;
  big.size = kSavedGameMinBytes + 2 * (kMaxGamePlies + 1);
  std::memset(big.bytes, 0, big.size);
  std::memcpy(big.bytes, g_blob.bytes, kOffFen);
  put16(big.bytes + kOffPlies, static_cast<uint16_t>(kMaxGamePlies + 1));
  big.bytes[kOffFenLength] = 0;
  reseal(big);
  expectRefused(big, SaveCheck::BadLayout, "1025 plies");
  // A FEN longer than any FEN can be.
  big.size = kSavedGameMinBytes + 96;
  std::memcpy(big.bytes, g_blob.bytes, kOffFen);
  put16(big.bytes + kOffPlies, 0);
  big.bytes[kOffFenLength] = 96;
  std::memset(big.bytes + kOffFen, 'p', 96);
  reseal(big);
  expectRefused(big, SaveCheck::BadLayout, "a 96-character FEN");
}

// Builds a blob by hand: the reference header, then `fen` and `moves` as given.
void handMade(Blob& b, const char* fen, const uint16_t* moves, int count, uint8_t result = 0, uint8_t reason = 0) {
  const size_t fenLength = std::strlen(fen);
  b.size = kSavedGameMinBytes + fenLength + 2 * static_cast<size_t>(count);
  std::memcpy(b.bytes, g_blob.bytes, kOffFen);
  b.bytes[kOffResult] = result;
  b.bytes[kOffReason] = reason;
  put16(b.bytes + kOffPlies, static_cast<uint16_t>(count));
  b.bytes[kOffFenLength] = static_cast<uint8_t>(fenLength);
  std::memcpy(b.bytes + kOffFen, fen, fenLength);
  for (int i = 0; i < count; ++i) put16(b.bytes + kOffFen + fenLength + 2 * i, moves[i]);
  reseal(b);
}

void testGameBehindTheCrc() {
  makeReference();
  static Blob b;
  const uint16_t e2e4 = Move(E2, E4).raw();
  const uint16_t e7e5 = Move(E7, E5).raw();

  handMade(b, "", &e2e4, 1);
  SavedGame back;
  CHECK(decodeSavedGame(b.bytes, b.size, g_decoded, back) == SaveCheck::Ok && g_decoded.plyCount() == 1,
        "the hand-made blob itself is good");

  handMade(b, "this is not a FEN", nullptr, 0);
  expectRefused(b, SaveCheck::BadStart, "a start that is not a FEN");
  handMade(b, "8/8/8/8/8/8/8/8 w - - 0 1", nullptr, 0);
  expectRefused(b, SaveCheck::BadStart, "a board without kings");

  const uint16_t e2e5 = Move(E2, E5).raw();
  handMade(b, "", &e2e5, 1);
  expectRefused(b, SaveCheck::IllegalMove, "a pawn three squares forward");
  const uint16_t blackFirst[] = {e7e5};
  handMade(b, "", blackFirst, 1);
  expectRefused(b, SaveCheck::IllegalMove, "Black moving first");
  const uint16_t castleAsNormal = Move(E1, G1).raw();
  const uint16_t kingsGambit[] = {e2e4, e7e5, Move(G1, F3).raw(), Move(B8, C6).raw(), Move(F1, C4).raw(),
                                  Move(G8, F6).raw(), castleAsNormal};
  handMade(b, "", kingsGambit, 7);
  expectRefused(b, SaveCheck::IllegalMove, "castling written as a plain king move (wrong kind)");
  const uint16_t promotionAsQueen = Move(A7, B8, Move::Kind::Promotion, PieceType::Queen).raw();
  const uint16_t wrongPiece = Move(A7, B8, Move::Kind::Promotion, PieceType::Rook).raw();
  handMade(b, "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", &promotionAsQueen, 1);
  SavedGame promoted;
  CHECK(decodeSavedGame(b.bytes, b.size, g_decoded, promoted) == SaveCheck::Ok &&
            g_decoded.position().pieceAt(B8) == Piece(Color::White, PieceType::Queen),
        "a capture-promotion to a queen from a FEN");
  CHECK(wrongPiece != promotionAsQueen, "a different promotion piece is a different move");
  const uint16_t noKind = Move(A7, B8).raw();
  handMade(b, "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", &noKind, 1);
  expectRefused(b, SaveCheck::IllegalMove, "a promotion without its kind");
  const uint16_t zero = 0;
  handMade(b, "", &zero, 1);
  expectRefused(b, SaveCheck::IllegalMove, "the null move");

  // Moves after the end that the rules found.
  const uint16_t foolsMate[] = {Move(F2, F3).raw(), e7e5, Move(G2, G4).raw(), Move(D8, H4).raw(), e2e4};
  handMade(b, "", foolsMate, 5);
  expectRefused(b, SaveCheck::IllegalMove, "a move after checkmate");
  // Declared results the rules refuse.
  handMade(b, "", foolsMate, 4, static_cast<uint8_t>(GameResult::WhiteWins), static_cast<uint8_t>(GameEndReason::Resignation));
  expectRefused(b, SaveCheck::BadResult, "a resignation after checkmate");
  handMade(b, "", &e2e4, 1, static_cast<uint8_t>(GameResult::Draw), static_cast<uint8_t>(GameEndReason::Resignation));
  expectRefused(b, SaveCheck::BadResult, "a resignation that is a draw");
  handMade(b, "", &e2e4, 1, static_cast<uint8_t>(GameResult::WhiteWins), static_cast<uint8_t>(GameEndReason::Agreement));
  expectRefused(b, SaveCheck::BadResult, "an agreement that is a win");
  handMade(b, "", &e2e4, 1, static_cast<uint8_t>(GameResult::Draw), static_cast<uint8_t>(GameEndReason::Agreement));
  CHECK(decodeSavedGame(b.bytes, b.size, g_decoded, back) == SaveCheck::Ok && g_decoded.isOver(),
        "and a draw agreed is fine");
}

// Made-up blobs: random bytes, and the reference with a few random bytes changed, with or
// without a fresh CRC. Nothing may crash or read past the blob; whatever is accepted
// must be a real game that encodes back to the very same bytes.
void testMadeUp() {
  makeReference();
  uint32_t state = 20261006u;
  auto next = [&state]() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };
  static Blob b;
  int accepted = 0;
  int accepted2 = 0;
  for (int round = 0; round < 40000; ++round) {
    const bool mutate = (round & 1) != 0;
    if (mutate) {
      b = g_blob;
      const int changes = 1 + static_cast<int>(next() % 3);
      for (int c = 0; c < changes; ++c) b.bytes[next() % b.size] = static_cast<uint8_t>(next());
    } else {
      b.size = next() % 80;
      for (size_t i = 0; i < b.size; ++i) b.bytes[i] = static_cast<uint8_t>(next());
      if (b.size >= kSavedGameMinBytes && (next() & 1)) {
        std::memcpy(b.bytes, "ARSG", 4);
        b.bytes[kOffVersion] = 1;
      }
    }
    if (b.size >= 4 && (next() & 1)) reseal(b);
    SavedGame back = sentinel();
    const SaveCheck got = decodeSavedGame(b.bytes, b.size, g_decoded, back);
    if (got != SaveCheck::Ok) {
      CHECK(isNewGame(g_decoded) && sameSaved(back, sentinel()), "refused: new game, untouched");
      continue;
    }
    ++accepted;
    if (b.bytes[kOffFenLength] != 0) continue;   // a FEN may be normalised on the way back
    ++accepted2;
    g_other.size = encodeSavedGame(g_decoded, back, g_other.bytes, sizeof g_other.bytes);
    CHECK(g_other.size == b.size && std::memcmp(g_other.bytes, b.bytes, b.size) == 0,
          "accepted: it encodes back to the same bytes");
  }
  std::printf("     made-up blobs: %d accepted (%d compared byte for byte) of 40000\n", accepted, accepted2);
  CHECK(accepted > 0, "some mutations are still games (a level, a time, a flag changed)");
}

// ------------------------------------------------------------------ the clock

void testClock() {
  GameClock clock;
  clock.reset(ClockPreset::Rapid15Inc10);
  clock.start(Color::White, 1000);
  clock.moveMade(5000);   // White spent 4 s and got 10 back; Black runs from 5000
  CHECK(clock.bankedMs(Color::White) == 906000 && clock.bankedMs(Color::Black) == 900000,
        "banked times: as of the last move");
  CHECK(clock.remainingMs(Color::Black, 65000) == 840000 && clock.bankedMs(Color::Black) == 900000,
        "the running side's banked time does not move while it thinks");
  CHECK(clock.preset() == ClockPreset::Rapid15Inc10, "the clock knows its preset");

  GameClock back;
  back.restore(ClockPreset::Rapid15Inc10, clock.bankedMs(Color::White), clock.bankedMs(Color::Black));
  CHECK(back.enabled() && !back.running(), "restored: on, and stopped");
  CHECK(back.remainingMs(Color::Black, 999999) == 900000, "nothing runs before start()");
  back.start(Color::Black, 100);
  CHECK(back.remainingMs(Color::Black, 1100) == 899000, "resumed: Black spends from now");
  back.moveMade(1100);
  CHECK(back.bankedMs(Color::Black) == 909000 && back.runningSide() == Color::White, "and the increment still comes");

  back.restore(ClockPreset::Off, 5, 6);
  CHECK(!back.enabled() && back.bankedMs(Color::White) == 0 && back.bankedMs(Color::Black) == 0 &&
            back.preset() == ClockPreset::Off,
        "no clock: no times");
  back.restore(ClockPreset::Count, 5, 6);
  CHECK(!back.enabled() && back.preset() == ClockPreset::Off, "a preset that is not one: no clock");
  back.reset(ClockPreset::Off);
  CHECK(back.bankedMs(Color::White) == 0 && back.bankedMs(Color::Black) == 0, "a clock that is off banks zero");
}

}  // namespace

int main() {
  struct Test { const char* name; void (*run)(); };
  static const Test kTests[] = {
      {"CRC-32", testCrc},
      {"empty game", testEmptyGame},
      {"castling, e.p., clock", testRichGame},
      {"promotions, FEN starts", testPromotions},
      {"finished games", testFinishedGames},
      {"browsing and changes", testBrowsingAndChanges},
      {"repetition over a cut", testRepetitionAcrossTheCut},
      {"1024 plies", testLongestGame},
      {"torn and flipped", testTornAndFlipped},
      {"header", testHeader},
      {"fields behind the CRC", testFieldsBehindTheCrc},
      {"game behind the CRC", testGameBehindTheCrc},
      {"made-up blobs", testMadeUp},
      {"clock", testClock},
  };
  for (const Test& test : kTests) {
    const int failuresBefore = g_failures;
    const int checksBefore = g_checks;
    test.run();
    std::printf("%s %-24s %6d checks\n", g_failures == failuresBefore ? "ok  " : "FAIL", test.name, g_checks - checksBefore);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
