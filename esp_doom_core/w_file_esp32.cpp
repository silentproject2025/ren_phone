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

typedef struct
{
    wad_file_t wad;
    File file;
} esp32_wad_file_t;

extern "C"
{

int DG_FileExists(const char *path)
{
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

static size_t W_ESP32_Read(
    wad_file_t* wad,
    unsigned int offset,
    void* buffer,
    size_t len)
{
    esp32_wad_file_t* f = (esp32_wad_file_t*)wad;

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

extern "C"
{

wad_file_class_t esp32_wad_file =
{
    W_ESP32_OpenFile,
    W_ESP32_CloseFile,
    W_ESP32_Read
};

}