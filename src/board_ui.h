#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "config.h"

// Layout: scacchiera 480x480 a sinistra (caselle 60 px), colonna laterale 320 px a destra.
#define SQ 60
#define BOARD_X 0
#define BOARD_Y 0
#define SIDE_X 480

using Epd = GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>;

// Stato scacchiera fase 1: array 64 char in notazione FEN-like ('.' vuota, 'P' bianco, 'p' nero)
struct BoardState {
  char sq[64];
  void setStart();
  char& at(int file, int rank) { return sq[rank * 8 + file]; }   // file 0..7 = a..h, rank 0..7 = 1..8
};

class BoardUI {
public:
  explicit BoardUI(Epd& d) : _d(d) {}
  void drawFull(const BoardState& b, const char* status);
  void drawSquare(const BoardState& b, int file, int rank, bool highlight);   // partial refresh 1 casella
  void drawStatus(const char* status);                                         // partial refresh colonna
  bool hitTest(int16_t x, int16_t y, int& file, int& rank);                   // pixel -> casella
private:
  void squareRect(int file, int rank, int& x, int& y);
  void paintSquare(const BoardState& b, int file, int rank, bool highlight);
  void paintPiece(int x, int y, char piece, bool darkSq);
  Epd& _d;
};
