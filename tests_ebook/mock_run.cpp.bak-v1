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
static bool g_f0 = false; static int g_fid = 1; static int g_scale100 = 100;
struct LGFX_Sprite {
  void fillRect(int,int,int,int,uint16_t) {} void fillRoundRect(int,int,int,int,int,uint16_t) {} void fillCircle(int,int,int,uint16_t) {}
  void drawFastHLine(int,int,int,uint16_t) {}
  void setFont(const lgfx::IFont* f) { g_f0 = (f == &lgfx::fonts::Font0); g_fid = f->id; }
  void setTextSize(float z) { g_scale100 = (int)(z * 100 + 0.5f); } void setTextColor(uint16_t) {}
  void setCursor(int,int) {} void print(const char* t) { g_lines.push_back(t); }
  int textWidth(const char* t) { return (int)strlen(t) * (g_f0 ? 6 : 6 + g_fid) * g_scale100 / 100; }
  int fontHeight() { return (g_f0 ? 8 : 16) * g_scale100 / 100; }
  void setTextWrap(bool) {} void setClipRect(int,int,int,int) {} void clearClipRect() {}
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

#include "../ebook_renphone.ino"

// ---------------- driver ----------------
static LGFX_Sprite spr; static int fails = 0;
#define CHECK(c, ...) do { bool ok_ = (c); printf("  %s ", ok_ ? "ok  " : "GAGAL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static void frame() { g_lines.clear(); needRedraw = false; drawEbook(spr); }
static std::string screen() { std::string s; for (auto& l : g_lines) { s += l; s += "|"; } return s; }
static void tap(int x, int y) {               // jari turun lalu lepas, seperti dispatch di loop()
  ebookTouch(x, y, false, true); ebookPointerTick(true, false, x, y); ebookPointerTick(false, true, x, y);
}
static void swipe(int x0, int y0, int x1, int y1) {
  ebookTouch(x0, y0, false, true); ebookPointerTick(true, false, x0, y0);
  for (int i = 1; i <= 6; i++) { int x = x0 + (x1 - x0) * i / 6, y = y0 + (y1 - y0) * i / 6; ebookTouch(x, y, true, false); ebookPointerTick(true, true, x, y); }
  ebookPointerTick(false, true, x1, y1);
}
static void settle() { for (int i = 0; i < 5; i++) { g_ms += 200; ebookLoopPoll(); frame(); } }
static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

int main(int argc, char** argv) {
  g_root = argv[1];
  printf("[ui] perpustakaan\n");
  ebookEnter(); frame();
  std::string sc = screen();
  CHECK(ebkLibN == 4, "4 buku ditemukan (3 di /books + 1 di root; .pdf dan file tersembunyi diabaikan): %d", ebkLibN);
  CHECK(has(sc, "EPUB") && has(sc, "TXT"), "daftar menampilkan tipe: %s", sc.c_str());
  int ie = -1, it = -1; for (int i = 0; i < ebkLibN; i++) { if (!strcmp(ebkLib[i].name, "uji.epub")) ie = i; if (!strcmp(ebkLib[i].name, "hardwrap.txt")) it = i; }
  CHECK(ie >= 0 && it >= 0, "uji.epub dan hardwrap.txt terdaftar");
  tap(10, 5);                                   // di atas daftar: tidak membuka apa pun
  CHECK(ebkMode == EBK_LIB, "ketuk di luar daftar tidak membuka buku");
  tap(SCR_W / 2, 50 + ie * EBK_ROW_H + 10);
  CHECK(ebkMode == EBK_LOADING, "ketuk buku -> layar Membuka...");
  frame(); CHECK(has(screen(), "Membuka..."), "layar loading tampil");
  settle();
  CHECK(ebkMode == EBK_READ && ebkOpened, "EPUB terbuka, mode baca");
  frame(); sc = screen();
  CHECK(ebkPgN > 3, "halaman terpaginasi: %u", (unsigned)ebkPgN);
  CHECK(has(sc, "Bab 1") , "teks bab 1 tergambar: %.80s", sc.c_str());
  uint32_t p0 = ebkPage;
  tap(300, 100); frame(); CHECK(ebkPage == p0 + 1, "ketuk kanan -> halaman berikut");
  tap(20, 100);  frame(); CHECK(ebkPage == p0, "ketuk kiri -> halaman sebelumnya");
  swipe(250, 120, 60, 125); frame(); CHECK(ebkPage == p0 + 1, "geser kiri -> halaman berikut");
  swipe(60, 120, 250, 118); frame(); CHECK(ebkPage == p0, "geser kanan -> halaman sebelumnya");
  tap(300, 235); CHECK(ebkPage == p0, "ketuk di zona gestur bawah diabaikan");
  // lintas bab ke depan & belakang
  int ch0 = ebkBook.curCh; int guard = 0;
  while (ebkBook.curCh == ch0 && guard++ < 500) tap(300, 100);
  CHECK(ebkBook.curCh == ch0 + 1 && ebkPage == 0, "halaman terakhir + ketuk kanan -> bab berikut, halaman 0 (bab %d)", ebkBook.curCh);
  tap(20, 100); frame();
  CHECK(ebkBook.curCh == ch0 && ebkPage == ebkPgN - 1, "ketuk kiri di halaman 0 -> bab sebelumnya, halaman terakhir");
  // menu
  tap(160, 100); frame();
  CHECK(ebkMenu, "ketuk tengah -> menu"); sc = screen();
  CHECK(has(sc, "Isi") && has(sc, "Tutup") && has(sc, "Hal "), "menu tergambar: %s", sc.substr(sc.find("Isi") == std::string::npos ? 0 : sc.find("Isi"), 40).c_str());
  tap(ebkSliderX1(), ebkSliderY()); 
  CHECK(ebkPage == ebkPgN - 1, "slider ujung kanan -> halaman terakhir");
  swipe(ebkSliderX1(), ebkSliderY(), ebkSliderX0(), ebkSliderY()); frame();
  CHECK(ebkPage == 0, "geser slider ke kiri -> halaman 0");
  int f0 = ebkFontIdx; uint32_t pg0 = ebkPgN;
  tap(ebkBtnX(1) + 5, ebkBtnY() + 5); frame();
  CHECK(ebkFontIdx == (f0 + 1) % 6 && ebkPgN != pg0, "tombol Aa: ganti font & paginasi ulang (%u -> %u)", (unsigned)pg0, (unsigned)ebkPgN);
  int pi0 = ebkPalIdx; tap(ebkBtnX(2) + 5, ebkBtnY() + 5);
  CHECK(ebkPalIdx == (pi0 + 1) % 4, "tombol Tema");
  tap(ebkBtnX(0) + 5, ebkBtnY() + 5); frame();
  CHECK(ebkMode == EBK_TOC && ebkBook.nToc >= 3, "tombol Isi -> daftar isi (%d entri)", ebkBook.nToc);
  sc = screen(); CHECK(has(sc, "Judul NCX 3"), "judul bab dari NCX tampil");
  tap(SCR_W / 2, 50 + 2 * EBK_TOC_ROW_H + 8); frame();
  CHECK(ebkMode == EBK_READ && ebkBook.curCh == 2 && ebkPage == 0, "ketuk entri 3 -> loncat ke bab 3 (curCh=%d)", ebkBook.curCh);
  // posisi tersimpan lintas sesi
  for (int i = 0; i < 4; i++) tap(300, 100);
  uint32_t savedOff = ebkPg[ebkPage]; int savedCh = ebkBook.curCh; int fontNow = ebkFontIdx;
  tap(160, 100);
  tap(ebkBtnX(3) + 5, ebkBtnY() + 5);
  CHECK(ebkMode == EBK_LIB && !ebkOpened, "tombol Tutup -> kembali ke daftar, buku ditutup");
  CHECK(ebkLib[0].pct > 0 && !strcmp(ebkLib[0].name, "uji.epub"), "buku terakhir dibaca di urutan teratas, persen %u%%", (unsigned)ebkLib[0].pct);
  ebookExit(); CHECK(ebkLib == nullptr && ebkProgs == nullptr, "Exit membebaskan semua buffer");
  ebookEnter(); settle();
  tap(SCR_W / 2, 50 + 10); settle();
  CHECK(ebkMode == EBK_READ && ebkBook.curCh == savedCh, "buka ulang: bab dipulihkan (%d)", ebkBook.curCh);
  frame();
  CHECK(ebkFontIdx == fontNow, "font tersimpan antar sesi (%d)", ebkFontIdx);
  CHECK(ebkPg[ebkPage] <= savedOff && (ebkPage + 1 >= ebkPgN || ebkPg[ebkPage + 1] > savedOff), "offset teks dipulihkan di halaman yang sama");
  // orientasi
  uint32_t pl = ebkPgN; SCR_W = 240; SCR_H = 320; frame();
  CHECK(ebkLayW == 240 && ebkPgN != pl, "ganti orientasi -> paginasi ulang (%u -> %u)", (unsigned)pl, (unsigned)ebkPgN);
  SCR_W = 320; SCR_H = 240; frame();
  ebookExit();
  // TXT berbaris keras
  printf("[ui] TXT\n");
  ebookEnter(); for (int i = 0; i < ebkLibN; i++) if (!strcmp(ebkLib[i].name, "hardwrap.txt")) it = i;
  tap(SCR_W / 2, 50 + it * EBK_ROW_H + 10); settle(); frame();
  CHECK(ebkMode == EBK_READ && ebkBook.kind == EB_KIND_TXT && ebkBook.nToc == 2, "TXT terbuka, 2 judul bab di daftar isi");
  sc = screen(); CHECK(has(sc, "CHAPTER I"), "judul bab tampil: %.60s", sc.c_str());
  tap(160, 100); tap(ebkBtnX(0) + 5, ebkBtnY() + 5); tap(SCR_W / 2, 50 + 1 * EBK_TOC_ROW_H + 8); frame();
  CHECK(ebkMode == EBK_READ && has(screen(), "CHAPTER II"), "daftar isi TXT -> loncat ke CHAPTER II");
  int guard2 = 0; while (ebkPage + 1 < ebkPgN && guard2++ < 2000) tap(300, 100);
  g_toasts.clear(); tap(300, 100); CHECK(!g_toasts.empty() && g_toasts.back() == "Akhir buku", "halaman terakhir buku -> toast Akhir buku");
  ebookExit();
  // berkas rusak
  printf("[ui] berkas rusak\n");
  ebookEnter(); int ib = -1; for (int i = 0; i < ebkLibN; i++) if (!strcmp(ebkLib[i].name, "rusak.epub")) ib = i;
  g_toasts.clear(); tap(SCR_W / 2, 50 + ib * EBK_ROW_H + 10); settle();
  CHECK(ib >= 0 && ebkMode == EBK_LIB && !g_toasts.empty(), "EPUB rusak -> toast '%s', kembali ke daftar", g_toasts.empty() ? "" : g_toasts.back().c_str());
  ebookExit();
  printf("\nUI: %s\n", fails ? "ADA GAGAL" : "SEMUA LULUS");
  return fails ? 1 : 0;
}
