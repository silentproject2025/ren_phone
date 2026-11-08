#!/usr/bin/env python3
# CONTROL CENTER: tahan-lama sebuah tile = buka pengaturan fitur itu (seperti di HP biasa).
#   Wi-Fi / Pesawat -> Pengaturan, langsung ke bagian WI-FI
#   Tema            -> Pengaturan, ke baris Tema
#   Rotasi          -> Pengaturan, ke baris Rotasi Otomatis
#   Goyang          -> Pengaturan, ke bagian SENSOR & LAYAR
#   LED             -> app Neopixel
#   BT              -> app Musik (pindai & sambung perangkat Bluetooth)
#   Tangkap         -> Galeri (album Screenshot)
#   DND / Kunci / Senyap -> tidak punya halaman pengaturan: muncul toast saja
# Ketuk biasa tetap menjalankan aksi, tapi sekarang diproses saat jari LEPAS (supaya bisa dibedakan dari tahan).
# Aman diulang. Semua anchor dicek dulu; kalau ada yang tidak ketemu, TIDAK ada file yang diubah.
import sys
p=open('phone.ino',encoding='utf-8').read()
if 'CC-LONGPRESS' in p:
    print("Tahan-lama Control Center sudah terpasang, dilewati"); sys.exit(0)
def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

# 1) variabel
p=rep(p,"bool  ccSliderDrag=false;\n","""bool  ccSliderDrag=false;
// CC-LONGPRESS: tahan tile >= CC_LONG_MS = buka pengaturan fitur itu; ketuk biasa = aksi (diproses saat jari lepas)
const unsigned long CC_LONG_MS=450;
int  ccHoldIdx=-1;            // tile yg sedang ditekan (-1 = tidak ada)
unsigned long ccHoldStart=0;
int  ccHoldX0=0, ccHoldY0=0;
int  settJumpCode=0;          // saat Pengaturan digambar: lompat ke 1 Wi-Fi, 2 Tema, 3 Rotasi, 4 Sensor & layar
""","variabel")

# 2) tile mengecil sedikit selagi ditahan
p=rep(p,"      rs *= 1.0f-0.12f*sinf(pt*PI);\n    }\n","      rs *= 1.0f-0.12f*sinf(pt*PI);\n    }\n    if(ccHoldIdx==i) rs *= 0.94f; // CC-LONGPRESS: tetap sedikit mengecil selagi ditahan\n","draw tile")

