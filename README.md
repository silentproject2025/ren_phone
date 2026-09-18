# 📱 ren_phone — HP DIY dari Nol, Ditenagai ESP32-S3

> **"Kenapa beli HP, kalau bisa dibikin sendiri?"**

**ren_phone** adalah sistem operasi HP custom yang ditulis dari nol — bukan modif Android, bukan port sistem lain — murni satu file firmware C++ raksasa yang berjalan di atas chip **ESP32-S3**. Ia punya lock screen, launcher, status bar, Control Center, notifikasi mengambang ala Dynamic Island, sampai AI assistant yang bisa diajak ngobrol pakai suara. Semuanya nyala di layar TFT kecil seharga recehan dibanding HP beneran.

Sistemnya dinamai **NyxOS**, dengan boot animation bertema *"Accretion"* — ratusan partikel yang berputar mengorbit, lalu perlahan menyatu membentuk tulisan sebelum melebur jadi logo. Bukan sekadar splash screen "Loading...".

Proyek ini juga unik karena **seluruh riwayat perubahannya** — puluhan iterasi, dari v9 sampai versi terbaru — didokumentasikan langsung di dalam kode: apa bug-nya, apa akar masalahnya, kenapa fix-nya begitu. Jadi firmware ini sekaligus jadi semacam studi kasus panjang soal ngoprek ESP32 sampai ke akar-akarnya (fragmentasi RAM internal, timing SPI, kalibrasi sensor, dan sejenisnya).

---

## ✨ Kenapa Ini Menarik

- 🧠 **Ada AI di dalam HP-nya sendiri.** Chat teks maupun ngobrol suara penuh (rekam → Gemini paham & jawab → dijawab balik pakai suara TTS) — semua lewat Google Gemini API.
- 🚀 **Terhubung ke NASA.** Buka foto astronomi harian, lacak asteroid yang mendekati Bumi, lihat foto terbaru dari rover di Mars, sampai citra satelit Bumi — langsung dari genggaman.
- 🎮 **6 game bawaan**, termasuk klon Pac-Man lengkap dengan sistem level & rekor skor yang tersimpan permanen.
- 🎵 **Pemutar MP3 & video AVI** — muter musik di background sambil buka app lain, persis HP beneran.
- 🌐 **Web server built-in.** Colok WiFi, buka browser di laptop, langsung bisa upload file, ganti wallpaper, atau atur API key tanpa sentuh HP-nya sama sekali.
- 🔧 **Update firmware OTA** — tinggal taruh file `.bin` di kartu SD atau kasih link URL, tanpa colok kabel USB.
- 💡 **Kontrol hardware nyata**: LED RGB, motor getar dengan pola berbeda tiap notifikasi, sensor gerak buat auto-rotate & shake-to-home, sampai meteran level mic real-time buat ngecek wiring.
- 🩺 **Ada Task Manager-nya sendiri** (HWmonitor) — bisa lihat beban tiap core CPU, sisa RAM/PSRAM, bahkan tombol tes speaker.

---

## 📚 Daftar Isi

