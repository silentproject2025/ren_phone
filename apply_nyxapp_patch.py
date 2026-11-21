#!/usr/bin/env python3
"""
apply_nyxapp_patch.py -- pasang "Nyx Apps" (app native dari SD) ke firmware/phone/phone.ino

Pakai (dari root repo, mis. ~/ren_phone di Termux):
    python3 apply_nyxapp_patch.py            # pasang
    python3 apply_nyxapp_patch.py --check    # hanya cek apakah anchor cocok

Idempoten (aman dijalankan ulang) & atomik: kalau SATU anchor tidak ketemu persis sekali,
TIDAK ADA yang diubah. File baru (nyx*.h, nyxapp_renphone.ino) harus sudah disalin ke
firmware/phone/ -- skrip ini hanya mengubah phone.ino.
"""
import sys, os

PATH = os.path.join("firmware", "phone", "phone.ino")
MARK = "NYXAPP_V1"

EDITS = [
    # (nama, anchor lama, pengganti)
    ("include header host",
     '#include "esp_heap_caps.h"',
     '#include "esp_heap_caps.h"\n#include "nyxhost.h"   // NYXAPP_V1: tipe nyx:: harus terbaca sebelum prototipe otomatis arduino-cli'),
    ("enum Screen",
     '              // Catur online (MQTT) -- lihat chess_renphone.ino\n              SCR_CHESS };',
     '              // Catur online (MQTT) -- lihat chess_renphone.ino\n              SCR_CHESS,\n'
     '              // Nyx Apps: app native (ELF) dari SD -- lihat nyxapp_renphone.ino  // NYXAPP_V1\n              SCR_NYX };'),
    ("deklarasi hook",
     'void chessEnter(); void chessExit(); void drawChess(LGFX_Sprite&); void chessTouch(int,int,bool,bool); void chessLoopPoll();',
     'void chessEnter(); void chessExit(); void drawChess(LGFX_Sprite&); void chessTouch(int,int,bool,bool); void chessLoopPoll();\n'
     'void nyxAppEnter(); void nyxAppExit(); void drawNyxApp(LGFX_Sprite&); void nyxAppTouch(int,int,bool,bool); bool nyxWantsRedraw(); // NYXAPP_V1'),
    ("ukuran apps[]",
     'AppDef apps[45] = {',
     'AppDef apps[46] = {'),
    ("entri apps[]",
     '  { "Catur",   \'q\', 0, chessEnter,  chessExit,  drawChess,  chessTouch,  SCR_CHESS  },\n};\n#define APP_COUNT 45',
     '  { "Catur",   \'q\', 0, chessEnter,  chessExit,  drawChess,  chessTouch,  SCR_CHESS  },\n'
     '  // ---- Nyx Apps: jalankan app native (ELF) dari SD ----  // NYXAPP_V1\n'
     '  { "Nyx Apps", \'n\', 0, nyxAppEnter, nyxAppExit, drawNyxApp, nyxAppTouch, SCR_NYX },\n};\n#define APP_COUNT 46'),
    ("ikon 'n'",
     "    case 'i': { // About Phone",
     "    case 'n': { // Nyx Apps: chip dengan kaki  // NYXAPP_V1\n"
     "      int rr=max(8,r-6); int q=rr*3/4;\n"
     "      nyxAaRRRing(s, cx-q,cy-q,q*2,q*2,max(2,q/4),ic); nyxAaRRRing(s, cx-q+1,cy-q+1,q*2-2,q*2-2,max(2,q/4),ic);\n"
     "      for(int k=-1;k<=1;k++){ s.fillRect(cx+k*q*2/3-1,cy-q-max(2,rr/5),2,max(2,rr/5),ic); s.fillRect(cx+k*q*2/3-1,cy+q,2,max(2,rr/5),ic);\n"
     "                              s.fillRect(cx-q-max(2,rr/5),cy+k*q*2/3-1,max(2,rr/5),2,ic); s.fillRect(cx+q,cy+k*q*2/3-1,max(2,rr/5),2,ic); }\n"
     "      s.fillRect(cx-q/3,cy-q/3,q*2/3+1,q*2/3+1,ic);\n"
     "      break;\n"
     "    }\n"
     "    case 'i': { // About Phone"),
    ("redraw kontinu",
     '    if(curScreen()==SCR_ABOUT) aboutLoopPoll();',
     '    if(curScreen()==SCR_ABOUT) aboutLoopPoll();\n'
     '    if(curScreen()==SCR_NYX && nyxWantsRedraw() && millis()-lastSen>33){ needRedraw=true; lastSen=millis(); } // NYXAPP_V1: app native jalan ~30 fps'),
]

def main():
    check = "--check" in sys.argv
    if not os.path.exists(PATH):
        sys.exit(f"tidak ada {PATH} -- jalankan dari root repo")
    src = open(PATH, encoding="utf-8").read()
    if MARK in src:
        print("sudah terpasang (penanda NYXAPP_V1 ada) -- tidak ada yang diubah"); return
    out = src
    for name, old, new in EDITS:
        n = out.count(old)
        if n != 1:
            sys.exit(f"GAGAL: anchor '{name}' ditemukan {n}x (harus tepat 1x). phone.ino berubah dari versi yang dipatch? Tidak ada yang diubah.")
        out = out.replace(old, new)
    for need in ("nyxhost.h", "nyxapp_renphone.ino", "nyxmem.h", "nyx_app.h"):
        if not os.path.exists(os.path.join("firmware", "phone", need)):
            print(f"PERINGATAN: firmware/phone/{need} belum ada -- salin dulu sebelum build")
    if check:
        print("OK: semua anchor cocok (mode --check, tidak menulis)"); return
    open(PATH + ".bak_nyxapp", "w", encoding="utf-8").write(src)
    open(PATH, "w", encoding="utf-8").write(out)
    print(f"OK: {len(EDITS)} perubahan dipasang. Cadangan: {PATH}.bak_nyxapp")

if __name__ == "__main__":
    main()
