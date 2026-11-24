#!/usr/bin/env python3
"""PSRAM_BSS_V1: pindahkan array BSS besar (UI/game, bukan DMA/ISR) ke PSRAM + nyalakan opsi di workflow libs.
Pakai dari root repo: python3 tools/patches/apply_psram_bss_patch.py [--check]   (idempoten)"""
import re, sys, pathlib
CHECK = "--check" in sys.argv
ATTR = "EXT_RAM_BSS_ATTR"
P = pathlib.Path("firmware/phone")
# (file, deklarasi persis tanpa titik koma)
TARGETS = [
  ("about_renphone.ino",    "static AboutRow aboutRows[ABOUT_MAX_ROWS]"),
  ("features_renphone.ino", "static char     memoNames[MEMO_MAX_FILES][32]"),
  ("gallery_renphone.ino",  "static GalAlbum  galAlbums[GAL_MAX_ALBUMS]"),
  ("kamus_renphone.ino",    "static char        kmsUserBuf[KMS_USER_MAX][KMS_MAXW + 1]"),
  ("nyxstore_renphone.ino", "static nyxstore::Entry nsApps[NYXSTORE_MAX_APPS]"),
  ("phone.ino",             "SnakeSeg snakeBody[SNAKE_MAXLEN]"),
  ("phone.ino",             "BootDiskP bootDisk[BOOT_DISK_N]"),
]
bad = 0
for fn, decl in TARGETS:
    f = P / fn; t = f.read_text()
    pat = re.compile(r'^' + re.escape(decl) + r'(\s*);', re.M)
    done = re.compile(r'^' + re.escape(decl) + r'\s+' + ATTR + r'\s*;', re.M)
    if done.search(t): print(f"sudah   {fn}: {decl.split()[-1]}"); continue
    if len(pat.findall(t)) != 1: print(f"GAGAL   {fn}: '{decl}' tidak ditemukan tepat 1x"); bad += 1; continue
    if not CHECK: f.write_text(pat.sub(decl + " " + ATTR + ";", t, count=1))
    print(f"{'cocok ' if CHECK else 'dipasang'} {fn}: {decl.split()[-1]}")
# --- workflow libs: izinkan BSS di PSRAM ---
W = pathlib.Path(".github/workflows/build-arduino-libs.yml"); w = W.read_text()
A1 = "            echo 'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y'\n"
N1 = A1 + "            echo 'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y'   # PSRAM_BSS_V1\n"
A2 = "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP; do"
N2 = "CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP; do"
if "PSRAM_BSS_V1" in w: print("sudah   workflow libs")
elif w.count(A1) != 1 or w.count(A2) != 1: print("GAGAL   anchor workflow libs tidak cocok"); bad += 1
else:
    if not CHECK: W.write_text(w.replace(A1, N1).replace(A2, N2))
    print(f"{'cocok ' if CHECK else 'dipasang'} workflow libs")
sys.exit(1 if bad else 0)
