#!/usr/bin/env python3
# apply_fit_ux.py -- SETT_FIT_V1 + BATT_FIT_V1
# Jalankan dari root repo (SETELAH apply_cc_batt_hwmon_ux.py):  python3 apply_fit_ux.py
#
# Saran dev: teks harus tetap DI DALAM kotak/kartu, jangan keluar.
#  Pengaturan (phone.ino):
#   - Tiap baris sekarang digambar dengan batas kiri-kanan = kartunya (kalau ada teks yg kepanjangan, tidak mungkin
#     lagi tembus keluar kartu).
#   - Teks kiri yang berdampingan dgn kontrol di kanan (SPI Overclock, Scroll Hardware, Offset ketuk, Heaplog,
#     Rotasi Otomatis, Pindai Jaringan, Kalibrasi, Reset Pabrik, Daya, Kecerahan, dll) dipotong rapi dgn ".." sesuai
#     lebar yg tersedia, jadi tidak menabrak kontrolnya (mis. subtitle Scroll Hardware vs pil 3 pilihan di layar 320).
#  Baterai (ina219_renphone.ino, nyxextras_renphone.ino):
#   - Kartu angka, baris rincian, daftar Sensor/Baterai, tombol: teks dipotong ".." bila kepanjangan.
#   - Pesan "INA219 tidak terdeteksi" dan "Belum ada data" dibungkus per kata DI DALAM kartu (dulu menyambung ke tepi
#     layar), tinggi kartu mengikuti jumlah baris.
#   - Grafik: baris min/rata/maks kini di dalam kartu grafik (dulu menempel di bawah kartu).
# Aman dijalankan ulang. Jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

PH = os.path.join("firmware", "phone", "phone.ino")
IN = os.path.join("firmware", "phone", "ina219_renphone.ino")
NX = os.path.join("firmware", "phone", "nyxextras_renphone.ino")
CL = "CHANGELOG.md"

HELPERS_INA = r"""// ===== BATT_FIT_V1 BEGIN =====
// Potong teks di tempat (tambah "..") supaya lebarnya <= maxW. cap = ukuran buffer.
static void rpFit(LGFX_Sprite& s, char* buf, size_t cap, int maxW) {
  int L = (int)strlen(buf);
  if (L < 2 || (int)s.textWidth(buf) <= maxW) return;
  char t[72];
  for (int cut = L - 1; cut > 1; cut--) {
    int n = cut; if (n > (int)sizeof(t) - 3) n = (int)sizeof(t) - 3;
    if (n > (int)cap - 3) n = (int)cap - 3;
    if (n < 1) break;
    memcpy(t, buf, n); t[n] = '.'; t[n + 1] = '.'; t[n + 2] = 0;
    if ((int)s.textWidth(t) <= maxW) { strlcpy(buf, t, cap); return; }
  }
  buf[1] = 0;
}

// Tulis teks dibungkus per kata di dalam lebar maxW (maks maxLines baris; baris terakhir diberi ".." kalau masih ada sisa).
// Mengembalikan tinggi yg terpakai.
static int rpWrapText(LGFX_Sprite& s, int x, int y, int maxW, int lineH, int maxLines, const char* text, uint16_t col) {
  String all = text; all.trim();
  const int L = (int)all.length();
  int pos = 0, n = 0;
  s.setTextColor(col);
  while (pos < L && n < maxLines) {
    int cut = pos, lastSp = -1;
    for (int i = pos; i < L; i++) {
      if (all[i] == ' ') lastSp = i;
      String t = all.substring(pos, i + 1);
      if ((int)s.textWidth(t.c_str()) > maxW) { cut = i; break; }
      cut = i + 1;
    }
    int brk;
    if (cut >= L) brk = L;
    else if (lastSp > pos) brk = lastSp;
    else brk = (cut > pos) ? cut : pos + 1;
    String ln = all.substring(pos, brk);
    if (n == maxLines - 1 && brk < L) {
      while (ln.length() > 1 && (int)s.textWidth((ln + "..").c_str()) > maxW) ln.remove(ln.length() - 1);
      ln += "..";
    }
    s.setCursor(x, y + n * lineH); s.print(ln.c_str());
    n++; pos = brk;
    while (pos < L && all[pos] == ' ') pos++;
  }
  return n * lineH;
}
// ===== BATT_FIT_V1 END =====

"""

