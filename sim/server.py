#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco simulator — local web front end for arrocco-sim. Python 3 stdlib only.

Starts sim/build/arrocco-sim as a child process, serves sim/web/ on
http://127.0.0.1:8765, streams the child's events (frames, beeps, ...) to the browser
as Server-Sent Events and forwards the browser's pointer and control lines to the
child's stdin. Normally started by sim/run.sh.

HTTP surface (all on 127.0.0.1 only):
  GET  /                 the page (static files from sim/web/)
  GET  /events           Server-Sent Events: one JSON object per message
  POST /input            text/plain, one protocol line per line (see host/protocol.h)
  POST /restart          start the app again from scratch
  GET  /state            JSON summary (counters, controls, child alive)
  GET  /frame            JSON of the last frame; ?after=ID&timeout=S long-polls for a newer one
  GET  /frame.png        the last frame as a plain 1-bit PNG (no e-ink emulation)

  GET  /lichess/status   JSON: whether a token was found, how many streams are open
  POST /lichess/request  "<METHOD> <path>\\n<form body>"  -> the upstream status and body
  POST /lichess/stream   "<path>"                         -> a stream id
  GET  /lichess/read     ?id=N&max=M&wait=S               -> the bytes that have arrived
  POST /lichess/close    "<id>"

THE TOKEN NEVER LEAVES THIS FILE. It is read from ARROCCO_LICHESS_TOKEN or from
~/.config/arrocco/lichess-token and put into the Authorization header here; neither the C++
simulator nor the browser page ever sees it, and it is never logged or echoed. The proxy also
refuses any path that is not on the small allow list below, so the board:play flow is all that
can be reached through it: no chat, no seeks, no account settings.

