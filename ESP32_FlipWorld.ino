/* ============================================================================
   ESP32 FlipWorld — Pancake (ESP32-C5, ST7796 320x480, FT6336 capacitive touch)
                     & Marauder V8 (ILI9341 240x320, XPT2046 resistive)
   ============================================================================
   FlipWorld — the top-down action game from Picoware (jblanked/FlipWorld) — on
   the FlipSocial touch UI shell. A themed main menu launches the game; WiFi,
   theme, brightness and About live under Settings. The game renders in full
   colour (per-entity ink colours over a coloured world). Settings + credentials
   persist to SPIFFS.

   Arduino IDE settings:
     Board            : ESP32C5 Dev Module
     Flash Size       : 8MB
     Partition Scheme : Custom  ->  partitions.csv in this folder
     Flash Frequency  : 80 MHz

   Requires the patched TFT_eSPI-ESP32-C5 library with User_Setup_Select.h set to
   #include <User_Setup_marauder_pancake.h>.
   ============================================================================ */

#include "configs.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "ft6336.h"
#include "TouchKeyboard.h"
#include "theme.h"

// Picoware core (panel init, touch, HTTP) + the FlipWorld game engine.
#include "src/Picoware/internal/boards.hpp"
#include "src/Picoware/internal/gui/draw.hpp"
#include "src/Picoware/internal/system/input.hpp"
#include "src/Picoware/internal/system/input_manager.hpp"
#include "src/Picoware/internal/system/http.hpp"
#include "src/Picoware/internal/system/view.hpp"
#include "src/Picoware/internal/system/view_manager.hpp"
// FlipWorld game engine + world (game.hpp / icon.hpp pull in engine/*).
#include "src/Picoware/internal/engine/engine.hpp"
#include "src/Picoware/internal/engine/game.hpp"
#include "src/Picoware/internal/engine/level.hpp"
#include "src/Picoware/internal/applications/games/flipworld/assets.hpp"
#include "src/Picoware/internal/applications/games/flipworld/game.hpp"
#include "src/Picoware/internal/applications/games/flipworld/icon.hpp"
using namespace Picoware;

// Globals
#ifdef HAS_C5_SD
SPIClass sharedSPI(SPI);
#endif

static ViewManager *vm    = nullptr;   // owns Draw (panel) + InputManager (touch)
static TFT_eSPI    *tft   = nullptr;   // raw panel (from Draw) for the shell screens
static TouchInput  *touch = nullptr;   // FT6336 touch source (from InputManager)
static Theme        theme;             // colour theme + accent + font + brightness

// Theme-driven colours (macros so every use follows the current theme).
#define COL_BG     (theme.bg())
#define COL_FG     (theme.fg())
#define COL_ACCENT (theme.hdr())
#define COL_DIM    (theme.dim())
#define COL_SEL    (theme.sel())
static const uint16_t COL_OK = 0x07E0;   // status green (theme-independent)

// Panel size comes from the board block in configs.h. Rotation is left at the
// power-on default (0 = portrait), so these map straight onto the panel:
// Pancake ST7796 = 320x480, V8 ILI9341 = 240x320. The layout below is derived
// from these, so it reflows per board.
static const int SCRW = TFT_WIDTH;
static const int SCRH = TFT_HEIGHT;

// Shell layout — matches H4W9 (header 28, nav 28, list rows 34).
static const int HDRH     = 28;
static const int NAVH     = 28;
// The settings list is drawn at fixed offsets and does not scroll, so the rows
// have to fit the panel: at 34 px they run off the bottom of the V8's 320 px
// screen. 26 px still clears the 22 px chips and the 16 px font.
#ifdef MARAUDER_V8
static const int ITEMH    = 26;
#else
static const int ITEMH    = 34;
#endif
static const int CONTENTY = HDRH;

// FlipWorld player progression (defined up here so Arduino's auto-generated
// prototypes — inserted above the first function — can see the type).
struct FWStats { String name; float level, xp, health, max_health, strength; bool valid; };

#ifndef HAS_CAP_TOUCH
// Resistive touch calibration (V8). Capacitive panels report real coordinates
// and need none of this. The 5 uint16 blob is TFT_eSPI's own format; it lives on
// SPIFFS next to the other settings.
static const char *TOUCH_CAL_FILE = "/pico_touch.dat";

static bool touchCalLoad(uint16_t *cal) {
  File f = SPIFFS.open(TOUCH_CAL_FILE, "r");
  if (!f) return false;
  bool ok = (f.read((uint8_t *)cal, sizeof(uint16_t) * 5) == sizeof(uint16_t) * 5);
  f.close();
  return ok;
}
static void touchCalSave(const uint16_t *cal) {
  File f = SPIFFS.open(TOUCH_CAL_FILE, "w");
  if (!f) return;
  f.write((const uint8_t *)cal, sizeof(uint16_t) * 5);
  f.close();
}
// TFT_eSPI's 4-corner wizard. Blocking, and deliberately drawn without the theme
// so it is legible before anything else is up.
static void touchCalRun() {
  uint16_t cal[5];
  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("Touch Calibration", SCRW / 2, SCRH / 2 - 24, 4);
  tft->drawString("Tap each corner arrow", SCRW / 2, SCRH / 2 + 6, 2);
  tft->setTextDatum(TL_DATUM);
  delay(1500);
  tft->fillScreen(TFT_BLACK);
  tft->calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);
  tft->setTouch(cal);
  touchCalSave(cal);
}
// Load the stored calibration, or run the wizard once on first boot.
static void touchCalInit() {
  uint16_t cal[5];
  if (touchCalLoad(cal)) tft->setTouch(cal);
  else                   touchCalRun();
}
#endif // !HAS_CAP_TOUCH

// Touch helpers
// Wait for a fresh tap (press edge) and return its point; blocks.
static bool waitTap(uint16_t &x, uint16_t &y, uint32_t timeoutMs = 0) {
  uint32_t start = millis();
  bool wasDown = touch->isPressed();
  for (;;) {
    touch->run();
    bool down = touch->isPressed();
    if (down && !wasDown) { x = touch->x(); y = touch->y(); return true; }
    wasDown = down;
    if (timeoutMs && (millis() - start) > timeoutMs) return false;
    delay(8);
    yield();
  }
}

static bool inRect(uint16_t x, uint16_t y, int rx, int ry, int rw, int rh) {
  return (int)x >= rx && (int)x < rx + rw && (int)y >= ry && (int)y < ry + rh;
}

// Theme / brightness plumbing
static void applyBrightness() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, theme.duty());
#else
  ledcWrite(0, theme.duty());
#endif
}
static void applyThemeToViewManager() {
  if (!vm) return;
  vm->setBackgroundColor(theme.bg());
  vm->setForegroundColor(theme.fg());
  vm->setSelectedColor(theme.sel());
}

// Status LED. Colour-coded by action where the hardware allows:
//   amber = WiFi scan/connect, blue = HTTP fetch, green = success, red = error.
// The public API (ledOff/ledWifi/ledHttp/ledOk/ledErr/ledBlinkOk/ledSet) is the
// same for both backends; only the drive layer differs per board.
#ifdef HAS_ACT_LED
// V8: one blue GPIO LED (active-high). GPIO28 is a strapping pin (pull-up =
// normal SPI boot), so it is handled exactly like the Marauder firmware does:
// a plain digital output — no PWM/LEDC routed onto the strap pin and no pad-hold,
// so a reset always releases it back to the pull-up and boots normally. No colour,
// so every status maps to on/off; success is a brief blink. led_bright 0 = off,
// keeping the Settings LED row functional (as a simple on/off, not a dimmer).
static bool g_actLedReady = false;
static void ledActArm() {
  if (g_actLedReady) return;
  pinMode(ACT_LED_PIN, OUTPUT);
  digitalWrite(ACT_LED_PIN, LOW);
  g_actLedReady = true;
}
static void ledActSet(bool on) {
  ledActArm();
  digitalWrite(ACT_LED_PIN, (on && theme.led_bright > 0) ? HIGH : LOW);
}
static inline void ledOff()  { ledActSet(false); }
static inline void ledWifi() { ledActSet(true); }
static inline void ledHttp() { ledActSet(true); }
static inline void ledOk()   { ledActSet(true); }
static inline void ledErr()  { ledActSet(true); }
static inline void ledBlinkOk(uint16_t ms = 150) { ledActSet(true); delay(ms); ledActSet(false); }
static void ledSet(bool on) { ledActSet(on); }

#else
// Pancake: onboard addressable RGB LED (WS2812-style).
#ifdef RGB_BUILTIN
  #define PW_RGB_PIN RGB_BUILTIN
#else
  #define PW_RGB_PIN LED_BUILTIN
