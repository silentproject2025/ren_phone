// =====================================================================
// backup_cloud.h -- inti Backup ke database (Supabase), C++ murni tanpa Arduino/ESP-IDF supaya bisa diuji di PC
// (tests/backup/test_cloud.cpp + cloud_check.py). Penghubung jaringan & NVS ada di backup_sync_renphone.ino.
//
// IDENTITAS : kode perangkat (device_id, 16 hex, turunan MAC eFuse, tidak bisa diganti) = "user".
//             Kode ini BUKAN rahasia (MAC terpancar lewat WiFi/Bluetooth), jadi tidak pernah cukup untuk membuka apa pun.
// RAHASIA   : 32 byte rahasia chip (eFuse unique ID, atau acak di NVS sebagai cadangan). Tidak pernah dikirim.
//
//   S      = HMAC-SHA256(rahasia_chip, "ren_phone.cloud.v1|" + device_id)
//   token  = hex( HMAC-SHA256(S, "auth") )   -> dikirim ke server; server hanya menyimpan SHA-256-nya
//   enc    = HMAC-SHA256(S, "enc")           -> TIDAK dikirim; kunci enkripsi file (dipakai sbg "sandi" bkEncrypt)
//
// Isi yang dikirim = file backup terenkripsi NYXE1 (AES-256-CTR + HMAC, lihat backup_crypto.h) dalam base64.
// Server tidak pernah memegang kunci `enc`, jadi tidak bisa membaca isinya. Server juga menolak apa pun yang
// bukan NYXE1 (lihat supabase/backup.sql).
// =====================================================================
#pragma once
#include <stdio.h>
#include "backup_crypto.h"

// ---------------------------------------------------------------- base64 (standar, dengan padding)
static const char bkB64Tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static inline size_t bkB64Len(size_t n) { return ((n + 2) / 3) * 4; }
// out harus punya ruang bkB64Len(n) + 1 (NUL). Mengembalikan panjang tanpa NUL, 0 = ruang kurang.
static inline size_t bkB64Enc(const uint8_t* in, size_t n, char* out, size_t cap) {
  size_t need = bkB64Len(n);
  if (cap < need + 1) return 0;
  size_t o = 0, i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    out[o++] = bkB64Tab[v >> 18]; out[o++] = bkB64Tab[(v >> 12) & 63]; out[o++] = bkB64Tab[(v >> 6) & 63]; out[o++] = bkB64Tab[v & 63];
  }
  if (n - i == 1) {
    uint32_t v = (uint32_t)in[i] << 16;
    out[o++] = bkB64Tab[v >> 18]; out[o++] = bkB64Tab[(v >> 12) & 63]; out[o++] = '='; out[o++] = '=';
  } else if (n - i == 2) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
    out[o++] = bkB64Tab[v >> 18]; out[o++] = bkB64Tab[(v >> 12) & 63]; out[o++] = bkB64Tab[(v >> 6) & 63]; out[o++] = '=';
  }
  out[o] = 0;
  return o;
}
static inline int bkB64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
// Ketat: panjang kelipatan 4, hanya alfabet standar, padding hanya di ujung. Mengembalikan jumlah byte, -1 = salah.
static inline long bkB64Dec(const char* in, size_t n, uint8_t* out, size_t cap) {
  if (n == 0 || (n & 3)) return -1;
  size_t pad = 0;
  if (in[n - 1] == '=') pad++;
  if (in[n - 2] == '=') pad++;
  for (size_t i = 0; i + pad < n; i++) if (in[i] == '=') return -1;     // '=' hanya boleh di ujung
  size_t outLen = (n / 4) * 3 - pad;
  if (outLen > cap) return -1;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    int a = bkB64Val(in[i]), b = bkB64Val(in[i + 1]);
    int c = (i + 2 < n - pad) ? bkB64Val(in[i + 2]) : 0;
    int d = (i + 3 < n - pad) ? bkB64Val(in[i + 3]) : 0;
    if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
    uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    if (o < outLen) out[o++] = (uint8_t)(v >> 16);
    if (o < outLen) out[o++] = (uint8_t)(v >> 8);
    if (o < outLen) out[o++] = (uint8_t)v;
  }
  return (long)outLen;
}

// ---------------------------------------------------------------- turunan kunci
struct BkCloudKeys { char token[65]; uint8_t enc[32]; };

// Rahasia chip dianggap layak kalau panjang >= 16 dan bukan semua 0x00 / semua 0xFF (eFuse belum diprogram).
static inline bool bkCloudUidOk(const uint8_t* uid, size_t n) {
  if (n < 16) return false;
  bool z = true, f = true;
  for (size_t i = 0; i < n; i++) { if (uid[i] != 0x00) z = false; if (uid[i] != 0xFF) f = false; }
  return !(z || f);
}

static inline void bkCloudHex(const uint8_t* d, size_t n, char* out) {     // out: 2n+1 byte
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = h[d[i] >> 4]; out[2 * i + 1] = h[d[i] & 15]; }
  out[2 * n] = 0;
}

// chip32 = 32 byte rahasia chip, devId = 16 hex (NUL-terminated).
static inline void bkCloudDerive(const uint8_t* chip32, const char* devId, BkCloudKeys& k) {
  uint8_t msg[19 + 16], s[32], t[32];
  memcpy(msg, "ren_phone.cloud.v1|", 19);
  memcpy(msg + 19, devId, 16);
  bkHmac(chip32, 32, msg, sizeof(msg), s);
  bkHmac(s, 32, (const uint8_t*)"auth", 4, t);
  bkCloudHex(t, 32, k.token);
  bkHmac(s, 32, (const uint8_t*)"enc", 3, k.enc);
  memset(s, 0, 32); memset(t, 0, 32); memset(msg, 0, sizeof(msg));
}

