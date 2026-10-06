# Arrocco — decisioni di progetto

Aggiornato il 6 ottobre 2026. Questo file è la fonte di verità: se il codice o un altro documento lo contraddice, ha ragione questo file (oppure va aggiornato qui per primo).

Nome pubblico: **Arrocco — e-ink chess**, by CollectiCraft. Mai "Atlas" né "replica" in nomi, immagini o descrizioni: al massimo "an open alternative to commercial e-ink chessboards".

## Chi fa cosa
- Fabrizio: compra, salda, assembla, disegna la scocca e il set di pezzi.
- Claude: firmware, simulatore, documentazione tecnica. Lavora in autonomia nel repo e fa i commit da solo. Niente push su GitHub senza chiedere.

## Vincoli di Fabrizio
- Saldatore sì, poca pratica: solo saldature passanti (header, fili nei fori). Niente pad minuscoli sotto il XIAO.
- XIAO ESP32-S3 Plus con header già saldati.
- Strumenti sul banco: solo calibro. Il firmware di collaudo deve quindi diagnosticare da solo e dirlo sullo schermo. Un multimetro economico va comprato (polarità batteria).
- Download autorizzati sul Mac: toolchain e librerie PlatformIO; il sorgente di CT800 da ct800.net (fatto il 22/09/2026, importato non modificato in `lib/ct800/upstream/`); il database dei puzzle Lichess da database.lichess.org (fatto il 22/09/2026, resta fuori dal repo: si rigenera col comando in `docs/puzzles.md`).

## Hardware
- Pannello Good Display GDEY075T7-T01 (800×480, UC8179, touch GT911), Seeed ePaper Driver Board 114993558, XIAO ESP32-S3 Plus, LiPo 606090 4000 mAh, buzzer KY-006.
- Uso **orizzontale da tavolo**: scacchiera a sinistra, colonna laterale a destra.
- Scocca **sottile con un "mento"**: l'elettronica (12–15 mm con il XIAO montato) sta in una fascia accanto al display, dietro il pannello solo la batteria da 6 mm. La USB-C esce dal bordo del mento.
- Risveglio **col tocco**; l'interruttore della driver board è lo spento vero. Come funziona il sonno: sezione "Batteria" qui sotto.
- Percentuale batteria: **MAX17048** I2C (0x36) sullo stesso bus del touch, celle ai pad BAT +/− sul retro della driver board.
- FTS02 solo al banco. Nella scocca: breakout FPC 6 pin passo 0,5 mm + due pull-up da 4,7 kΩ su SDA e SCL (+ 10 kΩ su RST consigliato).
- Sulla driver board vanno saldati due header 1×7 nei fori CN1/CN2 (meglio a 90° o fili diretti, per lo spessore).

### Pin map definitiva (GPIO ESP32-S3, tra parentesi il nome XIAO)
| Funzione | GPIO | XIAO |
|---|---|---|
| EPD RST | 1 | D0 |
| EPD CS | 2 | D1 |
| EPD BUSY | 3 | D2 |
| EPD DC | 4 | D3 |
| I2C SDA (touch + MAX17048) | 5 | D4 |
| I2C SCL (touch + MAX17048) | 6 | D5 |
| Touch RST | 43 | D6 |
| Buzzer | 44 | D7 |
| EPD SCK | 7 | D8 |
| Touch INT (sveglia dal deep sleep) | 8 | D9 |
| EPD MOSI | 9 | D10 |

`SPI.begin(7, -1, 9, -1)`: il display non ha MISO e GPIO8 è del touch. I2C a 100 kHz finché non ci sono pull-up da 4,7 kΩ e fili corti.

