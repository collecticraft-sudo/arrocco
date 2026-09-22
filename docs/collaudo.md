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

Il firmware di collaudo è `src/hwtest`. Sul banco c'è solo il calibro: si legge lo schermo e si copia il log seriale.

Caricare e ascoltare, dal Mac, nella cartella del progetto:

```
pio run -e hwtest -t upload
pio device monitor
```

Se il Mac non vede la porta: tieni premuto BOOT sul XIAO mentre infili il cavo, poi riprova.

**Cosa fa all'avvio.**
- **Un bip** subito: il firmware gira, qualunque cosa faccia lo schermo.
- Lampo nero, poi compare la schermata **Touch**. È la prima apposta: finché gli assi del touch non sono provati, nessun pulsante è affidabile.
- **Due bip**: la prima schermata è stata mandata al pannello.
- Sul seriale l'ultima riga è `READY tap the screen, or press '?' here for the serial keys`.

**Lo schermo.** A sinistra la prova, a destra (da x=480) la colonna di diagnostica. In basso nella colonna i quattro pulsanti delle schermate: `Board`, `Touch`, `Panel`, `Sound` (quella attiva è nera). Sopra, i pulsanti della schermata: `Reset` (Board), `Clear` (Touch), `All black` / `All white` / `Full refresh` (Panel), `Beep` / `Scale` / `Sweep` (Sound).

Le righe della colonna, dall'alto:

| Riga | Cosa c'è scritto | Cosa vuol dire |
|---|---|---|
| 1 | `Arrocco hwtest 0.3` | versione |
| 2 | `Touch OK 0x5D id 911` | touch trovato, fili RST e INT provati |
| | `TOUCH WARN 0x.. id 911` | il touch risponde ma c'è un difetto: leggi la riga 3 |
| | `TOUCH FAIL no answer` | nessuna risposta sull'I2C: leggi la riga 3 |
| | `TOUCH FAIL at 0x..` / `TOUCH FAIL I2C errors` | risponde male, o si è perso dopo: cavo che balla |
| 3 | vuota | tutto bene |
| | `no answer: flat? RST wire?` | flat del touch al rovescio o non in fondo; oppure filo RST |
| | `SDA+SCL low: 3V3 missing?` | sul bus non ci sono i 3,3 V: fili `3.3V`/`GND`, o touch non collegato |
| | `SDA low: ...` / `SCL low: ...` | quel filo manca (`A4`→`D4` è SDA, `A5`→`D5` è SCL) |
| | `swap the A4/A5 wires` | SDA e SCL scambiati: funziona lo stesso, ma scambiali |
| | `0x14: INT or RST wire bad` | il chip ha preso l'indirizzo sbagliato: filo INT o RST |
| | `addr stuck: RST/INT wire?` | il chip non obbedisce al reset: tocca lo schermo 3–4 volte e leggi la riga 5 |
| | `cfg ...x..., not 800x480` | configurazione del touch inattesa: mandami il numero |
| 4 | `raw 123,456 -> e4` | ultimo tocco: coordinate grezze e casella |
| 5 | `INT 12  I2C err 0` | fronti contati sul filo INT, errori I2C. INT deve salire a ogni tocco |
| | `INT WIRE MISSING? A0-D9` | i tocchi arrivano ma INT resta a 0: manca il filo `A0`→`D9` |
| | `RST WIRE MISSING? A3-D6` | INT funziona ma il reset no: manca il filo `A3`→`D6` |
| 6 | `part 700ms full 1900ms` | durata totale dell'ultimo refresh parziale e completo |
| 7 | `BUSY 450ms 3/16 to full` | tempo che ci ha messo il pannello da solo; parziali fatti dall'ultimo completo |
| 8 | `gauge 3.91V 76.5%` / `no gauge` | MAX17048 trovato, oppure no (è facoltativo) |
| 9 | `heap ...k psram ...k` | memoria libera |
| 10 | `I2C: 5D` / `I2C: bus empty` / `I2C: SDA/SCL held LOW` | indirizzi trovati nella scansione all'avvio (`5D` touch, `36` MAX17048) |
| 11 | frase di stato | cosa ha capito dell'ultimo tocco |

