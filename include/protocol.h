#pragma once

#include <Arduino.h>

// Protocole ASCII de type NMEA entre l'ESP32-C3 et le TMS320.
//
// Telemetrie TMS320 -> ESP32, periodique (regulation fermee, voir
// doc/tms320_agent.md) :
//   $T,FAULT=0,STATE=4,REJ=0,FREQ1=200000,FREQ2=100000,DUTY1=45.2,
//      DUTY2=50.0,VIN=400.5,IIN=1.20,V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,
//      I2=2.48,T2=44.8,IOUT=1.05,V1SP=200.0,VOSP=400.0*XX\n
//
// Commande ESP32 -> TMS320, a chaque changement (et rafraichissement
// periodique) :
//   $C,HT=1,V1SET=35.0,VOSET=400.0,RUN=1*XX\n
//
// XX = XOR de tous les octets entre '$' et '*', en hexadecimal majuscule.
//
// V1SET (15-50V) / VOSET (200-500V ou 0 = etage 2 desactive) / RUN sont
// des consignes : le TMS320 est seul juge de leurs bornes et REJETTE (ne
// clampe jamais) toute valeur hors plage, en gardant la consigne
// precedente et en incrementant REJ. L'ESP32 relaie, il ne valide pas --
// ne jamais dupliquer ces bornes cote firmware ESP32. V1SP/VOSP dans la
// telemetrie sont les consignes EFFECTIVEMENT acceptees (pas forcement
// egales a la derniere demandee en cas de rejet).
//
// VOSET=0 (donc VOSP=0 en retour) n'est pas une consigne de 0V : c'est la
// convention "etage 2 desactive", l'etat par defaut tant que l'etage 2
// n'est pas monte sur la carte. STATE=4 avec VOSP=0 est un fonctionnement
// normal (etage 1 seul etabli), pas une panne -- l'IHM doit le signaler
// explicitement (voir OledUi::drawDashboard()).
//
// FAULT (entier, 0-9) : code de defaut courant, transmis par le TMS320
// des qu'il coupe lui-meme les sorties PWM par securite.
//
// IMPORTANT -- deux comportements differents selon le code (voir
// isFaultLatched()) :
// - Codes TRANSITOIRES (1=EMUSTOP, 8=liaison UART perdue cote TMS320) :
//   se relevent seuls, retour spontane a FAULT_NONE possible.
// - Codes VERROUILLES (2-7, 9) : ne s'effacent JAMAIS automatiquement, ni
//   a la disparition de leur cause, ni par commande UART -- seul un cycle
//   d'alimentation de la carte les reinitialise. L'ESP32 affiche le
//   dernier code recu de maniere persistante, sans jamais supposer un
//   retour spontane a FAULT_NONE pour ces codes.
//
// Priorite quand plusieurs defauts coexistent cote TMS320 (telle
// qu'implementee, doc/CLAUDE-esp32.md) :
// OVERCURRENT_I1 > OVERCURRENT_I2 > OVERVOLT_V1 > OVERVOLT_VOUT >
// UNDERVOLT_VIN > OVERTEMP_T1 > OVERTEMP_T2 > LINK_LOST > EMUSTOP
// (purement informatif cote ESP32 : un seul code FAULT est recu a la
// fois, la priorisation est entierement decidee cote TMS320.)
//
// EMUSTOP et LINK_LOST arretent aussi l'UART cote TMS320 (attendu, pas
// une panne) : cote affichage ESP32 (isFaultLatched()/faultLabel() dans
// oled_ui.cpp et web_ui.cpp), ces deux codes sont affiches nommement
// ("SYS:EMUSTOP", "SYS:LINKLOST"), et contrairement a toute autre perte
// de liaison, l'ESP32 continue d'afficher le dernier code transitoire
// connu plutot que de basculer sur "KO" au bout du timeout de 2s -- le
// temps que le TMS320 reparte.
//
// Sur un defaut verrouille (2-7, 9), le TMS320 pilote expressement la
// broche DISCHARGE a 0 pour decharger activement le condensateur de
// sortie (~2s) ; sur EMUSTOP/LINK_LOST, DISCHARGE n'est PAS actionne (pas
// de routine de defaut). Dans tous les cas, l'icone danger reste pilotee
// par VOUT seul, independamment de FAULT (voir VOUT_DANGER_THRESHOLD,
// src/oled_ui.cpp) : le condensateur peut encore etre charge quel que
// soit le code.
enum TmsFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_EMUSTOP = 1,  // le debogueur a arrete le CPU (pas un defaut de puissance)
  FAULT_OVERCURRENT_I1 = 2,
  FAULT_OVERCURRENT_I2 = 3,
  FAULT_OVERTEMP_T1 = 4,     // T1 > 80 C
  FAULT_OVERTEMP_T2 = 5,     // T2 > 80 C
  FAULT_OVERVOLT_V1 = 6,     // V1 > 55 V
  FAULT_OVERVOLT_VOUT = 7,   // VOUT > 520 V
  FAULT_LINK_LOST = 8,       // liaison UART perdue cote TMS320
  // VIN < 9,5V pendant ~75us, typiquement en charge elevee sur l'etage 1
  // (l'elevateur compenserait en augmentant le rapport cyclique jusqu'a
  // la surintensite -- le TMS320 coupe avant). Surveillance armee apres
  // un premier franchissement de 12V, pour ne pas se declencher pendant
  // la montee de l'alimentation au demarrage. Ajoute au firmware TMS320
  // apres ESP32-UART.md (qui s'arrete a 8) -- voir doc/CLAUDE-esp32.md.
  FAULT_UNDERVOLT_VIN = 9,
};