#endif
// Colours are full-intensity hues; ledRGB scales them by the LED-brightness
// setting (0..20, where 0 = off). Default 4 keeps the LED gentle.
//
// The RGB LED is WS2812-style: consecutive frames must be separated by a reset
// gap (>50us idle low) or the LED latches the first frame and passes the next
// one down the chain — so a colour followed immediately by off stays lit. Hold
// off before every frame so back-to-back writes always land.
static inline void ledGap() { delayMicroseconds(300); }
static void ledRGB(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t s = theme.led_bright;
  ledGap();
  rgbLedWrite(PW_RGB_PIN, (uint8_t)((uint16_t)r * s / 20),
                          (uint8_t)((uint16_t)g * s / 20),
                          (uint8_t)((uint16_t)b * s / 20));
}
static inline void ledOff()  { ledGap(); rgbLedWrite(PW_RGB_PIN, 0, 0, 0); }  // truly off
static inline void ledWifi() { ledRGB(255, 150, 0); }  // amber — scanning / connecting
static inline void ledHttp() { ledRGB(0,   80, 255); } // blue  — HTTP request in flight
static inline void ledOk()   { ledRGB(0,  255,   0); } // green — success
static inline void ledErr()  { ledRGB(255,  0,   0); } // red   — error
// Success blink for actions that finish too fast for a bare ledOk() to be seen.
static inline void ledBlinkOk(uint16_t ms = 150) { ledOk(); delay(ms); ledOff(); }
// Back-compat shim: old on/off calls map to the WiFi (amber) colour.
static void ledSet(bool on) { if (on) ledWifi(); else ledOff(); }
#endif // HAS_ACT_LED

// FlipWorld account credentials (SPIFFS: /pico_user.json)
static String credGet(const char *key) {
  File f = SPIFFS.open("/pico_user.json", FILE_READ);
  if (!f) return "";
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return "";
  return d[key].as<String>();
}
static void credSet(const char *key, const String &val) {
  JsonDocument d;
  File f = SPIFFS.open("/pico_user.json", FILE_READ);
  if (f) { deserializeJson(d, f); f.close(); }
  d[key] = val;
  File w = SPIFFS.open("/pico_user.json", FILE_WRITE);
  if (!w) return;
  serializeJson(d, w);
  w.close();
}

// Saved WiFi networks (SPIFFS: /pico_wifi.json = {"nets":[{"s","p"}]})
static const int WIFI_MAX_SAVED = 12;
static int wifiLoad(String *ss, String *pp, int maxN) {
  File f = SPIFFS.open("/pico_wifi.json", FILE_READ);
  if (!f) return 0;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return 0;
  JsonArray a = d["nets"].as<JsonArray>();
  if (a.isNull()) return 0;
  int n = 0;
  for (JsonVariant v : a) {
    if (n >= maxN) break;
    ss[n] = v["s"].as<String>();
    pp[n] = v["p"].as<String>();
    n++;
  }
  return n;
}
static void wifiWriteAll(String *ss, String *pp, int n) {
  JsonDocument d;
  JsonArray a = d["nets"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["s"] = ss[i];
    o["p"] = pp[i];
  }
  File w = SPIFFS.open("/pico_wifi.json", FILE_WRITE);
  if (!w) return;
  serializeJson(d, w);
  w.close();
}
// Add/update a network, moving it to the front (most-recent-first).
static void wifiSave(const String &ssid, const String &pass) {
  String ss[WIFI_MAX_SAVED], pp[WIFI_MAX_SAVED];
  int n = wifiLoad(ss, pp, WIFI_MAX_SAVED);
  String os[WIFI_MAX_SAVED], op[WIFI_MAX_SAVED];
  int m = 0;
  os[m] = ssid; op[m] = pass; m++;                 // new entry first
  for (int i = 0; i < n && m < WIFI_MAX_SAVED; i++) {
    if (ss[i] == ssid) continue;                   // drop old duplicate
    os[m] = ss[i]; op[m] = pp[i]; m++;
  }
  wifiWriteAll(os, op, m);
}
static String wifiPassFor(const String &ssid) {
  String ss[WIFI_MAX_SAVED], pp[WIFI_MAX_SAVED];
  int n = wifiLoad(ss, pp, WIFI_MAX_SAVED);
  for (int i = 0; i < n; i++) if (ss[i] == ssid) return pp[i];
  return "";
}
static void wifiForget(const String &ssid) {
  String ss[WIFI_MAX_SAVED], pp[WIFI_MAX_SAVED];
  int n = wifiLoad(ss, pp, WIFI_MAX_SAVED);
  String os[WIFI_MAX_SAVED], op[WIFI_MAX_SAVED];
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (ss[i] == ssid) continue;
    os[m] = ss[i]; op[m] = pp[i]; m++;
  }
  wifiWriteAll(os, op, m);
}

// Battery fuel gauge (MAX17048, I2C 0x36, shared bus)
// SOC register 0x04: high byte = integer %, low byte = 1/256 % (discarded).
static int      g_battPct = -1;      // -1 = unknown / gauge absent
static bool     g_battOk  = false;
static uint32_t g_battMs  = 0;
static void battInit() {
  Wire.beginTransmission(0x36);
  g_battOk = (Wire.endTransmission() == 0);
  Serial.println(g_battOk ? F("[Battery] MAX17048 OK") : F("[Battery] MAX17048 not found"));
}
static void battUpdate() {
  if (!g_battOk) return;
  Wire.beginTransmission(0x36);
  Wire.write(0x04);                          // SOC register
  if (Wire.endTransmission(false) != 0) { g_battOk = false; return; }
  Wire.requestFrom((uint8_t)0x36, (uint8_t)2);
  if (Wire.available() < 2) return;
  uint8_t hi = Wire.read();
  Wire.read();                               // fractional byte — discard
  g_battPct = (hi > 100) ? 100 : hi;
  g_battMs  = millis();
}

// True while a saved-network connect attempt is in flight (header icon = yellow).
static volatile bool g_wifiConnecting = false;

// Rendering helpers
// One 90°-wide WiFi arc (a real wifi-fan wedge: ±45° around straight up),
// plotted point-by-point so it doesn't depend on any drawArc angle convention.
static void wifiArc(int cx, int cy, int r, uint16_t c) {
  for (int deg = -45; deg <= 45; deg += 2) {
    float a = deg * 0.0174533f;
    int x = cx + (int)lroundf(r * sinf(a));
    int y = cy - (int)lroundf(r * cosf(a));
    tft->drawPixel(x, y, c);              // 1px-thin arc
  }
}

// Battery % (right edge) + WiFi state icon, painted into the header's top-right.
// Self-clearing, so it can also be called on its own for a periodic refresh.
static void drawHeaderStatus() {
  if (g_battOk && (g_battMs == 0 || millis() - g_battMs > 10000)) battUpdate();
  tft->fillRect(SCRW - 62, 0, 62, HDRH, COL_ACCENT);   // clear the status corner

  int rx = SCRW - 4;                                   // right edge for battery text
  if (g_battPct >= 0) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", g_battPct);
    tft->setTextColor(COL_FG, COL_ACCENT);
    tft->setTextDatum(MR_DATUM);
    tft->drawString(pct, rx, HDRH / 2, 1);             // small (font 1) like H4W9
    rx -= tft->textWidth(pct, 1) + 8;                  // slot the icon left of the %
  }

  // WiFi icon: source dot + three 90° arcs. green=connected, yellow=connecting, red=off.
  uint16_t wc = g_wifiConnecting               ? TFT_YELLOW
              : (WiFi.status() == WL_CONNECTED) ? COL_OK
                                                : TFT_RED;
  int cx = rx - 10, cy = HDRH / 2 + 5;                 // arc apex (bottom) point
  tft->fillCircle(cx, cy, 1, wc);
  wifiArc(cx, cy, 4,  wc);
  wifiArc(cx, cy, 7,  wc);
  wifiArc(cx, cy, 10, wc);
  tft->setTextDatum(TL_DATUM);
}

// Crisp vector chevron "<"/">" (solid triangle) — matches H4W9 selectors.
static void drawChevron(int bx, int by, int bw, int bh, bool right, uint16_t col) {
  int cx = bx + bw / 2, cy = by + bh / 2;
  if (right) tft->fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy, col);
  else       tft->fillTriangle(cx + 3, cy - 5, cx + 3, cy + 5, cx - 4, cy, col);
}
// Centered "+"/"-" (2px strokes) for the brightness selector.
static void drawPlusMinus(int bx, int by, int bw, int bh, bool plus, uint16_t col) {
  int cx = bx + bw / 2, cy = by + bh / 2, r = 6;
  tft->fillRect(cx - r, cy - 1, 2 * r, 2, col);          // horizontal
  if (plus) tft->fillRect(cx - 1, cy - r, 2, 2 * r, col); // vertical
}

// H4W9-style header: optional back box with chevron (top-left), centred
// title, status corner (WiFi icon + battery %) top-right.
static void drawHeader(const String &title, bool showBack) {
  tft->fillRect(0, 0, SCRW, HDRH, COL_ACCENT);
  if (showBack) {
    tft->fillRoundRect(2, 3, 40, 22, 4, COL_ACCENT);
    tft->drawRoundRect(2, 3, 40, 22, 4, theme.neon(3, COL_DIM));
    drawChevron(2, 3, 40, 22, false, COL_FG);
  }
  tft->setTextColor(COL_FG, COL_ACCENT);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(title, SCRW / 2, HDRH / 2, 2);
  drawHeaderStatus();
  tft->setTextDatum(TL_DATUM);
}
static bool backTapped(uint16_t x, uint16_t y) {
  return (int)y < HDRH && (int)x < 48;   // top-left back box
}

