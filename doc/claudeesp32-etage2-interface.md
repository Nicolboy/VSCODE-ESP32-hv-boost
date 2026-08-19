# Prompt — Interface de commande étage 2 (côté ESP32-C3)

> À donner à une session Claude Code travaillant sur le **firmware ESP32-C3**.
> Rédigé le 26/08/2026 depuis le dépôt TMS320. Le firmware TMS320 est déjà en
> place et **ne doit pas être modifié** : tout ce qui suit est côté ESP32.

---

## 1. Contexte

Alimentation haute tension pour préamplis à tubes. Deux cartes :

- **TMS320F28027** — régulation, protections, seule autorité de sécurité ;
- **ESP32-C3** — supervision : OLED SSD1322 256×64, serveur web, pont UART.

Liaison **UART 57600 8N1**, protocole ASCII `$C` / `$T` décrit dans
`ESP32-UART.md`. L'ESP32 émet une trame `$C` toutes les **500 ms** et
immédiatement à chaque changement depuis l'IHM.

**Règle cardinale : l'ESP32 n'a aucune autorité de sécurité.** Il propose des
consignes, le TMS320 seul décide, valide, refuse et verrouille. Voir
`CLAUDE-esp32.md`, qui reste la référence de comportement et prime sur ce
document en cas de divergence.

L'étage 2 (200-500 V) va être mis en service. L'IHM actuelle ne permet pas de
le piloter, et il lui manque même de quoi démarrer la conversion.

---

## 2. État actuel — ce qui manque

Relevé sur cible, documenté dans `esp32_agent.md` :

| # | Manque | Conséquence |
|---|---|---|
| 1 | **Aucune commande `RUN`** | Rien ne démarre. Le bouton « HV_EN » est mappé sur `HT`, or le TMS320 n'arme le chemin de puissance que sur `RUN=1` |
| 2 | `V1SET=0.0` émis | Hors plage (15-52), refusé à chaque trame, `REJ` s'incrémente, la consigne appliquée reste à 15 V |
| 3 | Aucun réglage de `VOSET` | L'étage 2 ne peut pas être commandé |

`REJ` est déjà affiché par l'IHM — c'est acquis, ne pas le retirer.

---

## 3. À implémenter

### 3.1 Commande `RUN` — priorité absolue

Un bouton **marche/arrêt distinct**, pilotant le tag `RUN` (`0` ou `1`).

`RUN` et `HT` sont **deux commandes indépendantes** et l'IHM doit exposer les
deux. `HT` ne démarre rien : il ne commande que la mise sous tension de la
sortie HT, et n'a d'effet qu'une fois `STATE=4` atteint.

### 3.2 `V1SET` — constante, mais **présente dans chaque trame**

`V1SET=50.0` dans **toutes** les trames `$C`. Pas de champ, pas de curseur :
la valeur est figée par décision de conception.

> ⚠️ **Ne pas « optimiser » en cessant d'émettre le tag.** Un tag absent laisse
> le TMS320 sur sa dernière valeur, et sa valeur d'initialisation est **15 V**.
> L'omission ferait donc tourner l'alimentation à 15 V au lieu de 50, sans que
> `REJ` bouge et sans qu'aucun indicateur ne le signale.

Pourquoi 50 V : c'est ce qui rend les 500 V atteignables. Le rapport cyclique
de l'étage 2 vaut `1 − V1/VOUT`, soit 0,90 à V1 = 50 V, contre 0,95 — le
plafond de l'étage — dès que V_inter descend vers 25 V.

### 3.3 `VOSET` — quatre boutons de palier + réglage fin

**Consigne émise = palier + décalage**, arithmétique en **entiers**.

**Quatre boutons de palier**, chacun exigeant `RUN=0` pour être appliqué :

| Bouton | `VOSET` émis | Effet |
|---|---|---|
| **OFF** | `0.0` | Étage 2 désactivé, mode étage 1 seul |
| **200** | `200.0` | Palier bas |
| **300** | `300.0` | |
| **400** | `400.0` | |

**Deux boutons `+` / `−`**, agissant **à la volée** (sans repasser par
`RUN=0`) : décalage de **0 à +100 V par pas de 10 V**, ajouté au palier.
Plage résultante 200-500 V, avec recouvrement entre paliers.

Trois règles :

1. **Le décalage se remet à 0 à chaque changement de palier.** Sinon un
   opérateur à 400+100 (= 500 V) qui appuie sur « 200 » obtiendrait 300 V.
   Le changement de palier passant déjà par `RUN=0`, c'est le moment naturel.
2. **Arithmétique entière**, formatage `"%d.0"`. Les bornes 200 et 500 tombent
   alors sur des valeurs exactes, et aucune consigne ne peut être refusée pour
   une erreur d'arrondi.
3. **Le bouton OFF est indispensable** : sans lui, aucun retour au mode étage 1
   seul depuis l'IHM. C'est le seul mode utilisable tant que le MOSFET de
   l'étage 2 n'est pas monté, et c'est l'état par défaut du TMS320.

---

## 4. Règles à respecter — non négociables

**Ne pas valider localement.** L'ESP32 relaie, il ne borne pas. Le TMS320 est
seul juge des limites physiques : il refuse en bloc une consigne hors plage,
conserve l'ancienne et incrémente `REJ`. Dupliquer les bornes côté ESP32
garantit qu'elles divergeront un jour. Les valeurs de ce document servent à
construire l'IHM, **pas** à filtrer ce qui part sur l'UART.

