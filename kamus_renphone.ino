// =====================================================================
// kamus_renphone.ino -- KAMUS KEYBOARD: baris saran kata + autokoreksi
//
// Logika (saran, jarak edit, autokoreksi) ada di kamus_core.h (tanpa Arduino, diuji di
// komputer: tests_kamus/). Berkas ini hanya penghubung: menggambar baris saran di atas
// tuts, menangani ketukan, menyimpan pengaturan/kata pelajaran, dan memuat kamus SD.
//
// Dipanggil dari phone.ino:  kbY()/drawKb()/kbTouch()  (lihat blok "VIRTUAL KEYBOARD").
// Diurutkan SETELAH phone.ino, chat_renphone.ino, dst. (urutan abjad Arduino), jadi di sini
// variabel field teks (noteText, chatInput) sudah terlihat.
//
// Cara pakai (untuk pengguna):
//   - Ketik: 3 saran muncul di atas tuts; ketuk saran -> kata diganti + spasi.
//   - Tekan spasi / , . ? ! : kata salah ketik otomatis dikoreksi (kata yg dikoreksi = oranye/aksen
//     di baris saran SEBELUM spasi ditekan).
//   - Tekan HAPUS tepat setelah koreksi -> kembali ke ketikan asli, dan kata itu dipelajari
//     (tidak dikoreksi lagi).
//   - Tombol "ab" di kiri baris saran: ganti mode  saran+koreksi -> saran saja -> mati.
//   - Kamus tambahan opsional: /kamus.txt di kartu SD (satu kata per baris, yg sering dulu).
// =====================================================================
#include "kamus_core.h"
#include "kamus_data.h"

#define KMS_NVS_NS    "kms"
#define KMS_SD_FILE   "/kamus.txt"
#define KMS_SD_MAXB   (2 * 1024 * 1024)      // batas ukuran berkas SD
#define KMS_USER_MAX  120                    // kata pelajaran maksimum (disimpan di NVS)
#define KMS_BAR_H     18                     // tinggi baris saran (px)
#define KMS_CHIP_W    30                     // lebar tombol mode di kiri baris saran

static bool        kmsInited = false;
static int         kmsMode = 2;              // 0 mati | 1 saran saja | 2 saran + autokoreksi (pilihan pengguna)
static char        kmsUserBuf[KMS_USER_MAX][KMS_MAXW + 1];
static const char* kmsUserPtr[KMS_USER_MAX];
static int         kmsUserN = 0;
static char*       kmsSdBuf = nullptr;       // isi /kamus.txt (PSRAM), dipotong per kata di tempat
static const char** kmsSdPtr = nullptr;
static int         kmsSdN = 0;
static int         kmsIdScore = 0, kmsEnScore = 0;   // bahasa yang lebih sering dipakai -> didahulukan
static KmsCtx      kmsC;

static KmsSugs     kmsCur;                   // saran untuk kata yang sedang diketik
static int         kmsFixIdx = -1;           // slot saran yang akan dipakai autokoreksi (-1 = tidak ada)
static uint32_t    kmsSigLast = 0;
static char        kmsSeen[12][KMS_MAXW + 1];  // kata asing yang pernah dikonfirmasi 1x (dipelajari saat kedua kali)
static int         kmsSeenPos = 0;
static struct { bool valid; char orig[KMS_MAXW + 1]; char fixed[KMS_MAXW + 1]; char sep; } kmsAc = { false, "", "", 0 };

// ---------------------------------------------------------------- NVS / SD
static void kmsSaveUser() {
  String j;
  for (int i = 0; i < kmsUserN; i++) { if (i) j += ','; j += kmsUserBuf[i]; }
  Preferences p; p.begin(KMS_NVS_NS, false); p.putString("uw", j); p.end();
}
static void kmsSaveMode() { Preferences p; p.begin(KMS_NVS_NS, false); p.putInt("mode", kmsMode); p.end(); }

