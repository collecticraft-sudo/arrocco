// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_token.h"

#include <Preferences.h>
#include <WebServer.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "log.h"
#include "net_http.h"
#include "net_wifi.h"

namespace net {
namespace {

constexpr char kNvsNamespace[] = "arrocco-lich"; // NVS namespaces stop at 15 characters
constexpr char kKeyToken[] = "token";

// Lichess accepts any client_id from an unregistered public client; it is shown to the
// user on the consent page, so it says what is asking.
constexpr char kClientId[] = "arrocco.local";
constexpr char kScope[] = "board:play";
constexpr char kCallbackPath[] = "/oauth/callback";

constexpr size_t kVerifierLen = 64; // 43..128 per RFC 7636; 64 is comfortably inside

char s_token[kTokenMax] = {};
bool s_tokenLoaded = false;
bool s_tokenPresent = false;

// Written on the loop task (handleCallback, oauthBegin, oauthCancel) and on the console
// worker (oauthService), and polled by both: volatile so neither side keeps a stale copy.
volatile OauthState s_oauthState = OauthState::Idle;
char s_oauthError[80] = {};
char s_verifier[kVerifierLen + 1] = {};
char s_state[33] = {};
char s_code[512] = {};    // the authorization code: as secret as the token, never logged
char s_redirect[64] = {};
bool s_handlerRegistered = false;

// --- storage --------------------------------------------------------------------------

void ensureLoaded() {
  if (s_tokenLoaded) return;
  s_tokenLoaded = true;
  Preferences p;
  if (!p.begin(kNvsNamespace, true)) {
    s_tokenPresent = false;
    return;
  }
  const size_t n = p.getString(kKeyToken, s_token, sizeof(s_token));
  p.end();
  s_tokenPresent = (n > 0 && s_token[0] != '\0');
}

// --- small helpers ----------------------------------------------------------------------

bool unreserved(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '.' || c == '_' || c == '~';
}

// Percent-encodes into out. False when it does not fit.
bool urlEncode(const char* in, char* out, size_t outSize) {
  size_t o = 0;
  for (const char* p = in; *p; ++p) {
    if (unreserved(*p)) {
      if (o + 1 >= outSize) return false;
      out[o++] = *p;
    } else {
      if (o + 3 >= outSize) return false;
      static const char kHex[] = "0123456789ABCDEF";
      out[o++] = '%';
      out[o++] = kHex[(static_cast<unsigned char>(*p) >> 4) & 0xF];
      out[o++] = kHex[static_cast<unsigned char>(*p) & 0xF];
    }
  }
  if (o >= outSize) return false;
  out[o] = '\0';
  return true;
}

void randomChars(char* out, size_t len) {
  static const char kSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  constexpr size_t kSetLen = sizeof(kSet) - 1;
  for (size_t i = 0; i < len; ++i) out[i] = kSet[esp_random() % kSetLen];
  out[len] = '\0';
}

// base64url of SHA-256(verifier), no padding: the S256 code_challenge of RFC 7636.
bool s256Challenge(const char* verifier, char* out, size_t outSize) {
  unsigned char digest[32];
  if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(verifier), strlen(verifier), digest,
                     0) != 0)
    return false;
  unsigned char b64[64];
  size_t olen = 0;
  if (mbedtls_base64_encode(b64, sizeof(b64), &olen, digest, sizeof(digest)) != 0) return false;
  if (olen + 1 > outSize) return false;
  size_t o = 0;
  for (size_t i = 0; i < olen; ++i) {
    const char c = static_cast<char>(b64[i]);
    if (c == '=') continue; // no padding
    out[o++] = (c == '+') ? '-' : (c == '/') ? '_' : c;
  }
  out[o] = '\0';
  return true;
}

// Pulls a string field out of a flat JSON object. No allocation, no library: the two
// replies this has to read ({"access_token":"..."} and {"error":"..."}) are flat.
bool jsonString(const char* json, const char* key, char* out, size_t outSize) {
  char needle[40];
  const int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(needle)) return false;
  const char* p = strstr(json, needle);
  if (!p) return false;
  p += n;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != ':') return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != '"') return false;
  ++p;
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < outSize) {
    if (*p == '\\' && p[1]) ++p; // the two fields of interest never need this, but be safe
    out[o++] = *p++;
  }
  out[o] = '\0';
  return o > 0;
}

