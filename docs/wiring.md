# Cablaggio — fase 1

## Display (zero fili)
Flat 24 pin del GDEY075T7-T01 → presa FPC della **Seeed ePaper Driver Board** (contatti verso l'alto, linguetta nera chiusa dopo l'inserimento).
XIAO ESP32-S3 Plus → socket della driver board (USB-C verso l'esterno, verso il bordo con l'interruttore).

## Touch (6 fili Dupont F-F)
Flat 6 pin del touch → presa **"Touch Screen / TP-FPC2"** sul FTS02 (quella marcata 3.7/4.2/4.26/5.83/7.5).
Dal header del FTS02 al XIAO:

| FTS02 header | XIAO pin | GPIO |
|---|---|---|
| 3V3 / VCC | 3V3 | — |
| GND | GND | — |
| TP_SDA (SDA) | D4 | 5 |
| TP_SCL (SCL) | D5 | 6 |
| TP_INT | D6 | 43 |
| TP_RST | D7 | 44 |

Le etichette esatte sono serigrafate sul FTS02 accanto al header. La presa e-paper del FTS02 resta vuota.
Il FTS02 va alimentato a **3,3 V** (non 5 V): il GT911 accetta 2,8–3,6 V.

## Buzzer KY-006 (3 fili)
| KY-006 | XIAO |
|---|---|
| S | D9 |
| – | GND |
| + (centrale) | non collegato (il KY-006 passivo non lo usa) |

## Batteria
JST PH 2.0 → presa BAT della driver board. **Prima**: tester sul connettore, rosso = + secondo la serigrafia della board.
Interruttore della driver board = accensione generale.

## Pin liberi sul XIAO Plus
D8/D10 sono SPI display, D9 buzzer. Restano D1..D3 no (display). Sul Plus ci sono i pad extra sul retro (GPIO 47, 48, 38, ecc.) per pulsanti fisici futuri.
