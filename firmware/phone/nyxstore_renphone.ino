// =====================================================================
// NYXSTORE_UI_V2 (tampilan baru: squircle, kartu hero, tile info)
// nyxstore_renphone.ino -- "Nyx Store": katalog app native (ELF) yang bisa dipasang dari HP
// =====================================================================
// Dibuka dari tombol "Store" di layar Nyx Apps (nyxapp_renphone.ino). Alur:
//   1. unduh indeks  <rilis nyxapps>/nyxapps.json            (dibuat tools/nyxapps_release.py)
//   2. ketuk app -> kartu rincian (ukuran, memori, izin, status) -> Pasang / Perbarui / Hapus
//   3. pasang: unduh <id>.manifest.txt -> unduh <id>.elf -> cek SHA-256 dengan indeks
//              -> tulis ke  /apps/<id>/  (SD atau FFat, sesuai pilihan Nyx Apps) -> catat store.txt
//
// ARSITEKTUR (kebijakan murni ada di nyxstore_core.h, diuji di PC; file ini LEM):
//   * Jaringan + SHA-256 dikerjakan task sendiri (stack PSRAM, core 0) -> UI tidak macet.
//     Task ini TIDAK menyentuh SD/FFat (akses FFat dari stack PSRAM = crash, lihat catatan
//     NYXFS_DUAL_V1 di nyxapp_renphone.ino).
//   * Penulisan berkas dilakukan loopTask, per-frame dalam potongan kecil (batas waktu ~20 ms),
//     jadi bilah kemajuan tetap bergerak walau FFat lambat.
//   * ELF ditulis dulu ke app.elf.tmp lalu di-rename; manifest.txt ditulis TERAKHIR -> app setengah
//     jadi tidak pernah muncul di daftar Nyx Apps (nyxScan() hanya menerima manifest yang sah).
//
// KEJUJURAN KEAMANAN: koneksi memakai setInsecure() (sama seperti OTA di proyek ini), jadi SHA-256
// di indeks melindungi dari berkas RUSAK/terpotong, BUKAN dari penyerang yang mengendalikan jaringan.
// App native tidak di-sandbox (lihat docs/features/native-apps.md). Kartu rincian mengingatkan ini.
//
// CATATAN ARDUINO-CLI: tidak ada tipe nyx::/nyxstore:: di tanda tangan fungsi (prototipe otomatis
// dibuat sebelum header terbaca); state memakai global + parameter primitif.
// =====================================================================

#include "nyxstore_core.h"
#include <mbedtls/sha256.h>

#define NYXSTORE_BASE      "https://github.com/silentproject2025/ren_phone/releases/download/nyxapps/"
#define NYXSTORE_INDEX     "nyxapps.json"
#define NYXSTORE_ROW_H     64          // NYXSTORE_UI_V2 (dulu 56)
#define NYXSTORE_TOP       62          // NYXSTORE_UI_V2: awal konten, di bawah judul + subjudul
#define NYXSTORE_CHUNK     4096
#define NYXSTORE_SLICE_MS  20
#define NYXSTORE_MARGIN    (32u * 1024u)    // sisa ruang minimal setelah ELF ditulis

enum { NS_FETCH = 0, NS_LIST = 1, NS_DETAIL = 2, NS_CONFIRM_RM = 3, NS_WORK = 4, NS_ERR = 5 };
enum { NSJ_NONE = 0, NSJ_INDEX = 1, NSJ_MANIFEST = 2, NSJ_ELF = 3 };
enum { NSW_IDLE = 0, NSW_RUN = 1, NSW_OK = 2, NSW_FAIL = 3 };
enum { NSP_MANIFEST = 0, NSP_ELF = 1, NSP_WRITE = 2 };
enum { NSB_INSTALL = 1, NSB_REMOVE = 2, NSB_CANCEL = 3, NSB_YES_RM = 4 };

static nyxstore::Entry nsApps[NYXSTORE_MAX_APPS];
static uint8_t  nsAppState[NYXSTORE_MAX_APPS];
static int      nsN = 0, nsSub = NS_FETCH, nsSel = -1, nsPhase = NSP_MANIFEST;
static float    nsScroll = 0;
static int      nsLastX = 0, nsLastY = 0, nsDownY = 0, nsPrevY = 0; static bool nsDrag = false, nsMoved = false;
static char     nsErr[96], nsStatus[48];
static bool     nsLeaveAfter = false;       // Kembali ditekan saat tugas jaringan masih jalan -> keluar setelah selesai

// ---- worker jaringan ----
static volatile int      nsJob = NSJ_NONE, nsJobSt = NSW_IDLE;
static volatile uint32_t nsGot = 0, nsTotal = 0;
static volatile bool     nsAbort = false;
static char     nsUrl[220], nsJobErr[72];
static uint8_t* nsBuf = nullptr; static uint32_t nsBufCap = 0; static volatile uint32_t nsBufLen = 0;
static bool     nsWantSha = false; static char nsShaCalc[65];
static TaskHandle_t nsTask = nullptr; static StackType_t* nsStack = nullptr; static StaticTask_t nsTcb;

// ---- pasang ----
static nyx::Manifest nsMf;
static char     nsMfTxt[512]; static size_t nsMfLen = 0;
static File     nsOut; static bool nsOutOpen = false; static uint32_t nsWritten = 0;
static char     nsDir[48], nsTmpPath[96], nsDstPath[96];

