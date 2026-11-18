#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_back_button_bigger.py -- tombol "Kembali" lebih mudah ditekan (terutama layar ST7796 3,5").
1) Build kapasitif (ST7796): pil digambar lebih besar (84x34, ILI9341 tetap 68x24).
2) Area ketuk (isBack) diperluas: ke kiri sampai tepi layar, ke kanan +14 px, ke bawah sampai
   dasar layar, ke atas +4 px (celah ke tombol di atasnya cuma 6 px, jangan sampai menimpa).
3) Home-indicator (app dgn swipe-back): area ketuk diperlebar dari 16 px ke 28 px tinggi, 160 px lebar.
Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "BACK_BIGGER_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR: blok tidak ketemu / tidak unik:\n" + old[:80] + "\nDibatalkan, tidak ada yg diubah.")
    s = s.replace(old, new)

rep("#define BACK_W 68\n#define BACK_H 24\n",
    "// BACK_BIGGER_V1 (apply_back_button_bigger.py)\n"
    "#if RP_TOUCH_CAPACITIVE\n#define BACK_W 84\n#define BACK_H 34\n#else\n#define BACK_W 68\n#define BACK_H 24\n#endif\n")

rep("  s.setCursor(bx+21,by+8); s.print(\"Kembali\");",
    "  s.setCursor(bx+(BACK_W>68?26:21),by+(BACK_H-8)/2); s.print(\"Kembali\");")

rep("  iosChevron(s,bx+11,cy,T().accent);",
    "  iosChevron(s,bx+(BACK_W>68?14:11),cy,T().accent);")

rep("  if(!kbVisible && backHidden()) return y>=SCR_H-16 && abs(x-SCR_W/2)<=60;   // cadangan: ketuk home indicator\n"
    "  return !kbVisible && x>=backX() && x<=backX()+BACK_W && y>=backY() && y<=backY()+BACK_H;",
    "  if(!kbVisible && backHidden()) return y>=SCR_H-28 && abs(x-SCR_W/2)<=80;   // cadangan: ketuk home indicator (diperlebar)\n"
    "  return !kbVisible && x>=0 && x<=backX()+BACK_W+14 && y>=backY()-4 && y<=SCR_H;   // hitbox diperluas (lihat BACK_BIGGER_V1)")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: tombol Kembali diperbesar + area ketuk diperluas")
