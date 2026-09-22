#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build Arrocco's offline puzzle pack from the Lichess CC0 puzzle database.

Usage:
    python3 tools/make_puzzle_pack.py /path/to/lichess_db_puzzle.csv.zst

The .zst is ~300 MB and is NOT part of the repository; download it from
https://database.lichess.org/#puzzles (CC0 1.0).  The file is streamed, never
loaded into RAM.  The output is deterministic: the same input always produces
byte-identical files.

Written files
    lib/arrocco/src/arrocco/puzzles/puzzle_data.h    generated header (count, sizes, version)
    lib/arrocco/src/arrocco/puzzles/puzzle_data.cpp  the two const byte arrays
    test/puzzles/puzzle_reference.h                  raw Lichess rows for the native test

Only the Python standard library is used (compression.zstd needs Python >= 3.14).
"""

import argparse
import heapq
import io
import os
import sys

try:
    import compression.zstd as zstd
except ImportError:  # pragma: no cover - only hit on older interpreters
    sys.exit("python3 >= 3.14 is required (compression.zstd is missing)")

FORMAT_VERSION = 1

# --------------------------------------------------------------------------- themes
#
# A puzzle carries a whole bag of Lichess themes; we keep exactly one label, the
# first of this list that the bag contains.  The order is "most instructive
# first": a back-rank mate in one is more usefully labelled "Back-rank mate"
# than "Mate in 1".  The C++ enum is generated from this same table.

THEMES = [
    # (lichess theme, C++ enumerator, English label shown to the user)
    ("smotheredMate",     "SmotheredMate",     "Smothered mate"),
    ("backRankMate",      "BackRankMate",      "Back-rank mate"),
    ("mateIn1",           "MateIn1",           "Mate in 1"),
    ("mateIn2",           "MateIn2",           "Mate in 2"),
    ("mateIn3",           "MateIn3",           "Mate in 3"),
    ("doubleCheck",       "DoubleCheck",       "Double check"),
    ("fork",              "Fork",              "Fork"),
    ("pin",               "Pin",               "Pin"),
    ("skewer",            "Skewer",            "Skewer"),
    ("discoveredAttack",  "DiscoveredAttack",  "Discovered attack"),
    ("deflection",        "Deflection",        "Deflection"),
    ("attraction",        "Attraction",        "Attraction"),
    ("clearance",         "Clearance",         "Clearance"),
    ("interference",      "Interference",      "Interference"),
    ("trappedPiece",      "TrappedPiece",      "Trapped piece"),
    ("hangingPiece",      "HangingPiece",      "Hanging piece"),
    ("sacrifice",         "Sacrifice",         "Sacrifice"),
    ("promotion",         "Promotion",         "Promotion"),
    ("advancedPawn",      "AdvancedPawn",      "Advanced pawn"),
    ("zugzwang",          "Zugzwang",          "Zugzwang"),
    ("defensiveMove",     "DefensiveMove",     "Defensive move"),
    ("quietMove",         "QuietMove",         "Quiet move"),
]
THEME_INDEX = {name: i for i, (name, _, _) in enumerate(THEMES)}

# --------------------------------------------------------------------------- selection

RATING_MIN = 600
RATING_MAX = 2000
BAND_WIDTH = 100
BAND_COUNT = (RATING_MAX - RATING_MIN) // BAND_WIDTH          # 14 bands: 600-699 .. 1900-1999
PER_BAND = 250                                                 # 14 * 250 = 3500 puzzles
MAX_PLIES = 12                                                 # blunder + solution, total
# The pack stores no halfmove clock: every position comes back as "0 1", because a
# clock costs a byte per puzzle and a solution is far too short to reach the 50-move
# rule.  That only holds while the clock a puzzle starts from plus its own plies
# stays under 100, so it is enforced here rather than assumed.  On the September
# 2026 database the worst case is 95, so this rejects nothing today; it is here to
# keep a future dump from quietly packing a position whose FEN we cannot represent.
FIFTY_MOVE_PLIES = 100
MAX_RATING_DEVIATION = 80
MIN_POPULARITY = 90
MIN_PLAYS = 500
KEEP_PER_BUCKET = 80                                           # bounded memory while streaming

# --------------------------------------------------------------------------- FEN -> bits

PIECE_CODE = {
    "P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5,
    "p": 6, "n": 7, "b": 8, "r": 9, "q": 10, "k": 11,
}
PIECE_CHAR = "PNBRQKpnbrqk"
CASTLING_BIT = {"K": 1, "Q": 2, "k": 4, "q": 8}
PROMO_CODE = {"": 0, "n": 1, "b": 2, "r": 3, "q": 4}


def parse_fen(fen):
    """Return (board, side_to_move, castling_mask, ep_target) or None.

    board is a list of 64 entries, None or a PIECE_CODE value, square 0 = a1.
    """
    parts = fen.split()
    if len(parts) < 4:
        return None
    placement, stm, castling, ep = parts[0], parts[1], parts[2], parts[3]
    board = [None] * 64
    rows = placement.split("/")
    if len(rows) != 8:
        return None
    for row_index, row in enumerate(rows):
        rank = 7 - row_index
        file = 0
        for ch in row:
            if ch.isdigit():
                file += int(ch)
            else:
                code = PIECE_CODE.get(ch)
                if code is None or file > 7:
                    return None
                board[rank * 8 + file] = code
                file += 1
        if file != 8:
            return None
    if stm not in ("w", "b"):
        return None
    mask = 0
    if castling != "-":
        for ch in castling:
            if ch not in CASTLING_BIT:
                return None
            mask |= CASTLING_BIT[ch]
    ep_target = None
    if ep != "-":
        if len(ep) != 2 or ep[0] < "a" or ep[0] > "h" or ep[1] < "1" or ep[1] > "8":
            return None
        ep_target = (ord(ep[1]) - ord("1")) * 8 + (ord(ep[0]) - ord("a"))
    return board, stm, mask, ep_target


def encode_position(board, stm, castling, ep_target):
    """Pack a position: occupancy bitmap + one nibble per piece + one flags byte."""
    nibbles = []
    occupancy = 0
    ep_pawn = None
    if ep_target is not None:
        rank = ep_target >> 3
        if rank == 5 and board[ep_target - 8] == PIECE_CODE["p"]:
            ep_pawn = ep_target - 8          # black pawn that just double-pushed
        elif rank == 2 and board[ep_target + 8] == PIECE_CODE["P"]:
            ep_pawn = ep_target + 8          # white pawn that just double-pushed
        # anything else: the FEN's en-passant square is bogus, drop it silently
    for square in range(64):
        code = board[square]
        if code is None:
            continue
        occupancy |= 1 << square
        if square == ep_pawn:
            code = 12 if code == PIECE_CODE["P"] else 13
        nibbles.append(code)
    out = bytearray(occupancy.to_bytes(8, "little"))
    for i in range(0, len(nibbles), 2):
        low = nibbles[i]
        high = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        out.append(low | (high << 4))
    out.append((1 if stm == "b" else 0) | (castling << 1))
    return bytes(out), len(nibbles)


def placement_of(board):
    rows = []
    for rank in range(7, -1, -1):
        row = []
        empty = 0
        for file in range(8):
            code = board[rank * 8 + file]
            if code is None:
                empty += 1
                continue
            if empty:
                row.append(str(empty))
                empty = 0
            row.append(PIECE_CHAR[code])
        if empty:
            row.append(str(empty))
        rows.append("".join(row))
    return "/".join(rows)


# Which castling right dies when a piece leaves or lands on one of these squares.
CASTLING_GUARD = {0: 2, 4: 3, 7: 1, 56: 8, 60: 12, 63: 4}


def start_key(board, stm, castling, uci):
    """A key for "the position the solver sees", used only to drop duplicates.

    This is a deliberately cheap move application: it need not be a legal-move
    engine, it only has to give equal keys to equal positions.  The en-passant
    square is left out on purpose - we cannot tell here whether the capture would
    be legal, and merging two positions that differ only by a dead e.p. flag just
    costs us one puzzle out of six million candidates.
    """
    board = list(board)
    src = parse_square(uci[0:2])
    dst = parse_square(uci[2:4])
    if src is None or dst is None:
        return None
    piece = board[src]
    if piece is None:
        return None
    white = piece < 6
    board[src] = None
    if piece in (PIECE_CODE["K"], PIECE_CODE["k"]) and abs((dst & 7) - (src & 7)) == 2:
        rank = src & ~7
        if dst > src:                                   # kingside: h-rook to f
            board[rank + 5], board[rank + 7] = board[rank + 7], None
        else:                                           # queenside: a-rook to d
            board[rank + 3], board[rank] = board[rank], None
    if piece in (PIECE_CODE["P"], PIECE_CODE["p"]) and (src & 7) != (dst & 7) and board[dst] is None:
        board[(src & ~7) | (dst & 7)] = None            # en-passant capture
    promotion = uci[4:5].lower()
    if promotion:
        code = PIECE_CODE[promotion.upper() if white else promotion]
        board[dst] = code
    else:
        board[dst] = piece
    if piece == PIECE_CODE["K"]:
        castling &= ~3
    elif piece == PIECE_CODE["k"]:
        castling &= ~12
    castling &= ~CASTLING_GUARD.get(src, 0)
    castling &= ~CASTLING_GUARD.get(dst, 0)
    return (placement_of(board), "b" if stm == "w" else "w", castling)


def encode_move(uci):
    """A UCI move as a little-endian uint16: from | to<<6 | promo<<12."""
    if len(uci) not in (4, 5):
        return None
    src = parse_square(uci[0:2])
    dst = parse_square(uci[2:4])
    if src is None or dst is None:
        return None
    promo = PROMO_CODE.get(uci[4:5].lower())
    if promo is None:
        return None
    return src | (dst << 6) | (promo << 12)


def parse_square(text):
    if text[0] < "a" or text[0] > "h" or text[1] < "1" or text[1] > "8":
        return None
    return (ord(text[1]) - ord("1")) * 8 + (ord(text[0]) - ord("a"))


# --------------------------------------------------------------------------- streaming pass


def rank_key(popularity, plays, puzzle_id):
    """Higher is better; the id makes ties deterministic."""
    return (popularity, plays, tuple(255 - b for b in puzzle_id.encode("ascii")))


def scan(path, stats):
    """Stream the database, keeping the best KEEP_PER_BUCKET rows per (band, theme)."""
    buckets = [[[] for _ in THEMES] for _ in range(BAND_COUNT)]
    with zstd.ZstdFile(path, "rb") as raw:
        text = io.TextIOWrapper(raw, encoding="utf-8", newline="")
        header = text.readline()
        if not header.startswith("PuzzleId,FEN,Moves,Rating"):
            sys.exit("unexpected CSV header: " + header[:80])
        for line in text:
            stats["rows"] += 1
            fields = line.rstrip("\n").split(",")
            if len(fields) != 11:
                stats["malformed"] += 1
                continue
            (puzzle_id, fen, moves, rating, deviation,
             popularity, plays, themes) = fields[0:8]
            try:
                rating_i = int(rating)
                deviation_i = int(deviation)
                popularity_i = int(popularity)
                plays_i = int(plays)
            except ValueError:
                stats["malformed"] += 1
                continue
            if rating_i < RATING_MIN or rating_i >= RATING_MAX:
                continue
            if deviation_i > MAX_RATING_DEVIATION:
                stats["rejected_deviation"] += 1
                continue
            if popularity_i < MIN_POPULARITY or plays_i < MIN_PLAYS:
                stats["rejected_unpopular"] += 1
                continue
            move_list = moves.split()
            if len(move_list) < 2 or len(move_list) > MAX_PLIES:
                stats["rejected_length"] += 1
                continue
            # Dropping the halfmove clock has to stay harmless: see FIFTY_MOVE_PLIES.
            fen_fields = fen.split()
            if len(fen_fields) < 6:
                stats["malformed"] += 1
                continue
            try:
                halfmove_i = int(fen_fields[4])
            except ValueError:
                stats["malformed"] += 1
                continue
            if halfmove_i + len(move_list) >= FIFTY_MOVE_PLIES:
                stats["rejected_clock"] += 1
                continue
            if len(puzzle_id) != 5:
                stats["rejected_id"] += 1
                continue
            theme_set = themes.split()
            theme_id = None
            for name in theme_set:
                index = THEME_INDEX.get(name)
                if index is not None and (theme_id is None or index < theme_id):
                    theme_id = index
            if theme_id is None:
                stats["rejected_theme"] += 1
                continue
            stats["eligible"] += 1
            band = (rating_i - RATING_MIN) // BAND_WIDTH
            bucket = buckets[band][theme_id]
            entry = (rank_key(popularity_i, plays_i, puzzle_id),
                     puzzle_id, fen, moves, rating_i, theme_id,
                     1 if "mate" in theme_set else 0)
            if len(bucket) < KEEP_PER_BUCKET:
                heapq.heappush(bucket, entry)
            elif entry[0] > bucket[0][0]:
                heapq.heapreplace(bucket, entry)
    return buckets


def select(buckets, stats):
    """Round-robin over the themes of every band until the band quota is full."""
    chosen = []
    seen_positions = set()
    for band in range(BAND_COUNT):
        ordered = [sorted(bucket, key=lambda e: e[0], reverse=True) for bucket in buckets[band]]
        cursors = [0] * len(THEMES)
        taken = 0
        progress = True
        while taken < PER_BAND and progress:
            progress = False
            for theme_id in range(len(THEMES)):
                if taken >= PER_BAND:
                    break
                while cursors[theme_id] < len(ordered[theme_id]):
                    entry = ordered[theme_id][cursors[theme_id]]
                    cursors[theme_id] += 1
                    parsed = parse_fen(entry[2])
                    if parsed is None:
                        stats["rejected_fen"] += 1
                        continue
                    key = start_key(parsed[0], parsed[1], parsed[2], entry[3].split()[0])
                    if key is None:
                        stats["rejected_fen"] += 1
                        continue
                    if key in seen_positions:
                        stats["duplicates"] += 1
                        continue
                    seen_positions.add(key)
                    chosen.append(entry)
                    taken += 1
                    progress = True
                    break
        stats["per_band"].append(taken)
    # Final pack order: by rating, then by id.  Lets the reader binary-search a rating band.
    chosen.sort(key=lambda e: (e[4], e[1]))
    return chosen


# --------------------------------------------------------------------------- output


def build_arrays(chosen, stats):
    index = bytearray()
    blob = bytearray()
    for entry in chosen:
        _, puzzle_id, fen, moves, rating, theme_id, mate = entry
        parsed = parse_fen(fen)
        if parsed is None:
            sys.exit("puzzle %s: cannot parse FEN %r" % (puzzle_id, fen))
        board, stm, castling, ep_target = parsed
        packed, piece_count = encode_position(board, stm, castling, ep_target)
        stats["pieces"] += piece_count
        move_words = []
        for uci in moves.split():
            word = encode_move(uci)
            if word is None:
                sys.exit("puzzle %s: cannot parse move %r" % (puzzle_id, uci))
            move_words.append(word)
        offset = len(blob)
        if offset >= 1 << 24:
            sys.exit("blob larger than the 24-bit offset field")
        blob += packed
        for word in move_words:
            blob += word.to_bytes(2, "little")
        index += puzzle_id.encode("ascii")
        index.append(theme_id | (mate << 6))
        index += rating.to_bytes(2, "little")
        index.append(len(move_words))
        index += offset.to_bytes(3, "little")
        stats["plies"] += len(move_words)
    return bytes(index), bytes(blob)


def hex_array(data, per_line=16):
    lines = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + "".join("0x%02x," % b for b in chunk))
    return "\n".join(lines)


HEADER_TOP = """// SPDX-License-Identifier: GPL-3.0-or-later
// GENERATED FILE - do not edit.  Rebuild with:
//   python3 tools/make_puzzle_pack.py <lichess_db_puzzle.csv.zst>
// Puzzles from the Lichess puzzle database, CC0 1.0 Universal.
"""


def write_data_header(path, count, index_bytes, blob_bytes):
    with open(path, "w", encoding="utf-8") as out:
        out.write(HEADER_TOP)
        out.write("""#pragma once
