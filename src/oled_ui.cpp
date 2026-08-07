#include "oled_ui.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <WiFi.h>
#include <math.h>

#include "config.h"            // PIN_OLED_MOSI / CLK / CS / DC / RST, WIFI_AP_SSID
#include "tms_link.h"
#include "web_ui.h"             // WebUi::lastActivityMs(), reveil ecran si dashboard web consulte
#include "lebel_logo_xbm.h"    // logo splash 256x64 (deux moities 128px)
#include "warning_icon_xbm.h"  // icone danger principale 40x40

namespace OledUi {
namespace {

// Memes broches que dans l'ancien er_oled_begin() : SPI matériel avec
// broches personnalisees (SCK/MOSI ne sont pas les pins HW SPI par
// defaut de l'ESP32-C3, d'ou l'appel explicite a SPI.begin() dans begin()
// ci-dessous, avant u8g2.begin()).
//
// NB : "_1_"/"_2_"/"_F_" dans le nom du constructeur u8g2 designent le
// mode de buffer (taille memoire), pas une variante de cablage du
// panneau -- il n'y a donc qu'un seul constructeur "full buffer" HW SPI
// pour ce controleur. Le remap segment/COM par defaut d'u8g2 ne
// correspondait pas au cablage de notre panneau (image coupee en deux
// moities gauche/droite inversees + miroir horizontal) ; corrige dans
// begin() en reappliquant le registre remap connu-fonctionnel de
// l'ancien driver maison (voir legacy/er_oled.cpp).
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(
    U8G2_R0,
    /* cs=*/ PIN_OLED_CS,
    /* dc=*/ PIN_OLED_DC,
    /* reset=*/ PIN_OLED_RST);

const uint32_t REFRESH_PERIOD_MS = 300;
uint32_t g_lastRefreshMs = 0;

// Luminosite/contraste. g_brightness est la position du slider web
// (0-255, valeur brute conservee telle quelle pour re-synchroniser le
// slider -- voir OledUi::brightness()) ; 159 = valeur par defaut de la
// sequence d'init u8g2 pour ce controleur (u8x8_d_ssd1322.c, 0xC1).
//
// L'oeil percoit la luminosite de facon logarithmique : un mapping
// lineaire slider->registre 0xC1 concentre toute la variation
// perceptible dans le dernier tiers de la course (0-150 ne change
// quasiment rien a l'oeil, 200-255 fait toute la difference -- constate
// sur le hardware reel). GAMMA est configurable pour affiner apres test
// visuel (2.2 = valeur de depart standard, cf. courbes gamma d'affichage).
const float BRIGHTNESS_GAMMA = 2.2f;

uint8_t sliderToContrast(uint8_t slider_0_255) {
  float normalized = slider_0_255 / 255.0f;
  float corrected = powf(normalized, BRIGHTNESS_GAMMA);
  return (uint8_t)(corrected * 255.0f + 0.5f);
}

uint8_t g_brightness = 159;

// ---------------------------------------------------------------------
// Splash logo (voir doc/OLED-u8g2.md pour l'historique des problemes
// d'affichage rencontres et resolus sur ce panneau).
// ---------------------------------------------------------------------

// Logo remonte de 4px : drawXBMP ne prend pas de y negatif (u8g2_uint_t
// est non signe), donc on saute les 4 premieres lignes de la source
// (deja du blanc) et on reduit la hauteur d'autant.
const uint8_t LOGO_Y_SHIFT = 4;
const uint8_t LOGO_HALF_STRIDE = LEBEL_LOGO_HALF_W / 8;
const uint8_t LOGO_DRAW_H = LEBEL_LOGO_HEIGHT - LOGO_Y_SHIFT;

const uint32_t SPLASH_ANIM_MS = 500;
const uint8_t SPLASH_ANIM_FRAMES = 20;
const uint32_t SPLASH_HOLD_MS = 4000;

void drawLogo(uint16_t x) {
  const uint8_t *leftBits = lebel_logo_left + LOGO_Y_SHIFT * LOGO_HALF_STRIDE;
  const uint8_t *rightBits = lebel_logo_right + LOGO_Y_SHIFT * LOGO_HALF_STRIDE;
  u8g2.drawXBMP(x, 0, LEBEL_LOGO_HALF_W, LOGO_DRAW_H, leftBits);
  u8g2.drawXBMP(x + LEBEL_LOGO_HALF_W, 0, LEBEL_LOGO_HALF_W, LOGO_DRAW_H, rightBits);
}

// Dessine "s" caractere par caractere a partir d'un x signe : les
// caracteres dont la position tombe encore hors ecran (x < 0) ne sont
// pas dessines. u8g2.drawStr() n'accepte pas de x negatif
// (u8g2_uint_t est non signe) -- c'est le seul moyen de faire "entrer"
// du texte depuis en dehors de l'ecran.
void drawStrFromOffscreen(int16_t x, uint8_t y, const char *s) {
  u8g2_t *raw = u8g2.getU8g2();
  int16_t cx = x;
  for (const char *p = s; *p; p++) {
    uint8_t w = (uint8_t)u8g2_GetGlyphWidth(raw, (uint8_t)*p);
    if (cx >= 0) u8g2.drawGlyph((uint16_t)cx, y, (uint8_t)*p);
    cx += w;
  }
}

void showSplash() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setFontPosTop();
  const char *title = "HIVY - Alimentation Haute Tension 2026";
  int16_t titleW = u8g2.getStrWidth(title);
  int16_t titleFinalX = (LEBEL_LOGO_WIDTH - titleW) / 2;
  if (titleFinalX < 0) titleFinalX = 0;

