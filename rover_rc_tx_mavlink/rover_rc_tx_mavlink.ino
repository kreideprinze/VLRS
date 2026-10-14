/*
  ROVER//RC - UNO R4 WiFi handheld RC for ArduRover via MAVProxy.

  Sticks + switches -> MAVLink RC_CHANNELS_OVERRIDE (sysid 200) over UDP:14560.
  On the laptop, rc_link.py receives it and hands it to MAVProxy -> Cube.

  WiFi: joins the network saved from the web page; if there is none, or it
  does not come up within STA_JOIN_TIMEOUT_MS, it starts its own AP
  "ROVER-RC" (192.168.4.1). Hold SW2 DOWN at power-on to force AP.
  Hold BOTH 2-pos switches ON at power-on to run stick calibration.

  Discovery: until the vehicle's heartbeat comes back, the RC broadcasts its
  heartbeat on the subnet; the first vehicle heartbeat locks it to the
  sender (unicast). Overrides are only sent while locked and after the
  interlock (throttle centred) has been satisfied. The RC never arms or
  disarms: the 2-pos switch on D2 only flips ch5 between HOLD and MANUAL.
*/
#include <EEPROM.h>
#include <WiFiS3.h>
#include "config.h"
#include "leds.h"
#include "web_page.h"
#include "src/mavlink/common/mavlink.h"

// --- stick calibration (same layout/magic as ~/code.ino, so it carries over)
struct CalibrationData {
  uint32_t magic_number;
  int lx_min, lx_center, lx_max;
  int ly_min, ly_center, ly_max;
  int rx_min, rx_center, rx_max;
  int ry_min, ry_center, ry_max;
};
CalibrationData calData;
bool calFromEeprom = false;
const int EEPROM_ADDRESS = 0;
const uint32_t VALID_CALIBRATION_MAGIC = 0x4A4F5953;

// --- persistent settings ------------------------------------------------------
enum NetMode : uint8_t { NET_NONE = 0, NET_AP = 1, NET_STA = 2 };
struct Settings {
  uint32_t magic;
  char ssid[33];
  char pass[65];
  uint8_t gcsIp[4];     // 0.0.0.0 = auto-discover
  char apPass[33];
  uint8_t lastMode;     // NetMode the RC ended up in last boot
};
Settings cfg;
const int SETTINGS_ADDR = 128;
const uint32_t SETTINGS_MAGIC = 0x52435331;  // "RCS1"

void saveSettings() { EEPROM.put(SETTINGS_ADDR, cfg); }

void loadSettings() {
  EEPROM.get(SETTINGS_ADDR, cfg);
  if (cfg.magic != SETTINGS_MAGIC) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = SETTINGS_MAGIC;
    strcpy(cfg.apPass, AP_PASS_DEFAULT);
    saveSettings();
  }
  cfg.ssid[32] = 0; cfg.pass[64] = 0; cfg.apPass[32] = 0;
  if (strlen(cfg.apPass) < 8) strcpy(cfg.apPass, AP_PASS_DEFAULT);
}

// --- runtime state ---------------------------------------------------------------
NetMode netMode = NET_NONE;
IPAddress myIp, bcastIp, gcsIp;
uint16_t gcsPort = MAV_PORT;
char fwVer[16] = "?";
bool fwOld = false;

WiFiUDP udp;
WiFiServer server(80);

bool locked = false;        // vehicle heartbeat is coming back to us
bool interlock = true;      // overrides held until throttle centred + ARM off
bool vehArmed = false;
uint32_t vehMode = 0xFFFFFFFF;
uint32_t lastVehHb = 0;

struct Inputs { int lx, ly, rx, ry; int raw[4]; bool manual, estop; uint8_t sw1, sw2; } in;
uint16_t chOut[7];          // ch1..6, ch14 as last computed (for UI)
uint32_t tStatus = 0;
uint32_t rxPk = 0, rxBytes = 0, rxMsgs = 0, rxHb = 0;   // receive diagnostics

uint32_t tLastInput = 0, tLastRx = 0, tLastHb = 0, tLastOv = 0, tRate = 0;
uint16_t ovCount = 0, txHz = 0;

struct Net { char ssid[33]; int8_t rssi; uint8_t enc; };
Net nets[12];
int netCount = 0;

uint32_t rebootAt = 0;

