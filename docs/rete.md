<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Rete

Come la scacchiera si collega al WiFi e a Lichess. Il codice di rete sta in
`src/app/net_wifi.*`, `net_http.*`, `net_token.*`, `net_console.*`; le schermate in
`lib/arrocco/src/arrocco/ui/lichess_*` (le stesse nel firmware e nel simulatore), e
`src/app/lichess_link.*` le collega alla rete della scheda.

> **Stato di oggi (6/10/2026).** La voce "Lichess" del menu porta alle schermate: rete,
> collegamento dell'account col QR, il menu di Lichess, la partita online. Sono provate nel
> simulatore contro un Lichess finto (`--fake-lichess`, piu' sotto) e compilate per la scheda,
> ma **sulla scheda vera non sono ancora girate**, e con l'account vero nemmeno: la prima
> prova dal vivo e' quella di "Provarlo sulla scheda", in fondo. Il monitor seriale resta
> utile per vedere cosa succede (`net`, `wifi-status`, `oauth-status`).

## La radio e' spenta finche' non serve

Una scacchiera a batteria con la radio sempre accesa si scarica in pochi giorni. Quindi,
dalla versione 0.2 del firmware:

- **all'accensione la radio e' spenta**, che una rete sia salvata o no;
- si accende da sola quando **un lavoro di rete** la chiede: una richiesta a Lichess, uno
  stream, un login OAuth, il comando `wifi-on`. La scacchiera si aggancia alla rete
  salvata, fa il lavoro e **spegne la radio 2 minuti dopo l'ultimo** (il margine serve a
  non riagganciarsi per ogni comando quando se ne mandano diversi di fila). Uno stream
  aperto tiene la radio accesa finche' resta aperto;
- il **portale** di configurazione si apre **solo quando lo si chiede**: dalla schermata
  "Connect to Wi-Fi" di Lichess, o da seriale con `wifi-portal` e `wifi-forget`; si chiude da
  solo dopo **5 minuti senza che il telefono chieda una pagina**, e subito quando si esce
  dalla schermata con Back;
- con la radio accesa la scacchiera non va in deep sleep: aspetta che si spenga.

Per i lavori fatti dal monitor seriale non cambia niente nell'uso: `li-whoami` scrive
`NET   queued; the radio comes up first` e la risposta arriva dopo l'aggancio (fino a una
ventina di secondi). `oauth-start` a radio spenta aggancia prima la rete e poi stampa
l'indirizzo.

## Il portale

Quando la scacchiera non conosce ancora nessuna rete e si tocca "Lichess", si apre il
portale e la schermata "Connect to Wi-Fi" dice cosa fare (da seriale: `wifi-portal`):

1. compare una rete WiFi aperta che si chiama **Arrocco-XXXX** (le ultime due cifre sono
   quelle del MAC della scheda, cosi due scacchiere vicine non si confondono). Il nome e' scritto
   sullo schermo, e accanto c'e' un **QR** che lo contiene (`WIFI:T:nopass;S:Arrocco-XXXX;;`):
   la fotocamera del telefono lo riconosce e propone di collegarsi, senza cercare la rete a mano;
2. ci si collega col telefono. Il telefono apre da solo la pagina di configurazione
   (captive portal); se non lo fa, basta aprire il browser su **http://4.3.2.1** (anche questo
   e' scritto sullo schermo);
3. la pagina mostra le reti trovate: si sceglie la propria, si scrive la password e si
   preme **Save and connect**. C'e' anche un campo per scrivere a mano il nome di una
   rete nascosta;
4. la rete Arrocco-XXXX si chiude e la scacchiera si collega alla rete di casa; la schermata
   passa da sola a "Connecting to Wi-Fi" e poi al collegamento dell'account. Se la rete non
   va (password sbagliata, rete fuori portata) lo dice la schermata stessa, con il motivo
   (`wrong password (Casa)`) e i pulsanti *Try again* e *Set up Wi-Fi*. Sul monitor seriale
   c'e' lo stesso, piu' dettagliato (`wifi-status`).

La pagina del portale e' fatta tutta di testo scritto dentro il firmware: nessuna
immagine, nessun carattere scaricato, nessun indirizzo esterno. Deve funzionare su un
telefono che in quel momento **non ha internet**, quindi qualunque cosa da scaricare non
arriverebbe mai.

