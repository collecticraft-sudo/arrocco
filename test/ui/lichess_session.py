#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Arrocco UI - scripted sessions for the Lichess screens (python3 stdlib only).

Runs sim/build/arrocco-sim --virtual-time --fake-lichess: the real Lichess screens against the
pretend Lichess of sim/host/fake_lichess.h. No network, no account, no token: nothing here can
reach lichess.org. Each scenario starts a fresh simulator and drives it like a person would,
then checks the frames (count, kind, what changed) and what the pretend Lichess saw ("lichess
state": every request, every stream, the moves on its side).

  1. link: no Wi-Fi -> the setup network's QR -> the login's QR -> linked -> the Lichess menu
  2. the computer: level, colour, clock; moves both ways; the clock; Menu and back; resign
  3. invitations: decline one, a bullet one refused, accept one, abort
  4. a friend: the keyboard, recent opponents (kept in flash), declined, cancelled, accepted,
     a draw offered both ways
  5. a game left running behind the menu: its end comes to the glass by itself, once
  6. errors: no Wi-Fi, a refused token (401), a 429 on a move, a dropped game stream, a slow
     network, a lost request, a POST that timed out but arrived: no move lost, none doubled

The QR codes on the glass are read back with the Mac's own detector (test/ui/qr_decode.m,
Core Image): they must say exactly what the phone needs.

    usage: test/ui/lichess_session.py [--png DIR]   (build first: sim/build.sh)

Exits non-zero on the first failed assertion.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_session import (Sim, check, center, square_rect, is_board_frame, box_present,  # noqa: E402
                         MENU_BTN, SIDE_BTN, PROMO_BTN, GAMEOVER_BOX, GAMEOVER_BTN, HEADLINE, SUBLINE, CLOCK_ROW)

HERE = os.path.dirname(os.path.abspath(__file__))
MENU_SLOT_LICHESS = 4
# The Lichess menu (lichess_screen.cpp).
HUB_PLAY, HUB_FRIEND, HUB_INVITES, HUB_RATED, HUB_UNLINK, HUB_BACK = range(6)
COMP_LEVEL, COMP_COLOUR, COMP_CLOCK, COMP_START, COMP_BACK = 0, 1, 2, 3, 5
FRIEND_NAME, FRIEND_CLOCK, FRIEND_COLOUR, FRIEND_RATED, FRIEND_SEND, FRIEND_BACK = range(6)
NAMES_TYPE, NAMES_BACK = 4, 5
SIDE_AGAIN, SIDE_BACK = 4, 5                       # code pages: "New code" / "Set up Wi-Fi", Back
WAIT_RETRY, WAIT_SETUP, WAIT_BACK = 3, 4, 5
# The online game's side buttons (lichess_game_screen.cpp).
G_END, G_DRAW, G_DRAW2, G_FLIP, G_MENU = 0, 1, 2, 3, 5
OVER_AGAIN, OVER_REVIEW, OVER_LICHESS = 0, 1, 2
# Dialogs: "Resign the game?" over the board, "Unlink the account?" centred on the screen.
BOARD_DIALOG_BTN = lambda i: (80 + i * 164, 256, 156, 48)
SCREEN_DIALOG_BTN = lambda i: (240 + i * 164, 256, 156, 48)
# Invitations: one row per challenge.
INVITE_ACCEPT = lambda row: (448, 96 + row * 70, 152, 48)
INVITE_DECLINE = lambda row: (616, 96 + row * 70, 152, 48)
# The keyboard (name_keyboard.h).
KEYS = ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_"]
KEY_DONE = (556, 400, 220, 48)
KEY_CLEAR = (290, 400, 220, 48)
KEY_CANCEL = (24, 400, 220, 48)
QR_BOX = (16, 16, 448, 448)
FOOTER = (16, 440, 768, 36)                         # the menu pages' one line of news


def key_center(ch):
    for row, keys in enumerate(KEYS):
        col = keys.find(ch)
        if col >= 0:
            return 24 + col * 76 + 34, 128 + row * 64 + 28
    raise KeyError(ch)


def sq(name, flipped=False):
    return center(square_rect(name, flipped))


