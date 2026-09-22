// SPDX-License-Identifier: GPL-3.0-or-later
// The one I2C bus (GT911 touch + optional MAX17048), with every return value checked.
#pragma once
#include <Arduino.h>

namespace i2cbus {

struct IdleLevels {
  bool sdaHigh;
  bool sclHigh;
};

struct Scan {
  bool done;
  bool stuck;       // SDA or SCL held LOW: the scan was not even tried
  uint8_t count;    // devices that answered
  uint8_t addr[8];  // the first eight of them
};

// Stops the bus and samples both lines against the internal pull-downs: a line
// that reads LOW has no working pull-up (missing wire, or no 3.3 V on the touch board).
IdleLevels idleLevels();

// (Re)starts the bus. swapped = SDA and SCL exchanged, used only to diagnose crossed wires.
bool start(bool swapped);
bool isSwapped();

// Both lines HIGH with the bus started. false = a line is held LOW: nothing can work.
bool linesIdle();

// Address-only transaction. A missing device is an answer, not an error.
// Not even attempted while a line is held LOW.
bool probe(uint8_t addr);

// Register access. On failure the buffer is left zeroed and the error counter goes up.
bool read16(uint8_t addr, uint16_t reg, uint8_t* buf, size_t len); // 16-bit register, high byte first
bool write16(uint8_t addr, uint16_t reg, uint8_t value);
bool read8(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len);   // 8-bit register

uint32_t errors();

// Probes 0x08..0x77, logs the result and keeps it for the screen.
const Scan& scan();
const Scan& lastScan();

} // namespace i2cbus
