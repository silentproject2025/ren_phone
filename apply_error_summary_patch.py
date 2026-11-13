#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_error_summary_patch.py -- build-firmware.yml: kalau compile/link gagal, tampilkan RINGKASAN error
di halaman run (Annotations + Summary), jadi tidak perlu baca log panjang atau pakai terminal.
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = ".github/workflows/build-firmware.yml"
if not os.path.exists(P):
    sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "NYX-ERRSUM" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

# 1) compile -> tee ke compile.log (pipefail supaya status gagal tetap terbaca)
a = "      - name: Compile Sketch\n"
i = s.find(a)
r = s.find("        run: |\n", i)
if i < 0 or r < 0: sys.exit("ERROR: step Compile Sketch tidak ketemu")
r_end = r + len("        run: |\n")
s = s[:r_end] + "          set -o pipefail  # NYX-ERRSUM\n" + s[r_end:]
old = "            build_sketch --output-dir build\n"
if s.count(old) != 1: sys.exit("ERROR: baris akhir perintah compile tidak ketemu/unik")
s = s.replace(old, "            build_sketch --output-dir build 2>&1 | tee compile.log\n", 1)

# 2) step ringkasan sebelum 'Diagnostik prototipe'
step = '''      - name: Ringkasan error (hanya jalan kalau compile gagal)
        if: failure()
        run: |
          [ -f compile.log ] || { echo "compile.log tidak ada (gagal sebelum compile)"; exit 0; }
          PAT='error:|Error:|dangerous relocation|undefined reference|overflowed|too big|cannot encode|multiple definition|No such file|ld returned|Sketch too big'
          N_ASSERT=$(grep -c "assertion fail" compile.log || true)
          {
            echo "## Ringkasan error compile (${VARIANT:-?})"
            echo
            echo "Baris 'assertion fail' (linker crash): **${N_ASSERT}** kali"
            grep -m1 "assertion fail" compile.log | cut -c1-300 | sed 's/^/    /' || true
            echo
            echo "### Error pertama (selain assertion)"
            echo '```'
            grep -v "assertion fail" compile.log | grep -v "^/builds/idf" | grep -n -iE "$PAT" | head -25 | cut -c1-300
            echo '```'
            echo "### 25 baris terakhir (selain assertion)"
            echo '```'
            grep -v "assertion fail" compile.log | grep -v "^/builds/idf" | tail -25 | cut -c1-300
            echo '```'
          } >> "$GITHUB_STEP_SUMMARY"
          # juga jadi Annotations (mudah dibaca di app GitHub HP)
          grep -v "assertion fail" compile.log | grep -iE "$PAT" | head -8 | cut -c1-280 | while IFS= read -r L; do
            echo "::error::${L}"
          done

'''
d = "      - name: Diagnostik prototipe (hanya jalan kalau compile gagal)\n"
if s.count(d) != 1: sys.exit("ERROR: anchor step Diagnostik tidak ketemu/unik")
s = s.replace(d, step + d, 1)
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: ringkasan error ditambahkan (lihat tab Summary + Annotations di halaman run yang gagal)")
