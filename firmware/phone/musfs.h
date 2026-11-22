#pragma once
// =====================================================================
//  musfs.h  (SPK_FFAT_V1) -- musik dari FFat + pengukur RAM internal pemutar speaker
//
//  MASALAH: task musik (musTask) dan spkTask ber-STACK PSRAM. Task ber-stack PSRAM DILARANG menyentuh
//  flash (FFat) -> assert "esp_task_stack_is_sane_cache_disabled" lalu restart (kasus yg sama dgn DOOM,
//  lihat docs/troubleshooting.md). SD_MMC tidak kena karena bukan flash.
//
//  SOLUSI: semua akses FFat dikerjakan "worker" kecil ber-stack RAM INTERNAL (mffio). Worker dibuat
//  hanya saat ada permintaan dan menghapus dirinya sendiri setelah MFF_IDLE_MS tanpa permintaan, jadi
//  saat tidak memutar dari FFat ia tidak menahan RAM sama sekali.
//  Biaya RAM internal HANYA saat memutar dari FFat: stack 6 KB + buffer 2 KB + buffer sektor FatFS
//  file yg terbuka (~4 KB). Memutar dari SD tidak memakai ini.
//
//  Buffer tujuan di PSRAM tidak dibaca langsung dari flash: data lewat buffer pantul internal (mffBuf)
//  lalu memcpy -- pola yg sama dgn "wadio" milik DOOM.
//
//  JANGAN pakai tipe buatan sendiri di signature fungsi (prototipe otomatis arduino-cli).
// =====================================================================
#include <Arduino.h>
#include <FS.h>
#include <FFat.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>

#define MFF_STACK    6144
#define MFF_BUF      2048
#define MFF_IDLE_MS  3000

// true = daftar lagu & lagu dibaca dari FFat (diputuskan musScanPlaylist: SD dulu, FFat kalau SD tak ada / tak ada MP3)
static volatile bool musUseFfat = false;

typedef void (*MusFfFn)(void*);

static SemaphoreHandle_t mffMtx  = nullptr;   // satu pemanggil dalam satu waktu
static SemaphoreHandle_t mffReq  = nullptr;
static SemaphoreHandle_t mffDone = nullptr;
static TaskHandle_t      mffTask = nullptr;
static MusFfFn           mffFn   = nullptr;
static void*             mffArg  = nullptr;
static uint8_t*          mffBuf  = nullptr;   // pantul RAM internal; hanya valid selama worker hidup
static File              mffPlayF;            // file lagu yg sedang diputar spkTask (hanya disentuh di worker)

// dipanggil SEKALI dari musBegin() sebelum task musik dibuat
static void musFsInit() {
  if (mffMtx) return;
  mffMtx  = xSemaphoreCreateMutex();
  mffReq  = xSemaphoreCreateBinary();
  mffDone = xSemaphoreCreateBinary();
}

static void mffWorker(void*) {
  for (;;) {
    if (xSemaphoreTake(mffReq, pdMS_TO_TICKS(MFF_IDLE_MS)) == pdTRUE) {
      if (mffFn) mffFn(mffArg);
      xSemaphoreGive(mffDone);
      continue;
    }
    // sepi: mundur kalau tidak ada pemanggil yg sedang memegang mutex (kalau ada, ia akan segera memberi permintaan)
    if (xSemaphoreTake(mffMtx, 0) == pdTRUE) {
      if (mffBuf) { heap_caps_free(mffBuf); mffBuf = nullptr; }
      mffTask = nullptr;
      xSemaphoreGive(mffMtx);
      vTaskDelete(NULL);
    }
  }
}

// jalankan fn(arg) di worker (stack internal) dan tunggu selesai. false = worker gagal dibuat / FFat macet.
static bool mffRun(MusFfFn fn, void* arg) {
  if (!mffMtx || !mffReq || !mffDone) return false;
  if (xSemaphoreTake(mffMtx, pdMS_TO_TICKS(3000)) != pdTRUE) return false;
  bool ok = true;
  if (!mffTask) {
    mffBuf = (uint8_t*)heap_caps_malloc(MFF_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!mffBuf || xTaskCreate(mffWorker, "mffio", MFF_STACK, NULL, 4, &mffTask) != pdPASS) {
      if (mffBuf) { heap_caps_free(mffBuf); mffBuf = nullptr; }
      mffTask = nullptr;
      ok = false;
    }
  }
  if (ok) {
    xSemaphoreTake(mffDone, 0);                 // buang sisa token kalau permintaan sebelumnya sempat timeout
    mffFn = fn; mffArg = arg;
    xSemaphoreGive(mffReq);
    if (xSemaphoreTake(mffDone, pdMS_TO_TICKS(8000)) != pdTRUE) ok = false;
  }
  xSemaphoreGive(mffMtx);
  return ok;
}

// HANYA dipanggil di dalam worker: baca n byte dari f ke dst (boleh PSRAM) lewat buffer pantul internal.
static size_t mffReadI(File& f, uint8_t* dst, size_t n) {
  if (!mffBuf) return 0;
  size_t tot = 0;
  while (tot < n) {
    size_t w = n - tot; if (w > MFF_BUF) w = MFF_BUF;
    int g = f.read(mffBuf, w);
    if (g <= 0) break;
    memcpy(dst + tot, mffBuf, (size_t)g);
    tot += (size_t)g;
    if ((size_t)g < w) break;
  }
  return tot;
}

// ---------------------------------------------------------------------
//  Pengukur RAM (Serial) -- tag: pra (sebelum buka) / decoder / i2s / main-2d (2 dtk sesudah mulai) / stop
// ---------------------------------------------------------------------
static void musRamLog(const char* tag) {
  Serial.printf("[SPK-RAM] %-8s free=%uK blok=%uK min=%uK psram=%uK\n", tag,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
