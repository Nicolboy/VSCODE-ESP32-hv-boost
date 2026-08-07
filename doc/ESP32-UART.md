# Liaison UART ESP32-C3 <-> TMS320

Ce document decrit le protocole que le firmware TMS320 doit implementer
pour dialoguer avec l'ESP32-C3 (cote ESP32 deja fonctionnel, voir
`src/tms_link.cpp` / `include/protocol.h`). Le TMS320 envoie sa telemetrie
periodiquement, et recoit periodiquement les commandes de l'operateur.

> **Mise a jour** : coefficients de calibration remplaces par les valeurs
> mesurees sur la carte reelle (voir `mesure-cartepuissance.md`), et ajout
> du champ `FAULT`.

## Cablage

| Signal | ESP32-C3 (config.h) | TMS320 |
|---|---|---|
| ESP32 TX -> TMS320 RX | GPIO7 | GPIO28, broche 48 |
| ESP32 RX <- TMS320 TX | GPIO6 | GPIO29, broche 1 |
| GND commun | GND | GND |

Niveaux logiques 3,3 V des deux cotes -- **verifie, carte CPU validee**.

## Parametres UART

- Vitesse : **57600 bauds**
- Format : **8N1** (8 bits de donnees, pas de parite, 1 bit de stop)
- Cote ESP32 : `Serial1`, defini dans `TmsLink::begin()`

## Format des trames

Protocole ASCII texte, inspire du format NMEA, termine par `\n` (LF).
Chaque trame est encadree par `$` ... `*XX`, ou `XX` est un checksum XOR
en hexadecimal (2 chiffres, majuscules).

### Telemetrie : TMS320 -> ESP32 (periodique)

```
$T,FREQ1=200000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,FAULT=0*XX
```

Champs (`TAG=valeur`, separes par des virgules, ordre libre) :

| Tag | Unite | Description |
|---|---|---|
| `FREQ1` | Hz | Frequence de decoupage etage 1 (200 kHz) |
| `FREQ2` | Hz | Frequence de decoupage etage 2 (100 kHz) |
| `DUTY1` | % | Rapport cyclique etage 1 |
| `DUTY2` | % | Rapport cyclique etage 2 |
| `VIN` | V | Tension d'entree |
| `IIN` | A | Courant d'entree |
| `V1` | V | Tension intermediaire (sortie etage 1) |
| `I1` | A | Courant shunt MOSFET etage 1 |
| `T1` | degC | Temperature NTC etage 1 |
| `VOUT` | V | Tension de sortie HT |
| `I2` | A | Courant shunt MOSFET etage 2 |
| `T2` | degC | Temperature NTC etage 2 |
| `IOUT` | A | Courant de sortie |
| `FAULT` | code 0-5 | Defaut courant (voir ci-dessous) |

### Codes `FAULT`

| Code | Signification |
|---|---|
| `0` | Aucun defaut |
| `1` | EMUSTOP -- le debogueur a arrete le CPU |
| `2` | Surintensite I1 (etage 1) |
| `3` | Surintensite I2 (etage 2) |
| `4` | Temperature critique T1 (> 80 degC) |
| `5` | Temperature critique T2 (> 80 degC) |

**Priorite quand plusieurs defauts coexistent : 2 > 3 > 4 > 5 > 1.**
EMUSTOP a donc la priorite la plus BASSE malgre son numero : en
developpement il se declenche a chaque halte du debogueur et ne doit
jamais masquer une surintensite reelle.

**Il n'existe volontairement pas de code "court-circuit".** Un
court-circuit franchit le meme comparateur et le meme seuil qu'une
surintensite ; le firmware ne dispose d'aucune information permettant de
les distinguer. Les codes 2 et 3 couvrent les deux cas.

#### Verrouillage : pas d'effacement par le protocole

Contrairement a ce que supposait une version precedente de ce document,
**un defaut ne s'efface JAMAIS automatiquement**, ni a la disparition de
sa cause, ni par commande UART. Il n'existe aucun tag `$C` permettant de
l'acquitter.

Le seul moyen de repartir est de **couper puis retablir l'alimentation**.
C'est un choix deliberé (PROMPT §8) : un defaut de puissance doit exiger
une intervention humaine et un examen de la carte.

