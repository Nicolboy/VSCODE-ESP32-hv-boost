# TMS320 — contrat de protocole (agent firmware TMS320F28027)

Ce document définit ce que **le firmware TMS320 doit émettre et
accepter** sur la liaison UART vers l'ESP32. Il est la contrepartie de
`esp32_agent.md` (qui définit ce que l'ESP32 doit faire de son côté) —
les deux documents doivent rester synchronisés, un changement de contrat
d'un côté implique une revue immédiate de l'autre.

Pour le câblage physique, la calibration analogique détaillée et la
traçabilité des mesures : voir `mesure-cartepuissance.md`. Pour
l'architecture de sécurité générale : voir `PROMPT-claude-code-TMS320.md`.

---

## Ce que le TMS320 émet : trame `$T` (télémétrie)

Toutes les **300 ms**.

```
$T,FREQ1=200000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,FAULT=0,STATE=4,V1SP=200.0,VOSP=400.0,REJ=0*XX
```

| Tag | Unité | Description |
|---|---|---|
| `FREQ1` | Hz | Fréquence de découpage étage 1 (200 kHz) |
| `FREQ2` | Hz | Fréquence de découpage étage 2 (100 kHz) |
| `DUTY1` | % | Rapport cyclique étage 1 |
| `DUTY2` | % | Rapport cyclique étage 2 |
| `VIN` | V | Tension d'entrée |
| `IIN` | A | Courant d'entrée |
| `V1` | V | Tension intermédiaire (sortie étage 1) |
| `I1` | A | Courant shunt MOSFET étage 1 |
| `T1` | °C | Température NTC étage 1 |
| `VOUT` | V | Tension de sortie HT |
| `I2` | A | Courant shunt MOSFET étage 2 |
| `T2` | °C | Température NTC étage 2 |
| `IOUT` | A | Courant de sortie |
| `FAULT` | code | Défaut courant — voir table ci-dessous |
| `STATE` | code | État de la machine de régulation — **valeurs à confirmer**, voir note |
| `V1SP` | V | Consigne V1 **effectivement acceptée** (pas forcément égale à la dernière demandée si rejet) |
| `VOSP` | V | Consigne VOUT effectivement acceptée, même remarque |
| `REJ` | compteur | Nombre cumulé de consignes rejetées depuis le boot |

Tous les champs restent optionnels à l'émission individuelle (un champ
absent garde sa dernière valeur côté ESP32), mais en pratique le firmware
les envoie tous à chaque trame.

### `STATE` — table complète (confirmée sur le firmware)

| Code | État | Signification |
|---|---|---|
| `0` | IDLE | tout coupé, décharge active, en attente de `RUN=1` |
| `1` | START_S1 | étage 1 en montée, consigne rampée depuis la tension mesurée |
| `2` | RUN_S1 | V_inter établie, étage 2 encore à zéro |
| `3` | START_S2 | étage 2 en montée |
| `4` | RUN | régulation établie — **seul état où un point de mesure est exploitable** |
| `5` | FAULT | état sûr verrouillé |

Deux pièges à connaître avant de s'appuyer sur ce champ :

**`STATE=2` ne dure qu'un seul pas de régulation, soit ~200 µs.** Aucun
interrogateur cadencé à la seconde ne l'observera jamais. Ne pas écrire de
logique qui l'attend ou qui suppose l'avoir manqué à tort.

**`STATE=4` ne signifie pas toujours « les deux étages établis ».** En mode
étage 1 seul (`VOSET=0`, voir plus bas), la machine passe directement de
`1` à `4`, et `4` veut alors dire « étage 1 établi ». Le seul moyen de
distinguer les deux cas est `VOSP` : nul en mode étage 1 seul.

### Ordre des champs : `FAULT`, `STATE`, `REJ` en tête de trame

