// mock_run.cpp -- uji fungsional headless antarmuka ebook_renphone.ino.
// SD_MMC dipetakan ke folder sungguhan, layar merekam teks, sentuhan disimulasikan.
//   g++ -std=gnu++17 -g -fsanitize=address,undefined -I.. mock_run.cpp -o mock_run && ./mock_run <folder_sd>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <string>
#include <vector>
#include <memory>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
static inline void* heap_caps_realloc(void* p, size_t n, int) { return realloc(p, n); }
static inline void yield() {}
static unsigned long g_ms = 0;
static inline unsigned long millis() { return g_ms; }
namespace lgfx { struct IFont { int id; };
  namespace fonts { const IFont Font0{0}, FreeSans9pt7b{1}, FreeSerif9pt7b{2}, FreeMono9pt7b{3}; } }
static std::vector<std::string> g_lines;   // teks tergambar pada frame ini
static std::vector<std::string> g_toasts;
static bool g_f0 = false; static int g_fid = 1; static int g_scale100 = 100; static bool g_vlw = false;
static int g_images = 0, g_pushImg = 0, g_pushSpr = 0, g_loadFont = 0; static std::vector<int> g_sprX;
struct LGFXMock { int dummy = 0; };
static LGFXMock display;
namespace lgfx { struct swap565_t { uint16_t v; }; }
struct LGFX_Sprite {
  std::vector<uint16_t> buf; int W = 0, H = 0;
  LGFX_Sprite(void* = nullptr) {}
  void setPsram(bool) {}
  bool createSprite(int w, int h) { W = w; H = h; buf.assign((size_t)w * h, 0); return true; }
  void deleteSprite() { buf.clear(); W = H = 0; }
  void* getBuffer() { return buf.data(); }
  void fillSprite(uint16_t c) { for (auto& p : buf) p = c; }
  bool drawPng(const uint8_t*, uint32_t, int, int, int, int, int, int, float, float) { for (auto& p : buf) p = 0x1234; return true; }
  void fillRect(int,int,int,int,uint16_t) {} void fillRoundRect(int,int,int,int,int,uint16_t) {} void fillCircle(int,int,int,uint16_t) {}
  void drawFastHLine(int,int,int,uint16_t) {}
  void setFont(const lgfx::IFont* f) { g_f0 = (f == &lgfx::fonts::Font0); g_fid = f->id; g_vlw = false; }
  bool loadFont(const uint8_t* a) { if (!a) return false; g_vlw = true; g_loadFont++; return true; }
  void unloadFont() { g_vlw = false; }
  void setTextSize(float z) { g_scale100 = (int)(z * 100 + 0.5f); } void setTextColor(uint16_t) {}
  void setCursor(int,int) {} void print(const char* t) { g_lines.push_back(t); }
  int textWidth(const char* t) {
    int n = 0; for (const uint8_t* p = (const uint8_t*)t; *p; p++) if ((*p & 0xC0) != 0x80) n++;   // hitung codepoint
    return n * (g_vlw ? 7 : g_f0 ? 6 : 6 + g_fid) * g_scale100 / 100;
  }
  int fontHeight() { return (g_vlw ? 18 : g_f0 ? 8 : 16) * g_scale100 / 100; }
  void setTextWrap(bool) {} void setClipRect(int,int,int,int) {} void clearClipRect() {}
  void pushImage(int, int, int, int, const lgfx::swap565_t*) { g_pushImg++; }
  void pushSprite(LGFX_Sprite*, int x, int) { g_pushSpr++; g_sprX.push_back(x); }
};
struct Theme { uint16_t bg, surface, surface2, accent, accent2, text, subtext, divider, good, danger; };
static Theme g_theme = {0,0,0,1,2,3,4,5,6,7};
static Theme& T() { return g_theme; }
static int SCR_W = 320, SCR_H = 240; static bool needRedraw = false, sdReady = true;
#define BACK_H 24
#define FILE_READ "r"
#define FILE_WRITE "w"
static std::string g_root;
struct FileImpl { FILE* f = nullptr; DIR* d = nullptr; std::string path; bool dir = false; std::string nm;
  ~FileImpl() { if (f) fclose(f); if (d) closedir(d); } };
