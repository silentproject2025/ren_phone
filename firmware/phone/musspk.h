#pragma once
// =====================================================================
//  musspk.h -- pemutar MP3/WAV LOKAL: SD -> decoder Helix -> I2S -> MAX98357A
//
//  Dipakai app Musik (musicbt_renphone.ino) saat output = "SPK". Jalur
//  Bluetooth lewat ESP32-CAM TIDAK berubah; ini hanya jalur tambahan.
//
//  HEMAT RAM INTERNAL:
//   - Task punya stack di PSRAM; buffer baca SD & buffer PCM di PSRAM.
//   - Decoder Helix (state ~20+ KB, biasanya malloc kecil -> RAM internal)
//     dialokasikan sementara dgn heap_caps_malloc_extmem_enable() supaya
//     masuk PSRAM (matikan lewat -DMUS_SPK_DEC_PSRAM=0 kalau tidak mau).
//   - Decoder + I2S dibuat HANYA saat lagu main dan dibuang saat berhenti,
//     jadi saat idle tidak menahan RAM sama sekali.
//
//  Library: arduino-libhelix (pschatzmann) -- decoder yg sama dgn yg
//  dipakai firmware CAM. BELUM PERNAH di-compile/dites di board.
//
//  JANGAN pakai tipe buatan sendiri di signature fungsi (prototipe otomatis
//  arduino-cli disisipkan sebelum tipe dikenal) -- cuma tipe dasar di sini.
// =====================================================================
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include "musfs.h"      // SPK_FFAT_V1: worker FFat + musRamLog
#include "MP3DecoderHelix.h"
using namespace libhelix;

#ifndef MUS_SPK_DEC_PSRAM
#define MUS_SPK_DEC_PSRAM 1
#endif
// Ambang bawaan Arduino-ESP32 (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL). Dipulihkan setelah decoder dibuat.
// MUSIK_HEAP_FIX_V1: pulihkan ke nilai SDK yg sebenarnya (preset hemat RAM = 2048), bukan 16384 tetap.
#ifndef MUS_SPK_HEAP_THRESHOLD
 #ifdef CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
  #define MUS_SPK_HEAP_THRESHOLD CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
 #else
  #define MUS_SPK_HEAP_THRESHOLD 2048
 #endif
#endif
#define SPK_IN_CHUNK    2048          // byte file dibaca per putaran
#define SPK_OUT_FRAMES  1152          // frame stereo maks per tulisan I2S (1 frame MP3)

static I2SClass          spkI2S;
static bool              spkI2SOpen   = false;
static uint32_t          spkI2SRate   = 0;

// ---- perintah dari musTask -> spkTask (task = satu-satunya pemilik File/decoder/I2S) ----
static volatile uint32_t spkCmdSeq    = 0;
static char              spkCmdPath[128];
static volatile bool     spkCmdWav    = false;
static volatile uint32_t spkCmdOff    = 0;
static volatile bool     spkCmdFfat   = false;   // SPK_FFAT_V1: sesi ini membaca dari FFat

// ---- status ----
static volatile bool     spkPausedF   = false;
static volatile bool     spkEndedF    = false;
static volatile bool     spkFailF     = false;
static volatile uint32_t spkFrames    = 0;      // frame yg sudah ditulis ke I2S (untuk waktu putar)
static volatile uint32_t spkRate      = 0;
static volatile int      spkGainQ     = 160;    // 0..256 (256 = tanpa pelemahan)
static volatile uint32_t spkSeenSeq   = 0;      // perintah terakhir yg sudah diproses spkTask
static volatile bool     spkIdleF     = true;   // true = spkTask tidak sedang memutar & I2S sudah ditutup
static volatile bool     spkLive      = false;  // true = telepon memakai I2S (mode "live"); musik tidak boleh jalan
static volatile bool     spkPreemptF  = false;  // musik dihentikan krn telepon

static TaskHandle_t      spkTaskH     = nullptr;
static StackType_t*      spkStack     = nullptr;
static StaticTask_t      spkTCB;
static int16_t*          spkOut       = nullptr;   // PSRAM: SPK_OUT_FRAMES * 2 sampel
static MP3DecoderHelix*  spkDec       = nullptr;