Regole del firmware, utili per capire cosa vedi: **un refresh per ogni tocco**; dopo 16 parziali fa un refresh completo alla prima pausa di 1,5 s (al più tardi dopo 32); dopo 3 s senza tocchi spegne l'alta tensione del pannello (`PANEL power off after idle`), e il refresh seguente dura circa 0,15 s in più. I tocchi fatti mentre lo schermo si aggiorna vengono buttati.

**Se il touch è morto** si comanda dal monitor seriale: `b` `t` `p` `s` cambiano schermata, `r` refresh parziale, `f` completo, `i` rapporto, `?` aiuto.

**Tappa 0 — XIAO da solo.**
Solo XIAO e cavo USB-C. Non servono i pin saldati. Carica il firmware e apri il monitor.
- Senti un bip? No: il buzzer non c'è ancora. Guarda il seriale.
- Deve comparire `BOOT  flash 16 MB, PSRAM 8192 KB`. Se c'è `WARNING expected 16 MB`, non è un XIAO **Plus**.
- È normale leggere `PANEL FAIL: BUSY pin FLOATS`, `PANEL NOT RESPONDING` e `TOUCH FAIL`: non c'è attaccato niente.

**Tappa 1 — Display.**
A corrente staccata: XIAO nello zoccolo (**USB-C dal lato opposto al flat**, controlla due volte), flat a 24 pin nel connettore con i contatti verso l'alto, linguetta chiusa. Niente touch, niente batteria. Poi USB.
- Sul seriale deve comparire `PANEL BUSY pin is driven HIGH (idle): a panel is connected`.
- Lo schermo fa un lampo nero e disegna la schermata Touch. `TOUCH FAIL no answer` e `SDA+SCL low: 3V3 missing?` qui sono normali: il touch non è collegato.
- Dal monitor premi `p` (schermata **Panel**): sei riquadri `12.5%`, `25%`, `37.5%`, `50%`, `hatch`, `solid`, ognuno con un pezzo bianco e uno nero. Devono essere uniformi e nitidi.
- Il nero pieno e il bianco pieno si provano coi pulsanti `All black` e `All white`, quindi alla tappa 2, quando il touch funziona. Devono essere puliti, senza puntini né "neve". Un tocco qualsiasi riporta ai riquadri.
- Tempi: ogni refresh scrive sul seriale `REFRESH partial #n: ... ms, of which BUSY wait ... ms`. Il numero da confrontare è **BUSY wait**: atteso circa 450 ms il parziale, 1100–1300 ms il completo (più circa 150 ms se il pannello era spento). Il totale è più lungo: c'è il tempo per spedire l'immagine. Annota tutti e due.

**Tappa 2 — Touch.**
A corrente staccata: FTS02 collegato come in `wiring.md` §5, flat del touch in P7 **girato**. Poi USB.
- Riga 2 della colonna: `Touch OK 0x5D id 911`. Sul seriale: `TOUCH address test: asked 0x5D, 0x14, 0x5D -> got 0x5D, 0x14, 0x5D: RST and INT wires proven`. Questa prova dice che i fili RST e INT lavorano davvero.
- **Se c'è `TOUCH FAIL no answer`: stacca subito la USB**, gira il flat, riprova. (Il firmware riprova da solo ogni 2 secondi, ma il flat si gira a corrente staccata.)
- Tocca i bersagli **1, 2, 3, 4 in ordine** (alto-sinistra, alto-destra, basso-sinistra, basso-destra). A ogni tocco compare un mirino dove il firmware crede che tu abbia toccato; un bersaglio centrato diventa nero.
- Dopo il quarto, nel riquadro al centro c'è il verdetto:
  - `axes OK (S0 X0 Y0)`: assi giusti, niente da fare.
  - `SET SWAP=. MIRX=. MIRY=.`: assi scambiati o a specchio. Non sono i fili. Metti quei tre valori in `src/hwtest/config.h` (`TOUCH_SWAP_XY`, `TOUCH_MIRROR_X`, `TOUCH_MIRROR_Y`), ricarica, rifai la prova. Il seriale dice la stessa cosa: `TOUCHTEST panel needs ...`.
  - `unclear: tap 1 2 3 4 again`: tocchi fuori ordine. `Clear` e si rifà.
