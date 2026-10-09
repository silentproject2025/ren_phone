#!/usr/bin/env python3
"""
ROMBAK UI "Aurora Glass" -- Accretion / Ren Phone
  * Home      : widget dulu (jam besar, kartu musik dgn kontrol, baterai) + grid app di bawahnya
  * Lock      : tanggal + jam bergaya iOS, chip baterai, kartu musik mini, hint geser
  * Control Center : tombol kaca + glow aktif, slider lebih tebal
  * Status bar: transparan di atas wallpaper (khusus Home), jam tebal, baterai kapsul

Jalankan di root repo (folder berisi phone.ino):
    python apply_ui_overhaul_patch.py
Idempotent (aman diulang). Perlu patch sebelumnya (apply_cc_bt_music_icons_patch.py) sudah terpasang.
Yang berubah: phone.ino + musicbt_renphone.ino (S3 saja, CAM tidak perlu di-flash ulang).
"""
import os
import sys

ROOT = "."
cache = {}


def load(path):
    if path not in cache:
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            sys.exit(f"[GAGAL] {path} tidak ketemu. Jalankan dari root repo.")
        with open(full, encoding="utf-8", newline="") as f:
            cache[path] = f.read()
    return cache[path]


def edit(name, path, old, new, marker):
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    n = s.count(old)
    if n != 1:
        sys.exit(f"[GAGAL] {name}: anchor ditemukan {n}x di {path} (harus 1x). "
                 f"File kamu mungkin beda dari versi yang saya baca.")
    cache[path] = s.replace(old, new)
    print(f"[ok   ] {name}")


def replace_between(name, path, start, end, new, marker):
    """Ganti teks dari awal `start` sampai SEBELUM `end` (end tidak ikut diganti)."""
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    if s.count(start) != 1:
        sys.exit(f"[GAGAL] {name}: anchor awal ditemukan {s.count(start)}x (harus 1x).")
    i = s.index(start)
    j = s.find(end, i + len(start))
    if j < 0:
        sys.exit(f"[GAGAL] {name}: anchor akhir tidak ketemu.")
    cache[path] = s[:i] + new + s[j:]
    print(f"[ok   ] {name}")


PH = "phone.ino"
MU = "musicbt_renphone.ino"

# =====================================================================
# 1. forward declaration (dipakai Lock Screen & status bar yg letaknya lebih awal)
# =====================================================================
edit("fwd decl widget & musik", PH,
     "void renderCurrentFrame(); // forward decl - dipakai utk render frame BARU sblm animasi\n",
     "void renderCurrentFrame(); // forward decl - dipakai utk render frame BARU sblm animasi\n"
     "// UI-OVERHAUL: API musik utk widget Home/Lock (definisi di musicbt_renphone.ino)\n"
     "bool homeMusStarted(); bool homeMusLoaded(); bool homeMusPlaying(); float homeMusProgress();\n"
     "void homeMusTitle(char* out,int cap); void homeMusToggle(); void homeMusNext();\n"
     "const char* homeGreeting(int hour);   // definisi di dekat drawHome\n"
     "uint16_t appIconInk(uint16_t c);      // definisi di dekat drawAppIcon\n",
     "UI-OVERHAUL: API musik utk widget")

