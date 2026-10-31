// =====================================================================
// ebook_renphone.ino -- app BUKU (SCR_EBOOK): pembaca e-book .txt dan .epub
// =====================================================================
// Letakkan file .txt / .epub di folder /books pada SD card (folder dibuat otomatis;
// file di root SD juga ikut terdaftar). Fitur:
//   * Perpustakaan  : daftar buku, persen terbaca, buku terakhir dibaca di atas.
//   * Pembaca       : halaman berpaginasi, ketuk kanan/kiri atau geser untuk pindah halaman,
//                     ketuk tengah untuk menu (slider halaman, daftar isi, font, tema, tutup).
//   * Font          : Sans, Serif, Mono, Kecil, Sans+, Serif+  (font GFX bawaan LovyanGFX, ASCII).
//   * Tema baca     : Terang, Sepia, Gelap, Hitam (terpisah dari tema sistem).
//   * Posisi baca   : disimpan per buku di /books/.nyxreader (bab + offset, tidak bergantung font).
//   * EPUB 2 & 3    : bab dibaca satu per satu dari ZIP (hemat RAM), daftar isi dari NCX / nav.
//   * TXT           : UTF-8, UTF-16 (BOM), Latin-1/Windows-1252; teks berbaris-keras di-reflow;
//                     judul "Bab/Chapter N" terdeteksi untuk daftar isi. Batas 3 MB per file.
//
// BATASAN: font layar hanya ASCII, jadi huruf beraksen diubah ke huruf dasar (e/a/n...),
// tanda kutip/strip melengkung diluruskan, dan aksara non-Latin (CJK, Arab, Kiril) tampil "?".
//
// Logika berat (inflate, ZIP, EPUB, HTML->teks, tata letak) ada di ebook_core.h dan diuji di
// komputer lewat tests_ebook/. Berkas ini hanya antarmuka + SD.
//
// ATURAN PROYEK: semua buffer besar di PSRAM dan dibebaskan saat app ditutup; tidak ada String
// per frame; fungsi publik hanya bertipe primitif / LGFX_Sprite& (bug prototype arduino-cli).
//
// DIPASANG ke phone.ino lewat apply_ebook_patch.py:
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
#define EBK_MAX_BOOKS  200
#define EBK_NAME_LEN   96
#define EBK_ROW_H      38
#define EBK_TOC_ROW_H  24

enum { EBK_LIB = 0, EBK_LOADING, EBK_READ, EBK_TOC };

struct EbkEntry { char name[EBK_NAME_LEN]; uint32_t size; uint8_t epub; uint8_t inRoot; uint8_t pct; uint32_t seq; };
struct EbkProg  { char name[EBK_NAME_LEN]; int32_t ch; uint32_t off; uint8_t pct; uint32_t seq; };
struct EbkFont  { const char* name; const lgfx::IFont* f; float scale; };
struct EbkPal   { const char* name; uint16_t bg, text, sub, head; };

static const EbkFont ebkFonts[6] = {
  { "Sans",   &lgfx::fonts::FreeSans9pt7b,  1.0f },
  { "Serif",  &lgfx::fonts::FreeSerif9pt7b, 1.0f },
  { "Mono",   &lgfx::fonts::FreeMono9pt7b,  1.0f },
  { "Kecil",  &lgfx::fonts::Font0,          1.0f },
  { "Sans+",  &lgfx::fonts::FreeSans9pt7b,  1.3f },
  { "Serif+", &lgfx::fonts::FreeSerif9pt7b, 1.3f },
};
static const EbkPal ebkPals[4] = {
  { "Terang", 0xF79E, 0x18E3, 0x8410, 0x1AB9 },
  { "Sepia",  0xF79B, 0x5A26, 0x8BCC, 0x9A43 },
  { "Gelap",  0x18C3, 0xCE59, 0x738E, 0x7DBF },
  { "Hitam",  0x0000, 0xB596, 0x630C, 0x5D1F },
};

static uint8_t   ebkMode = EBK_LIB;
// perpustakaan
static EbkEntry* ebkLib = nullptr;   static int ebkLibN = 0;
static EbkProg*  ebkProgs = nullptr; static int ebkProgN = 0;
static uint32_t  ebkSeq = 0;
static float     ebkScroll = 0;      // px (perpustakaan / daftar isi)
static int       ebkPendingIdx = -1; static unsigned long ebkLoadDue = 0;
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
static bool      ebkMenu = false;
static int       ebkTurns = 0;
// sentuhan
static bool ebkDown = false, ebkMoved = false, ebkSlide = false;
static int  ebkSx = 0, ebkSy = 0, ebkLx = 0, ebkLy = 0, ebkPy = 0;

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

