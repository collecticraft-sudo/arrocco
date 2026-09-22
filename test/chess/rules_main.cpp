// SPDX-License-Identifier: GPL-3.0-or-later
// Adversarial unit tests for the chess rules library: FEN, SAN, the awkward corners of en passant
// and castling, make/unmake, draws, and Game (history, browsing, results, engine/PGN output).
// Plain checks with messages; every failure is printed, the exit code is non-zero if any failed.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "arrocco/chess/game.h"
#include "arrocco/chess/position.h"

using namespace arrocco::chess;

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

#define CHECK_STR(actual, expected, msg)                                              \
  do {                                                                                \
    ++g_checks;                                                                       \
    if (std::strcmp((actual), (expected)) != 0) {                                     \
      ++g_failures;                                                                   \
      std::printf("FAIL line %d: %s   got \"%s\"  expected \"%s\"\n", __LINE__, (msg), (actual), (expected)); \
    }                                                                                 \
  } while (0)

const char* const kKiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

Position positionOf(const char* fen) {
  Position p;
  const bool ok = p.setFen(fen);
  CHECK(ok, fen);
  return p;
}

// FEN of a position in a rotating set of buffers, so that two calls can sit in one expression.
const char* fenOf(const Position& p) {
  static char buffers[4][kFenBufferSize];
  static int next = 0;
  char* out = buffers[next];
  next = (next + 1) % 4;
  p.toFen(out, kFenBufferSize);
  return out;
}

bool hasMove(const Position& p, const char* uci) { return !p.parseUci(uci).isNone(); }

void expectSan(const char* fen, const char* uci, const char* expected) {
  const Position p = positionOf(fen);
  const Move m = p.parseUci(uci);
  CHECK(!m.isNone(), uci);
  char san[16];
  std::memset(san, '~', sizeof san);
  p.toSan(m, san);
  CHECK_STR(san, expected, fen);
  CHECK(p.parseSan(expected) == m, "parseSan must read back what toSan wrote");
}

void expectSanParse(const char* fen, const char* san, const char* expectedUci) {
  const Position p = positionOf(fen);
  const Move m = p.parseSan(san);
  char uci[kUciBufferSize];
  m.toUci(uci);
  CHECK_STR(uci, expectedUci, san);
}

// ------------------------------------------------------------------ FEN

