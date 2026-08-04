# Liaison UART ESP32-C3 <-> TMS320

Ce document decrit le protocole que le firmware TMS320 doit implementer
pour dialoguer avec l'ESP32-C3 (cote ESP32 deja fonctionnel, voir
`src/tms_link.cpp` / `include/protocol.h`). Le TMS320 envoie sa telemetrie
periodiquement, et recoit periodiquement les commandes de l'operateur.

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
$T,FREQ1=100000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05*7A
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
telemetrie :

| Grandeur | Point de mesure | Pleine echelle / max | Relation |
|---|---|---|---|
| `VIN` | 1,8 V @ 20 V | 30 V max | `VIN = Vadc x 11,111` |
| `IIN` | 2,4 V @ 2,7 A | 3 A max | `IIN = Vadc x 1,125` |
| `V1` (inter) | 3,2 V @ 100 V | 50 V max | `V1 = Vadc x 31,25` |
| `VOUT` | 2,8 V @ 500 V | 450 V max | `VOUT = Vadc x 178,571` |
| `IOUT` | 3,0 V @ 50 mA | 50 mA max | `IOUT = Vadc x 0,016667` (A) |
| `I1`, `I2` | shunt 0,02 Ω, gain x30 | 3 A max | `I = Vadc / 0,6 = Vadc x 1,6667` |

Note sur `V1` : le point de calibration (3,2V @ 100V) et le max indique
(50V) sont incoherents en l'etat — a reverifier avec l'auteur des
mesures avant implementation.

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
                       "VOUT=%.1f,I2=%.2f,T2=%.1f,IOUT=%.2f",
                freq1, freq2, duty1, duty2, vin, iin, v1, i1, t1, vout, i2, t2, iout);
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
  `updateSimulation()`, qui simule des valeurs realistes (utile comme
  reference de plage de valeurs attendues : `Vin` ~400V, `Iin` ~1.2A,
  `V1`/`Vout` ~200V, `I1`/`I2` ~2.5A, `T1`/`T2` ~40°C).
- `include/config.h` : `TMS_DUMMY_MODE` (actuellement `false`, liaison
  reelle active) fait tourner l'ESP32 sur la simulation interne au lieu
  de lire l'UART reel quand mis a `true` (utile pour developper l'IHM
  sans hardware TMS320 branche).

## A faire cote protocole (pas encore implemente)

- **Trame de defaut** : le TMS doit signaler lui-meme une coupure de
  securite des sorties PWM (court-circuit, surintensite I1/I2,
  temperature critique >80°C) — pas de champ dedie actuellement, la
  telemetrie ne donne que les mesures brutes. Cote ESP32,
  `OledUi::drawDashboard()` (`src/oled_ui.cpp`) affiche pour l'instant
  DEFAUT simplement quand HT n'est pas commande (`!cmd.ht`), en attendant
  cette trame. Quand elle existera, ajouter le champ correspondant dans
  `Telemetry` (`include/protocol.h`) et brancher la vraie condition dans
  `drawDashboard()`.

## Points de vigilance

- Le format hexadecimal du checksum doit etre en **majuscules** sur 2
  chiffres (`%02X`), sinon la trame sera rejetee cote ESP32 (silencieux,
  pas d'erreur visible — la telemetrie n'avancera simplement pas).
- Terminer chaque trame par `\n` uniquement (le `\r` est tolere/ignore
  cote ESP32 mais pas necessaire).
- Ne pas depasser une longueur de ligne de 200 caracteres (buffer fixe
  cote ESP32, `g_lineBuf[200]` dans `tms_link.cpp`).
- Verifier au multimetre/oscilloscope le niveau logique du TMS320 avant
  de le relier a l'ESP32-C3 (3,3V attendu).
