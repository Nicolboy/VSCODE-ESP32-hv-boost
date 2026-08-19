# Session ESP32-C3 — état et travaux à faire

Document de reprise pour une session Claude Code travaillant sur le **firmware
ESP32-C3** (dépôt PlatformIO séparé). Le TMS320 est développé dans un autre
dépôt ; les deux doivent rester synchronisés sur le protocole.

## Rôle de l'ESP32

Trois fonctions, et rien d'autre :

1. **pont série** vers le TMS320F28027 (protocole `$C` / `$T`, 57600 8N1) ;
2. **IHM** (OLED + serveur web) ;
3. **API HTTP** pour un orchestrateur externe.

> **Règle numéro un : l'ESP32 n'a AUCUNE responsabilité de sécurité.** La
> protection réelle est matérielle côté TMS320 (comparateurs analogiques +
> Trip Zone). Le lien UART n'est qu'un report d'information émis après coup :
> quand une trame annonce un défaut, la coupure a déjà eu lieu.
>
> Conséquences : ne jamais dupliquer les bornes physiques, ne jamais recalculer
> localement ce qui existe en télémétrie, ne jamais proposer d'acquittement de
> défaut.

## Fichiers de référence

| Fichier | Contenu |
|---|---|
| `docs/CLAUDE-esp32.md` | à déposer comme `CLAUDE.md` à la racine du dépôt ESP32 |
| `docs/esp32_agent.md` | contrat détaillé côté ESP32 — **source de vérité de l'API HTTP** |
| `docs/ESP32-UART.md` | protocole `$C`/`$T`. ⚠️ s'arrête au code `FAULT` 8, le 9 existe |
| `docs/tms320_agent.md` | ce que le TMS320 émet et accepte |
| `docs/orchestration.md` | architecture de l'orchestrateur externe |

---

## 1. État matériel — a changé, et ça vous concerne

**Carte de puissance V0.2. Tous les composants sont montés SAUF le MOSFET
principal de l'étage 2.**

| Étage | État |
|---|---|
| 1 — `V_inter` 15 à 50 V | ✅ **validé à 50 W**, rendement 91,7 % |
| 2 — sortie HT 200 à 500 V | ⚠️ en cours de peuplement, **jamais mis sous tension** |

Tant que le MOSFET de l'étage 2 n'est pas monté, la carte reste en **mode
étage 1 seul** : `VOSET = 0`, `STATE = 4`, `VOUT ≈ 0`, `DUTY2 = 0`. Ce n'est
pas une panne, c'est l'état nominal — et l'IHM doit l'afficher explicitement,
sinon ça ressemble à une avarie.

---

## 2. 🔴 Blocage n°1 — `VOSET` n'est pas exposé

**C'est le travail qui conditionne toute la mise en route de l'étage 2.**

Aujourd'hui l'ESP32 n'offre **aucun réglage de `VOSET`** ; il est figé et
l'étage 2 reste donc désactivé, quelle que soit la qualité du câblage. Sans
cette commande, la haute tension ne peut pas être mise en route du tout.

### Contraintes du protocole, à respecter à la lettre

- Plage acceptée : **200 à 500 V, ou exactement 0** (0 = étage 2 désactivé).
  Une valeur hors plage est **refusée en bloc** par le TMS320 — pas clampée —
  et incrémente `REJ`.
- **Activer ou désactiver l'étage 2 EN MARCHE est refusé.** La séquence est
  obligatoirement `RUN=0` → nouveau `VOSET` → `RUN=1`. L'IHM doit imposer cet
  ordre, pas envoyer la valeur à la volée.
- **Ne pas dupliquer les bornes côté ESP32.** Le TMS320 est seule source de
  vérité ; deux jeux de bornes finiront par diverger. L'ESP32 relaie, il ne
  valide pas.
- Afficher `VOSP` reçu à côté de la valeur demandée : c'est le seul moyen de
  savoir ce qui est réellement appliqué.

### Contrainte de mise en route

