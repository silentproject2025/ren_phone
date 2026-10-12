// =====================================================================
//  avi_renphone.ino -- Pemutar AVI (video MJPEG + audio MP3) NyxOS / Ren Phone
//
//  Video : MJPEG di-decode JPEGDEC -> layar (sama spt pemutar .mjpeg lama).
//  Audio : byte MP3 dari AVI dikirim ke ESP32-CAM lewat jalur "audio eksternal"
//          di musicbt_renphone.ino (musExt*) -> CAM decode -> A2DP ke TWS.
//  Sinkron: JAM UTAMA = posisi putar audio di CAM (musExtClockMs), dikurangi
//          AVI_BT_LATENCY_MS. Frame video yg telat DILEWATI (bukan diperlambat),
//          jadi suara tidak pernah tertinggal. Tanpa audio / audio gagal -> jam
//          dinding (millis).
//
//  Format yg didukung (cara bikin: lihat README, bagian "Pemutar AVI"):
//    video MJPEG (fourcc MJPG), audio MP3 44.1 kHz (wFormatTag 0x0055)
//    ffmpeg -i in.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
//           -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k out.avi
//
//  Alur memori: antrean frame terkompresi di PSRAM (baca-di-depan, ~2-3 dtk)
//  supaya audio selalu punya cadangan di CAM walau decode JPEG sesekali telat.
//
//  JANGAN pakai tipe buatan sendiri di signature fungsi (auto-prototype
//  arduino-cli disisipkan sebelum tipe dikenal) -- makanya semua state global.
// =====================================================================
#include <new>
#include <esp_heap_caps.h>

// ---- API audio eksternal (definisi: musicbt_renphone.ino; file ini dikompilasi LEBIH DULU) ----
int      musExtBegin();
uint32_t musExtFree();
uint32_t musExtWrite(const uint8_t* d, uint32_t n);
void     musExtFinishStream();
bool     musExtActive();
bool     musExtEnded();
uint32_t musExtClockMs();
void     musExtPause(bool p);
void     musExtEnd();

// Jeda CAM -> TWS (buffer A2DP + decode earbuds). Video terlihat DULUAN dari suara -> naikkan;
// suara duluan dari video -> turunkan. Satuan ms.
#define AVI_BT_LATENCY_MS   160
#define AVI_SLOTS_MAX       40          // jumlah slot antrean frame (dicoba turun kalau PSRAM kurang)
#define AVI_AUD_CHUNK       2048
#define AVI_MIN_AUD_FREE    8192        // baca chunk baru hanya kalau ring audio punya >= ini

// ---- metadata file ----
static File     aviFile;
static uint32_t aviFileSize = 0;
static uint32_t aviPos = 0;             // posisi header chunk berikutnya (logis)
static uint32_t aviRd  = 0;             // posisi baca file yg sebenarnya
static uint32_t aviMoviEnd = 0;
static bool     aviEof = false;
static uint32_t aviUsPerFrame = 66667;
static uint32_t aviW = 0, aviH = 0, aviTotalFrames = 0;
static int      aviVidStream = -1, aviAudStream = -1;
static uint16_t aviAudFmt = 0, aviAudCh = 0;
static uint32_t aviAudRate = 0;
static bool     aviVidIsMjpg = false;
static bool     aviAudioOn = false;     // audio benar2 dikirim ke CAM

// ---- antrean frame video (PSRAM, slot ukuran tetap) ----
static uint8_t* aviVq = nullptr;
static uint32_t aviSlotSize = 0;
static int      aviSlotN = 0;
static uint32_t aviVqSize[AVI_SLOTS_MAX];
static uint32_t aviVqIdx[AVI_SLOTS_MAX];
static int      aviVqHead = 0, aviVqCount = 0;
static uint32_t aviFrameNo = 0;         // nomor frame yg sudah dibaca dari file
static uint8_t* aviTmp = nullptr;       // buffer sementara audio
static JPEGDEC* aviJpeg = nullptr;
static void*    aviJpegMem = nullptr;

