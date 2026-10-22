```
 ┌──────────────────────────────────────────────────────────────┐
 │  NyxOS  ·  boot log                                          │
 │                                                              │
 │  [ OK  ] display      ILI9341 320x240                        │
 │  [ OK  ] psram        8 MB OPI                               │
 │  [ OK  ] storage      microSD + FFat                         │
 │  [ OK  ] coprocessor  ESP32-CAM  (Bluetooth audio)           │
 │  [ OK  ] apps         41 loaded                              │
 │  [BETA ] phone-call   SIP + LAN         <- belum diuji       │
 │  [ EXP ] nyx-ppu      pixel engine, fase 1                   │
 │                                                              │
 │  Accretion Phone ready.                                      │
 └──────────────────────────────────────────────────────────────┘
```

# Accretion Phone &nbsp;·&nbsp; NyxOS

**Ponsel buatan sendiri dengan sistem operasi yang ditulis dari nol, berjalan di satu chip ESP32-S3.**

Tanpa Android, tanpa Linux. Launcher, notifikasi, jaringan, emulator, sampai mesin grafisnya adalah kode sendiri di atas FreeRTOS.

`ESP32-S3` · `PSRAM 8 MB` · `41 aplikasi` · `±34.000 baris firmware` · `AGPL-3.0`

