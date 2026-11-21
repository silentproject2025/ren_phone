// =====================================================================
// nyxapp_renphone.ino -- "Nyx Apps": jalankan app NATIVE (ELF) dari SD atau FFat
// =====================================================================
// Setiap app = folder  /apps/<id>/  di SD berisi:
//     manifest.txt   (nama, ikon, izin, quota memori)  -> lihat nyxhost.h
//     app.elf        (hasil build ESP-IDF `idf.py elf`)  -> lihat docs/features/native-apps.md
//     data/          (dibuat otomatis; satu-satunya folder yang boleh disentuh app)
//
// ARSITEKTUR (kebijakan ada di nyxhost.h/nyxmem.h, diuji di PC; file ini cuma LEM):
//   * app jalan di TASK sendiri (stack PSRAM, core 0, prioritas 2). UI (loopTask)
//     memanggilnya per frame lalu MENUNGGU maksimal NYXAPP_FRAME_MS. Selama loopTask
//     menunggu, app boleh menggambar ke sprite -> tak ada balapan.
//   * frame lewat batas => task DIBUNUH (watchdog), arena dibebaskan, HP tidak beku.
//     Pengecualian: sedang di operasi SD -> diberi waktu tambahan (mutex FATFS).
//   * memori app = ARENA PSRAM ber-quota (nyxmem.h); keluar app => seluruh arena
//     dibebaskan sekali, kebocoran/kerusakan canary dilaporkan lewat toast + Serial.
//   * TLS Gate: sebelum HTTPS, tunggu/reclaim sampai blok kontigu RAM internal cukup,
//     memakai rantai onLowMem App Memory Guard yang sudah ada.
//
// KEJUJURAN KEAMANAN: app native berbagi ruang alamat dengan firmware. Pointer liar
// di app bisa merusak sistem (reboot); watchdog hanya menangkap app yang MACET.
// Pasang hanya app yang kamu percaya.
//
// ELF loader (espressif/elf_loader) adalah komponen ESP-IDF; kalau build Arduino
// belum menyertakannya, `esp_elf.h` tidak ada dan bagian loader ter-kompilasi
// keluar (NYXAPP_HAVE_ELF=0): launcher tetap jalan & memberi tahu alasannya.
//
// CATATAN ARDUINO-CLI: tidak ada tipe nyx:: di tanda tangan fungsi (prototipe otomatis
// dibuat sebelum header ini terbaca); state memakai global + parameter primitif.
// =====================================================================

#include "nyxhost.h"
#if __has_include("esp_elf.h")
#include "esp_elf.h"
#define NYXAPP_HAVE_ELF 1
#else
#define NYXAPP_HAVE_ELF 0
#endif

#define NYXAPP_MAX_LIST   16
#define NYXAPP_FRAME_MS   250       // batas keras satu frame
#define NYXAPP_START_MS   3000      // main()+enter()
#define NYXAPP_STOP_MS    800       // leave()
#define NYXAPP_SD_GRACE   3000      // tambahan bila macet di dalam operasi SD
#define NYXAPP_ELF_MAX    (1024 * 1024)
#define NYXAPP_HTTP_BUF   (128 * 1024)
#define NYXAPP_ROW_H      52

enum { NYX_ST_LIST = 0, NYX_ST_RUN = 1, NYX_ST_ERR = 2 };
enum { NYXC_NONE = 0, NYXC_START = 1, NYXC_FRAME = 2, NYXC_STOP = 3 };

// ---------------------------- state global ----------------------------
static int   nyxState = NYX_ST_LIST;
static char  nyxErr[112];
static char  nyxIds[NYXAPP_MAX_LIST][24];
static char  nyxNames[NYXAPP_MAX_LIST][NYX_MF_NAME_MAX];
static char  nyxSyms[NYXAPP_MAX_LIST];
static uint16_t nyxCols[NYXAPP_MAX_LIST];
static int   nyxN = 0;
static float nyxScroll = 0; static int nyxLastY = 0, nyxDownY = 0; static bool nyxDrag = false, nyxMoved = false;
static int   nyxCurIdx = -1;

static nyx::MemMgr   nyxMM;
static nyx::Host     nyxHost;
static nyx::Manifest nyxMf;
static nyx::Backend  nyxBE;
static nyx::MemBackend nyxMemBE;
static bool  nyxMMInit = false;
static int   nyxMemId = -1;

static LGFX_Sprite* nyxS = nullptr;
static int nyxVX = 0, nyxVY = 0, nyxVW = 0, nyxVH = 0;
static bool nyxTouchDown = false, nyxTouchNew = false, nyxTouchRel = false; static int nyxTouchX = 0, nyxTouchY = 0;


