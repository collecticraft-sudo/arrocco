// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only stand-in for the generated arrocco/puzzles/puzzle_data.h.
//
// The real pack is two `const` arrays in flash, which a test cannot corrupt and
// cannot fence.  This header hands puzzles.cpp the same names as plain pointers
// instead, so fuzz_main can put the data on a page of its own with an unreadable
// guard page beside it: a byte read outside the pack is then a SIGSEGV rather than
// a silent read of whatever happened to sit nearby.  It can also truncate the
// records, lie about the offsets and flip every bit.
//
// Only this one header is shadowed: the fuzz binary is compiled with -Ifuzz ahead
// of -I$(LIB), and "arrocco/puzzles/puzzle_data.h" is a nested path, so the
// sibling-directory rule inside puzzles.cpp does not find the real one first.  The
// reader under test is the real lib/arrocco/src/arrocco/puzzles/puzzles.cpp.
#pragma once
#include <cstdint>

namespace arrocco::puzzles::data {

constexpr uint16_t kFormatVersion = 1;
constexpr int kCount = 4;
constexpr int kIndexStride = 12;
constexpr uint32_t kIndexBytes = 48;   // 4 * 12
constexpr uint32_t kBlobBytes = 104;
constexpr uint32_t kTotalBytes = kIndexBytes + kBlobBytes;

// Fenced storage, installed by fuzz_main before anything reads it.
extern const uint8_t* kIndex;
extern const uint8_t* kBlob;

}  // namespace arrocco::puzzles::data
