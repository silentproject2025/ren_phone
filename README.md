# 📱 Accretion — HP DIY dari Nol, Ditenagai ESP32-S3

> **"Kenapa beli HP, kalau bisa dibikin sendiri?"**

**Accretion** adalah sistem operasi HP custom yang ditulis dari nol — bukan modif Android, bukan port sistem lain — sebuah firmware C++ raksasa (`phone.ino`, ±16.700 baris) yang berjalan di atas chip **ESP32-S3**. Nama kode folder/repo-nya adalah `ren_phone`, tapi HP-nya sendiri bernama **Accretion**. Ia punya lock screen, launcher grid, status bar, Control Center, notifikasi mengambang ala Dynamic Island, emulator NES, port DOOM, sampai pemutar musik Bluetooth dengan rekomendasi lagu berbasis AI on-device — semua nyala di layar TFT kecil bergaya iOS.

Proyek ini juga unik karena **seluruh riwayat perubahannya** (v7 sampai v115+) didokumentasikan langsung di dalam komentar kode: apa bug-nya, apa akar masalahnya, kenapa fix-nya begitu. Firmware ini sekaligus jadi studi kasus panjang soal ngoprek ESP32 sampai ke akar-akarnya (fragmentasi RAM internal, timing SPI dua-bus, manajemen PSRAM, dan sejenisnya).

> ⚠️ Dokumen ini ditulis dari isi kode sumber di repo. Fitur lama yang **sudah dicabut** (MP3 Player lokal via speaker, AVI Player, Mars Rover, AI Live/suara, speaker MAX98357A) tidak dijelaskan sebagai fitur aktif.

---

## ✨ Kenapa Ini Menarik

- 🧠 **AI Chat berbasis teks** (Google Gemini API) dengan input teks maupun dikte suara (mic INMP441), lengkap dengan memori percakapan yang tersimpan permanen di SD Card.
- 🎧 **Musik via Bluetooth (A2DP)** — S3 membaca MP3 dari SD Card, mengirimnya lewat kabel UART ke **ESP32-CAM** yang bertugas sebagai co-prosesor Bluetooth, lalu diputar ke TWS/earbuds. Ada mode acak **AI (RYNE v2)** yang belajar kebiasaan dengarmu.
- 🎙️ **Wake Word "Hei Nyx"** — kata kunci suara buatan sendiri (MFCC + DTW), on-device, tanpa internet dan tanpa training di laptop.
- 🕹️ **Emulator NES** (core Nofrendo) — scan otomatis semua file `.nes` di root SD Card.
- 😈 **Port DOOM** (ESP-DOOM/doomgeneric) — virtual joystick + drag-look di layar sentuh, jalan di task FreeRTOS terpisah.
- 🚀 **Terhubung ke NASA** — foto astronomi harian (APOD, auto-translate ke Indonesia), radar asteroid (NeoWs), citra satelit Bumi (EPIC), dan pencarian arsip foto NASA.
- 🎮 **8 game orisinal**, termasuk **Inferno** (FPS raycaster gothic dengan 10 achievement, pintu terkunci, tong meledak) dan **Labirin** (klon Pac-Man dengan rekor skor permanen).
- 🎞️ **Pemutar video MJPEG** — pause & lanjut dari posisi mm:ss terakhir.
- 🌐 **Web File Manager built-in** — upload/edit/hapus file, ganti wallpaper, atur API key dari browser.
- 🔧 **Update firmware OTA** — dari SD Card (`/update`) atau URL.
- 💡 **Kontrol hardware nyata**: LED RGB (Neopixel), motor getar untuk haptic, sensor gerak MPU6050 untuk auto-rotate & shake-to-home, kalibrasi touch otomatis saat boot pertama.
- 🩺 **Task Manager sendiri** (HWmonitor) — beban tiap core CPU, sisa RAM internal/PSRAM, dan indikator "blok terbesar" untuk mendiagnosis fragmentasi RAM.
- 🎨 **UI bergaya iOS** — Lock Screen, Control Center, dock, Status Bar, Dynamic Island, Notification Shade, keyboard on-screen, dan Search Bar dengan ikon bulat & panel kaca gelap. Ada 5 tema: Aurora (default), Dark, AMOLED, Light, Pastel.

---

## 📚 Daftar Isi