- [Galeri Aplikasi](#️-galeri-aplikasi)
- [Yang Perlu Disiapkan](#️-yang-perlu-disiapkan)
- [Peta Kabel (Wiring)](#-peta-kabel-wiring)
- [Software & Library](#-software--library)
- [Cara Bangun Sendiri](#-cara-bangun-sendiri)
- [Setel API Key](#-setel-api-key)
- [Struktur Kartu SD](#️-struktur-kartu-sd)
- [Web File Manager](#-web-file-manager)
- [Gesture & Kontrol](#-gesture--kontrol)
- [Format Video Kustom](#-format-video-kustom)
- [Hal-Hal yang Perlu Diketahui](#️-hal-hal-yang-perlu-diketahui)
- [Lisensi](#-lisensi)

---

## 🖼️ Galeri Aplikasi

29 aplikasi berjalan di satu launcher grid dengan ikon vektor kustom (bukan cuma huruf tunggal!):

| # | Aplikasi | Yang bisa dilakukan |
|---|---|---|
| 1 | **Jam** | Jam digital real-time via NTP |
| 2 | **Kalkulator** | Hitung-hitungan dasar |
| 3 | **Orientasi 3D** | Kubus 3D yang berputar sesuai kemiringan HP asli (data MPU6050) |
| 4 | **Setting** | WiFi, tema, kecerahan, kalibrasi sensor & touch |
| 5 | **Notepad** | Catatan tersimpan permanen di SD Card |
| 6 | **Canvas** | Corat-coret pakai jari, brush halus tanpa "bertangga" |
| 7 | **AI Chat** | Ngobrol teks dengan Gemini, bisa juga dikte pakai suara |
| 8 | **Files** | File explorer buat kartu SD |
| 9 | **MJPEG** | Pemutar video motion-JPEG |
| 10 | **Update** | Flash firmware baru — dari SD Card atau URL WiFi |
| 11 | **Baterai** | Tegangan, persentase, estimasi sisa daya |
| 12 | **Snake** | Klasik, dengan tembus tepi layar |
| 13 | **Flappy** | Terbang lewati pipa |
| 14 | **2048** | Swipe 4 arah, gabung angka |
| 15 | **TicTacToe** | Lawan CPU atau 2 pemain, papan sampai 5×5 |
| 16 | **Breakout** | Kendali drag jari **atau** miringkan HP |
| 17 | **Trivia** | Kuis online, 12 kategori × 4 level |
| 18 | **Astronomi** | Foto NASA APOD harian + auto-translate ke Indonesia |
| 19 | **NEO Asteroid** | Radar asteroid dekat Bumi hari ini |
| 20 | **Mars Rover** | Foto terbaru dari Curiosity/Perseverance |
| 21 | **Bumi EPIC** | Foto Bumi dari satelit DSCOVR |
| 22 | **Galeri NASA** | Pencarian bebas ke arsip foto NASA |
| 23 | **Neopixel** | Kontrol LED RGB — solid color atau rainbow otomatis |
| 24 | **MP3 Player** | Muter musik, jalan terus di background |
| 25 | **AVI Player** | Video + audio, format kustom ringan |
| 26 | **Mic Level** | VU meter buat ngecek mic beneran nangkep suara |
| 27 | **HWmonitor** | Beban CPU per-core, RAM/PSRAM, tes speaker |
| 28 | **AI Live** | Ngobrol suara penuh — bicara, AI jawab pakai suara |
| 29 | **Labirin** | Klon Pac-Man, kejar-kejaran hantu, rekor tersimpan |

---

## 🛠️ Yang Perlu Disiapkan

| Komponen | Kenapa Perlu |
|---|---|
| **ESP32-S3** (dengan PSRAM!) | Otak dari semuanya — PSRAM wajib untuk decode gambar & buffer audio |
| **TFT ILI9341** | Layarnya |
| **Touch XPT2046** | Biar bisa disentuh, bukan cuma dilihat |
| **MicroSD Card + modul SDIO** | Simpan file, cache foto, musik, video |
| **MPU6050** | Deteksi kemiringan & goyangan |
| **INMP441** | Mic — buat dikte suara & AI Live |
| **MAX98357A** | Ampli speaker |
| **Motor getar kecil** | Feedback haptic |
| **LED Neopixel (opsional)** | Lampu RGB, tapi seru |
| **Voltage divider 10k+10k** | Biar tahu baterai sekarat atau belum |

---

## 🔌 Peta Kabel (Wiring)

**Layar ILI9341 (SPI)**

| Fungsi | GPIO |
|---|---|
| SCLK | 12 |
| MOSI | 11 |
| MISO | 13 |
| DC | 2 |
| CS | 10 |
| RST | 14 |
| Backlight | 21 |

**Touch XPT2046 (bus SPI terpisah)**

| Fungsi | GPIO |
|---|---|
| SCLK | 6 |
| MOSI | 5 |
| MISO | 4 |
| CS | 9 |

**SD Card (SDIO 1-bit)**

| Fungsi | GPIO |
|---|---|
| CLK | 39 |
| CMD | 38 |
| D0 | 40 |

**Audio Output — MAX98357A** *(dipakai gantian oleh MP3/AVI/AI Live, tidak bisa jalan bersamaan)*

| Fungsi | GPIO |
|---|---|
| BCLK | 42 |
| LRC | 41 |
| DOUT | 17 |

**Mic — INMP441**

| Fungsi | GPIO |
|---|---|
| SCK | 47 |
| WS | 46 |
| SD | 45 |

> Pin L/R ditarik ke GND (channel kiri aktif).

**Sensor & Lainnya**

| Komponen | GPIO |
|---|---|
| MPU6050 SDA | 15 |
| MPU6050 SCL | 7 (alamat I2C `0x68`) |
| Motor getar | 18 |
| LED Neopixel | 48 |
| Baterai (ADC via divider 1:2) | 8 |

---

## 💻 Software & Library

**Wajib**: Arduino-ESP32 core **3.x** (dibutuhkan untuk `ESP_I2S.h`, `rgbLedWrite()`, dan API-API baru lainnya).

**Install manual dulu**, belum bawaan Arduino IDE:
- 🎨 `LovyanGFX` — mesin grafis + driver layar/touch
- 🖼️ `JPEGDEC` — decode semua gambar JPG (foto NASA, wallpaper, MJPEG)
- 🔊 `ESP32-audioI2S` oleh schreibfaul1 (`Audio.h`) — decoder MP3
- 🎞️ `MjpegClass` — pemutar video MJPEG

**Sudah bawaan** Arduino-ESP32 core, tinggal pakai:
`WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `FS`, `SD_MMC`, `FFat`, `Update`, `Preferences`, `Wire`, `ESP_I2S.h`, `mbedtls/base64.h`, `esp_bt.h`, `esp_freertos_hooks.h`.

---

## 🚀 Cara Bangun Sendiri

1. Install **Arduino IDE** + board package **esp32 by Espressif** (versi 3.x).
2. Pilih board **ESP32-S3**, pastikan **PSRAM diaktifkan**, dan pilih skema partisi yang menyisakan ruang untuk **OTA** + **FFat**.
3. Install semua library "wajib" di atas (lewat Library Manager atau clone manual).
4. Cocokkan wiring dengan tabel pin — atau ubah angka pin di kode kalau board kamu beda susunan.
5. Compile & upload seperti sketch Arduino biasa.
6. Nyalakan pertama kali → ikuti layar kalibrasi touch (sentuh tanda di tiap sudut).

Selesai — HP kamu sudah hidup. 🎉

---

## 🔑 Setel API Key

Beberapa fitur butuh kunci API. Semua disimpan sebagai file teks biasa di SD Card (otomatis dibuat saat pertama nyala), atau bisa diisi lewat **Web File Manager** tanpa perlu SD Card sama sekali:

| File | Untuk Fitur | Info |
|---|---|---|
| `/gemini_key.txt` | AI Chat, AI Live, dikte suara | Gratis di Google AI Studio (aistudio.google.com/app/apikey) |
| `/nasa_key.txt` | APOD, NEO, Mars Rover, EPIC, Galeri NASA | Boleh dibiarkan `DEMO_KEY` (limitnya kecil), atau ambil gratis di api.nasa.gov |

WiFi diatur langsung dari HP-nya sendiri di app **Setting** — lengkap dengan fitur scan jaringan sekitar.

---

## 🗂️ Struktur Kartu SD

```
/
├── notepad.txt              → isi Notepad
├── gemini_key.txt           → kunci API Gemini
├── nasa_key.txt             → kunci API NASA
├── ai_memory.txt            → memori percakapan AI (auto-terpangkas)
├── wallpaper.jpg            → wallpaper Home & Lock Screen
├── canvas_land.bin          → kanvas gambar (landscape)
├── canvas_port.bin          → kanvas gambar (portrait)
├── mjpeg/                   → video .mjpeg
├── update/                  → firmware .bin (OTA lokal)
├── mp3/                     → lagu .mp3
├── avi/                     → video .avi (format kustom, lihat di bawah)
├── apod_cache/              → cache foto+terjemahan APOD
├── neo_cache/               → cache data NASA NeoWs
├── mars_cache/              → cache foto Mars Rover
├── epic_cache/              → cache citra Bumi EPIC
└── imglib_cache/            → cache hasil pencarian Galeri NASA
```

> 💡 **Tanpa SD Card?** Tetap jalan! Wallpaper otomatis pindah ke flash internal (FFat), API key ke NVS, dan foto NASA diunduh langsung ke PSRAM tanpa disimpan permanen.

---

## 🌐 Web File Manager

WiFi nyala → buka `http://<IP-HP>/` dari browser mana saja di jaringan yang sama:

- 📤 Upload file apa pun ke SD Card
- ✏️ Edit langsung file teks lewat browser (`.txt`, `.json`, `.csv`, `.log`, `.ini`, `.md`)
- 📥 Download / 🗑️ hapus file
- 🔑 Atur kunci API Gemini
- 🖼️ Ganti wallpaper (upload `wallpaper.jpg`) — langsung aktif, tanpa restart

---

## 🤙 Gesture & Kontrol

| Gerakan | Aksi |
|---|---|
| Swipe ke atas di lock screen | Buka kunci |
| Swipe ke bawah dari status bar | Buka Control Center |
| Swipe ke atas dari tepi bawah | Kembali ke Home |
| Goyangkan HP | Balik ke Home (bisa dimatikan) |
| Miringkan HP | Auto-rotate layar / kontrol Breakout & Labirin |
| Ketuk Dynamic Island | Buka app terkait / lihat detail notifikasi |

---

## 🎞️ Format Video Kustom

**AVI Player** pakai parser kustom yang ringan — cuma dukung video **MJPEG** + audio **PCM 16-bit**. Konversi video biasa jadi format ini pakai ffmpeg:

```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 5 -vf "fps=15,scale=320:240" \
       -c:a pcm_s16le -ar 22050 -ac 1 output.avi
```

> ⚠️ Resolusi **wajib** 320×240 (landscape) — player tidak melakukan scaling otomatis.

**MJPEG Player** lebih simpel — tinggal taruh file `.mjpeg` mentah di folder `/mjpeg`.

---

## ⚠️ Hal-Hal yang Perlu Diketahui

- **RAM internal ESP32-S3 itu kecil (~162KB)**, dan koneksi HTTPS butuh blok memori kontigu ~20–30KB. Kadang muncul error `HTTP -1` yang sebenarnya bukan soal jaringan, tapi RAM internal lagi terpecah-pecah (fragmented). Firmware sudah punya mekanisme tunggu & coba-ulang otomatis, dan app **HWmonitor** menampilkan angka *"Blok Terbesar"* secara real-time buat diagnosis kalau masih kejadian.
- **PSRAM itu wajib**, bukan opsional — tanpa itu, fitur berat (wallpaper, foto NASA, rekam suara, AI Live) gak akan jalan mulus.
- MP3 Player, AVI Player, dan AI Live **berbagi satu jalur audio fisik** yang sama — tidak bisa nyala bersamaan, otomatis saling menghentikan.
- Estimasi baterai murni dari pembacaan tegangan (bukan fuel-gauge IC beneran) — anggap sebagai perkiraan kasar, bukan angka presisi.
- `DEMO_KEY` NASA limitnya kecil — kalau sering dipakai, ambil key pribadi (gratis).
- Endpoint **Mars Rover Photos** dari NASA kadang balas `404` untuk kombinasi sol/rover tertentu — ini keterbatasan di sisi server NASA sendiri, bukan bug aplikasi (sudah ada fallback otomatis ke foto terbaru).

---

## 📄 Lisensi

Belum ditentukan — tambahkan berkas `LICENSE` sesuai kebutuhanmu sebelum membagikan atau menerbitkan ulang proyek ini.

---

<p align="center"><i>Dibangun sepotong demi sepotong, satu bug demi satu bug, sampai jadi "HP" yang beneran bisa dipakai. 🔋📲</i></p>
