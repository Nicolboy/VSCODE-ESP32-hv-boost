# Liaison UART ESP32-C3 <-> TMS320

Ce document decrit le protocole que le firmware TMS320 doit implementer
pour dialoguer avec l'ESP32-C3 (cote ESP32 deja fonctionnel, voir
`src/tms_link.cpp` / `include/protocol.h`). Le TMS320 envoie sa telemetrie
periodiquement, et recoit periodiquement les commandes de l'operateur.

> **Mise a jour** : coefficients de calibration remplaces par les valeurs
> mesurees sur la carte reelle (voir `mesure-cartepuissance.md` pour le
> detail des mesures et ce qui reste a faire). La remarque precedente sur
> une incoherence de `V1` etait un malentendu de formulation, pas une
> erreur de calcul — voir note dans le tableau ci-dessous.

## Cablage

| Signal | ESP32-C3 (config.h) | TMS320 |
|---|---|---|
| ESP32 TX -> TMS320 RX | GPIO7 | RX |
| ESP32 RX <- TMS320 TX | GPIO6 | TX |
| GND commun | GND | GND |

A verifier avant de cabler : le TMS320 doit etre en logique 3,3V (comme
l'ESP32-C3). Si sa liaison serie sort en 5V, prevoir un level shifter
avant de relier les deux cartes.

## Parametres UART

- Vitesse : **57600 bauds**
- Format : **8N1** (8 bits de donnees, pas de parite, 1 bit de stop)
- Cote ESP32 : `Serial1`, defini dans `TmsLink::begin()`
  (`src/tms_link.cpp`)

## Format des trames

Protocole ASCII texte, inspire du format NMEA, terminé par `\n` (LF).
Chaque trame est encadree par `$` ... `*XX`, ou `XX` est un checksum XOR
en hexadecimal (2 chiffres, majuscules).

### Telemetrie : TMS320 -> ESP32 (periodique)

```
$T,FREQ1=100000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,FAULT=0*XX
```

Champs (`TAG=valeur`, separes par des virgules, ordre libre) :

| Tag | Unite | Description |
|---|---|---|
| `FREQ1` | Hz | Frequence de decoupage etage 1 |
| `FREQ2` | Hz | Frequence de decoupage etage 2 |
| `DUTY1` | % | Rapport cyclique etage 1 |
| `DUTY2` | % | Rapport cyclique etage 2 |
| `VIN` | V | Tension d'entree |
| `IIN` | A | Courant d'entree |
| `V1` | V | Tension etage 1 |
| `I1` | A | Courant shunt MOSFET etage 1 |
| `T1` | °C | Temperature NTC etage 1 |
| `VOUT` | V | Tension de sortie (sortie boost, etage 2) |
| `I2` | A | Courant shunt MOSFET etage 2 |
| `T2` | °C | Temperature NTC etage 2 |
| `IOUT` | A | Courant de sortie |
| `FAULT` | code 0-5 | Defaut courant (voir tableau ci-dessous) |

### Codes `FAULT`

Transmis par le TMS320 des qu'il coupe lui-meme les sorties PWM par
securite (protection integree au TMS320, l'ESP32 ne fait que l'afficher) :

| Code | Signification |
|---|---|
| `0` | Aucun defaut (fonctionnement normal) |
| `1` | Court-circuit |
| `2` | Surintensite I1 |
| `3` | Surintensite I2 |
| `4` | Temperature critique T1 (>80°C) |
| `5` | Temperature critique T2 (>80°C) |

Remettre `FAULT=0` automatiquement des que la cause a disparu (pas de
"latch" gere par le protocole — un verrouillage manuel eventuel doit
etre implemente cote TMS320, l'ESP32 se contente d'afficher le dernier
code recu). Cote ESP32, `FAULT` pilote directement l'affichage
`DEFAUT` (clignotant, colonne 3 de l'OLED, remplace `HT`/`OFF`) et
l'icone danger (n'apparait jamais tant que `FAULT != 0`, meme si HT est
commande et Vout > 50V) -- voir `OledUi::drawDashboard()`
(`src/oled_ui.cpp`) et l'enum `TmsFault` (`include/protocol.h`).

> Rappel important cote firmware TMS320 (voir
> `PROMPT-claude-code-TMS320.md` §2) : la protection reelle (comparateurs
> + Trip Zone matériel) ne depend jamais de cette liaison UART. `FAULT`
> n'est qu'un report d'information vers l'IHM, envoye apres coup ; la
> coupure elle-meme a deja eu lieu en matériel avant meme que la trame ne
> soit construite.

**Frequence d'envoi recommandee : toutes les 200-500 ms.** L'ESP32
considere la liaison perdue (`TMS:KO` affiche sur l'OLED/web) si aucune
trame `$T,...*XX` valide n'est recue depuis plus de **2 secondes**
(`TmsLink::linkOk()`).

Tous les champs sont optionnels a l'envoi individuel (un champ absent
garde sa derniere valeur connue cote ESP32), mais en pratique il est plus
simple d'envoyer tous les champs a chaque trame.

### Calibration des mesures analogiques (cote TMS320)

Coefficients de mise a l'echelle a appliquer sur la lecture ADC (`Vadc`,
en volts) pour obtenir la valeur physique a inserer dans la trame
telemetrie. **Valeurs mesurees sur la carte reelle**, sauf mention
contraire — voir `mesure-cartepuissance.md` pour le detail des mesures et
la tracabilite complete.

