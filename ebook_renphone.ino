// =====================================================================
// ebook_renphone.ino -- app BUKU (SCR_EBOOK): pembaca e-book .txt, .md dan .epub
// =====================================================================
// Letakkan file .txt / .md / .epub di folder /books pada SD card (folder dibuat otomatis;
// file di root SD juga ikut terdaftar). Fitur:
//   * Perpustakaan  : sampul EPUB (cache di /books/.covers), judul + pengarang dari metadata EPUB,
//                     cari judul (keyboard sistem), urut Terakhir / Judul / Penulis, tanda "Selesai".
//   * Pembaca       : halaman berpaginasi, ketuk kanan/kiri atau geser untuk pindah halaman (dengan
//                     animasi geser), ketuk tengah untuk menu (slider, Isi, Aa, Tema, Rata, Animasi, Tutup).
//   * Teks          : rata kiri-kanan (justify) bisa dimatikan.
//   * Font          : Sans, Serif, Mono, Kecil, Sans+, Serif+ (font GFX bawaan, ASCII) dan 5 font Unicode
//                     "... U" bila file .vlw ada di /books/fonts (lihat tools/make_vlw.py): huruf beraksen,
//                     Yunani, Kiril, kutip/strip melengkung tampil benar. CJK/Arab/Thai tetap "?".
//   * Gambar EPUB   : gambar JPEG/PNG di dalam bab tampil satu halaman penuh (hiasan kecil dilewati).
//   * Markdown      : .md dibersihkan jadi teks (judul, daftar, kutipan, kode, tabel) + daftar isi dari judul.
//   * Posisi baca   : disimpan per buku di /books/.nyxreader (bab + offset, tidak bergantung font).
//   * EPUB 2 & 3    : bab dibaca satu per satu dari ZIP (hemat RAM), daftar isi dari NCX / nav.
//   * TXT           : UTF-8, UTF-16 (BOM), Latin-1/Windows-1252; teks berbaris-keras di-reflow;
//                     judul "Bab/Chapter N" terdeteksi untuk daftar isi. Batas 3 MB per file.
//
// Logika berat (inflate, ZIP, EPUB, HTML->teks, Markdown, tata letak) ada di ebook_core.h dan diuji di
// komputer lewat tests_ebook/. Berkas ini hanya antarmuka + SD + dekoder gambar.
//
// ATURAN PROYEK: semua buffer besar di PSRAM dan dibebaskan saat app ditutup; tidak ada String
// per frame; fungsi publik hanya bertipe primitif / LGFX_Sprite& (bug prototype arduino-cli) --
// fungsi static pun tidak memakai tipe struct di signature.
//
// DIPASANG ke phone.ino lewat apply_ebook_patch.py (v1, tidak berubah di v2):
//   enum Screen <- SCR_EBOOK ; apps[] + warna + ikon 'b' ; loop() <- ebookLoopPoll() ;
//   dispatch sentuhan <- ebookPointerTick() (ketuk diproses saat jari LEPAS)
// =====================================================================

static void* ebPsAlloc(size_t n) {
  if (n == 0) n = 1;
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p && n <= 4096) p = heap_caps_malloc(n, MALLOC_CAP_8BIT);   // kecil: boleh RAM internal
  return p;
}
static void* ebPsRealloc(void* q, size_t n) {
  if (n == 0) n = 1;
  void* p = heap_caps_realloc(q, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}
#define EB_ALLOC(n)      ebPsAlloc(n)
#define EB_REALLOC(p, n) ebPsRealloc((p), (n))
#define EB_FREE(p)       free(p)
#define EB_YIELD()       yield()
#include "ebook_core.h"

// ---------------------------------------------------------------------
// konstanta & state
// ---------------------------------------------------------------------
#define EBK_DIR        "/books"
#define EBK_PROG       "/books/.nyxreader"
#define EBK_PROG_TMP   "/books/.nyxreader.tmp"
#define EBK_META       "/books/.nyxmeta"
#define EBK_META_TMP   "/books/.nyxmeta.tmp"
#define EBK_COVDIR     "/books/.covers"
#define EBK_FONTDIR    "/books/fonts"
#define EBK_MAX_BOOKS  200
#define EBK_NAME_LEN   96
#define EBK_TITLE_LEN  56
#define EBK_AUTH_LEN   36
#define EBK_ROW_H      58
#define EBK_TOC_ROW_H  24
#define EBK_CV_W       36
#define EBK_CV_H       48
#define EBK_CV_PX      (EBK_CV_W * EBK_CV_H)
#define EBK_CV_SLOTS   16
#define EBK_ANIM_MS    170
#define EBK_VLW_MAX    (1100u * 1024u)
#define EBK_NFONT      11

enum { EBK_LIB = 0, EBK_LOADING, EBK_READ, EBK_TOC, EBK_GUT };

// ---- Gutenberg (gutenberg_renphone.ino, dikompilasi SETELAH berkas ini) ----
void gutEnter();
void gutExit();
void gutLoopPoll();
void gutDraw(LGFX_Sprite& s);
void gutTouch(int x, int y, bool held, bool isNew);
void gutPointerTick(bool touched, bool wasT, int tx, int ty);
enum { EBK_SORT_RECENT = 0, EBK_SORT_TITLE, EBK_SORT_AUTHOR };

struct EbkEntry {
  char name[EBK_NAME_LEN]; char title[EBK_TITLE_LEN]; char author[EBK_AUTH_LEN];
  uint32_t size, key, seq;
  uint8_t epub, md, inRoot, pct, meta, cover;      // meta: 0 belum, 1 selesai, 2 gagal; cover: 1 = ada thumbnail di SD
};
struct EbkMeta { char name[EBK_NAME_LEN]; char title[EBK_TITLE_LEN]; char author[EBK_AUTH_LEN]; uint32_t size; uint8_t cover; };
struct EbkProg { char name[EBK_NAME_LEN]; int32_t ch; uint32_t off; uint8_t pct; uint32_t seq; };
struct EbkFont { const char* name; const lgfx::IFont* f; float scale; const char* vlw; };
struct EbkPal  { const char* name; uint16_t bg, text, sub, head; };

static const EbkFont ebkFonts[EBK_NFONT] = {
  { "Sans",    &lgfx::fonts::FreeSans9pt7b,  1.0f, nullptr },
  { "Serif",   &lgfx::fonts::FreeSerif9pt7b, 1.0f, nullptr },
  { "Mono",    &lgfx::fonts::FreeMono9pt7b,  1.0f, nullptr },
  { "Kecil",   &lgfx::fonts::Font0,          1.0f, nullptr },
  { "Sans+",   &lgfx::fonts::FreeSans9pt7b,  1.3f, nullptr },
  { "Serif+",  &lgfx::fonts::FreeSerif9pt7b, 1.3f, nullptr },
  { "Sans U",  &lgfx::fonts::FreeSans9pt7b,  1.0f, EBK_FONTDIR "/nyxbook-sans15.vlw" },
  { "Serif U", &lgfx::fonts::FreeSerif9pt7b, 1.0f, EBK_FONTDIR "/nyxbook-serif15.vlw" },
  { "Mono U",  &lgfx::fonts::FreeMono9pt7b,  1.0f, EBK_FONTDIR "/nyxbook-mono15.vlw" },
  { "Sans U+", &lgfx::fonts::FreeSans9pt7b,  1.3f, EBK_FONTDIR "/nyxbook-sans19.vlw" },
  { "Serif U+",&lgfx::fonts::FreeSerif9pt7b, 1.3f, EBK_FONTDIR "/nyxbook-serif19.vlw" },
};
static const EbkPal ebkPals[4] = {
  { "Terang", 0xF79E, 0x18E3, 0x8410, 0x1AB9 },
  { "Sepia",  0xF79B, 0x5A26, 0x8BCC, 0x9A43 },
  { "Gelap",  0x18C3, 0xCE59, 0x738E, 0x7DBF },
  { "Hitam",  0x0000, 0xB596, 0x630C, 0x5D1F },
};
static const char* const ebkSortNames[3] = { "Terakhir", "Judul", "Penulis" };

static uint8_t   ebkMode = EBK_LIB;
// perpustakaan
static EbkEntry* ebkLib = nullptr;   static int ebkLibN = 0;
static EbkProg*  ebkProgs = nullptr; static int ebkProgN = 0;
static EbkMeta*  ebkMetas = nullptr; static int ebkMetaN = 0; static bool ebkMetaDirty = false; static int ebkJobsSinceSave = 0;
static int*      ebkView = nullptr;  static int ebkViewN = 0; static bool ebkViewDirty = true;   // indeks ke ebkLib (hasil cari/urut)
static String    ebkQuery;           static char ebkQApplied[40] = "";
static uint8_t   ebkSortMode = EBK_SORT_RECENT;
static uint32_t  ebkSeq = 0;
static float     ebkScroll = 0;      // px (perpustakaan / daftar isi)
static int       ebkPendingIdx = -1; static unsigned long ebkLoadDue = 0, ebkJobDue = 0, ebkLastTouchMs = 0;
// sampul (cache RAM)
static uint16_t* ebkCvPool = nullptr; static uint32_t ebkCvKey[EBK_CV_SLOTS]; static int ebkCvNext = 0;
// buku terbuka
static bool      ebkOpened = false;
static File      ebkFile;
static EbBook    ebkBook;
static char      ebkName[EBK_NAME_LEN];     // nama berkas buku terbuka
static uint32_t* ebkPg = nullptr; static uint32_t ebkPgCap = 0, ebkPgN = 0, ebkPage = 0;
static EbLay     ebkLay;
static int       ebkLineH = 14, ebkLayW = 0, ebkLayH = 0;
static bool      ebkDirty = true;            // perlu hitung ulang tata letak
static uint32_t  ebkWantOff = 0; static bool ebkWantLast = false;
static int       ebkFontIdx = 0, ebkPalIdx = 0;
static bool      ebkJustify = true, ebkAnimOn = true;
static bool      ebkMenu = false;
static int       ebkTurns = 0;
// font Unicode (VLW dari SD, disimpan di PSRAM selama dipakai)
static uint8_t*  ebkVlwBuf = nullptr; static int ebkVlwIdx = -1; static bool ebkUniActive = false;
static uint8_t   ebkFontOk[EBK_NFONT];
static LGFX_Sprite* ebkCpwSpr = nullptr; static uint32_t ebkCpwKey[256]; static int16_t ebkCpwVal[256];
// animasi geser halaman
static bool      ebkAnimPending = false, ebkAnimActive = false; static int ebkAnimDir = 1; static uint32_t ebkAnimFrom = 0; static unsigned long ebkAnimT0 = 0;
static LGFX_Sprite* ebkAnimA = nullptr; static LGFX_Sprite* ebkAnimB = nullptr;
// halaman gambar
static LGFX_Sprite* ebkImgSpr = nullptr; static int ebkImgKey = -1, ebkImgW = 0, ebkImgH = 0; static char ebkImgErr[40] = "";
// sentuhan
static bool ebkDown = false, ebkMoved = false, ebkSlide = false;
static int  ebkSx = 0, ebkSy = 0, ebkLx = 0, ebkLy = 0, ebkPy = 0;
static PsramJpeg ebkJpeg;                   // objek JPEGDEC lazy di PSRAM (lihat PsramJpeg di phone.ino)

// ---------------------------------------------------------------------
// util
// ---------------------------------------------------------------------
static bool ebkSdRead(void* ctx, uint32_t off, void* dst, uint32_t n) {
  File* f = (File*)ctx;
  if (!f->seek(off)) return false;
  return f->read((uint8_t*)dst, n) == n;
}

static bool ebkEndsCI(const char* s, const char* suf) {
  int n = (int)strlen(s), k = (int)strlen(suf);
  return n >= k && ebEqCI(s + n - k, suf, k);
}

static void ebkBaseName(const char* in, char* out, int cap) {   // tanpa folder
  const char* sl = strrchr(in, '/');
  const char* b = sl ? sl + 1 : in;
  strncpy(out, b, (size_t)cap - 1); out[cap - 1] = 0;
}

static void ebkStem(const char* name, char* out, int cap) {      // tanpa ekstensi
  strncpy(out, name, (size_t)cap - 1); out[cap - 1] = 0;
  char* d = strrchr(out, '.');
  if (d && d != out) *d = 0;
}

// judul untuk UI (ASCII murni): huruf beraksen dilipat, sisanya '?'
static void ebkFold(const char* src, char* dst, int cap) { ebFoldAscii(src, dst, cap); }

static void ebkFitText(LGFX_Sprite& s, char* buf, int maxPx) {   // potong + "..." sesuai lebar
  int n = (int)strlen(buf);
  if (s.textWidth(buf) <= maxPx) return;
  while (n > 3 && s.textWidth(buf) > maxPx - s.textWidth("...")) buf[--n] = 0;
  strcat(buf, "...");
}

static void ebkFmtSize(char* out, int cap, uint32_t b) {
  if (b >= (1u << 20)) snprintf(out, (size_t)cap, "%.1f MB", (double)b / 1048576.0);
  else snprintf(out, (size_t)cap, "%u KB", (unsigned)((b + 1023) / 1024));
}

static bool ebkContainsCI(const char* hay, const char* needle) {
  int nl = (int)strlen(needle);
  if (nl == 0) return true;
  for (const char* p = hay; *p; p++) if (ebEqCI(p, needle, nl) && strlen(p) >= (size_t)nl) return true;
  return false;
}

static uint32_t ebkHash(const char* s, uint32_t size) {            // FNV-1a nama + ukuran
  uint32_t h = 2166136261u;
  for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
  for (int i = 0; i < 4; i++) { h ^= (size >> (i * 8)) & 0xFF; h *= 16777619u; }
  return h;
}

static void ebkPath(int i, char* out, int cap) {
  if (ebkLib[i].inRoot) snprintf(out, (size_t)cap, "/%s", ebkLib[i].name);
  else snprintf(out, (size_t)cap, EBK_DIR "/%s", ebkLib[i].name);
}

// judul yang ditampilkan: metadata kalau ada, kalau tidak nama berkas tanpa ekstensi (dilipat ke ASCII)
static void ebkEntryTitle(int i, char* out, int cap) {
  if (ebkLib[i].title[0]) { strncpy(out, ebkLib[i].title, (size_t)cap - 1); out[cap - 1] = 0; return; }
  char stem[EBK_NAME_LEN]; ebkStem(ebkLib[i].name, stem, sizeof(stem));
  ebkFold(stem, out, cap);
}

// ---------------------------------------------------------------------
// font Unicode (VLW dari SD -> PSRAM) + lebar glyph non-ASCII
// ---------------------------------------------------------------------
static void ebkCpwReset() { for (int i = 0; i < 256; i++) { ebkCpwKey[i] = 0xFFFFFFFFu; ebkCpwVal[i] = 0; } }

static void ebkVlwFree() {
  if (ebkVlwBuf) { free(ebkVlwBuf); ebkVlwBuf = nullptr; }
  ebkVlwIdx = -1; ebkUniActive = false;
}

static void ebkProbeFonts() {
  for (int i = 0; i < EBK_NFONT; i++) {
    ebkFontOk[i] = 1;
    if (ebkFonts[i].vlw) ebkFontOk[i] = (sdReady && SD_MMC.exists(ebkFonts[i].vlw)) ? 1 : 0;
  }
}

// pastikan data VLW font idx ada di ebkVlwBuf. Mengembalikan false bila gagal.
static bool ebkVlwEnsure(int idx) {
  if (ebkVlwBuf && ebkVlwIdx == idx) return true;
  ebkVlwFree();
  File f = SD_MMC.open(ebkFonts[idx].vlw, FILE_READ);
  if (!f) return false;
  size_t sz = (size_t)f.size();
  if (sz < 64 || sz > EBK_VLW_MAX) { f.close(); return false; }
  uint8_t* b = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) { f.close(); return false; }
  size_t got = 0;
  while (got < sz) {
    size_t want = sz - got; if (want > 16384) want = 16384;
    int n = f.read(b + got, want);
    if (n <= 0) break;
    got += (size_t)n;
    yield();
  }
  f.close();
  // header VLW: jumlah glyph (int32 BE) dan versi 11
  uint32_t gc = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  uint32_t ver = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
  if (got != sz || gc == 0 || gc > 20000 || ver != 11) { free(b); return false; }
  ebkVlwBuf = b; ebkVlwIdx = idx;
  return true;
}

