# TOUCH_SDA_GPIO6_V1: pindahkan SDA sentuh FT6236 dari GPIO 15 ke GPIO 6 (SCL tetap 7).
# Jalankan dari root repo ren_phone:  py apply_touch_sda6.py
import sys, os
f = 'display_conf.h'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'TOUCH_SDA_GPIO6_V1' in s:
    print("Sudah terpasang, dilewati"); sys.exit(0)
old = "  #define RP_TOUCH_SDA        15\n"
new = "  #define RP_TOUCH_SDA        6            // TOUCH_SDA_GPIO6_V1 (sebelumnya 15)\n"
if s.count(old) != 1:
    print("ERROR: anchor RP_TOUCH_SDA ketemu %d kali" % s.count(old)); sys.exit(1)
s = s.replace(old, new)
s = s.replace("Bus ini (Wire, SDA 15 / SCL 7)", "Bus ini (Wire, SDA 6 / SCL 7)")
open(f, 'w', encoding='utf-8').write(s)
print("OK: sentuh FT6236 -> SDA=GPIO6, SCL=GPIO7")
