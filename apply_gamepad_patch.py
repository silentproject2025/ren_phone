#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_gamepad_patch.py -- pasang fitur GAMEPAD WebSocket (HP lain jadi controller NES/DOOM)
#
# Jalankan dari folder repo (yang ada phone.ino + remote_renphone.ino + gamepad_renphone.ino):
#     python3 apply_gamepad_patch.py
#
# Yang diubah:
#   remote_renphone.ino    <- rcAllowedPublic()/rcLanAllowedPublic(), rute /pad + WebSocket, padLoopPoll()
#   nes_input_renphone.ino <- osd_getinput(): newState |= rcPadMask
#   doom_renphone.ino      <- RenPhoneDoomInput::update(): cur[i] = raw[i] || bit rcPadMask
#   build-firmware.yml     <- cp gamepad_renphone.ino + lib install "WebSockets"
#
# Idempoten: bagian yang sudah terpasang dilewati. Kalau ada anchor yang tidak
# ketemu persis 1x, TIDAK ADA file yang diubah (semua-atau-tidak-sama-sekali).
import sys, os

NEED = ['gamepad_renphone.ino', 'remote_renphone.ino', 'nes_input_renphone.ino', 'doom_renphone.ino']
for f in NEED:
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

files = {}
def load(p):
    if p not in files:
        files[p] = open(p, encoding='utf-8').read()
    return files[p]

changed = set()
def rep(path, old, new, what, marker):
    s = load(path)
    if marker in s:
        print("  (lewati) %s: %s sudah terpasang" % (path, what)); return
    n = s.count(old)
    if n != 1:
        print("ERROR: anchor '%s' di %s ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, path, n))
        sys.exit(1)
    files[path] = s.replace(old, new)
    changed.add(path)
    print("  ok: %s: %s" % (path, what))

# ---------------- remote_renphone.ino ----------------
R = 'remote_renphone.ino'
rep(R,
    "static bool rcAllowed() {\n"
    "  rcLoadCfg();\n"
    "  if (rcApUp) {\n"
    "    IPAddress li = webServer.client().localIP();\n"
    "    if (li == WiFi.softAPIP()) return true;\n"
    "  }\n"
    "  return rcCfg.allowLan;\n"
    "}\n",
    "static bool rcAllowed() {\n"
    "  rcLoadCfg();\n"
    "  if (rcApUp) {\n"
    "    IPAddress li = webServer.client().localIP();\n"
    "    if (li == WiFi.softAPIP()) return true;\n"
    "  }\n"
    "  return rcCfg.allowLan;\n"
    "}\n"
    "// ---- dipakai gamepad_renphone.ino (HP lain jadi gamepad) ----\n"
    "bool rcAllowedPublic()    { return rcAllowed(); }\n"
    "bool rcLanAllowedPublic() { rcLoadCfg(); return rcCfg.allowLan; }\n",
    "rcAllowedPublic", "rcAllowedPublic")

rep(R,
    "bool rcApIsUp() { return rcApUp; }",
    "void padRegisterRoutes(); void padLoopPoll(); // gamepad_renphone.ino\n"
    "bool rcApIsUp() { return rcApUp; }",
    "forward decl pad", "void padRegisterRoutes(); void padLoopPoll();")

rep(R,
    "  webServer.on(\"/rc/wst\",  HTTP_GET, rcHandleWifiStatus);\n}",
    "  webServer.on(\"/rc/wst\",  HTTP_GET, rcHandleWifiStatus);\n"
    "  padRegisterRoutes(); // Gamepad: /pad + WebSocket :81\n}",
    "padRegisterRoutes()", "padRegisterRoutes(); // Gamepad")

rep(R,
    "void rcLoopPoll() {\n  unsigned long now = millis();\n  rcAnnounceIp(now);\n",
    "void rcLoopPoll() {\n  unsigned long now = millis();\n  rcAnnounceIp(now);\n"
    "  padLoopPoll(); // Gamepad: WebSocket + watchdog tombol (jalan terus, juga selagi NES/DOOM main)\n",
    "padLoopPoll()", "padLoopPoll(); // Gamepad")

# ---------------- nes_input_renphone.ino ----------------
N = 'nes_input_renphone.ino'
rep(N,
    "extern volatile bool nesExitRequested; // dari nes_app_renphone.ino\n",
    "extern volatile bool nesExitRequested; // dari nes_app_renphone.ino\n"
    "extern volatile uint8_t rcPadMask;     // gamepad_renphone.ino: tombol dari HP lain (bit sama dgn NB_*)\n",
    "extern rcPadMask", "extern volatile uint8_t rcPadMask")

rep(N,
    "  uint8_t newState = nesBtnState; // salin sekali, hindari race dgn task lain\n",
    "  uint8_t newState = nesBtnState | rcPadMask; // salin sekali, hindari race dgn task lain (+ gamepad WebSocket)\n",
    "osd_getinput OR rcPadMask", "nesBtnState | rcPadMask")

# ---------------- doom_renphone.ino ----------------
D = 'doom_renphone.ino'
rep(D,
    "class RenPhoneDoomInput : public IDoomInput {",
    "extern volatile uint8_t rcPadMask; // gamepad_renphone.ino: bit i = DBTN_i (UP,DOWN,LEFT,RIGHT,FIRE,USE,START,SELECT)\n"
    "class RenPhoneDoomInput : public IDoomInput {",
    "extern rcPadMask", "extern volatile uint8_t rcPadMask")

rep(D,
    "  void update() override { for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i]; } }",
    "  void update() override { uint8_t pad=rcPadMask; for(int i=0;i<DBTN_COUNT;i++){ prev[i]=cur[i]; cur[i]=raw[i] || ((pad>>i)&1); } }",
    "update() OR rcPadMask", "uint8_t pad=rcPadMask")

# ---------------- workflow CI ----------------
wf = '.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    s = load(wf)
    if 'gamepad_renphone.ino' not in s:
        a = "          cp remote_renphone.ino build_sketch/\n"
        if s.count(a) == 1:
            files[wf] = s.replace(a, a + "          cp gamepad_renphone.ino build_sketch/\n"); changed.add(wf)
            print("  ok: %s: cp gamepad_renphone.ino" % wf)
        else:
            print("PERINGATAN: tambahkan 'cp gamepad_renphone.ino build_sketch/' ke workflow sendiri")
    s = load(wf)
    if 'lib install "WebSockets"' not in s:
        b = "          arduino-cli lib install \"lvgl\"\n"
        if s.count(b) == 1:
            files[wf] = s.replace(b, b + "          arduino-cli lib install \"WebSockets\"\n"); changed.add(wf)
            print("  ok: %s: lib install WebSockets" % wf)
        else:
            print("PERINGATAN: tambahkan 'arduino-cli lib install \"WebSockets\"' ke workflow sendiri")
else:
    print("PERINGATAN: workflow tidak ditemukan; pastikan gamepad_renphone.ino ikut ke-copy & library WebSockets terpasang")

for p in changed:
    open(p, 'w', encoding='utf-8').write(files[p])
print("OK: gamepad terpasang (%d file diubah)" % len(changed) if changed else "OK: tidak ada perubahan (sudah terpasang)")
if changed:
    print("Buka di HP lain:  http://192.168.4.1/pad   (setelah AP Remote nyala)")