// ---------------------------------------------------------------------
static bool spkEnsureBuf() {
  if (!spkOut) spkOut = (int16_t*)heap_caps_malloc(SPK_OUT_FRAMES * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return spkOut != nullptr;
}
static bool spkOpenI2S(uint32_t rate) {
  if (spkI2SOpen && spkI2SRate == rate) return true;
  if (spkI2SOpen) { spkI2S.end(); spkI2SOpen = false; }
  spkI2S.setPins(AUDIO_I2S_BCLK, AUDIO_I2S_LRC, AUDIO_I2S_DOUT, -1, -1);
  if (!spkI2S.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return false;
  spkI2S.setTimeout(1000);                         // tulisan musik boleh menunggu DMA (jadi pengatur tempo)
  spkI2SOpen = true; spkI2SRate = rate;
  musRamLog("i2s");
  return true;
}
static void spkCloseI2S() {
  if (!spkI2SOpen) return;
  spkI2S.end(); spkI2SOpen = false; spkI2SRate = 0;
}
// Tulis keheningan: DMA I2S mengulang buffer terakhir kalau kehabisan data -> bisa berdengung saat pause/stop.
static void spkSilence() {
  if (!spkI2SOpen || !spkOut) return;
  memset(spkOut, 0, SPK_OUT_FRAMES * 4);
  for (int i = 0; i < 2; i++) spkI2S.write((uint8_t*)spkOut, SPK_OUT_FRAMES * 4);
}

// PCM (mono/stereo, 16-bit) -> stereo + volume -> I2S. Blocking sampai DMA punya ruang (itu pengatur tempo).
static void spkWritePcm(const int16_t* in, size_t frames, int ch, uint32_t sr) {
  if (!spkEnsureBuf() || !spkOpenI2S(sr)) return;
  spkRate = sr;
  const int g = spkGainQ;
  size_t done = 0;
  while (done < frames) {
    size_t n = frames - done; if (n > SPK_OUT_FRAMES) n = SPK_OUT_FRAMES;
    for (size_t i = 0; i < n; i++) {
      int32_t l, r;
      if (ch == 1) { l = r = in[done + i]; }
      else         { l = in[2 * (done + i)]; r = in[2 * (done + i) + 1]; }
      spkOut[2 * i]     = (int16_t)((l * g) >> 8);
      spkOut[2 * i + 1] = (int16_t)((r * g) >> 8);
    }
    spkI2S.write((uint8_t*)spkOut, n * 4);
    done += n;
    spkFrames += (uint32_t)n;
  }
}

// dipanggil Helix tiap satu frame MP3 selesai di-decode. len = jumlah sampel (semua kanal).
static void spkPcmCb(MP3FrameInfo& info, short* pcm, size_t len, void* ref) {
  int ch = info.nChans; if (ch < 1 || ch > 2) ch = 2;
  spkWritePcm((const int16_t*)pcm, len / (size_t)ch, ch, (uint32_t)info.samprate);
}

static bool spkDecCreate() {
  if (spkDec) return true;
#if MUS_SPK_DEC_PSRAM
  heap_caps_malloc_extmem_enable(64);                  // alokasi >= 64 byte -> PSRAM selama decoder dibangun
#endif
  spkDec = new MP3DecoderHelix();
  spkDec->setDataCallback(spkPcmCb);
  spkDec->begin();
#if MUS_SPK_DEC_PSRAM
  heap_caps_malloc_extmem_enable(MUS_SPK_HEAP_THRESHOLD);   // kembalikan perilaku malloc normal
#endif
  musRamLog("decoder");
  return spkDec != nullptr;
}
static void spkDecDestroy() {
  if (!spkDec) return;
  spkDec->end();
  delete spkDec;
  spkDec = nullptr;
}

// ---- SPK_FFAT_V1: akses file lagu. SD langsung; FFat lewat worker ber-stack internal (musfs.h) ----
static File spkF;                                  // file SD (spkTask pemilik tunggal)
struct SpkIo { int op; const char* path; uint8_t* dst; size_t n; uint32_t pos; long ret; };
static void spkIoFn(void* p) {                     // jalan di worker
  SpkIo* o = (SpkIo*)p;
  o->ret = 0;
  switch (o->op) {
    case 0:  mffPlayF = FFat.open(o->path, FILE_READ); o->ret = mffPlayF ? 1 : 0; break;
    case 1:  o->ret = mffPlayF ? (long)mffReadI(mffPlayF, o->dst, o->n) : -1; break;
    case 2:  o->ret = (mffPlayF && mffPlayF.seek(o->pos)) ? 1 : 0; break;
    default: if (mffPlayF) mffPlayF.close(); o->ret = 1; break;
  }
}
static long spkIo(int op, const char* path, uint8_t* dst, size_t n, uint32_t pos) {
  SpkIo o; o.op = op; o.path = path; o.dst = dst; o.n = n; o.pos = pos; o.ret = -1;
  if (!mffRun(spkIoFn, &o)) return -1;
  return o.ret;
}
static bool spkOpenFile(const char* p, bool ffat) {
  if (ffat) return spkIo(0, p, nullptr, 0, 0) == 1;
  spkF = SD_MMC.open(p, FILE_READ);
  return (bool)spkF;
}
static int spkReadFile(bool ffat, uint8_t* b, size_t n) {
  if (ffat) return (int)spkIo(1, nullptr, b, n, 0);
  return spkF.read(b, n);
}
static void spkSeekFile(bool ffat, uint32_t pos) {
  if (ffat) spkIo(2, nullptr, nullptr, 0, pos); else spkF.seek(pos);
}
static void spkCloseFile(bool ffat) {
  if (ffat) spkIo(3, nullptr, nullptr, 0, 0); else if (spkF) spkF.close();
}

static void spkTaskFn(void* arg) {
  uint8_t* buf = (uint8_t*)heap_caps_malloc(SPK_IN_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf || !spkEnsureBuf()) {
    if (buf) free(buf);
    spkFailF = true; spkTaskH = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  uint32_t seen = 0;
  bool fileOpen = false, ffat = false;
  bool playing = false, wav = false, pausedHandled = false, ramLogged = false;
  uint32_t wavSr = 16000; int wavCh = 1;

  for (;;) {
    if (spkCmdSeq != seen) {                         // perintah baru: bongkar sesi lama, mulai sesi baru
      seen = spkCmdSeq;
      if (fileOpen) { spkCloseFile(ffat); fileOpen = false; }
      spkDecDestroy();
      if (spkI2SOpen && !spkLive) { spkSilence(); spkCloseI2S(); }   // mode live: I2S milik telepon, jangan ditutup di sini
      playing = false; pausedHandled = false; ramLogged = false;
      if (spkCmdPath[0]) musRamLog("pra");
      if (spkCmdPath[0] && !spkLive) {
        ffat = spkCmdFfat;
        bool ok = spkOpenFile(spkCmdPath, ffat);
        fileOpen = ok;
        wav = spkCmdWav;
        if (ok && wav) {                             // memo WAV (selalu dari SD): header 44 byte, PCM 16-bit
          uint8_t h[44];
          ok = (spkF.read(h, 44) == 44) && memcmp(h, "RIFF", 4) == 0 && h[34] == 16;
          wavCh = h[22];
          wavSr = (uint32_t)h[24] | ((uint32_t)h[25] << 8) | ((uint32_t)h[26] << 16) | ((uint32_t)h[27] << 24);
          if (wavCh < 1 || wavCh > 2 || wavSr < 8000 || wavSr > 96000) ok = false;
        } else if (ok) {
          if (spkCmdOff) spkSeekFile(ffat, spkCmdOff);   // lewati tag ID3
          ok = spkDecCreate();
        }
        if (ok) playing = true;
        else { if (fileOpen) { spkCloseFile(ffat); fileOpen = false; } spkDecDestroy(); spkFailF = true; }
      }
      spkIdleF = !playing;
      spkSeenSeq = seen;
    }
    if (!playing) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }

    if (spkPausedF) {
      if (!pausedHandled) { spkSilence(); pausedHandled = true; }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    pausedHandled = false;

    if (!ramLogged && spkRate && spkFrames > spkRate * 2u) { ramLogged = true; musRamLog("main-2d"); }

    int n = spkReadFile(ffat, buf, SPK_IN_CHUNK);
    if (n <= 0) {                                    // file habis (atau FFat macet kalau n < 0)
      spkSilence();
      if (fileOpen) { spkCloseFile(ffat); fileOpen = false; }
      spkDecDestroy();
      spkCloseI2S();
      playing = false;
      spkIdleF = true;
      if (n < 0) spkFailF = true;
      else if (spkCmdSeq == seen) spkEndedF = true;  // jangan tandai "habis" kalau sudah ada perintah baru
      musRamLog("stop");
      continue;
    }
    if (wav) spkWritePcm((const int16_t*)buf, (size_t)n / (2u * (size_t)wavCh), wavCh, wavSr);
    else     spkDec->write(buf, (size_t)n);
    vTaskDelay(1);                                   // kasih IDLE kebagian CPU (anti task-watchdog)
  }
}

// =====================================================================
//  API untuk musTask (tipe dasar saja)
// =====================================================================
static bool spkEnsureTask() {
  if (spkTaskH) return true;
  const uint32_t stackBytes = 8192;
  if (!spkStack)
    spkStack = (StackType_t*)heap_caps_aligned_alloc(16, stackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (spkStack)
    spkTaskH = xTaskCreateStaticPinnedToCore(spkTaskFn, "spkTask", stackBytes, NULL, 4, spkStack, &spkTCB, 0);
  else
    xTaskCreatePinnedToCore(spkTaskFn, "spkTask", stackBytes, NULL, 4, &spkTaskH, 0);
  return spkTaskH != nullptr;
}
static bool spkPlay(const char* path, bool wav, uint32_t off) {
  if (spkLive) return false;                         // telepon sedang memakai speaker
  if (!spkEnsureTask()) return false;
  strncpy(spkCmdPath, path, sizeof(spkCmdPath) - 1);
  spkCmdPath[sizeof(spkCmdPath) - 1] = 0;
  spkCmdWav = wav; spkCmdOff = off;
  spkCmdFfat = (!wav && musUseFfat);          // memo WAV selalu dari SD; lagu ikut hasil pindai
  spkPausedF = false; spkEndedF = false; spkFailF = false;
  spkFrames = 0;
  __sync_synchronize();
  spkCmdSeq++;
  return true;
}
static void spkStopPlay() {
  if (!spkTaskH) return;
  spkCmdPath[0] = 0;
  spkPausedF = false;
  __sync_synchronize();
  spkCmdSeq++;
}
static void spkSetPause(bool p) { spkPausedF = p; }
static void spkSetVolume(int v) {                    // v = 0..127 (skala UI), kurva kuadrat biar terasa linear di telinga
  if (v < 0) v = 0; if (v > 127) v = 127;
  spkGainQ = (v * v * 256) / (127 * 127);
}
static uint32_t spkPlayedMs() {
  uint32_t r = spkRate;
  return r ? (uint32_t)(((uint64_t)spkFrames * 1000ULL) / r) : 0;
}
static bool spkTakePreempt() { if (spkPreemptF) { spkPreemptF = false; return true; } return false; }
static bool spkTakeEnded() { if (spkEndedF) { spkEndedF = false; return true; } return false; }
static bool spkTakeFail()  { if (spkFailF)  { spkFailF  = false; return true; } return false; }

// =====================================================================
//  MODE LIVE (telepon): PCM mono 8 kHz didorong dari callTask -> I2S.
//  Tulisan pakai timeout pendek: kalau DMA penuh, sisa sampel DIBUANG
//  (menjaga latensi) dan callTask tidak pernah tertahan lama.
// =====================================================================
static bool spkLiveBegin(uint32_t sr) {
  if (!spkEnsureBuf()) return false;
  spkLive = true;
  if (spkTaskH) {                                    // hentikan musik yg sedang jalan & tunggu spkTask melepas I2S
    bool wasBusy = !spkIdleF;
    spkStopPlay();
    uint32_t t0 = millis();
    while (!(spkSeenSeq == spkCmdSeq && spkIdleF) && millis() - t0 < 400) vTaskDelay(pdMS_TO_TICKS(5));
    if (!(spkSeenSeq == spkCmdSeq && spkIdleF)) { spkLive = false; return false; }   // spkTask belum melepas I2S: jangan dipaksa
    if (wasBusy) spkPreemptF = true;
  }
  if (spkI2SOpen) { spkSilence(); spkCloseI2S(); }
  if (!spkOpenI2S(sr)) { spkLive = false; return false; }
  spkI2S.setTimeout(10);
  spkRate = sr;
  return true;
}
static void spkLiveWrite(const int16_t* pcm, int n) {
  if (!spkLive || !spkI2SOpen || !spkOut || n <= 0) return;
  const int g = spkGainQ;
  while (n > 0) {
    int c = n > SPK_OUT_FRAMES ? SPK_OUT_FRAMES : n;
    for (int i = 0; i < c; i++) {
      int16_t v = (int16_t)(((int32_t)pcm[i] * g) >> 8);
      spkOut[2 * i] = v; spkOut[2 * i + 1] = v;
    }
    spkI2S.write((uint8_t*)spkOut, (size_t)c * 4);   // tulisan parsial = sisa dibuang (timeout 10 ms)
    pcm += c; n -= c;
  }
}
static void spkLiveEnd() {
  if (!spkLive) return;
  if (spkI2SOpen) { spkSilence(); spkCloseI2S(); }
  spkLive = false;
}

// Jembatan untuk call_renphone.ino: file itu digabung SEBELUM musicbt_renphone.ino (urutan abjad), jadi ia
// tidak bisa melihat fungsi static di sini -- ia memakai deklarasi maju fungsi non-static di bawah ini.
