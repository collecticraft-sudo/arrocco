# Da comprare adesso

Pezzi che mancano per montare Arrocco. Costano poco e arrivano lenti: conviene ordinarli subito, così sono sul banco insieme al resto.
I prezzi sono indicativi, non verificati. Niente link: cerca le parole della colonna "Cosa cercare".

## Lista

| # | Pezzo | Quanti | Prezzo | Perché serve |
|---|---|---|---|---|
| 1 | Header maschio passo 2,54 mm, **a 90°**, strip 1×40 da tagliare | 1 strip (ne servono 2 pezzi da 1×7) | ~1 € | La driver board Seeed ha due file di 7 fori vuoti (CN1 e CN2) e nella scatola non c'è nessun header. È l'unico punto da cui prendere I2C, touch, buzzer, 3V3 e GND. Va saldato per forza. A 90° perché dritti, con i Dupont sopra, aggiungono circa 14 mm di altezza e toccano il bordo del XIAO. |
| 2 | Header maschio 2,54 mm **dritto**, strip 1×40 | 1 strip | ~1 € | Scorta. Serve di sicuro se il XIAO arriva **senza** pin saldati (vedi nota sotto). |
| 3 | Breakout FPC **6 pin passo 0,5 mm** → fori 2,54 mm, con connettore già montato | 2 con contatti **sopra** (top) + 2 con contatti **sotto** (bottom), oppure 2–3 a **doppio contatto** | 1–2 € l'uno | Sostituisce il FTS02 dentro la scocca (il FTS02 è circa 59×55 mm e alto 13–14 mm, non ci sta). Non si sa ancora da che lato ha i contatti il flat del touch: si vede solo all'arrivo. Per questo si prendono entrambi i tipi. |
| 4 | Resistenze **4,7 kΩ**, 1/4 W, a reofori | 10 (ne servono 2) | centesimi | Pull-up su SDA e SCL. Sul banco le fornisce il FTS02 (ha due 10 kΩ); con il breakout spariscono e quelle interne dell'ESP32 (circa 45 kΩ) sono troppo deboli anche a 100 kHz. |
| 5 | Resistenze **10 kΩ**, 1/4 W, a reofori | 10 (ne serve 1) | centesimi | Pull-up su RST del touch. Il datasheet del GT911 lo chiede, il FTS02 non ce l'ha. Tiene fermo il touch quando l'ESP32 dorme. |
| 6 | Breakout **MAX17048** (indicatore di carica I2C) | 1 (2 se costa poco) | 3–5 € | La driver board non misura la batteria in nessun modo. Il MAX17048 sta sullo stesso bus I2C del touch (indirizzo 0x36, nessun conflitto), non usa pin in più e si collega con due fili ai pad BAT +/− sul retro della driver board. Scegline uno con **fori passo 2,54 mm**, non solo connettorini JST/Qwiic. |
| 7 | **Multimetro** economico | 1 | 10–20 € | **Obbligatorio prima di collegare la batteria.** La driver board non ha protezione contro l'inversione di polarità e la batteria è di un venditore AliExpress: se rosso e nero sono scambiati, il chip di ricarica molto probabilmente si brucia. Servono: volt in continua, continuità col cicalino, ohm. Meglio se ha anche la portata µA/mA (servirà per misurare il consumo in sleep). |
| 8 | Cavetti Dupont **femmina-femmina**, 10 cm e 20 cm | 1 mazzetto da 40 per misura | 2–4 € | Banco: header della driver board ↔ header del FTS02, buzzer. Tienili corti: l'I2C resta a 100 kHz finché i fili sono lunghi. |
| 9 | Viti **M2** assortite (M2×4, ×6, ×8) e inserti a caldo M2 o viti M2 autofilettanti | 1 scatolina | 3–6 € | La driver board ha 4 fori da circa 2,2 mm (stima da foto, **da misurare col calibro**). Per chiudere la scocca restano gli inserti M3 e le viti M3×6 già previsti nel recap. |

