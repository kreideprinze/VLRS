#pragma once
// ---------------------------------------------------------------------------
// ROVER//RC handheld (TX) configuration. Bind ID, WiFi, peer and static IP
// are runtime settings (web page / serial), not here.
// ---------------------------------------------------------------------------

#define SEND_PERIOD_MS     20      // 50 Hz to the RX while linked
#define SEARCH_PERIOD_MS   200     // 5 Hz broadcast while looking for the RX
#define LINK_TIMEOUT_MS    1500    // no RX stats for this long -> searching, interlock

// --- Pins (same wiring as ~/code.ino) ---------------------------------------
const int PIN_MODE     = 2;   // 2-pos #1  -> ch5  mode: ON = MANUAL, OFF = HOLD (RC never arms/disarms)
const int PIN_ESTOP    = 3;   // 2-pos #2  -> ch14 Motor E-stop
const int PIN_SW1_UP   = 4;   // 3-pos #1  -> not sent (spare, shown on the web page)
const int PIN_SW1_DN   = 5;
const int PIN_SW2_UP   = 6;   // 3-pos #2  -> ch10 spare; held DOWN at boot = force config AP
const int PIN_SW2_DN   = 7;
const int PIN_LX = A0, PIN_LY = A1, PIN_RX = A2, PIN_RY = A3;

// Flip these if a stick moves the wrong way (check on the web page / rc_monitor.py).
// Convention after inversion: right = +, forward/up = +.
const bool INVERT_LX = false;
const bool INVERT_LY = false;
const bool INVERT_RX = true;    // A2 = throttle after SWAP_RIGHT_STICK; inverted so forward = ch2 high
const bool INVERT_RY = false;   // A3 = steering after SWAP_RIGHT_STICK

// The right gimbal is mounted rotated: A2 moves with forward/back, A3 with left/right.
// true = A3 drives steering (ch1) and A2 drives throttle (ch2). INVERT_RX/RY still
// refer to the pins (A2/A3), applied before the swap.
const bool SWAP_RIGHT_STICK = true;

const float STICK_DEADZONE = 0.05f;   // +-5 % around centre, same as controller.py

// --- PWM values (match the Cube's RC1/RC2 MIN/TRIM/MAX = 1051/1501/1951) ----
const uint16_t PWM_LOW  = 1051;
const uint16_t PWM_MID  = 1501;
const uint16_t PWM_HIGH = 1951;
const uint16_t PWM_SPAN = 450;

// Fallback stick ranges when EEPROM has no calibration: full ADC span, so an
// uncalibrated stick can only under-shoot (never full throttle early).
// Centres are re-measured at every power-on, so leave the sticks alone while it boots.
const int DEF_MIN = 0, DEF_MID = 512, DEF_MAX = 1023;

// ch5 mode slots with MODE_CH=5: 1501 -> slot 4 (HOLD), 1951 -> slot 6 (MANUAL).
// The RC never sends slot 1 (1051), so GUIDED cannot be selected from here.
const uint16_t PWM_MODE_MANUAL = 1951;
const uint16_t PWM_MODE_HOLD   = 1501;

// Channels with no control sit at the MIDDLE (1501). On the rover Cube ch6/ch9/ch16
// are ArmDisarm aux switches and ch7/ch8 mount/roll inputs; ArduPilot takes no
// action for a 3-pos aux switch in the middle, so the RC can never arm or disarm
// (a LOW value would disarm the moment RC input first appears).
const uint16_t PWM_IDLE = 1501;
