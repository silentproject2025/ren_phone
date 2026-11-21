#include "w_file_esp32.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string.h>
#include "esp_heap_caps.h"

extern "C"
{
#include "z_zone.h"
}

extern fs::FS* DG_FS;

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

typedef struct
{
    wad_file_t wad;
    File file;
    const uint8_t* mem;   // DOOM_NOMAP_DEBUG_V1: non-NULL = WAD ada di PSRAM (preload)
} esp32_wad_file_t;

extern "C"
{

int DG_FileExists(const char *path)
{
    // Mode proteksi flash: jangan stat() FFat dari task ber-stack PSRAM (assert cache).
    if (dgFlashProtect) return dgIsPreloaded(path) ? 1 : 0;
    return DG_FS->exists(path);
}

void *DG_FileOpen(const char *path)
{
    File *f = new File(DG_FS->open(path));

    if (!(*f))
    {
        delete f;
        return NULL;
    }

    return f;
}

void DG_FileClose(void *handle)
{
    File *f = (File *)handle;

    f->close();

    delete f;
}

size_t DG_FileRead(void *handle, void *buffer, size_t len)
{
    return ((File *)handle)->read((uint8_t *)buffer, len);
}

int DG_FileSeek(void *handle, unsigned int offset)
{
    return ((File *)handle)->seek(offset);
}

unsigned int DG_FileSize(void *handle)
{
    return ((File *)handle)->size();
}

}

static wad_file_t* W_ESP32_OpenFile(char* path)
{
    if (DG_FS == nullptr)
    {
        Serial.println("DG_FS == NULL");
        return nullptr;
    }

    if (dgIsPreloaded(path))
    {
        esp32_wad_file_t *m = new esp32_wad_file_t();
        if (!m) return nullptr;
        m->wad.file_class = &esp32_wad_file;
        m->mem = dgWadMem;
        // DOOM_NOMAP_DEBUG_V1: zero-copy dimatikan (pointer lump tak dijamin rata 4 byte).
        // 1 = lump langsung menunjuk ke PSRAM, 0 = disalin ke zone.
        #define DG_WAD_ZEROCOPY 0
        m->wad.mapped = DG_WAD_ZEROCOPY ? dgWadMem : NULL;
        m->wad.length = (unsigned int)dgWadLen;
        Serial.printf("OK (%u bytes, dari PSRAM)\n", m->wad.length);
        return &m->wad;
    }

    File file = DG_FS->open(path);
    if (!file)
    {
        Serial.println("OPEN FAILED");
        return nullptr;
    }
    
    esp32_wad_file_t *result = new esp32_wad_file_t();

    if (!result)
    {
        Serial.println("malloc failed");
        return nullptr;
    }

    result->wad.file_class = &esp32_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = file.size();
    result->file = file;

    Serial.printf("OK (%u bytes)\n", result->wad.length);

    return &result->wad;
}

// Buffer perantara di RAM internal (DMA-capable). Baca SD -> sini dulu, baru
// disalin ke tujuan. Menghindari baca SD langsung ke PSRAM (stack task Doom &
// zone memory ada di PSRAM) yang bikin data hasil baca kosong (nol).
#define WAD_BOUNCE_SZ 4096
static uint8_t *wadBounce = nullptr;

static void W_ESP32_CloseFile(wad_file_t* wad)
{
    esp32_wad_file_t *f = (esp32_wad_file_t *)wad;

    f->file.close();

    delete f;

    if (wadBounce) { heap_caps_free(wadBounce); wadBounce = nullptr; }
}

static size_t W_ESP32_ReadRaw(
    wad_file_t* wad,
    unsigned int offset,
    void* buffer,
    size_t len)
{
    esp32_wad_file_t* f = (esp32_wad_file_t*)wad;

    if (f->mem)   // WAD ada di PSRAM: salin langsung, tanpa flash
    {
        if (offset >= f->wad.length) return 0;
        size_t avail = f->wad.length - offset;
        if (len > avail) len = avail;
        memcpy(buffer, f->mem + offset, len);
        return len;
    }

    if (!f->file.seek(offset))
    {
        Serial.printf("[wad] seek GAGAL offset=%u\n", offset);
        return 0;
    }

    if (!wadBounce)
        wadBounce = (uint8_t *)heap_caps_malloc(WAD_BOUNCE_SZ,
                        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);

    if (!wadBounce)
    {
        Serial.println("[wad] bounce buffer gagal, baca langsung");
        return f->file.read((uint8_t*)buffer, len);
    }

    uint8_t *dst = (uint8_t *)buffer;
    size_t total = 0;

    while (total < len)
    {
        size_t want = len - total;
        if (want > WAD_BOUNCE_SZ) want = WAD_BOUNCE_SZ;

        size_t got = f->file.read(wadBounce, want);
        if (got == 0) break;

        if (offset == 0 && total == 0)
            Serial.printf("[wad] byte awal dari SD: %02X %02X %02X %02X (got=%u)\n",
                          wadBounce[0], wadBounce[1], wadBounce[2], wadBounce[3], (unsigned)got);

        memcpy(dst + total, wadBounce, got);
        total += got;
        if (got < want) break;
    }

    return total;
}

// AETHER_SD_FIX_V1: hook opsional (weak). Di board SD-SPI, layar menahan transaksi SPI sepanjang sesi DOOM;
// baca SD di bus yg sama saat itu = tabrakan -> hang/reboot. Hook melepas & mengambil lagi transaksi layar.
extern "C" { void (*DG_SdBusRelease)(void) = nullptr; void (*DG_SdBusAcquire)(void) = nullptr; }

static size_t W_ESP32_Read(wad_file_t* wad, unsigned int offset, void* buffer, size_t len)
{
    esp32_wad_file_t* f = (esp32_wad_file_t*)wad;
    if (f->mem) return W_ESP32_ReadRaw(wad, offset, buffer, len);   // dari PSRAM: bus SPI tidak dipakai
    if (DG_SdBusRelease) DG_SdBusRelease();
    size_t r = W_ESP32_ReadRaw(wad, offset, buffer, len);
    if (DG_SdBusAcquire) DG_SdBusAcquire();
    return r;
}

extern "C"
{

wad_file_class_t esp32_wad_file =
{
    W_ESP32_OpenFile,
    W_ESP32_CloseFile,
    W_ESP32_Read
};

}

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
        Serial.printf("[wad] preload: PSRAM kurang utk %u byte (maks blok %u)\n",
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
        Serial.printf("[wad] preload: baca tidak lengkap %u/%u\n", (unsigned)total, (unsigned)sz);
        heap_caps_free(buf);
        return 0;
    }

    dgWadMem = buf;
    dgWadLen = sz;
    strncpy(dgWadName, dgBaseName(path), sizeof(dgWadName) - 1);
    dgWadName[sizeof(dgWadName) - 1] = 0;
    dgFlashProtect = true;
    Serial.printf("[wad] preload %s -> PSRAM %u byte, sisa PSRAM %u\n", dgWadName, (unsigned)sz,
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

// DOOM_ZONE_SMALL_V1: engine tanya apakah WAD sudah ada di PSRAM (mapped) -> zone boleh kecil.
extern "C" int DG_WadIsPreloaded(void)
{
    return dgWadMem != nullptr;
}
