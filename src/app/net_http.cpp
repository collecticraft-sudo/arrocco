// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_http.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/platform.h>
#include <string.h>

#include "log.h"
#include "net_token.h"
#include "net_wifi.h"

namespace net {
namespace {

using arrocco::lichess::Method;
using arrocco::lichess::RequestState;

constexpr char kHost[] = "https://lichess.org";
constexpr char kUserAgent[] = "Arrocco/0.1";

constexpr uint32_t kConnectTimeoutMs = 15000;   // TLS handshake plus the first byte
constexpr uint32_t kStreamReadTimeoutMs = 1000; // a read with no data returns after this,
                                                // so the task checks stop/silence at 1 Hz
constexpr uint32_t kStreamSilenceMs = 20000;    // measured: Lichess blank line every 7.1 s
constexpr uint32_t kRateLimitWaitMs = 65000;    // 429: a full minute per the docs, plus slack
constexpr uint32_t kLockWaitMs = 30000;

constexpr uint32_t kBackoffFirstMs = 1000; // connect retries inside openStream's task
constexpr uint32_t kBackoffMaxMs = 8000;
constexpr uint8_t kConnectAttempts = 4;

constexpr size_t kRingBytes = 8192; // per stream, in PSRAM
constexpr size_t kReplyMax = 8192;  // the request task's own landing buffer, in PSRAM
constexpr size_t kBodyMax = 512;

constexpr uint32_t kTaskStack = 6144; // esp-tls handshake wants ~3.5 KB of it
constexpr UBaseType_t kTaskPrio = 2;
constexpr BaseType_t kTaskCore = 0; // core 1 runs the loop, the panel and the touch

SemaphoreHandle_t s_lock = nullptr; // the wire: one request at a time, as Lichess asks
SemaphoreHandle_t s_slotLock = nullptr;
SemaphoreHandle_t s_wake = nullptr;
SemaphoreHandle_t s_errLock = nullptr; // setError() is called from four tasks
bool s_psramTls = false;
// Written by the request task and by the stream tasks, read by the loop through
// rateLimitWaitS(): volatile so the read is never hoisted out of a poll.
volatile uint32_t s_gateUntilMs = 0;
char s_lastError[96] = {};

// --- the single request slot -----------------------------------------------------------

struct RequestSlot {
  volatile RequestState state = RequestState::Idle;
  volatile bool abandoned = false;
  char method[8] = {};
  char path[160] = {};
  char body[kBodyMax] = {};
  bool hasBody = false;
  char* out = nullptr; // the caller's buffer; only touched under s_slotLock
  int outSize = 0;
  // Written by the request task, polled by the loop right after it sees state == Done.
  volatile int status = 0;
  volatile int length = 0;
  char* reply = nullptr; // PSRAM, the task's own
};

RequestSlot s_req;

// --- streams -----------------------------------------------------------------------------

struct Stream {
  volatile bool inUse = false;
  volatile bool stop = false;
  volatile bool online = false;
  volatile bool ended = false; // dropped, silent, refused or overflowed: readStream -> -1
  char path[112] = {};
  SemaphoreHandle_t lock = nullptr;
  char* ring = nullptr;
  size_t head = 0;
  size_t tail = 0;
  size_t count = 0;
};

Stream s_streams[kMaxStreams];

void setError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void setError(const char* fmt, ...) {
  // The request task, the two stream tasks, the console worker and the loop all land
  // here. Without the mutex two of them interleave into one buffer and the diagnostic
  // string comes out as a mix of both.
  const bool held = s_errLock && xSemaphoreTake(s_errLock, pdMS_TO_TICKS(20)) == pdTRUE;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_lastError, sizeof(s_lastError), fmt, ap);
  va_end(ap);
  if (held) xSemaphoreGive(s_errLock);
}

// --- mbedTLS on PSRAM ---------------------------------------------------------------------

void* psramCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

void psramFree(void* p) {
  if (p) heap_caps_free(p); // free() spans both heaps, so a fallback block frees fine too
}

// --- the 429 gate -----------------------------------------------------------------------

bool gated(uint32_t now) {
  return s_gateUntilMs != 0 && static_cast<int32_t>(now - s_gateUntilMs) < 0;
}

void openGate(uint32_t now) {
  s_gateUntilMs = now + kRateLimitWaitMs;
  logLine("NET   HTTP 429: nothing goes out for %lu s",
          static_cast<unsigned long>(kRateLimitWaitMs / 1000));
}

bool takeLine(uint32_t waitMs) {
  return s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void giveLine() {
  if (s_lock) xSemaphoreGive(s_lock);
}

// --- one client ----------------------------------------------------------------------------

// `auth` is false only for the OAuth token exchange, which by definition has no token yet.
esp_http_client_handle_t makeClient(const char* method, const char* path, bool stream, bool auth) {
  char url[256];
  const int n = snprintf(url, sizeof(url), "%s%s", kHost, path);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(url)) {
    setError("path too long");
    return nullptr;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = (strcmp(method, "POST") == 0) ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  cfg.timeout_ms = static_cast<int>(kConnectTimeoutMs);
  cfg.crt_bundle_attach = esp_crt_bundle_attach; // Let's Encrypt rotates: never pin one root
  cfg.buffer_size = 1024;
  cfg.buffer_size_tx = 768; // the URL plus the headers, bearer token included
  cfg.keep_alive_enable = stream;

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    setError("out of memory for the HTTP client");
    return nullptr;
  }
  esp_http_client_set_header(c, "User-Agent", kUserAgent);
  esp_http_client_set_header(c, "Accept", stream ? "application/x-ndjson" : "application/json");
  if (auth) {
    char token[kTokenMax];
    if (tokenCopy(token, sizeof(token))) {
      char bearer[kTokenMax + 8];
      snprintf(bearer, sizeof(bearer), "Bearer %s", token);
      esp_http_client_set_header(c, "Authorization", bearer); // header only, never a query string
      memset(bearer, 0, sizeof(bearer));
    }
    memset(token, 0, sizeof(token));
  }
  return c;
}

// The blocking core. Runs on the request task, on the console worker or on the OAuth
// exchange - never on the Arduino loop.
int perform(const char* method, const char* path, const char* body, char* out, size_t outSize,
            bool auth, int* lengthOut) {
  if (lengthOut) *lengthOut = 0;
  if (out && outSize) out[0] = '\0';
  if (!wifiOnline()) {
    setError("offline");
    return kErrOffline;
  }
  const uint32_t now = ::millis();
  if (gated(now)) {
    setError("rate limited, %lu s left", static_cast<unsigned long>((s_gateUntilMs - now) / 1000UL));
    return kErrRateLimited;
  }
  if (!takeLine(kLockWaitMs)) {
    setError("another request is on the line");
    return kErrBusy;
  }

  int result = kErrTransport;
  esp_http_client_handle_t c = makeClient(method, path, false, auth);
  if (c) {
    const int bodyLen = body ? static_cast<int>(strlen(body)) : 0;
    if (bodyLen > 0)
      esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");

    // TLS may take the body a record at a time, so one write is not enough: keep going
    // until it is all out. A short write used to be reported as a failure.
    int sent = 0;
    bool writeOk = true;
    const esp_err_t err = esp_http_client_open(c, bodyLen);
    while (err == ESP_OK && writeOk && sent < bodyLen) {
      const int w = esp_http_client_write(c, body + sent, bodyLen - sent);
      if (w <= 0) writeOk = false;
      else sent += w;
    }
    if (err != ESP_OK) {
      setError("connect failed: %s", esp_err_to_name(err));
    } else if (!writeOk) {
      setError("could not send the body");
    } else if (esp_http_client_fetch_headers(c) < 0) {
      setError("no response headers");
    } else {
      result = esp_http_client_get_status_code(c);
      size_t got = 0;
      if (out && outSize > 1) {
        while (got + 1 < outSize) {
          const int r = esp_http_client_read(c, out + got, static_cast<int>(outSize - 1 - got));
          if (r <= 0) break;
          got += static_cast<size_t>(r);
        }
      }
      if (out && outSize) out[got] = '\0';
      if (lengthOut) *lengthOut = static_cast<int>(got);
      if (result == 429) openGate(::millis());
      if (result >= 400) setError("HTTP %d on %s", result, path);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
  }

  giveLine();
  return result;
}

// --- the request task ------------------------------------------------------------------------

void requestTask(void*) {
  for (;;) {
    if (xSemaphoreTake(s_wake, portMAX_DELAY) != pdTRUE) continue;
    if (s_req.state != RequestState::Busy) continue;

    int length = 0;
    const int status = perform(s_req.method, s_req.path, s_req.hasBody ? s_req.body : nullptr,
                               s_req.reply, kReplyMax, true, &length);
    memset(s_req.body, 0, sizeof(s_req.body));

    xSemaphoreTake(s_slotLock, portMAX_DELAY);
    if (s_req.abandoned) {
      // endRequest() came while this was in flight: the caller's buffer may be gone.
      s_req.abandoned = false;
      s_req.out = nullptr;
      s_req.state = RequestState::Idle;
    } else {
      int copied = 0;
      if (s_req.out && s_req.outSize > 1 && status > 0) {
        copied = length;
        if (copied > s_req.outSize - 1) copied = s_req.outSize - 1;
        memcpy(s_req.out, s_req.reply, static_cast<size_t>(copied));
        s_req.out[copied] = '\0';
      } else if (s_req.out && s_req.outSize > 0) {
        s_req.out[0] = '\0';
      }
      s_req.status = status;
      s_req.length = copied;
      s_req.state = (status > 0) ? RequestState::Done : RequestState::Failed;
    }
    xSemaphoreGive(s_slotLock);
  }
}

// --- stream plumbing --------------------------------------------------------------------------

void pushBytes(Stream& s, const char* data, size_t n) {
  if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    // Dropping the bytes here would splice two ndjson lines exactly as an overflow does,
    // only silently. readStream() holds this mutex for a memcpy, so failing to take it in
    // 50 ms means something is very wrong: end the stream and let the caller reopen.
    s.ended = true;
    return;
  }
  if (s.count + n > kRingBytes) {
    // Dropping bytes would splice two ndjson lines together and hand the caller
    // nonsense. Ending the stream is the honest answer: it reopens and resynchronises.
    xSemaphoreGive(s.lock);
    s.ended = true;
    logLine("NET   stream %s overflowed its buffer: ending it", s.path);
    return;
  }
  for (size_t i = 0; i < n; ++i) {
    s.ring[s.head] = data[i];
    s.head = (s.head + 1) % kRingBytes;
  }
  s.count += n;
  xSemaphoreGive(s.lock);
}

// The ring and its mutex are created once, in httpBegin(), and are never destroyed. The
// earlier code allocated them per stream and freed them when the reader task exited,
// which left a window: readStream() could pass its `s.ring && s.lock` check and then be
// inside xSemaphoreTake() while freeSlot() called vSemaphoreDelete() on that very mutex.
// 16 KB of PSRAM held for the life of the board buys that race away completely.
bool slotStorage(Stream& s) {
  if (!s.lock) s.lock = xSemaphoreCreateMutex();
  if (!s.lock) return false;
  if (!s.ring) {
    s.ring = static_cast<char*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s.ring) s.ring = static_cast<char*>(malloc(kRingBytes));
  }
  return s.ring != nullptr;
}

