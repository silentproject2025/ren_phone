#!/usr/bin/env python3
# apply_wx_anim_hw.py -- WX_ANIM_HW_V1
# Jalankan dari root repo:  python tools/patches/apply_wx_anim_hw.py
#
# Masalah: ikon cuaca di Home (hujan/salju/awan/matahari) beranimasi dgn needRedraw=true tiap 160 ms,
# tapi HANYA kalau data cuaca sudah ada (wxValid = sesudah WiFi tersambung). Akibatnya:
#   - tiap 160 ms jadi frame penuh + homeCacheValid=false -> cache Home dibangun ulang (hitch) saat scroll,
#   - pemanasan hardware scroll (butuh Home diam 250 ms tanpa needRedraw) tidak pernah jalan,
#   - scroll jatuh ke jalur non-hardware selama WiFi tersambung.
# Perbaikan: animasi ikon cuaca di Home dimatikan selagi Scroll Hardware tidak "Mati" (ST7796),
# dan tidak jalan kalau widget cuaca disembunyikan. Ikon tetap tampil (statis). Mau animasinya lagi:
# set Pengaturan > Scroll Hardware = Mati.
import sys, os
MARK = "WX_ANIM_HW_V1"
p = os.path.join("firmware/phone", "features_renphone.ino")
s = open(p, encoding="utf-8").read()
if MARK in s:
    print("sudah terpasang"); sys.exit(0)
old = "if (wxValid && !locked && !aodActive && curScreen() == SCR_HOME && nA - wxAnimMs >= WX_ANIM_MS) {"
new = "if (wxValid && !locked && !aodActive && curScreen() == SCR_HOME && hwsModeGet() == 0 && (homeWMask & 0x08) && nA - wxAnimMs >= WX_ANIM_MS) {   /* " + MARK + " */"
if s.count(old) != 1:
    sys.exit("GAGAL: pola animasi cuaca tidak ditemukan tepat 1x")
open(p, "w", encoding="utf-8").write(s.replace(old, new))
print("ok: features_renphone.ino (" + MARK + ")")
