# DOOM_LAYOUT_V2: pad sentuh DOOM baru untuk panel lebar (ST7796 480x320, margin hitam kiri/kanan 80 px).
#  - Joystick kiri TETAP (kelihatan): maju/mundur + geser (strafe kiri/kanan).
#  - Tombol baru: Senjata < > (ganti senjata), Run (ditahan), Map.
#  - Tombol lama TIDAK pindah: F (Fire), U (Use), OK, Pause, Kembali.
#  - Geser kanan = noleh, dan geser kiri di luar cincin = maju/mundur (perilaku lama tetap jalan).
#  - Semua tombol baru digambar di margin (tidak menutupi layar game). Panel 320x240 (ILI9341) TIDAK berubah.
# Jalankan dari root repo:  python apply_doom_layout_v2.py
import sys, os
f = 'doom_renphone.ino'
if not os.path.exists(f):
    print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)
s = open(f, encoding='utf-8').read()
if 'DOOM_LAYOUT_V2' in s:
    print("Sudah terpasang, dilewati"); sys.exit(0)

def rep(s, old, new, name):
    if s.count(old) != 1:
        print("ERROR [%s]: anchor ketemu %d kali: %r" % (name, s.count(old), old[:70])); sys.exit(1)
    return s.replace(old, new)

# 1) forward declaration + hook ekstra (gabungan gamepad web + sentuhan)
s = rep(s, "void doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();",
           "void doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();\nuint32_t doomExtKeys();   // DOOM_LAYOUT_V2", 'decl')
s = rep(s, "  DoomGlue_SetHooks(padExtKeys, padTakeMouseDX, padTakeTyped);",
           "  DoomGlue_SetHooks(doomExtKeys, padTakeMouseDX, padTakeTyped);   // DOOM_LAYOUT_V2: padExtKeys (web) + tombol sentuh", 'hooks')

# 2) geometri tombol baru
s = rep(s, "static int doomPauseY(){ return 24; }\n", """static int doomPauseY(){ return 24; }

// DOOM_LAYOUT_V2: pad lebar. Hanya aktif kalau panel >= 440x300 (ST7796 480x320: margin kiri/kanan 80 px hitam).
static inline bool doomWide(){ return SCR_W >= 440 && SCR_H >= 300; }
#define DOOM_STICK_R 34
static int doomStickCx(){ return 40; }
static int doomStickCy(){ return 186; }
static int doomWpPrevCx(){ return SCR_W-58; }
static int doomWpNextCx(){ return SCR_W-22; }
static int doomWpCy(){ return 72; }
static int doomRunCx(){ return SCR_W-30; }
static int doomRunCy(){ return 128; }
static int doomMapCx(){ return 40; }
static int doomMapCy(){ return 92; }
// Bit tombol EXTRA: urutan HARUS sama dgn EXTMAP[] di esp_doom_core/DoomGlue.cpp
#define DEXT_STRAFE_L (1u<<0)
#define DEXT_STRAFE_R (1u<<1)
#define DEXT_RUN      (1u<<2)
#define DEXT_MAP      (1u<<11)
#define DEXT_WPN_PREV (1u<<29)
#define DEXT_WPN_NEXT (1u<<30)
""", 'geom')