// Hands the slot back. Nothing is freed: only the indices and the flags are reset.
void releaseSlot(Stream& s) {
  if (s.lock && xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) == pdTRUE) {
    s.head = s.tail = s.count = 0;
    xSemaphoreGive(s.lock);
  } else {
    s.head = s.tail = s.count = 0;
  }
  s.online = false;
  s.inUse = false; // last: the slot is only reusable once nothing of it is left
}

// The wire lock is held only for the connect: a stream that is up must never keep it, or
// no move could ever be posted.
esp_http_client_handle_t streamConnect(Stream& s) {
  if (gated(::millis()) || !wifiOnline()) return nullptr;
  if (!takeLine(kLockWaitMs)) return nullptr;

  esp_http_client_handle_t c = makeClient("GET", s.path, true, true);
  bool ok = false;
  if (c) {
    if (esp_http_client_open(c, 0) != ESP_OK) {
      setError("stream connect failed: %s", s.path);
    } else if (esp_http_client_fetch_headers(c) < 0) {
      setError("stream: no response headers");
    } else {
      const int status = esp_http_client_get_status_code(c);
      if (status == 429) {
        openGate(::millis());
      } else if (status != 200) {
        setError("stream %s: HTTP %d", s.path, status);
      } else {
        ok = true;
        // Long reads from here on: a short timeout keeps the task responsive to stop.
        esp_http_client_set_timeout_ms(c, static_cast<int>(kStreamReadTimeoutMs));
      }
    }
    if (!ok) {
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      c = nullptr;
    }
  }
  giveLine();
  return c;
}

