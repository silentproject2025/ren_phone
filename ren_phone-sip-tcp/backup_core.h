// backup_core.h -- logika inti Backup & Restore pengaturan NyxOS.
// SENGAJA tanpa dependensi Arduino / ESP-IDF (hanya <stdint.h>, <string.h>, <stdio.h>) supaya bisa
// diuji di komputer: lihat tests_backup/. Penghubung ke NVS, kartu SD, dan layar ada di
// backup_renphone.ino.
//
// FORMAT FILE (.nbk, little-endian):
//   header  14 byte : "NYXB1\0" (6) | flags u8 | versi u8 | epoch u32 | jumlah rekaman u16
//   rekaman (berulang)  : nsLen u8 | ns | keyLen u8 | key | tipe u8 | panjang u32 | data
//   penutup          : 1 byte 0 (nsLen = 0)
//   CRC32 (u32)      : atas semua byte dari akhir header sampai penutup (termasuk penutup)
// flags bit0 = file ini MENGANDUNG sandi / API key.
//
// KEAMANAN SAAT RESTORE: file dari SD dianggap tidak tepercaya -> CRC dicek penuh, semua panjang
// dicek terhadap batas buffer, dan namespace sistem (nvs*, phy*, bt*, otaguard) tidak pernah ditulis.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define BK_HDR        14
#define BK_VERSION    1
#define BK_FLAG_SECRETS 0x01
#define BK_NAME_MAX   15          // batas NVS: namespace & key maksimum 15 karakter

// tipe nilai (kode sendiri, bukan nvs_type_t, supaya core tidak butuh nvs.h)
enum { BK_T_U8 = 1, BK_T_I8, BK_T_U16, BK_T_I16, BK_T_U32, BK_T_I32, BK_T_U64, BK_T_I64, BK_T_STR, BK_T_BLOB };

static inline int bkScalarSize(uint8_t t) {
  switch (t) {
    case BK_T_U8: case BK_T_I8: return 1;
    case BK_T_U16: case BK_T_I16: return 2;
    case BK_T_U32: case BK_T_I32: return 4;
    case BK_T_U64: case BK_T_I64: return 8;
  }
  return 0;   // STR / BLOB / tak dikenal
}

// ---------------------------------------------------------------- CRC32 (zlib), bisa bertahap
static inline uint32_t bkCrc32(uint32_t crc, const uint8_t* d, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *d++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
  }
  return ~crc;
}

// ---------------------------------------------------------------- kebijakan: apa yang di-backup
// Namespace milik sistem / ESP-IDF / pengaman OTA: TIDAK pernah di-backup dan TIDAK pernah ditulis balik.
static inline bool bkNsSkip(const char* ns) {
  if (!strncmp(ns, "nvs", 3) || !strncmp(ns, "phy", 3) || !strncmp(ns, "bt", 2)) return true;
  if (!strcmp(ns, "otaguard")) return true;      // status rollback OTA: bukan pengaturan pengguna
  return false;
}
// Sandi / API key: ikut hanya kalau pengguna memilih "Sertakan sandi & API key".
static inline bool bkIsSecret(const char* ns, const char* key) {
  if (!strcmp(ns, "wifi") || !strcmp(ns, "sip") || !strcmp(ns, "chat") || !strcmp(ns, "gemini")) return true;
  if (!strcmp(ns, "rcfg") && !strncmp(key, "pass", 4)) return true;   // sandi AP Remote Control
  if (!strcmp(ns, "chess") && (!strcmp(key, "bhost") || !strcmp(key, "buser") || !strcmp(key, "bpass"))) return true;   // kredensial broker MQTT Catur
  return false;
}

// ---------------------------------------------------------------- penulis (ke buffer memori)
struct BkWriter {
  uint8_t* buf; size_t cap, len;
  uint32_t count;      // jumlah rekaman
  bool     over;       // buffer kepenuhan
};

static inline void bkBegin(BkWriter& w, uint8_t* buf, size_t cap) {
  w.buf = buf; w.cap = cap; w.len = BK_HDR; w.count = 0; w.over = (cap < BK_HDR + 8);
}
static inline void bkPut(BkWriter& w, const void* p, size_t n) {
  if (w.over) return;
  if (w.len + n > w.cap) { w.over = true; return; }
  memcpy(w.buf + w.len, p, n); w.len += n;
}
static inline void bkPutU32(BkWriter& w, uint32_t v) {
  uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
  bkPut(w, b, 4);
}
// Tambah satu rekaman. false = argumen tidak valid (nama kosong / terlalu panjang / tipe salah).
static inline bool bkAddRecord(BkWriter& w, const char* ns, const char* key, uint8_t type, const void* data, uint32_t len) {
  size_t nl = strlen(ns), kl = strlen(key);
  if (nl == 0 || nl > BK_NAME_MAX || kl == 0 || kl > BK_NAME_MAX) return false;
  if (type < BK_T_U8 || type > BK_T_BLOB) return false;
  int ss = bkScalarSize(type);
  if (ss && (uint32_t)ss != len) return false;
  if (!ss && type == BK_T_STR && len == 0) return false;     // string NVS selalu >= 1 byte (NUL)
  uint8_t b;
  b = (uint8_t)nl; bkPut(w, &b, 1); bkPut(w, ns, nl);
  b = (uint8_t)kl; bkPut(w, &b, 1); bkPut(w, key, kl);
  bkPut(w, &type, 1); bkPutU32(w, len); bkPut(w, data, len);
  if (!w.over) w.count++;
  return !w.over;
}
// Selesai: tulis penutup + CRC, isi header. Mengembalikan panjang total file, atau 0 kalau gagal.
static inline size_t bkFinish(BkWriter& w, uint8_t flags, uint32_t epoch) {
  uint8_t z = 0; bkPut(w, &z, 1);
  if (w.over || w.count > 0xFFFF) return 0;
  uint32_t crc = bkCrc32(0, w.buf + BK_HDR, w.len - BK_HDR);
  bkPutU32(w, crc);
  if (w.over) return 0;
  memcpy(w.buf, "NYXB1\0", 6);
  w.buf[6] = flags; w.buf[7] = BK_VERSION;
  w.buf[8] = (uint8_t)epoch; w.buf[9] = (uint8_t)(epoch >> 8); w.buf[10] = (uint8_t)(epoch >> 16); w.buf[11] = (uint8_t)(epoch >> 24);
  w.buf[12] = (uint8_t)w.count; w.buf[13] = (uint8_t)(w.count >> 8);
  return w.len;
}

