// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - WiFi: credentials in NVS, first-boot captive portal, station with
// reconnect. Everything here is non-blocking: wifiService() is called from the Arduino
// loop between two app ticks and must never sit on the network.
//
// The state is what the UI shows; wifiStateText() and wifiDetail() are ready-to-draw
// English strings. The password is never logged, never shown and never leaves NVS.
//
// This module also owns the one WebServer on port 80: the provisioning portal and the
// OAuth redirect (net_token.h) register their handlers on the same instance, because two
// servers cannot share the port.
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

class WebServer;

namespace net {

enum class WifiState : uint8_t {
  Off,        // wifiBegin() not called yet
  Scanning,   // listing the networks for the portal page
  Portal,     // SoftAP up, captive portal waiting for the user
  Connecting, // associating with the stored network
  Online,     // associated and holding an IP
  Failed,     // the last attempt failed; wifiDetail() says why, a retry is scheduled
};

// Reads the credentials from NVS and either connects (they exist) or opens the portal
// (they do not). Returns at once: the work happens in wifiService().
void wifiBegin();

// Steps the state machine, the DNS responder and the web server. Call it every loop.
//
// One honest warning: WebServer::handleClient() is not bounded in the way the rest of
// this module is. The Arduino core gives the accepted socket a 5 s timeout and parses
// each request line with a blocking read, so a peer that opens a connection and then
// stops talking holds the loop task - game, clock and touch - for up to about five
// seconds. That is why the server is only serviced while the portal is open or while
// wifiWebWindow(true) is in force; at any other time nothing is read from port 80 and
// this function stays in the microseconds.
void wifiService();

WifiState wifiState();
const char* wifiStateText(); // "Online", "Portal open", "Connecting", ...
const char* wifiDetail();    // SSID + IP when online, the reason when failed
bool wifiOnline();
bool wifiPortalOpen();
bool wifiHasCredentials();

// "Arrocco-4F2A" - the last two bytes of the MAC, so two boards never collide.
const char* wifiApSsid();
IPAddress wifiIp(); // the station address, or the AP address while the portal is open

// Forgets the stored network and reopens the portal. Used by Settings and by the
// serial command 'wifi-forget'.
void wifiForget();

// Opens the portal without touching the stored credentials (the user wants to move the
// board to another network). Connecting again through the portal overwrites them.
void wifiOpenPortal();

// The one server on port 80, created by wifiBegin(). Other modules add handlers to it.
WebServer& web();

// Opens and closes the window in which that server is actually serviced while the board
// is online - see the warning on wifiService(). net_token.cpp holds it open for the few
// seconds an OAuth login needs to come back to /oauth/callback. While the portal is up
// the server is serviced anyway.
void wifiWebWindow(bool open);
bool wifiWebWindowOpen();

} // namespace net
