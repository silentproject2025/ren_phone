// =====================================================================
// pager_renphone.ino -- HOME_PAGER_V1 (dipasang oleh apply_home_pager.py)
//
// HOME LANDSCAPE BERHALAMAN (geser kiri/kanan) + HARDWARE SCROLL ST7796 pada sumbu 480 panel.
//
// Kenapa: di landscape, scroll vertikal = sumbu pendek panel (320) -> ST7796 tidak punya hardware scroll
// di sumbu itu, jadi tiap frame harus kirim ~220 KB lewat SPI (~44 ms @40 MHz). Geser HORIZONTAL di
// landscape = sumbu panjang panel (480 baris memori) -> bisa pakai VSCRDEF/VSCSAD seperti mode portrait:
// per frame cuma kolom baru yang ditulis (beberapa KB), sisanya digeser oleh panel sendiri.
//
// Cara kerja (ringkas):
//  * Isi Home (widget + chip + grid) TIDAK diubah. Dipotong jadi beberapa "halaman": halaman p = Home
//    pada scroll vertikal hpSc[p] (kelipatan baris grid). Saat diam, homeScrollY == hpSc[halaman] sehingga
//    seluruh kode lama (hit-test, zoom ikon, jiggle, pencarian) bekerja apa adanya.
//  * Tiap halaman adalah gambar layar penuh 480x320 (status bar + dock ikut) di PSRAM (maks 3 slot).
//    Dibuat saat Home diam (pemanasan), bukan di tengah gerakan jari.
//  * Sumbu virtual: kolom x halaman p = p*480 + x. Mesin HwsEngine (hwscroll_renphone.ino, TIDAK diubah)
//    dipakai apa adanya: "baris" = kolom layar. Ring panel D=480, B0=0, H=480.
//  * Fisika: seret ikut jari (rubber-band tepi), lepas -> pegas ke halaman terdekat. Geser vertikal
//    juga membalik halaman (kebiasaan lama), tanpa mengikuti jari.
//  * Mode runtime (NVS "ui"/"hwl", baris Pengaturan > Scroll Hardware saat HP landscape):
//    0 = Mati (scroll vertikal lama), 1 = Auto (halaman + HW scroll), 2 = Balik (arah HW dibalik).
//  * Fallback tanpa HW (Remote Control aktif, mode game, PSRAM tipis): gambar disusun ke canvas lalu push().
//  * Keluar mode (transisi, kunci, CC, shade, app lain, ganti orientasi, ...): lewat hwsLeave() -> hpLeave().
//  * Semua akses display HANYA dari loop().
//
// Bagian HP_PURE dipakai tes PC (tests/pager/). -DRP_HOME_PAGER=0 mematikan semuanya.
// =====================================================================
#ifndef RP_HOME_PAGER
#define RP_HOME_PAGER 1
#endif
#ifndef RP_HWL_DEFAULT_MODE
#define RP_HWL_DEFAULT_MODE 1
#endif
#define HP_MAXP 12

