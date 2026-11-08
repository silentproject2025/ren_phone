// test_core.cpp -- harness uji ebook_core.h di komputer (tanpa Arduino).
//   g++ -std=c++11 -O1 -g -fsanitize=address,undefined -I.. test_core.cpp -o test_core
// Dipanggil oleh test_ebook.py. Perintah:
//   inflate <in.raw> <out.bin> <usize>        inflate aliran deflate mentah
//   epub    <in.epub> <outdir>                dump judul, toc, dan teks tiap bab
//   txt     <in.txt>  <out.txt>               konversi TXT -> teks kanonik
//   layout  <text.txt> <width> <rows> <out>   paginasi + cek invarian
//   fuzz    <in> <epub|txt> <iterasi> <seed>  mutasi acak, tidak boleh crash/bocor
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "ebook_core.h"

struct FileCtx { FILE* f; };
static bool fileRead(void* ctx, uint32_t off, void* dst, uint32_t n) {
  FILE* f = ((FileCtx*)ctx)->f;
  if (fseek(f, off, SEEK_SET) != 0) return false;
  return fread(dst, 1, n, f) == n;
}
struct MemCtx { const uint8_t* p; uint32_t n; };
static bool memRead(void* ctx, uint32_t off, void* dst, uint32_t n) {
  MemCtx* m = (MemCtx*)ctx;
  if ((uint64_t)off + n > m->n) return false;
  memcpy(dst, m->p + off, n);
  return true;
}
static uint8_t* slurp(const char* path, uint32_t* n) {
  FILE* f = fopen(path, "rb"); if (!f) return nullptr;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t* b = (uint8_t*)malloc(sz + 1); if (fread(b, 1, sz, f) != (size_t)sz) { fclose(f); free(b); return nullptr; }
  fclose(f); b[sz] = 0; *n = (uint32_t)sz; return b;
}
static void writeFile(const char* path, const void* d, size_t n) {
  FILE* f = fopen(path, "wb"); if (!f) { perror(path); exit(2); }
  fwrite(d, 1, n, f); fclose(f);
}

// tabel lebar huruf buatan: proporsional kasar, spasi 4 px
static void fakeLay(EbLay* L, int width, int rows) {
  memset(L, 0, sizeof(*L));
  for (int c = 32; c < 127; c++) {
    int w = 6;
    if (c == ' ') w = 4;
    else if (strchr("il.,;:'!|jtfI()", c)) w = 3;
    else if (strchr("mwMW@", c)) w = 10;
    else if (c >= 'A' && c <= 'Z') w = 8;
    L->adv[c] = (uint8_t)w;
  }
  L->width = (int16_t)width; L->indent = 12; L->rows = (int16_t)rows;
}

static int cmdInflate(int argc, char** argv) {
  if (argc < 5) return 2;
  uint32_t n; uint8_t* in = slurp(argv[2], &n); if (!in) return 2;
  uint32_t usize = (uint32_t)atoi(argv[4]);
  uint8_t* out = (uint8_t*)malloc(usize + 1); uint32_t got = 0;
  int r = ebInflate(in, n, out, usize, &got);
  printf("rc=%d got=%u\n", r, got);
  if (r == 0) writeFile(argv[3], out, got);
  free(in); free(out);
  return r == 0 ? 0 : 1;
}

static int cmdEpub(int argc, char** argv) {
  if (argc < 4) return 2;
  FileCtx fc; fc.f = fopen(argv[2], "rb"); if (!fc.f) { printf("open gagal\n"); return 2; }
  fseek(fc.f, 0, SEEK_END); EbRd rd; rd.size = (uint32_t)ftell(fc.f); rd.read = fileRead; rd.ctx = &fc;
  EbBook b;
  int r = ebBookOpen(&b, rd, EB_KIND_EPUB);
  if (r != EB_OK) { printf("ERR %d %s\n", r, ebErrStr(r)); fclose(fc.f); return 1; }
  printf("TITLE\t%s\n", b.title);
  printf("CHAPTERS\t%d\n", b.nSp);
  for (int i = 0; i < b.nSp; i++) {
    printf("TOC\t%d\t%s\t%s\n", i, ebPoolStr(&b, b.sp[i].pathOff), ebPoolStr(&b, b.sp[i].titleOff));
    int lr = ebBookLoadCh(&b, i);
    if (lr != EB_OK) { printf("CHERR\t%d\t%d\n", i, lr); continue; }
    char p[512]; snprintf(p, sizeof(p), "%s/%03d.txt", argv[3], i);
    writeFile(p, b.text, b.textLen);
  }
  ebBookClose(&b); fclose(fc.f);
  return 0;
}

