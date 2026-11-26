#!/usr/bin/env python3
# apply_music_ui.py -- MUS_UX_V1
# Jalankan dari root repo SETELAH apply_audius.py:  python3 tools/patches/apply_music_ui.py   (--check = cek anchor saja)
# UI + UX baru untuk app Musik (Pemutar, Daftar Lagu, Online). Hanya kode gambar dan sentuh yang berubah.
#   Daftar Lagu & Online : geser dengan jari (halus per piksel) + meluncur setelah dilepas, sentuh saat meluncur = berhenti,
#                          baris disorot selagi ditekan, lagu baru diputar saat jari DILEPAS (geser tidak lagi memutar lagu),
#                          scrollbar mengikuti posisi piksel, daftar dibuka di lagu yang sedang main, layar kosong ditengah.
#   Online               : tombol x di kotak cari, rangka (skeleton) berdenyut saat memuat, "Memuat..." pada lagu online.
#   Pemutar              : tombol yang ditekan disorot, geser kiri/kanan di cover/judul = lagu berikut/sebelumnya
#                          (ketuk tetap play/jeda, kini saat jari dilepas).
#   phone.ino            : app Musik ikut menerima event "jari dilepas" (satu kondisi ditambah).
import os, sys

M = "MUS_UX_V1"
FW = os.path.join("firmware", "phone")
MB = os.path.join(FW, "musicbt_renphone.ino")
PH = os.path.join(FW, "phone.ino")
CL = "CHANGELOG.md"
MD = os.path.join("docs", "features", "media.md")
CHECK = "--check" in sys.argv

def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    c = s.count(o)
    if c != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, c))
    return s.replace(o, n)

mb, ph, cl, md = rd(MB), rd(PH), rd(CL), rd(MD)
if M in mb:
    print("sudah terpasang (%s)" % M); sys.exit(0)
if "AUDIUS_V1" not in mb:
    sys.exit("GAGAL: jalankan tools/patches/apply_audius.py dulu (patch ini dibuat di atasnya).")

