// =================================================================
// nes_video_renphone.cpp
// Jembatan video: Nofrendo (fork embedded, render per-scanline) -> LGFX
// (LovyanGFX) yang dipakai Ren Phone.
//
// CARA PAKAI:
//  1. Taruh file ini di folder project Ren Phone kamu, sejajar sama file
//     .ino utama (Arduino IDE akan compile semua .cpp di folder sketch).
//  2. Ganti "extern LGFX display;" di bawah kalau nama kelasnya beda dari
//     yang ada di .ino kamu (punya kamu namanya persis "LGFX display;").
//  3. Sebelum manggil nofrendo_main()/nes_insertcart() di app SCR_NES,
//     panggil nesVideo_begin() dulu (buka transaksi SPI sekali di awal,
//     bukan tiap scanline -- ini yang bikin ini jauh lebih cepat drpd
//     wrap startWrite()/endWrite() di tiap baris).
//  4. Pas keluar dari app NES (balik ke Home dll), panggil nesVideo_end().
//
// Fungsi-fungsi di bawah ini (osd_getvideoinfo, vid_setpalette,
// ppu_scanline_blit) adalah fungsi yang DIPANGGIL LANGSUNG oleh core
// nofrendo kamu (lihat nes.c baris ~334 & nofrendo.c) -- jangan diubah
// nama/signature-nya, itu kontrak dari core, bukan pilihan bebas.
// =================================================================

#include <Arduino.h>

extern "C" {
  #include <noftypes.h>
  #include <osd.h>
  #include <vid_drv.h>
  #include <bitmap.h>
  #include <nes.h>   // NES_SCREEN_WIDTH, NES_VISIBLE_HEIGHT
}

// CATATAN: file ini disimpan sbg TAB .ino baru (bukan .cpp berdiri
// sendiri) di folder sketch yang sama dgn Ren Phone. Arduino IDE
// otomatis GABUNG semua tab .ino jadi satu file sebelum compile, jadi
// "display", "SCR_W", "SCR_H" dari tab utama kamu OTOMATIS kelihatan
// di sini -- TIDAK perlu #include LovyanGFX.hpp atau extern apapun lagi.

// Dideklarasikan di nes_input_renphone.ino (file berikutnya) -- dipanggil
// di akhir tiap frame supaya overlay dpad/tombol touch selalu kegambar
// ulang di atas frame NES yg baru saja di-push.
void nesOverlay_afterFrame();

// -----------------------------------------------------------------
// Palet 256 warna NES -> RGB565, di-convert sekali tiap kali PPU
// minta ganti palet (biasanya cuma sekali di awal permainan).
// -----------------------------------------------------------------
static uint16_t nesPalette[256];

extern "C" void vid_setpalette(rgb_t *pal) {
  for (int i = 0; i < 256; i++) {
    // format 565, tapi channel R & B DITUKAR: panel kamu dikonfig
    // cfg.rgb_order=false (BGR) di LGFX config phone.ino. Fungsi
    // tinggi kayak fillRect()/canvas.pushSprite() otomatis nyesuaiin
    // urutan ini, tapi pushImage() buffer mentah kayak di bawah TIDAK
    // -- makanya warnanya kebalik cuma di NES doang.
    nesPalette[i] = ((pal[i].b & 0xF8) << 8) |
                    ((pal[i].g & 0xFC) << 3) |
                    ((pal[i].r) >> 3);
  }
}

// -----------------------------------------------------------------
// osd_getvideoinfo: dipanggil sekali di awal (main_loop di nofrendo.c).
// Di fork ini vid_init/vid_setmode sudah di-stub no-op (lihat stubs.c
// bawaan nofrendo kamu), jadi driver di bawah ini praktis gak dipanggil
// -- yang beneran dipakai adalah ppu_scanline_blit() di bawah. Tetap
// diisi wajar biar gak ada dangling pointer / undefined behavior.
// -----------------------------------------------------------------
static viddriver_t renphone_driver = {
  "renphone_lgfx",  // name
  NULL, NULL, NULL, // init, shutdown, set_mode (tidak dipakai fork ini)
  NULL,             // set_palette (kita override lewat vid_setpalette langsung)
  NULL,             // clear
  NULL, NULL, NULL, // lock_write, free_write, custom_blit (tidak dipakai)
  false
};

