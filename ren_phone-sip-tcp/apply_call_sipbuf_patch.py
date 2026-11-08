#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_sipbuf_patch.py -- perbesar buffer pesan SIP (1500 -> 4096 byte).
#
# Penyebab: lewat TCP, server mengirim pesan SIP (mis. INVITE) yang lebih besar dari 1500 byte;
# kode lama membuangnya ("TCP pesan >1.5KB"), jadi panggilan masuk tidak berdering.
# Perubahan: SIP_BUF 1500 -> 4096 (semua buffer SIP ada di PSRAM, tambahan sekitar 15 KB) dan pesan
# diagnosa sekarang menyebut ukuran pesan yang dibuang.
#
# Pasang SETELAH apply_call_tcp_transport_patch.py:
#     python3 apply_call_sipbuf_patch.py
import sys, os
F = 'call_renphone.ino'
if not os.path.exists(F):
    print("ERROR: %s tidak ada di folder ini" % F); sys.exit(1)
s = open(F, encoding='utf-8').read()
if '#define SIP_BUF 4096' in s:
    print("Patch SIP_BUF sudah terpasang, dilewati"); sys.exit(0)
if 'callTcpPref' not in s:
    print("ERROR: pasang dulu apply_call_tcp_transport_patch.py"); sys.exit(1)
edits = [
 ("SIP_BUF", "#define SIP_BUF 1500\n", "#define SIP_BUF 4096\n"),
 ("diag ukuran",
  '      } else if (ml >= SIP_BUF) callDbgPush("TCP pesan >1.5KB");',
  '      } else if (ml >= SIP_BUF) { char dg[40]; snprintf(dg, sizeof(dg), "TCP pesan %dB dibuang", ml); callDbgPush(dg); }'),
]
for name, old, new in edits:
    c = s.count(old)
    if c != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (name, c)); sys.exit(1)
for name, old, new in edits:
    s = s.replace(old, new)
open(F, 'w', encoding='utf-8').write(s)
print("OK: SIP_BUF jadi 4096 byte")
