#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_deadzone_v2.py -- filter ghost touch (kaca retak) yg lebih lengkap,
pengganti blok getInputPoint() dari apply_touch_deadzone.py (v1).

Yg baru dibanding v1:
  1. Strip mati di tepi kiri/kanan/atas/bawah (TOUCH_EDGE_L/R/T/B), bukan cuma
     kotak pojok kiri-bawah.
  2. Titik di luar area layar (> TOUCH_OOR_PX) dibuang -- ghost resistif sering
     nyangkut di ekstrem (x/y negatif atau melebihi layar).
  3. Ambang tekanan minimum (TOUCH_MIN_Z, pakai tp.size). Default 0 = mati;
     isi setelah lihat angka z di log.
  4. Log ke Serial Monitor: "[touch] SENTUH x y z" tiap ada sentuhan baru dan
     "[touch] BLOKIR x y z" tiap titik dibuang -> kelihatan di mana ghost
     SEBENARNYA muncul. Matikan dgn TOUCH_DEBUG 0.

Jalankan dari root repo (folder yg ada phone.ino). Idempotent.
Butuh apply_touch_deadzone.py (v1) sudah terpasang.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

if "TOUCH_FILTER_V2" in s:
    print("[skip] filter sentuhan v2 sudah terpasang")
    sys.exit(0)

old = (
    "// ---- zona mati sentuh (apply_touch_deadzone.py): sementara, kaca retak di pojok kiri-bawah ----\n"
    "#define TOUCH_DEAD_W 40   // lebar zona mati (px), 0 = nonaktif\n"
    "#define TOUCH_DEAD_H 40   // tinggi zona mati (px)\n\n"
    "bool getInputPoint(lgfx::touch_point_t &tp){\n"
    "  if(!display.getTouch(&tp)) return false;\n"
    "  if(tp.x < TOUCH_DEAD_W && tp.y >= SCR_H - TOUCH_DEAD_H) return false;\n"
    "  return true;\n"
    "}\n"
)

new = r'''// ---- filter sentuhan v2 (apply_touch_deadzone_v2.py): sementara, kaca retak -> ghost touch ----
// TOUCH_FILTER_V2
// Semua nilai px dihitung di koordinat layar SAAT INI (ikut portrait/landscape).
#define TOUCH_DEAD_W   40   // kotak mati pojok kiri-bawah: lebar (px), 0 = nonaktif
#define TOUCH_DEAD_H   40   // kotak mati pojok kiri-bawah: tinggi (px)
#define TOUCH_EDGE_L   0    // strip mati tepi KIRI  (px), 0 = nonaktif
#define TOUCH_EDGE_R   0    // strip mati tepi KANAN (px), 0 = nonaktif
#define TOUCH_EDGE_T   0    // strip mati tepi ATAS  (px), 0 = nonaktif
#define TOUCH_EDGE_B   0    // strip mati tepi BAWAH (px), 0 = nonaktif
#define TOUCH_MIN_Z    0    // tekanan minimum (tp.size), 0 = nonaktif -- isi setelah lihat log
#define TOUCH_OOR_PX   10   // titik di luar layar lebih dari ini dianggap ghost
#define TOUCH_DEBUG    1    // 1 = cetak log [touch] ke Serial Monitor, 0 = diam

static bool touchBlocked(const lgfx::touch_point_t &t){
  int x=(int)t.x, y=(int)t.y;
  if(x < -TOUCH_OOR_PX || y < -TOUCH_OOR_PX || x >= SCR_W+TOUCH_OOR_PX || y >= SCR_H+TOUCH_OOR_PX) return true;
  if(TOUCH_DEAD_W>0 && x<TOUCH_DEAD_W && y>=SCR_H-TOUCH_DEAD_H) return true;
  if(TOUCH_EDGE_L>0 && x<TOUCH_EDGE_L) return true;
  if(TOUCH_EDGE_R>0 && x>=SCR_W-TOUCH_EDGE_R) return true;
  if(TOUCH_EDGE_T>0 && y<TOUCH_EDGE_T) return true;
  if(TOUCH_EDGE_B>0 && y>=SCR_H-TOUCH_EDGE_B) return true;
  if(TOUCH_MIN_Z>0 && (int)t.size<TOUCH_MIN_Z) return true;
  return false;
}

static void touchLog(const char* tag, const lgfx::touch_point_t &t){
#if TOUCH_DEBUG
  static uint32_t lastMs=0;
  uint32_t now=millis();
  if(now-lastMs<150) return;   // jangan banjiri Serial kalau ghost nempel terus
  lastMs=now;
  Serial.printf("[touch] %s x=%d y=%d z=%d (layar %dx%d)\n", tag, (int)t.x, (int)t.y, (int)t.size, SCR_W, SCR_H);
#else
  (void)tag; (void)t;
#endif
}

bool getInputPoint(lgfx::touch_point_t &tp){
  static bool prevDown=false;
  if(!display.getTouch(&tp)){ prevDown=false; return false; }
  if(touchBlocked(tp)){ touchLog("BLOKIR", tp); prevDown=false; return false; }
  if(!prevDown) touchLog("SENTUH", tp);   // cuma pas sentuhan baru mulai
  prevDown=true;
  return true;
}
'''

if s.count(old) != 1:
    sys.exit("ERROR: blok v1 (apply_touch_deadzone.py) tidak cocok / belum terpasang. "
             "Jalankan apply_touch_deadzone.py dulu, atau cek apakah getInputPoint() sudah diubah patch lain.")

s = s.replace(old, new)
if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] filter sentuhan v2 terpasang (kotak 40x40 kiri-bawah + deteksi luar-layar + log Serial)")
