#pragma once
// display_conf.h -- PROFIL LAYAR. Dibaca phone.ino, hw_conf.h, about_renphone.ino.
//
// Default (tanpa flag apa pun) = ILI9341 240x320 + sentuh resistif XPT2046
// -> perilaku LAMA, tidak berubah sama sekali.
//
// Build layar 3,5" : tambahkan  -DRP_DISPLAY_ST7796  ke compiler.cpp.extra_flags
// (sudah dilakukan otomatis oleh job "st7796" di .github/workflows/build-firmware.yml).
//
// Semua nilai di bawah bisa ditimpa lewat -D... tanpa mengedit file ini.

#if defined(RP_DISPLAY_ST7796)
  #define RP_DISP_ID          "st7796"
  #define RP_DISP_NAME        "ST7796 480x320, sentuh FT6236"
  #define RP_DISP_NATIVE_W    320          // lebar panel pada rotasi 0 (portrait)
  #define RP_DISP_NATIVE_H    480
  #define RP_TOUCH_CAPACITIVE 1            // FT6236: tidak perlu wizard kalibrasi
  #define RP_CH_SUFFIX        "-st7796"    // channel OTA terpisah: channel-<branch>-st7796

  // ---- panel ----
  #ifndef RP_ST7796_INVERT
  #define RP_ST7796_INVERT    0            // layar warnanya negatif/pucat -> ganti 1
  #endif
  #ifndef RP_ST7796_RGB_ORDER
  #define RP_ST7796_RGB_ORDER 0            // merah<->biru tertukar -> ganti 1
  #endif
  #ifndef RP_DISP_SPI_FREQ
  #define RP_DISP_SPI_FREQ    40000000     // fallback first boot (ST7796: mulai 40MHz, naikkan lewat Settings)
  #endif

  // ---- sentuh FT6236 (I2C 0x38) ----
  // Default: SATU bus dengan MPU6050 (SDA 15 / SCL 7) lewat Wire -> tanpa pin baru.
  // RST/INT = -1 artinya tidak dipakai (INT tidak perlu, sentuh di-polling).
  #ifndef RP_TOUCH_SDA
  #define RP_TOUCH_SDA        15
  #endif
  #ifndef RP_TOUCH_SCL
  #define RP_TOUCH_SCL        7
  #endif
  #ifndef RP_TOUCH_RST
  #define RP_TOUCH_RST        -1
  #endif
  #ifndef RP_TOUCH_ADDR
  #define RP_TOUCH_ADDR       0x38
  #endif
  // Kalau sentuhan terbalik/tertukar setelah dites di layar asli, ubah salah satu saja:
  #ifndef RP_TOUCH_SWAP_XY
  #define RP_TOUCH_SWAP_XY    0            // 1 = tukar sumbu X<->Y (setelah rotasi)
  #endif
  #ifndef RP_TOUCH_FLIP_X
  #define RP_TOUCH_FLIP_X     0            // 1 = balik sumbu X
  #endif
  #ifndef RP_TOUCH_FLIP_Y
  #define RP_TOUCH_FLIP_Y     0            // 1 = balik sumbu Y
  #endif

  // DOOM: render 1:1 (320x200) di tengah layar 480x320, tanpa scaling.
  #define RP_DOOM_PANEL_W     480
  #define RP_DOOM_PANEL_H     320
  #define RP_DOOM_VIEW_X      80
  #define RP_DOOM_VIEW_Y      60
  // Inferno: lebar maksimum kolom raycaster
  #define RP_INF_MAX_COLS     480
#else
  #define RP_DISP_ID          "ili9341"
  #define RP_DISP_NAME        "ILI9341 320x240, sentuh XPT2046"
  #define RP_DISP_NATIVE_W    240
  #define RP_DISP_NATIVE_H    320
  #define RP_TOUCH_CAPACITIVE 0
  #define RP_CH_SUFFIX        ""
  #define RP_DISP_SPI_FREQ    60000000
  #define RP_DOOM_PANEL_W     320
  #define RP_DOOM_PANEL_H     240
  #define RP_DOOM_VIEW_X      0
  #define RP_DOOM_VIEW_Y      20
  #define RP_INF_MAX_COLS     320
#endif
