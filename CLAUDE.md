# CLAUDE.md — firmware ESP32-C3 (pont TMS320 ↔ IHM ↔ API HTTP)

## Ce qu'est cette carte

Un ESP32-C3 qui fait **trois choses et rien d'autre** :

1. un pont série vers le TMS320F28027 (protocole `$C` / `$T`, 57600 8N1) ;
2. une IHM (OLED + serveur web) ;
3. une API HTTP (`/api/telemetry`, `/api/setpoint`, `/api/stop`) pour un
   orchestrateur externe.

Il pilote une alimentation élévatrice **deux étages en cascade** (V_inter
15-77 V, puis sortie HT 200-500 V).

## Règle numéro un : l'ESP32 n'a AUCUNE responsabilité de sécurité

La protection réelle est matérielle côté TMS320 (comparateurs + Trip Zone).
Le lien UART n'est qu'un **report d'information**, émis après coup : quand une
trame `$T` annonce un défaut, la coupure a déjà eu lieu.

Conséquences directes, à ne jamais contourner :

- **Ne pas dupliquer les bornes physiques** (`V1SET` 15-77 V, `VOSET`
  200-500 V ou 0). L'ESP32 relaie, il ne valide pas. Le TMS320 rejette
  lui-même et incrémente `REJ`. Deux jeux de bornes finiront par diverger.
- **Ne rien recalculer localement** qui existe déjà en télémétrie (`LIM`
  encore à implémenter côté TMS320, `link_ok` calculé localement à partir
  du seul timeout de liaison — voir `src/tms_link.cpp`). Le TMS320 est la
  seule source de vérité.
- **Ne jamais proposer d'acquittement de défaut.** Aucun tag `$C` ne l'efface.
  Le seul retour à la normale est un cycle d'alimentation côté TMS320.

## État matériel actuel — mode étage 1 seul

Carte de puissance **V0.2**. Étage 1 **validé à 50 W** (rendement 91,7 %).
Étage 2 : composants montés sauf le MOSFET principal, jamais mis sous
tension — `VOSET=0` (mode étage 1 seul) est la convention par défaut au
démarrage et l'état réel de la carte aujourd'hui.

> Historique de la mise en route de `VOSET` (résolue) : **`doc/journal-20260819-mise-en-route-voset.md`**.

- **Ne jamais envoyer un `VOSET` non nul tant que l'étage 2 n'est pas
  monté** : la consigne serait acceptée, jamais atteinte, et la machine
  resterait bloquée en `STATE=3` avec l'intégrateur saturé. Comportement
  complet du mode : commentaires de `include/protocol.h` et
  `doc/tms320_agent.md` (skill, voir plus bas).
- **Le circuit de décharge active n'est pas monté à ce jour.** Seul le
  bleeder passif (~1 MΩ, près d'une minute) décharge le condensateur de
  sortie. L'icône danger HT (pilotée uniquement par `VOUT`, jamais par
  `FAULT` — voir `VOUT_DANGER_THRESHOLD` dans `src/oled_ui.cpp`) est donc
  en pratique la seule indication dont dispose l'opérateur.
- Les voies `VOUT` et `I2` du TMS320 **ne sont pas étalonnées** — à ne pas
  présenter comme des mesures fiables tant que l'étage 2 n'est pas mis en
  route.

## Mode simulation

`TMS_DUMMY_MODE` (`include/config.h`) fait tourner `updateSimulation()`
(`src/tms_link.cpp`) au lieu de l'UART réel, pour développer l'IHM sans
TMS320 branché — plages et justification déjà commentées sur place, ne
pas les recopier ici.

## Protocole, codes `FAULT`, API HTTP — source de vérité

Câblage UART, format des trames, table `FAULT` complète (0-9), champ
`LIM`, calibration analogique et API d'orchestration HTTP sont documentés
une seule fois, partagés avec le dépôt `TMS320F28027-dualboost` — ne pas
les recopier ici, exactement ce que ce partage existe pour éviter (voir
`electronique-savoir/log.md`, arbitrage du 2026-10-11, pour l'historique
de la divergence que ça a remplacée).

- Skill electronique-savoir `c2000:protocole-dualboost` — `tms320_agent.md`
  fait foi en cas d'écart entre les deux fichiers du skill (voir son
  `SKILL.md`).
- Contrat propre au firmware ESP32 : `doc/esp32_agent.md`.
- Détail d'implémentation (enum `TmsFault`, `isFaultLatched()`, parsing) :
  commentaires de `include/protocol.h` et `src/tms_link.cpp` — à jour, ne
  pas les recopier ici non plus.

## Chantiers ouverts et déjà fait

`doc/chantiers-ouverts.md` fait autorité pour l'état d'avancement côté
ESP32 (tag `LIM`, mode `STAGE2_OPENLOOP_TEST`, liste de ce qui est déjà
implémenté). À lire avant de proposer une fonctionnalité déjà faite ou
déjà écartée.

## Avant de modifier quoi que ce soit

- Lire `doc/esp32_agent.md` (contrat côté ESP32) et `tms320_agent.md` (ce
  que le TMS320 émet et accepte, dans le skill). **Les deux documents
  doivent rester synchronisés : toute évolution du protocole se répercute
  dans les deux.**
- Un champ ajouté à `$T` côté TMS320 se voit ici en premier. Si un tag
  inconnu apparaît, ne pas l'ignorer silencieusement — le signaler.
- Compiler (`pio run`) après chaque changement.
