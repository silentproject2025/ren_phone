#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_compile_log_artifact.py (COMPILELOG_V1) -- supaya penyebab "ld returned 1" kelihatan jelas.
 1) Summary run yg gagal: tambah blok "40 baris sebelum collect2 (apa adanya)" -> memuat baris
    'assertion fail' / pesan linker yg selama ini disaring dari ringkasan.
 2) compile.log penuh ikut diunggah di artifact "diagnostik-prototipe-<varian>" (file diag/compile.log).
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = ".github/workflows/build-firmware.yml"
if not os.path.exists(P):
    sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "COMPILELOG_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

old = ("            echo '```'\n"
       "            grep -v \"assertion fail\" compile.log | grep -v \"^/builds/idf\" | tail -25 | cut -c1-300\n"
       "            echo '```'\n"
       "          } >> \"$GITHUB_STEP_SUMMARY\"\n")
if s.count(old) != 1:
    sys.exit("ERROR: blok akhir ringkasan tidak ketemu / tidak unik. Dibatalkan.")
new = ("            echo '```'\n"
       "            grep -v \"assertion fail\" compile.log | grep -v \"^/builds/idf\" | tail -25 | cut -c1-300\n"
       "            echo '```'\n"
       "            # COMPILELOG_V1: konteks linker apa adanya (termasuk baris assertion/pesan ld yg disaring di atas)\n"
       "            echo \"### 40 baris sebelum collect2 (apa adanya)\"\n"
       "            echo '```'\n"
       "            CL=$(grep -n \"collect2: error\" compile.log | head -1 | cut -d: -f1)\n"
       "            if [ -n \"$CL\" ]; then S=$((CL>40 ? CL-40 : 1)); sed -n \"${S},${CL}p\" compile.log | cut -c1-300; else echo \"(tidak ada baris collect2)\"; fi\n"
       "            echo '```'\n"
       "          } >> \"$GITHUB_STEP_SUMMARY\"\n")
s = s.replace(old, new)

old2 = "          mkdir -p diag\n"
if s.count(old2) != 1:
    sys.exit("ERROR: 'mkdir -p diag' tidak ketemu / tidak unik. Dibatalkan.")
s = s.replace(old2, old2 + "          [ -f compile.log ] && cp compile.log diag/compile.log   # COMPILELOG_V1\n")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: ringkasan + artifact diagnostik sekarang memuat konteks linker & compile.log")
