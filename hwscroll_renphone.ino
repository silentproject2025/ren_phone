// =====================================================================
// hwscroll_renphone.ino -- HW_SCROLL_V1 (dipasang oleh apply_hw_scroll.py)
//
// HARDWARE VERTICAL SCROLL ST7796 untuk scroll grid Home, KHUSUS PORTRAIT (rotasi 0).
// Landscape / mode Mati / kondisi tak cocok -> tetap jalur cache HOME_CACHE_V1 (tidak diubah).
//
// Cara kerja (ringkas):
//  * Pita grid (TFA=homeGridClipTop(), VSA=tinggi pita, BFA=sisanya) dijadikan "area scroll" ST7796
//    lewat VSCRDEF (0x33). Memori panel untuk pita bertindak sbg RING BUFFER.
//  * Baris konten c (baris sprite homeCache) SELALU tinggal di baris memori  m=(c-B0-P) mod H
//    (B0=atas pita, H=tinggi pita, P=fase tetap per sesi). Scroll = geser alamat awal VSCSAD (0x37)
//    + tulis HANYA baris yang baru muncul (beberapa KB), bukan 300 KB per frame.
//  * Penulisan memakai API LovyanGFX pada koordinat layar "tanpa-scroll" (y = B0+m), jadi pemetaan
//    MADCTL ditangani LovyanGFX. Cuma perintah scroll yang dikirim mentah (writeCommand/writeData).
//  * Mode runtime (NVS "ui"/"hws"): 0=Mati, 1=Otomatis, 2=Otomatis+Balik (arah scroll dibalik + TFA/BFA
//    ditukar, utk panel yang rotasinya membalik MADCTL MY/ML). Diatur di Pengaturan > KINERJA.
//  * Overlay di dalam pita: rail scroll digambar ulang tiap frame (strip 8 px, tidak ikut bergulir);
//    ikon cari (tertutup) ditempel ke cache; kolom cari terbuka/toast/keyboard/animasi ikon ->
//    frame digambar jalur lama lalu dikirim lewat pemetaan scroll yg sama (tanpa keluar mode).
//  * Keluar mode (Control Center, kunci, app lain, landscape, wallpaper, dll): VSCRDEF(0,480,0),
//    VSCSAD=0, NORON(0x13), lalu pita ditulis ulang penuh dari canvas yg sudah disinkronkan.
//  * Semua akses display HANYA dari loop() (core 1).
//
// Bagian antara HWS_PURE_BEGIN / HWS_PURE_END tidak bergantung Arduino/LovyanGFX dan diuji di PC
// (tests_hwscroll/). -DRP_HW_SCROLL=0 mematikan semuanya (fungsi hook jadi no-op).
// =====================================================================
#ifndef RP_HW_SCROLL
#define RP_HW_SCROLL 1
#endif
#ifndef RP_HWS_DEFAULT_MODE
#define RP_HWS_DEFAULT_MODE 0   // 0=Mati bawaan (belum teruji di hardware); ubah ke 1 kalau sudah yakin
#endif

// ---- HWS_PURE_BEGIN ----
struct HwsGeom { int D; int B0; int H; bool flip; };   // D=jumlah baris memori(480), B0=atas pita, H=tinggi pita

static inline int hwsMod(int a, int n){ int r = a % n; return r < 0 ? r + n : r; }
static inline int hwsTfaReg(const HwsGeom& g){ return g.flip ? (g.D - g.B0 - g.H) : g.B0; }
static inline int hwsBfaReg(const HwsGeom& g){ return g.flip ? g.B0 : (g.D - g.B0 - g.H); }
// VSCSAD utk pergeseran k (0..H-1; konten naik k baris relatif fase). Balik: arah dibalik.
static inline int hwsSad(const HwsGeom& g, int k){
  return g.flip ? (hwsTfaReg(g) + hwsMod(g.H - k, g.H)) : (g.B0 + k);
}