// ---- tata letak kartu rincian (dipakai gambar & sentuh) ----
struct NsBtnRect { int x, y, w, h, id; };
static NsBtnRect nsBtns[3]; static int nsBtnN = 0;

// ------------------------------ util ------------------------------
static void nsSetErr(const char* m) { strncpy(nsErr, m, sizeof(nsErr) - 1); nsErr[sizeof(nsErr) - 1] = 0; nsSub = NS_ERR; Serial.printf("[NyxStore] %s\n", nsErr); }

static uint64_t nsFreeBytes() {
  if (!nyxAppFs) return 0;
  if (nyxFsFlash) return (uint64_t)FFat.totalBytes() - (uint64_t)FFat.usedBytes();
  return (uint64_t)SD_MMC.totalBytes() - (uint64_t)SD_MMC.usedBytes();
}

static uint32_t nsPermMask(const char* csv) {   // "fs,net" -> bit NYX_PERM_*; token tak dikenal -> bit 31 (supaya tak pernah sama)
  uint32_t m = 0; const char* t = csv;
  while (*t) {
    const char* e = strchr(t, ','); size_t n = e ? (size_t)(e - t) : strlen(t);
    if (n == 2 && !strncmp(t, "fs", 2)) m |= NYX_PERM_FS;
    else if (n == 3 && !strncmp(t, "net", 3)) m |= NYX_PERM_NET;
    else if (n == 4 && !strncmp(t, "tilt", 4)) m |= NYX_PERM_TILT;
    else if (n == 6 && !strncmp(t, "notify", 6)) m |= NYX_PERM_NOTIFY;
    else if (n > 0) m |= 0x80000000u;
    if (!e) break;
    t = e + 1;
  }
  return m;
}

static bool nsRmTree(fs::FS& F, const char* path, int depth) {
  File d = F.open(path);
  if (!d) return true;
  if (!d.isDirectory()) { d.close(); return F.remove(path); }
  for (int guard = 0; guard < 300; guard++) {
    File e = d.openNextFile();
    if (!e) break;
    const char* nm = e.name(); const char* sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
    char child[96]; snprintf(child, sizeof(child), "%s/%s", path, nm);
    bool isDir = e.isDirectory(); e.close(); d.close();          // mulai ulang iterasi setelah mengubah isi folder
    if (isDir) { if (depth < 4) nsRmTree(F, child, depth + 1); F.rmdir(child); }
    else F.remove(child);
    d = F.open(path); if (!d) return false;
  }
  d.close();
  return F.rmdir(path);
}

// status pasang tiap entri katalog (folder + manifest sah + catatan store.txt)
static void nsRefreshStates() {
  nyxPickFs();
  for (int i = 0; i < nsN; i++) {
    bool inst = false; char rec[65] = {0};
    if (nyxAppFs && nyxReadManifest(nsApps[i].id)) {
      inst = true;
      char p[64]; snprintf(p, sizeof(p), "/apps/%s/store.txt", nsApps[i].id);
      File f = nyxAppFs->open(p, FILE_READ);
      if (f) { char b[128]; size_t n = f.read((uint8_t*)b, sizeof(b) - 1); f.close(); b[n] = 0; nyxstore::parseRecord(b, n, rec); }
    }
    nsAppState[i] = (uint8_t)nyxstore::stateFor(inst, rec, nsApps[i].sha);
  }
}

