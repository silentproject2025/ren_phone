<div align="center">

```
        .  *  .        .      *       .
    .       ___---===[ ◉ ]===---___       .
        .--'   accretion disk   '--.
   *   (   ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   )     .
        '--.___               ___.--'
   .          '''---=======---'''       *
        .      *        .       .
```

# ACCRETION PHONE
### NyxOS — sistem operasi ponsel yang ditulis dari nol untuk ESP32-S3

`C++ / Arduino-ESP32 3.x` · `ESP32-S3 + PSRAM OPI` · `ILI9341 320×240` · `±26.500 baris firmware` · `AGPL-3.0`

</div>

---

## 00 — Premis

Ponsel ini tidak menjalankan Android, Linux, atau RTOS siap pakai dengan lapisan UI di atasnya. **NyxOS** adalah satu firmware monolitik: launcher, window transition, notifikasi, jaringan, emulator, dan mesin grafisnya ditulis sendiri di atas FreeRTOS bawaan ESP32.

Targetnya bukan "demo yang bisa jalan", tetapi perangkat yang dipakai harian dengan batasan keras: **162 KB RAM internal**, satu layar SPI, satu titik sentuh. Hampir seluruh keputusan desain di repo ini adalah jawaban atas batasan itu.

Nama diambil dari fisika lubang hitam: *accretion* adalah piringan materi yang berputar ke dalam, dan *Nyx* adalah dewi malam. Boot sequence-nya mengikuti tema itu (partikel spiral, *Event Horizon*, lalu logo NyxOS muncul dari garis cahaya).

> Folder/repo bernama `ren_phone` karena itu nama awal proyek. Beberapa komentar dan variabel lama masih memakai nama tersebut.

---

## 01 — Spesifikasi

| Bagian | Detail |
|---|---|
| SoC | ESP32-S3, flash 16 MB, PSRAM 8 MB (OPI) |
| Layar | ILI9341 320×240, SPI2, portrait & landscape |
| Sentuh | XPT2046 pada bus SPI3 terpisah |
| Penyimpanan | MicroSD via SDIO 1-bit, plus FFat internal sebagai cadangan |
| Sensor | MPU6050 (tilt, shake, auto-rotate) |
| Audio masuk | Mikrofon INMP441 (I2S) |
| Audio keluar | Bluetooth A2DP lewat **ko-prosesor ESP32-CAM** |
| Lain-lain | Motor getar, LED Neopixel, ADC baterai (voltage divider) |
| Partisi | `app3M_fat9M_16MB` |
| Jumlah aplikasi | 38 |
| Tema | 7 (Aurora, Dark, AMOLED, Light, Pastel, iOS, Full Dark) |

---

## 02 — Arsitektur

### 2.1 Peta memori

Ini inti dari seluruh proyek. RAM internal hanya ±162 KB, dan sebuah koneksi HTTPS (TLS) membutuhkan satu blok **kontigu** sekitar 20–30 KB. Tanpa disiplin memori, error `HTTP -1` dan reboot acak adalah hasil yang pasti.

| Aturan | Penerapan |
|---|---|
| Buffer besar tidak boleh di RAM internal | Sprite layar, tile bank, buffer decode, dan cache dialokasikan lewat `heap_caps_malloc(MALLOC_CAP_SPIRAM)` |
| Stack task berat dipindah ke PSRAM | Dibuat lewat `createTaskPsramStack()` (`xTaskCreateStaticPinnedToCore`): task Gemini, NES, Musik, Wake Word |
| Tunggu sebelum menyerah | `nasaWaitForHeap` / `nasaHeapReadyForTask()` memberi waktu lwIP melepas buffer TLS sebelumnya |
| Aplikasi berbasis jaringan bisa "dikorbankan" | **App Memory Guard** melepas cache aplikasi latar (Astronomi, NEO, EPIC, Galeri NASA) ketika sisa RAM menipis |
| Flag mbedTLS diperkecil saat compile | `CONFIG_MBEDTLS_DYNAMIC_BUFFER`, `ASYMMETRIC_CONTENT_LEN`, buffer in/out 4096/2048 |

Diagnosis dilakukan langsung di perangkat lewat **HWmonitor**: beban per-core, RAM internal, PSRAM, dan *largest free block*.

### 2.2 Pembagian task

