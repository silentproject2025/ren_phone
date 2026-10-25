import sys, os, re
for f in ('phone.ino','diffpush_renphone.ino'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo; salin diffpush_renphone.ino dulu)"%f); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'dpSmartPush' in s:
    print("Patch polish accretion-v2 sudah terpasang, dilewati"); sys.exit(0)
if 'ft6236Begin' not in s:
    print("ERROR: patch ILI9488+FT6236 (apply_ili9488_ft6236_patch.py) belum terpasang di branch ini."); sys.exit(1)

def rep(src, old, new, what, cnt=1):
    if src.count(old)!=cnt:
        print("ERROR: anchor '%s' ketemu %d kali (harus %d). Tidak ada yang diubah."%(what,src.count(old),cnt)); sys.exit(1)
    return src.replace(old,new)

# ---- deklarasi ----
s=rep(s,"#define STATUS_H 22   // tinggi status bar\n",
"#define STATUS_H 22   // tinggi status bar\nvoid dpSmartPush(); void dpInvalidate(); void dpSyncRows(int y,int h); void dpSyncAll(); // diffpush_renphone.ino\n","decl")

# ---- push() lewat mesin selisih ----
s=rep(s,"void push(){ unsigned long t0=micros(); canvas.pushSprite(0,0); nyxPushLastUs=micros()-t0; rcFrameMark(); }",
"void push(){ unsigned long t0=micros(); dpSmartPush(); nyxPushLastUs=micros()-t0; } // accretion-v2: kirim hanya kotak yg berubah (lihat diffpush_renphone.ino)","push")

# ---- mode pita Home: layar sudah diperbarui di baris-baris itu ----
s=rep(s,"  display.setClipRect(0,bt,SCR_W,bh);\n  canvas.pushSprite(0,0);\n  display.clearClipRect();\n  rcFrameMark();\n}",
"  display.setClipRect(0,bt,SCR_W,bh);\n  canvas.pushSprite(0,0);\n  display.clearClipRect();\n  dpSyncRows(bt,bh);\n  rcFrameMark();\n}","band")

# ---- invalidasi: navigasi, orientasi, layar modal yg menulis langsung ke display ----
s=rep(s,"void appOnEnter(Screen s){ int i=appIndexForScreen(s);","void appOnEnter(Screen s){ dpInvalidate(); int i=appIndexForScreen(s);","appOnEnter")
s=rep(s,"void appOnExit(Screen s){  int i=appIndexForScreen(s);","void appOnExit(Screen s){  dpInvalidate(); int i=appIndexForScreen(s);","appOnExit")
s=rep(s,"void applyOrientation(Orientation o, bool doSave) {\n","void applyOrientation(Orientation o, bool doSave) {\n  dpInvalidate();\n","orient")
for sig in ("void updDrawProgressFast(size_t written, size_t total){",
            "void updDrawProgress(const char* title, size_t written, size_t total){",
            "void updDrawBusy(const char* title){",
            "void updShowResult(bool success, const String& reason){",
            "void mjpegPlayFile(const String& filename){"):
    s=rep(s,sig+"\n",sig+"\n  dpInvalidate();\n",sig)

