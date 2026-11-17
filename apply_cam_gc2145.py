#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_gc2145.py -- GC2145_SOFTJPEG_V1: dukungan kamera RHYX M21-45 (sensor GC2145, TANPA JPEG hardware).

Sisi ESP32-CAM (cam_bt_coprocessor/cam_module.h):
  * init JPEG dicoba dulu (OV2640/OV3660/OV5640 tidak berubah). Kalau driver menjawab "sensor tanpa JPEG"
    (ESP_ERR_NOT_SUPPORTED), kamera dinyalakan ulang dalam mode RGB565 (SVGA, 2 buffer PSRAM) dan tiap frame
    di-encode JPEG lewat frame2jpg() sebelum dikirim -> sisa pipeline (UART, dedup header, S3, Galeri, AVI) TIDAK berubah.
  * Semua pemanggilan fungsi sensor diberi cek NULL (driver GC2145 tidak mengisi semua fungsi; memanggil pointer NULL = crash).
  * Foto maks 800x600 (batas buffer RGB565 di PSRAM), dilaporkan ke S3 lewat byte status [11].
  * Status [0] bit5 = mode JPEG-software.
Sisi S3 (camera_renphone.ino): nama sensor "GC2145", resolusi maks < UXGA diterima, teks error diperbarui.
Docs: KAMERA.md.
Jalankan dari root repo. Idempotent. Flash DUA firmware (S3 + CAM).
"""
import sys, os

def load(p):
    if not os.path.exists(p): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % p)
    s = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    return s.replace("\r\n", "\n"), crlf

def save(p, s, crlf):
    if crlf: s = s.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(s)

def rep(s, old, new, tag):
    n = s.count(old)
    if n != 1: sys.exit("ERROR [%s]: anchor ketemu %d kali (harus 1). Dibatalkan, tidak ada file diubah." % (tag, n))
    return s.replace(old, new)

CM = "cam_bt_coprocessor/cam_module.h"
CR = "camera_renphone.ino"
M, mc = load(CM)
R, rc = load(CR)
D, dc = load("KAMERA.md")

if "GC2145_SOFTJPEG_V1" in M:
    print("[skip] GC2145_SOFTJPEG_V1 sudah terpasang"); sys.exit(0)

# ============================ cam_module.h ============================
M = rep(M, '#include "esp_camera.h"\n',
           '#include "esp_camera.h"\n#include "img_converters.h"   // frame2jpg() -- GC2145_SOFTJPEG_V1\n', "include")

M = rep(M, "//  Pin kamera = AI-Thinker ESP32-CAM.",
           "//  GC2145_SOFTJPEG_V1: sensor tanpa JPEG (RHYX M21-45 = GC2145) -> mode RGB565 + encode JPEG software (frame2jpg), lihat camSoft.\n"
           "//\n//  Pin kamera = AI-Thinker ESP32-CAM.", "doc")

M = rep(M, "static inline framesize_t camFs(uint8_t code) { return CAM_FS_MAP[code > 10 ? 1 : code]; }\n",
"""static inline framesize_t camFs(uint8_t code) { return CAM_FS_MAP[code > 10 ? 1 : code]; }

