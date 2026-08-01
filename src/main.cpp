#include <Arduino.h>

#include "config.h"
#include "oled_ui.h"
#include "tms_link.h"
#include "web_ui.h"

namespace {

// Clignotement bref de la LED RGB embarquee, vert a 30% de luminosite,
// pour indiquer que le firmware tourne (ton=0.2s, toff=3.8s).
const uint32_t STATUS_LED_ON_MS = 200;
const uint32_t STATUS_LED_OFF_MS = 3800;
const uint8_t STATUS_LED_GREEN = (uint8_t)(255 * 0.30f);
uint32_t g_lastStatusLedMs = 0;
bool g_statusLedOn = false;

void updateStatusLed() {
  uint32_t now = millis();
  uint32_t period = g_statusLedOn ? STATUS_LED_ON_MS : STATUS_LED_OFF_MS;
  if (now - g_lastStatusLedMs < period) return;
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