static void ebkPath(int i, char* out, int cap) {
  if (ebkLib[i].inRoot) snprintf(out, (size_t)cap, "/%s", ebkLib[i].name);
  else snprintf(out, (size_t)cap, EBK_DIR "/%s", ebkLib[i].name);
}

// ---------------------------------------------------------------------
// progres baca: /books/.nyxreader  (teks, dipisah TAB)
//   S <font> <tema> <seq>
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
    char* fld[7]; int nf = 0;
    char* p = line;
    while (nf < 7) { fld[nf++] = p; char* t = strchr(p, '\t'); if (!t) break; *t = 0; p = t + 1; }
    if (nf >= 4 && fld[0][0] == 'S') {
      int fi = atoi(fld[1]), pi = atoi(fld[2]);
      if (fi >= 0 && fi < 6) ebkFontIdx = fi;
      if (pi >= 0 && pi < 4) ebkPalIdx = pi;
      ebkSeq = (uint32_t)strtoul(fld[3], nullptr, 10);
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
  char ln[200];
  int n = snprintf(ln, sizeof(ln), "S\t%d\t%d\t%u\n", ebkFontIdx, ebkPalIdx, (unsigned)ebkSeq);
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

// persen terbaca (0..100) untuk posisi sekarang
static int ebkPercent() {
  if (!ebkOpened || ebkPgN == 0) return 0;
  int nch = ebBookChapters(&ebkBook); if (nch < 1) nch = 1;
  int c = ebkBook.curCh < 0 ? 0 : ebkBook.curCh;
  float within = (float)(ebkPage + 1) / (float)ebkPgN;
  int p = (int)(((float)c + within) * 100.0f / (float)nch);
  return p < 0 ? 0 : p > 100 ? 100 : p;
}

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
// perpustakaan
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
      bool isTxt = ebkEndsCI(nm, ".txt"), isEpub = ebkEndsCI(nm, ".epub");
      if (nm[0] != '.' && (isTxt || isEpub) && strlen(nm) < EBK_NAME_LEN) {
        EbkEntry* e = &ebkLib[ebkLibN++];
        strcpy(e->name, nm); e->size = (uint32_t)f.size(); e->epub = isEpub ? 1 : 0; e->inRoot = inRoot;
        e->pct = 0; e->seq = 0;
        int pi = ebkFindProg(nm);
        if (pi >= 0) { e->pct = ebkProgs[pi].pct; e->seq = ebkProgs[pi].seq; }
      }
    }
    f.close();
    f = dir.openNextFile();
    yield();
  }
  if (f) f.close();
  dir.close();
}

static void ebkSortLib() {
  EbkEntry tmp;
  for (int i = 1; i < ebkLibN; i++) {
    tmp = ebkLib[i];
    int j = i - 1;
    while (j >= 0) {
      const EbkEntry& a = ebkLib[j];
      bool before;                                   // tmp harus di depan a?
      if (tmp.seq != a.seq && (tmp.seq || a.seq)) before = tmp.seq > a.seq;
      else before = strcasecmp(tmp.name, a.name) < 0;
      if (!before) break;
      ebkLib[j + 1] = ebkLib[j]; j--;
    }
    ebkLib[j + 1] = tmp;
  }
}

static void ebkScan() {
  if (!ebkLib) ebkLib = (EbkEntry*)ebPsAlloc(sizeof(EbkEntry) * EBK_MAX_BOOKS);
  ebkLibN = 0;
  if (!ebkLib || !sdReady) return;
  if (!SD_MMC.exists(EBK_DIR)) SD_MMC.mkdir(EBK_DIR);
  ebkLoadProgFile();
  ebkScanDir(EBK_DIR, 0);
  ebkScanDir("/", 1);
  ebkSortLib();
}

// ---------------------------------------------------------------------
// buku terbuka: bab, tata letak, navigasi halaman
// ---------------------------------------------------------------------
static void ebkFreePages() {
  if (ebkPg) { free(ebkPg); ebkPg = nullptr; }
  ebkPgCap = 0; ebkPgN = 0; ebkPage = 0;
}

