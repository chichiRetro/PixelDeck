/*
  PXL//DECK  - cyberpunk pixel-art display for ESP32-2432S028 (CYD, 2.8" ILI9341)

  - Shows GIF / PNG / BMP pixel art from the microSD card (/pixel folder), LED-matrix style
  - Built-in original character "Bitto" (works without SD card)
  - Wi-Fi: first boot opens a setup hotspot "PXL-DECK-xxxx" -> enter your Wi-Fi password from your phone
  - Web control panel: http://pxldeck.local (or the IP shown on screen) to upload / delete / show images

  Touch:  tap = next image,  hold 0.7s = AUTO <-> HOLD
  BOOT button: show network info.  Hold BOOT while powering on (3s) = forget Wi-Fi settings

  Libraries: LovyanGFX, AnimatedGIF (bitbank2).  Board: "ESP32 Dev Module", Partition: "Huge APP"
*/
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <LovyanGFX.hpp>
#include <AnimatedGIF.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <vector>
#include "esp_system.h"
#include "default_art.h"
#include "web_pages.h"

// ===================== settings =====================
#ifndef PANEL_ST7789
#define PANEL_ST7789     1      // 1 if your board uses ST7789 (colors look wrong / inverted)
#endif
#define PANEL_INVERT     false  // flip if colors look inverted
#define PANEL_RGB_ORDER  false  // flip if red/blue are swapped
#define SLIDE_SECONDS    10     // auto-advance interval
#define LED_GRID         true   // gaps between dots (LED-matrix look)
#define RGB_LED_ENABLE   true   // pulse the on-board RGB LED
#define HOSTNAME         "pxldeck"
#define FW_VERSION       "1.1"
#define MAX_IMG          128    // max image size (px)
#define GLITCH_FX        false  // glitch transition effect
#define MOTION_MS        180    // bob / sparkle frame time for still images
// ====================================================

// ---- pins (CYD) ----
#define TOUCH_IRQ 36
#define SD_CS 5
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define BOOT_BTN 0

