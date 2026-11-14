#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_gallery_nosticky.py -- tanda "?" di galeri tidak lagi permanen.

Penyebab: waktu thumbnail GAGAL dibuat (apa pun alasannya, termasuk gangguan SESAAT: SD sedang sibuk,
heap sempat tipis, decoder gagal buka), galCacheWrite() menulis penanda gagal 'NTX1' ke cache SD
(/.thumbs/...). Sejak itu galCacheRead() selalu mengembalikan "gagal" tanpa pernah mencoba lagi --
gangguan sesaat jadi "?" PERMANEN, bahkan setelah reboot, pada semua foto yang kebetulan diproses
saat gangguan itu.

Perbaikan:
 1) Kegagalan tidak lagi ditulis ke SD (hanya thumbnail yang berhasil yang di-cache).
 2) Penanda gagal LAMA yang sudah tersimpan otomatis dihapus saat terbaca, lalu thumbnail dibuat ulang
    -> "?" yang sekarang ada hilang sendiri tanpa perlu hapus cache manual.
Konsekuensi: file yang benar2 rusak dicoba decode sekali tiap galeri dibuka (gagal cepat, hanya sekali per sesi album).
Menyentuh gallery_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "gallery_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "GAL_NOSTICKY_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

rep("  uint8_t mg[4];\n  int rc = 0;\n  if (sz >= 4 && f.read(mg, 4) == 4 && mg[0] == 'N' && mg[1] == 'T') {\n",
    "  uint8_t mg[4];\n  int rc = 0;\n  bool stale = false;   // GAL_NOSTICKY_V1: penanda gagal lama -> hapus & buat ulang\n  if (sz >= 4 && f.read(mg, 4) == 4 && mg[0] == 'N' && mg[1] == 'T') {\n", "read-decl")
rep("    } else if (mg[2] == 'X') {\n      rc = 2;\n    }\n  }\n  f.close();\n  return rc;\n",
    "    } else if (mg[2] == 'X') {\n      stale = true;   // GAL_NOSTICKY_V1: dulu rc = 2 (\"?\" permanen)\n    }\n  }\n  f.close();\n  if (stale) SD_MMC.remove(cpath);\n  return rc;\n", "read-x")
rep("static void galCacheWrite(const char* cpath, uint64_t key, const uint16_t* px, bool ok) {\n  galCacheEnsureDir(key);\n",
    "static void galCacheWrite(const char* cpath, uint64_t key, const uint16_t* px, bool ok) {\n  if (!ok) return;   // GAL_NOSTICKY_V1: jangan simpan kegagalan ke SD (gangguan sesaat jadi \"?\" permanen)\n  galCacheEnsureDir(key);\n", "write")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] GAL_NOSTICKY_V1 terpasang di gallery_renphone.ino")