# ---- reading the QR codes on the glass --------------------------------------------------------
QR_TOOL = os.path.join(HERE, "build", "qr_decode")


def build_qr_tool():
    """The Core Image reader. None when it cannot be built (not a Mac): the codes are then only
    checked for being there, not read."""
    source = os.path.join(HERE, "qr_decode.m")
    if os.path.exists(QR_TOOL) and os.path.getmtime(QR_TOOL) >= os.path.getmtime(source):
        return QR_TOOL
    os.makedirs(os.path.dirname(QR_TOOL), exist_ok=True)
    try:
        subprocess.run(["clang", "-fobjc-arc", "-Wall", "-Wextra", "-framework", "Foundation", "-framework",
                        "CoreImage", source, "-o", QR_TOOL], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print("  ..  qr_decode could not be built (%s): QR codes are not read back" % error)
        return None
    return QR_TOOL


def read_qr(frame):
    """The texts of the QR codes in `frame`, as a phone would read them; None without the tool."""
    if TOOL is None:
        return None
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "frame.png")
        frame.png(path)
        out = subprocess.run([TOOL, path], check=True, capture_output=True, text=True).stdout
    line = out.rstrip("\n")
    return [t for t in line.split("\t") if t] if line else []


TOOL = None


# ---- the simulator with the pretend Lichess -----------------------------------------------------
class Li:
    def __init__(self, name, png_dir, state_dir=None, auto=False):
        args = ["--fake-lichess"] + (["--state", state_dir] if state_dir else [])
        self.sim = Sim(png_dir, args=args)
        self.name = name
        self.png_dir = png_dir
        self.n = 0
        self.sim.send("set scale 0")
        if not auto:
            self.li("auto off")

    def li(self, line):
        self.sim.send("lichess " + line)

    def state(self):
        self.sim.send("lichess state")
        return self.sim._read_until("lichess")

    def save(self, frames, label):
        if self.png_dir:
            for fr in frames:
                self.n += 1
                fr.png(os.path.join(self.png_dir, "%s_%02d_%s.png" % (self.name, self.n, label)))
        return frames

    def tap(self, xy_or_rect, label, frames=1, kind=None):
        xy = center(xy_or_rect) if len(xy_or_rect) == 4 else xy_or_rect
        got = self.save(self.sim.tap(*xy), label)
        check(len(got) == frames, "%s: %d frame(s), expected %d" % (label, len(got), frames))
        if kind is not None and got:
            check(got[-1].kind == kind, "%s: a %s refresh (%s)" % (label, kind, got[-1].kind))
        return got[-1] if got else None

    def tick(self, ms, label, frames=None, kind=None):
        got = self.save(self.sim.tick(ms), label)
        if frames is not None:
            check(len(got) == frames, "%s: %d frame(s) from the tick, expected %d" % (label, len(got), frames))
        if kind is not None and got:
            check(got[-1].kind == kind, "%s: a %s refresh (%s)" % (label, kind, got[-1].kind))
        return got[-1] if got else None

    def page(self):
        st = self.state()
        return st["screen"], st["page"]

    def at(self, screen, page, label):
        got = self.page()
        check(got == (screen, page), "%s: on %s/%s (%s/%s)" % (label, screen, page, got[0], got[1]))

    def log_count(self, text):
        return sum(1 for line in self.state()["log"] if line == text)

    def quit(self):
        self.sim.quit()


def open_lichess(s, label="menu: Lichess"):
    return s.tap(MENU_BTN(MENU_SLOT_LICHESS), label, kind="full")


def linked_hub(s):
    """Linked and online: the menu's Lichess entry lands on the Lichess menu, connected."""
    s.li("wifi online")
    s.li("linked 1")
    open_lichess(s)
    s.at("lichess", "hub", "linked")
    s.tick(50, "hub: connected", frames=1, kind="partial")


def play_move(s, frm, to, label, flipped=False):
    """Select, then the target: two frames, the second the move shown as sent; one tick later the
    server's echo moves the piece (one frame)."""
    s.tap(sq(frm, flipped), label + ": select " + frm, kind="partial")
    sent = s.tap(sq(to, flipped), label + ": tap " + to, kind="partial")
    return sent


