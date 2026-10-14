/*
  ROVER//RC rover receiver (RX) - UNO R4 WiFi.

  Receives RoverLink packets from the handheld with the same bind ID and
  outputs them as iBus (115200 8N1, 14 channels) on D1 into the Cube's RC IN.
  ArduPilot auto-detects iBus (RC_PROTOCOLS includes it).

  WiFi: saved external network -> own AP ROVER-LINK-<id> (192.168.5.1).

  Failsafe (works without any ArduPilot failsafe params):
    never linked          -> safe channels + iBus failsafe flag
    no packet > 300 ms     -> sticks neutral, ch5 HOLD
    no packet > 1.5 s      -> also iBus failsafe flag
  After any HOLD/failsafe (and at boot) ch5 stays HOLD until the handheld's
  mode switch has been seen at HOLD, so the rover never drives off again by
  itself when the link comes back.
*/
#include <WiFiS3.h>
#include <RoverLink.h>
#include "config.h"
#include "web_page.h"
#include "wave.h"

WiFiUDP udp;

RlRc last;                     // last valid TX packet
bool havePkt = false;
uint32_t lastPkt = 0;
IPAddress txIp;
uint32_t tTxIp = 0;
bool needToggle = true;        // ch5 forced HOLD until the TX's switch shows HOLD
uint8_t state = RL_S_WAIT;
uint16_t out[RL_NCH];
bool outFailsafe = true;

// link quality over a 1 s window, from TX sequence numbers
uint16_t winFirstSeq = 0, winLastSeq = 0;
uint16_t winCount = 0;
uint32_t tWin = 0;
uint8_t lq = 0;
uint16_t rate = 0;

uint32_t tIbus = 0, tRx = 0, tStat = 0, tStatus = 0, tLed = 0;
uint16_t statSeq = 0;

// -----------------------------------------------------------------------------
// iBus out
// -----------------------------------------------------------------------------
void ibusSend(const uint16_t* c, bool failsafe) {
  uint8_t f[32];
  f[0] = 0x20; f[1] = 0x40;
  for (int i = 0; i < RL_NCH; i++) {
    f[2 + 2 * i] = c[i] & 0xFF;
    f[3 + 2 * i] = (c[i] >> 8) & 0x0F;
  }
  if (failsafe) f[3] |= 0x10;   // ArduPilot: (frame[3] & 0xF0) -> failsafe
  uint16_t sum = 0xFFFF;
  for (int i = 0; i < 30; i++) sum -= f[i];
  f[30] = sum & 0xFF;
  f[31] = sum >> 8;
  Serial1.write(f, 32);
}

// -----------------------------------------------------------------------------
// Link
// -----------------------------------------------------------------------------
void pollUdp() {
  for (int k = 0; k < 4; k++) {
    int n = udp.parsePacket();
    if (n <= 0) return;
    uint8_t buf[64];
    int r = udp.read(buf, min(n, (int)sizeof(buf)));
    RlRc p;
    if (!rlCheck(buf, r, RL_T_RC, rlCfg.id, p)) continue;
    uint32_t now = millis();
    bool wasAway = !havePkt || now - lastPkt > HOLD_AFTER_MS;
    if (wasAway || now - tTxIp > 1000) {
      txIp = udp.remoteIP();
      tTxIp = now;
      if (wasAway) { Serial.print("LINK: TX id "); Serial.print(rlCfg.id); Serial.print(" at "); Serial.println(txIp); }
    }
    if (winCount == 0) winFirstSeq = p.seq;
    winLastSeq = p.seq;
    winCount++;
    last = p;
    havePkt = true;
    lastPkt = now;
  }
}

void updateOutput() {
  uint32_t age = millis() - lastPkt;
  uint8_t prev = state;
  if (!havePkt)                          state = RL_S_WAIT;
  else if (age > FAILSAFE_AFTER_MS)      state = RL_S_FAILSAFE;
  else if (age > HOLD_AFTER_MS || !(last.flags & RL_F_LIVE)) state = RL_S_HOLD;
  else                                   state = RL_S_LIVE;

  if (state != RL_S_LIVE) needToggle = true;
  if (state != prev) {
    static const char* names[] = {"WAIT", "LIVE", "HOLD", "FAILSAFE"};
    Serial.print("STATE: "); Serial.println(names[state]);
  }

  if (state == RL_S_LIVE) {
    memcpy(out, last.ch, sizeof(out));
    if (needToggle) {
      if (last.ch[4] < MODE_MANUAL_ABOVE) needToggle = false;   // switch seen at HOLD
      else out[4] = PWM_MODE_HOLD;
    }
    outFailsafe = false;
  } else {
    memcpy(out, SAFE_CH, sizeof(out));
    if (state == RL_S_HOLD) {            // TX still talking or just gone: keep its aux values
      out[9] = last.ch[9];
      out[13] = last.ch[13];
    }
    outFailsafe = state == RL_S_WAIT || state == RL_S_FAILSAFE;
  }
}

