// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the people the board played on Lichess, most recent first, so that challenging a
// friend again is one tap instead of a name typed letter by letter on the glass.
//
// Kept in flash with Platform::storeBlob() under kFriendsKey, written only when the list changes
// (a game against somebody new, or an old name moving to the top): a handful of writes a month.
//
// Format version 1: "ARLF", version, count, then per name its length and its characters, then the
// CRC-32 of everything before it (ui::crc32, the saved game's). A blob that fails any check, or a
// name with a character Lichess would not allow, is not a list: the board starts with none.
#pragma once
#include <cstddef>
#include <cstdint>

namespace arrocco::ui {

constexpr char kFriendsKey[] = "li-friends";
constexpr int kMaxFriends = 4;               // the rows of the "Choose a friend" page
constexpr int kMaxUsername = 30;             // Lichess usernames are 2 to 30 characters
constexpr int kMinUsername = 2;
constexpr size_t kFriendsMaxBytes = 4 + 1 + 1 + kMaxFriends * (1 + kMaxUsername) + 4;

// Letters, digits, '-' and '_', 2 to 30 of them: what a Lichess username can be, and what the
// client will put in a path. (Lichess has a few more rules about where '-' and '_' may go; the
// server answers those with "No such user", which the screen shows.)
bool validUsername(const char* name);
// Lichess usernames do not care about case.
bool sameUsername(const char* a, const char* b);

class RecentFriends {
 public:
  int count() const { return count_; }
  const char* at(int index) const;          // "" out of range

  // Puts `name` first, dropping an older copy of it (any case) and, when full, the oldest name.
  // False (nothing changes) for an invalid name. True also when the list already began with it.
  bool add(const char* name);
  void clear() { count_ = 0; }

  size_t encode(uint8_t* out, size_t capacity) const;   // 0 when `capacity` is too small
  bool decode(const uint8_t* data, size_t size);        // false: the list is left empty

 private:
  char names_[kMaxFriends][kMaxUsername + 1] = {};
  int count_ = 0;
};

}  // namespace arrocco::ui
