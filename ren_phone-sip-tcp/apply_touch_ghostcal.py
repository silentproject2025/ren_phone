import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: jalankan dari root repo")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf: s = s.replace("\r\n", "\n")
if "touchGhostAtBoot" in s:
    print("[skip] sudah terpasang"); sys.exit(0)
old1 = "extern int brightness;\nvoid loadOrRunCalibration(){\n"
new1 = '''// apply_touch_ghostcal.py: deteksi ghost touch saat boot
static bool touchGhostAtBoot(){
  delay(200);
  int hit = 0;
  lgfx::touch_point_t tp;
  for(int i=0;i<10;i++){ if(display.getTouch(&tp)) hit++; delay(30); }
  return hit >= 7;
}

''' + old1
old2 = "  } else {\n    // v45: kalibrasi pertama kali gambar LANGSUNG ke display"
new2 = '''  } else if(touchGhostAtBoot()){
    // ghost touch: lewati kalibrasi, pakai default (tidak disimpan ke NVS)
    static const uint16_t defCal[8] = { 239, 3926, 233, 265, 3856, 3897, 3714, 308 };
    uint16_t d[8]; memcpy(d, defCal, sizeof(d));
    display.setTouchCalibrate(d);
    Serial.println("[touch] ghost terdeteksi saat boot -> kalibrasi default");
  } else {
    // v45: kalibrasi pertama kali gambar LANGSUNG ke display'''
for o in (old1, old2):
    if s.count(o) != 1:
        sys.exit("ERROR: pola tidak cocok: " + o[:40])
s = s.replace(old1, new1).replace(old2, new2)
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] kalibrasi anti-ghost terpasang")
