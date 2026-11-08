#pragma once
// ============================================================
//  cam_avi.h -- penulis AVI MJPEG (video saja) untuk rekaman app Kamera
//
//  Tata letak (header TETAP 224 byte, jadi bisa ditulis dulu lalu ditimpa di akhir):
//     0  RIFF <size> 'AVI '
//    12  LIST <192> 'hdrl'
//    24    avih (56)            <- usPerFrame, total frame, ukuran
//    88    LIST <116> 'strl'
//   100      strh (56)          <- 'vids' 'MJPG', scale/rate
//   164      strf (40)          <- BITMAPINFOHEADER, biCompression 'MJPG'
//   212  LIST <moviSize> 'movi'
//   224    '00dc' <size> <jpeg> [pad]   ... (tiap frame)
//          idx1 (16 byte per frame, offset relatif ke tag 'movi' di posisi 220)
//
//  Kompatibel dengan pemutar avi_renphone.ino (seek lewat idx1) dan pemutar PC (VLC/ffmpeg).
//  Template atas tipe file supaya bisa diuji di komputer (File Arduino punya write/seek).
// ============================================================
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#define CAMAVI_HDR 224

template <class F>
struct CamAviWriter {
  F*        f = nullptr;
  uint32_t  w = 0, h = 0;
  uint32_t  frames = 0;
  uint32_t  moviBytes = 0;      // jumlah (8 + data + pad) semua chunk
  uint32_t  maxFrame = 0;
  uint32_t* idxOff = nullptr;
  uint32_t* idxSize = nullptr;
  uint32_t  idxCap = 0;
  bool      ok = false;

  static void p32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
  static void p16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
  static void fcc(uint8_t* p, const char* s) { memcpy(p, s, 4); }

  static void buildHeader(uint8_t* b, uint32_t W, uint32_t H, uint32_t usPerFrame, uint32_t nFrames,
                          uint32_t maxFr, uint32_t moviListSize, uint32_t riffSize) {
    memset(b, 0, CAMAVI_HDR);
    uint32_t fps1000 = usPerFrame ? (uint32_t)(1000000000ULL / usPerFrame) : 15000;
    fcc(b + 0, "RIFF");  p32(b + 4, riffSize);  fcc(b + 8, "AVI ");
    fcc(b + 12, "LIST"); p32(b + 16, 192);      fcc(b + 20, "hdrl");
    // avih
    fcc(b + 24, "avih"); p32(b + 28, 56);
    uint8_t* a = b + 32;
    p32(a + 0, usPerFrame);
    p32(a + 4, usPerFrame ? (uint32_t)(((uint64_t)maxFr * 1000000ULL) / usPerFrame) : 0);   // maks byte/dtk
    p32(a + 8, 0);
    p32(a + 12, 0x10);                  // AVIF_HASINDEX
    p32(a + 16, nFrames);
    p32(a + 20, 0);
    p32(a + 24, 1);                     // 1 stream
    p32(a + 28, maxFr);
    p32(a + 32, W); p32(a + 36, H);
    // strl
    fcc(b + 88, "LIST"); p32(b + 92, 116); fcc(b + 96, "strl");
    fcc(b + 100, "strh"); p32(b + 104, 56);
    uint8_t* s = b + 108;
    fcc(s + 0, "vids"); fcc(s + 4, "MJPG");
    p32(s + 8, 0);                      // flags
    p16(s + 12, 0); p16(s + 14, 0);     // priority, language
    p32(s + 16, 0);                     // initial frames
    p32(s + 20, usPerFrame ? usPerFrame : 66667);   // scale
    p32(s + 24, 1000000);                           // rate  -> scale/rate = detik per frame
    p32(s + 28, 0);                     // start
    p32(s + 32, nFrames);               // length
    p32(s + 36, maxFr);
    p32(s + 40, 0xFFFFFFFFu);           // quality
    p32(s + 44, 0);                     // sample size
    p16(s + 48, 0); p16(s + 50, 0); p16(s + 52, (uint16_t)W); p16(s + 54, (uint16_t)H);   // rcFrame
    (void)fps1000;
    fcc(b + 164, "strf"); p32(b + 168, 40);
    uint8_t* bm = b + 172;
    p32(bm + 0, 40); p32(bm + 4, W); p32(bm + 8, H);
    p16(bm + 12, 1); p16(bm + 14, 24);
    fcc(bm + 16, "MJPG");
    p32(bm + 20, W * H * 3);
    // movi
    fcc(b + 212, "LIST"); p32(b + 216, moviListSize); fcc(b + 220, "movi");
  }

