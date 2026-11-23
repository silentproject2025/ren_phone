#!/usr/bin/env python3
"""SPK_FFAT_V1 -- (1) musik bisa diputar dari FFat (flash internal), (2) pengukur RAM internal pemutar
speaker (Serial + chip di layar Musik), (3) Telepon tidak lagi bergantung pada TWS.

 1. FFat: musfs.h baru = worker kecil ber-stack RAM internal untuk semua akses FFat (task musik ber-stack
    PSRAM dilarang menyentuh flash). SD tetap diutamakan; FFat dipakai kalau SD tak ada / tak ada MP3.
    Putar dari FFat hanya lewat Output SPK (jalur BT membaca file dari musTask, belum disentuh).
 2. RAM: log [SPK-RAM] di Serial (pra / decoder / i2s / main-2d / stop) dan chip kanan-atas layar Musik
    saat Output=SPK menampilkan "SPK 74K/52K" = RAM internal bebas / blok terbesar.
 3. Telepon: kalau TWS tidak tersambung, suara masuk otomatis lewat speaker MAX98357A (macro
    CALL_SPK_FALLBACK, 1 = aktif). TWS tersambung tetap dipakai seperti biasa, kecuali Output=SPK.

Idempoten. Atomik: kalau ada satu anchor kode yg tidak cocok, TIDAK ADA file yg diubah.
Jalankan dari ROOT repo:  python3 tools/patches/apply_spk_ffat_patch.py"""
import sys, os

MUSFS_H = r'''#pragma once
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
'''

D = "firmware/phone/"
if not os.path.exists(D + "musicbt_renphone.ino"):
    sys.exit("Jalankan dari root repo (folder berisi firmware/phone/musicbt_renphone.ino)")

def rd(p):
    return open(p, encoding="utf-8").read()

spk = rd(D + "musspk.h")
mus = rd(D + "musicbt_renphone.ino")
call = rd(D + "call_renphone.ino")
if "SPK_FFAT_V1" in mus:
    print("SKIP (sudah terpasang)"); sys.exit(0)

def rep(s, old, new, name):
    if s.count(old) != 1:
        sys.exit("GAGAL [%s]: anchor tidak cocok persis 1x:\n  %s" % (name, old[:100].replace("\n", "\n  ")))
    return s.replace(old, new)

# ======================= musspk.h =======================
spk = rep(spk, '#include <esp_heap_caps.h>\n#include "MP3DecoderHelix.h"',
          '#include <esp_heap_caps.h>\n#include "musfs.h"      // SPK_FFAT_V1: worker FFat + musRamLog\n#include "MP3DecoderHelix.h"', "spk include")
spk = rep(spk, 'static volatile uint32_t spkCmdOff    = 0;\n',
          'static volatile uint32_t spkCmdOff    = 0;\nstatic volatile bool     spkCmdFfat   = false;   // SPK_FFAT_V1: sesi ini membaca dari FFat\n', "spk cmdffat")
spk = rep(spk, '  spkI2SOpen = true; spkI2SRate = rate;\n  return true;\n}',
          '  spkI2SOpen = true; spkI2SRate = rate;\n  musRamLog("i2s");\n  return true;\n}', "spk i2s log")
spk = rep(spk, '  spkDec->begin();\n#if MUS_SPK_DEC_PSRAM\n  heap_caps_malloc_extmem_enable(MUS_SPK_HEAP_THRESHOLD);   // kembalikan perilaku malloc normal\n#endif\n  return spkDec != nullptr;',
          '  spkDec->begin();\n#if MUS_SPK_DEC_PSRAM\n  heap_caps_malloc_extmem_enable(MUS_SPK_HEAP_THRESHOLD);   // kembalikan perilaku malloc normal\n#endif\n  musRamLog("decoder");\n  return spkDec != nullptr;', "spk decoder log")

i0 = spk.find("static void spkTaskFn(void* arg) {")
i1 = spk.find("// =====================================================================\n//  API untuk musTask")
if i0 < 0 or i1 < 0 or i1 < i0 or spk.count("static void spkTaskFn(void* arg) {") != 1:
    sys.exit("GAGAL [spkTaskFn]: batas fungsi tidak ditemukan")

NEW_TASK = r'''// ---- SPK_FFAT_V1: akses file lagu. SD langsung; FFat lewat worker ber-stack internal (musfs.h) ----
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

'''
spk = spk[:i0] + NEW_TASK + spk[i1:]
spk = rep(spk, '  spkCmdWav = wav; spkCmdOff = off;\n',
          '  spkCmdWav = wav; spkCmdOff = off;\n  spkCmdFfat = (!wav && musUseFfat);          // memo WAV selalu dari SD; lagu ikut hasil pindai\n', "spkPlay ffat")

