#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_deadzone_v4.py -- tangani kalibrasi touch yg "jalan sendiri" akibat
ghost touch (calibrateTouch() membaca sentuhan mentah sendiri, jadi filter
getInputPoint() v2/v3 tidak melindunginya).

Yg ditambahkan di loadOrRunCalibration():
  1. Log Serial "[touch] kalibrasi NVS: a,b,c,d,e,f,g,h" tiap boot, supaya
     kelihatan data kalibrasi yg tersimpan masuk akal atau sampah
     (sampah = angkanya hampir sama semua / berulang).
  2. Saklar TOUCH_FORCE_DEFAULT_CAL (default 0 = perilaku lama, tidak berubah):
     kalau diisi 1, SELALU pakai TOUCH_CAL_VALUES dan lewati wizard + data NVS.
     Dipakai selama ghost belum hilang (mis. sampai layar diganti).

Jalankan dari root repo. Idempotent. Butuh apply_touch_ghostcal.py terpasang.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

if "TOUCH_CAL_V4" in s:
    print("[skip] v4 sudah terpasang")
    sys.exit(0)

old1 = "extern int brightness;\nvoid loadOrRunCalibration(){\n"
new1 = (
    "// TOUCH_CAL_V4 (apply_touch_deadzone_v4.py)\n"
    "#define TOUCH_FORCE_DEFAULT_CAL 0   // 1 = selalu pakai TOUCH_CAL_VALUES, lewati wizard & data NVS\n"
    "#define TOUCH_CAL_VALUES { 239, 3926, 233, 265, 3856, 3897, 3714, 308 }\n"
    + old1
)
old2 = (
    "  bool done=p.getBool(\"done\",false);\n"
    "  if(done){\n"
    "    uint16_t d[8]; p.getBytes(\"data\",d,sizeof(d));\n"
    "    display.setTouchCalibrate(d);\n"
)
new2 = (
    "  bool done=p.getBool(\"done\",false);\n"
    "  if(done){\n"
    "    uint16_t dn[8]; p.getBytes(\"data\",dn,sizeof(dn));\n"
    "    Serial.printf(\"[touch] kalibrasi NVS: %u,%u,%u,%u,%u,%u,%u,%u\\n\",\n"
    "                  dn[0],dn[1],dn[2],dn[3],dn[4],dn[5],dn[6],dn[7]);\n"
    "  } else {\n"
    "    Serial.println(\"[touch] belum ada kalibrasi tersimpan di NVS\");\n"
    "  }\n"
    "#if TOUCH_FORCE_DEFAULT_CAL\n"
    "  {\n"
    "    uint16_t dd[8] = TOUCH_CAL_VALUES;\n"
    "    display.setTouchCalibrate(dd);\n"
    "    Serial.println(\"[touch] TOUCH_FORCE_DEFAULT_CAL=1 -> kalibrasi default dipakai, wizard dilewati\");\n"
    "    p.end();\n"
    "    return;\n"
    "  }\n"
    "#endif\n"
    "  if(done){\n"
    "    uint16_t d[8]; p.getBytes(\"data\",d,sizeof(d));\n"
    "    display.setTouchCalibrate(d);\n"
)
for o in (old1, old2):
    if s.count(o) != 1:
        sys.exit("ERROR: pola tidak cocok (apply_touch_ghostcal.py terpasang? phone.ino sudah diubah?):\n" + o[:70])
s = s.replace(old1, new1).replace(old2, new2)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] v4 terpasang: log kalibrasi NVS + saklar TOUCH_FORCE_DEFAULT_CAL (default 0)")
