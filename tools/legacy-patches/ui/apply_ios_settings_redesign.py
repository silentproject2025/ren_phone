#!/usr/bin/env python3
# DESAIN ULANG LAYOUT Pengaturan ala iOS (daftar "inset grouped", judul besar, switch,
# slider, pemilih tema bulat, daftar WiFi, bisa di-scroll). Jalankan SETELAH
# apply_ios_theme_patch.py dan apply_ios_theme_v2.py:
#   python3 apply_ios_settings_redesign.py
import sys
def read(p): return open(p,encoding="utf-8").read()
def write(p,s): open(p,"w",encoding="utf-8").write(s)

GLOBALS = r'''
// =====================================================================
// PENGATURAN ala iOS -- state layout & sentuhan (daftar scroll, tap-saat-lepas)
// =====================================================================
enum { SR_TITLE=0, SR_SECT, SR_BRIGHT, SR_THEME, SR_FONT, SR_ROT, SR_SPI, SR_SPIAPPLY,
       SR_SCAN, SR_WIFI, SR_NONET, SR_SSID, SR_PASS, SR_CONNECT, SR_MPU, SR_TCAL };
#define SETT_MAXROWS 40
int settRT[SETT_MAXROWS], settRG[SETT_MAXROWS], settRY[SETT_MAXROWS], settRH[SETT_MAXROWS], settRA[SETT_MAXROWS];
const char* settRL[SETT_MAXROWS];
int settRowN=0, settTotalH=0;
float settScroll=0;
int settTouchMode=0;            // 0 diam, 1 menunggu (tap/scroll), 2 geser slider, 3 scroll
int settTouchX0=0, settTouchY0=0, settTouchLastY=0;
unsigned long settTouchT0=0, settTcalArmUntil=0;
'''

