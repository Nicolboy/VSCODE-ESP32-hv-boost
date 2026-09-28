#pragma once

// Liaison UART vers le TMS320
#define PIN_TMS_UART_TX 7
#define PIN_TMS_UART_RX 6
#define TMS_UART_BAUD   57600

// LED RGB embarquee (WS2812) de l'ESP32-C3-DevKitM-1
#define PIN_RGB_LED 8

// Ecran OLED ER-OLEDM032-1 (SSD1322, SPI 4 fils)
#define PIN_OLED_MOSI 5
#define PIN_OLED_CLK  4
#define PIN_OLED_CS   10
#define PIN_OLED_RST  3
#define PIN_OLED_DC   1

// Le firmware TMS320 emet reellement des trames $T,...*XX sur l'UART :
// on lit/ecrit le vrai Serial1 au lieu de simuler la telemetrie en local.
// Repasser a true pour developper l'IHM sans hardware TMS320 branche.
#define TMS_DUMMY_MODE false

// Recopie sur Serial (USB, moniteur PlatformIO) de chaque trame TMS320
// recue/envoyee sur Serial1, pour debug sans adaptateur UART externe.
#define TMS_DEBUG_ECHO true

// WiFi/serveur web de l'IHM : l'ESP32 cree son propre point d'acces
// (mode AP), pas de dependance a un reseau local existant.
// SSID et mot de passe dans credentials.h (jamais versionne) -- copier
// include/credentials.h.example vers include/credentials.h et l'adapter.
#include "credentials.h"