static void kmsLoadSd() {
  if (!sdReady) return;
  File f = SD_MMC.open(KMS_SD_FILE, FILE_READ);
  if (!f) return;
  size_t sz = (size_t)f.size();
  if (sz < 2 || sz > KMS_SD_MAXB) { f.close(); return; }
  uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  char* buf = (char*)heap_caps_malloc(sz + 1, caps);
  if (!buf) { f.close(); return; }
  size_t got = f.read((uint8_t*)buf, sz);
  f.close();
  buf[got] = 0;
  int cap = 1; for (size_t i = 0; i < got; i++) if (buf[i] == '\n') cap++;
  const char** ptr = (const char**)heap_caps_malloc((size_t)cap * sizeof(char*), caps);
  if (!ptr) { free(buf); return; }
  int n = 0; size_t i = 0;
  while (i < got) {
    size_t s = i;
    while (i < got && buf[i] != '\n' && buf[i] != '\r') i++;
    size_t e = i;
    while (i < got && (buf[i] == '\n' || buf[i] == '\r')) i++;
    size_t k = s;                                               // kata pertama saja ("kata 1234" = format frekuensi)
    while (k < e && buf[k] != ' ' && buf[k] != '\t' && buf[k] != ',') k++;
    int len = (int)(k - s);
    if (len < 2 || len > KMS_MAXW) continue;
    bool ok = true;
    for (size_t j = s; j < k; j++) {
      char ch = buf[j];
      if (ch >= 'A' && ch <= 'Z') buf[j] = (char)(ch + 32);
      else if (ch < 'a' || ch > 'z') { ok = false; break; }
    }
    if (!ok) continue;
    buf[k] = 0;
    ptr[n++] = buf + s;
  }
  if (n == 0) { free(buf); free((void*)ptr); return; }
  kmsSdBuf = buf; kmsSdPtr = ptr; kmsSdN = n;
}

static void kmsInit() {
  if (kmsInited) return;
  kmsInited = true;
  for (int i = 0; i < KMS_USER_MAX; i++) kmsUserPtr[i] = kmsUserBuf[i];
  {
    Preferences p; p.begin(KMS_NVS_NS, true);
    kmsMode = p.getInt("mode", 2);
    String uw = p.getString("uw", "");
    p.end();
    if (kmsMode < 0 || kmsMode > 2) kmsMode = 2;
    int st = 0, L = (int)uw.length();
    while (st < L && kmsUserN < KMS_USER_MAX) {
      int en = uw.indexOf(',', st); if (en < 0) en = L;
      int len = en - st;
      if (len >= 2 && len <= KMS_MAXW) {
        bool ok = true;
        for (int j = st; j < en; j++) { char ch = uw[j]; if (ch < 'a' || ch > 'z') { ok = false; break; } }
        if (ok) { memcpy(kmsUserBuf[kmsUserN], uw.c_str() + st, (size_t)len); kmsUserBuf[kmsUserN][len] = 0; kmsUserN++; }
      }
      st = en + 1;
    }
  }
  kmsLoadSd();
  kmsC.user = { kmsUserPtr, kmsUserN };
  kmsC.sd   = { kmsSdPtr, kmsSdN };
  kmsC.id   = { kmsWordsID, KMS_ID_N };
  kmsC.en   = { kmsWordsEN, KMS_EN_N };
  kmsC.preferEn = 0;
}

// ---------------------------------------------------------------- kebijakan per field
// 0 = tanpa baris saran (password, URL, SSID, API key, nama, pencarian, dll.)
// 2 = saran + autokoreksi (teks bebas). Tambah field lain di sini bila perlu.
// Hanya field yang MENAMPILKAN ketikan saat keyboard terbuka. aiPrompt (app AI) sengaja belum
// dimasukkan: layar AI menyembunyikan kotak input selagi keyboard terbuka, jadi autokoreksi
// di sana berjalan tanpa terlihat. Masukkan &aiPrompt di sini setelah layarnya menampilkan draf.
static int kmsPolicy() {
  if (!kbTarget) return 0;
  if (kbTarget == &noteText || kbTarget == &chatInput) return 2;
  return 0;
}
static int kmsEffMode() { int p = kmsPolicy(); return kmsMode < p ? kmsMode : p; }

// Tinggi baris saran. Dipakai kbY(): layar lain otomatis bergeser karena memakai kbY().
int kbBarH() { return (kbVisible && kmsPolicy() > 0) ? KMS_BAR_H : 0; }

// ---------------------------------------------------------------- belajar
static void kmsLower(const char* src, int n, char* dst) { for (int i = 0; i < n; i++) dst[i] = kmsLow(src[i]); dst[n] = 0; }