class LGFX : public lgfx::LGFX_Device {
#if PANEL_ST7789
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0; c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = 12; c.pin_dc = 2;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 15; c.pin_rst = -1; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 320; c.offset_rotation = 0;
      c.readable = true; c.invert = PANEL_INVERT; c.rgb_order = PANEL_RGB_ORDER; c.dlen_16bit = false; c.bus_shared = false;
      _panel.config(c); }
    { auto c = _light.config(); c.pin_bl = 21; c.invert = false; c.freq = 12000; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

LGFX lcd;
RTC_NOINIT_ATTR uint32_t crashMagic, crashCount;
bool safeMode = false;
RTC_NOINIT_ATTR char rtcLoading[48];   // file being decoded (to detect a crash while loading)
RTC_NOINIT_ATTR char rtcBad[48];       // file that crashed last time -> skipped
String badFile;
RTC_NOINIT_ATTR char rtcStage[12];      // where the last run was when it died
String lastStage;
void setStage(const char *st) { strncpy(rtcStage, st, sizeof(rtcStage) - 1); rtcStage[sizeof(rtcStage) - 1] = 0; }
esp_reset_reason_t resetReason;
SPIClass sdSPI(VSPI);
AnimatedGIF gif;
WebServer server(80);
DNSServer dns;
Preferences prefs;

// ---- colors ----
#define C_BG     C_BGT
static uint16_t C_CY, C_MG, C_YL, C_DIM, C_TX, C_OFF, C_PANEL, C_SCAN, C_GRN;
static const uint16_t TKEY = 0x0020;   // "transparent" marker

// ===================== themes =====================
// colors: bg, primary, secondary, highlight, dim, panel, scan, off(unlit dot), text, good, pattern
// pattern: 0 none 1 stars 2 checker 3 hearts 4 polka 5 grass 6 waves 7 sky gradient 8 bricks 9 grid 10 flowers
enum { P_NONE, P_STARS, P_CHECK, P_HEART, P_POLKA, P_GRASS, P_WAVE, P_SKY, P_BRICK, P_GRID, P_FLOWER, P_SNOW, P_NOTE, P_LEAF, P_LCD, P_STRIPE, P_SPRINKLE };
struct Theme { uint8_t c[11][3]; uint8_t pat; };
static const Theme THEMES[] = {
  {{{0,0,0},{46,249,255},{255,43,214},{248,243,43},{120,100,190},{40,24,80},{10,6,22},{12,9,26},{216,247,255},{60,255,140},{24,18,48}}, P_GRID},          // 0 NEON CITY
  {{{10,2,24},{255,120,40},{200,60,255},{255,220,90},{140,80,170},{50,20,70},{18,6,30},{20,8,32},{255,230,240},{255,160,80},{60,20,70}}, P_SKY},         // 1 SYNTHWAVE
  {{{0,4,0},{60,255,90},{0,160,60},{200,255,120},{40,120,60},{10,50,20},{0,14,4},{4,16,6},{180,255,190},{120,255,120},{10,40,14}}, P_GRID},              // 2 MATRIX
  {{{6,0,0},{255,40,60},{150,0,30},{255,200,200},{150,60,70},{60,10,16},{18,2,4},{20,4,6},{255,220,220},{255,90,90},{40,6,10}}, P_CHECK},                // 3 CRIMSON
  {{{8,4,0},{255,170,30},{200,90,0},{255,230,150},{150,100,40},{60,36,8},{20,10,0},{22,12,2},{255,220,160},{255,200,80},{36,20,4}}, P_GRID},             // 4 AMBER CRT
  {{{14,6,30},{255,110,200},{60,230,220},{255,240,140},{150,110,200},{60,30,90},{22,10,40},{26,12,44},{255,235,250},{120,255,220},{70,30,90}}, P_SKY},   // 5 VAPORWAVE
  {{{0,0,0},{255,255,255},{150,150,150},{255,255,255},{110,110,110},{45,45,45},{12,12,12},{16,16,16},{230,230,230},{200,200,200},{30,30,30}}, P_CHECK},  // 6 MONOCHROME
  {{{255,236,242},{255,120,170},{255,180,200},{255,90,140},{220,150,175},{250,205,220},{250,226,234},{252,228,236},{120,50,80},{255,110,160},{255,205,220}}, P_HEART},   // 7 SAKURA
  {{{232,255,236},{40,200,110},{255,120,150},{255,170,60},{120,190,140},{200,240,210},{220,250,226},{226,252,230},{30,90,60},{40,190,100},{200,240,210}}, P_POLKA},    // 8 MELON SODA
  {{{240,232,255},{150,110,255},{255,150,220},{120,200,255},{170,150,220},{220,205,250},{232,222,252},{236,228,254},{70,50,120},{130,100,250},{214,200,250}}, P_STARS}, // 9 LAVENDER DREAM
  {{{255,248,230},{255,90,150},{60,190,255},{255,200,40},{220,170,160},{255,225,200},{255,240,220},{255,244,226},{110,60,70},{80,200,120},{255,222,200}}, P_CHECK},    // 10 CANDY POP
  {{{255,240,244},{235,70,110},{255,200,215},{255,170,190},{220,150,165},{250,210,220},{252,230,236},{254,236,240},{130,40,60},{230,60,100},{250,212,222}}, P_POLKA},  // 11 STRAWBERRY MILK
  {{{250,246,255},{255,140,200},{120,200,255},{170,240,180},{200,180,230},{235,225,250},{244,238,252},{246,242,255},{110,90,140},{150,220,255},{222,236,255}}, P_SKY}, // 12 UNICORN
  {{{255,244,248},{255,80,130},{255,255,255},{255,200,60},{230,160,180},{255,214,226},{255,234,240},{255,240,245},{120,40,70},{255,90,140},{255,214,226}}, P_HEART},   // 13 RIBBON PINK
  {{{14,30,16},{150,230,90},{230,200,90},{255,245,200},{110,150,90},{36,70,36},{18,38,20},{20,44,22},{235,240,210},{150,230,90},{30,62,30}}, P_GRASS},              // 14 FOREST QUEST
  {{{18,22,30},{80,230,220},{230,180,90},{200,255,250},{110,130,140},{50,60,70},{24,28,36},{28,32,40},{220,235,235},{80,230,220},{40,46,56}}, P_BRICK},             // 15 ANCIENT RUINS
  {{{200,236,150},{230,60,60},{60,140,230},{255,220,60},{120,170,90},{170,215,120},{190,230,140},{186,226,136},{40,70,30},{230,60,60},{160,206,110}}, P_GRASS},     // 16 GRASS ROUTE
  {{{250,240,210},{60,180,220},{240,140,80},{255,255,255},{190,170,130},{235,220,180},{245,232,200},{235,246,250},{90,70,40},{100,200,120},{200,232,244}}, P_WAVE},  // 17 COZY ISLAND
  {{{4,8,28},{255,215,100},{120,150,255},{255,255,220},{100,110,170},{24,34,80},{8,14,40},{10,16,44},{240,236,210},{255,215,100},{90,100,170}}, P_STARS},           // 18 STARRY NIGHT
  {{{20,4,26},{255,100,200},{190,120,255},{255,220,120},{170,100,180},{70,24,80},{30,8,38},{34,10,42},{255,230,250},{255,160,230},{70,26,80}}, P_FLOWER},          // 19 MAGICAL GIRL
  // ---- retro handheld ----
  {{{15,56,15},{155,188,15},{139,172,15},{200,220,120},{48,98,48},{48,98,48},{20,62,20},{139,172,15},{15,56,15},{155,188,15},{128,160,14}}, P_LCD},              // 20 POCKET LCD
  {{{40,44,40},{200,206,190},{150,156,140},{230,234,220},{100,106,96},{70,76,70},{46,50,46},{176,182,166},{30,34,30},{200,206,190},{160,166,152}}, P_LCD},       // 21 POCKET GRAY
  {{{24,16,48},{120,200,255},{200,120,255},{255,220,120},{90,80,150},{50,40,90},{30,22,58},{150,130,220},{240,230,255},{120,230,200},{132,114,204}}, P_LCD},     // 22 POCKET BERRY
  // ---- seasons ----
  {{{246,252,236},{255,140,180},{120,200,110},{255,210,80},{170,200,150},{220,240,200},{236,248,224},{240,250,232},{60,90,50},{120,200,110},{222,240,206}}, P_FLOWER},  // 23 SPRING MEADOW
  {{{8,12,40},{255,120,80},{90,200,255},{255,230,120},{90,100,160},{30,40,90},{12,18,50},{14,20,56},{240,240,255},{120,230,255},{40,52,110}}, P_WAVE},           // 24 SUMMER FESTIVAL
  {{{40,20,10},{255,140,40},{220,60,40},{255,210,90},{160,110,70},{80,44,24},{48,26,14},{54,30,16},{255,230,200},{255,170,60},{84,46,22}}, P_LEAF},             // 25 AUTUMN MAPLE
  {{{14,6,20},{255,140,20},{150,70,220},{200,255,90},{120,80,140},{50,24,60},{22,10,30},{24,12,32},{255,220,180},{255,160,40},{60,26,70}}, P_STARS},            // 26 HALLOWEEN
  {{{226,240,252},{90,160,230},{150,200,250},{255,255,255},{150,180,210},{200,220,240},{216,232,248},{232,242,252},{40,70,110},{90,160,230},{255,255,255}}, P_SNOW},  // 27 WINTER SNOW
  {{{10,40,24},{230,40,50},{60,180,90},{255,215,90},{110,140,110},{30,70,44},{14,48,30},{16,52,32},{255,240,230},{60,200,100},{56,30,30}}, P_STRIPE},          // 28 HOLIDAY NIGHT
  {{{255,246,236},{220,40,50},{230,180,60},{255,90,90},{210,160,140},{250,220,200},{252,236,224},{254,242,232},{110,30,30},{220,40,50},{250,214,196}}, P_CHECK},     // 29 NEW YEAR
  // ---- virtual singer ----
  {{{6,10,20},{60,230,210},{255,80,170},{230,240,255},{80,120,140},{20,40,56},{8,16,28},{10,18,30},{220,250,250},{60,230,210},{20,46,60}}, P_NOTE},             // 30 VIRTUAL STAGE
  {{{246,252,254},{40,200,200},{255,110,170},{120,140,255},{150,190,200},{210,236,240},{236,246,250},{242,250,252},{40,80,90},{40,200,200},{200,236,240}}, P_NOTE}, // 31 DIGITAL DIVA
  {{{30,20,4},{255,200,40},{255,120,40},{255,250,180},{170,140,80},{70,50,16},{40,28,6},{44,30,8},{255,245,220},{255,210,60},{80,58,18}}, P_STRIPE},           // 32 ELECTRO POP
  {{{10,8,34},{120,140,255},{255,110,150},{255,220,240},{110,100,170},{36,30,80},{16,12,44},{18,14,48},{230,230,255},{150,170,255},{40,34,90}}, P_NOTE},        // 33 MIDNIGHT SYNTH
  {{{20,16,36},{160,240,255},{230,160,255},{255,255,255},{140,130,190},{50,44,84},{26,22,46},{28,24,50},{240,240,255},{180,255,230},{60,52,100}}, P_GRID},      // 34 HOLOGRAM
  // ---- sweets & pink ----
  {{{255,244,248},{250,150,185},{140,215,195},{250,190,120},{225,175,190},{255,222,232},{255,236,242},{255,240,246},{130,70,90},{140,215,195},{255,214,226}}, P_SPRINKLE}, // 35 ICE CREAM
  {{{46,26,22},{255,150,190},{140,230,210},{255,230,190},{150,110,100},{80,50,44},{56,34,30},{60,38,32},{255,235,225},{140,230,210},{90,56,48}}, P_SPRINKLE},       // 36 MIDNIGHT SUNDAE
  {{{255,240,245},{245,150,185},{255,196,214},{240,120,165},{230,170,190},{255,218,230},{255,232,240},{255,236,243},{140,60,90},{245,140,180},{255,214,228}}, P_HEART},  // 37 PALE PINK
  {{{252,242,248},{225,150,200},{175,165,240},{250,160,195},{205,175,210},{240,220,238},{248,234,244},{252,240,248},{90,60,100},{170,190,245},{240,218,236}}, P_FLOWER}, // 38 UTAKATA
};
static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
int themeIdx = 0;
static uint16_t C_BGT = 0, C_SP3[3], C_PAT = 0;
uint16_t tc(int i) { auto &c = THEMES[themeIdx].c[i]; return lgfx::color565(c[0], c[1], c[2]); }
void applyThemeColors() {
  C_BGT = tc(0); C_CY = tc(1); C_MG = tc(2); C_YL = tc(3); C_DIM = tc(4); C_PANEL = tc(5); C_SCAN = tc(6); C_OFF = tc(7); C_TX = tc(8); C_GRN = tc(9);
  if (C_OFF == 0x0020) C_OFF = 0x0040;
  C_SP3[0] = C_CY; C_SP3[1] = C_YL; C_SP3[2] = C_MG; C_PAT = tc(10); if (C_PAT == 0x0020) C_PAT = 0x0040;
}


// ---- layout ----
static const int ART_X = 10, ART_Y = 16, ART_W = 208, ART_H = 208;
static const int SB_X = 232;

// ---- playlist ----
enum { T_ROM = 0, T_GIF, T_PNG, T_BMP, T_PXL };
struct Item { String name; uint8_t type; uint32_t size; };
std::vector<Item> items;
int cur = 0;
bool autoMode = true;
bool motion = true;          // bob + sparkles for still images
int motionFrame = 0;
struct Spark { uint8_t x, y, c, ph; };
Spark sparks[12]; int nSparks = 0;
uint16_t *disp = nullptr;    // composed canvas
int dispW = 0, dispH = 0;
bool sdOK = false;

// ---- image buffers ----
uint16_t *img = nullptr;    // current picture
uint16_t *shown = nullptr;  // what is on screen (for diff)
int imgW = 32, imgH = 32;
bool imgValid = false;
String errText = "";

// ---- playback state ----
uint32_t itemStart = 0, nextFrameAt = 0;
bool gifOpen = false, gifLoopEnded = false;
int romStep = 0;
static const uint8_t ROM_SEQ[]  = {0, 2, 0, 2, 0, 2, 0, 1, 0, 2, 0, 2};
static const uint16_t ROM_DUR[] = {500, 500, 500, 500, 500, 500, 900, 140, 500, 500, 500, 500};

// ---- net state ----
enum { NET_OFF, NET_STA, NET_AP };
int netMode = NET_OFF;
String apName, ipText = "", ssidSaved;
String optionsHtml;
File uploadFile;
bool uploading = false;
String uploadedName;

// =============================================================== utils
static String extOf(const String &n) { int p = n.lastIndexOf('.'); String e = p >= 0 ? n.substring(p + 1) : ""; e.toLowerCase(); return e; }
static uint8_t typeOf(const String &n) { String e = extOf(n); if (e == "gif") return T_GIF; if (e == "png") return T_PNG; if (e == "bmp") return T_BMP; if (e == "pxl") return T_PXL; return 255; }
static const char *typeName(uint8_t t) { switch (t) { case T_GIF: return "GIF"; case T_PNG: return "PNG"; case T_BMP: return "BMP"; case T_PXL: return "PXL"; default: return "ROM"; } }
static String cleanName(String n) {
  int s = n.lastIndexOf('/'); if (s >= 0) n = n.substring(s + 1);
  s = n.lastIndexOf('\\'); if (s >= 0) n = n.substring(s + 1);
  int dot = n.lastIndexOf('.');
  String base = dot > 0 ? n.substring(0, dot) : n, ext = dot > 0 ? n.substring(dot + 1) : "";
  ext.toLowerCase();
  String o;
  for (size_t i = 0; i < base.length() && o.length() < 28; i++) {
    char c = base[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_') o += c;
    else if ((unsigned char)c >= 0xC0 || c == ' ' || c == '.') { if (!o.endsWith("_")) o += '_'; }  // one '_' per non-ASCII char
  }
  while (o.startsWith("_")) o.remove(0, 1);
  if (!o.length()) o = "img_" + String(millis() % 100000);
  return ext.length() ? o + "." + ext : o;
}
static String pathOf(const String &n) { return "/pixel/" + n; }

void setLed(uint8_t r, uint8_t g, uint8_t b) {
#if RGB_LED_ENABLE
  ledcWrite(LED_R, 255 - r); ledcWrite(LED_G, 255 - g); ledcWrite(LED_B, 255 - b);
#endif
}

// =============================================================== drawing: chrome
void drawCorner(int x, int y, int dx, int dy, uint16_t c) {
  lcd.fillRect(dx > 0 ? x : x - 13, y, 14, 2, c);
  lcd.fillRect(x, dy > 0 ? y : y - 13, 2, 14, c);
}

// ---------- text-free cyber HUD ----------
static const int CORE_X = 276, CORE_Y = 40;
bool infoOverlay = false; uint32_t infoUntil = 0;

uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
}

void drawFrame() {
  lcd.fillScreen(C_BG);
  for (int y = 0; y < 240; y += 3) lcd.drawFastHLine(SB_X, y, 320 - SB_X, C_SCAN);
  lcd.drawFastVLine(SB_X - 3, 8, 224, C_PANEL);
  for (int y = 12; y < 230; y += 24) lcd.drawFastHLine(SB_X - 5, y, 5, C_DIM);
  // art frame
  lcd.drawRect(ART_X - 4, ART_Y - 4, ART_W + 8, ART_H + 8, C_PANEL);
  drawCorner(ART_X - 6, ART_Y - 6, 1, 1, C_CY);
  drawCorner(ART_X + ART_W + 4, ART_Y - 6, -1, 1, C_CY);
  drawCorner(ART_X - 6, ART_Y + ART_H + 4, 1, -1, C_MG);
  drawCorner(ART_X + ART_W + 4, ART_Y + ART_H + 4, -1, -1, C_MG);
  // tick marks along the art frame
  for (int x = ART_X + 20; x < ART_X + ART_W - 16; x += 16) { lcd.drawFastVLine(x, ART_Y - 9, 3, C_PANEL); lcd.drawFastVLine(x, ART_Y + ART_H + 6, 3, C_PANEL); }
  lcd.fillRect(ART_X + ART_W - 40, ART_Y - 10, 30, 3, C_MG);
  lcd.fillRect(ART_X + 4, ART_Y + ART_H + 7, 18, 3, C_CY);
  // separators
  for (int x = SB_X + 2; x < 318; x += 4) { lcd.drawPixel(x, 84, C_DIM); lcd.drawPixel(x, 142, C_DIM); lcd.drawPixel(x, 190, C_DIM); }
  // circuit trace decoration
  lcd.drawFastHLine(SB_X + 2, 216, 30, C_PANEL); lcd.drawLine(SB_X + 32, 216, SB_X + 38, 210, C_PANEL); lcd.drawFastHLine(SB_X + 38, 210, 48, C_PANEL);
  lcd.fillCircle(SB_X + 2, 216, 1, C_DIM); lcd.fillCircle(SB_X + 86, 210, 1, C_DIM);
}

// ---------- theme decorations (right side) ----------
// top: 0 reactor 1 heart 2 moon 3 compass 4 sun 5 flower 6 gem 7 sunset 8 handheld 9 pumpkin 10 note 11 mic 12 snowman 13 tree 14 ice cream
// mid: 0 eq 1 bubbles 2 petals 3 heart meter 4 waves 5 twinkle 6 grass 7 snow 8 leaves 9 notes 10 fireworks 11 bats 12 sprinkles
static const uint8_t THEME_TOP[] = {0, 7, 0, 0, 0, 7, 0, 5, 4, 2, 1, 1, 6, 1, 3, 6, 4, 4, 2, 6,  8, 8, 8,  5, 2, 4, 9, 12, 13, 4,  11, 10, 11, 10, 11,  14, 14, 1, 5};
static const uint8_t THEME_MID[] = {0, 0, 0, 0, 0, 4, 0, 2, 1, 5, 1, 2, 5, 2, 6, 3, 6, 4, 5, 5,  0, 3, 5,  2, 10, 8, 11, 7, 7, 5,  9, 0, 0, 9, 5,  12, 12, 2, 1};
LGFX_Sprite sTop(&lcd), sMid(&lcd);
static const int DW = 88, TOP_H = 82, MID_Y = 86, MID_H = 54;
static inline uint32_t hsh(uint32_t a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }
static const char *HEART9[8] = {"011000110", "111101111", "111111111", "111111111", "011111110", "001111100", "000111000", "000010000"};
static const char *HEART7[6] = {"0110110", "1111111", "1111111", "0111110", "0011100", "0001000"};

void decoBg(LGFX_Sprite &sp, int y0) { sp.fillScreen(C_BG); for (int y = 0; y < sp.height(); y++) if ((y0 + y) % 3 == 0) sp.drawFastHLine(0, y, DW, C_SCAN); }
void spark(LGFX_Sprite &sp, int x, int y, int sz, uint16_t c) {
  if (sz <= 0) return;
  for (int i = 1; i <= sz; i++) { sp.drawPixel(x + i, y, c); sp.drawPixel(x - i, y, c); sp.drawPixel(x, y + i, c); sp.drawPixel(x, y - i, c); }
  sp.drawPixel(x, y, sz > 1 ? C_TX : c);
}
void pixHeart(LGFX_Sprite &sp, const char **bm, int w, int h, int cx, int cy, int sc, uint16_t c, bool outline = false) {
  int ox = cx - w * sc / 2, oy = cy - h * sc / 2;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (bm[y][x] == '1') {
    bool edge = !outline || x == 0 || y == 0 || x == w - 1 || y == h - 1 || bm[y][x - 1] != '1' || bm[y][x + 1] != '1' || bm[y - 1][x] != '1' || (y + 1 < h && bm[y + 1][x] != '1');
    if (edge) sp.fillRect(ox + x * sc, oy + y * sc, sc, sc, c);
  }
}

void drawTop(uint32_t t) {
  LGFX_Sprite &sp = sTop; decoBg(sp, 0);
  const int cx = 44, cy = 40; int kind = THEME_TOP[themeIdx];
  float p = (sin(t / 300.0f) + 1) / 2;
  switch (kind) {
    case 0: {  // reactor
      sp.drawCircle(cx, cy, 35, C_PANEL); sp.drawCircle(cx, cy, 27, C_PANEL);
      for (int a = 0; a < 360; a += 30) { float r = a * DEG_TO_RAD; sp.drawLine(cx + cos(r) * 36, cy + sin(r) * 36, cx + cos(r) * 39, cy + sin(r) * 39, C_DIM); }
      int a0 = (t / 40 * 5) % 360, b0 = (720 - a0 * 2) % 360;
      for (int k = 0; k < 3; k++) sp.fillArc(cx, cy, 30, 33, a0 + k * 120, a0 + k * 120 + 40, C_CY);
      for (int k = 0; k < 2; k++) sp.fillArc(cx, cy, 20, 23, b0 + k * 180, b0 + k * 180 + 70, C_MG);
      sp.fillCircle(cx, cy, 5 + (int)(4 * p), lerp565(C_PANEL, autoMode ? C_CY : C_YL, p)); sp.fillCircle(cx, cy, 2, C_TX);
    } break;
    case 1: {  // beating heart
      int beat = (t % 1000) < 120 || ((t % 1000) > 220 && (t % 1000) < 320);
      pixHeart(sp, HEART9, 9, 8, cx, cy, beat ? 5 : 4, C_CY);
      sp.fillRect(cx - 12, cy - 10, 4, 4, C_TX);
      for (int k = 0; k < 3; k++) { float a = t / 900.0f + k * 2.094f; spark(sp, cx + cos(a) * 34, cy + sin(a) * 30, 1 + ((t / 200 + k) % 3 == 0), k == 1 ? C_YL : C_MG); }
    } break;
    case 2: {  // crescent moon + stars
      int my = cy + (int)(sin(t / 900.0f) * 2);
      for (int y = -22; y <= 22; y++) for (int x = -22; x <= 22; x++)
        if (x * x + y * y <= 22 * 22 && (x - 10) * (x - 10) + (y + 7) * (y + 7) > 19 * 19) sp.drawPixel(cx - 4 + x, my + y, C_YL);
      for (int k = 0; k < 7; k++) { uint32_t h = hsh(k * 31 + 7); int ph = (t / 220 + h) % 6; spark(sp, 4 + h % 80, 4 + (h >> 8) % 72, ph == 2 ? 2 : (ph == 1 || ph == 3) ? 1 : 0, k & 1 ? C_CY : C_TX); }
    } break;
    case 3: {  // compass
      sp.fillArc(cx, cy, 28, 31, 0, 360, C_PANEL); sp.drawCircle(cx, cy, 33, C_DIM);
      for (int a = 0; a < 360; a += 45) { float r = (a - 90) * DEG_TO_RAD; int l = a % 90 ? 3 : 6; sp.drawLine(cx + cos(r) * 27, cy + sin(r) * 27, cx + cos(r) * (27 - l), cy + sin(r) * (27 - l), a == 0 ? C_MG : C_YL); }
      float ang = (sin(t / 1100.0f) * 35 + sin(t / 370.0f) * 6 - 90) * DEG_TO_RAD;
      int nx = cx + cos(ang) * 22, ny = cy + sin(ang) * 22, sx = cx - cos(ang) * 22, sy = cy - sin(ang) * 22;
      int px = -sin(ang) * 5, py = cos(ang) * 5;
      sp.fillTriangle(nx, ny, cx + px, cy + py, cx - px, cy - py, C_MG);
      sp.fillTriangle(sx, sy, cx + px, cy + py, cx - px, cy - py, C_TX);
      sp.fillCircle(cx, cy, 3, C_YL);
    } break;
    case 4: {  // sun
      float rot = t / 2500.0f;
      for (int k = 0; k < 12; k++) { float a = rot + k * PI / 6; int r1 = 17, r2 = 25 + (k & 1 ? 0 : (int)(4 * p));
        for (int w = -1; w <= 1; w++) sp.drawLine(cx + cos(a) * r1 + w, cy + sin(a) * r1, cx + cos(a) * r2 + w, cy + sin(a) * r2, k & 1 ? C_MG : C_YL); }
      sp.fillCircle(cx, cy, 13, C_YL); sp.fillCircle(cx - 4, cy - 4, 3, C_TX);
    } break;
    case 5: {  // flower
      float rot = t / 1800.0f;
      for (int k = 0; k < 6; k++) { float a = rot + k * PI / 3; sp.fillCircle(cx + cos(a) * 15, cy + sin(a) * 15, 10, k & 1 ? C_CY : C_MG); }
      sp.fillCircle(cx, cy, 8, C_YL); sp.fillCircle(cx - 2, cy - 2, 2, C_TX);
      for (int k = 0; k < 3; k++) { uint32_t h = hsh(k + 99); int ph = (t / 200 + h) % 6; spark(sp, 6 + h % 76, 4 + (h >> 9) % 74, ph == 2 ? 2 : ph == 1 ? 1 : 0, C_YL); }
    } break;
    case 6: {  // spinning gem
      float w = cos(t / 600.0f); int hw = max(2, (int)(fabs(w) * 22));
      int ty = cy - 26, my = cy - 8, by = cy + 28;
      sp.fillTriangle(cx, ty, cx - hw, my, cx + hw, my, C_CY);
      sp.fillTriangle(cx - hw, my, cx + hw, my, cx, by, C_MG);
      int hx = cx + (int)(w * hw * 0.4f);
      sp.drawLine(cx - hw, my, cx + hw, my, C_TX); sp.drawLine(hx, ty + 3, hx, by - 4, lerp565(C_TX, C_MG, 0.5f));
      sp.drawLine(cx, ty, cx - hw, my, C_TX);
      for (int k = 0; k < 4; k++) { uint32_t h = hsh(k + 7); int ph = (t / 180 + h) % 6; spark(sp, 6 + h % 76, 4 + (h >> 9) % 74, ph == 2 ? 2 : ph == 1 ? 1 : 0, C_YL); }
    } break;
    case 8: {  // handheld console icon (generic)
      sp.fillRoundRect(cx - 34, cy - 26, 68, 52, 10, C_PANEL); sp.drawRoundRect(cx - 34, cy - 26, 68, 52, 10, C_DIM);
      int pressed = (t / 300) % 6;
      int dx = cx - 18, dy = cy + 2;
      sp.fillRect(dx - 3, dy - 10, 7, 21, pressed == 0 ? C_CY : C_BG); sp.fillRect(dx - 10, dy - 3, 21, 7, pressed == 1 ? C_CY : C_BG);
      sp.fillCircle(cx + 12, dy + 5, 6, pressed == 2 ? C_CY : C_MG); sp.fillCircle(cx + 24, dy - 4, 6, pressed == 3 ? C_CY : C_MG);
      sp.fillRoundRect(cx - 10, cy - 20, 20, 4, 2, pressed == 4 ? C_CY : C_DIM);
      sp.fillCircle(cx + 28, cy - 18, 2, (t / 500) & 1 ? C_YL : C_DIM);
    } break;
    case 9: {  // jack-o-lantern
      int bob = (int)(sin(t / 700.0f) * 2);
      for (int k = -1; k <= 1; k++) sp.fillEllipse(cx + k * 10, cy + 4 + bob, 14, 22 - abs(k) * 3, C_CY);
      for (int k = -1; k <= 1; k += 2) sp.drawFastVLine(cx + k * 6, cy - 12 + bob, 30, C_PANEL);
      sp.fillRect(cx - 3, cy - 24 + bob, 6, 8, C_GRN);
      uint16_t glow = lerp565(C_YL, C_MG, (sin(t / 120.0f) + 1) / 4);
      sp.fillTriangle(cx - 14, cy + bob, cx - 6, cy + bob, cx - 10, cy - 7 + bob, glow);
      sp.fillTriangle(cx + 6, cy + bob, cx + 14, cy + bob, cx + 10, cy - 7 + bob, glow);
      for (int i = -12; i <= 12; i += 4) sp.fillRect(cx + i - 1, cy + 9 + bob + ((i / 4) & 1 ? 2 : 0), 3, 4, glow);
    } break;
    case 10: {  // bouncing music note
      float b = fabs(sin(t / 350.0f)); int ny = cy + 12 - (int)(b * 10), nx = cx - 6;
      sp.fillEllipse(nx, ny, 9, 7, C_CY); sp.fillRect(nx + 6, ny - 34, 4, 34, C_CY);
      sp.fillTriangle(nx + 9, ny - 34, nx + 24, ny - 22, nx + 9, ny - 24, C_MG);
      sp.fillEllipse(nx - 2, ny - 2, 3, 2, C_TX);
      for (int k = 0; k < 4; k++) { uint32_t h = hsh(k + 41); int ph = (t / 180 + h) % 6; spark(sp, 6 + h % 76, 4 + (h >> 9) % 74, ph == 2 ? 2 : ph == 1 ? 1 : 0, k & 1 ? C_YL : C_MG); }
    } break;
    case 11: {  // microphone with sound waves
      sp.fillRoundRect(cx - 10, cy - 30, 20, 28, 10, C_DIM);
      for (int y = -26; y < -6; y += 4) sp.drawFastHLine(cx - 8, cy + y, 16, C_PANEL);
      sp.drawRoundRect(cx - 10, cy - 30, 20, 28, 10, C_CY);
      sp.fillRect(cx - 3, cy - 2, 6, 18, C_CY); sp.fillRect(cx - 12, cy + 16, 24, 4, C_CY);
      sp.drawArc(cx, cy - 12, 16, 15, 20, 160, C_PANEL);
      for (int k = 0; k < 3; k++) { int r = 20 + k * 7 + (int)((t / 60) % 7); uint16_t c = lerp565(C_MG, C_BG, (r - 20) / 26.0f);
        sp.drawArc(cx, cy - 16, r, r - 1, 300, 360, c); sp.drawArc(cx, cy - 16, r, r - 1, 0, 60, c);
        sp.drawArc(cx, cy - 16, r, r - 1, 120, 240, c); }
    } break;
    case 12: {  // snowman
      sp.fillCircle(cx, cy + 14, 17, 0xFFFF); sp.drawCircle(cx, cy + 14, 17, C_DIM);
      sp.fillCircle(cx, cy - 10, 12, 0xFFFF); sp.drawCircle(cx, cy - 10, 12, C_DIM);
      sp.fillRect(cx - 11, cy - 24, 22, 3, C_PANEL); sp.fillRect(cx - 7, cy - 36, 14, 12, C_PANEL);
      sp.fillRect(cx - 5, cy - 13, 3, 3, C_TX); sp.fillRect(cx + 3, cy - 13, 3, 3, C_TX);
      sp.fillTriangle(cx, cy - 9, cx, cy - 6, cx + 9, cy - 7, 0xFD20);
      sp.fillRect(cx - 12, cy - 1, 24, 4, C_CY);
      for (int i = 0; i < 3; i++) sp.fillCircle(cx, cy + 6 + i * 8, 2, C_PANEL);
      for (int k = 0; k < 6; k++) { uint32_t h = hsh(k + 55); int y = (int)((t / (40 + h % 30) + h) % 84) - 2, x = (h % 88 + (int)(sin(t / 600.0f + k) * 4)) % 88; sp.drawPixel(x, y, C_YL); sp.drawPixel(x + 1, y, C_YL); }
    } break;
    case 13: {  // holiday tree
      for (int k = 0; k < 3; k++) sp.fillTriangle(cx, cy - 30 + k * 14, cx - 14 - k * 6, cy - 6 + k * 14, cx + 14 + k * 6, cy - 6 + k * 14, C_GRN);
      sp.fillRect(cx - 4, cy + 36, 8, 6, C_DIM);
      static const int8_t orn[8][2] = {{-6, -12}, {5, -6}, {-10, 4}, {9, 10}, {-3, 14}, {-16, 22}, {14, 24}, {3, 28}};
      for (int i = 0; i < 8; i++) { uint16_t c = ((t / 400 + i) % 3 == 0) ? C_YL : (i & 1 ? C_CY : C_MG); sp.fillCircle(cx + orn[i][0], cy + orn[i][1], 2, c); }
      spark(sp, cx, cy - 32, 2 + ((t / 300) & 1), C_YL);
    } break;
    case 14: {  // ice cream cone
      int wob = (int)(sin(t / 500.0f) * 2);
      sp.fillTriangle(cx - 13, cy + 2, cx + 13, cy + 2, cx, cy + 38, 0xEDCB);
      for (int k = -2; k <= 2; k++) { sp.drawLine(cx + k * 6 - 6, cy + 2, cx + k * 3 + 3, cy + 24, 0xC4E6); sp.drawLine(cx + k * 6 + 6, cy + 2, cx + k * 3 - 3, cy + 24, 0xC4E6); }
      sp.fillCircle(cx + wob, cy - 4, 15, C_MG);
      sp.fillCircle(cx + wob, cy - 20, 13, C_CY);
      sp.fillCircle(cx - 6 + wob, cy + 8, 4, C_MG); sp.fillCircle(cx + 7 + wob, cy + 9, 3, C_MG);
      sp.fillCircle(cx + wob + 3, cy - 36, 4, 0xF8A6); sp.drawLine(cx + wob + 3, cy - 40, cx + wob + 7, cy - 45, C_GRN);
      static const int8_t sp8[8][3] = {{-8, -24, 0}, {4, -26, 1}, {-2, -16, 2}, {8, -18, 0}, {-9, -6, 1}, {6, -2, 2}, {0, -10, 0}, {-4, 2, 1}};
      for (int i = 0; i < 8; i++) { uint16_t c = sp8[i][2] == 0 ? C_YL : sp8[i][2] == 1 ? 0xFFFF : C_GRN; sp.fillRect(cx + sp8[i][0] + wob, cy + sp8[i][1], 3, 1, c); }
      sp.fillCircle(cx - 6 + wob, cy - 24, 2, 0xFFFF);
      for (int k = 0; k < 3; k++) { uint32_t h = hsh(k + 77); int ph = (t / 200 + h) % 6; spark(sp, 6 + h % 76, 4 + (h >> 9) % 74, ph == 2 ? 2 : ph == 1 ? 1 : 0, C_YL); }
    } break;
    case 7: {  // retro sunset
      int scy = cy + 10;
      for (int y = -28; y <= 0; y++) {
        int half = (int)sqrt(28 * 28 - y * y);
        int band = (-y + (int)(t / 90)) % 8;
        if (y > -16 && band < (16 + y) / 5) continue;
        sp.drawFastHLine(cx - half, scy + y, half * 2, lerp565(C_YL, C_MG, (y + 28) / 28.0f));
      }
      sp.drawFastHLine(4, scy + 2, 80, C_CY);
      for (int i = 0; i < 4; i++) { int yy = scy + 6 + i * i * 2 + (int)((t / 120) % (2 + i)); if (yy < TOP_H) sp.drawFastHLine(4, yy, 80, C_PANEL); }
      for (int k = -4; k <= 4; k++) sp.drawLine(cx + k * 6, scy + 3, cx + k * 22, TOP_H - 1, C_PANEL);
    } break;
  }
  sp.pushSprite(SB_X, 0);
}

void drawMid(uint32_t t) {
  LGFX_Sprite &sp = sMid; decoBg(sp, MID_Y);
  int kind = THEME_MID[themeIdx];
  switch (kind) {
    case 0: {  // equalizer
      static float h[8] = {0};
      for (int i = 0; i < 8; i++) {
        float target = (hsh(i * 977 + t / 70) % 10 + 1) * (0.5f + 0.5f * sin(t / 700.0f + i));
        h[i] += (max(1.0f, target) - h[i]) * 0.5f; int n = (int)h[i];
        for (int s2 = 0; s2 < 10; s2++) sp.fillRect(5 + i * 10, 50 - s2 * 5 - 3, 7, 3, s2 < n ? lerp565(C_CY, C_MG, s2 / 9.0f) : C_SCAN);
      }
    } break;
    case 1: {  // bubbles
      for (int i = 0; i < 9; i++) { uint32_t h = hsh(i + 5); int sp2 = 25 + h % 20;
        int y = MID_H + 6 - (int)((t / sp2 + h) % (MID_H + 14)), x = 6 + h % 76 + (int)(sin(t / 600.0f + i) * 3), r = 2 + (h >> 4) % 4;
        sp.drawCircle(x, y, r, i & 1 ? C_CY : C_MG); sp.drawPixel(x - r / 2, y - r / 2, C_TX); }
    } break;
    case 2: {  // falling petals
      for (int i = 0; i < 11; i++) { uint32_t h = hsh(i + 33); int spd = 30 + h % 25;
        int y = (int)((t / spd + h) % (MID_H + 10)) - 5, x = (int)((h % 88) + y / 3 + sin(t / 500.0f + i) * 5) % 92 - 2;
        uint16_t c = i % 3 == 0 ? C_MG : C_CY; sp.fillEllipse(x, y, 3, 2, c); sp.drawPixel(x - 1, y - 1, C_TX); }
    } break;
    case 3: {  // heart meter + stamina bar
      int full = (t / 600) % 6;
      for (int i = 0; i < 5; i++) pixHeart(sp, HEART7, 7, 6, 9 + i * 17, 16, 2, i < full ? C_MG : C_DIM, i >= full);
      int w = 78, fillw = (int)((sin(t / 900.0f) + 1) / 2 * (w - 4));
      sp.drawRect(5, 34, w, 8, C_DIM); sp.fillRect(7, 36, fillw, 4, C_CY);
    } break;
    case 4: {  // waves
      for (int k = 0; k < 3; k++) { uint16_t c = k == 0 ? C_CY : k == 1 ? C_MG : C_YL;
        for (int x = 0; x < DW; x++) { int y = 14 + k * 13 + (int)(sin(x / 8.0f + t / (380.0f + k * 120)) * 4); sp.fillRect(x, y, 1, 2, c); } }
    } break;
    case 5: {  // twinkling stars + shooting star
      for (int i = 0; i < 14; i++) { uint32_t h = hsh(i + 71); int ph = (t / 200 + h) % 8;
        spark(sp, 3 + h % 82, 3 + (h >> 8) % 48, ph == 3 ? 2 : (ph == 2 || ph == 4) ? 1 : (ph == 0 ? 0 : 0), i % 3 ? C_TX : C_YL);
        if (ph > 4) sp.drawPixel(3 + h % 82, 3 + (h >> 8) % 48, C_DIM); }
      int ss = t % 4000; if (ss < 500) { int x = 90 - ss / 5, y = 4 + ss / 14; for (int k = 0; k < 10; k++) sp.drawPixel(x + k, y - k / 3, lerp565(C_YL, C_BG, k / 10.0f)); }
    } break;
    case 7: {  // snowfall
      for (int i = 0; i < 16; i++) { uint32_t h = hsh(i + 91); int spd = 45 + h % 40;
        int y = (int)((t / spd + h) % (MID_H + 6)) - 3, x = (int)(h % 88 + sin(t / 700.0f + i) * 4) % 88;
        if (h % 3 == 0) spark(sp, x, y, 1, C_YL); else { sp.drawPixel(x, y, C_YL); sp.drawPixel(x + 1, y, C_YL); } }
    } break;
    case 8: {  // falling leaves
      for (int i = 0; i < 10; i++) { uint32_t h = hsh(i + 17); int spd = 35 + h % 30;
        int y = (int)((t / spd + h) % (MID_H + 10)) - 5, x = (int)((h % 88) + sin(t / 450.0f + i) * 7) % 92 - 2;
        uint16_t c = i % 3 == 0 ? C_MG : i % 3 == 1 ? C_CY : C_YL; bool flip = ((t / 300 + i) & 1);
        sp.fillTriangle(x - 3, y, x + 3, y, x, flip ? y - 4 : y + 4, c); sp.fillTriangle(x - 2, y, x + 2, y, x, flip ? y + 3 : y - 3, c); }
    } break;
    case 9: {  // rising music notes
      for (int i = 0; i < 7; i++) { uint32_t h = hsh(i + 63); int spd = 30 + h % 25;
        int y = MID_H + 8 - (int)((t / spd + h) % (MID_H + 20)), x = 6 + h % 72 + (int)(sin(t / 500.0f + i) * 5);
        uint16_t c = i % 3 == 0 ? C_MG : i % 3 == 1 ? C_CY : C_YL;
        sp.fillEllipse(x, y, 3, 2, c); sp.drawFastVLine(x + 2, y - 9, 9, c); if (i & 1) sp.drawLine(x + 2, y - 9, x + 6, y - 6, c); else sp.drawFastHLine(x + 2, y - 9, 6, c); }
    } break;
    case 10: {  // fireworks
      for (int k = 0; k < 3; k++) { uint32_t cyc = t / 1600 + k * 7, h = hsh(cyc * 13 + k); int ph = (t + k * 530) % 1600;
        int fx = 12 + h % 64, fy = 10 + (h >> 8) % 26; uint16_t c = k == 0 ? C_CY : k == 1 ? C_MG : C_YL;
        if (ph < 300) { sp.drawPixel(fx, MID_H - 1 - (MID_H - fy) * ph / 300, C_YL); continue; }
        float r = (ph - 300) / 60.0f; if (r > 16) continue; uint16_t cc = lerp565(c, C_BG, r / 16.0f);
        for (int a = 0; a < 12; a++) { float an = a * PI / 6; sp.drawPixel(fx + cos(an) * r, fy + sin(an) * r, cc); sp.drawPixel(fx + cos(an) * r * 0.6f, fy + sin(an) * r * 0.6f, cc); } }
    } break;
    case 11: {  // bats
      sp.fillCircle(70, 12, 8, C_YL); sp.fillCircle(74, 10, 7, C_BG);
      for (int i = 0; i < 5; i++) { uint32_t h = hsh(i + 29); int spd = 18 + h % 14;
        int x = (int)((t / spd + h) % 110) - 12, y = 10 + (h >> 7) % 34 + (int)(sin(t / 260.0f + i) * 3); bool up = ((t / 140 + i) & 1);
        sp.fillRect(x - 1, y - 1, 3, 3, C_MG);
        sp.drawLine(x - 1, y, x - 6, up ? y - 4 : y + 2, C_MG); sp.drawLine(x + 1, y, x + 6, up ? y - 4 : y + 2, C_MG); }
    } break;
    case 12: {  // falling sprinkles
      static const uint16_t SC[5] = {0xFFFF, 0xFE60, 0x87F0, 0xF81F, 0x5D7F};
      for (int i = 0; i < 18; i++) { uint32_t h = hsh(i + 151); int spd = 30 + h % 30;
        int y = (int)((t / spd + h) % (MID_H + 6)) - 3, x = (int)(h % 86 + sin(t / 600.0f + i) * 3) % 88;
        uint16_t c = (i % 3 == 0) ? C_MG : (i % 3 == 1) ? C_CY : SC[h % 5]; int ang = (t / 150 + i) % 4;
        if (ang == 0) sp.fillRect(x, y, 4, 1, c); else if (ang == 1) sp.fillRect(x + 1, y - 1, 1, 4, c); else if (ang == 2) sp.drawLine(x, y, x + 3, y - 3, c); else sp.drawLine(x, y - 3, x + 3, y, c); }
    } break;
    case 6: {  // swaying grass
      for (int i = 0; i < 18; i++) { uint32_t h = hsh(i + 13); int x = 2 + i * 5, len = 14 + h % 22; float sw = sin(t / 500.0f + i * 0.6f) * 3;
        uint16_t c = i & 1 ? C_CY : C_GRN; sp.drawLine(x, MID_H - 1, x + (int)sw, MID_H - 1 - len, c); sp.drawLine(x + 1, MID_H - 1, x + 1 + (int)sw, MID_H - 1 - len, c);
        if (h % 5 == 0) sp.fillCircle(x + (int)sw, MID_H - 1 - len, 2, C_MG); }
    } break;
  }
  sp.pushSprite(SB_X, MID_Y);
}

void tickDeco() {
  static uint32_t last = 0; if (millis() - last < 50) return; last = millis();
  drawTop(millis()); drawMid(millis());
}

// wifi icon
void drawWifi(int cx, int cy, uint16_t col, int bars) {
  for (int i = 0; i < 3; i++) lcd.fillArc(cx, cy, 4 + i * 5, 6 + i * 5, 225, 315, i < bars ? col : C_PANEL);
  lcd.fillCircle(cx, cy, 2, bars > 0 ? col : C_PANEL);
}

void drawHud() {
  // slot grid (current = magenta)
  const int gx = SB_X + 4, gy = 150, cs = 6, cg = 3, perRow = 9, rows = 3;
  int n = items.size(), page = cur / (perRow * rows), base = page * perRow * rows;
  for (int i = 0; i < perRow * rows; i++) {
    int idx = base + i, x = gx + (i % perRow) * (cs + cg), y = gy + (i / perRow) * (cs + cg);
    uint16_t c = idx >= n ? C_SCAN : idx == cur ? C_MG : (items[idx].type == T_ROM ? C_YL : C_PANEL);
    lcd.fillRect(x, y, cs, cs, c);
    if (idx == cur) lcd.drawRect(x - 1, y - 1, cs + 2, cs + 2, C_TX); else lcd.drawRect(x - 1, y - 1, cs + 2, cs + 2, C_BG);
  }
  // mode icon: play / pause
  lcd.fillRect(SB_X + 4, 194, 20, 14, C_BG);
  if (autoMode) lcd.fillTriangle(SB_X + 8, 195, SB_X + 8, 207, SB_X + 18, 201, C_GRN);
  else { lcd.fillRect(SB_X + 7, 195, 4, 12, C_YL); lcd.fillRect(SB_X + 14, 195, 4, 12, C_YL); }
  // error flag
  lcd.fillRect(SB_X + 30, 196, 10, 10, errText.length() ? C_MG : C_BG);
  if (errText.length()) { lcd.drawLine(SB_X + 31, 197, SB_X + 38, 204, C_BG); lcd.drawLine(SB_X + 38, 197, SB_X + 31, 204, C_BG); }
}

void drawNet() {
  static uint32_t t = 0; static bool on = true;
  if (millis() - t < 500) return; t = millis(); on = !on;
  int bars = 0; uint16_t col = C_DIM;
  if (netMode == NET_STA) {
    long r = WiFi.RSSI(); bars = r > -60 ? 3 : r > -72 ? 2 : r > -85 ? 1 : 0;
    col = WiFi.status() == WL_CONNECTED ? C_GRN : C_MG;
  } else if (netMode == NET_AP) { bars = on ? 3 : 0; col = C_YL; }
  lcd.fillRect(SB_X + 54, 189, 32, 20, C_BG);
  drawWifi(SB_X + 70, 207, col, bars);
}

void drawProgress() {
  static int last = -1;
  const int segs = 14, y = 224;
  int p = autoMode ? min(segs, (int)((millis() - itemStart) * segs / (SLIDE_SECONDS * 1000UL))) : segs;
  if (p == last) return; last = p;
  for (int i = 0; i < segs; i++) lcd.fillRect(SB_X + 4 + i * 6, y, 4, 4, i < p ? (autoMode ? C_CY : C_YL) : C_PANEL);
}

// scanner light running along the bottom rail + live dot
void drawBlink() {
  static uint32_t t = 0; static int x = 0;
  if (millis() - t < 30) return; t = millis();
  const int x0 = SB_X + 12, w = 318 - x0, y = 234;
  lcd.drawFastHLine(x0, y, w, C_PANEL);
  x = (x + 3) % (w + 16);
  for (int i = 0; i < 14; i++) { int px = x0 + x - i; if (px >= x0 && px < x0 + w) lcd.drawPixel(px, y, lerp565(C_CY, C_PANEL, i / 14.0f)); }
  bool on = (millis() / 500) & 1;
  lcd.fillCircle(SB_X + 5, 234, 2, on ? C_MG : C_PANEL);
}

void drawStream() { if (!safeMode) tickDeco(); drawNet(); }

// =============================================================== drawing: art
void clearArt() { lcd.fillRect(ART_X, ART_Y, ART_W, ART_H, C_BG); }


// background pattern for unlit dots (dot coordinates inside the art canvas)
static const uint8_t TILE_HEART[8] = {0x6C, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00, 0x00};
static const uint8_t TILE_GRASS[8] = {0x00, 0x00, 0x20, 0xA8, 0xA8, 0x00, 0x02, 0x05};
static const uint8_t TILE_FLOWER[8] = {0x00, 0x20, 0x70, 0x20, 0x00, 0x02, 0x00, 0x00};
static const uint8_t TILE_SNOW[8] = {0x00, 0x10, 0x54, 0x38, 0x54, 0x10, 0x00, 0x00};
static const uint8_t TILE_NOTE[8] = {0x00, 0x0C, 0x0E, 0x0A, 0x08, 0x38, 0x38, 0x00};
static const uint8_t TILE_SPRINKLE[8] = {0x00, 0x60, 0x00, 0x04, 0x04, 0x00, 0x30, 0x00};
static const uint8_t TILE_LEAF[8] = {0x00, 0x10, 0x54, 0x7C, 0x38, 0x10, 0x10, 0x00};
uint16_t bgAt(int x, int y, int h) {
  uint8_t p = THEMES[themeIdx].pat;
  auto tile = [&](const uint8_t *t, int ox) { int tx = (x + ((y / 8) & 1) * ox) & 7, ty = y & 7; return (t[ty] >> (7 - tx)) & 1; };
  switch (p) {
    case P_STARS: { uint32_t k = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u); return (k % 29 == 0) ? C_PAT : C_OFF; }
    case P_CHECK: return (((x >> 2) + (y >> 2)) & 1) ? C_PAT : C_OFF;
    case P_HEART: return tile(TILE_HEART, 4) ? C_PAT : C_OFF;
    case P_POLKA: { int tx = x & 7, ty = y & 7; return ((tx == 1 || tx == 2) && (ty == 1 || ty == 2)) || ((tx == 5 || tx == 6) && (ty == 5 || ty == 6)) ? C_PAT : C_OFF; }
    case P_GRASS: return tile(TILE_GRASS, 3) ? C_PAT : C_OFF;
    case P_WAVE: { static const int8_t tri[4] = {0, 1, 2, 1}; return ((y + tri[(x >> 1) & 3]) % 6 == 0) ? C_PAT : C_OFF; }
    case P_SKY: return lerp565(C_OFF, C_PAT, h > 1 ? (float)y / (h - 1) : 0);
    case P_BRICK: return (y % 4 == 0 || ((x + ((y / 4) & 1) * 4) % 8 == 0)) ? C_PAT : C_OFF;
    case P_GRID: return (x % 8 == 0 || y % 8 == 0) ? C_PAT : C_OFF;
    case P_FLOWER: return tile(TILE_FLOWER, 4) ? C_PAT : C_OFF;
    case P_SNOW: return tile(TILE_SNOW, 4) ? C_PAT : C_OFF;
    case P_NOTE: return tile(TILE_NOTE, 4) ? C_PAT : C_OFF;
    case P_LEAF: return tile(TILE_LEAF, 3) ? C_PAT : C_OFF;
    case P_LCD: return (x % 3 == 2 || y % 3 == 2) ? C_PAT : C_OFF;
    case P_STRIPE: return (((x + y) >> 2) & 1) ? C_PAT : C_OFF;
    case P_SPRINKLE: return tile(TILE_SPRINKLE, 4) ? C_PAT : C_OFF;
    default: return C_OFF;
  }
}

