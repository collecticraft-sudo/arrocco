#pragma once
// ---------------------------------------------------------------
// Pin map — Seeed XIAO ESP32-S3 (Plus) + Seeed ePaper Driver Board
// GPIO reali ESP32-S3; tra parentesi il nome D# del XIAO.
// ---------------------------------------------------------------

// e-Paper (cablato dalla driver board, nessun filo da fare)
#define EPD_RST   1   // D0
#define EPD_CS    2   // D1
#define EPD_BUSY  3   // D2
#define EPD_DC    4   // D3
#define EPD_SCK   7   // D8
#define EPD_MOSI  9   // D10
#define EPD_MISO  8   // D9 (non usato dal display)

// Touch GT911 (dal header del FTS02 al XIAO)
#define TP_SDA    5   // D4
#define TP_SCL    6   // D5
#define TP_INT    43  // D6
#define TP_RST    44  // D7
#define TP_I2C_ADDR 0x5D   // GT911 default; 0x14 se INT alto al reset

// Buzzer KY-006 (pin S). Nota: D9=GPIO8 è MISO SPI, libero perché il display non lo usa.
#define BUZZER_PIN 8  // D9

#define EPD_W 800
#define EPD_H 480
