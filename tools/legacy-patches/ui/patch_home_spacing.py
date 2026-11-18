#!/usr/bin/env python3
"""
Patch tata letak widget Home (hanya phone.ino) -- ngatasi tampilan yang "berdempet":
  1. Kartu jam: angka jam terlalu besar -> menimpa sapaan ("Selamat Siang") di atas dan
     tanggal di bawah. Sekarang ukuran angka dihitung dari TINGGI kartu dan posisinya
     dipatok lewat garis dasar (baseline), bukan perkiraan metrik font.
  2. Landscape: kartu jam/baterai 70 -> 66 px, kartu musik 54 -> 48 px, supaya ada
     jarak lega sebelum dock (dulu hampir menempel).

Jalankan di root repo:   python patch_home_spacing.py
Idempotent. Cuma S3 (phone.ino) yang perlu dicompile ulang; CAM tidak perlu di-flash.
"""
import os
import sys

PH = "phone.ino"
if not os.path.exists(PH):
    sys.exit("[GAGAL] phone.ino tidak ketemu. Jalankan dari root repo.")
with open(PH, encoding="utf-8", newline="") as f:
    s = f.read()


def edit(name, old, new, marker):
    global s
    if marker in s:
        print(f"[skip ] {name}")
        return
    n = s.count(old)
    if n != 1:
        sys.exit(f"[GAGAL] {name}: anchor ditemukan {n}x (harus 1x). File kamu mungkin sudah beda dari versi yang saya baca.")
    s = s.replace(old, new)
    print(f"[ok   ] {name}")


edit("Landscape: kartu jam 66px",
     "r.h=homeIsLand()?70:80; r.w=homeIsLand()?188:SCR_W-20; return r; }",
     "r.h=homeIsLand()?66:80; r.w=homeIsLand()?188:SCR_W-20; return r; }",
     "r.h=homeIsLand()?66:80;")

edit("Landscape: kartu musik 48px",
     "if(homeIsLand()){ r.y=STATUS_H+28+70+6; r.h=54; }",
     "if(homeIsLand()){ r.y=STATUS_H+28+66+6; r.h=48; }",
     "r.y=STATUS_H+28+66+6; r.h=48;")

edit("Landscape: kartu baterai 66px",
     "if(homeIsLand()){ r.x=206; r.y=STATUS_H+28; r.w=SCR_W-216; r.h=70; }",
     "if(homeIsLand()){ r.x=206; r.y=STATUS_H+28; r.w=SCR_W-216; r.h=66; }",
     "r.w=SCR_W-216; r.h=66; }")

edit("Kartu jam: sapaan naik 2px", 
     "uiText(s,x+14,y+10,ok?homeGreeting(t.tm_hour):\"Halo\",T().accent,false,true);",
     "uiText(s,x+14,y+8,ok?homeGreeting(t.tm_hour):\"Halo\",T().accent,false,true);",
     "uiText(s,x+14,y+8,ok?homeGreeting")

edit("Kartu jam: ukuran & posisi angka dari tinggi kartu + baseline",
     "  int sz=4;\n"
     "  for(;sz>=2;sz--){ s.setTextSize(sz); if(s.textWidth(tb)<=w-28) break; }\n"
     "  s.setTextSize(sz);\n"
     "  int fh=s.fontHeight();\n"
     "  int cyc=y+h/2+4;\n"
     "  s.setTextColor(T().text);\n"
     "  s.setCursor(x+12,cyc-(fh*48)/100); // angka jam ~ di tengah-bawah kartu (perkiraan metrik font; geser di sini kalau perlu)\n"
     "  s.print(tb);\n",
     "  // ukuran angka dihitung dari TINGGI kartu (sisakan ruang sapaan di atas & tanggal di bawah),\n"
     "  // lalu dikecilkan sampai muat lebar. Tinggi angka ~14px per satuan ukuran.\n"
     "  float sz=(float)(h-38)/14.0f; if(sz>4.0f) sz=4.0f; sz=floorf(sz*4.0f)/4.0f; if(sz<1.5f) sz=1.5f;\n"
     "  s.setTextSize(sz);\n"
     "  while(sz>1.5f && s.textWidth(tb)>w-28){ sz-=0.25f; s.setTextSize(sz); }\n"
     "  s.setTextColor(T().text);\n"
     "  s.setTextDatum(lgfx::textdatum_t::baseline_left); // dipatok ke garis dasar angka, bukan perkiraan metrik font\n"
     "  s.drawString(tb,x+14,y+h-18);\n"
     "  s.setTextDatum(lgfx::textdatum_t::top_left);\n",
     "textdatum_t::baseline_left")

with open(PH, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("\nSelesai. Compile ulang firmware S3 (phone.ino). CAM tidak perlu di-flash.")