NEW_FUNCS = r'''int settAreaTop(){ return STATUS_H; }
int settAreaBot(){ return kbVisible ? (kbY()-2) : (backY()-4); }

void settAddRow(int type,int grp,int h,int arg,const char* lbl,int& y){
  if(settRowN>=SETT_MAXROWS) return;
  settRT[settRowN]=type; settRG[settRowN]=grp; settRY[settRowN]=y; settRH[settRowN]=h; settRA[settRowN]=arg; settRL[settRowN]=lbl;
  settRowN++; y+=h;
}

// Satu-satunya sumber posisi baris: dipakai gambar DAN hit-test (gak mungkin meleset)
void settBuildLayout(){
  settRowN=0; int y=4; int g=1;
  settAddRow(SR_TITLE,0,36,0,"Pengaturan",y);

  settAddRow(SR_SECT,0,22,0,"TAMPILAN",y);
  settAddRow(SR_BRIGHT,g,34,0,nullptr,y);
  settAddRow(SR_THEME,g,52,0,nullptr,y);
  settAddRow(SR_FONT,g,34,0,nullptr,y);
  settAddRow(SR_ROT,g,34,0,nullptr,y); g++;

  settAddRow(SR_SECT,0,26,0,"KINERJA",y);
  settAddRow(SR_SPI,g,38,0,nullptr,y);
  if(spiOcPendingIdx!=spiOcIdx) settAddRow(SR_SPIAPPLY,g,34,0,nullptr,y);
  g++;

  settAddRow(SR_SECT,0,26,0,"WI-FI",y);
  settAddRow(SR_SCAN,g,34,0,nullptr,y);
  if(!wifiScanning && scannedWifiNum==0) settAddRow(SR_NONET,g,30,0,nullptr,y);
  for(int i=0;i<scannedWifiNum && i<5;i++) settAddRow(SR_WIFI,g,32,i,nullptr,y);
  settAddRow(SR_SSID,g,34,0,nullptr,y);
  settAddRow(SR_PASS,g,34,0,nullptr,y);
  settAddRow(SR_CONNECT,g,36,0,nullptr,y); g++;

  settAddRow(SR_SECT,0,26,0,"SENSOR & LAYAR",y);
  settAddRow(SR_MPU,g,34,0,nullptr,y);
  settAddRow(SR_TCAL,g,34,0,nullptr,y); g++;

  y+=14; settTotalH=y;
}
float settMaxScroll(){ int vis=settAreaBot()-settAreaTop(); int m=settTotalH-vis; return m>0?(float)m:0.0f; }

String settFit(LGFX_Sprite& s,String v,int maxW){
  while(v.length()>1 && s.textWidth(v.c_str())>maxW) v.remove(v.length()-1);
  return v;
}
void settTextR(LGFX_Sprite& s,int xr,int ty,const char* str,uint16_t col){
  s.setTextColor(col); s.setCursor(xr-s.textWidth(str),ty); s.print(str);
}
void settChevron(LGFX_Sprite& s,int cx,int cy,uint16_t col){ // ">" tipis
  s.drawLine(cx-2,cy-4,cx+2,cy,col); s.drawLine(cx+2,cy,cx-2,cy+4,col);
}

void drawSettings(LGFX_Sprite& s){
  checkWifiScanComplete();
  settBuildLayout();

  // keyboard terbuka: geser daftar supaya kolom yg lagi diisi tetap kelihatan di atas keyboard
  if(kbVisible){
    int want=(settFocus==0)?SR_SSID:SR_PASS;
    for(int i=0;i<settRowN;i++) if(settRT[i]==want){
      int vis=settAreaBot()-settAreaTop(); int bottom=settRY[i]+settRH[i]+10;
      if(bottom-(int)settScroll>vis) settScroll=(float)(bottom-vis);
    }
  }
  float mxs=settMaxScroll(); if(settScroll>mxs) settScroll=mxs; if(settScroll<0) settScroll=0;

  iosBackdrop(s); drawStatusBar(s);
  const int top=settAreaTop(), bot=settAreaBot();
  const int cx=10, cw=SCR_W-20;
  const int oy=top-(int)lroundf(settScroll);
  s.setClipRect(0,top,SCR_W,bot-top);
  s.setFont(&lgfx::fonts::Font0);
  s.setTextSize(1);

  // 1) kartu kaca per grup + garis pemisah antar baris
  for(int i=0;i<settRowN;){
    int g=settRG[i]; if(g==0){ i++; continue; }
    int j=i; while(j+1<settRowN && settRG[j+1]==g) j++;
    int gy=oy+settRY[i], gh=settRY[j]+settRH[j]-settRY[i];
    if(gy+gh>top && gy<bot){
      drawGlassCard(s,cx,gy,cw,gh,12,T().surface,215);
      for(int k=i;k<j;k++){
        int ly=oy+settRY[k]+settRH[k];
        if(ly>top && ly<bot) s.drawFastHLine(cx+14,ly,cw-14,T().divider);
      }
    }
    i=j+1;
  }

  // 2) isi tiap baris
  for(int i=0;i<settRowN;i++){
    int ry=oy+settRY[i], rh=settRH[i];
    if(ry+rh<=top || ry>=bot) continue;
    int ty=ry+rh/2-4;
    int xl=cx+14, xr=cx+cw-14;
    s.setTextSize(1);
    switch(settRT[i]){
      case SR_TITLE:
        s.setTextSize(2); uiText(s,cx+2,ry+10,settRL[i],T().text,false,true); s.setTextSize(1);
        break;
      case SR_SECT:
        s.setTextColor(T().subtext); s.setCursor(cx+14,ry+rh-13); s.print(settRL[i]);
        break;
      case SR_BRIGHT: {
        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("Kecerahan");
        int x0=cx+92, x1=cx+cw-50, my=ry+rh/2;
        s.fillRoundRect(x0,my-2,x1-x0,4,2,blend565(T().surface2,0xFFFF,60));
        int kx=x0+(constrain((int)brightness,10,255)-10)*(x1-x0)/245;
        if(kx>x0) s.fillRoundRect(x0,my-2,kx-x0,4,2,T().accent);
        s.fillCircle(kx,my+1,8,blend565(T().surface,0x0000,120));
        s.fillCircle(kx,my,8,0xFFFF);
        char bb[8]; sprintf(bb,"%d%%",brightness*100/255);
        settTextR(s,xr,ty,bb,T().subtext);
        } break;
      case SR_THEME: {
        s.setTextColor(T().text); s.setCursor(xl,ry+14); s.print("Tema");
        int x0=cx+64, step=(cw-76)/THEME_COUNT;
        for(int t=0;t<THEME_COUNT;t++){
          int tcx=x0+step*t+step/2, tcy=ry+19;
          s.fillCircle(tcx,tcy,9,themes[t].bg);
          s.drawCircle(tcx,tcy,9,blend565(themes[t].bg,0xFFFF,110));
          s.fillCircle(tcx,tcy,4,themes[t].accent);
          if(t==themeIdx){ s.drawCircle(tcx,tcy,12,T().accent); s.drawCircle(tcx,tcy,11,T().accent); }
        }
        const char* nm=themes[themeIdx].name;
        s.setTextColor(T().subtext); s.setCursor(x0+(step*THEME_COUNT)/2-s.textWidth(nm)/2,ry+37); s.print(nm);
        } break;
      case SR_FONT:
        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("Font");
        settChevron(s,xr-3,ty+4,T().subtext);
        settTextR(s,xr-12,ty,uiFontNames[uiFontIdx],T().subtext);
        break;
      case SR_ROT:
        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("Rotasi Otomatis");
        iosSwitch(s,xr-34,ry+(rh-20)/2,autoRotateEnabled);
        break;
      case SR_SPI: {
        s.setTextColor(T().text); s.setCursor(xl,ty-4); s.print("SPI Overclock");
        s.setTextColor(T().subtext); s.setCursor(xl,ty+6); s.print(spiOcPendingIdx!=spiOcIdx?"Belum diterapkan":"Kecepatan layar");
        int sw=108, sx=xr-sw, sy=ry+(rh-22)/2;
        s.fillRoundRect(sx,sy,sw,22,11,T().surface2);
        int selx=sx+2+spiOcPendingIdx*(sw/2-2);
        s.fillRoundRect(selx,sy+2,sw/2-2,18,9,T().accent);
        for(int k=0;k<2;k++){
          char lb[8]; sprintf(lb,"%dMHz",SPI_OC_OPTIONS[k]);
          s.setTextColor(k==spiOcPendingIdx?(uint16_t)0xFFFF:T().subtext);
          s.setCursor(sx+k*(sw/2)+(sw/2)/2-s.textWidth(lb)/2,sy+7); s.print(lb);
        }
        } break;
      case SR_SPIAPPLY:
        s.setTextSize(1); uiText(s,cx+cw/2-s.textWidth("Terapkan & Mulai Ulang")/2-1,ty,"Terapkan & Mulai Ulang",T().accent,false,true);
        break;
      case SR_SCAN:
        s.setTextColor(wifiScanning?T().subtext:T().accent); s.setCursor(xl,ty); s.print("Pindai Jaringan");
        if(wifiScanning){
          int dots=(int)((millis()/300)%4); char sb[12]="Memindai"; for(int d=0;d<dots;d++) strcat(sb,".");
          settTextR(s,xr,ty,sb,T().subtext); needRedraw=true; // animasi titik selagi memindai
        }
        break;
      case SR_NONET:
        s.setTextColor(T().subtext); s.setCursor(xl,ty); s.print("Tidak ada jaringan. Ketuk Pindai.");
        break;
      case SR_WIFI: {
        int w=settRA[i];
        bool sel=(settSSID==scannedWifis[w].ssid);
        if(sel){ // centang
          s.drawLine(xl,ty+4,xl+3,ty+7,T().accent); s.drawLine(xl+3,ty+7,xl+9,ty+1,T().accent);
          s.drawLine(xl,ty+5,xl+3,ty+8,T().accent); s.drawLine(xl+3,ty+8,xl+9,ty+2,T().accent);
        }
        String nm=settFit(s,scannedWifis[w].ssid,cw-14-14-14-36);
        s.setTextColor(sel?T().accent:T().text); s.setCursor(xl+16,ty); s.print(nm.c_str());
        int lv=(scannedWifis[w].rssi>-60)?3:((scannedWifis[w].rssi>-75)?2:1);
        for(int b=0;b<3;b++){ int bh=3+b*3; s.fillRect(xr-14+b*5,ty+8-bh,3,bh,b<lv?T().text:T().divider); }
        } break;
      case SR_SSID: {
        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("SSID");
        bool has=settSSID.length()>0;
        String v=settFit(s,has?settSSID:String("Ketuk untuk isi"),cw-28-50);
        settTextR(s,xr,ty,v.c_str(),(kbVisible&&settFocus==0)?T().accent:(has?T().subtext:T().divider));
        } break;
      case SR_PASS: {
        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("Kata Sandi");
        String v;
        if(settPass.length()==0) v="Ketuk untuk isi";
        else if(settShowPass) v=settPass.substring(0,18);
        else { for(int k=0;k<(int)settPass.length()&&k<18;k++) v+="*"; }
        v=settFit(s,v,cw-28-50-44);
        settTextR(s,xr-46,ty,v.c_str(),(kbVisible&&settFocus==1)?T().accent:(settPass.length()?T().subtext:T().divider));
        settTextR(s,xr,ty,settShowPass?"Tutup":"Lihat",T().accent);
        } break;
      case SR_CONNECT:
        uiText(s,cx+cw/2-s.textWidth("Sambungkan")/2-1,ty,"Sambungkan",T().accent,false,true);
        break;
      case SR_MPU:
        s.setTextColor(mpuReady?T().accent:T().subtext); s.setCursor(xl,ty); s.print("Kalibrasi Sensor Gerak");
        if(!mpuReady) settTextR(s,xr,ty,"Tidak terdeteksi",T().subtext);
        break;
      case SR_TCAL:
        s.setTextColor(T().danger); s.setCursor(xl,ty); s.print("Kalibrasi Layar Sentuh");
        if(millis()<settTcalArmUntil){ settTextR(s,xr,ty,"Ketuk lagi",T().danger); needRedraw=true; }
        break;
    }
  }
  s.clearClipRect();

  // indikator scroll
  if(mxs>0){
    int railTop=top+6, railH=(bot-top)-12;
    if(railH>20){
      s.fillRoundRect(SCR_W-5,railTop,3,railH,1,blend565(T().surface,0xFFFF,50));
      int thumbH=max(16,(int)(railH*(float)(bot-top)/(float)settTotalH));
      int thumbY=railTop+(int)((railH-thumbH)*(settScroll/mxs));
      s.fillRoundRect(SCR_W-5,thumbY,3,thumbH,1,T().accent);
    }
  }

  if(kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

int settRowAt(int y){
  int cy=y-settAreaTop()+(int)lroundf(settScroll);
  for(int i=0;i<settRowN;i++) if(settRG[i]>0 && cy>=settRY[i] && cy<settRY[i]+settRH[i]) return i;
  return -1;
}
void settSetBrightnessFromX(int x){
  int cx=10, cw=SCR_W-20; int x0=cx+92, x1=cx+cw-50;
  brightness=(uint8_t)constrain(map(constrain(x,x0,x1),x0,x1,10,255),10,255);
  display.setBrightness(brightness);
  needRedraw=true;
}

// Ketukan (diproses saat jari DILEPAS, supaya bisa dibedakan dari scroll)
void settTap(int x,int y){
  int r=settRowAt(y); if(r<0) return;
  const int cx=10, cw=SCR_W-20, xr=cx+cw-14;
  switch(settRT[r]){
    case SR_THEME: {
      int x0=cx+64, step=(cw-76)/THEME_COUNT;
      int t=(x-x0)/step;
      if(x>=x0 && t>=0 && t<THEME_COUNT){
        themeIdx=t; saveTheme(); initAppColors();
        showToast("Tema diganti"); needRedraw=true;
      }
      } break;
    case SR_FONT:
      uiFontIdx=(uiFontIdx+1)%UI_FONT_COUNT; saveFontPref();
      showToast((String("Font: ")+uiFontNames[uiFontIdx]).c_str());
      needRedraw=true; break;
    case SR_ROT:
      autoRotateEnabled=!autoRotateEnabled; saveAutoRotatePref();
      showToast(autoRotateEnabled?"Auto-rotate aktif":"Auto-rotate mati");
      needRedraw=true; break;
    case SR_SPI: {
      int sw=108, sx=xr-sw;
      if(x>=sx && x<=sx+sw){ spiOcPendingIdx=(x<sx+sw/2)?0:1; needRedraw=true; }
      } break;
    case SR_SPIAPPLY:
      spiOcIdx=spiOcPendingIdx; saveSpiOcPref();
      showToast("Menerapkan & restart...");
      renderCurrentFrame(); push(); // toast harus sempat kelihatan sebelum layar mati karena restart
      delay(500); ESP.restart();
      break;
    case SR_SCAN:
      if(!wifiScanning){ startWifiScan(); showToast("Memindai..."); }
      break;
    case SR_WIFI: {
      int w=settRA[r];
      if(w>=0 && w<scannedWifiNum){
        settSSID=scannedWifis[w].ssid; settPass="";
        settFocus=1; kbTarget=&settPass; kbVisible=true; kbMode=KB_LOWER;
        showToast(settSSID.c_str()); needRedraw=true;
      }
      } break;
    case SR_SSID:
      settFocus=0; kbTarget=&settSSID; kbVisible=true; kbMode=KB_LOWER; needRedraw=true; break;
    case SR_PASS:
      if(x>=xr-44){ settShowPass=!settShowPass; needRedraw=true; }
      else { settFocus=1; kbTarget=&settPass; kbVisible=true; kbMode=KB_LOWER; needRedraw=true; }
      break;
    case SR_CONNECT:
      settSSID.toCharArray(WIFI_SSID,64);
      settPass.toCharArray(WIFI_PASSWORD,64);
      saveWifiCreds(); connectWifi(true);
      showToast("Menghubungkan WiFi...");
      needRedraw=true; break;
    case SR_MPU:
      if(mpuReady){ calibrateMPU(); needRedraw=true; }
      else showToast("Sensor MPU6050 tidak terdeteksi");
      break;
    case SR_TCAL:
      // aman dari salah ketuk: harus diketuk 2x dalam 2,5 detik (ini me-restart perangkat)
      if(millis()<settTcalArmUntil){
        Preferences p; p.begin("touch_cal",false); p.putBool("done",false); p.end();
        ESP.restart();
      } else { settTcalArmUntil=millis()+2500; showToast("Ketuk lagi untuk kalibrasi layar"); needRedraw=true; }
      break;
  }
}

// Dipanggil tiap iterasi loop() selagi layar Pengaturan terbuka (tanpa keyboard)
void settingsPointerTick(bool touched,bool wasT,int tx,int ty){
  if(wifiScanning) checkWifiScanComplete();
  settBuildLayout();
  if(touched && !wasT){
    settTouchX0=tx; settTouchY0=ty; settTouchLastY=ty; settTouchT0=millis(); settTouchMode=0;
    if(isBack(tx,ty)){ navBack(); return; }
    if(ty>=settAreaTop() && ty<settAreaBot()){
      int r=settRowAt(ty);
      if(r>=0 && settRT[r]==SR_BRIGHT){ settTouchMode=2; settSetBrightnessFromX(tx); }
      else settTouchMode=1;
    }
    return;
  }
  if(touched && wasT){
    if(settTouchMode==2){ settSetBrightnessFromX(tx); }
    else if(settTouchMode==1 || settTouchMode==3){
      if(settTouchMode==1 && abs(ty-settTouchY0)>8) settTouchMode=3;
      if(settTouchMode==3){
        settScroll+=(float)(settTouchLastY-ty);
        float mx=settMaxScroll(); if(settScroll>mx) settScroll=mx; if(settScroll<0) settScroll=0;
        needRedraw=true;
      }
    }
    settTouchLastY=ty;
    return;
  }
  if(!touched && wasT){
    if(settTouchMode==1 && millis()-settTouchT0<600) settTap(settTouchX0,settTouchY0);
    settTouchMode=0;
  }
}

// Jalur sentuh generik: sekarang HANYA untuk keyboard (sisanya lewat settingsPointerTick)
void settingsTouch(int x,int y,bool held,bool isNew){
  if(!kbVisible) return;
  if(!isNew) return;
  int y0=kbY();
  if(y<y0-2){ kbVisible=false; kbTarget=nullptr; settFocus=-1; }
  else kbTouch(x,y);
  needRedraw=true;
}

'''

