// =====================================================================
//  musicbt_renphone.ino -- app "Musik" (Ren Phone, ESP32-S3)
//
//  S3 = MASTER. ESP32-CAM (firmware cam_bt_coprocessor) = co-prosesor
//  Bluetooth A2DP. Alur: SD S3 -> UART 921600 -> CAM (decode MP3) -> TWS.
//
//  Kabel:  S3 GPIO41 (TX) -> CAM GPIO13 (RX)
//          S3 GPIO42 (RX) <- CAM GPIO14 (TX)
//          GND -- GND
//
//  Pola sama dgn app NES/DOOM: file .ino terpisah, patch kecil di phone.ino
//  (lihat apply_musik_patch.py). JANGAN pakai tipe buatan sendiri di
//  signature fungsi (prototype otomatis arduino-cli disisipkan sebelum
//  tipe di file ini dikenal).
//
//  Musik tetap jalan walau pindah app (task terpisah). Task pakai stack
//  PSRAM -> TIDAK BOLEH nulis NVS dari task; simpan prefs lewat
//  musicLoopPoll() (konteks loop utama).
//  BELUM PERNAH di-compile di environment (gak ada toolchain ESP32).
// =====================================================================
#include "rplink.h"
#include "ryne_engine.h"
#include <new>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <math.h>

#define MUS_UART_RX     42
#define MUS_UART_TX     41
#define MUS_DIR         "/music"
#define MUS_MAX_TRACKS  200
#define MUS_VOL_STEP    8
#define MUS_SCAN_MAX    12
#define MUS_CREDIT_MIN  (24*1024)   // sisa ruang CAM minimal sebelum kirim lagi (margin in-flight)

struct MusReq { uint8_t t; int32_t a; char s[40]; };

// ---- state task/link ----
static bool          musStarted = false;
static TaskHandle_t  musTaskHandle = nullptr;
static StackType_t*  musTaskStack = nullptr;
static StaticTask_t  musTaskTCB;
static QueueHandle_t musQ = nullptr;
static SemaphoreHandle_t musMtx = nullptr;
static uint8_t*      musBlk = nullptr;     // buffer baca SD 4KB (PSRAM)
static File          musFile;

// ---- dari CAM ----
static volatile bool     musCamAlive = false;
static volatile uint32_t musLastStatusMs = 0;
static volatile bool     musBtConn = false;
static volatile uint32_t musPlayedMs = 0;
static volatile bool     musEnded = false;
static volatile bool     musHello = false;
static volatile uint32_t musCredit = 0, musSentSince = 0;
static volatile uint32_t musRxBytes = 0, musRxFrames = 0, musRxErr = 0;   // diagnosa link CAM -> S3

// ---- milik S3 ----
static std::vector<String> musList;
static volatile int  musCur = 0;
static volatile bool musLoaded = false;      // ada lagu yg lagi di-stream/diputar
static volatile bool musPaused = false;
static volatile int  musVol = 80;
static volatile int  musRepeat = 0;          // 0 mati, 1 semua, 2 satu
static volatile int  musShufMode = 0;       // 0 mati, 1 acak, 2 AI (RYNE v2)

// ---- RYNE v2 (engine di ryne_engine.h) ----
static RyneEngine*   ryne = nullptr;        // dialokasi di PSRAM, hanya disentuh task musik
static int           musEngineCur = -1;     // lagu yg sedang "dibukukan" engine
static int           musHist[16];
static int           musHistN = 0;
static volatile int  musVibeIdx = 6, musVibePct = 0;
static volatile bool musLiked = false;
static volatile bool musRyneDirty = false;
static uint32_t      musRyneSaveMs = 0, musTickMs = 0;
static volatile uint32_t musUiTouch = 0;
static uint32_t      musUiTouchSeen = 0;
static int           musLastVolSeen = -1;
static volatile uint32_t musDurMs = 0;
static volatile uint32_t musKbps = 0, musSr = 0;
static volatile bool musStreaming = false, musFileDone = false;
static volatile bool musResumeOnBt = false;
static uint8_t       musSeq = 0;

static char          musScanName[MUS_SCAN_MAX][40];
static int8_t        musScanRssi[MUS_SCAN_MAX];
static volatile int  musScanCount = 0;
static volatile bool musScanning = false;
static volatile bool musConnecting = false;
static volatile uint32_t musConnectStart = 0;

static char          musMsg[48];
static volatile bool musMsgPending = false;
static volatile bool musPrefsDirty = false;
static volatile uint32_t musPrefsMs = 0;

// ---- UI ----
static int musPage = 0;       // 0 pemutar, 1 daftar, 2 cari TWS
static int musListTop = 0;

// =====================================================================
//  UTIL
// =====================================================================
static void musToast(const char* m) {
  strncpy(musMsg, m, sizeof(musMsg) - 1);
  musMsg[sizeof(musMsg) - 1] = 0;
  musMsgPending = true;
}

static void musPost(uint8_t t, int32_t a, const char* s) {
  if (!musQ) return;
  MusReq r; memset(&r, 0, sizeof(r));
  r.t = t; r.a = a;
  if (s) { strncpy(r.s, s, sizeof(r.s) - 1); }
  xQueueSend(musQ, &r, 0);
}

static int musListCount() {
  int n = 0;
  if (musMtx && xSemaphoreTake(musMtx, pdMS_TO_TICKS(15)) == pdTRUE) {
    n = (int)musList.size();
    xSemaphoreGive(musMtx);
  }
  return n;
}

static void musGetTitle(char* out, int cap, int idx) {
  out[0] = 0;
  if (!musMtx || xSemaphoreTake(musMtx, pdMS_TO_TICKS(15)) != pdTRUE) {
    strncpy(out, "...", cap - 1); out[cap - 1] = 0; return;
  }
  if (idx >= 0 && idx < (int)musList.size()) {
    const String& p = musList[idx];
    int sl = p.lastIndexOf('/');
    String nm = (sl >= 0) ? p.substring(sl + 1) : p;
    int dot = nm.lastIndexOf('.');
    if (dot > 0) nm = nm.substring(0, dot);
    strncpy(out, nm.c_str(), cap - 1); out[cap - 1] = 0;
  } else {
    strncpy(out, "Pilih lagu di Daftar", cap - 1); out[cap - 1] = 0;
  }
  xSemaphoreGive(musMtx);
}

