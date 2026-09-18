// =================================================================
// nes_app_renphone.ino
// "App" SCR_NES: scan ROM .nes di root SD card, biarin user pilih dari
// list (bukan 1 path hardcode), baru nyalain task emulator.
// =================================================================

extern "C" {
  #include <nofrendo.h>
}

// FIX: nesRomPath didefinisikan di nes_osd_renphone.ino, tapi file itu
// baru ke-compile SETELAH file ini (urutan alfabetis Arduino) -- jadi
// perlu di-extern-in di sini biar kekenal duluan.
extern char nesRomPath[160];

static TaskHandle_t nesTaskHandle = nullptr;
static volatile bool nesTaskRunning = false;

// FIX v87 (bug: layar nyangkut selamanya di "Memuat NES..."): cek
// dulu ROM-nya ada apa nggak SEBELUM nyalain task, drpd task mati
// diam2 & layar nyangkut selamanya tanpa pesan apa2.
static bool nesRomError = false;
static String nesRomErrorMsg = "";
static bool nesTaskEverStarted = false;
// nesVideoActive: nesVideo_end() cuma boleh dipanggil kalau nesVideo_begin()
// beneran kepanggil duluan.
static bool nesVideoActive = false;

// v88: ROM PICKER -- scan ROOT SD card, list semua *.nes yg ketemu,
// biar user bisa pilih ROM apa aja tanpa perlu isi 1 nama file hardcode.
#define NES_MAX_ROMS 40
static String nesRomList[NES_MAX_ROMS];
static int    nesRomCount  = 0;
static int    nesListPage  = 0;
static bool   nesShowingPicker = true;
static const int NES_ITEMS_PER_PAGE = 5;

// Dicek tiap frame di osd_getinput() (nes_input_renphone.ino) -- itu
// yg beneran manggil nes_poweroff() supaya loop emulasi berhenti aman
// (dari THREAD/TASK yg sama dgn emulator, bukan dari luar -- penting
// biar gak race condition).
volatile bool nesExitRequested = false;

// Scan ROOT SD card (bukan subfolder) cari semua file *.nes.
// Pola SAMA PERSIS kayak scanSdFiles() punya File Explorer kamu, biar
// konsisten cara SD_MMC.open()-nya (pakai "/" + nama file, BUKAN
// "/sdcard/..." -- SD_MMC di ESP32 udah otomatis anggap "/" = root SD,
// gak perlu prefix mountpoint lagi).
static void nesScanRoms(){
  nesRomCount = 0;
  if(!sdReady) return;
  File root = SD_MMC.open("/");
  if(!root || !root.isDirectory()){ if(root) root.close(); return; }
  File f = root.openNextFile();
  while(f && nesRomCount < NES_MAX_ROMS){
    if(!f.isDirectory()){
      String name = String(f.name());
      int slashIdx = name.lastIndexOf('/');
      if(slashIdx >= 0) name = name.substring(slashIdx+1);
      String lower = name; lower.toLowerCase();
      if(lower.endsWith(".nes")){
        nesRomList[nesRomCount++] = name;
      }
    }
    f = root.openNextFile();
  }
  root.close();
}

// Task terpisah -- WAJIB, karena nofrendo_main() itu BLOCKING (baru
// return kalau game beneran keluar), jadi gak boleh dipanggil langsung
// dari loop() utama (nanti seluruh UI Ren Phone kebekukan total).
static void nesTaskFn(void *param) {
  nesTaskRunning = true;
  nofrendo_main(0, nullptr);  // blocking sampai game keluar
  nesTaskRunning = false;
  vTaskDelete(nullptr);
}

// FIX crash: 12KB stack terlalu mepet buat emulator CPU+PPU+mapper NES
// (rantai pemanggilannya lumayan dalam) -- gampang stack-overflow &
// bikin ESP32 crash/reset random. Dinaikin ke 48KB, dan DITARUH DI
// PSRAM (bukan internal RAM) via xTaskCreateStaticPinnedToCore, biar
// gak rebutan sama canvas/canvasApp/transShot punya UI Ren Phone yang
// udah makan internal RAM lumayan banyak.
#define NES_TASK_STACK_BYTES (48 * 1024)
static StackType_t *nesTaskStack = nullptr;
static StaticTask_t nesTaskTCB;