VARS = r"""// =====================================================================
//  MUS_UX_V1 -- keadaan sentuh baru untuk app Musik (geser daftar, ketuk saat lepas, geser di pemutar)
// =====================================================================
static int      musPtrMode = 0;               // 0 diam | 1 ditekan (calon ketuk) | 2 menghentikan glide | 3 menggeser daftar | 5 pemutar: calon ketuk | 6 pemutar: menggeser kiri/kanan
static int      musPtrX0 = 0, musPtrY0 = 0, musPtrLastY = 0, musPtrDx = 0;
static uint32_t musPtrT0 = 0, musPtrMoveT = 0;
static float    musPtrVel = 0.f;              // px/ms (daftar)
static bool     musLsGlide = false;           // daftar meluncur setelah jari diangkat
static uint32_t musLsGlideT = 0;
static int      musLsFrac = 0;                // 0..29: piksel baris pertama yang sudah tergeser keluar di atas
static int      musPressRow = -1;             // baris daftar yang sedang ditekan (disorot)
static int      musPressCtl = 0;              // tombol pemutar yang sedang ditekan: 1 play, 2 prev, 3 next, 4 acak, 5 ulang
static void musLsGlideTick(uint32_t now);     // definisi di blok halaman Online

"""
HELPERS = r"""// =====================================================================
//  MUS_UX_V1: geser daftar (Daftar Lagu & Online) dengan momentum, ketuk saat jari dilepas + sorotan baris;
//  di pemutar: sorotan tombol yang ditekan, geser kiri/kanan di cover/judul = lagu sebelum/berikut.
//  Posisi daftar = musListTop/musJamTop (baris pertama) + musLsFrac (0..29 px) -> semua kode lama tetap berlaku.
// =====================================================================
static bool  musLsActive() { return musPage == 1 || (musPage == 4 && !musJamKbOn()); }
static int   musLsTopY()   { return musPage == 4 ? musJamTopY() : 48; }
static int   musLsBotY()   { return musPage == 4 ? musJamBot()  : mBotY - 4; }
static int   musLsN()      { return musPage == 4 ? jamCount()   : musListCount(); }
static int&  musLsRow()    { return musPage == 4 ? musJamTop    : musListTop; }
static int   musLsMax()    { int m = musLsN() * 30 - 2 - (musLsBotY() - musLsTopY()); return m < 0 ? 0 : m; }
static float musLsGet()    { return (float)musLsRow() * 30.f + (float)musLsFrac; }
static void  musLsSet(float px) {
  float mx = (float)musLsMax();
  if (px > mx) px = mx;
  if (px < 0.f) px = 0.f;
  int r = (int)(px / 30.f);
  int f = (int)(px - (float)r * 30.f);
  if (f < 0) f = 0; if (f > 29) f = 29;
  musLsRow() = r; musLsFrac = f;
}
static int musLsRowAt(int y) {
  int rel = y - musLsTopY() + musLsFrac;
  if (rel < 0) return -1;
  int idx = musLsRow() + rel / 30;
  return (idx < musLsN()) ? idx : -1;
}
static void musLsDrawBar(LGFX_Sprite& s, int top, int bot, int n) {
  int vh = bot - top, total = n * 30 - 2, mx = total - vh;
  if (mx <= 0 || vh <= 4) return;
  int trackH = vh - 2, thumbH = (trackH * vh) / total;
  if (thumbH < 12) thumbH = 12;
  if (thumbH > trackH) thumbH = trackH;
  int thumbY = top + (int)((float)(trackH - thumbH) * musLsGet() / (float)mx);
  nyxAaRR(s, SCR_W - 9, top, 3, trackH, 1, blend565(T().bg, T().text, 30));
  nyxAaRR(s, SCR_W - 9, thumbY, 3, thumbH, 1, T().accent);
}
static void musLsGlideTick(uint32_t now) {
  if (!musLsGlide) return;
  if (!(musPage == 1 || musPage == 4) || musPtrMode == 3) { musLsGlide = false; musPtrVel = 0.f; return; }
  uint32_t dt = now - musLsGlideT;
  if (dt == 0) return;
  if (dt > 50) dt = 50;
  musLsGlideT = now;
  float px = musLsGet() + musPtrVel * (float)dt;
  if (px < 0.f || px > (float)musLsMax()) { musLsGlide = false; musPtrVel = 0.f; }   // mentok ujung: berhenti
  musLsSet(px);
  musPtrVel *= (1.0f - 0.0045f * (float)dt);
  if (musPtrVel < 0.02f && musPtrVel > -0.02f) { musLsGlide = false; musPtrVel = 0.f; }
  needRedraw = true;
}
// jari menyentuh area daftar? true = ditangani (ketuk baru dijalankan saat jari dilepas)
static bool musLsDown(int x, int y) {
  if (!musLsActive()) return false;
  if (y < musLsTopY() || y >= musLsBotY()) return false;
  bool wasGliding = musLsGlide;
  musLsGlide = false; musPtrVel = 0.f;
  musPtrMode = wasGliding ? 2 : 1;                 // sentuhan yang menghentikan glide tidak boleh membuka lagu
  musPtrX0 = x; musPtrY0 = y; musPtrLastY = y;
  musPtrT0 = millis(); musPtrMoveT = musPtrT0;
  musPressRow = wasGliding ? -1 : musLsRowAt(y);
  needRedraw = true;
  return true;
}
// jari bergeser (event held)
static void musPtrMove(int x, int y) {
  if (musPtrMode == 1 || musPtrMode == 2) {
    if (abs(y - musPtrY0) > 8) { musPtrMode = 3; musPressRow = -1; needRedraw = true; }
  }
  if (musPtrMode == 3) {
    uint32_t nw = millis(), dt = nw - musPtrMoveT;
    if (dt < 1) dt = 1;
    musPtrMoveT = nw;
    musPtrVel = musPtrVel * 0.5f + ((float)(musPtrLastY - y) / (float)dt) * 0.5f;
    musLsSet(musLsGet() + (float)(musPtrLastY - y));
    needRedraw = true;
  } else if (musPtrMode == 5 || musPtrMode == 6) {
    int dx = x - musPtrX0, dy = y - musPtrY0;
    musPtrDx = dx;
    if (musPtrMode == 5 && abs(dx) > 14 && abs(dx) > abs(dy)) musPtrMode = 6;
    else if (musPtrMode == 5 && abs(dy) > 24) musPtrMode = 0;     // geser tegak = batal
    if (musPtrMode == 6) needRedraw = true;
  }
  musPtrLastY = y;
}
// jari diangkat
static void musPtrUp() {
  int mode = musPtrMode, pr = musPressRow, dx = musPtrDx;
  uint32_t now = millis();
  musPtrMode = 0; musPressRow = -1; musPressCtl = 0; musPtrDx = 0;
  if (mode == 1 && pr >= 0 && now - musPtrT0 < 700 && pr < musLsN()) {            // ketuk baris
    if (musPage == 4) musPost('J', pr, nullptr); else musPost('p', pr, nullptr);
    musPage = 0; vibTap();
  } else if (mode == 3) {                                                         // lepas setelah geser -> meluncur
    if (now - musPtrMoveT < 90 && musLsMax() > 0) {
      if (musPtrVel > 3.0f) musPtrVel = 3.0f;
      if (musPtrVel < -3.0f) musPtrVel = -3.0f;
      if (musPtrVel > 0.05f || musPtrVel < -0.05f) { musLsGlide = true; musLsGlideT = now; }
    }
  } else if (mode == 5 && now - musPtrT0 < 700) {                                 // ketuk cover/judul = play/jeda
    musPost('u', 0, nullptr); vibTap();
  } else if (mode == 6) {                                                         // geser kiri = berikutnya, kanan = sebelumnya
    if (dx <= -50)     { musPost('n', 0, nullptr); vibTap(); }
    else if (dx >= 50) { musPost('b', 0, nullptr); vibTap(); }
  }
  needRedraw = true;
}

"""

