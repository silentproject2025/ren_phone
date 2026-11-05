// kamus_core.h -- logika kamus: saran kata (prefix + typo) dan autokoreksi.
// SENGAJA tanpa dependensi Arduino (hanya <string.h>) supaya bisa diuji di komputer:
// lihat tests_kamus/. Penghubung ke keyboard/layar/SD ada di kamus_renphone.ino.
//
// Konsep:
//   - Kamus = beberapa KmsList. Urutan dalam list = peringkat frekuensi (indeks kecil = sering).
//   - Semua kata di kamus huruf kecil a-z. Kata yang diketik dibandingkan tanpa peduli kapital.
//   - Saran: kata persis (bila ada) -> pelengkapan awalan -> tebakan typo (jarak edit 1).
//   - Autokoreksi: HANYA bila kata tidak ada di kamus mana pun, panjang >= 4, huruf pertama
//     sama, dan ada kata kamus berjarak 1 edit yang MASUK AKAL sebagai salah ketik (huruf ganda,
//     tuts bersebelahan, huruf terlewat, dua huruf tertukar) -- lihat kmsFixOk().
#pragma once
#include <string.h>

#define KMS_MAXW 24      // panjang kata maksimum yang diproses
#define KMS_SUG   3      // jumlah saran yang ditampilkan

struct KmsList { const char* const* w; int n; };

struct KmsCtx {
  KmsList user;          // kata yang dipelajari pengguna (prioritas tertinggi)
  KmsList sd;            // kamus tambahan dari kartu SD (opsional)
  KmsList id;            // bawaan: Indonesia
  KmsList en;            // bawaan: Inggris
  int     preferEn;      // 0 = bahasa Indonesia didahulukan, 1 = Inggris didahulukan
};

static inline char kmsLow(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline bool kmsIsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline bool kmsIsUpper(char c) { return c >= 'A' && c <= 'Z'; }

// Panjang rangkaian huruf di UJUNG teks (kata yang sedang diketik). Dibatasi 64.
static inline int kmsTailWord(const char* t, int n) {
  int k = 0;
  while (k < n && k < 64 && kmsIsLetter(t[n - 1 - k])) k++;
  return k;
}

// ---------------------------------------------------------------- pencocokan dasar
// w = kata kamus (huruf kecil, NUL-terminated), p = ketikan (bebas kapital)
static inline bool kmsHasPrefix(const char* w, const char* p, int pl) {
  for (int i = 0; i < pl; i++) { if (!w[i] || w[i] != kmsLow(p[i])) return false; }
  return true;
}
static inline bool kmsEqual(const char* w, const char* p, int pl) {
  return kmsHasPrefix(w, p, pl) && w[pl] == 0;
}
static inline bool kmsInList(const KmsList& l, const char* p, int pl) {
  for (int i = 0; i < l.n; i++) if (kmsEqual(l.w[i], p, pl)) return true;
  return false;
}
static inline bool kmsKnown(const KmsCtx& c, const char* p, int pl) {
  return kmsInList(c.user, p, pl) || kmsInList(c.sd, p, pl) || kmsInList(c.id, p, pl) || kmsInList(c.en, p, pl);
}

// a = ketikan (huruf kecil, NUL-terminated, panjang la); b = kata kamus (panjang lb).
// true bila tepat 1 edit: ganti 1 huruf, sisip 1, hapus 1, atau tukar 2 huruf bersebelahan.
static inline bool kmsDist1(const char* a, int la, const char* b, int lb) {
  if (la == lb) {
    int i = 0; while (i < la && a[i] == b[i]) i++;
    if (i == la) return false;                                   // identik (jarak 0)
    if (strncmp(a + i + 1, b + i + 1, (size_t)(la - i - 1)) == 0) return true;            // ganti
    if (i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] &&
        strncmp(a + i + 2, b + i + 2, (size_t)(la - i - 2)) == 0) return true;            // tukar
    return false;
  }
  if (la == lb + 1) { int i = 0; while (i < lb && a[i] == b[i]) i++; return strncmp(a + i + 1, b + i, (size_t)(lb - i)) == 0; }   // ketikan kelebihan 1
  if (lb == la + 1) { int i = 0; while (i < la && a[i] == b[i]) i++; return strncmp(a + i, b + i + 1, (size_t)(la - i)) == 0; }   // ketikan kurang 1
  return false;
}