// ------------------------------ worker jaringan ------------------------------
static bool nsDoJob(int job) {
  nsJobErr[0] = 0;
  if (WiFi.status() != WL_CONNECTED) { strncpy(nsJobErr, RPTR("WiFi tidak terhubung"), sizeof(nsJobErr) - 1); return false; }
  const uint32_t expect = (job == NSJ_ELF) ? (uint32_t)nsTotal : 0;   // ELF: ukuran PASTI dari indeks; lainnya: sampai habis
  nsGot = 0; nsBufLen = 0;
  int code = -1; bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok && !nsAbort; attempt++) {
    if (attempt) { delay(1200); }
    nasaWaitForHeap();
    WiFiClientSecure sc; sc.setInsecure(); sc.setTimeout(15000);
    HTTPClient http;
    if (!http.begin(sc, nsUrl)) { code = -1; continue; }
    http.setTimeout(15000); http.setConnectTimeout(15000); http.setUserAgent("RenPhone-NyxStore");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    code = http.GET();
    if (code == HTTP_CODE_OK) {
      int len = http.getSize();
      if (expect) {
        if (len <= 0) { strncpy(nsJobErr, "Server tidak mengirim ukuran berkas", sizeof(nsJobErr) - 1); http.end(); return false; }
        if ((uint32_t)len != expect) { snprintf(nsJobErr, sizeof(nsJobErr), "Ukuran beda dari indeks (%d / %u B)", len, (unsigned)expect); http.end(); return false; }
        WiFiClient* st = http.getStreamPtr(); unsigned long lastData = millis();
        while (!nsAbort && nsBufLen < expect && (http.connected() || st->available()) && millis() - lastData < 15000UL) {
          int av = st->available();
          if (av > 0) {
            size_t room = expect - nsBufLen; size_t want = (size_t)av < room ? (size_t)av : room;
            int rd = st->readBytes(nsBuf + nsBufLen, want);
            if (rd > 0) { nsBufLen += (uint32_t)rd; nsGot = nsBufLen; lastData = millis(); }
          } else delay(5);
        }
        ok = !nsAbort && nsBufLen == expect;
        if (!ok && !nsAbort) snprintf(nsJobErr, sizeof(nsJobErr), "Unduhan terputus (%u / %u B)", (unsigned)nsBufLen, (unsigned)expect);
      } else {
        if (len > 0 && (uint32_t)len > nsBufCap) { strncpy(nsJobErr, "Berkas terlalu besar", sizeof(nsJobErr) - 1); http.end(); return false; }
        String body = rpHttpBody(http);           // aman utk chunked; berkasnya kecil (indeks / manifest)
        if (body.length() == 0 || body.length() >= nsBufCap) { strncpy(nsJobErr, "Isi kosong / terlalu besar", sizeof(nsJobErr) - 1); http.end(); return false; }
        memcpy(nsBuf, body.c_str(), body.length()); nsBuf[body.length()] = 0;
        nsBufLen = (uint32_t)body.length(); nsGot = nsBufLen; ok = !nsAbort;
      }
    }
    http.end();
    if (code != -1) break;                        // kode HTTP asli atau selesai: jangan ulang (-1 = koneksi gagal, sering sementara)
  }
  if (!ok && !nsJobErr[0] && !nsAbort) {
    if (code == 404) strncpy(nsJobErr, "Berkas tidak ada di rilis (404). Build Nyx Apps belum terbit?", sizeof(nsJobErr) - 1);
    else snprintf(nsJobErr, sizeof(nsJobErr), "Gagal unduh, kode %d", code);
  }
  if (ok && nsWantSha) {
    unsigned char dg[32]; mbedtls_sha256(nsBuf, nsBufLen, dg, 0);
    nyxstore::toHex(dg, 32, nsShaCalc);
  }
  return ok;
}

static void nsWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool ok = nsDoJob(nsJob);
    nsJobSt = ok ? NSW_OK : NSW_FAIL;
  }
}

// mulai satu tugas jaringan. cap = kapasitas penyangga (PSRAM). false bila gagal memulai (nsErr terisi).
static bool nsStartJob(int job, const char* file, uint32_t expect, uint32_t cap, bool sha) {
  if (nsJobSt == NSW_RUN) return false;
  if (!nyxstore::joinUrl(NYXSTORE_BASE, file, nsUrl, sizeof(nsUrl))) { nsSetErr("URL terlalu panjang"); return false; }
  if (!nsBuf || nsBufCap < cap + 1) {
    if (nsBuf) { heap_caps_free(nsBuf); nsBuf = nullptr; nsBufCap = 0; }
    nsBuf = (uint8_t*)heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nsBuf) { nsSetErr(RPTR("PSRAM tidak cukup")); return false; }
    nsBufCap = cap + 1;
  }
  if (!nsTask) {
    BaseType_t r = createTaskPsramStack(nsWorker, "nyxStore", 12288, NULL, &nsStack, &nsTcb, &nsTask, 0);
    if (r != pdPASS) { nsTask = nullptr; nsSetErr("Gagal membuat task jaringan"); return false; }
  }
  nsJob = job; nsTotal = expect; nsGot = 0; nsBufLen = 0; nsWantSha = sha; nsShaCalc[0] = 0; nsAbort = false;
  nsJobSt = NSW_RUN;
  xTaskNotifyGive(nsTask);
  return true;
}

static void nsFreeBuf() { if (nsBuf && nsJobSt != NSW_RUN) { heap_caps_free(nsBuf); nsBuf = nullptr; nsBufCap = 0; nsBufLen = 0; } }

// ------------------------------ alur ------------------------------
static void nsStartFetch() {
  nsN = 0; nsScroll = 0; nsSel = -1; nsLeaveAfter = false;
  nyxPickFs();
  if (WiFi.status() != WL_CONNECTED) { nsSetErr(RPTR("Sambungkan WiFi dulu di Setting")); return; }
  if (!nyxAppFs) { nsSetErr(RPTR("Tidak ada SD maupun FFat")); return; }
  nsSub = NS_FETCH;
  if (!nsStartJob(NSJ_INDEX, NYXSTORE_INDEX, 0, NYXSTORE_INDEX_MAX, false)) { if (nsSub == NS_FETCH) nsSetErr("Tidak bisa memulai unduhan"); }
}

void nyxStoreOpen() {
  if (nsJobSt == NSW_RUN) { nsAbort = true; for (int i = 0; i < 150 && nsJobSt == NSW_RUN; i++) delay(20); }   // sisa tugas dari kunjungan lalu
  if (nsJobSt != NSW_RUN) nsJobSt = NSW_IDLE;
  nsFreeBuf();
  nsStartFetch();
}

// dipanggil saat Nyx Apps ditutup / daftar dibuka ulang: lepas penyangga & berkas setengah jadi
void nyxStoreReset() {
  if (nsOutOpen) { nsOut.close(); nsOutOpen = false; if (nyxAppFs && nsTmpPath[0]) nyxAppFs->remove(nsTmpPath); }
  if (nsJobSt == NSW_RUN) nsAbort = true; else { nsJobSt = NSW_IDLE; nsFreeBuf(); }
  nsSub = NS_FETCH; nsLeaveAfter = false;
}