#include <cstdint>

namespace arrocco::puzzles::data {

// Bumped whenever the byte layout below changes; puzzles.cpp static_asserts on it.
constexpr uint16_t kFormatVersion = %d;

constexpr int kCount = %d;
constexpr int kIndexStride = 12;
constexpr uint32_t kIndexBytes = %d;
constexpr uint32_t kBlobBytes = %d;
constexpr uint32_t kTotalBytes = kIndexBytes + kBlobBytes;

// One 12-byte record per puzzle, sorted by rating then by Lichess id:
//   [0..4]  Lichess puzzle id, 5 ASCII chars, no terminator
//   [5]     theme id in bits 0-5, "ends in mate" in bit 6
//   [6..7]  rating, little-endian uint16
//   [8]     number of stored moves (the blunder plus the solution)
//   [9..11] offset into kBlob, little-endian uint24
extern const uint8_t kIndex[kIndexBytes];

// Variable-length records:
//   [0..7]           occupancy bitmap, little-endian uint64, bit n = square n (0 = a1)
//   [8..]            one nibble per occupied square in ascending square order, low nibble first
//                    0-5 = white P N B R Q K, 6-11 = black P N B R Q K,
//                    12/13 = a white/black pawn that has just double-pushed (gives the e.p. square)
//   next byte        side to move in bit 0 (1 = black), castling rights in bits 1-4 (K Q k q)
//   then 2 bytes/move  little-endian uint16: from | to<<6 | promotion<<12
//                    promotion 0 = none, 1 = N, 2 = B, 3 = R, 4 = Q
extern const uint8_t kBlob[kBlobBytes];

}  // namespace arrocco::puzzles::data
""" % (FORMAT_VERSION, count, index_bytes, blob_bytes))


def write_data_source(path, index, blob):
    with open(path, "w", encoding="utf-8") as out:
        out.write(HEADER_TOP)
        out.write('#include "arrocco/puzzles/puzzle_data.h"\n\n')
        out.write("namespace arrocco::puzzles::data {\n\n")
        out.write("const uint8_t kIndex[kIndexBytes] = {\n")
        out.write(hex_array(index))
        out.write("\n};\n\n")
        out.write("const uint8_t kBlob[kBlobBytes] = {\n")
        out.write(hex_array(blob))
        out.write("\n};\n\n")
        out.write("}  // namespace arrocco::puzzles::data\n")


def write_themes_header(path):
    with open(path, "w", encoding="utf-8") as out:
        out.write(HEADER_TOP)
        out.write("""#pragma once
