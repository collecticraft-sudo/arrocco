#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - the offline puzzles (python3 stdlib only).

Runs sim/build/arrocco-sim --virtual-time with --state DIR, a temporary directory that
plays the board's flash, and goes through the puzzle mode the way a person does:

  - the menu's "Puzzles" is live; the first visit asks for a starting level, and nothing
    is written before one is picked;
  - a puzzle appears as Lichess published it, then the opponent's blunder lands with the
    move marks, a moment later (or at once, with a tap);
  - one puzzle SOLVED by tapping its solution, read from the pack's reference rows
    (test/puzzles/puzzle_reference.h) at the index the board wrote to its flash; the
    replies come by themselves; the rating goes up;
  - one puzzle FAILED with a wrong move found the way a person finds one (tap a piece,
    look at the dots): the board stays, an X marks the square, the rating goes down once;
  - Hint selects the right piece, Solution plays the line to its end move by move, Retry
    puts the start position back, Skip and Next draw new puzzles, Level starts again;
  - the progress (rating, counts, the puzzle on the board and how far it got) survives a
    power cut (SIGKILL), and a wake from sleep on the puzzle board comes back to it;
  - one frame per tap, nothing past the side column's edge, and the flash written only
    after a refresh and never for a selection.

    usage: test/ui/puzzle_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion.