static void nsBeginInstall(int idx) {
  nsSel = idx; nyxPickFs();
  if (!nyxAppFs) { nsSetErr(RPTR("Tidak ada SD maupun FFat")); return; }
  if (WiFi.status() != WL_CONNECTED) { nsSetErr(RPTR("Sambungkan WiFi dulu di Setting")); return; }
  const nyxstore::Entry& e = nsApps[idx];
  if (nsFreeBytes() < (uint64_t)e.elfSize + NYXSTORE_MARGIN) { nsSetErr(RPTR("Ruang penyimpanan kurang")); return; }
  nsPhase = NSP_MANIFEST; nsMfLen = 0; nsWritten = 0; nsOutOpen = false; nsTmpPath[0] = 0;
  snprintf(nsStatus, sizeof(nsStatus), "%s", RPTR("Mengunduh manifest..."));
  nsSub = NS_WORK;
  if (!nsStartJob(NSJ_MANIFEST, e.manifest, 0, 1024, false)) { if (nsSub == NS_WORK) nsSetErr("Tidak bisa memulai unduhan"); }
}

static void nsFinishInstall() {
  // manifest.txt ditulis TERAKHIR (app baru muncul di daftar Nyx Apps hanya bila ELF sudah lengkap)
  const nyxstore::Entry& e = nsApps[nsSel];
  char p[96];
  snprintf(p, sizeof(p), "%s/manifest.txt", nsDir);
  { File f = nyxAppFs->open(p, FILE_WRITE);
    if (!f) { nsSetErr("Gagal menulis manifest"); return; }
    size_t w = f.write((const uint8_t*)nsMfTxt, nsMfLen); f.close();
    if (w != nsMfLen) { nyxAppFs->remove(p); nsSetErr("Gagal menulis manifest"); return; } }
  snprintf(p, sizeof(p), "%s/store.txt", nsDir);
  { File f = nyxAppFs->open(p, FILE_WRITE);
    if (f) { char rec[96]; int n = snprintf(rec, sizeof(rec), "sha256=%s\n", e.sha); f.write((const uint8_t*)rec, (size_t)n); f.close(); } }
  snprintf(p, sizeof(p), "%s/data", nsDir);
  if (!nyxAppFs->exists(p)) nyxAppFs->mkdir(p);
  char t[64]; snprintf(t, sizeof(t), "%s: %s", RPTR("Terpasang"), e.name);
  showToast(t, 2000);
  nsFreeBuf(); nsRefreshStates(); nsSub = NS_LIST;
}

static void nsTickWork() {
  if (nsPhase == NSP_WRITE) {                      // ---- tulis ELF ke penyimpanan, per-frame ----
    if (nsAbort) {                                 // dibatalkan di tengah penulisan
      if (nsOutOpen) { nsOut.close(); nsOutOpen = false; }
      nyxAppFs->remove(nsTmpPath); nsAbort = false; nsFreeBuf(); nsSub = NS_LIST; return;
    }
    const uint32_t total = nsBufLen;
    unsigned long t0 = millis();
    while (nsWritten < total && millis() - t0 < NYXSTORE_SLICE_MS) {
      uint32_t n = total - nsWritten; if (n > NYXSTORE_CHUNK) n = NYXSTORE_CHUNK;
      size_t w = nsOut.write(nsBuf + nsWritten, n);
      if (w != n) { nsOut.close(); nsOutOpen = false; nyxAppFs->remove(nsTmpPath); nsFreeBuf(); nsSetErr(RPTR("Penyimpanan penuh / gagal menulis")); return; }
      nsWritten += n;
    }
    nsGot = nsWritten;
    if (nsWritten < total) return;
    nsOut.close(); nsOutOpen = false;
    nyxAppFs->remove(nsDstPath);                   // update: buang ELF lama; rename gagal -> catatan lama tetap menunjuk berkas yang hilang -> dilaporkan
    if (!nyxAppFs->rename(nsTmpPath, nsDstPath)) { nyxAppFs->remove(nsTmpPath); nsFreeBuf(); nsSetErr("Gagal mengganti nama berkas ELF"); return; }
    nsFinishInstall();
    return;
  }

  // ---- fase jaringan ----
  if (nsJobSt == NSW_RUN) {
    if (nsAbort) snprintf(nsStatus, sizeof(nsStatus), "%s", RPTR("Membatalkan..."));
    return;
  }
  if (nsAbort) { nsJobSt = NSW_IDLE; nsAbort = false; nsFreeBuf(); nsSub = NS_LIST; return; }
  if (nsJobSt == NSW_FAIL) { nsJobSt = NSW_IDLE; nsFreeBuf(); nsSetErr(nsJobErr[0] ? nsJobErr : "Unduhan gagal"); return; }
  if (nsJobSt != NSW_OK) return;
  nsJobSt = NSW_IDLE;
  const nyxstore::Entry& e = nsApps[nsSel];

  if (nsPhase == NSP_MANIFEST) {
    if (nsBufLen == 0 || nsBufLen >= sizeof(nsMfTxt)) { nsFreeBuf(); nsSetErr("Manifest kosong / terlalu besar"); return; }
    memcpy(nsMfTxt, nsBuf, nsBufLen); nsMfLen = nsBufLen; nsMfTxt[nsMfLen] = 0;
    nyx::parseManifest(nsMfTxt, nsMfLen, &nsMf);
    if (!nsMf.valid) { nsFreeBuf(); nsSetErr("Manifest dari server tidak valid"); return; }
    if (nsPermMask(e.perms) != nsMf.perms) { nsFreeBuf(); nsSetErr("Izin di manifest beda dari indeks. Dibatalkan"); return; }   // cegah indeks & manifest tak sinkron
    nsPhase = NSP_ELF;
    snprintf(nsStatus, sizeof(nsStatus), "%s", RPTR("Mengunduh app..."));
    if (!nsStartJob(NSJ_ELF, e.elf, e.elfSize, e.elfSize, true)) { if (nsSub == NS_WORK) nsSetErr("Tidak bisa memulai unduhan"); }
    return;
  }

  if (nsPhase == NSP_ELF) {
    if (strcmp(nsShaCalc, e.sha) != 0) { nsFreeBuf(); nsSetErr(RPTR("SHA-256 tidak cocok. Berkas ditolak")); return; }
    // siapkan folder + berkas sementara
    snprintf(nsDir, sizeof(nsDir), "/apps/%s", e.id);
    snprintf(nsTmpPath, sizeof(nsTmpPath), "%s/%s.tmp", nsDir, nsMf.elf);
    snprintf(nsDstPath, sizeof(nsDstPath), "%s/%s", nsDir, nsMf.elf);
    if (!nyxAppFs->exists("/apps")) nyxAppFs->mkdir("/apps");
    if (!nyxAppFs->exists(nsDir)) nyxAppFs->mkdir(nsDir);
    nsOut = nyxAppFs->open(nsTmpPath, FILE_WRITE);
    if (!nsOut) { nsFreeBuf(); nsSetErr(RPTR("Gagal membuka berkas tujuan")); return; }
    nsOutOpen = true; nsWritten = 0; nsPhase = NSP_WRITE;
    snprintf(nsStatus, sizeof(nsStatus), "%s", RPTR("Menulis ke penyimpanan..."));
  }
}

