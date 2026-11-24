#!/usr/bin/env python3
# apply_call_ui_patch.py -- CALL_UI_V2: tampilan & UX app Telepon dirombak
# Jalankan dari root repo:  python3 tools/patches/apply_call_ui_patch.py
#
# Yang berubah (hanya call_renphone.ino bagian GAMBAR + SENTUHAN; SIP/RTP/audio/TURN TIDAK disentuh):
#  - layar utama: judul besar, chip status (VoIP terdaftar / IP sendiri), tab VoIP|LAN bentuk pil,
#    kolom tujuan besar dgn tombol hapus (x), tombol Panggil pil hijau berikon gagang,
#    riwayat/perangkat jadi baris kartu berisi avatar huruf berwarna + nama + tombol panggil bulat
#  - layar panggilan: avatar besar (cincin hijau = tersambung, merah = mute, gelombang berdenyut saat berdering),
#    timer besar, meter Lawan/Kamu, chip rute audio, tombol bulat berikon (Mute, Vol -, Vol +) + pil Akhiri;
#    panggilan masuk = dua tombol bulat Tolak / Terima
#  - banner panggilan masuk: kartu dgn avatar + tombol bulat Tolak / Angkat
#  - pengaturan: baris label-kiri / nilai-kanan, saklar Siaga, kata sandi jadi titik, Simpan di baris bawah
#  - layout menyesuaikan landscape (dua kolom) dan portrait; semua tata letak dihitung dari SCR_W/SCR_H
#  - ketuk chip status = tampilkan/sembunyikan log diagnosa (dulu selalu tampil)
#  - ditambah: 2 pasang teks terjemahan (Terakhir, Perangkat) di tools/i18n_pairs.txt + i18n_dict.h
import os, re, subprocess, sys

FN = os.path.join("firmware", "phone", "call_renphone.ino")
if not os.path.exists(FN):
    sys.exit("GAGAL: jalankan dari root repo (tidak ada %s)" % FN)
s = open(FN, encoding="utf-8").read()
if "CALL_UI_V2 -- helper gambar bersama" in s:
    print("sudah terpasang:", FN); sys.exit(0)

def once(s, old, new):
    if s.count(old) != 1:
        sys.exit("GAGAL: pola tidak ditemukan tepat 1x:\n" + old[:110])
    return s.replace(old, new)

def cut(s, start, end, repl):
    a = s.find(start)
    if a < 0 or s.count(start) != 1:
        sys.exit("GAGAL: penanda awal tidak ketemu: " + start[:60])
    b = len(s) if end is None else s.find(end, a)
    if b < 0 or (end is not None and s.count(end) != 1):
        sys.exit("GAGAL: penanda akhir tidak ketemu: " + str(end)[:60])
    return s[:a] + repl + s[b:]

