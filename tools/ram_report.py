#!/usr/bin/env python3
"""RAM_REPORT_V1: ringkas pemakaian RAM internal statis (.bss/.data) dari file .map + nm.
Pakai: ram_report.py <file.map> [top_n]. Hanya membaca; tidak pernah menggagalkan build."""
import re, sys, collections
mp = sys.argv[1]; top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
L = open(mp, errors="ignore").read().splitlines()
hdr = re.compile(r'^ (\.\S+)\s*(?:(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*(.*))?$')
cont = re.compile(r'^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*(.*)$')
per = collections.Counter(); sym = []
i = 0
while i < len(L):
    m = hdr.match(L[i]); i += 1
    if not m: continue
    name, a, s, o = m.groups()
    if a is None:                       # nama panjang: alamat ada di baris berikutnya
        if i >= len(L): break
        c = cont.match(L[i])
        if not c: continue
        a, s, o = c.groups(); i += 1
    a = int(a, 16); s = int(s, 16)
    if s == 0 or not (0x3fc80000 <= a < 0x3fd00000): continue   # hanya DRAM internal
    if not re.search(r'\.(bss|data|noinit)', name): continue
    o = (o or "").strip()
    lib = re.search(r'([\w.+-]+\.a)\(', o)
    key = lib.group(1) if lib else (o.split('/')[-1] if o else "?")
    per[key] += s; sym.append((s, name, key))
tot = sum(per.values())
print(f"RAM internal statis (.bss+.data+.noinit) menurut map: {tot} B ({tot/1024:.1f} KB)\n")
print(f"== {top} kontributor terbesar (per library / file objek) ==")
for k, v in per.most_common(top): print(f"{v:>8} B  {k}")
print(f"\n== {top} simbol terbesar ==")
for s, n, k in sorted(sym, reverse=True)[:top]: print(f"{s:>8} B  {n}  <- {k}")
