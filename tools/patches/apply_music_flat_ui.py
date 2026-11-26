#!/usr/bin/env python3
# apply_music_flat_ui.py -- MUS_FLAT_V1
# Jalankan dari root repo SETELAH apply_music_ui.py:  python3 tools/patches/apply_music_flat_ui.py   (--check = cek anchor saja)
# ROMBAK tampilan app Musik: gaya "flat editorial" -- blok warna solid, garis tipis, sudut hampir siku,
# SATU aksen (T().accent). Tanpa gradasi (cover, equalizer, batang vibe pelangi dll), tanpa glow/ring, tanpa kaca/kilap.
#   Pemutar    : cover = blok warna solid (palet 8 warna) + bayangan keras (blok aksen, bukan blur) + piringan hitam berputar;
#                label status kecil ("SEDANG DIPUTAR"/"DIJEDA"/"ONLINE"); equalizer kotak satu warna;
#                progres = garis 4 px + penanda tegak (bukan gelombang); volume = garis 2 px + penanda tegak;
#                Play = blok aksen persegi membulat (bukan lingkaran ber-ring); ikon lain polos, penanda acak/ulang = garis kecil.
#   Header     : chip jadi kotak berbingkai tipis (bukan pil kaca); status TWS = kotak kecil berkedip saat menghubungkan.
#   Daftar/Online/Cari TWS : baris datar dengan garis pemisah + bilah aksen di kiri untuk lagu yang sedang main
#                (tanpa panel kaca & lingkaran nomor); tombol bawah = bingkai tipis / blok aksen; judul halaman + garis aksen pendek.
#   RYNE       : tab pakai blok datar; batang vibe tanpa pelangi (terpilih = aksen, lain = abu).
# Posisi sentuh TIDAK berubah (semua hit-test tetap memakai variabel layout yang sama; tinggi baris tetap 30 px).
import os, sys

M = "MUS_FLAT_V1"
FW = os.path.join("firmware", "phone")
MB = os.path.join(FW, "musicbt_renphone.ino")
CL = "CHANGELOG.md"
MD = os.path.join("docs", "features", "media.md")
CHECK = "--check" in sys.argv

def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    c = s.count(o)
    if c != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, c))
    return s.replace(o, n)
def func_range(s, sig, l):
    a = s.find(sig)
    if a < 0 or s.find(sig, a + 1) >= 0: sys.exit("GAGAL: fungsi '%s' tidak ditemukan tepat 1x." % l)
    b = s.find("\n}\n", a)
    if b < 0: sys.exit("GAGAL: akhir fungsi '%s' tidak ketemu." % l)
    return a, b + 3
def repl_func(s, sig, new, l):
    a, b = func_range(s, sig, l)
    return s[:a] + new.rstrip("\n") + "\n" + s[b:]

mb, cl, md = rd(MB), rd(CL), rd(MD)
if M in mb:
    print("sudah terpasang (%s)" % M); sys.exit(0)
if "MUS_UX_V1" not in mb:
    sys.exit("GAGAL: jalankan tools/patches/apply_music_ui.py dulu (patch ini dibuat di atasnya).")

# ---------------------------------------------------------------- layout
CALC = r"""static void musCalcLayout() {   // MUS_FLAT_V1: cover lebih kecil + label status di atas judul (landscape)
  int W = SCR_W, H = SCR_H;
  mLand = (W >= H);
  mHdrY = 25; mChipH = 18;
  mDafW = 70; mDafX = 12;
  mTwsW = 118; mTwsX = W - 12 - mTwsW;
  int cx = W / 2;
  int dPN = (W >= 300) ? 62 : 50, dSR = (W >= 300) ? 112 : 92;
  mCtlCY = H - BACK_H - 3 - 4 - mPlayR;
  mPlayX = cx; mPrevX = cx - dPN; mNextX = cx + dPN; mShufX = cx - dSR; mRepX = cx + dSR;
  mBotH = 28; mBotY = H - BACK_H - 3 - 6 - mBotH;
  if (mLand) {
    mCovX = 12; mCovY = 48; mCovS = 84;
    int x0 = mCovX + mCovS + 16, rw = W - 12 - x0;
    mTitX = x0; mTitY = 61; mTitW = rw; mSubY = 83;
    mVibeY = 96;
    mEqX = x0; mEqY = 108; mEqW = rw; mEqH = 16;
    mVolY = 127; mVolIcX = x0 + 8; mVolSX = x0 + 26; mVolSW = rw - 26;
    mBarX = 12; mBarY = 153; mBarW = W - 24; mTimeY = 142;
  } else {
    mCovS = 120; mCovX = (W - mCovS) / 2 - 2; mCovY = 48;
    mTitX = 12; mTitY = 176; mTitW = W - 24; mSubY = 197;
    mVibeY = -1;
    mEqX = 12; mEqY = 0; mEqW = 0; mEqH = 0;
    mVolY = 208; mVolIcX = 20; mVolSX = 38; mVolSW = W - 12 - 38;
    mBarX = 12; mBarY = 242; mBarW = W - 24; mTimeY = 231;
  }
}
"""

