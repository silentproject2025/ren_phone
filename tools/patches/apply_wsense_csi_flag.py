#!/usr/bin/env python3
# WSENSE_CSI_FLAG -- menambah -DWSENSE_FORCE=1 ke kedua perintah arduino-cli di build-firmware.yml,
# supaya app WiFi Sensing selalu dibuild dengan CSI aktif (tidak bergantung CONFIG_ESP_WIFI_CSI_ENABLED).
# Kalau CSI ternyata tidak ada di library WiFi, app menampilkan pesan di layar, tidak crash.
# Pakai (dari root repo):  python3 tools/patches/apply_wsense_csi_flag.py
import sys
P = ".github/workflows/build-firmware.yml"
s = open(P, encoding="utf-8").read()
if "WSENSE_FORCE" in s:
    print("Sudah terpasang."); sys.exit(0)
a = "compiler.cpp.extra_flags=-DRP_BUILD_NUM="
b = "compiler.cpp.extra_flags=${DISP_FLAG} -I${NOF_INCLUDE}"
if s.count(a) != 1 or s.count(b) != 1:
    sys.exit("GAGAL: baris compiler.cpp.extra_flags tidak ditemukan 1x masing-masing (a=%d b=%d)" % (s.count(a), s.count(b)))
s = s.replace(a, "compiler.cpp.extra_flags=-DWSENSE_FORCE=1 -DRP_BUILD_NUM=")
s = s.replace(b, "compiler.cpp.extra_flags=-DWSENSE_FORCE=1 ${DISP_FLAG} -I${NOF_INCLUDE}")
open(P, "w", encoding="utf-8").write(s)
print("OK: -DWSENSE_FORCE=1 ditambahkan ke 2 perintah build.")