static void nsTickFetch() {
  if (nsJobSt == NSW_RUN) return;
  if (nsAbort || nsLeaveAfter) { nsJobSt = NSW_IDLE; nsFreeBuf(); nsAbort = false; nsLeaveAfter = false; nyxState = NYX_ST_LIST; nyxScan(); return; }
  if (nsJobSt == NSW_FAIL) { nsJobSt = NSW_IDLE; nsFreeBuf(); nsSetErr(nsJobErr[0] ? nsJobErr : "Gagal memuat katalog"); return; }
  if (nsJobSt != NSW_OK) return;
  nsJobSt = NSW_IDLE;
  int skipped = 0;
  nsBuf[nsBufLen] = 0;
  int n = nyxstore::parseIndex((const char*)nsBuf, nsBufLen, nsApps, NYXSTORE_MAX_APPS, &skipped);
  nsFreeBuf();
  if (n < 0) { nsSetErr("Indeks katalog rusak"); return; }
  if (skipped) Serial.printf("[NyxStore] %d entri indeks ditolak (id/berkas/SHA/ukuran tidak sah)\n", skipped);
  nsN = n; nsScroll = 0;
  nsRefreshStates();
  nsSub = NS_LIST;
}

// dipanggil drawNyxApp() tiap frame saat layar Store aktif
void nyxStoreTick() {
  if (nsSub == NS_FETCH) nsTickFetch();
  else if (nsSub == NS_WORK) nsTickWork();
}

bool nyxStoreBusy() { return nsSub == NS_FETCH || nsSub == NS_WORK; }

// Kembali: true = sudah ditangani di dalam Store; false = keluar dari Nyx Apps seperti biasa
bool nyxStoreBack() {
  if (nsSub == NS_WORK) { nsAbort = true; snprintf(nsStatus, sizeof(nsStatus), "%s", RPTR("Membatalkan...")); return true; }
  if (nsSub == NS_FETCH) { nsAbort = true; nsLeaveAfter = true; return true; }
  if (nsSub == NS_DETAIL || nsSub == NS_CONFIRM_RM) { nsSub = NS_LIST; return true; }
  if (nsSub == NS_ERR && nsN > 0) { nsSub = NS_LIST; return true; }
  // NS_LIST / NS_ERR tanpa daftar -> kembali ke daftar Nyx Apps
  nyxStoreReset(); nyxState = NYX_ST_LIST; nyxScan();
  return true;
}

// ------------------------------ gambar ------------------------------
static void nsFitText(LGFX_Sprite& s, int x, int y, const char* str, uint16_t col, int maxW) {
  s.setTextColor(col);
  s.setTextSize(2);
  if (s.textWidth(str) > maxW) s.setTextSize(1);
  s.setCursor(x, y + (s.textWidth("A") > 8 ? 0 : 4)); s.print(str);
  s.setTextSize(1);
}

