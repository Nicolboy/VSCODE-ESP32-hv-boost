#include <Arduino.h>

#include "config.h"
#include "oled_ui.h"
#include "tms_link.h"
#include "web_ui.h"

namespace {

// Clignotement 1 Hz de la LED RGB embarquee, vert a 30% de luminosite,
// pour indiquer que le firmware tourne.
const uint32_t STATUS_LED_PERIOD_MS = 500;
const uint8_t STATUS_LED_GREEN = (uint8_t)(255 * 0.30f);
uint32_t g_lastStatusLedMs = 0;
bool g_statusLedOn = false;

void updateStatusLed() {
  uint32_t now = millis();
  if (now - g_lastStatusLedMs < STATUS_LED_PERIOD_MS) return;
  g_lastStatusLedMs = now;
  g_statusLedOn = !g_statusLedOn;
  neopixelWrite(PIN_RGB_LED, 0, g_statusLedOn ? STATUS_LED_GREEN : 0, 0);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-C3-dualboost");
  Serial.println(TMS_DUMMY_MODE ? "Mode simulation TMS320 (dummy)" : "Mode liaison TMS320 reelle");

  TmsLink::begin();
  OledUi::begin();
  WebUi::begin();
}

void loop() {
  TmsLink::poll();
  OledUi::update(TmsLink::telemetry(), TmsLink::linkOk());
  WebUi::poll();
  updateStatusLed();
}
