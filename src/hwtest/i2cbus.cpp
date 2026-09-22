// SPDX-License-Identifier: GPL-3.0-or-later
#include "i2cbus.h"

#include <Wire.h>
#include <driver/gpio.h>

#include "config.h"
#include "log.h"

namespace i2cbus {
namespace {

bool s_started = false;
bool s_swapped = false;
uint32_t s_errors = 0;
Scan s_scan = {false, false, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

void stop() {
  if (s_started) {
    Wire.end();
    s_started = false;
  }
}

bool fail() {
  ++s_errors;
  return false;
}

// Sends the register pointer, then reads len bytes. Never leaves garbage in buf.
bool readAfterPointer(uint8_t addr, const uint8_t* pointer, size_t pointerLen, uint8_t* buf,
                      size_t len) {
  memset(buf, 0, len);
  if (!s_started) return fail();
  Wire.beginTransmission(addr);
  Wire.write(pointer, pointerLen);
  if (Wire.endTransmission(true) != 0) return fail();
  const size_t got = Wire.requestFrom(static_cast<uint16_t>(addr), len, true);
  if (got != len) {
    while (Wire.available() > 0) Wire.read();
    return fail();
  }
  for (size_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

} // namespace

IdleLevels idleLevels() {
  stop();
  pinMode(cfg::kI2cSda, INPUT_PULLDOWN);
  pinMode(cfg::kI2cScl, INPUT_PULLDOWN);
  delay(2);
  const IdleLevels levels = {digitalRead(cfg::kI2cSda) == HIGH, digitalRead(cfg::kI2cScl) == HIGH};
  pinMode(cfg::kI2cSda, INPUT);
  pinMode(cfg::kI2cScl, INPUT);
  return levels;
}

bool start(bool swapped) {
  stop();
  const int sda = swapped ? cfg::kI2cScl : cfg::kI2cSda;
  const int scl = swapped ? cfg::kI2cSda : cfg::kI2cScl;
  s_started = Wire.begin(sda, scl, cfg::kI2cHz);
  s_swapped = swapped;
  if (s_started) Wire.setTimeOut(50);
  else logLine("I2C   Wire.begin(SDA=%d, SCL=%d) FAILED", sda, scl);
  return s_started;
}

bool isSwapped() { return s_swapped; }

// A line held LOW (short, or an unpowered touch board clamping it) makes every
// transaction time out, and the driver logs an error for each: never start one.
// gpio_get_level(): digitalRead() refuses pins that belong to the I2C peripheral.
bool linesIdle() {
  return s_started && gpio_get_level(static_cast<gpio_num_t>(cfg::kI2cSda)) == 1 &&
         gpio_get_level(static_cast<gpio_num_t>(cfg::kI2cScl)) == 1;
}

bool probe(uint8_t addr) {
  if (!linesIdle()) return false;
  Wire.beginTransmission(addr);
  return Wire.endTransmission(true) == 0;
}

bool read16(uint8_t addr, uint16_t reg, uint8_t* buf, size_t len) {
  const uint8_t pointer[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
  return readAfterPointer(addr, pointer, sizeof(pointer), buf, len);
}

bool read8(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
  return readAfterPointer(addr, &reg, 1, buf, len);
}

bool write16(uint8_t addr, uint16_t reg, uint8_t value) {
  if (!s_started) return fail();
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  if (Wire.endTransmission(true) != 0) return fail();
  return true;
}

uint32_t errors() { return s_errors; }

const Scan& scan() {
  s_scan = Scan{true, !linesIdle(), 0, {0, 0, 0, 0, 0, 0, 0, 0}};
  if (s_scan.stuck) {
    logLine("I2C   scan skipped: SDA or SCL is held LOW. Short circuit, or no 3.3 V on the touch "
            "board (check the 3V3 and GND wires)");
    return s_scan;
  }
  char list[48] = "";
  size_t used = 0;
  const uint32_t t0 = millis();
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (!probe(addr)) continue;
    if (s_scan.count < sizeof(s_scan.addr)) {
      s_scan.addr[s_scan.count] = addr;
      const int n = snprintf(list + used, sizeof(list) - used, " 0x%02X", addr);
      if (n > 0) used += static_cast<size_t>(n);
    }
    ++s_scan.count;
  }
  logLine("I2C   scan 0x08..0x77 (SDA/SCL %s): %u device(s)%s, %lu ms",
          s_swapped ? "SWAPPED" : "normal", s_scan.count, list,
          static_cast<unsigned long>(millis() - t0));
  if (s_scan.count == 0) logLine("I2C   bus is empty: check 3.3V, GND and the A4 (SDA) / A5 (SCL) wires");
  return s_scan;
}

const Scan& lastScan() { return s_scan; }

} // namespace i2cbus
