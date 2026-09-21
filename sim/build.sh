#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Arrocco simulator — build script.
# Finds the Adafruit GFX sources PlatformIO already downloaded (the directory name has
# spaces and the PlatformIO env name changes over time), exposes them through the
# space-free symlink sim/build/gfx so that make can cope, then runs make.
# Nothing is downloaded and the library is never copied or modified.
#
# usage: sim/build.sh [make arguments, e.g. clean | CORE=0 | V=1]
#   ARROCCO_GFX_DIR=/path/to/Adafruit_GFX_Library   overrides the search
set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SIM_DIR/.." && pwd)"
BUILD_DIR="$SIM_DIR/build"

fail() {
  printf 'sim/build.sh: %s\n' "$*" >&2
  exit 1
}

command -v make >/dev/null 2>&1 || fail "make not found: install the Xcode command line tools (xcode-select --install)"
command -v "${CXX:-clang++}" >/dev/null 2>&1 || fail "${CXX:-clang++} not found: install the Xcode command line tools (xcode-select --install)"

gfx_dir=""
if [ -n "${ARROCCO_GFX_DIR:-}" ]; then
  [ -f "$ARROCCO_GFX_DIR/Adafruit_GFX.cpp" ] || fail "ARROCCO_GFX_DIR='$ARROCCO_GFX_DIR' does not contain Adafruit_GFX.cpp"
  gfx_dir="$(cd "$ARROCCO_GFX_DIR" && pwd)"
else
  # One copy per PlatformIO env (xiao_esp32s3, hwtest, ...). They are the same pinned
  # version, so any will do; prefer the most recently installed one.
  for candidate in "$REPO_DIR"/.pio/libdeps/*/"Adafruit GFX Library"; do
    [ -f "$candidate/Adafruit_GFX.cpp" ] || continue
    [ -f "$candidate/glcdfont.c" ] || continue
    [ -d "$candidate/Fonts" ] || continue
    if [ -z "$gfx_dir" ] || [ "$candidate/Adafruit_GFX.cpp" -nt "$gfx_dir/Adafruit_GFX.cpp" ]; then
      gfx_dir="$candidate"
    fi
  done
fi

if [ -z "$gfx_dir" ]; then
  cat >&2 <<EOF
sim/build.sh: Adafruit GFX Library not found under
    $REPO_DIR/.pio/libdeps/*/Adafruit GFX Library
The simulator compiles the very same library the firmware uses, and PlatformIO is
what downloads it. Run this once, then try again:

    cd "$REPO_DIR" && pio pkg install

EOF
  exit 1
fi

mkdir -p "$BUILD_DIR"
# -n: replace the link itself instead of descending into the directory it points to.
ln -sfn "$gfx_dir" "$BUILD_DIR/gfx"

gfx_version="$(sed -n 's/^version=//p' "$gfx_dir/library.properties" 2>/dev/null | head -n 1)"
printf 'sim/build.sh: Adafruit GFX %s <- %s\n' "${gfx_version:-unknown}" "$gfx_dir"

jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
# cd first and keep every path in the Makefile relative: make cannot handle spaces,
# and a maker may well clone the repo into "~/My Projects".
cd "$SIM_DIR"
make -j"$jobs" "$@"
