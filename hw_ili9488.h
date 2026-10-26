#pragma once
// ============================================================
//  hw_ili9488.h -- konfigurasi layar 3,5" ILI9488 (480x320) + touch FT6236
//  Branch: ili9488-ft6236
//
//  Pin layar SAMA dengan versi ILI9341 (tidak perlu kabel ulang):
//    SCLK 12 | MOSI 11 | MISO 13 | DC 2 | CS 10 | RST 14 | BL 21
//  Touch XPT2046 (GPIO 6/5/4/9) DIBUANG. FT6236 pakai I2C:
//    SDA/SCL  = bus I2C yang sama dgn MPU6050 + INA219 (15 / 7), alamat 0x38
//    T_RST    = GPIO 5  (bekas MOSI XPT2046)
//    T_INT    = GPIO 4  (bekas MISO XPT2046; opsional, tidak dipakai polling)
//  Ubah nilai di bawah kalau kabelmu beda.
// ============================================================

// ---- panel (ukuran NATIVE, portrait) ----
#define LCD_NATIVE_W   320
#define LCD_NATIVE_H   480
#define LCD_INVERT     false   // true kalau warna seperti negatif
#define LCD_RGB_ORDER  false   // true kalau merah & biru tertukar

// ---- touch FT6236 / FT6336 (I2C) ----
#define TP_I2C_ADDR    0x38
#define TP_SDA_PIN     15
#define TP_SCL_PIN     7
#define TP_RST_PIN     5       // -1 kalau T_RST tidak disambung
#define TP_INT_PIN     4       // -1 kalau T_INT tidak disambung
#define TP_I2C_HZ      400000

// Orientasi modul touch terhadap panel (dalam koordinat NATIVE, rotasi 0).
// Kalau sentuhan terbalik/tertukar, ubah 3 nilai ini (0/1) lalu build ulang.
#define TP_SWAP_XY     0
#define TP_FLIP_X      0
#define TP_FLIP_Y      0

#define TP_POLL_MS     5       // jeda minimum antar pembacaan I2C
#define TP_MULTITOUCH  1       // 1 = baca 2 jari (FT6236/FT6336). 0 = hanya 1 jari (kalau modulmu sering ada ghost touch jari ke-2)
#define TP_DEBUG_RAW   0       // 1 = cetak koordinat mentah ke Serial

// ---- accretion-v2 ----
// Ambang sentuh bawaan chip (reg 0x80): makin KECIL makin sensitif. Level bisa diganti dari
// Pengaturan (baris "Sensitivitas Sentuh": Normal / Tinggi / Rendah) & tersimpan di NVS.
#define TP_THR_NORMAL  22
#define TP_THR_HIGH    16      // sensitif (mis. pakai pelindung layar tebal / jari kering)
#define TP_THR_LOW     32      // kurang sensitif (ghost touch / tetesan air)
// 1 = lewati baca I2C selagi tidak ada jari DAN pin T_INT (TP_INT_PIN) HIGH. Hemat bus I2C & CPU,
// tapi HANYA aktifkan kalau T_INT benar-benar tersambung ke ESP32 (kalau tidak, sentuhan tidak terbaca!).
#define TP_INT_SKIP    0
