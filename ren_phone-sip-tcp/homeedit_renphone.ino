// =====================================================================
// homeedit_renphone.ino -- ATUR HOME: urutan ikon + sembunyikan app dari grid Home.
// Data (homeOrder/homeHide/homeVis + simpan/muat NVS) ada di phone.ino (dipakai drawHome).
// Layar ini cuma antarmuka: daftar semua app, tombol naik / turun / Tampil-Sembunyi per baris.
// Hanya tipe primitif di parameter fungsi (aman dari bug prototype otomatis arduino-cli).
// =====================================================================
#define HE_RH 30
#define HE_WROWS 6   // v132: home widget setting -- 6 baris pengaturan widget di atas daftar app
static int  heScroll = 0;
static bool heDown = false, heDrag = false;
static int  heSx = 0, heSy = 0, heLy = 0;
static unsigned long heResetArmUntil = 0;

static int heTop() { return 48; }
static int heBot() { return backY() - 6; }
static int heMaxScroll() { int m = (APP_COUNT + HE_WROWS) * HE_RH - (heBot() - heTop()); return m > 0 ? m : 0; }
static void heClamp() { int m = heMaxScroll(); if (heScroll > m) heScroll = m; if (heScroll < 0) heScroll = 0; }

// tombol di tiap baris (dari kanan): [naik] [turun] [Tampil/Sembunyi]
static int heVisW()  { return 56; }
static int heVisX()  { return SCR_W - 14 - heVisW(); }
static int heDownX() { return heVisX() - 4 - 26; }
static int heUpX()   { return heDownX() - 4 - 26; }
static void heResetRect(int& x, int& y, int& w, int& h) { w = 68; h = 20; x = SCR_W - w - 8; y = 27; }

void homeEditEnter() {
  homeLayoutEnsure();
  heScroll = 0; heDown = false; heDrag = false; heResetArmUntil = 0;
}

void homeEditExit() {
  heResetArmUntil = 0; heDown = false;
  homeScrollY = 0; homeScrollVel = 0; homeScrollSpringing = false;   // jumlah baris Home bisa berubah -> mulai dari atas
}

