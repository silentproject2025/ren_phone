// =====================================================================
// gutenberg_renphone.ino -- cari & unduh buku gratis dari Project Gutenberg, di dalam app BUKU
// =====================================================================
// Dibuka dari tombol "Gutenberg" di kepala perpustakaan Buku (mode EBK_GUT di ebook_renphone.ino).
// Alur: daftar populer / hasil cari -> ketuk buku -> Detail -> Unduh -> berkas masuk /books -> Buka.
//
//   * Pencarian lewat Gutendex (https://gutendex.com/books/) -- API JSON komunitas atas katalog
//     Gutenberg. Hanya buku berstatus domain publik (copyright=false). Filter bahasa & urutan.
//   * Unduh EPUB (disarankan: sampul, daftar isi, judul/pengarang) atau TXT bila tak ada EPUB.
//     EPUB default "tanpa gambar" (jauh lebih kecil); bisa diganti "Dgn gbr" lewat chip.
//   * Semua kerja jaringan di task terpisah (stack PSRAM, pola sama dgn APOD/Trivia) supaya UI
//     tidak macet. Hanya satu pekerjaan sekaligus, dan jeda >= 1,5 dtk antar permintaan:
//     Gutenberg meminta akses otomatis tidak agresif (satu aksi pengguna = satu permintaan).
//   * Berkas ditulis ke "<nama>.part" lalu di-rename -> unduhan setengah jalan tidak pernah
//     muncul sebagai buku rusak. Batal / keluar app menghentikan unduhan.
//
// Logika murni (JSON, URL, nama berkas) ada di gutendex_core.h dan diuji di komputer
// (tests_gutenberg/). Berkas ini dikompilasi SETELAH ebook_renphone.ino, jadi boleh memakai
// fungsi static-nya (ebkScan, ebkStartOpen, ebkFitText, ...).
// ATURAN PROYEK: buffer besar di PSRAM; fungsi di sini tidak memakai tipe struct di signature
// (bug prototype arduino-cli).
// =====================================================================

#include "gutendex_core.h"

#define GUT_CAP        96          // maks buku di daftar (3 halaman Gutendex)
#define GUT_ROW_H      46
#define GUT_JSON_CAP   (320u * 1024u)
#define GUT_TXT_MAX    (3u * 1024u * 1024u)    // batas TXT pembaca Buku
#define GUT_EPUB_MAX   (40u * 1024u * 1024u)
#define GUT_GAP_MS     1500        // jeda minimum antar permintaan ke server
#define GUT_DL_REDRAW_MS 1000      // selama mengunduh layar digambar ulang tiap ~1 dtk (UI berat bisa menahan pembacaan jaringan)
#define GUT_IDLE_MS    8000        // diam selama ini = anggap macet, sambung ulang
#define GUT_MAX_STALL  6           // sambung ulang berturut-turut tanpa kemajuan sebelum menyerah (dengan jeda makin panjang)
#define GUT_MAX_TRY    40          // maks sambungan ulang per unduhan (berhenti lebih awal bila 3x tanpa kemajuan)
#define GUT_PARALLEL   0           // 1 = unduh dgn 2 koneksi paralel (Range, tiap koneksi separuh berkas); 0 = satu koneksi saja
#define GUT_TLS_MIN_BLOCK 24000u        // blok RAM internal terbesar yg diminta sebelum (sambung ulang); mbedTLS sudah di PSRAM, jadi ambang rendah. TEBAKAN, sesuaikan dari log
#define GUT_PAR_MIN_BLOCK 30000u        // paralel hanya dicoba bila blok internal terbesar >= ini ... (TEBAKAN)
#define GUT_PAR_MIN_FREE  60000u        // ... dan total RAM internal bebas >= ini (TEBAKAN)
#define GUT_PAR_MIN    (192u * 1024u)   // berkas lebih kecil dari ini: satu koneksi saja (handshake TLS kedua tak sepadan)

enum { GUT_JOB_NONE = 0, GUT_JOB_SEARCH, GUT_JOB_DL };
enum { GUT_V_LIST = 0, GUT_V_DETAIL, GUT_V_PROG };
enum { GUT_PH_IDLE = 0, GUT_PH_CONNECT, GUT_PH_DOWNLOAD, GUT_PH_DONE, GUT_PH_ERROR };

static const char* const gutLangs[9]  = { "", "en", "id", "fr", "de", "es", "nl", "pt", "it" };
static const char* const gutLangUp[9] = { "Semua", "EN", "ID", "FR", "DE", "ES", "NL", "PT", "IT" };

// daftar & tampilan
static GutItem*  gutItems = nullptr;
static volatile int gutN = 0;               // jumlah buku yang SUDAH utuh (hanya task yang menambah)
static int       gutTotal = 0, gutPage = 1;
static volatile bool gutHasNext = false;
static String    gutQuery;
static char      gutQApplied[40] = "";
static uint8_t   gutLangIdx = 0, gutSortIdx = 0;
static bool      gutLight = true;           // EPUB tanpa gambar
static uint8_t   gutView = GUT_V_LIST;
static float     gutScroll = 0;
static int       gutSel = -1;
static bool      gutSelExists = false; static char gutSelName[96] = "";
static bool      gutAutoLoad = false;
static uint32_t  gutAnimMs = 0;
// sentuhan
static bool      gutDown = false, gutMoved = false;
static int       gutSx = 0, gutSy = 0, gutPy = 0;
// pekerjaan jaringan
static volatile bool    gutBusy = false;
static volatile uint8_t gutJob = GUT_JOB_NONE;
static volatile uint8_t gutPhase = GUT_PH_IDLE;
static volatile bool    gutCancel = false;
static volatile uint32_t gutGot = 0, gutTot = 0;
static volatile uint32_t gutLastReqMs = 0, gutEndMs = 0, gutT0 = 0;   // gutT0 = awal data pertama (utk kecepatan)
static char      gutMsg[64] = "";
static char      gutDiag[96] = "";
static bool      gutNoWbuf = false;     // true = tanpa buffer besar (tulis SD langsung per potongan): mode aman setelah data rusak saat ditulis
static uint8_t   gutHead[8] = {0}; static uint8_t gutHeadN = 0;   // 8 byte pertama yg DITERIMA dari jaringan (utk diagnosis "bukan EPUB")
static char      gutCT[64] = "";                                 // Content-Type (+ Content-Encoding) balasan server
static volatile uint32_t gutSdKBs = 0;                // kode diagnosis utk layar kesalahan (+ Serial)
static char      gutReqUrl[220] = "";
static volatile uint32_t gutInstBps = 0, gutSpdT = 0, gutSpdB = 0, gutLastDataMs = 0;   // kecepatan SAAT INI (jendela ~2 dtk)
static volatile uint16_t gutRetries = 0;               // jumlah sambung ulang unduhan berjalan
static char      gutPartUrl[GUT_URL_LEN + 8] = "";   // URL milik .part yang tertinggal (utk melanjutkan)
static bool      gutReqAppend = false;
static int       gutDlIdx = -1;
static char      gutDoneName[96] = "";
static StackType_t* gutStack = nullptr; static StaticTask_t gutTCB; static TaskHandle_t gutTaskH = NULL;
// unduhan paralel: koneksi 0 jalan di task unduh utama, koneksi 1 di task pembantu (stack PSRAM juga)
static StackType_t* gutStack2 = nullptr; static StaticTask_t gutTCB2; static TaskHandle_t gutTaskH2 = NULL;
static portMUX_TYPE gutMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t gutLaneGot[2] = { 0, 0 };     // byte yg sudah diterima per koneksi
static volatile uint8_t  gutLaneState[2] = { 0, 0 };   // 0 diam | 1 menyambung | 2 menerima | 3 sukses | 4 gagal
static volatile int      gutLaneRc[2] = { 0, 0 };
static volatile bool     gutLaneAbort = false;         // koneksi 0 gagal -> koneksi 1 berhenti
static volatile bool     gutLane1Alive = false;
static volatile bool     gutParActive = false;         // utk tampilan "2 koneksi"
static char      gutL1Url[GUT_URL_LEN + 8] = "";
static char      gutL1Path[140] = "";
static uint32_t  gutL1From = 0, gutL1To = 0;

