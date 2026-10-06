// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — recent Lichess opponents. See lichess_friends.h.
#include "arrocco/ui/lichess_friends.h"

#include <cstring>

#include "arrocco/ui/saved_game.h"

namespace arrocco::ui {

namespace {

constexpr uint8_t kMagic[4] = {'A', 'R', 'L', 'F'};
constexpr uint8_t kVersion = 1;

bool usernameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

}  // namespace

bool validUsername(const char* name) {
  if (name == nullptr) return false;
  int n = 0;
  for (; name[n] != '\0'; ++n) {
    if (n >= kMaxUsername || !usernameChar(name[n])) return false;
  }
  return n >= kMinUsername;
}

bool sameUsername(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  for (;; ++a, ++b) {
    if (lower(*a) != lower(*b)) return false;
    if (*a == '\0') return true;
  }
}

const char* RecentFriends::at(int index) const {
  return index >= 0 && index < count_ ? names_[index] : "";
}

bool RecentFriends::add(const char* name) {
  if (!validUsername(name)) return false;
  int found = -1;
  for (int i = 0; i < count_; ++i) {
    if (sameUsername(names_[i], name)) {
      found = i;
      break;
    }
  }
  // Shift everything above the old copy (or the whole list, the oldest falling off) one down.
  int last = found >= 0 ? found : (count_ < kMaxFriends ? count_ : kMaxFriends - 1);
  for (int i = last; i > 0; --i) std::memcpy(names_[i], names_[i - 1], sizeof names_[i]);
  const size_t n = std::strlen(name);   // at most kMaxUsername: validUsername() said so
  std::memcpy(names_[0], name, n);
  names_[0][n] = '\0';
  if (found < 0 && count_ < kMaxFriends) ++count_;
  return true;
}

size_t RecentFriends::encode(uint8_t* out, size_t capacity) const {
  size_t size = 4 + 1 + 1 + 4;
  for (int i = 0; i < count_; ++i) size += 1 + std::strlen(names_[i]);
  if (out == nullptr || capacity < size) return 0;
  size_t at = 0;
  std::memcpy(out, kMagic, 4);
  at += 4;
  out[at++] = kVersion;
  out[at++] = static_cast<uint8_t>(count_);
  for (int i = 0; i < count_; ++i) {
    const size_t n = std::strlen(names_[i]);
    out[at++] = static_cast<uint8_t>(n);
    std::memcpy(out + at, names_[i], n);
    at += n;
  }
  const uint32_t crc = crc32(out, at);
  for (int b = 0; b < 4; ++b) out[at++] = static_cast<uint8_t>(crc >> (8 * b));
  return at;
}

bool RecentFriends::decode(const uint8_t* data, size_t size) {
  count_ = 0;
  if (data == nullptr || size < 4 + 1 + 1 + 4 || size > kFriendsMaxBytes) return false;
  if (std::memcmp(data, kMagic, 4) != 0 || data[4] != kVersion) return false;
  uint32_t stored = 0;
  for (int b = 0; b < 4; ++b) stored |= static_cast<uint32_t>(data[size - 4 + b]) << (8 * b);
  if (stored != crc32(data, size - 4)) return false;
  const int count = data[5];
  if (count > kMaxFriends) return false;
  size_t at = 6;
  char names[kMaxFriends][kMaxUsername + 1] = {};
  for (int i = 0; i < count; ++i) {
    if (at >= size - 4) return false;
    const size_t n = data[at++];
    if (n < static_cast<size_t>(kMinUsername) || n > static_cast<size_t>(kMaxUsername) || at + n > size - 4)
      return false;
    std::memcpy(names[i], data + at, n);
    names[i][n] = '\0';
    at += n;
    if (!validUsername(names[i])) return false;
  }
  if (at != size - 4) return false;   // nothing may be left over
  std::memcpy(names_, names, sizeof names_);
  count_ = count;
  return true;
}

}  // namespace arrocco::ui