> **Soal status fitur.** Hampir semua fitur di sini sudah diuji langsung oleh pembuatnya di perangkat asli. Satu fitur, **Telepon**, baru dirilis dan **belum pernah dites**, jadi bug-nya mungkin ada. Dua hal lain punya catatan khusus: NyxPPU masih eksperimen, dan SPI Overclock tergantung kualitas kabel. Penjelasan lengkapnya ada di [bagian 2](#2--fitur-beta-dan-eksperimen).

---

## Isi

| # | Bagian |
|---|---|
| 1 | [Status fitur sekilas](#1--status-fitur-sekilas) |
| 2 | [Fitur Beta dan eksperimen](#2--fitur-beta-dan-eksperimen) |
| 3 | [Penjelasan fitur satu per satu](#3--penjelasan-fitur-satu-per-satu) |
| 4 | [Perangkat keras dan pin](#4--perangkat-keras-dan-pin) |
| 5 | [Pasang dan build](#5--pasang-dan-build) |
| 6 | [Konfigurasi di SD card](#6--konfigurasi-di-sd-card) |
| 7 | [Cara kerja di balik layar](#7--cara-kerja-di-balik-layar) |
| 8 | [Batasan yang diketahui](#8--batasan-yang-diketahui) |
| 9 | [Melapor bug, struktur repo, lisensi](#9--melapor-bug-struktur-repo-lisensi) |

---

## 1 · Status fitur sekilas

| Tanda | Arti |
|---|---|
| `OK` | Sudah diuji pemilik di perangkat asli dan berjalan baik |
| `BETA` | Sudah ditulis lengkap, **belum diuji**. Bug mungkin ada |
| `HW` | Kode sudah benar, hasilnya bergantung pada kualitas perangkat keras |
| `EXP` | Eksperimen atau kerangka awal |

| Fitur | Status | Catatan singkat |
|---|---|---|
| Antarmuka (Home, Lock Screen, Control Center, dst) | `OK` | Dipakai harian |
| Chat lewat broker MQTT | `OK` | Sudah dites dengan broker sungguhan |
| Musik Bluetooth + AVRCP | `OK` | Cover ID3, kontrol earbuds, mode AI |
| Pemutar AVI (seek, resume, zoom) | `OK` | |
| Wake Word "Hei Nyx" | `OK` | Berhasil dikenali |
| Update OTA per channel | `OK` | Termasuk pengaman rollback |
| Remote Control (web) | `OK` | |
| Gamepad web untuk NES dan DOOM | `OK` | |
| Baterai (INA219), Galeri, HWmonitor, Profil, dll | `OK` | |
| **Telepon (SIP + LAN)** | **`BETA`** | **Fitur baru rilis, belum pernah dites** |
| SPI Overclock layar | `HW` | Hasilnya tergantung batas fisik kabel |
| NyxPPU (mesin piksel) | `EXP` | Baru fase 1, hanya di app PPU Demo |

> "Diuji" di sini berarti diuji pemilik pada **satu unit perangkat miliknya**. Unit lain dengan kabel atau komponen berbeda bisa saja berperilaku lain.

---

## 2 · Fitur Beta dan eksperimen

Bagian ini menjelaskan tiga fitur yang statusnya bukan `OK`, supaya kamu tahu persis apa yang boleh diharapkan.

### 2.1 Telepon (SIP + antar-HP lewat WiFi) &nbsp; `BETA`

**Apa ini?** Aplikasi panggilan suara lewat WiFi. Ada dua mode:

| Mode | Untuk apa | Butuh internet? |
|---|---|---|
| **SIP** | Menelepon alamat SIP lain (`teman@sip.linphone.org`) lewat akun gratis | Ya |
| **LAN** | Menelepon langsung antar HP NyxOS yang berada di WiFi yang sama | Tidak |

Keduanya memakai satu "agen SIP" yang sama di UDP port 5060, dengan suara G.711 (PCMU/PCMA, 8 kHz, paket 20 ms) lewat RTP.

**Kenapa disebut Beta?** Karena belum ada satu pun panggilan nyata yang pernah dilakukan dengan fitur ini di HP asli. Yang sudah dicek baru sintaks kode serta uji codec dan parser di PC. Artinya, risiko bug ada di mana-mana: dari pendaftaran akun, bunyi dering, sampai suara tidak terdengar.

**Alur suara (penting dipahami):**

```
Mic INMP441 ──16 kHz──▶ turun ke 8 kHz ──▶ G.711 ──RTP──▶ lawan bicara

lawan bicara ──RTP──▶ G.711 ──▶ PCM 8 kHz ──▶ jalur audio-eksternal
                                              ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
```

Karena ponsel ini **tidak punya speaker**, suara lawan bicara hanya bisa terdengar lewat **TWS Bluetooth**. Tanpa TWS tersambung, mic tetap mengirim suara, tapi kamu tidak mendengar apa-apa.

**Cara memakai:**

1. Pasang fitur lewat `python3 apply_call_patch.py` (aman dijalankan ulang).
2. **Mode SIP:** daftar akun gratis di <https://subscribe.linphone.org>. Di app Telepon buka *Atur*, isi Server `sip.linphone.org`, username, password, dan nama. Atau taruh file `/sip.cfg` di SD (isi `host=`, `user=`, `pass=`, `name=`). Lalu di tab SIP ketik alamat tujuan dan tekan Panggil.
3. **Mode LAN:** di tab LAN isi IP HP lawan, atau tekan **Cari HP NyxOS** (mengirim broadcast SIP OPTIONS), lalu ketuk hasilnya. HP penerima harus satu WiFi dan opsi **Siaga terima panggilan** aktif.
4. Sambungkan TWS dulu lewat app Musik.

**Yang perlu diwaspadai:**

- Hanya bisa menelepon alamat SIP. **Nomor HP biasa (PSTN) tidak bisa** lewat layanan gratis.
- Tidak ada ICE, jadi panggilan lewat internet bisa **satu arah atau bisu** di jaringan dengan NAT tertentu. Mode LAN paling andal.
- UDP polos tanpa enkripsi. Password dikirim sebagai hash digest, tetapi suaranya tidak terenkripsi.
- Latensi satu arah diperkirakan 300–500 ms (karena A2DP).
- Siaga **mati secara default** untuk menghemat RAM dan baterai. Panggilan masuk ditolak otomatis saat NES atau DOOM berjalan.
- Mic dipakai bersama fitur lain. Tutup Voice Memo dan Mic Level sebelum menelepon. Wake Word dijeda otomatis.
- Patch ini ikut mengubah `rplink.h` dan firmware CAM. Flash ulang CAM opsional, tapi membuat suara sekitar 140 ms lebih cepat.

**Kalau mau membantu menguji**, urutan yang paling berguna:

1. Mode LAN antar dua HP NyxOS (paling sedikit variabelnya).
2. Mode SIP ke akun Linphone lain.
3. Catat: apakah dering muncul, apakah suara dua arah, seberapa jernih, dan apakah ada restart.

Panduan singkat fitur ini juga ada di [`call-sip/README.md`](./call-sip/README.md).

### 2.2 SPI Overclock layar &nbsp; `HW`

**Apa ini?** Pilihan di app **Pengaturan** untuk menaikkan kecepatan bus SPI ke layar. Ada dua preset: **60 MHz** (aman, bawaan) dan **80 MHz**.

**Kenapa ada?** Frame digambar ke sprite lalu didorong ke panel lewat SPI. Bus yang lebih cepat berarti gambar sampai ke layar lebih cepat. Dari pengujian pemilik, game berat seperti Inferno dan NyxPPU naik fps-nya secara nyata hanya di 80 MHz. Preset 65, 70, dan 75 MHz sengaja dibuang karena hasilnya sama persis dengan 60 MHz (chip hanya bisa melompat ke nilai clock tertentu).

**Kenapa disebut tergantung kabel?** Kodenya tidak salah, tetapi 80 MHz menuntut jalur SCLK dan MOSI yang pendek dan rapi. Kalau layar jadi **bergaris, goyang, pecah, atau warnanya acak**, itu tanda kabel sudah mentok. Turunkan lagi ke 60 MHz. Perubahan baru berlaku setelah restart.

### 2.3 NyxPPU (mesin piksel ala konsol retro) &nbsp; `EXP`

**Apa ini?** Mesin grafis yang meniru konsep *Pixel Processing Unit* pada konsol lama:

| Komponen | Spesifikasi |
|---|---|
| Tile bank | 40 tile 8×8 piksel RGB565 di PSRAM |
| Background | 2 layer 48×36 tile (384×288 piksel virtual), masing-masing punya scroll sendiri, jadi ada efek parallax |
| Sprite (OAM) | Hingga 32 objek dengan skala, flip, tint, dan bit prioritas (tampil di depan atau belakang layer BG1) |
| Transparansi | Magenta `0xF81F` sebagai color key; tile id 0 di BG1 berarti lubang |

**Kenapa masih eksperimen?** Ini baru **fase 1**. Mesinnya nyata dan bisa dicoba di app **PPU Demo**, tetapi belum dipasang ke Inferno maupun Home/Lock Screen. Perlu jujur juga: ESP32-S3 tidak punya GPU, jadi semuanya tetap render software di CPU. Yang dikerjakan adalah optimasi supaya terasa seperti ada akselerasi.

---

## 3 · Penjelasan fitur satu per satu

### 3.1 Antarmuka sistem

NyxOS meniru pengalaman ponsel modern di layar kecil 320×240.

| Komponen | Fungsi |
|---|---|
| **Lock Screen** | Jam, wallpaper kustom, geser untuk buka kunci |
| **Home** | Grid ikon dengan pencarian, widget cuaca (Open-Meteo, tanpa API key), badge notifikasi di pojok ikon |
| **Control Center** | Tombol bulat: WiFi, Airplane, DND, Tema, Orientasi, Shake, Kunci, Neopixel, Senyap, Bluetooth, Tangkap layar; plus slider kecerahan |
| **Notification Shade** | Riwayat notifikasi |
| **Dynamic Island** | Pil hitam di atas layar yang berubah bentuk sesuai kejadian |
| **App Switcher** | Berpindah antar app |
| **Always-On Display** | Tampilan hemat daya saat layar redup |
| **Keyboard layar** | Untuk mengetik di semua app |
| **7 tema** | Aurora, Dark, AMOLED, Light, Pastel, iOS, Full Dark |

Getaran sengaja dibuat **minimal**: hanya untuk kejadian yang perlu, bukan di tiap ketukan. Efek kaca buram adalah tiruan murah (sampel warna dari piksel yang sudah tergambar, dicampur tint tema), bukan blur sungguhan, supaya tetap ringan.

**Wallpaper.** Taruh `wallpaper.jpg` di SD (atau unggah lewat Web File Manager). Berlaku di Home dan Lock Screen, langsung aktif tanpa restart.

**Atur Home.** Ubah urutan ikon dan sembunyikan app dari grid. Ada tombol Reset dengan konfirmasi.

**Profil.** Kartu avatar dan nama pemilik di atas Pengaturan. Foto dipilih dari SD.

**About Phone.** Info perangkat dan sistem.

### 3.2 Komunikasi

#### Chat (MQTT) &nbsp; `OK`

Chat pribadi antar HP **tanpa server buatan sendiri**. Sebuah broker MQTT (TLS, port 8883, login username dan password) berperan sebagai servernya.

- **Riwayat tersimpan di broker.** Tiap pesan dikirim sebagai pesan *retained* ke salah satu dari 24 slot bergilir (`renphone/chat/m/<slot>`). Saat HP tersambung, broker langsung mengirim isi 24 slot itu, jadi 24 pesan terakhir muncul, termasuk yang dikirim ketika app Chat kamu tertutup.
- **Format pesan:** `seq|waktu|nama|pesan`. Nomor `seq` dipakai untuk mengurutkan dan membuang duplikat.
- **Hemat RAM:** HP hanya tersambung selagi app Chat terbuka. Setelah ditutup, koneksi TLS dilepas dan RAM kembali.
- **Isi konfigurasi** lewat app (Broker, Username, Password, Nama, lalu *Simpan & sambung*) atau taruh `/chat.cfg` di SD lalu tekan *Muat SD*.
- **Broker:** HiveMQ Cloud Serverless gratis sudah dihentikan. Pilihan gratis yang dibahas di [`chat-mqtt/README.md`](./chat-mqtt/README.md) adalah EMQX Cloud Serverless, tetapi firmware tidak terikat ke satu penyedia.

Batasan: tidak ada notifikasi saat app tertutup, dua HP yang kirim di detik yang sama bisa bertabrakan di satu slot, layar hanya menampilkan ASCII, dan sertifikat broker tidak diverifikasi (`setInsecure()`), jadi jangan kirim hal sensitif.

#### Telepon &nbsp; `BETA`

Lihat [bagian 2.1](#21-telepon-sip--antar-hp-lewat-wifi--beta).

### 3.3 Kendali jarak jauh

#### Remote Control &nbsp; `OK`

HP memancarkan **WiFi sendiri (Access Point)**. Sambungkan HP atau laptop lain, buka `http://192.168.4.1/remote`, dan **seluruh antarmuka Accretion tampil di browser dan bisa dikontrol**.

| Kemampuan | Cara kerja |
|---|---|
| Layar live | Isi canvas dikirim sebagai **tile 16×16 yang berubah saja** (delta), jadi UI yang diam hampir tidak memakai data. Mode *Hemat* memakai RGB332 (setengah ukuran) |
| Sentuh | Ketuk, geser, dan tahan di web diinjeksi sebagai sentuhan asli, jadi **semua** UI merespons persis seperti jari |
| Tombol | Back, Home, Recents, Notifikasi, Control Center, Kunci, Screenshot, slider kecerahan, daftar app, dan ketik teks dari keyboard perangkat pengontrol |
| Pengaturan | AP on/off, nama dan sandi WiFi, mulai otomatis, mode lihat-saja, layar tetap nyala, kualitas gambar, batas FPS, izin lewat WiFi rumah. Bisa diatur dari web **atau** dari app *Remote* di HP |

**Keamanan:** AP memakai WPA2 dengan sandi yang dibuat saat pertama kali (tampil di app Remote, bisa diganti). Endpoint hanya melayani koneksi lewat AP; lewat WiFi rumah ditolak kecuali opsi *Izinkan via WiFi rumah* dinyalakan. Hanya satu penonton aktif sekaligus.

**Perlu diketahui:** menyalakan AP memakan RAM internal. Kalau fitur HTTPS (Gemini, NASA, Trivia) tiba-tiba `HTTP -1`, matikan AP dulu. Layar yang digambar langsung ke panel (NES, DOOM, pemutar video) tidak lewat canvas, jadi di web tampil beku dengan penanda, tetapi sentuhan tetap diteruskan.

#### Gamepad web &nbsp; `OK`

HP lain jadi **controller** untuk NES dan DOOM. Sambung ke WiFi Accretion, buka `http://192.168.4.1/pad`, lalu mulai game.

- Transportasi: WebSocket di port 81 (library `WebSockets`). Browser mengirim 1 byte bitmask tiap tombol berubah.
- **DOOM lengkap:** strafe, run, senjata 1–7, MAP, tombol F1–F11, geser-lihat ala mouse, dan ketik teks (nama save, cheat).
- **Laptop/PC:** halaman `/pad` juga bisa dikontrol dengan keyboard dan mouse (WASD atau panah, Spasi, Enter, Esc, 1–7; tombol `L` untuk mouse-look DOOM).
- **HUD HW Monitor:** overlay transparan berisi CPU per-core, RAM/PSRAM, suhu, baterai, dan FPS game.
- **Anti macet:** klien putus, diam lebih dari 700 ms, atau ganti game, maka tombol direset ke 0 agar tidak "nyangkut". Maksimal 5 klien, input mereka digabung.

### 3.4 Media

#### Musik (Bluetooth) &nbsp; `OK`

Karena tidak ada speaker, audio keluar lewat **TWS Bluetooth** melalui ESP32-CAM sebagai ko-prosesor (lihat [bagian 7](#7--cara-kerja-di-balik-layar)).

- Maksimal 200 lagu di folder `music/`.
- **Cover album** dari ID3 tag (hanya JPEG baseline; selain itu tampil piringan hitam).
- **Mode acak biasa atau AI** (RYNE v2, lihat di bawah).
- **Kontrol dari tombol earbuds** (AVRCP).
- Alasan restart CAM, kalau terjadi, ditampilkan di layar untuk memudahkan diagnosa.

**RYNE v2** adalah mesin rekomendasi lagu on-device (`ryne_engine.h`, header-only). Ia menebak "vibe" lagu (8 kelas) dari 13 fitur perilaku dan audio dengan softmax dan filter HMM, lalu memilih lagu dengan contextual Thompson Sampling. Data belajarnya disimpan di `/ryne2.bin`.

#### Pemutar AVI / MJPEG &nbsp; `OK`

Video MJPEG dengan audio MP3 dalam satu file `.avi`; suara keluar lewat TWS.

- **Sinkron:** jam utama adalah posisi putar audio di CAM. Frame yang telat dilewati sehingga suara tidak pernah tertinggal.
- **Daftar video** tampil sebagai kartu dengan thumbnail, durasi, ukuran, dan status *Lanjut mm:ss*. Thumbnail di-cache di SD sehingga pembukaan berikutnya instan.
- **Seek:** indeks `idx1` dibaca sekali saat video dibuka, lalu dibuat tabel titik lompat tiap ±1 detik. Akurasi lompat ±1 detik.
- **Resume:** posisi terakhir 8 video tersimpan. Buka lagi, pilih *Awal* atau *Lanjut*.
- **Zoom otomatis:** video kecil diperbesar sampai sebesar mungkin dengan interpolasi bilinear. Tombol **Z** mematikannya kalau fps terasa turun.
- Tanpa TWS atau audio bukan MP3, video tetap main tanpa suara.

| Gestur | Fungsi |
|---|---|
| Ketuk | Buka panel kontrol dan pause |
| Usap kanan / kiri | Maju / mundur 10 detik |
| Usap atas-bawah, separuh kiri | Kecerahan |
| Usap atas-bawah, separuh kanan | Volume TWS |
| Tahan 0,6 detik | Berhenti |
| Usap tepi bawah ke atas | Home |

Membuat video:

```bash
ffmpeg -i input.mp4 -vf "scale=320:240:force_original_aspect_ratio=decrease,fps=15" \
       -c:v mjpeg -q:v 7 -c:a libmp3lame -ar 44100 -ac 2 -b:a 128k output.avi
```

Audio **wajib MP3 44,1 kHz**, sebaiknya CBR agar seek akurat. Untuk video portrait, pakai `scale=240:320`.

#### Galeri &nbsp; `OK`

Album adalah **folder di SD** (maksimal 2 tingkat), seperti galeri HP asli. Thumbnail 72×72 dibuat sekali dan disimpan di `/.thumbs/`, jadi CPU tidak perlu men-decode gambar penuh tiap dibuka. Nama cache adalah hash dari path, ukuran, dan waktu ubah, sehingga gambar yang diganti otomatis dibuat ulang. Mendukung **JPG dan PNG**.

### 3.5 Suara dan AI

#### Wake Word "Hei Nyx" &nbsp; `OK`

HP mendengarkan terus lewat mic. Begitu kamu mengucapkan kata kunci, HP **bergetar, membuka AI Chat, dan langsung merekam**.

Ini bukan neural network. Kamu **merekam sendiri 4 sampel** ucapan kata kunci, lalu HP mencocokkan suara dengan teknik klasik: MFCC (12 koefisien per frame) dan DTW (toleran terhadap beda kecepatan bicara). Semuanya on-device, tanpa internet. Ambang dikalibrasi otomatis dan ada slider *Sensitivitas*.

Konsekuensinya, ia mengenali **suara dan kata kamu sendiri**, bukan siapa pun yang mengucapkannya, dan lebih sulit di ruangan bising.

#### AI Chat (Gemini)

Tanya jawab dengan Gemini lewat HTTPS, dengan input teks atau dikte suara. Memori percakapan disimpan di SD (`ai_memory.txt`). Butuh `gemini_key.txt`.

#### Voice Memo

Rekam lewat mic INMP441 menjadi WAV di `/memo`, diputar lewat jalur Musik BT.

### 3.6 Sistem dan perangkat

#### Baterai (INA219)

Daya dibaca dari sensor **INA219** lewat I2C. Aplikasi punya 4 tab (Ringkas, Grafik, Sensor, Baterai) dengan:

- tegangan, arus, dan daya real-time, plus grafik 10 menit,
- persen baterai dari kurva tegangan Li-ion, kompensasi resistansi dalam, dan coulomb counting,
- deteksi charging, mAh masuk dan keluar, estimasi sisa waktu atau waktu penuh, serta hitungan siklus,
- pengaturan lengkap chip (shunt, PGA, resolusi ADC, averaging, mode operasi) yang tersimpan di NVS.

Wiring: VCC 3,3 V, GND, SDA 15, SCL 7. Resistor shunt dipasang seri di kabel positif baterai. Bawaan: shunt 100 mΩ, gain /8, kapasitas 2300 mAh, R dalam 150 mΩ.

> Persen baterai adalah **estimasi** (bukan chip fuel-gauge khusus). Isi Kapasitas dan R dalam dengan benar di tab Baterai agar angkanya masuk akal.

#### Update OTA per channel &nbsp; `OK`

Memperbarui firmware dari GitHub tanpa kabel, dengan banyak pengaman.

1. Pilih **channel** (nama branch) sebagai sumber firmware.
2. *Cek update* membaca `info.txt` (branch, build, base, ukuran, MD5).
3. HP **menolak** firmware dari "basis kode" berbeda.
4. Sebelum memasang, HP memeriksa ukuran, header image ESP32-S3, dan **MD5 seluruh file**. Gagal salah satu, update dibatalkan dan firmware lama tidak tersentuh.
5. **Pengaman boot:** firmware baru ditandai "pending". Kalau belum sehat (loop hidup 30 detik) sampai boot ke-4, HP **otomatis kembali ke firmware sebelumnya**.

Catatan: pengaman rollback baru aktif untuk update yang dilakukan oleh firmware yang sudah memuat kode ini, dan repo harus **public** agar HP bisa mengunduh tanpa login.

#### HWmonitor

Pantau langsung di HP: beban per-core, RAM internal, PSRAM, dan *largest free block*. Alat utama untuk mendiagnosa masalah memori.

#### Lain-lain

Screenshot (BMP ke `/screenshot`), Stopwatch dan Timer (tetap jalan di latar), Pomodoro, Neopixel, Mic Level, Orientasi3D (MPU6050), Notepad, Canvas, Kalkulator ilmiah, Jam, Files, dan Web File Manager.

### 3.7 Permainan

| Game | Keterangan |
|---|---|
| **Inferno** | FPS raycaster bertema gothic: musuh teranimasi (jarak dekat, jarak jauh, cepat), boss, 3 tingkat senjata, ammo terbatas dan pickup, kontrol joystick plus look sentuh atau miring |
| **NES** | Emulator (core Nofrendo) di task terpisah, ROM `.nes` di root SD |
| **DOOM** | Berbasis doomgeneric, tanpa suara. File `doom1.wad` disediakan sendiri |
| **Labirin** | Kejar-kejaran di labirin, kontrol sentuh dan miring |
| **Klasik** | Snake, Flappy, 2048, TicTacToe, Breakout |
| **PPU Demo** | Peragaan mesin NyxPPU `EXP` |

---

## 4 · Perangkat keras dan pin

| Bagian | Detail |
|---|---|
| Otak | ESP32-S3, flash 16 MB, PSRAM OPI 8 MB |
| Layar | ILI9341 320×240 (SPI2), portrait dan landscape |
| Sentuh | XPT2046 di bus SPI3 terpisah |
| Penyimpanan | MicroSD (SDIO 1-bit) dan FFat internal sebagai cadangan |
| Sensor | MPU6050 (tilt, shake, auto-rotate), INA219 (tegangan, arus, daya) |
| Mikrofon | INMP441 (I2S) |
| Audio keluar | **Hanya Bluetooth (A2DP)** lewat ko-prosesor ESP32-CAM. Tidak ada speaker |
| Lain-lain | Motor getar, Neopixel |
| Partisi | `app3M_fat9M_16MB` |

| Blok | Sinyal → GPIO |
|---|---|
| Layar | SCLK 12 · MOSI 11 · MISO 13 · DC 2 · CS 10 · RST 14 · BL 21 |
| Sentuh | SCLK 6 · MOSI 5 · MISO 4 · CS 9 |
| SD (SDIO 1-bit) | CLK 39 · CMD 38 · D0 40 |
| Mikrofon | SCK 47 · WS 46 · SD 45 |
| Link ke CAM | S3 TX 41 → CAM RX 13 · S3 RX 42 ← CAM TX 14 · GND bersama |
| MPU6050 + INA219 | SDA 15 · SCL 7 (satu bus I2C) |
| Lainnya | Motor getar 18 · Neopixel 48 |

> **ESP32-CAM:** beri daya lewat pin **5V** dan **cabut kartu SD-nya**, karena GPIO 13/14 dipakai bersama slot itu.

---

## 5 · Pasang dan build

**Jalur cepat:** fork repo, jalankan workflow build di GitHub Actions, unduh `.bin` dari artifact, lalu flash.

**Build manual (S3)**

1. Pasang board package **esp32 by Espressif 3.x**, lalu library `LovyanGFX`, `JPEGDEC`, `PubSubClient`, dan `WebSockets`.
2. Satukan semua `*.ino` dan header di root ke satu folder sketch, dengan `phone.ino` sebagai file utama.
3. Salin `nofrendo_core/` dan `esp_doom_core/` ke `src/` (patch kecil yang dibutuhkan ada di workflow CI).
4. Tambahkan flag mbedTLS:
   ```
   -DCONFIG_MBEDTLS_DYNAMIC_BUFFER=1 -DCONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=1
   -DCONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096 -DCONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
   ```
5. Pilih `esp32:esp32:esp32s3`, `FlashSize=16M`, `PSRAM=opi`, `PartitionScheme=app3M_fat9M_16MB`.
6. Compile, unggah, dan ikuti kalibrasi sentuh di boot pertama.

**Build ko-prosesor (CAM):** folder `cam_bt_coprocessor/`, board *AI Thinker ESP32-CAM*, partisi *Huge APP*, PSRAM aktif. Library: `audio-tools`, `arduino-libhelix`, `ESP32-A2DP`. Pastikan `rplink.h` **identik** di kedua sisi.

**Memasang fitur lewat patch.** Banyak fitur berupa pasangan `*_renphone.ino` dan `apply_*.py`. Jalankan dari root repo; skrip aman dijalankan ulang.

```bash
python3 apply_call_patch.py
git add -A && git commit -m "Telepon SIP + LAN" && git push
```

> File `.ino` baru harus ikut didaftarkan di langkah `cp` pada `build-firmware.yml`. Versi `arduino-cli`, core ESP32, dan library di CI belum dikunci, jadi update upstream bisa memecahkan build.

---

## 6 · Konfigurasi di SD card

```
/
├── gemini_key.txt   nasa_key.txt   wallpaper.jpg
├── chat.cfg         sip.cfg
├── ai_memory.txt    ryne2.bin      notepad.txt
├── music/   memo/   screenshot/   update/   .thumbs/
├── *.avi   *.mjpeg   *.nes        doom1.wad  (sediakan sendiri)
└── (folder album Galeri)
```

| File | Dipakai oleh |
|---|---|
| `/gemini_key.txt` | AI Chat dan dikte. Kalau SD tidak terdeteksi, disimpan di NVS |
| `/nasa_key.txt` | NEO, EPIC, Galeri NASA (`DEMO_KEY` boleh, limitnya kecil). Astronomi tidak butuh key |
| `/chat.cfg` | Chat: `host=`, `user=`, `pass=`, `name=` |
| `/sip.cfg` | Telepon: `host=`, `user=`, `pass=`, `name=` |

**Web File Manager.** Saat WiFi aktif, buka `http://<IP-perangkat>/` untuk unggah, edit, unduh, hapus, atur key Gemini, dan ganti wallpaper (nama berkas harus persis `wallpaper.jpg`).

---

## 7 · Cara kerja di balik layar

### Memori adalah musuh utama

RAM internal hanya **±162 KB**, sedangkan satu koneksi HTTPS butuh blok **kontigu** sekitar 20–30 KB. Tanpa disiplin memori, `HTTP -1` dan reboot acak pasti terjadi.

| Aturan | Penerapan |
|---|---|
| Buffer besar jangan di RAM internal | Sprite layar, cache, dan buffer decode dialokasikan di PSRAM |
| Stack task berat dipindah ke PSRAM | `createTaskPsramStack()` untuk Gemini, NES, Musik, Wake Word, Chat, Telepon |
| Tunggu sebelum menyerah | Ada jeda dan coba-ulang agar lwIP sempat melepas buffer TLS sebelumnya |
| App jaringan bisa dikorbankan | **App Memory Guard** melepas cache app latar saat RAM menipis |
| mbedTLS diperkecil | Flag compile di bagian 5 |

### Pembagian task

```
Core 0                              Core 1
──────────────────────              ──────────────────────
loop()  → UI, input, redraw         doomTask   (DOOM)
musTask → UART ke ESP32-CAM         NES task   (stack 48 KB di PSRAM)
Gemini / Chat / Wake Word           callTask   (Telepon)
```

UI tetap di `loop()` agar urutan gambar deterministik. Pekerjaan yang memblokir (HTTP, decode, emulator) dipindah ke task terpisah, dan UI hanya membaca statusnya.

### Render

Frame digambar ke sprite di PSRAM (LovyanGFX) lalu didorong ke panel. Transisi berbasis **waktu** (`millis()`), bukan jumlah frame, sehingga durasinya konsisten walau CPU sibuk. Saat Home hanya di-scroll, header, status bar, dan dock tidak digambar ulang.

### Ko-prosesor Bluetooth (RPLink)

ESP32-S3 tidak memakai Bluetooth untuk audio. Data dikirim lewat UART 921600 ke **ESP32-CAM**, yang men-decode MP3 (libhelix) dan mengirim A2DP ke TWS.

```
SD ─▶ S3 (master) ──UART──▶ ESP32-CAM ──A2DP──▶ TWS
      UI, playlist, RYNE      decode MP3 + buffer PCM
```

Format frame (`rplink.h`, **harus identik di kedua sisi**): `[0xA5][type][lenLo][lenHi][payload ≤1024][xor]`, dengan `xor` hasil XOR dari type, panjang, dan seluruh payload.

| Arah | Perintah |
|---|---|
| S3 → CAM | `P` begin · `D` data · `F` finish · `U` pause · `V` volume · `X` stop · `A` scan · `C` connect · `T` tx-gain · `B` mode BT · `G` ping |
| CAM → S3 | `S` status · `E` lagu habis · `L` hasil scan · `W` scan selesai · `R` hello · `K` tombol earbuds |

Volume absolut AVRCP sengaja **tidak diiklankan**, karena pada sebagian earbuds membuat CAM mati oleh watchdog. Saklar `RP_AVRCP_ENABLE` tersedia untuk mengisolasi masalah memori atau link.

---

## 8 · Batasan yang diketahui

- **RAM internal sempit.** `HTTP -1` sering berarti fragmentasi memori, bukan jaringan. Sudah ada mekanisme tunggu dan coba-ulang, tetapi belum bisa dihilangkan sepenuhnya dari level sketch.
- **Audio hanya lewat Bluetooth.** Tanpa ESP32-CAM dan TWS, tidak ada suara dari app mana pun.
- **Telepon belum teruji** (lihat bagian 2.1).
- **Sertifikat TLS tidak diverifikasi** (`setInsecure()`) di fitur jaringan. Jangan kirim data sensitif.
- **Chat:** layar hanya ASCII; tidak ada notifikasi saat app tertutup.
- **Baterai:** persen adalah estimasi. Kalau INA219 tidak terdeteksi, persen dan tegangan berhenti diperbarui (tidak ada fallback ADC).
- **PNG:** maksimal 4 MB dan 8 juta piksel; di atas 2 MB tidak punya thumbnail.
- **AVI OpenDML (> 1 GB)** tidak didukung untuk seek dan resume. MP3 VBR bisa bergeser sedikit setelah lompat jauh.
- **NyxPPU** belum terpasang di Inferno maupun Home/Lock Screen.
- **Build CI** tidak mengunci versi tool, jadi update upstream bisa memecahkan build tanpa perubahan di repo.

---

## 9 · Melapor bug, struktur repo, lisensi

**Melapor bug.** Terutama untuk fitur Telepon, laporan sangat membantu. Sertakan fitur dan langkah yang memicu masalah, versi atau branch firmware (app *About Phone* atau *Update*), foto layar HWmonitor, log Serial Monitor kalau ada, dan apakah AP Remote atau Bluetooth sedang menyala.

**Struktur repo**

```
phone.ino                    inti NyxOS (UI, boot, jaringan, app dasar, game)
phone_notif_center.ino       notifikasi / Dynamic Island
*_renphone.ino               satu file per fitur (galeri, avi, musik, remote,
                             chat, call, ina219, about, owner, homeedit, ota, dst)
ppu_engine_renphone.ino      NyxPPU
ryne_engine.h                RYNE v2
rplink.h  MjpegClass.h       protokol link CAM, parser MJPEG
cam_bt_coprocessor/          firmware ESP32-CAM
chat-mqtt/  call-sip/        panduan Chat dan Telepon
nofrendo_core/ esp_doom_core/  core pihak ketiga (di-vendor)
apply_*.py  patch_*.py       skrip patch bertahap (idempoten)
ci_hoist_types.py            langkah CI untuk menghindari bug prototype arduino-cli
.github/workflows/           build S3, build CAM, flasher web
Bruce-accretion-phone.bin    firmware alternatif (fork Bruce), terpisah dari NyxOS
```

Riwayat perubahan ditulis sebagai komentar langsung di kode: gejala, akar masalah, dan alasan perbaikan. Kalau penasaran kenapa sebuah bagian ditulis begitu, mulai dari komentar di atasnya.

> Folder dan repo bernama `ren_phone` karena itu nama awal proyek. Sebagian komentar dan variabel lama masih memakai nama tersebut.

**Lisensi:** [AGPL-3.0](./LICENSE). Kode pihak ketiga (Nofrendo, ESP-DOOM/doomgeneric) mengikuti lisensinya masing-masing.

```
$ nyxos --status
all systems nominal. phone-call: untested. godspeed.
```