// ---------------------------------------------------------------- kedekatan tuts (QWERTY)
// Posisi fisik tuts: baris 0 "qwertyuiop", baris 1 "asdfghjkl" (geser 0,25), baris 2 "zxcvbnm" (geser 0,75).
// Dipakai agar autokoreksi hanya memperbaiki salah ketik yang MASUK AKAL (jari meleset ke tuts sebelah),
// bukan mengganti kata asli yang kebetulan tidak ada di kamus (mis. "buku" -> "buka").
static inline bool kmsKeyPos(char c, int& row, int& x4) {      // x4 = posisi x dalam seperempat tuts
  static const char* const rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
  static const int off4[3] = { 0, 1, 3 };
  for (int r = 0; r < 3; r++) {
    const char* p = rows[r];
    for (int i = 0; p[i]; i++) if (p[i] == c) { row = r; x4 = i * 4 + off4[r]; return true; }
  }
  return false;
}
static inline bool kmsAdjacent(char a, char b) {
  int ra, xa, rb, xb;
  if (!kmsKeyPos(a, ra, xa) || !kmsKeyPos(b, rb, xb)) return false;
  int dx = xa > xb ? xa - xb : xb - xa, dr = ra > rb ? ra - rb : rb - ra;
  if (dr == 0) return dx == 4;
  if (dr == 1) return dx <= 3;
  return false;
}

// Seperti kmsDist1, tapi hanya menerima edit yang lazim sebagai salah ketik:
//   ganti huruf   -> kedua huruf bertetangga di keyboard
//   huruf lebih   -> huruf ganda, atau bertetangga dengan huruf di sebelahnya
//   huruf kurang  -> diterima (jari melewatkan satu tuts)
//   tukar 2 huruf -> diterima
static inline bool kmsFixOk(const char* a, int la, const char* b, int lb) {
  if (la == lb) {
    int i = 0; while (i < la && a[i] == b[i]) i++;
    if (i == la) return false;
    if (strncmp(a + i + 1, b + i + 1, (size_t)(la - i - 1)) == 0) return kmsAdjacent(a[i], b[i]);
    if (i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] &&
        strncmp(a + i + 2, b + i + 2, (size_t)(la - i - 2)) == 0) return true;
    return false;
  }
  if (la == lb + 1) {
    int i = 0; while (i < lb && a[i] == b[i]) i++;
    if (strncmp(a + i + 1, b + i, (size_t)(lb - i)) != 0) return false;
    if (i > 0 && (a[i] == a[i - 1] || kmsAdjacent(a[i], a[i - 1]))) return true;
    if (i + 1 < la && (a[i] == a[i + 1] || kmsAdjacent(a[i], a[i + 1]))) return true;
    return false;
  }
  if (lb == la + 1) { int i = 0; while (i < la && a[i] == b[i]) i++; return strncmp(a + i, b + i + 1, (size_t)(la - i)) == 0; }
  return false;
}

// ---------------------------------------------------------------- kapital
// Samakan kapital kata saran dengan ketikan: "Teh"->"The", "TEH"->"THE", "teh"->"the".
static inline void kmsApplyCase(const char* typed, int tl, const char* cand, char* out) {
  int n = (int)strlen(cand); if (n > KMS_MAXW) n = KMS_MAXW;
  bool allUp = tl >= 2;
  for (int i = 0; i < tl; i++) if (!kmsIsUpper(typed[i])) { allUp = false; break; }
  for (int i = 0; i < n; i++) out[i] = cand[i];
  out[n] = 0;
  if (allUp) { for (int i = 0; i < n; i++) if (out[i] >= 'a' && out[i] <= 'z') out[i] = (char)(out[i] - 32); }
  else if (tl >= 1 && kmsIsUpper(typed[0])) { if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32); }
}

// ---------------------------------------------------------------- iterasi berperingkat
// Daftar sumber menurut prioritas pada peringkat r: user (semua didahulukan), lalu sd/pref/other bergantian.
// Memanggil fn(kata) untuk tiap kata kamus dalam urutan prioritas; fn mengembalikan false = berhenti.
template <class F>
static inline void kmsEach(const KmsCtx& c, F fn) {
  for (int i = 0; i < c.user.n; i++) if (!fn(c.user.w[i])) return;
  const KmsList* first  = c.preferEn ? &c.en : &c.id;
  const KmsList* second = c.preferEn ? &c.id : &c.en;
  int mx = c.sd.n; if (first->n > mx) mx = first->n; if (second->n > mx) mx = second->n;
  for (int r = 0; r < mx; r++) {
    if (r < c.sd.n    && !fn(c.sd.w[r]))     return;
    if (r < first->n  && !fn(first->w[r]))   return;
    if (r < second->n && !fn(second->w[r]))  return;
  }
}

// ---------------------------------------------------------------- saran
struct KmsSugs { char w[KMS_SUG][KMS_MAXW + 1]; int n; };

