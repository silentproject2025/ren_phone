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
//    - Usap kanan/kiri = +/-10 dtk | ketuk = panel kontrol (bar progres, -10/+10, cerah, volume).
//    - Usap atas/bawah: separuh kiri = kecerahan, separuh kanan = volume (video tetap jalan).
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
int      musExtGetVol();
void     musExtSetVol(int v);   // 0..127
extern int brightness;          // phone.ino (10..255)

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
static int       aviOsdBar = -1;                               // persen bar OSD (-1 = tanpa bar)
static char      aviTitle[40];                                 // judul tampilan (nama file)
static int       aviSplashY = 0;
static bool      aviReqStop = false, aviReqHome = false;       // diset panel kontrol
static int       aviVolCur = 80, aviVolPend = -1;              // volume 0..127 (nilai terbaru / belum terkirim)
static unsigned long aviVolMs = 0;
static LGFX_Sprite aviOsdSpr(&display);                      // OSD kecil (di-push 1x per frame)
static bool aviOsdSprOk = false, aviOsdDirty = false, aviBarDirty = true;
static int  aviBarFw = -1;
// geometri panel kontrol
static int pnW = 0, pnH = 0, pnBy0 = 0, pnBarX = 0, pnBarW = 0, pnBarY = 0, pnCy = 0;
static int pnPlayX = 0, pnBackX = 0, pnFwdX = 0, pnSlY = 0, pnSlH = 0, pnSlBX = 0, pnSlVX = 0;

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

// ---- warna ----
#define AVI_GRAY   0x8410
#define AVI_DARK   0x4208
#define AVI_TRACK  0x2104

// ---- OSD kecil di pojok kiri atas (teks + bar opsional), mis. ">> 01:23" / "Cerah 72%" ----
// Digambar ke sprite kecil lalu di-push SEKALI per frame (1 jendela SPI) -> tdk berkedip.
static void aviOsdRender(LGFX_Sprite& g) {
  g.fillRect(0, 0, 150, 34, TFT_BLACK);
  g.setTextColor(TFT_WHITE);
  g.setTextSize(2);
  g.setCursor(4, 4);
  g.print(aviOsdTxt);
  g.setTextSize(1);
  if (aviOsdBar >= 0) {
    g.fillRoundRect(4, 26, 142, 4, 2, AVI_DARK);
    int fw = aviOsdBar * 142 / 100;
    if (fw > 0) g.fillRoundRect(4, 26, fw, 4, 2, TFT_WHITE);
  }
}
static void aviOsdPush() {
  if (!aviOsdSprOk) return;                          // sprite gagal dibuat -> tanpa OSD (video tetap normal)
  if (aviOsdDirty) { aviOsdRender(aviOsdSpr); aviOsdDirty = false; }
  aviOsdSpr.pushSprite(4, 4);
}
static void aviOsdSetTxt(const char* txt, int pct) {
  snprintf(aviOsdTxt, sizeof(aviOsdTxt), "%s", txt);
  aviOsdBar = pct > 100 ? 100 : pct;
  aviOsdOn = true;
  aviOsdDirty = true;
  aviOsdUntil = millis() + 1200;
  aviOsdPush();
}
static void aviOsdSet(const char* prefix, uint32_t ms) {
  uint32_t s = ms / 1000, dur = aviDurMs();
  char b[24];
  snprintf(b, sizeof(b), "%s %02u:%02u", prefix, (unsigned)(s / 60), (unsigned)(s % 60));
  aviOsdSetTxt(b, dur ? (int)(((uint64_t)ms * 100ULL) / dur) : -1);
}
// garis progres tipis di tepi bawah. Video di-clip supaya tdk menimpanya, jadi
// cukup digambar saat berubah (bukan tiap frame) -> tdk berkedip.
static void aviDrawMiniBar(bool force) {
  uint32_t dur = aviDurMs();
  if (!dur) return;
  int W = display.width(), H = display.height();
  uint32_t cur = aviCurMs > dur ? dur : aviCurMs;
  int fw = (int)(((uint64_t)cur * (uint32_t)W) / dur);
  if (!force && !aviBarDirty && fw == aviBarFw) return;
  display.fillRect(fw, H - 3, W - fw, 3, AVI_TRACK);
  if (fw > 0) display.fillRect(0, H - 3, fw, 3, TFT_WHITE);
  aviBarFw = fw; aviBarDirty = false;
}
static void aviOsdTick() {           // dipanggil setelah tiap frame tampil
  aviDrawMiniBar(false);
  if (!aviOsdOn) return;
  if (millis() < aviOsdUntil) aviOsdPush();
  else {
    aviOsdOn = false;                // frame yg baru tampil sudah menimpa area OSD
    if (aviOffX > 0 || aviOffY > 0) display.fillRect(4, 4, 150, 34, TFT_BLACK);   // video tdk full-layar: bersihkan manual
  }
}