La trame complète fait environ **175 caractères sur les 200** autorisés, et
le formateur côté TMS320 **abandonne** un champ qui ne tiendrait pas plutôt
que de déborder. Comme un champ absent laisse le récepteur sur sa dernière
valeur connue, émettre `FAULT` en fin de trame signifierait qu'un
débordement le fasse disparaître — et que l'ESP32 continue d'afficher
`FAULT=0` pendant qu'un défaut réel est actif.

L'état de sécurité passe donc avant les mesures : perdre `T2` ou `IOUT` est
sans conséquence, perdre `FAULT` ne l'est pas.

### `FAULT` — table complète

| Code | Signification | Nature |
|---|---|---|
| `0` | Aucun défaut | — |
| `1` | EMUSTOP — le débogueur a arrêté le CPU | **transitoire**, se relève seul |
| `2` | Surintensité I1 (étage 1) | verrouillé |
| `3` | Surintensité I2 (étage 2) | verrouillé |
| `4` | Température critique T1 (> 80 °C) | verrouillé |
| `5` | Température critique T2 (> 80 °C) | verrouillé |
| `6` | Survoltage V_inter (> 55 V) | verrouillé |
| `7` | Survoltage sortie HT (> 520 V) | verrouillé |
| `8` | Liaison UART perdue | **transitoire**, se relève seul |

Les codes 6 et 7 sont détectés dans l'ISR ADC par comparaison directe sur
la valeur brute, sans conversion en volts — les seuils sont pré-calculés en
counts à la compilation.

**Priorité entre défauts simultanés, telle qu'implémentée :**

```
2 > 3 > 6 > 7 > 4 > 5 > 8 > 1
```

Les survoltages passent donc **avant** les surtempératures, et non après
comme une révision antérieure de ce document le supposait. Pour la question
restée ouverte sur les deux transitoires : **`8` prime sur `1`**.

**Il n'existe volontairement pas de code « court-circuit » séparé.** Un
court-circuit franchit le même comparateur et le même seuil qu'une
surintensité ; les codes 2 et 3 couvrent les deux cas.

**Verrouillage — pas d'effacement par le protocole.** Un défaut
verrouillé (2-7) ne s'efface jamais automatiquement, ni à la disparition
de sa cause, ni par commande UART. Aucun tag `$C` ne permet de
l'acquitter. Le seul retour à `FAULT=0` sur un code verrouillé est un
cycle d'alimentation complet — choix délibéré : un défaut de puissance
doit exiger une intervention humaine.

> Rappel structurel : la protection réelle (comparateurs + Trip Zone
> matériel) ne dépend jamais de cette liaison UART. `FAULT` est un simple
> report d'information, envoyé après coup — la coupure a déjà eu lieu en
> matériel avant même que la trame ne soit construite.

---

## Ce que le TMS320 accepte : trame `$C` (commande)

```
$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX
```

| Tag | Valeurs | Statut | Description |
|---|---|---|---|
| `HT` | 0 ou 1 | optionnel | Sortie HT activée/désactivée |
| `PWM1` | 0 ou 1 | optionnel, **accepté mais sans effet** | La régulation pilote les étages elle-même |
| `PWM2` | 0 ou 1 | optionnel, **accepté mais sans effet** | idem |
| `V1SET` | float | optionnel | Consigne tension intermédiaire, **15 à 50 V** |
| `VOSET` | float | optionnel | Consigne sortie HT, **200 à 500 V**, ou **0 = étage 2 désactivé** |
| `RUN` | 0 ou 1 | optionnel | Autorise la régulation à suivre `V1SET`/`VOSET` |

Tous les tags sont désormais optionnels, y compris `HT` — un tag inconnu
est ignoré silencieusement. Ce comportement permet aux deux firmwares
d'évoluer indépendamment sans se bloquer mutuellement sur une trame
incomplète.

### `VOSET=0` : mode étage 1 seul

`VOSET` **exactement nul** n'est pas une consigne de 0 V, c'est la
convention qui **désactive l'étage 2** :

