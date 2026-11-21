#include "w_file_esp32.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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

// DOOM_FFAT_STREAM_V1: WAD dari FFat TIDAK lagi dimuat penuh ke PSRAM (doom1.wad 4MB menyisakan ~2MB -> zone cuma 1MiB
// -> Z_Malloc failed di E1M3). Sebagai gantinya WAD dibaca on-demand lewat task kecil ber-stack RAM INTERNAL
// ("wadio"): task DOOM ber-stack PSRAM dilarang menyentuh flash (assert cache), jadi semua baca FFat dilakukan task
// ini, task DOOM cuma kirim permintaan & menunggu. Zone kembali besar (6/5 MiB). 0 = perilaku lama (preload).
#define DG_FFAT_STREAM 1
#define DG_STREAM_CH   4096
static File             dgStreamFile;                 // dibuka di doomEnter, dipakai HANYA oleh task wadio
static bool             dgStream = false;
static TaskHandle_t     dgStreamTask = nullptr;
static SemaphoreHandle_t dgReqSem = nullptr, dgDoneSem = nullptr;
static uint8_t*         dgStreamBuf = nullptr;        // RAM internal (DMA-capable)
static volatile unsigned dgReqOff = 0;
static volatile size_t   dgReqLen = 0, dgReqGot = 0;
static volatile bool     dgStreamQuit = false;

static void dgStreamWorker(void*)
{
    for (;;)
    {
        xSemaphoreTake(dgReqSem, portMAX_DELAY);
        if (dgStreamQuit) break;
        size_t got = 0;
        if (dgStreamFile.seek(dgReqOff)) got = dgStreamFile.read(dgStreamBuf, dgReqLen);
        dgReqGot = got;
        xSemaphoreGive(dgDoneSem);
    }
    dgStreamFile.close();
    xSemaphoreGive(dgDoneSem);   // jangan sentuh semaphore lagi sesudah ini
    vTaskDelete(NULL);
}

static size_t dgStreamRead(unsigned offset, void* dst, size_t len)
{
    uint8_t* d = (uint8_t*)dst;
    size_t total = 0;
    while (total < len)
    {
        size_t want = len - total;
        if (want > DG_STREAM_CH) want = DG_STREAM_CH;
        dgReqOff = offset + (unsigned)total;
        dgReqLen = want;
        xSemaphoreGive(dgReqSem);
        if (xSemaphoreTake(dgDoneSem, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            Serial.println("[wad] stream: timeout menunggu task wadio");
            break;
        }
        size_t got = dgReqGot;
        if (got == 0) break;
        memcpy(d + total, dgStreamBuf, got);
        total += got;
        if (got < want) break;
    }
    return total;
}

static void dgStreamStop(void)
{
    if (!dgStream) return;
    if (dgStreamTask && dgReqSem && dgDoneSem)
    {
        dgStreamQuit = true;
        xSemaphoreGive(dgReqSem);
        xSemaphoreTake(dgDoneSem, pdMS_TO_TICKS(2000));
    }
    dgStreamTask = nullptr;
    if (dgReqSem)  { vSemaphoreDelete(dgReqSem);  dgReqSem = nullptr; }
    if (dgDoneSem) { vSemaphoreDelete(dgDoneSem); dgDoneSem = nullptr; }
    if (dgStreamBuf) { heap_caps_free(dgStreamBuf); dgStreamBuf = nullptr; }
    dgStream = false;
}

static const char* dgBaseName(const char* p)
{
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static bool dgIsPreloaded(const char* path)
{
    return (dgWadMem || dgStream) && path && strcasecmp(dgBaseName(path), dgWadName) == 0;
}

typedef struct
{
    wad_file_t wad;
    File file;
    const uint8_t* mem;   // DOOM_NOMAP_DEBUG_V1: non-NULL = WAD ada di PSRAM (preload)
    bool stream;          // DOOM_FFAT_STREAM_V1: true = baca lewat task wadio
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
        m->stream = dgStream;
        // DOOM_NOMAP_DEBUG_V1: zero-copy dimatikan (pointer lump tak dijamin rata 4 byte).
        // 1 = lump langsung menunjuk ke PSRAM, 0 = disalin ke zone.
        #define DG_WAD_ZEROCOPY 0
        m->wad.mapped = DG_WAD_ZEROCOPY ? dgWadMem : NULL;
        m->wad.length = (unsigned int)dgWadLen;
        Serial.printf(dgStream ? "OK (%u bytes, stream FFat via wadio)\n" : "OK (%u bytes, dari PSRAM)\n", m->wad.length);
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

    if (f->stream)   // WAD di FFat: baca lewat task wadio (stack internal)
    {
        if (offset >= f->wad.length) return 0;
        size_t avail = f->wad.length - offset;
        if (len > avail) len = avail;
        return dgStreamRead(offset, buffer, len);
    }

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
    if (f->mem || f->stream) return W_ESP32_ReadRaw(wad, offset, buffer, len);   // PSRAM / FFat: bus SPI SD tidak dipakai
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
    dgStreamStop();
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

#if DG_FFAT_STREAM
    {
        dgStreamBuf = (uint8_t*)heap_caps_malloc(DG_STREAM_CH, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        dgReqSem  = xSemaphoreCreateBinary();
        dgDoneSem = xSemaphoreCreateBinary();
        dgStreamQuit = false;
        dgStreamFile = f;
        if (dgStreamBuf && dgReqSem && dgDoneSem &&
            xTaskCreate(dgStreamWorker, "wadio", 6144, NULL, 4, &dgStreamTask) == pdPASS)
        {
            dgStream = true;
            dgWadLen = sz;
            strncpy(dgWadName, dgBaseName(path), sizeof(dgWadName) - 1);
            dgWadName[sizeof(dgWadName) - 1] = 0;
            dgFlashProtect = true;
            Serial.printf("[wad] stream %s %u byte dari FFat (tidak di-preload), PSRAM bebas %u KB\n", dgWadName,
                          (unsigned)sz, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            return 1;
        }
        Serial.println("[wad] stream: gagal buat task/buffer, fallback preload ke PSRAM");
        dgStream = true;          // supaya dgStreamStop() membersihkan semaphore & buffer
        dgStreamTask = nullptr;
        dgStreamStop();
        dgStreamFile = File();
    }
#endif

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
