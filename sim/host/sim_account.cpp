// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the account in front of the proxy. See sim_account.h.
#include "sim_account.h"

#include <cstdio>

namespace arrocco_sim {

namespace {

constexpr uint32_t kAskEveryMs = 5000;

}  // namespace

bool ProxyAccount::linked() {
  if (unlinked_) return false;
  const uint32_t now = proxy_.millis();
  if (!known_ || now - askedMs_ >= kAskEveryMs) {
    cached_ = proxy_.proxyHasToken();
    known_ = true;
    askedMs_ = now;
  }
  return cached_;
}

bool ProxyAccount::beginLogin(char* url, int urlSize) {
  if (url != nullptr && urlSize > 0) url[0] = '\0';
  return false;
}

const char* ProxyAccount::loginError() {
  return unlinked_ ? "Restart the simulator to use the token sim/server.py keeps again."
                   : "The simulator signs in with the token sim/server.py reads from "
                     "~/.config/arrocco/lichess-token. Put a token there and restart it.";
}

}  // namespace arrocco_sim