// ---- decode / layar ----
static bool     aviScaleReady = false;
static int      aviScaleOpt = 0, aviOffX = 0, aviOffY = 0, aviMaxMcus = 20;
static uint32_t aviShown = 0, aviDropped = 0, aviBigSkipped = 0;

// ---- jam ----
static bool          aviAudioClk = false;
static unsigned long aviT0 = 0, aviPausedAccum = 0;

// =====================================================================
//  Util baca little-endian
// =====================================================================
static uint32_t aviLE32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t aviLE16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static bool aviIs(const uint8_t* p, const char* s) {
  return memcmp(p, s, 4) == 0;
}

// baca/seek dgn pelacakan posisi supaya seek hanya dilakukan kalau perlu
static int aviRead(uint8_t* buf, uint32_t n) {
  int r = (int)aviFile.read(buf, n);
  if (r > 0) aviRd += (uint32_t)r;
  return r;
}
static void aviSeekTo(uint32_t pos) {
  if (pos == aviRd) return;
  aviFile.seek(pos);
  aviRd = pos;
}

// =====================================================================
//  PARSER HEADER AVI
// =====================================================================
static bool aviParseStrl(uint32_t start, uint32_t end, int streamNo) {
  uint32_t pos = start;
  bool isVid = false, isAud = false;
  uint32_t scale = 0, rate = 0;
  uint8_t h[8], b[64];
  while (pos + 8 <= end) {
    aviSeekTo(pos);
    if (aviRead(h, 8) != 8) return false;
    uint32_t sz = aviLE32(h + 4);
    if (aviIs(h, "strh") && sz >= 28) {
      uint32_t n = sz < sizeof(b) ? sz : sizeof(b);
      if (aviRead(b, n) != (int)n) return false;
      isVid = aviIs(b, "vids");
      isAud = aviIs(b, "auds");
      scale = aviLE32(b + 20);
      rate  = aviLE32(b + 24);
    } else if (aviIs(h, "strf")) {
      uint32_t n = sz < 40 ? sz : 40;
      if (aviRead(b, n) != (int)n) return false;
      if (isVid && aviVidStream < 0 && n >= 20) {
        aviVidStream = streamNo;
        int32_t bh = (int32_t)aviLE32(b + 8);
        if (bh < 0) bh = -bh;
        aviW = aviLE32(b + 4);
        aviH = (uint32_t)bh;
        // biCompression: MJPG (huruf besar/kecil)
        aviVidIsMjpg = (memcmp(b + 16, "MJPG", 4) == 0) || (memcmp(b + 16, "mjpg", 4) == 0);
        if (scale && rate) aviUsPerFrame = (uint32_t)(((uint64_t)scale * 1000000ULL) / rate);
      } else if (isAud && aviAudStream < 0 && n >= 8) {
        aviAudStream = streamNo;
        aviAudFmt  = aviLE16(b);
        aviAudCh   = aviLE16(b + 2);
        aviAudRate = aviLE32(b + 4);
      }
    }
    pos += 8 + sz + (sz & 1);
  }
  return true;
}

static bool aviParseHdrl(uint32_t start, uint32_t end) {
  uint32_t pos = start;
  int stream = 0;
  uint8_t h[8], b[64];
  while (pos + 8 <= end) {
    aviSeekTo(pos);
    if (aviRead(h, 8) != 8) return false;
    uint32_t sz = aviLE32(h + 4);
    if (aviIs(h, "avih") && sz >= 40) {
      uint32_t n = sz < sizeof(b) ? sz : sizeof(b);
      if (aviRead(b, n) != (int)n) return false;
      uint32_t us = aviLE32(b);
      if (us > 0) aviUsPerFrame = us;
      aviTotalFrames = aviLE32(b + 16);
      if (aviW == 0) aviW = aviLE32(b + 32);
      if (aviH == 0) aviH = aviLE32(b + 36);
    } else if (aviIs(h, "LIST")) {
      uint8_t t[4];
      if (aviRead(t, 4) != 4) return false;
      if (aviIs(t, "strl")) {
        if (!aviParseStrl(pos + 12, pos + 8 + sz, stream)) return false;
        stream++;
      }
    }
    pos += 8 + sz + (sz & 1);
  }
  return true;
}

