# DOOM_ZONE_SMALL_V1: kecilkan zone Doom saat WAD sudah di-preload ke PSRAM (zero-copy / mapped).
# Lump WAD tidak lagi dialokasi dari zone -> zone cukup 1-3 MiB (bukan 4-6 MiB), sisanya buat WAD.
# Jalankan dari root repo ren_phone:  py apply_doom_zone_small.py
import sys, os
def load(f):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
    return open(f, encoding='utf-8').read()
def rep(s, old, new, n, name):
    if s.count(old) != n:
        print("ERROR [%s]: anchor ketemu %d kali (harusnya %d): %r" % (name, s.count(old), n, old[:60])); sys.exit(1)
    return s.replace(old, new)

W = 'esp_doom_core/w_file_esp32.cpp'
I = 'esp_doom_core/i_system.c'
w = load(W); s = load(I)
if 'DOOM_ZONE_SMALL_V1' in w or 'DOOM_ZONE_SMALL_V1' in s:
    print("Sudah terpasang, dilewati"); sys.exit(0)
if 'DG_WadPreload' not in w:
    print("ERROR: patch apply_doom_ffat_preload.py belum terpasang"); sys.exit(1)

w = w.rstrip('\n') + '''

// DOOM_ZONE_SMALL_V1: engine tanya apakah WAD sudah ada di PSRAM (mapped) -> zone boleh kecil.
extern "C" int DG_WadIsPreloaded(void)
{
    return dgWadMem != nullptr;
}
'''

s = rep(s, "#define ZONE_PSRAM_RESERVE (256 * 1024)\n",
"""#define ZONE_PSRAM_RESERVE (256 * 1024)
// DOOM_ZONE_SMALL_V1: WAD di PSRAM (mapped) -> sisakan lebih banyak PSRAM utk screens/tabel
extern int DG_WadIsPreloaded(void);
#define ZONE_RESERVE_NOW() (DG_WadIsPreloaded() ? (size_t)(640 * 1024) : (size_t)ZONE_PSRAM_RESERVE)
""", 1, 'i:reserve')
s = rep(s, "(size_t)*size + ZONE_PSRAM_RESERVE >", "(size_t)*size + ZONE_RESERVE_NOW() >", 2, 'i:use')
s = rep(s, "        default_ram = DEFAULT_RAM;\n        min_ram = MIN_RAM;\n",
"""        default_ram = DEFAULT_RAM;
        min_ram = MIN_RAM;
#ifdef ARDUINO
        // DOOM_ZONE_SMALL_V1: lump WAD dibaca langsung dari PSRAM, tidak lewat zone
        if (DG_WadIsPreloaded()) { default_ram = 3; min_ram = 1; }
#endif
""", 1, 'i:size')

open(W, 'w', encoding='utf-8').write(w)
open(I, 'w', encoding='utf-8').write(s)
print("OK: zone Doom otomatis 1-3 MiB kalau WAD di-preload")