```
Core 0                              Core 1
──────────────────────              ──────────────────────
loop()  → UI, input, redraw         doomTask      (DOOM, memegang SPI layar)
musTask → UART ke ESP32-CAM         nofrendo task (NES, stack 48 KB di PSRAM)
Gemini task (stack PSRAM)
Wake word / mic task
```

UI tetap di `loop()` supaya urutan gambar deterministik. Pekerjaan blocking (HTTP, decode, emulator) dipindah ke task terpisah dan UI hanya membaca statusnya.

### 2.3 Pipeline render

- **LovyanGFX** sebagai mesin grafis; frame digambar ke sprite di PSRAM lalu didorong ke panel.
- **Transisi berbasis waktu** (`millis()`), bukan jumlah frame, sehingga durasi konsisten (±170 ms) walau CPU sedang sibuk. Animasi Control Center dan Notification Shade memakai easing yang sama.
- **Band render**: saat Home hanya di-scroll, header, status bar, dan dock tidak digambar ulang.
- **Kaca buram palsu** (`glassSampleAvg`, `drawGlassPanel`, `drawGlassCircle`): tidak ada blur sungguhan. Firmware mengambil sampel warna dari piksel yang sudah tergambar, mencampurnya dengan tint tema, lalu menambah highlight dan bayangan. Jauh lebih murah daripada blur gaussian dan cukup meyakinkan di layar 2,x inci.
- **Kalibrasi sentuh dan rotasi selalu sinkron**: rotasi tersimpan dipasang *sebelum* boot sequence dan kalibrasi, karena XPT2046 memetakan sumbu mengikuti rotasi aktif saat kalibrasi (akar bug swipe-to-unlock di v9).

### 2.4 NyxPPU — mesin piksel ala konsol retro

`ppu_engine_renphone.ino` meniru konsep PPU konsol lama, disesuaikan untuk ESP32-S3:

| Komponen | Spesifikasi |
|---|---|
| Tile bank | 40 tile 8×8 RGB565 di PSRAM |
| Background | 2 layer nametable 48×36 tile (384×288 piksel virtual), masing-masing punya scroll sendiri → parallax |
| Sprite (OAM) | hingga 32 objek: skala, flip, tint, dan bit prioritas (di depan atau di belakang BG1) |
| Transparansi | magenta `0xF81F` sebagai color key sprite; tile id 0 di BG1 berarti lubang |

Status: **fase 1**. Mesin sudah nyata dan bisa dicoba lewat app *PPU Demo*; integrasi ke Inferno dan Home/Lock Screen masih rencana.

### 2.5 Ko-prosesor Bluetooth (RPLink)

ESP32-S3 tidak memakai Bluetooth untuk audio. MP3 dibaca dari SD, dikirim lewat UART ke **ESP32-CAM**, yang melakukan decode (libhelix) dan mengirim A2DP ke TWS.

```
SD ─▶ S3 (master) ──UART 921600 8N1──▶ ESP32-CAM ──A2DP──▶ TWS
      UI, playlist, RYNE                decode MP3 + buffer PCM
```

Format frame (`rplink.h`, **harus identik di kedua sisi**):

```
[0xA5][type][lenLo][lenHi][payload ≤1024][xor]
xor = type ^ lenLo ^ lenHi ^ semua byte payload
```

| Arah | Perintah |
|---|---|
| S3 → CAM | `P` begin · `D` data · `F` finish · `U` pause · `V` volume · `X` stop · `A` scan · `C` connect · `T` tx-gain · `B` mode BT · `G` ping |
| CAM → S3 | `S` status (40 ms) · `E` lagu habis · `L` hasil scan · `W` scan selesai · `R` hello (CAM boot) · `K` tombol earbuds (AVRCP) |

Catatan penting: **volume absolut AVRCP sengaja tidak diiklankan.** Saat diiklankan, sebagian earbuds mendaftarkan notifikasi yang tidak dijawab callback, dan CAM mati oleh *task watchdog* beberapa detik setelah lagu mulai. Saklar `RP_AVRCP_ENABLE` tersedia untuk mengisolasi masalah memori/link.

---

## 03 — Subsistem

### Antarmuka

