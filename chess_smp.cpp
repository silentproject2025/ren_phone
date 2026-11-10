// =====================================================================
//  chess_smp.cpp -- core ke-2 untuk mesin catur NYX ("Lazy SMP")
//
//  File ini di-build sebagai translation unit TERPISAH dan meng-include blok ENGINE + AI dari
//  chess_renphone.ino (CI menyalinnya ke build_sketch/src/chess_engine.inc), jadi task pembantu
//  mendapat SALINAN mesin sendiri: papan, killer, history, tumpukan langkah, semuanya terpisah dari
//  mesin yang dipakai loop UI. Yang dibagi cuma tabel transposisi (TT, di PSRAM) + beberapa kata kendali.
//
//  Alur satu pencarian (dipanggil chsAiChoose):
//    chsSmpBegin()  -> salin posisi akar + batas waktu ke job, bangunkan task pembantu
//    (pembantu)     -> iterative deepening pada kedalaman utama+1, mengisi TT bersama
//    chsSmpEnd()    -> minta berhenti, tunggu pembantu parkir lagi
//    chsSmpNodes()  -> jumlah node pembantu (untuk NPS)
//  Task dibuat sekali (stack 10 KB di PSRAM, core = kebalikan core loop()), lalu diparkir.
//  Prioritas 1 (sama dgn loop) dan menyerahkan CPU 1 tick tiap ~100 ms supaya IDLE-watchdog core itu
//  & WiFi tidak kelaparan.
//
//  Bisa juga dibuild di PC (g++ -pthread) untuk uji: lihat tests_chess/run_smp.sh.
// =====================================================================
#ifdef CHS_SMP
#undef CHS_SMP
#endif
#define CHS_SMP 1
#define CHS_SMP_HELPER 1
#define CHS_NN_NONET 1                 // salinan pembantu tak memuat bobot/kode jaringan NN (SMP otomatis mati bila NN utama aktif)

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  #define CHS_SMP_ESP 1
  #include <Arduino.h>
  #include <esp_heap_caps.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #define CHS_AI_NOW() millis()
  #define CHS_AI_RAND() (0u)           // jalur pembantu tak memakai acak
#else
  #define CHS_SMP_ESP 0
  #include <stdint.h>
  #include <string.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <math.h>
  #include <time.h>
  #include <thread>
  #include <mutex>
  #include <condition_variable>
  static uint32_t smpHostMs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
  #define CHS_AI_NOW() smpHostMs()
  #define CHS_AI_RAND() (0u)
  #define CHS_AI_ALLOC(n) malloc(n)
#endif

// ---- kata kendali bersama (ditulis core utama, dibaca pembantu, dan sebaliknya) ----
volatile int chsSmpMainDepthV = 0;               // kedalaman iterasi yg sedang dicari pemain utama
static volatile uint8_t  chsSmpStopV = 0;        // 1 = pembantu harus berhenti
static volatile uint32_t chsSmpNodesV = 0;       // node pembantu pada job terakhir
static volatile uint8_t  smpBusy = 0;            // 1 = ada job berjalan

static uint32_t smpLastYield = 0;
static inline bool smpStopOrYield() {            // dipanggil mesin tiap 256 node
  if (chsSmpStopV) return true;
#if CHS_SMP_ESP
  uint32_t t = millis();
  if ((uint32_t)(t - smpLastYield) >= 100u) { smpLastYield = t; vTaskDelay(1); }     // NYX-SPEED: tiap 100 ms (dulu 25 ms = ~4% waktu pembantu terbuang); IDLE-watchdog baru lapar setelah 5 dtk
#endif
  return false;
}
#define CHS_AI_EXTRA_STOP() smpStopOrYield()

#include "chess_engine.inc"

// ---- job ----
struct SmpJob { int8_t bd[64]; int white, castle, ep, half, age, trust; uint32_t deadline; int maxDepth; void* tt; uint32_t mask; };
static SmpJob smpJob;

static void smpRunJob() {                        // berjalan di task pembantu
  memcpy(chsBd, smpJob.bd, 64);
  chsWhite = smpJob.white != 0; chsCastle = (uint8_t)smpJob.castle; chsEp = (int8_t)smpJob.ep; chsHalf = (uint16_t)smpJob.half;
  chsAiTT = (ChsTT*)smpJob.tt; chsAiTTMask = smpJob.mask;      // TT milik pemain utama (dipakai bersama)
  chsAiAge = (uint8_t)smpJob.age; chsXTrust = smpJob.trust;
  chsAiHelperRun(smpJob.maxDepth, smpJob.deadline);
  chsSmpNodesV = chsAiNodes;
}

