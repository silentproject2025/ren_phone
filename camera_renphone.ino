// ============================================================
//  camera_renphone.ino -- app "Kamera" (OV2640 di ESP32-CAM)  v1
//
//  Kamera ada di modul ESP32-CAM (ko-prosesor), BUKAN di S3. Gambar JPEG dikirim lewat
//  link UART yang sama dengan audio Bluetooth (rplink.h), jadi:
//    * link dinaikkan otomatis 921600 -> 2/3 Mbaud selama app terbuka (dengan fallback aman)
//    * frame preview dikirim dengan kontrol kredit (maks 2 frame di jalan) -> latensi kecil
//    * foto resolusi tinggi (sampai 1600x1200) diminta terpisah, disimpan APA ADANYA
//      dari sensor ke SD (tanpa encode ulang di S3)
//
//  Fitur: preview langsung (landscape), foto + timer + beruntun + flash LED PWM, video AVI MJPEG
//  (jalan di pemutar AVI bawaan & PC), timelapse -> AVI, deteksi gerak (decode DC-only 1/8,
//  hampir gratis) yang menyimpan foto otomatis, kontrol sensor (WB, efek, exposure, mode malam,
//  mirror/flip...), info diagnostik link/RAM CAM.
//
//  File ini dimuat SEBELUM musicbt_renphone.ino (urutan abjad). musicbt memanggil
//  camRxFrame()/camNoteRx()/camLinkBusy() di bawah lewat prototipe otomatis.
//  camRxFrame() jalan di musTask (stack PSRAM) -> hanya salin memori, DILARANG menyentuh SD/NVS.
// ============================================================
#include "rplink.h"
#include "cam_rx.h"
#include "cam_avi.h"

#define CAM_UART_RXBUF   6144            // dipakai musicbt (setRxBufferSize); 2048 asli terlalu kecil di 2-3 Mbaud
#define CAM_SLOT_SZ      (96 * 1024)
#define CAM_STILL_SZ     (448 * 1024)
#define CAM_STILL_SZ_BIG (768 * 1024)    // sensor 3MP (OV3660/OV5640): foto sampai 2048x1536
#define CAM_AVI_MAXFR    20000
#define CAM_DIR          "/Kamera"
#define CAM_MOT_MAX      4800            // 80x60 sel (VGA / 8)

portMUX_TYPE camRxMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------- tabel ----------------
static const char*    const CAM_RES_NAME[11] = {"160x120", "320x240", "400x296", "480x320", "640x480", "800x600", "1024x768", "1280x720", "1280x1024", "1600x1200", "2048x1536"};
static const uint16_t       CAM_RES_W[11]    = {160, 320, 400, 480, 640, 800, 1024, 1280, 1280, 1600, 2048};
static const uint16_t       CAM_RES_H[11]    = {120, 240, 296, 320, 480, 600, 768, 720, 1024, 1200, 1536};
static const uint8_t        CAM_PRV_CODE[3]  = {RPR_QQVGA, RPR_QVGA, RPR_VGA};
static const char*    const CAM_PRV_NAME[3]  = {"160x120 (2x)", "320x240", "640x480 (1/2)"};
static const uint8_t        CAM_TIMER_S[3]   = {0, 3, 10};
static const uint8_t        CAM_BURST_N[3]   = {1, 3, 5};
static const uint8_t        CAM_XCLK_MHZ[3]  = {10, 16, 20};
static const uint8_t        CAM_LAPSE_S[5]   = {2, 5, 10, 30, 60};
static const uint32_t       CAM_LINK_BAUD[3] = {RP_BAUD, 2000000, 3000000};
static const char*    const CAM_LINK_NAME[3] = {"921 kbaud (aman)", "2 Mbaud", "3 Mbaud (cepat)"};
static const char*    const CAM_WB_NAME[5]   = {"Auto", "Cerah", "Mendung", "Kantor", "Rumah"};
static const char*    const CAM_FX_NAME[7]   = {"Normal", "Negatif", "Abu-abu", "Merah", "Hijau", "Biru", "Sepia"};
static const char*    const CAM_FLASH_NAME[4]= {"Mati", "Senter 30%", "Senter 100%", "Kilat saat foto"};
static const char*    const CAM_SENS_NAME[3] = {"Rendah", "Sedang", "Tinggi"};
static const char*    const CAM_MODE_NAME[4] = {"FOTO", "VIDEO", "LAPSE", "GERAK"};
static const char*    const CAM_TAB_NAME[4]  = {"Foto", "Gambar", "Layar", "Sistem"};

// ---------------- konfigurasi (disimpan di NVS "cam") ----------------
struct CamCfg {
  uint8_t ver;
  int8_t prv, prvQ, shotRes, shotQ, timer, burst, flash;
  int8_t bright, contrast, sat, sharp, wb, effect, aelevel;
  int8_t mirror, flip, night, lenc, grid, hud;
  int8_t link, xclk, lapse, motion;
};
#define CAMCFG_VER 1
static CamCfg camCfg;
static bool   camCfgLoaded = false;

static void camCfgDefaults() {
  memset(&camCfg, 0, sizeof(camCfg));
  camCfg.ver = CAMCFG_VER;
  camCfg.prv = 1; camCfg.prvQ = 14; camCfg.shotRes = RPR_SVGA; camCfg.shotQ = 10;
  camCfg.hud = 1; camCfg.link = 1; camCfg.xclk = 2; camCfg.lapse = 1; camCfg.motion = 1; camCfg.lenc = 1;
}
static void camCfgLoad() {
  if (camCfgLoaded) return;
  camCfgDefaults();
  Preferences p;
  if (p.begin("cam", true)) {
    CamCfg t;
    if (p.getBytes("cfg", &t, sizeof(t)) == sizeof(t) && t.ver == CAMCFG_VER) camCfg = t;
    p.end();
  }
  camCfgLoaded = true;
}
static void camCfgSave() {
  Preferences p;
  if (p.begin("cam", false)) { p.putBytes("cfg", &camCfg, sizeof(camCfg)); p.end(); }
}

// ---------------- keadaan ----------------
static bool      camActive = false, camBuffersOk = false;
static CamRx     camRx;
static PsramJpeg camJpeg;
static uint8_t*  camSlotBuf[CAMRX_SLOTS] = {nullptr, nullptr, nullptr};
static uint8_t*  camStillBuf = nullptr;
static Orientation camPrevOrient = ORIENT_LANDSCAPE;
static bool      camBoosted = false;

static uint8_t   camMode = 0;                 // 0 foto 1 video 2 lapse 3 gerak
static bool      camSettings = false;
static uint8_t   camTab = 0;

// status dari CAM
static volatile uint8_t  camStat[12];
static volatile uint32_t camStatMs = 0;
static volatile bool     camStatSeen = false;
static volatile uint32_t camLastRxMs = 0;
static bool      camOpened = false, camNoFw = false;
static uint32_t  camOpenReqMs = 0;
static uint8_t   camOpenTries = 0;