// -----------------------------------------------------------------------------
// Inputs
// -----------------------------------------------------------------------------
int axisNorm(int raw, int mn, int mid, int mx, bool inv) {
  long v;
  if (raw <= mid) v = (mid > mn) ? map(raw, mn, mid, -1000, 0) : 0;
  else            v = (mx > mid) ? map(raw, mid, mx, 0, 1000) : 0;
  v = constrain(v, -1000, 1000);
  const long dz = (long)(STICK_DEADZONE * 1000);
  if (labs(v) <= dz) v = 0;
  else v = (v > 0 ? 1 : -1) * (labs(v) - dz) * 1000 / (1000 - dz);
  return inv ? -v : v;
}

uint8_t read3pos(int pinUp, int pinDn) {
  bool up = digitalRead(pinUp) == LOW, dn = digitalRead(pinDn) == LOW;
  if (up && !dn) return 0;
  if (dn && !up) return 2;
  return 1;
}

uint16_t stickPwm(int v) { return PWM_MID + (long)v * PWM_SPAN / 1000; }

void readInputs() {
  in.raw[0] = analogRead(PIN_LX); in.raw[1] = analogRead(PIN_LY);
  in.raw[2] = analogRead(PIN_RX); in.raw[3] = analogRead(PIN_RY);
  in.lx = axisNorm(in.raw[0], calData.lx_min, calData.lx_center, calData.lx_max, INVERT_LX);
  in.ly = axisNorm(in.raw[1], calData.ly_min, calData.ly_center, calData.ly_max, INVERT_LY);
  in.rx = axisNorm(in.raw[2], calData.rx_min, calData.rx_center, calData.rx_max, INVERT_RX);
  in.ry = axisNorm(in.raw[3], calData.ry_min, calData.ry_center, calData.ry_max, INVERT_RY);
  if (SWAP_RIGHT_STICK) { int t = in.rx; in.rx = in.ry; in.ry = t; }   // in.rx = steer, in.ry = throttle
  in.manual = digitalRead(PIN_MODE) == LOW;
  in.estop = digitalRead(PIN_ESTOP) == LOW;
  in.sw1 = read3pos(PIN_SW1_UP, PIN_SW1_DN);
  in.sw2 = read3pos(PIN_SW2_UP, PIN_SW2_DN);

  chOut[0] = stickPwm(in.rx);                                   // ch1 steer
  chOut[1] = stickPwm(in.ry);                                   // ch2 throttle
  chOut[2] = stickPwm(in.ly);                                   // ch3 spare
  chOut[3] = stickPwm(in.lx);                                   // ch4 spare
  chOut[4] = in.manual ? PWM_MODE_MANUAL : PWM_MODE_HOLD;      // ch5 mode
  chOut[5] = in.sw2 == 0 ? PWM_HIGH : in.sw2 == 1 ? PWM_MID : PWM_LOW;  // ch6 spare
  chOut[6] = in.estop ? PWM_HIGH : PWM_LOW;                     // ch14 e-stop
}

// -----------------------------------------------------------------------------
// MAVLink
// -----------------------------------------------------------------------------
void sendMav(const mavlink_message_t& msg) {
  uint8_t buf[MAVLINK_MAX_PACKET_LEN];
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  bool manual = cfg.gcsIp[0] != 0;
  IPAddress dst = manual ? IPAddress(cfg.gcsIp[0], cfg.gcsIp[1], cfg.gcsIp[2], cfg.gcsIp[3])
                         : (locked ? gcsIp : bcastIp);
  uint16_t port = (!manual && locked) ? gcsPort : MAV_PORT;
  udp.beginPacket(dst, port);
  udp.write(buf, len);
  udp.endPacket();
}

