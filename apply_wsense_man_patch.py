#!/usr/bin/env python3
# apply_wsense_man_patch.py -- WSENSE_MAN_V1
# Jalankan dari root repo:  python3 apply_wsense_man_patch.py
#
# Tampilan ORANG-LIDI (stick man) untuk WiFi Sensing. Butuh WSENSE_VIZ_V1: kalau belum terpasang dan
# apply_wsense_viz_patch.py ada di folder yang sama dengan skrip ini, otomatis dijalankan dulu.
#
# Apa yang ditampilkan:
#   * Ikon kecil di kartu status (menggantikan titik berwarna) -- ikut berubah menurut status.
#   * Ketuk kartu status (tombol pil "Orang" / "Grafik" di kanan kartu) -> panel grafik diganti
#     orang-lidi besar yang beranimasi. Ketuk lagi -> kembali ke grafik.
#       Kosong / tidak aktif : siluet putus-putus redup (tidak ada orang)
#       Ada gerak            : orang-lidi oranye berjalan di tempat; makin besar gerakan (skor / ambang)
#                              makin cepat dan lebar ayunannya
#       Ada orang (diam)     : orang-lidi hijau berdiri; dadanya naik-turun dan ada cincin yang
#                              membesar sesuai laju napas hasil deteksi (napas/mnt)
#       Baru ada gerak       : orang-lidi kuning, tangan terangkat, bergetar kecil (menunggu tenang)
#
# BATAS YANG JUJUR: WiFi Sensing di proyek ini hanya tahu "ada gerak", "ada orang diam yang bernapas",
# dan "kosong". Ia TIDAK tahu posisi, pose, atau berapa orang. Orang-lidi adalah SIMBOL status +
# intensitas gerak + laju napas, bukan gambar tubuh yang sebenarnya (header panel menulis
# "Simbol kehadiran" supaya tidak menyesatkan).
#
# Di tampilan orang-lidi layar digambar ulang 10x/dtk (grafik: 5x/dtk) supaya animasinya halus.
# Tidak ada alokasi heap / String per frame. Berkas yang diubah: firmware/phone/wifisense_renphone.ino
# (+ CHANGELOG.md bila ada). Aman dijalankan ulang (cek penanda).
import os, sys, subprocess

MARK = "WSENSE_MAN_V1"
WS = os.path.join("firmware", "phone", "wifisense_renphone.ino")
CL = "CHANGELOG.md"
HERE = os.path.dirname(os.path.abspath(__file__))


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


if not os.path.exists(WS):
    sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo." % WS)
w = rd(WS)
if MARK in w:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)
if "WSENSE_VIZ_V1" not in w:
    viz = os.path.join(HERE, "apply_wsense_viz_patch.py")
    if not os.path.exists(viz):
        sys.exit("GAGAL: WSENSE_VIZ_V1 belum terpasang. Taruh apply_wsense_viz_patch.py di folder yang sama lalu ulangi.")
    r = subprocess.run([sys.executable, viz])
    if r.returncode != 0:
        sys.exit("GAGAL: apply_wsense_viz_patch.py gagal; patch ini dibatalkan.")
    w = rd(WS)

# 1) state tampilan
w = sub(w,
        "static int      wsHistHead   = 0, wsHistN = 0;\n",
        "static int      wsHistHead   = 0, wsHistN = 0;\n"
        "static uint8_t  wsView       = 0;                  // WSENSE_MAN_V1: 0 = grafik, 1 = orang-lidi\n",
        "state")

# 2) laju gambar ulang: lebih cepat di tampilan orang-lidi
w = sub(w,
        "  if (now - wsUiT0 >= 200UL) { wsUiT0 = now; needRedraw = true; }",
        "  if (now - wsUiT0 >= (wsView ? 100UL : 200UL)) { wsUiT0 = now; needRedraw = true; }   // WSENSE_MAN_V1",
        "redraw rate")

