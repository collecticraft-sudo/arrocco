# Cablaggio

Vale la pin map di `decisioni.md`. Questo file sostituisce del tutto il vecchio cablaggio "fase 1", che aveva INT, RST e buzzer su altri pin ed etichette del FTS02 sbagliate.

Due montaggi:
- **Banco**: touch attraverso il FTS02, fili Dupont. Serve a collaudare.
- **Finale**: touch attraverso un breakout FPC a 6 pin con tre resistenze, fili saldati. È quello che va nella scocca.

Regola generale: **si collega e si scollega tutto a scheda spenta** (USB staccata, interruttore su OFF).

## 1. Pin map

| Funzione | XIAO | GPIO | Dove lo prendi |
|---|---|---|---|
| Display RST | D0 | 1 | già collegato dalla driver board |
| Display CS | D1 | 2 | già collegato |
| Display BUSY | D2 | 3 | già collegato |
| Display DC | D3 | 4 | già collegato |
| Display SCK | D8 | 7 | già collegato |
| Display MOSI | D10 | 9 | già collegato |
| **I2C SDA** (touch + MAX17048) | D4 | 5 | foro `D4` di CN1 |
| **I2C SCL** (touch + MAX17048) | D5 | 6 | foro `D5` di CN1 |
| **Touch RST** | D6 | 43 | foro `D6` di CN1 |
| **Buzzer** | D7 | 44 | foro `D7` di CN2 |
| **Touch INT** | D9 | 8 | foro `D9` di CN2 |
| 3,3 V | 3V3 | — | foro `3V3` di CN2 (ce n'è uno solo) |
| Massa | GND | — | foro `GND` di CN2 (ce n'è uno solo) |

Il display non ha fili: passa tutto dal flat a 24 pin. I fili da fare sono 7: SDA, SCL, RST, INT, buzzer, 3V3, GND.

Indirizzi I2C: GT911 (touch) `0x5D`, in riserva `0x14`; MAX17048 `0x36`. Bus a 100 kHz.

Perché proprio questi pin: INT sta su D9 perché solo i GPIO 0–21 possono svegliare l'ESP32-S3 dal deep sleep, e D9 era l'unico libero. Il buzzer sta su D7 perché all'accensione resta muto. RST sta su D6, che all'accensione "chiacchiera" per qualche millisecondo: al touch non fa niente, il firmware lo resetta comunque subito dopo.

## 2. Driver board: dove sono i fori

Vista **da sopra** (lato dello zoccolo del XIAO), con il connettore del flat display a sinistra. `[o]` è la piazzola quadrata (pin 1).

```
                  presa batteria JST (nella tacca a mezzaluna)
          +-------------------\__/-------------------------------+
          |  CN1    D6    D5    D4    DC   BUSY   CS    RST      |
   flat   |          o     o     o     o     o     o    [o]      |
 display  |        +--------------------------------------+      |
  24 pin  |        |          zoccolo del XIAO            |  USB-C ===>
 ======== |        +--------------------------------------+      |
          |         [o]    o     o     o     o     o     o       |
          |  CN2    D7    SCK   D9   MOSI   3V3   GND    5V      |
          +----------[ON/OFF]------------------------------------+
                    interruttore
```

- **CN1** (fila dal lato della presa batteria), dal lato USB verso il flat: `RST CS BUSY DC D4 D5 D6`.
- **CN2** (fila dal lato dell'interruttore), dal lato USB verso il flat: `5V GND 3V3 MOSI D9 SCK D7`.
- Le stesse scritte sono serigrafate **sul retro**, accanto ai fori. In caso di dubbio vale la serigrafia, non il disegno qui sopra (ricavato da foto e schema Seeed, non da una scheda vera).

Fori che usi: `D4 D5 D6` su CN1, `D7 D9 3V3 GND` su CN2.
Fori da lasciare stare: `RST CS BUSY DC SCK MOSI` (sono del display) e soprattutto **`5V`**: non è un'uscita per altri carichi.

### Saldare i due header 1×7

1. Taglia due pezzi da 7 pin dalla strip a 90°.
2. Dal pezzo per CN2 **sfila il pin che finirebbe nel foro `5V`** (si tira via con una pinza): così non ci infili mai un Dupont per sbaglio.
3. Prova a secco: i pin a 90° devono uscire verso l'esterno della scheda senza coprire l'interruttore né la presa batteria. Se danno fastidio, montali dal lato opposto della scheda.
4. Salda dal lato opposto a quello in cui hai infilato l'header. Sono saldature passanti normali: punta sul pin e sulla piazzola insieme, un paio di secondi, stagno, via.
5. Controlla con una lente che non ci siano ponti tra pin vicini, soprattutto tra `3V3` e `GND`.

Nel montaggio finale, per stare più sottili, gli header si possono saltare: fili saldati direttamente nei fori.

## 3. XIAO nello zoccolo

Lo zoccolo **non ha chiave**: il XIAO entra anche girato di 180°.

- Verso giusto: **USB-C dal lato opposto al connettore del flat**, sopra l'estremità dove il retro dice `5V` / `RST`.
- Girato al contrario mette 5 V su un pin dati (D6) e scambia 3V3 e GND con due pin di segnale. Si controlla **due volte** prima di dare corrente.
- Spingilo fino in fondo, dritto.

## 4. Display

Flat a 24 pin del pannello nel connettore FPC della driver board. Il connettore Seeed fa contatto **solo verso l'alto**: i contatti dorati del flat devono guardare in su, lontano dalla scheda. Alza la linguetta, infila il flat dritto fino in fondo, richiudi.
Un flat infilato al rovescio è l'errore classico con questo pannello: lo schermo resta muto.

Su quale faccia del flat stiano i contatti si vede solo a pezzi in mano. È una delle cose da annotare in `collaudo.md`, perché decide come si orienta la driver board nella scocca.

## 5. Touch al banco (FTS02)

Il FTS02 qui fa solo da adattatore per il flat a 6 pin del touch. La sua presa per l'e-paper resta vuota. Interruttorini e trimmer del FTS02 non c'entrano col touch: lasciali come sono.

**Flat del touch** nella presa **TP-FPC2 / P7** (quella marcata `3.7/4.2/4.26/5.83/7.5`), **inserito girato** rispetto a come andrebbe il flat del display. In pratica: prima prova con il pannello a faccia in su e il FTS02 **capovolto** (pin degli header verso l'alto: così leggi anche le serigrafie). Se il touch non risponde, spegni e prova nell'altro verso.

**Header del FTS02** (le scritte sono sul lato dei pin):

```
P10:   A0   A3   A4   A5   A6   A7
P11:   5V  3.3V 3.3V  GND  GND  RESET  A19  A18
```

| FTS02 | → | Driver board | Funzione |
|---|---|---|---|
| P11 `3.3V` (uno dei due) | → | CN2 `3V3` | alimentazione touch |
| P11 `GND` (uno dei due) | → | CN2 `GND` | massa |
| P10 `A4` | → | CN1 `D4` | SDA |
| P10 `A5` | → | CN1 `D5` | SCL |
| P10 `A0` | → | CN2 `D9` | INT |
| P10 `A3` | → | CN1 `D6` | RST |

Per ricordarlo: **A4 con D4, A5 con D5**.

Le due trappole:
- **Mai il pin `5V` di P11.** È attaccato ai due `3.3V`: conta i pin prima di infilare il filo. Il touch regge al massimo 3,6 V.
- **Mai i pin serigrafati `SDA` e `SCL` di P8.** Non sono il bus del touch (vanno alla microSD e alla luce frontale).

Altre due cose da sapere:
- Il LED del FTS02 è sul ramo a 5 V: alimentato a 3,3 V **resta spento anche se tutto funziona**. Non usarlo come spia.
- Al banco non servono resistenze: il FTS02 ha già due pull-up da 10 kΩ su SDA e SCL.

**Prova di accettazione**: appena dai corrente, il firmware di collaudo deve dire che il GT911 risponde sull'I2C (`0x5D` oppure `0x14`). Se non risponde, **spegni subito** e gira il flat: con il flat al rovescio arrivano 3,3 V sul pin sbagliato del touch. Un controllo di corto tra 3,3 V e GND non basta ad accorgersene.

## 6. Buzzer KY-006

| KY-006 | → | Dove |
|---|---|---|
| `S` | → | CN2 `D7` |
| `−` | → | massa. Al banco: il **secondo `GND` di P11** del FTS02 (la driver board ha un solo foro GND). Nel finale: il pad `−` sul retro della driver board, oppure una giunzione con gli altri fili di massa |
| pin centrale | | non collegato |

## 7. Touch nel montaggio finale (breakout FPC + resistenze)

Piedinatura del flat del touch, dal disegno Good Display. Guardando il pannello di fronte con il flat verso il basso, il pin 1 è **a destra** e il pin 6 a sinistra.

| Pin flat | Segnale | Va a |
|---|---|---|
| 1 | GND | CN2 `GND` |
| 2 | VDD (3,3 V) | CN2 `3V3` |
| 3 | RST | CN1 `D6` |
| 4 | INT | CN2 `D9` |
| 5 | SDA | CN1 `D4` |
| 6 | SCL | CN1 `D5` |

Resistenze, saldate direttamente sul breakout tra il pad VDD e i tre segnali:

```
 3V3 ---+----------+----------+------- pad VDD (pin 2)
        |          |          |
     [4,7 k]    [4,7 k]     [10 k]
        |          |          |
       SDA        SCL        RST
     (pin 5)    (pin 6)    (pin 3)
```

INT non ha resistenza.

Quale breakout (contatti sopra o sotto) e in che verso, si decide a pezzi in mano:
1. Guarda su quale faccia del flat ci sono i contatti dorati e scegli il breakout che li tocca. Se il tipo è sbagliato non c'è contatto e il touch semplicemente non risponde.
2. I numeri stampati sul breakout possono risultare **specchiati** rispetto al flat (1↔6, 2↔5, 3↔4). Segui con l'occhio il pin 1 del flat fino al suo pad e scrivi `GND` sul breakout col pennarello. Col multimetro in continuità puoi verificare a quale pad arriva ciascun contatto del connettore.
3. La prova è la stessa del banco: il GT911 risponde? Se no, spegni subito e ricontrolla il verso.

Tieni il breakout entro circa 40 mm dal punto in cui il flat esce dal vetro (il flat sporge 47 mm). Sul flat c'è il chip del touch con una piastrina rigida di 15×14 mm: non piegare lì e non fare pieghe più strette di circa 1 mm di raggio.

## 8. MAX17048 (percentuale batteria)

| MAX17048 | → | Dove |
|---|---|---|
| positivo cella (`BAT`, `CELL`, `VBAT`, `+`: dipende dal breakout) | → | pad **`+`** sul retro della driver board |
| `GND` | → | pad **`−`** sul retro della driver board |
| `SDA` | → | bus SDA (`D4`) |
| `SCL` | → | bus SCL (`D5`) |

- I pad `+` e `−` stanno **prima** dell'interruttore: con la batteria inserita sono sempre sotto tensione, anche su OFF. **Salda con la batteria scollegata.** Rosso al `+`, nero al `−`, e ricontrolla.
- Per lo stesso motivo il MAX17048 resta alimentato anche a interruttore spento. Il consumo è piccolo ma non zero; il valore non è nel dossier, lo misureremo.
- I breakout non sono tutti uguali. Se il tuo ha un pin in più per la logica (`VIN`, `VCC`, `3V3`, `VIO`...), **mandami una foto prima di saldare**.
- SDA e SCL sono condivisi col touch. Nel finale i fili si saldano a catena (driver board → MAX17048 → breakout del touch). Al banco servono due "Y": un Dupont tagliato a metà con un terzo filo saldato in mezzo, isolato col termorestringente.

## 9. Batteria: procedura della polarità

La driver board **non ha protezione contro l'inversione**. La presa è una JST a 2 pin passo 2,0 mm; la serigrafia dice `− +` e il `+` è dal lato del triangolino bianco (pin 1). La batteria viene da un venditore qualunque: il verso dei suoi fili **non si sa** finché non lo misuri.

1. Interruttore su **OFF**, USB staccata. Batteria **non** inserita.
2. Multimetro su volt in continua (portata 20 V).
3. Accosta la spina alla presa nel verso in cui entrerebbe, **senza infilarla**, e guarda quale filo finirebbe sul `+` della serigrafia.
4. Puntale rosso sul contatto di quel filo, nero sull'altro. Devi leggere un valore **positivo**, tra circa 3 e 4,2 V.
5. Se leggi un valore **negativo**, la spina è al contrario: **non inserirla**. Si rimedia scambiando i due contatti nel guscio della spina (si solleva con un ago la linguetta di plastica di ciascuno, si sfila, si reinfila nell'altra sede). Poi rimisura.
6. Solo ora inserisci la spina, sempre con l'interruttore su OFF.
7. Secondo controllo: sul retro, tra i pad `+` e `−`, devi leggere lo stesso valore positivo. Poi interruttore su ON.

Il passo 4 è quello che conta: è l'unico controllo a rischio zero. Il passo 7 è solo una conferma.

Mai collegare la LiPo **anche** ai pad BAT sotto il XIAO: si scavalca l'interruttore e si mettono due caricatori sulla stessa cella.

## 10. Accensione e ricarica

| USB | Interruttore | Scheda | Batteria |
|---|---|---|---|
| collegata | OFF | **accesa** (va a USB) | isolata, **non si carica** |
| collegata | ON | accesa | **in carica** |
| staccata | ON | accesa a batteria | si scarica |
| staccata | OFF | **spenta davvero** | ferma (resta vivo solo il MAX17048) |

- **Per caricare: interruttore su ON.** Con la USB collegata la scheda è sempre accesa, qualunque sia la posizione dell'interruttore.
- Carica a circa 0,5 A fissi: la 4000 mAh ci mette **9–10 ore**. Si carica di notte.
- Non c'è nessun LED di carica e il firmware non può sapere se la batteria sta caricando. Il LED rosso del XIAO può accendersi per una trentina di secondi a ogni accensione: non vuol dire niente.
- A batteria scarica (circa 2,8 V) la scheda si spegne di colpo, senza avviso. Con il MAX17048 il firmware può avvisare prima.
- L'interruttore è lo spento vero. Il "sonno" del firmware (risveglio col tocco) consuma poco ma non zero: quanto, lo misuriamo a pezzi in mano.

## 11. Pin liberi

Sugli 11 pin laterali del XIAO non avanza niente. Il Plus ha in più 9 mezzi-fori passo 1,27 mm sul bordo, in mezzo ai pin normali (D11–D19 = GPIO 38–42, 10, 13, 12, 11): ci si arriva solo saldando un filo sottile di lato. Non sono previsti in questo progetto.
