#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_doom_back_guard.py -- pengaman salah-pencet tombol Back / gestur keluar saat main DOOM.

Masalah: hitbox isBack() sengaja diperluas (BACK_BIGGER_V1) sampai tepi kiri layar & dasar layar,
sehingga menimpa zona jalan (maju/mundur) di kiri-bawah layar DOOM. Satu ketukan nyasar = langsung
keluar & progres level hilang. Gestur swipe-atas dari tepi bawah (navGoHome / App Switcher) juga
bisa kepicu tanpa sengaja.

Solusi (konfirmasi ketuk-2x):
 1) Selagi DOOM benar2 jalan, ketukan Back pertama TIDAK keluar -- hanya memunculkan toast
    "Ketuk Back lagi untuk keluar" (digambar lewat jalur overlay pita yg sama dgn tombol F/U/OK).
 2) Ketukan Back kedua dalam DOOM_BACK_CONFIRM_MS (2 dtk) baru keluar.
 3) Gestur swipe-atas dari tepi bawah (keluar cepat / App Switcher) di DOOM diperlakukan sama:
    pertama = toast, kedua = keluar ke Home.
 4) Layar "Memuat..." / layar error (task belum jalan) tetap bisa keluar langsung dgn 1 ketukan.

Menyentuh: doom_renphone.ino & phone.ino. Jalankan dari root repo. Idempotent & atomik
(kalau ada blok yg tdk ketemu, tidak ada file yg diubah).
"""
import sys, os

FILES = ["doom_renphone.ino", "phone.ino"]
for f in FILES:
    if not os.path.exists(f):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % f)

def load(p):
    t = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in t
    return t.replace("\r\n", "\n"), crlf

def save(p, t, crlf):
    if crlf:
        t = t.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(t)

doom, doom_crlf = load("doom_renphone.ino")
phone, phone_crlf = load("phone.ino")

def rep(text, old, new, label):
    if text.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik:\n%s\nDibatalkan, tidak ada yg diubah." % (label, old[:90]))
    return text.replace(old, new)

# Deklarasi state guard HARUS ada sebelum doomEnter() (dipakai di sana) -> ditaruh dekat doomBusy.
DECL = (
"\n// DOOM_BACK_GUARD_V1 (decl): konfirmasi keluar ketuk-2x. 0 = tidak armed; selain itu = batas waktu (millis)\n"
"// sampai ketukan Back kedua dianggap konfirmasi. Dibaca juga oleh doomBandHook() (core 1) utk toast.\n"
"#define DOOM_BACK_CONFIRM_MS 2000\n"
"static volatile uint32_t doomBackArmUntil = 0;\n"
"static inline bool doomBackArmed(){\n"
"  uint32_t u = doomBackArmUntil;\n"
"  return u != 0 && (int32_t)(u - millis()) > 0;\n"
"}\n")
# blok lama (versi pertama patch ini) yg salah tempat -- dipakai utk mode perbaikan
OLD_DECL = (
"\n// DOOM_BACK_GUARD_V1: konfirmasi keluar ketuk-2x. 0 = tidak armed; selain itu = batas waktu (millis)\n"
"// sampai ketukan Back kedua dianggap konfirmasi. Dibaca juga oleh doomBandHook() (core 1) utk toast.\n"
"#define DOOM_BACK_CONFIRM_MS 2000\n"
"static volatile uint32_t doomBackArmUntil = 0;\n"
"static inline bool doomBackArmed(){\n"
"  uint32_t u = doomBackArmUntil;\n"
"  return u != 0 && (int32_t)(u - millis()) > 0;\n"
"}\n")
ANCHOR = "bool doomIsBusy(){ return doomBusy; }\n"

has_d = "DOOM_BACK_GUARD_V1" in doom
has_p = "DOOM_BACK_GUARD_V1" in phone
if has_d and has_p:
    if "DOOM_BACK_GUARD_V1 (decl)" in doom:
        print("[skip] sudah dipatch"); sys.exit(0)
    # MODE PERBAIKAN: patch versi lama terpasang, deklarasi salah tempat (error build: 'doomBackArmUntil' was not declared)
    doom = rep(doom, OLD_DECL, "", "repair/hapus-lama")
    doom = rep(doom, ANCHOR, ANCHOR + DECL, "repair/pasang-baru")
    save("doom_renphone.ino", doom, doom_crlf)
    print("[ok] diperbaiki: deklarasi DOOM_BACK_GUARD_V1 dipindah ke atas doomEnter()")
    sys.exit(0)
if has_d != has_p:
    sys.exit("ERROR: patch separuh terpasang (hanya salah satu file). Periksa manual.")

# =====================================================================
# doom_renphone.ino
# =====================================================================

# 1) struct overlay: tambah flag toast + kapasitas jadi 5
doom = rep(doom,
"struct DoomOvl { int x, y, w, h; uint16_t* pix; uint16_t key; };\n"
"static DoomOvl doomOvl[4];\n",
"struct DoomOvl { int x, y, w, h; uint16_t* pix; uint16_t key; bool toast; };   // DOOM_BACK_GUARD_V1: +toast\n"
"static DoomOvl doomOvl[5];\n",
"doom/struct")
doom = rep(doom, ANCHOR, ANCHOR + DECL, "doom/decl")

# 2) painter: kind 2 = toast
doom = rep(doom,
"  } else {                             // tombol pause\n"
"    sp.fillRoundRect(0,0,32,28,4,0x4208); sp.drawRoundRect(0,0,32,28,4,TFT_BLACK);\n"
"    sp.fillRect(10,6,4,16,TFT_WHITE); sp.fillRect(18,6,4,16,TFT_WHITE);\n"
"  }\n",
"  } else if(kind == 2){                // DOOM_BACK_GUARD_V1: toast konfirmasi keluar\n"
"    int w = sp.width(), h = sp.height();\n"
"    sp.fillRoundRect(0,0,w,h,6,0x2104); sp.drawRoundRect(0,0,w,h,6,TFT_WHITE);\n"
"    sp.setTextColor(TFT_WHITE); sp.setTextSize(1);\n"
"    int tw = sp.textWidth(label); sp.setCursor((w-tw)/2, (h-8)/2); sp.print(label);\n"
"  } else {                             // tombol pause\n"
"    sp.fillRoundRect(0,0,32,28,4,0x4208); sp.drawRoundRect(0,0,32,28,4,TFT_BLACK);\n"
"    sp.fillRect(10,6,4,16,TFT_WHITE); sp.fillRect(18,6,4,16,TFT_WHITE);\n"
"  }\n",
"doom/paint")

# 3) doomOvlAdd: batas array + init flag
doom = rep(doom,
"  if(doomOvlN >= 4) return;\n",
"  if(doomOvlN >= 5) return;\n",
"doom/add-limit")
doom = rep(doom,
"  o.x=x; o.y=y; o.w=w; o.h=h; o.pix=pix; o.key=key;\n",
"  o.x=x; o.y=y; o.w=w; o.h=h; o.pix=pix; o.key=key; o.toast=false;\n",
"doom/add-init")

# 4) doomOvlBuild: tambah toast (lebar menyesuaikan teks, di tengah-atas layar)
doom = rep(doom,
"  doomOvlAdd(doomPauseX()-16, doomPauseY()-14, 33, 29, 1, 0,  \"\",   0);\n"
"}\n",
"  doomOvlAdd(doomPauseX()-16, doomPauseY()-14, 33, 29, 1, 0,  \"\",   0);\n"
"  // DOOM_BACK_GUARD_V1: toast -- tinggi 22, lebar ~ teks 28 huruf @6px + padding; hanya DITEMPEL saat armed.\n"
"  { int tw = 196, th = 22;\n"
"    doomOvlAdd((SCR_W-tw)/2, 8, tw, th, 2, 0, \"Ketuk Back lagi untuk keluar\", 0);\n"
"    if(doomOvlN > 0) doomOvl[doomOvlN-1].toast = true; }\n"
"}\n",
"doom/build")

# 5) band hook: lewati toast kalau tidak armed
doom = rep(doom,
"    const DoomOvl& o = doomOvl[k];\n"
"    int y0 = max(o.y, py), y1 = min(o.y+o.h, py+h);\n",
"    const DoomOvl& o = doomOvl[k];\n"
"    if(o.toast && !doomBackArmed()) continue;   // DOOM_BACK_GUARD_V1\n"
"    int y0 = max(o.y, py), y1 = min(o.y+o.h, py+h);\n",
"doom/hook")

# 6) fungsi guard (setelah doomReleaseAll) + pakai di doomTouchSlot
doom = rep(doom,
"static void doomReleaseAll(){\n"
"  doomSlotMask[0]=doomSlotMask[1]=0;\n"
"  doomApplyMasks();\n"
"}\n",
"static void doomReleaseAll(){\n"
"  doomSlotMask[0]=doomSlotMask[1]=0;\n"
"  doomApplyMasks();\n"
"}\n"
"\n"
"// DOOM_BACK_GUARD_V1: gerbang keluar. true = BOLEH keluar sekarang.\n"
"//  - task Doom belum jalan (\"Memuat...\" / layar error) -> true langsung (tdk ada progres yg bisa hilang)\n"
"//  - sedang main & belum armed -> arm, munculkan toast, return false\n"
"//  - sedang main & sudah armed (ketukan ke-2 dlm DOOM_BACK_CONFIRM_MS) -> true\n"
"// Dipanggil dari doomTouchSlot() (tombol Back) & dari gestur swipe-atas di phone.ino.\n"
"bool doomExitGuard(){\n"
"  if(!doomPlaying){ doomBackArmUntil = 0; return true; }\n"
"  if(doomBackArmed()){ doomBackArmUntil = 0; return true; }\n"
"  doomBackArmUntil = millis() + DOOM_BACK_CONFIRM_MS;\n"
"  if(doomBackArmUntil == 0) doomBackArmUntil = 1;   // jaga2 millis() pas == 0 (wrap)\n"
"  doomReleaseAll();                                 // lepas semua tombol: tangan yg nyasar jgn bikin karakter lari terus\n"
"  vibWarn();\n"
"  return false;\n"
"}\n",
"doom/guard")

doom = rep(doom,
"  if(isNew && isBack(x,y)){ navBack(); return; } // keluar app -> doomExit() lewat appOnExit()\n",
"  if(isNew && isBack(x,y)){ if(doomExitGuard()) navBack(); return; } // DOOM_BACK_GUARD_V1: ketuk 2x utk keluar -> doomExit() lewat appOnExit()\n",
"doom/touch")

# 7) reset state tiap masuk app (jangan bawa armed lama)
doom = rep(doom,
"  doomStopRequested = false;\n"
"  // DOOM_RACE_FIX_V1: gambar \"Memuat...\" SEKALI",
"  doomStopRequested = false;\n"
"  doomBackArmUntil = 0;   // DOOM_BACK_GUARD_V1\n"
"  // DOOM_RACE_FIX_V1: gambar \"Memuat...\" SEKALI",
"doom/enter-reset")

# =====================================================================
# phone.ino
# =====================================================================

# prototipe
phone = rep(phone,
"bool doomIsBusy();    // DOOM_RACE_FIX_V1:",
"bool doomExitGuard(); // DOOM_BACK_GUARD_V1: true = boleh keluar (ketuk/gestur ke-2); false = baru toast konfirmasi\n"
"bool doomIsBusy();    // DOOM_RACE_FIX_V1:",
"phone/proto")

# helper gestur, tepat sebelum dragCloseAllowed()
phone = rep(phone,
"bool dragCloseAllowed(){\n",
"// DOOM_BACK_GUARD_V1: gestur swipe-atas dari tepi bawah (keluar cepat / App Switcher) selagi DOOM jalan.\n"
"// true = gestur sudah ditangani di sini (pemanggil cukup menyelesaikan gestur & return).\n"
"// Ketukan/gestur pertama -> toast; yg kedua dlm 2 dtk -> pulang ke Home. App Switcher sengaja TIDAK\n"
"// dibuka selagi Doom jalan (core 0 dilarang menggambar -- lihat DOOM_RACE_FIX_V1).\n"
"static bool doomGestureExit(){\n"
"  if(curScreen()!=SCR_DOOM || !doomIsBusy()) return false;\n"
"  if(doomExitGuard()) navGoHome();\n"
"  return true;\n"
"}\n"
"\n"
"bool dragCloseAllowed(){\n",
"phone/helper")

# cabang 'ditahan' (App Switcher) di dalam blok touched
phone = rep(phone,
"      if(millis()-gTouchStartMs>=APPSWITCH_HOLD_MS){\n"
"        vibExit();\n"
"        openAppSwitcher(); gGestureDone=true; exitArmed=false;\n",
"      if(millis()-gTouchStartMs>=APPSWITCH_HOLD_MS){\n"
"        if(doomGestureExit()){ gGestureDone=true; exitArmed=false; wasTouched=touched; delay(1); return; }   // DOOM_BACK_GUARD_V1\n"
"        vibExit();\n"
"        openAppSwitcher(); gGestureDone=true; exitArmed=false;\n",
"phone/hold")

# blok rilis (keputusan final)
phone = rep(phone,
"    exitArmed=false;\n"
"    vibExit(); // v82: getar keluar app",
"    exitArmed=false;\n"
"    if(doomGestureExit()){ gGestureDone=true; wasTouched=touched; delay(1); return; }   // DOOM_BACK_GUARD_V1\n"
"    vibExit(); // v82: getar keluar app",
"phone/release")

save("doom_renphone.ino", doom, doom_crlf)
save("phone.ino", phone, phone_crlf)
print("[ok] DOOM_BACK_GUARD_V1 terpasang di doom_renphone.ino & phone.ino")