# ---------------------------------------------------------------- helper datar (setelah musPanel)
HELP = r"""
// ---- MUS_FLAT_V1: helper gaya datar (tanpa gradasi / kaca / glow) ----
#define MUS_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t MUS_CVPAL[8] = {            // palet cover: warna solid, dipilih dari hash judul
  MUS_RGB(228, 87, 46),  MUS_RGB(242, 177, 52), MUS_RGB(46, 158, 131), MUS_RGB(58, 110, 165),
  MUS_RGB(214, 86, 138), MUS_RGB(91, 140, 62),  MUS_RGB(201, 193, 177), MUS_RGB(31, 122, 140)
};
static uint16_t musLn() { return blend565(T().bg, T().text, 60); }       // garis tipis / bingkai
static void musOutline(LGFX_Sprite& s, int x, int y, int w, int h, int r, uint16_t c) { nyxAaRRRing(s, x, y, w, h, r, c); }
static void musHead(LGFX_Sprite& s, const char* t) {                       // judul halaman + garis aksen pendek
  s.setTextSize(2); s.setTextColor(T().text); s.setCursor(12, mHdrY + 1); s.print(t);
  s.fillRect(12, mHdrY + 18, 28, 2, T().accent);
}
static void musRowBg(LGFX_Sprite& s, int y, bool cur, bool prs) {         // baris daftar: datar, garis pemisah, bilah aksen
  int x = 12, w = SCR_W - 30;
  if (prs)      s.fillRect(x, y, w, 29, blend565(T().bg, T().accent, 70));
  else if (cur) s.fillRect(x, y, w, 29, blend565(T().bg, T().accent, 36));
  s.drawFastHLine(x, y + 29, w, musLn());
  if (cur) s.fillRect(x, y, 3, 29, T().accent);
}
static void musBtnGhost(LGFX_Sprite& s, int x, int w) { musOutline(s, x, mBotY, w, mBotH, 6, musLn()); }
static void musBtnSolid(LGFX_Sprite& s, int x, int w, uint16_t c) { nyxAaRR(s, x, mBotY, w, mBotH, 6, c); }
static void musArrow(LGFX_Sprite& s, int x, int w, bool up) {
  int cx = x + w / 2, cy = mBotY + mBotH / 2;
  if (up) s.fillTriangle(cx, cy - 5, cx - 6, cy + 4, cx + 6, cy + 4, T().text);
  else    s.fillTriangle(cx, cy + 5, cx - 6, cy - 4, cx + 6, cy - 4, T().text);
}
"""

COVER = r"""static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {   // MUS_FLAT_V1
  int x = mCovX, y = mCovY, S = mCovS;
  s.fillRect(x + 4, y + 4, S, S, T().accent);          // bayangan keras: blok datar, bukan blur
  if (musCoverReady) {                              // cover asli dari tag ID3 (APIC)
    LGFX_Sprite* cv = musCvP[musCoverCur];
    if (cv->width() == MUS_COVER_SZ) {
      if (S == MUS_COVER_SZ) cv->pushSprite(&s, x, y);
      else {
        float z = (float)S / MUS_COVER_SZ;
        cv->pushRotateZoom(&s, x + S / 2.0f, y + S / 2.0f, 0.0f, z, z);
      }
      return;
    }
  }
  uint32_t hsh = 5381;
  for (const char* q = title; *q; q++) hsh = hsh * 33 + (uint8_t)*q;
  uint16_t col = MUS_CVPAL[hsh % 8];                // satu warna solid per judul
  s.fillRect(x, y, S, S, col);

  int cx = x + S / 2, cy = y + S / 2, R = (S * 44) / 100;
  nyxAaCircle(s, cx, cy, R, 0x1082);
  for (int g = 6; g < R - 8; g += 6) nyxAaCircleRing(s, cx, cy, R - g, 0x2124);
  uint32_t now = millis();
  float dt = (now - musAnimMs) / 1000.f; if (dt > 0.25f) dt = 0.25f;
  musAnimMs = now;
  if (playing) musDiscAng += dt * 150.f;
  if (musDiscAng > 360.f) musDiscAng -= 360.f;
  for (int k = 0; k < 2; k++) {
    float a = (musDiscAng + k * 180.f) * 0.0174533f;
    float ca = cosf(a), sa = sinf(a);
    s.drawLine(cx + (int)(ca * R * 0.5f), cy + (int)(sa * R * 0.5f),
               cx + (int)(ca * (R - 2)),  cy + (int)(sa * (R - 2)), 0x52AA);
  }
  nyxAaCircle(s, cx, cy, (R * 38) / 100, col);
  nyxAaCircle(s, cx, cy, 3, 0x1082);
}
"""