bool motionActive() { return motion && imgValid && (items[cur].type == T_PNG || items[cur].type == T_BMP || items[cur].type == T_PXL); }

static const uint8_t BOB[] = {1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1};   // 1 = rest, 0 = 1px up
static inline uint16_t imgAt(int x, int y) { return (x < 0 || y < 0 || x >= imgW || y >= imgH) ? TKEY : img[y * imgW + x]; }

void pickSparks() {
  nSparks = 0;
  int cw = imgW, ch = imgH + 2, r = max(1, min(imgW, imgH) / 40);
  for (int tries = 0; tries < 400 && nSparks < 10; tries++) {
    int x = random(cw), y = random(ch); bool ok = true;
    for (int dy = -r - 1; dy <= r + 1 && ok; dy++) for (int dx = -r - 1; dx <= r + 1 && ok; dx++) {
      int X = x + dx, Y = y + dy;
      if (X < 0 || Y < 0 || X >= cw || Y >= ch) { ok = false; break; }
      if (imgAt(X, Y - 1) != TKEY || imgAt(X, Y) != TKEY) ok = false;
    }
    for (int k = 0; k < nSparks && ok; k++) if (abs(sparks[k].x - x) + abs(sparks[k].y - y) < cw / 5) ok = false;
    if (ok) sparks[nSparks++] = {(uint8_t)x, (uint8_t)y, (uint8_t)random(3), (uint8_t)random(6)};
  }
}

