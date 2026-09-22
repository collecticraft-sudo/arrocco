// SPDX-License-Identifier: GPL-3.0-or-later
#include "lichess_transport.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace arrocco_sim {
namespace {

using arrocco::lichess::Method;
using arrocco::lichess::RequestState;
using arrocco::lichess::Transport;

constexpr int kSocketTimeoutMs = 25000;

uint32_t monotonicMs() {
  static const auto start = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

void splitAddress(const std::string& address, std::string& host, std::string& port) {
  const std::string::size_type colon = address.rfind(':');
  if (colon == std::string::npos) {
    host = address;
    port = "8765";
    return;
  }
  host = address.substr(0, colon);
  port = address.substr(colon + 1);
}

int connectTo(const std::string& address, std::string& error) {
  std::string host, port;
  splitAddress(address, host, port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* list = nullptr;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &list) != 0 || list == nullptr) {
    error = "cannot resolve " + address;
    return -1;
  }
  int fd = -1;
  for (addrinfo* candidate = list; candidate != nullptr; candidate = candidate->ai_next) {
    fd = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (fd < 0) continue;
    timeval timeout{};
    timeout.tv_sec = kSocketTimeoutMs / 1000;
    timeout.tv_usec = (kSocketTimeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (::connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(list);
  if (fd < 0) error = "cannot reach the simulator proxy at " + address + " (is sim/server.py running?)";
  return fd;
}

bool sendAll(int fd, const char* data, size_t size) {
  while (size > 0) {
    const ssize_t sent = ::send(fd, data, size, 0);
    if (sent <= 0) return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

// One reply from the proxy. It always sends Content-Length, so there is no chunking to undo.
struct Reply {
  int status = 0;
  std::string body;
  std::string streamState;  // X-Arrocco-Stream
  std::string reason;       // X-Arrocco-Error
  int upstream = 0;         // X-Arrocco-Status: what LICHESS answered, not what the proxy did
  bool ok = false;
};

std::string lowered(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

Reply talk(const std::string& address, const char* method, const std::string& path,
           const std::string& body, std::string& error) {
  Reply reply;
  const int fd = connectTo(address, error);
  if (fd < 0) return reply;

  std::string request;
  request.reserve(256 + body.size());
  request += method;
  request += ' ';
  request += path;
  request += " HTTP/1.1\r\nHost: ";
  request += address;
  request += "\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: ";
  request += std::to_string(body.size());
  request += "\r\n\r\n";
  request += body;
  if (!sendAll(fd, request.data(), request.size())) {
    error = "the proxy closed the connection while we were asking";
    ::close(fd);
    return reply;
  }

  std::string raw;
  char chunk[4096];
  for (;;) {
    const ssize_t got = ::recv(fd, chunk, sizeof chunk, 0);
    if (got < 0) {
      error = "the proxy did not answer in time";
      ::close(fd);
      return reply;
    }
    if (got == 0) break;
    raw.append(chunk, static_cast<size_t>(got));
    if (raw.size() > 8u * 1024u * 1024u) break;
  }
  ::close(fd);

  const std::string::size_type headerEnd = raw.find("\r\n\r\n");
  if (raw.compare(0, 5, "HTTP/") != 0 || headerEnd == std::string::npos) {
    error = "the proxy sent something that is not HTTP";
    return reply;
  }
  const std::string::size_type firstSpace = raw.find(' ');
  if (firstSpace == std::string::npos) {
    error = "the proxy sent a broken status line";
    return reply;
  }
  reply.status = std::atoi(raw.c_str() + firstSpace + 1);
  reply.body = raw.substr(headerEnd + 4);

  // Only one header matters to us, and only for streams.
  std::string::size_type line = raw.find("\r\n");
  while (line != std::string::npos && line < headerEnd) {
    const std::string::size_type start = line + 2;
    const std::string::size_type stop = raw.find("\r\n", start);
    if (stop == std::string::npos || start >= headerEnd) break;
    const std::string header = raw.substr(start, stop - start);
    const std::string::size_type colon = header.find(':');
    if (colon != std::string::npos) {
      const std::string name = lowered(header.substr(0, colon));
      std::string value = header.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
      if (name == "x-arrocco-stream") reply.streamState = value;
      if (name == "x-arrocco-error") reply.reason = value;
      if (name == "x-arrocco-status") reply.upstream = std::atoi(value.c_str());
    }
    line = stop;
  }
  reply.ok = true;
  return reply;
}

}  // namespace

// The request in flight. It is shared with the worker thread so that endRequest() can walk away
// from a request that is still running without ever touching the caller's buffer again.
struct ProxyTransport::Job {
  std::mutex lock;
  bool abandoned = false;
  char* out = nullptr;
  int outSize = 0;
  int status = 0;
  int length = 0;
  std::atomic<int> state{static_cast<int>(RequestState::Busy)};
  std::string error;
};

ProxyTransport::ProxyTransport() {
  const char* fromEnv = std::getenv("ARROCCO_SIM_PROXY");
  address_ = (fromEnv != nullptr && *fromEnv != '\0') ? fromEnv : "127.0.0.1:8765";
  clock_ = &monotonicMs;
}

ProxyTransport::~ProxyTransport() {
  endRequest();
  for (Stream& stream : streams_) {
    if (stream.id >= 0) closeStream(stream.id);
  }
}

void ProxyTransport::setClock(uint32_t (*clock)()) { clock_ = clock != nullptr ? clock : &monotonicMs; }

uint32_t ProxyTransport::millis() const { return clock_(); }

bool ProxyTransport::proxyHasToken() {
  std::string error;
  const Reply reply = talk(address_, "GET", "/lichess/status", "", error);
  if (!reply.ok) {
    error_ = error;
    return false;
  }
  return reply.body.find("\"token\":true") != std::string::npos;
}

bool ProxyTransport::beginRequest(Method method, const char* path, const char* body, char* out,
                                  int outSize) {
  if (job_ != nullptr) return false;
  if (path == nullptr || out == nullptr || outSize <= 0) return false;
  out[0] = '\0';

  std::string payload;
  payload += (method == Method::Post) ? "POST " : "GET ";
  payload += path;
  payload += '\n';
  if (body != nullptr) payload += body;

  auto job = std::make_shared<Job>();
  job->out = out;
  job->outSize = outSize;
  job_ = job;
  const std::string address = address_;
  std::thread([job, address, payload]() {
    std::string error;
    const Reply reply = talk(address, "POST", "/lichess/request", payload, error);
    std::lock_guard<std::mutex> guard(job->lock);
    if (job->abandoned) return;
    if (!reply.ok) {
      job->error = error;
      job->state.store(static_cast<int>(RequestState::Failed));
      return;
    }
    if (reply.status == 599) {  // the proxy itself refused: no token, path not allowed, ...
      job->error = reply.body;
      job->state.store(static_cast<int>(RequestState::Failed));
      return;
    }
    int length = static_cast<int>(reply.body.size());
    if (length > job->outSize - 1) length = job->outSize - 1;
    std::memcpy(job->out, reply.body.data(), static_cast<size_t>(length));
    job->out[length] = '\0';
    job->status = reply.status;
    job->length = length;
    job->state.store(static_cast<int>(RequestState::Done));
  }).detach();
  return true;
}

RequestState ProxyTransport::requestState() const {
  if (job_ == nullptr) return RequestState::Idle;
  return static_cast<RequestState>(job_->state.load());
}

int ProxyTransport::responseStatus() const { return job_ == nullptr ? 0 : job_->status; }

int ProxyTransport::responseLength() const { return job_ == nullptr ? 0 : job_->length; }

void ProxyTransport::endRequest() {
  if (job_ == nullptr) return;
  {
    std::lock_guard<std::mutex> guard(job_->lock);
    job_->abandoned = true;
    if (!job_->error.empty()) error_ = job_->error;
  }
  job_.reset();
}

ProxyTransport::Stream* ProxyTransport::findStream(int id) {
  for (Stream& stream : streams_) {
    if (stream.id == id) return &stream;
  }
  return nullptr;
}

int ProxyTransport::openStream(const char* path) {
  streamStatus_ = 0;
  if (path == nullptr) return Transport::kNoStream;
  Stream* slot = findStream(-1);
  if (slot == nullptr) {
    error_ = "no free stream slot";
    return Transport::kNoStream;
  }
  std::string error;
  const Reply reply = talk(address_, "POST", "/lichess/stream", path, error);
  if (!reply.ok) {
    error_ = error;
    return Transport::kNoStream;
  }
  if (reply.status != 200) {
    // The proxy answers 599 for its own refusals and repeats Lichess's status in X-Arrocco-Status.
    // 429 must reach the client: it owes Lichess a full minute, not a two-second reconnect.
    streamStatus_ = reply.upstream;
    error_ = reply.body.empty() ? "the proxy refused the stream" : reply.body;
    return Transport::kNoStream;
  }
  const int id = std::atoi(reply.body.c_str());
  if (id <= 0) {
    error_ = "the proxy gave no stream id";
    return Transport::kNoStream;
  }
  error_.clear();
  slot->id = id;
  slot->finished = false;
  slot->lastPollMs = millis() - kStreamPollMs;  // readable at once
  return id;
}

int ProxyTransport::readStream(int id, char* out, int outSize) {
  if (id < 0 || out == nullptr || outSize <= 0) return -1;
  Stream* stream = findStream(id);
  if (stream == nullptr) return -1;
  if (stream->finished) return -1;
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - stream->lastPollMs) < kStreamPollMs) return 0;
  stream->lastPollMs = now;

  std::string path = "/lichess/read?id=" + std::to_string(id) + "&max=" + std::to_string(outSize) +
                     "&wait=0";
  std::string error;
  const Reply reply = talk(address_, "GET", path, "", error);
  if (!reply.ok) {
    error_ = error;
    stream->finished = true;
    return -1;
  }
  if (reply.status != 200) {
    error_ = reply.body.empty() ? "the proxy lost the stream" : reply.body;
    stream->finished = true;
    return -1;
  }
  int length = static_cast<int>(reply.body.size());
  if (length > outSize) {
    // We asked for at most outSize bytes. More than that means the proxy is not the proxy we
    // think it is, and quietly dropping the excess would cut a line in half and corrupt the JSON
    // that follows it. End the stream instead; the client reopens it and re-reads the game.
    error_ = "the proxy sent more than it was asked for";
    stream->finished = true;
    return -1;
  }
  if (length > 0) std::memcpy(out, reply.body.data(), static_cast<size_t>(length));
  if (reply.streamState == "closed") {
    // Hand over what arrived last; the next call reports the end.
    stream->finished = true;
    error_ = reply.reason.empty() ? "lichess closed the stream" : reply.reason;
    if (length == 0) return -1;
  }
  return length;
}

void ProxyTransport::closeStream(int id) {
  Stream* stream = findStream(id);
  if (stream == nullptr) return;
  stream->id = -1;
  stream->finished = false;
  std::string error;
  talk(address_, "POST", "/lichess/close", std::to_string(id), error);
}

ProxyTransport& lichessTransport() {
  static ProxyTransport transport;
  return transport;
}

}  // namespace arrocco_sim