extern "C" void osd_getvideoinfo(vidinfo_t *info) {
  info->default_width  = NES_SCREEN_WIDTH;   // 256
  info->default_height = NES_VISIBLE_HEIGHT; // 224
  info->driver = &renphone_driver;
}

// -----------------------------------------------------------------
// Offset supaya frame NES 256x224 ditengahkan di layar Ren Phone
// (320x240 landscape). Kalau kamu mau ganti orientasi/ukuran window
// game, ubah dua baris ini saja.
// -----------------------------------------------------------------
static int nesOffsetX = 0;
static int nesOffsetY = 0;

// Buffer satu baris scanline (dipakai ulang tiap panggilan, tidak
// dialokasikan tiap frame -- penting utk RAM & kecepatan).
static uint16_t lineBuf[NES_SCREEN_WIDTH];

void nesVideo_begin() {
  nesOffsetX = (SCR_W - NES_SCREEN_WIDTH)  / 2;
  nesOffsetY = (SCR_H - NES_VISIBLE_HEIGHT) / 2;
  display.startWrite();     // buka transaksi SPI SEKALI (bukan per-scanline)
  display.fillScreen(TFT_BLACK);
}

void nesVideo_end() {
  display.endWrite();       // tutup transaksi SPI saat keluar dari app NES
}

// -----------------------------------------------------------------
// ppu_scanline_blit: DIPANGGIL LANGSUNG oleh core nofrendo (nes.c),
// SATU KALI PER SCANLINE selagi frame di-render (bukan per-frame
// penuh) -- ini yang bikin RAM irit, gak perlu framebuffer 256x224.
//   bmp         : array index-warna (0-255) sepanjang NES_SCREEN_WIDTH
//                 untuk scanline ini (offset +8 sudah pola dari fork asli,
//                 lihat display.c contoh: bmp += 8;)
//   scanline    : nomor baris NES yang lagi dirender (0..~262)
//   draw_flag   : false kalau baris ini di luar area visible / frame-skip
// -----------------------------------------------------------------
extern "C" void ppu_scanline_blit(uint8_t *bmp, int scanline, bool draw_flag) {
  if (!draw_flag) return;
  if (scanline < 0 || scanline >= NES_VISIBLE_HEIGHT) return;

  bmp += 8; // sama seperti fork referensi: 8 pixel pertama border, dibuang

  for (int x = 0; x < NES_SCREEN_WIDTH; x++) {
    lineBuf[x] = nesPalette[bmp[x]];
  }

  // pushImage 1 baris tinggi -- transaksi SPI SUDAH dibuka di nesVideo_begin(),
  // jadi ini cuma nge-stream data, gak buka/tutup transaksi tiap panggilan.
  display.pushImage(nesOffsetX, nesOffsetY + scanline,
                     NES_SCREEN_WIDTH, 1, lineBuf);

  // Scanline terakhir yg visible -> frame ini selesai di-push semua.
  // Gambar ulang overlay dpad/tombol touch di sini, SEKALI per frame
  // (bukan tiap scanline), supaya overlay gak "kemakan" render NES tapi
  // juga gak nambah overhead di 223 baris lainnya.
  if (scanline == NES_VISIBLE_HEIGHT - 1) {
    nesOverlay_afterFrame();
    // FIX crash: main_loop() nofrendo gak pernah vTaskDelay/yield --
    // kalau dibiarin, idle task core 1 gak kebagian jatah CPU sama
    // sekali & Task Watchdog Timer bakal panic/reset seluruh ESP32.
    // 1 tick di sini (sekali per FRAME, bukan per scanline) cukup buat
    // "ngasih napas" ke scheduler tanpa kerasa nge-lag gamenya.
    vTaskDelay(1);
  }
}
