#!/usr/bin/env python3
# UI POLISH V3 (jalankan SETELAH apply_ui_polish_v2.py; aman diulang, penanda UI-POLISH-V3):
#   1. FIX toast: showToast() yang dipanggil berulang (mis. ketuk widget cuaca beberapa kali) dulu
#      mengulang animasi masuk dari nol -> panel setengah transparan & teks redup. Sekarang animasi
#      masuk hanya diputar kalau toast sedang tidak tampil, dan fade-in dipercepat.
#   2. FITUR: tutup app mengecil kembali ke ikonnya di Home (kebalikan animasi buka).
#      Berlaku saat kembali ke Home lewat Back (dari app tingkat pertama) atau tombol/gestur Home,
#      untuk app yang dibuka dari Home (ikon grid, dock, kartu widget).
# Jalankan dari folder repo:  python3 apply_ui_polish_v3.py
import sys, os

def read(p):  return open(p, encoding="utf-8").read()
def write(p, s): open(p, "w", encoding="utf-8").write(s)

if not os.path.exists("phone.ino"):
    print("ERROR: jalankan dari folder repo (phone.ino harus ada)"); sys.exit(1)
P = read("phone.ino")
if "UI-POLISH-V3" in P:
    print("UI-POLISH-V3 sudah terpasang, dilewati"); sys.exit(0)
if "UI-POLISH-V2" not in P:
    print("ERROR: UI-POLISH-V2 belum terpasang. Jalankan apply_ui_polish_v2.py dulu."); sys.exit(1)

def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("GAGAL (%s): teks lama ditemukan %d kali (harus 1). Patch dibatalkan, tidak ada file yang diubah." % (what, n))
        sys.exit(1)
    return src.replace(old, new)

# ---------------------------------------------------------------- 1. FIX TOAST
P = rep(P,
"  toastMsg=msg; toastStartMs=millis(); toastUntil=toastStartMs+(unsigned long)ms+TOAST_OUT_MS;",
"""  // UI-POLISH-V3: animasi masuk HANYA diputar kalau toast sedang tidak tampil (atau sedang keluar).
  // Panggilan berulang saat toast masih tampil cuma ganti teks + perpanjang waktu, tanpa mengulang fade-in.
  unsigned long nowMs=millis();
  bool held = toastMsg.length() && nowMs<toastUntil && (toastUntil-nowMs)>TOAST_OUT_MS;
  if(!held) toastStartMs=nowMs;
  toastMsg=msg; toastUntil=nowMs+(unsigned long)ms+TOAST_OUT_MS;""", "toast restart")
P = rep(P,
"  float vis=pin*(1.0f-pout*pout); if(vis>1.0f) vis=1.0f;",
"  float fin=pin*2.5f; if(fin>1.0f) fin=1.0f;      // UI-POLISH-V3: fade-in cepat (tembus-pandang cuma sekejap)\n  float vis=fin*(1.0f-pout*pout);", "toast fade")

# ---------------------------------------------------------------- 2. ZOOM TUTUP
P = rep(P, "uint16_t navZoomCol=0;\n",
"""uint16_t navZoomCol=0;
// UI-POLISH-V3: salinan asal zoom utk animasi TUTUP (disimpan saat app dibuka dari Home)
bool navZoomSaved=false;
int navZoomSX=0,navZoomSY=0,navZoomSW=0,navZoomSH=0,navZoomSR=0,navZoomScr=0;
uint16_t navZoomSCol=0;
""", "vars tutup")

P = rep(P,
"  const int ox=navZoomX, oy=navZoomY, ow=navZoomW, oh=navZoomH, orad=navZoomR;\n  const uint16_t col=navZoomCol, dst=T().bg;\n  const unsigned long t0=millis();\n  int lx0=-1,ly0=-1,lx1=-1,ly1=-1;",
"""  const int ox=navZoomX, oy=navZoomY, ow=navZoomW, oh=navZoomH, orad=navZoomR;
  const uint16_t col=navZoomCol, dst=T().bg;
  navZoomSX=ox; navZoomSY=oy; navZoomSW=ow; navZoomSH=oh; navZoomSR=orad; navZoomSCol=col; // UI-POLISH-V3
  navZoomScr=SCR_W*1000+SCR_H; navZoomSaved=true;
  const unsigned long t0=millis();
  int lx0=-1,ly0=-1,lx1=-1,ly1=-1;""", "simpan asal")

