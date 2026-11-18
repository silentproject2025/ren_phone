#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_link_diag.py -- bikin penyebab "link turun" kelihatan di layar + 1 perbaikan aman.

1) Toast "Link turun: X" sekarang menyebut ALASAN: (rusak) = >=3 frame rusak/5 dtk,
   (sunyi) = tidak ada frame valid 1,5 dtk setelah pindah baud, (CAM hilang) = CAM diam >2 dtk.
2) Jalur senyap yg mengunci link di 921k selamanya ("CAM tak menjawab" saat minta ganti baud)
   sekarang memunculkan toast "CAM tak jawab ganti baud".
3) Aturan "CAM hilang >2 dtk" tidak dipakai selagi foto sedang diproses (kamera sengaja berhenti
   kirim preview saat ambil foto resolusi tinggi) -- supaya foto tidak memicu turun baud.
Menyentuh camera_renphone.ino saja. Jalankan dari root repo. Idempotent & atomik.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "CAM_LINK_DIAG_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label, count=1):
    global s
    if s.count(old) != count:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("static void camLinkStepDown(uint32_t now, int fromRung, bool alive) {\n",
    "static void camLinkStepDown(uint32_t now, int fromRung, bool alive, const char* why) {   // CAM_LINK_DIAG_V1: +why\n", "sig")
rep('  char m[40]; snprintf(m, sizeof(m), "Link turun: %s", nxt <= 0 ? "921 kbaud" : CAM_LINK_NAME[nxt]);\n',
    '  char m[64]; snprintf(m, sizeof(m), "Link turun: %s (%s)", nxt <= 0 ? "921 kbaud" : CAM_LINK_NAME[nxt], why);\n', "toast")
rep("      camLinkStepDown(now, from, false);\n    }\n  }\n  if (camBaudOkFlag)",
    '      camLinkStepDown(now, from, false, "sunyi");\n    }\n  }\n  if (camBaudOkFlag)', "sunyi")
rep("camBaudReq = false; camLinkFailed = true; }   // CAM tak menjawab (firmware lama?)",
    'camBaudReq = false; camLinkFailed = true; showToast("CAM tak jawab ganti baud", 2500); }   // CAM tak menjawab (firmware lama?)', "nojawab")
rep("  if (camOpened && camBaudCur != RP_BAUD && !camBaudSwitchedAt && now - camLastRxMs > 2000) {\n    int from = camRungOf(camBaudCur);\n    Serial1.updateBaudRate(RP_BAUD); camBaudCur = RP_BAUD;\n    camLinkStepDown(now, from, false);\n",
    "  if (camOpened && camBaudCur != RP_BAUD && !camBaudSwitchedAt && !camShotPending && now - camLastRxMs > 2000) {   // CAM_LINK_DIAG_V1: !camShotPending\n    int from = camRungOf(camBaudCur);\n    Serial1.updateBaudRate(RP_BAUD); camBaudCur = RP_BAUD;\n    camLinkStepDown(now, from, false, \"CAM hilang\");\n", "hilang")
rep("{ camLinkStepDown(now, camRungOf(camBaudCur), true); camBadAt = now;",
    '{ camLinkStepDown(now, camRungOf(camBaudCur), true, "rusak"); camBadAt = now;', "rusak")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] CAM_LINK_DIAG_V1 terpasang di camera_renphone.ino")