- [Galeri Aplikasi](#️-galeri-aplikasi)
- [Control Center](#️-control-center)
- [Musik & Bluetooth](#-musik--bluetooth)
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

**32 aplikasi** berjalan di satu launcher grid dengan ikon vektor kustom, bisa dicari lewat search bar di Home. Warna lingkaran tiap ikon berbeda, dan warna gambar ikon dipilih otomatis (gelap atau putih) sesuai terang lingkarannya, jadi tetap terbaca di semua tema.

| # | Aplikasi | Yang bisa dilakukan |
|---|---|---|
| 1 | **Jam** | Jam digital real-time via NTP |
| 2 | **Kalkulator** | Hitung-hitungan dasar |
| 3 | **Orientasi3D** | Kubus 3D yang berputar sesuai kemiringan HP asli (data MPU6050) |
| 4 | **Setting** | WiFi (dengan scan jaringan), tema, kecerahan, kalibrasi sensor & touch |
| 5 | **Notepad** | Catatan tersimpan permanen di SD Card |
| 6 | **Canvas** | Corat-coret pakai jari, kanvas landscape & portrait terpisah |
| 7 | **AI Chat** | Ngobrol teks dengan Gemini, bisa dikte pakai suara, ada memori riwayat percakapan |
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
| 23 | **Mic Level** | VU meter untuk ngecek mic INMP441 beneran nangkep suara |
| 24 | **HWmonitor** | Beban CPU per-core, RAM internal/PSRAM, blok memori terbesar |
| 25 | **Labirin** | Klon Pac-Man, kejar-kejaran hantu dengan pathfinding, rekor tersimpan |
| 26 | **Inferno** | FPS raycaster gothic — 3 varian peta, 10 achievement, pintu terkunci, tong meledak, knockback musuh |
| 27 | **NES** | Emulator NES (core Nofrendo) — scan & pilih ROM `.nes` dari SD Card |
| 28 | **DOOM** | Port doomgeneric, non-audio, WAD disediakan sendiri oleh user |
| 29 | **PPU Demo** | Demo engine **NyxPPU** ala konsol game lama: tile bank 8×8, 2 layer background dengan scroll parallax, dan sprite OAM dengan prioritas depan/belakang |
| 30 | **Pomodoro** | Timer fokus/istirahat dengan ring progres melingkar; tiap 4 sesi fokus dapat istirahat panjang 15 menit, durasi fokus & istirahat pendek bisa diatur |
| 31 | **Wake Word** | Rekam kata kunci "Hei Nyx" sendiri 4×, atur sensitivitas, tes skor live. Saat terdeteksi, HP getar lalu membuka AI Chat dan mulai merekam. Default mati (hemat baterai) |
| 32 | **Musik** | Pemutar MP3 dari SD Card ke TWS lewat Bluetooth (lihat bagian [Musik & Bluetooth](#-musik--bluetooth)) |

---

## 🎛️ Control Center

Swipe ke bawah dari status bar. Isinya **10 tombol bulat** (tanpa label, animasi pop-in bergantian, warna ON/OFF berpindah halus) plus slider **Brightness** yang bisa di-drag. Di landscape tombol disusun **5×2**, di portrait **3×4**.

| # | Tombol | Fungsi |
|---|---|---|
| 1 | **WiFi** | Sambung/putus WiFi |
| 2 | **Airplane** | Mode pesawat |
| 3 | **DND** | Jangan ganggu |
| 4 | **Tema** | Ganti tema (Aurora → Dark → AMOLED → Light → Pastel) |
| 5 | **Orientasi** | Ganti landscape/portrait (otomatis mematikan auto-rotate) |
| 6 | **Shake** | Aktif/nonaktif shake-to-home |
| 7 | **Kunci** | Kunci layar |
| 8 | **Neopixel** | Nyalakan LED RGB mode Rainbow |
| 9 | **Senyap** | Matikan/nyalakan getar (tersimpan permanen di NVS) |
| 10 | **Bluetooth** | Nyalakan/matikan Bluetooth modul CAM. Terisi saat BT nyala, ada titik hijau kecil saat TWS terhubung |

> Tombol Bluetooth mengirim perintah `RP_BTMODE` ke ESP32-CAM, jadi **firmware CAM harus versi terbaru** supaya BT-nya benar-benar ikut mati/nyala. Statusnya tersimpan di NVS dan dikirim ulang ke CAM setiap kali CAM restart.

---

## 🎧 Musik & Bluetooth

ESP32-S3 sendiri tidak memakai Bluetooth untuk audio. Tugas itu diserahkan ke board **ESP32-CAM** sebagai **co-prosesor Bluetooth A2DP**:

```
SD Card ──▶ ESP32-S3 (master) ──UART 921600──▶ ESP32-CAM ──A2DP──▶ TWS / earbuds
            baca MP3, UI,                      buffer MP3, decode (Helix),
            playlist, RYNE                     buffer PCM, kirim A2DP
```

- **Cover lagu dari ID3 tag**: kalau MP3 punya gambar cover (ID3v2.2/2.3/2.4, frame APIC) berformat **JPEG baseline**, app Musik menampilkannya menggantikan piringan hitam. PNG, JPEG progressive, atau lagu tanpa cover otomatis kembali ke piringan hitam.
- **Letakkan lagu di `/music`** pada SD Card (maks 200 lagu, format **MP3 Layer III 44,1 kHz stereo**).
- Pemutar punya cover warna unik per judul dengan piringan hitam berputar, equalizer, slider volume, progress bar, dan tombol acak/ulang/suka. Judul panjang akan bergeser (satu salinan, dengan jeda di awal dan di ujung).
- Halaman **TWS** untuk scan dan sambung ke perangkat Bluetooth; nama perangkat terakhir diingat oleh CAM dan disambung ulang otomatis.
- Musik tetap jalan walau pindah app (task terpisah), dan otomatis jeda saat TWS putus lalu lanjut saat tersambung lagi.
- **Mode acak 3 pilihan**: mati, biasa, atau **AI (RYNE v2)**.
- **AVRCP (kontrol dari earbuds)**: tombol play/pause, next, dan prev di TWS langsung mengendalikan app Musik. ESP32-CAM menangkap perintah AVRCP lalu meneruskannya ke S3 lewat frame `RP_KEY`. Sebagian earbuds hanya mengirim sebagian tombol, tergantung merek.
  - **Volume absolut AVRCP sengaja tidak diiklankan** (`esp_avrc_tg_set_rn_evt_cap` dihapus). Saat diiklankan, earbuds mendaftar notifikasi volume yang belum dijawab callback, dan CAM restart karena *task watchdog* beberapa detik setelah lagu mulai. Tanpa itu, AVRCP stabil.
  - Saklar `RP_AVRCP_ENABLE` di `cam_bt_coprocessor.ino` (`0` = matikan total AVRCP) tetap ada untuk isolasi bug memori/link.
  - Kalau CAM restart, S3 menampilkan alasannya di layar Musik (mis. `CAM restart: watchdog task`).

### RYNE v2 — rekomendasi lagu on-device

Header-only (`ryne_engine.h`), C++ murni tanpa library Arduino:

- **Vibe** (8 kelas: Semangat, Sendu, Bosen, Nostalgik, Fokus, Gelisah, Santai, Excited) ditebak dari 13 fitur perilaku dan audio (skip rate, listen-through, volume terhadap kebiasaanmu sendiri, energi dan kecerahan audio, jam malam, dan seterusnya), memakai softmax + filter HMM.
- **Pemilihan lagu** memakai *contextual Thompson Sampling* (regresi Bayesian online) dengan bonus lagu baru, penalti kejenuhan, dan preferensi kontinuitas sesuai vibe.
- Data belajar disimpan di SD Card sebagai `/ryne2.bin`.

---

## 🛠️ Yang Perlu Disiapkan

| Komponen | Kenapa Perlu |
|---|---|
| **ESP32-S3** (Flash 16MB, PSRAM 8MB OPI) | Otak dari semuanya — PSRAM wajib untuk decode gambar, buffer video, dan task berat (NES/DOOM) |
| **TFT ILI9341** (320×240) | Layarnya |
| **Touch XPT2046** | Biar bisa disentuh, bukan cuma dilihat |
| **MicroSD Card + modul SDIO** | Simpan file, ROM game, WAD DOOM, musik, cache foto, video, memori AI |
| **MPU6050** | Deteksi kemiringan & goyangan (auto-rotate, shake-to-home, kontrol tilt di game) |
| **INMP441** | Mic — dikte suara AI Chat, VU meter, dan Wake Word |
| **Motor getar kecil** | Feedback haptic |
| **ESP32-CAM (AI Thinker)** | Co-prosesor Bluetooth untuk app Musik *(opsional kalau tidak butuh musik BT)* |
| **TWS / earbuds Bluetooth** | Tujuan audio musik |
| **LED Neopixel (opsional)** | Lampu RGB kustom |
| **Voltage divider 10k+10k** | Biar tahu baterai sekarat atau belum |

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

**Link ke ESP32-CAM (UART, 921600 baud 8N1)**

| ESP32-S3 | ESP32-CAM |
|---|---|
| GPIO41 (TX) | GPIO13 (RX) |
| GPIO42 (RX) | GPIO14 (TX) |
| GND | GND |

> CAM diberi daya lewat pin **5V** (bukan 3V3). **Cabut kartu SD dari slot CAM** karena GPIO13/14 tersambung ke slot SD. Komentar di bagian atas `cam_bt_coprocessor.ino` masih menyebut GPIO17/16 di sisi S3, itu sudah usang — yang dipakai kode saat ini adalah **GPIO41/42** (`MUS_UART_TX`/`MUS_UART_RX` di `musicbt_renphone.ino`).

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

### Firmware utama (ESP32-S3)

Diinstal via `arduino-cli` di CI (`.github/workflows/build-firmware.yml`):
- 🎨 `LovyanGFX` — mesin grafis + driver layar/touch (mesin render utama)
- 🖼️ `JPEGDEC` — decode gambar JPG (foto NASA, wallpaper, MJPEG)
- 🎨 `lvgl` — masuk daftar instalasi sebagai persiapan, belum ada pemanggilan LVGL di kode aktif
- 🔊 `ESP32-audioI2S` — terdaftar di CI tapi jalur audio speaker sudah dicabut dari kode

**Di-vendor langsung di repo**:
- `nofrendo_core/` — core emulator NES, dipatch di CI
- `esp_doom_core/` — glue code ESP-DOOM untuk port DOOM

### Firmware CAM (ESP32-CAM)

Folder `cam_bt_coprocessor/`, dibuild oleh `.github/workflows/build-cam-coprocessor.yml`:
- `audio-tools` (pschatzmann) — pipeline audio
- `arduino-libhelix` — decoder MP3
- `ESP32-A2DP` (pschatzmann) — sumber A2DP

**Sudah bawaan** Arduino-ESP32 core: `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `FS`, `SD_MMC`, `FFat`, `Update`, `Preferences`, `Wire`, `ESP_I2S.h`, `mbedtls/base64.h`, `esp_bt.h`, `esp_heap_caps.h`.

---

## 🚀 Cara Bangun Sendiri

Build resmi memakai **GitHub Actions** (`arduino-cli`, board `esp32:esp32:esp32s3`, `FlashSize=16M`, `PSRAM=opi`, `PartitionScheme=app3M_fat9M_16MB`).

### A. Firmware ESP32-S3

1. Install **Arduino IDE** + board package **esp32 by Espressif** (versi 3.x).
2. Pilih board **ESP32-S3**, **PSRAM = OPI**, Flash **16MB**, partisi `app3M_fat9M_16MB`.
3. Install library `LovyanGFX` dan `JPEGDEC` lewat Library Manager.
4. Satukan file sketch ke satu folder build, dengan `phone.ino` sebagai file utama:
   - `phone.ino`, `phone_notif_center.ino`, `MjpegClass.h`
   - `ppu_engine_renphone.ino`, `wakeword_renphone.ino`
   - `musicbt_renphone.ino`, **`rplink.h`**, `ryne_engine.h`
   - 5 file `nes_*_renphone.ino`, `doom_renphone.ino`, `hw_conf.h`
5. Salin isi `nofrendo_core/` (kecuali `main.c`, `display.c`, `keyboard.c`, `osd.c`, `timing.c`, `sound.c`, `stubs.c`) dan `esp_doom_core/` ke folder `src/` di dalam folder build, lalu terapkan 3 patch kecil dari CI:
   - rename `log_printf` → `nof_log_printf`
   - hapus fungsi `vid_setpalette` di `vid_drv.c`
   - tambah flag `nesExitRequested` ke loop utama `nofrendo.c`
6. Tambahkan flag compiler MbedTLS (penting untuk HTTPS ke Gemini/NASA):
   `-DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1 -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048`
7. Cocokkan wiring dengan tabel pin, lalu compile & upload.
8. Nyalakan pertama kali → ikuti layar kalibrasi touch.

### B. Firmware ESP32-CAM (khusus fitur Musik/Bluetooth)

1. Install library `audio-tools`, `arduino-libhelix`, dan `ESP32-A2DP`.
2. Board **AI Thinker ESP32-CAM**, **Partition Scheme: Huge APP**, PSRAM aktif.
3. Buka folder `cam_bt_coprocessor/`, compile & upload (`rplink.h` di folder ini harus **identik** dengan `rplink.h` di root repo).
4. Sambungkan kabel UART sesuai tabel wiring.

> Paling gampang: **fork repo ini di GitHub dan biarkan Actions yang build** — hasil `.bin` S3 dan CAM muncul sebagai artifact di tab Actions.

---

## 🔑 Setel API Key

Semua kunci API disimpan sebagai file teks biasa di SD Card (otomatis dibuat saat pertama nyala), atau diisi lewat **Web File Manager**:

| File | Untuk Fitur | Info |
|---|---|---|
| `/gemini_key.txt` | AI Chat (teks + dikte suara) | Gratis di Google AI Studio. Kalau SD tidak terdeteksi, key disimpan di NVS sebagai fallback |
| `/nasa_key.txt` | Astronomi, NEO Asteroid, Bumi EPIC, Galeri NASA | Boleh `DEMO_KEY` (limit kecil), atau ambil gratis di api.nasa.gov |

WiFi diatur dari app **Setting**, lengkap dengan scan jaringan sekitar.

---

## 🗂️ Struktur Kartu SD

```
/
├── notepad.txt              → isi Notepad
├── gemini_key.txt           → kunci API Gemini
├── nasa_key.txt             → kunci API NASA (boleh "DEMO_KEY")
├── ai_memory.txt            → memori percakapan AI Chat (maks 12 entri / 2000 karakter)
├── wallpaper.jpg            → wallpaper Home & Lock Screen
├── canvas_land.bin          → kanvas gambar (landscape)
├── canvas_port.bin          → kanvas gambar (portrait)
├── ryne2.bin                → data belajar RYNE v2 (rekomendasi musik)
├── music/                   → file MP3 untuk app Musik (maks 200 lagu)
├── *.mjpeg                  → video MJPEG, langsung di ROOT
├── *.nes                    → ROM NES, langsung di ROOT
├── doom1.wad                → WAD DOOM milik sendiri (tidak disertakan repo)
├── update/                  → firmware .bin untuk OTA lokal
├── apod_cache/              → cache foto+terjemahan APOD
├── neo_cache/               → cache data NASA NeoWs
├── epic_cache/              → cache citra Bumi EPIC
└── imglib_cache/            → cache hasil pencarian Galeri NASA
```

> 💡 **Tanpa SD Card?** Tetap jalan sebagian: wallpaper pindah ke flash internal (FFat), API key Gemini ke NVS. Tapi NES/DOOM/MJPEG/notepad/musik/memori AI butuh SD Card.

---

## 🌐 Web File Manager

WiFi nyala → buka `http://<IP-HP>/` dari browser di jaringan yang sama:

- 📤 Upload file apa pun ke SD Card
- ✏️ Edit langsung file teks lewat browser
- 📥 Download / 🗑️ hapus file
- 🔑 Atur kunci API Gemini (`/apikey`)
- 🖼️ Ganti wallpaper — upload JPG dengan nama **persis** `wallpaper.jpg`, langsung aktif tanpa restart dan ikut diterapkan ke Lock Screen

---

## 🤙 Gesture & Kontrol

| Gerakan | Aksi |
|---|---|
| Swipe ke atas di lock screen | Buka kunci |
| Swipe ke bawah dari status bar | Buka Control Center |
| Swipe ke atas dari tepi bawah | Kembali ke Home (juga jalan di dalam app MJPEG) |
| Goyangkan HP | Balik ke Home (bisa dimatikan lewat Control Center/Setting) |
| Miringkan HP | Auto-rotate layar / kontrol tilt di Breakout & Inferno |
| Ketuk Dynamic Island | Buka app terkait / lihat detail notifikasi |
| Swipe turun dari pil Dynamic Island | Buka Notification Shade |
| Tekan-tahan pada notifikasi | Buka context menu |
| Ucapkan "Hei Nyx" *(kalau Wake Word diaktifkan)* | Buka AI Chat & mulai merekam |

---

## 🕹️ Emulator NES & DOOM

- **NES**: berjalan di task FreeRTOS terpisah (stack 48KB di PSRAM) supaya tidak membekukan UI. Buka app NES → firmware scan semua file `.nes` di root SD Card → pilih dari daftar.
- **DOOM**: port `doomgeneric` lewat ESP-DOOM, juga di task terpisah yang memegang SPI display langsung. Non-audio secara sengaja (`DOOM_NO_AUDIO`). Render 320×200 di tengah layar 320×240. WAD **tidak disertakan** karena isu hak cipta — taruh file WAD milikmu (mis. `doom1.wad` shareware) di root SD Card. Selama DOOM/NES berjalan, orientasi layar dipaksa sesuai kebutuhan lalu dikembalikan setelah keluar.

---

## 🎞️ Format Video MJPEG

Pemutar video memakai parser MJPEG murni (`MjpegClass.h` + `JPEGDEC`). Konversi video biasa dengan ffmpeg:

```bash
ffmpeg -i input.mp4 -vf "fps=15,scale=240:320" output.mjpeg
```

> ⚠️ Video di-encode untuk resolusi native panel **240×320 portrait** — pemutar memaksa rotasi portrait selama playback lalu mengembalikannya setelah selesai. Tap = pause/lanjut dari posisi mm:ss terakhir, tahan ≥600ms = berhenti total.

---

## 🔀 Firmware Alternatif: Fork Bruce

Repo ini juga menyertakan `Bruce-accretion-phone.bin` — build biner dari fork [Bruce](https://github.com/pr3y/Bruce) dengan board port khusus untuk hardware **Accretion yang sama** (XPT2046 di GPIO 6/5/4/9, ILI9341). Ini firmware terpisah/alternatif, bukan bagian dari OS Accretion/`phone.ino`. Flash lewat app **Update** atau esptool. Catatan: board port ini memakai konstanta kalibrasi touch generik dan **tidak** punya sistem kalibrasi sendiri seperti `phone.ino`.

---

## ⚠️ Hal-Hal yang Perlu Diketahui

- **RAM internal ESP32-S3 itu kecil (~162KB)**, dan koneksi HTTPS (Gemini/NASA) butuh blok memori kontigu yang cukup besar. Error `HTTP -1` kadang bukan soal jaringan, tapi RAM internal lagi terpecah-pecah. Firmware punya mekanisme tunggu & coba-ulang otomatis, dan **HWmonitor** menampilkan "blok terbesar" real-time untuk diagnosis. Ada juga *App Memory Guard* yang melepas cache RAM app astronomi saat RAM mepet.
- **PSRAM itu wajib**, bukan opsional — dipakai buffer decode MJPEG, stack task NES/DOOM/Musik/Wake Word, dan objek JPEGDEC.
- **Audio hanya lewat Bluetooth.** Tidak ada speaker/output audio langsung di S3 (jalur MAX98357A sudah dicabut). Musik berjalan lewat ESP32-CAM ke TWS, jadi tanpa CAM dan TWS app Musik tidak menghasilkan suara.
- **Wake Word bukan neural net.** Ia mengenali suara dan kata *kamu sendiri* (speaker-dependent) dari 4 rekaman. Di ruang bising atau kata yang mirip bisa salah picu atau meleset. Mic dipakai bergantian dengan dikte AI Chat dan Mic Level.
- **NyxPPU masih fase 1**: baru dipakai di app PPU Demo, belum dipasang ke Inferno atau Home Screen.
- Estimasi baterai murni dari pembacaan tegangan ADC (bukan fuel-gauge IC), anggap perkiraan kasar.
- `DEMO_KEY` NASA limitnya kecil — kalau sering dipakai, ambil key pribadi (gratis).
- `phone_notif_center.ino` sengaja dinamai begitu supaya urutan compile alfabetis Arduino menaruhnya setelah `phone.ino`, sehingga definisi global `phone.ino` otomatis terlihat tanpa `extern`.
- Library `lvgl` masuk daftar instalasi CI tapi belum dipakai; mesin render saat ini sepenuhnya LovyanGFX.

---

## 📄 Lisensi

**GNU Affero General Public License v3.0 (AGPL-3.0)** — lihat berkas [`LICENSE`](./LICENSE) untuk teks lengkap.

---

<p align="center"><i>Dibangun sepotong demi sepotong, satu bug demi satu bug, sampai jadi "HP" yang beneran bisa dipakai. 🔋📲</i></p>
