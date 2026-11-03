// Simulasi PC utk alur unduhan Gutenberg (lihat run_sim.py). SD = folder /tmp/gutsim_sd.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------- stub Arduino / ESP32 ----------------
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static void* heap_caps_malloc(size_t n, int) { return malloc(n); }
static void* heap_caps_aligned_alloc(size_t a, size_t n, int) { return aligned_alloc(a, (n + a - 1) / a * a); }
static void heap_caps_free(void* p) { free(p); }
static size_t heap_caps_get_largest_free_block(int) { return 100000; }
static size_t heap_caps_get_free_size(int) { return 100000; }
static uint32_t millis() { using namespace std::chrono; return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
static int g_delays = 0;
static void delay(uint32_t) { g_delays++; }
static void yield() {}
static bool g_verbose = false;
struct SerialStub { template <typename... A> void printf(const char* f, A... a) { if (g_verbose) ::printf(f, a...); } void println(const char* s) { if (g_verbose) ::puts(s); } } Serial;
static const char* FILE_READ = "r"; static const char* FILE_WRITE = "w"; static const char* FILE_APPEND = "a";
#define EBK_DIR "/books"
static const std::string SDROOT = "/tmp/gutsim_sd";
struct File {
  FILE* fp = nullptr; DIR* dp = nullptr; std::string path, nm;
  explicit operator bool() const { return fp || dp; }
  uint32_t size() { if (!fp) return 0; fflush(fp); struct stat st; fstat(fileno(fp), &st); return (uint32_t)st.st_size; }
  int read(uint8_t* b, size_t n) { return fp ? (int)fread(b, 1, n, fp) : -1; }
  size_t write(const uint8_t* b, size_t n) { return fp ? fwrite(b, 1, n, fp) : 0; }
  bool seek(uint32_t p) { return fp && fseek(fp, (long)p, SEEK_SET) == 0; }
  void close() { if (fp) fclose(fp); if (dp) closedir(dp); fp = nullptr; dp = nullptr; }
  bool isDirectory() { return dp != nullptr; }
  const char* name() { return nm.c_str(); }
  File openNextFile() {
    File f; if (!dp) return f;
    while (dirent* e = readdir(dp)) {
      if (e->d_name[0] == '.') continue;
      f.path = path + "/" + e->d_name; f.nm = e->d_name;
      struct stat st; if (stat(f.path.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) f.dp = opendir(f.path.c_str()); else f.fp = fopen(f.path.c_str(), "rb");
      return f;
    }
    return f;
  }
};
struct SDStub {
  std::string P(const char* p) { return SDROOT + p; }
  bool exists(const char* p) { struct stat st; return stat(P(p).c_str(), &st) == 0; }
  bool remove(const char* p) { return ::remove(P(p).c_str()) == 0; }
  bool rename(const char* a, const char* b) { return ::rename(P(a).c_str(), P(b).c_str()) == 0; }
  bool mkdir(const char* p) { return ::mkdir(P(p).c_str(), 0755) == 0; }
  File open(const char* p, const char* mode = "r") {
    File f; f.path = P(p); f.nm = p;
    struct stat st; bool ex = stat(f.path.c_str(), &st) == 0;
    if (ex && S_ISDIR(st.st_mode)) { f.dp = opendir(f.path.c_str()); return f; }
    if (!strcmp(mode, "r") && !ex) return f;
    f.fp = fopen(f.path.c_str(), !strcmp(mode, "r") ? "rb" : !strcmp(mode, "w") ? "wb" : "ab");
    return f;
  }
} SD_MMC;
struct IPAddress { uint32_t v; IPAddress(uint32_t x = 0) : v(x) {} bool operator==(const IPAddress& o) const { return v == o.v; } };
#define WL_CONNECTED 3
struct WiFiStub { int status() { return 3; } IPAddress localIP() { return IPAddress(1); } } WiFi;
struct String { const char* c_str() const { return ""; } };
typedef void* StackType_t; struct StaticTask_t { int x; }; typedef void* TaskHandle_t;
struct portMUX_TYPE { int x; };
#define portMUX_INITIALIZER_UNLOCKED { 0 }
static bool sdReady = true; static bool needRedraw = false;
static bool ebkEndsCI(const char* s, const char* suf) { size_t a = strlen(s), b = strlen(suf); if (b > a) return false; for (size_t i = 0; i < b; i++) if (tolower(s[a - b + i]) != tolower(suf[i])) return false; return true; }
static void ebkBaseName(const char* in, char* out, int cap) { const char* sl = strrchr(in, '/'); snprintf(out, (size_t)cap, "%s", sl ? sl + 1 : in); }

#include "../gutendex_core.h"
#include "../gutdl_core.h"
#include "sim_globals.inc"

// ---------------- "server" palsu + fungsi jaringan yang di-stub ----------------
struct FetchLog { std::string url; uint32_t resume; };
static std::vector<FetchLog> g_fetches;
static std::vector<uint8_t> g_payload;
static int g_corruptRuns = 0;          // berapa unduhan (dari byte 0) yang "ditulis rusak" ke SD
static long g_cutAt = -1;              // putus di byte ini (sekali)
static std::string g_404Contains;      // URL berisi ini -> 404
static bool g_gutendexFail = false;
static std::string g_opdsBody, g_gutBody;
static int gutFetch(const char* url, uint8_t* mem, uint32_t memCap, File* f, uint32_t limit, uint32_t* outGot, bool manual, uint32_t idleMs, uint32_t resumeFrom) {
  (void)manual; (void)idleMs; (void)limit;
  g_fetches.push_back({ url, resumeFrom });
  *outGot = resumeFrom;
  if (!f) {                                                      // pencarian (ke memori)
    bool opds = strstr(url, "search.opds") != nullptr;
    if (!opds && g_gutendexFail) return -3;
    const std::string& b = opds ? g_opdsBody : g_gutBody;
    size_t n = b.size() < memCap ? b.size() : memCap; memcpy(mem, b.data(), n); *outGot = (uint32_t)n; return 200;
  }
  if (!g_404Contains.empty() && strstr(url, g_404Contains.c_str())) return 404;
  size_t start = resumeFrom, end = g_payload.size(); bool cut = false;
  if (g_cutAt >= 0 && (size_t)g_cutAt > start && (size_t)g_cutAt < end) { end = (size_t)g_cutAt; cut = true; g_cutAt = -1; }
  std::vector<uint8_t> chunk(g_payload.begin() + (long)start, g_payload.begin() + (long)end);
  uint32_t crc = resumeFrom ? (uint32_t)gutNetCrc : 0;
  crc = gutCrc32(crc, chunk.data(), chunk.size());               // CRC dihitung dari data ASLI yang masuk dari jaringan
  if (!resumeFrom) { gutHeadN = 8; memcpy(gutHead, chunk.data(), 8); }
  if (!resumeFrom && g_corruptRuns > 0) { g_corruptRuns--; chunk[0] = 0xEB; chunk[1] = 0xDE; chunk[2] = 0xB3; chunk[3] = 0x52; }   // SD menulis kepala yang salah (kasus di log)
  f->write(chunk.data(), chunk.size());
  *outGot = (uint32_t)end; gutNetCrc = crc; gutNetCrcLen = (uint32_t)end; gutGot = (uint32_t)end;
  return cut ? -3 : 200;
}
static bool gutResolveUrl(const char*, char*, int) { return false; }
static bool gutWaitTlsHeap(uint32_t) { return true; }
static void gutWifiDiag(const char*) {}
static bool gutWifiRecover() { return true; }

#include "sim_block.inc"

// ---------------- tes ----------------
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL baris %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::vector<uint8_t> makeEpub(size_t dataLen) {
  std::vector<uint8_t> d(dataLen); uint32_t x = 12345; for (auto& b : d) { x = x * 1103515245u + 12345u; b = (uint8_t)(x >> 16); }
  std::vector<uint8_t> z; auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) z.push_back((uint8_t)(v >> (8 * i))); }; auto p16 = [&](uint32_t v) { for (int i = 0; i < 2; i++) z.push_back((uint8_t)(v >> (8 * i))); };
  uint32_t crc = gutCrc32(0, d.data(), d.size());
  p32(0x04034B50); p16(20); p16(0); p16(0); p16(0); p16(0); p32(crc); p32((uint32_t)dataLen); p32((uint32_t)dataLen); p16(1); p16(0); z.push_back('a'); z.insert(z.end(), d.begin(), d.end());
  uint32_t cdOff = (uint32_t)z.size();
  p32(0x02014B50); p16(20); p16(20); p16(0); p16(0); p16(0); p16(0); p32(crc); p32((uint32_t)dataLen); p32((uint32_t)dataLen); p16(1); p16(0); p16(0); p16(0); p16(0); p32(0); p32(0); z.push_back('a');
  uint32_t cdSize = (uint32_t)z.size() - cdOff;
  p32(0x06054B50); p16(0); p16(0); p16(1); p16(1); p32(cdSize); p32(cdOff); p16(0);
  return z;
}
static std::vector<uint8_t> readAll(const char* path) { std::vector<uint8_t> v; File f = SD_MMC.open(path); if (!f) return v; v.resize(f.size()); f.read(v.data(), v.size()); f.close(); return v; }
static void wipe() { system(("rm -rf " + SDROOT + " && mkdir -p " + SDROOT + "/books").c_str()); }
static void setItem(uint32_t id, const char* title, const char* author) {
  memset(&gutItems[0], 0, sizeof(GutItem)); gutItems[0].id = id; strcpy(gutItems[0].title, title); strcpy(gutItems[0].author, author);
  snprintf(gutItems[0].epub, GUT_URL_LEN, "https://www.gutenberg.org/ebooks/%u.epub3.images", (unsigned)id);
  gutN = 1; gutDlIdx = 0; gutLight = false;
}
static void resetRun() {
  g_fetches.clear(); g_corruptRuns = 0; g_cutAt = -1; g_404Contains.clear(); gutAutoRetry = 0; gutNoWbuf = false; gutPartUrl[0] = 0; gutNetCrcLen = 0; gutNetCrcId = 0;
  gutPhase = GUT_PH_CONNECT; gutMsg[0] = 0; gutDiag[0] = 0; gutCancel = false; memset(gutReg, 0, sizeof(gutReg)); memset(gutHave, 0, sizeof(gutHave));
}
static int countEntries(const char* suffix) { int n = 0; File d = SD_MMC.open("/books"); File f = d.openNextFile(); while (f) { if (ebkEndsCI(f.name(), suffix)) n++; f.close(); f = d.openNextFile(); } d.close(); return n; }