# =====================================================================
# 2. helper kaca ringan + widget + STATUS BAR baru
# =====================================================================
STATUS_NEW = r'''// =====================================================================
// UI-OVERHAUL "Aurora Glass" -- helper bersama (Home, Lock Screen, status bar)
// =====================================================================
// Kaca RINGAN utk kartu yg ikut scroll: sampel warna rata-rata wallpaper di
// bawah kartu SEKALI (bukan grid 8x6 spt drawGlassPanel), lalu tint + gradasi
// vertikal halus + rim + bayangan. Murah dirender tiap frame scroll.
void drawGlassCard(LGFX_Sprite& s,int x,int y,int w,int h,int r,uint16_t tint,uint8_t a){
  if(w<=0||h<=0||y>=SCR_H||y+h<=0) return;
  uint16_t base=glassSampleAvg(s,x,y,w,h,4);
  uint16_t fillC=blend565(base,tint,a);
  s.fillRoundRect(x,y+2,w,h,r,blend565(base,0x0000,70)); // bayangan tipis
  s.fillRoundRect(x,y,w,h,r,fillC);
  for(int dy=0;dy<h;dy++){ // pantulan cahaya: terang di atas, memudar ke bawah
    float t=1.0f-(float)dy/(float)h;
    uint8_t al=(uint8_t)(40.0f*t*t);
    if(al<2) break;
    int inset=0;
    if(dy<r){ float d=(float)(r-dy)-0.5f; float q=(float)r*(float)r-d*d; if(q<0) q=0; inset=(int)ceilf((float)r-sqrtf(q)); }
    s.drawFastHLine(x+inset,y+dy,w-2*inset,blend565(fillC,0xFFFF,al));
  }
  s.drawRoundRect(x,y,w,h,r,blend565(fillC,0xFFFF,70));
}

// Teks dgn bayangan 1px (biar kebaca di wallpaper apapun) & opsi "tebal"
// (cetak 2x geser 1px -- tanpa ganti font, jadi aman utk font pilihan user).
void uiText(LGFX_Sprite& s,int x,int y,const char* str,uint16_t col,bool shadow,bool bold){
  if(shadow){ s.setTextColor(0x0000); s.setCursor(x+1,y+1); s.print(str); if(bold){ s.setCursor(x+2,y+1); s.print(str); } }
  s.setTextColor(col); s.setCursor(x,y); s.print(str);
  if(bold){ s.setCursor(x+1,y); s.print(str); }
}

uint16_t battColor(){ return (battPercent<=15)?T().danger:(battPercent<=35)?T().accent:T().good; }

// Posisi tombol play & next di kartu musik (dipakai gambar DAN hit-test)
void homeMusBtnPos(int x,int w,int& playCx,int& nextCx){ nextCx=x+w-26; playCx=nextCx-34; }

// Kartu musik: cover piringan, judul, status, progres, (opsional) tombol kontrol
void drawMusicMini(LGFX_Sprite& s,int x,int y,int w,int h,bool ctrl){
  drawGlassCard(s,x,y,w,h,16,T().surface,150);
  int cy=y+h/2;
  bool started=homeMusStarted(), loaded=homeMusLoaded(), playing=homeMusPlaying();
  int cr=(h>=50)?18:14, ccx=x+12+cr;
  uint16_t disc=T().accent2;
  s.fillCircle(ccx,cy,cr,disc);
  s.drawCircle(ccx,cy,cr-4,blend565(disc,0x0000,90));
  s.fillCircle(ccx,cy,cr/3+1,blend565(disc,0xFFFF,130));
  s.fillCircle(ccx,cy,2,blend565(disc,0x0000,170));
  int tx=ccx+cr+10;
  int pc,nc; homeMusBtnPos(x,w,pc,nc);
  int maxX=(ctrl&&started)?(pc-20):(x+w-34);
  char title[56];
  if(started) homeMusTitle(title,sizeof(title)); else strcpy(title,"Musik");
  const char* sub=!started?"Ketuk untuk membuka":(!loaded?"Siap diputar":(playing?"Sedang diputar":"Dijeda"));
  s.setTextSize(1);
  int n=(int)strlen(title); bool cut=false;
  while(n>3 && s.textWidth(title)>(maxX-tx)){ title[--n]=0; cut=true; }
  if(cut && n>=2){ title[n-1]='.'; title[n-2]='.'; }
  int ty1=(h>=50)?y+10:y+8, ty2=(h>=50)?y+23:y+20;
  uiText(s,tx,ty1,title,T().text,false,true);
  s.setTextColor(T().subtext); s.setCursor(tx,ty2); s.print(sub);
  if(started&&loaded){ // progres tipis
    int bx=tx, bw=maxX-tx, by=y+h-11;
    if(bw>10){
      s.fillRoundRect(bx,by,bw,3,1,blend565(T().surface,0xFFFF,60));
      int fw=(int)(bw*homeMusProgress());
      if(fw>0) s.fillRoundRect(bx,by,fw,3,1,T().accent);
    }
  }
  uint16_t ink=appIconInk(T().accent);
  if(ctrl&&started){
    s.fillCircle(pc,cy,14,T().accent);
    if(playing){ s.fillRect(pc-5,cy-6,4,12,ink); s.fillRect(pc+1,cy-6,4,12,ink); }
    else s.fillTriangle(pc-4,cy-7,pc-4,cy+7,pc+6,cy,ink);
    s.fillCircle(nc,cy,11,blend565(T().surface2,0xFFFF,40));
    s.fillTriangle(nc-5,cy-5,nc-5,cy+5,nc+3,cy,T().text);
    s.fillRect(nc+4,cy-5,2,10,T().text);
  } else if(!ctrl && started && loaded){
    int gx=x+w-22;
    if(playing){ s.fillRect(gx-4,cy-6,3,12,T().accent); s.fillRect(gx+1,cy-6,3,12,T().accent); }
    else s.fillTriangle(gx-4,cy-6,gx-4,cy+6,gx+5,cy,T().accent);
  }
}

// Kartu jam: sapaan, jam besar (font sans tebal), tanggal
void drawClockCard(LGFX_Sprite& s,int x,int y,int w,int h){
  drawGlassCard(s,x,y,w,h,18,T().surface,150);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  s.setTextSize(1);
  uiText(s,x+14,y+10,ok?homeGreeting(t.tm_hour):"Halo",T().accent,false,true);
  s.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  int sz=4;
  for(;sz>=2;sz--){ s.setTextSize(sz); if(s.textWidth(tb)<=w-28) break; }
  s.setTextSize(sz);
  int fh=s.fontHeight();
  int cyc=y+h/2+4;
  s.setTextColor(T().text);
  s.setCursor(x+12,cyc-(fh*48)/100); // angka jam ~ di tengah-bawah kartu (perkiraan metrik font; geser di sini kalau perlu)
  s.print(tb);
  s.setFont(&lgfx::fonts::Font0); // Home selalu Font0 (lihat renderCurrentFrame)
  s.setTextSize(1);
  if(ok){
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[24]; snprintf(db,sizeof(db),"%s, %d %s",days[t.tm_wday],t.tm_mday,mons[t.tm_mon]);
    s.setTextColor(T().subtext); s.setCursor(x+14,y+h-14); s.print(db);
  }
}

// Kartu baterai: cincin (kartu tinggi) atau bar horizontal (kartu pendek)
void drawBattWidget(LGFX_Sprite& s,int x,int y,int w,int h){
  drawGlassCard(s,x,y,w,h,18,T().surface,150);
  uint16_t bc=battColor();
  char pb[8]; sprintf(pb,"%d",battPercent);
  if(h>=60){
    int cx=x+w/2, cy=y+h/2;
    int ro=min(h/2-6,w/2-6), ri=ro-6;
    uint16_t track=blend565(T().surface,0xFFFF,50);
    s.fillArc(cx,cy,ri,ro,0.0f,180.0f,track);
    s.fillArc(cx,cy,ri,ro,180.0f,360.0f,track);
    float a1=270.0f+battPercent*3.6f;
    if(battPercent>0){
      if(a1<=360.0f) s.fillArc(cx,cy,ri,ro,270.0f,a1,bc);
      else { s.fillArc(cx,cy,ri,ro,270.0f,360.0f,bc); s.fillArc(cx,cy,ri,ro,0.0f,a1-360.0f,bc); }
    }
    s.setTextSize(battPercent>=100?1:2);
    int tw=s.textWidth(pb), th=(battPercent>=100)?8:16;
    s.setTextColor(T().text); s.setCursor(cx-tw/2,cy-th/2); s.print(pb);
    s.setTextSize(1);
  } else {
    s.setTextSize(1);
    s.setTextColor(T().subtext); s.setCursor(x+14,y+9); s.print("Baterai");
    char pf[8]; sprintf(pf,"%d%%",battPercent);
    int tw=s.textWidth(pf);
    s.setTextColor(bc); s.setCursor(x+w-14-tw,y+9); s.print(pf);
    int bx=x+14, bw=w-28, by=y+h-14;
    s.fillRoundRect(bx,by,bw,7,3,blend565(T().surface,0xFFFF,50));
    int fw=(bw*battPercent)/100;
    if(fw>0) s.fillRoundRect(bx,by,max(fw,6),7,3,bc);
  }
}

// STATUS BAR: di Home transparan di atas wallpaper (teks putih + bayangan),
// di layar lain tetap bar solid spt biasa. Jam tebal, baterai bentuk kapsul.
void drawStatusBar(LGFX_Sprite& s){
  const bool ov = (curScreen()==SCR_HOME);
  const bool wp = ov && wallpaperReady;
  uint16_t fg  = wp ? (uint16_t)0xFFFF : T().text;
  uint16_t fg2 = wp ? (uint16_t)0xDEFB : T().subtext;
  if(!ov){
    s.fillRect(0,0,SCR_W,STATUS_H,T().surface);
    s.drawFastHLine(0,STATUS_H,SCR_W,T().divider);
  }
  struct tm t;
  bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextSize(1);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  uiText(s,10,7,tb,ok?fg:fg2,wp,true);

  int rx = SCR_W-8; // kursor kanan, bergerak ke kiri tiap elemen ditambah

  // --- Baterai: kapsul + persen ---
  int bw=24,bh=11, bx=rx-bw, by=6;
  uint16_t bc = battColor();
  s.drawRoundRect(bx,by,bw-2,bh,3,fg2);
  s.fillRoundRect(bx+bw-2,by+3,2,bh-6,1,fg2); // kutub kecil
  int innerW = bw-2-6;
  int fillW = constrain((innerW*battPercent)/100,0,innerW);
  if(fillW>0) s.fillRoundRect(bx+3,by+3,fillW,bh-6,1,bc);
  rx = bx-4;
  char pctBuf[6]; sprintf(pctBuf,"%d%%",battPercent);
  int pctW = s.textWidth(pctBuf);
  rx -= pctW;
  uiText(s,rx,7,pctBuf,bc,wp,false);
  rx -= 8;

  // --- Wifi / pesawat / tanpa sinyal (pola sama dgn drawCCIcon idx 0/1) ---
  if(airplaneMode){
    rx -= 12;
    int acx=rx, acy=12;
    s.fillTriangle(acx,acy-5, acx-3,acy+5, acx+3,acy+5, fg2);
    s.fillTriangle(acx-5,acy+6, acx+5,acy+6, acx,acy-1, fg2);
    rx -= 4;
  } else if(wifiConnected){
    rx -= 9;
    s.fillCircle(rx,16,2,fg);
    s.drawArc(rx,18,5,4,210,330,fg);
    s.drawArc(rx,18,9,8,210,330,fg);
    rx -= 12;
  } else {
    rx -= 12;
    int wcx=rx, wcy=14;
    s.fillCircle(wcx,wcy+2,1,T().danger);
    s.drawArc(wcx,wcy+2,4,3,210,330,T().danger);
    s.drawArc(wcx,wcy+2,7,6,210,330,T().danger);
    s.drawLine(wcx-6,wcy-4,wcx+6,wcy+8,T().danger);
    rx -= 4;
  }

  // --- SD ---
  if(sdReady){
    rx -= 12; uiText(s,rx,7,"SD",T().good,wp,false);
    rx -= 6;
  }
  // --- DND ---
  if(dndMode){
    rx -= 18; uiText(s,rx,7,"DND",T().accent,wp,false);
  }
}

'''

