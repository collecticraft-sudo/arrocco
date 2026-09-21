#include "board_ui.h"
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans12pt7b.h>

void BoardState::setStart() {
  const char* start = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
  memcpy(sq, start, 64);
}

void BoardUI::squareRect(int file, int rank, int& x, int& y) {
  x = BOARD_X + file * SQ;
  y = BOARD_Y + (7 - rank) * SQ;     // rank 8 in alto
}

bool BoardUI::hitTest(int16_t px, int16_t py, int& file, int& rank) {
  if (px < BOARD_X || px >= BOARD_X + 8 * SQ || py < BOARD_Y || py >= BOARD_Y + 8 * SQ) return false;
  file = (px - BOARD_X) / SQ;
  rank = 7 - (py - BOARD_Y) / SQ;
  return true;
}

// Casella scura = tratteggio leggero (non nero pieno) così i pezzi restano leggibili.
void BoardUI::paintSquare(const BoardState& b, int file, int rank, bool highlight) {
  int x, y; squareRect(file, rank, x, y);
  bool dark = ((file + rank) & 1) == 0;
  _d.fillRect(x, y, SQ, SQ, GxEPD_WHITE);
  if (dark) {
    for (int yy = y; yy < y + SQ; yy += 2)
      for (int xx = x + ((yy - y) & 2 ? 0 : 2); xx < x + SQ; xx += 4)
        _d.drawPixel(xx, yy, GxEPD_BLACK);
  }
  if (highlight) {
    _d.drawRect(x + 1, y + 1, SQ - 2, SQ - 2, GxEPD_BLACK);
    _d.drawRect(x + 2, y + 2, SQ - 4, SQ - 4, GxEPD_BLACK);
    _d.drawRect(x + 3, y + 3, SQ - 6, SQ - 6, GxEPD_BLACK);
  }
  char p = b.sq[rank * 8 + file];
  if (p != '.') paintPiece(x, y, p, dark);
}

// Fase 1: pezzi come lettera in un disco. Bianco = disco vuoto con lettera nera,
// Nero = disco pieno con lettera bianca. In fase 2 arrivano le bitmap dei pezzi.
void BoardUI::paintPiece(int x, int y, char piece, bool darkSq) {
  bool white = isupper(piece);
  int cx = x + SQ / 2, cy = y + SQ / 2, r = 22;
  _d.fillCircle(cx, cy, r + 2, GxEPD_WHITE);        // stacca dal tratteggio
  if (white) {
    _d.drawCircle(cx, cy, r, GxEPD_BLACK);
    _d.drawCircle(cx, cy, r - 1, GxEPD_BLACK);
  } else {
    _d.fillCircle(cx, cy, r, GxEPD_BLACK);
  }
  _d.setFont(&FreeSansBold24pt7b);
  _d.setTextColor(white ? GxEPD_BLACK : GxEPD_WHITE);
  char s[2] = { (char)toupper(piece), 0 };
  int16_t bx, by; uint16_t bw, bh;
  _d.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  _d.setCursor(cx - bw / 2 - bx, cy + bh / 2);
  _d.print(s);
}

void BoardUI::drawFull(const BoardState& b, const char* status) {
  _d.setFullWindow();
  _d.firstPage();
  do {
    _d.fillScreen(GxEPD_WHITE);
    for (int r = 0; r < 8; r++)
      for (int f = 0; f < 8; f++) paintSquare(b, f, r, false);
    _d.drawRect(BOARD_X, BOARD_Y, 8 * SQ, 8 * SQ, GxEPD_BLACK);
    // colonna laterale
    _d.setFont(&FreeSans12pt7b);
    _d.setTextColor(GxEPD_BLACK);
    _d.setCursor(SIDE_X + 16, 40);  _d.print("CollectiCraft");
    _d.setCursor(SIDE_X + 16, 66);  _d.print("e-ink chess  v0.1");
    _d.drawLine(SIDE_X + 16, 80, EPD_W - 16, 80, GxEPD_BLACK);
    _d.setCursor(SIDE_X + 16, 120); _d.print(status);
  } while (_d.nextPage());
}

void BoardUI::drawSquare(const BoardState& b, int file, int rank, bool highlight) {
  int x, y; squareRect(file, rank, x, y);
  _d.setPartialWindow(x, y, SQ, SQ);
  _d.firstPage();
  do { paintSquare(b, file, rank, highlight); } while (_d.nextPage());
}

void BoardUI::drawStatus(const char* status) {
  _d.setPartialWindow(SIDE_X, 96, EPD_W - SIDE_X, 40);
  _d.firstPage();
  do {
    _d.fillRect(SIDE_X, 96, EPD_W - SIDE_X, 40, GxEPD_WHITE);
    _d.setFont(&FreeSans12pt7b);
    _d.setTextColor(GxEPD_BLACK);
    _d.setCursor(SIDE_X + 16, 120); _d.print(status);
  } while (_d.nextPage());
}
