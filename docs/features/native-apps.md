# Nyx Apps — app native (ELF) dari kartu SD

Jalankan app buatan sendiri **dengan kecepatan native** (kode mesin Xtensa) tanpa flash ulang firmware.
App = folder di SD; dibuka dari ikon **Nyx Apps** di Home.

```
SD:/apps/<id>/
    manifest.txt     nama, ikon, izin, quota memori
    app.elf          hasil build ESP-IDF (idf.py elf)
    data/            dibuat otomatis — SATU-SATUNYA folder yang boleh disentuh app
```

## Status verifikasi (baca ini dulu)

| Bagian | Status |
|---|---|
| Arena memori, quota, deteksi bocor/korupsi, tekanan, TLS Gate (`nyxmem.h`) | **Diuji di PC**: 189 cek, fuzz 5 seed, ASan+UBSan, uji mutasi |
| Host: manifest, sandbox path, izin, handle file, HTTP async (`nyxhost.h`) | **Diuji di PC**: 158 cek, 5 app contoh dijalankan lewat `dlopen`, uji mutasi |
| Glue firmware (`nyxapp_renphone.ino`), patch `phone.ino` | **Hanya cek-sintaks** dengan stub; patch terbukti cocok & idempoten pada `phone.ino` repo ini. **Belum pernah dijalankan di HP.** |
| Loader ELF di Xtensa/PSRAM, task + watchdog, worker HTTP, tilt | **Belum diuji di hardware.** Titik risiko terbesar: lihat "Mengaktifkan ELF loader" dan "Hal yang harus dicek di HP". |

Tes PC: `cd tests/nyxapp && ./run.sh` (butuh `g++` dan `gcc`).

## Model kepercayaan (jujur)

App native berbagi ruang alamat dengan firmware dan memakai pointer asli. ESP32-S3 tidak punya proteksi
memori per-app yang bisa dipakai. Karena itu:

* **Terlindungi:** app yang *salah* — macet/loop tanpa henti (watchdog membunuh task), minta memori
  kelewat quota (ditolak, bukan crash), lupa `free` (dibebaskan & dilaporkan saat keluar),
  tulis lewat batas blok (canary terdeteksi), double-free/free pointer asing (dihitung & diabaikan),
  buka file di luar `data/` lewat API (ditolak), pakai fitur tanpa izin (ditolak).
* **Tidak terlindungi:** app yang *jahat* atau punya pointer liar. Ia bisa menimpa memori firmware,
  atau memanggil fungsi libc/IDF langsung (mis. `fopen` ke path mana pun) — sandbox file hanya berlaku
  untuk akses lewat `NyxApi`. **Pasang hanya app yang kamu percaya.**
  Jika perlu sandbox sungguhan, lihat `experimental/nyxvm/` (belum selesai).

## Manifest

```
name=Hello Native      # wajib, <24 karakter
sym=H                  # satu karakter untuk ikon
color=3060d0           # RRGGBB
perms=fs,net,tilt,notify
mem_kb=256             # quota arena PSRAM, 64..6144
stack_kb=8             # stack task app, 4..32
elf=app.elf
```
Nilai di luar rentang / izin tak dikenal / `elf` berisi `/` atau `..` → manifest ditolak.

## Menulis app

```c
#include "nyx_app.h"
NYX_APP_GLOBALS
static int  app_enter(void) { return 0; }                 // sekali saat dibuka
static void app_frame(void) { g_nyx->clear(g_nyx->rgb(0,0,0));
                              g_nyx->text(8, 8, "Halo", 0xFFFF, 2); }   // tiap frame, HARUS cepat
static void app_leave(void) {}                            // sekali saat ditutup
NYX_APP_MAIN(app_enter, app_frame, app_leave)
```
* `main()` hanya mendaftarkan `enter/frame/leave` lalu kembali. Semua kerja di `frame()`.
* Satu frame > **250 ms** → task dibunuh (watchdog). Target < 30 ms.
* Memori: `mem_alloc/mem_calloc/mem_realloc/mem_free` (arena ber-quota). Jangan pakai `malloc` libc.
* Gambar: koordinat relatif viewport (0,0 = kiri-atas area app, di bawah status bar). RGB565.
* File: `fopen("catatan.txt", NYX_FOPEN_WRITE)` — path relatif ke `data/`, tanpa `..`, tanpa `/` di awal. Maks 4 handle.
* Jaringan: `http_get` + polling `http_state` (satu permintaan pada satu waktu, buffer maks 128 KB).
  HTTPS menunggu **TLS Gate** (blok kontigu RAM internal ≥ 32 KB, memicu reclaim `onLowMem` app lain).
