#!/usr/bin/env python3
# apply_wsense_viz_patch.py -- WSENSE_VIZ_V1
# Jalankan dari root repo:  python3 apply_wsense_viz_patch.py
#
# Visualisasi WiFi Sensing dibuat ulang. Berdiri sendiri: boleh dijalankan SEBELUM atau SESUDAH
# apply_wsense_tidy_patch.py (patch ini sudah memuat perapian tulisannya).
#
# Dulu: satu garis amplitudo mentah yang diskalakan ke simpangan terbesar, jadi selalu naik-turun liar
# walau statusnya "Kosong", dan tidak kelihatan kapan garis itu melewati ambang.
# Sekarang grafik dibagi dua:
#   1. GERAK vs AMBANG (utama): batang per 100 ms selama 15 dtk terakhir. Tinggi batang = skor gerak
#      dibagi ambang. Batang oranye = di atas ambang (gerak), batang biru = di bawah. Garis putus-putus
#      oranye = ambang (rasio 1,0). Jadi langsung kelihatan: kosong = batang rendah di bawah garis.
#   2. SINYAL WIFI (bawah): gelombang amplitudo mentah, dihaluskan 3 titik supaya tidak bergerigi.
#      Hanya tampil kalau grafik cukup tinggi (layar landscape yang pendek cuma menampilkan Gerak).
# Strip atas (judul + Multi/Rata) dan strip bawah (pesan + Sens n/5) tidak bertabrakan dengan grafik.
# Baris angka Paket / Gerak / Napas rata kiri-tengah-kanan. Bar skor tipis di tepi atas dihapus
# (sudah diganti batang).
#
# Memori: +150 byte RAM (riwayat skor 1 byte per bin). Tidak ada alokasi heap, tidak ada String per frame.
# Berkas yang diubah: firmware/phone/wifisense_renphone.ino (+ CHANGELOG.md bila ada).
# Aman dijalankan ulang (cek penanda).
import os, sys

MARK = "WSENSE_VIZ_V1"
WS = os.path.join("firmware", "phone", "wifisense_renphone.ino")
CL = "CHANGELOG.md"


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus tepat 1x). Tidak ada berkas yang diubah." % (label, n))
    return s.replace(old, new)


def sub_opt(s, old, new):
    # ganti bila ada tepat 1x; kalau sudah diganti patch perapian, lewati
    return s.replace(old, new) if s.count(old) == 1 else s


if not os.path.exists(WS):
    sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo." % WS)
w = rd(WS)
if MARK in w:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)

# 1) riwayat skor gerak / ambang
w = sub(w,
        "static int      wsMovHist    = 0;",
        "static uint8_t  wsHist[WS_GRAPH_N];                // WSENSE_VIZ_V1: riwayat skor gerak / ambang (x50, maks 255)\n"
        "static int      wsHistHead   = 0, wsHistN = 0;\n"
        "static int      wsMovHist    = 0;",
        "state")
w = sub(w,
        "wsLastMove = 0;\n  portENTER_CRITICAL(&wsMux);",
        "wsLastMove = 0;\n  wsHistHead = 0; wsHistN = 0;                           // WSENSE_VIZ_V1\n  portENTER_CRITICAL(&wsMux);",
        "reset")
w = sub(w,
        "  wsMovHist = wsMovHist * 995 / 1000 + (mv ? 10 : 0);   // peluruhan lambat ~ 30 dtk\n",
        "  wsMovHist = wsMovHist * 995 / 1000 + (mv ? 10 : 0);   // peluruhan lambat ~ 30 dtk\n"
        "  { float rr = (thr > 0) ? wsScore / thr : 0;            // WSENSE_VIZ_V1: simpan rasio skor/ambang utk grafik batang\n"
        "    int q = (int)(rr * 50.0f + 0.5f); if (q < 0) q = 0; if (q > 255) q = 255;\n"
        "    wsHist[wsHistHead] = (uint8_t)q; wsHistHead = (wsHistHead + 1) % WS_GRAPH_N; if (wsHistN < WS_GRAPH_N) wsHistN++; }\n",
        "push")
w = sub(w,
        "wsMulti = !wsMulti; wsBrK = -1; wsBrRingN = 0; wsBr = 0; wsMovHist = 0;",
        "wsMulti = !wsMulti; wsBrK = -1; wsBrRingN = 0; wsBr = 0; wsMovHist = 0; wsHistN = 0;",
        "toggle")

# 2) baris angka rata (lewati kalau patch perapian sudah memasangnya)
w = sub_opt(w,
            "  s.setCursor(SCR_W / 3 + 4, iy); s.print(b);\n",
            "  s.setCursor((SCR_W - s.textWidth(b)) / 2, iy); s.print(b);          // tengah\n")