| Grandeur | Point de mesure | Pleine echelle reelle (3,3V) | Relation | Statut |
|---|---|---|---|---|
| `VIN` | mesure : coef 0,09 | 36,7 V | `VIN = Vadc x 11,11` | ✅ mesure |
| `IIN` | mesure : 0,208V @ 250mA | 3,97 A | `IIN = Vadc x 1,2019` | ✅ mesure (1 point) |
| `V1` (inter) | mesure : coef 0,032 | 103,1 V (usage limite a 50V max) | `V1 = Vadc x 31,25` | ✅ mesure |
| `VOUT` | mesure : coef 0,0055 | 600 V | `VOUT = Vadc x 181,82` | ✅ mesure |
| `IOUT` | valeur theorique non verifiee | 50 mA (a confirmer) | `IOUT = Vadc x 0,016667` (A) | ⚠️ a mesurer |
| `I1` (shunt etage 1) | mesure sur banc, **ampli MCP6001 provisoire** | 5,15 A | `I1 = (Vadc - 0,0473) / 0,631` | ⚠️ provisoire, a refaire avec TLV9151 |
| `I2` (shunt etage 2) | mesure sur banc, **ampli MCP6001 provisoire** | 5,12 A | `I2 = (Vadc - 0,0340) / 0,637` | ⚠️ provisoire, a refaire avec TLV9151 |

**Notes :**

- **V1** : la remarque precedente sur une incoherence entre le point de
  calibration (3,2V @ 100V) et le "max 50V" indique etait un malentendu
  de formulation, pas une erreur de calcul. Le pont diviseur donne
  effectivement une pleine echelle reelle de 103 V (coef 0,032 mesure,
  confirme identique a la valeur theorique de conception) ; la carte
  n'est simplement utilisee que jusqu'a 50V en usage normal (etage 1 =
  10V -> 50V), ce qui laisse une bonne marge sur l'ADC. Le calcul
  `V1 = Vadc x 31,25` est correct sur toute la plage 0-103V.
- **I1/I2** : contrairement aux autres voies, le gain differe legerement
  entre les deux etages (0,631 vs 0,637 V/A, ~1% d'ecart du a la
  tolerance des resistances) et chaque voie a un offset non negligeable
  (47,3 mV / 34,0 mV) qui n'est pas nul a courant nul. Ne pas utiliser un
  gain unique pour les deux voies. **Ces deux formules sont mesurees avec
  le MCP6001 monte provisoirement sur le banc de test** ; le composant
  definitif sera le TLV9151 (meilleure precision, meilleure bande
  passante). Le gain devrait rester valable au remplacement (fixe par le
  reseau de resistances), mais l'offset devra etre integralement
  remesure. Ne pas figer ces constantes comme definitives.
