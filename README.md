# 📱 Accretion — HP DIY dari Nol, Ditenagai ESP32-S3

> **"Kenapa beli HP, kalau bisa dibikin sendiri?"**

**Accretion** adalah sistem operasi HP custom yang ditulis dari nol — bukan modif Android, bukan port sistem lain — sebuah firmware C++ raksasa (`phone.ino`, ±16.000 baris) yang berjalan di atas chip **ESP32-S3**. Nama kode folder/repo-nya sendiri adalah `ren_phone` (dan repo GitHub-nya `accretion_phone`), tapi HP-nya sendiri bernama **Accretion**. Ia punya lock screen, launcher grid, status bar, Control Center, notifikasi mengambang ala Dynamic Island, sampai emulator NES dan port DOOM — semua nyala di layar TFT kecil bergaya iOS.

Proyek ini juga unik karena **seluruh riwayat perubahannya** — dari v7 sampai v100+ — didokumentasikan langsung di dalam komentar kode: apa bug-nya, apa akar masalahnya, kenapa fix-nya begitu. Firmware ini sekaligus jadi studi kasus panjang soal ngoprek ESP32 sampai ke akar-akarnya (fragmentasi RAM internal, timing SPI dua-bus, manajemen PSRAM, dan sejenisnya).

> ⚠️ Dokumen ini ditulis ulang dari isi kode sumber di repo per commit terbaru yang diunggah. Beberapa fitur di versi-versi lama (MP3 Player, AVI Player, Mars Rover, AI Live/suara, speaker MAX98357A) **sudah dicabut total** dari firmware (lihat catatan v64 & v100 di `phone.ino`) dan **tidak** dijelaskan sebagai fitur aktif di bawah ini.

---

## ✨ Kenapa Ini Menarik