void compose() {
  int cw = imgW, ch = imgH + 2, off = BOB[motionFrame % sizeof(BOB)];
  dispW = cw; dispH = ch;
  for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) disp[y * cw + x] = imgAt(x, y - off);
  const uint16_t *SPC = C_SP3;
  int r = max(1, min(imgW, imgH) / 40);
  for (int k = 0; k < nSparks; k++) {
    int ph = (motionFrame + sparks[k].ph) % 6, size = (ph == 1 || ph == 3) ? r : (ph == 2) ? r * 2 : 0;
    if (!size) continue;
    int x = sparks[k].x, y = sparks[k].y; uint16_t c = SPC[sparks[k].c];
    auto put = [&](int X, int Y, uint16_t col) { if (X >= 0 && Y >= 0 && X < cw && Y < ch && disp[Y * cw + X] == TKEY) disp[Y * cw + X] = col; };
    put(x, y, size > 1 ? 0xFFFF : c);
    for (int i = 1; i <= size; i++) { put(x + i, y, c); put(x - i, y, c); put(x, y + i, c); put(x, y - i, c); }
  }
}

void renderArt(bool force) {
  if (!imgValid) return;
  const uint16_t *src = img; int w = imgW, h = imgH;
  if (motionActive()) { compose(); src = disp; w = dispW; h = dispH; }
  static int lastW = 0, lastH = 0;
  if (w != lastW || h != lastH) { force = true; lastW = w; lastH = h; }
  int s = min(ART_W / w, ART_H / h); if (s < 1) s = 1;
  int ox = ART_X + (ART_W - w * s) / 2, oy = ART_Y + (ART_H - h * s) / 2;
  int cell = (LED_GRID && s >= 4) ? s - 1 : s;
  if (force) clearArt();
  lcd.startWrite();
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int i = y * w + x; uint16_t c = src[i];
    if (!force && shown[i] == c) continue;
    shown[i] = c;
    lcd.fillRect(ox + x * s, oy + y * s, cell, cell, c == TKEY ? bgAt(x, y, h) : c);
  }
  lcd.endWrite();
}