void sendHeartbeat() {
  mavlink_message_t msg;
  mavlink_msg_heartbeat_pack(RC_SYSID, RC_COMPID, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
  sendMav(msg);
}

void sendOverride() {
  mavlink_message_t msg;
  const uint16_t IGN = UINT16_MAX;   // ch1-8: "leave this channel alone"
  mavlink_msg_rc_channels_override_pack(RC_SYSID, RC_COMPID, &msg, VEHICLE_SYSID, VEHICLE_COMPID,
      chOut[0], chOut[1], chOut[2], chOut[3], chOut[4], chOut[5], IGN, IGN,
      0, 0, 0, 0, 0, chOut[6], 0, 0, 0, 0);   // ch9..18: 0 = ignore (ch9 arm never touched)
  sendMav(msg);
  ovCount++;
}

void handleVehicleHeartbeat(const mavlink_message_t& m) {
  mavlink_heartbeat_t hb;
  mavlink_msg_heartbeat_decode(&m, &hb);
  if (hb.autopilot == MAV_AUTOPILOT_INVALID) return;   // a GCS/companion, not the autopilot
  vehArmed = hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED;
  vehMode = hb.custom_mode;
  lastVehHb = millis();
  if (!locked) {
    gcsIp = udp.remoteIP();
    gcsPort = udp.remotePort();
    locked = true;
    interlock = true;   // never resume control without re-checking the throttle
    Serial.print("LINK: locked to "); Serial.print(gcsIp); Serial.print(':'); Serial.println(gcsPort);
  }
}

void pollUdp() {
  for (int k = 0; k < 3; k++) {       // packet budget per tick
    int n = udp.parsePacket();
    if (n <= 0) return;
    uint8_t buf[96];
    int r = udp.read(buf, min(n, (int)sizeof(buf)));
    rxPk++;
    if (r > 0) rxBytes += r;
    mavlink_message_t m;
    mavlink_status_t st;
    for (int i = 0; i < r; i++)
      if (mavlink_parse_char(MAVLINK_COMM_1, buf[i], &m, &st)) {
        rxMsgs++;
        if (m.msgid == MAVLINK_MSG_ID_HEARTBEAT && m.sysid == VEHICLE_SYSID) { rxHb++; handleVehicleHeartbeat(m); }
      }
  }
}

const char* interlockReason() {
  if (!interlock) return "";
  if (!locked) return "no link to rover (is start_bridge.sh running?)";
  if (in.ry != 0) return "throttle not centred";
  return "";
}

// Called from the main loop and from inside any wait loop, so control
// traffic keeps flowing while the web server is busy.
void controlTick() {
  uint32_t now = millis();
  if (now - tLastInput >= 20) { tLastInput = now; readInputs(); }
  if (now - tLastRx >= 20)    { tLastRx = now; pollUdp(); }

  if (locked && millis() - lastVehHb > LINK_TIMEOUT_MS) {   // not "now": pollUdp may have just set lastVehHb
    locked = false;
    interlock = true;
    Serial.println("LINK: lost vehicle heartbeat, searching");
  }
  if (locked && interlock && in.ry == 0) {
    interlock = false;
    Serial.println("LINK: interlock released, sending overrides");
  }
  if (now - tLastHb >= HEARTBEAT_PERIOD_MS) { tLastHb = now; sendHeartbeat(); }
  if (locked && !interlock && now - tLastOv >= OVERRIDE_PERIOD_MS) { tLastOv = now; sendOverride(); }
  if (now - tRate >= 1000) { tRate = now; txHz = ovCount; ovCount = 0; }
  if (now - tStatus >= 2000) {
    tStatus = now;
    char b[220];
    snprintf(b, sizeof(b), "STAT rx pk=%lu B=%lu msg=%lu hb=%lu | link=%s il=%s tx=%uHz raw lx%d ly%d rx%d ry%d -> steer %d thr %d  mode %s estop %d",
             rxPk, rxBytes, rxMsgs, rxHb,
             locked ? "LOCKED" : "search", interlock ? interlockReason() : "off", txHz,
             in.raw[0], in.raw[1], in.raw[2], in.raw[3], in.rx, in.ry, in.manual ? "MANUAL" : "HOLD", in.estop);
    Serial.println(b);
  }
}

const char* roverModeName(uint32_t m) {
  switch (m) {
    case 0: return "MANUAL";  case 1: return "ACRO";    case 3: return "STEERING";
    case 4: return "HOLD";    case 5: return "LOITER";  case 6: return "FOLLOW";
    case 7: return "SIMPLE";  case 8: return "DOCK";    case 9: return "CIRCLE";
    case 10: return "AUTO";   case 11: return "RTL";    case 12: return "SMART_RTL";
    case 15: return "GUIDED"; case 16: return "INIT";   case 0xFFFFFFFF: return "--";
    default: return "MODE?";
  }
}

// -----------------------------------------------------------------------------
// LED status view
// -----------------------------------------------------------------------------
enum Disp { D_ANIM, D_SCROLL, D_STATUS };
Disp disp = D_STATUS;
uint32_t dispUntil = 0, tScroll = 0, tDisp = 0;
int scrollX = 12;
char scrollText[28];

void startScroll() {
  snprintf(scrollText, sizeof(scrollText), "%s %d.%d.%d.%d", netMode == NET_AP ? "AP" : "STA",
           myIp[0], myIp[1], myIp[2], myIp[3]);
  scrollX = 12;
  disp = D_SCROLL;
}

void displayTick() {
  uint32_t now = millis();
  if (disp == D_ANIM) {
    if (now >= dispUntil) startScroll();
    return;
  }
  if (disp == D_SCROLL) {
    if (now - tScroll < 70) return;
    tScroll = now;
    Frame f; fbClear(f);
    fbText(f, scrollText, scrollX);
    ledShow(f);
    if (--scrollX < -textWidth(scrollText)) disp = D_STATUS;
    return;
  }
  if (now - tDisp < 100) return;
  tDisp = now;
  Frame f; fbClear(f);
  if (netMode == NET_AP) fbGlyph(f, G_TOWER, 5, 0);
  else                   fbFan(f, 0, true, true, true);
  const uint8_t* g;
  if (!locked)                 g = ((now / 300) & 1) ? G_DOT : nullptr;
  else if (interlock)          g = G_BANG;
  else if (in.estop)           g = G_X;
  else if (vehMode == 4)       g = G_HOLD;
  else if (vehArmed)           g = G_FILL;
  else                         g = G_BOX;
  if (g) fbGlyph(f, g, 5, 7);
  ledShow(f);
}

// -----------------------------------------------------------------------------
// Calibration (from ~/code.ino, HID parts removed)
// -----------------------------------------------------------------------------
void runLiveCalibration() {
  Serial.println("CAL: centre sticks (5 s)");
  flashMatrix();
  drawCenterSquare();
  delay(5000);
  calData.lx_center = analogRead(PIN_LX); calData.ly_center = analogRead(PIN_LY);
  calData.rx_center = analogRead(PIN_RX); calData.ry_center = analogRead(PIN_RY);
  calData.lx_min = calData.lx_max = calData.lx_center;
  calData.ly_min = calData.ly_max = calData.ly_center;
  calData.rx_min = calData.rx_max = calData.rx_center;
  calData.ry_min = calData.ry_max = calData.ry_center;

  Serial.println("CAL: sweep both sticks to every corner (15 s)");
  drawBorder();
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    int v;
    v = analogRead(PIN_LX); calData.lx_min = min(calData.lx_min, v); calData.lx_max = max(calData.lx_max, v);
    v = analogRead(PIN_LY); calData.ly_min = min(calData.ly_min, v); calData.ly_max = max(calData.ly_max, v);
    v = analogRead(PIN_RX); calData.rx_min = min(calData.rx_min, v); calData.rx_max = max(calData.rx_max, v);
    v = analogRead(PIN_RY); calData.ry_min = min(calData.ry_min, v); calData.ry_max = max(calData.ry_max, v);
    delay(10);
  }
  calData.magic_number = VALID_CALIBRATION_MAGIC;
  EEPROM.put(EEPROM_ADDRESS, calData);
  drawCheckmark();
  Serial.println("CAL: saved");
  delay(2000);
}

