#!/usr/bin/env python3
"""Pin versi core esp32 di workflow supaya toolchain tidak berubah sendiri.
Pakai:  python3 apply_pin_core.py 3.3.7      (ganti dgn versi core yg terakhir hijau)
Idempotent: dijalankan ulang dgn versi lain akan mengganti pin lama."""
import re, sys, pathlib
if len(sys.argv) != 2 or not re.fullmatch(r"\d+\.\d+\.\d+", sys.argv[1]):
    sys.exit("pakai: python3 apply_pin_core.py <versi, mis. 3.3.7>")
ver = sys.argv[1]
pat = re.compile(r"arduino-cli core install esp32:esp32(@\S+)?")
for name in ("build-firmware.yml", "build-arduino-libs.yml", "build-cam-coprocessor.yml"):
    p = pathlib.Path(".github/workflows") / name
    s = p.read_text()
    new, n = pat.subn(f"arduino-cli core install esp32:esp32@{ver}", s)
    if n == 0:
        print(f"[lewati] {name}: baris core install tidak ditemukan"); continue
    p.write_text(new); print(f"[ok] {name}: {n} baris -> esp32:esp32@{ver}")