# 3) status sentuh + hook ekstra (+ latch utk tombol sekali-tekan)
s = rep(s, "static uint8_t doomSlotMask[2]={0,0};\n", """static uint8_t doomSlotMask[2]={0,0};

// DOOM_LAYOUT_V2: tombol EXTRA dari sentuhan. Ganti senjata/Map = sekali-tekan -> di-latch supaya tidak
// hilang di antara dua tic DOOM (alasan sama dgn DOOM_FIRE_LATCH_V1).
static volatile uint32_t doomSlotExt[2] = {0,0};          // tombol EXTRA yg ditahan tiap jari
static bool     doomSlotStick[2] = {false,false};         // jari ini memegang joystick kiri
static bool     doomSlotBtn[2]   = {false,false};         // jari ini mulai di tombol baru (abaikan geseran)
static volatile uint32_t doomExtLatchMask = 0, doomExtLatchUntil = 0;
#define DOOM_EXT_LATCH_MS 120
static inline bool doomInCirc(int x,int y,int cx,int cy,int r){ int dx=x-cx, dy=y-cy; return dx*dx+dy*dy <= r*r; }
static void doomExtLatch(uint32_t bit){
  uint32_t now = millis();
  if((int32_t)(doomExtLatchUntil - now) <= 0) doomExtLatchMask = 0;   // latch lama sudah habis -> mulai bersih
  doomExtLatchMask = doomExtLatchMask | bit;
  uint32_t u = now + DOOM_EXT_LATCH_MS; if(u==0) u=1;
  doomExtLatchUntil = u;
}
// Dipanggil engine DOOM (core 1) lewat DoomGlue_SetHooks().
uint32_t doomExtKeys(){
  uint32_t e = padExtKeys() | doomSlotExt[0] | doomSlotExt[1];
  if((int32_t)(doomExtLatchUntil - millis()) > 0) e |= doomExtLatchMask;
  return e;
}
""", 'state')

# 4) lepas semua jari
s = rep(s, """static void doomReleaseAll(){
  doomSlotMask[0]=doomSlotMask[1]=0;
  doomApplyMasks();
}""", """static void doomReleaseAll(){
  doomSlotMask[0]=doomSlotMask[1]=0;
  doomSlotExt[0]=doomSlotExt[1]=0; doomSlotStick[0]=doomSlotStick[1]=false; doomSlotBtn[0]=doomSlotBtn[1]=false;   // DOOM_LAYOUT_V2
  doomApplyMasks();
}""", 'releaseall')

# 5) jari diangkat
s = rep(s, "  if(!held && !isNew){ doomSlotMask[s]=0; doomApplyMasks(); return; }",
           "  if(!held && !isNew){ doomSlotMask[s]=0; doomSlotExt[s]=0; doomSlotStick[s]=false; doomSlotBtn[s]=false; doomApplyMasks(); return; }   // DOOM_LAYOUT_V2", 'release')

# 6) blok sentuh pad lebar (sebelum tombol bulat lama)
s = rep(s, "  uint8_t &m = doomSlotMask[s];\n", """  uint8_t &m = doomSlotMask[s];

  // DOOM_LAYOUT_V2: joystick kiri tetap + tombol Senjata/Run/Map (hanya panel lebar)
  if(doomWide()){
    if(isNew){
      doomSlotStick[s]=false; doomSlotBtn[s]=false;
      if(doomInCirc(x,y,doomStickCx(),doomStickCy(),46))        doomSlotStick[s]=true;
      else if(doomInCirc(x,y,doomWpPrevCx(),doomWpCy(),17)){    doomExtLatch(DEXT_WPN_PREV); doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomWpNextCx(),doomWpCy(),17)){    doomExtLatch(DEXT_WPN_NEXT); doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomMapCx(),doomMapCy(),20)){      doomExtLatch(DEXT_MAP);      doomSlotBtn[s]=true; }
      else if(doomInCirc(x,y,doomRunCx(),doomRunCy(),22)){      doomSlotExt[s] = doomSlotExt[s] | DEXT_RUN; doomSlotBtn[s]=true; }
    }
    if(doomSlotStick[s]){
      // Joystick tetap: atas/bawah = maju/mundur, kiri/kanan = geser (strafe). Zona mati 12 px.
      int ox=x-doomStickCx(), oy=y-doomStickCy();
      const int SD=12;
      m &= ~DOOM_DIR_MASK;
      uint32_t e = 0;
      if(oy < -SD) m |= (1u<<DBTN_UP);
      if(oy >  SD) m |= (1u<<DBTN_DOWN);
      if(ox < -SD) e |= DEXT_STRAFE_L;
      if(ox >  SD) e |= DEXT_STRAFE_R;
      doomSlotExt[s] = (doomSlotExt[s] & ~(DEXT_STRAFE_L|DEXT_STRAFE_R)) | e;
      doomApplyMasks(); return;
    }
    if(doomSlotBtn[s]) return;   // jari mulai di tombol baru: abaikan geseran sampai diangkat (Run tetap ditahan)
  }
""", 'touch')