void fail(const char* why) {
  snprintf(s_oauthError, sizeof(s_oauthError), "%s", why);
  s_oauthState = OauthState::Failed;
  memset(s_verifier, 0, sizeof(s_verifier));
  memset(s_code, 0, sizeof(s_code));
  wifiWebWindow(false); // nothing more is coming back to /oauth/callback
  logLine("OAUTH failed: %s", why);
}

// Runs on the loop task inside WebServer::handleClient(): quick, no network.
void handleCallback() {
  WebServer& w = web();
  if (s_oauthState != OauthState::Waiting) {
    w.send(400, "text/html",
           F("<!doctype html><meta charset=utf-8><p>This login link has expired. Start again "
             "from the board.</p>"));
    return;
  }
  const String err = w.arg("error");
  if (!err.isEmpty()) {
    fail("refused on Lichess");
    w.send(200, "text/html",
           F("<!doctype html><meta charset=utf-8><p>Login refused. Nothing was saved.</p>"));
    return;
  }
  const String state = w.arg("state");
  const String code = w.arg("code");
  if (state != s_state) { // the only defence against a forged callback
    fail("state mismatch");
    w.send(400, "text/html",
           F("<!doctype html><meta charset=utf-8><p>This link does not belong to this "
             "board.</p>"));
    return;
  }
  if (code.isEmpty() || code.length() >= sizeof(s_code)) {
    fail("no usable code in the callback");
    w.send(400, "text/html", F("<!doctype html><meta charset=utf-8><p>Missing code.</p>"));
    return;
  }
  snprintf(s_code, sizeof(s_code), "%s", code.c_str()); // never logged: it buys a token
  s_oauthState = OauthState::Exchanging;
  w.send(200, "text/html",
         F("<!doctype html><html><head><meta charset=utf-8>"
           "<meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Arrocco</title></head><body style='font-family:system-ui,sans-serif;"
           "padding:24px'><h1>Done</h1><p>You can close this page and go back to the "
           "board.</p></body></html>"));
  logLine("OAUTH code received, exchanging it for a token");
}

} // namespace

// --- storage ----------------------------------------------------------------------------

bool tokenPresent() {
  ensureLoaded();
  return s_tokenPresent;
}

size_t tokenLength() {
  ensureLoaded();
  return s_tokenPresent ? strlen(s_token) : 0;
}

bool tokenSave(const char* token) {
  if (!token) return false;
  while (*token == ' ' || *token == '\t' || *token == '\r' || *token == '\n') ++token;
  size_t len = strlen(token);
  while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t' || token[len - 1] == '\r' ||
                     token[len - 1] == '\n'))
    --len;
  if (len == 0 || len >= kTokenMax) return false;

  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return false;
  char trimmed[kTokenMax];
  memcpy(trimmed, token, len);
  trimmed[len] = '\0';
  const size_t written = p.putString(kKeyToken, trimmed);
  p.end();
  if (written == 0) {
    memset(trimmed, 0, sizeof(trimmed));
    return false;
  }
  memcpy(s_token, trimmed, len + 1);
  memset(trimmed, 0, sizeof(trimmed));
  s_tokenLoaded = true;
  s_tokenPresent = true;
  logLine("TOKEN stored (%u characters, value never logged)", static_cast<unsigned>(len));
  return true;
}

void tokenClear() {
  Preferences p;
  if (p.begin(kNvsNamespace, false)) {
    p.remove(kKeyToken);
    p.end();
  }
  memset(s_token, 0, sizeof(s_token));
  s_tokenLoaded = true;
  s_tokenPresent = false;
  logLine("TOKEN cleared");
}

bool tokenCopy(char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  out[0] = '\0';
  ensureLoaded();
  if (!s_tokenPresent) return false;
  if (strlen(s_token) + 1 > outSize) return false;
  strcpy(out, s_token);
  return true;
}

// --- OAuth2 PKCE --------------------------------------------------------------------------