#if CHS_SMP_ESP
// ----------------------------------------------------------------- backend FreeRTOS
#define CHS_SMP_STACK 10240
static TaskHandle_t      smpTask = nullptr;
static StaticTask_t      smpTCB;
static StackType_t*      smpStack = nullptr;
static SemaphoreHandle_t smpDone = nullptr;

static void smpLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    smpRunJob();
    smpBusy = 0;
    xSemaphoreGive(smpDone);
  }
}
static bool smpEnsureTask() {
  if (smpTask) return true;
#if CONFIG_FREERTOS_UNICORE
  return false;
#else
  if (!smpDone) smpDone = xSemaphoreCreateBinary();
  if (!smpDone) return false;
  if (!smpStack) smpStack = (StackType_t*)heap_caps_aligned_alloc(16, CHS_SMP_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!smpStack) return false;                   // sengaja TIDAK jatuh ke RAM internal (RAM internal sangat terbatas)
  int core = 1 - (int)xPortGetCoreID();          // core lain dari pemanggil (loop task)
  smpTask = xTaskCreateStaticPinnedToCore(smpLoop, "nyxHelper", CHS_SMP_STACK, nullptr, 1, smpStack, &smpTCB, core);
  return smpTask != nullptr;
#endif
}
static void smpWake()  { xSemaphoreTake(smpDone, 0); xTaskNotifyGive(smpTask); }   // kuras token lama (jika End() pernah time-out) sebelum job baru
static bool smpWait(uint32_t ms) { return xSemaphoreTake(smpDone, pdMS_TO_TICKS(ms)) == pdTRUE; }
#else
// ----------------------------------------------------------------- backend PC (uji)
static std::thread*            smpThr = nullptr;
static std::mutex              smpMx;
static std::condition_variable smpCv;
static bool                    smpGo = false, smpFin = false, smpQuit = false;
static void smpLoop() {
  for (;;) {
    { std::unique_lock<std::mutex> lk(smpMx); smpCv.wait(lk, [] { return smpGo || smpQuit; }); if (smpQuit) return; smpGo = false; }
    smpRunJob();
    smpBusy = 0;
    { std::lock_guard<std::mutex> lk(smpMx); smpFin = true; }
    smpCv.notify_all();
  }
}
static bool smpEnsureTask() { if (!smpThr) smpThr = new std::thread(smpLoop); return true; }
static void smpWake() { { std::lock_guard<std::mutex> lk(smpMx); smpGo = true; smpFin = false; } smpCv.notify_all(); }
static bool smpWait(uint32_t ms) {
  std::unique_lock<std::mutex> lk(smpMx);
  bool ok = smpCv.wait_for(lk, std::chrono::milliseconds(ms), [] { return smpFin; });
  if (ok) smpFin = false;
  return ok;
}
void chsSmpPcQuit() { if (smpThr) { { std::lock_guard<std::mutex> lk(smpMx); smpQuit = true; } smpCv.notify_all(); smpThr->join(); delete smpThr; smpThr = nullptr; } }
#endif

// ----------------------------------------------------------------- API (tipe polos di batas antar-TU)
bool chsSmpBegin(const int8_t* bd, int white, int castle, int ep, int half, int age, int trust, uint32_t deadline, int maxDepth, void* tt, uint32_t ttMask) {
  if (smpBusy || !tt) return false;              // job lama belum selesai (tak seharusnya) -> jalan 1 core saja
  if (!smpEnsureTask()) return false;
  memcpy(smpJob.bd, bd, 64);
  smpJob.white = white; smpJob.castle = castle; smpJob.ep = ep; smpJob.half = half; smpJob.age = age; smpJob.trust = trust;
  smpJob.deadline = deadline; smpJob.maxDepth = maxDepth; smpJob.tt = tt; smpJob.mask = ttMask;
  chsSmpStopV = 0; chsSmpNodesV = 0; chsSmpMainDepthV = 0;
  smpBusy = 1;
  smpWake();
  return true;
}
void chsSmpEnd() {
  if (!smpBusy) return;
  chsSmpStopV = 1;
  if (smpWait(1000)) smpBusy = 0;                // pembantu berhenti dlm hitungan ms (cek tiap 256 node); 1 dtk = batas pengaman
}
uint32_t chsSmpNodes() { return chsSmpNodesV; }
void chsSmpShutdown() {                          // dipanggil chsAiFree() milik mesin utama SEBELUM TT dibebaskan
  chsSmpEnd();
  if (smpBusy) return;                           // pembantu macet: jangan sentuh memori apa pun
  chsAiTT = nullptr; chsAiTTMask = 0;            // TT milik pemain utama: jangan dibebaskan di sini
  if (chsAiMem) chsAiFree();                     // bebaskan memori kerja pembantu
}