w = sub_opt(w,
            "  s.setCursor(SCR_W * 2 / 3, iy); s.print(b);\n",
            "  s.setCursor(SCR_W - 12 - s.textWidth(b), iy); s.print(b);           // rata kanan\n")

# 3) 'Sens n/5' lama di atas tombol dibuang (pindah ke strip bawah grafik)
w = sub_opt(w,
            "  { int x, w; wsBtnRect(3, &x, &w); int x2, w2; wsBtnRect(2, &x2, &w2);   // indikator sensitivitas di tengah pasangan -/+\n"
            "    s.setTextSize(1); s.setTextColor(T().subtext);\n"
            "    snprintf(b, sizeof(b), \"Sens %d/5\", wsSens + 1);\n"
            "    int tw = s.textWidth(b); s.setCursor(x2 + (x + w - x2 - tw) / 2, wsBtnY() - 11); s.print(b); }\n",
            "  // WSENSE_VIZ_V1: indikator 'Sens n/5' ada di strip bawah grafik\n")

# 4) ganti seluruh blok grafik (dari komentar grafik sampai sebelum tombol), apa pun kondisinya sekarang
A = "  // ---- grafik 15 dtk terakhir"
Z = "  // ---- tombol ----"
if w.count(A) != 1 or w.count(Z) != 1:
    sys.exit("GAGAL: penanda blok grafik/tombol tidak ditemukan tepat 1x. Tidak ada berkas yang diubah.")
i0, i1 = w.index(A), w.index(Z)
if i1 < i0:
    sys.exit("GAGAL: urutan blok tidak terduga. Tidak ada berkas yang diubah.")