// lebar piksel codepoint non-ASCII pada font Unicode aktif (0 = tidak ada glyph -> dilipat). Dipakai ebook_core.h.
static int ebkCpw(uint32_t cp) {
  if (!ebkUniActive || !ebkCpwSpr) return 0;
  uint32_t h = (cp * 2654435761u) >> 24;
  if (ebkCpwKey[h] == cp) return ebkCpwVal[h];
  char u[6]; int n = ebEncUtf8(cp, u); u[n] = 0;
  int w = ebkCpwSpr->textWidth(u);
  if (w < 0) w = 0; if (w > 100) w = 100;
  ebkCpwKey[h] = cp; ebkCpwVal[h] = (int16_t)w;
  return w;
}

// pasang font terpilih ke sprite t. Font Unicode gagal dimuat -> jatuh ke font GFX yang setara.
static void ebkApplyFont(LGFX_Sprite& t) {
  const EbkFont& fo = ebkFonts[ebkFontIdx];
  t.unloadFont();
  if (fo.vlw && ebkFontOk[ebkFontIdx] && ebkVlwEnsure(ebkFontIdx) && t.loadFont(ebkVlwBuf)) {
    t.setTextSize(1);
    if (!ebkUniActive || ebkCpwSpr != &t) { /* spr berbeda: cache lebar tetap sah (font sama) */ }
    ebkUniActive = true; ebkCpwSpr = &t;
    return;
  }
  ebkUniActive = false; ebkCpwSpr = &t;
  t.setFont(fo.f); t.setTextSize(fo.scale);
}

// font berikutnya yang tersedia (font Unicode hanya bila file .vlw ada)
static int ebkNextFont(int cur) {
  for (int k = 1; k <= EBK_NFONT; k++) { int i = (cur + k) % EBK_NFONT; if (ebkFontOk[i]) return i; }
  return cur;
}

// ---------------------------------------------------------------------
// progres baca: /books/.nyxreader  (teks, dipisah TAB)
//   S <font> <tema> <seq> <justify> <animasi> <urut>
//   B <nama> <bab> <offset> <persen> <seq>
// ---------------------------------------------------------------------
static int ebkFindProg(const char* name) {            // indeks, -1 = belum ada (tanpa tipe struct di signature)
  for (int i = 0; i < ebkProgN; i++) if (strcmp(ebkProgs[i].name, name) == 0) return i;
  return -1;
}