INA_LIST = r"""static void inaDrawList(LGFX_Sprite& s, uint8_t tab, int top) {
  static const char* btnS[2] = {"Reset sensor", "Scan ulang"};
  static const char* btnB[4] = {"Hitung ulang %", "Reset sesi", "Reset total", "Reset puncak"};
  int rh = inaRowH(tab), nr = inaRowsFor(tab), nb = inaBtnsFor(tab);
  int vx0 = SCR_W / 2;
  char lb[24], vb[28];
  s.setTextSize(1);
  for (int r = 0; r < nr; r++) {
    int y = top + r * rh;
    iosRR(s, 6, y, SCR_W - 12, rh - 2, 4, T().surface2);
    inaRowInfo(tab, (uint8_t)r, lb, vb, (int)sizeof(lb));
    int ty = y + (rh - 2 - 8) / 2;
    rpFit(s, lb, sizeof(lb), vx0 - 4 - 12);                 // BATT_FIT_V1: label tidak masuk ke area nilai
    s.setTextColor(T().subtext); s.setCursor(12, ty); s.print(lb);
    s.setTextColor(T().accent);
    s.setCursor(vx0 + 2, ty); s.print("<");
    s.setCursor(SCR_W - 18, ty); s.print(">");
    s.setTextColor(T().text);
    rpFit(s, vb, sizeof(vb), SCR_W - 22 - (vx0 + 12));      // nilai tetap di antara dua panah
    int cx = (vx0 + 12 + SCR_W - 22) / 2;
    s.setCursor(cx - (int)s.textWidth(vb) / 2, ty); s.print(vb);
  }
  int by = top + nr * rh + 4;
  int bw = (SCR_W - 12 - 6) / 2;
  for (int i = 0; i < nb; i++) {
    int bx = 6 + (i % 2) * (bw + 6);
    int yy = by + (i / 2) * 22;
    iosRR(s, bx, yy, bw, 20, 6, T().surface2);
    char bt[24]; strlcpy(bt, (tab == 2) ? btnS[i] : btnB[i], sizeof(bt));
    rpFit(s, bt, sizeof(bt), bw - 10);
    s.setTextColor(T().accent);
    s.setCursor(bx + (bw - (int)s.textWidth(bt)) / 2, yy + 6); s.print(bt);
  }
}

"""

INA_ERR = r"""    int errLines = (avail - 40) / 10; if (errLines < 3) errLines = 3; if (errLines > 8) errLines = 8;
    inaCard(s, 6, top, W - 12, 34 + errLines * 10);
    s.setTextColor(T().danger); s.setCursor(16, top + 9); s.print(RPTR("INA219 tidak terdeteksi"));
    rpWrapText(s, 16, top + 26, W - 12 - 20, 10, errLines,
               "Cek wiring: VCC 3.3V, GND, SDA = GPIO" RP_STR(RP_SENS_SDA) ", SCL = GPIO" RP_STR(RP_SENS_SCL) ". Shunt dipasang seri di kabel (+) baterai (baterai -> VIN+, beban -> VIN-). Alamat 0x40-0x4F (A0/A1). Buka tab Sensor > Scan ulang setelah memperbaiki.",
               T().subtext);
    return;
  }
  const bool land = SCR_W > SCR_H;
"""

