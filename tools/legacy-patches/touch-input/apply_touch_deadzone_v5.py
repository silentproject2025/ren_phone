#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_deadzone_v5.py -- latch anti ghost touch yg "menyapu" sendiri.

Masalah: ghost mulai di kotak mati (diblok), lalu bergeser keluar kotak. Kode
lama menganggap titik di luar kotak sbg sentuhan BARU -> terbaca swipe/tap
-> app kebuka & tombol ketekan acak (mis. app Jam).

Perbaikan: begitu ada titik yg diblok (kotak/strip/di luar layar/tekanan
rendah), getInputPoint() masuk mode "latched" -> SEMUA sentuhan ditolak sampai
layar benar2 sepi (tanpa sentuhan sama sekali) selama TOUCH_LATCH_MS.
Log Serial: "[touch] TAHAN x y z" = sentuhan ditolak krn latch.

Konsekuensi: selama ghost masih aktif terus-menerus (jeda < TOUCH_LATCH_MS),
sentuhan jari juga ikut ditolak. Solusi bersihnya = ambang tekanan
TOUCH_MIN_Z (ghost terukur z ~36-42) -- isi setelah tahu z jari asli.

Jalankan dari root repo. Idempotent. Butuh apply_touch_deadzone_v2.py.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

if "TOUCH_LATCH_V5" in s:
    print("[skip] latch v5 sudah terpasang")
    sys.exit(0)

old = (
    "bool getInputPoint(lgfx::touch_point_t &tp){\n"
    "  static bool prevDown=false;\n"
    "  if(!display.getTouch(&tp)){ prevDown=false; return false; }\n"
    "  if(touchBlocked(tp)){ touchLog(\"BLOKIR\", tp); prevDown=false; return false; }\n"
    "  if(!prevDown) touchLog(\"SENTUH\", tp);   // cuma pas sentuhan baru mulai\n"
    "  prevDown=true;\n"
    "  return true;\n"
    "}\n"
)

new = r'''// TOUCH_LATCH_V5 (apply_touch_deadzone_v5.py)
#define TOUCH_LATCH_MS 150   // layar harus sepi sekian ms (sejak sentuhan terakhir) sebelum sentuhan diterima lagi setelah ghost

bool getInputPoint(lgfx::touch_point_t &tp){
  static bool prevDown=false;
  static bool latched=false;       // ghost baru saja kena blok -> tolak semua sentuhan
  static uint32_t lastTouchMs=0;   // waktu sampel sentuhan terakhir (apa pun)
  uint32_t now=millis();

  if(latched && now-lastTouchMs>=TOUCH_LATCH_MS) latched=false;   // sudah cukup sepi

  if(!display.getTouch(&tp)){ prevDown=false; return false; }
  lastTouchMs=now;

  if(touchBlocked(tp)){ touchLog("BLOKIR", tp); latched=true; prevDown=false; return false; }
  if(latched){ touchLog("TAHAN", tp); prevDown=false; return false; }

  if(!prevDown) touchLog("SENTUH", tp);   // cuma pas sentuhan baru mulai
  prevDown=true;
  return true;
}
'''

if s.count(old) != 1:
    sys.exit("ERROR: getInputPoint() v2 tidak cocok -- apply_touch_deadzone_v2.py terpasang? "
             "atau getInputPoint() sudah diubah patch lain.")
s = s.replace(old, new)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] latch v5 terpasang: ghost yg bergeser keluar kotak tetap diblok sampai layar sepi %d ms" % 150)