  // Phase 1 (~500ms) : le logo arrive de la droite (x: 256 -> 0).
  for (uint8_t frame = 0; frame <= SPLASH_ANIM_FRAMES; frame++) {
    float t = (float)frame / SPLASH_ANIM_FRAMES;
    uint16_t logoX = (uint16_t)(LEBEL_LOGO_WIDTH * (1.0f - t));
    u8g2.clearBuffer();
    drawLogo(logoX);
    u8g2.sendBuffer();
    delay(SPLASH_ANIM_MS / SPLASH_ANIM_FRAMES);
  }

  // Phase 2 (~500ms) : le logo est arrive, la phrase arrive ensuite de
  // la gauche, depuis entierement hors ecran (x: -titleW -> centree).
  for (uint8_t frame = 0; frame <= SPLASH_ANIM_FRAMES; frame++) {
    float t = (float)frame / SPLASH_ANIM_FRAMES;
    int16_t textX = (int16_t)(-titleW + (titleW + titleFinalX) * t);
    u8g2.clearBuffer();
    drawLogo(0);
    drawStrFromOffscreen(textX, 51, title);
    u8g2.sendBuffer();
    delay(SPLASH_ANIM_MS / SPLASH_ANIM_FRAMES);
  }

  delay(SPLASH_HOLD_MS);
}

// ---------------------------------------------------------------------
// Dashboard -- echelles d'affichage (VALEURS PROVISOIRES a corriger
// avec les vraies specs de l'alim, cf. doc/parametre-liaisonUART-TMS320-ESP32.txt
// et la calibration ADC dans doc/ESP32-UART.md pour les ordres de grandeur
// deja connus : VIN 30V max, IIN 3A max, VOUT 450V max, IOUT 50mA max).
// ---------------------------------------------------------------------
const float VIN_MIN = 0.0f,    VIN_MAX = 25.0f;
const float IIN_MIN = 0.0f,    IIN_MAX = 3.0f;
const float VOUT_MIN = 200.0f, VOUT_MAX = 500.0f;
const float IOUT_MIN = 0.0f,   IOUT_MAX = 0.050f;  // 50 mA
const float PWR_MIN = 0.0f,    PWR_MAX = 30.0f;    // W, PIN/POUT
const float RDT_MIN = 50.0f,   RDT_MAX = 100.0f;   // %, rendement

const float ALERT_THRESHOLD = 0.90f;  // 90% de l'echelle -> icone attention

// DEBUG PROVISOIRE : force l'affichage de toutes les icones (alerte +
// danger principale) pour verifier leur positionnement, independamment
// des seuils/etat reels. Positions validees -> desactive.
const bool DEBUG_FORCE_ALL_ICONS = false;

// DEBUG PROVISOIRE : force VIN/IIN/VOUT/IOUT a des valeurs de test
// realistes (au lieu de la telemetrie reelle) pour verifier l'affichage.
// Remettre a false une fois le test termine.
const bool DEBUG_FORCE_TEST_VALUES = false;
const float TEST_VIN = 10.5f;
const float TEST_IIN = 1.5f;
const float TEST_VOUT = 358.0f;
const float TEST_IOUT = 0.037f;  // 37 mA

// Seuils temperature -- PROVISOIRES, a corriger avec les vraies specs.
const float T1_ALERT_C = 70.0f;
const float T2_ALERT_C = 70.0f;

// Icone danger principale : DOIT dependre UNIQUEMENT de VOUT (doc/ESP32-UART.md,
// retour firmware TMS320) -- jamais de HT ni de FAULT. Un defaut ne
// decharge pas le condensateur de sortie (HV_EN isole la charge mais les
// ~400V restent presents) ; masquer l'icone sur HT=off ou FAULT!=0
// rassurerait l'operateur precisement quand le risque est maximal.
const float VOUT_DANGER_THRESHOLD = 50.0f;

// ---------------------------------------------------------------------
// Layout colonnes (256x64 reel)
// ---------------------------------------------------------------------
const int SCREEN_W = 256;
const int COL1_X = 0;
const int COL2_X = 110;
const int COL3_X = 205;

// Bargraphs + valeurs min/max + mesures descendus de 2px (BAR_Y 16->18).
const int BAR_Y = 18;
const int BAR_H = 32;
const int BAR_W = 8;

// Ligne verticale entre colonne 1 et 2, decalee de 2px vers la gauche
// par rapport a COL2_X (qui reste la reference des elements colonne 2).
const int COL1_COL2_DIVIDER_X = COL2_X - 2;

// Clignotement 1s ON / 1s OFF, partage par toutes les icones d'alerte
// et les mots d'etat de defaut (DEFAUT, SYS:KO).
bool Blink1Hz() { return (millis() / 1000) % 2 == 0; }

// Tous les codes FAULT (y compris EMUSTOP) sont affiches comme un defaut :
// EMUSTOP arrete a la fois l'UART et le PWM cote TMS320, donc l'operateur
// doit le voir (SYS:EMUSTOP) plutot que de juste constater une liaison
// perdue. Difference avec les defauts 2-5 : EMUSTOP peut etre acquitte et
// le TMS320 repart (retour spontane a FAULT_NONE possible) ; les defauts
// surintensite/surchauffe (2-5), eux, ne s'acquittent pas -- seul un cycle
// d'alimentation de la carte les reinitialise (cf. protocol.h).
bool isRealFault(uint8_t fault) { return fault != FAULT_NONE; }

// Libelle court du code FAULT (voir enum TmsFault, protocol.h), pour
// affichage OLED (SYS:) et web.
const char *faultLabel(uint8_t fault) {
  switch (fault) {
    case FAULT_EMUSTOP: return "EMUSTOP";
    case FAULT_OVERCURRENT_I1: return "OVERI1";
    case FAULT_OVERCURRENT_I2: return "OVERI2";
    case FAULT_OVERTEMP_T1: return "OVERT1";
    case FAULT_OVERTEMP_T2: return "OVERT2";
    default: return "OK";  // FAULT_NONE
  }
}

// Mini icone triangle + "!" (dessin vectoriel, trop petit pour un bitmap net)
void drawAlertIcon(int x, int y) {
  u8g2.drawTriangle(x + 5, y, x, y + 9, x + 10, y + 9);
  u8g2.drawVLine(x + 4, y + 3, 3);
  u8g2.drawPixel(x + 4, y + 7);
}

// Icone danger principale a moitie taille (20x20) : drawXBMP ne
// redimensionne pas, donc on ne prend qu'un pixel source sur deux
// (nearest-neighbor) sur le bitmap 40x40 existant plutot que de
// fabriquer un second asset.
void drawWarningIconHalf(int x, int y) {
  const uint8_t stride = WARNING_ICON_WIDTH / 8;
  for (int dy = 0; dy < WARNING_ICON_HEIGHT / 2; dy++) {
    for (int dx = 0; dx < WARNING_ICON_WIDTH / 2; dx++) {
      uint8_t b = pgm_read_byte(&warning_icon_bits[dy * 2 * stride + (dx * 2) / 8]);
      if (b & (1 << ((dx * 2) % 8))) u8g2.drawPixel(x + dx, y + dy);
    }
  }
}

// Bargraph vertical avec limites affichees a droite et icone d'alerte
// optionnelle si valeur >= ALERT_THRESHOLD de l'echelle.
// Valeurs min/max remontees de 4px par rapport au haut de la barre
// (LABEL_Y_SHIFT) pour ne plus mordre sur le cadre du bargraph -- ce
// shift suit BAR_Y (descend avec le bargraph). Le TITRE (nom de la
// grandeur, ex. "VIN") ne doit lui pas bouger quand BAR_Y descend, donc
// TITLE_Y_SHIFT = LABEL_Y_SHIFT + (delta BAR_Y) pour compenser.
const int LABEL_Y_SHIFT = 4;
const int TITLE_Y_SHIFT = 6;

// labelScale : facteur applique uniquement a l'affichage des bornes
// min/max (ex. IOUT_MAX=0.050 A -> affiche "50" avec labelScale=1000,
// pour matcher l'unite mA affichee ailleurs). Le remplissage de la
// barre reste calcule sur value/vmin/vmax tels quels (unite d'origine).
// labelXOffset : decalage horizontal du seul texte du label (le nom de
// la grandeur, ex. "RENDEMENT"), independant du bar/valeurs.
void drawVBar(int x, const char *label, float value, float vmin, float vmax,
              bool showAlert, int alertXOffset = 0, float labelScale = 1.0f,
              int labelXOffset = 0) {
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(x + labelXOffset, BAR_Y - 3 - TITLE_Y_SHIFT, label);

  u8g2.drawFrame(x, BAR_Y, BAR_W, BAR_H);
  float pct = (value - vmin) / (vmax - vmin);
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  int fillH = (int)(pct * (BAR_H - 2));
  if (fillH > 0) {
    u8g2.drawBox(x + 1, BAR_Y + (BAR_H - 1 - fillH), BAR_W - 2, fillH);
  }

  char buf[8];
  u8g2.setFont(u8g2_font_4x6_tr);
  snprintf(buf, sizeof(buf), "%.0f", vmax * labelScale);
  u8g2.drawStr(x + BAR_W + 3, BAR_Y + 3 - LABEL_Y_SHIFT, buf);  // remonte de 2px
  snprintf(buf, sizeof(buf), "%.0f", vmin * labelScale);
  u8g2.drawStr(x + BAR_W + 3, BAR_Y + BAR_H - LABEL_Y_SHIFT, buf);

  if (showAlert && (DEBUG_FORCE_ALL_ICONS || pct >= ALERT_THRESHOLD) && Blink1Hz()) {
    drawAlertIcon(x + BAR_W + 16 + alertXOffset, BAR_Y + BAR_H / 2 - 4);
  }
}

// ---------------------------------------------------------------------
// Economiseur d'ecran : balle rebondissante avec trainee de 20px qui
// s'estompe progressivement (utilise les 16 niveaux de gris du SSD1322).
// ---------------------------------------------------------------------

const uint32_t IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;  // 5 min sans activite
const uint32_t BALL_STEP_MS = 40;                       // ~25 fps
const int BALL_SIZE = 3;                                // balle 3x3 px
const int TRAIL_LEN = 40;                                // longueur de la trainee (~40px)

float g_ballX = 40, g_ballY = 20;
float g_ballVX = 1.3f, g_ballVY = 0.9f;

// buffer circulaire des dernieres positions (centre de la balle)
int g_trailX[TRAIL_LEN];
int g_trailY[TRAIL_LEN];
int g_trailCount = 0;   // nb de positions valides (monte jusqu'a TRAIL_LEN)
int g_trailHead = 0;    // index de la position la plus recente

uint32_t g_lastActivityMs = 0;
uint32_t g_lastBallStepMs = 0;
bool g_screensaverActive = false;

bool g_lastHtState = false;
bool g_htStateInit = false;

void resetActivityTimer() {
  g_lastActivityMs = millis();
}

void checkActivity(const Telemetry & /*t*/, bool /*linkOk*/, bool htOn) {
  if (!g_htStateInit) {
    g_lastHtState = htOn;
    g_htStateInit = true;
    resetActivityTimer();
    return;
  }
  if (htOn != g_lastHtState) {
    g_lastHtState = htOn;
    resetActivityTimer();
  }
}

void pushTrailPoint(int x, int y) {
  g_trailHead = (g_trailHead + 1) % TRAIL_LEN;
  g_trailX[g_trailHead] = x;
  g_trailY[g_trailHead] = y;
  if (g_trailCount < TRAIL_LEN) g_trailCount++;
}

void stepBall() {
  pushTrailPoint((int)g_ballX + BALL_SIZE / 2, (int)g_ballY + BALL_SIZE / 2);

  g_ballX += g_ballVX;
  g_ballY += g_ballVY;

  if (g_ballX <= 0 || g_ballX >= SCREEN_W - BALL_SIZE)  g_ballVX = -g_ballVX;
  if (g_ballY <= 0 || g_ballY >= 64 - BALL_SIZE)        g_ballVY = -g_ballVY;

  if (g_ballX < 0) g_ballX = 0;
  if (g_ballY < 0) g_ballY = 0;
  if (g_ballX > SCREEN_W - BALL_SIZE) g_ballX = SCREEN_W - BALL_SIZE;
  if (g_ballY > 64 - BALL_SIZE)       g_ballY = 64 - BALL_SIZE;
}

void drawScreensaver() {
  uint32_t now = millis();
  if (now - g_lastBallStepMs >= BALL_STEP_MS) {
    g_lastBallStepMs = now;
    stepBall();
  }

  u8g2.clearBuffer();

  // trainee : fade par densite, pas par niveau de gris. Le buffer u8g2
  // pour ce driver (u8g2_ll_hvline_vertical_top_lsb, u8g2_ll_hvline.c)
  // est 1 bit/pixel -- setDrawColor(2..15) y fait un XOR/toggle, pas un
  // niveau de gris intermediaire (seuls 0/1 sont on/off reels). Les 16
  // niveaux de gris du SSD1322 sont une capacite materielle (contraste),
  // pas exposee en 4bpp par pixel par cette configuration u8g2. On
  // simule donc le fade en n'affichant qu'un point sur deux dans la
  // moitie la plus ancienne de la trainee (dense pres de la balle,
  // clairsemee derriere).
  u8g2.setDrawColor(1);
  for (int i = 0; i < g_trailCount; i++) {
    int idx = (g_trailHead - g_trailCount + 1 + i + TRAIL_LEN) % TRAIL_LEN;
    if (i < TRAIL_LEN / 2 && (i % 2) != 0) continue;
    u8g2.drawPixel(g_trailX[idx], g_trailY[idx]);
  }

  u8g2.setDrawColor(1);  // pas 15 : cf. remarque ci-dessus, XOR/toggle sinon
  u8g2.drawBox((int)g_ballX, (int)g_ballY, BALL_SIZE, BALL_SIZE);

  u8g2.sendBuffer();
}

void drawDashboard(const Telemetry &t, bool linkOk) {
  const CommandState &cmd = TmsLink::command();
  u8g2.clearBuffer();
  // setDrawColor(1) explicite : l'economiseur d'ecran laisse le contexte
  // u8g2 sur un niveau de gris partiel (degrade de la trainee) apres son
  // dernier appel -- sans ca, le dashboard heriterait de ce niveau au
  // lieu du blanc plein apres une sortie de veille.
  u8g2.setDrawColor(1);
  char buf[40];

  // --- bandeau haut : HIVY (gauche) / SYS:OK (centre) / WIFI:... (droite) ---
  // SSID dynamique (config.h, maintenant "HIVY") + IP reelle (WiFi.softAPIP(),
  // attribuee par WiFi.softAP() dans web_ui.cpp). Pas de port : 80 est le
  // port HTTP par defaut, pas necessaire pour se connecter (http://<ip>/
  // suffit, le navigateur l'ajoute lui-meme).
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.setFontPosTop();
  u8g2.drawStr(0, 0, "HIVY");

  // SYS: affiche le defaut TMS320 courant (FAULT, voir faultLabel()).
  // Cas particulier EMUSTOP : l'UART s'arrete aussi cote TMS320 pendant
  // la halte (attendu, pas une panne) -- on continue donc d'afficher
  // "EMUSTOP" (derniere valeur connue) plutot que de basculer sur "KO"
  // au bout des 2s de timeout, le temps que le TMS320 reparte. "KO"
  // reste affiche pour toute autre perte de liaison (derniere valeur
  // connue = OK ou un vrai defaut 2-5, ou aucune trame jamais recue).
  bool showKo = !linkOk && t.fault != FAULT_EMUSTOP;
  bool sysProblem = showKo || isRealFault(t.fault);
  if (!sysProblem || Blink1Hz()) {
    snprintf(buf, sizeof(buf), "SYS:%s", showKo ? "KO" : faultLabel(t.fault));
    int wSys = u8g2.getStrWidth(buf);
    u8g2.drawStr((SCREEN_W - wSys) / 2 - 40, 0, buf);  // historique : +20, -40, -20 = -40
  }

  IPAddress ip = WiFi.softAPIP();
  snprintf(buf, sizeof(buf), "WIFI:%s %d.%d.%d.%d", WIFI_AP_SSID, ip[0], ip[1], ip[2], ip[3]);
  int wWifi = u8g2.getStrWidth(buf);
  u8g2.drawStr(SCREEN_W - wWifi, 0, buf);

  // --- colonne 1 : VIN / IIN / VOUT / IOUT ---
  // DEBUG_FORCE_TEST_VALUES : valeurs de test au lieu de la telemetrie
  // reelle (t.vin/t.iin/t.vout/t.iout), pour verifier l'affichage.
  float vinDisp = DEBUG_FORCE_TEST_VALUES ? TEST_VIN : t.vin;
  float iinDisp = DEBUG_FORCE_TEST_VALUES ? TEST_IIN : t.iin;
  float voutDisp = DEBUG_FORCE_TEST_VALUES ? TEST_VOUT : t.vout;
  float ioutDisp = DEBUG_FORCE_TEST_VALUES ? TEST_IOUT : t.iout;

  // Logo 4 (IOUT) : -10 encore -3px = -13.
  // Bargraphs 2/3/4 (IIN/VOUT/IOUT) + valeurs + mesures decales de 2px
  // a droite (29->31, 56->58, 83->85) ; VIN (bargraph 1) inchange.
  drawVBar(COL1_X + 2,  "VIN",  vinDisp,  VIN_MIN,  VIN_MAX,  true, -14);
  drawVBar(COL1_X + 32, "IIN",  iinDisp,  IIN_MIN,  IIN_MAX,  true, -14);
  drawVBar(COL1_X + 58, "VOUT", voutDisp, VOUT_MIN, VOUT_MAX, true, -14);
  // labelScale=1000 : bornes affichees en mA (0/50) au lieu de A (0/0.05).
  drawVBar(COL1_X + 85, "IOUT", ioutDisp, IOUT_MIN, IOUT_MAX, true, -13, 1000.0f);

  u8g2.setFont(u8g2_font_6x12_tr);
  snprintf(buf, sizeof(buf), "%.1fV", vinDisp);
  u8g2.drawStr(COL1_X, BAR_Y + BAR_H + 4, buf);
  snprintf(buf, sizeof(buf), "%.1fA", iinDisp);  // 2 digits (X.Y)
  u8g2.drawStr(COL1_X + 30, BAR_Y + BAR_H + 4, buf);
  snprintf(buf, sizeof(buf), "%.0fV", voutDisp);
  u8g2.drawStr(COL1_X + 56, BAR_Y + BAR_H + 4, buf);
  snprintf(buf, sizeof(buf), "%.0fmA", ioutDisp * 1000.0f);
  u8g2.drawStr(COL1_X + 83, BAR_Y + BAR_H + 4, buf);

  u8g2.drawVLine(COL1_COL2_DIVIDER_X, BAR_Y - 6, BAR_H + 20);

  // --- colonne 2 : RENDEMENT (50-100%, pas d'alerte) / PIN / POUT ---
  float pin  = vinDisp  * iinDisp;
  float pout = voutDisp * ioutDisp;
  float eff  = (pin > 0.01f) ? (pout / pin * 100.0f) : 0.0f;
  if (eff > 100.0f) eff = 100.0f;
  if (eff < 0.0f)   eff = 0.0f;

  // RENDEMENT (bar+valeur) decale de 6px a droite ; PIN/POUT (bar+valeur)
  // decales de 4px a droite. Logos 5 et 6 (PIN/POUT) suivent
  // automatiquement puisque l'offset icone est relatif au x du bar.
  // Seul le mot "RENDEMENT" est ensuite recentre 6px vers la gauche
  // (labelXOffset), independamment du bar/valeur qui restent en place.
  drawVBar(COL2_X + 9,  "RENDEMENT", eff, RDT_MIN, RDT_MAX, false, 0, 1.0f, -6);  // pas d'icone (showAlert=false)
  drawVBar(COL2_X + 43, "PIN",  pin,  PWR_MIN, PWR_MAX, true, -14);
  drawVBar(COL2_X + 70, "POUT", pout, PWR_MIN, PWR_MAX, true, -14);

  u8g2.setFont(u8g2_font_6x12_tr);
  snprintf(buf, sizeof(buf), "%.0f%%", eff);
  u8g2.drawStr(COL2_X + 7, BAR_Y + BAR_H + 4, buf);
  snprintf(buf, sizeof(buf), "%.1fW", pin);
  u8g2.drawStr(COL2_X + 33, BAR_Y + BAR_H + 4, buf);  // mesure PIN seule, -6px puis -2px
  snprintf(buf, sizeof(buf), "%.1fW", pout);
  u8g2.drawStr(COL2_X + 64, BAR_Y + BAR_H + 4, buf);  // mesure POUT seule, -4px

  u8g2.drawVLine(COL3_X, BAR_Y - 6, BAR_H + 20);

  // --- colonne 3 : I1/T1, I2/T2, HT/OFF/DEFAUT, icone danger ---
  // Tout le contenu de la colonne remonte de 5px.
  u8g2.setFont(u8g2_font_5x8_tr);
  snprintf(buf, sizeof(buf), "I1 %.1fA", t.i1);  // 2 digits (X.Y)
  u8g2.drawStr(COL3_X + 4, 11, buf);
  snprintf(buf, sizeof(buf), "T1 %.0fC", t.t1);
  u8g2.drawStr(COL3_X + 4, 21, buf);
  // Icone T1 completement a droite (flush ecran) ; le haut du triangle
  // (apex, "y" dans drawAlertIcon) aligne sur le haut de la ligne I1 (y=11).
  if ((DEBUG_FORCE_ALL_ICONS || t.t1 >= T1_ALERT_C) && Blink1Hz()) drawAlertIcon(SCREEN_W - 10, 11);

  // I2/T2 remontes de 2px supplementaires (35->33, 45->43).
  snprintf(buf, sizeof(buf), "I2 %.1fA", t.i2);  // 2 digits (X.Y)
  u8g2.drawStr(COL3_X + 4, 33, buf);
  snprintf(buf, sizeof(buf), "T2 %.0fC", t.t2);
  u8g2.drawStr(COL3_X + 4, 43, buf);
  // Icone T2 completement a droite (flush ecran) ; la base du triangle
  // (y+9 dans drawAlertIcon) aligne sur la base de la ligne I2 (33+8=41,
  // donc y=41-9=32).
  if ((DEBUG_FORCE_ALL_ICONS || t.t2 >= T2_ALERT_C) && Blink1Hz()) drawAlertIcon(SCREEN_W - 10, 32);

  u8g2.setFont(u8g2_font_7x13B_tr);
  // HT/OFF/DEFAUT (coherent avec le libelle du bouton "HT" de la page
  // web, plutot que "RUN") : HT si HT_EN actif, OFF si inactif, DEFAUT
  // (clignotant) si un probleme est detecte. HT/OFF restent fixes,
  // seul DEFAUT clignote (Blink1Hz).
  //
  // t.fault (voir enum TmsFault, protocol.h) est signale par le TMS320
  // lui-meme via le champ FAULT de la trame telemetrie -- pas de seuils
  // devines cote ESP32.
  bool problemDetected = isRealFault(t.fault);
  if (problemDetected) {
    if (Blink1Hz()) u8g2.drawStr(COL3_X + 2, 53, "DEFAUT");
  } else {
    u8g2.drawStr(COL3_X + 2, 53, cmd.ht ? "HT" : "OFF");
  }

  // icone danger principale (20x20), clignotante (Blink1Hz, 1s ON/1s OFF).
  // Regle reelle : UNIQUEMENT Vout > 50V -- independant de HT et de
  // FAULT (voir VOUT_DANGER_THRESHOLD ci-dessus). Calee tout en bas a
  // droite de la colonne 3 (= bord ecran).
  bool dangerActive = voutDisp > VOUT_DANGER_THRESHOLD;
  if ((DEBUG_FORCE_ALL_ICONS || dangerActive) && Blink1Hz()) {
    drawWarningIconHalf(SCREEN_W - WARNING_ICON_WIDTH / 2, 64 - WARNING_ICON_HEIGHT / 2);
  }

  u8g2.sendBuffer();
}

}  // namespace

