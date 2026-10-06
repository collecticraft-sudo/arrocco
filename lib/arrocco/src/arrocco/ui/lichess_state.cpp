// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the Lichess screens' shared state. See lichess_state.h.
#include "arrocco/ui/lichess_state.h"

#include <cstring>

#include "arrocco/platform.h"
#include "arrocco/ui/strings.h"

namespace arrocco::ui {

namespace {

bool same(const char* a, const char* b) { return a != nullptr && b != nullptr && std::strcmp(a, b) == 0; }

chess::Color colourOf(LichessColour c) { return c == LichessColour::Black ? chess::Color::Black : chess::Color::White; }

}  // namespace

LichessClockSpec lichessClock(LichessClock clock) {
  switch (clock) {
    case LichessClock::Blitz5:       return LichessClockSpec{300, 0, str::kLichessClock5};
    case LichessClock::Rapid15Inc10: return LichessClockSpec{900, 10, str::kLichessClock15};
    case LichessClock::Classic30:    return LichessClockSpec{1800, 0, str::kLichessClock30};
    default:                         return LichessClockSpec{600, 0, str::kLichessClock10};
  }
}

LichessState::LichessState(lichess::Transport& transport, lichess::Account& acct)
    : account(acct), client(transport, game) {}

bool LichessState::playComputer() {
  const LichessClockSpec clock = lichessClock(setup.clock);
  const bool random = setup.aiColour == LichessColour::Random;
  if (!client.startAiGame(setup.aiLevel, clock.limitSeconds, clock.incrementSeconds, colourOf(setup.aiColour),
                          random))
    return false;
  origin = LichessOrigin::Computer;
  return true;
}

bool LichessState::challengeFriend() {
  if (!validUsername(setup.friendName)) return false;
  const LichessClockSpec clock = lichessClock(setup.clock);
  const bool random = setup.friendColour == LichessColour::Random;
  if (!client.challengeUser(setup.friendName, setup.rated, clock.limitSeconds, clock.incrementSeconds,
                            colourOf(setup.friendColour), random))
    return false;
  origin = LichessOrigin::Friend;
  return true;
}

bool LichessState::acceptInvitation(const char* id) {
  if (!client.acceptChallenge(id)) return false;
  origin = LichessOrigin::Invitation;
  return true;
}

void LichessState::loadFriends(Platform& platform) {
  if (friendsLoaded_) return;
  friendsLoaded_ = true;
  uint8_t blob[kFriendsMaxBytes];
  const size_t size = platform.loadBlob(kFriendsKey, blob, sizeof blob);
  if (size == 0 || !friends.decode(blob, size)) friends.clear();
}

void LichessState::rememberOpponent(const char* name) {
  if (!validUsername(name)) return;
  if (friends.count() > 0 && same(friends.at(0), name)) return;   // already first, as spelt
  if (friends.add(name)) friendsDirty_ = true;
}

void LichessState::saveFriends(Platform& platform) {
  if (!friendsDirty_) return;
  uint8_t blob[kFriendsMaxBytes];
  const size_t size = friends.encode(blob, sizeof blob);
  // A failed write is tried again at the next call: the list is still marked as changed.
  if (size != 0 && platform.storeBlob(kFriendsKey, blob, size)) friendsDirty_ = false;
}

int LichessState::invitationCount() const {
  int n = 0;
  for (int i = 0; i < client.challengeCount(); ++i)
    if (!sameUsername(client.challengeAt(i).challenger, client.username())) ++n;
  return n;
}

const lichess::ChallengeInfo* LichessState::invitation(int index) const {
  for (int i = 0; i < client.challengeCount(); ++i) {
    const lichess::ChallengeInfo& c = client.challengeAt(i);
    if (sameUsername(c.challenger, client.username())) continue;
    if (index-- == 0) return &c;
  }
  return nullptr;
}

bool LichessState::playable(const lichess::ChallengeInfo& c) {
  // Standard chess, from the usual start or from a position: the rules know nothing else.
  if (c.variant[0] != '\0' && !same(c.variant, "standard") && !same(c.variant, "fromPosition")) return false;
  if (c.clockLimit < 0) return true;   // correspondence or unlimited: no clock to refuse
  // limit + 40 * increment under 180 s is bullet: the Board API will not play it.
  return c.clockLimit + 40 * (c.clockIncrement > 0 ? c.clockIncrement : 0) >= 180;
}

}  // namespace arrocco::ui
