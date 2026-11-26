#pragma once
// =====================================================================
//  jam_core.h -- parser JSON Jamendo (tanpa dependensi Arduino, bisa dites di PC)
//  Mengambil dari tiap objek di "results": name, artist_name, audio (URL MP3).
// =====================================================================
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define JAM_TITLE_LEN   48
#define JAM_ARTIST_LEN  32
#define JAM_URL_LEN     200
#define JAM_MAX_TRACKS  20

struct JamTrack { char title[JAM_TITLE_LEN]; char artist[JAM_ARTIST_LEN]; char url[JAM_URL_LEN]; uint16_t durSec; };

static int jamHex4(const char* s, uint32_t* v) {
  uint32_t r = 0;
  for (int k = 0; k < 4; k++) {
    char h = s[k]; int d;
    if (h >= '0' && h <= '9') d = h - '0';
    else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
    else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
    else return 0;
    r = r * 16 + d;
  }
  *v = r; return 1;
}
static size_t jamPutUtf8(uint32_t cp, char* o, size_t room) {
  if (cp < 0x80)        { if (room < 1) return 0; o[0] = (char)cp; return 1; }
  if (cp < 0x800)       { if (room < 2) return 0; o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000)     { if (room < 3) return 0; o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
  if (room < 4) return 0;
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}
// s = tepat setelah tanda kutip pembuka. Berhenti di kutip penutup. Hasil selalu ber-NUL.
static void jamUnescape(const char* s, char* out, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  while (*s && *s != '"' && o + 1 < cap) {
    if (*s != '\\') { out[o++] = *s++; continue; }
    s++;
    char c = *s; if (!c) break;
    s++;
    if (c == 'u') {
      uint32_t cp;
      if (!jamHex4(s, &cp)) { out[o++] = '?'; continue; }
      s += 4;
      if (cp >= 0xD800 && cp <= 0xDBFF && s[0] == '\\' && s[1] == 'u') {
        uint32_t lo;
        if (jamHex4(s + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); s += 6; }
      }
      size_t w = jamPutUtf8(cp, out + o, cap - 1 - o);
      if (!w) break;
      o += w;
    } else if (c == 'n' || c == 'r' || c == 't') out[o++] = ' ';
    else out[o++] = c;                                 // \" \\ \/
  }
  // buang byte UTF-8 yang terpotong di ujung
  size_t k = o;
  while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
  if (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) o = k - 1;
  out[o] = 0;
}
// cari "key":"nilai" di dalam objek (string ber-NUL). false kalau tidak ada / bukan string.
static bool jamFindStr(const char* obj, const char* key, char* out, size_t cap) {
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char* p = obj;
  while ((p = strstr(p, needle)) != nullptr) {
    const char* q = p + strlen(needle);
    while (*q == ' ') q++;
    if (*q != ':') { p = q; continue; }
    q++;
    while (*q == ' ') q++;
    if (*q != '"') return false;
    jamUnescape(q + 1, out, cap);
    return true;
  }
  return false;
}
// cari "key":123 atau "key":"123" (angka). -1 kalau tidak ada.
static int jamFindInt(const char* obj, const char* key) {
  char needle[32];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char* p = strstr(obj, needle);
  if (!p) return -1;
  p += strlen(needle);
  while (*p == ' ') p++;
  if (*p != ':') return -1;
  p++;
  while (*p == ' ' || *p == '"') p++;
  if (*p < '0' || *p > '9') return -1;
  long v = 0;
  while (*p >= '0' && *p <= '9' && v < 100000) { v = v * 10 + (*p - '0'); p++; }
  return (int)v;
}
// Parse isi respons /tracks. json DIUBAH (NUL disisipkan). Return jumlah track terisi.
static int jamParse(char* json, JamTrack* out, int maxN) {
  char* p = strstr(json, "\"results\"");
  if (!p) return 0;
  p = strchr(p, '[');
  if (!p) return 0;
  p++;
  int n = 0;
  while (n < maxN) {
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') break;
    char* start = p;
    int depth = 0; bool inStr = false;
    for (; *p; p++) {
      char c = *p;
      if (inStr) { if (c == '\\' && p[1]) p++; else if (c == '"') inStr = false; continue; }
      if (c == '"') inStr = true;
      else if (c == '{') depth++;
      else if (c == '}') { depth--; if (depth == 0) break; }
    }
    if (*p != '}') break;                              // objek terpotong
    *p = 0;                                            // akhiri objek (aman: kita maju terus)
    JamTrack& t = out[n];
    t.title[0] = t.artist[0] = t.url[0] = 0; t.durSec = 0;
    jamFindStr(start, "name", t.title, sizeof(t.title));
    jamFindStr(start, "artist_name", t.artist, sizeof(t.artist));
    { int d = jamFindInt(start, "duration"); t.durSec = (d > 0 && d < 65535) ? (uint16_t)d : 0; }
    bool ok = jamFindStr(start, "audio", t.url, sizeof(t.url)) && strncmp(t.url, "http", 4) == 0;
    if (ok) { if (!t.title[0]) strcpy(t.title, "(tanpa judul)"); n++; }
    p++;
  }
  return n;
}
// URL-encode sederhana untuk query pencarian
static void jamUrlEnc(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 4 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
    else { snprintf(out + o, 4, "%%%02X", c); o += 3; }
  }
  out[o] = 0;
}
