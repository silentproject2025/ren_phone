# Accretion Phone · NyxOS

**Ponsel buatan sendiri dengan sistem operasi yang ditulis dari nol, berjalan pada satu chip ESP32-S3.**

NyxOS tidak berbasis Android maupun Linux. Peluncur aplikasi (launcher), pusat notifikasi, tumpukan jaringan, emulator, pemutar media, hingga mesin grafisnya merupakan kode sendiri yang berjalan di atas FreeRTOS.

| | |
|---|---|
| **Mikrokontroler** | ESP32-S3, flash 16 MB, PSRAM OPI 8 MB |
| **Aplikasi bawaan** | 45 aplikasi |
| **Ukuran kode** | ±53.000 baris firmware utama + ±1.500 baris firmware ko-prosesor (tidak termasuk kode pihak ketiga) |
| **Versi** | 1.0.0 (lihat berkas `VERSION`) |
| **Lisensi** | AGPL-3.0 |

> **Catatan mengenai status pengujian.** Sebagian besar fitur telah diuji langsung oleh pembuatnya pada perangkat fisik. Terdapat dua pengecualian yang perlu diketahui sejak awal:
>
> - **Telepon** baru teruji sebagian: berhasil dengan akun `sip2sip.info` (SIP melalui TCP), sedangkan penyedia SIP lain, mode LAN, dan TURN belum terkonfirmasi.
> - **NyxPPU** masih berupa eksperimen (fase 1), dan **SPI Overclock** bergantung pada kualitas kabel.
> - **Gestur sistem baru** (tarik-untuk-muat-ulang, geser-kembali dari tepi kiri, panel yang mengikuti jari) baru ditambahkan dan berstatus `BETA`.
> - **Backup & Restore** pengaturan dan **Catur** (online dan lawan AI) berstatus `BETA`: logikanya diuji di komputer, tetapi belum diuji pada perangkat.
>
> Penjelasan lengkap tiap fitur tersedia pada [bagian 5](#5-fitur-secara-rinci) dan [bagian 6](#6-fitur-eksperimental-dan-catatan-perangkat-keras).

---

## Daftar Isi

1. [Ringkasan dan status fitur](#1-ringkasan-dan-status-fitur)
2. [Arsitektur sistem](#2-arsitektur-sistem)
3. [Perangkat keras dan pemetaan pin](#3-perangkat-keras-dan-pemetaan-pin)
4. [Instalasi dan build](#4-instalasi-dan-build)
5. [Fitur secara rinci](#5-fitur-secara-rinci)
6. [Fitur eksperimental dan catatan perangkat keras](#6-fitur-eksperimental-dan-catatan-perangkat-keras)
7. [Konfigurasi melalui kartu SD](#7-konfigurasi-melalui-kartu-sd)
8. [Cara kerja internal](#8-cara-kerja-internal)
9. [Keamanan dan privasi](#9-keamanan-dan-privasi)
10. [Batasan yang diketahui](#10-batasan-yang-diketahui)
11. [Pemecahan masalah](#11-pemecahan-masalah)
12. [Kontribusi dan pelaporan bug](#12-kontribusi-dan-pelaporan-bug)
13. [Struktur repositori](#13-struktur-repositori)
14. [Lisensi dan atribusi](#14-lisensi-dan-atribusi)

---

## 1. Ringkasan dan status fitur

### 1.1 Arti penanda status

| Penanda | Arti |
|---|---|
| `OK` | Telah diuji pemilik pada perangkat fisik dan berjalan baik. |
| `SEBAGIAN` | Telah diuji pada skenario tertentu; skenario lain belum terkonfirmasi. |
| `BETA` | Kode sudah lengkap, tetapi **belum diuji pada perangkat**. Bug mungkin masih ada. |
| `HW` | Kode sudah benar; hasil akhir bergantung pada kualitas perangkat keras. |
| `EXP` | Eksperimen atau kerangka awal. |

> Istilah "diuji" dalam dokumen ini berarti diuji pemilik pada **satu unit perangkat miliknya**. Unit lain dengan kabel atau komponen berbeda dapat berperilaku berbeda.

### 1.2 Ringkasan status

| Fitur | Status | Keterangan |
|---|---|---|
| Antarmuka sistem (Home, Lock Screen, Control Center, dan lainnya) | `OK` | Digunakan sehari-hari. Termasuk tema Nyx, ikon squircle, widget bento, dan animasi buka/tutup app. |
| Animasi mengisi daya (Lock Screen) | `OK` | Muncul saat charger dicolok dan HP terkunci. Syarat lengkap di [bagian 5.1](#51-antarmuka-sistem-ok). |
| Gestur sistem baru (pull-to-refresh, swipe-back tepi kiri, panel mengikuti jari) | `BETA` | Kode sudah terpasang di build, belum diuji menyeluruh pada perangkat. |
| Notifikasi berkelompok, geser-hapus, aksi cepat; preview kartu App Switcher | `BETA` | Kode sudah terpasang di build, belum diuji pada perangkat. Lihat [bagian 5.1](#51-antarmuka-sistem-ok). |
| Fitur tambahan NyxOS (perintah suara AI, energi per app, log crash, banner telepon, thumbnail screenshot, skeleton loading) | `BETA` | Kode sudah terpasang di build, belum diuji pada perangkat. Lihat bagian 5.5 dan 5.6. |
| Emoji (set bawaan dan set lengkap dari SD) | `BETA` | Emoji berwarna di seluruh teks NyxOS dan keyboard. Set bawaan 54 sprite; set lengkap dibuat dari Noto Emoji atau Twemoji menjadi `/emoji.bin`. Hanya emoji tunggal; ZWJ, bendera, dan keycap belum didukung. Lihat [bagian 5.1](#51-antarmuka-sistem-ok). |
| Kamus keyboard (saran kata + autokoreksi) | `BETA` | Baris 3 saran di atas tuts, autokoreksi salah ketik (spasi/`,`/`.`/`?`/`!`), batal dengan Hapus, kamus Indonesia + Inggris bawaan, kamus tambahan dari SD. Aktif di Notepad dan Chat. Logika diuji di komputer (`tests_kamus/`), belum diuji pada perangkat. Lihat [bagian 5.1](#51-antarmuka-sistem-ok). |
| Backup & Restore pengaturan | `BETA` | Seluruh pengaturan (NVS) disimpan ke `/backup/nyx_NNN.nbk` di SD dan dapat dipulihkan; sandi/API key tidak ikut kecuali diminta; opsi enkripsi AES-256 dengan sandi. Format, CRC, dan kripto diuji di komputer (`tests_backup/`), belum diuji pada perangkat. Lihat [bagian 5.6](#56-sistem-dan-perangkat). |
| Catur (online lewat MQTT dan lawan AI) | `BETA` | Aturan lengkap, 5 level AI termasuk level "Cermin" (belajar gaya pemilik di perangkat) dan "Nyx" (eksperimental, memanfaatkan kebiasaan pemilik), dan AI pengganti saat pemain AFK. Dilengkapi layar **Jaringan** untuk melihat bobot hasil belajar AI. Mesin aturan dan AI diuji di komputer (`tests_chess/`, termasuk *perft*), belum diuji pada perangkat atau antar dua HP. Lihat [bagian 5.2.3](#523-catur-online-dan-lawan-ai-beta). |
| Chat melalui broker MQTT | `OK` | Diuji dengan broker sungguhan. |
| Musik Bluetooth dan AVRCP | `OK` | Cover ID3, kontrol dari earbuds, mode acak AI. |
| Pemutar AVI (seek, resume, zoom) | `OK` | |
| Wake Word "Hei Nyx" | `OK` | Berhasil dikenali. |
| Pembaruan OTA per channel | `OK` | Termasuk pengaman rollback. |
| mbedTLS di PSRAM (library core kustom) | `OK` | Dipasang otomatis oleh CI. Pada uji awal, pemakaian RAM internal turun sekitar 40 KB saat HTTPS aktif. |
| Remote Control (web) | `OK` | |
| Gamepad web untuk NES dan DOOM | `OK` | |
| Baterai (INA219), Galeri, HWmonitor, Profil, dan lainnya | `OK` | |
| **Telepon (SIP dan LAN)** | `SEBAGIAN` | Teruji dan berjalan normal dengan `sip2sip.info` melalui TCP. Akun `sip.linphone.org` hanya berfungsi satu jalur (sebagai penerima). Mode LAN dan TURN belum terkonfirmasi. |
| Kamera (OV2640 pada ESP32-CAM) | `OK` | Seluruh fitur telah diuji pemilik, termasuk optimasi link kamera terbaru (ACK tanpa menunggu UI, dedup header JPEG, link hingga 5 Mbaud) yang hasilnya signifikan. Lihat [`KAMERA.md`](./KAMERA.md). |
| Buku (pembaca `.txt`, `.md` dan `.epub`) | `OK` | Diuji pemilik di perangkat dan berjalan baik. Logika inti (inflate, ZIP, EPUB, HTML ke teks, tata letak) juga diuji di komputer (`tests_ebook/`). Lihat bagian *App Buku* di bawah. |
| Buku: unduh dari Project Gutenberg | `OK` | Diuji pemilik di perangkat: unduh ±5 buku EPUB dengan gambar (sampai ±2,9 MB) berhasil. Logika inti dan alur unduh/verifikasi juga diuji di komputer (`tests_gutenberg/`). Sumber OPDS dan format TXT belum dikonfirmasi secara khusus. Lihat *Unduh dari Project Gutenberg* di bawah. |
| SPI Overclock layar | `HW` | Hasil bergantung pada batas fisik kabel. |
| NyxPPU (mesin piksel) | `EXP` | Fase 1; hanya tersedia pada aplikasi PPU Demo. |

---

## 2. Arsitektur sistem

Perangkat terdiri atas dua mikrokontroler yang saling terhubung melalui UART. ESP32-S3 menangani antarmuka, aplikasi, dan jaringan. ESP32-CAM bertindak sebagai **ko-prosesor** yang mengurus Bluetooth audio (A2DP) serta kamera.

```
┌────────────────────────────┐   UART 921.600 baud    ┌──────────────────────────┐
│ ESP32-S3  (utama)          │  (naik ke 2-5 Mbaud    │ ESP32-CAM  (ko-prosesor) │
│  · UI, aplikasi, game      │   saat Kamera dibuka)  │  · decode MP3 (libhelix) │
│  · WiFi, HTTP/TLS, SIP     │ ◀────── RPLink ──────▶ │  · A2DP ke TWS           │
│  · SD card, layar, sentuh  │                        │  · kamera OV2640         │
│  · mic INMP441, sensor     │                        │                          │
└────────────────────────────┘                        └────────────┬─────────────┘
                                                                   │ Bluetooth A2DP
                                                                   ▼
                                                           Earbuds / TWS
```

Konsekuensi desain yang penting:

- **Perangkat tidak memiliki speaker.** Seluruh keluaran suara (musik, video, panggilan, voice memo) melewati ESP32-CAM menuju TWS Bluetooth.
- **Dua firmware harus dijaga konsisten.** Berkas `rplink.h` wajib **identik** pada sisi S3 dan sisi CAM.
- **Memori adalah sumber daya paling terbatas.** Uraian strateginya ada pada [bagian 8](#8-cara-kerja-internal).

---

## 3. Perangkat keras dan pemetaan pin

### 3.1 Komponen

| Bagian | Spesifikasi |
|---|---|
| Otak | ESP32-S3, flash 16 MB, PSRAM OPI 8 MB |
| Layar | ILI9341 320×240 (SPI2), mode portrait dan landscape. Varian 3,5 inci: ST7796 480×320 + sentuh FT6236 (lihat bagian "Build layar ST7796") |
| Sentuh | XPT2046 pada bus SPI3 yang terpisah |
| Penyimpanan | MicroSD (SDIO 1-bit), dengan FFat internal sebagai cadangan |
| Sensor | MPU6050 (tilt, shake, rotasi otomatis) dan INA219 (tegangan, arus, daya) |
| Mikrofon | INMP441 (I2S) |
| Keluaran audio | **Hanya Bluetooth (A2DP)** melalui ko-prosesor ESP32-CAM; tidak ada speaker |
| Kamera | OV2640 pada modul ESP32-CAM (sensor OV3660/OV5640 dikenali otomatis) |
| Pendukung | Motor getar dan Neopixel |
| Skema partisi | `app3M_fat9M_16MB` |

### 3.2 Pemetaan pin ESP32-S3

| Blok | Sinyal → GPIO |
|---|---|
| Layar | SCLK 12 · MOSI 11 · MISO 13 · DC 2 · CS 10 · RST 14 · BL 21 |
| Sentuh | SCLK 6 · MOSI 5 · MISO 4 · CS 9 |
| SD (SDIO 1-bit) | CLK 39 · CMD 38 · D0 40 |
| Mikrofon | SCK 47 · WS 46 · SD 45 |
| Link ke CAM | S3 TX 41 → CAM RX 13 · S3 RX 42 ← CAM TX 14 · GND bersama |
| MPU6050 dan INA219 | SDA 15 · SCL 7 (satu bus I2C) |
| Lainnya | Motor getar 18 · Neopixel 48 |

### 3.3 Catatan perakitan ESP32-CAM

- Berikan daya melalui pin **5V** dengan catu yang cukup kuat. Modul ini mudah mengalami *brownout* ketika kamera dan radio Bluetooth aktif bersamaan.
- **Cabut kartu SD pada modul ESP32-CAM.** GPIO 13 dan 14 dipakai bersama oleh slot tersebut, dan GPIO 4 digunakan sebagai LED flash.
- Pemetaan pin kamera mengikuti papan **AI-Thinker**. Papan klon dengan pin berbeda memerlukan perubahan konstanta `CAM_PIN_*` pada `cam_bt_coprocessor/cam_module.h`.
- Kabel UART yang panjang atau berkualitas rendah dapat membuat link 4–5 Mbaud tidak stabil. Firmware akan turun otomatis satu anak tangga kecepatan (5M → 4M → 3M → 2M → 921.600 baud) hingga link bersih. Gunakan kabel sependek mungkin.

---

## 4. Instalasi dan build

Terdapat tiga jalur pemasangan. Pilih sesuai kebutuhan.

| Jalur | Cocok untuk | Perangkat lunak yang dibutuhkan |
|---|---|---|
| **A. Unduh firmware hasil build CI** | Pengguna yang hanya ingin mencoba | Web flasher atau `esptool` |
| **B. Fork dan build di GitHub Actions** | Pengembang yang ingin memodifikasi tanpa toolchain lokal | Akun GitHub |
| **C. Build manual (Arduino CLI / IDE)** | Pengembang yang membutuhkan kontrol penuh | Arduino CLI atau Arduino IDE |

### 4.1 Jalur A: firmware hasil build CI

Setiap *push* memicu workflow **Build Firmware Binaries** yang menerbitkan Release GitHub.

| Berkas Release | Fungsi |
|---|---|
| `ren_phone_s3_merged.bin` | Image lengkap ESP32-S3 (bootloader, partisi, aplikasi) untuk flash pertama kali. |
| `firmware.bin` | Image aplikasi saja, dipakai oleh pembaruan OTA. |
| `info.txt` | Metadata OTA (branch, build, basis kode, ukuran, MD5). Diterbitkan pada Release `channel-<branch>` bersama `firmware.bin`. |
| `cam_merged.bin` | Image lengkap ESP32-CAM, dari workflow **Build CAM BT Co-Processor**. |

Penamaan tag: `build-<nomor>` untuk S3, `cam-<nomor>` untuk CAM, dan `channel-<branch>` untuk channel OTA per branch. Branch `main`, `master`, dan `remote-control` diterbitkan sebagai rilis biasa; branch lain selalu berstatus *prerelease*.

Saat dijalankan manual (*Run workflow*), workflow menyediakan dua input: `use_custom_libs` (pakai atau lewati library core kustom) dan `loop_core` (`default`, `0`, atau `1`; memilih core tempat `loop()`/UI berjalan, untuk tes A/B performa game). Ada dua input catur: `simd_pie` (`on` bawaan, atau `off` = jalur skalar, flag `-DCHS_NN_NO_PIE`, untuk tes A/B) dan `nyx_nn` (`off` bawaan, atau `on` = `-DCHS_NN_NYX=1`, level Nyx memakai evaluasi jaringan saraf). SIMD PIE (instruksi vektor ESP32-S3) hanya mempercepat evaluasi NN, jadi baru terpakai bila `nyx_nn=on`; NN masih eksperimen, dan saat NN aktif core ke-2 (Lazy SMP) otomatis tidak dipakai. Build dari push memakai nilai bawaan.

Workflow **Web Flasher (GitHub Pages)** memanggil otomatis setelah Release dibuat dan menyusun halaman flasher berbasis browser. Halaman tersebut membedakan firmware **stabil** (dari `main`/`master`) dan **uji coba** (dari branch lain), serta menyediakan tombol unduh `.bin`, checksum SHA-256, dan tautan repositori.

Pada boot pertama, ikuti prosedur **kalibrasi sentuh** yang tampil di layar.

### 4.2 Jalur B: build melalui GitHub Actions

1. *Fork* repositori ini.
2. Aktifkan GitHub Actions pada fork tersebut.
3. Jalankan workflow **Build Firmware Binaries** (otomatis pada setiap *push*, atau manual melalui *Run workflow*).
4. Unduh berkas `.bin` dari *artifact* atau dari Release, lalu flash.

> Agar pembaruan OTA dapat mengunduh firmware tanpa login, repositori harus berstatus **public**.

### 4.3 Jalur C: build manual untuk ESP32-S3

**Prasyarat perangkat lunak**

- Board package **esp32 by Espressif 3.x**
- Library: `LovyanGFX`, `JPEGDEC`, `PubSubClient`, `WebSockets`

> Workflow CI saat ini juga memasang `lvgl` dan `ESP32-audioI2S`, tetapi kode sumber di repositori ini tidak lagi menyertakan keduanya (pemutar MP3 internal lama telah dicabut). Keduanya tidak diperlukan untuk build manual.

**Langkah**

1. Satukan seluruh berkas `*.ino` dan header di root repositori ke satu folder sketch, dengan `phone.ino` sebagai berkas utama.
2. Salin `nofrendo_core/` dan `esp_doom_core/` ke `src/`. Patch kecil yang dibutuhkan didefinisikan pada `build-firmware.yml`.
3. Tambahkan *build flag* mbedTLS berikut:

   ```
   -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1
   -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
   ```

4. Pilih board dan opsi berikut:

   ```
   FQBN : esp32:esp32:esp32s3
   Opsi : FlashSize=16M, PSRAM=opi, PartitionScheme=app3M_fat9M_16MB
   ```

5. Compile dan unggah, lalu ikuti kalibrasi sentuh pada boot pertama.

> **Catatan CI.** `ci_hoist_types.py` memindahkan blok `#include`, struct, dan enum di bagian atas `phone.ino` ke header sementara pada salinan di `build_sketch/`. Langkah ini hanya berlaku di CI dan tidak mengubah `phone.ino` di repositori. Tujuannya agar prototipe otomatis `arduino-cli` selalu disisipkan setelah seluruh tipe dikenal.

### 4.4 Build ko-prosesor ESP32-CAM

| Parameter | Nilai |
|---|---|
| Folder sumber | `cam_bt_coprocessor/` |
| Board | AI Thinker ESP32-CAM (`esp32:esp32:esp32cam`) |
| Skema partisi | Huge APP |
| PSRAM | Aktif |
| Library | `audio-tools`, `arduino-libhelix`, `ESP32-A2DP` |

Pastikan `rplink.h` **identik** pada sisi S3 dan sisi CAM. Fitur Kamera mensyaratkan **kedua firmware** (S3 dan CAM) dipasang ulang; tanpa firmware CAM yang baru, aplikasi Kamera hanya menampilkan pesan bahwa CAM belum mendukung kamera dan fitur lain tidak terganggu.

### 4.5 Library core kustom (mbedTLS di PSRAM)

Komponen ini bersifat **opsional**, tetapi digunakan oleh build CI.

**Latar belakang.** Library bawaan core menempatkan buffer mbedTLS di RAM internal. Library kustom memindahkannya ke PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`) sehingga RAM internal lebih lega saat koneksi HTTPS aktif.

**Dua workflow bekerja berurutan:**

1. **Build Arduino Libs (mbedTLS di PSRAM)**: dijalankan manual, **sekali per versi core**. Workflow ini memakai `esp32-arduino-lib-builder` dan mengunggah hasilnya ke Release *prerelease* `arduino-libs-custom` (`esp32s3-libs.tar.gz` dan `core-version.txt`). Prosesnya lama dan dapat memakan waktu berjam-jam.
2. **Build firmware**: pada setiap build, workflow mengunduh paket tersebut, menimpanya ke `~/.arduino15/packages/esp32/tools/esp32s3-libs/<versi core>/`, lalu meng-compile sketch.

**Opsi sdkconfig yang dipasang** (dijalankan ulang workflow *Build Arduino Libs* agar berlaku):

| Opsi | Fungsi |
|---|---|
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` | Buffer mbedTLS di PSRAM (**wajib**). |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `..._DYNAMIC_FREE_CONFIG_DATA=y` | Buffer TLS dialokasi per koneksi dan data konfigurasi dilepas setelah handshake. |
| `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` | Syarat stack task di PSRAM (`xTaskCreateStaticPinnedToCore`). |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` | Buffer WiFi/lwIP diutamakan di PSRAM. Throughput WiFi bisa sedikit turun; matikan lewat input `wifi_lwip_psram=false`. |

Flag `-DCONFIG_MBEDTLS_*` yang dulu ada di `build-firmware.yml` dihapus karena tidak memengaruhi `libmbedtls.a` yang sudah jadi. Opsi tambahan hanya memunculkan *warning* bila gagal masuk; hanya `EXTERNAL_MEM_ALLOC` yang menggagalkan build.

Inti emulator dan DOOM (`nofrendo_core/`, `esp_doom_core/`) dikompilasi dengan `-O2` (pragma per berkas di CI); sisa sketch tetap `-Os` agar partisi app 3 MB tidak penuh.

**Mekanisme pengaman.** Paket hanya dipasang jika Release-nya ada, versi core sama dengan `core-version.txt`, dan `sdkconfig` di dalam paket memuat opsi yang dibutuhkan. Jika salah satu syarat gagal, build **tetap berjalan dengan library bawaan** dan hanya mengeluarkan *notice* atau *warning*. Dengan demikian, **build yang berstatus hijau belum tentu memakai library kustom**. Periksa log berikut:

| Langkah di log | Hasil yang diharapkan |
|---|---|
| *Pasang library core kustom* | Muncul `Library kustom (mbedTLS di PSRAM) terpasang utk core X.Y.Z`. |
| *Verifikasi library kustom terpasang* | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1` pada `sdkconfig.h`, dan hash `libmbedtls.a` terpasang **berbeda** dari aslinya. |

Jalankan ulang workflow libs hanya jika versi core berubah atau opsi `sdkconfig` diubah. Workflow **Build Firmware Binaries** juga menyediakan input manual `use_custom_libs` (`false` untuk membandingkan secara A/B dengan library bawaan).

**Hasil uji awal.** Pengukuran melalui HWmonitor saat aplikasi Astronomi memuat data lewat HTTPS, dibandingkan dengan firmware sebelum library kustom:

| Ukuran | Sebelum | Sesudah |
|---|---|---|
| RAM internal terpakai | 167 KB | 127 KB |
| Blok bebas terbesar | 21 KB | 30 KB |

Angka di atas berasal dari satu cuplikan per firmware pada satu unit. Anggap sebagai indikasi, bukan nilai pasti.

### 4.6 Memasang fitur melalui skrip patch

Banyak fitur disusun sebagai pasangan `*_renphone.ino` dan `apply_*.py`. Skrip dijalankan dari root repositori dan **idempoten** (aman dijalankan berulang).

```bash
python3 apply_call_patch.py      # Telepon (SIP + LAN)
python3 apply_camera_patch.py    # Kamera
python3 apply_ebook_patch.py     # Buku (pembaca .txt / .epub)
git add -A && git commit -m "Pasang fitur" && git push
```

> Berkas `.ino` baru **harus** didaftarkan pada langkah `cp` di `build-firmware.yml`; berkas header `.h` yang di-`#include` dari sketch (mis. `gutdl_core.h`) juga. Jika terlewat, build gagal pada tahap *link* dengan pesan `undefined reference to ...` untuk fungsi yang didefinisikan di berkas tersebut (pernah terjadi pada `ptr_renphone.ino`). Versi `arduino-cli`, core ESP32, dan library di CI **belum dikunci**, sehingga pembaruan dari upstream dapat menyebabkan build gagal tanpa ada perubahan di repositori.

---

## 5. Fitur secara rinci

### 5.1 Antarmuka sistem `OK`

NyxOS menghadirkan pengalaman ponsel modern pada layar kecil 320×240.

| Komponen | Fungsi |
|---|---|
| **Lock Screen** | Jam, wallpaper kustom, geser untuk membuka kunci. Saat membuka, lock screen **naik mengikuti jari** dan menyingkap Home di bawahnya. |
| **Home** | Grid ikon dengan pencarian, widget cuaca (Open-Meteo, tanpa API key), dan badge notifikasi pada ikon. |
| **Control Center** | Tombol WiFi, Airplane, DND, Tema, Orientasi, Shake, Kunci, Neopixel, Senyap, Bluetooth, dan Tangkap layar, ditambah slider kecerahan. **Tahan lama** sebuah tombol untuk membuka pengaturan fitur itu (WiFi, Tema, Rotasi, Neopixel, Bluetooth, Galeri); tahan lama tombol **Kunci** membuka menu Daya. |
| **Notification Shade** | Riwayat notifikasi, dikelompokkan per aplikasi (chip jumlah di kanan untuk membuka/menutup grup). Geser baris ke kiri/kanan untuk menghapus; **tahan lama** sebuah baris untuk aksi cepat (Buka / Hapus / Batal). `BETA` |
| **Dynamic Island** | Elemen berbentuk pil di bagian atas layar yang berubah sesuai kejadian. |
| **App Switcher** | Berpindah antaraplikasi. Tiap kartu menampilkan **preview** layar terakhir app (disimpan di PSRAM, tidak ke SD), usap kartu ke atas untuk menutup, chip **Tutup semua** di kanan atas. `BETA` |
| **Always-On Display** | Tampilan hemat daya saat layar diredupkan. |
| **Keyboard layar** | Tersedia untuk seluruh aplikasi. |
| **8 tema** | Aurora, Dark, AMOLED, Light, Pastel, iOS, Full Dark, dan **Nyx** (hitam keunguan dengan aksen violet dan teal; jadi tema bawaan, dapat diganti di Pengaturan). |
| **Menu Daya** | Matikan total (deep sleep; bangun dengan tombol BOOT atau RST) dan Mulai ulang. Dibuka dari tahan-lama tombol Kunci atau Pengaturan → Tentang → Daya. |

**Keputusan desain.** Getaran sengaja dibuat minimal: hanya untuk kejadian penting, bukan pada setiap ketukan. Efek kaca buram merupakan tiruan ringan (mengambil sampel warna dari piksel yang sudah tergambar lalu mencampurnya dengan *tint* tema), bukan blur sungguhan, agar beban CPU tetap rendah.

**Tampilan "Nyx Modern"**

- **Ikon squircle** (kotak membulat) dengan bayangan lembut dan kilap tepi; warna ikon neon dilembutkan secara otomatis.
- **Latar aurora**: dua cahaya radial lembut dari warna aksen tema pada halaman seperti Pengaturan.
- **Pengaturan** dengan chip ikon berwarna di tiap baris, **widget Home gaya bento** (jam, baterai, musik, cuaca), dan **keyboard datar** dengan efek tekan.
- **Transisi app**: ikon di Home membesar menjadi layar app saat dibuka, dan mengecil kembali ke ikonnya saat ditutup. Toast dan Dynamic Island muncul dengan animasi yang lebih halus.
- **Animasi widget Home**: angka jam berganti halus, cincin baterai terisi saat Home dibuka, dan ikon cuaca bergerak pelan.

**Animasi mengisi daya** `OK`

Saat charger dicolok, lock screen menampilkan lubang hitam kecil dengan cincin akresi yang berdenyut selama ±4,6 detik; panjang busur cincin mengikuti persentase baterai, dan teks berubah menjadi "Terisi penuh" pada 100%. Menyentuh layar menghentikannya. Animasi hanya muncul jika **semua** syarat berikut terpenuhi:

- HP sedang berada di lock screen (AOD akan dibangunkan otomatis).
- Sudah lewat ±8 detik sejak boot.
- Status sebelumnya "tidak mengisi" minimal 1,5 detik, lalu charger dicolok (charger yang sudah terpasang sejak boot tidak memicu animasi sampai dicabut dulu).
- INA219 terbaca dan arus charge melewati 20 mA (arah arus dapat dibalik pada tab Baterai bila status tidak pernah berubah menjadi "Mengisi").

**Gestur sistem baru** `BETA`

| Gestur | Cara kerja |
|---|---|
| **Pull-to-refresh** | Tarik ke bawah dari strip atas layar (di kiri tombol cari) pada app yang punya aksi muat ulang: Update, NEO Asteroid, Bumi EPIC, dan Galeri NASA. Lingkaran penunjuk mengikuti jari dan terisi; lepas setelah penuh untuk memuat ulang. |
| **Swipe-back tepi kiri** | Sentuh tepi kiri layar lalu geser ke kanan; gelembung "<" mengikuti jari, lepas setelah cukup jauh untuk kembali. Hanya aktif pada app non-game dan non-editor agar tidak mengganggu D-pad, kanvas gambar, atau pengetikan. |
| **Panel mengikuti jari** | Control Center dan Notification Shade tumbuh mengikuti jari saat ditarik; lepas untuk membuka (dengan pantulan kecil) atau menutup. |

**Notifikasi dan App Switcher (v133)** `BETA`

| Fitur | Cara kerja |
|---|---|
| **Pengelompokan per app** | Notifikasi dari app yang sama digabung jadi satu baris (yang terbaru) dengan chip jumlah. Ketuk chip untuk membuka/menutup grup; baris di bawahnya bergeser halus. |
| **Geser untuk menghapus** | Geser baris ke kiri/kanan lewat sepertiga lebar layar (atau lemparan cepat). Pada baris induk, seluruh grup terhapus; pada baris anak, hanya entri itu. Getaran halus muncul sekali saat melewati ambang. |
| **Aksi cepat** | Tahan lama sebuah baris (±0,45 dtk, jari diam): **Buka** (bila notifikasi punya app tertaut), **Hapus**, **Batal**. Ketuk di luar baris untuk membatalkan. |
| **Preview kartu Switcher** | Thumbnail 64×48 layar terakhir app, diambil dari kanvas tepat sebelum app ditinggalkan. Maks 8 thumbnail di PSRAM, hilang saat reboot atau saat kartu ditutup. Matikan dengan `-DSW_THUMB_ENABLE=0` bila capture terasa menahan transisi. |
| **Tutup semua** | Chip di kanan atas Switcher; sama dengan usap-atas pada tiap kartu (cache RAM app ikut dilepas). |

Catatan: panel Notification Shade kini menyesuaikan tingginya saat isi berubah, dan batas scroll dihitung dari tinggi panel sebenarnya. Sebelumnya di landscape dua baris terbawah tidak bisa dijangkau. Label umur notifikasi ("N jam lalu") juga diperbaiki; format lamanya tidak valid.

**Perlindungan ghost touch.** Jika panel sentuh mengirim sentuhan palsu, sistem menolak titik di kotak mati, di luar layar, atau dengan tekanan rendah, lalu menahan seluruh input sampai layar benar-benar sepi. Saat boot, bila sentuhan palsu terdeteksi, wizard kalibrasi dilewati dan kalibrasi bawaan dipakai tanpa disimpan. Ambang dan kotak mati diatur lewat konstanta `TOUCH_*` pada `phone.ino`; sentuhan jari asli juga bisa ikut tertolak selama ghost terus-menerus aktif.

**Fitur personalisasi**

- **Wallpaper.** Simpan `wallpaper.jpg` di kartu SD, atau unggah melalui Web File Manager. Berlaku pada Home dan Lock Screen dan langsung aktif tanpa restart.
- **Atur Home.** Mengubah urutan ikon dan menyembunyikan aplikasi dari grid. Tersedia tombol *Reset* dengan konfirmasi.
- **Profil.** Kartu avatar dan nama pemilik di bagian atas Pengaturan; foto dipilih dari kartu SD.
- **About Phone.** Informasi perangkat dan sistem.
- **Backup & Restore.** Simpan dan pulihkan seluruh pengaturan ke kartu SD, dibuka dari Pengaturan → Tentang. Lihat [bagian 5.6](#56-sistem-dan-perangkat).

**Emoji** `BETA`

Emoji tampil di seluruh teks NyxOS (keyboard, Notepad, Chat, AI Chat/Gemini, dan lainnya) sebagai sprite berwarna 10, 16, atau 24 px, mengikuti tinggi baris. Font bawaan LovyanGFX hanya menerima kode karakter 16-bit, sehingga emoji disimpan di dalam firmware sebagai kode privat (PUA, `U+E000` + nomor sprite) dan semua `setFont()` dibungkus `emjF()`. Batas antara firmware dan dunia luar dijaga `emjNormalize()` (saat membaca) dan `emjToWire()` (saat menulis atau mengirim), jadi berkas `.txt` di SD dan pesan Chat tetap berupa UTF-8 standar yang bisa dibuka di HP atau PC biasa.

| Set | Keterangan |
|---|---|
| **Bawaan** | 54 sprite gambar sendiri (`emoji_data.h`, dibuat `tools/gen_emoji.py`). Selalu ada sebagai cadangan. |
| **Dari SD** | Berkas `/emoji.bin` di root kartu SD, dibuat `tools/make_emoji_bin.py` dari PNG Noto Emoji atau Twemoji. Saat boot hanya indeks kodepoint yang dimuat ke PSRAM; sprite dibaca dari SD saat pertama dibutuhkan dan disimpan di cache LRU. Menggantikan set bawaan. Mengganti berkas cukup dengan restart. |

**Membuat `emoji.bin`** (contoh di Termux; butuh Pillow: `pkg install python-pillow`, atau `pip install pillow`)

```
git clone --depth 1 https://github.com/googlefonts/noto-emoji ~/noto-emoji
python3 tools/make_emoji_bin.py ~/noto-emoji ~/storage/downloads/emoji.bin
```

Sumber yang dikenali (folder hasil `git clone`, dicari rekursif, atau berkas zip-nya): Noto (`png/128/emoji_u1f600.png`) dan Twemoji (`assets/72x72/1f600.png`, repo `jdecked/twemoji`). Salin hasilnya ke root kartu SD sebagai `/emoji.bin`, lalu restart HP.

**Keyboard emoji.** Halaman emoji dibuka dari tombol smiley di samping `123`: 30 emoji per halaman (3 baris × 10), tombol `<` dan `>` berganti halaman, dan penanda `spasi n/N` menunjukkan halaman aktif. Urutannya wajah populer, hati, dan simbol umum lebih dulu, sisanya menurut kodepoint. Backspace membuang satu emoji utuh.

**Batasan set emoji**

- **Hanya emoji tunggal (satu kodepoint) dari `U+231A` ke atas.** Urutan multi-kodepoint tidak punya sprite sendiri. Penanda warna kulit, ZWJ, `VS-16`, dan keycap dibuang saat tampil: 👍🏽 tampil sebagai 👍, 👨‍💻 tampil sebagai dua sprite terpisah (👨 dan 💻), dan `1️⃣` tampil sebagai `1`. Bendera tampil sebagai kotak `?`.
- Emoji yang tidak ada di set digambar sebagai kotak `?` (bukan hilang atau merusak teks).
- **Maksimal 4000 sprite** (slot PUA `E000` sampai `EF9F`; `EFFF` dicadangkan untuk kotak `?`). Batas ini hampir tidak tercapai karena Noto hanya punya sekitar seribu empat ratusan emoji tunggal.
- **Memori dan SD.** Berkas `emoji.bin` berukuran `16 + N × 1868` byte (N emoji; tiga ukuran sprite): 1.390 emoji ≈ 2,6 MB. Berkas **tidak** dimuat utuh: PSRAM hanya menyimpan indeks kodepoint (±12 byte per emoji) ditambah cache LRU berisi 128 sprite per ukuran, dialokasikan saat ukuran itu pertama dipakai (10 px ±25 KB, 16 px ±64 KB, 24 px ±144 KB). Berkas SD tetap terbuka selama set aktif dan sprite dibaca lewat penampung kecil di RAM internal. Konsekuensinya, kartu SD harus tetap terpasang: bila gagal dibaca, emoji yang belum ada di cache tampil sebagai kotak `?` (tiga kegagalan berturut-turut menutup berkas; emoji yang sudah di cache tetap tampil). Membuka halaman emoji pertama kali memberi jeda singkat karena ±30 sprite dibaca dari SD.
- Berkas tidak ada, kartu SD tidak terbaca, header atau ukuran tidak cocok, ukuran berkas kurang, atau PSRAM tidak cukup: set bawaan dipakai tanpa pesan galat.
- **Chat:** nama pengguna hanya ASCII (emoji pada nama tidak dikirim). Emoji pada pesan dikirim sebagai UTF-8 standar.

**Kamus keyboard (saran kata dan autokoreksi)** `BETA`

Di atas tuts muncul baris tipis berisi **3 saran kata** untuk kata yang sedang diketik, dengan tombol mode `ab` di kiri. Aktif di **Notepad** dan **Chat**. Kolom password, URL, SSID, API key, nama, dan pencarian sengaja tidak memakainya. App AI belum diikutkan karena layarnya menyembunyikan kotak input selagi keyboard terbuka, sehingga koreksi di sana tidak terlihat.

| Aksi | Hasil |
|---|---|
| Ketuk saran | Kata yang sedang diketik diganti saran itu, lalu spasi. |
| Tekan spasi, `,` `.` `?` `!` | Kata salah ketik dikoreksi otomatis. Kata yang akan dikoreksi tampil berwarna aksen di baris saran **sebelum** spasi ditekan. |
| Tekan **Hapus** tepat setelah koreksi | Kembali ke ketikan asli dan kata itu **dipelajari** (tidak dikoreksi lagi). Disimpan di NVS (maks. 120 kata). Kata asing yang diketik dua kali juga dipelajari otomatis. |
| Ketuk tombol `ab` | Ganti mode: **saran + koreksi** (tombol berwarna aksen) → **saran saja** (bergaris bawah) → **mati** (dicoret). Pilihan tersimpan. |

*Aturan autokoreksi (konservatif, agar kata sah tidak rusak).* Hanya bila kata **tidak ada** di kamus mana pun, panjang ≥ 4 huruf, huruf pertama sama, dan ada kata kamus yang berjarak **satu salah ketik yang masuk akal**: huruf ganda (`nasii`), tuts bersebelahan di QWERTY (`makab` → `makan`), huruf terlewat, atau dua huruf tertukar (`thier` → `their`). Tidak disentuh: kata berkapital di tengah kalimat (nama), `NASA`/`iPhone`, email/URL/hashtag/kode ber-angka, kata dengan apostrof (`don't`), tawa berulang (`hehe`, `wkwk`), dan huruf yang sengaja dipanjangkan (`yaaa`).

*Kamus.* Bawaan: ±1.290 kata Indonesia (termasuk bahasa gaul: `gak`, `udah`, `banget`, `mager`) dan ±940 kata Inggris, dari `tools/kamus_id.txt` dan `tools/kamus_en.txt` (urutan = perkiraan frekuensi; hasil dibuat `python3 tools/gen_kamus.py` menjadi `kamus_data.h`). Bahasa yang lebih sering Anda ketik didahulukan di saran. **Kamus tambahan opsional:** taruh `/kamus.txt` di root kartu SD (satu kata per baris, yang paling sering di atas; format `kata 1234` juga diterima, maksimum 2 MB). Dimuat sekali ke PSRAM saat keyboard pertama dipakai. Kata yang belum ada di kamus bisa saja dikoreksi keliru; satu kali tekan Hapus langsung membatalkannya dan mencegah terulang. Kamus yang lebih lengkap dari SD mengurangi hal ini.

### 5.2 Komunikasi

#### 5.2.1 Chat melalui MQTT `OK`

Fitur ini menyediakan pesan pribadi antarperangkat **tanpa server buatan sendiri**. Sebuah broker MQTT (TLS, port 8883, autentikasi username dan password) berperan sebagai servernya.

**Cara kerja**

- **Riwayat disimpan di broker.** Setiap pesan dipublikasikan sebagai pesan *retained* ke salah satu dari 24 slot bergilir (`renphone/chat/m/<slot>`). Ketika perangkat tersambung, broker langsung mengirim isi ke-24 slot tersebut, sehingga 24 pesan terakhir tampil, termasuk pesan yang dikirim saat aplikasi Chat sedang tertutup.
- **Format pesan:** `seq|waktu|nama|pesan`. Nomor `seq` dipakai untuk mengurutkan dan membuang duplikat.
- **Hemat RAM.** Perangkat hanya tersambung selama aplikasi Chat terbuka. Setelah ditutup, koneksi TLS dilepas dan memorinya dikembalikan.

**Konfigurasi.** Isi melalui aplikasi (Broker, Username, Password, Nama, lalu *Simpan & sambung*), atau letakkan `/chat.cfg` pada kartu SD dan tekan *Muat SD*. Format berkas ada pada [bagian 7](#7-konfigurasi-melalui-kartu-sd).

**Pemilihan broker.** Paket gratis HiveMQ Cloud Serverless telah dihentikan. Alternatif gratis yang dibahas pada [`chat-mqtt/README.md`](./chat-mqtt/README.md) adalah EMQX Cloud Serverless, tetapi firmware tidak terikat pada satu penyedia; berpindah broker cukup dengan mengganti isi konfigurasi.

**Batasan:** tidak ada notifikasi saat aplikasi tertutup; dua perangkat yang mengirim pada detik yang sama dapat bertabrakan di satu slot; layar menampilkan ASCII dan emoji (huruf lain tampil `?`); dan sertifikat broker tidak diverifikasi (lihat [bagian 9](#9-keamanan-dan-privasi)).

#### 5.2.2 Telepon (SIP dan LAN) `SEBAGIAN`

Aplikasi panggilan suara melalui WiFi dengan dua mode operasi:

| Mode | Kegunaan | Membutuhkan internet |
|---|---|---|
| **SIP** | Menelepon alamat SIP lain (misalnya `teman@sip2sip.info`) melalui akun gratis. | Ya |
| **LAN** | Menelepon langsung antarperangkat NyxOS pada jaringan WiFi yang sama. | Tidak |

**Spesifikasi teknis**

| Aspek | Detail |
|---|---|
| Agen SIP | Satu agen untuk kedua mode, port 5060. |
| Codec | G.711 (PCMU/PCMA), 8 kHz, paket 20 ms, dikirim melalui RTP. |
| Transport SIP (mode SIP) | **TCP secara default.** Dapat diganti ke UDP dengan `transport=udp` pada `sip.cfg`. Jika koneksi TCP gagal, sistem otomatis turun ke UDP. |
| Transport mode LAN | UDP. |
| Pemetaan alamat publik | STUN (`stun.l.google.com:19302`), hanya untuk panggilan melalui internet. |
| Relay (opsional) | TURN melalui UDP untuk jaringan CGNAT atau NAT ketat (lihat di bawah). |

**Status pengujian.** Telah diuji pada perangkat nyata dengan akun `sip2sip.info` (SIP melalui TCP): pendaftaran akun berhasil, serta panggilan keluar dan masuk antara Ren Phone dan ponsel Android (aplikasi Linphone) tersambung dan berjalan normal. Akun `sip.linphone.org` sudah dicoba dan hanya berfungsi satu jalur (sebagai penerima), sehingga `sip2sip.info` yang disarankan. Hal yang **belum** terkonfirmasi: mode LAN antara dua perangkat NyxOS, TURN, dan variasi kondisi NAT.

**Alur suara**

```
Mic INMP441 ──16 kHz──▶ turun ke 8 kHz ──▶ G.711 ──RTP──▶ lawan bicara

lawan bicara ──RTP──▶ G.711 ──▶ PCM 8 kHz ──▶ jalur audio eksternal
                                              ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
```

Karena perangkat **tidak memiliki speaker**, suara lawan bicara hanya dapat didengar melalui **TWS Bluetooth**. Tanpa TWS yang tersambung, mikrofon tetap mengirim suara, tetapi Anda tidak akan mendengar apa pun.

**Prosedur penggunaan**

1. Pasang fitur dengan `python3 apply_call_patch.py` (aman dijalankan ulang).
2. Sambungkan TWS terlebih dahulu melalui aplikasi **Musik**.
3. **Mode SIP**
   - Buat akun gratis. Yang sudah teruji: <https://www.sip2sip.info> (isi Server dengan `sip2sip.info`). Akun <https://subscribe.linphone.org> (Server `sip.linphone.org`) sudah dicoba dan hanya bisa sebagai penerima, jadi tidak disarankan.
   - Buka aplikasi Telepon, pilih *Atur*, lalu isi Server, username, password, dan nama. Sebagai alternatif, letakkan `/sip.cfg` pada kartu SD.
   - Pada tab SIP, ketik alamat tujuan (misalnya `teman` atau `teman@sip2sip.info`) lalu tekan **Panggil**.
4. **Mode LAN**
   - Pada tab LAN, isi alamat IP perangkat lawan, atau tekan **Cari HP NyxOS** (mengirim *broadcast* SIP OPTIONS) lalu pilih hasilnya.
   - Perangkat penerima harus berada pada WiFi yang sama dan opsi **Siaga terima panggilan** harus aktif.

**Konfigurasi TURN (opsional)**

Tanpa TURN, panggilan melalui internet pada jaringan CGNAT dapat bisu atau satu arah. Dengan TURN, suara dibungkus dan dilewatkan melalui server relay. TURN hanya berlaku untuk panggilan SIP melalui internet; panggilan LAN tidak berubah. Jika TURN gagal, sistem kembali ke STUN dan menampilkan alasannya melalui *toast*.

- **Melalui UI:** Telepon → Atur → tombol **TURN >** (kanan atas). Server dapat berupa `host` atau `host:port` (port default 3478).
- **Melalui kartu SD:** buat `/turn.cfg` (format pada [bagian 7](#7-konfigurasi-melalui-kartu-sd)). Berkas dibaca otomatis hanya ketika TURN belum diisi, atau melalui tombol *Muat SD* pada halaman TURN.
- **Cloudflare Realtime TURN:** buat *TURN key* pada dashboard, lalu hasilkan kredensial berumur pendek (contoh TTL 24 jam):

  ```bash
  curl -s -X POST -H "Authorization: Bearer $TURN_KEY_API_TOKEN" -H "Content-Type: application/json" \
    -d '{"ttl": 86400}' https://rtc.live.cloudflare.com/v1/turn/keys/$TURN_KEY_ID/credentials/generate
  ```

  Salin `username` dan `credential` dari hasilnya ke `user=` dan `pass=`. Kredensial kedaluwarsa sesuai TTL; buat ulang dan timpa `/turn.cfg` bila perlu.
- Server TURN lain (misalnya coturn milik sendiri dengan UDP dan autentikasi *long-term*) juga dapat dipakai.
- Batasan: hanya TURN melalui UDP (belum TCP/TLS), dan belum ada ICE. Belum ada catatan pengujian untuk TURN.

**Hal yang perlu diwaspadai**

- Hanya alamat SIP yang dapat dihubungi. **Nomor telepon biasa (PSTN) tidak dapat dihubungi** melalui layanan gratis.
- Tanpa ICE, panggilan melalui internet dapat **satu arah atau bisu** pada NAT tertentu. Mode LAN paling andal.
- **Tidak terenkripsi:** SIP berjalan melalui TCP/UDP polos (belum TLS) dan suara RTP tidak dienkripsi. Kata sandi dikirim sebagai *hash digest*.
- Latensi satu arah diperkirakan **300–500 ms** (akibat A2DP).
- Mode siaga **nonaktif secara default** untuk menghemat RAM dan baterai. Panggilan masuk ditolak otomatis ketika NES atau DOOM sedang berjalan.
- Mikrofon dipakai bersama fitur lain. Tutup Voice Memo dan Mic Level sebelum menelepon; Wake Word dijeda otomatis.
- Patch ini ikut mengubah `rplink.h` dan firmware CAM. Memasang ulang firmware CAM bersifat opsional, tetapi membuat suara sekitar 140 ms lebih cepat.

Panduan ringkas tersedia juga pada [`call-sip/README.md`](./call-sip/README.md).

#### 5.2.3 Catur (online dan lawan AI) `BETA`

Catur lengkap (skak, skakmat, pat, rokade, *en passant*, promosi bidak, aturan 50 langkah) dengan dua cara bermain. Papan diputar otomatis untuk pemain Hitam, dan panel di bawah papan dapat disembunyikan.

| Mode | Cara kerja |
|---|---|
| **Online** | Dua HP membuka app **Catur**, mengisi **kode room** yang sama (minimal 3 karakter), lalu menekan **Mulai**. HP yang masuk lebih dulu menjadi Putih, yang kedua menjadi Hitam. Tersedia **Main lagi** dan **Menyerah** (dengan konfirmasi). Perlu WiFi dan sebuah broker MQTT. |
| **Lawan AI** | Tanpa jaringan sama sekali. Tombol tingkat AI berganti antara Mudah, Sedang, Sulit, Cermin, dan Nyx. |

**Broker.** Catur memakai pengaturan broker **sendiri**, terpisah dari Chat (TLS, port 8883 bawaan; `host` boleh diberi `:port`). Diisi lewat app (host, username, password, lalu **Simpan**), lewat berkas `/chess.cfg` di SD (`host=`, `user=`, `pass=`; lihat [bagian 7](#7-konfigurasi-melalui-kartu-sd)), atau dengan tombol salin dari pengaturan Chat (setelah disalin, keduanya tetap terpisah). Bila akun broker dibatasi oleh ACL ke `renphone/chat/#`, tambahkan izin *publish* dan *subscribe* ke `renphone/chess/#`.

**Cara kerja online.**

- Setiap permainan disimpan sebagai pesan *retained* berisi **seluruh daftar langkah** (notasi koordinat, misalnya `e2e4 e7e5 g1f3`) beserta nomor generasi dan hasil. Karena yang dikirim selalu daftar lengkap, pesan ganda, terlambat, atau terputus tidak merusak permainan, dan lawan yang baru tersambung langsung memperoleh posisi terakhir.
- Kursi Putih dan Hitam juga berupa pesan *retained*; kehadiran pemain memakai *Last Will* sehingga HP yang putus ditandai offline.
- Seperti Chat, koneksi hanya ada selama app Catur terbuka. Bila RAM internal terlalu padat, app menampilkan pesan agar app lain ditutup.

**Tingkat AI.**

| Tingkat | Perilaku |
|---|---|
| **Mudah** | Pencarian dangkal (kedalaman 2); memilih acak di antara langkah yang masih tergolong layak, dengan toleransi lebar. |
| **Sedang** | Kedalaman 4; memilih acak di antara langkah layak dengan toleransi sempit. |
| **Sulit** | Kedalaman hingga 12 dalam batas 4 detik (NYX-MAX; dulu kedalaman 6); selalu memainkan langkah terbaik menurut pencariannya. |
| **Cermin** | Kedalaman 4; di antara langkah yang nilainya masih dekat dengan yang terbaik, AI memilih langkah yang paling **mirip gaya pemilik**. |
| **Nyx** `eksperimental` | Kedalaman hingga 14 dalam batas 7 detik (NYX-MAX; dulu 2,4 detik), toleransi 15 centipawan (sebelumnya kedalaman 4, 0,9 detik, toleransi 80). Memperkirakan **balasanmu** dengan model lawan yang belajar di perangkat (gaya Cermin + keuntungan taktis + keserakahan), lalu memilih langkah yang paling menghukum kebiasaanmu, **hanya bila model terbukti menebak lebih baik dari acak**. Lihat penjelasan di bawah. |

**Mesin pencarian (dipakai semua tingkat).** Pencarian *negamax* kini memakai PVS dengan tabel transposisi 64 bit (2^16 entri, ±768 KB di PSRAM; bila alokasi gagal, pencarian tetap jalan tanpa tabel; hanya entri dari pencarian yang sedang berjalan yang dipercaya, karena skor lama bergantung pada riwayat dan membuat AI mengira posisi menang sebagai remis), *null-move*, *late move reduction*, *futility* dan *reverse futility*, urutan langkah berbasis langkah-TT, MVV-LVA, *killer*, dan *history*, serta deteksi pengulangan posisi di jalur pencarian. *Quiescence* memakai *delta pruning*. Evaluasi menambahkan struktur pion (ganda, terisolasi, bebas), pasangan gajah, benteng di file terbuka atau baris ke-7, mobilitas kuda/gajah/benteng, perisai pion raja, dan *mop-up* endgame (dorong raja lawan ke tepi saat unggul) supaya K+R vs K atau K+Q vs K benar-benar dituntaskan. Karena rekursi berjalan di task loop (stack 8 KB), kedalaman dibatasi `CHS_AI_MAXD = 14` dan `CHS_AI_PLY = 32`, ditambah penjaga stack saat runtime (`CHS_AI_STACK_MAX`, 4800 byte). Format NVS (`sty`, `hab`, `bk`, `rb`, `mix`) **tidak berubah**; hash TT berbeda dari `chsPosHash` yang dipakai buku pembuka.

Hasil di komputer (bukan di perangkat): Nyx final (kedalaman 14, toleransi 15) menang 20-0 atas Nyx lama dari repo (kedalaman 4, toleransi 80) dengan waktu diskalakan ala ESP32; parameter final menang 13-6-5 atas parameter versi sebelumnya (kedalaman 10, toleransi 30) pada mesin yang sama, jadi kenaikan terakhir nyata tetapi sedang. Konversi endgame diuji pada 9 posisi K+R/K+Q vs K (semua skakmat) dan 50 posisi acak K+R vs K (50/50; mesin lama 47/50). Pada benchmark `bench_nyx2.cpp` yang sama dengan sebelumnya (48 partai, 60 ply), melawan bot *layak berkebiasaan* Nyx lama menghasilkan skakmat 26/48 dan Nyx baru 33/48; selisih antar jalankan masih berderau beberapa partai. Angka-angka itu memakai mesin di PC; **kecepatan sebenarnya di ESP32-S3 belum diukur**, sehingga kedalaman yang tercapai dalam 2,4 detik di perangkat akan lebih rendah daripada di PC. Selisih yang disebabkan logika jebakan kebiasaan (model lawan) sendiri tetap belum terbukti terpisah dari kenaikan kekuatan mesin dasar.

**Nyx (level eksperimental).** Level kelima ini menambahkan logika buatan sendiri di atas mesin yang ada. Pencarian minimax tetap menyaring kandidat yang aman (dalam 80 centipawan dari yang terbaik, paling banyak 8 kandidat). Untuk tiap kandidat, AI menjalankan langkahnya lalu memperkirakan **peluang tiap balasan sahmu** dengan model *product of experts*: peluang ∝ exp(θ₁·skor gaya + θ₂·keuntungan taktis + θ₃·keserakahan). *Skor gaya* adalah jaringan dan tabel kebiasaan milik Cermin, *keuntungan taktis* adalah nilai pencarian tangkapan (*quiescence*) setelah langkah itu, *keserakahan* adalah nilai bidak yang dimakan. Ketiga bobot θ dilatih langsung di perangkat setiap kali kamu melangkah dengan **langkah Newton rekursif** (regresi logit bersyarat; presisi menumpuk dengan faktor lupa 0,97 supaya gaya yang berubah ikut terkejar). Nilai tiap kandidat memakai **soft-min entropik** `F = −(1/k)·ln Σ pᵣ·exp(−k·Vᵣ)` dengan `k = 0,01` per centipawan. `F` selalu berada di antara `M` (kamu membalas terbaik) dan `E` (rata-rata menurut model), sehingga balasan yang jarang tetapi menyakitkan tetap diperhitungkan dan AI tidak terlalu optimis. Selisih `F − M` dikalikan keyakinan model (`1 − H/ln n`, `H` = entropi peluang balasan), dibatasi 150, lalu ditambahkan ke skor kandidat dengan bobot `0,9 · g`. **Gerbang `g`** = `min(1, N/30) · min(1, IG/1,2)`, dengan `IG` = rata-rata bergerak (12 langkah) dari `ln(peluang yang diberikan model pada langkahmu) − ln(1/n)`, dihitung *sebelum* model belajar dari langkah itu (prequential, jadi jujur). Bila model tidak menebak lebih baik dari acak maka `g = 0` dan Nyx bermain sebagai minimax murni; bobot jebakan baru membesar setelah model terbukti menebak langkahmu.

Model campuran (θ, presisi, `IG`) disimpan di NVS kunci `mix` (44 byte) dan ikut Backup & Restore; kunci yang tidak ada (perangkat lama) berarti nilai awal, jadi format `sty`, `hab`, `bk`, dan `rb` tidak berubah. Model ini hanya diamati saat level Nyx dipilih karena biayanya n pencarian tangkapan per langkahmu; pengamatan dibatasi 100 ms dan dilewati bila terlampaui, sehingga gerbang tetap tertutup dan Nyx bermain seperti minimax (gagal dengan aman).

Status: logika dan keamanannya diuji di komputer (`test_chess.cpp`: langkah selalu sah, papan tidak berubah, mat-dalam-1 tetap dimainkan, bonus nol pada model rata, blob `mix` yang rusak ditolak, gerbang terbuka pada pemain yang bisa diprediksi dan tertutup pada pemain acak). Percobaan di komputer (`bench_nyx2.cpp`, 48 partai per sel, 60 ply, seed baru) melawan bot *layak berkebiasaan* (pencarian 2-ply, 70% makan bidak terbesar yang layak): minimax murni menghasilkan skakmat 30/48, **Nyx versi pertama hanya 16/48** (jelas merugikan), Nyx versi ini 31/48. Melawan bot yang 95% selalu makan bidak terbesar: 45/48, 44/48, dan 46/48. **Keunggulan atas minimax murni belum terbukti**: selisihnya masih di dalam derau, dan model campuran hanya menambah informasi kecil (0,03 sampai 0,12 nat per langkah) di atas model gaya saja. Yang sudah terbukti adalah regresi versi pertama hilang. Belum diuji pada perangkat; biaya pengamatan di PC rata-rata sekitar 2 ms per langkahmu (maksimum 38 ms) dan belum diukur di ESP32-S3. Karena itu level ini tetap ditandai eksperimental.

**Cermin dan AFK.** Mesin AI memakai pencarian *negamax* alpha-beta dengan *quiescence* dan evaluasi materi serta tabel posisi. Level Cermin menambahkan tiga komponen kecil yang **belajar langsung di ESP32** setiap kali pemilik melangkah: jaringan saraf mini (26 masukan, 16 neuron tersembunyi, 1 keluaran; 449 parameter) yang menilai seberapa mirip suatu langkah dengan pilihan pemilik, **buku pembuka pribadi** (posisi dan langkah yang paling sering dimainkan), dan **tabel kebiasaan petak** (misalnya selalu membuka dengan `e4` atau gemar memajukan kuda ke `f3`). Model disimpan di NVS sehingga tidak hilang saat app ditutup. Pada permainan online, bila pemain diam melewati batas **AFK** (30, 60, atau 120 detik; dapat dimatikan), AI bergaya-pemilik memainkan gilirannya agar lawan tidak menunggu. Karena AI hanya memilih di antara langkah yang masih dekat terbaik, partai tidak dibuang begitu saja.

**NYX-MAX: kekuatan catur sampai batas hardware (belum diukur pada perangkat).** Tiga tuas dinaikkan sekaligus:

- **Kompilasi -O3** untuk blok ENGINE + AI (dulu -O2; sisa sketch tetap -Os). Input workflow `chess_opt` = `O3` (bawaan) atau `O2` (kode lebih kecil, untuk A/B atau bila partisi app 3 MB penuh). Flag: `-DCHS_OPT3=0`.
- **Cache CPU maksimum** (library SDK kustom, workflow *Build Arduino Libs*): cache data 64 KB, cache instruksi 32 KB, keduanya 8-way, garis cache data 64 B. Ini menyerang hambatan yang tercatat di atas: dua core berebut cache saat membaca tabel transposisi di PSRAM. Harganya **±48 KB RAM internal** (dcache 64 KB = -32 KB, icache 32 KB = -16 KB); bila muncul "RAM padat" lebih sering, jalankan ulang workflow itu dengan `dcache_kb=32` dan/atau `icache_kb=16`. Library kustom harus dibangun ulang sekali agar berlaku.
- **Waktu berpikir**: Sulit 1,8 → 4 detik, Nyx 2,4 → 7 detik, batas pondering 5 → 10 detik per giliranmu. Mudah, Sedang, dan Cermin sengaja tetap cepat.

Yang sengaja **tidak** diubah: CPU sudah 240 MHz; PSRAM OPI sudah 80 MHz (120 MHz eksperimental dan tidak stabil di banyak papan); tabel transposisi tetap 1,5 MB karena pada ±30 ribu node/detik pencarian 7 detik hanya ±200 ribu node, jadi tabel lebih besar tidak menambah apa pun dan hanya memakan PSRAM.

**Layar Jaringan (melihat isi otak AI).** Pada layar setup Catur, di bawah baris *Lawan AI | tingkat | AFK*, ada chip kecil **Jaringan** di kanan. Layar ini menampilkan **bobot hasil latihan** jaringan gaya langsung di HP; yang ditampilkan adalah hasil belajarnya, bukan grafik *loss* atau log tiap langkah latihan. Tombol Kembali menutupnya. Pada semua tampilan, warna aksen tema berarti bobot positif dan merah berarti bobot negatif; makin pekat berarti makin besar dibandingkan nilai terbesar yang sedang tampil.

| Tab | Isi dan cara memakai |
|---|---|
| **Jaringan** | Diagram 26-16-1: 26 titik masukan di kiri (abu-abu untuk 20 fitur langkah, warna lain untuk 6 konteks posisi), 16 neuron tersembunyi di tengah, dan 1 titik skor di kanan. Hanya garis masukan→neuron yang kuat (di atas 35% dari bobot terbesar) yang digambar agar layar tidak penuh. Warna isi tiap neuron menunjukkan bobotnya ke skor (aksen = mendorong skor naik, merah = menurunkan). **Ketuk satu neuron** (lingkaran tengah): garis neuron lain diredupkan, seluruh garis neuron itu tampil, dan baris info menampilkan skor neuron serta dua fitur yang paling memengaruhinya, misalnya `H7 skor +0.31 \| makan +0.42, skak -0.28`. Ketuk lagi untuk membatalkan. |
| **Bobot** | Peta panas matriks bobot lapisan pertama: 16 baris (H1 sampai H16) kali 26 kolom (fitur 1 sampai 26), jadi seluruh 416 bobot terlihat sekaligus. **Ketuk sebuah kotak** untuk nilainya, misalnya `H3 x makan: +0.215`. |
| **Petak** | Tabel kebiasaan petak sebagai 7 papan 8×8: satu papan untuk tiap jenis bidak tujuan (Pion, Kuda, Gajah, Benteng, Menteri, Raja) dan satu papan **Asal** untuk petak yang ditinggalkan; Putih berada di bawah. Petak yang pekat adalah petak yang sering kamu tuju atau tinggalkan. **Ketuk sebuah petak** untuk nilainya, misalnya `Kuda f3: +1.80`. |
| **Selisih** (chip) | Mengganti tampilan dari bobot sekarang menjadi **selisih dari bobot awal**, sehingga hanya bobot yang berubah karena belajar dari langkahmu yang berwarna. Bobot awal dihitung ulang dari generator acak yang sama dengan inisialisasi dan disimpan di PSRAM hanya selama layar ini dibuka. Bila RAM tidak cukup, muncul pesan *RAM tidak cukup utk selisih*. |

Dua baris teks di bawah tampilan memuat info sentuhan terakhir (neuron, kotak, atau petak) dan status latihan, misalnya `54 langkah | tebak 62% | bobot`. **Langkah** adalah jumlah langkahmu yang sudah dipelajari. **Tebak** adalah rata-rata bergerak 20 langkah terakhir: seberapa sering pilihan teratas jaringan, sebelum belajar dari langkah itu, sama dengan langkah yang kamu mainkan. Angka ini baru muncul setelah minimal 5 langkah; tebakan yang terus rendah berarti jaringan belum menangkap pola atau gayamu memang bervariasi.

Cara memakainya: mainkan beberapa partai melawan AI, buka **Bobot** dengan **Selisih** menyala untuk melihat fitur apa yang dipelajari AI dari gayamu, lalu buka **Petak** untuk melihat kebiasaan petak bukaanmu (misalnya `e4` dan `f3` yang menyala).

**Adu mesin vs Gemini (menguji kekuatan AI).** Pada layar setup Catur, di sebelah chip **Jaringan** ada chip **Gemini**. Mesin lokal (tingkat yang dipilih di baris *Lawan AI | tingkat | AFK*) bertanding melawan Gemini lewat kunci API yang sama dengan AI Chat (`/gemini_key.txt`), dan kamu menonton. Gemini diberi FEN, riwayat langkah, dan **daftar langkah sah**, lalu diminta membalas satu langkah UCI. Balasan yang tak sah diulang (maksimal 3 kali per langkah); tiga kali berturut-turut tak sah berarti Gemini kalah. Bila jaringan atau API gagal, partai dijeda dengan pesan galat dan bisa dilanjutkan dengan **Lanjut**. Tombol kiri panel: **Jeda/Lanjut**, lalu **Main lagi** (warna ditukar) setelah partai selesai. Skor per tingkat (`M` mesin menang, `G` Gemini menang, `S` seri) tersimpan di NVS dan tampil di panel. Perlu WiFi. Model bawaan sama dengan AI Chat; untuk lawan yang berbeda, isi `CHS_GEM_MODEL` di `chess_renphone.ino`. Logika giliran, pembacaan balasan, dan hitungan skor diuji di komputer dengan Gemini tiruan (`tests_chess/run_gem.sh`); panggilan API sungguhan dan tampilan layar belum diuji pada perangkat.

**Dua core dan pengukur NPS (Nyx, Lazy SMP)** `eksperimental`

Pencarian Nyx dapat memakai kedua core ESP32-S3. Core lain dari `loop()` menjalankan **salinan mesin sendiri** (`chess_smp.cpp`, di-build sebagai file terpisah; CI menyalin blok ENGINE+AI dari `chess_renphone.ino` ke `build_sketch/src/chess_engine.inc`), sehingga papan, killer, dan history terpisah. Yang dibagi hanya tabel transposisi (TT) di PSRAM, tanpa mutex: entri dibaca/ditulis sebagai tiga kata 32 bit dengan kunci ter-XOR, jadi entri yang "sobek" karena dua core menulis bersamaan terdeteksi dan dianggap meleset. Core pembantu mencari satu kedalaman di depan iterasi utama dan mengisi TT; pencarian utama memakainya untuk memangkas lebih awal.

- Aktif hanya bila anggaran waktu ≥ 400 ms dan kedalaman maksimum ≥ 5 (jadi tidak untuk pondering, Mudah, dan Sedang) serta jaringan NN utama tidak aktif. Task `nyxHelper` (stack 10 KB di PSRAM, prioritas 1, core kebalikan `loop()`) dibuat sekali lalu diparkir, dan menyerahkan CPU 1 tick tiap ±25 ms.
- **Melihat NPS:** Serial Monitor mencetak `[NYX-NPS] d=… node=utama+pembantu … ms NPS=… core=N` setiap AI selesai mencari. Di layar **Jaringan** ada tab **NPS**: pencarian terakhir, tombol **Uji NPS** (3 posisi tetap × 1 detik, sekali 1 core dan sekali 2 core, ±6 detik; hanya saat tidak ada partai), dan saklar **Core ke-2** (disimpan di NVS kunci `smp`). Hasil uji juga dicetak sebagai `[NYX-BENCH]`.
- Matikan saat build dengan `-DCHS_SMP=0` (satu core persis seperti sebelumnya).
- **Hasil ukur di perangkat:** 19,2k → 29,9k node/detik (×1,55), tetapi kedalaman rata-rata dalam 1 detik 6,0 → 5,7. Jadi kecepatan mentah naik, **kekuatan belum terbukti naik**: dua core berebut PSRAM dan cache, dan core utama melambat. Uji yang lebih adil (waktu mencapai kedalaman tertentu) belum ada. Di komputer, `tests_chess/run_smp.sh` menguji kebenaran (langkah selalu sah, mat dan taktik tetap benar, mulai/henti berulang tanpa deadlock), bukan kecepatan.

**Batasan.** Tidak ada jam catur, dan belum ada aturan ulangan tiga posisi. Fitur ini baru diuji di komputer (aturan dan *perft*, kekuatan tiap level, pelatihan serta serialisasi model gaya); perilaku nyata di dua perangkat dan pada broker sungguhan belum terkonfirmasi.
### 5.3 Kendali jarak jauh

#### 5.3.1 Remote Control `OK`

Perangkat memancarkan **WiFi sendiri (Access Point)**. Sambungkan ponsel atau laptop lain, buka `http://192.168.4.1/remote`, dan **seluruh antarmuka Accretion tampil di browser serta dapat dikendalikan**.

| Kemampuan | Cara kerja |
|---|---|
| Layar langsung | Isi canvas dikirim sebagai **ubin 16×16 yang berubah saja** (delta), sehingga antarmuka yang diam hampir tidak memakai data. Mode *Hemat* memakai RGB332 (setengah ukuran). |
| Sentuhan | Ketuk, geser, dan tahan di web disuntikkan sebagai sentuhan asli, sehingga **seluruh** antarmuka merespons seperti disentuh langsung. |
| Tombol | Back, Home, Recents, Notifikasi, Control Center, Kunci, Screenshot, slider kecerahan, daftar aplikasi, serta pengetikan teks dari keyboard perangkat pengontrol. |
| Pengaturan | AP hidup/mati, nama dan sandi WiFi, mulai otomatis, mode lihat-saja, layar tetap menyala, kualitas gambar, batas FPS, izin melalui WiFi rumah, dan **Tanpa preview**. Dapat diatur dari web **atau** dari aplikasi *Remote* pada perangkat. |
| Tanpa preview | Mematikan pengiriman layar ke web (hanya header kecil yang dikirim, tanpa alokasi buffer dan snapshot). Sentuhan tetap diteruskan. Berguna untuk mengukur FPS asli, misalnya pada app Kamera, tanpa beban streaming. |

**Keamanan.** AP memakai WPA2 dengan sandi yang dibuat saat pertama kali digunakan (tampil di aplikasi Remote dan dapat diganti). Endpoint hanya melayani koneksi melalui AP; akses melalui WiFi rumah ditolak kecuali opsi *Izinkan via WiFi rumah* dinyalakan. Hanya satu penonton aktif pada satu waktu.

**Perlu diketahui.** Mengaktifkan AP menyita RAM internal. Jika fitur HTTPS (Gemini, NASA, Trivia) tiba-tiba menghasilkan `HTTP -1`, matikan AP terlebih dahulu. Layar yang digambar langsung ke panel (NES, DOOM, pemutar video) tidak melewati canvas, sehingga di web tampil beku dengan penanda, tetapi sentuhan tetap diteruskan.

#### 5.3.2 Gamepad web `OK`

Perangkat lain berfungsi sebagai **controller** untuk NES dan DOOM. Sambungkan ke WiFi Accretion, buka `http://192.168.4.1/pad`, lalu mulai permainan.

- **Transport:** WebSocket pada port 81 (library `WebSockets`). Browser mengirim 1 byte *bitmask* setiap kali status tombol berubah.
- **DOOM lengkap:** strafe, lari, senjata 1–7, MAP, tombol F1–F11, geser-lihat ala mouse, dan pengetikan teks (nama save, cheat).
- **Laptop/PC:** halaman `/pad` dapat dikendalikan dengan keyboard dan mouse (WASD atau panah, Spasi, Enter, Esc, 1–7; tombol `L` untuk *mouse-look* DOOM).
- **HUD HW Monitor:** *overlay* transparan berisi CPU per-core, RAM/PSRAM, suhu, baterai, dan FPS permainan.
- **Pengaman input macet:** jika klien terputus, diam lebih dari 700 ms, atau permainan berganti, seluruh tombol direset ke 0 agar tidak "menyangkut". Maksimal 5 klien; input mereka digabung.

### 5.4 Media

#### 5.4.1 Musik (Bluetooth) `OK`

Audio keluar melalui **TWS Bluetooth** dengan ESP32-CAM sebagai ko-prosesor (lihat [bagian 8.4](#84-ko-prosesor-bluetooth-rplink)).

- Hingga 200 lagu pada folder `music/`.
- **Cover album** dari tag ID3 (hanya JPEG baseline; format lain menampilkan gambar piringan hitam).
- **Mode acak biasa atau AI** (RYNE v2, lihat di bawah).
- **Kontrol dari tombol earbuds** (AVRCP).
- Jika CAM melakukan restart, alasannya ditampilkan di layar untuk memudahkan diagnosis.

**RYNE v2** adalah mesin rekomendasi lagu yang berjalan sepenuhnya di perangkat (`ryne_engine.h`, *header-only*). Mesin ini memperkirakan "suasana" lagu (8 kelas) dari 13 fitur perilaku dan audio menggunakan softmax dan filter HMM, kemudian memilih lagu dengan *contextual Thompson Sampling*. Data pembelajarannya disimpan pada `/ryne2.bin`.

#### 5.4.2 Pemutar AVI / MJPEG `OK`

Memutar video MJPEG dengan audio MP3 dalam satu berkas `.avi`; suara keluar melalui TWS.

- **Sinkronisasi.** Jam utama adalah posisi putar audio pada CAM. Frame yang terlambat dilewati sehingga suara tidak pernah tertinggal.
- **Daftar video** tampil sebagai kartu berisi thumbnail, durasi, ukuran, dan status *Lanjut mm:ss*. Thumbnail disimpan di SD sehingga pembukaan berikutnya instan.
- **Seek.** Indeks `idx1` dibaca sekali saat video dibuka, lalu dibuat tabel titik lompat setiap ±1 detik. Akurasi lompatan ±1 detik.
- **Resume.** Posisi terakhir dari 8 video tersimpan. Saat dibuka kembali, pilih *Awal* atau *Lanjut*.
- **Zoom otomatis.** Video kecil diperbesar semaksimal mungkin dengan interpolasi bilinear. Tombol **Z** menonaktifkannya jika FPS terasa turun.
- Tanpa TWS, atau jika audio bukan MP3, video tetap diputar tanpa suara.

| Gestur | Fungsi |
|---|---|
| Ketuk | Membuka panel kontrol dan menjeda. |
| Usap kanan / kiri | Maju / mundur 10 detik. |
| Usap atas-bawah, separuh kiri | Kecerahan. |
| Usap atas-bawah, separuh kanan | Volume TWS. |
| Tahan 0,6 detik | Berhenti. |
| Usap tepi bawah ke atas | Kembali ke Home. |

**Membuat video yang kompatibel**

```bash
ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
       -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k output.avi
```

Audio **wajib MP3 44,1 kHz**, sebaiknya CBR agar seek akurat. Untuk video potret, gunakan `scale=240:320`.

#### 5.4.3 Galeri `OK`

Album adalah **folder pada kartu SD** (maksimal 2 tingkat), seperti galeri ponsel pada umumnya. Thumbnail 72×72 dibuat satu kali dan disimpan pada `/.thumbs/`, sehingga CPU tidak perlu men-decode gambar penuh setiap kali dibuka. Nama cache berupa hash dari path, ukuran, dan waktu ubah, sehingga gambar yang diganti dibuat ulang secara otomatis. Format yang didukung: **JPG dan PNG**.

#### 5.4.4 Kamera (OV2640 pada ESP32-CAM) `OK`

Kamera berada pada modul ESP32-CAM, bukan pada S3. Gambar JPEG dikirim melalui link UART yang sama dengan audio Bluetooth. Dokumentasi lengkap tersedia di [`KAMERA.md`](./KAMERA.md).

> **Status.** Seluruh fitur Kamera telah diuji pemilik pada perangkat, termasuk optimasi link terbaru (ACK tanpa menunggu UI, dedup header JPEG, link UART hingga 5 Mbaud), yang menurut hasil uji pemilik memberi peningkatan signifikan sesuai harapan. Sebelum pengujian pada perangkat, sintaks diperiksa dengan *mock API*; protokol (pemecahan dan penyusunan frame, frame rusak, slot, foto versus preview) serta penulis AVI diuji di komputer melalui `tests_kamera/` (termasuk dedup header, kontrol aliran berbasis nomor urut, serta pemulihan cache header), dan AVI yang dihasilkan berhasil di-decode oleh ffmpeg.

| Fitur | Keterangan |
|---|---|
| Preview langsung | 160×120 (2×), 320×240, dan 640×480 (decode 1/2). Layar dipaksa landscape. HUD menampilkan resolusi, FPS, KB/frame, dan indikator link cepat. |
| Foto | Hingga 1600×1200, disimpan **apa adanya dari sensor** ke `/Kamera/IMG_*.jpg` (tanpa encode ulang) dan muncul otomatis sebagai album "Kamera" di Galeri. Tersedia timer 3/10 detik, mode beruntun 3×/5×, dan kualitas JPEG 4–30. |
| Flash LED (GPIO 4, PWM) | Senter 30% atau 100%, atau kilat penuh saat memotret. Senter mati otomatis setelah 2 menit. |
| Video | AVI MJPEG pada `/Kamera/VID_*.avi`, dapat diputar oleh pemutar AVI bawaan maupun VLC di PC. |
| Timelapse | Interval 2/5/10/30/60 detik menjadi satu berkas AVI (diputar 10 fps). |
| Deteksi gerak | Decode JPEG skala 1/8 (hanya komponen DC) dengan kompensasi cahaya global. Menyimpan `GERAK_*.jpg` otomatis, disertai getar dan *toast*, dengan jeda 6 detik. |
| Kontrol sensor | Kecerahan, kontras, saturasi, ketajaman, white balance, efek (negatif/abu-abu/sepia/RGB), eksposur, mode malam, koreksi lensa, mirror, flip, garis bantu, dan kualitas preview. |

**Deteksi sensor otomatis.** Saat kamera dinyalakan, driver mengenali sensor melalui bus SCCB dan CAM melaporkannya ke S3.

| Sensor | Perilaku |
|---|---|
| OV2640 | Foto hingga 1600×1200. |
| OV3660 / OV5640 | Foto hingga 2048×1536 (buffer foto S3 diperbesar otomatis). OV3660 dikoreksi otomatis (vflip bawaan, saturasi dan kecerahan awal). Autofokus OV5640 tidak diaktifkan. |
| Tidak terdeteksi | Pesan jelas ditampilkan (periksa kabel flat dan pemasangan). |
| Tanpa keluaran JPEG (mis. OV7670/OV7725) | Pesan jelas ditampilkan. |

**Optimasi kinerja**

| Bagian | Pendekatan |
|---|---|
| Link UART | Naik otomatis dari 921,6 kbaud ke **2, 3, 4, atau 5 Mbaud** saat aplikasi dibuka (batas atas dipilih pada Atur → Sistem; bawaan 5M). Throughput sekitar 90 KB/s pada 921k, 300 KB/s pada 3M, dan sekitar 500 KB/s pada 5M. Pembagi UART 4M dan 5M pas (80 MHz ÷ 20 dan ÷ 16). Turun kembali saat aplikasi ditutup. |
| Turun otomatis | Dari batas atas, link turun **satu anak tangga** (5M → 4M → 3M → 2M → 921k) bila kabel tidak bersih: (a) minimal 3 frame rusak dalam 5 detik, CAM diminta pindah langsung; (b) tidak ada frame valid 1,5 detik setelah pindah baud, atau CAM hilang lebih dari 2 detik di baud tinggi, S3 mundur ke 921k, menunggu CAM ikut mundur (maksimal 3 detik), lalu mencoba anak tangga di bawahnya. Tidak naik lagi dalam sesi yang sama. |
| ACK tanpa menunggu UI | S3 mengirim `RP_CAM_ACK [seq]` begitu potongan akhir frame tiba (di `musTask`), bukan setelah UI selesai decode dan menggambar. Kabel tidak menganggur selama decode, sementara UI tetap mengambil frame terbaru. CAM dibangunkan langsung oleh ACK (*task notify*), bukan polling. |
| Kontrol aliran berbasis nomor urut | Jendela 3 frame dihitung dari selisih nomor urut kirim dan ACK, bukan hitungan kredit. ACK atau frame yang hilang tidak membocorkan kredit dan tidak membuat aliran macet. |
| Dedup header JPEG | Header OV2640 (sekitar 600–650 byte) identik di tiap frame preview, sehingga dihilangkan dan diganti id 16-bit; S3 menyalin dari cache. Id harus cocok, bila tidak frame dibuang (tidak pernah menampilkan warna atau gambar yang salah). Bila cache S3 hilang, bit "butuh header" pada ACK membuat frame berikutnya dikirim lengkap; header lengkap tetap dikirim tiap 30 frame. Paling terasa pada preview kecil (sekitar 25% lebih hemat pada 160×120, sekitar 8% pada 320×240). |
| Aliran frame | `GRAB_LATEST`: selalu frame terbaru, latensi rendah, dan tidak menyumbat musik. |
| Sisi S3 | CPU 240 MHz selama aplikasi terbuka; pembacaan UART per blok 256 byte; RX buffer 10 KB (diperlukan pada 5 Mbaud). |
| Sisi CAM | Buffer kamera di PSRAM (2 frame, ukuran UXGA); kamera hanya hidup saat aplikasi terbuka sehingga RAM dikembalikan ke BT/MP3 saat keluar. |

**Risiko yang perlu diuji**

1. **RAM internal CAM:** kamera, A2DP, dan MP3 sekaligus sangat ketat. Jika muncul pesan "RAM CAM kurang", matikan Bluetooth atau musik terlebih dahulu. Ambang pada `CAM_MIN_BLOCK` (`cam_module.h`).
2. **Daya:** ESP32-CAM mudah *brownout*. Gunakan pin 5V dengan catu yang kuat.
3. **Kabel UART 4–5 Mbaud** dapat tidak stabil bila panjang; sistem turun otomatis satu anak tangga, atau pilih batas atas yang lebih rendah secara manual pada Atur → Sistem.
3a. **Dua firmware harus diperbarui bersamaan.** Arti `RP_CAM_ACK` berubah: CAM baru dengan S3 lama membuat preview tersendat sekitar 1 fps, sedangkan CAM lama dengan S3 baru tetap berjalan tetapi tanpa dedup header.
3b. **RAM internal S3:** RX buffer UART naik dari 6 KB ke 10 KB (`CAM_UART_RXBUF`). Bila RAM internal terlalu ketat, nilai ini dapat diturunkan dengan risiko kurang aman pada 5 Mbaud.
4. Musik dapat tersendat sebentar saat aplikasi dibuka atau ditutup (momen pergantian baud).
5. Galeri hanya mengenali foto; video AVI diputar melalui tombol "Putar" atau aplikasi Files.
6. Orientasi kamera pada papan dapat terputar: gunakan Mirror/Flip pada Atur → Layar.

### 5.5 Suara dan AI

#### Wake Word "Hei Nyx" `OK`

Perangkat mendengarkan terus-menerus melalui mikrofon. Saat kata kunci diucapkan, perangkat **bergetar, membuka AI Chat, dan langsung merekam**.

Fitur ini **bukan jaringan saraf**. Pengguna **merekam sendiri 4 sampel** ucapan kata kunci, lalu perangkat mencocokkan suara dengan teknik klasik: MFCC (12 koefisien per frame) dan DTW (toleran terhadap perbedaan kecepatan bicara). Seluruh pemrosesan berlangsung di perangkat tanpa internet. Ambang dikalibrasi otomatis dan tersedia slider *Sensitivitas*.

Konsekuensinya, sistem mengenali **suara dan ucapan pemilik sendiri**, bukan siapa pun yang mengucapkan kata tersebut, dan akurasinya menurun di ruangan bising.

#### AI Chat (Gemini)

Tanya jawab dengan Gemini melalui HTTPS, dengan masukan teks atau dikte suara. Memori percakapan disimpan di SD (`ai_memory.txt`). Membutuhkan `gemini_key.txt`.

**Perintah suara `BETA`.** Gemini bisa mengendalikan HP: *buka app*, *timer*, *musik* (putar, jeda, lanjut, kembali, acak), *kecerahan*, *Home*, dan *kembali*. Di akhir jawabannya Gemini menulis baris `@@CMD {json}` (maksimal 3); baris itu dicabut dari teks yang tampil dan dijalankan oleh loop utama. Contoh: "Hei Nyx, setel timer 10 menit", "buka Musik lalu putar acak". Aksi hanya dijalankan saat HP tidak terkunci, dan hanya dari daftar aksi di atas (tidak ada hapus berkas atau matikan daya).

#### Voice Memo

Merekam melalui mikrofon INMP441 menjadi berkas WAV pada `/memo`, lalu diputar melalui jalur Musik BT.

### 5.6 Sistem dan perangkat

#### Baterai (INA219) `OK`

Daya dibaca dari sensor **INA219** melalui I2C. Aplikasi memiliki lima tab (Ringkas, Grafik, Sensor, Baterai, Per App) dengan kemampuan:

- tegangan, arus, dan daya waktu nyata, plus grafik 10 menit;
- persentase baterai dari kurva tegangan Li-ion, kompensasi resistansi dalam, dan *coulomb counting*;
- deteksi pengisian, mAh masuk dan keluar, estimasi sisa waktu atau waktu penuh, serta hitungan siklus;
- pengaturan lengkap chip (shunt, PGA, resolusi ADC, *averaging*, mode operasi) yang tersimpan di NVS;
- opsi **Arah arus** untuk membalik tanda arus jika pengkabelan shunt terbalik, dan status "Mengisi" yang memicu animasi di lock screen;
- tab **Per App** `BETA`: mAh yang keluar dari baterai dicatat per app (app yang sedang di layar; kategori *Home* dan *Terkunci* terpisah), diurutkan dari yang paling boros. Statistik tersimpan di NVS dan direset otomatis saat baterai penuh (atau lewat tombol Reset). Ini estimasi: pemakaian latar belakang (mis. musik) ikut tercatat pada app yang sedang terbuka.

**Pengkabelan:** VCC 3,3 V, GND, SDA 15, SCL 7. Resistor shunt dipasang seri pada kabel positif baterai. Nilai bawaan: shunt 100 mΩ, gain /8, kapasitas 2300 mAh, resistansi dalam 150 mΩ.

> Persentase baterai merupakan **estimasi** (bukan chip *fuel-gauge* khusus). Isi Kapasitas dan R dalam dengan benar pada tab Baterai agar angkanya masuk akal.

#### Pembaruan OTA per channel `OK`

Memperbarui firmware dari GitHub tanpa kabel, dengan beberapa lapis pengaman.

1. Pilih **channel** (nama branch) sebagai sumber firmware.
2. *Cek update* membaca `info.txt` (branch, build, basis kode, ukuran, MD5).
3. Perangkat **menolak** firmware dari "basis kode" yang berbeda.
4. Sebelum memasang, perangkat memeriksa ukuran, header image ESP32-S3, dan **MD5 seluruh berkas**. Jika salah satu gagal, pembaruan dibatalkan dan firmware lama tidak tersentuh.
5. **Pengaman boot:** firmware baru ditandai *pending*. Jika belum dinyatakan sehat (loop hidup selama 30 detik) hingga boot ke-4, perangkat **kembali otomatis ke firmware sebelumnya**.

**Catatan:**

- Pengaman rollback baru aktif untuk pembaruan yang dilakukan oleh firmware yang sudah memuat kode ini.
- Branch yang belum pernah di-build menampilkan pesan *Channel belum dipublikasikan*. Jalankan workflow build untuk branch tersebut satu kali agar Release `channel-<branch>` terbentuk.
- Firmware lama yang belum memuat kode channel tidak memiliki pemilih branch; pasang satu kali melalui flasher atau SD.

#### Backup dan Restore pengaturan `BETA`

Menyimpan seluruh pengaturan NyxOS ke kartu SD dan memulihkannya, misalnya sebelum flash ulang dengan *erase flash* atau saat pindah ke unit lain. Dibuka dari **Pengaturan → Tentang → Backup & Restore** (tidak tampil pada grid Home).

| Aksi | Hasil |
|---|---|
| **Buat Backup Sekarang** | Seluruh pengaturan di NVS (tema, kecerahan, susunan Home, kalibrasi, kata pelajaran kamus, Remote Control, dan lainnya) ditulis ke `/backup/nyx_NNN.nbk`. Yang disisir adalah isi NVS, bukan daftar tetap, jadi pengaturan dari fitur baru ikut otomatis. Namespace sistem (`nvs*`, `phy*`, `bt*`, `otaguard`) dikecualikan. |
| **Riwayat** | Daftar backup beserta nomor, waktu, jumlah nilai, dan ukuran. Maksimal **6** berkas; yang paling lama dibuang otomatis saat membuat backup baru. |
| **Pulihkan** | Pilih satu backup lalu ketuk dua kali (*Yakin? Ketuk lagi*). Nilainya ditulis kembali ke NVS, lalu HP **restart sendiri** karena pengaturan dibaca ke RAM saat boot. Pemulihan menimpa nilai yang ada di berkas; pengaturan yang dibuat setelah backup tidak dihapus. |
| **Hapus** | Membuang satu berkas backup (juga dengan konfirmasi ketuk dua kali). |
| **Sertakan sandi & API key** | Sakelar, **mati** secara bawaan. Sandi WiFi, akun SIP, akun Chat, API key Gemini, dan sandi AP Remote Control hanya ikut bila sakelar ini dinyalakan. |
| **Enkripsi dengan sandi** | Sakelar. Backup berikutnya dienkripsi dengan sandi 6 sampai 64 karakter yang ditanyakan setiap kali (dua kali saat membuat) dan **tidak disimpan**. |

**Keamanan berkas.**

- Berkas `.nbk` tanpa enkripsi **tidak terenkripsi**: siapa pun yang memegang kartu SD dapat membacanya. Itu sebabnya sandi dan API key tidak disertakan secara bawaan.
- Berkas terenkripsi memakai PBKDF2-HMAC-SHA256 untuk menurunkan kunci, AES-256 mode CTR, dan HMAC-SHA256 (*encrypt-then-MAC*). Sandi salah atau berkas yang diubah ditolak sebelum ada nilai yang diterapkan. Penurunan kunci memakan ±0,5 detik.
- Saat memulihkan, berkas dari SD dianggap tidak tepercaya: CRC32 diperiksa penuh, semua panjang dicek terhadap batas buffer, dan namespace sistem tidak pernah ditulis. Backup rusak ditolak.
- Satu berkas maksimal 96 KB; nilai NVS di atas 32 KB dilewati. Buffer dialokasikan di PSRAM hanya selama operasi.

Format berkas dan kripto ditulis sebagai kode murni C++ (`backup_core.h`, `backup_crypto.h`) dan diuji di komputer, termasuk dibandingkan dengan `hashlib` dan `cryptography` milik Python (`tests_backup/`). Belum diuji pada perangkat.
#### Log crash `BETA`

Saat boot, NyxOS membaca alasan reset terakhir. Untuk reset tidak normal (panic, watchdog, brownout) satu baris ditulis ke `/crashlog.txt` di SD: waktu, alasan, layar yang sedang terbuka, sisa RAM internal (bebas, minimum, blok terbesar), lama menyala, dan, bila core dump flash aktif di build, task + alamat crash + backtrace (decode dengan `addr2line` terhadap berkas ELF dari CI). Detail terbaru tampil di **About Phone**. Berkas dirotasi di 32 KB (`/crashlog.old`).

#### HWmonitor

Memantau langsung di perangkat: beban per-core, RAM internal, PSRAM, dan *largest free block*. Alat utama untuk mendiagnosis masalah memori.

#### Aplikasi pendukung lainnya

Screenshot (BMP ke `/screenshot`), Stopwatch dan Timer (tetap berjalan di latar), Pomodoro, Neopixel, Mic Level, Orientasi3D (MPU6050), Notepad, Canvas, Kalkulator ilmiah, Jam, Files, dan Web File Manager.

### 5.7 Permainan

| Permainan | Keterangan |
|---|---|
| **Inferno** | FPS *raycaster* bertema gotik: musuh beranimasi (jarak dekat, jarak jauh, cepat), boss, 3 tingkat senjata, amunisi terbatas dengan *pickup*, kontrol joystick plus *look* sentuh atau miring. |
| **NES** | Emulator (core Nofrendo) pada task terpisah; ROM `.nes` diletakkan di root SD. |
| **DOOM** | Berbasis doomgeneric, tanpa suara. Berkas `doom1.wad` disediakan sendiri oleh pengguna. |
| **Labirin** | Kejar-kejaran di labirin dengan kontrol sentuh dan miring. |
| **Catur** | Catur online antar HP lewat MQTT atau lawan AI (5 tingkat, termasuk AI yang meniru gaya pemilik dan level eksperimental Nyx) `BETA`. Lihat [bagian 5.2.3](#523-catur-online-dan-lawan-ai-beta). |
| **Klasik** | Snake, Flappy, 2048, TicTacToe, Breakout. |
| **PPU Demo** | Peragaan mesin NyxPPU `EXP`. |

---

## 6. Fitur eksperimental dan catatan perangkat keras

### 6.1 SPI Overclock layar `HW`

**Fungsi.** Opsi pada aplikasi **Pengaturan** untuk menaikkan kecepatan bus SPI ke layar. Tersedia dua preset: **60 MHz** (aman, bawaan) dan **80 MHz**.

**Alasan keberadaannya.** Frame digambar ke sprite lalu didorong ke panel melalui SPI. Bus yang lebih cepat berarti gambar tiba di layar lebih cepat. Dari pengujian pemilik, permainan berat seperti Inferno dan NyxPPU hanya mengalami kenaikan FPS yang nyata pada 80 MHz. Preset 65, 70, dan 75 MHz sengaja dibuang karena hasilnya identik dengan 60 MHz (chip hanya dapat melompat ke nilai clock tertentu).

**Mengapa bergantung pada kabel.** Kodenya tidak bermasalah, tetapi 80 MHz menuntut jalur SCLK dan MOSI yang pendek dan rapi. Jika layar menjadi **bergaris, goyang, pecah, atau warnanya acak**, kabel telah mencapai batasnya; turunkan kembali ke 60 MHz. Perubahan baru berlaku setelah restart.

### 6.2 NyxPPU (mesin piksel ala konsol retro) `EXP`

**Fungsi.** Mesin grafis yang meniru konsep *Pixel Processing Unit* pada konsol klasik.

| Komponen | Spesifikasi |
|---|---|
| Tile bank | 40 tile 8×8 piksel RGB565 di PSRAM. |
| Background | 2 layer 48×36 tile (384×288 piksel virtual), masing-masing dengan scroll sendiri sehingga menghasilkan efek paralaks. |
| Sprite (OAM) | Hingga 32 objek dengan skala, flip, tint, dan bit prioritas (tampil di depan atau di belakang layer BG1). |
| Transparansi | Magenta `0xF81F` sebagai *color key*; tile id 0 pada BG1 berarti lubang. |

**Mengapa masih eksperimen.** Saat ini baru **fase 1**. Mesinnya nyata dan dapat dicoba pada aplikasi **PPU Demo**, tetapi belum dipasang pada Inferno maupun Home/Lock Screen. Perlu dicatat pula bahwa ESP32-S3 tidak memiliki GPU, sehingga seluruh proses tetap berupa *render* perangkat lunak di CPU; yang dikerjakan adalah optimasi agar terasa seperti ada akselerasi.

---

## 7. Konfigurasi melalui kartu SD

### 7.1 Tata letak berkas

```
/
├── gemini_key.txt   nasa_key.txt   wallpaper.jpg   emoji.bin
├── chat.cfg         sip.cfg        turn.cfg        chess.cfg
├── ai_memory.txt    ryne2.bin      notepad.txt     kamus.txt
├── music/   memo/   screenshot/   update/   Kamera/   .thumbs/
├── backup/   books/
├── *.avi   *.mjpeg   *.nes        doom1.wad   (disediakan sendiri)
└── (folder album Galeri)
```

### 7.2 Daftar berkas konfigurasi

| Berkas | Digunakan oleh | Format dan catatan |
|---|---|---|
| `/gemini_key.txt` | AI Chat dan dikte | Berisi API key. Jika SD tidak terdeteksi, kunci disimpan di NVS. |
| `/nasa_key.txt` | NEO, EPIC, Galeri NASA | `DEMO_KEY` dapat dipakai, tetapi batasnya kecil. Aplikasi Astronomi tidak membutuhkan key. |
| `/chat.cfg` | Chat | `host=`, `user=`, `pass=`, `name=` |
| `/sip.cfg` | Telepon (SIP) | `host=`, `user=`, `pass=`, `name=`, dan opsional `transport=tcp` atau `transport=udp` (default `tcp`). |
| `/turn.cfg` | Telepon (relay TURN) | `host=`, `port=` (default 3478), `user=`, `pass=` |
| `wallpaper.jpg` | Home dan Lock Screen | Nama berkas harus persis `wallpaper.jpg`. |
| `/emoji.bin` | Emoji (semua teks dan keyboard) | Opsional. Dibuat `tools/make_emoji_bin.py` dari Noto Emoji atau Twemoji. Tanpa berkas ini dipakai set bawaan 54 sprite. Lihat [bagian 5.1](#51-antarmuka-sistem-ok). |
| `/chess.cfg` | Catur (online) | `host=`, `user=`, `pass=`. Opsional; broker juga dapat diisi lewat app. Terpisah dari `chat.cfg`. |
| `/kamus.txt` | Kamus keyboard | Opsional. Satu kata per baris, yang paling sering di atas (format `kata 1234` juga diterima), maksimum 2 MB. Lihat [bagian 5.1](#51-antarmuka-sistem-ok). |
| `/backup/nyx_NNN.nbk` | Backup & Restore | Dibuat otomatis (maksimal 6 berkas). Tidak terenkripsi kecuali enkripsi sandi dinyalakan. Lihat [bagian 5.6](#56-sistem-dan-perangkat). |

**Contoh `chat.cfg`**

```
host=xxxxxxxx.ala.eu-central-1.emqxsl.com
user=namauser
pass=passwordnya
name=Ren
```

**Contoh `sip.cfg`**

```
host=sip2sip.info
user=username
pass=password
name=Ren
transport=tcp
```

**Contoh `turn.cfg`**

```
host=free.expressturn.com
port=3478
user=USERNAME
pass=PASSWORD
```

### 7.3 Web File Manager

Saat WiFi aktif, buka `http://<IP-perangkat>/` untuk mengunggah, mengedit, mengunduh, dan menghapus berkas, mengatur key Gemini, serta mengganti wallpaper. Lihat catatan keamanannya pada [bagian 9](#9-keamanan-dan-privasi).

---

## 8. Cara kerja internal

### 8.1 Memori sebagai kendala utama

RAM internal hanya **±162 KB**, sedangkan satu koneksi HTTPS membutuhkan blok **kontigu** sekitar 20–30 KB. Tanpa disiplin memori, kegagalan `HTTP -1` dan reboot acak tidak terhindarkan.

| Aturan | Penerapan |
|---|---|
| Buffer besar tidak ditempatkan di RAM internal | Sprite layar, cache, dan buffer decode dialokasikan di PSRAM. |
| Stack task berat dipindahkan ke PSRAM | `createTaskPsramStack()` digunakan untuk Gemini, NES, Musik, Wake Word, Chat, dan Telepon. |
| Menunggu sebelum menyerah | Ada jeda dan percobaan ulang agar lwIP sempat melepas buffer TLS sebelumnya. |
| Aplikasi jaringan dapat "dikorbankan" | **App Memory Guard** melepas cache aplikasi latar saat RAM menipis. |
| mbedTLS diperkecil | *Build flag* pada [bagian 4.3](#43-jalur-c-build-manual-untuk-esp32-s3). |
| Buffer mbedTLS di PSRAM | Library core kustom (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), lihat [bagian 4.5](#45-library-core-kustom-mbedtls-di-psram). |
| Set emoji dari SD di PSRAM | `emjInit()` membaca `/emoji.bin` sekali saat boot; kodepoint, indeks pencarian, dan piksel semuanya dialokasikan di PSRAM (±1,8 KB per emoji). |

### 8.2 Pembagian task

```
Core 0                              Core 1
──────────────────────              ──────────────────────
musTask (prio 3, UART ke CAM)       doomTask   (DOOM, prio 1)
Gemini / Chat / Wake Word           NES task   (stack 48 KB di PSRAM)
WiFi / lwIP (milik ESP-IDF)         callTask   (Telepon, prio 3)
```

`loop()` (UI, input, redraw) berjalan di core yang ditetapkan `CONFIG_ARDUINO_RUNNING_CORE`; default Arduino-ESP32 adalah **Core 1**. Firmware mencetak `[CORE] setup/loop berjalan di core N` saat boot, jadi periksa Serial Monitor. Bila `loop()` ternyata di Core 1, DOOM/NES/Telepon berbagi CPU dengan UI.

Task `nyxHelper` (core ke-2 untuk pencarian Catur Nyx, lihat [5.2.3](#523-catur-online-dan-lawan-ai-beta)) berjalan di core kebalikan `loop()`, hanya hidup selama Nyx mencari, dengan stack 10 KB di PSRAM dan prioritas 1.

**Prioritas task** (angka lebih besar = lebih diutamakan; `loopTask` = 1):

| Task | Prioritas | Alasan |
|---|---|---|
| `musTask`, `callTask` | 3 | Aliran audio UART/RTP sensitif jitter. |
| `wakeTask`, `memoRec`, `micTask` | 2 | Pembacaan mikrofon I2S via DMA tidak boleh kehabisan giliran. |
| Task HTTP/decode/emulator lainnya | 1 | Memblok lama atau selalu `vTaskDelay`. |

Gunakan `createTaskPsramStackPrio()` bila perlu prioritas selain 1.

Antarmuka tetap berada di `loop()` agar urutan penggambaran deterministik. Pekerjaan yang memblokir (HTTP, decode, emulator) dipindahkan ke task terpisah, dan antarmuka hanya membaca statusnya.

> Jangan menulis NVS/`Preferences` atau OTA dari task ber-stack PSRAM: saat cache flash dimatikan sesaat, hal itu bisa membuat perangkat crash.

### 8.3 Render

Frame digambar ke sprite di PSRAM (LovyanGFX) lalu didorong ke panel. Transisi berbasis **waktu** (`millis()`), bukan jumlah frame, sehingga durasinya konsisten walaupun CPU sedang sibuk. Saat Home hanya di-scroll, header, status bar, dan dock tidak digambar ulang.

### 8.4 Ko-prosesor Bluetooth (RPLink)

ESP32-S3 tidak menggunakan Bluetooth untuk audio. Data dikirim melalui UART 921.600 baud ke **ESP32-CAM**, yang men-decode MP3 (libhelix) dan mengirimkannya melalui A2DP ke TWS.

```
SD ─▶ S3 (master) ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
      UI, playlist, RYNE      decode MP3 + buffer PCM
```

**Format frame** (`rplink.h`, **harus identik di kedua sisi**):

```
[0xA5][type][lenLo][lenHi][payload ≤ 1024][xor]
```

`xor` adalah hasil operasi XOR dari `type`, panjang, dan seluruh payload.

| Arah | Perintah |
|---|---|
| S3 → CAM | `P` begin · `D` data · `F` finish · `U` pause · `V` volume · `X` stop · `A` scan · `C` connect · `T` tx-gain · `B` mode BT · `G` ping |
| CAM → S3 | `S` status · `E` lagu habis · `L` hasil scan · `W` scan selesai · `R` hello · `K` tombol earbuds |

Protokol yang sama juga membawa frame kamera (preview, foto, dan kontrol aliran berbasis nomor urut) dan dinaikkan hingga 5 Mbaud selama aplikasi Kamera terbuka, dengan penurunan otomatis bila link tidak bersih.

Volume absolut AVRCP sengaja **tidak diiklankan**, karena pada sebagian earbuds hal ini membuat CAM dimatikan oleh watchdog. Saklar `RP_AVRCP_ENABLE` tersedia untuk mengisolasi masalah memori atau link.

---

## 9. Keamanan dan privasi

Proyek ini ditujukan sebagai perangkat pribadi dan eksperimental. Pertimbangkan hal-hal berikut sebelum menggunakannya dengan data atau jaringan yang sensitif.

| Area | Kondisi saat ini | Rekomendasi |
|---|---|---|
| Sertifikat TLS | Tidak diverifikasi (`setInsecure()`) pada fitur jaringan, termasuk Chat. | Jangan mengirim informasi sensitif. |
| Panggilan SIP | Sinyal SIP berjalan melalui TCP/UDP polos (belum TLS); suara RTP tidak dienkripsi; kata sandi dikirim sebagai *hash digest*. | Gunakan akun khusus untuk perangkat ini; anggap percakapan dapat disadap. |
| Unduhan Gutenberg | Berkas EPUB/TXT diunduh lewat HTTPS tanpa verifikasi sertifikat (`setInsecure()`) dari `gutenberg.org` dan `gutendex.com`. Integritas diperiksa (CRC32 data SD dibandingkan dengan data yang masuk dari jaringan, plus struktur ZIP), tetapi keaslian server tidak. | Hanya unduh dari sumber bawaan; jangan membuka berkas `.bad`. |
| Web File Manager | Tidak ditemukan pemeriksaan kredensial pada rute web server (`/`, `/edit`, `/upload`, `/delete`, `/download`, `/apikey`). Perangkat lain pada jaringan yang sama berpotensi dapat mengakses berkas SD. | Aktifkan hanya pada jaringan tepercaya. |
| Kunci API | `gemini_key.txt` dan `nasa_key.txt` tersimpan sebagai teks biasa pada SD. | Gunakan key dengan kuota terbatas dan jaga kartu SD secara fisik. |
| Remote Control | AP menggunakan WPA2 dengan sandi yang dibuat saat pertama kali; akses via WiFi rumah ditolak secara default; satu penonton aktif sekaligus. | Ganti sandi bawaan dan biarkan opsi *Izinkan via WiFi rumah* tetap mati bila tidak diperlukan. |
| Kredensial TURN | Disimpan pada perangkat atau `/turn.cfg`. | Gunakan kredensial berumur pendek (misalnya Cloudflare dengan TTL). |
| Backup pengaturan | Berkas `.nbk` di SD tidak terenkripsi kecuali opsi enkripsi dinyalakan; sandi WiFi, akun SIP, akun Chat, API key, dan sandi AP Remote hanya ikut bila sakelar *Sertakan sandi & API key* dinyalakan. | Biarkan sakelar itu mati, atau aktifkan enkripsi sandi bila perlu menyertakan rahasia; jaga kartu SD secara fisik. |
| Catur online | Broker memakai TLS tanpa verifikasi sertifikat seperti Chat; akun broker tersimpan di NVS dan `/chess.cfg` sebagai teks biasa. | Gunakan akun broker khusus dan jangan memakai ulang kata sandi lain. |

---

## 10. Batasan yang diketahui

**Memori dan jaringan**

- RAM internal sempit. `HTTP -1` sering kali menandakan fragmentasi memori, bukan masalah jaringan. Mekanisme tunggu dan coba-ulang sudah ada, tetapi belum dapat menghilangkan masalah ini sepenuhnya pada level sketch.
- Sertifikat TLS tidak diverifikasi (lihat [bagian 9](#9-keamanan-dan-privasi)).

**Audio**

- Audio hanya melalui Bluetooth. Tanpa ESP32-CAM dan TWS, tidak ada suara dari aplikasi mana pun.

**Telepon**

- Teruji terbatas: berjalan normal dengan `sip2sip.info` melalui TCP; akun `sip.linphone.org` hanya berfungsi satu jalur (sebagai penerima); mode LAN dan TURN belum terkonfirmasi.
- Belum ada ICE; TURN hanya melalui UDP.
- Tidak mendukung panggilan ke nomor PSTN melalui layanan gratis.

**Kamera**

- Seluruh fitur teruji pada satu unit milik pemilik; perilaku pada unit lain (kabel, catu daya, sensor) dapat berbeda. Lihat risiko yang diuraikan pada [bagian 5.4.4](#544-kamera-ov2640-pada-esp32-cam-ok).

**Fitur lain**

- **Chat:** layar menampilkan ASCII dan emoji (huruf lain tampil `?`); tidak ada notifikasi saat aplikasi tertutup.
- **Catur:** tidak ada jam catur dan belum ada aturan ulangan tiga posisi. Belum diuji pada perangkat; mode online membutuhkan broker MQTT dengan akun sendiri (terpisah dari Chat).
- **Backup & Restore:** maksimal 6 berkas, satu berkas maksimal 96 KB, nilai NVS di atas 32 KB dilewati. Pemulihan menimpa nilai yang ada di berkas dan tidak menghapus pengaturan yang dibuat sesudahnya. Backup tanpa enkripsi dapat dibaca siapa pun yang memegang SD. Belum diuji pada perangkat.
- **Emoji:** hanya emoji tunggal (satu kodepoint). Bendera, urutan ZWJ, keycap, dan varian warna kulit tidak punya sprite sendiri; maksimal 4000 sprite, dan set besar memakan PSRAM (±1,8 KB per emoji). Rinciannya ada di [bagian 5.1](#51-antarmuka-sistem-ok).
- **Baterai:** persentase adalah estimasi. Jika INA219 tidak terdeteksi, persentase dan tegangan berhenti diperbarui (tidak ada *fallback* ADC).
- **Unduh Gutenberg:** TXT maks 3 MB dan EPUB maks 40 MB; daftar memuat paling banyak 96 buku; satu unduhan sekaligus. Filter bahasa hanya untuk sumber Gutendex. Pilihan sumber (Gutendex/OPDS) tidak tersimpan setelah restart (bawaan: Gutendex). Unduhan dua koneksi paralel tersedia di kode tetapi dimatikan (`GUT_PARALLEL 0`).
- **PNG:** maksimal 4 MB dan 8 juta piksel; di atas 2 MB tidak memiliki thumbnail.
- **AVI OpenDML (> 1 GB)** tidak didukung untuk seek dan resume. MP3 VBR dapat bergeser sedikit setelah lompatan jauh.
- **NyxPPU** belum dipasang pada Inferno maupun Home/Lock Screen.

**Build dan rilis**

- Build CI tidak mengunci versi tool, sehingga pembaruan upstream dapat menyebabkan build gagal tanpa perubahan di repositori.
- Library core kustom terikat pada satu versi core. Jika versi core di CI berubah, paket pada Release dilewati (build tetap hijau tetapi memakai library bawaan) hingga workflow *Build Arduino Libs* dijalankan ulang.

---

## 11. Pemecahan masalah

| Gejala | Kemungkinan penyebab | Tindakan |
|---|---|---|
| Fitur HTTPS (Gemini, NASA, Trivia) menghasilkan `HTTP -1` | Fragmentasi RAM internal, atau AP Remote sedang aktif | Matikan AP Remote; periksa *largest free block* di HWmonitor; coba ulang. |
| Tidak ada suara dari aplikasi mana pun | TWS belum tersambung, ESP32-CAM tidak terpasang, atau SD pada modul CAM belum dicabut | Sambungkan TWS melalui aplikasi Musik; periksa pengkabelan dan firmware CAM. |
| Video diputar tanpa suara | TWS tidak tersambung, atau audio bukan MP3 44,1 kHz | Konversi ulang dengan perintah `ffmpeg` pada [bagian 5.4.2](#542-pemutar-avi--mjpeg-ok). |
| Layar bergaris, goyang, atau warnanya acak | SPI 80 MHz melampaui batas kabel | Turunkan ke 60 MHz pada Pengaturan, lalu restart. |
| Panggilan tersambung tetapi bisu atau satu arah | NAT atau CGNAT tanpa relay | Konfigurasikan TURN, atau gunakan mode LAN. |
| Pendaftaran SIP gagal | Transport tidak sesuai penyedia, atau kredensial salah | Coba `transport=udp` pada `sip.cfg`; periksa Server, username, dan password. |
| Tidak mendengar lawan bicara saat menelepon | TWS belum tersambung | Sambungkan TWS terlebih dahulu. |
| Telepon dengan akun `sip.linphone.org` hanya bisa menerima | Akun tersebut hanya berfungsi satu jalur pada firmware ini | Gunakan akun `sip2sip.info`. |
| Emoji tampil sebagai kotak `?` | Emoji tidak ada di set aktif (bendera, urutan ZWJ, atau emoji di luar `emoji.bin`) | Gunakan emoji tunggal, atau buat ulang `emoji.bin` dari sumber yang lebih lengkap. |
| Emoji yang tampil hanya 54 sprite bawaan | `/emoji.bin` tidak dipakai: tidak ada di root SD, SD tidak terbaca, header atau ukuran tidak cocok, atau PSRAM tidak cukup | Salin ulang `emoji.bin` ke root SD, buat ulang dengan `tools/make_emoji_bin.py`, kurangi jumlah emoji, lalu restart. |
| Preview Kamera diam beberapa detik setelah berjalan, tetapi HP tidak mati | Potongan **terakhir** sebuah frame hilang di link UART (lebih sering di baud tinggi) dan slot buffer preview yang sedang diisi bocor (`cam_rx.h` versi lama); setelah tiga kebocoran semua frame dibuang | Sudah diperbaiki: slot yang tak pernah selesai dibebaskan saat frame berikutnya mulai. Bila masih terjadi, lihat angka *dropped* dan *bad* di tab Sistem app Kamera dan turunkan baud link. |
| Emoji tampil acak-acakan (bukan kotak `?`) | Belum dipastikan. `emoji.bin` yang utuh dan benar sudah dikonfirmasi tidak menyebabkannya; preview web Remote (JPEG, ±2 fps) juga dapat memburamkan detail kecil | Jalankan `python preview_emoji_bin.py emoji.bin cek.png` (cek ukuran COCOK dan tampilan sprite), lalu bandingkan dengan foto layar HP asli, bukan preview web. |
| Tab NPS menampilkan "Core ke-2 tidak berjalan" | Task pembantu gagal dibuat (PSRAM tidak cukup untuk stack 10 KB) atau saklar Core ke-2 mati | Nyalakan saklar **Core ke-2**; periksa PSRAM bebas di HWmonitor. |
| Catur menampilkan "Atur broker Catur dulu" atau "Broker Catur belum diatur" | Host, username, atau password broker Catur belum diisi | Isi di app Catur lalu **Simpan**, taruh `/chess.cfg` di SD, atau salin dari pengaturan Chat. |
| Catur menampilkan "Username/password salah" atau "Tidak bisa menghubungi broker" | Kredensial salah, WiFi belum tersambung, atau broker tidak tersedia | Periksa akun dan host broker; pastikan WiFi aktif; bila ada ACL, izinkan `renphone/chess/#`. |
| Layar Jaringan Catur menampilkan "RAM tidak cukup utk selisih" | Bobot awal untuk chip **Selisih** tidak muat di PSRAM | Tutup app lain lalu buka ulang layar Jaringan; tab Jaringan, Bobot, dan Petak tetap dapat dipakai tanpa Selisih. |
| Catur menampilkan "RAM padat, tutup app lain" | RAM internal tidak cukup untuk koneksi TLS | Tutup app lain (App Switcher → Tutup semua), lalu coba lagi. |
| Backup menampilkan "SD Card tidak terdeteksi" | Kartu SD tidak terbaca | Periksa kartu SD; backup disimpan di `/backup/` pada SD. |
| Pulihkan menampilkan "Backup rusak, ditolak" atau "Sandi salah" | Berkas rusak atau diubah, atau sandi enkripsi salah | Pilih backup lain; untuk berkas terenkripsi masukkan sandi yang sama dengan saat backup dibuat. |
| Aplikasi Kamera menampilkan "CAM belum mendukung kamera" | Firmware CAM belum diperbarui | Flash ulang firmware `cam_bt_coprocessor`. |
| Pesan "RAM CAM kurang" | Kamera, A2DP, dan MP3 berjalan bersamaan | Matikan Bluetooth atau musik sebelum membuka Kamera. |
| ESP32-CAM sering restart | *Brownout* akibat catu daya lemah | Gunakan pin 5V dengan catu yang kuat. |
| Muncul pesan *Channel belum dipublikasikan* | Branch belum pernah di-build | Jalankan workflow build untuk branch tersebut satu kali. |
| Pembaruan OTA ditolak | Basis kode firmware berbeda | Perilaku yang disengaja; gunakan firmware dari basis kode yang sama. |
| Persen baterai dan tegangan tidak berubah | INA219 tidak terdeteksi | Periksa pengkabelan SDA 15 dan SCL 7 serta alamat I2C. |
| Wake Word sulit mengenali | Ruangan bising atau sampel kurang representatif | Rekam ulang 4 sampel di lingkungan tenang; sesuaikan slider *Sensitivitas*. |
| Build CI gagal dengan `undefined reference to ...` | Berkas `.ino` baru belum didaftarkan pada langkah `cp` di `build-firmware.yml` | Tambahkan baris `cp <berkas>.ino build_sketch/`, lalu push ulang. |
| Animasi mengisi daya tidak muncul | HP tidak sedang terkunci, charger sudah terpasang sejak boot, atau INA219 tidak mendeteksi arus charge | Kunci HP, tunggu 10 detik, cabut charger 3 detik, lalu colok lagi; cek status "Mengisi" di tab Baterai dan opsi Arah arus. |
| Fitur baru tidak muncul setelah flash | Build terbaru gagal sehingga yang ter-flash adalah `.bin` lama | Periksa Actions dan nomor build di About Phone. |
| Build CI hijau tetapi tidak memakai library kustom | Salah satu syarat pemasangan paket tidak terpenuhi | Periksa log langkah *Pasang* dan *Verifikasi* (lihat [bagian 4.5](#45-library-core-kustom-mbedtls-di-psram)). |

---

## 12. Kontribusi dan pelaporan bug

### 12.1 Pengujian yang paling dibutuhkan

Kontribusi berupa hasil uji pada perangkat nyata sangat membantu, terutama untuk fitur berikut (urut dari yang paling bernilai):

1. **Telepon, mode LAN** antara dua perangkat NyxOS (variabel paling sedikit).
2. **Telepon, mode SIP** ke penyedia selain `sip2sip.info` (`sip.linphone.org` sudah dicoba dan hanya satu jalur).
3. **Telepon dengan TURN** pada jaringan CGNAT atau NAT ketat.
4. **Kamera** pada unit lain, terutama dengan sensor selain OV2640 dan kabel UART yang lebih panjang.
5. **Catur online** antara dua perangkat NyxOS (aturan, sinkronisasi langkah, putus-sambung, AI pengganti saat AFK) dan **lawan AI** pada tiap tingkat.
6. **Backup & Restore**: buat dan pulihkan, dengan dan tanpa enkripsi, lalu pastikan pengaturan kembali setelah restart.

Untuk pengujian Telepon, catat apakah dering muncul, apakah suara dua arah, seberapa jernih, dan apakah ada restart.

### 12.2 Cara melaporkan bug

Sertakan informasi berikut agar masalah dapat direproduksi:

- fitur dan langkah-langkah yang memicu masalah;
- versi atau branch firmware (aplikasi *About Phone* atau *Update*);
- tangkapan layar HWmonitor;
- log Serial Monitor, jika tersedia;
- kondisi AP Remote dan Bluetooth (aktif atau tidak).

### 12.3 Membaca riwayat perubahan

Riwayat perubahan ditulis sebagai komentar langsung di dalam kode, memuat gejala, akar masalah, dan alasan perbaikan. Untuk memahami mengapa suatu bagian ditulis demikian, mulailah dari komentar di atasnya.

---

## 13. Struktur repositori

```
phone.ino                       Inti NyxOS (UI, boot, jaringan, aplikasi dasar, game)
phone_notif_center.ino          Notifikasi dan Dynamic Island
*_renphone.ino                  Satu berkas per fitur (galeri, avi, musik, remote, chat,
                                call, camera, ina219, about, owner, homeedit, ota,
                                wakeword, gamepad, power, doom, nes_*, features, dan lainnya)
ppu_engine_renphone.ino         NyxPPU
emoji_renphone.ino              Dukungan emoji (pembungkus font, keyboard emoji, pemuat /emoji.bin dari SD)
preview_emoji_bin.py            Menggambar isi emoji.bin jadi PNG dan memeriksa ukurannya (untuk memeriksa berkas emoji di komputer/Termux)
emoji_data.h                    54 sprite emoji bawaan (cadangan bila /emoji.bin tidak ada)
kamus_renphone.ino              Kamus keyboard: baris saran, autokoreksi, kata pelajaran (NVS), kamus SD
kamus_core.h                    Logika murni kamus (saran, jarak edit, autokoreksi); tanpa Arduino, diuji di komputer
kamus_data.h                    Kata bawaan Indonesia + Inggris (dibuat tools/gen_kamus.py dari tools/kamus_*.txt)
tools/                          gen_emoji.py (sprite bawaan), make_emoji_bin.py (emoji.bin dari Noto/Twemoji),
                                make_vlw.py (font Unicode untuk app Buku), gen_kamus.py (kamus_data.h)
ptr_renphone.ino                Gestur sistem: pull-to-refresh, swipe-back tepi kiri, panel mengikuti jari
nyxextras_renphone.ino          Log crash, energi per app, perintah suara Gemini, thumbnail screenshot, skeleton loading
ryne_engine.h                   RYNE v2
rplink.h  MjpegClass.h          Protokol link CAM dan parser MJPEG
cam_rx.h  cam_avi.h             Penerima frame kamera dan penulis AVI (sisi S3)
hw_conf.h                       Konfigurasi perangkat keras
cam_bt_coprocessor/             Firmware ESP32-CAM (Bluetooth audio dan kamera)
chat-mqtt/  call-sip/           Panduan Chat dan Telepon
KAMERA.md                       Dokumentasi fitur Kamera
tests_kamera/                   Uji protokol kamera dan penulis AVI di komputer
gutenberg_renphone.ino          Unduh buku dari Project Gutenberg (dibuka dari app Buku)
gutendex_core.h  gutdl_core.h   Logika murni Gutenberg: JSON/OPDS, URL langsung, registry, CRC32, cek ZIP
tests_gutenberg/                Uji logika Gutenberg dan simulasi alur unduh di komputer
tests_kamus/                    Uji kamus di komputer: test_kamus.cpp (logika murni), glue_test.cpp (kamus_renphone.ino dengan mock Arduino)
backup_renphone.ino             Backup & Restore pengaturan (NVS ke SD, opsional terenkripsi)
backup_core.h  backup_crypto.h  Format berkas .nbk dan CRC32; SHA-256, PBKDF2, AES-256-CTR (murni C++, diuji di komputer)
tests_backup/                   Uji backup dan kripto di komputer (kripto dibandingkan dengan Python hashlib/cryptography)
chess_renphone.ino              Catur: mesin aturan, AI (negamax + model gaya pemain), online lewat MQTT
chess_smp.cpp                   Core ke-2 untuk Nyx (Lazy SMP): task pembantu + salinan mesin; di-build bersama chess_engine.inc yang dibuat CI dari chess_renphone.ino
tests_chess/                    Uji mesin catur (perft), AI, dan serialisasi model gaya di komputer; bench_nyx.cpp = percobaan kasar level Nyx; `bench_nyx2.cpp` (di root) = perbandingan minimax vs Nyx lawan tiga jenis bot
ebook_renphone.ino ebook_core.h App Buku dan mesinnya (inflate, ZIP, EPUB, HTML ke teks, tata letak)
tests_ebook/                    Uji mesin Buku di komputer
nofrendo_core/ esp_doom_core/   Core pihak ketiga (di-vendor)
apply_*.py  patch_*.py          Skrip patch bertahap (idempoten)
ci_hoist_types.py               Langkah CI untuk menghindari bug prototipe arduino-cli
.github/workflows/              Build S3, build library kustom, build CAM, web flasher
VERSION                         Nomor versi proyek
Bruce-accretion-phone.bin       Firmware alternatif (fork Bruce), terpisah dari NyxOS
phone.ino.broken  *.bak-v1      Cadangan lama (tidak ikut build)
```

> Folder dan repositori bernama `ren_phone` karena itulah nama awal proyek. Sebagian komentar dan variabel lama masih memakai nama tersebut.

---

## 14. Lisensi dan atribusi

Proyek ini dilisensikan di bawah [AGPL-3.0](./LICENSE).

Kode pihak ketiga mengikuti lisensinya masing-masing:

| Komponen | Keterangan |
|---|---|
| Nofrendo (`nofrendo_core/`) | Emulator NES |
| ESP-DOOM / doomgeneric (`esp_doom_core/`) | Port DOOM |
| LovyanGFX, JPEGDEC, PubSubClient, WebSockets | Library Arduino yang dipasang saat build |
| audio-tools, arduino-libhelix, ESP32-A2DP | Library firmware ko-prosesor CAM |

---

## App Buku (pembaca `.txt`, `.md` dan `.epub`) `OK`

Salin file `.txt`, `.md` atau `.epub` ke folder `/books` pada SD card (dibuat otomatis; file di root SD juga ikut terdaftar), lalu buka app **Buku**.

| Aksi | Hasil |
|---|---|
| Ketuk sepertiga kanan / geser ke kiri | Halaman berikutnya (dengan animasi geser) |
| Ketuk sepertiga kiri / geser ke kanan | Halaman sebelumnya |
| Ketuk tengah | Menu: slider halaman; **Isi**, **Aa** (font), **Tema**, **Tutup**; **Rata** (rata kiri-kanan) dan **Animasi** (hidup/mati) |
| Ikon kaca pembesar (perpustakaan) | Cari judul / penulis dengan keyboard sistem; **x** membersihkan |
| Chip Terakhir / Judul / Penulis | Ganti urutan daftar |

- **Perpustakaan:** sampul EPUB (thumbnail 36x48, disimpan di `/books/.covers`), judul + penulis dari metadata EPUB (cache di `/books/.nyxmeta`, EPUB diproses sekali di latar), persen terbaca, dan tanda **Selesai** saat buku dibaca sampai akhir. Garis tipis di bawah judul menunjukkan kemajuan menyiapkan sampul.
- **Teks rata kiri-kanan** (bisa dimatikan lewat menu). Baris yang akan terlalu renggang dibiarkan rata kiri.
- **Font:** Sans, Serif, Mono, Kecil, Sans+, Serif+ (font GFX bawaan, ASCII). **Font Unicode** `Sans U`, `Serif U`, `Mono U`, `Sans U+`, `Serif U+` muncul di tombol **Aa** bila file `.vlw` ada di `/books/fonts` (salin isi `sd_books_fonts/`). Mencakup Latin-1/Latin Extended-A (aksen), Yunani modern, Kiril dasar, kutip/strip melengkung, elipsis, euro. CJK, Arab, Thai, dll tetap tampil `?`. Tanpa file `.vlw`, huruf beraksen dilipat ke huruf dasar seperti sebelumnya. File dibuat ulang dengan `python3 tools/make_vlw.py`.
- **Gambar di EPUB:** gambar JPEG (baseline) dan PNG di dalam bab tampil satu halaman penuh; hiasan < 3 KB dilewati; JPEG progresif, GIF, SVG, WebP tidak didukung (halaman diberi keterangan).
- **Markdown (`.md`):** judul (`#`), daftar, kutipan, blok kode, tabel, garis pemisah, tebal/miring/kode/tautan dibersihkan menjadi teks; judul masuk daftar isi. Batas 1 MB. Gambar markdown tampil sebagai `[teks alt]`.
- **Posisi baca** disimpan per buku di `/books/.nyxreader` (bab + offset teks, jadi tetap tepat setelah ganti font atau orientasi) bersama pilihan font, tema, rata, animasi dan urutan.
- **EPUB 2 dan 3:** bab dibaca satu per satu dari ZIP (hemat RAM); daftar isi dari NCX atau `nav`. ZIP64 dan EPUB ber-DRM tidak didukung.
- **TXT:** UTF-8, UTF-16 (dengan BOM), Latin-1/Windows-1252. Teks berbaris-keras (gaya Project Gutenberg) disambung ulang menjadi paragraf. Baris seperti `Chapter 1`, `BAB IV`, `Prolog` dikenali sebagai judul dan masuk daftar isi. Batas 3 MB per file.
- **Pengujian:** `tests_ebook/` — `python3 tests_ebook/test_ebook.py` (inflate, EPUB, TXT, tata letak, fuzz), `python3 tests_ebook/test_ebook_v2.py` (UTF-8 + lipat, gambar EPUB, sampul/metadata, Markdown) dan `python3 tests_ebook/run_ui_test.py` (alur antarmuka dengan *mock* LovyanGFX/SD/JPEGDEC/keyboard), semuanya dengan AddressSanitizer. Pemilik juga sudah menguji app Buku langsung di perangkat dan berjalan baik.

### Unduh dari Project Gutenberg `OK`

Buka lewat tombol **Gutenberg** di kepala perpustakaan Buku. Butuh WiFi dan SD card.

Alur: daftar populer atau hasil cari, ketuk buku, layar **Detail**, **Unduh**, berkas masuk ke `/books`, lalu **Buka**. Hanya buku domain publik yang ditampilkan.

| Chip | Fungsi |
|---|---|
| Bahasa | Berganti: semua, `en`, `id`, `fr`, `de`, `es`, `nl`, `pt`, `it`. Hanya untuk sumber Gutendex. |
| Gambar | `Tanpa gbr` (bawaan, EPUB jauh lebih kecil) atau `Dgn gbr`. |
| Urutan | `Populer` atau `Terbaru`. |
| Sumber | `Gutendex` (bawaan) atau `OPDS`. |

- **Dua sumber pencarian:** Gutendex (`gutendex.com`, API JSON komunitas atas katalog Gutenberg) dan feed OPDS resmi `gutenberg.org` (XML; hanya nomor buku, judul, penulis). Bila sumber yang dipilih gagal, pencarian otomatis dicoba di sumber satunya. Pilihan sumber tidak tersimpan setelah restart.
- **URL unduh dibangun langsung dari nomor buku** untuk kedua sumber, tanpa pengalihan: `cache/epub/<N>/pg<N>.epub` (tanpa gambar), `pg<N>-images-3.epub` (dengan gambar), atau `pg<N>.txt` bila tidak ada EPUB. URL dari API hanya cadangan terakhir bila URL langsung gagal (404/410).
- **Sopan ke server:** satu pekerjaan sekaligus, jeda minimal 1,5 detik antar permintaan, dan satu buku tidak bisa dimulai dua kali (registry per nomor buku).
- **Aman dari berkas setengah jadi:** data ditulis ke `<nama>.part`, baru diganti nama menjadi buku setelah lolos verifikasi. Unduhan yang putus disambung dengan `Range` dari byte terakhir; **Coba lagi** melanjutkan `.part` yang masih cocok.
- **Verifikasi sebelum jadi buku:** ukuran di SD, kepala `PK`, SD dibaca balik dan CRC32-nya dicocokkan dengan data yang masuk dari jaringan (berkas sampai 16 MB), serta pemeriksaan ujung dan daftar isi ZIP. Berkas yang gagal disimpan sebagai `<nama>.bad` untuk diperiksa di komputer.
- **Salah tulis SD:** bila data di SD tidak cocok dengan jaringan, unduhan diulang sekali dari awal dengan cara tulis lain (buffer atau langsung). Gagal dua kali menampilkan *Tulis SD rusak 2x. Cek SD card.*
- **Sudah ada?** Sebelum mengunduh, `/books` dicari berdasarkan nomor `[PGnnn]`. Buku yang sudah utuh tidak diunduh ulang; berkas lama yang rusak dipindah ke `.bad`.
- **Pengujian:** `g++ -std=c++11 -o t tests_gutenberg/test_gutendex.cpp && ./t` dan `g++ -std=c++11 -o t tests_gutenberg/test_gutdl.cpp && ./t` (logika murni), serta `python3 tests_gutenberg/run_sim.py` (alur unduh/verifikasi dari kode `.ino` asli dengan stub Arduino). Alur ini sudah diuji pemilik di perangkat (unduh ±5 buku EPUB dengan gambar berhasil); sumber OPDS dan format TXT belum dikonfirmasi secara khusus.


## Build layar ST7796 3,5 inci (sentuh FT6236)

Build ini **terpisah** dari build ILI9341 biasa; kodenya sama, dipilih lewat flag `-DRP_DISPLAY_ST7796`
(semua pengaturan ada di `display_conf.h`). Workflow `Build Firmware Binaries` kini menjalankan dua job:

| | ILI9341 (lama) | ST7796 (baru) |
|---|---|---|
| Rilis per build | `build-N` | `st7796-N` (selalu prerelease) |
| Berkas | `firmware.bin`, `ren_phone_s3_merged.bin` | `firmware-st7796.bin`, `ren_phone_s3_st7796_merged.bin` |
| Channel OTA | `channel-<branch>` | `channel-<branch>-st7796` |
| `info.txt` | `display=ili9341` | `display=st7796` |

HP menolak OTA kalau `display=` di `info.txt` tidak cocok dengan layarnya.

**Wiring bawaan:** pin SPI layar sama dengan ILI9341 (SCLK 12, MOSI 11, MISO 13, DC 2, CS 10, RST 14, BL 21).
FT6236 berbagi bus I2C dengan MPU6050 (SDA 15, SCL 7, alamat 0x38); RST/INT tidak dipakai. Ganti lewat
`RP_TOUCH_SDA / RP_TOUCH_SCL / RP_TOUCH_RST` di `display_conf.h`.

**Kalau setelah flash:** warna negatif -> `RP_ST7796_INVERT 1`; merah/biru tertukar -> `RP_ST7796_RGB_ORDER 1`;
sentuhan terbalik -> `RP_TOUCH_SWAP_XY` / `RP_TOUCH_FLIP_X` / `RP_TOUCH_FLIP_Y`. Sentuh kapasitif tidak memakai wizard kalibrasi.

**Multi-touch (2 jari).** FT6236 melaporkan dua jari. Jari kedua dipakai game (DOOM, NES, Inferno) sehingga joystick dan tombol aksi atau arah pandang bisa dipakai bersamaan. Aktif secara bawaan; matikan dengan `-DRP_MULTITOUCH=0`.

<!-- NYX-README-V1 -->