static void ebkCloseBook() {
  if (ebkOpened) { ebBookClose(&ebkBook); ebkOpened = false; }
  if (ebkFile) ebkFile.close();
  ebkFreePages();
  ebkMenu = false;
}

// Muat bab target; bab kosong dilewati searah dir (+1/-1). Mengembalikan false bila gagal/tak ada isi.
static bool ebkLoadChapter(int ch, int dir) {
  int nch = ebBookChapters(&ebkBook);
  int orig = ebkBook.curCh;
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
  s.setFont(fo.f); s.setTextSize(fo.scale);
  int fh = s.fontHeight(); if (fh < 8) fh = 8;
  ebkLineH = fh + (fo.f == &lgfx::fonts::Font0 ? 3 : 2);
  memset(&ebkLay, 0, sizeof(ebkLay));
  char c[2] = { 0, 0 };
  for (int ch = 32; ch < 127; ch++) {
    c[0] = (char)ch;
    int w = s.textWidth(c); if (w < 1) w = 1; if (w > 250) w = 250;
    ebkLay.adv[ch] = (uint8_t)w;
  }
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

static void ebkNext() {
  if (ebkPage + 1 < ebkPgN) { ebkPage++; ebkTurned(); return; }
  int nch = ebBookChapters(&ebkBook);
  if (ebkBook.curCh + 1 < nch) {
    int before = ebkBook.curCh;
    ebkGoChapter(ebkBook.curCh + 1, +1, 0, false);
    if (ebkBook.curCh == before) showToast("Akhir buku");
    else ebkTurned();
  } else showToast("Akhir buku");
}

static void ebkPrev() {
  if (ebkPage > 0) { ebkPage--; ebkTurned(); return; }
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
  int r = ebBookOpen(&ebkBook, rd, ebkLib[idx].epub ? EB_KIND_EPUB : EB_KIND_TXT);
  if (r != EB_OK) {
    ebkFile.close();
    showToast(ebErrStr(r));
    ebkMode = EBK_LIB; needRedraw = true; return;
  }
  ebkOpened = true;
  strncpy(ebkName, ebkLib[idx].name, EBK_NAME_LEN - 1); ebkName[EBK_NAME_LEN - 1] = 0;
  if (ebkBook.title[0] == 0) ebkStem(ebkName, ebkBook.title, sizeof(ebkBook.title));
  if (ebkBook.truncated) showToast("Teks dipotong di 3 MB", 2200);

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
  ebkScan();
  ebkScroll = 0;
  ebkMode = EBK_LIB;
  needRedraw = true;
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
static void ebkFreeAll() {
  ebkCloseBook();
  if (ebkLib) { free(ebkLib); ebkLib = nullptr; } ebkLibN = 0;
  if (ebkProgs) { free(ebkProgs); ebkProgs = nullptr; } ebkProgN = 0;
}

void ebookEnter() {
  ebkMode = EBK_LIB; ebkScroll = 0; ebkMenu = false; ebkDown = false; ebkPendingIdx = -1;
  ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; ebkDirty = true;
  ebkScan();
}

void ebookExit() {
  if (ebkOpened) ebkRememberPos(true);
  ebkFreeAll();
  ebkMode = EBK_LIB; ebkDown = false; ebkPendingIdx = -1;
}

// dipanggil dari loop(): menjalankan pembukaan buku setelah layar "Membuka..." tampil
void ebookLoopPoll() {
  if (ebkMode == EBK_LOADING && ebkPendingIdx >= 0 && (long)(millis() - ebkLoadDue) >= 0) ebkDoOpen();
}

// ---------------------------------------------------------------------
// geometri menu pembaca
// ---------------------------------------------------------------------
#define EBK_PANEL_H 78
static int ebkPanelY()   { return SCR_H - EBK_PANEL_H - 18; }
static int ebkSliderX0() { return 16; }
static int ebkSliderX1() { return SCR_W - 16; }
static int ebkSliderY()  { return ebkPanelY() + 14; }
static int ebkBtnW()     { return (SCR_W - 16 - 3 * 6) / 4; }
static int ebkBtnX(int i){ return 8 + i * (ebkBtnW() + 6); }
static int ebkBtnY()     { return ebkPanelY() + EBK_PANEL_H - 32; }

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
// gambar: perpustakaan
// ---------------------------------------------------------------------
static int ebkListTop() { return 50; }
static int ebkListBot() { return SCR_H - BACK_H - 3 - 6; }

static void ebkClampScroll(int contentH) {
  int view = ebkListBot() - ebkListTop();
  int mx = contentH - view; if (mx < 0) mx = 0;
  if (ebkScroll < 0) ebkScroll = 0;
  if (ebkScroll > (float)mx) ebkScroll = (float)mx;
}

static void ebkDrawLibrary(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Buku");
  int top = ebkListTop(), bot = ebkListBot();
  s.setTextSize(1);
  if (!sdReady) {
    s.setTextColor(T().subtext); s.setCursor(12, top + 10); s.print("SD card tidak terdeteksi.");
  } else if (ebkLibN == 0) {
    s.setTextColor(T().text);    s.setCursor(12, top + 10); s.print("Belum ada buku.");
    s.setTextColor(T().subtext); s.setCursor(12, top + 26); s.print("Salin file .txt / .epub ke folder");
    s.setCursor(12, top + 38); s.print("/books pada SD card.");
  } else {
    ebkClampScroll(ebkLibN * EBK_ROW_H);
    s.setClipRect(0, top, SCR_W, bot - top);
    int y0 = top - (int)ebkScroll;
    for (int i = 0; i < ebkLibN; i++) {
      int ry = y0 + i * EBK_ROW_H;
      if (ry + EBK_ROW_H < top || ry > bot) continue;
      char nm[EBK_NAME_LEN]; ebkStem(ebkLib[i].name, nm, sizeof(nm));
      ebkFitText(s, nm, SCR_W - 24 - 40);
      s.setTextColor(T().text); s.setCursor(12, ry + 6); s.print(nm);
      char sz[16], info[40]; ebkFmtSize(sz, sizeof(sz), ebkLib[i].size);
      snprintf(info, sizeof(info), "%s  %s", ebkLib[i].epub ? "EPUB" : "TXT", sz);
      s.setTextColor(T().subtext); s.setCursor(12, ry + 21); s.print(info);
      if (ebkLib[i].pct > 0) {
        char pc[8]; snprintf(pc, sizeof(pc), "%u%%", (unsigned)ebkLib[i].pct);
        s.setTextColor(T().accent); s.setCursor(SCR_W - 12 - s.textWidth(pc), ry + 6); s.print(pc);
        int bw = (SCR_W - 24) * ebkLib[i].pct / 100;
        s.fillRect(12, ry + EBK_ROW_H - 4, SCR_W - 24, 2, T().divider);
        s.fillRect(12, ry + EBK_ROW_H - 4, bw, 2, T().accent);
      } else s.drawFastHLine(12, ry + EBK_ROW_H - 3, SCR_W - 24, T().divider);
    }
    s.clearClipRect();
    int contentH = ebkLibN * EBK_ROW_H, view = bot - top;
    if (contentH > view) {
      int th = view * view / contentH; if (th < 12) th = 12;
      int mx = contentH - view;
      int ty = top + (int)((view - th) * (ebkScroll / (float)mx));
      s.fillRoundRect(SCR_W - 4, ty, 2, th, 1, blend565(T().bg, T().text, 90));
    }
  }
  drawBack(s);
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
  int top = ebkListTop(), bot = ebkListBot();
  s.setTextSize(1);
  int n = ebkBook.nToc;
  ebkClampScroll(n * EBK_TOC_ROW_H);
  int cur = ebkTocCurrent();
  s.setClipRect(0, top, SCR_W, bot - top);
  int y0 = top - (int)ebkScroll;
  for (int i = 0; i < n; i++) {
    int ry = y0 + i * EBK_TOC_ROW_H;
    if (ry + EBK_TOC_ROW_H < top || ry > bot) continue;
    if (i == cur) s.fillRoundRect(6, ry + 1, SCR_W - 12, EBK_TOC_ROW_H - 2, 6, blend565(T().bg, T().accent, 70));
    char t[96]; strncpy(t, ebPoolStr(&ebkBook, ebkBook.toc[i].titleOff), sizeof(t) - 1); t[sizeof(t) - 1] = 0;
    ebkFitText(s, t, SCR_W - 28);
    s.setTextColor(i == cur ? T().text : T().subtext);
    s.setCursor(14, ry + 8); s.print(t);
  }
  s.clearClipRect();
  drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// gambar: pembaca
// ---------------------------------------------------------------------
static void ebkDrawMenu(LGFX_Sprite& s, int palIdx) {
  const EbkPal& pal = ebkPals[palIdx];
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  uint16_t panel = blend565(pal.bg, pal.text, 38), line = blend565(pal.bg, pal.text, 90);
  // bilah atas: judul buku / bab
  s.fillRect(0, 0, SCR_W, 28, panel);
  char t[96];
  strncpy(t, ebkBook.title, sizeof(t) - 1); t[sizeof(t) - 1] = 0;
  ebkFitText(s, t, SCR_W - 20);
  s.setTextColor(pal.text); s.setCursor(10, 5); s.print(t);
  if (ebkBook.kind == EB_KIND_EPUB && ebkBook.curCh >= 0 && ebkBook.curCh < ebkBook.nSp) {
    strncpy(t, ebPoolStr(&ebkBook, ebkBook.sp[ebkBook.curCh].titleOff), sizeof(t) - 1); t[sizeof(t) - 1] = 0;
    ebkFitText(s, t, SCR_W - 20);
    s.setTextColor(pal.sub); s.setCursor(10, 16); s.print(t);
  }
  // panel bawah
  int py = ebkPanelY();
  s.fillRoundRect(4, py, SCR_W - 8, EBK_PANEL_H, 10, panel);
  int x0 = ebkSliderX0(), x1 = ebkSliderX1(), sy = ebkSliderY();
  s.fillRoundRect(x0, sy - 2, x1 - x0, 4, 2, line);
  float f = ebkPgN > 1 ? (float)ebkPage / (float)(ebkPgN - 1) : 0.0f;
  int kx = x0 + (int)((x1 - x0) * f);
  s.fillRoundRect(x0, sy - 2, kx - x0, 4, 2, pal.head);
  s.fillCircle(kx, sy, 6, pal.head);
  char info[64];
  if (ebkBook.kind == EB_KIND_EPUB)
    snprintf(info, sizeof(info), "Bab %d/%d   Hal %u/%u   %d%%", ebkBook.curCh + 1, ebBookChapters(&ebkBook), (unsigned)ebkPage + 1, (unsigned)ebkPgN, ebkPercent());
  else
    snprintf(info, sizeof(info), "Hal %u/%u   %d%%", (unsigned)ebkPage + 1, (unsigned)ebkPgN, ebkPercent());
  s.setTextColor(pal.text);
  s.setCursor((SCR_W - s.textWidth(info)) / 2, sy + 12); s.print(info);
  const char* lb[4] = { "Isi", "Aa", "Tema", "Tutup" };
  for (int i = 0; i < 4; i++) {
    int bx = ebkBtnX(i), by = ebkBtnY(), bw = ebkBtnW();
    s.fillRoundRect(bx, by, bw, 26, 8, i == 3 ? blend565(pal.bg, 0xF800, 110) : line);
    s.setTextColor(pal.text);
    s.setCursor(bx + (bw - s.textWidth(lb[i])) / 2, by + 9); s.print(lb[i]);
  }
}

static void ebkDrawReader(LGFX_Sprite& s) {
  const EbkPal& pal = ebkPals[ebkPalIdx];
  s.fillRect(0, 0, SCR_W, SCR_H, pal.bg);
  if (!ebkOpened || !ebkBook.text) { drawToast(s); return; }
  if (ebkDirty || ebkLayW != SCR_W || ebkLayH != SCR_H) {
    if (!ebkDirty) { ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; }
    ebkRelayout(s);
  }
  const EbkFont& fo = ebkFonts[ebkFontIdx];
  s.setFont(fo.f); s.setTextSize(fo.scale);
  s.setTextWrap(false);
  if (ebkPgN > 0 && ebkPage < ebkPgN) {
    const char* t = ebkBook.text; uint32_t len = ebkBook.textLen;
    uint32_t pos = ebkPg[ebkPage];
    bool head = ebParaIsHead(t, len, pos);
    EbLine ln; char buf[200];
    for (int r = 0; r < ebkLay.rows; r++) {
      if (!ebLine(t, len, pos, head, &ebkLay, &ln)) break;
      if (!ln.empty) {
        uint32_t n = ln.end - ln.start; if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
        memcpy(buf, t + ln.start, n); buf[n] = 0;
        s.setTextColor(ln.head ? pal.head : pal.text);
        s.setCursor(8 + ln.x, 8 + r * ebkLineH);
        s.print(buf);
      }
      pos = ln.next; head = ln.nextHead != 0;
    }
  }
  // garis info bawah
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);
  s.setTextColor(pal.sub);
  char info[40]; snprintf(info, sizeof(info), "%u/%u  %d%%", (unsigned)ebkPage + 1, (unsigned)ebkPgN, ebkPercent());
  s.setCursor(SCR_W - 8 - s.textWidth(info), SCR_H - 15); s.print(info);
  if (ebkBook.kind == EB_KIND_EPUB && ebkBook.curCh >= 0 && ebkBook.curCh < ebkBook.nSp) {
    char t[64]; strncpy(t, ebPoolStr(&ebkBook, ebkBook.sp[ebkBook.curCh].titleOff), sizeof(t) - 1); t[sizeof(t) - 1] = 0;
    ebkFitText(s, t, SCR_W - 24 - s.textWidth(info));
    s.setCursor(8, SCR_H - 15); s.print(t);
  } else {
    char t[64]; strncpy(t, ebkBook.title, sizeof(t) - 1); t[sizeof(t) - 1] = 0;
    ebkFitText(s, t, SCR_W - 24 - s.textWidth(info));
    s.setCursor(8, SCR_H - 15); s.print(t);
  }
  if (ebkMenu) ebkDrawMenu(s, ebkPalIdx);
  s.setFont(&lgfx::fonts::Font0); s.setTextSize(1);   // toast selalu Font0
  drawToast(s);
}

static void ebkDrawLoading(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Buku");
  char nm[EBK_NAME_LEN] = "";
  if (ebkPendingIdx >= 0 && ebkPendingIdx < ebkLibN) ebkStem(ebkLib[ebkPendingIdx].name, nm, sizeof(nm));
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
    default:          ebkDrawLibrary(s); break;
  }
}

// ---------------------------------------------------------------------
// sentuh
// ---------------------------------------------------------------------
// Sentuhan generik (jari baru menyentuh): hanya tombol Kembali di mode perpustakaan / daftar isi.
void ebookTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (ebkMode == EBK_LIB) { if (isBack(x, y)) navBack(); }
  else if (ebkMode == EBK_TOC) { if (isBack(x, y)) { ebkMode = EBK_READ; needRedraw = true; } }
}