// Footer nav bar (H4W9-style): up to three labelled rounded buttons in thirds.
static void drawNav(const char *l, const char *m, const char *r) {
  int y = SCRH - NAVH, third = SCRW / 3, bh = NAVH - 10, by = y + 5, bw = third - 10;
  tft->fillRect(0, y, SCRW, NAVH, COL_BG);
  tft->drawFastHLine(0, y, SCRW, theme.edge());
  const char *L[3] = { l, m, r };
  for (int i = 0; i < 3; i++) {
    if (!L[i] || !L[i][0]) continue;
    int cx = i * third + third / 2, bx = cx - bw / 2;
    tft->fillRoundRect(bx, by, bw, bh, 5, COL_ACCENT);
    tft->drawRoundRect(bx, by, bw, bh, 5, theme.neon(i, COL_DIM));
    tft->setTextColor(COL_FG, COL_ACCENT);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(L[i], cx, by + bh / 2, 2);
  }
  tft->setTextDatum(TL_DATUM);
}
// Which nav third was tapped: 0/1/2, or -1 if not in the footer band.
static int navHit(uint16_t x, uint16_t y) {
  if ((int)y < SCRH - NAVH) return -1;
  int c = (int)x / (SCRW / 3);
  return c > 2 ? 2 : c;
}

// One list row: fill, left text, optional right chevron, divider. Divider/chevron
// follow the theme (neon rainbow on the Neon theme, else edge/dim).
static void drawListRow(int y, const String &text, bool sel, bool arrow) {
  uint16_t bgc = sel ? COL_SEL : COL_BG;
  int seed = y / ITEMH;
  tft->fillRect(0, y, SCRW, ITEMH, bgc);
  tft->setTextColor(COL_FG, bgc);
  tft->setTextDatum(ML_DATUM);
  tft->drawString(text, 12, y + ITEMH / 2, 2);
  if (arrow) drawChevron(SCRW - 26, y, 16, ITEMH, true, theme.neon(seed, COL_DIM));
  tft->drawFastHLine(0, y + ITEMH - 1, SCRW, theme.neon(seed, theme.edge()));
  tft->setTextDatum(TL_DATUM);
}

// Sprite version of a list row (for flicker-free momentum scrolling). `seed` is
// the row index, used to vary the neon hue down the list.
static void drawRowSprite(TFT_eSprite &spr, int y, const String &text, bool arrow, int seed) {
  spr.fillRect(0, y, SCRW, ITEMH, COL_BG);
  spr.setTextColor(COL_FG, COL_BG);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(text, 12, y + ITEMH / 2, 2);
  if (arrow) {
    int cx = SCRW - 26 + 8, cy = y + ITEMH / 2;
    spr.fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy, theme.neon(seed, COL_DIM));
  }
  spr.drawFastHLine(0, y + ITEMH - 1, SCRW, theme.neon(seed, theme.edge()));
  spr.setTextDatum(TL_DATUM);
}

// Scrollbar drawn into a sprite: track + thumb at the right edge. The thumb
// follows the theme (neon hue on the Neon theme, else dim).
// `viewH` = visible height, `total` = content height, `scroll` = current offset.
static void sprScrollBar(TFT_eSprite &spr, int viewH, int total, float scroll) {
  if (total <= viewH) return;
  const int bw = 4, bx = SCRW - bw - 1;
  spr.fillRect(bx, 0, bw, viewH, theme.edge());
  int thumbH = viewH * viewH / total; if (thumbH < 14) thumbH = 14;
  int maxS = total - viewH;
  int thumbY = (maxS > 0) ? (int)((scroll / (float)maxS) * (viewH - thumbH)) : 0;
  // Thumb hue tracks the scroll position (neon rainbow on the Neon theme).
  spr.fillRect(bx, thumbY, bw, thumbH, theme.neon(thumbY / 12, COL_DIM));
}

// scrollList return sentinels for footer-button taps (Back is SL_BACK).
static const int SL_BACK = -1, SL_F0 = -2, SL_F1 = -3, SL_F2 = -4;

// Momentum-scrolling list of string rows with a right-edge scrollbar. Optional
// footer nav bar (pass labels): a footer tap returns SL_F0/SL_F1/SL_F2, Back
// returns SL_BACK, and a row tap returns its index.
static int scrollList(const String &title, String *rows, int n, bool arrow,
                      const char *fL = nullptr, const char *fM = nullptr, const char *fR = nullptr) {
  bool hasFooter = (fL && fL[0]) || (fM && fM[0]) || (fR && fR[0]);
  const int CY = CONTENTY;
  const int CH = SCRH - CONTENTY - (hasFooter ? NAVH : 0);
  int total = n * ITEMH;
  tft->fillScreen(COL_BG);
  drawHeader(title, true);
  if (hasFooter) drawNav(fL ? fL : "", fM ? fM : "", fR ? fR : "");

  TFT_eSprite spr(tft);
  spr.setColorDepth(16);
  bool haveSpr = (spr.createSprite(SCRW, CH) != nullptr);

  float scroll = 0, fling = 0;
  bool wasDown = false, moved = false;
  uint16_t pX = 0, pY = 0, lastY = 0;
  float pScroll = 0, vel = 0;
  uint32_t lastT = 0;

  auto render = [&]() {
    float maxS = total > CH ? total - CH : 0;
    if (scroll < 0) scroll = 0;
    if (scroll > maxS) scroll = maxS;
    if (haveSpr) {
      spr.fillSprite(COL_BG);
      for (int i = 0; i < n; i++) {
        int y = i * ITEMH - (int)scroll;
        if (y + ITEMH < 0 || y > CH) continue;
        drawRowSprite(spr, y, rows[i], arrow, i);
      }
      sprScrollBar(spr, CH, total, scroll);
      spr.pushSprite(0, CY);
    } else {
      tft->fillRect(0, CY, SCRW, CH, COL_BG);
      for (int i = 0; i < n; i++) {
        int y = i * ITEMH - (int)scroll;
        if (y + ITEMH < 0 || y > CH) continue;
        drawListRow(CY + y, rows[i], false, arrow);
      }
    }
  };
  render();

  for (;;) {
    touch->run();
    bool down = touch->isPressed();
    uint16_t ty = touch->y(), tx = touch->x();
    uint32_t now = millis();
    bool need = false;

    if (down && !wasDown) {
      pX = tx; pY = ty; pScroll = scroll; moved = false; fling = 0; lastY = ty; lastT = now; vel = 0;
    } else if (down && wasDown) {
      int dy = (int)pY - (int)ty;
      if (abs(dy) > 6) moved = true;
      scroll = pScroll + dy;
      uint32_t dt = now - lastT;
      if (dt > 0) { vel = (float)((int)lastY - (int)ty) / (float)dt * 1000.0f; lastY = ty; lastT = now; }
      need = true;
    } else if (!down && wasDown) {
      if (!moved) {
        if (backTapped(pX, pY)) { if (haveSpr) spr.deleteSprite(); return SL_BACK; }
        if (hasFooter && (int)pY >= SCRH - NAVH) {          // footer button
          int nh = navHit(pX, pY);
          if (haveSpr) spr.deleteSprite();
          return nh == 0 ? SL_F0 : nh == 2 ? SL_F2 : SL_F1;
        }
        if ((int)pY >= CY && (int)pY < CY + CH) {
          int idx = ((int)pY - CY + (int)scroll) / ITEMH;
          if (idx >= 0 && idx < n) { if (haveSpr) spr.deleteSprite(); return idx; }
        }
      } else {
        fling = vel;
      }
      need = true;
    } else if (fabs(fling) > 25) {
      scroll += fling * 0.016f;
      fling *= 0.95f;
      need = true;
    } else {
      fling = 0;
    }

    wasDown = down;
    if (need) render();
    delay(12);
  }
}

// Bottom status line (only on screens WITHOUT a footer nav bar).
static void statusLine(const char *msg, uint16_t col = 0xFFFF) {
  tft->fillRect(0, SCRH - 26, SCRW, 26, COL_BG);
  tft->setTextColor(col == 0xFFFF ? COL_FG : col, COL_BG);
  tft->setTextDatum(ML_DATUM);
  tft->drawString(msg, 8, SCRH - 13, 2);
  tft->setTextDatum(TL_DATUM);
}

