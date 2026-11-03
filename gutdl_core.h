// =====================================================================
// gutdl_core.h -- logika murni (tanpa Arduino) utk unduhan Gutenberg yang AMAN
// =====================================================================
// Isi:
//   1. URL LANGSUNG ke berkas buku dari nomor buku (cache/epub/N/pgN*.epub) -> tanpa pengalihan 302,
//      satu permintaan per unduhan. Dipakai utk SEMUA sumber pencarian (Gutendex maupun OPDS).
//   2. Registry unduhan per nomor buku: satu buku tidak bisa dimulai dua kali, satu pekerjaan sekaligus.
//   3. CRC32 (utk membandingkan data dari jaringan dgn data yang benar-benar ada di SD).
//   4. Cek struktur ZIP/EPUB (tanda awal "PK", End-Of-Central-Directory, Central Directory).
//   5. Sumber pencarian ke-2: feed OPDS resmi Gutenberg (XML) -> pembuat URL + pembaca ringan.
// Semua static inline, tanpa alokasi heap. Diuji di PC: tests_gutenberg/test_gutdl.cpp
// =====================================================================
#ifndef GUTDL_CORE_H
#define GUTDL_CORE_H

#include "gutendex_core.h"

// ---------------------------------------------------------------------
// 1. URL langsung
// ---------------------------------------------------------------------
enum { GUT_K_EPUB_LITE = 0, GUT_K_EPUB3_IMG, GUT_K_EPUB_IMG, GUT_K_TXT };

// https://www.gutenberg.org/cache/epub/<N>/pg<N><akhiran>
//   LITE  -> pgN.epub            (EPUB tanpa gambar, kecil)
//   EPUB3 -> pgN-images-3.epub   (persis URL tujuan pengalihan 302 yang terlihat di log)
//   EPUB  -> pgN-images.epub
//   TXT   -> pgN.txt             (UTF-8)
static inline bool gutDirectUrl(uint32_t id, int kind, char* out, int cap) {
  static const char* const suf[4] = { ".epub", "-images-3.epub", "-images.epub", ".txt" };
  if (!id || kind < 0 || kind > 3) return false;
  int n = snprintf(out, (size_t)cap, "https://www.gutenberg.org/cache/epub/%u/pg%u%s", (unsigned)id, (unsigned)id, suf[kind]);
  return n > 0 && n < cap;
}

// Urutan URL yang dicoba utk satu buku. tiers[i] = "" kalau tidak berlaku.
//   0 = EPUB ringan langsung (hanya bila isEpub && light)
//   1 = format utama langsung (EPUB3 dgn gambar, atau TXT)
//   2 = URL dari API (Gutendex: biasanya /ebooks/N.xxx -> pengalihan 302) -- cadangan terakhir
// Kembalikan indeks tier pertama yang terisi.
static inline int gutBuildTiers(const GutItem& it, bool isEpub, bool light, char tiers[3][GUT_URL_LEN + 8]) {
  const int W = GUT_URL_LEN + 8;
  tiers[0][0] = tiers[1][0] = tiers[2][0] = 0;
  if (isEpub && light) gutDirectUrl(it.id, GUT_K_EPUB_LITE, tiers[0], W);
  gutDirectUrl(it.id, isEpub ? GUT_K_EPUB3_IMG : GUT_K_TXT, tiers[1], W);
  const char* api = isEpub ? it.epub : it.txt;
  if (api[0] && strcmp(api, tiers[1]) != 0 && (int)strlen(api) < W) strcpy(tiers[2], api);
  return tiers[0][0] ? 0 : 1;
}

// ---------------------------------------------------------------------
// 2. Registry unduhan
// ---------------------------------------------------------------------
#define GUT_REG_N 12
enum { GUT_S_NONE = 0, GUT_S_RUN, GUT_S_VERIFY, GUT_S_DONE, GUT_S_FAIL };
enum { GUT_BEGIN_OK = 0, GUT_BEGIN_SAME, GUT_BEGIN_OTHER };

struct GutReg { uint32_t id; uint8_t st; };

static inline bool gutRegIsActive(uint8_t st) { return st == GUT_S_RUN || st == GUT_S_VERIFY; }

static inline GutReg* gutRegFind(GutReg* r, uint32_t id) {
  for (int i = 0; i < GUT_REG_N; i++) if (r[i].id == id && r[i].st != GUT_S_NONE) return &r[i];
  return nullptr;
}