// ---- HP_PURE_BEGIN ----
// Susunan halaman: sc[] = scroll vertikal tiap halaman. Mengembalikan jumlah halaman (>=1).
// bh = tinggi pita grid, pitch = tinggi baris grid (kartu+gap 10), gTop = homeGridTop(), catH = HOME_CAT_H.
// Baris dianggap muat kalau terpotong <= 10 px di bawah (HP_TOLB); bagian itu cuma bayangan kartu/ruang label.
#define HP_TOLB 10
static inline int hpPlanPages(int* sc, int maxP, int maxS, int bh, int pitch, int gTop, int catH, int clipTop){
  int n = 0;
  sc[n++] = 0;
  if(maxS <= 0 || bh < 80 || pitch < 8) return 1;
  const int gap = 10, titleH = catH + 24;
  int r1 = (bh - titleH + gap + HP_TOLB) / pitch; if(r1 < 1) r1 = 1;   // halaman 1: judul + chip + r1 baris
  int r  = (bh - 4 + gap + HP_TOLB) / pitch;      if(r  < 1) r  = 1;   // halaman berikutnya: r baris
  int next = gTop - titleH - clipTop;             // judul tepat di bawah status bar
  int rowsShown = 0;
  while(n < maxP){
    if(next <= sc[n-1]) next = sc[n-1] + pitch;
    if(next >= maxS){   // halaman terakhir rata dgn ujung konten; kalau nyaris kembar dgn halaman sebelumnya, gabungkan (tes memastikan tak ada baris hilang)
      if(sc[n-1] < maxS){ if(n >= 2 && maxS - sc[n-1] <= pitch / 2 + 4) sc[n-1] = maxS; else sc[n++] = maxS; }
      break;
    }
    sc[n++] = next;
    rowsShown = (n == 2) ? r1 : rowsShown + r;
    next = gTop + rowsShown * pitch - clipTop - 4;
  }
  return n;
}
// Area (layar y) yang harus ditutup warna latar supaya tak ada baris/judul yang terlihat setengah.
// Kembali jumlah segmen (a,b) di out[][2], sudah dipotong ke pita [clipTop, bot].
static inline int hpCoverRanges(int sc, int clipTop, int bot, int cardH, int pitch, int gTop, int catH, int rows, int out[][2], int maxOut){
  int c = 0;
  struct Blk { int a, b, tolTop, tolBot; };
  // judul+chip: harus utuh (toleransi 0); baris grid: toleransi atas 4, bawah HP_TOLB
  for(int k = -1; k < rows; k++){
    Blk bl;
    if(k < 0){ bl.a = gTop - catH - 24 - sc; bl.b = gTop - 2 - sc; bl.tolTop = 0; bl.tolBot = 0; }
    else     { bl.a = gTop + k * pitch - sc;  bl.b = bl.a + cardH;  bl.tolTop = 4; bl.tolBot = HP_TOLB; }
    if(bl.b <= clipTop || bl.a >= bot) continue;                 // di luar pita
    const bool partial = (bl.a < clipTop - bl.tolTop) || (bl.b > bot + bl.tolBot);
    if(!partial) continue;
    int a = bl.a < clipTop ? clipTop : bl.a, b = bl.b > bot ? bot : bl.b;
    if(b > a && c < maxOut){ out[c][0] = a; out[c][1] = b; c++; }
  }
  return c;
}
static inline int hpNearestPage(const int* sc, int n, float v){
  int best = 0; float bd = 1e9f;
  for(int i = 0; i < n; i++){ float d = v > sc[i] ? v - sc[i] : sc[i] - v; if(d < bd){ bd = d; best = i; } }
  return best;
}
// Halaman tujuan saat jari dilepas. x = posisi sumbu (px), v = px/ms, start = halaman awal drag.
// FEEL-FIX: kalibrasi ala launcher HP. Dulu harus seret > 50% layar ATAU flick > 0.35 px/ms, jadi terasa 'cuma ngikut jari lalu mental balik'.
#define HP_FLICK_V    0.16f   // px/ms: flick lebih pelan dari ini dianggap seret biasa (dulu 0.35)
#define HP_FLICK_MIN  8.0f    // px: geser minimal supaya flick dihitung (tap/getar jari tidak membalik halaman)
#define HP_FLIP_FRAC  0.22f   // pecahan lebar layar: seret lebih jauh dari ini = pindah halaman (dulu 0.5)
#define HP_SPRING_K   0.00070f  // kekakuan pegas (dulu 0.0011): lebih lembut = geser lebih mengalir
#define HP_SPRING_C   0.050f    // redaman: zeta ~ 0.94 (dulu ~0.75 = memantul/goyang di halaman tujuan)
static inline int hpPickTarget(float x, float v, int start, int n, int W){
  int tgt = start;
  const float d = x - (float)(start * W);
  if(v > HP_FLICK_V && d > HP_FLICK_MIN) tgt = start + 1;
  else if(v < -HP_FLICK_V && d < -HP_FLICK_MIN) tgt = start - 1;
  else if(d > (float)W * HP_FLIP_FRAC) tgt = start + 1;
  else if(d < -(float)W * HP_FLIP_FRAC) tgt = start - 1;
  if(tgt < start - 1) tgt = start - 1;
  if(tgt > start + 1) tgt = start + 1;
  if(tgt < 0) tgt = 0;
  if(tgt > n - 1) tgt = n - 1;
  return tgt;
}
// ---- HP_PURE_END ----

#if RP_HOME_PAGER && RP_HW_SCROLL && defined(RP_DISPLAY_ST7796)

extern bool homeSearchActive();
static int   hpN = 0;                 // jumlah halaman (0 = belum dihitung)
static int   hpSc[HP_MAXP];           // scroll vertikal-ekuivalen tiap halaman
static long  hpSig = -1;
static int   hpCur = 0;               // halaman saat diam
static float hpX = 0.0f;              // posisi sumbu halaman (px layar), 0..(hpN-1)*SCR_W
static float hpVel = 0.0f;            // px/ms
static float hpDragRaw = 0.0f, hpTarget = 0.0f, hpFx = 0.0f, hpFxLast = 0.0f, hpFy = 0.0f, hpFyLast = 0.0f, hpAccum = 0.0f;
static bool  hpCurDirty = true;       // gambar halaman SEKARANG sudah basi (widget hidup berubah); halaman tetangga tidak ikut dibuang
static unsigned long hpImgAt[3] = { 0, 0, 0 };   // kapan slot dibuat (utk penyegaran tetangga yang tua)
#define HP_VGAIN 2.4f                 // geser vertikal: 1 px jari = 2.4 px sumbu halaman (~200 px jari = 1 halaman)
static unsigned long hpSampleMs = 0;
static int   hpStartPage = 0, hpLock = 0;     // hpLock: 0 belum, 1 horizontal (ikut jari), 2 vertikal (balik halaman)
static bool  hpDragging = false, hpSpringing = false, hpPending = false, hpWasOn = false;
static bool  hpForeign = false, hpInternal = false, hpCanvasStale = false;
static int   hpCanvasPg = -2;         // halaman yang SEDANG tergambar di canvas (-2 = tak diketahui)
static unsigned long hpLastFastMs = 0, hpBlockUntil = 0;
static const char* hpLastWhy = nullptr;
static HwsEngine hpEng;
static int   hwlModeV = -1;