static void ebkCycleFont() {
  ebkFontIdx = (ebkFontIdx + 1) % 6;
  ebkWantOff = 0xFFFFFFFFu; ebkWantLast = false; ebkDirty = true;       // pertahankan posisi (ebkRelayout memakai offset halaman)
  char m[40]; snprintf(m, sizeof(m), "Font: %s", ebkFonts[ebkFontIdx].name);
  showToast(m);
  ebkSaveProgFile();
  needRedraw = true;
}

static void ebkCycleTheme() {
  ebkPalIdx = (ebkPalIdx + 1) % 4;
  char m[40]; snprintf(m, sizeof(m), "Tema: %s", ebkPals[ebkPalIdx].name);
  showToast(m);
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
    if (y >= py && y <= py + EBK_PANEL_H && y >= ebkBtnY() - 2 && y <= ebkBtnY() + 28) {
      for (int i = 0; i < 4; i++) {
        if (x >= ebkBtnX(i) && x <= ebkBtnX(i) + ebkBtnW()) {
          vibTap();
          if (i == 0) {
            if (ebkBook.nToc <= 0) { showToast("Buku ini tidak punya daftar isi"); return; }
            ebkRememberPos(false);
            int cur = ebkTocCurrent();
            ebkScroll = (float)((cur > 3 ? cur - 3 : 0) * EBK_TOC_ROW_H);
            ebkMode = EBK_TOC; needRedraw = true;
          } else if (i == 1) ebkCycleFont();
          else if (i == 2) ebkCycleTheme();
          else ebkBackToLibrary();
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
  int rowH = (ebkMode == EBK_TOC) ? EBK_TOC_ROW_H : EBK_ROW_H;
  int i = (int)((ebkSy - top + (int)ebkScroll) / rowH);
  if (ebkSy < top || i < 0) return;
  if (ebkMode == EBK_LIB) {
    if (i < ebkLibN) { vibTap(); ebkStartOpen(i); }
  } else if (ebkMode == EBK_TOC) {
    if (i < ebkBook.nToc) {
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
  if (ebkMode == EBK_LOADING) return;
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
