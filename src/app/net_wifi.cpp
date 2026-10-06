// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_wifi.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "log.h"

namespace net {
namespace {

constexpr char kNvsNamespace[] = "arrocco-wifi";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPass[] = "pass";

constexpr size_t kSsidMax = 33; // 32 + NUL
constexpr size_t kPassMax = 65; // 64 + NUL

constexpr uint32_t kConnectTimeoutMs = 20000; // association + DHCP
constexpr uint32_t kRetryFirstMs = 3000;      // backoff between attempts: 3, 6, 12, 24, 30 s
constexpr uint32_t kRetryMaxMs = 30000;
constexpr uint32_t kScanPeriodMs = 30000;     // refresh the portal's list in the background
constexpr uint32_t kPortalCloseDelayMs = 1500; // let the "saved" page reach the browser
// The radio goes off this long after the last network job asked for it: long enough for
// a few serial test commands in a row, short enough not to matter on battery.
constexpr uint32_t kStationIdleOffMs = 120000;
// The portal closes this long after it opened, or after the last page it served. Filling
// in the form takes a minute; a portal nobody uses must not keep an open AP up for ever.
constexpr uint32_t kPortalIdleMs = 300000;
constexpr uint8_t kDnsPort = 53;

// Portal address. 4.3.2.1 is the usual captive-portal pick: short to type and outside
// the ranges a home router hands out, so nothing collides while both are reachable.
const IPAddress kApIp(4, 3, 2, 1);
const IPAddress kApMask(255, 255, 255, 0);

WebServer* s_web = nullptr;
// One instance for good, never deleted: AsyncUDP may still hand a queued packet to it
// after stop(), and a deleted object there is a use-after-free. (The first version
// deleted it 1.5 s after "Save and connect", while the phone was still asking.)
DNSServer s_dns;
bool s_dnsUp = false;

WifiState s_state = WifiState::Off;
char s_ssid[kSsidMax] = {};
char s_pass[kPassMax] = {};
bool s_haveCreds = false;
char s_apSsid[16] = {};
char s_detail[96] = {};

uint32_t s_attemptStartMs = 0;
uint32_t s_nextAttemptMs = 0;
uint32_t s_retryMs = kRetryFirstMs;
uint8_t s_failures = 0;
uint32_t s_lastScanMs = 0;
bool s_scanRunning = false;
int16_t s_scanCount = 0;
uint32_t s_portalCloseAtMs = 0;
bool s_portalCloseArmed = false;
uint32_t s_portalActivityMs = 0;
bool s_handlersUp = false; // routes registered on s_web
bool s_serverUp = false;   // s_web listening on port 80
bool s_noCredsLogged = false;
// Raised and lowered by net_token.cpp, which does it from the console worker task as well
// as from the loop; read by wifiService() on the loop.
volatile bool s_webWindow = false;
// Written by any task through wifiNeed(), read by the loop.
volatile uint32_t s_needMs = 0;
volatile bool s_needed = false;

// Written by the WiFi event task, read by the loop: only ever a plain word.
volatile uint8_t s_lastDisconnectReason = 0;
volatile bool s_disconnectPending = false;

// --- credentials ---------------------------------------------------------------------

void loadCreds() {
  Preferences p;
  if (!p.begin(kNvsNamespace, true)) { // read-only; missing namespace is not an error
    s_haveCreds = false;
    return;
  }
  const size_t n = p.getString(kKeySsid, s_ssid, sizeof(s_ssid));
  p.getString(kKeyPass, s_pass, sizeof(s_pass));
  p.end();
  s_haveCreds = (n > 0 && s_ssid[0] != '\0');
}

void storeCreds(const char* ssid, const char* pass) {
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) {
    logLine("WIFI  cannot open NVS to store the network");
    return;
  }
  p.putString(kKeySsid, ssid);
  p.putString(kKeyPass, pass ? pass : "");
  p.end();
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
  snprintf(s_pass, sizeof(s_pass), "%s", pass ? pass : "");
  s_haveCreds = s_ssid[0] != '\0';
  s_noCredsLogged = false;
  // The SSID is broadcast by the router anyway; the password never reaches the log.
  logLine("WIFI  network '%s' stored (password %u chars, not logged)", s_ssid,
          static_cast<unsigned>(strlen(s_pass)));
}

void wipeCreds() {
  Preferences p;
  if (p.begin(kNvsNamespace, false)) {
    p.clear();
    p.end();
  }
  memset(s_ssid, 0, sizeof(s_ssid));
  memset(s_pass, 0, sizeof(s_pass));
  s_haveCreds = false;
}

// --- helpers -------------------------------------------------------------------------

const char* disconnectReasonText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND: return "network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "wrong password";
    case WIFI_REASON_AUTH_EXPIRE: return "the router dropped us";
    case WIFI_REASON_ASSOC_TOOMANY: return "the router is full";
    case WIFI_REASON_BEACON_TIMEOUT: return "out of range";
    case WIFI_REASON_CONNECTION_FAIL: return "the router refused the connection";
    default: return "connection failed";
  }
}