NEW_A = r'''// CALL_UI_V2 -- helper gambar bersama (tanda tangan hanya tipe primitif / String& / LGFX_Sprite&)
static bool callWide() { return SCR_W >= SCR_H * 13 / 10; }          // landscape: layout dua kolom

static uint16_t callAvatarColor(const char* nm) {
  uint32_t h = 5381;
  for (const char* p = nm; p && *p; p++) h = h * 33u + (uint8_t)*p;
  const uint16_t pal[6] = { (uint16_t)T().accent, (uint16_t)0xFD20, (uint16_t)0x34DF, (uint16_t)0xF9A6, (uint16_t)0x362B, (uint16_t)0xAB7F };
  return pal[h % 6];
}
static char callInitial(const char* nm) {
  for (const char* p = nm; p && *p; p++) if (isalnum((unsigned char)*p)) return (char)toupper((unsigned char)*p);
  return '?';
}
static void callAvatar(LGFX_Sprite& s, int cx, int cy, int r, const char* nm) {
  uint16_t col = callAvatarColor(nm);
  nyxAaCircle(s, cx, cy, r, col);
  if (r >= 14) nyxAaCircleRing(s, cx, cy, r, blend565(col, 0xFFFF, 60));
  int sz = r >= 28 ? 3 : (r >= 14 ? 2 : 1);
  char t[2] = { callInitial(nm), 0 };
  s.setTextSize(sz); s.setTextColor(appIconInk(col));
  s.setCursor(cx - s.textWidth(t) / 2, cy - (sz * 8) / 2); s.print(t);
  s.setTextSize(1);
}

// gagang telepon: busur tebal + dua bantalan. bulgeDeg = arah tonjolan (-90 = ke atas = "tutup", 135 = "angkat")
static void callHandset(LGFX_Sprite& s, int cx, int cy, int sz, int bulgeDeg, uint16_t col) {
  float b = bulgeDeg * 0.0174533f, R = sz * 0.85f, half = 62.0f * 0.0174533f;
  float ccx = cx - cosf(b) * R * 0.78f, ccy = cy - sinf(b) * R * 0.78f;
  int tb = sz * 22 / 100; if (tb < 2) tb = 2;
  for (int i = 0; i <= 12; i++) {
    float a = b - half + (2 * half) * i / 12.0f;
    nyxAaCircle(s, (int)lroundf(ccx + cosf(a) * R), (int)lroundf(ccy + sinf(a) * R), tb, col);
  }
  int pr = sz * 32 / 100; if (pr <= tb) pr = tb + 1;
  for (int e = 0; e < 2; e++) {
    float a = e ? b + half : b - half;
    float px = ccx + cosf(a) * R, py = ccy + sinf(a) * R;
    nyxAaCircle(s, (int)lroundf(px + (ccx - px) / R * sz * 0.16f), (int)lroundf(py + (ccy - py) / R * sz * 0.16f), pr, col);
  }
}

static void callIconMic(LGFX_Sprite& s, int cx, int cy, int r, bool slash, uint16_t col, uint16_t slashCol) {
  float u = r / 12.0f;
  int cw = (int)(4 * u); if (cw < 2) cw = 2;
  nyxAaRR(s, cx - cw, cy - (int)(9 * u), cw * 2, (int)(13 * u), cw, col);
  int x1 = cx - (int)(6 * u), x2 = cx + (int)(6 * u), yt = cy - (int)(2 * u), yb = cy + (int)(5 * u), c = (int)(3 * u);
  for (int k = 0; k < 2; k++) {
    s.drawFastVLine(x1 + k, yt, yb - yt - c, col); s.drawFastVLine(x2 - k, yt, yb - yt - c, col);
    s.drawLine(x1 + k, yb - c, x1 + c + k, yb, col); s.drawLine(x2 - k, yb - c, x2 - c - k, yb, col);
    s.drawFastHLine(x1 + c, yb + k, x2 - x1 - 2 * c, col);
    s.drawFastVLine(cx - k, yb, (int)(4 * u), col);
  }
  s.drawFastHLine(cx - (int)(4 * u), yb + (int)(4 * u), (int)(8 * u), col);
  if (slash) for (int k = 0; k < 3; k++) s.drawLine(cx - (int)(9 * u), cy - (int)(9 * u) + k, cx + (int)(9 * u), cy + (int)(9 * u) + k, slashCol);
}
static void callIconSpk(LGFX_Sprite& s, int cx, int cy, int r, int sign, uint16_t col) {   // pengeras suara + (+) atau (-)
  float u = r / 12.0f;
  int x0 = cx - (int)(11 * u), t = (int)(3 * u), l = (int)(4 * u);
  s.fillRect(x0, cy - t, (int)(4 * u), t * 2, col);
  s.fillTriangle(x0 + (int)(4 * u), cy - t, x0 + (int)(10 * u), cy - (int)(8 * u), x0 + (int)(10 * u), cy + (int)(8 * u), col);
  s.fillTriangle(x0 + (int)(4 * u), cy - t, x0 + (int)(4 * u), cy + t, x0 + (int)(10 * u), cy + (int)(8 * u), col);
  int px = cx + (int)(6 * u);
  for (int k = 0; k < 2; k++) {
    s.drawFastHLine(px - l, cy + k, l * 2, col);
    if (sign > 0) s.drawFastVLine(px - 1 + k, cy - l, l * 2, col);
  }
}
static void callIconGear(LGFX_Sprite& s, int cx, int cy, int r, uint16_t col, uint16_t hole) {
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f;
    nyxAaCircle(s, cx + (int)lroundf(cosf(a) * r), cy + (int)lroundf(sinf(a) * r), r / 4 + 1, col);
  }
  nyxAaCircle(s, cx, cy, r - 1, col);
  nyxAaCircle(s, cx, cy, r / 2, hole);
}

static void callRoundBtn(LGFX_Sprite& s, int cx, int cy, int r, uint16_t col) {
  nyxAaCircle(s, cx, cy + 2, r, blend565(T().bg, 0x0000, 110));
  nyxAaCircle(s, cx, cy, r, col);
  nyxAaCircleRing(s, cx, cy, r, blend565(col, 0xFFFF, 45));
}

// teks muat dalam maxW: ukuran pref, turun ke 1, lalu dipotong ".." ; mengembalikan ukuran terpakai (ukuran teks sprite ikut diset)
static int callFit(LGFX_Sprite& s, const char* t, int maxW, int pref, String& out) {
  out = t; int sz = pref;
  s.setTextSize(sz);
  if (s.textWidth(out.c_str()) > maxW && sz > 1) { sz = 1; s.setTextSize(1); }
  if (s.textWidth(out.c_str()) > maxW) {
    while (out.length() > 1 && s.textWidth((out + "..").c_str()) > maxW) out.remove(out.length() - 1);
    out += "..";
  }
  return sz;
}
// teks bungkus per kata; mengembalikan y setelah baris terakhir
static int callWrap(LGFX_Sprite& s, int x, int y, int w, const char* t, uint16_t col) {
  s.setTextSize(1); s.setTextColor(col);
  String line = "", word = "";
  int ly = y;
  for (const char* p = t; ; p++) {
    char c = *p;
    if (c == ' ' || c == 0) {
      String cand = line.length() ? (line + " " + word) : word;
      if (line.length() && s.textWidth(cand.c_str()) > w) { s.setCursor(x, ly); s.print(line.c_str()); ly += 11; line = word; }
      else line = cand;
      word = "";
      if (c == 0) break;
    } else word += c;
  }
  if (line.length()) { s.setCursor(x, ly); s.print(line.c_str()); ly += 11; }
  return ly;
}

// ---------------------------------------------------------------------
//  BANNER PANGGILAN MASUK (NYX-EXTRAS-V1, tampilan CALL_UI_V2)
//  HP terbuka + ada panggilan: kartu kecil di atas layar dgn avatar + tombol bulat Tolak / Angkat,
//  app yang sedang dipakai tidak terganggu. Ketuk badan banner = layar penuh.
//  Terkunci -> tetap layar penuh seperti sebelumnya.
// ---------------------------------------------------------------------
static void callBannerRect(int& x, int& y, int& w, int& h) {
  w = SCR_W - 16; if (w > 380) w = 380;
  h = 56; x = (SCR_W - w) / 2;
  uint32_t age = millis() - callBannerT0;
  float k = age >= 300 ? 1.0f : (float)age / 300.0f;
  float q = 1.0f - k;
  k = 1.0f - q * q * q;                                  // ease-out cubic
  y = (int)(-h + (h + 6) * k);                           // turun dari atas layar sampai y = 6
}
static void callBannerBtns(int& decCx, int& accCx, int& cyb, int& rb) {
  int x, y, w, h; callBannerRect(x, y, w, h);
  rb = 15; cyb = y + h / 2; accCx = x + w - 8 - rb; decCx = accCx - 2 * rb - 8;
}

bool callBannerActive() { return callBanner; }

void drawCallBanner(LGFX_Sprite& s) {
  if (!callBanner) return;
  int x, y, w, h; callBannerRect(x, y, w, h);
  char pe[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callUnlock();
  nyxAaRR(s, x + 1, y + 4, w, h, 18, blend565(T().bg, 0x0000, 150));          // bayangan
  nyxAaRR(s, x, y, w, h, 18, T().surface2);
  nyxAaRRRing(s, x, y, w, h, 18, blend565(T().surface2, T().text, 46));      // garis tipis
  int ax = x + 8 + 16, ay = y + h / 2;
  callAvatar(s, ax, ay, 16, pe);
  int decCx, accCx, cyb, rb; callBannerBtns(decCx, accCx, cyb, rb);
  int tx = ax + 16 + 8, maxW = decCx - rb - 6 - tx;
  String lb; callFit(s, callIsLan ? RPTR("Panggilan LAN") : RPTR("Panggilan masuk"), maxW, 1, lb);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, y + 11); s.print(lb.c_str());
  String nm; int sz = callFit(s, pe, maxW, 2, nm);
  s.setTextColor(T().text); s.setCursor(tx, y + (sz == 2 ? 26 : 30)); s.print(nm.c_str());
  s.setTextSize(1);
  callRoundBtn(s, decCx, cyb, rb, T().danger); callHandset(s, decCx, cyb, 9, -90, 0xFFFF);
  callRoundBtn(s, accCx, cyb, rb, T().good);   callHandset(s, accCx, cyb, 9, 135, 0xFFFF);
}

// true = sentuhan ditelan banner (jangan diteruskan ke app di bawahnya)
bool callBannerTouch(bool touched, bool newT, int tx, int ty) {
  if (!callBanner) { callBannerSupp = false; return false; }
  if (callBannerSupp) { if (!touched) callBannerSupp = false; return touched; }
  if (!touched) return false;
  int x, y, w, h; callBannerRect(x, y, w, h);
  bool inside = tx >= x && tx <= x + w && ty <= y + h;
  if (!inside) return false;
  if (!newT) return true;
  int decCx, accCx, cyb, rb; callBannerBtns(decCx, accCx, cyb, rb);
  long da = (long)(tx - accCx) * (tx - accCx) + (long)(ty - cyb) * (ty - cyb);
  long dd = (long)(tx - decCx) * (tx - decCx) + (long)(ty - cyb) * (ty - cyb);
  long lim = (long)(rb + 6) * (rb + 6);
  if (da <= lim || tx > accCx - rb)       { callPost(CRQ_ANSWER, ""); callBanner = false; }
  else if (dd <= lim || tx > decCx - rb)  { callPost(CRQ_REJECT, ""); callBanner = false; }
  else                                    { callBanner = false; callOvl = true; callOvlLocked = false; }
  callBannerSupp = true;
  vibTap();
  needRedraw = true;
  return true;
}

'''

