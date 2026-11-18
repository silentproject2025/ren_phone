#!/usr/bin/env python3
"""Haluskan animasi TUTUP app (navPlayZoomClose) di phone.ino.

Masalah lama:
  - ez = q*q  -> kecepatan MAKSIMAL di frame pertama (tanpa ease-in), jadi gerakan "disentak".
  - Fase besar (layar penuh -> balok ikon, ez 1 -> 0.5) cuma ~76 ms dari total 260 ms,
    sisanya ~184 ms buat balok kecil yg melandai. Gerakan yg paling kelihatan jadi kecepetan.
Perbaikan:
  - ez = 1 - navEase(u)  (cubic ease-in-out: start halus, tengah cepat, mendarat halus)
  - durasi 260 -> 360 ms (fase besar jadi ~180 ms)
Pakai:  python3 apply_close_anim_smooth.py [phone.ino]
"""
import sys
p = sys.argv[1] if len(sys.argv) > 1 else "phone.ino"
s = open(p, encoding="utf-8").read()

reps = [
 ("#define NAV_ZOOM_CLOSE_MS 260UL", "#define NAV_ZOOM_CLOSE_MS 360UL"),
 ("    float q=1.0f-u;\n    float ez=q*q;                                  // 1 -> 0, cepat di awal lalu melandai ke ikon\n",
  "    float ez=1.0f-navEase(u);                      // 1 -> 0, ease-in-out: start halus, mendarat halus di ikon\n"),
]
for old, new in reps:
    if s.count(old) != 1:
        sys.exit("GAGAL: pola tidak ditemukan/ganda:\n" + old)
    s = s.replace(old, new)
open(p, "w", encoding="utf-8").write(s)
print("OK: animasi tutup app dihaluskan")
