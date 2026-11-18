#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_shot_thumb_strip.py -- transisi thumbnail screenshot: pendekatan "kartu hasil bake + strip".

Kenapa patch V2 (apply_shot_thumb_smooth.py) belum menjamin mulus:
  Kurva dan durasi sudah bagus, tapi TIAP frame animasi masih berarti
     render ulang SELURUH UI  +  push 320x240 penuh lewat SPI  +  gambar thumbnail
  per-piksel. Kalau UI di belakang berat (Home dgn wallpaper/widget, app yg
  berat), satu frame bisa 40-80 ms. Thumbnail bergeser 6-9 px per frame yg
  ideal, jadi loncat 25-40 px per frame = terlihat patah. Easing tidak bisa
  memperbaiki frame yang lambat.

Pendekatan baru (frame animasi TIDAK lagi bergantung pada UI di belakang):
  1. BAKE sekali saat screenshot diambil: thumbnail + cincin + garis gelap +
     sudut anti-alias digambar SEKALI ke sprite sementara (di atas hitam dan di
     atas putih -> alpha per piksel), disimpan sebagai kartu premultiplied.
     Hanya 4 sudut (11x11 px) yang punya alpha; sisanya buram penuh.
  2. Bayangan = daftar kompak piksel (mx,my,kekuatan) dihitung sekali.
  3. LATAR BERSIH di bawah jalur thumbnail disalin dari canvas (tanpa thumbnail)
     tiap kali frame penuh digambar.
  4. Frame animasi = salin latar -> terapkan bayangan (±4.500 piksel) -> memcpy
     baris kartu (+ blend 484 piksel sudut) -> kirim STRIP kecil (±160x118)
     ke layar. Tidak ada render UI, tidak ada push penuh. Biaya per frame
     tetap, sama di layar mana pun.
  5. Waktu tetap berbasis millis() (posisi dihitung dari waktu nyata), jadi kalau
     ada frame yang telat, thumbnail tetap di posisi yang benar.
  6. Selama animasi CPU di-boost (uiBoostKick) seperti animasi UI lain.
  7. Fallback otomatis ke jalur lama (frame penuh) kalau: kamera/video/NES/Doom,
     terkunci, menu daya, banner telepon, layar/overlay berubah sejak latar
     diambil, atau memori sprite gagal dialokasi.
  8. Jalur frame penuh memakai fungsi compose YANG SAMA -> hasil piksel identik.

