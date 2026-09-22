// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco puzzles — decoding the packed pack.  See puzzle_data.h for the byte layout.
#include "arrocco/puzzles/puzzles.h"

#include "arrocco/puzzles/puzzle_data.h"

namespace arrocco::puzzles {
namespace {

static_assert(data::kFormatVersion == 1, "this reader understands format 1 only");
static_assert(data::kIndexBytes == static_cast<uint32_t>(data::kCount) * data::kIndexStride,
              "index array and puzzle count disagree");

// A nibble is one of these, plus 12/13 for a pawn that has just double-pushed.
constexpr char kPieceChars[12] = {'P', 'N', 'B', 'R', 'Q', 'K', 'p', 'n', 'b', 'r', 'q', 'k'};
constexpr char kPromotionChars[5] = {'\0', 'n', 'b', 'r', 'q'};

struct Header {
  const uint8_t* nibbles;  // start of the nibble run
  const uint8_t* moves;    // start of the 2-byte move words
  uint64_t occupancy;
  uint8_t flags;
  uint8_t pieces;
  uint8_t moveCount;
};

bool indexValid(int index) { return index >= 0 && index < data::kCount; }

const uint8_t* record(int index) { return data::kIndex + index * data::kIndexStride; }

uint16_t ratingOf(const uint8_t* r) { return static_cast<uint16_t>(r[6] | (r[7] << 8)); }
ThemeId themeOf(const uint8_t* r) { return static_cast<ThemeId>(r[5] & 0x3F); }
bool mateOf(const uint8_t* r) { return (r[5] & 0x40) != 0; }

int popcount64(uint64_t bits) {
  int n = 0;
  for (; bits != 0; bits &= bits - 1) ++n;
  return n;
}

// Walks the index record and the blob header; false only if the data is corrupt.
bool readHeader(int index, Header& out) {
  if (!indexValid(index)) return false;
  const uint8_t* r = record(index);
  const uint32_t offset = static_cast<uint32_t>(r[9]) | (static_cast<uint32_t>(r[10]) << 8) |
                          (static_cast<uint32_t>(r[11]) << 16);
  if (offset + 9 > data::kBlobBytes) return false;
  const uint8_t* blob = data::kBlob + offset;
  uint64_t occupancy = 0;
  for (int i = 0; i < 8; ++i) occupancy |= static_cast<uint64_t>(blob[i]) << (8 * i);
  const int pieces = popcount64(occupancy);
  const int nibbleBytes = (pieces + 1) / 2;
  const uint8_t moveCount = r[8];
  if (offset + 8 + nibbleBytes + 1 + 2 * moveCount > data::kBlobBytes) return false;
  if (moveCount < 1 || moveCount > kMaxSolutionPlies + 1) return false;
  out.occupancy = occupancy;
  out.nibbles = blob + 8;
  out.flags = blob[8 + nibbleBytes];
  out.moves = blob + 8 + nibbleBytes + 1;
  out.pieces = static_cast<uint8_t>(pieces);
  out.moveCount = moveCount;
  return true;
}

uint8_t nibbleAt(const Header& header, int n) {
  const uint8_t byte = header.nibbles[n >> 1];
  return (n & 1) == 0 ? static_cast<uint8_t>(byte & 0x0F) : static_cast<uint8_t>(byte >> 4);
}

// Rebuilds the FEN of the packed position.  Going through text costs a few
// microseconds once per puzzle and buys every check Position::setFen() makes.
int writeFen(const Header& header, char* out, int outSize) {
  if (out == nullptr || outSize < chess::kFenBufferSize) return 0;

  char board[64];
  for (int i = 0; i < 64; ++i) board[i] = '\0';
  int epSquare = -1;
  int seen = 0;
  for (int square = 0; square < 64; ++square) {
    if (((header.occupancy >> square) & 1u) == 0) continue;
    const uint8_t code = nibbleAt(header, seen++);
    if (code < 12) {
      board[square] = kPieceChars[code];
    } else if (code == 12) {
      board[square] = 'P';
      epSquare = square - 8;
    } else if (code == 13) {
      board[square] = 'p';
      epSquare = square + 8;
    } else {
      return 0;  // 14/15 are not used by format 1
    }
  }
  if (seen != header.pieces) return 0;

  int n = 0;
  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const char piece = board[rank * 8 + file];
      if (piece == '\0') {
        ++empty;
        continue;
      }
      if (empty != 0) {
        out[n++] = static_cast<char>('0' + empty);
        empty = 0;
      }
      out[n++] = piece;
    }
    if (empty != 0) out[n++] = static_cast<char>('0' + empty);
    if (rank != 0) out[n++] = '/';
  }

