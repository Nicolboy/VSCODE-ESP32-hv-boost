#pragma once

// Driver bas niveau pour ecran ER-OLEDM032-1 (controleur SSD1322, 256x64,
// SPI 4 fils). Adapte du tutoriel constructeur (buydisplay.com) pour
// utiliser les broches SPI personnalisees de l'ESP32-C3 (voir config.h) et
// depouille des fonctions d'affichage bitmap, inutiles pour ce projet
// (dashboard texte uniquement).

#include <Arduino.h>

#define OLED_X_MAXPIXEL 256
#define OLED_Y_MAXPIXEL 64

void er_oled_begin();
void er_oled_clear();
void er_oled_SetWindow(uint8_t Xstart, uint8_t Ystart, uint8_t Xend, uint8_t Yend);
void er_oled_char(uint8_t x, uint8_t y, const char *ascii, uint8_t mode);
void er_oled_string(uint8_t x, uint8_t y, const char *pString, uint8_t mode);
