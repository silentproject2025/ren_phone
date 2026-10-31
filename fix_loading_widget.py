#!/usr/bin/env python3
# 1) Loading ring seragam & halus untuk semua layar loading (Trivia, APOD, NASA/NEO/EPIC, foto EPIC, galeri NASA)
# 2) Widget baterai Home: ujung arc membulat (modern)
# Jalankan dari folder repo:  python3 fix_loading_widget.py
import io, sys
F="phone.ino"
s=io.open(F,encoding="utf-8",errors="surrogateescape",newline="").read()
if "v129: loading ring" in s: print("sudah diterapkan"); sys.exit(0)

def rep(old,new):
    global s
    if s.count(old)!=1: print("pola tidak unik/ketemu (%d): %s"%(s.count(old),old[:70])); sys.exit(1)
    s=s.replace(old,new)

def replace_func(head,body):
    global s
    a=s.find(head)
    if a<0 or s.count(head)!=1: print("fungsi tidak ketemu/unik:",head); sys.exit(1)
    b=s.find("\n}\n",a)
    s=s[:a]+body+s[b+3:]

RING='''// v129: loading ring seragam -- panjang busur "bernapas" (memanjang-memendek dgn smoothstep), ekor
// memudar ke arah belakang, titik di kepala, teks berdenyut halus. Semua murni fungsi waktu (tanpa state).
void lrArc(LGFX_Sprite& s,int cx,int cy,int ri,int ro,float a0,float len,uint16_t col){
  if(len<=0.5f) return;
  a0=fmodf(a0,360.0f); if(a0<0.0f) a0+=360.0f;
  float a1=a0+len;
  if(a1<=360.0f) s.fillArc(cx,cy,ri,ro,a0,a1,col);
  else { s.fillArc(cx,cy,ri,ro,a0,360.0f,col); s.fillArc(cx,cy,ri,ro,0.0f,a1-360.0f,col); }
}
void drawLoadingRing(LGFX_Sprite& s,int cx,int cy,const char* label){
  const unsigned long ms=millis();
  float t=fmodf((float)ms,1600.0f)/1600.0f;
  float k=(t<0.5f)?t*2.0f:(2.0f-t*2.0f);
  k=k*k*(3.0f-2.0f*k);                                         // smoothstep 0..1..0
  float sweep=50.0f+220.0f*k;
  float head=fmodf((float)ms,1000.0f)/1000.0f*360.0f+sweep*0.5f;
  float st=head-sweep;
  const int ri=20, ro=26;
  uint16_t acc=T().accent2, trk=T().divider;
  lrArc(s,cx,cy,ri,ro,0.0f,360.0f,trk);                        // cincin dasar
  lrArc(s,cx,cy,ri,ro,st,sweep*0.5f,blend565(trk,acc,110));    // ekor paling redup
  lrArc(s,cx,cy,ri,ro,st+sweep*0.5f,sweep*0.3f,blend565(trk,acc,190));
  lrArc(s,cx,cy,ri,ro,st+sweep*0.8f,sweep*0.2f,acc);           // kepala terang
  float hr=head*0.0174532925f;
  s.fillCircle(cx+(int)lroundf(cosf(hr)*23.0f),cy+(int)lroundf(sinf(hr)*23.0f),3,acc);
  uint8_t pa=(uint8_t)(110.0f+110.0f*sinf((float)ms*0.004f)); // teks berdenyut halus
  s.setTextColor(blend565(T().subtext,T().text,pa)); s.setTextSize(1);
  int tw=s.textWidth(label);
  s.setCursor(cx-tw/2,cy+40); s.print(label);
}
'''
rep("void drawTriviaLoading(LGFX_Sprite& s){", RING+"\nvoid drawTriviaLoading(LGFX_Sprite& s){")
replace_func("void drawTriviaLoading(LGFX_Sprite& s){",
 'void drawTriviaLoading(LGFX_Sprite& s){\n  drawLoadingRing(s,SCR_W/2,SCR_H/2-6,"Mengambil soal dari server...");\n}\n')
replace_func("void drawApodLoading(LGFX_Sprite& s){",
 'void drawApodLoading(LGFX_Sprite& s){\n  drawLoadingRing(s,SCR_W/2,SCR_H/2-6,"Mengambil data APOD dari NASA...");\n}\n')
replace_func("void drawNasaLoading(LGFX_Sprite& s, const char* label){",
 'void drawNasaLoading(LGFX_Sprite& s, const char* label){\n  drawLoadingRing(s,SCR_W/2,SCR_H/2-6,label);\n}\n')

rep('    const char* m="Memuat citra...";\n    s.setCursor(SCR_W/2-(int)strlen(m)*3,imgTop+imgH/2-4); s.print(m);',
    '    drawLoadingRing(s,SCR_W/2,imgTop+imgH/2-6,"Memuat citra...");')
rep('    const char* m="Memuat gambar...";\n    s.setCursor(SCR_W/2-(int)strlen(m)*3,imgTop+imgH/2-4); s.print(m);',
    '    drawLoadingRing(s,SCR_W/2,imgTop+imgH/2-6,"Memuat gambar...");')
rep("    if(imgLibPage==ILIB_LOADING) diTickProgress(); // v96\n",
    "    if(imgLibPage==ILIB_LOADING) diTickProgress(); // v96\n"
    "    if(curScreen()==SCR_EPIC && epicPhotoLoading){ needRedraw=true; }       // v129: ring foto EPIC beranimasi\n"
    "    if(curScreen()==SCR_NASAIMG && imgLibPhotoLoading){ needRedraw=true; }  // v129: ring galeri NASA beranimasi\n")

# widget baterai: ujung arc membulat
rep("      else { s.fillArc(cx,cy,ri,ro,270.0f,360.0f,bc); s.fillArc(cx,cy,ri,ro,0.0f,a1-360.0f,bc); }\n    }\n",
    "      else { s.fillArc(cx,cy,ri,ro,270.0f,360.0f,bc); s.fillArc(cx,cy,ri,ro,0.0f,a1-360.0f,bc); }\n"
    "      if(battPercent<100){                                   // v129: ujung arc membulat\n"
    "        int cr=(ro-ri)/2, rm=(ri+ro)/2; float er=a1*0.0174532925f;\n"
    "        s.fillCircle(cx,cy-rm,cr,bc);\n"
    "        s.fillCircle(cx+(int)lroundf(cosf(er)*rm),cy+(int)lroundf(sinf(er)*rm),cr,bc);\n"
    "      }\n    }\n")
io.open(F,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
print("OK: loading ring baru + ujung arc baterai membulat")
