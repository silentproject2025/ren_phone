# Ren Phone — SanzX OS

Firmware "sistem operasi" DIY untuk HP buatan sendiri berbasis **ESP32-S3**, layar **ILI9341** (320×240, resistif touch **XPT2046**), dan **SD Card via SDIO (SD_MMC)**. Ditulis sebagai satu file `.ino` dengan UI custom bergaya smartphone modern: home screen dengan grid app, status bar, lock screen dengan swipe-to-unlock, Control Center gaya iOS, Dynamic Island, transisi antar-app yang di-animasi, dan belasan aplikasi bawaan — mulai dari kalkulator sampai chatbot AI dan pengamat langit.

Nama tampil di boot sequence: **"Ren Phone"** (brand perangkat) → **"SanzX OS"** (brand sistem operasi), dengan tagline *"Simplicity, Redefined."*

---

## Daftar Isi

- [Ringkasan Fitur](#ringkasan-fitur)
- [Hardware & Wiring](#hardware--wiring)
- [Struktur Sistem / UI](#struktur-sistem--ui)
- [Daftar Aplikasi](#daftar-aplikasi)
- [Integrasi Layanan Online](#integrasi-layanan-online)
- [Struktur File di SD Card](#struktur-file-di-sd-card)
- [Cara Setup & Flashing](#cara-setup--flashing)
- [Library yang Dibutuhkan](#library-yang-dibutuhkan)
- [Preferensi Tersimpan (NVS)](#preferensi-tersimpan-nvs)
- [Filosofi & Catatan Desain](#filosofi--catatan-desain)
- [Known Limitations](#known-limitations)

---

## Ringkasan Fitur

- **UI ala smartphone modern**: home screen scrollable dengan grid ikon + dock 4 app favorit, status bar (jam, baterai, WiFi, SD, DND), lock screen dengan jam besar + swipe-to-unlock.
- **Control Center** (gaya iOS, diusap dari atas layar): toggle WiFi, Airplane Mode, DND, ganti Tema, ganti Orientasi, Shake-to-Home, kunci layar, slider brightness.
- **Dynamic Island**: pil hitam mengambang di tengah atas layar yang bermorfing untuk menampilkan notifikasi singkat (mis. status Game Mode, hasil aksi Control Center).
- **Transisi antar-app dianimasi** (slide + easing cubic) untuk Push/Back/Home, dengan pacing yang konsisten di kondisi hardware apa pun.
- **Auto-rotate & Shake-to-Home** via sensor **MPU6050** (accelerometer + gyroscope).
- **4 tema warna**: Dark, AMOLED, Light, Pastel.
- **Web File Manager** bawaan (akses dari browser PC/HP lain di jaringan WiFi yang sama) untuk upload/download/edit/hapus file di SD Card.
- **OTA Update firmware** — dari file `.bin` di SD Card maupun unduh langsung dari URL lewat WiFi.
- **12 aplikasi built-in** (lihat [Daftar Aplikasi](#daftar-aplikasi)), termasuk chatbot AI (Gemini), pemutar video MJPEG, editor gambar (Canvas), kuis trivia online, dan pengamat Astronomy Picture of the Day NASA dengan auto-translate.
- **5 game**: Snake (wrap-around), Flappy Bird-style, 2048, Tic-Tac-Toe (vs CPU / PvP, papan hingga 5×5), Breakout (kontrol drag jari / kemiringan HP) — semua dengan "Game Mode" (boost CPU ke 240MHz + jeda polling sensor background).

---

## Hardware & Wiring

| Komponen | Pin ESP32-S3 | Keterangan |
|---|---|---|
| **Layar ILI9341 (SPI)** | | Bus SPI2_HOST, 40MHz write / 16MHz read, DMA aktif |
| &nbsp;&nbsp;SCLK | GPIO 12 | |
| &nbsp;&nbsp;MOSI | GPIO 11 | |
| &nbsp;&nbsp;MISO | GPIO 13 | |
| &nbsp;&nbsp;DC | GPIO 2 | |
| &nbsp;&nbsp;CS | GPIO 10 | |
| &nbsp;&nbsp;RST | GPIO 14 | |
| &nbsp;&nbsp;Backlight (PWM) | GPIO 21 | |
| **Touch XPT2046 (SPI terpisah)** | | Bus SPI3_HOST, 2MHz |
| &nbsp;&nbsp;SCLK | GPIO 6 | |
| &nbsp;&nbsp;MOSI | GPIO 5 | |
| &nbsp;&nbsp;MISO | GPIO 4 | ⚠️ Jangan dipakai pin lain (lihat catatan Baterai di bawah) |
| &nbsp;&nbsp;CS | GPIO 9 | |
| **SD Card (SDIO / SD_MMC)** | | |
| &nbsp;&nbsp;CLK | GPIO 39 | |
| &nbsp;&nbsp;CMD | GPIO 38 | |
| &nbsp;&nbsp;D0 | GPIO 40 | |
| **MPU6050 (I2C, accel+gyro)** | | Alamat I2C `0x68`, clock 400kHz |
| &nbsp;&nbsp;SDA | GPIO 15 | |
| &nbsp;&nbsp;SCL | GPIO 7 | |
| **Baterai (voltage divider 10k+10k)** | | |
| &nbsp;&nbsp;ADC output | GPIO 8 | ⚠️ **Bukan GPIO4** — GPIO4 bentrok dgn pin MISO touch XPT2046 (`analogRead()` mengubah mode pin itu jadi ADC dan merusak pembacaan sentuh). Pastikan divider disambung ke GPIO8. |

Panel: `memory_width=240, memory_height=320` (native portrait 240×320, di-rotate software untuk landscape 320×240). Kalibrasi touch dilakukan sekali di awal (tersimpan di NVS) dan **selalu dijalankan di rotasi yang akan dipakai runtime** — penting karena kalibrasi XPT2046 terikat ke rotasi saat kalibrasi dilakukan.

Baterai: Li-ion 1 sel, rentang tegangan **3.0V–4.2V**, kapasitas yang dipakai di kalkulasi estimasi **2300 mAh** (ubah `BATTERY_CAPACITY_MAH` kalau baterai fisikmu beda). Estimasi persen/sisa daya dihitung murni dari pembacaan tegangan (bukan fuel-gauge IC), jadi perkiraan kasar — bukan angka presisi.

---

## Struktur Sistem / UI

### Navigasi
- **Home**: grid app (2 kolom di portrait, 3 kolom di landscape) + dock 4 app favorit (Jam, Notepad, AI Chat, Files) di bagian bawah, bisa di-scroll dengan momentum.
- **Swipe dari tepi atas layar** (di dalam app manapun) → buka Control Center.
- **Swipe ke atas dari tepi bawah** → pulang ke Home (kecuali sudah di Home).
- **Tombol "< Back"** di pojok kiri-bawah tiap app → kembali ke layar sebelumnya (navigation stack, maks. 8 level).
- Semua transisi (Push/Back/Home) dianimasi slide dengan easing, dibungkus satu transaksi SPI (`startWrite`/`endWrite`) supaya gerakannya tidak tersendat.

### Lock Screen
Jam + tanggal besar, diusap ke atas (≥50px) untuk membuka. Kalibrasi touch & auto-rotate dinonaktifkan otomatis saat masih terkunci supaya swipe tidak salah baca arah.

### Status Bar
Jam (NTP-synced), indikator DND, SD, WiFi (atau ikon pesawat/tanda-X), dan baterai (ikon + persen, warna berubah sesuai level: hijau → oranye ≤35% → merah ≤15%).

### Control Center
7 kartu (WiFi, Airplane, DND, Tema, Orientasi, Shake-to-Home, Kunci Layar) + slider Brightness. Dibuka dengan swipe dari tepi atas, animasi buka/tutup pakai easing yang sama dengan transisi nav (`navEase`) supaya "rasa" gerakannya konsisten di seluruh sistem.

### Keyboard Virtual
QWERTY custom (lower/UPPER/angka-simbol), dipakai di semua field teks (Notepad, WiFi SSID/Password, AI Chat, dll).

### Tema
4 preset warna (Dark/AMOLED/Light/Pastel), berlaku ke seluruh UI secara real-time lewat struct `Theme` global (`T()`).

---

## Daftar Aplikasi

| Ikon | Nama | Ringkasan |
|---|---|---|
| J | **Jam** | Jam digital real-time + tanggal, status sinkronisasi NTP |
| + | **Kalkulator** | Operasi dasar +−×÷, persen, +/− |
| 3 | **Orientasi 3D** | Visualisasi wireframe 3D bodi HP yang berputar real-time mengikuti kemiringan (roll/pitch) dari MPU6050, plus data mentah accel/gyro/suhu |
| @ | **Setting** | Kecerahan, ganti tema, scan & sambung WiFi, auto-rotate toggle, kalibrasi ulang touch |
| N | **Notepad** | Catatan teks tersimpan di SD, dengan dialog konfirmasi "Simpan/Buang" kalau keluar dengan perubahan belum tersimpan |
| C | **Canvas** | Kanvas gambar jari dengan palet 9 warna & ukuran kuas, otomatis tersimpan ke SD per-orientasi |
| A | **AI Chat** | Chatbot berbasis **Google Gemini API**, dengan memori percakapan persisten (SD) & tombol Hapus cepat |
| F | **Files** | File explorer SD Card + **web uploader** (akses dari browser lewat WiFi) |
| M | **MJPEG** | Pemutar video Motion-JPEG dari file `.mjpeg` di folder `/mjpeg` |
| U | **Update** | OTA update firmware dari file `.bin` di SD (folder `/update`) atau unduh dari URL via WiFi |
| B | **Baterai** | Detail tegangan, estimasi persen & sisa mAh, status kesehatan |
| S | **Snake** | Klasik, kontrol arah dengan ketuk layar, **wrap-around** menembus tepi layar |
| V | **Flappy** | Ketuk untuk terbang, hindari pipa dengan celah yang dijamin selalu valid |
| 2 | **2048** | Kontrol swipe 4 arah |
| X | **TicTacToe** | vs CPU (AI heuristik) **atau** PvP 2 pemain lokal, papan 3×3/4×4/5×5 (ukuran = tingkat kesulitan) |
| K | **Breakout** | Kontrol paddle drag jari (utama) atau kemiringan HP (kalau MPU6050 terpasang) |
| Q | **Trivia** | Kuis pilihan ganda real-time dari **Open Trivia Database**, 12 kategori, 3 tingkat kesulitan + acak, 5/10/15 soal |
| ★ | **Astronomi** | **NASA Astronomy Picture of the Day** — gambar + penjelasan (auto-translate EN→ID), lihat [detail di bawah](#nasa-apod--astronomi) |

---

## Integrasi Layanan Online

Semua fitur di bawah butuh WiFi tersambung (lewat app **Setting**).

### Google Gemini (AI Chat)
- Model: `gemini-3.5-flash-lite`
- API key disimpan di `/gemini_key.txt` di SD Card (dibuat otomatis dengan instruksi kalau belum ada). Isi baris tanpa tanda `#` dengan key dari [Google AI Studio](https://aistudio.google.com/app/apikey).
- Riwayat percakapan (`/ai_memory.txt`) disuntikkan otomatis ke setiap prompt baru supaya AI "ingat" konteks obrolan sebelumnya — maksimum 12 entri tanya-jawab disimpan, entri lama otomatis dipangkas.
- Request dijalankan di FreeRTOS task terpisah (bukan blocking UI), dengan watchdog soft-timeout (15 detik) & hard-timeout (30 detik).

### Open Trivia Database (Trivia)
- Endpoint: `opentdb.com/api.php`, data diminta dalam **base64** khusus untuk menghindari isu tanda kutip di parser JSON manual.
- 12 kategori: Umum, Sains & Alam, Komputer, Olahraga, Geografi, Sejarah, Film, Musik, Video Game, Hewan, Anime & Manga, Mitologi.
- Tingkat kesulitan: Mudah / Sedang / Sulit / Acak. Jumlah soal: 5 / 10 / 15.

### NASA APOD — Astronomi
Alur kerja lengkap tiap buka tanggal baru:
1. **Ambil JSON** dari `api.nasa.gov/planetary/apod` (default = "hari ini", ditentukan server — bukan RTC lokal HP, supaya tidak tergantung status sinkronisasi NTP).
2. Kalau media-nya foto: **unduh URL gambar** dan **stream langsung ke SD Card** sebagai cache (`/apod_cache/<tanggal>.jpg`) — tidak perlu diunduh ulang kalau tanggal yang sama dibuka lagi.
3. **Decode & render** file JPG dari cache ke layar pakai `JPEGDEC`, otomatis diskalakan (½/¼/⅛) supaya gambar besar tetap muat di layar 320×240.
4. Teks `explanation` (Inggris) **diterjemahkan ke Indonesia** lewat **MyMemory Translation API** (`api.mymemory.translated.net`, gratis tanpa key, ~5000 karakter/hari per-IP), dipecah per ±450 karakter karena ada batas panjang per-query. Hasil terjemahan **juga di-cache** ke SD (`_id.txt`) supaya kuota tidak terbuang buat tanggal yang sudah pernah dibuka.
- Navigasi hari sebelumnya/berikutnya (`<` `>`), toggle lihat teks asli EN vs terjemahan ID, kredit foto ditampilkan kalau ada.
- Kalau konten hari itu berupa video (bukan foto), ditampilkan pesan alih-alih error.
- API key NASA opsional, disimpan di `/nasa_key.txt` (pola sama seperti Gemini) — default `DEMO_KEY` tetap jalan tapi jatah request per jam lebih kecil. Daftar gratis di [api.nasa.gov](https://api.nasa.gov).

### Web File Manager (app Files)
Server HTTP (port 80) otomatis jalan begitu WiFi tersambung. Alamat IP ditampilkan di app Files. Bisa upload, download, edit (inline textarea), dan hapus file di SD Card dari browser HP/PC lain di jaringan yang sama.

---

## Struktur File di SD Card

```
/
├── notepad.txt              # isi app Notepad
├── gemini_key.txt           # API key Gemini (auto-generate)
├── nasa_key.txt             # API key NASA, opsional (auto-generate)
├── ai_memory.txt            # riwayat percakapan AI Chat (maks 12 entri)
├── canvas_land.bin          # kanvas gambar — orientasi landscape
├── canvas_port.bin          # kanvas gambar — orientasi portrait
├── mjpeg/                   # taruh file *.mjpeg di sini utk app MJPEG
├── update/                  # taruh file *.bin di sini utk OTA lokal
└── apod_cache/
    ├── <tanggal>.jpg        # cache gambar APOD (mis. 2026_08_24.jpg)
    ├── <tanggal>.txt        # cache metadata (judul, tanggal, media_type, copyright, explanation EN)
    └── <tanggal>_id.txt     # cache hasil terjemahan Indonesia
```

---

## Cara Setup & Flashing

1. Buka file `.ino` di Arduino IDE / `arduino-cli` dengan board **ESP32-S3** (pastikan PSRAM diaktifkan — dipakai untuk semua sprite full-screen: `canvas`, `canvasApp`, `transShot`, `apodImg`, buffer MJPEG, dsb).
2. Install semua [library yang dibutuhkan](#library-yang-dibutuhkan).
3. Sambungkan hardware sesuai [tabel wiring](#hardware--wiring) di atas.
4. Siapkan SD Card (format FAT32), masukkan ke slot SDIO.
5. Flash firmware. Saat pertama kali nyala:
   - Kalibrasi touch akan berjalan otomatis (ikuti instruksi di layar, sentuh tiap sudut).
   - Boot sequence "Ren Phone" → "SanzX OS" akan tampil.
6. Masuk ke app **Setting** untuk sambungkan WiFi (dibutuhkan untuk AI Chat, Trivia, Astronomi, Update via WiFi, dan Web File Manager).
7. (Opsional) isi `/gemini_key.txt` dan `/nasa_key.txt` di SD Card dengan API key sendiri untuk kuota lebih besar.

### Reset kalibrasi touch
Tombol **"Kalibrasi Ulang"** ada di app Setting — akan me-restart perangkat dan menjalankan ulang wizard kalibrasi.

---

## Library yang Dibutuhkan

| Library | Kegunaan |
|---|---|
| `LovyanGFX` | Driver layar ILI9341 + touch XPT2046 (sprite, DMA, dll) |
| `Preferences` | Penyimpanan pengaturan persisten (NVS) |
| `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure` | Konektivitas & web server bawaan |
| `SD_MMC`, `FS` | Akses SD Card via SDIO |
| `Update` | OTA firmware update |
| `Wire` | I2C untuk MPU6050 |
| `JPEGDEC` (bitbank2) | Decode gambar JPEG (dipakai MJPEG player & app Astronomi) |
| `MjpegClass` | Wrapper pemutaran video Motion-JPEG (berbasis JPEGDEC) |

Semua library di atas tersedia lewat Arduino Library Manager atau repo GitHub masing-masing (kecuali `MjpegClass.h`, yang perlu disertakan manual sebagai file lokal di folder sketch — cek referensi contoh player MJPEG ESP32 populer kalau belum punya salinannya).

---

## Preferensi Tersimpan (NVS)

| Namespace | Key | Isi |
|---|---|---|
| `ui` | `theme` | Indeks tema aktif |
| `ui` | `orient` | Orientasi (landscape/portrait) |
| `ui` | `autorot` | Status auto-rotate |
| `ui` | `shakeon` | Status shake-to-home |
| `wifi` | `ssid`, `pass` | Kredensial WiFi tersimpan |
| `touch_cal` | `done`, `data` | Status & data kalibrasi touch |

---

## Filosofi & Catatan Desain

Beberapa keputusan desain yang konsisten dipertahankan di seluruh codebase (didokumentasikan di komentar changelog sepanjang file):

- **Animasi berbasis waktu (time-based), bukan berbasis jumlah-frame tetap**, untuk transisi & scroll — supaya durasi terasa konsisten di kondisi hardware apa pun (SPI sibuk, WiFi aktif, dll), walau kadang mengorbankan jumlah frame yang sempat digambar.
- **Satu fungsi easing (`navEase`) dipakai ulang** oleh banyak animasi berbeda (transisi nav, Control Center, Game Booster, boot sequence) supaya "rasa" gerakan konsisten di seluruh sistem — animasi baru yang butuh nuansa berbeda (mis. overshoot Dynamic Island) sengaja dibuat fungsi terpisah, bukan mengubah `navEase` global.
- **Area gambar & area sentuh selalu dihitung dari fungsi/konstanta yang sama** (bukan angka hardcode berduplikat) — pelajaran dari bug lama di Control Center yang area kartunya sempat tidak sinkron dengan area yang bisa disentuh.
- **Trik "cheap glow"/gradient murah** dipakai di boot sequence & efek visual lain sebagai pengganti alpha-blending sungguhan yang mahal untuk MCU embedded.
- **Cache-first untuk semua data online** (Gemini memory, Trivia tidak di-cache krn real-time, APOD gambar+terjemahan) — meminimalkan pemakaian data & kuota API.

---

## Known Limitations

- Estimasi baterai berbasis voltage divider murni (bukan fuel-gauge IC) — perkiraan kasar, bukan presisi.
- `DEMO_KEY` NASA & mode tanpa key MyMemory punya rate-limit rendah untuk pemakaian berat.
- AI Chat & fitur berbasis API lain butuh WiFi + endpoint publik yang bisa diakses (tidak berfungsi offline).
- Custom JSON parser di beberapa app (Gemini, Trivia, APOD) adalah parser ringan/manual (bukan library JSON penuh) — cukup untuk bentuk respons API yang ditargetkan, tapi tidak robust untuk format JSON arbitrer.
- Layar APOD men-decode ulang gambar dari cache lokal kalau orientasi berubah selagi app terbuka (tidak instan, tapi tidak perlu koneksi internet lagi).
