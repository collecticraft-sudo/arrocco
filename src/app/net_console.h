// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - the serial console (the test path for the network stack, plus the
// commands other modules add), and the worker task that carries every blocking call.
//
// Why a task: Transport::request() sits on a TLS handshake plus a round trip, and the
// Arduino loop has to keep ticking the app and the panel. Anything that touches the
// network from a command, and the OAuth token exchange, runs here instead. Nothing in
// this file is on the game path; it exists so the stack can be exercised over USB
// before the Lichess screens are built.
//
// Type 'help' on the serial monitor for the command list. No command ever prints the
// token or the OAuth code, and an unknown line is never echoed back (it may be a token
// pasted without 'token-set' in front of it).
#pragma once
#include <stdint.h>

namespace net {

// Starts the worker task. Call from setup(), after httpBegin().
void consoleBegin();

// Reads one serial line if there is one, and prints whatever the open test streams
// have produced. Non-blocking; call it every loop.
void consoleService();

// More commands, from other modules: `handler` gets the first word and the rest of the
// line (writable, NUL-terminated) and returns true when the word was its own; `help`
// prints its line of 'help'. Up to four. Call from setup().
using CommandHandler = bool (*)(const char* verb, char* rest);
using HelpPrinter = void (*)();
void consoleAddCommands(CommandHandler handler, HelpPrinter help);

// Lines typed so far, whatever they were: the sleep timer counts them as activity.
uint32_t consoleLines();

// One line of status for the 30 s STAT report in main.cpp.
const char* netStatusLine();
// Stack never used by the network tasks, for the same report.
const char* netStackLine();

} // namespace net
