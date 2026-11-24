#!/usr/bin/env python3
"""CHANGELOG_RAM_V1: tambah catatan RAM report + BSS ke PSRAM di CHANGELOG.md (Unreleased) dan tabel tools/patches/README.md. Idempoten.
Pakai dari root repo: python3 tools/patches/apply_changelog_ram.py"""
import pathlib, sys
CL = pathlib.Path("CHANGELOG.md"); c = CL.read_text()
MARK = "static-RAM report (step"
ENTRIES = (
"- New: the firmware build now prints a static-RAM report (step *Laporan RAM statis* in Build Firmware): the biggest contributors of `.bss`/`.data`/`.noinit` in internal RAM per library or object file, and the biggest symbols, from the linker map and `nm`. It shows in the run summary and in the artifact `ram-report-<variant>`. The step cannot fail the build. Script: `tools/ram_report.py` (installed by `tools/patches/apply_ram_report_patch.py`). First measurement (st7796, run before the PSRAM move below): 225,756 B of static internal RAM, of which the sketch itself 126 KB, Doom about 39 KB and WiFi/lwIP about 19 KB\n"
"- Changed: seven large static arrays moved from internal RAM to PSRAM with `EXT_RAM_BSS_ATTR`: `nsApps`, `snakeBody`, `bootDisk`, `galAlbums`, `kmsUserBuf`, `aboutRows`, `memoNames`. About 23.5 KB more free internal RAM expected. Needs `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` in the custom libraries (now always set by Build Arduino Libs, with a check in its log), so run Build Arduino Libs first, then rebuild the firmware; without it the arrays simply stay in internal RAM. Left in internal RAM on purpose: the chess engine tables, the raycaster z-buffer, the translation cache and the NES PPU (read too often), and anything with a `String` member. Not yet tested on a device. Script: `tools/patches/apply_psram_bss_patch.py`\n"
)
if MARK in c: print("changelog sudah")
else:
    a = "## Unreleased\n"
    if c.count(a) != 1: print("GAGAL: '## Unreleased' tidak ditemukan"); sys.exit(1)
    CL.write_text(c.replace(a, a + ENTRIES, 1)); print("changelog diperbarui")
R = pathlib.Path("tools/patches/README.md")
if R.exists():
    r = R.read_text()
    if "apply_psram_bss_patch.py" in r: print("README patches sudah")
    else:
        row = ("| `apply_ram_report_patch.py` | `RAM_REPORT_V1` | History (adds the static-RAM report step to `build-firmware.yml`; needs `tools/ram_report.py`) |\n"
               "| `apply_psram_bss_patch.py` | `PSRAM_BSS_V1` | History (marks seven large BSS arrays `EXT_RAM_BSS_ATTR` and enables `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` in `build-arduino-libs.yml`) |\n")
        anchor = [l for l in r.splitlines(True) if l.startswith("| `apply_webfm_idle.py`")]
        if len(anchor) != 1: print("GAGAL: baris tabel terakhir tidak ditemukan"); sys.exit(1)
        R.write_text(r.replace(anchor[0], anchor[0] + row, 1)); print("README patches diperbarui")
