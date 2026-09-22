# Ricerca del 21 settembre 2026

Otto dossier scritti prima che arrivasse l'hardware. Niente è stato provato su un dispositivo: tutto viene da datasheet, schemi, sorgenti delle librerie e documentazione ufficiale. I punti più importanti sono stati ricontrollati una seconda volta sulle stesse fonti.

Le decisioni prese dopo stanno in [`../decisioni.md`](../decisioni.md): comanda quel file. Qui c'è il perché.

## In breve

**Confermato**
- I sei pin del display corrispondono allo schema Seeed, e la driver board pilota il GDEY075T7 senza regolazioni. D4, D5, D6, D7 e D9 sono liberi.
- Il touch è un GT911, gli bastano 3,3 V, il suo flat entra nella presa giusta dell'FTS02.
- GxEPD2 ha già la classe per questo pannello e tiene l'immagine intera in memoria (48 kB).
- CT-800 resta il motore giusto. Lichess sul XIAO è fattibile.
- GxEPD2 è GPL-3.0: il firmware è GPL già da oggi, non solo per via del motore.

**Cambiato nel piano**
- **Pin.** INT del touch da GPIO43 a **GPIO8 (D9)**: solo i GPIO 0–21 svegliano l'ESP32-S3 dal deep sleep, e GPIO43 è la seriale di avvio. RST del touch su D6, buzzer su D7.
- **Caselle da 60 a 56 px**, tutto a multipli di 8: con 60 ogni mossa sbiancava 4 colonne della casella accanto.
- **Un solo refresh per evento.** Il vecchio firmware ne faceva 3–4 per mossa, circa 1,3 s.
- **Pannello spento quando non serve**; refresh completo contato sui refresh, non sulle mosse.
- **Batteria.** Nessuna scheda misura la cella: serve un MAX17048 sul bus I2C del touch.
- **Cablaggio FTS02 riscritto**: le etichette del vecchio `wiring.md` sulla scheda non esistono.
- **Scocca.** Driver board più XIAO: 12–15 mm, non 8. Da qui il "mento". L'FTS02 resta sul banco.
- **Funzioni.** Entrano due giocatori, puzzle offline, sfida a un amico, login a Lichess con QR.
- **Nome e rilascio.** Nome proprio; scocca Exclusive su MakerWorld, quindi fuori da questo repo.

**Da scoprire con l'hardware**
- Quanto dura davvero un refresh parziale sul nostro pannello.
- Il verso dell'asse Y del touch e come va infilato il flat "girato".
- La polarità del connettore batteria: multimetro, prima di collegare.
- Il lotto della driver board: quelli di gennaio 2025 davano pixel sporchi.
- Il consumo a riposo attraverso il convertitore della driver board.
- Altezze e fori di fissaggio delle schede: nessun disegno pubblicato, si misura col calibro.

## Il prodotto commerciale e MakerWorld
Dossier: [atlas-makerworld.json](atlas-makerworld.json)

Atlas è una campagna Kickstarter (16 settembre – 16 ottobre 2026, consegna stimata marzo 2027): ESP32-C5, e-paper 7,5" 800×480, spessore 10 mm, CT-800, solo Lichess, uso verticale.

- Al nostro piano mancavano: due giocatori, sfida a un amico e inviti, partita contro l'IA di Lichess, puzzle offline, ritiro mossa, lezioni, analisi. Le prime quattro sono entrate.
- **Nome.** Rischio alto ed evitabile: usare "Atlas" o presentarsi come copia. Nome proprio e una frase di non affiliazione.
- **MakerWorld Exclusive** vieta di pubblicare i file 3D altrove, GitHub compreso, per almeno 90 giorni. Firmware e documenti non sono "il modello": repo GPL e link dalla descrizione vanno bene.
- **Flasher dal browser**: funziona con l'S3, ma con la scheda sbagliata in `platformio.ini` il binario porta scritto "8 MB" e non parte. Da USB non si nota.

Ignoto: se "Atlas" è un marchio registrato; se il flasher regge sulla USB nativa dell'S3 sotto Windows.

## Revisione del firmware di fase 1
Dossier: [code-review.json](code-review.json)

Primo firmware letto riga per riga, senza compilarlo. Si sarebbe acceso e avrebbe disegnato: registri del GT911 giusti, a1 scura, posizione iniziale giusta.

- Problemi veri: strisce bianche per l'allineamento a 8 px; bordo cancellato sulle caselle esterne; quattro refresh per mossa; tocchi fuori scacchiera mai scritti nel log; nessun ripiego sull'indirizzo 0x14; rotazioni 1 e 3 sbagliate; pannello mai spento.
- Minore: retino delle caselle scure al 12,5 %, forse invisibile sul pannello.
- Proposta accolta: il firmware di collaudo deve **diagnosticare da solo** fili e assi del touch.

Ignoto: la configurazione di fabbrica del GT911.

## Il motore CT-800
Dossier: [ct800.json](ct800.json)

Lo zip dei sorgenti **non è stato scaricato**: serve il consenso di Fabrizio. Le fonti sono sito, manuale e post dell'autore.

