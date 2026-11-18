#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_chess_patch.py -- daftarkan app "Catur" (catur online lewat MQTT) ke phone.ino + workflow.
#
# Jalankan dari folder repo (yang ada phone.ino + chess_renphone.ino + chat_renphone.ino):
#     python3 apply_chess_patch.py
#
# Idempoten & semua-atau-tidak-sama-sekali: kalau satu jangkar tidak ketemu persis 1 kali,
# TIDAK ADA file yang diubah. App Catur memakai broker/akun dari app Chat, jadi
# chat_renphone.ino harus sudah terpasang (apply_chat_patch.py).
import sys, os

for f in ('phone.ino', 'chess_renphone.ino', 'chat_renphone.ino'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
wf = '.github/workflows/build-firmware.yml'
w = open(wf, encoding='utf-8').read() if os.path.exists(wf) else None

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: jangkar '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

did = False
if 'chessEnter' in s:
    print("App Catur sudah terdaftar di phone.ino, dilewati")
else:
    # 1. enum Screen
    s = rep(s, "              SCR_BACKUP };\nenum Orientation",
               "              SCR_BACKUP,\n              // Catur online (MQTT) -- lihat chess_renphone.ino\n              SCR_CHESS };\nenum Orientation",
            "enum Screen")
    # 2. forward declarations
    s = rep(s, "void chatEnter(); void chatExit(); void drawChat(LGFX_Sprite&); void chatTouch(int,int,bool,bool); void chatLoopPoll();\n",
               "void chatEnter(); void chatExit(); void drawChat(LGFX_Sprite&); void chatTouch(int,int,bool,bool); void chatLoopPoll();\n"
               "// ---- Catur online MQTT: forward declarations (chess_renphone.ino) ----\n"
               "void chessEnter(); void chessExit(); void drawChess(LGFX_Sprite&); void chessTouch(int,int,bool,bool); void chessLoopPoll();\n",
            "forward decl")
    # 3. tabel apps (ditambah di PALING AKHIR supaya indeks app lain tidak bergeser)
    s = rep(s, "AppDef apps[44] = {", "AppDef apps[45] = {", "apps[44]")
    s = rep(s, "#define APP_COUNT 44", "#define APP_COUNT 45", "APP_COUNT")
    s = rep(s, "  { \"Backup\",  'z', 0x357C, backupEnter, backupExit, drawBackup, backupTouch, SCR_BACKUP },\n};",
               "  { \"Backup\",  'z', 0x357C, backupEnter, backupExit, drawBackup, backupTouch, SCR_BACKUP },\n"
               "  // ---- Catur: catur online antar HP lewat broker MQTT (permintaan user) ----\n"
               "  { \"Catur\",   'q', 0, chessEnter,  chessExit,  drawChess,  chessTouch,  SCR_CHESS  },\n};",
            "apps[] entry")
    # 4. warna ikon
    s = rep(s, "  apps[42].color=0xE68A; // Buku - krem kecokelatan (kertas)\n",
               "  apps[42].color=0xE68A; // Buku - krem kecokelatan (kertas)\n  apps[44].color=0xFE60; // Catur - emas\n",
            "warna")
    # 5. glyph ikon 'q': bidak raja sederhana
    s = rep(s, "    case 'c': { // Chat: gelembung pesan + ekor + 3 titik\n",
               "    case 'q': { // Catur: siluet bidak raja (salib + badan + alas)\n"
               "      int hh=max(14,r*2), top=cy-hh/2;\n"
               "      s.fillRect(cx-1,top,3,6,ic); s.fillRect(cx-3,top+2,7,2,ic);\n"
               "      s.fillTriangle(cx,top+6,cx-r*2/3-2,cy+hh/2-3,cx+r*2/3+2,cy+hh/2-3,ic);\n"
               "      s.fillRect(cx-r*2/3-3,cy+hh/2-3,r*4/3+7,3,ic);\n"
               "      break;\n"
               "    }\n"
               "    case 'c': { // Chat: gelembung pesan + ekor + 3 titik\n",
            "ikon")
    # 6. kategori Home: Game
    s = rep(s, "    case SCR_MAZE: case SCR_INF: case SCR_NES: case SCR_DOOM: case SCR_PPU:\n",
               "    case SCR_MAZE: case SCR_INF: case SCR_NES: case SCR_DOOM: case SCR_PPU: case SCR_CHESS:\n",
            "kategori game")
    # 7. loop(): sinkronisasi dari task MQTT
    s = rep(s, "  chatLoopPoll(); // Chat: redraw kalau ada pesan/status baru dari task MQTT\n",
               "  chatLoopPoll(); // Chat: redraw kalau ada pesan/status baru dari task MQTT\n"
               "  chessLoopPoll(); // Catur: sinkron langkah lawan dari task MQTT\n",
            "loop")
    did = True

wdid = False
if w is None:
    print("PERINGATAN: workflow tidak ditemukan, lewati bagian workflow")
elif 'chess_renphone.ino' in w:
    print("Workflow sudah memuat chess_renphone.ino, dilewati")
else:
    w = rep(w, "          cp chat_renphone.ino build_sketch/\n",
               "          cp chat_renphone.ino build_sketch/\n          cp chess_renphone.ino build_sketch/\n",
            "workflow cp")
    wdid = True

if did: open('phone.ino', 'w', encoding='utf-8').write(s)
if wdid: open(wf, 'w', encoding='utf-8').write(w)
print("OK: app Catur %s, workflow %s" % ("terpasang" if did else "(sudah)", "diperbarui" if wdid else "(sudah/lewat)"))