Consequence pour l'ESP32 : tant que le TMS320 emet un `FAULT != 0`, il
continuera de l'emettre indefiniment. L'IHM doit donc afficher ce code de
maniere persistante, sans attendre un retour spontane a 0.

> Rappel (PROMPT §2) : la protection reelle -- comparateurs + Trip Zone
> materiel -- ne depend jamais de cette liaison UART. `FAULT` n'est qu'un
> report d'information vers l'IHM, envoye apres coup ; la coupure a deja
> eu lieu en materiel avant meme que la trame ne soit construite.

#### Icone danger : pilotee par VOUT, jamais par FAULT

L'avertissement haute tension doit dependre **uniquement de `VOUT`**, et
rester affiche independamment de l'etat de `FAULT`.

Sur un **defaut I ou T** (OVERCURRENT_I1/I2, OVERTEMP_T1/T2), le TMS320
pilote expressement la broche `DISCHARGE` a 0 pour decharger activement le
condensateur de sortie (~2 s). Sur **EMUSTOP**, en revanche, le CPU est
juste arrete (halte debogueur, aucune routine de defaut executee) :
`DISCHARGE` n'est PAS actionne, les ~400 V peuvent donc rester presents
bien plus longtemps (seul le bleeder passif, ~42 s, agit alors). C'est
pour cette raison que l'icone danger doit rester pilotee par `VOUT` seul
et jamais par `FAULT` : meme apres un defaut I/T qui declenche la
decharge active, il faut quelques secondes avant que `VOUT` ne repasse
sous le seuil — masquer l'icone plus tot (ex. des que `FAULT != 0`)
reviendrait a rassurer l'operateur avant que la tension soit reellement
retombee.

Cote affichage ESP32 (`OledUi::isRealFault()` / `faultLabel()`,
`src/oled_ui.cpp`) : tous les codes sont affiches nommement
(`SYS:EMUSTOP`, `SYS:OVERI1`, `SYS:OVERI2`, `SYS:OVERT1`, `SYS:OVERT2`),
en plus de l'icone danger qui reste independante (pilotee par `VOUT`
seul, cf. ci-dessus).

Cas particulier EMUSTOP : comme il arrete aussi l'UART cote TMS320
(attendu, pas une panne), l'ESP32 **continue d'afficher `SYS:EMUSTOP`**
(derniere valeur connue) plutot que de basculer sur `SYS:KO` au bout du
timeout de liaison de 2s — le temps que le TMS320 reparte apres retrait
du breakpoint. `KO` reste affiche pour toute autre perte de liaison
(derniere valeur connue = OK ou un defaut 2-5, ou aucune trame jamais
recue).

La carte de puissance comporte deux dispositifs complementaires :

- un **bleeder de 1 MOhm avec LED rouge en serie**, indication passive
  qui reste valable alimentation coupee (~42 s pour passer sous 50 V) ;
- un **circuit de decharge actif** (MOSFET + 47 kOhm, ~2 s de 500 V a
  50 V), commande par le TMS320 sur GPIO5 / broche 40, **a logique
  inversee** : il conduit des que la commande disparait, donc au reset,
  en cas de plantage et alimentation coupee -- et expressement sur
  defaut I/T (voir ci-dessus).

**Frequence d'envoi : toutes les 300 ms.** L'ESP32 considere la liaison
perdue (`TMS:KO`) si aucune trame `$T` valide n'est recue depuis plus de
**2 secondes**. Le TMS320 applique le meme timeout dans l'autre sens.

Tous les champs sont optionnels individuellement (un champ absent garde
sa derniere valeur cote ESP32) ; en pratique le TMS320 les envoie tous.
Si une trame devait depasser la longueur maximale, le firmware abandonne
des champs entiers plutot que d'en tronquer un.

### Calibration des mesures analogiques (cote TMS320)

Coefficients appliques sur la lecture ADC (`Vadc`, en volts) pour obtenir
la valeur physique. **Valeurs mesurees sur la carte reelle**, sauf mention
contraire -- voir `mesure-cartepuissance.md` pour la tracabilite.

