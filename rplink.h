#pragma once
// ============================================================
//  rplink.h -- protokol kabel Ren Phone (ESP32-S3) <-> ESP32-CAM
//  FILE INI HARUS IDENTIK di dua sisi (salin ke folder sketch masing2).
//
//  Frame:  [0xA5][type][lenLo][lenHi][payload...][xor]
//  xor = type ^ lenLo ^ lenHi ^ semua byte payload
//  Dua arah, 921600 baud 8N1, 3 kabel: TX, RX, GND.
// ============================================================
#include <Arduino.h>

#define RP_SOF          0xA5
#define RP_MAX_PAYLOAD  1024
#define RP_BAUD         921600

// ---- S3 -> CAM ----
#define RP_BEGIN    'P'   // mulai track baru. payload: [seq]. CAM buang semua buffer lama
#define RP_DATA     'D'   // potongan byte file MP3 (maks 1024)
#define RP_FINISH   'F'   // semua byte file sudah terkirim
#define RP_PAUSE    'U'   // payload: [1=lanjut, 0=jeda]
#define RP_VOLUME   'V'   // payload: [0..127]
#define RP_STOP     'X'   // stop total + kosongkan buffer
#define RP_SCAN     'A'   // mulai scan perangkat BT
#define RP_CONNECT  'C'   // payload: nama perangkat (teks, tanpa NUL)
#define RP_TXGAIN   'T'   // payload: [0..7]

// ---- CAM -> S3 ----
#define RP_STATUS   'S'   // tiap 40ms, 13 byte (lihat bt_coprocessor.ino sendStatus)
#define RP_ENDED    'E'   // lagu habis diputar. payload: [seq]
#define RP_SCANRES  'L'   // hasil scan. payload: [rssi int8][nama...]
#define RP_SCANDONE 'W'   // scan selesai
#define RP_HELLO    'R'   // CAM baru boot

struct RpParser {
  uint8_t  st = 0;
  uint8_t  type = 0;
  uint16_t len = 0;
  uint16_t got = 0;
  uint8_t  chk = 0;
  uint32_t errors = 0;
  uint8_t  payload[RP_MAX_PAYLOAD];

  // true kalau satu frame lengkap & checksum cocok (isi di type/len/payload)
  bool feed(uint8_t b) {
    switch (st) {
      case 0: if (b == RP_SOF) st = 1; return false;
      case 1: type = b; chk = b; st = 2; return false;
      case 2: len = b; chk ^= b; st = 3; return false;
      case 3:
        len |= (uint16_t)b << 8; chk ^= b;
        if (len > RP_MAX_PAYLOAD) { errors++; st = 0; return false; }
        got = 0; st = (len == 0) ? 5 : 4;
        return false;
      case 4:
        payload[got++] = b; chk ^= b;
        if (got >= len) st = 5;
        return false;
      case 5:
        st = 0;
        if (b == chk) return true;
        errors++;
        return false;
    }
    st = 0;
    return false;
  }
};

// kirim satu frame utuh dalam SATU write() (aman dari interleave antar task
// selama pengirimnya cuma satu task)
inline void rpSend(HardwareSerial& s, uint8_t type, const uint8_t* p, uint16_t len) {
  uint8_t out[RP_MAX_PAYLOAD + 5];
  if (len > RP_MAX_PAYLOAD) len = RP_MAX_PAYLOAD;
  out[0] = RP_SOF; out[1] = type; out[2] = len & 0xFF; out[3] = len >> 8;
  uint8_t chk = type ^ out[2] ^ out[3];
  for (uint16_t i = 0; i < len; i++) { out[4 + i] = p[i]; chk ^= p[i]; }
  out[4 + len] = chk;
  s.write(out, 5 + len);
}
