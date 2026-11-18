// =====================================================================
// gutendex_core.h -- logika murni (tanpa Arduino) utk fitur Gutenberg di app Buku
// =====================================================================
// Isi: pembaca JSON ringan utk respons Gutendex (https://gutendex.com/books/), pembuat URL
// pencarian, pemilih URL EPUB "ringan", dan pembuat nama berkas. Proyek ini TIDAK memakai
// ArduinoJson (CI tidak memasangnya), jadi pembacanya ditulis sendiri: sekali jalan di atas
// buffer di memori, melewati semua field yang tidak dipakai (summaries, subjects, dst).
// Diuji di komputer lewat tests_gutenberg/test_gutendex.cpp.
//
// Semua fungsi static inline, tanpa alokasi heap. Buffer JSON HARUS diakhiri NUL.
// =====================================================================
#ifndef GUTENDEX_CORE_H
#define GUTENDEX_CORE_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define GUT_TITLE_LEN 64
#define GUT_AUTH_LEN  32
#define GUT_URL_LEN   96

struct GutItem {
  uint32_t id, dl;                 // id buku Gutenberg, jumlah unduhan (popularitas)
  char title[GUT_TITLE_LEN];
  char author[GUT_AUTH_LEN];       // "Nama Depan Nama Belakang" (sudah dibalik dari "Belakang, Depan")
  char lang[4];                    // bahasa pertama ("en", "id", ...)
  char epub[GUT_URL_LEN];          // "" kalau tak ada
  char txt[GUT_URL_LEN];           // "" kalau tak ada
};

