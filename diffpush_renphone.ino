// ============================================================
//  diffpush_renphone.ino -- push layar "selisih frame" (dirty-rect) untuk ILI9488
//
//  Masalah: ILI9488 lewat SPI wajib 18-bit (3 byte/piksel). Satu frame penuh
//  480x320 = 460 KB -> ~92 ms @40MHz (~11 fps) PADAHAL kebanyakan frame UI cuma
//  berubah sedikit (jam, highlight tombol, kursor, progress bar...).
//
//  Solusi: simpan salinan "apa yg sekarang ada di layar" (shadow, PSRAM),
//  bandingkan dgn canvas, lalu kirim HANYA kotak yg berubah. Frame yg sama
//  persis tidak mengirim apa-apa sama sekali.
//
//  Aturan emas: shadow harus SELALU sama dgn isi layar. Kalau ada kode yg
//  menulis ke layar tanpa lewat push() -> panggil dpInvalidate() (layar
//  berikutnya dikirim penuh) atau dpSyncRows()/dpSyncAll() (kalau tahu persis
//  area yg ditulis & isinya sama dgn canvas). Sebagai jaring pengaman, frame
//  penuh dikirim ulang tiap DP_IDLE_REFRESH_MS saat layar diam.
// ============================================================
#include <esp_heap_caps.h>

#ifndef DP_BAND_ROWS
#define DP_BAND_ROWS 8              // granularitas vertikal pemindaian
#endif
#ifndef DP_FULL_PCT
#define DP_FULL_PCT 55              // kalau area berubah >= segini % -> kirim penuh (lebih murah drpd banyak kotak)
#endif
#ifndef DP_IDLE_REFRESH_MS
#define DP_IDLE_REFRESH_MS 15000UL  // jaring pengaman anti-glitch (kirim ulang penuh saat diam)
#endif
#define DP_MAX_RECTS 48

static uint8_t* dpShadow = nullptr;
static int  dpW = 0, dpH = 0;
static bool dpValid = false;
static unsigned long dpLastFullMs = 0;

void dpInvalidate() { dpValid = false; }

static void dpFree() {
  if (dpShadow) { heap_caps_free(dpShadow); dpShadow = nullptr; }
  dpW = dpH = 0; dpValid = false;
}

static bool dpEnsure() {
  int w = canvas.width(), h = canvas.height();
  if (w <= 0 || h <= 0 || !canvas.getBuffer()) return false;
  if (dpShadow && (w != dpW || h != dpH)) dpFree();      // orientasi berubah
  if (!dpShadow) {
    dpShadow = (uint8_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dpShadow) return false;
    dpW = w; dpH = h; dpValid = false;
  }
  return true;
}

// Salin baris canvas -> shadow (dipanggil setelah baris itu BENAR-BENAR terkirim ke layar).
void dpSyncRows(int y, int h) {
  if (!dpShadow || !dpValid) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > dpH) h = dpH - y;
  if (h <= 0) return;
  const size_t rb = (size_t)dpW * 2;
  memcpy(dpShadow + (size_t)y * rb, (const uint8_t*)canvas.getBuffer() + (size_t)y * rb, rb * h);
}

// Layar == canvas penuh (mis. setelah transisi selesai).
void dpSyncAll() {
  if (!dpEnsure()) return;
  memcpy(dpShadow, canvas.getBuffer(), (size_t)dpW * dpH * 2);
  dpValid = true; dpLastFullMs = millis();
}

static void dpFullPush() {
  canvas.pushSprite(0, 0);
  if (dpEnsure()) { memcpy(dpShadow, canvas.getBuffer(), (size_t)dpW * dpH * 2); dpValid = true; }
  dpLastFullMs = millis();
  rcFrameMark();
}