CLOSE = r'''// UI-POLISH-V3: animasi TUTUP = kebalikan animasi buka. Layar app menyusut (jendela terpotong) lalu jadi balok
// berwarna ikon yg mengecil ke posisi ikon di Home. Home digambar ulang HANYA di area yg baru "terbuka" (4 strip),
// jadi tiap piksel ditulis sekali per frame; isi app di dalam jendela tidak perlu digambar ulang.
#ifndef NAV_ZOOM_CLOSE_MS
#define NAV_ZOOM_CLOSE_MS 260UL
#endif
void navZoomPushHome(int x,int y,int w,int h){   // tempel potongan Home (canvas) ke panel
  if(w<=0||h<=0) return;
  display.setClipRect(x,y,w,h);
  canvas.pushSprite(0,0);
  display.clearClipRect();
}
void navPlayZoomClose(){
  renderCurrentFrame();   // canvas = Home; panel masih menampilkan layar app (frame terakhir)
  const int ox=navZoomSX, oy=navZoomSY, ow=navZoomSW, oh=navZoomSH, orad=navZoomSR;
  const uint16_t col=navZoomSCol, dst=T().bg;
  const unsigned long t0=millis();
  int px0=0,py0=0,px1=SCR_W,py1=SCR_H;            // rect frame sebelumnya (selalu mengandung rect baru)
  display.startWrite();
  for(;;){
    uiBoostKick();
    unsigned long el=millis()-t0;
    bool lastFrame = el>=NAV_ZOOM_CLOSE_MS;
    float u = lastFrame ? 1.0f : (float)el/(float)NAV_ZOOM_CLOSE_MS;
    float q=1.0f-u;
    float ez=q*q;                                  // 1 -> 0, cepat di awal lalu melandai ke ikon
    int x0=(int)lroundf(ox*(1.0f-ez));
    int y0=(int)lroundf(oy*(1.0f-ez));
    int x1=(int)lroundf((ox+ow)*(1.0f-ez)+SCR_W*ez);
    int y1=(int)lroundf((oy+oh)*(1.0f-ez)+SCR_H*ez);
    if(x0<px0) x0=px0; if(y0<py0) y0=py0; if(x1>px1) x1=px1; if(y1>py1) y1=py1;
    if(x1>x0 && y1>y0 && (x0!=px0||y0!=py0||x1!=px1||y1!=py1)){
      int w=x1-x0, h=y1-y0;
      navZoomPushHome(px0,py0,px1-px0,y0-py0);     // strip atas
      navZoomPushHome(px0,y1,px1-px0,py1-y1);      // strip bawah
      navZoomPushHome(px0,y0,x0-px0,h);            // strip kiri
      navZoomPushHome(x1,y0,px1-x1,h);             // strip kanan
      if(ez<0.5f){
        float cr=ez*1.6f; if(cr>1.0f) cr=1.0f;
        int rr=(int)lroundf(orad*(1.0f-cr));
        if(rr>w/2) rr=w/2; if(rr>h/2) rr=h/2; if(rr<0) rr=0;
        if(rr>0){                                  // pojok yg terpotong lengkungan balok -> kembalikan isi Home
          navZoomPushHome(x0,y0,rr,rr); navZoomPushHome(x1-rr,y0,rr,rr);
          navZoomPushHome(x0,y1-rr,rr,rr); navZoomPushHome(x1-rr,y1-rr,rr,rr);
        }
        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));
      }
      px0=x0; py0=y0; px1=x1; py1=y1;
    }
    if(lastFrame) break;
    yield();
  }
  display.endWrite();
  push();                  // frame akhir = Home penuh (ikon asli muncul lagi)
}
'''
P = rep(P, "void playNavTransition(NavAnim anim){\n  if(anim==NAV_ANIM_PUSH && navZoomArmed){   // UI-POLISH-V2",
           CLOSE + "void playNavTransition(NavAnim anim){\n  if((anim==NAV_ANIM_BACK||anim==NAV_ANIM_HOME) && navDepth==0 && navZoomSaved){ // UI-POLISH-V3: tutup -> mengecil ke ikon\n    navZoomSaved=false;\n    if(navZoomScr==SCR_W*1000+SCR_H){ navPlayZoomClose(); return; }   // orientasi berubah -> asal tidak valid, pakai geser biasa\n  }\n  if(anim==NAV_ANIM_PUSH && navZoomArmed){   // UI-POLISH-V2", "play tutup")

# app dibuka dari Home TANPA zoom (App Switcher, notifikasi, dll) -> buang asal lama supaya tidak mengecil ke ikon yg salah
P = rep(P, "  playNavTransition(NAV_ANIM_PUSH);\n  needRedraw=false; // frame akhir sudah ke-push oleh playNavTransition",
           "  if(navDepth==1 && !navZoomArmed) navZoomSaved=false; // UI-POLISH-V3\n  playNavTransition(NAV_ANIM_PUSH);\n  needRedraw=false; // frame akhir sudah ke-push oleh playNavTransition", "navPush invalidasi")

write("phone.ino", P)
print("OK: UI-POLISH-V3 terpasang (fix toast + animasi tutup mengecil ke ikon)")
