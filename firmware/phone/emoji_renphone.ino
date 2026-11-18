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
//  SET EMOJI DARI SD: kalau /emoji.bin ada di SD (dibuat tools/make_emoji_bin.py dari Twemoji), emjInit()
//  memuatnya ke PSRAM dan set itu menggantikan set bawaan (55 sprite di emoji_data.h, jadi cadangan).
//  Penomoran PUA = urutan sprite di file; hanya tersimpan di RAM (file/jaringan selalu emoji sungguhan),
//  jadi mengganti emoji.bin cukup restart. emjInit() dipanggil SEKALI di setup() sesudah initSD().
//
//  Format emoji.bin (little-endian): "NYXE", u16 versi=1, u16 jumlah N, u8 ukuran[3]={10,16,24}, 5 byte nol;
//  lalu N x u32 kodepoint; lalu blok piksel RGB565 per ukuran (N*n*n u16, 0x0000 = transparan).
//
//  Emoji yang belum punya sprite digambar sebagai kotak "?" (bukan hilang/rusak).
//  Sprite digambar sendiri (gen_emoji.py, gaya flat) -- bukan font emoji berwarna.
//
//  Berkas ini digabung Arduino SETELAH phone.ino. Fungsi non-static yang dipakai berkas lain
//  diprototipe-kan di phone.ino (blok "EMOJI"). Tipe struct/kelas buatan sendiri TIDAK dipakai di
//  parameter/return fungsi non-static (aturan arduino-cli di proyek ini).
// =====================================================================
#include "emoji_data.h"

#define EMJ_PUA0     0xE000
#define EMJ_PUA_UNK  0xEFFF          // slot tetap utk kotak "?" (selalu sprite bawaan)
#define EMJ_SD_MAX   4000            // maks sprite dari SD (E000..EF9F), tetap berlead-byte 0xEE
#define EMJ_FILE     "/emoji.bin"

static bool      emjInited = false;
static uint16_t  emjSdN = 0;                         // >0 = set dari SD aktif
static uint32_t* emjSdCp = nullptr;                  // kodepoint kanonik per sprite
// NYX-EMJCACHE-V1: sprite TIDAK dimuat semua. File tetap terbuka, sprite dibaca per kebutuhan lalu disimpan di cache LRU (per ukuran).
#define EMJ_CACHE_N 128                              // sprite per ukuran di cache
static File      emjFile;                            // /emoji.bin (tetap terbuka selama set SD aktif)
static uint32_t  emjOff[3] = {0, 0, 0};              // offset awal blok piksel tiap ukuran di file
static uint16_t* emjCPx[3] = { nullptr, nullptr, nullptr };   // cache piksel: EMJ_CACHE_N * n*n u16 (dialokasi saat ukuran dipakai)
static uint16_t  emjCKey[3][EMJ_CACHE_N];            // indeks sprite + 1 (0 = slot kosong)
static uint32_t  emjCUse[3][EMJ_CACHE_N];            // stempel pemakaian terakhir (LRU)
static uint32_t  emjTick = 0;
static uint8_t   emjFails = 0;
static volatile bool emjBusy = false;
static uint8_t   emjStage[24 * 24 * 2] __attribute__((aligned(4)));   // NYX-EMJCACHE-FIX1: penampung baca SD di RAM internal (sprite terbesar 24x24 RGB565)
static uint64_t* emjSdKey = nullptr;                 // (cp<<16 | idx) terurut, utk pencarian biner

static inline int  emjCount() { return emjSdN ? (int)emjSdN : (int)EMJ_UNKNOWN; }   // sprite bawaan: 54 (+ kotak "?")
static inline bool emjIsPuaCp(uint32_t cp) { return (cp >= EMJ_PUA0 && cp < EMJ_PUA0 + (uint32_t)emjCount()) || cp == EMJ_PUA_UNK; }