// true kalau baris beda; x0/x1 = piksel pertama/terakhir yg beda.
static inline bool dpRowDiff(const uint8_t* a, const uint8_t* b, int words, int& x0, int& x1) {
  const uint32_t* A = (const uint32_t*)a;
  const uint32_t* B = (const uint32_t*)b;
  int i = 0;
  while (i < words && A[i] == B[i]) i++;
  if (i == words) return false;
  int j = words - 1;
  while (A[j] == B[j]) j--;
  x0 = i * 2; x1 = j * 2 + 1;
  return true;
}

struct DpRect { int x0, y0, x1, y1; };

void dpSmartPush() {
  if (!dpEnsure()) { canvas.pushSprite(0, 0); rcFrameMark(); return; }
  const int rowB = dpW * 2;
  if (!dpValid || (rowB & 3)) { dpFullPush(); return; }

  const uint8_t* cur = (const uint8_t*)canvas.getBuffer();
  DpRect rc[DP_MAX_RECTS];
  int n = 0;
  for (int by = 0; by < dpH; by += DP_BAND_ROWS) {
    const int yEnd = (by + DP_BAND_ROWS < dpH) ? by + DP_BAND_ROWS : dpH;
    int fy = -1, ly = -1, fx = dpW, lx = -1;
    for (int y = by; y < yEnd; y++) {
      int x0, x1;
      if (dpRowDiff(cur + (size_t)y * rowB, dpShadow + (size_t)y * rowB, rowB / 4, x0, x1)) {
        if (fy < 0) fy = y;
        ly = y;
        if (x0 < fx) fx = x0;
        if (x1 > lx) lx = x1;
      }
    }
    if (fy >= 0) {
      if (n >= DP_MAX_RECTS) { dpFullPush(); return; }
      rc[n++] = { fx, fy, lx, ly };
    }
  }

  if (n == 0) {                                   // tidak ada yg berubah -> TIDAK kirim apa-apa
    if (millis() - dpLastFullMs > DP_IDLE_REFRESH_MS) dpFullPush();
    return;
  }

  // gabung kotak yg bertetangga kalau hasil gabungan tidak jauh lebih besar
  bool merged = true;
  while (merged && n > 1) {
    merged = false;
    for (int i = 0; i + 1 < n; i++) {
      DpRect &a = rc[i], &b = rc[i + 1];
      if (b.y0 - a.y1 > 8) continue;
      DpRect u = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0, a.x1 > b.x1 ? a.x1 : b.x1, b.y1 > a.y1 ? b.y1 : a.y1 };
      long au = (long)(u.x1 - u.x0 + 1) * (u.y1 - u.y0 + 1);
      long as = (long)(a.x1 - a.x0 + 1) * (a.y1 - a.y0 + 1) + (long)(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1);
      if (au * 100 <= as * 135 + 200000) {        // <=35% lebih besar (+ toleransi kotak kecil)
        a = u;
        for (int k = i + 1; k + 1 < n; k++) rc[k] = rc[k + 1];
        n--; merged = true; break;
      }
    }
  }

  long total = 0;
  for (int i = 0; i < n; i++) total += (long)(rc[i].x1 - rc[i].x0 + 1) * (rc[i].y1 - rc[i].y0 + 1);
  if (total * 100 >= (long)dpW * dpH * DP_FULL_PCT) { dpFullPush(); return; }

  display.startWrite();
  for (int i = 0; i < n; i++) {
    const int w = rc[i].x1 - rc[i].x0 + 1, h = rc[i].y1 - rc[i].y0 + 1;
    display.setClipRect(rc[i].x0, rc[i].y0, w, h);
    canvas.pushSprite(0, 0);
    display.clearClipRect();
  }
  display.endWrite();
  for (int i = 0; i < n; i++) {
    const int bytes = (rc[i].x1 - rc[i].x0 + 1) * 2;
    for (int y = rc[i].y0; y <= rc[i].y1; y++)
      memcpy(dpShadow + (size_t)y * rowB + rc[i].x0 * 2, cur + (size_t)y * rowB + rc[i].x0 * 2, bytes);
  }
  rcFrameMark();
}
