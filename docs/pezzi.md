# Disegnare i pezzi di Arrocco

Guida per il set CollectiCraft a 1 bit. Servono 12 file SVG, uno per pezzo. Si parte da [`assets/pieces/template.svg`](../assets/pieces/template.svg).

## In breve

- Tela **56 × 56**, un'unità = un pixel del pannello.
- Solo **nero pieno e bianco pieno**. Niente grigi, niente sfumature.
- Due gruppi per file: **`mask`** (bianco, sotto) e **`ink`** (nero, sopra).
- Tratto minimo **2 px**, vuoto minimo **2 px**.
- Il pezzo sta dentro **48 × 48**, cioè 4 px liberi per lato.

## Le misure

Il pannello da 7,5" è largo 163,2 mm per 800 pixel: un pixel misura **0,204 mm**.

| Cosa | Pixel | Sul vetro |
|---|---|---|
| Casella | 56 × 56 | 11,4 mm |
| Area del pezzo (`ink`) | 48 × 48, da 4 a 52 | 9,8 mm |
| Limite della maschera (`mask`) | 52 × 52, da 2 a 54 | 10,6 mm |
| Tratto minimo | 2 | 0,41 mm |
| Vuoto minimo tra due neri | 2 | 0,41 mm |

I 4 px liberi per lato servono alla cornice di selezione e ai pallini delle mosse: non devono mai toccare il pezzo.

Tutti i pezzi poggiano sulla stessa linea: **y = 52**, il bordo basso dell'area verde del template. Il re è il più alto, il pedone il più basso.

## Solo bianco e nero

Lo schermo ha pixel neri o bianchi e basta. Il disegno viene trasformato in pixel a 56 px e ogni pixel coperto per meno della metà diventa bianco, gli altri neri. Conseguenze pratiche:

- Un tratto più sottile di circa **1,5 px sparisce o si spezza**. Resta sopra i 2 px.
- Un vuoto da 1 px tra due neri si chiude o resta a caso. Tienilo a 2 px o più.
- **Allinea i tratti principali alla griglia**: bordi orizzontali e verticali su coordinate intere (12, non 12,4). Un bordo a metà pixel cade da una parte o dall'altra senza che tu lo decida.
- Curve e diagonali diventano scalini. Le diagonali quasi orizzontali o quasi verticali sono le peggiori: meglio 45° o angoli decisi.
- Dove una curva incontra una retta nascono spesso tacche da 1 px. Si vedono solo nell'anteprima a pixel: vanno ritoccate a mano.
- I dettagli piccoli non pagano. A 11 mm conta la sagoma.

## I due gruppi: `mask` e `ink`

Le caselle scure non sono nere piene: sono un retino di puntini. Un pezzo disegnato direttamente lì sopra si confonderebbe con i puntini. Per questo ogni pezzo ha due strati.

- **`mask`** si disegna per prima, in **bianco**. È la sagoma piena del pezzo, **senza buchi**, **allargata di 2 px** tutto intorno. Cancella il retino dietro al pezzo e gli lascia un alone bianco.
- **`ink`** si disegna sopra, in **nero**. È il pezzo vero. Dentro `ink` una forma bianca messa sopra una nera la buca: lì si vede il bianco della maschera.

Come fare la maschera: copia la sagoma esterna del pezzo, uniscila in una forma sola, allargala di 2 px (*Offset path / Tracciato sfalsato*, 2 px, angoli arrotondati) e mettila nel gruppo `mask`. Se cambi la sagoma, rifai la maschera.

L'`ink` non esce mai dall'area verde (4–52). La maschera può uscire di 2 px, fino alla linea magenta (2–54), mai oltre.

## Bianchi e neri

- **Pezzi bianchi**: contorno nero da 2–3 px, interno bianco.
- **Pezzi neri**: pieni, con pochi dettagli bianchi all'interno (mai sotto i 2 px).

Devono distinguersi **al primo sguardo**, sia sulla casella chiara sia su quella scura. Sulla casella scura il bianco del pezzo bianco viene dalla maschera, e il pezzo nero si stacca grazie all'alone. Prova sempre tutte e due.

Ogni pezzo si deve riconoscere dalla sola sagoma: croce per il re, punte per la donna, merli per la torre, taglio della mitria per l'alfiere, testa di cavallo, pedone semplice. Stessa base per tutti aiuta a leggere il set come una famiglia.

