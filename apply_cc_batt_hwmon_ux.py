#!/usr/bin/env python3
# apply_cc_batt_hwmon_ux.py -- CC_UX_V2 + BATT_UX_V2 + HWMON_UX_V2
# Jalankan dari root repo:  python3 apply_cc_batt_hwmon_ux.py
#
# Tampilan & kemudahan pakai baru untuk 3 layar (hanya kode gambar + 1 area sentuh baru):
#  1. Control Center (phone.ino): strip info di atas tile (kiri baterai: persen + "Mengisi"/arus mA, kanan Wi-Fi: nama
#     jaringan). Ketuk kiri = app Baterai, ketuk kanan = Pengaturan > Wi-Fi. Ikon tile ada di lingkaran lembut,
#     titik kecil di pojok tile yang aktif, kenop slider kecerahan berbentuk bulat. Tinggi panel +24 px (dihitung
#     otomatis oleh ccPanelH(), jadi area sentuh tile & slider tetap pas).
#  2. Baterai (ina219_renphone.ino): tab jadi kontrol segmen, cincin persen + kartu angka (Tegangan, Arus, Daya,
#     Sisa waktu/Kapasitas) + baris rincian, garis arus kecil di portrait, grafik dgn area terisi, titik terakhir
#     dan nilai sekarang. Pembacaan INA219, counter, dan semua area sentuh tidak diubah.
#  3. HWmonitor (phone.ino): kartu per core (persen besar, bar, grafik gaya terminal), kartu Memori (RAM, Blok
#     Terbesar, PSRAM), 4 chip info (CPU, Suhu, Uptime, Baterai). Label tombol "Reset Min/Max" jadi "Reset"
#     (teksnya dulu keluar dari kotaknya). Cara hitung beban CPU/RAM dan area sentuh tidak diubah.
#
# Berkas yang diubah: firmware/phone/phone.ino, firmware/phone/ina219_renphone.ino, CHANGELOG.md.
# Aman dijalankan ulang (cek penanda). Jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

PH = os.path.join("firmware", "phone", "phone.ino")
IN = os.path.join("firmware", "phone", "ina219_renphone.ino")
CL = "CHANGELOG.md"