With --proxy-only the server does the Lichess half alone (no simulator child, no web page):
that is what test/lichess uses for its live test.
"""

import argparse
import base64
import http.client
import json
import os
import queue
import re
import signal
import struct
import subprocess
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

SIM_DIR = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(SIM_DIR, "web")
DEFAULT_BINARY = os.path.join(SIM_DIR, "build", "arrocco-sim")
DEFAULT_PORT = 8765

LICHESS_HOST = "lichess.org"
TOKEN_FILE = os.path.expanduser("~/.config/arrocco/lichess-token")
MAX_RESPONSE = 256 * 1024
STREAM_BUFFER_LIMIT = 512 * 1024

# Only the board:play flow, and only what Arrocco actually calls. Everything else is refused
# here rather than trusted to the caller: a bug above must not be able to post a chat line,
# create a public seek or touch the account.
ALLOWED_REQUESTS = (
    ("GET", re.compile(r"^/api/account$")),
    # Read-only, and the one way to prove afterwards that we left no game open.
    ("GET", re.compile(r"^/api/account/playing$")),
    ("POST", re.compile(r"^/api/challenge/ai$")),
    ("POST", re.compile(r"^/api/challenge/[A-Za-z0-9_-]{2,30}$")),
    ("POST", re.compile(r"^/api/challenge/[A-Za-z0-9]{4,16}/(accept|decline|cancel)$")),
    ("POST", re.compile(r"^/api/board/game/[A-Za-z0-9]{6,16}/move/[a-h][1-8][a-h][1-8][qrbn]?$")),
    ("POST", re.compile(r"^/api/board/game/[A-Za-z0-9]{6,16}/(resign|abort|claim-victory)$")),
    ("POST", re.compile(r"^/api/board/game/[A-Za-z0-9]{6,16}/draw/(yes|no)$")),
)
ALLOWED_STREAMS = (
    re.compile(r"^/api/stream/event$"),
    re.compile(r"^/api/board/game/stream/[A-Za-z0-9]{6,16}$"),
)

# Only these lines ever reach the child: the page is the only intended client, but a
# strict grammar costs nothing and keeps a stray request from wedging the protocol.
INPUT_LINE = re.compile(
    r"^(?:touch (?:down|move|up) -?\d{1,5} -?\d{1,5}"
    r"|tick(?: \d{1,8})?"
    r"|set battery -?\d{1,3}"
    r"|set usb [01]"
    r"|set scale \d(?:\.\d{1,3})?"
    r"|frame)$"
)

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".json": "application/json",
}


def one_bit_png(bits, width, height):
    """PNG of the raw panel buffer. The buffer already is PNG's 1-bit grayscale layout
    (MSB first, 1 = white), so each scanline is just a filter byte plus the row."""
    stride = (width + 7) // 8
    raw = b"".join(b"\x00" + bits[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


class Hub:
    """What the server knows about the running app, plus the SSE subscribers."""

    def __init__(self):
        self.lock = threading.Lock()
        self.new_frame = threading.Condition(self.lock)
        self.subscribers = []
        self.frame = None          # last frame event (dict, with "id" and "data")
        self.frame_id = 0          # server-side counter: survives app restarts
        self.hello = None
        self.panel_on = False
        self.ignored_touches = 0
        self.alive = False
        self.exit_code = None
        self.controls = {"battery": 80, "usb": False, "scale": 1.0}

    def snapshot(self):
        # Caller holds the lock.
        return {
            "ev": "snapshot", "hello": self.hello, "controls": dict(self.controls),
            "panel_on": self.panel_on, "ignored_touches": self.ignored_touches,
            "alive": self.alive, "exit_code": self.exit_code, "frame": self.frame,
        }

    def subscribe(self):
        q = queue.Queue(maxsize=256)
        with self.lock:
            q.put_nowait(json.dumps(self.snapshot(), separators=(",", ":")).encode())
            self.subscribers.append(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            if q in self.subscribers:
                self.subscribers.remove(q)

    def _broadcast(self, event):
        # Caller holds the lock. A browser that stopped reading is dropped, not waited for.
        payload = json.dumps(event, separators=(",", ":")).encode()
        for q in list(self.subscribers):
            try:
                q.put_nowait(payload)
            except queue.Full:
                self.subscribers.remove(q)

    def publish(self, event):
        with self.lock:
            kind = event.get("ev")
            if kind == "frame":
                if not event.get("resend"):
                    self.frame_id += 1
                event["id"] = self.frame_id
                self.frame = event
                self.new_frame.notify_all()
            elif kind == "hello":
                self.hello = event
                self.alive = True
                self.exit_code = None
                self.panel_on = False
                self.ignored_touches = 0
            elif kind == "panel":
                self.panel_on = bool(event.get("on"))
            elif kind == "touch_ignored":
                self.ignored_touches = int(event.get("count", 0))
            elif kind == "state":
                for key in ("battery", "usb", "scale"):
                    if key in event:
                        self.controls[key] = event[key]
            elif kind == "exit":
                self.alive = False
                self.exit_code = event.get("code")
            self._broadcast(event)

    def close_all(self):
        with self.lock:
            for q in self.subscribers:
                try:
                    q.put_nowait(None)
                except queue.Full:
                    pass
            self.subscribers.clear()
            self.new_frame.notify_all()

    def wait_frame(self, after_id, timeout):
        deadline = time.monotonic() + timeout
        with self.lock:
            while self.frame is None or self.frame["id"] <= after_id:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.new_frame.wait(remaining)
            return self.frame


class SimProcess:
    """Owns the arrocco-sim child: start, feed, restart, and above all stop."""

    def __init__(self, binary, hub, port=DEFAULT_PORT):
        self.binary = binary
        self.hub = hub
        self.port = port
        self.lock = threading.Lock()
        self.proc = None
        self.generation = 0

    def start(self):
        with self.lock:
            self._start_locked()

    def _start_locked(self):
        # Own session: a Ctrl-C in the terminal reaches this server only, which then
        # shuts the child down in order. If the server is killed outright the child
        # still exits by itself, because its stdin reaches end of file.
        # The child is told where the Lichess proxy listens, never what the token is.
        env = dict(os.environ)
        env.pop("ARROCCO_LICHESS_TOKEN", None)
        env["ARROCCO_SIM_PROXY"] = "127.0.0.1:%d" % self.port
        self.proc = subprocess.Popen(
            [self.binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            start_new_session=True, env=env)
        self.generation += 1
        threading.Thread(target=self._pump, args=(self.proc, self.generation),
                         name="sim-stdout", daemon=True).start()
        controls = self.hub.controls
        for line in ("set battery %d" % int(controls["battery"]),
                     "set usb %d" % (1 if controls["usb"] else 0),
                     "set scale %.3f" % float(controls["scale"])):
            self._send_locked(line)

    def _pump(self, proc, generation):
        for raw in proc.stdout:
            try:
                event = json.loads(raw)
            except ValueError:
                sys.stderr.write("server.py: not JSON from arrocco-sim: %r\n" % raw[:120])
                continue
            if isinstance(event, dict):
                self.hub.publish(event)
        code = proc.wait()
        proc.stdout.close()               # closed here, by its only reader
        with self.lock:
            current = generation == self.generation
        if current:
            self.hub.publish({"ev": "exit", "code": code})

    def _send_locked(self, line):
        if self.proc is None or self.proc.poll() is not None:
            return False
        try:
            self.proc.stdin.write(line.encode("ascii") + b"\n")
            self.proc.stdin.flush()
            return True
        except (BrokenPipeError, OSError):
            return False

    def send_lines(self, lines):
        with self.lock:
            return all(self._send_locked(line) for line in lines)

    def restart(self):
        with self.lock:
            self.generation += 1          # the old pump must not report its exit
            self._stop_locked()
            self._start_locked()

    def stop(self):
        with self.lock:
            self.generation += 1
            self._stop_locked()

    def _stop_locked(self):
        proc, self.proc = self.proc, None
        if proc is None:
            return
        if proc.poll() is None:
            try:
                proc.stdin.write(b"quit\n")
                proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass
        try:
            proc.stdin.close()
        except OSError:
            pass
        # Ask, then insist, then force: nothing may be left running.
        for action in (None, proc.terminate, proc.kill):
            if action is not None:
                action()
            try:
                proc.wait(timeout=1.5)
                break
            except subprocess.TimeoutExpired:
                continue

    def pid(self):
        with self.lock:
            return self.proc.pid if self.proc and self.proc.poll() is None else None


def load_token():
    """The Lichess personal token, from the environment or from the 600 file. Never logged,
    never written anywhere, never sent to the child process or the browser."""
    token = os.environ.get("ARROCCO_LICHESS_TOKEN", "").strip()
    if token:
        return token
    try:
        with open(TOKEN_FILE, "r", encoding="ascii") as handle:
            return handle.read().strip()
    except OSError:
        return ""


class LichessStream:
    """One open ndjson stream. A thread reads it line by line (http.client de-chunks) and
    appends to a buffer; the simulator drains that buffer whenever it likes."""

    def __init__(self, stream_id, path, token):
        self.id = stream_id
        self.path = path
        self._token = token
        self._lock = threading.Lock()
        self._ready = threading.Condition(self._lock)
        self._buffer = bytearray()
        self._closed = False
        self._error = None
        self._status = 0      # what Lichess answered, so a 429 can be told from a dead link
        self._connection = None
        self._started = threading.Event()  # set once the status line is known (or it failed)
        self._thread = threading.Thread(target=self._run, name="lichess-stream-%d" % stream_id,
                                        daemon=True)
        self._thread.start()

    def _run(self):
        connection = None
        try:
            connection = http.client.HTTPSConnection(LICHESS_HOST, timeout=30)
            with self._lock:
                if self._closed:
                    return
                self._connection = connection
            connection.request("GET", self.path, headers={
                "Authorization": "Bearer " + self._token,
                "Accept": "application/x-ndjson",
                "User-Agent": "arrocco-sim",
            })
            response = connection.getresponse()
            if response.status != 200:
                body = response.read(512)
                with self._ready:
                    self._status = response.status
                    self._error = "HTTP %d %s" % (response.status,
                                                  body[:200].decode("utf-8", "replace").strip())
                    self._closed = True
                    self._ready.notify_all()
                self._started.set()
                return
            self._started.set()
            while True:
                line = response.readline()
                if not line:
                    break
                with self._ready:
                    if self._closed:
                        break
                    if len(self._buffer) + len(line) <= STREAM_BUFFER_LIMIT:
                        self._buffer += line
                    self._ready.notify_all()
        except Exception as error:                      # noqa: BLE001 - any failure ends the stream
            with self._ready:
                if self._error is None and not self._closed:
                    self._error = "%s: %s" % (type(error).__name__, error)
        finally:
            with self._ready:
                self._closed = True
                self._ready.notify_all()
            self._started.set()
            if connection is not None:
                try:
                    connection.close()
                except Exception:                       # noqa: BLE001
                    pass

    def read(self, limit, wait):
        """Up to `limit` bytes, waiting at most `wait` seconds for the first of them.
        Returns (bytes, finished)."""
        deadline = time.monotonic() + max(wait, 0.0)
        with self._ready:
            while not self._buffer and not self._closed:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._ready.wait(remaining)
            chunk = bytes(self._buffer[:limit])
            del self._buffer[:len(chunk)]
            finished = self._closed and not self._buffer
            return chunk, finished

    def error(self):
        with self._lock:
            return self._error

    def status(self):
        """Lichess's own HTTP status when it refused the stream, else 0. The client needs the
        one number 429: a rate-limited stream owes a full minute, not a 2 s reconnect."""
        with self._lock:
            return self._status

    def wait_started(self, timeout):
        """True when Lichess answered 200. A stream that Lichess refuses (a game the Board API
        will not touch, a bad token) must fail HERE, where the caller can be told why, instead of
        looking like a connection that keeps dropping."""
        self._started.wait(timeout)
        with self._lock:
            return self._error is None and not self._closed

    def close(self):
        with self._ready:
            self._closed = True
            connection = self._connection
            self._connection = None
            self._ready.notify_all()
        if connection is not None:
            try:
                connection.close()                      # unblocks the reading thread
            except Exception:                           # noqa: BLE001
                pass


class LichessProxy:
    """HTTPS to lichess.org on behalf of the simulator. One request at a time, as Lichess asks."""

    def __init__(self):
        self.token = load_token()
        self._request_lock = threading.Lock()
        self._streams = {}
        self._streams_lock = threading.Lock()
        self._next_id = 1

    def has_token(self):
        return bool(self.token)

    def stream_count(self):
        with self._streams_lock:
            return len(self._streams)

    @staticmethod
    def request_allowed(method, path):
        return any(method == m and pattern.match(path) for m, pattern in ALLOWED_REQUESTS)

    @staticmethod
    def stream_allowed(path):
        return any(pattern.match(path) for pattern in ALLOWED_STREAMS)

    def request(self, method, path, body):
        """(status, body bytes) from Lichess, or (None, reason) when the proxy itself refused."""
        if not self.token:
            return None, "no Lichess token (ARROCCO_LICHESS_TOKEN or %s)" % TOKEN_FILE
        if not self.request_allowed(method, path):
            return None, "path not allowed by the proxy: %s %s" % (method, path)
        headers = {
            "Authorization": "Bearer " + self.token,
            "Accept": "application/json",
            "User-Agent": "arrocco-sim",
        }
        if method == "POST":
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        with self._request_lock:
            connection = None
            try:
                connection = http.client.HTTPSConnection(LICHESS_HOST, timeout=20)
                connection.request(method, path, body=body if method == "POST" else None,
                                   headers=headers)
                response = connection.getresponse()
                return response.status, response.read(MAX_RESPONSE)
            except Exception as error:                  # noqa: BLE001
                return None, "%s: %s" % (type(error).__name__, error)
            finally:
                if connection is not None:
                    try:
                        connection.close()
                    except Exception:                   # noqa: BLE001
                        pass

    def open_stream(self, path):
        """(stream_id, None, 0) or (None, reason, upstream_status). The status is Lichess's own
        and is 0 when we never got that far; the caller passes it on so that a 429 becomes a
        minute of silence instead of a reconnect loop."""
        if not self.token:
            return None, "no Lichess token (ARROCCO_LICHESS_TOKEN or %s)" % TOKEN_FILE, 0
        if not self.stream_allowed(path):
            return None, "stream path not allowed by the proxy: %s" % path, 0
        with self._streams_lock:
            stream_id = self._next_id
            self._next_id += 1
            stream = LichessStream(stream_id, path, self.token)
            self._streams[stream_id] = stream
        # A few seconds at most: openStream() is the one call that waits, and it waits only
        # for Lichess's status line, not for any data.
        if not stream.wait_started(4):
            reason = stream.error() or "lichess did not open the stream"
            status = stream.status()
            self.close_stream(stream_id)
            return None, reason, status
        return stream_id, None, 0

    def read_stream(self, stream_id, limit, wait):
        with self._streams_lock:
            stream = self._streams.get(stream_id)
        if stream is None:
            return None
        return stream.read(limit, wait)

    def stream_error(self, stream_id):
        with self._streams_lock:
            stream = self._streams.get(stream_id)
        return None if stream is None else stream.error()

    def close_stream(self, stream_id):
        with self._streams_lock:
            stream = self._streams.pop(stream_id, None)
        if stream is not None:
            stream.close()

    def close_all(self):
        with self._streams_lock:
            streams = list(self._streams.values())
            self._streams.clear()
        for stream in streams:
            stream.close()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ArroccoSim/1"

    # ---- helpers ------------------------------------------------------------------

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def _allowed(self):
        """Same-machine, same-page only: refuse other Host names (DNS rebinding) and
        requests made by other sites' pages."""
        port = self.server.server_address[1]
        hosts = {"127.0.0.1:%d" % port, "localhost:%d" % port}
        if self.headers.get("Host", "") not in hosts:
            return False
        origin = self.headers.get("Origin")
        return origin is None or origin in {"http://" + h for h in hosts}

    def _reply(self, status, body=b"", content_type="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if body and self.command != "HEAD":
            self.wfile.write(body)

    def _reply_json(self, status, obj):
        self._reply(status, json.dumps(obj, separators=(",", ":")).encode(), "application/json")

    # ---- GET ------------------------------------------------------------------------

    def do_GET(self):
        if not self._allowed():
            return self._reply(403, b"forbidden\n")
        url = urlparse(self.path)
        hub = self.server.hub
        if url.path == "/events":
            return self._events()
        if url.path == "/state":
            with hub.lock:
                state = hub.snapshot()
                frame = state.pop("frame")
                state["frame_id"] = hub.frame_id
                state["last_frame"] = (
                    {k: v for k, v in frame.items() if k != "data"} if frame else None)
            state["pid"] = self.server.sim.pid() if self.server.sim is not None else None
            return self._reply_json(200, state)
        if url.path == "/frame":
            query = parse_qs(url.query)
            try:
                after = int(query.get("after", ["-1"])[0])
                timeout = min(max(float(query.get("timeout", ["0"])[0]), 0.0), 60.0)
            except ValueError:
                return self._reply(400, b"after and timeout must be numbers\n")
            frame = hub.wait_frame(after, timeout)
            if frame is None:
                return self._reply(204)
            return self._reply_json(200, frame)
        if url.path == "/lichess/status":
            proxy = self.server.proxy
            return self._reply_json(200, {"token": proxy.has_token(), "streams": proxy.stream_count()})
        if url.path == "/lichess/read":
            return self._lichess_read(url)
        if url.path == "/frame.png":
            with hub.lock:
                frame = hub.frame
            if frame is None:
                return self._reply(404, b"no frame yet\n")
            bits = base64.b64decode(frame["data"])
            return self._reply(200, one_bit_png(bits, frame["w"], frame["h"]), "image/png")
        return self._static(url.path)

    do_HEAD = do_GET

    def _static(self, path):
        if path == "/":
            path = "/index.html"
        full = os.path.realpath(os.path.join(WEB_DIR, path.lstrip("/")))
        if not full.startswith(os.path.realpath(WEB_DIR) + os.sep) or not os.path.isfile(full):
            return self._reply(404, b"not found\n")
        content_type = CONTENT_TYPES.get(os.path.splitext(full)[1].lower())
        if content_type is None:
            return self._reply(404, b"not found\n")
        with open(full, "rb") as handle:
            return self._reply(200, handle.read(), content_type)

    def _events(self):
        hub = self.server.hub
        q = hub.subscribe()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            while True:
                try:
                    payload = q.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": keep-alive\n\n")   # also detects a closed tab
                    self.wfile.flush()
                    continue
                if payload is None:
                    break
                self.wfile.write(b"data: " + payload + b"\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass
        finally:
            hub.unsubscribe(q)

    # ---- the Lichess proxy ------------------------------------------------------------

    def _proxy_error(self, reason, upstream=0):
        body = (reason or "proxy error").encode("utf-8", "replace") + b"\n"
        self.send_response(599)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Arrocco-Proxy", "error")
        if upstream:
            self.send_header("X-Arrocco-Status", str(int(upstream)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _lichess_read(self, url):
        query = parse_qs(url.query)
        try:
            stream_id = int(query.get("id", ["0"])[0])
            limit = min(max(int(query.get("max", ["4096"])[0]), 1), 65536)
            wait = min(max(float(query.get("wait", ["0"])[0]), 0.0), 10.0)
        except ValueError:
            return self._reply(400, b"id, max and wait must be numbers\n")
        result = self.server.proxy.read_stream(stream_id, limit, wait)
        if result is None:
            return self._reply(404, b"no such stream\n")
        chunk, finished = result
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(chunk)))
        self.send_header("X-Arrocco-Stream", "closed" if finished else "open")
        if finished:
            reason = self.server.proxy.stream_error(stream_id)
            if reason:
                self.send_header("X-Arrocco-Error", reason.replace("\r", " ").replace("\n", " ")[:200])
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if chunk and self.command != "HEAD":
            self.wfile.write(chunk)

    def _lichess_request(self, body):
        head, _, form = body.partition(b"\n")
        try:
            line = head.decode("ascii").strip()
        except UnicodeDecodeError:
            return self._reply(400, b"ASCII only\n")
        parts = line.split(" ")
        if len(parts) != 2 or parts[0] not in ("GET", "POST"):
            return self._reply(400, b"expected '<GET|POST> <path>' on the first line\n")
        status, payload = self.server.proxy.request(parts[0], parts[1], form)
        if status is None:
            return self._proxy_error(payload)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("X-Arrocco-Proxy", "ok")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if payload and self.command != "HEAD":
            self.wfile.write(payload)

    def _lichess_open(self, body):
        try:
            path = body.decode("ascii").strip()
        except UnicodeDecodeError:
            return self._reply(400, b"ASCII only\n")
        stream_id, reason, upstream = self.server.proxy.open_stream(path)
        if stream_id is None:
            return self._proxy_error(reason, upstream)
        return self._reply(200, b"%d\n" % stream_id)

    def _lichess_close(self, body):
        try:
            stream_id = int(body.decode("ascii").strip())
        except (UnicodeDecodeError, ValueError):
            return self._reply(400, b"expected a stream id\n")
        self.server.proxy.close_stream(stream_id)
        return self._reply(204)

    # ---- POST -----------------------------------------------------------------------

    def do_POST(self):
        if not self._allowed():
            return self._reply(403, b"forbidden\n")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length < 0 or length > 65536:
            self.close_connection = True
            return self._reply(413, b"body too large\n")
        body = self.rfile.read(length) if length else b""
        url = urlparse(self.path)
        if url.path == "/lichess/request":
            return self._lichess_request(body)
        if url.path == "/lichess/stream":
            return self._lichess_open(body)
        if url.path == "/lichess/close":
            return self._lichess_close(body)
        if url.path == "/restart":
            if self.server.sim is None:
                return self._reply(409, b"no simulator in this server (--proxy-only)\n")
            self.server.sim.restart()
            return self._reply(204)
        if url.path != "/input":
            return self._reply(404, b"not found\n")
        try:
            lines = [ln.strip() for ln in body.decode("ascii").splitlines() if ln.strip()]
        except UnicodeDecodeError:
            return self._reply(400, b"ASCII only\n")
        bad = [ln for ln in lines if not INPUT_LINE.match(ln)]
        if bad:
            return self._reply(400, ("not a simulator command: %s\n" % bad[0][:80]).encode())
        if self.server.sim is None:
            return self._reply(409, b"no simulator in this server (--proxy-only)\n")
        if not self.server.sim.send_lines(lines):
            return self._reply(409, b"the app is not running (POST /restart)\n")
        return self._reply(204)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description="Arrocco e-ink simulator server")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--binary", default=DEFAULT_BINARY, help="path to arrocco-sim")
    parser.add_argument("--open", action="store_true", help="open the page in the default browser")
    parser.add_argument("--verbose", action="store_true", help="log every HTTP request")
    parser.add_argument("--proxy-only", action="store_true",
                        help="serve the Lichess proxy alone: no simulator child, no web page")
    args = parser.parse_args()

    if not args.proxy_only:
        if not os.access(args.binary, os.X_OK):
            sys.exit("server.py: %s not found: run sim/run.sh (or sim/build.sh) first" % args.binary)
        if not os.path.isfile(os.path.join(WEB_DIR, "index.html")):
            sys.exit("server.py: %s/index.html is missing" % WEB_DIR)

    # Bind before starting the child: if the port is taken, nothing is left behind.
    try:
        httpd = Server(("127.0.0.1", args.port), Handler)
    except OSError as error:
        sys.exit("server.py: cannot listen on 127.0.0.1:%d (%s).\n"
                 "Is another simulator still running? Try: lsof -nP -iTCP:%d -sTCP:LISTEN"
                 % (args.port, error.strerror or error, args.port))

    hub = Hub()
    sim = None if args.proxy_only else SimProcess(args.binary, hub, args.port)
    httpd.hub, httpd.sim, httpd.verbose = hub, sim, args.verbose
    httpd.proxy = LichessProxy()

    stop = threading.Event()
    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, lambda *_: stop.set())

    if sim is not None:
        sim.start()
    serving = threading.Thread(target=httpd.serve_forever, name="http", daemon=True)
    serving.start()

    url = "http://127.0.0.1:%d/" % args.port
    if args.proxy_only:
        print("Arrocco Lichess proxy running: %s   token: %s   (Ctrl-C to stop)"
              % (url, "yes" if httpd.proxy.has_token() else "NO"), flush=True)
    else:
        print("Arrocco simulator running: %s   (Ctrl-C to stop)" % url, flush=True)
    if args.open:
        import webbrowser
        webbrowser.open(url)

    try:
        while not stop.wait(0.5):
            pass
    finally:
        print("\nstopping...", flush=True)
        hub.close_all()
        httpd.proxy.close_all()
        httpd.shutdown()
        httpd.server_close()
        if sim is not None:
            sim.stop()
        print("stopped.", flush=True)


if __name__ == "__main__":
    main()
