// =====================================================================
//  musicbt_renphone.ino -- app "Musik" (Ren Phone, ESP32-S3)
//
//  S3 = MASTER. ESP32-CAM (firmware cam_bt_coprocessor) = co-prosesor
//  Bluetooth A2DP. Alur: SD S3 -> UART 921600 -> CAM (decode MP3) -> TWS.
//
//  Kabel:  S3 GPIO17 (TX) -> CAM GPIO13 (RX)
//          S3 GPIO16 (RX) <- CAM GPIO14 (TX)
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
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

#define MUS_UART_RX     16
#define MUS_UART_TX     17
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

// ---- milik S3 ----
static std::vector<String> musList;
static volatile int  musCur = 0;
static volatile bool musLoaded = false;      // ada lagu yg lagi di-stream/diputar
static volatile bool musPaused = false;
static volatile int  musVol = 80;
static volatile int  musRepeat = 0;          // 0 mati, 1 semua, 2 satu
static volatile bool musShuffle = false;
static volatile uint32_t musDurMs = 0;
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
static int mM, mCardX, mCardY, mCardW, mCardH, mBarX, mBarY, mBarW, mCtlY, mCtlH;
static int mPrevX, mPrevW, mPlayX, mPlayW, mNextX, mNextW;
static int mVolY, mVolH, mVBtnW, mVMinX, mVPlusX, mVBarX, mVBarW;
static int mBotY, mBotH, mBotGap, mBotW, mBotX[4];

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
  snprintf(out, 8, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
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
  if (xSemaphoreTake(musMtx, portMAX_DELAY) == pdTRUE) {
    musList.swap(tmp);
    xSemaphoreGive(musMtx);
  }
  int n = (int)musList.size();
  if (musCur >= n) musCur = 0;
  if (musCur < 0) musCur = 0;
  musListTop = 0;
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
static bool musStartTrack(int idx) {
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
  if (sr != 44100) musToast("MP3 bukan 44.1kHz, nada bergeser");
  musFile.seek(0);

  musSeq++;
  uint8_t sq = musSeq;
  musCredit = 0; musSentSince = 0;
  rpSend(Serial1, RP_BEGIN, &sq, 1);

  musCur = idx; musLoaded = true; musPaused = false; musEnded = false;
  musPlayedMs = 0; musStreaming = true; musFileDone = false;
  musPrefsDirty = true; musPrefsMs = millis();
  return true;
}

static int musNextIdx(int dir, bool autoNext) {
  int n = (int)musList.size();
  if (n == 0) return -1;
  if (autoNext && musRepeat == 2) return musCur;
  if (musShuffle && n > 1) {
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
    case 'p': musStartTrack((int)a); break;
    case 'u':
      if (!musLoaded) musStartTrack(musCur < 0 ? 0 : musCur);
      else { musPaused = !musPaused; musSendPause(musPaused); }
      break;
    case 'n': { int i = musNextIdx(1, false); if (i >= 0) musStartTrack(i); break; }
    case 'b':
      if (musLoaded && musPlayedMs > 3000) musStartTrack(musCur);
      else { int i = musNextIdx(-1, false); if (i >= 0) musStartTrack(i); }
      break;
    case 'v': {
      int v = (int)a; if (v < 0) v = 0; if (v > 127) v = 127;
      uint8_t b = (uint8_t)v;
      rpSend(Serial1, RP_VOLUME, &b, 1);
      break;
    }
    case 's':
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
      if (!P.feed((uint8_t)c)) continue;
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
      int i = musNextIdx(1, true);
      if (i >= 0) musStartTrack(i);
      else { musLoaded = false; musStreaming = false; if (musFile) musFile.close(); }
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
    musShuffle = p.getBool("shuf", false);
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
  static uint32_t lastRd = 0;
  if (onScr && now - lastRd >= ((musScanning || musConnecting) ? 250u : 500u)) {
    lastRd = now; needRedraw = true;
  }
  if (musPrefsDirty && now - musPrefsMs > 1500) {
    musPrefsDirty = false;
    Preferences p; p.begin("music", false);
    p.putInt("vol", musVol); p.putInt("rep", musRepeat);
    p.putBool("shuf", musShuffle); p.putInt("cur", musCur);
    p.end();
  }
}

// =====================================================================
//  UI
// =====================================================================
static void musCalcLayout() {
  int W = SCR_W;
  mM = 8;
  mCardX = mM; mCardY = 38; mCardW = W - 2 * mM; mCardH = 38;
  mBarX = mM + 2; mBarY = 84; mBarW = W - 2 * mM - 4;
  mCtlY = 106; mCtlH = 36;
  int gap = 8, avail = W - 2 * mM - 2 * gap;
  mPrevW = avail * 28 / 100; mNextW = mPrevW; mPlayW = avail - 2 * mPrevW;
  mPrevX = mM; mPlayX = mPrevX + mPrevW + gap; mNextX = mPlayX + mPlayW + gap;
  mVolY = 150; mVolH = 24; mVBtnW = 40;
  mVMinX = mM; mVPlusX = W - mM - mVBtnW;
  mVBarX = mVMinX + mVBtnW + 10; mVBarW = mVPlusX - 10 - mVBarX;
  mBotH = 26; mBotY = SCR_H - BACK_H - 3 - 6 - mBotH;
  mBotGap = 6; mBotW = (W - 2 * mM - 3 * mBotGap) / 4;
  for (int i = 0; i < 4; i++) mBotX[i] = mM + i * (mBotW + mBotGap);
}

static bool musHit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

static int musListRows() { int r = (mBotY - 8 - 38) / 28; return r < 1 ? 1 : r; }
static int musScanRows() { int r = (mBotY - 8 - 52) / 24; return r < 1 ? 1 : r; }

// kind: 0 prev, 1 next, 2 play, 3 pause
static void musDrawIcon(LGFX_Sprite& s, int kind, int cx, int cy, uint16_t c) {
  switch (kind) {
    case 0: s.fillRect(cx - 9, cy - 8, 3, 16, c); s.fillTriangle(cx + 8, cy - 8, cx + 8, cy + 8, cx - 6, cy, c); break;
    case 1: s.fillRect(cx + 6, cy - 8, 3, 16, c); s.fillTriangle(cx - 8, cy - 8, cx - 8, cy + 8, cx + 6, cy, c); break;
    case 2: s.fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 9, cy, c); break;
    default: s.fillRect(cx - 7, cy - 9, 5, 18, c); s.fillRect(cx + 2, cy - 9, 5, 18, c); break;
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

static void musDrawPlayer(LGFX_Sprite& s) {
  // kartu judul (teks gulir kalau kepanjangan)
  drawGlassPanel(s, mCardX, mCardY, mCardW, mCardH, 10, T().surface, 160);
  char title[64];
  musGetTitle(title, sizeof(title), musCur);
  s.setTextSize(2); s.setTextColor(T().text);
  int tw = s.textWidth(title);
  int ix = mCardX + 8, iw = mCardW - 16;
  s.setClipRect(ix, mCardY + 2, iw, mCardH - 4);
  if (tw <= iw) {
    s.setCursor(ix + (iw - tw) / 2, mCardY + 5); s.print(title);
  } else {
    int span = tw + 40;
    int off = (int)((millis() / 25) % (uint32_t)span);
    s.setCursor(ix - off, mCardY + 5);        s.print(title);
    s.setCursor(ix - off + span, mCardY + 5); s.print(title);
  }
  s.clearClipRect();
  s.setTextSize(1); s.setTextColor(T().subtext);
  char sub[32]; snprintf(sub, sizeof(sub), "%d / %d", musCur + 1, musListCount());
  int sw = s.textWidth(sub);
  s.setCursor(mCardX + mCardW / 2 - sw / 2, mCardY + 25); s.print(sub);

  // progress + waktu
  s.fillRoundRect(mBarX, mBarY, mBarW, 5, 2, T().divider);
  uint32_t dur = musDurMs;
  float pr = (dur > 0) ? (float)musPlayedMs / (float)dur : 0.f;
  if (pr > 1.f) pr = 1.f;
  int fw = (int)(pr * mBarW);
  if (musLoaded && fw > 0) s.fillRoundRect(mBarX, mBarY, fw, 5, 2, T().accent);
  char t1[8], t2[8];
  musFmtTime(t1, musLoaded ? musPlayedMs : 0);
  if (musLoaded && dur > 0) musFmtTime(t2, dur); else strcpy(t2, "--:--");
  s.setTextColor(T().subtext);
  s.setCursor(mBarX, mBarY + 9); s.print(t1);
  s.setCursor(mBarX + mBarW - s.textWidth(t2), mBarY + 9); s.print(t2);

  // kontrol
  bool playing = musLoaded && !musPaused;
  drawGlassPanel(s, mPrevX, mCtlY, mPrevW, mCtlH, 12, T().surface2, 190);
  musDrawIcon(s, 0, mPrevX + mPrevW / 2, mCtlY + mCtlH / 2, T().text);
  drawGlassPanel(s, mPlayX, mCtlY, mPlayW, mCtlH, 12, T().accent, 210);
  musDrawIcon(s, playing ? 3 : 2, mPlayX + mPlayW / 2, mCtlY + mCtlH / 2, T().bg);
  drawGlassPanel(s, mNextX, mCtlY, mNextW, mCtlH, 12, T().surface2, 190);
  musDrawIcon(s, 1, mNextX + mNextW / 2, mCtlY + mCtlH / 2, T().text);

  // volume
  drawGlassPanel(s, mVMinX, mVolY, mVBtnW, mVolH, 8, T().surface2, 190);
  musDrawLabel(s, "-", mVMinX, mVolY, mVBtnW, mVolH, T().text);
  drawGlassPanel(s, mVPlusX, mVolY, mVBtnW, mVolH, 8, T().surface2, 190);
  musDrawLabel(s, "+", mVPlusX, mVolY, mVBtnW, mVolH, T().text);
  s.fillRoundRect(mVBarX, mVolY + 9, mVBarW, 6, 3, T().divider);
  int vf = (mVBarW * musVol) / 127;
  if (vf > 0) s.fillRoundRect(mVBarX, mVolY + 9, vf, 6, 3, T().accent2);
  s.fillCircle(mVBarX + vf, mVolY + 12, 6, T().text);

  // baris bawah
  const char* repl = (musRepeat == 0) ? "Ulang:-" : (musRepeat == 1 ? "Ulang:Y" : "Ulang:1");
  drawGlassPanel(s, mBotX[0], mBotY, mBotW, mBotH, 8, musShuffle ? T().accent : T().surface2, musShuffle ? 210 : 190);
  musDrawLabel(s, "Acak", mBotX[0], mBotY, mBotW, mBotH, musShuffle ? T().bg : T().subtext);
  drawGlassPanel(s, mBotX[1], mBotY, mBotW, mBotH, 8, musRepeat ? T().accent : T().surface2, musRepeat ? 210 : 190);
  musDrawLabel(s, repl, mBotX[1], mBotY, mBotW, mBotH, musRepeat ? T().bg : T().subtext);
  drawGlassPanel(s, mBotX[2], mBotY, mBotW, mBotH, 8, T().surface2, 190);
  musDrawLabel(s, "Daftar", mBotX[2], mBotY, mBotW, mBotH, T().subtext);
  drawGlassPanel(s, mBotX[3], mBotY, mBotW, mBotH, 8, T().surface2, 190);
  musDrawLabel(s, "TWS", mBotX[3], mBotY, mBotW, mBotH, T().subtext);
}

static void musDrawList(LGFX_Sprite& s) {
  int n = musListCount();
  int rows = musListRows();
  if (musListTop >= n) musListTop = 0;
  int maxc = (SCR_W - 32) / 6;
  if (n == 0) {
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(12, 50); s.print("Tidak ada MP3 di folder /music");
    s.setCursor(12, 64); s.print("(atau di root SD). Taruh file lalu");
    s.setCursor(12, 78); s.print("buka ulang app ini.");
  }
  for (int r = 0; r < rows; r++) {
    int idx = musListTop + r;
    if (idx >= n) break;
    int y = 38 + r * 28;
    bool cur = (idx == musCur);
    drawGlassPanel(s, 8, y, SCR_W - 16, 26, 8, cur ? T().accent : T().surface2, cur ? 200 : 150);
    char nm[64]; musGetTitle(nm, sizeof(nm), idx);
    musTrunc(nm, maxc);
    s.setTextSize(1); s.setTextColor(cur ? T().bg : T().text);
    s.setCursor(16, y + 9); s.print(nm);
  }
  int bw3 = (SCR_W - 16 - 12) / 3;
  drawGlassPanel(s, mM, mBotY, bw3, mBotH, 8, T().surface2, 190);
  musDrawLabel(s, "^ Atas", mM, mBotY, bw3, mBotH, T().text);
  drawGlassPanel(s, mM + bw3 + 6, mBotY, bw3, mBotH, 8, T().surface2, 190);
  musDrawLabel(s, "v Bawah", mM + bw3 + 6, mBotY, bw3, mBotH, T().text);
  drawGlassPanel(s, mM + 2 * (bw3 + 6), mBotY, bw3, mBotH, 8, T().accent, 210);
  musDrawLabel(s, "Pemutar", mM + 2 * (bw3 + 6), mBotY, bw3, mBotH, T().bg);
}

static void musDrawScan(LGFX_Sprite& s) {
  s.setTextSize(1);
  s.setTextColor(T().subtext);
  s.setCursor(8, 38);
  if (!musCamAlive) { s.setTextColor(T().danger); s.print("Modul BT tidak terdeteksi (cek kabel)"); }
  else if (musScanning) {
    int d = (millis() / 300) % 4;
    s.print("Memindai"); for (int i = 0; i < d; i++) s.print(".");
  } else { s.print(musScanCount); s.print(" perangkat. Ketuk untuk menyambung."); }

  int rows = musScanRows();
  int maxc = (SCR_W - 70) / 6;
  for (int r = 0; r < rows && r < musScanCount; r++) {
    int y = 52 + r * 24;
    drawGlassPanel(s, 8, y, SCR_W - 16, 22, 8, T().surface2, 170);
    char nm[40]; strncpy(nm, musScanName[r], sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    musTrunc(nm, maxc);
    s.setTextColor(T().text); s.setCursor(16, y + 7); s.print(nm);
    char rs[8]; snprintf(rs, sizeof(rs), "%d", (int)musScanRssi[r]);
    s.setTextColor(T().subtext); s.setCursor(SCR_W - 16 - s.textWidth(rs), y + 7); s.print(rs);
  }
  int bw2 = (SCR_W - 16 - 6) / 2;
  drawGlassPanel(s, mM, mBotY, bw2, mBotH, 8, T().surface2, 190);
  musDrawLabel(s, "Pindai ulang", mM, mBotY, bw2, mBotH, T().text);
  drawGlassPanel(s, mM + bw2 + 6, mBotY, bw2, mBotH, 8, T().accent, 210);
  musDrawLabel(s, "Pemutar", mM + bw2 + 6, mBotY, bw2, mBotH, T().bg);
}

void drawMusic(LGFX_Sprite& s) {
  musCalcLayout();
  s.fillSprite(T().bg); drawStatusBar(s);
  s.setTextSize(1);
  s.setTextColor(T().accent); s.setCursor(8, 26);
  s.print(musPage == 1 ? "Daftar Lagu" : (musPage == 2 ? "Cari TWS" : "Musik"));

  const char* st; uint16_t col;
  if (!musCamAlive)        { st = "Modul BT mati";     col = T().danger; }
  else if (musBtConn)      { st = "TWS terhubung";     col = T().good; }
  else if (musConnecting)  { st = "Menghubungkan...";  col = T().accent2; }
  else                     { st = "TWS putus";         col = T().subtext; }
  s.setTextColor(col);
  s.setCursor(SCR_W - 8 - s.textWidth(st), 26); s.print(st);

  if (musPage == 1)      musDrawList(s);
  else if (musPage == 2) musDrawScan(s);
  else                   musDrawPlayer(s);

  drawBack(s); drawToast(s);
}

void musicTouch(int x, int y, bool held, bool isNew) {
  if (!isNew) return;
  musCalcLayout();
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
      if (musHit(x, y, 8, 38 + r * 28, SCR_W - 16, 26)) {
        musPost('p', idx, nullptr); musPage = 0; vibTap(); needRedraw = true; return;
      }
    }
    int bw3 = (SCR_W - 16 - 12) / 3;
    if (musHit(x, y, mM, mBotY, bw3, mBotH)) { musListTop -= rows; if (musListTop < 0) musListTop = 0; vibTap(); needRedraw = true; return; }
    if (musHit(x, y, mM + bw3 + 6, mBotY, bw3, mBotH)) { if (musListTop + rows < n) musListTop += rows; vibTap(); needRedraw = true; return; }
    if (musHit(x, y, mM + 2 * (bw3 + 6), mBotY, bw3, mBotH)) { musPage = 0; vibTap(); needRedraw = true; return; }
    return;
  }

  if (musPage == 2) {
    int rows = musScanRows();
    for (int r = 0; r < rows && r < musScanCount; r++) {
      if (musHit(x, y, 8, 52 + r * 24, SCR_W - 16, 22)) {
        musPost('c', 0, musScanName[r]);
        showToast("Menghubungkan..."); musPage = 0; vibTap(); needRedraw = true; return;
      }
    }
    int bw2 = (SCR_W - 16 - 6) / 2;
    if (musHit(x, y, mM, mBotY, bw2, mBotH)) { musPost('a', 0, nullptr); vibTap(); needRedraw = true; return; }
    if (musHit(x, y, mM + bw2 + 6, mBotY, bw2, mBotH)) { musPage = 0; vibTap(); needRedraw = true; return; }
    return;
  }

  // ---- halaman pemutar ----
  if (musHit(x, y, mPrevX, mCtlY, mPrevW, mCtlH)) { musPost('b', 0, nullptr); vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mPlayX, mCtlY, mPlayW, mCtlH)) { musPost('u', 0, nullptr); vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mNextX, mCtlY, mNextW, mCtlH)) { musPost('n', 0, nullptr); vibTap(); needRedraw = true; return; }

  if (musHit(x, y, mVMinX, mVolY, mVBtnW, mVolH)) {
    int v = musVol - MUS_VOL_STEP; if (v < 0) v = 0;
    musVol = v; musPost('v', v, nullptr); musPrefsDirty = true; musPrefsMs = millis(); vibTap(); needRedraw = true; return;
  }
  if (musHit(x, y, mVPlusX, mVolY, mVBtnW, mVolH)) {
    int v = musVol + MUS_VOL_STEP; if (v > 127) v = 127;
    musVol = v; musPost('v', v, nullptr); musPrefsDirty = true; musPrefsMs = millis(); vibTap(); needRedraw = true; return;
  }
  if (musHit(x, y, mVBarX, mVolY, mVBarW, mVolH)) {
    int v = ((x - mVBarX) * 127) / (mVBarW > 0 ? mVBarW : 1);
    if (v < 0) v = 0; if (v > 127) v = 127;
    musVol = v; musPost('v', v, nullptr); musPrefsDirty = true; musPrefsMs = millis(); needRedraw = true; return;
  }

  if (musHit(x, y, mBotX[0], mBotY, mBotW, mBotH)) { musShuffle = !musShuffle; musPrefsDirty = true; musPrefsMs = millis(); vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mBotX[1], mBotY, mBotW, mBotH)) { musRepeat = (musRepeat + 1) % 3; musPrefsDirty = true; musPrefsMs = millis(); vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mBotX[2], mBotY, mBotW, mBotH)) { musPage = 1; int r = musListRows(); musListTop = (musCur / r) * r; vibTap(); needRedraw = true; return; }
  if (musHit(x, y, mBotX[3], mBotY, mBotW, mBotH)) { musPage = 2; musPost('a', 0, nullptr); vibTap(); needRedraw = true; return; }
}