static void kmsLearn(const char* lw, int wl) {
  if (wl < 3 || wl > KMS_MAXW || kmsInList(kmsC.user, lw, wl)) return;
  int last = kmsUserN < KMS_USER_MAX ? kmsUserN : KMS_USER_MAX - 1;
  for (int i = last; i > 0; i--) memcpy(kmsUserBuf[i], kmsUserBuf[i - 1], KMS_MAXW + 1);
  memcpy(kmsUserBuf[0], lw, (size_t)wl); kmsUserBuf[0][wl] = 0;
  if (kmsUserN < KMS_USER_MAX) kmsUserN++;
  kmsC.user.n = kmsUserN;
  kmsSaveUser();
}
static void kmsNoteUnknown(const char* lw, int wl) {          // kata asing yg diketik 2x -> dianggap benar
  for (int i = 0; i < 12; i++) {
    if (strcmp(kmsSeen[i], lw) == 0) { kmsSeen[i][0] = 0; kmsLearn(lw, wl); return; }
  }
  memcpy(kmsSeen[kmsSeenPos], lw, (size_t)wl); kmsSeen[kmsSeenPos][wl] = 0;
  kmsSeenPos = (kmsSeenPos + 1) % 12;
}
static void kmsTally(const char* lw, int wl) {                 // ID vs EN: yang lebih sering dipakai didahulukan
  bool a = kmsInList(kmsC.id, lw, wl), b = kmsInList(kmsC.en, lw, wl);
  if (a == b) return;
  if (a) kmsIdScore++; else kmsEnScore++;
  if (kmsIdScore + kmsEnScore >= 24) { kmsIdScore /= 2; kmsEnScore /= 2; }
  kmsC.preferEn = kmsEnScore > kmsIdScore ? 1 : 0;
}

// ---------------------------------------------------------------- hitung saran (hanya saat teks berubah)
static void kmsRefresh() {
  kmsInit();
  int mode = kmsEffMode();
  if (!kbTarget || mode == 0) { kmsCur.n = 0; kmsFixIdx = -1; kmsSigLast = 0; return; }
  int n = (int)kbTarget->length();
  const char* t = kbTarget->c_str();
  uint32_t sig = (uint32_t)(uintptr_t)kbTarget ^ ((uint32_t)n * 2654435761u) ^ ((uint32_t)mode * 977u) ^ ((uint32_t)kmsUserN << 20);
  for (int i = (n > 25 ? n - 25 : 0); i < n; i++) sig = sig * 16777619u + (uint8_t)t[i];
  if (sig == kmsSigLast) return;
  kmsSigLast = sig;
  kmsCur.n = 0; kmsFixIdx = -1;
  int wl = kmsTailWord(t, n);
  if (wl < 1 || wl > KMS_MAXW) return;
  kmsSuggest(kmsC, t + n - wl, wl, kmsCur);
  if (mode == 2) {                                              // tandai kata yg akan dipakai autokoreksi
    char fixed[KMS_MAXW + 1];
    if (kmsAutoFix(kmsC, t, n, fixed)) {
      for (int i = 0; i < kmsCur.n; i++) if (strcmp(kmsCur.w[i], fixed) == 0) { kmsFixIdx = i; break; }
      if (kmsFixIdx < 0) {                                      // tidak muncul di 3 saran -> paksa jadi slot pertama
        for (int i = KMS_SUG - 1; i > 0; i--) memcpy(kmsCur.w[i], kmsCur.w[i - 1], KMS_MAXW + 1);
        memcpy(kmsCur.w[0], fixed, strlen(fixed) + 1);
        if (kmsCur.n < KMS_SUG) kmsCur.n++;
        kmsFixIdx = 0;
      }
    }
  }
}

// ---------------------------------------------------------------- gambar
void kbBarDraw(LGFX_Sprite& s, int yTop) {
  int h = kbBarH();
  if (!h) return;
  kmsRefresh();
  int L = kbLeft(), R = SCR_W - L, mode = kmsEffMode();
  int cy = yTop + 1, ch = h - 3;
  uint16_t fnC = blend565(T().surface2, T().accent, 70);

  // tombol mode "ab": penuh (aksen) = saran+koreksi | garis bawah = saran saja | dicoret = mati
  uint16_t cbg = (mode == 2) ? (uint16_t)T().accent : fnC;
  uint16_t cfg = (mode == 2) ? appIconInk(T().accent) : (uint16_t)T().text;
  nyxAaRR(s, L, cy, KMS_CHIP_W, ch, 7, cbg);
  s.setTextSize(1); s.setTextColor(cfg);
  s.setCursor(L + (KMS_CHIP_W - 12) / 2, cy + (ch - 8) / 2);
  s.print("ab");
  if (mode == 1) s.drawFastHLine(L + 9, cy + ch - 4, 12, cfg);
  if (mode == 0) s.drawLine(L + 6, cy + ch - 4, L + KMS_CHIP_W - 6, cy + 4, T().subtext);

  // tiga slot saran
  int sx = L + KMS_CHIP_W + 4, slotW = (R - sx) / KMS_SUG;
  for (int i = 0; i < KMS_SUG; i++) {
    int x = sx + i * slotW;
    if (i > 0) s.drawFastVLine(x, cy + 3, ch - 6, T().divider);
    if (mode == 0 || i >= kmsCur.n) continue;
    char buf[KMS_MAXW + 3];
    int maxc = (slotW - 8) / 6; if (maxc < 3) maxc = 3;
    int len = (int)strlen(kmsCur.w[i]);
    if (len > maxc) { memcpy(buf, kmsCur.w[i], (size_t)(maxc - 1)); buf[maxc - 1] = '.'; buf[maxc] = 0; len = maxc; }
    else memcpy(buf, kmsCur.w[i], (size_t)len + 1);
    s.setTextSize(1);
    s.setTextColor(i == kmsFixIdx ? (uint16_t)T().accent : (uint16_t)T().text);
    s.setCursor(x + (slotW - len * 6) / 2, cy + (ch - 8) / 2);
    s.print(buf);
  }
}

