#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_libs_ram_patch.py (LIBS_RAM_V1): tambah opsi sdkconfig hemat RAM internal ke build-arduino-libs.yml.
 - input iram_to_flash (default true): kode WiFi/lwIP/FreeRTOS dari IRAM -> flash (IRAM bebas jadi heap)
 - input wifi_dyn_buf (default 16): jumlah buffer dinamis RX/TX WiFi (bawaan 32)
 - default spiram_always_internal: 4096 -> 2048
Jalankan dari root repo. Idempotent."""
import sys, os
P = ".github/workflows/build-arduino-libs.yml"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu" % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "LIBS_RAM_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
def rep(old, new, label):
    global s
    if s.count(old) != 1: sys.exit("ERROR: '%s' ketemu %d kali (harus 1). Dibatalkan." % (label, s.count(old)))
    s = s.replace(old, new)

# 1. default 2048
rep('''        options: [ "default", "4096", "2048" ]
        default: "4096"''', '''        options: [ "default", "4096", "2048" ]
        default: "2048"''', "default always_internal")

# 2. input baru (setelah tcp_window_kb)
rep('''        options: [ "default", "16", "32", "64" ]
        default: "default"
''', '''        options: [ "default", "16", "32", "64" ]
        default: "default"
      iram_to_flash:
        description: "LIBS_RAM_V1: pindahkan kode WiFi/lwIP/FreeRTOS dari IRAM ke flash (IRAM yg kosong jadi heap internal; WiFi sedikit lebih lambat)"
        type: boolean
        default: true
      wifi_dyn_buf:
        description: "LIBS_RAM_V1: jumlah buffer dinamis WiFi RX dan TX (bawaan IDF 32). Lebih kecil = lebih hemat, throughput WiFi bisa turun"
        type: choice
        options: [ "default", "24", "16", "12" ]
        default: "16"
''', "input")

# 3. tulis opsi ke defconfig
rep('''            if [ "$W" != "0" ]; then
              echo "CONFIG_LWIP_TCP_WND_DEFAULT=$W"''', '''            # LIBS_RAM_V1
            if [ "${{ inputs.iram_to_flash }}" = "true" ]; then
              echo 'CONFIG_ESP_WIFI_IRAM_OPT=n'
              echo 'CONFIG_ESP_WIFI_RX_IRAM_OPT=n'
              echo 'CONFIG_LWIP_IRAM_OPTIMIZATION=n'
              echo 'CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y'
            fi
            case "${{ inputs.wifi_dyn_buf }}" in
              24|16|12)
                echo "CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=${{ inputs.wifi_dyn_buf }}"
                echo "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=${{ inputs.wifi_dyn_buf }}" ;;
            esac
            if [ "$W" != "0" ]; then
              echo "CONFIG_LWIP_TCP_WND_DEFAULT=$W"''', "tulis opsi")

# 4. cek hasil
rep('''          echo "=== batas malloc internal (SPIRAM_MALLOC_ALWAYSINTERNAL) ==="''', '''          echo "=== LIBS_RAM_V1: opsi hemat IRAM/buffer di sdkconfig hasil ==="
          if [ "${{ inputs.iram_to_flash }}" = "true" ]; then
            for OPT in CONFIG_ESP_WIFI_IRAM_OPT CONFIG_ESP_WIFI_RX_IRAM_OPT CONFIG_LWIP_IRAM_OPTIMIZATION; do
              if grep -q "^$OPT=y" "$OUT/sdkconfig"; then echo "::warning::$OPT masih y (tidak berhasil dimatikan)"; else echo "OK   $OPT mati"; fi
            done
            grep -q '^CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y' "$OUT/sdkconfig" && echo "OK   FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y" || echo "::warning::FREERTOS_PLACE_FUNCTIONS_INTO_FLASH tidak aktif di sdkconfig hasil"
          fi
          grep -E '^CONFIG_ESP_WIFI_(DYNAMIC_(RX|TX)_BUFFER_NUM|STATIC_RX_BUFFER_NUM|RX_BA_WIN)=' "$OUT/sdkconfig" || echo "::warning::opsi buffer WiFi tidak ditemukan di sdkconfig hasil"
          echo "=== batas malloc internal (SPIRAM_MALLOC_ALWAYSINTERNAL) ==="''', "cek hasil")

if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: LIBS_RAM_V1 diterapkan")