"""
import os
import re
import shutil
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_session import (REPO, Sim, check, center, is_board_frame, box_present, MENU_BTN,  # noqa: E402
                         SIDE_BTN, PROMO_BTN, PROMO_BOX, HEADLINE, SUBLINE)

MENU_SLOT_PUZZLES = 3
PICK_BACK = 5
SLOT_HINT, SLOT_SOLUTION, SLOT_NEXT, SLOT_LEVEL, SLOT_MENU = 0, 1, 2, 3, 4
SLOT_RETRY = SLOT_HINT
LEVEL_RATINGS = (800, 1100, 1400, 1700)
PROMO_CHOICE = {"q": 0, "r": 1, "b": 2, "n": 3}
SQ, BX, BY = 56, 16, 16
RIGHT_MARGIN = (785, 0, 15, 480)          # past the side column and the buttons: always white
RATING_BLOCK = (496, 196, 288, 36)        # the solver's rating and its last change
OPPONENT_DELAY_MS = 700                   # layout.h kPuzzleOpponentDelayMs

# Flags of the progress blob (ui/puzzle_progress.h).
HINTED, MISSED, HELPED, SCORED, RETRIED, ON_SCREEN = 1, 2, 4, 8, 16, 32


# ---- the pack's reference rows ---------------------------------------------------------------
def load_rows():
    path = os.path.join(REPO, "test", "puzzles", "puzzle_reference.h")
    pattern = re.compile(r'\{"([^"]+)", "([^"]+)", "([^"]+)", (\d+), (\d+), (\d+)\},')
    rows = []
    with open(path) as f:
        for line in f:
            m = pattern.search(line)
            if m:
                rows.append({"id": m.group(1), "fen": m.group(2), "moves": m.group(3).split(),
                             "rating": int(m.group(4)), "mate": m.group(6) == "1"})
    return rows


class Pos:
    """Just enough chess to know where the pieces stand after a few UCI moves."""

    def __init__(self, fen):
        placement, turn = fen.split()[:2]
        self.board = {}
        for r, row in enumerate(placement.split("/")):
            f = 0
            for ch in row:
                if ch.isdigit():
                    f += int(ch)
                else:
                    self.board["abcdefgh"[f] + str(8 - r)] = ch
                    f += 1
        self.turn = turn

    def play(self, uci):
        frm, to, promo = uci[:2], uci[2:4], uci[4:]
        piece = self.board.pop(frm)
        if piece in "Kk" and abs(ord(frm[0]) - ord(to[0])) == 2:          # castling: the rook too
            rank = frm[1]
            rook_from, rook_to = ("h", "f") if to[0] == "g" else ("a", "d")
            self.board[rook_to + rank] = self.board.pop(rook_from + rank)
        if piece in "Pp" and frm[0] != to[0] and to not in self.board:     # en passant
            self.board.pop(to[0] + frm[1], None)
        if promo:
            piece = promo.upper() if piece.isupper() else promo.lower()
        self.board[to] = piece
        self.turn = "b" if self.turn == "w" else "w"

    def own(self):
        white = self.turn == "w"
        return [sq for sq, p in self.board.items() if p.isupper() == white]


SQUARES = [f + r for r in "12345678" for f in "abcdefgh"]


def square_rect(name, flipped):
    f, r = ord(name[0]) - ord("a"), int(name[1]) - 1
    col, row = (7 - f, r) if flipped else (f, 7 - r)
    return (BX + col * SQ, BY + row * SQ, SQ, SQ)


def centre_box(name, flipped, size=14):
    x, y, w, h = square_rect(name, flipped)
    return (x + (w - size) // 2, y + (h - size) // 2, size, size)


def changed_squares(a, b, flipped):
    return sorted(sq for sq in SQUARES if a.region(square_rect(sq, flipped)) != b.region(square_rect(sq, flipped)))


def move_squares(uci):
    """The squares a move changes on the board (castling and en passant: more)."""
    frm, to = uci[:2], uci[2:4]
    return {frm, to}


# ---- the progress blob ------------------------------------------------------------------------------
class Progress:
    def __init__(self, data):
        check(len(data) == 98 and data[:5] == b"ARPZ\x01", "the progress blob: magic, version 1, 98 bytes (%d)" % len(data))
        check(struct.unpack("<I", data[-4:])[0] == zlib.crc32(data[:-4]) & 0xFFFFFFFF, "the progress blob: CRC-32")
        (self.flags, self.rating, self.deviation, self.played, self.solved, self.streak, self.last_change,
         self.seed, self.pack, self.current, self.plies, self.buckets) = struct.unpack("<BHHHHHhIIHBB", data[5:30])
        self.drawn = struct.unpack("<32H", data[30:94])


class Board:
    """One simulator process on the state directory: the board between two power cuts."""

    def __init__(self, state_dir, png_dir, name, wake=False):
        self.state_dir = state_dir
        args = ["--state", state_dir] + (["--wake"] if wake else [])
        self.sim = Sim(png_dir, args)
        self.sim.send("set scale 0")
        self.name = name
        self.boot = self.sim.frames_at_boot
        check(len(self.boot) == 1, "%s: one frame at boot" % name)
        self.flipped = False
        self.taps = 0
        self.tap_frames = 0

    # -- the flash
    def stores(self, key="puzzles"):
        return [e for e in self.sim.events if e["ev"] == "store" and e["key"] == key]

    def progress(self):
        with open(os.path.join(self.state_dir, "puzzles.bin"), "rb") as f:
            return Progress(f.read())

    # -- taps and ticks
    def tap(self, rect_or_xy, desc, frames=1, kind=None):
        x, y = center(rect_or_xy) if len(rect_or_xy) == 4 else rect_or_xy
        before = len(self.stores())
        got = self.sim.tap(x, y)
        self.taps += 1
        self.tap_frames += len(got)
        check(len(got) == frames, "%s: %s gives %d frame(s) (%d)" % (self.name, desc, frames, len(got)))
        if kind is not None and got:
            check(got[-1].kind == kind, "%s: %s is a %s refresh (%s)" % (self.name, desc, kind, got[-1].kind))
        for fr in got:
            check(fr.ink(RIGHT_MARGIN) == 0, "%s: %s: nothing drawn past the side column" % (self.name, desc))
        for e in self.stores()[before:]:
            check(got and e["ok"] and e["t"] >= got[-1].t + got[-1].ev["nominal_ms"],
                  "%s: %s: the flash is written after the refresh" % (self.name, desc))
        return got[-1] if got else None

    def square(self, name, desc, frames=1, kind=None):
        return self.tap(square_rect(name, self.flipped), desc, frames, kind)

    def wait_opponent(self, desc):
        """The opponent's move: nothing on the first tick after the refresh, one frame once due."""
        first = self.sim.tick(0)
        check(len(first) == 0, "%s: %s: not on the first tick" % (self.name, desc))
        early = self.sim.tick(OPPONENT_DELAY_MS - 100)
        check(len(early) == 0, "%s: %s: not before %d ms" % (self.name, desc, OPPONENT_DELAY_MS))
        got = self.sim.tick(200)
        check(len(got) == 1 and got[0].kind in ("partial", "full"),
              "%s: %s: one frame a moment later (%d)" % (self.name, desc, len(got)))
        return got[0] if got else None

    def raw_tap(self, rect):
        """A tap whose frame count the caller judges (looking for a move)."""
        got = self.sim.tap(*center(rect))
        self.taps += 1
        self.tap_frames += len(got)
        return got

    def cut(self):
        self.sim.kill()