# ------------------------------------------------------------------ phone.ino: kirim event "jari dilepas" ke Musik
ph = sub(ph, "} else if(curScreen()==SCR_BACKUP || curScreen()==SCR_OWNER){",
             "} else if(curScreen()==SCR_BACKUP || curScreen()==SCR_OWNER || curScreen()==SCR_MUSIC){   // MUS_UX_V1: Musik juga butuh event lepas", "phone.ino dispatch")

# ------------------------------------------------------------------ musicbt: keadaan
mb = sub(mb, "static void musToast(const char* m) {", VARS + "static void musToast(const char* m) {", "blok variabel")
mb = sub(mb, "void musicEnter() {\n  musBegin();\n  musPage = 0;",
             "void musicEnter() {\n  musBegin();\n  musPage = 0;\n  musPtrMode = 0; musPressRow = -1; musPressCtl = 0; musLsGlide = false; musLsFrac = 0;   // MUS_UX_V1", "musicEnter")
mb = sub(mb, "  if (jamTakeDone()) {                           // JAM_UI_V1: pencarian online selesai",
             "  if (onScr) musLsGlideTick(now);                // MUS_UX_V1: daftar meluncur setelah jari diangkat\n  if (jamTakeDone()) {                           // JAM_UI_V1: pencarian online selesai", "poll glide")

# ------------------------------------------------------------------ pemutar: gambar
mb = sub(mb, "  musFmtTime(t1, musLoaded ? musPlayedMs : 0);\n",
             "  musFmtTime(t1, musLoaded ? musPlayedMs : 0);\n  if (musNetMode && musLoaded && !musPaused && musPlayedMs == 0) strcpy(t1, \"Memuat...\");   // MUS_UX_V1: lagu online belum mulai bunyi\n", "teks Memuat")
