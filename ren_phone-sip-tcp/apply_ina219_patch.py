#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_ina219_patch.py -- ganti pembacaan baterai ADC dengan INA219.
Jalankan dari root repo (ina219_renphone.ino harus sudah ada di folder yg sama).
Idempotent. Mengubah:
  phone.ino                        : hapus blok ADC + app Baterai lama (dipindah ke ina219_renphone.ino),
                                     pasang inaBegin() di setup, polling 250 ms di loop, forward declaration
  .github/workflows/build-firmware.yml : tambah 'cp ina219_renphone.ino build_sketch/'
"""
import sys, os

def load(p):
    s = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    return s.replace("\r\n", "\n"), crlf

def save(p, s, crlf):
    if crlf: s = s.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(s)

for need in ("phone.ino", "ina219_renphone.ino", ".github/workflows/build-firmware.yml"):
    if not os.path.exists(need):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % need)

# ---------------- phone.ino ----------------
s, crlf = load("phone.ino")
if "inaBegin();" in s:
    print("[skip] phone.ino sudah dipatch")
else:
    a = s.find("// =================================================================\n// BATERAI: voltage divider")
    b = s.find("// =============================================\n// APP: TRIVIA QUIZ")
    if a < 0 or b < 0 or a >= b:
        sys.exit("ERROR: blok BATERAI lama tidak ditemukan / urutan berubah.")
    seg = s[a:b]
    for k in ("analogRead(BATT_ADC_PIN)", "void battTouch(", "void drawBatteryApp("):
        if k not in seg:
            sys.exit("ERROR: blok tidak sesuai harapan (hilang: %s)." % k)
    stub = ("// =================================================================\n"
            "// BATERAI: sekarang dibaca dari sensor INA219 (lihat ina219_renphone.ino).\n"
            "// Di sini hanya variabel global yang dipakai seluruh UI. battUpdate(),\n"
            "// battEnter/battExit, drawBatteryApp dan battTouch dipindah ke ina219_renphone.ino.\n"
            "// ADC voltage divider (GPIO8) tidak dipakai lagi.\n"
            "// =================================================================\n"
            "float battVoltage = 3.7f;\n"
            "int   battPercent = 100;\n"
            "bool  battCharging = false; // diisi dari arus INA219 (negatif = mengisi)\n"
            "bool  battWarnedLow = false, battWarnedCritical = false;\n\n")
    s = s[:a] + stub + s[b:]

    old = ("  analogReadResolution(12);\n"
           "  analogSetAttenuation(ADC_11db);           // biar analogRead mentok di ~3.3V\n"
           "  battUpdate();\n")
    if s.count(old) != 1: sys.exit("ERROR: blok setup ADC tidak cocok.")
    s = s.replace(old, "  inaBegin();                               // INA219 (I2C bareng MPU6050, SDA=15 SCL=7)\n  battUpdate();\n")

    old = "    if(millis()-lastBatt>5000){ lastBatt=millis(); battUpdate(); needRedraw=true; }\n"
    if s.count(old) != 1: sys.exit("ERROR: baris loop baterai tidak cocok.")
    s = s.replace(old, "    if(millis()-lastBatt>250){ lastBatt=millis(); battUpdate(); if(inaWantRedraw()) needRedraw=true; }\n")

    old = "void drawBatteryApp(LGFX_Sprite&); void battTouch(int,int,bool,bool);\n"
    if s.count(old) != 1: sys.exit("ERROR: forward declaration baterai tidak cocok.")
    s = s.replace(old, old + "void battUpdate(); void inaBegin(); void inaPoll(); bool inaWantRedraw(); void inaSaveCounters(bool);\n")
    save("phone.ino", s, crlf)
    print("[ok] phone.ino dipatch (ADC dihapus, INA219 dipasang)")

# ---------------- workflow ----------------
w = ".github/workflows/build-firmware.yml"
s, crlf = load(w)
if "ina219_renphone.ino" in s:
    print("[skip] workflow sudah memuat ina219_renphone.ino")
else:
    old = "          cp about_renphone.ino build_sketch/\n"
    if s.count(old) != 1: sys.exit("ERROR: baris cp about_renphone.ino tidak ketemu di workflow.")
    s = s.replace(old, old + "          cp ina219_renphone.ino build_sketch/\n")
    save(w, s, crlf)
    print("[ok] workflow: ina219_renphone.ino ditambahkan ke build_sketch")
