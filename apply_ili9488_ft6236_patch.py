import sys, os, re
for f in ('phone.ino','hw_conf.h','hw_ili9488.h','ft6236_renphone.ino'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo, setelah menyalin hw_ili9488.h & ft6236_renphone.ino)"%f); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'ft6236Begin' in s:
    print("Patch ILI9488+FT6236 sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what, cnt=1):
    if src.count(old)!=cnt:
        print("ERROR: anchor '%s' ketemu %d kali (harus %d). Tidak ada yang diubah."%(what,src.count(old),cnt)); sys.exit(1)
    return src.replace(old,new)

# ---- LGFX: panel ILI9488, tanpa XPT2046 ----
s=rep(s,"class LGFX : public lgfx::LGFX_Device {\n","#include \"hw_ili9488.h\"   // ILI9488 3,5\" + FT6236\nclass LGFX : public lgfx::LGFX_Device {\n","include hw")
s=rep(s,"  lgfx::Panel_ILI9341 _panel_instance;","  lgfx::Panel_ILI9488 _panel_instance;","panel class")
s=rep(s,"  lgfx::Touch_XPT2046 _touch_instance;\n","","touch inst")
s=rep(s,"      cfg.memory_width=240; cfg.memory_height=320;\n      cfg.panel_width=240;  cfg.panel_height=320;",
        "      cfg.memory_width=LCD_NATIVE_W; cfg.memory_height=LCD_NATIVE_H;\n      cfg.panel_width=LCD_NATIVE_W;  cfg.panel_height=LCD_NATIVE_H;","panel size")
s=rep(s,"      cfg.readable=true; cfg.invert=false;\n      cfg.rgb_order=false; cfg.dlen_16bit=false;",
        "      cfg.readable=false; cfg.invert=LCD_INVERT;   // ILI9488: MISO tidak andal\n      cfg.rgb_order=LCD_RGB_ORDER; cfg.dlen_16bit=false;","panel flags")
i=s.find("    { auto cfg = _touch_instance.config();")
j=s.find("_panel_instance.setTouch(&_touch_instance); }\n",i)
if i<0 or j<0: print("ERROR: blok config touch XPT2046 tidak ketemu"); sys.exit(1)
s=s[:i]+s[j+len("_panel_instance.setTouch(&_touch_instance); }\n"):]

# ---- clock SPI: ILI9488 kirim 18-bit (3 byte/piksel), batas aman jauh di bawah ILI9341 ----
s=rep(s,"#define DISPLAY_SPI_FREQ_EXPERIMENTAL 60000000","#define DISPLAY_SPI_FREQ_EXPERIMENTAL 40000000","spi freq")
s=rep(s,"const int SPI_OC_OPTIONS[2] = {60,80};","const int SPI_OC_OPTIONS[2] = {27,40};   // ILI9488: 27 aman, 40 cepat (60+ biasanya korup)","spi options")

# ---- ukuran layar ----
s=rep(s,"int SCR_W = 320, SCR_H = 240;\n","int SCR_W = 480, SCR_H = 320;\nbool ft6236Begin(); bool ftGetTouch(lgfx::touch_point_t* tp); // ft6236_renphone.ino\n","SCR default")
s=rep(s,"  canvasApp.createSprite(320,240-STATUS_H-44);","  canvasApp.createSprite(480,320-STATUS_H-44);","canvasApp setup")
s=rep(s,"#define INF_MAX_COLS 320 ","#define INF_MAX_COLS 480 ","INF cols")
s=rep(s,"startTp.y > 320 - 40;","startTp.y > (int)display.height() - 40;","mjpeg gesture")

# ---- touch: ganti XPT2046 -> FT6236, matikan filter ghost khusus kaca lama ----
s=rep(s,"  if(!display.getTouch(&tp)){ prevDown=false; return false; }","  if(!ftGetTouch(&tp)){ prevDown=false; return false; }","getTouch")
s=rep(s,"  if(display.touch())loadOrRunCalibration(); // kalibrasi SEKARANG selalu match dgn rotasi runtime","  ft6236Begin(); // touch kapasitif FT6236: tanpa kalibrasi","touch init")
for name,val in (("TOUCH_DEAD_W","0"),("TOUCH_DEAD_H","0"),("TOUCH_GHOST_X0","9999"),("TOUCH_GHOST_X1","9999"),("TOUCH_GHOST_Y0","9999"),("TOUCH_GHOST_Y1","9999")):
    s,n=re.subn(r"(#define %s\s+)-?\d+"%name, r"\g<1>"+val, s)
    if n!=1: print("ERROR: %s ketemu %d kali"%(name,n)); sys.exit(1)
s=rep(s,"s.print(\"Kalibrasi Layar Sentuh\");","s.print(\"Touch FT6236 (tanpa kalibrasi)\");","label tcal")
s=rep(s,"      if(millis()<settTcalArmUntil){\n        Preferences p; p.begin(\"touch_cal\",false); p.putBool(\"done\",false); p.end();\n        ESP.restart();\n      } else { settTcalArmUntil=millis()+2500; showToast(\"Ketuk lagi untuk kalibrasi layar\"); needRedraw=true; }\n      break;",
        "      showToast(\"Touch kapasitif, tidak perlu kalibrasi\"); needRedraw=true;\n      break;","tcal action")
open('phone.ino','w',encoding='utf-8').write(s)

# ---- DOOM: 320x200 di tengah layar 480x320 ----
h=open('hw_conf.h',encoding='utf-8').read()
for name,val in (("DOOM_PANEL_W","480"),("DOOM_PANEL_H","320"),("DOOM_VIEWPORT_X","80"),("DOOM_VIEWPORT_Y","60")):
    h,n=re.subn(r"(#define %s\s+)\d+"%name, r"\g<1>"+val, h)
    if n!=1: print("ERROR: %s ketemu %d kali di hw_conf.h"%(name,n)); sys.exit(1)
open('hw_conf.h','w',encoding='utf-8').write(h)

# ---- workflow ----
wf='.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    a="          cp phone.ino build_sketch/\n"
    if 'ft6236_renphone.ino' not in w:
        if w.count(a)==1:
            open(wf,'w',encoding='utf-8').write(w.replace(a,a+"          cp ft6236_renphone.ino build_sketch/\n          cp hw_ili9488.h build_sketch/\n"))
        else: print("PERINGATAN: tambahkan 'cp ft6236_renphone.ino build_sketch/' dan 'cp hw_ili9488.h build_sketch/' ke workflow sendiri")
print("OK: ILI9488 480x320 + FT6236 terpasang")