void streamTask(void* arg) {
  Stream& s = *static_cast<Stream*>(arg);

  esp_http_client_handle_t c = nullptr;
  uint32_t backoff = kBackoffFirstMs;
  for (uint8_t attempt = 0; attempt < kConnectAttempts && !s.stop; ++attempt) {
    c = streamConnect(s);
    if (c) break;
    if (attempt + 1 == kConnectAttempts) break; // no point sleeping after the last try
    vTaskDelay(pdMS_TO_TICKS(backoff));
    backoff = (backoff * 2 > kBackoffMaxMs) ? kBackoffMaxMs : backoff * 2;
  }

  if (c) {
    s.online = true;
    uint32_t lastByteMs = ::millis();
    while (!s.stop && !s.ended) {
      char ch = 0;
      // One byte at a time: esp_http_client keeps the rest of each socket read in its own
      // buffer, so only the first byte of a burst touches the socket. Reading a bigger
      // block would sit waiting for it to fill and add a second of latency to every move.
      const int r = esp_http_client_read(c, &ch, 1);
      if (r == 1) {
        lastByteMs = ::millis();
        pushBytes(s, &ch, 1);
        continue;
      }
      // A read that ran out its kStreamReadTimeoutMs with nothing to show answers
      // -ESP_ERR_HTTP_EAGAIN (-0x7007), NOT 0. Taking that for a broken connection - as
      // this loop first did - kills every stream after one idle second, which is most of
      // them: Lichess only speaks every ~7 s when nothing is happening. It means exactly
      // "nothing yet", so it falls through to the silence check like a zero read.
      if (r < 0 && r != -ESP_ERR_HTTP_EAGAIN) {
        logLine("NET   stream %s: connection broke", s.path);
        break;
      }
      if (esp_http_client_is_complete_data_received(c)) {
        logLine("NET   stream %s: closed by the server", s.path);
        break;
      }
      if (::millis() - lastByteMs > kStreamSilenceMs) {
        logLine("NET   stream %s silent for %lu s: treating it as dead", s.path,
                static_cast<unsigned long>(kStreamSilenceMs / 1000));
        break;
      }
      // A zero read can come back at once (a peer that closed while the chunk decoder
      // still expects more). Without this yield the loop spins at full speed on core 0,
      // whose idle task IS watched by the 5 s task watchdog, and the board panics.
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    s.online = false;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
  } else {
    logLine("NET   stream %s could not be opened", s.path);
  }

  s.ended = true;
  // The task stays alive until closeStream() raises stop, because the caller still has to
  // drain what arrived and to see the -1 that says the stream is over. Nothing is freed
  // here (slotStorage() owns the ring and the mutex for good), so releaseSlot() cannot
  // pull anything out from under a readStream() that is running on the loop task.
  while (!s.stop) vTaskDelay(pdMS_TO_TICKS(100));
  releaseSlot(s);
  vTaskDelete(nullptr);
}

bool validId(int id) { return id >= 0 && id < kMaxStreams; }

} // namespace

