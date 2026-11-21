// =====================================================================
// sec_core.h -- inti keamanan murni C++ (tanpa Arduino/mbedtls), diuji di PC (tests/security).
//
//   1. Sandi login Web File Manager: PBKDF2-HMAC-SHA256 + salt -> yang disimpan hanya hash.
//   2. API key terenkripsi ("enc1:<hex>"): AES-256-CTR + HMAC-SHA256 (dipotong 16 byte),
//      kunci diturunkan dari rahasia khusus perangkat (NVS) + MAC chip.
//   3. Kebijakan sandi / nama pengguna, pemisah "user:pass" dari Basic Auth.
//   4. Lama penguncian login bertingkat (60 dtk -> 5 mnt -> 15 mnt -> 1 jam).
//   5. info.txt OTA: pisahkan badan yang ditandatangani dari baris sig=.
//
// Bergantung hanya pada backup_crypto.h (SHA-256, HMAC, PBKDF2, AES-CTR, banding waktu-tetap).
// =====================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "backup_crypto.h"

// ---------------------------------------------------------------- hex
static inline void secHex(const uint8_t* d, size_t n, char* out) {          // out >= 2n+1
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2*i] = H[d[i] >> 4]; out[2*i+1] = H[d[i] & 15]; }
  out[2*n] = 0;
}
static inline int secHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
// -1 = bukan hex / ganjil / buffer kekecilan; selain itu jumlah byte
static inline int secUnhex(const char* s, size_t sl, uint8_t* out, size_t cap) {
  if (sl == 0 || (sl & 1)) return -1;
  size_t n = sl / 2;
  if (n > cap) return -1;
  for (size_t i = 0; i < n; i++) {
    int a = secHexVal(s[2*i]), b = secHexVal(s[2*i+1]);
    if (a < 0 || b < 0) return -1;
    out[i] = (uint8_t)((a << 4) | b);
  }
  return (int)n;
}

// ---------------------------------------------------------------- sandi login web
#define SEC_PW_ITER   2000u     // ~0,1 dtk di ESP32-S3; hasil verifikasi sukses di-cache di RAM oleh pemanggil
#define SEC_PW_MIN    8
#define SEC_PW_MAX    32
#define SEC_USER_MIN  3
#define SEC_USER_MAX  16