#include <cstdint>

namespace arrocco::puzzles {

// The one label a puzzle carries.  Generated from the theme table in
// tools/make_puzzle_pack.py; the numeric values are what puzzle_data.cpp stores.
enum class ThemeId : uint8_t {
""")
        for _, enumerator, _ in THEMES:
            out.write("  %s,\n" % enumerator)
        out.write("  Count\n};\n\n")
        out.write("// English label, or \"\" for a value outside the enum.  Never null.\n")
        out.write("const char* themeName(ThemeId theme);\n\n")
        out.write("// The Lichess theme name the label came from (for looking a puzzle up).\n")
        out.write("const char* themeKey(ThemeId theme);\n\n")
        out.write("}  // namespace arrocco::puzzles\n")


def write_themes_source(path):
    with open(path, "w", encoding="utf-8") as out:
        out.write(HEADER_TOP)
        out.write('#include "arrocco/puzzles/theme_data.h"\n\n')
        out.write("namespace arrocco::puzzles {\nnamespace {\n\n")
        out.write("const char* const kNames[] = {\n")
        for _, _, label in THEMES:
            out.write('    "%s",\n' % label)
        out.write("};\n\n")
        out.write("const char* const kKeys[] = {\n")
        for key, _, _ in THEMES:
            out.write('    "%s",\n' % key)
        out.write("};\n\n")
        out.write("}  // namespace\n\n")
        out.write("""const char* themeName(ThemeId theme) {
  const unsigned i = static_cast<unsigned>(theme);
  return i < static_cast<unsigned>(ThemeId::Count) ? kNames[i] : "";
}

const char* themeKey(ThemeId theme) {
  const unsigned i = static_cast<unsigned>(theme);
  return i < static_cast<unsigned>(ThemeId::Count) ? kKeys[i] : "";
}

}  // namespace arrocco::puzzles
""")


def write_reference(path, chosen):
    with open(path, "w", encoding="utf-8") as out:
        out.write(HEADER_TOP)
        out.write("""// The raw Lichess rows behind the pack, so the native test can check the
// encoder end to end instead of only round-tripping itself.  Test data only:
// this header is never compiled into the firmware.
#pragma once
#include <cstdint>

namespace arrocco::puzzles::test_reference {

struct Row {
  const char* id;
  const char* fen;     // the Lichess FEN: the position BEFORE the blunder
  const char* moves;   // space-separated UCI, the blunder first
  uint16_t rating;
  uint8_t theme;
  uint8_t mate;
};

constexpr int kRowCount = %d;
inline const Row kRows[kRowCount] = {
""" % len(chosen))
        for entry in chosen:
            _, puzzle_id, fen, moves, rating, theme_id, mate = entry
            out.write('    {"%s", "%s", "%s", %d, %d, %d},\n'
                      % (puzzle_id, fen, moves, rating, theme_id, mate))
        out.write("};\n\n}  // namespace arrocco::puzzles::test_reference\n")


# --------------------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", help="path to lichess_db_puzzle.csv.zst")
    parser.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        help="repository root (default: the parent of tools/)")
    args = parser.parse_args()

    stats = {"rows": 0, "malformed": 0, "eligible": 0, "duplicates": 0,
             "rejected_deviation": 0, "rejected_unpopular": 0, "rejected_length": 0,
             "rejected_clock": 0,
             "rejected_theme": 0, "rejected_id": 0, "rejected_fen": 0, "pieces": 0, "plies": 0,
             "per_band": []}

    buckets = scan(args.database, stats)
    chosen = select(buckets, stats)
    index, blob = build_arrays(chosen, stats)

    puzzles_dir = os.path.join(args.repo, "lib", "arrocco", "src", "arrocco", "puzzles")
    test_dir = os.path.join(args.repo, "test", "puzzles")
    os.makedirs(puzzles_dir, exist_ok=True)
    os.makedirs(test_dir, exist_ok=True)

    write_data_header(os.path.join(puzzles_dir, "puzzle_data.h"), len(chosen), len(index), len(blob))
    write_data_source(os.path.join(puzzles_dir, "puzzle_data.cpp"), index, blob)
    write_themes_header(os.path.join(puzzles_dir, "theme_data.h"))
    write_themes_source(os.path.join(puzzles_dir, "theme_data.cpp"))
    write_reference(os.path.join(test_dir, "puzzle_reference.h"), chosen)

    total = len(index) + len(blob)
    print("rows read          %d" % stats["rows"])
    print("eligible           %d" % stats["eligible"])
    print("  dropped: deviation %d  unpopular %d  length %d  clock %d  theme %d  id %d  malformed %d"
          % (stats["rejected_deviation"], stats["rejected_unpopular"], stats["rejected_length"],
             stats["rejected_clock"], stats["rejected_theme"], stats["rejected_id"],
             stats["malformed"]))
    print("selected           %d  (duplicates skipped %d)" % (len(chosen), stats["duplicates"]))
    for band, taken in enumerate(stats["per_band"]):
        print("  band %4d-%4d   %d" % (RATING_MIN + band * BAND_WIDTH,
                                       RATING_MIN + (band + 1) * BAND_WIDTH - 1, taken))
    counts = {}
    for entry in chosen:
        counts[entry[5]] = counts.get(entry[5], 0) + 1
    for theme_id, (key, _, _) in enumerate(THEMES):
        print("  theme %-18s %d" % (key, counts.get(theme_id, 0)))
    print("index              %d bytes" % len(index))
    print("blob               %d bytes" % len(blob))
    print("total              %d bytes  (%.1f bytes/puzzle)" % (total, total / max(1, len(chosen))))
    print("average pieces     %.1f   average plies %.2f"
          % (stats["pieces"] / max(1, len(chosen)), stats["plies"] / max(1, len(chosen))))
    if total > 300 * 1024:
        sys.exit("pack is over the 300 KB budget")


if __name__ == "__main__":
    main()
