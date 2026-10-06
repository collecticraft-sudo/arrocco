#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - a game survives a power cut (python3 stdlib only).

Runs sim/build/arrocco-sim with --state DIR, a temporary directory that plays the board's
flash, and cuts the power the hard way: SIGKILL, so nothing in the process runs on the
way out, then a new process on the same DIR, as when the battery dies or the cable comes
out. Checks that:

  - the menu offers "Resume game", with a subline that tells the games apart;
  - resuming paints the very frame that was on the glass before the cut: the position,
    the move list, the orientation and the clocks, which stood still in the meantime
    (as they do in the menu, power cut or not);
  - Undo and threefold repetition still see the moves from before the cut, and the
    engine is asked again when it was its turn;
  - looking through the setup screens does not touch the saved game; a new game replaces
    it only after "Start a new game?"; a finished one is not offered; junk or another
    format version in the flash is ignored;
  - the flash gets one write per move and at the start and end of a game, none for a
    selection, a clock repaint or the setup screens, and always after the refresh.

    usage: test/ui/resume_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion.
"""
import os
import shutil
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_session import (Sim, check, center, square_rect, is_board_frame, box_present,  # noqa: E402
                         MENU_BTN, SIDE_BTN, GAMEOVER_BOX, HEADLINE, CLOCK_ROW, BTN_NEW, BTN_UNDO, BTN_FLIP,
                         BTN_MENU, MENU_SLOT_TWO_PLAYERS)
from engine_session import wait_for_engine  # noqa: E402

MENU_SLOT_RESUME, MENU_SLOT_ENGINE = 0, 2
ENGINE_SLOT_LEVEL, ENGINE_SLOT_START, ENGINE_SLOT_BACK = 1, 2, 5
CLOCK_SLOT_OFF, CLOCK_SLOT_15_10, CLOCK_SLOT_BACK = 0, 3, 5
BOARD = (16, 16, 448, 448)
MOVE_LIST = (496, 168, 288, 104)            # five rows of two columns, baselines 184..264
RESUME_NOTE = (208, 124, 384, 18)           # the subline inside the "Resume game" button
REASON_LINE = (60, 190, 360, 26)            # game-over overlay: "Threefold repetition"
# "Start a new game?" (layout.h kNewGameBox*): centred on the board, or on the screen.
NEW_GAME_BOX_BOARD = (32, 144, 416, 184)
NEW_GAME_BOX_SCREEN = (192, 144, 416, 184)
NEW_GAME_BTN_BOARD = lambda i: (80 + i * 164, 256, 156, 48)     # new game / cancel
NEW_GAME_BTN_SCREEN = lambda i: (240 + i * 164, 256, 156, 48)


class Board:
    """One simulator process on the state directory: the board between two power cuts."""

    def __init__(self, state_dir, png_dir, name):
        self.sim = Sim(png_dir, ["--state", state_dir])
        self.sim.send("set scale 0")
        self.name = name
        boot = self.sim.frames_at_boot
        check(len(boot) == 1 and boot[0].kind == "deep", "%s: boot paints the menu once, Deep" % name)
        self.menu = boot[0]
        check(not is_board_frame(self.menu), "%s: the board starts on the menu" % name)
        check(not self.stores(), "%s: nothing written at boot" % name)

    def stores(self):
        return [e for e in self.sim.events if e["ev"] == "store"]

    def tap(self, rect_or_xy, desc, frames=1, kind=None):
        x, y = center(rect_or_xy) if len(rect_or_xy) == 4 else rect_or_xy
        got = self.sim.tap(x, y)
        check(len(got) == frames, "%s: %s gives %d frame(s) (%d)" % (self.name, desc, frames, len(got)))
        if kind is not None and got:
            check(got[-1].kind == kind, "%s: %s is a %s refresh (%s)" % (self.name, desc, kind, got[-1].kind))
        return got[-1] if got else None

    def move(self, frm, to, flipped=False, writes=1):
        """Select and play: the selection writes nothing, the move `writes` times, after its refresh."""
        before = len(self.stores())
        self.tap(square_rect(frm, flipped), "select " + frm)
        check(len(self.stores()) == before, "%s: selecting %s writes nothing" % (self.name, frm))
        fr = self.tap(square_rect(to, flipped), "play %s%s" % (frm, to))
        written = self.stores()[before:]
        check(len(written) == writes, "%s: %s%s writes the flash %d time(s) (%d)" % (
            self.name, frm, to, writes, len(written)))
        for e in written:
            check(e["ok"] and e["key"] == "game", "%s: the write of %s%s went through" % (self.name, frm, to))
            check(e["t"] >= fr.t + fr.ev["nominal_ms"],
                  "%s: %s%s is written after its refresh (t %d >= %d + %d)" % (
                      self.name, frm, to, e["t"], fr.t, fr.ev["nominal_ms"]))
        return fr

    def resume_region(self, frame=None):
        return (frame or self.menu).region(MENU_BTN(MENU_SLOT_RESUME))

    def offers_resume(self, frame=None):
        fr = frame or self.menu
        return fr.ink(MENU_BTN(MENU_SLOT_RESUME)) > 300 and fr.ink(RESUME_NOTE) > 40

    def cut(self):
        self.sim.kill()


def blob_path(state_dir):
    return os.path.join(state_dir, "game.bin")


def read_blob(state_dir):
    with open(blob_path(state_dir), "rb") as f:
        return f.read()


def write_blob(state_dir, data):
    with open(blob_path(state_dir), "wb") as f:
        f.write(data)


def reseal(data):
    """A correct CRC-32 (the zlib one, as saved_game.h says) over an edited blob."""
    return data[:-4] + struct.pack("<I", zlib.crc32(data[:-4]) & 0xFFFFFFFF)


def main():
    png_dir = None
    if len(sys.argv) == 3 and sys.argv[1] == "--png":
        png_dir = sys.argv[2]
        os.makedirs(png_dir, exist_ok=True)
    state_dir = tempfile.mkdtemp(prefix="arrocco-resume-")
    try:
        run(state_dir, png_dir)
    finally:
        shutil.rmtree(state_dir, ignore_errors=True)
    print("ALL %d CHECKS PASSED" % check.count)


def run(state_dir, png_dir):
    # ---- 1. two players, 15 + 10, a flip, then the power goes ----------------------------------
    b = Board(state_dir, png_dir, "first start")
    check(b.menu.ink(MENU_BTN(MENU_SLOT_RESUME)) == 0, "first start: no 'Resume game' on a new board")
    check(not os.path.exists(blob_path(state_dir)), "first start: the flash holds no game")
    b.tap(MENU_BTN(MENU_SLOT_TWO_PLAYERS), "'Two players'", kind="full")
    start = b.tap(MENU_BTN(CLOCK_SLOT_15_10), "'15 + 10'", kind="deep")
    check(len(b.stores()) == 1 and b.stores()[0]["t"] >= start.t + start.ev["nominal_ms"],
          "a new game is written once, after its Deep refresh")
    b.move("g1", "f3")
    b.move("g8", "f6")
    b.move("f3", "g1")
    after4 = b.move("f6", "g8")
    stores = len(b.stores())
    ticks = []
    for _ in range(25):
        ticks += b.sim.tick(1000)
    check(len(ticks) >= 2, "the running clock repaints (%d frames in 25 s)" % len(ticks))
    check(len(b.stores()) == stores, "clock repaints write nothing")
    # The menu is a pause: the clock stops there, and "Resume game" starts it again.
    b.tap(SIDE_BTN(BTN_MENU), "'Menu'", kind="full")
    check(len(b.stores()) == stores + 1, "going to the menu writes the clock as it stopped")
    check(len(b.sim.tick(120000)) == 0, "two minutes in the menu: no repaint, no flag falls")
    paused = b.tap(MENU_BTN(MENU_SLOT_RESUME), "'Resume game' in the same session", kind="full")
    check(paused.region(CLOCK_ROW) == ticks[-1].region(CLOCK_ROW),
          "the two minutes in the menu were not charged to White")
    check(len(b.stores()) == stores + 1, "resuming writes nothing")
    b.tap(SIDE_BTN(BTN_FLIP), "'Flip'", kind="full")
    check(len(b.stores()) == stores + 2, "a flip is written: the orientation is part of the game")
    before_cut = b.move("g1", "f3", flipped=True)
    b.sim.save(before_cut, "r01_before_the_cut")
    check(is_board_frame(before_cut) and before_cut.ink(CLOCK_ROW) > 200, "on the glass: the board, with clocks")
    saved = read_blob(state_dir)
    check(saved[:5] == b"ARSG\x01" and len(saved) == 27 + 2 * 5, "the flash holds five plies (%d bytes)" % len(saved))
    b.cut()

    # ---- 2. power back: the menu offers it, the clocks stood still ----------------------------------
    b = Board(state_dir, png_dir, "after the cut")
    b.sim.save(b.menu, "r02_menu_after_the_cut")
    check(b.offers_resume(), "after the cut: 'Resume game' with a subline")
    two_players_move3 = b.resume_region()
    check(len(b.sim.tick(60000)) == 0, "a minute in the menu: no repaint")
    resumed = b.tap(MENU_BTN(MENU_SLOT_RESUME), "'Resume game'", kind="full")
    b.sim.save(resumed, "r03_resumed")
    check(resumed.data == before_cut.data,
          "resumed: the very frame of before the cut (board, flip, move list, clocks)")
    check(not b.stores(), "resuming writes nothing: nothing changed")
    running = []
    for _ in range(11):
        running += b.sim.tick(1000)
    check(len(running) >= 1 and running[-1].region(CLOCK_ROW) != resumed.region(CLOCK_ROW),
          "Black's clock runs again once resumed")

    # ---- 3. the history from before the cut: Undo, then a threefold repetition ------------------
    b.tap(SIDE_BTN(BTN_FLIP), "'Flip' back", kind="full")
    undone = b.tap(SIDE_BTN(BTN_UNDO), "'Undo'")
    check(undone.region(BOARD) == after4.region(BOARD), "Undo takes back the move played before the cut")
    check(undone.region(MOVE_LIST) == after4.region(MOVE_LIST), "and the move list is the one from before the cut")
    check(len(b.stores()) == 2, "the flip and the Undo are written (%d)" % len(b.stores()))
    b.move("g1", "f3")
    b.move("g8", "f6")
    b.move("f3", "g1")
    b.tap(square_rect("f6"), "select f6")
    stores = len(b.stores())
    over = b.tap(square_rect("g8"), "play f6g8: the start position for the third time", kind="deep")
    b.sim.save(over, "r04_threefold_over_the_cut")
    check(box_present(over, GAMEOVER_BOX) and over.ink(REASON_LINE) > 100,
          "threefold repetition, counted across the cut")
    check(len(b.stores()) == stores + 1, "the end of the game is written")
    b.cut()

    # ---- 4. a finished game is not offered ---------------------------------------------------
    b = Board(state_dir, png_dir, "after a finished game")
    check(b.menu.ink(MENU_BTN(MENU_SLOT_RESUME)) == 0, "a finished game: no 'Resume game'")

    # ---- 5. against the engine: power cut while it thinks -------------------------------------
    b.tap(MENU_BTN(MENU_SLOT_ENGINE), "'Play vs engine'", kind="full")
    b.tap(MENU_BTN(ENGINE_SLOT_START), "'Choose the clock and start'", kind="full")
    b.tap(MENU_BTN(CLOCK_SLOT_OFF), "'No clock'", kind="deep")
    check(len(b.stores()) == 1, "the engine game replaces the finished one in the flash")
    thinking = b.move("e2", "e4")
    b.sim.save(thinking, "r05_engine_thinking")
    b.cut()                       # the engine's answer was never taken

    b = Board(state_dir, png_dir, "cut while the engine thought")
    check(b.offers_resume(), "the engine game is offered")
    vs_engine_move1 = b.resume_region()
    check(vs_engine_move1 != two_players_move3, "its subline is not the two-player one")
    again = b.tap(MENU_BTN(MENU_SLOT_RESUME), "'Resume game'", kind="full")
    check(again.data == thinking.data, "resumed: 'Thinking...' over the same board, the engine asked again")
    answer, quiet = wait_for_engine(b.sim)
    check(len(answer) == 1, "the engine answers with one frame (after %d empty ticks)" % quiet)
    check(len(b.stores()) == 1, "its move is written")
    answered = answer[0]

    # ---- 6. the setup screens do not touch the game in progress --------------------------------
    menu = b.tap(SIDE_BTN(BTN_MENU), "'Menu'", kind="full")
    check(b.offers_resume(menu), "the menu offers the game")
    stores = len(b.stores())
    picker = b.tap(MENU_BTN(MENU_SLOT_TWO_PLAYERS), "'Two players'", kind="full")
    # A clock picked with a game waiting behind "Resume game": the picker asks first.
    asked = b.tap(MENU_BTN(CLOCK_SLOT_OFF), "'No clock' with a game in progress", kind="partial")
    b.sim.save(asked, "r06_picker_asks")
    check(box_present(asked, NEW_GAME_BOX_SCREEN), "'Start a new game?' over the clock picker")
    kept = b.tap(NEW_GAME_BTN_SCREEN(1), "'Cancel'", kind="partial")
    check(kept.data == picker.data, "Cancel: back to the clocks, nothing started")
    b.tap(MENU_BTN(CLOCK_SLOT_BACK), "clock picker 'Back'", kind="full")
    b.tap(MENU_BTN(MENU_SLOT_ENGINE), "'Play vs engine'", kind="full")
    b.tap(MENU_BTN(ENGINE_SLOT_LEVEL), "a stronger level")
    b.tap(MENU_BTN(ENGINE_SLOT_BACK), "'Back'", kind="full")
    check(len(b.stores()) == stores, "looking through the setup screens writes nothing")
    b.cut()

    b = Board(state_dir, png_dir, "after the setup screens")
    check(b.resume_region() == menu.region(MENU_BTN(MENU_SLOT_RESUME)),
          "still the engine game at move 2, not a two-player game")
    back = b.tap(MENU_BTN(MENU_SLOT_RESUME), "'Resume game'", kind="full")
    check(back.data == answered.data, "the board after the engine's move, White to move")

    # ---- 7. a new game replaces the stored one, after a question ---------------------------------
    asked = b.tap(SIDE_BTN(BTN_NEW), "'New game' in a game with moves", kind="partial")
    b.sim.save(asked, "r07_new_game_asks")
    check(box_present(asked, NEW_GAME_BOX_BOARD), "'Start a new game?' over the board")
    kept = b.tap(NEW_GAME_BTN_BOARD(1), "'Cancel'", kind="partial")
    check(kept.data == back.data, "Cancel: the game as it was")
    check(not b.stores(), "asking and cancelling write nothing")
    b.tap(SIDE_BTN(BTN_NEW), "'New game' again", kind="partial")
    b.tap(NEW_GAME_BTN_BOARD(0), "'New game' in the question", kind="deep")
    check(len(b.stores()) == 1, "the new game is written")
    b.cut()
    b = Board(state_dir, png_dir, "after New game")
    check(b.resume_region() == vs_engine_move1, "the new game is offered: vs engine, move 1")
    good = read_blob(state_dir)
    b.cut()

    # ---- 8. what is not a saved game is ignored ---------------------------------------------------
    for desc, data in (("junk", b"\x00\x13garbage in the flash" * 7),
                       ("half a blob", good[:len(good) // 2]),
                       ("a flipped bit", good[:10] + bytes([good[10] ^ 0x20]) + good[11:]),
                       ("format version 2", reseal(good[:4] + b"\x02" + good[5:])),
                       ("an empty file", b"")):
        write_blob(state_dir, data)
        b = Board(state_dir, png_dir, desc)
        check(b.menu.ink(MENU_BTN(MENU_SLOT_RESUME)) == 0, "%s in the flash: nothing offered, the menu as new" % desc)
        b.cut()
    write_blob(state_dir, good)
    b = Board(state_dir, png_dir, "the good blob back")
    check(b.resume_region() == vs_engine_move1, "the same blob, intact again, is offered again")
    b.sim.quit()


if __name__ == "__main__":
    main()