replace_between("status bar + helper kaca/widget", PH,
                "void drawStatusBar(LGFX_Sprite& s){",
                "// =============================================\n// DYNAMIC ISLAND\n",
                STATUS_NEW, "void drawGlassCard(LGFX_Sprite& s")

# =====================================================================
# 3. LOCK SCREEN
# =====================================================================
LOCK_NEW = r'''  // UI-OVERHAUL lock: wallpaper + tanggal & jam bergaya iOS + chip baterai + kartu musik + hint
  if(wallpaperReady) wallpaperImg.pushSprite(&s,0,0);
  else s.fillSprite(T().bg);
  const char* ldays[]={"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};
  const char* lmons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
  char db[32]=""; if(ok) snprintf(db,sizeof(db),"%s, %d %s",ldays[t.tm_wday],t.tm_mday,lmons[t.tm_mon]);
  int lift=(int)lockDragY;
  uint16_t fg  = wallpaperReady ? (uint16_t)0xFFFF : T().text;
  uint16_t fg2 = wallpaperReady ? (uint16_t)0xDEFB : T().subtext;

  // chip baterai (kanan atas)
  {
    char pb[8]; sprintf(pb,"%d%%",battPercent);
    s.setTextSize(1);
    int pw=s.textWidth(pb)+36, px=SCR_W-pw-10, py=8;
    drawGlassCard(s,px,py,pw,20,10,T().surface,140);
    uint16_t bc=battColor();
    s.drawRoundRect(px+9,py+5,16,10,3,fg2);
    s.fillRect(px+25,py+8,2,4,fg2);
    int fw=(10*battPercent)/100; if(fw>0) s.fillRoundRect(px+11,py+7,fw,6,1,bc);
    s.setTextColor(fg); s.setCursor(px+31,py+6); s.print(pb);
  }

  // jam besar (font sans tebal) + bayangan lembut
  s.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  s.setTextSize(3);
  int tw = s.textWidth(tb);
  int fh = s.fontHeight();
  int tx = SCR_W/2 - tw/2;
  int ty = SCR_H/2-70-lift;
  {
    uint16_t under=glassSampleAvg(s,tx,ty,tw,fh,3);
    uint16_t sh=blend565(under,0x0000,170);
    s.setTextColor(sh); s.setCursor(tx+3,ty+3); s.print(tb);
    s.setTextColor(blend565(under,0x0000,110)); s.setCursor(tx+2,ty+2); s.print(tb);
  }
  s.setTextColor(fg); s.setCursor(tx,ty); s.print(tb);
  s.setTextSize(1);
  s.setFont(&lgfx::fonts::Font0); // lock screen selalu Klasik (bukan font custom pilihan user)

  // gembok kecil + tanggal di atas jam
  int lockIconY = ty-22;
  s.drawRoundRect(SCR_W/2-7,lockIconY-1,14,11,2,fg);
  s.drawArc(SCR_W/2,lockIconY-3,6,5,180,360,fg);
  if(ok){
    int dw=(int)s.textWidth(db);
    uiText(s,SCR_W/2-dw/2,ty+2,db,fg2,true,false);
  }

  // kartu musik mini (hanya kalau ada lagu aktif)
  if(homeMusStarted() && homeMusLoaded()){
    int cw=SCR_W-40, cy0=SCR_H-100-lift;
    drawMusicMini(s,20,cy0,cw,46,false);
  }

  // hint geser (pill kaca + chevron)
  const char* hint="Geser ke atas utk buka";
  int hw=(int)s.textWidth(hint);
  int hintPillY = SCR_H-38;
  drawGlassPanel(s, SCR_W/2-hw/2-14, hintPillY, hw+28, 26, 13, T().surface, 130);
  int chevY = hintPillY-9;
  s.drawLine(SCR_W/2-6,chevY+4, SCR_W/2,chevY,   fg2);
  s.drawLine(SCR_W/2,  chevY,   SCR_W/2+6,chevY+4,fg2);
  s.setTextColor(fg2); s.setTextSize(1);
  s.setCursor(SCR_W/2-hw/2, SCR_H-24);
  s.print(hint);
}

'''
replace_between("lock screen", PH,
                "  if(wallpaperReady) wallpaperImg.pushSprite(&s,0,0); // v48:",
                "void lockScreenInput(bool touched",
                LOCK_NEW, "UI-OVERHAUL lock")