static void ebkLoadProgFile() {
  ebkProgN = 0;
  if (!ebkProgs) ebkProgs = (EbkProg*)ebPsAlloc(sizeof(EbkProg) * EBK_MAX_BOOKS);
  if (!ebkProgs || !sdReady) return;
  File f = SD_MMC.open(EBK_PROG, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  if (sz == 0 || sz > 64 * 1024) { f.close(); return; }
  char* buf = (char*)ebPsAlloc(sz + 1);
  if (!buf) { f.close(); return; }
  size_t got = f.read((uint8_t*)buf, sz);
  f.close();
  buf[got] = 0;
  char* line = buf;
  while (line && *line) {
    char* nl = strchr(line, '\n');
    if (nl) *nl = 0;
    char* fld[8]; int nf = 0;
    char* p = line;
    while (nf < 8) { fld[nf++] = p; char* t = strchr(p, '\t'); if (!t) break; *t = 0; p = t + 1; }
    if (nf >= 4 && fld[0][0] == 'S') {
      int fi = atoi(fld[1]), pi = atoi(fld[2]);
      if (fi >= 0 && fi < EBK_NFONT) ebkFontIdx = fi;
      if (pi >= 0 && pi < 4) ebkPalIdx = pi;
      ebkSeq = (uint32_t)strtoul(fld[3], nullptr, 10);
      if (nf >= 5) ebkJustify = atoi(fld[4]) != 0;
      if (nf >= 6) ebkAnimOn = atoi(fld[5]) != 0;
      if (nf >= 7) { int so = atoi(fld[6]); if (so >= 0 && so < 3) ebkSortMode = (uint8_t)so; }
    } else if (nf >= 6 && fld[0][0] == 'B' && ebkProgN < EBK_MAX_BOOKS) {
      EbkProg* e = &ebkProgs[ebkProgN++];
      strncpy(e->name, fld[1], EBK_NAME_LEN - 1); e->name[EBK_NAME_LEN - 1] = 0;
      e->ch = atoi(fld[2]); e->off = (uint32_t)strtoul(fld[3], nullptr, 10);
      int pc = atoi(fld[4]); e->pct = (uint8_t)(pc < 0 ? 0 : pc > 100 ? 100 : pc);
      e->seq = (uint32_t)strtoul(fld[5], nullptr, 10);
    }
    line = nl ? nl + 1 : nullptr;
  }
  free(buf);
}

static void ebkSaveProgFile() {
  if (!sdReady || !ebkProgs) return;
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  File f = SD_MMC.open(EBK_PROG_TMP, FILE_WRITE);
  if (!f) return;
  char ln[240];
  int n = snprintf(ln, sizeof(ln), "S\t%d\t%d\t%u\t%d\t%d\t%d\n", ebkFontIdx, ebkPalIdx, (unsigned)ebkSeq,
                   ebkJustify ? 1 : 0, ebkAnimOn ? 1 : 0, (int)ebkSortMode);
  f.write((const uint8_t*)ln, (size_t)n);
  for (int i = 0; i < ebkProgN; i++) {
    n = snprintf(ln, sizeof(ln), "B\t%s\t%d\t%u\t%u\t%u\n", ebkProgs[i].name, (int)ebkProgs[i].ch,
                 (unsigned)ebkProgs[i].off, (unsigned)ebkProgs[i].pct, (unsigned)ebkProgs[i].seq);
    f.write((const uint8_t*)ln, (size_t)n);
  }
  f.close();
  SD_MMC.remove(EBK_PROG);
  SD_MMC.rename(EBK_PROG_TMP, EBK_PROG);
}

// persen terbaca (0..100) untuk halaman pg
static int ebkPercentAt(uint32_t pg) {
  if (!ebkOpened || ebkPgN == 0) return 0;
  int nch = ebBookChapters(&ebkBook); if (nch < 1) nch = 1;
  int c = ebkBook.curCh < 0 ? 0 : ebkBook.curCh;
  float within = (float)(pg + 1) / (float)ebkPgN;
  int p = (int)(((float)c + within) * 100.0f / (float)nch);
  return p < 0 ? 0 : p > 100 ? 100 : p;
}
static int ebkPercent() { return ebkPercentAt(ebkPage); }

static void ebkRememberPos(bool writeFile) {
  if (!ebkOpened || !ebkProgs) return;
  int pi = ebkFindProg(ebkName);
  if (pi < 0) {
    if (ebkProgN >= EBK_MAX_BOOKS) { memmove(&ebkProgs[0], &ebkProgs[1], sizeof(EbkProg) * (EBK_MAX_BOOKS - 1)); ebkProgN = EBK_MAX_BOOKS - 1; }
    pi = ebkProgN++;
    strncpy(ebkProgs[pi].name, ebkName, EBK_NAME_LEN - 1); ebkProgs[pi].name[EBK_NAME_LEN - 1] = 0; ebkProgs[pi].seq = 0;
  }
  EbkProg* e = &ebkProgs[pi];
  if (e->seq != ebkSeq) e->seq = ebkSeq;
  e->ch = ebkBook.curCh < 0 ? 0 : ebkBook.curCh;
  e->off = (ebkPgN && ebkPage < ebkPgN) ? ebkPg[ebkPage] : 0;
  e->pct = (uint8_t)ebkPercent();
  if (writeFile) ebkSaveProgFile();
}

// ---------------------------------------------------------------------
// cache metadata: /books/.nyxmeta   M <nama> <ukuran> <judul> <pengarang> <sampul 0/1>   (dipisah TAB, ASCII)
// ---------------------------------------------------------------------
static void ebkCleanField(char* s) { for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }

static void ebkLoadMetaFile() {
  ebkMetaN = 0;
  if (!ebkMetas) ebkMetas = (EbkMeta*)ebPsAlloc(sizeof(EbkMeta) * EBK_MAX_BOOKS);
  if (!ebkMetas || !sdReady) return;
  File f = SD_MMC.open(EBK_META, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  if (sz == 0 || sz > 96 * 1024) { f.close(); return; }
  char* buf = (char*)ebPsAlloc(sz + 1);
  if (!buf) { f.close(); return; }
  size_t got = f.read((uint8_t*)buf, sz);
  f.close();
  buf[got] = 0;
  char* line = buf;
  while (line && *line && ebkMetaN < EBK_MAX_BOOKS) {
    char* nl = strchr(line, '\n');
    if (nl) *nl = 0;
    char* fld[6]; int nf = 0;
    char* p = line;
    while (nf < 6) { fld[nf++] = p; char* t = strchr(p, '\t'); if (!t) break; *t = 0; p = t + 1; }
    if (nf >= 6 && fld[0][0] == 'M') {
      EbkMeta* m = &ebkMetas[ebkMetaN++];
      strncpy(m->name, fld[1], EBK_NAME_LEN - 1); m->name[EBK_NAME_LEN - 1] = 0;
      m->size = (uint32_t)strtoul(fld[2], nullptr, 10);
      strncpy(m->title, fld[3], EBK_TITLE_LEN - 1); m->title[EBK_TITLE_LEN - 1] = 0;
      strncpy(m->author, fld[4], EBK_AUTH_LEN - 1); m->author[EBK_AUTH_LEN - 1] = 0;
      m->cover = (uint8_t)(atoi(fld[5]) != 0);
    }
    line = nl ? nl + 1 : nullptr;
  }
  free(buf);
}

static void ebkSaveMetaFile() {
  if (!sdReady || !ebkMetas || !ebkLib) return;
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  File f = SD_MMC.open(EBK_META_TMP, FILE_WRITE);
  if (!f) return;
  char ln[300];
  for (int i = 0; i < ebkLibN; i++) {
    const EbkEntry& e = ebkLib[i];
    if (!e.epub || e.meta != 1) continue;                  // hanya EPUB yang sudah diproses
    char t[EBK_TITLE_LEN], a[EBK_AUTH_LEN];
    strcpy(t, e.title); strcpy(a, e.author); ebkCleanField(t); ebkCleanField(a);
    int n = snprintf(ln, sizeof(ln), "M\t%s\t%u\t%s\t%s\t%d\n", e.name, (unsigned)e.size, t, a, e.cover ? 1 : 0);
    f.write((const uint8_t*)ln, (size_t)n);
  }
  f.close();
  SD_MMC.remove(EBK_META);
  SD_MMC.rename(EBK_META_TMP, EBK_META);
  ebkMetaDirty = false; ebkJobsSinceSave = 0;
}

// ---------------------------------------------------------------------
// sampul: thumbnail 36x48 (format piksel buffer sprite LovyanGFX = byte tertukar) disimpan per buku
// ---------------------------------------------------------------------
static void ebkCvPathOf(uint32_t key, char* out, int cap) { snprintf(out, (size_t)cap, EBK_COVDIR "/%08x.cv", (unsigned)key); }

static bool ebkCvSave(uint32_t key, const uint16_t* px) {
  if (!sdReady) return false;
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  if (!SD_MMC.exists(EBK_COVDIR)) SD_MMC.mkdir(EBK_COVDIR);
  char path[48]; ebkCvPathOf(key, path, sizeof(path));
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  static const uint8_t mg[4] = { 'N', 'C', 'V', '1' };
  bool ok = f.write(mg, 4) == 4 && f.write((const uint8_t*)px, EBK_CV_PX * 2) == (size_t)(EBK_CV_PX * 2);
  f.close();
  if (!ok) SD_MMC.remove(path);
  return ok;
}

// thumbnail untuk key: dari pool RAM, kalau belum dari SD. nullptr = tidak ada.
static uint16_t* ebkCvGet(uint32_t key) {
  if (!ebkCvPool) {
    ebkCvPool = (uint16_t*)heap_caps_malloc((size_t)EBK_CV_SLOTS * EBK_CV_PX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ebkCvPool) return nullptr;
    for (int i = 0; i < EBK_CV_SLOTS; i++) ebkCvKey[i] = 0;
    ebkCvNext = 0;
  }
  for (int i = 0; i < EBK_CV_SLOTS; i++) if (ebkCvKey[i] == key && key != 0) return ebkCvPool + (size_t)i * EBK_CV_PX;
  char path[48]; ebkCvPathOf(key, path, sizeof(path));
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  uint8_t mg[4];
  int slot = ebkCvNext; ebkCvNext = (ebkCvNext + 1) % EBK_CV_SLOTS;
  uint16_t* dst = ebkCvPool + (size_t)slot * EBK_CV_PX;
  bool ok = f.size() == 4 + EBK_CV_PX * 2 && f.read(mg, 4) == 4 && mg[0] == 'N' && mg[1] == 'C' && mg[2] == 'V' && mg[3] == '1'
            && f.read((uint8_t*)dst, EBK_CV_PX * 2) == (size_t)(EBK_CV_PX * 2);
  f.close();
  if (!ok) { ebkCvKey[slot] = 0; return nullptr; }
  ebkCvKey[slot] = key;
  return dst;
}

// ---------------------------------------------------------------------
// dekoder gambar (sampul + halaman gambar): JPEG lewat JPEGDEC (skala 1/2 1/4 1/8, rata-rata kotak),
// PNG lewat dekoder bawaan LovyanGFX. Keluaran: piksel RGB565 byte-tertukar (= buffer sprite LovyanGFX).
// ---------------------------------------------------------------------
static uint16_t* ebkAccBuf = nullptr;                    // jumlah r,g,b,cacah per piksel tujuan (4 x uint16)
static int ebkAccDW = 0, ebkAccDH = 0, ebkAccCX = 0, ebkAccCY = 0, ebkAccCW = 1, ebkAccCH = 1;

static int ebkJpgAccCb(JPEGDRAW* d) {
  if (!ebkAccBuf) return 0;
  const size_t plane = (size_t)ebkAccDW * (size_t)ebkAccDH;
  for (int r = 0; r < d->iHeight; r++) {
    int sy = d->y + r - ebkAccCY;
    if (sy < 0 || sy >= ebkAccCH) continue;
    int dy = (int)(((long)sy * ebkAccDH) / ebkAccCH);
    const uint16_t* src = d->pPixels + r * d->iWidth;
    for (int c = 0; c < d->iWidth; c++) {
      int sx = d->x + c - ebkAccCX;
      if (sx < 0 || sx >= ebkAccCW) continue;
      int dx = (int)(((long)sx * ebkAccDW) / ebkAccCW);
      uint16_t px = src[c];
      size_t o = (size_t)dy * (size_t)ebkAccDW + (size_t)dx;
      ebkAccBuf[o] += (uint16_t)((px >> 11) & 31);
      ebkAccBuf[plane + o] += (uint16_t)((px >> 5) & 63);
      ebkAccBuf[2 * plane + o] += (uint16_t)(px & 31);
      ebkAccBuf[3 * plane + o] += 1;
    }
  }
  return 1;
}

static void ebkJpegFree() {
  if (ebkJpeg.p) { ebkJpeg.p->~JPEGDEC(); free(ebkJpeg.p); ebkJpeg.p = nullptr; }
}

static bool ebkJpegDims(const uint8_t* d, uint32_t len, int* w, int* h) {
  if (!ebkJpeg->openRAM((uint8_t*)d, (int)len, ebkJpgAccCb)) return false;
  *w = ebkJpeg->getWidth(); *h = ebkJpeg->getHeight();
  ebkJpeg->close();
  return *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
}

// Dekode JPEG ke out[destW*destH] (byte tertukar). fill=true: isi penuh + potong tengah (sampul); false: seluruh gambar (skala ke destW x destH).
// minRatio = syarat minimal hasil dekode / tujuan sebelum rata-rata (1 = boleh langsung, 2 = lebih halus).
static bool ebkJpegDecode(const uint8_t* d, uint32_t len, int destW, int destH, bool fill, int minRatio, uint16_t* out) {
  if (!ebkJpeg->openRAM((uint8_t*)d, (int)len, ebkJpgAccCb)) return false;
  int iw = ebkJpeg->getWidth(), ih = ebkJpeg->getHeight();
  if (iw <= 0 || ih <= 0) { ebkJpeg->close(); return false; }
  int sh = 0, scale = 0;
  while (sh < 3 && (iw >> (sh + 1)) >= destW * minRatio && (ih >> (sh + 1)) >= destH * minRatio) sh++;
  if (sh == 1) scale = JPEG_SCALE_HALF; else if (sh == 2) scale = JPEG_SCALE_QUARTER; else if (sh == 3) scale = JPEG_SCALE_EIGHTH;
  int dw = iw >> sh, dh = ih >> sh;
  if (dw < 1 || dh < 1) { ebkJpeg->close(); return false; }
  ebkAccCX = 0; ebkAccCY = 0; ebkAccCW = dw; ebkAccCH = dh;
  if (fill) {
    if ((long)dw * destH > (long)dh * destW) { ebkAccCW = (int)((long)dh * destW / destH); ebkAccCX = (dw - ebkAccCW) / 2; }
    else { ebkAccCH = (int)((long)dw * destH / destW); ebkAccCY = (dh - ebkAccCH) / 2; }
    if (ebkAccCW < 1) ebkAccCW = 1;
    if (ebkAccCH < 1) ebkAccCH = 1;
  }
  size_t px = (size_t)destW * (size_t)destH;
  ebkAccBuf = (uint16_t*)heap_caps_calloc(px * 4, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ebkAccBuf) { ebkJpeg->close(); return false; }
  ebkAccDW = destW; ebkAccDH = destH;
  ebkJpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  int ok = ebkJpeg->decode(0, 0, scale);
  ebkJpeg->close();
  if (ok) {
    for (size_t i = 0; i < px; i++) {
      uint16_t n = ebkAccBuf[3 * px + i];
      uint16_t v = 0;
      if (n) {
        uint32_t r = ebkAccBuf[i] / n, g = ebkAccBuf[px + i] / n, b = ebkAccBuf[2 * px + i] / n;
        v = (uint16_t)((r << 11) | (g << 5) | b);
      }
      out[i] = (uint16_t)((v << 8) | (v >> 8));
    }
  }
  free(ebkAccBuf); ebkAccBuf = nullptr;
  return ok != 0;
}

// sampul PNG: skala sehingga menutup 36x48 lalu ambil bagian tengah
static bool ebkCvPng(const uint8_t* d, uint32_t len, uint16_t* out) {
  int iw = 0, ih = 0;
  if (!rpPngDims(d, len, &iw, &ih) || (long)iw * (long)ih > RP_PNG_MAX_PIXELS) return false;
  float sa = (float)EBK_CV_W / (float)iw, sb = (float)EBK_CV_H / (float)ih;
  float sc = sa > sb ? sa : sb;
  int sw = (int)((float)iw * sc + 0.5f), sh = (int)((float)ih * sc + 0.5f);
  if (sw < EBK_CV_W) sw = EBK_CV_W;
  if (sh < EBK_CV_H) sh = EBK_CV_H;
  if ((long)sw * (long)sh * 2L > 1024L * 1024L) return false;
  LGFX_Sprite t(&display);
  t.setPsram(true);
  if (!t.createSprite(sw, sh)) return false;
  t.fillSprite(0x0000);
  bool ok = t.drawPng(d, len, 0, 0, 0, 0, 0, 0, sc, sc);
  if (ok) {
    const uint16_t* src = (const uint16_t*)t.getBuffer();
    if (!src) ok = false;
    else {
      int ox = (sw - EBK_CV_W) / 2, oy = (sh - EBK_CV_H) / 2;
      for (int y = 0; y < EBK_CV_H; y++) memcpy(out + y * EBK_CV_W, src + (size_t)(oy + y) * (size_t)sw + ox, EBK_CV_W * 2);
    }
  }
  t.deleteSprite();
  return ok;
}

// buat + simpan thumbnail sampul dari data gambar. true bila tersimpan.
static bool ebkCoverFromData(const uint8_t* d, uint32_t len, uint32_t key) {
  if (len < 16) return false;
  uint16_t* out = (uint16_t*)heap_caps_malloc(EBK_CV_PX * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!out) return false;
  bool ok = false;
  if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') ok = ebkCvPng(d, len, out);
  else if (d[0] == 0xFF && d[1] == 0xD8) ok = ebkJpegDecode(d, len, EBK_CV_W, EBK_CV_H, true, 2, out);
  if (ok) ok = ebkCvSave(key, out);
  free(out);
  return ok;
}

// ---------------------------------------------------------------------
// perpustakaan: pindai, urut, saring, pekerjaan metadata/sampul
// ---------------------------------------------------------------------
static void ebkScanDir(const char* dirPath, uint8_t inRoot) {
  File dir = SD_MMC.open(dirPath);
  if (!dir) return;
  if (!dir.isDirectory()) { dir.close(); return; }
  File f = dir.openNextFile();
  while (f && ebkLibN < EBK_MAX_BOOKS) {
    if (!f.isDirectory()) {
      char nm[EBK_NAME_LEN + 8];
      ebkBaseName(f.name(), nm, sizeof(nm));
      bool isTxt = ebkEndsCI(nm, ".txt"), isEpub = ebkEndsCI(nm, ".epub"), isMd = ebkEndsCI(nm, ".md");
      if (nm[0] != '.' && (isTxt || isEpub || isMd) && strlen(nm) < EBK_NAME_LEN) {
        EbkEntry* e = &ebkLib[ebkLibN++];
        memset(e, 0, sizeof(*e));
        strcpy(e->name, nm); e->size = (uint32_t)f.size(); e->epub = isEpub ? 1 : 0; e->md = isMd ? 1 : 0; e->inRoot = inRoot;
        e->key = ebkHash(nm, e->size);
        int pi = ebkFindProg(nm);
        if (pi >= 0) { e->pct = ebkProgs[pi].pct; e->seq = ebkProgs[pi].seq; }
        if (isEpub) {                                           // metadata dari cache bila ukuran sama
          e->meta = 0;
          for (int m = 0; m < ebkMetaN; m++) {
            if (ebkMetas[m].size == e->size && strcmp(ebkMetas[m].name, nm) == 0) {
              strcpy(e->title, ebkMetas[m].title); strcpy(e->author, ebkMetas[m].author);
              e->cover = ebkMetas[m].cover; e->meta = 1; break;
            }
          }
        } else { e->meta = 1; }                                  // TXT/MD: judul = nama berkas (dilipat saat ditampilkan)
      }
    }
    f.close();
    f = dir.openNextFile();
    yield();
  }
  if (f) f.close();
  dir.close();
}

// kunci urut: (a lebih dulu dari b)?  -> mengembalikan <0, 0, >0
static int ebkCmpLib(int a, int b) {
  const EbkEntry& x = ebkLib[a]; const EbkEntry& y = ebkLib[b];
  char tx[EBK_TITLE_LEN], ty[EBK_TITLE_LEN];
  if (ebkSortMode == EBK_SORT_RECENT) {
    if (x.seq != y.seq && (x.seq || y.seq)) return x.seq > y.seq ? -1 : 1;
  } else if (ebkSortMode == EBK_SORT_AUTHOR) {
    bool ax = x.author[0] != 0, ay = y.author[0] != 0;
    if (ax != ay) return ax ? -1 : 1;                           // yang tak punya pengarang di belakang
    int c = strcasecmp(x.author, y.author);
    if (c) return c;
  }
  ebkEntryTitle(a, tx, sizeof(tx)); ebkEntryTitle(b, ty, sizeof(ty));
  int c = strcasecmp(tx, ty);
  return c ? c : strcasecmp(x.name, y.name);
}

// saring (kata kunci di judul/pengarang/nama berkas) lalu urutkan -> ebkView (indeks ke ebkLib)
static void ebkRebuildView() {
  if (!ebkView) ebkView = (int*)ebPsAlloc(sizeof(int) * EBK_MAX_BOOKS);
  ebkViewN = 0;
  strncpy(ebkQApplied, ebkQuery.c_str(), sizeof(ebkQApplied) - 1); ebkQApplied[sizeof(ebkQApplied) - 1] = 0;
  ebkViewDirty = false;
  if (!ebkView || !ebkLib) return;
  for (int i = 0; i < ebkLibN; i++) {
    if (ebkQApplied[0]) {
      char hay[EBK_TITLE_LEN + EBK_AUTH_LEN + EBK_NAME_LEN + 4], stem[EBK_NAME_LEN], fs[EBK_NAME_LEN];
      ebkStem(ebkLib[i].name, stem, sizeof(stem)); ebkFold(stem, fs, sizeof(fs));
      snprintf(hay, sizeof(hay), "%s %s %s", ebkLib[i].title, ebkLib[i].author, fs);
      if (!ebkContainsCI(hay, ebkQApplied)) continue;
    }
    ebkView[ebkViewN++] = i;
  }
  for (int i = 1; i < ebkViewN; i++) {                     // penyisipan stabil
    int v = ebkView[i], j = i - 1;
    while (j >= 0 && ebkCmpLib(ebkView[j], v) > 0) { ebkView[j + 1] = ebkView[j]; j--; }
    ebkView[j + 1] = v;
  }
}

static void ebkScan() {
  if (!ebkLib) ebkLib = (EbkEntry*)ebPsAlloc(sizeof(EbkEntry) * EBK_MAX_BOOKS);
  ebkLibN = 0;
  if (!ebkLib || !sdReady) { ebkViewN = 0; return; }
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  ebkLoadProgFile();
  if (ebkMetaN == 0) ebkLoadMetaFile();
  ebkProbeFonts();
  if (!ebkFontOk[ebkFontIdx]) ebkFontIdx = 0;
  ebkScanDir(EBK_DIR, 0);
  ebkScanDir("/", 1);
  ebkViewDirty = true;
  ebkRebuildView();
  ebkJobDue = millis() + 250UL;
}

// berapa EPUB yang metadata/sampulnya belum diproses
static int ebkJobsPending() {
  int n = 0;
  for (int i = 0; i < ebkLibN; i++) if (ebkLib[i].epub && ebkLib[i].meta == 0) n++;
  return n;
}
static int ebkEpubTotal() {
  int n = 0;
  for (int i = 0; i < ebkLibN; i++) if (ebkLib[i].epub) n++;
  return n;
}

static int ebkJobPick() {                                  // utamakan baris yang sedang terlihat
  int first = (int)(ebkScroll / (float)EBK_ROW_H);
  for (int v = first; v < ebkViewN && v < first + 9; v++) {
    int i = ebkView[v];
    if (ebkLib[i].epub && ebkLib[i].meta == 0) return i;
  }
  for (int i = 0; i < ebkLibN; i++) if (ebkLib[i].epub && ebkLib[i].meta == 0) return i;
  return -1;
}

// proses satu EPUB: judul, pengarang, sampul. Sekali per EPUB, hasil di-cache di SD.
static void ebkJobRun(int idx) {
  EbkEntry* e = &ebkLib[idx];
  e->meta = 2;                                               // anggap gagal dulu (mencegah diulang bila crash di tengah)
  char path[EBK_NAME_LEN + 16]; ebkPath(idx, path, sizeof(path));
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return;
  EbRd rd; rd.read = ebkSdRead; rd.ctx = &f; rd.size = (uint32_t)f.size();
  static EbBook jb;
  int r = ebBookOpen(&jb, rd, EB_KIND_EPUB);
  if (r == EB_OK) {
    ebkFold(jb.title, e->title, sizeof(e->title));
    ebkFold(jb.author, e->author, sizeof(e->author));
    e->cover = 0;
    if (jb.coverOff) {
      uint8_t* d = nullptr; uint32_t dl = 0;
      if (ebBookCover(&jb, &d, &dl) == EB_OK) { if (ebkCoverFromData(d, dl, e->key)) e->cover = 1; EB_FREE(d); }
    }
    e->meta = 1;
    ebBookClose(&jb);
  }
  f.close();
  ebkMetaDirty = true; ebkJobsSinceSave++;
}

// satu langkah pekerjaan latar (dipanggil dari ebookLoopPoll saat perpustakaan terbuka dan jari tidak menyentuh)
static void ebkJobTick() {
  if (ebkMode != EBK_LIB || ebkDown || !sdReady) return;
  if ((long)(millis() - ebkJobDue) < 0 || (long)(millis() - ebkLastTouchMs) < 350) return;
  int idx = ebkJobPick();
  if (idx < 0) {
    if (ebkMetaDirty) { ebkSaveMetaFile(); if (ebkSortMode != EBK_SORT_RECENT || ebkQApplied[0]) ebkViewDirty = true; needRedraw = true; }
    return;
  }
  ebkJobRun(idx);
  if (ebkJobsSinceSave >= 8) ebkSaveMetaFile();
  if (ebkJobPick() < 0 && ebkSortMode != EBK_SORT_RECENT) ebkViewDirty = true;   // urutan baru hanya setelah semua selesai (hindari baris melompat)
  ebkJobDue = millis() + 30UL;
  needRedraw = true;
}

// ---------------------------------------------------------------------
// buku terbuka: bab, tata letak, navigasi halaman
// ---------------------------------------------------------------------
static void ebkFreePages() {
  if (ebkPg) { free(ebkPg); ebkPg = nullptr; }
  ebkPgCap = 0; ebkPgN = 0; ebkPage = 0;
}

static void ebkFreeAnim() {
  if (ebkAnimA) { ebkAnimA->deleteSprite(); delete ebkAnimA; ebkAnimA = nullptr; }
  if (ebkAnimB) { ebkAnimB->deleteSprite(); delete ebkAnimB; ebkAnimB = nullptr; }
  ebkAnimActive = false; ebkAnimPending = false;
}

static void ebkFreeImg() {
  if (ebkImgSpr) { ebkImgSpr->deleteSprite(); delete ebkImgSpr; ebkImgSpr = nullptr; }
  ebkImgKey = -1; ebkImgErr[0] = 0;
}

static void ebkCloseBook() {
  ebkFreeAnim(); ebkFreeImg();
  if (ebkOpened) { ebBookClose(&ebkBook); ebkOpened = false; }
  if (ebkFile) ebkFile.close();
  ebkFreePages();
  ebkMenu = false;
  ebkCpwSpr = nullptr;
}

// Muat bab target; bab kosong dilewati searah dir (+1/-1). Mengembalikan false bila gagal/tak ada isi.
static bool ebkLoadChapter(int ch, int dir) {
  int nch = ebBookChapters(&ebkBook);
  int orig = ebkBook.curCh;
  ebkFreeImg();                                              // gambar bab lama tidak berlaku lagi
  for (int guard = 0; guard < nch && ch >= 0 && ch < nch; guard++, ch += dir) {
    int r = ebBookLoadCh(&ebkBook, ch);
    if (r != EB_OK) { showToast(ebErrStr(r)); break; }
    if (ebkBook.textLen > 0) return true;
  }
  if (orig >= 0 && orig < nch) ebBookLoadCh(&ebkBook, orig);   // gagal: kembali ke bab semula
  return false;
}

static void ebkRelayout(LGFX_Sprite& s) {
  const EbkFont& fo = ebkFonts[ebkFontIdx];
  ebkApplyFont(s);
  ebkCpwReset();
  int fh = s.fontHeight(); if (fh < 8) fh = 8;
  ebkLineH = fh + ((!ebkUniActive && fo.f == &lgfx::fonts::Font0) ? 3 : 2);
  memset(&ebkLay, 0, sizeof(ebkLay));
  char c[2] = { 0, 0 };
  for (int ch = 32; ch < 127; ch++) {
    c[0] = (char)ch;
    int w = s.textWidth(c); if (w < 1) w = 1; if (w > 250) w = 250;
    ebkLay.adv[ch] = (uint8_t)w;
  }
  ebkLay.uni = ebkUniActive ? 1 : 0; ebkLay.cpw = ebkCpw;
  ebkLayW = SCR_W; ebkLayH = SCR_H;
  ebkLay.width = (int16_t)(SCR_W - 16);
  ebkLay.indent = (int16_t)(ebkLay.adv[' '] * 2 + 4);
  int area = SCR_H - 8 - 18;                               // atas 8 px, bawah 18 px (info + zona gestur)
  ebkLay.rows = (int16_t)(area / ebkLineH); if (ebkLay.rows < 1) ebkLay.rows = 1;

  uint32_t keep = (ebkPgN && ebkPage < ebkPgN) ? ebkPg[ebkPage] : 0;
  ebkPgN = ebPaginate(ebkBook.text, ebkBook.textLen, &ebkLay, &ebkPg, &ebkPgCap);
  if (ebkWantLast) { ebkPage = ebkPgN ? ebkPgN - 1 : 0; ebkWantLast = false; }
  else if (ebkWantOff != 0xFFFFFFFFu) { ebkPage = ebPageOf(ebkPg, ebkPgN, ebkWantOff); ebkWantOff = 0xFFFFFFFFu; }
  else ebkPage = ebPageOf(ebkPg, ebkPgN, keep);
  if (ebkPage >= ebkPgN) ebkPage = ebkPgN ? ebkPgN - 1 : 0;
  ebkDirty = false;
  ebkFreeAnim();                                             // tata letak berubah: animasi lama tak berlaku
}

static void ebkGoChapter(int ch, int dir, uint32_t off, bool last) {
  if (ebkLoadChapter(ch, dir)) {
    ebkWantOff = last ? 0xFFFFFFFFu : off; ebkWantLast = last;
    ebkDirty = true; ebkPage = 0; ebkPgN = 0;
  }
  needRedraw = true;
}

static void ebkTurned() {
  if (++ebkTurns >= 10) { ebkTurns = 0; ebkRememberPos(true); }
  needRedraw = true;
}

// minta animasi geser dari halaman 'from' ke halaman sekarang (dir +1 = maju). Hanya dalam satu bab dan saat menu tertutup.
static void ebkAnimRequest(int dir, uint32_t from) {
  if (!ebkAnimOn || ebkMenu || ebkAnimActive || ebkDirty) return;
  ebkAnimPending = true; ebkAnimDir = dir; ebkAnimFrom = from;
}

static void ebkNext() {
  if (ebkAnimActive) return;                                 // tunggu animasi selesai
  if (ebkPage + 1 < ebkPgN) { uint32_t from = ebkPage; ebkPage++; ebkAnimRequest(+1, from); ebkTurned(); return; }
  int nch = ebBookChapters(&ebkBook);
  if (ebkBook.curCh + 1 < nch) {
    int before = ebkBook.curCh;
    ebkGoChapter(ebkBook.curCh + 1, +1, 0, false);
    if (ebkBook.curCh == before) showToast("Akhir buku");
    else ebkTurned();
  } else showToast("Akhir buku");
}

static void ebkPrev() {
  if (ebkAnimActive) return;
  if (ebkPage > 0) { uint32_t from = ebkPage; ebkPage--; ebkAnimRequest(-1, from); ebkTurned(); return; }
  if (ebkBook.curCh > 0) {
    int before = ebkBook.curCh;
    ebkGoChapter(ebkBook.curCh - 1, -1, 0, true);
    if (ebkBook.curCh == before) showToast("Awal buku");
    else ebkTurned();
  } else showToast("Awal buku");
}

// ---------------------------------------------------------------------
// buka buku (dua tahap supaya layar sempat menampilkan "Membuka...")
// ---------------------------------------------------------------------
static void ebkStartOpen(int idx) {
  ebkPendingIdx = idx;
  ebkMode = EBK_LOADING;
  if (kbVisible && kbTarget == &ebkQuery) { kbVisible = false; kbTarget = nullptr; }
  ebkLoadDue = millis() + 120UL;
  needRedraw = true;
}

static void ebkDoOpen() {
  int idx = ebkPendingIdx; ebkPendingIdx = -1;
  if (idx < 0 || idx >= ebkLibN) { ebkMode = EBK_LIB; needRedraw = true; return; }
  char path[EBK_NAME_LEN + 16];
  ebkPath(idx, path, sizeof(path));
  ebkCloseBook();
  ebkFile = SD_MMC.open(path, FILE_READ);
  if (!ebkFile) { showToast("Gagal membuka file"); ebkMode = EBK_LIB; needRedraw = true; return; }
  EbRd rd; rd.read = ebkSdRead; rd.ctx = &ebkFile; rd.size = (uint32_t)ebkFile.size();
  int r = ebBookOpen(&ebkBook, rd, ebkLib[idx].epub ? EB_KIND_EPUB : ebkLib[idx].md ? EB_KIND_MD : EB_KIND_TXT);
  if (r != EB_OK) {
    ebkFile.close();
    showToast(ebErrStr(r));
    ebkMode = EBK_LIB; needRedraw = true; return;
  }
  ebkOpened = true;
  strncpy(ebkName, ebkLib[idx].name, EBK_NAME_LEN - 1); ebkName[EBK_NAME_LEN - 1] = 0;
  if (ebkBook.title[0] == 0) ebkStem(ebkName, ebkBook.title, sizeof(ebkBook.title));
  if (ebkBook.truncated) showToast(ebkBook.kind == EB_KIND_EPUB ? "Terpotong" : "Teks dipotong di batas ukuran", 2200);

  int ch = 0; uint32_t off = 0;
  int pi = ebkFindProg(ebkName);
  if (pi >= 0) { ch = ebkProgs[pi].ch; off = ebkProgs[pi].off; }
  int nch = ebBookChapters(&ebkBook);
  if (ch < 0 || ch >= nch) { ch = 0; off = 0; }
  bool ok = true;
  if (ebkBook.kind == EB_KIND_EPUB) ok = ebkLoadChapter(ch, +1) || ebkLoadChapter(0, +1);
  if (!ok || ebkBook.text == nullptr || ebkBook.textLen == 0) {
    showToast("Buku tidak punya isi yang bisa dibaca");
    ebkCloseBook(); ebkMode = EBK_LIB; needRedraw = true; return;
  }
  ebkWantOff = off; ebkWantLast = false; ebkDirty = true; ebkPgN = 0; ebkPage = 0;
  ebkSeq++;
  ebkTurns = 0; ebkMenu = false; ebkMode = EBK_READ;
  ebkRememberPos(true);
  needRedraw = true;
}

static void ebkBackToLibrary() {
  ebkRememberPos(true);
  ebkCloseBook();
  ebkVlwFree();
  ebkScan();
  ebkScroll = 0;
  ebkMode = EBK_LIB;
  needRedraw = true;
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
static void ebkFreeAll() {
  gutExit();                                                 // batalkan unduhan Gutenberg + lepas daftarnya
  ebkCloseBook();
  ebkVlwFree();
  ebkJpegFree();
  if (ebkMetaDirty) ebkSaveMetaFile();
  if (ebkLib) { free(ebkLib); ebkLib = nullptr; } ebkLibN = 0;
  if (ebkProgs) { free(ebkProgs); ebkProgs = nullptr; } ebkProgN = 0;
  if (ebkMetas) { free(ebkMetas); ebkMetas = nullptr; } ebkMetaN = 0;
  if (ebkView) { free(ebkView); ebkView = nullptr; } ebkViewN = 0;
  if (ebkCvPool) { free(ebkCvPool); ebkCvPool = nullptr; }
  if (kbVisible && kbTarget == &ebkQuery) { kbVisible = false; kbTarget = nullptr; }
  ebkQuery = ""; ebkQApplied[0] = 0;
}

void ebookEnter() {
  ebkMode = EBK_LIB; ebkScroll = 0; ebkMenu = false; ebkDown = false; ebkPendingIdx = -1;
  ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; ebkDirty = true;
  ebkAnimPending = false; ebkAnimActive = false;
  ebKeepUni = 1;                                             // teks kanonik menyimpan UTF-8; dilipat saat tampil bila font tak punya glyph
  ebkQuery = ""; ebkQApplied[0] = 0; ebkMetaN = 0; ebkMetaDirty = false; ebkJobsSinceSave = 0;
  ebkScan();
}

void ebookExit() {
  if (ebkOpened) ebkRememberPos(true);
  ebkFreeAll();
  ebKeepUni = 0;
  ebkMode = EBK_LIB; ebkDown = false; ebkPendingIdx = -1;
}

// dipanggil dari loop(): membuka buku setelah layar "Membuka..." tampil + pekerjaan latar perpustakaan
void ebookLoopPoll() {
  if (ebkMode == EBK_GUT) { gutLoopPoll(); return; }
  if (ebkMode == EBK_LOADING && ebkPendingIdx >= 0 && (long)(millis() - ebkLoadDue) >= 0) ebkDoOpen();
  else if (ebkMode == EBK_LIB && ebkLib) ebkJobTick();
}

// ---------------------------------------------------------------------
// geometri menu pembaca (panel bawah: slider + dua baris tombol)
// ---------------------------------------------------------------------
#define EBK_PANEL_H 112
static int ebkPanelY()   { return SCR_H - EBK_PANEL_H - 18; }
static int ebkSliderX0() { return 16; }
static int ebkSliderX1() { return SCR_W - 16; }
static int ebkSliderY()  { return ebkPanelY() + 14; }
static int ebkBtnW(int row)       { return row == 0 ? (SCR_W - 16 - 3 * 6) / 4 : (SCR_W - 16 - 6) / 2; }
static int ebkBtnX(int row, int i){ return 8 + i * (ebkBtnW(row) + 6); }
static int ebkBtnY(int row)       { return ebkPanelY() + 44 + row * 34; }

static bool ebkInSlider(int x, int y) {
  return y >= ebkSliderY() - 12 && y <= ebkSliderY() + 14 && x >= ebkSliderX0() - 10 && x <= ebkSliderX1() + 10;
}

static void ebkSliderSet(int x) {
  if (ebkPgN < 2) return;
  int a = ebkSliderX0(), b = ebkSliderX1();
  float f = (float)(x - a) / (float)(b - a);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  uint32_t pg = (uint32_t)(f * (float)(ebkPgN - 1) + 0.5f);
  if (pg != ebkPage) { ebkPage = pg; needRedraw = true; }
}

// ---------------------------------------------------------------------
// halaman gambar (gambar dalam bab EPUB)
// ---------------------------------------------------------------------
static void ebkImgLoad(int imgIdx, int aw, int ah, int key) {
  ebkFreeImg();
  ebkImgKey = key; ebkImgW = 0; ebkImgH = 0; ebkImgErr[0] = 0;
  uint8_t* d = nullptr; uint32_t dl = 0;
  if (ebBookImage(&ebkBook, imgIdx, &d, &dl) != EB_OK) { strcpy(ebkImgErr, "Gambar tidak terbaca"); return; }
  int iw = 0, ih = 0; bool png = false;
  if (dl >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {
    png = true;
    if (!rpPngDims(d, dl, &iw, &ih)) { strcpy(ebkImgErr, "PNG rusak"); free(d); return; }
    if ((long)iw * (long)ih > RP_PNG_MAX_PIXELS) { strcpy(ebkImgErr, "PNG terlalu besar"); free(d); return; }
  } else if (dl >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
    if (!ebkJpegDims(d, dl, &iw, &ih)) { strcpy(ebkImgErr, "JPEG tidak didukung"); free(d); return; }
  } else { strcpy(ebkImgErr, "Format gambar tidak didukung"); free(d); return; }
  float sc = (float)aw / (float)iw, sb = (float)ah / (float)ih;
  if (sb < sc) sc = sb;
  if (sc > 1.0f) sc = 1.0f;
  int dw = (int)((float)iw * sc + 0.5f), dh = (int)((float)ih * sc + 0.5f);
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;
  ebkImgSpr = new LGFX_Sprite(&display);
  ebkImgSpr->setPsram(true);
  if (!ebkImgSpr->createSprite(dw, dh)) { delete ebkImgSpr; ebkImgSpr = nullptr; strcpy(ebkImgErr, "RAM tidak cukup"); free(d); return; }
  bool ok;
  if (png) { ebkImgSpr->fillSprite(ebkPals[ebkPalIdx].bg); ok = ebkImgSpr->drawPng(d, dl, 0, 0, 0, 0, 0, 0, sc, sc); }
  else ok = ebkJpegDecode(d, dl, dw, dh, false, 1, (uint16_t*)ebkImgSpr->getBuffer());
  free(d);
  if (!ok) { ebkImgSpr->deleteSprite(); delete ebkImgSpr; ebkImgSpr = nullptr; strcpy(ebkImgErr, "Gagal membaca gambar"); return; }
  ebkImgW = dw; ebkImgH = dh;
}

static void ebkDrawImagePage(LGFX_Sprite& t, int imgIdx, int palIdx) {
  const EbkPal& pal = ebkPals[palIdx];
  int ax = 4, ay = 4, aw = SCR_W - 8, ah = SCR_H - 8 - 20;
  int key = ebkBook.curCh * 4096 + imgIdx + (SCR_W > SCR_H ? 1000000 : 0);
  if (ebkImgKey != key) ebkImgLoad(imgIdx, aw, ah, key);
  if (ebkImgSpr && ebkImgW > 0) {
    ebkImgSpr->pushSprite(&t, ax + (aw - ebkImgW) / 2, ay + (ah - ebkImgH) / 2);
  } else {
    t.unloadFont(); t.setFont(&lgfx::fonts::Font0); t.setTextSize(1);
    const char* m = ebkImgErr[0] ? ebkImgErr : "Gambar";
    t.setTextColor(pal.sub);
    t.setCursor((SCR_W - t.textWidth("[gambar]")) / 2, SCR_H / 2 - 12); t.print("[gambar]");
    t.setCursor((SCR_W - t.textWidth(m)) / 2, SCR_H / 2 + 2); t.print(m);
  }
}

// ---------------------------------------------------------------------
// gambar: halaman teks (dipakai layar utama dan sprite animasi)
// ---------------------------------------------------------------------
// satu baris teks; justify bila boleh (kata-kata digambar satu per satu dengan jarak tambahan)
static void ebkDrawTextLine(LGFX_Sprite& t, const char* tx, uint32_t ls, uint32_t le, int x, int y, int natW, bool head, bool endPara, bool split, int palIdx) {
  const EbkPal& pal = ebkPals[palIdx];
  char buf[512]; int wpx = 0;
  t.setTextColor(head ? pal.head : pal.text);
  if (ebkJustify && !head && !endPara && !split) {
    int target = ebkLay.width - (x - 8);
    int extra = target - natW, spw = ebkLay.adv[' '], gaps = 0;
    for (uint32_t k = ls; k < le; k++) if (tx[k] == ' ') gaps++;
    if (gaps > 0 && extra > 0 && extra <= gaps * (spw * 2 + 2)) {          // terlalu renggang -> biarkan rata kiri
      int base = extra / gaps, rem = extra % gaps, acc = 0, cx = x;
      uint32_t k = ls;
      while (k < le) {
        uint32_t w0 = k; while (k < le && tx[k] != ' ') k++;
        ebRunText(&ebkLay, tx, w0, k, buf, sizeof(buf), &wpx);
        t.setCursor(cx, y); t.print(buf);
        cx += wpx;
        if (k < le) {
          int add = base; acc += rem; if (acc >= gaps) { acc -= gaps; add++; }
          cx += spw + add; k++;
        }
      }
      return;
    }
  }
  ebRunText(&ebkLay, tx, ls, le, buf, sizeof(buf), &wpx);
  t.setCursor(x, y); t.print(buf);
}

static void ebkRenderPage(LGFX_Sprite& t, uint32_t pg) {
  const EbkPal& pal = ebkPals[ebkPalIdx];
  t.fillRect(0, 0, SCR_W, SCR_H, pal.bg);
  ebkApplyFont(t);
  t.setTextWrap(false);
  if (ebkPgN > 0 && pg < ebkPgN) {
    const char* tx = ebkBook.text; uint32_t len = ebkBook.textLen;
    uint32_t pos = ebkPg[pg];
    EbLine ln;
    if (tx[pos] == 2) {
      if (ebLine(tx, len, pos, false, &ebkLay, &ln) && ln.img >= 0) ebkDrawImagePage(t, ln.img, ebkPalIdx);
    } else {
      bool head = ebParaIsHead(tx, len, pos);
      for (int r = 0; r < ebkLay.rows; r++) {
        if (pos < len && tx[pos] == 2) break;
        if (!ebLine(tx, len, pos, head, &ebkLay, &ln)) break;
        if (!ln.empty) ebkDrawTextLine(t, tx, ln.start, ln.end, 8 + ln.x, 8 + r * ebkLineH, ln.w, ln.head != 0, ln.endPara != 0, ln.split != 0, ebkPalIdx);
        pos = ln.next; head = ln.nextHead != 0;
      }
    }
  }
  // garis info bawah (selalu Font0)
  t.unloadFont();
  t.setFont(&lgfx::fonts::Font0); t.setTextSize(1);
  t.setTextColor(pal.sub);
  char info[40]; snprintf(info, sizeof(info), "%u/%u  %d%%", (unsigned)pg + 1, (unsigned)ebkPgN, ebkPercentAt(pg));
  t.setCursor(SCR_W - 8 - t.textWidth(info), SCR_H - 15); t.print(info);
  char tt[64];
  if (ebkBook.kind == EB_KIND_EPUB && ebkBook.curCh >= 0 && ebkBook.curCh < ebkBook.nSp) ebkFold(ebPoolStr(&ebkBook, ebkBook.sp[ebkBook.curCh].titleOff), tt, sizeof(tt));
  else ebkFold(ebkBook.title, tt, sizeof(tt));
  ebkFitText(t, tt, SCR_W - 24 - t.textWidth(info));
  t.setCursor(8, SCR_H - 15); t.print(tt);
}

static void ebkDrawMenu(LGFX_Sprite& s, int palIdx) {
  const EbkPal& pal = ebkPals[palIdx];
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  uint16_t panel = blend565(pal.bg, pal.text, 38), line = blend565(pal.bg, pal.text, 90);
  // bilah atas: judul buku / bab
  s.fillRect(0, 0, SCR_W, 28, panel);
  char t[96];
  ebkFold(ebkBook.title, t, sizeof(t));
  ebkFitText(s, t, SCR_W - 20);
  s.setTextColor(pal.text); s.setCursor(10, 5); s.print(t);
  if (ebkBook.kind == EB_KIND_EPUB && ebkBook.curCh >= 0 && ebkBook.curCh < ebkBook.nSp) {
    ebkFold(ebPoolStr(&ebkBook, ebkBook.sp[ebkBook.curCh].titleOff), t, sizeof(t));
    ebkFitText(s, t, SCR_W - 20);
    s.setTextColor(pal.sub); s.setCursor(10, 16); s.print(t);
  }
  // panel bawah
  int py = ebkPanelY();
  nyxAaRR(s, 4, py, SCR_W - 8, EBK_PANEL_H, 10, panel);
  int x0 = ebkSliderX0(), x1 = ebkSliderX1(), sy = ebkSliderY();
  nyxAaRR(s, x0, sy - 2, x1 - x0, 4, 2, line);
  float f = ebkPgN > 1 ? (float)ebkPage / (float)(ebkPgN - 1) : 0.0f;
  int kx = x0 + (int)((x1 - x0) * f);
  nyxAaRR(s, x0, sy - 2, kx - x0, 4, 2, pal.head);
  nyxAaCircle(s, kx, sy, 6, pal.head);
  char info[64];
  if (ebkBook.kind == EB_KIND_EPUB)
    snprintf(info, sizeof(info), "Bab %d/%d   Hal %u/%u   %d%%", ebkBook.curCh + 1, ebBookChapters(&ebkBook), (unsigned)ebkPage + 1, (unsigned)ebkPgN, ebkPercent());
  else
    snprintf(info, sizeof(info), "Hal %u/%u   %d%%", (unsigned)ebkPage + 1, (unsigned)ebkPgN, ebkPercent());
  s.setTextColor(pal.text);
  s.setCursor((SCR_W - s.textWidth(info)) / 2, sy + 12); s.print(info);
  const char* lb[4] = { "Isi", "Aa", "Tema", "Tutup" };
  for (int i = 0; i < 4; i++) {
    int bx = ebkBtnX(0, i), by = ebkBtnY(0), bw = ebkBtnW(0);
    nyxAaRR(s, bx, by, bw, 26, 8, i == 3 ? blend565(pal.bg, 0xF800, 110) : line);
    s.setTextColor(pal.text);
    s.setCursor(bx + (bw - s.textWidth(lb[i])) / 2, by + 9); s.print(lb[i]);
  }
  char l1[24], l2[24];
  snprintf(l1, sizeof(l1), "Rata: %s", ebkJustify ? "Ya" : "Tdk");
  snprintf(l2, sizeof(l2), "Animasi: %s", ebkAnimOn ? "Ya" : "Tdk");
  const char* l2p[2] = { l1, l2 };
  for (int i = 0; i < 2; i++) {
    int bx = ebkBtnX(1, i), by = ebkBtnY(1), bw = ebkBtnW(1);
    nyxAaRR(s, bx, by, bw, 26, 8, line);
    s.setTextColor(pal.text);
    s.setCursor(bx + (bw - s.textWidth(l2p[i])) / 2, by + 9); s.print(l2p[i]);
  }
}

// mulai animasi: dua sprite PSRAM (halaman lama + baru). Gagal alokasi -> tanpa animasi.
static void ebkAnimBegin() {
  ebkAnimPending = false;
  if (ebkAnimFrom >= ebkPgN || ebkAnimFrom == ebkPage) return;
  ebkAnimA = new LGFX_Sprite(&display); ebkAnimB = new LGFX_Sprite(&display);
  ebkAnimA->setPsram(true); ebkAnimB->setPsram(true);
  if (!ebkAnimA->createSprite(SCR_W, SCR_H) || !ebkAnimB->createSprite(SCR_W, SCR_H)) { ebkFreeAnim(); return; }
  ebkRenderPage(*ebkAnimA, ebkAnimFrom);
  ebkRenderPage(*ebkAnimB, ebkPage);
  ebkAnimActive = true; ebkAnimT0 = millis();
}

static void ebkDrawReader(LGFX_Sprite& s) {
  const EbkPal& pal = ebkPals[ebkPalIdx];
  if (!ebkOpened || !ebkBook.text) { s.fillRect(0, 0, SCR_W, SCR_H, pal.bg); drawToast(s); return; }
  if (ebkDirty || ebkLayW != SCR_W || ebkLayH != SCR_H) {
    if (!ebkDirty) { ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; }
    ebkRelayout(s);
  }
  if (ebkAnimPending) { if (!ebkMenu) ebkAnimBegin(); else ebkAnimPending = false; }
  if (ebkAnimActive) {
    float p = (float)(millis() - ebkAnimT0) / (float)EBK_ANIM_MS;
    if (p < 1.0f && ebkAnimA && ebkAnimB) {
      float e = 1.0f - (1.0f - p) * (1.0f - p);
      int off = (int)(e * (float)SCR_W);
      if (ebkAnimDir > 0) { ebkAnimA->pushSprite(&s, -off, 0); ebkAnimB->pushSprite(&s, SCR_W - off, 0); }
      else                { ebkAnimA->pushSprite(&s, off, 0);  ebkAnimB->pushSprite(&s, off - SCR_W, 0); }
      needRedraw = true;                                        // frame berikutnya
      return;
    }
    ebkFreeAnim();                                              // selesai -> gambar biasa di bawah
    ebkCpwSpr = nullptr;
  }
  ebkRenderPage(s, ebkPage);
  if (!ebkUniActive && ebkFonts[ebkFontIdx].vlw) { showToast("Font Unicode tidak bisa dimuat, pakai font biasa", 2200); }
  if (ebkMenu) ebkDrawMenu(s, ebkPalIdx);
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);   // toast selalu Font0
  drawToast(s);
}

// ---------------------------------------------------------------------
// gambar: perpustakaan
// ---------------------------------------------------------------------
static bool ebkQueryBox() { return (kbVisible && kbTarget == &ebkQuery) || ebkQApplied[0]; }
static int ebkListTop() { return 52; }
static int ebkListBot() { return (kbVisible && kbTarget == &ebkQuery) ? kbY() - 4 : SCR_H - BACK_H - 3 - 6; }

// area sentuh header perpustakaan
#define EBK_SORT_W 66
static bool ebkHitSort(int x, int y)   { return y >= 28 && y <= 50 && x >= SCR_W - 10 - EBK_SORT_W && x <= SCR_W - 10; }
#define EBK_GUT_W 66
static bool ebkHitGut(int x, int y)    { int bx = SCR_W - 10 - EBK_SORT_W - 6 - 28; return y >= 28 && y <= 50 && x >= bx - 6 - EBK_GUT_W && x < bx - 2; }
static bool ebkHitSearch(int x, int y) { return y >= 28 && y <= 50 && x >= SCR_W - 10 - EBK_SORT_W - 6 - 28 && x < SCR_W - 10 - EBK_SORT_W - 2; }
static bool ebkHitBox(int x, int y)    { return y >= 26 && y <= 52 && x >= 84 && x <= SCR_W - 6; }
static bool ebkHitClear(int x, int y)  { return ebkHitBox(x, y) && x >= SCR_W - 10 - 26; }

static void ebkClampScroll(int contentH) {
  int view = ebkListBot() - ebkListTop();
  int mx = contentH - view; if (mx < 0) mx = 0;
  if (ebkScroll < 0) ebkScroll = 0;
  if (ebkScroll > (float)mx) ebkScroll = (float)mx;
}

static void ebkDrawCover(LGFX_Sprite& s, int x, int y, int libIdx) {
  const EbkEntry& e = ebkLib[libIdx];
  uint16_t* px = e.cover ? ebkCvGet(e.key) : nullptr;
  if (px) { s.pushImage(x, y, EBK_CV_W, EBK_CV_H, (lgfx::swap565_t*)px); return; }
  static const uint16_t cols[6] = { 0x3A8B, 0xB2A5, 0x4D8A, 0x8B4F, 0xC5A3, 0x5B2D };
  nyxAaRR(s, x, y, EBK_CV_W, EBK_CV_H, 4, cols[e.key % 6]);
  char t[EBK_TITLE_LEN]; ebkEntryTitle(libIdx, t, sizeof(t));
  char ch[2] = { t[0] >= 'a' && t[0] <= 'z' ? (char)(t[0] - 32) : (t[0] ? t[0] : '?'), 0 };
  s.setTextSize(2); s.setTextColor(0xFFFF);
  s.setCursor(x + (EBK_CV_W - s.textWidth(ch)) / 2, y + (EBK_CV_H - 14) / 2); s.print(ch);
  s.setTextSize(1);
}

static void ebkDrawLibrary(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Buku");
  s.unloadFont();
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  if (ebkViewDirty || strcmp(ebkQuery.c_str(), ebkQApplied) != 0) ebkRebuildView();
  int top = ebkListTop(), bot = ebkListBot();
  // ---- kepala: cari / urut ----
  uint16_t chip = blend565(T().bg, T().text, 28);
  if (ebkQueryBox()) {
    nyxAaRR(s, 84, 29, SCR_W - 84 - 10, 20, 10, chip);
    char q[44]; strncpy(q, ebkQuery.c_str(), sizeof(q) - 1); q[sizeof(q) - 1] = 0;
    ebkFitText(s, q, SCR_W - 84 - 10 - 40);
    s.setTextColor(q[0] ? T().text : T().subtext);
    s.setCursor(96, 35); s.print(q[0] ? q : "cari judul / penulis");
    s.setTextColor(T().subtext); s.setCursor(SCR_W - 10 - 16, 35); s.print("x");
  } else {
    int sx = SCR_W - 10 - EBK_SORT_W;
    nyxAaRR(s, sx, 29, EBK_SORT_W, 20, 10, chip);
    const char* sn = ebkSortNames[ebkSortMode];
    s.setTextColor(T().text); s.setCursor(sx + (EBK_SORT_W - s.textWidth(sn)) / 2, 35); s.print(sn);
    int bx = sx - 6 - 28;
    { int gx = bx - 6 - EBK_GUT_W;                                                       // tombol Gutenberg (unduh buku gratis)
      nyxAaRR(s, gx, 29, EBK_GUT_W, 20, 10, T().accent);
      s.setTextColor(0xFFFF); s.setCursor(gx + (EBK_GUT_W - s.textWidth("Gutenberg")) / 2, 35); s.print("Gutenberg"); }
    nyxAaRR(s, bx, 29, 28, 20, 10, chip);
    nyxAaCircle(s, bx + 12, 38, 5, T().text); nyxAaCircle(s, bx + 12, 38, 3, chip);          // kaca pembesar
    for (int k = 0; k < 4; k++) nyxAaCircle(s, bx + 16 + k, 42 + k - 1, 1, T().text);
  }
  int pend = ebkJobsPending();
  if (pend > 0 && ebkEpubTotal() > 0) {
    int tot = ebkEpubTotal();
    s.fillRect(0, 50, SCR_W * (tot - pend) / tot, 2, T().accent);                       // kemajuan menyiapkan sampul
  }
  // ---- daftar ----
  s.setTextSize(1);
  if (!sdReady) {
    s.setTextColor(T().subtext); s.setCursor(12, top + 10); s.print("SD card tidak terdeteksi.");
  } else if (ebkLibN == 0) {
    s.setTextColor(T().text);    s.setCursor(12, top + 10); s.print("Belum ada buku.");
    s.setTextColor(T().subtext); s.setCursor(12, top + 26); s.print("Salin file .txt / .md / .epub ke");
    s.setCursor(12, top + 38); s.print("folder /books pada SD card.");
    s.setTextColor(T().accent);  s.setCursor(12, top + 56); s.print("Atau ketuk Gutenberg di atas untuk");
    s.setCursor(12, top + 68); s.print("mengunduh buku gratis.");
  } else if (ebkViewN == 0) {
    s.setTextColor(T().subtext); s.setCursor(12, top + 10); s.print("Tidak ada hasil.");
  } else {
    ebkClampScroll(ebkViewN * EBK_ROW_H);
    s.setClipRect(0, top, SCR_W, bot - top);
    int y0 = top - (int)ebkScroll;
    for (int v = 0; v < ebkViewN; v++) {
      int ry = y0 + v * EBK_ROW_H;
      if (ry + EBK_ROW_H < top || ry > bot) continue;
      int i = ebkView[v];
      const EbkEntry& e = ebkLib[i];
      ebkDrawCover(s, 10, ry + 5, i);
      int tx = 10 + EBK_CV_W + 8, rightW = (e.pct == 100) ? 52 : (e.pct > 0 ? 30 : 0);
      char nm[EBK_TITLE_LEN]; ebkEntryTitle(i, nm, sizeof(nm));
      ebkFitText(s, nm, SCR_W - tx - 12 - rightW);
      s.setTextColor(T().text); s.setCursor(tx, ry + 8); s.print(nm);
      if (e.author[0]) {
        char au[EBK_AUTH_LEN]; strcpy(au, e.author);
        ebkFitText(s, au, SCR_W - tx - 12);
        s.setTextColor(T().subtext); s.setCursor(tx, ry + 22); s.print(au);
      }
      char sz[16], info[40]; ebkFmtSize(sz, sizeof(sz), e.size);
      snprintf(info, sizeof(info), "%s  %s", e.epub ? "EPUB" : (e.md ? "MD" : "TXT"), sz);
      s.setTextColor(T().subtext); s.setCursor(tx, ry + 36); s.print(info);
      if (e.pct == 100) {
        int pw = s.textWidth("Selesai") + 10;
        nyxAaRR(s, SCR_W - 12 - pw, ry + 5, pw, 15, 7, T().accent);
        s.setTextColor(0xFFFF); s.setCursor(SCR_W - 12 - pw + 5, ry + 9); s.print("Selesai");
      } else if (e.pct > 0) {
        char pc[8]; snprintf(pc, sizeof(pc), "%u%%", (unsigned)e.pct);
        s.setTextColor(T().accent); s.setCursor(SCR_W - 12 - s.textWidth(pc), ry + 8); s.print(pc);
      }
      if (e.pct > 0 && e.pct < 100) {
        int bw = (SCR_W - tx - 12) * e.pct / 100;
        s.fillRect(tx, ry + EBK_ROW_H - 5, SCR_W - tx - 12, 2, T().divider);
        s.fillRect(tx, ry + EBK_ROW_H - 5, bw, 2, T().accent);
      } else s.drawFastHLine(tx, ry + EBK_ROW_H - 4, SCR_W - tx - 12, T().divider);
    }
    s.clearClipRect();
    int contentH = ebkViewN * EBK_ROW_H, view = bot - top;
    if (contentH > view) {
      int th = view * view / contentH; if (th < 12) th = 12;
      int mx = contentH - view;
      int ty = top + (int)((view - th) * (ebkScroll / (float)mx));
      nyxAaRR(s, SCR_W - 4, ty, 2, th, 1, blend565(T().bg, T().text, 90));
    }
  }
  if (kbVisible && kbTarget == &ebkQuery) drawKb(s); else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// gambar: daftar isi
// ---------------------------------------------------------------------
static int ebkTocCurrent() {
  if (!ebkOpened || ebkBook.nToc <= 0) return -1;
  if (ebkBook.kind == EB_KIND_EPUB) return ebkBook.curCh;
  uint32_t off = (ebkPgN && ebkPage < ebkPgN) ? ebkPg[ebkPage] : 0;
  int cur = -1;
  for (int i = 0; i < ebkBook.nToc; i++) if (ebkBook.toc[i].off <= off) cur = i;
  return cur;
}

static void ebkDrawToc(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Daftar isi");
  s.unloadFont();
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  int top = ebkListTop(), bot = ebkListBot();
  int n = ebkBook.nToc;
  ebkClampScroll(n * EBK_TOC_ROW_H);
  int cur = ebkTocCurrent();
  s.setClipRect(0, top, SCR_W, bot - top);
  int y0 = top - (int)ebkScroll;
  for (int i = 0; i < n; i++) {
    int ry = y0 + i * EBK_TOC_ROW_H;
    if (ry + EBK_TOC_ROW_H < top || ry > bot) continue;
    if (i == cur) nyxAaRR(s, 6, ry + 1, SCR_W - 12, EBK_TOC_ROW_H - 2, 6, blend565(T().bg, T().accent, 70));
    char t[96]; ebkFold(ebPoolStr(&ebkBook, ebkBook.toc[i].titleOff), t, sizeof(t));
    ebkFitText(s, t, SCR_W - 28);
    s.setTextColor(i == cur ? T().text : T().subtext);
    s.setCursor(14, ry + 8); s.print(t);
  }
  s.clearClipRect();
  drawBack(s);
  drawToast(s);
}

static void ebkDrawLoading(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Buku");
  s.setFont(&lgfx::fonts::Font0);
  char nm[EBK_TITLE_LEN] = "";
  if (ebkPendingIdx >= 0 && ebkPendingIdx < ebkLibN) ebkEntryTitle(ebkPendingIdx, nm, sizeof(nm));
  s.setTextSize(1);
  ebkFitText(s, nm, SCR_W - 24);
  s.setTextColor(T().text);    s.setCursor((SCR_W - s.textWidth(nm)) / 2, SCR_H / 2 - 12); s.print(nm);
  s.setTextColor(T().subtext); s.setCursor((SCR_W - s.textWidth("Membuka...")) / 2, SCR_H / 2 + 4); s.print("Membuka...");
  drawToast(s);
}

void drawEbook(LGFX_Sprite& s) {
  switch (ebkMode) {
    case EBK_LOADING: ebkDrawLoading(s); break;
    case EBK_READ:    ebkDrawReader(s);  break;
    case EBK_TOC:     ebkDrawToc(s);     break;
    case EBK_GUT:     gutDraw(s);        break;
    default:          ebkDrawLibrary(s); break;
  }
}

// ---------------------------------------------------------------------
// sentuh
// ---------------------------------------------------------------------
// Sentuhan generik (jari baru menyentuh): Kembali, kepala perpustakaan (cari/urut), keyboard cari.
void ebookTouch(int x, int y, bool held, bool isNew) {
  if (ebkMode == EBK_GUT) { gutTouch(x, y, held, isNew); return; }
  (void)held;
  if (!isNew) return;
  if (ebkMode == EBK_LIB) {
    if (kbVisible && kbTarget == &ebkQuery) {
      if (y >= kbY() - 2) { kbTouch(x, y); ebkScroll = 0; needRedraw = true; return; }
      if (ebkHitClear(x, y)) { ebkQuery = ""; kbVisible = false; kbTarget = nullptr; ebkViewDirty = true; ebkScroll = 0; needRedraw = true; return; }
      if (ebkHitBox(x, y)) { kbVisible = false; kbTarget = nullptr; needRedraw = true; }          // tutup keyboard, saringan tetap
      return;
    }
    if (isBack(x, y)) { navBack(); return; }
    if (ebkQueryBox()) {
      if (ebkHitClear(x, y)) { ebkQuery = ""; ebkViewDirty = true; ebkScroll = 0; needRedraw = true; }
      else if (ebkHitBox(x, y)) { kbTarget = &ebkQuery; kbVisible = true; kbMode = KB_LOWER; needRedraw = true; }
    } else if (ebkHitGut(x, y)) {
      vibTap(); gutEnter();
    } else if (ebkHitSearch(x, y)) {
      kbTarget = &ebkQuery; kbVisible = true; kbMode = KB_LOWER; ebkScroll = 0; needRedraw = true;
    } else if (ebkHitSort(x, y)) {
      ebkSortMode = (uint8_t)((ebkSortMode + 1) % 3);
      ebkViewDirty = true; ebkScroll = 0;
      char m[40]; snprintf(m, sizeof(m), "Urut: %s", ebkSortNames[ebkSortMode]); showToast(m);
      ebkSaveProgFile(); needRedraw = true;
    }
  } else if (ebkMode == EBK_TOC) { if (isBack(x, y)) { ebkMode = EBK_READ; needRedraw = true; } }
}

static void ebkCycleFont() {
  ebkFontIdx = ebkNextFont(ebkFontIdx);
  ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; ebkDirty = true;       // pertahankan posisi (ebkRelayout memakai offset halaman)
  char m[48]; snprintf(m, sizeof(m), "Font: %s", ebkFonts[ebkFontIdx].name);
  showToast(m);
  static bool hinted = false;
  if (!hinted && ebkFontIdx == 0) {
    bool any = false; for (int i = 0; i < EBK_NFONT; i++) if (ebkFonts[i].vlw && ebkFontOk[i]) any = true;
    if (!any) { showToast("Font Unicode: salin .vlw ke /books/fonts", 2600); hinted = true; }
  }
  ebkSaveProgFile();
  needRedraw = true;
}

static void ebkCycleTheme() {
  ebkPalIdx = (ebkPalIdx + 1) % 4;
  char m[40]; snprintf(m, sizeof(m), "Tema: %s", ebkPals[ebkPalIdx].name);
  showToast(m);
  ebkFreeImg();                                                          // latar gambar PNG ikut tema
  ebkSaveProgFile();
  needRedraw = true;
}

static void ebkReaderRelease() {
  int x = ebkSx, y = ebkSy;
  int dx = ebkLx - ebkSx, dy = ebkLy - ebkSy;
  if (ebkSlide) { ebkSlide = false; ebkTurns = 0; ebkRememberPos(true); return; }
  if (ebkMenu) {
    if (ebkMoved) return;
    int py = ebkPanelY();
    if (y >= py && y <= py + EBK_PANEL_H) {
      for (int row = 0; row < 2; row++) {
        if (y < ebkBtnY(row) - 2 || y > ebkBtnY(row) + 28) continue;
        int nb = row == 0 ? 4 : 2;
        for (int i = 0; i < nb; i++) {
          if (x < ebkBtnX(row, i) || x > ebkBtnX(row, i) + ebkBtnW(row)) continue;
          vibTap();
          if (row == 0) {
            if (i == 0) {
              if (ebkBook.nToc <= 0) { showToast("Buku ini tidak punya daftar isi"); return; }
              ebkRememberPos(false);
              int cur = ebkTocCurrent();
              ebkScroll = (float)((cur > 3 ? cur - 3 : 0) * EBK_TOC_ROW_H);
              ebkMode = EBK_TOC; needRedraw = true;
            } else if (i == 1) ebkCycleFont();
            else if (i == 2) ebkCycleTheme();
            else ebkBackToLibrary();
          } else if (i == 0) {
            ebkJustify = !ebkJustify; showToast(ebkJustify ? "Rata kiri-kanan: Ya" : "Rata kiri-kanan: Tidak");
            ebkSaveProgFile(); needRedraw = true;
          } else {
            ebkAnimOn = !ebkAnimOn; showToast(ebkAnimOn ? "Animasi halaman: Ya" : "Animasi halaman: Tidak");
            ebkSaveProgFile(); needRedraw = true;
          }
          return;
        }
      }
      return;
    }
    if (y >= py) return;                       // ketuk area kosong panel: abaikan
    ebkMenu = false; ebkRememberPos(true); needRedraw = true;   // ketuk di luar panel: tutup menu
    return;
  }
  if (ebkSy > SCR_H - 16) return;              // zona gestur sistem (geser naik = tutup app)
  if (ebkMoved) {
    if (abs(dx) >= 36 && abs(dx) > abs(dy) * 2) { if (dx < 0) ebkNext(); else ebkPrev(); }
    return;
  }
  if (x < SCR_W / 3) ebkPrev();
  else if (x > SCR_W * 2 / 3) ebkNext();
  else { ebkMenu = true; needRedraw = true; }
}

static void ebkListRelease() {
  if (ebkMoved) return;
  int top = ebkListTop();
  if (ebkSy < top) return;
  if (ebkMode == EBK_LIB) {
    int v = (int)((ebkSy - top + (int)ebkScroll) / EBK_ROW_H);
    if (v >= 0 && v < ebkViewN) { vibTap(); ebkStartOpen(ebkView[v]); }
  } else if (ebkMode == EBK_TOC) {
    int i = (int)((ebkSy - top + (int)ebkScroll) / EBK_TOC_ROW_H);
    if (i >= 0 && i < ebkBook.nToc) {
      vibTap();
      const EbToc& tc = ebkBook.toc[i];
      ebkMode = EBK_READ; ebkMenu = false;
      if (ebkBook.kind == EB_KIND_EPUB && tc.ch != ebkBook.curCh) ebkGoChapter(tc.ch, +1, 0, false);
      else { ebkWantOff = tc.off; ebkWantLast = false; ebkDirty = true; }
      ebkRememberPos(true);
      needRedraw = true;
    }
  }
}

// Dipanggil tiap putaran loop() selagi app terbuka: geser daftar / geser halaman, ketuk diproses saat LEPAS.
void ebookPointerTick(bool touched, bool wasT, int tx, int ty) {
  if (ebkMode == EBK_GUT) { gutPointerTick(touched, wasT, tx, ty); return; }
  if (ebkMode == EBK_LOADING) return;
  if (touched) ebkLastTouchMs = millis();
  bool listMode = (ebkMode == EBK_LIB || ebkMode == EBK_TOC);
  if (touched) {
    if (!wasT) {
      ebkSx = tx; ebkSy = ty; ebkLx = tx; ebkLy = ty; ebkPy = ty; ebkMoved = false; ebkSlide = false;
      if (listMode) ebkDown = (ty >= ebkListTop() && ty < ebkListBot());
      else {
        ebkDown = true;
        if (ebkMenu && ebkInSlider(tx, ty)) { ebkSlide = true; ebkSliderSet(tx); }
      }
    } else if (ebkDown) {
      ebkLx = tx; ebkLy = ty;
      if (ebkSlide) { ebkSliderSet(tx); return; }
      if (!ebkMoved && (abs(ty - ebkSy) > 8 || (!listMode && abs(tx - ebkSx) > 10))) ebkMoved = true;
      if (listMode && ebkMoved) {
        ebkScroll += (float)(ebkPy - ty);
        ebkPy = ty;
        needRedraw = true;
      }
    }
  } else if (wasT && ebkDown) {
    ebkDown = false;
    if (listMode) ebkListRelease();
    else ebkReaderRelease();
  } else ebkDown = false;
}
