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
#define JAM_IMG_LEN     160    // COVER_NET_V1: URL gambar cover (Jamendo "image" / Audius "artwork")
#define JAM_MAX_TRACKS  40     // JAM_PARTIAL_V1: 20 -> 40

struct JamTrack { char title[JAM_TITLE_LEN]; char artist[JAM_ARTIST_LEN]; char url[JAM_URL_LEN]; char img[JAM_IMG_LEN]; uint16_t durSec; };

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
  // buang byte UTF-8 yang terpotong di ujung (AUDIUS_V1: hanya kalau urutannya memang belum lengkap;
  // dulu satu huruf non-ASCII yang utuh di akhir teks ikut terbuang, mis. nama artis berakhiran "Ü")
  size_t k = o;
  while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
  if (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) {
    unsigned char lead = (unsigned char)out[k - 1];
    size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
    if (o - (k - 1) < need) o = k - 1;
  }
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
    t.title[0] = t.artist[0] = t.url[0] = t.img[0] = 0; t.durSec = 0;
    jamFindStr(start, "name", t.title, sizeof(t.title));
    jamFindStr(start, "artist_name", t.artist, sizeof(t.artist));
    { int d = jamFindInt(start, "duration"); t.durSec = (d > 0 && d < 65535) ? (uint16_t)d : 0; }
    bool ok = jamFindStr(start, "audio", t.url, sizeof(t.url)) && strncmp(t.url, "http", 4) == 0;
    if (ok) {
      jamFindStr(start, "image", t.img, sizeof(t.img));              // COVER_NET_V1: URL cover lagu/album
      if (strncmp(t.img, "http", 4) != 0) t.img[0] = 0;
      if (!t.title[0]) strcpy(t.title, "(tanpa judul)");
      n++;
    }
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

