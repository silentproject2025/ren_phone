#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_ci_retry.py -- CI_RETRY_V1: "arduino-cli core install esp32:esp32" diulang sampai 4x di semua workflow.
Penyebab: unduhan toolchain dari GitHub kadang terputus ("connection reset by peer") -> job gagal padahal kode tidak salah.
Jalankan dari root repo. Idempotent.
"""
import os, re, sys
FILES = [".github/workflows/build-cam-coprocessor.yml", ".github/workflows/build-firmware.yml", ".github/workflows/build-arduino-libs.yml"]
pat = re.compile(r"^([ \t]*)arduino-cli core install esp32:esp32[ \t]*$", re.M)
done = 0
for p in FILES:
    if not os.path.exists(p): print("[lewati] %s tidak ada" % p); continue
    s = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    s = s.replace("\r\n", "\n")
    if "CI_RETRY_V1" in s: print("[skip] %s sudah terpasang" % p); continue
    n = len(pat.findall(s))
    if n == 0: print("[lewati] %s: baris core install tidak ketemu" % p); continue
    def sub(m):
        i = m.group(1)
        return (f"{i}# CI_RETRY_V1: unduhan toolchain kadang putus -> ulangi\n"
                f"{i}for try in 1 2 3 4; do\n"
                f"{i}  arduino-cli core install esp32:esp32 && break\n"
                f"{i}  [ \"$try\" = 4 ] && exit 1\n"
                f"{i}  echo \"Unduhan core gagal (percobaan $try), ulangi 20 dtk lagi...\"; sleep 20\n"
                f"{i}done")
    s = pat.sub(sub, s)
    if crlf: s = s.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(s)
    print("OK: %s (%d tempat)" % (p, n)); done += 1
print("Selesai: %d file diubah" % done)