- 🧠 **AI Chat berbasis teks** (Google Gemini API) dengan input teks maupun dikte suara (mic INMP441), lengkap dengan memori percakapan yang tersimpan permanen di SD Card.
- 🕹️ **Emulator NES** (core Nofrendo) — scan otomatis semua file `.nes` di root SD Card, tinggal pilih dari daftar.
- 😈 **Port DOOM** (library ESP-DOOM/doomgeneric) — kontrol virtual joystick + drag-look di layar sentuh, non-audio, jalan di task FreeRTOS terpisah biar UI utama tidak beku.
- 🚀 **Terhubung ke NASA** — foto astronomi harian (APOD, auto-translate ke Indonesia), radar asteroid dekat Bumi (NeoWs), citra satelit Bumi (EPIC), dan pencarian bebas ke arsip foto NASA.
- 🎮 **8 game orisinal**, termasuk **Inferno** (FPS raycaster gothic dengan sistem achievement 10 item, pintu terkunci, tong bisa diledakkan) dan **Labirin** (klon Pac-Man dengan rekor skor permanen).
- 🎞️ **Pemutar video MJPEG** — scan langsung dari root SD Card, dipaksa portrait saat play (biar selalu full-screen), plus fitur pause & lanjut dari posisi mm:ss terakhir.
- 🌐 **Web File Manager built-in** — colok WiFi, buka browser di laptop, langsung bisa upload/edit/hapus file, ganti wallpaper, atau atur API key tanpa sentuh HP-nya sama sekali.
- 🔧 **Update firmware OTA** — taruh file `.bin` di kartu SD (folder `/update`) atau kasih link URL, tanpa colok kabel USB.
- 💡 **Kontrol hardware nyata**: LED RGB (Neopixel), motor getar untuk feedback haptic, sensor gerak MPU6050 untuk auto-rotate & shake-to-home, kalibrasi touch otomatis saat boot pertama.
- 🩺 **Task Manager sendiri** (HWmonitor) — beban tiap core CPU, sisa RAM internal/PSRAM, dan indikator "blok terbesar" (largest free block) untuk mendiagnosis fragmentasi RAM.
- 🎨 **UI bergaya iOS** — Lock Screen, Control Center, dock, Status Bar, Dynamic Island, Notification Shade, keyboard on-screen, dan Search Bar semua sudah di-redesign dengan ikon bulat & panel kaca gelap.

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
- [Emulator NES & DOOM](#-emulator-nes--doom)
- [Format Video MJPEG](#-format-video-mjpeg)
- [Firmware Alternatif: Fork Bruce](#-firmware-alternatif-fork-bruce)
- [Hal-Hal yang Perlu Diketahui](#️-hal-hal-yang-perlu-diketahui)
- [Lisensi](#-lisensi)

---

## 🖼️ Galeri Aplikasi

28 aplikasi berjalan di satu launcher grid dengan ikon vektor kustom, bisa dicari lewat search bar di Home:

| # | Aplikasi | Yang bisa dilakukan |
|---|---|---|
| 1 | **Jam** | Jam digital real-time via NTP |
| 2 | **Kalkulator** | Hitung-hitungan dasar |
| 3 | **Orientasi3D** | Kubus 3D yang berputar sesuai kemiringan HP asli (data MPU6050) |
| 4 | **Setting** | WiFi (dengan scan jaringan), tema, kecerahan, kalibrasi sensor & touch |
| 5 | **Notepad** | Catatan tersimpan permanen di SD Card |
| 6 | **Canvas** | Corat-coret pakai jari, kanvas landscape & portrait terpisah |
| 7 | **AI Chat** | Ngobrol teks dengan Gemini, bisa juga dikte pakai suara (mic), ada memori riwayat percakapan |
| 8 | **Files** | File explorer buat kartu SD |
| 9 | **MJPEG** | Pemutar video motion-JPEG dari root SD Card |
| 10 | **Update** | Flash firmware baru — dari SD Card (`/update`) atau URL WiFi |
| 11 | **Baterai** | Tegangan, persentase, estimasi sisa daya |
| 12 | **Snake** | Klasik, dengan tembus tepi layar |
| 13 | **Flappy** | Terbang lewati pipa |
| 14 | **2048** | Swipe 4 arah, gabung angka |
| 15 | **TicTacToe** | Lawan CPU atau 2 pemain, papan sampai 5×5 |
| 16 | **Breakout** | Kendali drag jari **atau** miringkan HP |
| 17 | **Trivia** | Kuis online, 12 kategori × 4 level |
| 18 | **Astronomi** | Foto NASA APOD harian + auto-translate ke Indonesia |
| 19 | **NEO Asteroid** | Radar asteroid dekat Bumi hari ini |
| 20 | **Bumi EPIC** | Foto Bumi dari satelit DSCOVR |
| 21 | **Galeri NASA** | Pencarian bebas ke arsip foto NASA |
| 22 | **Neopixel** | Kontrol LED RGB — solid color atau rainbow otomatis |
| 23 | **Mic Level** | VU meter buat ngecek mic INMP441 beneran nangkep suara |
| 24 | **HWmonitor** | Beban CPU per-core, RAM internal/PSRAM, blok memori terbesar |
| 25 | **Labirin** | Klon Pac-Man, kejar-kejaran hantu dengan pathfinding, rekor tersimpan |
| 26 | **Inferno** | FPS raycaster gothic — 3 varian peta, 10 achievement, pintu terkunci, tong meledak, knockback musuh |
| 27 | **NES** | Emulator NES (core Nofrendo) — scan & pilih ROM `.nes` dari SD Card |
| 28 | **DOOM** | Port doomgeneric (lib ESP-DOOM), non-audio, WAD disediakan sendiri oleh user |

> Fitur yang **sudah dicabut** dari versi-versi awal proyek dan tidak lagi ada di firmware saat ini: MP3 Player, AVI Player, Mars Rover Photos, AI Live (chat suara dua arah), dan seluruh jalur output speaker (MAX98357A) beserta fitur "Tes Speaker" di HWmonitor.

---

## 🛠️ Yang Perlu Disiapkan

| Komponen | Kenapa Perlu |
|---|---|
| **ESP32-S3** (Flash 16MB, PSRAM 8MB OPI) | Otak dari semuanya — PSRAM wajib untuk decode gambar, buffer video, dan task berat (NES/DOOM) |
| **TFT ILI9341** (320×240) | Layarnya |
| **Touch XPT2046** | Biar bisa disentuh, bukan cuma dilihat |
| **MicroSD Card + modul SDIO** | Simpan file, ROM game, WAD DOOM, cache foto, video, memori AI |
| **MPU6050** | Deteksi kemiringan & goyangan (auto-rotate, shake-to-home, kontrol tilt di game) |
| **INMP441** | Mic — buat dikte suara AI Chat & VU meter |
| **Motor getar kecil** | Feedback haptic |
| **LED Neopixel (opsional)** | Lampu RGB kustom |
| **Voltage divider 10k+10k** | Biar tahu baterai sekarat atau belum |

> Tidak ada modul speaker/audio output di daftar ini — fitur speaker sudah dicabut total dari firmware (lihat bagian "Hal-Hal yang Perlu Diketahui").

---

## 🔌 Peta Kabel (Wiring)

**Layar ILI9341 (SPI2_HOST)**

| Fungsi | GPIO |
|---|---|
| SCLK | 12 |
| MOSI | 11 |
| MISO | 13 |
| DC | 2 |
| CS | 10 |
| RST | 14 |
| Backlight | 21 |

**Touch XPT2046 (bus SPI terpisah, SPI3_HOST — `bus_shared=false`)**

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

**Mic — INMP441 (I2S)**

| Fungsi | GPIO |
|---|---|
| SCK | 47 |
| WS | 46 |
| SD | 45 |

**Sensor & Lainnya**

| Komponen | GPIO |
|---|---|
| MPU6050 SDA | 15 |
| MPU6050 SCL | 7 (alamat I2C `0x68`) |
| Motor getar | 18 |
| LED Neopixel | 48 |
| Baterai (ADC via divider) | 8 |

---

## 💻 Software & Library

**Wajib**: Arduino-ESP32 core **3.x**.

**Diinstal via `arduino-cli` di CI** (`.github/workflows/build-firmware.yml`):
- 🎨 `LovyanGFX` — mesin grafis + driver layar/touch (mesin render utama saat ini)
- 🖼️ `JPEGDEC` — decode gambar JPG (foto NASA, wallpaper, MJPEG)
- 🎨 `lvgl` — sudah masuk daftar instalasi build, sebagai persiapan untuk pengembangan UI ke depan (belum ada pemanggilan LVGL di kode `phone.ino` saat ini — engine render aktif tetap LovyanGFX sepenuhnya)
- 🔊 `ESP32-audioI2S` (schreibfaul1) — terdaftar di CI tapi jalur pemutaran audionya sudah dicabut dari kode

**Library C/C++ yang di-vendor langsung di repo** (bukan lewat Library Manager):
- `nofrendo_core/` — core emulator NES (Nofrendo), dipatch di CI (rename `log_printf`, hapus `vid_setpalette`, tambah flag exit)
- `esp_doom_core/` — glue code ESP-DOOM (`DoomGlue`, `IDoomDisplay`, `IDoomInput`, dst.) untuk port DOOM

**Sudah bawaan** Arduino-ESP32 core: `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `FS`, `SD_MMC`, `FFat`, `Update`, `Preferences`, `Wire`, `ESP_I2S.h`, `mbedtls/base64.h`, `esp_bt.h`, `esp_freertos_hooks.h`, `esp_heap_caps.h`.

---

## 🚀 Cara Bangun Sendiri

Build resmi memakai **GitHub Actions** (`arduino-cli`, board `esp32:esp32:esp32s3`, `FlashSize=16M`, `PSRAM=opi`, `PartitionScheme=app3M_fat9M_16MB`). Ringkasannya kalau mau build manual di komputer sendiri:

1. Install **Arduino IDE** + board package **esp32 by Espressif** (versi 3.x).
2. Pilih board **ESP32-S3**, pastikan **PSRAM diaktifkan (OPI)**, Flash Size **16MB**, dan skema partisi yang menyisakan ruang OTA + FFat (mis. `app3M_fat9M_16MB`).
3. Install library `LovyanGFX` dan `JPEGDEC` lewat Library Manager.
4. Satukan semua file sketch ke satu folder build (`phone.ino` sebagai file utama, plus `MjpegClass.h`, `phone_notif_center.ino`, 5 file `nes_*_renphone.ino`, `doom_renphone.ino`, dan `hw_conf.h`).
5. Salin isi `nofrendo_core/` (kecuali `main.c`, `display.c`, `keyboard.c`, `osd.c`, `timing.c`, `sound.c`, `stubs.c`) dan `esp_doom_core/` ke folder `src/` di dalam folder build, lalu terapkan 3 patch kecil yang dilakukan CI:
   - rename `log_printf` → `nof_log_printf` (hindari bentrok simbol)
   - hapus fungsi `vid_setpalette` di `vid_drv.c`
   - tambah flag `nesExitRequested` ke loop utama `nofrendo.c` (biar bisa keluar emulator dengan bersih)
6. Tambahkan flag compiler MbedTLS berikut (penting untuk HTTPS ke Gemini/NASA agar tidak gampang gagal alokasi buffer):
   `-DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1 -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048`
7. Cocokkan wiring dengan tabel pin — atau ubah angka pin di kode kalau board kamu beda susunan.
8. Compile & upload.
9. Nyalakan pertama kali → ikuti layar kalibrasi touch (kalibrasi berjalan otomatis tiap kali rotasi layar berubah dari kalibrasi sebelumnya).

Atau, paling gampang: **fork repo ini di GitHub dan biarkan Actions yang build** — hasil `.bin`-nya otomatis muncul sebagai artifact di tab Actions.

---

## 🔑 Setel API Key

Semua kunci API disimpan sebagai file teks biasa di SD Card (otomatis dibuat saat pertama nyala), atau bisa diisi lewat **Web File Manager** tanpa perlu buka casing:

| File | Untuk Fitur | Info |
|---|---|---|
| `/gemini_key.txt` | AI Chat (teks + dikte suara) | Gratis di Google AI Studio (aistudio.google.com/app/apikey). Kalau SD Card tidak terdeteksi, key disimpan di NVS (flash internal) sebagai fallback |
| `/nasa_key.txt` | Astronomi (APOD), NEO Asteroid, Bumi EPIC, Galeri NASA | Boleh dibiarkan `DEMO_KEY` (limit kecil, otomatis dibuat kalau file belum ada), atau ambil gratis di api.nasa.gov |

WiFi diatur langsung dari HP-nya sendiri di app **Setting**, lengkap dengan fitur scan jaringan sekitar.

---

## 🗂️ Struktur Kartu SD

```
/
├── notepad.txt              → isi Notepad
├── gemini_key.txt           → kunci API Gemini
├── nasa_key.txt             → kunci API NASA (boleh "DEMO_KEY")
├── ai_memory.txt            → memori percakapan AI Chat (maks 12 entri / 2000 karakter, auto-terpangkas)
├── wallpaper.jpg            → wallpaper Home & Lock Screen
├── canvas_land.bin          → kanvas gambar (landscape)
├── canvas_port.bin          → kanvas gambar (portrait)
├── *.mjpeg                  → video MJPEG, langsung di ROOT (bukan folder khusus)
├── *.nes                    → ROM NES, langsung di ROOT
├── doom1.wad                → WAD DOOM milik sendiri (tidak disertakan repo, isu hak cipta)
├── update/                  → firmware .bin untuk OTA lokal
├── apod_cache/              → cache foto+terjemahan APOD
├── neo_cache/                → cache data NASA NeoWs
├── epic_cache/               → cache citra Bumi EPIC
└── imglib_cache/            → cache hasil pencarian Galeri NASA
```

> 💡 **Tanpa SD Card?** Tetap jalan sebagian: wallpaper otomatis pindah ke flash internal (FFat), API key Gemini ke NVS, tapi NES/DOOM/MJPEG/notepad/memori AI tetap butuh SD Card karena bergantung penuh padanya.

---

## 🌐 Web File Manager

WiFi nyala → buka `http://<IP-HP>/` dari browser mana saja di jaringan yang sama:

- 📤 Upload file apa pun ke SD Card
- ✏️ Edit langsung file teks lewat browser
- 📥 Download / 🗑️ hapus file
- 🔑 Atur kunci API Gemini (`/apikey`)
- 🖼️ Ganti wallpaper — upload file JPG dengan nama **persis** `wallpaper.jpg`, langsung aktif tanpa restart, otomatis diterapkan juga ke Lock Screen

---

## 🤙 Gesture & Kontrol

| Gerakan | Aksi |
|---|---|
| Swipe ke atas di lock screen | Buka kunci |
| Swipe ke bawah dari status bar | Buka Control Center |
| Swipe ke atas dari tepi bawah | Kembali ke Home (juga jalan di dalam app MJPEG yang blocking) |
| Goyangkan HP | Balik ke Home (bisa dimatikan di Setting) |
| Miringkan HP | Auto-rotate layar / kontrol tilt di Breakout & Inferno |
| Ketuk Dynamic Island | Buka app terkait / lihat detail notifikasi |
| Swipe turun dari pil Dynamic Island | Buka Notification Shade |
| Tekan-tahan pada notifikasi | Buka context menu |

---

## 🕹️ Emulator NES & DOOM

- **NES**: berjalan di task FreeRTOS terpisah (stack 48KB di PSRAM) supaya tidak membekukan UI utama. Buka app NES → firmware scan semua file `.nes` di root SD Card → pilih dari daftar bertingkat halaman.
- **DOOM**: port `doomgeneric` lewat library ESP-DOOM, juga di task terpisah yang memegang SPI display langsung. Non-audio secara sengaja (`DOOM_NO_AUDIO`). WAD **tidak disertakan** di repo karena isu hak cipta — taruh file WAD milikmu sendiri (mis. `doom1.wad` versi shareware) persis di root SD Card; firmware akan menampilkan toast error yang jelas kalau file tidak ditemukan. Selama DOOM/NES berjalan, orientasi layar dipaksa landscape/portrait sesuai kontrak masing-masing lalu dikembalikan otomatis setelah keluar.

---

## 🎞️ Format Video MJPEG

Pemutar video pakai parser MJPEG murni (lewat `MjpegClass.h` + `JPEGDEC`). Konversi video biasa ke format ini pakai ffmpeg:

```bash
ffmpeg -i input.mp4 -vf "fps=15,scale=240:320" output.mjpeg
```

> ⚠️ Video di-encode untuk resolusi native panel **240×320 portrait** — pemutar memaksa rotasi portrait selama playback apapun orientasi device saat itu, lalu mengembalikannya setelah selesai. Tap = pause/lanjut dari posisi mm:ss terakhir (bukan mulai ulang), tahan ≥600ms = berhenti total dan kembali ke daftar file.

---

## 🔀 Firmware Alternatif: Fork Bruce

Repo ini juga menyertakan `Bruce-accretion-phone.bin` — build biner dari fork [Bruce](https://github.com/pr3y/Bruce) (tool multi-fungsi ESP32 pihak ketiga) dengan board port khusus untuk hardware **Accretion yang sama** (XPT2046 di GPIO 6/5/4/9, ILI9341). Ini adalah firmware terpisah/alternatif — bukan bagian dari OS Accretion/`phone.ino` di atas — untuk yang ingin memakai fitur-fitur Bruce (seperti tool RF/IR/NFC bawaan Bruce) di board fisik yang sama, tinggal flash file `.bin` ini lewat app **Update** atau esptool. Catatan: board port ini memakai konstanta kalibrasi touch generik (dari panel CYD lain) dan **tidak** punya sistem kalibrasi sendiri seperti di `phone.ino`.

---

## ⚠️ Hal-Hal yang Perlu Diketahui

- **RAM internal ESP32-S3 itu kecil (~162KB)**, dan koneksi HTTPS (Gemini/NASA) butuh blok memori kontigu yang cukup besar. Kadang muncul error `HTTP -1` yang sebenarnya bukan soal jaringan, tapi RAM internal lagi terpecah-pecah (fragmented). Firmware sudah punya mekanisme tunggu & coba-ulang otomatis (`nasaWaitForHeap`, dst.), dan app **HWmonitor** menampilkan angka "blok terbesar" (largest free block) secara real-time untuk diagnosis.
- **Fitur speaker/audio output sudah dicabut total** (v100) atas permintaan pengembangan proyek — tidak ada lagi MP3 Player, AVI Player, AI Live (chat suara dua arah), atau modul MAX98357A yang dipakai. Mic (INMP441) tetap ada dan tetap dipakai untuk dikte suara di AI Chat serta VU meter Mic Level.
- **PSRAM itu wajib**, bukan opsional — dipakai buffer decode MJPEG (~153KB), stack task NES (48KB) dan DOOM, serta objek JPEGDEC untuk foto NASA/wallpaper.
- Estimasi baterai murni dari pembacaan tegangan ADC (bukan fuel-gauge IC beneran) — anggap sebagai perkiraan kasar.
- `DEMO_KEY` NASA limitnya kecil — kalau sering dipakai, ambil key pribadi (gratis).
- File `phone_notif_center.ino` sengaja dinamai begitu (bukan `notif_center.ino`) supaya urutan compile alfabetis Arduino menaruhnya setelah `phone.ino`, sehingga semua definisi global di `phone.ino` otomatis terlihat tanpa perlu `extern`.
- Library `lvgl` sudah masuk daftar instalasi di workflow CI, tapi belum ada satupun pemanggilan LVGL di kode aktif — mesin render saat ini sepenuhnya LovyanGFX.

---

## 📄 Lisensi

**GNU Affero General Public License v3.0 (AGPL-3.0)** — lihat berkas [`LICENSE`](./LICENSE) untuk teks lengkap.

---

<p align="center"><i>Dibangun sepotong demi sepotong, satu bug demi satu bug, sampai jadi "HP" yang beneran bisa dipakai. 🔋📲</i></p>