// on-demand info panel (BOOT button / setup mode) - the only text on the main screen
void drawInfo() {
  int x = ART_X + 10, y = ART_Y + 58, w = ART_W - 20, h = 92;
  lcd.fillRect(x, y, w, h, C_BG); lcd.drawRect(x, y, w, h, C_CY); lcd.drawRect(x + 2, y + 2, w - 4, h - 4, C_PANEL);
  lcd.fillRect(x, y, w, 13, netMode == NET_AP ? C_YL : C_CY);
  lcd.setFont(&fonts::Font0); lcd.setTextSize(1); lcd.setTextColor(C_BG);
  lcd.drawString(netMode == NET_AP ? "WIFI SETUP" : "NETWORK", x + 5, y + 3);
  auto line = [&](int i, const String &a, uint16_t c) { lcd.setTextColor(c, C_BG); lcd.drawString(a, x + 8, y + 20 + i * 13); };
  if (netMode == NET_AP) {
    line(0, "1. JOIN WIFI:", C_DIM); line(1, apName, C_TX);
    line(2, "2. OPEN:", C_DIM); line(3, "http://192.168.4.1", C_CY);
    line(4, "   (2.4GHz only)", C_DIM);
  } else if (netMode == NET_STA) {
    line(0, "WEB PANEL:", C_DIM); line(1, "http://" HOSTNAME ".local", C_CY);
    line(2, "http://" + ipText, C_CY); line(3, "SSID: " + ssidSaved, C_TX);
    line(4, "FILES: " + String(items.size() - 1) + (sdOK ? "  SD OK" : "  NO SD"), C_DIM);
  } else { line(0, "OFFLINE", C_MG); }
}
void showInfo(uint32_t ms) { infoOverlay = true; infoUntil = millis() + ms; drawInfo(); }
void hideInfo() { infoOverlay = false; if (imgValid) renderArt(true); else clearArt(); }