- Guarda la riga 5: `INT` deve crescere a ogni tocco.
- Poi schermata **Board**: tocca un pezzo (`selected e2`), poi una casella (`moved e2-e4`). Prova le caselle sul bordo (colonna `h`, traversa `1`) e un tocco sulle lettere fuori dalla scacchiera: deve dare `raw ... -> --`, nessuna casella. `Reset` rimette i pezzi.

**Tappa 3 — Suono.**
A corrente staccata: buzzer come in `wiring.md` §6. All'avvio ora si sentono il bip e poi i due bip. Schermata **Sound**: `Beep` (880 Hz), `Scale` (otto note), `Sweep` (da 500 a 4000 Hz: annota dove suona più forte).

**Tappa 4 — Batteria.** Solo col multimetro.
1. Procedura della polarità, `wiring.md` §9, tutta.
2. Interruttore su ON, stacca la USB: la scheda deve **restare accesa**.
3. Interruttore su OFF a USB staccata: si spegne.
4. Per caricare: USB collegata **e** interruttore su ON, una notte (9–10 ore da scarica).

**Tappa 5 — Più avanti, quando arrivano i pezzi di `da-comprare.md`.**
Breakout FPC con le tre resistenze al posto del FTS02 (`wiring.md` §7), poi MAX17048 (§8). La prova è sempre la stessa: riga 2 `Touch OK`, e in più la riga 8 passa da `no gauge` a `gauge 3.9xV xx.x%` e la riga 10 mostra anche `36`.

## D. Se qualcosa non va

