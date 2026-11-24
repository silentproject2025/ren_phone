#!/usr/bin/env python3
# apply_kb_bigger_patch.py -- KB_BIGGER_V1: tuts keyboard layar lebih tinggi (kurangi salah tekan)
# Jalankan dari root repo:  python3 tools/patches/apply_kb_bigger_patch.py
# Tinggi tuts 22 px -> 24 px (layar pendek, mis. 240) atau 28 px (layar >= 300 px). Lebar tuts tidak berubah.
# kbY() dan semua layar yang memakainya menyesuaikan sendiri.
import os, sys
FN = os.path.join("firmware", "phone", "phone.ino")
if not os.path.exists(FN): sys.exit("GAGAL: jalankan dari root repo")
s = open(FN, encoding="utf-8").read()
if "KB_BIGGER_V1" in s: print("sudah terpasang"); sys.exit(0)
old = "int kbKeyH(){ return 22; }"
if s.count(old) != 1: sys.exit("GAGAL: pola kbKeyH tidak ditemukan tepat 1x")
s = s.replace(old, "int kbKeyH(){ return SCR_H >= 300 ? 28 : 24; }   // KB_BIGGER_V1 (dulu 22)")
open(FN, "w", encoding="utf-8").write(s)
CF = "CHANGELOG.md"
if os.path.exists(CF):
    c = open(CF, encoding="utf-8").read()
    if "## Unreleased\n" in c and "KB_BIGGER_V1" not in c:
        line = "- Improved: the on-screen keyboard keys are taller (22 px to 24 px on short screens, 28 px on screens 300 px or taller) so fewer keys are mistyped; the keyboard takes about 8 to 24 px more height. Script: `tools/patches/apply_kb_bigger_patch.py` (KB_BIGGER_V1)\n"
        open(CF, "w", encoding="utf-8").write(c.replace("## Unreleased\n", "## Unreleased\n" + line, 1))
print("OK: keyboard lebih tinggi ->", FN)
