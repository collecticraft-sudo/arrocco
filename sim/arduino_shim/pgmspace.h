// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — PROGMEM is a no-op on the Mac exactly as it is on the ESP32:
// flash-resident data is just const data in the normal address space.
#pragma once

#include <stdint.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif

// Opaque tag type Arduino uses to overload on "string stored in flash".
class __FlashStringHelper;
#ifndef F
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(PSTR(string_literal)))
#endif

// Adafruit_GFX.cpp only defines these when they are missing; giving them here keeps
// one definition for the library and for any core code that reads font tables.
// memcpy instead of a cast: the font tables are byte-packed, and an unaligned
// uint16_t load is undefined behaviour the host sanitizers would flag.
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#endif
#ifndef pgm_read_word
static inline uint16_t arrocco_shim_read_word(const void* addr) {
  uint16_t v;
  memcpy(&v, addr, sizeof v);
  return v;
}
#define pgm_read_word(addr) (arrocco_shim_read_word(addr))
#endif
#ifndef pgm_read_dword
static inline uint32_t arrocco_shim_read_dword(const void* addr) {
  uint32_t v;
  memcpy(&v, addr, sizeof v);
  return v;
}
#define pgm_read_dword(addr) (arrocco_shim_read_dword(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void* const*)(addr))
#endif
// pgm_read_pointer is NOT defined here: Adafruit_GFX.cpp defines it unconditionally
// (from pgm_read_dword, so 32 bits only) and uses it on AVR alone.

#ifndef memcpy_P
#define memcpy_P memcpy
#endif
#ifndef strlen_P
#define strlen_P strlen
#endif
#ifndef strcpy_P
#define strcpy_P strcpy
#endif