// Settings chip rows: label + [<] value [>] (or [-] value [+])
// Settings rows (H4W9 choiceRow layout)
// [<] value [>] with fixed-right buttons (28x22). Selected row highlights with
// sel_bg. `valcol` = colour to draw the value text (0 = follow font colour).
static const int CHIP_W = 28, CHIP_H = 22;
// Geometry helper (draw + hit-test share it): fwd/bwd button x for a row value.
static void chipGeom(const String &val, int &fwd_bx, int &bwd_bx, int &vx) {
  fwd_bx = SCRW - 8 - CHIP_W;
  int vw = tft->textWidth(val.c_str(), 2);
  vx     = fwd_bx - 4 - vw;
  bwd_bx = vx - 4 - CHIP_W;
}
static void drawChipRow(int y, const String &label, const String &val, bool pm,
                        bool sel, uint16_t valcol) {
  uint16_t rbg = sel ? COL_SEL : COL_BG;
  drawListRow(y, label, sel, false);
  int by = y + (ITEMH - CHIP_H) / 2, fwd_bx, bwd_bx, vx;
  chipGeom(val, fwd_bx, bwd_bx, vx);
  int seed = y / ITEMH;
  tft->fillRoundRect(fwd_bx, by, CHIP_W, CHIP_H, 4, COL_ACCENT);
  tft->drawRoundRect(fwd_bx, by, CHIP_W, CHIP_H, 4, theme.neon(seed, COL_DIM));
  tft->fillRoundRect(bwd_bx, by, CHIP_W, CHIP_H, 4, COL_ACCENT);
  tft->drawRoundRect(bwd_bx, by, CHIP_W, CHIP_H, 4, theme.neon(seed + 4, COL_DIM));
  if (pm) {
    drawPlusMinus(bwd_bx, by, CHIP_W, CHIP_H, false, COL_FG);
    drawPlusMinus(fwd_bx, by, CHIP_W, CHIP_H, true,  COL_FG);
  } else {
    drawChevron(bwd_bx, by, CHIP_W, CHIP_H, false, COL_FG);
    drawChevron(fwd_bx, by, CHIP_W, CHIP_H, true,  COL_FG);
  }
  tft->setTextColor(valcol ? valcol : COL_FG, rbg);
  tft->setTextDatum(ML_DATUM);
  tft->drawString(val, vx, y + ITEMH / 2, 2);
  tft->setTextDatum(TL_DATUM);
}
// Returns -1 none, 0 left/decrement, 1 right/increment. `val` must match draw.
static int chipHit(int y, const String &val, uint16_t x, uint16_t ty) {
  int by = y + (ITEMH - CHIP_H) / 2, fwd_bx, bwd_bx, vx;
  chipGeom(val, fwd_bx, bwd_bx, vx);
  if ((int)ty < by || (int)ty >= by + CHIP_H) return -1;
  if ((int)x >= fwd_bx && (int)x < fwd_bx + CHIP_W) return 1;
  if ((int)x >= bwd_bx && (int)x < bwd_bx + CHIP_W) return 0;
  return -1;
}
// Label + right-aligned dim value + arrow (WiFi / creds rows).
static void drawInfoRow(int y, const String &label, const String &val, bool sel) {
  uint16_t rbg = sel ? COL_SEL : COL_BG;
  drawListRow(y, label, sel, true);
  if (val.length()) {
    tft->setTextColor(COL_DIM, rbg);
    tft->setTextDatum(MR_DATUM);
    tft->drawString(val, SCRW - 26, y + ITEMH / 2, 2);
    tft->setTextDatum(TL_DATUM);
  }
}

// Centred message screen with a Back header: headline `a` + optional detail `b`
// (word-wrapped so long failure reasons stay readable). Blocks for a tap.
static void msgScreen(const char *title, const String &a, const String &b, uint16_t col) {
  tft->fillScreen(COL_BG);
  drawHeader(title, true);
  tft->setTextColor(col, COL_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(a, SCRW / 2, SCRH / 2 - 20, 2);
  if (b.length()) {
    tft->setTextColor(COL_DIM, COL_BG);
    // Greedy word-wrap to the screen width (no dependency on fsWrap).
    int y = SCRH / 2 + 6, maxW = SCRW - 24;
    String line = "", rest = b;
    while (rest.length() && y < SCRH - 20) {
      int sp = rest.indexOf(' ');
      String word = (sp < 0) ? rest : rest.substring(0, sp);
      String cand = line.length() ? line + " " + word : word;
      if (tft->textWidth(cand.c_str(), 2) <= maxW) { line = cand; }
      else { tft->drawString(line, SCRW / 2, y, 2); y += 20; line = word; }
      rest = (sp < 0) ? "" : rest.substring(sp + 1);
    }
    if (line.length() && y < SCRH - 20) tft->drawString(line, SCRW / 2, y, 2);
  }
  tft->setTextDatum(TL_DATUM);
  uint16_t x, y2; waitTap(x, y2);
}

// WiFi
// Last STA disconnect reason (WIFI_REASON_*): 15 = 4-way handshake timeout
// (usually wrong password), 201 = no AP found (band/channel), 205 = conn fail.
static volatile int g_wifiReason = 0;
static volatile int g_wifiEvt = -1;   // last Arduino WiFi event id (-1 = none seen)
static bool g_manualDisconnect = false;   // user tapped Disconnect — don't auto-reconnect
static void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  g_wifiEvt = (int)event;
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    g_wifiReason = info.wifi_sta_disconnected.reason;
}

// Poll for association up to timeoutMs, animating a "connecting..." line at
// `spinnerY`. Tapping the screen cancels (returns false).
static bool waitConnect(uint32_t timeoutMs, int spinnerY) {
  uint32_t start = millis(), lastAnim = 0;
  bool wasDown = touch->isPressed();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    touch->run();
    bool down = touch->isPressed();
    if (down && !wasDown) return false;    // tap to cancel
    wasDown = down;
    if (millis() - lastAnim > 350) {
      lastAnim = millis();
      String d = "connecting";
      for (int i = 0; i < (dots = (dots + 1) % 4); i++) d += ".";
      tft->fillRect(0, spinnerY, SCRW, 20, COL_BG);
      tft->setTextColor(COL_DIM, COL_BG); tft->setTextDatum(MC_DATUM);
      tft->drawString(d, SCRW / 2, spinnerY + 8, 2);
      tft->setTextDatum(TL_DATUM);
    }
    delay(30);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Blocking connect with a clean loading screen. FlipperHTTP's ESP32-C5 approach:
// setBandMode(AUTO) + a plain WiFi.begin() — no scan / BSSID pin / radio cycle.
static bool connectWiFi(const String &ssid, const String &pass) {
  g_wifiConnecting = true;
  g_manualDisconnect = false;                // an explicit connect re-enables auto-reconnect
  ledWifi();
  g_wifiReason = 0;
  g_wifiEvt = -1;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.scanDelete();                         // free any prior scan (harmless if none)
  WiFi.setBandMode(WIFI_BAND_MODE_AUTO);     // dual-band C5: auto-select the band
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.setAutoReconnect(false);

  tft->fillScreen(COL_BG);
  drawHeader("WiFi", true);
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(COL_DIM, COL_BG);
  tft->drawString("Connecting to", SCRW / 2, SCRH / 2 - 22, 2);
  tft->setTextColor(COL_FG, COL_BG);
  tft->drawString(String("\"") + ssid + "\"", SCRW / 2, SCRH / 2 + 4, 4);
  tft->setTextDatum(TL_DATUM);

  bool ok = waitConnect(12000, SCRH / 2 + 34);
  g_wifiConnecting = false;
  ledOff();
  return ok;
}

// Connect to a saved network — just its stored password, no scan.
static bool connectSaved(const String &ssid) {
  return connectWiFi(ssid, wifiPassFor(ssid));
}

// Background (re)connect — NON-BLOCKING so the menu stays responsive. An async
// scan orders the saved networks by signal strength (closest first); then it
// connects to each in turn (setBandMode(AUTO) + WiFi.begin(), poll, next on
// timeout). Driven from loop() via wifiBgTick(); retriggered by loop() on loss.
enum WbState { WB_IDLE, WB_SCAN, WB_CONNECT, WB_DONE };
static WbState  g_wb  = WB_IDLE;
static uint32_t g_wbT = 0;
static String   g_wbSs[WIFI_MAX_SAVED], g_wbPp[WIFI_MAX_SAVED];
static int      g_wbN = 0, g_wbIdx = 0;

static void wifiBgTry() {                     // begin() on the current saved network
  g_wifiReason = 0; g_wifiEvt = -1;
  WiFi.begin(g_wbSs[g_wbIdx].c_str(), g_wbPp[g_wbIdx].c_str());
  WiFi.setAutoReconnect(false);
  g_wbT = millis();
}

static void wifiBgBegin() {
  g_wbN = wifiLoad(g_wbSs, g_wbPp, WIFI_MAX_SAVED);
  if (g_wbN == 0) { g_wb = WB_IDLE; return; }
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);                    // async — order by RSSI when it completes
  g_wbT = millis();
  g_wb = WB_SCAN;
  g_wifiConnecting = true;
}