// Nyalain task emulator utk ROM yg SUDAH dipilih & diisi ke nesRomPath.
static void nesStartGame(){
  nesVideo_begin();  // buka transaksi SPI + bersihkan layar
  nesVideoActive = true;
  nesFpsReset(); // v89: FPS meter mulai ngitung dari nol tiap ROM baru dinyalain

  // Set true DI SINI (bukan cuma di dalam nesTaskFn) supaya gak ada celah
  // race dgn nesWatchdogTick() yg jalan tiap frame.
  nesTaskRunning = true;
  nesTaskEverStarted = true;

  if (!nesTaskStack) {
    nesTaskStack = (StackType_t*) heap_caps_malloc(NES_TASK_STACK_BYTES, MALLOC_CAP_SPIRAM);
  }
  if (!nesTaskStack) {
    // PSRAM gagal dialokasi (harusnya gak terjadi di N16R8) -- fallback
    // ke internal RAM drpd nge-crash total krn stack null.
    xTaskCreatePinnedToCore(nesTaskFn, "nofrendo", NES_TASK_STACK_BYTES,
                             nullptr, 1, &nesTaskHandle, 1);
    return;
  }

  // Pin ke core 1 (UI/touch Ren Phone tetap jalan bebas di core 0).
  nesTaskHandle = xTaskCreateStaticPinnedToCore(
      nesTaskFn, "nofrendo", NES_TASK_STACK_BYTES, nullptr, 1,
      nesTaskStack, &nesTaskTCB, 1);
}

// FIX bug "layar putih bergaris / ESP32 crash pas main NES": dipakai
// loop() utama (phone.ino) buat tau kapan emulator BENERAN pegang layar
// via SPI langsung (scanline blit di core 1), supaya core 0 BERHENTI
// nyentuh display lewat push()/canvas.pushSprite() selama momen itu --
// dua core nulis ke SPI yg sama tanpa lock = korupsi tampilan/hang.
bool nesIsPlaying() {
  return nesTaskRunning && !nesShowingPicker && !nesRomError;
}

void nesEnter() {
  nesRomError = false;
  nesShowingPicker = true;
  nesTaskEverStarted = false;
  nesListPage = 0;

  // FIX: sebelumnya nesEnter() lupa manggil enterGameMode() (beda dari
  // Snake/Flappy/2048/Maze/Inferno) -- akibatnya gameModeActive tetap
  // false selama main NES, jadi battUpdate() di loop() TETAP jalan tiap
  // 5 detik & set needRedraw=true walau lagi idle sekalipun. Itu yang
  // bikin loop utama numpang push() ke SPI di tengah scanline blit-nya
  // nofrendo tanpa lock -> race condition -> layar putih/garis, crash.
  // Efek sampingnya, CPU juga sekalian naik ke 240MHz spt game lain.
  enterGameMode();

  if (!sdReady) {
    nesRomError = true;
    nesShowingPicker = false;
    nesRomErrorMsg = "SD card belum siap/gak kedetek.";
    return;
  }

  nesScanRoms();
  if (nesRomCount == 0) {
    nesRomError = true;
    nesShowingPicker = false;
    nesRomErrorMsg = "Gak ada file .nes ditemukan di root SD card.\n\n"
                      "Copy ROM .nes ke root SD card (bukan di dalam "
                      "folder), terus buka lagi app NES ini.";
    return;
  }
  // nesShowingPicker tetap true -> layar list ROM digambar di drawNes(),
  // task emulator BARU dinyalain sesudah user tap salah satu di nesTouch().
}

void nesExit() {
  // CATATAN JUJUR: nofrendo_main() aslinya didesain jalan sampai user
  // "quit" dari dalam game itu sendiri, bukan di-interupsi dari luar
  // dgn aman. Flag nesExitRequested ini dicek lewat patch 1-baris di
  // nofrendo.c (sudah otomatis diterapkan CI build, lihat workflow).
  extern volatile bool nesExitRequested;
  nesExitRequested = true;

  // Tunggu task-nya beneran selesai sebelum lanjut (max ~2 detik),
  // supaya gak ada 2 pihak (task NES vs UI) yg sama2 nulis ke SPI.
  int waitMs = 0;
  while (nesTaskRunning && waitMs < 2000) { delay(10); waitMs += 10; }

  // Jangan endWrite() kalau belum pernah startWrite() (kasus keluar dari
  // layar picker/error sebelum game sempat dinyalain sama sekali).
  if (nesVideoActive) {
    nesVideo_end();
    nesVideoActive = false;
  }
  nesExitRequested = false; // reset buat sesi main berikutnya
  nesTaskEverStarted = false;
  nesShowingPicker = true; // balik lagi ke layar list kalau masuk NES lagi

  exitGameMode(); // FIX: pasangan enterGameMode() di nesEnter() -- balikin CPU & polling normal
}

