#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_audio_init_fix.py -- cegah crash saat panggilan masuk (audio dering -> modul CAM/BT).
#
# Temuan (dari penanda crash): "Reset: CRASH task=50 ui=41". Langkah 50 = awal callAudioStart()
# tepat sebelum musExtBegin(). musExtBegin() -> musBegin() menyalakan link ke CAM PERTAMA KALI,
# dan di situ ada baca NVS (Preferences). Itu dijalankan dari callTask yang stack-nya di PSRAM;
# akses flash/NVS dari task ber-stack PSRAM bisa membuat ESP32-S3 panic.
# Selama link belum pernah dinyalakan (mis. habis restart & app Musik belum dibuka), INVITE masuk
# langsung crash.
#
# Perbaikan:
#  1. link CAM dinyalakan dari UI thread (di callEnsureInit), BUKAN dari callTask.
#  2. callAudioStart() menolak (tanpa crash) kalau link belum menyala; panggilan tetap
#     berdering di layar, hanya nada dering ke TWS yang dilewati.
#
#     python3 apply_call_audio_init_fix.py
import sys, os

FC, FM = 'call_renphone.ino', 'musicbt_renphone.ino'
for f in (FC, FM):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)
c = open(FC, encoding='utf-8').read()
m = open(FM, encoding='utf-8').read()
if 'musExtPrepare' in c or 'musExtPrepare' in m:
    print("Patch audio-init sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

# --- musicbt: dua fungsi publik baru ---
m = rep(m, "uint32_t musExtFree() {",
'''// dipanggil dari UI thread (callEnsureInit): nyalakan link ke CAM di luar callTask (stack PSRAM, tak boleh sentuh NVS)
void musExtPrepare() { btCcEnabled(); musBegin(); }
bool musExtLinkUp()  { return musStarted; }
uint32_t musExtFree() {''', "musicbt")

# --- call: deklarasi maju ---
c = rep(c, "void wakeYieldMic(uint32_t holdMs);",
           "void wakeYieldMic(uint32_t holdMs);\nvoid musExtPrepare();\nbool musExtLinkUp();", "decl")

# --- call: nyalakan link dari UI thread ---
c = rep(c, "  if (!callCfgLoaded) callCfgLoad();\n  if (callTask) return true;",
           "  if (!callCfgLoaded) callCfgLoad();\n  musExtPrepare();                                  // link CAM harus dinyalakan di sini (UI thread), bukan di callTask\n  if (callTask) return true;", "ensureinit")

# --- call: pengaman di callAudioStart ---
c = rep(c, "  if (musExtActive()) return 9;                      // pemutar AVI sedang memakai jalur ini\n",
           "  if (musExtActive()) return 9;                      // pemutar AVI sedang memakai jalur ini\n"
           "  if (!musExtLinkUp()) return 5;                     // link CAM belum menyala: jangan inisialisasi dari callTask\n", "audiostart")
c = rep(c, '    case 9: return "Audio dipakai pemutar lain";',
           '    case 5: return "Audio belum siap (buka Telepon dulu)";\n    case 9: return "Audio dipakai pemutar lain";', "audioerr")

open(FC, 'w', encoding='utf-8').write(c)
open(FM, 'w', encoding='utf-8').write(m)
print("OK: link CAM dinyalakan dari UI thread + pengaman callAudioStart")