// link
static volatile uint32_t camBaudCur = RP_BAUD;
static volatile uint32_t camBaudSwitchedAt = 0;
static volatile bool     camBaudOkFlag = false;
static uint32_t  camBaudSentAt = 0;
static bool      camBaudReq = false, camLinkFailed = false;

// fps / hud
static uint32_t  camFpsT0 = 0, camFpsN = 0, camFpsShow10 = 0, camLastFrameKb = 0;
static bool      camNeedDraw = false;

// foto
static uint32_t  camCdUntil = 0, camNextShotAt = 0, camShotDeadline = 0, camFreezeUntil = 0;
static int       camBurstLeft = 0;
static bool      camShotPending = false;
static int       camSavedCount = 0, camPhotoCounter = 0;
static char      camLastPhoto[64] = "";
static char      camLastVideo[64] = "";

// video / timelapse
static bool      camRec = false, camLapse = false;
static File      camFile;
static CamAviWriter<File> camAvi;
static uint32_t* camIdxOff = nullptr;
static uint32_t* camIdxSz = nullptr;
static uint32_t  camRecStartMs = 0, camLapseNext = 0, camRecBytes = 0;
static char      camRecPath[64] = "";

// gerak
static bool      camMotionOn = false;
static uint8_t*  camMotBuf = nullptr;         // [cur CAM_MOT_MAX][bg CAM_MOT_MAX]
static int       camMotW = 0, camMotH = 0, camMotWarm = 0, camMotHits = 0, camMotCount = 0;
static uint32_t  camMotCool = 0;

// ---------------- util ----------------
static inline uint8_t camPrvCode() { return CAM_PRV_CODE[camCfg.prv < 0 ? 0 : (camCfg.prv > 2 ? 2 : camCfg.prv)]; }

// ---- deteksi sensor (dilaporkan CAM lewat RP_CAM_STAT) ----
static const char* camSensorName(unsigned pid) {
  switch (pid) {
    case 0x26: return "OV2640";   case 0x3660: return "OV3660"; case 0x5640: return "OV5640";
    case 0x96: return "OV9650";   case 0x77: return "OV7725";   case 0x76: return "OV7670";
    default: return "sensor ?";
  }
}
static int camMaxRes() {      // resolusi foto maksimum sensor yang terpasang (RPR_*)
  uint8_t m = camStat[11];
  return (camStatSeen && (camStat[0] & 1) && m >= RPR_UXGA && m <= RPR_QXGA) ? m : RPR_UXGA;
}
static int camEffRes() { int m = camMaxRes(); return camCfg.shotRes > m ? m : camCfg.shotRes; }
static unsigned camPidNow() { return (unsigned)camStat[3] | ((unsigned)camStat[4] << 8); }

static void camU32(uint8_t* o, uint32_t v) { o[0] = v; o[1] = v >> 8; o[2] = v >> 16; o[3] = v >> 24; }

static void camSendSet(uint8_t id, int8_t v) {
  uint8_t pl[2] = {id, (uint8_t)v};
  rpSend(Serial1, RP_CAM_SET, pl, 2);
}
static uint8_t camFlashLevel() { return camCfg.flash == 1 ? 77 : (camCfg.flash == 2 ? 255 : 0); }
static void camSendFlash() {
  uint8_t l = camFlashLevel();
  rpSend(Serial1, RP_CAM_FLASH, &l, 1);
}
static void camPushAll() {
  camSendSet(RPC_BRIGHT, camCfg.bright);   camSendSet(RPC_CONTRAST, camCfg.contrast);
  camSendSet(RPC_SATUR, camCfg.sat);       camSendSet(RPC_SHARP, camCfg.sharp);
  camSendSet(RPC_WBMODE, camCfg.wb);       camSendSet(RPC_EFFECT, camCfg.effect);
  camSendSet(RPC_HMIRROR, camCfg.mirror);  camSendSet(RPC_VFLIP, camCfg.flip);
  camSendSet(RPC_LENC, camCfg.lenc);       camSendSet(RPC_AELEVEL, camCfg.aelevel);
  camSendSet(RPC_NIGHT, camCfg.night);
  camSendFlash();
}
static void camSendOpen() {
  uint8_t pl[4] = {CAM_XCLK_MHZ[camCfg.xclk < 0 ? 0 : (camCfg.xclk > 2 ? 2 : camCfg.xclk)], camPrvCode(), (uint8_t)camCfg.prvQ, 1};
  rpSend(Serial1, RP_CAM_OPEN, pl, 4);
  camOpenReqMs = millis(); camOpenTries++;
}
static void camSendBaud(uint32_t b) {
  uint8_t pl[4]; camU32(pl, b);
  rpSend(Serial1, RP_CAM_BAUD, pl, 4);
}

// ---------------- hook dari musicbt_renphone.ino ----------------
bool camLinkBusy() { return camActive; }
void camNoteRx() { camLastRxMs = millis(); }

void camRxFrame(uint8_t type, const uint8_t* p, uint16_t n) {
  switch (type) {
    case RP_CAM_FRAME:
      if (camActive && camBuffersOk) camRx.feed(p, n);
      break;
    case RP_CAM_STAT:
      if (n >= 12) { for (int i = 0; i < 12; i++) camStat[i] = p[i]; camStatMs = millis(); camStatSeen = true; }
      break;
    case RP_CAM_BAUDOK:
      if (n >= 4) {
        uint32_t b = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (b >= 115200 && b <= 4000000) {
          Serial1.flush();
          Serial1.updateBaudRate(b);
          camBaudCur = b;
          camBaudSwitchedAt = (b != RP_BAUD) ? millis() : 0;
          camBaudOkFlag = true;
        }
      }
      break;
    default: break;
  }
}

// ---------------- buffer ----------------
static void camFreeBuffers() {
  camBuffersOk = false;
  delay(25);                                   // beri musTask waktu keluar dari camRx.feed()
  camRx.attach(nullptr, nullptr, nullptr, CAM_SLOT_SZ);
  camRx.attachStill(nullptr, 0);
  for (int i = 0; i < CAMRX_SLOTS; i++) { if (camSlotBuf[i]) { heap_caps_free(camSlotBuf[i]); camSlotBuf[i] = nullptr; } }
  if (camStillBuf) { heap_caps_free(camStillBuf); camStillBuf = nullptr; }
  if (camMotBuf) { heap_caps_free(camMotBuf); camMotBuf = nullptr; }
}
static bool camAllocBuffers() {
  for (int i = 0; i < CAMRX_SLOTS; i++) {
    camSlotBuf[i] = (uint8_t*)heap_caps_malloc(CAM_SLOT_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!camSlotBuf[i]) { camFreeBuffers(); return false; }
  }
  camRx.attach(camSlotBuf[0], camSlotBuf[1], camSlotBuf[2], CAM_SLOT_SZ);
  camStillBuf = (uint8_t*)heap_caps_malloc(CAM_STILL_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // boleh gagal: preview tetap jalan
  camRx.attachStill(camStillBuf, camStillBuf ? CAM_STILL_SZ : 0);
  camBuffersOk = true;
  return true;
}

// sensor 3MP butuh buffer foto lebih besar: tukar buffer saat idle (tidak sedang menerima foto)
static void camEnsureStill() {
  uint32_t want = (camMaxRes() >= RPR_QXGA) ? CAM_STILL_SZ_BIG : CAM_STILL_SZ;
  if (!camBuffersOk || camShotPending || camRx.stillState == CAMRX_STILL_RECV || camRx.stillCap >= want) return;
  uint8_t* nb = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!nb) return;
  camRx.attachStill(nullptr, 0);
  uint8_t* old = camStillBuf;
  camStillBuf = nb;
  camRx.attachStill(nb, want);
  if (old) heap_caps_free(old);
}

