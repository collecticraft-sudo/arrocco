#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - scripted session for "Play vs engine" (python3 stdlib only).

Plays a whole game against the weakest level through the touch protocol and checks
what the engine changes about the screen: that "Thinking..." appears while it works,
that its move costs exactly ONE frame, that the board stays usable while it thinks,
and that Undo / New game / Menu really abort the search.

The script knows nothing about chess. It finds the human's moves the way a person
does: tap a piece, look at the dots the board draws on its legal targets, tap one.
Only quiet moves are used (a target dot on an empty square), which is always enough
to have a move to make.

The simulator runs on virtual time, but the engine runs on the real clock - it is a
real search. So waiting for its answer means ticking until a frame comes out, exactly
as the firmware's loop() does.

    usage: test/ui/engine_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_session import (Sim, check, center, square_rect, square_center_box, is_board_frame,  # noqa: E402
                         box_present, MENU_BTN, SIDE_BTN, PROMO_BTN, PROMO_BOX, GAMEOVER_BOX,
                         HEADLINE, SUBLINE, BTN_NEW, BTN_UNDO, BTN_RESIGN, BTN_MENU)

# Menu and engine-setup slots, mirrored from ui/menu_screen.cpp.
MENU_SLOT_RESUME, MENU_SLOT_ENGINE = 0, 2
ENGINE_SLOT_SIDE, ENGINE_SLOT_LEVEL, ENGINE_SLOT_START, ENGINE_SLOT_BACK = 0, 1, 2, 5
CLOCK_SLOT_OFF = 0
CONFIRM_BTN = lambda i: (80, 120 + 56 + i * 56, 320, 48)   # resign / draw / cancel

SQUARES = [f + r for r in "12345678" for f in "abcdefgh"]
# Where a human's pieces are at the start, then the rest of the board.
SCAN_ORDER = [f + r for r in "2134" for f in "edcbafgh"] + SQUARES


class Rng:
    """A fixed-seed LCG, so the human's moves vary from ply to ply but the whole
    session replays move for move. Without it the player shuffles one rook back and
    forth for a hundred plies and the game proves nothing."""

    def __init__(self, seed=20260922):
        self.state = seed

    def below(self, n):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state % n if n > 0 else 0

MAX_PLIES = 100          # then the player resigns: an ending is an ending
ENGINE_TIMEOUT_S = 10.0  # level 1 thinks for 400 ms; this is the "it hung" limit


def centre(name):
    """The 14x14 box at the middle of a square: a target dot lands in it, a corner
    triangle or a check ring never does."""
    return square_center_box(name, 14)


def wait_for_engine(sim, timeout=ENGINE_TIMEOUT_S):
    """Ticks until the engine's move reaches the panel. Returns the frames of the ONE
    tick that produced something, plus how many ticks went by with nothing."""
    start = time.time()
    quiet = 0
    while time.time() - start < timeout:
        frames = sim.tick(50)
        if frames:
            return frames, quiet
        quiet += 1
        time.sleep(0.01)
    return [], quiet


def quiet_ticks(sim, count=20, sleep=0.03):
    """Ticks `count` times with nothing expected, and returns everything that came out."""
    seen = []
    for _ in range(count):
        seen += sim.tick(50)
        time.sleep(sleep)
    return seen


def select_and_targets(sim, square, before):
    """Taps `square`. Returns (frame, quiet targets) or (None, []) if nothing happened."""
    frames = sim.tap(*center(square_rect(square)))
    if not frames:
        return None, []
    selected = frames[-1]
    targets = [t for t in SQUARES
               if t != square and before.ink(centre(t)) == 0 and selected.ink(centre(t)) > 0]
    return selected, targets


