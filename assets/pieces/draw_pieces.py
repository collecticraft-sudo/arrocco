#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco - the CollectiCraft piece set.

Each piece is a SILHOUETTE (a union of polygons and discs, drawn on a 56x56 square with
y pointing down) plus optional DETAIL shapes that are punched out of it. From that one
definition the script derives everything the firmware needs:

  mask   the silhouette grown by 2 px, painted white first, so a black piece still reads
         on a dithered dark square;
  ink    what is painted black on top: for a black piece the filled silhouette with the
         details knocked out in white, for a white piece only a 2 px contour plus the
         same details, so its inside stays white.

Shapes are rasterised at 4x and thresholded at 50 %, which is what the e-paper does to
anything we draw. Growing and shrinking use a chamfer distance transform on that 4x grid,
so a 2 px rim is really 2 px after the downsample.

No third-party module: the Mac has no PIL, no cairo. PNG writing is zlib + struct.

  python3 draw_pieces.py --sheet out.png     contact sheet, 1:1 and 2x, light and dark
  python3 draw_pieces.py --header out.h      the C++ bitmaps for lib/arrocco/src/arrocco/ui
  python3 draw_pieces.py --svg DIR           one editable SVG per piece
"""

import argparse
import struct
import sys
import zlib

SIZE = 56          # one square, in pixels
SS = 4             # supersampling factor
N = SIZE * SS
HALO = 2.0         # how far the white mask reaches past the silhouette, in pixels
RIM = 2.5          # contour thickness of a white piece, in pixels

# ---------------------------------------------------------------- shape primitives

def disc(cx, cy, r):
    return ('disc', cx, cy, r)


def poly(*pts):
    return ('poly',) + tuple(pts)


def band(x0, y0, x1, y1):
    """Axis-aligned rectangle, written as a polygon so everything takes one code path."""
    return poly((x0, y0), (x1, y0), (x1, y1), (x0, y1))


def mirror(shapes, axis=28.0):
    """The same shapes flipped about the vertical centre line."""
    out = []
    for s in shapes:
        if s[0] == 'disc':
            out.append(('disc', 2 * axis - s[1], s[2], s[3]))
        else:
            out.append(('poly',) + tuple((2 * axis - x, y) for (x, y) in s[1:]))
    return out


def inside(shape, x, y):
    if shape[0] == 'disc':
        _, cx, cy, r = shape
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    pts = shape[1:]
    hit = False
    n = len(pts)
    for i in range(n):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % n]
        if (y0 > y) != (y1 > y):
            xx = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if x < xx:
                hit = not hit
    return hit


# ---------------------------------------------------------------- the twelve pieces
#
# Proportions follow a Staunton set seen from the side: a wide foot, a tapering body and
# a head that says which piece it is at a glance. Everything sits inside x 12..44,
# y 8..51, which leaves the 4 px margin the selection frame and the move dots need.

def foot(half_w, top_w, y_top=51.0, y_rise=45.5):
    """The base every piece stands on: a wide slab with a short bevel."""
    return [
        poly((28 - half_w, y_top), (28 + half_w, y_top), (28 + half_w, y_top - 2.5),
             (28 - half_w, y_top - 2.5)),
        poly((28 - half_w, y_top - 2.2), (28 + half_w, y_top - 2.2),
             (28 + top_w, y_rise), (28 - top_w, y_rise)),
    ]


def pawn():
    sil = foot(13.0, 8.0)
    sil += [
        poly((20.0, 45.8), (36.0, 45.8), (33.0, 41.5), (23.0, 41.5)),   # collar over the foot
        poly((23.6, 42.0), (32.4, 42.0), (31.0, 33.0), (25.0, 33.0)),   # stem
        poly((20.5, 33.5), (35.5, 33.5), (34.0, 30.0), (22.0, 30.0)),   # ring under the head
        disc(28, 23.5, 7.6),                                            # head
    ]
    return sil, []


def rook():
    sil = foot(14.0, 9.5)
    sil += [
        poly((21.5, 45.8), (34.5, 45.8), (33.0, 41.0), (23.0, 41.0)),   # collar
        poly((23.2, 41.5), (32.8, 41.5), (34.0, 26.5), (22.0, 26.5)),   # body, flaring up
        band(18.5, 26.8, 37.5, 21.5),                                   # shoulder
        band(18.5, 22.0, 23.0, 13.5),                                   # three crenellations
        band(25.8, 22.0, 30.2, 13.5),
        band(33.0, 22.0, 37.5, 13.5),
    ]
    # A groove across the shoulder, so the top reads as stonework and not as a blob.
    return sil, [band(19.5, 25.2, 36.5, 23.6)]


def knight():
    """A horse's head facing left: the one piece that is not symmetric, and the one that
    has to be drawn rather than assembled. What makes it read as a horse at 56 px is the
    long muzzle jutting out to the left, the notch between the two ears and the stepped
    mane down the back - so those three get the pixels, and the neck stays plain."""
    sil = foot(15.0, 11.0)
    sil += [
        poly((19.4, 45.8), (36.0, 45.8), (35.2, 41.5), (19.8, 41.5)),   # collar
        poly(
            (19.4, 45.6), (19.8, 40.0), (20.4, 35.6),                   # chest, leaning forward: the
            (21.0, 32.0),                                               # neck arches. Throat latch here.
            (18.2, 34.0), (14.6, 35.2),                                 # jowl: the cheek swings down
            (10.6, 34.6), (7.8, 33.2),                                  # jaw, then the chin
            (6.6, 30.0), (6.8, 26.6),                                   # blunt nose, carried LOW: the
            (9.8, 23.4), (13.4, 20.2), (16.4, 17.4),                    # head tilts down about 40 deg
            (17.6, 14.6),                                               # forehead
            (19.8, 8.2), (21.0, 13.0),                                  # front ear, small and laid back
            (23.4, 13.0),                                               # notch: flat-bottomed, 2.4 px,
            (24.6, 8.0), (26.2, 13.2), (27.6, 16.4),                    # ... or it closes at threshold
            (30.0, 18.6), (28.6, 21.0),                                 # mane: five steps down the arch
            (31.8, 23.2), (30.4, 25.8),
            (33.4, 28.0), (32.0, 30.8),
            (34.4, 33.2), (33.2, 35.8),
            (35.0, 38.4), (36.0, 45.6),                                 # back of the neck to the collar
        ),
    ]
    details = [
        disc(17.4, 21.2, 2.0),                                          # eye, high and well back
        poly((12.0, 32.2), (7.8, 30.4), (8.5, 29.0), (12.7, 30.8)),     # mouth line along the muzzle
    ]
    return sil, details


def bishop():
    sil = foot(13.5, 9.0)
    sil += [
        poly((21.0, 45.8), (35.0, 45.8), (33.0, 41.0), (23.0, 41.0)),   # collar
        poly((23.5, 41.5), (32.5, 41.5), (35.0, 36.5), (21.0, 36.5)),   # waist
        disc(28, 28.5, 8.2),                                            # mitre, the bulb
        poly((20.4, 29.5), (35.6, 29.5), (29.6, 17.5), (26.4, 17.5)),   # mitre, tapering up
        disc(28, 15.6, 3.4),                                            # knob, wider than the taper
    ]
    # The bishop's slit, cut on the diagonal as on a real mitre.
    details = [poly((23.4, 25.6), (30.6, 17.8), (32.4, 19.4), (25.2, 27.2))]
    return sil, details


def coronet(y_band, xs, heights, half):
    """The queen's crown: sharp points with deep notches between them. At 56 px five
    balls would touch and fill in, so only the centre point keeps one."""
    out = []
    for x, y_tip in zip(xs, heights):
        out.append(poly((x - half, y_band), (x + half, y_band), (x, y_tip)))
    out.append(disc(xs[len(xs) // 2], heights[len(xs) // 2] + 0.6, 2.4))
    return out


def queen():
    sil = foot(14.5, 10.0)
    sil += [
        poly((21.0, 45.8), (35.0, 45.8), (33.5, 41.0), (22.5, 41.0)),   # collar
        poly((23.0, 41.5), (33.0, 41.5), (34.5, 30.0), (21.5, 30.0)),   # body
        poly((19.0, 30.5), (37.0, 30.5), (35.0, 26.0), (21.0, 26.0)),   # crown band, flaring
    ]
    sil += coronet(26.5, (19.8, 23.9, 28.0, 32.1, 36.2),
                   (19.5, 16.5, 12.8, 16.5, 19.5), 2.0)
    return sil, [band(22.0, 29.4, 34.0, 27.9)]                          # groove in the band


def king():
    sil = foot(14.5, 10.0)
    sil += [
        poly((21.0, 45.8), (35.0, 45.8), (33.5, 41.0), (22.5, 41.0)),   # collar
        poly((23.0, 41.5), (33.0, 41.5), (35.5, 29.0), (20.5, 29.0)),   # body
        band(20.0, 29.5, 36.0, 24.5),                                   # crown band
        poly((20.5, 25.0), (35.5, 25.0), (33.0, 18.5), (23.0, 18.5)),   # crown, tapering
        band(25.9, 19.0, 30.1, 8.0),                                    # cross, upright
        band(22.4, 15.6, 33.6, 11.8),                                   # cross, arms
    ]
    return sil, [band(21.0, 28.8, 35.0, 27.2)]                          # groove in the band


PIECES = {'P': pawn, 'R': rook, 'N': knight, 'B': bishop, 'Q': queen, 'K': king}
ORDER = ('K', 'Q', 'R', 'B', 'N', 'P')


# ---------------------------------------------------------------- rasterising

def rasterise(shapes):
    """Union of the shapes on the N x N supersampled grid, as a list of bytearrays."""
    grid = [bytearray(N) for _ in range(N)]
    for shape in shapes:
        if shape[0] == 'disc':
            _, cx, cy, r = shape
            y0 = max(0, int((cy - r) * SS)); y1 = min(N - 1, int((cy + r) * SS) + 1)
            x0 = max(0, int((cx - r) * SS)); x1 = min(N - 1, int((cx + r) * SS) + 1)
        else:
            xs = [p[0] for p in shape[1:]]; ys = [p[1] for p in shape[1:]]
            y0 = max(0, int(min(ys) * SS)); y1 = min(N - 1, int(max(ys) * SS) + 1)
            x0 = max(0, int(min(xs) * SS)); x1 = min(N - 1, int(max(xs) * SS) + 1)
        for gy in range(y0, y1 + 1):
            y = (gy + 0.5) / SS
            row = grid[gy]
            for gx in range(x0, x1 + 1):
                if not row[gx] and inside(shape, (gx + 0.5) / SS, y):
                    row[gx] = 1
    return grid


def distance_outside(grid):
    """Chamfer distance, in supersampled pixels, from every empty cell to the shape."""
    big = 10 ** 6
    d = [[0 if grid[y][x] else big for x in range(N)] for y in range(N)]
    for y in range(N):
        row = d[y]; up = d[y - 1] if y else None
        for x in range(N):
            v = row[x]
            if v == 0:
                continue
            if x: v = min(v, row[x - 1] + 5)
            if up is not None:
                v = min(v, up[x] + 5)
                if x: v = min(v, up[x - 1] + 7)
                if x + 1 < N: v = min(v, up[x + 1] + 7)
            row[x] = v
    for y in range(N - 1, -1, -1):
        row = d[y]; dn = d[y + 1] if y + 1 < N else None
        for x in range(N - 1, -1, -1):
            v = row[x]
            if v == 0:
                continue
            if x + 1 < N: v = min(v, row[x + 1] + 5)
            if dn is not None:
                v = min(v, dn[x] + 5)
                if x + 1 < N: v = min(v, dn[x + 1] + 7)
                if x: v = min(v, dn[x - 1] + 7)
            row[x] = v
    return d   # 5 units == one supersampled pixel


def invert(grid):
    return [bytearray(1 - v for v in row) for row in grid]


def grown(grid, px):
    d = distance_outside(grid)
    limit = px * SS * 5
    return [bytearray(1 if (grid[y][x] or d[y][x] <= limit) else 0 for x in range(N))
            for y in range(N)]


def shrunk(grid, px):
    d = distance_outside(invert(grid))          # distance from inside to the border
    limit = px * SS * 5
    return [bytearray(1 if (grid[y][x] and d[y][x] > limit) else 0 for x in range(N))
            for y in range(N)]


def downsample(grid):
    """4x4 box filter, thresholded at half: exactly what the panel will show."""
    out = [bytearray(SIZE) for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            n = 0
            for sy in range(SS):
                row = grid[y * SS + sy]
                for sx in range(SS):
                    n += row[x * SS + sx]
            out[y][x] = 1 if n * 2 >= SS * SS else 0
    return out


def sub(a, b):
    return [bytearray(1 if (a[y][x] and not b[y][x]) else 0 for x in range(len(a[0])))
            for y in range(len(a))]


def add(a, b):
    return [bytearray(1 if (a[y][x] or b[y][x]) else 0 for x in range(len(a[0])))
            for y in range(len(a))]


def render(letter):
    """Returns (mask, ink_white_piece, ink_black_piece) as 56x56 bitmaps."""
    sil_shapes, detail_shapes = PIECES[letter]()
    sil = rasterise(sil_shapes)
    details = rasterise(detail_shapes) if detail_shapes else [bytearray(N) for _ in range(N)]
    details = [bytearray(details[y][x] & sil[y][x] for x in range(N)) for y in range(N)]

    mask = grown(sil, HALO)
    rim = sub(sil, shrunk(sil, RIM))

    # A white piece is its contour plus the details; a black piece is filled, with the
    # details cut back out in white.
    white_ink = add(rim, details)
    black_ink = sub(sil, details)
    return downsample(mask), downsample(white_ink), downsample(black_ink)


# ---------------------------------------------------------------- output

def png(path, rows, scale=1):
    h = len(rows); w = len(rows[0])
    raw = b''
    for y in range(h):
        line = bytearray([0])
        for x in range(w):
            line += bytes([rows[y][x]]) * scale
        raw += bytes(line) * scale
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 0, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def hatch(x, y):
    """The dithered dark square the board draws, so the halo can be judged."""
    return 0 if ((x & 3) == ((y & 3) * 2) % 4) else 255


def sheet(path):
    """Every piece at 1:1 and at 2x, on a light and on a dark square."""
    pad = 4
    cell = SIZE + pad
    w = max(pad + len(ORDER) * cell, pad + len(ORDER) * (SIZE * 2 + pad))
    h = pad + 4 * cell + 2 * (SIZE * 2 + pad)
    img = [bytearray([255] * w) for _ in range(h)]

    def blit(px, py, mask, ink, dark, scale=1):
        for y in range(SIZE * scale):
            for x in range(SIZE * scale):
                sx, sy = x // scale, y // scale
                v = hatch(px + x, py + y) if dark else 255
                if mask[sy][sx]:
                    v = 255
                if ink[sy][sx]:
                    v = 0
                if 0 <= py + y < h and 0 <= px + x < w:
                    img[py + y][px + x] = v

    rendered = {L: render(L) for L in ORDER}
    for col, L in enumerate(ORDER):
        mask, wi, bi = rendered[L]
        x = pad + col * cell
        blit(x, pad + 0 * cell, mask, wi, False)          # white piece, light square
        blit(x, pad + 1 * cell, mask, bi, False)          # black piece, light square
        blit(x, pad + 2 * cell, mask, wi, True)           # white piece, dark square
        blit(x, pad + 3 * cell, mask, bi, True)           # black piece, dark square
    y2 = pad + 4 * cell
    for col, L in enumerate(ORDER):
        mask, wi, bi = rendered[L]
        x = pad + col * (SIZE * 2 + pad)
        blit(x, y2, mask, wi, False, 2)
        blit(x, y2 + SIZE * 2 + pad, mask, bi, True, 2)
    png(path, img)
    return w, h


def board(path):
    """The opening position drawn the way board_view.cpp draws it: 56 px squares at
    16,16, dithered dark squares, coordinates in the gutters. This is the real test."""
    W = H = 480
    img = [bytearray([255] * W) for _ in range(H)]
    rendered = {L: render(L) for L in ORDER}
    back = 'RNBQKBNR'
    for rank in range(8):          # rank 0 = rank 8, drawn at the top
        for file in range(8):
            x0 = 16 + file * SIZE
            y0 = 16 + rank * SIZE
            dark = ((file + rank) & 1) == 1
            for y in range(SIZE):
                for x in range(SIZE):
                    img[y0 + y][x0 + x] = hatch(x0 + x, y0 + y) if dark else 255
            letter = None
            white = False
            if rank == 0: letter, white = back[file], False
            elif rank == 1: letter, white = 'P', False
            elif rank == 6: letter, white = 'P', True
            elif rank == 7: letter, white = back[file], True
            if letter:
                mask, wi, bi = rendered[letter]
                ink = wi if white else bi
                for y in range(SIZE):
                    for x in range(SIZE):
                        if mask[y][x]: img[y0 + y][x0 + x] = 255
                        if ink[y][x]: img[y0 + y][x0 + x] = 0
    for x in range(14, 16 + 8 * SIZE + 2):
        for y in (14, 15, 16 + 8 * SIZE, 17 + 8 * SIZE):
            img[y][x] = 0
    for y in range(14, 18 + 8 * SIZE):
        for x in (14, 15, 16 + 8 * SIZE, 17 + 8 * SIZE):
            img[y][x] = 0
    png(path, img)
    return W, H


def pack(bitmap):
    """56x56 -> 7 bytes per row, MSB first: the layout Adafruit_GFX drawBitmap wants."""
    out = bytearray()
    for y in range(SIZE):
        for byte in range(SIZE // 8):
            v = 0
            for bit in range(8):
                if bitmap[y][byte * 8 + bit]:
                    v |= 0x80 >> bit
            out.append(v)
    return out


def header(path):
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later',
             '// Generated by assets/pieces/draw_pieces.py - do not edit by hand.',
             '// The CollectiCraft piece set: 56x56, 1 bit, 7 bytes per row, MSB first.',
             '#pragma once', '#include <cstdint>', '', 'namespace arrocco::ui::art {', '',
             f'constexpr int kPieceSize = {SIZE};',
             f'constexpr int kPieceStride = {SIZE // 8};', '']
    for L in ORDER:
        mask, wi, bi = render(L)
        for name, bm in (('Mask', mask), ('White', wi), ('Black', bi)):
            data = pack(bm)
            body = ', '.join(f'0x{b:02X}' for b in data)
            wrapped = []
            while body:
                cut = body.rfind(', ', 0, 96)
                if cut < 0 or len(body) < 96:
                    wrapped.append(body); break
                wrapped.append(body[:cut + 1]); body = body[cut + 2:]
            lines.append(f'constexpr uint8_t k{L}{name}[{len(data)}] = {{')
            lines += ['    ' + w for w in wrapped]
            lines.append('};')
    lines += ['', '}  // namespace arrocco::ui::art', '']
    open(path, 'w').write('\n'.join(lines))
    return sum(len(pack(b)) for L in ORDER for b in render(L))


def svg(dirname):
    """One editable SVG per piece, with the ink and mask groups docs/pezzi.md describes."""
    import os
    os.makedirs(dirname, exist_ok=True)
    for L in ORDER:
        sil_shapes, detail_shapes = PIECES[L]()
        def paths(shapes, fill):
            out = []
            for s in shapes:
                if s[0] == 'disc':
                    out.append(f'    <circle cx="{s[1]:.2f}" cy="{s[2]:.2f}" r="{s[3]:.2f}" fill="{fill}"/>')
                else:
                    pts = ' '.join(f'{x:.2f},{y:.2f}' for (x, y) in s[1:])
                    out.append(f'    <polygon points="{pts}" fill="{fill}"/>')
            return out
        body = ['<?xml version="1.0" encoding="UTF-8"?>',
                '<!-- SPDX-License-Identifier: GPL-3.0-or-later -->',
                '<!-- Arrocco / CollectiCraft piece set. Generated by draw_pieces.py;',
                '     edit freely, then rasterise with the same script. -->',
                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56" width="56" height="56">',
                '  <g id="mask" fill="#ffffff" stroke="#ffffff" stroke-width="4" stroke-linejoin="round">']
        body += paths(sil_shapes, '#ffffff')
        body += ['  </g>', '  <g id="ink">']
        body += paths(sil_shapes, '#000000')
        if detail_shapes:
            body += paths(detail_shapes, '#ffffff')
        body += ['  </g>', '</svg>', '']
        open(os.path.join(dirname, f'{L}.svg'), 'w').write('\n'.join(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sheet')
    ap.add_argument('--board')
    ap.add_argument('--header')
    ap.add_argument('--svg')
    a = ap.parse_args()
    if not (a.sheet or a.board or a.header or a.svg):
        ap.error('nothing to do: pass --sheet, --board, --header or --svg')
    if a.sheet:
        w, h = sheet(a.sheet)
        print(f'sheet {a.sheet} {w}x{h}')
    if a.board:
        w, h = board(a.board)
        print(f'board {a.board} {w}x{h}')
    if a.header:
        n = header(a.header)
        print(f'header {a.header} {n} bytes of bitmaps')
    if a.svg:
        svg(a.svg)
        print(f'svg in {a.svg}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
