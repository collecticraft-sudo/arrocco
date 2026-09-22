// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco Lichess — the transport seam.
//
// Everything above this header is portable: it builds paths and bodies, reads ndjson lines and
// keeps the state machine. Everything below it is the platform's business: TLS, sockets, threads.
// On the Mac the simulator's python server proxies HTTPS (and owns the token); on the ESP32 an
// esp_http_client implementation will sit here.
//
// TWO RULES the implementations must keep:
//   1. Nothing blocks. beginRequest() starts a request and returns at once; readStream() returns
//      what is there right now. The UI calls LichessClient::poll() between two screen refreshes
//      and must never wait for the network.
//   2. ONE request at a time, because that is what Lichess allows. beginRequest() refuses while
//      another request is in flight; streams are separate (budget: one event stream plus one
//      game stream, and one request beside them).
//
// The token belongs to the implementation. Nothing above this seam ever sees it: the portable
// code passes a path and a form body, never a header.
#pragma once
#include <stdint.h>

namespace arrocco::lichess {

enum class Method : uint8_t { Get, Post };

enum class RequestState : uint8_t {
  Idle,    // no request: beginRequest() is free
  Busy,    // in flight
  Done,    // finished: responseStatus()/responseLength() are valid, call endRequest()
  Failed   // could not be sent or the connection broke; call endRequest()
};

class Transport {
 public:
  static constexpr int kNoStream = -1;

  virtual ~Transport() = default;

  // Milliseconds since some fixed point, the same clock Platform::millis() uses. Only differences
  // matter, and the state machine is written so that the 49-day wrap is harmless.
  virtual uint32_t millis() const = 0;

  // ---------------------------------------------------------------- the single request slot

  // Starts `method path` with `body` (an application/x-www-form-urlencoded string, or nullptr).
  // The response body lands NUL-terminated in `out`; a longer body is truncated, which is fine
  // for us because every field we read comes first in Lichess's JSON.
  // Returns false when the slot is busy or the request could not even be started.
  virtual bool beginRequest(Method method, const char* path, const char* body, char* out, int outSize) = 0;
  virtual RequestState requestState() const = 0;
  virtual int responseStatus() const = 0;  // HTTP status, valid in Done
  virtual int responseLength() const = 0;  // bytes written into the caller's buffer
  // Releases the slot. Aborts the request if it is still Busy. Always safe to call.
  virtual void endRequest() = 0;

  // ---------------------------------------------------------------- streams

  // Opens a GET stream. Returns a stream id, or kNoStream.
  virtual int openStream(const char* path) = 0;
  // Reads what has arrived: the number of bytes written into `out`, 0 when nothing is there yet,
  // or -1 when the stream is finished or broken (the caller then closes it).
  // Byte oriented rather than line oriented on purpose: the line assembly is portable code
  // (NdjsonReader) so it can be tested on split and partial lines, and an implementation on top
  // of esp_http_client hands out bytes anyway.
  virtual int readStream(int id, char* out, int outSize) = 0;
  virtual void closeStream(int id) = 0;

  // The HTTP status behind the last openStream() that returned kNoStream, or 0 when the
  // implementation cannot tell. It exists for ONE number: 429. A stream Lichess refused for rate
  // limiting must not be tried again two seconds later — it wants a full minute — and without
  // this the client cannot tell that refusal from a flapping link. Overriding it is optional.
  virtual int lastStreamStatus() const { return 0; }

  // Why the last call failed, in words the user can read ("This game cannot be played with the
  // Board API."), or "" when the implementation has nothing to add. Never anything secret: the
  // token does not live on this side of the seam. Valid until the next call.
  virtual const char* lastError() const { return ""; }
};

}  // namespace arrocco::lichess