def play_human_move(sim, ply, rng):
    """One move for the human side. Returns (from, to, frames of the move)."""
    start = rng.below(len(SCAN_ORDER))
    for index in range(len(SCAN_ORDER)):
        square = SCAN_ORDER[(start + index) % len(SCAN_ORDER)]
        before = sim.last()
        selected, targets = select_and_targets(sim, square, before)
        if selected is None:
            continue                      # not a piece of the side to move
        if not targets:
            sim.tap(*center(square_rect(square)))   # put it back down
            continue
        target = targets[rng.below(len(targets))]
        frames = sim.tap(*center(square_rect(target)))
        check(len(frames) == 1, "ply %d: %s%s is one frame" % (ply, square, target))
        if frames and box_present(frames[-1], PROMO_BOX):
            frames = sim.tap(*center(PROMO_BTN(0)))   # always a queen
            check(len(frames) == 1, "ply %d: promoting to a queen is one frame" % ply)
        return square, target, frames
    return None, None, []


def open_engine_game(sim, level_taps=0, side_taps=0):
    """Menu -> Play vs engine -> (colour, level) -> clock picker -> the board."""
    setup = sim.tap(*center(MENU_BTN(MENU_SLOT_ENGINE)))
    check(len(setup) == 1 and setup[0].kind == "full",
          "menu: 'Play vs engine' opens the setup screen with one Full frame")
    for _ in range(side_taps):
        check(len(sim.tap(*center(MENU_BTN(ENGINE_SLOT_SIDE)))) == 1,
              "setup: the colour line repaints once per tap")
    for _ in range(level_taps):
        check(len(sim.tap(*center(MENU_BTN(ENGINE_SLOT_LEVEL)))) == 1,
              "setup: the level line repaints once per tap")
    picker = sim.tap(*center(MENU_BTN(ENGINE_SLOT_START)))
    check(len(picker) == 1 and picker[0].kind == "full",
          "setup: 'Choose the clock and start' goes to the clock picker")
    board = sim.tap(*center(MENU_BTN(CLOCK_SLOT_OFF)))
    check(len(board) == 1 and board[0].kind == "deep", "clock picker: 'No clock' starts the game")
    check(is_board_frame(board[0]), "the board is on the panel")
    return board[0]


