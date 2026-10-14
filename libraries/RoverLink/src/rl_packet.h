#pragma once
// ---------------------------------------------------------------------------
// TX <-> RX packet format (UDP, little-endian, packed).
//   TX -> RX  RlRc    50 Hz  stick/switch channels
//   RX -> TX  RlStat   5 Hz  link quality + what the RX is outputting
// Both carry the bind ID; a board ignores packets whose ID differs from its own.
// ---------------------------------------------------------------------------
#include <Arduino.h>

#define RL_PORT   14570
#define RL_MAGIC  0x314B4C52UL     // "RLK1"
#define RL_NCH    14               // iBus carries 14 channels

enum : uint8_t { RL_T_RC = 1, RL_T_STAT = 2 };

// RlRc.flags
#define RL_F_LIVE   0x01   // TX interlock released: RX may pass sticks through
#define RL_F_ESTOP  0x02

// RlStat.state
enum : uint8_t { RL_S_WAIT = 0, RL_S_LIVE = 1, RL_S_HOLD = 2, RL_S_FAILSAFE = 3 };

struct __attribute__((packed)) RlRc {
  uint32_t magic;
  uint8_t  type;
  uint8_t  id;
  uint16_t seq;
  uint8_t  flags;
  uint16_t ch[RL_NCH];
  uint16_t crc;
};

struct __attribute__((packed)) RlStat {
  uint32_t magic;
  uint8_t  type;
  uint8_t  id;
  uint16_t seq;
  uint8_t  state;     // RL_S_*
  uint8_t  lq;        // % of TX packets received over the last second
  int8_t   rssi;      // RX's WiFi RSSI (dBm), 0 when it is the AP
  uint8_t  manual;    // 1 = RX is outputting MANUAL on ch5
  uint16_t rate;      // TX packets/s received
  uint16_t ch1, ch2;  // what the RX is outputting (steer, throttle)
  uint16_t crc;
};

inline uint16_t rlCrc16(const uint8_t* p, size_t n) {   // CRC-16/CCITT-FALSE
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
  }
  return c;
}

template <typename T> inline void rlSeal(T& pkt) {
  pkt.magic = RL_MAGIC;
  pkt.crc = rlCrc16((const uint8_t*)&pkt, sizeof(T) - 2);
}

// True if buf holds a valid packet of type T for bind ID id.
template <typename T> inline bool rlCheck(const uint8_t* buf, int n, uint8_t type, uint8_t id, T& out) {
  if (n != (int)sizeof(T)) return false;
  memcpy(&out, buf, sizeof(T));
  if (out.magic != RL_MAGIC || out.type != type || out.id != id) return false;
  return out.crc == rlCrc16(buf, sizeof(T) - 2);
}