// GC2145_SOFTJPEG_V1 ------------------------------------------------------------------------------
// Fungsi sensor yang tidak diisi driver bernilai NULL (struct di-memset 0) -> selalu lewat makro ini.
#define CAM_SET(s, fn, ...) do { if ((s)->fn) (s)->fn((s), __VA_ARGS__); } while (0)
// Mode RGB565: buffer kamera dialokasikan sebesar CAM_SOFT_FS x 2 byte x fb_count (SVGA: 960 KB x 2 = 1,9 MB PSRAM).
// Mau foto lebih besar? Naikkan ke FRAMESIZE_XGA + RPR_XGA (3,1 MB, PSRAM CAM sisa ~0,9 MB) -- belum diuji.
#define CAM_SOFT_FS      FRAMESIZE_SVGA
#define CAM_SOFT_MAXRES  RPR_SVGA
// Warna ungu/hijau aneh (byte RGB565 tertukar)? Aktifkan: tambahkan -DCAM_SOFT_SWAP565 ke build_flags / #define di sini.
//#define CAM_SOFT_SWAP565
""", "macro")

M = rep(M, "static uint16_t            camPid = 0;\n",
           "static uint16_t            camPid = 0;\nstatic bool                camSoft = false;                      // GC2145_SOFTJPEG_V1: sensor RGB565, JPEG di-encode software\n", "var")

M = rep(M, "  b[0] = (camOpen ? 1 : 0) | (camStream ? 2 : 0) | (camBaudNow != RP_BAUD ? 4 : 0) | (camFlashLvl ? 8 : 0) | (camShotBusy ? 16 : 0);\n",
           "  b[0] = (camOpen ? 1 : 0) | (camStream ? 2 : 0) | (camBaudNow != RP_BAUD ? 4 : 0) | (camFlashLvl ? 8 : 0) | (camShotBusy ? 16 : 0) | (camSoft ? 32 : 0);\n", "stat")

M = rep(M, "//  [0] flags: b0 kamera nyala, b1 streaming, b2 baud tinggi, b3 flash nyala, b4 sedang foto\n",
           "//  [0] flags: b0 kamera nyala, b1 streaming, b2 baud tinggi, b3 flash nyala, b4 sedang foto, b5 mode JPEG-software (GC2145)\n", "statdoc")

HELP = """// ---------------- GC2145_SOFTJPEG_V1: helper ----------------
// kualitas sensor (4..63, kecil = bagus) -> kualitas encoder software (10..95, besar = bagus)
static inline uint8_t camJq(uint8_t q) { int j = 100 - (int)q * 3 / 2; return (uint8_t)(j < 10 ? 10 : (j > 95 ? 95 : j)); }

// set resolusi; di mode soft: jangan lewati ukuran buffer init, dan kalau sensor menolak -> QVGA
static void camSetFs(sensor_t* s, framesize_t fs) {
  if (!s || !s->set_framesize) return;
  if (camSoft && fs > CAM_SOFT_FS) fs = CAM_SOFT_FS;
  if (s->set_framesize(s, fs) != 0 && camSoft && fs != FRAMESIZE_QVGA) s->set_framesize(s, FRAMESIZE_QVGA);
}

// Ambil data JPEG dari fb. Sensor JPEG: pointer langsung ke fb. Sensor RGB565 (camSoft): encode software ->
// *own != nullptr dan WAJIB di-free() pemanggil (fb boleh dikembalikan lebih dulu). Return false = tak ada JPEG.
static bool camFbJpeg(camera_fb_t* fb, uint8_t q, const uint8_t** d, uint32_t* n, uint8_t** own) {
  *own = nullptr;
  if (fb->format == PIXFORMAT_JPEG) { *d = fb->buf; *n = (uint32_t)fb->len; return fb->len > 0; }
  if (!camSoft || fb->format != PIXFORMAT_RGB565) return false;
#ifdef CAM_SOFT_SWAP565
  { uint16_t* p = (uint16_t*)fb->buf; size_t cnt = fb->len / 2; for (size_t i = 0; i < cnt; i++) p[i] = __builtin_bswap16(p[i]); }
#endif
  uint8_t* o = nullptr; size_t ol = 0;
  if (!frame2jpg(fb, camJq(q), &o, &ol) || !o || ol == 0) { if (o) free(o); return false; }
  *own = o; *d = o; *n = (uint32_t)ol;
  return true;
}

"""
M = rep(M, "// ---------------- buka / tutup kamera ----------------\n", HELP + "// ---------------- buka / tutup kamera ----------------\n", "helpers")

M = rep(M, "  esp_err_t e = esp_camera_init(&c);\n",
"""  esp_err_t e = esp_camera_init(&c);
  camSoft = false;
  if (e == ESP_ERR_NOT_SUPPORTED) {            // GC2145_SOFTJPEG_V1: sensor tanpa JPEG (RHYX M21-45) -> RGB565 + encode software
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = CAM_SOFT_FS;
    c.fb_count = 2;
    e = esp_camera_init(&c);
    if (e == ESP_OK) camSoft = true;
  }
