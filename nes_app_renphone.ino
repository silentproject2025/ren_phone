// =================================================================
// nes_app_renphone.ino
// "App" SCR_NES itu sendiri: nyalain/matiin task emulator, dan 3 fungsi
// standar (nesEnter/nesExit/drawNes/nesTouch) pola SAMA PERSIS kayak
// Snake/Flappy/dkk di firmware kamu.
// =================================================================

extern "C" {
  #include <nofrendo.h>
}

static TaskHandle_t nesTaskHandle = nullptr;
static volatile bool nesTaskRunning = false;

// FIX v87 (bug: layar nyangkut selamanya di "Memuat NES..."): dulu
// nesEnter() langsung nyalain task tanpa ngecek ROM-nya ada apa nggak.
// Kalau file gak ketemu/SD belum siap, task-nya mati DIAM-DIAM (cuma
// log_printf ke Serial) begitu balik dari main_loop() -- tapi layar
// gak pernah digambar ulang (drawNes cuma sekali) jadi teks loading-nya
// nyangkut selamanya, padahal ESP32-nya sendiri baik2 aja (makanya
// neopixel dkk tetap jalan normal).
//
// nesRomError/nesRomErrorMsg: dicek DULU sebelum task dinyalain.
// nesTaskEverStarted: dicek tiap frame lewat nesWatchdogTick() (dipanggil
// dari loop utama di phone.ino) -- kalau task SEMPAT jalan tapi lalu mati
// sendiri (ROM corrupt/mapper gak didukung/RAM kurang di tengah jalan),
// ini yg narik kita otomatis balik ke Home drpd nyangkut selamanya lagi.
static bool nesRomError = false;
static String nesRomErrorMsg = "";
static bool nesTaskEverStarted = false;
// nesVideoActive: nesVideo_end() cuma boleh dipanggil kalau nesVideo_begin()
// beneran kepanggil duluan (kalau nesEnter() gagal di pre-check ROM, transaksi
// SPI-nya belum dibuka sama sekali -- endWrite() tanpa startWrite() bisa bikin
// state SPI berantakan).
static bool nesVideoActive = false;

// Dicek tiap frame di osd_getinput() (nes_input_renphone.ino) -- itu
// yg beneran manggil nes_poweroff() supaya loop emulasi berhenti aman
// (dari THREAD/TASK yg sama dgn emulator, bukan dari luar -- penting
// biar gak race condition).
volatile bool nesExitRequested = false;

// Task terpisah -- WAJIB, karena nofrendo_main() itu BLOCKING (baru
// return kalau game beneran keluar), jadi gak boleh dipanggil langsung
// dari loop() utama (nanti seluruh UI Ren Phone kebekukan total).
static void nesTaskFn(void *param) {
  nesTaskRunning = true;
  nofrendo_main(0, nullptr);  // blocking sampai game keluar
  nesTaskRunning = false;
  vTaskDelete(nullptr);
}

void nesEnter() {
  // nesRomPath (di nes_osd_renphone.ino) HARUS sudah diisi SEBELUM ini
  // -- misal dari hasil pilih file di File Explorer kamu. Untuk testing
  // awal, biarin default "/sdcard/roms/game.nes" dulu.
  nesRomError = false;
  nesTaskEverStarted = false;

  // FIX v87: cek dulu SD siap & filenya BENERAN ada, sebelum nyalain apa2.
  // Kalau enggak, jangan buka transaksi video / nyalain task sama sekali --
  // cukup tampilkan pesan error yg jelas di drawNes() (lihat di bawah).
  if (!sdReady) {
    nesRomError = true;
    nesRomErrorMsg = "SD card belum siap/gak kedetek.";
    return;
  }
  if (!SD_MMC.exists(nesRomPath)) {
    nesRomError = true;
    nesRomErrorMsg = String("File ROM gak ketemu:\n") + nesRomPath +
                     "\n\nCopy dulu file .nes ke path ini lewat Files, "
                     "atau pilih ROM lain di File Explorer.";
    return;
  }

  nesVideo_begin();  // buka transaksi SPI + bersihkan layar (lihat file video)
  nesVideoActive = true;

  // Set true DI SINI (bukan cuma di dalam nesTaskFn) supaya gak ada celah
  // race: kalau nesWatchdogTick() sempat jalan tepat sesudah task dibuat
  // tapi SEBELUM baris pertama nesTaskFn() sempat dieksekusi, dia gak boleh
  // salah kira task-nya udah mati.
  nesTaskRunning = true;
  nesTaskEverStarted = true;

  // Pin ke core 1 (UI/touch Ren Phone tetap jalan bebas di core 0).
  xTaskCreatePinnedToCore(nesTaskFn, "nofrendo", 12288, nullptr, 1,
                           &nesTaskHandle, 1);
}