Lock Screen, Home (grid + pencarian), Control Center, Notification Shade, Dynamic Island, App Switcher, Always-On Display, badge notifikasi, keyboard layar, dan wallpaper kustom (`wallpaper.jpg`, berlaku di Home dan Lock Screen). Getaran dibuat minimal dan hanya untuk kejadian yang perlu, bukan tiap ketukan.

Control Center berisi tombol bulat (WiFi, Airplane, DND, Tema, Orientasi, Shake, Kunci, Neopixel, Senyap, Bluetooth, Tangkap layar) dan slider kecerahan.

### Aplikasi (38)

| Kategori | Isi |
|---|---|
| Sistem | Jam, Setting, Update (OTA), Baterai, HWmonitor, Orientasi3D, Mic Level, Neopixel, Profil, About Phone, Atur Home |
| Produktivitas | Kalkulator, Notepad, Canvas, Files, Pomodoro, Stopwatch, Voice Memo |
| Media | Musik (BT), MJPEG, Galeri |
| AI & online | AI Chat (Gemini), Trivia, Astronomi (APOD), NEO Asteroid, Bumi EPIC, Galeri NASA |
| Suara | Wake Word "Hei Nyx" |
| Permainan | Snake, Flappy, 2048, TicTacToe, Breakout, Labirin, Inferno, NES, DOOM, PPU Demo |

### Yang layak dijelaskan lebih dalam

- **Galeri** — album = folder di SD (kedalaman maksimal 2). Thumbnail 72×72 dibuat sekali, disimpan sebagai `/.thumbs/<xx>/<hash>.th` (RGB565, ±10 KB), dan berikutnya cukup dibaca tanpa decode. Nama cache adalah hash dari path + ukuran + waktu ubah, jadi gambar yang diganti otomatis dibuat ulang. Pembuatan thumbnail dibatasi satu gambar per putaran loop dan hanya saat jari tidak menyentuh layar. **Format gambar: JPG dan PNG.** PNG di-decode dengan decoder bawaan LovyanGFX (helper `rpPng*` di `phone.ino`), dengan batas 4 MB per file dan 8 juta piksel; PNG di atas 2 MB tetap bisa dibuka tetapi tanpa thumbnail. Astronomi (APOD) memakai helper PNG yang sama.
- **Pemutar AVI** (`avi_renphone.ino`) — video MJPEG + audio MP3 dalam satu file `.avi`, suaranya keluar lewat TWS. Video di-decode JPEGDEC ke layar; byte MP3 hasil demux masuk ring PSRAM lalu dikirim `musTask` ke ESP32-CAM lewat jalur RPLink yang sama dengan app Musik. **Jam utama = posisi putar audio di CAM** (dikurangi `AVI_BT_LATENCY_MS`), jadi frame yang telat dilewati dan suara tidak pernah tertinggal. Antrean frame di PSRAM menjaga audio tetap punya cadangan saat decode sesekali melambat. TWS belum tersambung / audio bukan MP3 → video tetap main tanpa suara. Taruh `.avi` di root SD (muncul di app MJPEG atau Files). Cara bikin file:

  ```
  ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
         -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k output.avi
  ```
  Audio **wajib MP3 44,1 kHz** (sampling lain bikin nada bergeser). Video portrait (mis. `scale=240:320`) diputar portrait, landscape diputar landscape.

  **Daftar video.** App MJPEG menampilkan tiap file sebagai kartu: thumbnail 64×48 (frame di ±10% video, atau frame pertama untuk `.mjpeg`), durasi di pojok thumbnail, ukuran file, dan status **Lanjut mm:ss** beserta garis progres ditonton di dasar thumbnail. Ketuk di mana saja pada kartu untuk memutar. Thumbnail dibuat satu per satu di latar saat daftar dibuka lalu di-cache di SD (`/.thumbs/<hash>.th`, ±6 KB per video), jadi pembukaan berikutnya langsung muncul, juga setelah reboot. Ketuk **Scan** untuk memuat ulang daftar; hapus folder `.thumbs` bila ingin membuat ulang semua thumbnail.

  **Kontrol**

  | Gestur | Fungsi |
  |---|---|
  | Ketuk | Buka panel kontrol dan pause (lihat di bawah) |
  | Usap kanan / kiri | Maju / mundur 10 detik (`AVI_SEEK_STEP_MS`) |
  | Usap atas / bawah di separuh kiri layar | Kecerahan (video tetap jalan) |
  | Usap atas / bawah di separuh kanan layar | Volume TWS (video tetap jalan) |
  | Tahan 0,6 dtk | Berhenti |
  | Usap tepi bawah ke atas | Home |

  Saat memutar, garis progres tipis selalu terlihat di tepi bawah layar, dan perubahan posisi / kecerahan / volume muncul sebagai OSD kecil di pojok kiri atas.

  **Panel kontrol** (muncul saat ketuk, video pause): bar judul dengan tombol Home dan Tutup; slider kecerahan di kiri dan slider volume di kanan; di bawah ada waktu, bar progres yang bisa digeser, serta tombol −10 / Play / +10. Loncat dari panel (tombol atau bar) tetap dalam keadaan pause di frame tujuan, tekan Play atau ketuk area video untuk lanjut. Slider kecerahan mengubah kecerahan layar sistem (nilai yang sama dengan Control Center); slider volume memakai volume yang sama dengan app Musik.

  **Seek.** Indeks `idx1` di akhir file dibaca sekali saat video dibuka (sekitar satu detik), lalu dibuat tabel titik lompat tiap ±1 detik video di PSRAM (maks 8192 titik, 64 KB): offset chunk video dan waktu audio di titik itu. Karena MJPEG berisi keyframe di setiap frame, lompat bisa ke mana saja tanpa artefak. Tiap lompat, stream audio di CAM di-restart (`musExtEnd` + `musExtBegin`) dan jam audio diberi offset awal supaya sinkron gambar-suara tetap benar. Akurasi lompat ±1 detik.

  **Resume.** Posisi terakhir disimpan di NVS (namespace `avires`, 8 video terakhir, urutan terbaru dulu) saat pause, saat keluar, dan tiap 30 detik. Kunci video = hash nama file + ukuran file. Buka video yang sama lagi → muncul pilihan **Awal** / **Lanjut**. Posisi di bawah 5 detik dari awal atau di 5 detik terakhir tidak disimpan, dan video yang tamat menghapus catatannya. Ganti nama atau isi file = dianggap video baru.

  File tanpa `idx1` tetap diputar normal; hanya seek dan resume yang mati (muncul toast "Tanpa indeks").
