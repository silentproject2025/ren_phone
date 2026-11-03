// =====================================================================
//  emoji_renphone.ino  --  dukungan EMOJI untuk semua teks NyxOS
//
//  MASALAH: font bawaan LovyanGFX (Font0, FreeSans, ...) hanya ASCII, dan jalur teks LovyanGFX
//  menerima kode karakter 16-bit (uint16_t), jadi emoji "sungguhan" (U+1F600 dst, 4 byte UTF-8)
//  tidak mungkin lewat font biasa.
//
//  CARA KERJA
//   1. Emoji disimpan DI DALAM firmware dalam bentuk kode privat (PUA) U+E000 + nomor sprite
//      (3 byte UTF-8: EE 80 80 .. EE 80 xx). Itu muat di 16 bit, jadi lewat print()/drawString()/
//      textWidth()/setTextWrap() LovyanGFX apa adanya.
//   2. EmojiFont = pembungkus IFont. Untuk karakter biasa ia meneruskan ke font aslinya; untuk kode
//      PUA ia menggambar sprite RGB565 (10/16/24 px, emoji_data.h) sebesar tinggi baris + 2 px.
//      SEMUA setFont() di phone.ino dibungkus emjF() oleh apply_emoji_patch.py -> semua print()
//      di semua app otomatis bisa menampilkan emoji.
//   3. Batas antara dunia luar dan firmware: emjNormalize() (emoji sungguhan -> PUA) dipakai saat
//      MEMBACA teks dari luar (file Notepad, pesan Chat, jawaban Gemini); emjToWire() (PUA ->
//      emoji sungguhan) dipakai saat MENULIS/MENGIRIM (simpan Notepad, kirim Chat/Gemini). Jadi file
//      .txt di SD dan pesan di broker tetap UTF-8 standar -- bisa dibuka di HP/PC biasa.
//   4. Keyboard virtual punya halaman emoji (tombol smiley di sebelah 123), hapus (backspace)
//      sadar UTF-8 supaya satu ketukan membuang satu emoji utuh.
//
//  Emoji yang belum punya sprite digambar sebagai kotak "?" (bukan hilang/rusak).
//  Sprite digambar sendiri (gen_emoji.py, gaya flat) -- bukan font emoji berwarna.
//
//  Berkas ini digabung Arduino SETELAH phone.ino. Fungsi non-static yang dipakai berkas lain
//  diprototipe-kan di phone.ino (blok "EMOJI"). Tipe struct/kelas buatan sendiri TIDAK dipakai di
//  parameter/return fungsi non-static (aturan arduino-cli di proyek ini).
// =====================================================================
#include "emoji_data.h"

#define EMJ_PUA0 0xE000

// ---------------------------------------------------------------------
// pembungkus font
// ---------------------------------------------------------------------
struct EmojiFont : public lgfx::IFont {
  const lgfx::IFont* base;
  explicit EmojiFont(const lgfx::IFont* b) : base(b) {}

  font_type_t getType(void) const override { return base->getType(); }
  void getDefaultMetric(lgfx::FontMetrics* m) const override { base->getDefaultMetric(m); }
  bool unloadFont(void) override { return false; }      // font dasar bersifat statis, jangan dilepas

  static inline bool isEmoji(uint16_t u) { return u >= EMJ_PUA0 && u < EMJ_PUA0 + EMJ_COUNT; }
  // sisi emoji (px, sebelum dikali ukuran teks) = tinggi baris + 2; maju kursor = sisi + 2
  static inline int unitE(const lgfx::FontMetrics& dm) { int h = dm.height < 6 ? 6 : dm.height; return h + 2; }

  bool updateFontMetric(lgfx::FontMetrics* m, uint16_t uniCode) const override {
    if (!isEmoji(uniCode)) return base->updateFontMetric(m, uniCode);
    lgfx::FontMetrics dm; base->getDefaultMetric(&dm);
    int E = unitE(dm);
    m->width = (int16_t)(E + 2); m->x_advance = (int16_t)(E + 2); m->x_offset = 0;
    m->height = dm.height; m->y_advance = dm.y_advance; m->y_offset = dm.y_offset; m->baseline = dm.baseline;
    return true;
  }

  // nearest-neighbour dari sumber 10/16/24 px terdekat ke sisi t. 0x0000 = transparan.
  static void blit(lgfx::LGFXBase* g, int x, int y, int idx, int t) {
    if (idx < 0 || idx >= EMJ_COUNT || t < 4) return;
    int bi = 0, bd = 9999;
    for (int i = 0; i < 3; i++) {
      int d = (int)emjSizes[i] - t; if (d < 0) d = -d;
      if (d < bd || (d == bd && emjSizes[i] > emjSizes[bi])) { bd = d; bi = i; }
    }
    int n = emjSizes[bi];
    const uint16_t* src = (bi == 0) ? emjPx10[idx] : (bi == 1 ? emjPx16[idx] : emjPx24[idx]);
    for (int dy = 0; dy < t; dy++) {
      const uint16_t* row = src + (dy * n / t) * n;
      for (int dx = 0; dx < t; dx++) {
        uint16_t px = row[dx * n / t];
        if (px) g->drawPixel(x + dx, y + dy, px);
      }
    }
  }

