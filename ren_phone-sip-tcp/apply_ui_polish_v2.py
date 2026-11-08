#!/usr/bin/env python3
# UI POLISH V2 -- 5 perubahan tampilan sekaligus (aman diulang, ada penanda UI-POLISH-V2):
#   1. Settings   : chip ikon berwarna di kiri tiap baris (gaya iOS Settings) + header bagian lebih tegas
#   2. Widget Home: kartu jam/baterai/musik/cuaca gaya "bento" (rona tonal, sudut besar, tipografi tegas)
#   3. Keyboard   : tuts datar, radius lebih besar, tuts fungsi tonal, efek tekan
#   4. Buka app   : ikon di Home MEMBESAR jadi layar app (bukan sekadar slide)
#   5. Toast + Dynamic Island: bentuk & animasi muncul lebih halus
# Jalankan dari folder repo (yang ada phone.ino):
#   python3 apply_ui_polish_v2.py
import sys, os

def read(p):  return open(p, encoding="utf-8").read()
def write(p, s): open(p, "w", encoding="utf-8").write(s)

if not os.path.exists("phone.ino") or not os.path.exists("features_renphone.ino"):
    print("ERROR: jalankan dari folder repo (phone.ino & features_renphone.ino harus ada)"); sys.exit(1)

P = read("phone.ino")
F = read("features_renphone.ino")
if "UI-POLISH-V2" in P:
    print("UI-POLISH-V2 sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("GAGAL (%s): teks lama ditemukan %d kali (harus 1). Patch dibatalkan, tidak ada file yang diubah." % (what, n))
        sys.exit(1)
    return src.replace(old, new)

def swap_between(src, start, end, new, what):
    a = src.find(start)
    if a < 0 or src.count(start) != 1:
        print("GAGAL (%s): penanda awal tidak unik/tidak ada" % what); sys.exit(1)
    b = src.find(end, a)
    if b < 0:
        print("GAGAL (%s): penanda akhir tidak ada" % what); sys.exit(1)
    return src[:a] + new + src[b:]

# =====================================================================
# 3. KEYBOARD  (datar, radius besar, tuts fungsi tonal, efek tekan)
# =====================================================================
KB_NEW = r'''// UI-POLISH-V2: keyboard datar. Kode tuts utk efek tekan: baris*10+kolom, 100=shift, 101=123/ABC, 102=spasi, 103=hapus
int kbPressKey=-1;
unsigned long kbPressUntil=0;
#define KB_PRESS_MS 130
void drawKb(LGFX_Sprite& s){
  int y0=kbY();
  bool pressing = (kbPressKey>=0 && millis()<kbPressUntil);
  if(pressing) needRedraw=true; else kbPressKey=-1;   // frame berikutnya menggambar ulang tuts normal
  s.fillRect(0,y0-3,SCR_W,SCR_H-y0+3,blend565(T().bg,T().surface,175));
  s.drawFastHLine(0,y0-3,SCR_W,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  int kw=kbKeyW(), kh=kbKeyH(), L=kbLeft(), R=SCR_W-L;
  const int kr=9;                                              // radius tuts (dulu 6)
  const uint16_t keyC = blend565(T().surface2,0xFFFF,40);      // tuts huruf/angka (datar, tanpa bayangan)
  const uint16_t fnC  = blend565(T().surface2,T().accent,70);  // tuts fungsi: tonal (abu + rona aksen)
  const uint16_t keyP = blend565(keyC,T().accent,110);         // tuts huruf saat ditekan
  const uint16_t fnP  = blend565(fnC,0xFFFF,70);               // tuts fungsi saat ditekan
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2), ox=kbOffCol(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(kw+2);
      bool pr=(kbPressKey==r*10+c);
      int in=pr?1:0;                                           // ditekan: mengecil 1px tiap sisi
      s.fillRoundRect(kx+in,ry+in,kw-2*in,kh-2*in,kr,pr?keyP:keyC);
      const char* lb=lay[r][c];
      s.setTextSize(2); s.setTextColor(T().text);
      int tw=s.textWidth(lb);
      s.setCursor(kx+(kw-tw)/2,ry+(kh-16)/2+in);
      s.print(lb);
    }
  }
  s.setTextSize(1);
  int cy=y0+3*(kh+2);
  int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;
  int dlW=84, dlX=R-dlW;
  int spW=max(40, dlX-4-spX);
  bool up = (kbMode==KB_UPPER);
  uint16_t inkAcc = appIconInk(T().accent);
  { // shift: aksen penuh saat aktif, tonal saat mati
    bool pr=(kbPressKey==100); int in=pr?1:0;
    uint16_t bg = up ? T().accent : (pr?fnP:fnC);
    uint16_t ic = up ? inkAcc : T().text;
    s.fillRoundRect(shX+in,cy+in,shW-2*in,kh-2*in,kr,bg);
    int acx=shX+shW/2, acy=cy+kh/2;
    s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);
    s.fillRect(acx-3,acy+2,6,4,ic);
  }
  { // 123 / ABC
    bool pr=(kbPressKey==101); int in=pr?1:0;
    s.fillRoundRect(nmX+in,cy+in,nmW-2*in,kh-2*in,kr,pr?fnP:fnC);
    const char* nl=(kbMode==KB_NUM)?"ABC":"123";
    s.setTextColor(T().text);
    s.setCursor(nmX+(nmW-s.textWidth(nl))/2,cy+kh/2-4+in); s.print(nl);
  }
  { // spasi (tuts huruf)
    bool pr=(kbPressKey==102); int in=pr?1:0;
    s.fillRoundRect(spX+in,cy+in,spW-2*in,kh-2*in,kr,pr?keyP:keyC);
    s.setTextColor(T().text);
    s.setCursor(spX+(spW-s.textWidth("spasi"))/2,cy+kh/2-4+in); s.print("spasi");
  }
  { // hapus
    bool pr=(kbPressKey==103); int in=pr?1:0;
    s.fillRoundRect(dlX+in,cy+in,dlW-2*in,kh-2*in,kr,pr?fnP:fnC);
    int acx=dlX+dlW/2-4, acy=cy+kh/2;
    s.fillTriangle(acx-8,acy, acx,acy-6, acx,acy+6, T().text);
    s.fillRect(acx,acy-3,10,6,T().text);
  }
}

void kbTouch(int x,int y){
  if(!kbVisible||!kbTarget) return;
  int y0=kbY();
  if(y<y0-2) return;
  int kw=kbKeyW(), kh=kbKeyH(), L=kbLeft(), R=SCR_W-L;
  int cell=kh+2;
  int cy=y0+3*cell;
  if(y>=cy){
    int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;
    int dlX=R-84;
    if(x<nmX-2){            kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER); kbPressKey=100; }
    else if(x<spX-2){       kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);     kbPressKey=101; }
    else if(x<dlX-2){       *kbTarget+=" ";                              kbPressKey=102; }
    else {
      if(kbTarget->length()>0) *kbTarget=kbTarget->substring(0,kbTarget->length()-1);
      kbPressKey=103;
    }
    kbPressUntil=millis()+KB_PRESS_MS;
    needRedraw=true;
    return;
  }
  int r=(y-y0)/cell;
  if(r<0||r>2) return;
  int c=(x-L)/(kw+2);
  if(x<L||c<0||c>9) return;
  auto* lay=kbMaps[(int)kbMode];
  *kbTarget+=lay[r][c];
  kbPressKey=r*10+c; kbPressUntil=millis()+KB_PRESS_MS;
  if(kbMode==KB_UPPER)kbMode=KB_LOWER;
  needRedraw=true;
}

'''
P = swap_between(P, "void drawKb(LGFX_Sprite& s){", "// =============================================\n// TOAST", KB_NEW, "keyboard")

# =====================================================================
# 5a. TOAST (masuk: naik + melebar sedikit memantul; keluar: turun + menyempit)
# =====================================================================
TOAST_NEW = r'''String toastMsg="";
unsigned long toastUntil=0;
// UI-POLISH-V2: toast beranimasi. toastUntil = akhir TAMPIL + animasi keluar, jadi toastVisibleNow() & pemanggil lain tetap valid.
#define TOAST_IN_MS  240
#define TOAST_OUT_MS 200
unsigned long toastStartMs=0;
void showToast(const char* msg,int ms){
  if(dndMode) return;
  toastMsg=msg; toastStartMs=millis(); toastUntil=toastStartMs+(unsigned long)ms+TOAST_OUT_MS;
}
void drawToast(LGFX_Sprite& s){
  unsigned long now=millis();
  if(!toastMsg.length()||now>toastUntil) return;
  unsigned long holdEnd=toastUntil-TOAST_OUT_MS;
  float pin=(float)(now-toastStartMs)/(float)TOAST_IN_MS; if(pin>1.0f) pin=1.0f;
  float pout=(now>holdEnd)?(float)(now-holdEnd)/(float)TOAST_OUT_MS:0.0f; if(pout>1.0f) pout=1.0f;
  float ein=easeOutBackSoft(pin);                // 0 -> ~1.03 -> 1 (memantul tipis)
  float vis=pin*(1.0f-pout*pout); if(vis>1.0f) vis=1.0f;
  needRedraw=true;                               // animasi + penanda waktu keluar
  if(vis<0.12f) return;
  s.setTextSize(1);
  char buf[64]; strncpy(buf,toastMsg.c_str(),sizeof(buf)-1); buf[sizeof(buf)-1]=0;
  int maxTw=SCR_W-16-40; int n=(int)strlen(buf);
  while(n>3 && s.textWidth(buf)>maxTw) buf[--n]=0;
  const int th=26;
  int textW=s.textWidth(buf);
  int fullW=textW+40;                            // titik(26) + teks + padding kanan(14)
  float sc=0.90f+0.10f*ein; sc*=(1.0f-0.06f*pout);
  int tw=(int)(fullW*sc); if(tw<th) tw=th;
  int tx=SCR_W/2-tw/2;
  int ty=SCR_H-40+(int)((1.0f-ein)*14.0f)+(int)(pout*pout*10.0f);
  uint8_t ta=(uint8_t)(205.0f*vis);
  drawGlassPanel(s, tx,ty,tw,th,th/2, T().surface, ta);
  s.drawRoundRect(tx,ty,tw,th,th/2,blend565(T().surface,0xFFFF,60));   // rim tipis
  if(tx+26+textW <= tx+tw-4){                    // teks hanya digambar kalau muat di dalam pil
    uint8_t va=(uint8_t)(255.0f*vis);
    s.fillCircle(tx+14,ty+th/2,3,blend565(T().surface,T().accent,va));
    s.setTextColor(blend565(T().surface,T().text,va)); s.setTextSize(1);
    s.setCursor(tx+26,ty+th/2-4); s.print(buf);
  }
}

'''
P = swap_between(P, 'String toastMsg="";', "// =============================================\n// STATUS BAR", TOAST_NEW, "toast")

# =====================================================================
# 5b. DYNAMIC ISLAND (kurva lebih lembut, isi memudar, rim + bayangan)
# =====================================================================
P = rep(P, "const float DI_ANIM_MS = 380.0f;", "const float DI_ANIM_MS = 420.0f; // UI-POLISH-V2: 380->420, morph lebih lega", "DI durasi")
P = rep(P, "float diSpringEase(float p){\n  const float c1=1.70158f, c3=c1+1.0f;",
           "float diSpringEase(float p){\n  const float c1=0.85f, c3=c1+1.0f; // UI-POLISH-V2: 1.70158 -> 0.85 (mantul tipis, bukan membal)", "DI easing")
P = rep(P, "  float e=diSpringEase(t);\n  float tw=diAnimFromW+(diTargetW()-diAnimFromW)*e;\n  float th=diAnimFromH+(diTargetH()-diAnimFromH)*e;\n",
"""  // UI-POLISH-V2: membesar = pegas lembut (tinggi sedikit mendahului lebar); mengecil = ease-in-out tanpa mantul.
  const bool growing = (diTargetW()+diTargetH()) >= (int)(diAnimFromW+diAnimFromH);
  float tH=t*1.15f; if(tH>1.0f) tH=1.0f;
  float e  = growing ? diSpringEase(t)  : navEase(t);
  float eh = growing ? diSpringEase(tH) : navEase(t);
  float ca = growing ? (t-0.30f)/0.45f : 1.0f-t/0.35f;   // transparansi isi (ikon+teks)
  if(ca<0.0f) ca=0.0f; if(ca>1.0f) ca=1.0f;
  if(t>=1.0f) ca=1.0f;
  float tw=diAnimFromW+(diTargetW()-diAnimFromW)*e;
  float th=diAnimFromH+(diTargetH()-diAnimFromH)*eh;
  if(th<0.0f) th=0.0f; if(tw<0.0f) tw=0.0f;
""", "DI hitung")
P = swap_between(P, "  s.fillRoundRect(x,y,w,h,h/2, isAchv?blend565(0x0000,0xFE05,18):0x0000);", "  if(w>h){\n    uint16_t sheen",
"""  uint16_t pillC = isAchv?blend565(0x0000,0xFE05,18):0x0000; // UI-POLISH-V2
  if(h>=20) shadowSoft(s,x,y,w,h,h/2);                       // bayangan lembut: pil terasa melayang
  s.fillRoundRect(x,y,w,h,h/2,pillC);
  if(h>=14) s.drawRoundRect(x,y,w,h,h/2,blend565(0x0000,0xFFFF,26)); // rim halus
""", "DI pil")
P = rep(P, "    drawAppIcon(s, diIcon, iconCx, iconCy, iconR, diColor);\n    s.setTextColor(0xFFFF); s.setTextSize(1);\n    int textX = iconCx+iconR+6;",
"""    uint16_t icCol = blend565(pillC, diColor, (uint8_t)(255.0f*ca)); // UI-POLISH-V2: isi memudar masuk/keluar
    drawAppIcon(s, diIcon, iconCx, iconCy, iconR, icCol);
    s.setTextColor(blend565(pillC, 0xFFFF, (uint8_t)(255.0f*ca))); s.setTextSize(1);
    int textX = iconCx+iconR+6;""", "DI isi")

# =====================================================================
# 4. BUKA APP: ikon membesar dari posisinya di Home
# =====================================================================
ZOOM = r'''// UI-POLISH-V2: transisi BUKA APP -- ikon/kartu di Home membesar memenuhi layar (bukan sekadar slide).
// Home memanggil navZoomArm() sesaat sebelum navPush(); playNavTransition(PUSH) memakainya SEKALI.
// Kalau tidak di-arm (buka dari App Switcher, notifikasi, dll) -> tetap animasi geser lama.
#ifndef NAV_ZOOM_MS
#define NAV_ZOOM_MS 300UL
#endif
bool navZoomArmed=false;
unsigned long navZoomArmMs=0;
int navZoomX=0,navZoomY=0,navZoomW=0,navZoomH=0,navZoomR=0;
uint16_t navZoomCol=0;
void navZoomArm(int x,int y,int w,int h,int r,uint16_t col){
  navZoomX=x; navZoomY=y; navZoomW=w; navZoomH=h; navZoomR=r; navZoomCol=col;
  navZoomArmed=true; navZoomArmMs=millis();
}
// Fase 1: balok berwarna ikon membesar (sudut membulat -> persegi), warnanya pelan2 menyatu ke warna latar app.
// Fase 2: layar baru "terbuka" di jendela yg terus membesar. Rect hanya TUMBUH, jadi tiap piksel ditulis tanpa sisa.
void navPlayZoomOpen(){
  renderCurrentFrame();   // canvas = layar BARU; panel masih menampilkan Home (frame terakhir)
  const int ox=navZoomX, oy=navZoomY, ow=navZoomW, oh=navZoomH, orad=navZoomR;
  const uint16_t col=navZoomCol, dst=T().bg;
  const unsigned long t0=millis();
  int lx0=-1,ly0=-1,lx1=-1,ly1=-1;
  display.startWrite();
  for(;;){
    uiBoostKick();
    unsigned long el=millis()-t0;
    bool lastFrame = el>=NAV_ZOOM_MS;
    float lin = lastFrame ? 1.0f : (float)el/(float)NAV_ZOOM_MS;
    float inv=1.0f-lin;
    float ez=1.0f-inv*inv*inv;                            // ease-out cubic: cepat di awal, mendarat halus
    int x0=(int)lroundf(ox*(1.0f-ez));
    int y0=(int)lroundf(oy*(1.0f-ez));
    int x1=(int)lroundf((ox+ow)*(1.0f-ez)+SCR_W*ez);
    int y1=(int)lroundf((oy+oh)*(1.0f-ez)+SCR_H*ez);
    if(x0<0) x0=0; if(y0<0) y0=0; if(x1>SCR_W) x1=SCR_W; if(y1>SCR_H) y1=SCR_H;
    if(x1>x0 && y1>y0 && (x0!=lx0||y0!=ly0||x1!=lx1||y1!=ly1)){
      lx0=x0; ly0=y0; lx1=x1; ly1=y1;
      int w=x1-x0, h=y1-y0;
      if(ez<0.5f){
        float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;
        int rr=(int)lroundf(orad*(1.0f-cr));
        if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;
        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));
      } else {
        display.setClipRect(x0,y0,w,h);
        canvas.pushSprite(0,0);
        display.clearClipRect();
      }
    }
    if(lastFrame) break;
    yield();
  }
  display.endWrite();
  push();                  // frame akhir persis sama dgn 'canvas'
}
'''
P = rep(P, "void playNavTransition(NavAnim anim){\n  canvas.pushSprite(&transShot,0,0);",
           ZOOM + "void playNavTransition(NavAnim anim){\n  if(anim==NAV_ANIM_PUSH && navZoomArmed){   // UI-POLISH-V2\n    navZoomArmed=false;\n    if(millis()-navZoomArmMs<1500UL){ navPlayZoomOpen(); return; }\n  }\n  canvas.pushSprite(&transShot,0,0);", "nav zoom")

HOME_ARM = r'''
// UI-POLISH-V2: hitung asal animasi zoom (ikon grid / ikon dock / kartu widget) lalu arm navZoomArm().
// Geometri disalin dari drawHome()/homeCheck() -- kalau layout Home diubah, ubah di sini juga.
void homeArmZoom(int idx,int tx,int ty,float sc){
  int scPx=homeScrollPx(sc);
  int dockY=homeDockY(), dockH=38;
  if(idx>=0){
    int cols=homeCols(), cw=homeCardW(), ch=homeCardH(), gap=10;
    int iconR=min(cw,ch-16)/2-2; if(iconR>24) iconR=24; if(iconR<14) iconR=14;
    bool srch=homeSearchActive(); int k=-1;
    if(srch){ for(int q=0;q<homeFilteredCount;q++) if(homeFilteredIdx[q]==idx){ k=q; break; } }
    else { homeLayoutEnsure(); for(int q=0;q<homeVisN;q++) if(homeVis[q]==idx){ k=q; break; } }
    if(k<0) return;
    int col=k%cols, row=k/cols;
    int x=gap+col*(cw+gap);
    int y= srch ? homeResultsTop()+row*(ch+gap) : homeGridTop()+row*(ch+gap)-scPx;
    int cx=x+cw/2, cy=y+(ch-16)/2;
    navZoomArm(cx-iconR,cy-iconR,iconR*2,iconR*2,(iconR*11)/20,apps[idx].color);
    return;
  }
  if(ty>=dockY && ty<=dockY+dockH){
    int dockIdx[4]={0,4,6,7}; int dw=(SCR_W-12)/4;
    for(int i=0;i<4;i++){
      int cx=6+i*dw+dw/2;
      if(abs(tx-cx)<dw/2){ int r=14; navZoomArm(cx-r,dockY+dockH/2-r,r*2,r*2,(r*11)/20,apps[dockIdx[i]].color); return; }
    }
    return;
  }
  if(homeSearchActive() || ty<homeGridClipTop() || ty>=dockY-4) return;
  int yc=ty+scPx;
  HomeRect rm=homeWMusic(), rc=homeWClock(), rb=homeWBatt();
  const HomeRect* hit=nullptr;
  if(tx>=rm.x&&tx<=rm.x+rm.w&&yc>=rm.y&&yc<=rm.y+rm.h) hit=&rm;
  else if(tx>=rc.x&&tx<=rc.x+rc.w&&yc>=rc.y&&yc<=rc.y+rc.h) hit=&rc;
  else if(tx>=rb.x&&tx<=rb.x+rb.w&&yc>=rb.y&&yc<=rb.y+rb.h) hit=&rb;
  if(hit) navZoomArm(hit->x,hit->y-scPx,hit->w,hit->h,22,T().surface2);
}
'''
P = rep(P, "// =============================================\n// GAME MODE: dipanggil tiap masuk ke game apapun.",
           HOME_ARM.lstrip("\n") + "\n// =============================================\n// GAME MODE: dipanggil tiap masuk ke game apapun.", "home arm fn")
P = rep(P, "          navPush(nx); // v96: getar tap navigasi dicabut",
           "          homeArmZoom(idx,touchStartX,touchStartY,homeScrollY); // UI-POLISH-V2: ikon membesar jadi layar app\n          navPush(nx); // v96: getar tap navigasi dicabut", "home arm call")

# =====================================================================
# 2. WIDGET HOME "bento" (jam, baterai, musik; cuaca di features_renphone.ino)
# =====================================================================
# jarak antar kartu seragam 8px
P = rep(P, "r.y=STATUS_H+28+66+6; r.h=48;", "r.y=STATUS_H+28+66+8; r.h=48;", "gap musik")
P = rep(P, "r.y=max(b.y+b.h,m.y+m.h)+6;", "r.y=max(b.y+b.h,m.y+m.h)+8;", "gap cuaca")

BENTO = r'''// UI-POLISH-V2 "bento": tiap kartu punya rona tonal sendiri, sudut besar, tipografi lebih tegas
uint16_t bentoTint(uint16_t c,uint8_t a){ return blend565(T().surface,c,a); }
int bentoR(int h,int want){ int r=want; if(r>h/2) r=h/2; return r; }

// Kartu musik: cover piringan, judul, status, progres, (opsional) tombol kontrol
void drawMusicMini(LGFX_Sprite& s,int x,int y,int w,int h,bool ctrl){
  drawGlassCard(s,x,y,w,h,bentoR(h,22),bentoTint(T().accent2,70),185);
  int cy=y+h/2;
  bool started=homeMusStarted(), loaded=homeMusLoaded(), playing=homeMusPlaying();
  int cr=(h>=50)?19:15, ccx=x+14+cr;
  uint16_t disc=T().accent2;
  s.fillCircle(ccx,cy,cr,disc);
  s.drawCircle(ccx,cy,cr-4,blend565(disc,0x0000,90));
  s.fillCircle(ccx,cy,cr/3+1,blend565(disc,0xFFFF,130));
  s.fillCircle(ccx,cy,2,blend565(disc,0x0000,170));
  int tx=ccx+cr+12;
  int pc,nc; homeMusBtnPos(x,w,pc,nc);
  int maxX=(ctrl&&started)?(pc-20):(x+w-34);
  char title[56];
  if(started) homeMusTitle(title,sizeof(title)); else strcpy(title,"Musik");
  const char* sub=!started?"Ketuk untuk membuka":(!loaded?"Siap diputar":(playing?"Sedang diputar":"Dijeda"));
  s.setTextSize(1);
  int n=(int)strlen(title); bool cut=false;
  while(n>3 && s.textWidth(title)>(maxX-tx)){ title[--n]=0; cut=true; }
  if(cut && n>=2){ title[n-1]='.'; title[n-2]='.'; }
  int ty1=(h>=50)?y+10:y+8, ty2=(h>=50)?y+24:y+20;
  uiText(s,tx,ty1,title,T().text,false,true);
  s.setTextColor(blend565(T().subtext,T().accent2,70)); s.setCursor(tx,ty2); s.print(sub);
  if(started&&loaded){ // progres tipis
    int bx=tx, bw=maxX-tx, by=y+h-12;
    if(bw>10){
      s.fillRoundRect(bx,by,bw,4,2,blend565(T().surface,0xFFFF,60));
      int fw=(int)(bw*homeMusProgress());
      if(fw>0) s.fillRoundRect(bx,by,max(fw,4),4,2,T().accent);
    }
  }
  uint16_t ink=appIconInk(T().accent);
  if(ctrl&&started){
    s.fillCircle(pc,cy,15,T().accent);
    if(playing){ s.fillRect(pc-5,cy-6,4,12,ink); s.fillRect(pc+1,cy-6,4,12,ink); }
    else s.fillTriangle(pc-4,cy-7,pc-4,cy+7,pc+6,cy,ink);
    s.fillCircle(nc,cy,12,blend565(T().surface2,0xFFFF,40));
    s.fillTriangle(nc-5,cy-5,nc-5,cy+5,nc+3,cy,T().text);
    s.fillRect(nc+4,cy-5,2,10,T().text);
  } else if(!ctrl && started && loaded){
    int gx=x+w-22;
    if(playing){ s.fillRect(gx-4,cy-6,3,12,T().accent); s.fillRect(gx+1,cy-6,3,12,T().accent); }
    else s.fillTriangle(gx-4,cy-6,gx-4,cy+6,gx+5,cy,T().accent);
  }
}

// Kartu jam: sapaan (kiri-atas) + tanggal (kanan-atas), jam besar tebal di bawah
void drawClockCard(LGFX_Sprite& s,int x,int y,int w,int h){
  drawGlassCard(s,x,y,w,h,bentoR(h,24),bentoTint(T().accent,80),185);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  s.setTextSize(1);
  uiText(s,x+16,y+10,ok?homeGreeting(t.tm_hour):"Halo",T().accent,false,true);
  if(ok){
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[24]; snprintf(db,sizeof(db),"%s, %d %s",days[t.tm_wday],t.tm_mday,mons[t.tm_mon]);
    s.setTextColor(T().subtext); s.setCursor(x+w-16-s.textWidth(db),y+10); s.print(db);
  }
  s.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  // angka jam: tinggi dari tinggi kartu (ruang atas utk sapaan/tanggal), lalu dikecilkan sampai muat lebar
  float sz=(float)(h-34)/14.0f; if(sz>4.5f) sz=4.5f; sz=floorf(sz*4.0f)/4.0f; if(sz<1.5f) sz=1.5f;
  s.setTextSize(sz);
  while(sz>1.5f && s.textWidth(tb)>w-32){ sz-=0.25f; s.setTextSize(sz); }
  s.setTextColor(T().text);
  s.setTextDatum(lgfx::textdatum_t::baseline_left);
  s.drawString(tb,x+16,y+h-12);
  s.setTextDatum(lgfx::textdatum_t::top_left);
  s.setFont(&lgfx::fonts::Font0); // Home selalu Font0 (lihat renderCurrentFrame)
  s.setTextSize(1);
}

// Kartu baterai: cincin (kartu tinggi) atau bar horizontal (kartu pendek); rona kartu ikut level baterai
void drawBattWidget(LGFX_Sprite& s,int x,int y,int w,int h){
  uint16_t bc=battColor();
  drawGlassCard(s,x,y,w,h,bentoR(h,22),bentoTint(bc,70),185);
  char pb[8]; sprintf(pb,"%d",battPercent);
  if(h>=60){
    int cx=x+w/2, cy=y+h/2;
    int ro=min(h/2-6,w/2-6), ri=ro-7;
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
    uiText(s,cx-tw/2,cy-th/2,pb,T().text,false,true);
    s.setTextSize(1);
  } else {
    s.setTextSize(1);
    uiText(s,x+16,y+9,"Baterai",T().text,false,true);
    char pf[8]; sprintf(pf,"%d%%",battPercent);
    int tw=s.textWidth(pf);
    uiText(s,x+w-16-tw,y+9,pf,bc,false,true);
    int bx=x+16, bw=w-32, by=y+h-15;
    s.fillRoundRect(bx,by,bw,8,4,blend565(T().surface,0xFFFF,50));
    int fw=(bw*battPercent)/100;
    if(fw>0) s.fillRoundRect(bx,by,max(fw,8),8,4,bc);
  }
}

'''
P = swap_between(P, "// Kartu musik: cover piringan, judul, status, progres, (opsional) tombol kontrol", "// STATUS BAR: di Home transparan", BENTO, "bento")

# widget cuaca (features_renphone.ino)
F = rep(F, "  drawGlassCard(s, x, y, w, h, 18, T().surface, 150);\n  int cy = y + h / 2;",
           "  drawGlassCard(s, x, y, w, h, bentoR(h, 22), bentoTint((uint16_t)0x357C, 70), 185); // UI-POLISH-V2 bento: rona langit\n  int cy = y + h / 2;", "cuaca kartu")
F = rep(F, "  s.setTextSize(2); s.setTextColor(T().text);\n  s.setCursor(tx, cy - 8); s.print(tb);\n",
           "  s.setTextSize(2); s.setTextColor(T().text);\n  s.setCursor(tx, cy - 8); s.print(tb);\n  s.setCursor(tx + 1, cy - 8); s.print(tb);   // UI-POLISH-V2: suhu tebal\n", "cuaca suhu")
F = rep(F, "  s.setTextColor(T().text);\n  s.setCursor(dx, cy - 11); s.print(wxDesc(wxCode));\n",
           "  uiText(s, dx, cy - 11, wxDesc(wxCode), T().text, false, true);   // UI-POLISH-V2: deskripsi tebal\n", "cuaca desc")

# =====================================================================
# 1. SETTINGS: chip ikon berwarna + header bagian tegas
# =====================================================================
CHIP = r'''
// UI-POLISH-V2 -------- chip ikon berwarna ala iOS Settings (22x22, sudut 6) + glyph putih kecil
#define CHIP_BLUE   0x0C3F
#define CHIP_GREEN  0x362B
#define CHIP_ORANGE 0xFCA0
#define CHIP_RED    0xFA27
#define CHIP_PURPLE 0xAA9B
#define CHIP_INDIGO 0x5ABA
#define CHIP_GRAY   0x8C72
#define CHIP_TEAL   0x357C
#define CHIP_PINK   0xF96A
bool settHasChip(int t){
  switch(t){
    case SR_BRIGHT: case SR_THEME: case SR_FONT: case SR_ROT: case SR_HOMEEDIT: case SR_SPI:
    case SR_SCAN: case SR_SSID: case SR_PASS: case SR_MPU: case SR_TCAL: case SR_ABOUT: case SR_POWER:
      return true;
  }
  return false;
}
void settChip(LGFX_Sprite& s,int type,int x,int y){ // x,y = pojok kiri-atas chip
  uint16_t col=CHIP_GRAY;
  switch(type){
    case SR_BRIGHT:   col=CHIP_BLUE;   break;
    case SR_THEME:    col=CHIP_PINK;   break;
    case SR_FONT:     col=CHIP_INDIGO; break;
    case SR_ROT:      col=CHIP_GREEN;  break;
    case SR_HOMEEDIT: col=CHIP_PURPLE; break;
    case SR_SPI:      col=CHIP_ORANGE; break;
    case SR_SCAN:     col=CHIP_BLUE;   break;
    case SR_SSID:     col=CHIP_TEAL;   break;
    case SR_PASS:     col=CHIP_GRAY;   break;
    case SR_MPU:      col=CHIP_INDIGO; break;
    case SR_TCAL:     col=CHIP_RED;    break;
    case SR_ABOUT:    col=CHIP_GRAY;   break;
    case SR_POWER:    col=CHIP_RED;    break;
    default: return;
  }
  const uint16_t W=0xFFFF;
  int cx=x+11, cy=y+11;
  s.fillRoundRect(x,y+1,22,22,6,blend565(col,0x0000,70));   // tepi bawah tipis
  s.fillRoundRect(x,y,22,22,6,col);
  s.drawFastHLine(x+6,y+1,10,blend565(col,0xFFFF,70));      // kilap atas
  switch(type){
    case SR_BRIGHT: {  // matahari
      s.fillCircle(cx,cy,3,W);
      static const int dxs[8]={0,1,1,1,0,-1,-1,-1}, dys[8]={-1,-1,0,1,1,1,0,-1};
      for(int i=0;i<8;i++){ int r1=(i&1)?6:7; s.drawLine(cx+dxs[i]*5,cy+dys[i]*5,cx+dxs[i]*r1,cy+dys[i]*r1,W); }
      } break;
    case SR_THEME:     // palet
      s.fillCircle(cx,cy,7,W);
      s.fillCircle(cx-3,cy-2,1,col); s.fillCircle(cx+1,cy-4,1,col); s.fillCircle(cx+3,cy,1,col); s.fillCircle(cx-1,cy+3,1,col);
      break;
    case SR_FONT:      // "Aa"
      s.setTextSize(1); s.setTextColor(W); s.setCursor(cx-6,cy-4); s.print("Aa");
      break;
    case SR_ROT:       // panah putar
      s.fillArc(cx,cy,4,6,40.0f,320.0f,W);
      s.fillTriangle(cx+2,cy-9,cx+2,cy-3,cx+8,cy-6,W);
      break;
    case SR_HOMEEDIT:  // kisi 2x2
      s.fillRoundRect(cx-6,cy-6,5,5,1,W); s.fillRoundRect(cx+1,cy-6,5,5,1,W);
      s.fillRoundRect(cx-6,cy+1,5,5,1,W); s.fillRoundRect(cx+1,cy+1,5,5,1,W);
      break;
    case SR_SPI:       // petir
      s.fillTriangle(cx+2,cy-8,cx-4,cy+1,cx+1,cy+1,W);
      s.fillTriangle(cx-1,cy-1,cx+4,cy-1,cx-2,cy+8,W);
      break;
    case SR_SCAN:      // wifi
      s.fillCircle(cx,cy+6,1,W);
      s.drawArc(cx,cy+6,5,4,210,330,W); s.drawArc(cx,cy+6,8,7,210,330,W); s.drawArc(cx,cy+6,11,10,210,330,W);
      break;
    case SR_SSID:      // globe
      s.drawCircle(cx,cy,7,W); s.drawFastHLine(cx-7,cy,15,W); s.drawFastVLine(cx,cy-7,15,W); s.drawCircle(cx,cy,3,W);
      break;
    case SR_PASS:      // gembok
      s.fillRoundRect(cx-5,cy-1,11,8,2,W);
      s.drawFastVLine(cx-3,cy-6,5,W); s.drawFastVLine(cx+3,cy-6,5,W); s.drawFastHLine(cx-3,cy-7,7,W);
      s.fillCircle(cx,cy+3,1,col);
      break;
    case SR_MPU:       // bidik
      s.drawCircle(cx,cy,5,W); s.fillCircle(cx,cy,1,W);
      s.drawFastHLine(cx-8,cy,4,W); s.drawFastHLine(cx+5,cy,4,W);
      s.drawFastVLine(cx,cy-8,4,W); s.drawFastVLine(cx,cy+5,4,W);
      break;
    case SR_TCAL:      // riak sentuh
      s.fillCircle(cx,cy,2,W); s.drawCircle(cx,cy,5,W); s.drawCircle(cx,cy,8,W);
      break;
    case SR_ABOUT:     // "i"
      s.fillCircle(cx,cy-5,2,W); s.fillRect(cx-1,cy-1,3,8,W);
      break;
    case SR_POWER:     // tombol daya
      s.fillArc(cx,cy+1,5,7,300.0f,360.0f,W); s.fillArc(cx,cy+1,5,7,0.0f,240.0f,W);
      s.fillRect(cx-1,cy-8,3,8,W);
      break;
  }
}
'''
a = P.index("void settChevron(LGFX_Sprite& s,int cx,int cy,uint16_t col){")
b = P.index("\n}\n", a) + 3
P = P[:b] + CHIP + P[b:]

# garis pemisah mulai dari teks (bukan dari tepi) utk baris ber-chip
P = rep(P, "        if(ly>top && ly<bot) s.drawFastHLine(cx+14,ly,cw-14,T().divider);",
           "        if(ly>top && ly<bot){ int lx=settHasChip(settRT[k])?cx+44:cx+14; s.drawFastHLine(lx,ly,cx+cw-lx,T().divider); } // UI-POLISH-V2", "settings divider")
P = rep(P, "drawGlassCard(s,cx,gy,cw,gh,12,T().surface,215);", "drawGlassCard(s,cx,gy,cw,gh,14,T().surface,215);", "settings radius")
# chip + geser teks
P = rep(P, "    int xl=cx+14, xr=cx+cw-14;\n    s.setTextSize(1);\n    switch(settRT[i]){",
           "    int xl=cx+14, xr=cx+cw-14;\n    if(settHasChip(settRT[i])){ // UI-POLISH-V2: chip ikon di kiri, teks bergeser\n      xl=cx+44; settChip(s,settRT[i],cx+12,(settRT[i]==SR_THEME)?ry+8:ry+(rh-22)/2);\n    }\n    s.setTextSize(1);\n    switch(settRT[i]){", "settings chip draw")
# header bagian tegas
P = rep(P, "      case SR_SECT:\n        s.setTextColor(T().subtext); s.setCursor(cx+14,ry+rh-13); s.print(settRL[i]);\n        break;",
           "      case SR_SECT: // UI-POLISH-V2: batang aksen + teks tebal\n        s.fillRoundRect(cx+2,ry+rh-14,3,10,1,T().accent);\n        uiText(s,cx+11,ry+rh-13,settRL[i],blend565(T().subtext,T().text,110),false,true);\n        break;", "settings header")
# slider kecerahan & pemilih tema: geser karena label bergeser 30px (gambar + hit-test harus sama)
P = rep(P, "        int x0=cx+92, x1=cx+cw-50, my=ry+rh/2;", "        int x0=cx+110, x1=cx+cw-50, my=ry+rh/2;", "slider gambar")
P = rep(P, "  int cx=10, cw=SCR_W-20; int x0=cx+92, x1=cx+cw-50;", "  int cx=10, cw=SCR_W-20; int x0=cx+110, x1=cx+cw-50;", "slider hit")
P = rep(P, "\n        int x0=cx+64, step=(cw-76)/THEME_COUNT;", "\n        int x0=cx+72, step=(cw-84)/THEME_COUNT;", "tema gambar")
P = rep(P, "\n      int x0=cx+64, step=(cw-76)/THEME_COUNT;", "\n      int x0=cx+72, step=(cw-84)/THEME_COUNT;", "tema hit")
P = rep(P, 'String(\"Ketuk untuk isi\"),cw-28-50);'.replace('\\"','"'), 'String("Ketuk untuk isi"),cw-28-50-30);', "ssid lebar")
P = rep(P, "v=settFit(s,v,cw-28-50-44);", "v=settFit(s,v,cw-28-50-44-30);", "pass lebar")

# penanda + simpan
P = rep(P, "// =============================================\n// VIRTUAL KEYBOARD\n// =============================================",
           "// =============================================\n// VIRTUAL KEYBOARD  (UI-POLISH-V2: datar + efek tekan)\n// =============================================", "penanda")
write("phone.ino", P)
write("features_renphone.ino", F)
print("OK: UI-POLISH-V2 terpasang (Settings chip, widget bento, keyboard datar, zoom buka app, toast & Dynamic Island halus)")
