#!/usr/bin/env python3
# Pengaturan widget Home di layar "Atur Home" (disimpan permanen):
#   - Tampil/Sembunyi: kartu Jam, Musik, Baterai, Cuaca
#   - Susunan (potret): Standar (lama) / Ringkas (baterai+cuaca satu baris)
#   - Ukuran kartu Jam: Normal / Besar
# Mengubah phone.ino & homeedit_renphone.ino. Jalankan SETELAH fix_home_layout.py, dari folder repo:
#   python3 fix_home_widget_settings.py
import io, sys
def rd(p): return io.open(p,encoding="utf-8",errors="surrogateescape",newline="").read()
def wr(p,s): io.open(p,"w",encoding="utf-8",errors="surrogateescape",newline="").write(s)
P="phone.ino"; H="homeedit_renphone.ino"
p=rd(P); h=rd(H)
if "v132: home widget setting" in p: print("sudah diterapkan"); sys.exit(0)
if "v131: home layout" not in p: print("jalankan fix_home_layout.py dulu"); sys.exit(1)
def rep(s,old,new,name):
    if s.count(old)!=1: print("pola tidak unik/ketemu (%d): %s"%(s.count(old),name)); sys.exit(1)
    return s.replace(old,new)

# ---------- phone.ino : data + simpan/muat ----------
p=rep(p,"bool homeLayoutLoaded = false;\n",
"bool homeLayoutLoaded = false;\nuint8_t homeWMask = 0x0F;  // v132: home widget setting -- bit0 jam, bit1 musik, bit2 baterai, bit3 cuaca (1 = tampil)\nuint8_t homeWLayout = 1;   // 0 = Standar, 1 = Ringkas (baterai+cuaca satu baris, potret)\nuint8_t homeWBig = 1;      // 0 = jam Normal, 1 = jam Besar\n","globals")
p=rep(p,"void homeLayoutDefault(){\n",
"void homeLayoutDefault(){\n  homeWMask=0x0F; homeWLayout=1; homeWBig=1;   // v132: ikut kembali ke awal saat Reset\n","default")
p=rep(p,'  if(!pr.begin("homelay",true)) return;\n',
'  if(!pr.begin("homelay",true)) return;\n  homeWMask=pr.getUChar("wmask",homeWMask); homeWLayout=pr.getUChar("wlay",homeWLayout); homeWBig=pr.getUChar("wbig",homeWBig); // v132\n','load')
p=rep(p,'  pr.putULong64("hid",hid);\n  pr.end();\n}\nvoid homeLayoutEnsure()',
'  pr.putULong64("hid",hid);\n  pr.putUChar("wmask",homeWMask); pr.putUChar("wlay",homeWLayout); pr.putUChar("wbig",homeWBig); // v132\n  pr.end();\n}\nvoid homeLayoutEnsure()','save')

# ---------- phone.ino : posisi kartu dihitung dari pengaturan ----------
a=p.find("HomeRect homeWClock(){")
b=p.find("int homeGridTop(){")
if a<0 or b<a: print("blok homeWClock..homeGridTop tidak ketemu"); sys.exit(1)
NEW='''// v132: home widget setting -- posisi semua kartu dihitung SEKALI dari pengaturan (homeWMask/homeWLayout/homeWBig).
// Kartu yang disembunyikan = rect kosong (h=0, di luar layar) sehingga gambar & hit-test otomatis melewatinya.
HomeRect hwNone(){ HomeRect r; r.x=-1000; r.y=-1000; r.w=0; r.h=0; return r; }
void hwCompute(HomeRect& c, HomeRect& m, HomeRect& b, HomeRect& w, int& endY){
  homeLayoutEnsure();
  const bool sc=(homeWMask&1)!=0, sm=(homeWMask&2)!=0, sb=(homeWMask&4)!=0, sw=(homeWMask&8)!=0;
  const bool land=homeIsLand(), big=(homeWBig!=0);
  c=hwNone(); m=hwNone(); b=hwNone(); w=hwNone();
  int y=STATUS_H+28; bool any=false;
  if(land){
    int rh=big?76:66;
    if(sc||sb){
      if(sc&&sb){ c.x=10; c.y=y; c.w=188; c.h=rh;  b.x=206; b.y=y; b.w=SCR_W-216; b.h=rh; }
      else if(sc){ c.x=10; c.y=y; c.w=SCR_W-20; c.h=rh; }
      else { b.x=10; b.y=y; b.w=SCR_W-20; b.h=rh; }
      y+=rh+8; any=true;
    }
    if(sm){ m.x=10; m.y=y; m.w=SCR_W-20; m.h=48; y+=56; any=true; }
    if(sw){ w.x=10; w.y=y; w.w=SCR_W-20; w.h=40; y+=48; any=true; }
  } else {
    if(sc){ c.x=10; c.y=y; c.w=SCR_W-20; c.h=big?88:80; y+=c.h+8; any=true; }
    if(sm){ m.x=10; m.y=y; m.w=SCR_W-20; m.h=58; y+=66; any=true; }
    if(homeWLayout!=0){                                   // Ringkas
      if(sb&&sw){ b.x=10; b.y=y; b.w=84; b.h=76; w.x=b.x+b.w+8; w.y=y; w.w=SCR_W-10-w.x; w.h=76; y+=84; any=true; }
      else if(sb){ b.x=10; b.y=y; b.w=SCR_W-20; b.h=76; y+=84; any=true; }
      else if(sw){ w.x=10; w.y=y; w.w=SCR_W-20; w.h=48; y+=56; any=true; }
    } else {                                              // Standar (susunan lama)
      if(sb){ b.x=10; b.y=y; b.w=SCR_W-20; b.h=44; y+=52; any=true; }
      if(sw){ w.x=10; w.y=y; w.w=SCR_W-20; w.h=48; y+=56; any=true; }
    }
  }
  endY = any ? (y-8) : (STATUS_H+22);
}
HomeRect homeWClock(){ HomeRect c,m,b,w; int e; hwCompute(c,m,b,w,e); return c; }
HomeRect homeWMusic(){ HomeRect c,m,b,w; int e; hwCompute(c,m,b,w,e); return m; }
HomeRect homeWBatt(){ HomeRect c,m,b,w; int e; hwCompute(c,m,b,w,e); return b; }
HomeRect homeWWeather(){ HomeRect c,m,b,w; int e; hwCompute(c,m,b,w,e); return w; }
int homeWidgetsEnd(){ HomeRect c,m,b,w; int e; hwCompute(c,m,b,w,e); return e; }
'''
p=p[:a]+NEW+p[b:]