static inline bool kmsSugHas(const KmsSugs& s, const char* w) {
  for (int i = 0; i < s.n; i++) if (strcmp(s.w[i], w) == 0) return true;
  return false;
}
static inline void kmsSugAdd(KmsSugs& s, const char* w) {
  if (s.n >= KMS_SUG || kmsSugHas(s, w)) return;
  int k = (int)strlen(w); if (k > KMS_MAXW) return;
  memcpy(s.w[s.n], w, (size_t)k); s.w[s.n][k] = 0; s.n++;
}

// word = ketikan (huruf saja, panjang wl 1..KMS_MAXW). Hasil sudah disesuaikan kapitalnya.
static inline void kmsSuggest(const KmsCtx& c, const char* word, int wl, KmsSugs& out) {
  out.n = 0;
  if (wl < 1 || wl > KMS_MAXW) return;
  char lw[KMS_MAXW + 1]; for (int i = 0; i < wl; i++) lw[i] = kmsLow(word[i]); lw[wl] = 0;

  KmsSugs raw; raw.n = 0;
  if (kmsKnown(c, lw, wl)) kmsSugAdd(raw, lw);                        // kata sudah benar -> tampil pertama
  kmsEach(c, [&](const char* w) {                                     // pelengkapan awalan
    if (raw.n >= KMS_SUG) return false;
    if (kmsHasPrefix(w, lw, wl)) kmsSugAdd(raw, w);
    return true;
  });
  if (raw.n < KMS_SUG && wl >= 3) {                                   // tebakan typo (jarak 1)
    kmsEach(c, [&](const char* w) {
      if (raw.n >= KMS_SUG) return false;
      int lb = (int)strlen(w);
      if (lb >= 3 && lw[0] == w[0] && kmsDist1(lw, wl, w, lb)) kmsSugAdd(raw, w);
      return true;
    });
  }
  for (int i = 0; i < raw.n; i++) {
    char tmp[KMS_MAXW + 1]; kmsApplyCase(word, wl, raw.w[i], tmp);
    kmsSugAdd(out, tmp);
  }
}

// ---------------------------------------------------------------- autokoreksi
// t/n = seluruh teks sebelum pemisah (spasi/koma/titik...) diketik. true + fixed = kata pengganti
// (kapital sudah disesuaikan) bila kata terakhir layak dikoreksi otomatis.
static inline bool kmsAutoFix(const KmsCtx& c, const char* t, int n, char* fixed) {
  int wl = kmsTailWord(t, n);
  if (wl < 4 || wl > KMS_MAXW) return false;
  const char* w = t + n - wl;

  // kapital: "Budi" di tengah kalimat = nama, "NASA"/"iPhone" = disengaja -> jangan disentuh
  for (int i = 1; i < wl; i++) if (kmsIsUpper(w[i])) return false;
  int before = n - wl;
  if (kmsIsUpper(w[0])) {
    int j = before - 1; while (j >= 0 && (t[j] == ' ' || t[j] == '\t')) j--;
    bool sentenceStart = (j < 0) || t[j] == '.' || t[j] == '!' || t[j] == '?' || t[j] == '\n';
    if (!sentenceStart) return false;
  }
  // konteks bukan kata biasa: contraction (don't), hashtag, email, URL, kode ber-angka
  if (before > 0) {
    char p = t[before - 1];
    if (p == '\'' || p == '@' || p == '#' || p == '/' || p == '_' || p == '-' || p == '\\' || (p >= '0' && p <= '9')) return false;
  }
  for (int j = before - 1; j >= 0 && t[j] != ' ' && t[j] != '\n' && t[j] != '\t'; j--) {
    char q = t[j]; if (q == '@' || q == '/' || (q >= '0' && q <= '9')) return false;
  }

  char lw[KMS_MAXW + 1]; for (int i = 0; i < wl; i++) lw[i] = kmsLow(w[i]); lw[wl] = 0;
  if (kmsKnown(c, lw, wl)) return false;

  // sengaja bergaya: tawa berulang (hehe, wkwk, hihihi) dan huruf dipanjangkan (yaaa, bangeeet)
  bool rep2 = true;  for (int i = 2; i < wl; i++) if (lw[i] != lw[i - 2]) { rep2 = false; break; }
  if (rep2) return false;
  for (int i = 2; i < wl; i++) if (lw[i] == lw[i - 1] && lw[i] == lw[i - 2]) return false;

  const char* best = nullptr;
  kmsEach(c, [&](const char* cand) {
    int lb = (int)strlen(cand);
    if (lb >= 4 && lw[0] == cand[0] && kmsFixOk(lw, wl, cand, lb)) { best = cand; return false; }   // yang pertama = peringkat terbaik
    return true;
  });
  if (!best) return false;
  kmsApplyCase(w, wl, best, fixed);
  return true;
}