static LGFX_Sprite hpImg0(&display), hpImg1(&display), hpImg2(&display);
static LGFX_Sprite* const hpImgP[3] = { &hpImg0, &hpImg1, &hpImg2 };
static int   hpImgPg[3] = { -1, -1, -1 };     // halaman yang tersimpan di slot (p % 3); -1 = tak sah

// ---------------- mode (NVS) ----------------
static int hwlModeLoad(){
  if(hwlModeV < 0){
    Preferences p; p.begin("ui", true);
    int v = p.getInt("hwl", RP_HWL_DEFAULT_MODE);
    p.end();
    if(v < 0 || v > 2) v = RP_HWL_DEFAULT_MODE;
    hwlModeV = v;
    Serial.printf("[hp] mode landscape NVS = %d\n", v);
  }
  return hwlModeV;
}
static bool hwlModeStore(int v){
  if(v < 0 || v > 2) return false;
  Preferences p;
  if(!p.begin("ui", false)){ Serial.println("[hp] NVS begin(ui) GAGAL"); return false; }
  size_t w = p.putInt("hwl", v);
  int back = p.getInt("hwl", -1);
  p.end();
  bool ok = (w > 0 && back == v);
  Serial.printf("[hp] simpan mode landscape %d: %s\n", v, ok ? "OK" : "GAGAL");
  if(ok) hwlModeV = v;
  return ok;
}
// dipanggil hwsModeGet()/hwsModeSet() (hwscroll_renphone.ino) saat orientasi landscape
int  hpModeGet(){ return hwlModeLoad(); }
bool hpModeSet(int v){ return hwlModeStore(v); }

// ---------------- syarat ----------------
static inline int hpW(){ return SCR_W; }
static bool hpGeomOk(){
  return currentOrient == ORIENT_LANDSCAPE && display.getRotation() == 1
      && SCR_W == RP_DISP_NATIVE_H && SCR_H == RP_DISP_NATIVE_W
      && canvas.getBuffer() && canvas.width() == SCR_W && canvas.height() == SCR_H;
}
// Syarat dasar pager (gesture + halaman). Belum termasuk jumlah halaman.
static bool hpAllowed(){
  if(hwlModeLoad() == 0) return false;
  if(!hpGeomOk()) return false;
  if(curScreen() != SCR_HOME) return false;
  if(locked || appSwitcherOpen || controlCenterOpen || panelPullActive || ccAnimating || notifShadeOpen) return false;
  if(homeAodActive || diMenuOpen || powerMenuOpen || callOverlayActive()) return false;
  if(homeSearchActive() || homeJiggle || homeGoneIdx >= 0 || kbVisible) return false;
  if(homeSearchBar.open || homeSearchBar.animating) return false;
  return true;
}
// Boleh pakai hardware scroll (selain itu: gambar disusun ke canvas lalu push biasa).
static bool hpHwOk(){ return hwlModeLoad() != 0 && !rcApIsUp() && !gameModeActive; }

static const char* hpWhyNot(){
  if(hwlModeLoad() == 0) return "mode Mati";
  if(!hpGeomOk()) return "bukan landscape / geometri tak cocok";
  if(curScreen() != SCR_HOME) return "pindah ke layar lain";
  if(locked) return "terkunci";
  if(appSwitcherOpen) return "App Switcher";
  if(controlCenterOpen || panelPullActive || ccAnimating) return "Control Center";
  if(notifShadeOpen) return "Notification Shade";
  if(homeAodActive) return "AOD";
  if(diMenuOpen) return "menu Dynamic Island";
  if(powerMenuOpen) return "menu Daya";
  if(callOverlayActive()) return "layar panggilan";
  if(homeSearchActive() || homeSearchBar.open || homeSearchBar.animating) return "pencarian";
  if(homeJiggle || homeGoneIdx >= 0) return "mode atur Home";
  if(kbVisible) return "keyboard";
  if(!hpHwOk()) return "HW scroll tidak boleh (Remote Control / mode game)";
  if(hpN < 2) return "halaman < 2";
  if(hpEng.active && (hpEng.g.D != SCR_W || hpEng.g.H != SCR_W)) return "geometri berubah";
  return nullptr;
}
static void hpNote(const char* why){
  if(why != hpLastWhy){ hpLastWhy = why; if(why) Serial.printf("[hp] fallback: %s\n", why); }
}