Le firmware TMS320 impose `CTRL_VOUT_SET_MIN_V = 200 V`, ce qui **interdit un
démarrage doux** de l'étage 2. La mise en route pas à pas exigera soit
d'abaisser temporairement cette borne côté TMS320, soit d'alimenter
`V_inter` depuis une alimentation de laboratoire. À arbitrer avec
l'utilisateur — ça ne se décide pas côté ESP32, mais ça conditionne l'ordre
des travaux.

---

## 3. 🔴 Blocage n°2 — le code `FAULT` 9 n'est pas géré

Table complète. **`ESP32-UART.md` s'arrête au 8 : il est périmé.**

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
| **9** | **Sous-tension d'entrée VIN (< 9,5 V)** | **verrouillé** |

Priorité d'affichage : `2 > 3 > 6 > 7 > 9 > 4 > 5 > 8 > 1`. EMUSTOP est le
moins prioritaire malgré son numéro — en développement il se déclenche à
chaque halte du débogueur et ne doit jamais masquer une surintensité réelle.

**Le bug à corriger** : si le test des codes verrouillés est une liste
explicite `2..7`, le code 9 tombe en « transitoire » ou « inconnu » et l'IHM
attend indéfiniment un retour spontané à `FAULT=0` qui ne viendra jamais.

Écrire `verrouillé = (code >= 2 && code != 8)`, ou mieux : une table explicite
qui **lève une erreur visible sur un code inconnu**, pour que le prochain
ajout côté TMS320 se signale au lieu de disparaître.

**Aucun défaut verrouillé ne s'efface** — ni automatiquement, ni par UART. Le
seul retour à la normale est un cycle d'alimentation côté TMS320. Ne proposer
aucun bouton d'acquittement, ni OLED ni web.

---

## 4. Travaux restants, par priorité

| # | Tâche | Motif |
|---|---|---|
| 1 | **Réglage de `VOSET`** | bloque la mise en route de l'étage 2 (§2) |
| 2 | **Code 9 traité comme verrouillé** | l'IHM attend un retour qui ne viendra pas (§3) |
| 3 | **Commande marche/arrêt distincte pilotant `RUN`** | voir ci-dessous |
| 4 | **`V1SET` initialisé dans la plage** | voir ci-dessous |
| 5 | **Afficher `REJ`** | voir ci-dessous |
| 6 | **Réglage de `V1SET` depuis l'IHM** | bloque les courbes de rendement |
| 7 | **Tag `LIM`** | pas encore émis par le TMS320, renvoyer `0` |
| 8 | **Les trois routes HTTP** | spécifiées dans `esp32_agent.md`, existence non confirmée |

### `HT` ne démarre rien — seul `RUN` le fait

Relevé sur cible : `$C,HT=1,V1SET=0.0,VOSET=0.0,RUN=0*69`. Checksum et parsing
corrects, mais **`RUN` n'est jamais mis à 1** — c'est la cause directe de
l'absence de PWM. Le bouton « HV_EN » de l'IHM est mappé sur le mauvais tag.

`HT` ne fait qu'autoriser `HV_EN` **une fois les deux étages établis**, et en
mode étage 1 seul il est de toute façon sans aucun effet. Ce sont deux
commandes indépendantes ; l'IHM doit exposer les deux.

### `V1SET = 0.0` est hors plage et refusé à chaque trame

La plage est 15 à 50 V. Le TMS320 refuse, conserve la consigne précédente et
incrémente `REJ` — observé croissant d'une unité par trame. La consigne
réellement appliquée restait à 15,0 V, sa valeur d'initialisation, et non
celle affichée à l'écran. **Initialiser à 35,0 V.**

### `REJ` doit être affiché

Ce compteur était visible en télémétrie depuis le début et signalait le
problème ci-dessus. Un `REJ` qui s'incrémente signifie que **ce qui est
affiché n'est pas ce qui est appliqué** — exactement le cas de figure qui rend
un dysfonctionnement indéchiffrable. Le comparer à `V1SP`/`VOSP` dit laquelle
des consignes a été refusée.

