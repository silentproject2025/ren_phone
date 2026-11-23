#!/usr/bin/env python3
# WSENSE_CSI_INPUT -- build-arduino-libs.yml: toggle "wifi_csi" di form Run workflow (bawaan: nyala).
# Menggantikan apply_wsense_libs_csi.py (yang menyalakan CSI permanen). Aman dijalankan sebelum/sesudah skrip itu.
# Pakai (dari root repo):  python3 apply_wsense_csi_input.py
import sys, re
P = ".github/workflows/build-arduino-libs.yml"
s = open(P, encoding="utf-8").read()
if "WSENSE_CSI_INPUT" in s:
    print("Sudah terpasang."); sys.exit(0)

# 0. bersihkan versi permanen dari skrip lama (kalau ada)
s = s.replace("            echo 'CONFIG_ESP_WIFI_CSI_ENABLED=y'   # WSENSE_LIBS_CSI: CSI WiFi untuk app WiFi Sensing\n", "")
s = re.sub(r'          echo "=== WSENSE_LIBS_CSI: CSI WiFi di hasil build ==="\n          grep -q .*\n', "", s)

def one(old):
    if s.count(old) != 1:
        sys.exit("GAGAL: penanda ditemukan %d kali (harus 1):\n%s" % (s.count(old), old[:120]))

# 1. input baru di form (setelah elf_loader)
a = '        description: "NYX-ELF: sertakan espressif/elf_loader'
one(a)
i = s.index(a); j = s.index("        default: true\n", i) + len("        default: true\n")
s = s[:j] + (
 '      wifi_csi:\n'
 '        description: "WSENSE_CSI_INPUT: nyalakan CONFIG_ESP_WIFI_CSI_ENABLED (CSI WiFi) untuk app WiFi Sensing. Matikan kalau RAM internal terasa sempit"\n'
 '        type: boolean\n'
 '        default: true\n') + s[j:]

# 2. opsi sdkconfig bersyarat
b = "            echo 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y'\n"
one(b)
s = s.replace(b, b + '            if [ "${{ inputs.wifi_csi }}" = "true" ]; then echo \'CONFIG_ESP_WIFI_CSI_ENABLED=y\'; fi   # WSENSE_CSI_INPUT\n')

# 3. verifikasi bersyarat
c = '          echo "=== NYX-ELF: elf_loader di hasil build ==="\n'
one(c)
s = s.replace(c,
 '          echo "=== WSENSE_CSI_INPUT: CSI WiFi di hasil build ==="\n'
 '          if [ "${{ inputs.wifi_csi }}" = "true" ]; then\n'
 '            grep -q \'^CONFIG_ESP_WIFI_CSI_ENABLED=y\' "$OUT/sdkconfig" && echo "OK   CONFIG_ESP_WIFI_CSI_ENABLED=y" || echo "::warning::CONFIG_ESP_WIFI_CSI_ENABLED tidak aktif di sdkconfig hasil build"\n'
 '          else echo "CSI dimatikan lewat input wifi_csi"; fi\n' + c)
open(P, "w", encoding="utf-8").write(s)
print("OK: input wifi_csi ditambahkan (bawaan nyala).")