// return nullptr kalau OK, selain itu pesan error singkat (utk toast)
static const char* aviParseFile() {
  uint8_t h[12];
  aviPos = 0; aviRd = 0;
  aviSeekTo(0);
  if (aviRead(h, 12) != 12) return "File AVI rusak";
  if (!aviIs(h, "RIFF") || memcmp(h + 8, "AVI ", 4) != 0) return "Bukan file AVI";

  aviVidStream = -1; aviAudStream = -1; aviVidIsMjpg = false;
  aviW = aviH = 0; aviTotalFrames = 0; aviUsPerFrame = 66667;
  aviAudFmt = 0; aviAudCh = 0; aviAudRate = 0;

  bool gotHdrl = false, gotMovi = false;
  uint32_t pos = 12;
  while (!gotMovi && pos + 12 <= aviFileSize) {
    aviSeekTo(pos);
    if (aviRead(h, 12) != 12) break;
    uint32_t sz = aviLE32(h + 4);
    if (aviIs(h, "LIST")) {
      if (aviIs(h + 8, "hdrl")) {
        if (!aviParseHdrl(pos + 12, pos + 8 + sz)) return "Header AVI rusak";
        gotHdrl = true;
      } else if (aviIs(h + 8, "movi")) {
        aviPos = pos + 12;
        aviMoviEnd = pos + 8 + sz;
        if (aviMoviEnd > aviFileSize || aviMoviEnd < aviPos) aviMoviEnd = aviFileSize;
        gotMovi = true;
        break;
      }
    }
    pos += 8 + sz + (sz & 1);
  }
  if (!gotHdrl || !gotMovi) return "Struktur AVI tdk dikenal";
  if (aviVidStream < 0)     return "AVI tanpa video";
  if (!aviVidIsMjpg)        return "Video harus MJPEG (MJPG)";
  if (aviW == 0 || aviH == 0 || aviW > 1024 || aviH > 1024) return "Resolusi video tdk valid";
  aviSeekTo(aviPos);
  aviEof = false;
  return nullptr;
}

