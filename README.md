<div align="center">

# 📱 Accretion Phone
### Firmware "Smartphone" DIY untuk ESP32-S3 — dijalankan oleh **NyxOS**

*Satu chip, satu layar sentuh, satu kartu SD — dan sebuah sistem operasi mini lengkap dengan asisten AI, integrasi NASA, pemutar media, dan lebih dari 25 aplikasi bawaan.*

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue?style=for-the-badge&logo=espressif)
![Language](https://img.shields.io/badge/language-C%2B%2B%20(Arduino)-00979D?style=for-the-badge&logo=cplusplus)
![Display](https://img.shields.io/badge/display-ILI9341%20%2B%20XPT2046-orange?style=for-the-badge)
![Status](https://img.shields.io/badge/status-active%20development-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=for-the-badge)

</div>

---

## ✨ Apa ini?

**Ren Phone** adalah firmware Arduino/ESP32 yang mengubah sebuah ESP32-S3 + layar TFT resistif/kapasitif 2.4" (ILI9341 + XPT2046) menjadi perangkat seperti smartphone mini — lengkap dengan **lock screen**, **home screen**, **Control Center**, **Dynamic Island**, sistem navigasi bergaya iOS/Android dengan animasi *easing* yang mulus, dan **26 aplikasi bawaan**: mulai dari kalkulator, chat AI (Gemini), pemutar MP3/video, sampai penjelajah data astronomi resmi NASA.

Semua ditulis dalam **satu file `.ino` monolitik** (ya, sengaja) dengan disiplin *root-cause debugging* yang ditulis langsung sebagai komentar di setiap perbaikan bug — jadi proyek ini juga berfungsi sebagai *log rekayasa* yang jujur tentang bagaimana firmware embedded yang kompleks berevolusi dari waktu ke waktu.

> 💡 **Kenapa satu file?** Supaya gampang di-*flash* ulang, gampang di-*review* per baris di GitHub, dan gampang dikompilasi otomatis lewat GitHub Actions tanpa drama manajemen banyak file header.

---

## 📚 Daftar Isi

- [Fitur Unggulan](#-fitur-unggulan)
- [Daftar Lengkap Aplikasi](#-daftar-lengkap-aplikasi-26-app)
- [Fitur "Sistem Operasi"](#-fitur-sistem-operasi-nyxos)
- [Hardware yang Dibutuhkan](#-hardware-yang-dibutuhkan)
- [Skema Pengkabelan](#-skema-pengkabelan-wiring)
- [Struktur Folder di SD Card](#-struktur-folder-di-sd-card)
- [Instalasi & Build](#-instalasi--build)
- [Konfigurasi API Key](#-konfigurasi-api-key)
- [Gestur & Kontrol](#-gestur--kontrol)
- [Filosofi Pengembangan](#-filosofi-pengembangan--catatan-teknik)
- [Keterbatasan yang Diketahui](#-keterbatasan-yang-diketahui)
- [Roadmap / Ide Lanjutan](#-roadmap--ide-lanjutan)
- [Kredit & Pustaka Pihak Ketiga](#-kredit--pustaka-pihak-ketiga)
- [Kontribusi](#-kontribusi)
- [Lisensi](#-lisensi)
- [Disclaimer](#-disclaimer)

---

## 🚀 Fitur Unggulan

| | |
|---|---|
| 🌀 **Boot animation sinematik** | Partikel berputar membentuk cincin *accretion disk* yang benar-benar membentuk tulisan "ACCRETION" huruf demi huruf, lalu bertransisi jadi logo OS **NyxOS** — semua dengan gerakan berbasis waktu (bukan hitung-frame) supaya konsisten di kondisi hardware apapun. |
| 🤖 **AI Chat bertenaga Gemini** | Chat teks dua arah dengan Google Gemini, punya **memori percakapan persisten** (disimpan ke SD), dan bisa **didikte suara** — rekam lewat mic INMP441, dikirim sebagai audio ke Gemini, hasil transkrip otomatis mengisi kolom chat. |
| 🪐 **Integrasi resmi NASA** | 5 aplikasi terhubung langsung ke API NASA: *Astronomy Picture of the Day* (dengan auto-translate ke Bahasa Indonesia), asteroid dekat Bumi (NeoWs), foto Mars Rover, citra Bumi dari satelit EPIC, dan pencarian di NASA Image & Video Library — semuanya di-*cache* ke SD/PSRAM biar hemat kuota. |
| 🎬 **Multimedia penuh** | Pemutar video MJPEG, pemutar **AVI** (video MJPEG + audio PCM lewat I2S) dengan parser RIFF/AVI custom, dan pemutar **MP3** yang tetap main di *background* walau pindah aplikasi — persis musik player HP beneran. |
| 🕹️ **5 game klasik** | Snake (dengan mode *wrap-around*), Flappy Block, 2048, Tic-Tac-Toe (bisa 3x3/4x4/5x5, vs CPU atau 2 pemain), dan Breakout (kontrol drag jari **atau** kemiringan HP via MPU6050). |
| 🌐 **Web File Manager bawaan** | Selama WiFi tersambung, buka IP perangkat di browser untuk **upload/download/edit/hapus** file di SD Card, dan mengatur API key — tanpa perlu cabut kartu SD sama sekali. |
| 💫 **Dynamic Island** | Pil notifikasi mengambang ala HP flagship modern, bermorfing antara status tersembunyi/ringkas/melebar dengan animasi *spring easing* — dipakai untuk status AI Chat, mode Game, dan skor Trivia live. |
| 🎨 **4 tema visual** | Dark, AMOLED (hitam pekat, hemat daya di panel OLED), Light, dan Pastel — bisa diganti kapan saja dari Control Center. |
| 📳 **Haptic feedback kaya pola** | Motor getar merespons setiap ketukan, sukses, error, dan peringatan dengan pola getar yang berbeda-beda (bukan cuma nyala/mati polos). |
| 🔄 **OTA Update** | Update firmware langsung dari file `.bin` di SD Card **atau** unduh dari URL lewat WiFi, lengkap dengan progress bar dan konfirmasi ganda anti-salah-pencet. |

---

## 📱 Daftar Lengkap Aplikasi (26 App)

<details>
<summary><b>Klik untuk membuka daftar lengkap 26 aplikasi bawaan</b></summary>

| # | Ikon | Aplikasi | Deskripsi Singkat |
|---|:---:|---|---|
| 1 | 🕐 | **Jam** | Jam digital real-time hasil sinkronisasi NTP + tanggal lengkap |
| 2 | 🧮 | **Kalkulator** | Kalkulator standar dengan operasi dasar, persen, dan negasi |
| 3 | 📐 | **Orientasi 3D** | Visualisasi kotak 3D real-time yang mengikuti kemiringan HP (data MPU6050 mentah: accel, gyro, suhu) |
| 4 | ⚙️ | **Setting** | Kecerahan, ganti tema, koneksi & pemindaian WiFi, kalibrasi ulang touch, kalibrasi sensor gerak |
| 5 | 📝 | **Notepad** | Catatan teks tersimpan ke SD, dengan dialog konfirmasi "Simpan/Buang" saat keluar tanpa menyimpan |
| 6 | 🎨 | **Canvas** | Kanvas gambar jari dengan palet warna, ukuran kuas, dan goresan yang di-*stamp* halus (bukan garis kotak-kotak) |
| 7 | 🤖 | **AI Chat** | Chat dengan Gemini AI, memori percakapan persisten, dan dikte suara via mic |
| 8 | 📁 | **Files** | File explorer SD Card + server web upload/edit/hapus file dari browser |
| 9 | 🎬 | **MJPEG** | Pemutar video format MJPEG dari folder `/mjpeg` di SD |
| 10 | ⬇️ | **Update** | OTA update firmware dari SD (`/update`) atau URL WiFi |
| 11 | 🔋 | **Baterai** | Estimasi persentase & sisa daya dari pembacaan tegangan (voltage divider) |
| 12 | 🐍 | **Snake** | Klasik ular, kontrol ketuk arah, mode *wrap-around* di tepi layar |
| 13 | 🐤 | **Flappy** | Flappy Bird ala blok, kontrol ketuk layar |
| 14 | 🔢 | **2048** | Puzzle geser angka klasik, kontrol swipe 4 arah |
| 15 | ❌⭕ | **TicTacToe** | Bisa papan 3x3/4x4/5x5, mode vs CPU (AI menang→blok→tengah→acak) atau 2 pemain lokal |
| 16 | 🧱 | **Breakout** | Kontrol paddle via drag jari **atau** kemiringan HP (MPU6050) |
| 17 | ❓ | **Trivia** | Kuis trivia online dari Open Trivia DB — 12 tema, 4 tingkat kesulitan, hingga 15 soal |
| 18 | 🌌 | **Astronomi** | *Astronomy Picture of the Day* NASA, teks penjelasan auto-translate ke Bahasa Indonesia |
| 19 | ☄️ | **NEO Asteroid** | Daftar asteroid dekat Bumi per tanggal (NASA NeoWs), tandai yang berpotensi berbahaya |
| 20 | 🚀 | **Mars Rover** | Jelajah foto dari rover Curiosity/Perseverance berdasarkan nomor "sol" (hari Mars) |
| 21 | 🌍 | **Bumi EPIC** | Citra Bumi utuh dari satelit DSCOVR (NASA EPIC) per tanggal |
| 22 | 🖼️ | **Galeri NASA** | Pencarian bebas di NASA Image & Video Library |
| 23 | 💡 | **Neopixel** | Kontrol LED RGB addressable: 8 warna solid + mode Rainbow otomatis + slider kecerahan |
| 24 | 🎵 | **MP3 Player** | Pemutar MP3 dari SD, tetap main di background lintas aplikasi |
| 25 | 📹 | **AVI Player** | Pemutar video+audio format AVI (MJPEG+PCM) hasil konversi `ffmpeg` |
| 26 | 🎙️ | **Mic Level** | VU meter sederhana untuk mengecek mic INMP441 benar-benar menangkap suara |

</details>

---

## 🖥️ Fitur "Sistem Operasi" NyxOS

Di luar daftar aplikasi, ada seluruh lapisan "shell" yang membuatnya terasa seperti OS beneran:

- **Lock Screen** — usap ke atas untuk membuka, jam & tanggal ikut bergerak mengikuti drag jari.
- **Control Center** — usap dari tepi atas layar ke bawah: toggle WiFi, Airplane Mode, DND, ganti Tema, Orientasi layar, Shake-to-Home, Kunci Layar, Neopixel, dan Mode Senyap (getar off), plus slider kecerahan — semua tombol bulat mengambang bergaya HP modern.
- **Dynamic Island** — pil notifikasi di tengah-atas layar yang bermorfing antar 3 keadaan (tersembunyi/ringkas/melebar) dengan animasi *spring easing*, dan bisa diketuk untuk lompat ke aplikasi terkait.
- **Navigasi bertransisi mulus** — geser masuk/keluar antar layar dengan *easing* kubik konsisten, dibungkus satu transaksi SPI supaya tidak patah-patah.
- **Auto-rotate** — orientasi layar (potret/lanskap) menyesuaikan kemiringan fisik perangkat via MPU6050, dengan histeresis anti-*flip-flop*.
- **Shake-to-Home** — goyangkan perangkat untuk langsung kembali ke Home (otomatis nonaktif saat sedang mengetik/menggambar/update firmware).
- **Keyboard virtual QWERTY** + **keypad numerik** khusus (dengan auto-format tanggal `YYYY-MM-DD`) untuk input yang lebih cepat di aplikasi pencarian NASA.
- **Widget "Search Bar" animasi** — ikon kaca pembesar yang melebar mulus jadi kolom pencarian penuh, dipakai di semua aplikasi NASA.
- **Haptic feedback** — pola getar berbeda untuk ketuk, sukses, error, dan peringatan.
- **4 Tema** — Dark, AMOLED, Light, Pastel, tersimpan permanen di NVS.

---

## 🔧 Hardware yang Dibutuhkan

| Komponen | Spesifikasi | Fungsi |
|---|---|---|
| **MCU** | ESP32-S3 (disarankan Flash 16MB / N16 + PSRAM OPI 8MB) | Otak utama, WiFi, dual-core untuk task AI/network di background |
| **Layar** | TFT ILI9341 2.4"–2.8", 240×320, SPI | Layar utama |
| **Touchscreen** | XPT2046 resistif, SPI terpisah | Input sentuh |
| **Penyimpanan** | MicroSD (mode SDIO/SD_MMC) | Cache, media, konfigurasi, log AI |
| **Sensor gerak** | MPU6050 (accel + gyro, I2C) | Auto-rotate, shake detect, app Orientasi 3D, kontrol tilt Breakout |
| **Motor getar** | Motor vibrasi kecil (coin/ERM) | Haptic feedback |
| **LED** | LED RGB addressable (Neopixel-compatible) | App Neopixel & indikator |
| **Ampli audio** | Modul I2S MAX98357A + speaker | MP3 Player & AVI Player |
| **Mikrofon** | INMP441 (I2S digital mic) | Dikte suara AI Chat & Mic Level meter |
| **Baterai** | Li-ion 1 sel (3.0–4.2V) + voltage divider 10k/10k | Estimasi daya |

> Semua modul bisa dibeli terpisah secara umum (breakout board generik) — tidak ada komponen custom/proprietary.

---

## 🔌 Skema Pengkabelan (Wiring)

### Layar TFT ILI9341 (SPI2)
| Pin Layar | Pin ESP32-S3 |
|---|---|
| VCC | 5V *(wajib 5V bila jalur J1 terbuka pada modul)* |
| GND | GND |
| CS | GPIO 10 |
| RESET | GPIO 14 |
| DC | GPIO 2 |
| SDI (MOSI) | GPIO 11 |
| SCK | GPIO 12 |
| SDO (MISO) | GPIO 13 |
| LED (backlight) | GPIO 21 |

### Touchscreen XPT2046 (SPI3, bus terpisah dari layar)
| Pin Touch | Pin ESP32-S3 |
|---|---|
| T_CLK | GPIO 6 |
| T_CS | GPIO 9 |
| T_DIN (MOSI) | GPIO 5 |
| T_DO (MISO) | GPIO 4 |
| T_IRQ | *(tidak dipakai / opsional)* |

### MicroSD (SDIO / SD_MMC)
| Pin SD | Pin ESP32-S3 |
|---|---|
| CLK | GPIO 39 |
| CMD | GPIO 38 |
| D0 | GPIO 40 |

### Sensor Gerak MPU6050 (I2C)
| Pin MPU | Pin ESP32-S3 |
|---|---|
| SDA | GPIO 15 |
| SCL | GPIO 7 |
| VCC | 3.3V |
| GND | GND |
| Alamat I2C | `0x68` |

### Lain-lain
| Modul | Pin | Keterangan |
|---|---|---|
| Motor getar | GPIO 18 | Digital ON/OFF, pola diatur software |
| Neopixel RGB | GPIO 48 | Pakai `rgbLedWrite()` bawaan core, tanpa library eksternal |
| Baterai (ADC) | GPIO 8 | Voltage divider 10k+10k (rasio 1:2), kapasitas rujukan 2300 mAh |
| Ampli MAX98357A | BCLK=42, LRC=41, DIN=17 | Dipakai bergantian oleh MP3 Player & AVI Player |
| Mic INMP441 | SCK=47, WS=46, SD=45 | I2S terpisah dari jalur speaker |

> ⚠️ **Catatan penting soal GPIO 8 (baterai):** pin ini sengaja dipindah dari GPIO 4 karena GPIO 4 bentrok dengan jalur MISO touchscreen — memakainya untuk `analogRead()` akan merusak pembacaan sentuh dan menyebabkan lock screen tidak bisa diusap.

---

## 🗂️ Struktur Folder di SD Card

Firmware akan otomatis membuat folder-folder berikut saat pertama kali boot (kosongkan/isi sesuai kebutuhan):

```
/
├── gemini_key.txt        # API key Google Gemini (dibuat otomatis, edit isinya)
├── nasa_key.txt          # API key NASA (opsional, default DEMO_KEY)
├── notepad.txt           # isi app Notepad
├── ai_memory.txt         # riwayat percakapan AI Chat (auto-terkelola)
├── canvas_land.bin       # buffer gambar Canvas (orientasi landscape)
├── canvas_port.bin       # buffer gambar Canvas (orientasi portrait)
├── mjpeg/                # taruh file *.mjpeg di sini
├── update/               # taruh file firmware *.bin di sini untuk OTA lokal
├── mp3/                  # taruh file *.mp3 di sini
├── avi/                  # taruh file *.avi (MJPEG+PCM) di sini
├── apod_cache/           # cache otomatis: gambar & teks APOD
├── neo_cache/            # cache otomatis: data asteroid NeoWs
├── mars_cache/           # cache otomatis: foto Mars Rover
├── epic_cache/           # cache otomatis: citra Bumi EPIC
└── imglib_cache/         # cache otomatis: hasil pencarian NASA Image Library
```

> 💾 **Tanpa SD card?** Sebagian besar fitur tetap jalan — gambar-gambar dari NASA akan diunduh langsung ke **PSRAM** sebagai fallback (tidak persisten antar boot, tapi tetap bisa dilihat).

---

## 🛠️ Instalasi & Build

### Kebutuhan Library

| Library | Sumber | Catatan |
|---|---|---|
| **LovyanGFX** | Library Manager | Driver layar & touch |
| **JPEGDEC** | Library Manager | Decoder gambar/video JPEG |
| **MjpegClass** | Bundle proyek ini | Wrapper pemutar MJPEG |
| **ESP32-audioI2S** (`Audio.h`) | [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | **Wajib install manual**, tidak bawaan core |
| `ESP_I2S.h`, `WiFi`, `HTTPClient`, `WiFiClientSecure`, `SD_MMC`, `Preferences`, `Update`, `Wire` | Bawaan **arduino-esp32 core 3.x** | Tidak perlu install terpisah |

### Pengaturan Board (Arduino IDE)

- **Board:** `ESP32S3 Dev Module`
- **Flash Size:** `16MB (128Mb)`
- **Partition Scheme:** yang menyediakan slot OTA (mis. *Default 16MB with spiffs*)
- **PSRAM:** `OPI PSRAM`
- **USB Mode / CDC:** sesuaikan modul dev board yang dipakai

### Build via `arduino-cli` (dipakai juga di GitHub Actions proyek ini)

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "LovyanGFX" "JPEGDEC"
# ESP32-audioI2S di-clone manual ke folder libraries Arduino

arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default \
  ren_phone.ino

arduino-cli upload -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default \
  ren_phone.ino
```

Proyek ini juga sudah dilengkapi workflow **GitHub Actions** (`build.yml`) yang otomatis mengompilasi firmware setiap kali ada perubahan kode — bagus untuk memastikan tidak ada regresi build sebelum di-*flash* ke perangkat asli.

### Kalibrasi Pertama Kali

Saat pertama kali menyala (atau setelah kalibrasi direset dari menu Setting), perangkat akan meminta kamu menyentuh titik-titik di sudut layar untuk kalibrasi touch. Ini **hanya perlu dilakukan sekali** — hasilnya tersimpan permanen dan otomatis selalu sinkron dengan orientasi layar yang sedang aktif.

---

## 🔑 Konfigurasi API Key

| Layanan | Dipakai Untuk | Cara Dapat | Wajib? |
|---|---|---|---|
| **Google Gemini** | AI Chat (teks & transkrip suara) | [aistudio.google.com/app/apikey](https://aistudio.google.com/app/apikey) — gratis | Ya, untuk app AI Chat |
| **NASA API** | Astronomi, NEO, Mars Rover, EPIC*, Image Library | [api.nasa.gov](https://api.nasa.gov) — gratis | Opsional (`DEMO_KEY` bawaan bisa dipakai, tapi jatah request per jam kecil) |

Ada dua cara mengisi key:
1. **Lewat SD Card** — edit langsung file `gemini_key.txt` / `nasa_key.txt` yang otomatis dibuat.
2. **Lewat Web File Manager** — sambungkan WiFi, buka IP perangkat (ditampilkan di app **Files**) di browser, lalu masuk ke halaman "Atur API Key". Jika SD Card tidak terpasang, key otomatis disimpan ke NVS (flash internal) sebagai gantinya.

> *EPIC (`epic.gsfc.nasa.gov`) tidak memerlukan API key sama sekali.

---

## 👆 Gestur & Kontrol

| Gestur | Aksi |
|---|---|
| Usap ke atas di **Lock Screen** | Buka kunci |
| Usap ke **bawah** dari tepi atas status bar | Buka Control Center |
| Usap ke **atas** dari tepi bawah layar | Kembali ke Home |
| Goyangkan perangkat | Kembali ke Home (jika Shake-to-Home aktif) |
| Ketuk **Dynamic Island** | Perluas notifikasi / lompat ke aplikasi terkait |
| Tombol **"< Back"** di tiap app | Kembali ke layar sebelumnya |
| Drag pada Breakout / Orientasi 3D | Kontrol manual, atau biarkan sensor gerak yang mengambil alih |

---

## 🧠 Filosofi Pengembangan & Catatan Teknik

Proyek ini dikembangkan secara iteratif dengan pendekatan **"tulis akar masalahnya, bukan cuma tempelan"** — setiap kali ada bug, komentar kode menjelaskan *kenapa* bug itu terjadi sebelum menjelaskan perbaikannya. Beberapa contoh temuan menarik selama perjalanan proyek ini:

- 🔓 **Bug lock screen tak bisa diusap** ternyata bukan satu, tapi *dua* akar masalah berbeda di dua waktu berbeda: pertama karena GPIO ADC baterai bentrok dengan jalur SPI touchscreen, kedua karena kalibrasi touch dijalankan di rotasi layar yang berbeda dari rotasi yang dipakai saat runtime.
- 🐛 **Error kompilasi misterius** (`'diNotify' was not declared`) ternyata disebabkan oleh satu-satunya fungsi di seluruh file yang memakai *default parameter* bertipe `const String&` — `ctags` yang dipakai `arduino-cli` untuk auto-generate prototype gagal mem-parsing sintaks itu secara spesifik.
- 📶 **"Fix" yang ternyata bikin masalah baru:** upaya mengalokasikan buffer besar otomatis ke PSRAM secara global (`heap_caps_malloc_extmem_enable`) memang menghemat RAM internal untuk Gemini, tapi diam-diam merusak koneksi TCP/IP fitur lain karena struktur internal `lwIP` ikut "kesasar" ke PSRAM yang lebih lambat.
- 🎞️ **Animasi transisi antar layar** dirombak berkali-kali (langkah tetap → berbasis waktu → easing kubik → quintic → *revert* balik ke kubik) sampai ketemu titik manis antara kesan "premium" dan performa nyata di hardware.

Semangatnya: **jangan cuma menempelkan patch, tapi pahami dulu kenapa sesuatu rusak** — supaya perbaikan berikutnya tidak mengulang kesalahan yang sama.

---

## ⚠️ Keterbatasan yang Diketahui

- Estimasi baterai berbasis pembacaan tegangan sederhana (voltage divider), **bukan** IC *fuel-gauge* — anggap sebagai perkiraan kasar, bukan angka presisi.
- API key `DEMO_KEY` NASA punya jatah request per jam yang kecil; disarankan pakai key pribadi untuk penggunaan intensif.
- Endpoint `mars-photos/api/v1/rovers/.../photos?sol=` milik NASA kadang mengembalikan `404` di sisi server mereka sendiri (proyek open-source di baliknya sudah tidak dirawat) — firmware otomatis mencoba fallback ke `/latest_photos`.
- Video AVI harus dikonversi lebih dulu (lihat format encoding di komentar kode `ffmpeg -c:v mjpeg -vf "scale=320:240" -c:a pcm_s16le ...`) — tidak semua file AVI sembarang bisa langsung diputar.
- Ini proyek DIY untuk satu perangkat pribadi, bukan produk masal — beberapa asumsi hardware (pinout, ukuran layar) tertanam langsung di kode.

---

## 🗺️ Roadmap / Ide Lanjutan

- [ ] Dukungan multi-akun WiFi tersimpan (bukan cuma 1 slot)
- [ ] Widget kalender & pengingat
- [ ] Dukungan Bluetooth (headset/keyboard eksternal)
- [ ] Mode hemat daya / deep sleep terjadwal
- [ ] Lebih banyak tema & kustomisasi wallpaper Home Screen
- [ ] Enkripsi API key yang tersimpan di SD/NVS

*(Punya ide lain? Lihat bagian [Kontribusi](#-kontribusi) di bawah!)*

---

## 🙏 Kredit & Pustaka Pihak Ketiga

| Pustaka / Layanan | Kegunaan |
|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Driver grafis & sentuh performa tinggi |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | Decoder JPEG untuk MJPEG/AVI/gambar NASA |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | Decoder MP3 real-time via I2S |
| [Google Gemini API](https://ai.google.dev/) | Model AI untuk chat & transkripsi suara |
| [NASA Open APIs](https://api.nasa.gov/) | APOD, NeoWs, Mars Rover Photos, Image & Video Library |
| [NASA EPIC](https://epic.gsfc.nasa.gov/) | Citra Bumi harian dari satelit DSCOVR |
| [Open Trivia Database](https://opentdb.com/) | Bank soal kuis Trivia |
| [MyMemory Translation API](https://mymemory.translated.net/) | Terjemahan teks APOD EN→ID |

---

## 🤝 Kontribusi

Pull request, laporan bug, dan ide fitur baru sangat diterima! Beberapa panduan singkat:

1. Fork repo ini & buat branch baru untuk perubahanmu.
2. Ikuti gaya komentar yang sudah ada — jelaskan **akar masalah**, bukan cuma solusinya, terutama untuk perbaikan bug.
3. Uji perubahanmu dulu (idealnya di hardware asli) sebelum membuka PR.
4. Buka *issue* dulu untuk perubahan besar, supaya bisa didiskusikan arahnya.

---

## 📄 Lisensi

Proyek ini dirilis di bawah lisensi **MIT** — bebas dipakai, dimodifikasi, dan didistribusikan ulang, dengan atau tanpa atribusi (walau tentu saja sangat dihargai 😊). Lihat file `LICENSE` untuk teks lengkap.

---

## ⚖️ Disclaimer

Ini adalah proyek hobi/DIY, bukan produk komersial. Gunakan dengan risiko sendiri — pastikan pengkabelan baterai & regulator daya sudah benar sebelum menyalakan perangkat untuk mencegah kerusakan hardware. Data dari API pihak ketiga (Gemini, NASA, Open Trivia DB, MyMemory) tunduk pada kebijakan layanan masing-masing penyedia.

<div align="center">

---

**Dibuat dengan 🔧, banyak percobaan, dan lebih banyak lagi sesi debugging tengah malam.**

*Jika proyek ini membantu atau menginspirasi build-mu sendiri, jangan lupa kasih ⭐ di repo ini!*

</div>