# =====================================================================
# 4. CONTROL CENTER
# =====================================================================
edit("CC: tombol kaca + glow", PH,
     "    float a=ccBtnAnim[i];\n"
     "    uint16_t fillC = lerpColor565(T().surface2, T().accent, a);\n"
     "    s.fillCircle(ccx,ccy,r,fillC);\n",
     "    float a=ccBtnAnim[i];\n"
     "    // UI-OVERHAUL: tombol kaca -- idle sedikit lebih terang dari panel, aktif = aksen + glow lembut\n"
     "    uint16_t glassIdle = blend565(T().surface2,0xFFFF,24);\n"
     "    uint16_t fillC = lerpColor565(glassIdle, T().accent, a);\n"
     "    if(a>0.05f) s.fillCircle(ccx,ccy,r+3,blend565(T().surface,T().accent,(uint8_t)(80.0f*a)));\n"
     "    s.fillCircle(ccx,ccy,r,fillC);\n"
     "    s.drawCircle(ccx,ccy,r,blend565(fillC,0xFFFF,60));\n"
     "    if(r>8) s.drawArc(ccx,ccy,r-2,r-2,205,335,blend565(fillC,0xFFFF,90));\n",
     "UI-OVERHAUL: tombol kaca")

edit("CC: slider tebal + knob berbayang", PH,
     "    int tw=(int)(sw*sp);\n"
     "    s.fillRoundRect(sx,sy,tw,4,2,T().divider);\n"
     "    int fillW = map(brightness,0,255,0,sw);\n"
     "    if(fillW>tw) fillW=tw;\n"
     "    if(fillW>0) s.fillRoundRect(sx,sy,fillW,4,2,T().accent);\n"
     "    if(sp>=1.0f){\n"
     "      int kr = ccSliderDrag ? 8 : 6; // knob membesar dikit selagi di-drag\n"
     "      s.fillCircle(sx+fillW,sy+2,kr,T().text);\n"
     "    }\n",
     "    int tw=(int)(sw*sp);\n"
     "    // UI-OVERHAUL: track tebal 8px, isi aksen + highlight, knob berbayang\n"
     "    s.fillRoundRect(sx,sy-2,tw,8,4,blend565(T().surface,0xFFFF,45));\n"
     "    int fillW = map(brightness,0,255,0,sw);\n"
     "    if(fillW>tw) fillW=tw;\n"
     "    if(fillW>0){\n"
     "      s.fillRoundRect(sx,sy-2,max(fillW,8),8,4,T().accent);\n"
     "      if(fillW>12) s.drawFastHLine(sx+4,sy-1,fillW-8,blend565(T().accent,0xFFFF,100));\n"
     "    }\n"
     "    if(sp>=1.0f){\n"
     "      int kr = ccSliderDrag ? 9 : 7; // knob membesar dikit selagi di-drag\n"
     "      s.fillCircle(sx+fillW,sy+3,kr+1,blend565(T().surface,0x0000,120));\n"
     "      s.fillCircle(sx+fillW,sy+2,kr,0xFFFF);\n"
     "      s.fillCircle(sx+fillW,sy+2,kr-4,T().accent);\n"
     "    }\n",
     "UI-OVERHAUL: track tebal 8px")

