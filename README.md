<div align="center">

# 📱 Ren Phone

### Custom smartphone OS untuk ESP32-S3 — dari layar sentuh biasa jadi pengalaman "HP" beneran

![Platform](https://img.shields.io/badge/platform-ESP32--S3-black?style=for-the-badge&logo=espressif&logoColor=white)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D?style=for-the-badge&logo=arduino&logoColor=white)
![Display](https://img.shields.io/badge/display-ILI9341%20%2B%20XPT2046-blue?style=for-the-badge)
![Status](https://img.shields.io/badge/status-active%20development-brightgreen?style=for-the-badge)

**Ren Phone** menjalankan **SanzX OS** — sebuah "sistem operasi" custom yang dibangun
dari nol di atas [LovyanGFX](https://github.com/lovyan03/LovyanGFX): lock screen dengan
swipe-to-unlock, home screen dengan grid app & scroll momentum, Control Center gaya
quick-settings dengan tombol bulat, sistem tema, hingga game — semuanya dianimasikan
dengan easing & timing berbasis waktu supaya terasa mulus di hardware manapun.

</div>

---

## 📋 Daftar Isi

- [Kenapa proyek ini ada](#-kenapa-proyek-ini-ada)
- [Fitur Sistem](#-fitur-sistem)
- [Gestur & Navigasi](#-gestur--navigasi)
- [Aplikasi Bawaan](#-aplikasi-bawaan)
- [Tema](#-tema)
- [Di Balik Layar: Animation Engine](#-di-balik-layar-animation-engine)
- [Hardware](#-hardware)
- [Wiring](#-wiring)
- [Dependensi](#-dependensi-arduino-library)
- [Build & Flash](#-build--flash)
- [Konfigurasi WiFi & AI Chat](#-konfigurasi-wifi--ai-chat)
- [Web File Manager](#-web-file-manager)
- [Struktur Folder SD Card](#-struktur-folder-sd-card)
- [Monitoring Baterai](#-monitoring-baterai)
- [Catatan Teknis & Gotcha](#-catatan-teknis--gotcha)
- [Changelog](#-changelog)
- [Lisensi](#-lisensi)

---

## 💡 Kenapa proyek ini ada

Kebanyakan proyek TFT+ESP32 berhenti di "tampilkan teks di layar" atau "satu app
doang". Ren Phone dibangun sebagai eksperimen: seberapa jauh sebuah mikrokontroler
kelas hobi bisa dibuat terasa seperti smartphone sungguhan — lengkap dengan multi-app,
sistem navigasi bertumpuk (stack-based, bisa back), gesture, animasi bertenaga easing,
sampai OTA update dan AI chat — tanpa OS eksternal (murni di atas Arduino core).

## ✨ Fitur Sistem

| Fitur | Detail |
|---|---|
| 🔒 **Lock Screen** | Swipe ke atas untuk unlock, dengan animasi drag yang mengikuti jari |
| 🏠 **Home Screen** | Grid ikon aplikasi (16 app), scroll vertikal dengan momentum + efek elastis di ujung atas/bawah |
| 🎛️ **Control Center** | Swipe dari tepi atas layar — tombol **bulat** (bukan kotak) untuk WiFi, Airplane Mode, DND, Tema, Orientasi, Shake to Home, dan Kunci layar, plus slider Brightness dengan handle bulat |
| 🎨 **4 Tema Warna** | Dark, AMOLED, Light, Pastel — bisa diganti langsung dari Control Center |
| 🔄 **Auto-Rotate** | Landscape ⇄ Portrait otomatis via MPU6050, atau manual dari Control Center — kalibrasi touch selalu disinkronkan ke rotasi aktif |
| 📳 **Shake to Home** | Goyangkan perangkat untuk langsung kembali ke Home |
| 🌙 **Do Not Disturb** | Menonaktifkan toast notifikasi sementara |
| ✈️ **Airplane Mode** | Memutus WiFi sepenuhnya |
| 🔋 **Battery Monitoring** | Pembacaan tegangan baterai real-time via voltage divider, dengan peringatan baterai lemah/kritis |
| 📶 **Status Bar** | Jam (NTP), indikator koneksi, dll — selalu tampil di atas |
| ⬆️ **OTA Update** | Flash firmware baru langsung dari file `.bin` di SD card, tanpa perlu USB |
| 🌐 **Web File Manager** | Server web bawaan untuk kelola isi SD card dari browser laptop/HP lain |

## 👆 Gestur & Navigasi

| Gestur | Aksi |
|---|---|
| Swipe ke atas dari lock screen | Unlock |
| Swipe ke bawah dari tepi **atas** layar | Buka Control Center |
| Swipe handle Control Center ke bawah / tap area kosong di bawah panel | Tutup Control Center |
| Swipe ke atas dari tepi **bawah** layar (di luar Home) | Kembali ke Home |
| Tap ikon app di Home | Buka app (transisi *push*, geser dari kanan) |
| Tombol Back di dalam app | Kembali ke layar sebelumnya (transisi *back*, geser ke kanan) |
| Drag vertikal di Home | Scroll grid app, dengan momentum & efek elastis |
| Drag di slider Brightness | Atur kecerahan layar langsung |

## 📱 Aplikasi Bawaan

<table>
<tr><th>App</th><th>Deskripsi</th><th>Kontrol</th></tr>
<tr><td>🕐 <b>Jam</b></td><td>Jam digital, sinkron NTP saat WiFi tersambung</td><td>—</td></tr>
<tr><td>🧮 <b>Kalkulator</b></td><td>Kalkulator dasar dgn keypad on-screen</td><td>Tap tombol</td></tr>
<tr><td>🧭 <b>Orientasi 3D</b></td><td>Visualisasi real-time orientasi perangkat dari MPU6050</td><td>Miringkan perangkat</td></tr>
<tr><td>⚙️ <b>Setting</b></td><td>WiFi, ganti tema, orientasi, kalibrasi sensor gerak, dll</td><td>Tap menu</td></tr>
<tr><td>📝 <b>Notepad</b></td><td>Catatan teks, tersimpan ke SD card</td><td>Keyboard on-screen</td></tr>
<tr><td>🎨 <b>Canvas</b></td><td>Kanvas gambar bebas — goresan "distempel" (fillCircle diinterpolasi) supaya mulus tanpa celah</td><td>Gambar dgn jari</td></tr>
<tr><td>🤖 <b>AI Chat</b></td><td>Chat dgn Gemini API (butuh WiFi + API key)</td><td>Keyboard on-screen, scroll momentum di respons</td></tr>
<tr><td>📁 <b>Files</b></td><td>File explorer SD card, isi file teks bisa dibaca & di-scroll (wrap otomatis)</td><td>Tap file, drag utk scroll</td></tr>
<tr><td>🎬 <b>MJPEG</b></td><td>Pemutar video format MJPEG dari SD card</td><td>Tap kontrol</td></tr>
<tr><td>⬆️ <b>Update</b></td><td>Pilih file <code>.bin</code> di SD card lalu flash (OTA)</td><td>Tap file, konfirmasi</td></tr>
<tr><td>🔋 <b>Baterai</b></td><td>Detail voltase & persentase baterai</td><td>—</td></tr>
<tr><td>🐍 <b>Snake</b></td><td>Game ular klasik</td><td>Tap di sekitar kepala ular sesuai arah tujuan</td></tr>
<tr><td>🐤 <b>Flappy Block</b></td><td>Hindari pipa, terbang terus-menerus</td><td>Tap layar utk "terbang"</td></tr>
<tr><td>🔢 <b>2048</b></td><td>Puzzle angka klasik</td><td>Swipe 4 arah</td></tr>
<tr><td>❌⭕ <b>TicTacToe</b></td><td>Lawan AI sederhana (menang → blok → tengah → acak)</td><td>Tap kotak</td></tr>
<tr><td>🧱 <b>Breakout</b></td><td>Pantulkan bola pecahkan balok</td><td>Miringkan perangkat (MPU6050) — fallback drag jari kalau sensor tak terdeteksi</td></tr>
</table>

## 🎨 Tema

| Tema | Aksen | Karakter |
|---|---|---|
| **Dark** | Oranye + Cyan | Default, gelap netral |
| **AMOLED** | Oranye + Cyan | Hitam pekat, hemat daya di panel OLED |
| **Light** | Biru | Terang, kontras tinggi |
| **Pastel** | Ungu muda | Warna lembut, tone-on-tone |

Ganti tema kapan saja lewat tombol **Tema** di Control Center (langsung apply ke
semua layar) atau dari menu Setting.

## 🚀 Di Balik Layar: Animation Engine

Yang bikin Ren Phone terasa "smooth" bukan cuma delay pendek — tapi sistem animasi
yang sengaja dirancang konsisten:

- **Easing terpusat** — satu fungsi `navEase()` (ease-in-out cubic) dipakai ulang di
  *semua* animasi: transisi antar layar, buka/tutup Control Center, boot sequence,
  bahkan animasi "Game Booster". Semua animasi jadi punya "rasa" gerak yang sama.
- **Berbasis waktu, bukan jumlah frame** — animasi dihitung dari `millis()` terhadap
  durasi target (mis. transisi layar ~170ms), bukan loop N-frame dengan delay tetap.
  Efeknya: durasi animasi **selalu konsisten** walau render sedang berat (WiFi/AI
  jalan bersamaan) — di hardware lambat animasinya sedikit kurang halus, tapi tidak
  pernah kerasa "molor".
- **Momentum scroll** — scroll Home, AI Chat, dan File Explorer memakai *low-pass
  velocity* + decay, bukan mengikuti jari 1:1 lalu berhenti mendadak. Ada efek
  elastis ringan di ujung atas/bawah list.
- **Goresan Canvas mulus** — bukan `drawLine()` bergeser per piksel (bertangga di
  brush besar/diagonal), tapi "distempel" pakai `fillCircle` yang diinterpolasi
  sepanjang jalur gerakan jari.
- **Boot sequence sinematik** — logo membesar dgn easing, halo/glow berlapis,
  gradient background halus, judul fade-in, loader titik berdenyut (wave pulse) —
  palet warnanya sengaja dijaga monokrom gelap (bukan warna-warni) supaya kesannya
  premium & senada tema dark, bukan "ramai".

## 🔧 Hardware

| Komponen | Spesifikasi |
|---|---|
| **MCU** | ESP32-S3 — Flash 16MB (N16), PSRAM OPI 8MB |
| **Layar** | TFT ILI9341, 240×320, antarmuka SPI |
| **Touchscreen** | XPT2046 resistif, SPI **terpisah** dari layar (bus independen) |
| **Sensor gerak** | MPU6050 (accelerometer + gyroscope), I2C |
| **Penyimpanan** | microSD via SDIO (`SD_MMC`, mode 1-bit) |
| **Monitoring baterai** | ADC + voltage divider (2× 10kΩ) |

## 🔌 Wiring

### Layar TFT — `SPI2_HOST`

| Pin Layar | ESP32-S3 |
|---|---|
| VCC | **5V** (wajib — J1 terbuka) |
| GND | GND |
| CS | GPIO 10 |
| RESET | GPIO 14 |
| DC | GPIO 2 |
| SDI (MOSI) | GPIO 11 |
| SCK | GPIO 12 |
| SDO (MISO) | GPIO 13 |
| LED (backlight, PWM) | GPIO 21 |

### Touchscreen XPT2046 — `SPI3_HOST` (bus independen, `bus_shared=false`)

| Pin Touch | ESP32-S3 |
|---|---|
| T_CLK | GPIO 6 |
| T_DIN (MOSI) | GPIO 5 |
| T_DO (MISO) | GPIO 4 |
| T_CS | GPIO 9 |
| T_IRQ | GPIO 1 *(opsional, tidak dipakai — polling)* |

### MPU6050 — I2C

| Pin | ESP32-S3 |
|---|---|
| SDA | GPIO 15 |
| SCL | GPIO 7 |
| VCC | 3.3V |
| GND | GND |
| Alamat I2C | `0x68` |

### microSD — SDIO (`SD_MMC`)

| Pin | ESP32-S3 |
|---|---|
| CLK | GPIO 39 |
| CMD | GPIO 38 |
| D0 | GPIO 40 |

### Sensor Baterai — ADC

| Pin | ESP32-S3 |
|---|---|
| Vbat (lewat divider 10k+10k) | GPIO 8 |

> ⚠️ Wiring di atas mengikuti konfigurasi `LGFX CONFIG` di kode. Kalau board kamu
> beda, sesuaikan langsung di bagian itu (kelas `LGFX` di awal file `.ino`).

## 📦 Dependensi (Arduino Library)

| Library | Fungsi |
|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Driver layar ILI9341 + touch XPT2046, sprite/double-buffering |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | Dekoder JPEG untuk pemutar MJPEG |
| `MjpegClass` | Wrapper pemutaran MJPEG di atas JPEGDEC |
| `Preferences` | Penyimpanan setting persisten (NVS) — tema, orientasi, dll |
| `WiFi`, `WiFiClientSecure`, `HTTPClient` | Koneksi WiFi & request HTTPS (Gemini API) |
| `WebServer` | Web file manager bawaan |
| `Update` | OTA flashing firmware dari `.bin` |
| `Wire` | I2C untuk MPU6050 |
| `FS`, `SD_MMC` | Akses microSD |

## 🛠️ Build & Flash

Firmware ini dikompilasi dengan **arduino-cli** untuk board:

```
esp32:esp32:esp32s3
```

**Board settings yang dipakai:**
- Flash Size: **16MB (N16)**
- Partition Scheme: default N16 (App + SPIFFS/FAT sesuai kebutuhan)
- PSRAM: **OPI PSRAM**, 8MB

### Opsi 1 — Build manual dengan arduino-cli

```bash
# Install core ESP32 (kalau belum)
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Install library yang dibutuhkan
arduino-cli lib install "LovyanGFX"
# JPEGDEC + MjpegClass biasanya perlu diinstal manual/dari .zip

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32s3 \
  --board-options FlashSize=16M,PSRAM=opi \
  .

# Upload lewat USB
arduino-cli upload -p <PORT> --fqbn esp32:esp32:esp32s3 .
```

### Opsi 2 — GitHub Actions (otomatis)

Repo ini di-build otomatis lewat workflow `build.yml` setiap ada push — memakai
`arduino-cli` dengan konfigurasi board yang sama seperti di atas. Hasil compile
(`.bin`) tersedia sebagai **artifact** di tab Actions, siap:
1. Diflash langsung lewat USB (`esptool.py` / arduino-cli), **atau**
2. Ditaruh di folder `/update` pada SD card, lalu diflash **OTA** langsung dari
   menu **Update** di perangkat — tanpa kabel sama sekali.

## 🔑 Konfigurasi WiFi & AI Chat

**WiFi** — masukkan SSID & password lewat menu **Setting** di perangkat (tersimpan
persisten via `Preferences`/NVS, otomatis konek ulang tiap boot).

**AI Chat (Gemini API)** — saat pertama kali boot, firmware otomatis membuat file
`gemini_key.txt` di root SD card kalau belum ada, isinya:

```
# GEMINI API KEY CONFIGURATION (ESP32-S3)
YOUR_GEMINI_API_KEY_HERE
```

Ganti baris terakhir dengan API key Gemini kamu — bisa lewat:
- Edit langsung file di SD card (cabut, edit di komputer), **atau**
- Menu **Setting** di perangkat (keyboard on-screen), **atau**
- Web File Manager (lihat bawah) setelah WiFi tersambung

## 🌐 Web File Manager

Setelah WiFi tersambung, buka `http://<IP-perangkat>/` dari browser laptop/HP lain
untuk kelola isi SD card tanpa cabut kartu:

| Endpoint | Fungsi |
|---|---|
| `GET /` | Daftar file & folder |
| `GET /edit` | Buka editor teks utk sebuah file |
| `POST /save` | Simpan hasil edit |
| `POST /upload` | Upload file baru ke SD card |
| `GET /delete` | Hapus file |
| `GET /download` | Unduh file |

## 📁 Struktur Folder SD Card

```
/
├── gemini_key.txt   → API key Gemini (dibuat otomatis saat boot pertama)
├── mjpeg/           → taruh file .mjpeg di sini untuk app MJPEG
└── update/          → taruh file .bin di sini untuk OTA update lewat menu Update
```

## 🔋 Monitoring Baterai

Voltase baterai dibaca lewat ADC (GPIO 8) melalui voltage divider 2× 10kΩ (rasio
1:2), lalu di-*smoothing* (`battVoltage = battVoltage*0.7 + vBat*0.3`) supaya
angkanya tidak melompat-lompat. Persentase dihitung dari rentang tegangan Li-ion
3.0V (kosong) – 4.2V (penuh), dengan notifikasi toast otomatis saat baterai
**≤15% (lemah)** dan **≤5% (kritis)**.

## 🧩 Catatan Teknis & Gotcha

Beberapa hal yang perlu diketahui kalau mau ikut mengembangkan:

- **Kalibrasi touch terikat rotasi** — `setup()` membaca orientasi tersimpan lalu
  langsung `display.setRotation()` **sebelum** boot sequence & kalibrasi touch
  dijalankan. Ini disengaja: XPT2046 di LovyanGFX memetakan sumbu X/Y touch
  berdasarkan rotasi yang aktif *saat kalibrasi* — kalau meleset dari rotasi
  runtime, gesture bisa terbaca terbalik/miring.
- **Urutan definisi struct/enum** — beberapa `struct`/`enum` (`AiLine`, `Vec3f`,
  `NavAnim`) sengaja diletakkan di paling atas file, bukan dekat fungsi
  pemakainya. Ini workaround untuk cara `arduino-cli` men-generate function
  prototype otomatis di puncak file — kalau tipe tsb didefinisikan belakangan,
  compile gagal dgn error "was not declared in this scope".
- **Dua bus SPI independen** — layar (`SPI2_HOST`) dan touch (`SPI3_HOST`)
  sengaja dipisah (`bus_shared=false`) supaya operasi baca touch tidak bentrok
  dengan render layar.

## 📝 Changelog

Riwayat perubahan versi (mulai v9) ditulis lengkap di komentar header file `.ino`
utama — mencakup fix kalibrasi touch, animasi Control Center & boot sequence yang
dibuat berbasis waktu, momentum scroll, tombol Control Center jadi bulat, dan
lainnya. Baca langsung di bagian atas source code untuk detail tiap versi.

## 📄 Lisensi

Belum ditentukan — tambahkan file `LICENSE` sesuai kebutuhan kamu (mis. MIT,
GPL-3.0) sebelum membagikan repo ini secara publik.

---

<div align="center">

Dibuat dengan ❤️ di atas ESP32-S3 — *Simplicity, Redefined.*

</div>