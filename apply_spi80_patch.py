import sys, os
if not os.path.exists('phone.ino'): print("ERROR: phone.ino tidak ada (jalankan dari root repo)"); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'SPI_OC_OPTIONS[3]' in s: print("Opsi 80MHz sudah terpasang, dilewati"); sys.exit(0)
def rep(src,old,new,what):
    if src.count(old)!=1: print("ERROR: anchor '%s' ketemu %d kali (harus 1). Pastikan apply_accretion_v2_polish_patch.py sudah jalan. Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)
s=rep(s,"const int SPI_OC_OPTIONS[2] = {40,60};   // ILI9488: 40 = aman (default), 60 = overclock (kalau layar noise/korup, balik ke 40)",
"const int SPI_OC_OPTIONS[3] = {40,60,80}; // ILI9488: 40 = aman (default); 60 & 80 = overclock. Kalau layar noise/garis/korup -> balik ke 40","opts")
s=rep(s,"for(int i=0;i<2;i++) if(SPI_OC_OPTIONS[i]==v) return i;","for(int i=0;i<3;i++) if(SPI_OC_OPTIONS[i]==v) return i;","load")
s=rep(s,"        int sw=108, sx=xr-sw, sy=ry+(rh-22)/2;\n        s.fillRoundRect(sx,sy,sw,22,11,T().surface2);\n        int selx=sx+2+spiOcPendingIdx*(sw/2-2);\n        s.fillRoundRect(selx,sy+2,sw/2-2,18,9,T().accent);\n        for(int k=0;k<2;k++){\n          char lb[8]; sprintf(lb,\"%dMHz\",SPI_OC_OPTIONS[k]);\n          s.setTextColor(k==spiOcPendingIdx?(uint16_t)0xFFFF:T().subtext);\n          s.setCursor(sx+k*(sw/2)+(sw/2)/2-s.textWidth(lb)/2,sy+7); s.print(lb);\n        }",
"        int sw=138, sx=xr-sw, sy=ry+(rh-22)/2, sg=sw/3;\n        s.fillRoundRect(sx,sy,sw,22,11,T().surface2);\n        int selx=sx+2+spiOcPendingIdx*sg;\n        s.fillRoundRect(selx,sy+2,sg-2,18,9,T().accent);\n        for(int k=0;k<3;k++){\n          char lb[8]; sprintf(lb,\"%d\",SPI_OC_OPTIONS[k]);\n          s.setTextColor(k==spiOcPendingIdx?(uint16_t)0xFFFF:T().subtext);\n          s.setCursor(sx+k*sg+sg/2-s.textWidth(lb)/2,sy+7); s.print(lb);\n        }","draw")
s=rep(s,"      int sw=108, sx=xr-sw;\n      if(x>=sx && x<=sx+sw){ spiOcPendingIdx=(x<sx+sw/2)?0:1; needRedraw=true; }",
"      int sw=138, sx=xr-sw;\n      if(x>=sx && x<=sx+sw){ int k=(x-sx)*3/sw; if(k>2)k=2; spiOcPendingIdx=k; needRedraw=true; }","touch")
open('phone.ino','w',encoding='utf-8').write(s)
print("OK: opsi SPI 40/60/80 MHz terpasang.")