  // file harus baru & kosong. offBuf/sizeBuf = array uint32_t sebanyak cap (PSRAM).
  bool begin(F* file, uint32_t W, uint32_t H, uint32_t* offBuf, uint32_t* sizeBuf, uint32_t cap) {
    f = file; w = W; h = H; frames = 0; moviBytes = 0; maxFrame = 0;
    idxOff = offBuf; idxSize = sizeBuf; idxCap = cap;
    uint8_t hd[CAMAVI_HDR];
    buildHeader(hd, W, H, 66667, 0, 0, 4, CAMAVI_HDR - 8 + 4);   // sementara; ditimpa di finish()
    ok = f->write(hd, CAMAVI_HDR) == CAMAVI_HDR;
    return ok;
  }
  bool full() const { return frames >= idxCap; }

  bool addFrame(const uint8_t* d, uint32_t n) {
    if (!ok || !f || n == 0 || frames >= idxCap) return false;
    uint8_t ch[8];
    fcc(ch, "00dc"); p32(ch + 4, n);
    uint32_t chunkPos = CAMAVI_HDR + moviBytes;           // posisi header chunk di file
    if (f->write(ch, 8) != 8) { ok = false; return false; }
    if (f->write(d, n) != n)  { ok = false; return false; }
    uint32_t pad = n & 1;
    if (pad) { uint8_t z = 0; if (f->write(&z, 1) != 1) { ok = false; return false; } }
    idxOff[frames]  = chunkPos - 220;                      // relatif ke fourcc 'movi'
    idxSize[frames] = n;
    frames++;
    moviBytes += 8 + n + pad;
    if (n > maxFrame) maxFrame = n;
    return true;
  }

  // durMs = lama rekaman (ms). Menulis idx1 lalu menimpa header. Pemanggil yang menutup file.
  bool finish(uint32_t durMs) {
    if (!ok || !f) return false;
    uint32_t us = (frames > 0 && durMs > 0) ? (uint32_t)(((uint64_t)durMs * 1000ULL) / frames) : 66667;
    if (us < 1000) us = 1000;
    // idx1
    uint8_t ih[8]; fcc(ih, "idx1"); p32(ih + 4, frames * 16);
    if (f->write(ih, 8) != 8) { ok = false; return false; }
    uint8_t buf[16 * 64];
    uint32_t i = 0;
    while (i < frames) {
      uint32_t c = frames - i; if (c > 64) c = 64;
      for (uint32_t k = 0; k < c; k++) {
        uint8_t* e = buf + k * 16;
        fcc(e, "00dc"); p32(e + 4, 0x10); p32(e + 8, idxOff[i + k]); p32(e + 12, idxSize[i + k]);
      }
      if (f->write(buf, c * 16) != c * 16) { ok = false; return false; }
      i += c;
    }
    uint32_t riffSize = (CAMAVI_HDR - 8) + moviBytes + 8 + frames * 16;
    uint8_t hd[CAMAVI_HDR];
    buildHeader(hd, w, h, us, frames, maxFrame, 4 + moviBytes, riffSize);
    if (!f->seek(0)) { ok = false; return false; }
    if (f->write(hd, CAMAVI_HDR) != CAMAVI_HDR) { ok = false; return false; }
    return true;
  }
};
