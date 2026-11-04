#pragma once
// ============================================================
//  cam_tx.h -- pemecah frame JPEG jadi potongan RP_CAM_FRAME (sisi ESP32-CAM)
//  Format potongan: [seq][flags b0 awal b1 akhir b2 foto b3 header-dihilangkan][offset u32 LE][data...]
//  Dipisah dari cam_module.h supaya bisa diuji di komputer.
//
//  v2 (link maksimal):
//   * Dedup header JPEG: header OV2640 (~600 B: DQT+DHT+SOF) identik di tiap frame preview. Kalau sama persis dgn
//     header yang terakhir dikirim LENGKAP, header dihilangkan & diganti id 2 byte (S3 punya cache). Dikirim lengkap
//     lagi tiap CAM_HDR_REFRESH frame supaya S3 yang ketinggalan cache pulih sendiri.
//   * CamFlow: kontrol aliran berbasis NOMOR URUT (bukan hitungan kredit) -> ACK hilang tidak membuat macet.
// ============================================================
#include <Arduino.h>
#include "rplink.h"

#define CAM_CHUNK        (RP_MAX_PAYLOAD - 6)
#define CAM_HDR_REFRESH  30      // kirim header lengkap lagi tiap sekian frame (pulih kalau cache S3 ketinggalan)

struct CamHdrCache {
  uint8_t  b[RP_HDR_MAX];
  uint16_t len = 0;
  uint16_t id = 0;
  uint8_t  run = 0;              // sudah berapa frame berturut2 header-nya dihilangkan
  void reset() { len = 0; run = 0; }
};

// Jendela kirim berbasis seq. `sent` = seq frame terakhir yang dikirim CAM (dikelola pemanggil).
struct CamFlow {
  volatile uint8_t acked = 0;
  inline uint8_t inflight(uint8_t sent) const { return (uint8_t)(sent - acked); }
  inline bool canSend(uint8_t sent, uint8_t window) const { return inflight(sent) < window; }
  // ACK dari S3. Diabaikan kalau basi (lebih lama dari yang sudah dikenal) atau "dari masa depan" (> sent).
  inline void onAck(uint8_t sent, uint8_t s) {
    int8_t adv = (int8_t)(s - acked), ahead = (int8_t)(sent - s);
    if (adv > 0 && ahead >= 0) acked = s;
  }
  inline void sync(uint8_t sent) { acked = sent; }                                           // mulai bersih (nol frame di jalan)
  inline void rescue(uint8_t sent, uint8_t window) { acked = (uint8_t)(sent - (window - 1)); }   // ACK hilang lama -> izinkan 1 frame lagi
};

// Return false kalau dibatalkan lewat abortFn (dicek setelah tiap potongan).
// hc != nullptr -> dedup header (hanya utk preview, bukan foto).
inline bool camTxFrame(HardwareSerial& ser, uint8_t seq, const uint8_t* d, uint32_t n, bool still, bool (*abortFn)(),
                       CamHdrCache* hc = nullptr) {
  uint8_t buf[RP_MAX_PAYLOAD];
  uint32_t off = 0;
  bool skip = false;
  uint16_t hid = 0;
  if (hc && !still) {
    uint16_t hl = rpJpegHdrLen(d, n);
    if (hl && hl < CAM_CHUNK - 2 && n > (uint32_t)hl + 8) {
      if (hc->len == hl && hc->run < CAM_HDR_REFRESH && memcmp(hc->b, d, hl) == 0) {
        skip = true; hid = hc->id; off = hl; hc->run++;
      } else {
        memcpy(hc->b, d, hl); hc->len = hl; hc->id = rpHdrHash(d, hl); hc->run = 0;
      }
    } else hc->len = 0;
  }
  bool firstChunk = true;
  while (off < n) {
    uint32_t extra = (skip && firstChunk) ? 2 : 0;
    uint32_t c = n - off;
    uint32_t cap = CAM_CHUNK - extra;
    if (c > cap) c = cap;
    uint8_t fl = (uint8_t)((firstChunk ? RPF_FIRST : 0) | (off + c >= n ? RPF_LAST : 0) | (still ? RPF_STILL : 0) |
                           ((skip && firstChunk) ? RPF_HDRSKIP : 0));
    buf[0] = seq; buf[1] = fl;
    buf[2] = (uint8_t)off; buf[3] = (uint8_t)(off >> 8); buf[4] = (uint8_t)(off >> 16); buf[5] = (uint8_t)(off >> 24);
    if (extra) { buf[6] = (uint8_t)hid; buf[7] = (uint8_t)(hid >> 8); }
    memcpy(buf + 6 + extra, d + off, c);
    rpSend(ser, RP_CAM_FRAME, buf, (uint16_t)(6 + extra + c));
    off += c;
    firstChunk = false;
    if (abortFn && abortFn()) return false;
  }
  return true;
}
