// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_console.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "log.h"
#include "net_http.h"
#include "net_token.h"
#include "net_wifi.h"

namespace net {
namespace {

constexpr uint32_t kWorkerStack = 6144; // esp-tls handshake wants ~3.5 KB of it
constexpr UBaseType_t kWorkerPrio = 2;
constexpr BaseType_t kWorkerCore = 0;
constexpr size_t kReplyMax = 4096; // lives in PSRAM: far too big for the task stack
// Only for echoing stream lines to the serial log, and only the first 300 characters of
// each are printed anyway. A longer line simply wraps onto a second log entry: this is a
// debug path, and 2 x 2 KB of internal RAM for it would not be worth the tidier output.
constexpr size_t kLineMax = 512;

enum class Job : uint8_t { Request, Oauth };

// Copies at most dstSize-1 characters and terminates. snprintf("%s") would do the same
// but makes the compiler complain about a truncation that is the whole point here.
void copyHead(char* dst, size_t dstSize, const char* src) {
  const size_t n = strnlen(src, dstSize - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

struct Cmd {
  Job job;
  char method[8];
  char path[160];
  char body[224];
};

QueueHandle_t s_queue = nullptr;
char s_input[256] = {};
size_t s_inputLen = 0;
int s_testStreams[kMaxStreams] = {-1, -1};
char s_line[kMaxStreams][kLineMax] = {};
size_t s_lineLen[kMaxStreams] = {};
char s_status[192] = {};

// --- the worker -------------------------------------------------------------------------

void workerTask(void*) {
  char* reply = static_cast<char*>(heap_caps_malloc(kReplyMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!reply) reply = static_cast<char*>(malloc(kReplyMax));
  for (;;) {
    Cmd cmd;
    if (s_queue && xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (cmd.job == Job::Oauth) {
        oauthService();
      } else if (reply) {
        const int status = requestBlocking(cmd.method, cmd.path, cmd.body[0] ? cmd.body : nullptr,
                                           reply, kReplyMax, true);
        memset(cmd.body, 0, sizeof(cmd.body)); // a body can carry a secret too
        if (status > 0) {
          logLine("NET   %s %s -> HTTP %d, %u bytes", cmd.method, cmd.path, status,
                  static_cast<unsigned>(strlen(reply)));
          // One reply must never be echoed: /api/token answers with the access_token
          // itself, and 'li-post /api/token ...' is a command anyone can type. Everything
          // else gets its first 400 characters printed, which is enough to see whether
          // the call did what it should.
          if (strncmp(cmd.path, "/api/token", 10) == 0) {
            logLine("NET   (body withheld: this endpoint answers with a token)");
          } else {
            char head[401];
            copyHead(head, sizeof(head), reply);
            logLine("NET   %s", head);
          }
        } else {
          logLine("NET   %s %s -> failed (%d): %s", cmd.method, cmd.path, status,
                  transport().lastError());
        }
      }
    }
    // The exchange is driven from here too, so a callback that lands between two
    // commands is still finished.
    if (oauthBusy()) oauthService();
  }
}

bool enqueue(const Cmd& cmd) {
  if (!s_queue) return false;
  return xQueueSend(s_queue, &cmd, 0) == pdTRUE;
}

void queueRequest(const char* method, const char* path, const char* body) {
  Cmd cmd = {};
  cmd.job = Job::Request;
  snprintf(cmd.method, sizeof(cmd.method), "%s", method);
  snprintf(cmd.path, sizeof(cmd.path), "%s", path);
  if (body) snprintf(cmd.body, sizeof(cmd.body), "%s", body);
  if (!enqueue(cmd)) logLine("NET   the worker queue is full, command dropped");
}

// --- commands ----------------------------------------------------------------------------

void printHelp() {
  logLine("CMD   help | net | heap");
  logLine("CMD   wifi-status | wifi-portal | wifi-forget");
  logLine("CMD   token-status | token-set <token> | token-clear");
  logLine("CMD   oauth-start | oauth-status | oauth-cancel");
  logLine("CMD   li-whoami | li-get <path> | li-post <path> [body]");
  logLine("CMD   stream-open <path> | stream-close <id>");
}

void printNet() {
  logLine("NET   wifi %s (%s) | mbedTLS on %s | token %s", wifiStateText(), wifiDetail(),
          mbedtlsOnPsram() ? "PSRAM" : "internal RAM",
          tokenPresent() ? "present" : "absent");
  const uint32_t wait = transport().rateLimitWaitS();
  if (wait) logLine("NET   rate limited: %lu s to go", static_cast<unsigned long>(wait));
  for (int i = 0; i < kMaxStreams; ++i) {
    if (s_testStreams[i] < 0) continue;
    const int id = s_testStreams[i];
    logLine("NET   stream %d: %s", id,
            transport().streamOnline(id) ? "up" : (transport().streamEnded(id) ? "ended" : "down"));
  }
  if (transport().lastError()[0]) logLine("NET   last error: %s", transport().lastError());
}

void cmdStreamOpen(const char* path) {
  for (int i = 0; i < kMaxStreams; ++i) {
    if (s_testStreams[i] >= 0) continue;
    const int id = transport().openStream(path);
    if (id < 0) {
      logLine("NET   stream-open failed: %s", transport().lastError());
      return;
    }
    s_testStreams[i] = id;
    return;
  }
  logLine("NET   no free test stream slot");
}

void cmdStreamClose(int id) {
  for (int i = 0; i < kMaxStreams; ++i) {
    if (s_testStreams[i] != id) continue;
    transport().closeStream(id);
    s_testStreams[i] = -1;
    s_lineLen[i] = 0;
    return;
  }
  logLine("NET   no test stream with id %d", id);
}

// Splits "verb rest": returns the rest (possibly empty, never null) and NUL-terminates
// the verb in place.
char* splitVerb(char* line) {
  char* sp = strchr(line, ' ');
  if (!sp) return line + strlen(line);
  *sp = '\0';
  ++sp;
  while (*sp == ' ') ++sp;
  return sp;
}

void dispatch(char* line) {
  char* rest = splitVerb(line);
  if (strcmp(line, "help") == 0) {
    printHelp();
  } else if (strcmp(line, "net") == 0) {
    printNet();
  } else if (strcmp(line, "heap") == 0) {
    logLine("NET   free heap %lu KB (min %lu KB), free PSRAM %lu KB",
            static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL),
            static_cast<unsigned long>(ESP.getMinFreeHeap() / 1024UL),
            static_cast<unsigned long>(ESP.getFreePsram() / 1024UL));
  } else if (strcmp(line, "wifi-status") == 0) {
    logLine("WIFI  %s - %s (AP name %s)", wifiStateText(), wifiDetail(), wifiApSsid());
  } else if (strcmp(line, "wifi-portal") == 0) {
    wifiOpenPortal();
  } else if (strcmp(line, "wifi-forget") == 0) {
    wifiForget();
  } else if (strcmp(line, "token-status") == 0) {
    logLine("TOKEN %s (%u characters)", tokenPresent() ? "present" : "absent",
            static_cast<unsigned>(tokenLength()));
  } else if (strcmp(line, "token-set") == 0) {
    // The value is never echoed and never logged - only its length.
    const bool ok = tokenSave(rest);
    memset(rest, 0, strlen(rest));
    if (!ok) logLine("TOKEN rejected: empty or longer than %u characters",
                     static_cast<unsigned>(kTokenMax - 1));
  } else if (strcmp(line, "token-clear") == 0) {
    tokenClear();
  } else if (strcmp(line, "oauth-start") == 0) {
    char url[400];
    if (oauthBegin(url, sizeof(url)))
      logLine("OAUTH open this on a phone on the same network (this becomes the QR):\n%s", url);
    else
      logLine("OAUTH could not start: %s", oauthError());
  } else if (strcmp(line, "oauth-status") == 0) {
    logLine("OAUTH %s%s%s", oauthStateText(), oauthError()[0] ? " - " : "", oauthError());
  } else if (strcmp(line, "oauth-cancel") == 0) {
    oauthCancel();
  } else if (strcmp(line, "li-whoami") == 0) {
    queueRequest("GET", "/api/account", nullptr);
  } else if (strcmp(line, "li-get") == 0) {
    if (rest[0] != '/') logLine("NET   usage: li-get /api/...");
    else queueRequest("GET", rest, nullptr);
  } else if (strcmp(line, "li-post") == 0) {
    char* body = splitVerb(rest);
    if (rest[0] != '/') logLine("NET   usage: li-post /api/... [a=1&b=2]");
    else queueRequest("POST", rest, body[0] ? body : nullptr);
  } else if (strcmp(line, "stream-open") == 0) {
    if (rest[0] != '/') logLine("NET   usage: stream-open /api/stream/event");
    else cmdStreamOpen(rest);
  } else if (strcmp(line, "stream-close") == 0) {
    cmdStreamClose(atoi(rest));
  } else if (line[0]) {
    logLine("CMD   unknown command '%s' - type help", line);
  }
}

// The seam hands out bytes, not lines: above it that is NdjsonReader's job. This is the
// throwaway equivalent for the serial log, so a stream can be watched before the Lichess
// screens exist. Blank lines are the ~7 s keep-alive and are not printed.
void drainStreams() {
  static char chunk[256];
  for (int i = 0; i < kMaxStreams; ++i) {
    const int id = s_testStreams[i];
    if (id < 0) continue;
    const int n = transport().readStream(id, chunk, sizeof(chunk));
    if (n < 0) {
      logLine("STRM %d ended: %s", id, transport().lastError());
      transport().closeStream(id);
      s_testStreams[i] = -1;
      s_lineLen[i] = 0;
      continue;
    }
    for (int k = 0; k < n; ++k) {
      const char c = chunk[k];
      if (c == '\n') {
        if (s_lineLen[i] > 0) {
          s_line[i][s_lineLen[i]] = '\0';
          char head[301];
          copyHead(head, sizeof(head), s_line[i]);
          logLine("STRM %d %s", id, head);
        }
        s_lineLen[i] = 0;
      } else if (c != '\r' && s_lineLen[i] + 1 < kLineMax) {
        s_line[i][s_lineLen[i]++] = c;
      }
    }
  }
}

} // namespace

void consoleBegin() {
  if (!s_queue) s_queue = xQueueCreate(4, sizeof(Cmd));
  xTaskCreatePinnedToCore(workerTask, "arrocco-net", kWorkerStack, nullptr, kWorkerPrio, nullptr,
                          kWorkerCore);
}

void consoleService() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (s_inputLen > 0) {
        s_input[s_inputLen] = '\0';
        dispatch(s_input);
        memset(s_input, 0, sizeof(s_input)); // a pasted token does not linger in RAM
        s_inputLen = 0;
      }
      break; // one command per loop: the app keeps ticking
    }
    if (s_inputLen + 1 < sizeof(s_input)) s_input[s_inputLen++] = static_cast<char>(c);
  }
  drainStreams();
  if (oauthState() == OauthState::Exchanging) {
    Cmd cmd = {};
    cmd.job = Job::Oauth;
    static uint32_t lastNudgeMs = 0;
    const uint32_t now = ::millis();
    if (now - lastNudgeMs > 1000) { // the worker also polls, this only shortens the wait
      lastNudgeMs = now;
      enqueue(cmd);
    }
  }
}

const char* netStatusLine() {
  snprintf(s_status, sizeof(s_status), "net %s (%s), TLS %s, token %s", wifiStateText(),
           wifiDetail(), mbedtlsOnPsram() ? "PSRAM" : "int", tokenPresent() ? "yes" : "no");
  return s_status;
}

} // namespace net
