// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco chess rules — Game: history, browsing, results that depend on history, UCI move list, PGN.
#include "arrocco/chess/game.h"

namespace arrocco::chess {

namespace {

int textLength(const char* text) {
  int n = 0;
  while (text[n] != '\0') ++n;
  return n;
}

// Appends to a bounded buffer; remembers overflow instead of writing past the end.
struct TextWriter {
  char* out;
  int capacity;
  int length = 0;
  int column = 0;
  bool overflow = false;

  void put(char ch) {
    if (length + 1 < capacity) out[length++] = ch;
    else overflow = true;
    column = (ch == '\n') ? 0 : column + 1;
  }
  void putText(const char* text) {
    for (; *text != '\0'; ++text) put(*text);
  }
  // A movetext token: separated from the previous one by a space, or by a newline when the
  // line would grow past 79 characters (the PGN export format asks for lines under 80).
  void putToken(const char* text) {
    if (column > 0) put(column + 1 + textLength(text) > 79 ? '\n' : ' ');
    putText(text);
  }
  // [Name "value"] with the two escapes PGN knows; control characters become spaces.
  void putTag(const char* name, const char* value, const char* fallback) {
    if (value == nullptr || value[0] == '\0') value = fallback;
    put('[');
    putText(name);
    put(' ');
    put('"');
    for (; *value != '\0'; ++value) {
      const char ch = *value;
      if (ch == '"' || ch == '\\') put('\\');
      put(static_cast<unsigned char>(ch) < 0x20 ? ' ' : ch);
    }
    put('"');
    put(']');
    put('\n');
  }
};

// "12." or "12..." into `out` (at least 12 chars).
void formatMoveNumber(int number, bool blackToMove, char* out) {
  char digits[8];
  int n = 0;
  do { digits[n++] = static_cast<char>('0' + number % 10); number /= 10; } while (number > 0 && n < 8);
  int length = 0;
  while (n > 0) out[length++] = digits[--n];
  out[length++] = '.';
  if (blackToMove) { out[length++] = '.'; out[length++] = '.'; }
  out[length] = '\0';
}

}  // namespace

const char* pgnResultToken(GameResult result) {
  switch (result) {
    case GameResult::WhiteWins: return "1-0";
    case GameResult::BlackWins: return "0-1";
    case GameResult::Draw:      return "1/2-1/2";
    default:                    return "*";
  }
}

// ------------------------------------------------------------------ setup

void Game::reset(const Position& start, bool fromStartpos) {
  start_ = start;
  position_ = start;
  latestHash_ = start.hash();
  count_ = 0;
  cursor_ = 0;
  startedFromStartpos_ = fromStartpos;
  declaredResult_ = GameResult::Ongoing;
  declaredReason_ = GameEndReason::None;
  lastError_ = PlayError::None;
  updateRuleResult();
}

void Game::newGame() {
  const Position standard;
  reset(standard, true);
}

bool Game::setFromFen(const char* fen) {
  Position parsed;
  if (!parsed.setFen(fen)) return false;
  const Position standard;
  reset(parsed, parsed == standard);
  return true;
}

// ------------------------------------------------------------------ playing

bool Game::play(Move m) {
  if (!position_.isLegal(m)) {
    lastError_ = PlayError::IllegalMove;
    return false;
  }
  if (atLatest() && hasDeclaredResult()) {
    lastError_ = PlayError::GameOver;
    return false;
  }
  // cursor_ == kMaxGamePlies implies count_ == kMaxGamePlies: there is no future to drop that
  // would make room, so the move is refused before anything is touched.
  if (cursor_ >= kMaxGamePlies) {
    lastError_ = PlayError::HistoryFull;
    return false;
  }

  if (cursor_ < count_) {  // browsing: the line continues from here, the old future is gone
    count_ = cursor_;
    clearDeclaredResult();
  }
  Ply& ply = history_[cursor_];
  position_.toSan(m, ply.san);
  position_.make(m, ply.undo);
  ++cursor_;
  count_ = cursor_;
  latestHash_ = position_.hash();
  updateRuleResult();
  lastError_ = PlayError::None;
  return true;
}

bool Game::playUci(const char* text) {
  const Move m = position_.parseUci(text);
  if (m.isNone()) {
    lastError_ = PlayError::IllegalMove;
    return false;
  }
  return play(m);
}

bool Game::playSan(const char* text) {
  const Move m = position_.parseSan(text);
  if (m.isNone()) {
    lastError_ = PlayError::IllegalMove;
    return false;
  }
  return play(m);
}

bool Game::takeBack() {
  if (count_ == 0) return false;
  goToLatest();
  position_.unmake(history_[count_ - 1].undo);
  --count_;
  cursor_ = count_;
  latestHash_ = position_.hash();
  clearDeclaredResult();
  updateRuleResult();
  return true;
}

// ------------------------------------------------------------------ browsing

bool Game::stepBack() {
  if (cursor_ == 0) return false;
  --cursor_;
  position_.unmake(history_[cursor_].undo);
  return true;
}

bool Game::stepForward() {
  if (cursor_ >= count_) return false;
  // make() is deterministic: it rewrites the very Undo it is replaying.
  Undo& undo = history_[cursor_].undo;
  position_.make(undo.move, undo);
  ++cursor_;
  return true;
}

void Game::goToStart() {
  while (stepBack()) {}
}

void Game::goToLatest() {
  while (stepForward()) {}
}

bool Game::goToPly(int ply) {
  if (ply < 0 || ply > count_) return false;
  while (cursor_ > ply) stepBack();
  while (cursor_ < ply) stepForward();
  return true;
}

// ------------------------------------------------------------------ the line

Move Game::moveAt(int index) const {
  return (index >= 0 && index < count_) ? history_[index].undo.move : Move::none();
}

const char* Game::sanAt(int index) const {
  return (index >= 0 && index < count_) ? history_[index].san : "";
}

Color Game::sideOfPly(int index) const {
  const Color first = start_.sideToMove();
  return (index & 1) == 0 ? first : opposite(first);
}

int Game::moveNumberOfPly(int index) const {
  const int blackStarts = start_.sideToMove() == Color::Black ? 1 : 0;
  return start_.fullmoveNumber() + (index + blackStarts) / 2;
}

// ------------------------------------------------------------------ result

uint64_t Game::hashAfterPlies(int plies) const {
  // Each Undo carries the hash BEFORE its move, which is the hash after the plies that precede it.
  return plies < count_ ? history_[plies].undo.hash : latestHash_;
}

int Game::countRepetitions(int plies, int halfmoveClock) const {
  // Nothing before the last capture or pawn move can come back, and the same position needs the
  // same side to move: look every second ply, no further than the halfmove clock reaches.
  const uint64_t hash = hashAfterPlies(plies);
  int earliest = plies - halfmoveClock;
  if (earliest < 0) earliest = 0;
  int occurrences = 1;
  for (int k = plies - 2; k >= earliest; k -= 2)
    if (hashAfterPlies(k) == hash) ++occurrences;
  return occurrences;
}

int Game::repetitionCount() const { return countRepetitions(cursor_, position_.halfmoveClock()); }

void Game::updateRuleResult() {
  ruleResult_ = GameResult::Ongoing;
  ruleReason_ = GameEndReason::None;
  if (!position_.hasLegalMoves()) {
    if (position_.inCheck()) {
      ruleResult_ = position_.sideToMove() == Color::White ? GameResult::BlackWins : GameResult::WhiteWins;
      ruleReason_ = GameEndReason::Checkmate;
    } else {
      ruleResult_ = GameResult::Draw;
      ruleReason_ = GameEndReason::Stalemate;
    }
  } else if (position_.isInsufficientMaterial()) {
    ruleResult_ = GameResult::Draw;
    ruleReason_ = GameEndReason::InsufficientMaterial;
  } else if (countRepetitions(count_, position_.halfmoveClock()) >= 3) {
    ruleResult_ = GameResult::Draw;
    ruleReason_ = GameEndReason::ThreefoldRepetition;
  } else if (position_.isFiftyMoveDraw()) {
    ruleResult_ = GameResult::Draw;
    ruleReason_ = GameEndReason::FiftyMove;
  }
}

bool Game::declareResult(GameResult result, GameEndReason reason) {
  if (result == GameResult::Ongoing || endedByRule()) return false;
  if (reason != GameEndReason::Resignation && reason != GameEndReason::Timeout &&
      reason != GameEndReason::Agreement) {
    return false;
  }
  if (reason == GameEndReason::Agreement && result != GameResult::Draw) return false;
  if (reason == GameEndReason::Resignation && result == GameResult::Draw) return false;
  declaredResult_ = result;
  declaredReason_ = reason;
  return true;
}

void Game::clearDeclaredResult() {
  declaredResult_ = GameResult::Ongoing;
  declaredReason_ = GameEndReason::None;
}

// ------------------------------------------------------------------ for the UI and the engine

CapturedList Game::capturedPieces(Color c) const {
  int perType[7] = {0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < cursor_; ++i) {
    const Piece captured = history_[i].undo.captured;
    if (captured && captured.color() == c) ++perType[indexOf(captured.type())];
  }
  CapturedList list;
  static constexpr PieceType kOrder[] = {PieceType::Queen, PieceType::Rook, PieceType::Bishop,
                                         PieceType::Knight, PieceType::Pawn};
  for (const PieceType type : kOrder)
    for (int n = perType[indexOf(type)]; n > 0 && list.count < kMaxCapturedPieces; --n)
      list.types[list.count++] = type;
  return list;
}

int Game::uciMoveList(char* out, int outSize) const {
  if (out == nullptr || outSize < 1) return -1;
  TextWriter w{out, outSize};
  char uci[kUciBufferSize];
  for (int i = 0; i < cursor_; ++i) {
    if (i > 0) w.put(' ');
    history_[i].undo.move.toUci(uci);
    w.putText(uci);
  }
  if (w.overflow) {
    out[0] = '\0';
    return -1;
  }
  out[w.length] = '\0';
  return w.length;
}

int Game::toPgn(char* out, int outSize, const PgnTags& tags) const {
  if (out == nullptr || outSize < 1) return 0;
  TextWriter w{out, outSize};
  const char* resultToken = pgnResultToken(result());

  w.putTag("Event", tags.event, "?");
  w.putTag("Site", tags.site, "?");
  w.putTag("Date", tags.date, "????.??.??");
  w.putTag("Round", tags.round, "?");
  w.putTag("White", tags.white, "?");
  w.putTag("Black", tags.black, "?");
  w.putTag("Result", resultToken, "*");
  if (!startedFromStartpos_) {
    char fen[kFenBufferSize];
    start_.toFen(fen, kFenBufferSize);
    w.putTag("SetUp", "1", "1");
    w.putTag("FEN", fen, "?");
  }
  w.put('\n');

  char number[12];
  for (int i = 0; i < count_; ++i) {
    const bool blackToMove = sideOfPly(i) == Color::Black;
    if (!blackToMove || i == 0) {
      formatMoveNumber(moveNumberOfPly(i), blackToMove, number);
      w.putToken(number);
    }
    w.putToken(history_[i].san);
  }
  w.putToken(resultToken);
  w.put('\n');

  if (w.overflow) {
    out[0] = '\0';
    return 0;
  }
  out[w.length] = '\0';
  return w.length;
}

}  // namespace arrocco::chess
