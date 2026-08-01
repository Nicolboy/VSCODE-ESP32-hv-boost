#pragma once

// Serveur web de l'IHM : l'ESP32 cree son propre point d'acces WiFi
// (voir config.h) et sert un dashboard + controles HT/PWM1/PWM2.

namespace WebUi {

void begin();
void poll();

}  // namespace WebUi