// =====================================================================
//  DEMUX: baca 1 chunk dari file -> antrean video / ring audio
//  return: 1 = ada progres | 0 = tertahan (antrean/ring penuh) | -1 = EOF
// =====================================================================
static int aviPump() {
  if (aviEof) return -1;
  if (aviVqCount >= aviSlotN) return 0;
  if (aviAudioOn && musExtFree() < AVI_MIN_AUD_FREE) return 0;
  if (aviPos + 8 > aviMoviEnd) { aviEof = true; return -1; }

  aviSeekTo(aviPos);
  uint8_t hd[8];
  if (aviRead(hd, 8) != 8) { aviEof = true; return -1; }
  uint32_t sz = aviLE32(hd + 4);
  uint32_t body = aviPos + 8;

  if (aviIs(hd, "LIST")) {            // mis. 'rec ' di dalam movi: masuk ke dalamnya
    aviPos = body + 4;
    return 1;
  }
  if (aviIs(hd, "idx1")) { aviEof = true; return -1; }
  if (body + sz > aviFileSize) { aviEof = true; return -1; }   // chunk terpotong

  uint32_t next = body + sz + (sz & 1);
  bool digits = (hd[0] >= '0' && hd[0] <= '9' && hd[1] >= '0' && hd[1] <= '9');
  int sn = digits ? ((hd[0] - '0') * 10 + (hd[1] - '0')) : -1;

  if (sn == aviVidStream && hd[2] == 'd') {
    int slot = (aviVqHead + aviVqCount) % aviSlotN;
    uint32_t keep = 0;
    if (sz > 0 && sz <= aviSlotSize) {
      if (aviRead(aviVq + (size_t)slot * aviSlotSize, sz) == (int)sz) keep = sz;
      else { aviEof = true; return -1; }
    } else if (sz > aviSlotSize) {
      aviBigSkipped++;                // frame kebesaran utk slot: dilewati (ulang frame sebelumnya)
    }
    aviVqSize[slot] = keep;
    aviVqIdx[slot]  = aviFrameNo++;
    aviVqCount++;
  } else if (sn == aviAudStream && hd[2] == 'w' && aviAudioOn) {
    uint32_t done = 0;
    unsigned long t0 = millis();
    while (done < sz && aviAudioOn) {
      uint32_t n = sz - done;
      if (n > AVI_AUD_CHUNK) n = AVI_AUD_CHUNK;
      int r = aviRead(aviTmp, n);
      if (r <= 0) { aviEof = true; return -1; }
      uint32_t w = 0;
      while (w < (uint32_t)r) {
        uint32_t a = musExtWrite(aviTmp + w, (uint32_t)r - w);
        w += a;
        if (a == 0) {
          if (!musExtActive() || millis() - t0 > 3000) { aviAudioOn = false; break; }   // CAM mati / macet
          delay(2);
        }
      }
      done += (uint32_t)r;
    }
  }
  aviPos = next;
  return 1;
}

// =====================================================================
//  DECODE & TAMPIL 1 FRAME
// =====================================================================
static void aviComputeScale(int dispW, int dispH) {
  float ratio = (float)aviH / (float)dispH;
  float rw = (float)aviW / (float)dispW;
  if (rw > ratio) ratio = rw;
  int shift = 0;
  if (ratio <= 1.0f)      { aviScaleOpt = 0;                shift = 0; aviMaxMcus = dispW / 16; }
  else if (ratio <= 2.0f) { aviScaleOpt = JPEG_SCALE_HALF;    shift = 1; aviMaxMcus = dispW / 8; }
  else if (ratio <= 4.0f) { aviScaleOpt = JPEG_SCALE_QUARTER; shift = 2; aviMaxMcus = dispW / 4; }
  else                    { aviScaleOpt = JPEG_SCALE_EIGHTH;  shift = 3; aviMaxMcus = dispW / 2; }
  if (aviMaxMcus < 1) aviMaxMcus = 1;
  int sw = (int)(aviW >> shift), sh = (int)(aviH >> shift);
  aviOffX = (dispW > sw) ? (dispW - sw) / 2 : 0;
  aviOffY = (dispH > sh) ? (dispH - sh) / 2 : 0;
  aviScaleReady = true;
}

static bool aviShowFrame(const uint8_t* data, uint32_t size) {
  if (!aviJpeg) return false;
  if (!aviJpeg->openRAM((uint8_t*)data, (int)size, mjpegDrawCallback)) return false;
  if (!aviScaleReady) aviComputeScale(display.width(), display.height());
  aviJpeg->setPixelType(RGB565_BIG_ENDIAN);
  aviJpeg->setMaxOutputSize(aviMaxMcus);
  int rc = aviJpeg->decode(aviOffX, aviOffY, aviScaleOpt);
  aviJpeg->close();
  return rc != 0;
}

// =====================================================================
//  JAM, SENTUHAN, OVERLAY
// =====================================================================
static uint32_t aviNowMs() {
  if (aviAudioClk) {
    uint32_t c = musExtClockMs();
    return c > AVI_BT_LATENCY_MS ? c - AVI_BT_LATENCY_MS : 0;
  }
  return (uint32_t)(millis() - aviT0 - aviPausedAccum);
}