EQ = r"""static void musDrawEq(LGFX_Sprite& s, bool playing) {   // MUS_FLAT_V1: batang kotak, satu warna
  if (mEqH <= 0) return;
  const int n = 16, gap = 3;
  int bw = (mEqW - (n - 1) * gap) / n; if (bw < 2) bw = 2;
  uint32_t t = millis();
  uint16_t col = playing ? (uint16_t)T().accent : blend565(T().bg, T().text, 45);
  for (int i = 0; i < n; i++) {
    float tgt = playing ? (0.18f + 0.82f * fabsf(sinf(t * 0.0021f * (1.f + 0.23f * i) + i * 1.7f) *
                                                 cosf(t * 0.0013f + i * 0.9f)))
                        : 0.07f;
    musEq[i] += (tgt - musEq[i]) * 0.45f;
    int h = (int)(musEq[i] * mEqH); if (h < 2) h = 2;
    s.fillRect(mEqX + i * (bw + gap), mEqY + mEqH - h, bw, h, col);
  }
}
"""

CHIPS = r"""static void musDrawChips(LGFX_Sprite& s) {   // MUS_FLAT_V1: kotak berbingkai tipis
  uint16_t ln = musLn();
  musOutline(s, mDafX, mHdrY, mDafW, mChipH, 4, ln);
  for (int i = 0; i < 3; i++) s.fillRect(mDafX + 9, mHdrY + 5 + i * 4, 10, 2, T().text);
  s.setTextSize(1); s.setTextColor(T().text);
  s.setCursor(mDafX + 26, mHdrY + 5); s.print(RPTR("Daftar"));
  { int cx, cw; if (ryvChipRect(&cx, &cw)) { musOutline(s, cx, mHdrY, cw, mChipH, 4, ln); s.setTextColor(T().accent); s.setCursor(cx + (cw - 24) / 2, mHdrY + 5); s.print("RYNE"); s.setTextColor(T().text); } }
  { int jx, jw; if (musJamChipRect(&jx, &jw)) { musOutline(s, jx, mHdrY, jw, mChipH, 4, ln); s.setTextColor(T().accent); s.setCursor(jx + (jw - 36) / 2, mHdrY + 5); s.print("Online"); s.setTextColor(T().text); } }   // JAM_UI_V1

  const char* st; uint16_t col;
  char spkTxt[24];
  if (musOutSpk) {   // SPK_FFAT_V1: RAM internal bebas / blok terbesar (KB) -- bandingkan saat idle vs lagu main
    snprintf(spkTxt, sizeof(spkTxt), "SPK%s %uK/%uK", musUseFfat ? "-F" : "",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    st = spkTxt; col = T().good;
  }
  else if (!musCamAlive)   { st = "Modul BT mati";     col = T().danger; }
  else if (musBtConn)      { st = "TWS terhubung";     col = T().good; }
  else if (musConnecting)  { st = "Menghubungkan...";  col = T().accent2; }
  else                     { st = "TWS putus";         col = T().subtext; }
  musOutline(s, mTwsX, mHdrY, mTwsW, mChipH, 4, ln);
  bool blink = musConnecting && !musBtConn && ((millis() / 300) & 1);
  if (!blink) s.fillRect(mTwsX + 8, mHdrY + 6, 6, 6, col);
  s.setTextColor(T().text); s.setCursor(mTwsX + 20, mHdrY + 5); s.print(st);
}
"""

