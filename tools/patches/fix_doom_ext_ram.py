#!/usr/bin/env python3
"""DOOM_EXTRAM_FIX_V1: buang pasangan 'EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR ' yang nyasar di third_party/esp_doom_core.
Dulu makro itu kosong (opsi libs belum nyala) jadi tak berbahaya; sekarang jadi atribut section -> compile error.
Yang tersisa hanya satu penanda per variabel (xLUT, yLUT, row, rgb565_palette, musBus, musicScratch, acc, out).
Pakai dari root repo: python3 tools/patches/fix_doom_ext_ram.py [--check]   (idempoten)"""
import pathlib, re, sys
CHECK = "--check" in sys.argv
PAIR = "EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR "
root = pathlib.Path("third_party/esp_doom_core")
tot = 0
for f in sorted(root.rglob("*")):
    if f.suffix not in (".c", ".cpp", ".h"): continue
    t = f.read_text(errors="surrogateescape")
    n = t.count(PAIR)
    if not n: continue
    tot += n
    print(f"{'ada   ' if CHECK else 'dibuang'} {n:>2}x  {f}")
    if not CHECK:
        while PAIR in t: t = t.replace(PAIR, "")
        f.write_text(t, errors="surrogateescape")
# sisa penanda harus tepat di depan 'static' (deklarasi variabel), tidak boleh ada yang dobel/nyasar
bad = 0
for f in sorted(root.rglob("*")):
    if f.suffix not in (".c", ".cpp", ".h"): continue
    for i, l in enumerate(f.read_text(errors="surrogateescape").splitlines(), 1):
        if "EXT_RAM_BSS_ATTR" in l and (l.count("EXT_RAM_BSS_ATTR") > 1 or not re.search(r"EXT_RAM_BSS_ATTR\s+static\b", l)):
            print(f"CURIGA  {f}:{i}: {l.strip()[:90]}"); bad += 1
print(f"selesai: {tot} pasangan {'ditemukan' if CHECK else 'dibuang'}, {bad} baris mencurigakan")
sys.exit(1 if bad else 0)