void setDetail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void setDetail(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_detail, sizeof(s_detail), fmt, ap);
  va_end(ap);
}

void setOffDetail() {
  if (s_haveCreds) setDetail("radio off, '%s' stored", s_ssid);
  else setDetail("radio off, no network stored");
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  // Runs on the event task: only plain words written here.
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    s_lastDisconnectReason = info.wifi_sta_disconnected.reason;
    s_disconnectPending = true;
  }
}

// Signed: a task may stamp its need a moment after the loop read the clock.
bool stationWanted(uint32_t now) {
  return s_webWindow || (s_needed && static_cast<int32_t>(now - s_needMs) < static_cast<int32_t>(kStationIdleOffMs));
}

// A scan in AP_STA mode makes the radio leave the AP's channel for a second or two, which
// drops whoever is filling in the form. So the background refresh only runs while nobody
// is joined to the AP; the "Scan again" link forces one anyway, because then the user is
// asking for it and is willing to wait for the page to come back.
bool apHasClient() { return WiFi.softAPgetStationNum() > 0; }

void startScan() {
  if (s_scanRunning) return;
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true /* async */, false /* hidden */) == WIFI_SCAN_RUNNING) {
    s_scanRunning = true;
    s_lastScanMs = ::millis();
  }
}

void pollScan() {
  if (!s_scanRunning) return;
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  s_scanRunning = false;
  s_scanCount = (n < 0) ? 0 : n;
  if (s_state == WifiState::Scanning) {
    s_state = WifiState::Portal;
    setDetail("Join '%s', then open any page", s_apSsid);
  }
  logLine("WIFI  scan done: %d networks", static_cast<int>(s_scanCount));
}

// --- the portal page -----------------------------------------------------------------

String escapeHtml(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c;
    }
  }
  return out;
}