static void nsPill(LGFX_Sprite& s, int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg) {
  nyxAaRR(s, x, y, w, h, h / 2, bg);
  s.setTextSize(1); s.setTextColor(fg);
  int tw = s.textWidth(label); s.setCursor(x + (w - tw) / 2, y + (h - 8) / 2); s.print(label);
}

static const char* nsLabelFor(int st) {
  switch (st) {
    case nyxstore::ST_NOT_INSTALLED: return RPTR("Pasang");
    case nyxstore::ST_UPDATE:        return RPTR("Perbarui");
    default:                         return RPTR("Terpasang");
  }
}

// NYXSTORE_UI_V2 -- tanda tangan hanya tipe primitif (aturan prototipe arduino-cli)
static void nsIcon(LGFX_Sprite& s, int cx, int cy, int r, char sym, uint16_t col) {
  modernIconBase(s, cx, cy, r, col);
  s.setTextColor(appIconInk(col));
  int sz = (r >= 24) ? 3 : 2; s.setTextSize(sz);
  char t[2] = { sym, 0 };
  s.setCursor(cx - s.textWidth(t) / 2, cy - (sz * 8) / 2); s.print(t);
  s.setTextSize(1);
}

static void nsDrawHeader(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Nyx Store");
  if (nyxAppFs) {
    const char* w = nyxFsFlash ? "Flash" : "SD";
    nsPill(s, SCR_W - 8 - 46, 28, 46, 18, w, blend565(T().surface2, T().text, 40), T().subtext);
  }
  if ((nsSub == NS_LIST || nsSub == NS_DETAIL || nsSub == NS_CONFIRM_RM || nsSub == NS_WORK) && nsN > 0) {
    int upd = 0; for (int i = 0; i < nsN; i++) if (nsAppState[i] == nyxstore::ST_UPDATE) upd++;
    char sb[40];
    if (upd > 0) snprintf(sb, sizeof(sb), "%d app  \xC2\xB7  %d update", nsN, upd);
    else         snprintf(sb, sizeof(sb), "%d app", nsN);
    s.setTextSize(1); s.setTextColor(upd > 0 ? (uint16_t)0xFD20 : (uint16_t)T().subtext);
    s.setCursor(12, 49); s.print(sb);
  }
}

static void nsDrawList(LGFX_Sprite& s) {
  nsDrawHeader(s);
  int top = NYXSTORE_TOP, bot = backY() - 4;
  if (nsN == 0) {
    s.setTextColor(T().subtext); s.setTextSize(1); s.setCursor(12, top + 10); s.print(RPTR("Katalog kosong"));
    return;
  }
  s.setClipRect(0, top, SCR_W, bot - top);
  const int pillW = 68;
  for (int i = 0; i < nsN; i++) {
    int ry = top + i * NYXSTORE_ROW_H - (int)nsScroll;
    if (ry + NYXSTORE_ROW_H < top || ry > bot) continue;
    const nyxstore::Entry& e = nsApps[i];
    int st = nsAppState[i]; bool act = (st == nyxstore::ST_NOT_INSTALLED || st == nyxstore::ST_UPDATE);
    drawGlassPanel(s, 8, ry + 3, SCR_W - 16, NYXSTORE_ROW_H - 7, 14, T().surface2, 170);
    nsIcon(s, 8 + 12 + 20, ry + NYXSTORE_ROW_H / 2, 20, e.sym, e.color);
    int tx = 8 + 12 + 48, avail = SCR_W - 8 - 10 - pillW - tx - 6;
    nsFitText(s, tx, ry + 11, e.name, T().text, avail);
    char sz[16], sub[40]; nyxstore::fmtSize(e.elfSize, sz, sizeof(sz));
    snprintf(sub, sizeof(sub), "%s%s%s", sz, e.perms[0] ? "  \xC2\xB7  " : "", e.perms);
    if (e.test) { size_t l = strlen(sub); snprintf(sub + l, sizeof(sub) - l, "  \xC2\xB7  %s", RPTR("UJI")); }
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, ry + 36); s.print(sub);
    uint16_t bg = (st == nyxstore::ST_UPDATE) ? 0xFD20 : (act ? (uint16_t)T().accent : blend565(T().surface2, T().text, 40));
    nsPill(s, SCR_W - 8 - 10 - pillW, ry + NYXSTORE_ROW_H / 2 - 12, pillW, 24, nsLabelFor(st), bg, act ? 0xFFFF : T().subtext);
  }
  s.clearClipRect();
}

static void nsBtn(int i, int x, int y, int w, int h, int id) { nsBtns[i].x = x; nsBtns[i].y = y; nsBtns[i].w = w; nsBtns[i].h = h; nsBtns[i].id = id; }

// susun tombol kartu rincian untuk app terpilih (dipakai gambar & sentuh)
static void nsLayoutDetail() {
  int bot = backY() - 4, y = bot - 32, x0 = 16, wAll = SCR_W - 32, gap = 8;
  int st = nsAppState[nsSel];
  nsBtnN = 0;
  if (nsSub == NS_CONFIRM_RM) {
    int w = (wAll - gap) / 2;
    nsBtn(nsBtnN++, x0, y, w, 28, NSB_YES_RM); nsBtn(nsBtnN++, x0 + w + gap, y, w, 28, NSB_CANCEL);
    return;
  }
  if (st == nyxstore::ST_NOT_INSTALLED) {
    int w = (wAll - gap) / 2;
    nsBtn(nsBtnN++, x0, y, w, 28, NSB_INSTALL); nsBtn(nsBtnN++, x0 + w + gap, y, w, 28, NSB_CANCEL);
  } else {
    int w = (wAll - 2 * gap) / 3;
    nsBtn(nsBtnN++, x0, y, w, 28, NSB_INSTALL); nsBtn(nsBtnN++, x0 + w + gap, y, w, 28, NSB_REMOVE); nsBtn(nsBtnN++, x0 + 2 * (w + gap), y, w, 28, NSB_CANCEL);
  }
}