NEW_B = r'''static void callCenter(LGFX_Sprite& s, int x, int w, int y, const char* t) {
  s.setCursor(x + (w - s.textWidth(t)) / 2, y);
  s.print(t);
}

// ---------------------------------------------------------------------
//  LAYAR PANGGILAN (CALL_UI_V2): hero (avatar, nama, status/timer, meter) + tombol bulat
//  Portrait: hero di atas, tombol di bawah. Landscape: hero di kiri, tombol di kanan.
// ---------------------------------------------------------------------
static void callAreaY(int& top, int& bot) { top = STATUS_H + 4; bot = callOvl ? SCR_H - 10 : backY() - 6; }

static void callActGeom(int& ax, int& aw, int& y0, int& cr, int& pillH) {
  int top, bot; callAreaY(top, bot);
  bool wide = callWide();
  if (wide) { ax = SCR_W * 46 / 100; aw = SCR_W - ax - 10; } else { ax = 8; aw = SCR_W - 16; }
  bool inc = callPhase == CPH_INCOMING;
  cr = (aw - 20) / 6; if (cr > 24) cr = 24; if (cr < 16) cr = 16;
  pillH = 34;
  if (!wide && bot - top < 250) { if (cr > 20) cr = 20; pillH = 30; }
  int blockH = inc ? ((cr + 8) * 2 + 6 + 10) : (cr * 2 + 6 + 10 + 10 + pillH);
  if (wide) y0 = top + ((bot - top) - blockH) / 2; else y0 = bot - blockH;
  if (y0 < top) y0 = top;
}

// idx 0..1 (masuk: Tolak, Terima) / 0..2 bulat (Mute, Vol-, Vol+) + 3 pil (Akhiri). w = lebar sentuh
static bool callBtnGeom(int idx, int& cx, int& cy, int& r, int& w) {
  int ax, aw, y0, cr, pillH; callActGeom(ax, aw, y0, cr, pillH);
  int mid = ax + aw / 2;
  if (callPhase == CPH_INCOMING) {
    if (idx > 1) return false;
    int rr = cr + 8, sp = aw / 2; if (sp > 2 * rr + 30) sp = 2 * rr + 30;
    cx = mid + (idx ? sp / 2 : -sp / 2); cy = y0 + rr; r = rr; w = rr * 2;
    return true;
  }
  if (idx > 3) return false;
  if (idx < 3) {
    int sp = aw / 3; if (sp > 2 * cr + 18) sp = 2 * cr + 18;
    cx = mid + (idx - 1) * sp; cy = y0 + cr; r = cr; w = cr * 2;
    return true;
  }
  w = aw * 85 / 100; if (w > 220) w = 220;
  r = pillH / 2; cx = mid; cy = y0 + cr * 2 + 6 + 10 + 10 + pillH / 2;
  return true;
}

static void callBtnLabel(LGFX_Sprite& s, int cx, int cy, int r, const char* t) {
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(cx - s.textWidth(t) / 2, cy + r + 5); s.print(t);
}

static void callDrawInCall(LGFX_Sprite& s) {
  char pe[44], st[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callCopy(st, sizeof(st), callStatus); callUnlock();
  uint8_t ph = callPhase;
  bool act = ph == CPH_ACTIVE;
  bool ringing = ph == CPH_DIAL || ph == CPH_RINGBACK || ph == CPH_INCOMING;
  int top, bot; callAreaY(top, bot);
  int ax, aw, y0, cr, pillH; callActGeom(ax, aw, y0, cr, pillH);
  int hw, hy0, hy1;
  if (callWide()) { hw = ax - 4; hy0 = top; hy1 = bot; } else { hw = SCR_W; hy0 = top; hy1 = y0 - 8; }
  int hcx = hw / 2;
  // ukuran avatar menyesuaikan tinggi yang tersedia
  int avail = (hy1 - hy0) - (ringing ? 10 : 0);
  bool showRoute = true;
  int rA = (avail - 64 - (act ? 26 : 0)) / 2;
  if (rA < 18) { showRoute = false; rA = (avail - 42 - (act ? 26 : 0)) / 2; }
  if (rA > 36) rA = 36;
  if (rA < 14) rA = 14;
  int total = 2 * rA + 8 + 16 + 2 + 16 + (act ? 26 : 0) + (showRoute ? 22 : 0);
  int y = hy0 + ((hy1 - hy0) - total) / 2; if (y < hy0) y = hy0;
  int cyA = y + rA;
  if (ringing) {                                             // gelombang berdenyut
    uint32_t t = millis() % 1800;
    for (int k = 0; k < 2; k++) {
      uint32_t tk = (t + k * 900) % 1800;
      nyxAaCircleRing(s, hcx, cyA, rA + 3 + (int)(tk * 12 / 1800), blend565(T().bg, T().accent, (uint8_t)(130 - tk * 120 / 1800)));
    }
  }
  if (act) {                                                 // cincin status: hijau = tersambung, merah = mute
    uint16_t rc = callMuted ? (uint16_t)T().danger : (uint16_t)T().good;
    nyxAaCircleRing(s, hcx, cyA, rA + 3, rc); nyxAaCircleRing(s, hcx, cyA, rA + 4, rc);
  }
  callAvatar(s, hcx, cyA, rA, pe);
  y = cyA + rA + 8;
  String nm; int sz = callFit(s, pe, hw - 16, 2, nm);
  s.setTextColor(T().text); s.setCursor(hcx - s.textWidth(nm.c_str()) / 2, y + (sz == 2 ? 0 : 4)); s.print(nm.c_str());
  s.setTextSize(1);
  y += 18;
  if (act) {
    uint32_t sec = (millis() - callStartMs) / 1000UL;
    char line[16]; snprintf(line, sizeof(line), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    s.setTextSize(2); s.setTextColor(T().good);
    s.setCursor(hcx - s.textWidth(line) / 2, y); s.print(line);
    s.setTextSize(1);
  } else {
    String ss; callFit(s, ph == CPH_INCOMING ? (callIsLan ? RPTR("Panggilan LAN") : RPTR("Panggilan masuk")) : st, hw - 16, 1, ss);
    s.setTextColor(T().accent); s.setCursor(hcx - s.textWidth(ss.c_str()) / 2, y + 4); s.print(ss.c_str());
  }
  y += 24;
  if (act) {                                                 // CALL_RXLEVEL_V1: meter lawan (atas) + mic sendiri (bawah)
    int bw = hw - 16 - 36; if (bw > 110) bw = 110;
    int x0 = hcx - (36 + bw) / 2, bx = x0 + 36;
    uint8_t rxl = callRxLvl, mcl = callMicLvl;
    callShownRx = rxl; callShownMic = mcl;
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(x0, y);     s.print(RPTR("Lawan"));
    s.setCursor(x0, y + 10); s.print(RPTR("Kamu"));
    nyxAaRR(s, bx, y + 1, bw, 6, 3, blend565(T().surface2, T().text, 30));
    int fr = bw * rxl / 100; if (fr > 5) nyxAaRR(s, bx, y + 1, fr, 6, 3, (uint16_t)T().accent);
    nyxAaRR(s, bx, y + 11, bw, 6, 3, blend565(T().surface2, T().text, 30));
    int fm = bw * mcl / 100; if (fm > 5) nyxAaRR(s, bx, y + 11, fm, 6, 3, callMuted ? (uint16_t)T().danger : (uint16_t)T().good);
    y += 26;
  }
  if (showRoute) {
    char rt[40]; bool bad = false;
    if (act) {
      if (callAudioOut) snprintf(rt, sizeof(rt), "%s \xC2\xB7 %s", callIsLan ? "LAN" : "VoIP", aoSpk ? "Speaker" : "TWS");
      else { callCopy(rt, sizeof(rt), RPTR("Tanpa audio keluar (TWS?)")); bad = true; }
    } else callCopy(rt, sizeof(rt), callIsLan ? RPTR("LAN langsung") : "VoIP");
    String rts; callFit(s, rt, hw - 28, 1, rts);
    s.setTextSize(1);
    int tw = s.textWidth(rts.c_str()), cw = tw + 20;
    nyxAaRR(s, hcx - cw / 2, y, cw, 15, 7, bad ? blend565(T().bg, T().danger, 70) : blend565(T().bg, T().surface2, 200));
    s.setTextColor(bad ? (uint16_t)T().danger : (uint16_t)T().subtext);
    s.setCursor(hcx - tw / 2, y + 4); s.print(rts.c_str());
  }
  // tombol
  int cx, cy, r, w;
  if (ph == CPH_INCOMING) {
    callBtnGeom(0, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().danger); callHandset(s, cx, cy, r * 55 / 100, -90, 0xFFFF);
    callBtnLabel(s, cx, cy, r, RPTR("Tolak"));
    callBtnGeom(1, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().good);   callHandset(s, cx, cy, r * 55 / 100, 135, 0xFFFF);
    callBtnLabel(s, cx, cy, r, RPTR("Terima"));
  } else {
    callBtnGeom(0, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, callMuted ? (uint16_t)T().accent : (uint16_t)T().surface2);
    callIconMic(s, cx, cy, r * 6 / 10, callMuted, callMuted ? appIconInk(T().accent) : (uint16_t)T().text, (uint16_t)T().danger);
    callBtnLabel(s, cx, cy, r, callMuted ? "Unmute" : "Mute");
    callBtnGeom(1, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().surface2); callIconSpk(s, cx, cy, r * 6 / 10, -1, T().text);
    callBtnLabel(s, cx, cy, r, "Vol -");
    callBtnGeom(2, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().surface2); callIconSpk(s, cx, cy, r * 6 / 10, +1, T().text);
    callBtnLabel(s, cx, cy, r, "Vol +");
    callBtnGeom(3, cx, cy, r, w);
    nyxAaRR(s, cx - w / 2, cy - r + 2, w, r * 2, r, blend565(T().bg, 0x0000, 110));
    nyxAaRR(s, cx - w / 2, cy - r, w, r * 2, r, T().danger);
    const char* lb = ph == CPH_ENDING ? "..." : RPTR("Akhiri");
    s.setTextSize(1); s.setTextColor(0xFFFF);
    int tw = s.textWidth(lb), gw = 16 + 8 + tw, gx = cx - gw / 2;
    callHandset(s, gx + 8, cy, 8, -90, 0xFFFF);
    s.setCursor(gx + 24, cy - 4); s.print(lb);
  }
}

void drawCallOverlay(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false);
  callMarkU(75);
  callDrawInCall(s);
  callMarkU(76);
  drawToast(s);
}

'''

