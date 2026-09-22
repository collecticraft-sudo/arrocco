#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco - turn a reference row of chess piece silhouettes into the 56x56 piece set.

The reference (assets/pieces/reference/staunton-row.png) is six solid black pieces on
white, in one row, at their REAL relative heights - a king twice the height of a pawn.
A chessboard needs the opposite: every piece has to fill the same square. So this script
segments the row, then rescales each piece on its own.

Scaling is not uniform. Scaling every piece to the same height would give the pawn a
king-sized ball; keeping the real heights would make it vanish. Online chess sets sit in
between, and kHeights below is that compromise: the tallest piece fills the cell and the
others keep a little of their real hierarchy. Bases all land on the same baseline.

Output is the same three bitmaps per piece that draw_pieces.py produces (mask, white-piece
contour, black-piece fill), so the firmware side does not change.

  python3 trace_reference.py --sheet out.png --board out2.png --header piece_bitmaps.h
"""

import argparse
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import draw_pieces as dp          # rasteriser helpers: distance transform, PNG, packing

SIZE = dp.SIZE                    # 56
SS = dp.SS                        # supersampling used by the rest of the pipeline
N = dp.N

# Where the piece sits inside its square: 4 px of margin all round is what the selection
# frame and the legal-move dots need.
TOP = 6.0
BASELINE = 51.0
MAX_W = 48.0

# Fraction of the available height each piece gets. The king fills it; the pawn keeps
# enough of its shortness to still read as a pawn, without disappearing.
HEIGHTS = {'K': 1.00, 'Q': 0.97, 'B': 0.93, 'N': 0.92, 'R': 0.86, 'P': 0.76}

# The reference is a realistic Staunton drawing, far slimmer than an icon: its king is
# 0.36 as wide as it is tall, where a chess-app icon is nearer 0.65. So x and y are scaled
# independently. Forcing a fixed WIDTH per piece distorts them unevenly, because the six
# reference drawings do not share an aspect ratio; what does hold a set together visually
# is the BASE. Every piece is therefore widened until its base is BASE_W px, which keeps
# the stretch moderate (1.5x to 1.9x) and makes the row look like one set.
BASE_W = 31.0
ORDER = ('K', 'Q', 'R', 'B', 'N', 'P')   # the order the pieces appear in the reference

# Every piece in the reference has a white rule just above the bottom plinth, and on most
# of them it reads as a moulding line. On the king and the queen it does not: their stem
# is narrow where it meets a wide base, so the gap makes the piece look cut in half. For
# those two the gap is closed before scaling - and only that one, between 55 % and 90 %
# of the height, so the crown line higher up and the plinth line below both survive.
CLOSE_BASE_GAP = ('K', 'Q')
GAP_BAND = (0.55, 0.90)


# ---------------------------------------------------------------- PNG in

def read_png(path):
    raw = open(path, 'rb').read()
    if raw[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    pos, idat, ihdr = 8, b'', None
    while pos < len(raw):
        ln = struct.unpack('>I', raw[pos:pos + 4])[0]
        tag = raw[pos + 4:pos + 8]
        if tag == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', raw[pos + 8:pos + 8 + ln])
        elif tag == b'IDAT':
            idat += raw[pos + 8:pos + 8 + ln]
        elif tag == b'IEND':
            break
        pos += 12 + ln
    w, h, depth, colour, _, _, interlace = ihdr
    if depth != 8 or interlace != 0 or colour not in (0, 2, 4, 6):
        raise SystemExit(f'{path}: need an 8-bit non-interlaced PNG, got depth={depth} '
                         f'colour={colour} interlace={interlace}')
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[colour]
    data = zlib.decompress(idat)
    stride = w * channels
    out = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        ft = data[pos]; pos += 1
        line = bytearray(data[pos:pos + stride]); pos += stride
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise SystemExit(f'{path}: unknown row filter {ft}')
        out.append(line)
        prev = line
    return w, h, channels, out


def to_binary(w, h, channels, rows, threshold=128):
    """1 where the pixel is dark. Transparent pixels count as background."""
    grid = [bytearray(w) for _ in range(h)]
    for y in range(h):
        row = rows[y]
        g = grid[y]
        for x in range(w):
            base = x * channels
            if channels >= 3:
                lum = (row[base] * 299 + row[base + 1] * 587 + row[base + 2] * 114) // 1000
                alpha = row[base + 3] if channels == 4 else 255
            else:
                lum = row[base]
                alpha = row[base + 1] if channels == 2 else 255
            g[x] = 1 if (alpha > 128 and lum < threshold) else 0
    return grid


# ---------------------------------------------------------------- segmenting

def segment(grid, w, h, expect=6, min_ink=40):
    """Split the row into pieces on the empty columns between them. Thin separator
    rules in the reference are light grey and have already fallen below the threshold;
    anything left that is only a few pixels of ink is ignored as speckle."""
    cols = [sum(grid[y][x] for y in range(h)) for x in range(w)]
    spans, start = [], None
    for x in range(w):
        if cols[x] > 0 and start is None:
            start = x
        elif cols[x] == 0 and start is not None:
            spans.append((start, x - 1)); start = None
    if start is not None:
        spans.append((start, w - 1))
    spans = [s for s in spans if sum(cols[s[0]:s[1] + 1]) >= min_ink]
    if len(spans) != expect:
        raise SystemExit(f'found {len(spans)} pieces in the reference, expected {expect}: '
                         f'{spans}')
    boxes = []
    for x0, x1 in spans:
        ys = [y for y in range(h) if any(grid[y][x] for x in range(x0, x1 + 1))]
        boxes.append((x0, x1, ys[0], ys[-1]))
    return boxes


def close_base_gap(grid, box):
    """Bridge the white rule between body and base: for every column that has ink on both
    sides of the gap, fill the gap. Columns where only the base is present stay empty, so
    the base keeps its outline."""
    x0, x1, y0, y1 = box
    height = y1 - y0 + 1
    lo = y0 + int(height * GAP_BAND[0])
    hi = y0 + int(height * GAP_BAND[1])
    gaps, run = [], None
    for y in range(lo, hi + 1):
        if sum(grid[y][x0:x1 + 1]) == 0:
            if run is None:
                run = y
        elif run is not None:
            gaps.append((run, y - 1)); run = None
    if run is not None:
        gaps.append((run, hi))
    filled = 0
    for top, bottom in gaps:
        if top == y0 or bottom >= y1:
            continue
        above, below = grid[top - 1], grid[bottom + 1]
        for y in range(top, bottom + 1):
            row = grid[y]
            for x in range(x0, x1 + 1):
                if above[x] and below[x]:
                    row[x] = 1
                    filled += 1
    return filled


# ---------------------------------------------------------------- rescale

def place(grid, box, letter):
    """Draw one piece into the N x N supersampled cell, scaled and sat on the baseline."""
    x0, x1, y0, y1 = box
    src_w, src_h = x1 - x0 + 1, y1 - y0 + 1
    want_h = (BASELINE - TOP) * HEIGHTS[letter]
    # Width of the foot: the widest row in the bottom eighth of the drawing.
    foot = max(sum(grid[y][x0:x1 + 1]) for y in range(y1 - src_h // 8, y1 + 1))
    sy_scale = want_h / src_h
    sx_scale = BASE_W / foot
    dst_w = src_w * sx_scale
    if dst_w > MAX_W:                             # never wider than the safe area
        sx_scale *= MAX_W / dst_w
        dst_w = MAX_W
    left = 28.0 - dst_w / 2.0                     # centred
    top = BASELINE - want_h                       # every piece on the same baseline

    out = [bytearray(N) for _ in range(N)]
    for gy in range(N):
        y = (gy + 0.5) / SS
        if y < top or y >= top + want_h:
            continue
        sy = y0 + int((y - top) / sy_scale)
        if sy > y1:
            continue
        srow = grid[sy]
        orow = out[gy]
        for gx in range(N):
            x = (gx + 0.5) / SS
            if x < left or x >= left + dst_w:
                continue
            sx = x0 + int((x - left) / sx_scale)
            if sx <= x1 and srow[sx]:
                orow[gx] = 1
    return out


def derive(sil):
    """mask / white-piece contour / black-piece fill, exactly as draw_pieces.py does."""
    mask = dp.grown(sil, dp.HALO)
    rim = dp.sub(sil, dp.shrunk(sil, dp.RIM))
    return dp.downsample(mask), dp.downsample(rim), dp.downsample(sil)


def build(reference):
    w, h, channels, rows = read_png(reference)
    grid = to_binary(w, h, channels, rows)
    boxes = segment(grid, w, h)
    pieces = {}
    for letter, box in zip(ORDER, boxes):
        if letter in CLOSE_BASE_GAP:
            close_base_gap(grid, box)
        pieces[letter] = derive(place(grid, box, letter))
    return pieces, boxes


# ---------------------------------------------------------------- output

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--reference', default='reference/staunton-row.png')
    ap.add_argument('--sheet')
    ap.add_argument('--board')
    ap.add_argument('--header')
    ap.add_argument('--boxes', action='store_true', help='just report what was found')
    a = ap.parse_args()

    pieces, boxes = build(a.reference)
    for letter, (x0, x1, y0, y1) in zip(ORDER, boxes):
        print(f'{letter}: x {x0}..{x1} ({x1 - x0 + 1} px)  y {y0}..{y1} ({y1 - y0 + 1} px)'
              f'  -> height {HEIGHTS[letter]:.2f}')
    if a.boxes:
        return 0

    # Hand the rendered bitmaps to draw_pieces.py's own sheet / board / header writers.
    dp.render = lambda letter: pieces[letter]
    if a.sheet:
        print('sheet', a.sheet, dp.sheet(a.sheet))
    if a.board:
        print('board', a.board, dp.board(a.board))
    if a.header:
        print('header', a.header, dp.header(a.header), 'bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
