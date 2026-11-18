#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cpu240.py (CPU240_V1) -- CPU ESP32-S3 selalu 240 MHz (batas maksimum chip).
Dulu: CPU_MHZ_NORMAL=160, naik ke 240 hanya saat animasi/game (uiBoostKick) lalu turun lagi.
Sekarang: CPU_MHZ_NORMAL=240, dan setup() memaksa 240 MHz di awal. uiBoostKick()/enterGameMode()
tetap jalan tapi jadi no-op (240 -> 240), jadi tidak ada lagi ganti clock di tengah animasi.
Harga: lebih panas & boros baterai saat idle. Untuk balik: ganti CPU_MHZ_NORMAL ke 160.
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "CPU240_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

old = "#define CPU_MHZ_NORMAL 160\n"
if s.count(old) != 1:
    sys.exit("ERROR: '#define CPU_MHZ_NORMAL 160' tidak ketemu / tidak unik. Dibatalkan.")
s = s.replace(old, "#define CPU_MHZ_NORMAL 240   // CPU240_V1 (apply_cpu240.py): dulu 160, CPU selalu di batas maksimum chip\n")

old = "void setup(){\n  Serial.begin(115200);\n"
if s.count(old) != 1:
    sys.exit("ERROR: awal setup() tidak sama dgn yg diharapkan. Dibatalkan.")
s = s.replace(old, old + "  setCpuFrequencyMhz(CPU_MHZ_NORMAL);   // CPU240_V1: pastikan 240 MHz sejak awal (apa pun default board)\n")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: CPU dipatok 240 MHz")