NEW_C = r'''static bool callSdHas(const char* p) { return sdReady && SD_MMC.exists(p); }

// ---------------------------------------------------------------------
//  PENGATURAN (CALL_UI_V2): baris label-kiri / nilai-kanan, tombol Simpan di baris bawah
//  baris 0..3 = akun / 0..2 = TURN, 4 = Siaga (hal. 1) atau Muat SD (hal. 2), 5 = TURN >, 6 = Muat SD (hal. 1)
// ---------------------------------------------------------------------
static void callCfgRowRect(int i, int& x, int& y, int& w, int& h) {
  bool wd = callWide();
  h = wd ? (SCR_H < 260 ? 26 : 32) : (SCR_H < 400 ? 28 : 32);
  int gap = wd ? 4 : 3, top = 50;
  if (callWide()) {
    int cw = (SCR_W - 24) / 2;
    w = cw;
    if (i < 4) { x = 8; y = top + i * (h + gap); } else { x = 16 + cw; y = top + (i - 4) * (h + gap); }
  } else {
    w = SCR_W - 16; x = 8;
    y = top + i * (h + gap) + (i >= 4 ? 8 : 0);
  }
}
static int callTurnSdIdx() { return callWide() ? 4 : 3; }      // baris "Muat SD" di halaman TURN
static void callSaveRect(int& x, int& y, int& w, int& h) { w = 92; h = BACK_H; x = SCR_W - 8 - w; y = backY(); }

static void callCfgRowDraw(LGFX_Sprite& s, int i, const char* label, const char* val, bool secret, bool chev) {
  int x, y, w, h; callCfgRowRect(i, x, y, w, h);
  iosRR(s, x, y, w, h, 10, T().surface);
  s.setTextSize(1); s.setTextColor(T().text);
  int ty = y + (h - 8) / 2;
  s.setCursor(x + 10, ty); s.print(label);
  int lw = s.textWidth(label);
  int rx = x + w - (chev ? 22 : 10);
  if (chev) iosChevron(s, x + w - 12, y + h / 2, T().subtext);
  if (secret) {
    int n = (int)strlen(val); if (n > 8) n = 8;
    if (n == 0) { s.setTextColor(T().subtext); s.setCursor(rx - s.textWidth("-"), ty); s.print("-"); return; }
    for (int k = 0; k < n; k++) nyxAaCircle(s, rx - 3 - k * 8, y + h / 2, 2, T().subtext);
    return;
  }
  String v; callFit(s, val[0] ? val : "-", w - 20 - lw - 12 - (chev ? 12 : 0), 1, v);
  s.setTextColor(T().subtext); s.setCursor(rx - s.textWidth(v.c_str()), ty); s.print(v.c_str());
}

static void callDrawSaveBtn(LGFX_Sprite& s) {
  int x, y, w, h; callSaveRect(x, y, w, h);
  nyxAaRR(s, x, y, w, h, h / 2, T().accent);
  s.setTextSize(1); s.setTextColor(appIconInk(T().accent));
  callCenter(s, x, w, y + (h - 8) / 2, RPTR("Simpan"));
}

static void callDrawCfgTitle(LGFX_Sprite& s, const char* t) {
  String ts; int sz = callFit(s, t, SCR_W - 16, 2, ts);
  s.setTextColor(T().text); s.setCursor(10, 26 + (sz == 2 ? 0 : 4)); s.print(ts.c_str());
  s.setCursor(11, 26 + (sz == 2 ? 0 : 4)); s.print(ts.c_str());
  s.setTextSize(1);
}

static void callDrawEdit(LGFX_Sprite& s) {
  callDrawCfgTitle(s, callPage == 2 ? RPTR("Telepon - TURN (relay)") : RPTR("Telepon - Pengaturan"));
  String lb; callFit(s, callCfgLabel(callCfgEdit), SCR_W - 20, 1, lb);
  s.setTextColor(T().subtext); s.setCursor(12, 50); s.print(lb.c_str());
  iosRR(s, 8, 64, SCR_W - 16, 34, 10, T().surface);
  nyxAaRRRing(s, 8, 64, SCR_W - 16, 34, 10, T().accent);
  String v = *callCfgField(callCfgEdit);
  int maxW = SCR_W - 16 - 20;
  int sz = 2; s.setTextSize(2);
  if (s.textWidth(v.c_str()) + 14 > maxW) { sz = 1; s.setTextSize(1); }
  while (v.length() > 3 && s.textWidth(v.c_str()) + 14 > maxW) v = v.substring(1);
  v += "_";
  s.setTextColor(T().text); s.setCursor(18, 64 + (34 - sz * 8) / 2); s.print(v.c_str());
  s.setTextSize(1);
  s.setTextColor(T().subtext); callWrap(s, 12, 106, SCR_W - 24, RPTR("Ketuk area di atas keyboard = selesai"), T().subtext);
}

static void callDrawTurnCfg(LGFX_Sprite& s) {
  callDrawCfgTitle(s, RPTR("Telepon - TURN (relay)"));
  const char* L[3] = { "Server", "Username", "Password" };
  for (int i = 0; i < 3; i++) callCfgRowDraw(s, i, L[i], callCfgField(4 + i)->c_str(), i == 2, true);
  bool sd = callSdHas("/turn.cfg");
  int x, y, w, h;
  int si = callTurnSdIdx();
  callCfgRowRect(si, x, y, w, h);
  if (sd) {
    iosRR(s, x, y, w, h, 10, T().surface2);
    s.setTextColor(T().accent); callCenter(s, x, w, y + (h - 8) / 2, RPTR("Muat SD"));
  }
  int hx = x + 2, hwid = w - 4, hy = y + (sd ? h + 10 : 2);
  hy = callWrap(s, hx, hy, hwid, RPTR("Dipakai utk panggilan lewat internet."), T().subtext);
  hy = callWrap(s, hx, hy, hwid, RPTR("Kosong = pakai STUN saja."), T().subtext);
  if (turnErr[0]) callWrap(s, hx, hy + 2, hwid, turnErr, T().danger);
}

static void callDrawCfg(LGFX_Sprite& s) {
  if (callCfgEdit >= 0) { callDrawEdit(s); return; }
  if (callPage == 2) { callDrawTurnCfg(s); return; }
  callDrawCfgTitle(s, RPTR("Telepon - Pengaturan"));
  const char* L[4] = { "Server", "Username", "Password", RPTR("Nama tampilan") };
  for (int i = 0; i < 4; i++) callCfgRowDraw(s, i, L[i], callCfgField(i)->c_str(), i == 2, true);
  int x, y, w, h;
  callCfgRowRect(4, x, y, w, h);                                   // Siaga: saklar
  iosRR(s, x, y, w, h, 10, T().surface);
  String lb; callFit(s, RPTR("Siaga terima panggilan"), w - 20 - 38, 1, lb);
  s.setTextSize(1); s.setTextColor(T().text); s.setCursor(x + 10, y + (h - 8) / 2); s.print(lb.c_str());
  int sx = x + w - 10 - 30, sy = y + (h - 16) / 2;
  nyxAaRR(s, sx, sy, 30, 16, 8, callStandby ? (uint16_t)T().good : blend565(T().surface2, T().text, 40));
  nyxAaCircle(s, callStandby ? sx + 22 : sx + 8, sy + 8, 6, 0xFFFF);
  callCfgRowDraw(s, 5, "TURN (relay)", callTurnHostS.c_str(), false, true);
  if (callSdHas("/sip.cfg")) {
    callCfgRowRect(6, x, y, w, h);
    iosRR(s, x, y, w, h, 10, T().surface2);
    s.setTextColor(T().accent); callCenter(s, x, w, y + (h - 8) / 2, RPTR("Muat SD"));
  }
}

static void callRegText(char* out, int cap, uint16_t* col) {
  uint8_t st = callRegState;
  if (st == 2) { callCopy(out, cap, "VoIP terdaftar"); *col = T().good; }
  else if (st == 1) { callCopy(out, cap, "mendaftar..."); *col = T().subtext; }
  else if (st == 3) { char m[40]; callLock(); callCopy(m, sizeof(m), callRegMsg); callUnlock(); callCopy(out, cap, m); *col = T().danger; }
  else { callCopy(out, cap, callSipConfigured() ? RPTR("VoIP belum aktif") : RPTR("VoIP belum diatur")); *col = T().subtext; }
}

// ---------------------------------------------------------------------
//  LAYAR UTAMA TELEPON (CALL_UI_V2)
// ---------------------------------------------------------------------
#define CALL_TITLE_Y 26
#define CALL_CHIP_Y  44
#define CALL_SEG_Y   62
#define CALL_FIELD_Y 88
#define CALL_BTN_Y   124

static void callIdleGeom(int& lx, int& lw, int& rx, int& rw, int& hdrY, int& rowY0, int& pitch, int& cardH) {
  lx = 8;
  if (callWide()) { lw = SCR_W / 2 - 12; rx = SCR_W / 2 + 4; rw = SCR_W - rx - 8; hdrY = 30; rowY0 = 44; }
  else { lw = SCR_W - 16; rx = 8; rw = SCR_W - 16; hdrY = 160; rowY0 = 172; }
  bool big = SCR_H >= 400;
  pitch = big ? 46 : 32; cardH = big ? 42 : 28;
}
static int callIdleSlots() {
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  int bot = kbVisible ? kbY() - 2 : backY() - 4;
  int n = (bot - rowY0 + (pitch - cardH)) / pitch;
  return n < 0 ? 0 : n;
}

static void callDrawRow(LGFX_Sprite& s, int x, int y, int w, int h, const char* full, const char* sub) {
  iosRR(s, x, y, w, h, 12, T().surface);
  int r = h / 2 - 5; if (r < 9) r = 9;
  int cyr = y + h / 2;
  callAvatar(s, x + 8 + r, cyr, r, full);
  int bx = x + w - 8 - r;
  nyxAaCircle(s, bx, cyr, r, T().good);
  callHandset(s, bx, cyr, r * 6 / 10 + 1, 135, 0xFFFF);
  int tx = x + 8 + 2 * r + 8, maxW = bx - r - 6 - tx;
  char nm[44]; const char* at = strchr(full, '@');
  int nl = at ? (int)(at - full) : (int)strlen(full); if (nl > 43) nl = 43;
  memcpy(nm, full, nl); nm[nl] = 0;
  const char* dom = at ? at + 1 : (sub ? sub : "");
  String a; callFit(s, nm, maxW, 1, a);
  s.setTextSize(1); s.setTextColor(T().text);
  if (dom[0]) {
    s.setCursor(tx, cyr - 9); s.print(a.c_str());
    String b; callFit(s, dom, maxW, 1, b);
    s.setTextColor(T().subtext); s.setCursor(tx, cyr + 2); s.print(b.c_str());
  } else { s.setCursor(tx, cyr - 4); s.print(a.c_str()); }
}

void drawCall(LGFX_Sprite& s) {
  callMarkU(41);
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);
  if (!callTask && !callEnsureInit()) {
    s.setTextColor(T().danger); s.setCursor(10, 60); s.print(RPTR("RAM tidak cukup untuk Telepon."));
    drawBack(s); drawToast(s); return;
  }
  if (callPhase != CPH_IDLE) {
    callDrawInCall(s);
    drawBack(s); drawToast(s); return;
  }
  if (callPage == 1 || callPage == 2) {
    callDrawCfg(s);
    if (kbVisible) drawKb(s); else { callDrawSaveBtn(s); drawBack(s); }
    drawToast(s); return;
  }
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  // judul + tombol pengaturan
  s.setTextSize(2); uiText(s, lx + 2, CALL_TITLE_Y, RPTR("Telepon"), T().text, false, true); s.setTextSize(1);
  nyxAaCircle(s, lx + lw - 14, CALL_TITLE_Y + 8, 14, T().surface2);
  nyxAaCircleRing(s, lx + lw - 14, CALL_TITLE_Y + 8, 14, blend565(T().surface2, T().text, 40));
  callIconGear(s, lx + lw - 14, CALL_TITLE_Y + 8, 8, T().accent, T().surface2);
  // chip status
  char ct[44]; uint16_t cc;
  if (callTab == 0) callRegText(ct, sizeof(ct), &cc);
  else if (!wifiConnected) { callCopy(ct, sizeof(ct), RPTR("WiFi belum tersambung")); cc = T().danger; }
  else { char ip[20]; callLocalIpStr(ip, sizeof(ip)); snprintf(ct, sizeof(ct), "IP: %s", ip); cc = T().good; }
  String cs; callFit(s, ct, lw - 44, 1, cs);
  int cw = s.textWidth(cs.c_str()) + 24;
  nyxAaRR(s, lx, CALL_CHIP_Y, cw, 15, 7, blend565(T().bg, cc, 45));
  nyxAaCircle(s, lx + 9, CALL_CHIP_Y + 7, 2, cc);
  s.setTextColor(cc == (uint16_t)T().danger ? (uint16_t)T().danger : (uint16_t)T().text); s.setCursor(lx + 17, CALL_CHIP_Y + 4); s.print(cs.c_str());
  // tab VoIP | LAN
  nyxAaRR(s, lx, CALL_SEG_Y, lw, 22, 11, T().surface2);
  int sw = (lw - 4) / 2;
  nyxAaRR(s, lx + 2 + callTab * sw, CALL_SEG_Y + 2, sw, 18, 9, T().accent);
  for (int i = 0; i < 2; i++) {
    s.setTextColor(callTab == i ? appIconInk(T().accent) : (uint16_t)T().subtext);
    callCenter(s, lx + 2 + i * sw, sw, CALL_SEG_Y + 7, i == 0 ? "VoIP" : (sw >= 96 ? RPTR("LAN (antar HP)") : "LAN"));
  }
  // kolom tujuan
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  bool typing = kbVisible && kbTarget == tgt;
  bool has = tgt->length() > 0;
  iosRR(s, lx, CALL_FIELD_Y, lw, 32, 10, T().surface);
  if (typing) nyxAaRRRing(s, lx, CALL_FIELD_Y, lw, 32, 10, T().accent);
  int maxVW = lw - 20 - (has ? 24 : 0);
  String cap; callFit(s, callTab == 0 ? RPTR("Tujuan (user, user@domain)") : RPTR("Alamat IP HP lawan"), maxVW, 1, cap);
  s.setTextColor(T().subtext); s.setCursor(lx + 10, CALL_FIELD_Y + 4); s.print(cap.c_str());
  String v = *tgt; if (typing) v += "_";
  if (!v.length()) {
    s.setTextColor(T().subtext); s.setCursor(lx + 10, CALL_FIELD_Y + 18);
    s.print(callTab == 0 ? RPTR("ketuk untuk mengetik...") : "mis. 192.168.1.23");
  } else {
    int sz = 2; s.setTextSize(2);
    if (s.textWidth(v.c_str()) > maxVW) { sz = 1; s.setTextSize(1); }
    if (typing) { while (v.length() > 3 && s.textWidth(v.c_str()) > maxVW) v = v.substring(1); }       // mengetik: ujung kanan tetap terlihat
    else if (sz == 1) { String vv; callFit(s, v.c_str(), maxVW, 1, vv); v = vv; }                      // diam: awal tetap terlihat
    s.setTextColor(T().text); s.setCursor(lx + 10, CALL_FIELD_Y + (sz == 2 ? 14 : 18)); s.print(v.c_str());
    s.setTextSize(1);
  }
  if (has) {                                                        // tombol hapus
    int bx = lx + lw - 14, by = CALL_FIELD_Y + 16;
    nyxAaCircle(s, bx, by, 8, blend565(T().surface2, T().text, 40));
    s.drawLine(bx - 3, by - 3, bx + 3, by + 3, T().text); s.drawLine(bx - 3, by + 3, bx + 3, by - 3, T().text);
  }
  // tombol Panggil
  nyxAaRR(s, lx, CALL_BTN_Y, lw, 30, 15, has ? (uint16_t)T().good : blend565(T().surface2, T().good, 80));
  { const char* lb = RPTR("Panggil"); s.setTextColor(has ? (uint16_t)0xFFFF : (uint16_t)T().subtext);
    int gw = 16 + 8 + s.textWidth(lb), gx = lx + (lw - gw) / 2;
    callHandset(s, gx + 8, CALL_BTN_Y + 15, 9, 135, has ? (uint16_t)0xFFFF : (uint16_t)T().subtext);
    s.setCursor(gx + 24, CALL_BTN_Y + 11); s.print(lb); }
  // daftar: riwayat (VoIP) atau perangkat ditemukan (LAN)
  s.setTextColor(T().subtext); s.setCursor(rx + 4, hdrY);
  s.print(callTab == 0 ? RPTR("Terakhir") : RPTR("Perangkat"));
  int slots = callIdleSlots();
  int items = callTab == 0 ? callRecentCount : 1 + callScanCount;
  if (items > slots) items = slots;
  for (int k = 0; k < items; k++) {
    int ry = rowY0 + k * pitch;
    if (callTab == 0) callDrawRow(s, rx, ry, rw, cardH, callRecent[k], "");
    else if (k == 0) {
      iosRR(s, rx, ry, rw, cardH, 12, T().surface2);
      String sl; callFit(s, callScanBusy ? RPTR("Mencari...") : RPTR("Cari HP NyxOS di jaringan"), rw - 20, 1, sl);
      s.setTextColor(T().accent); callCenter(s, rx, rw, ry + (cardH - 8) / 2, sl.c_str());
    } else callDrawRow(s, rx, ry, rw, cardH, callScanName[k - 1], callScanIp[k - 1]);
  }
  if (callTab == 0 && callRecentCount == 0) callWrap(s, rx + 4, rowY0 + 4, rw - 8, RPTR("Belum ada riwayat panggilan"), T().subtext);
  if (callShowDiag && !kbVisible && callDbgHist[0][0]) {
    int y0 = backY() - 6 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 6 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 6; i++) {
      if (!callDbgHist[5 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[5 - i]);
    }
  }
  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

'''

