#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_chat_patch.py -- daftarkan app "Chat" (MQTT) ke phone.ino + workflow.
#
# Jalankan dari folder repo (yang ada phone.ino + chat_renphone.ino):
#     python3 apply_chat_patch.py
#
# Idempoten & semua-atau-tidak-sama-sekali, sama seperti patch lain di repo ini.
# Tidak bergantung pada apply_ota_channel_patch.py (boleh urutan apa saja).
import sys, os

if not os.path.exists('phone.ino') or not os.path.exists('chat_renphone.ino'):
    print("ERROR: phone.ino / chat_renphone.ino tidak ada di folder ini"); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
wf = '.github/workflows/build-firmware.yml'
w = open(wf, encoding='utf-8').read() if os.path.exists(wf) else None

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

did = False
if 'chatEnter' in s:
    print("App Chat sudah terdaftar di phone.ino, dilewati")
else:
    # 1. enum Screen
    s = rep(s, "              SCR_REMOTE };\nenum Orientation",
               "              SCR_REMOTE,\n              // Chat MQTT -- lihat chat_renphone.ino\n              SCR_CHAT };\nenum Orientation",
            "enum Screen")
    # 2. forward declarations
    s = rep(s, "void drawUpdate(LGFX_Sprite&); void updTouch(int,int,bool,bool);\n",
               "void drawUpdate(LGFX_Sprite&); void updTouch(int,int,bool,bool);\n"
               "// ---- Chat MQTT: forward declarations (chat_renphone.ino) ----\n"
               "void chatEnter(); void chatExit(); void drawChat(LGFX_Sprite&); void chatTouch(int,int,bool,bool); void chatLoopPoll();\n",
            "forward decl")
    # 3. tabel apps
    s = rep(s, "AppDef apps[39] = {", "AppDef apps[40] = {", "apps[39]")
    s = rep(s, "#define APP_COUNT 39", "#define APP_COUNT 40", "APP_COUNT")
    s = rep(s, "  { \"Remote\", 'r', 0, remoteEnter, remoteExit, drawRemote, remoteTouch, SCR_REMOTE },\n};",
               "  { \"Remote\", 'r', 0, remoteEnter, remoteExit, drawRemote, remoteTouch, SCR_REMOTE },\n"
               "  { \"Chat\",   'c', 0, chatEnter,   chatExit,   drawChat,   chatTouch,   SCR_CHAT   },\n};",
            "apps[] entry")
    # 4. warna ikon
    s = rep(s, "  apps[38].color=0x2DFA; // Remote - cyan\n",
               "  apps[38].color=0x2DFA; // Remote - cyan\n  apps[39].color=0x34DF; // Chat - biru lembut\n",
            "warna")
    # 5. glyph ikon 'c': gelembung pesan + ekor + 3 titik
    s = rep(s, "    case 'r': { // Remote: monitor + kursor\n",
               "    case 'c': { // Chat: gelembung pesan + ekor + 3 titik\n"
               "      int bw=max(16,r*3/2), bh=max(11,r), bx=cx-bw/2, by=cy-bh/2-2;\n"
               "      s.drawRoundRect(bx,by,bw,bh,4,ic); s.drawRoundRect(bx+1,by+1,bw-2,bh-2,3,ic);\n"
               "      s.fillTriangle(bx+bw/4,by+bh-1,bx+bw/4+6,by+bh-1,bx+bw/4-1,by+bh+4,ic);\n"
               "      int dy=by+bh/2; for(int i=-1;i<=1;i++) s.fillCircle(cx+i*5,dy,1,ic);\n"
               "      break;\n"
               "    }\n"
               "    case 'r': { // Remote: monitor + kursor\n",
            "ikon")
    # 6. loop(): redraw saat ada pesan baru (di luar if-else per layar)
    s = rep(s, "  rcLoopPoll(); // Remote Control: aksi tombol, status penonton, keep-awake\n",
               "  chatLoopPoll(); // Chat: redraw kalau ada pesan/status baru dari task MQTT\n"
               "  rcLoopPoll(); // Remote Control: aksi tombol, status penonton, keep-awake\n",
            "loop")
    did = True

wdid = False
if w is None:
    print("PERINGATAN: workflow tidak ditemukan, lewati bagian workflow")
elif 'chat_renphone.ino' in w:
    print("Workflow sudah memuat chat_renphone.ino, dilewati")
else:
    w = rep(w, '          arduino-cli lib install "WebSockets"\n',
               '          arduino-cli lib install "WebSockets"\n          arduino-cli lib install "PubSubClient"\n',
            "workflow lib")
    w = rep(w, "          cp remote_renphone.ino build_sketch/\n",
               "          cp remote_renphone.ino build_sketch/\n          cp chat_renphone.ino build_sketch/\n",
            "workflow cp")
    wdid = True

if did: open('phone.ino', 'w', encoding='utf-8').write(s)
if wdid: open(wf, 'w', encoding='utf-8').write(w)
print("OK: app Chat %s, workflow %s" % ("terpasang" if did else "(sudah)", "diperbarui" if wdid else "(sudah/lewat)"))
