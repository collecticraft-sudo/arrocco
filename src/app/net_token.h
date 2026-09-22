// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - the Lichess token: where it is kept, and how it is obtained.
//
// The token is the account. It lives in its own NVS namespace and nowhere else: it is
// never logged, never drawn, never put in a URL or a query string (net_http.h sends it
// in the Authorization header only), and never written to a file. tokenPresent() and
// tokenLength() are all the diagnostics anything outside this file gets.
//
// Two ways in, both from docs/decisioni.md:
//   - OAuth2 PKCE: the board shows a QR, the phone opens lichess.org, Lichess redirects
//     back to the board on the LAN, the board swaps the code for a token. The QR and the
//     screens belong to a later step; what is here is the machinery plus a serial path
//     ('oauth-start') to exercise it.
//   - a personal token pasted by the user (tokenSave), the documented fallback.
#pragma once
#include <Arduino.h>

namespace net {

// --- storage (NVS namespace "arrocco-lich", key "token") ---

constexpr size_t kTokenMax = 128; // Lichess personal tokens are ~24 chars, OAuth ones ~32

bool tokenPresent();
size_t tokenLength();                    // 0 when absent; the value itself never leaves
bool tokenSave(const char* token);       // trims spaces; rejects empty and over-long
void tokenClear();                       // wipes NVS and the RAM copy

// Copies the token into the caller's buffer. Only net_http.cpp needs this; it returns
// false (and leaves out empty) when there is no token. Callers must not log the result.
bool tokenCopy(char* out, size_t outSize);

// --- OAuth2 PKCE, device side ---

enum class OauthState : uint8_t {
  Idle,      // nothing in flight
  Waiting,   // URL handed out, waiting for the phone to come back to /oauth/callback
  Exchanging,// code received, POST /api/token in flight on the worker task
  Done,      // a token was saved
  Failed,    // oauthError() says why
};

// Generates a fresh verifier and S256 challenge, registers /oauth/callback on the shared
// web server and writes the authorize URL (the one the QR will encode) into urlOut.
// Needs the station to be online: the phone must be able to reach the board's IP.
// False when offline or when the URL does not fit.
bool oauthBegin(char* urlOut, size_t urlSize);

void oauthCancel();       // forgets the verifier; the callback then answers "expired"
OauthState oauthState();
const char* oauthStateText();
const char* oauthError();  // "" unless Failed; never contains the code or the token

// Drives the exchange once the callback has fired. Called from the worker task in
// net_console.cpp, because it blocks on an HTTPS round trip.
void oauthService();

// True while oauthService() has work to do; lets the worker task idle otherwise.
bool oauthBusy();

} // namespace net