NEW_D = r'''static bool callHitCirc(int x, int y, int cx, int cy, int r) {
  long dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= (long)(r + 6) * (r + 6);
}
static bool callHitRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx - 2 && x <= rx + rw + 2 && y >= ry - 2 && y <= ry + rh + 2;
}

static bool callInCallTouch(int x, int y) {      // true kalau sentuhan ditangani
  uint8_t ph = callPhase;
  int cx, cy, r, w;
  if (ph == CPH_INCOMING) {
    if (callBtnGeom(0, cx, cy, r, w) && callHitCirc(x, y, cx, cy, r)) { callPost(CRQ_REJECT, ""); return true; }
    if (callBtnGeom(1, cx, cy, r, w) && callHitCirc(x, y, cx, cy, r)) { callPost(CRQ_ANSWER, ""); return true; }
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (!callBtnGeom(i, cx, cy, r, w) || !callHitCirc(x, y, cx, cy, r)) continue;
    if (i == 0) { callMuted = !callMuted; needRedraw = true; }
    else {
      int v = musExtGetVol() + (i == 1 ? -8 : 8);
      if (v < 0) v = 0;
      if (v > 127) v = 127;
      musExtSetVol(v);
      char b[24]; snprintf(b, sizeof(b), "Volume %d%%", v * 100 / 127);
      showToast(b, 700);
    }
    return true;
  }
  if (callBtnGeom(3, cx, cy, r, w) && callHitRect(x, y, cx - w / 2, cy - r, w, r * 2)) { callPost(CRQ_HANGUP, ""); return true; }
  return false;
}

void callOverlayTouch(bool newT, int x, int y) {
  if (!newT) return;
  callInCallTouch(x, y);
  needRedraw = true;
}

void callTouch(int x, int y, bool held, bool isNew) {
  callMarkU(42);
  if (kbVisible) {
    if (!isNew) return;
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; callCfgEdit = -1; }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }
  if (!isNew) return;
  if (callPhase != CPH_IDLE) {
    if (isBack(x, y)) { navBack(); return; }          // panggilan tetap jalan di latar
    callInCallTouch(x, y);
    return;
  }
  int rx0, ry0, rw0, rh0;
  if (callPage == 1 || callPage == 2) {
    if (isBack(x, y)) { callCfgSave(); callPage = (callPage == 2) ? 1 : 0; needRedraw = true; return; }
    callSaveRect(rx0, ry0, rw0, rh0);
    if (callHitRect(x, y, rx0, ry0, rw0, rh0)) {
      callCfgSave();
      if (callPage == 2) { callPage = 1; showToast(RPTR("TURN tersimpan"), 1200); }
      else { callPage = 0; showToast(RPTR("Tersimpan"), 1200); }
      needRedraw = true; return;
    }
    int nf = callPage == 2 ? 3 : 4;
    for (int i = 0; i < nf; i++) {
      callCfgRowRect(i, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { int f = (callPage == 2) ? 4 + i : i; callCfgEdit = f; callStartTyping(callCfgField(f)); return; }
    }
    if (callPage == 1) {
      callCfgRowRect(4, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { callStandby = !callStandby; needRedraw = true; return; }
      callCfgRowRect(5, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { callCfgSave(); callPage = 2; needRedraw = true; return; }
      callCfgRowRect(6, rx0, ry0, rw0, rh0);
      if (callSdHas("/sip.cfg") && callHitRect(x, y, rx0, ry0, rw0, rh0)) {
        if (callCfgFromSd()) showToast(RPTR("Dimuat dari /sip.cfg"), 1600);
        else showToast(RPTR("/sip.cfg tdk valid (host= user= pass=)"), 2600);
        needRedraw = true;
      }
    } else {
      callCfgRowRect(callTurnSdIdx(), rx0, ry0, rw0, rh0);
      if (callSdHas("/turn.cfg") && callHitRect(x, y, rx0, ry0, rw0, rh0)) {
        if (callTurnCfgFromSd()) showToast(RPTR("Dimuat dari /turn.cfg"), 1600);
        else showToast(RPTR("/turn.cfg tdk valid (host= user= pass=)"), 2600);
        needRedraw = true;
      }
    }
    return;
  }
  if (isBack(x, y)) { navBack(); return; }
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  bool inLeft = x >= lx - 2 && x <= lx + lw + 2;
  if (inLeft && x >= lx + lw - 32 && y >= 16 && y <= 52) { callPage = 1; needRedraw = true; return; }       // pengaturan
  if (inLeft && y >= CALL_CHIP_Y - 3 && y <= CALL_CHIP_Y + 18) { callShowDiag = !callShowDiag; needRedraw = true; return; }  // ketuk status = diagnosa
  if (inLeft && y >= CALL_SEG_Y - 2 && y <= CALL_SEG_Y + 24) { callTab = (x < lx + lw / 2) ? 0 : 1; needRedraw = true; return; }
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  if (inLeft && y >= CALL_FIELD_Y - 2 && y <= CALL_FIELD_Y + 34) {
    if (tgt->length() > 0 && x >= lx + lw - 28) { *tgt = ""; needRedraw = true; return; }
    callStartTyping(tgt); return;
  }
  if (inLeft && y >= CALL_BTN_Y - 2 && y <= CALL_BTN_Y + 32) { callDoDial(*tgt, callTab == 0); if (callTab == 1) callCfgSave(); return; }
  if (x >= rx - 2 && x <= rx + rw + 2 && y >= rowY0) {
    int k = (y - rowY0) / pitch;
    if ((y - rowY0) % pitch > cardH + 2) return;
    if (k < 0 || k >= callIdleSlots()) return;
    if (callTab == 0) {
      if (k < callRecentCount) { callTargetS = callRecent[k]; callDoDial(callTargetS, true); }
    } else if (k == 0) { callPost(CRQ_SCAN, ""); needRedraw = true; }
    else if (k - 1 < callScanCount) { callLanIpS = callScanIp[k - 1]; callDoDial(callLanIpS, false); }
  }
}
'''