// Tujuan keluaran engine (di firmware = LovyanGFX, di tes = simulator panel).
struct HwsSink {
  virtual void begin() = 0;                                  // startWrite
  virtual void end() = 0;                                    // endWrite
  virtual void scrollDef(int tfa, int vsa, int bfa) = 0;     // 0x33
  virtual void scrollAddr(int sad) = 0;                      // 0x37
  virtual void scrollOff() = 0;                              // (0,D,0) + VSCSAD 0 + NORON
  virtual void writeCache(int yw, int n, int c0) = 0;        // baris cache [c0,c0+n) -> layar-tanpa-scroll y=yw
  virtual void writeBg(int yw, int n) = 0;                   // isi warna latar (di luar konten / rubber-band)
  virtual void writeCanvas(int yw, int n, int line0) = 0;    // canvas pita baris line0.. -> y=yw
  virtual void stripBegin(int px, bool rail) = 0;            // susun strip rail utk posisi px
  virtual void writeStrip(int yw, int n, int line0) = 0;     // strip baris line0.. -> y=yw (kolom kanan)
  virtual void stripEnd() = 0;
  virtual void writePlain(int y0, int n) = 0;                // canvas baris [y0,y0+n) apa adanya (keluar mode)
};

struct HwsEngine {
  HwsGeom g;
  int  P, px;               // fase ring & posisi (px) yang sedang tampil
  bool active, dirty, railWas;
  unsigned long nFast, nFull, nSlow, nRows;
  HwsEngine() : P(0), px(0), active(false), dirty(false), railWas(false), nFast(0), nFull(0), nSlow(0), nRows(0) {
    g.D = 0; g.B0 = 0; g.H = 0; g.flip = false;
  }
  int  k() const { return hwsMod(px - P, g.H); }

  // tulis baris konten [c0,c1) ke ring; potong di batas wrap ring dan batas isi cache [0,cacheH)
  void rows(HwsSink& s, int c0, int c1, int cacheH){
    int c = c0;
    while(c < c1){
      int m = hwsMod(c - g.B0 - P, g.H);
      int run = c1 - c;
      if(run > g.H - m) run = g.H - m;
      bool ok = (c >= 0 && c < cacheH);
      if(c < 0){ if(run > -c) run = -c; }
      else if(c < cacheH){ if(run > cacheH - c) run = cacheH - c; }
      if(ok) s.writeCache(g.B0 + m, run, c); else s.writeBg(g.B0 + m, run);
      nRows += (unsigned long)run;
      c += run;
    }
  }
  // tulis baris layar pita [0,H) (canvas / strip) ke ring pada pergeseran kk
  void lines(HwsSink& s, int kk, bool strip){
    int i = 0;
    while(i < g.H){
      int m = (i + kk) % g.H;
      int run = g.H - i;
      if(run > g.H - m) run = g.H - m;
      if(strip) s.writeStrip(g.B0 + m, run, i); else s.writeCanvas(g.B0 + m, run, i);
      i += run;
    }
  }
  void stripPass(HwsSink& s, bool railOn){
    if(railOn || railWas){ s.stripBegin(px, railOn); lines(s, k(), true); s.stripEnd(); }
    railWas = railOn;
  }
  // Masuk mode dari layar yang menampilkan pita apa adanya: fase P=px -> k=0 -> tulis pita seperti push biasa.
  void enter(HwsSink& s, const HwsGeom& geom, int pxNow, int cacheH, bool railOn){
    g = geom; P = pxNow; px = pxNow; active = true; dirty = false; railWas = false;
    s.begin();
    s.scrollDef(hwsTfaReg(g), g.H, hwsBfaReg(g));
    s.scrollAddr(hwsSad(g, 0));
    rows(s, px + g.B0, px + g.B0 + g.H, cacheH);
    stripPass(s, railOn);
    s.end();
    nFull++;
  }
  // Frame cepat: sumber = cache. Hanya baris baru (atau seluruh pita kalau kotor / lompat >= H).
  void update(HwsSink& s, int pxNew, int cacheH, bool railOn){
    if(!active) return;
    int dpx = pxNew - px;
    if(!dirty && dpx == 0 && railOn == railWas) return;
    int a = 0, b = 0; bool full = dirty;
    if(full || dpx >= g.H || -dpx >= g.H){ a = pxNew + g.B0; b = a + g.H; full = true; }
    else if(dpx > 0){ a = px + g.B0 + g.H; b = pxNew + g.B0 + g.H; }
    else if(dpx < 0){ a = pxNew + g.B0; b = px + g.B0; }
    s.begin();
    if(b > a) rows(s, a, b, cacheH);
    px = pxNew;
    stripPass(s, railOn);
    s.scrollAddr(hwsSad(g, k()));
    dirty = false;
    s.end();
    if(full) nFull++; else nFast++;
  }
  // Frame lambat: pita dari canvas (overlay/animasi/frame penuh), dikirim lewat pemetaan scroll yg sama.
  void slow(HwsSink& s, int pxNew, bool overlay, bool railOn){
    if(!active) return;
    px = pxNew;
    s.begin();
    lines(s, k(), false);
    s.scrollAddr(hwsSad(g, k()));
    s.end();
    dirty = overlay;      // overlay ada di memori panel -> frame cepat berikutnya wajib tulis ulang penuh dari cache
    railWas = railOn;     // canvas sudah memuat rail (kalau ada) di kolom kanan
    nSlow++;
  }
  // Keluar: scroll mati. Return true kalau memori ring TIDAK lurus (k!=0) -> pita harus ditulis ulang.
  bool leave(HwsSink& s, bool rewrite){
    if(!active) return false;
    bool need = (k() != 0);
    s.begin();
    s.scrollOff();
    if(rewrite && need) s.writePlain(g.B0, g.H);
    s.end();
    active = false;
    return need;
  }
};
// ---- HWS_PURE_END ----