void glitch() {
  if (!GLITCH_FX) return;
  for (int k = 0; k < 14; k++) {
    int y = ART_Y + random(ART_H - 10), h = 2 + random(9), x = ART_X + random(40), w = ART_W - random(80);
    uint16_t c = (k % 3 == 0) ? C_CY : (k % 3 == 1) ? C_MG : C_BG;
    lcd.fillRect(x, y, min(w, ART_X + ART_W - x), h, c);
    delay(12);
  }
  clearArt();
}

void showError(const String &e) {
  errText = e; imgValid = false; clearArt();
  uint16_t base = (e == "RECEIVING" || e == "REBOOT") ? C_CY : C_MG;
  for (int y = ART_Y; y < ART_Y + ART_H; y += 4) for (int x = ART_X; x < ART_X + ART_W; x += 4)
    if (random(4) == 0) lcd.fillRect(x, y, 3, 3, random(3) ? C_PANEL : base);
  int cx = ART_X + ART_W / 2, cy = ART_Y + ART_H / 2;
  if (base == C_MG) for (int k = -3; k <= 3; k++) { lcd.drawLine(cx - 30 + k, cy - 30, cx + 30 + k, cy + 30, C_MG); lcd.drawLine(cx + 30 + k, cy - 30, cx - 30 + k, cy + 30, C_MG); }
  else { lcd.fillTriangle(cx - 22, cy - 4, cx + 22, cy - 4, cx, cy + 22, C_CY); lcd.fillRect(cx - 8, cy - 30, 16, 26, C_CY); }
}

// =============================================================== GIF callbacks
void *gOpen(const char *fn, int32_t *sz) { File *f = new File(SD.open(fn)); if (!*f) { delete f; return nullptr; } *sz = f->size(); return f; }
void gClose(void *h) { File *f = (File *)h; if (f) { f->close(); delete f; } }
int32_t gRead(GIFFILE *p, uint8_t *buf, int32_t len) {
  File *f = (File *)p->fHandle; int32_t n = min(len, p->iSize - p->iPos); if (n <= 0) return 0;
  n = f->read(buf, n); p->iPos = f->position(); return n;
}
int32_t gSeek(GIFFILE *p, int32_t pos) { File *f = (File *)p->fHandle; f->seek(pos); p->iPos = f->position(); return p->iPos; }
void gDraw(GIFDRAW *d) {
  int y = d->iY + d->y; if (y < 0 || y >= imgH) return;
  uint8_t *s = d->pPixels; uint16_t *pal = d->pPalette;
  for (int i = 0; i < d->iWidth; i++) {
    int x = d->iX + i; if (x >= imgW) break;
    uint8_t c = s[i];
    if (d->ucHasTransparency && c == d->ucTransparent) { if (d->ucDisposalMethod == 2) img[y * imgW + x] = TKEY; continue; }
    img[y * imgW + x] = pal[c];
  }
}

void closeGif() { if (gifOpen) { gif.close(); gifOpen = false; } }

// =============================================================== loading
bool readPngSize(const String &p, int &w, int &h) {
  File f = SD.open(p); if (!f) return false; uint8_t b[24]; bool ok = f.read(b, 24) == 24; f.close();
  if (!ok || b[1] != 'P' || b[2] != 'N' || b[3] != 'G') return false;
  w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]; h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23]; return true;
}
bool readBmpSize(const String &p, int &w, int &h) {
  File f = SD.open(p); if (!f) return false; uint8_t b[26]; bool ok = f.read(b, 26) == 26; f.close();
  if (!ok || b[0] != 'B' || b[1] != 'M') return false;
  w = (int32_t)(b[18] | (b[19] << 8) | (b[20] << 16) | (b[21] << 24)); h = abs((int32_t)(b[22] | (b[23] << 8) | (b[24] << 16) | (b[25] << 24))); return true;
}

void setSize(int w, int h) { imgW = w; imgH = h; for (int i = 0; i < w * h; i++) img[i] = TKEY; for (int i = 0; i < w * (h + 2); i++) shown[i] = 0xFFFF ^ TKEY; }

void loadRomFrame(int f) {
  for (int i = 0; i < ROM_W * ROM_H; i++) { uint8_t k = pgm_read_byte(&ROM_ART[f][i]); img[i] = k ? ROM_PAL[k] : TKEY; }
}


// raw format written by the web uploader: "PXL1" u16 w, u16 h, w*h RGB565 (0x0020 = transparent)
bool loadPxl(const Item &it) {
  File f = SD.open(pathOf(it.name)); if (!f) { showError("BAD FILE"); return false; }
  uint8_t h8[8]; if (f.read(h8, 8) != 8 || memcmp(h8, "PXL1", 4)) { f.close(); showError("BAD FILE"); return false; }
  int w = h8[4] | (h8[5] << 8), h = h8[6] | (h8[7] << 8);
  if (w < 1 || h < 1 || w > MAX_IMG || h > MAX_IMG) { f.close(); showError("TOO BIG"); return false; }
  setSize(w, h);
  size_t need = (size_t)w * h * 2; bool ok = f.read((uint8_t *)img, need) == need; f.close();
  if (!ok) { showError("BAD FILE"); return false; }
  return true;
}

bool loadStatic(const Item &it) {
  String p = pathOf(it.name); int w = 0, h = 0;
  bool ok = it.type == T_PNG ? readPngSize(p, w, h) : readBmpSize(p, w, h);
  if (!ok) { showError("BAD FILE"); return false; }
  if (w < 1 || h < 1 || w > MAX_IMG || h > MAX_IMG) { showError("TOO BIG"); return false; }
  if (it.type == T_PNG) { showError("RE-UPLOAD"); return false; }   // PNG is converted by the app (.pxl); on-device PNG decoding needs too much RAM
  LGFX_Sprite spr(&lcd); spr.setColorDepth(16);
  if (!spr.createSprite(w, h)) { showError("NO MEM"); return false; }
  spr.fillScreen(TKEY);
  bool dok = it.type == T_PNG ? spr.drawPngFile(SD, p.c_str(), 0, 0) : spr.drawBmpFile(SD, p.c_str(), 0, 0);
  spr.releasePngMemory();
  if (!dok) { spr.deleteSprite(); showError("DECODE"); return false; }
  setSize(w, h);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) img[y * w + x] = spr.readPixel(x, y);
  spr.deleteSprite();
  return true;
}

bool loadGif(const Item &it) {
  String p = pathOf(it.name);
  if (!gif.open(p.c_str(), gOpen, gClose, gRead, gSeek, gDraw)) { showError("BAD GIF"); return false; }
  int w = gif.getCanvasWidth(), h = gif.getCanvasHeight();
  if (w < 1 || h < 1 || w > MAX_IMG || h > MAX_IMG) { gif.close(); showError("TOO BIG"); return false; }
  gifOpen = true; gifLoopEnded = false; setSize(w, h); nextFrameAt = 0;
  return true;
}

void showItem(int idx, bool fx = true) {
  closeGif();
  if (items.empty()) return;
  cur = (idx % (int)items.size() + items.size()) % items.size();
  errText = ""; imgValid = false; infoOverlay = false;
  if (fx && GLITCH_FX) glitch(); else clearArt();
  const Item &it = items[cur];
  bool ok = false;
  if (it.type == T_ROM) { setSize(ROM_W, ROM_H); romStep = 0; loadRomFrame(ROM_SEQ[0]); nextFrameAt = millis() + ROM_DUR[0]; ok = true; }
  else if (!sdOK) showError("NO SD");
  else if (badFile.length() && it.name == badFile) showError("SKIPPED");
  else {
    strncpy(rtcLoading, it.name.c_str(), sizeof(rtcLoading) - 1); rtcLoading[sizeof(rtcLoading) - 1] = 0;
    if (it.type == T_GIF) ok = loadGif(it);
    else if (it.type == T_PXL) ok = loadPxl(it);
    else ok = loadStatic(it);
    rtcLoading[0] = 0;
  }
  imgValid = ok;
  motionFrame = 0; nextFrameAt = millis() + MOTION_MS;
  if (ok && motionActive()) pickSparks();
  if (ok) renderArt(true);
  if (netMode == NET_AP) showInfo(0);
  itemStart = millis();
  drawHud();
}

void scanFiles() {
  items.clear();
  items.push_back({"BITTO.ROM", T_ROM, 0});
  if (!sdOK) return;
  if (!SD.exists("/pixel")) SD.mkdir("/pixel");
  File dir = SD.open("/pixel"); if (!dir) return;
  std::vector<Item> found;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) { String n = f.name(); int s = n.lastIndexOf('/'); if (s >= 0) n = n.substring(s + 1);
      uint8_t t = typeOf(n); if (t != 255 && !n.startsWith(".")) found.push_back({n, t, (uint32_t)f.size()}); }
    f.close();
  }
  dir.close();
  std::sort(found.begin(), found.end(), [](const Item &a, const Item &b) { return a.name < b.name; });
  for (auto &i : found) items.push_back(i);
}

