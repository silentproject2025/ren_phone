#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_ci_fix.py  --  jalankan SEKALI dari root repo ren_phone (folder yang ada phone.ino).

Yang dilakukan:
 1. Menulis ci_hoist_types.py ke repo. Skrip ini dipakai CI untuk memindahkan blok
    #include + struct/enum/forward-declaration di phone.ino ke header rp_types.h
    yang di-#include di baris 1 -- HANYA di salinan build_sketch/ di CI.
    phone.ino di repo TIDAK diubah, jadi patch .py lamamu tetap jalan.
 2. Mengedit .github/workflows/build-firmware.yml:
      - menjalankan ci_hoist_types.py setelah 'mv ... build_sketch.ino'
      - menambah step diagnostik (hanya jalan kalau compile gagal) yang meng-upload
        hasil preprocess + versi tool sebagai artifact 'diagnostik-prototipe'.
Aman dijalankan ulang (idempotent).
"""
import os, sys

HOIST = r'''#!/usr/bin/env python3
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
'''

WF = os.path.join(".github", "workflows", "build-firmware.yml")
MV = "          mv build_sketch/phone.ino build_sketch/build_sketch.ino\n"
HOOK = "          python3 ci_hoist_types.py build_sketch/build_sketch.ino build_sketch/rp_types.h\n"
UPLOAD = "      - name: Upload Binary Artifacts\n"
DIAG = '''      - name: Diagnostik prototipe (hanya jalan kalau compile gagal)
        if: failure()
        run: |
          mkdir -p diag
          NOF_INCLUDE="$(pwd)/build_sketch/src"
          arduino-cli version > diag/versions.txt 2>&1 || true
          arduino-cli core list >> diag/versions.txt 2>&1 || true
          arduino-cli lib list >> diag/versions.txt 2>&1 || true
          arduino-cli compile --fqbn esp32:esp32:esp32s3 \\
            --board-options "FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \\
            --build-property "compiler.cpp.extra_flags=-I${NOF_INCLUDE}" \\
            --build-property "compiler.c.extra_flags=-I${NOF_INCLUDE}" \\
            --preprocess build_sketch > diag/preprocessed.cpp 2> diag/preprocess.err || true
          head -c 300000 diag/preprocessed.cpp > diag/preprocessed_head.cpp || true
          rm -f diag/preprocessed.cpp
          cp build_sketch/rp_types.h diag/ 2>/dev/true || true

      - name: Upload diagnostik
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: diagnostik-prototipe
          path: diag/

'''.replace("/dev/true", "/dev/null")

def main():
    if not os.path.isfile("phone.ino") or not os.path.isfile(WF):
        print("ERROR: jalankan dari root repo ren_phone (harus ada phone.ino dan %s)" % WF)
        return 1
    with open("ci_hoist_types.py", "w", encoding="utf-8", newline="\n") as f:
        f.write(HOIST)
    print("OK  ci_hoist_types.py ditulis")

    with open(WF, encoding="utf-8") as f:
        w = f.read()
    changed = False
    if "ci_hoist_types.py" not in w:
        if w.count(MV) != 1:
            print("ERROR: baris 'mv build_sketch/phone.ino ...' tidak ketemu persis 1x di workflow"); return 1
        w = w.replace(MV, MV + HOOK); changed = True
        print("OK  step hoist ditambahkan ke workflow")
    else:
        print("--  step hoist sudah ada")
    if "diagnostik-prototipe" not in w:
        if w.count(UPLOAD) != 1:
            print("ERROR: step 'Upload Binary Artifacts' tidak ketemu persis 1x"); return 1
        w = w.replace(UPLOAD, DIAG + UPLOAD); changed = True
        print("OK  step diagnostik ditambahkan ke workflow")
    else:
        print("--  step diagnostik sudah ada")
    if changed:
        with open(WF, "w", encoding="utf-8", newline="\n") as f:
            f.write(w)
    print("\nSelesai. Sekarang: git add -A && git commit && git push")
    return 0

if __name__ == "__main__":
    sys.exit(main())