- **Wake Word** — pengenalan suara speaker-dependent berbasis MFCC + DTW, sepenuhnya on-device dan tanpa internet. Perekaman 4 sampel kata sendiri. Ini bukan neural network, jadi sensitif terhadap ruangan bising.
- **RYNE v2** (`ryne_engine.h`, header-only, tanpa library Arduino) — rekomendasi lagu on-device. Menebak "vibe" (8 kelas) dari 13 fitur perilaku dan audio dengan softmax + filter HMM, lalu memilih lagu memakai contextual Thompson Sampling. Data belajar di `/ryne2.bin`.
- **Musik** — cover diambil dari ID3 tag (hanya JPEG baseline; selain itu kembali ke piringan hitam), mode acak biasa atau AI, kontrol AVRCP dari earbuds, dan alasan restart CAM ditampilkan di layar.
- **AI Chat** — Gemini lewat HTTPS dengan input teks atau dikte suara, memori percakapan tersimpan di SD.
- **Inferno** — FPS raycaster bertema gothic dengan musuh teranimasi, tiga tingkat senjata, joystick + look sentuh, dan kontrol miring.
- **NES** — core Nofrendo di task terpisah; **DOOM** — doomgeneric, non-audio, WAD disediakan pengguna.
- **Voice Memo** — rekam INMP441 ke WAV di `/memo`, diputar lewat jalur Musik BT.
- **Widget cuaca** — Open-Meteo, tanpa API key.

---

## 04 — Peta Pin

| Blok | Sinyal → GPIO |
|---|---|
| Layar (SPI2) | SCLK 12 · MOSI 11 · MISO 13 · DC 2 · CS 10 · RST 14 · BL 21 |
| Sentuh (SPI3) | SCLK 6 · MOSI 5 · MISO 4 · CS 9 |
| SD (SDIO 1-bit) | CLK 39 · CMD 38 · D0 40 |
| Mikrofon (I2S) | SCK 47 · WS 46 · SD 45 |
| Link CAM (UART) | S3 TX 41 → CAM RX 13 · S3 RX 42 ← CAM TX 14 · GND bersama |
| MPU6050 (I2C 0x68) | SDA 15 · SCL 7 |
| Lainnya | Motor getar 18 · Neopixel 48 · ADC baterai 8 |

