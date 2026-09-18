#pragma once

// hw_conf.h -- dibaca OTOMATIS oleh DoomGlue.cpp (library ESP-DOOM) lewat
// __has_include("hw_conf.h"). File ini WAJIB ada di folder sketch yang sama
// dengan phone.ino (bukan di dalam folder library), sama seperti pola tiap
// contoh resmi ESP-DOOM (examples/Minimal, examples/CanvasDisplay, dst).
//
// CATATAN PENTING (baca sebelum ubah-ubah):
// - Pin LCD/SD/tombol fisik yg biasanya didefinisikan di sini (DOOM_PIN_*)
//   SENGAJA TIDAK dipakai: ren_phone sudah punya objek `display` (LGFX)
//   sendiri yg dikonfigurasi & di-init di phone.ino (SPI2_HOST, pin sclk=12
//   mosi=11 miso=13 dc=2 cs=10 rst=14). doom_renphone.ino membungkus objek
//   `display` itu langsung (bukan bikin bus SPI baru), jadi DOOM_PIN_LCD_*
//   tidak relevan sama sekali di sini.
// - Input juga BUKAN 8 tombol fisik (SimpleButtons) spt contoh resmi --
//   ren_phone cuma punya 1 titik sentuh (XPT2046), jadi input DOOM
//   diimplementasikan sbg kontrol virtual (joystick kiri + drag-look kanan +
//   tombol TEMBAK/USE/PAUSE/KELUAR) di doom_renphone.ino, pola SAMA PERSIS
//   dgn skema layar-sentuh game "Inferno" yg sudah ada di phone.ino.
// - Audio TIDAK dipakai (permintaan user, project non-audio) -- lihat
//   DOOM_NO_AUDIO di bawah. DoomSound.cpp/DoomSound.h dari library boleh
//   tetap ikut ke-copy di folder src/, tapi tidak akan pernah dipanggil.

// --- Panel & viewport -------------------------------------------------
// ILI9341 320x240 fisik. DOOM dipaksa jalan di ORIENTASI LANDSCAPE selagi
// app ini aktif (doomEnter() menyimpan orientasi lama & memaksa landscape,
// doomExit() mengembalikannya) -- doomgeneric_Create() cuma dipanggil SEKALI
// per sesi app, jadi ukuran panel di bawah ini HARUS konstan compile-time,
// tidak bisa ikut berubah kalau user gonta-ganti orientasi Setting seperti
// app lain (lihat catatan panjang soal ini di doom_renphone.ino).
#define DOOM_PANEL_W       320
#define DOOM_PANEL_H       240

// DOOM asli selalu render di 320x200 (rasio 320:200 = 8:5). Cuma
// DOOM_VIEWPORT_W yg diisi (mode FILL) -> tinggi konten otomatis mengikuti
// rasio itu (200px), lalu DOOM_VIEWPORT_Y menaruhnya di TENGAH layar
// 240px (240-200=40 -> 20px sisa strip hitam di atas & bawah, bukan bug).
#define DOOM_VIEWPORT_W    320
#define DOOM_VIEWPORT_X    0
#define DOOM_VIEWPORT_Y    20

// --- Audio: DIMATIKAN (permintaan user, project non-audio) ------------
#define DOOM_NO_AUDIO

// --- WAD ---
// WAD TIDAK disertakan di sini (isu hak cipta) -- user wajib menaruh file
// WAD miliknya sendiri (mis. doom1.wad shareware, atau WAD retail yg
// dibeli) persis di path ini di kartu SD (root, via SD_MMC yg sudah dipakai
// ren_phone). doomEnter() di doom_renphone.ino akan menampilkan toast error
// yg jelas kalau file ini tidak ditemukan -- tidak bikin hang/crash.
#define DOOM_WAD_PATH      "/doom1.wad"