- **IOUT** : aucune mesure reelle a ce jour, valeur theorique de
  conception conservee par defaut. A remplacer des que mesure.

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
chaque changement d'etat depuis l'IHM (OLED/web). Le TMS320 doit
simplement appliquer le dernier etat recu et valide (checksum correct).

## Calcul du checksum

XOR de tous les octets entre `$` et `*` (exclus), formate en hexadecimal
majuscule sur 2 chiffres (`%02X`).

Exemple en C pour une trame a construire :

```c
uint8_t checksum_of(const char *s, int len) {
    uint8_t cs = 0;
    for (int i = 0; i < len; i++) cs ^= (uint8_t)s[i];
    return cs;
}

// Construction d'une trame telemetrie :
char body[128];
int n = sprintf(body, "T,FREQ1=%.0f,FREQ2=%.0f,DUTY1=%.1f,DUTY2=%.1f,"
                       "VIN=%.1f,IIN=%.2f,V1=%.1f,I1=%.2f,T1=%.1f,"
                       "VOUT=%.1f,I2=%.2f,T2=%.1f,IOUT=%.2f,FAULT=%d",
                freq1, freq2, duty1, duty2, vin, iin, v1, i1, t1, vout, i2, t2, iout, fault_code);
uint8_t cs = checksum_of(body, n);
char frame[160];
sprintf(frame, "$%s*%02X\n", body, cs);
// envoyer frame sur l'UART
```

Pour la reception des trames `$C,...*XX` : appliquer le meme calcul sur
la partie entre `$` et `*`, comparer au checksum recu, et ignorer la
trame si ca ne correspond pas (trame corrompue).

## Cote ESP32 : ce qui existe deja

- `include/protocol.h` : structures `Telemetry` et `CommandState`,
  description du protocole (source de verite en cas de doute).
- `src/tms_link.cpp` : parsing des trames `$T,...`, envoi des trames
  `$C,...`, gestion du timeout de liaison. Contient aussi
  `updateSimulation()`, qui simule des valeurs realistes : `Vin` ~20V,
  `Iin` ~2.5A, `V1` ~40V, `Vout` ~400V, `I1`/`I2` ~2A, `T1`/`T2` ~40°C
  (deja coherent avec les plages mesurees ci-dessus, verifie dans le
  code — une note precedente affirmait le contraire par erreur, en se
  basant sur une vieille description perimee de ce fichier plutot que
  sur le code reel).
- Bornes d'affichage des bargraphs OLED (`src/oled_ui.cpp`) et jauges
  web (`src/web_ui.cpp`), volontairement **en dessous** de la pleine
  echelle ADC reelle (marge d'usage normal, pas la limite physique du
  capteur) : `VIN_MAX=25V`, `IIN_MAX=3A`, `VOUT_MIN=200V`/`MAX=500V`,
  `IOUT_MAX=50mA`, `I1_MAX=I2_MAX=3A`.
- `include/config.h` : `TMS_DUMMY_MODE` (actuellement `false`, liaison
  reelle active) fait tourner l'ESP32 sur la simulation interne au lieu
  de lire l'UART reel quand mis a `true` (utile pour developper l'IHM
  sans hardware TMS320 branche).

## Points de vigilance

- Le format hexadecimal du checksum doit etre en **majuscules** sur 2
  chiffres (`%02X`), sinon la trame sera rejetee cote ESP32 (silencieux,
  pas d'erreur visible — la telemetrie n'avancera simplement pas).
- Terminer chaque trame par `\n` uniquement (le `\r` est tolere/ignore
  cote ESP32 mais pas necessaire).
- Ne pas depasser une longueur de ligne de 200 caracteres (buffer fixe
  cote ESP32, `g_lineBuf[200]` dans `tms_link.cpp`).
- Verifier au multimetre/oscilloscope le niveau logique du TMS320 avant
  de le relier a l'ESP32-C3 (3,3V attendu) — **fait, carte CPU validee**.