void loadCalibration() {
  EEPROM.get(EEPROM_ADDRESS, calData);
  calFromEeprom = calData.magic_number == VALID_CALIBRATION_MAGIC;
  if (!calFromEeprom) {
    Serial.println("WARN: no stick calibration in EEPROM, using calibration.json ranges (boot with both 2-pos ON to calibrate)");
    calData.lx_min = calData.ly_min = calData.rx_min = calData.ry_min = DEF_MIN;
    calData.lx_center = calData.ly_center = calData.rx_center = calData.ry_center = DEF_MID;
    calData.lx_max = calData.ly_max = calData.rx_max = calData.ry_max = DEF_MAX;
  }
  // Re-measure centres at power-on (sticks at rest). A reading far from the
  // stored centre means the stick is being held, so keep the stored one.
  recenter(PIN_LX, calData.lx_min, calData.lx_center, calData.lx_max, "LX");
  recenter(PIN_LY, calData.ly_min, calData.ly_center, calData.ly_max, "LY");
  recenter(PIN_RX, calData.rx_min, calData.rx_center, calData.rx_max, "RX");
  recenter(PIN_RY, calData.ry_min, calData.ry_center, calData.ry_max, "RY");
}

void recenter(int pin, int mn, int& mid, int mx, const char* name) {
  long sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(pin); delay(2); }
  int v = sum / 16;
  int lo = mn + (mx - mn) / 5, hi = mx - (mx - mn) / 5;
  Serial.print("CAL: "); Serial.print(name); Serial.print(" rest "); Serial.print(v);
  Serial.print(" (range "); Serial.print(mn); Serial.print('-'); Serial.print(mx); Serial.print(", stored centre ");
  Serial.print(mid);
  if (v >= lo && v <= hi) { mid = v; Serial.println(") -> centre updated"); }
  else Serial.println(") -> stick looks deflected, keeping stored centre");
}

