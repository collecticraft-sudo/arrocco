# Collaudo: il giorno che arrivano i pezzi

Si va in ordine. Ogni tappa aggiunge **una cosa sola**, così se qualcosa non va si sa dov'è.
Cablaggio: `wiring.md`. Pezzi mancanti: `da-comprare.md`.

Cosa serve: calibro, Mac con PlatformIO, cavo USB-C **dati**, un panno morbido pulito (il pannello ci si appoggia sopra), telefono per le foto.
Senza multimetro si arriva fino alla tappa 3 compresa. **La batteria (tappa 4) senza multimetro non si collega.**

## A. Guardare e fotografare (niente corrente)

Foto nitide, fronte e retro, di ogni scheda e di ogni etichetta. Una foto col calibro accanto vale più di una misura scritta.

- [ ] **Bustina della driver board**: cerca sull'etichetta il codice del lotto e la data. Il lotto `MOA250113001` (gennaio 2025) dava pixel sporchi; `MOA250210012` (marzo 2025) e successivi sono a posto. Annota quello che trovi, qualunque sia.
- [ ] **Driver board**: i fori CN1 e CN2 sono vuoti, come previsto? Sul retro si leggono `5V GND 3V3 MOSI D9 SCK D7` e `RST CS BUSY DC D4 D5 D6`? Ci sono i due pad `+` e `−` della batteria?
- [ ] **XIAO**: è arrivato **con i pin saldati o senza**? (Se senza: non saldare ancora, vedi `da-comprare.md`.) L'antennina nella bustina serve solo per il WiFi, più avanti.
- [ ] **Flat del display (24 pin)**: tenendo il pannello a faccia in su, i contatti dorati del flat guardano **in su o in giù**? Copia la sigla stampata sul flat (tipo `FPC-C001 ...`) e quello che c'è scritto sull'etichetta dietro al pannello.
- [ ] **Flat del touch (6 pin)**: stessa domanda, contatti **in su o in giù** a pannello a faccia in su? I due flat escono dallo stesso lato lungo, come previsto?
- [ ] **FTS02**: copia la sigla di revisione stampata sulla scheda (attesa: `v3.0-20250819`). Trova la presa P7 / TP-FPC2 (`3.7/4.2/4.26/5.83/7.5`) e gli header P10 e P11.
- [ ] **Batteria**: gonfia, ammaccata o con fili spellati? Se sì non si usa. **Non inserirla da nessuna parte.**

## B. Misure col calibro

Seeed non pubblica disegni quotati della driver board: queste misure servono a te per la scocca. Scrivi il valore vero accanto a quello atteso.

| Cosa | Atteso | Misurato |
|---|---|---|
| Driver board, lunghezza × larghezza | 43 × 26 mm | |
| Driver board nuda, altezza (dal sotto della scheda alla cima dello zoccolo) | 8 mm | |
| Driver board, 4 fori di fissaggio: diametro | circa 2,2 mm (stima da foto) | |
| Driver board, fori: interasse sui due lati | circa 17–18 × 21–22 mm (stima da foto) | |
| Driver board, fori: distanza dai bordi della scheda | non nota | |
| Interruttore e presa batteria: posizione lungo il lato e sporgenza dal bordo | non note | |
| **Pila completa** con il XIAO nello zoccolo (dal punto più basso alla cima della USB-C) | stima 14,7 mm con pin di fabbrica; 12,2 mm col montaggio ribassato | |
| Centro della USB-C: altezza dal sotto della driver board | non nota | |
| Pannello, ingombro | 170,2 × 111,2 × 2,13 mm | |
| Flat display: larghezza | 24,0 mm | |
| Flat display: **lunghezza fuori dal vetro** e distanza dallo spigolo del pannello | non note | |
| Flat touch: larghezza × lunghezza fuori dal vetro | 3,5 × 47 mm | |
| Flat touch: distanza dallo spigolo del pannello | circa 39–43 mm dallo spigolo sinistro, visto di fronte | |
| Piastrina rigida col chip sul flat touch | 15 × 14 mm, parte a circa 4,5 mm dal vetro | |
| FTS02 (solo per il banco): lati e altezza totale | 58,6 × 54,7 mm; altezza stimata 13–14 mm | |
| Batteria: spessore × larghezza × lunghezza reali, lunghezza dei fili | nominale 6 × 60 × 90 mm | |
| Buzzer KY-006: ingombro della schedina e altezza | non noto | |