void sendStat() {
  if (!havePkt || millis() - lastPkt > FAILSAFE_AFTER_MS) {
    if (!rlHasPeer()) return;            // nobody to talk to yet
  }
  RlStat s;
  s.type = RL_T_STAT;
  s.id = rlCfg.id;
  s.seq = statSeq++;
  s.state = state;
  s.lq = lq;
  s.rssi = rlIsAp() ? 0 : (int8_t)WiFi.RSSI();
  s.manual = out[4] >= MODE_MANUAL_ABOVE;
  s.rate = rate;
  s.ch1 = out[0];
  s.ch2 = out[1];
  rlSeal(s);
  udp.beginPacket(rlHasPeer() ? rlPeer() : txIp, RL_PORT);
  udp.write((const uint8_t*)&s, sizeof(s));
  udp.endPacket();
}

const char* stateName() {
  switch (state) {
    case RL_S_LIVE: return "LIVE";
    case RL_S_HOLD: return "HOLD";
    case RL_S_FAILSAFE: return "FAILSAFE";
    default: return "WAIT";
  }
}

void controlTick() {
  uint32_t now = millis();
  if (now - tRx >= 5) { tRx = now; pollUdp(); }
  updateOutput();
  if (rlCfg.rxOut == 1) {
    if (now - tIbus >= IBUS_PERIOD_MS) { tIbus = now; ibusSend(out, outFailsafe); }
  } else if (WaveBuf* w = waveSpare()) {   // the timer took the last frame: queue the next one
    if (rlCfg.rxOut == 2) waveBuildSbus(w, out, outFailsafe, state == RL_S_HOLD);
    else                  waveBuildPpm(w, out, !outFailsafe);   // PPM: no pulses = failsafe
    waveQueue(w);
  }
  if (now - tWin >= 1000) {
    uint16_t expected = winCount ? (uint16_t)(winLastSeq - winFirstSeq + 1) : 0;
    lq = expected ? min(100, winCount * 100 / expected) : 0;
    rate = winCount;
    winCount = 0;
    tWin = now;
  }
  if (now - tStat >= STAT_PERIOD_MS) { tStat = now; sendStat(); }
  if (now - tStatus >= 2000) {
    static uint32_t lastFrames = 0;
    uint32_t fr = waveFrames;
    uint32_t hz = (fr - lastFrames) * 1000 / max(1UL, now - tStatus);
    lastFrames = fr;
    tStatus = now;
    if (rlCfg.rxOut != 1) {
      Serial.print(outName()); Serial.print(": "); Serial.print(hz); Serial.print(" frames/s");
      Serial.println(outFailsafe ? (rlCfg.rxOut == 2 ? ", failsafe flag SET" : ", pulses OFF (failsafe)") : "");
    }
    char b[200];
    snprintf(b, sizeof(b), "STAT id %u %s %s | %s lq=%u%% rate=%u/s age=%lums | out ch1 %u ch2 %u ch5 %u ch14 %u%s%s",
             rlCfg.id, rlModeName(), outName(), stateName(), lq, rate, havePkt ? (unsigned long)(millis() - lastPkt) : 0UL,
             out[0], out[1], out[4], out[13], outFailsafe ? " FAILSAFE-FLAG" : "", needToggle ? " (waiting for HOLD on TX switch)" : "");
    Serial.println(b);
  }
}

// -----------------------------------------------------------------------------
// LED
// -----------------------------------------------------------------------------
void ledTickRx() {
  if (!ledTick()) return;
  uint32_t now = millis();
  if (now - tLed < 100) return;
  tLed = now;
  char s;
  if (state == RL_S_WAIT)          s = '?';
  else if (state == RL_S_FAILSAFE) s = 'F';
  else if (out[13] >= MODE_MANUAL_ABOVE) s = 'X';
  else                             s = out[4] >= MODE_MANUAL_ABOVE ? 'M' : 'H';
  bool recent = havePkt && now - lastPkt <= FAILSAFE_AFTER_MS;
  ledMain(rlCfg.id, s, rlNetGlyph(), recent ? lq : -1);
}