// ---- cerah & volume ----
static void aviSetBri(int b) {
  if (b < 10) b = 10;
  if (b > 255) b = 255;
  if (b == brightness) return;
  brightness = b;
  display.setBrightness((uint8_t)b);
}
static void aviVolApply(int v, bool force) {      // kirim ke CAM maks tiap 60 ms, nilai akhir selalu terkirim
  if (v < 0) v = 0;
  if (v > 127) v = 127;
  aviVolCur = v; aviVolPend = v;
  if (force || millis() - aviVolMs >= 60) { musExtSetVol(v); aviVolMs = millis(); aviVolPend = -1; }
}
static void aviVolFlush() {
  if (aviVolPend >= 0) { musExtSetVol(aviVolPend); aviVolPend = -1; aviVolMs = millis(); }
}

// ---- helper teks ----
static void aviTextC(const char* t, int cx, int y, int size, uint16_t col) {
  display.setTextSize(size);
  if (display.textWidth(t) > display.width() - 8) display.setTextSize(1);
  display.setTextColor(col);
  display.setCursor(cx - display.textWidth(t) / 2, y);
  display.print(t);
  display.setTextSize(1);
}
static void aviFmtTime(char* out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s >= 3600) snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  else           snprintf(out, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// ---- layar pembuka ----
static void aviSplashStatus(const char* msg) {
  int W = display.width();
  display.fillRect(0, aviSplashY - 2, W, 14, TFT_BLACK);
  aviTextC(msg, W / 2, aviSplashY, 1, AVI_GRAY);
}
static void aviSplash(const char* status, bool hints) {
  int W = display.width(), H = display.height();
  display.fillScreen(TFT_BLACK);
  int cx = W / 2, cy = H / 2 - 40;
  display.drawCircle(cx, cy, 28, TFT_WHITE);
  display.drawCircle(cx, cy, 27, TFT_WHITE);
  display.fillTriangle(cx - 8, cy - 13, cx - 8, cy + 13, cx + 15, cy, TFT_WHITE);
  aviTextC(aviTitle, cx, cy + 42, 2, TFT_WHITE);
  aviSplashY = cy + 68;
  aviSplashStatus(status);
  if (hints) {
    aviTextC("Ketuk: kontrol | Tahan: stop", cx, H - 44, 1, AVI_DARK);
    aviTextC("Usap kiri/kanan: -/+10 dtk", cx, H - 32, 1, AVI_DARK);
    aviTextC("Usap atas/bawah: cerah | volume", cx, H - 20, 1, AVI_DARK);
  }
}

// ---- ikon ----
static void aviIconSun(int cx, int cy, uint16_t col) {
  display.fillCircle(cx, cy, 4, col);
  static const int8_t d[8][2] = { {1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1} };
  for (int k = 0; k < 8; k++)
    display.drawLine(cx + d[k][0] * 7, cy + d[k][1] * 7, cx + d[k][0] * 9, cy + d[k][1] * 9, col);
}
static void aviIconSpeaker(int cx, int cy, bool muted, uint16_t col) {
  display.fillRect(cx - 8, cy - 3, 4, 7, col);
  display.fillTriangle(cx - 4, cy - 3, cx + 2, cy - 7, cx + 2, cy + 7, col);
  display.fillTriangle(cx - 4, cy - 3, cx - 4, cy + 3, cx + 2, cy + 7, col);
  if (muted) {
    display.drawLine(cx + 5, cy - 4, cx + 10, cy + 4, col);
    display.drawLine(cx + 10, cy - 4, cx + 5, cy + 4, col);
  } else {
    display.drawLine(cx + 5, cy - 3, cx + 7, cy, col);
    display.drawLine(cx + 7, cy, cx + 5, cy + 3, col);
    display.drawLine(cx + 8, cy - 6, cx + 11, cy, col);
    display.drawLine(cx + 11, cy, cx + 8, cy + 6, col);
  }
}

// =====================================================================
//  PANEL KONTROL (muncul saat ketuk = pause)
//   atas   : judul + tombol Home / Tutup
//   kiri   : slider kecerahan | kanan: slider volume
//   bawah  : waktu, bar progres (geser), tombol -10 / play / +10
// =====================================================================
static void aviPanelLayout() {
  pnW = display.width(); pnH = display.height();
  pnBy0   = pnH - 80;
  pnBarX  = 14;
  pnBarW  = pnW - 28;
  pnBarY  = pnBy0 + 28;
  pnCy    = pnBy0 + 57;
  pnPlayX = pnW / 2;
  pnBackX = pnW / 2 - 64;
  pnFwdX  = pnW / 2 + 64;
  pnSlY   = 38;
  pnSlH   = pnBy0 - 8 - pnSlY;
  pnSlBX  = 8;
  pnSlVX  = pnW - 38;
}
static bool aviNear(int x, int y, int cx, int cy, int r) {
  int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}
static void aviDrawSlider(int which, int pct) {          // which 0 = cerah, 1 = volume
  int x = which ? pnSlVX : pnSlBX, y = pnSlY, w = 30, h = pnSlH;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  display.fillRoundRect(x, y, w, h, 15, TFT_BLACK);
  display.drawRoundRect(x, y, w, h, 15, AVI_DARK);
  if (which) aviIconSpeaker(x + 15, y + 15, pct == 0, TFT_WHITE);
  else       aviIconSun(x + 15, y + 15, TFT_WHITE);
  int t0 = y + 30, t1 = y + h - 22, th = t1 - t0;
  display.fillRoundRect(x + 11, t0, 8, th, 4, AVI_DARK);
  int fh = th * pct / 100;
  if (fh > 0) display.fillRoundRect(x + 11, t1 - fh, 8, fh, 4, TFT_WHITE);
  display.fillCircle(x + 15, t1 - fh, 6, TFT_WHITE);
  char b[8];
  snprintf(b, sizeof(b), "%d", pct);
  display.setTextSize(1);
  display.setTextColor(AVI_GRAY);
  display.setCursor(x + 15 - display.textWidth(b) / 2, y + h - 14);
  display.print(b);
}
static int aviSliderPct(int y) {                          // posisi sentuh -> persen
  int t0 = pnSlY + 30, t1 = pnSlY + pnSlH - 22;
  int p = (t1 - y) * 100 / (t1 - t0);
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}
static int aviBriPct() { return brightness * 100 / 255; }
static int aviVolPct() { return aviVolCur * 100 / 127; }

static void aviDrawPanelBar(uint32_t ms) {                // waktu + bar progres
  uint32_t dur = aviDurMs();
  char a[12], b[12];
  aviFmtTime(a, sizeof(a), ms);
  aviFmtTime(b, sizeof(b), dur);
  display.fillRect(0, pnBy0 + 6, pnW, 36, TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(pnBarX, pnBy0 + 8);
  display.print(a);
  display.setTextColor(AVI_GRAY);
  display.setCursor(pnBarX + pnBarW - display.textWidth(b), pnBy0 + 8);
  display.print(b);
  display.fillRoundRect(pnBarX, pnBarY, pnBarW, 6, 3, AVI_DARK);
  int fw = dur ? (int)(((uint64_t)(ms > dur ? dur : ms) * (uint32_t)pnBarW) / dur) : 0;
  if (fw > 0) display.fillRoundRect(pnBarX, pnBarY, fw, 6, 3, TFT_WHITE);
  display.fillCircle(pnBarX + fw, pnBarY + 3, 7, TFT_WHITE);
}
static void aviDrawPanel(uint32_t ms) {
  int W = pnW;
  // bar atas
  display.fillRect(0, 0, W, 28, TFT_BLACK);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  char tt[40];
  int maxc = (W - 84) / 6;
  if (maxc > (int)sizeof(tt) - 1) maxc = sizeof(tt) - 1;
  snprintf(tt, sizeof(tt), "%.*s", maxc, aviTitle);
  display.setCursor(10, 10);
  display.print(tt);
  // Home
  int hx = W - 72;
  display.drawRoundRect(hx, 3, 30, 22, 6, AVI_DARK);
  display.drawLine(hx + 15, 8, hx + 8, 14, TFT_WHITE);  display.drawLine(hx + 15, 8, hx + 22, 14, TFT_WHITE);
  display.drawRect(hx + 10, 14, 10, 7, TFT_WHITE);
  // Tutup (X)
  int cx = W - 36;
  display.drawRoundRect(cx, 3, 30, 22, 6, AVI_DARK);
  display.drawLine(cx + 10, 9, cx + 20, 19, TFT_WHITE);  display.drawLine(cx + 20, 9, cx + 10, 19, TFT_WHITE);
  // slider samping
  aviDrawSlider(0, aviBriPct());
  aviDrawSlider(1, aviVolPct());
  // kartu bawah
  display.fillRoundRect(0, pnBy0, W, 90, 14, TFT_BLACK);
  display.drawFastHLine(20, pnBy0, W - 40, AVI_DARK);
  aviDrawPanelBar(ms);
  // tombol -10 / play / +10
  display.setTextSize(1);
  display.drawCircle(pnBackX, pnCy, 18, AVI_DARK);
  display.setTextColor(aviCkN > 0 ? TFT_WHITE : AVI_DARK);
  display.setCursor(pnBackX - display.textWidth("-10") / 2, pnCy - 3);
  display.print("-10");
  display.drawCircle(pnFwdX, pnCy, 18, AVI_DARK);
  display.setCursor(pnFwdX - display.textWidth("+10") / 2, pnCy - 3);
  display.print("+10");
  display.fillCircle(pnPlayX, pnCy, 21, TFT_WHITE);
  display.fillTriangle(pnPlayX - 6, pnCy - 10, pnPlayX - 6, pnCy + 10, pnPlayX + 10, pnCy, TFT_BLACK);
}

static void aviDoSeek(uint32_t ms, bool osd);
static void aviResumeStore(uint32_t ms);

// loncat saat panel terbuka: tetap PAUSE di frame tujuan
static uint32_t aviPanelSeek(uint32_t ms, unsigned long* pauseStart) {
  uint32_t dur = aviDurMs();
  if (dur > 2000 && ms > dur - 2000) ms = dur - 2000;
  aviDoSeek(ms, false);
  if (aviVqCount >= 1) {
    int sl = aviVqHead;
    if (aviVqSize[sl] > 0) aviShowFrame(aviVq + (size_t)sl * aviSlotSize, aviVqSize[sl]);
  }
  if (aviAudioClk) musExtPause(true);
  *pauseStart = millis();
  return ms;
}

// sesi pause: kembali kalau lanjut main atau aviReqStop (Tutup / Home) diset
static void aviRunPanel(uint32_t vt) {
  unsigned long pauseStart = millis();
  if (aviAudioClk) musExtPause(true);
  aviReqStop = false; aviReqHome = false;
  aviPanelLayout();
  uint32_t shown = vt;
  aviDrawPanel(shown);
  aviResumeStore(vt);                              // simpan posisi saat pause

  lgfx::touch_point_t tp;
  while (getInputPoint(tp)) delay(10);             // tunggu jari ketukan pertama lepas
  bool resume = false;
  while (!resume && !aviReqStop) {
    if (getInputPoint(tp)) {
      int x = (int)tp.x, y = (int)tp.y;
      if (y < 28 && x >= pnW - 36)       { aviReqStop = true; }
      else if (y < 28 && x >= pnW - 72)  { aviReqStop = true; aviReqHome = true; }
      else if (aviNear(x, y, pnPlayX, pnCy, 26)) { resume = true; }
      else if (aviCkN > 0 && aviNear(x, y, pnBackX, pnCy, 24)) {
        shown = aviPanelSeek(shown > AVI_SEEK_STEP_MS ? shown - AVI_SEEK_STEP_MS : 0, &pauseStart);
        aviDrawPanel(shown);
      }
      else if (aviCkN > 0 && aviNear(x, y, pnFwdX, pnCy, 24)) {
        shown = aviPanelSeek(shown + AVI_SEEK_STEP_MS, &pauseStart);
        aviDrawPanel(shown);
      }
      else if (aviCkN > 0 && y >= pnBarY - 14 && y <= pnBarY + 18 && x >= pnBarX - 10 && x <= pnBarX + pnBarW + 10) {
        // geser bar progres, lepas jari = loncat (tetap pause)
        uint32_t dur = aviDurMs(), sel = shown;
        int lastPx = -1;
        while (getInputPoint(tp)) {
          int px = (int)tp.x - pnBarX;
          if (px < 0) px = 0;
          if (px > pnBarW) px = pnBarW;
          if (px != lastPx) {
            lastPx = px;
            sel = (uint32_t)(((uint64_t)dur * (uint32_t)px) / (uint32_t)pnBarW);
            aviDrawPanelBar(sel);
          }
          delay(8);
        }
        shown = aviPanelSeek(sel, &pauseStart);
        aviDrawPanel(shown);
      }
      else if (y >= pnSlY && y <= pnSlY + pnSlH && x >= pnSlBX - 4 && x <= pnSlBX + 34) {
        while (getInputPoint(tp)) {                 // slider kecerahan
          int p = aviSliderPct((int)tp.y);
          aviSetBri(p * 255 / 100);
          aviDrawSlider(0, aviBriPct());
          delay(8);
        }
      }
      else if (y >= pnSlY && y <= pnSlY + pnSlH && x >= pnSlVX - 4 && x <= pnSlVX + 34) {
        while (getInputPoint(tp)) {                 // slider volume
          int p = aviSliderPct((int)tp.y);
          aviVolApply(p * 127 / 100, false);
          aviDrawSlider(1, aviVolPct());
          delay(8);
        }
        aviVolFlush();
      }
      else if (y < pnBy0 && y > 28) { resume = true; }           // ketuk area video = lanjut
      else { while (getInputPoint(tp)) delay(10); }              // sentuh area kosong kartu bawah
      if (!resume) while (getInputPoint(tp)) delay(10);
    }
    if (wifiConnected && webServerRunning) webServer.handleClient();
    delay(5);
  }
  while (getInputPoint(tp)) delay(10);
  if (!aviReqStop) {
    if (aviAudioClk) musExtPause(false);
    if (aviOffX > 0 || aviOffY > 0) display.fillScreen(TFT_BLACK);   // video full-layar: frame berikut menimpa panel, tanpa kedip hitam
    aviBarDirty = true;
    aviPausedAccum += millis() - pauseStart;
  }
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
static void aviDoSeek(uint32_t ms, bool osd) {
  if (aviCkN <= 0) return;
  uint32_t dur = aviDurMs();
  if (dur > 2000 && ms > dur - 2000) ms = dur - 2000;
  if (osd) aviOsdSet("Cari", ms);

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
  if (osd) aviOsdSet("Cari", ms);            // perpanjang tampilnya OSD setelah selesai
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

// layar tanya "Lanjutkan menonton?" -> true = lanjut dari posisi tersimpan, false = dari awal
static bool aviAskResume(uint32_t ms) {
  int W = display.width(), H = display.height();
  uint32_t dur = aviDurMs();
  char a[12], b[12], tt[28];
  aviFmtTime(a, sizeof(a), ms);
  aviFmtTime(b, sizeof(b), dur);
  snprintf(tt, sizeof(tt), "%s / %s", a, b);
  display.fillScreen(TFT_BLACK);
  aviTextC("Lanjutkan menonton?", W / 2, H / 2 - 70, 2, TFT_WHITE);
  aviTextC(tt, W / 2, H / 2 - 44, 2, AVI_GRAY);
  display.fillRoundRect(20, H / 2 - 18, W - 40, 5, 2, AVI_DARK);
  int fw = dur ? (int)(((uint64_t)ms * (uint32_t)(W - 40)) / dur) : 0;
  if (fw > 0) display.fillRoundRect(20, H / 2 - 18, fw, 5, 2, TFT_WHITE);
  int bw = (W - 50) / 2, bh = 50, by = H / 2 + 6;
  display.drawRoundRect(20, by, bw, bh, 12, TFT_WHITE);
  display.fillRoundRect(30 + bw, by, bw, bh, 12, TFT_WHITE);
  aviTextC("Dari awal", 20 + bw / 2, by + 18, 2, TFT_WHITE);
  aviTextC("Lanjutkan", 30 + bw + bw / 2, by + 18, 2, TFT_BLACK);

  lgfx::touch_point_t tp;
  while (getInputPoint(tp)) delay(10);              // tunggu jari dari menu terangkat
  while (true) {
    if (getInputPoint(tp)) {
      int x = (int)tp.x, y = (int)tp.y;
      if (y >= by - 8 && y <= by + bh + 8) {
        bool lanjut = (x >= 25 + bw);
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
//   Ketuk            = panel kontrol (pause, bar progres, -10/+10, cerah, volume)
//   Usap kiri/kanan  = -/+10 dtk
//   Usap atas/bawah  = cerah (separuh kiri) / volume (separuh kanan)
//   Tahan            = berhenti | Usap tepi bawah ke atas = Home
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

  // judul tampilan = nama file tanpa folder & ekstensi
  {
    String base = filename;
    int sl = base.lastIndexOf('/');
    if (sl >= 0) base = base.substring(sl + 1);
    int dt = base.lastIndexOf('.');
    if (dt > 0) base = base.substring(0, dt);
    snprintf(aviTitle, sizeof(aviTitle), "%s", base.c_str());
    if (strlen(aviTitle) > 30) { aviTitle[28] = '.'; aviTitle[29] = '.'; aviTitle[30] = 0; }
  }

  // Rotasi mengikuti bentuk video (lebar>=tinggi -> landscape), dikembalikan setelah selesai.
  int prevRotation = display.getRotation();
  display.setRotation(aviW >= aviH ? 1 : 0);
  aviScaleReady = false;
  aviShown = aviDropped = aviBigSkipped = 0;
  aviFrameNo = 0; aviVqHead = 0; aviVqCount = 0;
  aviPausedAccum = 0;
  aviOsdOn = false; aviOsdBar = -1; aviOsdDirty = false;
  aviBarDirty = true; aviBarFw = -1;
  aviOsdSprOk = false;
  aviOsdSpr.setColorDepth(16);
  aviOsdSpr.setPsram(false);
  if (aviOsdSpr.createSprite(150, 34)) aviOsdSprOk = true;
  else { aviOsdSpr.setPsram(true); if (aviOsdSpr.createSprite(150, 34)) aviOsdSprOk = true; }
  aviVolCur = musExtGetVol(); aviVolPend = -1; aviVolMs = 0;

  // ---- indeks seek + tanya resume ----
  aviSplash("Membuka video...", false);
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
  aviSplash("Memuat...", true);

  // ---- audio -> TWS ----
  const char* audMsg = nullptr;
  aviAudioOn = false;
  if (aviAudStream >= 0) {
    if (aviAudFmt != 0x0055) {
      audMsg = "Audio bukan MP3: tanpa suara";
    } else {
      aviSplashStatus("Menyambung audio ke TWS...");
      int rc = musExtBegin();
      if (rc == 0) aviAudioOn = true;
      else if (rc == 1) audMsg = "Bluetooth mati: tanpa suara";
      else if (rc == 2) audMsg = "Modul BT tdk terdeteksi";
      else if (rc == 3) audMsg = "TWS belum terhubung";
      else              audMsg = "RAM audio kurang";
    }
  }
  if (audMsg) aviSplashStatus(audMsg);

  // ---- isi antrean dulu, tunggu jam audio CAM hidup ----
  const char* syncMsg = aviPrefillSync();
  if (syncMsg) audMsg = syncMsg;
  if (audMsg) showToast(audMsg);
  if (aviCkN <= 0) showToast("Tanpa indeks: seek/resume mati");
  display.fillScreen(TFT_BLACK);
  aviT0 = millis() - aviWallOriginMs;
  aviCurMs = aviWallOriginMs;
  if (startMs) aviOsdSet(">>", startMs);

  const int W = display.width(), H = display.height();
  bool stopReq = false, goHome = false;
  aviFinSig = false;
  unsigned long lastTouchChk = 0;
  unsigned long lastSave = millis();
  unsigned long playT0 = millis();

  // state sentuhan (non-blocking, supaya video tetap jalan saat usap cerah/volume)
  // tsMode: 0 belum jelas | 1 usap horizontal (seek saat lepas) | 2 cerah | 3 volume | 4 Home | 5 tahan = stop
  bool tsAct = false; int tsMode = 0, tsX0 = 0, tsY0 = 0, tsDx = 0, tsBase = 0;
  unsigned long tsT0 = 0;

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
        display.setClipRect(0, 0, W, H - 3);           // video tdk menimpa garis progres
        if (aviShowFrame(aviVq + (size_t)sl * aviSlotSize, aviVqSize[sl])) aviShown++;
        else aviDropped++;
        display.clearClipRect();
        aviOsdTick();
      }
      aviVqHead = (aviVqHead + 1) % aviSlotN; aviVqCount--;
    } else if (aviEof && aviVqCount == 0) {
      break;                                       // video habis
    } else {
      delay(1);
    }

    // 4. sentuhan
    if (tsAct || millis() - lastTouchChk >= 40) {
      lastTouchChk = millis();
      lgfx::touch_point_t tp;
      if (getInputPoint(tp)) {
        if (!tsAct) {
          tsAct = true; tsMode = 0; tsX0 = (int)tp.x; tsY0 = (int)tp.y; tsT0 = millis(); tsDx = 0;
        } else {
          int dx = (int)tp.x - tsX0, dy = (int)tp.y - tsY0;
          int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
          tsDx = dx;
          if (tsMode == 0) {
            bool nearBottom = tsY0 > H - 40;
            if (nearBottom && -dy > 28)               tsMode = 4;
            else if (adx > 40 && adx > ady * 2)       tsMode = 1;
            else if (ady > 16 && ady > adx * 2) {
              tsMode = (tsX0 < W / 2) ? 2 : 3;
              tsBase = (tsMode == 2) ? brightness : aviVolCur;
              tsY0 = (int)tp.y;                       // titik awal geser = di sini (tdk loncat)
            }
            else if (adx < 12 && ady < 12 && millis() - tsT0 >= 600) tsMode = 5;
          }
          if (tsMode == 2 || tsMode == 3) {
            int span = H * 55 / 100;
            int dv = tsY0 - (int)tp.y;                // geser ke atas = naik
            if (tsMode == 2) {
              int nb = tsBase + dv * 245 / span;
              aviSetBri(nb);
              char b[20]; snprintf(b, sizeof(b), "Cerah %d%%", aviBriPct());
              aviOsdSetTxt(b, aviBriPct());
            } else {
              int nv = tsBase + dv * 127 / span;
              aviVolApply(nv, false);
              char b[20]; snprintf(b, sizeof(b), "Vol %d%%", aviVolPct());
              aviOsdSetTxt(b, aviVolPct());
            }
          }
          if (tsMode == 4) { stopReq = true; goHome = true; }
          if (tsMode == 5) { stopReq = true; }
        }
      } else if (tsAct) {                             // jari lepas
        tsAct = false;
        if (tsMode == 3) aviVolFlush();
        if (tsMode == 0) {                            // ketuk -> panel kontrol
          aviRunPanel(vt);
          if (aviReqStop) { stopReq = true; goHome = aviReqHome; }
          lastSave = millis();
          lastTouchChk = millis();
        } else if (tsMode == 1) {                     // usap kanan/kiri -> loncat +/-10 dtk
          if (aviCkN > 0) {
            uint32_t to = (tsDx > 0) ? vt + AVI_SEEK_STEP_MS : (vt > AVI_SEEK_STEP_MS ? vt - AVI_SEEK_STEP_MS : 0);
            aviDoSeek(to, true);
            lastSave = millis();
          }
          lastTouchChk = millis();
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

  aviVolFlush();
  {                                                // jangan sampai sisa sentuhan 'bocor' ke layar berikutnya
    lgfx::touch_point_t tp;
    unsigned long tr = millis();
    while (getInputPoint(tp) && millis() - tr < 1500) delay(10);
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
  if (aviOsdSprOk) { aviOsdSpr.deleteSprite(); aviOsdSprOk = false; }
  display.setRotation(prevRotation);

  unsigned long used = millis() - playT0 - aviPausedAccum;
  Serial.printf("[AVI] Selesai '%s' | tampil=%lu lewat=%lu kebesaran=%lu | waktu=%lums | fps=%.1f\n",
                filename.c_str(), (unsigned long)aviShown, (unsigned long)aviDropped,
                (unsigned long)aviBigSkipped, used, used ? (1000.0f * aviShown / used) : 0.0f);

  if (goHome) navGoHome();
  else        needRedrawNow();
  if (aviBigSkipped > 0) showToast("Frame kebesaran: turunkan -q:v");   // setelah redraw supaya toast kelihatan
}
