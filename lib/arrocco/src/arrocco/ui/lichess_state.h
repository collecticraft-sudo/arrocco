// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — what the Lichess screens share: the account seam, the board of the online game
// with the client that keeps it equal to the server's, the choices for the next game, and the
// recent opponents.
//
// Whoever builds the app owns it and hands ChessApp a pointer (ChessApp::setLichess): the
// firmware puts it in PSRAM, because the Game alone is 25 KB and the client 9 KB more; the
// simulator keeps a static one. No pointer, no Lichess: the menu entry stays greyed.
//
// The online game has its own chess::Game on purpose. The offline game in ChessApp (and its copy
// in flash, the "Resume game") is left exactly as it was while the board plays on Lichess.
#pragma once
#include <cstdint>

#include "arrocco/chess/game.h"
#include "arrocco/lichess/account.h"
#include "arrocco/lichess/client.h"
#include "arrocco/lichess/transport.h"
#include "arrocco/ui/lichess_friends.h"

namespace arrocco {
class Platform;
}

namespace arrocco::ui {

// The clocks the Lichess screens offer: the offline picker's four, all of them 3+0 or slower,
// because the Board API refuses bullet (and the client refuses to ask for it).
enum class LichessClock : uint8_t { Blitz5, Rapid10, Rapid15Inc10, Classic30, Count };
struct LichessClockSpec {
  int limitSeconds;
  int incrementSeconds;
  const char* label;   // "10 + 0"
};
LichessClockSpec lichessClock(LichessClock clock);

enum class LichessColour : uint8_t { White, Black, Random };

// What the next game will be. Not kept in flash: every boot starts casual, at level 3, 10 + 0.
struct LichessSetup {
  int aiLevel = 3;                                   // Lichess Stockfish, 1..8
  LichessColour aiColour = LichessColour::White;
  LichessColour friendColour = LichessColour::Random;
  LichessClock clock = LichessClock::Rapid10;
  bool rated = false;                                // friends only: AI games are never rated
  char friendName[kMaxUsername + 1] = {};
};

// How the game on the board came about, for "Play again".
enum class LichessOrigin : uint8_t { None, Computer, Friend, Invitation, Found };

class LichessState {
 public:
  LichessState(lichess::Transport& transport, lichess::Account& account);

  lichess::Account& account;
  chess::Game game;                  // declared before the client, which keeps a reference to it
  lichess::LichessClient client;
  LichessSetup setup;
  RecentFriends friends;
  LichessOrigin origin = LichessOrigin::None;
  // Set by the game-over screen's "Play again": the Lichess screen starts another game like the
  // last one (the computer again, or a challenge to the same person) when it opens.
  bool playAgain = false;
  char lastOpponent[kMaxUsername + 1] = {};   // the human of the last game, "" after the computer

  // Every tick, from ChessApp::tick(): the client's poll(), and nothing that draws.
  void poll() { client.poll(); }

  // Starting games, with the choices in `setup`.
  bool playComputer();
  bool challengeFriend();            // setup.friendName
  bool acceptInvitation(const char* id);

  // A game is on (or finished and still on the board): what the menu note and the hub show.
  bool gameInProgress() const { return client.inGame(); }

  // The recent opponents, read from the platform's store the first time the screens open, and
  // written back after a change, at a moment the caller knows is not between a tap and its
  // refresh (onTick).
  void loadFriends(Platform& platform);
  void rememberOpponent(const char* name);
  void saveFriends(Platform& platform);

  // The challenges sent TO us (the client also lists the ones we sent), and whether the board can
  // play one: standard chess, and a clock the Board API takes (or none at all).
  int invitationCount() const;
  const lichess::ChallengeInfo* invitation(int index) const;
  static bool playable(const lichess::ChallengeInfo& challenge);

 private:
  bool friendsLoaded_ = false;
  bool friendsDirty_ = false;
};

}  // namespace arrocco::ui