static inline void secPwHash(const char* pw, size_t pl, const uint8_t* salt16, uint8_t* out32) {
  bkPbkdf2((const uint8_t*)pw, pl, salt16, 16, SEC_PW_ITER, out32, 32);
}
static inline bool secPwCheck(const char* pw, size_t pl, const uint8_t* salt16, const uint8_t* hash32) {
  uint8_t h[32]; secPwHash(pw, pl, salt16, h);
  bool ok = bkCtEq(h, hash32, 32);
  memset(h, 0, sizeof(h));
  return ok;
}
// nullptr = lolos; selain itu teks alasan (Indonesia, sama dgn yg sudah ada di layar)
static inline const char* secPwPolicy(const char* pw, size_t n) {
  if (n < SEC_PW_MIN) return "Min. 8 karakter";
  if (n > SEC_PW_MAX) return "Maks. 32 karakter";
  for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)pw[i]; if (c < 33 || c > 126) return "Tanpa spasi / emoji"; }
  return nullptr;
}
static inline const char* secUserPolicy(const char* u, size_t n) {
  if (n < SEC_USER_MIN || n > SEC_USER_MAX) return "Nama: 3-16 karakter";
  for (size_t i = 0; i < n; i++) {
    char c = u[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (!ok) return "Nama: huruf/angka/_-. saja";
  }
  return nullptr;
}
// "user:pass" (hasil decode base64 Basic Auth) -> pecah di ':' PERTAMA (sandi boleh berisi ':')
static inline bool secSplitCred(const char* dec, size_t n, const char** user, size_t* ul, const char** pass, size_t* pl) {
  for (size_t i = 0; i < n; i++) if (dec[i] == ':') { *user = dec; *ul = i; *pass = dec + i + 1; *pl = n - i - 1; return true; }
  return false;
}
// Penguncian bertingkat: level = jumlah penguncian sebelumnya sejak login sukses terakhir (0 = yang pertama)
static inline uint32_t secLockSecs(uint8_t level) {
  static const uint32_t T[4] = { 60u, 300u, 900u, 3600u };
  return T[level > 3 ? 3 : level];
}

// ---------------------------------------------------------------- API key terenkripsi
#define SEC_KEY_PREFIX     "enc1:"
#define SEC_KEY_PREFIX_LEN 5
#define SEC_KEY_MAX        200                       // panjang key (plain) maksimum
#define SEC_KEY_TXT_CAP    (SEC_KEY_PREFIX_LEN + 2 * (16 + SEC_KEY_MAX + 16) + 1)

// ikm = rahasia perangkat (32 B) + MAC chip (6 B) -> kunci enkripsi & kunci MAC terpisah
static inline void secKeyDerive(const uint8_t* ikm, size_t il, uint8_t* encKey32, uint8_t* macKey32) {
  bkHmac(ikm, il, (const uint8_t*)"nyx-keyenc-v1", 13, encKey32);
  bkHmac(ikm, il, (const uint8_t*)"nyx-keymac-v1", 13, macKey32);
}
// Segel plain -> "enc1:<hex(iv16 | ciphertext | mac16)>". Mengembalikan panjang teks, 0 = gagal.
static inline size_t secKeySeal(const uint8_t* ek, const uint8_t* mk, const uint8_t* iv16,
                                const char* plain, size_t n, char* out, size_t cap) {
  if (n == 0 || n > SEC_KEY_MAX) return 0;
  size_t total = 16 + n + 16;
  if (cap < SEC_KEY_PREFIX_LEN + 2 * total + 1) return 0;
  uint8_t buf[16 + SEC_KEY_MAX + 16], mac[32];
  memcpy(buf, iv16, 16);
  bkAesCtr(ek, iv16, (const uint8_t*)plain, buf + 16, n);
  bkHmac(mk, 32, buf, 16 + n, mac);
  memcpy(buf + 16 + n, mac, 16);
  memcpy(out, SEC_KEY_PREFIX, SEC_KEY_PREFIX_LEN);
  secHex(buf, total, out + SEC_KEY_PREFIX_LEN);
  return SEC_KEY_PREFIX_LEN + 2 * total;
}
// 0 = baris BUKAN terenkripsi (tanpa awalan enc1:) -> pemanggil memperlakukannya sbg teks biasa & memigrasikan
// 1 = berhasil, out = key (diakhiri NUL)
// -1 = terenkripsi tapi rusak / bukan milik perangkat ini (MAC salah)
static inline int secKeyOpen(const uint8_t* ek, const uint8_t* mk, const char* line, size_t ln, char* out, size_t cap) {
  if (ln < SEC_KEY_PREFIX_LEN || memcmp(line, SEC_KEY_PREFIX, SEC_KEY_PREFIX_LEN) != 0) return 0;
  uint8_t buf[16 + SEC_KEY_MAX + 16];
  int bl = secUnhex(line + SEC_KEY_PREFIX_LEN, ln - SEC_KEY_PREFIX_LEN, buf, sizeof(buf));
  if (bl < 16 + 1 + 16) return -1;
  size_t n = (size_t)bl - 32;
  if (n >= cap) return -1;
  uint8_t mac[32];
  bkHmac(mk, 32, buf, 16 + n, mac);
  if (!bkCtEq(mac, buf + 16 + n, 16)) return -1;
  bkAesCtr(ek, buf, buf + 16, (uint8_t*)out, n);       // iv = 16 byte pertama
  out[n] = 0;
  return 1;
}

// ---------------------------------------------------------------- info.txt OTA bertanda tangan
// Format: baris "kunci=nilai" ... lalu SATU baris terakhir "sig=<hex DER ECDSA>".
// Yang ditandatangani = semua byte SEBELUM baris sig= (termasuk \n penutup baris terakhir).
// Menolak: tidak ada sig=, sig= lebih dari sekali, atau ada isi (selain spasi) SESUDAH sig=.
static inline bool secInfoSplit(const char* body, size_t n, size_t* signedLen, const char** sigHex, size_t* sigLen) {
  size_t pos = 0; bool found = false; size_t sigStart = 0, sigEnd = 0;
  while (pos < n) {
    size_t e = pos; while (e < n && body[e] != '\n') e++;
    if (e - pos >= 4 && memcmp(body + pos, "sig=", 4) == 0) {
      if (found) return false;                          // dua baris sig=
      found = true; sigStart = pos; sigEnd = e;
    } else if (found) {
      for (size_t i = pos; i < e; i++) if (body[i] != ' ' && body[i] != '\r' && body[i] != '\t') return false;   // isi setelah sig=
    }
    pos = e + 1;
  }
  if (!found) return false;
  size_t s = sigStart + 4, t = sigEnd;
  while (t > s && (body[t-1] == '\r' || body[t-1] == ' ' || body[t-1] == '\t')) t--;
  if (t <= s) return false;
  *signedLen = sigStart; *sigHex = body + s; *sigLen = t - s;
  return true;
}
