# Third-party components

Arrocco itself is GPL-3.0-or-later (see [COPYING](COPYING)). This file lists what it builds on. None of this code is copied into the repository: PlatformIO fetches the libraries and the framework at build time, and their licence texts ship with them.

## Used by the firmware today

| Component | Version checked | Licence | Role |
|---|---|---|---|
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) by Jean-Marc Zingg | 1.6.9 | GPL-3.0 | e-paper display driver |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | 1.12.6 | BSD 2-clause | drawing primitives and bitmap fonts |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | MIT | pulled in by Adafruit GFX; not used directly |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | 3.3.8 | LGPL-2.1 | Arduino framework |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.4 | Apache-2.0 | SDK under the Arduino core, linked as prebuilt libraries |

How each licence was checked:

- GxEPD2, Adafruit GFX and Adafruit BusIO: from the `LICENSE` / `license.txt` files PlatformIO installs under `.pio/libdeps/`.
- Arduino core and ESP-IDF: from their upstream repositories. The PlatformIO framework package ships without a top-level licence file. ESP-IDF also bundles components under other permissive licences (FreeRTOS, mbedTLS, lwIP and others); see its own notices.

GxEPD2 is the reason the firmware is GPL-3.0 already. All the other licences above are compatible with it.

**Fonts.** The interface uses the `FreeSans` bitmap fonts from the `Fonts/` folder of Adafruit GFX. They are generated from GNU FreeFont, which is GPL-3.0-or-later with a font exception. The Adafruit repository does not state this origin itself.

**Simulator.** `sim/` compiles the same Adafruit GFX sources for the Mac. Its server uses only the Python standard library and its web page loads no external scripts.

## Included in the repository

| Component | Version | Licence | Role |
|---|---|---|---|
| [CT800](https://www.ct800.net/) chess engine by Rasmus Althoff | V1.46 | GPL-3.0-or-later | offline opponent; imported unmodified in `lib/ct800/upstream/` (see `ARROCCO-IMPORT.md` there), Arrocco's front end in `lib/ct800/port/` |
| [Lichess puzzle database](https://database.lichess.org/#puzzles) | selection of 3500 puzzles | CC0 1.0 Universal | offline puzzle pack in `lib/arrocco/src/arrocco/puzzles/`; how it was built: [docs/puzzles.md](docs/puzzles.md) |

## Artwork

The piece set in `assets/pieces/` is original CollectiCraft artwork, released with the rest of the project under GPL-3.0-or-later. No third-party piece set is used.

The sleep screen in `lib/arrocco/src/arrocco/ui/sleep_art.h` is Vincent van Gogh's *The Starry Night* (1889), which is in the public domain: the Google Art Project scan on [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Van_Gogh_-_Starry_Night_-_Google_Art_Project.jpg), marked public domain there, cropped to 800 × 480 and dithered to 1 bit by `assets/sleep/make_sleep_art.py`.