// ---------------- halaman ----------------
static void hpImgInvalidateAll(){ hpImgPg[0] = hpImgPg[1] = hpImgPg[2] = -1; }
static void hpImgFreeAll(){
  for(int i = 0; i < 3; i++){ if(hpImgP[i]->getBuffer()) hpImgP[i]->deleteSprite(); hpImgPg[i] = -1; }
  hpCanvasPg = -2;
}
static void hpSnapTo(int p){
  if(p < 0) p = 0;
  if(p > hpN - 1) p = hpN - 1;
  hpCur = p; hpX = (float)(p * hpW()); hpCurDirty = false;
  hpVel = 0.0f; hpSpringing = false; hpDragging = false; hpPending = false;
  homeScrollY = (float)hpSc[p];
  homeScrollVel = 0.0f; homeScrollSpringing = false; homeSpringMode = false;
}
static void hpLeave(const char* why, int mode);
// Hitung ulang daftar halaman kalau isi/ukuran Home berubah (kategori, app disembunyikan, orientasi, ...).
static void hpEnsurePages(){
  homeLayoutEnsure();
  const long sig = (long)homeShowN * 131L + (long)homeCols() * 7919L + (long)homeMaxScroll() * 31L + (long)SCR_W * 104729L + (long)homeDockY();
  if(sig == hpSig && hpN > 0) return;
  if(hpEng.active) hpLeave("daftar halaman berubah", 2);
  const int clipTop = homeGridClipTop(), bot = homeDockY() - 4;
  hpN = hpPlanPages(hpSc, HP_MAXP, homeMaxScroll(), bot - clipTop, homeCardH() + 10, homeGridTop(), HOME_CAT_H, clipTop);
  hpSig = sig;
  hpImgInvalidateAll(); hpCanvasPg = -2;
  if(hpN >= 2){
    int p = hpNearestPage(hpSc, hpN, homeScrollY);
    if(!hpDragging && !hpSpringing){ hpCur = p; hpX = (float)(p * hpW()); }
  } else { hpCur = 0; hpX = 0.0f; }
  Serial.printf("[hp] halaman=%d (maxScroll=%d)\n", hpN, homeMaxScroll());
}
// true = pager aktif (landscape + Home + tidak ada overlay + >= 2 halaman)
bool hpOn(){
  bool a = hpAllowed();
  if(a){ hpEnsurePages(); if(hpN < 2) a = false; }
  if(a && !hpWasOn){            // baru aktif: rapikan ke halaman terdekat
    hpSnapTo(hpNearestPage(hpSc, hpN, homeScrollY));
    needRedraw = true;
  }
  if(!a && hpWasOn){ hpDragging = false; hpSpringing = false; hpPending = false; }
  hpWasOn = a;
  return a;
}
bool hpIsActive(){ return hpEng.active; }
bool hpMotionNow(){ return hpDragging || hpSpringing; }

// ---------------- gambar halaman ----------------
static bool hpImgAlloc(LGFX_Sprite& sp){
  if(sp.getBuffer() && sp.width() == SCR_W && sp.height() == SCR_H) return true;
  if(sp.getBuffer()) sp.deleteSprite();
  const size_t bytes = (size_t)SCR_W * (size_t)SCR_H * 2;
  if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 1500000UL) return false;   // sisakan utk Doom/NES/dll
  sp.setColorDepth(16); sp.setPsram(true);
  return sp.createSprite(SCR_W, SCR_H);
}
static void hpRenderPage(int p){      // gambar Home pada scroll halaman p ke canvas
  const float sv = homeScrollY;
  homeScrollY = (float)hpSc[p];
  canvas.clearClipRect();
  renderCurrentFrame();
  homeScrollY = sv;
  // tutup baris/judul yang terlihat setengah di tepi atas/bawah pita (hanya latar polos; wallpaper dibiarkan)
  // HOME-FIX: dulu hanya jalan kalau TIDAK ada wallpaper -> dgn wallpaper baris setengah (atas/bawah) tetap kelihatan.
  // Sekarang area itu ditimpa potongan wallpaper (atau warna latar kalau tak ada wallpaper).
  {
    int rg[17][2];
    const int cols = homeCols(), rows = (homeShowN + cols - 1) / cols;
    int n = hpCoverRanges(hpSc[p], homeGridClipTop(), homeDockY() - 4, homeCardH(), homeCardH() + 10, homeGridTop(), HOME_CAT_H, rows, rg, 16);
    if(p == 0){   // halaman 1 = widget saja: sembunyikan judul + chip + seluruh grid di bawah widget
      const int cut = homeWidgetsEnd() + 4 - hpSc[0];
      if(cut < homeDockY() - 4){ rg[n][0] = cut > homeGridClipTop() ? cut : homeGridClipTop(); rg[n][1] = homeDockY() - 4; n++; }
    }
    for(int i = 0; i < n; i++){
      const int a = rg[i][0], h = rg[i][1] - rg[i][0];
      if(h <= 0) continue;
      if(wallpaperReady){ canvas.setClipRect(0, a, SCR_W - 12, h); wallpaperImg.pushSprite(&canvas, 0, 0); canvas.clearClipRect(); }
      else canvas.fillRect(0, a, SCR_W - 12, h, T().bg);   // sisakan 12 px kanan utk rail
    }
  }
  hpCanvasPg = p;
}
static LGFX_Sprite* hpImgGet(int p, bool build){
  if(p < 0 || p >= hpN) return nullptr;
  const int s = p % 3;
  const bool valid = (hpImgPg[s] == p && hpImgP[s]->getBuffer());
  if(valid && !(build && p == hpCur && hpCurDirty)) return hpImgP[s];   // basi tetap dipakai utk gerakan (tak ada render berat di tengah jari)
  if(!build) return nullptr;
  LGFX_Sprite& im = *hpImgP[s];
  if(!hpImgAlloc(im)){ hpImgPg[s] = -1; return nullptr; }
  if(hpCanvasPg != p) hpRenderPage(p);
  memcpy(im.getBuffer(), canvas.getBuffer(), (size_t)SCR_W * (size_t)SCR_H * 2);
  hpImgPg[s] = p; hpImgAt[s] = millis();
  if(p == hpCur) hpCurDirty = false;
  return &im;
}
// canvas := tampilan pada offset sumbu px (dari gambar halaman). Gambar yang hilang dibangun DULU
// (membangun menimpa canvas), baru disusun.
static bool hpCompose(int px){
  const int W = hpW(), H = SCR_H;
  const int a = (px >= 0) ? (px / W) : -1;
  const int b = (px + W - 1 >= 0) ? ((px + W - 1) / W) : -1;
  for(int p = a; p <= b; p++) if(p >= 0 && p < hpN && !hpImgGet(p, true)) return false;
  int x = 0;
  while(x < W){
    int v = px + x;
    int p = (v >= 0) ? (v / W) : -((-v + W - 1) / W);
    int xl = v - p * W;
    int run = W - xl; if(run > W - x) run = W - x;
    canvas.setClipRect(x, 0, run, H);
    LGFX_Sprite* im = (p >= 0 && p < hpN) ? hpImgGet(p, false) : nullptr;
    if(im) im->pushSprite(&canvas, x - xl, 0); else canvas.fillRect(x, 0, run, H, T().bg);
    x += run;
  }
  canvas.clearClipRect();
  hpCanvasPg = -2;
  return true;
}
static void hpSyncCanvas(){
  if(!hpEng.active && !hpMotionNow()){ if(hpCanvasPg == hpCur) { hpCanvasStale = false; return; } }
  hpCompose((int)lroundf(hpX));
  hpCanvasPg = ((int)lroundf(hpX) == hpCur * hpW()) ? hpCur : -2;
  hpCanvasStale = false;
}