# ======================= musicbt_renphone.ino =======================
mus = rep(mus, '  musMtx = xSemaphoreCreateMutex();\n  musQ = xQueueCreate(8, sizeof(MusReq));\n',
          '  musMtx = xSemaphoreCreateMutex();\n  musFsInit();                                 // SPK_FFAT_V1: semaphore worker FFat (musfs.h)\n  musQ = xQueueCreate(8, sizeof(MusReq));\n', "musBegin init")

# --- pindai playlist: SD dulu, FFat kalau SD tak ada / kosong
s0 = mus.find("static void musScanPlaylist() {")
s1 = mus.find("// header MP3: cari frame pertama utk sample rate & bitrate")
if s0 < 0 or s1 < s0 or mus.count("static void musScanPlaylist() {") != 1:
    sys.exit("GAGAL [musScanPlaylist]: batas fungsi tidak ditemukan")
NEW_SCAN = r'''// SPK_FFAT_V1: pindai satu filesystem -> daftar .mp3 (/music dulu, kalau tak ada pakai root). Untuk FFat
// WAJIB dipanggil dari worker (musScanFfFn); task musik ber-stack PSRAM dilarang menyentuh flash.
static void musScanDirOn(fs::FS& fsx, std::vector<String>& tmp) {
  String base = MUS_DIR;
  File root = fsx.open(MUS_DIR);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    root = fsx.open("/");
    base = "";
  }
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f && (int)tmp.size() < MUS_MAX_TRACKS) {
      if (!f.isDirectory()) {
        String nm = String(f.name());
        String low = nm; low.toLowerCase();
        int sl = nm.lastIndexOf('/');
        String bn = (sl >= 0) ? nm.substring(sl + 1) : nm;
        if (low.endsWith(".mp3") && !bn.startsWith(".")) {
          if (nm.startsWith("/")) tmp.push_back(nm);
          else                    tmp.push_back(base + "/" + nm);
        }
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
  }
}
static void musScanFfFn(void* p) { musScanDirOn(FFat, *(std::vector<String>*)p); }

static void musScanPlaylist() {
  std::vector<String> tmp;
  bool ff = false;
  musScanDirOn(SD_MMC, tmp);                         // SD diutamakan
  if (tmp.empty() && ffatReady) {                    // SD tak ada / tak ada MP3 -> coba flash internal
    std::vector<String> t2;
    if (mffRun(musScanFfFn, &t2) && !t2.empty()) { tmp.swap(t2); ff = true; }
  }
  musUseFfat = ff;
  std::sort(tmp.begin(), tmp.end(), [](const String& a, const String& b) { return a.compareTo(b) < 0; });
  uint32_t keys[MUS_MAX_TRACKS];
  int nk = (int)tmp.size(); if (nk > MUS_MAX_TRACKS) nk = MUS_MAX_TRACKS;
  for (int i = 0; i < nk; i++) keys[i] = musKey(tmp[i]);
  if (xSemaphoreTake(musMtx, portMAX_DELAY) == pdTRUE) {
    musList.swap(tmp);
    xSemaphoreGive(musMtx);
  }
  int n = (int)musList.size();
  if (musCur >= n) musCur = 0;
  if (musCur < 0) musCur = 0;
  musListTop = 0;
  musRyneAttach(keys, n);
}

'''
mus = mus[:s0] + NEW_SCAN + mus[s1:]

# --- baca dari musFile: FFat lewat buffer pantul internal
mus = rep(mus, '// header MP3: cari frame pertama utk sample rate & bitrate',
r'''// SPK_FFAT_V1: baca n byte ke dst (PSRAM boleh). FFat (hanya di worker) lewat buffer pantul internal.
static size_t musFReadF(File& f, uint8_t* dst, size_t n) {
  if (musUseFfat && mffBuf) return mffReadI(f, dst, n);
  return (size_t)f.read(dst, n);
}
// header MP3: cari frame pertama utk sample rate & bitrate''', "musFRead")
mus = rep(mus, '  musFile.seek(o);\n  int n = musFile.read(musBlk, 4096);\n', '  musFile.seek(o);\n  int n = (int)musFReadF(musFile, musBlk, 4096);\n', "parse read")

# --- buka + parse file FFat di worker
mus = rep(mus, '// =====================================================================\n//  COVER ART (ID3v2 APIC',
r'''// SPK_FFAT_V1: buka + parse header MP3 + ukuran file untuk lagu di FFat (jalan di worker), lalu tutup lagi.
struct MusFfInfo { const char* path; uint32_t sr, kbps, off, size; int ok; };
static void musFfInfoFn(void* p) {
  MusFfInfo* o = (MusFfInfo*)p;
  o->ok = -1;
  File f = FFat.open(o->path, FILE_READ);
  if (!f) return;
  o->size = (uint32_t)f.size();
  musFile = f;
  o->ok = musParseMp3(&o->sr, &o->kbps, &o->off) ? 1 : 0;
  musFile.close();
}

// =====================================================================
//  COVER ART (ID3v2 APIC''', "ffinfo")