  out[n++] = ' ';
  out[n++] = (header.flags & 1u) != 0 ? 'b' : 'w';
  out[n++] = ' ';
  const uint8_t castling = static_cast<uint8_t>((header.flags >> 1) & 0x0F);
  if (castling == 0) {
    out[n++] = '-';
  } else {
    if ((castling & chess::kWhiteKingside) != 0) out[n++] = 'K';
    if ((castling & chess::kWhiteQueenside) != 0) out[n++] = 'Q';
    if ((castling & chess::kBlackKingside) != 0) out[n++] = 'k';
    if ((castling & chess::kBlackQueenside) != 0) out[n++] = 'q';
  }
  out[n++] = ' ';
  if (epSquare >= 0 && epSquare < 64) {
    out[n++] = static_cast<char>('a' + (epSquare & 7));
    out[n++] = static_cast<char>('1' + (epSquare >> 3));
  } else {
    out[n++] = '-';
  }
  // Puzzles carry no clocks: a solution is never long enough for the 50-move rule.
  out[n++] = ' ';
  out[n++] = '0';
  out[n++] = ' ';
  out[n++] = '1';
  out[n] = '\0';
  return n;
}

// The k-th stored move, resolved against `position` (which supplies the kind:
// castling, en passant, promotion).  Move::none() if it is not legal there.
chess::Move decodeMove(const Header& header, int k, const chess::Position& position) {
  if (k < 0 || k >= header.moveCount) return chess::Move::none();
  const uint8_t* word = header.moves + 2 * k;
  const unsigned bits = static_cast<unsigned>(word[0]) | (static_cast<unsigned>(word[1]) << 8);
  const unsigned from = bits & 63u;
  const unsigned to = (bits >> 6) & 63u;
  const unsigned promotion = (bits >> 12) & 7u;
  if (promotion > 4) return chess::Move::none();
  char uci[chess::kUciBufferSize];
  uci[0] = static_cast<char>('a' + (from & 7u));
  uci[1] = static_cast<char>('1' + (from >> 3));
  uci[2] = static_cast<char>('a' + (to & 7u));
  uci[3] = static_cast<char>('1' + (to >> 3));
  uci[4] = promotion == 0 ? '\0' : kPromotionChars[promotion];
  uci[5] = '\0';
  return position.parseUci(uci);
}

bool loadSetup(const Header& header, chess::Position& out) {
  char fen[chess::kFenBufferSize];
  if (writeFen(header, fen, chess::kFenBufferSize) == 0) return false;
  return out.setFen(fen);
}

// Setup position + the blunder.
bool loadStart(const Header& header, chess::Position& out) {
  if (!loadSetup(header, out)) return false;
  const chess::Move blunder = decodeMove(header, 0, out);
  if (blunder.isNone()) return false;
  chess::Undo undo;
  out.make(blunder, undo);
  return true;
}

}  // namespace

// ------------------------------------------------------------------ the pack

int count() { return data::kCount; }
uint16_t formatVersion() { return data::kFormatVersion; }
uint32_t packBytes() { return data::kTotalBytes; }

bool byIndex(int index, Puzzle& out) {
  Header header;
  if (!readHeader(index, header)) return false;
  const uint8_t* r = record(index);
  for (int i = 0; i < 5; ++i) out.id[i] = static_cast<char>(r[i]);
  out.id[5] = '\0';
  out.rating = ratingOf(r);
  out.theme = themeOf(r);
  out.solutionPlies = static_cast<uint8_t>(header.moveCount - 1);
  out.endsInMate = mateOf(r);
  return true;
}

// ------------------------------------------------------------------ positions

bool setupPosition(int index, chess::Position& out) {
  Header header;
  return readHeader(index, header) && loadSetup(header, out);
}

chess::Move blunderMove(int index) {
  Header header;
  if (!readHeader(index, header)) return chess::Move::none();
  chess::Position position;
  if (!loadSetup(header, position)) return chess::Move::none();
  return decodeMove(header, 0, position);
}

bool startPosition(int index, chess::Position& out) {
  Header header;
  return readHeader(index, header) && loadStart(header, out);
}

int startFen(int index, char* out, int outSize) {
  chess::Position position;
  if (!startPosition(index, position)) return 0;
  return position.toFen(out, outSize);
}

