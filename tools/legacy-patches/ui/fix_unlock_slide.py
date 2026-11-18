#!/usr/bin/env python3
# Unlock dari lock screen: ganti "jendela zoom" jadi lock screen yang GESER NAIK mengikuti jari,
# membuka Home yang diam di bawahnya (seperti iOS). Jalankan dari folder repo:
#   python3 fix_unlock_slide.py
import io, sys
F="phone.ino"
s=io.open(F,encoding="utf-8",errors="surrogateescape",newline="").read()
if "// v126: unlock geser" in s: print("sudah diterapkan"); sys.exit(0)

a=s.find("void unlockFollowDraw(float u){")
b=s.find("void unlockFollowFinish(float u0){")
if a<0 or b<a: print("unlockFollowDraw tidak ketemu"); sys.exit(1)
new_draw='''void unlockFollowDraw(float u){   // v126: unlock geser -- lock screen naik, Home diam di bawahnya
  if(u<0.0f) u=0.0f; if(u>1.0f) u=1.0f;
  int off=(int)lroundf((float)SCR_H*u);
  if(off<0) off=0; if(off>SCR_H) off=SCR_H;
  if(ufHasPrev && off==ufPX0) return;                  // ufPX0 dipakai sbg "offset terakhir"
  display.startWrite();
  if(off<SCR_H){ display.setClipRect(0,0,SCR_W,SCR_H-off); transShot.pushSprite(0,-off); }   // lock screen (naik)
  if(off>0){ display.setClipRect(0,SCR_H-off,SCR_W,off); canvas.pushSprite(0,0); }           // Home (terbuka dari bawah)
  display.clearClipRect();
  display.endWrite();
  ufPX0=off; ufHasPrev=true;
}
'''
s=s[:a]+new_draw+s[b:]

o1="float p=(float)dy/((float)SCR_H*0.55f);"
n1="float p=(float)dy/((float)SCR_H*0.80f);"
o2="unlockP>=0.30f || el<220UL"
n2="unlockP>=0.20f || el<220UL"
for o,n in((o1,n1),(o2,n2)):
    if s.count(o)!=1: print("pola tidak unik:",o,s.count(o)); sys.exit(1)
    s=s.replace(o,n)
io.open(F,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
print("OK: unlock sekarang geser naik (lock screen menggeser, Home di bawah)")