// -----------------------------------------------------------------------------
// WiFi bring-up
// -----------------------------------------------------------------------------
bool versionOlder(const char* a, const char* b) {
  int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
  sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
  sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
  if (a1 != b1) return a1 < b1;
  if (a2 != b2) return a2 < b2;
  return a3 < b3;
}

void scanToCache() {
  int n = WiFi.scanNetworks();
  if (n <= 0) return;            // keep the previous list
  netCount = min(n, 12);
  for (int i = 0; i < netCount; i++) {
    strncpy(nets[i].ssid, WiFi.SSID(i), 32); nets[i].ssid[32] = 0;
    nets[i].rssi = WiFi.RSSI(i);
    nets[i].enc = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
}

bool waitForIp(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    myIp = WiFi.localIP();
    if (myIp != IPAddress(0, 0, 0, 0)) return true;
    delay(200);
  }
  return false;
}

void netStart(bool forceAp) {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("FATAL: WiFi module not responding");
    Frame f; fbClear(f); fbGlyph(f, G_X, 5, 3); ledShow(f);
    while (true) delay(1000);
  }
  strncpy(fwVer, WiFi.firmwareVersion(), sizeof(fwVer) - 1);
  fwOld = versionOlder(fwVer, WIFI_FIRMWARE_LATEST_VERSION);
  if (fwOld) {
    Serial.print("WARN: WiFi firmware "); Serial.print(fwVer);
    Serial.print(" older than "); Serial.println(WIFI_FIRMWARE_LATEST_VERSION);
  }

  bool triedSta = false;
  if (!forceAp && cfg.ssid[0]) {
    triedSta = true;
    Serial.print("NET: joining '"); Serial.print(cfg.ssid); Serial.println("'");
    playConnecting();
    WiFi.setTimeout(STA_JOIN_TIMEOUT_MS);
    if (WiFi.begin(cfg.ssid, cfg.pass) == WL_CONNECTED && waitForIp(5000)) {
      netMode = NET_STA;
      scanToCache();
    } else {
      Serial.println("NET: join failed, falling back to AP");
      WiFi.disconnect();
    }
  }
  if (netMode != NET_STA) {
    scanToCache();   // cache nearby networks for the web page before going AP
    if (WiFi.beginAP(AP_SSID, cfg.apPass) != WL_AP_LISTENING) {
      Serial.println("FATAL: could not start AP");
      Frame f; fbClear(f); fbGlyph(f, G_X, 5, 3); ledShow(f);
      while (true) delay(1000);
    }
    netMode = NET_AP;
    waitForIp(3000);
  }

  if (netMode == NET_AP) {
    bcastIp = IPAddress(myIp[0], myIp[1], myIp[2], 255);
  } else {
    IPAddress mask = WiFi.subnetMask();
    if (mask == IPAddress(0, 0, 0, 0)) mask = IPAddress(255, 255, 255, 0);
    for (int i = 0; i < 4; i++) bcastIp[i] = myIp[i] | (uint8_t)~mask[i];
  }

  Serial.print("NET: "); Serial.print(netMode == NET_AP ? "AP '" AP_SSID "'" : "STA '");
  if (netMode == NET_STA) { Serial.print(cfg.ssid); Serial.print("'"); }
  Serial.print("  ip "); Serial.print(myIp); Serial.print("  bcast "); Serial.println(bcastIp);
  if (netMode == NET_AP) { Serial.print("NET: AP password: "); Serial.println(cfg.apPass); }

  // Mode-change animation, then the IP scrolls once.
  uint32_t animMs = 0;
  if (netMode == NET_AP && (triedSta || cfg.lastMode == NET_STA)) animMs = playStaToAp();
  else if (netMode == NET_STA && cfg.lastMode == NET_AP)          animMs = playApToSta();
  if (animMs) { disp = D_ANIM; dispUntil = millis() + animMs; }
  else startScroll();

  if (cfg.lastMode != netMode) { cfg.lastMode = netMode; saveSettings(); }

  udp.begin(MAV_PORT);
  server.begin();
}

