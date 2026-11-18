#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_shot_thumb_smooth.py -- transisi thumbnail screenshot jadi lebih halus.

Masalah: thumbnail masuk dari kiri ke kanan terasa patah-patah / "loncat".
Dua penyebab:
  1. Kurva ease-out kubik paling cepat di awal. Saat frame rate turun (UI digambar
     ulang penuh tiap frame), frame pertama langsung melompat ±59% jaraknya.
  2. Tiap frame menghitung bayangan lembut dengan 2x jarak-bertanda + sqrt untuk
     ±14.000 piksel, jadi frame animasi lambat.

Perbaikan (hanya nyxextras_renphone.ino):
  - Kurva ease-in-out (kosinus): awal dan akhir lembut, gerak merata.
  - Durasi masuk 260 -> 420 ms, keluar 220 -> 320 ms.
  - Posisi dibulatkan (bukan dipotong), tidak lagi goyang 1 px.
  - Bayangan dihitung SEKALI saat screenshot diambil (mask 8-bit di PSRAM); tiap
    frame tinggal baca mask. Tampilannya sama persis.
  - Gambar thumbnail: matematika sudut membulat hanya dipakai di 4 sudut.
  - Frame yang masih sepenuhnya di luar layar tidak digambar.

Jalankan dari root repo. Idempotent.
"""
import sys, os, re

P = "nyxextras_renphone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

MARK = "NYX_TH_SMOOTH_V2"
if MARK in s:
    print("[skip] %s sudah terpasang" % MARK)
    sys.exit(0)

def rep(old, new, what):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: %s ditemukan %d kali (harus 1). Kode thumbnail sudah berubah?" % (what, n))
    s = s.replace(old, new)

# 1) durasi
rep("#define NYX_TH_IN_MS    260\n#define NYX_TH_HOLD_MS  3800\n#define NYX_TH_OUT_MS   220\n",
    "#define NYX_TH_IN_MS    420          // " + MARK + " (dulu 260)\n"
    "#define NYX_TH_HOLD_MS  3800\n"
    "#define NYX_TH_OUT_MS   320          // (dulu 220)\n"
    "#define NYX_TH_SH_SP    9            // bayangan memudar sejauh ini (px)\n"
    "#define NYX_TH_SH_OY    4            // bayangan turun sekian px\n",
    "konstanta durasi")

# 2) mask bayangan + pembebasan
rep("static int  nyxThX0 = 0;\n\nstatic void nyxThFree() {\n  if (nyxThumb) { free(nyxThumb); nyxThumb = nullptr; }\n",
    "static int  nyxThX0 = 0;\n"
    "static uint8_t* nyxThShMask = nullptr;   // kekuatan bayangan per piksel (0..255), dihitung sekali per screenshot\n"
    "static int  nyxThShW = 0, nyxThShH = 0;\n"
    "static void nyxThBuildShadow();\n\n"
    "static void nyxThFree() {\n"
    "  if (nyxThumb) { free(nyxThumb); nyxThumb = nullptr; }\n"
    "  if (nyxThShMask) { free(nyxThShMask); nyxThShMask = nullptr; }\n"
    "  nyxThShW = 0; nyxThShH = 0;\n",
    "nyxThFree")

# 3) bangun mask setelah ukuran thumbnail diketahui
rep("  nyxThW = tw; nyxThH = th;\n  strncpy(nyxThPath",
    "  nyxThW = tw; nyxThH = th;\n  nyxThBuildShadow();\n  strncpy(nyxThPath",
    "capture")

# 4) easing + posisi
old_cur = s[s.index("static float nyxEaseOut(float k)"):s.index("static int nyxThCurY()")]
new_cur = '''static float nyxEaseOut(float k) { if (k < 0) k = 0; if (k > 1) k = 1; float q = 1.0f - k; return 1.0f - q * q * q; }
// ease-in-out kosinus: awal & akhir lembut, gerak tengah merata (tidak "melompat" di frame pertama)
static float nyxEaseIO(float k) { if (k < 0) k = 0; if (k > 1) k = 1; return 0.5f - 0.5f * cosf(3.14159265f * k); }

// posisi x thumbnail saat ini (animasi masuk / keluar)
static int nyxThCurX() {
  const int restX = 12;
  uint32_t now = millis();
  if (nyxThOutT0) {
    float k = nyxEaseIO((float)(now - nyxThOutT0) / (float)NYX_TH_OUT_MS);
    return (int)lroundf((float)restX - (float)(nyxThW + restX + 12) * k);
  }
  float k = nyxEaseIO((float)(now - nyxThT0) / (float)NYX_TH_IN_MS);
  return (int)lroundf((float)(-(nyxThW + 12)) + (float)(restX + nyxThW + 12) * k);
}
'''
s = s.replace(old_cur, new_cur)

# 5) mask builder (setelah nyxSdfRR) + drawShotThumb baru
i0 = s.index("void drawShotThumb(LGFX_Sprite& s) {")
i1 = s.index("// true = sentuhan ini milik thumbnail")
new_draw = '''// hitung sekali: kekuatan bayangan tiap piksel relatif terhadap bingkai (0 = tanpa bayangan / tertutup bingkai)
static void nyxThBuildShadow() {
  if (nyxThShMask) { free(nyxThShMask); nyxThShMask = nullptr; }
  nyxThShW = 0; nyxThShH = 0;
  if (nyxThW <= 0 || nyxThH <= 0) return;
  const int bw = NYX_TH_BORDER, ro = NYX_TH_RADIUS, sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  int fw = nyxThW + bw * 2, fh = nyxThH + bw * 2;
  int mw = fw + sp * 2, mh = fh + sp * 2;
  uint8_t* m = (uint8_t*)heap_caps_malloc((size_t)mw * mh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!m) m = (uint8_t*)malloc((size_t)mw * mh);
  if (!m) return;                                   // tanpa mask: bayangan dilewati, thumbnail tetap tampil
  float cx = fw * 0.5f, cy = fh * 0.5f, hw = cx, hh = cy;
  for (int my = 0; my < mh; my++) {
    for (int mx = 0; mx < mw; mx++) {
      float fp = (float)(mx - sp) + 0.5f, fq = (float)(my - sp + soy) + 0.5f;
      uint8_t v = 0;
      if (nyxSdfRR(fp, fq, cx, cy, hw, hh, (float)ro) > 0.0f) {                 // di luar bingkai
        float d = nyxSdfRR(fp, fq, cx, cy + (float)soy, hw, hh, (float)ro);     // jarak ke bayangan
        if (d < (float)sp) {
          float k = d <= 0 ? 1.0f : 1.0f - d / (float)sp;
          v = (uint8_t)(k * k * 255.0f + 0.5f);
        }
      }
      m[(size_t)my * mw + mx] = v;
    }
  }
  nyxThShMask = m; nyxThShW = mw; nyxThShH = mh;
}

void drawShotThumb(LGFX_Sprite& s) {
  if (!nyxShotThumbActive()) return;
  const int bw = NYX_TH_BORDER, ro = NYX_TH_RADIUS, ri = ro - bw;
  const int sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  int x = nyxThCurX(), y = nyxThCurY();
  int fx = x - bw, fy = y - bw, fw = nyxThW + bw * 2, fh = nyxThH + bw * 2;
  if (fx + fw + sp <= 0) return;                    // masih sepenuhnya di luar layar (awal animasi)

  // 1) bayangan lembut dari mask (tanpa sqrt per frame)
  if (nyxThShMask) {
    const uint16_t acc = (uint16_t)T().accent;
    int ox = fx - sp, oy = fy - sp + soy;
    for (int my = 0; my < nyxThShH; my++) {
      int py = oy + my;
      if (py < 0 || py >= SCR_H) continue;
      const uint8_t* mrow = nyxThShMask + (size_t)my * nyxThShW;
      for (int mx = 0; mx < nyxThShW; mx++) {
        uint32_t m = mrow[mx];
        if (!m) continue;
        int px = ox + mx;
        if (px < 0 || px >= SCR_W) continue;
        uint16_t bgp = (uint16_t)s.readPixel(px, py);
        uint32_t lum = ((((bgp >> 11) & 31) << 3) * 77 + (((bgp >> 5) & 63) << 2) * 150 + ((bgp & 31) << 3) * 29) >> 8;   // 0..255
        uint32_t il = 255u - lum;
        uint32_t aDark = (150u * m * lum) / 65025u;                // latar terang: bayangan gelap
        uint32_t aGlow = (110u * m * il * il) / 16581375u;         // latar gelap: cahaya halus warna aksen
        if (aDark < 3 && aGlow < 3) continue;
        uint16_t o = bgp;
        if (aDark >= 3) o = blend565(o, 0x0000, (uint8_t)aDark);
        if (aGlow >= 3) o = blend565(o, acc, (uint8_t)aGlow);
        s.drawPixel(px, py, o);
      }
    }
  }

  // 2) bingkai berlapis: cincin terang 2 px + garis gelap 1 px (memisahkan gambar dari bingkai)
  const uint16_t hair = 0x1082;
  iosRR(s, fx, fy, fw, fh, ro, blend565(T().text, T().bg, 22));
  iosRR(s, fx + 2, fy + 2, fw - 4, fh - 4, ro - 2, hair);

  // 3) gambar; matematika sudut membulat (anti-alias) hanya dipakai di 4 sudut
  uint16_t frameCol = hair;
  for (int j = 0; j < nyxThH; j++) {
    bool cornerRow = (j < ri) || (j >= nyxThH - ri);
    const uint16_t* row = nyxThumb + (size_t)j * nyxThW;
    int yy = y + j;
    for (int i = 0; i < nyxThW; i++) {
      uint16_t c = row[i];
      if (cornerRow && ((i < ri) || (i >= nyxThW - ri))) {
        float ddx = fmaxf(fmaxf((float)ri - (i + 0.5f), (i + 0.5f) - (float)(nyxThW - ri)), 0.0f);
        float ddy = fmaxf(fmaxf((float)ri - (j + 0.5f), (j + 0.5f) - (float)(nyxThH - ri)), 0.0f);
        if (ddx > 0.0f && ddy > 0.0f) {
          float d = sqrtf(ddx * ddx + ddy * ddy) - (float)ri;
          if (d > 0.5f) continue;
          if (d > -0.5f) c = blend565(frameCol, c, (uint8_t)((0.5f - d) * 255.0f));
        }
      }
      s.drawPixel(x + i, yy, c);
    }
  }
}

'''
s = s[:i0] + new_draw + s[i1:]

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] transisi thumbnail screenshot dihaluskan di %s" % P)
