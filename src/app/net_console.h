// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - the serial test path for the network stack, and the worker task
// that carries every blocking call.
//
// Why a task: Transport::request() sits on a TLS handshake plus a round trip, and the
// Arduino loop has to keep ticking the app and the panel. Anything that touches the
// network from a command, and the OAuth token exchange, runs here instead. Nothing in
// this file is on the game path; it exists so the stack can be exercised over USB
// before the Lichess screens are built.
//
// Type 'help' on the serial monitor for the command list. No command ever prints the
// token or the OAuth code.
#pragma once

namespace net {

// Starts the worker task. Call from setup(), after httpBegin().
void consoleBegin();

// Reads one serial line if there is one, and prints whatever the open test streams
// have produced. Non-blocking; call it every loop.
void consoleService();

// One line of status for the 30 s STAT report in main.cpp.
const char* netStatusLine();

} // namespace net