void handlePortalRoot() {
  // Nothing external: no CDN, no web font, no image. The phone that loads this page has
  // no way out to the internet, so anything not inline would simply never arrive. The
  // empty data: icon is there so the browser does not go asking for /favicon.ico either.
  String page = F("<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<link rel=icon href='data:,'>"
                  "<title>Arrocco</title><style>"
                  "body{font-family:system-ui,sans-serif;margin:0;padding:24px;background:#fafafa;color:#111}"
                  "h1{font-size:20px;margin:0 0 4px}p{color:#555;font-size:14px;margin:0 0 20px}"
                  "label{display:block;font-size:13px;margin:16px 0 4px;color:#333}"
                  "select,input{width:100%;box-sizing:border-box;padding:10px;font-size:16px;"
                  "border:1px solid #ccc;border-radius:6px;background:#fff}"
                  "button{width:100%;margin-top:20px;padding:12px;font-size:16px;border:0;"
                  "border-radius:6px;background:#111;color:#fff}"
                  "a{display:block;text-align:center;margin-top:14px;color:#555;font-size:13px}"
                  "</style></head><body><h1>Arrocco</h1>"
                  "<p>Choose your WiFi network.</p><form method=POST action=/save>"
                  "<label>Network</label><select name=ssid>");
  if (s_scanCount == 0) {
    page += F("<option value=''>-- no network found, use the field below --</option>");
  }
  for (int16_t i = 0; i < s_scanCount; ++i) {
    const String ssid = WiFi.SSID(static_cast<uint8_t>(i));
    if (ssid.isEmpty()) continue;
    const String safe = escapeHtml(ssid);
    page += F("<option value=\"");
    page += safe;
    page += F("\">");
    page += safe;
    page += F(" (");
    page += String(WiFi.RSSI(static_cast<uint8_t>(i)));
    page += F(" dBm)");
    if (WiFi.encryptionType(static_cast<uint8_t>(i)) == WIFI_AUTH_OPEN) page += F(" open");
    page += F("</option>");
  }
  page += F("</select>"
            "<label>Or type a hidden network name</label><input name=ssid2 autocapitalize=off autocorrect=off>"
            "<label>Password</label><input name=pass type=password autocapitalize=off autocorrect=off>"
            "<button type=submit>Save and connect</button></form>"
            "<a href='/?scan=1'>Scan again</a></body></html>");
  s_web->send(200, "text/html", page);
}

void handleRoot() {
  if (wifiPortalOpen()) {
    s_portalActivityMs = ::millis();
    if (s_web->hasArg("scan")) startScan();
    handlePortalRoot();
    return;
  }
  // Online (inside a login window): a one-line status page. No secrets on it.
  String page = F("<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>Arrocco</title><style>body{font-family:system-ui,sans-serif;"
                  "margin:0;padding:24px;color:#111}</style></head><body><h1>Arrocco</h1><p>");
  page += wifiStateText();
  page += F(" &mdash; ");
  page += escapeHtml(String(wifiDetail()));
  page += F("</p></body></html>");
  s_web->send(200, "text/html", page);
}

void handleSave() {
  s_portalActivityMs = ::millis();
  String ssid = s_web->arg("ssid");
  const String typed = s_web->arg("ssid2");
  if (!typed.isEmpty()) ssid = typed;
  const String pass = s_web->arg("pass");
  if (ssid.isEmpty()) {
    s_web->send(400, "text/html",
                F("<!doctype html><meta charset=utf-8><p>Pick a network first.</p>"
                  "<p><a href=/>Back</a></p>"));
    return;
  }
  if (ssid.length() >= kSsidMax || pass.length() >= kPassMax) {
    s_web->send(400, "text/html",
                F("<!doctype html><meta charset=utf-8><p>Name or password too long.</p>"
                  "<p><a href=/>Back</a></p>"));
    return;
  }
  String page = F("<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>Arrocco</title><style>body{font-family:system-ui,sans-serif;"
                  "margin:0;padding:24px;color:#111}</style></head><body><h1>Saved</h1>"
                  "<p>Connecting to <b>");
  page += escapeHtml(ssid);
  page += F("</b>. This access point is closing; the board will show the result on its "
            "own screen.</p></body></html>");
  s_web->send(200, "text/html", page);
  storeCreds(ssid.c_str(), pass.c_str());
  s_portalCloseAtMs = ::millis() + kPortalCloseDelayMs;
  s_portalCloseArmed = true;
}

// Every captive-portal probe (Apple, Android, Windows) must get a redirect, not a 204,
// or the phone decides the network is fine and never opens the page.
void handleCaptiveProbe() {
  if (wifiPortalOpen()) {
    s_portalActivityMs = ::millis();
    String url = F("http://");
    url += kApIp.toString();
    url += F("/");
    s_web->sendHeader("Location", url, true);
    s_web->send(302, "text/plain", "");
    return;
  }
  s_web->send(404, "text/plain", "not found");
}