- GPL-3.0-or-later, versione 1.46 del 29 agosto 2024, solo come zip su ct800.net.
- Circa 2100–2200 Elo su un Cortex-M4 a 168 MHz. 33 000 righe di C, memoria statica, libro di aperture compilato dentro.
- Si porta la **versione UCI**, come scatola nera in un task suo con stack grande. Solo lei ha i livelli in Elo, con un minimo intorno a 1000: i livelli più facili li costruiamo noi.
- Le regole non si prendono dal motore: una **libreria nostra** serve a interfaccia, Lichess e puzzle.
- Molti set di pezzi di Lichess non sono compatibili con la GPL. Scelta: set disegnato da Fabrizio.

Ignoto: l'interno dei sorgenti, le opzioni UCI esatte, velocità e stack sull'ESP32-S3.

## La driver board Seeed
Dossier: [driver-board.json](driver-board.json)

- **Niente pin maschi**: i segnali liberi escono su due file di 7 fori (CN1/CN2) senza header. Si salda. Un solo foro 3V3 e un solo GND.
- **Alimentazione.** Cella, interruttore, ETA9740 (carica e boost a 5 V), pin 5V del XIAO. Con la USB inserita il XIAO è acceso comunque. La carica funziona solo con l'interruttore su ON, a 0,5 A: 9–10 ore per 4000 mAh. Nessun LED di carica.
- **Nessuna protezione dalla batteria al contrario.** Il "+" sembra seguire la convenzione Adafruit, ma è dedotto da una foto.
- La USB-C esce dal lato corto opposto al flat del display.

Ignoto: se il boost si spegne da solo a carico leggero; da che lato ha i contatti il flat a 24 pin.

## Touch e adattatore FTS02
Dossier: [fts02-touch.json](fts02-touch.json)

- Sull'FTS02 i segnali stanno sull'header P10: **A0 = INT, A3 = RST, A4 = SDA, A5 = SCL**. Alimentazione da P11: 3.3V e GND. I pin "SDA" e "SCL" di P8 sono altro. Il pin 5V accanto ai 3.3V: mai.
- Good Display scrive che il flat del 7,5" va inserito **girato**.
- L'FTS02 è alta 13–14 mm. Il suo percorso touch è passivo: nella scocca basta un **breakout FPC a 6 pin passo 0,5 mm** con due pull-up da 4,7 kΩ.
- Indirizzo 0x5D. La configurazione del GT911 è scritta in fabbrica: il firmware non la tocca mai. I2C a 100 kHz: a 400 con quei fili è al limite.
- Coordinate 800×480 nel verso del display. X verificata, Y no.

Ignoto: lato dei contatti del flat e della presa; se il flat ha già i suoi pull-up.

## La libreria del display
Dossier: [gxepd2.json](gxepd2.json)

- Su questo pannello ogni refresh "parziale" aggiorna **tutto lo schermo** e dura uguale qualunque sia la finestra: circa 435 ms per l'autore, 0,3 s per Good Display. Conviene disegnare tutto e spingere una volta.
- Dopo un parziale il booster resta acceso. Spegnerlo costa circa 40 ms, riaccenderlo 130.
- Il refresh "completo" qui è quello rapido a un lampo (circa 1,2 s). Good Display ne consiglia uno ogni 5 parziali; un progetto simile (*crosscheck*) l'ha tolto perché sembrava un lampo a caso. Scelta: circa ogni 16, nelle pause.
- BUSY scollegato non blocca: 10 s di attesa e "Busy Timeout!". BUSY sempre alto finge di riuscire senza mostrare nulla.
- Niente grigi: lampeggiano a ogni aggiornamento. Per i pezzi **due bitmap a 1 bit**, maschera bianca e inchiostro nero.

Ignoto: il tempo del parziale sul nostro lotto; il comportamento al freddo.

## Lichess
Dossier: [lichess.json](lichess.json)

- Tre vincoli: ogni connessione cifrata prende circa 42 kB di RAM interna, quindi **due alla volta**; il core Arduino 3.3.8 ha un difetto che rompe le riconnessioni (corretto dalla 3.3.9); i flussi in tempo reale vanno chiesti in HTTP/1.0.
- Ricerca di avversario solo da Rapid in su (tempo + 40 × incremento ≥ 480 s). Contro l'IA o un amico va bene anche il Blitz.
- **Login con QR**: Lichess accetta il ritorno OAuth su un indirizzo della rete di casa. Il token incollato resta come riserva.
- Puzzle offline dal database CSV in CC0, non dall'API.

Ignoto: la RAM interna libera con display, WiFi e motore insieme; il login dal browser del telefono.

## XIAO ESP32-S3 Plus
Dossier: [xiao-s3-plus.json](xiao-s3-plus.json)

- Su GPIO43 a ogni reset esce il messaggio di avvio: l'INT del touch lì avrebbe litigato col chip.
- I pin in più della Plus sono mezzi fori a passo 1,27 mm: ci va un filo, non un header.
- I consumi in sleep pubblicati (14–33 µA) valgono dai pad batteria. Qui si entra dal pin 5V attraverso il boost: si misura.
- In `platformio.ini` servono la scheda **Plus** e la piattaforma pioarduino fissata per versione.

Ignoto: consumo reale a riposo.
