#pragma once
// ---------------------------------------------------------------------------
// Timer-generated output waveform on D1 (P302), used for SBUS and PPM.
//
// The RA4M1 UART can't invert its TX line (SPTR.TINV is not implemented on
// SCI2), so inverted SBUS - which ExpressLRS gets from the ESP UART's line
// inversion - is built here as a list of constant-level intervals. A GPT
// timer's buffered period register times every interval in hardware; the
// overflow ISR only sets the pin level for the interval that just started and
// queues the length of the next one. Edges therefore stay on the 10 us SBUS
// bit grid even while the main loop is blocked in WiFi calls.
//
// Frames are double-buffered: the main loop fills the spare buffer and hands
// it over; the ISR switches to it at the end of the current frame.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "FspTimer.h"
#include "sbus.h"

#define WAVE_MAX       320            // intervals per frame (SBUS: <= 301)
#define PPM_CH         8
#define PPM_FRAME_US   22500
#define PPM_MARK_US    300

struct WaveBuf {
  uint16_t n;
  uint8_t  lvl[WAVE_MAX];
  uint16_t cnt[WAVE_MAX];   // interval length in timer counts
};

static FspTimer waveTimer;
static WaveBuf waveA, waveB;
static WaveBuf* volatile waveCur = &waveA;
static WaveBuf* volatile wavePend = nullptr;
static volatile uint16_t waveIdx = 0;
static volatile uint32_t waveFrames = 0;
static uint32_t waveCountsPerUs = 3;

static inline void wavePin(uint8_t high) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_03_PIN_02, high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

// Invariant: on entry, interval (waveCur, waveIdx) is starting now.
static void waveIsr(timer_callback_args_t* a) {
  if (a->event != TIMER_EVENT_CYCLE_END) return;
  WaveBuf* b = waveCur;
  wavePin(b->lvl[waveIdx]);                 // first: keep edge latency constant
  uint16_t ni = waveIdx + 1;
  WaveBuf* nb = b;
  if (ni >= b->n) {
    ni = 0;
    waveFrames++;
    if (wavePend) { nb = wavePend; wavePend = nullptr; }
  }
  waveTimer.set_period(nb->cnt[ni]);        // buffered: applies when this interval ends
  waveCur = nb;
  waveIdx = ni;
}

// --- frame builders --------------------------------------------------------------
static void waveAdd(WaveBuf* w, uint8_t level, uint32_t us) {
  uint32_t c = us * waveCountsPerUs;
  if (w->n && w->lvl[w->n - 1] == level) {       // merge with the previous run
    uint32_t t = (uint32_t)w->cnt[w->n - 1] + c;
    w->cnt[w->n - 1] = t > 65000 ? 65000 : t;
    return;
  }
  if (w->n >= WAVE_MAX) return;
  w->lvl[w->n] = level;
  w->cnt[w->n] = c > 65000 ? 65000 : c;
  w->n++;
}

// Inverted SBUS: line HIGH = UART "0". 25 bytes x (start, 8 data LSB first,
// even parity, 2 stop) at 10 us/bit, then idle (LOW) for the rest of 9 ms.
static void waveBuildSbus(WaveBuf* w, const uint16_t* us14, bool failsafe, bool lost) {
  uint8_t f[25];
  sbusFrame(f, us14, failsafe, lost);
  w->n = 0;
  for (int i = 0; i < 25; i++) {
    uint8_t b = f[i], par = 0;
    waveAdd(w, 1, 10);                           // start bit (0) -> HIGH
    for (int j = 0; j < 8; j++) {
      uint8_t bit = (b >> j) & 1;
      par ^= bit;
      waveAdd(w, !bit, 10);
    }
    waveAdd(w, !par, 10);                        // even parity
    waveAdd(w, 0, 20);                           // 2 stop bits (1) -> LOW
  }
  waveAdd(w, 0, SBUS_PERIOD_MS * 1000 - 25 * 120);   // idle LOW until the next frame
}

// PPM: 300 us LOW mark + HIGH remainder per channel, then the sync gap.
// run = false: line held HIGH (no pulses -> ArduPilot sees no RC).
static void waveBuildPpm(WaveBuf* w, const uint16_t* ch, bool run) {
  w->n = 0;
  if (!run) { waveAdd(w, 1, PPM_FRAME_US); return; }
  uint32_t sum = 0;
  for (int i = 0; i < PPM_CH; i++) {
    uint16_t v = constrain(ch[i], 900, 2100);
    sum += v;
    waveAdd(w, 0, PPM_MARK_US);
    waveAdd(w, 1, v - PPM_MARK_US);
  }
  waveAdd(w, 0, PPM_MARK_US);
  waveAdd(w, 1, PPM_FRAME_US - sum - PPM_MARK_US);
}

// Main loop: returns the spare buffer to fill, or nullptr if one is still queued.
static WaveBuf* waveSpare() {
  if (wavePend) return nullptr;
  return waveCur == &waveA ? &waveB : &waveA;
}
static void waveQueue(WaveBuf* w) { wavePend = w; }

static bool waveBegin(uint8_t idleLevel) {
  pinMode(1, OUTPUT);
  wavePin(idleLevel);
  waveA.n = waveB.n = 1;
  waveA.lvl[0] = waveB.lvl[0] = idleLevel;
  uint8_t type;
  int8_t ch = FspTimer::get_available_timer(type);
  if (ch < 0 || type != GPT_TIMER) return false;
  waveCountsPerUs = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKD) / 16 / 1000000;
  waveA.cnt[0] = waveB.cnt[0] = 1000 * waveCountsPerUs;
  if (!waveTimer.begin(TIMER_MODE_PERIODIC, type, ch, 1000 * waveCountsPerUs, 0, TIMER_SOURCE_DIV_16, waveIsr)) return false;
  if (!waveTimer.setup_overflow_irq(2)) return false;   // highest practical priority: edge timing
  if (!waveTimer.open()) return false;
  waveCur = &waveA;
  waveIdx = 0;
  return waveTimer.start();
}
