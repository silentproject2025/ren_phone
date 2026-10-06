#!/usr/bin/env python3
"""
Patch otomatis app "Musik" ke repo ren_phone.
Pakai (di folder repo, tempat phone.ino berada):
    python apply_musik_patch.py
Idempotent: aman dijalankan dua kali. Gagal keras kalau anchor tidak ketemu
persis satu kali (artinya phone.ino sudah beda dari versi yg dipatch).
"""
import sys, os

def patch(path, edits):
    s = open(path, encoding="utf-8").read()
    for name, old, new, marker in edits:
        if marker in s:
            print(f"[skip ] {name} (sudah ada)")
            continue
        n = s.count(old)
        if n != 1:
            sys.exit(f"[GAGAL] {name}: anchor ditemukan {n}x (harus 1x) di {path}")
        s = s.replace(old, new)
        print(f"[ok   ] {name}")
    open(path, "w", encoding="utf-8").write(s)

phone = [
 ("enum Screen: SCR_MUSIC",
  "              SCR_WAKE };",
  "              SCR_WAKE,\n              // app baru \"Musik\" -- S3 master, ESP32-CAM co-prosesor BT A2DP,\n              // lihat file musicbt_renphone.ino + rplink.h.\n              SCR_MUSIC };",
  "SCR_MUSIC };"),
 ("forward declarations Musik",
  "void wakeBegin(); void wakeLoopPoll(); void wakeYieldMic(uint32_t holdMs);\n",
  "void wakeBegin(); void wakeLoopPoll(); void wakeYieldMic(uint32_t holdMs);\n"
  "// ---- Musik (S3 master -> ESP32-CAM A2DP) -- definisi di musicbt_renphone.ino ----\n"
  "void musicEnter(); void musicExit();\n"
  "void drawMusic(LGFX_Sprite&); void musicTouch(int,int,bool,bool);\n"
  "void musicLoopPoll();\n",
  "void musicLoopPoll();"),
 ("apps[] ukuran 32",
  "AppDef apps[31] = {", "AppDef apps[32] = {", "AppDef apps[32] = {"),
 ("entri app Musik",
  "  { \"Wake Word\",  'w', 0, wakeEnter,    wakeExit,     drawWake,         wakeTouch,     SCR_WAKE },\n",
  "  { \"Wake Word\",  'w', 0, wakeEnter,    wakeExit,     drawWake,         wakeTouch,     SCR_WAKE },\n"
  "  // ---- Musik: pemutar MP3 SD -> TWS lewat ESP32-CAM (kabel UART) ----\n"
  "  { \"Musik\",      'm', 0, musicEnter,   musicExit,    drawMusic,        musicTouch,    SCR_MUSIC },\n",
  "SCR_MUSIC },\n};"),
 ("APP_COUNT 32", "#define APP_COUNT 31", "#define APP_COUNT 32", "#define APP_COUNT 32"),
 ("ikon 'm' (not nada)",
  "      s.fillRect(cx-cw/2-1, cy-rr/3+cw+max(2,rr/3), cw+2, 2, ic); // alas\n      break;\n    }\n",
  "      s.fillRect(cx-cw/2-1, cy-rr/3+cw+max(2,rr/3), cw+2, 2, ic); // alas\n      break;\n    }\n"
  "    case 'm': { // Musik: dua not berpalang\n"
  "      int rr=max(8,r-6);\n"
  "      int hx=max(3,rr/3);\n"
  "      int lx=cx-rr/2, rx=cx+rr/2, hy=cy+rr/2-2;\n"
  "      s.fillCircle(lx, hy, hx, ic);\n"
  "      s.fillCircle(rx, hy, hx, ic);\n"
  "      s.fillRect(lx+hx-1, cy-rr/2, 2, hy-(cy-rr/2), ic);\n"
  "      s.fillRect(rx+hx-1, cy-rr/2, 2, hy-(cy-rr/2), ic);\n"
  "      s.fillRect(lx+hx-1, cy-rr/2, rx-lx+2, 3, ic);\n"
  "      break;\n"
  "    }\n",
  "case 'm': { // Musik"),
 ("hook musicLoopPoll di loop()",
  "  wakeLoopPoll(); // v115: urus hasil task wake word (simpan template, aksi pas terdeteksi) -- murah kalau gak ada kerjaan\n",
  "  wakeLoopPoll(); // v115: urus hasil task wake word (simpan template, aksi pas terdeteksi) -- murah kalau gak ada kerjaan\n"
  "  musicLoopPoll(); // Musik: toast/redraw/simpan prefs -- no-op kalau app belum pernah dibuka\n",
  "musicLoopPoll(); // Musik"),
]
if not os.path.exists("phone.ino"):
    sys.exit("phone.ino tidak ketemu di folder ini. Jalankan dari root repo ren_phone.")
patch("phone.ino", phone)

wf = ".github/workflows/build-firmware.yml"
if os.path.exists(wf):
    patch(wf, [("workflow: salin file Musik",
        "          cp wakeword_renphone.ino build_sketch/\n",
        "          cp wakeword_renphone.ino build_sketch/\n          cp musicbt_renphone.ino build_sketch/\n          cp rplink.h build_sketch/\n",
        "cp musicbt_renphone.ino build_sketch/")])
else:
    print("[info ] workflow tidak ada, lewati")

for f in ("musicbt_renphone.ino", "rplink.h"):
    if not os.path.exists(f):
        print(f"[PERHATIAN] {f} belum ada di folder ini -- salin dari bundle")
print("Selesai.")
