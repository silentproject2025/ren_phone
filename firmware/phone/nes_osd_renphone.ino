// =================================================================
// nes_osd_renphone.ino
// Fungsi-fungsi "wajib ada" yang diminta core nofrendo (osd.h), yang
// nilainya sepele/gak butuh hardware khusus Ren Phone -- termasuk juga
// loader ROM dari SD_MMC (karena fork nofrendo ini baca ROM lewat SATU
// pointer buffer penuh, bukan fopen/fread per-bank, lihat komentar di
// osd_getromdata() di bawah).
//
// TAB INI JUGA .ino -- taruh sejajar sama file lain, gak perlu extern.
// =================================================================

extern "C" {
  #include <noftypes.h>
  #include <osd.h>
  #include <nofrendo.h>
  #include <log.h>
}

// -----------------------------------------------------------------
// Path ROM yang lagi mau dimainkan. Diisi oleh nes_app_renphone.ino
// (nesEnter()) SEBELUM task nofrendo_main() di-start -- misal dari
// pilihan user di File Explorer, atau hardcode dulu buat testing.
// -----------------------------------------------------------------
char nesRomPath[160] = "/sdcard/roms/game.nes"; // <- GANTI/atur dari File Explorer

// -----------------------------------------------------------------
// osd_main: dipanggil dari dalam nofrendo_main(). Fork aslinya manggil
// main_loop("builtin", system_autodetect) -- kita ganti "builtin" jadi
// path ROM asli kita, dan system_nes (skip auto-detect, kita udah tau
// ini NES).
// -----------------------------------------------------------------
extern "C" int osd_main(int argc, char *argv[]) {
  return main_loop(nesRomPath, system_nes);
}

extern "C" int osd_init(void)     { return 0; }
extern "C" void osd_shutdown(void) { }
extern "C" void osd_getmouse(int *x, int *y, int *button) { }

// -----------------------------------------------------------------
// Timer virtual: fork ini (lihat timing.c aslinya) TIDAK pakai timer
// interrupt beneran, cuma itung "tick" dari waktu berjalan / frekuensi
// target (60Hz NES). Kita ganti sumber waktunya ke millis() Arduino.
// -----------------------------------------------------------------
static int nesTimerFreq = 60;

extern "C" int osd_installtimer(int frequency, void *func, int funcsize,
                                 void *counter, int countersize) {
  nesTimerFreq = frequency;
  return 0;
}

extern "C" int osd_nofrendo_ticks(void) {
  return millis() / (1000 / nesTimerFreq);
}

// -----------------------------------------------------------------
// Filename helpers -- disalin apa adanya dari referensi nofrendo,
// gak ada yg perlu diubah utk ESP32.
// -----------------------------------------------------------------
extern "C" void osd_fullname(char *fullname, const char *shortname) {
  strncpy(fullname, shortname, PATH_MAX);
}

extern "C" char *osd_newextension(char *string, char *ext) {
  int l = strlen(string);
  while (l && string[l] != '.') l--;
  if (l) string[l] = 0;
  strcat(string, ext);
  return string;
}

extern "C" int osd_makesnapname(char *filename, int len) {
  return -1; // fitur snapshot PCX dimatikan, gak dipakai
}

// -----------------------------------------------------------------
// osd_getromdata() / osd_unloadromdata()
// PENTING: fork nofrendo ini baca ROM dgn cara BEDA dari nofrendo biasa
// -- rom_load() di nes_rom.c minta SATU pointer ke ISI FILE .nes PENUH
// di RAM (rominfo->rom nantinya nunjuk LANGSUNG ke tengah buffer ini,
// tanpa disalin lagi -- lihat rom_loadrom() di nes_rom.c). Makanya di
// sini kita HARUS baca seluruh file dari SD ke satu buffer malloc,
// bukan cuma buka FILE* biasa.
//
// Buffer ini WAJIB tetap hidup selama game jalan (jangan di-free
// sebelum nesExit()!), makanya disimpan di variabel static di luar
// fungsi supaya nempel sampai osd_unloadromdata() dipanggil manual.
// -----------------------------------------------------------------
static uint8_t *nesRomBuffer = nullptr;

extern "C" const char *osd_getromdata(const char *name) {
  // name di sini sebenarnya path yg sama dgn nesRomPath (diteruskan
  // apa adanya oleh rom_load), tapi kita pakai nesRomPath langsung
  // biar konsisten sama yg di-set nesEnter().
  if (!sdReady) {                       // 'sdReady' sudah ada di .ino utama kamu
    log_printf("NES: SD belum siap\n");
    return nullptr;
  }

  File f = SD_MMC.open(nesRomPath, FILE_READ);
  if (!f) {
    log_printf("NES: gagal buka ROM %s\n", nesRomPath);
    return nullptr;
  }

  size_t sz = f.size();

  // Prioritaskan PSRAM kalau ada (game MMC1/MMC3 ukurannya bisa
  // ratusan KB) -- fallback ke heap biasa kalau PSRAM gak tersedia
  // (cukup utk game NROM kecil, ~32-40KB).
  if (psramFound()) {
    nesRomBuffer = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  } else {
    nesRomBuffer = (uint8_t*)malloc(sz);
  }

  if (!nesRomBuffer) {
    log_printf("NES: RAM kurang buat ROM %u byte\n", (unsigned)sz);
    f.close();
    return nullptr;
  }

  f.read(nesRomBuffer, sz);
  f.close();
  return (const char*)nesRomBuffer;
}

extern "C" void osd_unloadromdata() {
  if (nesRomBuffer) { free(nesRomBuffer); nesRomBuffer = nullptr; }
}
