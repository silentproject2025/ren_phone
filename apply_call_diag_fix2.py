#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_diag_fix2.py -- riwayat diagnosa Telepon: jangan penuhi dengan REGISTER.
#
# Masalah: Ren Phone daftar ulang ke server tiap ~96 detik, 1x daftar = 2 baris
# (401 lalu 200). Riwayat cuma 4 baris, jadi INVITE gampang terdorong keluar.
# Patch ini membuang baris REGISTER dari riwayat (tetap dicetak ke Serial).
# Wajib dijalankan SETELAH apply_call_diag_patch.py.
#
#     python3 apply_call_diag_fix2.py
import sys, os

F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if 'callDbgPush' not in s:
    print("ERROR: jalankan apply_call_diag_patch.py dulu"); sys.exit(1)
if 'REGISTER") == nullptr' in s:
    print("Patch fix2 sudah terpasang, dilewati"); sys.exit(0)

old = '    { char dg[40]; snprintf(dg, sizeof(dg), "rx %s", callDbgRx); callDbgPush(dg); }\n'
new = ('    if (strstr(callDbgRx, "REGISTER") == nullptr) {      // REGISTER berkala tdk masuk riwayat\n'
       '      char dg[40]; snprintf(dg, sizeof(dg), "rx %s", callDbgRx); callDbgPush(dg);\n'
       '    } else Serial.printf("[CALL] rx %s\\n", callDbgRx);\n')
if s.count(old) != 1:
    print("ERROR: anchor ketemu %d kali (harus 1). Tidak ada yang diubah." % s.count(old)); sys.exit(1)
open(F, 'w', encoding='utf-8').write(s.replace(old, new))
print("OK: REGISTER tidak lagi masuk riwayat diagnosa")
