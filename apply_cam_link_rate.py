#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_link_rate.py -- link kamera tidak gampang turun + foto diulang otomatis kalau rusak.
Butuh apply_cam_link_diag.py sudah terpasang.

1) Turun baud karena "rusak" sekarang butuh >=3 frame rusak DAN >=10% dari frame di jendela 5 dtk
   (dulu cukup 3 frame rusak, berapa pun jumlah frame yg lancar). Preview 20 fps = ~100 frame/5 dtk,
   jadi 3 frame hilang (3%) dulu sudah menurunkan link padahal preview kelihatan mulus.
   Toast sekarang memuat angkanya: "Link turun: 4 Mbaud (rusak 12/95)" = 12 rusak dari 95.
2) Foto yang rusak/timeout diulang otomatis sampai 2x sebelum menyerah (satu bit hilang di transfer
   foto besar membuang seluruh foto; preview tidak terasa karena cuma melewati frame itu).
Menyentuh camera_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "CAM_LINK_RATE_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
if "CAM_LINK_DIAG_V1" not in s: sys.exit("ERROR: jalankan apply_cam_link_diag.py dulu.")

def rep(old, new, label, count=1):
    global s
    if s.count(old) != count:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("camBadBase = 0, camBadAt = 0;",
    "camBadBase = 0, camBadAt = 0, camFrBase = 0;   /* CAM_LINK_RATE_V1: frames utuh di awal jendela */", "decl")
n = s.count("camBadBase = camRx.bad;")
if n < 4: sys.exit("ERROR [reset]: hanya %d kemunculan camBadBase = camRx.bad;" % n)
s = s.replace("camBadBase = camRx.bad;", "camBadBase = camRx.bad; camFrBase = camRx.frames;")

rep('    else if ((uint32_t)(camRx.bad - camBadBase) >= 3) { camLinkStepDown(now, camRungOf(camBaudCur), true, "rusak"); camBadAt = now; camBadBase = camRx.bad; camFrBase = camRx.frames; }\n',
    '    else {\n'
    '      uint32_t bd = camRx.bad - camBadBase, fd = camRx.frames - camFrBase;\n'
    '      if (bd >= 3 && bd * 10 >= bd + fd) {   // CAM_LINK_RATE_V1: >=3 rusak DAN >=10% dari semua frame\n'
    '        char w[24]; snprintf(w, sizeof(w), "rusak %lu/%lu", (unsigned long)bd, (unsigned long)(bd + fd));\n'
    '        camLinkStepDown(now, camRungOf(camBaudCur), true, w); camBadAt = now; camBadBase = camRx.bad; camFrBase = camRx.frames;\n'
    '      }\n'
    '    }\n', "rate")

rep("static bool      camShotPending = false;\n",
    "static bool      camShotPending = false;\nstatic uint8_t   camShotRetry = 0;   // CAM_LINK_RATE_V1: ulang foto rusak maks 2x\n", "retry-decl")
rep("static void camStartShoot() {\n",
    "static void camStartShoot() {\n  camShotRetry = 0;\n", "retry-reset")
rep("    camShotPending = false;\n    if (ok) {\n      strncpy(camLastPhoto",
    "    camShotPending = false; camShotRetry = 0;\n    if (ok) {\n      strncpy(camLastPhoto", "retry-ok")
rep("    camShotPending = false; camBurstLeft = 0; camRx.disarmStill();\n    showToast(st == CAMRX_STILL_FAIL",
    "    if (camShotRetry < 2) {   // CAM_LINK_RATE_V1: coba lagi otomatis\n"
    "      camShotRetry++; camShotPending = false; camRx.disarmStill();\n"
    "      showToast(\"Foto rusak, mengulang...\", 900);\n"
    "      camShootNow(); return;\n"
    "    }\n"
    "    camShotRetry = 0;\n"
    "    camShotPending = false; camBurstLeft = 0; camRx.disarmStill();\n    showToast(st == CAMRX_STILL_FAIL", "retry-fail")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] CAM_LINK_RATE_V1 terpasang di camera_renphone.ino")