static int cmdTxt(int argc, char** argv) {
  if (argc < 4) return 2;
  FileCtx fc; fc.f = fopen(argv[2], "rb"); if (!fc.f) return 2;
  fseek(fc.f, 0, SEEK_END); EbRd rd; rd.size = (uint32_t)ftell(fc.f); rd.read = fileRead; rd.ctx = &fc;
  EbBook b;
  int r = ebBookOpen(&b, rd, EB_KIND_TXT);
  if (r != EB_OK) { printf("ERR %d\n", r); return 1; }
  writeFile(argv[3], b.text, b.textLen);
  printf("LEN\t%u\nTOC\t%d\n", b.textLen, b.nToc);
  for (int i = 0; i < b.nToc; i++) printf("T\t%u\t%s\n", b.toc[i].off, ebPoolStr(&b, b.toc[i].titleOff));
  ebBookClose(&b); fclose(fc.f);
  return 0;
}

// cek invarian tata letak
static int cmdLayout(int argc, char** argv) {
  if (argc < 6) return 2;
  uint32_t len; uint8_t* t = slurp(argv[2], &len); if (!t) return 2;
  int width = atoi(argv[3]), rows = atoi(argv[4]);
  EbLay L; fakeLay(&L, width, rows);
  uint32_t* pg = nullptr; uint32_t cap = 0;
  uint32_t n = ebPaginate((const char*)t, len, &L, &pg, &cap);
  int bad = 0;
  // rekonstruksi: gabung semua baris tiap halaman, bandingkan dengan teks tanpa penanda/spasi berlebih
  std::string rec, exp;
  for (uint32_t pi = 0; pi < n; pi++) {
    uint32_t pos = pg[pi], endp = (pi + 1 < n) ? pg[pi + 1] : len;
    if (pi > 0 && pg[pi] <= pg[pi - 1]) { printf("halaman tidak naik: %u\n", pi); bad++; }
    bool head = ebParaIsHead((const char*)t, len, pos);
    int lines = 0; EbLine ln;
    while (lines < rows && pos < endp && ebLine((const char*)t, len, pos, head, &L, &ln)) {
      if (ln.end < ln.start || ln.end > len) { printf("end ngawur\n"); bad++; break; }
      int w = 0; for (uint32_t k = ln.start; k < ln.end; k++) w += ebAdv(&L, (char)t[k]);
      int avail = width - ((ln.x == L.indent && !ln.head) ? L.indent : 0);
      if (!ln.head && w > avail && ln.end - ln.start > 1) { printf("baris kelebaran %d>%d p%u\n", w, avail, pi); bad++; }
      if (ln.head && w > width && ln.end - ln.start > 1) { printf("judul kelebaran\n"); bad++; }
      if (ln.x < 0 || ln.x + w > width + 1) { if (!(ln.end - ln.start == 1)) { printf("x ngawur x=%d w=%d\n", ln.x, w); bad++; } }
      rec.append((const char*)t + ln.start, ln.end - ln.start);
      rec.push_back('|');
      pos = ln.next; head = ln.nextHead != 0; lines++;
    }
    // sisa di antara akhir halaman dan awal halaman berikut hanya boleh baris kosong yang dibuang
    while (pos < endp && t[pos] == '\n') pos++;
    if (pos != endp) { printf("halaman %u berhenti di %u bukan %u\n", pi, pos, endp); bad++; }
  }
  // teks harapan: semua non-penanda, non-spasi, non-newline
  std::string a, b2;
  for (size_t i = 0; i < rec.size(); i++) if (rec[i] != '|' && rec[i] != ' ') a.push_back(rec[i]);
  for (uint32_t i = 0; i < len; i++) if (t[i] != 1 && t[i] != ' ' && t[i] != '\n') b2.push_back((char)t[i]);
  if (a != b2) { printf("REKONSTRUKSI BEDA (%zu vs %zu)\n", a.size(), b2.size()); bad++; }
  printf("pages=%u bad=%d\n", n, bad);
  FILE* o = fopen(argv[5], "wb"); if (o) { fprintf(o, "%u\n", n); fclose(o); }
  free(pg); free(t);
  return bad ? 1 : 0;
}


// ---------------------------------------------------------------------
// v2: perintah tambahan
//   book <epub|txt|md> <in> <outdir> <keepUni 0/1>   buka buku, dump metadata + teks + gambar
//   uilayout <text.txt> <width> <rows> <mode>       invarian tata letak UTF-8 (mode 0 = font ASCII, 1 = font Unicode, 2 = Unicode tanpa Yunani)
//   fold <teks>                                     ebFoldAscii
// ---------------------------------------------------------------------
static int fakeCpw1(uint32_t cp) { (void)cp; return 7; }
static int fakeCpw2(uint32_t cp) { return (cp >= 0x370 && cp <= 0x3FF) ? 0 : 7; }

