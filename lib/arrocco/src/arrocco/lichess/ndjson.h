// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — ndjson line assembly over a caller-provided buffer. No heap, no STL.
//
// Lichess streams are "one JSON object per line", with an EMPTY line every ~7 s as a keep-alive.
// Bytes arrive in whatever chunks TLS and the network feel like, so a line may be split anywhere
// and a chunk may hold several lines. Use it like this:
//
//   int n = transport.readStream(id, chunk, sizeof chunk);
//   for (int used = 0; used < n; ) {
//     used += reader.feed(chunk + used, n - used);
//     const char* line; int length;
//     while (reader.takeLine(line, length)) handle(line, length);   // length 0 = keep-alive
//   }
//
// feed() stops right after a newline so nothing is ever overwritten before it is taken.
#pragma once
#include <stdint.h>

namespace arrocco::lichess {

class NdjsonReader {
 public:
  // `buffer` must outlive the reader and hold `capacity` chars, terminating NUL included.
  NdjsonReader(char* buffer, int capacity) : buffer_(buffer), capacity_(capacity) { reset(); }

  void reset();

  // Copies bytes until a newline has been taken in, the buffer is full, or the input is used up.
  // Returns how many bytes were consumed — always call it in a loop, it may stop early.
  // Returns 0 when a completed line is waiting: take it first.
  int feed(const char* data, int size);

  bool hasLine() const { return hasLine_; }
  // The next complete line, without its CR/LF, NUL-terminated, valid until the next feed().
  // `length` is 0 for a keep-alive line.
  bool takeLine(const char*& line, int& length);

  // True when a line longer than the buffer had to be dropped (the rest of that line is skipped,
  // never half-parsed). Sticky until clearOverflow().
  bool overflow() const { return overflow_; }
  void clearOverflow() { overflow_ = false; }

 private:
  char* buffer_;
  int capacity_;
  int length_ = 0;
  bool hasLine_ = false;
  bool dropping_ = false;
  bool overflow_ = false;
};

}  // namespace arrocco::lichess
