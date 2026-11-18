#!/usr/bin/env python3
# Percantik unlock geser naik: parallax Home, garis tepi lembut, easing + durasi lebih halus.
# Jalankan SETELAH fix_unlock_slide.py, dari folder repo:  python3 fix_unlock_anim.py
import io, sys
F="phone.ino"
s=io.open(F,encoding="utf-8",errors="surrogateescape",newline="").read()
if "v127: unlock anim" in s: print("sudah diterapkan"); sys.exit(0)
if "// v126: unlock geser" not in s: print("jalankan fix_unlock_slide.py dulu"); sys.exit(1)

a=s.find("void unlockFollowDraw(float u){")
b=s.find("void unlockFollowFinish(float u0){")
if a<0 or b<a: print("unlockFollowDraw tidak ketemu"); sys.exit(1)
new_draw='''void unlockFollowDraw(float u){   // v126: unlock geser; v127: unlock anim (parallax + garis tepi)
  if(u<0.0f) u=0.0f; if(u>1.0f) u=1.0f;
  int off=(int)lroundf((float)SCR_H*u);
  if(off<0) off=0; if(off>SCR_H) off=SCR_H;
  if(ufHasPrev && off==ufPX0) return;                  // ufPX0 dipakai sbg "offset terakhir"
  const int edge=SCR_H-off;                            // posisi tepi bawah lock screen
  const int par=(int)lroundf((float)edge*0.16f);       // parallax: Home mulai agak turun, naik ke posisi asli
  display.startWrite();
  if(off<SCR_H){ display.setClipRect(0,0,SCR_W,edge); transShot.pushSprite(0,-off); }          // lock screen (naik)
  if(off>0){ display.setClipRect(0,edge,SCR_W,off); canvas.pushSprite(0,par); }                // Home (naik pelan, parallax)
  display.clearClipRect();
  if(off>0 && off<SCR_H){                              // garis tepi lembut: kesan lapisan terangkat
    display.fillRect(0,edge,SCR_W,1,blend565(T().bg,TFT_WHITE,70));
    display.fillRect(0,edge+1,SCR_W,2,blend565(T().bg,TFT_BLACK,150));
  }
  display.endWrite();
  ufPX0=off; ufHasPrev=true;
}
'''
s=s[:a]+new_draw+s[b:]

old="  unsigned long MS=(unsigned long)(90.0f+200.0f*(1.0f-u0));\n  unsigned long t0=millis();\n  for(;;){\n    float k=(float)(millis()-t0)/(float)MS; if(k>1.0f) k=1.0f;\n    float e=1.0f-(1.0f-k)*(1.0f-k);\n    unlockFollowDraw(u0+(1.0f-u0)*e);"
new="  unsigned long MS=(unsigned long)(150.0f+240.0f*(1.0f-u0));   // v127: sedikit lebih panjang\n  unsigned long t0=millis();\n  for(;;){\n    float k=(float)(millis()-t0)/(float)MS; if(k>1.0f) k=1.0f;\n    float ik=1.0f-k; float e=1.0f-ik*ik*ik;                    // v127: ease-out cubic, mendarat halus\n    unlockFollowDraw(u0+(1.0f-u0)*e);"
if s.count(old)!=1: print("pola Finish tidak unik:",s.count(old)); sys.exit(1)
s=s.replace(old,new)
io.open(F,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
print("OK: animasi unlock diperhalus")
