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

## Yang dipacu
| Bagian | Cara |
|---|---|
| Link UART | Naik otomatis 921.6k -> **2 / 3 Mbaud** saat app dibuka (pilih di Atur > Sistem). Di 921k, ~90 KB/s; di 3M, ~300 KB/s. Turun lagi saat keluar. |
| Fallback aman | Jika tidak ada frame valid 1.5 dtk setelah pindah baud, S3 kembali ke 921k; CAM juga kembali sendiri setelah 3 dtk. |
| Aliran frame | Kontrol kredit (maks 2 frame di jalan) + `GRAB_LATEST`: selalu frame terbaru, latensi rendah, tidak menyumbat musik. |
| S3 | CPU 240 MHz selama app terbuka; baca UART per blok 256 byte (bukan per byte); RX buffer 6 KB; task link berputar tiap 1 tick saat kamera aktif. |
| CAM | Buffer kamera di PSRAM (2 frame, ukuran UXGA), kamera hanya hidup saat app terbuka (RAM dikembalikan ke BT/MP3 saat keluar), XCLK 10/16/20 MHz. |

Batas yang tidak diubah: CPU CAM sudah 240 MHz; XCLK 20 MHz sudah mendekati batas praktis OV2640; PSRAM CAM berjalan 40 MHz (setelan core, perlu build core khusus untuk mengubahnya).

## Status pengujian (jujur)
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
