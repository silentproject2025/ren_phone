// =====================================================================
// backup_crypto.h -- kripto murni C++ (tanpa Arduino/mbedtls) utk backup terenkripsi sandi.
// Diuji di PC terhadap Python hashlib/hmac/pbkdf2 dan `cryptography` (tests_backup/crypto_check.py).
//
//   SHA-256 -> HMAC-SHA256 -> PBKDF2-HMAC-SHA256 (turunan kunci dari sandi)
//   AES-256 (hanya arah enkripsi) mode CTR (dekripsi = enkripsi yg sama)
//
// FORMAT FILE TERENKRIPSI  (magic "NYXE1\0", header 14 byte SAMA dgn backup biasa -> daftar riwayat tetap bisa
// menampilkan waktu & jumlah nilai tanpa sandi; semuanya ikut diautentikasi oleh HMAC):
//   0..13   header (magic NYXE1\0 | flags | ver | epoch u32 | count u16)
//   14..17  iterasi PBKDF2 (u32 LE)
//   18..33  salt (16)
//   34..49  nonce/IV awal CTR (16)
//   50..    ciphertext = isi backup biasa SETELAH 14 byte header (rekaman + penutup + CRC32)
//   akhir   HMAC-SHA256 (32) atas SEMUA byte sebelumnya  (encrypt-then-MAC)
// Kunci: PBKDF2(sandi, salt, iterasi, 64 byte) -> 32 byte pertama = kunci AES, 32 berikutnya = kunci HMAC.
// Sandi salah ATAU file diubah -> HMAC tidak cocok -> ditolak sebelum ada yang didekripsi/diterapkan.
// =====================================================================
#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#ifndef BK_YIELD
#define BK_YIELD() do {} while (0)         // .ino mengisi dgn yield() supaya watchdog tidak lapar saat PBKDF2
#endif