static volatile bool nyxInSd = false;

// ---------------- NYXFS_DUAL_V1: penyimpanan app = SD ATAU FFat ----------------
// Pilihan dibuat di nyxScan(): SD bila ada & berisi /apps; kalau tidak, FFat (flash internal)
// bila berisi /apps; kalau dua-duanya kosong, SD bila terbaca, selain itu FFat.
// CATATAN: task app berstack PSRAM, sedangkan akses FFat mematikan cache flash -> assert
// esp_task_stack_is_sane_cache_disabled. Jadi operasi file app di FFat dijalankan oleh
// WORKER ber-stack RAM internal (nyxFsWorker); jalur SD tetap langsung seperti semula.
static fs::FS* nyxAppFs = nullptr;
static bool    nyxFsFlash = false;

enum { NYXF_OPEN = 1, NYXF_READ, NYXF_WRITE, NYXF_CLOSE, NYXF_SEEK, NYXF_EXISTS, NYXF_REMOVE };
struct NyxFsReq { int op; int h; const char* path; int mode; void* buf; int n; int ret; };
static NyxFsReq nyxFsReq;
static TaskHandle_t nyxFsTask = nullptr;
static volatile TaskHandle_t nyxFsWaiter = nullptr;
static File nyxFiles[8];

static void nyxPickFs() {
  if (sdReady && SD_MMC.exists("/apps"))        { nyxAppFs = &SD_MMC; nyxFsFlash = false; return; }
  if (ffatReady && FFat.exists("/apps"))        { nyxAppFs = &FFat;   nyxFsFlash = true;  return; }
  if (sdReady)                                  { nyxAppFs = &SD_MMC; nyxFsFlash = false; return; }
  if (ffatReady)                                { nyxAppFs = &FFat;   nyxFsFlash = true;  return; }
  nyxAppFs = nullptr; nyxFsFlash = false;
}

static void nyxFsExec(void* vp) {
  NyxFsReq* r = (NyxFsReq*)vp;
  fs::FS& F = *nyxAppFs;
  switch (r->op) {
    case NYXF_OPEN: {
      int slot = -1; for (int i = 0; i < 8; i++) if (!nyxFiles[i]) { slot = i; break; }
      if (slot < 0) { r->ret = -1; break; }
      nyxFiles[slot] = F.open(r->path, r->mode == 0 ? FILE_READ : (r->mode == 1 ? FILE_WRITE : FILE_APPEND));
      r->ret = nyxFiles[slot] ? slot : -1; break; }
    case NYXF_READ:   r->ret = (int)nyxFiles[r->h].read((uint8_t*)r->buf, (size_t)r->n); break;
    case NYXF_WRITE:  r->ret = (int)nyxFiles[r->h].write((const uint8_t*)r->buf, (size_t)r->n); break;
    case NYXF_CLOSE:  if (nyxFiles[r->h]) nyxFiles[r->h].close(); r->ret = 0; break;
    case NYXF_SEEK:   r->ret = nyxFiles[r->h].seek((uint32_t)r->n) ? 0 : -1; break;
    case NYXF_EXISTS: r->ret = F.exists(r->path) ? 1 : 0; break;
    case NYXF_REMOVE: r->ret = F.remove(r->path) ? 0 : -1; break;
    default: r->ret = -1;
  }
}

static void nyxFsWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    nyxFsExec(&nyxFsReq);
    TaskHandle_t w = nyxFsWaiter;
    if (w) xTaskNotifyGive(w);
  }
}

// jalankan satu operasi file di backend yang aktif. Dipanggil dari task app.
static int nyxFsDo(int op, int h, const char* path, int mode, void* buf, int n) {
  if (!nyxAppFs) return -1;
  NyxFsReq r = { op, h, path, mode, buf, n, -1 };
  nyxInSd = true;
  if (!nyxFsFlash) {
    nyxFsExec(&r);
  } else {
    if (!nyxFsTask) xTaskCreatePinnedToCore(nyxFsWorker, "nyxfs", 4096, nullptr, 2, &nyxFsTask, 1);   // stack RAM internal
    if (!nyxFsTask) { nyxInSd = false; return -1; }
    nyxFsReq = r;
    nyxFsWaiter = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0);
    xTaskNotifyGive(nyxFsTask);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8000)) == 0) { nyxFsWaiter = nullptr; nyxInSd = false; return -1; }
    r = nyxFsReq;
    nyxFsWaiter = nullptr;
  }
  nyxInSd = false;
  return r.ret;
}