# ---------------------------------------------------------------- pemutar: potongan
LABEL = r"""  if (mLand) {                                       // MUS_FLAT_V1: label status kecil di atas judul
    uint16_t lc = playing ? (uint16_t)T().accent : (uint16_t)T().subtext;
    s.fillRect(mTitX, mCovY + 1, 6, 6, lc);
    s.setTextSize(1); s.setTextColor(lc);
    s.setCursor(mTitX + 11, mCovY);
    s.print(musNetMode ? "ONLINE" : (playing ? "SEDANG DIPUTAR" : (musLoaded ? "DIJEDA" : "SIAP")));
  }
"""

PLAYER_BOTTOM = r"""  musDrawEq(s, playing);

  // volume: garis tipis + penanda tegak (MUS_FLAT_V1: tanpa lingkaran)
  int lvl = (musVol == 0) ? 0 : (musVol < 64 ? 1 : 2);
  musDrawSpeaker(s, mVolIcX, mVolY + 7, T().subtext, lvl);
  s.fillRect(mVolSX, mVolY + 6, mVolSW, 2, musLn());
  int vf = (mVolSW * musVol) / 127;
  if (vf > 0) s.fillRect(mVolSX, mVolY + 6, vf, 2, T().text);
  int vx = vf - 1; if (vx < 0) vx = 0;
  s.fillRect(mVolSX + vx, mVolY + 1, 3, 12, T().text);

  // progress + waktu: garis 4 px + penanda tegak
  char t1[12], t2[12];
  musFmtTime(t1, musLoaded ? musPlayedMs : 0);
  if (musNetMode && musLoaded && !musPaused && musPlayedMs == 0) strcpy(t1, "Memuat...");   // MUS_UX_V1: lagu online belum mulai bunyi
  if (musLoaded && musDurMs > 0) musFmtTime(t2, musDurMs); else strcpy(t2, "--:--");
  s.setTextColor(T().text);
  s.setCursor(mBarX, mTimeY); s.print(t1);
  s.setTextColor(T().subtext);
  s.setCursor(mBarX + mBarW - s.textWidth(t2), mTimeY); s.print(t2);
  float pr = (musDurMs > 0) ? (float)musPlayedMs / (float)musDurMs : 0.f;
  if (pr > 1.f) pr = 1.f;
  int fw = (int)(pr * mBarW);
  if (!musLoaded) fw = 0;
  s.fillRect(mBarX, mBarY + 1, mBarW, 4, musLn());
  if (fw > 0) s.fillRect(mBarX, mBarY + 1, fw, 4, T().accent);
  if (musLoaded) {
    int mx = fw - 1; if (mx < 0) mx = 0; if (mx > mBarW - 3) mx = mBarW - 3;
    s.fillRect(mBarX + mx, mBarY - 3, 3, 12, T().text);
  }

  // kontrol: ikon polos; Play = blok aksen persegi membulat
  if (musPressCtl >= 2) {                              // MUS_UX_V1: sorotan tombol yang sedang ditekan
    int px = (musPressCtl == 2) ? mPrevX : (musPressCtl == 3) ? mNextX : (musPressCtl == 4) ? mShufX : mRepX;
    nyxAaRR(s, px - 19, mCtlCY - 19, 38, 38, 8, blend565(T().bg, T().text, 38));
  }
  int sm = musShufMode;
  musDrawShuffle(s, mShufX, mCtlCY, sm == 2 ? T().accent2 : (sm == 1 ? T().accent : T().subtext));
  if (sm == 1) s.fillRect(mShufX - 6, mCtlCY + 14, 12, 2, T().accent);
  if (sm == 2) { s.setTextSize(1); s.setTextColor(T().accent2); s.setCursor(mShufX - 6, mCtlCY + 11); s.print("AI"); }
  musDrawRepeat(s, mRepX, mCtlCY, musRepeat ? T().accent : T().subtext, musRepeat == 2);
  if (musRepeat) s.fillRect(mRepX - 6, mCtlCY + 14, 12, 2, T().accent);
  musDrawIcon(s, 0, mPrevX, mCtlCY, T().text);
  musDrawIcon(s, 1, mNextX, mCtlCY, T().text);
  uint16_t pc = (musPressCtl == 1) ? blend565(T().accent, T().bg, 70) : (uint16_t)T().accent;
  nyxAaRR(s, mPlayX - mPlayR - 2, mCtlCY - mPlayR - 2, 2 * mPlayR + 4, 2 * mPlayR + 4, 10, pc);
  musDrawIcon(s, playing ? 3 : 2, mPlayX, mCtlCY, T().bg);

"""