# ---- transisi navigasi: lapisan bawah DIAM, cuma lapisan atas yg bergerak (jauh lebih sedikit piksel) ----
a=s.find("void playNavTransition(NavAnim anim){")
b=s.find("  push(); // pastikan frame akhir persis sama dgn 'canvas'\n}\n",a)
if a<0 or b<0: print("ERROR: playNavTransition tidak ketemu"); sys.exit(1)
b+=len("  push(); // pastikan frame akhir persis sama dgn 'canvas'\n}\n")
newtr=r'''void playNavTransition(NavAnim anim){
  // accretion-v2: ILI9488 = 3 byte/piksel -> satu frame penuh ~92ms. Versi lama mengirim SELURUH layar tiap langkah
  // (lapisan bawah ikut geser/parallax). Sekarang lapisan bawah DIAM (sudah ada di layar), yg dikirim cuma
  // lapisan atas yg bergerak + sliver yg baru terbuka -> rata-rata separuh piksel, langkah animasi lebih banyak.
  push();                                              // pastikan layar == layar LAMA (murah kalau sudah sinkron)
  if(anim==NAV_ANIM_BACK) canvas.pushSprite(&transShot,0,0); // layar yg menutup = lapisan atas yg bergerak
  renderCurrentFrame();                                // frame BARU ke canvas

  const int span = (anim==NAV_ANIM_HOME) ? SCR_H : SCR_W;
  const unsigned long t0 = millis();
  int lastOff = 0;
  display.startWrite();
  for(;;){
    uiBoostKick();
    unsigned long el = millis()-t0;
    bool lastFrame = el>=NAV_TRANS_MS;
    float lin = lastFrame ? 1.0f : (float)el/(float)NAV_TRANS_MS;
    int offset = (int)(span*navEase(lin)+0.5f);
    if(offset>span) offset=span;
    if(offset!=lastOff){
      if(anim==NAV_ANIM_PUSH){            // layar baru masuk dari KANAN, menutupi layar lama yg diam
        navEdgeShadowV(SCR_W-offset);
        canvas.pushSprite(SCR_W-offset,0);
      } else if(anim==NAV_ANIM_BACK){     // layar yg ditutup geser ke KANAN, membuka layar tujuan (diam)
        int sx0 = lastOff>2 ? lastOff-2 : 0;
        int sw  = offset-sx0;
        if(sw>0){ display.setClipRect(sx0,0,sw,SCR_H); canvas.pushSprite(0,0); display.clearClipRect(); }
        navEdgeShadowV(offset);
        transShot.pushSprite(offset,0);
      } else {                            // NAV_ANIM_HOME: layar Home turun menutupi layar lama yg diam
        canvas.pushSprite(0,-SCR_H+offset);
      }
      lastOff = offset;
    }
    if(lastFrame) break;
    yield();
  }
  display.endWrite();
  dpSyncAll();   // layar sekarang == canvas penuh -> shadow ikut sinkron, TIDAK perlu kirim ulang
}
'''
s=s[:a]+newtr+s[b:]

# ---- layout: kepadatan mengikuti lebar layar (480x320 / 320x480), bukan angka 320x240 ----
s=rep(s,"int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 4 : 3; }",
"int homeCols(){ int c=(SCR_W-10)/68; return c<3?3:c; } // kepadatan tetap ~68px/kolom: 240px->3, 320px->4, 480px->6","homeCols")
s=rep(s,"r.w=homeIsLand()?188:SCR_W-20;","r.w=homeIsLand()?((SCR_W-28)*188/292):SCR_W-20;","clock w")
s=rep(s,"if(homeIsLand()){ r.x=206; r.y=STATUS_H+28; r.w=SCR_W-216; r.h=66; }",
"if(homeIsLand()){ r.x=10+homeWClock().w+8; r.y=STATUS_H+28; r.w=SCR_W-r.x-10; r.h=66; }","batt")
s=rep(s,"int ccCols(){ return currentOrient==ORIENT_LANDSCAPE ? 6 : 3; }",
"int ccCols(){ return currentOrient==ORIENT_LANDSCAPE ? 6 : (SCR_W>=300 ? 4 : 3); }","ccCols")
s=rep(s,"  int base = currentOrient==ORIENT_LANDSCAPE ? 36 : 44; // v108: tanpa label",
"  int base = currentOrient==ORIENT_LANDSCAPE ? (SCR_W>=440 ? 46 : 36) : (SCR_W>=300 ? 50 : 44); // v108: tanpa label","ccCircleD")
s=rep(s,"int ccCardH(){ return currentOrient==ORIENT_LANDSCAPE ? 54 : 50; }",
"int ccCardH(){ return currentOrient==ORIENT_LANDSCAPE ? (SCR_H>=300 ? 62 : 54) : (SCR_W>=300 ? 56 : 50); }","ccCardH")

# ---- clock SPI: 40 MHz default, 60 MHz "OC" ----
s=rep(s,"const int SPI_OC_OPTIONS[2] = {27,40};   // ILI9488: 27 aman, 40 cepat (60+ biasanya korup)",
"const int SPI_OC_OPTIONS[2] = {40,60};   // ILI9488: 40 = aman (default), 60 = overclock (kalau layar noise/korup, balik ke 40)","spi")
s=rep(s,"#define DISPLAY_SPI_FREQ_EXPERIMENTAL 40000000","#define DISPLAY_SPI_FREQ_EXPERIMENTAL 40000000","spi freq")
open('phone.ino','w',encoding='utf-8').write(s)

# ---- workflow ----
wf='.github/workflows/build-firmware.yml'
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    a="          cp phone.ino build_sketch/\n"
    if 'diffpush_renphone.ino' not in w:
        if w.count(a)==1: open(wf,'w',encoding='utf-8').write(w.replace(a,a+"          cp diffpush_renphone.ino build_sketch/\n"))
        else: print("PERINGATAN: tambahkan 'cp diffpush_renphone.ino build_sketch/' ke workflow sendiri")
print("OK: polish accretion-v2 terpasang (layout kepadatan + push selisih frame + transisi ringan).")