Quanto resta aperto il portale: **5 minuti dall'apertura o dall'ultima pagina chiesta dal
telefono**, poi si chiude e la radio si spegne (oppure, se nel frattempo un lavoro di rete
aspetta, la scacchiera prova la rete salvata). Compilare il modulo prende un minuto; un
portale aperto che nessuno usa e' solo un access point aperto in piu' e una batteria che
cala. Il portale non si apre piu' da solo quando la rete di casa non risponde: chi aspetta
la rete riprova, con pause sempre piu' lunghe (3, 6, 12, 24, 30 s), finche' gli serve.

Mentre il portale e' aperto la scacchiera rifa' la scansione delle reti ogni mezzo minuto,
ma **solo se al portale non e' collegato nessuno**: una scansione fa saltare via per un
paio di secondi il telefono che sta scrivendo la password. Se la lista serve aggiornata
c'e' il link *Scan again* in fondo alla pagina.

### Una cosa che la partita la blocca davvero

Finche' il portale e' aperto, oppure finche' un login a Lichess sta tornando indietro, la
scacchiera deve rispondere sulla porta 80. Il server HTTP di Arduino legge la richiesta
in modo bloccante, con cinque secondi di pazienza: se qualcuno apre una connessione e poi
si zittisce a meta' richiesta, il programma principale — partita, orologio e tocco
compresi — resta fermo fino a cinque secondi. Non e' un guasto e non fa ripartire la
scheda, ma si vede.

Per questo la porta 80 **esiste solo in quelle due finestre**: il server apre il suo
socket quando si apre il portale o parte un login, e lo chiude quando finiscono. Il login
che aspetta il telefono si abbandona da solo dopo **10 minuti** (`OAUTH failed: the phone
did not come back in time`). Quando la scacchiera e' semplicemente online e si sta
giocando, su quella porta non ascolta nessuno e il problema non esiste. Il resto del lavoro
di rete (Lichess, TLS, gli stream) sta su task suoi e non ferma mai la partita, nemmeno per
un millisecondo.

## Dove finiscono le cose

Niente di tutto questo sta nel firmware o in un file: e' tutto in NVS, la memoria di
configurazione della scheda.

| Cosa | Dove | Note |
|---|---|---|
| Nome della rete e password | NVS, namespace `arrocco-wifi` | la password non finisce mai nel log |
| Token Lichess | NVS, namespace `arrocco-lich` | non viene mai stampato, ne' messo in un URL |
| Gli ultimi quattro avversari su Lichess | NVS, namespace `arrocco-game`, chiave `li-friends` | solo i nomi; scritti quando la lista cambia |

Si scrive in NVS **solo** quando l'utente salva qualcosa: a ogni accensione non si scrive
niente, e la copia che il driver WiFi terrebbe per conto suo e' disattivata. La memoria
flash non si consuma stando accesa.

Il token viaggia solo nell'header `Authorization`, mai nell'indirizzo. Nei log si vede
al massimo "presente / assente" e quanti caratteri e'.

## Il login a Lichess

Due strade, come da `decisioni.md`:

