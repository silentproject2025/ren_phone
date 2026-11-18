#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_call_patch.py -- daftarkan app "Telepon" (SIP + LAN) ke phone.ino, workflow, rplink.h & firmware CAM.
#
# Jalankan dari folder repo (ada phone.ino + call_renphone.ino):
#     python3 apply_call_patch.py
# Idempoten & semua-atau-tidak-sama-sekali (anchor harus ketemu TEPAT 1x), pola sama dgn apply_chat_patch.py.
import sys, os

for f in ('phone.ino', 'call_renphone.ino', 'rplink.h'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

def rd(p): return open(p, encoding='utf-8').read()
def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

out = {}   # path -> isi baru (ditulis di akhir, kalau semua anchor beres)

s = rd('phone.ino')
if 'callLoopPoll' in s:
    print("App Telepon sudah terdaftar di phone.ino, dilewati")
else:
    s = rep(s, "              SCR_CHAT };\nenum Orientation",
               "              SCR_CHAT,\n              // Telepon SIP/LAN -- lihat call_renphone.ino\n              SCR_CALL };\nenum Orientation", "enum Screen")
    s = rep(s, "void chatEnter(); void chatExit(); void drawChat(LGFX_Sprite&); void chatTouch(int,int,bool,bool); void chatLoopPoll();\n",
               "void chatEnter(); void chatExit(); void drawChat(LGFX_Sprite&); void chatTouch(int,int,bool,bool); void chatLoopPoll();\n"
               "// ---- Telepon SIP/LAN: forward declarations (call_renphone.ino) ----\n"
               "void callEnter(); void callExit(); void drawCall(LGFX_Sprite&); void callTouch(int,int,bool,bool); void callLoopPoll();\n"
               "bool callOverlayActive(); void drawCallOverlay(LGFX_Sprite&); void callOverlayTouch(bool,int,int);\n", "forward decl")
    s = rep(s, "AppDef apps[40] = {", "AppDef apps[41] = {", "apps[40]")
    s = rep(s, "#define APP_COUNT 40", "#define APP_COUNT 41", "APP_COUNT")
    s = rep(s, "  { \"Chat\",   'c', 0, chatEnter,   chatExit,   drawChat,   chatTouch,   SCR_CHAT   },\n};",
               "  { \"Chat\",   'c', 0, chatEnter,   chatExit,   drawChat,   chatTouch,   SCR_CHAT   },\n"
               "  { \"Telepon\", 'p', 0, callEnter,   callExit,   drawCall,   callTouch,   SCR_CALL   },\n};", "apps[] entry")
    s = rep(s, "  apps[39].color=0x34DF; // Chat - biru lembut\n",
               "  apps[39].color=0x34DF; // Chat - biru lembut\n  apps[40].color=0x2E8B; // Telepon - hijau\n", "warna")
    s = rep(s, "    case 'c': { // Chat: gelembung pesan + ekor + 3 titik\n",
               "    case 'p': { // Telepon: gagang telepon\n"
               "      int rr=max(8,r-6);\n"
               "      s.fillRoundRect(cx-rr,cy-rr/3,rr*2,rr*2/3,rr/3,ic);\n"
               "      s.fillRoundRect(cx-rr,cy-rr/3,rr*2/3,rr*4/3,rr/3,ic);\n"
               "      s.fillRoundRect(cx+rr/3,cy-rr/3,rr*2/3,rr*4/3,rr/3,ic);\n"
               "      break;\n"
               "    }\n"
               "    case 'c': { // Chat: gelembung pesan + ekor + 3 titik\n", "ikon")
    s = rep(s, "  chatLoopPoll(); // Chat: redraw kalau ada pesan/status baru dari task MQTT\n",
               "  chatLoopPoll(); // Chat: redraw kalau ada pesan/status baru dari task MQTT\n"
               "  callLoopPoll(); // Telepon: panggilan masuk, dering, notifikasi, siaga (no-op kalau belum dipakai)\n", "loop poll")
    # overlay panggilan masuk: gambar
    s = rep(s, "  if(appSwitcherOpen){\n    drawAppSwitcher(canvas);",
               "  if(callOverlayActive()){\n    drawCallOverlay(canvas); // panggilan masuk / panggilan saat terkunci menimpa semua layar\n  } else if(appSwitcherOpen){\n    drawAppSwitcher(canvas);", "render overlay")
    # overlay panggilan masuk: sentuh (sebelum blok App Switcher di loop)
    s = rep(s, "  if(appSwitcherOpen){\n    switcherAnimTick();",
               "  if(callOverlayActive()){\n"
               "    callOverlayTouch(newT,tx,ty);\n"
               "    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }\n"
               "    wasTouched=touched; delay(1); return;\n"
               "  }\n"
               "  if(appSwitcherOpen){\n    switcherAnimTick();", "loop overlay")
    out['phone.ino'] = s

wf = '.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    w = rd(wf)
    if 'call_renphone.ino' in w: print("Workflow sudah memuat call_renphone.ino, dilewati")
    else:
        out[wf] = rep(w, "          cp chat_renphone.ino build_sketch/\n",
                         "          cp chat_renphone.ino build_sketch/\n          cp call_renphone.ino build_sketch/\n", "workflow cp")
else:
    print("PERINGATAN: workflow tidak ditemukan, lewati")

# rplink.h (HARUS identik di dua sisi)
for rp in ('rplink.h', 'cam_bt_coprocessor/rplink.h'):
    if not os.path.exists(rp): continue
    r = rd(rp)
    if 'RP_PREFILL' in r: print("%s sudah punya RP_PREFILL, dilewati" % rp)
    else:
        out[rp] = rep(r, "#ifndef RP_PING\n",
            "#define RP_PREFILL  'Y'   // payload: [u16 LE byte PCM sebelum mulai diputar; 0 = bawaan]. Dipakai Telepon (latensi rendah)\n#ifndef RP_PING\n", "rplink")

cam = 'cam_bt_coprocessor/cam_bt_coprocessor.ino'
if os.path.exists(cam):
    c = rd(cam)
    if 'RP_PREFILL' in c: print("CAM sudah menangani RP_PREFILL, dilewati")
    else:
        c = rep(c, "    case RP_STOP:\n      decErr = false;\n",
                   "    case RP_PREFILL:\n"
                   "      if (len >= 2) {\n"
                   "        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8);\n"
                   "        if (v == 0) v = PREFILL_BYTES;\n"
                   "        if (v < 4096) v = 4096;\n"
                   "        if (v > pcmRing.size / 2) v = pcmRing.size / 2;\n"
                   "        prefillBytes = v;\n"
                   "      }\n"
                   "      break;\n"
                   "    case RP_STOP:\n      decErr = false;\n", "CAM handleFrame")
        out[cam] = c

for p, txt in out.items():
    open(p, 'w', encoding='utf-8').write(txt)
print("OK: Telepon terpasang. File diubah: " + (", ".join(out.keys()) or "(tidak ada)"))
print("PENTING: firmware ESP32-CAM juga perlu di-flash ulang agar latensi suara rendah (opsional; tanpa itu tetap jalan, hanya +140 ms).")
