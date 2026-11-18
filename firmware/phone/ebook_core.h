// =====================================================================
// ebook_core.h -- mesin buku (TXT + EPUB) untuk app "Buku" Accretion Phone / NyxOS
// =====================================================================
// Kode murni C/C++ tanpa Arduino / LovyanGFX / SD, sehingga seluruh logika (inflate,
// ZIP, parser EPUB, konversi HTML -> teks, tata letak halaman) bisa diuji di komputer
// lewat tests_ebook/. Antarmuka layar & SD ada di ebook_renphone.ino.
//
// Isi:
//   1. Inflate (RFC 1951) mandiri -- tanpa miniz/zlib, tanpa ROM ESP.
//   2. Pembaca ZIP (central directory, method 0 dan 8). ZIP64 & terenkripsi ditolak.
//   3. Konversi UTF-8/UTF-16/Latin-1 -> ASCII (font bawaan layar hanya ASCII).
//   4. HTML/XHTML -> teks polos berparagraf.  TXT -> teks (deteksi baris keras / reflow).
//   5. Parser EPUB: container.xml -> OPF -> spine -> daftar isi (NCX / nav).
//   6. Tata letak: pembungkus kata + paginasi memakai tabel lebar huruf.
//
// FORMAT TEKS KANONIK (keluaran 4, masukan 6):
//   * '\n'  = akhir paragraf/baris.  Baris kosong ("\n\n") = jarak antar bagian.
//   * '\x01' di awal baris = judul (dirender di tengah). Tidak ada kode kontrol lain.
//   * Hanya ASCII 0x20..0x7E, spasi tunggal (tidak ada spasi ganda / spasi di ujung baris).
//
// ATURAN PROYEK: semua alokasi lewat EB_ALLOC/EB_REALLOC/EB_FREE (di firmware diarahkan
// ke PSRAM). Tidak ada String. Semua tipe struct didefinisikan di sini (bukan di .ino)
// supaya tidak kena bug prototype otomatis arduino-cli.
// =====================================================================
#ifndef EBOOK_CORE_H
#define EBOOK_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifndef EB_ALLOC
#define EB_ALLOC(n)      malloc(n)
#define EB_REALLOC(p, n) realloc((p), (n))
#define EB_FREE(p)       free(p)
#endif
#ifndef EB_YIELD
#define EB_YIELD()       ((void)0)
#endif

// ---- kode galat ----
#define EB_OK          0
#define EB_E_IO       -1   // gagal baca file
#define EB_E_NOMEM    -2   // RAM/PSRAM tidak cukup
#define EB_E_ZIP      -3   // bukan ZIP / rusak
#define EB_E_ZIP64    -4   // ZIP64 / terenkripsi (tidak didukung)
#define EB_E_INFL     -5   // data terkompresi rusak
#define EB_E_NOCONT   -6   // container.xml tidak ada
#define EB_E_NOOPF    -7   // berkas OPF tidak ada
#define EB_E_EMPTY    -8   // buku tanpa bab
#define EB_E_TOOBIG   -9   // ukuran melebihi batas
#define EB_E_NOENT   -10   // berkas bab tidak ada di arsip

static inline const char* ebErrStr(int e) {
  switch (e) {
    case EB_OK:        return "OK";
    case EB_E_IO:      return "Gagal membaca file";
    case EB_E_NOMEM:   return "RAM tidak cukup";
    case EB_E_ZIP:     return "EPUB rusak";
    case EB_E_ZIP64:   return "EPUB tidak didukung";
    case EB_E_INFL:    return "Isi EPUB rusak/DRM";
    case EB_E_NOCONT:  return "EPUB tanpa container";
    case EB_E_NOOPF:   return "EPUB tanpa OPF";
    case EB_E_EMPTY:   return "Buku kosong";
    case EB_E_TOOBIG:  return "Bab terlalu besar";
    case EB_E_NOENT:   return "Bab tidak ditemukan";
    default:           return "Galat tak dikenal";
  }
}

// =====================================================================
// 1. INFLATE (turunan algoritma "puff" -- zlib/contrib/puff, domain publik)
// =====================================================================
struct EbInfl {
  const uint8_t* in; uint32_t inLen, inPos;
  uint32_t bitBuf; int bitCnt;
  uint8_t* out; uint32_t outCap, outPos;
  int err;
};
struct EbHuff { uint16_t count[16]; uint16_t symbol[288]; };

static inline int ebBits(EbInfl* s, int need) {
  uint32_t v = s->bitBuf;
  while (s->bitCnt < need) {
    if (s->inPos >= s->inLen) { s->err = 1; return 0; }
    v |= (uint32_t)s->in[s->inPos++] << s->bitCnt;
    s->bitCnt += 8;
  }
  s->bitBuf = v >> need;
  s->bitCnt -= need;
  return (int)(v & ((1u << need) - 1u));
}

