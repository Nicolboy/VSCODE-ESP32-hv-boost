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
// des qu'il coupe lui-meme les sorties PWM par securite. Rester a
// FAULT=0 tant qu'aucun defaut n'est actif -- le TMS320 doit remettre
// 0 automatiquement une fois la cause du defaut disparue (pas de "latch"
// cote protocole ; si un verrouillage manuel est necessaire, il doit
// etre gere cote TMS320, pas suppose par l'ESP32).
enum TmsFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_SHORT_CIRCUIT = 1,
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