# ---- one puzzle, as the screen and the flash say ---------------------------------------------------------
class Puzzle:
    def __init__(self, board, rows):
        p = board.progress()
        self.index = p.current
        check(0 <= self.index < len(rows), "%s: the flash names puzzle %d" % (board.name, self.index))
        self.row = rows[self.index]
        self.moves = self.row["moves"]             # the blunder, then the solution
        self.pos = Pos(self.row["fen"])
        self.solver = "b" if self.pos.turn == "w" else "w"   # the blunder is the other side's
        board.flipped = self.solver == "b"
        self.ply = 0                               # solution plies played


def appear(b, rows, frame, desc):
    """A new puzzle just appeared (`frame`): the published position, then the blunder."""
    pz = Puzzle(b, rows)
    check(is_board_frame(frame), "%s: %s: the board" % (b.name, desc))
    setup = frame
    landed = b.wait_opponent(desc + ": the blunder")
    moved = changed_squares(setup, landed, b.flipped)
    want = move_squares(pz.moves[0])
    check(want.issubset(moved) and len(moved) <= 5,
          "%s: %s: the blunder %s changes its squares and no others (%s)" % (b.name, desc, pz.moves[0], moved))
    check(landed.region(HEADLINE) != setup.region(HEADLINE), "%s: %s: the headline turns to the solver" % (b.name, desc))
    pz.pos.play(pz.moves[0])
    return pz, landed


def play_move(b, pz, uci, desc):
    """Taps a move of the solver's: piece, then target (and the promotion piece)."""
    frm, to, promo = uci[:2], uci[2:4], uci[4:]
    stores = len(b.stores())
    b.square(frm, "%s: select %s" % (desc, frm), kind="partial")
    check(len(b.stores()) == stores, "%s: %s: selecting writes nothing" % (b.name, desc))
    if promo:
        popup = b.square(to, "%s: %s opens the promotion popup" % (desc, to))
        check(box_present(popup, PROMO_BOX), "%s: %s: the promotion popup" % (b.name, desc))
        return b.tap(PROMO_BTN(PROMO_CHOICE[promo]), "%s: promote to %s" % (desc, promo))
    return b.square(to, "%s: play %s" % (desc, to))


