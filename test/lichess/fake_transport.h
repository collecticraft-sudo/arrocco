// SPDX-License-Identifier: GPL-3.0-or-later
// A scripted arrocco::lichess::Transport for the offline tests: no sockets, no threads, no clock
// but the one the test sets. Everything is a fixed array, like the code under test.
#pragma once
#include <cstring>

#include "arrocco/lichess/transport.h"

namespace arrocco_test {

class FakeTransport : public arrocco::lichess::Transport {
 public:
  static constexpr int kMaxAnswers = 16;
  static constexpr int kMaxStreams = 4;
  static constexpr int kStreamCapacity = 16384;
  static constexpr int kPathSize = 128;
  static constexpr int kBodySize = 2048;

  struct Answer {
    char path[kPathSize] = {};
    int status = 200;
    char body[kBodySize] = {};
    bool fail = false;  // the transport itself fails (no connection)
  };

  struct Stream {
    bool open = false;
    bool dead = false;  // readStream returns -1 once the pending bytes are gone
    char path[kPathSize] = {};
    char data[kStreamCapacity] = {};
    int length = 0;
    int taken = 0;
  };

  // ---------------------------------------------------------------- scripting

  void setNow(uint32_t ms) { now_ = ms; }
  void advance(uint32_t ms) { now_ += ms; }
  void setChunkSize(int bytes) { chunkSize_ = bytes; }
  // How long beginRequest() stays Busy before it answers.
  void setRequestDelay(uint32_t ms) { requestDelayMs_ = ms; }
  void failOpenStream(bool on) {
    failOpenStream_ = on;
    refusalStatus_ = 0;
  }
  // Refuse the next openStream() calls the way Lichess would, carrying an HTTP status the client
  // reads back through lastStreamStatus(). 0 turns the refusal off again.
  void refuseOpenStream(int httpStatus) {
    failOpenStream_ = httpStatus != 0;
    refusalStatus_ = httpStatus;
  }

  void answer(int status, const char* body, const char* path = nullptr) {
    if (answerCount_ >= kMaxAnswers) return;
    Answer& a = answers_[answerCount_++];
    a.status = status;
    a.fail = false;
    copyInto(a.body, sizeof a.body, body);
    copyInto(a.path, sizeof a.path, path);
  }
  void answerFailure() {
    if (answerCount_ >= kMaxAnswers) return;
    answers_[answerCount_++].fail = true;
  }

  // ---------------------------------------------------------------- what the code did
  const char* lastPath() const { return lastPath_; }
  const char* lastBody() const { return lastBody_; }
  const char* pathAt(int index) const { return index >= 0 && index < historyCount_ ? history_[index] : ""; }
  int requestCount() const { return requestCount_; }
  int openStreamCount() const {
    int n = 0;
    for (const Stream& s : streams_) {
      if (s.open) ++n;
    }
    return n;
  }
  const char* streamPath(int id) const {
    const Stream* s = stream(id);
    return s != nullptr ? s->path : "";
  }
  bool streamIsOpen(int id) const {
    const Stream* s = stream(id);
    return s != nullptr && s->open;
  }
  int streamOpenCalls() const { return streamOpenCalls_; }

  // ---------------------------------------------------------------- feeding a stream
  void push(int id, const char* text) {
    Stream* s = stream(id);
    if (s == nullptr || text == nullptr) return;
    const int size = static_cast<int>(std::strlen(text));
    if (s->length + size >= kStreamCapacity) return;
    std::memcpy(s->data + s->length, text, static_cast<size_t>(size));
    s->length += size;
  }
  void kill(int id) {
    Stream* s = stream(id);
    if (s != nullptr) s->dead = true;
  }
  // The id of the n-th stream still open, or kNoStream.
  int openStreamId(int index) const {
    for (int i = 0; i < kMaxStreams; ++i) {
      if (streams_[i].open && index-- == 0) return i + 1;
    }
    return kNoStream;
  }

  // ---------------------------------------------------------------- Transport
  uint32_t millis() const override { return now_; }

  bool beginRequest(arrocco::lichess::Method method, const char* path, const char* body, char* out,
                    int outSize) override {
    if (state_ != arrocco::lichess::RequestState::Idle) return false;
    lastMethod_ = method;
    copyInto(lastPath_, sizeof lastPath_, path);
    copyInto(lastBody_, sizeof lastBody_, body);
    if (historyCount_ < kMaxAnswers) copyInto(history_[historyCount_++], kPathSize, path);
    ++requestCount_;
    out_ = out;
    outSize_ = outSize;
    if (out != nullptr && outSize > 0) out[0] = '\0';
    startedMs_ = now_;
    state_ = arrocco::lichess::RequestState::Busy;
    settle();
    return true;
  }