// ---------------------------------------------------------------------
// JSON: dasar
// ---------------------------------------------------------------------
static inline const char* gutWs(const char* p, const char* e) {
  while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

static inline void gutUtf8Put(char* out, int cap, int* n, uint32_t cp, bool* tr) {
  int need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
  if (*n + need >= cap) { *tr = true; return; }          // tak muat -> jangan tulis separuh huruf
  if (need == 1) out[(*n)++] = (char)cp;
  else if (need == 2) { out[(*n)++] = (char)(0xC0 | (cp >> 6)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
  else if (need == 3) { out[(*n)++] = (char)(0xE0 | (cp >> 12)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
  else { out[(*n)++] = (char)(0xF0 | (cp >> 18)); out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
}

// buang urutan UTF-8 yang terpotong di ujung string (akibat batas buffer)
static inline void gutTrimUtf8(char* s, int n) {
  int i = n;
  while (i > 0 && (((uint8_t)s[i - 1]) & 0xC0) == 0x80) i--;
  if (i > 0) {
    uint8_t lead = (uint8_t)s[i - 1];
    int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (need > 1 && (n - (i - 1)) < need) n = i - 1;
  }
  s[n] = 0;
}

// baca string JSON di p (menunjuk '"') -> out (UTF-8, dipotong di cap-1). Kembalikan pointer SETELAH '"'
// penutup, atau nullptr kalau rusak/terpotong. *trunc = true kalau isi lebih panjang dari buffer.
static inline const char* gutReadStr(const char* p, const char* e, char* out, int cap, bool* trunc) {
  bool tr = false;
  if (trunc) *trunc = false;
  if (p >= e || *p != '"' || cap < 2) return nullptr;
  p++;
  int n = 0;
  while (p < e && *p != '"') {
    if (*p == '\\') {
      p++;
      if (p >= e) return nullptr;
      char c = *p++;
      if (c == 'u') {
        if (e - p < 4) return nullptr;
        uint32_t cp = 0;
        for (int k = 0; k < 4; k++) {
          char h = p[k]; int v = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
          if (v < 0) return nullptr;
          cp = cp * 16 + (uint32_t)v;
        }
        p += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {     // pasangan surrogat
          uint32_t lo = 0; bool okh = true;
          for (int k = 0; k < 4; k++) {
            char h = p[2 + k]; int v = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
            if (v < 0) { okh = false; break; }
            lo = lo * 16 + (uint32_t)v;
          }
          if (okh && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
        }
        if (cp >= 0xD800 && cp < 0xE000) cp = '?';                                          // surrogat yatim
        gutUtf8Put(out, cap, &n, cp, &tr);
      } else {
        uint32_t cp = (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f') ? ' ' : ((uint8_t)c < 0x80 ? (uint32_t)(uint8_t)c : '?');
        gutUtf8Put(out, cap, &n, cp, &tr);
      }
    } else {
      if (n + 1 < cap) out[n++] = *p; else tr = true;
      p++;
    }
  }
  if (p >= e) return nullptr;
  gutTrimUtf8(out, n);
  if (trunc) *trunc = tr;
  return p + 1;
}

// lewati satu nilai JSON (string / angka / literal / objek / larik). nullptr kalau terpotong.
static inline const char* gutSkip(const char* p, const char* e) {
  if (p >= e) return nullptr;
  if (*p == '"') {
    p++;
    while (p < e && *p != '"') { if (*p == '\\') p++; p++; }
    if (p >= e) return nullptr;
    return p + 1;
  }
  if (*p == '{' || *p == '[') {
    int d = 0;
    while (p < e) {
      char c = *p;
      if (c == '"') {
        p++;
        while (p < e && *p != '"') { if (*p == '\\') p++; p++; }
        if (p >= e) return nullptr;
      } else if (c == '{' || c == '[') d++;
      else if (c == '}' || c == ']') { d--; if (d == 0) return p + 1; }
      p++;
    }
    return nullptr;
  }
  while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
  return p;
}

// ---------------------------------------------------------------------
// JSON: bentuk respons Gutendex
// ---------------------------------------------------------------------
// authors:[{"name":"Austen, Jane",...},...] -> nama penulis PERTAMA
static inline const char* gutParseFirstName(const char* p, const char* e, char* out, int cap) {
  if (p >= e) return nullptr;
  if (*p != '[') return gutSkip(p, e);
  p++;
  int k = 0;
  for (;;) {
    p = gutWs(p, e);
    if (p >= e) return nullptr;
    if (*p == ']') return p + 1;
    if (*p == ',') { p++; continue; }
    if (k == 0 && *p == '{') {
      p++;
      for (;;) {
        p = gutWs(p, e);
        if (p >= e) return nullptr;
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }
        char key[16];
        const char* q = gutReadStr(p, e, key, sizeof(key), nullptr);
        if (!q) return nullptr;
        p = gutWs(q, e);
        if (p >= e || *p != ':') return nullptr;
        p = gutWs(p + 1, e);
        if (p >= e) return nullptr;
        if (!strcmp(key, "name") && *p == '"') p = gutReadStr(p, e, out, cap, nullptr);
        else p = gutSkip(p, e);
        if (!p) return nullptr;
      }
    } else {
      p = gutSkip(p, e);
      if (!p) return nullptr;
    }
    k++;
  }
}

// languages:["en","fr"] -> bahasa PERTAMA
static inline const char* gutParseFirstStr(const char* p, const char* e, char* out, int cap) {
  if (p >= e) return nullptr;
  if (*p != '[') return gutSkip(p, e);
  p++;
  int k = 0;
  for (;;) {
    p = gutWs(p, e);
    if (p >= e) return nullptr;
    if (*p == ']') return p + 1;
    if (*p == ',') { p++; continue; }
    if (k == 0 && *p == '"') p = gutReadStr(p, e, out, cap, nullptr);
    else p = gutSkip(p, e);
    if (!p) return nullptr;
    k++;
  }
}

// formats:{"application/epub+zip":"https://...","text/plain; charset=utf-8":"https://...",...}
static inline const char* gutParseFormats(const char* p, const char* e, GutItem* it) {
  if (p >= e) return nullptr;
  if (*p != '{') return gutSkip(p, e);
  p++;
  bool txtUtf = false;
  for (;;) {
    p = gutWs(p, e);
    if (p >= e) return nullptr;
    if (*p == '}') return p + 1;
    if (*p == ',') { p++; continue; }
    char key[48];
    const char* q = gutReadStr(p, e, key, sizeof(key), nullptr);
    if (!q) return nullptr;
    p = gutWs(q, e);
    if (p >= e || *p != ':') return nullptr;
    p = gutWs(p + 1, e);
    if (p >= e) return nullptr;
    if (*p == '"') {
      bool isEpub = !strncmp(key, "application/epub+zip", 20);
      bool isTxt  = !strncmp(key, "text/plain", 10);
      if (isEpub || isTxt) {
        char url[GUT_URL_LEN]; bool tr = false;
        q = gutReadStr(p, e, url, sizeof(url), &tr);
        if (!q) return nullptr;
        p = q;
        if (!tr) {
          if (isEpub) { if (!it->epub[0]) strcpy(it->epub, url); }
          else {
            bool utf = strstr(key, "utf-8") != nullptr;
            if (!it->txt[0] || (utf && !txtUtf)) { strcpy(it->txt, url); txtUtf = utf; }
          }
        }
      } else p = gutSkip(p, e);
    } else p = gutSkip(p, e);
    if (!p) return nullptr;
  }
}

// "Austen, Jane" -> "Jane Austen" (hanya kalau tepat satu koma)
static inline void gutFlipName(char* s, int cap) {
  int n = (int)strlen(s), c = -1, commas = 0;
  for (int i = 0; i < n; i++) if (s[i] == ',') { commas++; if (c < 0) c = i; }
  if (commas != 1 || c <= 0 || c + 1 >= n) return;
  const char* first = s + c + 1;
  while (*first == ' ') first++;
  if (!*first) return;
  char tmp[GUT_AUTH_LEN + 8];
  snprintf(tmp, sizeof(tmp), "%s %.*s", first, c, s);
  if ((int)strlen(tmp) < cap) strcpy(s, tmp);
}

// satu objek buku. Kembalikan pointer setelah '}' (nullptr kalau terpotong). *good = punya id, judul & format unduh.
static inline const char* gutParseItem(const char* p, const char* e, GutItem* it, bool* good) {
  memset(it, 0, sizeof(*it));
  *good = false;
  if (p >= e || *p != '{') return nullptr;
  p++;
  for (;;) {
    p = gutWs(p, e);
    if (p >= e) return nullptr;
    if (*p == '}') { p++; break; }
    if (*p == ',') { p++; continue; }
    char key[24];
    const char* q = gutReadStr(p, e, key, sizeof(key), nullptr);
    if (!q) return nullptr;
    p = gutWs(q, e);
    if (p >= e || *p != ':') return nullptr;
    p = gutWs(p + 1, e);
    if (p >= e) return nullptr;
    if (!strcmp(key, "id"))                  { it->id = (uint32_t)strtoul(p, nullptr, 10); p = gutSkip(p, e); }
    else if (!strcmp(key, "download_count")) { it->dl = (uint32_t)strtoul(p, nullptr, 10); p = gutSkip(p, e); }
    else if (!strcmp(key, "title"))          { p = (*p == '"') ? gutReadStr(p, e, it->title, sizeof(it->title), nullptr) : gutSkip(p, e); }
    else if (!strcmp(key, "authors"))        p = gutParseFirstName(p, e, it->author, sizeof(it->author));
    else if (!strcmp(key, "languages"))      p = gutParseFirstStr(p, e, it->lang, sizeof(it->lang));
    else if (!strcmp(key, "formats"))        p = gutParseFormats(p, e, it);
    else                                     p = gutSkip(p, e);
    if (!p) return nullptr;
  }
  gutFlipName(it->author, GUT_AUTH_LEN);
  *good = it->id > 0 && it->title[0] && (it->epub[0] || it->txt[0]);
  return p;
}

// "http://" -> "https://" (Gutenberg & Gutendex melayani https; ESP32 hanya perlu satu jalur TLS)
static inline void gutHttps(char* url, int cap) {
  if (!strncmp(url, "http://", 7) && (int)strlen(url) + 1 < cap) memmove(url + 8, url + 7, strlen(url + 7) + 1), memcpy(url, "https://", 8);
}

// Parse satu halaman. Kembalikan jumlah buku yang disimpan di out[] (>=0), atau -1 kalau bukan JSON Gutendex.
// *hasNext = ada halaman berikut, *total = "count" dari server. Buku tanpa EPUB/TXT dilewati.
static inline int gutParsePage(const char* js, int len, GutItem* out, int maxOut, int* hasNext, int* total) {
  const char* p = js; const char* e = js + len;
  int n = 0;
  if (hasNext) *hasNext = 0;
  if (total) *total = 0;
  p = gutWs(p, e);
  if (p >= e || *p != '{') return -1;
  p++;
  bool sawResults = false;
  for (;;) {
    p = gutWs(p, e);
    if (p >= e) return sawResults ? n : -1;             // terpotong: pakai yang sudah utuh
    if (*p == '}') break;
    if (*p == ',') { p++; continue; }
    char key[16];
    const char* q = gutReadStr(p, e, key, sizeof(key), nullptr);
    if (!q) return sawResults ? n : -1;
    p = gutWs(q, e);
    if (p >= e || *p != ':') return sawResults ? n : -1;
    p = gutWs(p + 1, e);
    if (p >= e) return sawResults ? n : -1;
    if (!strcmp(key, "count")) { if (total) *total = (int)strtol(p, nullptr, 10); p = gutSkip(p, e); }
    else if (!strcmp(key, "next")) { if (hasNext) *hasNext = (*p == '"') ? 1 : 0; p = gutSkip(p, e); }
    else if (!strcmp(key, "results")) {
      sawResults = true;
      if (*p != '[') return -1;
      p++;
      for (;;) {
        p = gutWs(p, e);
        if (p >= e) return n;                           // larik terpotong
        if (*p == ']') { p++; break; }
        if (*p == ',') { p++; continue; }
        GutItem it; bool good = false;
        const char* r = gutParseItem(p, e, &it, &good);
        if (!r) return n;                               // objek terakhir terpotong: buang
        if (good && n < maxOut) {
          gutHttps(it.epub, GUT_URL_LEN); gutHttps(it.txt, GUT_URL_LEN);
          out[n++] = it;
        }
        p = r;
      }
    } else p = gutSkip(p, e);
    if (!p) return sawResults ? n : -1;
  }
  return sawResults ? n : -1;
}

// ---------------------------------------------------------------------
// URL & nama berkas
// ---------------------------------------------------------------------
static inline void gutUrlEncode(const char* s, char* out, int cap) {
  static const char* hx = "0123456789ABCDEF";
  int n = 0;
  for (; *s && n + 4 < cap; s++) {
    uint8_t c = (uint8_t)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[n++] = (char)c;
    else { out[n++] = '%'; out[n++] = hx[c >> 4]; out[n++] = hx[c & 15]; }
  }
  out[n] = 0;
}

// sortMode: 0 = populer (bawaan Gutendex), 1 = terbaru (id menurun). lang "" = semua bahasa.
// copyright=false: hanya buku berstatus domain publik (AS). page 1 = tanpa parameter page.
static inline void gutBuildQuery(char* out, int cap, const char* query, const char* lang, int sortMode, int page) {
  int n = snprintf(out, (size_t)cap, "https://gutendex.com/books/?copyright=false");
  if (query && query[0] && n < cap - 8) {
    char enc[120]; gutUrlEncode(query, enc, sizeof(enc));
    n += snprintf(out + n, (size_t)(cap - n), "&search=%s", enc);
  }
  if (lang && lang[0] && n < cap - 8) n += snprintf(out + n, (size_t)(cap - n), "&languages=%s", lang);
  if (sortMode == 1 && n < cap - 8) n += snprintf(out + n, (size_t)(cap - n), "&sort=descending");
  if (page > 1 && n < cap - 8) n += snprintf(out + n, (size_t)(cap - n), "&page=%d", page);
}

// EPUB "tanpa gambar" jauh lebih kecil. Gutendex memberi .../ebooks/N.epub3.images atau .epub.images;
// pola .epub.noimages adalah tautan resmi di tiap halaman buku Gutenberg. Selain itu dibiarkan.
static inline bool gutLightUrl(const char* url, char* out, int cap) {
  static const char* sfx[2] = { ".epub3.images", ".epub.images" };
  int n = (int)strlen(url);
  for (int k = 0; k < 2; k++) {
    int sl = (int)strlen(sfx[k]);
    if (n >= sl && !strcmp(url + n - sl, sfx[k])) {
      if (n - sl + 14 + 1 > cap) return false;
      memcpy(out, url, (size_t)(n - sl)); strcpy(out + n - sl, ".epub.noimages");
      return true;
    }
  }
  return false;
}

// hanya ASCII aman utk nama berkas FAT/SD; sisanya dibuang, spasi dirapatkan
static inline void gutSanitize(const char* src, char* dst, int cap) {
  int n = 0; bool sp = true;
  for (; *src && n + 1 < cap; src++) {
    uint8_t c = (uint8_t)*src;
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '(' || c == ')' || c == '\'' || c == ',' || c == '.';
    if (ok) { dst[n++] = (char)c; sp = false; }
    else if (c == ' ' || c == ':' || c == ';' || c == '/' || c == '\\') { if (!sp) { dst[n++] = ' '; sp = true; } }
  }
  while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '.')) n--;
  dst[n] = 0;
}

// "Judul - Penulis [PG1342].epub"; kalau judul kosong setelah dibersihkan -> "PG1342.epub"
static inline void gutMakeFileName(const GutItem& it, const char* ext, char* out, int cap) {
  char t[40], a[24];
  char tfull[GUT_TITLE_LEN], afull[GUT_AUTH_LEN];
  gutSanitize(it.title, tfull, sizeof(tfull)); gutSanitize(it.author, afull, sizeof(afull));
  snprintf(t, sizeof(t), "%.36s", tfull); snprintf(a, sizeof(a), "%.20s", afull);
  int tl = (int)strlen(t); while (tl > 0 && (t[tl - 1] == ' ' || t[tl - 1] == ',' || t[tl - 1] == '-')) t[--tl] = 0;   // sisa pemotongan
  int al = (int)strlen(a); while (al > 0 && (a[al - 1] == ' ' || a[al - 1] == ',' || a[al - 1] == '-')) a[--al] = 0;
  if (t[0] && a[0]) snprintf(out, (size_t)cap, "%s - %s [PG%u].%s", t, a, (unsigned)it.id, ext);
  else if (t[0])    snprintf(out, (size_t)cap, "%s [PG%u].%s", t, (unsigned)it.id, ext);
  else              snprintf(out, (size_t)cap, "PG%u.%s", (unsigned)it.id, ext);
}

// 1234 -> "1,2k"; 1500000 -> "1,5jt"
static inline void gutFmtCount(char* out, int cap, uint32_t n) {
  if (n >= 1000000u) snprintf(out, (size_t)cap, "%.1fjt", (double)n / 1000000.0);
  else if (n >= 1000u) snprintf(out, (size_t)cap, "%.1frb", (double)n / 1000.0);
  else snprintf(out, (size_t)cap, "%u", (unsigned)n);
}

#endif
