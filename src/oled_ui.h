#pragma once
#include <Arduino.h>

#include "protocol.h"

namespace OledUi {
void begin();
void update(const Telemetry &t, bool linkOk);

// Luminosite/contraste OLED (u8g2.setContrast(), plage 0-255, defaut 159).
// Reglable depuis le slider de la page web (voir web_ui.cpp).
void setBrightness(uint8_t value);
uint8_t brightness();
}  // namespace OledUi
