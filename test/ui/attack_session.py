#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - adversarial scripted sessions against the simulator binary.

Complements sim_session.py (whose helpers it reuses): taps on gutters and frame,
wrong-side pieces, castling, en passant, every promotion piece including a
capture-promotion and an underpromotion giving check (through Undo), stalemate,
threefold repetition, resign / draw through the confirm popup, Undo at ply 0,
Flip then tap, review at both ends, stray Move/Up events, out-of-range
coordinates, the clock across present(), timeout winner, repaint cadence,
tick() refresh storms (idle, battery jitter) and touches during a real refresh.

    usage: test/ui/attack_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_session import (SIM, Sim, Session, check, center, square_rect, is_board_frame, box_present,  # noqa: E402
                         MENU_BTN, SIDE_BTN, PROMO_BTN, PROMO_BOX, GAMEOVER_BOX, GAMEOVER_BTN, HEADLINE,
                         SUBLINE, MENU_SLOT_TWO_PLAYERS, MENU_SLOT_SETTINGS, CLOCK_SLOT_OFF, CLOCK_SLOT_5,
                         BTN_NEW, BTN_UNDO, BTN_FLIP, BTN_RESIGN, BTN_MENU, REV_PREV, REV_NEXT)

CONFIRM_BOX = (64, 120, 352, 232)
CONFIRM_BTN = lambda i: (80, 120 + 56 + i * 56, 320, 48)     # resign / draw / cancel
MENU_SLOT_RESUME = 0
KING_RING = lambda name: square_rect(name)                    # ring hugs the halo: square ink jumps


def new_game(ses, clock_slot=CLOCK_SLOT_OFF, desc="new game"):
    ses.step(desc + ": Two players", *center(MENU_BTN(MENU_SLOT_TWO_PLAYERS)), kind="full")
    return ses.step(desc + ": clock", *center(MENU_BTN(clock_slot)), kind="deep")[0]