Collegamento FTS02 (pannello 7,5" nella presa TP-FPC2/P7, flat inserito **girato**): header P10 `A0`=INT, `A3`=RST, `A4`=SDA, `A5`=SCL; header P11 `3.3V` e `GND`. Mai il pin `5V` di P11, mai i pin serigrafati `SDA`/`SCL` di P8.

## Batteria (firmware `arrocco` 0.2)
- **Deep sleep dopo 5 minuti** senza tocchi né comandi seriali (`cfg::kSleepAfterIdleMs` in `src/common/config.h`), **solo se non gira niente**: motore che pensa, orologio di partita che corre, rete accesa, dito sul vetro. Con l'orologio fermo o senza orologio una partita in corso può dormire: salvarla e riproporla al risveglio è compito dell'app, non del sonno.
- Prima di dormire: **una schermata del sonno**, poi pannello in hibernate.
  - **Con una partita sullo schermo** resta la scacchiera, con la nota "Asleep. Tap to wake." sopra la colonna laterale (refresh parziale): l'e-ink la tiene senza consumare, e si può pensare alla mossa a scacchiera spenta. Il tocco che la sveglia riporta **dritti alla partita**, senza passare dal menu (deciso da Fabrizio il 06/10/2026).
  - **Altrimenti** il quadro `lib/arrocco/src/arrocco/ui/sleep_art.h` con l'etichetta "Tap to wake" in basso a destra (refresh completo); senza quadro nel build, la stessa nota.
- Nel sonno il **GT911 resta acceso** e scansiona (dopo pochi secondi in green mode, ~3,3 mA da datasheet: è lui il consumo maggiore). RST tenuto alto (GPIO43 è un pad digitale: `gpio_hold_en` + `gpio_deep_sleep_hold_en`), INT con la resistenza verso l'alto del dominio RTC, sveglia **ext0 su INT basso**. Il log di avvio della ROM viene spento prima del sonno: passerebbe su GPIO43, cioè su RST.
- **Il risveglio è un riavvio**, senza i 2 s di attesa del monitor seriale e **senza reset del GT911**: il chip non si è mai spento, e un reset con il dito ancora sul vetro gli farebbe ricalibrare la base col dito sopra. Il tocco che sveglia non vale come tocco sulla schermata nuova.
- Un tocco mentre la schermata del sonno si disegna sveglia la scheda subito (il rapporto non letto tiene INT in movimento).
- **WiFi spento di default.** La radio si accende solo quando un lavoro di rete la chiede (richieste, stream, login OAuth, `wifi-on`) e si spegne 2 minuti dopo l'ultimo. Il portale di configurazione si apre **solo a richiesta** (dalla schermata WiFi di Lichess, o da seriale con `wifi-portal` e `wifi-forget`) e si chiude da solo dopo 5 minuti senza pagine richieste, o subito con Back. La porta 80 è aperta solo col portale o mentre un login OAuth torna indietro, e il login si abbandona dopo 10 minuti. Niente più portale riaperto da solo dopo quattro tentativi falliti. Dettagli: `rete.md`.

## Touch
- La configurazione di fabbrica del GT911 (v65, `Module_Switch1 0x3D`) resta quella: **il firmware non la scrive mai da solo**. Il problema "devo premere forte" si prova a risolvere abbassando `Screen_Touch_Level`/`Screen_Leave_Level` **a mano, dal monitor seriale** (`touch-cfg`, `touch-ladder`, `touch-level … yes`, `touch-restore yes`): il chip conserva quello che riceve nella sua flash, quindi prima della prima scrittura il blocco di fabbrica viene copiato in NVS e stampato. Mai sotto la metà del valore di fabbrica.
- Un tocco fatto **mentre lo schermo si aggiorna** si scarta, tranne quando la scacchiera sotto il dito è rimasta quella che l'utente vedeva: ridisegno dell'orologio (o della batteria) e segni di selezione dopo un tocco su una casella ("pezzo, poi destinazione"). Allora il tocco arriva all'app appena il refresh finisce. Deve sembrare un dito: almeno due rapporti, fermo, un dito solo (il refresh dell'e-paper fa rumore proprio sotto il touch). Regole in `src/app/touch_policy.h`.
- In una partita con l'orologio, i refresh dell'app (orologio, mossa del motore) aspettano 0,7 s dopo un tocco, al massimo 1,5 s: così non cadono sul secondo tocco.
- Il GT911 si legge quando INT si muove (ogni rapporto lo fa pulsare), più una lettura ogni 100 ms di scorta: niente più 100 letture al secondo a vetro vuoto.