NX_EMPTY = r"""    {   // BATT_FIT_V1: pesan kosong dibungkus di dalam kartu
      int eh = btnY - 6 - y; if (eh > 76) eh = 76;
      if (eh >= 34) {
        inaCard(s, 10, y, SCR_W - 20, eh);
        rpWrapText(s, 20, y + 9, SCR_W - 20 - 20, 10, (eh - 14) / 10, RPTR("Belum ada data. Pakai HP dengan baterai (tanpa charger) beberapa menit, lalu buka tab ini lagi."), T().subtext);
      }
    }
"""

SETT_HELPER = r"""// SETT_FIT_V1: cetak teks dipotong ".." supaya lebarnya <= maxW (tidak menabrak kontrol di kanan / keluar kartu)
void settPrintFit(LGFX_Sprite& s,int x,int y,const char* str,int maxW,uint16_t col){
  String v=str;
  if(maxW<12) maxW=12;
  if(s.textWidth(v.c_str())>maxW){
    while(v.length()>1 && s.textWidth((v+"..").c_str())>maxW) v.remove(v.length()-1);
    v+="..";
  }
  s.setTextColor(col); s.setCursor(x,y); s.print(v.c_str());
}
"""


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def die(msg):
    sys.exit("GAGAL: " + msg + " Tidak ada berkas yang diubah.")


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        die("pola '%s' ditemukan %dx (harus tepat 1x)." % (label, n))
    return s.replace(old, new)


def region(s, start, end, new, label):
    if s.count(start) != 1:
        die("awal '%s' tidak ditemukan tepat 1x." % label)
    a = s.find(start)
    if s.count(end) != 1:
        die("akhir '%s' tidak ditemukan tepat 1x." % label)
    b = s.find(end, a)
    if b < 0:
        die("akhir '%s' ada sebelum awalnya." % label)
    return s[:a] + new + s[b:]


for p in (PH, IN, NX):
    if not os.path.exists(p):
        die("%s tidak ada. Jalankan dari root repo." % p)
ph, ina, nx = rd(PH), rd(IN), rd(NX)
if "BATT_FIT_V1" in ina or "SETT_FIT_V1" in ph:
    print("sudah terpasang (SETT_FIT_V1 / BATT_FIT_V1)")
    sys.exit(0)
if "BATT_UX_V2" not in ina:
    die("BATT_UX_V2 belum ada. Jalankan apply_cc_batt_hwmon_ux.py dulu.")
if "\r\n" in ph or "\r\n" in ina or "\r\n" in nx:
    die("berkas memakai akhir baris CRLF; ubah ke LF dulu (git config core.autocrlf false).")

# ================= BATERAI =================
ina = sub(ina, "static void inaCard(LGFX_Sprite& s, int x, int y, int w, int h) {\n",
          HELPERS_INA + "static void inaCard(LGFX_Sprite& s, int x, int y, int w, int h) {\n", "inaCard")
# baris rincian: label dipotong kalau bertabrakan dgn nilai
ina = sub(ina,
          "  s.setTextSize(1);\n"
          "  s.setTextColor(T().subtext); s.setCursor(x, y); s.print(k);\n"
          "  s.setTextColor(vc);\n"
          "  s.setCursor(x + w - (int)s.textWidth(v), y); s.print(v);\n",
          "  s.setTextSize(1);\n"
          "  char kb[40]; strlcpy(kb, k, sizeof(kb));\n"
          "  rpFit(s, kb, sizeof(kb), w - (int)s.textWidth(v) - 10);   // BATT_FIT_V1\n"
          "  s.setTextColor(T().subtext); s.setCursor(x, y); s.print(kb);\n"
          "  s.setTextColor(vc);\n"
          "  s.setCursor(x + w - (int)s.textWidth(v), y); s.print(v);\n",
          "inaRow")
# kartu angka: batas klip + nilai dipotong
ina = sub(ina,
          "  inaCard(s, x, y, w, h);\n  s.setTextSize(1);\n  char kb[24]; strlcpy(kb, k, sizeof(kb));\n",
          "  inaCard(s, x, y, w, h);\n  s.setClipRect(x + 3, y, w - 6, h);   // BATT_FIT_V1: teks tidak bisa keluar dari kartu\n  s.setTextSize(1);\n  char kb[24]; strlcpy(kb, k, sizeof(kb));\n",
          "inaTile awal")
