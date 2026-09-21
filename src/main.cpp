// eink-chess — fase 1
// Obiettivo: display acceso, scacchiera disegnata, touch che sposta i pezzi (senza regole),
// partial refresh sulle sole caselle toccate. Serve a validare hardware e tempi di refresh.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "config.h"
#include "gt911.h"
#include "board_ui.h"

Epd display(GxEPD2_750_GDEY075T7(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
GT911 touch(Wire, TP_I2C_ADDR, TP_INT, TP_RST);
BoardUI ui(display);
BoardState board;

int selFile = -1, selRank = -1;
uint32_t partialCount = 0;
char status[48];

void beep(uint16_t freq, uint16_t ms) {
  tone(BUZZER_PIN, freq, ms);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[eink-chess] fase 1 boot");

  pinMode(BUZZER_PIN, OUTPUT);
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init(115200, true, 2, false);
  display.setRotation(0);

  Wire.begin(TP_SDA, TP_SCL, 400000);
  if (!touch.begin()) Serial.println("[eink-chess] GT911 non risponde: controlla fili e indirizzo (0x5D/0x14)");

  board.setStart();
  snprintf(status, sizeof status, "Tocca un pezzo");
  uint32_t t0 = millis();
  ui.drawFull(board, status);
  Serial.printf("[eink-chess] full refresh: %lu ms\n", millis() - t0);
  beep(880, 80);
}

void loop() {
  TouchPoint p;
  if (!touch.tapped(p)) { delay(10); return; }

  int f, r;
  if (!ui.hitTest(p.x, p.y, f, r)) return;
  Serial.printf("[touch] %d,%d -> %c%d\n", p.x, p.y, 'a' + f, r + 1);

  if (selFile < 0) {
    if (board.at(f, r) == '.') return;             // niente da selezionare
    selFile = f; selRank = r;
    ui.drawSquare(board, f, r, true);
    beep(1200, 30);
    return;
  }

  if (f == selFile && r == selRank) {               // deseleziona
    ui.drawSquare(board, f, r, false);
    selFile = selRank = -1;
    return;
  }

  // Mossa libera (fase 1: nessuna regola)
  uint32_t t0 = millis();
  board.at(f, r) = board.at(selFile, selRank);
  board.at(selFile, selRank) = '.';
  ui.drawSquare(board, selFile, selRank, false);
  ui.drawSquare(board, f, r, false);
  uint32_t dt = millis() - t0;
  snprintf(status, sizeof status, "%c%d-%c%d  (%lu ms)", 'a' + selFile, selRank + 1, 'a' + f, r + 1, dt);
  ui.drawStatus(status);
  selFile = selRank = -1;
  beep(660, 40);

  // Ogni 20 partial refresh, un full refresh per pulire il ghosting
  if (++partialCount % 20 == 0) ui.drawFull(board, status);
}