## Schermo e interfaccia
- Caselle da **56 px** con coordinate a–h e 1–8: scacchiera 448 px a partire da x=16, y=16; colonna laterale da x=480 a 799. Tutto allineato a multipli di 8.
- **Un solo refresh per evento** visibile all'utente, disegnando nel buffer intero e spingendo una volta sola.
- **Pochi lampi, un po' di ghosting accettato**: refresh completo solo ogni ~16 refresh parziali e solo nelle pause naturali; costante regolabile.
- Pannello spento (powerOff) dopo qualche secondo di inattività, hibernate prima del deep sleep.
- Primo tocco su un pezzo: **cornice + pallini sulle mosse legali**, in un refresh.
- Pezzi: **set CollectiCraft disegnato da Fabrizio** (1 bit, sagoma + maschera bianca). Fino ad allora un set provvisorio disegnato dal firmware.
- Interfaccia **solo in inglese**, con tutte le stringhe in un file unico.
- Nel piede del menu la percentuale della batteria c'è solo se c'è il misuratore (MAX17048): senza, il menu della batteria **non dice niente**.
- **In zeitnot niente refresh completo**: se a chi deve muovere resta meno di un minuto, la mossa resta un refresh parziale anche quando toccherebbe il completo (1,2–1,8 s che si mangerebbero il suo tempo). Il fantasma aspetta.