def main():
    s=read('phone.ino')
    if 'settingsPointerTick' in s:
        print("redesign Pengaturan sudah terpasang, dilewati"); return
    for need in ('int iosRadius(','void iosBackdrop(','void iosSwitch('):
        if need not in s: sys.exit("GAGAL: jalankan apply_ios_theme_patch.py dan apply_ios_theme_v2.py dulu (kurang: %s)"%need)
    # 1) global state sebelum settingsEnter
    k='void settingsEnter(){ \n'
    if k not in s: sys.exit("GAGAL: settingsEnter tidak cocok")
    s=s.replace(k, GLOBALS+'\n'+k+'  settScroll=0; settTouchMode=0; settTcalArmUntil=0;\n',1)
    # 2) ganti drawSettings + settingsTouch
    a=s.index('void drawSettings(LGFX_Sprite& s){')
    mark='// =============================================\n// APP: NOTEPAD'
    b=s.index(mark)
    if not (a<b): sys.exit("GAGAL: urutan fungsi tak terduga")
    s=s[:a]+NEW_FUNCS+s[b:]
    # 3) hook di loop(): pointer tick utk layar Pengaturan
    old='''  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0){
      bool held = touched&&wasTouched;'''
    new='''  } else {
    if(curScreen()==SCR_SETTINGS && !kbVisible) settingsPointerTick(touched,wasTouched,tx,ty); // Pengaturan ala iOS: scroll + tap-saat-lepas
    int idx=appIndexForScreen(curScreen());
    if(idx>=0){
      bool held = touched&&wasTouched;'''
    if s.count(old)!=1: sys.exit("GAGAL: titik sambung di loop() ketemu %d kali"%s.count(old))
    s=s.replace(old,new,1)
    write('phone.ino',s)
    print("OK: Pengaturan didesain ulang ala iOS (grouped list, switch, slider, pemilih tema, scroll)")
main()