// ---------------------------------------------------------------- pembaca
struct BkHdr { uint8_t flags; uint8_t ver; uint32_t epoch; uint16_t count; bool enc; };   // enc = file terenkripsi sandi (magic NYXE1)

// Baca 14 byte header saja (dipakai untuk daftar riwayat). false = bukan file backup NyxOS.
static inline bool bkParseHdr(const uint8_t* b, size_t n, BkHdr& h) {
  if (n < BK_HDR) return false;
  if (memcmp(b, "NYXB1\0", 6) == 0) h.enc = false;
  else if (memcmp(b, "NYXE1\0", 6) == 0) h.enc = true;
  else return false;
  h.flags = b[6]; h.ver = b[7];
  h.epoch = (uint32_t)b[8] | ((uint32_t)b[9] << 8) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
  h.count = (uint16_t)(b[12] | (b[13] << 8));
  return h.ver == BK_VERSION;
}
// Periksa file utuh: header + CRC. true = aman dibaca rekamannya.
static inline bool bkVerify(const uint8_t* b, size_t n, BkHdr& h) {
  if (n < BK_HDR + 1 + 4 || !bkParseHdr(b, n, h)) return false;
  if (h.enc) return false;            // terenkripsi: harus dibuka dulu dgn sandi (bkDecrypt di backup_crypto.h)
  uint32_t want = (uint32_t)b[n - 4] | ((uint32_t)b[n - 3] << 8) | ((uint32_t)b[n - 2] << 16) | ((uint32_t)b[n - 1] << 24);
  return bkCrc32(0, b + BK_HDR, n - BK_HDR - 4) == want;
}

struct BkRec { char ns[BK_NAME_MAX + 1]; char key[BK_NAME_MAX + 1]; uint8_t type; uint32_t len; const uint8_t* data; };

// Rekaman berikutnya. pos = indeks byte (mulai BK_HDR), end = n - 4 (sebelum CRC).
// 1 = ada rekaman, 0 = selesai (penutup), -1 = data rusak.
static inline int bkNext(const uint8_t* b, size_t end, size_t& pos, BkRec& r) {
  if (pos >= end) return -1;
  size_t nl = b[pos++];
  if (nl == 0) return 0;
  if (nl > BK_NAME_MAX || pos + nl + 1 > end) return -1;
  memcpy(r.ns, b + pos, nl); r.ns[nl] = 0; pos += nl;
  size_t kl = b[pos++];
  if (kl == 0 || kl > BK_NAME_MAX || pos + kl + 1 + 4 > end) return -1;
  memcpy(r.key, b + pos, kl); r.key[kl] = 0; pos += kl;
  r.type = b[pos++];
  r.len = (uint32_t)b[pos] | ((uint32_t)b[pos + 1] << 8) | ((uint32_t)b[pos + 2] << 16) | ((uint32_t)b[pos + 3] << 24);
  pos += 4;
  if (r.type < BK_T_U8 || r.type > BK_T_BLOB) return -1;
  int ss = bkScalarSize(r.type);
  if (ss && r.len != (uint32_t)ss) return -1;
  if (r.len > end - pos) return -1;
  if (r.type == BK_T_STR && (r.len == 0 || b[pos + r.len - 1] != 0)) return -1;   // string harus ber-NUL
  r.data = b + pos; pos += r.len;
  return 1;
}

// ---------------------------------------------------------------- nama file di kartu SD
// "nyx_007.nbk" -> 7 ; selain itu -1. (nama boleh berawalan path)
static inline int bkParseName(const char* name) {
  const char* s = strrchr(name, '/'); s = s ? s + 1 : name;
  if (strncmp(s, "nyx_", 4) != 0) return -1;
  int n = 0, d = 0; const char* p = s + 4;
  while (*p >= '0' && *p <= '9' && d < 4) { n = n * 10 + (*p - '0'); p++; d++; }
  if (d == 0 || strcmp(p, ".nbk") != 0) return -1;
  return n;
}
static inline void bkMakeName(char* out, size_t cap, int num) { snprintf(out, cap, "/backup/nyx_%03d.nbk", num); }