# --- state baru + animasi redraw saat berdering ---
s = once(s, "static uint32_t callPollMs = 0, callTimerMs = 0;",
            "static uint32_t callPollMs = 0, callTimerMs = 0;\nstatic uint32_t callAnimMs = 0;           // CALL_UI_V2: redraw gelombang dering\nstatic bool     callShowDiag = false;     // CALL_UI_V2: ketuk chip status = log diagnosa")
s = once(s, "  if (visible && callScanBusy && now - callTimerMs > 400) { callTimerMs = now; needRedraw = true; }\n}",
            "  if (visible && callScanBusy && now - callTimerMs > 400) { callTimerMs = now; needRedraw = true; }\n"
            "  if ((ph == CPH_DIAL || ph == CPH_RINGBACK || ph == CPH_INCOMING) && (callOvl || (!callBanner && curScreen() == SCR_CALL)) && now - callAnimMs > 110) {\n"
            "    callAnimMs = now; needRedraw = true;            // CALL_UI_V2: gelombang berdenyut di sekitar avatar\n  }\n}")

# --- empat wilayah kode ---
s = cut(s, "static void callBannerRect(int& x, int& y, int& w, int& h) {", "bool callOverlayActive() { return callOvl; }", NEW_A)
s = cut(s, "static void callCenter(LGFX_Sprite& s, int x, int w, int y, const char* t) {", "static const char* callCfgLabel(int i) {", NEW_B)
s = cut(s, "static void callDrawTurnCfg(LGFX_Sprite& s) {", "// =====================================================================\n//  SENTUHAN", NEW_C)
s = cut(s, "static bool callInCallTouch(int x, int y) {", None, NEW_D)
open(FN, "w", encoding="utf-8").write(s)
print("OK: Telepon UI v2 terpasang ->", FN)

