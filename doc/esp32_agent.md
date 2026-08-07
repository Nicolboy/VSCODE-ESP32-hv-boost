# ESP32-C3 — contrat de protocole et API (agent firmware ESP32)

Ce document définit ce que **le firmware ESP32-C3 doit faire** : côté
liaison série avec le TMS320, côté IHM (OLED + web), et côté API HTTP
pour l'orchestration externe. Contrepartie de `tms320_agent.md`, qui
définit ce que le TMS320 émet/accepte — les deux documents doivent rester
synchronisés.

---

## Câblage

| Signal | ESP32-C3 (config.h) | TMS320 |
|---|---|---|
| ESP32 TX → TMS320 RX | GPIO7 | GPIO28, broche 48 |
| ESP32 RX ← TMS320 TX | GPIO6 | GPIO29, broche 1 |
| GND commun | GND | GND |

Niveaux logiques 3,3 V des deux côtés — **vérifié, carte CPU validée**.

## Paramètres UART

- Vitesse : **57600 bauds**
- Format : **8N1**
- Côté ESP32 : `Serial1`, défini dans `TmsLink::begin()`

---

## Réception `$T` (télémétrie du TMS320)

Format complet et signification de chaque champ : voir `tms320_agent.md`.
Champs à parser, y compris ceux ajoutés récemment : `FAULT`, `STATE`,
`V1SP`, `VOSP`, `REJ` — nécessaires à l'API HTTP d'orchestration (plus
bas), pas seulement à l'affichage.

- Timeout de liaison : `TMS:KO` affiché si aucune trame `$T` valide
  depuis plus de **2 secondes** (`TmsLink::linkOk()`).
- Champ absent dans une trame reçue → garder la dernière valeur connue,
  ne jamais remettre à zéro par défaut.

### L'ordre des champs a changé : `FAULT,STATE,REJ` viennent en tête

```
$T,FAULT=0,STATE=4,REJ=0,FREQ1=200000,...,V1SP=35.0,VOSP=0.0*XX
```

La trame fait ~175 caractères sur les 200 autorisés et le TMS320
**abandonne** un champ qui ne tiendrait pas plutôt que de déborder.
L'état de sécurité est donc émis en premier pour ne jamais pouvoir être
tronqué — perdre `T2` est sans conséquence, perdre `FAULT` ne l'est pas.

Le parseur doit rester **indifférent à l'ordre** (chercher chaque tag, ne
pas décoder par position) : c'est déjà le cas si le décodage est fait par
recherche de `TAG=`, mais toute optimisation par position serait cassée
par ce changement et par les suivants.

## Émission `$C` (commande vers le TMS320)

```
$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX
```

Tags disponibles : `HT`, `PWM1`/`PWM2` (acceptés côté TMS320 mais sans
effet — la régulation pilote les étages elle-même, ne pas s'appuyer
dessus), `V1SET` (15-50 V), `VOSET` (200-500 V **ou 0**), `RUN`.

Envoi toutes les **500 ms**, et immédiatement à chaque changement depuis
l'IHM (OLED/web) ou un appel de l'API HTTP décrite plus bas.

### `VOSET=0` — mode étage 1 seul, **état actuel de la carte**

C'est le point le plus important de cette révision pour l'ESP32.

Sur la carte de puissance, **le MOSFET, la diode et l'inductance de
l'étage 2 ne sont pas montés** : seul le premier étage est en cours de
validation. `VOSET=0` est la convention qui désactive l'étage 2 côté
TMS320 — la régulation s'arrête à l'étage 1 établi (`STATE=4`), l'étage 2
reste inerte, et `HT=1` est sans effet.

C'est **l'état par défaut au démarrage du TMS320**. Concrètement pour
l'ESP32 :

- ne pas envoyer de `VOSET` non nul tant que l'étage 2 n'est pas monté :
  la consigne serait acceptée, la sortie ne pourrait jamais l'atteindre, et
  la machine resterait bloquée en `STATE=3`, intégrateur saturé à 95 % ;
- ne pas traiter `VOSP=0.0` comme une anomalie ni comme une consigne de
  0 V : c'est le marqueur du mode étage 1 seul ;
- l'IHM devrait afficher ce mode explicitement, sinon `STATE=4` avec
  `VOUT≈0` ressemble à une panne alors que tout est normal ;
- `DUTY2` restera à `0.0` et `I2` au bruit de la chaîne de mesure.

**Changer de mode exige `RUN=0` d'abord.** Une trame qui activerait ou
désactiverait l'étage 2 en marche est **refusée** et incrémente `REJ`. La
séquence correcte est `RUN=0`, puis le nouveau `VOSET`, puis `RUN=1`. Si
l'IHM propose un jour un réglage de `VOSET`, elle doit imposer cet ordre
plutôt que d'envoyer la valeur à la volée.