// 0 = ketuk (pause/lanjut) | 1 = tahan (berhenti) | 2 = usap tepi bawah ke atas (Home)
static int aviTouchGesture(int startY) {
  bool nearBottom = startY > display.height() - 40;
  unsigned long t0 = millis();
  lgfx::touch_point_t tp;
  tp.x = 0; tp.y = startY;
  while (getInputPoint(tp)) {
    if (nearBottom && (startY - (int)tp.y) > 28) {
      while (getInputPoint(tp)) delay(10);     // tunggu jari benar2 diangkat
      return 2;
    }
    if (millis() - t0 >= 600) return 1;
    delay(10);
  }
  return 0;
}

static void aviDrawPauseOverlay(uint32_t ms) {
  uint32_t sec = ms / 1000, tot = (uint32_t)(((uint64_t)aviTotalFrames * aviUsPerFrame) / 1000000ULL);
  int W = display.width(), H = display.height();
  int bx = (W - 200) / 2, by = (H - 60) / 2;
  display.fillRect(bx, by, 200, 60, TFT_BLACK);
  display.drawRect(bx, by, 200, 60, TFT_WHITE);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(bx + 20, by + 8);
  if (tot > 0) display.printf("%02u:%02u/%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60), (unsigned)(tot / 60), (unsigned)(tot % 60));
  else         display.printf("%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
  display.setTextSize(1);
  display.setCursor(bx + 10, by + 38);
  display.print("PAUSED - ketuk lanjut");
}

static void aviFreeAll() {
  if (aviJpeg) { aviJpeg->~JPEGDEC(); aviJpeg = nullptr; }
  if (aviJpegMem) { heap_caps_free(aviJpegMem); aviJpegMem = nullptr; }
  if (aviVq)  { heap_caps_free(aviVq);  aviVq = nullptr; }
  if (aviTmp) { heap_caps_free(aviTmp); aviTmp = nullptr; }
  aviSlotN = 0; aviVqCount = 0; aviVqHead = 0;
}

static bool aviAllocAll() {
  // objek JPEGDEC: RAM internal dulu (lebih cepat), PSRAM sbg cadangan
  aviJpegMem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!aviJpegMem) aviJpegMem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!aviJpegMem) return false;
  aviJpeg = new (aviJpegMem) JPEGDEC();

  aviTmp = (uint8_t*)heap_caps_malloc(AVI_AUD_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!aviTmp) aviTmp = (uint8_t*)heap_caps_malloc(AVI_AUD_CHUNK, MALLOC_CAP_8BIT);
  if (!aviTmp) return false;

  uint32_t slot = (aviW * aviH) / 2;               // 320x240 -> ~38 KB per slot (frame q~7 cuma ~8-15 KB)
  if (slot < 16384) slot = 16384;
  if (slot > 98304) slot = 98304;
  aviSlotSize = slot;
  const int cand[] = { AVI_SLOTS_MAX, 28, 20, 12, 8 };
  for (int i = 0; i < 5; i++) {
    aviVq = (uint8_t*)heap_caps_malloc((size_t)cand[i] * slot, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (aviVq) { aviSlotN = cand[i]; break; }
  }
  return aviVq != nullptr;
}

// =====================================================================
//  PEMUTAR (blocking, pola sama dgn mjpegPlayFile)
//  Ketuk = pause/lanjut | Tahan = berhenti | Usap tepi bawah ke atas = Home
// =====================================================================
void aviPlayFile(const String& filename) {
  if (!sdReady) { showToast("SD Card tdk siap"); return; }
  String fullPath = String("/") + filename;      // sama spt mjpeg: nama relatif dari root SD
  aviFile = SD_MMC.open(fullPath, FILE_READ);
  if (!aviFile || aviFile.isDirectory()) {
    if (aviFile) aviFile.close();
    showToast("Gagal membuka file AVI");
    return;
  }
  aviFileSize = (uint32_t)aviFile.size();

  const char* perr = aviParseFile();
  if (perr) { aviFile.close(); showToast(perr); return; }
  if (!aviAllocAll()) {
    aviFreeAll(); aviFile.close();
    showToast("RAM tidak cukup utk AVI");
    return;
  }

  // Rotasi mengikuti bentuk video (lebar>=tinggi -> landscape), dikembalikan setelah selesai.
  int prevRotation = display.getRotation();
  display.setRotation(aviW >= aviH ? 1 : 0);
  aviScaleReady = false;
  aviShown = aviDropped = aviBigSkipped = 0;
  aviFrameNo = 0; aviVqHead = 0; aviVqCount = 0;
  aviPausedAccum = 0;

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.print("Memutar: "); display.println(filename);
  display.setCursor(10, 26);
  display.print("Ketuk: pause | Tahan: berhenti");
  display.setCursor(10, 42);
  display.print("Usap tepi bawah ke atas: Home");

  // ---- audio -> TWS ----
  const char* audMsg = nullptr;
  aviAudioOn = false;
  if (aviAudStream >= 0) {
    if (aviAudFmt != 0x0055) {
      audMsg = "Audio bukan MP3: tanpa suara";
    } else {
      display.setCursor(10, 62);
      display.print("Menyambung audio ke TWS...");
      int rc = musExtBegin();
      if (rc == 0) aviAudioOn = true;
      else if (rc == 1) audMsg = "Bluetooth mati: tanpa suara";
      else if (rc == 2) audMsg = "Modul BT tdk terdeteksi";
      else if (rc == 3) audMsg = "TWS belum terhubung";
      else              audMsg = "RAM audio kurang";
    }
  }
  if (audMsg) { display.setCursor(10, 62); display.fillRect(10, 62, 220, 10, TFT_BLACK); display.print(audMsg); }

  // ---- isi antrean dulu (~1 dtk video), audio ikut mengalir ke CAM ----
  int fps = (int)(1000000UL / (aviUsPerFrame ? aviUsPerFrame : 66667));
  int want = fps < 4 ? 4 : fps;
  if (want > aviSlotN - 2) want = aviSlotN - 2;
  unsigned long tp0 = millis();
  while (!aviEof && aviVqCount < want && millis() - tp0 < 4000) {
    if (aviPump() == 0) break;
  }

  // ---- tunggu CAM benar2 mulai memutar (jam audio hidup) ----
  aviAudioClk = false;
  if (aviAudioOn) {
    unsigned long tw = millis();
    while (millis() - tw < 4000 && musExtClockMs() == 0) {
      aviPump();
      delay(5);
    }
    if (musExtClockMs() > 0) aviAudioClk = true;
    else { aviAudioOn = false; musExtEnd(); audMsg = "Audio tdk mulai: tanpa suara"; }
  }
  if (audMsg) showToast(audMsg);
  display.fillScreen(TFT_BLACK);
  aviT0 = millis();

  bool stopReq = false, goHome = false, finSignaled = false;
  uint32_t lastVt = 0;
  unsigned long lastTouchChk = 0;
  unsigned long playT0 = millis();

  while (!stopReq) {
    // 1. demux selama <=6 ms (sisa waktu buat decode)
    unsigned long tb = millis();
    while (millis() - tb < 6) { if (aviPump() <= 0) break; }

    if (aviEof && aviAudioClk && !finSignaled) { musExtFinishStream(); finSignaled = true; }

    // 2. jam: audio putus / habis lebih dulu -> lanjut dgn jam dinding dari posisi terakhir
    if (aviAudioClk && !musExtActive()) {
      aviAudioClk = false;
      aviT0 = millis() - lastVt; aviPausedAccum = 0;
      if (!finSignaled) showToast("Audio putus");
    }
    uint32_t vt = aviNowMs();
    if (aviAudioClk) lastVt = vt;
    uint32_t target = (uint32_t)(((uint64_t)vt * 1000ULL) / (aviUsPerFrame ? aviUsPerFrame : 66667));

    // 3. pilih frame: lewati yg sudah basi, tampilkan yg jatuh tempo
    while (aviVqCount >= 2 && aviVqIdx[(aviVqHead + 1) % aviSlotN] <= target) {
      aviVqHead = (aviVqHead + 1) % aviSlotN; aviVqCount--; aviDropped++;
    }
    if (aviVqCount >= 1 && aviVqIdx[aviVqHead] <= target) {
      int sl = aviVqHead;
      if (aviVqSize[sl] > 0) {
        if (aviShowFrame(aviVq + (size_t)sl * aviSlotSize, aviVqSize[sl])) aviShown++;
        else aviDropped++;
      }
      aviVqHead = (aviVqHead + 1) % aviSlotN; aviVqCount--;
    } else if (aviEof && aviVqCount == 0) {
      break;                                       // video habis
    } else {
      delay(1);
    }

    // 4. sentuhan
    if (millis() - lastTouchChk >= 70) {
      lastTouchChk = millis();
      lgfx::touch_point_t tp;
      if (getInputPoint(tp)) {
        int g = aviTouchGesture((int)tp.y);
        if (g == 2)      { stopReq = true; goHome = true; }
        else if (g == 1) { stopReq = true; }
        else {                                     // ketuk -> PAUSE
          unsigned long pauseStart = millis();
          if (aviAudioClk) musExtPause(true);
          aviDrawPauseOverlay(vt);
          bool resumed = false;
          while (!resumed && !stopReq) {
            lgfx::touch_point_t tp2;
            if (getInputPoint(tp2)) {
              int g2 = aviTouchGesture((int)tp2.y);
              if (g2 == 2)      { stopReq = true; goHome = true; }
              else if (g2 == 1) { stopReq = true; }
              else              { resumed = true; }
            }
            if (wifiConnected && webServerRunning) webServer.handleClient();
            delay(5);
          }
          if (aviAudioClk) musExtPause(false);
          aviPausedAccum += millis() - pauseStart;
          if (!stopReq) {
            display.fillScreen(TFT_BLACK);
            lastTouchChk = millis();
          }
        }
      }
    }

    if (wifiConnected && webServerRunning) webServer.handleClient();
    yield();
  }

  // ---- video habis: tunggu sisa audio yg masih tertahan di buffer CAM ----
  if (!stopReq && aviAudioClk) {
    if (!finSignaled) musExtFinishStream();
    unsigned long te = millis();
    while (!stopReq && musExtActive() && !musExtEnded() && millis() - te < 12000) {
      lgfx::touch_point_t tp;
      if (getInputPoint(tp)) {
        int g = aviTouchGesture((int)tp.y);
        if (g == 2) { stopReq = true; goHome = true; }
        else        { stopReq = true; }
      }
      if (wifiConnected && webServerRunning) webServer.handleClient();
      delay(20);
    }
  }

  if (aviAudioOn || aviAudioClk) musExtEnd();     // idempotent; juga membersihkan buffer CAM kalau berhenti dini
  aviAudioOn = false; aviAudioClk = false;
  aviFile.close();
  aviFreeAll();
  display.setRotation(prevRotation);

  unsigned long used = millis() - playT0 - aviPausedAccum;
  Serial.printf("[AVI] Selesai '%s' | tampil=%lu lewat=%lu kebesaran=%lu | waktu=%lums | fps=%.1f\n",
                filename.c_str(), (unsigned long)aviShown, (unsigned long)aviDropped,
                (unsigned long)aviBigSkipped, used, used ? (1000.0f * aviShown / used) : 0.0f);

  if (goHome) navGoHome();
  else        needRedrawNow();
  if (aviBigSkipped > 0) showToast("Frame kebesaran: turunkan -q:v");   // setelah redraw supaya toast kelihatan
}
