import sys, os
for f in ('power_renphone.ino','phone.ino','rplink.h','cam_bt_coprocessor/cam_bt_coprocessor.ino','cam_bt_coprocessor/rplink.h'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari folder root repo ren_phone)"%f); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'powerMenuOpen' in s:
    print("Menu Daya sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

# ---------- phone.ino ----------
s=rep(s,"void ccActScreenshot(); // definisi di features_renphone.ino\n",
 "void ccActScreenshot(); // definisi di features_renphone.ino\nextern bool powerMenuOpen; void powerMenuShow(); void drawPowerMenu(LGFX_Sprite&); void powerMenuTouch(bool,int,int); // power_renphone.ino\n","forward decl")
# tahan-lama tile Kunci (index 6) di Control Center -> menu Daya
s=rep(s,"    case 10: dest=SCR_GALLERY;  break;                   // Tangkap -> album Screenshot\n",
 "    case 10: dest=SCR_GALLERY;  break;                   // Tangkap -> album Screenshot\n"
 "    case 6: { vibExit(); ccOffset=0; ccAnimating=false; controlCenterOpen=false; powerMenuShow(); needRedraw=true; return; } // Kunci (tahan) -> menu Daya\n","ccLongPress")
# gambar: paling atas (kecuali panggilan)
s=rep(s,"    if(diMenuOpen) drawDiMenu(canvas); // v90: context menu ketuk-lama, ditumpuk di atas pil DI\n  }\n}\n",
 "    if(diMenuOpen) drawDiMenu(canvas); // v90: context menu ketuk-lama, ditumpuk di atas pil DI\n  }\n  if(powerMenuOpen && !callOverlayActive()) drawPowerMenu(canvas); // menu Daya: modal di atas semuanya\n}\n","render")
# sentuhan: modal, sesudah overlay panggilan, sebelum App Switcher
s=rep(s,"  if(appSwitcherOpen){\n    switcherAnimTick();",
 "  if(powerMenuOpen){\n    powerMenuTouch(newT,tx,ty);\n    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }\n    wasTouched=touched; delay(1); return;\n  }\n  if(appSwitcherOpen){\n    switcherAnimTick();","touch")
# baris di Pengaturan > TENTANG
s=rep(s,"SR_ABOUT, SR_HOMEEDIT };","SR_ABOUT, SR_POWER, SR_HOMEEDIT };","enum SR")
s=rep(s,"  settAddRow(SR_ABOUT,g,34,0,nullptr,y); g++;\n","  settAddRow(SR_ABOUT,g,34,0,nullptr,y); g++;\n  settAddRow(SR_POWER,g,34,0,nullptr,y); g++;\n","layout")
s=rep(s,"      case SR_TCAL:\n        s.setTextColor(T().danger);",
 "      case SR_POWER:\n        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print(\"Daya (Matikan / Mulai Ulang)\");\n        settChevron(s,xr-4,ty+4,T().subtext);\n        break;\n      case SR_TCAL:\n        s.setTextColor(T().danger);","draw row")
s=rep(s,"    case SR_ABOUT:\n      navPush(SCR_ABOUT); break;\n","    case SR_ABOUT:\n      navPush(SCR_ABOUT); break;\n    case SR_POWER:\n      powerMenuShow(); needRedraw=true; break;\n","touch row")

# ---------- protokol: perintah baru 'Z' (dua salinan harus identik) ----------
for f in ('rplink.h','cam_bt_coprocessor/rplink.h'):
    r=open(f,encoding='utf-8').read()
    if 'RP_POWEROFF' not in r:
        r=rep(r,"#ifndef RP_PING\n","#define RP_POWEROFF 'Z'   // S3 -> CAM: tidur dalam (deep sleep). Bangun lewat sinyal di RX (GPIO13)\n#ifndef RP_PING\n",f)
        open(f,'w',encoding='utf-8').write(r)

# ---------- sisi CAM ----------
cf='cam_bt_coprocessor/cam_bt_coprocessor.ino'
c=open(cf,encoding='utf-8').read()
if 'pwrOffReq' not in c:
    c=rep(c,"// Saklar Bluetooth dari tombol Control Center (RP_BTMODE).\nstatic void btSetEnabled(bool on) {",
     "// Perintah matikan dari S3 (RP_POWEROFF): diproses di loop() krn butuh konteks BT stack.\nstatic volatile bool pwrOffReq = false;\n"
     "static void camPowerOff() {\n  Serial.println(\"[PWR] deep sleep atas perintah S3\");\n  a2dp.set_auto_reconnect(false);\n  if (btConnected) a2dp.disconnect();\n  delay(500);\n"
     "  rtc_gpio_pullup_en(GPIO_NUM_13);               // RX dari S3 (GPIO13) idle HIGH\n  rtc_gpio_pulldown_dis(GPIO_NUM_13);\n"
     "  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);  // bangun begitu S3 mulai kirim data (start-bit LOW)\n  esp_deep_sleep_start();\n}\n\n"
     "// Saklar Bluetooth dari tombol Control Center (RP_BTMODE).\nstatic void btSetEnabled(bool on) {","cam deklarasi")
    c=rep(c,"    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN: case RP_BTMODE:\n      queueCmd(type, p, len);\n      break;\n",
     "    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN: case RP_BTMODE:\n      queueCmd(type, p, len);\n      break;\n    case RP_POWEROFF:\n      pwrOffReq = true;\n      break;\n","cam handleFrame")
    c=rep(c,"  // perintah dari link (butuh konteks loop: BT stack / NVS)\n  Cmd c;\n",
     "  if (pwrOffReq) camPowerOff();   // tidak kembali\n\n  // perintah dari link (butuh konteks loop: BT stack / NVS)\n  Cmd c;\n","cam loop")
    if '#include "driver/rtc_io.h"' not in c and '#include <driver/rtc_io.h>' not in c:
        i=c.find('#include "rplink.h"')
        if i<0: print("ERROR: #include \"rplink.h\" tidak ketemu di firmware CAM"); sys.exit(1)
        c=c[:i]+'#include "driver/rtc_io.h"\n#include "esp_sleep.h"\n'+c[i:]
    open(cf,'w',encoding='utf-8').write(c)

# ---------- workflow ----------
wf='.github/workflows/build-firmware.yml'
w=None
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    a="          cp wakeword_renphone.ino build_sketch/\n"
    if 'power_renphone.ino' not in w:
        if w.count(a)==1: w=w.replace(a,a+"          cp power_renphone.ino build_sketch/\n")
        else: print("PERINGATAN: tambahkan 'cp power_renphone.ino build_sketch/' ke workflow S3 sendiri"); w=None
    else: w=None
open('phone.ino','w',encoding='utf-8').write(s)
if w: open(wf,'w',encoding='utf-8').write(w)
print("OK: menu Daya terpasang. Flash ULANG kedua board (S3 dan CAM) supaya CAM ikut tidur.")