## Gioco
- Offline per tutti, dai bambini al circolo: livelli con nomi sopra l'Elo di CT-800, più livelli facili sotto i 1000.
- Nella prima versione giocabile: partita contro il motore, **due giocatori sulla stessa scacchiera**, **orologio**, **puzzle offline** (pacchetto Lichess CC0 in flash). Ritiro mossa e suggerimento dopo.
- Ordine di costruzione: prima la libreria di regole (serve a tutto), poi interfaccia e due giocatori, poi CT-800 in un task suo che parla UCI, poi Lichess.
- **La partita sopravvive allo spegnimento**: batteria finita, interruttore, cavo staccato, e il risveglio dal deep sleep, che riavvia il chip. Dopo ogni refresh che cambia la partita (mossa, ritiro, partita nuova, fine, Flip, menu con l'orologio in corsa) il firmware la riscrive in NVS, namespace `arrocco-game`: posizione di partenza e mosse a 2 byte l'una (27 byte più 2 a semimossa, al massimo circa 2,2 KB), modo, livello, colori, orientamento, orologio e tempi rimasti all'ultima mossa, risultato dichiarato. Formato con magic, versione e CRC (`lib/arrocco/src/arrocco/ui/saved_game.h`): un blob rovinato o di un'altra versione si ignora. Si scrive **dopo** il refresh, mai fra il tocco e il refresh; selezioni, ripaint dell'orologio e schermate di scelta (tipo di partita, livello, orologio, impostazioni) non scrivono niente. Usura stimata: qualche cancellazione di settore NVS a partita, decine di migliaia di partite.
- All'accensione una partita **non finita** è il primo pulsante del menu: "Resume game", e sotto "vs engine · move 12" oppure "two players · move 12". Il menu dell'avvio resta il solito Deep; "Resume game" porta alla scacchiera con un Full, come dal menu durante il gioco. Una partita finita non si offre; una partita contro il motore nemmeno, se all'avvio il motore non è partito (resta salvata per l'avvio dopo). Una partita nuova sostituisce quella salvata.
- Alla ripresa gli orologi ripartono dai tempi dell'**ultima mossa**: il tempo fra l'ultima mossa e lo spegnimento non viene addebitato, perché la scacchiera non sa quanto è rimasta spenta. L'orologio di chi muove riparte solo al tocco su "Resume game". Il menu è una pausa allo stesso modo: l'orologio si ferma quando ci si va e riparte con "Resume game". Se toccava al motore, ci ripensa da capo.
- Prima di buttare via una partita non finita con almeno una mossa ("New game" durante la partita, oppure un orologio scelto dal menu mentre c'è "Resume game") la scacchiera chiede "Start a new game?". Senza mosse non chiede niente.
- Contro il motore si abbandona solo col proprio colore, anche mentre il motore pensa, e la patta d'accordo non c'è: il motore non accetta niente. Fra due giocatori abbandona chi ha la mossa, o si accordano per la patta.
- I puzzle non si riprendono: un puzzle dura poche mosse. Quando arriveranno nel menu, al massimo si terrà a che puzzle si era arrivati, con la stessa API e una chiave sua. Anche le impostazioni (suono, Flip, refresh) per ora non si salvano.
- Lichess: login con **QR (OAuth PKCE)** e token incollato come riserva; modi della prima versione: **contro lo Stockfish di Lichess** e **sfida un amico / accetta inviti**. Partite **amichevoli di default**, rated a scelta da menu. Com'è fatto (dettagli in `rete.md`, "Le schermate Lichess"):
  - la voce "Lichess" porta dove la scheda è arrivata: senza rete salvata alla schermata del WiFi (nome della rete Arrocco-XXXX e il suo **QR `WIFI:`**, portale aperto, chiuso da Back), con la rete ma senza account al **QR del login**, con l'account al menu di Lichess. Errori detti a parole sulla schermata (password sbagliata, token rifiutato, attesa per il 429);
  - il QR del login porta all'indirizzo corto della scacchiera, `http://<ip>/login`, che rimanda a lichess.org: codice da 29 x 29 moduli da 2,4 mm invece di 65 x 65 da 1,2 mm, e un indirizzo che si scrive a mano. Il token incollato da seriale (`token-set`) è la riserva, scritta in piccolo;
  - menu di Lichess: *Play the computer*, *Challenge a friend*, *Invitations*, *Rated games* (off a ogni accensione: vale solo per gli amici, contro il computer Lichess non fa partite rated), *Unlink account*, *Back*. Orologi offerti: 5+0, **10+0** (predefinito), 15+10, 30+0, gli stessi della partita offline e tutti accettati dalla Board API (niente bullet);
  - il nome dell'amico: fra i **quattro ultimi avversari** (in flash) o scritto su una tastiera a schermo con i soli caratteri di un nome Lichess (minuscole, cifre, `-`, `_`), tasti da 14 x 11 mm. La sfida resta viva finché l'amico risponde (`keepAliveStream`), non 20 secondi;
  - nella partita online la scacchiera **non gioca la mossa toccata**: la mostra come mandata e sposta il pezzo quando Lichess la conferma. Una mossa non si perde e non si ripete (vedi `rete.md`). *Menu* lascia la partita in corso e il menu di Lichess offre *Back to the game*; a fine partita *New game* ne fa un'altra uguale (computer, o la stessa persona). La partita online ha la sua scacchiera in memoria: quella offline salvata e il suo "Resume game" non si toccano;
  - una partita che comincia senza che la scacchiera l'abbia chiesta (dal telefono, o quella in corso alla riaccensione) prende lo schermo da sola, ma solo se è in tempo reale e la Board API la gioca: le partite per corrispondenza no;
  - fuori partita, dopo **5 minuti senza tocchi** le schermate di Lichess tornano al menu principale e chiudono la sessione, così la radio si spegne e la scacchiera può dormire. Durante una partita no.

## Rilascio
- Scocca su MakerWorld come **Exclusive**: i file della scocca (STL, 3MF, STEP) **non** entrano in questo repo né altrove. Qui stanno solo firmware, misure e cablaggio.
- Niente vendita: solo file gratuiti.
- Firmware GPL-3.0-or-later su GitHub (account `collecticraft-sudo`), **flasher dal browser** su GitHub Pages, binari costruiti dalla CI a ogni versione taggata. Mai secure boot né flash encryption.
- Simulatore del display sul Mac, da provare prima che arrivi l'hardware.