Interasse dei fori col calibro: misura da bordo esterno a bordo esterno dei due fori e togli un diametro.

## C. Prima accensione, a tappe

Il firmware di collaudo (`src/hwtest`) ha quattro schermate, **Board**, **Touch**, **Panel**, **Sound**, e una colonna di diagnostica che dice in chiaro cosa ha trovato e cosa no. È fatto apposta perché sul banco c'è solo il calibro: leggi lo schermo e copia i numeri.
Si carica dal Mac come spiegato nel `README.md` (sezione "Build the hardware test"). Se mentre gira scrive anche sul monitor seriale, copia pure quel testo.

**Tappa 0 — XIAO da solo.**
Solo XIAO e cavo USB-C. Carica il firmware. Se il Mac non vede la porta: tieni premuto il tasto BOOT del XIAO mentre infili il cavo, poi riprova.
Non servono i pin saldati.

**Tappa 1 — Display.**
A corrente staccata: XIAO nello zoccolo (**USB-C dal lato opposto al flat**, controlla due volte), flat a 24 pin nel connettore con i contatti verso l'alto, linguetta chiusa. Niente touch, niente batteria. Poi USB.
- Lo schermo deve disegnare. La prima volta fa un lampo nero: è normale.
- Guarda una schermata **tutta nera** e una **tutta bianca** (schermata Panel): devono essere pulite, senza puntini sparsi né "neve".
- Tempi attesi: refresh **parziale circa 0,45 s** (Good Display dichiara 0,3 s), refresh **completo circa 1,1–1,3 s**. Annota quelli veri.

**Tappa 2 — Touch.**
A corrente staccata: FTS02 collegato come in `wiring.md` §5, flat del touch in P7 **girato**. Poi USB.
- Schermata Touch: il GT911 deve rispondere (atteso indirizzo `0x5D`, product id `911`, risoluzione 800×480).
- **Se non risponde: stacca subito la USB**, gira il flat, riprova.
- Tocca i quattro angoli dello schermo e annota le coordinate che vedi. Alto-sinistra dovrebbe dare numeri vicini a 0, 0; basso-destra vicini a 800, 480. La direzione dell'asse Y non è documentata: se è capovolta si sistema nel firmware, non nei fili.

**Tappa 3 — Suono.**
A corrente staccata: buzzer come in `wiring.md` §6. Schermata Sound: deve suonare.

**Tappa 4 — Batteria.** Solo col multimetro.
1. Procedura della polarità, `wiring.md` §9, tutta.
2. Interruttore su ON, stacca la USB: la scheda deve **restare accesa**.
3. Interruttore su OFF a USB staccata: si spegne.
4. Per caricare: USB collegata **e** interruttore su ON, una notte (9–10 ore da scarica).

**Tappa 5 — Più avanti, quando arrivano i pezzi di `da-comprare.md`.**
Breakout FPC con le tre resistenze al posto del FTS02 (`wiring.md` §7), poi MAX17048 (§8). La prova è sempre la stessa: il touch risponde, e in più compare la percentuale della batteria.

## D. Se qualcosa non va

