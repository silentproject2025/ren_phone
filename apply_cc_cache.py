#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_cc_cache.py (CC_CACHE_V1) -- swipe turun Control Center jadi smooth.

Masalah: tiap frame tarikan / animasi buka-tutup, drawControlCenter() menghitung ulang SELURUH
kaca (48 sampel + ribuan fillRect bilinear + shadow) + 11 tile AA + ikon, dan Home di bawahnya
juga digambar ulang. Frame jadi berat (animasi 200 ms cuma kebagian 2-4 frame) dan warna kaca
di-resample dari titik yg bergeser tiap frame (berkedip).

Perbaikan:
 1. ccCache: panel digambar SEKALI pada tinggi penuh ke sprite PSRAM. Selama tarik/animasi panel
    cuma disalin (menggeser turun, bagian bawahnya mengikuti jari). Warna kaca stabil.
    Sudut membulat dikunci dgn warna transparan -> tdk ada sisa latar di sudut.
 2. ccBg: snapshot latar (Home/app) diambil sekali di awal sesi; frame berikutnya latar disalin
    (memcpy) -- tidak menggambar ulang Home/wallpaper. Latar diam selama panel bergerak.
 3. Begitu panel diam (penuh), kembali ke jalur gambar live (tile, tekan, slider, tema berubah).
 Konsekuensi: pop-in tile bergantian & bounce overshoot tdk terlihat saat bergerak (panel menggeser).
 Butuh PSRAM bebas >= ukuran sprite + 1,5 MB, kalau tidak -> jalur lama. -DCC_CACHE_V1=0 mematikan.

