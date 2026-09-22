# Prompt per generare i pezzi di riferimento

Servono come riferimento visivo da ricalcare, non come file finali: il firmware usa le bitmap generate da `draw_pieces.py`. Quindi quello che conta è che la **sagoma** sia giusta e leggibile in nero pieno su bianco.

## Prompt principale (tutti e sei i pezzi)

```
Flat vector silhouette chess piece set, side view, six pieces in a single row:
king, queen, rook, bishop, knight, pawn. Pure solid black shapes on a pure white
background. No grey, no gradient, no shading, no outline stroke, no anti-aliasing,
no perspective, no 3D, no shadow, no reflection, no background texture, no text.

Classic Staunton proportions: wide round base, tapering column, distinctive head.
King with a cross on top. Queen with a five-point coronet. Rook with three square
crenellations. Bishop with a bulbous mitre, a small knob on top and a diagonal slit.
Knight as a horse head facing LEFT with a long blunt muzzle, a pronounced jaw and
cheek, two small ears laid back, and a stepped mane along the arched neck.
Pawn with a round ball head on a collar.

Each piece centred in its own square cell, same baseline, same height ratio,
generous even spacing. Icon design, pictogram, woodcut stencil, high contrast,
crisp hard edges. Front-facing orthographic elevation, as printed in a chess book
diagram.
```

## Prompt per il solo cavallo (quello difficile)

```
Flat vector silhouette of a chess knight, single piece, side view, horse head
facing LEFT on a wide round base. Pure solid black on pure white. No grey,
no gradient, no shading, no outline, no 3D, no shadow, no text.

Staunton knight: long blunt muzzle held level and jutting well forward of the
chest, nostril and mouth suggested by a thin notch, deep rounded jaw and cheek,
eye set high and far back, two short ears laid back over the poll with a clear
notch between them, thick arched neck with a stepped mane running down the back,
flaring into a wide circular base. The head must overhang the neck at the front.

Woodcut stencil, pictogram, orthographic side elevation, crisp hard edges,
high contrast, centred, generous margin.
```

## Cosa chiedere se il risultato non convince

Aggiungi in coda, una alla volta:

- `the muzzle must be longer than the rest of the head` — se esce un cane
- `ears short and swept back, not pointed upright` — se escono orecchie da cane o da coniglio
- `thicker neck, strongly arched` — se il collo è sottile
- `no rider, no armour, no chess board, no other pieces` — se aggiunge roba

## Cosa evita il prompt, e perché

| Parola chiave | Serve a |
|---|---|
| `pure solid black on pure white` | il pezzo finale è a 1 bit: grigi e sfumature vanno buttati comunque |
| `no outline stroke` | il contorno dei pezzi bianchi lo calcola lo script, non deve stare nel disegno |
| `side view`, `orthographic` | niente prospettiva: il pezzo è visto di lato, come in un diagramma |
| `facing LEFT` | tutti i cavalli guardano a sinistra, bianchi e neri |
| `woodcut stencil`, `pictogram` | spinge verso la sagoma piena invece del rendering realistico |
| `same baseline, same height ratio` | i sei pezzi devono stare insieme sulla stessa scacchiera |

## Come lo uso

Genero l'immagine, ne ricavo la sagoma a soglia, e la traccio come poligoni dentro
`draw_pieces.py`. Il riferimento resta in `assets/pieces/reference/` come materiale di
lavoro. Le bitmap che finiscono nel firmware restano generate dal codice, quindi
restano nostre: nessuna immagine generata entra nel prodotto.