static int ebDecode(EbInfl* s, const EbHuff* h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++) {
    code |= ebBits(s, 1);
    if (s->err) return -1;
    int count = h->count[len];
    if (code - count < first) return h->symbol[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  return -1;
}

static int ebConstruct(EbHuff* h, const uint8_t* length, int n) {
  int offs[16];
  for (int len = 0; len <= 15; len++) h->count[len] = 0;
  for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
  if (h->count[0] == n) return 0;
  int left = 1;
  for (int len = 1; len <= 15; len++) {
    left <<= 1; left -= h->count[len];
    if (left < 0) return left;
  }
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
  for (int sym = 0; sym < n; sym++)
    if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
  return left;
}

static const uint16_t ebLBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t  ebLExt[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t ebDBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t  ebDExt[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int ebCodes(EbInfl* s, const EbHuff* lc, const EbHuff* dc) {
  for (;;) {
    int sym = ebDecode(s, lc);
    if (sym < 0) return -10;
    if (sym < 256) {
      if (s->outPos >= s->outCap) return -12;
      s->out[s->outPos++] = (uint8_t)sym;
    } else if (sym == 256) {
      return 0;
    } else {
      sym -= 257;
      if (sym >= 29) return -10;
      int len = ebLBase[sym] + ebBits(s, ebLExt[sym]);
      int ds = ebDecode(s, dc);
      if (ds < 0 || ds >= 30) return -11;
      uint32_t dist = (uint32_t)ebDBase[ds] + (uint32_t)ebBits(s, ebDExt[ds]);
      if (s->err) return -13;
      if (dist > s->outPos) return -11;
      if (s->outPos + (uint32_t)len > s->outCap) return -12;
      uint8_t* o = s->out + s->outPos;
      const uint8_t* src = o - dist;
      for (int i = 0; i < len; i++) o[i] = src[i];
      s->outPos += (uint32_t)len;
    }
  }
}

static int ebStored(EbInfl* s) {
  s->bitBuf = 0; s->bitCnt = 0;
  if (s->inPos + 4 > s->inLen) return -1;
  uint32_t len = s->in[s->inPos] | ((uint32_t)s->in[s->inPos + 1] << 8);
  uint32_t nlen = s->in[s->inPos + 2] | ((uint32_t)s->in[s->inPos + 3] << 8);
  s->inPos += 4;
  if (len != (~nlen & 0xFFFFu)) return -2;
  if (s->inPos + len > s->inLen) return -1;
  if (s->outPos + len > s->outCap) return -12;
  memcpy(s->out + s->outPos, s->in + s->inPos, len);
  s->inPos += len; s->outPos += len;
  return 0;
}

static int ebFixed(EbInfl* s) {
  EbHuff lc, dc; uint8_t lengths[288];
  int sym = 0;
  for (; sym < 144; sym++) lengths[sym] = 8;
  for (; sym < 256; sym++) lengths[sym] = 9;
  for (; sym < 280; sym++) lengths[sym] = 7;
  for (; sym < 288; sym++) lengths[sym] = 8;
  ebConstruct(&lc, lengths, 288);
  for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
  ebConstruct(&dc, lengths, 30);
  return ebCodes(s, &lc, &dc);
}

static int ebDynamic(EbInfl* s) {
  static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
  EbHuff lc, dc; uint8_t lengths[320];
  int nlen = ebBits(s, 5) + 257, ndist = ebBits(s, 5) + 1, ncode = ebBits(s, 4) + 4;
  if (s->err) return -1;
  if (nlen > 286 || ndist > 30) return -3;
  int index;
  for (index = 0; index < ncode; index++) lengths[order[index]] = (uint8_t)ebBits(s, 3);
  for (; index < 19; index++) lengths[order[index]] = 0;
  if (s->err) return -1;
  if (ebConstruct(&lc, lengths, 19) != 0) return -4;
  index = 0;
  while (index < nlen + ndist) {
    int symbol = ebDecode(s, &lc);
    if (symbol < 0) return -5;
    if (symbol < 16) {
      lengths[index++] = (uint8_t)symbol;
    } else {
      int len = 0;
      if (symbol == 16) {
        if (index == 0) return -5;
        len = lengths[index - 1];
        symbol = 3 + ebBits(s, 2);
      } else if (symbol == 17) symbol = 3 + ebBits(s, 3);
      else symbol = 11 + ebBits(s, 7);
      if (s->err) return -1;
      if (index + symbol > nlen + ndist) return -6;
      while (symbol--) lengths[index++] = (uint8_t)len;
    }
  }
  if (lengths[256] == 0) return -9;
  int err = ebConstruct(&lc, lengths, nlen);
  if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -7;
  err = ebConstruct(&dc, lengths + nlen, ndist);
  if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -8;
  return ebCodes(s, &lc, &dc);
}

// Inflate aliran deflate mentah. out harus cukup besar (outCap). 0 = sukses.
static int ebInflate(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outCap, uint32_t* outLen) {
  EbInfl s; s.in = in; s.inLen = inLen; s.inPos = 0; s.bitBuf = 0; s.bitCnt = 0;
  s.out = out; s.outCap = outCap; s.outPos = 0; s.err = 0;
  int last, type, err = 0;
  do {
    last = ebBits(&s, 1);
    type = ebBits(&s, 2);
    if (s.err) { err = -1; break; }
    if (type == 0) err = ebStored(&s);
    else if (type == 1) err = ebFixed(&s);
    else if (type == 2) err = ebDynamic(&s);
    else err = -1;
    if (err) break;
  } while (!last);
  *outLen = s.outPos;
  return err;
}

// =====================================================================
// 2. ZIP
// =====================================================================
struct EbRd {                        // sumber data akses-acak (file SD / memori)
  bool (*read)(void* ctx, uint32_t off, void* dst, uint32_t n);
  void* ctx;
  uint32_t size;
};
struct EbZipEnt { uint32_t lho, csize, usize, nameOff; uint16_t method, nameLen; };
struct EbZip { EbRd rd; EbZipEnt* ent; uint32_t n; char* pool; uint32_t poolLen; };

static inline uint32_t ebU16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t ebU32(const uint8_t* p) { return ebU16(p) | (ebU16(p + 2) << 16); }

// baca n byte dalam potongan 16 KB (memberi napas ke sistem)
static bool ebRdAll(const EbRd* rd, uint32_t off, uint8_t* dst, uint32_t n) {
  while (n) {
    uint32_t c = n > 16384u ? 16384u : n;
    if (!rd->read(rd->ctx, off, dst, c)) return false;
    off += c; dst += c; n -= c;
    EB_YIELD();
  }
  return true;
}

static void ebZipClose(EbZip* z) {
  if (z->ent) EB_FREE(z->ent);
  if (z->pool) EB_FREE(z->pool);
  z->ent = nullptr; z->pool = nullptr; z->n = 0; z->poolLen = 0;
}

static int ebZipOpen(EbZip* z, EbRd rd) {
  memset(z, 0, sizeof(*z));
  z->rd = rd;
  if (rd.size < 22) return EB_E_ZIP;
  uint32_t tail = rd.size < 65557u ? rd.size : 65557u;
  uint8_t* tb = (uint8_t*)EB_ALLOC(tail);
  if (!tb) return EB_E_NOMEM;
  if (!ebRdAll(&rd, rd.size - tail, tb, tail)) { EB_FREE(tb); return EB_E_IO; }
  int64_t at = -1;
  for (int64_t i = (int64_t)tail - 22; i >= 0; i--) {
    if (tb[i] == 0x50 && tb[i + 1] == 0x4B && tb[i + 2] == 5 && tb[i + 3] == 6) { at = i; break; }
  }
  if (at < 0) { EB_FREE(tb); return EB_E_ZIP; }
  uint32_t total = ebU16(tb + at + 10), cdSize = ebU32(tb + at + 12), cdOff = ebU32(tb + at + 16);
  EB_FREE(tb);
  if (total == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return EB_E_ZIP64;
  if (total == 0 || (uint64_t)cdOff + cdSize > rd.size) return EB_E_ZIP;
  if (cdSize > 8u * 1024u * 1024u) return EB_E_TOOBIG;
  uint8_t* cd = (uint8_t*)EB_ALLOC(cdSize);
  z->ent = (EbZipEnt*)EB_ALLOC((size_t)total * sizeof(EbZipEnt));
  uint32_t poolCap = cdSize + total + 8;
  z->pool = (char*)EB_ALLOC(poolCap);
  if (!cd || !z->ent || !z->pool) { if (cd) EB_FREE(cd); ebZipClose(z); return EB_E_NOMEM; }
  if (!ebRdAll(&rd, cdOff, cd, cdSize)) { EB_FREE(cd); ebZipClose(z); return EB_E_IO; }
  uint32_t p = 0, n = 0;
  while (n < total && p + 46 <= cdSize) {
    const uint8_t* e = cd + p;
    if (ebU32(e) != 0x02014B50u) break;
    uint32_t flags = ebU16(e + 8), method = ebU16(e + 10);
    uint32_t csize = ebU32(e + 20), usize = ebU32(e + 24);
    uint32_t nl = ebU16(e + 28), xl = ebU16(e + 30), cl = ebU16(e + 32), lho = ebU32(e + 42);
    if ((uint64_t)p + 46 + nl + xl + cl > cdSize) break;
    if (z->poolLen + nl + 1 > poolCap) break;
    EbZipEnt* d = &z->ent[n];
    d->lho = lho; d->csize = csize; d->usize = usize;
    d->method = (flags & 1u) ? 0xFFFFu : (uint16_t)method;   // terenkripsi -> tak didukung
    d->nameOff = z->poolLen; d->nameLen = (uint16_t)nl;
    memcpy(z->pool + z->poolLen, e + 46, nl);
    z->pool[z->poolLen + nl] = 0;
    z->poolLen += nl + 1;
    n++; p += 46 + nl + xl + cl;
  }
  EB_FREE(cd);
  z->n = n;
  if (n == 0) { ebZipClose(z); return EB_E_ZIP; }
  return EB_OK;
}

static inline char ebLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool ebEqCI(const char* a, const char* b, int n) {
  for (int i = 0; i < n; i++) if (ebLower(a[i]) != ebLower(b[i])) return false;
  return true;
}

// cari entri; cocok persis dulu, lalu tak peka huruf besar/kecil. -1 = tidak ada.
static int ebZipFind(const EbZip* z, const char* name) {
  for (uint32_t i = 0; i < z->n; i++) if (strcmp(z->pool + z->ent[i].nameOff, name) == 0) return (int)i;
  int nl = (int)strlen(name);
  for (uint32_t i = 0; i < z->n; i++)
    if (z->ent[i].nameLen == nl && ebEqCI(z->pool + z->ent[i].nameOff, name, nl)) return (int)i;
  return -1;
}

// Ekstrak entri ke buffer baru (diakhiri NUL). NULL kalau gagal; *err berisi sebabnya.
static uint8_t* ebZipExtract(const EbZip* z, int idx, uint32_t maxLen, uint32_t* outLen, int* err) {
  *err = EB_OK; *outLen = 0;
  if (idx < 0 || (uint32_t)idx >= z->n) { *err = EB_E_NOENT; return nullptr; }
  const EbZipEnt* e = &z->ent[idx];
  if (e->method == 0xFFFFu || (e->method != 0 && e->method != 8)) { *err = EB_E_ZIP64; return nullptr; }
  if (e->usize > maxLen) { *err = EB_E_TOOBIG; return nullptr; }
  uint8_t lh[30];
  if ((uint64_t)e->lho + 30 > z->rd.size || !z->rd.read(z->rd.ctx, e->lho, lh, 30)) { *err = EB_E_IO; return nullptr; }
  if (ebU32(lh) != 0x04034B50u) { *err = EB_E_ZIP; return nullptr; }
  uint64_t dataOff = (uint64_t)e->lho + 30 + ebU16(lh + 26) + ebU16(lh + 28);
  if (dataOff + e->csize > z->rd.size) { *err = EB_E_ZIP; return nullptr; }
  uint8_t* out = (uint8_t*)EB_ALLOC((size_t)e->usize + 1);
  if (!out) { *err = EB_E_NOMEM; return nullptr; }
  if (e->method == 0) {
    if (e->csize != e->usize || !ebRdAll(&z->rd, (uint32_t)dataOff, out, e->usize)) { EB_FREE(out); *err = EB_E_IO; return nullptr; }
    *outLen = e->usize;
  } else {
    uint8_t* comp = (uint8_t*)EB_ALLOC((size_t)e->csize + 1);
    if (!comp) { EB_FREE(out); *err = EB_E_NOMEM; return nullptr; }
    if (!ebRdAll(&z->rd, (uint32_t)dataOff, comp, e->csize)) { EB_FREE(comp); EB_FREE(out); *err = EB_E_IO; return nullptr; }
    uint32_t got = 0;
    int r = ebInflate(comp, e->csize, out, e->usize, &got);
    EB_FREE(comp);
    if (r != 0) { EB_FREE(out); *err = EB_E_INFL; return nullptr; }
    *outLen = got;
  }
  out[*outLen] = 0;
  return out;
}

// =====================================================================
// 3. TRANSLITERASI -> ASCII
// =====================================================================
static const char* const ebL1[96] = {
  " ", "!", "c", "GBP", "", "Y", "|", "S",
  "", "(c)", "a", "<<", "", "", "(R)", "-",
  "o", "+/-", "2", "3", "'", "u", "P", ".",
  "", "1", "o", ">>", "1/4", "1/2", "3/4", "?",
  "A", "A", "A", "A", "A", "A", "AE", "C",
  "E", "E", "E", "E", "I", "I", "I", "I",
  "D", "N", "O", "O", "O", "O", "O", "x",
  "O", "U", "U", "U", "U", "Y", "TH", "ss",
  "a", "a", "a", "a", "a", "a", "ae", "c",
  "e", "e", "e", "e", "i", "i", "i", "i",
  "d", "n", "o", "o", "o", "o", "o", "/",
  "o", "u", "u", "u", "u", "y", "th", "y"
};
static const char* const ebLA[128] = {
  "A", "a", "A", "a", "A", "a", "C", "c",
  "C", "c", "C", "c", "C", "c", "D", "d",
  "D", "d", "E", "e", "E", "e", "E", "e",
  "E", "e", "E", "e", "G", "g", "G", "g",
  "G", "g", "G", "g", "H", "h", "H", "h",
  "I", "i", "I", "i", "I", "i", "I", "i",
  "I", "i", "IJ", "ij", "J", "j", "K", "k",
  "k", "L", "l", "L", "l", "L", "l", "L",
  "l", "L", "l", "N", "n", "N", "n", "N",
  "n", "n", "N", "n", "O", "o", "O", "o",
  "O", "o", "OE", "oe", "R", "r", "R", "r",
  "R", "r", "S", "s", "S", "s", "S", "s",
  "S", "s", "T", "t", "T", "t", "T", "t",
  "U", "u", "U", "u", "U", "u", "U", "u",
  "U", "u", "U", "u", "W", "w", "Y", "y",
  "Y", "Z", "z", "Z", "z", "Z", "z", "s"
};

// Tulis pengganti ASCII untuk codepoint ke d (maks 4 byte). Mengembalikan panjang, 0 = buang, -1 = tak dikenal.
static int ebCpToAscii(uint32_t cp, char* d) {
  const char* s = nullptr;
  if (cp < 0x80) { d[0] = (char)cp; return 1; }
  if (cp >= 0xA0 && cp <= 0xFF) s = ebL1[cp - 0xA0];
  else if (cp >= 0x100 && cp <= 0x17F) s = ebLA[cp - 0x100];
  else {
    switch (cp) {
      case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: s = "-"; break;
      case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: case 0x02BC: s = "'"; break;
      case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: s = "\""; break;
      case 0x2022: case 0x25CF: case 0x25AA: case 0x2023: s = "*"; break;
      case 0x2026: s = "..."; break;
      case 0x2039: s = "<"; break;
      case 0x203A: s = ">"; break;
      case 0x2044: s = "/"; break;
      case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006: case 0x2007: case 0x2008:
      case 0x2009: case 0x200A: case 0x202F: case 0x205F: case 0x3000: s = " "; break;
      case 0x2028: case 0x2029: s = "\n"; break;
      case 0x200B: case 0x200C: case 0x200D: case 0x200E: case 0x200F: case 0xFEFF: case 0x2060: case 0x202A:
      case 0x202B: case 0x202C: case 0x202D: case 0x202E: s = ""; break;
      case 0x20AC: s = "EUR"; break;
      case 0x2122: s = "TM"; break;
      case 0x2190: s = "<-"; break;
      case 0x2192: s = "->"; break;
      case 0xFB00: s = "ff"; break;
      case 0xFB01: s = "fi"; break;
      case 0xFB02: s = "fl"; break;
      case 0xFB03: s = "ffi"; break;
      case 0xFB04: s = "ffl"; break;
      default: return -1;
    }
  }
  int n = 0;
  while (s[n] && n < 4) { d[n] = s[n]; n++; }
  return n;
}

// ---------------------------------------------------------------------
// v2: MODE UNICODE. ebKeepUni = 1 -> teks kanonik menyimpan UTF-8 asli untuk codepoint yang
// dicakup font Unicode (ebUniKeep); sisanya tetap dilipat ke ASCII seperti dulu. Pelipatan
// saat TAMPIL (font tanpa glyph) dilakukan oleh ebCharW / ebRunText. Bawaan 0 = perilaku lama.
// ebUniKeep HARUS sama dengan cakupan font VLW buatan tools/make_vlw.py.
// ---------------------------------------------------------------------
static uint8_t ebKeepUni = 0;
static bool ebUniKeep(uint32_t cp) {
  if (cp <= 0xA0 || cp == 0xAD) return false;          // ASCII, NBSP (-> spasi), soft hyphen (-> buang)
  if (cp <= 0x17F) return true;                        // Latin-1 Supplement + Latin Extended-A
  if (cp >= 0x384 && cp <= 0x3CE) return cp != 0x38B && cp != 0x38D && cp != 0x3A2;   // Yunani modern (+ tonos)
  if (cp >= 0x400 && cp <= 0x45F) return true;         // Kiril dasar (Rusia, Ukraina, Serbia, Bulgaria, Makedonia, ...)
  if (cp == 0x490 || cp == 0x491) return true;         // Ukraina Ґ ґ
  switch (cp) {
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015:
    case 0x2018: case 0x2019: case 0x201A: case 0x201C: case 0x201D: case 0x201E:
    case 0x2020: case 0x2021: case 0x2022: case 0x2026: case 0x2030: case 0x2039: case 0x203A:
    case 0x20AC: case 0x2122: case 0x2190: case 0x2192: case 0x2212:
      return true;
    default: return false;
  }
}
// tulis codepoint sebagai UTF-8 ke d (maks 4 byte); mengembalikan panjang
static int ebEncUtf8(uint32_t cp, char* d) {
  if (cp < 0x80) { d[0] = (char)cp; return 1; }
  if (cp < 0x800) { d[0] = (char)(0xC0 | (cp >> 6)); d[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) { d[0] = (char)(0xE0 | (cp >> 12)); d[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
  d[0] = (char)(0xF0 | (cp >> 18)); d[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); d[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

// Windows-1252 0x80..0x9F -> codepoint Unicode (0 = tak terdefinisi)
static uint32_t ebCp1252(uint8_t b) {
  static const uint16_t t[32] = {
    0x20AC,0,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0,0x017D,0,
    0,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0,0x017E,0x0178 };
  return t[b - 0x80];
}

// Decode satu codepoint UTF-8 di p (n byte tersisa). Mengembalikan jumlah byte terpakai;
// 0 = urutan terpotong di akhir buffer; -1 (lewat *bad) = byte tidak valid (pakai Latin-1/1252).
static int ebUtf8(const uint8_t* p, uint32_t n, uint32_t* cp, bool* bad) {
  *bad = false;
  uint8_t c = p[0];
  if (c < 0x80) { *cp = c; return 1; }
  int need; uint32_t v;
  if (c >= 0xC2 && c <= 0xDF) { need = 1; v = c & 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { need = 2; v = c & 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { need = 3; v = c & 0x07; }
  else { *bad = true; *cp = c; return 1; }
  if (n < (uint32_t)need + 1) {
    // terpotong? valid hanya jika byte yang ada semuanya kelanjutan
    for (uint32_t i = 1; i < n; i++) if ((p[i] & 0xC0) != 0x80) { *bad = true; *cp = c; return 1; }
    return 0;
  }
  for (int i = 1; i <= need; i++) {
    if ((p[i] & 0xC0) != 0x80) { *bad = true; *cp = c; return 1; }
    v = (v << 6) | (p[i] & 0x3F);
  }
  if ((need == 2 && v < 0x800) || (need == 3 && v < 0x10000) || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
    *bad = true; *cp = c; return 1;
  }
  *cp = v;
  return need + 1;
}

// =====================================================================
// 4a. PENAMPUNG KELUARAN TEKS KANONIK
// =====================================================================
struct EbOut {
  char* buf; uint32_t len, cap;
  uint8_t pendSp;      // ada spasi tertunda
  uint8_t atStart;     // belum ada teks di baris ini
  uint8_t headPend;    // judul menunggu teks pertama
  uint8_t unk;         // sedang dalam rentetan karakter tak dikenal
  uint8_t full;        // buffer penuh (keluaran terpotong)
  int (*img)(void* ctx, const char* src, int srcLen);   // v2: daftarkan gambar, hasil = indeks (-1 = lewati); null = abaikan <img>
  void* imgCtx;
};
static void ebOutInit(EbOut* o, char* buf, uint32_t cap) {
  o->buf = buf; o->len = 0; o->cap = cap;
  o->pendSp = 0; o->atStart = 1; o->headPend = 0; o->unk = 0; o->full = 0;
  o->img = nullptr; o->imgCtx = nullptr;
}
static inline void ebRaw(EbOut* o, char c) {
  if (o->len + 1 >= o->cap) { o->full = 1; return; }
  o->buf[o->len++] = c;
}
static void ebOutChar(EbOut* o, char c) {      // c = ASCII tercetak
  if (c == ' ') { if (!o->atStart) o->pendSp = 1; return; }
  if (o->atStart && o->headPend) { ebRaw(o, 1); o->headPend = 0; }
  if (o->pendSp && !o->atStart) ebRaw(o, ' ');
  o->pendSp = 0;
  ebRaw(o, c);
  o->atStart = 0;
}
static void ebOutSpace(EbOut* o) { if (!o->atStart) o->pendSp = 1; }
static void ebOutBreak(EbOut* o) {             // akhir paragraf (tidak pernah dobel)
  if (!o->atStart) { ebRaw(o, '\n'); o->atStart = 1; }
  o->pendSp = 0;
}
static void ebOutCp(EbOut* o, uint32_t cp) {
  char t[4];
  if (ebKeepUni && ebUniKeep(cp)) {                  // v2: simpan UTF-8 asli
    int k = ebEncUtf8(cp, t);
    o->unk = 0;
    ebOutChar(o, t[0]);                              // byte pertama mengurus spasi tertunda / penanda judul
    for (int i = 1; i < k; i++) ebRaw(o, t[i]);
    return;
  }
  int n = ebCpToAscii(cp, t);
  if (n < 0) { if (!o->unk) { ebOutChar(o, '?'); o->unk = 1; } return; }
  o->unk = 0;
  for (int i = 0; i < n; i++) {
    if (t[i] == '\n') ebOutBreak(o);
    else if ((uint8_t)t[i] >= 0x20 && t[i] != 0x7F) ebOutChar(o, t[i]);
  }
}

// =====================================================================
// 4b. HTML/XHTML -> TEKS
// =====================================================================
struct EbEnt { const char* n; uint16_t cp; };
static const EbEnt ebEnts[] = {
  {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"ndash",0x2013},{"mdash",0x2014},
  {"hellip",0x2026},{"lsquo",0x2018},{"rsquo",0x2019},{"ldquo",0x201C},{"rdquo",0x201D},{"laquo",0xAB},{"raquo",0xBB},
  {"copy",0xA9},{"reg",0xAE},{"trade",0x2122},{"bull",0x2022},{"middot",0xB7},{"eacute",0xE9},{"egrave",0xE8},
  {"agrave",0xE0},{"aacute",0xE1},{"acirc",0xE2},{"auml",0xE4},{"ouml",0xF6},{"uuml",0xFC},{"szlig",0xDF},
  {"ccedil",0xE7},{"ntilde",0xF1},{"iexcl",0xA1},{"iquest",0xBF},{"deg",0xB0},{"times",0xD7},{"divide",0xF7},
  {"euro",0x20AC},{"pound",0xA3},{"yen",0xA5},{"cent",0xA2},{"sect",0xA7},{"para",0xB6},{"frac12",0xBD},
  {"frac14",0xBC},{"frac34",0xBE},{"plusmn",0xB1},{"sup2",0xB2},{"sup3",0xB3},{"ecirc",0xEA},{"euml",0xEB},
  {"icirc",0xEE},{"iuml",0xEF},{"ocirc",0xF4},{"ucirc",0xFB},{"oacute",0xF3},{"iacute",0xED},{"uacute",0xFA},
  {"Eacute",0xC9},{"thinsp",0x2009},{"ensp",0x2002},{"emsp",0x2003},{"zwnj",0x200C},{"shy",0xAD},
};

// Decode entitas di in[i..] (in[i]=='&'). Mengembalikan jumlah byte terpakai (0 = bukan entitas).
// *cp = codepoint, 0 = entitas valid tapi tak dikenal (dibuang).
static uint32_t ebEntity(const uint8_t* in, uint32_t n, uint32_t i, uint32_t* cp) {
  uint32_t j = i + 1, k = 0;
  char nm[12];
  while (j < n && in[j] != ';' && k < 10 && in[j] > ' ' && in[j] != '<' && in[j] != '&') nm[k++] = (char)in[j++];
  if (j >= n || in[j] != ';' || k == 0) return 0;
  nm[k] = 0;
  *cp = 0;
  if (nm[0] == '#') {
    uint32_t v = 0; int ok = 0;
    if (nm[1] == 'x' || nm[1] == 'X') {
      for (uint32_t q = 2; q < k; q++) {
        char c = nm[q]; int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = v * 16 + (uint32_t)d; ok = 1;
        if (v > 0x10FFFF) return 0;
      }
    } else {
      for (uint32_t q = 1; q < k; q++) {
        if (nm[q] < '0' || nm[q] > '9') return 0;
        v = v * 10 + (uint32_t)(nm[q] - '0'); ok = 1;
        if (v > 0x10FFFF) return 0;
      }
    }
    if (!ok) return 0;
    if (v >= 0x80 && v <= 0x9F) v = ebCp1252((uint8_t)v);
    *cp = v;
    return j + 1 - i;
  }
  for (size_t q = 0; q < sizeof(ebEnts) / sizeof(ebEnts[0]); q++)
    if (strcmp(nm, ebEnts[q].n) == 0) { *cp = ebEnts[q].cp; break; }
  return j + 1 - i;
}

enum { EB_K_OTHER = 0, EB_K_SKIP, EB_K_BLOCK, EB_K_BR, EB_K_HR, EB_K_HEAD, EB_K_LI, EB_K_PRE, EB_K_CELL, EB_K_IMG };
static int ebTagKind(const char* nm) {
  static const char* const skip[] = {"head", "script", "style", "title", "svg", "math", "rt", "rp", nullptr};
  static const char* const blk[] = {"p", "div", "section", "article", "header", "footer", "nav", "aside", "blockquote",
    "ul", "ol", "dl", "dt", "dd", "table", "tr", "figure", "figcaption", "body", "address", "details", "summary",
    "form", "fieldset", "center", "caption", "tbody", "thead", "tfoot", "html", "main", nullptr};
  for (int i = 0; skip[i]; i++) if (strcmp(nm, skip[i]) == 0) return EB_K_SKIP;
  for (int i = 0; blk[i]; i++) if (strcmp(nm, blk[i]) == 0) return EB_K_BLOCK;
  if (strcmp(nm, "br") == 0) return EB_K_BR;
  if (strcmp(nm, "hr") == 0) return EB_K_HR;
  if (nm[0] == 'h' && nm[1] >= '1' && nm[1] <= '6' && nm[2] == 0) return EB_K_HEAD;
  if (strcmp(nm, "li") == 0) return EB_K_LI;
  if (strcmp(nm, "pre") == 0) return EB_K_PRE;
  if (strcmp(nm, "td") == 0 || strcmp(nm, "th") == 0) return EB_K_CELL;
  if (strcmp(nm, "img") == 0 || strcmp(nm, "image") == 0) return EB_K_IMG;
  return EB_K_OTHER;
}

static uint32_t ebFindStr(const uint8_t* in, uint32_t n, uint32_t from, const char* pat, uint32_t pl) {
  if (n < pl) return 0xFFFFFFFFu;
  for (uint32_t i = from; i + pl <= n; i++) {
    if (in[i] == (uint8_t)pat[0] && memcmp(in + i, pat, pl) == 0) return i;
  }
  return 0xFFFFFFFFu;
}

// cari penutup "</name" (tak peka huruf) mulai from; kembalikan indeks setelah '>' penutup, atau n.
static uint32_t ebSkipElement(const uint8_t* in, uint32_t n, uint32_t from, const char* name) {
  int nl = (int)strlen(name);
  for (uint32_t i = from; i + 2 + (uint32_t)nl < n; i++) {
    if (in[i] == '<' && in[i + 1] == '/' && ebEqCI((const char*)in + i + 2, name, nl)) {
      uint32_t j = i + 2 + (uint32_t)nl;
      while (j < n && in[j] != '>') j++;
      return j < n ? j + 1 : n;
    }
  }
  return n;
}

static int ebDecodeVal(const char* v, int vl, char* out, int cap, bool stripFrag);   // definisi di bagian XML
// Cari nilai atribut src / xlink:href / href di teks atribut mentah a[0..n). Hasil didekode (%XX, &amp;), tanpa #fragmen.
static int ebAttrFromRaw(const char* a, int n, char* out, int cap) {
  static const char* const keys[3] = { "src", "xlink:href", "href" };
  for (int k = 0; k < 3; k++) {
    int kl = (int)strlen(keys[k]);
    int i = 0;
    while (i < n) {
      while (i < n && (uint8_t)a[i] <= ' ') i++;
      int ns = i;
      while (i < n && a[i] != '=' && (uint8_t)a[i] > ' ') i++;
      int ne = i;
      while (i < n && (uint8_t)a[i] <= ' ') i++;
      if (i >= n || a[i] != '=') { if (i == ns) i++; continue; }
      i++;
      while (i < n && (uint8_t)a[i] <= ' ') i++;
      if (i >= n) break;
      int vs, ve; char q = a[i];
      if (q == '"' || q == '\'') { i++; vs = i; while (i < n && a[i] != q) i++; ve = i; if (i < n) i++; }
      else { vs = i; while (i < n && (uint8_t)a[i] > ' ') i++; ve = i; }
      if (ne - ns == kl && ebEqCI(a + ns, keys[k], kl)) return ebDecodeVal(a + vs, ve - vs, out, cap, true);
    }
  }
  if (cap > 0) out[0] = 0;
  return 0;
}

// Ubah HTML jadi teks kanonik; hasil ditambahkan ke o.
static void ebHtmlToText(const uint8_t* in, uint32_t n, EbOut* o) {
  uint32_t i = 0; int pre = 0;
  while (i < n) {
    uint8_t c = in[i];
    if (c == '<') {
      if (i + 3 < n && memcmp(in + i, "<!--", 4) == 0) {
        uint32_t e = ebFindStr(in, n, i + 4, "-->", 3); i = (e == 0xFFFFFFFFu) ? n : e + 3; continue;
      }
      if (i + 8 < n && memcmp(in + i, "<![CDATA[", 9) == 0) {
        uint32_t e = ebFindStr(in, n, i + 9, "]]>", 3);
        uint32_t te = (e == 0xFFFFFFFFu) ? n : e;
        for (uint32_t q = i + 9; q < te; q++) {
          uint8_t b = in[q];
          if (b <= ' ') ebOutSpace(o); else if (b < 0x80) ebOutChar(o, (char)b);
        }
        i = (e == 0xFFFFFFFFu) ? n : e + 3; continue;
      }
      if (i + 1 < n && (in[i + 1] == '!' || in[i + 1] == '?')) {
        while (i < n && in[i] != '>') i++;
        i++; continue;
      }
      // ---- tag biasa ----
      uint32_t j = i + 1; bool closing = false;
      if (j < n && in[j] == '/') { closing = true; j++; }
      char nm[16]; int nl = 0;
          while (j < n && in[j] > ' ' && in[j] != '>' && in[j] != '/') { if (nl < 15) nm[nl++] = ebLower((char)in[j]); j++; }
      nm[nl] = 0;
      const char* nmp = nm;
      for (int q = nl - 1; q >= 0; q--) if (nm[q] == ':') { nmp = nm + q + 1; break; }
      // lewati atribut (hormati kutipan)
      char qt = 0; bool selfClose = false; uint32_t a0 = j;
      while (j < n) {
        uint8_t d = in[j];
        if (qt) { if (d == (uint8_t)qt) qt = 0; }
        else if (d == '"' || d == '\'') qt = (char)d;
        else if (d == '>') break;
        j++;
      }
      if (j > i + 1 && j <= n && j > 0 && in[j - 1] == '/') selfClose = true;
      i = (j < n) ? j + 1 : n;
      int kind = ebTagKind(nmp);
      if (kind == EB_K_SKIP && o->img && strcmp(nmp, "svg") == 0) kind = EB_K_OTHER;   // v2: halaman sampul EPUB sering <svg><image/></svg>
      switch (kind) {
        case EB_K_IMG:
          if (!closing && o->img) {
            // atribut tag berada di in[a0..j): cari src / xlink:href / href
            char src[200]; int sl = ebAttrFromRaw((const char*)in + a0, (int)(j - a0), src, sizeof(src));
            if (sl > 0) {
              int idx = o->img(o->imgCtx, src, sl);
              if (idx >= 0) {
                ebOutBreak(o);
                char mk[16]; int mn = 0; mk[mn++] = 2;
                char dg[12]; int dn = 0; int v = idx; if (v == 0) dg[dn++] = '0';
                while (v > 0 && dn < 11) { dg[dn++] = (char)('0' + v % 10); v /= 10; }
                while (dn) mk[mn++] = dg[--dn];
                for (int q = 0; q < mn; q++) ebRaw(o, mk[q]);
                ebRaw(o, '\n'); o->atStart = 1; o->pendSp = 0;
              }
            }
          }
          break;
        case EB_K_SKIP:
          if (!closing && !selfClose) i = ebSkipElement(in, n, i, nmp);
          break;
        case EB_K_BLOCK: ebOutBreak(o); break;
        case EB_K_BR:    ebOutBreak(o); break;
        case EB_K_HR:
          ebOutBreak(o); o->headPend = 1;
          ebOutChar(o, '*'); ebOutChar(o, ' '); ebOutChar(o, '*'); ebOutChar(o, ' '); ebOutChar(o, '*');
          ebOutBreak(o); o->headPend = 0;
          break;
        case EB_K_HEAD:
          ebOutBreak(o); o->headPend = closing ? 0 : 1;
          break;
        case EB_K_LI:
          ebOutBreak(o);
          if (!closing) { ebOutChar(o, '*'); ebOutChar(o, ' '); }
          break;
        case EB_K_PRE:
          ebOutBreak(o);
          if (closing) { if (pre > 0) pre--; } else pre++;
          break;
        case EB_K_CELL: ebOutSpace(o); break;
        default: break;
      }
      continue;
    }
    if (c == '&') {
      uint32_t cp = 0;
      uint32_t used = ebEntity(in, n, i, &cp);
      if (used) { if (cp) ebOutCp(o, cp); i += used; continue; }
      ebOutChar(o, '&'); i++; continue;
    }
    if (c <= ' ') {
      if (pre && c == '\n') ebOutBreak(o); else ebOutSpace(o);
      i++; continue;
    }
    if (c < 0x80) {
      if (c != 0x7F) ebOutChar(o, (char)c);
      o->unk = 0; i++; continue;
    }
    uint32_t cp; bool bad;
    int u = ebUtf8(in + i, n - i, &cp, &bad);
    if (u == 0) { u = 1; bad = true; cp = c; }
    if (bad && cp >= 0x80 && cp <= 0x9F) { uint32_t m = ebCp1252((uint8_t)cp); cp = m ? m : 0x20; }
    ebOutCp(o, cp);
    i += (uint32_t)u;
  }
  ebOutBreak(o);
}

// Potongan HTML kecil -> satu baris teks biasa (judul bab, dst).
static void ebSnippet(const char* s, uint32_t n, char* out, int cap) {
  EbOut o; ebOutInit(&o, out, (uint32_t)cap);
  ebHtmlToText((const uint8_t*)s, n, &o);
  int w = 0;
  for (uint32_t i = 0; i < o.len; i++) {
    char c = out[i];
    if (c == 1) continue;
    if (c == '\n') c = ' ';
    if (c == ' ' && (w == 0 || out[w - 1] == ' ')) continue;
    out[w++] = c;
  }
  while (w > 0 && out[w - 1] == ' ') w--;
  out[w] = 0;
}

// =====================================================================
// 4c. TXT -> TEKS (streaming, UTF-8 / UTF-16 / Latin-1, reflow baris keras, deteksi judul)
// =====================================================================
#define EB_LEAD 64
struct EbTxt {
  EbOut* o;
  int enc;                       // 0 belum tahu, 1 utf8, 2 utf16le, 3 utf16be
  char lead[EB_LEAD]; int ll;    // awal baris (untuk deteksi judul)
  uint8_t streaming, pendSp, lastCR;
  uint32_t lineLen;
  uint32_t nb, blank, hist[64], over;   // statistik baris
};

static void ebTxtInit(EbTxt* t, EbOut* o) {
  memset(t, 0, sizeof(*t)); t->o = o;
}

static bool ebWordIs(const char* w, int wl, const char* const* list) {
  for (int i = 0; list[i]; i++) if ((int)strlen(list[i]) == wl && ebEqCI(w, list[i], wl)) return true;
  return false;
}

// Apakah satu baris pendek merupakan judul bab? ("Chapter 1", "BAB IV: Pulang", "Prolog", ...)
static bool ebIsHeading(const char* l, int n) {
  static const char* const kwNum[] = {"chapter", "bab", "bagian", "part", "book", "buku", "jilid", "volume", nullptr};
  static const char* const kwAlone[] = {"prolog", "prologue", "epilog", "epilogue", "prakata", "pendahuluan",
    "introduction", "preface", "foreword", "afterword", "kata pengantar", nullptr};
  static const char* const numWords[] = {"one","two","three","four","five","six","seven","eight","nine","ten","eleven",
    "twelve","first","second","third","last","final","satu","dua","tiga","empat","lima","enam","tujuh","delapan",
    "sembilan","sepuluh","pertama","kedua","ketiga","keempat","kelima","terakhir", nullptr};
  if (n <= 0 || n > 60) return false;
  int i = 0; while (i < n && l[i] == ' ') i++;
  int ws = i; while (i < n && ((l[i] >= 'a' && l[i] <= 'z') || (l[i] >= 'A' && l[i] <= 'Z'))) i++;
  int wl = i - ws;
  if (wl == 0) return false;
  if (ebWordIs(l + ws, wl, kwNum)) {
    while (i < n && l[i] == ' ') i++;
    int ts = i; while (i < n && ((l[i] >= '0' && l[i] <= '9') || (l[i] >= 'a' && l[i] <= 'z') || (l[i] >= 'A' && l[i] <= 'Z'))) i++;
    int tl = ts < n ? i - ts : 0;
    if (tl <= 0) return false;
    bool digits = true, roman = tl <= 7;
    for (int q = ts; q < ts + tl; q++) {
      char c = ebLower(l[q]);
      if (c < '0' || c > '9') digits = false;
      if (!(c == 'i' || c == 'v' || c == 'x' || c == 'l' || c == 'c' || c == 'd' || c == 'm')) roman = false;
    }
    if (digits || roman || ebWordIs(l + ts, tl, numWords)) return true;
    return false;
  }
  if (n <= 30) {
    // kata kunci mandiri ("Kata Pengantar" berisi spasi -> cocokkan seluruh awal baris)
    int e = n; while (e > 0 && (l[e - 1] == '.' || l[e - 1] == ':' || l[e - 1] == ' ')) e--;
    if (ebWordIs(l + ws, e - ws, kwAlone)) return true;
  }
  return false;
}

static inline void ebTxtStat(EbTxt* t, uint32_t len) {
  t->nb++;
  uint32_t b = len / 4; if (b >= 64) { t->over++; return; }
  t->hist[b]++;
}

static void ebTxtEmitLead(EbTxt* t) {
  for (int i = 0; i < t->ll; i++) ebRaw(t->o, t->lead[i]);
  t->lineLen += (uint32_t)t->ll; t->ll = 0;
}

static void ebTxtEndLine(EbTxt* t) {
  EbOut* o = t->o;
  if (!t->streaming) {
    if (t->ll == 0) { ebRaw(o, '\n'); t->blank++; }
    else {
      if (ebIsHeading(t->lead, t->ll)) ebRaw(o, 1);
      for (int i = 0; i < t->ll; i++) ebRaw(o, t->lead[i]);
      ebRaw(o, '\n');
      ebTxtStat(t, (uint32_t)t->ll);
    }
  } else {
    ebRaw(o, '\n');
    ebTxtStat(t, t->lineLen);
  }
  t->ll = 0; t->streaming = 0; t->pendSp = 0; t->lineLen = 0;
}

static void ebTxtPut(EbTxt* t, char c) {
  if (c == ' ') {
    if (!t->streaming && t->ll == 0) return;       // spasi di awal baris dibuang
    t->pendSp = 1; return;
  }
  if (t->pendSp) {
    t->pendSp = 0;
    if (!t->streaming) { t->lead[t->ll++] = ' '; if (t->ll >= EB_LEAD) { ebTxtEmitLead(t); t->streaming = 1; } }
    else { ebRaw(t->o, ' '); t->lineLen++; }
  }
  if (!t->streaming) {
    t->lead[t->ll++] = c;
    if (t->ll >= EB_LEAD) { ebTxtEmitLead(t); t->streaming = 1; }
  } else { ebRaw(t->o, c); t->lineLen++; }
}

static void ebTxtCp(EbTxt* t, uint32_t cp) {
  if (cp == '\r') { ebTxtEndLine(t); t->lastCR = 1; return; }
  if (cp == '\n') { if (t->lastCR) { t->lastCR = 0; return; } ebTxtEndLine(t); return; }
  t->lastCR = 0;
  if (cp == '\t') cp = ' ';
  if (cp < 0x20 || cp == 0x7F) return;
  char b[4];
  if (ebKeepUni && ebUniKeep(cp)) {                  // v2: simpan UTF-8 asli
    int k = ebEncUtf8(cp, b);
    t->o->unk = 0;
    for (int i = 0; i < k; i++) ebTxtPut(t, b[i]);
    return;
  }
  int n = ebCpToAscii(cp, b);
  if (n < 0) { if (!t->o->unk) { ebTxtPut(t, '?'); t->o->unk = 1; } return; }
  t->o->unk = 0;
  for (int i = 0; i < n; i++) {
    if (b[i] == '\n') ebTxtEndLine(t);
    else if ((uint8_t)b[i] >= 0x20 && b[i] != 0x7F) ebTxtPut(t, b[i]);
  }
}

// Umpan potongan byte. Mengembalikan jumlah byte yang dikonsumsi; sisa (urutan terpotong)
// harus disodorkan lagi bersama potongan berikutnya. eof=true: konsumsi semuanya.
static uint32_t ebTxtFeed(EbTxt* t, const uint8_t* p, uint32_t n, bool eof) {
  uint32_t i = 0;
  if (t->enc == 0) {
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { t->enc = 1; i = 3; }
    else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { t->enc = 2; i = 2; }
    else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { t->enc = 3; i = 2; }
    else t->enc = 1;
  }
  if (t->enc == 1) {
    while (i < n) {
      uint32_t cp; bool bad;
      int u = ebUtf8(p + i, n - i, &cp, &bad);
      if (u == 0) {
        if (!eof) break;
        u = 1; cp = p[i]; bad = true;
      }
      if (bad && cp >= 0x80 && cp <= 0x9F) { uint32_t m = ebCp1252((uint8_t)cp); cp = m ? m : 0x20; }
      ebTxtCp(t, cp);
      i += (uint32_t)u;
    }
  } else {
    while (i + 1 < n) {
      uint32_t u0 = (t->enc == 2) ? ebU16(p + i) : (((uint32_t)p[i] << 8) | p[i + 1]);
      if (u0 >= 0xD800 && u0 <= 0xDBFF) {
        if (i + 3 >= n) { if (!eof) break; ebTxtCp(t, 0xFFFD); i += 2; continue; }
        uint32_t u1 = (t->enc == 2) ? ebU16(p + i + 2) : (((uint32_t)p[i + 2] << 8) | p[i + 3]);
        if (u1 >= 0xDC00 && u1 <= 0xDFFF) { ebTxtCp(t, 0x10000 + ((u0 - 0xD800) << 10) + (u1 - 0xDC00)); i += 4; continue; }
        ebTxtCp(t, 0xFFFD); i += 2; continue;
      }
      ebTxtCp(t, u0); i += 2;
    }
    if (eof && i < n) i = n;
  }
  return i;
}

// Selesai: tutup baris terakhir, putuskan reflow, rapikan baris kosong. Memakai o->buf di tempat.
static void ebTxtFinish(EbTxt* t) {
  EbOut* o = t->o;
  if (t->streaming || t->ll > 0) ebTxtEndLine(t);
  char* b = o->buf; uint32_t len = o->len;

  // ---- putuskan apakah file berbaris keras (hard-wrap) ----
  bool wrapped = false;
  if (t->nb >= 12 && t->blank * 10 >= t->nb) {
    uint32_t acc = 0, p95 = 0, target = (t->nb * 95 + 99) / 100;
    for (int k = 0; k < 64; k++) { acc += t->hist[k]; if (acc >= target) { p95 = (uint32_t)k * 4 + 3; break; } }
    if (acc < target) p95 = 256;
    if (p95 >= 40 && p95 <= 120) {
      uint32_t thr = p95 * 3 / 4, longN = 0;
      for (int k = 0; k < 64; k++) if ((uint32_t)k * 4 + 3 >= thr) longN += t->hist[k];
      longN += t->over;
      if (longN * 100 >= t->nb * 50) wrapped = true;
    }
  }

  // ---- tulis ulang di tempat (w <= r selalu) ----
  uint32_t r = 0, w = 0, blankRun = 0; bool open = false;
  while (r < len) {
    uint32_t ls = r; while (r < len && b[r] != '\n') r++;
    uint32_t le = r; if (r < len) r++;
    uint32_t ln = le - ls;
    if (ln == 0) {
      blankRun++;
      if (wrapped && open) { b[w++] = '\n'; open = false; }
      continue;
    }
    bool head = (b[ls] == 1);
    bool gap = (blankRun >= 2);
    if (!(wrapped && open && !head)) {
      // paragraf baru: sisipkan jarak bila ada >=2 baris kosong di sumber
      if (open) { b[w++] = '\n'; open = false; }
      if (gap && w > 0 && b[w - 1] == '\n' && !(w >= 2 && b[w - 2] == '\n')) b[w++] = '\n';
    }
    if (wrapped && !head) {
      if (open) b[w++] = ' ';
      memmove(b + w, b + ls, ln); w += ln; open = true;
    } else {
      memmove(b + w, b + ls, ln); w += ln; b[w++] = '\n';
    }
    blankRun = 0;
  }
  if (open) b[w++] = '\n';
  o->len = w;
  b[w] = 0;
}

// =====================================================================
// 5. XML MINIMAL (untuk container.xml / OPF / NCX / nav)
// =====================================================================
struct EbTag { char name[24]; bool closing, selfClose; const char* attr; int attrLen; };

static bool ebXmlNext(const char* s, uint32_t n, uint32_t* pos, EbTag* t) {
  uint32_t i = *pos;
  while (i < n) {
    const void* lt = memchr(s + i, '<', n - i);
    if (!lt) break;
    i = (uint32_t)((const char*)lt - s);
    if (i + 3 < n && memcmp(s + i, "<!--", 4) == 0) {
      uint32_t e = ebFindStr((const uint8_t*)s, n, i + 4, "-->", 3);
      if (e == 0xFFFFFFFFu) break;
      i = e + 3; continue;
    }
    if (i + 8 < n && memcmp(s + i, "<![CDATA[", 9) == 0) {
      uint32_t e = ebFindStr((const uint8_t*)s, n, i + 9, "]]>", 3);
      if (e == 0xFFFFFFFFu) break;
      i = e + 3; continue;
    }
    if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) {
      const void* gt = memchr(s + i, '>', n - i);
      if (!gt) break;
      i = (uint32_t)((const char*)gt - s) + 1; continue;
    }
    uint32_t j = i + 1; t->closing = false;
    if (j < n && s[j] == '/') { t->closing = true; j++; }
    char full[32]; int fl = 0;
    while (j < n && (uint8_t)s[j] > ' ' && s[j] != '>' && s[j] != '/') { if (fl < 31) full[fl++] = ebLower(s[j]); j++; }
    full[fl] = 0;
    const char* nm = full;
    for (int q = fl - 1; q >= 0; q--) if (full[q] == ':') { nm = full + q + 1; break; }
    strncpy(t->name, nm, sizeof(t->name) - 1); t->name[sizeof(t->name) - 1] = 0;
    uint32_t a0 = j; char qt = 0;
    while (j < n) {
      char c = s[j];
      if (qt) { if (c == qt) qt = 0; }
      else if (c == '"' || c == '\'') qt = c;
      else if (c == '>') break;
      j++;
    }
    if (j >= n) break;
    t->selfClose = (j > a0 && s[j - 1] == '/');
    t->attr = s + a0; t->attrLen = (int)(j - a0) - (t->selfClose ? 1 : 0);
    *pos = j + 1;
    return true;
  }
  *pos = n;
  return false;
}

static bool ebAttrRaw(const EbTag* t, const char* key, const char** v, int* vl) {
  const char* p = t->attr; const char* e = p + t->attrLen; int kl = (int)strlen(key);
  while (p < e) {
    while (p < e && (uint8_t)*p <= ' ') p++;
    const char* ns = p;
    while (p < e && *p != '=' && (uint8_t)*p > ' ') p++;
    const char* ne = p;
    while (p < e && (uint8_t)*p <= ' ') p++;
    if (p >= e || *p != '=') continue;
    p++;
    while (p < e && (uint8_t)*p <= ' ') p++;
    if (p >= e) break;
    const char* vs; const char* ve; char q = *p;
    if (q == '"' || q == '\'') { p++; vs = p; while (p < e && *p != q) p++; ve = p; if (p < e) p++; }
    else { vs = p; while (p < e && (uint8_t)*p > ' ') p++; ve = p; }
    const char* c = ne; while (c > ns && c[-1] != ':') c--;
    if (ne - c == kl && ebEqCI(c, key, kl)) { *v = vs; *vl = (int)(ve - vs); return true; }
  }
  return false;
}

static int ebHex(char h) {
  return (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
}
// salin nilai mentah v[0..vl) dengan %XX dan &amp; didekode; berhenti di '#'/'?' bila stripFrag
static int ebDecodeVal(const char* v, int vl, char* out, int cap, bool stripFrag) {
  int w = 0;
  for (int i = 0; i < vl && w < cap - 1; i++) {
    char c = v[i];
    if (stripFrag && (c == '#' || c == '?')) break;
    if (c == '%' && i + 2 < vl) {
      int a = ebHex(v[i + 1]), b = ebHex(v[i + 2]);
      if (a >= 0 && b >= 0) { out[w++] = (char)(a * 16 + b); i += 2; continue; }
    }
    if (c == '&' && i + 4 < vl && memcmp(v + i, "&amp;", 5) == 0) { out[w++] = '&'; i += 4; continue; }
    out[w++] = c;
  }
  out[w] = 0;
  return w;
}
static int ebAttrCopy(const EbTag* t, const char* key, char* out, int cap, bool stripFrag) {
  const char* v; int vl;
  if (!ebAttrRaw(t, key, &v, &vl)) { if (cap > 0) out[0] = 0; return -1; }
  return ebDecodeVal(v, vl, out, cap, stripFrag);
}
static bool ebEndsCI(const char* s, int n, const char* suf) {
  int k = (int)strlen(suf);
  return n >= k && ebEqCI(s + n - k, suf, k);
}

// gabungkan base (diakhiri '/' atau kosong) + rel lalu normalkan "." / ".."
static void ebResolve(const char* base, const char* rel, char* out, int cap) {
  char tmp[256]; int tl = 0;
  if (rel[0] == '/') rel++;
  else { for (int i = 0; base[i] && tl < 250; i++) tmp[tl++] = base[i]; }
  for (int i = 0; rel[i] && tl < 255; i++) tmp[tl++] = rel[i];
  tmp[tl] = 0;
  int w = 0; int i = 0;
  while (tmp[i]) {
    int s = i; while (tmp[i] && tmp[i] != '/') i++;
    int sl = i - s;
    if (tmp[i] == '/') i++;
    if (sl == 0 || (sl == 1 && tmp[s] == '.')) continue;
    if (sl == 2 && tmp[s] == '.' && tmp[s + 1] == '.') {
      if (w > 0) { w--; while (w > 0 && out[w - 1] != '/') w--; }
      continue;
    }
    if (w + sl + 2 >= cap) break;
    memcpy(out + w, tmp + s, (size_t)sl); w += sl;
    if (tmp[i - 1] == '/' || tmp[i] != 0) out[w++] = '/';
  }
  if (w > 0 && out[w - 1] == '/' && tl > 0 && tmp[tl - 1] != '/') w--;
  out[w] = 0;
}

// =====================================================================
// 5b. MODEL BUKU
// =====================================================================
#define EB_KIND_TXT   1
#define EB_KIND_EPUB  2
#define EB_KIND_MD    3          // Markdown: dibuka sebagai TXT setelah dikonversi
#define EB_MD_MAX     (1u * 1024u * 1024u)
#define EB_MAX_CHIMG  64         // gambar per bab
#define EB_IMG_MIN    3072u      // gambar lebih kecil = hiasan, dilewati
#define EB_IMG_MAX    (3584u * 1024u)
#define EB_MAX_SPINE  2000
#define EB_MAX_TOC    400
#define EB_TXT_MAX    (3u * 1024u * 1024u)
#define EB_CH_MAX     (4u * 1024u * 1024u)

struct EbSpine { uint32_t pathOff, titleOff; };      // offset di pool (0 = kosong)
struct EbToc   { uint32_t titleOff; int32_t ch; uint32_t off; };
struct EbImg   { char path[148]; uint32_t usize; };
struct EbBook {
  int kind;
  EbRd rd; EbZip zip;
  char* pool; uint32_t poolLen, poolCap;
  EbSpine* sp; int nSp;
  EbToc* toc; int nToc;
  char title[80];
  char author[64];                                    // v2: dc:creator (UTF-8)
  uint32_t coverOff;                                  // v2: offset jalur gambar sampul di pool (0 = tak ada)
  char* text; uint32_t textLen; int curCh;
  int truncated;                                      // TXT dipotong di EB_TXT_MAX
  EbImg* imgs; int nImg; int loadCh;                  // v2: gambar bab yang sedang dimuat
};

static uint32_t ebPoolAdd(EbBook* b, const char* s, uint32_t n) {
  if (b->poolLen + n + 1 > b->poolCap) {
    uint32_t nc = b->poolCap ? b->poolCap * 2 : 8192;
    while (nc < b->poolLen + n + 1) nc *= 2;
    char* np = (char*)EB_REALLOC(b->pool, nc);
    if (!np) return 0;
    b->pool = np; b->poolCap = nc;
  }
  uint32_t off = b->poolLen;
  memcpy(b->pool + off, s, n); b->pool[off + n] = 0;
  b->poolLen += n + 1;
  return off;
}
static inline const char* ebPoolStr(const EbBook* b, uint32_t off) { return b->pool + off; }

static void ebBookInit(EbBook* b) {
  memset(b, 0, sizeof(*b));
  b->curCh = -1;
  b->poolCap = 8192; b->pool = (char*)EB_ALLOC(b->poolCap);
  if (b->pool) { b->pool[0] = 0; b->poolLen = 1; } else b->poolCap = 0;
}
static void ebBookClose(EbBook* b) {
  if (b->imgs) EB_FREE(b->imgs);
  if (b->text) EB_FREE(b->text);
  if (b->sp) EB_FREE(b->sp);
  if (b->toc) EB_FREE(b->toc);
  if (b->pool) EB_FREE(b->pool);
  if (b->kind == EB_KIND_EPUB) ebZipClose(&b->zip);
  memset(b, 0, sizeof(*b));
}
static inline int ebBookChapters(const EbBook* b) { return b->kind == EB_KIND_EPUB ? b->nSp : 1; }

// ---- TXT ----
static int ebTxtLoad(EbBook* b) {
  uint32_t size = b->rd.size;
  if (size > EB_TXT_MAX) { size = EB_TXT_MAX; b->truncated = 1; }
  uint32_t cap = size + size / 16 + 4096;
  char* out = (char*)EB_ALLOC(cap);
  const uint32_t CH = 16384;
  uint8_t* buf = (uint8_t*)EB_ALLOC(CH + 8);
  if (!out || !buf) { if (out) EB_FREE(out); if (buf) EB_FREE(buf); return EB_E_NOMEM; }
  EbOut o; ebOutInit(&o, out, cap);
  EbTxt* t = (EbTxt*)EB_ALLOC(sizeof(EbTxt));
  if (!t) { EB_FREE(out); EB_FREE(buf); return EB_E_NOMEM; }
  ebTxtInit(t, &o);
  uint32_t off = 0, carry = 0;
  for (;;) {
    uint32_t want = size - off; if (want > CH - carry) want = CH - carry;
    if (want && !b->rd.read(b->rd.ctx, off, buf + carry, want)) { EB_FREE(t); EB_FREE(out); EB_FREE(buf); return EB_E_IO; }
    off += want;
    uint32_t have = carry + want;
    bool eof = (off >= size);
    uint32_t used = ebTxtFeed(t, buf, have, eof);
    carry = have - used;
    if (carry) memmove(buf, buf + used, carry);
    if (eof) break;
    EB_YIELD();
  }
  ebTxtFinish(t);
  EB_FREE(t); EB_FREE(buf);
  b->text = out; b->textLen = o.len; b->curCh = 0;
  return EB_OK;
}

// daftar isi TXT dari baris judul ('\x01')
static void ebTxtBuildToc(EbBook* b) {
  b->toc = (EbToc*)EB_ALLOC(sizeof(EbToc) * EB_MAX_TOC);
  b->nToc = 0;
  if (!b->toc) return;
  for (uint32_t i = 0; i < b->textLen && b->nToc < EB_MAX_TOC; i++) {
    if (b->text[i] == 1 && (i == 0 || b->text[i - 1] == '\n')) {
      uint32_t e = i + 1; while (e < b->textLen && b->text[e] != '\n' && e - i < 60) e++;
      EbToc* tc = &b->toc[b->nToc++];
      tc->titleOff = ebPoolAdd(b, b->text + i + 1, e - i - 1);
      tc->ch = 0; tc->off = i;
    }
  }
}

// ---- Markdown (.md) ----
// Konversi ringan ke teks kanonik: judul (#), daftar, kutipan, blok kode, tabel, garis pemisah,
// tebal/miring/kode-inline/tautan/gambar dibersihkan. Tidak lengkap (tanpa setext, tanpa HTML blok), tapi cukup untuk dibaca.
static inline bool ebMdAlnum(uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }

// keluarkan satu karakter (UTF-8 / ASCII) dari s[i] ke o dan majukan i
static void ebMdPutCp(EbOut* o, const uint8_t* s, uint32_t n, uint32_t* i) {
  uint8_t c = s[*i];
  if (c < 0x80) {
    if (c == ' ' || c == '\t') ebOutSpace(o);
    else if (c >= 32 && c != 0x7F) ebOutChar(o, (char)c);
    (*i)++; return;
  }
  uint32_t cp; bool bad;
  int u = ebUtf8(s + *i, n - *i, &cp, &bad);
  if (u == 0) { u = 1; bad = true; cp = c; }
  if (bad && cp >= 0x80 && cp <= 0x9F) { uint32_t m = ebCp1252((uint8_t)cp); cp = m ? m : 0x20; }
  ebOutCp(o, cp);
  *i += (uint32_t)u;
}

static void ebMdInline(const uint8_t* s, uint32_t n, EbOut* o) {
  uint32_t i = 0; bool code = false;
  while (i < n) {
    uint8_t c = s[i];
    if (c == '`') { code = !code; while (i < n && s[i] == '`') i++; continue; }
    if (code) { ebMdPutCp(o, s, n, &i); continue; }
    if (c == '\\' && i + 1 < n && s[i + 1] > ' ' && s[i + 1] < 0x80 && !ebMdAlnum(s[i + 1])) { ebOutChar(o, (char)s[i + 1]); i += 2; continue; }
    if (c == '*' || c == '_') {
      uint32_t j = i; while (j < n && s[j] == c) j++;
      uint8_t prev = i > 0 ? s[i - 1] : ' ', next = j < n ? s[j] : ' ';
      bool literal = (c == '*' && prev <= ' ' && next <= ' ') || (c == '_' && ebMdAlnum(prev) && ebMdAlnum(next));
      if (literal) { for (uint32_t q = i; q < j; q++) ebOutChar(o, (char)c); }
      i = j; continue;
    }
    if (c == '~' && i + 1 < n && s[i + 1] == '~') { i += 2; continue; }
    if ((c == '!' && i + 1 < n && s[i + 1] == '[') || c == '[') {
      bool isImg = (c == '!');
      uint32_t a = i + (isImg ? 2 : 1), e = a;
      while (e < n && s[e] != ']') e++;
      if (e < n && !(c == '[' && a < n && s[a] == '^')) {                 // [teks] ditemukan (bukan catatan kaki)
        uint32_t after = e + 1;
        if (after < n && s[after] == '(') {                              // (url)
          uint32_t u = after + 1; int depth = 1;
          while (u < n && depth > 0) { if (s[u] == '(') depth++; else if (s[u] == ')') depth--; if (depth > 0) u++; }
          if (u < n) {
            if (isImg) { if (e > a) { ebOutChar(o, '['); ebMdInline(s + a, e - a, o); ebOutChar(o, ']'); } }
            else ebMdInline(s + a, e - a, o);
            i = u + 1; continue;
          }
        } else if (after < n && s[after] == '[') {                       // [teks][ref]
          uint32_t u = after + 1; while (u < n && s[u] != ']') u++;
          if (u < n) { if (!isImg) ebMdInline(s + a, e - a, o); i = u + 1; continue; }
        }
      }
      ebOutChar(o, (char)c); i++; continue;
    }
    if (c == '<') {
      uint32_t e = i + 1; while (e < n && s[e] != '>' && e - i < 200) e++;
      if (e < n && s[e] == '>') {
        bool url = (e - i > 5 && (memcmp(s + i + 1, "http", 4) == 0 || memcmp(s + i + 1, "mailto", 6) == 0));
        bool tag = (i + 1 < n && ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= 'A' && s[i + 1] <= 'Z') || s[i + 1] == '/' || s[i + 1] == '!'));
        if (url) { for (uint32_t q = i + 1; q < e; q++) ebOutChar(o, (char)s[q]); i = e + 1; continue; }
        if (tag) { i = e + 1; continue; }
      }
      ebOutChar(o, '<'); i++; continue;
    }
    if (c == '&') {
      uint32_t cp = 0; uint32_t used = ebEntity(s, n, i, &cp);
      if (used) { if (cp) ebOutCp(o, cp); i += used; continue; }
    }
    ebMdPutCp(o, s, n, &i);
  }
}

static void ebMdToText(const uint8_t* in, uint32_t n, EbOut* o) {
  uint32_t pos = 0; bool fence = false, inPara = false;
  if (n >= 3 && in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xBF) pos = 3;
  while (pos < n) {
    uint32_t ls = pos; while (pos < n && in[pos] != '\n') pos++;
    uint32_t le = pos; if (pos < n) pos++;
    if (le > ls && in[le - 1] == '\r') le--;
    const uint8_t* L = in + ls; uint32_t ln = le - ls;
    uint32_t k = 0; while (k < ln && (L[k] == ' ' || L[k] == '\t')) k++;
    if (ln - k >= 3 && ((L[k] == '`' && L[k + 1] == '`' && L[k + 2] == '`') || (L[k] == '~' && L[k + 1] == '~' && L[k + 2] == '~'))) {
      fence = !fence; ebOutBreak(o); inPara = false; continue;
    }
    if (fence) { for (uint32_t i = 0; i < ln;) ebMdPutCp(o, L, ln, &i); ebOutBreak(o); continue; }
    if (k == ln) { ebOutBreak(o); inPara = false; continue; }
    while (k < ln && L[k] == '>') { k++; if (k < ln && L[k] == ' ') k++; }          // kutipan: buang penanda
    if (k >= ln) { ebOutBreak(o); inPara = false; continue; }
    // judul ATX
    if (L[k] == '#') {
      uint32_t h = 0; while (k + h < ln && L[k + h] == '#') h++;
      if (h <= 6 && (k + h == ln || L[k + h] == ' ')) {
        uint32_t a = k + h; while (a < ln && L[a] == ' ') a++;
        uint32_t e = ln; while (e > a && L[e - 1] == ' ') e--;
        while (e > a && L[e - 1] == '#') e--;
        while (e > a && L[e - 1] == ' ') e--;
        ebOutBreak(o); o->headPend = 1;
        ebMdInline(L + a, e - a, o);
        ebOutBreak(o); o->headPend = 0; inPara = false; continue;
      }
    }
    // garis pemisah / garis bawah judul
    {
      uint8_t hc = L[k]; uint32_t cnt = 0; bool only = true;
      for (uint32_t q = k; q < ln; q++) { if (L[q] == hc) cnt++; else if (L[q] != ' ') { only = false; break; } }
      if (only && cnt >= 3 && (hc == '-' || hc == '*' || hc == '_')) {
        ebOutBreak(o); o->headPend = 1;
        ebOutChar(o, '*'); ebOutChar(o, ' '); ebOutChar(o, '*'); ebOutChar(o, ' '); ebOutChar(o, '*');
        ebOutBreak(o); o->headPend = 0; inPara = false; continue;
      }
      if (only && cnt >= 2 && hc == '=' && inPara) continue;                       // garis bawah judul setext: abaikan
    }
    // butir daftar
    if ((L[k] == '-' || L[k] == '*' || L[k] == '+') && k + 1 < ln && L[k + 1] == ' ') {
      uint32_t a = k + 2; while (a < ln && L[a] == ' ') a++;
      ebOutBreak(o); ebOutChar(o, '*'); ebOutChar(o, ' ');
      ebMdInline(L + a, ln - a, o); inPara = true; goto hardbreak;
    }
    if (L[k] >= '0' && L[k] <= '9') {
      uint32_t d = k; while (d < ln && L[d] >= '0' && L[d] <= '9' && d - k < 6) d++;
      if (d < ln && (L[d] == '.' || L[d] == ')') && d + 1 < ln && L[d + 1] == ' ') {
        uint32_t a = d + 2; while (a < ln && L[a] == ' ') a++;
        ebOutBreak(o);
        for (uint32_t q = k; q < d; q++) ebOutChar(o, (char)L[q]);
        ebOutChar(o, '.'); ebOutChar(o, ' ');
        ebMdInline(L + a, ln - a, o); inPara = true; goto hardbreak;
      }
    }
    // tabel: baris berpipa; baris pemisah |---|---| dilewati
    if (L[k] == '|') {
      bool sep = true; for (uint32_t q = k; q < ln; q++) if (L[q] != '|' && L[q] != '-' && L[q] != ':' && L[q] != ' ') { sep = false; break; }
      if (sep) continue;
      ebOutBreak(o);
      uint32_t cs = k + 1; bool firstCell = true;
      while (cs <= ln) {
        uint32_t ce = cs; while (ce < ln && L[ce] != '|') ce++;
        uint32_t a = cs, e = ce; while (a < e && L[a] == ' ') a++; while (e > a && L[e - 1] == ' ') e--;
        if (e > a || ce < ln) {
          if (e > a) { if (!firstCell) { ebOutSpace(o); ebOutChar(o, '|'); ebOutSpace(o); } ebMdInline(L + a, e - a, o); firstCell = false; }
        }
        if (ce >= ln) break;
        cs = ce + 1;
      }
      ebOutBreak(o); inPara = false; continue;
    }
    // paragraf biasa: baris berurutan disambung dengan spasi
    if (inPara) ebOutSpace(o); else ebOutBreak(o);
    ebMdInline(L + k, ln - k, o); inPara = true;
  hardbreak:
    if ((ln >= 2 && L[ln - 1] == ' ' && L[ln - 2] == ' ') || (ln >= 1 && L[ln - 1] == '\\')) { ebOutBreak(o); inPara = false; }
  }
  ebOutBreak(o);
}

static int ebMdLoad(EbBook* b) {
  uint32_t size = b->rd.size;
  if (size > EB_MD_MAX) { size = EB_MD_MAX; b->truncated = 1; }
  uint8_t* in = (uint8_t*)EB_ALLOC((size_t)size + 1);
  if (!in) return EB_E_NOMEM;
  if (size && !ebRdAll(&b->rd, 0, in, size)) { EB_FREE(in); return EB_E_IO; }
  in[size] = 0;
  if (size >= 2 && ((in[0] == 0xFF && in[1] == 0xFE) || (in[0] == 0xFE && in[1] == 0xFF))) { EB_FREE(in); return ebTxtLoad(b); }   // UTF-16: perlakukan sebagai TXT
  uint32_t cap = size + size / 8 + 4096;
  char* out = (char*)EB_ALLOC(cap);
  if (!out) { EB_FREE(in); return EB_E_NOMEM; }
  EbOut o; ebOutInit(&o, out, cap);
  ebMdToText(in, size, &o);
  EB_FREE(in);
  out[o.len] = 0;
  b->text = out; b->textLen = o.len; b->curCh = 0;
  if (o.len > 1 && out[0] == 1) {                                  // judul dokumen = judul pertama
    uint32_t e = 1; while (e < o.len && out[e] != '\n' && e < sizeof(b->title)) e++;
    uint32_t l = e - 1; if (l >= sizeof(b->title)) l = sizeof(b->title) - 1;
    memcpy(b->title, out + 1, l); b->title[l] = 0;
  }
  return EB_OK;
}

// ---- EPUB ----
static int ebZipGet(EbBook* b, const char* path, uint32_t maxLen, uint8_t** data, uint32_t* len) {
  int idx = ebZipFind(&b->zip, path);
  if (idx < 0) return EB_E_NOENT;
  int err; *data = ebZipExtract(&b->zip, idx, maxLen, len, &err);
  return *data ? EB_OK : err;
}

struct EbMan { uint32_t idOff; uint16_t idLen; uint32_t hrefOff; uint16_t hrefLen; uint8_t flags; };
// flags: 1 = (x)html, 2 = ncx, 4 = nav, 8 = properti cover-image, 16 = gambar jpeg/png

static void ebDirOf(const char* path, char* dir, int cap) {
  int n = (int)strlen(path), s = n;
  while (s > 0 && path[s - 1] != '/') s--;
  if (s >= cap) s = cap - 1;
  memcpy(dir, path, (size_t)s); dir[s] = 0;
}

static int ebSpineFind(const EbBook* b, const char* path) {
  for (int i = 0; i < b->nSp; i++) if (strcmp(ebPoolStr(b, b->sp[i].pathOff), path) == 0) return i;
  int pl = (int)strlen(path);
  for (int i = 0; i < b->nSp; i++) {
    const char* q = ebPoolStr(b, b->sp[i].pathOff);
    if ((int)strlen(q) == pl && ebEqCI(q, path, pl)) return i;
  }
  return -1;
}

static void ebSetTitle(EbBook* b, int spineIdx, const char* title) {
  if (spineIdx < 0 || spineIdx >= b->nSp || b->sp[spineIdx].titleOff || !title[0]) return;
  b->sp[spineIdx].titleOff = ebPoolAdd(b, title, (uint32_t)strlen(title));
}

// daftar isi dari NCX atau nav; mengisi judul spine
static void ebEpubToc(EbBook* b, const char* tocPath, bool isNav) {
  uint8_t* d; uint32_t dl;
  if (ebZipGet(b, tocPath, 2u * 1024u * 1024u, &d, &dl) != EB_OK) return;
  const char* s = (const char*)d;
  char dir[200]; ebDirOf(tocPath, dir, sizeof(dir));
  EbTag t; uint32_t pos = 0;
  char title[96], href[200], path[256];
  if (!isNav) {
    bool have = false;
    while (ebXmlNext(s, dl, &pos, &t)) {
      if (t.closing) continue;
      if (strcmp(t.name, "text") == 0 && !t.selfClose) {
        const void* lt = memchr(s + pos, '<', dl - pos);
        uint32_t e = lt ? (uint32_t)((const char*)lt - s) : dl;
        ebSnippet(s + pos, e - pos, title, sizeof(title)); have = title[0] != 0;
      } else if (strcmp(t.name, "content") == 0) {
        if (have && ebAttrCopy(&t, "src", href, sizeof(href), true) >= 0) {
          ebResolve(dir, href, path, sizeof(path));
          ebSetTitle(b, ebSpineFind(b, path), title);
        }
        have = false;
      }
    }
  } else {
    int inToc = 0, navSeen = 0;
    while (ebXmlNext(s, dl, &pos, &t)) {
      if (strcmp(t.name, "nav") == 0) {
        if (t.closing) { inToc = 0; continue; }
        char ty[64]; ebAttrCopy(&t, "type", ty, sizeof(ty), false);
        bool isToc = strstr(ty, "toc") != nullptr || (ty[0] == 0 && navSeen == 0);
        inToc = isToc ? 1 : 0; navSeen++;
        continue;
      }
      if (!inToc || t.closing || strcmp(t.name, "a") != 0) continue;
      if (ebAttrCopy(&t, "href", href, sizeof(href), true) < 0) continue;
      uint32_t e = pos;
      while (e + 3 < dl && !(s[e] == '<' && s[e + 1] == '/' && ebLower(s[e + 2]) == 'a' && (s[e + 3] == '>' || (uint8_t)s[e + 3] <= ' '))) e++;
      if (e + 3 >= dl) e = dl;
      ebSnippet(s + pos, e - pos, title, sizeof(title));
      ebResolve(dir, href, path, sizeof(path));
      ebSetTitle(b, ebSpineFind(b, path), title);
    }
  }
  EB_FREE(d);
}

static int ebEpubOpen(EbBook* b) {
  int r = ebZipOpen(&b->zip, b->rd);
  if (r != EB_OK) return r;
  uint8_t* d = nullptr; uint32_t dl = 0;
  r = ebZipGet(b, "META-INF/container.xml", 256u * 1024u, &d, &dl);
  if (r != EB_OK) return r == EB_E_NOENT ? EB_E_NOCONT : r;
  char opfPath[256]; opfPath[0] = 0;
  {
    EbTag t; uint32_t pos = 0; const char* s = (const char*)d;
    while (ebXmlNext(s, dl, &pos, &t)) {
      if (!t.closing && strcmp(t.name, "rootfile") == 0) {
        char fp[256];
        if (ebAttrCopy(&t, "full-path", fp, sizeof(fp), false) > 0) { ebResolve("", fp, opfPath, sizeof(opfPath)); break; }
      }
    }
  }
  EB_FREE(d);
  if (!opfPath[0]) return EB_E_NOOPF;
  r = ebZipGet(b, opfPath, 4u * 1024u * 1024u, &d, &dl);
  if (r != EB_OK) return r == EB_E_NOENT ? EB_E_NOOPF : r;
  const char* s = (const char*)d;
  char opfDir[200]; ebDirOf(opfPath, opfDir, sizeof(opfDir));

  // ---- manifest ----
  EbMan* man = (EbMan*)EB_ALLOC(sizeof(EbMan) * 4000);
  b->sp = (EbSpine*)EB_ALLOC(sizeof(EbSpine) * EB_MAX_SPINE);
  if (!man || !b->sp) { if (man) EB_FREE(man); EB_FREE(d); return EB_E_NOMEM; }
  int nMan = 0; b->nSp = 0;
  char tocId[64]; tocId[0] = 0;
  char coverId[64]; coverId[0] = 0;
  b->title[0] = 0; b->author[0] = 0; b->coverOff = 0;
  {
    EbTag t; uint32_t pos = 0;
    while (ebXmlNext(s, dl, &pos, &t)) {
      if (t.closing) continue;
      if (strcmp(t.name, "item") == 0 && nMan < 4000) {
        const char *id, *hr, *mt, *pr; int idl, hrl, mtl, prl;
        if (!ebAttrRaw(&t, "id", &id, &idl) || !ebAttrRaw(&t, "href", &hr, &hrl)) continue;
        EbMan* m = &man[nMan++];
        m->idOff = (uint32_t)(id - s); m->idLen = (uint16_t)idl;
        m->hrefOff = (uint32_t)(hr - s); m->hrefLen = (uint16_t)hrl;
        m->flags = 0;
        if (ebAttrRaw(&t, "media-type", &mt, &mtl)) {
          if ((mtl >= 9 && memcmp(mt, "text/html", 9) == 0) || (mtl >= 21 && memcmp(mt, "application/xhtml+xml", 21) == 0)) m->flags |= 1;
          else if (mtl >= 20 && memcmp(mt, "application/x-dtbncx", 20) == 0) m->flags |= 2;
        } else if (ebEndsCI(hr, hrl, "html") || ebEndsCI(hr, hrl, ".htm")) {
          m->flags |= 1;
        }
        if (ebAttrRaw(&t, "properties", &pr, &prl)) {
          for (int q = 0; q + 3 <= prl; q++) if ((q == 0 || pr[q - 1] == ' ') && memcmp(pr + q, "nav", 3) == 0 && (q + 3 == prl || pr[q + 3] == ' ')) m->flags |= 4;
          for (int q = 0; q + 11 <= prl; q++) if ((q == 0 || pr[q - 1] == ' ') && memcmp(pr + q, "cover-image", 11) == 0 && (q + 11 == prl || pr[q + 11] == ' ')) m->flags |= 8;
        }
        if (ebEndsCI(hr, hrl, ".jpg") || ebEndsCI(hr, hrl, ".jpeg") || ebEndsCI(hr, hrl, ".png")) m->flags |= 16;
      } else if (strcmp(t.name, "meta") == 0) {
        char mn[24]; ebAttrCopy(&t, "name", mn, sizeof(mn), false);
        if (strcmp(mn, "cover") == 0) ebAttrCopy(&t, "content", coverId, sizeof(coverId), false);
      } else if (strcmp(t.name, "creator") == 0 && !b->author[0] && !t.selfClose) {
        const void* lt = memchr(s + pos, '<', dl - pos);
        uint32_t e = lt ? (uint32_t)((const char*)lt - s) : dl;
        ebSnippet(s + pos, e - pos, b->author, sizeof(b->author));
      } else if (strcmp(t.name, "spine") == 0) {
        ebAttrCopy(&t, "toc", tocId, sizeof(tocId), false);
      } else if (strcmp(t.name, "title") == 0 && !b->title[0] && !t.selfClose) {
        const void* lt = memchr(s + pos, '<', dl - pos);
        uint32_t e = lt ? (uint32_t)((const char*)lt - s) : dl;
        ebSnippet(s + pos, e - pos, b->title, sizeof(b->title));
      }
    }
  }
  // ---- spine ----
  int tocIdx = -1, navIdx = -1, ncxIdx = -1;
  for (int i = 0; i < nMan; i++) {
    if (man[i].flags & 4) navIdx = navIdx < 0 ? i : navIdx;
    if (man[i].flags & 2) ncxIdx = ncxIdx < 0 ? i : ncxIdx;
    if (tocId[0] && man[i].idLen == strlen(tocId) && memcmp(s + man[i].idOff, tocId, man[i].idLen) == 0) tocIdx = i;
  }
  {
    EbTag t; uint32_t pos = 0; bool inSpine = false;
    while (ebXmlNext(s, dl, &pos, &t)) {
      if (strcmp(t.name, "spine") == 0) { inSpine = !t.closing && !t.selfClose; if (t.closing) break; continue; }
      if (!inSpine || t.closing || strcmp(t.name, "itemref") != 0) continue;
      const char *ir, *lin; int irl, linl;
      if (!ebAttrRaw(&t, "idref", &ir, &irl)) continue;
      if (ebAttrRaw(&t, "linear", &lin, &linl) && linl == 2 && ebEqCI(lin, "no", 2)) continue;
      int mi = -1;
      for (int i = 0; i < nMan; i++) if (man[i].idLen == irl && memcmp(s + man[i].idOff, ir, (size_t)irl) == 0) { mi = i; break; }
      if (mi < 0 || !(man[mi].flags & 1) || b->nSp >= EB_MAX_SPINE) continue;
      char href[200], path[256];
      ebDecodeVal(s + man[mi].hrefOff, man[mi].hrefLen, href, sizeof(href), true);
      ebResolve(opfDir, href, path, sizeof(path));
      uint32_t po = ebPoolAdd(b, path, (uint32_t)strlen(path));
      if (!po) continue;
      b->sp[b->nSp].pathOff = po; b->sp[b->nSp].titleOff = 0; b->nSp++;
    }
  }
  // ---- sampul: properti cover-image > <meta name="cover"> > gambar bernama "cover" ----
  {
    int ci = -1;
    for (int i = 0; i < nMan && ci < 0; i++) if (man[i].flags & 8) ci = i;
    if (ci < 0 && coverId[0]) {
      size_t cl = strlen(coverId);
      for (int i = 0; i < nMan; i++) if (man[i].idLen == cl && memcmp(s + man[i].idOff, coverId, cl) == 0) { ci = i; break; }
    }
    if (ci < 0) {
      for (int i = 0; i < nMan && ci < 0; i++) {
        if (!(man[i].flags & 16)) continue;
        for (int q = 0; q + 5 <= (int)man[i].hrefLen; q++) if (ebEqCI(s + man[i].hrefOff + q, "cover", 5)) { ci = i; break; }
      }
    }
    if (ci >= 0) {
      char href[200], path[256];
      ebDecodeVal(s + man[ci].hrefOff, man[ci].hrefLen, href, sizeof(href), true);
      ebResolve(opfDir, href, path, sizeof(path));
      b->coverOff = ebPoolAdd(b, path, (uint32_t)strlen(path));
    }
  }
  // simpan path toc sebelum membebaskan OPF
  char tocPath[256]; tocPath[0] = 0; bool tocIsNav = false;
  {
    int use = tocIdx >= 0 ? tocIdx : (ncxIdx >= 0 ? ncxIdx : navIdx);
    if (use >= 0) {
      char href[200];
      ebDecodeVal(s + man[use].hrefOff, man[use].hrefLen, href, sizeof(href), true);
      ebResolve(opfDir, href, tocPath, sizeof(tocPath));
      tocIsNav = (man[use].flags & 4) != 0 && !(man[use].flags & 2);
    }
  }
  EB_FREE(man); EB_FREE(d);
  if (b->nSp == 0) return EB_E_EMPTY;
  if (tocPath[0]) ebEpubToc(b, tocPath, tocIsNav);
  // judul cadangan + daftar isi = seluruh spine
  b->toc = (EbToc*)EB_ALLOC(sizeof(EbToc) * (size_t)b->nSp);
  if (!b->toc) return EB_E_NOMEM;
  b->nToc = b->nSp;
  for (int i = 0; i < b->nSp; i++) {
    char tt[24]; int k = 0, v = i + 1; char dg[12]; int dn = 0;
    while (v > 0) { dg[dn++] = (char)('0' + v % 10); v /= 10; }
    const char* pf = "Bagian "; while (*pf) tt[k++] = *pf++;
    while (dn) tt[k++] = dg[--dn];
    tt[k] = 0;
    if (!b->sp[i].titleOff) b->sp[i].titleOff = ebPoolAdd(b, tt, (uint32_t)k);
    b->toc[i].titleOff = b->sp[i].titleOff; b->toc[i].ch = i; b->toc[i].off = 0;
  }
  return EB_OK;
}

// v2: pengait <img>: selesaikan jalur relatif thd berkas bab, cek ukuran, daftarkan. Hasil = indeks (-1 = lewati).
static int ebChImg(void* ctx, const char* src, int srcLen) {
  EbBook* b = (EbBook*)ctx;
  (void)srcLen;
  if (!b->imgs || b->loadCh < 0 || b->loadCh >= b->nSp) return -1;
  if (strncmp(src, "data:", 5) == 0 || strstr(src, "://")) return -1;
  int sl = (int)strlen(src);
  if (!(ebEndsCI(src, sl, ".jpg") || ebEndsCI(src, sl, ".jpeg") || ebEndsCI(src, sl, ".png"))) return -1;
  char dir[200], path[256];
  ebDirOf(ebPoolStr(b, b->sp[b->loadCh].pathOff), dir, sizeof(dir));
  ebResolve(dir, src, path, sizeof(path));
  if (strlen(path) >= sizeof(b->imgs[0].path)) return -1;
  for (int i = 0; i < b->nImg; i++) if (strcmp(b->imgs[i].path, path) == 0) return i;
  if (b->nImg >= EB_MAX_CHIMG) return -1;
  int zi = ebZipFind(&b->zip, path);
  if (zi < 0) return -1;
  const EbZipEnt* e = &b->zip.ent[zi];
  if (e->usize < EB_IMG_MIN || e->usize > EB_IMG_MAX) return -1;
  EbImg* im = &b->imgs[b->nImg];
  strcpy(im->path, path); im->usize = e->usize;
  return b->nImg++;
}

// Ambil data gambar bab ke buffer baru (PSRAM). Pemanggil membebaskan dengan EB_FREE.
static int ebBookImage(EbBook* b, int idx, uint8_t** data, uint32_t* len) {
  if (!b->imgs || idx < 0 || idx >= b->nImg) return EB_E_NOENT;
  return ebZipGet(b, b->imgs[idx].path, EB_IMG_MAX, data, len);
}
// Ambil gambar sampul (hanya EPUB yang punya sampul).
static int ebBookCover(EbBook* b, uint8_t** data, uint32_t* len) {
  if (b->kind != EB_KIND_EPUB || !b->coverOff) return EB_E_NOENT;
  return ebZipGet(b, ebPoolStr(b, b->coverOff), 8u * 1024u * 1024u, data, len);
}

// Muat bab (EPUB: dari zip; TXT: sudah dimuat). Teks lama dibuang.
static int ebBookLoadCh(EbBook* b, int ch) {
  if (b->kind == EB_KIND_TXT) return ch == 0 ? EB_OK : EB_E_NOENT;
  if (ch < 0 || ch >= b->nSp) return EB_E_NOENT;
  if (b->text) { EB_FREE(b->text); b->text = nullptr; b->textLen = 0; }
  b->curCh = -1;
  uint8_t* raw; uint32_t rl;
  int r = ebZipGet(b, ebPoolStr(b, b->sp[ch].pathOff), EB_CH_MAX, &raw, &rl);
  if (r != EB_OK) return r;
  uint32_t cap = rl + rl / 16 + 4096;
  char* out = (char*)EB_ALLOC(cap);
  if (!out) { EB_FREE(raw); return EB_E_NOMEM; }
  EbOut o; ebOutInit(&o, out, cap);
  if (b->imgs) { EB_FREE(b->imgs); b->imgs = nullptr; }
  b->nImg = 0; b->loadCh = ch;
  b->imgs = (EbImg*)EB_ALLOC(sizeof(EbImg) * EB_MAX_CHIMG);
  if (b->imgs) { o.img = ebChImg; o.imgCtx = b; }
  ebHtmlToText(raw, rl, &o);
  EB_FREE(raw);
  out[o.len] = 0;
  b->text = out; b->textLen = o.len; b->curCh = ch;
  return EB_OK;
}

// Buka buku dari rd. kind = EB_KIND_TXT / EB_KIND_EPUB. Untuk TXT teks langsung dimuat.
static int ebBookOpen(EbBook* b, EbRd rd, int kind) {
  ebBookInit(b);
  if (!b->pool) return EB_E_NOMEM;
  b->kind = kind; b->rd = rd;
  int r;
  if (kind == EB_KIND_MD) {
    b->kind = EB_KIND_TXT;                           // setelah dikonversi, md diperlakukan seperti TXT
    r = ebMdLoad(b);
    if (r == EB_OK) ebTxtBuildToc(b);
  } else if (kind == EB_KIND_TXT) {
    r = ebTxtLoad(b);
    if (r == EB_OK) ebTxtBuildToc(b);
  } else {
    r = ebEpubOpen(b);
  }
  if (r != EB_OK) ebBookClose(b);
  return r;
}

// =====================================================================
// 6. TATA LETAK
// =====================================================================
struct EbLay {
  uint8_t adv[128];     // lebar tiap karakter ASCII dalam piksel
  int16_t width;        // lebar area teks
  int16_t indent;       // indent baris pertama paragraf
  int16_t rows;         // baris per halaman
  uint8_t uni;          // v2: 1 = font punya glyph Unicode (cpw valid)
  int (*cpw)(uint32_t cp);   // v2: lebar piksel codepoint non-ASCII; <= 0 = font tak punya glyph -> dilipat ke ASCII
};
struct EbLine {
  uint32_t start, end, next;   // [start,end) teks yang digambar; next = awal baris berikut
  int16_t x, w;                // posisi x (relatif area teks) dan lebar teks
  uint8_t head, empty, nextHead;
  uint8_t endPara;             // v2: baris terakhir paragraf (tidak di-justify)
  uint8_t split;               // v2: baris dipotong di tengah kata (tidak di-justify)
  int32_t img;                 // v2: >= 0 = penanda gambar nomor ini
};

static inline int ebAdv(const EbLay* L, char c) {
  uint8_t u = (uint8_t)c; return (u >= 32 && u < 127) ? L->adv[u] : 0;
}

// Lebar piksel karakter pada t[r] dan jumlah byte yang dipakai (*step). Mengenali UTF-8; byte rusak = '?'.
static int ebCharW(const EbLay* L, const char* t, uint32_t r, uint32_t len, int* step) {
  uint8_t u = (uint8_t)t[r];
  if (u < 0x80) { *step = 1; return (u >= 32 && u < 127) ? L->adv[u] : 0; }
  uint32_t cp; bool bad;
  int n = ebUtf8((const uint8_t*)t + r, len - r, &cp, &bad);
  if (n <= 0 || bad) { *step = 1; return L->adv['?']; }
  *step = n;
  if (L->uni && L->cpw && ebUniKeep(cp)) { int w = L->cpw(cp); if (w > 0) return w; }
  char f[4]; int fn = ebCpToAscii(cp, f);
  if (fn < 0) return L->adv['?'];
  int w = 0; for (int i = 0; i < fn; i++) w += ebAdv(L, f[i]);
  return w;
}

// Ubah teks t[a,b) (boleh UTF-8) menjadi string yang BISA digambar font aktif: codepoint yang dicakup
// font Unicode dipertahankan, sisanya dilipat ke ASCII. *wpx = lebar piksel (sama dgn yang dipakai tata letak).
static int ebRunText(const EbLay* L, const char* t, uint32_t a, uint32_t b, char* out, int cap, int* wpx) {
  int w = 0, o = 0; uint32_t r = a;
  while (r < b && o < cap - 5) {
    uint8_t u = (uint8_t)t[r];
    if (u < 0x80) {
      if (u >= 32 && u < 127) { out[o++] = (char)u; w += L->adv[u]; }
      r++; continue;
    }
    uint32_t cp; bool bad;
    int n = ebUtf8((const uint8_t*)t + r, b - r, &cp, &bad);
    if (n <= 0 || bad) { out[o++] = '?'; w += L->adv['?']; r++; continue; }   // rusak/terpotong: sama dgn ebCharW
    r += (uint32_t)n;
    if (L->uni && L->cpw && ebUniKeep(cp)) {
      int cw = L->cpw(cp);
      if (cw > 0) { o += ebEncUtf8(cp, out + o); w += cw; continue; }
    }
    char f[4]; int fn = ebCpToAscii(cp, f);
    if (fn < 0) { out[o++] = '?'; w += L->adv['?']; continue; }
    for (int i = 0; i < fn; i++) if ((uint8_t)f[i] >= 32 && (uint8_t)f[i] < 127) { out[o++] = f[i]; w += ebAdv(L, f[i]); }
  }
  out[o] = 0;
  if (wpx) *wpx = w;
  return o;
}

// Salin teks UTF-8 sebagai ASCII murni (judul/pengarang untuk UI berfont ASCII). Urutan terpotong dibuang.
static void ebFoldAscii(const char* src, char* out, int cap) {
  int o = 0; uint32_t n = (uint32_t)strlen(src), r = 0;
  while (r < n && o < cap - 5) {
    uint8_t u = (uint8_t)src[r];
    if (u < 0x80) { if (u >= 32 && u != 0x7F) out[o++] = (char)u; r++; continue; }
    uint32_t cp; bool bad;
    int k = ebUtf8((const uint8_t*)src + r, n - r, &cp, &bad);
    if (k <= 0) break;
    if (bad) { out[o++] = '?'; r++; continue; }
    r += (uint32_t)k;
    char f[4]; int fn = ebCpToAscii(cp, f);
    if (fn < 0) { out[o++] = '?'; continue; }
    for (int i = 0; i < fn; i++) if ((uint8_t)f[i] >= 32 && (uint8_t)f[i] < 127) out[o++] = f[i];
  }
  out[o] = 0;
}

// apakah paragraf yang memuat pos adalah judul? (untuk halaman yang mulai di tengah paragraf)
static bool ebParaIsHead(const char* t, uint32_t len, uint32_t pos) {
  if (pos >= len) return false;
  if (pos == 0 || t[pos - 1] == '\n') return t[pos] == 1;
  uint32_t k = pos; int lim = 400;
  while (k > 0 && lim-- > 0 && t[k - 1] != '\n') k--;
  if (k > 0 && t[k - 1] != '\n') return false;
  return k < len && t[k] == 1;
}

// Tata satu baris yang mulai di pos. head = status judul bawaan (untuk baris lanjutan).
static bool ebLine(const char* t, uint32_t len, uint32_t pos, bool head, const EbLay* L, EbLine* o) {
  if (pos >= len) return false;
  uint32_t p = pos;
  bool para = (pos == 0 || t[pos - 1] == '\n');
  if (t[p] == 1) { head = true; p++; para = true; }
  else if (para) head = false;
  o->head = head ? 1 : 0; o->empty = 0; o->nextHead = 0; o->endPara = 0; o->split = 0; o->img = -1;
  o->start = p; o->x = 0; o->w = 0;
  if (p < len && t[p] == 2 && para) {               // penanda gambar: \x02 <angka> \n
    int32_t id = 0; uint32_t q = p + 1;
    while (q < len && t[q] >= '0' && t[q] <= '9' && id < 100000) { id = id * 10 + (t[q] - '0'); q++; }
    while (q < len && t[q] != '\n') q++;
    o->end = q; o->next = (q < len) ? q + 1 : q; o->empty = 1; o->endPara = 1; o->img = id;
    o->head = 0;
    if (o->next <= pos) o->next = pos + 1;
    return true;
  }
  if (p >= len || t[p] == '\n') {
    o->end = p; o->next = (p < len) ? p + 1 : p; o->empty = 1; o->endPara = 1;
    if (o->next <= pos) o->next = pos + 1;
    return true;
  }
  int avail = L->width - ((para && !head) ? L->indent : 0);
  if (avail < 8) avail = 8;
  int sp = ebAdv(L, ' ');
  int w = 0; uint32_t lineEnd = p, q = p, next = p;
  for (;;) {
    if (q >= len || t[q] == '\n') { lineEnd = (w > 0) ? lineEnd : q; next = (q < len) ? q + 1 : q; o->nextHead = 0; o->endPara = 1; break; }
    uint32_t r = q; int ww = 0, st = 1;
    while (r < len && t[r] != ' ' && t[r] != '\n') { ww += ebCharW(L, t, r, len, &st); r += (uint32_t)st; }
    int add = ww + (w > 0 ? sp : 0);
    if (w + add <= avail) {
      w += add; lineEnd = r; q = r;
      if (q < len && t[q] == ' ') q++;
      continue;
    }
    if (w == 0) {                                  // satu kata lebih lebar dari baris -> potong per huruf
      uint32_t c = q; int cw = 0, cs = 1;
      for (;;) {
        if (c >= r) break;
        int w1 = ebCharW(L, t, c, len, &cs);
        if (cw + w1 > avail) break;
        cw += w1; c += (uint32_t)cs;
      }
      if (c == q) { cw = ebCharW(L, t, c, len, &cs); c += (uint32_t)cs; }
      lineEnd = c; w = cw; next = c; o->nextHead = head ? 1 : 0; o->split = 1;
      break;
    }
    next = q; o->nextHead = head ? 1 : 0;           // bungkus sebelum kata ini
    break;
  }
  o->end = lineEnd; o->next = next; o->w = (int16_t)w;
  if (o->next <= pos) o->next = pos + 1;
  if (head) o->x = (int16_t)((L->width - w) / 2);
  else if (para) o->x = L->indent;
  return true;
}

// Paginasi seluruh teks. *pg = larik awal halaman (EB_REALLOC), *cap = kapasitas. Mengembalikan jumlah halaman.
// v2: penanda gambar (\x02) selalu menempati satu halaman sendiri.
static uint32_t ebPaginate(const char* t, uint32_t len, const EbLay* L, uint32_t** pg, uint32_t* cap) {
  uint32_t n = 0, pos = 0; bool head = false;
  int rows = L->rows < 1 ? 1 : L->rows;
  while (pos < len) {
    while (pos < len && t[pos] == '\n') pos++;        // baris kosong di puncak halaman dibuang
    if (pos >= len) break;
    if (n >= *cap) {
      uint32_t nc = *cap ? *cap * 2 : 256;
      uint32_t* np = (uint32_t*)EB_REALLOC(*pg, sizeof(uint32_t) * nc);
      if (!np) return n;
      *pg = np; *cap = nc;
    }
    (*pg)[n++] = pos;
    if (t[pos] == 2) {                                // halaman gambar
      EbLine il;
      if (ebLine(t, len, pos, false, L, &il)) pos = il.next; else pos++;
      if ((n & 255u) == 0) EB_YIELD();
      continue;
    }
    head = ebParaIsHead(t, len, pos);
    EbLine ln;
    for (int r = 0; r < rows && pos < len; r++) {
      if (t[pos] == 2) break;                         // gambar memulai halaman baru
      if (!ebLine(t, len, pos, head, L, &ln)) break;
      pos = ln.next; head = ln.nextHead != 0;
    }
    if ((n & 255u) == 0) EB_YIELD();
  }
  return n;
}

// halaman yang memuat offset (pencarian biner)
static uint32_t ebPageOf(const uint32_t* pg, uint32_t n, uint32_t off) {
  if (n == 0) return 0;
  uint32_t lo = 0, hi = n - 1;
  while (lo < hi) { uint32_t mid = (lo + hi + 1) / 2; if (pg[mid] <= off) lo = mid; else hi = mid - 1; }
  return lo;
}

#endif // EBOOK_CORE_H
