// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco UI — the Lichess pages before the game. See lichess_screen.h.
#include "arrocco/ui/lichess_screen.h"

#include <cstring>

#include <Adafruit_GFX.h>

#include "arrocco/ui/lichess_page.h"
#include "arrocco/ui/lichess_state.h"
#include "arrocco/ui/name_keyboard.h"
#include "arrocco/ui/strings.h"
#include "arrocco/ui/text.h"

namespace arrocco::ui {

namespace {

using lichess::ClientState;
using lichess::LoginStatus;
using lichess::WifiStatus;
namespace page = lichess_page;

constexpr int16_t kCenterX = arrocco::kScreenW / 2;
constexpr uint32_t kNeedEveryMs = 1000;     // wantNetwork() while a page waits for the network

// Menu slots of the Lichess menu and of the setup pages.
enum HubSlot : int { kHubPlay = 0, kHubFriend, kHubInvites, kHubRated, kHubUnlink, kHubBack };
enum ComputerSlot : int { kCompLevel = 0, kCompColour, kCompClock, kCompStart, kCompBack = 5 };
enum FriendSlot : int { kFriendName = 0, kFriendClock, kFriendColour, kFriendRated, kFriendSend, kFriendBack };
enum WifiWaitSlot : int { kWaitRetry = 3, kWaitSetUp = 4, kWaitBack = 5 };
constexpr int kNamesType = 4;
constexpr int kNamesBack = 5;
constexpr int kSideSlotAgain = 4;   // code pages: "New code" / "Set up Wi-Fi"
constexpr int kSideSlotBack = 5;

uint32_t hashText(uint32_t h, const char* text) {
  if (text == nullptr) return h;
  for (; *text != '\0'; ++text) h = (h ^ static_cast<uint8_t>(*text)) * 16777619u;
  return (h ^ 0xFFu) * 16777619u;
}

uint32_t hashNumber(uint32_t h, uint32_t value) {
  for (int i = 0; i < 4; ++i) h = (h ^ ((value >> (8 * i)) & 0xFFu)) * 16777619u;
  return h;
}

void drawTitle(Adafruit_GFX& gfx, const char* title, const char* subtitle) {
  drawCentered(gfx, Font::Bold24, title, kCenterX, static_cast<int16_t>(kMenuTitleBaseline - 12));
  if (subtitle != nullptr) drawCentered(gfx, Font::Sans9, subtitle, kCenterX, kMenuSubtitleBaseline);
}

void drawFooter(Adafruit_GFX& gfx, const char* text) {
  if (text != nullptr && text[0] != '\0') drawCentered(gfx, Font::Sans9, text, kCenterX, kMenuFooterBaseline - 4);
}

const char* colourLabel(LichessColour c) {
  switch (c) {
    case LichessColour::White: return str::kComputerWhite;
    case LichessColour::Black: return str::kComputerBlack;
    default:                   return str::kComputerRandom;
  }
}

LichessColour nextColour(LichessColour c) {
  return static_cast<LichessColour>((static_cast<int>(c) + 1) % 3);
}

// "WIFI:T:nopass;S:<name>;;" is what phone cameras offer to join; ;,:\ and " need a backslash.
void wifiQrText(const char* ssid, char* out, int outSize) {
  int n = formatText(out, static_cast<size_t>(outSize), "WIFI:T:nopass;S:");
  for (const char* c = ssid; c != nullptr && *c != '\0' && n + 3 < outSize; ++c) {
    if (*c == ';' || *c == ',' || *c == ':' || *c == '\\' || *c == '"') out[n++] = '\\';
    out[n++] = *c;
  }
  if (n + 3 < outSize) {
    out[n++] = ';';
    out[n++] = ';';
  }
  out[n] = '\0';
}

// The side column of a code page, from the top: what the board should do next.
struct Column {
  Adafruit_GFX& gfx;
  int16_t y = page::kTextTop;
  void head(const char* text) { drawText(gfx, Font::Bold18, kSideInnerX, page::kHeadBaseline, text); }
  void para(Font font, const char* text) {
    const int16_t line = (font == Font::Sans9 || font == Font::Bold9) ? page::kLine9 : page::kLine12;
    y = drawWrapped(gfx, font, kSideInnerX, y, kSideInnerW, line, text, page::kTextBottom);
    y = static_cast<int16_t>(y + page::kParagraphGap);
  }
  void big(const char* text) {
    y = static_cast<int16_t>(y + 6);
    drawText(gfx, Font::Bold18, kSideInnerX, y, text);
    y = static_cast<int16_t>(y + 36);
  }
};

}  // namespace

LichessState& LichessScreen::st() const { return *ctx_.lichess; }

const char* LichessScreen::pageName(Page page) {
  switch (page) {
    case Page::Wifi:        return "wifi";
    case Page::WifiWait:    return "wifi-wait";
    case Page::Link:        return "link";
    case Page::Hub:         return "hub";
    case Page::Computer:    return "computer";
    case Page::Friend:      return "friend";
    case Page::Names:       return "names";
    case Page::Keyboard:    return "keyboard";
    case Page::Invitations: return "invitations";
    case Page::Waiting:     return "waiting";
  }
  return "?";
}

// ---- where the user belongs -------------------------------------------------------------------

LichessScreen::Page LichessScreen::route() const {
  LichessState& s = st();
  const ClientState cs = s.client.state();
  // A client that is up says the network and the account are fine: straight to the menu.
  if (cs != ClientState::Offline && cs != ClientState::Failed) return Page::Hub;
  const WifiStatus wifi = s.account.wifiStatus();
  if (wifi == WifiStatus::Portal || wifi == WifiStatus::NoNetwork) return Page::Wifi;
  if (wifi != WifiStatus::Online) return Page::WifiWait;
  if (!s.account.linked()) return Page::Link;
  return Page::Hub;
}

void LichessScreen::arrive(Page next) {
  LichessState& s = st();
  if (page_ == Page::Link && next != Page::Link && s.account.loginStatus() != LoginStatus::Done) {
    s.account.cancelLogin();
    loginStarted_ = false;
  }
  page_ = next;
  unlinkAsked_ = false;
  starting_ = false;
  switch (next) {
    case Page::Wifi: {
      if (s.account.wifiStatus() != WifiStatus::Portal) s.account.openPortal();
      char text[64];
      wifiQrText(s.account.portalName(), text, sizeof text);
      qr_.encode(text);
      break;
    }
    case Page::WifiWait:
      s.account.wantNetwork();
      lastNeedMs_ = ctx_.platform.millis();
      break;
    case Page::Link:
      qr_.clear();
      loginStarted_ = false;
      startLogin();
      break;
    case Page::Hub: {
      const ClientState cs = s.client.state();
      if (cs == ClientState::Offline || cs == ClientState::Failed) s.client.begin();
      break;
    }
    case Page::Keyboard:
      keyboardMessage_ = nullptr;
      break;
    default:
      break;
  }
  snapshot();
}

Action LichessScreen::show(Page next) {
  arrive(next);
  return Action::repaint(Refresh::Full);
}

Action LichessScreen::leave() {
  LichessState& s = st();
  if (page_ == Page::Wifi) s.account.closePortal();
  if (page_ == Page::Link) s.account.cancelLogin();
  loginStarted_ = false;
  // A game still running keeps the client (and its streams) alive behind the main menu; with
  // nothing on, the client goes, and with it the radio a couple of minutes later.
  if (!s.gameInProgress()) {
    if (s.client.state() == ClientState::Challenging) s.client.cancelOutgoing();
    s.client.end();
  }
  return Action::go(ScreenId::Menu);
}

void LichessScreen::startLogin() {
  LichessState& s = st();
  if (loginStarted_ || s.account.wifiStatus() != WifiStatus::Online) return;
  loginStarted_ = true;
  loginUrl_[0] = '\0';
  if (s.account.beginLogin(loginUrl_, sizeof loginUrl_)) {
    qr_.encode(loginUrl_);
  } else {
    qr_.clear();
  }
}

void LichessScreen::setMessage(const char* text) {
  formatText(message_, sizeof message_, "%s", text != nullptr ? text : "");
}

void LichessScreen::enter() {
  LichessState& s = st();
  lastTapMs_ = ctx_.platform.millis();
  s.loadFriends(ctx_.platform);
  message_[0] = '\0';
  page_ = Page::Hub;
  if (s.playAgain) {
    s.playAgain = false;
    playAgain();
    return;
  }
  arrive(route());
}

void LichessScreen::playAgain() {
  LichessState& s = st();
  if (s.origin == LichessOrigin::Computer) {
    arrive(Page::Computer);
    if (s.playComputer()) {
      starting_ = true;
      setMessage(str::kStarting);
    } else {
      setMessage(s.client.lastError());
    }
    snapshot();
    return;
  }
  if (s.lastOpponent[0] != '\0') {
    formatText(s.setup.friendName, sizeof s.setup.friendName, "%s", s.lastOpponent);
    if (s.challengeFriend()) {
      arrive(Page::Waiting);
    } else {
      arrive(Page::Friend);
      setMessage(s.client.lastError());
      snapshot();
    }
    return;
  }
  arrive(route());
}

// ---- change detection ---------------------------------------------------------------------------

// A page repaints by itself only when something it draws has changed: the client's revision
// moves for things no page shows (a challenge id, our own challenge on the event stream), and
// each of those would have cost a refresh of an identical screen.
uint32_t LichessScreen::viewHash() const {
  LichessState& s = st();
  const lichess::LichessClient& c = s.client;
  const ClientState cs = c.state();
  const bool ready = cs == ClientState::Idle || cs == ClientState::Finished;
  uint32_t h = 2166136261u;
  h = hashNumber(h, static_cast<uint32_t>(page_));
  h = hashText(h, message_);
  switch (page_) {
    case Page::Wifi:
    case Page::WifiWait:
      h = hashNumber(h, static_cast<uint32_t>(s.account.wifiStatus()));
      h = hashText(h, s.account.wifiDetail());
      break;
    case Page::Link:
      h = hashNumber(h, s.account.wifiStatus() == WifiStatus::Online ? 1u : 0u);
      h = hashNumber(h, static_cast<uint32_t>(s.account.loginStatus()));
      h = hashNumber(h, loginStarted_ ? 1u : 0u);
      h = hashText(h, s.account.loginError());
      break;
    case Page::Hub: {
      h = hashNumber(h, static_cast<uint32_t>(cs));
      h = hashText(h, c.username());
      h = hashNumber(h, static_cast<uint32_t>(c.lastHttpStatus()));
      if (cs == ClientState::Failed) h = hashText(h, c.lastError());
      const uint32_t wait = c.backoffRemainingMs();
      h = hashNumber(h, static_cast<uint32_t>(waitTensOfSeconds(wait)));
      h = hashNumber(h, static_cast<uint32_t>(s.invitationCount()));
      if (c.inGame()) {
        h = hashText(h, c.opponentName());
        h = hashNumber(h, static_cast<uint32_t>(s.game.moveNumberOfPly(s.game.plyCount())));   // the note's "move 12"
      }
      break;
    }
    case Page::Computer:
    case Page::Friend:
      h = hashNumber(h, (ready ? 1u : 0u) | (starting_ ? 2u : 0u));
      break;
    case Page::Invitations:
      h = hashNumber(h, (ready ? 1u : 0u) | (starting_ ? 2u : 0u));
      for (int i = 0; i < s.invitationCount(); ++i) {
        const lichess::ChallengeInfo* inv = s.invitation(i);
        if (inv != nullptr) h = hashText(h, inv->id);
      }
      break;
    default:
      break;
  }
  return h;
}

void LichessScreen::snapshot() {
  shownHash_ = viewHash();
  shownPlaying_ = st().client.inGame();
}

bool LichessScreen::changed() const { return viewHash() != shownHash_; }

// ---- ticks: the network and the account move on by themselves --------------------------------------

Action LichessScreen::onTick(uint32_t now) {
  LichessState& s = st();
  s.saveFriends(ctx_.platform);
  // Nobody here for a while and no game on: give the radio, and the battery, back.
  if (!s.gameInProgress() && !starting_ && now - lastTapMs_ >= kIdleLeaveMs) return leave();
  const WifiStatus wifi = s.account.wifiStatus();

  switch (page_) {
    case Page::Wifi:
      // The phone saved a network: the board is joining it. On to the next page.
      if (wifi == WifiStatus::Connecting || wifi == WifiStatus::Online || wifi == WifiStatus::Failed)
        return show(route());
      break;
    case Page::WifiWait:
      if (now - lastNeedMs_ >= kNeedEveryMs) {
        lastNeedMs_ = now;
        s.account.wantNetwork();
      }
      if (wifi == WifiStatus::Online || wifi == WifiStatus::Portal || wifi == WifiStatus::NoNetwork)
        return show(route());
      break;
    case Page::Link:
      // Linked by the phone, or by a token typed on the serial console meanwhile.
      if (s.account.linked()) return show(Page::Hub);
      if (!loginStarted_ && wifi == WifiStatus::Online) {
        startLogin();
        snapshot();
        return Action::repaint();
      }
      break;
    case Page::Hub:
      return tickHub();
    case Page::Waiting:
      return tickWaiting();
    case Page::Invitations:
    case Page::Computer:
      return tickStarting();
    case Page::Friend:
      // A game found on the account (started from the phone): the game takes the screen.
      if (!shownPlaying_ && s.client.inGame()) {
        s.origin = LichessOrigin::Found;
        return Action::go(ScreenId::LichessGame, Refresh::Deep);
      }
      break;
    default:
      break;
  }
  if (!changed()) return Action::none();
  snapshot();
  return Action::repaint();
}

Action LichessScreen::tickHub() {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  if (!shownPlaying_ && c.inGame()) {
    // A game started without this board asking (from the phone, or the replay after a reboot).
    s.origin = LichessOrigin::Found;
    return Action::go(ScreenId::LichessGame, Refresh::Deep);
  }
  // The game left running behind a menu is over: its result belongs on the glass, once.
  if (c.state() == ClientState::Finished && !s.resultShown) return Action::go(ScreenId::LichessGame, Refresh::Deep);
  // /api/account answered 429: the client waits the minute out, then this asks again.
  if (c.state() == ClientState::Connecting && !c.busy() && c.backoffRemainingMs() == 0) c.begin();
  if (!changed()) return Action::none();
  snapshot();
  return Action::repaint();
}

Action LichessScreen::tickStarting() {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  if (!shownPlaying_ && c.inGame()) {
    if (!starting_) s.origin = LichessOrigin::Found;   // not ours: found on the account
    return Action::go(ScreenId::LichessGame, Refresh::Deep);
  }
  if (starting_) {
    const bool accepting = s.origin == LichessOrigin::Invitation && c.outgoingChallengeId()[0] != '\0';
    if (c.state() != ClientState::Challenging && !c.busy() && !accepting) {
      // It did not start: say why, on the page it was asked from.
      starting_ = false;
      setMessage(c.lastError()[0] != '\0' ? c.lastError() : str::kNotStarted);
      snapshot();
      return Action::repaint();
    }
  }
  if (!changed()) return Action::none();
  snapshot();
  return Action::repaint();
}

Action LichessScreen::tickWaiting() {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  if (c.inGame()) return Action::go(ScreenId::LichessGame, Refresh::Deep);
  if (c.state() != ClientState::Challenging && !c.busy()) {
    // Declined, withdrawn, lost: the reason goes back to the friend's page.
    char why[96];
    formatText(why, sizeof why, "%s", c.lastError()[0] != '\0' ? c.lastError() : str::kNotStarted);
    arrive(Page::Friend);
    setMessage(why);
    snapshot();
    return Action::repaint(Refresh::Full);
  }
  if (!changed()) return Action::none();
  snapshot();
  return Action::repaint();
}

// ---- drawing --------------------------------------------------------------------------------------------

void LichessScreen::draw(Adafruit_GFX& gfx) {
  switch (page_) {
    case Page::Wifi:        drawWifi(gfx); break;
    case Page::WifiWait:    drawWifiWait(gfx); break;
    case Page::Link:        drawLink(gfx); break;
    case Page::Hub:         drawHub(gfx); break;
    case Page::Computer:    drawComputer(gfx); break;
    case Page::Friend:      drawFriend(gfx); break;
    case Page::Names:       drawNames(gfx); break;
    case Page::Keyboard:    keyboard::draw(gfx, str::kKeyboardTitle, typed_, keyboardMessage_); break;
    case Page::Invitations: drawInvitations(gfx); break;
    case Page::Waiting:     drawWaiting(gfx); break;
  }
}

void LichessScreen::drawWifi(Adafruit_GFX& gfx) {
  LichessState& s = st();
  const bool open = s.account.wifiStatus() == WifiStatus::Portal;
  if (open) drawQr(gfx, qr_, page::kQrBox, page::kQrMaxModulePx);
  Column col{gfx};
  col.head(str::kWifiTitle);
  col.para(Font::Sans12, str::kWifiStep1);
  col.big(s.account.portalName());
  col.para(Font::Sans12, str::kWifiStep2);
  char line[96];
  formatText(line, sizeof line, str::kWifiNoPageFmt, s.account.portalAddress());
  col.para(Font::Sans9, line);
  col.y = static_cast<int16_t>(col.y + 8);
  col.para(Font::Bold12, open ? str::kWifiWaitingPhone : s.account.wifiDetail());
  if (!open) drawButton(gfx, sideButtonRect(kSideSlotAgain), Font::Bold12, str::kWifiSetUp);
  drawButton(gfx, sideButtonRect(kSideSlotBack), Font::Bold12, str::kBack);
}

void LichessScreen::drawWifiWait(Adafruit_GFX& gfx) {
  LichessState& s = st();
  const bool failed = s.account.wifiStatus() == WifiStatus::Failed;
  drawTitle(gfx, str::kLichessTitle, nullptr);
  drawCentered(gfx, Font::Bold18, failed ? str::kWifiFailed : str::kWifiJoining, kCenterX, 176);
  drawCentered(gfx, Font::Sans12, s.account.wifiDetail(), kCenterX, 224);
  if (failed) drawButton(gfx, menuButtonRect(kWaitRetry), Font::Bold12, str::kWifiTryAgain);
  drawButton(gfx, menuButtonRect(kWaitSetUp), Font::Bold12, str::kWifiSetUp);
  drawButton(gfx, menuButtonRect(kWaitBack), Font::Bold12, str::kBack);
}

void LichessScreen::drawLink(Adafruit_GFX& gfx) {
  LichessState& s = st();
  const LoginStatus login = s.account.loginStatus();
  if (loginStarted_ && login == LoginStatus::Waiting) drawQr(gfx, qr_, page::kQrBox, page::kQrMaxModulePx);
  Column col{gfx};
  col.head(str::kLinkTitle);
  col.para(Font::Sans12, str::kLinkStep1);
  col.para(Font::Sans12, str::kLinkStep2);
  const char* status = str::kLinkStarting;
  if (loginStarted_) {
    switch (login) {
      case LoginStatus::Waiting:    status = str::kLinkWaiting; break;
      case LoginStatus::Exchanging: status = str::kLinkExchanging; break;
      case LoginStatus::Done:       status = str::kLinkExchanging; break;
      default:                      status = str::kLinkFailed; break;
    }
  } else if (s.account.wifiStatus() != WifiStatus::Online) {
    status = str::kWifiJoining;
  }
  col.para(Font::Bold12, status);
  const bool failed = loginStarted_ && (login == LoginStatus::Failed || login == LoginStatus::Idle);
  if (failed && s.account.loginError()[0] != '\0') col.para(Font::Sans9, s.account.loginError());
  if (loginStarted_ && login == LoginStatus::Waiting && std::strlen(loginUrl_) <= 40) {
    char line[96];
    formatText(line, sizeof line, str::kLinkOrOpenFmt, loginUrl_);
    col.para(Font::Sans9, line);
  }
  col.para(Font::Sans9, str::kLinkTokenHint);
  if (failed) drawButton(gfx, sideButtonRect(kSideSlotAgain), Font::Bold12, str::kLinkNewCode);
  drawButton(gfx, sideButtonRect(kSideSlotBack), Font::Bold12, str::kBack);
}

void LichessScreen::drawHub(Adafruit_GFX& gfx) {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  const ClientState cs = c.state();
  const bool ready = cs == ClientState::Idle || cs == ClientState::Finished;
  const bool failed = cs == ClientState::Failed;
  const bool refused = failed && c.lastHttpStatus() == 401;

  char subtitle[112];
  const uint32_t wait = c.backoffRemainingMs();
  if (refused) {
    formatText(subtitle, sizeof subtitle, "%s", str::kHubRefused);
  } else if (failed) {
    formatText(subtitle, sizeof subtitle, str::kHubOfflineFmt, c.lastError());
  } else if (wait > 0) {
    formatText(subtitle, sizeof subtitle, str::kHubWaitFmt, waitTensOfSeconds(wait));
  } else if (cs == ClientState::Connecting) {
    formatText(subtitle, sizeof subtitle, "%s", str::kHubConnecting);
  } else {
    formatText(subtitle, sizeof subtitle, str::kHubSignedInFmt, c.username());
  }
  drawTitle(gfx, str::kLichessTitle, subtitle);

  char note[64];
  if (c.inGame()) {
    formatText(note, sizeof note, str::kHubVsFmt, c.opponentName(), kNoteDot,
                  s.game.moveNumberOfPly(s.game.plyCount()));
    drawButton(gfx, menuButtonRect(kHubPlay), Font::Bold12, str::kHubBackToGame, true, note);
  } else if (failed) {
    drawButton(gfx, menuButtonRect(kHubPlay), Font::Bold12, refused ? str::kHubLinkAgain : str::kHubTryAgain);
  } else {
    formatText(note, sizeof note, str::kHubComputerNoteFmt, s.setup.aiLevel, kNoteDot,
                  lichessClock(s.setup.clock).label);
    drawButton(gfx, menuButtonRect(kHubPlay), Font::Bold12, str::kHubPlayComputer, ready, note);
  }
  drawButton(gfx, menuButtonRect(kHubFriend), Font::Bold12, str::kHubChallenge, ready,
             s.setup.rated ? str::kFriendRated : str::kFriendCasual);
  const int invites = s.invitationCount();
  char count[24];
  if (invites > 0) formatText(count, sizeof count, str::kHubInvitationsFmt, invites);
  drawButton(gfx, menuButtonRect(kHubInvites), Font::Bold12, str::kHubInvitations, ready,
             invites > 0 ? count : str::kHubInvitationsNone);
  drawButton(gfx, menuButtonRect(kHubRated), Font::Bold12, s.setup.rated ? str::kHubRatedOn : str::kHubRatedOff,
             true, str::kHubRatedNote);
  drawButton(gfx, menuButtonRect(kHubUnlink), Font::Bold12, str::kHubUnlink, !c.inGame(),
             c.username()[0] != '\0' ? c.username() : nullptr);
  drawButton(gfx, menuButtonRect(kHubBack), Font::Bold12, str::kBack);
  drawFooter(gfx, message_);

  if (unlinkAsked_) {
    Dialog d = newGameDialog(kCenterX);
    d.title = str::kUnlinkTitle;
    d.subtitle = str::kUnlinkSubtitle;
    d.labels[0] = str::kUnlinkYes;
    d.labels[1] = str::kCancel;
    drawDialog(gfx, d);
  }
}

void LichessScreen::drawComputer(Adafruit_GFX& gfx) {
  LichessState& s = st();
  drawTitle(gfx, str::kComputerTitle, str::kComputerHint);
  char line[48];
  formatText(line, sizeof line, str::kComputerLevelFmt, s.setup.aiLevel);
  drawButton(gfx, menuButtonRect(kCompLevel), Font::Bold12, line);
  drawButton(gfx, menuButtonRect(kCompColour), Font::Bold12, colourLabel(s.setup.aiColour));
  formatText(line, sizeof line, str::kClockFmt, lichessClock(s.setup.clock).label);
  drawButton(gfx, menuButtonRect(kCompClock), Font::Bold12, line);
  const ClientState cs = s.client.state();
  drawButton(gfx, menuButtonRect(kCompStart), Font::Bold12, str::kComputerStart,
             !starting_ && (cs == ClientState::Idle || cs == ClientState::Finished));
  drawButton(gfx, menuButtonRect(kCompBack), Font::Bold12, str::kBack);
  drawFooter(gfx, message_);
}

void LichessScreen::drawFriend(Adafruit_GFX& gfx) {
  LichessState& s = st();
  drawTitle(gfx, str::kFriendTitle, str::kFriendHint);
  char line[64];
  const bool named = validUsername(s.setup.friendName);
  if (named) {
    char name[40];
    fitText(gfx, Font::Bold12, s.setup.friendName, 300, name, sizeof name);
    formatText(line, sizeof line, str::kFriendNameFmt, name);
  }
  drawButton(gfx, menuButtonRect(kFriendName), Font::Bold12, named ? line : str::kFriendChoose);
  char clock[32];
  formatText(clock, sizeof clock, str::kClockFmt, lichessClock(s.setup.clock).label);
  drawButton(gfx, menuButtonRect(kFriendClock), Font::Bold12, clock);
  drawButton(gfx, menuButtonRect(kFriendColour), Font::Bold12, colourLabel(s.setup.friendColour));
  drawButton(gfx, menuButtonRect(kFriendRated), Font::Bold12, s.setup.rated ? str::kFriendRated : str::kFriendCasual);
  const ClientState cs = s.client.state();
  drawButton(gfx, menuButtonRect(kFriendSend), Font::Bold12, str::kFriendSend,
             named && (cs == ClientState::Idle || cs == ClientState::Finished));
  drawButton(gfx, menuButtonRect(kFriendBack), Font::Bold12, str::kBack);
  drawFooter(gfx, message_);
}

void LichessScreen::drawNames(Adafruit_GFX& gfx) {
  LichessState& s = st();
  drawTitle(gfx, str::kNamesTitle, s.friends.count() > 0 ? str::kNamesHint : str::kNamesNone);
  for (int i = 0; i < s.friends.count() && i < kNamesType; ++i)
    drawButton(gfx, menuButtonRect(i), Font::Bold12, s.friends.at(i));
  drawButton(gfx, menuButtonRect(kNamesType), Font::Bold12, str::kNamesType);
  drawButton(gfx, menuButtonRect(kNamesBack), Font::Bold12, str::kBack);
}

void LichessScreen::drawInvitations(Adafruit_GFX& gfx) {
  LichessState& s = st();
  drawTitle(gfx, str::kInvitesTitle, str::kInvitesHint);
  const int count = s.invitationCount();
  if (count == 0) drawCentered(gfx, Font::Sans12, str::kInvitesNone, kCenterX, 200);
  for (int row = 0; row < count && row < page::kInviteRows; ++row) {
    const lichess::ChallengeInfo* c = s.invitation(row);
    if (c == nullptr) break;
    const int16_t y = static_cast<int16_t>(page::kInviteY0 + row * page::kInviteStep);
    char name[40];
    fitText(gfx, Font::Bold12, c->challenger, static_cast<int16_t>(page::kInviteAcceptX - page::kInviteLabelX - 16),
            name, sizeof name);
    drawText(gfx, Font::Bold12, page::kInviteLabelX, static_cast<int16_t>(y + 20), name);
    char detail[64];
    const bool playable = LichessState::playable(*c);
    if (!playable) {
      formatText(detail, sizeof detail, "%s", str::kInvitesNotHere);
    } else if (c->clockLimit >= 0) {
      formatText(detail, sizeof detail, "%d + %d, %s", static_cast<int>(c->clockLimit / 60),
                    static_cast<int>(c->clockIncrement > 0 ? c->clockIncrement : 0),
                    c->rated ? str::kInvitesRated : str::kInvitesCasual);
    } else if (c->days > 0) {
      char days[24];
      formatText(days, sizeof days, str::kInvitesDaysFmt, static_cast<int>(c->days));
      formatText(detail, sizeof detail, "%s, %s", days, c->rated ? str::kInvitesRated : str::kInvitesCasual);
    } else {
      formatText(detail, sizeof detail, "%s, %s", str::kInvitesNoClock,
                    c->rated ? str::kInvitesRated : str::kInvitesCasual);
    }
    drawText(gfx, Font::Sans9, page::kInviteLabelX, static_cast<int16_t>(y + 42), detail);
    const ClientState cs = s.client.state();
    const bool free = !starting_ && (cs == ClientState::Idle || cs == ClientState::Finished);
    drawButton(gfx, page::inviteAcceptRect(row), Font::Bold12, str::kInvitesAccept, playable && free);
    drawButton(gfx, page::inviteDeclineRect(row), Font::Bold12, str::kInvitesDecline);
  }
  drawButton(gfx, menuButtonRect(5), Font::Bold12, str::kBack);
  drawFooter(gfx, message_);
}

// Only a challenge to a friend waits here: the friend may take a while to answer.
void LichessScreen::drawWaiting(Adafruit_GFX& gfx) {
  LichessState& s = st();
  drawTitle(gfx, str::kWaitingAskTitle, nullptr);
  char line[80];
  formatText(line, sizeof line, str::kWaitingForFmt, s.setup.friendName);
  drawCentered(gfx, Font::Bold18, line, kCenterX, 200);
  drawButton(gfx, menuButtonRect(5), Font::Bold12, str::kWaitingCancel);
}

// ---- taps ---------------------------------------------------------------------------------------------------

// Whatever a tap repaints is what the glass then shows: the next tick compares against that.
Action LichessScreen::onTap(int16_t x, int16_t y) {
  lastTapMs_ = ctx_.platform.millis();
  const Action action = dispatchTap(x, y);
  if (action.kind == Action::Kind::Repaint) snapshot();
  return action;
}

Action LichessScreen::dispatchTap(int16_t x, int16_t y) {
  switch (page_) {
    case Page::Wifi:        return tapWifi(x, y);
    case Page::WifiWait:    return tapWifiWait(x, y);
    case Page::Link:        return tapLink(x, y);
    case Page::Hub:         return tapHub(x, y);
    case Page::Computer:    return tapComputer(x, y);
    case Page::Friend:      return tapFriend(x, y);
    case Page::Names:       return tapNames(x, y);
    case Page::Keyboard:    return tapKeyboard(x, y);
    case Page::Invitations: return tapInvitations(x, y);
    case Page::Waiting:     return tapWaiting(x, y);
  }
  return Action::none();
}

Action LichessScreen::tapWifi(int16_t x, int16_t y) {
  const int slot = sideButtonAt(x, y);
  if (slot == kSideSlotBack) return leave();
  if (slot == kSideSlotAgain && st().account.wifiStatus() != WifiStatus::Portal) return show(Page::Wifi);
  return Action::none();
}

Action LichessScreen::tapWifiWait(int16_t x, int16_t y) {
  LichessState& s = st();
  switch (menuButtonAt(x, y)) {
    case kWaitRetry:
      if (s.account.wifiStatus() != WifiStatus::Failed) return Action::none();
      s.account.wantNetwork();
      return Action::none();   // the next status change repaints
    case kWaitSetUp: return show(Page::Wifi);
    case kWaitBack:  return leave();
    default:         return Action::none();
  }
}

Action LichessScreen::tapLink(int16_t x, int16_t y) {
  LichessState& s = st();
  const int slot = sideButtonAt(x, y);
  if (slot == kSideSlotBack) return leave();
  if (slot == kSideSlotAgain) {
    const LoginStatus login = s.account.loginStatus();
    if (!loginStarted_ || (login != LoginStatus::Failed && login != LoginStatus::Idle)) return Action::none();
    s.account.cancelLogin();
    loginStarted_ = false;
    startLogin();
    snapshot();
    return Action::repaint(Refresh::Full);   // a new code: no ghost of the old one
  }
  return Action::none();
}

Action LichessScreen::tapHub(int16_t x, int16_t y) {
  LichessState& s = st();
  lichess::LichessClient& c = s.client;
  if (unlinkAsked_) {
    Dialog d = newGameDialog(kCenterX);
    const int button = dialogButtonAt(d, x, y);
    unlinkAsked_ = false;
    if (button == 0) {
      c.end();
      s.account.unlink();
      return show(route());
    }
    return Action::repaint();
  }
  const ClientState cs = c.state();
  const bool ready = cs == ClientState::Idle || cs == ClientState::Finished;
  message_[0] = '\0';
  switch (menuButtonAt(x, y)) {
    case kHubPlay:
      if (c.inGame()) return Action::go(ScreenId::LichessGame, Refresh::Full);
      if (cs == ClientState::Failed) {
        if (c.lastHttpStatus() == 401) {
          c.end();
          s.account.unlink();
        }
        return show(route());   // the Wi-Fi first if it is down, then the client again
      }
      return ready ? show(Page::Computer) : Action::none();
    case kHubFriend:
      return ready ? show(Page::Friend) : Action::none();
    case kHubInvites:
      return ready ? show(Page::Invitations) : Action::none();
    case kHubRated:
      s.setup.rated = !s.setup.rated;
      return Action::repaint();
    case kHubUnlink:
      if (c.inGame()) return Action::none();
      unlinkAsked_ = true;
      return Action::repaint();
    case kHubBack:
      return leave();
    default:
      return Action::none();
  }
}

Action LichessScreen::tapComputer(int16_t x, int16_t y) {
  LichessState& s = st();
  if (starting_ && menuButtonAt(x, y) != kCompBack) return Action::none();
  switch (menuButtonAt(x, y)) {
    case kCompLevel:
      s.setup.aiLevel = s.setup.aiLevel >= 8 ? 1 : s.setup.aiLevel + 1;
      return Action::repaint();
    case kCompColour:
      s.setup.aiColour = nextColour(s.setup.aiColour);
      return Action::repaint();
    case kCompClock:
      s.setup.clock = static_cast<LichessClock>((static_cast<int>(s.setup.clock) + 1) %
                                                static_cast<int>(LichessClock::Count));
      return Action::repaint();
    case kCompStart: {
      const ClientState cs = s.client.state();
      if (starting_ || (cs != ClientState::Idle && cs != ClientState::Finished)) return Action::none();
      if (!s.playComputer()) {
        setMessage(s.client.lastError());
        return Action::repaint();
      }
      starting_ = true;
      setMessage(str::kStarting);
      snapshot();
      return Action::repaint();
    }
    case kCompBack:
      message_[0] = '\0';
      return show(Page::Hub);
    default:
      return Action::none();
  }
}

Action LichessScreen::tapFriend(int16_t x, int16_t y) {
  LichessState& s = st();
  switch (menuButtonAt(x, y)) {
    case kFriendName:
      message_[0] = '\0';
      return show(Page::Names);
    case kFriendClock:
      s.setup.clock = static_cast<LichessClock>((static_cast<int>(s.setup.clock) + 1) %
                                                static_cast<int>(LichessClock::Count));
      return Action::repaint();
    case kFriendColour:
      s.setup.friendColour = nextColour(s.setup.friendColour);
      return Action::repaint();
    case kFriendRated:
      s.setup.rated = !s.setup.rated;
      return Action::repaint();
    case kFriendSend: {
      const ClientState cs = s.client.state();
      if (!validUsername(s.setup.friendName) || (cs != ClientState::Idle && cs != ClientState::Finished))
        return Action::none();
      if (!s.challengeFriend()) {
        setMessage(s.client.lastError());
        return Action::repaint();
      }
      message_[0] = '\0';
      return show(Page::Waiting);
    }
    case kFriendBack:
      message_[0] = '\0';
      return show(Page::Hub);
    default:
      return Action::none();
  }
}

Action LichessScreen::tapNames(int16_t x, int16_t y) {
  LichessState& s = st();
  const int slot = menuButtonAt(x, y);
  if (slot >= 0 && slot < kNamesType && slot < s.friends.count()) {
    formatText(s.setup.friendName, sizeof s.setup.friendName, "%s", s.friends.at(slot));
    return show(Page::Friend);
  }
  if (slot == kNamesType) {
    formatText(typed_, sizeof typed_, "%s", s.setup.friendName);
    return show(Page::Keyboard);
  }
  if (slot == kNamesBack) return show(Page::Friend);
  return Action::none();
}

Action LichessScreen::tapKeyboard(int16_t x, int16_t y) {
  LichessState& s = st();
  const keyboard::Hit hit = keyboard::hitAt(x, y);
  const size_t length = std::strlen(typed_);
  switch (hit.key) {
    case keyboard::Key::Char:
      if (length >= static_cast<size_t>(kMaxUsername)) return Action::none();
      typed_[length] = hit.ch;
      typed_[length + 1] = '\0';
      keyboardMessage_ = nullptr;
      return Action::repaint();
    case keyboard::Key::Delete:
      if (length == 0) return Action::none();
      typed_[length - 1] = '\0';
      keyboardMessage_ = nullptr;
      return Action::repaint();
    case keyboard::Key::Clear:
      if (length == 0) return Action::none();
      typed_[0] = '\0';
      keyboardMessage_ = nullptr;
      return Action::repaint();
    case keyboard::Key::Cancel:
      return show(Page::Friend);
    case keyboard::Key::Done:
      if (!validUsername(typed_)) {
        keyboardMessage_ = str::kKeyboardTooShort;
        return Action::repaint();
      }
      formatText(s.setup.friendName, sizeof s.setup.friendName, "%s", typed_);
      return show(Page::Friend);
    case keyboard::Key::None:
      return Action::none();
  }
  return Action::none();
}

Action LichessScreen::tapInvitations(int16_t x, int16_t y) {
  LichessState& s = st();
  if (menuButtonAt(x, y) == 5) {
    message_[0] = '\0';
    return show(Page::Hub);
  }
  for (int row = 0; row < page::kInviteRows; ++row) {
    const lichess::ChallengeInfo* c = s.invitation(row);
    if (c == nullptr) break;
    char id[lichess::kChallengeIdSize];
    formatText(id, sizeof id, "%s", c->id);
    if (page::inviteDeclineRect(row).contains(x, y)) {
      if (!s.client.declineChallenge(id)) setMessage(s.client.lastError());
      else message_[0] = '\0';
      snapshot();
      return Action::repaint();
    }
    if (page::inviteAcceptRect(row).contains(x, y)) {
      const ClientState cs = s.client.state();
      if (starting_ || !LichessState::playable(*c) || (cs != ClientState::Idle && cs != ClientState::Finished))
        return Action::none();
      formatText(s.lastOpponent, sizeof s.lastOpponent, "%s", c->challenger);
      if (!s.acceptInvitation(id)) {
        setMessage(s.client.lastError());
        return Action::repaint();
      }
      starting_ = true;
      setMessage(str::kStarting);
      snapshot();
      return Action::repaint();
    }
  }
  return Action::none();
}

Action LichessScreen::tapWaiting(int16_t x, int16_t y) {
  LichessState& s = st();
  if (menuButtonAt(x, y) != 5) return Action::none();
  if (s.client.state() == ClientState::Challenging) s.client.cancelOutgoing();
  return show(Page::Friend);
}

}  // namespace arrocco::ui
