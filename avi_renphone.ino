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
//  SEEK + RESUME:
//    - Indeks idx1 di akhir file AVI dibaca sekali saat dibuka -> tabel titik
//      lompat tiap ~1 dtk (offset file + posisi audio). MJPEG = tiap frame
//      keyframe, jadi lompat bisa ke titik mana saja tanpa artefak.
//    - Usap kanan/kiri = +/-10 dtk | saat PAUSE: geser bar progres utk loncat.
//    - Posisi terakhir (menit:detik) disimpan di NVS (namespace "avires", 8 video
//      terakhir). Buka video yg sama lagi -> ditanya Lanjut / Awal.
//    - Audio di-restart di CAM tiap seek (musExtEnd + musExtBegin) dan jam audio
//      diberi offset awal (aviAudOriginMs) supaya sinkron tetap benar.
//    - File tanpa idx1: video tetap main normal, seek/resume dimatikan.
//
//  Alur memori: antrean frame terkompresi di PSRAM (baca-di-depan, ~2-3 dtk)
//  supaya audio selalu punya cadangan di CAM walau decode JPEG sesekali telat.
//
//  JANGAN pakai tipe buatan sendiri di signature fungsi (auto-prototype
//  arduino-cli disisipkan sebelum tipe dikenal) -- makanya semua state global.
// =====================================================================
#include <new>
#include <esp_heap_caps.h>
#include <Preferences.h>

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
#define AVI_SEEK_STEP_MS    10000       // lompatan usap kanan/kiri
#define AVI_CK_MAX          8192        // maks titik lompat di tabel (3 x 32 KB PSRAM)
#define AVI_RESUME_MIN_MS   5000        // < ini dari awal: tdk disimpan
#define AVI_RESUME_TAIL_MS  5000        // sisa < ini dianggap sudah tamat: tdk disimpan
#define AVI_RESUME_SAVE_MS  30000       // simpan posisi berkala saat memutar
#define AVI_RESUME_SLOTS    8           // jumlah video yg diingat posisinya

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
static uint32_t aviAudBps = 0;          // nAvgBytesPerSec audio (utk hitung waktu audio di titik lompat)
static uint32_t aviMoviTag = 0;         // posisi tag 'movi' (acuan offset idx1)
static uint32_t aviMoviFirst = 0;       // posisi chunk pertama di movi

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

// ---- seek / resume ----
static uint32_t* aviCkOff = nullptr;    // [i] = posisi file header chunk video ke (i*aviCkStep)
static uint32_t* aviCkAud = nullptr;    // [i] = waktu audio (ms) chunk audio pertama setelah titik itu
static int       aviCkN = 0;            // 0 = tdk ada indeks -> seek/resume mati
static uint32_t  aviCkStep = 15;        // frame per titik lompat
static uint32_t  aviIdxFrames = 0;      // jumlah frame video menurut idx1
static uint32_t  aviAudOriginMs = 0;    // waktu audio saat stream CAM mulai (setelah seek)
static uint32_t  aviWallOriginMs = 0;   // waktu video di titik mulai (utk jam dinding)
static uint32_t  aviCurMs = 0;          // posisi putar terakhir (ms)
static bool      aviFinSig = false;     // musExtFinishStream sudah dikirim
static uint32_t  aviKey = 0;            // kunci resume (hash nama+ukuran file)
static unsigned long aviOsdUntil = 0;
static bool      aviOsdOn = false;
static char      aviOsdTxt[24];
static int       aviOvX = 0, aviOvY = 0, aviOvW = 0, aviOvH = 0;   // kotak overlay pause

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
        aviAudBps  = (n >= 12) ? aviLE32(b + 8) : 0;
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
  aviAudFmt = 0; aviAudCh = 0; aviAudRate = 0; aviAudBps = 0;

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
        aviMoviTag = pos + 8;
        aviMoviFirst = aviPos;
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
    uint32_t cc = c > AVI_BT_LATENCY_MS ? c - AVI_BT_LATENCY_MS : 0;
    return aviAudOriginMs + cc;            // origin != 0 setelah seek / resume
  }
  return (uint32_t)(millis() - aviT0 - aviPausedAccum);
}

