#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_remote_patch.py -- pasang fitur REMOTE CONTROL (AP + web) ke phone.ino
#
# Jalankan dari folder repo (yang ada phone.ino + remote_renphone.ino):
#     python3 apply_remote_patch.py
#
# Idempoten: kalau sudah terpasang, dilewati. Kalau ada anchor yang tidak
# ketemu persis 1x, TIDAK ADA yang diubah (semua-atau-tidak-sama-sekali).
import sys, os

if not os.path.exists('remote_renphone.ino'):
    print("ERROR: remote_renphone.ino tidak ada di folder ini"); sys.exit(1)
if not os.path.exists('phone.ino'):
    print("ERROR: phone.ino tidak ada di folder ini"); sys.exit(1)

s = open('phone.ino', encoding='utf-8').read()
if 'SCR_REMOTE' in s:
    print("Remote Control sudah terpasang di phone.ino, dilewati")
    s_done = True
else:
    s_done = False

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n))
        sys.exit(1)
    return src.replace(old, new)

if not s_done:
    # 1. enum Screen
    s = rep(s, "SCR_HOMEEDIT };\nenum Orientation",
               "SCR_HOMEEDIT,\n              // Remote Control (AP + web) -- lihat remote_renphone.ino\n              SCR_REMOTE };\nenum Orientation",
            "enum Screen")

    # 2. forward declaration
    s = rep(s, "void homeEditPointerTick(bool,bool,int,int);\n",
               "void homeEditPointerTick(bool,bool,int,int);\n\n"
               "// ---- remote_renphone.ino: Remote Control (AP + web server) ----\n"
               "void remoteEnter(); void remoteExit(); void drawRemote(LGFX_Sprite&); void remoteTouch(int,int,bool,bool);\n"
               "void remotePointerTick(bool,bool,int,int);\n"
               "void rcLoopPoll(); void rcRegisterRoutes(); bool rcApIsUp(); bool rcInputPoll(int*,int*); void rcFrameMark();\n",
            "forward decl")

    # 3. tabel apps[] (entri baru ditaruh PALING AKHIR supaya indeks lama tidak bergeser)
    s = rep(s, "AppDef apps[38] = {", "AppDef apps[39] = {", "apps[38]")
    s = rep(s, "#define APP_COUNT 38", "#define APP_COUNT 39", "APP_COUNT")
    s = rep(s, "homeEditEnter, homeEditExit, drawHomeEdit, homeEditTouch, SCR_HOMEEDIT },\n};",
               "homeEditEnter, homeEditExit, drawHomeEdit, homeEditTouch, SCR_HOMEEDIT },\n"
               "  // ---- Remote Control: kontrol penuh dari HP/laptop lewat WiFi AP sendiri ----\n"
               "  { \"Remote\", 'r', 0, remoteEnter, remoteExit, drawRemote, remoteTouch, SCR_REMOTE },\n};",
            "apps entry")

    # 4. warna ikon
    s = rep(s, "  apps[36].color=0x8C71; // About Phone\n",
               "  apps[36].color=0x8C71; // About Phone\n  apps[38].color=0x2DFA; // Remote - cyan\n",
            "initAppColors")

    # 5. ikon (layar + kursor)
    s = rep(s, "    case 'i': { // About Phone\n",
               "    case 'r': { // Remote: monitor + kursor\n"
               "      int mw=max(14,r*3/2), mh=max(10,r), mx=cx-mw/2, my=cy-mh/2-2;\n"
               "      s.drawRoundRect(mx,my,mw,mh,2,ic); s.drawRoundRect(mx+1,my+1,mw-2,mh-2,2,ic);\n"
               "      s.fillRect(cx-1,my+mh,3,3,ic); s.fillRect(cx-mw/4,my+mh+3,max(4,mw/2),2,ic);\n"
               "      s.fillTriangle(cx-mw/6,cy-mh/3,cx+mw/6,cy+1,cx-mw/16,cy+mh/5,ic);\n"
               "      break;\n"
               "    }\n"
               "    case 'i': { // About Phone\n",
            "drawAppIcon")

    # 6. rute web
    s = rep(s, "  webServer.on(\"/apikey_save\", HTTP_POST, handleApiKeySavePost);\n  webServer.begin();",
               "  webServer.on(\"/apikey_save\", HTTP_POST, handleApiKeySavePost);\n  rcRegisterRoutes(); // Remote Control: /remote, /rc/*\n  webServer.begin();",
            "startWebServer")

    # 7. loop: web server juga jalan kalau cuma AP yang hidup + poll remote
    s = rep(s, "  if(wifiConnected && webServerRunning){\n    webServer.handleClient();\n  }\n",
               "  if((wifiConnected || rcApIsUp()) && webServerRunning){\n    webServer.handleClient();\n  }\n"
               "  rcLoopPoll(); // Remote Control: aksi tombol, status penonton, keep-awake\n",
            "loop handleClient")

    # 8. injeksi sentuh
    s = rep(s, "  uint32_t now=millis();\n\n  if(latched && now-lastTouchMs>=TOUCH_LATCH_MS) latched=false;",
               "  uint32_t now=millis();\n\n"
               "  { int rx=0,ry=0; // Remote Control: sentuhan dari web menang atas touchscreen fisik\n"
               "    if(rcInputPoll(&rx,&ry)){ tp.x=rx; tp.y=ry; tp.size=64; return true; } }\n\n"
               "  if(latched && now-lastTouchMs>=TOUCH_LATCH_MS) latched=false;",
            "getInputPoint")

    # 9. penanda frame baru
    s = rep(s, "void push(){ unsigned long t0=micros(); canvas.pushSprite(0,0); nyxPushLastUs=micros()-t0; }",
               "void push(){ unsigned long t0=micros(); canvas.pushSprite(0,0); nyxPushLastUs=micros()-t0; rcFrameMark(); }",
            "push()")
    s = rep(s, "  canvas.pushSprite(0,0);\n  display.clearClipRect();\n}",
               "  canvas.pushSprite(0,0);\n  display.clearClipRect();\n  rcFrameMark();\n}",
            "renderHomeBandFrame")

    # 10. tap-saat-lepas + scroll di app Remote
    s = rep(s, "    if(curScreen()==SCR_HOMEEDIT) homeEditPointerTick(touched,wasTouched,tx,ty);",
               "    if(curScreen()==SCR_REMOTE) remotePointerTick(touched,wasTouched,tx,ty);        // Remote: geser daftar + ketuk saat lepas\n"
               "    if(curScreen()==SCR_HOMEEDIT) homeEditPointerTick(touched,wasTouched,tx,ty);",
            "pointer tick")

# 11. workflow CI (build hanya meng-copy .ino yang terdaftar)
wf = '.github/workflows/build-firmware.yml'
w = None
if os.path.exists(wf):
    w = open(wf, encoding='utf-8').read()
    if 'remote_renphone.ino' in w:
        w = None
    else:
        a = "          cp about_renphone.ino build_sketch/\n"
        if w.count(a) == 1:
            w = w.replace(a, a + "          cp remote_renphone.ino build_sketch/\n")
        else:
            print("PERINGATAN: tambahkan 'cp remote_renphone.ino build_sketch/' ke workflow sendiri")
            w = None
else:
    print("PERINGATAN: workflow tidak ditemukan; pastikan remote_renphone.ino ikut ke-copy saat build")

if not s_done:
    open('phone.ino', 'w', encoding='utf-8').write(s)
if w:
    open(wf, 'w', encoding='utf-8').write(w)
print("OK: Remote Control terpasang" if not s_done else "OK: tidak ada perubahan di phone.ino")