#if RP_HW_SCROLL && defined(RP_DISPLAY_ST7796)

#define HWS_RW 8   // lebar strip rail (px) di tepi kanan

static HwsEngine hwsEng;
static HwsGeom   hwsGeomCur;                // diisi hwsGeomNow()
static int       hwsModeV = -1;             // -1 = belum dimuat dari NVS
static bool      hwsCanvasStale = false;    // canvas pita BELUM mencerminkan layar (frame cepat tdk menggambar ke canvas)
static bool      hwsCacheIcon = false;      // ikon cari sudah ditempel ke homeCache
static bool      hwsForeign = false;        // push() dipanggil dari core lain saat mode aktif
static float     hwsRailSc = 0.0f;          // homeScrollY frame terakhir (utk rail)
static unsigned long hwsLastFastMs = 0;
static const char* hwsLastWhy = nullptr;
static TaskHandle_t hwsLoopTask = nullptr;
static LGFX_Sprite hwsStrip(&display);
static int       hwsStripH = 0;

static inline bool hwsOnLoop(){ return hwsLoopTask != nullptr && xTaskGetCurrentTaskHandle() == hwsLoopTask; }

// ---------------- mode (NVS) ----------------
const char* hwsModeText(int m){ return m == 1 ? "Otomatis" : (m == 2 ? "Otomatis+Balik" : "Mati"); }
int hwsModeGet(){
  if(hwsModeV < 0){
    Preferences p; p.begin("ui", true);
    int v = p.getInt("hws", RP_HWS_DEFAULT_MODE);
    p.end();
    if(v < 0 || v > 2) v = RP_HWS_DEFAULT_MODE;
    hwsModeV = v;
    Serial.printf("[hws] mode NVS = %d (%s)\n", v, hwsModeText(v));
  }
  return hwsModeV;
}
bool hwsModeSet(int v){
  if(v < 0 || v > 2) return false;
  Preferences p;
  if(!p.begin("ui", false)){ Serial.println("[hws] NVS begin(ui) GAGAL"); return false; }
  size_t w = p.putInt("hws", v);
  int back = p.getInt("hws", -1);
  p.end();
  bool ok = (w > 0 && back == v);
  Serial.printf("[hws] simpan mode %d (%s): putInt=%u baca-ulang=%d -> %s\n", v, hwsModeText(v), (unsigned)w, back, ok ? "OK" : "GAGAL");
  if(ok) hwsModeV = v;
  return ok;   // mode baru dipakai langsung: hwsLoopGuard() melihat geometri/mode berubah & keluar bila perlu
}