## Opzionale

| Pezzo | Prezzo | Quando serve |
|---|---|---|
| Prolunga FPC **24 pin passo 0,5 mm**: schedina di giunzione (coupler) + cavo piatto FFC 24 pin 0,5 mm, 10–15 cm | pochi euro | Solo se, a pezzi in mano, il flat del display non arriva al connettore della driver board nella posizione che vuoi dare al "mento", oppure ci arriva con i contatti dal lato sbagliato (il connettore Seeed prende i contatti **solo verso l'alto**). Seeed nel suo pannello da 7,5" usa proprio una prolunga. Se la ordini ora per non aspettare dopo, prendi il cavo in tutte e due le versioni: contatti dallo **stesso lato** (tipo A) e da **lati opposti** (tipo B). |

## Nota sul XIAO: pin saldati o no?

In `decisioni.md` c'è scritto "XIAO con header già saldati". L'inserzione che hai linkato (negozio ufficiale Seeed su AliExpress, variante unica "ESP32S3 Plus") mostra però nelle foto la scheda **nuda** più l'antenna, e nel testo non parla mai di pin saldati.

- Se arriva **con** i pin: niente da fare, si infila nello zoccolo.
- Se arriva **senza**: non è un problema, sono 14 saldature passanti (il tuo genere). Hai due strade:
  1. **Montaggio normale**: due pezzi 1×7 di strip dritta (riga 2) saldati sotto il XIAO, come quelli di fabbrica. Pila stimata circa 14,7 mm.
  2. **Montaggio ribassato**: il XIAO appoggiato direttamente sopra lo zoccolo, senza il distanziale di plastica dei pin. Il dossier stima circa 12,2 mm, cioè 2,5 mm in meno nel "mento". È più delicato da saldare.

Se arriva senza pin **non saldare subito**: le misure e la prima accensione del XIAO da solo (`collaudo.md`) si fanno senza pin. Poi si decide insieme: la strada 1 è facile, ma una volta saldata non si passa più alla 2.

## Cosa cercare

| # | Parole da cercare |
|---|---|
| 1 | `pin header 2.54mm right angle 1x40` oppure `7-Pin Male Header XIAO` |
| 2 | `pin header 2.54mm straight 1x40` |
| 3 | `FPC FFC 6pin 0.5mm adapter board 2.54`, `6P 0.5mm FPC to DIP`. Molte schedine hanno da un lato il passo 0,5 e dall'altro il passo 1,0: controlla che il connettore **montato** sia quello da 0,5 mm e leggi nella descrizione `top contact` / `bottom contact` / `dual contact`. |
| 4–5 | `resistor 4.7k 1/4W metal film`, `resistor 10k 1/4W metal film` (o un kit assortito) |
| 6 | `MAX17048 breakout`, `MAX17048 fuel gauge module I2C` |
| 7 | `multimetro digitale` con "continuità" o "buzzer" tra le funzioni |
| 8 | `dupont female female 10cm`, `dupont female female 20cm` |
| 9 | `M2 screw assortment`, `M2 heat set insert` |
| opz. | `FFC FPC 24pin 0.5mm extension board`, `FFC cable 24pin 0.5mm type A`, `... type B` |

## Se non li hai già

Stagno sottile (0,5–0,8 mm), un po' di flussante, pompetta o treccia dissaldante, una pinzetta. Un cavo USB-C **dati** (non solo ricarica) per il Mac.

## Cosa NON comprare

- Un secondo caricabatterie o un modulo TP4056: la driver board carica già la batteria da sola. Mai collegare la LiPo anche ai pad BAT del XIAO.
- Un alimentatore a 5 V per il FTS02: il FTS02 si alimenta a 3,3 V dal pin 3V3 della driver board.
- Una batteria diversa, per ora. Se dopo le misure la 606090 (6 mm) risultasse troppo spessa, l'alternativa del dossier è una 407090 da circa 2500 mAh: 2 mm in meno e ricarica in 5–6 ore invece di 9–10.