mus = rep(mus, '      if ((uint32_t)f.read(buf, fs) != fs) { free(buf); return nullptr; }',
          '      if ((uint32_t)musFReadF(f, buf, fs) != fs) { free(buf); return nullptr; }', "apic read")
mus = rep(mus, 'static int musCoverDraw(JPEGDRAW* d) {',
r'''// SPK_FFAT_V1: baca gambar cover dari SD, atau dari FFat lewat worker.
struct MusFfApic { const char* path; uint8_t* img; uint32_t len; };
static void musFfApicFn(void* p) {
  MusFfApic* o = (MusFfApic*)p;
  File f = FFat.open(o->path, FILE_READ);
  if (!f) return;
  o->img = musReadApic(f, &o->len);
  f.close();
}
static uint8_t* musReadApicPath(const char* path, uint32_t* outLen) {
  *outLen = 0;
  if (musUseFfat) {
    MusFfApic o; o.path = path; o.img = nullptr; o.len = 0;
    if (!mffRun(musFfApicFn, &o)) return nullptr;
    *outLen = o.len;
    return o.img;
  }
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  uint8_t* img = musReadApic(f, outLen);
  f.close();
  return img;
}

static int musCoverDraw(JPEGDRAW* d) {''', "apic path")
mus = rep(mus, '''  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint32_t len = 0;
  uint8_t* img = musReadApic(f, &len);
  f.close();
  if (!img) return false;
''', '''  uint32_t len = 0;
  uint8_t* img = musReadApicPath(path, &len);
  if (!img) return false;
''', "cover load")

# --- musStartTrack
mus = rep(mus, '''  if (musFile) musFile.close();
  musFile = SD_MMC.open(musList[idx].c_str(), FILE_READ);
  if (!musFile) { musToast("Gagal membuka file"); return false; }

  uint32_t sr = 0, kbps = 0, off = 0;
  if (!musParseMp3(&sr, &kbps, &off)) {
    musFile.close();
    musToast("Bukan MP3 Layer III");
    return false;
  }
  uint64_t body = (musFile.size() > off) ? (musFile.size() - off) : musFile.size();
''', '''  if (musFile) musFile.close();
  uint32_t sr = 0, kbps = 0, off = 0, fsz = 0;
  if (musUseFfat) {                             // SPK_FFAT_V1: lagu di flash internal
    if (!musOutSpk) { musToast("Lagu di FFat: pilih Output SPK"); return false; }
    MusFfInfo inf; inf.path = musList[idx].c_str(); inf.sr = inf.kbps = inf.off = inf.size = 0; inf.ok = -1;
    if (!mffRun(musFfInfoFn, &inf)) { musToast("FFat tidak merespons"); return false; }
    if (inf.ok < 0) { musToast("Gagal membuka file"); return false; }
    if (inf.ok == 0) { musToast("Bukan MP3 Layer III"); return false; }
    sr = inf.sr; kbps = inf.kbps; off = inf.off; fsz = inf.size;
  } else {
    musFile = SD_MMC.open(musList[idx].c_str(), FILE_READ);
    if (!musFile) { musToast("Gagal membuka file"); return false; }
    if (!musParseMp3(&sr, &kbps, &off)) {
      musFile.close();
      musToast("Bukan MP3 Layer III");
      return false;
    }
    fsz = (uint32_t)musFile.size();
  }
  uint64_t body = (fsz > off) ? (fsz - off) : fsz;
''', "startTrack open")
mus = rep(mus, '  if (off > 0 && off < (uint32_t)musFile.size()) musFile.seek(off); else musFile.seek(0);\n',
          '  if (!musUseFfat) { if (off > 0 && off < (uint32_t)musFile.size()) musFile.seek(off); else musFile.seek(0); }\n', "startTrack seek")

# --- chip RAM di layar Musik
mus = rep(mus, '''  const char* st; uint16_t col;
  if (musOutSpk)           { st = "Speaker";           col = T().good; }
''', '''  const char* st; uint16_t col;
  char spkTxt[24];
  if (musOutSpk) {   // SPK_FFAT_V1: RAM internal bebas / blok terbesar (KB) -- bandingkan saat idle vs lagu main
    snprintf(spkTxt, sizeof(spkTxt), "SPK%s %uK/%uK", musUseFfat ? "-F" : "",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    st = spkTxt; col = T().good;
  }
''', "chip ram")

