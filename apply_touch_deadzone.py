#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_deadzone.py -- zona mati sentuh di pojok kiri-bawah (solusi sementara
ghost touch akibat kaca retak). Sentuhan di kotak TOUCH_DEAD_W x TOUCH_DEAD_H px
di pojok kiri-bawah diabaikan. Tombol "Kembali" (x 4..72) tetap bisa ditekan di
bagian kanannya (x >= 40). Untuk menonaktifkan: set TOUCH_DEAD_W jadi 0, atau
git revert commit patch ini setelah layar diganti.
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")

if "TOUCH_DEAD_W" in s:
    print("[skip] zona mati sudah terpasang"); sys.exit(0)

old = ("bool getInputPoint(lgfx::touch_point_t &tp){\n"
       "  return display.getTouch(&tp);\n"
       "}\n")
new = ("// ---- zona mati sentuh (apply_touch_deadzone.py): sementara, kaca retak di pojok kiri-bawah ----\n"
       "#define TOUCH_DEAD_W 40   // lebar zona mati (px), 0 = nonaktif\n"
       "#define TOUCH_DEAD_H 40   // tinggi zona mati (px)\n\n"
       "bool getInputPoint(lgfx::touch_point_t &tp){\n"
       "  if(!display.getTouch(&tp)) return false;\n"
       "  if(tp.x < TOUCH_DEAD_W && tp.y >= SCR_H - TOUCH_DEAD_H) return false;\n"
       "  return true;\n"
       "}\n")
if s.count(old) != 1:
    sys.exit("ERROR: getInputPoint() asli tidak cocok (mungkin sudah diubah patch lain, mis. filter ghost touch).")
s = s.replace(old, new)
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] zona mati sentuh terpasang (%s)" % "40x40 px pojok kiri-bawah")