# =====================================================================
# 5. HOME: layout widget
# =====================================================================
LAYOUT_NEW = r'''// UI-OVERHAUL home: layout WIDGET (koordinat RUANG KONTEN = belum dikurangi scroll).
// Landscape: jam (kiri) + baterai (kanan) di baris 1, kartu musik di baris 2.
// Portrait : jam, kartu musik, baterai (bar) bertumpuk. Grid app mulai di bawahnya.
struct HomeRect{ int x,y,w,h; };
bool homeIsLand(){ return currentOrient==ORIENT_LANDSCAPE; }
HomeRect homeWClock(){ HomeRect r; r.x=10; r.y=STATUS_H+28; r.h=homeIsLand()?70:80; r.w=homeIsLand()?188:SCR_W-20; return r; }
HomeRect homeWMusic(){ HomeRect r; r.x=10; r.w=SCR_W-20; if(homeIsLand()){ r.y=STATUS_H+28+70+6; r.h=54; } else { r.y=STATUS_H+28+80+8; r.h=58; } return r; }
HomeRect homeWBatt(){
  HomeRect r;
  if(homeIsLand()){ r.x=206; r.y=STATUS_H+28; r.w=SCR_W-216; r.h=70; }
  else { r.x=10; r.y=STATUS_H+28+80+8+58+8; r.w=SCR_W-20; r.h=44; }
  return r;
}
int homeWidgetsEnd(){ HomeRect b=homeWBatt(), m=homeWMusic(); return max(b.y+b.h,m.y+m.h); }
int homeGridTop(){ return homeWidgetsEnd()+30; } // UI-OVERHAUL: grid di bawah blok widget (+ ruang utk judul "Semua App")
int homeResultsTop(){ return STATUS_H+34; }      // hasil pencarian dirender dari atas layar'''
edit("Home: layout widget + gridTop", PH,
     "int homeGridTop(){ return STATUS_H+46; } // v-baru: naik dr +62 (header dipadatkan)",
     LAYOUT_NEW, "UI-OVERHAUL home: layout WIDGET")