// =====================================================================
//  JAM_PARTIAL_V1 -- urutan hasil untuk pencarian sebagian (tanpa dependensi Arduino)
//  skor: 0 judul diawali ketikan | 1 ketikan di awal sebuah kata | 2 ada di dalam judul | 3 ada di nama artis | 4 tidak ada
//  Huruf besar/kecil dianggap sama (ASCII). Urutan asal dipertahankan di dalam skor yang sama (stabil).
// =====================================================================
static char jamLow(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool jamIsWordCh(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || ((unsigned char)c >= 0x80);
}
// offset kemunculan pertama nd di hay (tanpa beda besar/kecil), -1 kalau tidak ada
static int jamCiFind(const char* hay, const char* nd) {
  size_t nl = strlen(nd);
  if (nl == 0) return 0;
  for (int i = 0; hay[i]; i++) {
    size_t k = 0;
    while (k < nl && hay[i + k] && jamLow(hay[i + k]) == jamLow(nd[k])) k++;
    if (k == nl) return i;
    if (!hay[i + k]) break;
  }
  return -1;
}
static int jamScore(const JamTrack& t, const char* q) {
  size_t nl = strlen(q);
  int best = 4;
  for (int i = 0; t.title[i]; i++) {
    size_t k = 0;
    while (k < nl && t.title[i + k] && jamLow(t.title[i + k]) == jamLow(q[k])) k++;
    if (k == nl) {
      int s = (i == 0) ? 0 : (!jamIsWordCh(t.title[i - 1]) ? 1 : 2);
      if (s < best) best = s;
      if (best == 0) return 0;
    } else if (!t.title[i + k]) break;
  }
  if (best == 4 && jamCiFind(t.artist, q) >= 0) best = 3;
  return best;
}
static void jamRank(JamTrack* a, int n, const char* q) {
  if (!a || !q || !q[0] || n < 2 || n > JAM_MAX_TRACKS) return;
  int sc[JAM_MAX_TRACKS];
  for (int i = 0; i < n; i++) sc[i] = jamScore(a[i], q);
  for (int i = 1; i < n; i++) {                        // insertion sort stabil (n <= 40)
    JamTrack tmp = a[i]; int s = sc[i]; int j = i - 1;
    while (j >= 0 && sc[j] > s) { a[j + 1] = a[j]; sc[j + 1] = sc[j]; j--; }
    a[j + 1] = tmp; sc[j + 1] = s;
  }
}
// =====================================================================
//  AUDIUS_V1 -- parser respons Audius (/v1/tracks/search dan /v1/tracks/trending)
//  Bentuk: {"data":[{ "id":"D7KyD", "title":"..", "duration":180, "user":{"id":"..","name":".."}, ... }]}
//  Objek track berisi objek bersarang (artwork, user, access, ...) yang juga punya "id" dan "name",
//  jadi kunci DICARI HANYA di kedalaman 1 (audKey), bukan strstr seperti jamFindStr.
//  Lagu yang tidak bisa diputar (is_streamable:false, is_stream_gated:true, access.stream:false) dilewati.
// =====================================================================
#define AUD_BASE  "https://api.audius.co/v1"
#define AUD_APP   "ren_phone"

// pointer ke nilai (setelah ':' dan spasi) dari "key" di kedalaman 1 objek; nullptr kalau tidak ada.
// obj harus diawali '{' atau '['. Untuk objek bersarang: audKey(audKey(o,"user"),"name").
static const char* audKey(const char* obj, const char* key) {
  if (!obj) return nullptr;
  size_t kl = strlen(key); int depth = 0;
  for (const char* p = obj; *p; p++) {
    char c = *p;
    if (c == '"') {
      const char* s = p + 1; const char* q = s;
      while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
      if (!*q) return nullptr;
      if (depth == 1 && (size_t)(q - s) == kl && strncmp(s, key, kl) == 0) {
        const char* v = q + 1;
        while (*v == ' ') v++;
        if (*v == ':') { v++; while (*v == ' ') v++; return v; }
      }
      p = q; continue;
    }
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') depth--;
  }
  return nullptr;
}
static bool audStr(const char* obj, const char* key, char* out, size_t cap) {
  const char* v = audKey(obj, key);
  if (!v || *v != '"') return false;
  jamUnescape(v + 1, out, cap);
  return true;
}
static int audInt(const char* obj, const char* key) {            // -1 kalau tidak ada / bukan angka
  const char* v = audKey(obj, key);
  if (!v || *v < '0' || *v > '9') return -1;
  long n = 0;
  while (*v >= '0' && *v <= '9' && n < 100000) { n = n * 10 + (*v - '0'); v++; }
  return (int)n;
}
static bool audIsFalse(const char* obj, const char* key) { const char* v = audKey(obj, key); return v && strncmp(v, "false", 5) == 0; }
static bool audIsTrue (const char* obj, const char* key) { const char* v = audKey(obj, key); return v && strncmp(v, "true", 4) == 0; }

// id Audius = huruf/angka saja (mis. "D7KyD"); apa pun selain itu ditolak supaya URL aman
static bool audIdOk(const char* id) {
  size_t n = strlen(id);
  if (n < 2 || n > 24) return false;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;
}

// Parse respons. json DIUBAH (NUL disisipkan). Isi out[] dengan JamTrack yang sama dengan Jamendo:
// url = https://api.audius.co/v1/tracks/<id>/stream?app_name=ren_phone (server membalas 302 ke file MP3).
static int audParse(char* json, JamTrack* out, int maxN) {
  char* p = strstr(json, "\"data\"");
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
    *p = 0;
    JamTrack& t = out[n];
    t.title[0] = t.artist[0] = t.url[0] = t.img[0] = 0; t.durSec = 0;
    char id[28]; id[0] = 0;
    bool ok = audStr(start, "id", id, sizeof(id)) && audIdOk(id);
    if (ok && (audIsFalse(start, "is_streamable") || audIsTrue(start, "is_stream_gated"))) ok = false;
    if (ok) { const char* ac = audKey(start, "access"); if (ac && *ac == '{' && audIsFalse(ac, "stream")) ok = false; }
    if (ok) {
      audStr(start, "title", t.title, sizeof(t.title));
      const char* u = audKey(start, "user");
      if (u && *u == '{') audStr(u, "name", t.artist, sizeof(t.artist));
      int d = audInt(start, "duration"); t.durSec = (d > 0 && d < 65535) ? (uint16_t)d : 0;
      snprintf(t.url, sizeof(t.url), AUD_BASE "/tracks/%s/stream?app_name=" AUD_APP, id);
      const char* aw = audKey(start, "artwork");                     // COVER_NET_V1: {"150x150":"https://..","480x480":..}
      if (aw && *aw == '{') { if (!audStr(aw, "150x150", t.img, sizeof(t.img))) audStr(aw, "480x480", t.img, sizeof(t.img)); }
      if (strncmp(t.img, "http", 4) != 0) t.img[0] = 0;
      if (!t.title[0]) strcpy(t.title, "(tanpa judul)");
      n++;
    }
    p++;
  }
  return n;
}

// chip genre aplikasi (tag Jamendo huruf kecil) -> nama genre Audius untuk trending; "" = semua genre
static const char* audGenre(const char* tag) {
  if (!tag || !tag[0]) return "";
  if (!strcmp(tag, "pop"))  return "Pop";
  if (!strcmp(tag, "rock")) return "Rock";
  if (!strcmp(tag, "lofi")) return "Lo-Fi";
  if (!strcmp(tag, "jazz")) return "Jazz";
  return "";
}
