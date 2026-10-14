#include "rl_net.h"
#include <EEPROM.h>

RlSettings rlCfg;
RlNetMode  rlMode = NM_NONE;
IPAddress  rlMyIp, rlBcast;
char       rlFwVer[16] = "?";
bool       rlFwOld = false;
RlNetEntry rlNets[12];
int        rlNetCount = 0;

static const int      SETTINGS_ADDR  = 128;
static const uint32_t SETTINGS_MAGIC = 0x31534C52UL;   // "RLS1"

// Settings layout of the first (MAVLink) handheld firmware, migrated once.
struct LegacySettings {
  uint32_t magic;          // "RCS1"
  char ssid[33];
  char pass[65];
  uint8_t gcsIp[4];
  char apPass[33];
  uint8_t lastMode;
};
static const uint32_t LEGACY_MAGIC = 0x52435331UL;

void rlSaveSettings() { EEPROM.put(SETTINGS_ADDR, rlCfg); }

void rlLoadSettings() {
  EEPROM.get(SETTINGS_ADDR, rlCfg);
  if (rlCfg.magic != SETTINGS_MAGIC) {
    LegacySettings old;
    EEPROM.get(SETTINGS_ADDR, old);
    bool legacy = old.magic == LEGACY_MAGIC;
    memset(&rlCfg, 0, sizeof(rlCfg));
    rlCfg.magic = SETTINGS_MAGIC;
    rlCfg.id = 1;
    rlCfg.rxOut = 2;   // SBUS
    strcpy(rlCfg.apPass, RL_AP_PASS_DEFAULT);
    if (legacy) {
      old.ssid[32] = old.pass[64] = old.apPass[32] = 0;
      strcpy(rlCfg.ssid, old.ssid);
      strcpy(rlCfg.pass, old.pass);
      if (strlen(old.apPass) >= 8) strcpy(rlCfg.apPass, old.apPass);
      Serial.println("CFG: migrated WiFi settings from the MAVLink firmware");
    }
    rlSaveSettings();
  }
  rlCfg.ssid[32] = rlCfg.pass[64] = rlCfg.apPass[32] = 0;
  if (rlCfg.id < 1 || rlCfg.id > 99) rlCfg.id = 1;
  if (rlCfg.espStatic > 1) rlCfg.espStatic = 0;   // fields added after first release
  if (rlCfg.rxOut > 2) rlCfg.rxOut = 2;
  if (strlen(rlCfg.apPass) < 8) strcpy(rlCfg.apPass, RL_AP_PASS_DEFAULT);
}

void rlLinkSsid(char* out, size_t n)   { snprintf(out, n, "ROVER-LINK-%02u", rlCfg.id); }
void rlConfigSsid(char* out, size_t n) { snprintf(out, n, "ROVER-RC-%02u", rlCfg.id); }
uint8_t rlLinkChannel() { static const uint8_t ch[3] = {1, 6, 11}; return ch[rlCfg.id % 3]; }

const char* rlModeName() {
  switch (rlMode) {
    case NM_EXT:        return "EXT";
    case NM_DIRECT_STA: return "LINK";
    case NM_DIRECT_AP:  return "AP";
    case NM_CONFIG_AP:  return "CFG";
    default:            return "--";
  }
}

const char* rlSsidNow() {
  static char s[33];
  if (rlMode == NM_EXT) return rlCfg.ssid;
  if (rlMode == NM_CONFIG_AP) { rlConfigSsid(s, sizeof(s)); return s; }
  rlLinkSsid(s, sizeof(s));
  return s;
}

bool rlIsAp() { return rlMode == NM_DIRECT_AP || rlMode == NM_CONFIG_AP; }

RlNetGlyph rlNetGlyph() {
  switch (rlMode) {
    case NM_EXT:        return NG_EXTERNAL;
    case NM_DIRECT_STA:
    case NM_DIRECT_AP:  return NG_DIRECT;
    case NM_CONFIG_AP:  return NG_CONFIG;
    default:            return NG_NONE;
  }
}

bool rlHasPeer() { return rlCfg.peerIp[0] != 0; }
IPAddress rlPeer() { return IPAddress(rlCfg.peerIp[0], rlCfg.peerIp[1], rlCfg.peerIp[2], rlCfg.peerIp[3]); }

static bool versionOlder(const char* a, const char* b) {
  int a1 = 0, a2 = 0, a3 = 0, b1 = 0, b2 = 0, b3 = 0;
  sscanf(a, "%d.%d.%d", &a1, &a2, &a3);
  sscanf(b, "%d.%d.%d", &b1, &b2, &b3);
  if (a1 != b1) return a1 < b1;
  if (a2 != b2) return a2 < b2;
  return a3 < b3;
}

bool rlScan() {
  int n = WiFi.scanNetworks();
  if (n <= 0) return false;
  rlNetCount = min(n, 12);
  for (int i = 0; i < rlNetCount; i++) {
    strncpy(rlNets[i].ssid, WiFi.SSID(i), 32); rlNets[i].ssid[32] = 0;
    rlNets[i].rssi = WiFi.RSSI(i);
    rlNets[i].enc = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  return true;
}

static bool waitForIp(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    rlMyIp = WiFi.localIP();
    if (rlMyIp != IPAddress(0, 0, 0, 0)) return true;
    delay(200);
  }
  return false;
}

// The ESP32 keeps a static IP across MCU resets, so DHCP must be restored
// explicitly - but only then: on the 0.5.1 bridge firmware this call stalls
// for the 10 s modem timeout.
static void useDhcp() {
  if (rlCfg.espStatic != 0) {
    IPAddress z(0, 0, 0, 0);
    WiFi.config(z, z, z, z);
    rlCfg.espStatic = 0;
    rlSaveSettings();
  }
}