ina = sub(ina,
          "  s.setTextColor(vc);\n  s.setCursor(x + 9, y + h - 8 - (sz == 2 ? 16 : 8)); s.print(v);\n  s.setTextSize(1);\n}\n",
          "  char vb2[28]; strlcpy(vb2, v, sizeof(vb2));\n  rpFit(s, vb2, sizeof(vb2), w - 16);\n"
          "  s.setTextColor(vc);\n  s.setCursor(x + 9, y + h - 8 - (sz == 2 ? 16 : 8)); s.print(vb2);\n  s.setTextSize(1);\n  s.clearClipRect();\n}\n",
          "inaTile akhir")
# kartu error "INA219 tidak terdeteksi"
ina = region(ina, "    inaCard(s, 6, top, W - 12, 96);\n",
             "  const bool land = SCR_W > SCR_H;\n  const uint16_t bc = inaSocColor();\n",
             INA_ERR, "kartu error INA219")
ina = sub(ina, "  const bool land = SCR_W > SCR_H;\n  const uint16_t bc = inaSocColor();\n",
          "  const uint16_t bc = inaSocColor();\n", "dup land") if ina.count("  const bool land = SCR_W > SCR_H;\n  const uint16_t bc = inaSocColor();\n") == 1 else ina
# grafik: baris min/rata/maks masuk ke kartu
ina = sub(ina, "  int gx = 10, gy = top + 22, gw = SCR_W - 20, gh = bottom - 14 - gy;\n  if (gh < 20) gh = 20;\n",
          "  int gx = 10, gy = top + 22, gw = SCR_W - 20, gh = bottom - gy;   // BATT_FIT_V1: kartu mencakup baris min/rata/maks\n  if (gh < 40) gh = 40;\n",
          "ukuran kartu grafik")
ina = sub(ina, "int ix0 = gx + 6, iw = gw - 12, iy0 = gy + 6, ih = gh - 12;\n",
          "int ix0 = gx + 6, iw = gw - 12, iy0 = gy + 6, ih = gh - 12 - 14;\n", "area grafik")
ina = sub(ina, "  s.setTextColor(T().subtext); s.setCursor(10, bottom - 10); s.print(b);\n",
          "  rpFit(s, b, sizeof(b), gw - 16);\n  s.setTextColor(T().subtext); s.setCursor(gx + 8, gy + gh - 15); s.print(b);\n",
          "footer grafik")
# daftar Sensor / Baterai
ina = region(ina, "static void inaDrawList(LGFX_Sprite& s, uint8_t tab, int top) {\n",
             "// =====================================================================\n// Entry app Baterai", INA_LIST, "inaDrawList")
# tab Per App
nx = sub(nx,
         "    s.setTextColor(T().subtext); s.setTextWrap(true);\n"
         "    s.setCursor(10, y + 6);\n"
         "    s.print(RPTR(\"Belum ada data. Pakai HP dengan baterai (tanpa charger) beberapa menit, lalu buka tab ini lagi.\"));\n"
         "    s.setTextWrap(false);\n",
         NX_EMPTY, "pesan Per App")

# ================= PENGATURAN =================
ph = sub(ph, "void settChevron(LGFX_Sprite& s,int cx,int cy,uint16_t col){ // \">\" tipis\n",
         SETT_HELPER + "void settChevron(LGFX_Sprite& s,int cx,int cy,uint16_t col){ // \">\" tipis\n", "settChevron")
