// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the account seam in front of the real proxy (sim/server.py).
//
// On the Mac there is no Wi-Fi to join and no login to make: the network is the Mac's, and the
// account is whatever token sim/server.py found (ARROCCO_LICHESS_TOKEN or
// ~/.config/arrocco/lichess-token). The token stays in python; this only asks whether there is one.
// So the Wi-Fi page never shows, and the login page explains where the token goes instead of
// showing a code: the OAuth round trip needs the board on the LAN, which the simulator is not.
#pragma once

#include <arrocco/lichess/account.h>

#include <string>

#include "lichess_transport.h"

namespace arrocco_sim {

class ProxyAccount final : public arrocco::lichess::Account {
 public:
  explicit ProxyAccount(ProxyTransport& proxy) : proxy_(proxy) {}

  arrocco::lichess::WifiStatus wifiStatus() override { return arrocco::lichess::WifiStatus::Online; }
  const char* wifiDetail() override { return "the Mac's network"; }
  const char* portalName() override { return "Arrocco-SIM"; }
  const char* portalAddress() override { return "http://4.3.2.1"; }
  void openPortal() override {}
  void closePortal() override {}
  void wantNetwork() override {}
  // Asks the proxy (a localhost round trip of a millisecond), at most every few seconds.
  bool linked() override;
  bool beginLogin(char* url, int urlSize) override;
  arrocco::lichess::LoginStatus loginStatus() override { return arrocco::lichess::LoginStatus::Failed; }
  const char* loginError() override;
  void cancelLogin() override {}
  // The token is the proxy's: "unlink" only makes this run of the simulator forget it.
  void unlink() override { unlinked_ = true; }

 private:
  ProxyTransport& proxy_;
  bool unlinked_ = false;
  bool cached_ = false;
  bool known_ = false;
  uint32_t askedMs_ = 0;
};

}  // namespace arrocco_sim