// EMJ-CACHE BEGIN
// Piksel sprite idx pada ukuran bi (0..2), atau nullptr bila tak terbaca (-> pemanggil menggambar kotak "?").
static const uint16_t* emjSprite(int bi, int idx) {
  if (bi < 0 || bi > 2 || idx < 0 || idx >= (int)emjSdN || emjBusy) return nullptr;
  int n = emjSizes[bi];
  size_t px = (size_t)n * n;
  uint16_t* base = emjCPx[bi];
  if (!base) {
    base = (uint16_t*)heap_caps_malloc((size_t)EMJ_CACHE_N * px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!base) return nullptr;
    memset(emjCKey[bi], 0, sizeof(emjCKey[bi])); memset(emjCUse[bi], 0, sizeof(emjCUse[bi]));
    emjCPx[bi] = base;
  }
  emjTick++;
  int victim = 0; uint32_t oldest = 0xFFFFFFFFu;
  for (int i = 0; i < EMJ_CACHE_N; i++) {
    if (emjCKey[bi][i] == (uint16_t)(idx + 1)) { emjCUse[bi][i] = emjTick; return base + (size_t)i * px; }
    if (emjCUse[bi][i] < oldest) { oldest = emjCUse[bi][i]; victim = i; }
  }
  if (!emjFile) return nullptr;                      // file sudah ditutup (kartu bermasalah)
  emjBusy = true;
  uint16_t* dst = base + (size_t)victim * px;
  bool ok = emjFile.seek(emjOff[bi] + (uint32_t)idx * (uint32_t)(px * 2)) && emjFile.read(emjStage, px * 2) == px * 2;   // baca ke RAM internal dulu (bukan DMA langsung ke PSRAM)
  if (ok) memcpy(dst, emjStage, px * 2);
  emjBusy = false;
  if (!ok) { emjCKey[bi][victim] = 0; emjCUse[bi][victim] = 0; if (++emjFails >= 3) emjFile.close(); return nullptr; }
  emjFails = 0;
  emjCKey[bi][victim] = (uint16_t)(idx + 1); emjCUse[bi][victim] = emjTick;
  return dst;
}
// EMJ-CACHE END

// ---------------------------------------------------------------------
// pembungkus font
// ---------------------------------------------------------------------
struct EmojiFont : public lgfx::IFont {
  const lgfx::IFont* base;
  explicit EmojiFont(const lgfx::IFont* b) : base(b) {}

  font_type_t getType(void) const override { return base->getType(); }
  void getDefaultMetric(lgfx::FontMetrics* m) const override { base->getDefaultMetric(m); }
  bool unloadFont(void) override { return false; }      // font dasar bersifat statis, jangan dilepas