edit("Home: clip top tepat di bawah status bar", PH,
     "int homeGridClipTop(){ return STATUS_H+40; } // v-baru: naik dr +54 (header dipadatkan)",
     "int homeGridClipTop(){ return STATUS_H+1; } // UI-OVERHAUL: seluruh konten (widget+grid) scroll bareng, batas atas = tepat di bawah status bar",
     "return STATUS_H+1; } // UI-OVERHAUL")

# =====================================================================
# 6. HOME: drawHome baru
# =====================================================================
HOME_NEW = r'''void drawHome(LGFX_Sprite& s,float sc){
  // UI-OVERHAUL home "Aurora Glass": widget dulu (jam, kartu musik, baterai), grid app di bawahnya.
  // Widget + grid scroll BARENG sbg satu konten (pakai mesin scroll v105 yg sudah ada). Cuma status bar
  // & dock yg diam -> mode PITA (v106) tetap jalan: yg digambar ulang cuma area antara status bar & dock.
  const bool band = homeBandMode;
  const int clipTop = homeGridClipTop(), contentBot = homeDockY()-4, contentH = contentBot-clipTop;
  if(band) s.setClipRect(0,clipTop,SCR_W,contentH);
  if(wallpaperReady) wallpaperImg.pushSprite(&s,0,0);
  else if(band) s.fillRect(0,clipTop,SCR_W,contentH,T().bg);
  else s.fillSprite(T().bg);
  if(!band) drawStatusBar(s);

  bool searching = homeSearchActive();
  homeRecomputeFilter();
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=10, gridTop=homeGridTop();
  int iconR = min(cw,ch-16)/2 - 2; if(iconR>24) iconR=24; if(iconR<14) iconR=14;
  int scPx = homeScrollPx(sc);
  if(!band) s.setClipRect(0,clipTop,SCR_W,contentH);
  const bool wp = wallpaperReady;

  if(searching){
    // hasil pencarian: grid hasil filter dirender statis dari atas (tanpa widget & tanpa scroll)
    int rt=homeResultsTop();
    if(homeFilteredCount==0){
      const char* msg="Tidak ada app cocok";
      s.setTextColor(T().subtext); s.setTextSize(1);
      int mw=s.textWidth(msg);
      s.setCursor(SCR_W/2-mw/2, rt+20); s.print(msg);
    } else {
      for(int k=0;k<homeFilteredCount;k++){
        int i=homeFilteredIdx[k];
        int col=k%cols, row=k/cols;
        int x=gap+col*(cw+gap), y=rt+row*(ch+gap);
        if(y>contentBot) continue;
        float pa = homePressAmt(i);
        int cx=x+cw/2, cy=y+(ch-16)/2;
        int rEff = iconR - (int)lroundf(pa*iconR*0.12f);
        drawAppIcon(s, apps[i].sym, cx, cy, rEff, apps[i].color);
        drawNotifBadge(s, cx, cy, iconR, appNotifCount[i]);
        s.setTextColor(T().text);s.setTextSize(1);
        homeDrawLabel(s, apps[i].name, x+cw/2, y+ch-14, cw+6);
      }
    }
  } else {
    // ---- baris judul (ikut scroll) ----
    s.setTextSize(1);
    uiText(s,14,STATUS_H+8-scPx,"Accretion",wp?(uint16_t)0xFFFF:T().text,wp,true);

    // ---- widget ----
    HomeRect rc=homeWClock(); drawClockCard(s,rc.x,rc.y-scPx,rc.w,rc.h);
    HomeRect rb=homeWBatt();  drawBattWidget(s,rb.x,rb.y-scPx,rb.w,rb.h);
    HomeRect rm=homeWMusic(); drawMusicMini(s,rm.x,rm.y-scPx,rm.w,rm.h,true);

    // ---- judul grid ----
    s.setTextSize(1);
    uiText(s,14,gridTop-20-scPx,"Semua App",wp?(uint16_t)0xDEFB:T().subtext,wp,false);

    // ---- grid app ----
    for(int i=0;i<APP_COUNT;i++){
      int col=i%cols, row=i/cols;
      int x=gap+col*(cw+gap), y=gridTop+row*(ch+gap)-scPx;
      if(y+ch<clipTop||y>contentBot)continue;
      float pa = homePressAmt(i);
      int cx=x+cw/2, cy=y+(ch-16)/2;
      int rEff = iconR - (int)lroundf(pa*iconR*0.12f);
      drawAppIcon(s, apps[i].sym, cx, cy, rEff, apps[i].color);
      drawNotifBadge(s, cx, cy, iconR, appNotifCount[i]);
      s.setTextColor(T().text);s.setTextSize(1);
      homeDrawLabel(s, apps[i].name, x+cw/2, y+ch-14, cw+6);
    }

    // ---- indikator scroll ----
    int maxSc = homeMaxScroll();
    if(maxSc>0){
      int railX=SCR_W-5, railTop=clipTop+6, railH=contentBot-2-railTop;
      if(railH>20){
        s.fillRoundRect(railX,railTop,3,railH,1,blend565(T().surface,0xFFFF,50));
        int thumbH=max(16,(int)(railH*railH/(float)(railH+maxSc)));
        float fr = sc/(float)maxSc; if(fr<0.0f) fr=0.0f; if(fr>1.0f) fr=1.0f;
        int thumbY=railTop+(int)((railH-thumbH)*fr);
        s.fillRoundRect(railX,thumbY,3,thumbH,1,T().accent);
      }
    }
  }

  // kolom cari (ikon ikut scroll saat tertutup; saat terbuka menempel di atas)
  drawSearchBar(s, homeSearchBar, homeSearchRightX(), homeSearchTopY(), homeSearchInput, "Cari app...");

  if(band) s.setClipRect(0,clipTop,SCR_W,contentH); else s.clearClipRect();

  // dock kaca (statis, dilewati di mode pita)
  if(!band){
    int dockY=homeDockY(), dockH=38;
    drawGlassPanel(s, 6,dockY,SCR_W-12,dockH,18, T().surface2, 150);
    int dockIdx[4]={0,4,6,7};
    int dw=(SCR_W-12)/4;
    int dockIconR=14;
    for(int i=0;i<4;i++){
      int di=dockIdx[i];
      int cx=6+i*dw+dw/2, cy=dockY+dockH/2;
      drawAppIcon(s, apps[di].sym, cx, cy, dockIconR, apps[di].color);
    }
  }
  if(!band && kbVisible && kbTarget==&homeSearchInput) drawKb(s);
  drawToast(s);
  // mode pita: clip pita SENGAJA dibiarkan aktif sampai renderHomeBandFrame() selesai
}

'''
replace_between("Home: drawHome baru", PH,
                "void drawHome(LGFX_Sprite& s,float sc){",
                "Screen homeCheck(int x,int y,float sc){",
                HOME_NEW, "UI-OVERHAUL home \"Aurora Glass\"")