// Routes only: the socket is opened and closed by reconcileServer().
void installHandlers() {
  if (s_handlersUp) return;
  s_web->on("/", handleRoot);
  s_web->on("/save", HTTP_POST, handleSave);
  s_web->onNotFound(handleCaptiveProbe);
  s_handlersUp = true;
}

// Port 80 listens exactly while the portal is open, or while a login is coming back
// with the radio on. Called on the loop only, so begin/stop never race handleClient().
void reconcileServer() {
  const bool serve = wifiPortalOpen() || (s_webWindow && s_state != WifiState::Off);
  if (serve == s_serverUp) return;
  if (serve) {
    if (!s_web) s_web = new WebServer(80);
    installHandlers();
    s_web->begin();
    logLine("WIFI  port 80 open (%s)", wifiPortalOpen() ? "setup portal" : "a login is coming back");
  } else {
    s_web->stop();
    logLine("WIFI  port 80 closed");
  }
  s_serverUp = serve;
}

// --- state transitions ---------------------------------------------------------------

void beginStation() {
  WiFi.setAutoReconnect(false); // the retry policy below is ours, with a real backoff
  WiFi.begin(s_ssid, s_pass);
  s_state = WifiState::Connecting;
  s_attemptStartMs = ::millis();
  s_disconnectPending = false;
  setDetail("Joining '%s'", s_ssid);
  logLine("WIFI  connecting to '%s'", s_ssid);
}

void startStation() {
  if (!s_haveCreds) {
    if (!s_noCredsLogged) {
      logLine("WIFI  a network job is waiting, but no network is stored: type wifi-portal");
      s_noCredsLogged = true;
    }
    s_needed = false;
    return;
  }
  logLine("WIFI  radio on: a network job needs '%s'", s_ssid);
  WiFi.mode(WIFI_STA);
  s_failures = 0;
  s_retryMs = kRetryFirstMs;
  beginStation();
}

void openPortal() {
  WiFi.mode(WIFI_AP_STA); // AP for the portal, STA so the scan has a radio to use
  WiFi.softAPConfig(kApIp, kApIp, kApMask);
  WiFi.softAP(s_apSsid); // open network: the user has no password to be told yet
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
  WiFi.AP.enableDhcpCaptivePortal(); // DHCP option 114, so modern phones pop the page
#endif
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dnsUp = s_dns.start(kDnsPort, "*", kApIp); // every name resolves to us
  s_state = WifiState::Scanning;
  s_portalActivityMs = ::millis();
  s_portalCloseArmed = false;
  setDetail("Join '%s', then open any page", s_apSsid);
  logLine("WIFI  portal open: SSID '%s', http://%s - it closes after %lu min with no page asked",
          s_apSsid, kApIp.toString().c_str(), static_cast<unsigned long>(kPortalIdleMs / 60000UL));
  startScan();
  reconcileServer();
}