mb = sub(mb, "  // kontrol\n  int sm = musShufMode;\n",
"""  // kontrol
  if (musPressCtl) {                                   // MUS_UX_V1: sorotan tombol yang sedang ditekan
    if (musPressCtl == 1) nyxAaCircle(s, mPlayX, mCtlCY, mPlayR + 7, blend565(T().bg, T().accent, 110));
    else {
      int px = (musPressCtl == 2) ? mPrevX : (musPressCtl == 3) ? mNextX : (musPressCtl == 4) ? mShufX : mRepX;
      nyxAaCircle(s, px, mCtlCY, 19, blend565(T().bg, T().text, 38));
    }
  }
  int sm = musShufMode;
""", "sorotan tombol")
mb = sub(mb, "  musDrawIcon(s, playing ? 3 : 2, mPlayX, mCtlCY, T().bg);\n}\n",
"""  musDrawIcon(s, playing ? 3 : 2, mPlayX, mCtlCY, T().bg);

  if (musPtrMode == 6) {                               // MUS_UX_V1: petunjuk geser kiri/kanan (melewati 50 px = siap)
    bool armed = abs(musPtrDx) >= 50, nxt = musPtrDx < 0;
    const char* lb = nxt ? RPTR("Berikutnya >>") : RPTR("<< Sebelumnya");
    int pw = 112, px = mCovX + mCovS / 2 - pw / 2, py = mCovY + mCovS / 2 - 10;
    if (mLand) px = mCovX + mCovS / 2 - pw / 2;
    nyxAaRR(s, px, py, pw, 20, 10, armed ? T().accent : blend565(T().bg, T().text, 60));
    musDrawLabel(s, lb, px, py, pw, 20, armed ? T().bg : T().text);
  }
}
""", "pill geser")

# ------------------------------------------------------------------ Daftar Lagu: gambar
mb = sub(mb, "static void musDrawList(LGFX_Sprite& s) {",
             "static void musLsSet(float px);                       // MUS_UX_V1 (definisi di blok halaman Online)\nstatic float musLsGet();\nstatic void musLsDrawBar(LGFX_Sprite& s, int top, int bot, int n);\nstatic void musDrawList(LGFX_Sprite& s) {", "deklarasi maju")
mb = sub(mb, "  if (musListTop >= n) musListTop = 0;\n  s.setTextSize(2); s.setTextColor(T().accent); s.setCursor(12, mHdrY + 1); s.print(RPTR(\"Daftar Lagu\"));",
             "  musLsSet(musLsGet());                             // MUS_UX_V1: rapikan posisi (daftar bisa menyusut)\n  s.setTextSize(2); s.setTextColor(T().accent); s.setCursor(12, mHdrY + 1); s.print(RPTR(\"Daftar Lagu\"));", "rapikan posisi daftar")
