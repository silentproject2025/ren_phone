# App Kamera (OV2640 di ESP32-CAM)

Kamera ada di modul **ESP32-CAM** (ko-prosesor), bukan di S3. Gambar JPEG dikirim lewat link UART yang sama
dengan audio Bluetooth (`rplink.h`). **Flash ulang DUA firmware:** S3 (`phone`) dan ESP32-CAM (`cam_bt_coprocessor`).
Tanpa firmware CAM baru, app menampilkan "CAM belum mendukung kamera" (tidak merusak fitur lain).

## Pasang
```
python3 apply_camera_patch.py     # idempoten; mendaftarkan app, hook musicbt, firmware CAM, workflow
```
File baru: `camera_renphone.ino`, `cam_rx.h`, `cam_avi.h`, `cam_bt_coprocessor/cam_module.h`, `cam_bt_coprocessor/cam_tx.h`.
Tidak ada library tambahan (driver kamera sudah bawaan core ESP32).

## Fitur
- **Preview langsung** (layar dipaksa landscape): 160x120 (2x), 320x240, 640x480 (decode 1/2). HUD: resolusi, fps, KB/frame, tanda link cepat.
- **Foto** sampai 1600x1200, disimpan **apa adanya dari sensor** ke `/Kamera/IMG_*.jpg` (tanpa encode ulang; muncul otomatis sebagai album "Kamera" di Galeri). Timer 3/10 dtk, beruntun 3x/5x, kualitas JPEG 4..30.
- **Flash LED GPIO4 dengan PWM**: senter 30% / 100%, atau kilat penuh saat foto (senter mati otomatis setelah 2 menit).
- **Video** AVI MJPEG (`/Kamera/VID_*.avi`, pemutar AVI bawaan + PC/VLC). Tombol "Putar" langsung memutar video terakhir.
- **Timelapse** 2/5/10/30/60 dtk -> satu file AVI (diputar 10 fps).
- **Deteksi gerak**: decode JPEG skala 1/8 (hanya DC) + kompensasi cahaya global; simpan `GERAK_*.jpg` otomatis, getar + toast, jeda 6 dtk.
- **Kontrol sensor**: kecerahan, kontras, saturasi, ketajaman, white balance, efek (negatif/abu/sepia/RGB), eksposur, mode malam, koreksi lensa, mirror, flip, garis bantu, kualitas preview.

## Deteksi sensor otomatis
Saat kamera dinyalakan, driver mengenali sensor lewat bus SCCB dan CAM melaporkannya ke S3 (toast "OV3660 - foto maks 2048x1536"; juga tampil di Atur > Sistem).
- **OV2640**: foto sampai 1600x1200.
- **OV3660 / OV5640**: foto sampai 2048x1536 (buffer foto S3 otomatis diperbesar). OV3660 dikoreksi otomatis (vflip bawaan, saturasi/kecerahan awal) agar tidak terbalik/terlalu jenuh; tombol Mirror/Flip tetap bekerja relatif terhadap koreksi itu. Autofokus OV5640 tidak diaktifkan.
- Pesan jelas bila **sensor tidak terdeteksi** (kabel flat/pemasangan) atau **sensor tanpa JPEG** (mis. OV7670/OV7725).
- Yang tidak otomatis: peta pin board lain. Kode memakai pin AI-Thinker; board klon dengan pin berbeda perlu mengubah `CAM_PIN_*` di `cam_module.h`.