HOMECHECK_NEW = r'''Screen homeCheck(int x,int y,float sc){
  // UI-OVERHAUL: + hit-test widget (jam -> app Jam, baterai -> app Baterai, kartu musik -> kontrol/app Musik)
  int dockY=homeDockY(), dockH=38;
  if(y>=dockY&&y<=dockY+dockH){
    int dockIdx[4]={0,4,6,7};
    int dw=(SCR_W-12)/4;
    for(int i=0;i<4;i++){
      int cx=6+i*dw+dw/2;
      if(abs(x-cx)<dw/2) return apps[dockIdx[i]].screen;
    }
  }
  if(y<homeGridClipTop() || y>=dockY-4) return SCR_HOME;
  int scPx=homeScrollPx(sc);
  if(!homeSearchActive()){
    int yc=y+scPx; // koordinat ruang konten
    HomeRect rm=homeWMusic();
    if(x>=rm.x&&x<=rm.x+rm.w&&yc>=rm.y&&yc<=rm.y+rm.h){
      if(homeMusStarted()){
        int pc,nc; homeMusBtnPos(rm.x,rm.w,pc,nc);
        int cy=rm.y+rm.h/2;
        if(abs(x-pc)<=18&&abs(yc-cy)<=18){ homeMusToggle(); needRedraw=true; return SCR_HOME; }
        if(abs(x-nc)<=16&&abs(yc-cy)<=18){ homeMusNext();   needRedraw=true; return SCR_HOME; }
      }
      return SCR_MUSIC;
    }
    HomeRect rc=homeWClock();
    if(x>=rc.x&&x<=rc.x+rc.w&&yc>=rc.y&&yc<=rc.y+rc.h) return SCR_CLOCK;
    HomeRect rb=homeWBatt();
    if(x>=rb.x&&x<=rb.x+rb.w&&yc>=rb.y&&yc<=rb.y+rb.h) return SCR_BATTERY;
  }
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=10, gridTop=homeGridTop();
  for(int i=0;i<APP_COUNT;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-scPx;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[i].screen;
  }
  return SCR_HOME;
}

'''
replace_between("Home: homeCheck + hit-test widget", PH,
                "Screen homeCheck(int x,int y,float sc){",
                "int homeMaxScroll(){",
                HOMECHECK_NEW, "UI-OVERHAUL: + hit-test widget")

