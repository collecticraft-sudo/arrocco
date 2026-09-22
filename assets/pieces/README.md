# Set di pezzi CollectiCraft

Pezzi a 1 bit per caselle da 56 px su e-ink. Nostri, GPL-3.0-or-later come il resto del firmware.

## Come nasce il set

Due strade, stesso risultato finale (tre bitmap per pezzo: maschera bianca, contorno per i bianchi, sagoma piena per i neri).

1. **`trace_reference.py` — quella in uso.** Parte da `reference/staunton-row.png`, un'immagine generata con il prompt in `PROMPT-riferimento.md`: sei sagome Staunton in fila, a **altezze reali**. Lo script le separa, le riscala una per una e le rimette tutte sulla stessa linea di base.

   La normalizzazione è il punto. Un'immagine generata dà il re alto il doppio del pedone, ma sulla scacchiera ogni pezzo deve riempire la sua casella. Quindi: l'altezza segue `HEIGHTS` (il re riempie, il pedone resta il più basso ma non sparisce), e la larghezza è scalata a parte finché **la base misura `BASE_W` px uguale per tutti**. È la base che tiene insieme visivamente un set. Un fattore di larghezza fisso invece deforma, perché i sei disegni di riferimento non hanno lo stesso rapporto.

2. **`draw_pieces.py` — il set disegnato a mano.** Ogni pezzo è un'unione di poligoni e cerchi scritti nel codice. Più grossolano, ma non dipende da nessuna immagine. Resta qui come riserva e perché contiene il rasterizzatore che usano entrambi.

Tutti e due rasterizzano a 4× e tagliano al 50 %, che è quello che farà il pannello: **l'anteprima è quello che vedrai sul vetro.**

## File

- `piece_bitmaps.h` — generato, è quello che finisce nel firmware. Non modificarlo a mano.
- `preview-set.png`, `preview-board.png` — anteprime generate.
- `reference/staunton-row.png` — il riferimento di partenza.
- `PROMPT-riferimento.md` — il prompt per rigenerare il riferimento.
- `svg/`, `template.svg` — i pezzi disegnati a mano in SVG, se vuoi ripartire da lì.

## Rigenerare

```
python3 trace_reference.py --sheet preview-set.png --board preview-board.png \
                           --header piece_bitmaps.h
```

Con un riferimento nuovo: `--reference reference/tuo-file.png`. Serve un PNG a 8 bit non interlacciato, sfondo chiaro, sei pezzi neri in fila nell'ordine re, donna, torre, alfiere, cavallo, pedone.

## Se qualcosa non torna

- **Un pezzo troppo alto o troppo basso**: `HEIGHTS` in `trace_reference.py`.
- **Set troppo stretto o troppo largo**: `BASE_W`.
- **Lo script non trova sei pezzi**: le sagome nel riferimento si toccano, oppure lo sfondo non è abbastanza chiaro. Alza la soglia in `to_binary`.
