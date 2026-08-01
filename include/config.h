#pragma once

// Liaison UART vers le TMS320
#define PIN_TMS_UART_TX 7
#define PIN_TMS_UART_RX 6
#define TMS_UART_BAUD   115200

// LED RGB embarquee (WS2812) de l'ESP32-C3-DevKitM-1
#define PIN_RGB_LED 8

// Ecran OLED ER-OLEDM032-1 (SSD1322, SPI 4 fils)
#define PIN_OLED_MOSI 5
#define PIN_OLED_CLK  4
#define PIN_OLED_CS   10
#define PIN_OLED_RST  3
#define PIN_OLED_DC   1

// Le firmware TMS320 n'est pas encore programme : on simule la telemetrie
// en local le temps que le hardware TMS320 tourne. Passer a false quand le
// TMS320 emet reellement des trames $T,...*XX sur l'UART.
#define TMS_DUMMY_MODE true

// WiFi/serveur web de l'IHM : l'ESP32 cree son propre point d'acces
// (mode AP), pas de dependance a un reseau local existant.
#define WIFI_AP_SSID     "AlimHT"
#define WIFI_AP_PASSWORD "REDACTED_WIFI_PASSWORD"