# 3) fungsi gambar orang-lidi (sebelum drawWsense)
HELPER = r'''// ---------------------------------------------------------------------
// WSENSE_MAN_V1: orang-lidi. SIMBOL status + intensitas gerak + laju napas, bukan pose sebenarnya.
//   cx = tengah, top = ujung kepala, h = tinggi total, st = WsState, ratio = skor gerak / ambang
// ---------------------------------------------------------------------
static void wsDrawMan(LGFX_Sprite& s, int cx, int top, int h, int st, float ratio, bool big) {
  uint16_t col; bool ghost = false;
  switch (st) {
    case WS_MOVING: col = WS_RGB(255,159,10); break;
    case WS_STILL:  col = WS_RGB(48,209,88);  break;
    case WS_HOLD:   col = WS_RGB(255,214,10); break;
    default:        col = blend565(T().surface2, T().subtext, 150); ghost = true; break;   // kosong / tidak aktif
  }
  float t = millis() / 1000.0f;
  int r = (int)(h * 0.11f + 0.5f); if (r < 3) r = 3;
  int th = big ? 3 : 2;                                  // tebal garis
  float sw = 0, bob = 0, breath = 0, hz = 0;
  if (st == WS_MOVING) {                                 // jalan di tempat: makin besar gerakan, makin cepat & lebar
    float a = ratio < 1.0f ? 1.0f : (ratio > 3.0f ? 3.0f : ratio);
    float ph = t * 6.2832f * (1.0f + 0.35f * a);
    float amp = 0.5f + 0.2f * a; if (amp > 1.0f) amp = 1.0f;
    sw = sinf(ph) * amp; bob = fabsf(sinf(ph)) * h * 0.02f;
  } else if (st == WS_HOLD) {
    sw = sinf(t * 9.0f) * 0.15f;                         // bergetar kecil
  } else if (st == WS_STILL) {
    hz = (wsBr > 0) ? wsBr / 60.0f : 0.25f;              // laju napas hasil deteksi
    breath = sinf(t * 6.2832f * hz);
  }
  int hy   = top + r - (int)bob;                         // pusat kepala
  int shY  = top + 2 * r + (int)(h * 0.04f) - (int)bob + (int)(breath * (big ? 2.0f : 1.0f));
  int hipY = shY + (int)(h * 0.32f);
  int legL = (int)(h * 0.36f), armL = (int)(h * 0.28f);
  int fw = (int)(h * 0.09f), sx = (int)(h * 0.14f);
  int footY = hipY + legL + (int)bob;
  int handY = shY + (st == WS_HOLD ? (int)(armL * 0.45f) : (int)(armL * 0.95f));
  float lUp = sw > 0 ? sw : 0, rUp = sw < 0 ? -sw : 0;   // kaki kiri naik saat sw > 0, kanan saat sw < 0
  int lFootY = footY - (int)(lUp * h * 0.12f), rFootY = footY - (int)(rUp * h * 0.12f);
  int lHandY = handY - (int)(rUp * armL * 0.5f), rHandY = handY - (int)(lUp * armL * 0.5f);   // lengan berlawanan dgn kaki
  auto ln = [&](int x0, int y0, int x1, int y1) {
    for (int o = 0; o < th; o++) s.drawLine(x0 + o - th / 2, y0, x1 + o - th / 2, y1, col);
  };
  if (ghost) nyxAaCircleRing(s, cx, hy, r, col); else nyxAaCircle(s, cx, hy, r, col);
  ln(cx, hy + r, cx, hipY);                              // leher + badan
  ln(cx, shY + 3, cx - sx, lHandY);                      // lengan kiri
  ln(cx, shY + 3, cx + sx, rHandY);                      // lengan kanan
  ln(cx, hipY, cx - fw, lFootY);                         // kaki kiri
  ln(cx, hipY, cx + fw, rFootY);                         // kaki kanan
  if (st == WS_STILL && big) {                           // cincin napas: membesar & memudar tiap tarikan napas
    float fr = t * hz - floorf(t * hz);
    int rr = (int)(h * 0.10f + fr * h * 0.16f);
    nyxAaCircleRing(s, cx, shY + (int)(h * 0.08f), rr, blend565(T().surface2, col, (int)(200.0f * (1.0f - fr))));
  }
}

void drawWsense(LGFX_Sprite& s) {'''
w = sub(w, "void drawWsense(LGFX_Sprite& s) {", HELPER, "helper")

# 4) kartu status: titik -> orang-lidi kecil + pil pengganti tampilan
w = sub(w,
        "  nyxAaCircle(s, cx + 20, cy + ch / 2, 8, col);\n"
        "  s.setTextSize(2); s.setTextColor(T().text); s.setCursor(cx + 38, cy + 8); s.print(txt);\n"
        "  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(cx + 38, cy + 30); s.print(sub);\n",
        "  (void)col;\n"
        "  { float rN0 = (wsThr() > 0) ? wsScore / wsThr() : 0;                    // WSENSE_MAN_V1: ikon orang-lidi kecil\n"
        "    wsDrawMan(s, cx + 20, cy + 6, ch - 12, wsState, rN0, false); }\n"
        "  s.setTextSize(2); s.setTextColor(T().text); s.setCursor(cx + 38, cy + 8); s.print(txt);\n"
        "  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(cx + 38, cy + 30); s.print(sub);\n"
        "  { const char* pl = wsView ? \"Grafik\" : \"Orang\";                         // pil: ketuk kartu = ganti tampilan\n"
        "    int pw2 = s.textWidth(pl) + 16, px2 = cx + cw - pw2 - 8;\n"
        "    nyxAaRR(s, px2, cy + ch / 2 - 9, pw2, 18, 9, blend565(T().surface2, T().text, 40));\n"
        "    s.setTextColor(T().text); s.setCursor(px2 + 8, cy + ch / 2 - 4); s.print(pl); }\n",
        "status card")