// ---------------- sink LovyanGFX (sumbu = x layar) ----------------
struct HpSink : HwsLgfxSink {
  static LGFX_Sprite* img(int p){ return hpImgGet(p, false); }
  void scrollOff() override { scrollDef(0, RP_DISP_NATIVE_H, 0); scrollAddr(0); display.writeCommand(0x13); }
  void writeCache(int yw, int n, int c0) override {     // kolom virtual [c0,c0+n) -> kolom layar-tanpa-scroll yw
    const int W = hpW();
    while(n > 0){
      int p = c0 / W, xl = c0 % W;
      int run = W - xl; if(run > n) run = n;
      display.setClipRect(yw, 0, run, SCR_H);
      LGFX_Sprite* im = img(p);
      if(im) im->pushSprite(&display, yw - xl, 0); else display.fillRect(yw, 0, run, SCR_H, T().bg);
      display.clearClipRect();
      yw += run; c0 += run; n -= run;
    }
  }
  void writeBg(int yw, int n) override { display.fillRect(yw, 0, n, SCR_H, T().bg); }
  void writeCanvas(int yw, int n, int line0) override {
    display.setClipRect(yw, 0, n, SCR_H);
    canvas.pushSprite(&display, yw - (hpEng.g.B0 + line0), 0);
    display.clearClipRect();
  }
  void writePlain(int y0, int n) override {
    display.setClipRect(y0, 0, n, SCR_H);
    canvas.pushSprite(&display, 0, 0);
    display.clearClipRect();
  }
};
static HpSink hpSink;

static HwsGeom hpGeomCur;                  // diisi hpGeomFill() (hindari fungsi bebas bertipe struct: prototipe otomatis Arduino)
static bool hpGeomFill(){
  hpGeomCur.D = SCR_W; hpGeomCur.B0 = 0; hpGeomCur.H = SCR_W;
  hpGeomCur.flip = ((hwlModeLoad() == 2) != (display.getRotation() == 3));
  return true;
}

// mode: 0 = tidak tulis apa pun | 1 = sinkron canvas | 2 = sinkron canvas + tulis ulang panel
static void hpLeave(const char* why, int mode){
  if(!hpEng.active) return;
  if(!hwsOnLoop()){ hpForeign = true; return; }
  const bool canvasOk = hpGeomOk();
  if(mode >= 1 && canvasOk) hpSyncCanvas(); else mode = 0;
  const int kk = hpEng.k();
  hpEng.leave(hpSink, mode >= 2 && canvasOk);
  Serial.printf("[hp] keluar: %s (k=%d, mode-tulis=%d, sesi: cepat=%lu penuh=%lu lambat=%lu kolom=%lu)\n",
                why ? why : "-", kk, mode, hpEng.nFast, hpEng.nFull, hpEng.nSlow, hpEng.nRows);
  hpCanvasStale = false;
#if RP_PUSH_DIFF
  rpShValid = false;
#endif
  hpLastWhy = why;
}
// dipanggil dari awal hwsLeave() supaya semua titik keluar lama otomatis mencakup pager
void hpLeaveHook(const char* why, int mode){ hpLeave(why, mode); }