// -----------------------------------------------------------------------------
// Web server: one request at a time, response written in chunks so the
// control loop keeps running between them.
// -----------------------------------------------------------------------------
WiFiClient http;
bool httpBusy = false;
const char* hBody = nullptr;
size_t hLeft = 0;
char hHdr[160];
bool hHdrSent = false;
uint32_t hDeadline = 0, tAccept = 0;
char dyn[1100];
bool rebootAfterResponse = false;

int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }

// Copy url-decoded value of key from query into out. Returns false if absent.
bool qparam(const char* q, const char* key, char* out, size_t outLen) {
  size_t kl = strlen(key);
  while (q && *q) {
    if (!strncmp(q, key, kl) && q[kl] == '=') {
      const char* p = q + kl + 1;
      size_t n = 0;
      while (*p && *p != '&' && n < outLen - 1) {
        if (*p == '+') { out[n++] = ' '; p++; }
        else if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { out[n++] = hexv(p[1]) * 16 + hexv(p[2]); p += 3; }
        else out[n++] = *p++;
      }
      out[n] = 0;
      return true;
    }
    q = strchr(q, '&');
    if (q) q++;
  }
  return false;
}

// JSON-escape s into dst (always terminated).
void jsonStr(char* dst, size_t len, const char* s) {
  size_t n = 0;
  for (; *s && n + 7 < len; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') { dst[n++] = '\\'; dst[n++] = c; }
    else if (c >= 0x20) dst[n++] = c;
  }
  dst[n] = 0;
}

void respond(const char* type, const char* body, size_t len) {
  snprintf(hHdr, sizeof(hHdr),
           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
           type, (unsigned)len);
  hBody = body; hLeft = len; hHdrSent = false;
}
void respondJson(const char* s) { strncpy(dyn, s, sizeof(dyn) - 1); dyn[sizeof(dyn) - 1] = 0; respond("application/json", dyn, strlen(dyn)); }

void buildState() {
  char ss[70], gc[20] = "";
  jsonStr(ss, sizeof(ss), netMode == NET_AP ? AP_SSID : cfg.ssid);
  bool manual = cfg.gcsIp[0] != 0;
  if (manual) snprintf(gc, sizeof(gc), "%d.%d.%d.%d", cfg.gcsIp[0], cfg.gcsIp[1], cfg.gcsIp[2], cfg.gcsIp[3]);
  else if (locked) snprintf(gc, sizeof(gc), "%d.%d.%d.%d", gcsIp[0], gcsIp[1], gcsIp[2], gcsIp[3]);
  int rssi = netMode == NET_STA ? WiFi.RSSI() : 0;
  snprintf(dyn, sizeof(dyn),
    "{\"m\":\"%s\",\"ss\":\"%s\",\"ip\":\"%d.%d.%d.%d\",\"rs\":%d,\"lk\":%d,\"gc\":\"%s\",\"gm\":%d,"
    "\"ar\":%d,\"md\":\"%s\",\"il\":%d,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,"
    "\"ch\":[%u,%u,%u,%u,%u,%u,%u],\"sw\":[%d,%d,%d,%d],\"raw\":[%d,%d,%d,%d],\"ir\":\"%s\",\"tx\":%u,\"fw\":\"%s\",\"fo\":%d}",
    netMode == NET_AP ? "AP" : "STA", ss, myIp[0], myIp[1], myIp[2], myIp[3], rssi, locked, gc, manual,
    locked && vehArmed, locked ? roverModeName(vehMode) : "--", interlock, in.lx, in.ly, in.rx, in.ry,
    chOut[0], chOut[1], chOut[2], chOut[3], chOut[4], chOut[5], chOut[6],
    in.manual, in.estop, in.sw1, in.sw2, in.raw[0], in.raw[1], in.raw[2], in.raw[3],
    interlockReason(), txHz, fwVer, fwOld);
  respond("application/json", dyn, strlen(dyn));
}