mb = sub(mb, """  if (n == 0) {
    s.setTextColor(T().subtext);
    s.setCursor(12, 56); s.print(RPTR("Tidak ada MP3 di folder /music"));
    s.setCursor(12, 70); s.print(RPTR("(atau di root SD). Taruh file lalu"));
    s.setCursor(12, 84); s.print(RPTR("buka ulang app ini."));
  }
  for (int r = 0; r < rows; r++) {
    int idx = musListTop + r;
    if (idx >= n) break;
    int y = 48 + r * 30;
    bool cur = (idx == musCur);
    musPanel(s, 12, y, SCR_W - 30, 28, 8, cur ? T().accent : T().surface2, cur ? 90 : 170);
""", """  int lTop = 48, lBot = mBotY - 4;
  if (n == 0) {                                     // MUS_UX_V1: layar kosong di tengah
    int cy = lTop + 34;
    nyxAaCircle(s, SCR_W / 2, cy, 20, blend565(T().bg, T().text, 28));
    nyxAaCircle(s, SCR_W / 2, cy, 6, blend565(T().bg, T().text, 70));
    musDrawLabel(s, RPTR("Tidak ada MP3 di folder /music"), 0, cy + 30, SCR_W, 12, T().text);
    musDrawLabel(s, RPTR("(atau di root SD). Taruh file lalu"), 0, cy + 46, SCR_W, 12, T().subtext);
    musDrawLabel(s, RPTR("buka ulang app ini."), 0, cy + 60, SCR_W, 12, T().subtext);
  }
  s.setClipRect(0, lTop, SCR_W, lBot - lTop);       // MUS_UX_V1: baris boleh terpotong di tepi saat digeser
  for (int r = 0; ; r++) {
    int idx = musListTop + r;
    if (idx >= n) break;
    int y = lTop + r * 30 - musLsFrac;
    if (y >= lBot) break;
    bool cur = (idx == musCur), prs = (idx == musPressRow);
    musPanel(s, 12, y, SCR_W - 30, 28, 8, (cur || prs) ? T().accent : T().surface2, prs ? (cur ? 130 : 60) : (cur ? 90 : 170));
""", "daftar: awal loop")
mb = sub(mb, """  if (n > rows) {                                   // scrollbar
    int trackH = rows * 30 - 2, thumbH = (trackH * rows) / n; if (thumbH < 12) thumbH = 12;
    int thumbY = 48 + ((trackH - thumbH) * musListTop) / (n - rows > 0 ? n - rows : 1);
    nyxAaRR(s, SCR_W - 9, 48, 3, trackH, 1, blend565(T().bg, T().text, 30));
    nyxAaRR(s, SCR_W - 9, thumbY, 3, thumbH, 1, T().accent);
  }
""", """  s.clearClipRect();
  musLsDrawBar(s, lTop, lBot, n);                   // MUS_UX_V1: scrollbar mengikuti posisi piksel
""", "daftar: scrollbar")

# ------------------------------------------------------------------ Online: helper + gambar
mb = sub(mb, "static int  musJamRows()  { int r = (musJamBot() - musJamTopY()) / 30; return r < 0 ? 0 : r; }\n",
             "static int  musJamRows()  { int r = (musJamBot() - musJamTopY()) / 30; return r < 0 ? 0 : r; }\n\n" + HELPERS, "helper geser")