LIST = r"""static void musDrawList(LGFX_Sprite& s) {   // MUS_FLAT_V1: baris datar + garis pemisah
  int n = musListCount();
  musLsSet(musLsGet());                             // MUS_UX_V1: rapikan posisi (daftar bisa menyusut)
  musHead(s, RPTR("Daftar Lagu"));
  char cnt[16]; snprintf(cnt, sizeof(cnt), "%d lagu", n);
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(SCR_W - 12 - s.textWidth(cnt), mHdrY + 5); s.print(cnt);

  int maxc = (SCR_W - 80) / 6;
  int lTop = 48, lBot = mBotY - 4;
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
    musRowBg(s, y, cur, prs);
    char num[12]; snprintf(num, sizeof(num), "%d", idx + 1);
    s.setTextSize(1); s.setTextColor(cur ? T().accent : T().subtext);
    s.setCursor(34 - s.textWidth(num), y + 10); s.print(num);
    char nm[64]; musGetTitle(nm, sizeof(nm), idx);
    musTrunc(nm, maxc);
    s.setTextColor(T().text); s.setCursor(46, y + 10); s.print(nm);
    if (cur) musMiniEq(s, SCR_W - 18 - 14, y + 22, musLoaded && !musPaused, T().accent);
  }
  s.clearClipRect();
  musLsDrawBar(s, lTop, lBot, n);                   // MUS_UX_V1: scrollbar mengikuti posisi piksel

  int bw3 = (SCR_W - 24 - 18) / 4;                  // JAM_UI_V1: 4 tombol (▲ ▼ Online Pemutar)
  int bx0 = 12, bx1 = 12 + bw3 + 6, bxJ = 12 + 2 * (bw3 + 6), bx2 = 12 + 3 * (bw3 + 6);
  musBtnGhost(s, bx0, bw3); musArrow(s, bx0, bw3, true);
  musBtnGhost(s, bx1, bw3); musArrow(s, bx1, bw3, false);
  musBtnGhost(s, bxJ, bw3); musDrawLabel(s, "Online", bxJ, mBotY, bw3, mBotH, T().accent);
  musBtnSolid(s, bx2, bw3, T().accent); musDrawLabel(s, RPTR("Pemutar"), bx2, mBotY, bw3, mBotH, T().bg);
}
"""

JAM_TOP = r"""  // kotak cari + tombol Cari  (MUS_FLAT_V1: kotak datar, bingkai aksen saat mengetik)
  int by = musJamBoxY(), bx = 12, bw = musJamSrcX() - 4 - bx;     // AUDIUS_V1: ruang untuk chip sumber
  nyxAaRR(s, bx, by, bw, 22, 6, T().surface);
  nyxAaRRRing(s, bx, by, bw, 22, 6, kbOn ? (uint16_t)T().accent : chip);
  char q[48]; strncpy(q, musJamQuery.c_str(), sizeof(q) - 1); q[sizeof(q) - 1] = 0;
  musTrunc(q, (bw - (musJamQuery.length() > 0 ? 40 : 24)) / 6);   // MUS_UX_V1: ruang tombol x
  s.setTextColor(q[0] ? T().text : T().subtext);
  s.setCursor(bx + 10, by + 7); s.print(q[0] ? q : RPTR("cari lagu / artis"));
  if (kbOn && ((millis() / 500) & 1)) { int cw = q[0] ? s.textWidth(q) : 0; s.fillRect(bx + 10 + cw + 1, by + 5, 1, 12, T().accent); }
  if (musJamQuery.length() > 0) {                    // MUS_UX_V1: tombol hapus teks (x)
    int xx = bx + bw - 13, yy = by + 11;
    for (int d = 0; d < 2; d++) {
      s.drawLine(xx - 4 + d, yy - 4, xx + 4 + d, yy + 4, T().subtext);
      s.drawLine(xx + 4 + d, yy - 4, xx - 4 + d, yy + 4, T().subtext);
    }
  }
  nyxAaRRRing(s, musJamSrcX(), by, MJ_SRC_W, 22, 6, chip);          // AUDIUS_V1: chip sumber
  musDrawLabel(s, jamSrcName(), musJamSrcX(), by, MJ_SRC_W, 22, T().accent2);
  nyxAaRR(s, SCR_W - 12 - MJ_GO_W, by, MJ_GO_W, 22, 6, T().accent);
  musDrawLabel(s, busy ? "..." : RPTR("Cari"), SCR_W - 12 - MJ_GO_W, by, MJ_GO_W, 22, T().bg);

  // chip genre: terpilih = blok aksen, lain = bingkai tipis
  int cw5 = (SCR_W - 24 - 16) / 5, cy = musJamChipY();
  for (int i = 0; i < 5; i++) {
    int x = 12 + i * (cw5 + 4); bool on = (i == musJamTag);
    if (on) nyxAaRR(s, x, cy, cw5, 20, 6, T().accent);
    else    nyxAaRRRing(s, x, cy, cw5, 20, 6, chip);
    musDrawLabel(s, MJ_CHIP_LB[i], x, cy, cw5, 20, on ? T().bg : T().text);
  }

"""