int findItem(const String &n) { for (size_t i = 0; i < items.size(); i++) if (items[i].name == n) return i; return -1; }

// =============================================================== playback tick
void tickPlayback() {
  if (infoOverlay && netMode != NET_AP && millis() > infoUntil) hideInfo();
  if (!imgValid || uploading) return;
  uint32_t now = millis();
  const Item &it = items[cur];
  if (motionActive() && now >= nextFrameAt) {
    motionFrame++; if (!infoOverlay) renderArt(false);
    nextFrameAt = now + MOTION_MS;
  }
  if (it.type == T_ROM && now >= nextFrameAt) {
    romStep = (romStep + 1) % sizeof(ROM_SEQ);
    loadRomFrame(ROM_SEQ[romStep]); if (!infoOverlay) renderArt(false);
    nextFrameAt = now + ROM_DUR[romStep];
  } else if (it.type == T_GIF && gifOpen && now >= nextFrameAt) {
    int delayMs = 100;
    int r = gif.playFrame(false, &delayMs);
    if (!infoOverlay) renderArt(false);
    if (delayMs < 20) delayMs = 100;
    nextFrameAt = now + delayMs;
    if (r == 0) { gifLoopEnded = true; gif.reset(); }
    else if (r < 0) { closeGif(); showError("GIF ERR"); }
  }
  if (autoMode && items.size() > 1 && now - itemStart >= SLIDE_SECONDS * 1000UL) {
    bool canSwitch = it.type != T_GIF || gifLoopEnded || now - itemStart > SLIDE_SECONDS * 3000UL;
    if (canSwitch) showItem(cur + 1);
  }
}

// =============================================================== input
void tickInput() {
  static bool down = false; static uint32_t t0 = 0; static bool longDone = false; static uint32_t lastUp = 0;
  bool touch = digitalRead(TOUCH_IRQ) == LOW;
  if (touch && !down && millis() - lastUp > 120) { down = true; t0 = millis(); longDone = false; }
  if (down && touch && !longDone && millis() - t0 > 700) {
    longDone = true; autoMode = !autoMode; itemStart = millis(); drawHud(); setLed(255, 200, 0);
  }
  if (down && !touch) { down = false; lastUp = millis(); if (!longDone && millis() - t0 > 30) showItem(cur + 1); }

  static bool bDown = false;
  bool b = digitalRead(BOOT_BTN) == LOW;
  if (b && !bDown) { if (infoOverlay && netMode != NET_AP) hideInfo(); else showInfo(8000); }
  bDown = b;
}

void tickLed() {
#if RGB_LED_ENABLE
  static uint32_t t = 0; if (millis() - t < 40) return; t = millis();
  float ph = (millis() % 4000) / 4000.0f * 2 * PI;
  float a = (sin(ph) + 1) / 2;
  setLed(40 * a, 20 * (1 - a), 30 + 40 * (1 - a));
#endif
}

// =============================================================== boot console
int bootY = 6;
void bootLine(const String &l, uint16_t c = 0) {
  lcd.setFont(&fonts::Font0); lcd.setTextSize(1); lcd.setTextColor(c ? c : C_CY, C_BG);
  for (size_t i = 0; i < l.length(); i++) { lcd.drawChar(l[i], 6 + i * 6, bootY); if (i % 3 == 0) delay(2); }
  bootY += 11;
}
void bootOk(const String &t, uint16_t c = 0) {
  lcd.setTextColor(c ? c : C_GRN, C_BG); lcd.drawRightString(t, 314, bootY - 11);
}

// =============================================================== web: handlers
String withCss(const char *page) { String s = FPSTR(page); s.replace("%CSS%", FPSTR(CSS_COMMON)); return s; }

void handleSetupRoot() { String s = withCss(PAGE_SETUP_HEAD); s.replace("%OPTIONS%", optionsHtml); server.send(200, "text/html; charset=utf-8", s); }
void handleSave() {
  String ss = server.arg("ssid"), pw = server.arg("pass"); ss.trim();
  if (!ss.length()) { server.send(400, "text/plain", "SSID required"); return; }
  prefs.putString("ssid", ss); prefs.putString("pass", pw);
  server.send(200, "text/html; charset=utf-8", withCss(PAGE_SAVED));
  showError("REBOOT");
  delay(1500); ESP.restart();
}

void handleMain() { server.send(200, "text/html; charset=utf-8", withCss(PAGE_MAIN)); }


String jsonEsc(const String &v) { String o; for (size_t i = 0; i < v.length(); i++) { char c = v[i]; if (c == '"' || c == '\\') o += '\\'; if ((uint8_t)c >= 0x20) o += c; } return o; }
String listJson() {
  String j = "{\"idx\":" + String(cur) + ",\"total\":" + items.size() + ",\"auto\":" + (autoMode ? "true" : "false") + ",\"motion\":" + (motion ? "true" : "false") +
             ",\"theme\":" + String(themeIdx) + ",\"sd\":" + (sdOK ? "true" : "false") + ",\"cur\":\"" + (items[cur].type == T_ROM ? "" : jsonEsc(items[cur].name)) + "\",\"files\":[";
  bool first = true;
  for (auto &i : items) { if (i.type == T_ROM) continue; if (!first) j += ","; first = false; j += "{\"n\":\"" + jsonEsc(i.name) + "\",\"s\":" + String(i.size) + "}"; }
  return j + "]}";
}
String infoJson() {
  const char *nm = netMode == NET_STA ? "sta" : netMode == NET_AP ? "ap" : "off";
  return String("{\"v\":\"") + FW_VERSION + "\",\"net\":\"" + nm + "\",\"ip\":\"" + ipText + "\",\"ssid\":\"" + jsonEsc(ssidSaved) +
         "\",\"ap\":\"" + apName + "\",\"host\":\"" HOSTNAME ".local\",\"theme\":" + themeIdx + ",\"themes\":" + THEME_COUNT +
         ",\"auto\":" + (autoMode ? "true" : "false") + ",\"motion\":" + (motion ? "true" : "false") + ",\"sd\":" + (sdOK ? "true" : "false") +
         ",\"total\":" + items.size() + ",\"idx\":" + cur + "}";
}
void setTheme(int t) {
  themeIdx = constrain(t, 0, THEME_COUNT - 1); prefs.putInt("theme", themeIdx);
  applyThemeColors(); drawFrame(); showItem(cur, false);
}
void handleList() { server.send(200, "application/json", listJson()); }
void handleInfo() { server.send(200, "application/json", infoJson()); }
void handleTheme() { setTheme(server.arg("t").toInt()); server.send(200, "text/plain", "ok"); }

void handleImg() {
  String n = cleanName(server.arg("n")); String p = pathOf(n);
  if (!sdOK || !SD.exists(p)) { server.send(404, "text/plain", "not found"); return; }
  bool wasGif = gifOpen && items[cur].name == n;   // avoid sharing the file handle
  File f = SD.open(p);
  String ct = extOf(n) == "gif" ? "image/gif" : extOf(n) == "png" ? "image/png" : extOf(n) == "bmp" ? "image/bmp" : "application/octet-stream";
  server.sendHeader("Cache-Control", "max-age=3600");
  server.streamFile(f, ct); f.close();
  (void)wasGif;
}

void handleShow() { int i = findItem(cleanName(server.arg("n"))); if (i >= 0) showItem(i); server.send(200, "text/plain", "ok"); }
void handleNext() { showItem(cur + 1); server.send(200, "text/plain", "ok"); }
void handlePrev() { showItem(cur - 1); server.send(200, "text/plain", "ok"); }
void handleMotion() { motion = !motion; prefs.putBool("motion", motion); showItem(cur, false); server.send(200, "text/plain", "ok"); }
void handleAuto() { autoMode = !autoMode; itemStart = millis(); drawHud(); server.send(200, "text/plain", "ok"); }
void handleDelete() {
  String n = cleanName(server.arg("n"));
  if (sdOK && n.length()) {
    String curName = items[cur].name;
    closeGif(); SD.remove(pathOf(n)); scanFiles();
    int i = findItem(curName); showItem(i >= 0 ? i : 0, false);
  }
  server.send(200, "text/plain", "ok");
}
void handleForget() {
  server.send(200, "text/plain", "ok");
  prefs.remove("ssid"); prefs.remove("pass"); delay(500); ESP.restart();
}

void handleUploadData() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    uploading = true; closeGif();
    uploadedName = cleanName(u.filename);
    if (!sdOK || typeOf(uploadedName) == 255) { uploadedName = ""; return; }
    showError("RECEIVING");
    uploadFile = SD.open(pathOf(uploadedName), FILE_WRITE);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (u.status == UPLOAD_FILE_ABORTED && uploadedName.length()) { SD.remove(pathOf(uploadedName)); uploadedName = ""; }
  }
}
void handleUploadDone() {
  uploading = false;
  if (!sdOK) { server.send(500, "text/plain", "NO SD CARD"); showItem(cur, false); return; }
  if (!uploadedName.length()) { server.send(400, "text/plain", "unsupported file"); showItem(cur, false); return; }
  scanFiles(); int i = findItem(uploadedName);
  server.send(200, "text/plain", "ok");
  showItem(i >= 0 ? i : 0);
}

void captiveRedirect() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); }


// =============================================================== USB serial control
// line commands (reply lines start with '@'):
//  PING | SCAN | LIST | NEXT | PREV | AUTO | MOTION | THEME n | SHOW name | DEL name | GET name | PUT name size | WIFI ssid<TAB>pass | WIFIRESET
String serLine;
void serOk(const String &m = "") { Serial.print("@OK"); if (m.length()) { Serial.print(' '); Serial.print(m); } Serial.print('\n'); }
void serErr(const String &m) { Serial.print("@ERR "); Serial.print(m); Serial.print('\n'); }

void serPut(const String &args) {
  int sp = args.lastIndexOf(' '); if (sp < 0) { serErr("args"); return; }
  String name = cleanName(args.substring(0, sp)); long size = args.substring(sp + 1).toInt();
  if (!sdOK) { serErr("nosd"); return; }
  if (typeOf(name) == 255 || size <= 0 || size > 2000000) { serErr("type"); return; }
  closeGif(); uploading = true; showError("RECEIVING");
  File f = SD.open(pathOf(name), FILE_WRITE);
  if (!f) { uploading = false; serErr("open"); showItem(cur, false); return; }
  Serial.print("@READY\n");
  uint8_t buf[512]; long got = 0; uint32_t t = millis();
  while (got < size && millis() - t < 8000) {
    int av = Serial.available();
    if (av > 0) { int n = Serial.readBytes(buf, min((long)min(av, 512), size - got)); f.write(buf, n); got += n; t = millis(); }
    else delay(1);
  }
  f.close(); uploading = false;
  if (got < size) { SD.remove(pathOf(name)); serErr("timeout"); showItem(cur, false); return; }
  scanFiles(); int i = findItem(name); serOk(name); showItem(i >= 0 ? i : 0);
}

void serGet(const String &args) {
  String n = cleanName(args); File f = sdOK ? SD.open(pathOf(n)) : File();
  if (!f) { serErr("notfound"); return; }
  Serial.print("@BIN "); Serial.print((uint32_t)f.size()); Serial.print('\n');
  uint8_t buf[512]; int r; while ((r = f.read(buf, sizeof(buf))) > 0) Serial.write(buf, r);
  f.close(); Serial.print('\n');
}