// --- public ------------------------------------------------------------------------------------

void mbedtlsUsePsram() {
  if (s_psramTls) return;
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
    logLine("NET   no PSRAM: mbedTLS stays in internal RAM (~42 KB per TLS session)");
    return;
  }
  // libmbedcrypto is built with MBEDTLS_PLATFORM_MEMORY, so the whole of mbedTLS - the
  // fixed 16 KB input and 16 KB output record buffers included - moves to PSRAM with this
  // one call. It has to happen before the first session: after that the buffers are
  // already placed. Nothing is rebuilt and no sdkconfig is touched.
  if (mbedtls_platform_set_calloc_free(psramCalloc, psramFree) == 0) {
    s_psramTls = true;
    logLine("NET   mbedTLS allocations moved to PSRAM");
  } else {
    logLine("NET   mbedTLS could not be moved to PSRAM: staying in internal RAM");
  }
}

bool mbedtlsOnPsram() { return s_psramTls; }

void httpBegin() {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  if (!s_slotLock) s_slotLock = xSemaphoreCreateMutex();
  if (!s_errLock) s_errLock = xSemaphoreCreateMutex();
  if (!s_wake) s_wake = xSemaphoreCreateBinary();
  if (!s_req.reply) {
    s_req.reply = static_cast<char*>(heap_caps_malloc(kReplyMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_req.reply) s_req.reply = static_cast<char*>(malloc(kReplyMax));
  }
  for (int i = 0; i < kMaxStreams; ++i) {
    s_streams[i].inUse = false;
    // 2 x 8 KB of PSRAM plus two mutexes, taken once and held for good: see slotStorage().
    if (!slotStorage(s_streams[i])) logLine("NET   stream slot %d has no buffer", i);
  }
  if (xTaskCreatePinnedToCore(requestTask, "arrocco-req", kTaskStack, nullptr, kTaskPrio, nullptr,
                              kTaskCore) != pdPASS)
    logLine("NET   the request task did not start: Lichess calls will all be refused");
}

uint32_t Esp32Transport::millis() const { return ::millis(); }

bool Esp32Transport::beginRequest(Method method, const char* path, const char* body, char* out,
                                  int outSize) {
  if (!path || !s_slotLock || !s_req.reply) return false;
  if (body && strlen(body) >= kBodyMax) {
    setError("request body too long");
    return false;
  }
  // A truncated path is a different request, silently: refuse it rather than POST a move
  // to half a game id.
  if (strlen(path) >= sizeof(s_req.path)) {
    setError("request path too long");
    return false;
  }
  xSemaphoreTake(s_slotLock, portMAX_DELAY);
  if (s_req.state != RequestState::Idle) {
    xSemaphoreGive(s_slotLock);
    return false; // one at a time, as the seam and Lichess both require
  }
  snprintf(s_req.method, sizeof(s_req.method), "%s", method == Method::Post ? "POST" : "GET");
  snprintf(s_req.path, sizeof(s_req.path), "%s", path);
  s_req.hasBody = body != nullptr && body[0] != '\0';
  if (s_req.hasBody) snprintf(s_req.body, sizeof(s_req.body), "%s", body);
  s_req.out = out;
  s_req.outSize = outSize;
  s_req.status = 0;
  s_req.length = 0;
  s_req.abandoned = false;
  s_req.state = RequestState::Busy;
  if (out && outSize > 0) out[0] = '\0';
  xSemaphoreGive(s_slotLock);
  xSemaphoreGive(s_wake);
  return true;
}

RequestState Esp32Transport::requestState() const { return s_req.state; }
int Esp32Transport::responseStatus() const { return s_req.status; }
int Esp32Transport::responseLength() const { return s_req.length; }

void Esp32Transport::endRequest() {
  if (!s_slotLock) return;
  xSemaphoreTake(s_slotLock, portMAX_DELAY);
  if (s_req.state == RequestState::Busy) {
    s_req.abandoned = true; // the task will drop the reply and free the slot itself
    s_req.out = nullptr;
    s_req.outSize = 0;
  } else {
    s_req.out = nullptr;
    s_req.outSize = 0;
    s_req.state = RequestState::Idle;
  }
  xSemaphoreGive(s_slotLock);
}

int Esp32Transport::openStream(const char* path) {
  if (!path) return kNoStream;
  if (!wifiOnline()) {
    setError("offline");
    return kNoStream;
  }
  for (int i = 0; i < kMaxStreams; ++i) {
    Stream& s = s_streams[i];
    if (s.inUse) continue;
    s.inUse = true;
    s.stop = false;
    s.online = false;
    s.ended = false;
    s.head = s.tail = s.count = 0;
    if (static_cast<size_t>(snprintf(s.path, sizeof(s.path), "%s", path)) >= sizeof(s.path)) {
      setError("stream path too long"); // never open a truncated, i.e. wrong, path
      releaseSlot(s);
      return kNoStream;
    }
    if (!slotStorage(s)) {
      setError("out of memory for the stream buffer");
      releaseSlot(s);
      return kNoStream;
    }
    char name[16];
    snprintf(name, sizeof(name), "arrocco-str%d", i);
    if (xTaskCreatePinnedToCore(streamTask, name, kTaskStack, &s, kTaskPrio, nullptr, kTaskCore) !=
        pdPASS) {
      setError("could not start the stream task");
      releaseSlot(s);
      return kNoStream;
    }
    logLine("NET   stream %d open on %s", i, s.path);
    return i;
  }
  setError("both stream slots are taken");
  return kNoStream;
}

int Esp32Transport::readStream(int id, char* out, int outSize) {
  if (!validId(id) || !out || outSize <= 0) return -1;
  Stream& s = s_streams[id];
  if (!s.inUse || !s.ring || !s.lock) return -1;

  int got = 0;
  if (xSemaphoreTake(s.lock, pdMS_TO_TICKS(5)) == pdTRUE) {
    while (got < outSize && s.count > 0) {
      out[got++] = s.ring[s.tail];
      s.tail = (s.tail + 1) % kRingBytes;
      --s.count;
    }
    xSemaphoreGive(s.lock);
  }
  if (got > 0) return got;        // drain first: the last bytes matter as much as the rest
  return s.ended ? -1 : 0;        // then, and only then, say the stream is over
}

void Esp32Transport::closeStream(int id) {
  if (!validId(id)) return;
  Stream& s = s_streams[id];
  if (!s.inUse) return;
  // Just the flag: the reader task closes the socket, frees the ring and releases the
  // slot. Freeing from here would pull the ring out from under it. The slot comes back
  // within kStreamReadTimeoutMs, so a close-then-reopen may briefly find it taken.
  s.stop = true;
  logLine("NET   stream %d closing", id);
}

bool Esp32Transport::streamOnline(int id) const {
  return validId(id) && s_streams[id].inUse && s_streams[id].online;
}

bool Esp32Transport::streamEnded(int id) const {
  return validId(id) && s_streams[id].inUse && s_streams[id].ended;
}

uint32_t Esp32Transport::rateLimitWaitS() const {
  const uint32_t now = ::millis();
  if (!gated(now)) return 0;
  return (s_gateUntilMs - now + 999U) / 1000U;
}

const char* Esp32Transport::lastError() const { return s_lastError; }

Esp32Transport& transport() {
  static Esp32Transport instance;
  return instance;
}

int requestBlocking(const char* method, const char* path, const char* body, char* out,
                    size_t outSize, bool auth) {
  return perform(method, path, body, out, outSize, auth, nullptr);
}

} // namespace net
