# CLAUDE.md — firmware ESP32-C3 (pont TMS320 ↔ IHM ↔ API HTTP)

> Contexte de référence pour ce dépôt. Les documents cités (`esp32_agent.md`,
> `tms320_agent.md`, `ESP32-UART.md`, `orchestration.md`, `mesure-cartepuissance.md`,
> `claudeesp32-etage2-interface.md`) vivent dans `doc/` de ce même dépôt — ce
> sont la source de vérité du protocole, à garder synchronisés avec le dépôt
> TMS320 quand celui-ci évolue.

---

## Ce qu'est cette carte

Un ESP32-C3 qui fait **trois choses et rien d'autre** :

1. un pont série vers le TMS320F28027 (protocole `$C` / `$T`, 57600 8N1) ;
2. une IHM (OLED + serveur web) ;
3. une API HTTP (`/api/telemetry`, `/api/setpoint`, `/api/stop`) pour un
   orchestrateur externe.

Il pilote une alimentation élévatrice **deux étages en cascade** (V_inter
15-50 V, puis sortie HT 200-500 V).

## Règle numéro un : l'ESP32 n'a AUCUNE responsabilité de sécurité

La protection réelle est matérielle côté TMS320 (comparateurs + Trip Zone).
Le lien UART n'est qu'un **report d'information**, émis après coup : quand une
trame `$T` annonce un défaut, la coupure a déjà eu lieu.

Conséquences directes, à ne jamais contourner :

- **Ne pas dupliquer les bornes physiques** (`V1SET` 15-50 V, `VOSET`
  200-500 V ou 0). L'ESP32 relaie, il ne valide pas. Le TMS320 rejette
  lui-même et incrémente `REJ`. Deux jeux de bornes finiront par diverger.
- **Ne rien recalculer localement** qui existe déjà en télémétrie (`LIM`,
  `link_ok` mis à part, cf. plus bas). Le TMS320 est la seule source de vérité.
- **Ne jamais proposer d'acquittement de défaut.** Aucun tag `$C` ne l'efface.
  Le seul retour à la normale est un cycle d'alimentation côté TMS320.

## État matériel actuel — mode étage 1 seul

Carte de puissance **V0.2**. Étage 1 **validé à 50 W** (rendement 91,7 %).

**Étage 2 : tous les composants sont montés SAUF le MOSFET principal**, et il
n'a jamais été mis sous tension. `VOSET=0` est la convention qui désactive
l'étage 2 ; c'est l'état par défaut au démarrage du TMS320 et l'état dans
lequel tourne la carte aujourd'hui.

> État détaillé et historique de session : **`doc/PROMPT-session-esp32.md`**.

- Ne **jamais** envoyer un `VOSET` non nul tant que l'étage 2 n'est pas monté :
  la consigne serait acceptée, jamais atteinte, et la machine resterait bloquée
  en `STATE=3` avec l'intégrateur saturé.
- `VOSP=0.0` n'est **pas** une anomalie : c'est le marqueur du mode étage 1
  seul. L'IHM l'affiche explicitement (`ETAGE1` à la place du libellé `VOUT`
  côté OLED, mention dédiée côté web), sinon `STATE=4` avec `VOUT≈0`
  ressemble à une panne alors que tout est normal.
- `HT=1` est **sans effet** dans ce mode (`HV_EN` exige les deux étages
  établis). `DUTY2` reste à 0,0 et `I2` au bruit de mesure.
- Changer de mode exige `RUN=0` d'abord : `RUN=0` → nouveau `VOSET` → `RUN=1`.
  Une bascule en marche est refusée et incrémente `REJ`.

## Protocole — l'essentiel

Détail complet : `doc/esp32_agent.md` et `doc/ESP32-UART.md` (⚠️ ce dernier
s'arrête au code `FAULT` 8, périmé sur ce point précis — le 9 existe, voir
plus bas).

| | |
|---|---|
| UART | `Serial1`, **57600 bauds**, 8N1 |
| TX ESP32 → RX TMS | GPIO7 → GPIO28 (broche 48) |
| RX ESP32 ← TX TMS | GPIO6 ← GPIO29 (broche 1) |
| Checksum | XOR des octets entre `$` et `*` exclus, **hexa MAJUSCULE `%02X`** |
| Fin de ligne | `\n` (le `\r` est toléré), ligne max **200 caractères** |

Trame reçue :
```
$T,FAULT=0,STATE=4,REJ=0,FREQ1=200000,FREQ2=100000,DUTY1=68.4,DUTY2=0.0,VIN=12.1,IIN=0.61,V1=35.0,I1=0.62,T1=38.4,VOUT=0.3,I2=0.00,T2=36.1,IOUT=0.00,V1SP=35.0,VOSP=0.0*XX
```

