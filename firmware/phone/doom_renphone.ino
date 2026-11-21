// =================================================================
// APP: DOOM -- port doomgeneric via library ESP-DOOM (Rear Labs).
// File tab TERPISAH, persis pola nes_*_renphone.ino: implementasi
// display/input DOOM ada di sini, phone.ino cuma tahu 5 fungsi publik
// (doomEnter/doomExit/drawDoom/doomTouch/doomIsPlaying) lewat forward
// declaration di dekat AppDef apps[].
//
// RINGKASAN CARA KERJA (baca ini dulu kalau mau ubah sesuatu):
// 1. DOOM (lib ESP-DOOM/doomgeneric) minta kontrak IDoomDisplay &
//    IDoomInput. Di sini keduanya diimplementasikan sbg wrapper tipis
//    di atas infrastruktur yg SUDAH ADA di ren_phone -- BUKAN reinvent:
//      - RenPhoneDoomDisplay bungkus objek global `display` (LGFX) yg
//        udah di-init di setup() phone.ino. Tidak bikin bus SPI baru.
//      - RenPhoneDoomInput itu 8 tombol digital yg diisi dari SATU
//        titik sentuh XPT2046 lewat doomTouch() -- kontrolnya kombinasi
//        d-pad (kiri, maju/mundur) + drag-turn (kanan, noleh),
//        + 4 tombol bulat tetap (Pause/OK/Use/Fire), gaya PERSIS sama
//        dgn skema 2-zona game "Inferno" yg sudah ada duluan.
// 2. Sama seperti NES (Nofrendo): render DOOM (35 tic/detik, banyak
//    baris SPI per frame) TERLALU BERAT utk lewat pipeline canvas biasa
//    (draw ke sprite PSRAM lalu pushSprite tiap loop() UI) -- jadi
//    dijalankan di TASK FreeRTOS TERPISAH (core 1) yg pegang SPI
//    display LANGSUNG, selagi core 0 (loop() utama di phone.ino) SAMA
//    SEKALI tidak boleh nyentuh `display`/`canvas.pushSprite()` (lihat
//    guard doomIsPlaying() yg ditambahkan di loop() phone.ino, pola
//    sama persis dgn nesIsPlaying()).
// 3. DOOM dipaksa ORIENTASI LANDSCAPE selagi app ini aktif (doomgeneric
//    cuma bisa di-Create() SEKALI per sesi & ukuran panelnya fixed
//    compile-time lewat hw_conf.h -- lihat catatan panjang di file itu),
//    lalu dikembalikan ke orientasi semula pas keluar.
// 4. NON-AUDIO (permintaan user) -- DOOM_NO_AUDIO di hw_conf.h, DG_PCM
//    dibiarkan nullptr (default dari DoomSound.cpp bawaan library),
//    tidak ada satupun include DoomSound.h di sini.
// =================================================================

#include <DoomGlue.h>
#include "hw_conf.h"
extern "C" {
#include <doomgeneric_fs.h>
}

// ---- Kontrak minimal dgn library (lihat IDoomDisplay.h/IDoomInput.h) ----
// DoomButton:: Up/Down/Left/Right/A/B/Start/Select (dipetakan oleh
// DoomGlue.cpp ke KEY_UPARROW/DOWNARROW/LEFT/RIGHTARROW/FIRE/USE/ENTER/ESCAPE)
enum { DBTN_UP=0, DBTN_DOWN, DBTN_LEFT, DBTN_RIGHT, DBTN_FIRE, DBTN_USE, DBTN_START, DBTN_SELECT, DBTN_COUNT };

// -----------------------------------------------------------------
// DISPLAY -- bungkus `display` (LGFX) yg SUDAH di-init di phone.ino.
// beginFrame()/endFrame() membuka SATU transaksi SPI utk SELURUH frame
// (bukan per baris) -- writeRow() di antaranya cuma setAddrWindow+
// writePixels (LovyanGFX otomatis lanjutin dari window yg masih
// terbuka), jauh lebih hemat overhead drpd buka/tutup transaksi 200x
// per frame (200 baris konten, lihat DOOM_VIEWPORT_* di hw_conf.h).
// -----------------------------------------------------------------
class RenPhoneDoomDisplay : public IDoomDisplay {
public:
  bool begin() override { return true; } // display sudah di-init phone.ino setup()
  // v-boost: transaksi SPI dibuka SEKALI & dibiarkan terbuka selama sesi DOOM (ditutup di close()),
  // supaya DMA pita terakhir tetap jalan SELAGI core ini merender frame berikutnya.
  void beginFrame() override { if(!txOpen){ display.startWrite(); txOpen=true; } }
  void writeRow(int x, int y, const uint16_t* rgb565, int count) override {
    display.setAddrWindow(x, y, count, 1);
    display.writePixels((const lgfx::rgb565_t*)rgb565, count);
  }
  bool supportsBand() const override { return true; }
  void writeBand(int x, int y, int w, int h, const uint16_t* wire565) override {
    display.waitDMA();                                            // pita sebelumnya harus selesai dulu
    display.pushImageDMA(x, y, w, h, (const lgfx::swap565_t*)wire565); // data sudah urutan kabel -> tanpa konversi
  }
  void flushDMA() override { display.waitDMA(); }
  void endFrame() override { /* sengaja kosong: transaksi tetap terbuka (lihat beginFrame) */ }
  void close() { if(txOpen){ display.waitDMA(); display.endWrite(); txOpen=false; } }
  // AETHER_SD_FIX_V1: SD lewat SPI berbagi bus dgn layar -> transaksi layar HARUS dilepas selama baca SD
  void busRelease() { if(txOpen){ display.waitDMA(); display.endWrite(); } }
  void busAcquire() { if(txOpen){ display.startWrite(); } }
  int width()  const override { return DOOM_PANEL_W; }
  int height() const override { return DOOM_PANEL_H; }
private:
  bool txOpen = false;
};

