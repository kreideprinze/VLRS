#pragma once
// ---------------------------------------------------------------------------
// 12x8 LED matrix: glyphs, a tiny 3x5 font for the IP scroll, and the
// WiFi-mode change animations. Animations are played by the matrix library's
// timer ISR, so they keep running while the main loop is blocked in a WiFi
// call (e.g. WiFi.begin()).
//
// Layout of the status view:
//   cols 0-5  : WiFi mode glyph   (AP = radio tower, STA = WiFi fan)
//   cols 7-11 : link / vehicle state
// ---------------------------------------------------------------------------
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

typedef uint8_t Frame[8][12];

// Row masks, MSB = leftmost column of the glyph.
// AP: radio tower ((•)) on an A-frame mast, 5 wide.
const uint8_t G_TOWER[8] = {0b01010, 0b10101, 0b01010, 0b00100, 0b00100, 0b01010, 0b01010, 0b10001};
const uint8_t G_TOWER_MAST[8] = {0, 0b00100, 0, 0b00100, 0b00100, 0b01010, 0b01010, 0b10001};
// STA: WiFi fan, 6 wide, built from three parts so it can fill in.
const uint8_t G_FAN_ARC3[8] = {0, 0b011110, 0b100001, 0, 0, 0, 0, 0};
const uint8_t G_FAN_ARC2[8] = {0, 0, 0, 0b001100, 0b010010, 0, 0, 0};
const uint8_t G_FAN_DOT[8]  = {0, 0, 0, 0, 0, 0, 0b001100, 0b001100};
// Right-zone state glyphs, 5 wide.
const uint8_t G_BANG[8] = {0, 0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100, 0};
const uint8_t G_DOT[8]  = {0, 0, 0, 0b00100, 0b00100, 0, 0, 0};
const uint8_t G_BOX[8]  = {0, 0b11111, 0b10001, 0b10001, 0b10001, 0b10001, 0b11111, 0};
const uint8_t G_FILL[8] = {0, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0};
const uint8_t G_HOLD[8] = {0, 0b10001, 0b10001, 0b11111, 0b11111, 0b10001, 0b10001, 0};
const uint8_t G_X[8]    = {0, 0b10001, 0b01010, 0b00100, 0b00100, 0b01010, 0b10001, 0};
const uint8_t G_ARROW[8] = {0, 0, 0b00100, 0b00010, 0b11111, 0b00010, 0b00100, 0};

inline void fbClear(Frame f) { memset(f, 0, sizeof(Frame)); }

void fbGlyph(Frame f, const uint8_t rows[8], int w, int x0) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < w; c++)
      if (rows[r] & (1 << (w - 1 - c))) {
        int x = x0 + c;
        if (x >= 0 && x < 12) f[r][x] = 1;
      }
}

void fbFan(Frame f, int x0, bool dot, bool arc2, bool arc3) {
  if (dot)  fbGlyph(f, G_FAN_DOT, 6, x0);
  if (arc2) fbGlyph(f, G_FAN_ARC2, 6, x0);
  if (arc3) fbGlyph(f, G_FAN_ARC3, 6, x0);
}

// Pack a frame into the library's 3x uint32 format (LED 0 = MSB of word 0).
void fbPack(const Frame f, uint32_t out[3]) {
  out[0] = out[1] = out[2] = 0;
  for (int i = 0; i < 96; i++)
    if (f[i / 12][i % 12]) out[i / 32] |= (1UL << (31 - (i % 32)));
}

// --- sequences (ISR-driven) -------------------------------------------------
static uint32_t seqBuf[24][4];
static int seqLen = 0;
static uint32_t seqTotalMs = 0;

void seqBegin() { seqLen = 0; seqTotalMs = 0; }
void seqAdd(const Frame f, uint32_t ms) {
  if (seqLen >= 24) return;
  fbPack(f, seqBuf[seqLen]);
  seqBuf[seqLen][3] = ms;
  seqLen++;
  seqTotalMs += ms;
}
// Returns the total duration; caller shows the next static frame after it.
uint32_t seqPlay(bool loop) {
  matrix.loadWrapper(seqBuf, seqLen * 16);
  matrix.play(loop);
  return seqTotalMs;
}

