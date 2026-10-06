// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — "Lichess" from the menu: everything up to the online game itself.
//
// One Screen with pages, so that the rest of the app sees a single ScreenId for all of it. Which
// page comes first depends on the board, never on a setting:
//
//   no Wi-Fi network stored  -> Wi-Fi: the setup network's name and a QR code that joins it, the
//                               portal opened (it closes by itself after 5 minutes)
//   a network, not online    -> joining it, with the reason when it fails
//   no Lichess account       -> Link: a QR code of the login, the token fallback in small print
//   linked                   -> the Lichess menu: play the computer, challenge a friend,
//                               invitations, rated on/off (casual by default), unlink
//
// and from the menu the pages that set a game up: the computer (level, colour, clock), a friend
// (a recent opponent or a name typed on the keyboard, clock, colour, rated), the invitations
// (accept or decline), and the wait for a friend to accept. A page change is one Full refresh;
// news on the same page (the login coming back, an invitation arriving) is one Partial. A game
// against the computer, or an invitation accepted, starts from its own page: "Starting the
// game..." under it (Partial), then the game (Deep), without a page in between to flash.
//
// Nothing here waits for the network: every page reads the account and the client on onTick()
// and repaints when what it shows has changed. When the game starts, the online game screen
// takes over (lichess_game_screen.h).
//
// Left alone for kIdleLeaveMs on any of these pages, with no game on, the screen goes back to the
// main menu by itself and ends the session (a challenge still out is withdrawn): the event stream
// would otherwise keep the radio up, and the board awake, for as long as nobody came back.
#pragma once
#include <cstdint>

#include "arrocco/lichess/account.h"
#include "arrocco/ui/lichess_friends.h"
#include "arrocco/ui/qr_code.h"
#include "arrocco/ui/screen.h"

namespace arrocco::ui {

class LichessState;

class LichessScreen final : public Screen {
 public:
  explicit LichessScreen(Context& ctx) : ctx_(ctx) {}
  void enter() override;
  void draw(Adafruit_GFX& gfx) override;
  Action onTap(int16_t x, int16_t y) override;
  Action onTick(uint32_t now) override;

  // Which page is on the glass, for the tests (the simulator's "lichess state" reports it).
  enum class Page : uint8_t { Wifi, WifiWait, Link, Hub, Computer, Friend, Names, Keyboard, Invitations, Waiting };
  Page page() const { return page_; }
  static const char* pageName(Page page);

  static constexpr uint32_t kIdleLeaveMs = 5UL * 60UL * 1000UL;

 private:
  LichessState& st() const;
  Page route() const;                 // where the network and the account say the user belongs
  void arrive(Page page);             // the page's own first steps (portal, login, connect)
  Action show(Page page);             // arrive() and one Full refresh
  Action leave();                     // back to the main menu
  void startLogin();
  void snapshot();                    // what the glass shows now, to tell a change later
  bool changed() const;
  uint32_t viewHash() const;          // everything the current page draws that can change by itself
  Action dispatchTap(int16_t x, int16_t y);

  void drawWifi(Adafruit_GFX& gfx);
  void drawWifiWait(Adafruit_GFX& gfx);
  void drawLink(Adafruit_GFX& gfx);
  void drawHub(Adafruit_GFX& gfx);
  void drawComputer(Adafruit_GFX& gfx);
  void drawFriend(Adafruit_GFX& gfx);
  void drawNames(Adafruit_GFX& gfx);
  void drawInvitations(Adafruit_GFX& gfx);
  void drawWaiting(Adafruit_GFX& gfx);

  Action tapWifi(int16_t x, int16_t y);
  Action tapWifiWait(int16_t x, int16_t y);
  Action tapLink(int16_t x, int16_t y);
  Action tapHub(int16_t x, int16_t y);
  Action tapComputer(int16_t x, int16_t y);
  Action tapFriend(int16_t x, int16_t y);
  Action tapNames(int16_t x, int16_t y);
  Action tapKeyboard(int16_t x, int16_t y);
  Action tapInvitations(int16_t x, int16_t y);
  Action tapWaiting(int16_t x, int16_t y);

  Action tickHub();
  Action tickWaiting();
  Action tickStarting();             // the computer page and the invitations, after a tap that starts a game
  void setMessage(const char* text);
  void playAgain();                   // the game-over screen's "Play again", on arrival

  Context& ctx_;
  Page page_ = Page::Hub;

  // What the glass shows, so that onTick() repaints only for a change.
  uint32_t shownHash_ = 0;
  bool shownPlaying_ = false;
  uint32_t lastNeedMs_ = 0;

  bool unlinkAsked_ = false;          // the "Unlink the account?" dialog is open
  bool starting_ = false;             // an AI game or an accepted invitation is on its way
  uint32_t lastTapMs_ = 0;            // for kIdleLeaveMs
  bool loginStarted_ = false;
  char loginUrl_[400] = {};
  QrCode qr_;
  char typed_[kMaxUsername + 1] = {};
  const char* keyboardMessage_ = nullptr;
  char message_[96] = {};             // one line of news for the page ("the challenge was declined")
};

}  // namespace arrocco::ui