I due pedoni di esempio (`example-wP.svg`, `example-bP.svg`) mostrano solo il meccanismo dei due strati. Lo stile è tuo.

## Il file

Nomi, esatti, maiuscole comprese: `wK wQ wR wB wN wP bK bQ bR bB bN bP`, tutti `.svg`, dentro `assets/pieces/`. `w` = bianco, `b` = nero, `N` = cavallo.

- `viewBox="0 0 56 56"`, senza cambiare le dimensioni della tela.
- I gruppi si chiamano esattamente `mask` e `ink` (nel file è l'`id` del gruppo). Il template li ha già come livelli: se disegni lì dentro, il nome resta giusto da solo. `mask` sta sotto `ink`.
- I livelli `square` e `guides` del template sono solo un aiuto: non vengono convertiti. Puoi lasciarli nel file.

**Solo forme piene.** Vanno bene tracciati chiusi con riempimento, rettangoli, cerchi, poligoni. Non vanno bene:

- **Tratti con spessore.** Una linea con "contorno 2 px" va trasformata in forma piena prima di salvare. In Illustrator: *Oggetto > Tracciato > Traccia contorno*, oppure *Oggetto > Espandi* (in inglese *Object > Path > Outline Stroke*, *Object > Expand*). In Inkscape: *Tracciato > Da contorno a tracciato*. In Affinity: *Livello > Espandi contorno*. In altri programmi cerca "espandi", "outline stroke", "converti in curve".
- **Testo.** Se proprio serve, convertilo in tracciati.
- **Sfumature, trasparenze, opacità**, metodi di fusione, ombre, sfocature, filtri.
- **Maschere di ritaglio e clip.** Se hai ritagliato una forma, applica il ritaglio davvero (*Elaborazione tracciati > Interseca / Sottrai*) così resta una forma normale.
- **Immagini incorporate**, simboli collegati, motivi (pattern).
- Colori diversi da `#000000` e `#ffffff`.

Salva come **SVG semplice** (*Plain SVG* in Inkscape; in Illustrator *Esporta > SVG* con stili "Attributi di presentazione"). Meglio senza trasformazioni sui gruppi `mask` e `ink`: niente scala o rotazione applicate al gruppo intero.

## Come diventano firmware

Lo fa Claude, tu consegni solo gli SVG.

1. Ogni gruppo viene disegnato a 56 × 56 px.
2. Soglia al 50 %: ogni pixel diventa nero o bianco.
3. Escono **due bitmap a 1 bit per pezzo** (maschera e inchiostro), scritte come array C dentro il firmware. 24 bitmap da 392 byte: meno di 10 kB in tutto.
4. Sullo schermo l'ordine è: casella, maschera in bianco, inchiostro in nero.

Il convertitore non c'è ancora: arriva più avanti. Fino ad allora il firmware usa un set provvisorio.

## Anteprima

**Nel programma di disegno.** Accendi il livello `square` per vedere la casella scura, spegnilo per la chiara. Il retino del template è indicativo: quello definitivo si sceglie sul pannello vero. Poi rimpicciolisci finché la casella misura **11,4 mm sullo schermo** (col calibro): è la grandezza vera. Se lì il pezzo non si legge, non si leggerà sul pannello. Se il programma ha un'anteprima a pixel (*Vista > Anteprima pixel*), usala a 56 px senza anti-aliasing.

**Sulla scacchiera vera.** Una volta convertiti, i pezzi compaiono nel simulatore per Mac in `sim/`: si lancia con `sim/run.sh` e mostra lo schermo 800 × 480 pixel per pixel, con caselle chiare, scure, cornice e pallini.

## Checklist

- [ ] 12 file con i nomi giusti in `assets/pieces/`
- [ ] `viewBox="0 0 56 56"`, gruppi `mask` e `ink`, `mask` sotto
- [ ] Solo `#000000` e `#ffffff`, nessuna sfumatura o trasparenza
- [ ] Solo forme piene: nessun tratto con spessore, testo, clip, filtro, immagine
- [ ] Nessun tratto e nessun vuoto sotto i 2 px
- [ ] Bordi principali su coordinate intere
- [ ] `ink` dentro 4–52, `mask` dentro 2–54
- [ ] `mask` piena, senza buchi, 2 px più larga della sagoma
- [ ] Tutti i pezzi poggiano su y = 52
- [ ] Bianchi e neri si distinguono su casella chiara **e** scura
- [ ] Ogni pezzo si riconosce dalla sagoma a 11,4 mm
