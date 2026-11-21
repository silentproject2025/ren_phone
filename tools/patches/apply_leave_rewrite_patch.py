#!/usr/bin/env python3
# apply_leave_rewrite_patch.py -- HWS_LEAVE_REWRITE_V1
# Jalankan dari root repo:  python tools/patches/apply_leave_rewrite_patch.py
#
# Masalah: saat app dibuka (navPush -> hwsLeave("navPush", 1)) sementara hardware scroll
# sedang bergeser (k != 0), mesin hanya mematikan scroll (VSCSAD=0) tanpa menulis ulang panel.
# Memori panel masih berupa ring yang bergeser, jadi layar tampak terbelah / ada pita dari
# halaman lain sampai frame penuh terakhir di-push. Animasi zoom-buka cuma menggambar
# area kecil di sekitar ikon, jadi retakan itu terlihat selama animasi.
#
# Perbaikan: pada mode 1 (canvas sudah disinkronkan) dan k != 0, panel ditulis ulang penuh
# dari canvas saat keluar. Kasus k == 0 (diam) tidak berubah, jadi tidak ada biaya tambahan.
import sys, os
MARK = "HWS_LEAVE_REWRITE_V1"
base = "firmware/phone"
edits = [
 ("pager_renphone.ino",
  "  hpEng.leave(hpSink, mode >= 2 && canvasOk);\n",
  "  hpEng.leave(hpSink, canvasOk && (mode >= 2 || (mode == 1 && kk != 0)));   // " + MARK + "\n"),
 ("hwscroll_renphone.ino",
  "  bool need = hwsEng.leave(hwsSink, mode >= 2 && canvasOk);\n",
  "  bool need = hwsEng.leave(hwsSink, canvasOk && (mode >= 2 || (mode == 1 && kk != 0)));   // " + MARK + "\n"),
]
for fn, old, new in edits:
    p = os.path.join(base, fn)
    s = open(p, encoding="utf-8").read()
    if MARK in s:
        print("sudah terpasang:", fn); continue
    if s.count(old) != 1:
        sys.exit("GAGAL: pola tidak ditemukan tepat 1x di " + fn)
    open(p, "w", encoding="utf-8").write(s.replace(old, new))
    print("ok:", fn)