// task app
static volatile int  nyxCmd = NYXC_NONE; static volatile int nyxCmdRc = 0;
static TaskHandle_t  nyxTask = nullptr, nyxCaller = nullptr;
static StackType_t*  nyxStack = nullptr; static uint32_t nyxStackBytes = 0; static StaticTask_t nyxTcb;

#if NYXAPP_HAVE_ELF
static esp_elf_t nyxElf; static uint8_t* nyxElfBuf = nullptr; static bool nyxElfLoaded = false;
#endif

// HTTP worker
static volatile int nyxHttpState = NYX_HTTP_IDLE; static volatile int nyxHttpCode = 0; static volatile uint32_t nyxHttpLen = 0;
static uint8_t* nyxHttpBuf = nullptr; static char nyxHttpUrl[208]; static volatile bool nyxHttpAbort = false;
static TaskHandle_t nyxHttpTask = nullptr; static StackType_t* nyxHttpStack = nullptr; static StaticTask_t nyxHttpTcb;

// ------------------------- backend: grafis ----------------------------
static void nyxB_clear(void*, uint16_t c) { if (nyxS) nyxS->fillRect(nyxVX, nyxVY, nyxVW, nyxVH, c); }
static void nyxB_fill(void*, int x, int y, int w, int h, uint16_t c) { if (nyxS) nyxS->fillRect(nyxVX + x, nyxVY + y, w, h, c); }
static void nyxB_rect(void*, int x, int y, int w, int h, uint16_t c) { if (nyxS) nyxS->drawRect(nyxVX + x, nyxVY + y, w, h, c); }
static void nyxB_rrect(void*, int x, int y, int w, int h, int r, uint16_t c) { if (nyxS) nyxS->fillRoundRect(nyxVX + x, nyxVY + y, w, h, r, c); }
static void nyxB_pixel(void*, int x, int y, uint16_t c) { if (nyxS) nyxS->drawPixel(nyxVX + x, nyxVY + y, c); }
static void nyxB_line(void*, int x0, int y0, int x1, int y1, uint16_t c) { if (nyxS) nyxS->drawLine(nyxVX + x0, nyxVY + y0, nyxVX + x1, nyxVY + y1, c); }
static void nyxB_circle(void*, int x, int y, int r, uint16_t c) { if (nyxS) nyxS->fillCircle(nyxVX + x, nyxVY + y, r, c); }
static void nyxB_ring(void*, int x, int y, int r, uint16_t c) { if (nyxS) nyxS->drawCircle(nyxVX + x, nyxVY + y, r, c); }
static void nyxB_text(void*, int x, int y, const char* str, uint16_t c, int sz) {
  if (!nyxS) return;
  nyxS->setTextSize(sz); nyxS->setTextColor(c); nyxS->setCursor(nyxVX + x, nyxVY + y); nyxS->print(str);
}
static int  nyxB_textw(void*, const char* str, int sz) { if (!nyxS) return (int)strlen(str) * 6 * sz; nyxS->setTextSize(sz); return nyxS->textWidth(str); }
static void nyxB_blit(void*, int x, int y, int w, int h, const uint16_t* px) { if (nyxS) nyxS->pushImage(nyxVX + x, nyxVY + y, w, h, px); }
static void nyxB_view(void*, int* w, int* h) { *w = nyxVW; *h = nyxVH; }
static bool nyxB_tilt(void*, int* ax, int* ay, int* az) {
  mpuReadRaw();
  *ax = (int)(mpuAx * 1000.0f); *ay = (int)(mpuAy * 1000.0f); *az = (int)(mpuAz * 1000.0f);
  return true;
}
static uint32_t nyxB_millis(void*) { return (uint32_t)millis(); }
static uint32_t nyxB_rand(void*) { return (uint32_t)esp_random(); }
static void nyxB_log(void*, const char* str) { Serial.printf("[app:%s] %s\n", nyxMf.name, str); }
static void nyxB_toast(void*, const char* str) { showToast(str, 1800); }

// ----------------------- backend: file (SD atau FFat) -----------------
static int nyxB_fopen(void*, const char* path, int mode) { return nyxFsDo(NYXF_OPEN, 0, path, mode, nullptr, 0); }
static int nyxB_fread(void*, int h, void* buf, int n)  { return nyxFsDo(NYXF_READ, h, nullptr, 0, buf, n); }
static int nyxB_fwrite(void*, int h, const void* buf, int n) { return nyxFsDo(NYXF_WRITE, h, nullptr, 0, (void*)buf, n); }
static void nyxB_fclose(void*, int h) { nyxFsDo(NYXF_CLOSE, h, nullptr, 0, nullptr, 0); }
static int nyxB_fseek(void*, int h, int pos) { return nyxFsDo(NYXF_SEEK, h, nullptr, 0, nullptr, pos); }
static int nyxB_fsize(void*, int h) { return (int)nyxFiles[h].size(); }
static int nyxB_fexists(void*, const char* path) { return nyxFsDo(NYXF_EXISTS, 0, path, 0, nullptr, 0); }
static int nyxB_fremove(void*, const char* path) { return nyxFsDo(NYXF_REMOVE, 0, path, 0, nullptr, 0); }

