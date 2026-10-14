/*
  ROVER//RC handheld (TX) - UNO R4 WiFi.

  Sticks + switches -> 14 RC channels -> UDP (RoverLink) -> rover R4 (RX),
  which outputs them as iBus into the Cube's RC IN. No MAVLink involved.

  WiFi: saved external network -> the rover's direct link ROVER-LINK-<id>
  -> own config AP ROVER-RC-<id>. Hold SW2 DOWN at power-on for the config AP.
  Hold BOTH 2-pos switches ON at power-on to run stick calibration.

  Pairing: TX and RX with the same bind ID (1-99) find each other; the TX
  broadcasts until its RX answers, then talks unicast. Sticks only pass once
  the throttle has been centred after (re)linking (interlock). The RC never
  arms or disarms: D2 only flips ch5 between HOLD and MANUAL.
*/
#include <EEPROM.h>
#include <WiFiS3.h>
#include <RoverLink.h>
#include "config.h"
#include "web_page.h"

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

// --- runtime state ---------------------------------------------------------------
WiFiUDP udp;

bool linked = false;        // our RX's stats are coming back
bool interlock = true;      // sticks held until throttle centred after (re)linking
IPAddress rxIp;
uint32_t lastStat = 0;
RlStat st;                  // last stats from the RX

struct Inputs { int lx, ly, rx, ry; int raw[4]; bool manual, estop; uint8_t sw1, sw2; } in;
uint16_t ch[RL_NCH];
uint16_t seq = 0;

uint32_t tLastInput = 0, tLastRx = 0, tLastSend = 0, tRate = 0, tStatus = 0, tLed = 0;
uint16_t sendCount = 0, txHz = 0;

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

  ch[0] = stickPwm(in.rx);                                    // ch1 steer
  ch[1] = stickPwm(in.ry);                                    // ch2 throttle
  ch[2] = stickPwm(in.ly);                                    // ch3 spare
  ch[3] = stickPwm(in.lx);                                    // ch4 spare
  ch[4] = in.manual ? PWM_MODE_MANUAL : PWM_MODE_HOLD;        // ch5 mode
  for (int i = 5; i < 13; i++) ch[i] = PWM_IDLE;             // ch6-13 middle (ch6/ch9 = ArmDisarm)
  ch[9] = in.sw2 == 0 ? PWM_HIGH : in.sw2 == 1 ? PWM_MID : PWM_LOW;  // ch10 spare
  ch[13] = in.estop ? PWM_HIGH : PWM_LOW;                     // ch14 e-stop
}

// -----------------------------------------------------------------------------
// Link
// -----------------------------------------------------------------------------
void sendRc() {
  RlRc p;
  p.type = RL_T_RC;
  p.id = rlCfg.id;
  p.seq = seq++;
  p.flags = (interlock ? 0 : RL_F_LIVE) | (in.estop ? RL_F_ESTOP : 0);
  memcpy(p.ch, ch, sizeof(p.ch));
  rlSeal(p);
  IPAddress dst = rlHasPeer() ? rlPeer() : (linked ? rxIp : rlBcast);
  udp.beginPacket(dst, RL_PORT);
  udp.write((const uint8_t*)&p, sizeof(p));
  udp.endPacket();
  sendCount++;
}

void pollUdp() {
  for (int k = 0; k < 3; k++) {
    int n = udp.parsePacket();
    if (n <= 0) return;
    uint8_t buf[64];
    int r = udp.read(buf, min(n, (int)sizeof(buf)));
    RlStat s;
    if (!rlCheck(buf, r, RL_T_STAT, rlCfg.id, s)) continue;
    st = s;
    lastStat = millis();
    if (!linked) {
      rxIp = udp.remoteIP();
      linked = true;
      interlock = true;   // never resume without re-checking the throttle
      Serial.print("LINK: RX id "); Serial.print(rlCfg.id); Serial.print(" at "); Serial.println(rxIp);
    }
  }
}

const char* interlockReason() {
  if (!interlock) return "";
  if (!linked) return "no rover receiver with this bind ID";
  if (in.ry != 0) return "throttle not centred";
  return "";
}

const char* rxStateName() {
  if (!linked) return "--";
  switch (st.state) {
    case RL_S_LIVE: return "LIVE";
    case RL_S_HOLD: return "HOLD";
    case RL_S_FAILSAFE: return "FAILSAFE";
    default: return "WAIT";
  }
}