# ---- 1. link ----------------------------------------------------------------------------------------
def scenario_link(png):
    s = Li("link", png)
    s.li("wifi none")
    s.li("linked 0")
    menu = s.sim.frames_at_boot[-1]
    check(menu.ink(MENU_BTN(MENU_SLOT_LICHESS)) > 300, "menu: the Lichess entry is live")

    wifi = open_lichess(s, "menu: Lichess, no Wi-Fi stored")
    s.at("lichess", "wifi", "no network")
    check(s.state()["wifi"] == "portal" and s.state()["portal_opens"] == 1, "the setup portal was opened, once")
    check(wifi.ink(QR_BOX) > 20000, "a QR code where the board would be")
    texts = read_qr(wifi)
    if texts is not None:
        check(texts == ["WIFI:T:nopass;S:Arrocco-51A7;;"], "the code joins the setup network: %r" % texts)

    # Back closes the portal: an open access point nobody uses is a battery going flat.
    s.tap(SIDE_BTN(SIDE_BACK), "wifi: Back", kind="full")
    s.at("menu", "", "back on the menu")
    check(s.state()["wifi"] == "none", "the portal is closed")
    open_lichess(s, "menu: Lichess again")
    check(s.state()["portal_opens"] == 2, "it opens again")

    # The phone joins and saves the home network: the board joins it, then shows the login.
    s.li("wifi join")
    s.tick(10, "phone saved a network", frames=1, kind="full")
    s.at("lichess", "wifi-wait", "joining")
    link = s.tick(1600, "online", frames=1, kind="full")
    s.at("lichess", "link", "the login")
    check(s.state()["logins"] == 1 and s.state()["login"] == "waiting", "one login started")
    texts = read_qr(link)
    if texts is not None:
        check(texts == ["http://192.168.1.50/login"], "the code opens the board's login page: %r" % texts)
    check(s.tick(1000, "nothing happens", frames=0) is None, "waiting costs no refresh")

    # The phone refuses: the board says so and offers a new code.
    s.li("login refuse")
    refused = s.tick(10, "refused on Lichess", frames=1, kind="partial")
    check(refused.ink(SIDE_BTN(SIDE_AGAIN)) > 300, "a 'New code' button")
    check(refused.ink(QR_BOX) == 0, "and no stale code on the glass")
    s.tap(SIDE_BTN(SIDE_AGAIN), "link: New code", kind="full")
    check(s.state()["logins"] == 2 and s.state()["login"] == "waiting", "a second login, a new code")

    s.li("login approve")
    s.tick(10, "approved: linking", frames=1, kind="partial")
    s.tick(800, "linked", frames=1, kind="full")
    s.at("lichess", "hub", "the Lichess menu")
    hub = s.tick(10, "connected", frames=1, kind="partial")
    check(hub.ink(MENU_BTN(HUB_PLAY)) > 300, "Play the computer is live")
    st = s.state()
    check("GET /api/account -> 200" in st["log"], "the account was asked for")
    check("/api/stream/event" in st["streams"], "the event stream is open")

    # Unlink: asked first, then the board forgets the token and shows the login again.
    s.tap(MENU_BTN(HUB_UNLINK), "hub: Unlink", kind="partial")
    s.tap(SCREEN_DIALOG_BTN(1), "unlink: Cancel", kind="partial")
    check(s.state()["linked"] is True, "Cancel keeps the account")
    s.tap(MENU_BTN(HUB_UNLINK), "hub: Unlink again", kind="partial")
    s.tap(SCREEN_DIALOG_BTN(0), "unlink: Unlink", kind="full")
    check(s.state()["linked"] is False, "the token is gone")
    s.at("lichess", "link", "back to the login")
    check(s.state()["streams"] == [], "and every stream is closed")
    s.quit()


