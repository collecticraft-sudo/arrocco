// SPDX-License-Identifier: GPL-3.0-or-later
#include "arrocco/lichess/ndjson.h"

namespace arrocco::lichess {

void NdjsonReader::reset() {
  length_ = 0;
  hasLine_ = false;
  dropping_ = false;
  overflow_ = false;
  if (capacity_ > 0) buffer_[0] = '\0';
}

int NdjsonReader::feed(const char* data, int size) {
  if (hasLine_ || data == nullptr || size <= 0 || capacity_ < 2) return 0;
  int used = 0;
  while (used < size) {
    const char c = data[used++];
    if (dropping_) {
      if (c == '\n') dropping_ = false;  // the oversized line ends here; start clean
      continue;
    }
    if (c == '\n') {
      if (length_ > 0 && buffer_[length_ - 1] == '\r') --length_;
      buffer_[length_] = '\0';
      hasLine_ = true;
      return used;
    }
    if (length_ + 1 >= capacity_) {  // no room for this byte and the NUL
      overflow_ = true;
      dropping_ = true;
      length_ = 0;
      continue;
    }
    buffer_[length_++] = c;
  }
  return used;
}

bool NdjsonReader::takeLine(const char*& line, int& length) {
  if (!hasLine_) return false;
  line = buffer_;
  length = length_;
  length_ = 0;
  hasLine_ = false;
  return true;
}

}  // namespace arrocco::lichess