NEW = r'''  // ---- grafik 15 dtk terakhir: (1) batang GERAK vs AMBANG, (2) gelombang SINYAL (WSENSE_VIZ_V1) ----
  int gx = 8, gy = iy + 14, gw = SCR_W - 16, gh = wsGraphBottom() - gy;
  if (gh < 30) gh = 30;
  drawGlassPanel(s, gx, gy, gw, gh, 10, T().surface2, 140);
  const int HDR = 19, FTR = 17;                          // tinggi strip atas / bawah di dalam panel
  const uint16_t ORG = WS_RGB(255,159,10);
  s.setTextSize(1);
  { // strip atas: judul (kiri) + mode (kanan, bisa diketuk)
    char hl[40];
    if (wsRun && fabsf(wsAdaptMul - 1.0f) > 0.15f) snprintf(hl, sizeof(hl), "Gerak vs ambang  (Adaptif x%.1f)", (double)wsAdaptMul);
    else snprintf(hl, sizeof(hl), "Gerak vs ambang");
    s.setTextColor(T().subtext); s.setCursor(gx + 8, gy + 8); s.print(hl);
    char tb[16]; const char* tg;
    if (wsUseSub) { snprintf(tb, sizeof(tb), "Multi %d", wsNValid); tg = tb; }
    else tg = wsMulti ? "Multi.." : "Rata";
    int tw = s.textWidth(tg); s.setCursor(gx + gw - tw - 8, gy + 8); s.print(tg);
  }
  int top = gy + HDR + 3, bot = gy + gh - FTR - 3, H = bot - top;
  bool two = (H >= 90);                                  // layar pendek: hanya grafik Gerak
  int p1H = two ? (H * 58 / 100) : H;
  int pxL = gx + 8, pw = gw - 16;
  if (gh >= 70) {                                        // garis pemisah strip atas & bawah
    s.drawFastHLine(gx + 8, gy + HDR, gw - 16, T().divider);
    s.drawFastHLine(gx + 8, gy + gh - FTR, gw - 16, T().divider);
  }
  // ---- grafik 1: batang gerak / ambang ----
  {
    int pb = top + p1H;                                  // dasar batang
    if (wsHistN > 3) {
      float ymax = 2.0f;                                 // skala: ambang di tengah, naik otomatis bila ada lonjakan
      for (int i = 0; i < wsHistN; i++) {
        float r = wsHist[((wsHistHead - 1 - i) % WS_GRAPH_N + WS_GRAPH_N) % WS_GRAPH_N] / 50.0f;
        if (r * 1.1f > ymax) ymax = r * 1.1f;
      }
      if (ymax > 5.0f) ymax = 5.0f;
      const uint16_t lowC = blend565(T().surface2, T().accent, 210);
      for (int i = 0; i < wsHistN; i++) {                // i = 0 terbaru (paling kanan)
        float r = wsHist[((wsHistHead - 1 - i) % WS_GRAPH_N + WS_GRAPH_N) % WS_GRAPH_N] / 50.0f;
        int x1 = pxL + pw - (int)((long)i * pw / WS_GRAPH_N);
        int x0 = pxL + pw - (int)((long)(i + 1) * pw / WS_GRAPH_N);
        if (x1 <= x0) continue;
        int hh = (int)(r / ymax * p1H); if (hh > p1H) hh = p1H; if (hh < 1 && r > 0.0f) hh = 1;
        if (hh > 0) s.fillRect(x0, pb - hh, x1 - x0, hh, r >= 1.0f ? ORG : lowC);
      }
      int ty = pb - (int)(p1H / ymax);                   // garis ambang (rasio 1,0), putus-putus
      const uint16_t thC = blend565(T().surface2, ORG, 170);
      for (int x = pxL; x < pxL + pw; x += 8) s.drawFastHLine(x, ty, (x + 4 <= pxL + pw) ? 4 : pxL + pw - x, thC);
    } else {
      s.setTextColor(T().subtext);
      s.setCursor(pxL + 2, top + p1H / 2 - 4); s.print(wsRun ? "Mengumpulkan data..." : "-");
    }
    s.drawFastHLine(pxL, pb, pw, T().divider);
  }
  // ---- grafik 2: gelombang sinyal (dihaluskan 3 titik) ----
  if (two) {
    int ly = top + p1H + 3;                              // baris judul
    s.setTextColor(T().subtext); s.setCursor(pxL, ly); s.print("Sinyal WiFi");
    int t2 = ly + 11, b2 = bot, h2 = b2 - t2;
    int n = wsCount < WS_GRAPH_N ? wsCount : WS_GRAPH_N;
    int ns = n - 2;                                      // jumlah titik setelah penghalusan
    if (ns > 3 && h2 >= 12) {
      float m = 0;
      for (int k = 0; k < ns; k++) m += (wsAt(k) + wsAt(k + 1) + wsAt(k + 2)) / 3.0f;
      m /= ns;
      float mx = 0.01f * m;                              // lantai skala 1% dari rata-rata
      for (int k = 0; k < ns; k++) { float a = fabsf((wsAt(k) + wsAt(k + 1) + wsAt(k + 2)) / 3.0f - m); if (a > mx) mx = a; }
      if (mx < 1e-3f) mx = 1e-3f;
      int mid = (t2 + b2) / 2, half = h2 / 2 - 1;
      s.drawFastHLine(pxL, mid, pw, T().divider);
      int px = 0, py = 0;
      uint16_t lc = wsMoving ? ORG : (uint16_t)T().accent;
      for (int k = 0; k < ns; k++) {
        int xx = pxL + (int)((long)pw * (ns - 1 - k) / (ns - 1));   // terbaru di kanan
        int yy = mid - (int)(((wsAt(k) + wsAt(k + 1) + wsAt(k + 2)) / 3.0f - m) / mx * half);
        if (k > 0) s.drawLine(px, py, xx, yy, lc);
        px = xx; py = yy;
      }
    } else if (ns <= 3) {
      s.setCursor(pxL + 2, (t2 + b2) / 2 - 4); s.print(wsRun ? "Mengumpulkan data..." : "-");
    }
  }
  // ---- strip bawah: pesan (kiri, dipotong bila kepanjangan) + Sens n/5 (kanan) ----
  { char sb[12]; snprintf(sb, sizeof(sb), "Sens %d/5", wsSens + 1);
    int sw = s.textWidth(sb);
    s.setTextColor(T().subtext); s.setCursor(gx + gw - sw - 8, gy + gh - 12); s.print(sb);
    char mb[64]; mb[0] = 0; uint16_t mc = T().subtext;
    if (wsCal) {
      int sec = (int)((WS_CAL_MS - (millis() - wsCalT0)) / 1000UL);
      snprintf(mb, sizeof(mb), "Kalibrasi... %d dtk (jangan ada orang)", sec < 0 ? 0 : sec);
      mc = WS_RGB(255,214,10);
    } else if (wsMsg[0] && wsRun) {
      snprintf(mb, sizeof(mb), "%s", wsMsg);
    }
    if (mb[0]) {
      int maxW = gw - 20 - sw - 8, l = (int)strlen(mb);
      while (l > 3 && s.textWidth(mb) > maxW) mb[--l] = 0;
      s.setTextColor(mc); s.setCursor(gx + 10, gy + gh - 12); s.print(mb);
    } }

'''
w = w[:i0] + NEW + w[i1:]

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- Improved: new graph for WiFi Sensing (WSENSE_VIZ_V1). The top graph shows the last 15 seconds of motion as bars against the threshold: "
                 "orange bars are above the threshold (movement), blue bars below it, and the dashed line is the threshold, so an empty room is simply low bars under the line. "
                 "A second graph below shows the raw WiFi signal as a smoothed wave (hidden on short landscape screens). "
                 "The title, the Multi/Rata mode label, the message and *Sens n/5* now sit in their own strips so no text overlaps the graph. "
                 "Costs 150 bytes of RAM. Not yet tested on a device. Script: `apply_wsense_viz_patch.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))

wr(WS, w)
print("ok: wifisense_renphone.ino (+ CHANGELOG.md bila ada)")