File yang diubah: nyxextras_renphone.ino, phone.ino (4 titik kecil).
Jalankan dari root repo. Idempotent.
"""
import sys, os

MARK = "NYX_TH_STRIP_V3"
NYX = "nyxextras_renphone.ino"
PH = "phone.ino"
for f in (NYX, PH):
    if not os.path.exists(f):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % f)


def load(p):
    s = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    if crlf:
        s = s.replace("\r\n", "\n")
    return s, crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(s)


# ---------------------------------------------------------------------------
# BLOK A: pengganti dari "#define NYX_TH_MIN_W" sampai sebelum nyxShotThumbTouch
# ---------------------------------------------------------------------------
BLOCK_A = r'''#define NYX_TH_MIN_W    88          // lebar thumbnail = ±30% lebar layar, dijepit 88..124 px
#define NYX_TH_MAX_W    124
#define NYX_TH_BORDER   3           // 2 px cincin terang + 1 px garis gelap di antara cincin & gambar
#define NYX_TH_RADIUS   11          // radius luar bingkai
#define NYX_TH_IN_MS    420         // NYX_TH_STRIP_V3
#define NYX_TH_HOLD_MS  3800
#define NYX_TH_OUT_MS   320
#define NYX_TH_SH_SP    9           // bayangan memudar sejauh ini (px)
#define NYX_TH_SH_OY    4           // bayangan turun sekian px
#define NYX_TH_REST_X   12          // posisi x gambar saat diam

void uiBoostKick();

static bool nyxThOn = false;
static uint32_t nyxThT0 = 0, nyxThOutT0 = 0;
static char nyxThPath[64] = "";
static bool nyxThDown = false, nyxThDrag = false;
static int  nyxThX0 = 0;
static int  nyxThW = 0, nyxThH = 0;            // ukuran GAMBAR (tanpa bingkai)

// ---- kartu hasil bake: urutan byte = urutan buffer sprite (swap565), premultiplied ----
static uint16_t* nyxThCard   = nullptr;        // fw*fh
static uint8_t*  nyxThCardA  = nullptr;        // fw*fh alpha (255 = buram)
static uint8_t*  nyxThRowLo  = nullptr;        // per baris: piksel [0,lo) di kiri tidak buram penuh
static uint8_t*  nyxThRowHi  = nullptr;        // per baris: piksel [hi,fw) di kanan tidak buram penuh
static uint8_t*  nyxThShList = nullptr;        // bayangan: 3 byte per entri (mx, my, kekuatan)
static int       nyxThShN    = 0;
static int       nyxThFw = 0, nyxThFh = 0;     // ukuran bingkai
static int       nyxThFy = 0;                  // y layar tepi atas bingkai (tetap)
// ---- jalur (band) layar tempat thumbnail bergerak ----
static LGFX_Sprite nyxThBg(&display);          // latar bersih di jalur (salinan canvas, TANPA thumbnail)
static LGFX_Sprite nyxThStrip(&display);       // buffer kerja: latar + bayangan + kartu -> ke layar
static bool nyxThSprOk = false;
static int  nyxThBx = 0, nyxThBy = 0, nyxThBw = 0, nyxThBh = 0;
static int  nyxThScrW = 0, nyxThScrH = 0;
// ---- status jalur cepat ----
static bool     nyxThBgValid = false;          // latar sah = salinan canvas dari frame penuh terakhir
static bool     nyxThDispVis = false;          // kartu sedang terlihat di layar
static bool     nyxThFastWant = false;         // tick minta 1 frame cepat
static uint32_t nyxThFullN = 0, nyxThSeenFullN = 0;
static uint32_t nyxThSig = 0;                  // tanda tangan layar saat latar diambil

static void* nyxThAlloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return p;
}
static inline uint16_t nyxSw16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static void nyxThFree() {
  if (nyxThCard)   { free(nyxThCard);   nyxThCard = nullptr; }
  if (nyxThCardA)  { free(nyxThCardA);  nyxThCardA = nullptr; }
  if (nyxThRowLo)  { free(nyxThRowLo);  nyxThRowLo = nullptr; }
  if (nyxThRowHi)  { free(nyxThRowHi);  nyxThRowHi = nullptr; }
  if (nyxThShList) { free(nyxThShList); nyxThShList = nullptr; }
  if (nyxThSprOk)  { nyxThBg.deleteSprite(); nyxThStrip.deleteSprite(); nyxThSprOk = false; }
  nyxThShN = 0; nyxThFw = 0; nyxThFh = 0;
  nyxThBgValid = false; nyxThDispVis = false; nyxThFastWant = false;
  nyxThOn = false; nyxThDown = false; nyxThDrag = false; nyxThOutT0 = 0;
}

void nyxShotThumbHide() { if (nyxThOn || nyxThCard) { nyxThFree(); needRedraw = true; } }
bool nyxShotThumbActive() { return nyxThOn && nyxThCard && nyxThSprOk; }

// ease-in-out kosinus: awal & akhir lembut, gerak tengah merata
static float nyxEaseIO(float k) { if (k < 0) k = 0; if (k > 1) k = 1; return 0.5f - 0.5f * cosf(3.14159265f * k); }

// posisi x gambar saat ini (animasi masuk / keluar) -- dihitung dari WAKTU NYATA
static int nyxThCurX() {
  const int restX = NYX_TH_REST_X;
  uint32_t now = millis();
  if (nyxThOutT0) {
    float k = nyxEaseIO((float)(now - nyxThOutT0) / (float)NYX_TH_OUT_MS);
    return (int)lroundf((float)restX - (float)(nyxThW + restX + 12) * k);
  }
  float k = nyxEaseIO((float)(now - nyxThT0) / (float)NYX_TH_IN_MS);
  return (int)lroundf((float)(-(nyxThW + 12)) + (float)(restX + nyxThW + 12) * k);
}
static int nyxThCurY() { return SCR_H - BACK_H - nyxThH - 18; }

// jarak bertanda (px) dari titik ke persegi membulat: <= 0 di dalam
static float nyxSdfRR(float px, float py, float cx, float cy, float hw, float hh, float r) {
  float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = (qx > qy ? qx : qy); if (inside > 0) inside = 0;
  return sqrtf(ox * ox + oy * oy) + inside - r;
}

// gambar kartu (cincin + garis gelap + gambar dgn sudut anti-alias) ke sprite g di (0,0)
static void nyxThDrawCardInto(LGFX_Sprite& g, const uint16_t* img, int tw, int th) {
  const int bw = NYX_TH_BORDER, ro = NYX_TH_RADIUS, ri = ro - bw;
  int fw = tw + bw * 2, fh = th + bw * 2;
  const uint16_t hair = 0x1082;
  iosRR(g, 0, 0, fw, fh, ro, blend565(T().text, T().bg, 22));
  iosRR(g, 2, 2, fw - 4, fh - 4, ro - 2, hair);
  for (int j = 0; j < th; j++) {
    bool cornerRow = (j < ri) || (j >= th - ri);
    const uint16_t* row = img + (size_t)j * tw;
    for (int i = 0; i < tw; i++) {
      uint16_t c = row[i];
      if (cornerRow && ((i < ri) || (i >= tw - ri))) {
        float ddx = fmaxf(fmaxf((float)ri - (i + 0.5f), (i + 0.5f) - (float)(tw - ri)), 0.0f);
        float ddy = fmaxf(fmaxf((float)ri - (j + 0.5f), (j + 0.5f) - (float)(th - ri)), 0.0f);
        if (ddx > 0.0f && ddy > 0.0f) {
          float d = sqrtf(ddx * ddx + ddy * ddy) - (float)ri;
          if (d > 0.5f) continue;
          if (d > -0.5f) c = blend565(hair, c, (uint8_t)((0.5f - d) * 255.0f));
        }
      }
      g.drawPixel(bw + i, bw + j, c);
    }
  }
}

// BAKE (sekali per screenshot). false = gagal (memori) -> pemanggil memanggil nyxThFree()
static bool nyxThBake(const uint16_t* img) {
  const int bd = NYX_TH_BORDER, ro = NYX_TH_RADIUS, sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  int fw = nyxThW + bd * 2, fh = nyxThH + bd * 2;
  size_t n = (size_t)fw * fh;
  if (fw > 250 || fh > 250) return false;

  // --- 1) kartu premultiplied + alpha: gambar di atas hitam dan putih ---
  LGFX_Sprite t(&display);
  t.setColorDepth(16); t.setPsram(true);
  if (!t.createSprite(fw, fh)) return false;
  uint16_t* pb = (uint16_t*)nyxThAlloc(n * 2);
  nyxThCard  = (uint16_t*)nyxThAlloc(n * 2);
  nyxThCardA = (uint8_t*)nyxThAlloc(n);
  nyxThRowLo = (uint8_t*)nyxThAlloc((size_t)fh);
  nyxThRowHi = (uint8_t*)nyxThAlloc((size_t)fh);
  if (!pb || !nyxThCard || !nyxThCardA || !nyxThRowLo || !nyxThRowHi) {
    if (pb) free(pb);
    t.deleteSprite();
    return false;
  }
  t.fillSprite(0x0000);
  nyxThDrawCardInto(t, img, nyxThW, nyxThH);
  for (int j = 0; j < fh; j++) for (int i = 0; i < fw; i++) pb[(size_t)j * fw + i] = (uint16_t)t.readPixel(i, j);
  t.fillSprite(0xFFFF);
  nyxThDrawCardInto(t, img, nyxThW, nyxThH);
  for (int j = 0; j < fh; j++) {
    for (int i = 0; i < fw; i++) {
      size_t k = (size_t)j * fw + i;
      uint16_t pw = (uint16_t)t.readPixel(i, j);
      int dg = (int)((pw >> 5) & 63) - (int)((pb[k] >> 5) & 63);
      if (dg < 0) dg = 0;
      uint8_t a;
      if (dg == 0) a = 255;
      else { int v = 255 - (dg * 255) / 63; a = (uint8_t)(v < 0 ? 0 : v); }
      nyxThCard[k]  = nyxSw16(a ? pb[k] : (uint16_t)0);
      nyxThCardA[k] = a;
    }
  }
  free(pb);
  t.deleteSprite();
  for (int j = 0; j < fh; j++) {                       // bagian buram penuh per baris -> memcpy saat compose
    const uint8_t* ar = nyxThCardA + (size_t)j * fw;
    int lo = 0; while (lo < fw && ar[lo] != 255) lo++;
    int hi = fw; while (hi > lo && ar[hi - 1] != 255) hi--;
    for (int i = lo; i < hi; i++) if (ar[i] != 255) { lo = hi = fw; break; }   // tak terduga: semua lewat jalur blend
    nyxThRowLo[j] = (uint8_t)lo; nyxThRowHi[j] = (uint8_t)hi;
  }
  nyxThFw = fw; nyxThFh = fh;

  // --- 2) bayangan: daftar piksel tak-nol relatif terhadap kartu ---
  int mw = fw + sp * 2, mh = fh + sp * 2;
  if (mw > 255 || mh > 255) return false;
  uint8_t* m = (uint8_t*)nyxThAlloc((size_t)mw * mh);
  if (!m) return false;
  float cx = fw * 0.5f, cy = fh * 0.5f, hw = cx, hh = cy;
  int cnt = 0;
  for (int my = 0; my < mh; my++) {
    for (int mx = 0; mx < mw; mx++) {
      float fp = (float)(mx - sp) + 0.5f, fq = (float)(my - sp + soy) + 0.5f;
      uint8_t v = 0;
      if (nyxSdfRR(fp, fq, cx, cy, hw, hh, (float)ro) > 0.0f) {
        float d = nyxSdfRR(fp, fq, cx, cy + (float)soy, hw, hh, (float)ro);
        if (d < (float)sp) {
          float kk = d <= 0 ? 1.0f : 1.0f - d / (float)sp;
          v = (uint8_t)(kk * kk * 255.0f + 0.5f);
        }
      }
      m[(size_t)my * mw + mx] = v;
      if (v) cnt++;
    }
  }
  nyxThShList = (uint8_t*)nyxThAlloc((size_t)(cnt > 0 ? cnt : 1) * 3);
  if (!nyxThShList) { free(m); return false; }
  int w = 0;
  for (int my = 0; my < mh; my++) for (int mx = 0; mx < mw; mx++) {
    uint8_t v = m[(size_t)my * mw + mx];
    if (!v) continue;
    nyxThShList[w * 3 + 0] = (uint8_t)mx; nyxThShList[w * 3 + 1] = (uint8_t)my; nyxThShList[w * 3 + 2] = v; w++;
  }
  nyxThShN = w;
  free(m);

  // --- 3) jalur layar + sprite latar/kerja ---
  int fy = nyxThCurY() - bd;
  int by = fy - sp, bh = fh + sp * 2 + soy;
  if (by < 0) { bh += by; by = 0; }
  if (by + bh > SCR_H) bh = SCR_H - by;
  int bw = (NYX_TH_REST_X - bd) + fw + sp;
  if (bw > SCR_W) bw = SCR_W;
  if (bw <= 0 || bh <= 0) return false;
  nyxThFy = fy; nyxThBx = 0; nyxThBy = by; nyxThBw = bw; nyxThBh = bh;
  nyxThScrW = SCR_W; nyxThScrH = SCR_H;
  nyxThBg.setColorDepth(16);    nyxThBg.setPsram(true);
  nyxThStrip.setColorDepth(16); nyxThStrip.setPsram(true);
  if (!nyxThBg.createSprite(bw, bh)) return false;
  if (!nyxThStrip.createSprite(bw, bh)) { nyxThBg.deleteSprite(); return false; }
  nyxThSprOk = true;
  return true;
}

// dipanggil dari screenshotSaveNow() setelah BMP tersimpan
void nyxShotThumbCapture(const char* path) {
  nyxThFree();
  int w = canvas.width(), h = canvas.height();
  if (w <= 0 || h <= 0) return;
  int tw = SCR_W * 30 / 100;
  if (tw < NYX_TH_MIN_W) tw = NYX_TH_MIN_W;
  if (tw > NYX_TH_MAX_W) tw = NYX_TH_MAX_W;
  int th = (tw * h) / w;
  if (th < 40) th = 40;
  if (th > 110) th = 110;
  uint16_t* img = (uint16_t*)nyxThAlloc((size_t)tw * th * 2);
  if (!img) return;
  for (int j = 0; j < th; j++) {
    int y0 = (j * h) / th, y1 = ((j + 1) * h) / th - 1; if (y1 < y0) y1 = y0;
    for (int i = 0; i < tw; i++) {
      int x0 = (i * w) / tw, x1 = ((i + 1) * w) / tw - 1; if (x1 < x0) x1 = x0;
      uint32_t r = 0, g = 0, b = 0; int cnt = 0;
      for (int sy = 0; sy < 3; sy++) for (int sx = 0; sx < 3; sx++) {            // rata-rata 3x3 titik -> lebih halus
        uint16_t c = (uint16_t)canvas.readPixel(x0 + ((x1 - x0) * sx) / 2, y0 + ((y1 - y0) * sy) / 2);
        r += (c >> 11) & 0x1F; g += (c >> 5) & 0x3F; b += c & 0x1F; cnt++;
      }
      r /= cnt; g /= cnt; b /= cnt;
      img[j * tw + i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
  nyxThW = tw; nyxThH = th;
  bool ok = nyxThBake(img);
  free(img);
  if (!ok) { nyxThFree(); return; }                 // gagal: pemanggil menampilkan toast biasa
  strncpy(nyxThPath, path ? path : "", sizeof(nyxThPath) - 1);
  nyxThPath[sizeof(nyxThPath) - 1] = 0;
  nyxThOn = true; nyxThT0 = millis(); nyxThOutT0 = 0;
  vibSuccess();
}

// tanda tangan layar: kalau berubah sejak latar diambil, latar tidak boleh dipakai lagi
static uint32_t nyxThScreenSig() {
  return (uint32_t)curScreen()
       | ((uint32_t)controlCenterOpen << 8) | ((uint32_t)notifShadeOpen << 9) | ((uint32_t)appSwitcherOpen << 10)
       | ((uint32_t)homeAodActive << 11) | ((uint32_t)diMenuOpen << 12) | ((uint32_t)kbVisible << 13);
}

// salin latar bersih (canvas SEBELUM thumbnail digambar) ke sprite latar
static void nyxThCaptureBg(LGFX_Sprite& s) {
  s.pushSprite(&nyxThBg, -nyxThBx, -nyxThBy);
  nyxThSig = nyxThScreenSig();
}

static inline void nyxThBlendPx(uint16_t* d, uint16_t cardRaw, uint8_t a) {
  if (!a) return;
  uint16_t c = nyxSw16(cardRaw), b = nyxSw16(*d);
  uint32_t ia = 255u - a;
  uint32_t r  = ((c >> 11) & 31) + ((((b >> 11) & 31) * ia + 127u) / 255u);
  uint32_t g  = ((c >> 5) & 63)  + ((((b >> 5) & 63)  * ia + 127u) / 255u);
  uint32_t bl = (c & 31)         + (((b & 31)         * ia + 127u) / 255u);
  if (r > 31) r = 31; if (g > 63) g = 63; if (bl > 31) bl = 31;
  *d = nyxSw16((uint16_t)((r << 11) | (g << 5) | bl));
}

// SATU-SATUNYA jalur gambar: latar -> bayangan -> kartu, hasilnya di nyxThStrip.
// fl = x tepi kiri bingkai dalam koordinat jalur (boleh negatif / melewati kanan).
static void nyxThCompose(int fl) {
  const int bw = nyxThBw, bh = nyxThBh, sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  const int ft = nyxThFy - nyxThBy;
  nyxThBg.pushSprite(&nyxThStrip, 0, 0);
  uint16_t* buf = (uint16_t*)nyxThStrip.getBuffer();
  if (!buf) return;
  const int stride = nyxThStrip.width();

  // 1) bayangan (latar terang: gelap; latar gelap: cahaya halus warna aksen)
  if (nyxThShList) {
    const uint16_t acc = (uint16_t)T().accent;
    const int ox = fl - sp, oy = ft - sp + soy;
    const uint8_t* e = nyxThShList;
    for (int i = 0; i < nyxThShN; i++, e += 3) {
      int px = ox + e[0], py = oy + e[1];
      if ((unsigned)px >= (unsigned)bw || (unsigned)py >= (unsigned)bh) continue;
      uint32_t m = e[2];
      uint16_t* d = buf + (size_t)py * stride + px;
      uint16_t bgp = nyxSw16(*d);
      uint32_t lum = ((((bgp >> 11) & 31) << 3) * 77 + (((bgp >> 5) & 63) << 2) * 150 + ((bgp & 31) << 3) * 29) >> 8;
      uint32_t il = 255u - lum;
      uint32_t aDark = (150u * m * lum) / 65025u;
      uint32_t aGlow = (110u * m * il * il) / 16581375u;
      if (aDark < 3 && aGlow < 3) continue;
      uint16_t o = bgp;
      if (aDark >= 3) o = blend565(o, 0x0000, (uint8_t)aDark);
      if (aGlow >= 3) o = blend565(o, acc, (uint8_t)aGlow);
      *d = nyxSw16(o);
    }
  }

  // 2) kartu: bagian buram = memcpy per baris, hanya tepi sudut yang di-blend
  const int fw = nyxThFw;
  for (int j = 0; j < nyxThFh; j++) {
    int py = ft + j;
    if ((unsigned)py >= (unsigned)bh) continue;
    uint16_t* drow = buf + (size_t)py * stride;
    const uint16_t* crow = nyxThCard + (size_t)j * fw;
    const uint8_t* arow = nyxThCardA + (size_t)j * fw;
    int lo = nyxThRowLo[j], hi = nyxThRowHi[j];
    if (hi > lo) {
      int x0 = fl + lo, x1 = fl + hi;
      if (x0 < 0) x0 = 0;
      if (x1 > bw) x1 = bw;
      if (x1 > x0) memcpy(drow + x0, crow + (x0 - fl), (size_t)(x1 - x0) * 2);
    }
    for (int i = 0; i < lo; i++) { int px = fl + i; if ((unsigned)px < (unsigned)bw) nyxThBlendPx(drow + px, crow[i], arow[i]); }
    for (int i = hi; i < fw; i++) { int px = fl + i; if ((unsigned)px < (unsigned)bw) nyxThBlendPx(drow + px, crow[i], arow[i]); }
  }
}

// ---- jalur FRAME PENUH (dipanggil renderCurrentFrame): hasil piksel identik dgn jalur cepat ----
void drawShotThumb(LGFX_Sprite& s) {
  if (!nyxShotThumbActive()) return;
  nyxThFullN++;
  nyxThCaptureBg(s);
  nyxThBgValid = (&s == &canvas) && !homeBandMode;   // mode pita: canvas di luar pita belum tentu bersih
  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;
  if (fl + nyxThFw + NYX_TH_SH_SP <= 0) { nyxThDispVis = false; return; }   // masih di luar layar
  nyxThCompose(fl);
  nyxThStrip.pushSprite(&s, nyxThBx, nyxThBy);       // ke canvas (patuh clip rect)
  nyxThDispVis = true;
}

// boleh pakai jalur cepat (kirim strip langsung ke layar tanpa render UI)?
static bool nyxThFastOk() {
  if (!nyxThBgValid || !nyxThSprOk || !nyxThCard) return false;
  if (SCR_W != nyxThScrW || SCR_H != nyxThScrH) return false;
  if (locked || powerMenuOpen || callBannerActive() || gameModeActive) return false;
  Screen sc = curScreen();
  if (sc == SCR_CAMERA || sc == SCR_MJPEG || sc == SCR_NES || sc == SCR_DOOM || sc == SCR_PPU || sc == SCR_CALL) return false;
  if (nyxThScreenSig() != nyxThSig) return false;
  return true;
}

// dipanggil dari loop() (akhir putaran): gambar 1 frame animasi hanya sebagai strip
void nyxShotThumbFastFrame() {
  if (!nyxThFastWant) return;
  nyxThFastWant = false;
  if (!nyxShotThumbActive()) return;
  if (nyxThFullN != nyxThSeenFullN) { nyxThSeenFullN = nyxThFullN; return; }   // frame penuh baru saja menggambarnya
  if (!nyxThFastOk()) { needRedraw = true; return; }                            // fallback: jalur lama
  uiBoostKick();
  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;
  bool vis = (fl + nyxThFw + NYX_TH_SH_SP > 0);
  if (!vis && !nyxThDispVis) return;                 // belum muncul & layar sudah bersih: tak ada yang dikirim
  nyxThCompose(fl);
  nyxThStrip.pushSprite(nyxThBx, nyxThBy);           // hanya strip ±160x118 lewat SPI
  nyxThDispVis = vis;
}

// true selama thumbnail sedang bergerak (masuk / keluar)
bool nyxShotThumbAnimating() {
  if (!nyxShotThumbActive()) return false;
  if (nyxThOutT0) return true;
  return (millis() - nyxThT0) < (uint32_t)(NYX_TH_IN_MS + 80);
}

'''

# ---------------------------------------------------------------------------
# BLOK B: pengganti nyxThumbTick
# ---------------------------------------------------------------------------
BLOCK_B = r'''static void nyxThAnimStep() {
  if (nyxThFastOk()) nyxThFastWant = true;     // frame animasi = strip kecil saja
  else needRedraw = true;                      // belum ada latar sah / kondisi khusus -> frame penuh (jalur lama)
}

static void nyxThumbTick() {
  if (!nyxThCard) return;
  if (locked) { nyxThFree(); return; }
  uint32_t now = millis();
  if (SCR_W != nyxThScrW || SCR_H != nyxThScrH) { nyxThFree(); needRedraw = true; return; }   // rotasi berubah
  if (nyxThOutT0) {
    if (now - nyxThOutT0 >= NYX_TH_OUT_MS) { nyxThFree(); needRedraw = true; }
    else nyxThAnimStep();
    return;
  }
  if (now - nyxThT0 < NYX_TH_IN_MS + 40) nyxThAnimStep();
  else if (!nyxThDown && now - nyxThT0 > NYX_TH_IN_MS + NYX_TH_HOLD_MS) { nyxThOutT0 = now; nyxThAnimStep(); }
}

'''

s, crlf = load(NYX)
if MARK in s:
    print("[skip] %s sudah terpasang di %s" % (MARK, NYX))
else:
    a0 = s.find("#define NYX_TH_MIN_W")
    a1 = s.find("// true = sentuhan ini milik thumbnail")
    if a0 < 0 or a1 < 0 or a1 < a0:
        sys.exit("ERROR: blok thumbnail di %s tidak ditemukan" % NYX)
    s = s[:a0] + BLOCK_A + s[a1:]
    b0 = s.find("static void nyxThumbTick() {")
    b1 = s.find("// =====================================================================\n//  5. SKELETON LOADING")
    if b0 < 0 or b1 < 0 or b1 < b0:
        sys.exit("ERROR: nyxThumbTick di %s tidak ditemukan" % NYX)
    s = s[:b0] + BLOCK_B + s[b1:]
    save(NYX, s, crlf)
    print("[ok] %s: thumbnail -> kartu bake + strip" % NYX)

# ---------------------------------------------------------------------------
# phone.ino: 4 titik kecil
# ---------------------------------------------------------------------------
p, pcrlf = load(PH)
if MARK in p:
    print("[skip] %s sudah terpasang di %s" % (MARK, PH))
else:
    def rep(old, new, what):
        global p
        n = p.count(old)
        if n != 1:
            sys.exit("ERROR: %s ditemukan %d kali di %s (harus 1)" % (what, n, PH))
        p = p.replace(old, new)

    rep("void drawShotThumb(LGFX_Sprite&); bool nyxShotThumbTouch(bool,bool,int,int);",
        "void drawShotThumb(LGFX_Sprite&); bool nyxShotThumbTouch(bool,bool,int,int);\n"
        "bool nyxShotThumbAnimating(); void nyxShotThumbFastFrame();   // " + MARK,
        "prototipe thumbnail")
    # mode pita Home jangan dipakai selagi thumbnail bergerak (bagian kartu di luar pita tidak ikut ter-push)
    rep("if(bandCand && !redrawBefore && !sbChanged && homeBandReady && plainNow){",
        "if(bandCand && !redrawBefore && !sbChanged && homeBandReady && plainNow && !nyxShotThumbAnimating()){",
        "syarat mode pita Home")
    # jalur sentuh thumbnail (return awal): frame animasi tetap digambar
    rep("if(callBannerTouch(touched,newT,tx,ty) || nyxShotThumbTouch(touched,newT,tx,ty)){\n"
        "    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }\n"
        "    wasTouched=touched; delay(1); return;",
        "if(callBannerTouch(touched,newT,tx,ty) || nyxShotThumbTouch(touched,newT,tx,ty)){\n"
        "    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }\n"
        "    nyxShotThumbFastFrame();\n"
        "    wasTouched=touched; delay(1); return;",
        "jalur sentuh thumbnail")
    # akhir loop()
    rep("  wasTouched=touched;\n  // v95: SEMUA delay(8)",
        "  nyxShotThumbFastFrame();   // " + MARK + ": frame animasi thumbnail = strip kecil, tanpa render UI\n"
        "  wasTouched=touched;\n  // v95: SEMUA delay(8)",
        "akhir loop()")
    save(PH, p, pcrlf)
    print("[ok] %s: 4 titik dipasang" % PH)