// Takes the AP and the DNS responder down. The caller decides what the radio does next.
void closePortal() {
  if (s_dnsUp) {
    s_dns.stop();
    s_dnsUp = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.scanDelete();
  s_scanRunning = false;
  s_portalCloseArmed = false;
  logLine("WIFI  portal closed");
}

void radioOff(const char* why) {
  if (s_state == WifiState::Off) return;
  if (wifiPortalOpen()) closePortal();
  s_state = WifiState::Off;
  reconcileServer(); // the socket closes while lwIP is still up
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  s_failures = 0;
  s_retryMs = kRetryFirstMs;
  s_disconnectPending = false;
  setOffDetail();
  logLine("WIFI  radio off: %s", why);
}

void noteFailure(const char* why) {
  ++s_failures;
  s_state = WifiState::Failed;
  setDetail("%s (%s)", why, s_ssid);
  WiFi.disconnect(false, false);
  s_nextAttemptMs = ::millis() + s_retryMs;
  logLine("WIFI  failed: %s - retry in %lu s while a network job still wants it (attempt %u)", why,
          static_cast<unsigned long>(s_retryMs / 1000), static_cast<unsigned>(s_failures));
  s_retryMs = (s_retryMs * 2 > kRetryMaxMs) ? kRetryMaxMs : s_retryMs * 2;
}

void noteOnline() {
  s_state = WifiState::Online;
  s_failures = 0;
  s_retryMs = kRetryFirstMs;
  setDetail("%s - %s", s_ssid, WiFi.localIP().toString().c_str());
  logLine("WIFI  online: '%s' %s, RSSI %d dBm", s_ssid, WiFi.localIP().toString().c_str(),
          static_cast<int>(WiFi.RSSI()));
}

void servicePortal(uint32_t now) {
  // Signed difference: s_portalCloseAtMs is now + 1.5 s and may have wrapped past zero.
  if (s_portalCloseArmed && static_cast<int32_t>(now - s_portalCloseAtMs) >= 0) {
    closePortal();
    WiFi.mode(WIFI_STA);
    s_failures = 0;
    s_retryMs = kRetryFirstMs;
    wifiNeed(); // keep the station up a while: long enough to see that the network works
    beginStation();
    return;
  }
  // Signed: the page handlers stamp the activity after `now` was read.
  if (static_cast<int32_t>(now - s_portalActivityMs) >= static_cast<int32_t>(kPortalIdleMs)) {
    char why[48];
    snprintf(why, sizeof(why), "nobody used the portal for %lu min",
             static_cast<unsigned long>(kPortalIdleMs / 60000UL));
    radioOff(why); // a network job still waiting brings the station up at the next call
    return;
  }
  if (!s_scanRunning && !apHasClient() &&
      static_cast<int32_t>(now - s_lastScanMs) >= static_cast<int32_t>(kScanPeriodMs))
    startScan();
}

void serviceStation(uint32_t now) {
  if (!stationWanted(now)) {
    char why[48];
    snprintf(why, sizeof(why), "no network job for %lu s",
             static_cast<unsigned long>(kStationIdleOffMs / 1000UL));
    radioOff(why);
    return;
  }

  if (s_disconnectPending && s_state != WifiState::Failed) {
    s_disconnectPending = false;
    const uint8_t reason = s_lastDisconnectReason;
    if (s_state == WifiState::Online) {
      logLine("WIFI  dropped: %s", disconnectReasonText(reason));
      s_state = WifiState::Failed;
      setDetail("%s (%s)", disconnectReasonText(reason), s_ssid);
      s_nextAttemptMs = now + s_retryMs;
    } else if (s_state == WifiState::Connecting) {
      // Association refused: no point waiting out the 20 s timeout.
      noteFailure(disconnectReasonText(reason));
      return;
    }
  }

  switch (s_state) {
    case WifiState::Connecting:
      if (WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0) {
        noteOnline();
      } else if (now - s_attemptStartMs >= kConnectTimeoutMs) {
        noteFailure("timed out");
      }
      break;

    case WifiState::Online:
      if (WiFi.status() != WL_CONNECTED) {
        logLine("WIFI  connection lost");
        s_state = WifiState::Failed;
        setDetail("connection lost (%s)", s_ssid);
        s_nextAttemptMs = now + s_retryMs;
      }
      break;

    case WifiState::Failed:
      if (s_haveCreds && static_cast<int32_t>(now - s_nextAttemptMs) >= 0) beginStation();
      break;

    case WifiState::Off:
    case WifiState::Scanning:
    case WifiState::Portal:
      break;
  }
}

} // namespace

// --- public ---------------------------------------------------------------------------

