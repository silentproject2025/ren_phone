#pragma once
// ============================================================
//  cam_tx.h -- pemecah frame JPEG jadi potongan RP_CAM_FRAME (sisi ESP32-CAM)
//  Format potongan: [seq][flags b0 awal b1 akhir b2 foto][offset u32 LE][data...]
//  Dipisah dari cam_module.h supaya bisa diuji di komputer.
// ============================================================
#include <Arduino.h>
#include "rplink.h"

#define CAM_CHUNK (RP_MAX_PAYLOAD - 6)

// Return false kalau dibatalkan lewat abortFn (dicek setelah tiap potongan).
inline bool camTxFrame(HardwareSerial& ser, uint8_t seq, const uint8_t* d, uint32_t n, bool still, bool (*abortFn)()) {
  uint8_t buf[RP_MAX_PAYLOAD];
  uint32_t off = 0;
  while (off < n) {
    uint32_t c = n - off;
    if (c > CAM_CHUNK) c = CAM_CHUNK;
    uint8_t fl = (uint8_t)((off == 0 ? 1 : 0) | (off + c >= n ? 2 : 0) | (still ? 4 : 0));
    buf[0] = seq; buf[1] = fl;
    buf[2] = (uint8_t)off; buf[3] = (uint8_t)(off >> 8); buf[4] = (uint8_t)(off >> 16); buf[5] = (uint8_t)(off >> 24);
    memcpy(buf + 6, d + off, c);
    rpSend(ser, RP_CAM_FRAME, buf, (uint16_t)(6 + c));
    off += c;
    if (abortFn && abortFn()) return false;
  }
  return true;
}