# --- terjemahan ---
PF = os.path.join("tools", "i18n_pairs.txt")
if os.path.exists(PF):
    t = open(PF, encoding="utf-8").read()
    add = [l for l in ("Terakhir ::: Recent", "Perangkat ::: Devices") if (l.split(" ::: ")[0] + " ::: ") not in t]
    if add:
        if not t.endswith("\n"): t += "\n"
        open(PF, "w", encoding="utf-8").write(t + "# --- Telepon UI v2 ---\n" + "\n".join(add) + "\n")
        r = subprocess.run([sys.executable, os.path.join("tools", "gen_i18n.py")])
        print("terjemahan ditambah:", add, "| gen_i18n rc =", r.returncode)

# --- CHANGELOG ---
CF = "CHANGELOG.md"
if os.path.exists(CF):
    c = open(CF, encoding="utf-8").read()
    line = ("- Improved: new look and easier use for the Phone app (CALL_UI_V2). Main screen with a status chip, pill tabs VoIP / LAN, a bigger target field with a clear button, "
            "a green Call button and recent calls / found phones as cards with a coloured letter avatar and a round call button. The call screen has a big avatar (green ring = connected, "
            "red ring = muted, pulsing waves while ringing), a large timer, the Them / You level bars, an audio route chip, round icon buttons (Mute, Vol -, Vol +) and an End pill; "
            "an incoming call shows two round Decline / Answer buttons. The incoming-call banner and the settings pages got the same style (password shown as dots, Save in the bottom bar). "
            "Landscape uses two columns. Tap the status chip to show or hide the diagnostic log (it used to be always on). Only drawing and touch code changed, not SIP, audio or TURN. "
            "Checked with a PC preview only, not yet on a device. Script: `tools/patches/apply_call_ui_patch.py`\n")
    if "CALL_UI_V2" not in c and "## Unreleased\n" in c:
        open(CF, "w", encoding="utf-8").write(c.replace("## Unreleased\n", "## Unreleased\n" + line, 1))
        print("CHANGELOG diperbarui")