mb = sub(mb, "  musJamTop = 0; needRedraw = true;\n}", "  musJamTop = 0; musLsFrac = 0; needRedraw = true;\n}", "fetch reset posisi")
mb = sub(mb, "  if (!musJamOpened && jamCount() == 0 && !jamBusy()) { musJamOpened = true; musJamTag = 0; musJamFetch(\"\", \"\"); }\n}",
"""  musLsFrac = 0;
  if (!musJamOpened && jamCount() == 0 && !jamBusy()) { musJamOpened = true; musJamTag = 0; musJamFetch("", ""); }
  else if (musNetMode && musNetCur >= 0 && musNetCur < jamCount()) {           // MUS_UX_V1: buka di lagu yang sedang main
    int t = musNetCur - musJamRows() / 2; if (t < 0) t = 0;
    musJamTop = t; musLsSet(musLsGet());
  }
}""", "buka Online di lagu aktif")
mb = sub(mb, "  musTrunc(q, (bw - 24) / 6);", "  musTrunc(q, (bw - (musJamQuery.length() > 0 ? 40 : 24)) / 6);   // MUS_UX_V1: ruang tombol x", "trunc kotak cari")
mb = sub(mb, "  nyxAaRR(s, musJamSrcX(), by, MJ_SRC_W, 22, 11, chip);            // AUDIUS_V1: chip sumber\n",
"""  if (musJamQuery.length() > 0) {                    // MUS_UX_V1: tombol hapus teks (x)
    int xx = bx + bw - 13, yy = by + 11;
    nyxAaCircle(s, xx, yy, 7, blend565(T().bg, T().text, 50));
    s.drawLine(xx - 3, yy - 3, xx + 3, yy + 3, T().bg); s.drawLine(xx + 3, yy - 3, xx - 3, yy + 3, T().bg);
  }
  nyxAaRR(s, musJamSrcX(), by, MJ_SRC_W, 22, 11, chip);            // AUDIUS_V1: chip sumber
""", "tombol x")
mb = sub(mb, "  if (musJamTop >= n) musJamTop = 0;\n", "  musLsSet(musLsGet());                             // MUS_UX_V1: rapikan posisi\n", "rapikan posisi Online")
mb = sub(mb, """      char m[16]; snprintf(m, sizeof(m), "Memuat%.*s", (int)((millis() / 300) % 4), "...");
      s.setTextColor(T().text); s.setCursor(12, tY); s.print(m);
""", """      for (int r = 0; r < rows && r < 5; r++) {      // MUS_UX_V1: rangka berdenyut selagi memuat
        int y = top + r * 30;
        uint8_t a = 60 + (uint8_t)(50.f * (0.5f + 0.5f * sinf(millis() * 0.006f - r * 0.8f)));
        musPanel(s, 12, y, SCR_W - 30, 28, 8, T().surface2, a);
        nyxAaCircle(s, 28, y + 14, 10, blend565(T().bg, T().text, 30));
        nyxAaRR(s, 46, y + 6, 90 + (r * 37) % 70, 6, 3, blend565(T().bg, T().text, 35));
        nyxAaRR(s, 46, y + 17, 60 + (r * 23) % 50, 5, 2, blend565(T().bg, T().text, 22));
      }
""", "skeleton")
mb = sub(mb, """  for (int r = 0; r < rows; r++) {
    int idx = musJamTop + r;
    if (idx >= n) break;
    int y = top + r * 30;
    bool cur = (musNetMode && idx == musNetCur);
    musPanel(s, 12, y, SCR_W - 30, 28, 8, cur ? T().accent : T().surface2, cur ? 90 : 170);
""", """  int bot = musJamBot();
  s.setClipRect(0, top, SCR_W, bot - top);          // MUS_UX_V1: baris boleh terpotong di tepi saat digeser
  for (int r = 0; ; r++) {
    int idx = musJamTop + r;
    if (idx >= n) break;
    int y = top + r * 30 - musLsFrac;
    if (y >= bot) break;
    bool cur = (musNetMode && idx == musNetCur), prs = (idx == musPressRow);
    musPanel(s, 12, y, SCR_W - 30, 28, 8, (cur || prs) ? T().accent : T().surface2, prs ? (cur ? 130 : 60) : (cur ? 90 : 170));
""", "Online: awal loop")
mb = sub(mb, """  if (n > rows && rows > 0) {                       // scrollbar
    int trackH = rows * 30 - 2, thumbH = (trackH * rows) / n; if (thumbH < 12) thumbH = 12;
    int thumbY = top + ((trackH - thumbH) * musJamTop) / (n - rows > 0 ? n - rows : 1);
    nyxAaRR(s, SCR_W - 9, top, 3, trackH, 1, blend565(T().bg, T().text, 30));
    nyxAaRR(s, SCR_W - 9, thumbY, 3, thumbH, 1, T().accent);
  }
""", """  s.clearClipRect();
  musLsDrawBar(s, top, bot, n);                     // MUS_UX_V1: scrollbar mengikuti posisi piksel
""", "Online: scrollbar")

# ------------------------------------------------------------------ Online: sentuh
mb = sub(mb, "  if (rowBox && x >= 12 && x < musJamSrcX() - 2) {",
"""  if (rowBox && musJamQuery.length() > 0 && x >= musJamSrcX() - 4 - 22 && x < musJamSrcX() - 2) {   // MUS_UX_V1: tombol x
    musJamQuery = ""; vibTap(); needRedraw = true; return;
  }
  if (rowBox && x >= 12 && x < musJamSrcX() - 2) {""", "sentuh tombol x")