JAM_BOTTOM = r"""  if (!kbOn) {                                      // bar bawah: ▲ ▼ Pemutar
    int bw3 = (SCR_W - 24 - 12) / 3;
    int bx0 = 12, bx1 = 12 + bw3 + 6, bx2 = 12 + 2 * (bw3 + 6);
    musBtnGhost(s, bx0, bw3); musArrow(s, bx0, bw3, true);
    musBtnGhost(s, bx1, bw3); musArrow(s, bx1, bw3, false);
    musBtnSolid(s, bx2, bw3, T().accent);
    musDrawLabel(s, RPTR("Pemutar"), bx2, mBotY, bw3, mBotH, T().bg);
  }
}
"""

SCAN = r"""static void musDrawScan(LGFX_Sprite& s) {   // MUS_FLAT_V1
  musHead(s, RPTR("Cari TWS"));
  if (musScanning) {                                // radar
    int rcx = SCR_W - 24, rcy = mHdrY + 9;
    int ph = (int)((millis() / 90) % 12);
    nyxAaCircle(s, rcx, rcy, 3, T().accent);
    nyxAaCircleRing(s, rcx, rcy, 3 + ph, blend565(T().bg, T().accent, (uint8_t)(255 - ph * 20)));
    nyxAaCircleRing(s, rcx, rcy, 3 + ((ph + 6) % 12), blend565(T().bg, T().accent, (uint8_t)(255 - ((ph + 6) % 12) * 20)));
  }
  s.setTextSize(1);
  s.setCursor(12, 47);
  if (!musCamAlive) { s.setTextColor(T().danger); s.print(RPTR("Modul BT tidak terdeteksi (cek kabel)")); }
  else if (musScanning) {
    s.setTextColor(T().subtext);
    int d = (millis() / 300) % 4;
    s.print(RPTR("Memindai")); for (int i = 0; i < d; i++) s.print(".");
  } else {
    s.setTextColor(T().subtext);
    s.print(musScanCount); s.print(" perangkat. Ketuk untuk menyambung.");
  }

  int rows = musScanRows();
  int maxc = (SCR_W - 90) / 6;
  for (int r = 0; r < rows && r < musScanCount; r++) {
    int y = 58 + r * 30;
    s.drawFastHLine(12, y + 29, SCR_W - 24, musLn());
    musDrawSignal(s, 20, y + 7, musScanRssi[r]);
    char nm[40]; strncpy(nm, musScanName[r], sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    musTrunc(nm, maxc);
    s.setTextSize(1); s.setTextColor(T().text); s.setCursor(48, y + 10); s.print(nm);
    char rs[8]; snprintf(rs, sizeof(rs), "%d", (int)musScanRssi[r]);
    s.setTextColor(T().subtext); s.setCursor(SCR_W - 20 - s.textWidth(rs), y + 10); s.print(rs);
  }
  int bw3s = (SCR_W - 24 - 12) / 3;
  musBtnGhost(s, 12, bw3s);
  musDrawLabel(s, RPTR("Pindai"), 12, mBotY, bw3s, mBotH, T().text);
  if (musOutSpk) { musBtnSolid(s, 12 + bw3s + 6, bw3s, T().good); }
  else           { musBtnGhost(s, 12 + bw3s + 6, bw3s); }
  musDrawLabel(s, musOutSpk ? RPTR("Output: SPK") : RPTR("Output: BT"), 12 + bw3s + 6, mBotY, bw3s, mBotH, musOutSpk ? T().bg : T().text);
  musBtnSolid(s, 12 + (bw3s + 6) * 2, bw3s, T().accent);
  musDrawLabel(s, RPTR("Pemutar"), 12 + (bw3s + 6) * 2, mBotY, bw3s, mBotH, T().bg);
}
"""

