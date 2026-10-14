#include "rl_web.h"
#include "rl_net.h"
#include "rl_page.h"

char rlDyn[1400];

static WiFiServer server(80);
static WiFiClient http;
static bool httpBusy = false;
static const char* hBody = nullptr;
static size_t hLeft = 0;
static char hHdr[170];
static bool hHdrSent = false;
static uint32_t hDeadline = 0, tAccept = 0;
static bool rebootAfterResponse = false;
static uint32_t rebootAt = 0;
static uint32_t lastRequest = 0;

uint32_t rlWebLastRequest() { return lastRequest; }

void rlRequestReboot(uint32_t delayMs) { rebootAt = millis() + delayMs; if (!rebootAt) rebootAt = 1; }
void rlRebootTick() { if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) NVIC_SystemReset(); }

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c |= 32;
  return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

bool rlQparam(const char* q, const char* key, char* out, size_t outLen) {
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
  if (outLen) out[0] = 0;
  return false;
}

void rlJsonStr(char* dst, size_t len, const char* s) {
  size_t n = 0;
  for (; *s && n + 7 < len; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') { dst[n++] = '\\'; dst[n++] = c; }
    else if (c >= 0x20) dst[n++] = c;
  }
  dst[n] = 0;
}

void rlRespond(const char* type, const char* body, size_t len) {
  snprintf(hHdr, sizeof(hHdr),
           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
           type, (unsigned)len);
  hBody = body; hLeft = len; hHdrSent = false;
}

void rlRespondJson(const char* s) {
  if (s != rlDyn) { strncpy(rlDyn, s, sizeof(rlDyn) - 1); rlDyn[sizeof(rlDyn) - 1] = 0; }
  rlRespond("application/json", rlDyn, strlen(rlDyn));
}

// "a.b.c.d" -> out; "" -> 0.0.0.0. False on garbage.
static bool parseIp(const char* s, uint8_t out[4]) {
  if (!s[0] || !strcmp(s, "-")) { memset(out, 0, 4); return true; }
  IPAddress ip;
  if (!ip.fromString(s) || ip[0] == 0) return false;
  for (int i = 0; i < 4; i++) out[i] = ip[i];
  return true;
}

