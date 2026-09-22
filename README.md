# Arrocco — e-ink chess

An open e-ink touch chessboard by CollectiCraft: a 7.5" e-paper panel with a touch layer, an ESP32-S3 and a battery, in a thin 3D-printed case that lies flat on the table. Play the built-in engine, play a friend on the same board, solve puzzles offline, or play on Lichess over Wi-Fi.

Arrocco is an open alternative to commercial e-ink chessboards. It is not affiliated with any commercial e-ink chessboard.

## Status: pre-hardware

The parts are ordered. **Nothing has been tested on a device yet.**

What works today, on a Mac:

- The chess rules library passes its perft tests (move generation checked against known node counts).
- The Mac simulator runs the portable code and shows the 800 × 480 screen pixel for pixel in a browser.

Everything about the hardware below comes from datasheets, schematics and library sources. The first job when the parts arrive is the hardware-validation firmware.

## Hardware

| Part | Notes |
|---|---|
| Good Display GDEY075T7-T01 | 7.5" e-paper, 800 × 480, UC8179 controller, GT911 touch |
| Seeed ePaper Driver Board for XIAO (114993558) | display connector, LiPo charger, power switch |
| Seeed XIAO ESP32-S3 Plus, pre-soldered headers | 16 MB flash, 8 MB PSRAM |
| LiPo 606090, 4000 mAh, JST PH 2.0 | check polarity with a multimeter before plugging in |
| MAX17048 fuel gauge breakout | battery percentage, on the touch I2C bus |
| KY-006 passive buzzer | optional |
| 6-pin 0.5 mm FPC breakout + two 4.7 kΩ resistors | touch connection inside the case |
| Good Display ESP32-FTS02 | bench only, as a touch adapter; too tall for the case |
| Two 1 × 7 pin headers, 2.54 mm | soldered into the driver board's CN1/CN2 holes |

Through-hole soldering only. Wiring and pin map: [docs/wiring.md](docs/wiring.md). Shopping list: [docs/da-comprare.md](docs/da-comprare.md). Bring-up procedure: [docs/collaudo.md](docs/collaudo.md).

## Repository layout

| Path | What it is |
|---|---|
| `lib/arrocco/` | Portable core: chess rules and user interface. The same code runs on the device and on a Mac. |
| `src/hwtest/` | Hardware-validation firmware. It checks the display, the touch and the wiring by itself and reports on the screen. |
| `src/app/` | The real firmware. Comes later. |
| `sim/` | Mac simulator. Run `sim/run.sh`. |
| `test/chess/` | Native tests for the rules library. Run `make -C test/chess test`. |
| `assets/pieces/` | SVG sources for the CollectiCraft 1-bit piece set, with a template. |
| `docs/` | Working notes, mostly in Italian. [docs/decisioni.md](docs/decisioni.md) is the source of truth for project decisions. |

## Build and run

The firmware builds with [PlatformIO](https://platformio.org/):

```sh
pio run -e hwtest             # build the hardware-validation firmware
pio run -e hwtest -t upload   # flash it over USB-C
pio device monitor            # read its log
```

The tests need only a C++17 compiler. The simulator also needs Python 3 (no packages) and the Adafruit GFX sources that PlatformIO downloads during the first firmware build, so build the firmware once before running it:

```sh
make -C test/chess test     # rules library tests (perft)
sim/run.sh                  # build, start the local server, open the page
```

## Roadmap

1. **Hardware validation.** The `hwtest` firmware finds wiring faults by itself and names them on the screen.
2. **Rules library.** Done; everything else builds on it.
3. **Interface and two players** on the same board, with a clock. One screen refresh per event, few full-screen flashes.
4. **Engine.** CT800 in its own task, speaking UCI. Named levels, from easy ones below 1000 Elo up to club strength.
5. **Offline puzzles** from the Lichess puzzle database, stored in flash.
6. **Lichess.** Login by QR code (OAuth PKCE), pasted token as a fallback. Play Lichess Stockfish, challenge a friend, accept invites. Casual games by default, rated on request.
7. **Later:** take back, hints.
8. **Release.** Tagged versions built by CI, with a browser flasher on GitHub Pages. No secure boot and no flash encryption: you can always install your own build.

The CollectiCraft piece set is drawn by hand ([guide, in Italian](docs/pezzi.md)); until it is ready the firmware draws a placeholder set. The interface is English only.

## The case

The 3D-printed case is published separately on MakerWorld and is not in this repository. This repository holds the firmware, the wiring and the measurements.

## Licence

GPL-3.0-or-later, see [COPYING](COPYING). The display library, GxEPD2, is GPL-3.0, so the firmware already has to be; the planned chess engine is GPL-3.0-or-later as well. Third-party components are listed in [THIRD-PARTY.md](THIRD-PARTY.md).