# ---- 2. the computer --------------------------------------------------------------------------------
def scenario_computer(png):
    s = Li("computer", png)
    linked_hub(s)
    s.tap(MENU_BTN(HUB_PLAY), "hub: Play the computer", kind="full")
    s.at("lichess", "computer", "the computer's page")
    s.tap(MENU_BTN(COMP_LEVEL), "level 4", kind="partial")
    s.tap(MENU_BTN(COMP_LEVEL), "level 5", kind="partial")
    s.tap(MENU_BTN(COMP_COLOUR), "you play Black", kind="partial")
    s.tap(MENU_BTN(COMP_COLOUR), "colour drawn by Lichess", kind="partial")
    s.tap(MENU_BTN(COMP_COLOUR), "you play White", kind="partial")
    s.tap(MENU_BTN(COMP_CLOCK), "clock 15 + 10", kind="partial")
    s.tap(MENU_BTN(COMP_CLOCK), "clock 30 + 0", kind="partial")
    s.tap(MENU_BTN(COMP_CLOCK), "clock 5 + 0", kind="partial")
    starting = s.tap(MENU_BTN(COMP_START), "Start the game", kind="partial")
    check(starting.ink(FOOTER) > 50, "'Starting the game...' under the page")
    game = s.tick(10, "the game", frames=1, kind="deep")
    s.at("lichess-game", "play", "in the game")
    check(is_board_frame(game), "a board on the glass")
    st = s.state()
    check(st["game"] == "fakeAi01" and st["us"] == "white", "an AI game, we are White")
    check(any("POST /api/challenge/ai" in line for line in st["log"]), "made with POST /api/challenge/ai")

    # A move: shown as sent (the piece does not move), then moved by the server's echo.
    before = game
    sent = play_move(s, "e2", "e4", "1. e4")
    check(sent.region(square_rect("e2")) != before.region(square_rect("e2")), "sent: e2 framed")
    check(sent.ink(square_rect("e4")) > before.ink(square_rect("e4")), "sent: a dot on e4, the pawn still on e2")
    check(sent.region(SUBLINE) != before.region(SUBLINE), "sent: 'Sending e4...'")
    moved = s.tick(10, "1. e4 confirmed", frames=1, kind="partial")
    check(moved.ink((square_rect("e2")[0] + 12, square_rect("e2")[1] + 12, 32, 32)) <
          sent.ink((square_rect("e2")[0] + 12, square_rect("e2")[1] + 12, 32, 32)), "the pawn left e2")
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 200") == 1, "e2e4 POSTed once")
    check(s.state()["moves"] == "e2e4", "and played once on Lichess")
    # The board takes no other move while it is the opponent's turn.
    s.tap(sq("d2"), "Black to move: d2 does nothing", frames=0)

    beeps = len(s.sim.beeps)
    s.li("opp e7e5")
    reply = s.tick(10, "1... e5 arrives", frames=1, kind="partial")
    check(len(s.sim.beeps) == beeps + 1, "the opponent's move beeps")
    check(reply.region(HEADLINE) == game.region(HEADLINE), "'Your move' again")
    play_move(s, "g1", "f3", "2. Nf3")
    s.tick(10, "2. Nf3 confirmed", frames=1, kind="partial")
    s.li("opp b8c6")
    two = s.tick(10, "2... Nc6 arrives", frames=1, kind="partial")
    check(s.state()["moves"] == "e2e4 e7e5 g1f3 b8c6", "four moves on both sides")

    # The server's clock counts down here, at the offline cadence: every 10 s.
    clock = s.tick(10000, "ten seconds later", frames=1, kind="partial")
    check(clock.region(CLOCK_ROW) != two.region(CLOCK_ROW), "the clock moved")
    check(s.tick(3000, "three more", frames=0) is None, "not every second above 20 s")

    # Menu: the game goes on behind the Lichess menu, and comes back.
    s.tap(SIDE_BTN(G_MENU), "game: Menu", kind="full")
    s.at("lichess", "hub", "the Lichess menu, the game still on")
    s.tap(MENU_BTN(HUB_PLAY), "hub: Back to the game", kind="full")
    s.at("lichess-game", "play", "the game again")

    # Promotion is not reachable this early; resign instead, asked first.
    s.tap(SIDE_BTN(G_END), "Resign", kind="partial")
    s.at("lichess-game", "confirm", "asked first")
    s.tap(BOARD_DIALOG_BTN(1), "resign: Cancel", kind="partial")
    check(s.state()["status"] == "started", "Cancel resigns nothing")
    s.tap(SIDE_BTN(G_END), "Resign again", kind="partial")
    s.tap(BOARD_DIALOG_BTN(0), "resign: Resign", kind="partial")
    over = s.tick(10, "the end", frames=1, kind="deep")
    s.at("lichess-game", "over", "game over")
    check(box_present(over, GAMEOVER_BOX), "the result over the board")
    check(s.state()["status"] == "resign" and s.state()["winner"] == "black", "Lichess: resigned, Black won")

    s.tap(GAMEOVER_BTN(OVER_REVIEW), "over: Review", kind="partial")
    s.tap(SIDE_BTN(0), "review: <", kind="partial")
    s.tap(SIDE_BTN(5), "review: Lichess", kind="full")
    s.at("lichess", "hub", "the Lichess menu after the game")

    # Back to the main menu: the client goes, the streams close.
    s.tap(MENU_BTN(HUB_BACK), "hub: Back", kind="full")
    s.at("menu", "", "the main menu")
    check(s.state()["streams"] == [], "no stream left open")
    s.quit()


