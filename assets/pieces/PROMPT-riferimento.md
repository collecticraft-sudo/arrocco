# Prompt per generare i pezzi di riferimento

Servono come riferimento visivo da ricalcare, non come file finali: nel firmware finiscono le bitmap generate da `draw_pieces.py`. Quello che conta è che la **sagoma** sia giusta, riconoscibile e **già proporzionata per stare in un quadrato**.

## La regola che cambia tutto

I set di scacchi online non rispettano le altezze reali dei pezzi. Ogni pezzo è riscalato per riempire la **sua** casella: il pedone è disegnato molto più grande del vero rispetto al re, altrimenti sulla scacchiera sparirebbe. Se chiedi "sei pezzi in fila" il generatore li mette in scala reale e in un quadrato non ci stanno più.

Quindi: **un pezzo per immagine, tela quadrata, il pezzo riempie la tela.**

## Prompt — uno per pezzo

Genera sei immagini separate. Copia il blocco e sostituisci le due righe fra parentesi quadre.

```
A single chess piece icon: [PIECE]. Square 1:1 canvas, pure white background.
The piece is drawn as one solid black shape.

SIZING IS CRITICAL: the piece is centred horizontally, its base resting on the
bottom margin and its top reaching the top margin, filling about 85% of the canvas
height with an even margin all around. It must fill the square: do not leave it
small in the middle, do not let it touch or cross the edges. Scale this piece to
fill its own square regardless of how tall the piece would be in real life, the way
chess piece icons on online chess sites are normalised so a pawn and a king occupy
the same board square.

Shape: [DESCRIPTION].

Style: the flat two-dimensional chess piece icon used on online chess sites and in
printed chess diagrams. Side elevation, orthographic. No perspective, no 3D, no
shading, no gradient, no grey, no shadow, no reflection, no texture, no outline
stroke, no anti-aliasing, no board, no other pieces, no text, no border, no frame,
no watermark.
```

### Le sei sostituzioni

| `[PIECE]` | `[DESCRIPTION]` |
|---|---|
| `the king` | `classic Staunton king: wide round base, tapering collar, a banded crown, and a plain Latin cross on top. The cross is the tallest part` |
| `the queen` | `classic Staunton queen: wide round base, tapering body, a flaring banded crown topped by a coronet of five sharp points, the centre point tallest and capped with a small ball` |
| `the rook` | `classic Staunton rook: wide round base, a short straight cylindrical tower flaring slightly upward, topped by a flat battlement with three square crenellations separated by two square gaps` |
| `the bishop` | `classic Staunton bishop: wide round base, slim waist, a tall bulbous mitre with a single diagonal slit cut across it, and a small round knob on top, wider than the taper below it` |
| `the knight` | `classic Staunton knight: a horse head facing LEFT on a wide round base. The head is tilted DOWN about 40 degrees, so the face is one long straight diagonal running down and forward from between the ears to the nose. Long blunt muzzle carried low, deep rounded jaw and cheek, eye set high and far back, two short ears laid back over the poll with a clear gap between them, thick arched neck with a stepped mane down the back. The head overhangs the neck at the front` |
| `the pawn` | `classic Staunton pawn: wide round base, a short collar, a slim stem and a large round ball head. The ball head is generous, not small` |

## Se il risultato non convince

Aggiungi in coda, una riga alla volta:

- `the piece must fill the square, top to bottom` — se resta piccolo in mezzo alla tela
- `the muzzle must be longer than the rest of the head` — cavallo che esce come un cane
- `ears short and swept back, not pointed upright` — orecchie da cane o da coniglio
- `thicker neck, strongly arched` — collo troppo sottile
- `pure black fill, no white lines inside the shape` — se aggiunge dettagli interni che non servono
- `no rider, no armour, no crown jewels, no decoration` — se aggiunge roba

## Variante: tutti e sei in una immagine sola

Meno affidabile (il generatore tende comunque alle altezze reali), ma comoda per un colpo d'occhio:

```
Six chess piece icons in one horizontal row on a pure white background: king, queen,
rook, bishop, knight, pawn. Each piece is a solid black shape inside its own invisible
square cell, and all six cells are the same size.

SIZING IS CRITICAL: every piece is scaled to fill about 85% of the height of its OWN
cell, so the pawn is drawn as large as the king. Do NOT draw them at their real
relative heights. This is how chess piece icons are normalised on online chess sites,
where every piece has to fill one board square.

Classic Staunton shapes: king with a cross on top; queen with a coronet of five points;
rook with three square crenellations; bishop with a bulbous mitre, a diagonal slit and
a knob on top; knight as a horse head facing LEFT with the head tilted down about 40
degrees and a long muzzle carried low; pawn with a large round ball head.

Flat two-dimensional icon style, side elevation, orthographic. No perspective, no 3D,
no shading, no gradient, no grey, no shadow, no texture, no outline stroke, no board,
no text, no frame.
```

## Cosa ci faccio

Ricavo la sagoma a soglia dall'immagine, la traccio come poligoni dentro `draw_pieces.py`, e da lì escono alone, contorno e sagoma piena come per gli altri pezzi. Il riferimento resta materiale di lavoro in `assets/pieces/reference/`: nel firmware non entra nessuna immagine generata.

Formato utile: PNG quadrato, almeno 1024×1024, sfondo bianco pieno (non trasparente: a me serve il contrasto).
