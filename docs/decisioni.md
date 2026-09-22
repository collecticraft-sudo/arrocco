# Arrocco — decisioni di progetto

Aggiornato il 21 settembre 2026. Questo file è la fonte di verità: se il codice o un altro documento lo contraddice, ha ragione questo file (oppure va aggiornato qui per primo).

Nome pubblico: **Arrocco — e-ink chess**, by CollectiCraft. Mai "Atlas" né "replica" in nomi, immagini o descrizioni: al massimo "an open alternative to commercial e-ink chessboards".

## Chi fa cosa
- Fabrizio: compra, salda, assembla, disegna la scocca e il set di pezzi.
- Claude: firmware, simulatore, documentazione tecnica. Lavora in autonomia nel repo e fa i commit da solo. Niente push su GitHub senza chiedere.

## Vincoli di Fabrizio
- Saldatore sì, poca pratica: solo saldature passanti (header, fili nei fori). Niente pad minuscoli sotto il XIAO.
- XIAO ESP32-S3 Plus con header già saldati.
- Strumenti sul banco: solo calibro. Il firmware di collaudo deve quindi diagnosticare da solo e dirlo sullo schermo. Un multimetro economico va comprato (polarità batteria).
- Download autorizzati sul Mac: toolchain e librerie PlatformIO; il sorgente di CT800 da ct800.net (fatto il 22/09/2026, importato non modificato in `lib/ct800/upstream/`); il database dei puzzle Lichess da database.lichess.org (fatto il 22/09/2026, resta fuori dal repo: si rigenera col comando in `docs/puzzles.md`).

## Hardware
- Pannello Good Display GDEY075T7-T01 (800×480, UC8179, touch GT911), Seeed ePaper Driver Board 114993558, XIAO ESP32-S3 Plus, LiPo 606090 4000 mAh, buzzer KY-006.
- Uso **orizzontale da tavolo**: scacchiera a sinistra, colonna laterale a destra.
- Scocca **sottile con un "mento"**: l'elettronica (12–15 mm con il XIAO montato) sta in una fascia accanto al display, dietro il pannello solo la batteria da 6 mm. La USB-C esce dal bordo del mento.
- Risveglio **col tocco**; l'interruttore della driver board è lo spento vero.
- Percentuale batteria: **MAX17048** I2C (0x36) sullo stesso bus del touch, celle ai pad BAT +/− sul retro della driver board.
- FTS02 solo al banco. Nella scocca: breakout FPC 6 pin passo 0,5 mm + due pull-up da 4,7 kΩ su SDA e SCL (+ 10 kΩ su RST consigliato).
- Sulla driver board vanno saldati due header 1×7 nei fori CN1/CN2 (meglio a 90° o fili diretti, per lo spessore).

### Pin map definitiva (GPIO ESP32-S3, tra parentesi il nome XIAO)
| Funzione | GPIO | XIAO |
|---|---|---|
| EPD RST | 1 | D0 |
| EPD CS | 2 | D1 |
| EPD BUSY | 3 | D2 |
| EPD DC | 4 | D3 |
| I2C SDA (touch + MAX17048) | 5 | D4 |
| I2C SCL (touch + MAX17048) | 6 | D5 |
| Touch RST | 43 | D6 |
| Buzzer | 44 | D7 |
| EPD SCK | 7 | D8 |
| Touch INT (sveglia dal deep sleep) | 8 | D9 |
| EPD MOSI | 9 | D10 |

`SPI.begin(7, -1, 9, -1)`: il display non ha MISO e GPIO8 è del touch. I2C a 100 kHz finché non ci sono pull-up da 4,7 kΩ e fili corti.

Collegamento FTS02 (pannello 7,5" nella presa TP-FPC2/P7, flat inserito **girato**): header P10 `A0`=INT, `A3`=RST, `A4`=SDA, `A5`=SCL; header P11 `3.3V` e `GND`. Mai il pin `5V` di P11, mai i pin serigrafati `SDA`/`SCL` di P8.

## Schermo e interfaccia
- Caselle da **56 px** con coordinate a–h e 1–8: scacchiera 448 px a partire da x=16, y=16; colonna laterale da x=480 a 799. Tutto allineato a multipli di 8.
- **Un solo refresh per evento** visibile all'utente, disegnando nel buffer intero e spingendo una volta sola.
- **Pochi lampi, un po' di ghosting accettato**: refresh completo solo ogni ~16 refresh parziali e solo nelle pause naturali; costante regolabile.
- Pannello spento (powerOff) dopo qualche secondo di inattività, hibernate prima del deep sleep.
- Primo tocco su un pezzo: **cornice + pallini sulle mosse legali**, in un refresh.
- Pezzi: **set CollectiCraft disegnato da Fabrizio** (1 bit, sagoma + maschera bianca). Fino ad allora un set provvisorio disegnato dal firmware.
- Interfaccia **solo in inglese**, con tutte le stringhe in un file unico.

## Gioco
- Offline per tutti, dai bambini al circolo: livelli con nomi sopra l'Elo di CT-800, più livelli facili sotto i 1000.
- Nella prima versione giocabile: partita contro il motore, **due giocatori sulla stessa scacchiera**, **orologio**, **puzzle offline** (pacchetto Lichess CC0 in flash). Ritiro mossa e suggerimento dopo.
- Ordine di costruzione: prima la libreria di regole (serve a tutto), poi interfaccia e due giocatori, poi CT-800 in un task suo che parla UCI, poi Lichess.
- Lichess: login con **QR (OAuth PKCE)** e token incollato come riserva; modi della prima versione: **contro lo Stockfish di Lichess** e **sfida un amico / accetta inviti**. Partite **amichevoli di default**, rated a scelta da menu.

## Rilascio
- Scocca su MakerWorld come **Exclusive**: i file della scocca (STL, 3MF, STEP) **non** entrano in questo repo né altrove. Qui stanno solo firmware, misure e cablaggio.
- Niente vendita: solo file gratuiti.
- Firmware GPL-3.0-or-later su GitHub (account `collecticraft-sudo`), **flasher dal browser** su GitHub Pages, binari costruiti dalla CI a ogni versione taggata. Mai secure boot né flash encryption.
- Simulatore del display sul Mac, da provare prima che arrivi l'hardware.
