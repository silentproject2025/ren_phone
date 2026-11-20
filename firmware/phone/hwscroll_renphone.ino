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
#define RP_HWS_DEFAULT_MODE 1   // 1=Otomatis bawaan (HWS_DEFAULT_ON_V3); 0=Mati; 2=Otomatis+Balik. Pilihan di NVS tetap menang
#endif

#include <stdint.h>
// ---- HWS_PURE_BEGIN ----
struct HwsGeom {
  int D; int B0; int H; bool flip;   // D=jumlah baris memori(480), B0=atas pita, H=tinggi pita
  // Method (bukan fungsi bebas): Arduino builder membuat prototipe otomatis utk fungsi bertipe struct & menaruhnya sebelum struct.
  int tfaReg() const { return flip ? (D - B0 - H) : B0; }
  int bfaReg() const { return flip ? B0 : (D - B0 - H); }
  // VSCSAD utk pergeseran k (0..H-1; konten naik k baris relatif fase). Balik: arah dibalik.
  int sad(int k) const { return flip ? (tfaReg() + (((H - k) % H) + H) % H) : (B0 + k); }
};

static inline int hwsMod(int a, int n){ int r = a % n; return r < 0 ? r + n : r; }

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
  virtual void writeRect(int yw, int n, int srcY, int x0, int w) = 0;  // canvas baris srcY.., kolom [x0,x0+w) -> y=yw (mode generik)
};

struct HwsEngine {
  HwsGeom g;
  int  P, px;               // fase ring & posisi (px) yang sedang tampil
  bool active, dirty, railWas;
  unsigned long nFast, nFull, nSlow, nRows;
  int badFrames; bool wantLeave; int lastDirty;   // mode generik
  HwsEngine() : P(0), px(0), active(false), dirty(false), railWas(false), nFast(0), nFull(0), nSlow(0), nRows(0), badFrames(0), wantLeave(false), lastDirty(0) {
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
    s.scrollDef(g.tfaReg(), g.H, g.bfaReg());
    s.scrollAddr(g.sad(0));
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
    s.scrollAddr(g.sad(k()));
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
    s.scrollAddr(g.sad(k()));
    s.end();
    dirty = overlay;      // overlay ada di memori panel -> frame cepat berikutnya wajib tulis ulang penuh dari cache
    railWas = railOn;     // canvas sudah memuat rail (kalau ada) di kolom kanan
    nSlow++;
  }