mb = sub(mb, """  int n = jamCount(), rows = musJamRows(), top = musJamTopY();
  for (int r = 0; r < rows; r++) {
    int idx = musJamTop + r;
    if (idx >= n) break;
    if (musHit(x, y, 12, top + r * 30, SCR_W - 30, 28)) {
      musPost('J', idx, nullptr); musPage = 0; vibTap(); needRedraw = true; return;
    }
  }
""", """  int n = jamCount(), rows = musJamRows();
  if (musLsDown(x, y)) return;                      // MUS_UX_V1: daftar -- ketuk dijalankan saat jari dilepas, bisa digeser
""", "Online: sentuh daftar")
mb = sub(mb, "{ musJamTop -= rows; if (musJamTop < 0) musJamTop = 0; vibTap();", "{ musJamTop -= rows; if (musJamTop < 0) musJamTop = 0; musLsFrac = 0; vibTap();", "Online: tombol naik")
mb = sub(mb, "{ if (musJamTop + rows < n) musJamTop += rows; vibTap();", "{ if (musJamTop + rows < n) musJamTop += rows; musLsFrac = 0; vibTap();", "Online: tombol turun")

# ------------------------------------------------------------------ musicTouch
mb = sub(mb, "  if (isNew) musUiTouch++;                          // sinyal 'interaksi' utk RYNE\n",
             "  if (isNew) musUiTouch++;                          // sinyal 'interaksi' utk RYNE\n  if (!isNew && !held) { musPtrUp(); return; }      // MUS_UX_V1: jari diangkat\n", "musicTouch: lepas")
mb = sub(mb, "  if (!isNew) return;\n\n  if (musPage == 4 && musJamKbOn() && y >= kbY() - 2)",
             "  if (!isNew) { musPtrMove(x, y); return; }          // MUS_UX_V1: geser\n\n  if (musPage == 4 && musJamKbOn() && y >= kbY() - 2)", "musicTouch: geser")
mb = sub(mb, """    for (int r = 0; r < rows; r++) {
      int idx = musListTop + r;
      if (idx >= n) break;
      if (musHit(x, y, 12, 48 + r * 30, SCR_W - 30, 28)) {
        musPost('p', idx, nullptr); musPage = 0; vibTap(); needRedraw = true; return;
      }
    }
""", """    if (musLsDown(x, y)) return;                    // MUS_UX_V1: daftar -- ketuk dijalankan saat jari dilepas, bisa digeser
""", "Daftar: sentuh")
mb = sub(mb, "{ musListTop -= rows; if (musListTop < 0) musListTop = 0; vibTap();", "{ musListTop -= rows; if (musListTop < 0) musListTop = 0; musLsFrac = 0; vibTap();", "Daftar: tombol naik")
mb = sub(mb, "{ if (musListTop + rows < n) musListTop += rows; vibTap();", "{ if (musListTop + rows < n) musListTop += rows; musLsFrac = 0; vibTap();", "Daftar: tombol turun")
mb = sub(mb, "{ musPage = 1; int r = musListRows(); musListTop = (musCur / r) * r; vibTap();",
             "{ musPage = 1; { int t = musCur - musListRows() / 2; musListTop = t < 0 ? 0 : t; musLsFrac = 0; musLsSet(musLsGet()); } vibTap();   // MUS_UX_V1: buka di lagu yang sedang main\n   ", "buka Daftar di lagu aktif")
mb = sub(mb, "  if (musHitC(x, y, mPlayX, mCtlCY, mPlayR + 6)) { musPost('u', 0, nullptr); vibTap();",
             "  if (musHitC(x, y, mPlayX, mCtlCY, mPlayR + 6)) { musPost('u', 0, nullptr); musPressCtl = 1; vibTap();", "tekan play")
