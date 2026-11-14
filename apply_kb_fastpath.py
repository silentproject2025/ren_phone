#!/usr/bin/env python3
# KB_FASTPATH_V1 -- sentuh & keyboard terasa seperti HP beneran.
#
# 1) Task poll sentuh (core 0): FT6236 dibaca tiap ~3 ms walau loop() sedang
#    menggambar/mengirim frame. Sebelumnya sentuh hanya dibaca 1x per putaran
#    loop(), jadi ketukan yg selesai selama render hilang.
# 2) Antrean ketukan (8 slot, jari 1 & 2): tiap jari-turun dicatat. Saat
#    keyboard tampil, semua ketukan di area tuts diputar ulang berurutan ke
#    handler app yg SAMA (apps[idx].touch(...,isNew=true)) -> tidak ada
#    ketikan yg hilang, 2 jempol bisa menumpuk, perilaku app (mis. chatNoteTyping)
#    tetap jalan.
# 3) Efek tekan tuts tidak lagi memicu gambar-ulang penuh terus-menerus selama
#    130 ms. Sekarang: 1 frame saat ditekan + 1 gambar-ulang HANYA area
#    keyboard (clip) saat dilepas. KB_PRESS_MS 130 -> 70.
# 4) push-diff: kalau yg berubah = kolom teks (atas) + tuts (bawah), kirim
#    2 pita terpisah, bukan satu pita besar dari atas sampai bawah.
#
# Idempoten: aman dijalankan ulang.
import re, sys

P = "phone.ino"
s = open(P, encoding="utf-8").read()
if "KB_FASTPATH_V1" in s:
    print("sudah terpasang"); sys.exit(0)

def rep(old, new, count=1):
    global s
    n = s.count(old)
    assert n == count, "pola tidak ditemukan/ambigu (%d x): %r" % (n, old[:80])
    s = s.replace(old, new)

# --- 0) mutex status sentuh (sebelum class LGFX) -------------------------
rep("class LGFX : public lgfx::LGFX_Device {\n",
"""#if RP_TOUCH_CAPACITIVE
// KB_FASTPATH_V1: status sentuh dibaca/ditulis dari 2 core (task poll core 0, loop() core 1)
static portMUX_TYPE rpTouchMux = portMUX_INITIALIZER_UNLOCKED;
#endif

class LGFX : public lgfx::LGFX_Device {
""")

# --- 1) touchSlot: baca atomik ------------------------------------------
rep("""    if(s<0||s>1) return false;
    if(_sDown[s]){ *tp=_sPt[s]; _sNew[s]=false; return true; }
    if(_sHold[s]){ *tp=_sPt[s]; _sHold[s]=false; _sNew[s]=false; return true; }   // ketukan kilat: tampil 1 pembacaan
    return false;
  }""",
"""    if(s<0||s>1) return false;
    bool r=false;
    portENTER_CRITICAL(&rpTouchMux);
    if(_sDown[s]){ *tp=_sPt[s]; _sNew[s]=false; r=true; }
    else if(_sHold[s]){ *tp=_sPt[s]; _sHold[s]=false; _sNew[s]=false; r=true; }   // ketukan kilat: tampil 1 pembacaan
    portEXIT_CRITICAL(&rpTouchMux);
    return r;
  }
  // KB_FASTPATH_V1: antrean jari-turun (diisi touchPollInner, dikuras loop())
  bool touchQPop(int* x,int* y,uint32_t* ms){
    bool ok=false;
    portENTER_CRITICAL(&rpTouchMux);
    if(_qH!=_qT){ *x=_q[_qH].x; *y=_q[_qH].y; *ms=_q[_qH].ms; _qH=(uint8_t)((_qH+1)&7); ok=true; }
    portEXIT_CRITICAL(&rpTouchMux);
    return ok;
  }
  void touchQClear(){
    portENTER_CRITICAL(&rpTouchMux); _qH=_qT=0; portEXIT_CRITICAL(&rpTouchMux);
  }""")

# --- 2) interval poll minimum 4 -> 3 ms ---------------------------------
rep("if(now-_tPollMs < 4) return;", "if(now-_tPollMs < 3) return;")

# --- 3) touchPollInner: pembaruan status di dalam critical section + antrean
rep("    // 1) slot yg sudah ada: cari titik dgn id yg sama\n",
    "    portENTER_CRITICAL(&rpTouchMux);   // KB_FASTPATH_V1\n    // 1) slot yg sudah ada: cari titik dgn id yg sama\n")