static void wifiBgTick() {
  if (g_wb == WB_SCAN) {
    int r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING && millis() - g_wbT < 6000) return;   // wait for the scan (<=6s)
    if (r > 0) {
      // Best RSSI of each saved net (−999 = out of range), then sort desc (closest first).
      int rssi[WIFI_MAX_SAVED];
      for (int i = 0; i < g_wbN; i++) {
        rssi[i] = -999;
        for (int j = 0; j < r; j++)
          if (WiFi.SSID(j) == g_wbSs[i] && WiFi.RSSI(j) > rssi[i]) rssi[i] = WiFi.RSSI(j);
      }
      for (int a = 0; a < g_wbN - 1; a++) {
        int best = a;
        for (int b = a + 1; b < g_wbN; b++) if (rssi[b] > rssi[best]) best = b;
        if (best != a) {
          int tr = rssi[a]; rssi[a] = rssi[best]; rssi[best] = tr;
          String ts = g_wbSs[a]; g_wbSs[a] = g_wbSs[best]; g_wbSs[best] = ts;
          String tp = g_wbPp[a]; g_wbPp[a] = g_wbPp[best]; g_wbPp[best] = tp;
        }
      }
    }
    WiFi.scanDelete();
    g_wbIdx = 0;
    wifiBgTry();
    g_wb = WB_CONNECT;
    return;
  }
  if (g_wb == WB_CONNECT) {
    if (WiFi.status() == WL_CONNECTED) { g_wifiConnecting = false; g_wb = WB_DONE; return; }
    // Try only the two closest saved networks (once each, 8 s), then give up and
    // stay disconnected so the LED isn't lit the whole time we're offline.
    int maxTry = g_wbN < 2 ? g_wbN : 2;
    if (millis() - g_wbT > 8000) {
      if (++g_wbIdx >= maxTry) { g_wifiConnecting = false; g_wb = WB_DONE; return; }
      wifiBgTry();
    }
  }
}

// Test HTTPS GET through Picoware's HTTP class; render the first lines.
static void httpTest() {
  tft->fillScreen(COL_BG);
  drawHeader("HTTP Test", true);
  statusLine("GET https://httpbin.org/get ...");
  HTTP http;
  String resp = http.request("GET", "https://httpbin.org/get");
  tft->setTextColor(COL_FG, COL_BG);
  int y = 50, start = 0;
  for (int i = 0; i < (int)resp.length() && y < SCRH - 40; i++) {
    if (resp[i] == '\n' || i - start > 44) {
      tft->drawString(resp.substring(start, i), 6, y, 1);
      y += 12;
      start = i + 1;
    }
  }
  if (resp.length() == 0) statusLine("No response (check TLS / connection).", TFT_RED);
  else                    statusLine("Tap to continue.", COL_OK);
  uint16_t x, ty; waitTap(x, ty);
}

// Scan / pick / password / connect flow. Smooth-scroll list, no paging.
// Row 0 = "Rescan"; the rest are scanned SSIDs.
static void scanFlow() {
  static String rows[41];
  for (;;) {
    tft->fillScreen(COL_BG);
    drawHeader("Scan", true);
    tft->setTextColor(COL_DIM, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("Scanning...", SCRW / 2, SCRH / 2, 2);
    tft->setTextDatum(TL_DATUM);
    ledSet(true);
    int nnet = WiFi.scanNetworks();
    ledSet(false);
    int rc = (nnet < 0) ? 0 : nnet;

    for (int i = 0; i < rc && i < 41; i++)
      rows[i] = WiFi.SSID(i) + "   ch" + WiFi.channel(i) + "  (" + WiFi.RSSI(i) + ")";

    int sel = scrollList("Scan", rows, rc, true, "Back", "Rescan", "");
    if (sel == SL_BACK || sel == SL_F0) return;        // Back
    if (sel == SL_F1) continue;                        // Rescan

    int idx = sel;
    if (idx < 0 || idx >= rc) continue;
    String ssid = WiFi.SSID(idx);
    char pass[65] = {0};
    String sp = wifiPassFor(ssid);
    if (sp.length()) strncpy(pass, sp.c_str(), sizeof(pass) - 1);
    if (!touchKeyboardInput(*tft, COL_FG, COL_BG, pass, sizeof(pass),
                            (String("Password: ") + ssid).c_str(), true)) continue;
    if (connectWiFi(ssid, pass)) {
      wifiSave(ssid, pass);
      statusLine("Connected!", COL_OK);
      uint16_t a, bb; waitTap(a, bb);
      return;
    }
    statusLine((String("Failed (reason ") + g_wifiReason + "). Tap to re-scan.").c_str(), TFT_RED);
    uint16_t a, bb; waitTap(a, bb);
  }
}

// WiFi Setup: saved networks (tap to connect) with a [Disconnect][Scan][Forget]
// footer. The header chevron is Back; the footer left button disconnects WiFi.
static void wifiSetup() {
  static String rows[WIFI_MAX_SAVED];
  for (;;) {
    String ss[WIFI_MAX_SAVED], pp[WIFI_MAX_SAVED];
    int n = wifiLoad(ss, pp, WIFI_MAX_SAVED);
    for (int i = 0; i < n; i++) {
      bool cur = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ss[i]);
      rows[i] = (cur ? String("* ") : String("")) + ss[i];
    }
    int sel = scrollList("WiFi Setup", rows, n, true, "Disconnect", "Scan", n > 0 ? "Forget" : "");
    if (sel == SL_BACK) return;                            // header back
    if (sel == SL_F0) { WiFi.disconnect(true); g_manualDisconnect = true; continue; }   // Disconnect
    if (sel == SL_F1) { scanFlow(); continue; }            // Scan
    if (sel == SL_F2 && n > 0) {                           // Forget — pick a saved net
      static String frows[WIFI_MAX_SAVED];
      for (int i = 0; i < n; i++) frows[i] = ss[i];
      int f = scrollList("Forget", frows, n, true);
      if (f >= 0 && f < n) wifiForget(ss[f]);
      continue;
    }
    if (sel >= 0 && sel < n) connectSaved(ss[sel]);        // tap a saved network
  }
}

// WiFi Debug: live status/event/reason + heap, with HTTP-test/reconnect actions.
static void wifiDebug() {
  for (;;) {
    tft->fillScreen(COL_BG);
    drawHeader("WiFi Debug", true);
    int y = CONTENTY + 10;
    tft->setTextColor(COL_FG, COL_BG);
    tft->setTextDatum(TL_DATUM);
    auto line = [&](const String &s) { tft->drawString(s, 12, y, 2); y += 24; };
    bool up = (WiFi.status() == WL_CONNECTED);
    line(String("Status:       ") + WiFi.status() + (up ? "  (connected)" : ""));
    line(String("Last event:   ") + g_wifiEvt);
    line(String("Disc reason:  ") + g_wifiReason);
    line(String("SSID:         ") + (up ? WiFi.SSID() : String("-")));
    line(String("Channel:      ") + (up ? String(WiFi.channel()) : String("-")));
    line(String("IP:           ") + (up ? WiFi.localIP().toString() : String("-")));
    line(String("RSSI:         ") + (up ? String(WiFi.RSSI()) : String("-")));
    line(String("Free heap:    ") + ESP.getFreeHeap());
    line(String("Free PSRAM:   ") + ESP.getFreePsram());
    drawNav("Disconnect", "HTTP Test", "Reconnect");

    uint16_t x, ty;
    if (!waitTap(x, ty)) continue;
    if (backTapped(x, ty)) return;                 // header back
    int nh = navHit(x, ty);
    if (nh == 0) { WiFi.disconnect(true); g_manualDisconnect = true; continue; }   // Disconnect
    if (nh == 1) httpTest();
    if (nh == 2) {
      String ss[WIFI_MAX_SAVED], pp[WIFI_MAX_SAVED];
      int n = wifiLoad(ss, pp, WIFI_MAX_SAVED);
      if (n) connectSaved(ss[0]);
    }
  }
}

// Settings (H4W9 layout: highlight on tap, partial redraw, no flash)
// Theme, Accent, Font Color, Brightness, LED, WiFi Setup, WiFi Debug, User, Pass, About
// (+ Calibrate Touch on resistive panels — capacitive needs no calibration).
#ifdef HAS_CAP_TOUCH
static const int SET_N = 10;
#else
static const int SET_N = 11;
#endif
// Value string for the chip rows that need it for hit-testing.
static String setChipVal(int row) {
  switch (row) {
    case 0: return theme.themeName();
    case 1: return theme.accentName();
    case 2: return theme.fontColName();
    case 3: return String(theme.bright + 1) + "/20";
    case 4: return String(theme.led_bright) + "/20";
  }
  return "";
}
// Draw one settings row at its slot, highlighted if `sel`. No fillScreen.
static void drawSettingRow(int row, int sel) {
  int y = CONTENTY + row * ITEMH;
  bool s = (row == sel);
  switch (row) {
    case 0: drawChipRow(y, "Theme",      theme.themeName(),  false, s, 0); break;
    case 1: drawChipRow(y, "Accent",     theme.accentName(), false, s, 0); break;
    case 2: drawChipRow(y, "Font Color", theme.fontColName(), false, s, theme.fontColPreview()); break;
    case 3: drawChipRow(y, "Brightness", setChipVal(3), true, s, 0); break;
    case 4: drawChipRow(y, "LED",        setChipVal(4), true, s, 0); break;
    case 5: drawInfoRow(y, "WiFi Setup", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""), s); break;
    case 6: drawInfoRow(y, "WiFi Debug", "", s); break;
    case 7: drawInfoRow(y, "Username",   credGet("user"), s); break;
    case 8: drawInfoRow(y, "Password",   credGet("pass").length() ? String("****") : String(""), s); break;
    case 9: drawInfoRow(y, "About",      "", s); break;
#ifndef HAS_CAP_TOUCH
    case 10: drawInfoRow(y, "Calibrate Touch", "", s); break;
#endif
  }
}

