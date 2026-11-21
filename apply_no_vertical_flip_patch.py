#!/usr/bin/env python3
# apply_no_vertical_flip_patch.py -- HP_NO_VFLIP_V1
# Jalankan dari root repo:  python apply_no_vertical_flip_patch.py
#
# Masalah: di Home landscape berhalaman, geser vertikal (mis. dari bawah ke atas) ikut membalik
# halaman ke kiri/kanan (kebiasaan lama dari scroll grid). Itu mengganggu.
#
# Perbaikan: gain geser vertikal jadi 0, jadi gerakan vertikal tidak menggeser halaman sama
# sekali. Geser horizontal tidak berubah. Untuk mengembalikan perilaku lama, ubah angka
# HP_VGAIN di pager_renphone.ino kembali ke 2.4f.
import sys, os
MARK = "HP_NO_VFLIP_V1"
p = os.path.join("firmware/phone", "pager_renphone.ino")
old = "#define HP_VGAIN 2.4f "
new = "#define HP_VGAIN 0.0f   /* " + MARK + ": 0 = geser vertikal tidak membalik halaman; 2.4f = perilaku lama */ //"
s = open(p, encoding="utf-8").read()
if MARK in s:
    print("sudah terpasang"); sys.exit(0)
if s.count(old) != 1:
    sys.exit("GAGAL: pola HP_VGAIN tidak ditemukan tepat 1x")
open(p, "w", encoding="utf-8").write(s.replace(old, new))
print("ok: pager_renphone.ino")