Untuk ESP32-CAM: beri daya ke pin **5V**, dan **cabut kartu SD dari slotnya** karena GPIO13/14 dipakai bersama slot tersebut.

---

## 05 — Build

CI memakai `arduino-cli` dengan `esp32:esp32:esp32s3`, `FlashSize=16M`, `PSRAM=opi`, `PartitionScheme=app3M_fat9M_16MB`. Cara paling cepat: fork repo dan ambil `.bin` dari artifact GitHub Actions.

**Build manual (S3)**

1. Pasang board package *esp32 by Espressif* 3.x, lalu library `LovyanGFX` dan `JPEGDEC`.
2. Satukan seluruh `*.ino` dan header di root repo ke satu folder sketch, dengan `phone.ino` sebagai file utama.
3. Salin `nofrendo_core/` (tanpa `main.c`, `display.c`, `keyboard.c`, `osd.c`, `timing.c`, `sound.c`, `stubs.c`) dan `esp_doom_core/` ke `src/`, lalu terapkan tiga patch kecil yang ada di workflow CI (rename `log_printf`, hapus `vid_setpalette`, tambah flag `nesExitRequested`).
4. Tambahkan flag mbedTLS:
   `-DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1 -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048`
5. Compile, unggah, dan ikuti kalibrasi sentuh pada boot pertama.

**Build ko-prosesor (CAM)**

Folder `cam_bt_coprocessor/`, board *AI Thinker ESP32-CAM*, partisi *Huge APP*, PSRAM aktif. Library: `audio-tools`, `arduino-libhelix`, `ESP32-A2DP`. Pastikan `rplink.h` identik dengan versi di root.

**Catatan penamaan file**: `phone_notif_center.ino` sengaja dinamai demikian agar urutan alfabetis Arduino memasukkannya setelah `phone.ino`, sehingga definisi global `phone.ino` terlihat tanpa `extern`. Selain itu, fungsi yang menerima struct custom berisiko memicu bug prototype otomatis `arduino-cli`; beberapa struct di kode sengaja diletakkan paling atas file karena alasan ini.

**Langkah CI tambahan**: `ci_hoist_types.py` memindahkan blok `#include`, struct, dan enum di bagian atas `phone.ino` (±baris 459–716) ke header `rp_types.h` yang di-`#include` di baris pertama. Ini hanya terjadi pada salinan di `build_sketch/` saat CI; `phone.ino` di repo tidak diubah. Tujuannya supaya prototipe otomatis `arduino-cli` selalu disisipkan setelah semua tipe dikenal. Jika compile gagal, workflow meng-upload artifact `diagnostik-prototipe` (hasil preprocess dan versi tool) untuk membantu melacak penyebabnya. Versi `arduino-cli`, core ESP32, dan library di CI belum dikunci, jadi update dari upstream bisa memengaruhi build.

---

## 06 — Konfigurasi Runtime

**API key** (file teks di SD, atau lewat Web File Manager)

| File | Dipakai oleh |
|---|---|
| `/gemini_key.txt` | AI Chat dan dikte. Jika SD tidak terdeteksi, disimpan di NVS |
| `/nasa_key.txt` | NEO, EPIC, Galeri NASA. `DEMO_KEY` diperbolehkan tetapi limitnya kecil |

Astronomi (APOD) tidak lagi membutuhkan key sejak NASA memindahkan endpoint-nya ke `science.nasa.gov`.

**Tata letak SD**

```
/
├── gemini_key.txt  nasa_key.txt  wallpaper.jpg  ai_memory.txt  ryne2.bin
├── notepad.txt  canvas_land.bin  canvas_port.bin
├── music/            MP3, maks 200 lagu
├── memo/             hasil Voice Memo (WAV)
├── screenshot/       tangkapan layar (BMP)
├── update/           firmware .bin untuk OTA lokal
├── .thumbs/          cache thumbnail Galeri
├── *_cache/          apod / neo / epic / imglib
├── *.mjpeg  *.nes    di root
└── doom1.wad         disediakan sendiri
```