  size_t drawChar(lgfx::LGFXBase* gfx, int32_t x, int32_t y, uint16_t c, const lgfx::TextStyle* st,
                  lgfx::FontMetrics* m, int32_t& filled_x) const override {
    if (!isEmoji(c)) return base->drawChar(gfx, x, y, c, st, m, filled_x);
    lgfx::FontMetrics dm; base->getDefaultMetric(&dm);
    int h = dm.height < 6 ? 6 : dm.height;
    int E = unitE(dm);
    float sx = st->size_x, sy = st->size_y;
    int adv = (int)((E + 2) * sx);
    int t = (int)(E * sy + 0.5f);
    if (st->back_rgb888 != st->fore_rgb888) {                 // teks dgn warna latar: isi sel dulu
      gfx->fillRect(x, y, adv, (int)(dm.height * sy), st->back_rgb888);
      filled_x = x + adv;
    }
    blit(gfx, x + (int)(1 * sx + 0.5f), y + (int)(((h - E) * 0.5f) * sy), (int)c - EMJ_PUA0, t);
    return (size_t)adv;
  }
};

#define EMJ_MAXFONTS 12
static EmojiFont* emjFonts[EMJ_MAXFONTS];
static int emjFontN = 0;

// Bungkus font dasar. Idempoten: font yg sudah terbungkus dikembalikan apa adanya.
const lgfx::IFont* emjF(const lgfx::IFont* base) {
  if (!base) return base;
  for (int i = 0; i < emjFontN; i++) {
    if (emjFonts[i]->base == base) return emjFonts[i];
    if ((const lgfx::IFont*)emjFonts[i] == base) return base;
  }
  if (emjFontN >= EMJ_MAXFONTS) return base;                  // kehabisan slot: tampil tanpa emoji, tetap aman
  EmojiFont* f = new EmojiFont(base);
  emjFonts[emjFontN++] = f;
  return f;
}

// ---------------------------------------------------------------------
// UTF-8 <-> PUA
// ---------------------------------------------------------------------
static int emjFindCp(uint32_t cp) {
  int lo = 0, hi = EMJ_PAIRS - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t v = emjPairs[mid].cp;
    if (v == cp) return emjPairs[mid].idx;
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

static void emjPutUtf8(String& o, uint32_t cp) {
  if (cp < 0x80) { o += (char)cp; }
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

// Decode satu karakter UTF-8 di p (sisa avail byte). Kembalikan panjang byte (>=1); cp=0xFFFFFFFF kalau rusak.
static int emjDecode(const uint8_t* p, int avail, uint32_t* cp) {
  uint8_t c = p[0];
  if (c < 0x80) { *cp = c; return 1; }
  int bl = ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : ((c & 0xF8) == 0xF0) ? 4 : 0;
  if (bl == 0 || bl > avail) { *cp = 0xFFFFFFFFUL; return 1; }
  uint32_t v = (bl == 2) ? (c & 0x1F) : (bl == 3) ? (c & 0x0F) : (c & 0x07);
  for (int i = 1; i < bl; i++) {
    if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFFFFFFUL; return 1; }
    v = (v << 6) | (p[i] & 0x3F);
  }
  *cp = v;
  return bl;
}

// true kalau karakter yang dibuang diam-diam (selektor variasi, ZWJ, penanda warna kulit, keycap)
static bool emjIgnorable(uint32_t cp) {
  return cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x20E3 || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}
// kodepoint emoji yang tidak punya sprite -> digambar sebagai kotak "?"
static bool emjLooksEmoji(uint32_t cp) {
  return cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x2190 && cp <= 0x21FF && cp != 0x2190 && cp != 0x2192);
}

// emoji sungguhan -> PUA. ASCII & karakter lain tidak diubah. Aman dipanggil berulang.
String emjNormalize(const String& s) {
  String o; o.reserve(s.length() + 4);
  const uint8_t* p = (const uint8_t*)s.c_str(); int n = (int)s.length(), i = 0;
  while (i < n) {
    if (p[i] < 0x80) { o += (char)p[i]; i++; continue; }
    uint32_t cp; int bl = emjDecode(p + i, n - i, &cp);
    if (cp == 0xFFFFFFFFUL) { o += '?'; i += bl; continue; }
    i += bl;
    if (emjIgnorable(cp)) continue;
    if (cp >= EMJ_PUA0 && cp < EMJ_PUA0 + EMJ_COUNT) { emjPutUtf8(o, cp); continue; }
    int k = emjFindCp(cp);
    if (k >= 0) { emjPutUtf8(o, EMJ_PUA0 + k); continue; }
    if (emjLooksEmoji(cp)) { emjPutUtf8(o, EMJ_PUA0 + EMJ_UNKNOWN); continue; }
    emjPutUtf8(o, cp);
  }
  return o;
}