static void nsDrawDetail(LGFX_Sprite& s) {
  nsDrawHeader(s);
  const nyxstore::Entry& e = nsApps[nsSel];
  int st = nsAppState[nsSel];
  int top = NYXSTORE_TOP, W = SCR_W;
  nsLayoutDetail();

  // ---- kartu hero ----
  drawGlassPanel(s, 8, top, W - 16, 70, 14, T().surface2, 180);
  nsIcon(s, 8 + 14 + 26, top + 35, 26, e.sym, e.color);
  int tx = 8 + 14 + 60;
  nsFitText(s, tx, top + 10, e.name, T().text, W - 8 - 10 - tx);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, top + 34); s.print(e.id);
  const char* msg = (st == nyxstore::ST_UPDATE) ? RPTR("Ada versi baru")
                  : (st == nyxstore::ST_UP_TO_DATE) ? RPTR("Sudah versi terbaru")
                  : (st == nyxstore::ST_UNKNOWN) ? RPTR("Terpasang manual (bukan dari Store)") : "";
  if (*msg) { s.setTextColor(st == nyxstore::ST_UPDATE ? (uint16_t)0xFD20 : (uint16_t)T().accent); s.setCursor(tx, top + 49); s.print(msg); }

  int y = top + 78;
  if (nsSub == NS_CONFIRM_RM) {
    drawGlassPanel(s, 8, y, W - 16, 60, 12, blend565(T().surface2, 0xF800, 50), 190);
    s.setTextSize(1); s.setTextColor(T().text); s.setCursor(18, y + 16); s.print(RPTR("Hapus app ini?"));
    s.setTextColor(T().subtext); s.setCursor(18, y + 34); s.print(RPTR("Semua datanya ikut terhapus."));
  } else {
    // ---- 3 tile info ----
    int gap = 8, tw = (W - 16 - 2 * gap) / 3, th = 40;
    char v0[16], v1[16]; nyxstore::fmtSize(e.elfSize, v0, sizeof(v0)); snprintf(v1, sizeof(v1), "%u KB", (unsigned)e.memKb);
    const char* lab[3] = { RPTR("Ukuran"), RPTR("Memori"), RPTR("Izin") };
    const char* val[3] = { v0, v1, e.perms[0] ? e.perms : "-" };
    for (int i = 0; i < 3; i++) {
      int x = 8 + i * (tw + gap);
      drawGlassPanel(s, x, y, tw, th, 10, T().surface2, 170);
      s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(x + 8, y + 7); s.print(lab[i]);
      nsFitText(s, x + 8, y + 19, val[i], T().text, tw - 14);
    }
    // ---- kotak peringatan ----
    int y2 = y + th + 8;
    drawGlassPanel(s, 8, y2, W - 16, 44, 10, blend565(T().surface2, 0xFD20, 50), 170);
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(18, y2 + 10); s.print(RPTR("App native tidak di-sandbox."));
    s.setCursor(18, y2 + 24); s.print(RPTR("Pasang hanya yang kamu percaya."));
  }
  for (int i = 0; i < nsBtnN; i++) {
    const NsBtnRect& r = nsBtns[i]; const char* lb = ""; uint16_t bg = blend565(T().surface2, T().text, 50), fg = T().text;
    if (r.id == NSB_INSTALL) { lb = (st == nyxstore::ST_NOT_INSTALLED) ? RPTR("Pasang") : (st == nyxstore::ST_UPDATE ? RPTR("Perbarui") : RPTR("Pasang ulang")); bg = T().accent; fg = 0xFFFF; }
    else if (r.id == NSB_REMOVE || r.id == NSB_YES_RM) { lb = RPTR("Hapus"); bg = 0xF800; fg = 0xFFFF; }
    else lb = RPTR("Batal");
    nsPill(s, r.x, r.y, r.w, r.h, lb, bg, fg);
  }
}