  static inline bool isEmoji(uint16_t u) { return emjIsPuaCp(u); }
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
    if (t < 4) return;                                        // idx < 0 = kotak "?" bawaan
    int bi = 0, bd = 9999;
    for (int i = 0; i < 3; i++) {
      int d = (int)emjSizes[i] - t; if (d < 0) d = -d;
      if (d < bd || (d == bd && emjSizes[i] > emjSizes[bi])) { bd = d; bi = i; }
    }
    int n = emjSizes[bi];
    const uint16_t* src = (idx >= 0 && emjSdN) ? emjSprite(bi, idx) : nullptr;     // set SD: dari cache (dibaca dari SD bila belum ada)
    if (!src) {
      int k = (idx < 0 || emjSdN) ? EMJ_UNKNOWN : idx;                           // set SD gagal dibaca -> kotak "?"
      src = (bi == 0) ? emjPx10[k] : (bi == 1 ? emjPx16[k] : emjPx24[k]);
    }
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
    blit(gfx, x + (int)(1 * sx + 0.5f), y + (int)(((h - E) * 0.5f) * sy), (c == EMJ_PUA_UNK) ? -1 : (int)c - EMJ_PUA0, t);
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
  if (emjSdN) {                                               // set dari SD
    int lo = 0, hi = (int)emjSdN - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >> 1;
      uint32_t v = (uint32_t)(emjSdKey[mid] >> 16);
      if (v == cp) return (int)(emjSdKey[mid] & 0xFFFF);
      if (v < cp) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
  }
  int lo = 0, hi = EMJ_PAIRS - 1;                             // set bawaan
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t v = emjPairs[mid].cp;
    if (v == cp) return emjPairs[mid].idx;
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}
static uint32_t emjCanonOf(uint32_t puaCp) {
  if (puaCp == EMJ_PUA_UNK) return 0x2753;
  uint32_t i = puaCp - EMJ_PUA0;
  return emjSdN ? emjSdCp[i] : emjCanon[i];
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
  return cp >= 0x1F000 || (cp >= 0x231A && cp <= 0x23FF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF);
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
    if (emjIsPuaCp(cp)) { emjPutUtf8(o, cp); continue; }
    int k = emjFindCp(cp);
    if (k >= 0) { emjPutUtf8(o, EMJ_PUA0 + k); continue; }
    if (emjLooksEmoji(cp)) { emjPutUtf8(o, EMJ_PUA_UNK); continue; }
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
    if (emjIsPuaCp(cp)) emjPutUtf8(o, emjCanonOf(cp));
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
    if (emjIsPuaCp(cp) && bl == 3) {
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

// ---------------------------------------------------------------------
// set emoji dari SD (/emoji.bin): indeks di PSRAM, sprite dibaca per kebutuhan + cache LRU (NYX-EMJCACHE-V1)
// ---------------------------------------------------------------------
static int emjCmpKey(const void* a, const void* b) {
  uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}
static bool emjReadAll(File& f, void* dst, size_t bytes) {
  uint8_t* p = (uint8_t*)dst;
  while (bytes) {
    size_t n = bytes > 16384 ? 16384 : bytes;
    if (f.read(p, n) != n) return false;
    p += n; bytes -= n; yield();
  }
  return true;
}
static void emjFreeSd() {
  if (emjFile) emjFile.close();
  if (emjSdCp) { free(emjSdCp); emjSdCp = nullptr; }
  if (emjSdKey) { free(emjSdKey); emjSdKey = nullptr; }
  for (int i = 0; i < 3; i++) if (emjCPx[i]) { free(emjCPx[i]); emjCPx[i] = nullptr; }
}
// Panggil SEKALI di setup(), sesudah initSD() dan SEBELUM teks apa pun dinormalisasi.
// Tanpa SD / tanpa file / file rusak / PSRAM kurang: tetap pakai set bawaan (aman).
void emjInit() {
  if (emjInited) return;
  emjInited = true;
  if (!sdReady) return;
  File f = SD_MMC.open(EMJ_FILE, FILE_READ);
  if (!f) return;
  uint8_t h[16];
  bool ok = (f.read(h, 16) == 16) && memcmp(h, "NYXE", 4) == 0 && h[4] == 1 && h[5] == 0 &&
            h[8] == emjSizes[0] && h[9] == emjSizes[1] && h[10] == emjSizes[2];
  uint32_t n = ok ? ((uint32_t)h[6] | ((uint32_t)h[7] << 8)) : 0;
  if (!ok || n < 1 || n > EMJ_SD_MAX) { f.close(); return; }
  size_t szPx[3];
  size_t total = 16 + (size_t)n * 4;
  for (int i = 0; i < 3; i++) { szPx[i] = (size_t)n * emjSizes[i] * emjSizes[i] * 2; total += szPx[i]; }
  if ((size_t)f.size() < total) { f.close(); return; }
  uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  emjSdCp  = (uint32_t*)heap_caps_malloc((size_t)n * 4, caps);
  emjSdKey = (uint64_t*)heap_caps_malloc((size_t)n * 8, caps);
  bool good = emjSdCp && emjSdKey && emjReadAll(f, emjSdCp, (size_t)n * 4);     // hanya indeks kodepoint; sprite dibaca nanti (cache LRU)
  if (!good) { f.close(); emjFreeSd(); return; }                                // set bawaan tetap dipakai
  emjOff[0] = 16 + (uint32_t)n * 4; emjOff[1] = emjOff[0] + (uint32_t)szPx[0]; emjOff[2] = emjOff[1] + (uint32_t)szPx[1];
  for (uint32_t i = 0; i < n; i++) emjSdKey[i] = ((uint64_t)emjSdCp[i] << 16) | i;
  qsort(emjSdKey, n, sizeof(uint64_t), emjCmpKey);
  emjFile = f;                                                 // tetap terbuka (salinan handle)
  emjSdN = (uint16_t)n;                                        // terakhir: baru sekarang set SD aktif
}

// ---------------------------------------------------------------------
// halaman emoji keyboard virtual: 30 emoji per halaman (3 baris x 10)
// ---------------------------------------------------------------------
int emjKbPages() { int n = emjCount(); return n < 1 ? 1 : (n + 29) / 30; }
// Label (3 byte UTF-8 PUA + NUL) tuts ke-slot (0..29) di halaman page. false = tuts kosong.
bool emjKbLabel(int page, int slot, char* out) {
  int k = page * 30 + slot;
  if (slot < 0 || slot >= 30 || k < 0 || k >= emjCount()) return false;
  uint32_t cp = EMJ_PUA0 + k;
  out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); out[3] = 0;
  return true;
}