// ---------------------------------------------------------------- ketukan di baris saran
bool kbBarTouch(int x, int y) {
  (void)y;
  if (!kbBarH()) return false;
  kmsInit();
  int L = kbLeft(), R = SCR_W - L;
  if (x < L + KMS_CHIP_W + 2) {                                 // ganti mode: 2 -> 1 -> 0 -> 2
    kmsMode = kmsMode > 0 ? kmsMode - 1 : 2;
    kmsSaveMode(); kmsSigLast = 0; kmsAc.valid = false;
    showToast(kmsMode == 2 ? "Kamus: saran + koreksi" : (kmsMode == 1 ? "Kamus: saran saja" : "Kamus: mati"), 1100);
    return true;
  }
  if (kmsEffMode() == 0 || !kbTarget) return true;
  int sx = L + KMS_CHIP_W + 4, slotW = (R - sx) / KMS_SUG;
  if (x < sx || slotW < 1) return true;
  int i = (x - sx) / slotW;
  if (i < 0 || i >= KMS_SUG) return true;
  kmsRefresh();
  if (i >= kmsCur.n) return true;
  String& t = *kbTarget;
  int n = (int)t.length(), wl = kmsTailWord(t.c_str(), n);
  if (wl < 1 || wl > KMS_MAXW) return true;
  char lw[KMS_MAXW + 1]; kmsLower(kmsCur.w[i], (int)strlen(kmsCur.w[i]), lw);
  t.remove((unsigned int)(n - wl));
  t += kmsCur.w[i];
  t += ' ';
  kmsAc.valid = false;
  kmsTally(lw, (int)strlen(lw));
  return true;
}

// ---------------------------------------------------------------- hook ketikan (dipanggil kbTouch)
// SEBELUM karakter c ditambahkan ke teks. c = 0 untuk tuts non-teks (emoji dsb.).
void kbSugOnChar(String& t, char c) {
  kmsAc.valid = false;
  kmsInit();
  int mode = kmsEffMode();
  if (mode == 0) return;
  if (!(c == ' ' || c == ',' || c == '.' || c == '?' || c == '!')) return;     // bukan pemisah kata
  int n = (int)t.length(), wl = kmsTailWord(t.c_str(), n);
  if (wl < 2 || wl > KMS_MAXW) return;
  char lw[KMS_MAXW + 1]; kmsLower(t.c_str() + n - wl, wl, lw);
  if (mode == 2) {
    char fixed[KMS_MAXW + 1];
    if (kmsAutoFix(kmsC, t.c_str(), n, fixed)) {
      memcpy(kmsAc.orig, t.c_str() + n - wl, (size_t)wl); kmsAc.orig[wl] = 0;
      strcpy(kmsAc.fixed, fixed); kmsAc.sep = c; kmsAc.valid = true;
      t.remove((unsigned int)(n - wl));
      t += fixed;
      return;                                                   // pemisah c ditambahkan oleh kbTouch setelah ini
    }
  }
  if (kmsKnown(kmsC, lw, wl)) kmsTally(lw, wl);
  else if (wl >= 3) kmsNoteUnknown(lw, wl);
}

// Hapus tepat setelah autokoreksi: kembalikan ketikan asli & pelajari kata itu. true = sudah ditangani.
bool kbSugBackspace(String& t) {
  if (!kmsAc.valid) return false;
  kmsAc.valid = false;
  int fl = (int)strlen(kmsAc.fixed), need = fl + 1, n = (int)t.length();
  if (n < need || t[n - 1] != kmsAc.sep) return false;
  if (strncmp(t.c_str() + n - need, kmsAc.fixed, (size_t)fl) != 0) return false;
  t.remove((unsigned int)(n - need));
  t += kmsAc.orig;
  char lw[KMS_MAXW + 1]; int ol = (int)strlen(kmsAc.orig); kmsLower(kmsAc.orig, ol, lw);
  kmsLearn(lw, ol);
  return true;
}