bool oauthBegin(char* urlOut, size_t urlSize) {
  if (!urlOut || urlSize == 0) return false;
  urlOut[0] = '\0';
  if (!wifiOnline()) {
    fail("the board is not online");
    return false;
  }

  randomChars(s_verifier, kVerifierLen);
  randomChars(s_state, 32);
  char challenge[48];
  if (!s256Challenge(s_verifier, challenge, sizeof(challenge))) {
    fail("could not compute the challenge");
    return false;
  }

  snprintf(s_redirect, sizeof(s_redirect), "http://%s%s", wifiIp().toString().c_str(),
           kCallbackPath);
  char redirectEnc[128];
  if (!urlEncode(s_redirect, redirectEnc, sizeof(redirectEnc))) {
    fail("redirect address too long");
    return false;
  }

  if (!s_handlerRegistered) {
    web().on(kCallbackPath, handleCallback);
    s_handlerRegistered = true;
  }

  const int n = snprintf(urlOut, urlSize,
                         "https://lichess.org/oauth?response_type=code&client_id=%s"
                         "&redirect_uri=%s&code_challenge_method=S256&code_challenge=%s"
                         "&scope=%s&state=%s",
                         kClientId, redirectEnc, challenge, kScope, s_state);
  if (n < 0 || static_cast<size_t>(n) >= urlSize) {
    fail("the authorize URL does not fit");
    return false;
  }
  s_oauthError[0] = '\0';
  s_oauthState = OauthState::Waiting;
  wifiWebWindow(true); // from here until the code lands, port 80 has to be answered
  logLine("OAUTH waiting: scan the QR, the phone comes back to %s", s_redirect);
  return true;
}

void oauthCancel() {
  memset(s_verifier, 0, sizeof(s_verifier));
  memset(s_code, 0, sizeof(s_code));
  s_oauthState = OauthState::Idle;
  s_oauthError[0] = '\0';
  wifiWebWindow(false);
}

OauthState oauthState() { return s_oauthState; }

const char* oauthStateText() {
  switch (s_oauthState) {
    case OauthState::Idle: return "Idle";
    case OauthState::Waiting: return "Waiting for the phone";
    case OauthState::Exchanging: return "Finishing the login";
    case OauthState::Done: return "Logged in";
    case OauthState::Failed: return "Login failed";
  }
  return "Idle";
}

const char* oauthError() { return s_oauthError; }

bool oauthBusy() { return s_oauthState == OauthState::Exchanging; }

void oauthService() {
  if (s_oauthState != OauthState::Exchanging) return;

  // The body carries the code and the verifier: a POST form, never a query string.
  char codeEnc[600];
  char redirectEnc[128];
  if (!urlEncode(s_code, codeEnc, sizeof(codeEnc)) ||
      !urlEncode(s_redirect, redirectEnc, sizeof(redirectEnc))) {
    fail("could not build the exchange request");
    return;
  }
  char body[900];
  const int n = snprintf(body, sizeof(body),
                         "grant_type=authorization_code&code=%s&code_verifier=%s"
                         "&redirect_uri=%s&client_id=%s",
                         codeEnc, s_verifier, redirectEnc, kClientId);
  memset(codeEnc, 0, sizeof(codeEnc));
  if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
    memset(body, 0, sizeof(body));
    fail("the exchange request does not fit");
    return;
  }

  char reply[512];
  const int status = requestBlocking("POST", "/api/token", body, reply, sizeof(reply), false);
  memset(body, 0, sizeof(body));
  memset(s_code, 0, sizeof(s_code));

  if (status != 200) {
    char why[48] = {};
    if (status > 0 && jsonString(reply, "error", why, sizeof(why)))
      snprintf(s_oauthError, sizeof(s_oauthError), "Lichess said: %s", why);
    else
      snprintf(s_oauthError, sizeof(s_oauthError), "token exchange returned %d", status);
    memset(reply, 0, sizeof(reply));
    s_oauthState = OauthState::Failed;
    memset(s_verifier, 0, sizeof(s_verifier));
    wifiWebWindow(false);
    logLine("OAUTH failed: %s", s_oauthError);
    return;
  }

  char token[kTokenMax];
  const bool got = jsonString(reply, "access_token", token, sizeof(token));
  memset(reply, 0, sizeof(reply)); // the token was in there too
  memset(s_verifier, 0, sizeof(s_verifier));
  if (!got || !tokenSave(token)) {
    memset(token, 0, sizeof(token));
    fail("no usable token in the reply");
    return;
  }
  memset(token, 0, sizeof(token));
  s_oauthState = OauthState::Done;
  s_oauthError[0] = '\0';
  wifiWebWindow(false); // the phone already has its "you can close this page"
  logLine("OAUTH done: the board is logged in");
}

} // namespace net