// ---------------- hook: dispatch render Home ----------------
// true = frame sudah ditangani (tidak perlu render/push biasa)
bool hpFrame(){
  if(!hpPending) return false;
  hpPending = false;
  if(!hpOn() || !hwsOnLoop()) return false;
  const int W = hpW();
  int px = (int)lroundf(hpX);
  const bool settled = !hpDragging && !hpSpringing;
  // pastikan semua halaman yang tampak pada [px, px+W) ada (membangun menimpa canvas -> canvas basi, aman di jalur HW)
  { const int a = (px >= 0) ? (px / W) : -1, b = (px + W - 1 >= 0) ? ((px + W - 1) / W) : -1;
    for(int p = a; p <= b; p++) if(p >= 0 && p < hpN) hpImgGet(p, true); }
  const char* why = hpWhyNot();
  if(!why){
    hpNote(nullptr);
    hpGeomFill(); const HwsGeom& g = hpGeomCur;
    if(!hpEng.active){
      hpEng.nFast = hpEng.nFull = hpEng.nSlow = hpEng.nRows = 0;
      hpEng.enter(hpSink, g, px, hpN * W, false);
      Serial.printf("[hp] masuk: mode=%d D=%d H=%d TFA=%d BFA=%d flip=%d px=%d halaman=%d\n", hwlModeLoad(), g.D, g.H, g.tfaReg(), g.bfaReg(), (int)g.flip, px, hpN);
    } else hpEng.update(hpSink, px, hpN * W, false);
    hpCanvasStale = true; hpCanvasPg = -2;
    hpLastFastMs = millis();
  } else {
    hpNote(why);
    if(hpEng.active) hpLeave(why, 0);
    if(hpCompose(px)){ hpInternal = true; push(); hpInternal = false; }
  }
  if(settled){
    homeScrollY = (float)hpSc[hpCur];
    hpSyncCanvas();                   // canvas = halaman yang tampil (screenshot, frame lambat berikutnya)
    homeBandReady = true;
    if(hpCur == 0 && millis() - hpImgAt[0] > 1500UL){ hpCurDirty = true; needRedraw = true; }   // widget (jam/baterai) segar lagi begitu mendarat
  }
  rcFrameMark();
  return true;
}

// ---------------- hook: push() frame penuh ----------------
bool hpPushHook(){
  if(!hpInternal && !hpMotionNow()){ hpCurDirty = true; hpCanvasPg = (hpOn() && !hpMotionNow() && (int)lroundf(hpX) == hpCur * hpW()) ? hpCur : -2; }
  if(!hpEng.active) return false;
  if(!hwsOnLoop()){ hpForeign = true; return false; }
  const char* why = hpWhyNot();
  if(why){ hpLeave(why, 0); return false; }
  const int px = (int)lroundf(hpX);
  if(hpMotionNow()){ if(!hpCompose(px)) return false; }       // frame lambat di tengah gerakan: canvas disusun ulang dari gambar halaman
  hpEng.slow(hpSink, px, false, false);
  hpCanvasStale = false;
  return true;
}
// pita Home jalur lama (animasi tekan ikon dll.) tidak boleh menulis display langsung selagi ring aktif
bool hpBandRedirect(){
  if(!hpEng.active) return false;
  renderCurrentFrame(); push();
  return true;
}

// ---------------- sentuh ----------------
static const float HP_RUBBER_PX = 80.0f;
static inline float hpMaxX(){ return (float)((hpN - 1) * hpW()); }

void hpTouchHeld(bool isNew, int tx, int ty){
  const unsigned long now = millis();
  if(isNew){
    touchStartX = tx; touchStartY = ty; touchLastY = ty;
    isSwiping = false; swipeStartTime = now;
    const bool caught = hpMotionNow();            // jari menangkap halaman yang masih bergerak
    hpSpringing = false; hpDragging = false; hpVel = 0.0f;
    hpDragRaw = hpX; hpAccum = 0.0f; hpSampleMs = now;
    hpFx = hpFxLast = (float)tx; hpFy = hpFyLast = (float)ty; hpLock = 0;
    hpStartPage = hpNearestPage(hpSc, hpN, homeScrollY);
    if(caught){ hpStartPage = (int)((hpX + (float)hpW() * 0.5f) / (float)hpW()); if(hpStartPage < 0) hpStartPage = 0; if(hpStartPage > hpN - 1) hpStartPage = hpN - 1; }
    homeScrollVel = 0.0f; homeScrollSpringing = false; homeSpringMode = false;
    if(caught){ isSwiping = true; homePressedIdx = -1; hpLock = 1; hpDragging = true; }   // ketuk = berhenti, bukan buka app
    else homePressedIdx = homeJiggle ? -1 : homeIndexAt(tx, ty, homeScrollY);
    return;
  }
  { float dF = (float)tx - hpFx; float aF = 0.45f + fabsf(dF) * 0.08f; if(aF > 1.0f) aF = 1.0f; hpFx += dF * aF; }
  { float dF = (float)ty - hpFy; float aF = 0.45f + fabsf(dF) * 0.08f; if(aF > 1.0f) aF = 1.0f; hpFy += dF * aF; }
  if(!homeJiggle && !isSwiping && homePressedIdx >= 0 && now - swipeStartTime >= 550UL){
    homeJiggle = true; homePressedIdx = -1; vibTap(); needRedraw = true;   // HOME-JIGGLE: tahan ikon = mode atur
  }
  if(!isSwiping){
    const int dx = tx - touchStartX, dy = ty - touchStartY;
    if(abs(dx) > HOME_DRAG_SLOP || abs(dy) > HOME_DRAG_SLOP){
      isSwiping = true; homePressedIdx = -1; homePressAnimIdx = -1; homePressAnim = 0.0f;
      hpLock = (abs(dx) >= abs(dy)) ? 1 : 2;
      hpFxLast = (float)touchStartX; hpFyLast = (float)touchStartY;
      hpDragging = true;
    }
  }
  if(isSwiping && hpLock != 0){
    const float raw = (hpLock == 1) ? (hpFxLast - hpFx)                      // jari ke kiri -> konten maju
                                    : (hpFyLast - hpFy) * HP_VGAIN;          // jari ke atas -> konten maju (kebiasaan lama: tarik ke atas)
    hpDragRaw += raw; hpAccum += raw;
    const float sampleDt = (float)(now - hpSampleMs);
    if(sampleDt >= 12.0f){ hpVel = hpVel * 0.5f + (hpAccum / sampleDt) * 0.5f; hpAccum = 0.0f; hpSampleMs = now; }
    const float nx = homeRubber(hpDragRaw, hpMaxX());
    if((int)lroundf(nx) != (int)lroundf(hpX)){ hpX = nx; hpPending = true; needRedraw = true; }
    else hpX = nx;
  }
  touchLastY = ty;
  hpFxLast = hpFx; hpFyLast = hpFy;
}

