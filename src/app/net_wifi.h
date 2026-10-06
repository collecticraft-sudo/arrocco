// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - WiFi: credentials in NVS, a setup portal opened on request, and a
// station that is up only while something needs the network.
//
// The radio is OFF at boot and whenever nothing needs it: a board on battery that keeps
// its radio up drains in days what it would otherwise keep for weeks.
//   - The station starts when a network job calls wifiNeed() (an HTTPS request, a stream,
//     an OAuth login, 'wifi-on'), and the radio goes off kStationIdleOffMs after the last
//     call. Requests and streams in net_http.cpp call it by themselves.
//   - The setup portal (open SoftAP "Arrocco-XXXX", http://4.3.2.1) opens only when asked:
//     wifiOpenPortal(), wifiForget(), or the serial commands. It closes by itself after
//     kPortalIdleMs with no page requested, then the radio goes off.
//   - Port 80 is served only while the portal is open or while an OAuth login is coming
//     back (wifiWebWindow): the listening socket is opened and closed with those windows.
//
// Everything here is non-blocking except wifiWaitOnline(), which is for tasks. The state
// is what the UI will show; wifiStateText() and wifiDetail() are ready-to-draw English
// strings. The password is never logged, never shown and never leaves NVS.
//
// A future "Lichess" or "WiFi setup" menu item needs no more than this: wifiOpenPortal()
// for setup, and the transport (net_http.h) brings the station up by itself.
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

class WebServer;

namespace net {

enum class WifiState : uint8_t {
  Off,        // radio off: the normal state when no network job is running
  Scanning,   // portal up, listing the networks for its page
  Portal,     // SoftAP up, captive portal waiting for the user
  Connecting, // associating with the stored network
  Online,     // associated and holding an IP
  Failed,     // the last attempt failed; wifiDetail() says why, a retry is scheduled
};

// Reads the credentials from NVS. The radio stays off.
void wifiBegin();

// Steps the state machine, the DNS responder and the web server. Call it every loop.
//
// One honest warning: WebServer::handleClient() is not bounded in the way the rest of
// this module is. The Arduino core gives the accepted socket a 5 s timeout and parses
// each request line with a blocking read, so a peer that opens a connection and then
// stops talking holds the loop task - game, clock and touch - for up to about five
// seconds. That is why the server only exists while the portal is open or while
// wifiWebWindow(true) is in force; at any other time nothing listens on port 80 and this
// function stays in the microseconds.
void wifiService();

WifiState wifiState();
const char* wifiStateText(); // "WiFi off", "Online", "Portal open", "Connecting", ...
const char* wifiDetail();    // SSID + IP when online, the reason when failed
bool wifiOnline();
bool wifiPortalOpen();
bool wifiHasCredentials();
// The radio is on, for whatever reason: the board must not sleep yet.
bool wifiBusy();

// "Arrocco-4F2A" - the last two bytes of the MAC, so two boards never collide.
const char* wifiApSsid();
IPAddress wifiIp(); // the station address, or the AP address while the portal is open

// --- on demand ---

// Something needs the station now: starts it if the radio is off, keeps it on for
// kStationIdleOffMs more. Any task may call it (two plain words written).
void wifiNeed();
// For tasks only (it blocks): wifiNeed() until the station is online, at most timeoutMs.
// false at once when no network is stored.
bool wifiWaitOnline(uint32_t timeoutMs);
// Loop task only: closes the portal, drops the station, radio off. `why` goes to the log.
void wifiRadioOff(const char* why);

// Forgets the stored network and opens the portal. Used by Settings and by the
// serial command 'wifi-forget'.
void wifiForget();

// Opens the portal without touching the stored credentials (the user wants to move the
// board to another network). Connecting again through the portal overwrites them.
void wifiOpenPortal();

// The one server on port 80. Other modules add handlers to it; it listens only inside
// the windows described above.
WebServer& web();

// Opens and closes the window in which that server is serviced while the board is
// online. net_token.cpp holds it open for an OAuth login to come back to /oauth/callback;
// while it is open the station is kept up. May be called from any task.
void wifiWebWindow(bool open);
bool wifiWebWindowOpen();

} // namespace net