void wifiBegin() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(s_apSsid, sizeof(s_apSsid), "Arrocco-%02X%02X", mac[4], mac[5]);

  if (!s_web) s_web = new WebServer(80); // routes are added now, the socket only when needed
  WiFi.onEvent(onWifiEvent);
  WiFi.persistent(false); // NVS is ours (Preferences), not the driver's shadow copy
  loadCreds();
  s_state = WifiState::Off;
  setOffDetail();
  if (s_haveCreds)
    logLine("WIFI  radio off; network '%s' stored, joined only when a network job needs it", s_ssid);
  else
    logLine("WIFI  radio off; no network stored ('wifi-portal' opens the setup portal)");
}

void wifiService() {
  const uint32_t now = ::millis();
  if (s_state == WifiState::Off) {
    if (stationWanted(now)) startStation();
    reconcileServer();
    return;
  }
  reconcileServer();
  // handleClient() is NOT the couple of milliseconds it may look like: the core sets the
  // client timeout to HTTP_MAX_SEND_WAIT (5 s) and Parsing.cpp reads every request line
  // with a blocking readStringUntil(), so one peer that opens a socket and then stalls
  // mid-request freezes the loop task for seconds. The server exists only inside the
  // portal and login windows, so that is the only time this can happen.
  if (s_serverUp) s_web->handleClient();
  pollScan();
  if (wifiPortalOpen()) servicePortal(now);
  else serviceStation(now);
}

WifiState wifiState() { return s_state; }

const char* wifiStateText() {
  switch (s_state) {
    case WifiState::Off: return "WiFi off";
    case WifiState::Scanning: return "Scanning";
    case WifiState::Portal: return "Portal open";
    case WifiState::Connecting: return "Connecting";
    case WifiState::Online: return "Online";
    case WifiState::Failed: return "Failed";
  }
  return "WiFi off";
}

const char* wifiDetail() { return s_detail; }
bool wifiOnline() { return s_state == WifiState::Online; }
bool wifiPortalOpen() { return s_state == WifiState::Portal || s_state == WifiState::Scanning; }
bool wifiHasCredentials() { return s_haveCreds; }
bool wifiBusy() { return s_state != WifiState::Off; }
const char* wifiApSsid() { return s_apSsid; }

IPAddress wifiIp() { return wifiPortalOpen() ? kApIp : WiFi.localIP(); }

void wifiNeed() {
  s_needMs = ::millis();
  s_needed = true;
}

bool wifiWaitOnline(uint32_t timeoutMs) {
  const uint32_t t0 = ::millis();
  for (;;) {
    wifiNeed();
    if (wifiOnline()) return true;
    if (!s_haveCreds || ::millis() - t0 >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void wifiRadioOff(const char* why) {
  s_needed = false;
  radioOff(why);
}

void wifiForget() {
  wipeCreds();
  logLine("WIFI  stored network forgotten");
  if (wifiPortalOpen()) {
    s_portalActivityMs = ::millis();
    return;
  }
  if (s_state != WifiState::Off) WiFi.disconnect(false, false);
  s_failures = 0;
  s_retryMs = kRetryFirstMs;
  openPortal();
}

void wifiOpenPortal() {
  if (wifiPortalOpen()) {
    s_portalActivityMs = ::millis();
    logLine("WIFI  the portal is already open: '%s', http://%s", s_apSsid, kApIp.toString().c_str());
    return;
  }
  if (s_state != WifiState::Off) WiFi.disconnect(false, false);
  s_failures = 0;
  s_retryMs = kRetryFirstMs;
  openPortal();
}

WebServer& web() {
  if (!s_web) s_web = new WebServer(80);
  return *s_web;
}

void wifiWebWindow(bool open) {
  if (s_webWindow == open) return;
  s_webWindow = open;
  logLine("WIFI  login window %s", open ? "open: port 80 answers while it lasts" : "closed");
}

bool wifiWebWindowOpen() { return s_webWindow; }

} // namespace net