# ---- 3. invitations ---------------------------------------------------------------------------------
def scenario_invitations(png):
    s = Li("invites", png)
    linked_hub(s)
    s.li("invite inv00002 amico 300 3 1")
    s.li("invite inv00003 bullet_fan 60 0 0")
    s.li("invite inv00004 rapid_rita 900 10 0")
    s.tick(10, "three invitations arrive", frames=1, kind="partial")
    page = s.tap(MENU_BTN(HUB_INVITES), "hub: Invitations", kind="full")
    s.at("lichess", "invitations", "the invitations")
    for row in range(3):
        check(page.ink(INVITE_DECLINE(row)) > 300, "row %d: a Decline button" % row)
    # A live button has a solid frame, a greyed one a dotted frame: compare the top edges.
    edge = lambda row: (INVITE_ACCEPT(row)[0] + 10, INVITE_ACCEPT(row)[1], INVITE_ACCEPT(row)[2] - 20, 1)
    check(page.ink(edge(0)) > 120 and page.ink(edge(1)) < 60, "the bullet one cannot be accepted (%d, %d)" % (
        page.ink(edge(0)), page.ink(edge(1))))
    s.tap(INVITE_ACCEPT(1), "accept the bullet one: nothing", frames=0)

    s.tap(INVITE_DECLINE(0), "decline amico", kind="partial")
    check(s.log_count("POST /api/challenge/inv00002/decline -> 200") == 1, "declined on Lichess")
    check("inv00002" not in s.state()["incoming"], "gone from Lichess")
    s.li("withdraw inv00003")
    s.tick(10, "bullet_fan withdraws", frames=1, kind="partial")
    rows = s.tick(10, "nothing else", frames=0)

    # rapid_rita is now the first row: accept, and the game starts with us as Black.
    starting = s.tap(INVITE_ACCEPT(0), "accept rapid_rita", kind="partial")
    check(starting.ink(FOOTER) > 50, "'Starting the game...'")
    check(s.log_count("POST /api/challenge/inv00004/accept -> 200") == 1, "accepted on Lichess")
    game = s.tick(10, "the game", frames=1, kind="deep")
    s.at("lichess-game", "play", "playing rapid_rita")
    check(s.state()["us"] == "black", "we have Black")
    # Black at the bottom: a8 is drawn bottom-right.
    check(game.ink(square_rect("e8", flipped=True)) > 300 and game.ink(square_rect("e1", flipped=True)) > 300,
          "the board is turned for Black")
    s.tap(sq("e7", True), "White to move: our pawn does nothing", frames=0)
    s.li("opp d2d4")
    s.tick(10, "1. d4 arrives", frames=1, kind="partial")
    # One move each is not played yet: the game can still be aborted.
    s.tap(SIDE_BTN(G_END), "Abort", kind="partial")
    s.tap(BOARD_DIALOG_BTN(0), "abort: Abort", kind="partial")
    s.tick(10, "aborted", frames=1, kind="deep")
    check(s.state()["status"] == "aborted", "aborted on Lichess")
    s.tap(GAMEOVER_BTN(OVER_LICHESS), "over: Lichess", kind="full")
    s.at("lichess", "hub", "the Lichess menu")
    # Left alone on the Lichess menu with no game on: back to the main menu by itself, the
    # session ended, so that the radio (and then the board) can go to sleep.
    check(s.tick(4 * 60 * 1000, "four idle minutes", frames=0) is None, "four minutes: still there")
    s.tick(60 * 1000 + 100, "five idle minutes", frames=1, kind="full")
    s.at("menu", "", "back on the main menu by itself")
    check(s.state()["streams"] == [], "and every stream closed")
    s.quit()