""", "init")

M = rep(M, "  camMaxRes = is3m ? RPR_QXGA : RPR_UXGA;\n",
           "  camMaxRes = camSoft ? CAM_SOFT_MAXRES : (is3m ? RPR_QXGA : RPR_UXGA);\n", "maxres")

M = rep(M, "  s->set_vflip(s, camVflipQuirk);\n  s->set_framesize(s, camFs(camPrvRes));\n  s->set_quality(s, camPrvQ);\n  camOpen = true;\n",
           "  CAM_SET(s, set_vflip, camVflipQuirk);\n  camSetFs(s, camFs(camPrvRes));\n  CAM_SET(s, set_quality, camPrvQ);\n  camOpen = true;\n", "open-set")

M = rep(M, '  Serial.printf("[CAM] kamera nyala PID=0x%x xclk=%uMHz heap %u->%u blokMax=%u\\n", (unsigned)camPid, (unsigned)camXclk,\n                (unsigned)h0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());\n',
           '  Serial.printf("[CAM] kamera nyala PID=0x%x xclk=%uMHz heap %u->%u blokMax=%u%s\\n", (unsigned)camPid, (unsigned)camXclk,\n                (unsigned)h0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), camSoft ? " [RGB565+JPEG software]" : "");\n', "log")

a = M.index("  switch (id) {\n    case RPC_BRIGHT:")
endm = "    default: break;   // RPC_FPSCAP dibaca langsung dari camCfg\n  }\n"
b = M.index(endm, a) + len(endm)
NEWSW = """  switch (id) {   // GC2145_SOFTJPEG_V1: semua lewat CAM_SET (cek NULL)
    case RPC_BRIGHT:   CAM_SET(s, set_brightness, camClampI(v + camBrightQuirk, -2, 2)); break;
    case RPC_CONTRAST: CAM_SET(s, set_contrast, v); break;
    case RPC_SATUR:    CAM_SET(s, set_saturation, camClampI(v + camSatQuirk, -2, 2)); break;
    case RPC_SHARP:    CAM_SET(s, set_sharpness, v); break;
    case RPC_WBMODE:   CAM_SET(s, set_whitebal, 1); CAM_SET(s, set_awb_gain, 1); CAM_SET(s, set_wb_mode, v); break;
    case RPC_EFFECT:   CAM_SET(s, set_special_effect, v); break;
    case RPC_HMIRROR:  CAM_SET(s, set_hmirror, v ? 1 : 0); break;
    case RPC_VFLIP:    CAM_SET(s, set_vflip, (v ? 1 : 0) ^ camVflipQuirk); break;
    case RPC_NIGHT:
      CAM_SET(s, set_aec2, v ? 1 : 0);
      CAM_SET(s, set_gainceiling, v ? GAINCEILING_128X : GAINCEILING_8X);
      CAM_SET(s, set_ae_level, v ? 1 : camCfgOr(RPC_AELEVEL, 0));
      break;
    case RPC_LENC:     CAM_SET(s, set_lenc, v ? 1 : 0); break;
    case RPC_AELEVEL:  CAM_SET(s, set_ae_level, v); break;
    case RPC_PRVRES:   if (!camShotBusy) camSetFs(s, camFs((uint8_t)v)); break;
    case RPC_PRVQ:     if (!camShotBusy) CAM_SET(s, set_quality, camPrvQ); break;
    case RPC_COLORBAR: CAM_SET(s, set_colorbar, v ? 1 : 0); break;
    default: break;   // RPC_FPSCAP dibaca langsung dari camCfg
  }
"""
M = M[:a] + NEWSW + M[b:]

M = rep(M, "  s->set_framesize(s, camFs(res));\n  s->set_quality(s, q);\n  if (flash) camFlashSet(flash);\n",
           "  camSetFs(s, camFs(res));\n  CAM_SET(s, set_quality, q);\n  if (flash) camFlashSet(flash);\n", "shot-set")

M = rep(M, """  if (fb) {
    if (fb->format == PIXFORMAT_JPEG && fb->len > 0) ok = camTxFrame(Serial2, ++camSeq, fb->buf, (uint32_t)fb->len, true, camAbortFn);
    esp_camera_fb_return(fb);
  }
""", """  if (fb) {
    const uint8_t* jd = nullptr; uint32_t jn = 0; uint8_t* own = nullptr;
    bool got = camFbJpeg(fb, q, &jd, &jn, &own);
    if (own) { esp_camera_fb_return(fb); fb = nullptr; }     // soft: buffer kamera dibebaskan selama JPEG dikirim
    if (got) ok = camTxFrame(Serial2, ++camSeq, jd, jn, true, camAbortFn);
    if (own) free(own);
    if (fb) esp_camera_fb_return(fb);
  }
""", "shot-grab")

M = rep(M, "  s->set_framesize(s, camFs(camPrvRes));\n  s->set_quality(s, camPrvQ);\n  camShotBusy = false;\n",
           "  camSetFs(s, camFs(camPrvRes));\n  CAM_SET(s, set_quality, camPrvQ);\n  camShotBusy = false;\n", "shot-restore")

M = rep(M, """        if (fb->format == PIXFORMAT_JPEG && fb->len > 0 && fb->len <= CAM_MAX_PREVIEW) {
          camTxFrame(Serial2, ++camSeq, fb->buf, (uint32_t)fb->len, false, camAbortFn, &camHdr);
          camBytesTx += (uint32_t)fb->len; fpsN++;
          if (cap > 0) nextFrameAt = millis() + 1000 / cap;
        }
        esp_camera_fb_return(fb);