// ---------------- SD ----------------
static void camEnsureDir() { if (sdReady && !SD_MMC.exists(CAM_DIR)) SD_MMC.mkdir(CAM_DIR); }

// bikin nama unik: /Kamera/<prefix>_YYYYMMDD_HHMMSS<ext> (atau _NNNN kalau jam belum sinkron)
static void camMakeName(const char* prefix, const char* ext, char* out, size_t cap) {
  struct tm t; bool ok = ntpSynced && getLocalTime(&t, 0);
  char base[48];
  if (ok) snprintf(base, sizeof(base), "%s_%04d%02d%02d_%02d%02d%02d", prefix, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  else { camPhotoCounter++; snprintf(base, sizeof(base), "%s_%04d", prefix, camPhotoCounter); }
  snprintf(out, cap, "%s/%s%s", CAM_DIR, base, ext);
  for (int k = 2; k < 100 && SD_MMC.exists(out); k++) snprintf(out, cap, "%s/%s_%d%s", CAM_DIR, base, k, ext);
}
static bool camSaveJpeg(const uint8_t* d, uint32_t n, const char* prefix, char* outPath, size_t cap) {
  if (!sdReady) return false;
  camEnsureDir();
  camMakeName(prefix, ".jpg", outPath, cap);
  File f = SD_MMC.open(outPath, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write(d, n);
  f.close();
  if (w != n) { SD_MMC.remove(outPath); return false; }
  return true;
}

// ---------------- video / timelapse ----------------
static void camRecStop(bool quiet) {
  if (!camRec) return;
  camRec = false;
  uint32_t dur = millis() - camRecStartMs;
  bool ok = camAvi.ok;
  if (ok) ok = camAvi.finish(camLapse ? camAvi.frames * 100 : dur);   // timelapse diputar 10 fps
  uint32_t fr = camAvi.frames;
  camFile.close();
  if (camIdxOff) { heap_caps_free(camIdxOff); camIdxOff = nullptr; }
  if (camIdxSz)  { heap_caps_free(camIdxSz);  camIdxSz = nullptr; }
  if (!ok || fr == 0) { SD_MMC.remove(camRecPath); if (!quiet) showToast("Rekaman gagal / kosong", 2000); vibError(); }
  else {
    strncpy(camLastVideo, camRecPath + 1, sizeof(camLastVideo) - 1); camLastVideo[sizeof(camLastVideo) - 1] = 0;   // tanpa '/' depan (format aviPlayFile)
    if (!quiet) {
      char m[64]; snprintf(m, sizeof(m), "%s: %lu frame, %lu dtk", camLapse ? "Timelapse" : "Video", (unsigned long)fr, (unsigned long)(dur / 1000));
      showToast(m, 2400); vibSuccess();
    }
  }
  camLapse = false;
}
static bool camRecStart(bool lapse) {
  if (camRec) return true;
  if (!sdReady) { showToast("Butuh SD card", 1800); vibWarn(); return false; }
  camEnsureDir();
  camIdxOff = (uint32_t*)heap_caps_malloc(CAM_AVI_MAXFR * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  camIdxSz  = (uint32_t*)heap_caps_malloc(CAM_AVI_MAXFR * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!camIdxOff || !camIdxSz) {
    if (camIdxOff) { heap_caps_free(camIdxOff); camIdxOff = nullptr; }
    if (camIdxSz)  { heap_caps_free(camIdxSz);  camIdxSz = nullptr; }
    showToast("RAM tidak cukup utk rekam", 2000); return false;
  }
  camMakeName(lapse ? "LAPSE" : "VID", ".avi", camRecPath, sizeof(camRecPath));
  camFile = SD_MMC.open(camRecPath, FILE_WRITE);
  if (!camFile) { showToast("Gagal buka file di SD", 2000); return false; }
  uint8_t pc = camPrvCode();
  if (!camAvi.begin(&camFile, CAM_RES_W[pc], CAM_RES_H[pc], camIdxOff, camIdxSz, CAM_AVI_MAXFR)) { camFile.close(); showToast("SD error", 2000); return false; }
  camRec = true; camLapse = lapse; camRecStartMs = millis(); camLapseNext = 0; camRecBytes = 0;
  vibTap();
  return true;
}
static void camRecWrite(const uint8_t* d, uint32_t n) {
  if (!camRec) return;
  if (!camAvi.addFrame(d, n) || camAvi.full()) { bool full = camAvi.full() && camAvi.ok; camRecStop(true); showToast(full ? "Rekaman penuh, disimpan" : "SD penuh / error", 2200); return; }
  camRecBytes += n;
}

// ---------------- deteksi gerak ----------------
static int camMotCb(JPEGDRAW* d) {
  if (!camMotBuf) return 0;
  for (int r = 0; r < d->iHeight; r++) {
    int y = d->y + r; if (y >= camMotH) break;
    for (int c = 0; c < d->iWidth; c++) {
      int x = d->x + c; if (x >= camMotW) break;
      int idx = y * camMotW + x; if (idx >= CAM_MOT_MAX) continue;
      uint16_t p = d->pPixels[r * d->iWidth + c];
      int R = (p >> 11) & 31, G = (p >> 5) & 63, B = p & 31;
      camMotBuf[idx] = (uint8_t)((R * 8 * 77 + G * 4 * 150 + B * 8 * 29) >> 8);
    }
  }
  return 1;
}
static void camMotionReset() { camMotWarm = 0; camMotHits = 0; camMotW = camMotH = 0; }
static void camMotionAnalyze(const uint8_t* d, uint32_t n, uint32_t now) {
  if (!camMotBuf) return;
  if (!camJpeg->openRAM((uint8_t*)d, (int)n, camMotCb)) return;
  int w = camJpeg->getWidth(), h = camJpeg->getHeight();
  int gw = (w + 7) >> 3, gh = (h + 7) >> 3;
  if (gw * gh > CAM_MOT_MAX) { camJpeg->close(); return; }
  if (gw != camMotW || gh != camMotH) { camMotW = gw; camMotH = gh; camMotWarm = 0; camMotHits = 0; }
  camJpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  int ok = camJpeg->decode(0, 0, JPEG_SCALE_EIGHTH);
  camJpeg->close();
  if (!ok) return;
  uint8_t* cur = camMotBuf; uint8_t* bg = camMotBuf + CAM_MOT_MAX;
  int N = gw * gh;
  if (camMotWarm < 12) { for (int i = 0; i < N; i++) bg[i] = (camMotWarm == 0) ? cur[i] : (uint8_t)((bg[i] + cur[i]) >> 1); camMotWarm++; return; }
  long sum = 0; for (int i = 0; i < N; i++) sum += (int)cur[i] - (int)bg[i];
  int md = (int)(sum / N);                                  // kompensasi perubahan cahaya global (AE)
  static const uint8_t thr[3] = {26, 18, 12};
  static const uint8_t frac[3] = {80, 40, 15};              // permil sel yang berubah
  int s = camCfg.motion < 0 ? 0 : (camCfg.motion > 2 ? 2 : camCfg.motion);
  int cnt = 0;
  for (int i = 0; i < N; i++) { int df = (int)cur[i] - (int)bg[i] - md; if (df < 0) df = -df; if (df > thr[s]) cnt++; }
  bool mv = (cnt * 1000 / N) > frac[s];
  camMotHits = mv ? camMotHits + 1 : 0;
  if (!mv) for (int i = 0; i < N; i++) bg[i] = (uint8_t)((int)bg[i] + (((int)cur[i] - (int)bg[i]) >> 3));
  if (camMotHits >= 2 && (int32_t)(now - camMotCool) >= 0) {
    camMotCool = now + 6000; camMotHits = 0; camMotCount++;
    char path[64];
    if (camSaveJpeg(d, n, "GERAK", path, sizeof(path))) { strncpy(camLastPhoto, path, sizeof(camLastPhoto) - 1); camSavedCount++; }
    showToast("Gerak terdeteksi!", 1500); vibWarn();
    for (int i = 0; i < N; i++) bg[i] = cur[i];
  }
}
static void camMotionSet(bool on) {
  if (on == camMotionOn) return;
  if (on) {
    if (!camMotBuf) camMotBuf = (uint8_t*)heap_caps_malloc(CAM_MOT_MAX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!camMotBuf) { showToast("RAM tidak cukup", 1500); return; }
    camMotionReset(); camMotCool = millis() + 1500; camMotCount = 0;
  }
  camMotionOn = on;
}

// ---------------- foto ----------------
static void camShootNow() {
  if (!camOpened) { showToast("Kamera belum siap", 1200); return; }
  if (!sdReady) { showToast("Butuh SD card", 1800); vibWarn(); camBurstLeft = 0; return; }
  if (!camStillBuf) { showToast("RAM foto tidak cukup", 1800); camBurstLeft = 0; return; }
  if (camShotPending) return;
  camFreezeUntil = 0;
  camRx.armStill();
  uint8_t pl[4] = {(uint8_t)camEffRes(), (uint8_t)camCfg.shotQ, (uint8_t)(camCfg.flash == 3 ? 255 : 0), 0};
  rpSend(Serial1, RP_CAM_SHOT, pl, 4);
  camShotPending = true;
  camShotDeadline = millis() + 15000;
}
static void camStartShoot() {
  camBurstLeft = CAM_BURST_N[camCfg.burst < 0 ? 0 : (camCfg.burst > 2 ? 2 : camCfg.burst)];
  uint8_t ts = CAM_TIMER_S[camCfg.timer < 0 ? 0 : (camCfg.timer > 2 ? 2 : camCfg.timer)];
  if (ts) { camCdUntil = millis() + ts * 1000UL; vibTap(); }
  else camShootNow();
}
static void camStillTick(uint32_t now) {
  if (camCdUntil && (int32_t)(now - camCdUntil) >= 0) { camCdUntil = 0; camShootNow(); }
  if (camBurstLeft > 0 && !camShotPending && !camCdUntil && camNextShotAt && (int32_t)(now - camNextShotAt) >= 0) { camNextShotAt = 0; camShootNow(); }
  if (!camShotPending) return;
  uint8_t st = camRx.stillState;
  if (st == CAMRX_STILL_DONE) {
    char path[64];
    bool ok = camSaveJpeg(camStillBuf, camRx.stillLen, "IMG", path, sizeof(path));
    camShotPending = false;
    if (ok) {
      strncpy(camLastPhoto, path, sizeof(camLastPhoto) - 1); camSavedCount++;
      camFreezeUntil = now + 1100; camNeedDraw = true;
      vibSuccess();
      if (--camBurstLeft > 0) camNextShotAt = now + 1400; else camBurstLeft = 0;
    } else { showToast("Gagal menulis ke SD", 2000); vibError(); camBurstLeft = 0; }
    camRx.disarmStill();
  } else if (st == CAMRX_STILL_FAIL || (int32_t)(now - camShotDeadline) >= 0) {
    camShotPending = false; camBurstLeft = 0; camRx.disarmStill();
    showToast(st == CAMRX_STILL_FAIL ? "Foto rusak/terlalu besar" : "Foto gagal (timeout)", 2200);
    vibError(); camNeedDraw = true;
  }
}

// ---------------- link & buka kamera ----------------
static void camLinkTick(uint32_t now) {
  if (camBaudSwitchedAt) {
    if (camLastRxMs > camBaudSwitchedAt + 200) camBaudSwitchedAt = 0;                      // terbukti ada frame valid -> baud baru OK
    else if (now - camBaudSwitchedAt > 1500) {                                              // sunyi -> mundur ke baud aman
      Serial1.updateBaudRate(RP_BAUD); camBaudCur = RP_BAUD; camBaudSwitchedAt = 0; camLinkFailed = true; camBaudReq = false;
      showToast("Link cepat gagal, pakai 921k", 2200);
    }
  }
  if (camBaudOkFlag) { camBaudOkFlag = false; camBaudReq = false; }
  if (camOpened && !camLinkFailed && !camBaudReq && camCfg.link > 0 && camBaudCur == RP_BAUD) {
    camSendBaud(CAM_LINK_BAUD[camCfg.link]); camBaudReq = true; camBaudSentAt = now;
  }
  if (camBaudReq && now - camBaudSentAt > 1500 && camBaudCur == RP_BAUD) { camBaudReq = false; camLinkFailed = true; }   // CAM tak menjawab (firmware lama?)
}
static void camOpenTick(uint32_t now) {
  bool open = camStatSeen && (camStat[0] & 1);
  if (open && !camOpened) {
    camOpened = true; camPushAll(); camFpsT0 = now; camFpsN = 0; camNeedDraw = true;
    camEnsureStill();
    char m[48]; snprintf(m, sizeof(m), "%s - foto maks %s", camSensorName(camPidNow()), CAM_RES_NAME[camMaxRes()]);
    showToast(m, 2200);
  }
  if (!open && camOpened && camStatSeen && now - camStatMs < 1000) camOpened = false;     // CAM menutup kamera sendiri
  if (camOpened) return;
  if (camStatSeen && camStat[1] && camStat[1] != 6 && camStat[1] != 5) return;           // error permanen: biarkan pesan tampil
  if (now - camOpenReqMs > 1500) {
    if (camOpenTries < 4) { camSendOpen(); camNeedDraw = true; }
    else if (!camStatSeen) { camNoFw = true; camNeedDraw = true; }
  }
}

// ---------------- siklus hidup ----------------
void camEnter() {
  camCfgLoad();
  camSettings = false; camMode = 0; camTab = 0;
  camShotPending = false; camCdUntil = 0; camBurstLeft = 0; camFreezeUntil = 0; camNextShotAt = 0;
  camStatSeen = false; camOpened = false; camNoFw = false; camOpenTries = 0;
  camBaudReq = false; camLinkFailed = false; camBaudSwitchedAt = 0;
  camSavedCount = 0; camMotionOn = false;
  camPrevOrient = currentOrient;
  if (currentOrient != ORIENT_LANDSCAPE) applyOrientation(ORIENT_LANDSCAPE, false);
  if (!gameModeActive) { gameModeActive = true; setCpuFrequencyMhz(CPU_MHZ_GAME); camBoosted = true; }
  if (!camAllocBuffers()) { showToast("RAM tidak cukup utk Kamera", 2500); camActive = true; return; }
  musExtPrepare();                                 // nyalakan link UART ke CAM (idempoten)
  camLastRxMs = millis();
  camActive = true;
  camSendOpen();
  camNeedDraw = true; needRedraw = true;
}
void camExit() {
  camRecStop(true);
  camMotionSet(false);
  if (camBuffersOk) {
    rpSend(Serial1, RP_CAM_CLOSE, nullptr, 0);
    if (camBaudCur != RP_BAUD) camSendBaud(RP_BAUD);     // turunkan link; musTask memprosesnya (camRxFrame)
  }
  camActive = false;
  camFreeBuffers();
  camCfgSave();
  if (camBoosted) { camBoosted = false; exitGameMode(); }
  if (camPrevOrient != ORIENT_LANDSCAPE) applyOrientation(camPrevOrient, false);
}

// ---------------- loop ----------------
void camLoopPoll() {
  if (!camActive || !camBuffersOk) return;
  uint32_t now = millis();
  camLinkTick(now);
  camOpenTick(now);
  camStillTick(now);

  if (camFreezeUntil && (int32_t)(now - camFreezeUntil) >= 0) { camFreezeUntil = 0; camNeedDraw = true; }

  bool wantFrames = camOpened && (!camSettings || camRec || camMotionOn);
  if (wantFrames) {
    int i = camRx.take();
    if (i >= 0) {
      const uint8_t* d = camRx.data(i); uint32_t n = camRx.len(i);
      uint8_t seq = 0; rpSend(Serial1, RP_CAM_ACK, &seq, 1);       // kredit +1 -> CAM kirim frame berikutnya
      camFpsN++; camLastFrameKb = n / 1024;
      if (camRec && !camLapse) camRecWrite(d, n);
      else if (camRec && camLapse && (int32_t)(now - camLapseNext) >= 0) { camRecWrite(d, n); camLapseNext = now + CAM_LAPSE_S[camCfg.lapse < 0 ? 0 : (camCfg.lapse > 4 ? 4 : camCfg.lapse)] * 1000UL; }
      if (camMotionOn) camMotionAnalyze(d, n, now);
      if (!camFreezeUntil && !camSettings) camNeedDraw = true;
    }
  }
  if (now - camFpsT0 >= 1000) { camFpsShow10 = camFpsN * 10000UL / (now - camFpsT0); camFpsN = 0; camFpsT0 = now; if (camCfg.hud || camRec) camNeedDraw = true; }
  if (camRec || camCdUntil) { static uint32_t lastSec = 0; if (now - lastSec >= 250) { lastSec = now; camNeedDraw = true; } }
  if (camNeedDraw) { camNeedDraw = false; needRedraw = true; }
}

// ---------------- gambar ----------------
static LGFX_Sprite* camDst = nullptr;
static int camZoom = 1, camOx = 0, camOy = 0;
static int camJpgCb(JPEGDRAW* d) {
  if (!camDst) return 0;
  if (camZoom == 1) {
    camDst->pushImage(d->x, d->y, d->iWidth, d->iHeight, (lgfx::swap565_t*)d->pPixels);
  } else {
    for (int r = 0; r < d->iHeight; r++)
      for (int c = 0; c < d->iWidth; c++) {
        uint16_t p = d->pPixels[r * d->iWidth + c]; p = (p >> 8) | (p << 8);
        camDst->fillRect(camOx + (d->x + c) * 2, camOy + (d->y + r) * 2, 2, 2, p);
      }
  }
  return 1;
}
static bool camDrawJpeg(LGFX_Sprite& s, const uint8_t* d, uint32_t n) {
  camDst = &s;
  if (!camJpeg->openRAM((uint8_t*)d, (int)n, camJpgCb)) return false;
  int w = camJpeg->getWidth(), h = camJpeg->getHeight();
  int sh = 0; while (sh < 3 && ((w >> sh) > SCR_W || (h >> sh) > SCR_H)) sh++;
  int sc = sh == 0 ? 0 : (sh == 1 ? JPEG_SCALE_HALF : (sh == 2 ? JPEG_SCALE_QUARTER : JPEG_SCALE_EIGHTH));
  camZoom = (w <= 160 && h <= 120) ? 2 : 1;
  int dw = (w >> sh) * camZoom, dh = (h >> sh) * camZoom;
  camOx = (SCR_W - dw) / 2; camOy = (SCR_H - dh) / 2;
  if (camOx < 0) camOx = 0;
  if (camOy < 0) camOy = 0;
  camJpeg->setPixelType(RGB565_BIG_ENDIAN);
  int ok = camJpeg->decode(camZoom == 1 ? camOx : 0, camZoom == 1 ? camOy : 0, sc);
  camJpeg->close();
  return ok != 0;
}

static void camTextC(LGFX_Sprite& s, const char* t, int cx, int y) { int w = s.textWidth(t); s.setCursor(cx - w / 2, y); s.print(t); }
static void camBtn(LGFX_Sprite& s, int x, int y, int w, int h, const char* label, bool on) {
  s.fillRoundRect(x, y, w, h, 6, on ? (uint16_t)T().accent : (uint16_t)0x2104);
  s.setTextColor(on ? (uint16_t)TFT_BLACK : (uint16_t)TFT_WHITE);
  camTextC(s, label, x + w / 2, y + (h - 8) / 2);
}

#define CAM_RAIL_X 266
static bool camIn(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void camStatusMsg(char* o, size_t cap) {
  uint8_t e = camStat[1];
  if (camNoFw) snprintf(o, cap, "CAM belum mendukung kamera.\nFlash ulang cam_bt_coprocessor.");
  else if (!camStatSeen) snprintf(o, cap, "Menghubungi ESP32-CAM...");
  else if (e == 1) snprintf(o, cap, "ESP32-CAM tanpa PSRAM.");
  else if (e == 2) snprintf(o, cap, "RAM CAM kurang.\nMatikan Bluetooth/musik lalu coba lagi.");
  else if (e == 7) snprintf(o, cap, "Sensor kamera tidak terdeteksi.\nCek kabel flat & pemasangan modul.");
  else if (e == 4) snprintf(o, cap, "Sensor ini tidak mendukung JPEG.\n(Dukungan: OV2640/OV3660/OV5640)");
  else if (e == 3) snprintf(o, cap, "Kamera gagal init (0x%02X).\nCek modul kamera & daya 5V.", camStat[2]);
  else snprintf(o, cap, "Menyalakan kamera...");
}

static void camDrawMain(LGFX_Sprite& s) {
  uint32_t now = millis();
  s.fillScreen(TFT_BLACK);
  bool shown = false;
  if (camFreezeUntil && camStillBuf && camRx.stillState != CAMRX_STILL_RECV && camRx.stillLen > 0) shown = camDrawJpeg(s, camStillBuf, camRx.stillLen);
  if (!shown && camOpened) { int c = camRx.current(); if (c >= 0) shown = camDrawJpeg(s, camRx.data(c), camRx.len(c)); }
  if (!shown) {
    char m[96]; camStatusMsg(m, sizeof(m));
    s.setTextColor(camStatSeen && camStat[1] && camStat[1] != 5 && camStat[1] != 6 ? (uint16_t)T().danger : (uint16_t)TFT_WHITE);
    char* ln = m; int y = SCR_H / 2 - 12;
    while (ln && *ln) { char* nl = strchr(ln, '\n'); if (nl) *nl = 0; camTextC(s, ln, (CAM_RAIL_X) / 2, y); y += 14; ln = nl ? nl + 1 : nullptr; }
  }
  // grid 3x3
  if (camCfg.grid && shown) {
    uint16_t g = 0xFFFF;
    for (int i = 1; i < 3; i++) { s.drawFastVLine(CAM_RAIL_X * i / 3, 0, SCR_H, g); s.drawFastHLine(0, SCR_H * i / 3, CAM_RAIL_X, g); }
  }
  // rail kanan
  s.fillRectAlpha(CAM_RAIL_X, 0, SCR_W - CAM_RAIL_X, SCR_H, 150, TFT_BLACK);
  s.setTextSize(1);
  camBtn(s, CAM_RAIL_X + 4, 6, 46, 28, CAM_MODE_NAME[camMode], false);
  int cx = CAM_RAIL_X + 27, cy = 112;
  bool busy = camRec || camMotionOn || camShotPending || camCdUntil;
  uint16_t sc = (camRec || camMotionOn) ? (uint16_t)T().danger : (uint16_t)TFT_WHITE;
  s.fillCircle(cx, cy, 26, 0x2104);
  s.drawCircle(cx, cy, 25, sc); s.drawCircle(cx, cy, 24, sc);
  if (camRec) s.fillRoundRect(cx - 9, cy - 9, 18, 18, 3, sc);
  else if (camMotionOn) { s.fillCircle(cx, cy, 10, sc); }
  else if (busy) { s.drawCircle(cx, cy, 14, sc); }
  else s.fillCircle(cx, cy, 17, camMode == 0 ? (uint16_t)TFT_WHITE : (uint16_t)T().danger);
  camBtn(s, CAM_RAIL_X + 4, 154, 46, 26, camCfg.flash == 0 ? "Flash" : (camCfg.flash == 3 ? "Kilat" : "Senter"), camCfg.flash != 0);
  camBtn(s, CAM_RAIL_X + 4, 186, 46, 26, "Atur", false);
  // kiri atas: back + HUD
  s.fillRoundRect(4, 4, 30, 22, 6, 0x2104); s.setTextColor(TFT_WHITE); camTextC(s, "<", 19, 11);
  if (camCfg.hud && camOpened) {
    char h[64]; uint8_t pc = camPrvCode();
    snprintf(h, sizeof(h), "%s %lu.%lufps %luK%s", CAM_RES_NAME[pc], (unsigned long)(camFpsShow10 / 10), (unsigned long)(camFpsShow10 % 10), (unsigned long)camLastFrameKb,
             camBaudCur != RP_BAUD ? " HI" : "");
    s.fillRect(38, 6, s.textWidth(h) + 6, 14, 0x0000); s.setTextColor(TFT_WHITE); s.setCursor(41, 9); s.print(h);
  }
  // bawah kiri: galeri / putar video / jumlah
  camBtn(s, 4, SCR_H - 30, 52, 26, "Galeri", false);
  if (camLastVideo[0]) camBtn(s, 60, SCR_H - 30, 40, 26, "Putar", false);
  if (camSavedCount) { char c[16]; snprintf(c, sizeof(c), "+%d", camSavedCount); s.setTextColor(TFT_WHITE); s.setCursor(104, SCR_H - 22); s.print(c); }
  // status tengah
  if (camRec) {
    uint32_t sec = (now - camRecStartMs) / 1000; char r[48];
    if (camLapse) snprintf(r, sizeof(r), "LAPSE %lu f", (unsigned long)camAvi.frames);
    else snprintf(r, sizeof(r), "REC %02lu:%02lu  %luK", (unsigned long)(sec / 60), (unsigned long)(sec % 60), (unsigned long)(camRecBytes / 1024));
    s.fillRoundRect(CAM_RAIL_X / 2 - 50, 4, 100, 18, 6, T().danger); s.setTextColor(TFT_WHITE); camTextC(s, r, CAM_RAIL_X / 2, 9);
  } else if (camMotionOn) {
    char r[40]; snprintf(r, sizeof(r), "GERAK AKTIF  %d", camMotCount);
    s.fillRoundRect(CAM_RAIL_X / 2 - 60, 4, 120, 18, 6, T().danger); s.setTextColor(TFT_WHITE); camTextC(s, r, CAM_RAIL_X / 2, 9);
  } else if (camShotPending) {
    s.fillRoundRect(CAM_RAIL_X / 2 - 50, 4, 100, 18, 6, 0x2104); s.setTextColor(TFT_WHITE); camTextC(s, "Mengambil foto...", CAM_RAIL_X / 2, 9);
  }
  if (camCdUntil) {
    int left = (int)((camCdUntil - now + 999) / 1000); char b[8]; snprintf(b, sizeof(b), "%d", left < 0 ? 0 : left);
    s.setTextSize(6); s.setTextColor(TFT_WHITE); int w = s.textWidth(b); s.setCursor(CAM_RAIL_X / 2 - w / 2, SCR_H / 2 - 24); s.print(b); s.setTextSize(1);
  }
  drawToast(s);
}

// ---------------- pengaturan ----------------
// id baris global; tiap tab punya daftar id
static const int8_t CAM_TAB_ROWS[4][8] = {
  {0, 1, 2, 3, 4, -1, -1, -1},          // Foto: res, kualitas, timer, beruntun, flash
  {5, 6, 7, 8, 9, 10, 11, 12},          // Gambar
  {13, 14, 15, 16, 17, 18, 19, -1},     // Layar
  {20, 21, 22, 23, 24, -1, -1, -1}      // Sistem
};
static const char* const CAM_ROW_LABEL[25] = {
  "Resolusi foto", "Kualitas foto", "Timer", "Beruntun", "Flash/senter",
  "Kecerahan", "Kontras", "Saturasi", "Ketajaman", "White balance", "Efek", "Mode malam", "Eksposur",
  "Mirror", "Flip", "Garis bantu", "HUD", "Preview", "Kualitas preview", "Koreksi lensa",
  "Link UART", "Clock sensor", "Interval lapse", "Sensitivitas gerak", "Reset pengaturan"
};
static void camRowValue(int r, char* o, size_t cap) {
  switch (r) {
    case 0: snprintf(o, cap, "%s", CAM_RES_NAME[camEffRes()]); break;
    case 1: snprintf(o, cap, "Q%d%s", camCfg.shotQ, camCfg.shotQ <= 8 ? " tinggi" : (camCfg.shotQ >= 16 ? " hemat" : "")); break;
    case 2: if (CAM_TIMER_S[camCfg.timer]) snprintf(o, cap, "%d dtk", CAM_TIMER_S[camCfg.timer]); else snprintf(o, cap, "Mati"); break;
    case 3: snprintf(o, cap, "%dx", CAM_BURST_N[camCfg.burst]); break;
    case 4: snprintf(o, cap, "%s", CAM_FLASH_NAME[camCfg.flash]); break;
    case 5: snprintf(o, cap, "%+d", camCfg.bright); break;
    case 6: snprintf(o, cap, "%+d", camCfg.contrast); break;
    case 7: snprintf(o, cap, "%+d", camCfg.sat); break;
    case 8: snprintf(o, cap, "%+d", camCfg.sharp); break;
    case 9: snprintf(o, cap, "%s", CAM_WB_NAME[camCfg.wb]); break;
    case 10: snprintf(o, cap, "%s", CAM_FX_NAME[camCfg.effect]); break;
    case 11: snprintf(o, cap, "%s", camCfg.night ? "Nyala" : "Mati"); break;
    case 12: snprintf(o, cap, "%+d", camCfg.aelevel); break;
    case 13: snprintf(o, cap, "%s", camCfg.mirror ? "Nyala" : "Mati"); break;
    case 14: snprintf(o, cap, "%s", camCfg.flip ? "Nyala" : "Mati"); break;
    case 15: snprintf(o, cap, "%s", camCfg.grid ? "Nyala" : "Mati"); break;
    case 16: snprintf(o, cap, "%s", camCfg.hud ? "Nyala" : "Mati"); break;
    case 17: snprintf(o, cap, "%s", CAM_PRV_NAME[camCfg.prv]); break;
    case 18: snprintf(o, cap, "Q%d", camCfg.prvQ); break;
    case 19: snprintf(o, cap, "%s", camCfg.lenc ? "Nyala" : "Mati"); break;
    case 20: snprintf(o, cap, "%s", CAM_LINK_NAME[camCfg.link]); break;
    case 21: snprintf(o, cap, "%d MHz", CAM_XCLK_MHZ[camCfg.xclk]); break;
    case 22: snprintf(o, cap, "%d dtk", CAM_LAPSE_S[camCfg.lapse]); break;
    case 23: snprintf(o, cap, "%s", CAM_SENS_NAME[camCfg.motion]); break;
    default: o[0] = 0; break;
  }
}
static int8_t camStepv(int8_t v, int dir, int lo, int hi, int step, bool wrap) {
  int n = v + dir * step;
  if (n > hi) n = wrap ? lo : hi;
  if (n < lo) n = wrap ? hi : lo;
  return (int8_t)n;
}
static void camReopen() {                         // dipakai saat clock sensor berubah (butuh init ulang)
  rpSend(Serial1, RP_CAM_CLOSE, nullptr, 0);
  camOpened = false; camStatSeen = false; camOpenTries = 0; camOpenReqMs = millis() - 1400;
}
static void camAdj(int r, int dir) {
  switch (r) {
    case 0: camCfg.shotRes = camStepv(camEffRes(), dir, RPR_QVGA, camMaxRes(), 1, true); break;
    case 1: camCfg.shotQ = camStepv(camCfg.shotQ, dir, 4, 30, 2, false); break;
    case 2: camCfg.timer = camStepv(camCfg.timer, dir, 0, 2, 1, true); break;
    case 3: camCfg.burst = camStepv(camCfg.burst, dir, 0, 2, 1, true); break;
    case 4: camCfg.flash = camStepv(camCfg.flash, dir, 0, 3, 1, true); camSendFlash(); break;
    case 5: camCfg.bright = camStepv(camCfg.bright, dir, -2, 2, 1, false); camSendSet(RPC_BRIGHT, camCfg.bright); break;
    case 6: camCfg.contrast = camStepv(camCfg.contrast, dir, -2, 2, 1, false); camSendSet(RPC_CONTRAST, camCfg.contrast); break;
    case 7: camCfg.sat = camStepv(camCfg.sat, dir, -2, 2, 1, false); camSendSet(RPC_SATUR, camCfg.sat); break;
    case 8: camCfg.sharp = camStepv(camCfg.sharp, dir, -2, 2, 1, false); camSendSet(RPC_SHARP, camCfg.sharp); break;
    case 9: camCfg.wb = camStepv(camCfg.wb, dir, 0, 4, 1, true); camSendSet(RPC_WBMODE, camCfg.wb); break;
    case 10: camCfg.effect = camStepv(camCfg.effect, dir, 0, 6, 1, true); camSendSet(RPC_EFFECT, camCfg.effect); break;
    case 11: camCfg.night ^= 1; camSendSet(RPC_NIGHT, camCfg.night); break;
    case 12: camCfg.aelevel = camStepv(camCfg.aelevel, dir, -2, 2, 1, false); camSendSet(RPC_AELEVEL, camCfg.aelevel); break;
    case 13: camCfg.mirror ^= 1; camSendSet(RPC_HMIRROR, camCfg.mirror); break;
    case 14: camCfg.flip ^= 1; camSendSet(RPC_VFLIP, camCfg.flip); break;
    case 15: camCfg.grid ^= 1; break;
    case 16: camCfg.hud ^= 1; break;
    case 17: camCfg.prv = camStepv(camCfg.prv, dir, 0, 2, 1, true); camSendSet(RPC_PRVRES, camPrvCode()); camMotionReset(); break;
    case 18: camCfg.prvQ = camStepv(camCfg.prvQ, dir, 8, 30, 2, false); camSendSet(RPC_PRVQ, camCfg.prvQ); break;
    case 19: camCfg.lenc ^= 1; camSendSet(RPC_LENC, camCfg.lenc); break;
    case 20: camCfg.link = camStepv(camCfg.link, dir, 0, 2, 1, true);
             camLinkFailed = false; camBaudReq = false;
             if (camBaudCur != RP_BAUD) camSendBaud(camCfg.link ? CAM_LINK_BAUD[camCfg.link] : RP_BAUD);     // ganti langsung (tanpa tutup kamera)
             break;
    case 21: camCfg.xclk = camStepv(camCfg.xclk, dir, 0, 2, 1, true); camReopen(); break;
    case 22: camCfg.lapse = camStepv(camCfg.lapse, dir, 0, 4, 1, true); break;
    case 23: camCfg.motion = camStepv(camCfg.motion, dir, 0, 2, 1, true); break;
    case 24: { int8_t keepLink = camCfg.link; camCfgDefaults(); camCfg.link = keepLink; camPushAll(); camSendSet(RPC_PRVRES, camPrvCode()); camSendSet(RPC_PRVQ, camCfg.prvQ); showToast("Pengaturan direset", 1200); break; }
    default: break;
  }
}

#define CAM_SET_Y0   48
#define CAM_SET_RH   25
static void camDrawSettings(LGFX_Sprite& s) {
  iosBackdrop(s);
  s.setTextWrap(false); s.setTextSize(1);
  s.setTextColor(T().accent); s.setCursor(10, 8); s.print("Pengaturan Kamera");
  s.fillRoundRect(SCR_W - 54, 4, 48, 22, 6, T().accent); s.setTextColor(TFT_BLACK); camTextC(s, "Selesai", SCR_W - 30, 11);
  for (int t = 0; t < 4; t++) {
    int x = 8 + t * 76; bool on = (t == camTab);
    s.fillRoundRect(x, 28, 72, 17, 6, on ? (uint16_t)T().accent : (uint16_t)T().surface);
    s.setTextColor(on ? (uint16_t)TFT_BLACK : (uint16_t)T().text); camTextC(s, CAM_TAB_NAME[t], x + 36, 33);
  }
  int y = CAM_SET_Y0;
  for (int i = 0; i < 8; i++) {
    int r = CAM_TAB_ROWS[camTab][i]; if (r < 0) break;
    s.fillRoundRect(8, y, SCR_W - 16, CAM_SET_RH - 3, 6, T().surface);
    s.setTextColor(T().text); s.setCursor(16, y + 8); s.print(CAM_ROW_LABEL[r]);
    if (r == 24) { s.setTextColor(T().danger); s.setCursor(SCR_W - 70, y + 8); s.print("Tekan"); }
    else {
      char v[28]; camRowValue(r, v, sizeof(v));
      s.setTextColor(T().accent); int w = s.textWidth(v);
      s.setCursor(SCR_W - 30 - w, y + 8); s.print(v);
      s.setTextColor(T().subtext); s.setCursor(SCR_W - 24 - w - 8, y + 8); s.print("<");
      s.setCursor(SCR_W - 22, y + 8); s.print(">");
    }
    y += CAM_SET_RH;
  }
  if (camTab == 3) {
    int iy = y + 4; s.setTextColor(T().subtext);
    char l[80];
    snprintf(l, sizeof(l), "%s  heap CAM %uK (blok %uK)", camSensorName(camPidNow()), camStat[5], camStat[6]); s.setCursor(12, iy); s.print(l); iy += 11;
    snprintf(l, sizeof(l), "CAM %u.%u fps  rx %lu drop %lu rusak %lu", (camStat[7] | (camStat[8] << 8)) / 10, (camStat[7] | (camStat[8] << 8)) % 10,
             (unsigned long)camRx.frames, (unsigned long)camRx.dropped, (unsigned long)(camRx.bad + camRx.oversize)); s.setCursor(12, iy); s.print(l); iy += 11;
    snprintf(l, sizeof(l), "Link %lu baud%s", (unsigned long)camBaudCur, camLinkFailed ? " (cepat gagal)" : ""); s.setCursor(12, iy); s.print(l);
  }
  drawToast(s);
}

void drawCamera(LGFX_Sprite& s) {
  if (camSettings) camDrawSettings(s); else camDrawMain(s);
}

// ---------------- sentuh ----------------
static void camShutterTap() {
  switch (camMode) {
    case 0: if (camShotPending || camCdUntil) { camCdUntil = 0; camBurstLeft = 0; vibTap(); } else camStartShoot(); break;
    case 1: if (camRec) camRecStop(false); else if (camOpened) camRecStart(false); break;
    case 2: if (camRec) camRecStop(false); else if (camOpened) camRecStart(true); break;
    case 3: camMotionSet(!camMotionOn); vibTap(); break;
  }
}
static void camPlayLast() {
  if (!camLastVideo[0] || camRec) return;
  String path = String(camLastVideo);
  camExit();                                       // lepas RAM + kamera selama putar
  aviPlayFile(path);
  camEnter();                                      // kembali ke kamera
}

void camTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (camSettings) {
    if (camIn(x, y, SCR_W - 56, 2, 54, 26)) { camSettings = false; camCfgSave(); vibTap(); needRedraw = true; return; }
    if (y >= 28 && y < 46) { int t = (x - 8) / 76; if (t >= 0 && t < 4) { camTab = t; vibTap(); needRedraw = true; } return; }
    int i = (y - CAM_SET_Y0) / CAM_SET_RH;
    if (y >= CAM_SET_Y0 && i >= 0 && i < 8 && CAM_TAB_ROWS[camTab][i] >= 0) {
      int r = CAM_TAB_ROWS[camTab][i];
      // Panah "<" dan ">" digambar di kiri/kanan NILAI (dekat tepi kanan), jadi batas turun/naik = tengah nilai,
      // bukan tengah layar (dulu tap "<" malah dianggap "+", jadi Q30 tidak bisa diturunkan lewat panahnya).
      char cv[28]; camRowValue(r, cv, sizeof(cv));
      int cw = (int)strlen(cv) * 6;
      camAdj(r, (r != 24 && x < SCR_W - 30 - cw / 2) ? -1 : 1);
      vibTap(); needRedraw = true;
    }
    return;
  }
  if (camIn(x, y, 0, 0, 40, 30)) { navBack(); return; }
  if (camIn(x, y, CAM_RAIL_X, 4, SCR_W - CAM_RAIL_X, 32)) { if (!camRec && !camMotionOn) { camMode = (camMode + 1) & 3; vibTap(); needRedraw = true; } else showToast("Hentikan dulu", 900); return; }
  if (camIn(x, y, CAM_RAIL_X, 82, SCR_W - CAM_RAIL_X, 62)) { camShutterTap(); needRedraw = true; return; }
  if (camIn(x, y, CAM_RAIL_X, 152, SCR_W - CAM_RAIL_X, 30)) { camAdj(4, 1); vibTap(); needRedraw = true; return; }
  if (camIn(x, y, CAM_RAIL_X, 184, SCR_W - CAM_RAIL_X, 30)) { camSettings = true; vibTap(); needRedraw = true; return; }
  if (camIn(x, y, 0, SCR_H - 34, 58, 34)) { camRecStop(true); navPush(SCR_GALLERY); return; }
  if (camLastVideo[0] && camIn(x, y, 58, SCR_H - 34, 46, 34)) { camPlayLast(); needRedraw = true; return; }
}