## Yang dipacu
| Bagian | Cara |
|---|---|
| Link UART | Naik otomatis 921.6k -> **2 / 3 / 4 / 5 Mbaud** saat app dibuka (pilih batas atas di Atur > Sistem; bawaan 5M). 921k ~90 KB/s, 3M ~300 KB/s, **5M ~500 KB/s**. 4M & 5M pembagi UART-nya pas (80 MHz/20 dan /16). Turun lagi saat keluar. |
| Turun otomatis | Dari batas atas, link turun **satu anak tangga** (5M -> 4M -> 3M -> 2M -> 921k) kalau kabel tidak bersih: (a) >= 3 frame rusak dalam 5 dtk -> CAM diminta pindah langsung; (b) tidak ada frame valid 1.5 dtk setelah pindah, atau CAM hilang > 2 dtk di baud tinggi -> S3 mundur ke 921k, tunggu CAM ikut mundur (maks 3 dtk), lalu coba anak tangga di bawahnya. Tidak naik lagi dalam sesi yang sama. |
| ACK tanpa menunggu UI | S3 mengirim `RP_CAM_ACK [seq]` begitu potongan akhir frame tiba (di `musTask`), bukan setelah UI selesai decode + gambar. Kabel tidak menganggur selama decode; UI tetap mengambil frame terbaru. Kontrol aliran berbasis NOMOR URUT (jendela 3 frame): ACK/frame yang hilang tidak membocorkan kredit. `camTask` dibangunkan langsung oleh ACK (task notify), bukan polling 2 ms. |
| Dedup header JPEG | Header OV2640 (~600-650 B: DQT+DHT+SOF) identik di tiap frame preview -> dihilangkan, diganti id 16-bit; S3 menyalin dari cache. Id harus cocok, kalau tidak frame dibuang (tidak pernah salah warna/gambar). Cache S3 hilang -> bit "butuh header" di ACK -> frame berikutnya lengkap lagi. Tiap 30 frame tetap dikirim lengkap. Paling terasa di preview kecil (160x120: ~25% lebih hemat; 320x240: ~8%). |
| Aliran frame | `GRAB_LATEST`: selalu frame terbaru, latensi rendah, tidak menyumbat musik. |
| S3 | CPU 240 MHz selama app terbuka; baca UART per blok 256 byte (bukan per byte); RX buffer **10 KB** (naik dari 6 KB; +4 KB RAM internal, perlu di 5 Mbaud); task link berputar tiap 1 tick saat kamera aktif. |
| CAM | Buffer kamera di PSRAM (2 frame, ukuran UXGA), kamera hanya hidup saat app terbuka (RAM dikembalikan ke BT/MP3 saat keluar), XCLK 10/16/20 MHz. |

Batas yang tidak diubah: CPU CAM sudah 240 MHz; XCLK 20 MHz sudah mendekati batas praktis OV2640; PSRAM CAM berjalan 40 MHz (setelan core, perlu build core khusus untuk mengubahnya).

## Status pengujian (jujur)
- **Link maks (v2)**: dedup header, kontrol aliran seq (termasuk wrap-around 256, ACK basi/ngawur, rescue), id header basi, cache hilang + pemulihan, frame rusak -> diuji di komputer (`tests_kamera/test_cam.cpp`, bit-exact). Sintaks `camera_renphone.ino` & `cam_module.h` dicek dgn mock. **Belum dijalankan di perangkat**: stabilitas 4/5 Mbaud tergantung kabel; kalau tidak stabil, link turun sendiri (lihat "Turun otomatis").
- **Flash DUA firmware sekaligus** (S3 + CAM): arti `RP_CAM_ACK` berubah. CAM baru + S3 lama = preview tersendat ~1 fps (ACK lama dianggap basi); CAM lama + S3 baru tetap jalan (tanpa dedup).
- **Belum dikompilasi dengan toolchain ESP32 dan belum dijalankan di perangkat.** Sintaks `camera_renphone.ino` dan `cam_module.h` dicek dengan mock API; protokol (pemecahan/penyusunan frame, frame rusak, slot, foto vs preview) dan penulis AVI diuji di komputer (`tests_kamera/`); AVI hasilnya didecode oleh ffmpeg (10 frame, 10 fps, idx1 valid).
- fps nyata tergantung kabel dan beban; HUD menampilkannya.

## Risiko yang perlu dicoba
1. **RAM internal CAM**: kamera + A2DP + MP3 sekaligus ketat. Jika HUD/pesan "RAM CAM kurang", matikan Bluetooth/musik dulu. Ambang ada di `CAM_MIN_BLOCK` (`cam_module.h`).
2. **Daya**: ESP32-CAM gampang brownout saat kamera + radio BT. Pakai pin 5V dengan catu yang kuat.
3. **Kabel UART 3 Mbaud** bisa tidak stabil kalau panjang; fallback otomatis ke 921k, atau pilih 2M / 921k di Atur.
4. Musik bisa tersendat sebentar saat app dibuka/ditutup (momen pindah baud).
5. Galeri hanya mengenali foto; video AVI diputar lewat tombol "Putar" atau app Files.
6. Kamera pada board bisa ter-rotasi: pakai Mirror/Flip di Atur > Layar.

## Pengaturan pin (AI-Thinker)
Kamera memakai pin standar AI-Thinker; link UART (GPIO13/14) tidak bentrok. SD card di ESP32-CAM harus tetap kosong (GPIO4 = LED flash).