* Contoh lengkap: `examples/native/{hello,bounce,memhog,notes,leaky}`. Referensi API: `firmware/phone/nyx_app.h`.

## Membangun app (ESP-IDF)

```bash
cd examples/native/idf_template
export NYX_SDK=$PWD/../../../firmware/phone
cp ../hello/app.c main/app.c
idf.py set-target esp32s3 && idf.py elf             # -> build/nyxapp.app.elf
python3 ../../../tools/pack_app.py --id hello --elf build/nyxapp.app.elf \
        --manifest ../hello/manifest.txt --sd /media/SDCARD
```
Untuk mencoba logika app di PC tanpa hardware: `gcc -shared -fPIC -I firmware/phone app.c -o app.so`
(dipakai `tests/nyxapp/test_host.cpp`).

## Memasang ke firmware

1. Salin `firmware/phone/{nyx_app.h,nyxmem.h,nyxhost.h,nyxapp_renphone.ino}` ke folder sketch.
2. Dari root repo: `python3 apply_nyxapp_patch.py` (atau `--check` dulu). Cadangan: `phone.ino.bak_nyxapp`.
   Menambah `SCR_NYX`, entri `apps[]` (45→46), ikon chip, dan redraw ~30 fps saat app jalan.
3. Tanpa ELF loader, build tetap sukses: Nyx Apps terbuka & menjelaskan bahwa loader belum ada.

## Mengaktifkan ELF loader (bagian yang belum kucoba)

`espressif/elf_loader` adalah **komponen ESP-IDF** (diaktifkan lewat `CONFIG_ELF_LOADER`, dan
`ELF_LOADER_LOAD_PSRAM` agar kode dimuat di PSRAM). Build Arduino-ESP32 memakai pustaka IDF yang sudah
dikompilasi, jadi komponen ini harus ikut masuk ke sana:
* lewat workflow **Build Arduino Libs** milikmu (tambahkan komponen + opsi sdkconfig, lalu pakai libs hasilnya), atau
* lewat mode "Arduino sebagai komponen ESP-IDF".

Catatan dari riwayat repo: build libs kustom pernah bermasalah di linker, jadi lakukan di cabang terpisah.
Glue memakai API `esp_elf_init / esp_elf_relocate(elf, data, size) / esp_elf_request / esp_elf_deinit`
(seri 2.x); sesuaikan bila versimu berbeda. `esp_elf.h` terdeteksi otomatis lewat `__has_include`.

## Hal yang harus dicek di HP (urutan saran)

1. Build tanpa loader → buka Nyx Apps → daftar app tampil, pesan loader benar.
2. Build dengan loader → `hello`: ELF termuat? **Cek simbol titik masuk**: glue memanggil
   `esp_elf_request(&elf, 0, NYX_ENTRY_ARGC, argv)` dengan `argv[0]` = pointer `NyxApi`. Jika loader
   memakai konvensi entry lain, hanya `nyxTaskFn` (cabang `NYXC_START`) yang perlu diubah.
3. `memhog` → Serial harus menampilkan puncak ≈ quota & `bocor 0 blok`. `leaky` → toast "App merusak heap-nya sendiri".
4. Watchdog: buat app dengan `while(1){}` di `frame()` → harus dibunuh < 0,5 s tanpa membekukan HP.
5. HTTP: `http_get` ke URL biasa dulu, baru HTTPS. Task worker memakai `HTTPClient` di core 0.
6. Pantau `[NyxApp]` di Serial dan `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` sebelum/sesudah app.

## Batasan yang disengaja
* Satu app pada satu waktu; tidak ada hibernate/resume (itu butuh VM; lihat `experimental/nyxvm/`).
* Tidak ada syscall audio/kamera (izin `AUDIO`/`CAMERA` belum ada).
* Ikon app tidak muncul di grid Home; semua lewat satu ikon **Nyx Apps** (grid Home bersifat statis dari `apps[]`).
* Tidak terintegrasi ke App Switcher.
* ELF maks 1 MB; quota arena 64 KB – 6 MB; `NYXAPP_FRAME_MS` = 250 ms (ubah di `nyxapp_renphone.ino`).