static void musFmtTime(char* out, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(out, 12, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// =====================================================================
//  RYNE glue (task musik)
// =====================================================================
static uint32_t musKey(const String& path) {          // FNV-1a nama file (huruf kecil)
  int sl = path.lastIndexOf('/');
  String b = (sl >= 0) ? path.substring(sl + 1) : path;
  b.toLowerCase();
  uint32_t h = 2166136261u;
  for (int i = 0; i < (int)b.length(); i++) { h ^= (uint8_t)b[i]; h *= 16777619u; }
  return h;
}

static int musHourNow() {
  time_t t = time(nullptr);
  if (t < 1700000000) return -1;                      // jam belum disinkron
  struct tm tmv;
  localtime_r(&t, &tmv);
  return tmv.tm_hour;
}

static void musRyneLoad() {
  File f = SD_MMC.open("/ryne2.bin", FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  if (sz > 0 && sz <= RyneEngine::maxSerial()) {
    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      if (f.read(buf, sz) == (int)sz) ryne->deserialize(buf, sz);
      free(buf);
    }
  }
  f.close();
}

static void musRyneSave() {
  if (!ryne) return;
  size_t cap = RyneEngine::maxSerial();
  uint8_t* buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return;
  size_t sz = ryne->serialize(buf, cap);
  if (sz) {
    File f = SD_MMC.open("/ryne2.tmp", FILE_WRITE);
    if (f) {
      size_t w = f.write(buf, sz);
      f.close();
      if (w == sz) { SD_MMC.remove("/ryne2.bin"); SD_MMC.rename("/ryne2.tmp", "/ryne2.bin"); }
      else SD_MMC.remove("/ryne2.tmp");
    }
  }
  free(buf);
}

static void musRyneAttach(const uint32_t* keys, int n) {
  if (!ryne) {
    void* mem = heap_caps_malloc(sizeof(RyneEngine), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ryne = mem ? new (mem) RyneEngine() : new RyneEngine();
    ryne->seed(esp_random());
    musRyneLoad();
  }
  ryne->setPlaylist(n, keys);
  musEngineCur = -1;
  if (musCur >= 0 && musCur < n) musLiked = ryne->liked(musCur);
}

// tutup lagu yg sedang berjalan di buku engine. how: 0 habis, 1 skip, 2 pilih manual, 3 stop
static void musEndCurrent(int how) {
  if (ryne && musEngineCur >= 0) {
    ryne->onTrackEnd(musEngineCur, musPlayedMs, musDurMs, how, millis());
    musRyneDirty = true;
  }
  musEngineCur = -1;
}

static void musHistPush(int idx) {
  if (musHistN < 16) musHist[musHistN++] = idx;
  else { memmove(musHist, musHist + 1, 15 * sizeof(int)); musHist[15] = idx; }
}

// =====================================================================
//  PLAYLIST (dipanggil di task)
// =====================================================================
static void musScanPlaylist() {
  std::vector<String> tmp;
  String base = MUS_DIR;
  File root = SD_MMC.open(MUS_DIR);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    root = SD_MMC.open("/");
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

// header MP3: cari frame pertama utk sample rate & bitrate (estimasi durasi CBR)
static bool musParseMp3(uint32_t* sr, uint32_t* kbps, uint32_t* off) {
  uint8_t h[10];
  musFile.seek(0);
  if (musFile.read(h, 10) != 10) return false;
  uint32_t o = 0;
  if (h[0] == 'I' && h[1] == 'D' && h[2] == '3')
    o = 10 + (((uint32_t)(h[6] & 0x7F) << 21) | ((uint32_t)(h[7] & 0x7F) << 14) |
              ((uint32_t)(h[8] & 0x7F) << 7) | (uint32_t)(h[9] & 0x7F));
  *off = o;
  musFile.seek(o);
  int n = musFile.read(musBlk, 4096);
  static const uint16_t srT[3]  = {44100, 48000, 32000};
  static const uint16_t br1[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
  static const uint16_t br2[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
  for (int i = 0; i + 3 < n; i++) {
    if (musBlk[i] == 0xFF && (musBlk[i + 1] & 0xE0) == 0xE0) {
      int ver = (musBlk[i + 1] >> 3) & 3;      // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
      int layer = (musBlk[i + 1] >> 1) & 3;    // 1 = Layer III
      int bi = musBlk[i + 2] >> 4;
      int si = (musBlk[i + 2] >> 2) & 3;
      if (ver == 1 || layer != 1 || bi == 0 || bi == 15 || si == 3) continue;
      uint32_t s = srT[si];
      if (ver == 2) s /= 2; else if (ver == 0) s /= 4;
      *sr = s;
      *kbps = (ver == 3) ? br1[bi] : br2[bi];
      return true;
    }
  }
  return false;
}

// =====================================================================
//  KONTROL LAGU (task)
// =====================================================================
static bool musStartTrack(int idx, int prevHow) {
  int n = (int)musList.size();
  if (n == 0) { musToast("Tidak ada MP3 di /music"); return false; }
  if (!musCamAlive) { musToast("Modul BT tidak terdeteksi"); return false; }
  if (!musBtConn)   { musToast("TWS belum terhubung"); return false; }
  if (idx < 0) idx = n - 1;
  if (idx >= n) idx = 0;

  if (musFile) musFile.close();
  musFile = SD_MMC.open(musList[idx].c_str(), FILE_READ);
  if (!musFile) { musToast("Gagal membuka file"); return false; }

  uint32_t sr = 0, kbps = 0, off = 0;
  if (!musParseMp3(&sr, &kbps, &off)) {
    musFile.close();
    musToast("Bukan MP3 Layer III");
    return false;
  }
  uint64_t body = (musFile.size() > off) ? (musFile.size() - off) : musFile.size();
  musDurMs = kbps ? (uint32_t)((body * 8ULL) / kbps) : 0;   // bytes*8 / kbps = ms
  musKbps = kbps; musSr = sr;
  if (sr != 44100) musToast("MP3 bukan 44.1kHz, nada bergeser");
  musFile.seek(0);

  musEndCurrent(prevHow);                       // bukukan lagu sebelumnya (reward) SEBELUM musPlayedMs di-reset
  musSeq++;
  uint8_t sq = musSeq;
  musCredit = 0; musSentSince = 0;
  rpSend(Serial1, RP_BEGIN, &sq, 1);

  musCur = idx; musLoaded = true; musPaused = false; musEnded = false;
  musPlayedMs = 0; musStreaming = true; musFileDone = false;
  musPrefsDirty = true; musPrefsMs = millis();
  if (ryne) {
    ryne->onTrackStart(idx, millis());
    musEngineCur = idx;
    musLiked = ryne->liked(idx);
  }
  return true;
}

static int musNextIdx(int dir, bool autoNext) {
  int n = (int)musList.size();
  if (n == 0) return -1;
  if (autoNext && musRepeat == 2) return musCur;
  if (dir < 0 && musShufMode != 0 && musHistN > 0) return musHist[--musHistN];   // "kembali" = lagu sebelumnya
  if (musShufMode == 2 && dir > 0 && ryne && n > 1) {                            // AI: Thompson sampling
    int i = ryne->selectNext(musCur, n, millis());
    if (i >= 0) return i;
  }
  if (musShufMode != 0 && n > 1) {
    int r;
    do { r = (int)random(0, n); } while (r == musCur);
    return r;
  }
  int nx = musCur + dir;
  if (nx >= n) { if (musRepeat == 1 || !autoNext) nx = 0; else return -1; }
  if (nx < 0) nx = n - 1;
  return nx;
}

static void musSendPause(bool pause) {
  uint8_t v = pause ? 0 : 1;
  rpSend(Serial1, RP_PAUSE, &v, 1);
}

static void musHandleReq(uint8_t t, int32_t a, const char* s) {
  switch (t) {
    case 'p': musStartTrack((int)a, 2); break;
    case 'u':
      if (!musLoaded) musStartTrack(musCur < 0 ? 0 : musCur, 3);
      else { musPaused = !musPaused; musSendPause(musPaused); }
      break;
    case 'n': {
      int old = musCur, i = musNextIdx(1, false);
      if (i >= 0 && musStartTrack(i, 1) && musShufMode != 0) musHistPush(old);
      break;
    }
    case 'b':
      if (musLoaded && musPlayedMs > 3000) musStartTrack(musCur, 2);
      else { int i = musNextIdx(-1, false); if (i >= 0) musStartTrack(i, 2); }
      break;
    case 'v': {
      int v = (int)a; if (v < 0) v = 0; if (v > 127) v = 127;
      uint8_t b = (uint8_t)v;
      rpSend(Serial1, RP_VOLUME, &b, 1);
      break;
    }
    case 's':
      musEndCurrent(3);
      musLoaded = false; musStreaming = false;
      if (musFile) musFile.close();
      rpSend(Serial1, RP_STOP, nullptr, 0);
      break;
    case 'a':
      musScanCount = 0; musScanning = true;
      if (musLoaded && !musPaused) { musPaused = true; musSendPause(true); }
      rpSend(Serial1, RP_SCAN, nullptr, 0);
      break;
    case 'c':
      musConnecting = true; musConnectStart = millis();
      rpSend(Serial1, RP_CONNECT, (const uint8_t*)s, (uint16_t)strlen(s));
      break;
    case 'r': musScanPlaylist(); break;
    case 'k':                                     // tombol suka (toggle) untuk lagu terpilih
      if (ryne && musCur >= 0) {
        bool on = !ryne->liked(musCur);
        ryne->like(musCur, on, millis());
        musLiked = on; musRyneDirty = true;
      }
      break;
    default: break;
  }
}

// =====================================================================
//  TASK UTAMA LINK (core 0, stack PSRAM)
// =====================================================================
static void musTask(void* arg) {
  RpParser P;
  bool prevAlive = false, prevBt = false;
  musScanPlaylist();

  for (;;) {
    // ---- 1. terima dari CAM ----
    int avail = Serial1.available();
    while (avail-- > 0) {
      int c = Serial1.read();
      if (c < 0) break;
      musRxBytes++;
      if (!P.feed((uint8_t)c)) { musRxErr = P.errors; continue; }
      musRxFrames++;
      switch (P.type) {
        case RP_STATUS:
          if (P.len >= 13) {
            uint8_t fl = P.payload[0];
            musBtConn = (fl & 1) != 0;
            musCredit = (uint32_t)P.payload[3] | ((uint32_t)P.payload[4] << 8) |
                        ((uint32_t)P.payload[5] << 16) | ((uint32_t)P.payload[6] << 24);
            musSentSince = 0;
            musPlayedMs = (uint32_t)P.payload[8] | ((uint32_t)P.payload[9] << 8) |
                          ((uint32_t)P.payload[10] << 16) | ((uint32_t)P.payload[11] << 24);
            musLastStatusMs = millis();
            musCamAlive = true;
            if (P.len >= 17 && ryne) {                 // energi & kecerahan audio (CAM v2)
              uint32_t en = (uint32_t)P.payload[13] | ((uint32_t)P.payload[14] << 8);
              uint32_t zc = (uint32_t)P.payload[15] | ((uint32_t)P.payload[16] << 8);
              if (en > 0) {
                float loud = log10f(1.f + (float)en) / 4.0792f; if (loud > 1.f) loud = 1.f;
                float br = (float)zc / 260.f; if (br > 1.f) br = 1.f;
                bool pl = (fl & 2) != 0 && musLoaded && !musPaused;
                ryne->onAudio(loud, br, pl, millis());
              }
            }
          }
          break;
        case RP_ENDED:
          if (P.len >= 1 && P.payload[0] == musSeq && musLoaded) musEnded = true;
          break;
        case RP_SCANRES:
          if (P.len >= 2 && musScanCount < MUS_SCAN_MAX) {
            int k = musScanCount;
            musScanRssi[k] = (int8_t)P.payload[0];
            int l = P.len - 1; if (l > 39) l = 39;
            memcpy(musScanName[k], P.payload + 1, l);
            musScanName[k][l] = 0;
            musScanCount = k + 1;
          }
          break;
        case RP_SCANDONE: musScanning = false; break;
        case RP_HELLO:    musHello = true; break;
        default: break;
      }
    }
    if (musCamAlive && millis() - musLastStatusMs > 1500) musCamAlive = false;
    {   // keepalive ke CAM (utk LED diagnosa CAM)
      static uint32_t lastPing = 0;
      uint32_t np = millis();
      if (np - lastPing >= 1000) { lastPing = np; rpSend(Serial1, RP_PING, nullptr, 0); }
    }

    // ---- 2. CAM baru nyala / baru terdeteksi -> kirim ulang volume ----
    if (musHello) {
      musHello = false;
      if (musLoaded) { musLoaded = false; musStreaming = false; if (musFile) musFile.close(); musToast("Modul BT restart"); }
      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);
    } else if (musCamAlive && !prevAlive) {
      uint8_t v = (uint8_t)musVol; rpSend(Serial1, RP_VOLUME, &v, 1);
    }
    prevAlive = musCamAlive;

    // ---- 3. TWS putus -> jeda otomatis, lanjut otomatis pas nyambung lagi ----
    if (prevBt && !musBtConn && musLoaded && !musPaused) {
      musPaused = true; musSendPause(true); musResumeOnBt = true;
    }
    if (!prevBt && musBtConn && musResumeOnBt) {
      musResumeOnBt = false; musPaused = false; musSendPause(false);
    }
    prevBt = musBtConn;
    if (musConnecting && (musBtConn || millis() - musConnectStart > 12000)) musConnecting = false;

    // ---- 4. request dari UI ----
    MusReq rq;
    while (xQueueReceive(musQ, &rq, 0) == pdTRUE) musHandleReq(rq.t, rq.a, rq.s);

    // ---- 5. lagu habis -> berikutnya ----
    if (musEnded) {
      musEnded = false;
      musEndCurrent(0);                            // habis alami = reward positif
      int old = musCur, i = musNextIdx(1, true);
      if (i >= 0) { if (musStartTrack(i, 3) && musShufMode != 0 && i != old) musHistPush(old); }
      else { musLoaded = false; musStreaming = false; if (musFile) musFile.close(); }
    }

    // ---- 5b. RYNE: tick 2 Hz (vibe) + event volume/interaksi + simpan berkala ----
    {
      uint32_t nowT = millis();
      if (ryne && nowT - musTickMs >= 500) {
        musTickMs = nowT;
        if (musLastVolSeen < 0) musLastVolSeen = musVol;
        int dv = (int)musVol - musLastVolSeen;
        if (dv >= 4 || dv <= -4) { ryne->onVolume(dv, nowT); musLastVolSeen = musVol; }
        if (musUiTouch != musUiTouchSeen) { musUiTouchSeen = musUiTouch; ryne->onInteraction(nowT); }
        ryne->tick(nowT, musHourNow(), (int)musVol, (int)musRepeat, musLoaded && !musPaused);
        int top = ryne->vibeTop();
        musVibeIdx = top; musVibePct = (int)(ryne->pv[top] * 100.f + 0.5f);
        if (musRyneDirty && nowT - musRyneSaveMs > 15000) {
          musRyneDirty = false; musRyneSaveMs = nowT; musRyneSave();
        }
      }
    }

    // ---- 6. streaming file -> CAM (kredit dari status CAM) ----
    if (musStreaming && !musFileDone) {
      int32_t freeEst = (int32_t)musCredit - (int32_t)musSentSince;
      if (freeEst > MUS_CREDIT_MIN) {
        int n = musFile.read(musBlk, 4096);
        if (n <= 0) {
          rpSend(Serial1, RP_FINISH, nullptr, 0);
          musFileDone = true; musStreaming = false;
          if (musFile) musFile.close();
        } else {
          for (int off = 0; off < n; off += 1024) {
            int c = n - off; if (c > 1024) c = 1024;
            rpSend(Serial1, RP_DATA, musBlk + off, (uint16_t)c);
          }
          musSentSince += (uint32_t)n;
        }
        vTaskDelay(1);
        continue;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// =====================================================================
//  START (lazy, pas app dibuka pertama kali)
// =====================================================================
static void musBegin() {
  if (musStarted) return;
  musStarted = true;
  musMtx = xSemaphoreCreateMutex();
  musQ = xQueueCreate(8, sizeof(MusReq));
  musBlk = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!musBlk) musBlk = (uint8_t*)malloc(4096);
  {
    Preferences p; p.begin("music", true);
    musVol = p.getInt("vol", 80);
    musRepeat = p.getInt("rep", 0);
    musShufMode = p.getInt("smode", p.getBool("shuf", false) ? 1 : 0);
    musCur = p.getInt("cur", 0);
    p.end();
  }
  Serial1.setRxBufferSize(2048);
  Serial1.begin(RP_BAUD, SERIAL_8N1, MUS_UART_RX, MUS_UART_TX);

  const uint32_t stackBytes = 12288;
  if (!musTaskStack)
    musTaskStack = (StackType_t*)heap_caps_aligned_alloc(16, stackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (musTaskStack) {
    musTaskHandle = xTaskCreateStaticPinnedToCore(musTask, "musTask", stackBytes, NULL, 1,
                                                  musTaskStack, &musTaskTCB, 0);
  } else {
    xTaskCreatePinnedToCore(musTask, "musTask", stackBytes, NULL, 1, &musTaskHandle, 0);
  }
}

// =====================================================================
//  HOOK DARI phone.ino
// =====================================================================
void musicEnter() {
  musBegin();
  musPage = 0;
  if (musListCount() == 0) musPost('r', 0, nullptr);
}
void musicExit() {}

// dipanggil TERUS dari loop() utama (murah kalau app belum pernah dibuka)
void musicLoopPoll() {
  if (!musStarted) return;
  uint32_t now = millis();
  bool onScr = (curScreen() == SCR_MUSIC);

  if (musMsgPending) {
    musMsgPending = false;
    if (onScr || musLoaded) showToast(musMsg);
    needRedraw = true;
  }
  static bool prevConn = false;
  if (musBtConn != prevConn) {
    prevConn = musBtConn;
    if (onScr || musLoaded) showToast(musBtConn ? "TWS terhubung" : "TWS terputus");
    needRedraw = true;
  }
  // volume: kirim ke CAM maks tiap 60 ms (slider di-drag banyak event)
  static int lastSentVol = -1;
  static uint32_t lastVolMs = 0;
  if (musVol != lastSentVol && now - lastVolMs > 60) {
    musPost('v', musVol, nullptr);
    lastSentVol = musVol; lastVolMs = now;
  }
  // kadensi redraw: animasi (cover/equalizer/gulir judul) butuh ~10 fps
  static uint32_t lastRd = 0;
  uint32_t iv = 500;
  if (onScr) {
    if (musPage == 0)      iv = 100;
    else if (musPage == 1) iv = (musLoaded && !musPaused) ? 150 : 500;
    else                   iv = (musScanning || musConnecting) ? 100 : 500;
  }
  if (onScr && now - lastRd >= iv) { lastRd = now; needRedraw = true; }
  if (musPrefsDirty && now - musPrefsMs > 1500) {
    musPrefsDirty = false;
    Preferences p; p.begin("music", false);
    p.putInt("vol", musVol); p.putInt("rep", musRepeat);
    p.putInt("smode", musShufMode); p.putInt("cur", musCur);
    p.end();
  }
}

// =====================================================================
//  UI  (v2: cover vinyl berputar, equalizer, panel flat biar ringan)
//  drawGlassPanel sengaja TIDAK dipakai di sini: dia nyampel wallpaper + blur
//  per panel, kemahalan buat layar yg di-redraw ~10x/detik (animasi).
// =====================================================================
static bool  mLand = true;
static int   mHdrY, mChipH, mDafX, mDafW, mTwsX, mTwsW;
static int   mCovX, mCovY, mCovS, mTitX, mTitY, mTitW, mSubY;
static int   mEqX, mEqY, mEqW, mEqH, mVibeY;
static int   mVolY, mVolIcX, mVolSX, mVolSW;
static int   mBarX, mBarY, mBarW, mTimeY;
static int   mCtlCY, mShufX, mPrevX, mPlayX, mNextX, mRepX;
static int   mBotY, mBotH;
static const int mPlayR = 20;
static float    musEq[16];
static float    musDiscAng = 0;
static uint32_t musAnimMs = 0;
static int      musMuteVol = 0;

static void musCalcLayout() {
  int W = SCR_W, H = SCR_H;
  mLand = (W >= H);
  mHdrY = 25; mChipH = 18;
  mDafW = 70; mDafX = 12;
  mTwsW = 118; mTwsX = W - 12 - mTwsW;
  int cx = W / 2;
  int dPN = (W >= 300) ? 62 : 50, dSR = (W >= 300) ? 112 : 92;
  mCtlCY = H - BACK_H - 3 - 4 - mPlayR;
  mPlayX = cx; mPrevX = cx - dPN; mNextX = cx + dPN; mShufX = cx - dSR; mRepX = cx + dSR;
  mBotH = 28; mBotY = H - BACK_H - 3 - 6 - mBotH;
  if (mLand) {
    mCovX = 12; mCovY = 50; mCovS = 88;
    int x0 = mCovX + mCovS + 12, rw = W - 12 - x0;
    mTitX = x0; mTitY = 52; mTitW = rw; mSubY = 74;
    mVibeY = 88;
    mEqX = x0; mEqY = 100; mEqW = rw; mEqH = 18;
    mVolY = 124; mVolIcX = x0 + 8; mVolSX = x0 + 26; mVolSW = rw - 26;
    mBarX = 12; mBarY = 152; mBarW = W - 24; mTimeY = 141;
  } else {
    mCovS = 120; mCovX = (W - mCovS) / 2; mCovY = 48;
    mTitX = 12; mTitY = 176; mTitW = W - 24; mSubY = 197;
    mVibeY = -1;
    mEqX = 12; mEqY = 0; mEqW = 0; mEqH = 0;
    mVolY = 208; mVolIcX = 20; mVolSX = 38; mVolSW = W - 12 - 38;
    mBarX = 12; mBarY = 242; mBarW = W - 24; mTimeY = 231;
  }
}

static bool musHit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}
static bool musHitC(int x, int y, int cx, int cy, int r) {
  return x >= cx - r && x <= cx + r && y >= cy - r && y <= cy + r;
}

static int musListRows() { int r = (mBotY - 8 - 48) / 30; return r < 1 ? 1 : r; }
static int musScanRows() { int r = (mBotY - 8 - 48) / 30; return r < 1 ? 1 : r; }

static void musPanel(LGFX_Sprite& s, int x, int y, int w, int h, int r, uint16_t col, uint8_t a) {
  s.fillRoundRect(x, y, w, h, r, blend565(T().bg, col, a));
}

static void musSetVol(int v) {
  if (v < 0) v = 0; if (v > 127) v = 127;
  musVol = v; musPrefsDirty = true; musPrefsMs = millis();
}

// HSV (h 0..360, s,v 0..1) -> RGB565
static uint16_t musHsv(float h, float sat, float v) {
  h = fmodf(h, 360.f); if (h < 0) h += 360.f;
  float c = v * sat, x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f)), m = v - c;
  float r = 0, g = 0, b = 0;
  int sec = (int)(h / 60.f);
  switch (sec) {
    case 0: r = c; g = x; break;  case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;  case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;  default: r = c; b = x; break;
  }
  int R = (int)((r + m) * 31), G = (int)((g + m) * 63), B = (int)((b + m) * 31);
  return (uint16_t)((R << 11) | (G << 5) | B);
}

// ---- ikon vektor ----   kind: 0 prev, 1 next, 2 play, 3 pause
static void musDrawIcon(LGFX_Sprite& s, int kind, int cx, int cy, uint16_t c) {
  switch (kind) {
    case 0: s.fillRect(cx - 9, cy - 8, 3, 16, c); s.fillTriangle(cx + 8, cy - 8, cx + 8, cy + 8, cx - 6, cy, c); break;
    case 1: s.fillRect(cx + 6, cy - 8, 3, 16, c); s.fillTriangle(cx - 8, cy - 8, cx - 8, cy + 8, cx + 6, cy, c); break;
    case 2: s.fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 10, cy, c); break;
    default: s.fillRect(cx - 7, cy - 9, 5, 18, c); s.fillRect(cx + 2, cy - 9, 5, 18, c); break;
  }
}
static void musDrawShuffle(LGFX_Sprite& s, int cx, int cy, uint16_t c) {
  s.drawLine(cx - 9, cy - 5, cx + 4, cy + 5, c); s.drawLine(cx - 9, cy - 4, cx + 4, cy + 6, c);
  s.drawLine(cx - 9, cy + 5, cx + 4, cy - 5, c); s.drawLine(cx - 9, cy + 6, cx + 4, cy - 4, c);
  s.fillTriangle(cx + 10, cy + 5, cx + 4, cy + 1, cx + 4, cy + 9, c);
  s.fillTriangle(cx + 10, cy - 5, cx + 4, cy - 9, cx + 4, cy - 1, c);
}
static void musDrawRepeat(LGFX_Sprite& s, int cx, int cy, uint16_t c, bool one) {
  s.drawRoundRect(cx - 10, cy - 6, 20, 12, 4, c);
  s.fillRect(cx + 3, cy - 7, 7, 3, T().bg);
  s.fillRect(cx - 10, cy + 4, 7, 3, T().bg);
  s.fillTriangle(cx + 9, cy - 5, cx + 3, cy - 9, cx + 3, cy - 1, c);
  s.fillTriangle(cx - 9, cy + 5, cx - 3, cy + 1, cx - 3, cy + 9, c);
  if (one) { s.setTextSize(1); s.setTextColor(c); s.setCursor(cx - 2, cy - 3); s.print("1"); }
}
static void musDrawHeart(LGFX_Sprite& s, int cx, int cy, uint16_t c) {
  s.fillCircle(cx - 3, cy - 2, 3, c);
  s.fillCircle(cx + 3, cy - 2, 3, c);
  s.fillTriangle(cx - 6, cy - 1, cx + 6, cy - 1, cx, cy + 6, c);
}
static void musDrawSpeaker(LGFX_Sprite& s, int cx, int cy, uint16_t c, int lvl) {
  s.fillRect(cx - 7, cy - 3, 4, 6, c);
  s.fillTriangle(cx - 3, cy - 3, cx + 2, cy - 7, cx + 2, cy + 7, c);
  s.fillTriangle(cx - 3, cy - 3, cx + 2, cy + 7, cx - 3, cy + 3, c);
  if (lvl >= 1) s.drawLine(cx + 5, cy - 3, cx + 5, cy + 3, c);
  if (lvl >= 2) s.drawLine(cx + 8, cy - 6, cx + 8, cy + 6, c);
  if (lvl == 0) { s.drawLine(cx + 5, cy - 4, cx + 11, cy + 4, c); s.drawLine(cx + 5, cy + 4, cx + 11, cy - 4, c); }
}
static void musDrawSignal(LGFX_Sprite& s, int x, int y, int rssi) {
  int lvl = (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    s.fillRect(x + i * 5, y + 13 - h, 3, h, i < lvl ? T().accent : blend565(T().bg, T().text, 50));
  }
}

static void musDrawLabel(LGFX_Sprite& s, const char* t, int x, int y, int w, int h, uint16_t col) {
  s.setTextSize(1); s.setTextColor(col);
  int tw = s.textWidth(t);
  s.setCursor(x + w / 2 - tw / 2, y + h / 2 - 4);
  s.print(t);
}
static void musTrunc(char* nm, int maxc) {
  if ((int)strlen(nm) > maxc && maxc > 3) { nm[maxc - 1] = 0; nm[maxc - 2] = '.'; nm[maxc - 3] = '.'; }
}

// ---- cover: warna unik per judul + piringan hitam berputar ----
static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {
  uint32_t hsh = 5381;
  for (const char* q = title; *q; q++) hsh = hsh * 33 + (uint8_t)*q;
  float hue = (float)(hsh % 360);
  uint16_t c1 = musHsv(hue, 0.65f, 0.85f), c2 = musHsv(hue + 50.f, 0.70f, 0.42f);
  int x = mCovX, y = mCovY, S = mCovS;
  for (int i = 0; i < S; i += 4) {
    int hh = (S - i < 4) ? (S - i) : 4;
    s.fillRect(x, y + i, S, hh, blend565(c1, c2, (uint8_t)((i * 255) / S)));
  }
  s.drawRoundRect(x - 1, y - 1, S + 2, S + 2, 2, blend565(T().bg, T().text, 60));

  int cx = x + S / 2, cy = y + S / 2, R = (S * 44) / 100;
  s.fillCircle(cx, cy, R, 0x1082);
  for (int g = 6; g < R - 8; g += 6) s.drawCircle(cx, cy, R - g, 0x2124);
  uint32_t now = millis();
  float dt = (now - musAnimMs) / 1000.f; if (dt > 0.25f) dt = 0.25f;
  musAnimMs = now;
  if (playing) musDiscAng += dt * 150.f;
  if (musDiscAng > 360.f) musDiscAng -= 360.f;
  for (int k = 0; k < 2; k++) {
    float a = (musDiscAng + k * 180.f) * 0.0174533f;
    float ca = cosf(a), sa = sinf(a);
    s.drawLine(cx + (int)(ca * R * 0.5f), cy + (int)(sa * R * 0.5f),
               cx + (int)(ca * (R - 2)),  cy + (int)(sa * (R - 2)), 0x52AA);
  }
  s.fillCircle(cx, cy, (R * 38) / 100, c1);
  s.fillCircle(cx, cy, 3, 0x1082);
}

static void musDrawEq(LGFX_Sprite& s, bool playing) {
  if (mEqH <= 0) return;
  const int n = 16, gap = 3;
  int bw = (mEqW - (n - 1) * gap) / n; if (bw < 2) bw = 2;
  uint32_t t = millis();
  for (int i = 0; i < n; i++) {
    float tgt = playing ? (0.18f + 0.82f * fabsf(sinf(t * 0.0021f * (1.f + 0.23f * i) + i * 1.7f) *
                                                 cosf(t * 0.0013f + i * 0.9f)))
                        : 0.07f;
    musEq[i] += (tgt - musEq[i]) * 0.45f;
    int h = (int)(musEq[i] * mEqH); if (h < 2) h = 2;
    s.fillRoundRect(mEqX + i * (bw + gap), mEqY + mEqH - h, bw, h, 1,
                    blend565(T().accent, T().accent2, (uint8_t)((i * 255) / (n - 1))));
  }
}

static void musDrawChips(LGFX_Sprite& s) {
  musPanel(s, mDafX, mHdrY, mDafW, mChipH, 9, T().surface2, 210);
  for (int i = 0; i < 3; i++) s.fillRect(mDafX + 10, mHdrY + 5 + i * 4, 10, 2, T().text);
  s.setTextSize(1); s.setTextColor(T().text);
  s.setCursor(mDafX + 26, mHdrY + 5); s.print("Daftar");

  const char* st; uint16_t col;
  if (!musCamAlive)        { st = "Modul BT mati";     col = T().danger; }
  else if (musBtConn)      { st = "TWS terhubung";     col = T().good; }
  else if (musConnecting)  { st = "Menghubungkan...";  col = T().accent2; }
  else                     { st = "TWS putus";         col = T().subtext; }
  musPanel(s, mTwsX, mHdrY, mTwsW, mChipH, 9, T().surface2, 210);
  s.fillCircle(mTwsX + 11, mHdrY + 9, 4, col);
  if (musConnecting && !musBtConn) s.drawCircle(mTwsX + 11, mHdrY + 9, 5 + (int)((millis() / 150) % 4), col);
  s.setTextColor(T().text); s.setCursor(mTwsX + 22, mHdrY + 5); s.print(st);
}

static void musDrawPlayer(LGFX_Sprite& s) {
  bool playing = musLoaded && !musPaused;
  char title[64];
  musGetTitle(title, sizeof(title), musCur);

  musDrawChips(s);
  musDrawCover(s, playing, title);

  // judul (gulir kalau kepanjangan)
  s.setTextSize(2); s.setTextColor(T().text);
  int tw = s.textWidth(title);
  s.setClipRect(mTitX, mTitY - 1, mTitW, 19);
  if (tw <= mTitW) {
    int tx = mLand ? mTitX : mTitX + (mTitW - tw) / 2;
    s.setCursor(tx, mTitY); s.print(title);
  } else {
    int span = tw + 40;
    int off = (int)((millis() / 25) % (uint32_t)span);
    s.setCursor(mTitX - off, mTitY);        s.print(title);
    s.setCursor(mTitX - off + span, mTitY); s.print(title);
  }
  s.clearClipRect();

  // info
  char sub[64];
  int vp = musVibePct, vi = musVibeIdx;
  if (vi < 0 || vi >= RY_NV) vi = 6;
  if (mLand) {
    if (musLoaded && musKbps)
      snprintf(sub, sizeof(sub), "%d/%d   MP3 %ukbps  %u.%ukHz", musCur + 1, musListCount(),
               (unsigned)musKbps, (unsigned)(musSr / 1000), (unsigned)((musSr % 1000) / 100));
    else
      snprintf(sub, sizeof(sub), "%d/%d", musCur + 1, musListCount());
  } else {
    if (vp > 0) snprintf(sub, sizeof(sub), "%d/%d   %s %d%%", musCur + 1, musListCount(), RY_VIBE_NAME[vi], vp);
    else        snprintf(sub, sizeof(sub), "%d/%d", musCur + 1, musListCount());
  }
  s.setTextSize(1); s.setTextColor(T().subtext);
  int sw = s.textWidth(sub);
  s.setCursor(mLand ? mTitX : mTitX + (mTitW - sw) / 2, mSubY); s.print(sub);
  musDrawHeart(s, mTitX + mTitW - 8, mSubY + 4, musLiked ? 0xF9A6 : blend565(T().bg, T().text, 70));

  if (!musCamAlive) {                                  // diagnosa: apa yg masuk dari CAM?
    char dg[56];
    snprintf(dg, sizeof(dg), "link: rx %uB  frame %u  salah %u", (unsigned)musRxBytes, (unsigned)musRxFrames, (unsigned)musRxErr);
    s.setTextColor(T().danger); s.setCursor(mTitX, mVibeY >= 0 ? mVibeY : mSubY + 11); s.print(dg);
  } else if (mVibeY >= 0) {                            // baris vibe (RYNE)
    uint16_t vc = musHsv(vi * 45.f, 0.7f, 0.95f);
    s.fillCircle(mTitX + 4, mVibeY + 4, 3, vc);
    char vb[40];
    if (vp > 0) snprintf(vb, sizeof(vb), "Vibe: %s %d%%", RY_VIBE_NAME[vi], vp);
    else        snprintf(vb, sizeof(vb), "Vibe: mempelajari...");
    s.setTextColor(T().text); s.setCursor(mTitX + 12, mVibeY); s.print(vb);
  }

  musDrawEq(s, playing);

  // volume
  int lvl = (musVol == 0) ? 0 : (musVol < 64 ? 1 : 2);
  musDrawSpeaker(s, mVolIcX, mVolY + 7, T().subtext, lvl);
  s.fillRoundRect(mVolSX, mVolY + 4, mVolSW, 6, 3, blend565(T().bg, T().text, 40));
  int vf = (mVolSW * musVol) / 127;
  if (vf > 0) s.fillRoundRect(mVolSX, mVolY + 4, vf, 6, 3, T().accent2);
  s.fillCircle(mVolSX + vf, mVolY + 7, 6, T().text);

  // progress + waktu
  char t1[12], t2[12];
  musFmtTime(t1, musLoaded ? musPlayedMs : 0);
  if (musLoaded && musDurMs > 0) musFmtTime(t2, musDurMs); else strcpy(t2, "--:--");
  s.setTextColor(T().subtext);
  s.setCursor(mBarX, mTimeY); s.print(t1);
  s.setCursor(mBarX + mBarW - s.textWidth(t2), mTimeY); s.print(t2);
  s.fillRoundRect(mBarX, mBarY, mBarW, 6, 3, blend565(T().bg, T().text, 40));
  float pr = (musDurMs > 0) ? (float)musPlayedMs / (float)musDurMs : 0.f;
  if (pr > 1.f) pr = 1.f;
  int fw = (int)(pr * mBarW);
  if (musLoaded && fw > 0) {
    s.fillRoundRect(mBarX, mBarY, fw, 6, 3, T().accent);
    s.fillCircle(mBarX + fw, mBarY + 3, 6, T().text);
  }

  // kontrol
  int sm = musShufMode;
  musDrawShuffle(s, mShufX, mCtlCY, sm == 2 ? T().accent2 : (sm == 1 ? T().accent : T().subtext));
  if (sm == 1) s.fillCircle(mShufX, mCtlCY + 15, 2, T().accent);
  if (sm == 2) { s.setTextSize(1); s.setTextColor(T().accent2); s.setCursor(mShufX - 6, mCtlCY + 11); s.print("AI"); }
  musDrawRepeat(s, mRepX, mCtlCY, musRepeat ? T().accent : T().subtext, musRepeat == 2);
  if (musRepeat) s.fillCircle(mRepX, mCtlCY + 15, 2, T().accent);
  musDrawIcon(s, 0, mPrevX, mCtlCY, T().text);
  musDrawIcon(s, 1, mNextX, mCtlCY, T().text);
  if (playing) s.drawCircle(mPlayX, mCtlCY, mPlayR + 3, blend565(T().bg, T().accent, 120));
  s.fillCircle(mPlayX, mCtlCY, mPlayR, T().accent);
  musDrawIcon(s, playing ? 3 : 2, mPlayX, mCtlCY, T().bg);
}

static void musMiniEq(LGFX_Sprite& s, int x, int yBase, bool playing, uint16_t col) {
  uint32_t t = millis();
  for (int i = 0; i < 3; i++) {
    float f = playing ? (0.25f + 0.75f * fabsf(sinf(t * 0.006f * (1.f + 0.4f * i) + i * 2.1f))) : 0.2f;
    int h = 3 + (int)(f * 11);
    s.fillRect(x + i * 5, yBase - h, 3, h, col);
  }
}

static void musDrawList(LGFX_Sprite& s) {
  int n = musListCount();
  int rows = musListRows();
  if (musListTop >= n) musListTop = 0;
  s.setTextSize(2); s.setTextColor(T().accent); s.setCursor(12, mHdrY + 1); s.print("Daftar Lagu");
  char cnt[16]; snprintf(cnt, sizeof(cnt), "%d lagu", n);
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(SCR_W - 12 - s.textWidth(cnt), mHdrY + 5); s.print(cnt);

  int maxc = (SCR_W - 80) / 6;
  if (n == 0) {
    s.setTextColor(T().subtext);
    s.setCursor(12, 56); s.print("Tidak ada MP3 di folder /music");
    s.setCursor(12, 70); s.print("(atau di root SD). Taruh file lalu");
    s.setCursor(12, 84); s.print("buka ulang app ini.");
  }
  for (int r = 0; r < rows; r++) {
    int idx = musListTop + r;
    if (idx >= n) break;
    int y = 48 + r * 30;
    bool cur = (idx == musCur);
    musPanel(s, 12, y, SCR_W - 30, 28, 8, cur ? T().accent : T().surface2, cur ? 90 : 170);
    s.fillCircle(28, y + 14, 10, cur ? T().accent : blend565(T().bg, T().text, 40));
    char num[12]; snprintf(num, sizeof(num), "%d", idx + 1);
    s.setTextSize(1); s.setTextColor(cur ? T().bg : T().subtext);
    s.setCursor(28 - s.textWidth(num) / 2, y + 10); s.print(num);
    char nm[64]; musGetTitle(nm, sizeof(nm), idx);
    musTrunc(nm, maxc);
    s.setTextColor(T().text); s.setCursor(46, y + 10); s.print(nm);
    if (cur) musMiniEq(s, SCR_W - 18 - 14, y + 22, musLoaded && !musPaused, T().accent);
  }
  if (n > rows) {                                   // scrollbar
    int trackH = rows * 30 - 2, thumbH = (trackH * rows) / n; if (thumbH < 12) thumbH = 12;
    int thumbY = 48 + ((trackH - thumbH) * musListTop) / (n - rows > 0 ? n - rows : 1);
    s.fillRoundRect(SCR_W - 9, 48, 3, trackH, 1, blend565(T().bg, T().text, 30));
    s.fillRoundRect(SCR_W - 9, thumbY, 3, thumbH, 1, T().accent);
  }

  int bw3 = (SCR_W - 24 - 12) / 3;
  int bx0 = 12, bx1 = 12 + bw3 + 6, bx2 = 12 + 2 * (bw3 + 6);
  musPanel(s, bx0, mBotY, bw3, mBotH, 10, T().surface2, 210);
  s.fillTriangle(bx0 + bw3 / 2, mBotY + 9, bx0 + bw3 / 2 - 7, mBotY + 19, bx0 + bw3 / 2 + 7, mBotY + 19, T().text);
  musPanel(s, bx1, mBotY, bw3, mBotH, 10, T().surface2, 210);
  s.fillTriangle(bx1 + bw3 / 2, mBotY + 19, bx1 + bw3 / 2 - 7, mBotY + 9, bx1 + bw3 / 2 + 7, mBotY + 9, T().text);
  s.fillRoundRect(bx2, mBotY, bw3, mBotH, 10, T().accent);
  musDrawLabel(s, "Pemutar", bx2, mBotY, bw3, mBotH, T().bg);
}

static void musDrawScan(LGFX_Sprite& s) {
  s.setTextSize(2); s.setTextColor(T().accent); s.setCursor(12, mHdrY + 1); s.print("Cari TWS");
  if (musScanning) {                                // radar
    int rcx = SCR_W - 24, rcy = mHdrY + 9;
    int ph = (int)((millis() / 90) % 12);
    s.fillCircle(rcx, rcy, 3, T().accent);
    s.drawCircle(rcx, rcy, 3 + ph, blend565(T().bg, T().accent, (uint8_t)(255 - ph * 20)));
    s.drawCircle(rcx, rcy, 3 + ((ph + 6) % 12), blend565(T().bg, T().accent, (uint8_t)(255 - ((ph + 6) % 12) * 20)));
  }
  s.setTextSize(1);
  s.setCursor(12, 46);
  if (!musCamAlive) { s.setTextColor(T().danger); s.print("Modul BT tidak terdeteksi (cek kabel)"); }
  else if (musScanning) {
    s.setTextColor(T().subtext);
    int d = (millis() / 300) % 4;
    s.print("Memindai"); for (int i = 0; i < d; i++) s.print(".");
  } else {
    s.setTextColor(T().subtext);
    s.print(musScanCount); s.print(" perangkat. Ketuk untuk menyambung.");
  }

  int rows = musScanRows();
  int maxc = (SCR_W - 90) / 6;
  for (int r = 0; r < rows && r < musScanCount; r++) {
    int y = 58 + r * 30;
    musPanel(s, 12, y, SCR_W - 24, 28, 8, T().surface2, 180);
    musDrawSignal(s, 20, y + 7, musScanRssi[r]);
    char nm[40]; strncpy(nm, musScanName[r], sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    musTrunc(nm, maxc);
    s.setTextSize(1); s.setTextColor(T().text); s.setCursor(48, y + 10); s.print(nm);
    char rs[8]; snprintf(rs, sizeof(rs), "%d", (int)musScanRssi[r]);
    s.setTextColor(T().subtext); s.setCursor(SCR_W - 20 - s.textWidth(rs), y + 10); s.print(rs);
  }
  int bw2 = (SCR_W - 24 - 6) / 2;
  musPanel(s, 12, mBotY, bw2, mBotH, 10, T().surface2, 210);
  musDrawLabel(s, "Pindai ulang", 12, mBotY, bw2, mBotH, T().text);
  s.fillRoundRect(12 + bw2 + 6, mBotY, bw2, mBotH, 10, T().accent);
  musDrawLabel(s, "Pemutar", 12 + bw2 + 6, mBotY, bw2, mBotH, T().bg);
}

void drawMusic(LGFX_Sprite& s) {
  musCalcLayout();
  s.fillSprite(T().bg); drawStatusBar(s);
  if (musPage == 1)      musDrawList(s);
  else if (musPage == 2) musDrawScan(s);
  else                   musDrawPlayer(s);
  drawBack(s); drawToast(s);
}

void musicTouch(int x, int y, bool held, bool isNew) {
  musCalcLayout();
  if (isNew) musUiTouch++;                          // sinyal 'interaksi' utk RYNE

  // slider volume: boleh di-drag (event held)
  if (musPage == 0 && (isNew || held) && musHit(x, y, mVolSX - 6, mVolY - 4, mVolSW + 12, 20)) {
    musSetVol(((x - mVolSX) * 127) / (mVolSW > 0 ? mVolSW : 1));
    needRedraw = true; return;
  }
  if (!isNew) return;

  if (isBack(x, y)) {
    if (musPage != 0) { musPage = 0; needRedraw = true; }
    else navBack();
    return;
  }

  if (musPage == 1) {
    int n = musListCount(), rows = musListRows();
    for (int r = 0; r < rows; r++) {
      int idx = musListTop + r;
      if (idx >= n) break;
      if (musHit(x, y, 12, 48 + r * 30, SCR_W - 30, 28)) {
        musPost('p', idx, nullptr); musPage = 0; vibTap(); needRedraw = true; return;
      }
    }
    int bw3 = (SCR_W - 24 - 12) / 3;
    int bx0 = 12, bx1 = 12 + bw3 + 6, bx2 = 12 + 2 * (bw3 + 6);
    if (musHit(x, y, bx0, mBotY, bw3, mBotH)) { musListTop -= rows; if (musListTop < 0) musListTop = 0; vibTap(); needRedraw = true; return; }
    if (musHit(x, y, bx1, mBotY, bw3, mBotH)) { if (musListTop + rows < n) musListTop += rows; vibTap(); needRedraw = true; return; }
    if (musHit(x, y, bx2, mBotY, bw3, mBotH)) { musPage = 0; vibTap(); needRedraw = true; return; }
    return;
  }

  if (musPage == 2) {
    int rows = musScanRows();
    for (int r = 0; r < rows && r < musScanCount; r++) {
      if (musHit(x, y, 12, 58 + r * 30, SCR_W - 24, 28)) {
        musPost('c', 0, musScanName[r]);
        showToast("Menghubungkan..."); musPage = 0; vibTap(); needRedraw = true; return;
      }
    }
    int bw2 = (SCR_W - 24 - 6) / 2;
    if (musHit(x, y, 12, mBotY, bw2, mBotH)) { musPost('a', 0, nullptr); vibTap(); needRedraw = true; return; }
    if (musHit(x, y, 12 + bw2 + 6, mBotY, bw2, mBotH)) { musPage = 0; vibTap(); needRedraw = true; return; }
    return;
  }

  // ---- halaman pemutar ----
  if (musHit(x, y, mDafX, mHdrY - 3, mDafW, mChipH + 6)) { musPage = 1; int r = musListRows(); musListTop = (musCur / r) * r; vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mTwsX, mHdrY - 3, mTwsW, mChipH + 6)) { musPage = 2; musPost('a', 0, nullptr); vibTap(); needRedraw = true; return; }

  if (musHitC(x, y, mPlayX, mCtlCY, mPlayR + 6)) { musPost('u', 0, nullptr); vibTap(); needRedraw = true; return; }
  if (musHitC(x, y, mPrevX, mCtlCY, 22)) { musPost('b', 0, nullptr); vibTap(); needRedraw = true; return; }
  if (musHitC(x, y, mNextX, mCtlCY, 22)) { musPost('n', 0, nullptr); vibTap(); needRedraw = true; return; }
  if (musHitC(x, y, mShufX, mCtlCY, 20)) {
    musShufMode = (musShufMode + 1) % 3; musPrefsDirty = true; musPrefsMs = millis(); vibTap();
    showToast(musShufMode == 0 ? "Acak: mati" : (musShufMode == 1 ? "Acak: biasa" : "Acak: AI (RYNE)"));
    needRedraw = true; return;
  }
  if (musHitC(x, y, mTitX + mTitW - 8, mSubY + 4, 14)) {          // hati = suka
    musPost('k', 0, nullptr); musLiked = !musLiked; vibTap(); needRedraw = true; return;
  }
  if (musHitC(x, y, mRepX, mCtlCY, 20)) { musRepeat = (musRepeat + 1) % 3; musPrefsDirty = true; musPrefsMs = millis(); vibTap(); needRedraw = true; return; }

  if (musHitC(x, y, mVolIcX, mVolY + 7, 12)) {      // ikon speaker = bisu / kembalikan
    if (musVol > 0) { musMuteVol = musVol; musSetVol(0); }
    else            { musSetVol(musMuteVol > 0 ? musMuteVol : 80); }
    vibTap(); needRedraw = true; return;
  }
  if (musHit(x, y, mCovX, mCovY, mCovS, mCovS)) { musPost('u', 0, nullptr); vibTap(); needRedraw = true; return; }   // ketuk cover = play/pause
}
