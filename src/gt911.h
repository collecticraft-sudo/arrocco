#pragma once
#include <Arduino.h>
#include <Wire.h>

// Driver minimale GT911: solo lettura del primo punto di tocco.
struct TouchPoint { int16_t x = -1, y = -1; bool down = false; };

class GT911 {
public:
  GT911(TwoWire& w, uint8_t addr, int pinInt, int pinRst) : _w(w), _addr(addr), _int(pinInt), _rst(pinRst) {}
  bool begin();
  bool read(TouchPoint& p);           // true se lo stato è cambiato
  bool tapped(TouchPoint& p);         // true una sola volta al rilascio
  void setRotation(uint8_t r) { _rot = r; }
private:
  void writeReg(uint16_t reg, uint8_t v);
  bool readRegs(uint16_t reg, uint8_t* buf, size_t n);
  TwoWire& _w; uint8_t _addr; int _int, _rst; uint8_t _rot = 0;
  bool _wasDown = false; TouchPoint _last;
};
