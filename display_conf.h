#pragma once
#define RP_STR_(x) #x
#define RP_STR(x)  RP_STR_(x)   // angka pin -> string (utk teks "cek wiring")
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

  // ST7796_DIFFPUSH_V1: push() hanya kirim pita baris yg berubah (lihat apply_st7796_diffpush.py).
  // 0 = push penuh seperti dulu. Layar korup/ada sisa gambar -> coba -DRP_PUSH_DIFF=0 dulu.
  #ifndef RP_PUSH_DIFF
  #define RP_PUSH_DIFF        1
  #endif

  // ---- panel ----
  #ifndef RP_ST7796_INVERT
  #define RP_ST7796_INVERT    1            // default 1 (kebanyakan modul 3,5" ST7796 butuh INVON). Warna negatif/pucat -> ganti 0
  #endif
  #ifndef RP_ST7796_RGB_ORDER
  #define RP_ST7796_RGB_ORDER 0            // merah<->biru tertukar -> ganti 1
  #endif
  #ifndef RP_DISP_SPI_FREQ
  #define RP_DISP_SPI_FREQ    40000000     // fallback first boot (ST7796: mulai 40MHz, naikkan lewat Settings)
  #endif

  // ---- sentuh FT6236 (I2C 0x38) ----
  // Bus ini (Wire, SDA 15 / SCL 7) KHUSUS FT6236. MPU6050/INA219 ada di bus kedua (RP_SENS_*, Wire1) di bawah.
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
  // TOUCH_PROBE_V1: clock I2C (Hz). Sentuh tidak stabil/kabel panjang -> coba 100000 atau 200000.
  #ifndef RP_TOUCH_I2C_HZ
  #define RP_TOUCH_I2C_HZ     400000
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

  // SENS_BUS_V1: bus I2C KEDUA (Wire1) khusus MPU6050 + INA219. Bus 15/7 di atas sekarang
  // eksklusif untuk sentuh FT6236. Pin default = pin SPI sentuh XPT2046 (5/4) yang di build
  // ST7796 tidak dipakai. Wajib ada pull-up 4,7k-10k ke 3V3 (kebanyakan modul MPU6050/INA219 sudah punya).
  // Hindari GPIO 33-37 (PSRAM octal), 26-32 (flash), 19/20 (USB), 0/3/45/46 (strapping).
  #ifndef RP_SENS_SDA
  #define RP_SENS_SDA         5
  #endif
  #ifndef RP_SENS_SCL
  #define RP_SENS_SCL         4
  #endif
  #ifndef RP_SENS_I2C_HZ
  #define RP_SENS_I2C_HZ      400000
  #endif
  #define RP_SENS_WIRE        Wire1
  #if (RP_SENS_SDA==RP_TOUCH_SDA) || (RP_SENS_SDA==RP_TOUCH_SCL) || (RP_SENS_SCL==RP_TOUCH_SDA) || (RP_SENS_SCL==RP_TOUCH_SCL)
    #error "RP_SENS_SDA/SCL tidak boleh sama dengan pin sentuh RP_TOUCH_SDA/SCL (bus terpisah)"
  #endif

  // Multi-touch: FT6236 melaporkan 2 jari. 1 = jari kedua dipakai game (DOOM/NES/Inferno).
  #ifndef RP_MULTITOUCH
  #define RP_MULTITOUCH       1
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
  #define RP_MULTITOUCH       0
  // SENS_BUS_V1: build ILI9341 = perilaku lama (MPU6050 + INA219 di Wire, SDA 15 / SCL 7)
  #ifndef RP_SENS_SDA
  #define RP_SENS_SDA         15
  #endif
  #ifndef RP_SENS_SCL
  #define RP_SENS_SCL         7
  #endif
  #ifndef RP_SENS_I2C_HZ
  #define RP_SENS_I2C_HZ      400000
  #endif
  #define RP_SENS_WIRE        Wire
  #define RP_CH_SUFFIX        ""
  #ifndef RP_PUSH_DIFF
  #define RP_PUSH_DIFF        0            // ST7796_DIFFPUSH_V1: ILI9341 = perilaku lama
  #endif
  #define RP_DISP_SPI_FREQ    60000000
  #define RP_DOOM_PANEL_W     320
  #define RP_DOOM_PANEL_H     240
  #define RP_DOOM_VIEW_X      0
  #define RP_DOOM_VIEW_Y      20
  #define RP_INF_MAX_COLS     320
#endif