void buildScan() {
  if (locked && vehArmed) {
    respondJson("{\"e\":\"disarm the rover first - a scan stalls the link for ~3 s\"}");
    return;
  }
  bool cached = false;
  int before = netCount;
  netCount = 0;
  scanToCache();
  if (netCount == 0) { netCount = before; cached = true; }
  size_t n = snprintf(dyn, sizeof(dyn), "{\"c\":%d,\"n\":[", cached);
  for (int i = 0; i < netCount && n < sizeof(dyn) - 90; i++) {
    char ss[70];
    jsonStr(ss, sizeof(ss), nets[i].ssid);
    n += snprintf(dyn + n, sizeof(dyn) - n, "%s[\"%s\",%d,%d]", i ? "," : "", ss, nets[i].rssi, nets[i].enc);
  }
  snprintf(dyn + n, sizeof(dyn) - n, "]}");
  respond("application/json", dyn, strlen(dyn));
}

void route(const char* path, const char* q) {
  char a[70], b[70];
  if (!strcmp(path, "/")) { respond("text/html; charset=utf-8", PAGE, strlen(PAGE)); return; }
  if (!strcmp(path, "/s")) { buildState(); return; }
  if (!strcmp(path, "/scan")) { buildScan(); return; }
  if (!strcmp(path, "/join")) {
    if (!qparam(q, "s", a, 33) || !a[0]) { respondJson("{\"e\":\"missing ssid\"}"); return; }
    if (!qparam(q, "p", b, 65)) b[0] = 0;
    if (b[0] && strlen(b) < 8) { respondJson("{\"e\":\"WPA password must be 8+ chars\"}"); return; }
    strcpy(cfg.ssid, a); strcpy(cfg.pass, b); saveSettings();
    Serial.print("WEB: join '"); Serial.print(cfg.ssid); Serial.println("' -> rebooting");
    respondJson("{\"ok\":1}");
    rebootAfterResponse = true;
    return;
  }
  if (!strcmp(path, "/ap")) {
    cfg.ssid[0] = 0; cfg.pass[0] = 0; saveSettings();
    Serial.println("WEB: forget network -> rebooting into AP");
    respondJson("{\"ok\":1}");
    rebootAfterResponse = true;
    return;
  }
  if (!strcmp(path, "/gcs")) {
    qparam(q, "ip", a, 20);
    IPAddress ip;
    if (!a[0]) memset(cfg.gcsIp, 0, 4);
    else if (ip.fromString(a) && ip[0] != 0) for (int i = 0; i < 4; i++) cfg.gcsIp[i] = ip[i];
    else { respondJson("{\"e\":\"bad IP\"}"); return; }
    saveSettings();
    respondJson("{\"ok\":1}");
    return;
  }
  if (!strcmp(path, "/appass")) {
    if (!qparam(q, "p", a, 33) || strlen(a) < 8 || strlen(a) > 32) { respondJson("{\"e\":\"8-32 chars\"}"); return; }
    strcpy(cfg.apPass, a); saveSettings();
    respondJson("{\"ok\":1}");
    return;
  }
  snprintf(hHdr, sizeof(hHdr), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  hBody = nullptr; hLeft = 0; hHdrSent = false;
}

void httpFinish() {
  http.stop();
  httpBusy = false;
  if (rebootAfterResponse) rebootAt = millis() + 600;
}

void serviceHttp() {
  uint32_t now = millis();
  if (!httpBusy) {
    if (now - tAccept < 30) return;
    tAccept = now;
    WiFiClient c = server.available();
    if (!c) return;
    http = c;

    // request line
    char line[300];
    size_t n = 0;
    uint32_t dl = millis() + 300;
    while (millis() < dl && n < sizeof(line) - 1) {
      controlTick();
      if (http.available() <= 0) continue;
      int ch = http.read();
      if (ch < 0 || ch == '\r') continue;
      if (ch == '\n') break;
      line[n++] = ch;
    }
    line[n] = 0;
    // drain the rest of the headers so closing doesn't RST the browser
    uint8_t junk[128];
    dl = millis() + 150;
    while (millis() < dl) {
      int a = http.available();
      if (a <= 0) break;
      http.read(junk, min(a, (int)sizeof(junk)));
    }

    char* sp1 = strchr(line, ' ');
    char* sp2 = sp1 ? strchr(sp1 + 1, ' ') : nullptr;
    if (!sp1 || !sp2) { http.stop(); return; }
    *sp2 = 0;
    char* path = sp1 + 1;
    char* q = strchr(path, '?');
    if (q) *q++ = 0;
    route(path, q ? q : "");
    httpBusy = true;
    hDeadline = millis() + 5000;
    return;
  }

  if (!http.connected() || now > hDeadline) { httpFinish(); return; }
  if (!hHdrSent) { http.write((const uint8_t*)hHdr, strlen(hHdr)); hHdrSent = true; return; }
  if (hLeft) {
    size_t chunk = min(hLeft, (size_t)256);
    http.write((const uint8_t*)hBody, chunk);
    hBody += chunk; hLeft -= chunk;
    return;
  }
  httpFinish();
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  matrix.begin();
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_ESTOP, INPUT_PULLUP);
  pinMode(PIN_SW1_UP, INPUT_PULLUP);
  pinMode(PIN_SW1_DN, INPUT_PULLUP);
  pinMode(PIN_SW2_UP, INPUT_PULLUP);
  pinMode(PIN_SW2_DN, INPUT_PULLUP);
  delay(300);
  Serial.println("\nROVER//RC boot");

  loadSettings();
  if (digitalRead(PIN_MODE) == LOW && digitalRead(PIN_ESTOP) == LOW) runLiveCalibration();
  loadCalibration();
  readInputs();

  bool forceAp = in.sw2 == 2;
  if (forceAp) Serial.println("NET: SW2 held down -> forcing AP");
  netStart(forceAp);
  Serial.println("ready: centre the throttle to release the interlock once linked");
}