def play_line(ses, line, desc):
    """'e2e4 e7e5 ...' pairs of squares; the last frame is returned."""
    fr = None
    for i, mv in enumerate(line.split()):
        fr = ses.move(mv[:2], mv[2:4], "%s %d.%s" % (desc, i // 2 + 1, mv))
    return fr


def game_over_from(ses, frm, to, desc):
    ses.tap_square(frm, desc + ": select " + frm)
    fr = ses.tap_square(to, desc + ": final move " + to, kind="deep")[0]
    check(box_present(fr, GAMEOVER_BOX), desc + ": game-over box drawn")
    return fr


def main():
    png_dir = None
    if len(sys.argv) >= 3 and sys.argv[1] == "--png":
        png_dir = sys.argv[2]
        os.makedirs(png_dir, exist_ok=True)
    sim = Sim(png_dir)
    ses = Session(sim)
    sim.send("set scale 0")
    sim.sync()
    check(len(sim.frames_at_boot) == 1 and sim.frames_at_boot[0].kind == "deep", "boot: one Deep frame")

    # ---- stray input before anything ---------------------------------------------------------
    sim.send("touch move 100 100")
    sim.send("touch up 100 100")
    check(len(sim.sync()) == 0, "Move + Up without Down: no frame")
    sim.send("touch up 300 120")
    check(len(sim.sync()) == 0, "Up without Down on a menu button: no frame")

    # ---- game without clock -------------------------------------------------------------------
    start = new_game(ses)
    check(is_board_frame(start), "board drawn")
    for desc, (x, y) in [("rank gutter", (6, 200)), ("file gutter", (200, 472)), ("frame corner", (15, 15)),
                         ("gap board/side", (470, 240)), ("side blank", (600, 300)), ("bottom-right", (799, 479))]:
        ses.step("tap on " + desc, x, y, expect_frames=0)
    sim.send("touch down -50 -50")
    sim.send("touch up -50 -50")
    check(len(sim.sync()) == 0, "negative coordinates: no frame")
    sim.send("touch down 5000 5000")
    sim.send("touch up 5000 5000")
    check(len(sim.sync()) == 0, "coordinates beyond 800x480: no frame")
    ses.tap_square("e7", "opponent pawn while White to move", expect_frames=0)
    ses.tap_square("e4", "empty square, nothing selected", expect_frames=0)
    before = sim.last()
    fr = ses.tap_square("a1", "rook with no legal move: selected (frame, no dots)")
    check(fr[0].ink(square_rect("a1")) > before.ink(square_rect("a1")), "a1 shows the selection frame")
    check(fr[0].region(square_rect("a3")) == before.region(square_rect("a3")), "a3 has no target dot")
    fr = ses.tap_square("a1", "same piece again: deselect")
    check(fr[0].region(square_rect("a1")) == before.region(square_rect("a1")), "a1 back to plain")
    ses.step("Undo at ply 0", *center(SIDE_BTN(BTN_UNDO)), expect_frames=0)

    # A drag that ends far from where it started is not a tap.
    sim.send("touch down %d %d" % center(square_rect("e2")))
    sim.send("touch move %d %d" % center(square_rect("e3")))
    sim.send("touch up %d %d" % center(square_rect("e4")))
    check(len(sim.sync()) == 0, "Down e2 / Up e4 (slop exceeded): no frame")

    # Castling and en passant: 1.e4 a6 2.Nf3 d5 3.Bc4 d4 4.O-O c5 5.e5 f5 6.exf6 (e.p.)
    play_line(ses, "e2e4 a7a6 g1f3 d7d5 f1c4 d5d4", "castle setup")
    ses.tap_square("e1", "castling: select king")
    fr = ses.tap_square("g1", "castling: tap g1", pause=True)[0]
    check(fr.ink(square_rect("g1")) > 200 and fr.ink(square_rect("f1")) > 200, "king on g1, rook on f1")
    check(fr.ink((square_rect("h1")[0] + 12, square_rect("h1")[1] + 12, 32, 32)) == 0, "h1 empty after castling")
    play_line(ses, "c7c5 e4e5 f7f5", "ep setup")
    fr = ses.tap_square("e5", "en passant: select e5")[0]
    check(fr.ink((square_rect("f6")[0] + 20, square_rect("f6")[1] + 20, 16, 16)) > 40, "f6 shows a target dot")
    fr = ses.tap_square("f6", "en passant: play exf6", pause=True)[0]
    check(fr.ink((square_rect("f5")[0] + 12, square_rect("f5")[1] + 12, 32, 32)) == 0, "f5 pawn removed by e.p.")

    # Flip, then tap by flipped coordinates.
    fr = ses.step("Flip", *center(SIDE_BTN(BTN_FLIP)), kind="full")[0]
    ses.tap_square("e2", "flipped: tap where e2 is drawn (bottom-left region is h-file)", expect_frames=0)
    fr = ses.tap_square("g7", "flipped: Black's g7 pawn", flipped=True)[0]
    check(fr.ink(square_rect("g7", True)) > sim.frames[-2].ink(square_rect("g7", True)), "g7 selected on the flipped board")
    ses.tap_square("g7", "flipped: deselect", flipped=True)
    ses.step("Flip back", *center(SIDE_BTN(BTN_FLIP)), kind="full")

    # ---- resign through the popup, then New game from the overlay ------------------------------
    fr = ses.step("Resign/Draw", *center(SIDE_BTN(BTN_RESIGN)))[0]
    check(box_present(fr, CONFIRM_BOX), "confirm box drawn")
    ses.step("tap outside the confirm box", 700, 60)
    check(not box_present(sim.last(), CONFIRM_BOX), "confirm box gone after tap outside")
    ses.step("Resign/Draw again", *center(SIDE_BTN(BTN_RESIGN)))
    fr = ses.step("Black resigns", *center(CONFIRM_BTN(0)), kind="deep")[0]
    check(box_present(fr, GAMEOVER_BOX), "game-over box after resignation")
    headline_white_wins = fr.region(HEADLINE)
    ses.step("New game from overlay", *center(GAMEOVER_BTN(0)), kind="deep")
    check(is_board_frame(sim.last()) and not box_present(sim.last(), GAMEOVER_BOX), "fresh board")

    # ---- promotion to each piece via Undo; underpromotion N gives check -------------------------
    # 1.h4 g5 2.hxg5 h6 3.gxh6 Nf6 4.h7 Rg8 5.a3 e6 6.b3 Ke7 : pawn h7, rook g8, king e7, h8 empty
    play_line(ses, "h2h4 g7g5 h4g5 h7h6 g5h6 g8f6 h6h7 h8g8 a2a3 e7e6 b2b3 e8e7", "promo setup")
    plain = sim.last()
    ses.tap_square("h7", "promotion: select h7")
    fr = ses.tap_square("h8", "promotion: tap h8 -> popup")[0]
    check(box_present(fr, PROMO_BOX), "promotion popup drawn")
    fr = ses.step("promotion: tap the popup title (not a button)", PROMO_BOX[0] + 200, PROMO_BOX[1] + 20)[0]
    check(fr.region((16, 16, 448, 448)) == plain.region((16, 16, 448, 448)), "board back to pre-selection")
    ses.tap_square("h7", "promotion: select h7 again")
    ses.tap_square("h8", "promotion: popup")
    fr = ses.step("promotion: Queen", *center(PROMO_BTN(0)), kind="partial", allow_full_upgrade=True)[0]
    check(fr.ink(square_rect("h8")) > 200, "queen on h8")
    check(fr.ink((square_rect("h7")[0] + 12, square_rect("h7")[1] + 12, 32, 32)) == 0, "h7 empty after h8=Q")
    check(fr.region(SUBLINE) != plain.region(SUBLINE), "subline shows the promotion")
    e7_by_piece = {}
    for choice, name, gives_check in [(3, "Knight", True), (1, "Rook", False), (2, "Bishop", False)]:
        fr = ses.step("Undo the promotion", *center(SIDE_BTN(BTN_UNDO)))[0]
        check(fr.region((16, 16, 448, 448)) == plain.region((16, 16, 448, 448)), "board restored by Undo")
        ses.tap_square("h7", "capture-promotion: select h7")
        fr = ses.tap_square("g8", "capture-promotion: tap g8 (rook)")[0]
        check(box_present(fr, PROMO_BOX), "popup for the capture-promotion")
        fr = ses.step("capture-promotion: " + name, *center(PROMO_BTN(choice)), kind="partial", allow_full_upgrade=True)[0]
        check(fr.ink((square_rect("h7")[0] + 12, square_rect("h7")[1] + 12, 32, 32)) == 0, "h7 empty after =" + name)
        check(fr.ink(square_rect("g8")) > 200, "new piece on g8 (" + name + ")")
        # The e7 king is in check only after the knight promotion. Counting ink on the
        # square does not show that: the king's white halo removes more hatch than the
        # ring adds. Compare the three promotions against each other instead - same
        # square, same king, the only difference is the ring.
        e7_by_piece[name] = fr.region(square_rect("e7"))
    check(e7_by_piece["Rook"] == e7_by_piece["Bishop"], "e7 identical when nothing gives check")
    check(e7_by_piece["Knight"] != e7_by_piece["Rook"], "check ring drawn on the e7 king")

    # ---- Menu with a game in progress, Resume, then Draw by agreement -------------------------
    ses.step("Menu", *center(SIDE_BTN(BTN_MENU)), kind="full")
    ses.step("Resume game", *center(MENU_BTN(MENU_SLOT_RESUME)), kind="full")
    check(is_board_frame(sim.last()), "resumed on the board")
    ses.step("Resign/Draw", *center(SIDE_BTN(BTN_RESIGN)))
    fr = ses.step("Agree a draw", *center(CONFIRM_BTN(1)), kind="deep")[0]
    headline_draw = fr.region(HEADLINE)
    check(headline_draw != headline_white_wins, "Draw headline differs from White wins")
    ses.step("Menu from the overlay", *center(GAMEOVER_BTN(2)), kind="full")
    ses.step("Resume slot on the menu after game over: nothing", *center(MENU_BTN(MENU_SLOT_RESUME)), expect_frames=0)

    # ---- stalemate (Loyd): 1.e3 a5 2.Qh5 Ra6 3.Qxa5 h5 4.h4 Rah6 5.Qxc7 f6 6.Qxd7+ Kf7 7.Qxb7 Qd3 8.Qxb8 Qh7 9.Qxc8 Kg6 10.Qe6
    new_game(ses, desc="stalemate game")
    play_line(ses, "e2e3 a7a5 d1h5 a8a6 h5a5 h7h5 h2h4 a6h6 a5c7 f7f6 c7d7 e8f7 d7b7 d8d3 b7b8 d3h7 b8c8 f7g6",
              "stalemate")
    fr = game_over_from(ses, "c8", "e6", "stalemate")
    check(fr.region(HEADLINE) == headline_draw, "stalemate headline is Draw")
    reason_stalemate = fr.region((GAMEOVER_BOX[0], GAMEOVER_BOX[1] + 50, GAMEOVER_BOX[2], 30))

    # ---- review at both ends --------------------------------------------------------------------
    ses.step("Review", *center(GAMEOVER_BTN(1)))
    at_latest = sim.last()
    ses.step("> at latest", *center(SIDE_BTN(REV_NEXT)), expect_frames=0)
    for i in range(19):
        ses.step("< step %d" % (i + 1), *center(SIDE_BTN(REV_PREV)), kind="partial", allow_full_upgrade=True)
    check(fr.region(HEADLINE) == sim.last().region(HEADLINE), "headline stays the result while reviewing")
    ses.step("< at start", *center(SIDE_BTN(REV_PREV)), expect_frames=0)
    start_board = sim.last().region((16, 16, 448, 448))
    for i in range(19):
        ses.step("> step %d" % (i + 1), *center(SIDE_BTN(REV_NEXT)), kind="partial", allow_full_upgrade=True)
    check(sim.last().region((16, 16, 448, 448)) == at_latest.region((16, 16, 448, 448)), "board at latest again")
    ses.step("> at latest again", *center(SIDE_BTN(REV_NEXT)), expect_frames=0)
    ses.step("Flip in review", *center(SIDE_BTN(BTN_FLIP)), kind="full")
    ses.step("Flip back in review", *center(SIDE_BTN(BTN_FLIP)), kind="full")
    ses.step("Result", *center(SIDE_BTN(3)))
    check(box_present(sim.last(), GAMEOVER_BOX), "overlay back")

    # ---- threefold: 1.Nf3 Nf6 2.Ng1 Ng8 3.Nf3 Nf6 4.Ng1 Ng8 --------------------------------------
    ses.step("New game from overlay", *center(GAMEOVER_BTN(0)), kind="deep")
    play_line(ses, "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1", "threefold")
    fr = game_over_from(ses, "f6", "g8", "threefold")
    check(fr.region(HEADLINE) == headline_draw, "threefold headline is Draw")
    check(fr.region((GAMEOVER_BOX[0], GAMEOVER_BOX[1] + 50, GAMEOVER_BOX[2], 30)) != reason_stalemate,
          "threefold reason differs from stalemate reason")

    # ---- clock: charged across present(), cadence, timeout winner, no storm -------------------
    ses.step("Menu", *center(GAMEOVER_BTN(2)), kind="full")
    deep = new_game(ses, CLOCK_SLOT_5, "5+0 game")
    spent = deep.ev["nominal_ms"]
    check(spent >= 3000, "the Deep present took %d ms of virtual time" % spent)
    # White has been running since before the Deep present. Remaining = 300000 - spent - ticks.
    # Shown value (rounded up to 10 s) first changes when remaining <= 290000.
    wait = 10000 - spent                   # ms of ticks needed if present() was charged
    check(200 <= wait < 10000, "the Deep present left %d ms before 04:50 is due" % wait)
    frames = sim.tick(wait - 100)
    ses.note_tick_frames(frames)
    check(len(frames) == 0, "clock: 100 ms before 04:50 is due: no repaint")
    frames = sim.tick(100)
    ses.note_tick_frames(frames)
    check(len(frames) == 1 and frames[0].kind == "partial", "clock: 04:50 due exactly when present() time is counted")
    count = 0
    for _ in range(60):
        f = sim.tick(1000)
        ses.note_tick_frames(f)
        count += len(f)
    check(count == 6, "clock cadence: %d repaints in a virtual minute (expected 6)" % count)
    count = 0
    for _ in range(120):
        f = sim.tick(500)
        ses.note_tick_frames(f)
        count += len(f)
    check(count == 6, "clock cadence at 500 ms ticks: %d repaints in a minute (expected 6)" % count)
    ses.move("e2", "e4", "5+0 e4")
    frames = sim.tick(0)
    check(len(frames) == 0, "no repaint right after a move")
    # Black now runs. Black timing out must make White the winner.
    frames = sim.tick(310000)
    ses.note_tick_frames(frames)
    check(len(frames) == 1 and frames[0].kind == "deep", "timeout: one Deep frame")
    check(box_present(frames[0], GAMEOVER_BOX), "timeout: game-over box")
    check(frames[0].region(HEADLINE) == headline_white_wins, "timeout of Black: White wins")
    for _ in range(3):
        check(len(sim.tick(5000)) == 0, "no frames from idle ticks after game over")

    # Flag falls on the mover's own tap: a move tapped after the time ran out is a timeout.
    ses.step("Menu", *center(GAMEOVER_BTN(2)), kind="full")
    new_game(ses, CLOCK_SLOT_5, "5+0 game 2")
    fr = ses.tap_square("e2", "select e2 with 5 minutes")[0]
    left = 300000 - (fr.t - sim.frames[-2].t) - fr.ev["nominal_ms"] - 0   # White's time after the two presents
    frames = sim.tick(left - 1000)   # one tick: at most one clock repaint
    ses.note_tick_frames(frames)
    check(len(frames) == 1 and frames[0].kind == "partial", "one clock repaint for one big tick")
    sim.send("touch down %d %d" % center(square_rect("e4")))
    sim.send("tick 1500")            # nothing may refresh under a finger; the flag falls meanwhile
    check(len(sim.sync()) == 0, "tick under a finger: no refresh")
    sim.send("touch up %d %d" % center(square_rect("e4")))
    frames = sim.sync()
    check(len(frames) == 1 and frames[0].kind == "deep", "move tapped after the flag fell: Deep game over (%s)"
          % [f.kind for f in frames])
    check(frames[0].region(HEADLINE) != headline_white_wins and frames[0].region(HEADLINE) != headline_draw,
          "White timed out: Black wins")

    # A finger that never lifts must not freeze tick(): after kTouchHoldMaxMs the clock runs again.
    ses.step("Menu", *center(GAMEOVER_BTN(2)), kind="full")
    deep = new_game(ses, CLOCK_SLOT_5, "5+0 game 3")
    check(len(sim.tick(10000 - deep.ev["nominal_ms"] - 500)) == 0, "stuck finger: 04:50 not yet due")
    sim.send("touch down 600 300")
    check(len(sim.tick(1500)) == 0, "stuck finger: 04:50 due but no refresh under a fresh finger")
    frames = sim.tick(1000)
    ses.note_tick_frames(frames)
    check(len(frames) == 1, "stuck finger: the clock repaints again once the hold is stale")
    sim.send("touch up %d %d" % center(square_rect("e2")))
    check(len(sim.sync()) == 0, "stuck finger: the late Up is not a tap")
    ses.tap_square("e2", "normal tap works afterwards")
    ses.step("Menu", *center(SIDE_BTN(BTN_MENU)), kind="full")
    ses.step("Resume game at ply 0", *center(MENU_BTN(MENU_SLOT_RESUME)), kind="full")
    check(is_board_frame(sim.last()), "a started game with no move yet can be resumed")
    ses.step("Resign/Draw", *center(SIDE_BTN(BTN_RESIGN)))
    ses.step("White resigns", *center(CONFIRM_BTN(0)), kind="deep")

    # ---- menu: battery jitter must not cause a refresh storm ----------------------------------
    sim.send("set battery 50")
    sim.sync()
    ses.step("Menu", *center(GAMEOVER_BTN(2)), kind="full")      # footer painted with 50 %
    storm = 0
    for i in range(40):
        sim.send("set battery %d" % (49 + (i % 3)))
        sim.sync()
        storm += len(sim.tick(1000))
    check(storm == 0, "menu: battery jitter 49..51 over 40 s caused %d repaints (expected 0)" % storm)
    sim.send("set battery 45")
    sim.sync()
    check(len(sim.tick(1000)) == 1, "menu: a 5-point drop repaints the footer")
    sim.send("set battery 40")
    sim.sync()
    check(len(sim.tick(1000)) == 0, "menu: another drop within 30 s waits")
    check(len(sim.tick(30000)) == 1, "menu: ... and is painted once the interval is over")
    sim.send("set usb 1")
    sim.sync()
    check(len(sim.tick(1000)) == 1, "menu: USB plugged in -> one repaint at once")
    sim.send("set usb 0")
    sim.sync()
    check(len(sim.tick(1000)) == 1, "menu: USB unplugged -> one repaint")
    check(len(sim.tick(1000)) == 0, "menu: nothing changed -> no repaint")

    # ---- settings + panel off ------------------------------------------------------------------
    ses.step("Settings", *center(MENU_BTN(MENU_SLOT_SETTINGS)), kind="full")
    ses.step("toggle refresh policy", *center(MENU_BTN(2)))
    ses.step("toggle it back", *center(MENU_BTN(2)))
    ses.step("Back", *center(MENU_BTN(5)), kind="full")
    panels = len(sim.panel)
    sim.tick(2900)
    check(len(sim.panel) == panels, "panel still on at 2.9 s")
    sim.tick(200)
    check(len(sim.panel) == panels + 1 and not sim.panel[-1]["on"], "panel off at 3.1 s")
    sim.tick(10000)
    check(len(sim.panel) == panels + 1, "panel off sent once")

    kinds = {k: sum(1 for f in sim.frames if f.kind == k) for k in ("partial", "full", "deep")}
    print("frames: %d total, taps: %d, frames from taps: %d, kinds: %s" % (len(sim.frames), ses.taps,
                                                                            ses.frames_from_taps, kinds))
    check(kinds["full"] == ses.screen_fulls + ses.upgrades,
          "refresh policy: %d Full frames = %d screen changes/flips + %d upgrades at >= 16 partials"
          % (kinds["full"], ses.screen_fulls, ses.upgrades))
    check(ses.upgrades >= 2, "the Full upgrade happened (%d times) during the long review and promotion runs" % ses.upgrades)
    sim.quit()

    # ---- real time: a tap during the refresh is dropped by the platform, and only once --------
    rt = Sim.__new__(Sim)
    rt.p = subprocess.Popen([SIM], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    rt.png_dir = None
    rt.events, rt.frames, rt.beeps, rt.panel = [], [], [], []
    rt.hello = rt._read_until("hello")
    rt.frames_at_boot = rt.sync()
    rt.send("set scale 1")
    rt.sync()
    x, y = center(MENU_BTN(MENU_SLOT_TWO_PLAYERS))
    rt.send("touch down %d %d" % (x, y))
    rt.send("touch up %d %d" % (x, y))
    # These arrive while the Full refresh (1.2 s) blocks: the platform must drop them.
    x2, y2 = center(MENU_BTN(CLOCK_SLOT_OFF))
    rt.send("touch down %d %d" % (x2, y2))
    rt.send("touch up %d %d" % (x2, y2))
    frames = rt.sync()
    ignored = [e for e in rt.events if e["ev"] == "touch_ignored"]
    check(len(frames) == 1 and frames[0].kind == "full", "real time: the first tap gave one Full frame")
    check(len(ignored) >= 1, "real time: the tap during the refresh was reported ignored")
    check(not is_board_frame(frames[0]), "real time: the ignored tap did not start a game")
    rt.send("tick")
    check(len(rt.sync()) == 0, "real time: no frame from the following tick")
    rt.quit()
    print("ALL %d CHECKS PASSED" % check.count)


if __name__ == "__main__":
    main()
