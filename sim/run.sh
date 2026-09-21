#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Arrocco simulator — the one command: build what changed, start the server, print the
# URL. Ctrl-C stops everything (server and app), nothing is left running.
#
# usage: sim/run.sh [--no-open] [--port N] [--verbose]
#   The page opens in the default browser when run from a terminal; --no-open (or a
#   non-interactive shell) only prints the URL.
set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

open_page=auto
server_args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --no-open) open_page=no ;;
    --open) open_page=yes ;;
    -h|--help)
      sed -n '3,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) server_args+=("$1") ;;
  esac
  shift
done

command -v python3 >/dev/null 2>&1 || {
  echo "sim/run.sh: python3 not found (any Python 3.8+ will do, no packages needed)" >&2
  exit 1
}

"$SIM_DIR/build.sh"

if [ "$open_page" = auto ]; then
  if [ -t 1 ]; then open_page=yes; else open_page=no; fi
fi
if [ "$open_page" = yes ]; then server_args+=("--open"); fi

# exec: signals go straight to the server, with no shell left in between to orphan it.
# (The ${arr[@]+...} form keeps bash 3.2, the one macOS ships, happy with an empty array.)
exec python3 "$SIM_DIR/server.py" ${server_args[@]+"${server_args[@]}"}
