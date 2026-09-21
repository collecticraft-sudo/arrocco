# eink-chess

Scacchiera smart e-ink open, 7,5", touch, per gioco offline (engine) e online (Lichess).
Firmware per Seeed XIAO ESP32-S3 (Plus) + Seeed ePaper Driver Board + Good Display GDEY075T7-T01.

**Stato: fase 1** — display, touch e scacchiera funzionanti. Nessuna regola di gioco ancora: serve a validare hardware e tempi di refresh.

## Hardware
| Pezzo | Note |
|---|---|
| Good Display GDEY075T7-T01 | 800×480, UC8179, touch GT911 |
| Good Display ESP32-FTS02 | solo come adattatore per il flat del touch |
| Seeed ePaper Driver Board (114993558) | FPC display, carica LiPo, interruttore |
| Seeed XIAO ESP32-S3 Plus | 8 MB PSRAM, 16 MB flash |
| LiPo 1S 3,7 V 4000 mAh, JST PH 2.0 | 606090 o 407090 |
| KY-006 buzzer passivo | opzionale |

Cablaggio: [docs/wiring.md](docs/wiring.md).

## Flash
1. Installa [PlatformIO](https://platformio.org/) (estensione VS Code).
2. `pio run -t upload` con il XIAO collegato via USB-C.
3. `pio device monitor` per il log: all'avvio stampa il product id del GT911 e il tempo del full refresh.

Se il GT911 non risponde a 0x5D, cambia `TP_I2C_ADDR` in `src/config.h` a 0x14.

## Cosa fa la fase 1
- Full refresh all'avvio, scacchiera con posizione iniziale.
- Tap su un pezzo = selezione (cornice), tap su una casella = mossa (senza regole), partial refresh delle due caselle.
- Ogni 20 mosse un full refresh per pulire il ghosting.
- Il tempo di ogni partial refresh è mostrato nella colonna laterale: è il numero che ci interessa.

## Roadmap
1. ✅ Display + touch + scacchiera
2. Motore CT-800 (offline, livelli), regole complete, promozione, orologio
3. Lichess Board API (WiFi setup via captive portal, token, seek, partita in tempo reale)
4. Puzzle Lichess, replay PGN, batteria, deep sleep, OTA
5. Release MakerWorld (scocca + firmware)

## Licenza
GPL-3.0 (richiesta dall'integrazione futura di CT-800).
