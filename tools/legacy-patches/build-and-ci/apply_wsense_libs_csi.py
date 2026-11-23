#!/usr/bin/env python3
# ARSIP: digantikan oleh tools/patches/apply_wsense_csi_input.py. Menjalankannya lagi menyalakan CSI PERMANEN di build-arduino-libs.yml (input wifi_csi tidak berlaku lagi).
# WSENSE_LIBS_CSI -- build-arduino-libs.yml: selalu menyalakan CONFIG_ESP_WIFI_CSI_ENABLED=y di library hasil build
# (dipakai app WiFi Sensing) dan memverifikasinya di sdkconfig hasil build.
# Pakai (dari root repo):  python3 tools/legacy-patches/build-and-ci/apply_wsense_libs_csi.py   lalu jalankan workflow "Build Arduino Libs", lalu build ulang firmware.
import sys
P = ".github/workflows/build-arduino-libs.yml"
s = open(P, encoding="utf-8").read()
if "WSENSE_LIBS_CSI" in s:
    print("Sudah terpasang."); sys.exit(0)
a = "            echo 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y'\n"
b = "          echo \"=== NYX-ELF: elf_loader di hasil build ===\"\n"
if s.count(a) != 1 or s.count(b) != 1:
    sys.exit("GAGAL: penanda ditemukan a=%d b=%d (harus 1 dan 1)" % (s.count(a), s.count(b)))
s = s.replace(a, a + "            echo 'CONFIG_ESP_WIFI_CSI_ENABLED=y'   # WSENSE_LIBS_CSI: CSI WiFi untuk app WiFi Sensing\n")
s = s.replace(b,
  "          echo \"=== WSENSE_LIBS_CSI: CSI WiFi di hasil build ===\"\n"
  "          grep -q '^CONFIG_ESP_WIFI_CSI_ENABLED=y' \"$OUT/sdkconfig\" && echo \"OK   CONFIG_ESP_WIFI_CSI_ENABLED=y\" || echo \"::warning::CONFIG_ESP_WIFI_CSI_ENABLED tidak aktif di sdkconfig hasil build\"\n"
  + b)
open(P, "w", encoding="utf-8").write(s)
print("OK: CSI ditambahkan ke build-arduino-libs.yml (opsi + verifikasi).")