// jari dilepas (dipanggil sebagai pengganti logika fling vertikal lama)
void hpRelease(){
  const int W = hpW();
  hpDragging = false;
  int tgt = hpStartPage;
  if(hpLock != 0){
    float v = hpVel; if(v > 2.4f) v = 2.4f; if(v < -2.4f) v = -2.4f;
    hpVel = v;
    tgt = hpPickTarget(hpX, v, hpStartPage, hpN, W);
  } else { hpVel = 0.0f; }
  hpTarget = (float)(tgt * W);
  hpSpringing = (fabsf(hpX - hpTarget) > 0.4f || fabsf(hpVel) > 0.02f);
  if(!hpSpringing){ hpX = hpTarget; if(hpCur != tgt) hpCurDirty = false; hpCur = tgt; homeScrollY = (float)hpSc[tgt]; }
  if(hpLock != 0 || hpSpringing){ hpPending = true; needRedraw = true; }
}

// satu langkah pegas ke hpTarget (berbasis waktu nyata)
void hpStep(float dtMs){
  const float K = HP_SPRING_K, C = HP_SPRING_C;
  const float maxX = hpMaxX();
  float remain = dtMs > 64.0f ? 64.0f : dtMs;
  float y = hpX, v = hpVel;
  while(remain > 0.0f){
    const float h = remain > 8.0f ? 8.0f : remain; remain -= h;
    const float a = -K * (y - hpTarget) - C * v;
    v += a * h; y += v * h;
    if(y < -60.0f){ y = -60.0f; if(v < 0.0f) v = 0.0f; }
    if(y > maxX + 60.0f){ y = maxX + 60.0f; if(v > 0.0f) v = 0.0f; }
    if(fabsf(y - hpTarget) < 0.5f && fabsf(v) < 0.02f){
      y = hpTarget; v = 0.0f; hpSpringing = false;
      hpCur = (int)((hpTarget + 0.5f) / (float)hpW()); hpCurDirty = false;
      homeScrollY = (float)hpSc[hpCur];
      break;
    }
  }
  if((int)lroundf(y) != (int)lroundf(hpX) || !hpSpringing) hpPending = true;
  hpX = y; hpVel = v;
  if(hpPending) needRedraw = true;   // wajib: tanpa ini frame pegas baru tergambar saat ada redraw lain
}

// ---------------- titik halaman ----------------
void hpDrawDots(LGFX_Sprite& s){
  if(!hpWasOn || hpN < 2) return;
  const int cur = hpNearestPage(hpSc, hpN, homeScrollY);
  const int gap = 11, total = (hpN - 1) * gap;
  const int cy = RP_NO_DOCK ? (homeDockY() + 6) : (homeDockY() - 2);   // NO-DOCK: titik di strip bawah, tidak nabrak label baris terakhir
  int x = SCR_W / 2 - total / 2;
  for(int i = 0; i < hpN; i++, x += gap){
    if(i == cur) s.fillCircle(x, cy, 3, T().accent);
    else s.fillCircle(x, cy, 2, T().subtext);
  }
}