# gambar: lewati kartu yang disembunyikan
p=rep(p,"    HomeRect rc=homeWClock(); drawClockCard(s,rc.x,rc.y-scPx,rc.w,rc.h);\n    HomeRect rb=homeWBatt();  drawBattWidget(s,rb.x,rb.y-scPx,rb.w,rb.h);\n    HomeRect rm=homeWMusic(); drawMusicMini(s,rm.x,rm.y-scPx,rm.w,rm.h,true);\n    HomeRect rw=homeWWeather(); drawWeatherWidget(s,rw.x,rw.y-scPx,rw.w,rw.h);\n",
"    HomeRect rc=homeWClock(); if(rc.h>0) drawClockCard(s,rc.x,rc.y-scPx,rc.w,rc.h);\n    HomeRect rb=homeWBatt();  if(rb.h>0) drawBattWidget(s,rb.x,rb.y-scPx,rb.w,rb.h);\n    HomeRect rm=homeWMusic(); if(rm.h>0) drawMusicMini(s,rm.x,rm.y-scPx,rm.w,rm.h,true);\n    HomeRect rw=homeWWeather(); if(rw.h>0) drawWeatherWidget(s,rw.x,rw.y-scPx,rw.w,rw.h);\n","draw widgets")

# ---------- homeedit_renphone.ino : baris pengaturan di atas daftar app ----------
h=rep(h,"#define HE_RH 30\n","#define HE_RH 30\n#define HE_WROWS 6   // v132: home widget setting -- 6 baris pengaturan widget di atas daftar app\n","HE_RH")
h=rep(h,"int m = APP_COUNT * HE_RH - (heBot() - heTop());","int m = (APP_COUNT + HE_WROWS) * HE_RH - (heBot() - heTop());","maxscroll")
h=rep(h,"  int nameX = 46, nameMax = heUpX() - 6 - nameX;\n",
'''  {                                                    // v132: home widget setting -- baris pengaturan widget
    static const char* const wl[HE_WROWS] = {"Kartu Jam", "Kartu Musik", "Kartu Baterai", "Kartu Cuaca", "Susunan (potret)", "Ukuran kartu Jam"};
    for (int r = 0; r < HE_WROWS; r++) {
      int y = top + r * HE_RH - heScroll;
      if (y + HE_RH < top || y > bot) continue;
      bool isTog = (r < 4);
      bool on = isTog ? (((homeWMask >> r) & 1) != 0) : (r == 4 ? (homeWLayout != 0) : (homeWBig != 0));
      const char* bl = isTog ? (on ? "Tampil" : "Sembunyi") : (r == 4 ? (on ? "Ringkas" : "Standar") : (on ? "Besar" : "Normal"));
      bool dim = isTog && !on;
      s.setTextSize(1); s.setTextColor(dim ? T().subtext : T().text);
      s.setCursor(18, y + HE_RH / 2 - 4); s.print(wl[r]);
      int by = y + (HE_RH - 22) / 2;
      drawGlassPanel(s, heVisX(), by, heVisW(), 22, 8, dim ? T().surface2 : T().accent, 190);
      s.setTextColor(dim ? T().subtext : T().text);
      s.setCursor(heVisX() + heVisW() / 2 - s.textWidth(bl) / 2, by + 7); s.print(bl);
      s.drawFastHLine(14, y + HE_RH - 1, SCR_W - 28, T().divider);
    }
  }
  int nameX = 46, nameMax = heUpX() - 6 - nameX;
''',"draw rows")
h=rep(h,"    int y = top + k * HE_RH - heScroll;\n","    int y = top + (HE_WROWS + k) * HE_RH - heScroll;\n","row y")
h=rep(h,"  int k = (y - top + heScroll) / HE_RH;\n  if (k < 0 || k >= APP_COUNT) return;\n",
'''  int r0 = (y - top + heScroll) / HE_RH;
  if (r0 < 0) return;
  if (r0 < HE_WROWS) {                                  // v132: ketuk baris pengaturan widget
    if (x < heUpX() - 2) return;
    if (r0 < 4) homeWMask ^= (uint8_t)(1u << r0);
    else if (r0 == 4) homeWLayout = homeWLayout ? 0 : 1;
    else homeWBig = homeWBig ? 0 : 1;
    homeLayoutSave();
    needRedraw = true;
    return;
  }
  int k = r0 - HE_WROWS;
  if (k < 0 || k >= APP_COUNT) return;
''',"tap")
wr(P,p); wr(H,h)
print("OK: pengaturan widget ditambahkan ke layar Atur Home")
