#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_doom_race_patch.py  --  DOOM_RACE_FIX_V1

Memperbaiki Doom yang kadang jalan, kadang layar blank / setengah frame /
Doom "dipaksa muncul" sambil teks "Memuat..." masih kelihatan.

AKAR MASALAH
  doomIsPlaying() baru TRUE setelah DoomGlue_Begin() selesai. Padahal di
  dalam Begin() (doomgeneric_Create -> D_DoomLoop -> doomgeneric_Tick) core 1
  SUDAH menggambar frame Doom (title + efek melt, puluhan frame). Selama itu
  guard di loop() phone.ino belum aktif, jadi core 0 masih sempat
  renderCurrentFrame()+push() (teks "Memuat...", Dynamic Island "Mode Game
  Aktif" 1,3 dtk, sentuhan, jam) ke objek `display` yang SAMA -> dua core
  rebutan bus SPI + state LovyanGFX -> blank / setengah frame / hang.
  Terjadinya tergantung ada/tidaknya redraw core 0 pas jendela itu -> acak.

  Bug kedua: doomExit() menunggu `doomPlaying`, padahal saat fase Begin
  nilainya masih false -> keluar pas "Memuat..." tidak menunggu task sama
  sekali (task lama masih menggambar sementara Home sudah digambar core 0,
  dan doomEnter berikutnya bisa membuat task kedua).

CARA PAKAI (Termux, di folder repo yang berisi phone.ino):
  python apply_doom_race_patch.py
Backup otomatis: phone.ino.bak-doomrace dan doom_renphone.ino.bak-doomrace
"""
import os
import shutil
import sys

MARK = "DOOM_RACE_FIX_V1"

PATCHES = {
    "doom_renphone.ino": [
        # 1) flag baru doomBusy
        (
            "bool doomIsPlaying(){ return doomPlaying; }\n",
            "// DOOM_RACE_FIX_V1: doomBusy = TRUE sejak SEBELUM task dibuat sampai task BENAR-BENAR selesai.\n"
            "// doomPlaying baru true SETELAH DoomGlue_Begin() selesai, padahal di dalam Begin() core 1 sudah\n"
            "// menggambar frame Doom -> tidak cukup utk melindungi `display` dari core 0.\n"
            "static volatile bool doomBusy = false;\n"
            "bool doomIsPlaying(){ return doomPlaying; }\n"
            "bool doomIsBusy(){ return doomBusy; }\n",
        ),
        # 2) task selesai -> lepas flag
        (
            "  doomPlaying = false;\n  doomTaskHandle = nullptr;\n  vTaskDelete(nullptr);\n",
            "  doomPlaying = false;\n  doomTaskHandle = nullptr;\n  doomBusy = false;   // DOOM_RACE_FIX_V1: display boleh dipakai core 0 lagi\n  vTaskDelete(nullptr);\n",
        ),
        # 3) tolak masuk kalau sesi lama belum tutup
        (
            "void doomEnter(){\n  doomErrorMsg = \"\";\n\n  if(!sdReady){\n",
            "void doomEnter(){\n  doomErrorMsg = \"\";\n\n"
            "  if(doomBusy || doomTaskHandle != nullptr){ // DOOM_RACE_FIX_V1: jangan bikin task ke-2\n"
            "    doomErrorMsg = \"DOOM sebelumnya masih menutup.\\nTunggu sebentar lalu buka lagi.\";\n"
            "    return;\n"
            "  }\n"
            "  if(!sdReady){\n",
        ),
        # 4) gambar 'Memuat...' sekali dari core 0, lalu serahkan display ke core 1
        (
            "  doomStopRequested = false;\n  const uint32_t DOOM_STACK_SZ = 32768;\n",
            "  doomStopRequested = false;\n"
            "  // DOOM_RACE_FIX_V1: gambar \"Memuat...\" SEKALI dari core 0 (task belum ada -> aman), lalu\n"
            "  // serahkan display ke core 1. Mulai titik ini core 0 DILARANG menyentuh display\n"
            "  // (loop() + push() di phone.ino cek doomIsBusy()).\n"
            "  drawDoom(canvas); push();\n"
            "  doomBusy = true;\n"
            "  const uint32_t DOOM_STACK_SZ = 32768;\n",
        ),
        # 5) gagal bikin task -> lepas flag
        (
            "    doomErrorMsg = \"Gagal membuat task DOOM (RAM internal habis).\";\n",
            "    doomBusy = false;   // DOOM_RACE_FIX_V1\n"
            "    doomErrorMsg = \"Gagal membuat task DOOM (RAM internal habis).\";\n",
        ),
        # 6) doomExit: tunggu task selesai (bukan cuma doomPlaying)
        (
            "    while(doomPlaying && millis()-waitStart < 2000) delay(10);\n  }\n",
            "    // DOOM_RACE_FIX_V1: tunggu doomBusy (termasuk fase Begin/\"Memuat...\"), bukan doomPlaying.\n"
            "    while(doomBusy && millis()-waitStart < 12000) delay(10);\n"
            "    if(doomBusy) Serial.println(\"[doom] PERINGATAN: task belum selesai setelah 12 dtk\");\n"
            "    else delay(30);   // kasih idle task waktu bersihin TCB statis sebelum doomEnter berikutnya\n"
            "  }\n",
        ),
    ],
    "phone.ino": [
        # forward declaration
        (
            "bool doomIsPlaying(); // true selagi task DoomGlue pegang SPI display langsung\n",
            "bool doomIsPlaying(); // true selagi task DoomGlue pegang SPI display langsung\n"
            "bool doomIsBusy();    // DOOM_RACE_FIX_V1: true sejak sebelum task dibuat s/d task selesai (termasuk fase \"Memuat...\")\n",
        ),
        # guard utama di loop()
        (
            "else if(curScreen()==SCR_DOOM && doomIsPlaying()){",
            "else if(curScreen()==SCR_DOOM && doomIsBusy()){   /* DOOM_RACE_FIX_V1 */",
        ),
        # guard terpusat di push()
        (
            "void push(){ if(nyxThFreezeFrame()) return;\n",
            "void push(){ if(nyxThFreezeFrame()) return;\n"
            "  if(curScreen()==SCR_DOOM && doomIsBusy()) return;   // DOOM_RACE_FIX_V1: core 0 dilarang nyentuh display selagi task DOOM hidup\n",
        ),
        # tarik Control Center / Notif Shade saat Doom jalan = rebutan SPI juga (sama kasusnya dgn NES v89)
        (
            "bool nesBlocksCC = (curScreen()==SCR_NES && nesIsPlaying());",
            "bool nesBlocksCC = (curScreen()==SCR_NES && nesIsPlaying()) || (curScreen()==SCR_DOOM && doomIsBusy());   // DOOM_RACE_FIX_V1",
        ),
    ],
}


def main():
    base = os.getcwd()
    for fn in PATCHES:
        if not os.path.isfile(os.path.join(base, fn)):
            print("GAGAL: %s tidak ada di %s (jalankan dari folder repo)" % (fn, base))
            sys.exit(1)

    new_texts = {}
    for fn, reps in PATCHES.items():
        path = os.path.join(base, fn)
        with open(path, "r", encoding="utf-8", newline="") as f:
            txt = f.read()
        if MARK in txt:
            print("%s: sudah dipatch (%s), dilewati." % (fn, MARK))
            continue
        crlf = "\r\n" in txt
        for old, new in reps:
            if crlf:
                old = old.replace("\n", "\r\n")
                new = new.replace("\n", "\r\n")
            n = txt.count(old)
            if n != 1:
                print("GAGAL di %s: potongan kode ditemukan %d kali (harus 1):" % (fn, n))
                print("  " + old.strip().splitlines()[0][:100])
                print("Tidak ada file yang diubah. Kirim file terbaru kalau mau dicocokkan ulang.")
                sys.exit(1)
            txt = txt.replace(old, new, 1)
        new_texts[fn] = txt

    for fn, txt in new_texts.items():
        path = os.path.join(base, fn)
        shutil.copyfile(path, path + ".bak-doomrace")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(txt)
        print("OK: %s dipatch (backup: %s.bak-doomrace)" % (fn, fn))

    print("\nSelesai. Build ulang & flash, lalu tes buka-tutup Doom beberapa kali.")


if __name__ == "__main__":
    main()
