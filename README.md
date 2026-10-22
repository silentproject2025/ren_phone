# 📱 Accretion Phone · NyxOS

**Ponsel buatan sendiri dengan sistem operasi yang ditulis dari nol, berjalan di satu chip ESP32-S3.**

`ESP32-S3` · `PSRAM 8 MB` · `ILI9341 320×240` · `41 aplikasi` · `±34.000 baris firmware` · `AGPL-3.0`

> ⚠️ **PROYEK AKTIF — ADA FITUR BETA.**
> Sebagian fitur di repo ini **masih tahap Beta**: sudah bisa dipakai, tetapi **belum diuji menyeluruh di perangkat asli** dan **bug-nya mungkin masih ada**. Daftar lengkapnya ada di bagian [Fitur Beta](#-fitur-beta--baca-ini-dulu). Kalau ada yang aneh, itu belum tentu salah kabel kamu.

---

## 🧭 Daftar Isi

1. [Apa ini?](#-apa-ini)
2. [Cara membaca status fitur](#-cara-membaca-status-fitur)
3. [Fitur Beta — baca ini dulu](#-fitur-beta--baca-ini-dulu)
4. [Perangkat keras](#-perangkat-keras)
5. [Aplikasi](#-aplikasi)
6. [Cara pasang & build](#-cara-pasang--build)
7. [Konfigurasi di SD card](#-konfigurasi-di-sd-card)
8. [Cara kerja di balik layar](#-cara-kerja-di-balik-layar)
9. [Batasan yang sudah diketahui](#-batasan-yang-sudah-diketahui)
10. [Melapor bug](#-melapor-bug)
11. [Struktur repo & lisensi](#-struktur-repo--lisensi)

---

## 🌌 Apa ini?

**Accretion Phone** adalah ponsel DIY. **NyxOS** adalah firmware-nya: launcher, notifikasi, Control Center, Dynamic Island, jaringan, emulator, dan mesin grafis, semuanya ditulis sendiri di atas FreeRTOS bawaan ESP32. Tidak ada Android atau Linux di dalamnya.

Tantangan terbesarnya adalah memori. RAM internal cuma **±162 KB**, sementara satu koneksi HTTPS saja butuh blok kosong sekitar 20–30 KB yang bersambung. Banyak keputusan desain di repo ini adalah jawaban atas batas itu.

> Folder dan repo bernama `ren_phone` karena itu nama awal proyek. Sebagian komentar dan variabel lama masih memakai nama tersebut.

---

## 🚦 Cara membaca status fitur

| Tanda | Arti |
|---|---|
| 🟢 **Stabil** | Sudah dipakai harian di perangkat asli |
| 🟡 **Beta** | Jalan, tapi belum banyak diuji. **Bug mungkin ada.** |
| 🔴 **Eksperimen** | Baru kerangka atau uji coba. Jangan diandalkan |

---

## 🧪 Fitur Beta — baca ini dulu

Fitur di tabel ini **bisa saja bermasalah**: macet, restart sendiri, suara putus, atau tidak jalan di jaringan tertentu. Fitur di luar tabel ini umumnya sudah lebih matang, tapi namanya perangkat DIY, tetap jangan kaget kalau ada kejutan.

| Fitur | Status | Yang perlu kamu tahu |
|---|---|---|
| **Telepon** (SIP + antar-HP lewat WiFi) | 🟡 Beta | **Belum diuji di HP asli**; baru dicek sintaks dan codec/parser di PC. Tanpa ICE, panggilan lewat internet bisa satu arah/bisu di NAT tertentu. Suara belum dienkripsi |
| **Chat** (lewat broker MQTT) | 🟡 Beta | **Belum dicoba dengan broker sungguhan.** Koneksi TLS tidak memverifikasi sertifikat. Pesan bisa bertabrakan kalau dua HP kirim di detik yang sama |
| **Remote Control** (WiFi AP + web) | 🟡 Beta | Menyalakan AP memakan RAM internal, jadi fitur HTTPS (Gemini, NASA, Trivia) bisa kena `HTTP -1`. Layar NES/DOOM/video tidak ikut tampil di web (beku) |
| **Gamepad web** untuk NES & DOOM | 🟡 Beta | Butuh library `WebSockets`. Kontrol DOOM lengkap (look, senjata, F-key) masih baru |
| **Update OTA per channel (branch)** | 🟡 Beta | Pengaman rollback baru aktif untuk update yang dilakukan oleh firmware yang *sudah* memuat kode ini. Repo harus **public** |
| **Musik Bluetooth + AVRCP** | 🟡 Beta | MP3 bitrate tinggi (mis. 192 kbps) pernah membuat ESP32-CAM restart. Bitrate rendah stabil. Masih diperbaiki |
| **Pemutar AVI: seek & zoom** | 🟡 Beta | Seek butuh `idx1`. MP3 VBR bisa bergeser setelah lompat jauh. Zoom bisa menurunkan fps (ada tombol untuk mematikan) |
| **Wake Word "Hei Nyx"** | 🟡 Beta | Bukan neural network; sensitif terhadap ruangan bising |
| **SPI Overclock layar** (Pengaturan) | 🟡 Beta | Kalau layar bergaris atau warnanya acak setelah dinaikkan, turunkan lagi. Biasanya itu batas kabel fisik, bukan salah kode |
| **NyxPPU** (mesin piksel ala konsol) | 🔴 Eksperimen | Baru fase 1 (app *PPU Demo*). **Belum terpasang** di Inferno maupun Home/Lock Screen |

### Kalau ketemu masalah di fitur Beta

1. Catat yang kamu lakukan sebelum masalah muncul.
2. Buka **HWmonitor** dan lihat RAM internal serta *largest free block*. Banyak bug di sini ujungnya soal memori.
3. Kalau `HTTP -1` muncul, matikan AP Remote dulu, lalu coba lagi.
4. Kalau bisa, sambungkan Serial Monitor dan simpan lognya.
5. Laporkan lewat [Issues](#-melapor-bug).

---

## 🔧 Perangkat keras

| Bagian | Detail |
|---|---|
| Otak | ESP32-S3, flash 16 MB, PSRAM OPI 8 MB |
| Layar | ILI9341 320×240 (SPI2), portrait dan landscape |
| Sentuh | XPT2046 di bus SPI3 terpisah |
| Penyimpanan | MicroSD (SDIO 1-bit) + FFat internal sebagai cadangan |
| Sensor | MPU6050 (tilt, shake, auto-rotate) · INA219 (tegangan, arus, daya) |
| Mikrofon | INMP441 (I2S) |
| Audio keluar | **Hanya Bluetooth (A2DP)**, lewat ko-prosesor **ESP32-CAM**. Tidak ada speaker |
| Lain-lain | Motor getar, Neopixel, USB OTG/Host pada GPIO 19 & 20 |
| Partisi | `app3M_fat9M_16MB` |

### Peta pin

| Blok | Sinyal → GPIO |
|---|---|
| Layar | SCLK 12 · MOSI 11 · MISO 13 · DC 2 · CS 10 · RST 14 · BL 21 |
| Sentuh | SCLK 6 · MOSI 5 · MISO 4 · CS 9 |
| SD (SDIO 1-bit) | CLK 39 · CMD 38 · D0 40 |
| Mikrofon | SCK 47 · WS 46 · SD 45 |
| Link ke CAM | S3 TX 41 → CAM RX 13 · S3 RX 42 ← CAM TX 14 · GND bersama |
| MPU6050 + INA219 | SDA 15 · SCL 7 (satu bus I2C) |
| Lainnya | Motor getar 18 · Neopixel 48 |

> **ESP32-CAM:** beri daya lewat pin **5V**, dan **cabut kartu SD-nya** karena GPIO 13/14 dipakai bersama slot itu.

### Baterai

Pembacaan daya memakai **INA219** (shunt 100 mΩ, gain /8 sebagai bawaan). Persen baterai adalah **estimasi** dari kurva tegangan, kompensasi resistansi dalam, dan hitungan arus (coulomb counting), bukan chip fuel-gauge khusus. Isi nilai kapasitas dan R dalam dengan benar di app Baterai supaya angkanya masuk akal. Kalau INA219 tidak terdeteksi, persen dan tegangan berhenti diperbarui.

---

## 📲 Aplikasi

Total **41 aplikasi** dan **7 tema**.

| Kelompok | Isi |
|---|---|
| **Sistem** | Jam, Pengaturan, Update (OTA), Baterai, HWmonitor, Orientasi3D, Mic Level, Neopixel, Profil, About Phone, Atur Home |
| **Produktivitas** | Kalkulator, Notepad, Canvas, Files, Pomodoro, Stopwatch, Voice Memo |
| **Media** | Musik (BT), MJPEG/AVI, Galeri |
| **Online & AI** | AI Chat (Gemini), Trivia, Astronomi (APOD), NEO Asteroid, Bumi EPIC, Galeri NASA |
| **Komunikasi** 🟡 | Chat (MQTT), Telepon (SIP/LAN) |
| **Kendali jarak jauh** 🟡 | Remote (app di layar). Gamepad web `/pad` bukan app, jadi tidak ikut dihitung |
| **Suara** 🟡 | Wake Word "Hei Nyx" |
| **Permainan** | Snake, Flappy, 2048, TicTacToe, Breakout, Labirin, Inferno, NES, DOOM, PPU Demo 🔴 |

### Antarmuka sistem

Lock Screen, Home (grid + pencarian), Control Center, Notification Shade, Dynamic Island, App Switcher, Always-On Display, badge notifikasi, keyboard layar, dan wallpaper kustom. Getaran sengaja dibuat minimal.

### Yang layak diketahui lebih dalam

- **Galeri** — album adalah folder di SD (maksimal 2 tingkat). Thumbnail 72×72 dibuat sekali lalu disimpan di `/.thumbs/`, jadi pembukaan berikutnya tidak perlu decode gambar penuh. Mendukung JPG dan PNG.
- **Pemutar AVI** — video MJPEG + audio MP3 dalam satu file `.avi`. Suara keluar lewat TWS, dan jam utama mengikuti posisi audio supaya gambar tidak mendahului suara. Posisi terakhir tiap video diingat.
- **Musik** — cover diambil dari ID3 tag (hanya JPEG baseline), mode acak biasa atau AI (RYNE v2), dan kontrol dari tombol earbuds.
- **AI Chat** — Gemini lewat HTTPS, input teks atau dikte suara.
- **Inferno** — FPS raycaster bertema gothic dengan musuh, boss, senjata berjenjang, dan ammo terbatas.
- **NES / DOOM** — core Nofrendo dan doomgeneric. File WAD DOOM disediakan sendiri, dan DOOM tanpa suara.
- **Wake Word** 🟡 — pengenalan suara on-device (MFCC + DTW), tanpa internet. Kamu merekam 4 sampel kata sendiri.

---

## 🛠️ Cara pasang & build

### Paling cepat: ambil dari GitHub Actions

1. Fork repo ini.
2. Jalankan workflow build, lalu unduh `.bin` dari artifact.
3. Flash ke ESP32-S3.

### Build manual (S3)

1. Pasang board package **esp32 by Espressif 3.x**, lalu library `LovyanGFX`, `JPEGDEC`, `PubSubClient`, dan `WebSockets`.
2. Satukan semua `*.ino` dan header di root ke satu folder sketch, dengan `phone.ino` sebagai file utama.
3. Salin `nofrendo_core/` dan `esp_doom_core/` ke `src/` (patch kecil yang diperlukan ada di workflow CI).
4. Tambahkan flag mbedTLS:
   ```
   -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1
   -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
   ```
5. Pilih `esp32:esp32:esp32s3`, `FlashSize=16M`, `PSRAM=opi`, `PartitionScheme=app3M_fat9M_16MB`.
6. Compile, unggah, lalu ikuti kalibrasi sentuh saat boot pertama.

### Build ko-prosesor (ESP32-CAM)

Folder `cam_bt_coprocessor/`, board **AI Thinker ESP32-CAM**, partisi **Huge APP**, PSRAM aktif. Library: `audio-tools`, `arduino-libhelix`, `ESP32-A2DP`. Pastikan `rplink.h` **identik** di kedua sisi.

### Memasang fitur lewat patch

Banyak fitur ditambahkan sebagai pasangan `*_renphone.ino` + `apply_*.py`. Jalankan dari root repo, skrip aman dijalankan ulang:

```bash
python3 apply_call_patch.py        # contoh: Telepon (Beta); aman dijalankan ulang
git add -A && git commit -m "Telepon SIP + LAN" && git push
```

> **Catatan CI:** file `.ino` baru harus ikut didaftarkan di langkah `cp` pada `build-firmware.yml`, kalau tidak, file itu tidak ikut dikompilasi. Versi `arduino-cli`, core ESP32, dan library di CI belum dikunci, jadi update dari upstream bisa memecahkan build.

---

## 💾 Konfigurasi di SD card

```
/
├── gemini_key.txt   nasa_key.txt   wallpaper.jpg
├── chat.cfg         sip.cfg                      ← fitur Beta
├── music/    memo/    screenshot/    update/    .thumbs/
├── *.avi  *.mjpeg  *.nes    doom1.wad (sediakan sendiri)
└── ai_memory.txt  ryne2.bin  notepad.txt ...
```

**API key.** `/gemini_key.txt` dipakai AI Chat dan dikte. `/nasa_key.txt` dipakai NEO, EPIC, dan Galeri NASA (`DEMO_KEY` boleh, tapi limitnya kecil). Astronomi (APOD) tidak butuh key.

**Chat (Beta).** Pakai broker MQTT dengan TLS port 8883. Isi lewat app, atau taruh `/chat.cfg`:
```
host=xxxxxxxx.ala.eu-central-1.emqxsl.com
user=namauser
pass=passwordnya
name=Ren
```
Panduan lengkap ada di [`chat-mqtt/README.md`](./chat-mqtt/README.md). Jangan pakai HiveMQ Cloud Serverless gratis, karena layanannya dihentikan.

**Telepon (Beta).** Akun SIP gratis di Linphone (`sip.linphone.org`), atau mode LAN antar-HP NyxOS tanpa server. Isi lewat app Telepon atau `/sip.cfg`:
```
host=sip.linphone.org
user=...
pass=...
name=...
```
Telepon hanya bisa ke alamat SIP lain, **bukan nomor HP biasa**. Suara masuk wajib lewat TWS, dan mic dipakai bersama fitur lain, jadi tutup Voice Memo/Mic Level sebelum menelepon.

Patch Telepon ikut mengubah `rplink.h` (kedua salinan) dan firmware CAM. Flash ulang CAM bersifat opsional, tapi bikin suara sekitar 140 ms lebih cepat.

**Remote Control (Beta).** Nyalakan AP dari app *Remote*, sambungkan HP/laptop lain ke WiFi-nya, lalu buka `http://192.168.4.1/remote`. Gamepad ada di `http://192.168.4.1/pad`. AP memakai WPA2 dengan sandi yang dibuat otomatis, dan hanya melayani koneksi yang masuk lewat AP (kecuali kamu mengizinkan WiFi rumah).

**Web File Manager.** Saat WiFi aktif, buka `http://<IP-perangkat>/` untuk upload, edit, unduh, hapus, dan ganti wallpaper (nama berkas harus persis `wallpaper.jpg`).

**Membuat video.**
```bash
ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
       -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k output.avi
```
Audio **wajib MP3 44,1 kHz**, sebaiknya CBR (`-b:a 128k`).

---

## ⚙️ Cara kerja di balik layar

**Disiplin memori**

| Aturan | Penerapan |
|---|---|
| Buffer besar jangan di RAM internal | Sprite layar, cache, dan buffer decode dialokasikan di PSRAM |
| Stack task berat dipindah ke PSRAM | `createTaskPsramStack()` untuk Gemini, NES, Musik, Wake Word, Chat |
| Tunggu sebelum menyerah | Ada jeda dan coba-ulang supaya lwIP sempat melepas buffer TLS sebelumnya |
| App jaringan bisa dikorbankan | **App Memory Guard** melepas cache aplikasi latar saat RAM menipis |
| mbedTLS diperkecil | Flag compile di atas |

**Render.** Frame digambar ke sprite di PSRAM lalu didorong ke panel (LovyanGFX). Transisi berbasis waktu, bukan jumlah frame. Efek kaca buram adalah tiruan murah: sampel warna dari piksel yang sudah tergambar dicampur dengan tint tema.

**Ko-prosesor Bluetooth.** MP3 dibaca dari SD, dikirim lewat UART 921600 ke ESP32-CAM, yang men-decode dan mengirim A2DP ke TWS. Format frame `[0xA5][type][lenLo][lenHi][payload ≤1024][xor]`. Volume absolut AVRCP sengaja tidak diiklankan karena pernah membuat CAM mati oleh watchdog. Saklar `RP_AVRCP_ENABLE` tersedia untuk mengisolasi masalah.

**Update OTA per channel.** App Update memilih *branch* sebagai sumber firmware, membaca `info.txt` dari release, memeriksa ukuran, header image, dan MD5, lalu memasang. Firmware dari basis kode yang berbeda ditolak. Ada pengaman boot: kalau firmware baru tidak sehat sampai 3 kali boot, HP kembali ke slot sebelumnya.

**NyxPPU 🔴.** Mesin piksel ala konsol retro (tile bank 8×8, 2 layer background berparalaks, hingga 32 sprite). Rencananya dipakai di game dan UI, tapi saat ini baru bisa dicoba lewat *PPU Demo*.

---

## 🧱 Batasan yang sudah diketahui

- **RAM internal sempit.** `HTTP -1` sering berarti fragmentasi memori, bukan jaringan. Ada mekanisme tunggu dan coba-ulang, tapi belum bisa dihilangkan sepenuhnya dari level sketch.
- **Audio hanya lewat Bluetooth.** Tanpa ESP32-CAM dan TWS, tidak ada suara.
- **Bitrate MP3 tinggi** pernah memicu restart CAM (lihat tabel Beta).
- **PNG** dibatasi 4 MB per file dan 8 juta piksel; di atas 2 MB tidak punya thumbnail.
- **AVI OpenDML (> 1 GB)** tidak didukung untuk seek/resume.
- **Layar Chat hanya ASCII**; aksen dan emoji tampil sebagai `?`.
- **Semua koneksi TLS** memakai `setInsecure()` (sertifikat server tidak diverifikasi). Jangan kirim data sensitif.
- **Telepon:** latensi satu arah diperkirakan 300–500 ms, UDP tanpa enkripsi.
- **Tidak ada fallback ADC** untuk baterai kalau INA219 tidak terdeteksi.

---

## 🐞 Melapor bug

Karena banyak bagian masih Beta, laporan kamu sangat membantu. Sertakan:

- fitur yang bermasalah dan **langkah yang membuatnya muncul**,
- versi/branch firmware (lihat app *About Phone* atau *Update*),
- foto layar HWmonitor (RAM internal, largest block),
- log Serial Monitor kalau ada,
- apakah AP Remote atau Bluetooth sedang menyala.

---

## 🗂️ Struktur repo & lisensi

```
phone.ino                    inti NyxOS (UI, boot, jaringan, app dasar, game)
phone_notif_center.ino       notifikasi / Dynamic Island
*_renphone.ino               satu file per fitur (galeri, avi, musik, remote,
                             chat, call, ina219, about, owner, homeedit, ota, dst)
ppu_engine_renphone.ino      NyxPPU
ryne_engine.h                RYNE v2 (rekomendasi lagu on-device)
rplink.h  MjpegClass.h       protokol link CAM, parser MJPEG
cam_bt_coprocessor/          firmware ESP32-CAM
chat-mqtt/                   panduan broker MQTT untuk Chat
nofrendo_core/ esp_doom_core/  core pihak ketiga (di-vendor)
apply_*.py  patch_*.py       skrip patch bertahap (idempoten)
ci_hoist_types.py            langkah CI untuk menghindari bug prototype arduino-cli
.github/workflows/           build S3, build CAM, flasher web
Bruce-accretion-phone.bin    firmware alternatif (fork Bruce), terpisah dari NyxOS
```

Riwayat perubahan ditulis sebagai komentar langsung di kode: gejala, akar masalah, dan alasan perbaikan. Kalau penasaran kenapa sebuah bagian ditulis begitu, mulai dari komentar di atasnya.

**Lisensi:** [AGPL-3.0](./LICENSE). Kode pihak ketiga (Nofrendo, ESP-DOOM/doomgeneric) mengikuti lisensinya masing-masing.

---

<sub>Proyek hobi, dibuat dan diuji di perangkat sendiri. Pakai dengan sadar bahwa fitur Beta bisa berubah atau rusak sewaktu-waktu.</sub>