- la machine s'arrête à `STATE=4` dès que l'étage 1 est établi, sans jamais
  passer par `2` ni `3` ;
- le rapport cyclique de l'étage 2 est forcé à zéro à chaque pas, sa sortie
  ePWM reste inhibée (`AQCSFRC`) et sa porte ET n'est pas armée ;
- **`HT=1` reste sans effet** : il n'y a pas de sortie HT à mettre sous
  tension. `VOSP` renvoie `0.0`.

Ce mode existe parce que sans lui la machine resterait bloquée
indéfiniment en `STATE=3`, l'intégrateur saturé à 95 % : une sortie ne peut
pas atteindre 200 V quand le MOSFET, la diode et l'inductance de l'étage 2
ne sont pas montés. C'est **l'état par défaut au démarrage** du TMS320 —
le défaut le plus sûr est de ne rien commander sur un étage absent.

Zéro est sans ambiguïté : ce n'est pas une consigne plausible, et toute
valeur strictement comprise entre 0 et 200 reste refusée comme avant. Une
valeur négative est refusée elle aussi.

**Basculer entre les deux modes exige `RUN=0` d'abord.** Une trame qui
activerait ou désactiverait l'étage 2 alors que la machine tourne est
**refusée** (`REJ` s'incrémente) : l'activer en marche ferait démarrer
l'étage 2 avec un intégrateur et une rampe hors contexte, donc par un
à-coup de rapport cyclique ; le désactiver couperait la sortie sans passer
par l'état sûr. La séquence correcte est `RUN=0`, puis le nouveau `VOSET`,
puis `RUN=1`.

### Comportement hors bornes : **rejet, jamais clamp**

Une valeur `V1SET`/`VOSET` hors de sa plage est **refusée en bloc** :

- la consigne précédemment acceptée est conservée telle quelle
- `REJ` est incrémenté
- `V1SP`/`VOSP` (dans la télémétrie) ne changent pas

Ce choix est délibéré : un clamp silencieux masquerait le refus et
laisserait croire qu'une valeur a été appliquée alors que ce n'est pas le
cas. C'est à `V1SP`/`VOSP`/`REJ` de porter cette information, jamais à un
comportement de clamp implicite.

`RUN` démarre à `0` par défaut au boot : aucune régulation ne s'engage
tant qu'il n'a pas été explicitement mis à `1`.

Un `FAULT` verrouillé (2-7) interrompt immédiatement la régulation
(retour à l'état sûr, PWM inhibés) **indépendamment de l'état de `RUN`**.
Les `FAULT` transitoires (1, 8) ne doivent pas interrompre une régulation
en cours au-delà de leur propre durée.

---

## Circuit de décharge active — rôle du TMS320

Piloté sur **GPIO5 / broche 40**, logique inversée, via un pont résistif
référencé à la HT elle-même (pas une pull-down sur le rail logique
3,3 V) : le MOSFET de décharge est rendu passant dès que GPIO5 ne pilote
plus activement un état bas — CPU planté, halte debug, ou alimentation
logique du TMS320 coupée alors que la HT est encore présente. Ce
failsafe ne dépend à aucun moment du rail 3,3 V.

Conséquence pour le firmware : **ne jamais initialiser GPIO5 dans un état
qui bloquerait la décharge** avant que le système ait explicitement
validé un état sûr. L'état par défaut au reset (avant toute écriture
logicielle) doit laisser le pont reprendre la main.

---

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), hexadécimal majuscule
sur 2 chiffres (`%02X`).