// About — app name/version/author, hardware + build detail rows, credits.
static void aboutScreen() {
  tft->fillScreen(COL_BG);
  drawHeader("About", true);

  // Compact spacing so the whole page fits above the "tap to go back" line on the
  // shorter V8 panel (240x320); the Pancake keeps its original roomier layout.
#ifdef MARAUDER_V8
  const int dName = 26, dSub = 17, dAuth = 18, dRule = 6, dRow = 17, dGap = 2, dRule2 = 6, dCred = 16;
  const int valX = 82;   // value column; keeps "XPT2046 resistive" inside 240 px
  const char *credit1 = "Game by JBlanked";
  const char *credit2 = "Picoware engine";
#else
  const int dName = 32, dSub = 22, dAuth = 24, dRule = 10, dRow = 21, dGap = 4, dRule2 = 8, dCred = 20;
  const int valX = 110;
  const char *credit1 = "FlipWorld game by JBlanked";
  const char *credit2 = "jblanked.com/flipper  -  Picoware";
#endif

  int cx = SCRW / 2, y = CONTENTY + 12;

  // Name + version + author (centred, prominent).
  tft->setTextColor(COL_FG, COL_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(FW_NAME, cx, y, 4); y += dName;
  tft->drawString(String("Version ") + FW_VERSION, cx, y, 2); y += dSub;
  tft->setTextColor(COL_DIM, COL_BG);
  tft->drawString("UI by " FW_AUTHOR, cx, y, 2); y += dAuth;
  tft->drawFastHLine(16, y, SCRW - 32, theme.neon(1, theme.edge())); y += dRule;

  // Label : value detail rows.
  tft->setTextDatum(TL_DATUM);
  auto row = [&](const char *label, const String &value) {
    tft->setTextColor(COL_DIM, COL_BG); tft->drawString(label, 16, y, 2);
    tft->setTextColor(COL_FG, COL_BG);  tft->drawString(value, valX, y, 2);
    y += dRow;
  };
  row("Board",   BOARD_NAME);
  row("MCU",     BOARD_MCU);
  row("Display", BOARD_DISPLAY);
  row("Touch",   BOARD_TOUCH);
  // Actual PSRAM size (0 if absent or init failed), rounded to whole MB.
  {
    size_t ps = ESP.getPsramSize();
    if (ps >= 1024 * 1024)  row("PSRAM", String((unsigned)((ps + 512 * 1024) / (1024 * 1024))) + " MB");
    else if (ps > 0)        row("PSRAM", String((unsigned)(ps / 1024)) + " KB");
    else                    row("PSRAM", "None");
  }
  row("Built",   __DATE__);
  row("Commit",  FW_COMMIT);

  y += dGap;
  tft->drawFastHLine(16, y, SCRW - 32, theme.neon(2, theme.edge())); y += dRule2;
  tft->setTextColor(COL_DIM, COL_BG);
  tft->drawString(credit1, 16, y, 2); y += dCred;
  tft->drawString(credit2, 16, y, 2);

  statusLine("Tap to go back.", COL_DIM);
  uint16_t x, ty; waitTap(x, ty);
}

static void settingsFlow() {
  int sel = -1;
  auto full = [&]() {                         // full repaint (theme/bg changed)
    tft->fillScreen(COL_BG);
    drawHeader("Settings", true);
    for (int i = 0; i < SET_N; i++) drawSettingRow(i, sel);
  };
  auto recolor = [&]() {                      // font colour changed — no fillScreen
    drawHeader("Settings", true);
    for (int i = 0; i < SET_N; i++) drawSettingRow(i, sel);
  };
  full();

  for (;;) {
    uint16_t x, y;
    if (!waitTap(x, y)) continue;
    if (backTapped(x, y)) { ledOff(); return; }     // clear any LED preview on exit
    if ((int)y < CONTENTY) continue;
    int row = ((int)y - CONTENTY) / ITEMH;
    if (row < 0 || row >= SET_N) continue;

    // Move the highlight to the tapped row (partial redraw of old + new).
    int old = sel; sel = row;
    if (old != row) { if (old >= 0) drawSettingRow(old, sel); drawSettingRow(row, sel); }
    if (row != 4) ledOff();                         // LED preview only while on the LED row

    int h = (row <= 4) ? chipHit(CONTENTY + row * ITEMH, setChipVal(row), x, y) : -1;
    switch (row) {
      case 0: if (h >= 0) { theme.cycleTheme(h); theme.save(); applyThemeToViewManager(); full(); } break;
      case 1: if (h >= 0) { theme.cycleAccent(h); theme.save(); applyThemeToViewManager(); drawSettingRow(1, sel); } break;
      case 2: if (h >= 0) { theme.cycleFontCol(h); theme.save(); applyThemeToViewManager(); recolor(); } break;
      case 3: if (h == 0 && theme.bright > 0)  theme.bright--;
              else if (h == 1 && theme.bright < 19) theme.bright++;
              if (h >= 0) { theme.save(); applyBrightness(); drawSettingRow(3, sel); } break;
      case 4: if (h == 0 && theme.led_bright > 0)  theme.led_bright--;
              else if (h == 1 && theme.led_bright < 20) theme.led_bright++;
              if (h >= 0) { theme.save(); drawSettingRow(4, sel); }
              ledWifi(); break;                     // live preview at the new brightness
      case 5: wifiSetup(); full(); break;
      case 6: wifiDebug(); full(); break;
      case 7: { char b[64] = {0}; String u = credGet("user"); strncpy(b, u.c_str(), sizeof(b) - 1);
                if (touchKeyboardInput(*tft, COL_FG, COL_BG, b, sizeof(b), "FlipWorld User:", false))
                  credSet("user", String(b));
                full(); } break;
      case 8: { char b[64] = {0}; String p = credGet("pass"); strncpy(b, p.c_str(), sizeof(b) - 1);
                if (touchKeyboardInput(*tft, COL_FG, COL_BG, b, sizeof(b), "FlipWorld Password:", true))
                  credSet("pass", String(b));
                full(); } break;
      case 9: aboutScreen(); full(); break;
#ifndef HAS_CAP_TOUCH
      case 10: touchCalRun(); full(); break;   // resistive drifts — allow a redo
#endif
      default: break;
    }
  }
}


// ═════════════════════════════════════════════════════════════════════════════
//  FlipWorld — top-down action game ported from Picoware (jblanked/FlipWorld).
//  Rendered in full colour: the original 1-bit Flipper sprites are blitted per
//  entity in their own ink colour over a coloured world (see Draw::imageMaskPGM
//  and the ink_color assignments in the flipworld/ engine files).
//
//  This shell is touch-only, so the game is driven through the same tap-zones the
//  rest of the UI uses (InputManager maps screen edges to BUTTON_UP/DOWN/LEFT/
//  RIGHT and the centre to BUTTON_CENTER). Input is reported continuously while
//  the panel is held, so:
//    • hold the top / bottom / left / right edge → move continuously
//    • hold the centre                           → attack
//    • tap the header Back button (top-left)     → exit to the menu
// ═════════════════════════════════════════════════════════════════════════════

// The world background is solid black; sprites are drawn over it in full colour.
static const uint16_t FW_WORLD_BG = TFT_BLACK;

// ── FlipWorld progression (SD-persisted, online-authoritative) ────────────────
// Player stats live on SD at /flipworld_stats.json so the game plays fully
// offline. When signed in (Settings → Username/Password) AND WiFi is up, we pull
// the server copy at game start and OVERWRITE the SD file with it — online is the
// source of truth. On exit we save back to SD and, if online, push to the server
// so it stays the master for next time. Same jblanked.com account as FlipSocial.
static const char *FW_STATS_FILE = "/flipworld_stats.json";
static const char *FW_API        = "https://www.jblanked.com/flipper/api/user/";

static bool fwOnline() { return WiFi.status() == WL_CONNECTED && credGet("user").length() > 0; }

static Entity *fwFindPlayer(Level *level) {
  for (int i = 0; i < level->getEntityCount(); i++) {
    Entity *e = level->getEntity(i);
    if (e && e->type == ENTITY_PLAYER) return e;
  }
  return nullptr;
}

// Fill s from a {"game_stats":{...}} JSON document (server or SD file share it).
static bool fwStatsFromDoc(JsonDocument &d, const String &fallbackName, FWStats &s) {
  JsonObject g = d["game_stats"];
  if (g.isNull()) return false;
  s.name       = g["username"]   | fallbackName;
  s.level      = g["level"]      | 1.0f;
  s.xp         = g["xp"]         | 0.0f;
  s.health     = g["health"]     | 100.0f;
  s.max_health = g["max_health"] | 100.0f;
  s.strength   = g["strength"]   | 10.0f;
  s.valid = true;
  return true;
}

static bool fwStatsReadSD(FWStats &s) {
  s.valid = false;
  File f = SD.open(FW_STATS_FILE, FILE_READ);
  if (!f) return false;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return false;
  return fwStatsFromDoc(d, "Player", s);
}

static void fwStatsWriteSD(const FWStats &s) {
  JsonDocument d;
  JsonObject g = d["game_stats"].to<JsonObject>();
  g["username"]   = s.name;
  g["level"]      = (int)s.level;
  g["xp"]         = (long)s.xp;
  g["health"]     = (int)s.health;
  g["max_health"] = (int)s.max_health;
  g["strength"]   = s.strength;
  SD.remove(FW_STATS_FILE);                 // truncate: FILE_WRITE won't shrink a file
  File f = SD.open(FW_STATS_FILE, FILE_WRITE);
  if (!f) return;
  serializeJson(d, f);
  f.close();
}

// GET /game-stats/{user}/ → overwrite s. Blocks on the TLS request.
static bool fwStatsFetchOnline(FWStats &s) {
  s.valid = false;
  String user = credGet("user"), pass = credGet("pass");
  if (user.length() == 0) return false;
  HTTP http;
  const char *hk[] = {"Content-Type", "Username", "Password"};
  const char *hv[] = {"application/json", user.c_str(), pass.c_str()};
  ledHttp();
  String r = http.request("GET", String(FW_API) + "game-stats/" + user + "/", "", hk, hv, 3);
  ledOff();
  if (r.length() == 0) return false;
  JsonDocument d;
  if (deserializeJson(d, r)) return false;
  return fwStatsFromDoc(d, user, s);
}

// POST /update-game-stats/ with the current progression (best-effort).
static void fwStatsPushOnline(const FWStats &s) {
  String user = credGet("user"), pass = credGet("pass");
  if (user.length() == 0) return;
  JsonDocument d;
  d["username"] = user;
  JsonObject g = d["game_stats"].to<JsonObject>();
  g["username"]   = user;
  g["level"]      = (int)s.level;
  g["xp"]         = (long)s.xp;
  g["health"]     = (int)s.health;
  g["max_health"] = (int)s.max_health;
  g["strength"]   = s.strength;
  String payload; serializeJson(d, payload);
  HTTP http;
  const char *hk[] = {"Content-Type", "Username", "Password"};
  const char *hv[] = {"application/json", user.c_str(), pass.c_str()};
  ledHttp();
  http.request("POST", String(FW_API) + "update-game-stats/", payload, hk, hv, 3);
  ledOff();
}

// After player_spawn: load progression (online overwrites SD; else SD) and apply.
static void flipWorldStatsApply(Level *level) {
  Entity *p = fwFindPlayer(level);
  if (!p) return;
  FWStats s; s.valid = false;
  if (fwOnline()) {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_ACCENT, COL_BG);
    tft->drawString("Syncing stats...", SCRW / 2, SCRH - 20, 2);
    tft->setTextDatum(TL_DATUM);
    if (fwStatsFetchOnline(s) && s.valid) fwStatsWriteSD(s);   // online is authoritative
  }
  if (!s.valid) fwStatsReadSD(s);           // offline or fetch failed → SD copy
  if (!s.valid) return;                     // brand-new player → keep spawn defaults
  p->level      = s.level;
  p->xp         = s.xp;
  p->health     = s.health;
  p->max_health = s.max_health;
  p->strength   = s.strength;
}