| Cosa vedi | Causa più probabile | Cosa controllare |
|---|---|---|
| Il Mac non vede il XIAO | cavo solo ricarica; XIAO non in modalità di caricamento | cambia cavo; BOOT premuto mentre infili il cavo |
| Bip all'avvio, schermo muto, seriale `PANEL FAIL: BUSY pin FLOATS` e poi `PANEL NOT RESPONDING ... BUSY never went active` | il display non è collegato alla scheda | flat a 24 pin: al rovescio, non in fondo o linguetta aperta. Poi: XIAO non spinto fino in fondo, o **girato** |
| Schermo muto, ogni refresh dura 20–30 s, seriale `Busy Timeout!` e `PANEL NOT RESPONDING ... BUSY never released` | il pannello resta "occupato" per sempre | flat a 24 pin messo a metà; interruttore della driver board; XIAO nello zoccolo |
| Immagine spostata, a righe o con pezzi sbagliati | flat del display che fa contatto male | stacca, riapri la linguetta, reinfila il flat dritto e fino in fondo |
| Puntini sparsi o "neve" su `All black` | driver board del lotto difettoso di gennaio 2025 | non è il firmware: foto, codice lotto, e si chiede la sostituzione |
| `BUSY wait` del parziale intorno a **1500 ms** invece di 450 | pannello di un lotto vecchio (forma d'onda diversa in memoria) | non è un guasto: mandami il numero, si sistema nel firmware |
| L'immagine sbiadisce o il XIAO si riavvia durante un refresh; al riavvio il seriale dice `BROWNOUT` | 3,3 V che cede | cavo USB migliore o altra porta; a batteria: cella scarica |
| `TOUCH FAIL no answer` + `no answer: flat? RST wire?` | flat del touch nel verso sbagliato (caso più probabile) | **stacca subito**, gira il flat. Poi: presa sbagliata (P6 invece di P7), linguetta aperta, filo `A3`→`D6` |
| `TOUCH FAIL no answer` + `SDA+SCL low: 3V3 missing?` | al touch non arrivano i 3,3 V | `3.3V` e `GND` di P11 (non `5V`!) |
| `TOUCH FAIL no answer` + `SDA low` oppure `SCL low` | manca un filo dell'I2C | `A4`→`D4` (SDA), `A5`→`D5` (SCL); non i pin `SDA`/`SCL` di P8; saldature di CN1/CN2 opache o a pallina: ripassale |
| `TOUCH WARN` + `swap the A4/A5 wires` | SDA e SCL scambiati | scambia i due fili |
| `TOUCH WARN 0x14` + `0x14: INT or RST wire bad` | il firmware non riesce a comandare INT o RST | tocca lo schermo 3–4 volte: la riga 5 dice quale dei due |
| `TOUCH WARN` + `addr stuck: RST/INT wire?` | come sopra | come sopra |
| Riga 5: `INT WIRE MISSING? A0-D9` | il filo INT non arriva | filo `A0`→`D9` |
| Riga 5: `RST WIRE MISSING? A3-D6` | il filo RST non arriva | filo `A3`→`D6` |
| Il mirino compare lontano dal dito, o dal lato opposto | assi del touch, non i fili | fai la prova dei bersagli 1–4 e metti in `config.h` i valori di `SET SWAP=.. MIRX=.. MIRY=..` |
| Seriale: `TOUCH rejected raw=(...)` | coordinate fuori dal pannello | mandami la riga; guarda anche `cfg ...x...` in riga 3 |
| `TOUCH FAIL I2C errors`, poi torna da solo | filo o flat che balla | reinfila il flat del touch e i Dupont |
| Nessun suono, nemmeno il bip all'avvio | filo del segnale o massa | `S`→`D7`, `−`→`GND` |
| A batteria non si accende | interruttore su OFF; cella scarica; spina non in fondo | se la polarità **non** l'avevi misurata: fermati e misurala adesso |
| Tappa 5: resta `no gauge`, seriale `GAUGE no MAX17048 at 0x36 (optional)` | il MAX17048 non risponde | SDA/SCL scambiati; fili ai pad `+`/`−`; batteria collegata? |

## E. Numeri da mandare a Claude

Copia e incolla questa lista con le risposte. La cosa più utile: **tutto il log seriale** dall'avvio (premi `i` alla fine per il rapporto) e una foto dello schermo.

1. Driver board: codice lotto e data sull'etichetta.
2. XIAO: con o senza pin saldati.
3. Flat display: contatti in su o in giù (pannello a faccia in su); sigla stampata sul flat; etichetta dietro al pannello.
4. Flat touch: contatti in su o in giù; in quale posizione ha funzionato dentro P7 (pannello a faccia in su e FTS02 capovolto, oppure l'altra).
5. GT911, dal seriale: le righe `TOUCH OK at ...`, `TOUCH address test ...`, `TOUCH id ...` (product id, firmware, versione della configurazione, risoluzione) e `TOUCH Module_Switch1 ...`.
6. Prova dei bersagli: il verdetto (`axes OK ...` oppure `SET SWAP=...`) e le quattro righe `TOUCH DOWN raw=(...)` dei quattro angoli.
7. Tempi di refresh, parziale e completo: totale e `BUSY wait`, in millisecondi (righe `REFRESH ...`).
8. `All black` e `All white`: puliti o con difetti? Foto.
9. Caselle scure della scacchiera di prova: si distinguono bene dalle chiare? Dopo una decina di refresh parziali, quanto "fantasma" resta? Foto.
10. `Sweep`: a che punto il buzzer suona più forte.
11. Tabella delle misure (sezione B) compilata.
12. Col multimetro: tensione della batteria all'arrivo; polarità della spina com'era (giusta o invertita).
13. Col multimetro, in ohm: resistenza del KY-006 tra `S` e `−` (circuito aperto = piezo; circa 16 Ω = magnetico).
14. Col multimetro, a tutto spento e con i Dupont staccati, sul FTS02 con il flat del touch inserito: ohm tra `A4` e `3.3V`, tra `A5` e `3.3V`, tra `A3` e `3.3V`. Serve a capire se il flat ha già le sue resistenze di pull-up: il FTS02 da solo dà circa 10 kΩ sulle prime due e circuito aperto sulla terza; valori più bassi vogliono dire che il flat ne ha di sue.
15. Più avanti, col multimetro in serie alla batteria (portata µA/mA): consumo con la scheda "addormentata".
