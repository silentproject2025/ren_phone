#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_readme_update.py -- jalankan dari root repo ren_phone (folder yang ada README.md).
Menyamakan README dengan kode terbaru: 38 aplikasi, ±26.500 baris, dukungan PNG,
langkah CI baru (ci_hoist_types.py + diagnostik), struktur repo, dan batasan baru.
Aman dijalankan ulang (yang sudah terpasang dilewati).
"""
import io, sys

R = []  # (nama, lama, baru, penanda_sudah_terpasang)

R.append(("jumlah baris",
 "`±17.800 baris firmware`", "`±26.500 baris firmware`", "±26.500 baris"))
R.append(("jumlah app (tabel spesifikasi)",
 "| Jumlah aplikasi | 35 |", "| Jumlah aplikasi | 38 |", "| Jumlah aplikasi | 38 |"))
R.append(("jumlah app (judul)",
 "### Aplikasi (35)", "### Aplikasi (38)", "### Aplikasi (38)"))
R.append(("kategori Sistem",
 "| Sistem | Jam, Setting, Update (OTA), Baterai, HWmonitor, Orientasi3D, Mic Level, Neopixel |",
 "| Sistem | Jam, Setting, Update (OTA), Baterai, HWmonitor, Orientasi3D, Mic Level, Neopixel, Profil, About Phone, Atur Home |",
 "Neopixel, Profil, About Phone, Atur Home"))
R.append(("Galeri + PNG",
 "Pembuatan thumbnail dibatasi satu gambar per putaran loop dan hanya saat jari tidak menyentuh layar.",
 "Pembuatan thumbnail dibatasi satu gambar per putaran loop dan hanya saat jari tidak menyentuh layar. "
 "**Format gambar: JPG dan PNG.** PNG di-decode dengan decoder bawaan LovyanGFX (helper `rpPng*` di `phone.ino`), "
 "dengan batas 4 MB per file dan 8 juta piksel; PNG di atas 2 MB tetap bisa dibuka tetapi tanpa thumbnail. "
 "Astronomi (APOD) memakai helper PNG yang sama.",
 "Format gambar: JPG dan PNG"))
R.append(("catatan build / CI",
 "beberapa struct di kode sengaja diletakkan paling atas file karena alasan ini.",
 "beberapa struct di kode sengaja diletakkan paling atas file karena alasan ini.\n\n"
 "**Langkah CI tambahan**: `ci_hoist_types.py` memindahkan blok `#include`, struct, dan enum di bagian atas `phone.ino` "
 "(±baris 459–716) ke header `rp_types.h` yang di-`#include` di baris pertama. Ini hanya terjadi pada salinan di `build_sketch/` "
 "saat CI; `phone.ino` di repo tidak diubah. Tujuannya supaya prototipe otomatis `arduino-cli` selalu disisipkan setelah semua tipe dikenal. "
 "Jika compile gagal, workflow meng-upload artifact `diagnostik-prototipe` (hasil preprocess dan versi tool) untuk membantu melacak penyebabnya. "
 "Versi `arduino-cli`, core ESP32, dan library di CI belum dikunci, jadi update dari upstream bisa memengaruhi build.",
 "Langkah CI tambahan"))
R.append(("struktur repo: file baru",
 "gallery_renphone.ino         Galeri + thumbnail cache\n",
 "gallery_renphone.ino         Galeri + thumbnail cache (JPG/PNG)\n"
 "avi_renphone.ino             pemutar AVI (MJPEG + MP3)\n"
 "about_renphone.ino  owner_renphone.ino  homeedit_renphone.ino   About Phone, Profil, Atur Home\n",
 "avi_renphone.ino             pemutar AVI"))
R.append(("struktur repo: skrip CI",
 "apply_*.py  patch_*.py       skrip patch untuk perubahan bertahap\n",
 "apply_*.py  patch_*.py       skrip patch untuk perubahan bertahap\n"
 "ci_hoist_types.py            langkah CI: pindahkan tipe ke rp_types.h (dijalankan workflow)\n",
 "ci_hoist_types.py            langkah CI"))
R.append(("batasan baru",
 "- Library `lvgl` terdaftar di CI tetapi tidak dipakai; mesin render sepenuhnya LovyanGFX.",
 "- PNG dibatasi 4 MB per file dan 8 juta piksel; decode memakai buffer penuh di PSRAM, jadi PNG di atas 2 MB tidak punya thumbnail di Galeri.\n"
 "- Versi `arduino-cli`, core ESP32, dan library tidak dikunci di CI; update upstream bisa mematahkan build tanpa perubahan apa pun di repo.\n"
 "- Library `lvgl` terdaftar di CI tetapi tidak dipakai; mesin render sepenuhnya LovyanGFX.",
 "PNG dibatasi 4 MB per file"))

def main():
    try:
        with io.open("README.md", encoding="utf-8") as f:
            t = f.read()
    except FileNotFoundError:
        print("ERROR: README.md tidak ada di folder ini"); return 1
    gagal = False
    for nama, lama, baru, tanda in R:
        if tanda in t:
            print("--  %s: sudah terpasang" % nama); continue
        if t.count(lama) != 1:
            print("GAGAL %s: teks lama tidak ketemu persis 1x (README sudah berubah?)" % nama)
            gagal = True; continue
        t = t.replace(lama, baru); print("OK  %s" % nama)
    if gagal:
        print("\nAda bagian yang gagal; README TIDAK ditulis."); return 1
    with io.open("README.md", "w", encoding="utf-8", newline="\n") as f:
        f.write(t)
    print("\nSelesai. Sekarang: git add README.md && git commit && git push")
    return 0

if __name__ == "__main__":
    sys.exit(main())
