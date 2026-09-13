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
  nesVideo_begin();  // buka transaksi SPI + bersihkan layar (lihat file video)

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

  nesVideo_end();
  nesExitRequested = false; // reset buat sesi main berikutnya
}

// drawNes(): cuma kepanggil SEKALI pas transisi Home->NES (sebelum
// task emulator jalan & ambil alih layar langsung) -- jadi cukup buat
// layar "Memuat..." simpel, gak perlu dianimasikan.
void drawNes(LGFX_Sprite& s) {
  s.fillScreen(TFT_BLACK);
  s.setTextColor(TFT_WHITE); s.setTextSize(2);
  s.setCursor(SCR_W/2-60, SCR_H/2-10);
  s.print("Memuat NES...");
}

void nesTouch(int tx, int ty, bool held, bool newT) {
  nesInput_touch(tx, ty, held, newT);
}
