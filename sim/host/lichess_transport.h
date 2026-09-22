// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — arrocco::lichess::Transport on the Mac.
//
// It speaks PLAIN HTTP to sim/server.py on 127.0.0.1, which does the TLS to lichess.org and is
// the only thing that knows the token. Nothing here, and nothing in the browser page, ever sees
// it: this side sends a method, a path and a form body, and gets the status and the body back.
//
// Nothing blocks the simulator's event loop:
//   * a request runs on its own thread; beginRequest() returns at once and requestState() polls;
//   * a stream is drained by a short localhost GET that the proxy answers immediately, at most
//     once every kStreamPollMs, so readStream() costs a fraction of a millisecond.
//
// The address comes from ARROCCO_SIM_PROXY (server.py sets it for its child), default
// 127.0.0.1:8765.
#pragma once

#include <arrocco/lichess/transport.h>

#include <atomic>
#include <memory>
#include <string>

namespace arrocco_sim {

class ProxyTransport : public arrocco::lichess::Transport {
 public:
  static constexpr int kMaxStreams = 4;
  // One short localhost GET per stream at most this often: ten a second is far more than the
  // 0.45 s the panel needs for one refresh, and it keeps the proxy from being hammered.
  static constexpr uint32_t kStreamPollMs = 100;

  ProxyTransport();
  ~ProxyTransport() override;

  // Where the proxy is, e.g. "127.0.0.1:8765".
  const char* address() const { return address_.c_str(); }
  // Why the last call failed, when it did — including the sentence Lichess itself sent
  // ("This game cannot be played with the Board API."). Never holds anything secret.
  const char* lastTransportError() const { return error_.c_str(); }
  // Asks the proxy whether it found a token (GET /lichess/status). Blocks for a moment: call it
  // from a menu, not from the render loop.
  bool proxyHasToken();

  // The clock the state machine measures timeouts with. By default a monotonic clock started with
  // the process; bind it to Platform::millis() so that the simulator's notion of time is the one
  // used everywhere.
  void setClock(uint32_t (*clock)());

  uint32_t millis() const override;
  bool beginRequest(arrocco::lichess::Method method, const char* path, const char* body, char* out,
                    int outSize) override;
  arrocco::lichess::RequestState requestState() const override;
  int responseStatus() const override;
  int responseLength() const override;
  void endRequest() override;
  int openStream(const char* path) override;
  int readStream(int id, char* out, int outSize) override;
  void closeStream(int id) override;
  int lastStreamStatus() const override { return streamStatus_; }
  const char* lastError() const override { return error_.c_str(); }

 private:
  struct Job;
  struct Stream {
    int id = -1;  // the proxy's stream id, also what the client holds
    bool finished = false;
    uint32_t lastPollMs = 0;
  };

  Stream* findStream(int id);

  std::string address_;
  std::string error_;
  std::shared_ptr<Job> job_;
  Stream streams_[kMaxStreams];
  // The upstream HTTP status of the last refused openStream(), from the proxy's X-Arrocco-Status.
  // 0 when it never got that far (no proxy, no token, path not allowed).
  int streamStatus_ = 0;
  uint32_t (*clock_)() = nullptr;
};

// The transport the simulator uses. One per process.
ProxyTransport& lichessTransport();

}  // namespace arrocco_sim
