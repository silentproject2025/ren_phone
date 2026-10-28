# Accretion Phone · NyxOS

**Ponsel buatan sendiri dengan sistem operasi yang ditulis dari nol, berjalan pada satu chip ESP32-S3.**

NyxOS tidak berbasis Android maupun Linux. Peluncur aplikasi (launcher), pusat notifikasi, tumpukan jaringan, emulator, pemutar media, hingga mesin grafisnya merupakan kode sendiri yang berjalan di atas FreeRTOS.

| | |
|---|---|
| **Mikrokontroler** | ESP32-S3, flash 16 MB, PSRAM OPI 8 MB |
| **Aplikasi bawaan** | 42 aplikasi |
| **Ukuran kode** | ±37.400 baris firmware utama + ±1.400 baris firmware ko-prosesor (tidak termasuk kode pihak ketiga) |
| **Versi** | 1.0.0 (lihat berkas `VERSION`) |
| **Lisensi** | AGPL-3.0 |

> **Catatan mengenai status pengujian.** Sebagian besar fitur telah diuji langsung oleh pembuatnya pada perangkat fisik. Terdapat dua pengecualian yang perlu diketahui sejak awal:
>
> - **Telepon** baru teruji sebagian: berhasil dengan akun `sip2sip.info` (SIP melalui TCP), sedangkan penyedia SIP lain, mode LAN, dan TURN belum terkonfirmasi.
> > - **NyxPPU** masih berupa eksperimen (fase 1), dan **SPI Overclock** bergantung pada kualitas kabel.
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
| Antarmuka sistem (Home, Lock Screen, Control Center, dan lainnya) | `OK` | Digunakan sehari-hari. |
| Chat melalui broker MQTT | `OK` | Diuji dengan broker sungguhan. |
| Musik Bluetooth dan AVRCP | `OK` | Cover ID3, kontrol dari earbuds, mode acak AI. |
| Pemutar AVI (seek, resume, zoom) | `OK` | |
| Wake Word "Hei Nyx" | `OK` | Berhasil dikenali. |
| Pembaruan OTA per channel | `OK` | Termasuk pengaman rollback. |
| mbedTLS di PSRAM (library core kustom) | `OK` | Dipasang otomatis oleh CI. Pada uji awal, pemakaian RAM internal turun sekitar 40 KB saat HTTPS aktif. |
| Remote Control (web) | `OK` | |
| Gamepad web untuk NES dan DOOM | `OK` | |
| Baterai (INA219), Galeri, HWmonitor, Profil, dan lainnya | `OK` | |
| **Telepon (SIP dan LAN)** | `SEBAGIAN` | Teruji dengan `sip2sip.info` melalui TCP. Penyedia lain, mode LAN, dan TURN belum terkonfirmasi. |
| Kamera (OV2640 pada ESP32-CAM) | `OK` | Seluruh fitur telah diuji pemilik, termasuk link 3 Mbaud dengan kualitas JPEG 10. Lihat [`KAMERA.md`](./KAMERA.md). |
| SPI Overclock layar | `HW` | Hasil bergantung pada batas fisik kabel. |
| NyxPPU (mesin piksel) | `EXP` | Fase 1; hanya tersedia pada aplikasi PPU Demo. |

---

## 2. Arsitektur sistem

Perangkat terdiri atas dua mikrokontroler yang saling terhubung melalui UART. ESP32-S3 menangani antarmuka, aplikasi, dan jaringan. ESP32-CAM bertindak sebagai **ko-prosesor** yang mengurus Bluetooth audio (A2DP) serta kamera.

```
┌────────────────────────────┐   UART 921.600 baud    ┌──────────────────────────┐
│ ESP32-S3  (utama)          │  (naik ke 2/3 Mbaud    │ ESP32-CAM  (ko-prosesor) │
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
| Layar | ILI9341 320×240 (SPI2), mode portrait dan landscape |
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
- Kabel UART yang panjang dapat membuat link 3 Mbaud tidak stabil. Firmware akan kembali otomatis ke 921.600 baud.

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
git add -A && git commit -m "Pasang fitur" && git push
```

> Berkas `.ino` baru harus didaftarkan pada langkah `cp` di `build-firmware.yml`. Versi `arduino-cli`, core ESP32, dan library di CI **belum dikunci**, sehingga pembaruan dari upstream dapat menyebabkan build gagal tanpa ada perubahan di repositori.

