#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_still_grow.py -- buffer foto DINAMIS (membesar sendiri) + perbaikan ulang-foto.
Butuh apply_cam_still_big.py sudah terpasang.

1) Foto "kebesaran" -> buffer digandakan otomatis (maks 4 MB, selama PSRAM muat) lalu foto diulang.
   Tidak perlu menebak ukuran dulu: ukuran foto tergantung adegan & kualitas, jadi cara paling jujur
   adalah membesar saat terbukti kurang. Tidak memakai jatah ulang (2x) foto rusak.
   Pembesaran hanya terjadi di antara foto (tidak pernah di tengah transfer -> aman bagi UART).
2) PERBAIKAN cacat di apply_cam_link_rate.py: ulang foto sebelumnya meminta ulang SEKETIKA, padahal CAM
   terus mengirim sisa potongan foto lama sampai habis. Potongan sisa itu menabrak foto baru
   (potongan bukan-pertama saat menunggu) dan menggagalkannya lagi, jadi ulangan sering sia-sia.
   Sekarang ulangan menunggu: >=500 ms setelah gagal DAN tidak ada frame masuk selama >=400 ms
   (sisa foto lama sudah habis), baru minta foto lagi.
Menyentuh camera_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "CAM_STILL_GROW_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
if "CAM_STILL_BIG_V1" not in s: sys.exit("ERROR: jalankan apply_cam_still_big.py dulu.")

def rep(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("static uint32_t  camOverBase = 0;",
    "static uint32_t  camRetryAt = 0;      // CAM_STILL_GROW_V1: != 0 = menunggu sisa foto lama habis, lalu minta foto lagi\n"
    "static uint32_t  camOverBase = 0;", "decl")

rep("  camBuffersOk = false; camStillTried = 0;\n", "  camBuffersOk = false; camStillTried = 0; camRetryAt = 0;   // CAM_STILL_GROW_V1\n", "free-reset2")

rep("// ---------------- SD ----------------\n",
"// CAM_STILL_GROW_V1: gandakan buffer foto (maks 4 MB). Dipanggil HANYA saat foto idle (di antara foto).\n"
"// Buffer lama dibebaskan dulu (dua buffer besar sekaligus belum tentu muat); kalau tak ada yg lebih besar\n"
"// muat, ukuran lama diambil lagi. true = kapasitas benar2 bertambah.\n"
"static bool camGrowStill() {\n"
"  if (!camBuffersOk || camRx.stillState == CAMRX_STILL_RECV) return false;\n"
"  uint32_t cap = camRx.stillCap, top = 4096u * 1024;\n"
"  if (cap >= top) return false;\n"
"  uint32_t want = (cap * 2 < top) ? cap * 2 : top;\n"
"  camRx.attachStill(nullptr, 0);\n"
"  uint8_t* old = camStillBuf; camStillBuf = nullptr;\n"
"  if (old) heap_caps_free(old);\n"
"  uint8_t* nb = nullptr; uint32_t got = 0;\n"
"  for (uint32_t sz = want; sz >= cap + 128u * 1024; sz -= 128u * 1024) {\n"
"    nb = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n"
"    if (nb) { got = sz; break; }\n"
"  }\n"
"  if (!nb && cap) { nb = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); got = nb ? cap : 0; }\n"
"  camStillBuf = nb; camRx.attachStill(nb, got);\n"
"  return got > cap;\n"
"}\n"
"\n"
"// ---------------- SD ----------------\n", "grow-fn")

rep("  if (!camShotPending) return;\n  uint8_t st = camRx.stillState;\n",
"  if (!camShotPending) return;\n"
"  if (camRetryAt) {   // CAM_STILL_GROW_V1: tunggu sisa potongan foto lama habis (CAM terus kirim sampai selesai), baru minta ulang\n"
"    if ((int32_t)(now - camRetryAt) >= 0 && (int32_t)(now - camLastRxMs) >= 400) { camRetryAt = 0; camShotPending = false; camShootNow(); }\n"
"    else if ((int32_t)(now - camShotDeadline) >= 0) { camRetryAt = 0; camShotPending = false; camBurstLeft = 0; camShotRetry = 0; showToast(\"Foto gagal (timeout)\", 2200); vibError(); camNeedDraw = true; }\n"
"    return;\n"
"  }\n"
"  uint8_t st = camRx.stillState;\n", "tick-wait")

rep(
"    if (!over && camShotRetry < 2) {   // CAM_LINK_RATE_V1: coba lagi otomatis\n"
"      camShotRetry++; camShotPending = false; camRx.disarmStill();\n"
"      showToast(\"Foto rusak, mengulang...\", 900);\n"
"      camShootNow(); return;\n"
"    }\n",
"    if (over) {   // CAM_STILL_GROW_V1: kebesaran -> besarkan buffer lalu ulang (tidak memakai jatah ulang foto rusak)\n"
"      camRx.disarmStill();\n"
"      if (camGrowStill()) {\n"
"        camRetryAt = now + 500; camShotDeadline = now + 15000;\n"
"        showToast(\"Buffer foto diperbesar, mengulang...\", 1500); return;\n"
"      }\n"
"    }\n"
"    if (!over && camShotRetry < 2) {   // CAM_LINK_RATE_V1: foto rusak di jalur -> ulang, tapi tunggu sisa foto lama habis dulu\n"
"      camShotRetry++; camRx.disarmStill();\n"
"      camRetryAt = now + 500; camShotDeadline = now + 15000;\n"
"      showToast(\"Foto rusak, mengulang...\", 1200); return;\n"
"    }\n", "fail-branch")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] CAM_STILL_GROW_V1 terpasang di camera_renphone.ino")
