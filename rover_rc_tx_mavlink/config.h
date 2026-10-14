#pragma once
// ---------------------------------------------------------------------------
// ROVER//RC configuration. Everything you might want to tune lives here.
// ---------------------------------------------------------------------------

// --- WiFi --------------------------------------------------------------------
#define AP_SSID            "ROVER-RC"
#define AP_PASS_DEFAULT    "roverrc123"     // WPA2, >= 8 chars; changeable from the web page
#define STA_JOIN_TIMEOUT_MS 15000           // saved network must come up within this, else AP

// --- MAVLink -----------------------------------------------------------------
#define RC_SYSID           200              // must equal MAV_GCS_SYSID on the Cube
#define RC_COMPID          190              // MAV_COMP_ID_MISSIONPLANNER
#define VEHICLE_SYSID      1                // MAV_SYSID on the Cube
#define VEHICLE_COMPID     1
#define MAV_PORT           14560            // RC listens/sends here; rc_link.py listens here too

#define OVERRIDE_PERIOD_MS 40               // 25 Hz RC_CHANNELS_OVERRIDE
#define HEARTBEAT_PERIOD_MS 500             // 2 Hz GCS heartbeat (faster discovery)
#define LINK_TIMEOUT_MS    3000             // no vehicle heartbeat for this long -> searching

// --- Pins (same wiring as ~/code.ino) ---------------------------------------
const int PIN_MODE     = 2;   // 2-pos #1  -> ch5  mode: ON = MANUAL, OFF = HOLD (RC never arms/disarms)
const int PIN_ESTOP    = 3;   // 2-pos #2  -> ch14 Motor E-stop
const int PIN_SW1_UP   = 4;   // 3-pos #1  -> not sent (spare, shown on the web page)
const int PIN_SW1_DN   = 5;
const int PIN_SW2_UP   = 6;   // 3-pos #2  -> ch6  spare; held DOWN at boot = force AP
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