// Before teardown: save progression to SD, and push online if signed in.
static void flipWorldStatsSave(Level *level) {
  Entity *p = fwFindPlayer(level);
  if (!p) return;
  FWStats s;
  s.name       = credGet("user").length() ? credGet("user") : String("Player");
  s.level      = p->level;
  s.xp         = p->xp;
  s.health     = p->health;
  s.max_health = p->max_health;
  s.strength   = p->strength;
  s.valid      = true;
  fwStatsWriteSD(s);
  if (fwOnline()) fwStatsPushOnline(s);
}

// Controls splash — shown until the player taps (or a 15 s timeout).
static void flipWorldIntro() {
  tft->fillScreen(COL_BG);
  drawHeader("FlipWorld", false);
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(COL_FG, COL_BG);
  int cy = SCRH / 2;
  tft->drawString("Hold edges to move",   SCRW / 2, cy - 34, 2);
  tft->drawString("Hold centre to attack", SCRW / 2, cy - 12, 2);
  tft->drawString("Back button to exit",  SCRW / 2, cy + 10, 2);
  tft->setTextColor(COL_ACCENT, COL_BG);
  tft->drawString("Tap to begin",         SCRW / 2, cy + 44, 4);
  tft->setTextDatum(TL_DATUM);

  InputManager *im = vm->getInputManager();
  TouchInput   *t  = im->getTouch();
  im->reset(true, 200);                    // swallow the tap that opened this screen
  uint32_t t0 = millis();
  bool wasDown = t && t->isPressed();
  while (millis() - t0 < 15000) {
    im->run();
    bool down = t && t->isPressed();
    if (down && !wasDown) break;           // fresh tap → start
    wasDown = down;
    delay(15);
  }
  im->reset(true, 250);
}

// The three worlds, played in order (like the Flipper build's LevelHomeWoods /
// LevelRockWorld / LevelForestWorld). All three are built up front as levels of
// one Game and share a single player entity; game->level_switch() moves between
// them. The reference only switches levels over multiplayer, so for single-player
// we advance when every enemy in the current world has been defeated.
static const int   FW_WORLD_COUNT = 3;
static const char *FW_WORLD_NAMES[FW_WORLD_COUNT] = { "Home Woods", "Rock World", "Forest World" };

// Count enemies still alive (not yet marked ENTITY_DEAD) in a level.
static int fwLivingEnemies(Level *level) {
  int n = 0;
  for (int i = 0; i < level->getEntityCount(); i++) {
    Entity *e = level->getEntity(i);
    if (e && e->type == ENTITY_ENEMY && e->state != ENTITY_DEAD) n++;
  }
  return n;
}

// Brief full-screen title card shown when a world is entered.
static void fwWorldBanner(int idx) {
  tft->fillScreen(COL_BG);
  drawHeader(FW_WORLD_NAMES[idx], true);
  tft->setTextDatum(MC_DATUM);
  tft->setTextColor(COL_DIM, COL_BG);
  char sub[24]; snprintf(sub, sizeof(sub), "World %d of %d", idx + 1, FW_WORLD_COUNT);
  tft->drawString(sub, SCRW / 2, SCRH / 2 - 16, 2);
  tft->setTextColor(COL_ACCENT, COL_BG);
  tft->drawString(FW_WORLD_NAMES[idx], SCRW / 2, SCRH / 2 + 12, 4);
  tft->setTextDatum(TL_DATUM);
  delay(1200);
}

static void playFlipWorld() {
  flipWorldIntro();

  const char *worldJson[FW_WORLD_COUNT] = { world_home_woods, world_rock_world, world_forest_world };

  Board board = vm->getBoard();

  Game *game = new Game(
      "FlipWorld",
      Vector(board.width, board.height),
      vm->getDraw(),
      vm->getInputManager(),
      TFT_WHITE,             // fg — used by the in-game text overlays
      FW_WORLD_BG,           // bg — solid black world
      CAMERA_FIRST_PERSON,
      nullptr,
      FlipWorld::game_stop);

  // Build all three worlds and add them to the game.
  Level *worlds[FW_WORLD_COUNT];
  for (int i = 0; i < FW_WORLD_COUNT; i++) {
    worlds[i] = new Level(FW_WORLD_NAMES[i], Vector(768, 384), game, NULL, NULL);
    game->level_add(worlds[i]);
    FlipWorld::icon_spawn_json(worlds[i], worldJson[i]);
    FlipWorld::enemy_spawn_json(worlds[i], worldJson[i]);
  }

  // One player, shared across every world (is_player keeps Level::clear from
  // deleting it when a level is torn down).
  FlipWorld::player_spawn(worlds[0], "sword", Vector(384, 192));
  Entity *player = fwFindPlayer(worlds[0]);
  if (player) {
    worlds[1]->entity_add(player);
    worlds[2]->entity_add(player);
  }

  flipWorldStatsApply(worlds[0]);   // saved/online progression onto the shared player

  game->pos     = Vector(384, 192);
  game->old_pos = game->pos;
  game->level_switch(0);

  GameEngine *engine = new GameEngine(game, 60);

  InputManager *im = vm->getInputManager();
  TouchInput   *t  = im->getTouch();
  int  worldIdx = 0;
  bool exiting   = false;
  fwWorldBanner(0);
  drawHeader(FW_WORLD_NAMES[worldIdx], true);
  while (!exiting) {
    im->run();

    // Header Back button exits (checked before the frame so the tap doesn't also
    // move the player up). Only a tap inside the top-left back box counts.
    if (t && t->isPressed() && backTapped(t->x(), t->y())) {
      exiting = true;
      break;
    }

    // Wipe the play area (below the header) so nothing smears as the camera pans.
    tft->fillRect(0, HDRH, SCRW, SCRH - HDRH, TFT_BLACK);
    engine->runAsync(false);
    drawHeader(FW_WORLD_NAMES[worldIdx], true);

    // Cleared every enemy in this world → advance to the next (or win).
    if (fwLivingEnemies(worlds[worldIdx]) == 0) {
      if (worldIdx + 1 < FW_WORLD_COUNT) {
        worldIdx++;
        if (player) { player->position = Vector(384, 192); player->old_position = player->position; }
        game->pos = Vector(384, 192); game->old_pos = game->pos;
        game->level_switch(worldIdx);
        fwWorldBanner(worldIdx);
        drawHeader(FW_WORLD_NAMES[worldIdx], true);
      } else {
        tft->fillScreen(COL_BG);
        drawHeader("FlipWorld", true);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->drawString("All worlds cleared!", SCRW / 2, SCRH / 2, 4);
        tft->setTextDatum(TL_DATUM);
        delay(2500);
        exiting = true;
        break;
      }
    }

    delay(1000 / 60);
  }

  flipWorldStatsSave(worlds[0]);   // persist progression (shared player, any level works)

  engine->stop();          // stops the game, clears the screen, deletes the game (+ levels)
  delete engine;
  if (player) delete player;   // Game deleted the levels but Level::clear skipped is_player
  im->reset(true, 200);
}