// ------------------------ backend: HTTP (worker) ----------------------
static void nyxHttpWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int code = -1; uint32_t n = 0; bool ok = false;
    if (WiFi.status() == WL_CONNECTED && nyxHttpBuf) {
      bool tls = strncmp(nyxHttpUrl, "https://", 8) == 0;
      HTTPClient http; WiFiClientSecure sc; WiFiClient pc; bool begun;
      if (tls) { sc.setInsecure(); sc.setTimeout(12000); begun = http.begin(sc, nyxHttpUrl); }
      else begun = http.begin(pc, nyxHttpUrl);
      if (begun) {
        http.setTimeout(12000);
        code = http.GET();
        if (code > 0) {
          WiFiClient* st = http.getStreamPtr(); unsigned long t0 = millis();
          while (!nyxHttpAbort && st && (http.connected() || st->available()) && n < NYXAPP_HTTP_BUF && millis() - t0 < 15000UL) {
            int av = st->available();
            if (av > 0) {
              size_t room = NYXAPP_HTTP_BUF - n; size_t want = (size_t)av < room ? (size_t)av : room;
              int rd = st->readBytes(nyxHttpBuf + n, want); if (rd > 0) n += (uint32_t)rd;
            } else delay(5);
          }
          ok = !nyxHttpAbort;
        }
        http.end();
      }
    }
    nyxHttpCode = code; nyxHttpLen = n; nyxHttpState = ok ? NYX_HTTP_DONE : NYX_HTTP_FAILED;
  }
}
static int nyxB_hbegin(void*, const char* url) {
  if (nyxHttpState == NYX_HTTP_RUNNING) return -6;
  if (!nyxHttpBuf) nyxHttpBuf = (uint8_t*)heap_caps_malloc(NYXAPP_HTTP_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!nyxHttpBuf) return -4;
  if (!nyxHttpTask) {
    BaseType_t r = createTaskPsramStack(nyxHttpWorker, "nyxHttp", 12288, NULL, &nyxHttpStack, &nyxHttpTcb, &nyxHttpTask, 0);
    if (r != pdPASS) { nyxHttpTask = nullptr; return -5; }
  }
  strncpy(nyxHttpUrl, url, sizeof(nyxHttpUrl) - 1); nyxHttpUrl[sizeof(nyxHttpUrl) - 1] = 0;
  nyxHttpAbort = false; nyxHttpCode = 0; nyxHttpLen = 0; nyxHttpState = NYX_HTTP_RUNNING;
  xTaskNotifyGive(nyxHttpTask);
  return 1;
}
static int nyxB_hpoll(void*, int, int* code, uint32_t* len) { *code = nyxHttpCode; *len = nyxHttpLen; return nyxHttpState; }
static const uint8_t* nyxB_hdata(void*, int) { return nyxHttpBuf; }
static void nyxB_hclose(void*, int) {
  if (nyxHttpState == NYX_HTTP_RUNNING) nyxHttpAbort = true;      // worker berhenti sendiri; begin berikutnya menunggu
  else nyxHttpState = NYX_HTTP_IDLE;
}