---

## 5. Fitur secara rinci

### 5.1 Antarmuka sistem `OK`

NyxOS menghadirkan pengalaman ponsel modern pada layar kecil 320×240.

| Komponen | Fungsi |
|---|---|
| **Lock Screen** | Jam, wallpaper kustom, geser untuk membuka kunci. |
| **Home** | Grid ikon dengan pencarian, widget cuaca (Open-Meteo, tanpa API key), dan badge notifikasi pada ikon. |
| **Control Center** | Tombol WiFi, Airplane, DND, Tema, Orientasi, Shake, Kunci, Neopixel, Senyap, Bluetooth, dan Tangkap layar, ditambah slider kecerahan. |
| **Notification Shade** | Riwayat notifikasi. |
| **Dynamic Island** | Elemen berbentuk pil di bagian atas layar yang berubah sesuai kejadian. |
| **App Switcher** | Berpindah antaraplikasi. |
| **Always-On Display** | Tampilan hemat daya saat layar diredupkan. |
| **Keyboard layar** | Tersedia untuk seluruh aplikasi. |
| **7 tema** | Aurora, Dark, AMOLED, Light, Pastel, iOS, Full Dark. |

**Keputusan desain.** Getaran sengaja dibuat minimal: hanya untuk kejadian penting, bukan pada setiap ketukan. Efek kaca buram merupakan tiruan ringan (mengambil sampel warna dari piksel yang sudah tergambar lalu mencampurnya dengan *tint* tema), bukan blur sungguhan, agar beban CPU tetap rendah.

**Fitur personalisasi**

- **Wallpaper.** Simpan `wallpaper.jpg` di kartu SD, atau unggah melalui Web File Manager. Berlaku pada Home dan Lock Screen dan langsung aktif tanpa restart.
- **Atur Home.** Mengubah urutan ikon dan menyembunyikan aplikasi dari grid. Tersedia tombol *Reset* dengan konfirmasi.
- **Profil.** Kartu avatar dan nama pemilik di bagian atas Pengaturan; foto dipilih dari kartu SD.
- **About Phone.** Informasi perangkat dan sistem.

### 5.2 Komunikasi

#### 5.2.1 Chat melalui MQTT `OK`

Fitur ini menyediakan pesan pribadi antarperangkat **tanpa server buatan sendiri**. Sebuah broker MQTT (TLS, port 8883, autentikasi username dan password) berperan sebagai servernya.

**Cara kerja**

- **Riwayat disimpan di broker.** Setiap pesan dipublikasikan sebagai pesan *retained* ke salah satu dari 24 slot bergilir (`renphone/chat/m/<slot>`). Ketika perangkat tersambung, broker langsung mengirim isi ke-24 slot tersebut, sehingga 24 pesan terakhir tampil, termasuk pesan yang dikirim saat aplikasi Chat sedang tertutup.
- **Format pesan:** `seq|waktu|nama|pesan`. Nomor `seq` dipakai untuk mengurutkan dan membuang duplikat.
- **Hemat RAM.** Perangkat hanya tersambung selama aplikasi Chat terbuka. Setelah ditutup, koneksi TLS dilepas dan memorinya dikembalikan.

