#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the live Lichess test with a proxy of its own.

It starts sim/server.py --proxy-only on a spare port, runs the C++ test against it, and stops the
proxy whatever happens. Without a token it prints a clear SKIP and exits 0, so `make test` is
green on a machine that has none.

The token is never read here either: server.py reads it (from ARROCCO_LICHESS_TOKEN or
~/.config/arrocco/lichess-token), keeps it in memory and puts it in the Authorization header
itself. This script only checks that one of the two EXISTS, and never prints it.
"""

import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "..", "..", "sim", "server.py")
TOKEN_FILE = os.path.expanduser("~/.config/arrocco/lichess-token")


def token_available():
    if os.environ.get("ARROCCO_LICHESS_TOKEN", "").strip():
        return True
    try:
        with open(TOKEN_FILE, "r", encoding="ascii") as handle:
            return bool(handle.read().strip())
    except OSError:
        return False


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def wait_ready(port, deadline):
    url = "http://127.0.0.1:%d/lichess/status" % port
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                return b'"token":true' in response.read()
        except (urllib.error.URLError, OSError):
            time.sleep(0.1)
    return False


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: live_run.py <path to the live test binary>")
    binary = sys.argv[1]
    if not os.access(binary, os.X_OK):
        sys.exit("live_run.py: %s is not executable" % binary)

    if not token_available():
        print("== live: SKIPPED - no Lichess token.")
        print("   Put one in ARROCCO_LICHESS_TOKEN or in %s (chmod 600) and run again." % TOKEN_FILE)
        print("   The offline tests cover everything that does not need the real server.")
        return 0

    port = free_port()
    proxy = subprocess.Popen([sys.executable, SERVER, "--proxy-only", "--port", str(port)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        if not wait_ready(port, time.monotonic() + 15):
            proxy.terminate()
            output = proxy.communicate(timeout=5)[0] or ""
            print("== live: the proxy did not come up with a token")
            print(output.strip())
            return 1
        print("== live: proxy on 127.0.0.1:%d (the token stays inside it)" % port)
        environment = dict(os.environ)
        environment["ARROCCO_SIM_PROXY"] = "127.0.0.1:%d" % port
        environment.pop("ARROCCO_LICHESS_TOKEN", None)  # the C++ side must never see it
        result = subprocess.run([binary], env=environment, check=False)
        return result.returncode
    finally:
        proxy.terminate()
        try:
            proxy.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proxy.kill()


if __name__ == "__main__":
    sys.exit(main())