// Dipanggil TIAP FRAME dari loop utama (phone.ino) selagi curScreen()==
// SCR_NES -- jaring pengaman kalau task nofrendo SEMPAT jalan normal tapi
// lalu mati sendiri di tengah jalan (ROM corrupt, mapper gak didukung,
// RAM abis, dll). Otomatis balik ke Home drpd nyangkut selamanya.
void nesWatchdogTick() {
  if (nesTaskEverStarted && !nesTaskRunning) {
    nesTaskEverStarted = false; // biar cuma trigger sekali
    navGoHome(); // appOnExit(SCR_NES) di dalamnya bakal manggil nesExit()
  }
}

void drawNes(LGFX_Sprite& s) {
  s.fillScreen(TFT_BLACK);

  if (nesRomError) {
    s.setTextColor(TFT_RED); s.setTextSize(1);
    s.setCursor(8, STATUS_H + 20); s.print("Gagal membuka NES:");
    int maxChars = (SCR_W - 16) / 6;
    int n = aiWrapAppend(aiLinesBuf, AI_MAX_LINES, 0, nesRomErrorMsg, TFT_WHITE, maxChars);
    for (int i = 0; i < n && i < 8; i++) {
      s.setTextColor(TFT_WHITE); s.setCursor(8, STATUS_H + 36 + i * 11);
      s.print(aiLinesBuf[i].text.c_str());
    }
    drawBack(s); // ketuk pojok back utk kembali ke Home
    return;
  }

  if (nesShowingPicker) {
    s.setTextColor(T().accent); s.setTextSize(1);
    s.setCursor(8, 26); s.print("Pilih ROM NES");

    int listY = 44, itemH = 26;
    int startIdx = nesListPage * NES_ITEMS_PER_PAGE;
    for (int i = 0; i < NES_ITEMS_PER_PAGE && (startIdx + i) < nesRomCount; i++) {
      int idx = startIdx + i;
      int itemY = listY + i * (itemH + 4);
      s.fillRoundRect(4, itemY, SCR_W - 8, itemH, 6, T().surface);
      s.setTextColor(T().accent2); s.setCursor(10, itemY + 8); s.print("[N]");
      s.setTextColor(T().text); s.setCursor(32, itemY + 8);
      String fn = nesRomList[idx];
      if (fn.length() > 22) fn = fn.substring(0, 20) + "..";
      s.print(fn.c_str());
    }

    int pageY = backY() - 2;
    if (nesListPage > 0) {
      s.fillRoundRect(SCR_W - 120, pageY, 54, 22, 4, T().surface2);
      s.setTextColor(T().text); s.setCursor(SCR_W - 110, pageY + 6); s.print("< Prev");
    }
    if ((nesListPage + 1) * NES_ITEMS_PER_PAGE < nesRomCount) {
      s.fillRoundRect(SCR_W - 60, pageY, 54, 22, 4, T().surface2);
      s.setTextColor(T().text); s.setCursor(SCR_W - 52, pageY + 6); s.print("Next >");
    }

    drawBack(s);
    return;
  }

  // Cuma kelihatan sekejap pas transisi list->game, sebelum task emulator
  // ambil alih layar langsung lewat SPI.
  s.setTextColor(TFT_WHITE); s.setTextSize(2);
  s.setCursor(SCR_W/2-60, SCR_H/2-10);
  s.print("Memuat NES...");
}

void nesTouch(int tx, int ty, bool held, bool newT) {
  if (nesRomError) {
    if (newT && isBack(tx, ty)) { navBack(); }
    return;
  }

  if (nesShowingPicker) {
    if (!newT) return;
    if (isBack(tx, ty)) { navBack(); return; }

    int listY = 44, itemH = 26;
    int startIdx = nesListPage * NES_ITEMS_PER_PAGE;
    for (int i = 0; i < NES_ITEMS_PER_PAGE && (startIdx + i) < nesRomCount; i++) {
      int idx = startIdx + i;
      int itemY = listY + i * (itemH + 4);
      if (tx >= 4 && tx <= SCR_W - 4 && ty >= itemY && ty <= itemY + itemH) {
        String fn = nesRomList[idx];
        if (!fn.startsWith("/")) fn = "/" + fn;
        fn.toCharArray(nesRomPath, sizeof(nesRomPath));
        nesShowingPicker = false;
        needRedraw = true;
        nesStartGame();
        return;
      }
    }

    int pageY = backY() - 2;
    if (nesListPage > 0 && tx >= SCR_W-120 && tx <= SCR_W-66 && ty >= pageY) {
      nesListPage--; needRedraw = true; return;
    }
    if ((nesListPage+1) * NES_ITEMS_PER_PAGE < nesRomCount && tx >= SCR_W-60 && tx <= SCR_W-6 && ty >= pageY) {
      nesListPage++; needRedraw = true; return;
    }
    return;
  }

  nesInput_touch(tx, ty, held, newT);
}