// ---------------- geometri & syarat ----------------
static bool hwsGeomNow(){
  hwsGeomCur.D = SCR_H;
  hwsGeomCur.B0 = homeGridClipTop();
  hwsGeomCur.H = (homeDockY() - 4) - hwsGeomCur.B0;
  hwsGeomCur.flip = (hwsModeGet() == 2);
  return SCR_W == RP_DISP_NATIVE_W && SCR_H == RP_DISP_NATIVE_H && hwsGeomCur.H >= 64 && hwsGeomCur.B0 >= 0
      && hwsGeomCur.B0 + hwsGeomCur.H <= hwsGeomCur.D;
}
static bool hwsRailWanted(){
  const int clipTop = homeGridClipTop(), contentBot = homeDockY() - 4;
  return homeMaxScroll() > 0 && (contentBot - 2 - (clipTop + 6)) > 20;
}
// nullptr = boleh. Selain itu = alasan keluar / tidak masuk mode (juga dicetak ke Serial).
static const char* hwsWhyNot(){
  if(hwsModeGet() == 0) return "mode Mati";
  if(currentOrient != ORIENT_PORTRAIT || display.getRotation() != 0) return "bukan portrait";
  if(curScreen() != SCR_HOME) return "pindah ke layar lain";
  if(locked) return "terkunci";
  if(appSwitcherOpen) return "App Switcher";
  if(controlCenterOpen || panelPullActive || ccAnimating) return "Control Center";
  if(notifShadeOpen) return "Notification Shade";
  if(homeAodActive) return "AOD";
  if(diMenuOpen) return "menu Dynamic Island";
  if(powerMenuOpen) return "menu Daya";
  if(callOverlayActive()) return "layar panggilan";
  if(wallpaperReady) return "wallpaper aktif";
  if(gameModeActive) return "mode game";
  if(rcApIsUp()) return "Remote Control aktif (butuh canvas)";
  if(!hwsGeomNow()) return "geometri layar tak cocok";
  if(hwsEng.active && (hwsGeomCur.B0 != hwsEng.g.B0 || hwsGeomCur.H != hwsEng.g.H || hwsGeomCur.D != hwsEng.g.D || hwsGeomCur.flip != hwsEng.g.flip))
    return "geometri/mode berubah";
  return nullptr;
}
static void hwsNote(const char* why){   // log alasan fallback hanya saat berubah
  if(why != hwsLastWhy){ hwsLastWhy = why; if(why) Serial.printf("[hws] fallback jalur cache: %s\n", why); }
}
static bool hwsOverlayNow(){
  return !homeCacheUsable() || toastVisibleNow() || kbVisible || homeSearchBar.open || homeSearchBar.animating || homeSearchActive();
}

// ---------------- rail + strip ----------------
static void hwsDrawRail(LGFX_Sprite& s, int xo, int yo, float sc){
  const int clipTop = homeGridClipTop(), contentBot = homeDockY() - 4;
  const int maxSc = homeMaxScroll();
  if(maxSc <= 0) return;
  const int railX = SCR_W - 5, railTop = clipTop + 6, railH = contentBot - 2 - railTop;
  if(railH <= 20) return;
  nyxAaRR(s, railX - xo, railTop - yo, 3, railH, 1, blend565(T().surface, 0xFFFF, 50));
  int thumbH = max(16, (int)(railH * railH / (float)(railH + maxSc)));
  float fr = sc / (float)maxSc; if(fr < 0.0f) fr = 0.0f; if(fr > 1.0f) fr = 1.0f;
  int thumbY = railTop + (int)((railH - thumbH) * fr);
  nyxAaRR(s, railX - xo, thumbY - yo, 3, thumbH, 1, T().accent);
}
static bool hwsStripEnsure(int h){
  if(hwsStrip.getBuffer() && hwsStripH == h) return true;
  if(hwsStrip.getBuffer()) hwsStrip.deleteSprite();
  hwsStripH = 0;
  hwsStrip.setColorDepth(16); hwsStrip.setPsram(true);
  if(!hwsStrip.createSprite(HWS_RW, h)) return false;
  hwsStripH = h;
  return true;
}
static void hwsStripFree(){ if(hwsStrip.getBuffer()) hwsStrip.deleteSprite(); hwsStripH = 0; }

