#!/usr/bin/env python3
# Perbaiki build-arduino-libs.yml utk lib-builder branch release/v3.3:
#  1) step "Pra-clone arduino-esp32" dipindah SEBELUM "Tambah opsi sdkconfig"
#  2) pencarian defconfig.esp32s3 diperdalam (maxdepth 3 -> 6)
import re, sys
p = ".github/workflows/build-arduino-libs.yml"
s = open(p, encoding="utf-8").read()
if "NYX-V33-ORDER" in s:
    print("sudah dipatch"); sys.exit(0)
a = s.index("      - name: Tambah opsi sdkconfig")
b = s.index("      - name: Pra-clone arduino-esp32")
c = s.index("      - name: Build library esp32s3")
if not (a < b < c):
    sys.exit("urutan step tidak sesuai, patch dibatalkan")
tambah, pra = s[a:b], s[b:c]
tambah = tambah.replace("-maxdepth 3 -name 'defconfig.esp32s3'", "-maxdepth 6 -name 'defconfig.esp32s3'")
tambah = tambah.replace("find . -maxdepth 3 -iname '*defconfig*'", "find . -maxdepth 6 -iname '*defconfig*'")
tambah = tambah.replace("          cd \"$HOME/lib-builder\"\n          ls\n", "          cd \"$HOME/lib-builder\"  # NYX-V33-ORDER\n          ls\n", 1)
if "NYX-V33-ORDER" not in tambah:
    sys.exit("penanda gagal dipasang")
s = s[:a] + pra + tambah + s[c:]
open(p, "w", encoding="utf-8").write(s)
print("OK: urutan step diperbaiki, find diperdalam")
