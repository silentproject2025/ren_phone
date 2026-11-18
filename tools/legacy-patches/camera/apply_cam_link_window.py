#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_link_window.py -- link tidak turun gara-gara satu burst singkat + penyebab "rusak" dirinci.
Butuh apply_cam_link_rate.py sudah terpasang.

Temuan: "rusak 3 dari 19/20/30" = 3 frame rusak dalam ~1-1,5 dtk lalu link turun. Rusaknya mengumpul
(burst), bukan tersebar. Aturan lama menilai begitu rusak mencapai 3, padahal baru sedikit frame yg
lewat, jadi satu burst sesaat langsung terbaca ~15% dan link turun. Karena rusaknya TIDAK hilang
walau baud turun (turun terus sampai 921k), penyebabnya bukan kecepatan kabel.

1) Penilaian dilakukan di AKHIR jendela 5 dtk (bukan begitu rusak = 3): 3 rusak dari ~100 frame = 3%
   -> link tetap. Link hanya turun kalau rusak >= 3 DAN >= 10% dari frame sepanjang 5 dtk penuh.
2) Toast merinci penyebab (hitungan sejak kamera dibuka):
     a = potongan AKHIR frame hilang (frame baru datang sebelum yg lama selesai)
     g = ada CELAH di tengah frame (potongan di tengah hilang / urutan loncat)
     e = frame lengkap tapi gagal cek JPEG
   contoh: "Link turun: 4 Mbaud (rusak 12/98 a7 g5 e0)".
Menyentuh camera_renphone.ino & cam_rx.h. Idempotent & atomik.
"""
import sys, os
FILES = ["camera_renphone.ino", "cam_rx.h"]
for f in FILES:
    if not os.path.exists(f): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % f)
def load(p):
    t = open(p, encoding="utf-8", newline="").read(); return t.replace("\r\n", "\n"), "\r\n" in t
def save(p, t, crlf):
    open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n") if crlf else t)
cam, cam_crlf = load("camera_renphone.ino")
rx, rx_crlf = load("cam_rx.h")
if "CAM_LINK_WINDOW_V1" in cam and "CAM_LINK_WINDOW_V1" in rx: print("[skip] sudah dipatch"); sys.exit(0)
if "CAM_LINK_RATE_V1" not in cam: sys.exit("ERROR: jalankan apply_cam_link_rate.py dulu.")
if ("CAM_LINK_WINDOW_V1" in cam) != ("CAM_LINK_WINDOW_V1" in rx): sys.exit("ERROR: patch separuh terpasang. Periksa manual.")

def rep(text, old, new, label):
    if text.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, text.count(old), old[:90]))
    return text.replace(old, new)

# ---- cam_rx.h: hitung penyebab ----
rx = rep(rx, "  uint32_t  bad = 0;              // frame rusak (potongan hilang / checksum JPEG gagal)\n",
         "  uint32_t  bad = 0;              // frame rusak (potongan hilang / checksum JPEG gagal)\n"
         "  uint32_t  badFirst = 0, badGap = 0, badEoi = 0, badStill = 0;   // CAM_LINK_WINDOW_V1: rincian penyebab bad (a/g/e/foto)\n", "rx/decl")
rx = rep(rx, "off != stillLen) { sOn = false; stillState = CAMRX_STILL_FAIL; bad++; return false; }",
         "off != stillLen) { sOn = false; stillState = CAMRX_STILL_FAIL; bad++; badStill++; return false; }", "rx/still1")
rx = rep(rx, "else { stillState = CAMRX_STILL_FAIL; bad++; }",
         "else { stillState = CAMRX_STILL_FAIL; bad++; badStill++; }", "rx/still2")
rx = rep(rx, "slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; }   // NYX-CAMFREEZE-V1",
         "slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; badFirst++; }   // NYX-CAMFREEZE-V1", "rx/first")
rx = rep(rx, "slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; }\n      rxOn = false; rxSlot = -1;\n      return false;",
         "slotSt[rxSlot] = CAMRX_FREE; CAMRX_EXIT(); bad++; badGap++; }\n      rxOn = false; rxSlot = -1;\n      return false;", "rx/gap")
rx = rep(rx, "slotSt[s] = CAMRX_FREE; CAMRX_EXIT();\n      bad++;\n",
         "slotSt[s] = CAMRX_FREE; CAMRX_EXIT();\n      bad++; badEoi++;\n", "rx/eoi")

# ---- camera_renphone.ino: nilai di akhir jendela + toast rinci ----
old = (
"    if (now - camBadAt >= 5000) { camBadAt = now; camBadBase = camRx.bad; camFrBase = camRx.frames; }\n"
"    else {\n"
"      uint32_t bd = camRx.bad - camBadBase, fd = camRx.frames - camFrBase;\n"
"      if (bd >= 3 && bd * 10 >= bd + fd) {   // CAM_LINK_RATE_V1: >=3 rusak DAN >=10% dari semua frame\n"
"        char w[24]; snprintf(w, sizeof(w), \"rusak %lu/%lu\", (unsigned long)bd, (unsigned long)(bd + fd));\n"
"        camLinkStepDown(now, camRungOf(camBaudCur), true, w); camBadAt = now; camBadBase = camRx.bad; camFrBase = camRx.frames;\n"
"      }\n"
"    }\n")
new = (
"    if (now - camBadAt >= 5000) {   // CAM_LINK_WINDOW_V1: nilai di AKHIR jendela 5 dtk, bukan begitu rusak mencapai 3 (burst sesaat jangan dihitung 3/19)\n"
"      uint32_t bd = camRx.bad - camBadBase, fd = camRx.frames - camFrBase;\n"
"      bool step = bd >= 3 && bd * 10 >= bd + fd;   // >=3 rusak DAN >=10% dari semua frame\n"
"      char w[40]; snprintf(w, sizeof(w), \"rusak %lu/%lu a%lu g%lu e%lu\", (unsigned long)bd, (unsigned long)(bd + fd),\n"
"                           (unsigned long)camRx.badFirst, (unsigned long)camRx.badGap, (unsigned long)camRx.badEoi);\n"
"      if (step) camLinkStepDown(now, camRungOf(camBaudCur), true, w);\n"
"      camBadAt = now; camBadBase = camRx.bad; camFrBase = camRx.frames;\n"
"    }\n")
cam = rep(cam, old, new, "cam/window")
save("camera_renphone.ino", cam, cam_crlf); save("cam_rx.h", rx, rx_crlf)
print("[ok] CAM_LINK_WINDOW_V1 terpasang di camera_renphone.ino & cam_rx.h")
