#include "oled_ui.h"

#include <Arduino.h>

#include "er_oled.h"
#include "tms_link.h"

namespace OledUi {

namespace {

const uint32_t REFRESH_PERIOD_MS = 500;
const uint32_t PAGE_PERIOD_MS = 3000;

uint32_t g_lastRefreshMs = 0;
uint32_t g_lastPageChangeMs = 0;
uint8_t g_page = 0;
const uint8_t PAGE_COUNT = 3;

void drawLine(uint8_t y, const char *text) {
  char padded[33];
  snprintf(padded, sizeof(padded), "%-32s", text);
  er_oled_string(0, y, padded, 0);
}

void drawEntreeCommandes(const Telemetry &t, bool linkOk) {
  char l0[33], l1[33], l2[33], l3[33];
  snprintf(l0, sizeof(l0), "TMS320: %s", linkOk ? "OK" : "PERDU");
  snprintf(l1, sizeof(l1), "Vin=%.1fV Iin=%.2fA", t.vin, t.iin);
  snprintf(l2, sizeof(l2), "Iout=%.2fA", t.iout);
  snprintf(l3, sizeof(l3), "HT:%s PWM1:%s PWM2:%s", TmsLink::command().ht ? "ON " : "OFF",
           TmsLink::command().pwm1 ? "ON " : "OFF", TmsLink::command().pwm2 ? "ON " : "OFF");
  drawLine(0, l0);
  drawLine(16, l1);
  drawLine(32, l2);
  drawLine(48, l3);
}

void drawEtage1(const Telemetry &t) {
  char l1[33], l2[33];
  snprintf(l1, sizeof(l1), "F=%.0fHz D=%.1f%%", t.freq1, t.duty1);
  snprintf(l2, sizeof(l2), "V=%.1fV I=%.2fA T=%.1fC", t.v1, t.i1, t.t1);
  drawLine(0, "ETAGE 1");
  drawLine(16, l1);
  drawLine(32, l2);
  drawLine(48, "");
}

void drawEtage2(const Telemetry &t) {
  char l1[33], l2[33];
  snprintf(l1, sizeof(l1), "F=%.0fHz D=%.1f%%", t.freq2, t.duty2);
  snprintf(l2, sizeof(l2), "V=%.1fV I=%.2fA T=%.1fC", t.vout, t.i2, t.t2);
  drawLine(0, "ETAGE 2");
  drawLine(16, l1);
  drawLine(32, l2);
  drawLine(48, "");
}

}  // namespace

void begin() {
  er_oled_begin();
  er_oled_clear();
}

void update(const Telemetry &t, bool linkOk) {
  uint32_t now = millis();

  if (now - g_lastPageChangeMs >= PAGE_PERIOD_MS) {
    g_lastPageChangeMs = now;
    g_page = (g_page + 1) % PAGE_COUNT;
  }

  if (now - g_lastRefreshMs < REFRESH_PERIOD_MS) return;
  g_lastRefreshMs = now;

  switch (g_page) {
    case 0: drawEntreeCommandes(t, linkOk); break;
    case 1: drawEtage1(t); break;
    case 2: drawEtage2(t); break;
  }
}

}  // namespace OledUi
