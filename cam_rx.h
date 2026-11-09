#pragma once
// ============================================================
//  cam_rx.h -- penyusun ulang frame JPEG kiriman ESP32-CAM (sisi S3)
//
//  CAM memecah tiap frame JPEG jadi potongan RP_CAM_FRAME:
//      [0] seq   [1] flags (b0 awal, b1 akhir, b2 foto/still)   [2..5] offset u32 LE   [6..] data
//  Kelas ini menyusunnya kembali ke buffer PSRAM.
//
//  Preview : 3 slot bergilir. Frame baru yang selesai menggantikan frame "siap"
//            yang belum sempat diambil UI (latensi rendah, frame lama dibuang).
//  Header  : CAM boleh menghilangkan header JPEG (flag RPF_HDRSKIP) -> disalin dari cache header terakhir (id 16-bit harus cocok).
//  Foto    : satu buffer besar. Hanya diterima kalau UI sudah "mempersenjatai"
//            (arm) sebelum mengirim RP_CAM_SHOT, supaya sisa potongan lama diabaikan.
//
//  Dipanggil dari musTask (core 0, stack PSRAM): TIDAK boleh menyentuh SD/NVS di sini.
//  File ini sengaja tidak bergantung pada Serial/SD supaya bisa diuji di komputer.
// ============================================================
#include <stdint.h>
#include <string.h>
#include "rplink.h"      // rpJpegHdrLen / rpHdrHash / RPF_* (identik dgn sisi CAM)

#ifndef CAMRX_ENTER
  #if defined(ARDUINO) || defined(ESP_PLATFORM)
    extern portMUX_TYPE camRxMux;
    #define CAMRX_ENTER() portENTER_CRITICAL(&camRxMux)
    #define CAMRX_EXIT()  portEXIT_CRITICAL(&camRxMux)
  #else
    #define CAMRX_ENTER() do {} while (0)
    #define CAMRX_EXIT()  do {} while (0)
  #endif
#endif

#define CAMRX_SLOTS 3

enum { CAMRX_FREE = 0, CAMRX_FILL = 1, CAMRX_READY = 2, CAMRX_INUSE = 3 };
enum { CAMRX_STILL_IDLE = 0, CAMRX_STILL_RECV = 1, CAMRX_STILL_DONE = 2, CAMRX_STILL_FAIL = 3 };

struct CamRx {
  uint8_t*  slot[CAMRX_SLOTS] = {nullptr, nullptr, nullptr};
  uint32_t  slotCap = 0;
  uint32_t  slotLen[CAMRX_SLOTS] = {0, 0, 0};
  uint8_t   slotSt[CAMRX_SLOTS]  = {0, 0, 0};
  int       rxSlot = -1;          // slot yang sedang diisi (-1 = frame ini dibuang)
  bool      rxOn = false;         // sedang merakit frame preview
  uint8_t   rxSeq = 0;
  uint32_t  rxLen = 0;
  int       ready = -1;           // slot berisi frame terbaru yang belum diambil UI
  int       cur = -1;             // slot yang sedang dipakai UI

  uint8_t*  still = nullptr;
  uint32_t  stillCap = 0;
  uint32_t  stillLen = 0;
  volatile uint8_t stillState = CAMRX_STILL_IDLE;
  bool      sOn = false;
  uint8_t   sSeq = 0;

  uint8_t   hdr[RP_HDR_MAX];      // cache header JPEG utuh terakhir (dari potongan awal frame lengkap)
  uint16_t  hdrLen = 0, hdrId = 0;
  volatile uint8_t lastSeq = 0;   // seq frame preview terakhir yang potongan AKHIR-nya tiba (utuh atau rusak) -> isi RP_CAM_ACK
  uint32_t  bytes = 0;            // total byte data JPEG diterima (utk ukur KB/dtk)
  volatile bool needHdr = false;  // true = S3 butuh header lengkap -> dititipkan di RP_CAM_ACK (bit0 byte ke-2), CAM kirim lengkap di frame berikutnya
  uint32_t  hdrMiss = 0;          // frame header-dihilangkan yang dibuang krn cache tak cocok (pulih sendiri <= 1 dtk)
  uint32_t  frames = 0;           // frame preview utuh yang diterima
  uint32_t  dropped = 0;          // frame dibuang (tak ada slot kosong / terlambat diambil)
  uint32_t  bad = 0;              // frame rusak (potongan hilang / checksum JPEG gagal)
  uint32_t  oversize = 0;         // frame lebih besar dari buffer