// -----------------------------------------------------------------
// INPUT -- 8 tombol digital. `raw[]` diisi dari doomTouch() (dipanggil
// dari core 0 / loop UI), dibaca+diambil snapshot di update() (dipanggil
// dari task DOOM sendiri di core 1). Sama kayak bool biasa yg dipakai
// bendera lintas-task di tempat lain di file ini (mis. gameModeActive) --
// cukup aman utk 8 bool independen spt ini, tidak butuh mutex.
// -----------------------------------------------------------------
extern volatile uint8_t rcPadMask; // gamepad_renphone.ino: bit i = DBTN_i (UP,DOWN,LEFT,RIGHT,FIRE,USE,START,SELECT)
// Hook gamepad web lengkap (gamepad_renphone.ino) -> didaftarkan ke DoomGlue di doomTaskFunc().
uint32_t padExtKeys();      // 31 tombol EXTRA (strafe, run, senjata, F-keys, ...)
int32_t  padTakeMouseDX();  // geser-lihat (delta sejak panggilan lalu)
int      padTakeTyped();    // 1 karakter ketikan (0 = kosong)
void DoomGlue_SetHooks(uint32_t (*ext)(void), int32_t (*mouse)(void), int (*typed)(void)); // esp_doom_core/DoomGlue.cpp
void DoomGlue_SetBandHook(void (*fn)(uint16_t*, int, int, int, int)); // esp_doom_core/DoomGlue.cpp
bool DoomGlue_BandPathActive();
void doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();
uint32_t doomExtKeys();   // DOOM_LAYOUT_V2
extern volatile uint32_t DG_FrameCount, DG_DrawUsAccum; bool DG_VideoBufInternal(); bool DG_ColormapInternal();
void doomBandHook(uint16_t* buf, int px, int py, int w, int h);
class RenPhoneDoomInput : public IDoomInput {
public:
  volatile bool raw[DBTN_COUNT] = {false};
  volatile uint32_t latchUntil[DBTN_COUNT] = {0};   // DOOM_FIRE_LATCH_V1: tombol aksi tetap "ditekan" sampai waktu ini (millis)
  void begin() override { for(int i=0;i<DBTN_COUNT;i++){ raw[i]=false; cur[i]=false; prev[i]=false; latchUntil[i]=0; } }
  void update() override { uint8_t pad=rcPadMask; uint32_t now=millis(); for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i] || ((pad>>i)&1) || ((int32_t)(latchUntil[i]-now)>0); } }
  bool down(DoomButton b)     override { return cur[idx(b)]; }
  bool pressed(DoomButton b)  override { int i=idx(b); return cur[i] && !prev[i]; }
  bool released(DoomButton b) override { int i=idx(b); return !cur[i] && prev[i]; }
private:
  bool cur[DBTN_COUNT]={false}, prev[DBTN_COUNT]={false};
  static int idx(DoomButton b){
    switch(b){
      case DoomButton::Up: return DBTN_UP; case DoomButton::Down: return DBTN_DOWN;
      case DoomButton::Left: return DBTN_LEFT; case DoomButton::Right: return DBTN_RIGHT;
      case DoomButton::A: return DBTN_FIRE; case DoomButton::B: return DBTN_USE;
      case DoomButton::Start: return DBTN_START; default: return DBTN_SELECT;
    }
  }
};

static RenPhoneDoomDisplay doomDisplay;
#if RP_SD_SPI
// Function pointer (bukan fungsi) supaya tidak kena prototipe C++ otomatis Arduino. Didefinisikan di w_file_esp32.cpp.
extern "C" void (*DG_SdBusRelease)(void);
extern "C" void (*DG_SdBusAcquire)(void);
static void doomSdBusRel(void){ doomDisplay.busRelease(); }   // dipanggil w_file_esp32.cpp sebelum baca SD
static void doomSdBusAcq(void){ doomDisplay.busAcquire(); }   // ...dan sesudahnya
#endif
static RenPhoneDoomInput   doomInput;

// -----------------------------------------------------------------
// LIFECYCLE / TASK
// -----------------------------------------------------------------
static TaskHandle_t doomTaskHandle = nullptr;
// v104: stack task DOOM (12KB) dipindah ke PSRAM, pola sama persis kayak
// NES (lihat nes_app_renphone.ino) & geminiTask (phone.ino) yg sudah
// kebukti jalan di device asli -- gak nulis helper cross-file di sini
// krn urutan concat .ino Arduino antar tab gak dijamin (self-contained).
static StackType_t* doomTaskStack = nullptr;
static StaticTask_t doomTaskTCB;
static volatile bool doomPlaying = false;      // true selagi task DOOM pegang SPI langsung
static volatile bool doomStopRequested = false;
static Orientation doomPrevOrient;
static String doomErrorMsg = "";               // non-kosong -> drawDoom() tampilkan pesan ini, task TIDAK pernah start

// DOOM_RACE_FIX_V1: doomBusy = TRUE sejak SEBELUM task dibuat sampai task BENAR-BENAR selesai.
// doomPlaying baru true SETELAH DoomGlue_Begin() selesai, padahal di dalam Begin() core 1 sudah
// menggambar frame Doom -> tidak cukup utk melindungi `display` dari core 0.
static volatile bool doomBusy = false;
bool doomIsPlaying(){ return doomPlaying; }
bool doomIsBusy(){ return doomBusy; }

// DOOM_BACK_GUARD_V1 (decl): konfirmasi keluar ketuk-2x. 0 = tidak armed; selain itu = batas waktu (millis)
// sampai ketukan Back kedua dianggap konfirmasi. Dibaca juga oleh doomBandHook() (core 1) utk toast.
#define DOOM_BACK_CONFIRM_MS 2000
static volatile uint32_t doomBackArmUntil = 0;
static inline bool doomBackArmed(){
  uint32_t u = doomBackArmUntil;
  return u != 0 && (int32_t)(u - millis()) > 0;
}

// DOOM_MULTIWAD_V1: cari WAD otomatis di root SD (urutan = prioritas). Nama harus dikenal engine (d_iwad.c)
// atau isinya punya lump MAP01/E1M1.
static const char* const DOOM_WAD_CANDIDATES[] = {
  "/doom2.wad", "/doom1.wad", "/doom.wad", "/freedoom2.wad", "/freedoom1.wad", "/plutonia.wad", "/tnt.wad"
};
static const char* doomWadSel = nullptr;
static fs::FS* doomWadFs = nullptr;   // FFAT_UPLOAD_V1: filesystem tempat WAD ditemukan
static const char* doomFindWad(){
  const size_t n = sizeof(DOOM_WAD_CANDIDATES)/sizeof(DOOM_WAD_CANDIDATES[0]);
  if(sdReady)   for(size_t i=0;i<n;i++) if(SD_MMC.exists(DOOM_WAD_CANDIDATES[i])){ doomWadFs=&SD_MMC; return DOOM_WAD_CANDIDATES[i]; }
  if(ffatReady) for(size_t i=0;i<n;i++) if(FFat.exists(DOOM_WAD_CANDIDATES[i])){  doomWadFs=&FFat;   return DOOM_WAD_CANDIDATES[i]; }
  return nullptr;
}

