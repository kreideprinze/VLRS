#include "rl_leds.h"

ArduinoLEDMatrix matrix;

// --- glyph row masks, MSB = leftmost column ----------------------------------
static const uint8_t G_TOWER[8]      = {0b01010, 0b10101, 0b01010, 0b00100, 0b00100, 0b01010, 0b01010, 0b10001};
static const uint8_t G_TOWER_MAST[8] = {0, 0b00100, 0, 0b00100, 0b00100, 0b01010, 0b01010, 0b10001};
static const uint8_t G_FAN_ARC3[8]   = {0, 0b011110, 0b100001, 0, 0, 0, 0, 0};
static const uint8_t G_FAN_ARC2[8]   = {0, 0, 0, 0b001100, 0b010010, 0, 0, 0};
static const uint8_t G_FAN_DOT[8]    = {0, 0, 0, 0, 0, 0, 0b001100, 0b001100};
static const uint8_t G_ARROW[8]      = {0, 0, 0b00100, 0b00010, 0b11111, 0b00010, 0b00100, 0};
static const uint8_t G_X5[8]         = {0, 0b10001, 0b01010, 0b00100, 0b00100, 0b01010, 0b10001, 0};

void fbClear(Frame f) { memset(f, 0, sizeof(Frame)); }

static void fbGlyph(Frame f, const uint8_t rows[8], int w, int x0) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < w; c++)
      if (rows[r] & (1 << (w - 1 - c))) {
        int x = x0 + c;
        if (x >= 0 && x < 12) f[r][x] = 1;
      }
}

static void fbFan(Frame f, int x0, bool dot, bool arc2, bool arc3) {
  if (dot)  fbGlyph(f, G_FAN_DOT, 6, x0);
  if (arc2) fbGlyph(f, G_FAN_ARC2, 6, x0);
  if (arc3) fbGlyph(f, G_FAN_ARC3, 6, x0);
}

static void fbPack(const Frame f, uint32_t out[3]) {   // LED 0 = MSB of word 0
  out[0] = out[1] = out[2] = 0;
  for (int i = 0; i < 96; i++)
    if (f[i / 12][i % 12]) out[i / 32] |= (1UL << (31 - (i % 32)));
}

void ledShow(Frame f) { matrix.renderBitmap(f, 8, 12); }
void ledBegin() { matrix.begin(); }

// --- 3x5 font ----------------------------------------------------------------
struct Glyph3 { char c; uint8_t w; uint8_t rows[5]; };
static const Glyph3 FONT[] = {
  {'0',3,{7,5,5,5,7}}, {'1',3,{2,6,2,2,7}}, {'2',3,{7,1,7,4,7}}, {'3',3,{7,1,7,1,7}},
  {'4',3,{5,5,7,1,1}}, {'5',3,{7,4,7,1,7}}, {'6',3,{7,4,7,5,7}}, {'7',3,{7,1,1,2,2}},
  {'8',3,{7,5,7,5,7}}, {'9',3,{7,5,7,1,7}}, {'.',1,{0,0,0,0,1}}, {' ',2,{0,0,0,0,0}},
  {'A',3,{2,5,7,5,5}}, {'P',3,{6,5,6,4,4}}, {'S',3,{3,4,2,1,6}}, {'T',3,{7,2,2,2,2}},
  {'I',3,{7,2,2,2,7}}, {'D',3,{6,5,5,5,6}}, {'M',3,{5,7,7,5,5}}, {'H',3,{5,5,7,5,5}},
  {'F',3,{7,4,6,4,4}}, {'X',3,{5,5,2,5,5}}, {'?',3,{7,1,3,0,2}}, {'!',3,{2,2,2,0,2}},
  {'E',3,{7,4,6,4,7}}, {'R',3,{6,5,6,5,5}}, {'C',3,{3,4,4,4,3}}, {'L',3,{4,4,4,4,7}},
  {'N',3,{6,5,5,5,5}}, {'K',3,{5,5,6,5,5}}, {'G',3,{3,4,5,5,3}}, {'-',3,{0,0,7,0,0}},
};
static const Glyph3* fontFind(char c) {
  for (auto& g : FONT) if (g.c == c) return &g;
  return &FONT[11];
}
int textWidth(const char* s) {
  int w = 0;
  for (; *s; s++) w += fontFind(*s)->w + 1;
  return w;
}
void fbText(Frame f, const char* s, int x, int y) {
  for (; *s; s++) {
    const Glyph3* g = fontFind(*s);
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < g->w; c++)
        if (g->rows[r] & (1 << (g->w - 1 - c))) {
          int xx = x + c, yy = y + r;
          if (xx >= 0 && xx < 12 && yy >= 0 && yy < 8) f[yy][xx] = 1;
        }
    x += g->w + 1;
  }
}