static inline bool gutRegActive(GutReg* r, uint32_t id) {
  GutReg* e = gutRegFind(r, id);
  return e && gutRegIsActive(e->st);
}

// Mulai unduhan id. SAME = buku ini sudah berjalan; OTHER = ada unduhan buku lain yang berjalan (satu pekerjaan sekaligus).
static inline int gutRegBegin(GutReg* r, uint32_t id) {
  if (!id) return GUT_BEGIN_OTHER;
  if (gutRegActive(r, id)) return GUT_BEGIN_SAME;
  for (int i = 0; i < GUT_REG_N; i++) if (r[i].st != GUT_S_NONE && gutRegIsActive(r[i].st)) return GUT_BEGIN_OTHER;
  GutReg* e = gutRegFind(r, id);
  if (!e) {                                                  // slot kosong, kalau tidak ada: timpa yang sudah selesai/gagal
    for (int i = 0; i < GUT_REG_N && !e; i++) if (r[i].st == GUT_S_NONE) e = &r[i];
    for (int i = 0; i < GUT_REG_N && !e; i++) if (r[i].st == GUT_S_FAIL) e = &r[i];
    if (!e) e = &r[0];
  }
  e->id = id; e->st = GUT_S_RUN;
  return GUT_BEGIN_OK;
}

static inline void gutRegEnd(GutReg* r, uint32_t id, uint8_t st) {
  GutReg* e = gutRegFind(r, id);
  if (e) e->st = st;
}