> **Piège déjà rencontré, documenté pour ne pas le reproduire.** Décoder
> le checksum avec `strtol` sur un buffer non terminé par `\0` fait lire
> au-delà de la trame. Et sur C28x, `uint8_t` fait 16 bits (pas
> d'adressage par octet) : un cast `(uint8_t)` ne tronque rien, et un
> `"7D"` suivi d'un résidu `"7D"` donne `0x7D7D`, conservé tel quel —
> toute trame était alors rejetée. Décoder **exactement deux chiffres**,
> de manière bornée.

---

## Calibration analogique (résumé — détail complet dans `mesure-cartepuissance.md`)

| Grandeur | Relation | Statut |
|---|---|---|
| `VIN` | `VIN = Vadc x 11,11` | mesuré |
| `IIN` | `IIN = Vadc x 1,2019` | mesuré (1 point seulement) |
| `V1` | `V1 = Vadc x 31,25` | mesuré |
| `VOUT` | `VOUT = Vadc x 181,82` | mesuré |
| `IOUT` | `IOUT = Vadc x 0,016667` | **jamais mesuré** |
| `I1` | `I1 = (Vadc - 0,0473) / 0,631` | provisoire (ampli MCP6001 de banc, à refaire avec TLV9151) |
| `I2` | `I2 = (Vadc - 0,0340) / 0,637` | provisoire, idem |

### `I1` et `I2` ne sont **pas** des courants moyens — ne jamais les utiliser pour un calcul de puissance

Les shunts sont dans la **source des MOSFET** : ils mesurent le courant de
commutation, pas le courant de sortie de l'étage. Et l'ADC échantillonne à
un instant fixe du cycle (déclenchement à `CTR=ZERO`, `I1` converti environ
3,5 µs plus tard, soit près du **pic** du courant d'inductance à D = 0,8) —
c'est un échantillon instantané, pas une moyenne.

Conséquence : `V1 × I1` n'a **pas** la dimension d'une puissance de sortie
d'étage. Ces deux voies existent pour la protection et le diagnostic, pas
pour la mesure de rendement.

Avec une charge résistive connue `R` sur V_inter, le rendement de l'étage 1
se calcule sans `I1` du tout, à partir de grandeurs bien calibrées :

```
rendement_etage1 = (V1² / R) / (VIN × IIN)
```

Ce calcul suppose l'étage 2 désactivé (`VOSET=0`), faute de quoi l'étage 2
consomme lui aussi sur V_inter et le bilan est faussé.

NTC : `R_ntc = R_fixe x (VREF - Vadc) / Vadc`, seuil de coupure 80 °C
provisoire (thermistances à 5 mm des MOSFET, mesurent le cuivre, pas la
jonction).

---

## Points ouverts — état

Les quatre points ouverts de la révision précédente sont **traités** :

1. ~~Signification des codes `FAULT` 6 et 7~~ → survoltages, table ci-dessus.
2. ~~Table complète de `STATE`~~ → six états documentés, avec les deux
   pièges (`2` transitoire, `4` ambigu en mode étage 1 seul).
3. ~~Priorité entre `FAULT=1` et `FAULT=8`~~ → `8 > 1`, ordre complet
   `2 > 3 > 6 > 7 > 4 > 5 > 8 > 1`.
4. `IOUT` **reste à mesurer** — confirmé. La valeur de `calib.h` est
   théorique, jamais vérifiée sur la carte.

### Ce qui reste réellement ouvert côté matériel

- `IOUT` jamais mesuré, `IIN` calibré sur un seul point avec offset non
  vérifié : **aucune courbe de rendement système n'est fiable** tant que
  ces deux voies ne sont pas reprises. Le rendement de l'étage 1 seul,
  lui, est calculable dès maintenant (formule plus haut).
- `I1`/`I2` à recalibrer **entièrement, offset compris**, après le
  remplacement des MCP6001 par des TLV9151.
- MOSFET de décharge IRF840 en 500 V alors que la sortie peut atteindre
  500 V et couper à 520 V : **à remplacer par un 600 V** avant toute mise
  sous tension de l'étage 2.
- Sens de la formule NTC à valider avec une résistance asymétrique
  (4,7 kΩ ≈ 47 °C) : 10 k/10 k ne permet pas de discriminer.