void nesExit() {
  // CATATAN JUJUR: nofrendo_main() aslinya didesain jalan sampai user
  // "quit" dari dalam game itu sendiri (lewat event_quit), bukan
  // di-interupsi dari luar dgn aman. Utk keluar bersih ke Home tanpa
  // reboot, kamu perlu tambahan KECIL di file nofrendo.c milik core:
  // di dalam main_loop(), ubah kondisi while-nya jadi juga mengecek
  // flag ini:
  //     extern volatile bool nesExitRequested;
  //     while (false == console.quit && !nesExitRequested) { ... }
  // (satu baris tambahan -- lihat penjelasan lengkap di chat)
  extern volatile bool nesExitRequested;
  nesExitRequested = true;

  // Tunggu task-nya beneran selesai sebelum lanjut (max ~2 detik),
  // supaya gak ada 2 pihak (task NES vs UI) yg sama2 nulis ke SPI.
  int waitMs = 0;
  while (nesTaskRunning && waitMs < 2000) { delay(10); waitMs += 10; }

  // FIX v87: jangan endWrite() kalau belum pernah startWrite() (kasus
  // nesEnter() gagal di pre-check ROM & langsung return lebih awal).
  if (nesVideoActive) {
    nesVideo_end();
    nesVideoActive = false;
  }
  nesExitRequested = false; // reset buat sesi main berikutnya
  nesTaskEverStarted = false;
}

// FIX v87: dipanggil TIAP FRAME dari loop utama (phone.ino) selagi
// curScreen()==SCR_NES -- ini "jaring pengaman" kalau task nofrendo
// SEMPAT jalan normal tapi lalu mati sendiri di tengah jalan (ROM
// corrupt, mapper gak didukung, RAM abis pas alokasi ROM gede, dll).
// Tanpa ini, layar bakal nyangkut lagi persis kayak bug awal -- cuma
// bedanya sempat kelihatan game jalan sebentar dulu.
void nesWatchdogTick() {
  if (nesTaskEverStarted && !nesTaskRunning) {
    nesTaskEverStarted = false; // biar cuma trigger sekali
    navGoHome(); // appOnExit(SCR_NES) di dalamnya bakal manggil nesExit()
  }
}

// drawNes(): pas kondisi normal cuma kepanggil SEKALI pas transisi
// Home->NES (sebelum task emulator jalan & ambil alih layar langsung) --
// jadi cukup buat layar "Memuat..." simpel, gak perlu dianimasikan.
// Kalau ROM gagal dimuat (nesRomError), fungsi ini malah TERUS dipanggil
// ulang tiap frame (task emulatornya emang gak pernah dinyalain), jadi
// aman dipakai buat layar error + tombol kembali.
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
    drawBack(s); // ketuk pojok back utk kembali ke Home (lihat nesTouch)
    return;
  }
  s.setTextColor(TFT_WHITE); s.setTextSize(2);
  s.setCursor(SCR_W/2-60, SCR_H/2-10);
  s.print("Memuat NES...");
}

void nesTouch(int tx, int ty, bool held, bool newT) {
  if (nesRomError) {
    if (newT && isBack(tx, ty)) { navBack(); }
    return;
  }
  nesInput_touch(tx, ty, held, newT);
}