// --- main screen ---------------------------------------------------------------
void ledMain(uint8_t id, char state, RlNetGlyph net, int lq) {
  Frame f;
  fbClear(f);
  char d[3] = {char('0' + (id / 10) % 10), char('0' + id % 10), 0};
  fbText(f, d, 0, 0);
  bool blinkOn = (millis() / 300) & 1;
  if (state && (state != '?' || blinkOn)) {
    char s[2] = {state, 0};
    fbText(f, s, 9, 0);
  }
  switch (net) {
    case NG_DIRECT:   f[6][0] = f[6][1] = f[6][2] = f[7][0] = f[7][1] = f[7][2] = 1; break;
    case NG_EXTERNAL: f[6][0] = f[6][2] = f[7][1] = 1; break;
    case NG_CONFIG:   f[6][0] = f[6][1] = f[6][2] = f[7][0] = f[7][2] = 1; break;
    default: break;
  }
  if (lq < 0) {
    if (blinkOn) f[7][4] = 1;
  } else {
    int n = (lq * 8 + 50) / 100;
    if (lq > 0 && n == 0) n = 1;
    for (int i = 0; i < n && i < 8; i++) f[6][4 + i] = f[7][4 + i] = 1;
  }
  ledShow(f);
}

// --- sequences (ISR-driven) ------------------------------------------------------
static uint32_t seqBuf[24][4];
static int seqLen = 0;
static uint32_t seqTotalMs = 0;

static void seqBegin() { seqLen = 0; seqTotalMs = 0; }
static void seqAdd(const Frame f, uint32_t ms) {
  if (seqLen >= 24) return;
  fbPack(f, seqBuf[seqLen]);
  seqBuf[seqLen][3] = ms;
  seqLen++;
  seqTotalMs += ms;
}
static uint32_t seqPlay(bool loop) {
  matrix.loadWrapper(seqBuf, seqLen * 16);
  matrix.play(loop);
  return seqTotalMs;
}

void ledPlayConnecting() {
  Frame f;
  seqBegin();
  fbClear(f); fbFan(f, 3, true, false, false);  seqAdd(f, 250);
  fbClear(f); fbFan(f, 3, true, true, false);   seqAdd(f, 250);
  fbClear(f); fbFan(f, 3, true, true, true);    seqAdd(f, 400);
  fbClear(f);                                   seqAdd(f, 150);
  seqPlay(true);
}

static void addArrowSweep() {
  Frame f;
  for (int x = 0; x <= 7; x += 2) { fbClear(f); fbGlyph(f, G_ARROW, 5, x); seqAdd(f, 70); }
  fbClear(f); seqAdd(f, 70);
}