void serScan() {
  if (netMode == NET_AP) WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, false);
  String j = "[";
  int cnt = 0;
  for (int i = 0; i < n && cnt < 30; i++) {
    String ss = WiFi.SSID(i); if (!ss.length()) continue;
    bool dup = false; for (int k = 0; k < i; k++) if (WiFi.SSID(k) == ss) { dup = true; break; }
    if (dup) continue;
    if (cnt++) j += ",";
    j += "{\"s\":\"" + jsonEsc(ss) + "\",\"r\":" + String(WiFi.RSSI(i)) + ",\"c\":" + String(WiFi.channel(i)) + ",\"e\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
  }
  WiFi.scanDelete();
  if (netMode == NET_AP) WiFi.mode(WIFI_AP);
  Serial.print("@SCAN "); Serial.print(j + "]"); Serial.print('\n');
}

void serCommand(String l) {
  l.trim(); if (!l.length()) return;
  int sp = l.indexOf(' '); String cmd = sp < 0 ? l : l.substring(0, sp), arg = sp < 0 ? "" : l.substring(sp + 1);
  cmd.toUpperCase();
  if (cmd == "PING" || cmd == "INFO") { Serial.print("@PXL "); Serial.print(infoJson()); Serial.print('\n'); }
  else if (cmd == "LIST") { Serial.print("@LIST "); Serial.print(listJson()); Serial.print('\n'); }
  else if (cmd == "NEXT") { showItem(cur + 1); serOk(); }
  else if (cmd == "PREV") { showItem(cur - 1); serOk(); }
  else if (cmd == "AUTO") { autoMode = !autoMode; itemStart = millis(); drawHud(); serOk(); }
  else if (cmd == "MOTION") { motion = !motion; prefs.putBool("motion", motion); showItem(cur, false); serOk(); }
  else if (cmd == "THEME") { setTheme(arg.toInt()); serOk(); }
  else if (cmd == "SHOW") { int i = findItem(cleanName(arg)); if (i >= 0) { showItem(i); serOk(); } else serErr("notfound"); }
  else if (cmd == "DEL") {
    String n = cleanName(arg);
    if (sdOK && n.length()) { String curName = items[cur].name; closeGif(); SD.remove(pathOf(n)); scanFiles(); int i = findItem(curName); showItem(i >= 0 ? i : 0, false); }
    serOk();
  }
  else if (cmd == "SCAN") serScan();
  else if (cmd == "GET") serGet(arg);
  else if (cmd == "PUT") serPut(arg);
  else if (cmd == "WIFI") {
    int tab = arg.indexOf('\t'); String ss = tab < 0 ? arg : arg.substring(0, tab), pw = tab < 0 ? "" : arg.substring(tab + 1);
    ss.trim(); if (!ss.length()) { serErr("ssid"); return; }
    prefs.putString("ssid", ss); prefs.putString("pass", pw); serOk("restart"); Serial.flush(); delay(300); ESP.restart();
  }
  else if (cmd == "WIFIRESET") { prefs.remove("ssid"); prefs.remove("pass"); serOk("restart"); Serial.flush(); delay(300); ESP.restart(); }
  else serErr("unknown");
}

void tickSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (serLine.length()) { String l = serLine; serLine = ""; serCommand(l); } }
    else if (serLine.length() < 300) serLine += c;
  }
}

// =============================================================== network start
void startAP() {
  // scan first (cannot scan reliably while AP is serving)
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  int n = WiFi.scanNetworks();
  optionsHtml = "";
  for (int i = 0; i < n && i < 20; i++) {
    String s = WiFi.SSID(i); if (!s.length() || optionsHtml.indexOf(">" + s + "<") >= 0) continue;
    s.replace("\"", "&quot;"); s.replace("<", "&lt;");
    optionsHtml += "<option value=\"" + s + "\">" + s + " (" + WiFi.RSSI(i) + "dBm)</option>";
  }
  WiFi.scanDelete();
  uint8_t mac[6]; WiFi.macAddress(mac);
  char b[20]; snprintf(b, sizeof(b), "PXL-DECK-%02X%02X", mac[4], mac[5]); apName = b;
  WiFi.mode(WIFI_AP); WiFi.softAP(apName.c_str()); WiFi.setTxPower(WIFI_POWER_11dBm);
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", handleSetupRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(captiveRedirect);
  server.begin();
  netMode = NET_AP; ipText = "192.168.4.1";
}

bool startSTA() {
  ssidSaved = prefs.getString("ssid", "");
  if (!ssidSaved.length()) return false;
  WiFi.mode(WIFI_STA); WiFi.setHostname(HOSTNAME); WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.begin(ssidSaved.c_str(), prefs.getString("pass", "").c_str());
  uint32_t t = millis(); int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(300); lcd.setTextColor(C_DIM, C_BG); lcd.drawChar('.', 6 + (22 + dots++ % 30) * 6, bootY - 11);
  }
  if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(true); return false; }
  ipText = WiFi.localIP().toString();
  MDNS.begin(HOSTNAME); MDNS.addService("http", "tcp", 80);
  server.enableCORS(true);
  server.on("/", handleMain);
  server.on("/api/list", handleList);
  server.on("/api/info", handleInfo);
  server.on("/api/theme", HTTP_POST, handleTheme);
  server.on("/img", handleImg);
  server.on("/api/show", HTTP_POST, handleShow);
  server.on("/api/next", HTTP_POST, handleNext);
  server.on("/api/prev", HTTP_POST, handlePrev);
  server.on("/api/auto", HTTP_POST, handleAuto);
  server.on("/api/motion", HTTP_POST, handleMotion);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/forget", HTTP_POST, handleForget);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.onNotFound([]() { server.send(404, "text/plain", "404"); });
  server.begin();
  netMode = NET_STA;
  return true;
}

// =============================================================== setup / loop
void setup() {
  resetReason = esp_reset_reason();
  bool bad = resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT || resetReason == ESP_RST_BROWNOUT;
  if (crashMagic != 0xC0FFEE42) { crashMagic = 0xC0FFEE42; crashCount = 0; }
  crashCount = bad ? crashCount + 1 : 0;
  safeMode = crashCount >= 3;
  if (crashMagic == 0xC0FFEE42 && bad && rtcLoading[0]) { memcpy(rtcBad, rtcLoading, sizeof(rtcBad)); rtcBad[sizeof(rtcBad) - 1] = 0; }
  if (!bad) rtcBad[0] = 0;
  rtcLoading[0] = 0;
  badFile = String(rtcBad);
  lastStage = (bad && crashMagic == 0xC0FFEE42) ? String(rtcStage) : String("");
  setStage("BOOT");
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  Serial.printf("\n[PXL] boot v%s reset=%d crashes=%u safe=%d\n", FW_VERSION, (int)resetReason, (unsigned)crashCount, safeMode);
  pinMode(TOUCH_IRQ, INPUT);
  pinMode(BOOT_BTN, INPUT_PULLUP);
#if RGB_LED_ENABLE
  ledcAttach(LED_R, 5000, 8); ledcAttach(LED_G, 5000, 8); ledcAttach(LED_B, 5000, 8);
#endif
  setLed(0, 0, 0);

  lcd.init(); lcd.setRotation(1); lcd.setBrightness(220);
  prefs.begin("pxl", false);
  themeIdx = constrain(prefs.getInt("theme", 0), 0, THEME_COUNT - 1);
  applyThemeColors();
  lcd.fillScreen(C_BG);

  img = (uint16_t *)malloc(MAX_IMG * MAX_IMG * 2);
  shown = (uint16_t *)malloc(MAX_IMG * (MAX_IMG + 2) * 2);
  disp = (uint16_t *)malloc(MAX_IMG * (MAX_IMG + 2) * 2);
  sTop.setColorDepth(8); sTop.createSprite(DW, TOP_H);
  sMid.setColorDepth(8); sMid.createSprite(DW, MID_H);
  gif.begin(GIF_PALETTE_RGB565_LE);
  motion = prefs.getBool("motion", true);

  // ---- boot console ----
  bootLine("PXL//DECK BIOS v" FW_VERSION "  (c) NEON-WORKS", C_MG);
  bootLine("--------------------------------------------", C_DIM);
  bootLine("> CPU  ESP32 240MHz x2"); bootOk("OK");
  bootLine("> RAM  " + String(ESP.getFreeHeap() / 1024) + "KB FREE"); bootOk("OK");
  bootLine("> LCD  320x240 RGB565"); bootOk("OK");
  {
    const char *rn = resetReason == ESP_RST_POWERON ? "POWER ON" : resetReason == ESP_RST_SW ? "SOFTWARE" : resetReason == ESP_RST_PANIC ? "CRASH" :
                     resetReason == ESP_RST_BROWNOUT ? "LOW POWER" : (resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT) ? "WATCHDOG" :
                     resetReason == ESP_RST_EXT ? "RESET PIN" : "OTHER";
    bootLine("> RST  " + String(rn) + (crashCount ? "  x" + String(crashCount) : "") + (lastStage.length() ? " @" + lastStage : "")); bootOk(safeMode ? "SAFE MODE" : "OK", safeMode ? C_YL : C_GRN);
    if (resetReason == ESP_RST_BROWNOUT) bootLine("  ! USB POWER TOO WEAK? TRY ANOTHER PORT", C_YL);
  }

  // forget wifi if BOOT held
  if (digitalRead(BOOT_BTN) == LOW) {
    bootLine("> BOOT HELD: RESET WIFI?", C_YL);
    uint32_t t = millis(); while (digitalRead(BOOT_BTN) == LOW && millis() - t < 3000) delay(10);
    if (millis() - t >= 3000) { prefs.remove("ssid"); prefs.remove("pass"); bootOk("CLEARED", C_YL); }
  }

  setStage("SD");
  bootLine("> SD   MOUNT");
  sdSPI.begin(18, 19, 23, SD_CS);
  sdOK = SD.begin(SD_CS, sdSPI, 20000000);
  if (sdOK) bootOk(String((uint32_t)(SD.cardSize() / (1024 * 1024))) + "MB"); else bootOk("NONE", C_YL);
  scanFiles();
  bootLine("> SCAN /pixel"); bootOk(String(items.size() - 1) + " FILES", sdOK ? C_GRN : C_DIM);

  setStage("WIFI");
  bootLine("> NET  LINK  (" + String(ESP.getFreeHeap() / 1024) + "KB)");
  bool skipNet = safeMode && lastStage == "WIFI";
  if (skipNet) { bootOk("SKIPPED", C_YL); bootLine("  WIFI CRASHED LAST TIME - USB ONLY", C_YL); }
  else if (startSTA()) bootOk(ipText);
  else {
    bootOk("SETUP", C_YL);
    setStage("AP");
    startAP();
    bootLine("  JOIN WIFI: " + apName, C_YL);
    bootLine("  OPEN http://192.168.4.1", C_YL);
    delay(1500);
  }
  if (netMode == NET_STA) bootLine("  http://" HOSTNAME ".local", C_CY);
  bootLine(""); bootLine("SYSTEM READY_", C_GRN);
  delay(700);

  // ---- main screen ----
  if (GLITCH_FX) for (int k = 0; k < 20; k++) { lcd.fillRect(0, random(240), 320, 2 + random(6), (k & 1) ? C_CY : C_MG); delay(10); }
  setStage("SCREEN");
  drawFrame();
  showItem(0, false);
  setStage("LOOP");
}

void loop() {
  if (netMode == NET_AP) dns.processNextRequest();
  if (netMode != NET_OFF) server.handleClient();
  tickSerial();
  tickInput();
  tickPlayback();
  drawProgress();
  drawBlink();
  drawStream();
  tickLed();
  if (crashCount && millis() > 30000) { crashCount = 0; }
  delay(1);
}
