#!/usr/bin/env python3
"""PSRAM_BSS_V3: pindahkan BSS UI/data "dingin" ke PSRAM (bukan ISR, bukan DMA, bukan jalur render/pencarian panas).
Sekitar 8,5 KB RAM internal. Perlu CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY (sudah diset oleh PSRAM_BSS_V1).
Sengaja TIDAK dipindah: TCB task, buffer DMA (nesPalWire, lineBuf, nyxLineBuf), jalur panas (chsNet, chsHab, chsAiSvB,
i18nKey/Idx, infZBuf), callback WiFi/audio (wsSub*, rxLast), array String/LGFX_Sprite, buku catur (blok engine juga
dikompilasi di PC harness yang tak punya EXT_RAM_BSS_ATTR).
Pakai dari root repo: python3 tools/patches/apply_psram_bss3_patch.py [--check]   (idempoten)"""
import re, sys, pathlib
CHECK = "--check" in sys.argv
ATTR = "EXT_RAM_BSS_ATTR"
P = pathlib.Path("firmware/phone")
# (file, deklarasi persis tanpa titik koma)
TARGETS = [
  ("emoji_renphone.ino",    "static uint16_t  emjCKey[3][EMJ_CACHE_N]"),
  ("emoji_renphone.ino",    "static uint32_t  emjCUse[3][EMJ_CACHE_N]"),
  ("nyxapp_renphone.ino",   "static char  nyxIds[NYXAPP_MAX_LIST][24]"),
  ("nyxapp_renphone.ino",   "static char  nyxNames[NYXAPP_MAX_LIST][NYX_MF_NAME_MAX]"),
  ("nyxextras_renphone.ino","static char nyxCmdQ[NYX_CMD_Q][NYX_CMD_LEN]"),
  ("ina219_renphone.ino",   "static int16_t  inaHistI[INA_HIST_N]"),
  ("ina219_renphone.ino",   "static uint16_t inaHistV[INA_HIST_N]"),
  ("ina219_renphone.ino",   "static int16_t  inaHistP[INA_HIST_N]"),
  ("devopt_renphone.ino",   "static DvHit    dvHits[DV_MAX_HIT]"),
  ("about_renphone.ino",    "static uint16_t       eggLnOff[EGG_MAX_LINES]"),
  ("about_renphone.ino",    "static uint16_t       eggLnCum[EGG_MAX_LINES]"),
  ("musicbt_renphone.ino",  "static char          musScanName[MUS_SCAN_MAX][40]"),
  ("kamus_renphone.ino",    "static const char* kmsUserPtr[KMS_USER_MAX]"),
  ("kamus_renphone.ino",    "static char        kmsSeen[12][KMS_MAXW + 1]"),
  ("call_renphone.ino",     "static char     callRecent[CALL_REC_MAX][44]"),
  ("call_renphone.ino",     "static char callDbgHist[6][40]"),
  ("musspk.h",              "static char              spkCmdUrl[256]"),
  ("features_renphone.ino", "static uint32_t memoSizes[MEMO_MAX_FILES]"),
  ("owner_renphone.ino",    "static uint32_t ownerCnt[OWNER_STAT_MAX]"),
]
# deklarasi berderet: ganti string persis
MULTI = [
  ("nyxstore_renphone.ino", "static char     nsUrl[220], nsJobErr[72];",
   "static char     nsUrl[220] EXT_RAM_BSS_ATTR, nsJobErr[72] EXT_RAM_BSS_ATTR;"),
]
bad = 0
for fn, decl in TARGETS:
    f = P / fn; t = f.read_text()
    pat  = re.compile(r'^' + re.escape(decl) + r'(\s*);', re.M)
    done = re.compile(r'^' + re.escape(decl) + r'\s+' + ATTR + r'\s*;', re.M)
    name = re.search(r'(\w+)\s*\[', decl).group(1)
    if done.search(t): print(f"sudah    {fn}: {name}"); continue
    if len(pat.findall(t)) != 1: print(f"GAGAL    {fn}: '{decl}' tidak ditemukan tepat 1x"); bad += 1; continue
    if not CHECK: f.write_text(pat.sub(decl + " " + ATTR + ";", t, count=1))
    print(f"{'cocok  ' if CHECK else 'dipasang'} {fn}: {name}")
for fn, old, new in MULTI:
    f = P / fn; t = f.read_text()
    if new in t: print(f"sudah    {fn}: {old.split()[2]}..."); continue
    if t.count(old) != 1: print(f"GAGAL    {fn}: '{old[:40]}...' tidak ditemukan tepat 1x"); bad += 1; continue
    if not CHECK: f.write_text(t.replace(old, new, 1))
    print(f"{'cocok  ' if CHECK else 'dipasang'} {fn}: {old.split()[2]}...")
print("PSRAM_BSS_V3:", "ADA YANG GAGAL" if bad else ("cek OK" if CHECK else "selesai"))
sys.exit(1 if bad else 0)
