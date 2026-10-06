// SPDX-License-Identifier: GPL-3.0-or-later
#include "lichess_link.h"

#include "log.h"
#include "net_token.h"
#include "net_wifi.h"

using arrocco::lichess::LoginStatus;
using arrocco::lichess::WifiStatus;

namespace {

char s_longUrl[400] = {};   // oauthBegin() writes the authorize URL here; the QR shows /login

} // namespace

WifiStatus DeviceAccount::wifiStatus() {
  if (net::wifiPortalOpen()) return WifiStatus::Portal;
  if (!net::wifiHasCredentials()) return WifiStatus::NoNetwork;
  switch (net::wifiState()) {
    case net::WifiState::Off: return WifiStatus::Off;
    case net::WifiState::Connecting: return WifiStatus::Connecting;
    case net::WifiState::Online: return WifiStatus::Online;
    case net::WifiState::Failed: return WifiStatus::Failed;
    case net::WifiState::Scanning:
    case net::WifiState::Portal: return WifiStatus::Portal;
  }
  return WifiStatus::Off;
}

const char* DeviceAccount::wifiDetail() { return net::wifiDetail(); }

const char* DeviceAccount::portalName() { return net::wifiApSsid(); }

// net_wifi.cpp's kApIp: the portal answers there, and every name resolves to it.
const char* DeviceAccount::portalAddress() { return "http://4.3.2.1"; }

void DeviceAccount::openPortal() { net::wifiOpenPortal(); }

void DeviceAccount::closePortal() {
  if (net::wifiPortalOpen()) net::wifiRadioOff("left the Wi-Fi setup screen");
}

void DeviceAccount::wantNetwork() { net::wifiNeed(); }

// The station state machine retries by itself, with pauses of 3 to 30 s: "Try again" starts a
// fresh attempt at once instead, the radio off and on again.
void DeviceAccount::retryNetwork() {
  if (net::wifiState() == net::WifiState::Failed) net::wifiRadioOff("try again, from the screen");
  net::wifiNeed();
}

bool DeviceAccount::linked() { return net::tokenPresent(); }

bool DeviceAccount::beginLogin(char* url, int urlSize) {
  if (!url || urlSize <= 0) return false;
  url[0] = '\0';
  if (!net::oauthBegin(s_longUrl, sizeof(s_longUrl))) return false;
  if (!net::oauthLoginLink(url, static_cast<size_t>(urlSize))) {
    net::oauthCancel();
    return false;
  }
  logLine("LICH  the login QR code shows %s", url);
  return true;
}

LoginStatus DeviceAccount::loginStatus() {
  switch (net::oauthState()) {
    case net::OauthState::Idle: return LoginStatus::Idle;
    case net::OauthState::Waiting: return LoginStatus::Waiting;
    case net::OauthState::Exchanging: return LoginStatus::Exchanging;
    case net::OauthState::Done: return LoginStatus::Done;
    case net::OauthState::Failed: return LoginStatus::Failed;
  }
  return LoginStatus::Idle;
}

const char* DeviceAccount::loginError() { return net::oauthError(); }

// Not while the code is being exchanged: that runs on the console worker, and wiping the
// verifier under it would only make the exchange fail.
void DeviceAccount::cancelLogin() {
  const net::OauthState state = net::oauthState();
  if (state == net::OauthState::Waiting || state == net::OauthState::Failed) net::oauthCancel();
}

// The token goes, and with it a login state that would still say "done".
void DeviceAccount::unlink() {
  if (net::oauthState() != net::OauthState::Exchanging) net::oauthCancel();
  net::tokenClear();
}