// -----------------------------------------------------------------------------
// Web
// -----------------------------------------------------------------------------
void buildState() {
  size_t n = snprintf(rlDyn, sizeof(rlDyn), "{");
  n += rlNetJson(rlDyn + n, sizeof(rlDyn) - n);
  bool recent = havePkt && millis() - lastPkt <= FAILSAFE_AFTER_MS;
  char tip[16] = "";
  if (havePkt) snprintf(tip, sizeof(tip), "%u.%u.%u.%u", txIp[0], txIp[1], txIp[2], txIp[3]);
  n += snprintf(rlDyn + n, sizeof(rlDyn) - n,
    ",\"out\":\"%s\",\"st\":\"%s\",\"lk\":%d,\"tip\":\"%s\",\"lq\":%d,\"rate\":%u,\"age\":%ld,\"fs\":%d,\"nt\":%d,\"ch\":[",
    outName(), stateName(), recent, tip, recent ? lq : -1, rate, havePkt ? (long)(millis() - lastPkt) : -1L,
    outFailsafe, needToggle);
  for (int i = 0; i < RL_NCH; i++) n += snprintf(rlDyn + n, sizeof(rlDyn) - n, "%s%u", i ? "," : "", out[i]);
  snprintf(rlDyn + n, sizeof(rlDyn) - n, "]}");
  rlRespondJson(rlDyn);
}

void route(const char* path, const char* q) {
  if (!strcmp(path, "/")) { rlRespond("text/html; charset=utf-8", PAGE, strlen(PAGE)); return; }
  if (!strcmp(path, "/s")) { buildState(); return; }
  if (!strcmp(path, "/out")) {
    char v[8];
    rlQparam(q, "v", v, sizeof(v));
    if (strcmp(v, "ppm") && strcmp(v, "ibus") && strcmp(v, "sbus")) { rlRespondJson("{\"e\":\"v=sbus, ppm or ibus\"}"); return; }
    rlRespondJson("{\"ok\":1,\"reboot\":1}");
    setOutput(!strcmp(v, "sbus") ? 2 : !strcmp(v, "ibus") ? 1 : 0);
    return;
  }
  if (rlRouteNet(path, q, state != RL_S_LIVE)) return;
}

const char* outName() { return rlCfg.rxOut == 2 ? "SBUS" : rlCfg.rxOut == 1 ? "IBUS" : "PPM"; }

void setOutput(uint8_t v) {
  rlCfg.rxOut = v;
  rlSaveSettings();
  Serial.print("OUT: "); Serial.print(outName()); Serial.println(" -> rebooting");
  rlRequestReboot(300);
}

void serialCmd(char* l) {
  if (!strcmp(l, "stat")) { tStatus = 0; return; }
  if (!strcmp(l, "out ppm"))  { setOutput(0); return; }
  if (!strcmp(l, "out ibus")) { setOutput(1); return; }
  if (!strcmp(l, "out sbus")) { setOutput(2); return; }
  if (rlSerialNet(l)) return;
  Serial.println("SER: stat | out sbus|ppm|ibus | join <ssid><TAB><pass> | ap | id <n> | peer <ip|-> | ip <ip> [gw] [mask] | ip - | appass <p>");
}

void setup() {
  Serial.begin(115200);
  memcpy(out, SAFE_CH, sizeof(out));
  ledBegin();
  delay(300);
  Serial.println("\nROVER//RC RX boot");
  rlLoadSettings();
  if (rlCfg.rxOut == 1) {
    Serial1.begin(115200);        // iBus out on D1 (for a Cube serial port, SERIALx_PROTOCOL=23)
    ibusSend(out, true);
    Serial.println("OUT: iBus on D1 (115200 8N1, 14 ch)");
  } else {
    bool sbus = rlCfg.rxOut == 2;
    if (!waveBegin(sbus ? 0 : 1)) {
      Serial.println("FATAL: no free GPT timer for the output waveform");
      ledFatal();
      while (true) delay(1000);
    }
    // Queue a failsafe frame now; the timer repeats it while WiFi comes up.
    WaveBuf* w = waveSpare();
    if (sbus) waveBuildSbus(w, out, true, false);
    else      waveBuildPpm(w, out, false);
    waveQueue(w);
    Serial.println(sbus ? "OUT: SBUS on D1 (inverted, 100000 8E2, 16 ch, 9 ms, timer-generated like an ELRS receiver)"
                        : "OUT: PPM on D1 (8 ch, 22.5 ms)");
  }
  rlNetBegin(RL_RX, false);       // blocking (up to ~20 s); SBUS/PPM keep running from the timer
  udp.begin(RL_PORT);
  rlWebBegin();
  Serial.println("ready: waiting for the handheld");
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
  ledTickRx();
  rlRebootTick();
}
