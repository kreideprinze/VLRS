#pragma once
// ---------------------------------------------------------------------------
// Settings (EEPROM) and WiFi bring-up with fallbacks.
//
//   RX (rover):    saved SSID  ->  own AP  ROVER-LINK-<id>   (192.168.5.1)
//   TX (handheld): saved SSID  ->  join    ROVER-LINK-<id>
//                              ->  own AP  ROVER-RC-<id>     (192.168.4.1, config only)
//
// Direct-link APs use WiFi channel 1/6/11 picked from the bind ID so that
// several pairs side by side interfere less.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFiS3.h>
#include "rl_leds.h"

#define RL_AP_PASS_DEFAULT  "roverrc123"
#define RL_JOIN_TIMEOUT_MS  15000

enum RlRole : uint8_t { RL_TX = 0, RL_RX = 1 };
enum RlNetMode : uint8_t { NM_NONE = 0, NM_EXT, NM_DIRECT_STA, NM_DIRECT_AP, NM_CONFIG_AP };

struct RlSettings {
  uint32_t magic;
  uint8_t  id;            // bind ID 1..99
  char     ssid[33];      // external network ("" = none)
  char     pass[65];
  char     apPass[33];    // ROVER-LINK / ROVER-RC password, must match on TX and RX
  uint8_t  peerIp[4];     // 0.0.0.0 = discover by broadcast
  uint8_t  staticIp[4];   // 0.0.0.0 = DHCP (external network only)
  uint8_t  gw[4];
  uint8_t  mask[4];
  uint8_t  lastAp;        // 1 = last boot ended as an AP (tower), 2 = as STA (fan)
  uint8_t  espStatic;     // 1 = a static IP was pushed to the ESP32 and may still be active
  uint8_t  rxOut;         // RX only: 0 = PPM, 1 = iBus, 2 = SBUS (inverted, like ExpressLRS)
};

struct RlNetEntry { char ssid[33]; int8_t rssi; uint8_t enc; };

extern RlSettings rlCfg;
extern RlNetMode  rlMode;
extern IPAddress  rlMyIp, rlBcast;
extern char       rlFwVer[16];
extern bool       rlFwOld;
extern RlNetEntry rlNets[12];
extern int        rlNetCount;

void rlLoadSettings();
void rlSaveSettings();
void rlLinkSsid(char* out, size_t n);     // "ROVER-LINK-07"
void rlConfigSsid(char* out, size_t n);   // "ROVER-RC-07"
uint8_t rlLinkChannel();
const char* rlModeName();                 // EXT / LINK / AP / CFG
const char* rlSsidNow();
bool rlIsAp();
RlNetGlyph rlNetGlyph();
bool rlHasPeer();
IPAddress rlPeer();

void rlNetBegin(RlRole role, bool forceAp);   // blocking; sets up the LED sequence
bool rlScan();                                // refresh rlNets; false = scan failed, cache kept