# ---- 4. a friend ------------------------------------------------------------------------------------------
def type_name(s, name):
    for ch in name:
        s.tap(key_center(ch), "key " + ch, kind="partial")


def scenario_friend(png, state_dir):
    s = Li("friend", png, state_dir=state_dir)
    linked_hub(s)
    s.tap(MENU_BTN(HUB_FRIEND), "hub: Challenge a friend", kind="full")
    friend = s.tap(MENU_BTN(FRIEND_SEND), "Send with no name: nothing", frames=0)
    s.tap(MENU_BTN(FRIEND_NAME), "Choose a friend", kind="full")
    s.at("lichess", "names", "the recent opponents: none yet")
    s.tap(MENU_BTN(NAMES_TYPE), "Type a name", kind="full")
    s.at("lichess", "keyboard", "the keyboard")
    type_name(s, "a")
    s.tap(KEY_DONE, "Done with one letter", kind="partial")
    s.at("lichess", "keyboard", "a Lichess name has two letters at least")
    type_name(s, "mic")
    s.tap(key_center("o"), "key o", kind="partial")
    s.tap(KEY_CLEAR, "Clear", kind="partial")
    type_name(s, "amico")
    s.tap(KEY_DONE, "Done", kind="full")
    s.at("lichess", "friend", "back on the friend's page")
    s.tap(MENU_BTN(FRIEND_RATED), "rated", kind="partial")

    s.tap(MENU_BTN(FRIEND_SEND), "Send the challenge", kind="full")
    s.at("lichess", "waiting", "waiting for amico")
    st = s.state()
    check(st["outgoing"] != "" and "POST-STREAM /api/challenge/amico" in st["log"], "a kept-alive challenge to amico")
    check(s.tick(30000, "half a minute, nothing", frames=0) is None, "a kept-alive challenge does not lapse")
    s.li("friend decline")
    s.tick(10, "declined", frames=1, kind="full")
    s.at("lichess", "friend", "declined: back to the friend's page")

    s.tap(MENU_BTN(FRIEND_SEND), "Send again", kind="full")
    outgoing = s.state()["outgoing"]
    s.tap(MENU_BTN(5), "Cancel the challenge", kind="full")
    check(s.log_count("POST /api/challenge/%s/cancel -> 200" % outgoing) == 1, "cancelled on Lichess")
    check(s.state()["outgoing"] == "", "nothing left out there")

    s.tap(MENU_BTN(FRIEND_SEND), "Send a third time", kind="full")
    s.li("friend accept")
    s.tick(10, "accepted: the game", frames=1, kind="deep")
    s.at("lichess-game", "play", "playing amico")

    # Draws: the friend offers, we decline; we offer, the friend accepts.
    play_move(s, "e2", "e4", "1. e4")
    s.tick(10, "1. e4 confirmed", frames=1)
    s.li("opp e7e5")
    s.tick(10, "1... e5", frames=1)
    s.li("draw offer")
    offered = s.tick(10, "a draw offered", frames=1, kind="partial")
    check(offered.ink(SIDE_BTN(G_DRAW)) > 300 and offered.ink(SIDE_BTN(G_DRAW2)) > 300, "Accept and Decline draw")
    s.tap(SIDE_BTN(G_DRAW2), "Decline draw", kind="partial")
    s.tick(10, "declined", frames=1)
    check(s.log_count("POST /api/board/game/%s/draw/no -> 200" % s.state()["game"]) == 1, "draw/no")
    s.tap(SIDE_BTN(G_DRAW), "Offer draw", kind="partial")
    s.tick(10, "offered", frames=1)
    s.li("draw accept")
    s.tick(10, "a draw", frames=1, kind="deep")
    check(s.state()["status"] == "draw", "drawn on Lichess")
    s.tap(GAMEOVER_BTN(OVER_LICHESS), "over: Lichess", kind="full")
    s.quit()

    # The friend is remembered, in flash: the next board start offers the name.
    s = Li("friend2", png, state_dir=state_dir)
    linked_hub(s)
    s.tap(MENU_BTN(HUB_FRIEND), "hub: Challenge a friend", kind="full")
    names = s.tap(MENU_BTN(FRIEND_NAME), "Choose a friend", kind="full")
    check(names.ink(MENU_BTN(0)) > 300, "amico is on the list after a restart")
    s.tap(MENU_BTN(0), "pick amico", kind="full")
    picked = s.tap(MENU_BTN(FRIEND_SEND), "Send", kind="full")
    check("POST-STREAM /api/challenge/amico" in s.state()["log"], "challenged by name from the list")
    s.tap(MENU_BTN(5), "Cancel", kind="full")
    s.quit()


