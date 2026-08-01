#include "oled_ui.h"

#include <Arduino.h>

#include "er_oled.h"
#include "tms_link.h"

namespace OledUi {

namespace {

const uint32_t REFRESH_PERIOD_MS = 500;

uint32_t g_lastRefreshMs = 0;

void drawLine(uint8_t y, const char *text) {
  char padded[33];
  snprintf(padded, sizeof(padded), "%-32s", text);
  er_oled_string(0, y, padded, 0);
}

// Tout sur un seul ecran (4 lignes de 32 caracteres) : statut liaison et
// commandes, electrique d'entree, puis un resume par etage.
void drawDashboard(const Telemetry &t, bool linkOk) {
  char l0[33], l1[33], l2[33], l3[33];
  const CommandState &cmd = TmsLink::command();
  snprintf(l0, sizeof(l0), "TMS:%s HT:%s P1:%s P2:%s", linkOk ? "OK" : "KO",
           cmd.ht ? "ON" : "OFF", cmd.pwm1 ? "ON" : "OFF", cmd.pwm2 ? "ON" : "OFF");
  snprintf(l1, sizeof(l1), "Vin=%.0fV Iin=%.2fA Iout=%.2fA", t.vin, t.iin, t.iout);
  snprintf(l2, sizeof(l2), "E1 F%.0fk D%.0f%% V%.0fV I%.2fA T%.0fC", t.freq1 / 1000.0f,
           t.duty1, t.v1, t.i1, t.t1);
  snprintf(l3, sizeof(l3), "E2 F%.0fk D%.0f%% V%.0fV I%.2fA T%.0fC", t.freq2 / 1000.0f,
           t.duty2, t.vout, t.i2, t.t2);
  drawLine(0, l0);
  drawLine(16, l1);
  drawLine(32, l2);
  drawLine(48, l3);
}

}  // namespace

void begin() {
  er_oled_begin();
  er_oled_clear();
}

void update(const Telemetry &t, bool linkOk) {
  uint32_t now = millis();
  if (now - g_lastRefreshMs < REFRESH_PERIOD_MS) return;
  g_lastRefreshMs = now;

  drawDashboard(t, linkOk);
}

}  // namespace OledUi
