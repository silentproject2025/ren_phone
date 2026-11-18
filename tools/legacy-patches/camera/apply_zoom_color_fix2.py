import sys, os
P = "avi_renphone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: jalankan dari root repo ren_phone")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
old = ("  if (!aviJpeg->openRAM((uint8_t*)data, (int)size, aviZoomDraw)) return false;\n"
       "  aviJpeg->setPixelType(RGB565_LITTLE_ENDIAN);\n")
new = ("  if (!aviJpeg->openRAM((uint8_t*)data, (int)size, aviZoomDraw)) return false;\n"
       "  aviJpeg->setPixelType(RGB565_BIG_ENDIAN);\n")
if new in s: sys.exit("[skip] sudah terpasang")
if s.count(old) != 1: sys.exit("ERROR: pola tidak cocok")
s = s.replace(old, new)
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] zoom fix terpasang")
