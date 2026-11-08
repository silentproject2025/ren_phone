#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_deadzone_v3.py -- tambah kotak mati di lokasi ghost touch yg TERUKUR
dari log Serial: tepi kanan-tengah layar LANDSCAPE (x ~304-327, y ~115-120,
z ~36-42). Kotak dibuat lebih lebar dari titik ghost-nya supaya ghost yg
bergeser sedikit tetap kena.

Kotak ini cuma aktif saat layar landscape (SCR_W > SCR_H), krn koordinatnya
diukur di landscape. Di portrait kotak ini tidak dipakai.

Mau ubah ukuran/posisi: edit TOUCH_GHOST_X0/X1/Y0/Y1 di phone.ino.
Jalankan dari root repo. Idempotent. Butuh apply_touch_deadzone_v2.py.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

if "TOUCH_GHOST_RECT" in s:
    print("[skip] kotak ghost v3 sudah terpasang")
    sys.exit(0)

old1 = "#define TOUCH_DEBUG    1    // 1 = cetak log [touch] ke Serial Monitor, 0 = diam\n"
new1 = old1 + (
    "// TOUCH_GHOST_RECT (apply_touch_deadzone_v3.py): kotak mati di lokasi ghost terukur,\n"
    "// koordinat LANDSCAPE (320x240), tepi kanan-tengah. Aktif cuma saat landscape.\n"
    "#define TOUCH_GHOST_X0 285\n"
    "#define TOUCH_GHOST_X1 340\n"
    "#define TOUCH_GHOST_Y0 95\n"
    "#define TOUCH_GHOST_Y1 140\n"
)
old2 = "  if(TOUCH_MIN_Z>0 && (int)t.size<TOUCH_MIN_Z) return true;\n"
new2 = (
    "  if(SCR_W>SCR_H && x>=TOUCH_GHOST_X0 && x<=TOUCH_GHOST_X1 && y>=TOUCH_GHOST_Y0 && y<=TOUCH_GHOST_Y1) return true;\n"
    + old2
)
for o in (old1, old2):
    if s.count(o) != 1:
        sys.exit("ERROR: pola tidak cocok -- apply_touch_deadzone_v2.py belum terpasang?\n" + o[:60])
s = s.replace(old1, new1).replace(old2, new2)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] kotak ghost v3 terpasang: x %d..%d, y %d..%d (landscape)" % (285, 340, 95, 140))
