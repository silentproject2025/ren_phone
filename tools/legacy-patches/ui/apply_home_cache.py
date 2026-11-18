#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_home_cache.py (HOME_CACHE_V1) -- scroll Home dari "cache konten".

Masalah: tiap frame scroll Home, mode pita (v106) menggambar ulang widget kaca, chip, dan
semua ikon grid (lingkaran AA, alpha blend) ke canvas di PSRAM, lalu mengirim pitanya.
Di layar 480x320 pita itu ~2x lebih besar dari ILI9341 -> FPS turun & jarak antar frame tidak rata.

Perbaikan: seluruh konten Home (judul, widget, chip, grid) digambar SEKALI ke sprite tinggi
(homeCache, PSRAM) pada scroll 0. Frame scroll tinggal menyalin potongan sprite itu ke canvas
(satu pushSprite berklip), lalu menggambar hal murah di atasnya (indikator scroll, kolom cari, toast).

Aman-aman:
 - Hanya aktif kalau TIDAK ada wallpaper (tanpa wallpaper blend kaca persis sama; dgn wallpaper
   isi kaca harus ikut berubah saat scroll, jadi jalur lama dipakai).
 - Tidak dipakai saat: pencarian, mode jiggle/atur, ikon menyusut, animasi tekan/pantul ikon,
   banner panggilan, thumbnail screenshot, menu Daya -> jalur pita lama.
 - Cache dibuang tiap ada frame PENUH Home (ada yg berubah) dan saat keluar dari Home / terkunci
   (supaya PSRAM bebas utk Doom/NES). Dibangun ulang lazily di frame scroll berikutnya.
 - Butuh PSRAM bebas >= ukuran cache + 1,5 MB, kalau tidak -> jalur lama.
 - -DRP_HOME_CACHE=0 mematikan semuanya.

Jalankan dari root repo. Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "HOME_CACHE_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: blok '%s' ketemu %d kali (harus 1). Dibatalkan, phone.ino tidak diubah." % (label, n))
    s = s.replace(old, new)

# ---- 1. global ----
rep("bool homeBandReady=false;\n",
"""bool homeBandReady=false;
// HOME_CACHE_V1 (apply_home_cache.py): konten Home digambar sekali ke homeCache, scroll = salin potongan.
#ifndef RP_HOME_CACHE
#define RP_HOME_CACHE 1
#endif
LGFX_Sprite homeCache(&display);
bool homeCacheValid=false;
int  homeCacheW=0, homeCacheH=0;
static void homeCacheFree(){ if(homeCache.getBuffer()) homeCache.deleteSprite(); homeCacheValid=false; homeCacheW=homeCacheH=0; }
""", "global")