// ---------------------------------------------------------------- SHA-256
struct BkSha { uint32_t h[8]; uint8_t buf[64]; uint64_t total; size_t fill; };
static const uint32_t bkShaK[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
  0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
  0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
  0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define BK_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static inline void bkShaBlock(uint32_t* st, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = BK_ROR(w[i-15], 7) ^ BK_ROR(w[i-15], 18) ^ (w[i-15] >> 3);
    uint32_t s1 = BK_ROR(w[i-2], 17) ^ BK_ROR(w[i-2], 19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (BK_ROR(e, 6) ^ BK_ROR(e, 11) ^ BK_ROR(e, 25)) + ((e & f) ^ (~e & g)) + bkShaK[i] + w[i];
    uint32_t t2 = (BK_ROR(a, 2) ^ BK_ROR(a, 13) ^ BK_ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}
static inline void bkShaInit(BkSha& s) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(s.h, iv, 32); s.total = 0; s.fill = 0;
}
static inline void bkShaUpdate(BkSha& s, const uint8_t* d, size_t n) {
  s.total += n;
  while (n) {
    size_t k = 64 - s.fill; if (k > n) k = n;
    memcpy(s.buf + s.fill, d, k); s.fill += k; d += k; n -= k;
    if (s.fill == 64) { bkShaBlock(s.h, s.buf); s.fill = 0; }
  }
}
static inline void bkShaFinal(BkSha& s, uint8_t* out) {
  uint64_t bits = s.total * 8; uint8_t pad = 0x80;
  bkShaUpdate(s, &pad, 1);
  uint8_t z = 0; while (s.fill != 56) bkShaUpdate(s, &z, 1);
  uint8_t l[8]; for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
  bkShaUpdate(s, l, 8);
  for (int i = 0; i < 8; i++) { out[4*i] = (uint8_t)(s.h[i] >> 24); out[4*i+1] = (uint8_t)(s.h[i] >> 16); out[4*i+2] = (uint8_t)(s.h[i] >> 8); out[4*i+3] = (uint8_t)s.h[i]; }
}
static inline void bkSha256(const uint8_t* d, size_t n, uint8_t* out) { BkSha s; bkShaInit(s); bkShaUpdate(s, d, n); bkShaFinal(s, out); }

// ---------------------------------------------------------------- HMAC-SHA256 (dengan status pad terhitung-di-muka)
struct BkHmac { BkSha in, out; };
static inline void bkHmacInit(BkHmac& m, const uint8_t* key, size_t kl) {
  uint8_t k[64]; memset(k, 0, 64);
  if (kl > 64) bkSha256(key, kl, k); else memcpy(k, key, kl);
  uint8_t ip[64], op[64];
  for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
  bkShaInit(m.in); bkShaUpdate(m.in, ip, 64);
  bkShaInit(m.out); bkShaUpdate(m.out, op, 64);
  memset(k, 0, 64); memset(ip, 0, 64); memset(op, 0, 64);
}
static inline void bkHmacFinal(const BkHmac& base, BkSha inner, uint8_t* out) {   // inner = salinan status dalam yg sudah berisi pesan
  uint8_t ih[32]; bkShaFinal(inner, ih);
  BkSha o = base.out; bkShaUpdate(o, ih, 32); bkShaFinal(o, out);
}
static inline void bkHmac(const uint8_t* key, size_t kl, const uint8_t* d, size_t n, uint8_t* out) {
  BkHmac m; bkHmacInit(m, key, kl);
  BkSha in = m.in; bkShaUpdate(in, d, n); bkHmacFinal(m, in, out);
}

// ---------------------------------------------------------------- PBKDF2-HMAC-SHA256
static inline void bkPbkdf2(const uint8_t* pw, size_t pl, const uint8_t* salt, size_t sl, uint32_t iter, uint8_t* out, size_t outLen) {
  BkHmac base; bkHmacInit(base, pw, pl);
  uint32_t blk = 1;
  for (size_t done = 0; done < outLen; blk++) {
    uint8_t be[4] = {(uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk};
    uint8_t u[32], t[32];
    BkSha in = base.in; bkShaUpdate(in, salt, sl); bkShaUpdate(in, be, 4); bkHmacFinal(base, in, u);
    memcpy(t, u, 32);
    for (uint32_t i = 1; i < iter; i++) {
      BkSha in2 = base.in; bkShaUpdate(in2, u, 32); bkHmacFinal(base, in2, u);
      for (int j = 0; j < 32; j++) t[j] ^= u[j];
      if ((i & 1023u) == 0) BK_YIELD();
    }
    size_t k = outLen - done; if (k > 32) k = 32;
    memcpy(out + done, t, k); done += k;
    memset(u, 0, 32); memset(t, 0, 32);
  }
}

// ---------------------------------------------------------------- AES-256 (enkripsi blok) + CTR
struct BkAes { uint8_t rk[240]; };
static inline uint8_t bkXt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
static inline const uint8_t* bkSbox() {                   // dibangkitkan sekali (inversi GF(2^8) + afin), tanpa tabel 256 byte yg rawan salah ketik
  static uint8_t sb[256]; static bool ok = false;
  if (!ok) {
    uint8_t p = 1, q = 1;
    do {
      p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
      q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4); if (q & 0x80) q ^= 0x09;
      uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
      sb[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    sb[0] = 0x63; ok = true;
  }
  return sb;
}
static inline void bkAesInit(BkAes& a, const uint8_t* key32) {
  const uint8_t* sb = bkSbox();
  memcpy(a.rk, key32, 32);
  uint8_t rc = 1;
  for (int i = 32; i < 240; i += 4) {
    uint8_t t[4]; memcpy(t, a.rk + i - 4, 4);
    if (i % 32 == 0) { uint8_t x = t[0]; t[0] = (uint8_t)(sb[t[1]] ^ rc); t[1] = sb[t[2]]; t[2] = sb[t[3]]; t[3] = sb[x]; rc = bkXt(rc); }
    else if (i % 32 == 16) { for (int j = 0; j < 4; j++) t[j] = sb[t[j]]; }
    for (int j = 0; j < 4; j++) a.rk[i + j] = (uint8_t)(a.rk[i - 32 + j] ^ t[j]);
  }
}
static inline void bkAesBlock(const BkAes& a, const uint8_t* in, uint8_t* out) {
  const uint8_t* sb = bkSbox();
  uint8_t s[16]; for (int i = 0; i < 16; i++) s[i] = in[i] ^ a.rk[i];
  for (int r = 1; r <= 14; r++) {
    uint8_t t[16];
    for (int i = 0; i < 16; i++) t[i] = sb[s[i]];
    uint8_t u[16];                                                    // ShiftRows (kolom-utama)
    u[0]=t[0];  u[1]=t[5];  u[2]=t[10]; u[3]=t[15]; u[4]=t[4];  u[5]=t[9];  u[6]=t[14]; u[7]=t[3];
    u[8]=t[8];  u[9]=t[13]; u[10]=t[2]; u[11]=t[7]; u[12]=t[12]; u[13]=t[1]; u[14]=t[6]; u[15]=t[11];
    if (r < 14) {
      for (int c = 0; c < 4; c++) {
        uint8_t* p = u + 4 * c; uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], x = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        p[0] = (uint8_t)(a0 ^ x ^ bkXt((uint8_t)(a0 ^ a1)));
        p[1] = (uint8_t)(a1 ^ x ^ bkXt((uint8_t)(a1 ^ a2)));
        p[2] = (uint8_t)(a2 ^ x ^ bkXt((uint8_t)(a2 ^ a3)));
        p[3] = (uint8_t)(a3 ^ x ^ bkXt((uint8_t)(a3 ^ a0)));
      }
    }
    for (int i = 0; i < 16; i++) s[i] = u[i] ^ a.rk[16 * r + i];
  }
  memcpy(out, s, 16);
}
// CTR: counter = 16 byte big-endian, ditambah 1 tiap blok. in/out boleh sama.
static inline void bkAesCtr(const uint8_t* key32, const uint8_t* iv16, const uint8_t* in, uint8_t* out, size_t n) {
  BkAes a; bkAesInit(a, key32);
  uint8_t ctr[16], ks[16]; memcpy(ctr, iv16, 16);
  for (size_t i = 0; i < n; i += 16) {
    bkAesBlock(a, ctr, ks);
    size_t k = n - i; if (k > 16) k = 16;
    for (size_t j = 0; j < k; j++) out[i + j] = in[i + j] ^ ks[j];
    for (int c = 15; c >= 0; c--) if (++ctr[c]) break;
  }
  memset(&a, 0, sizeof(a)); memset(ks, 0, 16);
}
static inline bool bkCtEq(const uint8_t* a, const uint8_t* b, size_t n) {      // banding waktu-tetap
  uint8_t d = 0; for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]); return d == 0;
}

// ---------------------------------------------------------------- kontainer file
#define BK_ENC_ITER_DEF   10000u
#define BK_ENC_ITER_MAX   200000u          // batas wajar: file berisi angka gila tidak boleh mengunci HP berjam-jam
#define BK_ENC_OFF_ITER   14
#define BK_ENC_OFF_SALT   18
#define BK_ENC_OFF_IV     34
#define BK_ENC_OFF_DATA   50
#define BK_ENC_OVERHEAD   (BK_ENC_OFF_DATA - 14 + 32)      // byte tambahan dibanding file biasa (selain header 14)
#define BK_PASS_MIN       6

// Enkripsi file backup biasa (plain "NYXB1", panjang n) -> out. Mengembalikan panjang, 0 = gagal (buffer kecil / bukan backup).
// salt16 & iv16 WAJIB acak (esp_random) dari pemanggil -> inti ini tetap deterministik utk diuji.
static inline size_t bkEncrypt(const uint8_t* plain, size_t n, const uint8_t* pw, size_t pl,
                               const uint8_t* salt16, const uint8_t* iv16, uint32_t iter, uint8_t* out, size_t cap) {
  if (n < 14 + 5 || memcmp(plain, "NYXB1\0", 6) != 0 || iter == 0 || iter > BK_ENC_ITER_MAX) return 0;
  size_t total = BK_ENC_OFF_DATA + (n - 14) + 32;
  if (cap < total) return 0;
  uint8_t keys[64];
  bkPbkdf2(pw, pl, salt16, 16, iter, keys, 64);
  memcpy(out, plain, 14); memcpy(out, "NYXE1\0", 6);
  out[BK_ENC_OFF_ITER] = (uint8_t)iter; out[BK_ENC_OFF_ITER+1] = (uint8_t)(iter >> 8); out[BK_ENC_OFF_ITER+2] = (uint8_t)(iter >> 16); out[BK_ENC_OFF_ITER+3] = (uint8_t)(iter >> 24);
  memcpy(out + BK_ENC_OFF_SALT, salt16, 16); memcpy(out + BK_ENC_OFF_IV, iv16, 16);
  bkAesCtr(keys, iv16, plain + 14, out + BK_ENC_OFF_DATA, n - 14);
  bkHmac(keys + 32, 32, out, total - 32, out + total - 32);
  memset(keys, 0, 64);
  return total;
}
// status bkDecrypt
#define BK_DEC_OK      0
#define BK_DEC_FORMAT  1      // bukan file terenkripsi NyxOS / terpotong / parameter tak wajar
#define BK_DEC_PASS    2      // sandi salah ATAU file diubah (HMAC tidak cocok)
#define BK_DEC_SPACE   3      // buffer keluaran kekecilan
// Buka -> out = file backup BIASA ("NYXB1", siap bkVerify/bkApply). *outLen diisi panjangnya.
static inline int bkDecrypt(const uint8_t* b, size_t n, const uint8_t* pw, size_t pl, uint8_t* out, size_t cap, size_t* outLen) {
  if (n < BK_ENC_OFF_DATA + 5 + 32 || memcmp(b, "NYXE1\0", 6) != 0 || b[7] != 1) return BK_DEC_FORMAT;
  uint32_t iter = (uint32_t)b[BK_ENC_OFF_ITER] | ((uint32_t)b[BK_ENC_OFF_ITER+1] << 8) | ((uint32_t)b[BK_ENC_OFF_ITER+2] << 16) | ((uint32_t)b[BK_ENC_OFF_ITER+3] << 24);
  if (iter == 0 || iter > BK_ENC_ITER_MAX) return BK_DEC_FORMAT;
  size_t dl = n - BK_ENC_OFF_DATA - 32, plainLen = 14 + dl;
  if (cap < plainLen) return BK_DEC_SPACE;
  uint8_t keys[64], mac[32];
  bkPbkdf2(pw, pl, b + BK_ENC_OFF_SALT, 16, iter, keys, 64);
  bkHmac(keys + 32, 32, b, n - 32, mac);
  if (!bkCtEq(mac, b + n - 32, 32)) { memset(keys, 0, 64); return BK_DEC_PASS; }
  memcpy(out, b, 14); memcpy(out, "NYXB1\0", 6);
  bkAesCtr(keys, b + BK_ENC_OFF_IV, b + BK_ENC_OFF_DATA, out + 14, dl);
  memset(keys, 0, 64);
  *outLen = plainLen;
  return BK_DEC_OK;
}