# 5) judul strip atas
w = sub(w,
        "    if (wsRun && fabsf(wsAdaptMul - 1.0f) > 0.15f) snprintf(hl, sizeof(hl), \"Gerak vs ambang  (Adaptif x%.1f)\", (double)wsAdaptMul);\n",
        "    if (wsView) snprintf(hl, sizeof(hl), \"Simbol kehadiran\");\n"
        "    else if (wsRun && fabsf(wsAdaptMul - 1.0f) > 0.15f) snprintf(hl, sizeof(hl), \"Gerak vs ambang  (Adaptif x%.1f)\", (double)wsAdaptMul);\n",
        "header title")

# 6) cabang tampilan orang-lidi (grafik lama jadi cabang else)
MAN = r'''  if (wsView == 1) {                                     // WSENSE_MAN_V1: tampilan orang-lidi
    int ph = bot - top, capH = 12;
    int fh = ph - capH - 4; if (fh > 150) fh = 150;
    float rN = (wsThr() > 0) ? wsScore / wsThr() : 0;
    if (fh >= 40) {
      wsDrawMan(s, gx + gw / 2, top + (ph - capH - fh) / 2, fh, wsState, rN, true);
      char cp[40]; cp[0] = 0;
      switch (wsState) {
        case WS_MOVING: snprintf(cp, sizeof(cp), "Gerak %.1fx ambang", (double)rN); break;
        case WS_STILL:  if (wsBr > 0) snprintf(cp, sizeof(cp), "Napas ~%.0f/mnt", (double)wsBr); break;
        case WS_HOLD:   snprintf(cp, sizeof(cp), "Menunggu tenang..."); break;
        case WS_EMPTY:  snprintf(cp, sizeof(cp), "Tidak ada orang terdeteksi"); break;
        default:        snprintf(cp, sizeof(cp), "Ketuk Mulai"); break;
      }
      s.setTextSize(1); s.setTextColor(T().subtext);
      int cw2 = s.textWidth(cp); s.setCursor(gx + (gw - cw2) / 2, bot - 9); s.print(cp);
    }
  } else {
'''
w = sub(w,
        "  // ---- grafik 1: batang gerak / ambang ----\n",
        MAN + "  // ---- grafik 1: batang gerak / ambang ----\n",
        "graph branch open")
w = sub(w,
        "  // ---- strip bawah: pesan (kiri, dipotong bila kepanjangan) + Sens n/5 (kanan) ----\n",
        "  }  // WSENSE_MAN_V1: akhir cabang grafik\n"
        "  // ---- strip bawah: pesan (kiri, dipotong bila kepanjangan) + Sens n/5 (kanan) ----\n",
        "graph branch close")

# 7) sentuh kartu status = ganti tampilan
w = sub(w,
        "  if (isBack(x, y)) { navBack(); return; }\n",
        "  if (isBack(x, y)) { navBack(); return; }\n"
        "  if (y >= 26 + 18 && y <= 26 + 18 + 46) { wsView ^= 1; needRedraw = true; return; }   // WSENSE_MAN_V1: kartu status = ganti tampilan\n",
        "touch")

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- New: stick-man view for WiFi Sensing (WSENSE_MAN_V1). The status card shows a small stick man instead of the coloured dot, and tapping the card (pill *Orang* / *Grafik*) swaps the graph for a big animated stick man. "
                 "Empty: a dashed ghost outline. Movement: an orange stick man walking on the spot, faster and wider the bigger the movement is compared with the threshold. "
                 "Still person: a green stick man whose chest rises and falls with the detected breathing rate, with a ring that grows on each breath. Just moved: a yellow stick man waiting for calm. "
                 "It is a symbol of the status, not the person's real pose or position (WiFi sensing here cannot tell either). The stick-man view redraws 10 times a second. Not yet tested on a device. Script: `apply_wsense_man_patch.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))

wr(WS, w)
print("ok: wifisense_renphone.ino (+ CHANGELOG.md bila ada)")