JAM_SKEL_OLD = """        musPanel(s, 12, y, SCR_W - 30, 28, 8, T().surface2, a);
        nyxAaCircle(s, 28, y + 14, 10, blend565(T().bg, T().text, 30));
"""
JAM_SKEL_NEW = """        s.drawFastHLine(12, y + 29, SCR_W - 30, musLn());                       // MUS_FLAT_V1
        nyxAaRR(s, 16, y + 8, 12, 12, 2, blend565(T().bg, T().text, (uint8_t)(a / 4)));
"""
JAM_ROW_OLD = """    musPanel(s, 12, y, SCR_W - 30, 28, 8, (cur || prs) ? T().accent : T().surface2, prs ? (cur ? 130 : 60) : (cur ? 90 : 170));
    nyxAaCircle(s, 28, y + 14, 10, cur ? T().accent : blend565(T().bg, T().text, 40));
    char num[12]; snprintf(num, sizeof(num), "%d", idx + 1);
    s.setTextColor(cur ? T().bg : T().subtext);
    s.setCursor(28 - s.textWidth(num) / 2, y + 10); s.print(num);
"""
JAM_ROW_NEW = """    musRowBg(s, y, cur, prs);                                                // MUS_FLAT_V1
    char num[12]; snprintf(num, sizeof(num), "%d", idx + 1);
    s.setTextColor(cur ? T().accent : T().subtext);
    s.setCursor(34 - s.textWidth(num), y + 10); s.print(num);
"""

# ---------------------------------------------------------------- terapkan
# 1) layout
mb = repl_func(mb, "static void musCalcLayout() {", CALC, "musCalcLayout")
# 2) helper datar tepat sesudah musPanel
a, b = func_range(mb, "static void musPanel(LGFX_Sprite& s, int x, int y, int w, int h, int r, uint16_t col, uint8_t a) {", "musPanel")
mb = mb[:b] + HELP + mb[b:]
# 3) cover, eq, chips
mb = repl_func(mb, "static void musDrawCover(LGFX_Sprite& s, bool playing, const char* title) {", COVER, "musDrawCover")
mb = repl_func(mb, "static void musDrawEq(LGFX_Sprite& s, bool playing) {", EQ, "musDrawEq")
mb = repl_func(mb, "static void musDrawChips(LGFX_Sprite& s) {", CHIPS, "musDrawChips")
# 4) pemutar (potongan)
a, b = func_range(mb, "static void musDrawPlayer(LGFX_Sprite& s) {", "musDrawPlayer")
pl = mb[a:b]
pl = sub(pl, "  musDrawCover(s, playing, title);\n", "  musDrawCover(s, playing, title);\n" + LABEL, "player: cover")
pl = sub(pl, "musLiked ? 0xF9A6 :", "musLiked ? (uint16_t)T().danger :", "player: hati")
pl = sub(pl, "    uint16_t vc = musHsv(vi * 45.f, 0.7f, 0.95f);\n    nyxAaCircle(s, mTitX + 4, mVibeY + 4, 3, vc);\n",
         "    s.fillRect(mTitX, mVibeY + 1, 6, 6, T().accent2);\n", "player: titik vibe")