// ---------------------------------------------------------------------
// jaringan (di dalam task)
// ---------------------------------------------------------------------
// penampung keluaran HTTPClient::writeToStream: ke memori ATAU ke berkas SD, dgn hitung & batas & batal
class GutSink : public Stream {                          // HTTPClient::writeToStream menerima Stream*
 public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  uint8_t* mem = nullptr; uint32_t cap = 0; File* file = nullptr; uint32_t limit = 0;
  uint32_t got = 0; bool over = false, sdFail = false; uint32_t lastDraw = 0;
  uint8_t head[8] = {0}; uint8_t headN = 0;                // 8 byte pertama yg masuk (sebelum ke SD)
  uint8_t lane = 0xFF;                                     // 0xFF = unduhan tunggal; 0/1 = koneksi paralel (gutLaneGot[lane])
  // Tulis ke SD per blok besar: potongan jaringan cuma ~1,4 KB, dan SD card sangat lambat kalau ditulis sekecil itu.
  uint8_t* wbuf = nullptr; uint32_t wlen = 0, wcap = 0;
  uint32_t flushMs = 0, flushBytes = 0, flushMax = 0;      // statistik tulis SD (utk diagnosis kecepatan)
  ~GutSink() { if (wbuf) heap_caps_free(wbuf); }
  // Ukuran blok: maks 128 KB (sisa PSRAM tetap disisakan >= 1,5 MB utk app lain). Blok yang terlalu besar justru buruk:
  // jaringan BERHENTI dibaca selama blok ditulis ke SD, jadi blok 512 KB = jeda berdetik-detik. 128 KB = jeda ~0,1-0,4 dtk.
  // Gagal alokasi -> tulis langsung per potongan (cara lama).
  void useWriteBuffer() {
    if (!file || wbuf || gutNoWbuf) return;
    size_t fb = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    uint32_t want = 128u * 1024u;
    while (want > 32768u && fb < (size_t)want + 1536u * 1024u) want >>= 1;
    wbuf = (uint8_t*)heap_caps_aligned_alloc(64, want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // rata 64 B: DMA SD dari PSRAM S3 butuh alamat rata cache-line
    wcap = wbuf ? want : 0;
    Serial.printf("[GUT] buffer SD %u KB (PSRAM bebas terbesar %u KB)\n", (unsigned)(wcap / 1024), (unsigned)(fb / 1024));
  }
  bool flushSd() {
    if (!file || !wbuf || !wlen) return true;
    uint32_t t = millis();
    size_t w = file->write(wbuf, wlen);
    uint32_t d = millis() - t;
    flushMs += d; flushBytes += wlen; if (d > flushMax) flushMax = d;
    bool ok = (w == wlen); wlen = 0;
    if (!ok) sdFail = true;
    return ok;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    if (gutCancel || sdFail || over || (lane == 1 && gutLaneAbort)) return 0;                // sudah gagal: hentikan bersih (flag tetap menjelaskan sebabnya)
    if (limit && got + n > limit) { over = true; return 0; }
    if (headN < 8 && got < 8) { size_t k = 8 - headN; if (k > n) k = n; memcpy(head + headN, p, k); headN += (uint8_t)k; }
    if (mem) { if (got + n > cap) { over = true; return 0; } memcpy(mem + got, p, n); }
    else if (file) {
      if (wbuf) {
        size_t left = n; const uint8_t* q = p;
        while (left) {
          uint32_t room = wcap - wlen;
          uint32_t c = left < room ? (uint32_t)left : room;
          memcpy(wbuf + wlen, q, c); wlen += c; q += c; left -= c;
          if (wlen == wcap && !flushSd()) return 0;
        }
      } else { size_t w = file->write(p, n); if (w != n) { sdFail = true; return 0; } }
    }
    got += (uint32_t)n;
    if (lane == 0xFF) gutGot = got; else { gutLaneGot[lane] = got; gutGot = gutLaneGot[0] + gutLaneGot[1]; }
    { uint32_t ns = millis(); uint32_t tg = gutGot;          // kecepatan dihitung dari TOTAL kedua koneksi
      if (lane != 0xFF) portENTER_CRITICAL(&gutMux);
      gutLastDataMs = ns;
      if (!gutSpdT) { gutSpdT = ns; gutSpdB = tg; }
      else if (ns - gutSpdT >= 2000) { gutInstBps = (uint32_t)((uint64_t)(tg - gutSpdB) * 1000 / (ns - gutSpdT)); gutSpdT = ns; gutSpdB = tg; }
      if (lane != 0xFF) portEXIT_CRITICAL(&gutMux); }
    if (gutPhase == GUT_PH_CONNECT) { gutPhase = GUT_PH_DOWNLOAD; gutT0 = millis(); }
    uint32_t now = millis();
    if (now - lastDraw > GUT_DL_REDRAW_MS) { lastDraw = now; needRedraw = true; }   // gambar ulang layar jarang: UI & task unduh berbagi core 1
    return n;
  }
};

// return: 200 = ok | >0 = kode HTTP lain | -1 gagal konek/RAM | -2 dibatalkan | -3 putus/timeout | -4 terlalu besar | -5 gagal tulis SD
// manual=false: HTTPClient::writeToStream (mengurus chunked/Content-Length). manual=true: HTTP/1.0 + loop baca sendiri
// (tanpa chunked; server menutup koneksi di akhir) -- jalur cadangan kalau yang pertama putus di tengah.
// idleMs = batas diam menunggu data (Gutendex publik kadang lambat membalas, terutama urutan "Terbaru").
// resumeFrom > 0: kirim "Range: bytes=N-" supaya melanjutkan (file harus dibuka FILE_APPEND). return -7 = server tak dukung Range (ulang dari 0).
// *outGot = total byte yang sudah ada (termasuk resumeFrom).
static int gutFetch(const char* url, uint8_t* mem, uint32_t memCap, File* f, uint32_t limit, uint32_t* outGot, bool manual, uint32_t idleMs, uint32_t resumeFrom) {
  *outGot = resumeFrom; gutGot = resumeFrom; gutTot = 0; gutDiag[0] = 0;
  gutInstBps = 0; gutSpdT = 0; gutSpdB = 0; gutLastDataMs = millis();
  while ((uint32_t)(millis() - gutLastReqMs) < GUT_GAP_MS) { if (gutCancel) return -2; delay(40); }
  if (gutCancel) return -2;
  nasaWaitForHeap();                                   // beri waktu RAM internal lega sebelum TLS (pola APOD/NEO)
  WiFiClientSecure sc; WiFiClient pc;
  HTTPClient http;
  bool https = !strncmp(url, "https://", 8);
  if (https) { sc.setInsecure(); sc.setTimeout(idleMs / 1000 + 1); }
  bool ok = https ? http.begin(sc, url) : http.begin(pc, url);
  if (!ok) { snprintf(gutDiag, sizeof(gutDiag), "begin gagal"); return -1; }
  http.setUserAgent("NyxOS-Buku/1.0 (ESP32-S3; pembaca e-book)");
  http.setTimeout(idleMs > 60000 ? 60000 : idleMs); http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (manual) http.useHTTP10(true);
  { const char* hk[2] = { "Content-Type", "Content-Encoding" }; http.collectHeaders(hk, 2); }
  if (resumeFrom) { char rg[32]; snprintf(rg, sizeof(rg), "bytes=%u-", (unsigned)resumeFrom); http.addHeader("Range", rg); }
  uint32_t t0 = millis();
  int code = http.GET();
  gutLastReqMs = millis();
  Serial.printf("[GUT] GET %s%s -> %d (%lu ms)\n", manual ? "(1.0) " : "", url, code, (unsigned long)(millis() - t0));
  if (resumeFrom && code == 200) { http.end(); snprintf(gutDiag, sizeof(gutDiag), "server tak dukung Range"); return -7; }   // kirim ulang penuh: jangan ditempel
  if (code != 200 && !(resumeFrom && code == 206)) {
    snprintf(gutDiag, sizeof(gutDiag), "GET=%d %s int=%uK/%uK", code, manual ? "http1.0" : "http1.1",
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    Serial.printf("[GUT] GET gagal %d (%s), RAM internal: blok terbesar %u B, bebas %u B\n", code, HTTPClient::errorToString(code).c_str(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    http.end(); return code > 0 ? code : (code == -11 ? -3 : -1);
  }
  if (!resumeFrom) {
    String ct = http.header("Content-Type"), ce = http.header("Content-Encoding");
    snprintf(gutCT, sizeof(gutCT), "%.40s%s%.12s", ct.c_str(), ce.length() ? " enc=" : "", ce.c_str());
  }
  int len = http.getSize();
  uint32_t want = (len > 0) ? resumeFrom + (uint32_t)len : 0;      // total byte akhir yang diharapkan
  if (want) gutTot = want;
  if (limit && want && want > limit) { http.end(); return -4; }
  GutSink sink; sink.mem = mem; sink.cap = memCap; sink.file = f; sink.limit = limit; sink.useWriteBuffer();
  sink.got = resumeFrom;
  int wr = 0; bool idle = false;
  if (!manual) wr = http.writeToStream(&sink);
  else {
    WiFiClient* st = http.getStreamPtr();
    uint8_t* chunk = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!chunk || !st) { if (chunk) heap_caps_free(chunk); http.end(); return -1; }
    uint32_t last = millis();
    while (!gutCancel) {
      if (want && sink.got >= want) break;
      int av = st->available();
      if (av > 0) {
        int n = st->readBytes(chunk, av > 16384 ? 16384 : av);
        if (n <= 0) continue;
        if (sink.write(chunk, (size_t)n) != (size_t)n) { wr = -10; break; }
        last = millis();
      } else {
        if (!st->connected()) break;                    // server menutup = akhir data (HTTP/1.0)
        if (millis() - last > idleMs) { idle = true; break; }
        delay(1);
      }
    }
    heap_caps_free(chunk);
  }
  http.end();
  gutLastReqMs = millis();
  if (f && !gutCancel && !sink.sdFail) sink.flushSd();      // sisa blok terakhir
  *outGot = sink.got;
  if (!resumeFrom) { gutHeadN = sink.headN; memcpy(gutHead, sink.head, 8); }
  snprintf(gutDiag, sizeof(gutDiag), "%s wr=%d got=%u/%u%s%s%s%s fl=%u/%ums", manual ? "http1.0" : "http1.1", wr, (unsigned)sink.got, (unsigned)want,
           idle ? " idle" : "", sink.sdFail ? " sd!" : "", sink.over ? " lim!" : "", gutCancel ? " batal" : "", (unsigned)sink.flushMax, (unsigned)sink.flushMs);
  gutSdKBs = sink.flushMs ? (uint32_t)((uint64_t)sink.flushBytes * 1000 / sink.flushMs / 1024) : 0;
  Serial.printf("[GUT] selesai: %s\n", gutDiag);
  if (gutCancel) return -2;
  if (sink.over) return -4;
  if (sink.sdFail) return -5;
  if (wr == -10) return -6;                          // HTTPClient: tulis ke penampung gagal (bukan SD/batas/batal)
  if (wr < 0 || idle || (want && sink.got < want) || sink.got == 0) return -3;
  return 200;
}

// ---------------------------------------------------------------------
// unduhan paralel (2 koneksi): berkas dibelah dua dgn header Range.
//   koneksi 0 -> byte [0, half)      -> "<nama>.part"   (awalan utuh: kalau gagal, jalur satu-koneksi di bawah melanjutkannya)
//   koneksi 1 -> byte [half, total)  -> "<nama>.part2"  (digabung ke .part kalau keduanya sukses)
// Bila server tak dukung Range / RAM tak cukup utk TLS kedua / salah satu putus, otomatis jatuh ke jalur satu-koneksi lama.
// ---------------------------------------------------------------------
static void gutOpenSession(HTTPClient& http, WiFiClientSecure& sc, WiFiClient& pc, const char* url, bool* ok) {
  bool https = !strncmp(url, "https://", 8);
  if (https) { sc.setInsecure(); sc.setTimeout(GUT_IDLE_MS / 1000 + 1); }
  *ok = https ? http.begin(sc, url) : http.begin(pc, url);
  if (!*ok) return;
  http.setUserAgent("NyxOS-Buku/1.0 (ESP32-S3; pembaca e-book)");
  http.setTimeout(GUT_IDLE_MS); http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
}

// "Range: bytes=0-0" -> 206 + "Content-Range: bytes 0-0/TOTAL". true bila server dukung Range & ukuran diketahui.
static bool gutProbeSize(const char* url, uint32_t* total) {
  *total = 0;
  while ((uint32_t)(millis() - gutLastReqMs) < GUT_GAP_MS) { if (gutCancel) return false; delay(40); }
  if (gutCancel) return false;
  nasaWaitForHeap();
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http; bool ok = false;
  gutOpenSession(http, sc, pc, url, &ok);
  if (!ok) return false;
  const char* hk[1] = { "Content-Range" };
  http.collectHeaders(hk, 1);
  http.addHeader("Range", "bytes=0-0");
  int code = http.GET();
  gutLastReqMs = millis();
  bool good = false;
  if (code == 206) {
    String cr = http.header("Content-Range");
    int sl = cr.lastIndexOf('/');
    if (sl >= 0) { long v = cr.substring(sl + 1).toInt(); if (v > 0) { *total = (uint32_t)v; good = true; } }
  }
  Serial.printf("[GUT] probe %s -> %d total=%u\n", url, code, (unsigned)*total);
  http.end();
  return good;
}

// Ikuti SATU pengalihan (302) secara manual dan kembalikan URL tujuannya (mis. /ebooks/N.epub3.images -> /cache/epub/N/pgN-images-3.epub).
// Dipakai utk menyambung ulang (Range) langsung ke URL akhir: hemat satu handshake TLS (~2 dtk) tiap sambung ulang. false = tak ada pengalihan / gagal.
static bool gutResolveUrl(const char* url, char* out, int cap) {
  while ((uint32_t)(millis() - gutLastReqMs) < GUT_GAP_MS) { if (gutCancel) return false; delay(40); }
  if (gutCancel) return false;
  nasaWaitForHeap();
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http; bool ok = false;
  gutOpenSession(http, sc, pc, url, &ok);
  if (!ok) return false;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* hk[1] = { "Location" };
  http.collectHeaders(hk, 1);
  int code = http.GET();
  gutLastReqMs = millis();
  bool good = false;
  if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
    String loc = http.header("Location");
    if (loc.startsWith("https://")) { if ((int)loc.length() < cap) { snprintf(out, (size_t)cap, "%s", loc.c_str()); good = true; } }
    else if (loc.startsWith("/")) {                                   // relatif: pakai skema+host URL asal
      const char* h = strstr(url, "://");
      if (h) { h += 3; const char* sl = strchr(h, '/'); int hl = (int)((sl ? sl : url + strlen(url)) - url);
               if (hl + (int)loc.length() < cap) { snprintf(out, (size_t)cap, "%.*s%s", hl, url, loc.c_str()); good = true; } }
    }
  }
  Serial.printf("[GUT] resolve %s -> %d %s\n", url, code, good ? out : "(tanpa URL akhir)");
  http.end();
  return good;
}

// satu koneksi: ambil byte [from, to] (inklusif) dan tulis ke f dari awal berkas itu.
// return: 200 ok | >0 HTTP lain | -1 gagal konek/RAM | -2 batal | -3 putus/timeout/kurang | -5 gagal tulis SD | -7 server abaikan Range
static int gutFetchRange(const char* url, File* f, uint32_t from, uint32_t to, uint8_t lane, uint32_t* outGot) {
  *outGot = 0; gutLaneGot[lane] = 0;
  if (lane == 0) while ((uint32_t)(millis() - gutLastReqMs) < GUT_GAP_MS) { if (gutCancel) return -2; delay(40); }   // koneksi 1 sudah dijeda oleh koneksi 0
  if (gutCancel) return -2;
  nasaWaitForHeap();
  WiFiClientSecure sc; WiFiClient pc; HTTPClient http; bool ok = false;
  gutOpenSession(http, sc, pc, url, &ok);
  if (!ok) return -1;
  char rg[40]; snprintf(rg, sizeof(rg), "bytes=%u-%u", (unsigned)from, (unsigned)to);
  http.addHeader("Range", rg);
  uint32_t t0 = millis();
  int code = http.GET();
  gutLastReqMs = millis();
  Serial.printf("[GUT] L%u GET %s [%s] -> %d (%lu ms) RAM int: blok %u, bebas %u\n", (unsigned)lane, url, rg, code, (unsigned long)(millis() - t0),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (code == 200) { http.end(); return -7; }
  if (code != 206) { http.end(); return code > 0 ? code : (code == -11 ? -3 : -1); }
  uint32_t want = to - from + 1;
  int len = http.getSize();
  if (len > 0 && (uint32_t)len != want) { http.end(); return -7; }
  gutLaneState[lane] = 2;                                 // respons diterima: koneksi 1 boleh mulai
  GutSink sink; sink.file = f; sink.limit = want; sink.lane = lane; sink.useWriteBuffer();
  bool idle = false;
  WiFiClient* st = http.getStreamPtr();
  uint8_t* chunk = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!chunk || !st) { if (chunk) heap_caps_free(chunk); http.end(); return -1; }
  uint32_t last = millis();
  while (!gutCancel && !(lane == 1 && gutLaneAbort)) {
    if (sink.got >= want) break;
    int av = st->available();
    if (av > 0) {
      uint32_t room = want - sink.got;
      int n = st->readBytes(chunk, (uint32_t)av > 16384u ? 16384 : av);
      if (n <= 0) continue;
      if ((uint32_t)n > room) n = (int)room;
      if (sink.write(chunk, (size_t)n) != (size_t)n) break;
      last = millis();
    } else {
      if (!st->connected()) break;
      if (millis() - last > GUT_IDLE_MS) { idle = true; break; }
      delay(1);
    }
  }
  heap_caps_free(chunk);
  http.end();
  gutLastReqMs = millis();
  if (!gutCancel && !sink.sdFail) sink.flushSd();
  *outGot = sink.got;
  if (lane == 0) { gutHeadN = sink.headN; memcpy(gutHead, sink.head, 8); }
  Serial.printf("[GUT] L%u selesai got=%u/%u%s%s fl=%u/%ums\n", (unsigned)lane, (unsigned)sink.got, (unsigned)want,
                idle ? " idle" : "", sink.sdFail ? " sd!" : "", (unsigned)sink.flushMax, (unsigned)sink.flushMs);
  if (lane == 0) gutSdKBs = sink.flushMs ? (uint32_t)((uint64_t)sink.flushBytes * 1000 / sink.flushMs / 1024) : 0;
  if (gutCancel) return -2;
  if (sink.sdFail) return -5;
  if (sink.got < want) return -3;
  return 200;
}

static void gutLane1Func(void*) {                         // task pembantu: koneksi ke-2
  gutLaneState[1] = 1;
  uint32_t t0 = millis();
  while (!gutCancel && gutLaneState[0] < 2 && millis() - t0 < 25000) delay(20);   // tunggu koneksi 0 dapat respons (handshake TLS tidak barengan: hemat RAM internal)
  int rc = -2;
  if (!gutCancel && gutLaneState[0] != 4) {
    delay(250);
    for (int attempt = 0; attempt < 2 && !gutCancel && !gutLaneAbort; attempt++) {   // 1x ulang dari awal potongan (FILE_WRITE memotong berkas)
      File f = SD_MMC.open(gutL1Path, FILE_WRITE);
      if (!f) { rc = -5; break; }
      uint32_t got = 0;
      rc = gutFetchRange(gutL1Url, &f, gutL1From, gutL1To, 1, &got);
      f.close();
      if (rc == 200 || rc == -2 || rc == -5 || rc == -7) break;
      for (int w = 0; w < 2000 && !gutCancel && !gutLaneAbort; w += 100) delay(100);
    }
  }
  gutLaneRc[1] = rc;
  gutLaneState[1] = (rc == 200) ? 3 : 4;
  gutLane1Alive = false;                                  // paling akhir
  vTaskDelete(NULL);
}

// gabung src di ujung dst (src dihapus oleh pemanggil)
static bool gutAppendFile(const char* dst, const char* src) {
  File s = SD_MMC.open(src, FILE_READ);
  if (!s) return false;
  File d = SD_MMC.open(dst, FILE_APPEND);
  if (!d) { s.close(); return false; }
  const size_t CH = 32768;
  uint8_t* b = (uint8_t*)heap_caps_aligned_alloc(64, CH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool ok = (b != nullptr);
  while (ok && !gutCancel) {
    int n = s.read(b, CH);
    if (n <= 0) break;
    if (d.write(b, (size_t)n) != (size_t)n) ok = false;
  }
  if (b) heap_caps_free(b);
  s.close(); d.close();
  return ok && !gutCancel;
}

// return 200 = berkas utuh sudah ada di tmp | -2 batal | lainnya = gagal (tmp berisi awalan utuh, boleh dilanjut satu-koneksi)
static int gutDownloadParallel(const char* u, const char* tmp, uint32_t total) {
  uint32_t half = total / 2;
  char tmp2[140]; snprintf(tmp2, sizeof(tmp2), "%s2", tmp);
  SD_MMC.remove(tmp2);
  strncpy(gutL1Url, u, sizeof(gutL1Url) - 1); gutL1Url[sizeof(gutL1Url) - 1] = 0;
  strncpy(gutL1Path, tmp2, sizeof(gutL1Path) - 1); gutL1Path[sizeof(gutL1Path) - 1] = 0;
  gutL1From = half; gutL1To = total - 1;
  gutLaneGot[0] = gutLaneGot[1] = 0; gutLaneState[0] = 1; gutLaneState[1] = 0; gutLaneRc[0] = gutLaneRc[1] = 0;
  gutLaneAbort = false; gutGot = 0; gutTot = total; gutPartUrl[0] = 0;
  strncpy(gutPartUrl, u, sizeof(gutPartUrl) - 1); gutPartUrl[sizeof(gutPartUrl) - 1] = 0;   // .part = awalan dari URL ini (utk lanjut)
  gutInstBps = 0; gutSpdT = 0; gutSpdB = 0; gutLastDataMs = millis(); gutDiag[0] = 0;
  gutLane1Alive = true;
  if (createTaskPsramStack(gutLane1Func, "gutLane1", 12288, nullptr, &gutStack2, &gutTCB2, &gutTaskH2, 0) != pdPASS) { gutLane1Alive = false; return -1; }
  gutParActive = true;
  File f = SD_MMC.open(tmp, FILE_WRITE);
  int rc0 = -5; uint32_t got0 = 0;
  if (f) { rc0 = gutFetchRange(u, &f, 0, half - 1, 0, &got0); f.close(); }
  gutLaneRc[0] = rc0; gutLaneState[0] = (rc0 == 200) ? 3 : 4;
  if (rc0 != 200) gutLaneAbort = true;                    // tanpa koneksi 0, hasil koneksi 1 tak berguna
  while (gutLane1Alive) delay(30);
  gutParActive = false;
  gutEndMs = millis();
  int rc1 = gutLaneRc[1];
  Serial.printf("[GUT] paralel: L0 rc=%d got=%u/%u | L1 rc=%d got=%u/%u\n", rc0, (unsigned)got0, (unsigned)half, rc1, (unsigned)gutLaneGot[1], (unsigned)(total - half));
  snprintf(gutDiag, sizeof(gutDiag), "2 koneksi L0=%d L1=%d", rc0, rc1);
  if (gutCancel) { SD_MMC.remove(tmp2); return -2; }
  if (rc0 != 200 || rc1 != 200) { SD_MMC.remove(tmp2); return rc0 != 200 ? rc0 : rc1; }
  gutLaneGot[0] = half; gutLaneGot[1] = total - half; gutGot = total;
  bool merged = gutAppendFile(tmp, tmp2);
  SD_MMC.remove(tmp2);
  if (gutCancel) return -2;
  if (!merged) return -5;
  File g = SD_MMC.open(tmp, FILE_READ); uint32_t sz = g ? (uint32_t)g.size() : 0; if (g) g.close();
  if (sz != total) { SD_MMC.remove(tmp); gutPartUrl[0] = 0; return -3; }          // ukuran tak cocok: buang, ulang satu-koneksi
  return 200;
}

// tunggu RAM internal lega (blok terbesar >= GUT_TLS_MIN_BLOCK) sebelum membuka TLS lagi. true = cukup.
static bool gutWaitTlsHeap(uint32_t maxMs) {
  uint32_t t0 = millis();
  size_t lb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  while (lb < GUT_TLS_MIN_BLOCK && (uint32_t)(millis() - t0) < maxMs && !gutCancel) { delay(100); lb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }
  Serial.printf("[GUT] tunggu RAM: blok terbesar %u B (butuh %u) setelah %lu ms\n", (unsigned)lb, (unsigned)GUT_TLS_MIN_BLOCK, (unsigned long)(millis() - t0));
  return lb >= GUT_TLS_MIN_BLOCK;
}

// Diagnosis WiFi (Serial): dipanggil saat unduhan putus/macet. "reason" = alasan putus terakhir dari stack WiFi
// (2 AUTH_EXPIRE, 8 ASSOC_LEAVE, 15 4WAY_TIMEOUT, 200 BEACON_TIMEOUT, 201 NO_AP_FOUND, 202 AUTH_FAIL, 203 ASSOC_FAIL, 204 HANDSHAKE_TIMEOUT, 205 CONNECTION_FAIL).
static volatile uint8_t  gutDiscReason = 0;
static volatile uint32_t gutDiscMs = 0, gutDiscCount = 0;
static bool gutEvtReg = false;
static void gutWifiEvt(WiFiEvent_t ev, WiFiEventInfo_t info) {   // callback dari task WiFi: cukup simpan, jangan cetak
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { gutDiscReason = (uint8_t)info.wifi_sta_disconnected.reason; gutDiscMs = millis(); gutDiscCount++; }
}
static void gutWifiDiag(const char* tag) {
  Serial.printf("[GUT] WiFi %s: status=%d rssi=%d ch=%d mode=%d ip=%s gw=%s | putus terakhir: reason=%u %lu ms lalu (total %lu)\n", tag, (int)WiFi.status(), (int)WiFi.RSSI(), (int)WiFi.channel(),
                (int)WiFi.getMode(), WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                (unsigned)gutDiscReason, gutDiscMs ? (unsigned long)(millis() - gutDiscMs) : 0ul, (unsigned long)gutDiscCount);
}
static bool gutWifiWait(uint32_t ms) {                    // true bila status terhubung DAN sudah punya IP
  uint32_t t0 = millis();
  while (!gutCancel && (uint32_t)(millis() - t0) < ms && (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress((uint32_t)0))) delay(100);
  return WiFi.status() == WL_CONNECTED && !(WiFi.localIP() == IPAddress((uint32_t)0));
}

// Pulihkan STA (hanya lapisan radio/IP; status wifiConnected milik phone.ino TIDAK disentuh, web server & AP remote tetap).
// Langkah 1: reconnect biasa. Langkah 2 (kalau gagal): lepas lalu WiFi.begin dgn kredensial tersimpan -> scan ulang SEMUA kanal
// (reconnect biasa bisa buntu bila hotspot HP pindah kanal).
static bool gutWifiRecover() {
  gutWifiDiag("macet");
  gutPhase = GUT_PH_CONNECT; needRedraw = true;
  uint32_t t0 = millis();
  WiFi.reconnect();
  delay(600);                                              // beri event disconnect waktu diproses (status lama tak terbaca "terhubung")
  bool ok = gutWifiWait(5000);
  if (!ok && !gutCancel && WIFI_SSID[0]) {
    Serial.println("[GUT] WiFi reconnect gagal -> begin ulang (scan semua kanal)");
    WiFi.disconnect(false, false);
    delay(300);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    ok = gutWifiWait(12000);
  }
  WiFi.setSleep(false);                                    // jaga hemat daya WiFi tetap mati
  if (ok) for (uint32_t w = 0; w < 800 && !gutCancel; w += 100) delay(100);   // beri ARP/DHCP waktu mapan
  Serial.printf("[GUT] WiFi pulih=%d setelah %lu ms\n", (int)ok, (unsigned long)(millis() - t0));
  gutWifiDiag("sesudah");
  return ok;
}

static void gutSetErr(int rc) {
  gutPhase = GUT_PH_ERROR;
  switch (rc) {
    case -1: strcpy(gutMsg, "Gagal terhubung. Cek WiFi."); break;
    case -2: strcpy(gutMsg, "Dibatalkan."); break;
    case -3: strcpy(gutMsg, "Koneksi terputus / timeout."); break;
    case -4: strcpy(gutMsg, "Berkas terlalu besar."); break;
    case -5: strcpy(gutMsg, "Gagal menulis ke SD (penuh?)."); break;
    case -6: strcpy(gutMsg, "Gagal menulis data (kode -10)."); break;
    case 404: strcpy(gutMsg, "Berkas tidak tersedia (404)."); break;
    case 403: case 429: strcpy(gutMsg, "Ditolak server. Coba lagi nanti."); break;
    default: snprintf(gutMsg, sizeof(gutMsg), "Server membalas HTTP %d.", rc); break;
  }
}

static void gutDoSearch() {
  uint8_t* buf = (uint8_t*)heap_caps_malloc(GUT_JSON_CAP + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { strcpy(gutMsg, "RAM tidak cukup."); gutPhase = GUT_PH_ERROR; return; }
  uint32_t got = 0; int rc = -1;
  for (int attempt = 0; attempt < 3; attempt++) {       // galat sementara (timeout/putus/5xx) umum di Gutendex publik: ulangi
    if (attempt) { gutPhase = GUT_PH_CONNECT; needRedraw = true; }
    rc = gutFetch(gutReqUrl, buf, GUT_JSON_CAP, nullptr, 0, &got, attempt > 0, 30000, 0);
    if (rc == 200 || !(rc == -3 || rc == -1 || rc == 500 || rc == 502 || rc == 503 || rc == 504)) break;
  }
  if (rc != 200) { gutSetErr(rc); heap_caps_free(buf); return; }
  buf[got] = 0;
  int base = gutN, room = GUT_CAP - base, next = 0, total = 0;
  int k = gutParsePage((const char*)buf, (int)got, gutItems + base, room, &next, &total);
  heap_caps_free(buf);
  if (k < 0) { strcpy(gutMsg, "Balasan server tidak dikenali."); gutPhase = GUT_PH_ERROR; return; }
  gutTotal = total;
  gutN = base + k;                                      // terbitkan SETELAH semua item utuh
  gutHasNext = (next != 0) && (gutN < GUT_CAP);
  gutPhase = GUT_PH_IDLE;
}

static void gutDoDownload() {
  if (gutDlIdx < 0 || gutDlIdx >= gutN) { strcpy(gutMsg, "Buku tidak valid."); gutPhase = GUT_PH_ERROR; return; }
  GutItem it = gutItems[gutDlIdx];                      // salinan: daftar boleh berubah selama unduh
  bool isEpub = it.epub[0] != 0;
  gutHeadN = 0; gutCT[0] = 0;
  const char* orig = isEpub ? it.epub : it.txt;
  char url[GUT_URL_LEN + 8]; strncpy(url, orig, sizeof(url) - 1); url[sizeof(url) - 1] = 0;
  bool light = false;
  char lite[GUT_URL_LEN + 8];
  if (isEpub && gutLight) light = gutLightUrl(orig, lite, sizeof(lite));
  char name[96]; gutMakeFileName(it, isEpub ? "epub" : "txt", name, sizeof(name));
  char path[120], tmp[130];
  snprintf(path, sizeof(path), EBK_DIR "/%s", name);
  snprintf(tmp, sizeof(tmp), "%s.part", path);
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  if (SD_MMC.exists(path)) { strncpy(gutDoneName, name, sizeof(gutDoneName) - 1); gutPhase = GUT_PH_DONE; strcpy(gutMsg, "Sudah ada di perpustakaan."); return; }
  // .part sisa unduhan yang terputus: lanjutkan kalau URL-nya sama (tersimpan di RAM), kalau tidak buang.
  uint32_t resume = 0; bool useLite = light;
  if (SD_MMC.exists(tmp)) {
    File g = SD_MMC.open(tmp, FILE_READ); uint32_t sz = g ? (uint32_t)g.size() : 0; if (g) g.close();
    if (sz > 0 && useLite && !strcmp(gutPartUrl, lite)) resume = sz;
    else if (sz > 0 && !strcmp(gutPartUrl, url)) { useLite = false; resume = sz; }
    else SD_MMC.remove(tmp);
  }

  uint32_t got = 0; int rc = -1; int stall = 0, restarts = 0; bool keepPart = false;
  char fin[GUT_URL_LEN + 8]; fin[0] = 0; bool finOk = false, finTried = false;   // URL akhir setelah pengalihan (utk sambung ulang)
  gutRetries = 0;
  uint32_t lim = isEpub ? GUT_EPUB_MAX : GUT_TXT_MAX;
  bool parDone = false;
#if GUT_PARALLEL
  // Unduhan baru (bukan lanjutan .part): coba 2 koneksi paralel. Gagal di mana pun -> lanjut ke jalur satu-koneksi di bawah.
  size_t parBlk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), parFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (!resume && (parBlk < GUT_PAR_MIN_BLOCK || parFree < GUT_PAR_MIN_FREE))
    Serial.printf("[GUT] RAM internal kurang utk 2 koneksi (blok %u, bebas %u): satu koneksi\n", (unsigned)parBlk, (unsigned)parFree);
  else if (!resume) {
    uint32_t total = 0;
    const char* pu = useLite ? lite : url;
    if (gutProbeSize(pu, &total) && total >= GUT_PAR_MIN && total <= lim) {
      int pr = gutDownloadParallel(pu, tmp, total);
      if (pr == 200) { rc = 200; got = total; parDone = true; }
      else if (pr == -2) { rc = -2; parDone = true; }
      else {
        File g = SD_MMC.open(tmp, FILE_READ); uint32_t sz = g ? (uint32_t)g.size() : 0; if (g) g.close();
        if (sz > 0 && sz < total) resume = sz; else { SD_MMC.remove(tmp); gutPartUrl[0] = 0; }   // awalan utuh dari koneksi 0 dilanjutkan satu-koneksi
        gutPhase = GUT_PH_DOWNLOAD;
      }
    } else if (gutCancel) { rc = -2; parDone = true; }
  }
#endif
  // Unduhan sering putus di tengah (server/TLS): jangan mulai dari nol, sambung pakai Range dari byte terakhir yang sudah di SD.
  for (int attempt = 0; attempt < GUT_MAX_TRY && !parDone; attempt++) {
    const char* u = useLite ? lite : url;
    if (attempt) { gutRetries = (uint16_t)attempt; needRedraw = true; gutWaitTlsHeap(10000); }   // sambung ulang butuh RAM TLS: tunggu lega dulu
    if (attempt && resume && !finTried) { finTried = true; finOk = gutResolveUrl(u, fin, sizeof(fin)); }   // sekali saja: sambungan berikutnya langsung ke URL akhir
    if (!resume) { strncpy(gutPartUrl, u, sizeof(gutPartUrl) - 1); gutPartUrl[sizeof(gutPartUrl) - 1] = 0; }
    File f = SD_MMC.open(tmp, resume ? FILE_APPEND : FILE_WRITE);
    if (!f) { rc = -5; break; }
    rc = gutFetch((finOk && resume) ? fin : u, nullptr, 0, &f, lim, &got, true, GUT_IDLE_MS, resume);
    f.close();
    if (rc == 200 || rc == -2 || rc == -5 || rc == -4) break;     // sukses / batal / SD / terlalu besar: jangan ulangi
    File g = SD_MMC.open(tmp, FILE_READ); uint32_t sz = g ? (uint32_t)g.size() : 0; if (g) g.close();
    if (rc == -7 || (rc == 416 && resume)) {                      // server abaikan/tolak Range: ulang dari nol (maks 2x)
      SD_MMC.remove(tmp); resume = 0; gutPartUrl[0] = 0;
      if (++restarts > 2) { rc = -3; break; }
      continue;
    }
    if (rc > 0 && sz == 0) {                                      // kode HTTP (404 dll) tanpa data
      if (useLite) { useLite = false; resume = 0; finOk = false; finTried = false; continue; }     // versi ringan tak ada -> URL asli
      break;
    }
    if (rc == -1 || rc == -3) {                                   // data berhenti / gagal konek
      gutWifiDiag("putus");
      bool wifiOk = WiFi.status() == WL_CONNECTED && !(WiFi.localIP() == IPAddress((uint32_t)0));
      if (!wifiOk) gutWifiRecover();                              // WiFi benar2 lepas: pulihkan
      else for (uint32_t w = 0; w < 1500 && !gutCancel; w += 100) delay(100);   // WiFi sehat (server/jaringan yg menutup soket): JANGAN reconnect, cukup jeda lalu sambung (Range)
    }
    if (sz > resume) { resume = sz; stall = 0; }                  // ada kemajuan: sambung lagi
    else {
      if (++stall >= GUT_MAX_STALL) break;                        // berturut-turut tanpa kemajuan: menyerah
      // gagal sambung (server/WiFi/RAM): beri jeda makin panjang (3, 6, 9 ... dtk) supaya server tak dibanjiri & RAM sempat lega
      for (uint32_t w = 0; w < 3000u * (uint32_t)stall && !gutCancel; w += 100) delay(100);
    }
    if (resume == 0) gutPhase = GUT_PH_CONNECT;
  }
  if (rc != 200) {                                                // simpan .part supaya "Coba lagi" melanjutkan
    File g = SD_MMC.open(tmp, FILE_READ); uint32_t sz = g ? (uint32_t)g.size() : 0; if (g) g.close();
    keepPart = sz > 0 && rc != -2 && rc != -4 && rc != -5;        // ada data sebagian & bukan batal/terlalu besar/SD: boleh dilanjut
  }
  if (rc == 200 && got > 0 && isEpub) {                             // EPUB = ZIP: harus diawali "PK"
    File c = SD_MMC.open(tmp, FILE_READ);
    uint8_t sg[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; int nr = 0; uint32_t csz = 0;
    if (c) { csz = (uint32_t)c.size(); nr = c.read(sg, 8); c.close(); }
    if (nr < 2 || sg[0] != 'P' || sg[1] != 'K') {
      bool netPK = gutHeadN >= 2 && gutHead[0] == 'P' && gutHead[1] == 'K';
      char nh[24] = "?", sh[24] = "?";
      if (gutHeadN) { int o = 0; for (int i = 0; i < 6 && i < gutHeadN; i++) o += snprintf(nh + o, sizeof(nh) - o, i ? " %02X" : "%02X", gutHead[i]); }
      if (nr > 0)   { int o = 0; for (int i = 0; i < 6 && i < nr; i++)      o += snprintf(sh + o, sizeof(sh) - o, i ? " %02X" : "%02X", sg[i]); }
      Serial.printf("[GUT] BUKAN ZIP: jaringan=[%s] SD=[%s] ukuranSD=%u unduh=%u CT=%s\n", nh, sh, (unsigned)csz, (unsigned)got, gutCT[0] ? gutCT : "-");
      char bad[140]; snprintf(bad, sizeof(bad), "%s.bad", path);       // simpan utk diperiksa di PC (buka dgn editor teks / hex)
      if (SD_MMC.exists(bad)) SD_MMC.remove(bad);
      if (!SD_MMC.rename(tmp, bad)) SD_MMC.remove(tmp);
      snprintf(gutDiag, sizeof(gutDiag), "net:%s sd:%s", nh, sh);
      if (netPK) { gutNoWbuf = true; strcpy(gutMsg, "Rusak saat tulis SD. Coba lagi = mode aman."); }
      else snprintf(gutMsg, sizeof(gutMsg), "Server kirim: %.38s", gutCT[0] ? gutCT : "(tanpa Content-Type)");
      rc = -3; gutPartUrl[0] = 0; gutPhase = GUT_PH_ERROR; return;
    }
  }
  if (rc == 200 && got > 0) {
    if (SD_MMC.exists(path)) SD_MMC.remove(path);
    gutPartUrl[0] = 0;
    if (SD_MMC.rename(tmp, path)) {
      strncpy(gutDoneName, name, sizeof(gutDoneName) - 1); gutDoneName[sizeof(gutDoneName) - 1] = 0;
      gutPhase = GUT_PH_DONE; strcpy(gutMsg, "Tersimpan di /books.");
      uint32_t el = millis() - gutT0; uint32_t net = el ? (uint32_t)((uint64_t)got * 1000 / el / 1024) : 0;
      snprintf(gutDiag, sizeof(gutDiag), "rata2 %u KB/s%s, tulis SD %u KB/s", (unsigned)net, (parDone && rc == 200) ? " (2 koneksi)" : "", (unsigned)gutSdKBs);
      return;
    }
    strcpy(gutMsg, "Gagal menyimpan berkas."); gutPhase = GUT_PH_ERROR; SD_MMC.remove(tmp); return;
  }
  if (!keepPart) { SD_MMC.remove(tmp); gutPartUrl[0] = 0; }
  gutSetErr(rc == 200 ? -3 : rc);
  if (rc == -4 && !isEpub) strcpy(gutMsg, "TXT > 3 MB: terlalu besar utk Buku.");
  if (keepPart) strcpy(gutMsg, rc == -1 ? "Gagal sambung ulang. Coba lagi = lanjut." : "Terputus. Ketuk Coba lagi = lanjut.");
}

static void gutTaskFunc(void*) {
  if (gutJob == GUT_JOB_SEARCH) gutDoSearch();
  else if (gutJob == GUT_JOB_DL) gutDoDownload();
  gutJob = GUT_JOB_NONE;
  needRedraw = true;
  gutEndMs = millis();
  gutBusy = false;                                      // paling akhir: setelah ini UI boleh mulai pekerjaan baru
  vTaskDelete(NULL);
}

static bool gutStartJob(uint8_t job) {
  if (gutBusy) { showToast("Tunggu sebentar..."); return false; }
  if (WiFi.status() != WL_CONNECTED) { showToast("WiFi belum terhubung"); return false; }
  if (!nasaHeapReadyForTask()) { showToast("RAM internal padat, coba lagi"); return false; }
  while ((uint32_t)(millis() - gutEndMs) < 80) delay(10);     // beri idle task waktu membereskan task sebelumnya (TCB statis dipakai ulang)
  gutBusy = true; gutJob = job; gutCancel = false; gutPhase = GUT_PH_CONNECT; gutMsg[0] = 0;
  if (createTaskPsramStack(gutTaskFunc, "gutTask", 16384, nullptr, &gutStack, &gutTCB, &gutTaskH, 1) != pdPASS) {
    gutBusy = false; gutJob = GUT_JOB_NONE; gutPhase = GUT_PH_IDLE;
    showToast("Gagal memulai proses");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------
// aksi
// ---------------------------------------------------------------------
static void gutStartSearch(bool append) {
  if (gutBusy) { showToast("Tunggu sebentar..."); return; }
  if (!gutItems) return;
  if (!append) { gutN = 0; gutPage = 1; gutHasNext = false; gutScroll = 0; gutTotal = 0; }
  else gutPage++;
  gutBuildQuery(gutReqUrl, sizeof(gutReqUrl), gutQApplied, gutLangs[gutLangIdx], gutSortIdx, gutPage);
  gutReqAppend = append;
  if (!gutStartJob(GUT_JOB_SEARCH)) { if (append) gutPage--; }
  needRedraw = true;
}

static void gutSubmit() {
  strncpy(gutQApplied, gutQuery.c_str(), sizeof(gutQApplied) - 1); gutQApplied[sizeof(gutQApplied) - 1] = 0;
  gutStartSearch(false);
}

static void gutChipTap(int c) {
  if (gutBusy) { showToast("Tunggu sebentar..."); return; }
  if (c == 0) gutLangIdx = (uint8_t)((gutLangIdx + 1) % 9);
  else if (c == 1) { gutLight = !gutLight; showToast(gutLight ? "EPUB tanpa gambar (ringan)" : "EPUB dengan gambar (besar)"); needRedraw = true; return; }
  else gutSortIdx = (uint8_t)((gutSortIdx + 1) % 2);
  gutStartSearch(false);
}

static void gutOpenDetail(int idx) {
  if (idx < 0 || idx >= gutN) return;
  gutSel = idx; gutView = GUT_V_DETAIL;
  const GutItem& it = gutItems[idx];
  char nm[96]; gutMakeFileName(it, it.epub[0] ? "epub" : "txt", nm, sizeof(nm));
  char p[120]; snprintf(p, sizeof(p), EBK_DIR "/%s", nm);
  strncpy(gutSelName, nm, sizeof(gutSelName) - 1); gutSelName[sizeof(gutSelName) - 1] = 0;
  gutSelExists = sdReady && SD_MMC.exists(p);
  needRedraw = true;
}

static void gutOpenBook(const char* name) {
  gutView = GUT_V_LIST;
  ebkMode = EBK_LIB; ebkScroll = 0; ebkScan();
  for (int i = 0; i < ebkLibN; i++) if (!strcmp(ebkLib[i].name, name)) { ebkStartOpen(i); return; }
  showToast("Buku tidak ditemukan di /books");
  needRedraw = true;
}

static void gutLeave() {                                // kembali ke perpustakaan
  if (kbVisible && kbTarget == &gutQuery) { kbVisible = false; kbTarget = nullptr; }
  gutView = GUT_V_LIST;
  ebkMode = EBK_LIB; ebkScroll = 0; ebkScan();
  needRedraw = true;
}

static void gutBack() {
  if (gutView == GUT_V_PROG) { if (gutBusy && gutJob == GUT_JOB_DL) gutCancel = true; gutView = GUT_V_DETAIL; if (gutPhase == GUT_PH_DONE) gutOpenDetail(gutSel); }
  else if (gutView == GUT_V_DETAIL) gutView = GUT_V_LIST;
  else { gutLeave(); return; }
  needRedraw = true;
}

// ---------------------------------------------------------------------
// siklus
// ---------------------------------------------------------------------
void gutEnter() {
  if (!gutEvtReg) { WiFi.onEvent(gutWifiEvt); gutEvtReg = true; }     // catat alasan putus WiFi utk diagnosis
  if (!gutItems) gutItems = (GutItem*)ebPsAlloc(sizeof(GutItem) * GUT_CAP);
  if (!gutItems) { showToast("RAM tidak cukup"); return; }
  gutView = GUT_V_LIST; gutScroll = 0; gutDown = false; gutMsg[0] = 0;
  ebkMode = EBK_GUT; needRedraw = true;
  if (gutN == 0 && !gutBusy) gutAutoLoad = true;
}

void gutExit() {                                        // app Buku ditutup
  if (gutBusy) { gutCancel = true; uint32_t t0 = millis(); while (gutBusy && millis() - t0 < 3000) delay(10); }
  if (kbVisible && kbTarget == &gutQuery) { kbVisible = false; kbTarget = nullptr; }
  if (!gutBusy && gutItems) { free(gutItems); gutItems = nullptr; }   // masih sibuk -> biarkan (dipakai task), dipakai ulang saat masuk lagi
  if (!gutItems) { gutN = 0; gutHasNext = false; }
  gutQuery = ""; gutQApplied[0] = 0; gutView = GUT_V_LIST; gutAutoLoad = false; gutDown = false;
}

void gutLoopPoll() {
  if (gutAutoLoad && !gutBusy) {
    gutAutoLoad = false;
    if (WiFi.status() == WL_CONNECTED) gutStartSearch(false);
  }
  if (gutBusy && millis() - gutAnimMs > (gutPhase == GUT_PH_DOWNLOAD ? GUT_DL_REDRAW_MS : 250u)) { gutAnimMs = millis(); needRedraw = true; }
}

// ---------------------------------------------------------------------
// geometri
// ---------------------------------------------------------------------
#define GUT_GO_W 46
static int  gutListTop() { return 80; }
static int  gutBottom()  { return SCR_H - BACK_H - 3 - 6; }
static int  gutListBot() { return (kbVisible && kbTarget == &gutQuery) ? kbY() - 4 : gutBottom(); }
static bool gutHitBox(int x, int y) { return y >= 26 && y <= 52 && x >= 70 && x <= SCR_W - 10 - GUT_GO_W - 4; }
static bool gutHitGo(int x, int y)  { return y >= 26 && y <= 52 && x > SCR_W - 10 - GUT_GO_W - 4 && x <= SCR_W - 6; }
static int  gutChipW()  { return (SCR_W - 20 - 12) / 3; }
static int  gutChipX(int i) { return 10 + i * (gutChipW() + 6); }
static int  gutHitChip(int x, int y) {
  if (y < 54 || y > 76) return -1;
  for (int i = 0; i < 3; i++) if (x >= gutChipX(i) - 2 && x <= gutChipX(i) + gutChipW() + 2) return i;
  return -1;
}
static int  gutBtnY() { return gutBottom() - 28 - 4; }
static int  gutBtnCount() { return (gutPhase >= GUT_PH_DONE) ? 2 : 1; }
static bool gutHitBtn(int x, int y, int which) {         // which: 0 = kiri / tunggal, 1 = kanan
  if (y < gutBtnY() - 2 || y > gutBtnY() + 30) return false;
  if (gutBtnCount() == 1) return which == 0 && x >= 12 && x <= SCR_W - 12;
  int half = SCR_W / 2;
  return which == 0 ? (x >= 12 && x < half - 2) : (x >= half + 2 && x <= SCR_W - 12);
}

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
static void gutDrawBtn(LGFX_Sprite& s, int x, int y, int w, const char* label, bool primary) {
  uint16_t bg = primary ? T().accent : blend565(T().bg, T().text, 28);
  nyxAaRR(s, x, y, w, 28, 14, bg);
  s.setTextSize(1); s.setTextColor(primary ? (uint16_t)0xFFFF : T().text);
  s.setCursor(x + (w - s.textWidth(label)) / 2, y + 10); s.print(label);
}

static void gutDrawBtns(LGFX_Sprite& s, const char* a, const char* b) {
  int y = gutBtnY();
  if (!b) { gutDrawBtn(s, 12, y, SCR_W - 24, a, true); return; }
  int half = SCR_W / 2;
  gutDrawBtn(s, 12, y, half - 14, a, true);
  gutDrawBtn(s, half + 2, y, half - 14, b, false);
}

// kata demi kata, maks 'maxLines' baris; kembalikan jumlah baris yang dipakai
static int gutWrap(LGFX_Sprite& s, const char* text, int x, int y, int maxPx, int lineH, int maxLines, uint16_t col) {
  char line[112]; line[0] = 0; int ll = 0, lines = 0;
  const char* p = text;
  s.setTextColor(col);
  while (*p && lines < maxLines) {
    const char* e = p; while (*e && *e != ' ') e++;
    int wl = (int)(e - p);
    char word[48]; if (wl > 47) wl = 47; memcpy(word, p, (size_t)wl); word[wl] = 0;
    char trial[112]; snprintf(trial, sizeof(trial), ll ? "%s %s" : "%s%s", line, word);
    if (ll && s.textWidth(trial) > maxPx) {
      s.setCursor(x, y + lines * lineH); s.print(line); lines++;
      strcpy(line, word); ll = (int)strlen(line);
    } else { strcpy(line, trial); ll = (int)strlen(line); }
    p = e; while (*p == ' ') p++;
  }
  if (ll && lines < maxLines) {
    if (*p) { ebkFitText(s, line, maxPx - s.textWidth("...")); strcat(line, "..."); }
    s.setCursor(x, y + lines * lineH); s.print(line); lines++;
  }
  return lines;
}

static void gutDrawList(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Gutenberg");
  uint16_t chip = blend565(T().bg, T().text, 28);
  // kotak cari + tombol Cari
  int bx = 70, bw = SCR_W - 10 - GUT_GO_W - 4 - bx;
  nyxAaRR(s, bx, 29, bw, 20, 10, chip);
  char q[44]; strncpy(q, gutQuery.c_str(), sizeof(q) - 1); q[sizeof(q) - 1] = 0;
  ebkFitText(s, q, bw - 20);
  s.setTextColor(q[0] ? T().text : T().subtext);
  s.setCursor(bx + 10, 35); s.print(q[0] ? q : "cari judul / penulis");
  nyxAaRR(s, SCR_W - 10 - GUT_GO_W, 29, GUT_GO_W, 20, 10, T().accent);
  s.setTextColor(0xFFFF); s.setCursor(SCR_W - 10 - GUT_GO_W + (GUT_GO_W - s.textWidth("Cari")) / 2, 35); s.print("Cari");
  // chip: bahasa / format / urutan
  char lb[3][20];
  if (gutLangIdx) snprintf(lb[0], sizeof(lb[0]), "Bhs: %s", gutLangUp[gutLangIdx]); else strcpy(lb[0], "Semua bhs");
  strcpy(lb[1], gutLight ? "Tanpa gbr" : "Dgn gbr");
  strcpy(lb[2], gutSortIdx ? "Terbaru" : "Populer");
  for (int i = 0; i < 3; i++) {
    nyxAaRR(s, gutChipX(i), 54, gutChipW(), 20, 10, chip);
    ebkFitText(s, lb[i], gutChipW() - 8);
    s.setTextColor(T().text); s.setCursor(gutChipX(i) + (gutChipW() - s.textWidth(lb[i])) / 2, 60); s.print(lb[i]);
  }
  s.setTextSize(1);
  int top = gutListTop(), bot = gutListBot();
  int n = gutN;                                          // baca sekali (task bisa menambah di tengah gambar)
  bool busy = gutBusy && gutJob == GUT_JOB_SEARCH;
  if (n == 0) {
    int dots = (int)((millis() / 300) % 4);
    if (busy) { char m[24]; snprintf(m, sizeof(m), "Memuat%.*s", dots, "..."); s.setTextColor(T().text); s.setCursor(12, top + 10); s.print(m); }
    else if (WiFi.status() != WL_CONNECTED) {
      s.setTextColor(T().text);    s.setCursor(12, top + 10); s.print("WiFi belum terhubung.");
      s.setTextColor(T().subtext); s.setCursor(12, top + 26); s.print("Sambungkan lewat Setting, lalu");
      s.setCursor(12, top + 38); s.print("ketuk Cari.");
    } else if (gutPhase == GUT_PH_ERROR) {
      s.setTextColor(T().danger);  s.setCursor(12, top + 10); s.print(gutMsg);
      s.setTextColor(T().subtext); s.setCursor(12, top + 26); s.print("Ketuk Cari untuk mencoba lagi.");
      if (gutDiag[0]) { s.setCursor(12, top + 42); s.print(gutDiag); }
    } else {
      s.setTextColor(T().subtext); s.setCursor(12, top + 10); s.print(gutQApplied[0] ? "Tidak ada hasil." : "Ketuk Cari untuk memuat daftar.");
    }
    return;
  }
  int rows = n + ((gutHasNext || busy) ? 1 : 0);
  int contentH = rows * GUT_ROW_H, view = bot - top;
  { int mx = contentH - view; if (mx < 0) mx = 0; if (gutScroll < 0) gutScroll = 0; if (gutScroll > (float)mx) gutScroll = (float)mx; }
  s.setClipRect(0, top, SCR_W, bot - top);
  int y0 = top - (int)gutScroll;
  for (int v = 0; v < rows; v++) {
    int ry = y0 + v * GUT_ROW_H;
    if (ry + GUT_ROW_H < top || ry > bot) continue;
    if (v == n) {                                         // baris "muat lebih banyak"
      const char* t = busy ? "Memuat..." : "Muat lebih banyak";
      s.setTextColor(busy ? T().subtext : T().accent);
      s.setCursor((SCR_W - s.textWidth(t)) / 2, ry + 16); s.print(t);
      continue;
    }
    const GutItem& it = gutItems[v];
    char tt[GUT_TITLE_LEN], au[GUT_AUTH_LEN], info[48], cnt[12];
    ebkFold(it.title, tt, sizeof(tt)); ebkFold(it.author, au, sizeof(au));
    ebkFitText(s, tt, SCR_W - 24);
    s.setTextColor(T().text); s.setCursor(12, ry + 7); s.print(tt);
    if (au[0]) { ebkFitText(s, au, SCR_W - 24); s.setTextColor(T().subtext); s.setCursor(12, ry + 21); s.print(au); }
    gutFmtCount(cnt, sizeof(cnt), it.dl);
    snprintf(info, sizeof(info), "%s  %s  %s unduhan", it.epub[0] ? "EPUB" : "TXT", it.lang[0] ? it.lang : "--", cnt);
    s.setTextColor(T().accent); s.setCursor(12, ry + 34); s.print(info);
    s.drawFastHLine(12, ry + GUT_ROW_H - 2, SCR_W - 24, T().divider);
  }
  s.clearClipRect();
  if (contentH > view) {
    int th = view * view / contentH; if (th < 12) th = 12;
    int mx = contentH - view;
    int ty = top + (int)((view - th) * (gutScroll / (float)mx));
    nyxAaRR(s, SCR_W - 4, ty, 2, th, 1, blend565(T().bg, T().text, 90));
  }
}

static void gutDrawDetail(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Detail buku");
  if (gutSel < 0 || gutSel >= gutN) return;
  const GutItem& it = gutItems[gutSel];
  char tt[GUT_TITLE_LEN], au[GUT_AUTH_LEN], cnt[12];
  ebkFold(it.title, tt, sizeof(tt)); ebkFold(it.author, au, sizeof(au));
  int y = 50;
  int ln = gutWrap(s, tt, 12, y, SCR_W - 24, 12, 3, T().text);
  y += ln * 12 + 6;
  if (au[0]) { s.setTextColor(T().subtext); s.setCursor(12, y); s.print(au); y += 18; }
  gutFmtCount(cnt, sizeof(cnt), it.dl);
  char l1[48], l2[48], l3[64];
  snprintf(l1, sizeof(l1), "Bahasa : %s", it.lang[0] ? it.lang : "--");
  snprintf(l2, sizeof(l2), "Unduhan: %s", cnt);
  if (it.epub[0]) snprintf(l3, sizeof(l3), "Format : EPUB (%s)", gutLight ? "tanpa gambar" : "dengan gambar");
  else            snprintf(l3, sizeof(l3), "Format : TXT (maks 3 MB)");
  s.setTextColor(T().text);
  s.setCursor(12, y); s.print(l1); s.setCursor(12, y + 14); s.print(l2); s.setCursor(12, y + 28); s.print(l3);
  s.setTextColor(T().subtext);
  s.setCursor(12, y + 48); s.print("Sumber: gutenberg.org (domain publik)");
  if (!sdReady) { s.setTextColor(T().danger); s.setCursor(12, y + 62); s.print("SD card tidak terdeteksi."); }
  else if (gutSelExists) { s.setTextColor(T().good); s.setCursor(12, y + 62); s.print("Sudah ada di perpustakaan."); }
  gutDrawBtns(s, gutSelExists ? "Buka di Buku" : "Unduh", nullptr);
}

static void gutDrawProgress(LGFX_Sprite& s) {
  const char* head = gutPhase == GUT_PH_DONE ? "Selesai" : (gutPhase == GUT_PH_ERROR ? "Gagal" : "Mengunduh");
  iosTitle(s, 8, 26, head);
  if (gutDlIdx >= 0 && gutDlIdx < gutN) {
    char tt[GUT_TITLE_LEN]; ebkFold(gutItems[gutDlIdx].title, tt, sizeof(tt));
    gutWrap(s, tt, 12, 52, SCR_W - 24, 12, 3, T().text);
  }
  int by = 112, bw = SCR_W - 24;
  uint32_t got = gutGot, tot = gutTot;
  nyxAaRR(s, 12, by, bw, 8, 4, blend565(T().bg, T().text, 40));
  if (gutPhase == GUT_PH_DONE) nyxAaRR(s, 12, by, bw, 8, 4, T().good);
  else if (gutPhase == GUT_PH_ERROR) nyxAaRR(s, 12, by, bw, 8, 4, T().danger);
  else if (tot > 0) { int w = (int)((uint64_t)bw * (got > tot ? tot : got) / tot); if (w >= 8) nyxAaRR(s, 12, by, w, 8, 4, T().accent); }
  else { int seg = bw / 4, pos = (int)((millis() / 6) % (uint32_t)(bw + seg)) - seg; int x0 = 12 + (pos < 0 ? 0 : pos), x1 = 12 + (pos + seg > bw ? bw : pos + seg); if (x1 - x0 > 4) nyxAaRR(s, x0, by, x1 - x0, 8, 4, T().accent); }
  char sz[24], sz2[24], line[64];
  s.setTextColor(T().subtext);
  if (gutPhase == GUT_PH_CONNECT) { snprintf(line, sizeof(line), "Menghubungi server%.*s", (int)((millis() / 300) % 4), "..."); }
  else if (gutPhase == GUT_PH_DOWNLOAD) {
    ebkFmtSize(sz, sizeof(sz), got);
    if (tot > 0) { ebkFmtSize(sz2, sizeof(sz2), tot); snprintf(line, sizeof(line), "%s / %s  (%u%%)", sz, sz2, (unsigned)((uint64_t)got * 100 / tot)); }
    else snprintf(line, sizeof(line), "%s", sz);
  } else snprintf(line, sizeof(line), "%s", gutMsg);
  if (gutPhase == GUT_PH_ERROR) s.setTextColor(T().danger); else if (gutPhase == GUT_PH_DONE) s.setTextColor(T().good);
  s.setCursor(12, by + 18); s.print(line);
  if (gutPhase == GUT_PH_DOWNLOAD) {
    uint32_t el = millis() - gutT0;
    if (el > 700 && got > 0) {
      uint32_t nowD = millis();
      uint32_t bps = (nowD - gutLastDataMs > 3000) ? 0 : gutInstBps;      // kecepatan saat ini; 0 bila macet >3 dtk
      if (!bps && gutInstBps == 0 && nowD - gutLastDataMs <= 3000) bps = (uint32_t)((uint64_t)got * 1000 / el);   // jendela pertama belum terisi
      char sp[64]; char rt[16] = "";
      if (gutParActive) snprintf(rt, sizeof(rt), "  x2");
      else if (gutRetries) snprintf(rt, sizeof(rt), "  ulang %ux", (unsigned)gutRetries);
      if (!bps) snprintf(sp, sizeof(sp), "menunggu data...%s", rt);
      else if (tot > got) snprintf(sp, sizeof(sp), "%u KB/s  sisa ~%u dtk%s", (unsigned)(bps / 1024), (unsigned)((tot - got) / bps + 1), rt);
      else snprintf(sp, sizeof(sp), "%u KB/s%s", (unsigned)(bps / 1024), rt);
      s.setTextColor(T().subtext); s.setCursor(12, by + 32); s.print(sp);
    }
  }
  if ((gutPhase == GUT_PH_ERROR || gutPhase == GUT_PH_DONE) && gutDiag[0]) { s.setTextColor(T().subtext); s.setCursor(12, by + 32); s.print(gutDiag); }
  if (gutPhase == GUT_PH_DONE)       gutDrawBtns(s, "Buka", "Kembali");
  else if (gutPhase == GUT_PH_ERROR) gutDrawBtns(s, "Coba lagi", "Kembali");
  else                               gutDrawBtns(s, "Batal", nullptr);
}

void gutDraw(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  s.unloadFont(); s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  if (gutView == GUT_V_DETAIL) gutDrawDetail(s);
  else if (gutView == GUT_V_PROG) gutDrawProgress(s);
  else gutDrawList(s);
  if (kbVisible && kbTarget == &gutQuery && gutView == GUT_V_LIST) drawKb(s); else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh
// ---------------------------------------------------------------------
// jari baru menyentuh: keyboard, Kembali, kepala (kotak cari / Cari / chip)
void gutTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  bool kbOn = kbVisible && kbTarget == &gutQuery;
  if (kbOn) {
    if (y >= kbY() - 2) { kbTouch(x, y); needRedraw = true; return; }
    if (gutHitGo(x, y)) { kbVisible = false; kbTarget = nullptr; gutSubmit(); return; }
    if (gutHitBox(x, y)) { kbVisible = false; kbTarget = nullptr; needRedraw = true; }     // tutup keyboard
    return;
  }
  if (isBack(x, y)) { gutBack(); return; }
  if (gutView != GUT_V_LIST) return;
  if (gutHitGo(x, y))  { vibTap(); gutSubmit(); return; }
  if (gutHitBox(x, y)) { kbTarget = &gutQuery; kbVisible = true; kbMode = KB_LOWER; needRedraw = true; return; }
  int c = gutHitChip(x, y);
  if (c >= 0) { vibTap(); gutChipTap(c); }
}

static void gutRelease() {                              // ketukan (jari lepas tanpa menggeser)
  if (gutView == GUT_V_LIST) {
    int top = gutListTop();
    if (gutSy < top || gutSy >= gutListBot()) return;
    int v = (int)((gutSy - top + (int)gutScroll) / GUT_ROW_H);
    int n = gutN;
    if (v >= 0 && v < n) { vibTap(); gutOpenDetail(v); }
    else if (v == n && gutHasNext && !gutBusy) { vibTap(); gutStartSearch(true); }
    else if (n == 0 && !gutBusy) { /* daftar kosong: tombol Cari yang memuat */ }
  } else if (gutView == GUT_V_DETAIL) {
    if (gutHitBtn(gutSx, gutSy, 0)) {
      vibTap();
      if (gutSelExists) gutOpenBook(gutSelName);
      else if (!sdReady) showToast("SD card tidak terdeteksi");
      else if (gutBusy) showToast("Tunggu sebentar...");
      else { gutDlIdx = gutSel; gutView = GUT_V_PROG; gutGot = 0; gutTot = 0; if (!gutStartJob(GUT_JOB_DL)) gutView = GUT_V_DETAIL; needRedraw = true; }
    }
  } else {                                              // progres
    if (gutPhase == GUT_PH_DONE) {
      if (gutHitBtn(gutSx, gutSy, 0)) { vibTap(); gutOpenBook(gutDoneName); }
      else if (gutHitBtn(gutSx, gutSy, 1)) { vibTap(); gutOpenDetail(gutSel); }
    } else if (gutPhase == GUT_PH_ERROR) {
      if (gutHitBtn(gutSx, gutSy, 0)) {
        vibTap();
        if (gutBusy) showToast("Tunggu sebentar...");
        else { gutGot = 0; gutTot = 0; if (!gutStartJob(GUT_JOB_DL)) gutView = GUT_V_DETAIL; needRedraw = true; }
      } else if (gutHitBtn(gutSx, gutSy, 1)) { vibTap(); gutView = GUT_V_DETAIL; needRedraw = true; }
    } else if (gutHitBtn(gutSx, gutSy, 0)) {            // Batal
      vibTap(); gutCancel = true; gutView = GUT_V_DETAIL; needRedraw = true;
    }
  }
}

// tiap putaran loop() selagi app terbuka: geser daftar; ketukan diproses saat jari LEPAS
void gutPointerTick(bool touched, bool wasT, int tx, int ty) {
  if (touched) {
    if (!wasT) {
      gutSx = tx; gutSy = ty; gutPy = ty; gutMoved = false;
      gutDown = !(kbVisible && kbTarget == &gutQuery && ty >= kbY() - 2);
    } else if (gutDown) {
      if (!gutMoved && abs(ty - gutSy) > 8) gutMoved = true;
      if (gutMoved && gutView == GUT_V_LIST) { gutScroll += (float)(gutPy - ty); gutPy = ty; needRedraw = true; }
    }
  } else if (wasT && gutDown) {
    gutDown = false;
    if (!gutMoved) gutRelease();
  } else gutDown = false;
}
