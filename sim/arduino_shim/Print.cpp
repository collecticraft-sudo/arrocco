// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — Print implementation. Number formatting follows the Arduino
// core's algorithm (not printf) so that rounding and the "nan"/"inf"/"ovf" spellings
// match what the device prints.
#include "Print.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

size_t Print::write(const uint8_t* buffer, size_t size) {
  size_t written = 0;
  while (size--) {
    if (write(*buffer++) == 0) break;
    ++written;
  }
  return written;
}

size_t Print::printf(const char* format, ...) {
  // Same strategy as the ESP32 core: a small stack buffer, and the text is
  // truncated rather than heap-allocated if it does not fit.
  char text[256];
  va_list args;
  va_start(args, format);
  int needed = vsnprintf(text, sizeof text, format, args);
  va_end(args);
  if (needed < 0) return 0;
  size_t length = static_cast<size_t>(needed);
  if (length >= sizeof text) length = sizeof text - 1;
  return write(reinterpret_cast<const uint8_t*>(text), length);
}

size_t Print::print(const __FlashStringHelper* str) {
  return print(reinterpret_cast<const char*>(str));
}
size_t Print::print(const String& str) { return write(str.c_str(), str.length()); }
size_t Print::print(const char str[]) { return write(str); }
size_t Print::print(char c) { return write(static_cast<uint8_t>(c)); }
size_t Print::print(unsigned char value, int base) {
  return print(static_cast<unsigned long long>(value), base);
}
size_t Print::print(int value, int base) { return print(static_cast<long long>(value), base); }
size_t Print::print(unsigned int value, int base) {
  return print(static_cast<unsigned long long>(value), base);
}
size_t Print::print(long value, int base) { return print(static_cast<long long>(value), base); }
size_t Print::print(unsigned long value, int base) {
  return print(static_cast<unsigned long long>(value), base);
}

size_t Print::print(long long value, int base) {
  if (base == 0) return write(static_cast<uint8_t>(value));
  if (base == 10 && value < 0) {
    size_t sign = print('-');
    // Negate in unsigned arithmetic: -LLONG_MIN overflows a signed type.
    return sign + printNumber(0ULL - static_cast<unsigned long long>(value), 10);
  }
  return printNumber(static_cast<unsigned long long>(value), static_cast<uint8_t>(base));
}

size_t Print::print(unsigned long long value, int base) {
  if (base == 0) return write(static_cast<uint8_t>(value));
  return printNumber(value, static_cast<uint8_t>(base));
}

size_t Print::print(double value, int digits) {
  return printFloat(value, static_cast<uint8_t>(digits < 0 ? 0 : digits));
}

size_t Print::println(void) { return print("\r\n"); }
size_t Print::println(const __FlashStringHelper* str) { return print(str) + println(); }
size_t Print::println(const String& str) { return print(str) + println(); }
size_t Print::println(const char str[]) { return print(str) + println(); }
size_t Print::println(char c) { return print(c) + println(); }
size_t Print::println(unsigned char value, int base) { return print(value, base) + println(); }
size_t Print::println(int value, int base) { return print(value, base) + println(); }
size_t Print::println(unsigned int value, int base) { return print(value, base) + println(); }
size_t Print::println(long value, int base) { return print(value, base) + println(); }
size_t Print::println(unsigned long value, int base) { return print(value, base) + println(); }
size_t Print::println(long long value, int base) { return print(value, base) + println(); }
size_t Print::println(unsigned long long value, int base) {
  return print(value, base) + println();
}
size_t Print::println(double value, int digits) { return print(value, digits) + println(); }

size_t Print::printNumber(unsigned long long value, uint8_t base) {
  char text[8 * sizeof(value) + 1];  // worst case: every bit as a base-2 digit
  char* cursor = &text[sizeof text - 1];
  *cursor = '\0';
  if (base < 2) base = 10;
  do {
    const unsigned digit = static_cast<unsigned>(value % base);
    value /= base;
    *--cursor = static_cast<char>(digit < 10 ? '0' + digit : 'A' + digit - 10);
  } while (value);
  return write(cursor);
}

size_t Print::printFloat(double value, uint8_t digits) {
  if (isnan(value)) return print("nan");
  if (isinf(value)) return print("inf");
  if (value > 4294967040.0 || value < -4294967040.0) return print("ovf");

  size_t count = 0;
  if (value < 0.0) {
    count += print('-');
    value = -value;
  }
  // Round to the requested digit first so that 1.999 with 2 digits prints "2.00".
  double rounding = 0.5;
  for (uint8_t i = 0; i < digits; ++i) rounding /= 10.0;
  value += rounding;

  const unsigned long whole = static_cast<unsigned long>(value);
  double remainder = value - static_cast<double>(whole);
  count += print(whole);
  if (digits > 0) count += print('.');
  while (digits-- > 0) {
    remainder *= 10.0;
    const unsigned digit = static_cast<unsigned>(remainder);
    count += print(digit);
    remainder -= digit;
  }
  return count;
}