// ---------------- pemanasan + penjaga loop ----------------
static unsigned long hpIdleSince = 0;
static void hpPrewarm(){
  const unsigned long now = millis();
  if(needRedraw || hpMotionNow() || hpPending || wasTouched || homePressedIdx >= 0 || homePressAnimIdx >= 0 || toastVisibleNow()
     || hpWhyNot() != nullptr || !homeBandReady){ hpIdleSince = 0; return; }
  if((int32_t)(hpBlockUntil - now) > 0) return;
  if(hpIdleSince == 0){ hpIdleSince = now; return; }
  if(now - hpIdleSince < 250UL) return;                          // Home sudah diam >= 250 ms
  // satu langkah per loop: gambar halaman sekarang, lalu tetangga, lalu masuk mode
  const int cur = hpCur;
  if(!hpImgGet(cur, false) || hpCurDirty){ if(!hpImgGet(cur, true)){ hpBlockUntil = now + 5000UL; Serial.println("[hp] pemanasan gagal (PSRAM kurang?)"); } return; }
  if(cur + 1 < hpN && !hpImgGet(cur + 1, false)){ if(!hpImgGet(cur + 1, true)) hpBlockUntil = now + 5000UL; return; }
  if(cur - 1 >= 0 && !hpImgGet(cur - 1, false)){ if(!hpImgGet(cur - 1, true)) hpBlockUntil = now + 5000UL; return; }
  for(int d = -1; d <= 1; d += 2){                                   // tetangga yang sudah > 30 dtk: segarkan satu per loop
    const int q = cur + d; if(q < 0 || q >= hpN) continue;
    if(now - hpImgAt[q % 3] > 30000UL){ hpImgPg[q % 3] = -1; if(!hpImgGet(q, true)) hpBlockUntil = now + 5000UL; return; }
  }
  if(hpCanvasPg != cur){ hpCompose(cur * hpW()); hpCanvasPg = cur; }  // canvas dikembalikan ke halaman sekarang
  if(!hpEng.active){
    hpGeomFill(); const HwsGeom& g = hpGeomCur;
    hpEng.nFast = hpEng.nFull = hpEng.nSlow = hpEng.nRows = 0;
    hpEng.enter(hpSink, g, cur * hpW(), hpN * hpW(), false);
    Serial.printf("[hp] pemanasan saat diam: halaman siap + mode aktif (flip=%d)\n", (int)g.flip);
  }
  hpIdleSince = 0;
}

// TAP-FIX: jari sudah lepas tapi hpTouchHeld() tak pernah melihat gerakan (swipe sangat cepat / loop sedang sibuk).
// (lx,ly) = posisi terakhir jari menurut driver. Susun ulang seret + kecepatan supaya hpRelease() tetap membalik halaman.
void hpFlickFixup(int lx, int ly){
  if(isSwiping || hpLock != 0) return;
  const int dx = lx - touchStartX, dy = ly - touchStartY;
  if(abs(dx) <= HOME_DRAG_SLOP && abs(dy) <= HOME_DRAG_SLOP){ isSwiping = true; homePressedIdx = -1; return; }   // 9-10 px: bukan ketukan, bukan flick -> abaikan
  hpLock = (abs(dx) >= abs(dy)) ? 1 : 2;
  const float raw = (hpLock == 1) ? -(float)dx : -(float)dy * HP_VGAIN;
  float dt = (float)(millis() - swipeStartTime); if(dt < 16.0f) dt = 16.0f;
  hpDragRaw += raw;
  hpX = homeRubber(hpDragRaw, hpMaxX());
  hpVel = raw / dt;
  hpDragging = true; isSwiping = true; homePressedIdx = -1; homePressAnimIdx = -1; homePressAnim = 0.0f;
}
// HOME-FIX: batas Y (layar) mulai area tersembunyi di halaman widget; INT_MAX kalau tidak berlaku (hit-test grid/chip diblok >= nilai ini)
int hpWidgetCutY(){
  if(!hpWasOn || hpN < 2 || hpCur != 0 || hpMotionNow()) return 0x7fffffff;
  return homeWidgetsEnd() + 4 - hpSc[0];
}
// HOME-FIX: lepas 3 gambar halaman pager (~900 KB PSRAM) -- dipanggil DOOM sebelum cek blok kontigu; dibangun ulang otomatis saat Home dibuka lagi
void hpFreeImages(){ hpImgFreeAll(); }

void hpLoopGuard(){
  if(!hpEng.active && (curScreen() != SCR_HOME || locked || !hpGeomOk()) && (hpImg0.getBuffer() || hpImg1.getBuffer() || hpImg2.getBuffer()))
    hpImgFreeAll();                   // PSRAM kembali utk app lain (Doom/NES), pola sama dgn homeCacheFree()
  if(!hpGeomOk()){ if(hpEng.active) hpLeave("bukan landscape", 0); hpWasOn = false; return; }
  if(hpForeign){ hpForeign = false; hpLeave("push dari core lain", 2); return; }
  const bool on = hpOn();
  if(hpEng.active){
    const char* why = hpWhyNot();
    if(why || !on){ hpLeave(why ? why : "pager mati", 2); return; }
    if(hpCanvasStale && !hpMotionNow() && (uint32_t)(millis() - hpLastFastMs) >= 150UL) hpSyncCanvas();   // diam: canvas sah lagi
    return;
  }
  if(on) hpPrewarm();
}

#else   // ---- build tanpa pager (ILI9341 / -DRP_HOME_PAGER=0 / -DRP_HW_SCROLL=0): semua hook no-op ----
bool hpOn(){ return false; }
bool hpIsActive(){ return false; }
bool hpMotionNow(){ return false; }
bool hpFrame(){ return false; }
bool hpPushHook(){ return false; }
bool hpBandRedirect(){ return false; }
void hpTouchHeld(bool, int, int){}
void hpRelease(){}
void hpStep(float){}
void hpDrawDots(LGFX_Sprite&){}
void hpLoopGuard(){}
int  hpWidgetCutY(){ return 0x7fffffff; }
void hpFlickFixup(int,int){}
void hpFreeImages(){}
void hpLeaveHook(const char*, int){}
int  hpModeGet(){ return 0; }
bool hpModeSet(int){ return false; }
#endif