static bool joinSta(const char* ssid, const char* pass, bool staticIp) {
  if (staticIp) {
    IPAddress ip(rlCfg.staticIp[0], rlCfg.staticIp[1], rlCfg.staticIp[2], rlCfg.staticIp[3]);
    IPAddress gw(rlCfg.gw[0], rlCfg.gw[1], rlCfg.gw[2], rlCfg.gw[3]);
    IPAddress nm(rlCfg.mask[0], rlCfg.mask[1], rlCfg.mask[2], rlCfg.mask[3]);
    if (gw == IPAddress(0, 0, 0, 0)) gw = IPAddress(ip[0], ip[1], ip[2], 1);
    if (nm == IPAddress(0, 0, 0, 0)) nm = IPAddress(255, 255, 255, 0);
    WiFi.config(ip, gw, gw, nm);   // (local, dns, gateway, subnet)
    if (rlCfg.espStatic != 1) { rlCfg.espStatic = 1; rlSaveSettings(); }
  } else {
    useDhcp();
  }
  Serial.print("NET: joining '"); Serial.print(ssid); Serial.println("'");
  ledPlayConnecting();
  WiFi.setTimeout(RL_JOIN_TIMEOUT_MS);
  if (WiFi.begin(ssid, pass[0] ? pass : nullptr) == WL_CONNECTED && waitForIp(5000)) return true;
  Serial.print("NET: could not join '"); Serial.print(ssid); Serial.println("'");
  WiFi.disconnect();
  return false;
}

static bool startAp(const char* ssid, IPAddress ip, uint8_t channel) {
  WiFi.config(ip);
  if (WiFi.beginAP(ssid, rlCfg.apPass, channel) != WL_AP_LISTENING) return false;
  waitForIp(3000);
  return true;
}

void rlNetBegin(RlRole role, bool forceAp) {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("FATAL: WiFi module not responding");
    ledFatal();
    while (true) delay(1000);
  }
  strncpy(rlFwVer, WiFi.firmwareVersion(), sizeof(rlFwVer) - 1);
  rlFwOld = versionOlder(rlFwVer, WIFI_FIRMWARE_LATEST_VERSION);
  if (rlFwOld) {
    Serial.print("WARN: WiFi firmware "); Serial.print(rlFwVer);
    Serial.print(" older than "); Serial.println(WIFI_FIRMWARE_LATEST_VERSION);
  }

  char link[33];
  rlLinkSsid(link, sizeof(link));
  bool triedSta = false;
  rlMode = NM_NONE;

  // 1. saved external network
  if (!forceAp && rlCfg.ssid[0]) {
    triedSta = true;
    if (joinSta(rlCfg.ssid, rlCfg.pass, rlCfg.staticIp[0] != 0)) rlMode = NM_EXT;
  }
  // 2. TX: the rover's direct link
  if (rlMode == NM_NONE && role == RL_TX && !forceAp) {
    triedSta = true;
    if (joinSta(link, rlCfg.apPass, false)) rlMode = NM_DIRECT_STA;
  }
  if (rlMode != NM_NONE) rlScan();

  // 3. own AP: RX = direct link, TX = config only
  if (rlMode == NM_NONE) {
    rlScan();   // cache nearby networks for the web page before going AP
    bool ok;
    if (role == RL_RX) {
      ok = startAp(link, IPAddress(192, 168, 5, 1), rlLinkChannel());
      rlMode = NM_DIRECT_AP;
    } else {
      char cfgSsid[33];
      rlConfigSsid(cfgSsid, sizeof(cfgSsid));
      ok = startAp(cfgSsid, IPAddress(192, 168, 4, 1), rlLinkChannel());
      rlMode = NM_CONFIG_AP;
    }
    if (!ok) {
      Serial.println("FATAL: could not start AP");
      ledFatal();
      while (true) delay(1000);
    }
  }

  if (rlIsAp()) {
    rlBcast = IPAddress(rlMyIp[0], rlMyIp[1], rlMyIp[2], 255);
  } else {
    IPAddress mask = WiFi.subnetMask();
    if (mask == IPAddress(0, 0, 0, 0)) mask = IPAddress(255, 255, 255, 0);
    for (int i = 0; i < 4; i++) rlBcast[i] = rlMyIp[i] | (uint8_t)~mask[i];
  }

  Serial.print("NET: "); Serial.print(rlModeName()); Serial.print(" '"); Serial.print(rlSsidNow());
  Serial.print("'  id "); Serial.print(rlCfg.id);
  Serial.print("  ip "); Serial.print(rlMyIp); Serial.print("  bcast "); Serial.println(rlBcast);
  if (rlIsAp()) {
    Serial.print("NET: channel "); Serial.print(rlLinkChannel());
    Serial.print("  password "); Serial.println(rlCfg.apPass);
  }

  // LED: tower <-> fan animation when the kind of network changed, then scroll.
  uint8_t nowAp = rlIsAp() ? 1 : 2;
  uint32_t animMs = 0;
  if (nowAp == 1 && (triedSta || rlCfg.lastAp == 2)) animMs = ledPlayStaToAp();
  else if (nowAp == 2 && rlCfg.lastAp == 1)          animMs = ledPlayApToSta();
  char text[40];
  snprintf(text, sizeof(text), "ID%02u %s %d.%d.%d.%d", rlCfg.id, rlModeName(),
           rlMyIp[0], rlMyIp[1], rlMyIp[2], rlMyIp[3]);
  ledSequence(animMs, text);
  if (rlCfg.lastAp != nowAp) { rlCfg.lastAp = nowAp; rlSaveSettings(); }
}
