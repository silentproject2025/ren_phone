#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_spi_drive.py (SPI_DRIVE_V1) -- bantu SPI layar 80 MHz lebih stabil.

Datasheet ST7796S menetapkan siklus tulis SPI minimum 66 ns (~15 MHz). Clock 40/80 MHz di ESP32-S3 sudah
di atas spesifikasi, jadi 80 MHz sangat bergantung pada kualitas sinyal. Pin layar (SCLK 12, MOSI 11,
CS 10) sudah memakai jalur IO_MUX FSPI -> sudah yang terbaik. Yang bisa ditambah dari sisi firmware:
kekuatan drive GPIO. Patch ini menaikkan drive SCLK/MOSI/DC/CS ke maksimum (GPIO_DRIVE_CAP_3, ~40 mA)
HANYA saat mode SPI 80 MHz dipilih; mode 40 MHz tidak berubah sama sekali.

Jalankan dari root repo (folder yg ada phone.ino). Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "SPI_DRIVE_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: blok '%s' ketemu %d kali (harus 1). Dibatalkan, phone.ino tidak diubah." % (label, n))
    s = s.replace(old, new)

rep("void setup() {\n" if "void setup() {\n" in s else "void setup(){\n",
r'''// SPI_DRIVE_V1 (apply_spi_drive.py): drive GPIO maksimum utk jalur SPI layar saat mode 80 MHz.
#include "driver/gpio.h"
static void rpSpiDriveBoost(){
  if(SPI_OC_OPTIONS[spiOcIdx] < 80) return;   // 40 MHz (default) tdk disentuh
  const int pins[4] = {12, 11, 10, 2};        // SCLK, MOSI, CS, DC (lihat LGFX::LGFX)
  for(int i=0;i<4;i++) gpio_set_drive_capability((gpio_num_t)pins[i], GPIO_DRIVE_CAP_3);
  Serial.println("[spi] drive GPIO SCLK/MOSI/CS/DC = CAP_3 (80 MHz)");
}
''' + ("void setup() {\n" if "void setup() {\n" in s else "void setup(){\n"), "setup")

rep("  display.setSpiFreqMHz(SPI_OC_OPTIONS[spiOcIdx]);\n  display.init();\n",
    "  display.setSpiFreqMHz(SPI_OC_OPTIONS[spiOcIdx]);\n  display.init();\n  rpSpiDriveBoost();   // SPI_DRIVE_V1\n", "init")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: SPI_DRIVE_V1 terpasang di phone.ino")
