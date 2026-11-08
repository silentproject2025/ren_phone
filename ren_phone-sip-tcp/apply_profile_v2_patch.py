#!/usr/bin/env python3
# PROFIL v2: foto lebih besar + bingkai & warna (5 gaya x 10 warna) + statistik pemakaian
# (total waktu aktif, jumlah app dibuka, 5 app paling sering). Disimpan di NVS (namespace "nyxowner").
# Mengubah: owner_renphone.ino (utama) + phone.ino (1 forward-decl + 1 baris hook di navPush).
# Aman diulang. Semua anchor dicek dulu; kalau ada yang tidak ketemu, TIDAK ada file yang diubah.
import sys, os
for f in ('owner_renphone.ino','phone.ino'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini"%f); sys.exit(1)
o=open('owner_renphone.ino',encoding='utf-8').read()
p=open('phone.ino',encoding='utf-8').read()
if 'OWNER-V2' in o:
    print("Profil v2 sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

# ---------- 1) state + statistik + bingkai (disisipkan setelah state lama) ----------
BLOCK = r'''static unsigned long ownerDelArmUntil = 0;

// OWNER-V2 ------------------------------------------------------------------
// Bingkai foto + statistik pemakaian. Semua disimpan di NVS "nyxowner".
#include "esp_timer.h"
#define OWNER_PAL_N     10        // 0 = warna aksen tema, 1..9 = warna tetap
#define OWNER_STYLE_N   5
#define OWNER_STAT_MAX  64        // indeks = nilai enum Screen (enum hanya bertambah di akhir -> stabil)
#define OWN_RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static const uint16_t ownerPal[OWNER_PAL_N - 1] = {
  OWN_RGB(255,95,87), OWN_RGB(255,159,10), OWN_RGB(255,214,10), OWN_RGB(48,209,88), OWN_RGB(100,210,255),
  OWN_RGB(10,132,255), OWN_RGB(191,90,242), OWN_RGB(255,105,180), OWN_RGB(240,240,240) };
static const char* const ownerStyleName[OWNER_STYLE_N] = { "Tipis", "Solid", "Ganda", "Titik", "Pelangi" };

static uint8_t  ownerFrameStyle = 0;
static uint8_t  ownerFrameCol   = 0;
static int      ownerView       = 0;        // 0 utama, 1 bingkai & warna, 2 statistik
static unsigned long ownerStatArmUntil = 0;

static uint32_t ownerCnt[OWNER_STAT_MAX];   // berapa kali tiap app dibuka (per nilai enum Screen)
static uint32_t ownerUpBase = 0;            // total detik aktif sebelum boot ini
static bool     ownerStatLoaded = false;
static bool     ownerStatDirty  = false;
static unsigned long ownerStatLastSave = 0;

static uint32_t ownerSessionSec() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }
static uint32_t ownerUpTotal()    { return ownerUpBase + ownerSessionSec(); }

static void ownerStatsEnsure() {
  if (ownerStatLoaded) return;
  ownerStatLoaded = true;
  memset(ownerCnt, 0, sizeof(ownerCnt));
  Preferences pr; pr.begin("nyxowner", true);
  ownerUpBase = pr.getUInt("upsec", 0);
  if (pr.getBytesLength("cnt") == sizeof(ownerCnt)) pr.getBytes("cnt", ownerCnt, sizeof(ownerCnt));
  pr.end();
}

static void ownerStatsSave() {
  Preferences pr;
  if (!pr.begin("nyxowner", false)) return;
  pr.putUInt("upsec", ownerUpTotal());
  pr.putBytes("cnt", ownerCnt, sizeof(ownerCnt));
  pr.end();
  ownerStatDirty = false;
  ownerStatLastSave = millis();
}

// Dipanggil dari navPush() tiap app dibuka (hook di phone.ino).
void ownerStatsOpen(int id) {
  if (id <= 0 || id >= OWNER_STAT_MAX || id == (int)SCR_OWNER) return;
  ownerStatsEnsure();
  ownerCnt[id]++;
  ownerStatDirty = true;
}

// Dipanggil tiap putaran loop() lewat ownerLoopPoll() -- hanya cek waktu, menulis NVS paling sering 2x/menit.
static void ownerStatsTick() {
  ownerStatsEnsure();
  Screen sc = curScreen();
  if (sc == SCR_NES || sc == SCR_DOOM) return;               // jangan menulis flash saat emulator/game berat jalan
  unsigned long now = millis();
  if ((ownerStatDirty && now - ownerStatLastSave > 30000UL) || now - ownerStatLastSave > 300000UL) ownerStatsSave();
}

static void ownerFrameSave() {
  Preferences pr;
  if (!pr.begin("nyxowner", false)) return;
  pr.putUChar("fstyle", ownerFrameStyle);
  pr.putUChar("fcol", ownerFrameCol);
  pr.end();
}

static uint16_t ownerFrameColor() {
  if (ownerFrameCol == 0 || ownerFrameCol >= OWNER_PAL_N) return T().accent;
  return ownerPal[ownerFrameCol - 1];
}

static uint16_t ownerHue565(int deg) {
  deg %= 360; if (deg < 0) deg += 360;
  int sec = deg / 60, f = (deg % 60) * 255 / 60, r, g, b;
  switch (sec) {
    case 0: r = 255;     g = f;       b = 0;       break;
    case 1: r = 255 - f; g = 255;     b = 0;       break;
    case 2: r = 0;       g = 255;     b = f;       break;
    case 3: r = 0;       g = 255 - f; b = 255;     break;
    case 4: r = f;       g = 0;       b = 255;     break;
    default: r = 255;    g = 0;       b = 255 - f; break;
  }
  return OWN_RGB(r, g, b);
}

// Bingkai digambar DI DALAM jari-jari r (ukuran total avatar tidak berubah di Pengaturan/About).
static void ownerDrawFrame(LGFX_Sprite& s, int cx, int cy, int r) {
  uint16_t col = ownerFrameColor();
  int t = (r >= 40) ? 3 : (r >= 24 ? 2 : 1);
  switch (ownerFrameStyle) {
    case 1:                                                    // Solid
      for (int i = 0; i < t; i++) s.drawCircle(cx, cy, r - i, col);
      break;
    case 2:                                                    // Ganda: cincin tebal + cincin tipis di dalam
      s.drawCircle(cx, cy, r, col); s.drawCircle(cx, cy, r - 1, col);
      if (r >= 16) s.drawCircle(cx, cy, r - t - 3, blend565(T().bg, col, 200));
      break;
    case 3: {                                                  // Titik
      int n = r * 9 / 10; if (n < 14) n = 14; if (n > 40) n = 40;
      int dr = (r >= 30) ? 2 : 1;
      for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        s.fillCircle(cx + (int)lroundf(cosf(a) * (r - 2)), cy + (int)lroundf(sinf(a) * (r - 2)), dr, col);
      }
      break;
    }
    case 4: {                                                  // Pelangi (abaikan pilihan warna)
      int n = r * 5; if (n < 48) n = 48; if (n > 130) n = 130;
      for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        s.fillCircle(cx + (int)lroundf(cosf(a) * (r - 1)), cy + (int)lroundf(sinf(a) * (r - 1)), 1,
                     ownerHue565(i * 360 / n));
      }
      break;
    }
    default:                                                   // Tipis (gaya lama)
      s.drawCircle(cx, cy, r, blend565(T().bg, T().text, 70));
  }
}
// ---------------------------------------------------------------------------'''
o=rep(o,"static unsigned long ownerDelArmUntil = 0;",BLOCK,"state")

# ---------- 2) muat gaya & warna bingkai ----------
o=rep(o,'  String n = p.getString("name", "");\n  p.end();',
 '  String n = p.getString("name", "");\n  ownerFrameStyle = p.getUChar("fstyle", 0);\n  ownerFrameCol   = p.getUChar("fcol", 0);\n  if (ownerFrameStyle >= OWNER_STYLE_N) ownerFrameStyle = 0;\n  if (ownerFrameCol >= OWNER_PAL_N) ownerFrameCol = 0;\n  p.end();',"ensureLoaded")

# ---------- 3) bingkai dipakai di ownerDrawAvatar (foto & inisial) ----------
o=rep(o,"    s.drawCircle(cx, cy, r, blend565(T().bg, T().text, 70));     // cincin tipis: merapikan tepi\n",
 "    ownerDrawFrame(s, cx, cy, r);                                 // bingkai pilihan (gaya Tipis = cincin tipis lama)\n","avatar ring")
o=rep(o,"    s.print(ini);\n    s.setTextSize(1);\n  }\n}",
 "    s.print(ini);\n    s.setTextSize(1);\n    ownerDrawFrame(s, cx, cy, r);\n  }\n}","avatar initial")

# ---------- 4) loop poll: tick statistik ----------
o=rep(o,"void ownerLoopPoll() {\n  if (!ownerPickDone) return;","void ownerLoopPoll() {\n  ownerStatsTick();\n  if (!ownerPickDone) return;","loopPoll")

# ---------- 5) ganti layar Profil (dari ownerGeom sampai akhir file) ----------
TAIL = r'''// Layout: semua statis tanpa struct/parameter (aman dari bug prototype arduino-cli).
static int og_acx, og_acy, og_ar, og_nameY, og_nameMaxW, og_bx, og_by0, og_bw, og_bw2, og_bh, og_gap;

static void ownerLayout() {
  og_gap = 6;
  if (SCR_W > SCR_H) {                          // landscape: foto besar di kiri, tombol 2 kolom di kanan
    int leftW = 140;
    og_ar = 48; og_acx = 8 + leftW / 2; og_acy = 100;
    og_nameY = og_acy + og_ar + 8; og_nameMaxW = leftW - 4;
    og_bx = 8 + leftW + 6; og_bw2 = SCR_W - og_bx - 8; og_by0 = 52; og_bh = 34; og_gap = 8;
  } else {                                      // portrait: foto di atas, tombol 2 kolom di bawah
    og_ar = 40; og_acx = SCR_W / 2; og_acy = 80;
    og_nameY = og_acy + og_ar + 6; og_nameMaxW = SCR_W - 16;
    og_bx = 8; og_bw2 = SCR_W - 16; og_by0 = 166; og_bh = 32;
  }
  og_bw = (og_bw2 - 6) / 2;
}

// Tombol utama: 0 Pilih foto | 1 Ubah nama / 2 Bingkai | 3 Statistik / 4 Hapus foto (selebar penuh)
static void ownerBtnRect(int i, int& x, int& y, int& w, int& h) {
  ownerLayout();
  int row = (i < 4) ? i / 2 : 2, col = (i < 4) ? i % 2 : 0;
  h = og_bh; y = og_by0 + row * (og_bh + og_gap);
  if (i == 4) { x = og_bx; w = og_bw2; }
  else        { w = og_bw; x = og_bx + col * (og_bw + 6); }
}

static void ownerBtn(LGFX_Sprite& s, int x, int y, int w, int h, const char* lb, uint16_t tint, uint16_t txt) {
  drawGlassPanel(s, x, y, w, h, 10, tint, 170);
  s.setTextSize(1); s.setTextColor(txt);
  s.setCursor(x + w / 2 - s.textWidth(lb) / 2, y + h / 2 - 4);
  s.print(lb);
}

// ---- layar bingkai & warna ----
static int op_pcx, op_pcy, op_pr, op_rx, op_rw, op_cw, op_ch, op_cy0, op_cstride, op_lblY1, op_lblY2, op_sw, op_sy0, op_sstride;

static void ownerPickLayout() {
  if (SCR_W > SCR_H) {
    op_pcx = 70; op_pcy = 100; op_pr = 40;
    op_rx = 150; op_rw = SCR_W - op_rx - 8;
    op_lblY1 = 50;  op_cy0 = 62;  op_ch = 24; op_cstride = 30;
    op_lblY2 = 124; op_sy0 = 136; op_sw = 26; op_sstride = 32;
  } else {
    op_pcx = SCR_W / 2; op_pcy = 80; op_pr = 36;
    op_rx = 8; op_rw = SCR_W - 16;
    op_lblY1 = 122; op_cy0 = 134; op_ch = 26; op_cstride = 32;
    op_lblY2 = 198; op_sy0 = 210; op_sw = 34; op_sstride = 40;
  }
  op_cw = (op_rw - 12) / 3;
}

static void ownerDrawPick(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Bingkai & warna");
  ownerPickLayout();
  ownerDrawAvatar(s, op_pcx, op_pcy, op_pr);
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(op_rx, op_lblY1); s.print("Gaya");
  for (int i = 0; i < OWNER_STYLE_N; i++) {
    int x = op_rx + (i % 3) * (op_cw + 6), y = op_cy0 + (i / 3) * op_cstride;
    bool sel = (i == ownerFrameStyle);
    ownerBtn(s, x, y, op_cw, op_ch, ownerStyleName[i], sel ? T().accent : T().surface2, T().text);
  }
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(op_rx, op_lblY2); s.print(ownerFrameStyle == 4 ? "Warna (tidak dipakai di Pelangi)" : "Warna");
  for (int j = 0; j < OWNER_PAL_N; j++) {
    int x = op_rx + (j % 5) * (op_sw + 6), y = op_sy0 + (j / 5) * op_sstride;
    uint16_t c = (j == 0) ? T().accent : ownerPal[j - 1];
    s.fillRoundRect(x, y, op_sw, op_sw, 8, c);
    if (j == ownerFrameCol) s.drawRoundRect(x - 2, y - 2, op_sw + 4, op_sw + 4, 10, T().text);
  }
}

static void ownerTouchPick(int x, int y) {
  ownerPickLayout();
  for (int i = 0; i < OWNER_STYLE_N; i++) {
    int bx = op_rx + (i % 3) * (op_cw + 6), by = op_cy0 + (i / 3) * op_cstride;
    if (x >= bx && x <= bx + op_cw && y >= by && y <= by + op_ch) {
      ownerFrameStyle = (uint8_t)i; ownerFrameSave(); needRedraw = true; return;
    }
  }
  for (int j = 0; j < OWNER_PAL_N; j++) {
    int bx = op_rx + (j % 5) * (op_sw + 6), by = op_sy0 + (j / 5) * op_sstride;
    if (x >= bx - 2 && x <= bx + op_sw + 2 && y >= by - 2 && y <= by + op_sw + 2) {
      ownerFrameCol = (uint8_t)j; ownerFrameSave(); needRedraw = true; return;
    }
  }
}

// ---- layar statistik ----
static void ownerFmtDur(char* out, size_t n, uint32_t sec) {
  uint32_t d = sec / 86400UL; sec %= 86400UL;
  uint32_t h = sec / 3600UL;  sec %= 3600UL;
  uint32_t m = sec / 60UL, sd = sec % 60UL;
  if (d > 0)      snprintf(out, n, "%u hr %u j", (unsigned)d, (unsigned)h);
  else if (h > 0) snprintf(out, n, "%u j %u m", (unsigned)h, (unsigned)m);
  else            snprintf(out, n, "%u m %u d", (unsigned)m, (unsigned)sd);
}

static void ownerStatRow(LGFX_Sprite& s, int y, const char* k, const char* v) {
  s.setTextSize(1);
  s.setTextColor(T().subtext); s.setCursor(14, y); s.print(k);
  s.setTextColor(T().text);    s.setCursor(SCR_W - 14 - s.textWidth(v), y); s.print(v);
}

static void ownerStatResetRect(int& x, int& y, int& w, int& h) { w = 68; h = 20; x = SCR_W - w - 8; y = 27; }

static void ownerDrawStats(LGFX_Sprite& s) {
  ownerStatsEnsure();
  iosTitle(s, 8, 26, "Statistik");
  int rx, ry, rw, rh; ownerStatResetRect(rx, ry, rw, rh);
  bool armed = millis() < ownerStatArmUntil;
  ownerBtn(s, rx, ry, rw, rh, armed ? "Yakin?" : "Reset", armed ? T().danger : T().surface2, armed ? 0xFFFF : T().danger);
  if (armed) needRedraw = true;

  int top = 48, bot = backY() - 6;
  drawGlassPanel(s, 8, top, SCR_W - 16, bot - top, 12, T().surface2, 150);

  char a[24]; uint32_t total = 0;
  for (int i = 0; i < OWNER_STAT_MAX; i++) total += ownerCnt[i];
  ownerFmtDur(a, sizeof(a), ownerSessionSec());  ownerStatRow(s, top + 8,  "Aktif sesi ini", a);
  ownerFmtDur(a, sizeof(a), ownerUpTotal());     ownerStatRow(s, top + 24, "Total waktu aktif", a);
  snprintf(a, sizeof(a), "%u kali", (unsigned)total); ownerStatRow(s, top + 40, "Total app dibuka", a);
  s.drawFastHLine(14, top + 54, SCR_W - 28, T().divider);

  s.setTextSize(1); s.setTextColor(T().accent);
  s.setCursor(14, top + 60); s.print("Paling sering dibuka");

  int stride = (SCR_H > SCR_W) ? 24 : 18;
  int y0 = top + 74, nameW = 84, bx0 = 14 + nameW + 4, maxW = SCR_W - 14 - 40 - bx0;
  bool used[APP_COUNT];
  for (int i = 0; i < APP_COUNT; i++) used[i] = false;
  uint32_t best0 = 0;
  int rows = 0;
  for (int rk = 0; rk < 5; rk++) {
    int bi = -1; uint32_t bc = 0;
    for (int i = 0; i < APP_COUNT; i++) {
      int id = (int)apps[i].screen;
      if (used[i] || id <= 0 || id >= OWNER_STAT_MAX) continue;
      if (ownerCnt[id] > bc) { bc = ownerCnt[id]; bi = i; }
    }
    if (bi < 0) break;
    used[bi] = true;
    if (rk == 0) best0 = bc;
    int y = y0 + rk * stride;
    if (y + 12 > bot) break;
    char nm[24]; strncpy(nm, apps[bi].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    int n = (int)strlen(nm);
    s.setTextSize(1);
    while (n > 1 && s.textWidth(nm) > nameW) nm[--n] = 0;
    s.setTextColor(T().text); s.setCursor(14, y + 2); s.print(nm);
    int w = (int)((uint64_t)bc * (uint64_t)maxW / (uint64_t)best0); if (w < 4) w = 4;
    s.fillRoundRect(bx0, y, w, 10, 4, ownerFrameColor());
    char c[12]; snprintf(c, sizeof(c), "%u", (unsigned)bc);
    s.setTextColor(T().subtext); s.setCursor(SCR_W - 14 - s.textWidth(c), y + 2); s.print(c);
    rows++;
  }
  if (rows == 0) {
    const char* e = "Belum ada data -- buka beberapa app dulu";
    s.setTextColor(T().subtext);
    s.setCursor(SCR_W / 2 - s.textWidth(e) / 2, y0 + 12); s.print(e);
  }
}

static void ownerTouchStats(int x, int y) {
  int rx, ry, rw, rh; ownerStatResetRect(rx, ry, rw, rh);
  if (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh) {
    if (millis() < ownerStatArmUntil) {
      ownerStatArmUntil = 0;
      memset(ownerCnt, 0, sizeof(ownerCnt));
      ownerStatsSave();
      showToast("Hitungan app direset");
    } else {
      ownerStatArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk reset");
    }
    needRedraw = true;
  }
}

void ownerEnter() {
  ownerEnsureLoaded();
  ownerStatsEnsure();
  ownerEditing = false;
  ownerNameEdit = "";
  ownerDelArmUntil = 0;
  ownerStatArmUntil = 0;
  ownerView = 0;
}

void ownerExit() {
  if (ownerEditing) ownerCommitName();
  ownerDelArmUntil = 0;
  ownerStatArmUntil = 0;
  if (ownerStatDirty) ownerStatsSave();
}

void drawOwner(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);

  if (ownerEditing) {
    iosTitle(s, 8, 26, "Ubah nama");
    int fx = 8, fy = 50, fw = SCR_W - 16, fh = 36;
    drawGlassPanel(s, fx, fy, fw, fh, 10, T().surface2, 190);
    String t = ownerNameEdit + "|";
    int tsz = 2;
    s.setTextSize(2);
    if (s.textWidth(t.c_str()) > fw - 20) { tsz = 1; s.setTextSize(1); }
    s.setTextColor(T().text);
    s.setCursor(fx + 10, fy + fh / 2 - 4 * tsz);
    s.print(t.c_str());
    s.setTextSize(1);
    s.setTextColor(T().subtext);
    const char* hint = "Ketuk di luar keyboard = simpan";
    s.setCursor(SCR_W / 2 - s.textWidth(hint) / 2, fy + fh + 8);
    s.print(hint);
    drawKb(s);
    drawToast(s);
    return;
  }

  if (ownerView == 1) { ownerDrawPick(s);  drawBack(s); drawToast(s); return; }
  if (ownerView == 2) { ownerDrawStats(s); drawBack(s); drawToast(s); return; }

  iosTitle(s, 8, 26, "Profil");
  ownerLayout();
  ownerDrawAvatar(s, og_acx, og_acy, og_ar);

  char nm[OWNER_NAME_MAX + 1];
  strncpy(nm, ownerNameBuf, OWNER_NAME_MAX); nm[OWNER_NAME_MAX] = 0;
  s.setTextSize(2);
  int n = (int)strlen(nm);
  while (n > 1 && s.textWidth(nm) > og_nameMaxW) nm[--n] = 0;
  s.setTextColor(T().text);
  s.setCursor(og_acx - s.textWidth(nm) / 2, og_nameY);
  s.print(nm);
  s.setTextSize(1);
  s.setTextColor(T().subtext);
  const char* sub = "Pemilik perangkat";
  s.setCursor(og_acx - s.textWidth(sub) / 2, og_nameY + 22);
  s.print(sub);

  bool armed = millis() < ownerDelArmUntil;
  const char* lbl[4] = { "Pilih foto", "Ubah nama", "Bingkai", "Statistik" };
  for (int i = 0; i < 5; i++) {
    int x, y, w, h; ownerBtnRect(i, x, y, w, h);
    if (i < 4) ownerBtn(s, x, y, w, h, lbl[i], i == 0 ? T().accent : T().surface2, T().text);
    else       ownerBtn(s, x, y, w, h, armed ? "Yakin?" : "Hapus foto",
                        armed ? T().danger : T().surface2, armed ? 0xFFFF : T().danger);
  }
  if (armed) needRedraw = true;                 // supaya label kembali sendiri saat waktu habis

  drawBack(s);
  drawToast(s);
}

void ownerTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;

  if (ownerEditing) {
    if (y < kbY() - 2) {
      ownerCommitName();
    } else {
      kbTouch(x, y);
      if ((int)ownerNameEdit.length() > OWNER_NAME_MAX) ownerNameEdit.remove(OWNER_NAME_MAX);
    }
    needRedraw = true;
    return;
  }

  if (isBack(x, y)) {
    if (ownerView != 0) { ownerView = 0; ownerStatArmUntil = 0; needRedraw = true; }   // dari sub-layar balik ke Profil dulu
    else navBack();
    return;
  }
  if (ownerView == 1) { ownerTouchPick(x, y);  return; }
  if (ownerView == 2) { ownerTouchStats(x, y); return; }

  int idx = -1;
  for (int i = 0; i < 5; i++) {
    int bx, by, bw, bh; ownerBtnRect(i, bx, by, bw, bh);
    if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) { idx = i; break; }
  }
  if (idx < 0) return;

  if (idx == 0) {                                // Pilih foto dari SD (via Galeri mode pilih)
    if (!sdReady) { showToast("SD Card tidak terdeteksi"); needRedraw = true; return; }
    ownerPickMode = true;
    navPush(SCR_GALLERY);
    showToast("Pilih satu foto untuk profil");
  } else if (idx == 1) {                         // Ubah nama
    ownerNameEdit = String(ownerNameBuf);
    ownerEditing = true;
    kbTarget = &ownerNameEdit; kbVisible = true; kbMode = KB_UPPER;
    needRedraw = true;
  } else if (idx == 2) {                         // Bingkai & warna
    ownerView = 1; needRedraw = true;
  } else if (idx == 3) {                         // Statistik
    ownerView = 2; ownerStatArmUntil = 0; needRedraw = true;
  } else {                                       // Hapus foto (ketuk 2x)
    if (!ownerHasAv) { showToast("Belum ada foto profil"); needRedraw = true; return; }
    if (millis() < ownerDelArmUntil) {
      ownerDelArmUntil = 0;
      ownerClearAvatar();
      showToast("Foto profil dihapus");
    } else {
      ownerDelArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk hapus foto");
    }
    needRedraw = true;
  }
}
'''
a=o.index("static void ownerGeom(")
o=o[:a]+TAIL

# ---------- 6) phone.ino: forward-decl + hook penghitung di navPush ----------
p=rep(p,"void navPush(Screen s){\n","void ownerStatsOpen(int); // Profil v2: hitung berapa kali app dibuka\nvoid navPush(Screen s){\n","navPush decl")
p=rep(p,"  notifClear(s); // v-baru: badge notifikasi app ini ilang begitu beneran dibuka\n",
        "  notifClear(s); // v-baru: badge notifikasi app ini ilang begitu beneran dibuka\n  ownerStatsOpen((int)s); // Profil v2: statistik app paling sering dibuka\n","navPush hook")

open('owner_renphone.ino','w',encoding='utf-8').write(o)
open('phone.ino','w',encoding='utf-8').write(p)
print("OK: Profil v2 terpasang (foto besar + bingkai & warna + statistik)")