# ---- 5, 6. a game behind the menu, and errors --------------------------------------------------------------------
def start_ai_game(s):
    linked_hub(s)
    s.tap(MENU_BTN(HUB_PLAY), "hub: Play the computer", kind="full")
    s.tap(MENU_BTN(COMP_START), "Start the game", kind="partial")
    return s.tick(10, "the game", frames=1, kind="deep")


def scenario_background(png):
    """A game left running behind the Lichess menu ends there: its result comes to the glass by
    itself, once; and a game started from the phone takes the screen by itself."""
    s = Li("background", png)
    start_ai_game(s)
    play_move(s, "e2", "e4", "1. e4")
    s.tick(10, "1. e4 confirmed", frames=1)
    s.tap(SIDE_BTN(G_MENU), "game: Menu", kind="full")
    s.at("lichess", "hub", "the game goes on behind the menu")
    s.li("end resign white")                 # Black (Stockfish here) resigns, we win
    s.tick(10, "it ends behind the menu", frames=1, kind="deep")
    s.at("lichess-game", "over", "the result comes to the glass")
    s.tap(GAMEOVER_BTN(OVER_LICHESS), "over: Lichess", kind="full")
    s.at("lichess", "hub", "back on the menu")
    check(s.tick(10, "and it stays there", frames=0) is None, "the result is shown once, not again")
    s.quit()


