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
"""

import argparse
import base64
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

    def __init__(self, binary, hub):
        self.binary = binary
        self.hub = hub
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
        self.proc = subprocess.Popen(
            [self.binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            start_new_session=True)
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
            state["pid"] = self.server.sim.pid()
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
        if url.path == "/restart":
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
    args = parser.parse_args()

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
    sim = SimProcess(args.binary, hub)
    httpd.hub, httpd.sim, httpd.verbose = hub, sim, args.verbose

    stop = threading.Event()
    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, lambda *_: stop.set())

    sim.start()
    serving = threading.Thread(target=httpd.serve_forever, name="http", daemon=True)
    serving.start()

    url = "http://127.0.0.1:%d/" % args.port
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
        httpd.shutdown()
        httpd.server_close()
        sim.stop()
        print("stopped.", flush=True)


if __name__ == "__main__":
    main()