edit("Home: hasil pencarian dari atas (hit-test)", PH,
     "      int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap);\n"
     "      if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return homeFilteredIdx[k];\n",
     "      int ax=gap+col*(cw+gap), ay=homeResultsTop()+row*(ch+gap); // UI-OVERHAUL: hasil dari atas layar\n"
     "      if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return homeFilteredIdx[k];\n",
     "ay=homeResultsTop()+row*(ch+gap)")

edit("Home: posisi ikon cari ikut scroll", PH,
     "int homeSearchTopY(){ return STATUS_H+2; }",
     "extern float homeScrollY; // UI-OVERHAUL: ikon cari ikut scroll saat tertutup (definisi di bawah)\n"
     "int homeSearchTopY(){ if(homeSearchBar.open||homeSearchBar.animating) return STATUS_H+2; return STATUS_H+2-(int)lroundf(homeScrollY); }",
     "extern float homeScrollY; // UI-OVERHAUL")

edit("Home: buka pencarian reset scroll", PH,
     "if(sHit==1){ homeSearchInput=\"\"; searchBarOpen(homeSearchBar); wasTouched=touched; delay(1); return; }",
     "if(sHit==1){ homeScrollY=0; homeScrollVel=0; homeScrollSpringing=false; homeSearchInput=\"\"; searchBarOpen(homeSearchBar); wasTouched=touched; delay(1); return; }",
     "if(sHit==1){ homeScrollY=0;")

edit("Home: mode pita jangan dipakai saat kolom cari terbuka", PH,
     "&& !kbVisible && !searching && !toastVisibleNow()",
     "&& !kbVisible && !searching && !toastVisibleNow() && !homeSearchBar.open && !homeSearchBar.animating",
     "&& !homeSearchBar.open && !homeSearchBar.animating")

# =====================================================================
# 7. MUSIK: API ringan utk widget + redraw 1x/detik
# =====================================================================
edit("Musik: API widget Home/Lock", MU,
     "bool btCcConnected() { return musStarted && musBtOn && musBtConn; }\n",
     "bool btCcConnected() { return musStarted && musBtOn && musBtConn; }\n"
     "\n// ---- API untuk widget musik di Home & Lock Screen (dipanggil dari loop utama) ----\n"
     "bool homeMusStarted() { return musStarted; }\n"
     "bool homeMusLoaded()  { return musStarted && musLoaded; }\n"
     "bool homeMusPlaying() { return musStarted && musLoaded && !musPaused; }\n"
     "float homeMusProgress() {\n"
     "  if (!musStarted || !musLoaded || musDurMs == 0) return 0.0f;\n"
     "  float p = (float)musPlayedMs / (float)musDurMs;\n"
     "  return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);\n"
     "}\n"
     "void homeMusTitle(char* out, int cap) {\n"
     "  if (!musStarted) { strncpy(out, \"Musik\", cap - 1); out[cap - 1] = 0; return; }\n"
     "  musGetTitle(out, cap, musCur);\n"
     "}\n"
     "void homeMusToggle() { if (musStarted) musPost('u', 0, nullptr); }\n"
     "void homeMusNext()   { if (musStarted) musPost('n', 0, nullptr); }\n",
     "bool homeMusStarted()")

edit("Musik: redraw 1x/detik selagi lagu main di Home/Lock", MU,
     "void musicLoopPoll() {\n  if (!musStarted) return;\n  uint32_t now = millis();\n  bool onScr = (curScreen() == SCR_MUSIC);\n",
     "void musicLoopPoll() {\n  if (!musStarted) return;\n  uint32_t now = millis();\n  bool onScr = (curScreen() == SCR_MUSIC);\n"
     "  {   // progres kartu musik di Home/Lock tetap jalan: redraw 1x/detik selagi lagu main\n"
     "    static uint32_t lastWidgetMs = 0;\n"
     "    if ((curScreen() == SCR_HOME || locked) && musLoaded && !musPaused && now - lastWidgetMs >= 1000) {\n"
     "      lastWidgetMs = now; needRedraw = true;\n"
     "    }\n"
     "  }\n",
     "progres kartu musik di Home/Lock tetap jalan")

# =====================================================================
for path, content in cache.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as f:
        f.write(content)
print("\nSelesai. Compile ulang firmware S3 (phone.ino). CAM tidak perlu di-flash ulang.")