mb = sub(mb, "  if (musHitC(x, y, mPrevX, mCtlCY, 22)) { musPost('b', 0, nullptr); vibTap();",
             "  if (musHitC(x, y, mPrevX, mCtlCY, 22)) { musPost('b', 0, nullptr); musPressCtl = 2; vibTap();", "tekan prev")
mb = sub(mb, "  if (musHitC(x, y, mNextX, mCtlCY, 22)) { musPost('n', 0, nullptr); vibTap();",
             "  if (musHitC(x, y, mNextX, mCtlCY, 22)) { musPost('n', 0, nullptr); musPressCtl = 3; vibTap();", "tekan next")
mb = sub(mb, "  if (musHitC(x, y, mShufX, mCtlCY, 20)) {\n    musShufMode",
             "  if (musHitC(x, y, mShufX, mCtlCY, 20)) {\n    musPressCtl = 4;\n    musShufMode", "tekan acak")
mb = sub(mb, "  if (musHitC(x, y, mRepX, mCtlCY, 20)) { musRepeat = (musRepeat + 1) % 3;",
             "  if (musHitC(x, y, mRepX, mCtlCY, 20)) { musPressCtl = 5; musRepeat = (musRepeat + 1) % 3;", "tekan ulang")
mb = sub(mb, "  if (musHit(x, y, mCovX, mCovY, mCovS, mCovS)) { musPost('u', 0, nullptr); vibTap(); needRedraw = true; return; }   // ketuk cover = play/pause",
"""  if (y >= mCovY - 2 && y <= mSubY + 8) {            // MUS_UX_V1: area cover+judul: ketuk = play/jeda (saat dilepas), geser kiri/kanan = lagu lain
    musPtrMode = 5; musPtrX0 = x; musPtrY0 = y; musPtrDx = 0; musPtrT0 = millis(); needRedraw = true; return;
  }""", "tekan cover")

# ------------------------------------------------------------------ docs
cl = sub(cl, "## Unreleased\n", "## Unreleased\n" +
"- Improved: new look and easier use for the Music app (MUS_UX_V1). Song list and Online page: drag the list with your finger (smooth per pixel) and it keeps gliding after you lift it; touching it while it glides only stops it. A row lights up while you press it and the song starts when you LIFT your finger, so scrolling no longer starts a song by accident (before, only the up/down buttons scrolled and a touch played at once). The scrollbar follows the exact position, the list opens at the song that is playing, and the empty list message is centred. Online page: an x button in the search box clears the text, pulsing placeholder rows show while loading, and the player shows *Memuat...* until an online track starts to sound. Player: the button you press (play, previous, next, shuffle, repeat) is highlighted; swipe left or right on the cover or title for the next or previous song (a pill shows the action and turns solid when you have swiped far enough); a tap there still plays or pauses, now when the finger is lifted. `phone.ino` changes by one condition: the Music app now also gets the finger-up event. Only drawing and touch code changed. The scrolling and swipe logic is tested on a PC; the drawing is not yet compiled or tested on a device. Script: `tools/patches/apply_music_ui.py` (run after `apply_audius.py`)\n",
"changelog")
md = sub(md, "- Up to 200 songs in the `music/` folder.\n",
"- Up to 200 songs in the `music/` folder.\n- **Touch.** Drag the song list (and the Online list) with your finger, it glides after you lift it; tap a row to play (the song starts when you lift your finger). On the player, swipe left or right on the cover or title for the next or previous song; tap them to play or pause.\n", "media.md touch")

if CHECK:
    print("OK (--check): semua anchor cocok, tidak ada file yang diubah"); sys.exit(0)
for p, s in ((MB, mb), (PH, ph), (CL, cl), (MD, md)):
    open(p + ".bak_musui", "w", encoding="utf-8", newline="").write(rd(p)); wr(p, s)
print("OK: %s terpasang (backup: *.bak_musui)" % M)
