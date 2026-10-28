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
#define RP_BTMODE   'B'   // payload: [1=Bluetooth nyala, 0=mati] (tombol BT di Control Center)
#define RP_PREFILL  'Y'   // payload: [u16 LE byte PCM sebelum mulai diputar; 0 = bawaan]. Dipakai Telepon (latensi rendah)
#define RP_POWEROFF 'Z'   // S3 -> CAM: tidur dalam (deep sleep). Bangun lewat sinyal di RX (GPIO13)
#ifndef RP_PING
#define RP_PING     'G'   // keepalive S3 -> CAM, tanpa payload (CAM mengabaikannya)
#endif

// ---- CAM -> S3 ----
#define RP_STATUS   'S'   // tiap 40ms, 17 byte (lihat cam_bt_coprocessor.ino sendStatus)
#define RP_ENDED    'E'   // lagu habis diputar. payload: [seq]
#define RP_SCANRES  'L'   // hasil scan. payload: [rssi int8][nama...]
#define RP_SCANDONE 'W'   // scan selesai
#define RP_HELLO    'R'   // CAM baru boot
#define RP_KEY      'K'   // tombol earbuds (AVRCP). payload: [kode][arg]
                          //   kode: 1 play, 2 pause/stop, 3 next, 4 prev, 5 vol+, 6 vol-, 7 volume absolut (arg 0..127)

// ---- Kamera OV2640 di ESP32-CAM (app "Kamera": camera_renphone.ino <-> cam_module.h) ----
// S3 -> CAM
#define RP_CAM_OPEN   'J'   // payload: [xclk MHz][res RPR_*][quality][flags b0=langsung stream]. Nyalakan kamera
#define RP_CAM_CLOSE  'j'   // matikan kamera & lepas RAM
#define RP_CAM_SET    'k'   // payload: [id RPC_*][nilai int8]
#define RP_CAM_SHOT   'O'   // payload: [res RPR_*][quality][flash 0..255][flags]. Foto 1x resolusi tinggi
#define RP_CAM_ACK    'Q'   // payload: [seq]. S3 sudah mengambil 1 frame preview (kredit +1)
#define RP_CAM_FLASH  'N'   // payload: [0..255] LED flash GPIO4 (PWM)
#define RP_CAM_BAUD   'H'   // payload: [u32 LE baud]. CAM menjawab RP_CAM_BAUDOK lalu pindah kecepatan
// CAM -> S3
#define RP_CAM_FRAME  'M'   // payload: [seq][flags b0 awal b1 akhir b2 foto][u32 LE offset][data...]
#define RP_CAM_STAT   'm'   // 12 byte (lihat camSendStat di cam_module.h)
#define RP_CAM_BAUDOK 'h'   // payload: [u32 LE baud]

// kode resolusi (stabil; CAM memetakannya ke framesize_t milik library kamera)
#define RPR_QQVGA 0   // 160x120
#define RPR_QVGA  1   // 320x240
#define RPR_CIF   2   // 400x296
#define RPR_HVGA  3   // 480x320
#define RPR_VGA   4   // 640x480
#define RPR_SVGA  5   // 800x600
#define RPR_XGA   6   // 1024x768
#define RPR_HD    7   // 1280x720
#define RPR_SXGA  8   // 1280x1024
#define RPR_UXGA  9   // 1600x1200

// id untuk RP_CAM_SET
#define RPC_BRIGHT   0    // -2..2
#define RPC_CONTRAST 1    // -2..2
#define RPC_SATUR    2    // -2..2
#define RPC_SHARP    3    // -2..2
#define RPC_WBMODE   4    // 0 auto, 1 cerah, 2 mendung, 3 kantor, 4 rumah
#define RPC_EFFECT   5    // 0 normal, 1 negatif, 2 abu-abu, 3 merah, 4 hijau, 5 biru, 6 sepia
#define RPC_HMIRROR  6    // 0/1
#define RPC_VFLIP    7    // 0/1
#define RPC_NIGHT    8    // 0/1 mode malam (AEC2 + gain tinggi)
#define RPC_LENC     9    // 0/1 koreksi lensa
#define RPC_AELEVEL  10   // -2..2
#define RPC_PRVRES   11   // resolusi preview (RPR_*)
#define RPC_PRVQ     12   // kualitas JPEG preview (4..63, makin kecil makin bagus)
#define RPC_FPSCAP   13   // 0 = secepat mungkin, selain itu batas fps
#define RPC_COLORBAR 14   // 0/1 pola uji
#define RPC_COUNT    32

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