# 3) fungsi aksi/tahan-lama + ccTouch hanya 'mengarmkan' tahan
FUNCS=r'''// CC-LONGPRESS ---------------------------------------------------------------
void ccRunTap(int i){
  void(*actions[CC_ITEMS])() = { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, ccActShake, nullptr, ccActNeopixel, ccActSilent, ccActBluetooth, ccActScreenshot };
  if(i==6){ locked=true; lockScreenReset(); closeControlCenter(); return; }
  if(actions[i]) actions[i]();
  needRedraw=true;
}

// Tahan-lama: buka pengaturan fitur tile tsb. Panel ditutup INSTAN (bukan animasi) supaya transisi layar bersih.
void ccLongPress(int i){
  Screen dest=SCR_HOME; int jump=0;
  switch(i){
    case 0: case 1: dest=SCR_SETTINGS; jump=1; break;   // Wi-Fi, Pesawat -> bagian WI-FI
    case 3:  dest=SCR_SETTINGS; jump=2; break;           // Tema
    case 4:  dest=SCR_SETTINGS; jump=3; break;           // Rotasi -> Rotasi Otomatis
    case 5:  dest=SCR_SETTINGS; jump=4; break;           // Goyang -> Sensor & layar
    case 7:  dest=SCR_NEOPIXEL; break;
    case 9:  dest=SCR_MUSIC;    break;                   // BT: pindai & sambung perangkat ada di Musik
    case 10: dest=SCR_GALLERY;  break;                   // Tangkap -> album Screenshot
    default: showToast("Tidak ada pengaturan lanjutan"); needRedraw=true; return;
  }
  Screen cur=curScreen();
  if(cur==SCR_NES||cur==SCR_DOOM){ showToast("Keluar dari game dulu"); needRedraw=true; return; }
  vibExit();                                              // getar beda dari ketukan biasa = sinyal "tahan-lama"
  ccOffset=0; ccAnimating=false; controlCenterOpen=false;
  if(jump) settJumpCode=jump;
  if(cur==dest){ needRedraw=true; return; }               // sudah di layar itu (mis. Pengaturan): cukup lompat
  navPush(dest);
}

// Dipanggil tiap putaran loop() selagi Control Center terbuka.
void ccHoldTick(bool touched,int tx,int ty){
  if(ccHoldIdx<0) return;
  if(!controlCenterOpen){ ccHoldIdx=-1; return; }
  if(touched){
    if(abs(tx-ccHoldX0)>14 || abs(ty-ccHoldY0)>14){ ccHoldIdx=-1; needRedraw=true; return; } // jari bergeser -> batal
    if(millis()-ccHoldStart>=CC_LONG_MS){ int i=ccHoldIdx; ccHoldIdx=-1; ccLongPress(i); needRedraw=true; }
  } else {                                                // jari lepas sebelum ambang tahan-lama = ketukan biasa
    int i=ccHoldIdx; ccHoldIdx=-1;
    ccRunTap(i);
  }
}

'''
p=rep(p,"void ccTouch(int x,int y){\n",FUNCS+"void ccTouch(int x,int y){\n","ccTouch")
p=rep(p,"  void(*actions[CC_ITEMS])() = { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, ccActShake, nullptr, ccActNeopixel, ccActSilent, ccActBluetooth, ccActScreenshot };\n  for(int i=0;i<CC_ITEMS;i++){\n    int col=i%CC_COLS, row=i/CC_COLS;\n    int bx=gx+col*(cw+gap);\n    int by=top+row*(ch+gap);\n    if(x>=bx&&x<=bx+cw&&y>=by&&y<=by+ch){\n      ccPressIdx=i; ccPressMs=millis(); // v108: feedback tekan\n      if(i==6){ locked=true; lockScreenReset(); closeControlCenter(); return; }\n      if(actions[i]) actions[i]();\n      needRedraw=true;\n      return;\n    }\n  }",
"  for(int i=0;i<CC_ITEMS;i++){\n    int col=i%CC_COLS, row=i/CC_COLS;\n    int bx=gx+col*(cw+gap);\n    int by=top+row*(ch+gap);\n    if(x>=bx&&x<=bx+cw&&y>=by&&y<=by+ch){\n      ccPressIdx=i; ccPressMs=millis(); // v108: feedback tekan\n      // CC-LONGPRESS: aksi ketuk dijalankan saat jari LEPAS (ccHoldTick); tahan >= CC_LONG_MS = buka pengaturannya\n      ccHoldIdx=i; ccHoldStart=millis(); ccHoldX0=x; ccHoldY0=y;\n      needRedraw=true;\n      return;\n    }\n  }","ccTouch tile")

# 4) loop(): panggil ccHoldTick (SETELAH rantai if/else-if ccTouch/slider)
p=rep(p,"    else if(touched && ccSliderDrag) ccSliderSet(tx); // v108: drag slider Brightness\n",
 "    else if(touched && ccSliderDrag) ccSliderSet(tx); // v108: drag slider Brightness\n    ccHoldTick(touched,tx,ty); // CC-LONGPRESS: tahan tile = buka pengaturannya; ketuk = aksi saat jari lepas\n","loop")

# 5) Pengaturan: lompat ke bagian terkait (dikonsumsi sekali)
p=rep(p,"  checkWifiScanComplete();\n  settBuildLayout();\n","""  checkWifiScanComplete();
  settBuildLayout();
  // CC-LONGPRESS: dibuka dari tahan-lama tile Control Center -> gulir ke bagian terkait (sekali pakai)
  if(settJumpCode){
    int want = settJumpCode==1 ? SR_SCAN : settJumpCode==2 ? SR_THEME : settJumpCode==3 ? SR_ROT : SR_MPU;
    int back = (settJumpCode==1 || settJumpCode==4) ? 30 : 60;   // sisakan judul bagian di atas baris tujuan
    for(int i=0;i<settRowN;i++) if(settRT[i]==want){ settScroll=(float)(settRY[i]-back); break; }
    settJumpCode=0;
  }
""","drawSettings")
open('phone.ino','w',encoding='utf-8').write(p)
print("OK: tahan-lama Control Center terpasang")