uint32_t ledPlayStaToAp() {
  Frame f;
  seqBegin();
  fbClear(f); fbFan(f, 3, true, true, true);   seqAdd(f, 300);
  fbClear(f); fbFan(f, 3, true, true, false);  seqAdd(f, 120);
  fbClear(f); fbFan(f, 3, true, false, false); seqAdd(f, 120);
  fbClear(f);                                   seqAdd(f, 80);
  addArrowSweep();
  fbClear(f); fbGlyph(f, G_TOWER_MAST, 5, 3);  seqAdd(f, 150);
  fbClear(f); fbGlyph(f, G_TOWER, 5, 3);       seqAdd(f, 300);
  fbClear(f);                                   seqAdd(f, 80);
  fbClear(f); fbGlyph(f, G_TOWER, 5, 3);       seqAdd(f, 400);
  fbClear(f); fbGlyph(f, G_TOWER, 5, 3);       seqAdd(f, 1);
  return seqPlay(false);
}

uint32_t ledPlayApToSta() {
  Frame f;
  seqBegin();
  fbClear(f); fbGlyph(f, G_TOWER, 5, 3);       seqAdd(f, 300);
  fbClear(f); fbGlyph(f, G_TOWER_MAST, 5, 3);  seqAdd(f, 120);
  fbClear(f);                                   seqAdd(f, 80);
  addArrowSweep();
  fbClear(f); fbFan(f, 3, true, false, false); seqAdd(f, 150);
  fbClear(f); fbFan(f, 3, true, true, false);  seqAdd(f, 150);
  fbClear(f); fbFan(f, 3, true, true, true);   seqAdd(f, 300);
  fbClear(f);                                   seqAdd(f, 80);
  fbClear(f); fbFan(f, 3, true, true, true);   seqAdd(f, 400);
  fbClear(f); fbFan(f, 3, true, true, true);   seqAdd(f, 1);
  return seqPlay(false);
}

// --- anim -> scroll -> main ----------------------------------------------------------
enum { L_ANIM, L_SCROLL, L_MAIN };
static uint8_t lstate = L_MAIN;
static uint32_t lUntil = 0, lStep = 0;
static int lScrollX = 12;
static char lText[40];

void ledSequence(uint32_t animMs, const char* scrollText) {
  strncpy(lText, scrollText ? scrollText : "", sizeof(lText) - 1);
  lText[sizeof(lText) - 1] = 0;
  lScrollX = 12;
  if (animMs) { lstate = L_ANIM; lUntil = millis() + animMs; }
  else lstate = lText[0] ? L_SCROLL : L_MAIN;
}

bool ledTick() {
  uint32_t now = millis();
  if (lstate == L_ANIM) {
    if ((int32_t)(now - lUntil) < 0) return false;
    lstate = lText[0] ? L_SCROLL : L_MAIN;
  }
  if (lstate == L_SCROLL) {
    if (now - lStep < 70) return false;
    lStep = now;
    Frame f; fbClear(f);
    fbText(f, lText, lScrollX);
    ledShow(f);
    if (--lScrollX < -textWidth(lText)) lstate = L_MAIN;
    return false;
  }
  return true;
}

// --- calibration / error screens (from ~/code.ino) ------------------------------------
void ledFlash() {
  Frame full, empty;
  memset(full, 1, sizeof(full)); fbClear(empty);
  for (int i = 0; i < 3; i++) { ledShow(full); delay(120); ledShow(empty); delay(120); }
}
void ledCenterSquare() {
  Frame f; fbClear(f);
  f[3][5] = f[3][6] = f[4][5] = f[4][6] = 1;
  ledShow(f);
}
void ledBorder() {
  Frame f; fbClear(f);
  for (int x = 0; x < 12; x++) { f[0][x] = 1; f[7][x] = 1; }
  for (int y = 0; y < 8; y++)  { f[y][0] = 1; f[y][11] = 1; }
  ledShow(f);
}
void ledCheck() {
  Frame f; fbClear(f);
  f[5][2]=1; f[6][3]=1; f[7][4]=1; f[6][5]=1; f[5][6]=1; f[4][7]=1; f[3][8]=1; f[2][9]=1; f[1][10]=1;
  ledShow(f);
}
void ledFatal() {
  Frame f; fbClear(f); fbGlyph(f, G_X5, 5, 3); ledShow(f);
}
