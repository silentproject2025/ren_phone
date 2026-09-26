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
  void beginFrame() override { display.startWrite(); }
  void writeRow(int x, int y, const uint16_t* rgb565, int count) override {
    display.setAddrWindow(x, y, count, 1);
    display.writePixels((const lgfx::rgb565_t*)rgb565, count);
  }
  void endFrame() override { display.endWrite(); }
  int width()  const override { return DOOM_PANEL_W; }
  int height() const override { return DOOM_PANEL_H; }
};

// -----------------------------------------------------------------
// INPUT -- 8 tombol digital. `raw[]` diisi dari doomTouch() (dipanggil
// dari core 0 / loop UI), dibaca+diambil snapshot di update() (dipanggil
// dari task DOOM sendiri di core 1). Sama kayak bool biasa yg dipakai
// bendera lintas-task di tempat lain di file ini (mis. gameModeActive) --
// cukup aman utk 8 bool independen spt ini, tidak butuh mutex.
// -----------------------------------------------------------------
class RenPhoneDoomInput : public IDoomInput {
public:
  volatile bool raw[DBTN_COUNT] = {false};
  void begin() override { for(int i=0;i<DBTN_COUNT;i++){ raw[i]=false; cur[i]=false; prev[i]=false; } }
  void update() override { for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i]; } }
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

bool doomIsPlaying(){ return doomPlaying; }

static void doomTaskFunc(void* arg){
  DG_FS = &SD_MMC;
  DG_Display = &doomDisplay;
  DG_Input   = &doomInput;
  doomInput.begin();

  DoomGlue_Begin(DOOM_WAD_PATH);
  doomPlaying = true;

  while(!doomStopRequested){
    DoomGlue_Tick();
    doomDrawOverlay(); // gambar tombol virtual DI ATAS frame yg baru saja ditulis DoomGlue_Tick()
    vTaskDelay(1); // kasih jatah idle task FreeRTOS (WiFi/BT/WDT), sama alasannya dgn delay(1) di loop() utama
  }

  DoomGlue_Shutdown();
  doomPlaying = false;
  doomTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void doomEnter(){
  doomErrorMsg = "";

  if(!sdReady){
    doomErrorMsg = "Kartu SD tidak terpasang.\nDOOM butuh WAD di SD.";
    return;
  }
  if(!SD_MMC.exists(DOOM_WAD_PATH)){
    doomErrorMsg = String("WAD tidak ditemukan:\n") + DOOM_WAD_PATH +
                   "\nTaruh file WAD (mis. doom1.wad)\npersis di path itu di SD.";
    return;
  }
  // Lib ESP-DOOM butuh blok PSRAM kontigu besar (zona internal DOOM,
  // dokumentasi library: ~6MB). Dicek DULU drpd biarin alokasi di
  // dalam doomgeneric gagal diam-diam / crash ambigu di tengah task.
  size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if(freeBlock < (size_t)6*1024*1024){
    doomErrorMsg = "PSRAM kontigu tidak cukup utk DOOM\n(butuh ~6MB, tersedia " +
                   String(freeBlock/1024) + "KB).\nTutup app lain / restart device.";
    return;
  }

  // DOOM dipaksa LANDSCAPE (lihat catatan di hw_conf.h) -- orientasi lama
  // disimpan & dikembalikan pas doomExit().
  doomPrevOrient = currentOrient;
  if(currentOrient != ORIENT_LANDSCAPE) applyOrientation(ORIENT_LANDSCAPE, false);

  enterGameMode(); // boost CPU_MHZ_GAME, sama spt Snake/Inferno/dll

  doomStopRequested = false;
  const uint32_t DOOM_STACK_SZ = 12288;
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
    doomErrorMsg = "Gagal membuat task DOOM (RAM internal habis).";
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
    while(doomPlaying && millis()-waitStart < 2000) delay(10);
  }
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

void doomDrawOverlay(){
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
  display.endWrite();
}

// -----------------------------------------------------------------
// TOUCH -- 2 zona (kiri: maju/mundur, kanan: noleh) + 4 tombol bulat
// tetap (Fire/Use/OK/Pause) + tombol keluar (isBack(), pola standar
// yg sama dipakai semua app lain). Skema & gaya persis reuse dari
// infTouch() (game "Inferno") yg sudah ada duluan di phone.ino.
// -----------------------------------------------------------------
static bool doomZoneIsLeft=false;
static int  doomAnchorX=0, doomAnchorY=0;
#define DOOM_JOY_RADIUS 46

static void doomReleaseAll(){
  for(int i=0;i<DBTN_COUNT;i++) doomInput.raw[i]=false;
}

void doomTouch(int x,int y,bool held,bool isNew){
  if(!held && !isNew){ doomReleaseAll(); return; }

  if(isNew && isBack(x,y)){ navBack(); return; } // keluar app -> doomExit() lewat appOnExit()

  // Tombol bulat tetap diperiksa DULU (pola sama persis dgn tombol
  // tembak/ganti-senjata Inferno) supaya ketukan di areanya TIDAK ikut
  // jadi basis d-pad/turn di bawah.
  int dx,dy;
  dx=x-doomFireCx(); dy=y-doomFireCy();
  if(dx*dx+dy*dy <= 22*22){ doomInput.raw[DBTN_FIRE] = true; return; }
  dx=x-doomUseCx(); dy=y-doomUseCy();
  if(dx*dx+dy*dy <= 16*16){ if(isNew) doomInput.raw[DBTN_USE]=true; return; }
  dx=x-doomOkCx(); dy=y-doomOkCy();
  if(dx*dx+dy*dy <= 14*14){ if(isNew) doomInput.raw[DBTN_START]=true; return; }
  if(x>=doomPauseX()-16 && x<=doomPauseX()+16 && y>=doomPauseY()-14 && y<=doomPauseY()+14){
    if(isNew) doomInput.raw[DBTN_SELECT]=true; return;
  }

  if(isNew){
    doomZoneIsLeft = (x < SCR_W/2);
    doomAnchorX=x; doomAnchorY=y;
    return;
  }

  int ox=x-doomAnchorX, oy=y-doomAnchorY;
  const int DEAD=10;
  if(doomZoneIsLeft){
    // Kiri: maju/mundur dari offset VERTIKAL jangkar (spt Snake/Inferno --
    // ditahan miring, bukan digeser berkali-kali, biar jalan terus).
    doomInput.raw[DBTN_UP]   = (oy < -DEAD);
    doomInput.raw[DBTN_DOWN] = (oy >  DEAD);
    doomInput.raw[DBTN_LEFT] = false; doomInput.raw[DBTN_RIGHT] = false;
  } else {
    // Kanan: noleh dari offset HORIZONTAL jangkar.
    doomInput.raw[DBTN_LEFT]  = (ox < -DEAD);
    doomInput.raw[DBTN_RIGHT] = (ox >  DEAD);
    doomInput.raw[DBTN_UP] = false; doomInput.raw[DBTN_DOWN] = false;
  }
}

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
    s.setCursor(SCR_W/2-60, ly+10); s.setTextColor(T().accent); s.print("< Back utk kembali");
  } else {
    s.setCursor(SCR_W/2-40, SCR_H/2-10); s.print("Memuat...");
  }
  drawBack(s);
}
