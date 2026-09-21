#include "gt911.h"
#include "config.h"

#define GT_STATUS 0x814E
#define GT_POINT1 0x8150
#define GT_PID    0x8140

bool GT911::begin() {
  // Sequenza di reset: INT basso durante il reset -> indirizzo 0x5D
  pinMode(_rst, OUTPUT); pinMode(_int, OUTPUT);
  digitalWrite(_int, LOW); digitalWrite(_rst, LOW); delay(10);
  digitalWrite(_rst, HIGH); delay(60);
  pinMode(_int, INPUT);
  delay(50);
  uint8_t id[4] = {0};
  if (!readRegs(GT_PID, id, 4)) return false;
  Serial.printf("[GT911] product id: %c%c%c\n", id[0], id[1], id[2]);
  return id[0] == '9';
}

void GT911::writeReg(uint16_t reg, uint8_t v) {
  _w.beginTransmission(_addr); _w.write(reg >> 8); _w.write(reg & 0xFF); _w.write(v); _w.endTransmission();
}

bool GT911::readRegs(uint16_t reg, uint8_t* buf, size_t n) {
  _w.beginTransmission(_addr); _w.write(reg >> 8); _w.write(reg & 0xFF);
  if (_w.endTransmission(false) != 0) return false;
  if (_w.requestFrom((int)_addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = _w.read();
  return true;
}

bool GT911::read(TouchPoint& p) {
  uint8_t st = 0;
  if (!readRegs(GT_STATUS, &st, 1)) return false;
  if (!(st & 0x80)) { p.down = false; return false; }   // nessun dato nuovo
  uint8_t n = st & 0x0F;
  if (n > 0) {
    uint8_t d[6];
    readRegs(GT_POINT1, d, 6);
    int16_t x = d[0] | (d[1] << 8);
    int16_t y = d[2] | (d[3] << 8);
    switch (_rot) {   // il pannello touch è nativo 800x480 landscape; rotazioni per la scocca
      case 1: { int16_t t = x; x = EPD_W - 1 - y; y = t; break; }
      case 2: x = EPD_W - 1 - x; y = EPD_H - 1 - y; break;
      case 3: { int16_t t = x; x = y; y = EPD_H - 1 - t; break; }
    }
    p.x = x; p.y = y; p.down = true;
  } else {
    p.down = false;
  }
  writeReg(GT_STATUS, 0);   // ack buffer
  return true;
}

bool GT911::tapped(TouchPoint& p) {
  TouchPoint now = _last;
  if (read(now)) {
    if (now.down) { _last = now; _wasDown = true; }
    else if (_wasDown) { _wasDown = false; p = _last; return true; }
  }
  return false;
}