i0 = pl.find("  musDrawEq(s, playing);\n")
i1 = pl.find("  if (musPtrMode == 6) {")
if i0 < 0 or i1 < 0 or i1 < i0: sys.exit("GAGAL: blok bawah pemutar tidak ketemu.")
pl = pl[:i0] + PLAYER_BOTTOM + pl[i1:]
pl = sub(pl, "nyxAaRR(s, px, py, pw, 20, 10, armed", "nyxAaRR(s, px, py, pw, 20, 4, armed", "player: pil geser")
mb = mb[:a] + pl + mb[b:]
# 5) daftar lagu
mb = repl_func(mb, "static void musDrawList(LGFX_Sprite& s) {", LIST, "musDrawList")
# 6) halaman Online (potongan)
a, b = func_range(mb, "static void musDrawJam(LGFX_Sprite& s) {", "musDrawJam")
jm = mb[a:b]
jm = sub(jm, "  uint16_t chip = blend565(T().bg, T().text, 28);\n", "  uint16_t chip = musLn();\n", "jam: chip")
j0 = jm.find("  // kotak cari + tombol Cari\n")
j1 = jm.find("  // daftar\n")
if j0 < 0 or j1 < 0 or j1 < j0: sys.exit("GAGAL: blok atas halaman Online tidak ketemu.")
jm = jm[:j0] + JAM_TOP + jm[j1:]
jm = sub(jm, JAM_SKEL_OLD, JAM_SKEL_NEW, "jam: skeleton")
jm = sub(jm, JAM_ROW_OLD, JAM_ROW_NEW, "jam: baris")
k0 = jm.find("  if (!kbOn) {")
if k0 < 0 or jm.find("  if (!kbOn) {", k0 + 1) >= 0: sys.exit("GAGAL: blok bawah halaman Online tidak ketemu 1x.")
jm = jm[:k0] + JAM_BOTTOM
mb = mb[:a] + jm + mb[b:]
# 7) Cari TWS
mb = repl_func(mb, "static void musDrawScan(LGFX_Sprite& s) {", SCAN, "musDrawScan")
# 8) tab RYNE
mb = sub(mb, "    iosRR(s, x, y, w, h, 7, on ? (uint16_t)T().accent : (uint16_t)T().surface2);\n",
         "    if (on) nyxAaRR(s, x, y, w, h, 5, T().accent); else nyxAaRRRing(s, x, y, w, h, 5, musLn());   // MUS_FLAT_V1\n", "ryne: tab")
# 8b) batang vibe RYNE: tanpa pelangi -- yang dipilih = aksen, sisanya abu datar
mb = sub(mb, "s.fillRect(bx, by, len, bh, musHsv(i * 45.f, 0.7f, hi == i ? 0.98f : 0.7f));",
         "s.fillRect(bx, by, len, bh, hi == i ? (uint16_t)T().accent : blend565(T().bg, T().text, 110));   // MUS_FLAT_V1", "ryne: batang vibe")
mb = mb.replace("//  UI  (v2: cover vinyl berputar, equalizer, panel flat biar ringan)",
                "//  UI  (v2: cover vinyl berputar, equalizer, panel flat biar ringan)\n//  MUS_FLAT_V1: rombak -- gaya datar (blok solid, garis tipis, satu aksen), tanpa gradasi / kaca / glow", 1)
if "iosRR(" in mb: sys.exit("GAGAL: masih ada iosRR (kilap) di musicbt_renphone.ino.")
if M not in mb:
    mb = mb.replace("// MUS_FLAT_V1: rombak", "// " + M + ": rombak", 1)

# ---------------------------------------------------------------- changelog + dokumen
ENTRY = ("- Changed: new flat look for the Music app (MUS_FLAT_V1). The player, Song list, Online page, TWS search and the RYNE tabs lose every gradient, glow, glass panel and glossy button: the album placeholder is one solid colour (8-colour palette picked from the title) with a hard offset block behind it instead of a blur, the equalizer is square single-colour bars, progress and volume are thin lines with an upright marker, Play is a solid accent block, and the header chips are thin-outlined boxes (the TWS dot blinks while connecting). Lists are flat rows with a hairline between them and an accent bar on the playing song; page titles get a short accent underline; bottom buttons are outlined or solid. The player landscape layout gets a small status label (*SEDANG DIPUTAR* / *DIJEDA* / *ONLINE*) above the title. Positions of every touch target and the 30 px row pitch are unchanged. Only drawing code changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_music_flat_ui.py`\n")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n" + ENTRY, "changelog")
md = sub(md, "- Up to 200 songs in the `music/` folder.\n",
         "- Up to 200 songs in the `music/` folder.\n- **Look.** Flat and single-accent: solid colour blocks, thin lines and square-ish corners, with no gradients, glow or glass. The cover placeholder is one solid colour from the title with a spinning record on it.\n", "media.md")

if CHECK:
    print("anchor OK (%s)" % M); sys.exit(0)
bak = MB + ".bak_" + M
if not os.path.exists(bak): wr(bak, rd(MB))
wr(MB, mb); wr(CL, cl); wr(MD, md)
print("terpasang: %s  (backup: %s)" % (M, bak))
