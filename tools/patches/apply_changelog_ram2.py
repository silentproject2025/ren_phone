#!/usr/bin/env python3
"""CHANGELOG_RAM_V2: ganti catatan 'seven arrays ... expected' dengan hasil ukur nyata + catat perbaikan Doom. Idempoten.
Pakai dari root repo: python3 tools/patches/apply_changelog_ram2.py"""
import pathlib, sys
CL = pathlib.Path("CHANGELOG.md"); c = CL.read_text()
MARK = "225,756 B to 173,842 B"
CHANGED = ("- Changed: large static arrays moved from internal RAM to PSRAM with `EXT_RAM_BSS_ATTR`: `nsApps`, `snakeBody`, `bootDisk`, `galAlbums`, `kmsUserBuf`, `aboutRows`, `memoNames`, the WiFi Sensing buffers (`wsX`, `wsD`, `wsHann`, `wsCalBuf`), the call signalling buffers (`rq`, `urq`, `turn*`) and `nsMfTxt`. Build Arduino Libs now always sets `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` (checked in its log), so the static data of ESP-IDF's own WiFi, lwIP and wpa libraries moves too. Measured with the build's RAM report (st7796): static internal RAM 225,756 B to 173,842 B (51.9 KB less: sketch 33.4 KB, WiFi/lwIP/wpa libraries about 15 KB, Doom about 3 KB) and `Global variables use` 242,676 B to 190,708 B. Run Build Arduino Libs first, then rebuild the firmware; without the new libs the markers do nothing. Free internal RAM on a device is not measured yet. Left in internal RAM on purpose: the chess engine tables, the raycaster z-buffer, the translation cache, the NES PPU, task control blocks and anything with a `String` member. Script: `tools/patches/apply_psram_bss_patch.py`\n")
FIXED = ("- Fixed: nine stray `EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR ` markers in `third_party/esp_doom_core` (before a `{`, before comments and before a `constexpr`) were harmless while the libraries did not enable the option, but broke the build once they did. They are removed (`tools/patches/fix_doom_ext_ram.py`); the eight real markers (`xLUT`, `yLUT`, `row`, `rgb565_palette`, `musBus`, `musicScratch`, `acc`, `out`) stay, so those Doom buffers now live in PSRAM. Doom not yet tested on a device after this change\n")
if MARK in c: print("changelog sudah")
else:
    lines = c.splitlines(True)
    idx = [i for i, l in enumerate(lines) if l.startswith("- Changed: seven large static arrays moved")]
    if len(idx) == 1: lines[idx[0]] = CHANGED + FIXED
    else:
        u = [i for i, l in enumerate(lines) if l.strip() == "## Unreleased"]
        if len(u) != 1: print("GAGAL: '## Unreleased' tidak ditemukan"); sys.exit(1)
        lines.insert(u[0] + 1, CHANGED + FIXED)
    CL.write_text("".join(lines)); print("changelog diperbarui")
R = pathlib.Path("tools/patches/README.md")
if R.exists():
    r = R.read_text()
    if "fix_doom_ext_ram.py" not in r:
        a = [l for l in r.splitlines(True) if l.startswith("| `apply_psram_bss_patch.py`")]
        if len(a) == 1:
            R.write_text(r.replace(a[0], a[0] + "| `fix_doom_ext_ram.py` | `DOOM_EXTRAM_FIX_V1` | History (removes stray `EXT_RAM_BSS_ATTR` pairs in `third_party/esp_doom_core`; run from the repository root) |\n", 1)); print("README patches diperbarui")