// --------------------- backend: memori sistem (MemMgr) ----------------
static void*  nyxM_big(void*, size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void   nyxM_bigFree(void*, void* p) { heap_caps_free(p); }
static size_t nyxM_fi(void*) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
static size_t nyxM_lg(void*) { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }
static size_t nyxM_ps(void*) { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static void   nyxM_delay(void*, uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static uint32_t nyxM_now(void*) { return (uint32_t)millis(); }

// rantai reclaim: pakai mekanisme App Memory Guard yang sudah ada (korban LRU app background)
static void nyxReclaimCb(void*, int level) {
  (void)level;
  int v = appFindLruBackgroundVictim(curScreen());
  if (v >= 0) { apps[v].onLowMem(); appLastUsedMs[v] = 0; }
}

static void nyxInitBackends() {
  memset(&nyxBE, 0, sizeof(nyxBE)); nyxBE.ctx = nullptr;
  nyxBE.clear = nyxB_clear; nyxBE.fillRect = nyxB_fill; nyxBE.rect = nyxB_rect; nyxBE.roundRect = nyxB_rrect; nyxBE.pixel = nyxB_pixel;
  nyxBE.line = nyxB_line; nyxBE.circle = nyxB_circle; nyxBE.ring = nyxB_ring; nyxBE.text = nyxB_text; nyxBE.textW = nyxB_textw;
  nyxBE.blit = nyxB_blit; nyxBE.viewport = nyxB_view; nyxBE.tilt = nyxB_tilt; nyxBE.millis = nyxB_millis; nyxBE.rand32 = nyxB_rand;
  nyxBE.log = nyxB_log; nyxBE.toast = nyxB_toast;
  nyxBE.fopen = nyxB_fopen; nyxBE.fread = nyxB_fread; nyxBE.fwrite = nyxB_fwrite; nyxBE.fclose = nyxB_fclose; nyxBE.fseek = nyxB_fseek;
  nyxBE.fsize = nyxB_fsize; nyxBE.fexists = nyxB_fexists; nyxBE.fremove = nyxB_fremove;
  nyxBE.httpBegin = nyxB_hbegin; nyxBE.httpPoll = nyxB_hpoll; nyxBE.httpData = nyxB_hdata; nyxBE.httpClose = nyxB_hclose;
  memset(&nyxMemBE, 0, sizeof(nyxMemBE));
  nyxMemBE.bigAlloc = nyxM_big; nyxMemBE.bigFree = nyxM_bigFree; nyxMemBE.freeInternal = nyxM_fi; nyxMemBE.largestInternal = nyxM_lg;
  nyxMemBE.freePsram = nyxM_ps; nyxMemBE.delayMs = nyxM_delay; nyxMemBE.nowMs = nyxM_now;
}

// ------------------------------ daftar app ----------------------------
static bool nyxIdOk(const char* id) {
  size_t n = strlen(id); if (n == 0 || n >= 24) return false;
  for (size_t i = 0; i < n; i++) { char c = id[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false; }
  return true;
}

// baca manifest /apps/<id>/manifest.txt ke nyxMf. true bila valid.
static bool nyxReadManifest(const char* id) {
  char path[64]; snprintf(path, sizeof(path), "/apps/%s/manifest.txt", id);
  if (!nyxAppFs) return false;
  File f = nyxAppFs->open(path, FILE_READ); if (!f) return false;
  char buf[512]; size_t n = f.read((uint8_t*)buf, sizeof(buf) - 1); f.close(); buf[n] = 0;
  nyx::parseManifest(buf, n, &nyxMf);
  return nyxMf.valid;
}

static void nyxScan() {
  nyxN = 0; nyxScroll = 0;
  nyxPickFs();
  if (!nyxAppFs) return;
  File root = nyxAppFs->open("/apps");
  if (!root) return;
  if (!root.isDirectory()) { root.close(); return; }
  File e = root.openNextFile();
  while (e && nyxN < NYXAPP_MAX_LIST) {
    if (e.isDirectory()) {
      const char* nm = e.name(); const char* sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
      if (nyxIdOk(nm) && nyxReadManifest(nm)) {
        strncpy(nyxIds[nyxN], nm, 23); nyxIds[nyxN][23] = 0;
        strncpy(nyxNames[nyxN], nyxMf.name, NYX_MF_NAME_MAX - 1); nyxNames[nyxN][NYX_MF_NAME_MAX - 1] = 0;
        nyxSyms[nyxN] = nyxMf.sym; nyxCols[nyxN] = nyxMf.color; nyxN++;
      }
    }
    e.close(); e = root.openNextFile();
  }
  if (e) e.close();
  root.close();
}

// ------------------------------ task & sinkron ------------------------
static void nyxTaskFn(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int c = nyxCmd, rc = 0;
    if (c == NYXC_START) {
#if NYXAPP_HAVE_ELF
      char* argv[2] = { (char*)(void*)&nyxHost.api, nullptr };
      esp_elf_request(&nyxElf, 0, NYX_ENTRY_ARGC, argv);            // main() hanya mendaftarkan ops; sukses = opsSet
      rc = nyxHost.opsSet ? 0 : -1;
      if (rc == 0 && nyxHost.ops.enter) rc = nyxHost.ops.enter();
#else
      rc = -2;
#endif
    } else if (c == NYXC_FRAME) {
      if (nyxHost.ops.frame) nyxHost.ops.frame();
    } else if (c == NYXC_STOP) {
      if (nyxHost.opsSet && nyxHost.ops.leave) nyxHost.ops.leave();
    }
    nyxCmdRc = rc; nyxCmd = NYXC_NONE;
    xTaskNotifyGive(nyxCaller);
  }
}

// -9 = timeout (app macet)
static int nyxCall(int cmd, uint32_t ms) {
  if (!nyxTask) return -9;
  nyxCaller = xTaskGetCurrentTaskHandle();
  ulTaskNotifyTake(pdTRUE, 0);
  nyxCmd = cmd;
  xTaskNotifyGive(nyxTask);
  uint32_t grace = 0;
  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)) > 0) return nyxCmdRc;
    if (nyxInSd && grace < NYXAPP_SD_GRACE) { grace += ms; continue; }   // jangan bunuh di tengah FATFS
    return -9;
  }
}