---

## 5. Protocole — rappels qui coûtent cher

| | |
|---|---|
| UART | `Serial1`, **57600 bauds**, 8N1 |
| TX ESP32 → RX TMS | GPIO7 → GPIO28 (broche 48) |
| RX ESP32 ← TX TMS | GPIO6 ← GPIO29 (broche 1) |
| Checksum | XOR des octets entre `$` et `*` exclus, **hexa MAJUSCULE `%02X`** |
| Fin de ligne | `\n` (`\r` toléré), ligne max **200 caractères** |
| Émission `$C` | toutes les **500 ms** et à chaque changement IHM/API |

```
$T,FAULT=0,STATE=4,REJ=0,FREQ1=200000,FREQ2=100000,DUTY1=68.4,DUTY2=0.0,VIN=12.1,IIN=0.61,V1=35.0,I1=0.62,T1=38.4,VOUT=0.3,I2=0.00,T2=36.1,IOUT=0.00,V1SP=35.0,VOSP=0.0*XX
$C,HT=1,V1SET=35.0,VOSET=0.0,RUN=1*XX
```

- **Décoder par recherche de `TAG=`, jamais par position.** L'ordre a déjà
  changé (`FAULT,STATE,REJ` sont passés en tête pour ne jamais être tronqués
  si la trame frôle les 200 caractères) et changera encore.
- **Champ absent → garder la dernière valeur connue.** Un `FAULT` manquant ne
  vaut pas `FAULT=0`.
- Checksum en minuscules ⇒ **rejet silencieux** côté TMS320. Aucune erreur
  visible, seulement une télémétrie qui n'avance plus.

---

## 6. Règles d'IHM

**Icône danger HT : pilotée uniquement par `VOUT`, jamais par `FAULT`.** Un
défaut ne décharge pas le condensateur de sortie. Après une coupure, `HV_EN`
isole la charge mais les volts restent présents. Faire disparaître l'icône au
moment d'un défaut reviendrait à rassurer l'opérateur précisément quand le
risque est maximal.

⚠️ **Le circuit de décharge active n'est pas monté à ce jour.** Le
condensateur de sortie ne se vide que par le bleeder de 1 MΩ, soit près d'une
minute. Cette icône est, en pratique, la seule indication dont dispose
l'opérateur.

`TMS:KO` si aucune trame `$T` valide depuis plus de **2 s**
(`TmsLink::linkOk()`). Cette logique de timeout ne doit exister **qu'à cet
endroit** ; `link_ok` et `age_ms` de l'API en dérivent.

---

## 7. Grandeurs attendues, pour le mode simulation et les bornes d'affichage

Mesures réelles relevées sur la carte V0.2, étage 1 à 50 W :

```
Vin 23,7 V    I_in 2,17 A    V1 44,5 V    rendement 91,7 %
```

`include/config.h`, `TMS_DUMMY_MODE = true` fait tourner `updateSimulation()`
au lieu de l'UART réel. Garder les plages cohérentes : `Vin` ~10-24 V (pas
~400 V), `V1` jusqu'à ~50 V, `Vout` jusqu'à 400-500 V, `Iin` jusqu'à ~2,5 A.

**Note de fiabilité** : les voies `VOUT` et `I2` du TMS320 **ne sont pas
étalonnées** sur la carte V0.2. Les valeurs qu'elles remontent aujourd'hui ne
sont pas fiables — à ne pas présenter comme des mesures tant que la mise en
route de l'étage 2 n'est pas faite. `I2` est de plus échantillonnée à une
phase arbitraire du cycle de découpage de l'étage 2 : elle battra fortement,
c'est structurel et non une panne.

---

## 8. Sécurité de l'API HTTP

Aucune authentification dans cette révision — acceptable **tant que la carte
reste sur un réseau local de confiance uniquement**. À reconsidérer avant tout
usage hors banc, d'autant que l'API pilote une alimentation 500 V.