**Konfigurasi.** Isi melalui aplikasi (Broker, Username, Password, Nama, lalu *Simpan & sambung*), atau letakkan `/chat.cfg` pada kartu SD dan tekan *Muat SD*. Format berkas ada pada [bagian 7](#7-konfigurasi-melalui-kartu-sd).

**Pemilihan broker.** Paket gratis HiveMQ Cloud Serverless telah dihentikan. Alternatif gratis yang dibahas pada [`chat-mqtt/README.md`](./chat-mqtt/README.md) adalah EMQX Cloud Serverless, tetapi firmware tidak terikat pada satu penyedia; berpindah broker cukup dengan mengganti isi konfigurasi.

**Batasan:** tidak ada notifikasi saat aplikasi tertutup; dua perangkat yang mengirim pada detik yang sama dapat bertabrakan di satu slot; layar hanya menampilkan ASCII; dan sertifikat broker tidak diverifikasi (lihat [bagian 9](#9-keamanan-dan-privasi)).

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

**Status pengujian.** Telah diuji pada perangkat nyata dengan akun `sip2sip.info` (SIP melalui TCP): pendaftaran akun berhasil, serta panggilan keluar dan masuk antara Ren Phone dan ponsel Android (aplikasi Linphone) tersambung. Hal yang **belum** terkonfirmasi: penyedia SIP lain, mode LAN antara dua perangkat NyxOS, TURN, dan variasi kondisi NAT.

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
   - Buat akun gratis. Yang sudah teruji: <https://www.sip2sip.info> (isi Server dengan `sip2sip.info`). Alternatif: <https://subscribe.linphone.org> (Server `sip.linphone.org`, belum terkonfirmasi).
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

### 5.3 Kendali jarak jauh

#### 5.3.1 Remote Control `OK`

Perangkat memancarkan **WiFi sendiri (Access Point)**. Sambungkan ponsel atau laptop lain, buka `http://192.168.4.1/remote`, dan **seluruh antarmuka Accretion tampil di browser serta dapat dikendalikan**.

| Kemampuan | Cara kerja |
|---|---|
| Layar langsung | Isi canvas dikirim sebagai **ubin 16×16 yang berubah saja** (delta), sehingga antarmuka yang diam hampir tidak memakai data. Mode *Hemat* memakai RGB332 (setengah ukuran). |
| Sentuhan | Ketuk, geser, dan tahan di web disuntikkan sebagai sentuhan asli, sehingga **seluruh** antarmuka merespons seperti disentuh langsung. |
| Tombol | Back, Home, Recents, Notifikasi, Control Center, Kunci, Screenshot, slider kecerahan, daftar aplikasi, serta pengetikan teks dari keyboard perangkat pengontrol. |
| Pengaturan | AP hidup/mati, nama dan sandi WiFi, mulai otomatis, mode lihat-saja, layar tetap menyala, kualitas gambar, batas FPS, dan izin melalui WiFi rumah. Dapat diatur dari web **atau** dari aplikasi *Remote* pada perangkat. |

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

> **Status.** Seluruh fitur Kamera telah diuji pemilik pada perangkat, termasuk link UART 3 Mbaud dengan kualitas JPEG 10. Sebelum pengujian pada perangkat, sintaks diperiksa dengan *mock API*; protokol (pemecahan dan penyusunan frame, frame rusak, slot, foto versus preview) serta penulis AVI diuji di komputer melalui `tests_kamera/`, dan AVI yang dihasilkan berhasil di-decode oleh ffmpeg.

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
| Link UART | Naik otomatis dari 921,6 kbaud ke **2 atau 3 Mbaud** saat aplikasi dibuka (dapat dipilih pada Atur → Sistem). Throughput sekitar 90 KB/s pada 921k dan sekitar 300 KB/s pada 3M. Turun kembali saat aplikasi ditutup. |
| Fallback aman | Jika tidak ada frame valid dalam 1,5 detik setelah pindah baud, S3 kembali ke 921k; CAM juga kembali sendiri setelah 3 detik. |
| Aliran frame | Kontrol kredit (maksimal 2 frame dalam perjalanan) dan `GRAB_LATEST`: selalu frame terbaru, latensi rendah, dan tidak menyumbat musik. |
| Sisi S3 | CPU 240 MHz selama aplikasi terbuka; pembacaan UART per blok 256 byte; RX buffer 6 KB. |
| Sisi CAM | Buffer kamera di PSRAM (2 frame, ukuran UXGA); kamera hanya hidup saat aplikasi terbuka sehingga RAM dikembalikan ke BT/MP3 saat keluar. |

**Risiko yang perlu diuji**

1. **RAM internal CAM:** kamera, A2DP, dan MP3 sekaligus sangat ketat. Jika muncul pesan "RAM CAM kurang", matikan Bluetooth atau musik terlebih dahulu. Ambang pada `CAM_MIN_BLOCK` (`cam_module.h`).
2. **Daya:** ESP32-CAM mudah *brownout*. Gunakan pin 5V dengan catu yang kuat.
3. **Kabel UART 3 Mbaud** dapat tidak stabil bila panjang; sistem kembali otomatis ke 921k, atau pilih 2M/921k secara manual. Pada unit pemilik, 3 Mbaud berjalan lancar.
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

#### Voice Memo

Merekam melalui mikrofon INMP441 menjadi berkas WAV pada `/memo`, lalu diputar melalui jalur Musik BT.

### 5.6 Sistem dan perangkat

#### Baterai (INA219) `OK`

Daya dibaca dari sensor **INA219** melalui I2C. Aplikasi memiliki empat tab (Ringkas, Grafik, Sensor, Baterai) dengan kemampuan:

- tegangan, arus, dan daya waktu nyata, plus grafik 10 menit;
- persentase baterai dari kurva tegangan Li-ion, kompensasi resistansi dalam, dan *coulomb counting*;
- deteksi pengisian, mAh masuk dan keluar, estimasi sisa waktu atau waktu penuh, serta hitungan siklus;
- pengaturan lengkap chip (shunt, PGA, resolusi ADC, *averaging*, mode operasi) yang tersimpan di NVS.

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
├── gemini_key.txt   nasa_key.txt   wallpaper.jpg
├── chat.cfg         sip.cfg        turn.cfg
├── ai_memory.txt    ryne2.bin      notepad.txt
├── music/   memo/   screenshot/   update/   Kamera/   .thumbs/
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

### 8.2 Pembagian task

```
Core 0                              Core 1
──────────────────────              ──────────────────────
loop()  → UI, input, redraw         doomTask   (DOOM)
musTask → UART ke ESP32-CAM         NES task   (stack 48 KB di PSRAM)
Gemini / Chat / Wake Word           callTask   (Telepon)
```

Antarmuka tetap berada di `loop()` agar urutan penggambaran deterministik. Pekerjaan yang memblokir (HTTP, decode, emulator) dipindahkan ke task terpisah, dan antarmuka hanya membaca statusnya.

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

Protokol yang sama juga membawa frame kamera (preview, foto, dan kontrol kredit) dan dinaikkan ke 2 atau 3 Mbaud selama aplikasi Kamera terbuka.

Volume absolut AVRCP sengaja **tidak diiklankan**, karena pada sebagian earbuds hal ini membuat CAM dimatikan oleh watchdog. Saklar `RP_AVRCP_ENABLE` tersedia untuk mengisolasi masalah memori atau link.

---

## 9. Keamanan dan privasi

Proyek ini ditujukan sebagai perangkat pribadi dan eksperimental. Pertimbangkan hal-hal berikut sebelum menggunakannya dengan data atau jaringan yang sensitif.

| Area | Kondisi saat ini | Rekomendasi |
|---|---|---|
| Sertifikat TLS | Tidak diverifikasi (`setInsecure()`) pada fitur jaringan, termasuk Chat. | Jangan mengirim informasi sensitif. |
| Panggilan SIP | Sinyal SIP berjalan melalui TCP/UDP polos (belum TLS); suara RTP tidak dienkripsi; kata sandi dikirim sebagai *hash digest*. | Gunakan akun khusus untuk perangkat ini; anggap percakapan dapat disadap. |
| Web File Manager | Tidak ditemukan pemeriksaan kredensial pada rute web server (`/`, `/edit`, `/upload`, `/delete`, `/download`, `/apikey`). Perangkat lain pada jaringan yang sama berpotensi dapat mengakses berkas SD. | Aktifkan hanya pada jaringan tepercaya. |
| Kunci API | `gemini_key.txt` dan `nasa_key.txt` tersimpan sebagai teks biasa pada SD. | Gunakan key dengan kuota terbatas dan jaga kartu SD secara fisik. |
| Remote Control | AP menggunakan WPA2 dengan sandi yang dibuat saat pertama kali; akses via WiFi rumah ditolak secara default; satu penonton aktif sekaligus. | Ganti sandi bawaan dan biarkan opsi *Izinkan via WiFi rumah* tetap mati bila tidak diperlukan. |
| Kredensial TURN | Disimpan pada perangkat atau `/turn.cfg`. | Gunakan kredensial berumur pendek (misalnya Cloudflare dengan TTL). |

---

## 10. Batasan yang diketahui

**Memori dan jaringan**

- RAM internal sempit. `HTTP -1` sering kali menandakan fragmentasi memori, bukan masalah jaringan. Mekanisme tunggu dan coba-ulang sudah ada, tetapi belum dapat menghilangkan masalah ini sepenuhnya pada level sketch.
- Sertifikat TLS tidak diverifikasi (lihat [bagian 9](#9-keamanan-dan-privasi)).

**Audio**

- Audio hanya melalui Bluetooth. Tanpa ESP32-CAM dan TWS, tidak ada suara dari aplikasi mana pun.

**Telepon**

- Teruji terbatas: berhasil dengan `sip2sip.info` melalui TCP; penyedia lain, mode LAN, dan TURN belum terkonfirmasi.
- Belum ada ICE; TURN hanya melalui UDP.
- Tidak mendukung panggilan ke nomor PSTN melalui layanan gratis.

**Kamera**

- Seluruh fitur teruji pada satu unit milik pemilik; perilaku pada unit lain (kabel, catu daya, sensor) dapat berbeda. Lihat risiko yang diuraikan pada [bagian 5.4.4](#544-kamera-ov2640-pada-esp32-cam-ok).

**Fitur lain**

- **Chat:** layar hanya ASCII; tidak ada notifikasi saat aplikasi tertutup.
- **Baterai:** persentase adalah estimasi. Jika INA219 tidak terdeteksi, persentase dan tegangan berhenti diperbarui (tidak ada *fallback* ADC).
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
| Aplikasi Kamera menampilkan "CAM belum mendukung kamera" | Firmware CAM belum diperbarui | Flash ulang firmware `cam_bt_coprocessor`. |
| Pesan "RAM CAM kurang" | Kamera, A2DP, dan MP3 berjalan bersamaan | Matikan Bluetooth atau musik sebelum membuka Kamera. |
| ESP32-CAM sering restart | *Brownout* akibat catu daya lemah | Gunakan pin 5V dengan catu yang kuat. |
| Muncul pesan *Channel belum dipublikasikan* | Branch belum pernah di-build | Jalankan workflow build untuk branch tersebut satu kali. |
| Pembaruan OTA ditolak | Basis kode firmware berbeda | Perilaku yang disengaja; gunakan firmware dari basis kode yang sama. |
| Persen baterai dan tegangan tidak berubah | INA219 tidak terdeteksi | Periksa pengkabelan SDA 15 dan SCL 7 serta alamat I2C. |
| Wake Word sulit mengenali | Ruangan bising atau sampel kurang representatif | Rekam ulang 4 sampel di lingkungan tenang; sesuaikan slider *Sensitivitas*. |
| Build CI hijau tetapi tidak memakai library kustom | Salah satu syarat pemasangan paket tidak terpenuhi | Periksa log langkah *Pasang* dan *Verifikasi* (lihat [bagian 4.5](#45-library-core-kustom-mbedtls-di-psram)). |

---

## 12. Kontribusi dan pelaporan bug

### 12.1 Pengujian yang paling dibutuhkan

Kontribusi berupa hasil uji pada perangkat nyata sangat membantu, terutama untuk fitur berikut (urut dari yang paling bernilai):

1. **Telepon, mode LAN** antara dua perangkat NyxOS (variabel paling sedikit).
2. **Telepon, mode SIP** ke penyedia selain `sip2sip.info` (misalnya akun Linphone lain).
3. **Telepon dengan TURN** pada jaringan CGNAT atau NAT ketat.
4. **Kamera** pada unit lain, terutama dengan sensor selain OV2640 dan kabel UART yang lebih panjang.

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
ryne_engine.h                   RYNE v2
rplink.h  MjpegClass.h          Protokol link CAM dan parser MJPEG
cam_rx.h  cam_avi.h             Penerima frame kamera dan penulis AVI (sisi S3)
hw_conf.h                       Konfigurasi perangkat keras
cam_bt_coprocessor/             Firmware ESP32-CAM (Bluetooth audio dan kamera)
chat-mqtt/  call-sip/           Panduan Chat dan Telepon
KAMERA.md                       Dokumentasi fitur Kamera
tests_kamera/                   Uji protokol kamera dan penulis AVI di komputer
nofrendo_core/ esp_doom_core/   Core pihak ketiga (di-vendor)
apply_*.py  patch_*.py          Skrip patch bertahap (idempoten)
ci_hoist_types.py               Langkah CI untuk menghindari bug prototipe arduino-cli
.github/workflows/              Build S3, build library kustom, build CAM, web flasher
VERSION                         Nomor versi proyek
Bruce-accretion-phone.bin       Firmware alternatif (fork Bruce), terpisah dari NyxOS
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