| Cosa vedi | Causa più probabile | Cosa controllare |
|---|---|---|
| Il Mac non vede il XIAO | cavo solo ricarica; XIAO non in modalità di caricamento | cambia cavo; BOOT premuto mentre infili il cavo |
| Tappa 1: schermo muto, e il refresh risulta quasi istantaneo (meno di 0,3 s) oppure lunghissimo (oltre 5 s, "Busy Timeout") | il display non parla con la scheda | flat a 24 pin: al rovescio, non in fondo o linguetta aperta. Poi: XIAO non spinto fino in fondo, o **girato** |
| Puntini sparsi o "neve" sul nero pieno | driver board del lotto difettoso di gennaio 2025 | non è il firmware: foto, codice lotto, e si chiede la sostituzione |
| Refresh parziale intorno a **1,5 s** invece di 0,45 | pannello di un lotto vecchio (forma d'onda diversa in memoria) | non è un guasto: mandami il numero, si sistema nel firmware |
| L'immagine sbiadisce o il XIAO si riavvia durante un refresh | 3,3 V che cede | cavo USB migliore o altra porta; a batteria: cella scarica |
| Tappa 2: nessun dispositivo sull'I2C | flat del touch nel verso sbagliato (caso più probabile) | **stacca subito**, gira il flat. Poi: presa sbagliata (P6 invece di P7), linguetta aperta |
| | fili | `A4`→`D4` e `A5`→`D5` (non scambiati), `3.3V` e `GND` di P11 (non `5V`!), non i pin `SDA`/`SCL` di P8 |
| | saldature degli header CN1/CN2 | saldature opache o a pallina: ripassale |
| Il GT911 risponde, ma a `0x14` invece di `0x5D` | il firmware non riesce a comandare INT o RST | filo `A0`→`D9` (INT) e filo `A3`→`D6` (RST) |
| Il GT911 risponde, ma i tocchi non arrivano | INT non arriva | filo `A0`→`D9` |
| I tocchi arrivano, ma nel posto sbagliato o a specchio | non sono i fili | mandami le coordinate dei quattro angoli |
| Nessun suono | filo del segnale o massa | `S`→`D7`, `−`→`GND` |
| A batteria non si accende | interruttore su OFF; cella scarica; spina non in fondo | se la polarità **non** l'avevi misurata: fermati e misurala adesso |
| Tappa 5: nessuna percentuale batteria | MAX17048 non risponde a `0x36` | SDA/SCL scambiati; fili ai pad `+`/`−`; batteria collegata? |

## E. Numeri da mandare a Claude

Copia e incolla questa lista con le risposte.

1. Driver board: codice lotto e data sull'etichetta.
2. XIAO: con o senza pin saldati.
3. Flat display: contatti in su o in giù (pannello a faccia in su); sigla stampata sul flat; etichetta dietro al pannello.
4. Flat touch: contatti in su o in giù; in quale posizione ha funzionato dentro P7 (pannello a faccia in su e FTS02 capovolto, oppure l'altra).
5. GT911: indirizzo trovato, product id, versione della configurazione, risoluzione X/Y, e ogni altro dato che la schermata Touch mostra.
6. Coordinate lette toccando i quattro angoli (alto-sx, alto-dx, basso-sx, basso-dx).
7. Tempi di refresh: parziale e completo, in millisecondi.
8. Nero pieno e bianco pieno: puliti o con difetti? Foto.
9. Caselle scure della scacchiera di prova: si distinguono bene dalle chiare? Dopo una decina di refresh parziali, quanto "fantasma" resta? Foto.
10. Tabella delle misure (sezione B) compilata.
11. Col multimetro: tensione della batteria all'arrivo; polarità della spina com'era (giusta o invertita).
12. Col multimetro, in ohm: resistenza del KY-006 tra `S` e `−` (circuito aperto = piezo; circa 16 Ω = magnetico).
13. Col multimetro, a tutto spento e con i Dupont staccati, sul FTS02 con il flat del touch inserito: ohm tra `A4` e `3.3V`, tra `A5` e `3.3V`, tra `A3` e `3.3V`. Serve a capire se il flat ha già le sue resistenze di pull-up: il FTS02 da solo dà circa 10 kΩ sulle prime due e circuito aperto sulla terza; valori più bassi vogliono dire che il flat ne ha di sue.
14. Più avanti, col multimetro in serie alla batteria (portata µA/mA): consumo con la scheda "addormentata".