Jalankan dari root repo (folder yg ada phone.ino). Idempotent.
"""
import sys, os
P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "CC_CACHE_V1" in s:
    print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: blok '%s' ketemu %d kali (harus 1). Dibatalkan, phone.ino tidak diubah." % (label, n))
    s = s.replace(old, new)

# ---- 1. global sprite cache panel ----
rep("bool  ccAnimating=false;\n",
r'''bool  ccAnimating=false;
// CC_CACHE_V1 (apply_cc_cache.py): panel CC digambar sekali ke ccCache, tarik/animasi = salin.
#ifndef CC_CACHE_V1
#define CC_CACHE_V1 1
#endif
LGFX_Sprite ccCache(&display);
bool ccCacheValid=false;
int  ccCacheW=0, ccCacheH=0;
void ccCacheFree(){ if(ccCache.getBuffer()) ccCache.deleteSprite(); ccCacheValid=false; ccCacheW=ccCacheH=0; }
''', "global")

# ---- 2. drawControlCenter -> ccDrawLiveH + cache + wrapper ----
rep("void drawControlCenter(LGFX_Sprite& s){\n  int ph = (int)ccOffset;\n  if(ph<=0) return;\n",
    "static void ccDrawLiveH(LGFX_Sprite& s, int ph, bool withToast){\n  if(ph<=0) return;\n", "head")

rep("  drawToast(s);\n}\n\n// CC-LONGPRESS ---",
r'''  if(withToast) drawToast(s);
}

// CC_CACHE_V1: warna kunci utk piksel di luar bentuk panel (sudut/margin) saat blit transparan.
#define CC_CACHE_KEY 0xF81F
static bool ccCacheEnsure(LGFX_Sprite& s){
#if CC_CACHE_V1
  if(ccCacheValid) return true;
  if(s.getColorDepth()!=16 || !s.getBuffer()) return false;
  const int W=s.width(), PH=ccPanelH(), CH=PH+8;   // +8 baris: sampel kaca di tepi bawah sama persis dgn jalur live
  if(CH>s.height()) return false;
  if(ccCache.getBuffer() && (ccCacheW!=W || ccCacheH!=CH)) ccCacheFree();
  if(!ccCache.getBuffer()){
    const size_t bytes=(size_t)W*(size_t)CH*2;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 1500000UL) return false;   // sisakan utk Doom/NES/dll
    ccCache.setColorDepth(16); ccCache.setPsram(true);
    if(!ccCache.createSprite(W,CH)){ ccCacheFree(); return false; }
    ccCacheW=W; ccCacheH=CH;
  }
  // latar (Home/app tanpa panel) disalin dulu -> kaca mengambil sampel dari latar yg sama spt jalur live
  memcpy(ccCache.getBuffer(), s.getBuffer(), (size_t)W*(size_t)CH*2);
  ccCache.clearClipRect(); ccCache.setTextWrap(false); ccCache.setTextSize(1);
  int sp=ccPressIdx, sh=ccHoldIdx; ccPressIdx=-1; ccHoldIdx=-1;   // tanpa efek tekan di dalam cache
  ccDrawLiveH(ccCache, PH, false);                                 // tinggi penuh -> semua tile/slider tampil
  ccPressIdx=sp; ccHoldIdx=sh;
  // kunci: margin kiri/kanan, 8 baris bawah (di luar panel), dan 4 sudut membulat
  const int mx=ccMarginX(), pw=ccPanelW(), R=min(22, PH/2);
  ccCache.fillRect(0,0,mx,CH,(uint16_t)CC_CACHE_KEY);
  ccCache.fillRect(mx+pw,0,W-mx-pw,CH,(uint16_t)CC_CACHE_KEY);
  ccCache.fillRect(0,PH,W,CH-PH,(uint16_t)CC_CACHE_KEY);
  for(int dy=0; dy<R; dy++) for(int dx=0; dx<R; dx++){
    int ex=2*(R-dx)-1, ey=2*(R-dy)-1;           // 2x jarak pusat piksel ke pusat sudut (integer)
    if(ex*ex+ey*ey > (2*R+1)*(2*R+1)){          // lebih dari R+0,5 px dari pusat -> di luar bentuk (tepi AA dibiarkan)
      ccCache.drawPixel(mx+dx,       dy,        (uint16_t)CC_CACHE_KEY);
      ccCache.drawPixel(mx+pw-1-dx,  dy,        (uint16_t)CC_CACHE_KEY);
      ccCache.drawPixel(mx+dx,       PH-1-dy,   (uint16_t)CC_CACHE_KEY);
      ccCache.drawPixel(mx+pw-1-dx,  PH-1-dy,   (uint16_t)CC_CACHE_KEY);
    }
  }
  ccCacheValid=true;
  return true;
#else
  (void)s; return false;
#endif
}

// Panel digeser turun: dasar panel = ph (mengikuti jari). Bagian tengah disalin lurus (cepat),
// pita sudut atas/bawah disalin transparan supaya sudut membulat rapi di atas latar.
static void ccCacheBlit(LGFX_Sprite& s, int ph){
  const int mx=ccMarginX(), pw=ccPanelW(), PH=ccPanelH(), R=min(22, PH/2);
  int y=ph-PH; if(y>0) y=0;                      // overshoot karet: panel tetap menempel di atas
  const int H=s.height();
  auto clipRows=[&](int y0,int y1)->bool{ if(y0<0) y0=0; if(y1>H) y1=H; if(y1<=y0) return false; s.setClipRect(mx,y0,pw,y1-y0); return true; };
  if(clipRows(y+R, y+PH-R)) ccCache.pushSprite(&s, 0, y);
  if(clipRows(y,   y+R))    ccCache.pushSprite(&s, 0, y, (uint16_t)CC_CACHE_KEY);
  if(clipRows(y+PH-R, y+PH)) ccCache.pushSprite(&s, 0, y, (uint16_t)CC_CACHE_KEY);
  s.clearClipRect();
}

void drawControlCenter(LGFX_Sprite& s){
  int ph = (int)ccOffset;
  const bool motion = controlCenterOpen && (panelPullActive || ccAnimating);
  if(!motion) ccCacheValid=false;                // panel diam: isi bisa berubah (tema, toggle) -> cache basi
  else if(ccCacheEnsure(s)){                     // dibangun di frame pertama sesi (ph masih 0) -> tdk ada patah di tengah gerakan
    if(ph>0){ ccCacheBlit(s,ph); drawToast(s); }
    return;
  }
  if(ph<=0) return;
  ccDrawLiveH(s, ph, true);                      // jalur live (diam / PSRAM tipis)
}

// CC-LONGPRESS ---''', "tail")

# ---- 3. snapshot latar + hook di renderCurrentFrame ----
rep("void renderCurrentFrame(){\n",
r'''// CC_CACHE_V1: snapshot latar. Selama CC bergerak latar (Home/app) diam -> disalin, bukan digambar ulang.
static LGFX_Sprite ccBg(&display);
static bool ccBgValid=false;
static void ccBgFree(){ if(ccBg.getBuffer()) ccBg.deleteSprite(); ccBgValid=false; }
static bool ccMotionNow(){
  return controlCenterOpen && (panelPullActive || ccAnimating) && !locked && !appSwitcherOpen
      && !homeAodActive && !powerMenuOpen && !callOverlayActive();
}
static bool ccBgRestore(){
#if CC_CACHE_V1
  if(!ccBgValid) return false;
  if(!ccMotionNow() || canvas.getColorDepth()!=16 || !canvas.getBuffer()
     || ccBg.width()!=canvas.width() || ccBg.height()!=canvas.height()){ ccBgValid=false; return false; }
  memcpy(canvas.getBuffer(), ccBg.getBuffer(), (size_t)canvas.width()*(size_t)canvas.height()*2);
  return true;
#else
  return false;
#endif
}
static void ccBgCapture(){
#if CC_CACHE_V1
  if(!ccMotionNow()){ ccBgValid=false; return; }
  if(ccBgValid) return;
  if(canvas.getColorDepth()!=16 || !canvas.getBuffer()) return;
  const int W=canvas.width(), H=canvas.height();
  if(ccBg.getBuffer() && (ccBg.width()!=W || ccBg.height()!=H)) ccBgFree();
  if(!ccBg.getBuffer()){
    const size_t bytes=(size_t)W*(size_t)H*2;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + 1500000UL) return;
    ccBg.setColorDepth(16); ccBg.setPsram(true);
    if(!ccBg.createSprite(W,H)){ ccBgFree(); return; }
  }
  memcpy(ccBg.getBuffer(), canvas.getBuffer(), (size_t)W*(size_t)H*2);
  ccBgValid=true;
#endif
}
void renderCurrentFrame(){
''', "renderCurrentFrame global")

rep("  if(callOverlayActive()){\n    drawCallOverlay(canvas);",
    "  if(ccBgRestore()){\n    // CC_CACHE_V1: latar dari snapshot\n  } else if(callOverlayActive()){\n    drawCallOverlay(canvas);", "restore")

rep("  // v90: 2 overlay baru (Notification Shade + context menu DI) disisipkan\n",
    "  ccBgCapture();   // CC_CACHE_V1: latar bersih (tanpa panel) diambil di frame pertama sesi\n  // v90: 2 overlay baru (Notification Shade + context menu DI) disisipkan\n", "capture")

# ---- 4. loop(): invalidasi saat CC tertutup, bebaskan PSRAM kalau bukan di Home ----
rep("  if(curScreen()!=SCR_HOME || locked){ if(homeCache.getBuffer()) homeCacheFree(); }   // HOME_CACHE_V1: PSRAM kembali utk app lain (Doom/NES)\n",
    "  if(curScreen()!=SCR_HOME || locked){ if(homeCache.getBuffer()) homeCacheFree(); }   // HOME_CACHE_V1: PSRAM kembali utk app lain (Doom/NES)\n"
    "  if(!controlCenterOpen && !panelPullActive){   // CC_CACHE_V1: sesi selesai -> cache/snapshot basi; PSRAM dibebaskan kalau bukan di Home\n"
    "    ccCacheValid=false; ccBgValid=false;\n"
    "    if(curScreen()!=SCR_HOME || locked){ if(ccCache.getBuffer()) ccCacheFree(); if(ccBg.getBuffer()) ccBgFree(); }\n"
    "  }\n", "loop hook")

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("OK: CC_CACHE_V1 terpasang di phone.ino")
