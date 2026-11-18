# DOOM_FFAT_PRELOAD_V1
# Fix crash: assert esp_task_stack_is_sane_cache_disabled() saat DOOM baca WAD dari FFat.
# Penyebab : task DOOM stack-nya di PSRAM; akses FFat (flash internal) mematikan cache -> assert.
# Solusi   : kalau WAD ada di FFat, WAD dimuat UTUH ke PSRAM oleh task ber-stack internal
#            (doomEnter), lalu engine membacanya dari memori (wad.mapped = zero-copy).
#            Selama mode ini fopen() engine (savegame/config) diblok supaya tak menyentuh flash.
# Jalankan dari root repo ren_phone:  python3 apply_doom_ffat_preload.py
import sys, os

def load(f):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
    return open(f, encoding='utf-8').read()

def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:70])); sys.exit(1)
    return s.replace(old, new)

CORE = 'esp_doom_core/'
W = CORE + 'w_file_esp32.cpp'
I = 'doom_renphone.ino'

w = load(W); ino = load(I)
if 'DOOM_FFAT_PRELOAD_V1' in w or 'DOOM_FFAT_PRELOAD_V1' in ino:
    print("Patch sudah terpasang, dilewati"); sys.exit(0)

# ---------------- w_file_esp32.cpp ----------------
w = rep(w, "extern fs::FS* DG_FS;\n", '''extern fs::FS* DG_FS;

// DOOM_FFAT_PRELOAD_V1: WAD dari FFat dimuat penuh ke PSRAM (lihat DG_WadPreload di bawah).
static uint8_t* dgWadMem = nullptr;
static size_t   dgWadLen = 0;
static char     dgWadName[64] = {0};   // basename WAD yg dipreload
static volatile bool dgFlashProtect = false;   // true -> engine DILARANG menyentuh filesystem flash

static const char* dgBaseName(const char* p)
{
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static bool dgIsPreloaded(const char* path)
{
    return dgWadMem && path && strcasecmp(dgBaseName(path), dgWadName) == 0;
}
''', 'w:globals')

w = rep(w, '''int DG_FileExists(const char *path)
{
    return DG_FS->exists(path);''', '''int DG_FileExists(const char *path)
{
    // Mode proteksi flash: jangan stat() FFat dari task ber-stack PSRAM (assert cache).
    if (dgFlashProtect) return dgIsPreloaded(path) ? 1 : 0;
    return DG_FS->exists(path);''', 'w:exists')

w = rep(w, '''    File file = DG_FS->open(path);
    if (!file)
    {
        Serial.println("OPEN FAILED");
        return nullptr;
    }
    
    esp32_wad_file_t *result = new esp32_wad_file_t();''', '''    if (dgIsPreloaded(path))
    {
        esp32_wad_file_t *m = new esp32_wad_file_t();
        if (!m) return nullptr;
        m->wad.file_class = &esp32_wad_file;
        m->wad.mapped = dgWadMem;          // zero-copy: lump langsung menunjuk ke PSRAM
        m->wad.length = (unsigned int)dgWadLen;
        Serial.printf("OK (%u bytes, dari PSRAM)\\n", m->wad.length);
        return &m->wad;
    }

    File file = DG_FS->open(path);
    if (!file)
    {
        Serial.println("OPEN FAILED");
        return nullptr;
    }
    
    esp32_wad_file_t *result = new esp32_wad_file_t();''', 'w:open')

w = rep(w, '''    esp32_wad_file_t* f = (esp32_wad_file_t*)wad;

    if (!f->file.seek(offset))''', '''    esp32_wad_file_t* f = (esp32_wad_file_t*)wad;

    if (f->wad.mapped)   // WAD ada di PSRAM: salin langsung, tanpa flash
    {
        if (offset >= f->wad.length) return 0;
        size_t avail = f->wad.length - offset;
        if (len > avail) len = avail;
        memcpy(buffer, (const uint8_t*)f->wad.mapped + offset, len);
        return len;
    }

    if (!f->file.seek(offset))''', 'w:read')