static int cmdBook(int argc, char** argv) {
  if (argc < 6) return 2;
  ebKeepUni = (uint8_t)atoi(argv[5]);
  int kind = !strcmp(argv[2], "epub") ? EB_KIND_EPUB : !strcmp(argv[2], "md") ? EB_KIND_MD : EB_KIND_TXT;
  FileCtx fc; fc.f = fopen(argv[3], "rb"); if (!fc.f) { printf("open gagal\n"); return 2; }
  fseek(fc.f, 0, SEEK_END); EbRd rd; rd.size = (uint32_t)ftell(fc.f); rd.read = fileRead; rd.ctx = &fc;
  EbBook b;
  int r = ebBookOpen(&b, rd, kind);
  if (r != EB_OK) { printf("ERR %d %s\n", r, ebErrStr(r)); fclose(fc.f); return 1; }
  printf("TITLE\t%s\nAUTHOR\t%s\nCOVER\t%s\nCHAPTERS\t%d\nNTOC\t%d\n", b.title, b.author, b.coverOff ? ebPoolStr(&b, b.coverOff) : "", ebBookChapters(&b), b.nToc);
  if (b.kind == EB_KIND_EPUB) {
    uint8_t* d; uint32_t dl;
    int cr = ebBookCover(&b, &d, &dl);
    printf("COVERGET\t%d\t%u\n", cr, cr == EB_OK ? dl : 0);
    if (cr == EB_OK) { char p[512]; snprintf(p, sizeof(p), "%s/cover.bin", argv[4]); writeFile(p, d, dl); EB_FREE(d); }
  }
  for (int i = 0; i < ebBookChapters(&b); i++) {
    if (b.kind == EB_KIND_EPUB) { int lr = ebBookLoadCh(&b, i); if (lr != EB_OK) { printf("CHERR\t%d\t%d\n", i, lr); continue; } }
    char p[512]; snprintf(p, sizeof(p), "%s/%03d.txt", argv[4], i);
    writeFile(p, b.text, b.textLen);
    for (int k = 0; k < b.nImg; k++) {
      uint8_t* d; uint32_t dl; int ir = ebBookImage(&b, k, &d, &dl);
      printf("IMG\t%d\t%d\t%s\t%u\t%d\t%u\n", i, k, b.imgs[k].path, b.imgs[k].usize, ir, ir == EB_OK ? dl : 0);
      if (ir == EB_OK) EB_FREE(d);
    }
  }
  ebBookClose(&b); fclose(fc.f);
  return 0;
}

static int cmdUiLayout(int argc, char** argv) {
  if (argc < 6) return 2;
  uint32_t len; uint8_t* t = slurp(argv[2], &len); if (!t) return 2;
  int width = atoi(argv[3]), rows = atoi(argv[4]), mode = atoi(argv[5]);
  EbLay L; fakeLay(&L, width, rows);
  L.uni = (mode >= 1); L.cpw = mode == 1 ? fakeCpw1 : mode == 2 ? fakeCpw2 : nullptr;
  uint32_t* pg = nullptr; uint32_t cap = 0;
  uint32_t n = ebPaginate((const char*)t, len, &L, &pg, &cap);
  int bad = 0, imgPages = 0; uint32_t nonAscii = 0;
  for (uint32_t pi = 0; pi < n; pi++) {
    uint32_t pos = pg[pi], endp = (pi + 1 < n) ? pg[pi + 1] : len;
    if (t[pos] == 2) {                                           // halaman gambar: tepat satu baris gambar
      EbLine il; imgPages++;
      if (!ebLine((const char*)t, len, pos, false, &L, &il) || il.img < 0) { printf("halaman gambar tanpa img\n"); bad++; }
      else if (il.next != endp && !(il.next >= len)) { printf("halaman gambar %u memuat lebih dari satu paragraf\n", pi); bad++; }
      continue;
    }
    bool head = ebParaIsHead((const char*)t, len, pos);
    int lines = 0; EbLine ln;
    while (lines < rows && pos < endp && ebLine((const char*)t, len, pos, head, &L, &ln)) {
      if (ln.img >= 0) { printf("gambar di tengah halaman %u\n", pi); bad++; break; }
      char buf[400]; int wpx = 0;
      ebRunText(&L, (const char*)t, ln.start, ln.end, buf, sizeof(buf), &wpx);
      for (const char* q = buf; *q; q++) {
        if ((uint8_t)*q >= 0x80) nonAscii++;
        else if ((uint8_t)*q < 32) { printf("kontrol di keluaran p%u\n", pi); bad++; }
      }
      if (mode == 0) for (const char* q = buf; *q; q++) if ((uint8_t)*q >= 0x80) { printf("font ASCII tapi keluaran non-ASCII\n"); bad++; break; }
      int avail = width - ((ln.x == L.indent && !ln.head) ? L.indent : 0);
      if (!ln.head && !ln.empty && wpx > avail && ln.end - ln.start > 1 && !ln.split) { printf("baris kelebaran %d>%d\n", wpx, avail); bad++; }
      if (!ln.empty && wpx != ln.w) { printf("lebar ebRunText %d != ebLine %d (p%u)\n", wpx, ln.w, pi); bad++; }
      pos = ln.next; head = ln.nextHead != 0; lines++;
    }
  }
  printf("pages=%u imgPages=%d nonAscii=%u bad=%d\n", n, imgPages, nonAscii, bad);
  free(pg); free(t);
  return bad ? 1 : 0;
}

