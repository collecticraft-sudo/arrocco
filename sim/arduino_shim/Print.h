// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — Arduino's Print base class, the parent of Adafruit_GFX.
// Same overload set as the Arduino-ESP32 3.x core (minus Printable and struct tm),
// so core code like gfx.print(42) or gfx.printf("%02u", s) resolves to the same
// overload, and therefore the same characters, on the Mac and on the device.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "WString.h"
#include "pgmspace.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size);
  size_t write(const char* str) {
    return str ? write(reinterpret_cast<const uint8_t*>(str), strlen(str)) : 0;
  }
  size_t write(const char* buffer, size_t size) {
    return write(reinterpret_cast<const uint8_t*>(buffer), size);
  }

  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

  size_t print(const __FlashStringHelper* str);
  size_t print(const String& str);
  size_t print(const char str[]);
  size_t print(char c);
  size_t print(unsigned char value, int base = DEC);
  size_t print(int value, int base = DEC);
  size_t print(unsigned int value, int base = DEC);
  size_t print(long value, int base = DEC);
  size_t print(unsigned long value, int base = DEC);
  size_t print(long long value, int base = DEC);
  size_t print(unsigned long long value, int base = DEC);
  size_t print(double value, int digits = 2);

  size_t println(void);
  size_t println(const __FlashStringHelper* str);
  size_t println(const String& str);
  size_t println(const char str[]);
  size_t println(char c);
  size_t println(unsigned char value, int base = DEC);
  size_t println(int value, int base = DEC);
  size_t println(unsigned int value, int base = DEC);
  size_t println(long value, int base = DEC);
  size_t println(unsigned long value, int base = DEC);
  size_t println(long long value, int base = DEC);
  size_t println(unsigned long long value, int base = DEC);
  size_t println(double value, int digits = 2);

  virtual void flush() {}

  int getWriteError() const { return writeError_; }
  void clearWriteError() { writeError_ = 0; }

protected:
  void setWriteError(int err = 1) { writeError_ = err; }

private:
  size_t printNumber(unsigned long long value, uint8_t base);
  size_t printFloat(double value, uint8_t digits);

  int writeError_ = 0;
};
