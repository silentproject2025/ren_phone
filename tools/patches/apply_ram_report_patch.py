#!/usr/bin/env python3
"""RAM_REPORT_V1: sisipkan langkah laporan RAM statis ke build-firmware.yml (idempoten).
Pakai: python3 tools/patches/apply_ram_report_patch.py [--check]"""
import sys, pathlib
WF = pathlib.Path(".github/workflows/build-firmware.yml")
ANCHOR = "      - name: Ringkasan error (hanya jalan kalau compile gagal)\n"
MARK = "RAM_REPORT_V1"
STEP = '''      - name: Laporan RAM statis (RAM_REPORT_V1)
        continue-on-error: true
        run: |
          set +e
          MAP=$(ls build/*.map 2>/dev/null | head -1)
          ELF=$(ls build/*.elf 2>/dev/null | head -1)
          echo "map=$MAP elf=$ELF"
          {
            echo "### RAM statis (${{ matrix.variant }})"
            echo '```'
            grep -E "Global variables use|Sketch uses" compile.log
            [ -n "$MAP" ] && python3 tools/ram_report.py "$MAP" 25
            NM=$(find "$HOME/.arduino15" -name 'xtensa-esp32s3-elf-nm' 2>/dev/null | head -1)
            if [ -n "$NM" ] && [ -n "$ELF" ]; then
              echo; echo "== nm: 30 simbol RAM terbesar =="
              "$NM" -S --size-sort -t d "$ELF" | grep -E ' [bBdD] ' | tail -30
            fi
            echo '```'
          } | tee ram_report.md >> "$GITHUB_STEP_SUMMARY"

      - name: Upload laporan RAM
        if: always()
        continue-on-error: true
        uses: actions/upload-artifact@v4
        with:
          name: ram-report-${{ matrix.variant }}
          path: ram_report.md
          if-no-files-found: ignore

'''
t = WF.read_text()
if MARK in t: print("sudah terpasang"); sys.exit(0)
if t.count(ANCHOR) != 1: print("anchor tidak cocok"); sys.exit(1)
if "--check" in sys.argv: print("anchor cocok"); sys.exit(0)
WF.write_text(t.replace(ANCHOR, STEP + ANCHOR)); print("terpasang")