CC_BLOCK = r"""// ===== CC_UX_V2 BEGIN =====
// Strip info di atas tile: kiri baterai (persen + status), kanan Wi-Fi (nama jaringan). Ketuk kiri = app Baterai,
// ketuk kanan = Pengaturan > Wi-Fi.
extern float inaI; extern bool inaOk;   // ina219_renphone.ino
static int ccHdrY(){ return STATUS_H+4; }
static int ccHdrH(){ return 20; }
static int ccHdrSplit(){ return ccGridX() + (ccPanelW()-ccGap()*2)/2; }
static void ccDrawHeader(LGFX_Sprite& s){
  const int gx=ccGridX(), fw=ccPanelW()-ccGap()*2, y=ccHdrY(), h=ccHdrH(), gp=6;
  const int cwid=(fw-gp)/2, ty=y+(h-8)/2;
  const uint16_t chipC=blend565(T().surface2,0xFFFF,24);
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);

  // kiri: baterai
  nyxAaRR(s, gx,y,cwid,h,h/2,chipC);
  {
    const int bx=gx+9, by=y+(h-9)/2;
    nyxAaRRRing(s, bx,by,16,9,2,T().text);
    s.fillRect(bx+16,by+3,2,3,T().text);
    int fwb=(12*battPercent)/100; if(fwb<1 && battPercent>0) fwb=1;
    if(fwb>0) nyxAaRR(s, bx+2,by+2,fwb,5,1,battColor());
    char pb[8]; snprintf(pb,sizeof(pb),"%d%%",battPercent);
    const int tx=bx+24;
    s.setTextColor(T().text); s.setCursor(tx,ty); s.print(pb);
    char sb[16]; sb[0]=0;
    if(battCharging) snprintf(sb,sizeof(sb),"Mengisi");
    else if(inaOk)   snprintf(sb,sizeof(sb),"%.0f mA",fabsf(inaI));
    if(sb[0]){
      int sw=s.textWidth(sb), sx=gx+cwid-10-sw;
      if(sx>tx+(int)s.textWidth(pb)+6){ s.setTextColor(battCharging?T().good:T().subtext); s.setCursor(sx,ty); s.print(sb); }
    }
  }

  // kanan: Wi-Fi
  {
    const int rx=gx+cwid+gp, rw=fw-cwid-gp;
    nyxAaRR(s, rx,y,rw,h,h/2, wifiConnected ? blend565(chipC,T().accent,50) : chipC);
    const int wx=rx+14, wy=y+h/2+4;
    const uint16_t ic=wifiConnected?T().text:T().subtext;
    nyxAaCircle(s, wx,wy,1,ic);
    s.drawArc(wx,wy,5,4,225,315,ic);
    s.drawArc(wx,wy,9,8,225,315,ic);
    if(!wifiConnected) s.drawLine(wx-7,wy-9,wx+7,wy+1,ic);
    static char ssid[24]; static unsigned long ssidMs=0;
    if(!ssidMs || millis()-ssidMs>2000UL){
      ssidMs=millis(); ssid[0]=0;
      if(wifiConnected && WiFi.status()==WL_CONNECTED){ String t=WiFi.SSID(); strlcpy(ssid,t.c_str(),sizeof(ssid)); }
    }
    char tb[24]; strlcpy(tb, (wifiConnected && ssid[0]) ? ssid : RPTR("Mati"), sizeof(tb));
    const int maxW=rw-(wx+12-rx)-10;
    int L=(int)strlen(tb);
    while(L>1 && (int)s.textWidth(tb)>maxW){ tb[--L]=0; }
    s.setTextColor(wifiConnected?T().text:T().subtext); s.setCursor(wx+12,ty); s.print(tb);
  }
}
// ===== CC_UX_V2 END =====

"""
CC_TOUCH = r"""// CC_UX_V2: ketuk strip info = pindah ke app terkait (panel ditutup instan, sama spt tahan-lama tile)
static void ccJump(int destI,int jump){
  Screen dest=(Screen)destI; Screen cur=curScreen();
  if(cur==SCR_NES||cur==SCR_DOOM){ showToast(RPTR("Keluar dari game dulu")); needRedraw=true; return; }
  ccOffset=0; ccAnimating=false; controlCenterOpen=false;
  if(jump) settJumpCode=jump;
  if(cur==dest){ needRedraw=true; return; }
  navPush(dest);
}

void ccTouch(int x,int y){
  int ph=(int)ccOffset;
  if(y> ph-9 && y<=ph+4){ closeControlCenter(); return; }
  if(y>ph) { closeControlCenter(); return; }
  if(y>=ccHdrY() && y<=ccHdrY()+ccHdrH()+2){   // CC_UX_V2
    if(x<ccHdrSplit()) ccJump((int)SCR_BATTERY,0); else ccJump((int)SCR_SETTINGS,1);
    return;
  }
"""
HW_BLOCK = r"""// ===== HWMON_UX_V2 BEGIN =====
// Tampilan baru HWmonitor: kartu per core (persen besar + bar + grafik gaya terminal), kartu Memori,
// dan 4 chip info (CPU / Suhu / Uptime / Baterai). Cara hitung beban CPU, RAM, PSRAM dan area sentuh
// tombol (Reset, Tes Speaker) tidak berubah.
static void hwmonCard(LGFX_Sprite& s, int x, int y, int w, int h) {
  uint16_t c = blend565(T().surface2, 0xFFFF, 14);
  nyxAaRR(s, x, y, w, h, 9, c);
  nyxAaRRRing(s, x, y, w, h, 9, blend565(c, 0xFFFF, 34));
}

static void hwmonBar(LGFX_Sprite& s, int x, int y, int w, int h, int pct, uint16_t col) {
  nyxAaRR(s, x, y, w, h, h / 2, blend565(T().surface, 0xFFFF, 34));
  int fw = constrain((w * pct) / 100, 0, w);
  if (fw > 0) nyxAaRR(s, x, y, fw < h ? h : fw, h, h / 2, col);
}

// kartu 1 core; tinggi total = 58 + gh (dikembalikan)
static int hwmonCoreCard(LGFX_Sprite& s, int x, int y, int w, int gh, const char* label, int usage, int mn, int mx, uint8_t* hist) {
  const int pad = 9, ih = 58 + gh;
  hwmonCard(s, x, y, w, ih);
  const int ix = x + pad, iw = w - 2 * pad;
  const uint16_t lc = hwmonLoadColor(usage);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(ix, y + 9); s.print(label);
  char pb[8]; snprintf(pb, sizeof(pb), "%d%%", usage);
  s.setTextSize(2); s.setTextColor(lc);
  s.setCursor(ix + iw - (int)s.textWidth(pb), y + 5); s.print(pb);
  s.setTextSize(1);
  hwmonBar(s, ix, y + 26, iw, 8, usage, lc);

  // grafik gaya monitor sistem Linux (v50): latar pekat, garis hijau neon, area gelap di bawah garis
  const int gy = y + 40;
  s.fillRect(ix, gy, iw, gh, 0x0000);
  for (int q = 1; q < 4; q++) {
    int yy = gy + (gh * q) / 4;
    for (int gx = ix; gx < ix + iw; gx += (q == 2 ? 4 : 8)) s.drawPixel(gx, yy, T().divider);
  }
  const uint16_t lineCol = 0x07E0;
  const uint16_t areaCol = blend565(0x0000, lineCol, 60);
  int prevPx = -1, prevPy = -1;
  for (int i = 0; i < HWMON_HIST_LEN; i++) {
    int hgt = (gh * hist[i]) / 100; if (hgt < 1) hgt = 1;
    int px = ix + (i * (iw - 1)) / (HWMON_HIST_LEN - 1), py = gy + gh - hgt;
    if (prevPx >= 0) {
      for (int xi = prevPx; xi <= px; xi++) {
        float t = (px == prevPx) ? 0.0f : (float)(xi - prevPx) / (float)(px - prevPx);
        int yi = (int)(prevPy + t * (py - prevPy));
        s.drawFastVLine(xi, yi, gy + gh - yi, areaCol);
      }
      s.drawLine(prevPx, prevPy, px, py, lineCol);
    }
    prevPx = px; prevPy = py;
  }
  s.drawRect(ix, gy, iw, gh, T().divider);

  s.setTextColor(T().subtext);
  s.setCursor(ix, gy + gh + 5);
  char mm[28]; snprintf(mm, sizeof(mm), "min %d%%  maks %d%%", mn, mx);
  s.print(mm);
  return ih;
}

static void hwmonChip(LGFX_Sprite& s, int x, int y, int w, int h, const char* k, const char* v, uint16_t vc) {
  hwmonCard(s, x, y, w, h);
  s.setTextSize(1);
  s.setTextColor(T().subtext); s.setCursor(x + (w - (int)s.textWidth(k)) / 2, y + 6); s.print(k);
  s.setTextColor(vc);          s.setCursor(x + (w - (int)s.textWidth(v)) / 2, y + h - 14); s.print(v);
}

void drawHwmon(LGFX_Sprite& s) {
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);

  {   // ikon chip kecil + judul
    const uint16_t cc = 0x07FF;
    nyxAaRR(s, 9, 26, 12, 12, 3, cc);
    nyxAaRR(s, 12, 29, 6, 6, 1, T().bg);
    for (int k = 0; k < 3; k++) { int px = 11 + k * 4; s.drawFastVLine(px, 23, 3, cc); s.drawFastVLine(px, 38, 3, cc); }
    iosTitle(s, 28, 26, "HWmonitor");
  }
  {   // tombol (area sentuh sama persis dgn hwmonTouch)
    const uint16_t pc = blend565(T().surface2, 0xFFFF, 14);
    const char* rs = "Reset";
    nyxAaRR(s, SCR_W - 84, 24, 78, 18, 9, pc);
    s.setTextColor(T().accent); s.setCursor(SCR_W - 84 + (78 - (int)s.textWidth(rs)) / 2, 29); s.print(rs);
    const char* ts = RPTR("Tes Speaker");
    nyxAaRR(s, SCR_W - 168, 24, 78, 18, 9, pc);
    s.setTextColor(T().accent); s.setCursor(SCR_W - 168 + (78 - (int)s.textWidth(ts)) / 2, 29); s.print(ts);
  }

  const bool land = SCR_W > SCR_H;
  const int gh = !land ? 40 : (SCR_H >= 300 ? 28 : 18);
  int top = 50;
  if (land) {
    int colW = (SCR_W - 12 - 6) / 2;
    hwmonCoreCard(s, 6,            top, colW, gh, "Core 0", hwmonUsage0, hwmonMinUsage0, hwmonMaxUsage0, hwmonHist0);
    hwmonCoreCard(s, 6 + colW + 6, top, colW, gh, "Core 1", hwmonUsage1, hwmonMinUsage1, hwmonMaxUsage1, hwmonHist1);
    top += 58 + gh + 6;
  } else {
    int colW = SCR_W - 12;
    hwmonCoreCard(s, 6, top, colW, gh, "Core 0", hwmonUsage0, hwmonMinUsage0, hwmonMaxUsage0, hwmonHist0);
    top += 58 + gh + 6;
    hwmonCoreCard(s, 6, top, colW, gh, "Core 1", hwmonUsage1, hwmonMinUsage1, hwmonMaxUsage1, hwmonHist1);
    top += 58 + gh + 6;
  }

  // ---- kartu Memori ----
  // FRAG_V3: cadangan kontigu (sysmem) BUKAN pemakaian nyata -- dilepas otomatis saat dibutuhkan. Persen & warna
  // dihitung TANPA cadangan; cadangan digambar sbg segmen terpisah di bar RAM + dijelaskan di baris "Blok Terbesar".
  size_t ramHold = smHoldBytes();
  size_t ramUsedRaw = hwmonHeapTotal > hwmonHeapFree ? hwmonHeapTotal - hwmonHeapFree : 0;
  if (ramHold > ramUsedRaw) ramHold = 0;
  size_t ramUsed = ramUsedRaw - ramHold;
  int ramPct = hwmonHeapTotal > 0 ? (int)(ramUsed * 100UL / hwmonHeapTotal) : 0;
  // RAM_V2: blok yg benar-benar bisa didapat = max(blok bebas, cadangan) (cadangan dilepas otomatis utk TLS dll)
  uint32_t lbHold = smHoldBytes();
  size_t lbEff = hwmonHeapLargestBlock > lbHold ? hwmonHeapLargestBlock : lbHold;
  bool lbLow = lbEff < 20000;
  bool hint = lbLow || lbHold;
  const int memH = 9 + 24 + 8 + 12 + (hint ? 12 : 0) + 8 + (hwmonPsramTotal > 0 ? 24 : 12) + 7;
  hwmonCard(s, 6, top, SCR_W - 12, memH);
  const int ix = 15, iw = SCR_W - 30;
  int y = top + 9;

  s.setTextColor(T().text); s.setCursor(ix, y); s.print(RPTR("RAM Internal"));
  char ramBuf[40]; snprintf(ramBuf, sizeof(ramBuf), "%d%%  (%u / %u KB)", ramPct, (unsigned)(ramUsed / 1024), (unsigned)(hwmonHeapTotal / 1024));
  s.setTextColor(hwmonLoadColor(ramPct)); s.setCursor(ix + iw - (int)s.textWidth(ramBuf), y); s.print(ramBuf);
  hwmonBar(s, ix, y + 13, iw, 9, ramPct, hwmonLoadColor(ramPct));
  if (ramHold > 0 && hwmonHeapTotal > 0) {   // FRAG_V3: segmen cadangan (warna beda, bukan merah)
    int fillW = constrain((iw * ramPct) / 100, 0, iw);
    if (ramPct > 0 && fillW < 9) fillW = 9;
    int holdW = (int)((unsigned long)iw * (unsigned long)ramHold / hwmonHeapTotal);
    if (holdW < 4) holdW = 4;
    if (fillW + holdW > iw) holdW = iw - fillW;
    if (holdW > 0) nyxAaRR(s, ix + fillW, y + 13, holdW, 9, holdW < 9 ? holdW / 2 : 4, T().accent2);
  }
  y += 24 + 8;

  // "Blok Terbesar" = blok kosong BERSAMBUNG terbesar. Ini yg menentukan sukses/gagalnya koneksi TLS (HTTPS),
  // yg butuh SATU blok ~20-30 KB -- RAM bisa terlihat longgar tapi terfragmentasi (lihat catatan v60).
  s.setTextColor(T().text); s.setCursor(ix, y); s.print(RPTR("Blok Terbesar"));
  char lbBuf[32];
  if (lbHold) snprintf(lbBuf, sizeof(lbBuf), "%u KB + %uK", (unsigned)(hwmonHeapLargestBlock / 1024), (unsigned)(lbHold / 1024));
  else        snprintf(lbBuf, sizeof(lbBuf), "%u KB", (unsigned)(hwmonHeapLargestBlock / 1024));
  s.setTextColor(lbLow ? T().danger : T().good); s.setCursor(ix + iw - (int)s.textWidth(lbBuf), y); s.print(lbBuf);
  y += 12;
  if (lbLow) {
    s.setTextColor(T().subtext); s.setCursor(ix, y); s.print(RPTR("(<20KB -> app internet bs gagal HTTP -1)")); y += 12;
  } else if (lbHold) {
    nyxAaRR(s, ix, y + 1, 7, 7, 2, T().accent2);
    s.setTextColor(T().subtext); s.setCursor(ix + 12, y); s.print(RPTR("Cadangan, dilepas otomatis")); y += 12;
  }
  y += 8;

  if (hwmonPsramTotal > 0) {
    int psPct = (int)(100 - (hwmonPsramFree * 100UL / hwmonPsramTotal));
    s.setTextColor(T().text); s.setCursor(ix, y); s.print("PSRAM");
    char psBuf[40]; snprintf(psBuf, sizeof(psBuf), "%d%%  (%u / %u KB)", psPct, (unsigned)((hwmonPsramTotal - hwmonPsramFree) / 1024), (unsigned)(hwmonPsramTotal / 1024));
    s.setTextColor(hwmonLoadColor(psPct)); s.setCursor(ix + iw - (int)s.textWidth(psBuf), y); s.print(psBuf);
    hwmonBar(s, ix, y + 13, iw, 9, psPct, hwmonLoadColor(psPct));
  } else {
    s.setTextColor(T().subtext); s.setCursor(ix, y); s.print(RPTR("PSRAM: tidak terdeteksi"));
  }
  top += memH + 6;

  // ---- chip info: CPU, suhu on-die, uptime, baterai ----
  if (top + 30 <= SCR_H - 4) {
    unsigned long upS = millis() / 1000;
    int uh = upS / 3600, um = (upS % 3600) / 60, us = upS % 60;
    float tc = temperatureRead();
    char c0[12], c1[12], c2[12], c3[12];
    snprintf(c0, sizeof(c0), "%dMHz", (int)ESP.getCpuFreqMHz());
    snprintf(c1, sizeof(c1), "%.1fC", tc);
    snprintf(c2, sizeof(c2), "%02d:%02d:%02d", uh, um, us);
    snprintf(c3, sizeof(c3), "%d%%", battPercent);
    const int cw = (SCR_W - 12 - 3 * 6) / 4;
    const uint16_t tcol = tc >= 80.0f ? T().danger : tc >= 65.0f ? (uint16_t)0xFD20 : T().text;
    hwmonChip(s, 6,               top, cw, 30, "CPU",    c0, T().text);
    hwmonChip(s, 6 + (cw + 6),    top, cw, 30, "Suhu",   c1, tcol);
    hwmonChip(s, 6 + 2 * (cw + 6), top, cw, 30, "Uptime", c2, T().text);
    hwmonChip(s, 6 + 3 * (cw + 6), top, cw, 30, "Baterai", c3, battColor());
  }

  drawBack(s);
  drawToast(s);
}
// ===== HWMON_UX_V2 END =====

"""
BT_BLOCK = r"""// ===== BATT_UX_V2 BEGIN =====
// Tampilan baru app Baterai: cincin persen, kartu statistik, grafik dgn area terisi.
// Hanya kode gambar; pembacaan INA219, counter, dan geometri sentuh (tab, pil grafik, baris Sensor/Baterai) tidak berubah.
static void inaCard(LGFX_Sprite& s, int x, int y, int w, int h) {
  uint16_t c = blend565(T().surface2, 0xFFFF, 14);
  nyxAaRR(s, x, y, w, h, 9, c);
  nyxAaRRRing(s, x, y, w, h, 9, blend565(c, 0xFFFF, 34));
}

static void inaRing(LGFX_Sprite& s, int cx, int cy, int ro, int ri, float pct, uint16_t track, uint16_t col) {
  s.fillArc(cx, cy, ri, ro, 0.0f, 180.0f, track);
  s.fillArc(cx, cy, ri, ro, 180.0f, 360.0f, track);
  if (pct <= 0.0f) return;
  if (pct > 100.0f) pct = 100.0f;
  float a1 = 270.0f + 3.6f * pct;
  if (a1 <= 360.0f) s.fillArc(cx, cy, ri, ro, 270.0f, a1, col);
  else { s.fillArc(cx, cy, ri, ro, 270.0f, 360.0f, col); s.fillArc(cx, cy, ri, ro, 0.0f, a1 - 360.0f, col); }
}

static void inaBolt(LGFX_Sprite& s, int x, int y, uint16_t c) {   // petir 7x14
  s.fillTriangle(x + 3, y, x - 3, y + 7, x + 1, y + 7, c);
  s.fillTriangle(x - 1, y + 6, x + 4, y + 6, x - 2, y + 13, c);
}

static void inaRow(LGFX_Sprite& s, int x, int w, int y, const char* k, const char* v, uint16_t vc) {
  s.setTextSize(1);
  s.setTextColor(T().subtext); s.setCursor(x, y); s.print(k);
  s.setTextColor(vc);
  s.setCursor(x + w - (int)s.textWidth(v), y); s.print(v);
}

static void inaTile(LGFX_Sprite& s, int x, int y, int w, int h, const char* k, const char* v, uint16_t vc) {
  inaCard(s, x, y, w, h);
  s.setTextSize(1);
  char kb[24]; strlcpy(kb, k, sizeof(kb));
  for (int kl = (int)strlen(kb); kl > 1 && (int)s.textWidth(kb) > w - 14; ) kb[--kl] = 0;   // kolom sempit: label dipotong
  s.setTextColor(T().subtext); s.setCursor(x + 9, y + 7); s.print(kb);
  int sz = 2; s.setTextSize(2);
  if ((int)s.textWidth(v) > w - 16) { sz = 1; s.setTextSize(1); }
  s.setTextColor(vc);
  s.setCursor(x + 9, y + h - 8 - (sz == 2 ? 16 : 8)); s.print(v);
  s.setTextSize(1);
}

static float inaHistVal(int k, uint8_t m) {
  int idx = ((int)inaHistHead - (int)inaHistCount + k + 2 * INA_HIST_N) % INA_HIST_N;
  if (m == 0) return (float)inaHistI[idx];
  if (m == 1) return (float)inaHistV[idx] / 1000.0f;
  return (float)inaHistP[idx];
}

// garis arus kecil (10 menit terakhir) utk kartu ringkasan
static void inaSpark(LGFX_Sprite& s, int x, int y, int w, int h, uint16_t col) {
  int n = (int)inaHistCount;
  if (n < 2 || w < 8 || h < 8) {
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(x, y + h / 2 - 4); s.print(RPTR("Mengumpulkan data..."));
    return;
  }
  float lo = 0, hi = 0;
  for (int k = 0; k < n; k++) { float v = inaHistVal(k, 0); if (v < lo) lo = v; if (v > hi) hi = v; }
  if (hi - lo < 20.0f) { hi += 10.0f; lo -= 10.0f; }
  float span = hi - lo;
  int zy = y + (int)((hi - 0.0f) / span * (float)(h - 1));
  for (int gx = x; gx < x + w; gx += 4) s.drawPixel(gx, zy, T().divider);
  int px = x, py = zy;
  for (int i = 0; i < w; i++) {
    int k = (n == 1) ? 0 : (i * (n - 1)) / (w - 1);
    int yy = y + (int)((hi - inaHistVal(k, 0)) / span * (float)(h - 1));
    if (i > 0) s.drawLine(px, py, x + i, yy, col);
    px = x + i; py = yy;
  }
}

static void inaDrawSummary(LGFX_Sprite& s, int top, int bottom) {
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);
  const int W = SCR_W, avail = bottom - top;
  if (!inaOk) {
    inaCard(s, 6, top, W - 12, 96);
    s.setTextColor(T().danger); s.setCursor(14, top + 8); s.print(RPTR("INA219 tidak terdeteksi"));
    s.setTextColor(T().subtext); s.setTextWrap(true);
    s.setCursor(14, top + 26);
    s.print("Cek wiring: VCC 3.3V, GND, SDA = GPIO" RP_STR(RP_SENS_SDA) ", SCL = GPIO" RP_STR(RP_SENS_SCL) ". Shunt dipasang seri di kabel (+) baterai (baterai -> VIN+, beban -> VIN-). Alamat 0x40-0x4F (A0/A1). Buka tab Sensor > Scan ulang setelah memperbaiki.");
    s.setTextWrap(false);
    return;
  }
  const bool land = SCR_W > SCR_H;
  const uint16_t bc = inaSocColor();

  // ---- teks nilai ----
  char vV[16], vI[20], vP[20], vT[20], vCap[28], vSes[28], vCyc[16];
  snprintf(vV, sizeof(vV), "%.3f V", inaBatV);
  snprintf(vI, sizeof(vI), fabsf(inaI) >= 100.0f ? "%+.0f mA" : "%+.1f mA", inaI);
  snprintf(vP, sizeof(vP), "%.0f mW", fabsf(inaP));
  const char* tlb = RPTR("Sisa waktu"); float hrs = -1;
  if (inaCharging) {
    tlb = RPTR("Penuh dalam");
    if (-inaIavg > 5.0f) hrs = ((100.0f - inaSocF) / 100.0f * inaCapEff()) / (-inaIavg);
  } else if (inaIavg > 5.0f) {
    hrs = (inaSocF / 100.0f * inaCapEff()) / inaIavg;
  }
  { char d[16]; inaFmtDur(d, sizeof(d), hrs); snprintf(vT, sizeof(vT), "~%s", d); }
  if (inaCapLearn > 0) snprintf(vCap, sizeof(vCap), "%.0f mAh (%.0f%%)", (double)inaCapLearn, (double)(100.0f * inaCapLearn / (float)inaCapMah));
  else snprintf(vCap, sizeof(vCap), "%s", inaLrnActive ? RPTR("mengukur...") : RPTR("belum terukur"));
  snprintf(vSes, sizeof(vSes), "-%.0f / +%.0f mAh", inaMahOut, inaMahIn);
  snprintf(vCyc, sizeof(vCyc), "%.2f", (double)inaCycles);

  // ---- kartu cincin ----
  int R = land ? (avail - 24) / 2 : 54;
  if (R > 58) R = 58;
  if (R < 32) R = 32;
  const int ringW = (R >= 48) ? 9 : 7;
  int cardW, cardH, gcx, gcy;
  if (land) { cardW = 2 * R + 24; cardH = avail; gcx = 6 + cardW / 2; gcy = top + 10 + R; }
  else      { cardW = W - 12;     cardH = 2 * R + 18; gcx = 6 + 12 + R; gcy = top + cardH / 2; }
  inaCard(s, 6, top, cardW, cardH);
  inaRing(s, gcx, gcy, R, R - ringW, (float)inaSoc, blend565(T().surface, 0xFFFF, 30), bc);
  {
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", inaSoc);
    int sz = (R >= 46) ? 3 : 2;
    s.setTextSize(sz); s.setTextColor(bc);
    int ph = sz * 8;
    s.setCursor(gcx - (int)s.textWidth(pb) / 2, gcy - ph / 2 - 6); s.print(pb);
    s.setTextSize(1); s.setTextColor(T().text);
    const char* st = inaStatusText();
    int sw = (int)s.textWidth(st);
    int sx = gcx - sw / 2, sy = gcy + ph / 2 - 1;
    if (inaCharging) { inaBolt(s, sx - 6, sy - 3, T().good); sx += 5; }
    s.setCursor(sx, sy); s.print(st);
  }
  if (!land) {   // sisi kanan kartu: sisa waktu + garis arus
    int rx = gcx + R + 16, rw = (W - 14) - rx;
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(rx, top + 10); s.print(tlb);
    s.setTextSize(2); s.setTextColor(T().text);
    if ((int)s.textWidth(vT) > rw) s.setTextSize(1);
    s.setCursor(rx, top + 22); s.print(vT);
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(rx, top + 46); s.print(RPTR("Arus"));
    inaSpark(s, rx, top + 58, rw, cardH - 58 - 12, bc);
  } else if (avail - (10 + 2 * R) >= 78) {   // landscape yg lega: sisa waktu di bawah cincin
    int ry = gcy + R + 8;
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(gcx - (int)s.textWidth(tlb) / 2, ry); s.print(tlb);
    s.setTextSize(2); s.setTextColor(T().text);
    s.setCursor(gcx - (int)s.textWidth(vT) / 2, ry + 12); s.print(vT);
    s.setTextSize(1);
  }

  // ---- 4 kartu angka + baris rincian ----
  int tx0, tw, ty0;
  if (land) { tx0 = 6 + cardW + 6; tw = W - 6 - tx0; ty0 = top; }
  else      { tx0 = 6; tw = W - 12; ty0 = top + cardH + 6; }
  const int colW = (tw - 6) / 2;
  int th = 44;
  const int remain = bottom - ty0;
  if (remain < 2 * th + 6 + 3 * 14 + 8) th = (remain - 6 - 3 * 14 - 8) / 2;
  if (th < 32) th = 32;
  if (th > 44) th = 44;
  const bool timeTile = land && (avail - (10 + 2 * R) < 78);   // landscape sempit: sisa waktu jadi kartu
  const uint16_t cI = inaCharging ? T().good : T().text;
  inaTile(s, tx0,              ty0,          colW, th, "Tegangan", vV, T().text);
  inaTile(s, tx0 + colW + 6,   ty0,          colW, th, inaCharging ? "Arus (mengisi)" : "Arus (memakai)", vI, cI);
  inaTile(s, tx0,              ty0 + th + 6, colW, th, "Daya", vP, T().text);
  if (timeTile) inaTile(s, tx0 + colW + 6, ty0 + th + 6, colW, th, tlb, vT, T().text);
  else          inaTile(s, tx0 + colW + 6, ty0 + th + 6, colW, th, "Kapasitas", vCap, T().text);

  int y = ty0 + 2 * th + 6 + 8; const int lh = 14;
  const int rx0 = tx0 + 4, rw0 = tw - 8;
  if (timeTile && y + 8 <= bottom) { inaRow(s, rx0, rw0, y, "Kapasitas terukur", vCap, T().text); y += lh; }
  if (y + 8 <= bottom) { inaRow(s, rx0, rw0, y, "Sesi keluar / masuk", vSes, T().text); y += lh; }
  if (y + 8 <= bottom) {
    if (inaOvf) inaRow(s, rx0, rw0, y, "PERINGATAN", "OVERFLOW: turunkan gain/R", T().danger);
    else {
      char kb[28]; snprintf(kb, sizeof(kb), "Siklus (boot ke-%lu)", (unsigned long)inaBootCount);
      inaRow(s, rx0, rw0, y, kb, vCyc, T().subtext);
    }
  }
}

static void inaDrawGraph(LGFX_Sprite& s, int top, int bottom) {
  static const char* nm[3] = {"Arus", "Tegangan", "Daya"};
  int pw = (SCR_W - 16 - 8) / 3;
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);
  for (int i = 0; i < 3; i++) {
    int x = 8 + i * (pw + 4);
    bool act = (i == inaGraphMetric);
    nyxAaRR(s, x, top, pw, 16, 8, act ? T().accent : blend565(T().surface2, 0xFFFF, 14));
    s.setTextColor(act ? appIconInk(T().accent) : T().subtext);
    s.setCursor(x + (pw - (int)s.textWidth(nm[i])) / 2, top + 4); s.print(nm[i]);
  }
  int gx = 10, gy = top + 22, gw = SCR_W - 20, gh = bottom - 14 - gy;
  if (gh < 20) gh = 20;
  inaCard(s, gx, gy, gw, gh);
  int n = (int)inaHistCount;
  if (n < 2) {
    s.setTextColor(T().subtext);
    s.setCursor(gx + 10, gy + gh / 2 - 4); s.print(RPTR("Mengumpulkan data..."));
    return;
  }
  uint8_t m = inaGraphMetric;
  float mn = 1e9f, mx = -1e9f, sum = 0;
  for (int k = 0; k < n; k++) {
    float v = inaHistVal(k, m); sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  float lo = mn, hi = mx;
  if (m != 1) { if (lo > 0) lo = 0; if (hi < 0) hi = 0; }
  float span = hi - lo;
  float minSpan = (m == 1) ? 0.05f : 10.0f;
  if (span < minSpan) { float mid = (hi + lo) / 2; lo = mid - minSpan / 2; hi = mid + minSpan / 2; span = minSpan; }
  lo -= span * 0.08f; hi += span * 0.08f; span = hi - lo;
  int ix0 = gx + 6, iw = gw - 12, iy0 = gy + 6, ih = gh - 12;
  // garis bantu putus-putus (4 bagian)
  for (int q = 1; q < 4; q++) {
    int yy = iy0 + (ih * q) / 4;
    for (int xx = ix0; xx < ix0 + iw; xx += 5) s.drawPixel(xx, yy, T().divider);
  }
  int baseY = iy0 + ih - 1;
  if (m != 1 && lo < 0 && hi > 0) {
    int zy = iy0 + (int)((hi - 0) / span * ih);
    s.drawFastHLine(ix0, zy, iw, blend565(T().divider, 0xFFFF, 90));
    baseY = zy;
  }
  const uint16_t areaC = blend565(blend565(T().surface2, 0xFFFF, 14), T().accent, 80);
  float step = (float)iw / (float)(INA_HIST_N - 1);
  float px = 0, py = 0;
  for (int k = 0; k < n; k++) {
    float x = (float)(ix0 + iw) - (float)(n - 1 - k) * step;
    float y = (float)iy0 + (hi - inaHistVal(k, m)) / span * (float)ih;
    if (k > 0) {
      int xa = (int)px, xb = (int)x;
      for (int xi = xa; xi <= xb; xi++) {
        float t = (xb == xa) ? 0.0f : (float)(xi - xa) / (float)(xb - xa);
        int yi = (int)(py + t * (y - py));
        int y0 = yi < baseY ? yi : baseY, y1 = yi < baseY ? baseY : yi;
        s.drawFastVLine(xi, y0, y1 - y0 + 1, areaC);
      }
    }
    px = x; py = y;
  }
  px = 0; py = 0;
  for (int k = 0; k < n; k++) {
    float x = (float)(ix0 + iw) - (float)(n - 1 - k) * step;
    float y = (float)iy0 + (hi - inaHistVal(k, m)) / span * (float)ih;
    if (k > 0) { s.drawLine((int)px, (int)py, (int)x, (int)y, T().accent); s.drawLine((int)px, (int)py + 1, (int)x, (int)y + 1, T().accent); }
    px = x; py = y;
  }
  nyxAaCircle(s, (int)px, (int)py, 3, T().accent);
  nyxAaCircle(s, (int)px, (int)py, 1, 0xFFFF);
  // nilai terakhir (kanan atas grafik)
  {
    float cur = inaHistVal(n - 1, m); char cb[24];
    if (m == 1) snprintf(cb, sizeof(cb), "%.3f V", cur);
    else snprintf(cb, sizeof(cb), (m == 0) ? "%+.0f mA" : "%.0f mW", cur);
    s.setTextSize(2); s.setTextColor(T().text);
    int cw2 = (int)s.textWidth(cb);
    if (cw2 > gw / 2) { s.setTextSize(1); cw2 = (int)s.textWidth(cb); }
    nyxAaRR(s, gx + gw - cw2 - 14, gy + 5, cw2 + 10, (s.fontHeight() + 4), 6, blend565(T().surface2, 0x0000, 90));
    s.setCursor(gx + gw - cw2 - 9, gy + 7); s.print(cb);
    s.setTextSize(1);
  }
  char b[48];
  if (m == 1) snprintf(b, sizeof(b), "min %.2f  rata %.2f  maks %.2f V", mn, sum / n, mx);
  else snprintf(b, sizeof(b), "min %.0f  rata %.0f  maks %.0f %s", mn, sum / n, mx, (m == 0) ? "mA" : "mW");
  s.setTextColor(T().subtext); s.setCursor(10, bottom - 10); s.print(b);
}
// ===== BATT_UX_V2 END =====

"""
BT_HEAD  = r"""void drawBatteryApp(LGFX_Sprite& s) {
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setFont(emjF(&lgfx::fonts::Font0));
  s.setTextSize(1);
  {   // BATT_UX_V2: ikon baterai kecil + judul
    uint16_t ic = inaOk ? inaSocColor() : T().subtext;
    nyxAaRRRing(s, 10, 26, 15, 9, 2, T().text);
    s.fillRect(25, 29, 2, 3, T().text);
    int fw = inaOk ? (11 * inaSoc) / 100 : 0;
    if (fw > 0) nyxAaRR(s, 12, 28, fw, 5, 1, ic);
    iosTitle(s, 32, 26, RPTR("Baterai"));
  }

  char chip[16]; uint16_t cc = T().good;
  if (!inaOk) { snprintf(chip, sizeof(chip), "OFFLINE"); cc = T().danger; }
  else if (inaOvf) { snprintf(chip, sizeof(chip), "OVERFLOW"); cc = T().danger; }
  else if (!inaActive) { snprintf(chip, sizeof(chip), "STANDBY"); cc = T().accent; }
  else snprintf(chip, sizeof(chip), "0x%02X OK", inaAddr);
  int cw = (int)s.textWidth(chip) + 18;
  nyxAaRR(s, SCR_W - cw - 6, 23, cw, 15, 7, blend565(T().surface2, 0xFFFF, 14));
  nyxAaCircle(s, SCR_W - cw + 4, 30, 2, cc);
  s.setTextColor(cc); s.setCursor(SCR_W - cw + 10, 27); s.print(chip);

  {   // tab: kontrol segmen (satu wadah, segmen aktif berwarna aksen) -- area sentuh sama seperti sebelumnya
    int tw = (SCR_W - 12) / 5;
    nyxAaRR(s, 6, INA_TAB_Y, tw * 5, INA_TAB_H, INA_TAB_H / 2, blend565(T().surface2, 0xFFFF, 14));
    for (int i = 0; i < 5; i++) {
      int x = 6 + i * tw; bool act = (i == inaTab);
      if (act) nyxAaRR(s, x + 2, INA_TAB_Y + 2, tw - 4, INA_TAB_H - 4, (INA_TAB_H - 4) / 2, T().accent);
      s.setTextColor(act ? appIconInk(T().accent) : T().subtext);
      s.setCursor(x + (tw - (int)s.textWidth(INA_TAB_NAME[i])) / 2, INA_TAB_Y + (INA_TAB_H - 8) / 2); s.print(INA_TAB_NAME[i]);
    }
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
    a = s.find(start)
    if a < 0 or s.count(start) != 1:
        die("awal '%s' tidak ditemukan tepat 1x." % label)
    b = s.find(end, a)
    if b < 0 or s.count(end) != 1:
        die("akhir '%s' tidak ditemukan tepat 1x." % label)
    return s[:a] + new + s[b:]


for p in (PH, IN):
    if not os.path.exists(p):
        die("%s tidak ada. Jalankan dari root repo." % p)
ph, ina = rd(PH), rd(IN)
if "CC_UX_V2" in ph or "BATT_UX_V2" in ina:
    print("sudah terpasang (CC_UX_V2 / BATT_UX_V2 / HWMON_UX_V2)")
    sys.exit(0)
if "\r\n" in ph or "\r\n" in ina:
    die("berkas memakai akhir baris CRLF; ubah ke LF dulu (git config core.autocrlf false).")

# ---------- 1) Control Center ----------
ph = sub(ph,
         "int ccGridTop(){ return STATUS_H+6; }\n",
         "#define CC_HDR_H 24   // CC_UX_V2: tinggi strip info (baterai | Wi-Fi) di atas tile\n"
         "int ccGridTop(){ return STATUS_H+6+CC_HDR_H; }\n",
         "ccGridTop")
ph = sub(ph,
         "static void ccDrawLiveH(LGFX_Sprite& s, int ph, bool withToast){\n",
         CC_BLOCK + "static void ccDrawLiveH(LGFX_Sprite& s, int ph, bool withToast){\n",
         "ccDrawLiveH")
ph = sub(ph,
         "  s.setClipRect(mx,0,pw,max(1,ph-10));\n",
         "  s.setClipRect(mx,0,pw,max(1,ph-10));\n"
         "  if(openP>0.30f) ccDrawHeader(s);   // CC_UX_V2\n",
         "clip CC")
ph = sub(ph,
         "      drawCCIcon(s, i, ccx, ty+th2/2-7, ir, a>0.5f, icC, fillC);\n",
         "      // CC_UX_V2: ikon di atas lingkaran lembut + titik kecil di pojok tile yang aktif\n"
         "      int bcy=ty+th2/2-7, br=ir+3; if(br>15) br=15;\n"
         "      uint16_t badgeC=blend565(fillC,0xFFFF,(uint8_t)(a>0.5f?46:20));\n"
         "      nyxAaCircle(s, ccx,bcy,br,badgeC);\n"
         "      drawCCIcon(s, i, ccx, bcy, ir, a>0.5f, icC, badgeC);\n"
         "      if(a>0.5f) nyxAaCircle(s, tx+tw2-9, ty+9, 2, T().bg);\n",
         "ikon tile")
ph = sub(ph,
         "      int gh = ccSliderDrag ? 10 : 6;       // grip memanjang sedikit selagi di-drag\n"
         "      nyxAaRR(s, sx+fillW-7,trY+(trH-gh)/2,3,gh,1,0xFFFF);\n",
         "      // CC_UX_V2: kenop bulat (membesar sedikit selagi di-drag)\n"
         "      int kr = ccSliderDrag ? 8 : 6, kx2 = sx+(fillW<trH?trH:fillW)-7;\n"
         "      nyxAaCircle(s, kx2,trY+trH/2+1,kr,blend565(T().surface,0x0000,90));\n"
         "      nyxAaCircle(s, kx2,trY+trH/2,kr,0xFFFF);\n",
         "kenop slider")
ph = sub(ph,
         "void ccTouch(int x,int y){\n"
         "  int ph=(int)ccOffset;\n"
         "  if(y> ph-9 && y<=ph+4){ closeControlCenter(); return; }\n"
         "  if(y>ph) { closeControlCenter(); return; }\n",
         CC_TOUCH,
         "ccTouch")

# ---------- 3) HWmonitor ----------
ph = region(ph,
            "// Bar horizontal 1 core + histori batang bergeser di bawahnya.",
            "void hwmonTouch(int x,int y,bool held,bool isNew){\n",
            HW_BLOCK, "HWmonitor")

# ---------- 2) Baterai ----------
ina = region(ina,
             "static void inaRow(LGFX_Sprite& s, int y, const char* k, const char* v, uint16_t vc) {\n",
             "// ---- tab Sensor (2) & Baterai (3): baris nilai + tombol ----\n",
             BT_BLOCK, "ringkasan+grafik Baterai")
ina = region(ina,
             "void drawBatteryApp(LGFX_Sprite& s) {\n",
             "  int top = INA_TOP, bottom = inaBottom();\n",
             BT_HEAD, "kepala app Baterai")

wr(PH, ph)
wr(IN, ina)

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if anchor in c and "CC_UX_V2" not in c:
        entry = ("- Improved: new look and easier use for Control Center, Battery and HWmonitor (CC_UX_V2, BATT_UX_V2, HWMON_UX_V2). "
                 "Control Center gets an info strip above the tiles (battery % with *Mengisi* or the current in mA on the left, "
                 "the Wi-Fi name on the right); tap the left chip to open Battery, the right chip to open Settings > Wi-Fi. "
                 "Tile icons sit on a soft circle, active tiles show a small dot, and the brightness knob is round. "
                 "Battery gets a segmented tab bar, a percentage ring, cards for voltage / current / power / time left, "
                 "a small current line in portrait, and a graph with a filled area, the latest point and the current value. "
                 "HWmonitor gets one card per core (big percentage, bar, terminal-style graph), a Memory card (RAM, largest block, PSRAM) "
                 "and four info chips (CPU, temperature, uptime, battery); the *Reset Min/Max* label is now *Reset* because the text ran out of its button. "
                 "Only drawing code changed (plus one new tap area in Control Center); sensor reading, CPU load maths and all other touch areas are untouched. "
                 "New labels are Indonesian only. Not yet compiled on the real build or tested on a device. Script: `apply_cc_batt_hwmon_ux.py`\n")
        c = c.replace(anchor, anchor + entry, 1)
        wr(CL, c)

print("OK: Control Center, Baterai, HWmonitor diperbarui (CC_UX_V2, BATT_UX_V2, HWMON_UX_V2).")