// Called from the main loop and from inside any wait loop.
void controlTick() {
  uint32_t now = millis();
  if (now - tLastInput >= 10) { tLastInput = now; readInputs(); }
  if (now - tLastRx >= 20)    { tLastRx = now; pollUdp(); }

  if (linked && millis() - lastStat > LINK_TIMEOUT_MS) {   // millis(): pollUdp may have just set lastStat
    linked = false;
    interlock = true;
    Serial.println("LINK: lost RX, searching");
  }
  if (linked && interlock && in.ry == 0) {
    interlock = false;
    Serial.println("LINK: interlock released");
  }
  uint32_t period = (linked || rlHasPeer()) ? SEND_PERIOD_MS : SEARCH_PERIOD_MS;
  if (now - tLastSend >= period) { tLastSend = now; sendRc(); }
  if (now - tRate >= 1000) { tRate = now; txHz = sendCount; sendCount = 0; }
  if (now - tStatus >= 2000) {
    tStatus = now;
    char b[220];
    snprintf(b, sizeof(b), "STAT id %u %s | link=%s il=%s tx=%uHz rxLQ=%d%% rx=%s | raw lx%d ly%d rx%d ry%d -> steer %d thr %d mode %s estop %d",
             rlCfg.id, rlModeName(), linked ? "LINKED" : "search", interlock ? interlockReason() : "off", txHz,
             linked ? st.lq : -1, rxStateName(),
             in.raw[0], in.raw[1], in.raw[2], in.raw[3], in.rx, in.ry, in.manual ? "MANUAL" : "HOLD", in.estop);
    Serial.println(b);
  }
}

// -----------------------------------------------------------------------------
// LED
// -----------------------------------------------------------------------------
void ledTickTx() {
  if (!ledTick()) return;
  uint32_t now = millis();
  if (now - tLed < 100) return;
  tLed = now;
  char s;
  if (!linked)            s = '?';
  else if (interlock)     s = '!';
  else if (in.estop)      s = 'X';
  else                    s = st.manual ? 'M' : 'H';   // what the RX actually outputs
  ledMain(rlCfg.id, s, rlNetGlyph(), linked ? st.lq : -1);
}