# ---- 2. fungsi cache + hook di renderHomeBandFrame ----
rep("void renderHomeBandFrame(){\n  int bt=homeGridClipTop(), bh=(homeDockY()-4)-bt;\n",
r'''// HOME_CACHE_V1: syarat cache boleh dipakai di frame ini (semua yg dinamis harus diam).
static bool homeCacheUsable(){
#if RP_HOME_CACHE
  if(wallpaperReady || homeSearchActive() || homeJiggle || homeGoneIdx>=0 || homePressAnimIdx>=0 || homePressedIdx>=0) return false;
  bool dockOnly=false; if(appBounceAny(dockOnly)) return false;
  if(callOverlayActive() || callBannerActive() || nyxShotThumbActive() || powerMenuOpen || diMenuOpen) return false;
  return true;
#else
  return false;
#endif
}

// Gambar SELURUH konten Home (scroll 0, tanpa culling) ke homeCache. false = gagal/PSRAM tipis -> jalur lama.
static bool homeCacheBuild(){
  homeLayoutEnsure(); homeRecomputeFilter();
  const int cols=homeCols(), cw=homeCardW(), ch=homeCardH(), gap=10, gridTop=homeGridTop();
  const int rows=(homeShowN+cols-1)/cols;
  int needH = gridTop + rows*(ch+gap) + 8;
  if(needH<homeDockY()) needH=homeDockY();
  if(homeCache.getBuffer() && (homeCacheW!=SCR_W || homeCacheH!=needH)) homeCacheFree();
  if(!homeCache.getBuffer()){
    const size_t bytes=(size_t)SCR_W*(size_t)needH*2;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 1500000UL) return false;   // sisakan utk Doom/NES/dll
    homeCache.setColorDepth(16); homeCache.setPsram(true);
    if(!homeCache.createSprite(SCR_W,needH)){ homeCacheFree(); return false; }
    homeCacheW=SCR_W; homeCacheH=needH;
  }
  LGFX_Sprite& s=homeCache;
  s.clearClipRect();
  s.fillSprite(T().bg);
  int iconR = min(cw,ch-16)/2 - 2; if(iconR>24) iconR=24; if(iconR<14) iconR=14;
  s.setFont(emjF(&lgfx::fonts::Font0));
  s.setTextWrap(false);
  s.setTextSize(1);
  uiText(s,14,STATUS_H+8,"Accretion",T().text,false,true);
  HomeRect rc=homeWClock();   if(rc.h>0) drawClockCard(s,rc.x,rc.y,rc.w,rc.h);
  HomeRect rb=homeWBatt();    if(rb.h>0) drawBattWidget(s,rb.x,rb.y,rb.w,rb.h);
  HomeRect rm=homeWMusic();   if(rm.h>0) drawMusicMini(s,rm.x,rm.y,rm.w,rm.h,true);
  HomeRect rw=homeWWeather(); if(rw.h>0) drawWeatherWidget(s,rw.x,rw.y,rw.w,rw.h);
  s.setTextSize(1);
  {
    char ttl[28];
    if(homeCat==0) strcpy(ttl,"Semua App"); else snprintf(ttl,sizeof(ttl),"%s (%d)",HOME_CAT_NAME[homeCat],homeShowN);
    uiText(s,14,gridTop-HOME_CAT_H-20,ttl,T().subtext,false,false);
    homeCatDraw(s,homeCatChipTop(),false,0,needH);
  }
  for(int k=0;k<homeShowN;k++){
    int i=homeShow[k];
    int col=k%cols, row=k/cols;
    int x=gap+col*(cw+gap), y=gridTop+row*(ch+gap);
    int cx=x+cw/2, cy=y+(ch-16)/2;
    drawAppIcon(s, apps[i].sym, cx, cy, iconR, apps[i].color);
    drawNotifBadge(s, cx, cy, iconR, appNotifCount[i], appBadgePop(i));
    s.setTextColor(T().text); s.setTextSize(1);
    homeDrawLabel(s, apps[i].name, x+cw/2, y+ch-14, cw+6);
  }
  if(homeShowN==0){
    const char* em="Tidak ada app di kategori ini";
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(SCR_W/2-s.textWidth(em)/2, gridTop+14); s.print(em);
  }
  s.setTextSize(1);
  return true;
}

// Salin potongan cache ke canvas (canvas sudah di-clip ke pita) + gambar bagian murah yg tetap dinamis.
static void homeCacheBlit(float sc){
  const int clipTop=homeGridClipTop(), contentBot=homeDockY()-4, contentH=contentBot-clipTop;
  const int scPx=homeScrollPx(sc);
  if(scPx<0 || scPx+contentBot>homeCacheH) canvas.fillRect(0,clipTop,SCR_W,contentH,T().bg);   // overscroll / ujung bawah
  homeCache.pushSprite(&canvas,0,-scPx);   // pushSprite menghormati clip canvas -> cuma pita yg tersalin
  int maxSc=homeMaxScroll();
  if(maxSc>0){   // indikator scroll (sama dgn drawHome)
    int railX=SCR_W-5, railTop=clipTop+6, railH=contentBot-2-railTop;
    if(railH>20){
      nyxAaRR(canvas, railX,railTop,3,railH,1,blend565(T().surface,0xFFFF,50));
      int thumbH=max(16,(int)(railH*railH/(float)(railH+maxSc)));
      float fr = sc/(float)maxSc; if(fr<0.0f) fr=0.0f; if(fr>1.0f) fr=1.0f;
      int thumbY=railTop+(int)((railH-thumbH)*fr);
      nyxAaRR(canvas, railX,thumbY,3,thumbH,1,T().accent);
    }
  }
  drawSearchBar(canvas, homeSearchBar, homeSearchRightX(), homeSearchTopY(), homeSearchInput, "Cari app...");
  drawToast(canvas);
}

void renderHomeBandFrame(){
  int bt=homeGridClipTop(), bh=(homeDockY()-4)-bt;
#if RP_HOME_CACHE
  if(homeCacheUsable()){   // HOME_CACHE_V1
    if(!homeCacheValid) homeCacheValid=homeCacheBuild();
    if(homeCacheValid){
      canvas.setClipRect(0,bt,SCR_W,bh);
      homeCacheBlit(homeScrollY);
      canvas.clearClipRect();
      display.setClipRect(0,bt,SCR_W,bh);
      canvas.pushSprite(0,0);
      display.clearClipRect();
      rcFrameMark();
      return;
    }
  }
#endif
''', "renderHomeBandFrame")

# ---- 3. invalidasi di frame penuh Home ----
rep("        renderCurrentFrame(); push();\n        homeBandReady = plainNow;",
    "        renderCurrentFrame(); push();\n        homeCacheValid=false;   // HOME_CACHE_V1: frame penuh = ada yg berubah -> cache dibangun ulang saat scroll berikutnya\n        homeBandReady = plainNow;",
    "invalidasi")

# ---- 4. bebaskan PSRAM saat keluar Home / terkunci ----
rep("homeAodActive || diMenuOpen) homeBandReady=false;\n",
    "homeAodActive || diMenuOpen) homeBandReady=false;\n  if(curScreen()!=SCR_HOME || locked){ if(homeCache.getBuffer()) homeCacheFree(); }   // HOME_CACHE_V1: PSRAM kembali utk app lain (Doom/NES)\n",
    "bebaskan")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: HOME_CACHE_V1 diterapkan")
