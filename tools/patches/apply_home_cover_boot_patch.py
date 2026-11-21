#!/usr/bin/env python3
# apply_home_cover_boot_patch.py -- HP_COVER_BOOT_V1
# Jalankan dari root repo:  python tools/patches/apply_home_cover_boot_patch.py
#
# Masalah: setelah boot, daftar halaman pager (hpN) baru dihitung oleh hpOn(), dan hpOn() baru
# jalan setelah Home boleh aktif (bukan terkunci dll). Frame Home pertama digambar lewat jalur
# lama (widget + judul + grid app). Penutup hpHomeCover() langsung return karena hpN masih 0,
# jadi app tampil sebentar, lalu hilang saat pager menghitung halaman dan mendarat di halaman widget.
#
# Perbaikan: kalau hpN masih 0, hitung halaman dulu di dalam hpHomeCover() supaya penutup sudah
# berlaku sejak frame pertama. Hanya jalan sekali (hpN == 0), jadi tidak ada rekursi dan
# tidak ada biaya per frame.
import sys, os
MARK = "HP_COVER_BOOT_V1"
p = os.path.join("firmware/phone", "pager_renphone.ino")
old = "  if(hpN < 2 || hwlModeLoad() == 0 || !hpGeomOk()) return;\n"
new = ("  if(hwlModeLoad() == 0 || !hpGeomOk()) return;\n"
       "  if(hpN == 0) hpEnsurePages();   // " + MARK + ": halaman belum dihitung (frame Home pertama setelah boot) -> hitung sekarang, jangan biarkan grid app terlihat\n"
       "  if(hpN < 2) return;\n")
s = open(p, encoding="utf-8").read()
if MARK in s:
    print("sudah terpasang"); sys.exit(0)
if s.count(old) != 1:
    sys.exit("GAGAL: pola tidak ditemukan tepat 1x di pager_renphone.ino")
open(p, "w", encoding="utf-8").write(s.replace(old, new))
print("ok: pager_renphone.ino")
