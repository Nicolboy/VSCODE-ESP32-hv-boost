#include "tms_link.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"

namespace TmsLink {

namespace {

Telemetry g_telemetry;
CommandState g_command;
uint32_t g_lastValidFrameMs = 0;
uint32_t g_lastCommandSentMs = 0;

char g_lineBuf[160];
uint8_t g_lineLen = 0;

const uint32_t COMMAND_SEND_PERIOD_MS = 500;
const uint32_t SIM_UPDATE_PERIOD_MS = 200;
uint32_t g_lastSimMs = 0;

uint8_t checksumOf(const char *s, size_t len) {
  uint8_t cs = 0;
  for (size_t i = 0; i < len; i++) cs ^= (uint8_t)s[i];
  return cs;
}

// Applique une valeur "TAG=xxx" a la telemetrie courante.
void applyField(const char *tag, float value) {
  if (!strcmp(tag, "FREQ1")) g_telemetry.freq1 = value;
  else if (!strcmp(tag, "FREQ2")) g_telemetry.freq2 = value;
  else if (!strcmp(tag, "DUTY1")) g_telemetry.duty1 = value;
  else if (!strcmp(tag, "DUTY2")) g_telemetry.duty2 = value;
  else if (!strcmp(tag, "VIN")) g_telemetry.vin = value;
  else if (!strcmp(tag, "IIN")) g_telemetry.iin = value;
  else if (!strcmp(tag, "V1")) g_telemetry.v1 = value;
  else if (!strcmp(tag, "I1")) g_telemetry.i1 = value;
  else if (!strcmp(tag, "T1")) g_telemetry.t1 = value;
  else if (!strcmp(tag, "VOUT")) g_telemetry.vout = value;
  else if (!strcmp(tag, "I2")) g_telemetry.i2 = value;
  else if (!strcmp(tag, "T2")) g_telemetry.t2 = value;
  else if (!strcmp(tag, "IOUT")) g_telemetry.iout = value;
}

// Parse une ligne "$T,TAG=val,TAG=val,...*XX". Retourne true si le
// checksum est valide et que la trame a bien ete appliquee.
bool parseTelemetryLine(char *line) {
  size_t len = strlen(line);
  if (len < 5 || line[0] != '$' || line[1] != 'T' || line[2] != ',') return false;

  char *star = strrchr(line, '*');
  if (!star || star - line < 3) return false;

  uint8_t rxChecksum = (uint8_t)strtol(star + 1, nullptr, 16);
  uint8_t computed = checksumOf(line + 1, star - (line + 1));
  if (rxChecksum != computed) return false;

  *star = '\0';
  char *field = line + 3;  // apres "$T,"
  char *saveptr = nullptr;
  char *tok = strtok_r(field, ",", &saveptr);
  while (tok) {
    char *eq = strchr(tok, '=');
    if (eq) {
      *eq = '\0';
      applyField(tok, atof(eq + 1));
    }
    tok = strtok_r(nullptr, ",", &saveptr);
  }
  return true;
}

void pollUart() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      g_lineBuf[g_lineLen] = '\0';
      if (parseTelemetryLine(g_lineBuf)) {
        g_telemetry.lastUpdateMs = millis();
        g_lastValidFrameMs = g_telemetry.lastUpdateMs;
      }
      g_lineLen = 0;
    } else if (c != '\r') {
      if (g_lineLen < sizeof(g_lineBuf) - 1) {
        g_lineBuf[g_lineLen++] = c;
      } else {
        g_lineLen = 0;  // ligne trop longue, on repart de zero
      }
    }
  }
}

// Simule un convertisseur en fonctionnement pour pouvoir developper et
// tester l'IHM (OLED + web) sans firmware TMS320.
void updateSimulation() {
  uint32_t now = millis();
  if (now - g_lastSimMs < SIM_UPDATE_PERIOD_MS) return;
  g_lastSimMs = now;

  float t = now / 1000.0f;
  bool running = g_command.ht;

  g_telemetry.freq1 = 100000.0f;
  g_telemetry.freq2 = 100000.0f;
  g_telemetry.duty1 = running && g_command.pwm1 ? 45.0f + 2.0f * sinf(t) : 0.0f;
  g_telemetry.duty2 = running && g_command.pwm2 ? 50.0f + 2.0f * sinf(t + 1.0f) : 0.0f;

  g_telemetry.vin = 400.0f + 1.0f * sinf(t * 0.5f);
  g_telemetry.iin = running ? 1.2f + 0.1f * sinf(t) : 0.05f;

  g_telemetry.v1 = running && g_command.pwm1 ? 200.0f + 0.5f * sinf(t) : 0.0f;
  g_telemetry.i1 = running && g_command.pwm1 ? 2.5f + 0.2f * sinf(t * 1.3f) : 0.0f;
  g_telemetry.t1 = 40.0f + 5.0f * sinf(t * 0.1f);

  g_telemetry.vout = running && g_command.pwm2 ? 200.0f + 0.5f * sinf(t + 0.3f) : 0.0f;
  g_telemetry.i2 = running && g_command.pwm2 ? 2.48f + 0.2f * sinf(t * 1.1f) : 0.0f;
  g_telemetry.t2 = 39.5f + 5.0f * sinf(t * 0.1f + 0.5f);

  g_telemetry.iout = running ? 1.05f + 0.1f * sinf(t * 0.8f) : 0.0f;

  g_telemetry.lastUpdateMs = now;
  g_lastValidFrameMs = now;
}

void sendCommandFrame() {
  char body[48];
  int n = snprintf(body, sizeof(body), "C,HT=%d,PWM1=%d,PWM2=%d", g_command.ht ? 1 : 0,
                    g_command.pwm1 ? 1 : 0, g_command.pwm2 ? 1 : 0);
  uint8_t cs = checksumOf(body, n);

  char frame[64];
  snprintf(frame, sizeof(frame), "$%s*%02X\n", body, cs);
  Serial1.print(frame);
}

}  // namespace

void begin() {
  Serial1.begin(TMS_UART_BAUD, SERIAL_8N1, PIN_TMS_UART_RX, PIN_TMS_UART_TX);
}

void poll() {
  if (TMS_DUMMY_MODE) {
    updateSimulation();
  } else {
    pollUart();
  }

  uint32_t now = millis();
  if (now - g_lastCommandSentMs >= COMMAND_SEND_PERIOD_MS) {
    g_lastCommandSentMs = now;
    sendCommandFrame();
  }
}

void setCommand(const CommandState &cmd) {
  g_command = cmd;
  sendCommandFrame();
  g_lastCommandSentMs = millis();
}

const Telemetry &telemetry() { return g_telemetry; }
const CommandState &command() { return g_command; }

bool linkOk() {
  if (TMS_DUMMY_MODE) return true;
  return (millis() - g_lastValidFrameMs) < 2000;
}

}  // namespace TmsLink
