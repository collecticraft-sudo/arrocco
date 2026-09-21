// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — a stub of Arduino's String, present only because Adafruit_GFX.h
// and Print.h mention the type in overloads. The portable core must not use String
// (it allocates on the heap), so this offers just enough to be constructed and read.
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

class String {
public:
  String(const char* cstr = "") { assign(cstr); }
  String(const String& other) { assign(other.buffer_); }
  String& operator=(const String& other) {
    if (this != &other) {
      free(buffer_);
      assign(other.buffer_);
    }
    return *this;
  }
  ~String() { free(buffer_); }

  size_t length() const { return length_; }
  const char* c_str() const { return buffer_ ? buffer_ : ""; }

private:
  void assign(const char* cstr) {
    if (!cstr) cstr = "";
    length_ = strlen(cstr);
    buffer_ = static_cast<char*>(malloc(length_ + 1));
    if (buffer_) {
      memcpy(buffer_, cstr, length_ + 1);
    } else {
      length_ = 0;
    }
  }

  char* buffer_ = nullptr;
  size_t length_ = 0;
};
