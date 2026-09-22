<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Rete

Come la scacchiera si collega al WiFi e a Lichess. Il codice sta in `src/app/net_wifi.*`,
`net_http.*`, `net_token.*`, `net_console.*`.

> **Stato di oggi.** Sullo schermo della scacchiera di tutto questo non si vede ancora
> **niente**: la voce "Lichess" nel menu e' grigia e non c'e' nessuna schermata di rete.
> Per ora si guarda e si comanda tutto dal monitor seriale USB a 115200 baud. Il pezzo
> che manca e' l'interfaccia, non il collegamento.

## Primo avvio: il portale

Alla prima accensione la scacchiera non conosce nessuna rete, quindi ne apre una lei:

1. compare una rete WiFi aperta che si chiama **Arrocco-XXXX** (le ultime due cifre sono
   quelle del MAC della scheda, cosi due scacchiere vicine non si confondono);
2. ci si collega col telefono. Il telefono apre da solo la pagina di configurazione
   (captive portal); se non lo fa, basta aprire il browser su **http://4.3.2.1**;
3. la pagina mostra le reti trovate: si sceglie la propria, si scrive la password e si
   preme **Save and connect**. C'e' anche un campo per scrivere a mano il nome di una
   rete nascosta;
4. la rete Arrocco-XXXX si chiude e la scacchiera si collega alla rete di casa. Quello che
   sta succedendo (*Scanning*, *Portal open*, *Connecting*, *Online*, *Failed*) si legge
   sul monitor seriale, o col comando `wifi-status`.

La pagina del portale e' fatta tutta di testo scritto dentro il firmware: nessuna
immagine, nessun carattere scaricato, nessun indirizzo esterno. Deve funzionare su un
telefono che in quel momento **non ha internet**, quindi qualunque cosa da scaricare non
arriverebbe mai.

Quanto resta aperto il portale:

- se la scacchiera **non conosce nessuna rete**, resta aperto finche' non si salva
  qualcosa: non c'e' fretta;
- se invece una rete la conosce gia' e il portale si e' aperto **da solo** perche' quella
  rete non rispondeva, dopo **cinque minuti senza che nessuno si colleghi** lo richiude e
  torna a provare la rete di casa. Serve per il caso piu' comune di tutti: il router che
  si stava solo riavviando. Cosi la scacchiera si ricollega da sola, senza che nessuno
  debba toccarla;
- il portale aperto a mano con `wifi-portal` invece non scade: se lo si e' chiesto e' per
  cambiare rete, e sarebbe sciocco tornare da soli su quella vecchia.

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

Per questo la porta 80 viene **servita solo in quelle due finestre**. Quando la scacchiera
e' semplicemente online e si sta giocando, nessuno legge da quella porta e il problema non
esiste. Il resto del lavoro di rete (Lichess, TLS, gli stream) sta su task suoi e non
ferma mai la partita, nemmeno per un millisecondo.

## Dove finiscono le cose

Niente di tutto questo sta nel firmware o in un file: e' tutto in NVS, la memoria di
configurazione della scheda.

| Cosa | Dove | Note |
|---|---|---|
| Nome della rete e password | NVS, namespace `arrocco-wifi` | la password non finisce mai nel log |
| Token Lichess | NVS, namespace `arrocco-lich` | non viene mai stampato, ne' messo in un URL |

Si scrive in NVS **solo** quando l'utente salva qualcosa: a ogni accensione non si scrive
niente, e la copia che il driver WiFi terrebbe per conto suo e' disattivata. La memoria
flash non si consuma stando accesa.

Il token viaggia solo nell'header `Authorization`, mai nell'indirizzo. Nei log si vede
al massimo "presente / assente" e quanti caratteri e'.

## Il login a Lichess

Due strade, come da `decisioni.md`:

- **QR (OAuth2 PKCE)**: la scacchiera genera un indirizzo di lichess.org, lo mostra come
  QR, il telefono lo apre e autorizza, e Lichess rimanda il telefono **alla scacchiera
  stessa** (`http://<indirizzo-della-scacchiera>/oauth/callback`). La scacchiera si
  scambia il codice con un token e lo salva. Serve che telefono e scacchiera siano sulla
  stessa rete di casa. *Il QR e le schermate sono un passo successivo: per ora il giro si
  prova dal comando seriale `oauth-start`, che stampa l'indirizzo da aprire.*
- **Token incollato a mano**: si crea un token personale su lichess.org (permesso
  `board:play`) e lo si incolla. Per ora dal comando seriale `token-set <token>`.

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
minuto abbondante prima di riprovare: e' voluto, non e' un blocco.

TLS: tutto passa da mbedTLS, che di suo terrebbe **16 KB in ingresso piu' 16 KB in uscita
per ogni connessione, nella RAM interna** — circa 42 KB a sessione. Con due sessioni
aperte insieme (uno stream lungo piu' una richiesta) non ci starebbero. All'accensione,
prima di qualunque connessione, la scacchiera sposta tutte le allocazioni di mbedTLS nella
PSRAM, che di spazio ne ha da vendere. Non e' un'ottimizzazione: senza quello spostamento
la seconda connessione non si aprirebbe. Il comando `heap` mostra quanta RAM e' rimasta.

## Azzerare

Dal monitor seriale (115200 baud), un comando per riga:

- `wifi-forget` - dimentica la rete e riapre il portale Arrocco-XXXX;
- `wifi-portal` - riapre il portale **senza** dimenticare la rete (utile per spostare la
  scacchiera su un'altra rete);
- `token-clear` - cancella il token Lichess;
- `net` - stato di tutto: rete, TLS, token, stream aperti;
- `heap` - quanta memoria e' libera, e quanto poca ce n'e' stata al minimo;
- `help` - la lista completa.

Cancellando tutta la NVS (riflashando con `erase_flash`) si azzerano insieme rete e
token.

## Se non si collega

La scacchiera riprova da sola, aspettando ogni volta il doppio: 3 secondi, poi 6, poi 12.
**Al quarto tentativo fallito riapre il portale** e chiede di nuovo la rete (e se nessuno
si presenta, dopo cinque minuti ricomincia il giro da capo). Il motivo del fallimento per
ora si legge solo sul seriale, con `wifi-status`:

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

Niente di tutto questo e' mai girato su una scheda vera: non esiste ancora. Il portale, il
captive portal sul telefono, la macchina a stati del WiFi, TLS, lo spostamento di mbedTLS
in PSRAM e il giro OAuth sono verificati **solo leggendoli e compilandoli**. Di Lichess
sono verificati dal vivo, da Mac, i formati delle risposte e il ritmo dei keep-alive.
