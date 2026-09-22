#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - scripted session against the simulator binary (python3 stdlib only).

Drives sim/build/arrocco-sim --virtual-time through its line protocol with
'set scale 0' and checks, on the emitted 1-bit frames, that the real application
behaves as docs/decisioni.md says: menu, two players, selection dots, a full
miniature ending in checkmate, review, undo, the promotion popup, the game clock
cadence, one frame per tap, and the refresh policy.

    usage: test/ui/sim_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion. --png DIR writes the key frames as
PNG files so a reviewer can look at them.
"""
import base64
import json
import os
import struct
import subprocess
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIM = os.path.join(REPO, "sim", "build", "arrocco-sim")
W, H, ROW_BYTES = 800, 480, 100

# ---- layout mirrored from lib/arrocco/src/arrocco/ui/layout.h -------------------------
SQ, BX, BY = 56, 16, 16
BOARD = (16, 16, 448, 448)
SIDE_X = 496
MENU_BTN = lambda slot: (200, 96 + slot * 56, 400, 48)
SIDE_BTN = lambda slot: (496 if slot % 2 == 0 else 644, 304 + (slot // 2) * 56, 140, 48)
CONFIRM_BOX = (64, 120, 352, 232)
CONFIRM_BTN = lambda i: (80, 120 + 56 + i * 56, 320, 48)   # resign / draw / cancel
PROMO_BTN = lambda i: (36 + i * 104, 200, 96, 96)
PROMO_BOX = (24, 152, 432, 160)
GAMEOVER_BOX = (40, 136, 400, 208)
GAMEOVER_BTN = lambda i: (46 + i * 132, 136 + 208 - 48 - 24, 124, 48)
HEADLINE = (496, 22, 288, 28)      # "White to move" / "Black to move"
SUBLINE = (496, 58, 288, 24)       # "1. e4"
CLOCK_ROW = (496, 122, 288, 32)    # "05:00"
MOVELIST_ROW1 = (496, 174, 140, 18)
MARK_WHITE = (548, 96, 16, 16)     # running-side triangle after the "White" label
MARK_BLACK = (690, 96, 16, 16)
MENU_SLOT_TWO_PLAYERS, MENU_SLOT_SETTINGS = 1, 5
CLOCK_SLOT_OFF, CLOCK_SLOT_5 = 0, 1
BTN_NEW, BTN_UNDO, BTN_FLIP, BTN_RESIGN, BTN_MENU = 0, 1, 2, 3, 4
REV_PREV, REV_NEXT = 0, 1
PARTIALS_BEFORE_FULL = 16


def center(rect):
    x, y, w, h = rect
    return x + w // 2, y + h // 2


def square_rect(name, flipped=False):
    f, r = ord(name[0]) - ord("a"), int(name[1]) - 1
    col, row = (7 - f, r) if flipped else (f, 7 - r)
    return (BX + col * SQ, BY + row * SQ, SQ, SQ)


def square_center_box(name, size=16):
    x, y, w, h = square_rect(name)
    return (x + (w - size) // 2, y + (h - size) // 2, size, size)


# ---- frame decoding --------------------------------------------------------------------
class Frame:
    def __init__(self, ev):
        self.ev = ev
        self.kind = ev["kind"]
        self.t = ev["t"]
        self.seq = ev["seq"]
        self.data = base64.b64decode(ev["data"])
        assert len(self.data) == ROW_BYTES * H, len(self.data)
        self.rows = [int.from_bytes(self.data[y * ROW_BYTES:(y + 1) * ROW_BYTES], "big") for y in range(H)]

    def ink(self, rect):
        """Black pixels inside rect (bit 1 = white)."""
        x, y, w, h = rect
        mask = (1 << w) - 1
        shift = W - (x + w)
        white = sum(bin((row >> shift) & mask).count("1") for row in self.rows[y:y + h])
        return w * h - white

    def region(self, rect):
        x, y, w, h = rect
        mask = (1 << w) - 1
        shift = W - (x + w)
        return tuple((row >> shift) & mask for row in self.rows[y:y + h])

    def png(self, path):
        raw = b"".join(b"\x00" + self.data[y * ROW_BYTES:(y + 1) * ROW_BYTES] for y in range(H))

        def chunk(tag, body):
            c = struct.pack(">I", len(body)) + tag + body
            return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 1, 0, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
            f.write(chunk(b"IEND", b""))


# ---- the simulator driver ----------------------------------------------------------------
class Sim:
    def __init__(self, png_dir=None):
        self.p = subprocess.Popen([SIM, "--virtual-time"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self.png_dir = png_dir
        self.events = []          # every event, in order
        self.frames = []          # every new (non-resend) frame
        self.beeps = []
        self.panel = []
        self.hello = self._read_until("hello")
        self.frames_at_boot = self.sync()

    def _read_until(self, ev_name, resend=None):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("simulator closed its stdout")
            ev = json.loads(line)
            name = ev["ev"]
            self.events.append(ev)
            if name == "frame":
                if ev.get("resend"):
                    if ev_name == "frame" and resend:
                        return ev
                    continue
                self.frames.append(Frame(ev))
            elif name == "beep":
                self.beeps.append(ev)
            elif name == "panel":
                self.panel.append(ev)
            elif name == "error":
                raise RuntimeError("simulator error: %r" % ev)
            if name == ev_name and not resend:
                return ev

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def sync(self):
        """Ask for a resend and read everything up to it: returns the NEW frames since the last sync."""
        before = len(self.frames)
        self.send("frame")
        self._read_until("frame", resend=True)
        return self.frames[before:]

    def tap(self, x, y):
        self.send("touch down %d %d" % (x, y))
        self.send("touch up %d %d" % (x, y))
        return self.sync()

    def tick(self, ms=0):
        self.send("tick %d" % ms)
        return self.sync()

    def quit(self):
        self.send("quit")
        self.p.stdin.close()
        self.p.wait(timeout=10)

    def last(self):
        return self.frames[-1]

    def save(self, frame, name):
        if self.png_dir:
            frame.png(os.path.join(self.png_dir, name + ".png"))


# ---- assertions ----------------------------------------------------------------------------
class Check:
    def __init__(self):
        self.count = 0

    def __call__(self, cond, msg):
        self.count += 1
        if not cond:
            print("FAIL [%d]: %s" % (self.count, msg))
            sys.exit(1)
        print("  ok  [%d] %s" % (self.count, msg))


check = Check()


def is_board_frame(fr):
    """The board frame: its left edge column x=15 is solid black from y=15 to 464."""
    return fr.ink((15, 15, 1, 450)) == 450 and fr.ink((15, 15, 450, 1)) == 450


def box_present(fr, box):
    x, y, w, h = box
    return fr.ink((x, y, w, 1)) == w and fr.ink((x, y + h - 1, w, 1)) == w and fr.ink((x, y, 1, h)) == h


class Session:
    """Runs the scenario, keeping the tap/frame bookkeeping for the policy checks."""

    def __init__(self, sim):
        self.sim = sim
        self.taps = 0
        self.frames_from_taps = 0
        self.partials = 0          # partial frames since the last full/deep, as the app counts them
        self.upgrades = 0          # pause repaints that became Full because partials >= 16
        self.screen_fulls = 0      # Fulls asked for by name: screen changes and flips
        self.log = []              # (description, expected_kind_rule, frames)

    def step(self, desc, x, y, expect_frames=1, kind=None, allow_full_upgrade=False):
        """One tap. Checks the frame count and, if given, the refresh kind."""
        partials_before = self.partials
        frames = self.sim.tap(x, y)
        self.taps += 1
        self.frames_from_taps += len(frames)
        check(len(frames) == expect_frames, "%s: %d frame(s) for one tap (expected %d)" % (desc, len(frames), expect_frames))
        for fr in frames:
            expected = kind
            if allow_full_upgrade and kind == "partial" and partials_before >= PARTIALS_BEFORE_FULL:
                expected = "full"
                self.upgrades += 1
            if expected is not None:
                check(fr.kind == expected, "%s: refresh is %s (partials since full before: %d)" % (desc, fr.kind, partials_before))
            if expected == "full" and kind == "full":
                self.screen_fulls += 1
            self.partials = self.partials + 1 if fr.kind == "partial" else 0
        return frames

    def tap_square(self, name, desc=None, expect_frames=1, kind="partial", pause=False, flipped=False):
        x, y, w, h = square_rect(name, flipped)
        return self.step(desc or ("tap " + name), x + w // 2, y + h // 2, expect_frames, kind, allow_full_upgrade=pause)

    def move(self, frm, to, desc=None):
        """Select + play: two taps, two frames; the second is a natural pause."""
        self.tap_square(frm, "%s: select %s" % (desc or frm + to, frm))
        return self.tap_square(to, "%s: play %s" % (desc or frm + to, to), pause=True)[0]

    def note_tick_frames(self, frames):
        for fr in frames:
            self.partials = self.partials + 1 if fr.kind == "partial" else 0


def main():
    png_dir = None
    if "--png" in sys.argv:
        png_dir = sys.argv[sys.argv.index("--png") + 1]
        os.makedirs(png_dir, exist_ok=True)
    if not os.path.exists(SIM):
        print("missing %s: run sim/build.sh first" % SIM)
        sys.exit(2)

    sim = Sim(png_dir)
    s = Session(sim)
    check(sim.hello["app"] == "arrocco", "hello: app is %r" % sim.hello["app"])
    check(sim.hello["virtual_time"] is True, "hello: virtual time")
    sim.send("set scale 0")

    # ---- 1. boot: the menu -----------------------------------------------------------
    boot = sim.frames_at_boot
    check(len(boot) == 1, "boot: exactly one frame (%d)" % len(boot))
    menu = boot[0]
    sim.save(menu, "01_menu")
    check(menu.kind == "deep", "boot: first paint is a Deep refresh (%s)" % menu.kind)
    check(not is_board_frame(menu), "menu: no board frame")
    check(menu.ink((16, 448, 200, 24)) > 40, "menu: battery footer text present")
    check(menu.ink(MENU_BTN(1)) > 300, "menu: 'Two players' button has ink")
    check(menu.ink(MENU_BTN(0)) == 0, "menu: no 'Resume' button before any game")
    check(menu.ink((0, 0, 800, 90)) > 500, "menu: title present")

    # Still-greyed buttons do nothing: no frame. ('Play vs engine' is live now and has
    # its own session, test/ui/engine_session.py.)
    s.step("menu: tap greyed 'Puzzles'", *center(MENU_BTN(3)), expect_frames=0)
    s.step("menu: tap greyed 'Lichess'", *center(MENU_BTN(4)), expect_frames=0)

    # ---- 2. Two players -> clock picker -> game ------------------------------------------------
    picker = s.step("menu: tap 'Two players'", *center(MENU_BTN(MENU_SLOT_TWO_PLAYERS)), kind="full")[0]
    sim.save(picker, "02_clock_picker")
    check(not is_board_frame(picker) and picker.region(MENU_BTN(0)) != menu.region(MENU_BTN(0)),
          "clock picker: a different full-screen menu")
    game0 = s.step("clock picker: tap 'No clock'", *center(MENU_BTN(CLOCK_SLOT_OFF)), kind="deep")[0]
    sim.save(game0, "03_game_start")
    check(is_board_frame(game0), "game: board frame drawn")
    check(game0.ink((16, 464, 448, 16)) > 100, "game: file letters a-h under the board")
    check(game0.ink((0, 16, 16, 448)) > 100, "game: rank digits left of the board")
    # 32 pieces: every square of ranks 1, 2, 7, 8 has a disc; ranks 3-6 are bare.
    for sq in ("a1", "e1", "h2", "d7", "b8", "h8"):
        check(game0.ink(square_center_box(sq, 40)) > 300, "game: piece on %s" % sq)
    check(game0.ink(square_center_box("e4", 40)) < 500, "game: e4 is empty")
    check(game0.ink(HEADLINE) > 100, "game: headline 'White to move' present")
    check(game0.ink(CLOCK_ROW) == 0, "game: clock block blank when the clock is off")
    white_to_move = game0.region(HEADLINE)

    # Tapping an empty square or a black piece with nothing selected: no refresh at all.
    s.tap_square("e4", "tap empty e4 with no selection", expect_frames=0)
    s.tap_square("e7", "tap Black's e7 on White's turn", expect_frames=0)

    # ---- 3. select e2: dots on e3 and e4 ---------------------------------------------------------
    sel = s.tap_square("e2", "select e2")[0]
    sim.save(sel, "04_select_e2")
    for sq in ("e3", "e4"):
        box = square_center_box(sq)
        check(sel.ink(box) - game0.ink(box) > 80, "select e2: legal-target dot on %s (+%d px)" % (sq, sel.ink(box) - game0.ink(box)))
    for sq in ("d3", "d4", "f4"):
        check(sel.region(square_rect(sq)) == game0.region(square_rect(sq)), "select e2: %s untouched" % sq)
    e2 = square_rect("e2")
    check(sel.ink((e2[0] + 1, e2[1] + 1, 54, 3)) >= 150, "select e2: 3 px selection frame on e2")

    # ---- 4. play e4: "1. e4", Black to move -----------------------------------------------
    after_e4 = s.tap_square("e4", "play e4", pause=True)[0]
    sim.save(after_e4, "05_after_e4")
    check(after_e4.ink(square_center_box("e4", 40)) > 300, "1. e4: pawn now on e4")
    check(after_e4.ink(square_center_box("e2", 30)) == 0, "1. e4: e2 empty (light square, centre clear)")
    check(after_e4.ink((e2[0], e2[1], 8, 8)) > 20, "1. e4: last-move corner mark on e2")
    check(after_e4.region(HEADLINE) != white_to_move and after_e4.ink(HEADLINE) > 100, "1. e4: headline changed to 'Black to move'")
    check(after_e4.region(SUBLINE) != game0.region(SUBLINE) and after_e4.ink(SUBLINE) > 40, "1. e4: last-move line shows '1. e4'")
    check(after_e4.ink(MOVELIST_ROW1) > 40, "1. e4: move list row 1 has '1. e4'")
    black_to_move = after_e4.region(HEADLINE)
    check(len(sim.beeps) >= 2, "sounds: select and move beeps emitted (%d)" % len(sim.beeps))

    # ---- 5. the miniature: 1.e4 e5 2.Bc4 Nc6 3.Qh5 Nf6 4.Qxf7# -------------------------------
    after_e5 = s.move("e7", "e5", "1... e5")
    check(after_e5.region(HEADLINE) == white_to_move, "1... e5: headline back to 'White to move'")
    s.move("f1", "c4", "2. Bc4")
    s.move("b8", "c6", "2... Nc6")
    s.move("d1", "h5", "3. Qh5")
    s.move("g8", "f6", "3... Nf6")
    over = s.tap_square("h5", "4. Qxf7#: select h5")
    over = s.step("4. Qxf7#: play f7", *center(square_rect("f7")), kind="deep")[0]
    sim.save(over, "06_checkmate")
    check(box_present(over, GAMEOVER_BOX), "checkmate: game-over overlay box drawn over the board")
    check(over.ink((60, 160, 360, 30)) > 100, "checkmate: result line ('Black wins'/'White wins') present")
    check(over.ink((60, 190, 360, 26)) > 100, "checkmate: reason line ('Checkmate') present")
    checkmate_reason = over.region((60, 190, 360, 26))
    check(over.ink(GAMEOVER_BTN(1)) > 200, "checkmate: 'Review' button drawn")
    check(sum(1 for b in sim.beeps[-2:]) == 2 and sim.beeps[-1]["hz"] == sim.beeps[-2]["hz"],
          "checkmate: double beep")
    check(s.frames_from_taps == 16, "miniature: 16 frames for the 16 taps that changed something (%d)" % s.frames_from_taps)
    check(all(fr.kind != "full" for fr in sim.frames[3:]), "miniature: no Full refresh in 14 partials + the Deep at game end")

    # A tap outside the overlay buttons does nothing.
    s.step("game over: tap the board under the overlay", *center(square_rect("a1")), expect_frames=0)

    # ---- 6. review ---------------------------------------------------------------------------
    review = s.step("game over: tap 'Review'", *center(GAMEOVER_BTN(1)), kind="partial")[0]
    sim.save(review, "07_review")
    check(not box_present(review, GAMEOVER_BOX), "review: overlay gone")
    check(review.ink(square_center_box("f7", 40)) > 300, "review: queen still on f7 at the latest position")
    back1 = s.step("review: tap '<'", *center(SIDE_BTN(REV_PREV)), kind="partial")[0]
    sim.save(back1, "08_review_back")
    check(back1.ink(square_center_box("h5", 40)) > 300 and back1.region(square_rect("f7")) != review.region(square_rect("f7")),
          "review '<': queen back on h5, f7 changed")
    fwd = s.step("review: tap '>'", *center(SIDE_BTN(REV_NEXT)), kind="partial")[0]
    check(fwd.data == review.data, "review '>': frame identical to the one before '<'")
    s.step("review: tap '>' at the latest position", *center(SIDE_BTN(REV_NEXT)), expect_frames=0)

    # ---- 7. new game from review, then Undo ---------------------------------------------------------
    fresh = s.step("review: tap 'New game'", *center(SIDE_BTN(4)), kind="deep")[0]
    check(fresh.data == game0.data, "new game: frame identical to the first game frame")
    s.move("e2", "e4", "1. e4 (again)")
    undone = s.step("tap 'Undo'", *center(SIDE_BTN(BTN_UNDO)), kind="partial")[0]
    sim.save(undone, "09_after_undo")
    check(undone.data == game0.data, "undo: frame identical to the game start")
    s.step("tap 'Undo' with nothing to take back", *center(SIDE_BTN(BTN_UNDO)), expect_frames=0)

    # ---- 8. promotion: 1.a4 b5 2.axb5 a6 3.bxa6 Bb7 4.axb7 Nc6 5.bxa8=Q --------------------------
    # Somewhere in here the partial counter reaches 16 and a move upgrades to Full.
    s.move("a2", "a4", "1. a4")
    s.move("b7", "b5", "1... b5")
    s.move("a4", "b5", "2. axb5")
    s.move("a7", "a6", "2... a6")
    s.move("b5", "a6", "3. bxa6")
    s.move("c8", "b7", "3... Bb7")
    s.move("a6", "b7", "4. axb7")
    before_promo = s.move("b8", "c6", "4... Nc6")
    check(s.upgrades == 1, "refresh policy: exactly one move so far was upgraded to Full at 16 partials (%d)" % s.upgrades)
    check(before_promo.ink((SIDE_X, 280, 288, 22)) > 40, "material line present after captures")
    s.tap_square("b7", "promotion: select b7")
    popup = s.tap_square("a8", "promotion: tap a8 opens the popup")[0]
    sim.save(popup, "10_promotion_popup")
    check(box_present(popup, PROMO_BOX), "promotion: popup box over the board")
    for i in range(4):
        check(popup.ink(PROMO_BTN(i)) > 400, "promotion: choice button %d drawn with a piece" % i)
    cancelled = s.step("promotion: tap outside the popup cancels", SIDE_X + 100, 130, kind="partial")[0]
    check(cancelled.data == before_promo.data, "promotion cancelled: frame identical to before the selection")
    s.tap_square("b7", "promotion: select b7 again")
    s.tap_square("a8", "promotion: tap a8 again")
    promoted = s.step("promotion: choose Queen", *center(PROMO_BTN(0)), kind="partial", allow_full_upgrade=True)[0]
    sim.save(promoted, "11_promoted")
    check(promoted.ink(square_center_box("a8", 40)) > 300, "promotion: a piece on a8")
    check(promoted.ink(square_center_box("b7", 30)) == 0, "promotion: b7 empty")
    check(promoted.region(HEADLINE) == black_to_move, "promotion: Black to move")

    # ---- 9. flip: Full, board upside down ---------------------------------------------------------------
    flipped = s.step("tap 'Flip'", *center(SIDE_BTN(BTN_FLIP)), kind="full")[0]
    sim.save(flipped, "12_flipped")
    # Both a8 and h1 are light squares: after the flip the top-left square shows exactly
    # what the bottom-right one showed before (the white rook from h1).
    check(flipped.region(square_rect("a8")) == promoted.region(square_rect("h1")) and
          flipped.region(square_rect("a8")) != promoted.region(square_rect("a8")),
          "flip: h1 rook now drawn top-left, board upside down")
    s.step("tap 'Flip' back", *center(SIDE_BTN(BTN_FLIP)), kind="full")

    # ---- 10. resign via the confirm popup ---------------------------------------------------------------
    confirm = s.step("tap 'Resign / Draw'", *center(SIDE_BTN(BTN_RESIGN)), kind="partial")[0]
    sim.save(confirm, "13_confirm")
    check(box_present(confirm, CONFIRM_BOX), "resign: confirm popup drawn")
    s.step("resign: tap 'Cancel'", *center(CONFIRM_BTN(2)), kind="partial")
    s.step("tap 'Resign / Draw' again", *center(SIDE_BTN(BTN_RESIGN)), kind="partial")
    resigned = s.step("resign: tap 'Black resigns'", *center(CONFIRM_BTN(0)), kind="deep")[0]
    sim.save(resigned, "14_resigned")
    check(box_present(resigned, GAMEOVER_BOX), "resign: game-over overlay")
    check(resigned.region((60, 190, 360, 26)) != checkmate_reason, "resign: reason line differs from 'Checkmate'")

    # ---- 11. menu shows no Resume for a finished game; settings toggles ------------------------------
    menu2 = s.step("game over: tap 'Menu'", *center(GAMEOVER_BTN(2)), kind="full")[0]
    check(menu2.ink(MENU_BTN(0)) == 0, "menu: no 'Resume' for a finished game")
    settings = s.step("menu: tap 'Settings'", *center(MENU_BTN(MENU_SLOT_SETTINGS)), kind="full")[0]
    sim.save(settings, "15_settings")
    toggled = s.step("settings: toggle sound", *center(MENU_BTN(1)), kind="partial")[0]
    check(toggled.region(MENU_BTN(1)) != settings.region(MENU_BTN(1)), "settings: sound label changed")
    beeps_before = len(sim.beeps)
    s.step("settings: tap 'Back'", *center(MENU_BTN(5)), kind="full")

    # ---- 12. the clock: 5+0, cadence 10 s, then 1 s below 20 s, then timeout -------------------------
    s.step("menu: tap 'Two players'", *center(MENU_BTN(MENU_SLOT_TWO_PLAYERS)), kind="full")
    clocked = s.step("clock picker: tap '5 + 0'", *center(MENU_BTN(CLOCK_SLOT_5)), kind="deep")[0]
    sim.save(clocked, "16_clock_game")
    check(clocked.ink(CLOCK_ROW) > 200, "clock: both clocks drawn")
    check(clocked.ink(MARK_WHITE) > 10 and clocked.ink(MARK_BLACK) == 0, "clock: running-side marker next to 'White'")
    s.tap_square("e2", "clock: select e2")
    check(len(sim.beeps) == beeps_before, "sound off: no beep on select")
    e4c = s.tap_square("e4", "clock: play e4", pause=True)[0]
    check(e4c.ink(MARK_WHITE) == 0 and e4c.ink(MARK_BLACK) > 10, "clock: after the move the marker moved to Black")
    t0 = e4c.t
    # Black's clock runs: 60 one-second ticks must repaint about every 10 s.
    ticks = []
    for _ in range(60):
        fr = sim.tick(1000)
        s.note_tick_frames(fr)
        check(len(fr) <= 1, "clock tick: at most one frame per tick")
        ticks.extend(fr)
    check(5 <= len(ticks) <= 7, "clock: %d repaints in 60 s of ticks (expected ~6)" % len(ticks))
    gaps = [b.t - a.t for a, b in zip(ticks, ticks[1:])]
    check(all(8500 <= g <= 11500 for g in gaps), "clock: repaint gaps of ~10 s: %s" % gaps)
    check(all(fr.kind == "partial" for fr in ticks), "clock: cadence repaints are Partial")
    check(ticks[-1].region(CLOCK_ROW) != e4c.region(CLOCK_ROW), "clock: the shown time changed")
    sim.save(ticks[-1], "17_clock_60s")

    # Jump close to 20 s on Black's clock, then tick by the second.
    elapsed = ticks[-1].t - t0
    remaining = 300000 - elapsed
    jump = sim.tick(remaining - 24000)
    s.note_tick_frames(jump)
    fast = []
    for _ in range(12):
        fr = sim.tick(1000)
        s.note_tick_frames(fr)
        fast.extend(fr)
    check(len(fast) >= 8, "clock: %d repaints in 12 s once below 20 s (every second)" % len(fast))
    fast_gaps = [b.t - a.t for a, b in zip(fast[-5:], fast[-4:])]
    check(all(900 <= g <= 1600 for g in fast_gaps), "clock: one-second cadence below 20 s: %s" % fast_gaps)
    sim.save(fast[-1], "18_clock_under_20s")

    timeout = sim.tick(30000)
    s.note_tick_frames(timeout)
    check(len(timeout) == 1 and timeout[0].kind == "deep", "clock: timeout ends the game with one Deep frame")
    sim.save(timeout[0], "19_timeout")
    check(box_present(timeout[0], GAMEOVER_BOX), "timeout: game-over overlay ('White wins' / 'Time out')")
    check(timeout[0].region((60, 190, 360, 26)) != checkmate_reason, "timeout: reason line differs from 'Checkmate'")
    idle = sim.tick(1000) + sim.tick(1000)
    check(len(idle) == 0, "timeout: no more clock repaints")

    # ---- 13. panel off after 3 s idle ----------------------------------------------------------------
    panel_before = len(sim.panel)
    sim.tick(3100)
    offs = [p for p in sim.panel[panel_before:] if not p["on"]]
    check(len(offs) == 1, "panel: panelOff() once after 3 s without touches")
    sim.tick(1000)
    check(len(sim.panel) == panel_before + 1, "panel: panelOff() not repeated")

    # ---- 14. totals ------------------------------------------------------------------------------
    all_kinds = [fr.kind for fr in sim.frames]
    print("frames: %d total, taps: %d, frames from taps: %d, kinds: partial=%d full=%d deep=%d" % (
        len(sim.frames), s.taps, s.frames_from_taps, all_kinds.count("partial"), all_kinds.count("full"),
        all_kinds.count("deep")))
    check(s.frames_from_taps <= s.taps, "never more than one frame per tap")
    fulls = [fr for fr in sim.frames if fr.kind == "full"]
    check(len(fulls) == s.screen_fulls + s.upgrades,
          "refresh policy: %d Full frames = %d screen changes/flips + %d upgrades at >= 16 partials" % (
              len(fulls), s.screen_fulls, s.upgrades))
    sim.quit()
    print("ALL %d CHECKS PASSED" % check.count)


if __name__ == "__main__":
    main()
