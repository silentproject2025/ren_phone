#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_keepalive_fix.py -- percepat keepalive NAT (25 dtk -> 12 dtk) di app Telepon.
#
# Dugaan (BELUM terbukti): panggilan masuk lewat server Linphone tidak sampai karena lubang NAT
# hotspot/operator menutup sebelum keepalive berikutnya. Panggilan keluar tetap jalan karena
# paketnya dimulai dari Ren Phone. Paket keepalive cuma 4 byte, jadi aman dipercepat.
#
#     python3 apply_call_keepalive_fix.py
import sys, os
F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
old = "(int32_t)(now - regKeepAt) >= 25000 && srvOk"
new = "(int32_t)(now - regKeepAt) >= 12000 && srvOk"
if s.count(new) == 1 and s.count(old) == 0:
    print("Patch keepalive sudah terpasang, dilewati"); sys.exit(0)
if s.count(old) != 1:
    print("ERROR: anchor ketemu %d kali (harus 1). Tidak ada yang diubah." % s.count(old)); sys.exit(1)
open(F, 'w', encoding='utf-8').write(s.replace(old, new))
print("OK: keepalive NAT sekarang tiap 12 detik")