int main() {
  g_verbose = getenv("VERBOSE") != nullptr;
  gutItems = (GutItem*)calloc(GUT_CAP, sizeof(GutItem));
  g_payload = makeEpub(300000);
  char final1513[160], fn[96];

  // A. unduhan normal: URL langsung, tanpa 302, CRC cocok, berkas = payload
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare");
  gutMakeFileName(gutItems[0], "epub", fn, sizeof fn); snprintf(final1513, sizeof final1513, "/books/%s", fn);
  CHECK(gutRegBegin(gutReg, 1513) == GUT_BEGIN_OK);
  gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE);
  CHECK(g_fetches.size() == 1 && g_fetches[0].url == "https://www.gutenberg.org/cache/epub/1513/pg1513-images-3.epub");
  CHECK(readAll(final1513) == g_payload);
  CHECK(countEntries(".part") == 0 && countEntries(".bad") == 0);
  CHECK(strstr(gutDiag, "CRC cocok") != nullptr);
  CHECK(gutHave[0] == 1);

  // B. buku yang SAMA diminta lagi -> sistem cek dulu: tidak ada permintaan jaringan sama sekali
  resetRun(); gutRegBegin(gutReg, 1513);
  gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE && g_fetches.empty() && strstr(gutMsg, "Sudah ada"));

  // C. judul beda tulisan (sumber lain) tapi nomor PG sama -> tetap terdeteksi "sudah ada"
  resetRun(); setItem(1513, "Romeo & Juliet (Play)", "Shakespeare, William");
  { char have[100]; CHECK(gutCheckBook(0, true, false, have, sizeof have) == GUT_SS_HAVE && !strcmp(have, fn)); }
  gutRegBegin(gutReg, 1513); gutDoDownload();
  CHECK(g_fetches.empty() && countEntries(".epub") == 1);

  // D. berkas lama RUSAK (kepala salah) -> dipindah ke .bad lalu diunduh ulang, bukan dianggap "sudah ada"
  { File f = SD_MMC.open(final1513, FILE_WRITE); uint8_t junk[64]; memset(junk, 0xAB, sizeof junk); f.write(junk, sizeof junk); f.close(); }
  resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); gutRegBegin(gutReg, 1513);
  gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE && g_fetches.size() == 1 && readAll(final1513) == g_payload);

  // E. SD menulis kepala rusak (persis log: net=PK, SD=EB DE B3 52) -> terdeteksi, ulang otomatis 1x dgn mode tulis lain -> sukses
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); g_corruptRuns = 1; bool mode0 = gutNoWbuf;
  gutRegBegin(gutReg, 1513); gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE);
  CHECK(g_fetches.size() == 2);
  CHECK(gutNoWbuf != mode0);
  CHECK(readAll(final1513) == g_payload);
  CHECK(countEntries(".bad") == 0 && countEntries(".part") == 0);

  // F. rusak 2x berturut-turut -> berhenti dgn pesan jelas, BUKAN jadi buku; .bad disimpan utk diperiksa
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); g_corruptRuns = 2;
  gutRegBegin(gutReg, 1513); gutDoDownload();
  CHECK(gutPhase == GUT_PH_ERROR && strstr(gutMsg, "Tulis SD rusak"));
  CHECK(!SD_MMC.exists(final1513) && countEntries(".epub") == 0 && countEntries(".bad") == 1 && countEntries(".part") == 0);

  // G. putus di tengah -> sambung (Range) dari byte terakhir; rantai CRC tetap utuh -> verifikasi lolos
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); g_cutAt = 123456;
  gutRegBegin(gutReg, 1513); gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE && g_fetches.size() == 2 && g_fetches[0].resume == 0 && g_fetches[1].resume == 123456);
  CHECK(readAll(final1513) == g_payload && strstr(gutDiag, "CRC cocok"));

  // H. URL ringan 404 -> turun ke URL berikutnya (langsung), URL dari API tidak dipakai bila tidak perlu
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); gutLight = true; g_404Contains = "pg1513.epub";
  gutRegBegin(gutReg, 1513); gutDoDownload();
  CHECK(gutPhase == GUT_PH_DONE && g_fetches.size() == 2);
  CHECK(g_fetches[0].url == "https://www.gutenberg.org/cache/epub/1513/pg1513.epub");
  CHECK(g_fetches[1].url == "https://www.gutenberg.org/cache/epub/1513/pg1513-images-3.epub");

  // I. server membalas HTML (bukan ZIP) -> ditolak, tidak jadi buku
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare");
  { std::vector<uint8_t> keep = g_payload; const char* h = "<html><body>Too Many Requests</body></html>"; g_payload.assign(h, h + strlen(h));
    strcpy(gutCT, "text/html");
    gutRegBegin(gutReg, 1513); gutDoDownload(); g_payload = keep; }
  CHECK(gutPhase == GUT_PH_ERROR && strstr(gutMsg, "Server kirim") && countEntries(".epub") == 0);

  // J. daftar: tanda ADA dibaca sekali dari direktori
  wipe(); resetRun(); setItem(1513, "Romeo and Juliet", "William Shakespeare"); gutRegBegin(gutReg, 1513); gutDoDownload();
  gutN = 2; memset(&gutItems[1], 0, sizeof(GutItem)); gutItems[1].id = 2701; strcpy(gutItems[1].title, "Moby Dick");
  gutMarkHave(0, 2);
  CHECK(gutHave[0] == 1 && gutHave[1] == 0);

  // K. pencarian: Gutendex gagal 3x -> pindah otomatis ke OPDS
  g_opdsBody = "<?xml version='1.0'?><feed><entry><title>Moby Dick</title><content type=\"text\">Melville, Herman</content><id>https://www.gutenberg.org/ebooks/2701.opds</id></entry></feed>";
  g_gutBody = "{\"count\":1,\"next\":null,\"results\":[]}";
  resetRun(); gutN = 0; gutSrc = 0; gutQApplied[0] = 0; gutPage = 1; g_gutendexFail = true;
  gutMakeReqUrl(gutSrc); gutDoSearch();
  CHECK(gutSrc == 1 && gutN == 1 && gutItems[0].id == 2701 && gutPhase == GUT_PH_IDLE);
  CHECK(!strcmp(gutItems[0].epub, "https://www.gutenberg.org/cache/epub/2701/pg2701-images-3.epub"));
  // dan sebaliknya: kedua sumber gagal -> galat tampil
  resetRun(); gutN = 0; gutSrc = 0; g_gutendexFail = true; g_opdsBody = "<html>maintenance</html>";
  gutMakeReqUrl(gutSrc); gutDoSearch();
  CHECK(gutPhase == GUT_PH_ERROR && gutN == 0);

  printf(fails ? "\n%d UJI SIMULASI GAGAL\n" : "\nSEMUA UJI SIMULASI LULUS\n", fails);
  return fails ? 1 : 0;
}
