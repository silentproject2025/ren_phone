#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_still_big.py -- foto resolusi besar tidak lagi "terlalu besar" + pesan gagal yang jujur.
Butuh apply_cam_link_rate.py sudah terpasang.

Penyebab: buffer foto di S3 cuma 448 KB (sensor 2MP) / 768 KB (3MP). Foto 1600x1200 kualitas tinggi
bisa melewatinya -> transfer dibatalkan ("Foto rusak/terlalu besar"). Mengulang tidak menolong
karena ukurannya sama.

1) Buffer foto dinaikkan setelah sensor terdeteksi, dgn cadangan kalau PSRAM tidak cukup:
     2MP : 1280 KB -> 1024 -> 768 -> 448 KB      3MP : 2048 KB -> 1536 -> 1024 -> 768 KB
   Dicoba SEKALI per sesi kamera (tidak malloc berulang). Preview tidak terpengaruh (3 slot tetap).
2) Gagal karena ukuran -> TIDAK diulang (percuma) + toast "Foto kebesaran: naikkan Q/kecilkan res".
   Gagal karena data rusak di jalur -> tetap diulang 2x, toast akhirnya "Foto rusak di jalur, coba lagi".
Menyentuh camera_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "CAM_STILL_BIG_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
if "CAM_LINK_RATE_V1" not in s: sys.exit("ERROR: jalankan apply_cam_link_rate.py dulu.")

def rep(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("static uint8_t   camShotRetry = 0;",
    "static uint8_t   camStillTried = 0;   // CAM_STILL_BIG_V1: 0 = belum, 1 = sudah coba utk sensor 2MP, 2 = utk 3MP\n"
    "static uint32_t  camOverBase = 0;     // CAM_STILL_BIG_V1: camRx.oversize saat foto diminta (beda = gagal krn ukuran)\n"
    "static uint8_t   camShotRetry = 0;", "decl")

rep("  camBuffersOk = false;\n  delay(25);                                   // beri musTask",
    "  camBuffersOk = false; camStillTried = 0;\n  delay(25);                                   // beri musTask", "free-reset")

old_fn = (
"static void camEnsureStill() {\n"
"  uint32_t want = (camMaxRes() >= RPR_QXGA) ? CAM_STILL_SZ_BIG : CAM_STILL_SZ;\n"
"  if (!camBuffersOk || camShotPending || camRx.stillState == CAMRX_STILL_RECV || camRx.stillCap >= want) return;\n"
"  uint8_t* nb = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n"
"  if (!nb) return;\n"
"  camRx.attachStill(nullptr, 0);\n"
"  uint8_t* old = camStillBuf;\n"
"  camStillBuf = nb;\n"
"  camRx.attachStill(nb, want);\n"
"  if (old) heap_caps_free(old);\n"
"}\n")
new_fn = (
"static void camEnsureStill() {   // CAM_STILL_BIG_V1: buffer foto lebih besar + cadangan, sekali per sesi\n"
"  bool big = camMaxRes() >= RPR_QXGA;\n"
"  uint8_t tag = big ? 2 : 1;\n"
"  if (!camBuffersOk || camShotPending || camRx.stillState == CAMRX_STILL_RECV || camStillTried == tag) return;\n"
"  camStillTried = tag;\n"
"  static const uint32_t S2[4] = {1280u * 1024, 1024u * 1024, 768u * 1024, 448u * 1024};\n"
"  static const uint32_t S3[4] = {2048u * 1024, 1536u * 1024, 1024u * 1024, 768u * 1024};\n"
"  const uint32_t* L = big ? S3 : S2;\n"
"  for (int i = 0; i < 4; i++) {\n"
"    if (L[i] <= camRx.stillCap) break;                       // buffer yang ada sudah segini atau lebih besar\n"
"    uint8_t* nb = (uint8_t*)heap_caps_malloc(L[i], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n"
"    if (!nb) continue;                                       // PSRAM tidak cukup -> coba ukuran di bawahnya\n"
"    camRx.attachStill(nullptr, 0);\n"
"    uint8_t* old = camStillBuf;\n"
"    camStillBuf = nb;\n"
"    camRx.attachStill(nb, L[i]);\n"
"    if (old) heap_caps_free(old);\n"
"    break;\n"
"  }\n"
"}\n")
rep(old_fn, new_fn, "ensure")

rep("  camFreezeUntil = 0;\n  camRx.armStill();\n",
    "  camFreezeUntil = 0;\n  camOverBase = camRx.oversize;   // CAM_STILL_BIG_V1\n  camRx.armStill();\n", "arm")
rep("    if (camShotRetry < 2) {   // CAM_LINK_RATE_V1: coba lagi otomatis\n",
    "    bool over = (st == CAMRX_STILL_FAIL) && camRx.oversize != camOverBase;   // CAM_STILL_BIG_V1: gagal krn ukuran -> jangan diulang\n"
    "    if (!over && camShotRetry < 2) {   // CAM_LINK_RATE_V1: coba lagi otomatis\n", "retry-cond")
rep('showToast(st == CAMRX_STILL_FAIL ? "Foto rusak/terlalu besar" : "Foto gagal (timeout)", 2200);',
    'showToast(over ? "Foto kebesaran: naikkan Q/kecilkan res" : (st == CAMRX_STILL_FAIL ? "Foto rusak di jalur, coba lagi" : "Foto gagal (timeout)"), 2400);', "toast")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] CAM_STILL_BIG_V1 terpasang di camera_renphone.ino")
