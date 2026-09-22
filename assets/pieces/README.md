# Set di pezzi CollectiCraft

Set originale, disegnato per caselle da 56 px su e-ink a 1 bit. Nessun vincolo di licenza da terzi: è nostro, GPL-3.0-or-later come il resto del firmware.

- `draw_pieces.py` — la fonte di verità. Ogni pezzo è un insieme di forme geometriche; lo script ne ricava la maschera bianca (l'alone che stacca il pezzo dalle caselle tratteggiate), il contorno dei pezzi bianchi e la sagoma piena dei neri.
- `svg/` — gli stessi pezzi in SVG, un file per tipo, con i gruppi `mask` e `ink`. Da qui puoi ridisegnarli a mano.
- `piece_bitmaps.h` — generato, è quello che finisce nel firmware. Non modificarlo a mano.
- `preview-set.png`, `preview-board.png` — anteprime generate.
- `template.svg` — griglia 56×56 con l'area di sicurezza, se parti da zero.

## Rigenerare

```
python3 draw_pieces.py --sheet preview-set.png --board preview-board.png \
                       --header piece_bitmaps.h --svg svg
```

Poi copia `piece_bitmaps.h` dove il firmware lo legge (vedi `docs/pezzi.md`).

## Se li ridisegni tu

Le regole che contano sono in `docs/pezzi.md`: nero e bianco pieni senza grigi, tratto minimo 2 px, il pezzo dentro 48×48 con 4 px di margine. Lo script rasterizza a 4× e taglia al 50 %, esattamente come farà il pannello: quello che vedi nell'anteprima è quello che vedrai sul vetro.