w = w.rstrip('\n') + '''

// ---------------------------------------------------------------------------
// DOOM_FFAT_PRELOAD_V1
// Dipanggil dari doomEnter() (task ber-stack RAM internal -> AMAN akses flash).
// Return 1 = sukses (mode proteksi flash aktif), 0 = gagal (PSRAM kurang / file hilang).
// ---------------------------------------------------------------------------
extern "C" void DG_WadPreloadFree(void)
{
    if (dgWadMem) { heap_caps_free(dgWadMem); dgWadMem = nullptr; }
    dgWadLen = 0;
    dgWadName[0] = 0;
    dgFlashProtect = false;
}

extern "C" int DG_WadPreload(fs::FS* fs, const char* path)
{
    DG_WadPreloadFree();
    if (!fs || !path) return 0;

    File f = fs->open(path, "r");
    if (!f) { Serial.println("[wad] preload: open gagal"); return 0; }

    size_t sz = f.size();
    if (sz < 12) { f.close(); return 0; }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf)
    {
        Serial.printf("[wad] preload: PSRAM kurang utk %u byte (maks blok %u)\\n",
                      (unsigned)sz, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        f.close();
        return 0;
    }

    const size_t CH = 4096;
    uint8_t* bounce = (uint8_t*)heap_caps_malloc(CH, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!bounce) { heap_caps_free(buf); f.close(); return 0; }

    size_t total = 0;
    while (total < sz)
    {
        size_t want = sz - total; if (want > CH) want = CH;
        size_t got = f.read(bounce, want);
        if (got == 0) break;
        memcpy(buf + total, bounce, got);
        total += got;
        if ((total & 0xFFFF) == 0) yield();
    }
    heap_caps_free(bounce);
    f.close();

    if (total != sz)
    {
        Serial.printf("[wad] preload: baca tidak lengkap %u/%u\\n", (unsigned)total, (unsigned)sz);
        heap_caps_free(buf);
        return 0;
    }

    dgWadMem = buf;
    dgWadLen = sz;
    strncpy(dgWadName, dgBaseName(path), sizeof(dgWadName) - 1);
    dgWadName[sizeof(dgWadName) - 1] = 0;
    dgFlashProtect = true;
    Serial.printf("[wad] preload %s -> PSRAM %u byte, sisa PSRAM %u\\n", dgWadName, (unsigned)sz,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 1;
}

// Pembungkus fopen() utk engine: saat proteksi aktif return NULL (savegame/config tidak
// ditulis/dibaca dari FFat). Engine sudah menangani NULL (hanya "tidak bisa simpan").
extern "C" FILE* DG_Fopen(const char* path, const char* mode)
{
    if (dgFlashProtect) return NULL;
    return fopen(path, mode);
}
'''
open(W, 'w', encoding='utf-8').write(w)

# ---------------- engine .c: fopen -> DG_Fopen ----------------
import re
for fn in ['g_game.c', 'm_menu.c', 'm_misc.c', 'm_config.c']:
    p = CORE + fn
    s = load(p)
    if 'DG_Fopen' in s: continue
    s2, n = re.subn(r'\bfopen\s*\(', 'DG_Fopen(', s)
    m = re.search(r'^#include[^\n]*\n', s2, re.M)
    if not m:
        print("ERROR: %s tidak punya #include" % p); sys.exit(1)
    decl = '#include <stdio.h>\nFILE *DG_Fopen(const char *path, const char *mode); // DOOM_FFAT_PRELOAD_V1\n'
    s2 = s2[:m.start()] + decl + s2[m.start():]
    open(p, 'w', encoding='utf-8').write(s2)
    print("  %s: %d fopen diganti" % (fn, n))

# ---------------- doom_renphone.ino ----------------
ino = rep(ino, "extern volatile uint32_t padFrames; // gamepad_renphone.ino\n",
"""extern volatile uint32_t padFrames; // gamepad_renphone.ino
// DOOM_FFAT_PRELOAD_V1: lihat esp_doom_core/w_file_esp32.cpp
extern "C" int  DG_WadPreload(fs::FS* fs, const char* path);
extern "C" void DG_WadPreloadFree(void);
""", 'ino:decl')

ino = rep(ino, "  DoomGlue_Shutdown();\n  doomDisplay.close();",
"  DoomGlue_Shutdown();\n  DG_WadPreloadFree();   // DOOM_FFAT_PRELOAD_V1\n  doomDisplay.close();", 'ino:free')

ino = rep(ino, "  // DOOM dipaksa LANDSCAPE (lihat catatan di hw_conf.h)",
"""  // DOOM_FFAT_PRELOAD_V1: task DOOM ber-stack PSRAM -> akses FFat (flash internal) dari situ
  // memicu assert esp_task_stack_is_sane_cache_disabled() & reboot. WAD dari FFat dimuat UTUH ke
  // PSRAM di sini (task pemanggil ber-stack internal = aman), lalu engine baca dari memori.
  // WAD dari SD tidak perlu (SDMMC tidak mematikan cache).
  if(doomWadFs != (fs::FS*)&SD_MMC){
    if(!DG_WadPreload(doomWadFs, doomWadSel)){
      doomErrorMsg = "WAD di FFat gagal dimuat ke PSRAM\\n(PSRAM kurang / WAD terlalu besar).\\nPakai kartu SD utk WAD besar.";
      return;
    }
  }

  // DOOM dipaksa LANDSCAPE (lihat catatan di hw_conf.h)""", 'ino:preload')

ino = rep(ino, '    doomErrorMsg = "Gagal membuat task DOOM (RAM internal habis).";',
'    DG_WadPreloadFree();   // DOOM_FFAT_PRELOAD_V1\n    doomErrorMsg = "Gagal membuat task DOOM (RAM internal habis).";', 'ino:fail')

open(I, 'w', encoding='utf-8').write(ino)
print("OK: DOOM WAD FFat -> preload PSRAM (anti crash cache-disabled)")
