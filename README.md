# ESP32-C3 dualboost — pont TMS320 ↔ IHM ↔ API HTTP

Firmware ESP32-C3 pour une alimentation élévatrice deux étages en cascade
(étage 1 : V_inter 15-77 V, étage 2 : sortie HT 200-500 V), régulée par un
TMS320F28027 côté puissance.

Ce firmware fait **trois choses et rien d'autre** :

1. un pont série vers le TMS320F28027 (protocole texte `$C` / `$T`, UART
   57600 8N1) ;
2. une IHM locale (écran OLED SSD1322 + serveur web embarqué) ;
3. une API HTTP (`/api/telemetry`, `/api/setpoint`, `/api/stop`) pour un
   orchestrateur externe.

## Architecture

```
TMS320F28027  <--UART 57600 8N1-->  ESP32-C3  <--WiFi AP + HTTP-->  OLED / navigateur / orchestrateur
(regulation,      $C (commandes)    (pont, IHM,
 protections        $T (telemetrie)  API HTTP)
 materielles)
```

**Règle de conception centrale : l'ESP32 n'a aucune responsabilité de
sécurité.** La protection réelle (comparateurs + Trip Zone) est entièrement
côté TMS320. Le lien UART ne fait que relayer une information déjà
survenue : quand une trame `$T` annonce un défaut, la coupure a déjà eu
lieu côté matériel. L'ESP32 ne valide, ne recalcule ni n'acquitte rien —
il relaie.

Détail du protocole, des codes de défaut, et de chaque route HTTP :
[`CLAUDE.md`](CLAUDE.md) et les documents sous [`doc/`](doc/).

## État réel du projet

Carte de puissance **V0.2**.

- **Étage 1 : validé, fonctionnel, testé à 50 W** (rendement mesuré 91,7 %).
- **Étage 2 : tous les composants sont montés sauf le MOSFET principal.**
  Il n'a jamais été mis sous tension. `VOSET=0` (convention "étage 2
  désactivé") est l'état par défaut et l'état dans lequel tourne la carte
  aujourd'hui — ce firmware n'a donc jamais piloté de sortie HT réelle.
  Envoyer un `VOSET` non nul dans cet état bloque la régulation en
  `STATE=3` sans jamais atteindre la consigne.
- Les voies de mesure `VOUT` et `I2` du TMS320 **ne sont pas étalonnées** :
  ne pas les considérer comme fiables tant que l'étage 2 n'a pas été mis en
  route.
- L'API HTTP n'a **aucune authentification** dans cette révision — acceptable
  uniquement sur un réseau local de confiance. À reconsidérer avant tout
  usage hors banc, d'autant que cette API pilote in fine une alimentation
  500 V.
- Le circuit de décharge active du condensateur de sortie **n'est pas
  monté** sur cette carte : la décharge ne se fait que par le bleeder
  passif (~1 MΩ, près d'une minute). L'icône danger HT de l'IHM (pilotée
  par `VOUT`, jamais par `FAULT`) est en pratique la seule indication dont
  dispose l'opérateur après une coupure.

Chantiers ouverts et détail complet de l'état matériel/logiciel :
[`CLAUDE.md`](CLAUDE.md) § « État matériel actuel » et
[`doc/journal-20260819-mise-en-route-voset.md`](doc/journal-20260819-mise-en-route-voset.md).

## Compiler et flasher

Projet [PlatformIO](https://platformio.org/), cible `esp32-c3-devkitm-1`.

```
pio run
pio run --target upload
pio device monitor
```

Avant la première compilation, créer les identifiants du point d'accès
WiFi de l'IHM (jamais versionnés) :

```
cp include/credentials.h.example include/credentials.h
# puis editer include/credentials.h
```

Mode simulation sans TMS320 branché : `TMS_DUMMY_MODE` dans
`include/config.h` (voir `CLAUDE.md` § « Mode simulation »).

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — contrat protocole complet, codes `FAULT`,
  règles d'IHM, spécification de l'API HTTP, état matériel détaillé.
- [`doc/esp32_agent.md`](doc/esp32_agent.md) et
  [`doc/tms320_agent.md`](doc/tms320_agent.md) — contrat de part et
  d'autre de la liaison UART ; à garder synchronisés avec le firmware
  TMS320 quand celui-ci évolue.
- [`doc/references.md`](doc/references.md) — références et liens de
  téléchargement des datasheets constructeur (non redistribuées ici, voir
  licence).

## Licence

[CERN-OHL-S v2](LICENSE) (« strongly reciprocal »).

Les datasheets et le code d'exemple du fabricant de l'écran OLED
(EastRising / buydisplay.com) ne sont pas inclus dans ce dépôt — droits du
fabricant, non redistribuables. Voir [`doc/references.md`](doc/references.md)
pour les récupérer à la source.
