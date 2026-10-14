#pragma once
// ---------------------------------------------------------------------------
// ROVER//RC rover receiver (RX) configuration. Bind ID, WiFi, peer and static
// IP are runtime settings (web page / serial), not here.
//
// Wiring: R4 D1 (TX) -> Cube RC IN signal, GND -> GND. Power the R4 from its
// own 5 V (USB or a BEC), not from the RC IN 5 V pin.
// ---------------------------------------------------------------------------

#define IBUS_PERIOD_MS      7      // standard iBus frame rate (~143 Hz)
#define HOLD_AFTER_MS       300    // no TX packet: sticks neutral + ch5 HOLD
#define FAILSAFE_AFTER_MS   1500   // no TX packet: also set the iBus failsafe flag
#define STAT_PERIOD_MS      200    // link stats back to the TX (5 Hz)

const uint16_t PWM_LOW  = 1051;
const uint16_t PWM_MID  = 1501;
const uint16_t PWM_HIGH = 1951;
const uint16_t PWM_MODE_HOLD = 1501;     // ch5 slot 4 with MODE_CH=5
const uint16_t MODE_MANUAL_ABOVE = 1700; // ch5 above this = MANUAL request

// What the RX outputs when it has no usable TX packets. ch14 (E-stop) stays
// LOW so that RC IN never blocks the autonomy stack while the handheld is off.
// ch6-13 sit at the middle: ArmDisarm aux (ch6/ch9 on the rover Cube) takes no
// action there, so the RC can never arm or disarm.
const uint16_t SAFE_CH[14] = {
  PWM_MID, PWM_MID, PWM_MID, PWM_MID,   // ch1-4  sticks neutral
  PWM_MODE_HOLD,                        // ch5    HOLD
  PWM_MID, PWM_MID, PWM_MID, PWM_MID,   // ch6-9
  PWM_MID, PWM_MID, PWM_MID, PWM_MID,   // ch10-13
  PWM_LOW                               // ch14   E-stop off
};
