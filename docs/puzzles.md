<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Il pacchetto di puzzle offline

Arrocco porta a bordo 3500 puzzle tattici, senza rete e senza scheda SD: stanno
in flash, dentro il firmware.

## Da dove vengono i dati

Dal **Lichess puzzle database**, <https://database.lichess.org/#puzzles>,
pubblicato in **CC0 1.0 Universal** (pubblico dominio). Nessun obbligo legale di
attribuzione, ma la citazione si tiene e va riportata ovunque si mostrino i
puzzle (schermata di crediti, README, `THIRD-PARTY.md`):

> Puzzles from the Lichess puzzle database (https://database.lichess.org/#puzzles), CC0 1.0 Universal.

Il file sorgente, `lichess_db_puzzle.csv.zst`, pesa circa 304 MB compressi e
contiene 6.100.952 puzzle. **Non è nel repository** e non ci deve finire: chi
vuole rigenerare il pacchetto se lo riscarica.

## Come si rigenera

```sh
# una volta sola, fuori dal repository:
curl -O https://database.lichess.org/lichess_db_puzzle.csv.zst

python3 tools/make_puzzle_pack.py /percorso/a/lichess_db_puzzle.csv.zst
```

Serve Python 3.14 o più recente (usa `compression.zstd` della libreria standard,
nient'altro). Il file viene letto a flusso, mai caricato in memoria; la
selezione è deterministica, quindi dallo stesso database escono sempre gli
stessi byte. Circa sei secondi in tutto.

Lo script riscrive cinque file generati — non si modificano a mano:

| file | cosa contiene |
| --- | --- |
| `lib/arrocco/src/arrocco/puzzles/puzzle_data.h` | conteggio, versione del formato, dimensioni, descrizione del layout |
| `lib/arrocco/src/arrocco/puzzles/puzzle_data.cpp` | i due array `const uint8_t` |
| `lib/arrocco/src/arrocco/puzzles/theme_data.h/.cpp` | l'enum `ThemeId` e le etichette in inglese |
| `test/puzzles/puzzle_reference.h` | le righe CSV grezze, solo per il test nativo |

Dopo la rigenerazione si aggiornano le tre dimensioni attese in
`test/puzzles/pack_main.cpp` e si rilancia il test.

## Che cosa è stato scelto

3500 puzzle, 250 per ognuna delle 14 fasce di rating da 600 a 1999. Scartati:
scostamento del rating sopra 80, popolarità sotto 90, meno di 500 partite
giocate, più di 12 semimosse, posizioni di partenza duplicate, e le posizioni
in cui il contatore delle mosse senza presa né spinta di pedone, sommato alle
semimosse della soluzione, arriverebbe a 100 (il pacchetto quel contatore non lo
salva, vedi sotto: su questo database ne scarta una sola). Dentro ogni fascia le
22 tematiche si alternano a giro, prendendo ogni volta il puzzle più popolare e
più giocato che resta: 1.036.885 candidati idonei, 3500 tenuti.

Ogni puzzle porta **una** etichetta, la prima di questo elenco che compare fra
le sue tematiche Lichess (prima le più specifiche): matto soffocato, matto del
corridoio, matto in 1/2/3, scacco doppio, forchetta, inchiodatura, infilata,
attacco di scoperta, deviazione, attrazione, sgombero, interferenza, pezzo
intrappolato, pezzo in presa, sacrificio, promozione, pedone avanzato,
zugzwang, mossa difensiva, mossa tranquilla.

## Quanto occupa

| | byte |
| --- | --- |
| indice (12 byte per puzzle) | 42.000 |
| blob (posizione + mosse) | 96.741 |
| **totale** | **138.741** (39,6 byte per puzzle) |

Il budget era 300 KB: siamo sotto della metà. La posizione non è mai testo: una
maschera di occupazione da 8 byte, poi mezzo byte per ogni pezzo presente (in
media 16,5 pezzi) e un byte di flag; le mosse sono 2 byte l'una (in media 5,08
per puzzle). Una casella occupata da un pedone che ha appena fatto il salto
doppio ha un codice suo, così la casella di presa en passant non costa niente.

Due cose non vengono salvate, e nessuna delle due serve. Il tipo di mossa
(arrocco, presa en passant, promozione) non è scritto da nessuna parte: il
lettore ripassa ogni mossa per `Position::parseUci`, che glielo ricava dalla
posizione — nel pacchetto di oggi sono 11 arrocchi e 6 prese en passant, e il
test verifica che tornino indietro con il tipo giusto. Il contatore delle mosse
senza presa né spinta di pedone non c'è: ogni posizione torna con `0 1`. Vale
solo perché il generatore rifiuta i puzzle in cui quel contatore più la
soluzione arriverebbe a 100; su questo database il caso peggiore è 95.

In flash: 138.741 byte di dati più 2.122 byte di lettore e 723 di etichette,
cioè 141.586 byte in tutto, e zero RAM (`xtensa-esp32-elf-size`: tutto `text`,
`data` e `bss` a zero).

## Che cosa offre il lettore a una futura schermata

`lib/arrocco/src/arrocco/puzzles/puzzles.h`, namespace `arrocco::puzzles`:
niente heap, niente STL, niente UI. Come Lichess, il pacchetto conserva la
posizione **prima** dell'errore dell'avversario; il lettore fa da solo la prima
mossa, quindi la schermata vede già la posizione giusta.

- `count()`, `byIndex(i, Puzzle&)` → `id` (i 5 caratteri con cui cercarlo su
  lichess.org/training/…), `rating`, `theme`, `solutionPlies`, `endsInMate`.
- `themeName(theme)` → l'etichetta in inglese già pronta da stampare;
  `themeKey(theme)` → il nome della tematica su Lichess.
- `startPosition(i, Position&)` e `startFen(i, buf, n)` → la posizione che vede
  chi risolve; il colore al tratto è il suo.
- `setupPosition(i, …)` e `blunderMove(i)` se si vuole mostrare l'errore che ha
  creato il puzzle.
- `solutionMoves(i, buf, n)` → tutta la linea, per il pulsante "soluzione".
- Selezione: `Filter{minRating, maxRating, theme, matesOnly}` con
  `next()`, `countMatching()`, `nth()` e `lowerBoundByRating()`. Il pacchetto è
  ordinato per rating, quindi la fascia costa una ricerca binaria.
- `Cursor`: `begin(i)`, `position()`, `solversTurn()`, `expected()` (il
  suggerimento), `isCorrect(mossa)`, `play(mossa)` — che gioca anche la risposta
  dell'avversario — `lastSolverMove()`, `lastReply()`, `solved()`, `restart()`.
  Quando la soluzione dà matto, `isCorrect()` accetta qualunque altro matto,
  come fa Lichess.

## Il test

```sh
make -C test/puzzles test
```

Sono due programmi.

`build/pack` controlla **tutti** e 3500 i puzzle contro le righe CSV originali
(`test/puzzles/puzzle_reference.h`, i campi grezzi del CSV) e contro le nostre
regole: la posizione impacchettata coincide con il FEN di Lichess (disposizione,
tratto, arrocchi, en passant), ogni mossa è legale dove viene giocata, i colori
si alternano, nessun puzzle è già finito alla partenza, i matti in 1/2/3 danno
matto davvero, arrocchi e prese en passant tornano indietro con il tipo giusto,
il contatore delle 50 mosse era davvero trascurabile, id e rating sono sensati,
non ci sono duplicati, e il `Cursor` arriva in fondo a ogni soluzione.
**322.271 controlli, 0 fallimenti.**

`build/fuzz` attacca il lettore con dati rotti. Prende quattro puzzle veri, li
mette su pagine di memoria con due pagine illeggibili ai lati, poi dà a ogni
byte dell'indice e del blob ognuno dei 256 valori possibili e richiede al
lettore tutto quello che sa produrre: offset che puntano oltre la fine, numeri
di mosse impossibili, maschere di occupazione assurde, indici fuori intervallo,
filtri che non pescano niente, soluzioni di una mossa sola. Un byte letto fuori
dal pacchetto sarebbe un errore di segmentazione sulla pagina di guardia — e che
la guardia funzioni davvero è la prima cosa che il test verifica. I buffer che il
chiamante passa sono circondati da muri di 0xAB, così si vede anche una scrittura
di troppo. **11.839.911 controlli, 0 fallimenti**, in meno di due secondi.

```sh
make -C test/puzzles sanitize   # gli stessi test sotto UndefinedBehaviorSanitizer
```

Secondo parere, non necessario: le letture fuori dal pacchetto le prendono già le
pagine di guardia, anche senza sanitizer. Qui si cercano le altre cose — overflow
con segno, scorrimenti storti, letture disallineate. Pulito, in cinque secondi.

AddressSanitizer si chiede a mano:

```sh
make -C test/puzzles sanitize SAN=address,undefined SAN_STRIDE=23
```

Su questa macchina rallenta il generatore di mosse di più di mille volte, per cui
senza `SAN_STRIDE` (che tiene tutti i byte ma meno valori per byte) non finisce.
