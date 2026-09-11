# ren_phone (NyxOS)

Firmware DIY untuk membuat "HP" custom berbasis **ESP32-S3**, dengan layar sentuh, banyak aplikasi bawaan, integrasi AI (Google Gemini), integrasi data NASA, pemutar media, dan berbagai sensor. Seluruh sistem (bootscreen, launcher, status bar, lock screen, Control Center, dsb.) ditulis dari nol di atas [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — codename sistemnya sendiri adalah **NyxOS**, dengan animasi boot bertema "Accretion" (partikel yang membentuk tulisan lalu berubah jadi logo).

> File utama proyek ini adalah satu sketch `.ino` besar yang sudah berkembang lewat puluhan iterasi (v9 → v60+), dengan riwayat perubahan & alasan tiap fix didokumentasikan langsung sebagai komentar di kode.

---

## Daftar Isi

- [Fitur Utama](#fitur-utama)
- [Daftar Aplikasi](#daftar-aplikasi)
- [Kebutuhan Hardware](#kebutuhan-hardware)
- [Peta Pin (Wiring)](#peta-pin-wiring)
- [Kebutuhan Software](#kebutuhan-software)
- [Cara Build & Flash](#cara-build--flash)
- [Konfigurasi API Key](#konfigurasi-api-key)
- [Struktur File di SD Card](#struktur-file-di-sd-card)
- [Web File Manager](#web-file-manager)
- [Kontrol & Gesture](#kontrol--gesture)
- [Format Media Kustom](#format-media-kustom)
- [Catatan Teknis & Keterbatasan](#catatan-teknis--keterbatasan)
- [Lisensi](#lisensi)

---

## Fitur Utama

- **Layar sentuh penuh** dengan lock screen (swipe-to-unlock), Control Center (swipe-down dari status bar), status bar dinamis, dan **Dynamic Island** (notifikasi mengambang ala HP modern) untuk status AI Chat, Mode Game, dan skor Trivia.
- **UI glassmorphism** (efek kaca buram) di Home, Lock Screen, dan Control Center, dengan 4 tema: `Dark`, `AMOLED`, `Light`, `Pastel`.
- **Rotasi otomatis** (portrait/landscape) berbasis sensor MPU6050, dengan kalibrasi touch yang selalu sinkron dengan rotasi aktif.
- **Shake-to-Home**, mode pesawat, DND, kontrol kecerahan, kunci layar — semua dari Control Center.
- **Wallpaper kustom** (upload JPG lewat web file manager), dengan fallback ke penyimpanan flash internal (FFat) kalau SD Card tidak terpasang.
- **Web File Manager** bawaan (server HTTP di port 80): upload/download/edit/hapus file di SD Card, atur wallpaper, dan atur API key — semua dari browser tanpa perlu keyboard fisik di HP.
- **Update firmware OTA** — bisa dari file `.bin` di SD Card atau diunduh langsung lewat URL WiFi.
- **AI Chat** (Google Gemini API) dengan memori percakapan persisten di SD Card, plus **dikte suara** (rekam → transkrip otomatis → isi ke kolom chat).
- **AI Live** — mode ngobrol dengan suara penuh: rekam suara → Gemini memahami & menjawab → jawaban diubah jadi suara (Gemini TTS) dan diputar lewat speaker.
- **Integrasi data NASA**: APOD (foto astronomi harian + terjemahan otomatis ke Indonesia), NEO Asteroid tracker, Mars Rover Photos, citra Bumi EPIC, dan pencarian NASA Image Library — semua dengan caching lokal ke SD Card atau PSRAM.
- **Trivia Quiz** online (Open Trivia Database) dengan 12 kategori × 4 tingkat kesulitan.
- **6 game bawaan**: Snake, Flappy Block, 2048, Tic-Tac-Toe, Breakout, dan Labirin (klon Pac-Man dengan progres level & rekor tersimpan permanen).
- **Pemutar media**: MJPEG video player, MP3 Player (pemutaran latar belakang), dan AVI Player (video + audio, format kustom ringan).
- **Kontrol hardware tambahan**: LED Neopixel (RGB, mode solid & rainbow), motor getar dengan pola berbeda per jenis notifikasi, meteran level mic real-time, dan HWmonitor (estimasi beban CPU per-core, RAM/PSRAM, uji speaker).
- **Monitor baterai** dengan estimasi persentase & sisa kapasitas dari voltage divider.

---

## Daftar Aplikasi

| Aplikasi | Deskripsi singkat |
|---|---|
| Jam | Jam digital real-time (NTP) |
| Kalkulator | Kalkulator dasar |
| Orientasi 3D | Visualisasi 3D real-time orientasi HP dari data MPU6050 |
| Setting | WiFi, tema, kecerahan, auto-rotate, kalibrasi ulang sensor/touch |
| Notepad | Catatan teks tersimpan ke SD Card, dengan dialog konfirmasi simpan |
| Canvas | Kanvas gambar jari dengan palet warna & ukuran brush |
| AI Chat | Chat dengan Gemini AI, mendukung dikte suara & memori percakapan |
| Files | File explorer SD Card (lihat isi file .txt, hapus, dsb.) |
| MJPEG | Pemutar video MJPEG dari `/mjpeg` di SD Card |
| Update | Update firmware via SD Card atau URL WiFi (OTA) |
| Baterai | Info tegangan, persentase, & estimasi sisa daya |
| Snake | Snake klasik dengan wrap-around di tepi layar |
| Flappy | Flappy Bird ala block |
| 2048 | 2048 dengan swipe 4 arah |
| TicTacToe | Vs CPU atau 2 pemain, papan 3x3/4x4/5x5 |
| Breakout | Kontrol drag jari atau kemiringan HP (kalau ada MPU6050) |
| Trivia | Kuis trivia online (Open Trivia DB), 12 kategori |
| Astronomi | NASA APOD — foto astronomi harian + terjemahan ID |
| NEO Asteroid | Data asteroid dekat Bumi (NASA NeoWs) |
| Mars Rover | Foto dari rover Curiosity/Perseverance |
| Bumi EPIC | Citra Bumi dari kamera EPIC (DSCOVR satellite) |
| Galeri NASA | Pencarian NASA Image & Video Library |
| Neopixel | Kontrol LED RGB (solid color / rainbow) |
| MP3 Player | Pemutar MP3 dari `/mp3`, jalan di background |
| AVI Player | Pemutar video+audio format AVI kustom dari `/avi` |
| Mic Level | Meteran level mic real-time (uji wiring INMP441) |
| HWmonitor | Estimasi beban CPU per-core, RAM/PSRAM, uji speaker |
| AI Live | Ngobrol suara penuh dengan Gemini (voice-in, voice-out) |
| Labirin | Klon Pac-Man dengan level & rekor skor persisten |

---

## Kebutuhan Hardware

- **Board**: ESP32-S3 (dengan PSRAM — wajib untuk fitur gambar/audio)
- **Layar**: TFT ILI9341 (SPI)
- **Touch**: XPT2046 resistif (SPI terpisah)
- **Storage**: SD Card via SDIO (SD_MMC 1-bit)
- **Sensor gerak**: MPU6050 (accelerometer + gyroscope, I2C)
- **Mic**: INMP441 (I2S)
- **Speaker/Amp**: MAX98357A (I2S)
- **Motor getar** (vibration motor kecil, on/off digital)
- **LED Neopixel/WS2812** (opsional, addressable RGB)
- Voltage divider 10k+10k untuk monitor baterai Li-ion 1 sel

## Peta Pin (Wiring)

### Layar (ILI9341, SPI)
| Fungsi | GPIO |
|---|---|
| SCLK | 12 |
| MOSI | 11 |
| MISO | 13 |
| DC | 2 |
| CS | 10 |
| RST | 14 |
| Backlight (PWM) | 21 |

### Touch (XPT2046, SPI terpisah / SPI3)
| Fungsi | GPIO |
|---|---|
| SCLK | 6 |
| MOSI | 5 |
| MISO | 4 |
| CS | 9 |

### SD Card (SDIO 1-bit)
| Fungsi | GPIO |
|---|---|
| CLK | 39 |
| CMD | 38 |
| D0 | 40 |

### Audio Output (MAX98357A — dipakai bergantian oleh MP3/AVI/AI Live, tidak bisa jalan bersamaan)
| Fungsi | GPIO |
|---|---|
| BCLK | 42 |
| LRC | 41 |
| DOUT | 17 |

### Mic (INMP441)
| Fungsi | GPIO |
|---|---|
| SCK | 47 |
| WS | 46 |
| SD | 45 |

> L/R pin INMP441 ditarik ke GND (channel kiri aktif).

### Sensor & Lainnya
| Komponen | GPIO |
|---|---|
| MPU6050 SDA | 15 |
| MPU6050 SCL | 7 (alamat I2C `0x68`) |
| Motor getar | 18 |
| LED Neopixel | 48 |
| Baterai (ADC, via voltage divider 1:2) | 8 |

---

## Kebutuhan Software

- **Arduino-ESP32 core 3.x** (dibutuhkan untuk `ESP_I2S.h`, `rgbLedWrite()`, dan API terbaru lainnya)
- Library yang **wajib diinstall manual**:
  - **LovyanGFX**
  - **JPEGDEC**
  - **ESP32-audioI2S** oleh schreibfaul1 (`Audio.h`) — untuk decode MP3
  - `MjpegClass` (header pendukung pemutar MJPEG)
- Library bawaan Arduino-ESP32 core (tidak perlu install terpisah):
  - `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`
  - `FS`, `SD_MMC`, `FFat`
  - `Update` (OTA)
  - `Preferences` (penyimpanan NVS)
  - `Wire` (I2C)
  - `ESP_I2S.h` (I2S untuk mic & speaker mentah)
  - `mbedtls/base64.h` (encode/decode base64, untuk audio & API)
  - `esp_bt.h`, `esp_freertos_hooks.h` (pelepasan RAM Bluetooth & idle hook CPU monitor)

## Cara Build & Flash

1. Install Arduino IDE (atau `arduino-cli`) dengan board package **esp32 by Espressif** versi 3.x.
2. Pilih board ESP32-S3 dengan **PSRAM diaktifkan** dan skema partisi yang menyisakan ruang untuk **OTA** dan **FFat**.
3. Install library-library di atas lewat Library Manager / manual.
4. Sesuaikan pin wiring sesuai tabel di atas kalau board Anda berbeda.
5. Compile & upload seperti sketch Arduino biasa.
6. Saat boot pertama kali, perangkat akan menjalankan kalibrasi touch (ikuti instruksi di layar — sentuh titik di tiap sudut).

---

## Konfigurasi API Key

Beberapa fitur butuh API key, disimpan sebagai file teks di SD Card (dibuat otomatis kalau belum ada) — atau lewat **Web File Manager** kalau tidak punya SD Card:

| File | Untuk | Catatan |
|---|---|---|
| `/gemini_key.txt` | AI Chat, AI Live, dikte suara | Dapatkan gratis di [Google AI Studio](https://aistudio.google.com/app/apikey). Fallback ke NVS internal kalau SD tidak ada. |
| `/nasa_key.txt` | APOD, NEO, Mars Rover, EPIC, Galeri NASA | Opsional — dibiarkan `DEMO_KEY` tetap jalan, tapi limit request per jam jauh lebih kecil. Dapatkan gratis di [api.nasa.gov](https://api.nasa.gov). |

WiFi diatur lewat aplikasi **Setting** (bisa scan jaringan sekitar langsung dari HP).

---

## Struktur File di SD Card

```
/
├── notepad.txt              # isi Notepad
├── gemini_key.txt           # API key Gemini
├── nasa_key.txt             # API key NASA
├── ai_memory.txt            # riwayat percakapan AI Chat/AI Live (auto-terpangkas)
├── wallpaper.jpg            # wallpaper Home & Lock Screen (opsional)
├── canvas_land.bin          # kanvas gambar (landscape)
├── canvas_port.bin          # kanvas gambar (portrait)
├── mjpeg/                   # taruh file .mjpeg di sini
├── update/                  # taruh file firmware .bin di sini (OTA lokal)
├── mp3/                     # taruh file .mp3 di sini
├── avi/                     # taruh file .avi (format kustom, lihat di bawah)
├── apod_cache/              # cache foto+terjemahan APOD
├── neo_cache/                
├── mars_cache/
├── epic_cache/
└── imglib_cache/
```

> Kalau SD Card tidak terpasang: wallpaper otomatis pakai penyimpanan flash internal (FFat), API key jatuh ke NVS, dan gambar dari NASA API diunduh langsung ke PSRAM tanpa disimpan permanen.

---

## Web File Manager

Begitu WiFi tersambung, buka `http://<IP-perangkat>/` dari browser HP/laptop di jaringan yang sama untuk:

- Upload file apa saja ke SD Card
- Edit langsung file teks (`.txt`, `.json`, `.csv`, `.log`, `.ini`, `.md`) lewat browser
- Download / hapus file
- Atur & lihat status API Key Gemini
- Upload wallpaper (`wallpaper.jpg`) — otomatis aktif langsung tanpa restart

---

## Kontrol & Gesture

| Gesture | Aksi |
|---|---|
| Swipe ke atas di lock screen | Buka kunci |
| Swipe ke bawah dari status bar | Buka Control Center |
| Swipe ke atas dari tepi bawah layar | Kembali ke Home |
| Goyangkan HP | Kembali ke Home (bisa dimatikan di Control Center) |
| Miringkan HP | Auto-rotate layar (bisa dimatikan), kontrol game Breakout/Labirin |
| Ketuk Dynamic Island | Buka aplikasi terkait / perbesar detail notifikasi |

---

## Format Media Kustom

### AVI Player
Parser AVI custom yang ringan — hanya mendukung video **MJPEG** + audio **PCM 16-bit**. Konversi video biasa jadi format ini dengan ffmpeg:

```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 5 -vf "fps=15,scale=320:240" \
       -c:a pcm_s16le -ar 22050 -ac 1 output.avi
```

> Resolusi **wajib** 320×240 (landscape) — player tidak melakukan scaling otomatis.

### MJPEG Player
File `.mjpeg` biasa (motion JPEG mentah), ditaruh di folder `/mjpeg`.

---

## Catatan Teknis & Keterbatasan

- **RAM internal ESP32-S3 terbatas (~162KB)** — koneksi TLS/HTTPS butuh blok kontigu ~20-30KB. Kadang muncul error `HTTP -1` yang sebenarnya bukan masalah jaringan, melainkan RAM internal sedang terfragmentasi. Firmware sudah menyertakan mekanisme retry/wait otomatis sebelum menyerah, dan aplikasi **HWmonitor** menampilkan angka *"Blok Terbesar"* real-time untuk diagnosis.
- Fitur-fitur berat (dekode gambar/JPEG, buffer audio, rekaman suara) sangat bergantung pada **PSRAM** — tanpa PSRAM, sebagian fitur (unggah wallpaper, foto NASA, dikte suara, AI Live) tidak akan bekerja dengan baik.
- MP3 Player dan AVI Player berbagi **satu jalur I2S fisik** yang sama dengan AI Live — tidak bisa berjalan bersamaan; salah satu otomatis dihentikan saat yang lain dijalankan.
- Estimasi baterai dihitung murni dari pembacaan tegangan (voltage divider), bukan fuel-gauge IC — nilai persentase & sisa mAh adalah perkiraan kasar.
- API `DEMO_KEY` NASA punya limit rate yang kecil; pakai key pribadi untuk pemakaian rutin.
- Endpoint Mars Rover Photos milik NASA kadang mengembalikan `404` untuk kombinasi sol/rover tertentu — ini keterbatasan dari sisi server NASA sendiri (proyek open-source di baliknya sudah tidak dirawat), bukan bug aplikasi; firmware otomatis fallback ke endpoint `/latest_photos`.

---

## Lisensi

Belum ditentukan — tambahkan berkas `LICENSE` sesuai kebutuhan Anda sebelum membagikan/menerbitkan ulang proyek ini.
