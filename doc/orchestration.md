# Orchestration par agent IA — dual boost

Ce document décrit comment un orchestrateur externe (agent IA, script
d'optimisation, ou simple appel manuel) peut piloter l'alimentation à
distance pour des campagnes de mesure automatisées — typiquement un
balayage de consigne avec relevé de rendement.

Ce n'est **pas** un canal de contrôle temps réel. La boucle de régulation
rapide reste entièrement sur le TMS320 ; ce document ne fait que définir
comment lui transmettre des consignes lentes (échelle de la seconde) et
en lire le résultat.

> **Révision** : correction du comportement hors-bornes (rejet, pas
> clamp), mise à jour des bornes réelles, ajout des champs
> `STATE`/`V1SP`/`VOSP`/`REJ`, correction du critère de `wait_stable`,
> et distinction des codes `FAULT` auto-résolutifs. Voir le firmware TMS
> (`control.c`, commit de bouclage de la régulation) comme source de
> vérité en cas de nouvel écart.

---

## Principe : trois couches, chacune avec un rôle strict

```
Orchestrateur (agent IA, script, humain)
        |  HTTP (WiFi, latence non critique)
        v
ESP32 — serveur HTTP + pont vers le protocole $C/$T
        |  UART 57600 bauds ($C/$T, voir ESP32-UART.md)
        v
TMS320 — seule autorité sur les bornes de securite et la regulation
```

**Ce que chaque couche a le droit de faire, et pas plus :**

- **L'orchestrateur** propose des consignes et lit des résultats. Il ne
  connaît aucune limite physique de la carte — il peut demander n'importe
  quoi, y compris des valeurs absurdes ou dangereuses.
- **L'ESP32** est un pont sans intelligence de sécurité : il traduit des
  appels HTTP en trames `$C`, et des trames `$T` en JSON. Il ne clippe
  rien, ne valide rien au-delà du format.
- **Le TMS320** est la seule couche qui applique les bornes dures et
  décide de la régulation. **Une consigne hors bornes est refusée en
  bloc** : la consigne précédente est conservée, et un compteur de
  rejets (`REJ`) est incrémenté côté TMS320. Il n'y a **pas** de clamp
  silencieux — un choix délibéré, car un clamp masquerait le refus et
  laisserait croire qu'une valeur a été appliquée alors que ce n'est pas
  le cas.

Le corollaire pour l'orchestrateur : une consigne hors plage n'est
**jamais appliquée en substitut**, elle est ignorée. Le seul moyen de
savoir qu'un refus a eu lieu est de comparer la consigne envoyée à
`V1SP`/`VOSP` renvoyés en télémétrie (voir plus bas) — ne jamais supposer
qu'une tension stable signifie qu'elle correspond à la consigne demandée.

---

## Couche ESP32 : API HTTP

À ajouter au serveur web existant (`src/web_ui.cpp`), en plus des routes
déjà utilisées par le dashboard. Spécification détaillée et à jour dans
`esp32_agent.md` (source de vérité pour cette API) — ce qui suit est un
résumé pour le contexte de l'orchestration, pas la référence normative.

### `GET /api/telemetry`

Retourne le dernier `$T` reçu, **augmenté des champs de contrôle** —
indispensables pour qu'un orchestrateur externe puisse vérifier qu'une
consigne a réellement été acceptée, pas seulement lue depuis l'IHM :

```json
{
  "freq1": 200000, "freq2": 100000,
  "duty1": 68.4, "duty2": 74.1,
  "vin": 10.2, "iin": 0.61,
  "v1": 34.8, "i1": 0.62, "t1": 38.4,
  "vout": 399.7, "i2": 0.087, "t2": 36.1,
  "iout": 0.0224,
  "fault": 0,
  "state": 4,
  "v1_sp": 35.0,
  "vout_sp": 400.0,
  "rej": 0,
  "link_ok": true,
  "age_ms": 180
}
```

| Champ | Origine | Rôle pour l'orchestrateur |
|---|---|---|
| `state` | machine à états régulation TMS320 | `4` = régulation établie ; `1`/`3` = montée/cascade en cours. Ne considérer un point de mesure valide qu'à `state == 4`. |
| `v1_sp`, `vout_sp` | dernière consigne **effectivement acceptée** par le TMS320 | Seul moyen de détecter un rejet : comparer à la consigne envoyée, pas à la mesure `v1`/`vout` elle-même. |
| `rej` | compteur de rejets côté TMS320 | Doit être relu avant et après chaque `set_setpoint` ; s'il a incrémenté, la consigne a été refusée. |

`link_ok` et `age_ms` restent calculés côté ESP32 à partir du timeout de
liaison déjà implémenté (`TmsLink::linkOk()`).

### `POST /api/setpoint`

Corps JSON, tous les champs optionnels (comportement actuel du firmware :
tous les tags `$C` sont optionnels, un tag inconnu est ignoré, `PWM1`/
`PWM2` sont acceptés mais ne pilotent plus rien directement — la
régulation gère les étages elle-même) :

```json
{ "v1_set": 35.0, "vout_set": 400.0, "run": true, "ht": true }
```

Traduit directement en une trame `$C` correspondante. La réponse doit
inclure la trame `$C` effectivement envoyée **et** un état de la
télémétrie juste après (pour permettre à l'orchestrateur de vérifier
`rej` sans faire un aller-retour supplémentaire) :

```json
{ "frame_sent": "$C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX",
  "telemetry_after": { "...": "...", "rej": 0 } }
```

### `POST /api/stop`

Raccourci envoyant `RUN=0, HT=0`. Toujours disponible même si
l'orchestrateur est dans un état incohérent — c'est la commande d'arrêt
d'urgence côté logiciel. Elle ne remplace pas les protections
matérielles (TZ, comparateurs), qui restent actives indépendamment de
tout logiciel.

---

## Bornes réelles (firmware, pas conception théorique)

| Grandeur | Plage acceptée | Comportement hors plage |
|---|---|---|
| `V1SET` | **15 à 50 V** | Rejetée, consigne précédente conservée, `REJ++` |
| `VOSET` | **200 à 500 V** | Rejetée, consigne précédente conservée, `REJ++` |

Ces plages sont celles du firmware réel, pas les bornes de conception
initiales. Un orchestrateur qui demande une valeur en dehors — y compris
une valeur qui semblait raisonnable au vu d'une ancienne version de ce
document — se verra purement et simplement ignoré, sans message d'erreur
explicite autre que l'absence de changement de `V1SP`/`VOSP` et
l'incrément de `REJ`.

---

## Codes `FAULT` : tous ne bloquent pas la régulation

| Code | Nature | Effet sur la régulation |
|---|---|---|
| `1` (EMUSTOP) | auto-résolutif | Se relève seul, ne verrouille pas |
| `8` (liaison perdue) | auto-résolutif | Se relève seul dès que la liaison revient |
| `2`–`7` | verrouillés | Coupure définitive, cycle d'alimentation requis |

Un orchestrateur qui interromprait une campagne sur **n'importe quel**
`FAULT != 0` s'arrêterait à tort sur un simple hoquet UART. Seuls les
codes `2` à `7` doivent être traités comme un arrêt définitif de la
campagne en cours.

---

## Couche orchestrateur : serveur d'outils (MCP)

Un petit serveur, exécuté sur un PC/hôte (pas sur l'ESP32), qui expose
les routes ci-dessus comme des outils appelables par un agent.

```python
# mcp_server.py
import time
import requests
from mcp.server.fastmcp import FastMCP

ESP32_BASE = "http://alim-dualboost.local"  # ou IP fixe
mcp = FastMCP("dualboost-alim")

FAULT_LATCHED = set(range(2, 8))  # 2..7 inclus : verrouilles
FAULT_TRANSIENT = {1, 8}          # EMUSTOP, liaison perdue : auto-resolutifs

@mcp.tool()
def get_telemetry() -> dict:
    """Lit l'etat courant de l'alimentation, y compris state/v1_sp/
    vout_sp/rej -- necessaires pour verifier qu'une consigne a bien
    ete acceptee et pas seulement lue depuis l'IHM."""
    return requests.get(f"{ESP32_BASE}/api/telemetry", timeout=2).json()

@mcp.tool()
def set_setpoint(v1_set: float | None = None,
                  vout_set: float | None = None,
                  run: bool = True) -> dict:
    """Envoie une nouvelle consigne V1 (15-50V) et/ou VOUT (200-500V).
    ATTENTION : une valeur hors plage est REJETEE, pas clampee -- la
    consigne precedente reste active et REJ s'incremente cote TMS320.
    Toujours relire get_telemetry() ensuite et comparer v1_sp/vout_sp
    a ce qui a ete demande avant de considerer la consigne appliquee."""
    body = {"run": run}
    if v1_set is not None:
        body["v1_set"] = v1_set
    if vout_set is not None:
        body["vout_set"] = vout_set
    return requests.post(f"{ESP32_BASE}/api/setpoint", json=body, timeout=2).json()

@mcp.tool()
def emergency_stop() -> dict:
    """Coupe immediatement la regulation et la sortie HT."""
    return requests.post(f"{ESP32_BASE}/api/stop", timeout=2).json()

@mcp.tool()
def wait_stable(target: str, tolerance: float, timeout_s: float = 15.0,
                 poll_s: float = 0.5) -> dict:
    """Attend que la mesure (`v1` ou `vout`) rejoigne sa CONSIGNE
    acceptee (v1_sp/vout_sp), pas seulement qu'elle cesse de varier :
    une consigne inatteignable (charge trop lourde, entree trop
    faible) peut saturer l'integrateur et stabiliser la tension a une
    valeur fausse -- une comparaison a la consigne evite ce piege.
    Interrompt aussi l'attente sur tout FAULT verrouille (2-7) ou perte
    de liaison prolongee ; ignore les FAULT transitoires (1, 8)."""
    if target not in ("v1", "vout"):
        raise ValueError("target doit etre 'v1' ou 'vout'")
    sp_field = "v1_sp" if target == "v1" else "vout_sp"

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        t = get_telemetry()
        fault = t.get("fault", 0)
        if fault in FAULT_LATCHED:
            return {"stable": False, "reason": f"FAULT={fault} (verrouille)", "telemetry": t}
        if not t.get("link_ok", False):
            return {"stable": False, "reason": "link_lost", "telemetry": t}

        stable = (t.get("state") == 4) and \
                 (abs(t[target] - t[sp_field]) <= tolerance)
        if stable:
            return {"stable": True, "telemetry": t}
        time.sleep(poll_s)

    return {"stable": False, "reason": "timeout", "telemetry": get_telemetry()}

if __name__ == "__main__":
    mcp.run()
```

Points volontaires à conserver dans l'implémentation finale :

- **`wait_stable` compare toujours à la consigne acceptée
  (`v1_sp`/`vout_sp`), jamais à la seule immobilité de la mesure.** Une
  saturation d'intégrateur produit une valeur parfaitement stable mais
  fausse ; comparer à la consigne évite d'enregistrer ce genre de point
  comme valide.
- **`wait_stable` a toujours un timeout** et distingue les `FAULT`
  verrouillés (2-7, qui interrompent) des transitoires (1, 8, qui
  n'interrompent pas) — un simple hoquet de liaison ne doit pas faire
  échouer toute une campagne de mesure.
- **Aucun outil ne clippe ou ne réinterprète une consigne hors plage.**
  Le rejet est entièrement décidé par le TMS320 ; le rôle du serveur MCP
  est de relayer fidèlement `REJ`/`v1_sp`/`vout_sp`, jamais de
  compenser ou de masquer un refus.

---

## Exemple d'utilisation

Prompt type, une fois le serveur MCP connecté à un agent :

> *Garde VOUT constant à 400V, fais varier V1 de 20V à 50V par pas de
> 5V, charge = 10kΩ. Trace les courbes de rendement.*

Déroulé attendu côté agent :

1. `set_setpoint(vout_set=400, run=True)`
2. `wait_stable("vout", tolerance=1.0)` — vérifier `stable: True` avant
   de continuer, et que `telemetry.rej` n'a pas augmenté depuis l'appel
   précédent
3. Pour chaque valeur de V1 dans `[20, 25, 30, 35, 40, 45, 50]` :
   - `set_setpoint(v1_set=V1)`
   - `wait_stable("v1", tolerance=0.5)`
   - si `rej` a incrémenté ou `stable: False` : signaler le point comme
     invalide plutôt que de l'inclure dans la courbe
   - `get_telemetry()`, calculer `rendement = (vout*iout) / (vin*iin)`
4. `emergency_stop()` en fin de campagne ou dès qu'une anomalie
   (FAULT verrouillé, rejet répété) est détectée
5. Tracer le graphe rendement vs V1 (artifact)

### Précision sur la charge de cette manip

**La charge 10kΩ est placée sur V1 (tension intermédiaire), pas sur
VOUT.** Correction par rapport à une hypothèse précédente de ce document
qui supposait le contraire.

À cette échelle, aucun souci de puissance : sur la plage `V1SET` réelle
(15-50V), `P = V²/R` culmine à `50²/10000 = 0,25 W` et `5 mA` — très en
dessous des marges de tout composant de l'étage 1.

**Conséquence importante sur la portée de la mesure** : avec la charge
uniquement sur V1, l'étage 2 ne consomme presque rien. Le calcul
`rendement = (vout*iout) / (vin*iin)` décrit plus haut mesure alors le
**système complet à vide côté sortie**, pas le rendement de l'étage 1
isolé.

#### Le calcul du rendement d'étage 1 ne doit **pas** utiliser `i1`

Une révision précédente de ce document proposait
`rendement_etage1 = (v1 * i1) / (vin * iin)`. **C'est faux.**

`I1` est le shunt placé dans la **source du MOSFET** : il mesure le
courant de commutation, pas le courant de sortie de l'étage. Et l'ADC
l'échantillonne à un instant fixe du cycle de découpage (près du pic du
courant d'inductance), donc c'est un échantillon instantané, pas une
moyenne. `V1 × I1` n'a par conséquent pas la dimension d'une puissance de
sortie d'étage. Ces voies existent pour la protection et le diagnostic.

Avec une charge résistive connue `R`, le calcul juste n'a pas besoin de
`i1` du tout — toutes les grandeurs qu'il emploie sont bien calibrées :

```
rendement_etage1 = (v1 * v1 / R) / (vin * iin)
```

Il suppose l'étage 2 désactivé (`vout_set=0`, mode étage 1 seul), faute de
quoi l'étage 2 consomme lui aussi sur V1 et le bilan est faussé.

#### État actuel de la carte : étage 2 non monté

Le MOSFET, la diode et l'inductance de l'étage 2 ne sont pas encore en
place. La seule campagne réalisable aujourd'hui est donc celle du
**rendement de l'étage 1**, avec `vout_set=0` (mode étage 1 seul, voir
`tms320_agent.md`) et la formule ci-dessus. C'est aussi la seule qui ne
dépende pas de `IOUT`, jamais mesuré.

Dans ce mode, `wait_stable("vout", ...)` n'a aucun sens : il n'y a pas de
sortie régulée. N'attendre que sur `v1`.

**Ce qui reste à faire avant qu'une campagne SYSTÈME produise des chiffres
exploitables**, par ordre de priorité :

1. Clarifier si la campagne vise le rendement de l'étage 1 seul ou le
   système complet (voir ci-dessus) — change le calcul, pas l'architecture
2. Mesurer `IOUT` si le système complet est visé (voir
   `mesure-cartepuissance.md` §4 — jamais mesuré à ce jour)
3. Vérifier/compléter la calibration `IIN` (un seul point de mesure
   actuellement, offset non vérifié)
4. Implémenter les trois routes HTTP côté ESP32 (spécifiées dans
   `esp32_agent.md`, pas encore confirmées existantes)

---

## Sécurité — à ne jamais oublier en écrivant ou modifiant ce système

- Le TMS320 refuse toute régulation si un `FAULT` verrouillé (2-7) est
  actif, **indépendamment de `RUN`** — un agent qui aurait mis `RUN=1`
  avant un défaut ne doit pas pouvoir le relancer automatiquement après
  (aucun acquittement automatique, cycle d'alimentation requis).
- `emergency_stop()` doit fonctionner même si le format JSON envoyé par
  l'orchestrateur est par ailleurs invalide ou incomplet.
- Ce canal HTTP/WiFi n'a aucune authentification dans ce document. Tant
  que la carte reste sur un réseau local de confiance, ce n'est pas
  bloquant ; si elle devient accessible depuis l'extérieur un jour, ce
  point devra être traité avant d'exposer `/api/setpoint`.