rep("""      _sDown[s]=true; _sNew[s]=true; _sHw[s]=rid[i];
      int x,y; touchMap(rx[i],ry[i],x,y);
      _sPt[s].x=x; _sPt[s].y=y; _sPt[s].size=64; _sPt[s].id=s;
    }
  }
private:""",
"""      _sDown[s]=true; _sNew[s]=true; _sHw[s]=rid[i];
      int x,y; touchMap(rx[i],ry[i],x,y);
      _sPt[s].x=x; _sPt[s].y=y; _sPt[s].size=64; _sPt[s].id=s;
      { uint8_t nt=(uint8_t)((_qT+1)&7);                 // KB_FASTPATH_V1: catat jari-turun
        if(nt!=_qH){ _q[_qT].x=(int16_t)x; _q[_qT].y=(int16_t)y; _q[_qT].ms=millis(); _qT=nt; } }
    }
    portEXIT_CRITICAL(&rpTouchMux);
  }
private:
  struct TapEv { int16_t x,y; uint32_t ms; };           // KB_FASTPATH_V1
  TapEv   _q[8];
  uint8_t _qH=0, _qT=0;""")

# --- 4) KB_PRESS_MS + tanpa gambar-ulang terus-menerus ------------------
rep("#define KB_PRESS_MS 130", "#define KB_PRESS_MS 70   // KB_FASTPATH_V1 (dulu 130)")
rep("""void drawKb(LGFX_Sprite& s){""",
"""uint32_t kbRelDue=0;   // KB_FASTPATH_V1: kapan efek tekan harus dilepas (dicek di loop())
void drawKb(LGFX_Sprite& s){""")
rep("  if(pressing) needRedraw=true; else kbPressKey=-1;   // frame berikutnya menggambar ulang tuts normal",
    "  if(pressing) kbRelDue=(uint32_t)kbPressUntil; else kbPressKey=-1;   // KB_FASTPATH_V1: 1x gambar-ulang saat lepas (loop -> kbFastTick), bukan tiap frame")

# --- 5) push-diff: 2 pita ----------------------------------------------
rep("""  const int h=y1-y0+1;
  rpStBand++; rpStRows+=(uint32_t)h;
  if(h*100 >= H*75){ if(rpBusy<255) rpBusy++; } else rpBusy=0;
  rpOwnPush=true;
  if(h>=H){ canvas.pushSprite(0,0); }
  else { display.setClipRect(0,y0,W,h); canvas.pushSprite(0,0); display.clearClipRect(); }
  rpOwnPush=false;
  memcpy(rpSh+(size_t)y0*rowB, src+(size_t)y0*rowB, rowB*(size_t)h);
""",
"""  // KB_FASTPATH_V1: celah bersih terpanjang di tengah >= 24 baris -> kirim 2 pita (teks atas + tuts bawah)
  int g0=-1, g1=-1;
  if(y1-y0 >= 40){
    int bestLen=0, runS=-1;
    for(int r=y0+1; r<y1; r++){
      if(memcmp(src+(size_t)r*rowB, rpSh+(size_t)r*rowB, rowB)==0){ if(runS<0) runS=r; }
      else if(runS>=0){ int len=r-runS; if(len>bestLen){ bestLen=len; g0=runS; g1=r-1; } runS=-1; }
    }
    if(bestLen<24) g0=-1;
  }
  const int ha=(g0>=0)?(g0-y0):0, hb=(g0>=0)?(y1-g1):0;
  const int h=(g0>=0)?(ha+hb):(y1-y0+1);
  rpStBand++; rpStRows+=(uint32_t)h;
  if(h*100 >= H*75){ if(rpBusy<255) rpBusy++; } else rpBusy=0;
  rpOwnPush=true;
  if(g0>=0){
    display.setClipRect(0,y0,W,ha);   canvas.pushSprite(0,0);
    display.setClipRect(0,g1+1,W,hb); canvas.pushSprite(0,0);
    display.clearClipRect();
  } else if(h>=H){ canvas.pushSprite(0,0); }
  else { display.setClipRect(0,y0,W,h); canvas.pushSprite(0,0); display.clearClipRect(); }
  rpOwnPush=false;
  if(g0>=0){
    memcpy(rpSh+(size_t)y0*rowB,     src+(size_t)y0*rowB,     rowB*(size_t)ha);
    memcpy(rpSh+(size_t)(g1+1)*rowB, src+(size_t)(g1+1)*rowB, rowB*(size_t)hb);
  } else memcpy(rpSh+(size_t)y0*rowB, src+(size_t)y0*rowB, rowB*(size_t)h);
""")

# --- 6) task poll + helper keyboard (sebelum setup() dan loop()) --------
rep("void setup(){\n  Serial.begin(115200);",
"""// ===== KB_FASTPATH_V1: task poll sentuh (core 0) =====
#if RP_TOUCH_CAPACITIVE
static void rpTouchTask(void*){
  for(;;){ display.touchPoll(); vTaskDelay(pdMS_TO_TICKS(2)); }
}
static void rpTouchTaskStart(){
  static bool started=false;
  if(started) return;
  started=true;
  xTaskCreatePinnedToCore(rpTouchTask,"touchpoll",3072,nullptr,5,nullptr,0);
  Serial.println("[touch] task poll sentuh aktif (core 0, ~3 ms) + antrean ketukan");
}
#endif

void setup(){
  Serial.begin(115200);""")