**Web File Manager** — saat WiFi aktif, buka `http://<IP-perangkat>/` untuk upload, edit, unduh, hapus, mengatur key Gemini, dan mengganti wallpaper (nama berkas harus persis `wallpaper.jpg`; langsung aktif tanpa restart).

**Video** — format MJPEG portrait native:
```bash
ffmpeg -i input.mp4 -vf "fps=15,scale=240:320" output.mjpeg
```

---

## 07 — Batasan yang Diketahui

- RAM internal sempit. `HTTP -1` bisa berarti fragmentasi, bukan masalah jaringan; sudah ada mekanisme tunggu dan coba-ulang, tetapi belum bisa dihilangkan sepenuhnya dari level sketch.
- Audio hanya lewat Bluetooth. Tanpa ESP32-CAM dan TWS, app Musik tidak menghasilkan suara.
- File MP3 dengan bitrate tinggi (contoh 192 kbps) pernah memicu restart CAM; file bitrate rendah stabil. Masih dalam pengerjaan.
- Estimasi baterai berasal dari tegangan ADC, bukan fuel-gauge, jadi hanya perkiraan kasar.
- NyxPPU belum terpasang di Inferno maupun Home/Lock Screen.
- Seek/resume AVI butuh `idx1` dan hanya mencakup bagian pertama file (AVI OpenDML > 1 GB tidak didukung). Audio dihitung dari bitrate header, jadi MP3 VBR bisa bergeser sedikit setelah lompat jauh; pakai CBR (`-b:a 128k`) seperti di atas.
- PNG dibatasi 4 MB per file dan 8 juta piksel; decode memakai buffer penuh di PSRAM, jadi PNG di atas 2 MB tidak punya thumbnail di Galeri.
- Versi `arduino-cli`, core ESP32, dan library tidak dikunci di CI; update upstream bisa mematahkan build tanpa perubahan apa pun di repo.
- Library `lvgl` terdaftar di CI tetapi tidak dipakai; mesin render sepenuhnya LovyanGFX.

---

## 08 — Firmware Alternatif

`Bruce-accretion-phone.bin` adalah build fork [Bruce](https://github.com/pr3y/Bruce) dengan board port untuk hardware yang sama. Ini firmware terpisah dari NyxOS. Port ini memakai konstanta kalibrasi sentuh generik dan tidak membawa sistem kalibrasi `phone.ino`.

---

## 09 — Struktur Repo

```
phone.ino                    inti NyxOS (UI, boot, jaringan, app dasar, game)
phone_notif_center.ino       notifikasi / Dynamic Island
features_renphone.ino        screenshot, stopwatch, voice memo, widget cuaca
gallery_renphone.ino         Galeri + thumbnail cache (JPG/PNG)
avi_renphone.ino             pemutar AVI (MJPEG + MP3)
about_renphone.ino  owner_renphone.ino  homeedit_renphone.ino   About Phone, Profil, Atur Home
musicbt_renphone.ino         Musik, UART RPLink, AVRCP
wakeword_renphone.ino        Wake Word (MFCC + DTW)
ppu_engine_renphone.ino      NyxPPU
ryne_engine.h                RYNE v2
nes_*_renphone.ino           integrasi emulator NES
doom_renphone.ino  hw_conf.h integrasi DOOM
rplink.h  MjpegClass.h       protokol link CAM, parser MJPEG
cam_bt_coprocessor/          firmware ESP32-CAM
nofrendo_core/  esp_doom_core/   core pihak ketiga (di-vendor)
apply_*.py  patch_*.py       skrip patch untuk perubahan bertahap
ci_hoist_types.py            langkah CI: pindahkan tipe ke rp_types.h (dijalankan workflow)
.github/workflows/           build firmware S3 dan CAM
```

Riwayat perubahan (v7 hingga v119+) ditulis sebagai komentar langsung di kode: gejala, akar masalah, dan alasan perbaikannya. Untuk memahami kenapa sebuah bagian ditulis seperti itu, mulai dari komentar di atasnya.

---

## 10 — Lisensi

**AGPL-3.0**. Teks lengkap ada di [`LICENSE`](./LICENSE). Kode pihak ketiga (Nofrendo, ESP-DOOM/doomgeneric) mengikuti lisensinya masing-masing.

<div align="center">

<sub>Tidak ada yang runtuh melewati event horizon tanpa meninggalkan catatan di commit.</sub>

</div>
