#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cam_st7796_layout.py -- layar Kamera mengikuti ukuran layar (ST7796 480x320) + preview 480x320.

Masalah: UI Kamera ditulis untuk layar 320x240. Rail kanan dikunci di x=266 dan tombolnya di y tetap,
jadi di ST7796 (SCR_W=480, SCR_H=320) rail nyangkut di tengah layar, ada area kosong di kanan, dan
preview 320x240 hanya mengisi sepotong layar.

1) CAM_RAIL_X = SCR_W - 54 (di layar 320 hasilnya tetap 266 -> ILI9341 tidak berubah sama sekali).
2) Tombol rail (rana/flash/atur) digeser ke tengah secara vertikal: CAM_VOFF = (SCR_H-240)/2 (0 di 320x240).
   Area sentuhnya ikut digeser.
3) Opsi preview baru "480x320": piksel-per-piksel penuh di layar ST7796. Hanya muncul di menu Atur
   kalau layar >= 480 lebar. Default tetap 320x240 (frame 480x320 ~2x lebih besar -> beban link lebih
   berat; pilih sendiri di Atur > Resolusi preview kalau link sudah stabil).
   Firmware CAM, buffer slot, AVI dan deteksi gerak sudah mendukung 480x320 -> cam_bt_coprocessor
   TIDAK perlu di-flash ulang.
Menyentuh camera_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "camera_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "CAM_ST7796_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("static const uint8_t        CAM_PRV_CODE[3]  = {RPR_QQVGA, RPR_QVGA, RPR_VGA};",
    "static const uint8_t        CAM_PRV_CODE[4]  = {RPR_QQVGA, RPR_QVGA, RPR_VGA, RPR_HVGA};   // CAM_ST7796_V1: +HVGA (ditaruh di akhir supaya config tersimpan tetap valid)", "code")
rep('static const char*    const CAM_PRV_NAME[3]  = {"160x120 (2x)", "320x240", "640x480 (1/2)"};',
    'static const char*    const CAM_PRV_NAME[4]  = {"160x120 (2x)", "320x240", "640x480 (1/2)", "480x320"};', "name")
rep("(camCfg.prv > 2 ? 2 : camCfg.prv)]", "(camCfg.prv > 3 ? 3 : camCfg.prv)]", "clamp")
rep("camCfg.prv = camStepv(camCfg.prv, dir, 0, 2, 1, true);",
    "camCfg.prv = camStepv(camCfg.prv, dir, 0, SCR_W >= 480 ? 3 : 2, 1, true);", "step")
rep("#define CAM_RAIL_X 266\n",
    "#define CAM_RAIL_X (SCR_W - 54)                 // CAM_ST7796_V1: 266 di layar 320 (tidak berubah), 426 di ST7796\n"
    "#define CAM_VOFF   ((SCR_H - 240) / 2)          // CAM_ST7796_V1: 0 di 320x240, 40 di 480x320 -> tombol rana dst. ke tengah\n", "rail")
rep("int cx = CAM_RAIL_X + 27, cy = 112;", "int cx = CAM_RAIL_X + 27, cy = 112 + CAM_VOFF;", "cy")
rep("camBtn(s, CAM_RAIL_X + 4, 154, 46, 26,", "camBtn(s, CAM_RAIL_X + 4, 154 + CAM_VOFF, 46, 26,", "flash")
rep('camBtn(s, CAM_RAIL_X + 4, 186, 46, 26, "Atur", false);', 'camBtn(s, CAM_RAIL_X + 4, 186 + CAM_VOFF, 46, 26, "Atur", false);', "atur")
rep("camIn(x, y, CAM_RAIL_X, 82, SCR_W - CAM_RAIL_X, 62)",  "camIn(x, y, CAM_RAIL_X, 82 + CAM_VOFF, SCR_W - CAM_RAIL_X, 62)", "hit-rana")
rep("camIn(x, y, CAM_RAIL_X, 152, SCR_W - CAM_RAIL_X, 30)", "camIn(x, y, CAM_RAIL_X, 152 + CAM_VOFF, SCR_W - CAM_RAIL_X, 30)", "hit-flash")
rep("camIn(x, y, CAM_RAIL_X, 184, SCR_W - CAM_RAIL_X, 30)", "camIn(x, y, CAM_RAIL_X, 184 + CAM_VOFF, SCR_W - CAM_RAIL_X, 30)", "hit-atur")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] CAM_ST7796_V1 terpasang di camera_renphone.ino")