// STA connecting: the fan fills in, looped while WiFi.begin() blocks.
void playConnecting() {
  Frame f;
  seqBegin();
  fbClear(f); fbFan(f, 0, true, false, false);  seqAdd(f, 250);
  fbClear(f); fbFan(f, 0, true, true, false);   seqAdd(f, 250);
  fbClear(f); fbFan(f, 0, true, true, true);    seqAdd(f, 400);
  fbClear(f);                                   seqAdd(f, 150);
  seqPlay(true);
}

static void addArrowSweep() {
  Frame f;
  for (int x = 0; x <= 7; x += 2) { fbClear(f); fbGlyph(f, G_ARROW, 5, x); seqAdd(f, 70); }
  fbClear(f); seqAdd(f, 70);
}

// STA -> AP: fan collapses, arrow sweeps, tower appears and settles left.
uint32_t playStaToAp() {
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
  fbClear(f); fbGlyph(f, G_TOWER, 5, 3);       seqAdd(f, 300);
  fbClear(f); fbGlyph(f, G_TOWER, 5, 0);       seqAdd(f, 200);
  fbClear(f); fbGlyph(f, G_TOWER, 5, 0);       seqAdd(f, 1);   // end frame
  return seqPlay(false);
}

// AP -> STA: tower collapses, arrow sweeps, fan builds up and settles left.
uint32_t playApToSta() {
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
  fbClear(f); fbFan(f, 3, true, true, true);   seqAdd(f, 300);
  fbClear(f); fbFan(f, 0, true, true, true);   seqAdd(f, 200);
  fbClear(f); fbFan(f, 0, true, true, true);   seqAdd(f, 1);
  return seqPlay(false);
}

// --- 3x5 font for the IP scroll (digits, '.', ' ', A P S T) ----------------
struct Glyph3 { char c; uint8_t w; uint8_t rows[5]; };
const Glyph3 FONT[] = {
  {'0',3,{7,5,5,5,7}}, {'1',3,{2,6,2,2,7}}, {'2',3,{7,1,7,4,7}}, {'3',3,{7,1,7,1,7}},
  {'4',3,{5,5,7,1,1}}, {'5',3,{7,4,7,1,7}}, {'6',3,{7,4,7,5,7}}, {'7',3,{7,1,1,2,2}},
  {'8',3,{7,5,7,5,7}}, {'9',3,{7,5,7,1,7}}, {'.',1,{0,0,0,0,1}}, {' ',2,{0,0,0,0,0}},
  {'A',3,{2,5,7,5,5}}, {'P',3,{6,5,6,4,4}}, {'S',3,{3,4,2,1,6}}, {'T',3,{7,2,2,2,2}},
};
const Glyph3* fontFind(char c) {
  for (auto& g : FONT) if (g.c == c) return &g;
  return &FONT[11];
}
int textWidth(const char* s) {
  int w = 0;
  for (; *s; s++) w += fontFind(*s)->w + 1;
  return w;
}
// Draw text with its left edge at x (may be negative), rows 2..6.
void fbText(Frame f, const char* s, int x) {
  for (; *s; s++) {
    const Glyph3* g = fontFind(*s);
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < g->w; c++)
        if (g->rows[r] & (1 << (g->w - 1 - c))) {
          int xx = x + c;
          if (xx >= 0 && xx < 12) f[r + 2][xx] = 1;
        }
    x += g->w + 1;
  }
}

// --- boot / calibration helpers (from ~/code.ino) ---------------------------
void ledShow(Frame f) { matrix.renderBitmap(f, 8, 12); }

void flashMatrix() {
  Frame full, empty;
  memset(full, 1, sizeof(full)); fbClear(empty);
  for (int i = 0; i < 3; i++) { ledShow(full); delay(120); ledShow(empty); delay(120); }
}
void drawCenterSquare() {
  Frame f; fbClear(f);
  f[3][5] = f[3][6] = f[4][5] = f[4][6] = 1;
  ledShow(f);
}
void drawBorder() {
  Frame f; fbClear(f);
  for (int x = 0; x < 12; x++) { f[0][x] = 1; f[7][x] = 1; }
  for (int y = 0; y < 8; y++)  { f[y][0] = 1; f[y][11] = 1; }
  ledShow(f);
}
void drawCheckmark() {
  Frame f; fbClear(f);
  f[5][2]=1; f[6][3]=1; f[7][4]=1; f[6][5]=1; f[5][6]=1; f[4][7]=1; f[3][8]=1; f[2][9]=1; f[1][10]=1;
  ledShow(f);
}