| Grandeur | Pleine echelle (3,3 V) | Relation | Statut |
|---|---|---|---|
| `VIN` | 36,7 V | `VIN = Vadc x 11,11` | mesure |
| `IIN` | 3,97 A | `IIN = Vadc x 1,2019` | mesure (1 point) |
| `V1` | 103,1 V (usage limite a 50 V) | `V1 = Vadc x 31,25` | mesure |
| `VOUT` | 600 V | `VOUT = Vadc x 181,82` | mesure |
| `IOUT` | 50 mA (a confirmer) | `IOUT = Vadc x 0,016667` | **a mesurer** |
| `I1` | 5,15 A | `I1 = (Vadc - 0,0473) / 0,631` | **provisoire (MCP6001)** |
| `I2` | 5,12 A | `I2 = (Vadc - 0,0340) / 0,637` | **provisoire (MCP6001)** |

**Notes :**

- **I1/I2** : le gain differe d'environ 1 % entre les deux voies et chaque
  voie a un offset non nul a courant nul (47,3 / 34,0 mV). Ne jamais
  utiliser un gain unique pour les deux. Ces valeurs sont mesurees avec
  les **MCP6001 de banc** ; le composant definitif est le TLV9151. Le gain
  devrait rester valable (fixe par le reseau de resistances) mais
  **l'offset devra etre integralement remesure**.
- **MCP6001, limite de bande passante** : 1 MHz de produit gain-bande a un
  gain de ~31 plafonne vers 32 kHz. Pour proteger un decoupage a 200 kHz,
  les 30 ns du comparateur ne servent a rien si l'ampli en amont met des
  dizaines de microsecondes. **La protection rapide n'est pas reellement
  rapide tant que le MCP6001 est en place.**
- **NTC** : montage 3,3 V -- NTC -- R_fixe -- 0 V, mesure au point milieu.
  La NTC etant cote 3,3 V, la relation est
  `R_ntc = R_fixe x (VREF - Vadc) / Vadc`. Les thermistances sont a 5 mm
  des MOSFET : elles mesurent le cuivre, pas la jonction, avec un ecart
  statique et un retard thermique notables. Le seuil de 80 degC est donc
  **provisoire**, a recaler sur la temperature de boitier en charge.
- **IOUT** : aucune mesure reelle a ce jour.

### Commande : ESP32 -> TMS320 (periodique + a chaque changement)

```
$C,HT=1,PWM1=1,PWM2=0*3E
```

| Tag | Valeurs | Description |
|---|---|---|
| `HT` | 0 ou 1 | Sortie HT activee/desactivee |
| `PWM1` | 0 ou 1 | PWM etage 1 activee/desactivee |
| `PWM2` | 0 ou 1 | PWM etage 2 activee/desactivee |

L'ESP32 envoie cette trame toutes les **500 ms**, et immediatement a
chaque changement depuis l'IHM. Le TMS320 applique le dernier etat recu et
valide. Les trois tags doivent etre presents, sinon la trame est ignoree.

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), en hexadecimal majuscule
sur 2 chiffres (`%02X`).

Pour la reception : meme calcul, comparaison au checksum recu, trame
ignoree silencieusement si ca ne correspond pas.

> **Piege rencontre cote TMS320, a ne pas reproduire.** Decoder le
> checksum avec `strtol` sur un buffer non termine par `\0` fait lire
> au-dela de la trame. Et sur C28x, `uint8_t` fait 16 bits (pas
> d'adressage par octet) : un cast `(uint8_t)` ne tronque donc rien, et un
> `"7D"` suivi d'un residu `"7D"` donne 0x7D7D, conserve tel quel. Toute
> trame etait rejetee. Decoder **exactement deux chiffres**, de maniere
> bornee.

## Points de vigilance

- Checksum en **majuscules** sur 2 chiffres, sinon rejet silencieux cote
  ESP32 -- la telemetrie n'avance simplement plus.
- Terminer par `\n` uniquement (`\r` tolere).
- Longueur de ligne maximale : **200 caracteres** (`g_lineBuf[200]` cote
  ESP32, `UART_LINE_MAX` cote TMS320).
