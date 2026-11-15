#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""apply_doom_multiwad.py (DOOM_MULTIWAD_V1): DOOM mencari WAD otomatis di root SD, bukan cuma /doom1.wad.
Urutan: doom2.wad, doom1.wad, doom.wad, freedoom2.wad, freedoom1.wad, plutonia.wad, tnt.wad.
Mode game dikenali engine dari NAMA file (d_iwad.c) atau isi lump (MAP01 = Doom 2). Jalankan dari root repo. Idempotent."""
import sys, os
P = "doom_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu" % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "DOOM_MULTIWAD_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)
def rep(old, new, label):
    global s
    if s.count(old) != 1: sys.exit("ERROR: '%s' ketemu %d kali (harus 1). Dibatalkan." % (label, s.count(old)))
    s = s.replace(old, new)

rep("extern volatile uint32_t padFrames; // gamepad_renphone.ino\n",
'''// DOOM_MULTIWAD_V1: cari WAD otomatis di root SD (urutan = prioritas). Nama harus dikenal engine (d_iwad.c)
// atau isinya punya lump MAP01/E1M1.
static const char* const DOOM_WAD_CANDIDATES[] = {
  "/doom2.wad", "/doom1.wad", "/doom.wad", "/freedoom2.wad", "/freedoom1.wad", "/plutonia.wad", "/tnt.wad"
};
static const char* doomWadSel = nullptr;
static const char* doomFindWad(){
  for(size_t i=0;i<sizeof(DOOM_WAD_CANDIDATES)/sizeof(DOOM_WAD_CANDIDATES[0]);i++)
    if(SD_MMC.exists(DOOM_WAD_CANDIDATES[i])) return DOOM_WAD_CANDIDATES[i];
  return nullptr;
}

extern volatile uint32_t padFrames; // gamepad_renphone.ino
''', "kandidat")

rep("  DoomGlue_Begin(DOOM_WAD_PATH);\n",
    "  DoomGlue_Begin(doomWadSel ? doomWadSel : DOOM_WAD_PATH);   // DOOM_MULTIWAD_V1\n", "begin")

rep('''  if(!SD_MMC.exists(DOOM_WAD_PATH)){
    doomErrorMsg = String("WAD tidak ditemukan:\\n") + DOOM_WAD_PATH +
                   "\\nTaruh file WAD (mis. doom1.wad)\\npersis di path itu di SD.";
    return;
  }''', '''  doomWadSel = doomFindWad();   // DOOM_MULTIWAD_V1
  if(!doomWadSel){
    doomErrorMsg = String("WAD tidak ditemukan di SD.\\nTaruh di root SD: doom2.wad,\\ndoom1.wad, freedoom2.wad, plutonia.wad\\natau tnt.wad (nama persis).");
    return;
  }''', "cek wad")

if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: DOOM_MULTIWAD_V1 diterapkan")