// PUA -> emoji sungguhan (UTF-8 standar) untuk disimpan ke file / dikirim lewat jaringan.
String emjToWire(const String& s) {
  String o; o.reserve(s.length() + 8);
  const uint8_t* p = (const uint8_t*)s.c_str(); int n = (int)s.length(), i = 0;
  while (i < n) {
    if (p[i] < 0x80) { o += (char)p[i]; i++; continue; }
    uint32_t cp; int bl = emjDecode(p + i, n - i, &cp);
    if (cp == 0xFFFFFFFFUL) { o += '?'; i += bl; continue; }
    if (cp >= EMJ_PUA0 && cp < EMJ_PUA0 + EMJ_COUNT) emjPutUtf8(o, emjCanon[cp - EMJ_PUA0]);
    else { for (int k = 0; k < bl; k++) o += (char)p[i + k]; }
    i += bl;
  }
  return o;
}

// Salin teks pesan ke buffer tetap utk Chat: ASCII tercetak + emoji (PUA); selain itu '?'.
void emjSanitizeBuf(char* dst, int dstSize, const char* src, int len) {
  String t; t.reserve(len + 1);
  for (int i = 0; i < len; i++) t += src[i];
  String nrm = emjNormalize(t);
  const uint8_t* p = (const uint8_t*)nrm.c_str(); int n = (int)nrm.length(), i = 0, o = 0;
  while (i < n && o < dstSize - 1) {
    uint8_t c = p[i];
    if (c < 0x80) { dst[o++] = (c >= 32 && c <= 126) ? (char)c : '?'; i++; continue; }
    uint32_t cp; int bl = emjDecode(p + i, n - i, &cp);
    if (cp >= EMJ_PUA0 && cp < EMJ_PUA0 + EMJ_COUNT && bl == 3) {
      if (o + 3 > dstSize - 1) break;                          // tidak muat utuh: berhenti, jangan potong tengah
      dst[o++] = (char)p[i]; dst[o++] = (char)p[i + 1]; dst[o++] = (char)p[i + 2];
    } else dst[o++] = '?';
    i += bl;
  }
  dst[o] = 0;
}

// Hanya ASCII tercetak (utk nama pengguna Chat: emoji di nama tidak dikirim).
String emjAsciiOnly(const String& s) {
  String o; o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) { unsigned char c = (unsigned char)s[i]; if (c >= 32 && c <= 126) o += (char)c; }
  return o;
}

// ---------------------------------------------------------------------
// pemotongan / hapus / hitung lebar yang sadar UTF-8
// ---------------------------------------------------------------------
// Buang SATU karakter terakhir (emoji = 3 byte sekaligus).
void emjBackspace(String& s) {
  int n = (int)s.length();
  if (n <= 0) return;
  int k = n - 1;
  while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) k--;
  s = s.substring(0, k);
}
// Maksimal maxBytes byte dari DEPAN, selalu berhenti di batas karakter.
String emjHead(const String& s, int maxBytes) {
  int n = (int)s.length();
  if (n <= maxBytes) return s;
  int cut = maxBytes < 0 ? 0 : maxBytes;
  while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;
  return s.substring(0, cut);
}
// Maksimal maxBytes byte dari BELAKANG, mulai di batas karakter.
String emjTail(const String& s, int maxBytes) {
  int n = (int)s.length();
  if (n <= maxBytes) return s;
  int st = n - (maxBytes < 0 ? 0 : maxBytes);
  while (st < n && (((unsigned char)s[st]) & 0xC0) == 0x80) st++;
  return s.substring(st);
}
// Buang karakter UTF-8 yang terpotong di ujung buffer C (mis. hasil strncpy di tengah emoji).
static void emjFixTail(char* s) {
  int n = (int)strlen(s);
  if (n <= 0) return;
  int i = n - 1;
  while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80) i--;
  unsigned char lead = (unsigned char)s[i];
  int need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
  if (n - i < need) s[i] = 0;
}
// Potong buffer C di maxBytes tanpa membelah karakter (juga membuang sisa karakter terpotong di ujung).
void emjCutZ(char* s, int maxBytes) {
  int n = (int)strlen(s);
  if (n > maxBytes) {
    int cut = maxBytes < 0 ? 0 : maxBytes;
    while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) cut--;
    s[cut] = 0;
  }
  emjFixTail(s);
}
// Lebar teks dalam "sel" Font0 (6 px): ASCII = 1 sel, emoji (PUA 3 byte) = 2 sel.
int emjCells(const char* p, int len) {
  int cells = 0, i = 0;
  while (i < len) {
    if ((unsigned char)p[i] == 0xEE && i + 2 < len) { cells += 2; i += 3; }
    else { cells += 1; i++; }
  }
  return cells;
}