static void nsDrawWork(LGFX_Sprite& s) {
  nsDrawHeader(s);
  int top = NYXSTORE_TOP, W = SCR_W;
  const nyxstore::Entry& e = nsApps[nsSel];
  drawGlassPanel(s, 8, top, W - 16, 100, 14, T().surface2, 180);
  nsIcon(s, 8 + 14 + 26, top + 36, 26, e.sym, e.color);
  // progres: manifest 0-5 %, unduh 5-60 %, tulis 60-100 %
  uint32_t got = nsGot, tot = nsTotal ? nsTotal : 1; int pct = 0;
  if (nsPhase == NSP_MANIFEST) pct = 2;
  else if (nsPhase == NSP_ELF) pct = 5 + (int)((uint64_t)got * 55 / tot);
  else { uint32_t wt = nsBufLen ? nsBufLen : 1; pct = 60 + (int)((uint64_t)nsWritten * 40 / wt); }
  if (pct > 100) pct = 100;
  int tx = 8 + 14 + 60;
  char pc[8]; snprintf(pc, sizeof(pc), "%d%%", pct);
  s.setTextSize(2); int pw = s.textWidth(pc);
  s.setTextColor(T().accent); s.setCursor(W - 8 - 14 - pw, top + 12); s.print(pc);
  s.setTextSize(1);
  nsFitText(s, tx, top + 12, e.name, T().text, W - 8 - 14 - pw - 8 - tx);
  s.setTextColor(T().subtext); s.setCursor(tx, top + 40); s.print(nsStatus);
  int bx = 8 + 14, by = top + 74, bw = W - 16 - 28;
  nyxAaRR(s, bx, by, bw, 10, 5, blend565(T().surface2, T().text, 40));
  int fw = bw * pct / 100; if (fw > 10) nyxAaRR(s, bx, by, fw, 10, 5, T().accent);
}

static void nsDrawFetch(LGFX_Sprite& s) {
  nsDrawHeader(s);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(12, 56); s.print(RPTR("Memuat katalog..."));
}

static void nsDrawErr(LGFX_Sprite& s) {
  nsDrawHeader(s);
  nyxAaRR(s, 8, 52, SCR_W - 16, 78, 10, blend565(T().bg, 0xF800, 60));
  s.setTextSize(1); s.setTextColor(T().text);
  int y = 60; const char* p = nsErr; char line[48]; int maxc = (SCR_W - 32) / 6; if (maxc > 46) maxc = 46;
  while (*p && y < 106) {
    int n = 0; while (p[n] && n < maxc) n++;
    memcpy(line, p, (size_t)n); line[n] = 0; s.setCursor(16, y); s.print(line); y += 12; p += n;
  }
  s.setTextColor(T().subtext); s.setCursor(16, 114); s.print(RPTR("Ketuk untuk coba lagi"));
}

void nyxStoreDraw(LGFX_Sprite& s) {
  if (nsSub == NS_FETCH) nsDrawFetch(s);
  else if (nsSub == NS_LIST) nsDrawList(s);
  else if (nsSub == NS_DETAIL || nsSub == NS_CONFIRM_RM) nsDrawDetail(s);
  else if (nsSub == NS_WORK) nsDrawWork(s);
  else nsDrawErr(s);
}

// ------------------------------ sentuh ------------------------------
void nyxStoreTouch(int x, int y, bool held, bool isNew) {
  if (nsSub == NS_FETCH || nsSub == NS_WORK) return;                      // sibuk: hanya Kembali yang berlaku
  if (nsSub == NS_ERR) {
    if (isNew) { if (nsN > 0) nsSub = NS_LIST; else nsStartFetch(); needRedraw = true; }
    return;
  }
  if (isNew) { nsLastX = x; nsLastY = y; nsDownY = y; nsMoved = false; }
  else if (held) { nsLastX = x; nsLastY = y; }

  if (nsSub == NS_DETAIL || nsSub == NS_CONFIRM_RM) {
    if (isNew || held) return;                                             // aksi dieksekusi saat jari dilepas
    nsLayoutDetail();
    for (int i = 0; i < nsBtnN; i++) {
      const NsBtnRect& r = nsBtns[i];
      if (nsLastX >= r.x && nsLastX < r.x + r.w && nsLastY >= r.y && nsLastY < r.y + r.h) {
        if (r.id == NSB_INSTALL) nsBeginInstall(nsSel);
        else if (r.id == NSB_REMOVE) nsSub = NS_CONFIRM_RM;
        else if (r.id == NSB_YES_RM) {
          char d[40]; snprintf(d, sizeof(d), "/apps/%s", nsApps[nsSel].id);
          nyxPickFs();
          if (nyxAppFs && nyxIdOk(nsApps[nsSel].id) && nsRmTree(*nyxAppFs, d, 0)) showToast(RPTR("Dihapus"), 1600);
          else showToast("Gagal menghapus", 2000);
          nsRefreshStates(); nsSub = NS_LIST;
        } else nsSub = NS_LIST;
        needRedraw = true; return;
      }
    }
    return;
  }

  // ---- daftar: seret untuk menggulir, ketuk untuk membuka rincian ----
  int top = NYXSTORE_TOP, bot = backY() - 4;
  if (isNew) { nsDrag = (y >= top && y <= bot); nsPrevY = y; return; }
  if (!held) {
    if (nsDrag && !nsMoved) {
      int row = (nsLastY - top + (int)nsScroll) / NYXSTORE_ROW_H;
      if (row >= 0 && row < nsN) { nsSel = row; nsSub = NS_DETAIL; needRedraw = true; }
    }
    nsDrag = false; return;
  }
  if (!nsDrag) return;
  if (abs(y - nsDownY) > 8) nsMoved = true;
  nsScroll += (float)(nsPrevY - y); nsPrevY = y;
  float maxS = (float)(nsN * NYXSTORE_ROW_H - (bot - top)); if (maxS < 0) maxS = 0;
  if (nsScroll < 0) nsScroll = 0;
  if (nsScroll > maxS) nsScroll = maxS;
  needRedraw = true;
}