  arrocco::lichess::RequestState requestState() const override {
    const_cast<FakeTransport*>(this)->settle();
    return state_;
  }
  int responseStatus() const override { return status_; }
  int responseLength() const override { return length_; }
  void endRequest() override {
    state_ = arrocco::lichess::RequestState::Idle;
    status_ = 0;
    length_ = 0;
    out_ = nullptr;
  }

  int lastStreamStatus() const override { return streamStatus_; }

  int openStream(const char* path) override {
    ++streamOpenCalls_;
    streamStatus_ = 0;
    if (failOpenStream_) {
      streamStatus_ = refusalStatus_;
      return kNoStream;
    }
    for (int i = 0; i < kMaxStreams; ++i) {
      if (streams_[i].open) continue;
      streams_[i] = Stream();
      streams_[i].open = true;
      copyInto(streams_[i].path, kPathSize, path);
      return i + 1;
    }
    return kNoStream;
  }

  int readStream(int id, char* out, int outSize) override {
    Stream* s = stream(id);
    if (s == nullptr || !s->open) return -1;
    int available = s->length - s->taken;
    if (available <= 0) return s->dead ? -1 : 0;
    int size = available;
    if (size > outSize) size = outSize;
    if (chunkSize_ > 0 && size > chunkSize_) size = chunkSize_;
    std::memcpy(out, s->data + s->taken, static_cast<size_t>(size));
    s->taken += size;
    return size;
  }

  void closeStream(int id) override {
    Stream* s = stream(id);
    if (s != nullptr) *s = Stream();
  }

 private:
  static void copyInto(char* out, int outSize, const char* text) {
    if (out == nullptr || outSize <= 0) return;
    int i = 0;
    if (text != nullptr) {
      for (; text[i] != '\0' && i + 1 < outSize; ++i) out[i] = text[i];
    }
    out[i] = '\0';
  }

  Stream* stream(int id) { return id >= 1 && id <= kMaxStreams ? &streams_[id - 1] : nullptr; }
  const Stream* stream(int id) const { return id >= 1 && id <= kMaxStreams ? &streams_[id - 1] : nullptr; }

  // Turns a Busy request into its scripted answer once the delay has passed.
  void settle() {
    if (state_ != arrocco::lichess::RequestState::Busy) return;
    if (static_cast<uint32_t>(now_ - startedMs_) < requestDelayMs_) return;
    if (nextAnswer_ >= answerCount_) {  // nothing scripted: a network failure
      state_ = arrocco::lichess::RequestState::Failed;
      return;
    }
    const Answer& a = answers_[nextAnswer_++];
    if (a.fail) {
      state_ = arrocco::lichess::RequestState::Failed;
      return;
    }
    status_ = a.status;
    length_ = static_cast<int>(std::strlen(a.body));
    if (out_ != nullptr && outSize_ > 0) {
      if (length_ > outSize_ - 1) length_ = outSize_ - 1;
      std::memcpy(out_, a.body, static_cast<size_t>(length_));
      out_[length_] = '\0';
    }
    state_ = arrocco::lichess::RequestState::Done;
  }

  Answer answers_[kMaxAnswers];
  Stream streams_[kMaxStreams];
  char history_[kMaxAnswers][kPathSize] = {};
  char lastPath_[kPathSize] = {};
  char lastBody_[kBodySize] = {};
  arrocco::lichess::Method lastMethod_ = arrocco::lichess::Method::Get;
  arrocco::lichess::RequestState state_ = arrocco::lichess::RequestState::Idle;
  char* out_ = nullptr;
  int outSize_ = 0;
  int status_ = 0;
  int length_ = 0;
  int answerCount_ = 0;
  int nextAnswer_ = 0;
  int requestCount_ = 0;
  int historyCount_ = 0;
  int streamOpenCalls_ = 0;
  int chunkSize_ = 0;
  int refusalStatus_ = 0;
  int streamStatus_ = 0;
  uint32_t now_ = 1000;
  uint32_t startedMs_ = 0;
  uint32_t requestDelayMs_ = 0;
  bool failOpenStream_ = false;
};

}  // namespace arrocco_test