def solve(b, pz, desc):
    """Plays the rest of the solution; the replies come by themselves. Returns the last frame."""
    last = None
    while pz.ply + 1 < len(pz.moves):
        uci = pz.moves[1 + pz.ply]
        before = b.sim.last()
        last = play_move(b, pz, uci, "%s: move %d %s" % (desc, pz.ply // 2 + 1, uci))
        moved = changed_squares(before, last, b.flipped)
        check(move_squares(uci).issubset(moved), "%s: %s: %s is on the board" % (b.name, desc, uci))
        pz.pos.play(uci)
        pz.ply += 1
        if pz.ply + 1 < len(pz.moves):
            reply = pz.moves[1 + pz.ply]
            got = b.wait_opponent("%s: the reply %s" % (desc, reply))
            check(move_squares(reply).issubset(changed_squares(last, got, b.flipped)),
                  "%s: %s: the reply %s is on the board" % (b.name, desc, reply))
            pz.pos.play(reply)
            pz.ply += 1
            last = got
    return last


def find_wrong_move(b, pz, desc):
    """A legal move the puzzle refuses, found the way a person finds one: tap a piece and look
    at the dots. A quiet target that is not the solution's (and not on a capture ring)."""
    want = pz.moves[1 + pz.ply]
    for sq in sorted(pz.pos.own()):
        before = b.sim.last()
        got = b.raw_tap(square_rect(sq, b.flipped))
        check(len(got) <= 1, "%s: %s: at most one frame for a tap on %s" % (b.name, desc, sq))
        if not got:
            continue
        sel = got[-1]
        # A dot: ink appears in the middle of an empty square (on a hatched one, a lot more ink).
        targets = [t for t in SQUARES if t not in pz.pos.board and
                   sel.ink(centre_box(t, b.flipped)) - before.ink(centre_box(t, b.flipped)) > 60]
        wrong = [t for t in targets if sq + t != want[:4]]
        if wrong:
            return sq, wrong[0], before
        put_down = b.raw_tap(square_rect(sq, b.flipped))
        check(len(put_down) == 1 and put_down[0].data == before.data,
              "%s: %s: %s put down again, the board as before" % (b.name, desc, sq))
    check(False, "%s: %s: no wrong move found" % (b.name, desc))
    return None, None, None


# ---- the scenario ------------------------------------------------------------------------------------------
def main():
    png_dir = None
    if len(sys.argv) == 3 and sys.argv[1] == "--png":
        png_dir = sys.argv[2]
        os.makedirs(png_dir, exist_ok=True)
    state_dir = tempfile.mkdtemp(prefix="arrocco-puzzles-")
    try:
        run(state_dir, png_dir, load_rows())
    finally:
        shutil.rmtree(state_dir, ignore_errors=True)
    print("ALL %d CHECKS PASSED" % check.count)


def run(state_dir, png_dir, rows):
    check(len(rows) == 3500, "the pack's reference rows: %d" % len(rows))

    # ---- 1. the menu: Puzzles is live; the first visit asks for a level ----------------------
    b = Board(state_dir, png_dir, "first start")
    menu = b.boot[0]
    x, y, w, h = MENU_BTN(MENU_SLOT_PUZZLES)
    check(menu.ink((x + 10, y, w - 20, 1)) == w - 20, "menu: 'Puzzles' has a solid frame, it is live")
    picker = b.tap(MENU_BTN(MENU_SLOT_PUZZLES), "menu 'Puzzles'", kind="full")
    b.sim.save(picker, "z01_level_picker")
    check(not is_board_frame(picker), "the first visit: the level picker, not a board")
    for level in range(4):
        check(picker.ink(MENU_BTN(level)) > 300, "picker: level %d drawn" % level)
    check(picker.ink((150, 440, 500, 40)) > 100, "picker: the Lichess credit in the footer")
    b.tap(MENU_BTN(PICK_BACK), "picker 'Back'", kind="full")
    check(not b.stores() and not os.path.exists(os.path.join(state_dir, "puzzles.bin")),
          "nothing written before a level is picked")
    again = b.tap(MENU_BTN(MENU_SLOT_PUZZLES), "menu 'Puzzles' again", kind="full")
    check(again.data == picker.data, "still the picker")

    # ---- 2. Casual player: a puzzle appears, then the blunder ---------------------------------
    first = b.tap(MENU_BTN(1), "picker 'Casual player'", kind="full")
    b.sim.save(first, "z02_new_puzzle")
    check(len(b.stores()) == 1, "picking a level writes the progress once")
    p = b.progress()
    check(p.rating == 1100 and p.deviation == 250 and p.played == 0 and p.plies == 0,
          "the progress: 1100, deviation 250, nothing played (%d/%d/%d)" % (p.rating, p.deviation, p.played))
    check(p.flags == ON_SCREEN and p.seed != 0 and p.buckets == 32, "the progress: on screen, seeded")
    check(1100 <= rows[p.current]["rating"] < 1200, "the first puzzle is in the 1100s (%d)" % rows[p.current]["rating"])
    stores = len(b.stores())
    pz, landed = appear(b, rows, first, "first puzzle")
    b.sim.save(landed, "z03_blunder_played")
    check(len(b.stores()) == stores, "the blunder changes nothing to keep: no write")
    check(b.flipped == (pz.solver == "b"), "the board is seen from the solver's side")

    # ---- 3. solve it --------------------------------------------------------------------------
    solved = solve(b, pz, "first puzzle")
    b.sim.save(solved, "z04_solved")
    p = b.progress()
    check(p.played == 1 and p.solved == 1 and p.streak == 1 and p.flags & SCORED and not p.flags & MISSED,
          "solved: counted once, as solved")
    check(p.rating > 1100 and p.last_change == p.rating - 1100, "solved: the rating went up (%d)" % p.rating)
    check(p.plies == len(pz.moves) - 1, "solved: the whole line played (%d plies)" % p.plies)
    check(solved.region(RATING_BLOCK) != landed.region(RATING_BLOCK), "the side column shows the new rating")
    check(len(b.sim.beeps) >= 2 and b.sim.beeps[-1]["hz"] == b.sim.beeps[-2]["hz"] == 660, "solved: the double beep")
    b.square(pz.moves[-1][2:4], "a tap on the board of a solved puzzle", frames=0)

    # ---- 4. Retry: back at the start, no rating change ------------------------------------------------
    retried = b.tap(SIDE_BTN(SLOT_RETRY), "'Retry'", kind=None)
    check(retried.region((16, 16, 448, 448)) == landed.region((16, 16, 448, 448)),
          "Retry: the start position, the blunder marked, as it first stood")
    p = b.progress()
    check(p.plies == 0 and p.flags & RETRIED and p.flags & SCORED and p.played == 1, "Retry: practice, the score kept")
    pz.pos = Pos(pz.row["fen"])
    pz.pos.play(pz.moves[0])
    pz.ply = 0
    rating = p.rating
    solve(b, pz, "first puzzle again")
    p = b.progress()
    check(p.rating == rating and p.played == 1, "practice: the rating did not move")

    # ---- 5. Next, and a wrong move ------------------------------------------------------------
    fr = b.tap(SIDE_BTN(SLOT_NEXT), "'Next'")
    pz, landed = appear(b, rows, fr, "second puzzle")
    p_before = b.progress()
    check(p_before.played == 1 and p_before.plies == 0 and p_before.flags == ON_SCREEN, "Next: a new puzzle, clean")
    sq, target, before_select = find_wrong_move(b, pz, "second puzzle")
    stores = len(b.stores())
    beeps = len(b.sim.beeps)
    wrong = b.square(target, "the wrong move %s%s" % (sq, target), kind="partial")
    b.sim.save(wrong, "z05_wrong_move")
    check(changed_squares(before_select, wrong, b.flipped) == [target],
          "a wrong move is not played: only its square changed (the X)")
    check(wrong.ink(centre_box(target, b.flipped, 20)) > 40, "the X on %s" % target)
    check(wrong.region(HEADLINE) != landed.region(HEADLINE), "the headline says it was not the move")
    check(len(b.stores()) == stores + 1, "the miss is written")
    p = b.progress()
    check(p.played == 2 and p.solved == 1 and p.streak == 0 and p.flags & MISSED and p.flags & SCORED,
          "the miss is counted at once")
    check(p.rating < p_before.rating and p.last_change < 0, "the rating went down (%d)" % p.last_change)
    check(any(e["hz"] == 330 for e in b.sim.beeps[beeps:]), "the wrong move's low beep")

    # ---- 6. Hint, then Solution to the end ----------------------------------------------------
    hint = b.tap(SIDE_BTN(SLOT_HINT), "'Hint'", kind="partial")
    b.sim.save(hint, "z06_hint")
    want = pz.moves[1 + pz.ply]
    hx, hy, _, _ = square_rect(want[:2], b.flipped)
    check(hint.ink((hx + 1, hy + 1, 54, 3)) >= 150, "Hint: the frame on %s, the piece to move" % want[:2])
    check(hint.region(square_rect(target, b.flipped)) != wrong.region(square_rect(target, b.flipped)),
          "Hint: the X is gone")
    check(b.progress().flags & HINTED, "the hint is written")
    b.tap(SIDE_BTN(SLOT_HINT), "'Hint' again", frames=0)
    steps = 0
    while True:
        before = b.sim.last()
        fr = b.tap(SIDE_BTN(SLOT_SOLUTION), "'Solution' step %d" % (steps + 1), kind=None)
        uci = pz.moves[1 + pz.ply]
        check(move_squares(uci).issubset(changed_squares(before, fr, b.flipped)), "Solution: %s played" % uci)
        pz.pos.play(uci)
        pz.ply += 1
        steps += 1
        if pz.ply + 1 >= len(pz.moves):
            break
        b.wait_opponent("solution: the reply")
        pz.pos.play(pz.moves[1 + pz.ply])
        pz.ply += 1
    b.sim.save(b.sim.last(), "z07_solution_shown")
    p = b.progress()
    check(p.plies == len(pz.moves) - 1 and p.flags & HELPED and p.played == 2, "the solution to the end, scored once")
    b.tap(SIDE_BTN(SLOT_SOLUTION), "'Solution' once the line is over (no button)", frames=0)

    # ---- 7. Skip: an untouched puzzle counts as a miss --------------------------------------------
    fr = b.tap(SIDE_BTN(SLOT_NEXT), "'Next' after the solution")
    check(b.progress().played == 2, "Next after the solution: nothing more to count")
    # A tap while the blunder is still to come makes it come now.
    pz = Puzzle(b, rows)
    hurried = b.square("d4", "a tap before the blunder", kind=None)
    check(move_squares(pz.moves[0]).issubset(changed_squares(fr, hurried, b.flipped)), "the blunder came at once")
    check(len(b.sim.tick(2000)) == 0, "and not a second time")
    rating = b.progress().rating
    fr = b.tap(SIDE_BTN(SLOT_NEXT), "'Skip'")
    p = b.progress()
    check(p.played == 3 and p.solved == 1 and p.rating < rating, "Skip: counted as a miss (%d -> %d)" % (rating, p.rating))
    pz, landed = appear(b, rows, fr, "fourth puzzle")

    # ---- 8. Level: the picker from the board, Back keeps everything --------------------------------
    picker2 = b.tap(SIDE_BTN(SLOT_LEVEL), "'Level'", kind="full")
    b.sim.save(picker2, "z08_level_again")
    check(picker2.region((0, 60, 800, 30)) != picker.region((0, 60, 800, 30)), "the picker now says the rating")
    stores = len(b.stores())
    back = b.tap(MENU_BTN(PICK_BACK), "picker 'Back' to the board", kind="full")
    check(back.data == landed.data, "Back: the board exactly as it was")
    check(len(b.stores()) == stores, "nothing written by looking at the picker")

    # ---- 9. a move half-way, then the power goes -----------------------------------------------
    # Find a puzzle with a reply in it: Skip until one comes (each skip is a miss: fine).
    tries = 0
    while len(pz.moves) < 4 and tries < 30:
        fr = b.tap(SIDE_BTN(SLOT_NEXT), "'Skip' to a longer puzzle")
        pz, landed = appear(b, rows, fr, "a puzzle")
        tries += 1
    check(len(pz.moves) >= 4, "a puzzle with a reply in it")
    uci = pz.moves[1]
    play_move(b, pz, uci, "half-way")
    pz.pos.play(uci)
    reply_frame = b.wait_opponent("half-way: the reply")
    pz.pos.play(pz.moves[2])
    pz.ply = 2
    before_cut = reply_frame
    b.sim.save(before_cut, "z09_before_the_cut")
    p = b.progress()
    check(p.plies == 2 and p.flags & ON_SCREEN, "before the cut: two plies on the board, the board on the glass")
    played_before, rating_before = p.played, p.rating
    b.cut()

    # ---- 10. power back: the menu; Puzzles comes back to the same ply --------------------------------
    b = Board(state_dir, png_dir, "after the cut")
    check(b.boot[0].kind == "deep" and not is_board_frame(b.boot[0]), "after the cut: the menu, Deep")
    check(len(b.stores()) == 1 and not b.progress().flags & ON_SCREEN,
          "the boot clears 'on the glass' (one write: the menu is what the glass shows now)")
    back = b.tap(MENU_BTN(MENU_SLOT_PUZZLES), "menu 'Puzzles' after the cut", kind="full")
    b.sim.save(back, "z10_after_the_cut")
    check(back.data == before_cut.data, "the very frame of before the cut: puzzle, ply, marks, rating")
    p = b.progress()
    check(p.played == played_before and p.rating == rating_before and p.plies == 2, "the progress as it was")
    b.flipped = pz.solver == "b"
    solve(b, pz, "after the cut")
    p = b.progress()
    check(p.played == played_before + 1 and p.solved >= 2, "solved after the cut: counted once")

    # ---- 11. asleep on a puzzle: the wake comes straight back ----------------------------------------
    fr = b.tap(SIDE_BTN(SLOT_NEXT), "'Next' before the sleep")
    pz, landed = appear(b, rows, fr, "the puzzle before the sleep")
    b.cut()
    b = Board(state_dir, png_dir, "woken on a puzzle", wake=True)
    woke = b.boot[0]
    b.sim.save(woke, "z11_woken")
    check(woke.kind == "full" and woke.data == landed.data, "the wake: the same puzzle, one Full refresh, no menu")
    check(not b.stores(), "the wake writes nothing: the puzzle is still on the glass")
    b.tap(SIDE_BTN(SLOT_MENU), "'Menu'", kind="full")
    check(not b.progress().flags & ON_SCREEN, "the menu clears 'on the glass'")
    b.cut()
    b = Board(state_dir, png_dir, "woken after the menu", wake=True)
    check(b.boot[0].kind == "deep" and not is_board_frame(b.boot[0]),
          "a wake after leaving the puzzles: the menu, as after any sleep off the board")

    # ---- 12. a new start from Level -------------------------------------------------------------
    b.tap(MENU_BTN(MENU_SLOT_PUZZLES), "menu 'Puzzles'", kind="full")
    b.tap(SIDE_BTN(SLOT_LEVEL), "'Level'", kind="full")
    fr = b.tap(MENU_BTN(0), "picker 'Beginner'", kind="full")
    p = b.progress()
    check(p.rating == 800 and p.played == 0 and p.solved == 0 and p.plies == 0, "Beginner: 800, the counts start again")
    check(800 <= rows[p.current]["rating"] < 900, "a puzzle in the 800s (%d)" % rows[p.current]["rating"])
    check(sum(p.drawn) >= 6, "the walks through the pack go on (%d drawn)" % sum(p.drawn))
    appear(b, rows, fr, "the beginner's puzzle")
    check(b.tap_frames <= b.taps, "never more than one frame per tap (%d for %d)" % (b.tap_frames, b.taps))
    b.sim.quit()


if __name__ == "__main__":
    main()