static int cmdFold(int argc, char** argv) {
  if (argc < 3) return 2;
  char out[512]; ebFoldAscii(argv[2], out, sizeof(out));
  printf("%s\n", out);
  return 0;
}

static uint32_t rngState = 1;
static uint32_t rnd() { rngState = rngState * 1664525u + 1013904223u; return rngState >> 8; }

static int cmdFuzz(int argc, char** argv) {
  if (argc < 6) return 2;
  uint32_t n; uint8_t* src = slurp(argv[2], &n); if (!src) return 2;
  bool epub = strcmp(argv[3], "epub") == 0;
  int iters = atoi(argv[4]); rngState = (uint32_t)atoi(argv[5]);
  uint8_t* buf = (uint8_t*)malloc(n + 16);
  int okc = 0, errc = 0;
  for (int it = 0; it < iters; it++) {
    memcpy(buf, src, n);
    uint32_t len = n;
    int mode = rnd() % 4;
    if (mode == 0) { int k = 1 + rnd() % 8; for (int i = 0; i < k; i++) buf[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); }
    else if (mode == 1) { len = rnd() % (n + 1); }
    else if (mode == 2) { int k = 1 + rnd() % 4; for (int i = 0; i < k; i++) { uint32_t a = rnd() % len, c = 1 + rnd() % 64; for (uint32_t j = a; j < a + c && j < len; j++) buf[j] = (uint8_t)rnd(); } }
    else { int k = 1 + rnd() % 3; for (int i = 0; i < k; i++) { uint32_t a = rnd() % len; buf[a] = (uint8_t)rnd(); } len = len - rnd() % (len / 4 + 1); }
    MemCtx mc; mc.p = buf; mc.n = len;
    EbRd rd; rd.read = memRead; rd.ctx = &mc; rd.size = len;
    EbBook b;
    int r = ebBookOpen(&b, rd, epub ? EB_KIND_EPUB : EB_KIND_TXT);
    if (r != EB_OK) { errc++; continue; }
    okc++;
    int chs = ebBookChapters(&b);
    for (int c = 0; c < chs && c < 40; c++) {
      if (ebBookLoadCh(&b, c) != EB_OK) continue;
      EbLay L; fakeLay(&L, 280, 12);
      uint32_t* pg = nullptr; uint32_t cap = 0;
      uint32_t np = ebPaginate(b.text, b.textLen, &L, &pg, &cap);
      for (uint32_t pi = 0; pi < np && pi < 50; pi++) {
        uint32_t pos = pg[pi]; bool head = ebParaIsHead(b.text, b.textLen, pos); EbLine ln;
        for (int r2 = 0; r2 < 12 && ebLine(b.text, b.textLen, pos, head, &L, &ln); r2++) { pos = ln.next; head = ln.nextHead != 0; }
      }
      free(pg);
    }
    ebBookClose(&b);
  }
  printf("fuzz selesai: %d dibuka, %d ditolak\n", okc, errc);
  free(buf); free(src);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (!strcmp(argv[1], "inflate")) return cmdInflate(argc, argv);
  if (!strcmp(argv[1], "epub")) return cmdEpub(argc, argv);
  if (!strcmp(argv[1], "txt")) return cmdTxt(argc, argv);
  if (!strcmp(argv[1], "layout")) return cmdLayout(argc, argv);
  if (!strcmp(argv[1], "fuzz")) return cmdFuzz(argc, argv);
  if (!strcmp(argv[1], "book")) return cmdBook(argc, argv);
  if (!strcmp(argv[1], "uilayout")) return cmdUiLayout(argc, argv);
  if (!strcmp(argv[1], "fold")) return cmdFold(argc, argv);
  return 2;
}
