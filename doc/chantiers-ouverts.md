# Chantiers ouverts côté ESP32

Référencé depuis `CLAUDE.md` § « Chantiers ouverts et déjà fait ». Liste ce
qui reste à faire côté firmware ESP32, par priorité, et ce qui est déjà
fait — pour ne pas le proposer une seconde fois.

## Ouvert

1. **Tag `LIM`** (repliement de puissance, `0`/`1`/`2`) : à parser dès que
   le TMS320 l'émettra — **pas encore implémenté côté TMS320**, renvoyer
   `0` en attendant. Ce n'est **pas** un défaut (`FAULT=0`, `STATE=4`, la
   régulation tourne) : ne pas le présenter comme une alarme, mais
   l'afficher en permanence quand il est actif. Ne pas le déduire de
   `VIN` : l'hystérésis sur le seuil de 20 V rend la valeur dépendante de
   l'historique. Détail complet : skill `c2000:protocole-dualboost`
   (`tms320_agent.md`).
2. **Signaler le mode d'essai boucle ouverte étage 2**
   (`STAGE2_OPENLOOP_TEST` côté TMS320, tant que le MOSFET de l'étage 2
   n'est pas monté) : dans ce mode toute consigne `VOSET` non nulle est
   refusée (`REJ++`, `VOSP` reste à `0.0`). Pas encore un indicateur
   dédié — couvert pour l'instant par `REJ` et par le message « étage 1
   seul » qui continue de s'afficher malgré une demande de palier
   (détail : `claudeesp32-etage2-interface.md` §5).

## Déjà fait (ne pas refaire)

- Code 9 traité comme verrouillé (`isFaultLatched()`, `include/protocol.h`).
- Commande marche/arrêt distincte pilotant `RUN` (bouton web « Marche »,
  indépendant de « HT »).
- `V1SET` constante firmware à 70,0 V dans chaque trame (plage de
  validation 15-77 V, pas de champ IHM).
- Affichage de `REJ`/`V1SP`/`VOSP` (OLED pour `REJ`, web pour les trois).
- Les trois routes HTTP d'orchestration (`/api/telemetry`,
  `/api/setpoint`, `/api/stop`).
- Exposition de `VOSET` dans le dashboard web : 4 boutons de palier
  (OFF/200/300/400, actifs uniquement à l'arrêt — `RUN=0` imposé par
  désactivation des boutons pendant la marche) + décalage fin `±10 V`
  (0 à +100, actif en marche, désactivé en mode OFF). Arithmétique
  entière côté JS, `VOSET` envoyé = palier + décalage (détail :
  `claudeesp32-etage2-interface.md` §3.3).