int solutionMoves(int index, chess::Move* out, int outSize) {
  Header header;
  if (out == nullptr || !readHeader(index, header)) return 0;
  const int wanted = header.moveCount - 1;
  if (outSize < wanted) return 0;
  chess::Position position;
  if (!loadStart(header, position)) return 0;
  for (int k = 0; k < wanted; ++k) {
    const chess::Move move = decodeMove(header, k + 1, position);
    if (move.isNone()) return 0;
    out[k] = move;
    chess::Undo undo;
    position.make(move, undo);
  }
  return wanted;
}

// ------------------------------------------------------------------ selection

int lowerBoundByRating(uint16_t rating) {
  int low = 0;
  int high = data::kCount;
  while (low < high) {
    const int middle = low + (high - low) / 2;
    if (ratingOf(record(middle)) < rating) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

bool matches(const Filter& filter, int index) {
  if (!indexValid(index)) return false;
  const uint8_t* r = record(index);
  const uint16_t rating = ratingOf(r);
  if (rating < filter.minRating || rating > filter.maxRating) return false;
  if (filter.theme != ThemeId::Count && themeOf(r) != filter.theme) return false;
  if (filter.matesOnly && !mateOf(r)) return false;
  return true;
}

int next(const Filter& filter, int fromIndex) {
  int i = fromIndex < 0 ? 0 : fromIndex;
  // The pack is rating-sorted: skip straight to the band instead of scanning to it.
  const int lower = lowerBoundByRating(filter.minRating);
  if (i < lower) i = lower;
  for (; i < data::kCount; ++i) {
    if (ratingOf(record(i)) > filter.maxRating) return -1;
    if (matches(filter, i)) return i;
  }
  return -1;
}

int countMatching(const Filter& filter) {
  int n = 0;
  for (int i = next(filter, 0); i >= 0; i = next(filter, i + 1)) ++n;
  return n;
}

int nth(const Filter& filter, int n) {
  if (n < 0) return -1;
  for (int i = next(filter, 0); i >= 0; i = next(filter, i + 1)) {
    if (n-- == 0) return i;
  }
  return -1;
}

// ------------------------------------------------------------------ Cursor

bool Cursor::begin(int index) {
  Header header;
  if (!readHeader(index, header)) return false;
  Puzzle puzzle;
  if (!byIndex(index, puzzle)) return false;
  chess::Position position;
  if (!loadStart(header, position)) return false;

  const int wanted = header.moveCount - 1;
  chess::Position replay = position;
  for (int k = 0; k < wanted; ++k) {
    const chess::Move move = decodeMove(header, k + 1, replay);
    if (move.isNone()) return false;
    moves_[k] = move;
    chess::Undo undo;
    replay.make(move, undo);
  }

  index_ = index;
  puzzle_ = puzzle;
  start_ = position;
  position_ = position;
  total_ = wanted;
  ply_ = 0;
  solver_ = position.sideToMove();
  lastSolver_ = chess::Move::none();
  lastReply_ = chess::Move::none();
  return true;
}

void Cursor::restart() {
  if (index_ < 0) return;
  position_ = start_;
  ply_ = 0;
  lastSolver_ = chess::Move::none();
  lastReply_ = chess::Move::none();
}

chess::Move Cursor::expected() const {
  if (!solversTurn()) return chess::Move::none();
  return moves_[ply_];
}

bool Cursor::isCorrect(chess::Move m) const {
  const chess::Move want = expected();
  if (want.isNone() || m.isNone()) return false;
  if (m == want) return true;
  // Lichess accepts any mate when the solution mates: so do we.
  if (!position_.isLegal(m)) return false;
  chess::Position after = position_;
  chess::Undo undo;
  after.make(want, undo);
  if (!after.isCheckmate()) return false;
  after = position_;
  after.make(m, undo);
  return after.isCheckmate();
}

bool Cursor::play(chess::Move m) {
  if (!isCorrect(m)) return false;
  const bool alternative = m != moves_[ply_];
  chess::Undo undo;
  position_.make(m, undo);
  lastSolver_ = m;
  lastReply_ = chess::Move::none();
  if (alternative) {
    // A different mate ends the puzzle here; the stored continuation is moot.
    ply_ = total_;
    return true;
  }
  ++ply_;
  if (ply_ < total_) {
    const chess::Move reply = moves_[ply_];
    position_.make(reply, undo);
    lastReply_ = reply;
    ++ply_;
  }
  return true;
}

}  // namespace arrocco::puzzles