""", """        const uint8_t* jd = nullptr; uint32_t jn = 0; uint8_t* own = nullptr;
        bool got = camFbJpeg(fb, camPrvQ, &jd, &jn, &own);
        if (own) { esp_camera_fb_return(fb); fb = nullptr; }   // soft: DMA kamera lanjut menangkap frame berikut selama JPEG dikirim
        if (got && jn <= CAM_MAX_PREVIEW) {
          camTxFrame(Serial2, ++camSeq, jd, jn, false, camAbortFn, &camHdr);
          camBytesTx += jn; fpsN++;
          if (cap > 0) nextFrameAt = millis() + 1000 / cap;
        }
        if (own) free(own);
        if (fb) esp_camera_fb_return(fb);
""", "task")

# ============================ camera_renphone.ino ============================
R = rep(R, '    case 0x96: return "OV9650";   case 0x77: return "OV7725";   case 0x76: return "OV7670";\n',
           '    case 0x96: return "OV9650";   case 0x77: return "OV7725";   case 0x76: return "OV7670";\n    case 0x2145: return "GC2145";   // GC2145_SOFTJPEG_V1 (RHYX M21-45)\n', "name")
R = rep(R, "return (camStatSeen && (camStat[0] & 1) && m >= RPR_UXGA && m <= RPR_QXGA) ? m : RPR_UXGA;",
           "return (camStatSeen && (camStat[0] & 1) && m >= RPR_QVGA && m <= RPR_QXGA) ? m : RPR_UXGA;   // GC2145_SOFTJPEG_V1: sensor RGB565 melapor SVGA", "maxres")
R = rep(R, 'Sensor ini tidak mendukung JPEG.\\n(Dukungan: OV2640/OV3660/OV5640)',
           'Sensor ini tidak didukung.\\n(OV2640/OV3660/OV5640/GC2145)', "err4")

# ============================ KAMERA.md ============================
D = D.rstrip("\n") + """

## RHYX M21-45 (sensor GC2145, tanpa JPEG) -- GC2145_SOFTJPEG_V1
Modul kamera berlabel **RHYX M21-45** di sebagian ESP32-CAM adalah sensor **GC2145**: hanya keluaran mentah RGB565, **tanpa JPEG hardware**.
`apply_cam_gc2145.py` membuat CAM mencoba JPEG dulu; kalau driver menjawab "sensor tanpa JPEG", kamera dinyalakan ulang dalam mode
RGB565 (SVGA, 2 buffer PSRAM ~1,9 MB) dan tiap frame di-encode JPEG di CAM (`frame2jpg`). Link UART, dedup header, S3, Galeri, AVI tidak berubah.
- Terdeteksi otomatis: toast "GC2145 - foto maks 800x600"; di log serial CAM: `[CAM] kamera nyala PID=0x2145 ... [RGB565+JPEG software]`.
- **Foto maks 800x600** (batas buffer RGB565 di PSRAM). Preview 320x240 paling realistis; 640x480 lebih lambat (encode software).
- fps lebih rendah dari OV2640 (kira-kira 5-12 fps di 320x240): sensor mengirim data mentah 2x lebih banyak + encode di CPU CAM.
- Beberapa kontrol sensor (mode malam, koreksi lensa, pola uji, dst.) bisa tidak berefek: fungsi yang tidak diisi driver GC2145 dilewati (bukan crash).
- Warna aneh (ungu/hijau)? Tambah `-DCAM_SOFT_SWAP565` (atau hapus komentar `//#define CAM_SOFT_SWAP565` di `cam_module.h`).
- Pin tetap AI-Thinker. Kalau kamera tidak terdeteksi (error 0x105) cek kabel flat dulu.
- **Belum dikompilasi dengan toolchain ESP32 dan belum dijalankan di perangkat** (logika diuji dengan mock di komputer).
"""

save(CM, M, mc); save(CR, R, rc); save("KAMERA.md", D, dc)
print("OK: GC2145_SOFTJPEG_V1 terpasang (cam_module.h, camera_renphone.ino, KAMERA.md). Flash DUA firmware: S3 + CAM.")