extern volatile uint32_t padFrames; // gamepad_renphone.ino
// DOOM_FFAT_PRELOAD_V1: lihat esp_doom_core/w_file_esp32.cpp
extern "C" int  DG_WadPreload(fs::FS* fs, const char* path);
extern "C" void DG_WadPreloadFree(void);
static void doomTaskFunc(void* arg){
  DG_FS = doomWadFs ? doomWadFs : &SD_MMC;   // FFAT_UPLOAD_V1
  DG_Display = &doomDisplay;
  DG_Input   = &doomInput;
  DoomGlue_SetHooks(doomExtKeys, padTakeMouseDX, padTakeTyped);   // DOOM_LAYOUT_V2: padExtKeys (web) + tombol sentuh
  DoomGlue_SetBandHook(doomBandHook);
  doomOvlBuild();
  doomInput.begin();

  DoomGlue_Begin(doomWadSel ? doomWadSel : DOOM_WAD_PATH);   // DOOM_MULTIWAD_V1
  Serial.printf("[doom] Begin selesai | PSRAM bebas=%u internal=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); // DOOM_NOMAP_DEBUG_V1
  doomPlaying = true;
  uint32_t stT0 = millis(), stFrames0 = 0, stDraw0 = 0, stTickUs = 0, stTicks = 0;
  int ovlStatic = 3;   // tombol di strip hitam atas/bawah digambar sekali (frame tidak menyentuh strip itu)

  while(!doomStopRequested){
    uint32_t tk0 = micros();
    static uint8_t dbgTk = 0;   // DOOM_NOMAP_DEBUG_V1: 3 tick pertama dicatat (kelihatan macet di mana)
    if(dbgTk < 3) Serial.printf("[doom] tick %u mulai\n", (unsigned)dbgTk);
    DoomGlue_Tick();
    if(dbgTk < 3){ Serial.printf("[doom] tick %u selesai (%lu us)\n", (unsigned)dbgTk, (unsigned long)(micros() - tk0)); dbgTk++; }
    stTickUs += micros() - tk0; stTicks++;
#if DOOM_STATS
    if(millis() - stT0 >= 1000){
      uint32_t dt = millis() - stT0;
      uint32_t fr = DG_FrameCount - stFrames0;
      uint32_t dus = DG_DrawUsAccum - stDraw0;
      uint32_t fps10 = dt ? (fr * 10000UL) / dt : 0;
      uint32_t drawMs = fr ? (dus / fr) / 1000 : 0;
      uint32_t tickMs = stTicks ? (stTickUs / stTicks) / 1000 : 0;
      char l1[48], l2[48];
      snprintf(l1, sizeof(l1), "fps %u.%u  tick %ums  kirim %ums", (unsigned)(fps10/10), (unsigned)(fps10%10), (unsigned)tickMs, (unsigned)drawMs);
      snprintf(l2, sizeof(l2), "VB:%c CM:%c BAND:%c int %uK maks %uK",
               DG_VideoBufInternal()?'I':'P', DG_ColormapInternal()?'I':'P', DoomGlue_BandPathActive()?'Y':'N',
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024));
      Serial.printf("[doom] %s | %s\n", l1, l2);
      display.waitDMA();
      display.startWrite();
      display.fillRect(0, 0, 284, 19, TFT_BLACK);
      display.setTextSize(1); display.setTextColor(TFT_YELLOW);
      display.setCursor(2, 1);  display.print(l1);
      display.setCursor(2, 10); display.print(l2);
      display.endWrite();
      stT0 = millis(); stFrames0 = DG_FrameCount; stDraw0 = DG_DrawUsAccum; stTickUs = 0; stTicks = 0;
    }
#endif
    // jalur cepat: tombol sudah ditempel ke tiap pita oleh doomBandHook(); jalur lama: gambar langsung
    if(ovlStatic > 0){ ovlStatic--; doomDrawOverlay(); }
    else if(!DoomGlue_BandPathActive()) doomDrawOverlay();
    padFrames++;       // HUD HW monitor di halaman /pad (gamepad_renphone.ino)
    vTaskDelay(1); // kasih jatah idle task FreeRTOS (WiFi/BT/WDT), sama alasannya dgn delay(1) di loop() utama
  }

  DoomGlue_Shutdown();
  DG_WadPreloadFree();   // DOOM_FFAT_PRELOAD_V1
  doomDisplay.close();
  doomOvlFree();
  doomPlaying = false;
  doomTaskHandle = nullptr;
  doomBusy = false;   // DOOM_RACE_FIX_V1: display boleh dipakai core 0 lagi
  vTaskDelete(nullptr);
}

// DOOM_MELT_V1: efek melt ala Doom asli. Dipanggil dari doomEnter() SEBELUM task dibuat
// (core 0 masih boleh menyentuh display). Gagal alokasi -> lewati animasi, tampil "Memuat..." biasa.
static void doomMeltIntro(){
  const int W = SCR_W, H = SCR_H, CW = 4;
  const int N = (W + CW - 1) / CW;
  LGFX_Sprite src(&display);
  src.setPsram(true);
  src.setColorDepth(16);
  int16_t* ys = (int16_t*)heap_caps_malloc(N * sizeof(int16_t), MALLOC_CAP_8BIT);
  if(!ys || !src.createSprite(W, H)){
    if(ys) heap_caps_free(ys);
    drawDoom(canvas); push();
    return;
  }
  // splash yang akan meleleh: gradasi merah -> gelap + tulisan DOOM
  for(int y = 0; y < H; y++){
    int r = 40 + (170 * (H - y)) / H;
    src.drawFastHLine(0, y, W, (uint16_t)((r >> 3) << 11));
  }
  src.setFont(&lgfx::fonts::Font0);
  src.setTextWrap(false);
  const int ts = (W >= 400) ? 10 : 7;
  src.setTextSize(ts);
  const int tx = (W - 4 * 6 * ts) / 2, ty = (H - 8 * ts) / 2 - 10;
  src.setTextColor(0x2000); src.setCursor(tx + ts / 2 + 2, ty + ts / 2 + 2); src.print("DOOM");
  src.setTextColor(0xFD20); src.setCursor(tx, ty);                          src.print("DOOM");

  // offset awal tiap kolom acak seperti wipe Doom (naik-turun max 1 per kolom)
  ys[0] = -(int)(esp_random() % 8);
  for(int i = 1; i < N; i++){
    int v = ys[i - 1] + (int)(esp_random() % 3) - 1;
    if(v > 0) v = 0;
    if(v < -8) v = -8;
    ys[i] = (int16_t)v;
  }
  const int step = max(8, H / 14);

  drawDoom(canvas);                       // latar yang terungkap = layar "Memuat..."
  src.pushSprite(&canvas, 0, 0); push();
  delay(220);                             // tahan splash sebentar sebelum meleleh

  for(int f = 0; f < 80; f++){
    bool alive = false;
    for(int i = 0; i < N; i++){
      if(ys[i] < 0){ ys[i]++; alive = true; }
      else if(ys[i] < H){ ys[i] += step; if(ys[i] > H) ys[i] = H; alive = true; }
    }
    drawDoom(canvas);
    for(int i = 0; i < N; i++){
      int y = ys[i];
      if(y >= H) continue;
      if(y < 0) y = 0;
      canvas.setClipRect(i * CW, 0, CW, H);
      src.pushSprite(&canvas, 0, y);
    }
    canvas.clearClipRect();
    push();
    if(!alive) break;
  }
  heap_caps_free(ys);
  src.deleteSprite();
  drawDoom(canvas); push();
}