void printCalibration() {
  char b[200];
  snprintf(b, sizeof(b), "CAL %s  lx %d/%d/%d  ly %d/%d/%d  rx %d/%d/%d  ry %d/%d/%d  (min/centre/max)",
           calFromEeprom ? "eeprom" : "DEFAULT (not calibrated)",
           calData.lx_min, calData.lx_center, calData.lx_max, calData.ly_min, calData.ly_center, calData.ly_max,
           calData.rx_min, calData.rx_center, calData.rx_max, calData.ry_min, calData.ry_center, calData.ry_max);
  Serial.println(b);
}

// USB serial commands (newline-terminated):
//   c                       print stick calibration
//   join <ssid><TAB><pass>  save network and reboot into STA (same as the web page)
//   ap                      forget network, reboot into AP
void serialCmd(char* l) {
  if (!strcmp(l, "c")) { printCalibration(); return; }
  if (!strcmp(l, "ap")) {
    cfg.ssid[0] = 0; cfg.pass[0] = 0; saveSettings();
    Serial.println("SER: forget network -> rebooting into AP");
    rebootAt = millis() + 300;
    return;
  }
  if (!strncmp(l, "join ", 5)) {
    char* ssid = l + 5;
    char* tab = strchr(ssid, '\t');
    const char* pass = "";
    if (tab) { *tab = 0; pass = tab + 1; }
    if (!*ssid || strlen(ssid) > 32 || strlen(pass) > 64) { Serial.println("SER: bad ssid/pass"); return; }
    strcpy(cfg.ssid, ssid); strcpy(cfg.pass, pass); saveSettings();
    Serial.print("SER: join '"); Serial.print(cfg.ssid); Serial.println("' -> rebooting");
    rebootAt = millis() + 300;
    return;
  }
  Serial.println("SER: commands: c | join <ssid><TAB><pass> | ap");
}

void loop() {
  static char sline[120];
  static uint8_t sl = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { sline[sl] = 0; if (sl) serialCmd(sline); sl = 0; }
    else if (sl < sizeof(sline) - 1) sline[sl++] = ch;
  }
  controlTick();
  serviceHttp();
  displayTick();
  if (rebootAt && millis() > rebootAt) NVIC_SystemReset();
}