def scenario_errors(png):
    # No Wi-Fi: a stored network that refuses the password.
    s = Li("nowifi", png)
    s.li("wifi failed")
    s.li("linked 1")
    page = open_lichess(s, "menu: Lichess, Wi-Fi failing")
    s.at("lichess", "wifi-wait", "the Wi-Fi problem first")
    check(page.ink(MENU_BTN(WAIT_RETRY)) > 300 and page.ink(MENU_BTN(WAIT_SETUP)) > 300, "Try again, Set up Wi-Fi")
    s.tap(MENU_BTN(WAIT_SETUP), "Set up Wi-Fi", kind="full")
    s.at("lichess", "wifi", "the setup portal")
    check(s.state()["wifi"] == "portal", "the portal is open")
    s.tap(SIDE_BTN(SIDE_BACK), "Back", kind="full")
    check(s.state()["wifi"] != "portal", "and closed again on the way out")
    s.quit()

    # A token Lichess no longer knows: 401, and "Link again".
    s = Li("401", png)
    s.li("wifi online")
    s.li("linked 1")
    s.li("token bad")
    open_lichess(s)
    refused = s.tick(10, "401", frames=1, kind="partial")
    check(refused.ink(MENU_BTN(HUB_PLAY)) > 300, "'Link again' instead of the games")
    s.tap(MENU_BTN(HUB_PLAY), "Link again", kind="full")
    s.at("lichess", "link", "the login, to link again")
    check(s.state()["linked"] is False, "the bad token was dropped")
    s.quit()

    # 429 on a move: Lichess wants a minute. The move waits, then goes out by itself, once.
    s = Li("429", png)
    start_ai_game(s)
    s.li("next 429")
    play_move(s, "e2", "e4", "e4 refused with 429")
    wait = s.tick(10, "rate limited", frames=1, kind="partial")
    s.at("lichess-game", "play", "still playing")
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 429") == 1, "the 429")
    check(s.state()["moves"] == "", "nothing played yet")
    s.tick(30000, "half the minute", frames=None)
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 200") == 0, "no retry inside the minute")
    s.tick(31500, "the minute is over", frames=None)
    s.tick(10, "the echo", frames=None)
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 200") == 1, "then sent again, once")
    check(s.state()["moves"] == "e2e4", "and played once")
    s.quit()

    # A game stream that drops: "Reconnecting..." after 3 s, the board catches up, play goes on.
    s = Li("drop", png)
    start_ai_game(s)
    play_move(s, "e2", "e4", "1. e4")
    s.tick(10, "1. e4 confirmed", frames=1)
    s.li("drop game")
    s.li("opp e7e5")             # played on Lichess while the board cannot hear it
    quiet = s.tick(10, "the stream broke", frames=None)
    s.tick(1000, "a second", frames=None)
    note = s.tick(2500, "three seconds without it", frames=1, kind="partial")
    check(note.region(SUBLINE) != quiet.region(SUBLINE) if quiet else True, "'Reconnecting...'")
    back = s.tick(2000, "reopened: the gameFull", frames=None)
    check(s.state()["streams"].count("/api/board/game/stream/fakeAi01") == 1, "one game stream, reopened")
    s.tick(10, "settled", frames=None)
    play_move(s, "g1", "f3", "2. Nf3 after the reconnect")
    s.tick(10, "2. Nf3 confirmed", frames=1)
    check(s.state()["moves"] == "e2e4 e7e5 g1f3", "the missed move arrived, and play went on")
    s.quit()

    # A slow network: the echo comes before the answer to the POST; nothing is sent twice.
    s = Li("slow", png)
    start_ai_game(s)
    s.li("delay 5000")
    play_move(s, "e2", "e4", "a slow e4")
    s.tick(10, "the echo is quicker than the answer", frames=1, kind="partial")
    check(s.state()["moves"] == "e2e4", "on the board already")
    s.tick(5000, "the answer, at last", frames=None)
    s.tick(3000, "and later", frames=None)
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 200") == 1, "one POST, not two")
    # The POST outlasts the client's 20 s: given up on, but it did arrive. Not sent again.
    s.li("delay 25000")
    s.li("opp e7e5")
    s.tick(10, "1... e5", frames=None)
    play_move(s, "d2", "d4", "a very slow d4")
    for i in range(6):
        s.tick(5000, "waiting %d" % i, frames=None)
    check(s.log_count("POST /api/board/game/fakeAi01/move/d2d4 -> 200") == 1, "d2d4 POSTed once, never again")
    check(s.state()["moves"] == "e2e4 e7e5 d2d4", "and on the board once")
    s.li("delay 0")
    s.quit()

    # The network is gone: the move is kept and sent when it comes back, once.
    s = Li("offline", png)
    start_ai_game(s)
    s.li("offline 1")
    play_move(s, "e2", "e4", "e4 with no network")
    for i in range(3):
        s.tick(2100, "retry %d" % i, frames=None)
    lost = s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> lost")
    check(lost >= 2, "tried again every couple of seconds (%d)" % lost)
    check(s.state()["moves"] == "", "nothing played while offline")
    s.li("offline 0")
    s.tick(2100, "the network is back", frames=None)
    s.tick(10, "the echo", frames=None)
    check(s.log_count("POST /api/board/game/fakeAi01/move/e2e4 -> 200") == 1, "sent once when it came back")
    check(s.state()["moves"] == "e2e4", "and played once")
    s.quit()


def main():
    global TOOL
    png_dir = None
    if "--png" in sys.argv:
        png_dir = sys.argv[sys.argv.index("--png") + 1]
        os.makedirs(png_dir, exist_ok=True)
    TOOL = build_qr_tool()
    state_dir = tempfile.mkdtemp(prefix="arrocco-lichess-")
    try:
        scenario_link(png_dir)
        scenario_computer(png_dir)
        scenario_invitations(png_dir)
        scenario_friend(png_dir, state_dir)
        scenario_background(png_dir)
        scenario_errors(png_dir)
    finally:
        shutil.rmtree(state_dir, ignore_errors=True)
    print("ALL %d CHECKS PASSED" % check.count)


if __name__ == "__main__":
    main()