static void ipStr(char* d, size_t n, const uint8_t ip[4]) {
  if (ip[0]) snprintf(d, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  else d[0] = 0;
}

size_t rlNetJson(char* d, size_t len) {
  char ss[70], pr[16], sip[16], sgw[16], snm[16];
  rlJsonStr(ss, sizeof(ss), rlSsidNow());
  ipStr(pr, sizeof(pr), rlCfg.peerIp);
  ipStr(sip, sizeof(sip), rlCfg.staticIp);
  ipStr(sgw, sizeof(sgw), rlCfg.gw);
  ipStr(snm, sizeof(snm), rlCfg.mask);
  int rssi = rlIsAp() ? 0 : WiFi.RSSI();
  return snprintf(d, len,
    "\"m\":\"%s\",\"ss\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"rs\":%d,\"id\":%u,\"wc\":%u,"
    "\"pr\":\"%s\",\"sip\":\"%s\",\"sgw\":\"%s\",\"snm\":\"%s\",\"fw\":\"%s\",\"fo\":%d",
    rlModeName(), ss, rlMyIp[0], rlMyIp[1], rlMyIp[2], rlMyIp[3], rssi, rlCfg.id, rlLinkChannel(),
    pr, sip, sgw, snm, rlFwVer, rlFwOld);
}

static void scanJson(bool scanAllowed) {
  bool cached = true;
  if (scanAllowed) cached = !rlScan();
  size_t n = snprintf(rlDyn, sizeof(rlDyn), "{\"c\":%d,\"n\":[", cached);
  for (int i = 0; i < rlNetCount && n < sizeof(rlDyn) - 90; i++) {
    char ss[70];
    rlJsonStr(ss, sizeof(ss), rlNets[i].ssid);
    n += snprintf(rlDyn + n, sizeof(rlDyn) - n, "%s[\"%s\",%d,%d]", i ? "," : "", ss, rlNets[i].rssi, rlNets[i].enc);
  }
  snprintf(rlDyn + n, sizeof(rlDyn) - n, "]%s}", scanAllowed ? "" : ",\"e\":\"stop driving first - a live scan stalls the link for ~3 s; showing cached list\"");
  rlRespondJson(rlDyn);
}

static void saveAndReboot(const char* why) {
  rlSaveSettings();
  Serial.print("CFG: "); Serial.print(why); Serial.println(" -> rebooting");
  rlRespondJson("{\"ok\":1,\"reboot\":1}");
  rebootAfterResponse = true;
}

bool rlRouteNet(const char* path, const char* q, bool scanAllowed) {
  char a[70], b[70], c[20];
  if (!strcmp(path, "/c.css")) { rlRespond("text/css", RL_CSS, strlen(RL_CSS)); return true; }
  if (!strcmp(path, "/n.js")) { rlRespond("application/javascript", RL_NETJS, strlen(RL_NETJS)); return true; }
  if (!strcmp(path, "/scan")) { scanJson(scanAllowed); return true; }
  if (!strcmp(path, "/join")) {
    if (!rlQparam(q, "s", a, 33) || !a[0]) { rlRespondJson("{\"e\":\"missing ssid\"}"); return true; }
    rlQparam(q, "p", b, 65);
    if (b[0] && strlen(b) < 8) { rlRespondJson("{\"e\":\"WPA password must be 8+ chars\"}"); return true; }
    strcpy(rlCfg.ssid, a); strcpy(rlCfg.pass, b);
    saveAndReboot("join network");
    return true;
  }
  if (!strcmp(path, "/ap")) {
    rlCfg.ssid[0] = 0; rlCfg.pass[0] = 0;
    saveAndReboot("forget network");
    return true;
  }
  if (!strcmp(path, "/appass")) {
    rlQparam(q, "p", a, 33);
    if (strlen(a) < 8 || strlen(a) > 32) { rlRespondJson("{\"e\":\"8-32 chars\"}"); return true; }
    strcpy(rlCfg.apPass, a); rlSaveSettings();
    rlRespondJson("{\"ok\":1}");
    return true;
  }
  if (!strcmp(path, "/id")) {
    rlQparam(q, "v", a, 8);
    int v = atoi(a);
    if (v < 1 || v > 99) { rlRespondJson("{\"e\":\"bind ID must be 1-99\"}"); return true; }
    rlCfg.id = v;
    saveAndReboot("bind ID");
    return true;
  }
  if (!strcmp(path, "/peer")) {
    rlQparam(q, "ip", a, 20);
    if (!parseIp(a, rlCfg.peerIp)) { rlRespondJson("{\"e\":\"bad IP\"}"); return true; }
    rlSaveSettings();
    rlRespondJson("{\"ok\":1}");
    return true;
  }
  if (!strcmp(path, "/ip")) {
    uint8_t ip[4], gw[4], nm[4];
    rlQparam(q, "ip", a, 20); rlQparam(q, "gw", b, 20); rlQparam(q, "nm", c, 20);
    if (!parseIp(a, ip) || !parseIp(b, gw) || !parseIp(c, nm)) { rlRespondJson("{\"e\":\"bad IP / gateway / mask\"}"); return true; }
    memcpy(rlCfg.staticIp, ip, 4); memcpy(rlCfg.gw, gw, 4); memcpy(rlCfg.mask, nm, 4);
    saveAndReboot(ip[0] ? "static IP" : "DHCP");
    return true;
  }
  return false;
}

bool rlSerialNet(char* l) {
  if (!strcmp(l, "ap")) { rlCfg.ssid[0] = rlCfg.pass[0] = 0; rlSaveSettings(); Serial.println("SER: forget network -> rebooting"); rlRequestReboot(300); return true; }
  if (!strncmp(l, "join ", 5)) {
    char* ssid = l + 5;
    char* tab = strchr(ssid, '\t');
    const char* pass = "";
    if (tab) { *tab = 0; pass = tab + 1; }
    if (!*ssid || strlen(ssid) > 32 || strlen(pass) > 64) { Serial.println("SER: bad ssid/pass"); return true; }
    strcpy(rlCfg.ssid, ssid); strcpy(rlCfg.pass, pass); rlSaveSettings();
    Serial.print("SER: join '"); Serial.print(rlCfg.ssid); Serial.println("' -> rebooting");
    rlRequestReboot(300);
    return true;
  }
  if (!strncmp(l, "id ", 3)) {
    int v = atoi(l + 3);
    if (v < 1 || v > 99) { Serial.println("SER: bind ID must be 1-99"); return true; }
    rlCfg.id = v; rlSaveSettings();
    Serial.print("SER: bind ID "); Serial.print(v); Serial.println(" -> rebooting");
    rlRequestReboot(300);
    return true;
  }
  if (!strncmp(l, "peer ", 5)) {
    if (!parseIp(l + 5, rlCfg.peerIp)) { Serial.println("SER: bad IP"); return true; }
    rlSaveSettings();
    Serial.println(rlHasPeer() ? "SER: peer IP set" : "SER: peer IP cleared (broadcast discovery)");
    return true;
  }
  if (!strncmp(l, "ip ", 3)) {
    char a[20] = "", b[20] = "", c[20] = "";
    sscanf(l + 3, "%19s %19s %19s", a, b, c);
    uint8_t ip[4], gw[4], nm[4];
    if (!parseIp(a, ip) || !parseIp(b, gw) || !parseIp(c, nm)) { Serial.println("SER: bad IP / gateway / mask"); return true; }
    memcpy(rlCfg.staticIp, ip, 4); memcpy(rlCfg.gw, gw, 4); memcpy(rlCfg.mask, nm, 4);
    rlSaveSettings();
    Serial.println(ip[0] ? "SER: static IP set -> rebooting" : "SER: DHCP -> rebooting");
    rlRequestReboot(300);
    return true;
  }
  if (!strncmp(l, "appass ", 7)) {
    size_t n = strlen(l + 7);
    if (n < 8 || n > 32) { Serial.println("SER: 8-32 chars"); return true; }
    strcpy(rlCfg.apPass, l + 7); rlSaveSettings();
    Serial.println("SER: AP password saved (used after reboot)");
    return true;
  }
  return false;
}

void rlWebBegin() { server.begin(); }

static void httpFinish() {
  http.stop();
  httpBusy = false;
  if (rebootAfterResponse) rlRequestReboot(600);
}

void rlWebService(RlRouteFn appRoute, RlYieldFn yieldFn) {
  uint32_t now = millis();
  if (!httpBusy) {
    if (now - tAccept < 30) return;
    tAccept = now;
    WiFiClient c = server.available();
    if (!c) return;
    http = c;
    lastRequest = millis();

    char line[300];
    size_t n = 0;
    uint32_t dl = millis() + 300;
    while ((int32_t)(millis() - dl) < 0 && n < sizeof(line) - 1) {
      if (yieldFn) yieldFn();
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
    while ((int32_t)(millis() - dl) < 0) {
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
    hHdr[0] = 0;
    appRoute(path, q ? q : "");
    if (!hHdr[0]) {
      snprintf(hHdr, sizeof(hHdr), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      hBody = nullptr; hLeft = 0; hHdrSent = false;
    }
    httpBusy = true;
    hDeadline = millis() + 5000;
    return;
  }

  if (!http.connected() || (int32_t)(now - hDeadline) > 0) { httpFinish(); return; }
  if (!hHdrSent) { http.write((const uint8_t*)hHdr, strlen(hHdr)); hHdrSent = true; return; }
  if (hLeft) {
    size_t chunk = min(hLeft, (size_t)256);
    http.write((const uint8_t*)hBody, chunk);
    hBody += chunk; hLeft -= chunk;
    return;
  }
  httpFinish();
}
