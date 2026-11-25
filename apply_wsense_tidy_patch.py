#!/usr/bin/env python3
# apply_wsense_tidy_patch.py -- WSENSE_TIDY_V1
# Jalankan dari root repo:  python3 apply_wsense_tidy_patch.py
#
# Merapikan tulisan di layar WiFi Sensing (wifisense_renphone.ino):
#  1. Baris angka (Paket / Gerak / Napas): kiri - tengah - kanan rata, tidak lagi berjarak acak.
#  2. Grafik punya strip atas (Adaptif xN di kiri, Multi N di kanan) dan strip bawah
#     (pesan di kiri, Sens n/5 di kanan), dipisah garis tipis. Garis grafik hanya digambar
#     di antara dua strip itu, jadi tulisan tidak lagi bertabrakan dengan grafik.
#  3. "Sens n/5" pindah dari atas tombol (dulu menimpa tepi bawah grafik) ke strip bawah grafik.
#  4. Pesan di strip bawah dipotong otomatis kalau terlalu panjang, supaya tidak menabrak "Sens".
#
# Berkas yang diubah: firmware/phone/wifisense_renphone.ino (+ CHANGELOG.md bila ada).
# Aman dijalankan ulang (cek penanda).
import os, sys

MARK = "WSENSE_TIDY_V1"
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


if not os.path.exists(WS):
    sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo." % WS)
w = rd(WS)
if MARK in w:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)

# 1) baris angka: tengah & kanan benar-benar rata
w = sub(w,
        "  s.setCursor(SCR_W / 3 + 4, iy); s.print(b);\n",
        "  s.setCursor((SCR_W - s.textWidth(b)) / 2, iy); s.print(b);          // WSENSE_TIDY_V1: tengah\n",
        "gerak")
w = sub(w,
        "  s.setCursor(SCR_W * 2 / 3, iy); s.print(b);\n",
        "  s.setCursor(SCR_W - 12 - s.textWidth(b), iy); s.print(b);           // WSENSE_TIDY_V1: rata kanan\n",
        "napas")

# 2) area plot di antara strip atas & bawah
w = sub(w,
        "    int mid = gy + gh / 2, half = gh / 2 - 4;\n"
        "    s.drawFastHLine(gx + 4, mid, gw - 8, T().divider);\n",
        "    int pTop = gy + 21, pBot = gy + gh - 18;                           // WSENSE_TIDY_V1: di antara strip atas & bawah\n"
        "    if (pBot - pTop < 16) { pTop = gy + 4; pBot = gy + gh - 4; }       // grafik sangat pendek: pakai seluruh panel\n"
        "    int mid = (pTop + pBot) / 2, half = (pBot - pTop) / 2 - 2;\n"
        "    s.drawFastHLine(gx + 4, mid, gw - 8, T().divider);\n",
        "plot area")

# 3) strip bawah: pesan (kiri, dipotong) + Sens (kanan), garis pemisah atas & bawah-label
w = sub(w,
        "  if (wsCal) {\n"
        "    int sec = (int)((WS_CAL_MS - (millis() - wsCalT0)) / 1000UL);\n"
        "    snprintf(b, sizeof(b), \"Kalibrasi... %d dtk (jangan ada orang)\", sec < 0 ? 0 : sec);\n"
        "    s.setTextColor(WS_RGB(255,214,10)); s.setCursor(gx + 10, gy + gh - 12); s.print(b);\n"
        "  } else if (wsMsg[0] && wsRun) {\n"
        "    s.setTextColor(T().subtext); s.setCursor(gx + 10, gy + gh - 12); s.print(wsMsg);\n"
        "  }\n",
        "  if (gh >= 70) {                                                      // WSENSE_TIDY_V1: garis pemisah strip atas & bawah\n"
        "    s.drawFastHLine(gx + 8, gy + 19, gw - 16, T().divider);\n"
        "    s.drawFastHLine(gx + 8, gy + gh - 17, gw - 16, T().divider);\n"
        "  }\n"
        "  { char sb[12]; snprintf(sb, sizeof(sb), \"Sens %d/5\", wsSens + 1);   // strip bawah kanan: sensitivitas\n"
        "    int sw = s.textWidth(sb);\n"
        "    s.setTextSize(1); s.setTextColor(T().subtext);\n"
        "    s.setCursor(gx + gw - sw - 8, gy + gh - 12); s.print(sb);\n"
        "    char mb[64]; mb[0] = 0; uint16_t mc = T().subtext;                 // strip bawah kiri: pesan (dipotong bila kepanjangan)\n"
        "    if (wsCal) {\n"
        "      int sec = (int)((WS_CAL_MS - (millis() - wsCalT0)) / 1000UL);\n"
        "      snprintf(mb, sizeof(mb), \"Kalibrasi... %d dtk (jangan ada orang)\", sec < 0 ? 0 : sec);\n"
        "      mc = WS_RGB(255,214,10);\n"
        "    } else if (wsMsg[0] && wsRun) {\n"
        "      snprintf(mb, sizeof(mb), \"%s\", wsMsg);\n"
        "    }\n"
        "    if (mb[0]) {\n"
        "      int maxW = gw - 20 - sw - 8, l = (int)strlen(mb);\n"
        "      while (l > 3 && s.textWidth(mb) > maxW) mb[--l] = 0;\n"
        "      s.setTextColor(mc); s.setCursor(gx + 10, gy + gh - 12); s.print(mb);\n"
        "    } }\n",
        "footer")

# 4) buang 'Sens' lama di atas tombol (dulu menimpa tepi grafik)
w = sub(w,
        "  { int x, w; wsBtnRect(3, &x, &w); int x2, w2; wsBtnRect(2, &x2, &w2);   // indikator sensitivitas di tengah pasangan -/+\n"
        "    s.setTextSize(1); s.setTextColor(T().subtext);\n"
        "    snprintf(b, sizeof(b), \"Sens %d/5\", wsSens + 1);\n"
        "    int tw = s.textWidth(b); s.setCursor(x2 + (x + w - x2 - tw) / 2, wsBtnY() - 11); s.print(b); }\n",
        "  // WSENSE_TIDY_V1: indikator 'Sens n/5' sekarang ada di strip bawah grafik\n",
        "old sens")

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- Improved: the WiFi Sensing screen is tidier (WSENSE_TIDY_V1). The Packets / Motion / Breathing numbers are now aligned left, centre and right. "
                 "The graph has a top strip (*Adaptif* on the left, *Multi N* on the right) and a bottom strip (the message on the left, *Sens n/5* on the right), "
                 "and the line is drawn only between them, so no text sits on top of the graph any more. Long messages are cut to fit. Not yet tested on a device. Script: `apply_wsense_tidy_patch.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))

wr(WS, w)
print("ok: wifisense_renphone.ino (+ CHANGELOG.md bila ada)")