static void nyxKillTask() { nyxFsWaiter = nullptr; if (nyxTask) { TaskHandle_t t = nyxTask; nyxTask = nullptr; vTaskDelete(t); } }

static void nyxFail(const char* msg) {
  strncpy(nyxErr, msg, sizeof(nyxErr) - 1); nyxErr[sizeof(nyxErr) - 1] = 0;
  nyxState = NYX_ST_ERR; Serial.printf("[NyxApp] %s\n", nyxErr);
}

// hentikan app (aman dipanggil berulang). Tidak mengubah nyxState kecuali ke LIST bila tidak ada galat.
static void nyxStop() {
  if (nyxTask) {
    int rc = nyxCall(NYXC_STOP, NYXAPP_STOP_MS);
    if (rc == -9) Serial.println("[NyxApp] leave() macet -> task dibunuh");
    nyxKillTask();
  }
  if (nyx::nyxHostCur() == &nyxHost) { nyx::hostCloseAll(&nyxHost); nyx::nyxHostCur() = nullptr; }
  nyxHost.opsSet = false;
  for (int i = 0; i < 8; i++) if (nyxFiles[i]) nyxFiles[i].close();
  if (nyxHttpState == NYX_HTTP_RUNNING) nyxHttpAbort = true;
#if NYXAPP_HAVE_ELF
  if (nyxElfLoaded) { esp_elf_deinit(&nyxElf); nyxElfLoaded = false; }
  if (nyxElfBuf) { heap_caps_free(nyxElfBuf); nyxElfBuf = nullptr; }
#endif
  if (nyxMemId >= 0) {
    nyx::LeakReport r = nyxMM.release(nyxMemId); nyxMemId = -1;
    Serial.printf("[NyxApp] '%s' keluar: puncak %u B, alokasi %u, ditolak %u, bocor %u blok (%u B), op salah %u%s\n",
                  nyxMf.name, (unsigned)r.peakBytes, (unsigned)r.nAlloc, (unsigned)r.nFail, (unsigned)r.leakBlocks,
                  (unsigned)r.leakBytes, (unsigned)r.badOps, r.corrupted ? ", HEAP RUSAK" : "");
    if (r.corrupted) showToast("App merusak heap-nya sendiri", 2200);
    else if (r.leakBlocks) { char m[48]; snprintf(m, sizeof(m), "App bocor %u blok", (unsigned)r.leakBlocks); showToast(m, 1800); }
  }
}