// ---------------- sink LovyanGFX ----------------
struct HwsLgfxSink : HwsSink {
  static void w16(int v){ display.writeData((uint32_t)((v >> 8) & 0xFF), 8); display.writeData((uint32_t)(v & 0xFF), 8); }
  void begin() override { display.startWrite(); }
  void end() override { display.endWrite(); }
  void scrollDef(int tfa, int vsa, int bfa) override { display.writeCommand(0x33, 8); w16(tfa); w16(vsa); w16(bfa); }
  void scrollAddr(int sad) override { display.writeCommand(0x37, 8); w16(sad); }
  void scrollOff() override { scrollDef(0, SCR_H, 0); scrollAddr(0); display.writeCommand(0x13, 8); }
  void writeCache(int yw, int n, int c0) override {
    display.setClipRect(0, yw, SCR_W, n);
    homeCache.pushSprite(&display, 0, yw - c0);
    display.clearClipRect();
  }
  void writeBg(int yw, int n) override { display.fillRect(0, yw, SCR_W, n, T().bg); }
  void writeCanvas(int yw, int n, int line0) override {
    display.setClipRect(0, yw, SCR_W, n);
    canvas.pushSprite(&display, 0, yw - (hwsEng.g.B0 + line0));
    display.clearClipRect();
  }
  void stripBegin(int px, bool rail) override {
    const int B0 = hwsEng.g.B0, x0 = SCR_W - HWS_RW;
    hwsStrip.fillSprite(T().bg);
    homeCache.pushSprite(&hwsStrip, -x0, -(px + B0));          // kolom kanan cache, baris konten px+B0+i -> strip baris i
    if(rail) hwsDrawRail(hwsStrip, x0, B0, hwsRailSc);
  }
  void writeStrip(int yw, int n, int line0) override {
    const int x0 = SCR_W - HWS_RW;
    display.setClipRect(x0, yw, HWS_RW, n);
    hwsStrip.pushSprite(&display, x0, yw - line0);
    display.clearClipRect();
  }
  void stripEnd() override {}
  void writePlain(int y0, int n) override {
    display.setClipRect(0, y0, SCR_W, n);
    canvas.pushSprite(&display, 0, 0);
    display.clearClipRect();
  }
};
static HwsLgfxSink hwsSink;

// ---------------- sinkronisasi canvas ----------------
// Frame cepat tidak menggambar ke canvas -> canvas pita basi. Disalin dari cache bila ada yg butuh (keluar mode,
// transisi, screenshot, diam >150 ms).
static void hwsSyncCanvas(){
  if(!hwsCanvasStale) return;
  hwsCanvasStale = false;
  if(!homeCache.getBuffer() || !homeCacheValid || !canvas.getBuffer()) return;
  const int bt = hwsEng.g.B0, bh = hwsEng.g.H, px = hwsEng.px;
  canvas.setClipRect(0, bt, SCR_W, bh);
  if(px < 0 || px + bt + bh > homeCacheH) canvas.fillRect(0, bt, SCR_W, bh, T().bg);
  homeCache.pushSprite(&canvas, 0, -px);
  hwsDrawRail(canvas, 0, 0, hwsRailSc);
  canvas.clearClipRect();
}

// mode: 0 = tidak ada tulis ulang (pemanggil menulis pita/seluruh layar sesudahnya)
//       1 = sinkron canvas saja (transisi akan menulis frame penuh)
//       2 = sinkron canvas + tulis ulang pita sekarang
void hwsLeave(const char* why, int mode){
  if(!hwsEng.active) return;
  if(!hwsOnLoop()){ hwsForeign = true; return; }   // jangan sentuh display dari core lain
  const bool canvasOk = canvas.getBuffer() && canvas.width() == SCR_W && canvas.height() == SCR_H
                        && SCR_W == RP_DISP_NATIVE_W && SCR_H == RP_DISP_NATIVE_H;
  if(mode >= 1 && canvasOk) hwsSyncCanvas(); else mode = 0;
  const int kk = hwsEng.k();
  bool need = hwsEng.leave(hwsSink, mode >= 2 && canvasOk);
  Serial.printf("[hws] keluar: %s (k=%d, mode-tulis=%d, sesi: cepat=%lu penuh=%lu lambat=%lu baris=%lu)\n",
                why ? why : "-", kk, mode, hwsEng.nFast, hwsEng.nFull, hwsEng.nSlow, hwsEng.nRows);
  (void)need;
  hwsCanvasStale = false;
  hwsCacheIcon = false;
  hwsStripFree();
#if RP_PUSH_DIFF
  rpShValid = false;   // panel berubah di luar push() -> salinan diff dianggap basi
#endif
  hwsLastWhy = why;
}

bool hwsIsActive(){ return hwsEng.active; }