Trame émise, toutes les **500 ms** et immédiatement à chaque changement IHM/API :
```
$C,HT=1,V1SET=50.0,VOSET=0.0,RUN=1*XX
```

`V1SET` est **fixe à 50,0 V**, par décision de conception (pas de champ ni de
curseur IHM) : c'est ce qui rend les 500 V de sortie atteignables (rapport
cyclique étage 2 = `1 − V1/VOUT`, 0,90 à 50 V contre le plafond 0,95 dès que
V1 descend vers 25 V). **Toujours l'émettre dans chaque trame** : un tag
omis laisse le TMS320 sur sa valeur d'init (15 V), sans que `REJ` bouge ni
qu'aucun indicateur ne le signale (détail : `doc/claudeesp32-etage2-interface.md` §3.2).

Deux règles de parsing non négociables :

- **Décoder par recherche de `TAG=`, jamais par position.** L'ordre des champs
  a déjà changé (`FAULT,STATE,REJ` sont passés en tête pour ne jamais être
  tronqués si la trame frôle la limite de 200 caractères) et changera encore.
- **Champ absent → garder la dernière valeur connue**, ne jamais remettre à
  zéro par défaut. Un `FAULT` manquant ne vaut pas `FAULT=0`.
- Checksum en minuscules ⇒ rejet **silencieux** côté TMS320. Aucun message
  d'erreur, seulement une télémétrie qui n'avance plus.

## Codes `FAULT` — table complète, code 9 inclus

| Code | Signification | Nature |
|---|---|---|
| 0 | aucun défaut | — |
| 1 | EMUSTOP (débogueur a arrêté le CPU) | transitoire |
| 2 | Surintensité I1 (étage 1) | **verrouillé** |
| 3 | Surintensité I2 (étage 2) | **verrouillé** |
| 4 | Température critique T1 (> 80 °C) | **verrouillé** |
| 5 | Température critique T2 (> 80 °C) | **verrouillé** |
| 6 | Survoltage V_inter (> 55 V) | **verrouillé** |
| 7 | Survoltage sortie (> 520 V) | **verrouillé** |
| 8 | Liaison ESP32 perdue (> 2 s sans `$C` valide) | transitoire |
| 9 | Sous-tension d'entrée VIN (< 9,5 V pendant ~75 µs) | **verrouillé** |

Priorité d'affichage quand plusieurs coexistent côté TMS320 :
`2 > 3 > 6 > 7 > 9 > 4 > 5 > 8 > 1`. EMUSTOP est le moins prioritaire malgré
son numéro — en développement il se déclenche à chaque halte du débogueur et
ne doit jamais masquer une surintensité réelle. (Purement informatif côté
ESP32 : un seul code `FAULT` est reçu à la fois, la priorisation est décidée
entièrement côté TMS320.)

`isFaultLatched()` (`include/protocol.h`) est écrit comme "tout sauf les
transitoires connus" (`fault != NONE && fault != EMUSTOP && fault != LINK_LOST`),
pas comme une plage explicite `2..7` : un futur code ajouté côté TMS320 est
verrouillé par défaut plutôt que de tomber silencieusement en "inconnu" et
de laisser l'IHM attendre indéfiniment un retour spontané à `FAULT=0` qui ne
viendra jamais — c'est exactement ce qui s'est produit avec le code 9 avant
correction.

Le code 9 se déclenche quand l'alimentation d'entrée s'affaisse sous 9,5 V,
typiquement **sur charge élevée en sortie de l'étage 1** (l'élévateur
compenserait en augmentant le rapport cyclique, donc le courant, jusqu'à la
surintensité — le TMS320 coupe avant). La surveillance n'est armée qu'après un
premier franchissement de 12 V, pour ne pas verrouiller pendant la montée de
l'alimentation au démarrage.

## Chantiers ouverts côté ESP32, par priorité

1. **Tag `LIM`** (repliement de puissance, `0`/`1`/`2`) : à parser dès que le
   TMS320 l'émettra — **pas encore implémenté côté TMS320**, renvoyer `0` en
   attendant. Ce n'est **pas** un défaut (`FAULT=0`, `STATE=4`, la régulation
   tourne) : ne pas le présenter comme une alarme, mais l'afficher en
   permanence quand il est actif. Ne pas le déduire de `VIN` : l'hystérésis sur
   le seuil de 20 V rend la valeur dépendante de l'historique.
2. **Signaler le mode d'essai boucle ouverte étage 2** (`STAGE2_OPENLOOP_TEST`
   côté TMS320, tant que le MOSFET de l'étage 2 n'est pas monté) : dans ce
   mode toute consigne `VOSET` non nulle est refusée (`REJ++`, `VOSP` reste à
   `0.0`). Pas encore un indicateur dédié — couvert pour l'instant par `REJ`
   et par le message « étage 1 seul » qui continue de s'afficher malgré une
   demande de palier (détail : `doc/claudeesp32-etage2-interface.md` §5).

