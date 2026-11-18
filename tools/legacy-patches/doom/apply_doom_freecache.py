#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_doom_freecache.py (DOOM_FREECACHE_V1): bebaskan cache scroll Home (HOME_CACHE_V1, di PSRAM) SEBELUM
DOOM cek PSRAM kontigu. Cache itu dibebaskan di loop sesudah layar pindah dari Home, tapi cek PSRAM di
doomEnter() jalan lebih dulu, jadi cache masih memecah blok PSRAM (error 'tersedia 5887KB, butuh ~6MB').
Jalankan dari root repo. Idempotent."""
import sys, os
P = "doom_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu" % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "DOOM_FREECACHE_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
old = 'void doomEnter(){\n  doomErrorMsg = "";\n'
if s.count(old) != 1: sys.exit("ERROR: awal doomEnter() ketemu %d kali (harus 1). Dibatalkan." % s.count(old))
s = s.replace(old, old + "  homeCacheFree();   // DOOM_FREECACHE_V1: lepas cache scroll Home (PSRAM) sebelum cek blok kontigu\n")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: DOOM_FREECACHE_V1 diterapkan")
