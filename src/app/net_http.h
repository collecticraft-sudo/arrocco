// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco firmware - HTTPS to lichess.org: the device side of
// arrocco::lichess::Transport (lib/arrocco/src/arrocco/lichess/transport.h).
//
// Built on esp_http_client, which de-chunks Transfer-Encoding: chunked for us (Arduino's
// HTTPClient does not, and /api/stream/event is chunked) and which validates against the
// IDF certificate bundle, so nothing breaks when Let's Encrypt rotates its intermediates.
//
// HOW THE TWO RULES OF THE SEAM ARE KEPT
//   1. Nothing blocks. beginRequest() copies the job and hands it to a request task;
//      requestState() and readStream() only ever touch RAM. The loop task never waits on
//      a socket, so the app and the panel keep ticking.
//   2. One request at a time. A single request slot, and one mutex shared by the request
//      task, the stream connects and the OAuth exchange - so nothing ever overlaps on the
//      wire, which is what Lichess asks for.
//
// STREAMS
// Byte oriented, as the seam wants: line assembly is portable code above it. A reader
// task per stream pushes bytes into a ring in PSRAM; readStream() pops. A stream that
// drops, that is silent for more than 20 s (Lichess sends a blank line every ~7 s), or
// that overflows its ring is reported broken with -1 once the buffered bytes are drained.
// It is NOT silently reconnected: a transparent reconnect would splice a half-received
// line onto a fresh one, and the replay Lichess sends on a new event stream would arrive
// with no way for the caller to know. Reopening is the client's decision, above the seam.
// Backoff does live here, but only for the connect that openStream() starts.
//
// BUDGET
// Two concurrent TLS sessions are planned for (one long-lived stream plus one request),
// a third only in passing - which is exactly the Lichess flow: /api/stream/event plus one
// game stream, with a move POST crossing them. kMaxStreams is 2 for that reason.
#pragma once
#include <Arduino.h>
#include <stddef.h>

#include <arrocco/lichess/transport.h>

namespace net {

// Moves mbedTLS allocations from internal RAM to PSRAM. Must run before the first TLS
// session, i.e. at the very top of setup(): once a session exists it is too late.
void mbedtlsUsePsram();
bool mbedtlsOnPsram();

// Creates the mutex and the buffers and starts the request task. Call once from setup(),
// after mbedtlsUsePsram().
void httpBegin();

constexpr int kMaxStreams = 2;

class Esp32Transport final : public arrocco::lichess::Transport {
 public:
  uint32_t millis() const override;

  bool beginRequest(arrocco::lichess::Method method, const char* path, const char* body, char* out,
                    int outSize) override;
  arrocco::lichess::RequestState requestState() const override;
  int responseStatus() const override;
  int responseLength() const override;
  // Releases the slot. A request still in flight cannot be torn off its socket from
  // another task, so it is abandoned instead: its reply is dropped, the caller's buffer
  // is never touched again, and the slot reopens when the task comes back (a fraction of
  // a second, at worst the 15 s connect timeout). beginRequest() answers false until then.
  void endRequest() override;

  int openStream(const char* path) override;
  int readStream(int id, char* out, int outSize) override;
  void closeStream(int id) override;

  // --- diagnostics, not part of the seam ---
  bool streamOnline(int id) const;
  bool streamEnded(int id) const;
  uint32_t rateLimitWaitS() const; // seconds left in the 429 cool-down, 0 when free
  const char* lastError() const;
};

Esp32Transport& transport();

// The blocking form, for the two callers that already have a task of their own: the
// OAuth token exchange and the serial test commands. Never call it from the loop task.
// Returns the HTTP status or one of the kErr* values below.
int requestBlocking(const char* method, const char* path, const char* body, char* out,
                    size_t outSize, bool auth);

constexpr int kErrOffline = -1;     // no station connection
constexpr int kErrBusy = -2;        // another request holds the line
constexpr int kErrTransport = -3;   // DNS, TLS or socket failure
constexpr int kErrRateLimited = -4; // inside the cool-down a 429 imposed; nothing was sent

} // namespace net
