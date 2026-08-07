#pragma once

#include <Arduino.h>

// Protocole ASCII de type NMEA entre l'ESP32-C3 et le TMS320.
//
// Telemetrie TMS320 -> ESP32, periodique :
//   $T,FREQ1=100000,FREQ2=100000,DUTY1=45.2,DUTY2=50.0,VIN=400.5,IIN=1.20,
//      V1=200.3,I1=2.50,T1=45.2,VOUT=200.1,I2=2.48,T2=44.8,IOUT=1.05,
//      FAULT=0*XX\n
//
// Commande ESP32 -> TMS320, a chaque changement (et rafraichissement
// periodique) :
//   $C,HT=1,PWM1=1,PWM2=0*XX\n
//
// XX = XOR de tous les octets entre '$' et '*', en hexadecimal majuscule.
//
// FAULT (entier, 0-5) : code de defaut courant, transmis par le TMS320
// des qu'il coupe lui-meme les sorties PWM par securite.
//
// IMPORTANT -- deux comportements differents selon le code :
// - EMUSTOP (1) : peut etre acquitte, le TMS320 repart (retour spontane
//   a FAULT_NONE possible une fois le breakpoint retire).
// - OVERCURRENT_I1/I2, OVERTEMP_T1/T2 (2-5) : ne s'effacent JAMAIS
//   automatiquement, ni a la disparition de leur cause, ni par commande
//   UART -- seul un cycle d'alimentation de la carte les reinitialise.
// Dans tous les cas, l'ESP32 affiche simplement le dernier code recu, de
// maniere persistante, sans jamais supposer un retour spontane a
// FAULT_NONE pour les codes 2-5.
//
// Priorite quand plusieurs defauts coexistent cote TMS320 :
// OVERCURRENT_I1 > OVERCURRENT_I2 > OVERTEMP_T1 > OVERTEMP_T2 > EMUSTOP
// (EMUSTOP a la priorite la plus BASSE malgre son code=1 : en
// developpement il se declenche a chaque halte du debogueur et ne doit
// jamais masquer un defaut de puissance reel).
//
// EMUSTOP arrete a la fois l'UART et le PWM cote TMS320 (attendu, pas
// une panne) : cote affichage ESP32 (OledUi::isRealFault()/faultLabel(),
// src/oled_ui.cpp), EMUSTOP EST affiche nommement ("SYS:EMUSTOP"), et
// contrairement a toute autre perte de liaison, l'ESP32 continue
// d'afficher "EMUSTOP" (derniere valeur connue) plutot que de basculer
// sur "KO" au bout du timeout de 2s -- le temps que le TMS320 reparte.
//
// Sur un defaut I/T (2-5), le TMS320 pilote expressement la broche
// DISCHARGE a 0 pour decharger activement le condensateur de sortie
// (~2s) ; sur EMUSTOP, DISCHARGE n'est PAS actionne (pas de routine de
// defaut, juste un CPU arrete). Dans les deux cas, l'icone danger reste
// pilotee par VOUT seul, independamment de FAULT (voir
// VOUT_DANGER_THRESHOLD, src/oled_ui.cpp) : le condensateur peut encore
// etre charge quel que soit le code.
enum TmsFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_EMUSTOP = 1,  // le debogueur a arrete le CPU (pas un defaut de puissance)
  FAULT_OVERCURRENT_I1 = 2,
  FAULT_OVERCURRENT_I2 = 3,
  FAULT_OVERTEMP_T1 = 4,  // T1 > 80 C
  FAULT_OVERTEMP_T2 = 5,  // T2 > 80 C
};

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
  uint8_t fault = FAULT_NONE;  // voir enum TmsFault
  uint32_t lastUpdateMs = 0;
};

struct CommandState {
  bool ht = false;    // sortie HT activee/desactivee
  bool pwm1 = false;  // PWM etage 1 activee/desactivee
  bool pwm2 = false;  // PWM etage 2 activee/desactivee
};