// true pour les codes verrouilles : pas d'acquittement possible, l'IHM ne
// doit proposer aucune commande pour les effacer. false pour FAULT_NONE
// et les codes transitoires (1=EMUSTOP, 8=LINK_LOST).
//
// Ecrit comme "tout sauf les transitoires connus", plutot qu'une plage
// explicite (ex. 2-7) : un futur code ajoute cote TMS320 (comme le 9,
// ajoute apres coup) est verrouille par defaut au lieu de tomber
// silencieusement en "inconnu" et de laisser l'IHM attendre indefiniment
// un retour spontane a FAULT_NONE qui ne viendra jamais (cf.
// doc/CLAUDE-esp32.md, bug constate sur le code 9).
inline bool isFaultLatched(uint8_t fault) {
  return fault != FAULT_NONE && fault != FAULT_EMUSTOP && fault != FAULT_LINK_LOST;
}

struct Telemetry {
  float freq1 = 0;  // Hz, frequence etage 1
  float freq2 = 0;  // Hz, frequence etage 2
  float duty1 = 0;  // %, duty cycle etage 1
  float duty2 = 0;  // %, duty cycle etage 2
  float vin = 0;    // V, tension entree
  float iin = 0;    // A, courant entree
  float v1 = 0;     // V, tension etage 1
  float i1 = 0;     // A, courant shunt mosfet 1
  float t1 = 0;     // C, temperature NTC etage 1
  float vout = 0;   // V, tension etage 2 (sortie boost)
  float i2 = 0;     // A, courant shunt mosfet 2
  float t2 = 0;     // C, temperature NTC etage 2
  float iout = 0;   // A, courant sortie
  uint8_t fault = FAULT_NONE;  // voir enum TmsFault / isFaultLatched()
  uint8_t state = 0;   // machine a etats regulation TMS320, voir doc/tms320_agent.md (0-5)
  float v1Sp = 0;      // V, consigne V1 effectivement acceptee par le TMS320
  float voutSp = 0;    // V, consigne VOUT effectivement acceptee (0 = etage 2 desactive)
  uint16_t rej = 0;    // compteur cumule de consignes rejetees depuis le boot du TMS320
  uint32_t lastUpdateMs = 0;
};

struct CommandState {
  bool ht = false;        // autorise HV_EN -- sans effet tant que RUN=0 ou etage 2 non etabli
  // V, consigne tension intermediaire demandee (15-50, validee par le
  // TMS320). FIXE A 50.0 PAR DECISION DE CONCEPTION -- ne jamais exposer de
  // champ/curseur IHM dessus (voir doc/claudeesp32-etage2-interface.md
  // §3.2) : 50V est ce qui rend les 500V de sortie atteignables (rapport
  // cyclique etage 2 = 1-V1/VOUT, 0.90 a 50V contre le plafond 0.95 des
  // que V1 descend vers 25V). Doit rester dans la plage des le boot (une
  // valeur hors plage, ex. 0, est rejetee a chaque trame et REJ
  // s'incremente indefiniment sans jamais s'appliquer, defaut constate sur
  // cible -- cf. doc/esp32_agent.md) et etre emise dans CHAQUE trame $C :
  // un tag omis laisse le TMS320 sur sa valeur d'init (15V), sans que REJ
  // bouge ni qu'aucun indicateur ne le signale.
  float v1Set = 50.0f;
  float voutSet = 0;     // V, consigne sortie HT demandee (200-500 ou 0 = etage 2 desactive)
  // Demarre la regulation : c'est RUN, pas HT, qui arme le chemin de
  // puissance cote TMS320 (HT/HV_EN n'est consulte qu'ensuite). L'IHM doit
  // exposer une commande RUN distincte de HT (cf. doc/esp32_agent.md).
  bool run = false;
};