**Basculer l'étage 2 exige `RUN=0` d'abord.** Passer `VOSET` de 0 à non nul —
ou l'inverse — pendant que la machine tourne est **refusé** par le TMS320
(`REJ` s'incrémente). L'activer en marche démarrerait l'étage 2 avec un
intégrateur hors contexte, donc par un à-coup de rapport cyclique. Séquence
imposée : `RUN=0` → nouveau `VOSET` → `RUN=1`. L'IHM doit rendre cette
séquence évidente, pas laisser l'opérateur découvrir le refus.

En revanche, changer `VOSET` **à l'intérieur** de 200-500 en marche est
autorisé et rampé côté TMS320 : c'est exactement le rôle des boutons `+`/`−`.
Une variation de 100 V met environ 0,2 s à s'appliquer.

**Afficher `V1SP` et `VOSP` reçus à côté des valeurs demandées.** C'est le seul
moyen de savoir ce qui est réellement appliqué. Si demandé ≠ appliqué, la
consigne a été refusée et `REJ` s'est incrémenté.

**Ne jamais proposer d'acquittement de défaut.** Aucun tag `$C` n'efface un
défaut de puissance. Seul un cycle d'alimentation le lève. Ne pas inventer de
bouton « reset défaut ».

**Ne rien recalculer localement** de ce qui existe en télémétrie.

---

## 5. Cas particulier — mode d'essai en boucle ouverte

Le firmware TMS320 possède un drapeau de compilation `STAGE2_OPENLOOP_TEST`
(`calib.h`), utilisé tant que le MOSFET de l'étage 2 n'est pas monté. Quand il
est actif, le TMS320 **refuse toute consigne `VOSET` non nulle**.

Conséquence pour l'IHM : les boutons de palier 200/300/400 seront sans effet et
feront monter `REJ`. Ce n'est pas un bug. Si c'est simple à faire, signaler la
situation plutôt que de laisser l'opérateur appuyer sans effet — la détection
se fait sur `VOSP` qui reste à `0.0` alors qu'un palier non nul a été demandé.

---

## 6. Rappel du protocole

Trame de commande, tags optionnels, ordre libre, checksum XOR sur 2 chiffres
hexadécimaux majuscules, terminée par `\n` :

```
$C,RUN=1,V1SET=50.0,VOSET=400.0,HT=1*XX
```

| Tag | Valeurs | Description |
|---|---|---|
| `RUN` | 0 / 1 | Marche. À 0, arrêt immédiat |
| `V1SET` | 15 à 52 | Consigne V_inter (volts). **Toujours 50.0** |
| `VOSET` | 200 à 500, ou 0 | Consigne sortie HT. `0` désactive l'étage 2 |
| `HT` | 0 / 1 | Sortie HT. N'a d'effet qu'en `STATE=4` |

Télémétrie utile ici : `STATE` (0 IDLE, 1 START_S1, 2 RUN_S1, 3 START_S2,
4 RUN), `FAULT`, `REJ`, `V1SP`, `VOSP`.

Le détail complet — checksum, longueur maximale de trame, ordre imposé des
trois premiers champs de `$T`, codes de défaut — est dans **`ESP32-UART.md`**,
à lire avant de coder.

---

## 7. Critères de recette

À vérifier sur cible, ESP32 relié au TMS320 :

1. `REJ` **reste à 0** au repos, trames émises en continu. S'il monte, une
   consigne est refusée : comparer `V1SP`/`VOSP` aux valeurs demandées.
2. `V1SP` remonte **50.0** dès la première trame.
3. Bouton **OFF** + `RUN=1` → `STATE` passe 0 → 1 → 4, `VOSP=0.0`,
   `DUTY2=0.0`. C'est le mode étage 1 seul.
4. `RUN=0`, bouton **300**, `RUN=1` → `STATE` parcourt 0 → 1 → 2 → 3 → 4,
   `VOSP=300.0`. La traversée des états 2 et 3 est la preuve que l'étage 2 est
   engagé.
5. En marche, `+` cinq fois → `VOSP` monte à 350.0 sans que `STATE` quitte 4
   et sans que `REJ` bouge.
6. En marche, appuyer sur un bouton de palier → refus attendu, `REJ`
   s'incrémente, `VOSP` inchangé. L'IHM doit avoir empêché ou signalé le geste.
7. Changer de palier après `RUN=0` → le décalage est bien reparti de 0.
8. Débrancher l'UART plus de 2 s → le TMS320 replie en état sûr de lui-même.
   Vérifier que l'IHM le signale et ne prétend pas que la conversion tourne.

---

## 8. À lire avant de commencer

Dans ce dépôt, par ordre d'importance :

- **`CLAUDE-esp32.md`** — règles de comportement côté ESP32, prime sur tout ;
- **`ESP32-UART.md`** — protocole complet, codes de défaut, états ;
- **`esp32_agent.md`** — relevés sur cible et liste des manques.

Ne pas modifier le firmware TMS320. Si un besoin semble l'exiger, le signaler
plutôt que de le faire : les deux firmwares évoluent séparément, et le TMS320
porte la sécurité.
