#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Dipakai CI: python3 ci_hoist_types.py build_sketch/build_sketch.ino build_sketch/rp_types.h
import sys, io

def main(ino, hdr):
    with io.open(ino, encoding="utf-8", errors="surrogateescape") as f:
        L = f.read().split("\n")
    if any(l.strip() == '#include "rp_types.h"' for l in L[:3]):
        print("hoist: sudah diterapkan, lewati"); return 0
    if not L or not L[0].startswith("struct HomeRect"):
        print("hoist ERROR: baris 1 bukan 'struct HomeRect'"); return 1
    a = next((i for i, l in enumerate(L) if l.strip() == "#include <LovyanGFX.hpp>"), None)
    t = next((i for i, l in enumerate(L) if l.startswith("struct Theme {")), None)
    if a is None or t is None or t < a:
        print("hoist ERROR: penanda #include <LovyanGFX.hpp> / struct Theme tidak ketemu"); return 1
    e = next((i for i in range(t, len(L)) if L[i].strip() == "};"), None)
    if e is None:
        print("hoist ERROR: akhir struct Theme tidak ketemu"); return 1
    block = L[a:e + 1]
    for i, l in enumerate(block):          # blok harus cuma tipe/prototipe, bukan definisi global
        s = l.strip()
        if s.startswith("#define") or s.startswith("#if") or s.startswith("#else") or s.startswith("#endif"):
            print("hoist ERROR: ada direktif preprosesor di blok (baris %d): %s" % (a + i + 1, s)); return 1
    header = ["#pragma once", L[0]] + block + [""]
    main = ['#include "rp_types.h"'] + L[1:a] + L[e + 1:]
    with io.open(hdr, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write("\n".join(header))
    with io.open(ino, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write("\n".join(main))
    print("hoist OK: %d baris (asli %d-%d) dipindah ke %s" % (len(block), a + 1, e + 1, hdr))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