struct File {
  std::shared_ptr<FileImpl> p;
  operator bool() const { return (bool)p; }
  bool isDirectory() { return p && p->dir; }
  const char* name() { return p->nm.c_str(); }
  size_t size() { struct stat st; return stat(p->path.c_str(), &st) == 0 ? (size_t)st.st_size : 0; }
  void close() { p.reset(); }
  bool seek(uint32_t o) { return p && p->f && fseek(p->f, o, SEEK_SET) == 0; }
  size_t read(uint8_t* b, size_t n) { return p && p->f ? fread(b, 1, n, p->f) : 0; }
  size_t write(const uint8_t* b, size_t n) { return p && p->f ? fwrite(b, 1, n, p->f) : 0; }
  File openNextFile() {
    File r; if (!p || !p->d) return r;
    struct dirent* e;
    while ((e = readdir(p->d))) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      r.p = std::make_shared<FileImpl>(); r.p->path = p->path + "/" + e->d_name; r.p->nm = e->d_name;
      struct stat st; r.p->dir = stat(r.p->path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
      return r;
    }
    return r;
  }
};
struct FSMock {
  std::string full(const char* path) { std::string s = g_root; if (path[0] != '/') s += "/"; return s + path; }
  File open(const char* path, const char* mode = "r") {
    File r; std::string fp = full(path); struct stat st;
    if (strcmp(mode, "r") == 0) {
      if (stat(fp.c_str(), &st) != 0) return r;
      r.p = std::make_shared<FileImpl>(); r.p->path = fp; r.p->nm = fp.substr(fp.rfind('/') + 1);
      if (S_ISDIR(st.st_mode)) { r.p->dir = true; r.p->d = opendir(fp.c_str()); }
      else r.p->f = fopen(fp.c_str(), "rb");
    } else {
      r.p = std::make_shared<FileImpl>(); r.p->path = fp; r.p->f = fopen(fp.c_str(), "wb");
      if (!r.p->f) r.p.reset();
    }
    return r;
  }
  bool exists(const char* path) { struct stat st; return stat(full(path).c_str(), &st) == 0; }
  bool mkdir(const char* path) { return ::mkdir(full(path).c_str(), 0777) == 0; }
  bool remove(const char* path) { return ::remove(full(path).c_str()) == 0; }
  bool rename(const char* a, const char* b) { return ::rename(full(a).c_str(), full(b).c_str()) == 0; }
} SD_MMC;
static void showToast(const char* m, int = 1400) { g_toasts.push_back(m); }
static void drawToast(LGFX_Sprite&) {} static void drawBack(LGFX_Sprite&) {}
static bool isBack(int x, int y) { return x >= 4 && x <= 72 && y >= SCR_H - 27 && y <= SCR_H - 3; }
static int g_navBack = 0; static void navBack() { g_navBack++; } static void vibTap() {}
static void iosBackdrop(LGFX_Sprite&) {} static void drawStatusBar(LGFX_Sprite&) {} static void iosTitle(LGFX_Sprite&,int,int,const char*) {}
static uint16_t blend565(uint16_t a, uint16_t, uint8_t) { return a; }

