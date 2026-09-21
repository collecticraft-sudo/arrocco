// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the smallest fake Arduino.h that lets the unmodified
// Adafruit GFX library (Adafruit_GFX.cpp, glcdfont.c, Fonts/*.h) compile natively
// on the Mac. It deliberately offers NO timing, GPIO or Serial: the portable core
// must reach the outside world only through arrocco::Platform, and a missing symbol
// here is the compiler telling us that rule was broken.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Adafruit_GFX.h tests "ARDUINO >= 100" BEFORE it includes this file, so the real
// definition lives on the compiler command line (see sim/Makefile). This is a fallback
// for anything that includes Arduino.h first.
#ifndef ARDUINO
#define ARDUINO 10819
#endif

#include "pgmspace.h"

typedef bool boolean;
typedef uint8_t byte;

// Angle helpers, with the exact constants of the Arduino-ESP32 core so that rotated
// drawing (Adafruit_GFX::drawRotatedBitmap uses radians()) rounds the same way.
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#ifndef radians
#define radians(deg) ((deg) * DEG_TO_RAD)
#endif
#ifndef degrees
#define degrees(rad) ((rad) * RAD_TO_DEG)
#endif

// Note: no min()/max()/abs() macros on purpose. Arduino defines them, but as macros
// they break the C++ standard headers the simulator host needs; Adafruit_GFX.cpp
// defines its own min() when it is missing.

#include "WString.h"
#include "Print.h"