static uint32_t aviDurMs() {
  uint32_t fr = aviIdxFrames ? aviIdxFrames : aviTotalFrames;
  return (uint32_t)(((uint64_t)fr * aviUsPerFrame) / 1000ULL);
}

// 0 = ketuk (pause/lanjut) | 1 = tahan (berhenti) | 2 = usap tepi bawah ke atas (Home)
// 3 = usap ke kanan (+10 dtk) | 4 = usap ke kiri (-10 dtk)
static int aviTouchGesture(int startX, int startY) {
  bool nearBottom = startY > display.height() - 40;
  unsigned long t0 = millis();
  lgfx::touch_point_t tp;
  tp.x = startX; tp.y = startY;
  while (getInputPoint(tp)) {
    int dx = (int)tp.x - startX, dy = (int)tp.y - startY;
    if (nearBottom && (-dy) > 28) {
      while (getInputPoint(tp)) delay(10);     // tunggu jari benar2 diangkat
      return 2;
    }
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx > 40 && adx > ady * 2) {
      while (getInputPoint(tp)) delay(10);
      return dx > 0 ? 3 : 4;
    }
    if (millis() - t0 >= 600) return 1;
    delay(10);
  }
  return 0;
}

// ---- OSD kecil di pojok kiri atas (mis. ">> 01:23") ----
static void aviOsdDraw() {
  display.fillRect(4, 4, 132, 24, TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(8, 8);
  display.print(aviOsdTxt);
  display.setTextSize(1);
}
static void aviOsdSet(const char* prefix, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(aviOsdTxt, sizeof(aviOsdTxt), "%s %02u:%02u", prefix, (unsigned)(s / 60), (unsigned)(s % 60));
  aviOsdOn = true;
  aviOsdUntil = millis() + 1200;
  aviOsdDraw();
}
static void aviOsdTick() {           // dipanggil setelah tiap frame tampil
  if (!aviOsdOn) return;
  if (millis() < aviOsdUntil) aviOsdDraw();
  else { aviOsdOn = false; display.fillRect(4, 4, 132, 24, TFT_BLACK); }
}

// ---- overlay PAUSE: waktu + bar progres yg bisa digeser ----
static void aviOvLayout() {
  int W = display.width(), H = display.height();
  aviOvW = (W - 20 > 260) ? 260 : (W - 20);
  aviOvH = 92;
  aviOvX = (W - aviOvW) / 2;
  aviOvY = (H - aviOvH) / 2;
}
static bool aviInBar(int x, int y) {
  return x >= aviOvX && x <= aviOvX + aviOvW && y >= aviOvY + 30 && y <= aviOvY + 62;
}
static void aviDrawOvDynamic(uint32_t ms) {
  uint32_t dur = aviDurMs();
  uint32_t sec = ms / 1000, tot = dur / 1000;
  int bx = aviOvX + 10, bw = aviOvW - 20;
  display.fillRect(aviOvX + 2, aviOvY + 4, aviOvW - 4, 24, TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(aviOvX + 10, aviOvY + 8);
  if (tot > 0) display.printf("%02u:%02u/%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60), (unsigned)(tot / 60), (unsigned)(tot % 60));
  else         display.printf("%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
  display.setTextSize(1);
  int by = aviOvY + 42;
  display.fillRect(bx, by - 4, bw, 16, TFT_BLACK);
  display.fillRect(bx, by, bw, 8, 0x4208);
  int fw = (dur > 0) ? (int)(((uint64_t)(ms > dur ? dur : ms) * bw) / dur) : 0;
  if (fw > 0) display.fillRect(bx, by, fw, 8, TFT_WHITE);
  display.fillCircle(bx + fw, by + 4, 5, TFT_WHITE);
}
static void aviDrawPauseOverlay(uint32_t ms) {
  aviOvLayout();
  display.fillRect(aviOvX, aviOvY, aviOvW, aviOvH, TFT_BLACK);
  display.drawRect(aviOvX, aviOvY, aviOvW, aviOvH, TFT_WHITE);
  aviDrawOvDynamic(ms);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(aviOvX + 10, aviOvY + 66);
  display.print("PAUSED - ketuk lanjut");
  display.setCursor(aviOvX + 10, aviOvY + 78);
  display.print(aviCkN > 0 ? "Geser bar / usap: loncat" : "(tanpa indeks: seek mati)");
}

static void aviFreeAll() {
  if (aviJpeg) { aviJpeg->~JPEGDEC(); aviJpeg = nullptr; }
  if (aviJpegMem) { heap_caps_free(aviJpegMem); aviJpegMem = nullptr; }
  if (aviVq)  { heap_caps_free(aviVq);  aviVq = nullptr; }
  if (aviTmp) { heap_caps_free(aviTmp); aviTmp = nullptr; }
  if (aviCkOff) { heap_caps_free(aviCkOff); aviCkOff = nullptr; }
  if (aviCkAud) { heap_caps_free(aviCkAud); aviCkAud = nullptr; }
  aviSlotN = 0; aviVqCount = 0; aviVqHead = 0; aviCkN = 0;
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
  if (!aviVq) return false;

  // tabel titik lompat (opsional: gagal alokasi -> seek/resume mati, video tetap jalan)
  aviCkOff = (uint32_t*)heap_caps_malloc(AVI_CK_MAX * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  aviCkAud = (uint32_t*)heap_caps_malloc(AVI_CK_MAX * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!aviCkOff || !aviCkAud) {
    if (aviCkOff) { heap_caps_free(aviCkOff); aviCkOff = nullptr; }
    if (aviCkAud) { heap_caps_free(aviCkAud); aviCkAud = nullptr; }
  }
  return true;
}

// =====================================================================
//  INDEKS SEEK: baca idx1 -> tabel titik lompat tiap ~1 dtk video
// =====================================================================
static void aviBuildIndex() {
  aviCkN = 0; aviIdxFrames = 0;
  if (!aviCkOff || !aviCkAud || !aviTmp) return;

  uint8_t h[8];
  aviSeekTo(aviMoviEnd);
  if (aviRead(h, 8) != 8 || !aviIs(h, "idx1")) return;       // tanpa idx1 -> seek mati
  uint32_t ebase = aviMoviEnd + 8;
  if (ebase >= aviFileSize) return;
  uint32_t n = aviLE32(h + 4) / 16;
  uint32_t avail = (aviFileSize - ebase) / 16;
  if (n > avail) n = avail;
  if (n == 0) return;

  // offset idx1 bisa relatif ke tag 'movi' (standar) atau absolut: cek dgn membaca chunk-nya
  uint32_t base = 0; bool ok = false;
  uint8_t e[16], c[4];
  for (uint32_t i = 0; i < n && i < 8 && !ok; i++) {
    aviSeekTo(ebase + i * 16);
    if (aviRead(e, 16) != 16) return;
    if (e[0] < '0' || e[0] > '9') continue;
    uint32_t off = aviLE32(e + 8);
    uint32_t cand[2] = { aviMoviTag + off, off };
    for (int k = 0; k < 2 && !ok; k++) {
      if (cand[k] + 4 > aviFileSize) continue;
      aviSeekTo(cand[k]);
      if (aviRead(c, 4) == 4 && memcmp(c, e, 4) == 0) { base = (k == 0) ? aviMoviTag : 0; ok = true; }
    }
  }
  if (!ok) return;

  uint32_t fps = 1000000UL / (aviUsPerFrame ? aviUsPerFrame : 66667);
  if (fps < 1) fps = 1;
  uint32_t step = fps;
  if (n / step > AVI_CK_MAX) step = (n + AVI_CK_MAX - 1) / AVI_CK_MAX;
  aviCkStep = step;

  uint32_t vf = 0, done = 0;
  uint64_t ab = 0;
  const uint32_t per = AVI_AUD_CHUNK / 16;
  aviSeekTo(ebase);
  while (done < n) {
    uint32_t cnt = n - done; if (cnt > per) cnt = per;
    if (aviRead(aviTmp, cnt * 16) != (int)(cnt * 16)) break;
    for (uint32_t i = 0; i < cnt; i++) {
      const uint8_t* q = aviTmp + i * 16;
      if (q[0] < '0' || q[0] > '9' || q[1] < '0' || q[1] > '9') continue;
      int sn = (q[0] - '0') * 10 + (q[1] - '0');
      if (sn == aviVidStream && q[2] == 'd') {
        if ((vf % step) == 0) {
          uint32_t ci = vf / step;
          if (ci < AVI_CK_MAX) {
            uint32_t vms = (uint32_t)(((uint64_t)vf * aviUsPerFrame) / 1000ULL);
            uint32_t ams = aviAudBps ? (uint32_t)((ab * 1000ULL) / aviAudBps) : vms;
            if (ams > vms + 3000 || vms > ams + 3000) ams = vms;      // data audio meragukan -> pakai waktu video
            aviCkOff[ci] = base + aviLE32(q + 8);
            aviCkAud[ci] = ams;
            aviCkN = (int)ci + 1;
          }
        }
        vf++;
      } else if (sn == aviAudStream && q[2] == 'w') {
        ab += aviLE32(q + 12);
      }
    }
    done += cnt;
  }
  aviIdxFrames = vf;
  if (vf == 0) aviCkN = 0;
}

static int aviCkFor(uint32_t ms) {
  if (aviCkN <= 0) return -1;
  uint64_t fr = ((uint64_t)ms * 1000ULL) / (aviUsPerFrame ? aviUsPerFrame : 66667);
  uint32_t i = (uint32_t)(fr / aviCkStep);
  if (i >= (uint32_t)aviCkN) i = (uint32_t)aviCkN - 1;
  return (int)i;
}

// arahkan demuxer ke titik lompat terdekat sebelum 'ms' (antrean video dikosongkan)
static void aviSetPos(uint32_t ms) {
  aviVqHead = 0; aviVqCount = 0; aviEof = false; aviFinSig = false;
  int ck = (ms == 0) ? -1 : aviCkFor(ms);
  if (ck <= 0) {
    aviPos = aviMoviFirst; aviFrameNo = 0; aviAudOriginMs = 0; aviWallOriginMs = 0;
  } else {
    aviPos = aviCkOff[ck];
    aviFrameNo = (uint32_t)ck * aviCkStep;
    aviAudOriginMs = aviCkAud[ck];
    aviWallOriginMs = (uint32_t)(((uint64_t)aviFrameNo * aviUsPerFrame) / 1000ULL);
  }
}

// isi antrean (~1 dtk video) + tunggu CAM benar2 mulai memutar. return pesan error audio / nullptr
static const char* aviPrefillSync() {
  const char* audMsg = nullptr;
  int fps = (int)(1000000UL / (aviUsPerFrame ? aviUsPerFrame : 66667));
  int want = fps < 4 ? 4 : fps;
  if (want > aviSlotN - 2) want = aviSlotN - 2;
  unsigned long tp0 = millis();
  while (!aviEof && aviVqCount < want && millis() - tp0 < 4000) {
    if (aviPump() == 0) break;
  }
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
  return audMsg;
}

// loncat ke 'ms' saat memutar: restart audio di CAM + isi ulang antrean
static void aviDoSeek(uint32_t ms) {
  if (aviCkN <= 0) return;
  uint32_t dur = aviDurMs();
  if (dur > 2000 && ms > dur - 2000) ms = dur - 2000;
  aviOsdSet("Cari", ms);

  bool hadAudio = aviAudioOn;
  if (aviAudioOn) {
    musExtEnd();
    unsigned long t0 = millis();
    while (musExtActive() && millis() - t0 < 500) delay(5);   // tunggu musTask benar2 berhenti baca ring
  }
  aviAudioClk = false;
  aviSetPos(ms);
  if (hadAudio) {
    int rc = musExtBegin();
    aviAudioOn = (rc == 0);
    if (rc != 0) showToast("Audio gagal lanjut: tanpa suara");
  }
  const char* m = aviPrefillSync();
  if (m) showToast(m);
  aviT0 = millis() - aviWallOriginMs;
  aviPausedAccum = 0;
  aviCurMs = aviWallOriginMs;
  aviOsdSet("Cari", ms);                      // perpanjang tampilnya OSD setelah selesai
  Serial.printf("[AVI] seek -> %lu ms (frame %lu)\n", (unsigned long)ms, (unsigned long)aviFrameNo);
}

// =====================================================================
//  RESUME: posisi terakhir per video (NVS "avires", MRU 8 video)
//  Kunci = hash nama file + ukuran file.
// =====================================================================
static uint32_t aviMakeKey(const String& name, uint32_t size) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < name.length(); i++) { h ^= (uint8_t)name[i]; h *= 16777619UL; }
  h ^= size; h *= 16777619UL;
  return h ? h : 1;
}

static uint32_t aviResumeGet() {
  uint32_t t[AVI_RESUME_SLOTS * 2];
  memset(t, 0, sizeof(t));
  Preferences p;
  if (!p.begin("avires", true)) return 0;
  p.getBytes("t", t, sizeof(t));
  p.end();
  for (int i = 0; i < AVI_RESUME_SLOTS; i++)
    if (t[2 * i] == aviKey) return t[2 * i + 1];
  return 0;
}

// ms = 0 -> hapus entri video ini. Posisi < MIN atau hampir tamat otomatis dihapus.
static void aviResumeStore(uint32_t ms) {
  if (aviCkN <= 0) return;
  uint32_t dur = aviDurMs();
  if (ms < AVI_RESUME_MIN_MS || (dur && ms + AVI_RESUME_TAIL_MS > dur)) ms = 0;

  uint32_t t[AVI_RESUME_SLOTS * 2], nw[AVI_RESUME_SLOTS * 2];
  memset(t, 0, sizeof(t)); memset(nw, 0, sizeof(nw));
  Preferences p;
  if (!p.begin("avires", false)) return;
  p.getBytes("t", t, sizeof(t));

  uint32_t old = 0;
  for (int i = 0; i < AVI_RESUME_SLOTS; i++) if (t[2 * i] == aviKey) old = t[2 * i + 1];
  if (ms == 0 && old == 0) { p.end(); return; }                       // tdk ada yg berubah
  if (ms && old && (ms > old ? ms - old : old - ms) < 1000) { p.end(); return; }

  int k = 0;
  if (ms) { nw[0] = aviKey; nw[1] = ms; k = 1; }
  for (int i = 0; i < AVI_RESUME_SLOTS && k < AVI_RESUME_SLOTS; i++) {
    if (t[2 * i] && t[2 * i] != aviKey) { nw[2 * k] = t[2 * i]; nw[2 * k + 1] = t[2 * i + 1]; k++; }
  }
  p.putBytes("t", nw, sizeof(nw));
  p.end();
}

// layar tanya "Lanjutkan?" -> true = lanjut dari posisi tersimpan, false = dari awal
static bool aviAskResume(uint32_t ms) {
  int W = display.width(), H = display.height();
  uint32_t s = ms / 1000, tot = aviDurMs() / 1000;
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10, H / 2 - 56);
  display.print("Video ini pernah ditonton");
  display.setTextSize(2);
  display.setCursor(10, H / 2 - 38);
  display.printf("%02u:%02u / %02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60), (unsigned)(tot / 60), (unsigned)(tot % 60));
  int bw = (W - 30) / 2, bh = 48, by = H / 2 + 4;
  display.drawRect(10, by, bw, bh, TFT_WHITE);
  display.fillRect(20 + bw, by, bw, bh, TFT_WHITE);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE);
  display.setCursor(10 + (bw - display.textWidth("Awal")) / 2, by + 16);
  display.print("Awal");
  display.setTextColor(TFT_BLACK);
  display.setCursor(20 + bw + (bw - display.textWidth("Lanjut")) / 2, by + 16);
  display.print("Lanjut");
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);

  lgfx::touch_point_t tp;
  while (getInputPoint(tp)) delay(10);              // tunggu jari dari menu terangkat
  while (true) {
    if (getInputPoint(tp)) {
      int x = (int)tp.x, y = (int)tp.y;
      if (y >= by - 8 && y <= by + bh + 8) {
        bool lanjut = (x >= 15 + bw);
        while (getInputPoint(tp)) delay(10);
        return lanjut;
      }
    }
    if (wifiConnected && webServerRunning) webServer.handleClient();
    delay(10);
  }
}

// =====================================================================
//  PEMUTAR (blocking, pola sama dgn mjpegPlayFile)
//  Ketuk = pause/lanjut | Tahan = berhenti | Usap tepi bawah ke atas = Home
//  Usap kanan/kiri = +/-10 dtk | PAUSE + geser bar = loncat ke posisi
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
  aviKey = aviMakeKey(filename, aviFileSize);

  // Rotasi mengikuti bentuk video (lebar>=tinggi -> landscape), dikembalikan setelah selesai.
  int prevRotation = display.getRotation();
  display.setRotation(aviW >= aviH ? 1 : 0);
  aviScaleReady = false;
  aviShown = aviDropped = aviBigSkipped = 0;
  aviFrameNo = 0; aviVqHead = 0; aviVqCount = 0;
  aviPausedAccum = 0;
  aviOsdOn = false;

  // ---- indeks seek + tanya resume ----
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.print("Membuka video...");
  aviBuildIndex();

  uint32_t startMs = 0;
  if (aviCkN > 0) {
    uint32_t saved = aviResumeGet();
    uint32_t dur = aviDurMs();
    if (saved >= AVI_RESUME_MIN_MS && saved + AVI_RESUME_TAIL_MS <= dur) {
      if (aviAskResume(saved)) startMs = saved;
    }
  }
  aviSetPos(startMs);

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 10);
  display.print("Memutar: "); display.println(filename);
  display.setCursor(10, 26);
  display.print("Ketuk: pause | Tahan: berhenti");
  display.setCursor(10, 42);
  display.print("Usap tepi bawah ke atas: Home");
  display.setCursor(10, 52);
  display.print("Usap kiri/kanan: -/+10 dtk");

  // ---- audio -> TWS ----
  const char* audMsg = nullptr;
  aviAudioOn = false;
  if (aviAudStream >= 0) {
    if (aviAudFmt != 0x0055) {
      audMsg = "Audio bukan MP3: tanpa suara";
    } else {
      display.setCursor(10, 72);
      display.print("Menyambung audio ke TWS...");
      int rc = musExtBegin();
      if (rc == 0) aviAudioOn = true;
      else if (rc == 1) audMsg = "Bluetooth mati: tanpa suara";
      else if (rc == 2) audMsg = "Modul BT tdk terdeteksi";
      else if (rc == 3) audMsg = "TWS belum terhubung";
      else              audMsg = "RAM audio kurang";
    }
  }
  if (audMsg) { display.setCursor(10, 72); display.fillRect(10, 72, 220, 10, TFT_BLACK); display.print(audMsg); }

  // ---- isi antrean dulu, tunggu jam audio CAM hidup ----
  const char* syncMsg = aviPrefillSync();
  if (syncMsg) audMsg = syncMsg;
  if (audMsg) showToast(audMsg);
  if (aviCkN <= 0) showToast("Tanpa indeks: seek/resume mati");
  display.fillScreen(TFT_BLACK);
  aviT0 = millis() - aviWallOriginMs;
  aviCurMs = aviWallOriginMs;
  if (startMs) aviOsdSet(">>", startMs);

  bool stopReq = false, goHome = false;
  aviFinSig = false;
  unsigned long lastTouchChk = 0;
  unsigned long lastSave = millis();
  unsigned long playT0 = millis();

  while (!stopReq) {
    // 1. demux selama <=6 ms (sisa waktu buat decode)
    unsigned long tb = millis();
    while (millis() - tb < 6) { if (aviPump() <= 0) break; }

    if (aviEof && aviAudioClk && !aviFinSig) { musExtFinishStream(); aviFinSig = true; }

    // 2. jam: audio putus / habis lebih dulu -> lanjut dgn jam dinding dari posisi terakhir
    if (aviAudioClk && !musExtActive()) {
      aviAudioClk = false;
      aviT0 = millis() - aviCurMs; aviPausedAccum = 0;
      if (!aviFinSig) showToast("Audio putus");
    }
    uint32_t vt = aviNowMs();
    aviCurMs = vt;
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
        aviOsdTick();
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
        int g = aviTouchGesture((int)tp.x, (int)tp.y);
        if (g == 2)      { stopReq = true; goHome = true; }
        else if (g == 1) { stopReq = true; }
        else if (g == 3 || g == 4) {               // usap kanan/kiri -> loncat +/-10 dtk
          if (aviCkN > 0) {
            uint32_t to = (g == 3) ? vt + AVI_SEEK_STEP_MS : (vt > AVI_SEEK_STEP_MS ? vt - AVI_SEEK_STEP_MS : 0);
            aviDoSeek(to);
            lastSave = millis();
          }
          lastTouchChk = millis();
        }
        else {                                     // ketuk -> PAUSE
          unsigned long pauseStart = millis();
          if (aviAudioClk) musExtPause(true);
          aviDrawPauseOverlay(vt);
          aviResumeStore(vt);                      // simpan posisi saat pause
          bool resumed = false, seeked = false;
          uint32_t seekTo = 0;
          while (!resumed && !stopReq) {
            lgfx::touch_point_t tp2;
            if (getInputPoint(tp2)) {
              if (aviCkN > 0 && aviInBar((int)tp2.x, (int)tp2.y)) {
                // geser bar progres -> pilih posisi, lepas jari = loncat
                uint32_t dur = aviDurMs();
                int bw = aviOvW - 20;
                int lastPx = -1;
                uint32_t sel = vt;
                while (getInputPoint(tp2)) {
                  int px = (int)tp2.x - (aviOvX + 10);
                  if (px < 0) px = 0;
                  if (px > bw) px = bw;
                  if (px != lastPx) {
                    lastPx = px;
                    sel = (uint32_t)(((uint64_t)dur * (uint32_t)px) / (uint32_t)bw);
                    aviDrawOvDynamic(sel);
                  }
                  delay(10);
                }
                seeked = true; seekTo = sel; resumed = true;
              } else {
                int g2 = aviTouchGesture((int)tp2.x, (int)tp2.y);
                if (g2 == 2)      { stopReq = true; goHome = true; }
                else if (g2 == 1) { stopReq = true; }
                else if (g2 == 3 && aviCkN > 0) { seeked = true; seekTo = vt + AVI_SEEK_STEP_MS; resumed = true; }
                else if (g2 == 4 && aviCkN > 0) { seeked = true; seekTo = vt > AVI_SEEK_STEP_MS ? vt - AVI_SEEK_STEP_MS : 0; resumed = true; }
                else              { resumed = true; }
              }
            }
            if (wifiConnected && webServerRunning) webServer.handleClient();
            delay(5);
          }
          if (aviAudioClk && !seeked) musExtPause(false);
          if (!stopReq) {
            display.fillScreen(TFT_BLACK);
            if (seeked) {
              aviDoSeek(seekTo);                   // jam & aviPausedAccum di-reset di dalam
              lastSave = millis();
            } else {
              aviPausedAccum += millis() - pauseStart;
            }
            lastTouchChk = millis();
          }
        }
      }
    }

    // 5. simpan posisi berkala (tiap 30 dtk)
    if (aviCkN > 0 && millis() - lastSave >= AVI_RESUME_SAVE_MS) {
      lastSave = millis();
      aviResumeStore(aviCurMs);
    }

    if (wifiConnected && webServerRunning) webServer.handleClient();
    yield();
  }

  // ---- video habis: tunggu sisa audio yg masih tertahan di buffer CAM ----
  bool finished = !stopReq;                        // tamat sendiri (bukan dihentikan)
  if (!stopReq && aviAudioClk) {
    if (!aviFinSig) musExtFinishStream();
    unsigned long te = millis();
    while (!stopReq && musExtActive() && !musExtEnded() && millis() - te < 12000) {
      lgfx::touch_point_t tp;
      if (getInputPoint(tp)) {
        int g = aviTouchGesture((int)tp.x, (int)tp.y);
        if (g == 2) { stopReq = true; goHome = true; }
        else        { stopReq = true; }
      }
      if (wifiConnected && webServerRunning) webServer.handleClient();
      delay(20);
    }
  }

  // simpan posisi terakhir (tamat -> hapus entri, jadi tontonan berikutnya mulai dari awal)
  aviResumeStore(finished ? 0 : aviCurMs);

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