// -----------------------------------------------------------------------------
// Calibration (from ~/code.ino, HID parts removed)
// -----------------------------------------------------------------------------
void runLiveCalibration() {
  Serial.println("CAL: centre sticks (5 s)");
  ledFlash();
  ledCenterSquare();
  delay(5000);
  calData.lx_center = analogRead(PIN_LX); calData.ly_center = analogRead(PIN_LY);
  calData.rx_center = analogRead(PIN_RX); calData.ry_center = analogRead(PIN_RY);
  calData.lx_min = calData.lx_max = calData.lx_center;
  calData.ly_min = calData.ly_max = calData.ly_center;
  calData.rx_min = calData.rx_max = calData.rx_center;
  calData.ry_min = calData.ry_max = calData.ry_center;

  Serial.println("CAL: sweep both sticks to every corner (15 s)");
  ledBorder();
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
  ledCheck();
  Serial.println("CAL: saved");
  delay(2000);
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

void loadCalibration() {
  EEPROM.get(EEPROM_ADDRESS, calData);
  calFromEeprom = calData.magic_number == VALID_CALIBRATION_MAGIC;
  if (!calFromEeprom) {
    Serial.println("WARN: no stick calibration in EEPROM, using full ADC range (boot with both 2-pos ON to calibrate)");
    calData.lx_min = calData.ly_min = calData.rx_min = calData.ry_min = DEF_MIN;
    calData.lx_center = calData.ly_center = calData.rx_center = calData.ry_center = DEF_MID;
    calData.lx_max = calData.ly_max = calData.rx_max = calData.ry_max = DEF_MAX;
  }
  // Re-measure centres at power-on (sticks at rest).
  recenter(PIN_LX, calData.lx_min, calData.lx_center, calData.lx_max, "LX");
  recenter(PIN_LY, calData.ly_min, calData.ly_center, calData.ly_max, "LY");
  recenter(PIN_RX, calData.rx_min, calData.rx_center, calData.rx_max, "RX");
  recenter(PIN_RY, calData.ry_min, calData.ry_center, calData.ry_max, "RY");
}

void printCalibration() {
  char b[200];
  snprintf(b, sizeof(b), "CAL %s  lx %d/%d/%d  ly %d/%d/%d  rx %d/%d/%d  ry %d/%d/%d  (min/centre/max)",
           calFromEeprom ? "eeprom" : "DEFAULT (not calibrated)",
           calData.lx_min, calData.lx_center, calData.lx_max, calData.ly_min, calData.ly_center, calData.ly_max,
           calData.rx_min, calData.rx_center, calData.rx_max, calData.ry_min, calData.ry_center, calData.ry_max);
  Serial.println(b);
}

// -----------------------------------------------------------------------------
// Web
// -----------------------------------------------------------------------------
void buildState() {
  size_t n = snprintf(rlDyn, sizeof(rlDyn), "{");
  n += rlNetJson(rlDyn + n, sizeof(rlDyn) - n);
  char rip[16] = "";
  if (linked) snprintf(rip, sizeof(rip), "%u.%u.%u.%u", rxIp[0], rxIp[1], rxIp[2], rxIp[3]);
  snprintf(rlDyn + n, sizeof(rlDyn) - n,
    ",\"lk\":%d,\"rip\":\"%s\",\"lq\":%d,\"rrs\":%d,\"rst\":\"%s\",\"rm\":%d,\"rch\":[%u,%u],"
    "\"il\":%d,\"ir\":\"%s\",\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,"
    "\"ch\":[%u,%u,%u,%u,%u,%u,%u],\"sw\":[%d,%d,%d,%d],\"raw\":[%d,%d,%d,%d],\"tx\":%u}",
    linked, rip, linked ? st.lq : -1, linked ? st.rssi : 0, rxStateName(), linked && st.manual,
    linked ? st.ch1 : 0, linked ? st.ch2 : 0,
    interlock, interlockReason(), in.lx, in.ly, in.rx, in.ry,
    ch[0], ch[1], ch[2], ch[3], ch[4], ch[9], ch[13],
    in.manual, in.estop, in.sw1, in.sw2, in.raw[0], in.raw[1], in.raw[2], in.raw[3], txHz);
  rlRespondJson(rlDyn);
}

void route(const char* path, const char* q) {
  if (!strcmp(path, "/")) { rlRespond("text/html; charset=utf-8", PAGE, strlen(PAGE)); return; }
  if (!strcmp(path, "/s")) { buildState(); return; }
  bool driving = linked && !interlock;
  if (rlRouteNet(path, q, !driving)) return;
}

// -----------------------------------------------------------------------------
// USB serial: c | stat | + the shared network commands (see rl_web.h)
// -----------------------------------------------------------------------------
void serialCmd(char* l) {
  if (!strcmp(l, "c")) { printCalibration(); return; }
  if (!strcmp(l, "stat")) { tStatus = 0; return; }
  if (rlSerialNet(l)) return;
  Serial.println("SER: c | stat | join <ssid><TAB><pass> | ap | id <n> | peer <ip|-> | ip <ip> [gw] [mask] | ip - | appass <p>");
}

void setup() {
  Serial.begin(115200);
  ledBegin();
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_ESTOP, INPUT_PULLUP);
  pinMode(PIN_SW1_UP, INPUT_PULLUP);
  pinMode(PIN_SW1_DN, INPUT_PULLUP);
  pinMode(PIN_SW2_UP, INPUT_PULLUP);
  pinMode(PIN_SW2_DN, INPUT_PULLUP);
  delay(300);
  Serial.println("\nROVER//RC TX boot");

  rlLoadSettings();
  if (digitalRead(PIN_MODE) == LOW && digitalRead(PIN_ESTOP) == LOW) runLiveCalibration();
  loadCalibration();
  readInputs();

  bool forceAp = in.sw2 == 2;
  if (forceAp) Serial.println("NET: SW2 held down -> config AP");
  rlNetBegin(RL_TX, forceAp);
  udp.begin(RL_PORT);
  rlWebBegin();
  Serial.println("ready: centre the throttle to release the interlock once linked");
}

// In the config AP the handheld can't reach the rover. While nobody is using
// the web page, look for ROVER-LINK-<id> every 20 s and reboot onto it once
// the rover is powered (a scan blocks ~3 s; nothing is being driven here).
void configApRetry() {
  static uint32_t tTry = 0;
  if (rlMode != NM_CONFIG_AP) return;
  uint32_t now = millis();
  if (now - tTry < 20000 || (rlWebLastRequest() && now - rlWebLastRequest() < 60000)) return;
  tTry = now;
  char link[33];
  rlLinkSsid(link, sizeof(link));
  if (!rlScan()) return;
  for (int i = 0; i < rlNetCount; i++)
    if (!strcmp(rlNets[i].ssid, link)) {
      Serial.print("NET: "); Serial.print(link); Serial.println(" is up -> rebooting to join it");
      rlRequestReboot(200);
      return;
    }
}

void loop() {
  static char sline[120];
  static uint8_t sl = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { sline[sl] = 0; if (sl) serialCmd(sline); sl = 0; }
    else if (sl < sizeof(sline) - 1) sline[sl++] = c;
  }
  controlTick();
  rlWebService(route, controlTick);
  ledTickTx();
  configApRetry();
  rlRebootTick();
}