rep("""  loadOrRunCalibration();
#else
  if(display.touch())loadOrRunCalibration();""",
"""  loadOrRunCalibration();
  rpTouchTaskStart();   // KB_FASTPATH_V1
#else
  if(display.touch())loadOrRunCalibration();""")

rep("""unsigned long lastLoopMs=0; // v14: dipakai normalisasi decay momentum scroll thdp waktu nyata (dt)
void loop(){""",
"""unsigned long lastLoopMs=0; // v14: dipakai normalisasi decay momentum scroll thdp waktu nyata (dt)

// ===== KB_FASTPATH_V1: keyboard cepat =====
static uint32_t s_kbDropBefore=0;
static bool     s_kbPrev=false;
static bool kbFastOverlaysIdle(){
  return !locked && !controlCenterOpen && !notifShadeOpen && !diMenuOpen && !appSwitcherOpen && !powerMenuOpen
      && !callOverlayActive() && !panelPullActive && !dragCloseActive && !ccAnimating && !homeAodActive;
}
// Putar ulang semua ketukan di area tuts (urut waktu) ke handler app yg sama dgn jalur newT.
// Return true kalau ketukan newT sekarang sudah termasuk yg diputar ulang (newT harus dibuang).
static bool kbQueueDrain(bool newT,int tx,int ty){
#if RP_TOUCH_CAPACITIVE
  bool kbNow=(kbVisible && kbTarget);
  if(kbNow && !s_kbPrev) s_kbDropBefore=millis();        // ketukan pembuka keyboard jangan ikut diketik
  s_kbPrev=kbNow;
  if(!kbNow || !kbFastOverlaysIdle()){ display.touchQClear(); return false; }
  const int yMin=kbY()-2;
  int qx,qy; uint32_t qms; bool matched=false;
  const uint32_t now=millis();
  while(display.touchQPop(&qx,&qy,&qms)){
    if((int32_t)(qms-s_kbDropBefore)<=0) continue;       // sebelum keyboard muncul
    if((uint32_t)(now-qms)>600) continue;                // basi (loop sempat macet lama)
    if(qy<yMin) continue;                                // di luar tuts: biar jalur biasa yg urus
    if(!(kbVisible && kbTarget)) break;                  // keyboard ditutup tuts sebelumnya (mis. Enter)
    if(curScreen()==SCR_HOME){
      if(kbTarget==&homeSearchInput) kbTouch(qx,qy); else continue;
    } else {
      int idx=appIndexForScreen(curScreen());
      if(idx<0) continue;
      apps[idx].touch(qx,qy,false,true);
    }
    needRedraw=true;
    if(newT && abs(qx-tx)<=24 && abs(qy-ty)<=24) matched=true;
  }
  return matched && ty>=yMin;
#else
  (void)newT; (void)tx; (void)ty;
  return false;
#endif
}
static bool kbFastRelOk(){
  return kbFastOverlaysIdle() && curScreen()!=SCR_HOME && swLastFrameApp && !wasTouched
      && !toastVisibleNow() && !nyxShotThumbActive() && !callBannerActive() && !nyxThFreezeFrame();
}
// Efek tekan dilepas: gambar ulang HANYA area keyboard di canvas (clip) lalu push-diff (pita kecil).
static void kbFastRelease(){
  int yt=kbY()-3; if(yt<0) yt=0;
  bool custom=(curScreen()==SCR_NOTEPAD || curScreen()==SCR_AICHAT);
  canvas.setFont(emjF(custom ? uiFontList[uiFontIdx] : &lgfx::fonts::Font0));
  canvas.setTextWrap(false);
  canvas.setClipRect(0,yt,SCR_W,SCR_H-yt);
  drawKb(canvas);
  canvas.clearClipRect();
  push();
}
static void kbFastTick(){
  if(!kbRelDue) return;
  if((int32_t)(millis()-kbRelDue)<0) return;
  kbRelDue=0;
  if(!(kbVisible && kbTarget)) return;
  if(needRedraw) return;                                 // frame penuh sudah dijadwalkan: tuts ikut normal
  if(kbFastRelOk()) kbFastRelease(); else needRedraw=true;
}

void loop(){""")

rep("""  bool newT=touched&&!wasTouched;

  if(ccAnimating){""",
"""  bool newT=touched&&!wasTouched;
  if(kbQueueDrain(newT,tx,ty)) newT=false;   // KB_FASTPATH_V1: ketukan tuts sudah diputar dari antrean
  kbFastTick();                              // KB_FASTPATH_V1: lepas efek tekan (area keyboard saja)

  if(ccAnimating){""")

open(P, "w", encoding="utf-8").write(s)
print("OK: KB_FASTPATH_V1 terpasang")
