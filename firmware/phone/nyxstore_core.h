// =====================================================================
// nyxstore_core.h -- logika murni "Nyx Store" (tanpa Arduino; diuji di PC)
// =====================================================================
// Isi: pembaca indeks katalog (nyxapps.json), validasi id/nama berkas/SHA-256,
// pembaca catatan pasang (store.txt), penentu status (belum/terbaru/ada update).
// Firmware (UI, jaringan, tulis berkas) ada di nyxstore_renphone.ino.
//
// Format indeks (dibuat tools/nyxapps_release.py):
//   { "commit": "...", "apps": [ { "id": "hello", "name": "Hello Native", "sym": "H",
//       "color": "3060d0", "perms": ["notify"], "mem_kb": 64, "elf_size": 4180,
//       "sha256": "<64 hex>", "elf": "hello.elf", "manifest": "hello.manifest.txt",
//       "kind": "contoh" }, ... ] }
// Indeks lama (tanpa "elf"/"manifest") tetap terbaca: nama berkas diturunkan dari id.
//
// Pembaca JSON ditulis sendiri (CI tidak memasang ArduinoJson): cukup untuk objek datar
// berisi string/angka/larik-string, toleran terhadap spasi dan escape dasar.
// =====================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NYXSTORE_MAX_APPS   24
#define NYXSTORE_ELF_MAX    (1024u * 1024u)   // sama dengan NYXAPP_ELF_MAX di nyxapp_renphone.ino
#define NYXSTORE_INDEX_MAX  (24u * 1024u)