// ---------------- hook: renderHomeBandFrame (jalur cepat) ----------------
bool hwsFrame(){
  if(!hwsOnLoop()) return false;
  const char* why = hwsWhyNot();
  if(why){ if(hwsEng.active) hwsLeave(why, 0); else hwsNote(why); return false; }
  hwsNote(nullptr);
  if(!homeCacheUsable()) return false;                          // overlay sementara -> jalur lama; hwsBandPush() mengirimnya
  if(homeSearchBar.open || homeSearchBar.animating) return false;
  if(!homeCacheValid){
    homeCacheValid = homeCacheBuild();
    hwsCacheIcon = false;
    if(!homeCacheValid){ if(hwsEng.active) hwsLeave("cache gagal (PSRAM kurang?)", 0); else hwsNote("cache gagal (PSRAM kurang?)"); return false; }
  }
  if(!hwsCacheIcon){   // ikon cari (tertutup) ikut bergulir bersama konten -> tempel ke cache
    drawSearchBar(homeCache, homeSearchBar, homeSearchRightX(), STATUS_H + 2, homeSearchInput, "Cari app...");
    hwsCacheIcon = true;
  }
  const int px = homeScrollPx(homeScrollY);
  const bool rail = hwsRailWanted();
  hwsRailSc = homeScrollY;
  if(!hwsEng.active){
    if(!hwsStripEnsure(hwsGeomCur.H)){ hwsNote("alokasi strip rail gagal"); return false; }
    hwsEng.nFast = hwsEng.nFull = hwsEng.nSlow = hwsEng.nRows = 0;
    hwsEng.enter(hwsSink, hwsGeomCur, px, homeCacheH, rail);
    Serial.printf("[hws] masuk: mode=%d(%s) B0=%d H=%d D=%d TFA=%d BFA=%d px=%d\n", hwsModeGet(), hwsModeText(hwsModeGet()),
                  hwsGeomCur.B0, hwsGeomCur.H, hwsGeomCur.D, hwsTfaReg(hwsGeomCur), hwsBfaReg(hwsGeomCur), px);
  } else {
    hwsEng.update(hwsSink, px, homeCacheH, rail);
  }
  hwsCanvasStale = true;
  hwsLastFastMs = millis();
  rcFrameMark();
  return true;
}

// ---------------- hook: pita jalur lama (overlay sementara) ----------------
bool hwsBandPush(){
  if(!hwsEng.active || !hwsOnLoop()) return false;
  const char* why = hwsWhyNot();
  if(why){ hwsLeave(why, 0); return false; }
  hwsRailSc = homeScrollY;
  hwsEng.slow(hwsSink, homeScrollPx(homeScrollY), hwsOverlayNow(), hwsRailWanted());
  hwsCanvasStale = false;
  return true;
}

// ---------------- hook: push() frame penuh ----------------
bool hwsPushHook(){
  if(!hwsEng.active) return false;
  if(!hwsOnLoop()){ hwsForeign = true; return false; }
  const char* why = hwsWhyNot();
  if(why){ hwsLeave(why, 0); return false; }                    // canvas = frame baru; push biasa menulis ulang semuanya
  const HwsGeom& g = hwsEng.g;
  display.startWrite();
  display.setClipRect(0, 0, SCR_W, g.B0);                        canvas.pushSprite(&display, 0, 0);   // status bar (TFA, tetap)
  display.setClipRect(0, g.B0 + g.H, SCR_W, g.D - g.B0 - g.H);   canvas.pushSprite(&display, 0, 0);   // dock (BFA, tetap)
  display.clearClipRect();
  display.endWrite();
  hwsRailSc = homeScrollY;
  hwsEng.slow(hwsSink, homeScrollPx(homeScrollY), hwsOverlayNow(), hwsRailWanted());
  hwsCanvasStale = false;
  return true;
}

// ---------------- hook: awal tiap loop() ----------------
void hwsLoopGuard(){
  hwsLoopTask = xTaskGetCurrentTaskHandle();
  if(!homeCacheValid) hwsCacheIcon = false;                      // cache dibangun ulang -> ikon cari perlu ditempel lagi
  if(!hwsEng.active) return;
  if(hwsForeign){ hwsForeign = false; hwsLeave("push dari core lain", 2); return; }
  const char* why = hwsWhyNot();
  if(why){ hwsLeave(why, 2); return; }
  if(hwsCanvasStale && (uint32_t)(millis() - hwsLastFastMs) >= 150UL) hwsSyncCanvas();   // diam: canvas dibuat sah lagi (screenshot dll)
}

#else   // ---- build tanpa HW scroll (ILI9341 / -DRP_HW_SCROLL=0): semua hook no-op ----
bool hwsFrame(){ return false; }
bool hwsIsActive(){ return false; }
bool hwsBandPush(){ return false; }
bool hwsPushHook(){ return false; }
void hwsLoopGuard(){}
void hwsLeave(const char*, int){}
int  hwsModeGet(){ return 0; }
bool hwsModeSet(int){ return false; }
const char* hwsModeText(int){ return "Mati"; }
#endif