ph = sub(ph,
         "    int ty=ry+rh/2-4;\n    int xl=cx+14, xr=cx+cw-14;\n",
         "    int ty=ry+rh/2-4;\n    int xl=cx+14, xr=cx+cw-14;\n    s.setClipRect(cx,top,cw,bot-top);   // SETT_FIT_V1: isi baris tidak bisa keluar dari kartunya\n",
         "batas klip baris")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print(\"SPI Overclock\");\n"
         "        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(spiOcPendingIdx!=spiOcIdx?RPTR(\"Belum diterapkan\"):RPTR(\"Kecepatan layar\"));\n",
         "        settPrintFit(s,xl,ty-4,\"SPI Overclock\",xr-108-8-xl,T().text);   // SETT_FIT_V1\n"
         "        settPrintFit(s,xl,ty+6,spiOcPendingIdx!=spiOcIdx?RPTR(\"Belum diterapkan\"):RPTR(\"Kecepatan layar\"),xr-108-8-xl,T().subtext);\n",
         "SR_SPI")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print(\"Scroll Hardware\");\n",
         "        settPrintFit(s,xl,ty-4,\"Scroll Hardware\",xr-150-8-xl,T().text);   // SETT_FIT_V1\n", "SR_HWS judul")
ph = sub(ph,
         "        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(currentOrient==ORIENT_LANDSCAPE ?",
         "        settPrintFit(s,xl,ty+6,currentOrient==ORIENT_LANDSCAPE ?", "SR_HWS sub awal")
