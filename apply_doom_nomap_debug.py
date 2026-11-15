# DOOM_NOMAP_DEBUG_V1:
#  1) WAD di PSRAM TIDAK lagi dipakai sbg pointer "mapped" (zero-copy). Lump disalin ke zone
#     (aman utk alignment), seperti saat WAD dibaca dari SD. Ubah DG_WAD_ZEROCOPY ke 1 utk coba lagi.
#  2) Penanda Serial di doomTaskFunc: kelihatan tick pertama macet di mana.
# Jalankan dari root repo:  py apply_doom_nomap_debug.py
import sys, os
def load(f):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
    return open(f, encoding='utf-8').read()
def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:60])); sys.exit(1)
    return s.replace(old, new)

W = 'esp_doom_core/w_file_esp32.cpp'; I = 'doom_renphone.ino'
w = load(W); ino = load(I)
if 'DOOM_NOMAP_DEBUG_V1' in w or 'DOOM_NOMAP_DEBUG_V1' in ino:
    print("Sudah terpasang, dilewati"); sys.exit(0)
if 'DG_WadPreload' not in w:
    print("ERROR: patch apply_doom_ffat_preload.py belum terpasang"); sys.exit(1)

w = rep(w, "    File file;\n} esp32_wad_file_t;",
"    File file;\n    const uint8_t* mem;   // DOOM_NOMAP_DEBUG_V1: non-NULL = WAD ada di PSRAM (preload)\n} esp32_wad_file_t;", 'w:struct')
w = rep(w, "        m->wad.mapped = dgWadMem;          // zero-copy: lump langsung menunjuk ke PSRAM\n",
"""        m->mem = dgWadMem;
        // DOOM_NOMAP_DEBUG_V1: zero-copy dimatikan (pointer lump tak dijamin rata 4 byte).
        // 1 = lump langsung menunjuk ke PSRAM, 0 = disalin ke zone.
        #define DG_WAD_ZEROCOPY 0
        m->wad.mapped = DG_WAD_ZEROCOPY ? dgWadMem : NULL;
""", 'w:open')
w = rep(w, "    if (f->wad.mapped)   // WAD ada di PSRAM: salin langsung, tanpa flash\n    {\n        if (offset >= f->wad.length) return 0;\n        size_t avail = f->wad.length - offset;\n        if (len > avail) len = avail;\n        memcpy(buffer, (const uint8_t*)f->wad.mapped + offset, len);",
"    if (f->mem)   // WAD ada di PSRAM: salin langsung, tanpa flash\n    {\n        if (offset >= f->wad.length) return 0;\n        size_t avail = f->wad.length - offset;\n        if (len > avail) len = avail;\n        memcpy(buffer, f->mem + offset, len);", 'w:read')

ino = rep(ino, "  DoomGlue_Begin(doomWadSel ? doomWadSel : DOOM_WAD_PATH);   // DOOM_MULTIWAD_V1\n  doomPlaying = true;\n",
"  DoomGlue_Begin(doomWadSel ? doomWadSel : DOOM_WAD_PATH);   // DOOM_MULTIWAD_V1\n  Serial.printf(\"[doom] Begin selesai | PSRAM bebas=%u internal=%u\\n\", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); // DOOM_NOMAP_DEBUG_V1\n  doomPlaying = true;\n", 'i:begin')
ino = rep(ino, "    uint32_t tk0 = micros();\n    DoomGlue_Tick();\n    stTickUs += micros() - tk0; stTicks++;\n",
"""    uint32_t tk0 = micros();
    static uint8_t dbgTk = 0;   // DOOM_NOMAP_DEBUG_V1: 3 tick pertama dicatat (kelihatan macet di mana)
    if(dbgTk < 3) Serial.printf("[doom] tick %u mulai\\n", (unsigned)dbgTk);
    DoomGlue_Tick();
    if(dbgTk < 3){ Serial.printf("[doom] tick %u selesai (%lu us)\\n", (unsigned)dbgTk, (unsigned long)(micros() - tk0)); dbgTk++; }
    stTickUs += micros() - tk0; stTicks++;
""", 'i:tick')
open(W, 'w', encoding='utf-8').write(w); open(I, 'w', encoding='utf-8').write(ino)
print("OK: zero-copy off + penanda debug Doom")