// ---- mock JPEGDEC: "JPEG" uji = FF D8 'J' 'X' w(2 LE) h(2 LE) ... ----
#define JPEG_SCALE_HALF 2
#define JPEG_SCALE_QUARTER 4
#define JPEG_SCALE_EIGHTH 8
#define RGB565_LITTLE_ENDIAN 0
struct JPEGDRAW { int x, y, iWidth, iHeight; uint16_t* pPixels; };
typedef int (JPEG_DRAW_CALLBACK)(JPEGDRAW*);
struct JPEGDEC {
  int w = 0, h = 0; JPEG_DRAW_CALLBACK* cb = nullptr;
  int openRAM(uint8_t* p, int n, JPEG_DRAW_CALLBACK* c) { if (n < 8 || p[0] != 0xFF || p[1] != 0xD8 || p[2] != 'J' || p[3] != 'X') return 0; w = p[4] | (p[5] << 8); h = p[6] | (p[7] << 8); cb = c; return 1; }
  int getWidth() { return w; } int getHeight() { return h; } void close() {} void setPixelType(int) {}
  int decode(int, int, int scale) {
    int sd = scale ? scale : 1; int dw = w / sd, dh = h / sd;
    static uint16_t blk[8 * 8];
    for (int y = 0; y < dh; y += 8) for (int x = 0; x < dw; x += 8) {
      JPEGDRAW d; d.x = x; d.y = y; d.iWidth = (dw - x < 8) ? dw - x : 8; d.iHeight = (dh - y < 8) ? dh - y : 8; d.pPixels = blk;
      for (int i = 0; i < 64; i++) blk[i] = (uint16_t)(((x * 31 / (dw ? dw : 1)) << 11) | 0x07E0);
      if (!cb(&d)) return 0;
    }
    return 1;
  }
};
static void* heap_caps_calloc(size_t n, size_t sz, int) { return calloc(n, sz); }
#include <new>
struct PsramJpeg { JPEGDEC* p = nullptr; JPEGDEC* operator->() { if (!p) { void* m = calloc(1, sizeof(JPEGDEC)); p = new (m) JPEGDEC(); } return p; } };
#define RP_PNG_MAX_PIXELS 8000000L
static bool rpPngDims(const uint8_t* d, size_t len, int* w, int* h) {
  static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
  if (!d || len < 33 || memcmp(d, sig, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0) return false;
  uint32_t pw = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
  uint32_t ph = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
  if (!pw || !ph || pw > 16384 || ph > 16384) return false;
  *w = (int)pw; *h = (int)ph; return true;
}
// ---- mock String + keyboard sistem ----
struct String {
  std::string s;
  String() {} String(const char* c) : s(c) {}
  String& operator=(const char* c) { s = c; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  const char* c_str() const { return s.c_str(); } size_t length() const { return s.size(); }
};
enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };
static bool kbVisible = false; static String* kbTarget = nullptr; static KbMode kbMode = KB_LOWER;
static int kbY() { return SCR_H - 100; }
static void drawKb(LGFX_Sprite&) {}
static void kbTouch(int x, int) { if (kbTarget) *kbTarget += (char)('a' + (x / 10) % 26); }
#include "../ebook_renphone.ino"

// ---------------- driver ----------------
static LGFX_Sprite spr; static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("  %s ", ok_ ? "ok  " : "GAGAL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static void frame() { g_lines.clear(); g_pushImg = 0; g_pushSpr = 0; g_sprX.clear(); needRedraw = false; drawEbook(spr); }
static std::string screen() { std::string s; for (auto& l : g_lines) { s += l; s += "|"; } return s; }
static void tap(int x, int y) {               // jari turun lalu lepas, seperti dispatch di loop()
  ebookTouch(x, y, false, true); ebookPointerTick(true, false, x, y); ebookPointerTick(false, true, x, y);
}
static void swipe(int x0, int y0, int x1, int y1) {
  ebookTouch(x0, y0, false, true); ebookPointerTick(true, false, x0, y0);
  for (int i = 1; i <= 6; i++) { int x = x0 + (x1 - x0) * i / 6, y = y0 + (y1 - y0) * i / 6; ebookTouch(x, y, true, false); ebookPointerTick(true, true, x, y); }
  ebookPointerTick(false, true, x1, y1);
}
static void settle(int n = 14) { for (int i = 0; i < n; i++) { g_ms += 200; ebookLoopPoll(); frame(); } }
static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
static int libIdx(const char* nm) { for (int i = 0; i < ebkLibN; i++) if (!strcmp(ebkLib[i].name, nm)) return i; return -1; }
static int viewOf(const char* nm) { int li = libIdx(nm); for (int v = 0; v < ebkViewN; v++) if (ebkView[v] == li) return v; return -1; }
static int rowY(int v) { return ebkListTop() + v * EBK_ROW_H + 10 - (int)ebkScroll; }
static void scrollTo(int v) { ebkScroll = (float)(v * EBK_ROW_H); frame(); }       // jadikan baris v paling atas (terjepit batas)
static void openBook(const char* nm) { frame(); int v = viewOf(nm); scrollTo(v); tap(SCR_W / 2, rowY(v)); settle(3); }
static void menuBtn(int row, int i) { tap(ebkBtnX(row, i) + 5, ebkBtnY(row) + 5); frame(); }
static void typeQuery(const char* q) { for (const char* p = q; *p; p++) { ebookTouch(10 * (*p - 'a') + 3, kbY() + 10, false, true); } frame(); }
static bool exists(const char* p) { return access((g_root + p).c_str(), F_OK) == 0; }

int main(int argc, char** argv) {
  g_root = argv[1];
  printf("[ui] perpustakaan, metadata, sampul\n");
  ebookEnter(); frame();
  CHECK(ebkLibN == 7, "7 buku (uji, kisah, rusak, hardwrap, unicode.txt, catatan.md + 1 di root; pdf/tersembunyi diabaikan): %d", ebkLibN);
  CHECK(ebkJobsPending() >= 2, "EPUB menunggu diproses di latar: %d", ebkJobsPending());
  settle();
  int ik = libIdx("kisah.epub");
  CHECK(ik >= 0 && ebkLib[ik].meta == 1 && !strcmp(ebkLib[ik].title, "Kisah Evora") && !strcmp(ebkLib[ik].author, "Jose Alvarez"), "metadata dibaca & dilipat ke ASCII: '%s' / '%s'", ebkLib[ik].title, ebkLib[ik].author);
  CHECK(ebkLib[ik].cover == 1 && exists("/books/.covers"), "sampul dibuat dan disimpan di /books/.covers");
  CHECK(ebkLib[libIdx("rusak.epub")].meta == 2, "EPUB rusak ditandai gagal (tidak diulang)");
  CHECK(ebkJobsPending() == 0 && exists("/books/.nyxmeta"), "semua pekerjaan selesai, cache metadata tertulis");
  frame(); std::string sc = screen();
  CHECK(has(sc, "Kisah Evora") && has(sc, "Jose Alvarez"), "baris menampilkan judul + pengarang: %.100s", sc.c_str());
  CHECK(g_pushImg >= 1, "sampul digambar lewat pushImage (%d)", g_pushImg);
  CHECK(has(sc, "catatan") || has(sc, "Catatan") || has(sc, "MD"), "berkas .md terdaftar bertanda MD");
  // cache dipakai ulang tanpa buka EPUB lagi
  ebookExit(); ebookEnter(); frame();
  ik = libIdx("kisah.epub");
  CHECK(ebkLib[ik].meta == 1 && !strcmp(ebkLib[ik].title, "Kisah Evora") && ebkLib[ik].cover == 1, "sesi baru: metadata + sampul dari cache SD (tanpa pindai ulang)");
  int pend0 = ebkJobsPending(); CHECK(pend0 == 1, "hanya EPUB gagal yang tersisa? %d", pend0);   // rusak.epub tidak punya entri cache -> diproses lagi sekali
  settle();

  printf("[ui] cari + urut\n");
  int ns = 0; for (int v = 0; v < ebkViewN; v++) ns++;
  CHECK(ebkViewN == 7, "tanpa saringan: 7 baris");
  tap(SCR_W - 10 - EBK_SORT_W - 6 - 14, 38);
  CHECK(kbVisible && kbTarget == &ebkQuery, "ketuk ikon cari -> keyboard sistem terbuka");
  typeQuery("kis");
  CHECK(!strcmp(ebkQuery.c_str(), "kis") && ebkViewN == 1 && ebkView[0] == libIdx("kisah.epub"), "ketik 'kis' -> 1 hasil (kisah.epub): %d", ebkViewN);
  typeQuery("z"); CHECK(ebkViewN == 0 && has(screen(), "Tidak ada hasil"), "tak ada hasil -> pesan");
  tap(SCR_W - 10 - 8, 38); frame();
  CHECK(ebkQuery.length() == 0 && !kbVisible && ebkViewN == 7, "tombol x -> saringan dibersihkan, keyboard tutup");
  tap(SCR_W - 10 - EBK_SORT_W / 2, 38); frame();
  CHECK(ebkSortMode == EBK_SORT_TITLE, "ketuk chip urut -> Judul");
  { bool ok = true; char a[EBK_TITLE_LEN], b[EBK_TITLE_LEN];
    for (int v = 1; v < ebkViewN; v++) { ebkEntryTitle(ebkView[v - 1], a, sizeof(a)); ebkEntryTitle(ebkView[v], b, sizeof(b)); if (strcasecmp(a, b) > 0) ok = false; }
    CHECK(ok, "urutan judul A-Z"); }
  tap(SCR_W - 10 - EBK_SORT_W / 2, 38); frame();
  CHECK(ebkSortMode == EBK_SORT_AUTHOR && ebkView[0] == libIdx("kisah.epub"), "urut Penulis: yang punya pengarang di depan");
  tap(SCR_W - 10 - EBK_SORT_W / 2, 38); frame();
  CHECK(ebkSortMode == EBK_SORT_RECENT, "ketuk lagi -> kembali Terakhir");

  printf("[ui] EPUB + gambar + animasi + justify\n");
  openBook("kisah.epub");
  CHECK(ebkMode == EBK_READ && ebkOpened, "kisah.epub terbuka");
  frame();
  int imgPage = -1; for (uint32_t p = 0; p < ebkPgN; p++) if (ebkBook.text[ebkPg[p]] == 2) { imgPage = (int)p; break; }
  CHECK(imgPage > 0, "ada halaman gambar (halaman %d dari %u)", imgPage, (unsigned)ebkPgN);
  // justify: jumlah print() lebih banyak saat Ya (kata per kata) daripada Tdk (baris utuh)
  ebkPage = 0; frame(); size_t nJ = g_lines.size();
  tap(160, 100); menuBtn(1, 0); CHECK(!ebkJustify, "tombol Rata -> mati");
  tap(10, 20); ebkPage = 0; frame(); size_t nN = g_lines.size();
  CHECK(nJ > nN * 2, "justify: kata digambar satu-satu (%zu vs %zu panggilan print)", nJ, nN);
  tap(160, 100); menuBtn(1, 0); tap(10, 20); frame(); CHECK(ebkJustify, "tombol Rata -> hidup lagi");
  // animasi: halaman sebelum gambar -> maju
  ebkPage = (uint32_t)(imgPage - 1); frame(); g_ms += 1000;
  tap(300, 100);
  CHECK(ebkAnimPending && ebkPage == (uint32_t)imgPage, "ketuk kanan meminta animasi");
  frame(); CHECK(ebkAnimActive && g_pushSpr >= 2 && g_sprX.size() >= 2 && g_sprX.back() == SCR_W + g_sprX[g_sprX.size() - 2], "frame pertama: sprite lama+baru digeser (%d tempel)", g_pushSpr);
  int x1 = g_sprX.back();
  g_ms += 70; frame(); int x2 = g_sprX.back();
  CHECK(ebkAnimActive && x2 < x1 && needRedraw, "animasi bergerak: x halaman baru %d -> %d, minta frame lanjut", x1, x2);
  g_ms += 300; frame();
  CHECK(!ebkAnimActive && ebkAnimA == nullptr && ebkAnimB == nullptr && !needRedraw, "animasi selesai, sprite dibebaskan");
  CHECK(ebkImgSpr != nullptr && ebkImgW > 0 && ebkImgH > 0 && ebkImgW <= SCR_W, "halaman gambar: sprite %dx%d (<= layar)", ebkImgW, ebkImgH);
  frame(); CHECK(g_pushSpr >= 1, "gambar ditempel ke halaman");
  tap(160, 100); menuBtn(1, 1); CHECK(!ebkAnimOn, "tombol Animasi -> mati"); tap(10, 20);
  tap(300, 100); CHECK(!ebkAnimPending, "tanpa animasi: tidak diminta"); frame();
  tap(160, 100); menuBtn(1, 1); tap(10, 20);
  { int before = 0; for (int i = 0; i < 6; i++) { tap(20, 100); frame(); settle(2); before++; } CHECK(!ebkAnimActive, "geser balik beberapa halaman tanpa animasi menggantung"); }
  ebkBackToLibrary(); frame();

  printf("[ui] font Unicode (VLW)\n");
  openBook("unicode.txt"); frame();
  std::string u = screen();
  CHECK(ebkMode == EBK_READ && !ebkUniActive && has(u, "Cafe") && !has(u, "Caf\xC3\xA9"), "font ASCII: huruf beraksen dilipat -> 'Cafe': %.70s", u.c_str());
  int guard = 0; while (ebkFontIdx < 6 && guard++ < 12) { tap(160, 100); menuBtn(0, 1); tap(10, 20); }
  frame(); u = screen();
  CHECK(ebkFontIdx == 6 && ebkUniActive && g_loadFont > 0, "ganti font -> 'Sans U' dimuat dari SD (VLW)");
  CHECK(has(u, "Caf\xC3\xA9") && has(u, "\xD0\x96"), "UTF-8 dipertahankan (é, Ж) saat font punya glyph: %.80s", u.c_str());
  CHECK(!has(u, "\xE6\x97\xA5") && has(u, "?"), "CJK di luar cakupan -> '?'");
  ebkFontIdx = 0; ebkDirty = true; frame(); CHECK(!ebkUniActive && has(screen(), "Cafe"), "kembali ke font ASCII -> dilipat lagi");
  ebkFontIdx = 6; ebkDirty = true; frame();
  ebkBackToLibrary(); CHECK(ebkVlwBuf == nullptr, "VLW dibebaskan saat kembali ke perpustakaan");
  { // file VLW rusak -> jatuh ke font biasa, tidak crash
    FILE* f = fopen((g_root + "/books/fonts/nyxbook-sans15.vlw").c_str(), "wb"); fwrite("rusak", 1, 5, f); fclose(f);
    ebkProbeFonts(); openBook("unicode.txt"); ebkFontIdx = 6; ebkDirty = true; g_toasts.clear(); frame();
    CHECK(!ebkUniActive && !g_toasts.empty(), "VLW rusak -> font biasa + toast '%s'", g_toasts.empty() ? "" : g_toasts.back().c_str());
    ebkBackToLibrary(); }
  ebkFontIdx = 0;

  printf("[ui] Markdown\n");
  openBook("catatan.md"); frame();
  CHECK(ebkMode == EBK_READ && ebkBook.kind == EB_KIND_TXT && ebkBook.nToc >= 2, "md terbuka, daftar isi dari judul (%d)", ebkBook.nToc);
  sc = screen(); CHECK(has(sc, "Catatan Harian") && !has(sc, "**") && !has(sc, "# "), "penanda markdown dibersihkan: %.80s", sc.c_str());
  ebkBackToLibrary();

  printf("[ui] tanda Selesai\n");
  openBook("hardwrap.txt"); int g3 = 0; while (ebkPage + 1 < ebkPgN && g3++ < 3000) { ebkPage++; }
  frame(); tap(160, 100); menuBtn(0, 3);
  CHECK(ebkMode == EBK_LIB && ebkLib[libIdx("hardwrap.txt")].pct == 100, "membaca sampai akhir -> 100%%");
  frame(); CHECK(has(screen(), "Selesai"), "perpustakaan menampilkan tanda 'Selesai'");

  printf("[ui] alur lama: navigasi, menu, daftar isi, posisi tersimpan\n");
  ebookExit(); ebookEnter(); settle();
  openBook("uji.epub");
  CHECK(ebkMode == EBK_READ && ebkOpened, "uji.epub terbuka"); frame(); sc = screen();
  CHECK(ebkPgN > 3 && has(sc, "Bab 1"), "halaman terpaginasi (%u) dan teks bab 1 tergambar", (unsigned)ebkPgN);
  uint32_t p0 = ebkPage;
  tap(300, 100); frame(); CHECK(ebkPage == p0 + 1, "ketuk kanan -> halaman berikut"); settle(2);
  tap(20, 100);  frame(); CHECK(ebkPage == p0, "ketuk kiri -> halaman sebelumnya"); settle(2);
  swipe(250, 120, 60, 125); frame(); CHECK(ebkPage == p0 + 1, "geser kiri -> halaman berikut"); settle(2);
  swipe(60, 120, 250, 118); frame(); CHECK(ebkPage == p0, "geser kanan -> halaman sebelumnya"); settle(2);
  tap(300, 235); CHECK(ebkPage == p0, "ketuk di zona gestur bawah diabaikan");
  int ch0 = ebkBook.curCh; int guard1 = 0;
  while (ebkBook.curCh == ch0 && guard1++ < 500) { tap(300, 100); settle(2); }
  CHECK(ebkBook.curCh == ch0 + 1 && ebkPage == 0, "halaman terakhir + ketuk kanan -> bab berikut (bab %d)", ebkBook.curCh);
  tap(20, 100); frame(); settle(2);
  CHECK(ebkBook.curCh == ch0 && ebkPage == ebkPgN - 1, "ketuk kiri di halaman 0 -> bab sebelumnya, halaman terakhir");
  tap(160, 100); frame(); CHECK(ebkMenu, "ketuk tengah -> menu"); sc = screen();
  CHECK(has(sc, "Isi") && has(sc, "Tutup") && has(sc, "Hal ") && has(sc, "Rata: Ya") && has(sc, "Animasi: Ya"), "menu tergambar (dua baris tombol)");
  tap(ebkSliderX1(), ebkSliderY()); CHECK(ebkPage == ebkPgN - 1, "slider ujung kanan -> halaman terakhir");
  swipe(ebkSliderX1(), ebkSliderY(), ebkSliderX0(), ebkSliderY()); frame(); CHECK(ebkPage == 0, "geser slider ke kiri -> halaman 0");
  int f0 = ebkFontIdx; uint32_t pg0 = ebkPgN;
  menuBtn(0, 1); CHECK(ebkFontIdx == ebkNextFont(f0) && ebkPgN != pg0, "tombol Aa: font berikut & paginasi ulang (%u -> %u)", (unsigned)pg0, (unsigned)ebkPgN);
  int pi0 = ebkPalIdx; tap(ebkBtnX(0, 2) + 5, ebkBtnY(0) + 5); CHECK(ebkPalIdx == (pi0 + 1) % 4, "tombol Tema");
  menuBtn(0, 0); CHECK(ebkMode == EBK_TOC && ebkBook.nToc >= 3, "tombol Isi -> daftar isi (%d)", ebkBook.nToc);
  sc = screen(); CHECK(has(sc, "Judul NCX 3"), "judul bab dari NCX tampil");
  tap(SCR_W / 2, ebkListTop() + 2 * EBK_TOC_ROW_H + 8); frame();
  CHECK(ebkMode == EBK_READ && ebkBook.curCh == 2 && ebkPage == 0, "ketuk entri 3 -> bab 3 (curCh=%d)", ebkBook.curCh);
  for (int i = 0; i < 4; i++) { tap(300, 100); settle(2); }
  uint32_t savedOff = ebkPg[ebkPage]; int savedCh = ebkBook.curCh; int fontNow = ebkFontIdx;
  tap(160, 100); menuBtn(0, 3);
  CHECK(ebkMode == EBK_LIB && !ebkOpened, "tombol Tutup -> kembali ke daftar");
  CHECK(ebkLib[ebkView[0]].pct > 0 && !strcmp(ebkLib[ebkView[0]].name, "uji.epub"), "buku terakhir dibaca di urutan teratas");
  ebookExit(); CHECK(ebkLib == nullptr && ebkProgs == nullptr && ebkMetas == nullptr && ebkView == nullptr && ebkCvPool == nullptr && ebkVlwBuf == nullptr && ebkJpeg.p == nullptr, "Exit membebaskan semua buffer");
  ebookEnter(); settle();
  scrollTo(0); tap(SCR_W / 2, rowY(0)); settle(3);
  CHECK(ebkMode == EBK_READ && ebkBook.curCh == savedCh, "buka ulang: bab dipulihkan (%d)", ebkBook.curCh);
  frame(); CHECK(ebkFontIdx == fontNow, "font tersimpan antar sesi (%d)", ebkFontIdx);
  CHECK(ebkPg[ebkPage] <= savedOff && (ebkPage + 1 >= ebkPgN || ebkPg[ebkPage + 1] > savedOff), "offset teks dipulihkan di halaman yang sama");
  uint32_t pl = ebkPgN; SCR_W = 240; SCR_H = 320; frame();
  CHECK(ebkLayW == 240 && ebkPgN != pl, "ganti orientasi -> paginasi ulang (%u -> %u)", (unsigned)pl, (unsigned)ebkPgN);
  SCR_W = 320; SCR_H = 240; frame();
  ebookExit();
  printf("[ui] berkas rusak\n");
  ebookEnter(); settle(); frame();
  g_toasts.clear(); openBook("rusak.epub");
  CHECK(ebkMode == EBK_LIB && !g_toasts.empty(), "EPUB rusak -> toast '%s', kembali ke daftar", g_toasts.empty() ? "" : g_toasts.back().c_str());
  ebookExit();
  printf("\nUI: %s\n", fails ? "ADA GAGAL" : "SEMUA LULUS");
  return fails ? 1 : 0;
}