# --- Telepon tidak bergantung TWS
mus = rep(mus, 'bool callSpkSelected()                     { return musOutSpk; }',
r'''// SPK_FFAT_V1: telepon memakai speaker kalau Output=SPK ATAU TWS tidak tersambung (tidak lagi bergantung pada TWS).
// TWS tersambung + Output=BT -> tetap lewat TWS. Tanpa modul MAX98357A terpasang, set -DCALL_SPK_FALLBACK=0.
#ifndef CALL_SPK_FALLBACK
#define CALL_SPK_FALLBACK 1
#endif
bool callSpkSelected()                     { return musOutSpk || (CALL_SPK_FALLBACK && !(musCamAlive && musBtConn)); }''', "callSpkSelected")
mus = mus.replace("static File          musFile;", "static File          musFile;   // SPK_FFAT_V1 (penanda patch)", 1) if "SPK_FFAT_V1" not in mus else mus

# ======================= call_renphone.ino (komentar) =======================
call = rep(call, '''//   => TELEPON BUTUH TWS TERSAMBUNG (Musik > cari TWS) kecuali output = SPK. Tanpa keduanya tidak ada
//   suara masuk (mic tetap jalan).''', '''//   SPK_FFAT_V1: speaker dipakai kalau output Musik = SPK ATAU TWS tidak tersambung -> telepon TIDAK lagi
//   bergantung pada TWS (lihat callSpkSelected() di musicbt_renphone.ino, macro CALL_SPK_FALLBACK).
//   TWS tersambung + output = BT tetap lewat TWS. Tanpa modul MAX98357A & tanpa TWS tidak ada suara masuk
//   (mic tetap jalan).''', "call comment")

# ======================= tulis semuanya (setelah semua anchor cocok) =======================
open(D + "musfs.h", "w", encoding="utf-8").write(MUSFS_H)
open(D + "musspk.h", "w", encoding="utf-8").write(spk)
open(D + "musicbt_renphone.ino", "w", encoding="utf-8").write(mus)
open(D + "call_renphone.ino", "w", encoding="utf-8").write(call)
print("OK firmware/phone: musfs.h (baru), musspk.h, musicbt_renphone.ino, call_renphone.ino")

# ======================= dokumen (opsional; lewati kalau anchor beda) =======================
def doc(path, pairs):
    if not os.path.exists(path): return
    t = rd(path); ch = False
    for old, new in pairs:
        if t.count(old) == 1: t = t.replace(old, new); ch = True
        else: print("  (docs) anchor tidak cocok, dilewati:", path, "|", old[:50])
    if ch: open(path, "w", encoding="utf-8").write(t); print("OK", path)

doc("CHANGELOG.md", [("## Unreleased\n", "## Unreleased\n"
    "- New: the Music app can play MP3 files from internal flash (FFat) when there is no SD card or no MP3 on it (Output: SPK only). FFat is read by a small helper task with an internal-RAM stack, because tasks with a PSRAM stack must not touch flash\n"
    "- New: while Output is SPK, the chip at the top right of the Music screen shows free internal RAM / largest block (KB), and the Serial log prints `[SPK-RAM]` lines (pra, decoder, i2s, main-2d, stop)\n"
    "- Changed: phone calls no longer depend on TWS earbuds. When no TWS is connected the call audio goes to the MAX98357A speaker (build flag CALL_SPK_FALLBACK=0 turns this off)\n")])
doc("docs/features/phone-calls.md", [
    ("Without connected earbuds the microphone still transmits, but you will not hear anything.",
     "If no earbuds are connected the call audio goes to the MAX98357A speaker instead (see below); with neither, the microphone still transmits but you will not hear anything."),
    ("1. Connect the TWS earbuds first, through the **Music** app (or switch the output to **SPK** if you have the speaker module).",
     "1. If you want the call in earbuds, connect them first through the **Music** app. Without connected earbuds (or with the output set to **SPK**) the call uses the speaker module.")])
doc("docs/features/media.md", [
    ("The decoder, its buffers and the task stack are placed in PSRAM and are freed when playback stops.",
     "The decoder, its buffers and the task stack are placed in PSRAM and are freed when playback stops. With no SD card (or no MP3 on it) the app lists and plays MP3 files from internal FFat instead (SPK output only; the chip shows `SPK-F`). FFat is read through a helper task with an internal-RAM stack, which costs roughly 12 KB of internal RAM only while a FFat track plays. While Output is SPK, the chip at the top right shows free internal RAM / largest block in KB and the Serial log prints `[SPK-RAM]` lines.")])
print("Selesai. Build ulang firmware; lalu: Musik > Output: SPK > putar lagu dan baca chip kanan-atas / Serial.")