- **QR (OAuth2 PKCE)**, la schermata "Link Lichess": la scacchiera prepara un login, mostra un
  QR, il telefono lo apre, si entra su Lichess e si tocca *Authorize*; Lichess rimanda il
  telefono **alla scacchiera stessa** (`http://<indirizzo-della-scacchiera>/oauth/callback`),
  che si scambia il codice con un token e lo salva. La schermata passa da sola al menu di
  Lichess. Serve che telefono e scacchiera siano sulla **stessa rete di casa** (lo dice il
  primo passo scritto sullo schermo).

  Il QR **non contiene l'indirizzo lungo di lichess.org** (circa 270 caratteri: un codice da
  65 x 65 moduli, 6 pixel l'uno, 1,2 mm), ma quello corto della scacchiera,
  `http://<ip>/login`, che risponde con un rinvio (302) all'indirizzo lungo. Il codice viene
  di 29 x 29 moduli da 12 pixel, cioe' 2,4 mm l'uno: il doppio, molto piu' facile da leggere
  da un telefono su un e-paper a 125 PPI. E l'indirizzo corto e' scritto sotto ("Or open
  http://192.168.1.50/login on the phone"), cosi chi ha la fotocamera che non legge lo puo'
  scrivere a mano. Non cambia niente per la sicurezza: `/login` esiste solo mentre il login
  aspetta (la stessa finestra di `/oauth/callback`) e manda soltanto all'indirizzo che il QR
  lungo avrebbe mostrato; il *verifier* della PKCE non lascia mai la scheda. Se il telefono
  non e' sulla rete della scacchiera, il problema si vede subito (la pagina non si apre),
  invece che dopo aver autorizzato su Lichess.

  Login rifiutato o scaduto (10 minuti): la schermata lo dice e offre *New code*.
- **Token incollato a mano**, la riserva, scritta in piccolo sulla stessa schermata: si crea
  un token personale su lichess.org (permesso `board:play`) e lo si incolla dal monitor
  seriale con `token-set <token>`. La schermata se ne accorge da sola e va avanti.

"Unlink account" nel menu di Lichess (chiede conferma) cancella il token, come `token-clear`.

## Come si parla con Lichess

Due cose diverse, e vanno tenute distinte:

- le **richieste** (una mossa, una sfida, arrendersi) sono brevi e vanno una per volta,
  perche' e' quello che Lichess chiede. Partono da un task apposito: chi le ordina non
  aspetta la risposta;
- gli **stream** (gli eventi dell'account, la partita in corso) sono connessioni che
  restano aperte per ore. Lichess ci manda una riga vuota ogni sette secondi per dire
  "sono ancora qui". Se per **venti secondi** non arriva niente — cioe' se ne mancano due
  di fila — la scacchiera considera morta la connessione e la chiude.

Uno stream caduto **non si riapre da solo**: riaprirlo di nascosto attaccherebbe mezza
riga vecchia a una riga nuova, e all'apertura Lichess rimanda tutto quello che c'e' in
corso senza che nessuno se ne accorga. Chi lo aveva aperto se lo riapre, sapendo cosa sta
facendo.

Quando Lichess risponde **429** (troppe richieste) la scacchiera si ferma da sola per un
minuto abbondante prima di riprovare: e' voluto, non e' un blocco. Anche gli stream: durante
il minuto la scheda non ne riapre nessuno e dice 429 al client, che aspetta con lui.

Una **sfida a un amico** e' l'unica richiesta che resta aperta: Lichess lascia scadere dopo
20 secondi una sfida in tempo reale se la connessione che l'ha creata si chiude, quindi la
scacchiera la manda come POST con `keepAliveStream=true` e tiene aperta la risposta finche'
l'amico non accetta o rifiuta (o finche' non si tocca *Cancel the challenge*). Usa il secondo
posto per gli stream, libero finche' la partita non comincia; non viene mai rimandata da sola
(sarebbe una seconda sfida) e non viene chiusa per silenzio (Lichess non dice niente fino alla
risposta).

TLS: tutto passa da mbedTLS, che di suo terrebbe **16 KB in ingresso piu' 16 KB in uscita
per ogni connessione, nella RAM interna** — circa 42 KB a sessione. Con due sessioni
aperte insieme (uno stream lungo piu' una richiesta) non ci starebbero. All'accensione,
prima di qualunque connessione, la scacchiera sposta tutte le allocazioni di mbedTLS nella
PSRAM, che di spazio ne ha da vendere. Non e' un'ottimizzazione: senza quello spostamento
la seconda connessione non si aprirebbe. Il comando `heap` mostra quanta RAM e' rimasta.

## Le schermate Lichess

Tutto parte dalla voce "Lichess" del menu. Quale schermata si vede dipende dalla scheda, mai
da un'impostazione:

| Situazione | Schermata |
|---|---|
| nessuna rete salvata | **Connect to Wi-Fi**: il nome della rete Arrocco-XXXX, il suo QR, due righe su cosa fare; il portale e' aperto |
| rete salvata, non ancora agganciata | **Connecting to Wi-Fi**, oppure **Cannot connect to Wi-Fi** col motivo, *Try again* e *Set up Wi-Fi* |
| nessun account | **Link Lichess**: il QR del login (sopra), la riserva del token in piccolo |
| account collegato | il **menu di Lichess** |

Il menu di Lichess ha sei righe: **Play the computer**, **Challenge a friend**,
**Invitations** (sotto, quante ce ne sono), **Rated games: off/on** (amichevoli di default, a
ogni accensione), **Unlink account**, **Back**. Sotto il titolo "Signed in as Nome", oppure
cosa non va ("Lichess refused the link": il token non vale piu', e la prima riga diventa
*Link again*).

- **Play the computer**: livello di Stockfish (1-8), colore (Bianco, Nero, o deciso da
  Lichess) e orologio, ognuno si cambia toccandolo; *Start the game*. Gli orologi sono gli
  stessi della partita offline, 5+0, 10+0 (il predefinito), 15+10, 30+0: tutti 3+0 o piu'
  lenti, perche' il bullet la Board API non lo gioca (e il client si rifiuta di chiederlo).
  Contro il computer Lichess non fa mai partite rated.
- **Challenge a friend**: il nome, l'orologio, il colore, amichevole o rated, *Send the
  challenge*. Il nome si sceglie fra **gli ultimi quattro avversari** (tenuti in flash, NVS
  chiave `li-friends`, scritti solo quando la lista cambia) oppure si scrive su una
  **tastiera** a schermo: 10 x 4 tasti da 68 x 56 pixel (14 x 11 mm), solo i 38 caratteri
  che un nome Lichess puo' avere (minuscole, cifre, `-`, `_`; Lichess non bada alle
  maiuscole), piu' Delete, Clear, Cancel, Done. Mandata la sfida: "Waiting for amico to
  accept" con *Cancel the challenge*. Rifiutata: si torna alla pagina con il motivo sotto.
- **Invitations**: le sfide arrivate, una riga ciascuna (nome, orologio, rated o no) con
  *Accept* e *Decline*. Quelle che la scacchiera non puo' giocare (bullet, varianti) hanno
  *Accept* grigio e la nota "not playable on the board"; *Decline* c'e' sempre.

Una partita contro il computer o un invito accettato partono dalla loro pagina ("Starting
the game..." sotto, refresh parziale) e poi la partita, con un refresh Deep: nessuna pagina
in mezzo a lampeggiare. Una partita che comincia da sola (dal telefono, o quella in corso
quando la scacchiera si riaccende) prende lo schermo da sola, purche' sia in tempo reale e
giocabile con la Board API: le partite per corrispondenza no.

Lasciate stare 5 minuti, senza partita in corso, le schermate di Lichess tornano da sole al
menu principale e chiudono la sessione: lo stream degli eventi terrebbe accesa la radio, e
sveglia la scacchiera, per sempre.

### La partita online

La scacchiera e la colonna laterale sono quelle della partita offline. Le differenze:

- **la mossa toccata va a Lichess e la scacchiera non la gioca**: la mostra come mandata (la
  cornice sul pezzo, il pallino dove va, "Sending Nf3...") e sposta il pezzo solo quando lo
  stato della partita che torna dal server la contiene. Mentre una mossa e' in viaggio la
  scacchiera non ne prende altre;
- **una mossa non si perde e non si gioca due volte**: il client la tiene finche' Lichess non
  la accetta o la rifiuta. Dopo un 429 la rimanda finito il minuto ("e4 waits 60 s
  (Lichess)"); se la rete cade o la richiesta va in timeout la rimanda dopo 2 secondi; se
  intanto la scacchiera mostra che la mossa e' stata giocata lo stesso (la POST "fallita" era
  arrivata) la lascia stare. Rimandarla non puo' mai giocarla due volte: dopo una mossa la
  casella di partenza e' vuota, e Lichess risponde "Not your turn";
- le mosse dell'avversario arrivano dallo stream, un refresh (e un beep) ciascuna;
- **gli orologi** sono quelli del server e qui scendono fra un aggiornamento e l'altro, col
  passo della partita offline (ogni 10 secondi, ogni secondo sotto i 20); sopra ci sono i
  nomi ("You" e l'avversario);
- uno **stream caduto** si riapre da solo (il client, 2 secondi dopo); se la caduta dura piu'
  di 3 secondi la colonna dice "Reconnecting...", e il `gameFull` che torna rimette in pari la
  scacchiera, con le mosse giocate nel frattempo;
- pulsanti: **Abort** finche' tutti e due non hanno mosso, poi **Resign** (tutti e due
  chiedono conferma); contro una persona **Offer draw**, oppure **Accept draw** e **Decline
  draw** quando la patta la offre lei; **Claim win** quando l'avversario se ne e' andato e
  l'attesa e' finita; **Flip**; **Menu**, che porta al menu di Lichess lasciando la partita
  in corso (la prima riga diventa *Back to the game*);
- la fine: il risultato sopra la scacchiera ("You won", "You lost", "Draw", "Game aborted") con
  il motivo, e *New game* (un'altra partita come questa: di nuovo il computer, o una sfida
  alla stessa persona), *Review*, *Lichess*. Refresh Deep all'inizio e alla fine, come offline.

La partita online ha una sua `chess::Game`, separata da quella offline: la partita salvata in
flash e il "Resume game" del menu restano come erano. Gioco e client (35 KB in tutto) stanno in
PSRAM, allocati all'avvio come la tabella del motore; senza PSRAM la voce resta grigia.

Due cose del protocollo, decise qui perche' i due stream sono connessioni diverse:

- il `gameFinish` dello stream degli eventi puo' arrivare **prima** dell'ultimo `gameState`
  dello stream della partita, che porta l'ultima mossa (il matto, per esempio). Se lo stream
  della partita e' vivo il client aspetta quello stato fino a 5 secondi, poi chiude la partita
  con quello che diceva l'evento;
- una sfida mandata a un amico, ritirata prima che Lichess abbia detto il suo id, non si
  chiude subito: lo stream resta aperto finche' arriva l'id, e allora la sfida si cancella
  davvero (chiuderlo e basta lascerebbe all'amico 20 secondi per accettarla).

### Nel simulatore

`sim/run.sh --fake-lichess` gioca contro un **Lichess finto** dentro il simulatore
(`sim/host/fake_lichess.*`): niente rete, niente account, niente token. Fa tutto da solo, per
provarlo a mano: il telefono "si collega" al portale 8 secondi dopo, il login e' approvato 6
secondi dopo il QR, Stockfish risponde in 0,8 secondi con una mossa legale qualunque, l'amico
accetta in 3 secondi, e c'e' gia' un invito di "amico". I test lo guidano a righe (`lichess
auto off`, `lichess opp e7e5`, `lichess next 429`, `lichess drop game`, ...; l'elenco e' in
`fake_lichess.h`).

Senza `--fake-lichess`, `sim/run.sh` usa il proxy di `sim/server.py` verso il Lichess vero, con
il token che il proxy trova (il simulatore non ne vede mai il valore): la rete e' quella del
Mac, quindi niente pagina del WiFi, e niente login col QR (lo dice la pagina "Link Lichess").
Il simulatore lanciato direttamente, senza proxy e senza Lichess finto, ha la voce grigia.

Le prove: `make -C test/lichess offline` (il client), `make -C test/lichess_ui test` (amici,
nomi, tastiera, testo, QR disegnati e riletti), `test/ui/lichess_session.py` (collegamento,
computer, inviti, amico, errori: niente WiFi, 401, 429, stream caduto, rete lenta, rete
assente, POST in timeout ma arrivata). I QR sullo schermo vengono riletti dal riconoscitore del
Mac (Core Image, `test/ui/qr_decode.m`) come li leggerebbe un telefono.

## Azzerare

Dal monitor seriale (115200 baud), un comando per riga:

- `wifi-forget` - dimentica la rete e riapre il portale Arrocco-XXXX;
- `wifi-portal` - riapre il portale **senza** dimenticare la rete (utile per spostare la
  scacchiera su un'altra rete). Si chiude da solo dopo 5 minuti senza pagine chieste;
- `wifi-on` - aggancia la rete salvata adesso (resta su 2 minuti dopo l'ultimo lavoro);
- `wifi-off` - spegne la radio subito, portale compreso;
- `token-clear` - cancella il token Lichess (dalla scacchiera: *Unlink account* nel menu di
  Lichess);
- `net` - stato di tutto: rete, TLS, token, stream aperti, e quanta pila non hanno mai
  usato i task di rete (serve a dimensionarle: oggi 10 KB il worker dei comandi, 8 KB
  quello delle richieste e quelli degli stream);
- `heap` - quanta memoria e' libera, e quanto poca ce n'e' stata al minimo;
- `help` - la lista completa.

Cancellando tutta la NVS (riflashando con `erase_flash`) si azzerano insieme rete e
token.

## Se non si collega

Finche' un lavoro di rete aspetta, la scacchiera riprova da sola, aspettando ogni volta il
doppio: 3 secondi, poi 6, poi 12, fino a 30. Quando nessuno aspetta piu' (una richiesta
rinuncia dopo 25 s), dopo 2 minuti spegne la radio. **Il portale non si riapre da solo**:
la versione precedente lo faceva al quarto tentativo fallito e, con la rete di casa fuori
portata, teneva un access point aperto per sempre. Il motivo del fallimento per ora si
legge solo sul seriale, con `wifi-status`:

- **wrong password** - la password e' sbagliata. `wifi-forget` e si ricomincia.
- **network not found** - la rete non si vede. Attenzione: la scheda parla solo
  **2.4 GHz**, non 5 GHz. Se il router unisce le due bande sotto lo stesso nome, di
  solito funziona lo stesso; se non funziona, bisogna separarle.
- **out of range** - segnale troppo debole. L'RSSI si legge nel log al momento in cui la
  scacchiera si collega: sotto -80 dBm la connessione cade spesso.
- **the router refused the connection** / **the router is full** - qualche router
  rifiuta i dispositivi nuovi finche' non li si autorizza (filtro MAC, "lista ospiti").
- **timed out** - si e' agganciata ma il DHCP non ha risposto in 20 s.

Se il portale non si apre da solo sul telefono, e' quasi sempre il telefono che ricorda
la rete come "senza internet": basta aprire a mano **http://4.3.2.1**.

## Cosa non e' ancora stato provato

Niente di tutto questo e' ancora girato sulla scheda: il primo esemplare (06/10/2026) ha
fatto solo il collaudo dell'hardware. Il portale, il captive portal sul telefono, la
macchina a stati del WiFi (con la radio accesa e spenta a richiesta), TLS, lo spostamento
di mbedTLS in PSRAM e il giro OAuth sono verificati **solo leggendoli e compilandoli**. Di
Lichess sono verificati dal vivo, da Mac, i formati delle risposte e il ritmo dei
keep-alive. Le prove da fare sulla scheda sono in `collaudo.md` §G3, e per le schermate qui
sotto.

Non sono mai stati provati contro il Lichess vero: la sfida tenuta viva (`keepAliveStream`:
la forma della prima riga e se lo stream manda keep-alive sono ricavate dalla documentazione),
il rinvio di `/login` letto da un telefono vero, l'ordine in cui arrivano `gameFinish` e
l'ultimo `gameState`.

## Provarlo sulla scheda

Prima di una partita vera, nell'ordine (monitor seriale aperto, `sleep-after 0` per non far
dormire la scheda durante le prove):

1. **Rete da zero.** `wifi-forget`, poi "Lichess" dal menu: "Connect to Wi-Fi" con il QR. La
   fotocamera del telefono deve proporre la rete Arrocco-XXXX; ci si collega, si sceglie la
   rete di casa. La schermata passa a "Connecting to Wi-Fi" e poi a "Link Lichess". Back dalla
   prima schermata: nel log `portal closed`.
2. **Password sbagliata.** Rifarlo sbagliando apposta: "Cannot connect to Wi-Fi" con `wrong
   password (...)`; *Set up Wi-Fi* riapre il portale.
3. **Login col QR.** Il telefono sulla stessa rete: il QR apre `http://<ip>/login`, che manda
   su lichess.org (nel log `OAUTH the phone opened the login page`); *Authorize*; la scacchiera
   va da sola al menu di Lichess con "Signed in as ...". Provare anche a scrivere l'indirizzo
   corto a mano nel browser. Poi *Unlink account* e rifarlo col **token incollato**
   (`token-set ...` mentre la schermata "Link Lichess" e' aperta: deve andare avanti da sola).
4. **Stack e memoria.** Dopo il login: `net` e `heap`, e la riga `LICH  ready: N bytes in
   PSRAM` dell'avvio. Le pile dei task di rete non devono scendere sotto qualche centinaio di
   byte liberi.
5. **Una partita col computer, a perdere.** Livello 1, 10+0: due o tre mosse, guardando che il
   pezzo si sposti solo dopo "Sending ..." e che la mossa di Stockfish arrivi con un beep; poi
   *Menu* e *Back to the game*; poi *Resign*. Sul sito di Lichess (dal telefono) la partita deve
   risultare persa per abbandono, e amichevole.
6. **Uno stream che cade.** In partita, spegnere il router (o allontanare la scheda) per 10-20
   secondi: "Reconnecting...", poi la scacchiera torna in pari da sola. Una mossa toccata
   mentre la rete non c'e' deve partire quando torna, una volta sola.
7. **Un amico.** Con un secondo account (o un amico avvisato): *Challenge a friend*, il nome
   dalla tastiera, *Send*: la sfida deve restare visibile all'amico **oltre i 20 secondi**
   (e' la prova di `keepAliveStream`). *Cancel the challenge* deve farla sparire dal suo
   schermo. Poi una sfida accettata, una patta offerta e rifiutata, e un *Abort* prima della
   seconda mossa.
8. **Un invito.** L'amico sfida la scacchiera: in *Invitations* compare la riga; *Decline* la
   toglie dal suo schermo; un secondo invito *Accept* porta alla partita, con la scacchiera
   girata se si ha il Nero.
9. **Il sonno.** Lasciare il menu di Lichess senza toccarlo: dopo 5 minuti torna da solo al
   menu principale, 2 minuti dopo `WIFI  radio off`, poi la scacchiera dorme come sempre.