static void nyxLaunch(int idx) {
  if (idx < 0 || idx >= nyxN) return;
#if !NYXAPP_HAVE_ELF
  nyxFail("Build firmware ini belum menyertakan ELF loader (lihat docs/features/native-apps.md)");
  return;
#else
  if (!nyxAppFs) { nyxFail("Tidak ada SD maupun FFat"); return; }
  if (!nyxReadManifest(nyxIds[idx])) { nyxFail("manifest.txt tidak valid"); return; }
  if (!nyxMMInit) { nyxInitBackends(); nyxMM.init(nyxMemBE); nyxMM.addReclaim(nyxReclaimCb, nullptr, 50); nyxMMInit = true; }
  nyxCurIdx = idx;

  // 1) arena memori (quota keras)
  int id = nyxMM.launch(nyxMf.name, nyxMf.memKb * 1024u);
  if (id == -3) { nyxFail("PSRAM tidak cukup untuk quota app"); return; }
  if (id < 0) { nyxFail("Gagal membuat arena memori app"); return; }
  nyxMemId = id;

  // 2) folder data + berkas ELF -> PSRAM
  char dataDir[64]; snprintf(dataDir, sizeof(dataDir), "/apps/%s/data", nyxIds[idx]);
  nyxAppFs->mkdir(dataDir);
  char elfPath[96]; snprintf(elfPath, sizeof(elfPath), "/apps/%s/%s", nyxIds[idx], nyxMf.elf);
  File f = nyxAppFs->open(elfPath, FILE_READ);
  if (!f) { nyxStop(); nyxFail("app.elf tidak ditemukan"); return; }
  size_t sz = f.size();
  if (sz < 64 || sz > NYXAPP_ELF_MAX) { f.close(); nyxStop(); nyxFail("ukuran ELF tidak wajar"); return; }
  nyxElfBuf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!nyxElfBuf) { f.close(); nyxStop(); nyxFail("PSRAM tidak cukup untuk ELF"); return; }
  size_t got = f.read(nyxElfBuf, sz); f.close();
  if (got != sz) { nyxStop(); nyxFail("gagal membaca ELF"); return; }

  // 3) relokasi ELF ke memori eksekusi
  esp_elf_init(&nyxElf);
  int rr = esp_elf_relocate(&nyxElf, nyxElfBuf, sz);
  if (rr < 0) { char m[64]; snprintf(m, sizeof(m), "ELF gagal direlokasi (kode %d)", rr); nyxStop(); nyxFail(m); return; }
  nyxElfLoaded = true;

  // 4) host + task
  nyx::hostInit(&nyxHost, nyxBE, &nyxMM, nyxMemId, nyxMf, dataDir);
  nyx::nyxHostCur() = &nyxHost;
  uint32_t stackBytes = nyxMf.stackKb * 1024u;
  if (nyxStack && nyxStackBytes != stackBytes) { heap_caps_free(nyxStack); nyxStack = nullptr; }
  nyxStackBytes = stackBytes;
  BaseType_t tr = createTaskPsramStackPrio(nyxTaskFn, "nyxApp", stackBytes, NULL, &nyxStack, &nyxTcb, &nyxTask, 0, 2);
  if (tr != pdPASS) { nyxTask = nullptr; nyxStop(); nyxFail("gagal membuat task app"); return; }

  int rc = nyxCall(NYXC_START, NYXAPP_START_MS);
  if (rc != 0) {
    char m[72];
    if (rc == -9) snprintf(m, sizeof(m), "App macet saat start (watchdog)");
    else snprintf(m, sizeof(m), "App gagal start (kode %d)", rc);
    nyxStop(); nyxFail(m); return;
  }
  nyxTouchDown = nyxTouchNew = nyxTouchRel = false;
  nyxState = NYX_ST_RUN;
#endif
}

// ------------------------------ hook UI -------------------------------
void nyxAppEnter() {
  nyxStop();
  nyxState = NYX_ST_LIST; nyxErr[0] = 0; nyxDrag = false;
  nyxScan();
}

void nyxAppExit() {
  nyxStop();
  nyxState = NYX_ST_LIST;
}

bool nyxWantsRedraw() { return nyxState == NYX_ST_RUN; }

static void nyxDrawLetter(LGFX_Sprite& s, int cx, int cy, int r, char sym, uint16_t col) {
  nyxAaCircle(s, cx, cy, r, col);
  s.setTextSize(2); s.setTextColor(0xFFFF);
  char t[2] = { sym, 0 }; int w = s.textWidth(t);
  s.setCursor(cx - w / 2, cy - 7); s.print(t);
  s.setTextSize(1);
}

static void nyxDrawList(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Nyx Apps");
  int top = 26 + 22, bot = backY() - 4;
#if !NYXAPP_HAVE_ELF
  nyxAaRR(s, 8, top, SCR_W - 16, 30, 8, blend565(T().bg, 0xFD20, 70));
  s.setTextColor(T().text); s.setTextSize(1); s.setCursor(16, top + 6); s.print("Build ini belum punya ELF loader.");
  s.setTextColor(T().subtext); s.setCursor(16, top + 18); s.print("Lihat docs/features/native-apps.md");
  top += 36;
#endif
  if (!nyxAppFs) { s.setTextColor(T().subtext); s.setCursor(12, top + 10); s.print("SD & FFat tidak terbaca."); return; }
  if (nyxN == 0) {
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(12, top + 10); s.print("Belum ada app.");
    s.setCursor(12, top + 24); s.print(nyxFsFlash ? "Taruh di FFat: /apps/<id>/" : "Taruh di SD:  /apps/<id>/");
    s.setCursor(12, top + 36); s.print("  manifest.txt  +  app.elf");
    return;
  }
  s.setClipRect(0, top, SCR_W, bot - top);
  for (int i = 0; i < nyxN; i++) {
    int ry = top + i * NYXAPP_ROW_H - (int)nyxScroll;
    if (ry + NYXAPP_ROW_H < top || ry > bot) continue;
    drawGlassPanel(s, 8, ry + 2, SCR_W - 16, NYXAPP_ROW_H - 6, 10, T().surface2, 170);
    nyxDrawLetter(s, 8 + 10 + 16, ry + NYXAPP_ROW_H / 2 - 1, 16, nyxSyms[i], nyxCols[i]);
    s.setTextSize(2); s.setTextColor(T().text); s.setCursor(8 + 10 + 40, ry + 8); s.print(nyxNames[i]);
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(8 + 10 + 40, ry + 28); s.print(nyxIds[i]);
  }
  s.clearClipRect();
}