namespace nyxstore {

struct Entry {
  char     id[24];
  char     name[24];
  char     sym;
  uint16_t color;        // RGB565
  char     perms[28];    // "fs,net" (apa adanya dari indeks)
  char     sha[65];      // SHA-256 hex huruf kecil dari app.elf
  char     elf[40];      // nama aset ELF di rilis
  char     manifest[40]; // nama aset manifest di rilis
  uint32_t elfSize;
  uint32_t memKb;
  bool     test;         // kind == "uji" (app uji internal)
};

enum State { ST_NOT_INSTALLED = 0, ST_UP_TO_DATE = 1, ST_UPDATE = 2, ST_UNKNOWN = 3 };

// ------------------------------ validasi ------------------------------
inline bool idOk(const char* id) {   // aturan SAMA dengan nyxIdOk() di nyxapp_renphone.ino
  size_t n = strlen(id); if (n == 0 || n >= 24) return false;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

inline bool fileNameOk(const char* f) {   // nama aset: tanpa '/', '\\', tanpa titik di depan
  size_t n = strlen(f); if (n == 0 || n >= 40) return false;
  if (f[0] == '.') return false;
  for (size_t i = 0; i < n; i++) {
    char c = f[i];
    if (c == '/' || c == '\\' || c < 0x21 || c > 0x7E) return false;
  }
  return true;
}

inline bool shaOk(const char* s) {   // 64 hex huruf kecil
  if (strlen(s) != 64) return false;
  for (int i = 0; i < 64; i++) { char c = s[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; }
  return true;
}

inline void toHex(const uint8_t* d, size_t n, char* out) {   // out >= 2n+1
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = H[d[i] >> 4]; out[2 * i + 1] = H[d[i] & 15]; }
  out[2 * n] = 0;
}

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

// ------------------------------ pembaca JSON mini ------------------------------
struct Cur { const char* p; const char* e; };

inline void skipWs(Cur& c) { while (c.p < c.e && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) c.p++; }

// baca string JSON ("...") ke out (dipotong bila melebihi cap-1). false bila bukan string / tak tertutup.
inline bool readStr(Cur& c, char* out, size_t cap) {
  skipWs(c);
  if (c.p >= c.e || *c.p != '"') return false;
  c.p++;
  size_t n = 0;
  while (c.p < c.e && *c.p != '"') {
    char ch = *c.p++;
    if (ch == '\\' && c.p < c.e) {
      char x = *c.p++;
      if (x == 'n') ch = '\n'; else if (x == 't') ch = '\t'; else if (x == 'r') ch = '\r';
      else if (x == 'u') { ch = '?'; for (int k = 0; k < 4 && c.p < c.e; k++) c.p++; }   // \uXXXX -> '?'
      else ch = x;                                                                         // \" \\ \/
    }
    if (n + 1 < cap) out[n++] = ch;
  }
  if (c.p >= c.e) return false;   // tak ada penutup
  c.p++;
  out[n] = 0;
  return true;
}

// lewati satu nilai JSON apa pun (untuk kunci yang tak dikenal). Kedalaman dibatasi.
inline bool skipValue(Cur& c, int depth = 0) {
  skipWs(c);
  if (c.p >= c.e || depth > 6) return false;
  char ch = *c.p;
  if (ch == '"') { char tmp[8]; return readStr(c, tmp, sizeof(tmp)); }
  if (ch == '{' || ch == '[') {
    char close = (ch == '{') ? '}' : ']'; c.p++;
    skipWs(c);
    if (c.p < c.e && *c.p == close) { c.p++; return true; }
    for (;;) {
      if (ch == '{') { char k[8]; if (!readStr(c, k, sizeof(k))) return false; skipWs(c); if (c.p >= c.e || *c.p != ':') return false; c.p++; }
      if (!skipValue(c, depth + 1)) return false;
      skipWs(c);
      if (c.p < c.e && *c.p == ',') { c.p++; continue; }
      if (c.p < c.e && *c.p == close) { c.p++; return true; }
      return false;
    }
  }
  while (c.p < c.e && *c.p != ',' && *c.p != '}' && *c.p != ']' && *c.p != ' ' && *c.p != '\n' && *c.p != '\r' && *c.p != '\t') c.p++;   // angka / true / false / null
  return true;
}

// larik string -> "a,b,c" di out
inline bool readStrArray(Cur& c, char* out, size_t cap) {
  skipWs(c);
  if (c.p >= c.e || *c.p != '[') return false;
  c.p++; size_t n = 0; out[0] = 0;
  skipWs(c);
  if (c.p < c.e && *c.p == ']') { c.p++; return true; }
  for (;;) {
    char item[16];
    if (!readStr(c, item, sizeof(item))) return false;
    size_t il = strlen(item);
    if (n && n + 1 < cap) out[n++] = ',';
    for (size_t i = 0; i < il && n + 1 < cap; i++) out[n++] = item[i];
    out[n] = 0;
    skipWs(c);
    if (c.p < c.e && *c.p == ',') { c.p++; continue; }
    if (c.p < c.e && *c.p == ']') { c.p++; return true; }
    return false;
  }
}

inline bool readUInt(Cur& c, uint32_t* v) {
  skipWs(c);
  if (c.p >= c.e || *c.p < '0' || *c.p > '9') return false;
  uint64_t x = 0;
  while (c.p < c.e && *c.p >= '0' && *c.p <= '9') { x = x * 10 + (uint64_t)(*c.p - '0'); if (x > 0xFFFFFFFFull) x = 0xFFFFFFFFull; c.p++; }
  *v = (uint32_t)x;
  return true;
}

inline void copyStr(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src); if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = 0;
}

// satu objek app. Mengembalikan false bila JSON-nya rusak (berhenti total); *valid = isi lolos validasi.
inline bool parseApp(Cur& c, Entry* e, bool* valid) {
  memset(e, 0, sizeof(*e)); e->sym = '?'; e->color = rgb565(0x80, 0x80, 0x80);
  char colorHex[16] = {0}, kind[16] = {0};
  skipWs(c);
  if (c.p >= c.e || *c.p != '{') return false;
  c.p++; skipWs(c);
  if (c.p < c.e && *c.p == '}') { c.p++; *valid = false; return true; }
  for (;;) {
    char key[24];
    if (!readStr(c, key, sizeof(key))) return false;
    skipWs(c); if (c.p >= c.e || *c.p != ':') return false; c.p++; skipWs(c);
    char tmp[96]; bool ok = true;
    if (!strcmp(key, "id"))            ok = readStr(c, tmp, sizeof(tmp)) && (copyStr(e->id, sizeof(e->id), tmp), true);
    else if (!strcmp(key, "name"))     ok = readStr(c, tmp, sizeof(tmp)) && (copyStr(e->name, sizeof(e->name), tmp), true);
    else if (!strcmp(key, "sym"))      { ok = readStr(c, tmp, sizeof(tmp)); if (ok && tmp[0] >= 0x21 && tmp[0] < 0x7F) e->sym = tmp[0]; }
    else if (!strcmp(key, "color"))    ok = readStr(c, colorHex, sizeof(colorHex));
    else if (!strcmp(key, "perms"))    ok = readStrArray(c, e->perms, sizeof(e->perms));
    else if (!strcmp(key, "sha256"))   ok = readStr(c, tmp, sizeof(tmp)) && (copyStr(e->sha, sizeof(e->sha), tmp), true);
    else if (!strcmp(key, "elf"))      ok = readStr(c, tmp, sizeof(tmp)) && (copyStr(e->elf, sizeof(e->elf), tmp), true);
    else if (!strcmp(key, "manifest")) ok = readStr(c, tmp, sizeof(tmp)) && (copyStr(e->manifest, sizeof(e->manifest), tmp), true);
    else if (!strcmp(key, "kind"))     ok = readStr(c, kind, sizeof(kind));
    else if (!strcmp(key, "elf_size")) ok = readUInt(c, &e->elfSize);
    else if (!strcmp(key, "mem_kb"))   ok = readUInt(c, &e->memKb);
    else                               ok = skipValue(c);
    if (!ok) return false;
    skipWs(c);
    if (c.p < c.e && *c.p == ',') { c.p++; continue; }
    if (c.p < c.e && *c.p == '}') { c.p++; break; }
    return false;
  }
  if (strlen(colorHex) == 6) {
    int h[6]; bool good = true;
    for (int k = 0; k < 6; k++) { h[k] = hexVal(colorHex[k]); if (h[k] < 0) good = false; }
    if (good) e->color = rgb565((uint32_t)(h[0] * 16 + h[1]), (uint32_t)(h[2] * 16 + h[3]), (uint32_t)(h[4] * 16 + h[5]));
  }
  e->test = !strcmp(kind, "uji");
  if (!e->elf[0])      snprintf(e->elf, sizeof(e->elf), "%s.elf", e->id);                 // indeks lama
  if (!e->manifest[0]) snprintf(e->manifest, sizeof(e->manifest), "%s.manifest.txt", e->id);
  // kecilkan huruf SHA supaya perbandingan konsisten
  for (char* q = e->sha; *q; q++) if (*q >= 'A' && *q <= 'F') *q = (char)(*q - 'A' + 'a');
  *valid = idOk(e->id) && e->name[0] && shaOk(e->sha) && fileNameOk(e->elf) && fileNameOk(e->manifest)
           && e->elfSize >= 64 && e->elfSize <= NYXSTORE_ELF_MAX;
  return true;
}

// Baca seluruh indeks. Mengembalikan jumlah app VALID di out (<= maxN); *skipped = entri ditolak.
// -1 bila bukan indeks yang masuk akal (tak ada larik "apps" / JSON rusak sebelum satu app pun terbaca).
inline int parseIndex(const char* json, size_t len, Entry* out, int maxN, int* skipped) {
  if (skipped) *skipped = 0;
  Cur c = { json, json + len };
  // cari "apps" sebagai KUNCI tingkat atas: telusuri objek akar
  skipWs(c);
  if (c.p >= c.e || *c.p != '{') return -1;
  c.p++;
  bool found = false;
  for (;;) {
    char key[24];
    skipWs(c);
    if (c.p < c.e && *c.p == '}') break;
    if (!readStr(c, key, sizeof(key))) return -1;
    skipWs(c); if (c.p >= c.e || *c.p != ':') return -1; c.p++;
    skipWs(c);
    if (!strcmp(key, "apps")) { found = true; break; }
    if (!skipValue(c)) return -1;
    skipWs(c);
    if (c.p < c.e && *c.p == ',') c.p++;
  }
  if (!found) return -1;
  skipWs(c);
  if (c.p >= c.e || *c.p != '[') return -1;
  c.p++;
  int n = 0, bad = 0;
  skipWs(c);
  if (c.p < c.e && *c.p == ']') return 0;
  for (;;) {
    Entry e; bool valid = false;
    if (!parseApp(c, &e, &valid)) { if (n == 0 && bad == 0) return -1; bad++; break; }
    if (valid) {
      bool dup = false;
      for (int i = 0; i < n; i++) if (!strcmp(out[i].id, e.id)) dup = true;   // id ganda: abaikan yang kedua
      if (!dup && n < maxN) out[n++] = e; else bad++;
    } else bad++;
    skipWs(c);
    if (c.p < c.e && *c.p == ',') { c.p++; continue; }
    break;
  }
  if (skipped) *skipped = bad;
  return n;
}

// ------------------------------ status pasang ------------------------------
// catatan pasang  /apps/<id>/store.txt  :  baris "sha256=<64 hex>"
inline bool parseRecord(const char* text, size_t len, char* shaOut /*65*/) {
  shaOut[0] = 0;
  size_t i = 0;
  while (i < len) {
    char line[96]; size_t n = 0;
    while (i < len && text[i] != '\n') { if (n + 1 < sizeof(line) && text[i] != '\r') line[n++] = text[i]; i++; }
    i++; line[n] = 0;
    if (!strncmp(line, "sha256=", 7) && strlen(line + 7) == 64) {
      char t[65]; copyStr(t, sizeof(t), line + 7);
      for (char* q = t; *q; q++) if (*q >= 'A' && *q <= 'F') *q = (char)(*q - 'A' + 'a');
      if (shaOk(t)) { memcpy(shaOut, t, 65); return true; }
    }
  }
  return false;
}

// installed = folder app ada dan manifest-nya sah; recSha = isi store.txt ("" bila tak ada)
inline State stateFor(bool installed, const char* recSha, const char* indexSha) {
  if (!installed) return ST_NOT_INSTALLED;
  if (!recSha || !recSha[0]) return ST_UNKNOWN;     // dipasang manual (tanpa catatan Store)
  return strcmp(recSha, indexSha) == 0 ? ST_UP_TO_DATE : ST_UPDATE;
}

// ------------------------------ URL & format ------------------------------
// base harus berakhiran '/'. false bila tidak muat.
inline bool joinUrl(const char* base, const char* file, char* out, size_t cap) {
  int n = snprintf(out, cap, "%s%s", base, file);
  return n > 0 && (size_t)n < cap;
}

inline void fmtSize(uint32_t bytes, char* out, size_t cap) {
  if (bytes < 1024) snprintf(out, cap, "%u B", (unsigned)bytes);
  else if (bytes < 10u * 1024u) snprintf(out, cap, "%u.%u KB", (unsigned)(bytes / 1024), (unsigned)((bytes % 1024) * 10 / 1024));
  else if (bytes < 1024u * 1024u) snprintf(out, cap, "%u KB", (unsigned)((bytes + 512) / 1024));
  else snprintf(out, cap, "%u.%02u MB", (unsigned)(bytes >> 20), (unsigned)(((bytes & 0xFFFFF) * 100) >> 20));
}

}  // namespace nyxstore