# --- README patches ---
RF = os.path.join("tools", "patches", "README.md")
if os.path.exists(RF):
    r = open(RF, encoding="utf-8").read()
    row = "| `apply_call_ui_patch.py` | `CALL_UI_V2` | History (Phone app redesign: dialer, call screen, banner, settings) |\n"
    if "CALL_UI_V2" not in r:
        anchor = "| `apply_webfm_upload_ux.py` | `WEBFM_UPLOAD_UX_V1` | History (drag-and-drop upload queue) |\n"
        if anchor in r:
            open(RF, "w", encoding="utf-8").write(r.replace(anchor, anchor + row, 1))
            print("tools/patches/README.md diperbarui")

# --- docs ---
DF = os.path.join("docs", "features", "phone-calls.md")
if os.path.exists(DF):
    d = open(DF, encoding="utf-8").read()
    if "Screens**" not in d:
        d = d.replace("Open the Phone app, choose *Set*, and fill in", "Open the Phone app, tap the gear button (top right), and fill in")
        d = d.replace("   - The receiving device must be on the same WiFi, and the **Ready to receive calls** option must be on.\n",
            "   - The receiving device must be on the same WiFi, and the **Ready to receive calls** option must be on.\n"
            "4. **Screens**\n"
            "   - Main screen: status chip (tap it to show or hide the diagnostic log), VoIP / LAN tabs, target field with a clear (x) button, **Call**, and recent calls or found phones as cards (tap a card to call).\n"
            "   - During a call: big avatar (green ring = connected, red ring = muted), timer, *Them* / *You* level bars, audio route chip, **Mute**, **Vol -**, **Vol +** and **End**. An incoming call shows round **Decline** and **Answer** buttons; with an unlocked phone it appears as a small banner first (tap the banner body for the full screen).\n"
            "   - The layout adapts to landscape (two columns) and portrait.\n", 1)
        open(DF, "w", encoding="utf-8").write(d)
        print("docs/features/phone-calls.md diperbarui")