  // =================== MODE GENERIK (layar apa pun yang menggulir, tanpa cache) ===================
  // Pemanggil memberi hash per baris layar: Nm/Ns = canvas BARU (kolom utama / strip kanan sw px),
  // Sm/Ss = apa yang sedang tampil di panel (per baris layar). Engine mencari pergeseran vertikal u
  // (konten naik u baris) lewat suara hash, menggeser panel dengan VSCSAD, lalu menulis HANYA baris yang
  // tidak cocok dengan hasil geseran ("perbaikan diff"). Benar walau deteksi salah (cuma jadi lebih mahal).
  static bool genDetect(const uint64_t* Sm, const uint64_t* Nm, int a, int b, int& u, int& r0, int& r1){
    const int H = b - a;
    if(H < 16) return false;
    int dmax = H - 1; if(dmax > 200) dmax = 200;
    int info[480]; int ni = 0;
    for(int j = a; j < b && ni < 480; j++){          // baris "informatif": beda dgn tetangga (bukan area kosong)
      bool up = (j > a) ? (Nm[j] != Nm[j-1]) : true;
      bool dn = (j < b - 1) ? (Nm[j] != Nm[j+1]) : true;
      if(up && dn) info[ni++] = j;
    }
    if(ni < 6) return false;
    int bestv = 0, bestd = 0;
    for(int d = -dmax; d <= dmax; d++){
      if(d == 0) continue;
      int v = 0;
      for(int t = 0; t < ni; t++){ int j = info[t], idx = j - d; if(idx >= a && idx < b && Sm[idx] == Nm[j]) v++; }
      if(v > bestv){ bestv = v; bestd = d; }
    }
    if(bestv < 6) return false;
    int lo = b, hi = a - 1;
    for(int t = 0; t < ni; t++){ int j = info[t], idx = j - bestd; if(idx >= a && idx < b && Sm[idx] == Nm[j]){ if(j < lo) lo = j; if(j > hi) hi = j; } }
    u = -bestd;                                       // konten pindah turun bestd baris == naik u baris
    r0 = lo; r1 = hi + 1;
    if(u > 0) r1 += u; else r0 += u;                  // baris yang baru masuk ikut area scroll
    if(r0 < a) r0 = a;
    if(r1 > b) r1 = b;
    return true;
  }
  static int genDirtyCount(int u, const uint64_t* Sm, const uint64_t* Ss, const uint64_t* Nm, const uint64_t* Ns, int R0, int H){
    int c = 0;
    for(int i = 0; i < H; i++){ int s = i + u; if(!(s >= 0 && s < H && Nm[R0+i] == Sm[R0+s] && Ns[R0+i] == Ss[R0+s])) c++; }
    return c;
  }
  void genRectLines(HwsSink& s, int i0, int i1, int x0, int w, int kk){   // baris layar pita [i0,i1) -> ring (potong di wrap)
    int i = i0;
    while(i < i1){
      int m = (i + kk) % g.H;
      int run = i1 - i; if(run > g.H - m) run = g.H - m;
      s.writeRect(g.B0 + m, run, g.B0 + i, x0, w);
      nRows += (unsigned long)run;
      i += run;
    }
  }
  // Return -1: tidak ada yang ditulis -> pemanggil push penuh biasa lalu salin N ke S.
  //        >=0: frame sudah ditangani engine (jumlah baris pita yang ditulis ulang).
  int gen(HwsSink& s, uint64_t* Sm, uint64_t* Ss, const uint64_t* Nm, const uint64_t* Ns, int D, int W, int sw, bool flip, bool valid){
    wantLeave = false;
    if(!valid){ if(active) leave(s, false); return -1; }
    int u = 0;
    if(!active){
      int cu = 0, r0 = 0, r1 = 0;
      if(!genDetect(Sm, Nm, 0, D, cu, r0, r1)) return -1;
      if(r1 - r0 < 64) return -1;
      if(genDirtyCount(cu, Sm, Ss, Nm, Ns, r0, r1 - r0) * 2 >= (r1 - r0)) return -1;
      g.D = D; g.B0 = r0; g.H = r1 - r0; g.flip = flip;
      P = 0; px = 0; active = true; dirty = false; railWas = false; badFrames = 0;
      s.begin(); s.scrollDef(g.tfaReg(), g.H, g.bfaReg()); s.scrollAddr(g.sad(0)); s.end();   // identitas: tampilan tak berubah
      u = cu;
    } else {
      int cu = 0, r0 = 0, r1 = 0;
      if(genDetect(Sm, Nm, g.B0, g.B0 + g.H, cu, r0, r1)
         && genDirtyCount(cu, Sm, Ss, Nm, Ns, g.B0, g.H) < genDirtyCount(0, Sm, Ss, Nm, Ns, g.B0, g.H)) u = cu;
    }
    const int R0 = g.B0, H = g.H;
    px += u;
    const int kk = k();
    s.begin();
    // 1) baris tetap (status bar/footer): tulis langsung kalau berubah
    for(int y = 0; y < D; ){
      if(y >= R0 && y < R0 + H){ y = R0 + H; continue; }
      if(Nm[y] != Sm[y] || Ns[y] != Ss[y]){
        int y0 = y;
        while(y < D && !(y >= R0 && y < R0 + H) && (Nm[y] != Sm[y] || Ns[y] != Ss[y])) y++;
        s.writeRect(y0, y - y0, y0, 0, W); nRows += (unsigned long)(y - y0);
      } else y++;
    }
    // 2) pita: baris yang tidak cocok dengan hasil geseran
    int dirtyLines = 0;
    for(int i = 0; i < H; ){
      int sidx = i + u;
      bool md = !(sidx >= 0 && sidx < H && Nm[R0+i] == Sm[R0+sidx]);
      if(!md){ i++; continue; }
      int i0 = i;
      while(i < H){
        int si = i + u;
        bool match = (si >= 0 && si < H && Nm[R0+i] == Sm[R0+si]);
        if(match) break;
        i++;
      }
      dirtyLines += i - i0;
      genRectLines(s, i0, i, 0, W, kk);
    }
    for(int i = 0; i < H; ){                           // 3) baris yang cuma strip-nya berbeda (rail dsb.)
      int sidx = i + u;
      bool ok = (sidx >= 0 && sidx < H);
      bool md = !(ok && Nm[R0+i] == Sm[R0+sidx]);
      bool sd = !(ok && Ns[R0+i] == Ss[R0+sidx]);
      if(md || !sd){ i++; continue; }
      int i0 = i;
      while(i < H){
        int si = i + u; bool ok2 = (si >= 0 && si < H);
        if(!(ok2 && Nm[R0+i] == Sm[R0+si]) || (ok2 && Ns[R0+i] == Ss[R0+si])) break;
        i++;
      }
      genRectLines(s, i0, i, W - sw, sw, kk);
    }
    s.scrollAddr(g.sad(kk));
    s.end();
    for(int y = 0; y < D; y++){ Sm[y] = Nm[y]; Ss[y] = Ns[y]; }   // panel sekarang == canvas baru
    lastDirty = dirtyLines;
    if(dirtyLines * 10 > H * 6) badFrames++; else badFrames = 0;
    nFast++;
    if(badFrames >= 4){ wantLeave = true; leave(s, true); }      // banyak berubah = bukan scroll (animasi): kembali ke push biasa
    return dirtyLines;
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
  if(currentOrient == ORIENT_LANDSCAPE) return hpModeGet();   // HOME_PAGER_V1: mode landscape terpisah (NVS "hwl")
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
  if(currentOrient == ORIENT_LANDSCAPE) return hpModeSet(v);   // HOME_PAGER_V1
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
  // LovyanGFX 1.3.0: writeCommand(uint8_t) / writeData(uint8_t) -> satu byte per panggilan (urutan byte tidak ambigu)
  static void w16(int v){ display.writeData((uint8_t)((v >> 8) & 0xFF)); display.writeData((uint8_t)(v & 0xFF)); }
  void begin() override { display.startWrite(); }
  void end() override { display.endWrite(); }
  void scrollDef(int tfa, int vsa, int bfa) override { display.writeCommand(0x33); w16(tfa); w16(vsa); w16(bfa); }
  void scrollAddr(int sad) override { display.writeCommand(0x37); w16(sad); }
  void scrollOff() override { scrollDef(0, SCR_H, 0); scrollAddr(0); display.writeCommand(0x13); }
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
  void writeRect(int yw, int n, int srcY, int x0, int w) override {   // mode generik
    display.setClipRect(x0, yw, w, n);
    canvas.pushSprite(&display, 0, yw - srcY);
    display.clearClipRect();
  }
};
static HwsLgfxSink hwsSink;

// ---------------- MODE GENERIK: layar lain yang menggulir (Pengaturan, Berkas, Chat, Buku, dst.) ----------------
// Tanpa cache & tanpa pengetahuan per-layar: hash per baris canvas -> deteksi geser -> VSCSAD + tulis baris beda.
// Daftar layar sengaja eksplisit: hanya layar yang 100% menggambar lewat canvas+push() (tidak menulis display langsung).
#ifndef RP_HWS_GEN
#define RP_HWS_GEN 1
#endif
static HwsEngine hwsGen;
static uint64_t *hwsSm = nullptr, *hwsSs = nullptr, *hwsNm = nullptr, *hwsNs = nullptr;
static bool hwsGenValid = false;     // hash S == isi panel sebenarnya
static int  hwsGenScreen = -1;

static bool hwsGenScreenOk(){
  switch(curScreen()){
    case SCR_SETTINGS: case SCR_NOTEPAD: case SCR_AICHAT: case SCR_FILEEXPLORER: case SCR_TRIVIA:
    case SCR_APOD: case SCR_NEOWS: case SCR_EPIC: case SCR_NASAIMG: case SCR_HOMEEDIT: case SCR_REMOTE:
    case SCR_CHAT: case SCR_EBOOK: case SCR_BACKUP: case SCR_ABOUT:
      return true;
    case SCR_GALLERY:   // HWS_GAL_V1: hanya album + grid; viewer foto menulis display langsung
      return galHwScrollOk();
    default: return false;
  }
}
static bool hwsGenAlloc(){
  if(hwsSm) return true;
  uint64_t* p = (uint64_t*)heap_caps_malloc(4 * 480 * sizeof(uint64_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!p) return false;
  memset(p, 0, 4 * 480 * sizeof(uint64_t));
  hwsSm = p; hwsSs = p + 480; hwsNm = p + 960; hwsNs = p + 1440;
  return true;
}
static const char* hwsGenWhyNot(){
  if(hwsModeGet() == 0) return "mode Mati";
  if(currentOrient != ORIENT_PORTRAIT || display.getRotation() != 0) return "bukan portrait";
  if(!hwsGenScreenOk()) return "layar tidak didukung";
  if(locked) return "terkunci";
  if(appSwitcherOpen) return "App Switcher";
  if(controlCenterOpen || panelPullActive || ccAnimating) return "Control Center";
  if(notifShadeOpen) return "Notification Shade";
  if(homeAodActive) return "AOD";
  if(diMenuOpen) return "menu Dynamic Island";
  if(powerMenuOpen) return "menu Daya";
  if(callOverlayActive()) return "layar panggilan";
  if(rcApIsUp()) return "Remote Control aktif (butuh canvas)";
  if(SCR_W != RP_DISP_NATIVE_W || SCR_H != RP_DISP_NATIVE_H) return "geometri layar tak cocok";
  if(!canvas.getBuffer() || canvas.width() != SCR_W || canvas.height() != SCR_H) return "canvas tak cocok";
  if(hwsGen.active && hwsGen.g.flip != (hwsModeGet() == 2)) return "mode berubah";
  return nullptr;
}
static void hwsHashCanvas(){
  const uint32_t* b = (const uint32_t*)canvas.getBuffer();
  const int wpr = SCR_W / 2, mw = wpr - HWS_RW / 2;
  for(int y = 0; y < SCR_H; y++){
    const uint32_t* r = b + (size_t)y * wpr;
    uint64_t h = 1469598103934665603ULL;
    for(int x = 0; x < mw; x++){ h ^= r[x]; h *= 1099511628211ULL; }
    hwsNm[y] = h;
    h = 1469598103934665603ULL;
    for(int x = mw; x < wpr; x++){ h ^= r[x]; h *= 1099511628211ULL; }
    hwsNs[y] = h;
  }
}
static void hwsGenLeave(const char* why, int mode){
  if(!hwsGen.active) return;
  const bool canvasOk = canvas.getBuffer() && canvas.width() == SCR_W && canvas.height() == SCR_H
                        && SCR_W == RP_DISP_NATIVE_W && SCR_H == RP_DISP_NATIVE_H;
  const int kk = hwsGen.k();
  hwsGen.leave(hwsSink, mode >= 2 && canvasOk);
  Serial.printf("[hws] keluar (generik, layar %d): %s (k=%d, sesi: frame=%lu baris=%lu)\n", hwsGenScreen, why ? why : "-", kk, hwsGen.nFast, hwsGen.nRows);
  hwsGenValid = false;
#if RP_PUSH_DIFF
  rpShValid = false;
#endif
}
static bool hwsGenPush(){
  if(!hwsOnLoop()){ if(hwsGen.active) hwsForeign = true; return false; }
  const char* why = hwsGenWhyNot();
  if(why){ if(hwsGen.active) hwsGenLeave(why, 0); hwsGenValid = false; return false; }
  if(!hwsGenAlloc()){ hwsGenValid = false; return false; }
  const int scr = (int)curScreen();
  if(scr != hwsGenScreen){ if(hwsGen.active) hwsGenLeave("ganti layar", 0); hwsGenValid = false; hwsGenScreen = scr; }
  hwsHashCanvas();
  const bool was = hwsGen.active;
  const int r = hwsGen.gen(hwsSink, hwsSm, hwsSs, hwsNm, hwsNs, SCR_H, SCR_W, HWS_RW, hwsModeGet() == 2, hwsGenValid);
  if(!was && hwsGen.active){
    hwsGen.nFast = hwsGen.nRows = 0;
    Serial.printf("[hws] masuk (generik, layar %d): B0=%d H=%d TFA=%d BFA=%d flip=%d\n", scr, hwsGen.g.B0, hwsGen.g.H, hwsGen.g.tfaReg(), hwsGen.g.bfaReg(), (int)hwsGen.g.flip);
  }
  if(was && !hwsGen.active){
    Serial.printf("[hws] keluar (generik): %s (sesi: frame=%lu baris=%lu)\n", hwsGen.wantLeave ? "banyak berubah (bukan scroll)" : "hash tak sah", hwsGen.nFast, hwsGen.nRows);
#if RP_PUSH_DIFF
    rpShValid = false;
#endif
  }
  if(r < 0){                              // push penuh biasa menyusul di push(); panel == canvas sesudahnya
    for(int y = 0; y < SCR_H; y++){ hwsSm[y] = hwsNm[y]; hwsSs[y] = hwsNs[y]; }
    hwsGenValid = true;
    return false;
  }
  hwsGenValid = true;
  return true;
}

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
  hpLeaveHook(why, mode);   // HOME_PAGER_V1: semua titik keluar lama otomatis mencakup pager
#if RP_HWS_GEN
  if(hwsGen.active){ if(!hwsOnLoop()) hwsForeign = true; else hwsGenLeave(why, mode); }
  hwsGenValid = false;   // transisi/penulis lain akan menyentuh panel -> hash pembanding tak sah lagi
#endif
  if(!hwsEng.active) return;
  if(!hwsOnLoop()){ hwsForeign = true; return; }   // jangan sentuh display dari core lain
  const bool canvasOk = canvas.getBuffer() && canvas.width() == SCR_W && canvas.height() == SCR_H
                        && SCR_W == RP_DISP_NATIVE_W && SCR_H == RP_DISP_NATIVE_H;
  if(mode >= 1 && canvasOk) hwsSyncCanvas(); else mode = 0;
  const int kk = hwsEng.k();
  bool need = hwsEng.leave(hwsSink, canvasOk && (mode >= 2 || (mode == 1 && kk != 0)));   // HWS_LEAVE_REWRITE_V1
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
                  hwsGeomCur.B0, hwsGeomCur.H, hwsGeomCur.D, hwsGeomCur.tfaReg(), hwsGeomCur.bfaReg(), px);
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
#if RP_HWS_GEN
  if(!hwsEng.active && curScreen() != SCR_HOME) return hwsGenPush();
#endif
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
// Pemanasan saat Home diam: cache dibangun + mode dimasuki SEBELUM scroll pertama, supaya hitch (bangun cache
// = gambar semua ikon, + tulis pita awal) terjadi saat layar tidak bergerak, bukan di tengah gerakan jari.
static unsigned long hwsIdleSince = 0, hwsPrewarmBlock = 0;
static void hwsPrewarm(){
  const unsigned long now = millis();
  if(hwsModeGet() == 0 || needRedraw || !homeBandReady || homeScrollSpringing || homePressedIdx >= 0 || toastVisibleNow() || kbVisible
     || homeSearchBar.open || homeSearchBar.animating || hwsWhyNot() != nullptr || !homeCacheUsable()){ hwsIdleSince = 0; return; }
  if((int32_t)(hwsPrewarmBlock - now) > 0) return;
  if(hwsIdleSince == 0){ hwsIdleSince = now; return; }
  if(now - hwsIdleSince < 250UL) return;                       // Home sudah diam >= 250 ms
  hwsIdleSince = 0;
  if(hwsFrame()) Serial.println("[hws] pemanasan saat diam: cache siap + mode aktif");
  else hwsPrewarmBlock = now + 2000UL;                          // gagal (mis. PSRAM): jangan coba tiap loop
}

void hwsLoopGuard(){
  hwsLoopTask = xTaskGetCurrentTaskHandle();
  if(!homeCacheValid) hwsCacheIcon = false;                      // cache dibangun ulang -> ikon cari perlu ditempel lagi
#if RP_HWS_GEN
  if(hwsGen.active){ const char* gw = hwsGenWhyNot(); if(gw){ hwsGenLeave(gw, 2); return; } }
  else if(hwsGenValid && hwsGenWhyNot()) hwsGenValid = false;
#endif
  if(hwsForeign){ hwsForeign = false; hwsLeave("push dari core lain", 2); return; }
  if(!hwsEng.active){ hwsPrewarm(); return; }
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