void drawHomeEdit(LGFX_Sprite& s) {
  homeLayoutEnsure();
  iosBackdrop(s); drawStatusBar(s);
  iosTitle(s, 8, 26, "Atur Home");

  int rx, ry, rw, rh; heResetRect(rx, ry, rw, rh);
  bool armed = millis() < heResetArmUntil;
  drawGlassPanel(s, rx, ry, rw, rh, 10, armed ? T().danger : T().surface2, 170);
  s.setTextSize(1); s.setTextColor(armed ? (uint16_t)0xFFFF : T().danger);
  const char* rl = armed ? "Yakin?" : "Reset";
  s.setCursor(rx + rw / 2 - s.textWidth(rl) / 2, ry + rh / 2 - 4); s.print(rl);
  if (armed) needRedraw = true;                 // label "Yakin?" kembali sendiri saat waktu habis

  if (SCR_W > SCR_H) {
    char sm[28]; snprintf(sm, sizeof(sm), "%d tampil / %d sembunyi", homeVisN, APP_COUNT - homeVisN);
    s.setTextColor(T().subtext); s.setCursor(rx - 8 - s.textWidth(sm), 31); s.print(sm);
  }

  int top = heTop(), bot = heBot();
  drawGlassPanel(s, 8, top - 2, SCR_W - 16, bot - top + 4, 12, T().surface2, 150);
  heClamp();
  s.setClipRect(8, top, SCR_W - 16, bot - top);
  {                                                    // v132: home widget setting -- baris pengaturan widget
    static const char* const wl[HE_WROWS] = {"Kartu Jam", "Kartu Musik", "Kartu Baterai", "Kartu Cuaca", "Susunan (potret)", "Ukuran kartu Jam"};
    for (int r = 0; r < HE_WROWS; r++) {
      int y = top + r * HE_RH - heScroll;
      if (y + HE_RH < top || y > bot) continue;
      bool isTog = (r < 4);
      bool on = isTog ? (((homeWMask >> r) & 1) != 0) : (r == 4 ? (homeWLayout != 0) : (homeWBig != 0));
      const char* bl = isTog ? (on ? "Tampil" : "Sembunyi") : (r == 4 ? (on ? "Ringkas" : "Standar") : (on ? "Besar" : "Normal"));
      bool dim = isTog && !on;
      s.setTextSize(1); s.setTextColor(dim ? T().subtext : T().text);
      s.setCursor(18, y + HE_RH / 2 - 4); s.print(wl[r]);
      int by = y + (HE_RH - 22) / 2;
      drawGlassPanel(s, heVisX(), by, heVisW(), 22, 8, dim ? T().surface2 : T().accent, 190);
      s.setTextColor(dim ? T().subtext : T().text);
      s.setCursor(heVisX() + heVisW() / 2 - s.textWidth(bl) / 2, by + 7); s.print(bl);
      s.drawFastHLine(14, y + HE_RH - 1, SCR_W - 28, T().divider);
    }
  }
  int nameX = 46, nameMax = heUpX() - 6 - nameX;
  for (int k = 0; k < APP_COUNT; k++) {
    int y = top + (HE_WROWS + k) * HE_RH - heScroll;
    if (y + HE_RH < top || y > bot) continue;
    int i = homeOrder[k];
    bool hid = homeHide[i];
    uint16_t ic = hid ? blend565(T().bg, apps[i].color, 90) : apps[i].color;
    drawAppIcon(s, apps[i].sym, 28, y + HE_RH / 2, 11, ic);

    char nm[24]; strncpy(nm, apps[i].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    int n = (int)strlen(nm);
    s.setTextSize(1);
    while (n > 1 && s.textWidth(nm) > nameMax) nm[--n] = 0;
    s.setTextColor(hid ? T().subtext : T().text);
    s.setCursor(nameX, y + HE_RH / 2 - 4); s.print(nm);

    int by = y + (HE_RH - 22) / 2;
    bool canUp = (k > 0), canDn = (k < APP_COUNT - 1);
    uint16_t on = T().text, off = blend565(T().bg, T().text, 60);
    drawGlassPanel(s, heUpX(), by, 26, 22, 8, T().surface2, 170);
    s.fillTriangle(heUpX() + 13, by + 6, heUpX() + 7, by + 15, heUpX() + 19, by + 15, canUp ? on : off);
    drawGlassPanel(s, heDownX(), by, 26, 22, 8, T().surface2, 170);
    s.fillTriangle(heDownX() + 7, by + 7, heDownX() + 19, by + 7, heDownX() + 13, by + 16, canDn ? on : off);
    drawGlassPanel(s, heVisX(), by, heVisW(), 22, 8, hid ? T().surface2 : T().accent, 190);
    const char* vl = hid ? "Sembunyi" : "Tampil";
    s.setTextColor(hid ? T().subtext : T().text);
    s.setCursor(heVisX() + heVisW() / 2 - s.textWidth(vl) / 2, by + 7); s.print(vl);

    if (k < APP_COUNT - 1) s.drawFastHLine(14, y + HE_RH - 1, SCR_W - 28, T().divider);
  }
  s.clearClipRect();

  drawBack(s);
  drawToast(s);
}

static void heTap(int x, int y) {
  int top = heTop();
  int r0 = (y - top + heScroll) / HE_RH;
  if (r0 < 0) return;
  if (r0 < HE_WROWS) {                                  // v132: ketuk baris pengaturan widget
    if (x < heUpX() - 2) return;
    if (r0 < 4) homeWMask ^= (uint8_t)(1u << r0);
    else if (r0 == 4) homeWLayout = homeWLayout ? 0 : 1;
    else homeWBig = homeWBig ? 0 : 1;
    homeLayoutSave();
    needRedraw = true;
    return;
  }
  int k = r0 - HE_WROWS;
  if (k < 0 || k >= APP_COUNT) return;
  int i = homeOrder[k];
  if (x >= heUpX() - 2 && x <= heUpX() + 28) {                    // naik
    if (k > 0) { int t = homeOrder[k - 1]; homeOrder[k - 1] = homeOrder[k]; homeOrder[k] = t; heScroll -= HE_RH; }
    else return;
  } else if (x >= heDownX() - 2 && x <= heDownX() + 28) {         // turun
    if (k < APP_COUNT - 1) { int t = homeOrder[k + 1]; homeOrder[k + 1] = homeOrder[k]; homeOrder[k] = t; heScroll += HE_RH; }
    else return;
  } else if (x >= heVisX() - 2 && x <= heVisX() + heVisW() + 2) { // tampil / sembunyi
    homeHide[i] = !homeHide[i];
    if (homeHide[i]) showToast("Disembunyikan -- tetap bisa dicari");
  } else return;
  heClamp();
  homeLayoutRebuildVis();
  homeLayoutSave();
  needRedraw = true;
}

// Dipanggil tiap putaran loop() selagi layar ini terbuka: geser daftar + ketuk diproses saat jari LEPAS.
void homeEditPointerTick(bool touched, bool wasT, int tx, int ty) {
  int top = heTop(), bot = heBot();
  if (touched) {
    if (!wasT) { heDown = (ty >= top && ty < bot); heSx = tx; heSy = ty; heLy = ty; heDrag = false; }
    else if (heDown) {
      if (!heDrag && abs(ty - heSy) > 8) heDrag = true;
      if (heDrag) { heScroll += heLy - ty; heLy = ty; heClamp(); needRedraw = true; }
    }
  } else {
    if (wasT && heDown && !heDrag) heTap(heSx, heSy);
    heDown = false;
  }
}

// Sentuhan generik (saat jari baru menyentuh): tombol Kembali & Reset. Daftar ditangani homeEditPointerTick().
void homeEditTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }
  int rx, ry, rw, rh; heResetRect(rx, ry, rw, rh);
  if (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh) {
    if (millis() < heResetArmUntil) {
      heResetArmUntil = 0;
      homeLayoutDefault(); homeLayoutSave();
      heScroll = 0;
      showToast("Home dikembalikan ke awal");
    } else {
      heResetArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk reset");
    }
    needRedraw = true;
  }
}