// DOOM_PSRAM_CLEAN_V1: lepas cache PSRAM yg aman dibuang supaya blok kontigu terbesar membesar lagi.
//   level 1 = cache Home + preview App Switcher (kecil2 tapi tersebar -> memecah blok besar)
//   level 2 = level 1 + buffer gambar NASA di RAM (app NASA memuat ulang kalau dibuka lagi)
static void doomPsramClean(int level){
  homeCacheFree();
  hpFreeImages();   // HOME-FIX: 3 gambar halaman pager (~900 KB) tadinya tidak ikut dilepas -> blok kontigu < 6 MB
#ifdef SW_THUMB_SLOTS
  for(int i=0;i<SW_THUMB_SLOTS;i++) swThumbFree(i);
#endif
  if(level >= 2) nasaFreeRamImgBuf();
}

void doomEnter(){
  doomErrorMsg = "";
#if RP_SD_SPI
  DG_SdBusRelease = doomSdBusRel; DG_SdBusAcquire = doomSdBusAcq;   // AETHER_SD_FIX_V1
#endif
  homeCacheFree();   // DOOM_FREECACHE_V1: lepas cache scroll Home (PSRAM) sebelum cek blok kontigu
  hpFreeImages();    // HOME-FIX: gambar halaman pager juga

  if(doomBusy || doomTaskHandle != nullptr){ // DOOM_RACE_FIX_V1: jangan bikin task ke-2
    doomErrorMsg = "DOOM sebelumnya masih menutup.\nTunggu sebentar lalu buka lagi.";
    return;
  }
  if(!sdReady && !ffatReady){   // FFAT_UPLOAD_V1
    doomErrorMsg = "Kartu SD tidak terpasang\ndan FFat tidak tersedia.\nDOOM butuh WAD.";
    return;
  }
  doomWadSel = doomFindWad();   // DOOM_MULTIWAD_V1
  if(!doomWadSel){
    doomErrorMsg = String("WAD tidak ditemukan di SD/FFat.\nTaruh di root: doom2.wad, doom1.wad,\nfreedoom2.wad, plutonia.wad atau tnt.wad\n(nama persis; FFat via web upload).");
    return;
  }
  // Lib ESP-DOOM butuh blok PSRAM kontigu besar (zona internal DOOM,
  // dokumentasi library: ~6MB). Dicek DULU drpd biarin alokasi di
  // dalam doomgeneric gagal diam-diam / crash ambigu di tengah task.
  size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  // DOOM_PSRAM_CLEAN_V1: bersihkan bertahap, cek ulang tiap langkah (langkah 2 hanya kalau langkah 1 belum cukup)
  const size_t doomNeedBlk = (size_t)4*1024*1024 + 512*1024;  // DOOM_GATE_FIX_V1: MIN_RAM engine 4MiB + reserve; zone turun otomatis 6->5->4 (i_system.c)
  const size_t doomBlk0 = freeBlock;
  const size_t doomNasaKB = nasaRamImgLen/1024;
  int doomCleanLv = 0;
  if(freeBlock < doomNeedBlk){ doomPsramClean(1); doomCleanLv = 1; freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
  if(freeBlock < doomNeedBlk){ doomPsramClean(2); doomCleanLv = 2; freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
  Serial.printf("[doom] PSRAM kontigu: %uKB -> %uKB (bersih lv%d) | total bebas %uKB | butuh %uKB | gambar NASA %uKB\n",
                (unsigned)(doomBlk0/1024), (unsigned)(freeBlock/1024), doomCleanLv,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024), (unsigned)(doomNeedBlk/1024), (unsigned)doomNasaKB);
  if(freeBlock < doomNeedBlk){
    doomErrorMsg = "PSRAM kontigu tidak cukup utk DOOM\n(butuh ~4.5MB, tersedia " + String(freeBlock/1024) +
                   "KB;\nsebelum dibersihkan " + String(doomBlk0/1024) + "KB, total bebas " +
                   String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024) + "KB).\nTutup app lain / restart device.";
    return;
  }

  // DOOM_FFAT_PRELOAD_V1: task DOOM ber-stack PSRAM -> akses FFat (flash internal) dari situ
  // memicu assert esp_task_stack_is_sane_cache_disabled() & reboot. WAD dari FFat dimuat UTUH ke
  // PSRAM di sini (task pemanggil ber-stack internal = aman), lalu engine baca dari memori.
  // WAD dari SD tidak perlu (SDMMC tidak mematikan cache).
  if(doomWadFs != (fs::FS*)&SD_MMC){
    if(!DG_WadPreload(doomWadFs, doomWadSel)){
      doomErrorMsg = "WAD di FFat gagal dimuat ke PSRAM\n(PSRAM kurang / WAD terlalu besar).\nPakai kartu SD utk WAD besar.";
      return;
    }
  }

  // DOOM dipaksa LANDSCAPE (lihat catatan di hw_conf.h) -- orientasi lama
  // disimpan & dikembalikan pas doomExit().
  doomPrevOrient = currentOrient;
  if(currentOrient != ORIENT_LANDSCAPE) applyOrientation(ORIENT_LANDSCAPE, false);

  // v104: DOOM gambar LANGSUNG ke `display` (bukan lewat `canvas` spt app
  // lain -- lihat doomDrawOverlay() & DoomGlue), jadi paling kerasa kalau
  // SPI Overclock (Settings) lagi disetel tinggi -- gejalanya persis yg
  // dilaporkan: kadang crash / warna keinvert sendiri di tengah main.
  // Fix: paksa turun ke index 0 (60MHz, paling aman/terbukti) SELAMA
  // Doom jalan, lalu balikin ke pilihan user di doomExit(). AMAN dipanggil
  // di sini (runtime, bukan cuma pas boot) krn momennya pas TIDAK ADA
  // transaksi SPI yg lagi jalan -- task Doom belum dibuat sama sekali di
  // titik ini. spiOcIdx (pilihan user) TIDAK diubah/ditimpa, cuma dipakai
  // sbg patokan buat balikin nanti -- jadi pilihan di Settings tetap apa
  // adanya, cuma "dipinjam turun sementara" khusus selagi di app Doom.
  if(spiOcIdx != 0) display.setSpiFreqMHz(SPI_OC_OPTIONS[0]);

  // DOOM_MELT_V1: boost CPU sama spt enterGameMode(), tapi TANPA ring booster -> diganti efek melt.
  if(!gameModeActive){ gameModeActive = true; setCpuFrequencyMhz(CPU_MHZ_GAME); }
  doomMeltIntro();

  doomStopRequested = false;
  doomBackArmUntil = 0;   // DOOM_BACK_GUARD_V1
  // DOOM_RACE_FIX_V1: gambar "Memuat..." SEKALI dari core 0 (task belum ada -> aman), lalu
  // serahkan display ke core 1. Mulai titik ini core 0 DILARANG menyentuh display
  // (loop() + push() di phone.ino cek doomIsBusy()).
  drawDoom(canvas); push();
  doomBusy = true;
  const uint32_t DOOM_STACK_SZ = 32768;
  BaseType_t res;
  if(!doomTaskStack){
    doomTaskStack = (StackType_t*)heap_caps_aligned_alloc(16, DOOM_STACK_SZ, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  }
  if(doomTaskStack){
    doomTaskHandle = xTaskCreateStaticPinnedToCore(doomTaskFunc, "doomTask", DOOM_STACK_SZ, NULL, 1, doomTaskStack, &doomTaskTCB, 1);
    res = doomTaskHandle ? pdPASS : pdFAIL;
  } else {
    // PSRAM gagal dialokasi -- fallback ke stack internal drpd gagal total.
    res = xTaskCreatePinnedToCore(doomTaskFunc, "doomTask", DOOM_STACK_SZ, NULL, 1, &doomTaskHandle, 1);
  }
  if(res != pdPASS){
    doomBusy = false;   // DOOM_RACE_FIX_V1
    DG_WadPreloadFree();   // DOOM_FFAT_PRELOAD_V1
    doomErrorMsg = "Gagal membuat task DOOM (RAM internal habis).";
    if(spiOcIdx != 0) display.setSpiFreqMHz(SPI_OC_OPTIONS[spiOcIdx]); // v104: gagal start -> balikin SPI, jangan nyangkut di 60MHz
    exitGameMode();
    if(currentOrient != doomPrevOrient) applyOrientation(doomPrevOrient, false);
  }
}

void doomExit(){
  if(doomTaskHandle != nullptr){
    doomStopRequested = true;
    // Tunggu task beneran selesai (DoomGlue_Shutdown terpanggil) sblm
    // lanjut -- max ~2 detik, cukup longgar drpd nge-block selamanya
    // kalau ada yg aneh di dalam doomgeneric.
    uint32_t waitStart = millis();
    // DOOM_RACE_FIX_V1: tunggu doomBusy (termasuk fase Begin/"Memuat..."), bukan doomPlaying.
    while(doomBusy && millis()-waitStart < 12000) delay(10);
    if(doomBusy) Serial.println("[doom] PERINGATAN: task belum selesai setelah 12 dtk");
    else delay(30);   // kasih idle task waktu bersihin TCB statis sebelum doomEnter berikutnya
  }
  // v104: balikin SPI ke pilihan user di Settings -- aman di sini krn
  // loop di atas udah mastiin task Doom (yg gambar langsung ke `display`)
  // beneran berhenti dulu, jd gak ada transaksi SPI yg lagi jalan.
  if(spiOcIdx != 0) display.setSpiFreqMHz(SPI_OC_OPTIONS[spiOcIdx]);
  exitGameMode();
  if(currentOrient != doomPrevOrient) applyOrientation(doomPrevOrient, true);
  needRedrawNow();
}

// -----------------------------------------------------------------
// OVERLAY tombol virtual -- digambar LANGSUNG ke `display` (bukan ke
// canvas -- canvas sama sekali tidak disentuh selagi doomPlaying, lihat
// guard di loop() phone.ino), dipanggil tiap kali sesudah DoomGlue_Tick()
// menggambar 1 frame, pola sama persis dgn nesOverlay_afterFrame().
// -----------------------------------------------------------------
static int doomFireCx(){ return SCR_W-30; }
static int doomFireCy(){ return SCR_H-34; }
static int doomUseCx(){  return SCR_W-30; }
static int doomUseCy(){  return SCR_H-84; }
static int doomOkCx(){   return SCR_W-30; }
static int doomOkCy(){   return SCR_H-124; }
static int doomPauseX(){ return SCR_W-30; }
static int doomPauseY(){ return 24; }

// DOOM_LAYOUT_V2: pad lebar. Hanya aktif kalau panel >= 440x300 (ST7796 480x320: margin kiri/kanan 80 px hitam).
static inline bool doomWide(){ return SCR_W >= 440 && SCR_H >= 300; }
#define DOOM_STICK_R 34
static int doomStickCx(){ return 40; }
static int doomStickCy(){ return 186; }
static int doomWpPrevCx(){ return SCR_W-58; }
static int doomWpNextCx(){ return SCR_W-22; }
static int doomWpCy(){ return 72; }
static int doomRunCx(){ return SCR_W-30; }
static int doomRunCy(){ return 128; }
static int doomMapCx(){ return 40; }
static int doomMapCy(){ return 92; }
// Bit tombol EXTRA: urutan HARUS sama dgn EXTMAP[] di esp_doom_core/DoomGlue.cpp
#define DEXT_STRAFE_L (1u<<0)
#define DEXT_STRAFE_R (1u<<1)
#define DEXT_RUN      (1u<<2)
#define DEXT_MAP      (1u<<11)
#define DEXT_WPN_PREV (1u<<29)
#define DEXT_WPN_NEXT (1u<<30)

// ---- overlay yg ditempel ke tiap pita (jalur cepat) ----
// Tiap tombol di-render SEKALI ke sprite kecil, lalu dikonversi ke format kabel (R/B ditukar spt palet
// DOOM). Per frame cuma menyalin piksel non-transparan -> jauh lebih murah drpd gambar lingkaran+teks.
struct DoomOvl { int x, y, w, h; uint16_t* pix; uint16_t key; bool toast; };   // DOOM_BACK_GUARD_V1: +toast
static DoomOvl doomOvl[5];
static int doomOvlN = 0;

static uint16_t doomWireFixRB(uint16_t wire){
#if DOOM_SWAP_RB
  uint16_t v = (wire >> 8) | (wire << 8);
  v = ((v & 0x1F) << 11) | (v & 0x07E0) | (v >> 11);
  return (v >> 8) | (v << 8);
#else
  return wire;
#endif
}

static void doomOvlPaint(LGFX_Sprite& sp, int kind, int r, const char* label, uint16_t col){
  if(kind == 0){                       // tombol bulat
    sp.fillCircle(r,r,r,col); sp.drawCircle(r,r,r,TFT_BLACK);
    sp.setTextColor(TFT_WHITE); sp.setTextSize(1);
    int tw = sp.textWidth(label); sp.setCursor(r-tw/2, r-3); sp.print(label);
  } else if(kind == 2){                // DOOM_BACK_GUARD_V1: toast konfirmasi keluar
    int w = sp.width(), h = sp.height();
    sp.fillRoundRect(0,0,w,h,6,0x2104); sp.drawRoundRect(0,0,w,h,6,TFT_WHITE);
    sp.setTextColor(TFT_WHITE); sp.setTextSize(1);
    int tw = sp.textWidth(label); sp.setCursor((w-tw)/2, (h-8)/2); sp.print(label);
  } else {                             // tombol pause
    sp.fillRoundRect(0,0,32,28,4,0x4208); sp.drawRoundRect(0,0,32,28,4,TFT_BLACK);
    sp.fillRect(10,6,4,16,TFT_WHITE); sp.fillRect(18,6,4,16,TFT_WHITE);
  }
}

static void doomOvlAdd(int x, int y, int w, int h, int kind, int r, const char* label, uint16_t col){
  if(doomOvlN >= 5) return;
  LGFX_Sprite spr(&display);
  spr.setColorDepth(16);
  spr.setPsram(false);
  if(!spr.createSprite(w, h)) return;
  spr.fillSprite(0x0001);
  uint16_t* sb = (uint16_t*)spr.getBuffer();
  uint16_t key = sb[0];
  doomOvlPaint(spr, kind, r, label, col);
  uint16_t* pix = (uint16_t*)heap_caps_malloc((size_t)w*h*2, MALLOC_CAP_8BIT);
  if(!pix){ spr.deleteSprite(); return; }
  for(int i=0;i<w*h;i++) pix[i] = (sb[i]==key) ? key : doomWireFixRB(sb[i]);
  DoomOvl& o = doomOvl[doomOvlN++];
  o.x=x; o.y=y; o.w=w; o.h=h; o.pix=pix; o.key=key; o.toast=false;
  spr.deleteSprite();
}

void doomOvlBuild(){
  doomOvlN = 0;
  doomOvlAdd(doomFireCx()-22, doomFireCy()-22, 45, 45, 0, 22, "F",  0xF800);
  doomOvlAdd(doomUseCx()-16,  doomUseCy()-16,  33, 33, 0, 16, "U",  0x07E0);
  doomOvlAdd(doomOkCx()-14,   doomOkCy()-14,   29, 29, 0, 14, "OK", 0x001F);
  doomOvlAdd(doomPauseX()-16, doomPauseY()-14, 33, 29, 1, 0,  "",   0);
  // DOOM_BACK_GUARD_V1: toast -- tinggi 22, lebar ~ teks 28 huruf @6px + padding; hanya DITEMPEL saat armed.
  { int tw = 196, th = 22;
    doomOvlAdd((SCR_W-tw)/2, 8, tw, th, 2, 0, "Ketuk Back lagi untuk keluar", 0);
    if(doomOvlN > 0) doomOvl[doomOvlN-1].toast = true; }
}

void doomOvlFree(){
  for(int i=0;i<doomOvlN;i++){ if(doomOvl[i].pix) heap_caps_free(doomOvl[i].pix); doomOvl[i].pix=nullptr; }
  doomOvlN = 0;
}

// Dipanggil DoomGlue untuk tiap pita (koordinat panel). Tempel tombol yg beririsan dgn pita ini.
void doomBandHook(uint16_t* buf, int px, int py, int w, int h){
  for(int k=0;k<doomOvlN;k++){
    const DoomOvl& o = doomOvl[k];
    if(o.toast && !doomBackArmed()) continue;   // DOOM_BACK_GUARD_V1
    int y0 = max(o.y, py), y1 = min(o.y+o.h, py+h);
    int x0 = max(o.x, px), x1 = min(o.x+o.w, px+w);
    if(y0>=y1 || x0>=x1) continue;
    for(int y=y0;y<y1;y++){
      uint16_t* d = buf + (size_t)(y-py)*w + (x0-px);
      const uint16_t* sp = o.pix + (size_t)(y-o.y)*o.w + (x0-o.x);
      for(int x=x0;x<x1;x++,d++,sp++) if(*sp != o.key) *d = *sp;
    }
  }
}

// DOOM_LAYOUT_V2: gambar joystick + tombol baru di margin hitam (digambar langsung ke display, 3 frame pertama).
static void doomDrawPadV2(){
  const uint16_t BG=0x2104, EDGE=0x7BEF, BTN=0x4208;
  auto lbl=[&](int cx,int cy,const char* t,uint16_t col){
    display.setTextSize(1); display.setTextColor(col);
    int tw = display.textWidth(t);
    display.setCursor(cx-tw/2, cy-3);
    display.print(t);
  };
  // joystick: cincin + 4 panah (atas/bawah = maju/mundur, kiri/kanan = geser)
  int cx=doomStickCx(), cy=doomStickCy();
  display.fillCircle(cx,cy,DOOM_STICK_R,BG);
  display.drawCircle(cx,cy,DOOM_STICK_R,EDGE);
  display.drawCircle(cx,cy,12,BTN);
  display.fillTriangle(cx,cy-29, cx-6,cy-19, cx+6,cy-19, TFT_WHITE);
  display.fillTriangle(cx,cy+29, cx-6,cy+19, cx+6,cy+19, TFT_WHITE);
  display.fillTriangle(cx-29,cy, cx-19,cy-6, cx-19,cy+6, TFT_WHITE);
  display.fillTriangle(cx+29,cy, cx+19,cy-6, cx+19,cy+6, TFT_WHITE);
  lbl(cx, cy+DOOM_STICK_R+9, "GERAK", EDGE);
  // senjata sebelumnya / berikutnya
  for(int i=0;i<2;i++){
    int bx = i ? doomWpNextCx() : doomWpPrevCx();
    display.fillCircle(bx,doomWpCy(),14,BTN);
    display.drawCircle(bx,doomWpCy(),14,TFT_BLACK);
    lbl(bx,doomWpCy(), i ? ">" : "<", TFT_WHITE);
  }
  lbl(SCR_W-40, doomWpCy()+24, "SENJATA", EDGE);
  // run (ditahan) dan map
  display.fillCircle(doomRunCx(),doomRunCy(),16,0xFFE0);
  display.drawCircle(doomRunCx(),doomRunCy(),16,TFT_BLACK);
  lbl(doomRunCx(),doomRunCy(),"RUN",TFT_BLACK);
  display.fillCircle(doomMapCx(),doomMapCy(),14,BTN);
  display.drawCircle(doomMapCx(),doomMapCy(),14,TFT_BLACK);
  lbl(doomMapCx(),doomMapCy(),"MAP",TFT_WHITE);
}

void doomDrawOverlay(){
  display.waitDMA();   // DMA frame terakhir harus selesai sebelum gambar langsung
  display.startWrite();
  auto btn=[&](int cx,int cy,int r,const char* label,uint16_t col){
    display.fillCircle(cx,cy,r,col);
    display.drawCircle(cx,cy,r,TFT_BLACK);
    display.setTextColor(TFT_WHITE);
    display.setTextSize(1);
    int tw = display.textWidth(label);
    display.setCursor(cx-tw/2, cy-3);
    display.print(label);
  };
  btn(doomFireCx(),  doomFireCy(),  22, "F",  0xF800 /*red*/);
  btn(doomUseCx(),   doomUseCy(),   16, "U",  0x07E0 /*green*/);
  btn(doomOkCx(),    doomOkCy(),    14, "OK", 0x001F /*blue*/);
  display.fillRoundRect(doomPauseX()-16, doomPauseY()-14, 32, 28, 4, 0x4208 /*abu gelap*/);
  display.drawRoundRect(doomPauseX()-16, doomPauseY()-14, 32, 28, 4, TFT_BLACK);
  display.fillRect(doomPauseX()-6, doomPauseY()-8, 4, 16, TFT_WHITE);
  display.fillRect(doomPauseX()+2, doomPauseY()-8, 4, 16, TFT_WHITE);
  if(doomWide()) doomDrawPadV2();   // DOOM_LAYOUT_V2
  display.endWrite();
}

// -----------------------------------------------------------------
// TOUCH -- 2 zona (kiri: maju/mundur, kanan: noleh) + 4 tombol bulat
// tetap (Fire/Use/OK/Pause) + tombol keluar (isBack(), pola standar
// yg sama dipakai semua app lain). Skema & gaya persis reuse dari
// infTouch() (game "Inferno") yg sudah ada duluan di phone.ino.
// -----------------------------------------------------------------
// 2 jari (layar kapasitif): tiap jari punya zona/jangkar/bitmask tombol SENDIRI;
// doomInput.raw[] = gabungan (OR) kedua jari -> bisa jalan + tembak bersamaan.
static bool    doomSlotLeft[2]={false,false};
static int     doomSlotAX[2]={0,0}, doomSlotAY[2]={0,0};
static uint8_t doomSlotMask[2]={0,0};

// DOOM_LAYOUT_V2: tombol EXTRA dari sentuhan. Ganti senjata/Map = sekali-tekan -> di-latch supaya tidak
// hilang di antara dua tic DOOM (alasan sama dgn DOOM_FIRE_LATCH_V1).
static volatile uint32_t doomSlotExt[2] = {0,0};          // tombol EXTRA yg ditahan tiap jari
static bool     doomSlotStick[2] = {false,false};         // jari ini memegang joystick kiri
static bool     doomSlotBtn[2]   = {false,false};         // jari ini mulai di tombol baru (abaikan geseran)
static volatile uint32_t doomExtLatchMask = 0, doomExtLatchUntil = 0;
#define DOOM_EXT_LATCH_MS 120
static inline bool doomInCirc(int x,int y,int cx,int cy,int r){ int dx=x-cx, dy=y-cy; return dx*dx+dy*dy <= r*r; }
static void doomExtLatch(uint32_t bit){
  uint32_t now = millis();
  if((int32_t)(doomExtLatchUntil - now) <= 0) doomExtLatchMask = 0;   // latch lama sudah habis -> mulai bersih
  doomExtLatchMask = doomExtLatchMask | bit;
  uint32_t u = now + DOOM_EXT_LATCH_MS; if(u==0) u=1;
  doomExtLatchUntil = u;
}
// Dipanggil engine DOOM (core 1) lewat DoomGlue_SetHooks().
uint32_t doomExtKeys(){
  uint32_t e = padExtKeys() | doomSlotExt[0] | doomSlotExt[1];
  if((int32_t)(doomExtLatchUntil - millis()) > 0) e |= doomExtLatchMask;
  return e;
}
#define DOOM_JOY_RADIUS 46
#define DOOM_DIR_MASK ((1u<<DBTN_UP)|(1u<<DBTN_DOWN)|(1u<<DBTN_LEFT)|(1u<<DBTN_RIGHT))

#define DOOM_LATCH_MS 90   // DOOM_FIRE_LATCH_V1
static void doomApplyMasks(){
  uint8_t m = doomSlotMask[0] | doomSlotMask[1];
  for(int i=0;i<DBTN_COUNT;i++){
    bool nv = (m>>i)&1;
    // tombol AKSI (bukan arah) baru ditekan -> tahan minimal DOOM_LATCH_MS agar tak hilang antar tick
    if(nv && !doomInput.raw[i] && ((1u<<i) & ~DOOM_DIR_MASK)){
      uint32_t u = millis() + DOOM_LATCH_MS; if(u==0) u=1;
      doomInput.latchUntil[i] = u;
    }
    doomInput.raw[i] = nv;
  }
}
static void doomReleaseAll(){
  doomSlotMask[0]=doomSlotMask[1]=0;
  doomSlotExt[0]=doomSlotExt[1]=0; doomSlotStick[0]=doomSlotStick[1]=false; doomSlotBtn[0]=doomSlotBtn[1]=false;   // DOOM_LAYOUT_V2
  doomApplyMasks();
}

// DOOM_BACK_GUARD_V1: gerbang keluar. true = BOLEH keluar sekarang.
//  - task Doom belum jalan ("Memuat..." / layar error) -> true langsung (tdk ada progres yg bisa hilang)
//  - sedang main & belum armed -> arm, munculkan toast, return false
//  - sedang main & sudah armed (ketukan ke-2 dlm DOOM_BACK_CONFIRM_MS) -> true
// Dipanggil dari doomTouchSlot() (tombol Back) & dari gestur swipe-atas di phone.ino.
bool doomExitGuard(){
  if(!doomPlaying){ doomBackArmUntil = 0; return true; }
  if(doomBackArmed()){ doomBackArmUntil = 0; return true; }
  doomBackArmUntil = millis() + DOOM_BACK_CONFIRM_MS;
  if(doomBackArmUntil == 0) doomBackArmUntil = 1;   // jaga2 millis() pas == 0 (wrap)
  doomReleaseAll();                                 // lepas semua tombol: tangan yg nyasar jgn bikin karakter lari terus
  vibWarn();
  return false;
}

static void doomTouchSlot(int s,int x,int y,bool held,bool isNew){
  if(!held && !isNew){ doomSlotMask[s]=0; doomSlotExt[s]=0; doomSlotStick[s]=false; doomSlotBtn[s]=false; doomApplyMasks(); return; }   // DOOM_LAYOUT_V2

  if(isNew && isBack(x,y)){ if(doomExitGuard()) navBack(); return; } // DOOM_BACK_GUARD_V1: ketuk 2x utk keluar -> doomExit() lewat appOnExit()

  uint8_t &m = doomSlotMask[s];

  // DOOM_LAYOUT_V2: joystick kiri tetap + tombol Senjata/Run/Map (hanya panel lebar)
  if(doomWide()){
    if(isNew){
      doomSlotStick[s]=false; doomSlotBtn[s]=false;
      if(doomInCirc(x,y,doomStickCx(),doomStickCy(),46))        doomSlotStick[s]=true;
      else if(doomInCirc(x,y,doomWpPrevCx(),doomWpCy(),17)){    doomExtLatch(DEXT_WPN_PREV); doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomWpNextCx(),doomWpCy(),17)){    doomExtLatch(DEXT_WPN_NEXT); doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomMapCx(),doomMapCy(),20)){      doomExtLatch(DEXT_MAP);      doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomRunCx(),doomRunCy(),22)){      doomSlotExt[s] = doomSlotExt[s] | DEXT_RUN; doomSlotBtn[s]=true; }
    }
    if(doomSlotStick[s]){
      // Joystick tetap: atas/bawah = maju/mundur, kiri/kanan = geser (strafe). Zona mati 12 px.
      int ox=x-doomStickCx(), oy=y-doomStickCy();
      const int SD=12;
      m &= ~DOOM_DIR_MASK;
      uint32_t e = 0;
      if(oy < -SD) m |= (1u<<DBTN_UP);
      if(oy >  SD) m |= (1u<<DBTN_DOWN);
      if(ox < -SD) e |= DEXT_STRAFE_L;
      if(ox >  SD) e |= DEXT_STRAFE_R;
      doomSlotExt[s] = (doomSlotExt[s] & ~(DEXT_STRAFE_L|DEXT_STRAFE_R)) | e;
      doomApplyMasks(); return;
    }
    if(doomSlotBtn[s]) return;   // jari mulai di tombol baru: abaikan geseran sampai diangkat (Run tetap ditahan)
  }
  // Tombol bulat tetap diperiksa DULU supaya ketukan di areanya TIDAK ikut jadi basis d-pad/turn.
  int dx,dy;
  dx=x-doomFireCx(); dy=y-doomFireCy();
  if(dx*dx+dy*dy <= 30*30){ m |= (1u<<DBTN_FIRE); doomApplyMasks(); return; }   // DOOM_FIRE_LATCH_V1: hit area diperbesar (gambar tetap 22)
  dx=x-doomUseCx(); dy=y-doomUseCy();
  if(dx*dx+dy*dy <= 20*20){ if(isNew) m |= (1u<<DBTN_USE); doomApplyMasks(); return; }   // hit area diperbesar (gambar tetap 16)
  dx=x-doomOkCx(); dy=y-doomOkCy();
  if(dx*dx+dy*dy <= 14*14){ if(isNew) m |= (1u<<DBTN_START); doomApplyMasks(); return; }
  if(x>=doomPauseX()-16 && x<=doomPauseX()+16 && y>=doomPauseY()-14 && y<=doomPauseY()+14){
    if(isNew) m |= (1u<<DBTN_SELECT);
    doomApplyMasks(); return;
  }

  if(isNew){
    doomSlotLeft[s] = (x < SCR_W/2);
    doomSlotAX[s]=x; doomSlotAY[s]=y;
    return;
  }

  int ox=x-doomSlotAX[s], oy=y-doomSlotAY[s];
  const int DEAD=10;
  m &= ~DOOM_DIR_MASK;
  if(doomSlotLeft[s]){
    // Kiri: maju/mundur dari offset VERTIKAL jangkar (ditahan miring -> jalan terus).
    if(oy < -DEAD) m |= (1u<<DBTN_UP);
    if(oy >  DEAD) m |= (1u<<DBTN_DOWN);
  } else {
    // Kanan: noleh dari offset HORIZONTAL jangkar.
    if(ox < -DEAD) m |= (1u<<DBTN_LEFT);
    if(ox >  DEAD) m |= (1u<<DBTN_RIGHT);
  }
  doomApplyMasks();
}