// "0123456789abcdef" -> "0123-4567-89ab-cdef" (out >= 20 byte). Hanya untuk tampilan.
static inline void bkCloudCodeStr(const char* devId, char* out) {
  int o = 0;
  for (int i = 0; i < 16; i++) { if (i && i % 4 == 0) out[o++] = '-'; out[o++] = devId[i]; }
  out[o] = 0;
}

// ---------------------------------------------------------------- isi permintaan (JSON)
// Ruang yang dibutuhkan bkCloudBuildPut untuk file n byte.
static inline size_t bkCloudPutCap(size_t n) { return 128 + bkB64Len(n) + 1; }

// {"p_device_id":"..","p_token":"..","p_data":"<base64 file>"} . Mengembalikan panjang, 0 = gagal.
static inline size_t bkCloudBuildPut(const uint8_t* file, size_t n, const char* devId, const char* token, char* out, size_t cap) {
  if (cap < bkCloudPutCap(n)) return 0;
  int p = 0;
  const char* a = "{\"p_device_id\":\""; const char* b = "\",\"p_token\":\""; const char* c = "\",\"p_data\":\"";
  memcpy(out + p, a, strlen(a)); p += (int)strlen(a);
  memcpy(out + p, devId, 16); p += 16;
  memcpy(out + p, b, strlen(b)); p += (int)strlen(b);
  memcpy(out + p, token, 64); p += 64;
  memcpy(out + p, c, strlen(c)); p += (int)strlen(c);
  size_t e = bkB64Enc(file, n, out + p, cap - p);
  if (!e) return 0;
  p += (int)e;
  out[p++] = '"'; out[p++] = '}'; out[p] = 0;
  return (size_t)p;
}
// {"p_device_id":"..","p_token":"..","p_meta_only":false|true}  (juga dipakai delete_backup tanpa p_meta_only: metaOnly = -1)
static inline size_t bkCloudBuildAuth(const char* devId, const char* token, int metaOnly, char* out, size_t cap) {
  if (cap < 160) return 0;
  int p = snprintf(out, cap, "{\"p_device_id\":\"%.16s\",\"p_token\":\"%.64s\"", devId, token);
  if (metaOnly >= 0) p += snprintf(out + p, cap - p, ",\"p_meta_only\":%s", metaOnly ? "true" : "false");
  p += snprintf(out + p, cap - p, "}");
  return (size_t)p;
}

// ---------------------------------------------------------------- baca balasan
// Cari nilai string JSON `"key":"..."` (tanpa escape; base64 & status memang tidak butuh escape). Mengembalikan
// penunjuk awal & panjang nilai, false = tidak ada / ada backslash / tidak ditutup. Cocok hanya pada kunci tingkat mana pun
// yang namanya persis sama -- balasan server kita datar, jadi cukup.
static inline bool bkCloudJsonStr(const char* j, size_t n, const char* key, const char** val, size_t* vlen) {
  size_t kl = strlen(key);
  if (kl == 0 || kl > 32) return false;
  char pat[40]; pat[0] = '"'; memcpy(pat + 1, key, kl); pat[kl + 1] = '"'; pat[kl + 2] = ':';
  size_t pl = kl + 3;
  for (size_t i = 0; i + pl < n; i++) {
    if (j[i] != '"' || memcmp(j + i, pat, pl) != 0) continue;
    size_t s = i + pl;
    while (s < n && (j[s] == ' ' || j[s] == '\n' || j[s] == '\r' || j[s] == '\t')) s++;
    if (s >= n || j[s] != '"') continue;
    s++;
    size_t e = s;
    while (e < n && j[e] != '"') { if (j[e] == '\\') return false; e++; }
    if (e >= n) return false;
    *val = j + s; *vlen = e - s;
    return true;
  }
  return false;
}
static inline bool bkCloudJsonEq(const char* v, size_t vl, const char* lit) { return strlen(lit) == vl && memcmp(v, lit, vl) == 0; }
// Balasan fungsi teks (put/delete) berbentuk "ok" (JSON string). Mengembalikan isi tanpa tanda kutip.
static inline bool bkCloudPlainStr(const char* j, size_t n, char* out, size_t cap) {
  while (n && (*j == ' ' || *j == '\n' || *j == '\r' || *j == '\t')) { j++; n--; }
  while (n && (j[n - 1] == ' ' || j[n - 1] == '\n' || j[n - 1] == '\r' || j[n - 1] == '\t')) n--;
  if (n < 2 || j[0] != '"' || j[n - 1] != '"' || n - 2 >= cap) return false;
  memcpy(out, j + 1, n - 2); out[n - 2] = 0;
  return true;
}
// Pembaca angka bertanda "size":123 (tanpa kutip). -1 jika tidak ada.
static inline long bkCloudJsonInt(const char* j, size_t n, const char* key) {
  size_t kl = strlen(key); if (kl == 0 || kl > 32) return -1;
  char pat[40]; pat[0] = '"'; memcpy(pat + 1, key, kl); pat[kl + 1] = '"'; pat[kl + 2] = ':'; size_t pl = kl + 3;
  for (size_t i = 0; i + pl < n; i++) {
    if (j[i] != '"' || memcmp(j + i, pat, pl) != 0) continue;
    size_t s = i + pl; long v = 0; int d = 0;
    while (s < n && (j[s] == ' ' || j[s] == '\n' || j[s] == '\r' || j[s] == '\t')) s++;
    while (s < n && j[s] >= '0' && j[s] <= '9' && d < 9) { v = v * 10 + (j[s] - '0'); s++; d++; }
    return d ? v : -1;
  }
  return -1;
}
