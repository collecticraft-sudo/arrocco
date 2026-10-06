// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - the Lichess screens' account seam (arrocco/lichess/account.h) on the
// board's own network stack: net_wifi for the network and the setup portal, net_token for the
// login and the token. Nothing here talks to the network itself and nothing blocks: every call
// reads or sets a few words that the WiFi state machine (loop task) and the network tasks own.
//
// The login's QR code carries the board's short http://<ip>/login (net_token.h), not the long
// lichess.org URL: a smaller code, read more easily off e-paper, and typeable by hand.
#pragma once

#include <arrocco/lichess/account.h>

class DeviceAccount final : public arrocco::lichess::Account {
 public:
  arrocco::lichess::WifiStatus wifiStatus() override;
  const char* wifiDetail() override;
  const char* portalName() override;
  const char* portalAddress() override;
  void openPortal() override;
  void closePortal() override;
  void wantNetwork() override;
  void retryNetwork() override;
  bool linked() override;
  bool beginLogin(char* url, int urlSize) override;
  arrocco::lichess::LoginStatus loginStatus() override;
  const char* loginError() override;
  void cancelLogin() override;
  void unlink() override;
};