Déjà fait (ne pas refaire) : code 9 traité comme verrouillé, commande
marche/arrêt distincte pilotant `RUN` (bouton web "Marche", indépendant de
"HT"), `V1SET` **fixe à 50,0 V** dans chaque trame (pas de champ IHM, décision
de conception — voir plus haut), affichage de `REJ`/`V1SP`/`VOSP` (OLED pour
REJ, web pour les trois), les trois routes HTTP d'orchestration, et
l'exposition de `VOSET` dans le dashboard web : 4 boutons de palier (OFF/200/
300/400, actifs uniquement à l'arrêt — `RUN=0` imposé par désactivation des
boutons pendant la marche) + décalage fin `±10 V` (0 à +100, actif en marche,
désactivé en mode OFF). Arithmétique entière côté JS, `VOSET` envoyé =
palier + décalage (détail : `doc/claudeesp32-etage2-interface.md` §3.3).

## Règles d'IHM

- **Icône danger HT : pilotée uniquement par `VOUT`, jamais par `FAULT`.** Un
  défaut ne décharge pas le condensateur de sortie à lui seul. Après une
  coupure, `HV_EN` isole la charge mais les volts peuvent rester présents un
  moment (bleeder passif ~1 MΩ + circuit actif décrit dans
  `doc/tms320_agent.md`, piloté entièrement côté TMS320). Faire disparaître
  l'icône au moment d'un défaut reviendrait à rassurer l'opérateur précisément
  quand le risque est maximal.
- ⚠️ **Le circuit de décharge active n'est pas monté à ce jour** (carte V0.2).
  Le condensateur de sortie ne se vide que par le bleeder de 1 MΩ, soit près
  d'une minute. Cette icône est, en pratique, la seule indication dont
  dispose l'opérateur.
- `TMS:KO` si aucune trame `$T` valide depuis plus de **2 s**
  (`TmsLink::linkOk()`). Cette logique de timeout ne doit exister **qu'à cet
  endroit** ; `link_ok` et `age_ms` de l'API en dérivent.
- **Défaut : jamais d'acquittement.** Tant qu'un code verrouillé est actif,
  l'IHM affiche le code de façon persistante, sans bouton d'acquittement ni
  OLED ni web — seul un cycle d'alimentation côté TMS320 y met fin.
- Afficher explicitement le mode « étage 1 seul » (cf. plus haut).

## API HTTP

Spécification faisant foi : `doc/esp32_agent.md` §"API HTTP". Résumé :

- `GET /api/telemetry` → JSON de la dernière `$T`, augmenté de `link_ok` et
  `age_ms`.
- `POST /api/setpoint` → corps `{ "v1_set": 35.0, "vout_set": 0, "run": true,
  "ht": false }` (champs optionnels). Réponse : écho de la trame `$C` envoyée
  **et** un relevé de télémétrie juste après, pour que l'appelant vérifie `rej`
  sans deuxième requête.
- `POST /api/stop` → `RUN=0, HT=0`. Doit fonctionner même si le corps JSON
  d'une requête précédente était invalide : route la plus simple possible,
  sans dépendance à un état applicatif.

Pas d'authentification dans cette révision — acceptable **tant que la carte
reste sur un réseau local de confiance uniquement**. À reconsidérer avant tout
usage hors banc, d'autant que l'API pilote une alimentation 500 V.

## Mode simulation

`include/config.h` : `TMS_DUMMY_MODE = true` fait tourner `updateSimulation()`
au lieu de l'UART réel, pour développer l'IHM sans TMS320 branché. Garder les
plages cohérentes avec le matériel : `Vin` ~10-24 V (pas ~400 V), `V1` jusqu'à
~50 V (pas ~200 V), `Vout` jusqu'à 400-500 V, `Iin` jusqu'à ~2,5 A.

**Note de fiabilité** (carte V0.2) : les voies `VOUT` et `I2` du TMS320 ne
sont pas étalonnées — à ne pas présenter comme des mesures fiables tant que
la mise en route de l'étage 2 n'est pas faite. `I2` est de plus échantillonnée
à une phase arbitraire du cycle de découpage de l'étage 2 : elle battra
fortement, c'est structurel et non une panne.

## Avant de modifier quoi que ce soit

- Lire `doc/esp32_agent.md` (contrat côté ESP32) et `doc/tms320_agent.md` (ce
  que le TMS320 émet et accepte). **Les deux documents doivent rester
  synchronisés : toute évolution du protocole se répercute dans les deux.**
- Un champ ajouté à `$T` côté TMS320 se voit ici en premier. Si un tag inconnu
  apparaît, ne pas l'ignorer silencieusement — le signaler.
- Compiler (`pio run`) après chaque changement, comme systématiquement fait
  jusqu'ici dans ce projet.
