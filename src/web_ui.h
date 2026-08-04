#pragma once

#include <Arduino.h>

// Serveur web de l'IHM : l'ESP32 cree son propre point d'acces WiFi
// (voir config.h) et sert un dashboard + controles HT/PWM1/PWM2.

namespace WebUi {

void begin();
void poll();

// millis() de la derniere requete HTTP servie (page, /api/state ou
// /api/command) -- permet a l'IHM OLED de savoir si le dashboard web
// est actuellement consulte (ex. pour reactiver l'ecran OLED depuis
// l'economiseur).
uint32_t lastActivityMs();

}  // namespace WebUi