# 7) gambar pad lebar (sekali, di margin) -- dipanggil dari doomDrawOverlay()
s = rep(s, "void doomDrawOverlay(){\n  display.waitDMA();", """// DOOM_LAYOUT_V2: gambar joystick + tombol baru di margin hitam (digambar langsung ke display, 3 frame pertama).
static void doomDrawPadV2(){
  const uint16_t BG=0x2104, EDGE=0x7BEF, BTN=0x4208;
  auto lbl=[&](int cx,int cy,const char* t,uint16_t col){
    display.setTextSize(1); display.setTextColor(col);
    int tw = display.textWidth(t);
    display.setCursor(cx-tw/2, cy-3);
    display.print(t);
  };
  // joystick: cincin + 4 panah (atas/bawah = maju/mundur, kiri/kanan = geser)
  int cx=doomStickCx(), cy=doomStickCy();
  display.fillCircle(cx,cy,DOOM_STICK_R,BG);
  display.drawCircle(cx,cy,DOOM_STICK_R,EDGE);
  display.drawCircle(cx,cy,12,BTN);
  display.fillTriangle(cx,cy-29, cx-6,cy-19, cx+6,cy-19, TFT_WHITE);
  display.fillTriangle(cx,cy+29, cx-6,cy+19, cx+6,cy+19, TFT_WHITE);
  display.fillTriangle(cx-29,cy, cx-19,cy-6, cx-19,cy+6, TFT_WHITE);
  display.fillTriangle(cx+29,cy, cx+19,cy-6, cx+19,cy+6, TFT_WHITE);
  lbl(cx, cy+DOOM_STICK_R+9, "GERAK", EDGE);
  // senjata sebelumnya / berikutnya
  for(int i=0;i<2;i++){
    int bx = i ? doomWpNextCx() : doomWpPrevCx();
    display.fillCircle(bx,doomWpCy(),14,BTN);
    display.drawCircle(bx,doomWpCy(),14,TFT_BLACK);
    lbl(bx,doomWpCy(), i ? ">" : "<", TFT_WHITE);
  }
  lbl(SCR_W-40, doomWpCy()+24, "SENJATA", EDGE);
  // run (ditahan) dan map
  display.fillCircle(doomRunCx(),doomRunCy(),16,0xFFE0);
  display.drawCircle(doomRunCx(),doomRunCy(),16,TFT_BLACK);
  lbl(doomRunCx(),doomRunCy(),"RUN",TFT_BLACK);
  display.fillCircle(doomMapCx(),doomMapCy(),14,BTN);
  display.drawCircle(doomMapCx(),doomMapCy(),14,TFT_BLACK);
  lbl(doomMapCx(),doomMapCy(),"MAP",TFT_WHITE);
}

void doomDrawOverlay(){
  display.waitDMA();""", 'draw')

s = rep(s, """  display.fillRect(doomPauseX()+2, doomPauseY()-8, 4, 16, TFT_WHITE);
  display.endWrite();""", """  display.fillRect(doomPauseX()+2, doomPauseY()-8, 4, 16, TFT_WHITE);
  if(doomWide()) doomDrawPadV2();   // DOOM_LAYOUT_V2
  display.endWrite();""", 'draw2')

open(f, 'w', encoding='utf-8').write(s)
print("OK: layout pad DOOM v2 terpasang (joystick tetap, Senjata < >, Run, Map)")