void doomTouch(int x,int y,bool held,bool isNew){  doomTouchSlot(0,x,y,held,isNew); }
void doomTouch2(int x,int y,bool held,bool isNew){ doomTouchSlot(1,x,y,held,isNew); }

// -----------------------------------------------------------------
// DRAW (fallback) -- HANYA kepanggil selagi doomPlaying==false, yaitu:
// (a) sesaat sebelum task sempat mulai pas app baru dibuka, atau
// (b) doomEnter() gagal (WAD hilang / PSRAM kurang / dst) & doomErrorMsg
//     terisi -- task TIDAK PERNAH start, jadi ini jalan terus sbg layar
//     error yg jelas (bukan hang/crash diam-diam).
// -----------------------------------------------------------------
void drawDoom(LGFX_Sprite& s){
  s.fillSprite(TFT_BLACK);
  s.setTextColor(TFT_WHITE); s.setTextSize(2);
  s.setCursor(SCR_W/2-60, SCR_H/2-40); s.print("DOOM");
  s.setTextSize(1); s.setTextColor(T().subtext);
  if(doomErrorMsg.length()>0){
    // wrap kasar per baris (pesan sengaja pendek, lihat doomEnter())
    int ly=SCR_H/2-10; int start=0;
    for(int i=0;i<=doomErrorMsg.length();i++){
      if(i==doomErrorMsg.length() || doomErrorMsg[i]=='\n'){
        s.setCursor(SCR_W/2-100, ly); s.print(doomErrorMsg.substring(start,i));
        ly+=14; start=i+1;
      }
    }
    s.setCursor(SCR_W/2-60, ly+10); s.setTextColor(T().accent); s.print(RPTR("< Back utk kembali"));
  } else {
    s.setCursor(SCR_W/2-40, SCR_H/2-10); s.print(RPTR("Memuat..."));
  }
  drawBack(s);
}