void begin() {
  // Broches SPI personnalisees (comme l'ancien driver er_oled.cpp) :
  // sck, miso (non cable), mosi, ss. A faire avant u8g2.begin() pour
  // que le bus HW SPI d'u8g2 utilise ces pins plutot que les pins par
  // defaut de l'ESP32-C3.
  SPI.begin(PIN_OLED_CLK, -1, PIN_OLED_MOSI, PIN_OLED_CS);

  u8g2.begin();

  // Re-applique le registre "Set Re-map / Dual COM Line Mode" (0xA0) tel
  // que valide sur l'ancien driver maison (legacy/er_oled.cpp) : COM
  // remap + nibble remap actives, column remap desactive. Le remap par
  // defaut d'u8g2 pour ce controleur ne correspond pas au cablage de ce
  // panneau (image en miroir horizontal sinon).
  //
  // NB : desactiver le nibble remap (0x10 au lieu de 0x14) a ete essaye
  // pour tenter de corriger le logo coupe en deux, mais a fait
  // reapparaitre le miroir vertical et rendu l'affichage illisible --
  // donc le nibble remap doit rester actif ici, contrairement a
  // l'hypothese initiale.
  u8g2.sendF("caa", 0xA0, 0x14, 0x11);

  u8g2.setContrast(sliderToContrast(g_brightness));

  showSplash();
}