  static bool jpegOk(const uint8_t* b, uint32_t n) {
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return false;
    uint32_t lim = n > 16 ? n - 16 : 0;          // EOI boleh diikuti beberapa byte padding
    for (uint32_t i = n - 2; ; i--) {
      if (b[i] == 0xFF && b[i + 1] == 0xD9) return true;
      if (i <= lim) break;
    }
    return false;
  }
  static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }

  void attach(uint8_t* s0, uint8_t* s1, uint8_t* s2, uint32_t cap) {
    slot[0] = s0; slot[1] = s1; slot[2] = s2; slotCap = cap;
    resetPreview();
  }
  void attachStill(uint8_t* buf, uint32_t cap) { still = buf; stillCap = cap; stillLen = 0; stillState = CAMRX_STILL_IDLE; sOn = false; }

  void resetPreview() {
    CAMRX_ENTER();
    for (int i = 0; i < CAMRX_SLOTS; i++) { slotSt[i] = CAMRX_FREE; slotLen[i] = 0; }
    rxSlot = -1; rxOn = false; ready = -1; cur = -1; hdrLen = 0;
    CAMRX_EXIT();
  }

  // UI memanggil ini SEBELUM mengirim RP_CAM_SHOT
  void armStill() { stillLen = 0; sOn = false; stillState = CAMRX_STILL_RECV; }
  void disarmStill() { stillState = CAMRX_STILL_IDLE; sOn = false; }

  // Satu payload RP_CAM_FRAME. Return true kalau frame (preview) baru saja selesai.
  bool feed(const uint8_t* p, uint16_t n) {
    if (n < 7) return false;
    uint8_t  seq = p[0], fl = p[1];
    uint32_t off = le32(p + 2);
    const uint8_t* d = p + 6;
    uint32_t dn = (uint32_t)n - 6;
    bool first = fl & RPF_FIRST, last = fl & RPF_LAST, isStill = fl & RPF_STILL;
    if (last && !isStill) lastSeq = seq;

    if (isStill) {
      if (stillState != CAMRX_STILL_RECV || !still) return false;     // tidak diminta -> abaikan
      if (first) { sOn = true; sSeq = seq; stillLen = 0; }
      else if (!sOn || seq != sSeq || off != stillLen) { sOn = false; stillState = CAMRX_STILL_FAIL; bad++; return false; }
      if (off != stillLen || stillLen + dn > stillCap) { sOn = false; stillState = CAMRX_STILL_FAIL; oversize++; return false; }
      memcpy(still + stillLen, d, dn);
      stillLen += dn; bytes += dn;
      if (last) {
        sOn = false;
        if (jpegOk(still, stillLen)) stillState = CAMRX_STILL_DONE;
        else { stillState = CAMRX_STILL_FAIL; bad++; }
      }
      return false;
    }

    // ---- preview ----
    const uint8_t* src = d; uint32_t sn = dn;      // data JPEG yang benar2 disalin (tanpa id header)
    if (first) {
      if (rxOn && rxSlot >= 0) { CAMRX_ENTER(); slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; }   // NYX-CAMFREEZE-V1: frame sebelumnya tak pernah selesai (potongan AKHIR hilang) -> bebaskan slotnya, kalau tidak bocor & preview freeze
      rxOn = false; rxSlot = -1;
      bool skip = (fl & RPF_HDRSKIP) != 0;
      if (skip) {                                   // header dihilangkan: pakai cache, tapi hanya kalau id-nya cocok
        if (dn < 2 || hdrLen == 0 || off != hdrLen || (uint16_t)(d[0] | ((uint16_t)d[1] << 8)) != hdrId) { hdrMiss++; needHdr = true; return false; }
        src = d + 2; sn = dn - 2;
      } else if (off == 0) {                        // frame lengkap: simpan header-nya jadi cache
        uint16_t hl = rpJpegHdrLen(d, dn);
        if (hl) { memcpy(hdr, d, hl); hdrLen = hl; hdrId = rpHdrHash(d, hl); needHdr = false; } else hdrLen = 0;
      }
      CAMRX_ENTER();
      for (int i = 0; i < CAMRX_SLOTS; i++) if (slotSt[i] == CAMRX_FREE) { slotSt[i] = CAMRX_FILL; rxSlot = i; break; }
      CAMRX_EXIT();
      if (rxSlot < 0) { dropped++; return false; }
      rxOn = true; rxSeq = seq; rxLen = 0;
      if (skip) {
        if (hdrLen > slotCap) { CAMRX_ENTER(); slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); rxOn = false; rxSlot = -1; oversize++; return false; }
        memcpy(slot[rxSlot], hdr, hdrLen); rxLen = hdrLen;
      }
    } else if (!rxOn || seq != rxSeq || off != rxLen) {
      if (rxOn && rxSlot >= 0) { CAMRX_ENTER(); slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; }
      rxOn = false; rxSlot = -1;
      return false;
    }
    if (!rxOn) return false;
    if (off != rxLen || rxLen + sn > slotCap) {
      CAMRX_ENTER(); slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT();
      rxOn = false; rxSlot = -1; oversize++;
      return false;
    }
    memcpy(slot[rxSlot] + rxLen, src, sn);
    rxLen += sn; bytes += sn;
    if (!last) return false;

    rxOn = false;
    int s = rxSlot; rxSlot = -1;
    if (!jpegOk(slot[s], rxLen)) {
      CAMRX_ENTER(); slotSt[s] = CAMRX_FREE; CAMRX_EXIT();
      bad++;
      return false;
    }
    CAMRX_ENTER();
    slotLen[s] = rxLen;
    if (ready >= 0 && slotSt[ready] == CAMRX_READY) { slotSt[ready] = CAMRX_FREE; dropped++; }   // frame lama yang tak sempat diambil
    slotSt[s] = CAMRX_READY;
    ready = s;
    CAMRX_EXIT();
    frames++;
    return true;
  }

  // Dipanggil UI. Return indeks slot yang sekarang "dipakai UI", atau -1 kalau tidak ada frame BARU.
  // Slot tetap valid sampai take() berikutnya berhasil / releaseAll().
  int take() {
    int r = -1;
    CAMRX_ENTER();
    if (ready >= 0 && slotSt[ready] == CAMRX_READY) {
      if (cur >= 0 && slotSt[cur] == CAMRX_INUSE) slotSt[cur] = CAMRX_FREE;
      cur = ready; slotSt[cur] = CAMRX_INUSE; ready = -1;
      r = cur;
    }
    CAMRX_EXIT();
    return r;
  }
  // frame yang terakhir diambil (untuk digambar ulang tanpa frame baru), atau -1
  int current() const { return (cur >= 0 && slotSt[cur] == CAMRX_INUSE) ? cur : -1; }
  const uint8_t* data(int i) const { return slot[i]; }
  uint32_t len(int i) const { return slotLen[i]; }
};