void testFen() {
  // Exact round trips.
  static const char* const kRoundTrips[] = {
      kStartposFen,
      kKiwipete,
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      "rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3",  // en passant really possible
      "r3k2r/8/8/8/8/8/8/R3K2R b Kq - 37 120",
      "8/8/8/8/8/8/8/K6k w - - 99 3000",
  };
  for (const char* fen : kRoundTrips) {
    const Position p = positionOf(fen);
    CHECK_STR(fenOf(p), fen, "FEN round trip");
    CHECK(p.hash() == p.computeHash(), "hash after setFen");
  }

  // Missing fields are filled in.
  CHECK_STR(fenOf(positionOf("8/8/8/8/8/8/8/K6k")), "8/8/8/8/8/8/8/K6k w - - 0 1", "placement only");
  CHECK_STR(fenOf(positionOf("8/8/8/8/8/8/8/K6k b")), "8/8/8/8/8/8/8/K6k b - - 0 1", "placement + side");
  CHECK_STR(fenOf(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -")), "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "missing clocks");
  CHECK_STR(fenOf(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 12")), "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 12 1",
            "missing fullmove number");
  CHECK_STR(fenOf(positionOf("  8/8/8/8/8/8/8/K6k   b   -   -   5   9  ")), "8/8/8/8/8/8/8/K6k b - - 5 9",
            "extra blanks");

  // Normalised, not rejected.
  CHECK_STR(fenOf(positionOf("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1")),
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "en-passant square nobody can use");
  CHECK_STR(fenOf(positionOf("r3k2r/8/8/8/8/8/8/4K2R w KQkq - 0 1")), "r3k2r/8/8/8/8/8/8/4K2R w Kkq - 0 1",
            "castling right without its rook");
  CHECK_STR(fenOf(positionOf("r3k2r/8/8/8/8/8/8/R2K3R w KQkq - 0 1")), "r3k2r/8/8/8/8/8/8/R2K3R w kq - 0 1",
            "castling rights without the king at home");

  // Garbage: rejected, position untouched, no crash.
  static char longGarbage[1200];
  for (int i = 0; i + 2 < static_cast<int>(sizeof longGarbage); i += 2) { longGarbage[i] = '8'; longGarbage[i + 1] = '/'; }
  longGarbage[sizeof longGarbage - 1] = '\0';
  static char longDigits[400];
  std::snprintf(longDigits, sizeof longDigits, "8/8/8/8/8/8/8/K6k w - - %s %s",
                "99999999999999999999999999999999999999", "88888888888888888888888888888888888888888888");
  static const char* const kGarbage[] = {
      nullptr,
      "",
      "    ",
      "hello world",
      "8/8/8/8/8/8/8/8 w - - 0 1",                                        // no kings
      "4k3/8/8/8/8/8/8/8 w - - 0 1",                                      // one king
      "4k3/8/8/8/8/8/8/3KK3 w - - 0 1",                                   // two white kings
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",          // bad side
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNRw KQkq - 0 1",           // no blank
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNRR w KQkq - 0 1",         // nine files
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",                   // seven ranks
      "rnbqkbnr/pppppppp/8/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",        // nine ranks
      "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",          // digit 9
      "rnbqkbnr/pppppppp/7/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",          // short rank
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/ w KQkq - 0 1",         // trailing slash
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBXR w KQkq - 0 1",          // unknown piece
      "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",                                   // pawn on rank 8
      "4k3/8/8/8/8/8/8/p3K3 w - - 0 1",                                   // pawn on rank 1
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQxq - 0 1",          // bad castling letter
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e9 0 1",         // bad square
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq zz 0 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e3x 0 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",          // clock not a number
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - -3 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1x",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 junk",
      "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1",                                  // Black in check, White to move
      "4k3/8/8/8/8/8/8/3Kk3 w - - 0 1",                                   // two black kings, adjacent too
      "8/8/8/8/8/8/8/6Kk w - - 0 1",                                      // kings side by side
      "qqqqkqqq/qqqqqqqq/q7/8/8/8/8/K7 b - - 0 1",                        // seventeen black pieces
      longGarbage,
  };
  Position p = positionOf(kKiwipete);
  const Position before = p;
  for (const char* garbage : kGarbage) {
    CHECK(!p.setFen(garbage), garbage == nullptr ? "(null)" : (garbage == longGarbage ? "(long garbage)" : garbage));
    CHECK(p == before, "a rejected FEN must leave the position untouched");
  }
  // Absurd clocks are clamped, not overflowed.
  CHECK(p.setFen(longDigits), "huge clocks are accepted");
  CHECK(p.halfmoveClock() == 30000 && p.fullmoveNumber() == 30000, "huge clocks are clamped");

  // toFen never writes past the buffer it is given.
  char small[20];
  std::memset(small, '~', sizeof small);
  CHECK(positionOf(kKiwipete).toFen(small, 10) == 0, "toFen reports a buffer too small");
  CHECK(small[0] == '\0' && small[10] == '~', "toFen stays inside a small buffer");
  CHECK(positionOf(kKiwipete).toFen(nullptr, 10) == 0, "toFen(nullptr)");
}

// ------------------------------------------------------------------ SAN

void testSanGeneration() {
  // Disambiguation by file, by rank, by both.
  expectSan("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2", "Nbd2");
  expectSan("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "f3d2", "Nfd2");
  expectSan("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a1a3", "R1a3");
  expectSan("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a5a3", "R5a3");
  expectSan("8/6K1/k7/8/4Q2Q/8/8/7Q w - - 0 1", "h4e1", "Qh4e1");
  expectSan("8/6K1/k7/8/4Q2Q/8/8/7Q w - - 0 1", "e4e1", "Qee1");
  expectSan("8/6K1/k7/8/4Q2Q/8/8/7Q w - - 0 1", "h1e1", "Q1e1");
  // Captures keep the disambiguation before the 'x'.
  expectSan("4k3/8/8/8/8/5N2/3p4/1N2K3 w - - 0 1", "b1d2", "Nbxd2");
  // A pinned twin is no twin: Ne2 cannot leave the e-file.
  expectSan("4r1k1/8/8/8/8/8/4N3/1N2K3 w - - 0 1", "b1c3", "Nc3");
  {
    const Position p = positionOf("4r1k1/8/8/8/8/8/4N3/1N2K3 w - - 0 1");
    CHECK(p.parseSan("Nec3").isNone(), "the pinned knight cannot go to c3");
    CHECK(p.parseSan("Nbc3") == p.parseUci("b1c3"), "over-specified SAN is still understood");
  }
  // Pawns.
  expectSan(kStartposFen, "e2e4", "e4");
  expectSan("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "e5f6", "exf6");
  expectSan("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e7d8q", "exd8=Q+");
  expectSan("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e7d8n", "exd8=N");
  expectSan("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e7e8r", "e8=R+");
  expectSan("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e7e8b", "e8=B");
  // Castling, with and without check.
  expectSan("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "O-O");
  expectSan("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "O-O-O");
  expectSan("5k2/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1", "O-O+");
  expectSan("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1", "O-O-O+");
  // Mate.
  expectSan("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", "d8h4", "Qh4#");
  expectSan("7k/8/6K1/8/8/8/8/R7 w - - 0 1", "a1a8", "Ra8#");
  // En passant out of check, and en passant uncovering a check (rook e8 behind the capturing pawn).
  expectSan("8/8/8/2k5/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", "exd3");
  expectSan("4r2k/8/8/8/3Pp3/8/8/4K3 b - d3 0 1", "e4d3", "exd3+");
  // Bishop takes and b-pawn takes on the same square differ only by the case of the first letter.
  expectSan("4k3/8/8/8/8/2p5/1P1B4/4K3 w - - 0 1", "d2c3", "Bxc3");
  expectSan("4k3/8/8/8/8/2p5/1P1B4/4K3 w - - 0 1", "b2c3", "bxc3");
  // Two pawns that can take on the same square: the file letter is the disambiguation.
  expectSan("4k3/8/8/8/3p4/2P1P3/8/4K3 w - - 0 1", "c3d4", "cxd4");
  expectSan("4k3/8/8/8/3p4/2P1P3/8/4K3 w - - 0 1", "e3d4", "exd4");
  // Seven knights around one square: alone on its file, alone on its rank, neither.
  expectSan("7k/8/2N1N3/1N6/8/1N3N2/2N1N3/K7 w - - 0 1", "f3d4", "Nfd4");
  expectSan("7k/8/2N1N3/1N6/8/1N3N2/2N1N3/K7 w - - 0 1", "b5d4", "N5d4");
  expectSan("7k/8/2N1N3/1N6/8/1N3N2/2N1N3/K7 w - - 0 1", "c6d4", "Nc6d4");

  // An illegal move has no SAN.
  {
    const Position p;
    char san[kSanBufferSize];
    p.toSan(Move(E2, E5), san);
    CHECK_STR(san, "??", "SAN of an illegal move");
    p.toSan(Move::none(), san);
    CHECK_STR(san, "??", "SAN of no move");
  }
}

void testSanParsing() {
  expectSanParse("5k2/8/8/8/8/8/8/4K2R w K - 0 1", "0-0", "e1g1");
  expectSanParse("5k2/8/8/8/8/8/8/4K2R w K - 0 1", "O-O+", "e1g1");
  expectSanParse("5k2/8/8/8/8/8/8/4K2R w K - 0 1", "O-O-O", "0000");  // no such right
  expectSanParse("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "O-O-O+", "e1c1");
  expectSanParse("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "0-0-0", "e1c1");
  expectSanParse("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "O-O-", "0000");
  expectSanParse("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "O-O-O-O", "0000");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e8=Q", "e7e8q");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e8Q", "e7e8q");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e8=N+", "e7e8n");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e8", "0000");     // which piece?
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e8=K", "0000");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "exd8=Q+", "e7d8q");
  expectSanParse("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "ed8Q", "e7d8q");
  const char* const kEnPassant = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
  expectSanParse(kEnPassant, "exd6", "e5d6");
  expectSanParse(kEnPassant, "exd6 e.p.", "e5d6");
  expectSanParse(kEnPassant, "exd6e.p.", "e5d6");
  expectSanParse(kEnPassant, "ed6", "e5d6");
  CHECK(positionOf(kEnPassant).parseSan("exd6").isEnPassant(), "exd6 is read as an en-passant capture");
  expectSanParse(kStartposFen, "Nf3", "g1f3");
  expectSanParse(kStartposFen, "  Nf3!? ", "g1f3");
  expectSanParse(kStartposFen, "Ng1f3", "g1f3");
  expectSanParse(kStartposFen, "Nf3 Nf6", "g1f3");  // stops at the first blank
  expectSanParse(kStartposFen, "e4", "e2e4");
  expectSanParse(kStartposFen, "Nd2", "0000");   // own pawn there
  expectSanParse(kStartposFen, "Ke2", "0000");
  expectSanParse(kStartposFen, "e5", "0000");
  expectSanParse("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "Nd2", "0000");  // ambiguous: refuse to guess
  static const char* const kNonsense[] = {"", " ", "x", "+", "#", "Zf3", "Nf9", "Ni3", "N", "e", "=Q", "e8=",
                                          "O", "O-", "0", "--", "e.p.", "Nf3Nf3Nf3Nf3Nf3Nf3Nf3", "\xff\xfe\xfd",
                                          "Qh4e1h4e1", "O-O-O-O-O-O-O-O-O-O"};
  const Position start;
  for (const char* text : kNonsense) CHECK(start.parseSan(text).isNone(), text);
  CHECK(start.parseSan(nullptr).isNone(), "parseSan(nullptr)");

  // UCI parsing.
  CHECK(start.parseUci(nullptr).isNone(), "parseUci(nullptr)");
  static const char* const kBadUci[] = {"", "e2", "e2e", "e2e5", "e2e4q", "e2e4 ", "e9e4", "0000", "e2-e4", "e7e8q"};
  for (const char* text : kBadUci) {
    const bool shouldParse = std::strcmp(text, "e2e4 ") == 0;  // a trailing blank ends the token
    CHECK(start.parseUci(text).isNone() != shouldParse, text);
  }
  const Position castle = positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  CHECK(castle.parseUci("e1g1").isCastling(), "e1g1 is castling");
  CHECK(castle.parseUci("e1h1") == castle.parseUci("e1g1"), "king-takes-rook spelling of O-O");
  CHECK(castle.parseUci("e1a1") == castle.parseUci("e1c1"), "king-takes-rook spelling of O-O-O");
  const Position promo = positionOf("3n3k/4P3/8/8/8/8/8/4K3 w - - 0 1");
  CHECK(promo.parseUci("e7e8").isNone(), "a promotion needs its piece");
  CHECK(promo.parseUci("e7e8Q") == promo.parseUci("e7e8q"), "promotion letter in either case");
  CHECK(promo.parseUci("e7e8k").isNone(), "no promotion to king");
  CHECK(promo.isPromotionMove(E7, E8) && promo.isPromotionMove(E7, D8) && !promo.isPromotionMove(E1, E2),
        "isPromotionMove");
  CHECK(promo.findLegalMove(E7, E8).isNone(), "findLegalMove without a piece does not match a promotion");
  CHECK(promo.findLegalMove(E7, E8, PieceType::Rook).promotion() == PieceType::Rook, "findLegalMove with a piece");
}

// ------------------------------------------------------------------ en passant

void testEnPassant() {
  // The capture would remove both pawns from the fifth rank and leave the king facing the rook.
  {
    const Position p = positionOf("8/8/8/KPp4r/8/8/8/7k w - c6 0 1");
    CHECK(p.enPassantSquare() == kNoSquare, "an en-passant square nobody may use is not kept");
    CHECK_STR(fenOf(p), "8/8/8/KPp4r/8/8/8/7k w - - 0 1", "and does not come back in the FEN");
    CHECK(p.findLegalMove(B5, C6).isNone(), "bxc6 e.p. would expose the king along the rank");
    CHECK(p.parseSan("bxc6").isNone(), "bxc6 refused in SAN too");
    MoveList moves;
    p.generateLegalMoves(moves);
    bool anyEnPassant = false;
    for (const Move m : moves) anyEnPassant = anyEnPassant || m.isEnPassant();
    CHECK(!anyEnPassant, "no en-passant move generated");
    CHECK(moves.size() == 4, "exactly Ka4, Ka6, Kb6 and b6 (b4 is covered by the c5 pawn)");
  }
  // Same thing reached by playing the double push.
  {
    Position p = positionOf("8/2p5/8/KP5r/8/8/8/7k b - - 0 1");
    Undo undo;
    p.make(p.parseUci("c7c5"), undo);
    CHECK(p.enPassantSquare() == kNoSquare, "double push, capture illegal: no en-passant square");
    CHECK(p.hash() == p.computeHash(), "hash after the double push");
    CHECK(p.hash() == positionOf("8/8/8/KPp4r/8/8/8/7k w - - 0 1").hash(), "same hash as the position without e.p.");
    CHECK(!hasMove(p, "b5c6"), "b5c6 is not legal");
  }
  // Without the rook the capture is fine.
  {
    Position p = positionOf("8/2p5/8/KP6/8/8/8/7k b - - 0 1");
    Undo undo;
    p.make(p.parseUci("c7c5"), undo);
    CHECK(p.enPassantSquare() == C6, "double push with a legal capture: square kept");
    CHECK_STR(fenOf(p), "8/8/8/KPp5/8/8/8/7k w - c6 0 2", "FEN shows it");
    CHECK(p.hash() == p.computeHash(), "hash with en-passant file");
    const Move capture = p.parseUci("b5c6");
    CHECK(capture.isEnPassant(), "b5c6 is an en-passant capture");
    CHECK(p.isCapture(capture), "isCapture knows en passant");
    const Position beforeCapture = p;
    Undo undo2;
    p.make(capture, undo2);
    CHECK_STR(fenOf(p), "8/8/2P5/K7/8/8/8/7k b - - 0 2", "the pawn beside is gone");
    CHECK(undo2.captured == Piece(Color::Black, PieceType::Pawn), "captured piece recorded");
    CHECK(changedSquares(beforeCapture, p).count() == 3, "en passant changes three squares");
    p.unmake(undo2);
    CHECK(p == beforeCapture, "unmake of en passant");
  }
  // Capturing pawn pinned on a file/diagonal: the square is not kept either.
  {
    Position p = positionOf("4r2k/3p4/8/4P3/8/8/8/4K3 b - - 0 1");
    Undo undo;
    p.make(p.parseUci("d7d5"), undo);
    CHECK(p.enPassantSquare() == kNoSquare, "pawn pinned on the e-file cannot capture en passant");
    CHECK(!hasMove(p, "e5d6"), "exd6 illegal");
    CHECK(hasMove(p, "e5e6"), "but the pinned pawn may advance");
  }
  // The CAPTURED pawn was the only thing between a bishop and the king (diagonal, not rank).
  {
    const Position p = positionOf("6b1/8/8/3pP3/8/1K6/8/7k w - d6 0 1");
    CHECK(p.enPassantSquare() == kNoSquare && !hasMove(p, "e5d6"), "exd6 e.p. would open the g8-b3 diagonal");
    CHECK(hasMove(p, "e5e6"), "the pawn may still advance (and block)");
  }
  // En passant as the only answer to a check by the pawn that has just arrived.
  {
    const Position p = positionOf("8/8/8/2k5/3Pp3/8/8/4K3 b - d3 0 1");
    CHECK(p.inCheck() && p.enPassantSquare() == D3, "checked by the d4 pawn, which can be taken en passant");
    CHECK(p.parseUci("e4d3").isEnPassant(), "exd3 e.p. removes the checking pawn");
  }
}

// ------------------------------------------------------------------ castling

void testCastling() {
  struct Case { const char* fen; bool kingside; bool queenside; const char* what; };
  static const Case kCases[] = {
      {"r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1", true, true, "nothing in the way"},
      {"r3k2r/8/8/8/4R3/8/8/4K3 b kq - 0 1", false, false, "not out of check"},
      {"r3k2r/8/8/8/5R2/8/8/4K3 b kq - 0 1", false, true, "not through check (f8)"},
      {"r3k2r/8/8/8/6R1/8/8/4K3 b kq - 0 1", false, true, "not into check (g8)"},
      {"r3k2r/8/8/8/3R4/8/8/4K3 b kq - 0 1", true, false, "not through check (d8)"},
      {"r3k2r/8/8/8/2R5/8/8/4K3 b kq - 0 1", true, false, "not into check (c8)"},
      {"r3k2r/8/8/8/1R6/8/8/4K3 b kq - 0 1", true, true, "b8 attacked: only the rook crosses it"},
      {"r3k2r/8/8/8/R6R/8/8/4K3 b kq - 0 1", true, true, "attacked rooks may castle"},
      {"rn2k2r/8/8/8/8/8/8/4K3 b kq - 0 1", true, false, "b8 occupied"},
      {"r3k1nr/8/8/8/8/8/8/4K3 b kq - 0 1", false, true, "g8 occupied"},
      {"r3k2r/8/8/8/8/8/8/4K3 b q - 0 1", false, true, "no kingside right"},
      {"r3k2r/8/8/8/8/8/8/4K3 b - - 0 1", false, false, "no rights"},
      {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", true, true, "white, nothing in the way"},
      {"r3k2r/8/8/8/8/8/6p1/R3K2R w KQkq - 0 1", false, true, "pawn on g2 attacks f1"},
      {"r3k2r/8/8/8/8/8/1p6/R3K2R w KQkq - 0 1", true, false, "pawn on b2 attacks c1"},
      {"r3k2r/8/8/8/8/8/p7/R3K2R w KQkq - 0 1", true, true, "pawn on a2 attacks only b1"},
  };
  for (const Case& c : kCases) {
    const Position p = positionOf(c.fen);
    const bool white = p.sideToMove() == Color::White;
    CHECK(hasMove(p, white ? "e1g1" : "e8g8") == c.kingside, c.what);
    CHECK(hasMove(p, white ? "e1c1" : "e8c8") == c.queenside, c.what);
    CHECK(p.parseSan("O-O").isNone() != c.kingside, c.what);
    CHECK(p.parseSan("O-O-O").isNone() != c.queenside, c.what);
  }

  // Rights die with the rook, even when it never moved.
  {
    Position p = positionOf("r3k2r/8/8/8/8/8/6B1/4K3 w kq - 0 1");
    const Position before = p;
    Undo undo;
    p.make(p.parseUci("g2a8"), undo);
    CHECK_STR(fenOf(p), "B3k2r/8/8/8/8/8/8/4K3 b k - 0 1", "rook captured on a8: queenside right gone");
    CHECK(p.hash() == p.computeHash(), "hash follows the castling rights");
    CHECK(!hasMove(p, "e8c8") && hasMove(p, "e8g8"), "only O-O is left");
    p.unmake(undo);
    CHECK(p == before, "unmake gives the right back");
  }
  {
    Position p = positionOf("4k2r/8/8/8/8/8/8/R3K2R b KQk - 0 1");
    Undo undo;
    p.make(p.parseUci("h8h1"), undo);
    CHECK_STR(fenOf(p), "4k3/8/8/8/8/8/8/R3K2r w Q - 0 2", "RxR on h1: both kingside rights gone");
  }
  // A pawn promoting by capture on a8 takes the right with the rook.
  {
    Position p = positionOf("r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1");
    const Position before = p;
    Undo undo;
    p.make(p.parseSan("bxa8=Q+"), undo);
    CHECK_STR(fenOf(p), "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1", "bxa8=Q+ ends Black's queenside right");
    CHECK(p.hash() == p.computeHash(), "hash after promotion by capture");
    p.unmake(undo);
    CHECK(p == before, "unmake of a promotion by capture");
  }
  // A rook that leaves and comes back has still moved.
  {
    Position p = positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    Undo undo[4];
    p.make(p.parseUci("h1h2"), undo[0]);
    p.make(p.parseUci("a8a7"), undo[1]);
    p.make(p.parseUci("h2h1"), undo[2]);
    p.make(p.parseUci("a7a8"), undo[3]);
    CHECK_STR(fenOf(p), "r3k2r/8/8/8/8/8/8/R3K2R w Qk - 4 3", "rights do not come back with the rook");
    CHECK(!p.samePositionAs(positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")), "not the same position for FIDE");
  }
  // Castling itself: squares, rights, unmake.
  {
    Position p = positionOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const Position before = p;
    Undo undo;
    p.make(p.parseUci("e1c1"), undo);
    CHECK_STR(fenOf(p), "r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1", "O-O-O played");
    CHECK(p.kingSquare(Color::White) == C1, "king square follows");
    CHECK(changedSquares(before, p).count() == 4, "castling changes four squares");
    CHECK(p.hash() == p.computeHash(), "hash after castling");
    p.unmake(undo);
    CHECK(p == before, "unmake of castling");
    CHECK(p.kingSquare(Color::White) == E1, "king square restored");
  }
}

// ------------------------------------------------------------------ make / unmake

uint64_t g_lcg = 0;
uint32_t nextRandom() {
  g_lcg = g_lcg * 6364136223846793005ULL + 1442695040888963407ULL;
  return static_cast<uint32_t>(g_lcg >> 33);
}

void walk(const char* startFen, uint64_t seed) {
  constexpr int kPlies = 200;
  static Undo undos[kPlies];
  static Position snapshots[kPlies + 1];
  static char fens[kPlies + 1][kFenBufferSize];

  g_lcg = seed;
  Position p = positionOf(startFen);
  int played = 0;
  int failuresBefore = g_failures;
  for (; played < kPlies; ++played) {
    snapshots[played] = p;
    p.toFen(fens[played], kFenBufferSize);

    // The FEN must describe the position completely: en-passant normalisation and the hash agree
    // between "reached by moves" and "read from text".
    Position reread;
    CHECK(reread.setFen(fens[played]), "own FEN accepted");
    CHECK(reread == p, "position reached by moves == position read from its FEN");
    CHECK(p.hash() == p.computeHash(), "incremental hash == hash from scratch");

    MoveList moves;
    if (p.generateLegalMoves(moves) == 0) break;
    // Both notations round-trip for EVERY legal move here.
    for (const Move m : moves) {
      char san[16];
      std::memset(san, '~', sizeof san);
      p.toSan(m, san);
      CHECK(std::strlen(san) < static_cast<std::size_t>(kSanBufferSize) && san[kSanBufferSize] == '~', "SAN fits its buffer");
      CHECK(p.parseSan(san) == m, "SAN round trip");
      char uci[kUciBufferSize];
      m.toUci(uci);
      CHECK(p.parseUci(uci) == m, "UCI round trip");
      CHECK(p.isLegal(m), "isLegal agrees with the generator");
    }
    if (g_failures != failuresBefore) {
      std::printf("     ... in the walk from \"%s\" at ply %d: %s\n", startFen, played, fens[played]);
      failuresBefore = g_failures;
    }
    p.make(moves[static_cast<int>(nextRandom() % static_cast<uint32_t>(moves.size()))], undos[played]);
  }
  for (int i = played - 1; i >= 0; --i) {
    p.unmake(undos[i]);
    CHECK(p == snapshots[i], "unmake restores every field, hash included");
    CHECK_STR(fenOf(p), fens[i], "unmake restores the FEN");
  }
  std::printf("     walk of %3d plies from %.30s...\n", played, startFen);
}

void testMakeUnmake() {
  walk(kStartposFen, 20260922ULL);
  walk(kStartposFen, 1ULL);
  walk(kKiwipete, 7ULL);
  walk("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 42ULL);  // promotions
  walk("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 99ULL);
  walk("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3ULL);                           // en-passant pins
  walk("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1", 5ULL);                        // pawns only
}

// ------------------------------------------------------------------ draws and mates on a Position

void testPositionStatus() {
  struct Case { const char* fen; bool insufficient; const char* what; };
  static const Case kCases[] = {
      {"8/8/8/4k3/8/8/8/4K3 w - - 0 1", true, "K v K"},
      {"8/8/8/4k3/8/8/8/4KN2 w - - 0 1", true, "KN v K"},
      {"8/8/8/4k3/8/8/8/4KB2 w - - 0 1", true, "KB v K"},
      {"5n2/8/8/4k3/8/8/8/4K3 w - - 0 1", true, "K v KN"},
      {"5b2/8/8/4k3/8/8/8/2B1K3 w - - 0 1", true, "KB v KB, both on dark squares"},
      {"5b2/8/8/4k3/8/8/B7/2B1K3 w - - 0 1", false, "bishops on both colours"},
      {"2b5/8/8/4k3/8/8/8/2B1K3 w - - 0 1", false, "KB v KB, opposite colours"},
      {"8/8/8/4k3/8/8/8/3NKN2 w - - 0 1", false, "KNN v K (helpmate exists)"},
      {"5n2/8/8/4k3/8/8/8/4KN2 w - - 0 1", false, "KN v KN"},
      {"5n2/8/8/4k3/8/8/8/4KB2 w - - 0 1", false, "KB v KN"},
      {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", false, "a pawn"},
      {"8/8/8/4k3/8/8/8/R3K3 w - - 0 1", false, "a rook"},
      {"8/8/8/4k3/8/8/8/3QK3 w - - 0 1", false, "a queen"},
  };
  for (const Case& c : kCases) CHECK(positionOf(c.fen).isInsufficientMaterial() == c.insufficient, c.what);

  const Position stalemate = positionOf("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  CHECK(stalemate.isStalemate() && !stalemate.isCheckmate() && !stalemate.inCheck(), "stalemate");
  CHECK(stalemate.checkedKingSquare() == kNoSquare, "no king to highlight in stalemate");
  const Position mate = positionOf("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
  CHECK(mate.isCheckmate() && !mate.isStalemate(), "fool's mate");
  CHECK(mate.checkedKingSquare() == E1, "king to highlight");
  CHECK(mate.legalTargetsFrom(E1).empty(), "no targets for a mated king");
  const Position start;
  CHECK(!start.isCheckmate() && !start.isStalemate() && !start.isInsufficientMaterial() && !start.isFiftyMoveDraw(),
        "the starting position is none of these");
  CHECK(start.legalTargetsFrom(G1).count() == 2 && start.legalTargetsFrom(G1).contains(F3), "Ng1 has two targets");
  CHECK(start.legalTargetsFrom(E7).empty() && start.legalTargetsFrom(E4).empty(), "no targets from enemy or empty squares");
  CHECK(positionOf("8/8/8/4k3/8/8/8/R3K3 w - - 100 90").isFiftyMoveDraw(), "halfmove clock 100");
  CHECK(!positionOf("8/8/8/4k3/8/8/8/R3K3 w - - 99 90").isFiftyMoveDraw(), "halfmove clock 99");
}

// ------------------------------------------------------------------ Game

// Game is 25 KB: keep the ones used by the tests out of the stack, as the firmware will.
struct Guarded {
  uint64_t before = 0xA5A5A5A5A5A5A5A5ULL;
  Game game;
  uint64_t after = 0x5A5A5A5A5A5A5A5AULL;
  bool intact() const { return before == 0xA5A5A5A5A5A5A5A5ULL && after == 0x5A5A5A5A5A5A5A5AULL; }
};
Guarded g_guarded;

bool playAll(Game& game, const char* const* sans, int count) {
  for (int i = 0; i < count; ++i)
    if (!game.playSan(sans[i])) { std::printf("     could not play %s (ply %d)\n", sans[i], i); return false; }
  return true;
}

void testGameMiniature() {
  Game& game = g_guarded.game;
  game.newGame();
  CHECK(game.plyCount() == 0 && game.atLatest() && game.atStart() && game.lastMove().isNone(), "new game");
  CHECK(game.result() == GameResult::Ongoing && game.reason() == GameEndReason::None, "new game is ongoing");
  CHECK(game.startedFromStartpos(), "new game starts from the standard position");

  // Legal's mate.
  static const char* const kMoves[] = {"e4", "e5", "Nf3", "d6", "Bc4", "Bg4", "Nc3", "g6",
                                       "Nxe5", "Bxd1", "Bxf7+", "Ke7", "Nd5#"};
  constexpr int kCount = static_cast<int>(sizeof kMoves / sizeof kMoves[0]);
  for (int i = 0; i < kCount; ++i) {
    CHECK(game.result() == GameResult::Ongoing, "ongoing until the mate");
    CHECK(game.playSan(kMoves[i]), kMoves[i]);
    CHECK_STR(game.sanAt(i), kMoves[i], "SAN stored as generated");
  }
  CHECK(game.plyCount() == kCount && game.currentPly() == kCount, "thirteen plies");
  CHECK(game.result() == GameResult::WhiteWins && game.reason() == GameEndReason::Checkmate, "mate: White wins");
  CHECK(game.isOver(), "isOver");
  CHECK(game.position().isCheckmate(), "the position agrees");
  char uciMove[kUciBufferSize];
  game.lastMove().toUci(uciMove);
  CHECK_STR(uciMove, "c3d5", "last move");
  CHECK(!game.playSan("Kxf7") && game.lastError() == PlayError::IllegalMove, "nothing to play after mate");
  CHECK(!game.declareResult(GameResult::Draw, GameEndReason::Agreement), "no agreement after mate");
  CHECK(game.result() == GameResult::WhiteWins, "still mate");

  const CapturedList whiteLost = game.capturedPieces(Color::White);
  const CapturedList blackLost = game.capturedPieces(Color::Black);
  CHECK(whiteLost.count == 1 && whiteLost.types[0] == PieceType::Queen && whiteLost.material() == 9, "White lost the queen");
  CHECK(blackLost.count == 2 && blackLost.types[0] == PieceType::Pawn && blackLost.types[1] == PieceType::Pawn &&
            blackLost.material() == 2, "Black lost two pawns");

  char list[kUciMoveListBufferSize];
  CHECK(game.uciMoveList(list, sizeof list) == 13 * 5 - 1, "length of the UCI move list");
  CHECK_STR(list, "e2e4 e7e5 g1f3 d7d6 f1c4 c8g4 b1c3 g7g6 f3e5 g4d1 c4f7 e8e7 c3d5", "UCI move list");
  char tiny[64];
  std::memset(tiny, '~', sizeof tiny);
  CHECK(game.uciMoveList(tiny, 64) == -1 && tiny[0] == '\0', "too small: refused, never a partial list");
  CHECK(game.uciMoveList(tiny, 0) == -1 && game.uciMoveList(nullptr, 64) == -1, "degenerate buffers");
  char exact[13 * 5];
  CHECK(game.uciMoveList(exact, sizeof exact) == 13 * 5 - 1, "a buffer of exactly the right size is enough");

  static char pgn[kPgnBufferSize];
  PgnTags tags;
  tags.white = "De Legal \"Sire\"";
  tags.black = "Saint\\Brie";
  tags.date = "1750.??.??";
  const int length = game.toPgn(pgn, sizeof pgn, tags);
  CHECK(length > 0 && length == static_cast<int>(std::strlen(pgn)), "PGN written");
  const char* const kExpected =
      "[Event \"?\"]\n[Site \"?\"]\n[Date \"1750.??.??\"]\n[Round \"?\"]\n"
      "[White \"De Legal \\\"Sire\\\"\"]\n[Black \"Saint\\\\Brie\"]\n[Result \"1-0\"]\n\n"
      "1. e4 e5 2. Nf3 d6 3. Bc4 Bg4 4. Nc3 g6 5. Nxe5 Bxd1 6. Bxf7+ Ke7 7. Nd5# 1-0\n";
  CHECK_STR(pgn, kExpected, "PGN text");
  char smallPgn[100];
  std::memset(smallPgn, '~', sizeof smallPgn);
  CHECK(game.toPgn(smallPgn, 90, tags) == 0 && smallPgn[0] == '\0', "PGN buffer too small: refused, empty");
  CHECK(smallPgn[90] == '~' && smallPgn[99] == '~', "and nothing written past the size given");
  CHECK(game.toPgn(smallPgn, 0, tags) == 0 && game.toPgn(nullptr, 100, tags) == 0, "degenerate PGN buffers");

  // Browsing the finished game does not change its result, but the side column follows the board.
  CHECK(game.goToPly(9), "go to ply 9 (after 5. Nxe5)");
  CHECK(game.result() == GameResult::WhiteWins, "result is about the line, not the shown position");
  CHECK(game.capturedPieces(Color::White).count == 0 && game.capturedPieces(Color::Black).count == 1, "captures up to ply 9");
  CHECK(game.uciMoveList(list, sizeof list) == 9 * 5 - 1, "move list up to the shown position");
  game.goToLatest();
  CHECK(g_guarded.intact(), "guards around the Game");
}

void testGameBrowsing() {
  Game& game = g_guarded.game;
  game.newGame();
  CHECK(game.playUci("e2e4") && game.playUci("e7e5") && game.playUci("g1f3"), "three plies");
  const Position latest = game.position();
  CHECK(!game.stepForward(), "nothing after the latest position");
  CHECK(game.stepBack() && game.stepBack(), "two steps back");
  CHECK(game.currentPly() == 1 && game.plyCount() == 3 && !game.atLatest(), "browsing at ply 1");
  CHECK_STR(fenOf(game.position()), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "position after 1. e4");
  CHECK(game.lastMove() == Move(E2, E4), "the move to highlight is the one that led here");
  CHECK(game.stepBack() && !game.stepBack() && game.atStart(), "back to the start and no further");
  CHECK(game.position() == Position() && game.lastMove().isNone(), "start position shown");
  CHECK(game.stepForward() && game.stepForward() && game.stepForward() && !game.stepForward(), "forward again");
  CHECK(game.position() == latest, "browsing loses nothing");
  CHECK(!game.goToPly(4) && !game.goToPly(-1) && game.currentPly() == 3, "goToPly out of range");

  // A refused move while browsing truncates nothing.
  CHECK(game.goToPly(1), "to ply 1");
  CHECK(!game.playUci("e2e4") && game.lastError() == PlayError::IllegalMove, "illegal for Black");
  CHECK(!game.playSan("Qh5") && !game.play(Move::none()) && !game.play(Move(E7, E4)), "more illegal moves");
  CHECK(game.plyCount() == 3 && game.currentPly() == 1, "the line is intact");
  CHECK_STR(game.sanAt(2), "Nf3", "future still there");

  // A legal one does.
  CHECK(game.playSan("c5") && game.lastError() == PlayError::None, "1... c5 instead");
  CHECK(game.plyCount() == 2 && game.atLatest(), "old future dropped");
  CHECK_STR(game.sanAt(1), "c5", "new move recorded");
  CHECK_STR(game.sanAt(2), "", "nothing after it");
  CHECK(game.moveAt(2).isNone() && game.moveAt(-1).isNone() && game.moveAt(0) == Move(E2, E4), "moveAt bounds");
  CHECK(game.sideOfPly(0) == Color::White && game.sideOfPly(1) == Color::Black, "sideOfPly");
  CHECK(game.moveNumberOfPly(0) == 1 && game.moveNumberOfPly(1) == 1 && game.moveNumberOfPly(2) == 2, "moveNumberOfPly");
  char list[64];
  CHECK(game.uciMoveList(list, sizeof list) == 9, "two moves");
  CHECK_STR(list, "e2e4 c7c5", "UCI list after truncation");

  // Declared results.
  CHECK(!game.declareResult(GameResult::Ongoing, GameEndReason::Resignation), "Ongoing is not a result to declare");
  CHECK(!game.declareResult(GameResult::Draw, GameEndReason::Checkmate), "rule reasons cannot be declared");
  CHECK(!game.declareResult(GameResult::WhiteWins, GameEndReason::Agreement), "an agreed result is a draw");
  CHECK(game.declareResult(GameResult::BlackWins, GameEndReason::Resignation), "White resigns");
  CHECK(game.result() == GameResult::BlackWins && game.reason() == GameEndReason::Resignation, "declared result reported");
  CHECK(!game.playSan("Nf3") && game.lastError() == PlayError::GameOver && game.plyCount() == 2, "no moves after resignation");
  CHECK(game.stepBack(), "browse the resigned game");
  CHECK(game.result() == GameResult::BlackWins, "still resigned while browsing");
  CHECK(game.playSan("e5") && game.result() == GameResult::Ongoing, "a new line from an earlier position is a new game");
  CHECK(game.declareResult(GameResult::WhiteWins, GameEndReason::Timeout), "flag fall");
  CHECK(game.takeBack() && game.result() == GameResult::Ongoing && game.plyCount() == 1, "takeBack clears it");
  CHECK(game.takeBack() && !game.takeBack() && game.plyCount() == 0 && game.position() == Position(), "takeBack to the start");

  // takeBack while browsing works on the END of the line.
  CHECK(game.playSan("d4") && game.playSan("d5") && game.playSan("c4"), "three plies");
  game.goToStart();
  CHECK(game.takeBack() && game.plyCount() == 2 && game.atLatest(), "takeBack jumps to the latest position first");
  CHECK_STR(fenOf(game.position()), "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "after 1. d4 d5");

  // setFromFen: garbage changes nothing.
  CHECK(!game.setFromFen("not a fen") && !game.setFromFen(nullptr) && game.plyCount() == 2, "bad FEN refused, game untouched");
  CHECK(game.setFromFen(kStartposFen) && game.startedFromStartpos() && game.plyCount() == 0, "the standard FEN is the standard start");
  CHECK(g_guarded.intact(), "guards around the Game");
}

void testGameFromFen() {
  Game& game = g_guarded.game;
  // Black to move first, en passant on the way, PGN with SetUp/FEN and "N..." numbering.
  CHECK(game.setFromFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2"), "French-ish position");
  CHECK(!game.startedFromStartpos(), "not the standard start");
  CHECK(game.playSan("f5"), "2... f5");
  CHECK(game.position().enPassantSquare() == F6, "en passant available");
  CHECK(game.playSan("exf6"), "3. exf6");
  CHECK(game.lastMove().isEnPassant(), "recorded as en passant");
  const CapturedList lost = game.capturedPieces(Color::Black);
  CHECK(lost.count == 1 && lost.types[0] == PieceType::Pawn && game.capturedPieces(Color::White).count == 0,
        "the pawn taken en passant is in the list");
  CHECK(game.sideOfPly(0) == Color::Black && game.moveNumberOfPly(0) == 2 && game.moveNumberOfPly(1) == 3, "numbering");
  static char pgn[kPgnBufferSize];
  CHECK(game.toPgn(pgn, sizeof pgn) > 0, "PGN");
  CHECK(std::strstr(pgn, "[SetUp \"1\"]\n[FEN \"rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2\"]\n\n") != nullptr,
        "SetUp and FEN tags");
  CHECK(std::strstr(pgn, "\n2... f5 3. exf6 *\n") != nullptr, "movetext starting with Black");
  CHECK(game.stepBack() && game.stepBack() && game.atStart(), "back over the en passant");
  CHECK_STR(fenOf(game.position()), "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", "start restored");

  // Promotion, and a promoted piece being captured.
  CHECK(game.setFromFen("1n5k/P7/8/8/8/8/8/K7 w - - 0 1"), "promotion position");
  CHECK(game.playSan("axb8=Q+") && game.playSan("Kg7") && game.playSan("Qb2+") && game.playSan("Kg8"), "promote and go on");
  CHECK(game.capturedPieces(Color::Black).count == 1 && game.capturedPieces(Color::Black).types[0] == PieceType::Knight, "knight taken");
  CHECK_STR(game.sanAt(0), "axb8=Q+", "SAN of the promotion");
  char list[64];
  game.uciMoveList(list, sizeof list);
  CHECK_STR(list, "a7b8q h8g7 b8b2 g7g8", "UCI list with a promotion");
}

void testGameDraws() {
  Game& game = g_guarded.game;

  // Threefold: the starting position three times.
  game.newGame();
  static const char* const kShuffle[] = {"Nf3", "Nf6", "Ng1", "Ng8"};
  CHECK(playAll(game, kShuffle, 4), "first cycle");
  CHECK(game.repetitionCount() == 2 && game.result() == GameResult::Ongoing, "second occurrence: not yet");
  CHECK(playAll(game, kShuffle, 3), "most of the second cycle");
  CHECK(game.result() == GameResult::Ongoing, "one ply short");
  CHECK(game.playSan("Ng8"), "second cycle complete");
  CHECK(game.repetitionCount() == 3, "third occurrence");
  CHECK(game.result() == GameResult::Draw && game.reason() == GameEndReason::ThreefoldRepetition, "threefold");
  CHECK(game.playSan("e4") && game.result() == GameResult::Ongoing, "a claimable draw does not stop the game");
  CHECK(game.takeBack() && game.reason() == GameEndReason::ThreefoldRepetition, "takeBack finds the draw again");
  CHECK(game.stepBack() && game.repetitionCount() == 2, "repetitionCount is about the shown position (Ng1: second time)");
  game.goToStart();
  CHECK(game.repetitionCount() == 1, "the start has occurred once when nothing was played");

  // Same placement three times, but the first time with all four castling rights: not a repetition.
  CHECK(game.setFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), "rooks and kings");
  static const char* const kRooks[] = {"Rh2", "Ra7", "Rh1", "Ra8"};
  CHECK(playAll(game, kRooks, 4) && playAll(game, kRooks, 4), "two cycles");
  CHECK(game.position().samePositionAs(game.startPosition()) == false, "rights differ from the start");
  CHECK(game.repetitionCount() == 2, "placement seen three times, position only twice");
  CHECK(game.result() == GameResult::Ongoing, "no threefold when castling rights differ");
  CHECK(playAll(game, kRooks, 4), "third cycle");
  CHECK(game.repetitionCount() == 3 && game.reason() == GameEndReason::ThreefoldRepetition, "now it is");

  // A double push that allows a LEGAL en-passant capture is a different position from the same
  // placement later on...
  CHECK(game.setFromFen("4k3/8/8/8/3p4/8/4P3/4K1N1 w - - 0 1"), "pawn d4 waits for e2-e4");
  static const char* const kKnightDance[] = {"Kd8", "Nf3", "Ke8", "Ng1"};
  CHECK(game.playSan("e4") && game.position().enPassantSquare() == E3, "e4, dxe3 possible");
  CHECK(playAll(game, kKnightDance, 4) && playAll(game, kKnightDance, 4), "two cycles");
  CHECK(game.repetitionCount() == 2 && game.result() == GameResult::Ongoing, "the first time en passant was possible: only twice");
  CHECK(playAll(game, kKnightDance, 4) && game.reason() == GameEndReason::ThreefoldRepetition, "third real occurrence");
  // ... but a double push nobody can answer en passant is just the same position.
  CHECK(game.setFromFen("4k3/8/8/8/8/8/4P3/4K1N1 w - - 0 1"), "no black pawn");
  CHECK(game.playSan("e4") && game.position().enPassantSquare() == kNoSquare, "e4, nothing to capture with");
  CHECK(playAll(game, kKnightDance, 4) && playAll(game, kKnightDance, 4), "two cycles");
  CHECK(game.repetitionCount() == 3 && game.reason() == GameEndReason::ThreefoldRepetition, "threefold counts the double-push position");
  // ... and so is one where the capture exists on the board but is illegal (king exposed on the rank).
  CHECK(game.setFromFen("7k/2p5/8/KP5r/8/8/8/8 b - - 0 1"), "pinned en passant");
  static const char* const kKingDance[] = {"Ka4", "Kg8", "Ka5", "Kh8"};
  CHECK(game.playSan("c5") && playAll(game, kKingDance, 4) && playAll(game, kKingDance, 4), "c5 and two cycles");
  CHECK(game.repetitionCount() == 3 && game.reason() == GameEndReason::ThreefoldRepetition, "illegal en passant does not split positions");

  // Fifty moves.
  CHECK(game.setFromFen("4k3/8/8/8/8/8/P7/R3K3 w - - 99 80"), "clock at 99");
  CHECK(game.result() == GameResult::Ongoing, "99 is not 100");
  CHECK(game.playSan("Rb1"), "a quiet move");
  CHECK(game.result() == GameResult::Draw && game.reason() == GameEndReason::FiftyMove, "fifty-move draw");
  CHECK(game.takeBack() && game.playSan("a3") && game.result() == GameResult::Ongoing, "a pawn move resets the clock");
  CHECK(game.setFromFen("7k/8/6K1/8/8/8/8/R7 w - - 99 80") && game.playSan("Ra8#"), "mate on the hundredth ply");
  CHECK(game.result() == GameResult::WhiteWins && game.reason() == GameEndReason::Checkmate, "mate beats the fifty-move rule");

  // Stalemate and insufficient material, reached by a move and found in a FEN.
  CHECK(game.setFromFen("7k/8/6K1/8/8/8/8/5Q2 w - - 0 1") && game.playSan("Qf7"), "Qf7 stalemates");
  CHECK(game.result() == GameResult::Draw && game.reason() == GameEndReason::Stalemate, "stalemate");
  CHECK(game.setFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1") && game.reason() == GameEndReason::Stalemate, "stalemate straight from a FEN");
  CHECK(game.setFromFen("8/8/8/8/8/k7/8/Kr6 w - - 0 1") && game.playSan("Kxb1"), "KxR leaves bare kings");
  CHECK(game.result() == GameResult::Draw && game.reason() == GameEndReason::InsufficientMaterial, "insufficient material");
  CHECK(game.capturedPieces(Color::Black).count == 1 && game.capturedPieces(Color::Black).types[0] == PieceType::Rook, "the rook is in the list");
  CHECK(game.setFromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"), "mated position from a FEN");
  CHECK(game.result() == GameResult::BlackWins && game.reason() == GameEndReason::Checkmate, "Black has won");
  static char pgn[kPgnBufferSize];
  CHECK(game.toPgn(pgn, sizeof pgn) > 0 && std::strstr(pgn, "[Result \"0-1\"]") != nullptr && std::strstr(pgn, "\n\n0-1\n") != nullptr,
        "PGN of a game with no moves");
  CHECK(g_guarded.intact(), "guards around the Game");
}

void testHistoryLimit() {
  Game& game = g_guarded.game;
  game.newGame();
  static const char* const kShuffle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
  int played = 0;
  while (played < kMaxGamePlies && game.playUci(kShuffle[played % 4])) ++played;
  CHECK(played == kMaxGamePlies && game.plyCount() == kMaxGamePlies, "1024 plies fit");
  CHECK(game.isHistoryFull(), "history reported full");
  CHECK(!(game.position() == Position()), "clocks differ from a fresh start");
  CHECK(game.position().samePositionAs(Position()), "but the placement is the starting one");
  CHECK(game.position().halfmoveClock() == kMaxGamePlies, "halfmove clock counted every ply");

  const Position before = game.position();
  CHECK(!game.playUci("g1f3"), "ply 1025 is refused");
  CHECK(game.lastError() == PlayError::HistoryFull, "and the reason is reported");
  CHECK(!game.playSan("e4") && game.lastError() == PlayError::HistoryFull, "also through SAN");
  CHECK(!game.playUci("e2e5") && game.lastError() == PlayError::IllegalMove, "an illegal move is still just illegal");
  CHECK(game.plyCount() == kMaxGamePlies && game.currentPly() == kMaxGamePlies && game.position() == before, "nothing changed");
  CHECK(g_guarded.intact(), "nothing written past the history");

  static char list[kUciMoveListBufferSize];
  CHECK(game.uciMoveList(list, sizeof list) == kMaxGamePlies * 5 - 1, "full UCI list fits kUciMoveListBufferSize");
  CHECK(game.uciMoveList(list, kMaxGamePlies * 5 - 1) == -1 && list[0] == '\0', "one byte short: refused");
  static char pgn[kPgnBufferSize];
  const int length = game.toPgn(pgn, sizeof pgn);
  CHECK(length > 0, "full PGN fits kPgnBufferSize");
  int longestLine = 0, column = 0;
  for (int i = 0; i < length; ++i) {
    column = (pgn[i] == '\n') ? 0 : column + 1;
    if (column > longestLine) longestLine = column;
  }
  CHECK(longestLine <= 79, "PGN lines stay under 80 columns");
  for (int i = 0; i < length; ++i) if (pgn[i] == '\n') pgn[i] = ' ';  // undo the wrapping, wherever it fell
  CHECK(std::strstr(pgn, " 1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 ") != nullptr, "PGN starts with move 1");
  CHECK(length > 22 && std::strcmp(pgn + length - 22, " 512. Ng1 Ng8 1/2-1/2 ") == 0, "PGN ends with move 512 and the draw");

  // Browsing and branching still work at the limit.
  CHECK(game.stepBack() && game.currentPly() == kMaxGamePlies - 1, "step back from the last ply");
  CHECK(game.playUci("f6e4") && game.plyCount() == kMaxGamePlies && game.atLatest(), "replace the last ply");
  CHECK(!game.playUci("d2d3") && game.lastError() == PlayError::HistoryFull, "full again");
  CHECK(game.takeBack() && !game.isHistoryFull() && game.playUci("f6g8") && game.isHistoryFull(), "takeBack makes room for one");
  game.goToStart();
  CHECK(game.position() == Position(), "1024 steps back reach the exact start");
  game.goToLatest();
  CHECK(game.position() == before, "and 1024 steps forward the exact end");
  CHECK(g_guarded.intact(), "guards around the Game");
}

}  // namespace

int main() {
  struct Test { const char* name; void (*run)(); };
  static const Test kTests[] = {
      {"FEN", testFen},
      {"SAN generation", testSanGeneration},
      {"SAN and UCI parsing", testSanParsing},
      {"en passant", testEnPassant},
      {"castling", testCastling},
      {"make/unmake walks", testMakeUnmake},
      {"position status", testPositionStatus},
      {"game: miniature", testGameMiniature},
      {"game: browsing", testGameBrowsing},
      {"game: from FEN", testGameFromFen},
      {"game: draws", testGameDraws},
      {"game: history limit", testHistoryLimit},
  };
  for (const Test& test : kTests) {
    const int failuresBefore = g_failures;
    const int checksBefore = g_checks;
    test.run();
    std::printf("%s %-22s %5d checks\n", g_failures == failuresBefore ? "ok  " : "FAIL", test.name, g_checks - checksBefore);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