static void nyxDrawErr(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Nyx Apps");
  nyxAaRR(s, 8, 52, SCR_W - 16, 64, 10, blend565(T().bg, 0xF800, 60));
  s.setTextSize(1); s.setTextColor(T().text);
  s.setCursor(16, 60); s.print("App tidak bisa dijalankan:");
  s.setTextColor(T().subtext);
  int y = 76; const char* p = nyxErr; char line[48];
  while (*p && y < 108) {
    int n = 0; while (p[n] && n < 40) n++;
    memcpy(line, p, (size_t)n); line[n] = 0; s.setCursor(16, y); s.print(line); y += 12; p += n;
  }
  s.setTextColor(T().subtext); s.setCursor(16, 126); s.print("Ketuk di mana saja untuk kembali.");
}

void drawNyxApp(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  if (nyxState == NYX_ST_LIST) nyxDrawList(s);
  else if (nyxState == NYX_ST_ERR) nyxDrawErr(s);
  else {
    nyxVX = 0; nyxVY = STATUS_H; nyxVW = SCR_W; nyxVH = backY() - STATUS_H - 2;
    s.fillRect(nyxVX, nyxVY, nyxVW, nyxVH, 0x0000);
    int st = (nyxTouchDown ? NYX_TOUCH_DOWN : 0) | (nyxTouchNew ? NYX_TOUCH_PRESSED : 0) | (nyxTouchRel ? NYX_TOUCH_RELEASED : 0);
    nyx::hostSetTouch(&nyxHost, st, nyxTouchX - nyxVX, nyxTouchY - nyxVY);
    nyxTouchNew = false; nyxTouchRel = false;
    nyxS = &s; s.setClipRect(nyxVX, nyxVY, nyxVW, nyxVH);
    int rc = nyxCall(NYXC_FRAME, NYXAPP_FRAME_MS);
    s.clearClipRect(); s.setTextSize(1); nyxS = nullptr;
    if (rc == -9) {
      nyxStop(); nyxFail("App dihentikan watchdog (satu frame > 250 ms)");
    } else if (nyxHost.quitReq) {
      navBack();
    }
  }
  drawBack(s);
  drawToast(s);
}

void nyxAppTouch(int x, int y, bool held, bool isNew) {
  if (isNew && isBack(x, y)) { navBack(); return; }
  if (nyxState == NYX_ST_ERR) { if (isNew) { nyxState = NYX_ST_LIST; nyxErr[0] = 0; nyxScan(); needRedraw = true; } return; }
  if (nyxState == NYX_ST_RUN) {
    nyxTouchX = x; nyxTouchY = y;
    if (isNew) nyxTouchNew = true;
    if (held) nyxTouchDown = true; else if (nyxTouchDown) { nyxTouchDown = false; nyxTouchRel = true; }
    return;
  }
  // ---- daftar: seret untuk menggulir, ketuk untuk membuka ----
  int top = 26 + 22, bot = backY() - 4;
  if (isNew) { nyxDrag = (y >= top && y <= bot); nyxLastY = y; nyxDownY = y; nyxMoved = false; return; }
  if (!held) {
    if (nyxDrag && !nyxMoved) {
      int ry = (y > 0) ? y : nyxLastY;
      int row = (int)((ry - top + (int)nyxScroll) / NYXAPP_ROW_H);
      if (row >= 0 && row < nyxN) { nyxLaunch(row); needRedraw = true; }
    }
    nyxDrag = false; return;
  }
  if (!nyxDrag) return;
  int dy = nyxLastY - y; nyxLastY = y;
  if (abs(y - nyxDownY) > 8) nyxMoved = true;
  nyxScroll += (float)dy;
  float maxS = (float)(nyxN * NYXAPP_ROW_H - (bot - top)); if (maxS < 0) maxS = 0;
  if (nyxScroll < 0) { nyxScroll = 0; }
  if (nyxScroll > maxS) { nyxScroll = maxS; }
  needRedraw = true;
}
