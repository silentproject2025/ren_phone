#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_touch_probe_retry.py (TOUCH_PROBE_V1) -- untuk reset CTP yg digabung ke reset LCD.
 1) touchBegin(): probe FT6236 diulang tiap 50 ms sampai 1,5 dtk (FT6236 butuh ~300 ms sejak reset),
    log Serial mencantumkan berapa kali coba & berapa ms.
 2) Clock I2C bisa diganti: RP_TOUCH_I2C_HZ (default 400000, diatur di display_conf.h atau -D...).
    mpuInit() dulu memaksa 400 kHz SETELAH touchBegin() -> sekarang ikut nilai yg sama.
 3) Pin MPU6050/INA219 otomatis ikut RP_TOUCH_SDA/SCL di build ST7796 (satu bus), jadi ganti pin
    cukup di display_conf.h.
Jalankan dari root repo (ada phone.ino & display_conf.h). Idempotent.
"""
import sys, os
for f in ("phone.ino", "display_conf.h"):
    if not os.path.exists(f):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % f)

def load(p):
    t = open(p, encoding="utf-8", newline="").read()
    c = "\r\n" in t
    return (t.replace("\r\n", "\n") if c else t), c
def save(p, t, c):
    open(p, "w", encoding="utf-8", newline="").write(t.replace("\n", "\r\n") if c else t)
def rep(t, old, new, name):
    if t.count(old) != 1:
        sys.exit("ERROR: blok '%s' tidak ketemu / tidak unik. Dibatalkan, tidak ada yg diubah." % name)
    return t.replace(old, new)

ph, pc = load("phone.ino"); dc, dcc = load("display_conf.h")
if "TOUCH_PROBE_V1" in ph:
    print("[skip] sudah dipatch"); sys.exit(0)

# display_conf.h
dc = rep(dc, "  #ifndef RP_TOUCH_ADDR\n  #define RP_TOUCH_ADDR       0x38\n  #endif\n",
  "  #ifndef RP_TOUCH_ADDR\n  #define RP_TOUCH_ADDR       0x38\n  #endif\n"
  "  // TOUCH_PROBE_V1: clock I2C (Hz). Sentuh tidak stabil/kabel panjang -> coba 100000 atau 200000.\n"
  "  #ifndef RP_TOUCH_I2C_HZ\n  #define RP_TOUCH_I2C_HZ     400000\n  #endif\n", "display_conf RP_TOUCH_ADDR")

# touchBegin
old = ("    Wire.setClock(400000);\n"
       "    Wire.beginTransmission(RP_TOUCH_ADDR);\n"
       "    _touchOk = (Wire.endTransmission()==0);\n"
       "    Serial.printf(\"[touch] FT6236 @0x%02X %s (SDA=%d SCL=%d)\\n\", RP_TOUCH_ADDR, _touchOk?\"TERDETEKSI\":\"TIDAK ADA\", RP_TOUCH_SDA, RP_TOUCH_SCL);\n")
new = ("    Wire.setClock(RP_TOUCH_I2C_HZ);\n"
       "    // TOUCH_PROBE_V1: reset CTP digabung ke reset LCD -> FT6236 butuh ~300 ms sejak reset. Ulangi probe (maks 1,5 dtk).\n"
       "    uint32_t _t0=millis(); int _try=0;\n"
       "    do {\n"
       "      Wire.beginTransmission(RP_TOUCH_ADDR);\n"
       "      _touchOk = (Wire.endTransmission()==0);\n"
       "      _try++;\n"
       "      if(_touchOk) break;\n"
       "      delay(50);\n"
       "    } while(millis()-_t0 < 1500);\n"
       "    Serial.printf(\"[touch] FT6236 @0x%02X %s (SDA=%d SCL=%d, coba %d, %lu ms, I2C %lu Hz)\\n\", RP_TOUCH_ADDR, _touchOk?\"TERDETEKSI\":\"TIDAK ADA\", RP_TOUCH_SDA, RP_TOUCH_SCL, _try, (unsigned long)(millis()-_t0), (unsigned long)RP_TOUCH_I2C_HZ);\n")
ph = rep(ph, old, new, "touchBegin")

# MPU pin + clock
ph = rep(ph, "#define MPU_SDA_PIN 15\n#define MPU_SCL_PIN 7\n",
  "// TOUCH_PROBE_V1: build ST7796 = satu bus I2C dgn FT6236 -> pin ikut RP_TOUCH_SDA/SCL\n"
  "#ifdef RP_TOUCH_SDA\n#define MPU_SDA_PIN RP_TOUCH_SDA\n#define MPU_SCL_PIN RP_TOUCH_SCL\n#else\n#define MPU_SDA_PIN 15\n#define MPU_SCL_PIN 7\n#endif\n", "MPU pin")
ph = rep(ph, "  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);\n  Wire.setClock(400000);\n",
  "  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);\n#ifdef RP_TOUCH_I2C_HZ\n  Wire.setClock(RP_TOUCH_I2C_HZ);   // TOUCH_PROBE_V1: satu clock utk seluruh bus\n#else\n  Wire.setClock(400000);\n#endif\n", "mpuInit clock")

save("phone.ino", ph, pc); save("display_conf.h", dc, dcc)
print("OK: probe sentuh diulang, clock I2C bisa diatur (RP_TOUCH_I2C_HZ), pin MPU ikut pin sentuh")