**Une consigne hors plage envoyée par l'ESP32 sera rejetée par le
TMS320**, pas clampée : la consigne précédente reste active côté TMS320,
et `REJ` s'incrémente. L'ESP32 ne doit donc pas supposer qu'une valeur
envoyée a été appliquée — comparer `V1SP`/`VOSP` reçus en télémétrie à ce
qui a été demandé pour le savoir.

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), hexadécimal
**majuscule** sur 2 chiffres (`%02X`) — sinon rejet silencieux côté
TMS320 pour les trames `$C`, et la réciproque est vraie en réception :
une trame `$T` mal checksumée doit être ignorée sans erreur visible.

Terminer chaque trame par `\n` (le `\r` est toléré). Longueur de ligne
maximale : **200 caractères** (`g_lineBuf[200]`).

---

## IHM — règles à respecter (OLED et web)

### Défaut : jamais d'acquittement

Tant que `FAULT != 0` sur un code verrouillé (2 à 7, voir
`tms320_agent.md`), l'IHM doit :
- afficher le code de façon **persistante**, sans attendre un retour
  spontané à 0
- **ne proposer aucun bouton ou commande d'acquittement**, ni OLED ni web
- le seul retour à la normale est un cycle d'alimentation côté TMS320,
  hors du contrôle de l'ESP32

Les `FAULT` transitoires (1 = EMUSTOP, 8 = liaison perdue) se relèvent
seuls — l'IHM peut les afficher différemment (par exemple sans les
traiter comme aussi critiques qu'un défaut verrouillé), mais ce n'est pas
une obligation stricte, à confirmer selon le rendu voulu.

### Icône danger HT : pilotée uniquement par `VOUT`

L'avertissement haute tension doit dépendre **uniquement** de la valeur
de `VOUT`, jamais de `FAULT`. Un défaut ne décharge pas le condensateur
de sortie à lui seul : après une coupure, `HV_EN` isole la charge mais
les volts peuvent rester présents un moment (voir le double dispositif de
décharge — bleeder passif ~1 MΩ et circuit actif décrit dans
`tms320_agent.md` — piloté entièrement côté TMS320, l'ESP32 n'a aucune
action dessus, seulement l'affichage). Faire disparaître l'icône danger
au moment d'un défaut reviendrait à rassurer l'opérateur précisément
quand le risque est maximal.

### Mode simulation (`TMS_DUMMY_MODE`)

`include/config.h` : à `true`, l'ESP32 tourne sur `updateSimulation()`
au lieu de lire l'UART réel — utile pour développer l'IHM sans matériel
TMS320 branché. Plages de simulation à garder cohérentes avec les
grandeurs réelles (voir `mesure-cartepuissance.md`) : `Vin` ~10-20 V pas
~400 V, `V1` jusqu'à ~50 V pas ~200 V, `Vout` jusqu'à 400-500 V.

---

## API HTTP pour orchestration externe

Destinée à un orchestrateur externe (agent IA, script) — voir
`orchestration.md` pour l'architecture complète et l'usage côté
orchestrateur. Cette section est la spécification que l'ESP32 doit
implémenter ; `orchestration.md` en est le consommateur, pas l'inverse.

### `GET /api/telemetry`

Sérialisation JSON de la dernière trame `$T` reçue, augmentée de
`link_ok` et `age_ms` calculés côté ESP32 à partir de `TmsLink::linkOk()`
— ne pas dupliquer cette logique de timeout ailleurs.

```json
{
  "freq1": 200000, "freq2": 100000,
  "duty1": 68.4, "duty2": 74.1,
  "vin": 10.2, "iin": 0.61,
  "v1": 34.8, "i1": 0.62, "t1": 38.4,
  "vout": 399.7, "i2": 0.087, "t2": 36.1,
  "iout": 0.0224,
  "fault": 0, "state": 4,
  "v1_sp": 35.0, "vout_sp": 400.0, "rej": 0,
  "link_ok": true, "age_ms": 180
}
```

### `POST /api/setpoint`

Corps JSON, champs optionnels :

```json
{ "v1_set": 35.0, "vout_set": 400.0, "run": true, "ht": true }
```

Traduit en trame `$C` correspondante. Réponse : écho de la trame
effectivement envoyée **et** un relevé de télémétrie juste après, pour
que l'appelant puisse vérifier `rej` sans requête supplémentaire :

```json
{ "frame_sent": "$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX",
  "telemetry_after": { "...": "...", "rej": 0 } }
```

### `POST /api/stop`

Raccourci envoyant `RUN=0, HT=0`. Doit fonctionner même si le corps JSON
d'une requête précédente était invalide — route la plus simple possible,
sans dépendance à un état applicatif complexe.

---

## Points de vigilance

- Checksum en majuscules, sinon rejet silencieux — aucune erreur visible,
  seulement une télémétrie qui n'avance plus côté récepteur.
- Ne jamais dupliquer les bornes physiques (`V1SET`/`VOSET`) côté ESP32 :
  la seule source de vérité est le TMS320, qui rejette lui-même les
  valeurs hors plage. L'ESP32 relaie, il ne valide pas.
- L'API HTTP n'a aucune authentification dans cette révision — acceptable
  tant que la carte reste sur un réseau local de confiance uniquement.