void update(const Telemetry &t, bool linkOk) {
  const CommandState &cmd = TmsLink::command();
  checkActivity(t, linkOk, cmd.ht);

  // Page web consultee recemment (chargement de page ou poll /api/state,
  // qui revient toutes les 1s tant que la page reste ouverte dans un
  // navigateur) -> compte comme de l'activite, reactive/maintient le
  // dashboard OLED.
  if (millis() - WebUi::lastActivityMs() < IDLE_TIMEOUT_MS) {
    resetActivityTimer();
  }

  bool idle = (millis() - g_lastActivityMs) >= IDLE_TIMEOUT_MS;
  if (idle) {
    g_screensaverActive = true;
    drawScreensaver();
    return;
  }
  if (g_screensaverActive) {
    g_screensaverActive = false;
    g_lastRefreshMs = 0;  // force un redraw complet du dashboard au reveil
  }

  uint32_t now = millis();
  if (now - g_lastRefreshMs < REFRESH_PERIOD_MS) return;
  g_lastRefreshMs = now;
  drawDashboard(t, linkOk);
}

void setBrightness(uint8_t value) {
  g_brightness = value;
  u8g2.setContrast(sliderToContrast(g_brightness));
}

// Valeur brute du slider (0-255), pas la valeur gamma-corrigee envoyee
// au registre -- c'est ce que la page web doit reafficher.
uint8_t brightness() { return g_brightness; }

}  // namespace OledUi
