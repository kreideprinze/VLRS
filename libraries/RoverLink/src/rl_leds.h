#pragma once
// ---------------------------------------------------------------------------
// 12x8 LED matrix: main screen, 3x5 font, WiFi-mode change animations.
//
// Main screen (both boards):
//   cols 0-6,  rows 0-4 : bind ID, two digits            "07"
//   cols 9-11, rows 0-4 : state letter  M H X ! F, blinking ? = searching
//   cols 0-2,  rows 6-7 : network  solid = direct link, "v" = external WiFi,
//                                  "n" = TX config AP
//   cols 4-11, rows 6-7 : link-quality bar, 8 px = 100 %
//
// Animations run from the matrix library's timer ISR, so they keep playing
// while the main loop is blocked inside a WiFi call.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "Arduino_LED_Matrix.h"

extern ArduinoLEDMatrix matrix;
typedef uint8_t Frame[8][12];

enum RlNetGlyph : uint8_t { NG_NONE = 0, NG_DIRECT = 1, NG_EXTERNAL = 2, NG_CONFIG = 3 };

void ledBegin();
void fbClear(Frame f);
void fbText(Frame f, const char* s, int x, int y = 2);
int  textWidth(const char* s);
void ledShow(Frame f);

// Main screen. state: 'M','H','X','!','F','?' (blinks), 0 = blank. lq < 0 = no link.
void ledMain(uint8_t id, char state, RlNetGlyph net, int lq);

// Boot / mode-change sequence: optional animation, then one text scroll,
// then ledTick() returns true and the caller draws the main screen.
void ledPlayConnecting();                 // looping fan fill, while WiFi.begin() blocks
uint32_t ledPlayStaToAp();
uint32_t ledPlayApToSta();
void ledSequence(uint32_t animMs, const char* scrollText);
bool ledTick();                           // false while an animation / scroll is running

// Calibration / error screens.
void ledFlash();
void ledCenterSquare();
void ledBorder();
void ledCheck();
void ledFatal();
