#!/usr/bin/env python3
# build-arduino-libs.yml: lib-builder tetap 'master' (auto), tapi IDF dipaksa
# lewat build.sh -I (default release/v5.5 = IDF milik core esp32 3.3.x).
# Branch lib-builder dinamai menurut versi IDF, BUKAN versi core Arduino.
import sys
p = ".github/workflows/build-arduino-libs.yml"
s = open(p, encoding="utf-8").read()
if "NYX-IDF-INPUT" in s:
    print("sudah dipatch"); sys.exit(0)
# 1) input baru idf_branch
anchor = "      psram_cache_line_64b:"
if anchor not in s: sys.exit("anchor input tidak ketemu")
new_in = ('      idf_branch:\n'
          '        description: "NYX-IDF-INPUT: branch ESP-IDF utk build.sh -I. Core esp32 3.3.x = release/v5.5. Kosong = bawaan lib-builder"\n'
          '        default: "release/v5.5"\n'
          '        required: false\n')
s = s.replace(anchor, new_in + anchor, 1)
# 2) build.sh pakai -I
old = '          env -u GITHUB_REPOSITORY ./build.sh -t esp32s3 -A "$AR_BRANCH"\n'
if old not in s: sys.exit("baris build.sh tidak ketemu")
new = ('          IDF_BR="${{ inputs.idf_branch }}"\n'
       '          echo "IDF branch: ${IDF_BR:-<bawaan lib-builder>}"\n'
       '          if [ -n "$IDF_BR" ]; then\n'
       '            env -u GITHUB_REPOSITORY ./build.sh -t esp32s3 -A "$AR_BRANCH" -I "$IDF_BR"\n'
       '          else\n'
       '            env -u GITHUB_REPOSITORY ./build.sh -t esp32s3 -A "$AR_BRANCH"\n'
       '          fi\n')
s = s.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(s)
print("OK: input idf_branch + build.sh -I terpasang")
