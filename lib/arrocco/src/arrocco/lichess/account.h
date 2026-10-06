// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — the account seam: what the Lichess screens need to know about the board's
// Wi-Fi and its Lichess token, and the few things they may ask of them.
//
// The Transport (transport.h) carries requests and streams; this carries everything around them
// that the user has to see and do first: join a network, link an account, unlink it. The token
// itself never crosses this seam either: linked() says whether there is one, and that is all.
//
//   src/app/lichess_link.cpp       the firmware, on net_wifi / net_token
//   sim/host/fake_lichess.cpp      the simulator's scripted Lichess (tests, and trying it out)
//   sim/host/sim_account.cpp       the simulator in front of the real proxy
//
// Every call is made from the UI loop and returns at once. Strings are ready to draw (English) and
// stay valid until the next call; none of them ever holds a password, a code or a token.
#pragma once
#include <stdint.h>

namespace arrocco::lichess {

enum class WifiStatus : uint8_t {
  NoNetwork,   // no network stored: the setup portal is the only way on
  Off,         // a network is stored and the radio is off (it comes up when a job needs it)
  Connecting,  // joining the stored network
  Online,
  Failed,      // the last attempt failed, wifiDetail() says why; it retries while it is needed
  Portal,      // the setup portal is open: the phone joins portalName()
};

enum class LoginStatus : uint8_t {
  Idle,        // no login running
  Waiting,     // the code is on the screen: waiting for the phone to come back
  Exchanging,  // the phone came back: the board is swapping the code for a token
  Done,        // a token was saved: linked() is true
  Failed,      // loginError() says why
};

class Account {
 public:
  virtual ~Account() = default;

  // ---------------------------------------------------------------- Wi-Fi
  virtual WifiStatus wifiStatus() = 0;
  // "Joining 'Casa'", "wrong password (Casa)", "Casa - 192.168.1.50": for the status line.
  virtual const char* wifiDetail() = 0;
  // The setup network the phone joins ("Arrocco-4F2A") and the page it then opens
  // ("http://4.3.2.1"), for the text and for the Wi-Fi QR code.
  virtual const char* portalName() = 0;
  virtual const char* portalAddress() = 0;
  virtual void openPortal() = 0;
  // Closes the portal if it is open (the user left the setup screen); a no-op otherwise.
  virtual void closePortal() = 0;
  // Something on the screen needs the network: bring the station up, or keep it up a while.
  virtual void wantNetwork() = 0;

  // ---------------------------------------------------------------- the account
  virtual bool linked() = 0;
  // Starts an OAuth (PKCE) login. Writes into `url` the address the QR code shows: the phone
  // opens it, signs in on lichess.org and comes back to the board, which then saves the token.
  // Needs wifiStatus() == Online. False when it could not start: loginError() says why.
  virtual bool beginLogin(char* url, int urlSize) = 0;
  virtual LoginStatus loginStatus() = 0;
  virtual const char* loginError() = 0;
  virtual void cancelLogin() = 0;  // forgets a login in progress; Idle afterwards
  virtual void unlink() = 0;       // forgets the token: linked() is false afterwards
};

}  // namespace arrocco::lichess
