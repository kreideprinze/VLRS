#pragma once
// ---------------------------------------------------------------------------
// Tiny HTTP server for the R4 (one request at a time, response written in
// 256-byte chunks so the control loop keeps running between them) plus the
// network-settings routes and USB serial commands shared by TX and RX.
//
// Settings routes (all GET, answer JSON):
//   /scan               list networks (refused while driving)
//   /join?s=&p=         save external network, reboot
//   /ap                 forget external network, reboot
//   /appass?p=          AP / direct-link password (8-32), used after reboot
//   /id?v=              bind ID 1-99, reboot
//   /peer?ip=           partner IP, blank = discover by broadcast (immediate)
//   /ip?ip=&gw=&nm=     static IP on the external network, blank ip = DHCP, reboot
// Serial (newline-terminated): join <ssid><TAB><pass> | ap | id <n> | peer <ip|-> |
//                              ip <ip> [gw] [mask] | ip - | appass <p>
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFiS3.h>

typedef void (*RlRouteFn)(const char* path, const char* q);
typedef void (*RlYieldFn)();

extern char rlDyn[1400];

void rlWebBegin();
void rlWebService(RlRouteFn appRoute, RlYieldFn yieldFn);
void rlRespond(const char* type, const char* body, size_t len);
void rlRespondJson(const char* s);
bool rlQparam(const char* q, const char* key, char* out, size_t outLen);
void rlJsonStr(char* dst, size_t len, const char* s);

// Handles the settings routes above; false if path is not one of them.
bool rlRouteNet(const char* path, const char* q, bool scanAllowed);
// Appends the common network fields (no braces) to dst; returns chars written.
size_t rlNetJson(char* dst, size_t len);
// Handles the settings serial commands; false if not one of them.
bool rlSerialNet(char* line);

uint32_t rlWebLastRequest();   // millis() of the last HTTP request (0 = none yet)

void rlRequestReboot(uint32_t delayMs);
void rlRebootTick();