def main():
    png_dir = None
    if len(sys.argv) == 3 and sys.argv[1] == "--png":
        png_dir = sys.argv[2]
        os.makedirs(png_dir, exist_ok=True)
    sim = Sim(png_dir)
    rng = Rng()

    # ---- 1. set up a game: human White, level 1 (the setup screen opens on both) ----------
    board = open_engine_game(sim)
    sim.save(board, "e01_engine_board")
    to_move_headline = board.region(HEADLINE)
    check(board.ink(HEADLINE) > 0, "side panel: the headline says whose move it is")

    # ---- 2. the first human move starts the engine, in the SAME frame --------------------
    src, dst, frames = play_human_move(sim, 1, rng)
    check(src is not None, "the board offered the human a legal quiet move (%s%s)" % (src, dst))
    thinking = frames[-1]
    sim.save(thinking, "e02_thinking")
    thinking_headline = thinking.region(HEADLINE)
    check(thinking_headline != to_move_headline,
          "'Thinking...' replaced the to-move headline in the same frame as the move")
    check(thinking.ink(HEADLINE) > 0, "'Thinking...' has ink")
    check(thinking.ink(SUBLINE) > 0, "the level is named under it")
    subline_while_thinking = thinking.region(SUBLINE)

    # ---- 3. the answer costs exactly one frame -------------------------------------------
    answer, quiet = wait_for_engine(sim)
    check(len(answer) == 1, "the engine's move is exactly one frame (after %d empty ticks)" % quiet)
    check(quiet > 0, "the board went on ticking while it thought (%d ticks with no frame)" % quiet)
    check(answer[0].region(HEADLINE) == to_move_headline,
          "'Thinking...' is gone once the move is played")
    sim.save(answer[0], "e03_engine_answered")

    # ---- 4. Menu while it thinks: abort, and nothing repaints over the menu ---------------
    src, dst, frames = play_human_move(sim, 3, rng)
    check(frames[-1].region(HEADLINE) == thinking_headline, "it is thinking again after %s%s" % (src, dst))
    menu = sim.tap(*center(SIDE_BTN(BTN_MENU)))
    check(len(menu) == 1 and menu[0].kind == "full", "Menu while thinking: one Full frame")
    check(not is_board_frame(menu[0]), "Menu while thinking: the menu is on the panel")
    late = quiet_ticks(sim, 25)
    check(not late, "Menu while thinking: the aborted search never repaints over the menu")

    # ---- 5. Resume asks it again ----------------------------------------------------------
    resumed = sim.tap(*center(MENU_BTN(MENU_SLOT_RESUME)))
    check(len(resumed) == 1, "Resume: one frame")
    check(resumed[0].region(HEADLINE) == thinking_headline,
          "Resume: the engine is asked again and the panel says so")
    answer, quiet = wait_for_engine(sim)
    check(len(answer) == 1, "Resume: the move still arrives in one frame")

    # ---- 6. Undo while it thinks: back to the player, and no move arrives later ------------
    src, dst, frames = play_human_move(sim, 5, rng)
    check(frames[-1].region(HEADLINE) == thinking_headline, "thinking after %s%s" % (src, dst))
    undone = sim.tap(*center(SIDE_BTN(BTN_UNDO)))
    check(len(undone) == 1, "Undo while thinking: one frame")
    check(undone[0].region(HEADLINE) == to_move_headline,
          "Undo while thinking: it is the player's move again")
    check(undone[0].region(SUBLINE) != subline_while_thinking,
          "Undo while thinking: the level line is gone")
    late = quiet_ticks(sim, 25)
    check(not late, "Undo while thinking: the aborted search never plays its move")

    # ---- 7. New game while it thinks -------------------------------------------------------
    src, dst, frames = play_human_move(sim, 5, rng)
    check(frames[-1].region(HEADLINE) == thinking_headline, "thinking after %s%s" % (src, dst))
    fresh = sim.tap(*center(SIDE_BTN(BTN_NEW)))
    check(len(fresh) == 1 and fresh[0].kind == "deep", "New game while thinking: one Deep frame")
    check(fresh[0].region(HEADLINE) == to_move_headline,
          "New game while thinking: White to move on a fresh board")
    late = quiet_ticks(sim, 25)
    check(not late, "New game while thinking: the aborted search never plays its move")

    # ---- 8. a whole game, one frame per move, to a legal end --------------------------------
    plies = 0
    engine_moves = 0
    over = None
    while plies < MAX_PLIES:
        src, dst, frames = play_human_move(sim, plies + 1, rng)
        if src is None:
            # The tapper reads its moves off the frame and knows no chess, so a position
            # can defeat it. That is allowed, but not before the game has been a game:
            # without this floor a two-ply run would pass as loudly as a hundred-ply one.
            check(plies >= 20,
                  "the tapper found no move at ply %d, after a game of real length" % plies)
            break
        plies += 1
        if frames and box_present(frames[-1], GAMEOVER_BOX):
            over = ("the player's move", frames[-1])
            break
        check(frames[-1].region(HEADLINE) == thinking_headline or plies >= MAX_PLIES,
              "ply %d: the engine was asked (Thinking...)" % plies)
        answer, quiet = wait_for_engine(sim)
        if len(answer) != 1:
            check(False, "ply %d: the engine answered with %d frames" % (plies, len(answer)))
            break
        engine_moves += 1
        plies += 1
        if box_present(answer[0], GAMEOVER_BOX):
            over = ("the engine's move", answer[0])
            break
    check(engine_moves > 0, "the game ran: %d plies, %d of them the engine's" % (plies, engine_moves))

    if over is None:
        # Nobody was mated inside the budget: end it the other legal way.
        dialog = sim.tap(*center(SIDE_BTN(BTN_RESIGN)))
        check(len(dialog) == 1, "resign: the dialog opens with one frame")
        ended = sim.tap(*center(CONFIRM_BTN(0)))
        check(len(ended) == 1 and ended[0].kind == "deep", "resign: the game ends with one Deep frame")
        over = ("resignation after %d plies" % plies, ended[0])
    check(box_present(over[1], GAMEOVER_BOX), "the game reached a legal end (%s)" % over[0])
    sim.save(over[1], "e04_game_over")
    print("   game over by %s (%d plies, %d engine moves)" % (over[0], plies, engine_moves))

    sim.quit()
    print("ALL %d CHECKS PASSED" % check.count)


if __name__ == "__main__":
    main()