ph = sub(ph,
         "RPTR(\"Portrait, balik\")))); // HOME_PAGER_V1\n",
         "RPTR(\"Portrait, balik\"))),xr-150-8-xl,T().subtext); // HOME_PAGER_V1\n", "SR_HWS sub akhir")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print(RPTR(\"Offset ketuk\"));\n"
         "        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(\"Keyboard (px)\");\n",
         "        settPrintFit(s,xl,ty-4,RPTR(\"Offset ketuk\"),xr-84-8-xl,T().text);   // SETT_FIT_V1\n"
         "        settPrintFit(s,xl,ty+6,\"Keyboard (px)\",xr-84-8-xl,T().subtext);\n", "SR_KBTAP")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print(\"Heaplog (RAM)\");\n"
         "        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(rpHLWanted()!=rpHLBootOn()?RPTR(\"Setelah restart\"):RPTR(\"Log RAM saat boot\"));\n",
         "        settPrintFit(s,xl,ty-4,\"Heaplog (RAM)\",xr-34-8-xl,T().text);   // SETT_FIT_V1\n"
         "        settPrintFit(s,xl,ty+6,rpHLWanted()!=rpHLBootOn()?RPTR(\"Setelah restart\"):RPTR(\"Log RAM saat boot\"),xr-34-8-xl,T().subtext);\n",
         "SR_HEAPLOG")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR(\"Rotasi Otomatis\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Rotasi Otomatis\"),xr-34-8-xl,T().text);   // SETT_FIT_V1\n", "SR_ROT")
ph = sub(ph,
         "        s.setTextColor(wifiScanning?T().subtext:T().accent); s.setCursor(xl,ty); s.print(RPTR(\"Pindai Jaringan\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Pindai Jaringan\"),xr-xl-(wifiScanning?76:0),wifiScanning?T().subtext:T().accent);   // SETT_FIT_V1\n",
         "SR_SCAN")
ph = sub(ph,
         "        s.setTextColor(T().subtext); s.setCursor(xl,ty); s.print(RPTR(\"Tidak ada jaringan. Ketuk Pindai.\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Tidak ada jaringan. Ketuk Pindai.\"),xr-xl,T().subtext);   // SETT_FIT_V1\n", "SR_NONET")
ph = sub(ph,
         "        s.setTextColor(mpuReady?T().accent:T().subtext); s.setCursor(xl,ty); s.print(RPTR(\"Kalibrasi Sensor Gerak\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Kalibrasi Sensor Gerak\"),xr-xl-(mpuReady?0:(int)s.textWidth(RPTR(\"Tidak terdeteksi\"))+10),mpuReady?T().accent:T().subtext);   // SETT_FIT_V1\n",
         "SR_MPU")
ph = sub(ph,
         "        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Kalibrasi Layar Sentuh\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Kalibrasi Layar Sentuh\"),xr-xl-(millis()<settTcalArmUntil?(int)s.textWidth(RPTR(\"Ketuk lagi\"))+10:0),T().danger);   // SETT_FIT_V1\n",
         "SR_TCAL")
ph = sub(ph,
         "        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Reset Pabrik\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Reset Pabrik\"),xr-xl-(millis()<settFactArmUntil?(int)s.textWidth(RPTR(\"Ketuk lagi: HAPUS SEMUA\"))+10:16),T().danger);   // SETT_FIT_V1\n",
         "SR_FACTORY")
ph = sub(ph,
         "        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(RPTR(\"Daya (Matikan / Mulai Ulang)\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Daya (Matikan / Mulai Ulang)\"),xr-xl-16,T().danger);   // SETT_FIT_V1\n", "SR_POWER")
ph = sub(ph,
         "        s.setTextColor(T().subtext); s.setCursor(tx0,acy+6); s.print(RPTR(\"Ketuk untuk atur profil\"));\n",
         "        settPrintFit(s,tx0,acy+6,RPTR(\"Ketuk untuk atur profil\"),xr-18-tx0,T().subtext);   // SETT_FIT_V1\n", "SR_OWNER")
ph = sub(ph,
         "          s.setTextColor(T().subtext); s.setCursor(sx,ty+6); s.print(sb);\n",
         "          settPrintFit(s,sx,ty+6,sb,xr-20-sx,T().subtext);   // SETT_FIT_V1\n", "SR_WSTAT sinyal")
ph = sub(ph,
         "          s.setTextColor(T().subtext); s.setCursor(sx,ty+6); s.print(RPTR(\"Pilih jaringan di bawah\"));\n",
         "          settPrintFit(s,sx,ty+6,RPTR(\"Pilih jaringan di bawah\"),xr-20-sx,T().subtext);   // SETT_FIT_V1\n", "SR_WSTAT offline")
ph = sub(ph,
         "          s.setTextColor(T().text); s.setCursor(tx+10,ty0+th-22); s.print(lb);\n",
         "          settPrintFit(s,tx+10,ty0+th-22,lb,tw-18,T().text);   // SETT_FIT_V1\n", "SR_QUICK label")
ph = sub(ph,
         "        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR(\"Kecerahan\"));\n",
         "        settPrintFit(s,xl,ty,RPTR(\"Kecerahan\"),(cx+110)-8-xl,T().text);   // SETT_FIT_V1\n", "SR_BRIGHT")

wr(PH, ph)
wr(IN, ina)
wr(NX, nx)

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if anchor in c and "SETT_FIT_V1" not in c:
        entry = ("- Fixed: text now stays inside its box in Settings and the Battery app, as suggested by a tester (SETT_FIT_V1, BATT_FIT_V1). "
                 "In Settings every row is clipped to its card and the left-hand text next to a control (SPI Overclock, Scroll Hardware, Keyboard tap offset, Heaplog, "
                 "Auto-rotate, Scan, calibration, factory reset, power, brightness) is cut with \"..\" to the width that is free, so it no longer runs into the control. "
                 "In Battery, the number cards, detail rows, Sensor and Battery lists and buttons cut long text with \"..\", the *INA219 not found* and *no data yet* messages "
                 "wrap inside a card, and the min / average / max line of the graph is inside the graph card. Only drawing code changed. "
                 "Not yet compiled on the real build or tested on a device. Script: `apply_fit_ux.py`\n")
        c = c.replace(anchor, anchor + entry, 1)
        wr(CL, c)

print("OK: teks Pengaturan & Baterai dijaga tetap di dalam kotak (SETT_FIT_V1, BATT_FIT_V1).")