// ---------------------------------------------------------------------
// 3. CRC32 (IEEE, sama dgn zlib). Bisa dilanjutkan: crc = gutCrc32(crc, blok, n) berulang, mulai dari 0.
// ---------------------------------------------------------------------
static inline uint32_t gutCrc32(uint32_t crc, const uint8_t* p, size_t n) {
  static const uint32_t T[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
  crc = ~crc;
  while (n--) { crc ^= *p++; crc = (crc >> 4) ^ T[crc & 15]; crc = (crc >> 4) ^ T[crc & 15]; }
  return ~crc;
}

// ---------------------------------------------------------------------
// 4. Struktur ZIP / EPUB
// ---------------------------------------------------------------------
static inline bool gutZipHasPK(const uint8_t* h, int n) { return n >= 4 && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4; }
static inline uint32_t gutLe32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint32_t gutLe16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

// tail = tn byte TERAKHIR berkas (berkas sepanjang fsize). Cari End-Of-Central-Directory yang masuk akal.
// Berkas terpotong / ujungnya rusak -> false.
static inline bool gutZipEocd(const uint8_t* tail, int tn, uint32_t fsize, uint32_t* cdOff, uint32_t* cdSize) {
  if (tn < 22 || (uint32_t)tn > fsize) return false;
  for (int i = tn - 22; i >= 0; i--) {
    const uint8_t* t = tail + i;
    if (t[0] != 'P' || t[1] != 'K' || t[2] != 5 || t[3] != 6) continue;
    uint32_t cs = gutLe32(t + 12), co = gutLe32(t + 16), cl = gutLe16(t + 20);
    uint32_t pos = fsize - (uint32_t)tn + (uint32_t)i;               // posisi EOCD di berkas
    if ((uint32_t)i + 22u + cl > (uint32_t)tn) continue;             // komentar melewati ujung berkas
    if (cs == 0 || (uint64_t)co + cs > pos) continue;               // Central Directory harus ada & berada sebelum EOCD
    if (cdOff) *cdOff = co;
    if (cdSize) *cdSize = cs;
    return true;
  }
  return false;
}

static inline bool gutZipCdSig(const uint8_t* s) { return s[0] == 'P' && s[1] == 'K' && s[2] == 1 && s[3] == 2; }

// ---------------------------------------------------------------------
// 5. Sumber ke-2: OPDS resmi Gutenberg (XML)
//    https://www.gutenberg.org/ebooks/search.opds/?query=...&sort_order=...&start_index=...
//    Pembaca ini hanya mengambil nomor buku, judul, penulis. URL unduh TIDAK diambil dari feed:
//    dibangun langsung dari nomor buku (gutDirectUrl).
// ---------------------------------------------------------------------
#define GUT_OPDS_PAGE 25

static inline void gutBuildOpdsQuery(char* out, int cap, const char* query, int sortMode, int page) {
  int n = snprintf(out, (size_t)cap, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=%s", sortMode == 1 ? "release_date" : "downloads");
  if (query && query[0] && n < cap - 8) {
    char enc[120]; gutUrlEncode(query, enc, sizeof(enc));
    n += snprintf(out + n, (size_t)(cap - n), "&query=%s", enc);
  }
  if (page > 1 && n < cap - 8) n += snprintf(out + n, (size_t)(cap - n), "&start_index=%d", (page - 1) * GUT_OPDS_PAGE + 1);
}

// isi <tag ...>...</tag> pertama di [from,to) -> out (entitas XML dasar didekode). Kembalikan panjang, -1 kalau tak ada.
static inline int gutXmlText(const char* from, const char* to, const char* tag, char* out, int cap) {
  char open[24]; snprintf(open, sizeof(open), "<%s", tag);
  size_t ol = strlen(open);
  const char* p = from;
  for (;;) {
    p = strstr(p, open);
    if (!p || p + ol >= to) return -1;
    if (p[ol] == '>' || p[ol] == ' ') break;                          // bukan <titleX
    p += ol;
  }
  const char* gt = (const char*)memchr(p, '>', (size_t)(to - p));
  if (!gt || *(gt - 1) == '/') return -1;
  gt++;
  char close[24]; snprintf(close, sizeof(close), "</%s>", tag);
  const char* en = strstr(gt, close);
  if (!en || en > to) return -1;
  int n = 0; bool tr = false;
  for (const char* q = gt; q < en;) {
    if (*q == '&') {
      uint32_t cp = 0; int adv = 0;
      if (!strncmp(q, "&amp;", 5)) { cp = '&'; adv = 5; }
      else if (!strncmp(q, "&lt;", 4)) { cp = '<'; adv = 4; }
      else if (!strncmp(q, "&gt;", 4)) { cp = '>'; adv = 4; }
      else if (!strncmp(q, "&quot;", 6)) { cp = '"'; adv = 6; }
      else if (!strncmp(q, "&apos;", 6)) { cp = '\''; adv = 6; }
      else if (q[1] == '#') {
        const char* d = q + 2; int base = 10;
        if (*d == 'x' || *d == 'X') { base = 16; d++; }
        char* endp = nullptr; unsigned long v = strtoul(d, &endp, base);
        if (endp && *endp == ';' && v > 0 && v < 0x110000) { cp = (uint32_t)v; adv = (int)(endp - q) + 1; }
      }
      if (adv) { gutUtf8Put(out, cap, &n, cp, &tr); q += adv; continue; }
    }
    char c = *q++;
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (n + 1 < cap) out[n++] = c; else tr = true;
  }
  (void)tr;
  gutTrimUtf8(out, n);
  return (int)strlen(out);
}

// nomor setelah "/ebooks/" yang diawali angka (lewati /ebooks/author/12, /ebooks/search.opds/)
static inline uint32_t gutXmlBookId(const char* from, const char* to) {
  const char* p = from;
  while ((p = strstr(p, "/ebooks/")) != nullptr && p < to) {
    p += 8;
    if (*p >= '0' && *p <= '9') return (uint32_t)strtoul(p, nullptr, 10);
  }
  return 0;
}

// x = XML berakhiran NUL. Kembalikan jumlah buku (>=0) atau -1 bila bukan feed Atom/OPDS.
// *hasNext = ada halaman berikut. Entri tanpa nomor buku (mis. "Next Page") dilewati.
static inline int gutParseOpds(const char* x, int len, GutItem* out, int maxOut, int* hasNext) {
  (void)len;
  if (hasNext) *hasNext = 0;
  if (!strstr(x, "<feed")) return -1;
  if (hasNext && (strstr(x, "rel=\"next\"") || strstr(x, "Next Page"))) *hasNext = 1;
  int n = 0;
  const char* p = x;
  while (n < maxOut && (p = strstr(p, "<entry")) != nullptr) {
    const char* q = strstr(p, "</entry>");
    if (!q) break;                                                    // entri terakhir terpotong: buang
    GutItem it; memset(&it, 0, sizeof(it));
    it.id = gutXmlBookId(p, q);
    gutXmlText(p, q, "title", it.title, sizeof(it.title));
    gutXmlText(p, q, "content", it.author, sizeof(it.author));
    gutFlipName(it.author, GUT_AUTH_LEN);
    if (it.id && it.title[0]) {
      gutDirectUrl(it.id, GUT_K_EPUB3_IMG, it.epub, GUT_URL_LEN);
      gutDirectUrl(it.id, GUT_K_TXT, it.txt, GUT_URL_LEN);
      out[n++] = it;
    }
    p = q + 8;
  }
  return n;
}

#endif