// Main menu (H4W9-style large rounded buttons)
static const char *MENU_ITEMS[] = { "Play FlipWorld", "Settings" };
static const int    MENU_COUNT  = 2;
static const int    MENU_MARGIN = 16;
static const int    MENU_TOP    = CONTENTY + 12;
static const int    MENU_GAP    = 12;
static int menuBtnH() {
  int avail = SCRH - MENU_TOP - 12;
  return (avail - (MENU_COUNT - 1) * MENU_GAP) / MENU_COUNT;
}
static int menuBtnY(int i) { return MENU_TOP + i * (menuBtnH() + MENU_GAP); }
static int menuButtonAt(uint16_t x, uint16_t y) {
  if ((int)x < MENU_MARGIN || (int)x >= SCRW - MENU_MARGIN) return -1;
  int bh = menuBtnH();
  for (int i = 0; i < MENU_COUNT; i++) {
    int by = menuBtnY(i);
    if ((int)y >= by && (int)y < by + bh) return i;
  }
  return -1;
}

static void drawMenu() {
  tft->fillScreen(COL_BG);
  drawHeader("FlipWorld", false);
  int bh = menuBtnH();
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = menuBtnY(i);
    tft->fillRoundRect(MENU_MARGIN, y, SCRW - 2 * MENU_MARGIN, bh, 12, COL_ACCENT);
    tft->drawRoundRect(MENU_MARGIN, y, SCRW - 2 * MENU_MARGIN, bh, 12, theme.neon(i * 3, COL_DIM));
    tft->setTextColor(COL_FG, COL_ACCENT);
    tft->setTextDatum(MC_DATUM);
    // V8's buttons are ~34 px tall — font 4 (26 px) crowds them, so use font 2.
#ifdef MARAUDER_V8
    tft->drawString(MENU_ITEMS[i], SCRW / 2, y + bh / 2, 2);
#else
    tft->drawString(MENU_ITEMS[i], SCRW / 2, y + bh / 2, 4);
#endif
  }
  tft->setTextDatum(TL_DATUM);
}

static void openMenuItem(int i) {
  switch (i) {
    case 0: playFlipWorld(); break;   // Play — blocking game loop, returns on hold-to-exit
    case 1: settingsFlow();  break;   // WiFi, theme, brightness, About
    default: break;
  }
  drawMenu();
}

static bool mainMenuStart(ViewManager *viewManager) {
  drawMenu();
  return true;
}

static void mainMenuRun(ViewManager *viewManager) {
  static bool wasDown = false;
  TouchInput *t = viewManager->getInputManager()->getTouch();
  bool down = t->isPressed();
  if (down && !wasDown) {                              // fresh tap (press edge)
    uint16_t x = t->x(), y = t->y();
    int btn = menuButtonAt(x, y);
    if (btn >= 0) openMenuItem(btn);
  }
  wasDown = down;

  // Idle refresh of ONLY the header status corner (WiFi icon + battery). The menu
  // buttons don't depend on WiFi, so never repaint the whole screen here — doing so
  // made the menu flash repeatedly while the background connect cycled states.
  static uint32_t lastRefresh = 0;
  static int lastStatus = -2;
  static bool lastConn = false;
  if (WiFi.status() != lastStatus || g_wifiConnecting != lastConn || millis() - lastRefresh > 4000) {
    lastRefresh = millis();
    lastStatus  = WiFi.status();
    lastConn    = g_wifiConnecting;
    drawHeaderStatus();
  }
}

static const PROGMEM View mainMenuView = View("MainMenu", mainMenuRun, mainMenuStart, nullptr);

// Arduino entry points
void setup() {
  randomSeed(esp_random());
#ifndef DEVELOPER
  esp_log_level_set("*", ESP_LOG_NONE);
#endif

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.println(F("[" BOARD_NAME "] FlipWorld starting..."));

  // Backlight off during init (PWM).
  pinMode(TFT_BL, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 0);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, 0);
#endif

  // SD (shared FSPI bus on ESP32-C5) — must be up before ViewManager (Storage).
#ifdef HAS_C5_SD
  sharedSPI.begin(SD_SCK, SD_MISO, SD_MOSI);
  delay(100);
  if (!SD.begin(SD_CS, sharedSPI)) Serial.println(F("[" BOARD_NAME "] SD init failed"));
  else Serial.println(F("[" BOARD_NAME "] SD OK"));
#else
  if (!SD.begin(SD_CS)) Serial.println(F("[" BOARD_NAME "] SD init failed"));
#endif

  // SPIFFS for settings + credentials (format on first boot).
  if (!SPIFFS.begin(true)) Serial.println(F("[" BOARD_NAME "] SPIFFS mount failed"));
  else                     Serial.println(F("[" BOARD_NAME "] SPIFFS OK"));

#ifdef HAS_PSRAM
  if (!psramInit()) Serial.println(F("[" BOARD_NAME "] PSRAM unavailable"));
#endif

#ifdef HAS_CAP_TOUCH
  // Capacitive touch (also does Wire.begin on the shared I2C bus).
  ft6336_init();
#else
  // V8 has no I2C touch controller (XPT2046 rides the SPI bus), but the fuel
  // gauge below still needs the I2C bus that ft6336_init() would have opened.
  Wire.begin(I2C_SDA, I2C_SCL, 400000U);
#endif
  battInit();                          // MAX17048 fuel gauge on the same I2C bus

  // Load persisted theme/accent/font/brightness before anything draws.
  theme.load();

  // Put the status LED in a known-off state (also arms the V8's PWM channel).
  ledOff();

  // ViewManager owns the panel (Draw) and touch (InputManager).
#ifdef MARAUDER_V8
  vm    = new ViewManager(MarauderV8Config);
#else
  vm    = new ViewManager(PancakeConfig);
#endif
  tft   = vm->getDraw()->display->getTFT();
  touch = vm->getInputManager()->getTouch();
  applyThemeToViewManager();

  // Backlight on at the saved brightness.
  applyBrightness();

#ifndef HAS_CAP_TOUCH
  // Resistive panel: point TouchInput at TFT_eSPI's XPT2046 reader, then load
  // the stored calibration (or run the wizard). Must come after the backlight is
  // up, or a first-boot user would be tapping an unlit screen.
  if (touch) touch->attachTFT(tft);
  touchCalInit();
#endif

  // WiFi: capture disconnect reasons for diagnostics.
  WiFi.onEvent(wifiEvent);
  WiFi.mode(WIFI_STA);

  // Show the main menu immediately, then connect to saved WiFi in the background
  // (header icon + LED report progress).
  vm->add(&mainMenuView);
  vm->set("MainMenu");
  wifiBgBegin();
  drawHeaderStatus();                  // show the "connecting" (yellow) icon at once

  Serial.println(F("[" BOARD_NAME "] Ready."));
}

void loop() {
  vm->run();
  wifiBgTick();                        // advance the background WiFi connect

  // Reconnect watchdog: ONLY on the drop edge (connected -> lost), make one
  // reconnect pass (the two closest saved nets). If it fails we stay disconnected
  // rather than retrying forever — the LED goes off instead of pulsing amber.
  static bool wasConnected = false;
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (wasConnected && !nowConnected && !g_manualDisconnect && (g_wb == WB_DONE || g_wb == WB_IDLE)) {
    wifiBgBegin();                      // scans + reconnects to the closest saved network
  }
  wasConnected = nowConnected;

  // Activity LED mirrors the connecting state (on while scanning/associating).
  // Note: the FlipWorld game blocks loop() while playing (its own frame loop).
  static bool ledState = false;
  if (g_wifiConnecting != ledState) { ledState = g_wifiConnecting; ledSet(ledState); }

  delay(5);
}
