#!/usr/bin/env python3
# ATUR HOME: urutan ikon + sembunyikan app dari grid Home (tetap bisa dicari lewat "Cari app...").
#  - Pengaturan > TAMPILAN > "Atur Layar Home"  (juga muncul di pencarian sebagai "Atur Home")
#  - Daftar: tombol naik / turun / Tampil-Sembunyi per app, geser untuk gulir, Reset (ketuk 2x)
#  - Disimpan di NVS "homelay" per ID layar (bukan per nomor urut) -> aman kalau nanti ada app baru
# Mengubah phone.ino + membuat homeedit_renphone.ino + (kalau ada) workflow GitHub.
# Aman diulang. Semua anchor dicek dulu; kalau ada yang tidak ketemu, TIDAK ada file yang diubah.
import sys, os, re
if not os.path.exists('phone.ino'):
    print("ERROR: phone.ino tidak ada di folder ini"); sys.exit(1)
p=open('phone.ino',encoding='utf-8').read()
if 'HOME-LAYOUT' in p:
    print("Atur Home sudah terpasang, dilewati"); sys.exit(0)

def rep(src, old, new, what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)

# ---- 1) enum Screen ----
p=rep(p," };\nenum Orientation",",\n              // Atur Home -- lihat homeedit_renphone.ino\n              SCR_HOMEEDIT };\nenum Orientation","enum Screen")

# ---- 2) daftar app: jumlah + forward decl + entri (disembunyikan dari Home secara default) ----
m=re.search(r'AppDef apps\[(\d+)\] = \{',p); m2=re.search(r'#define APP_COUNT (\d+)',p)
if not m or not m2 or m.group(1)!=m2.group(1):
    print("ERROR: apps[]/APP_COUNT tidak ketemu atau tidak sama. Tidak ada yang diubah."); sys.exit(1)
n=int(m.group(1))
endArr=p.index("\n};",m.end())
ENTRY="\n  // ---- Atur Home: urutan & sembunyikan ikon (disembunyikan dari grid secara default; ada di Pengaturan) ----\n  { \"Atur Home\", '#', 0xFBE0, homeEditEnter, homeEditExit, drawHomeEdit, homeEditTouch, SCR_HOMEEDIT },"
p=p[:endArr]+ENTRY+p[endArr:]
DECL="// ---- homeedit_renphone.ino: Atur Home ----\nvoid homeEditEnter(); void homeEditExit(); void drawHomeEdit(LGFX_Sprite&); void homeEditTouch(int,int,bool,bool);\nvoid homeEditPointerTick(bool,bool,int,int);\n\n"
p=rep(p,"AppDef apps[%d] = {"%n, DECL+"AppDef apps[%d] = {"%(n+1),"apps[N]")
p=rep(p,"#define APP_COUNT %d"%n,"#define APP_COUNT %d"%(n+1),"APP_COUNT")

# ---- 3) data & fungsi layout Home (urutan, sembunyi, simpan/muat) ----
LAYOUT=r'''// ---- HOME-LAYOUT: urutan & sembunyikan ikon grid Home (diatur di layar "Atur Home") ----
// homeOrder[] = urutan SEMUA app (isi: indeks apps[]), homeHide[] per indeks apps[].
// homeVis[]/homeVisN = yang tampil di grid Home. Disimpan di NVS "homelay" per ID Screen
// (bukan per indeks) supaya tetap benar kalau nanti ada app baru. Pencarian tetap mencari SEMUA app.
int  homeOrder[APP_COUNT];
bool homeHide[APP_COUNT];
int  homeVis[APP_COUNT];
int  homeVisN = 0;
bool homeLayoutLoaded = false;

void homeLayoutRebuildVis(){
  homeVisN=0;
  for(int k=0;k<APP_COUNT;k++){ int i=homeOrder[k]; if(!homeHide[i]) homeVis[homeVisN++]=i; }
}
void homeLayoutDefault(){
  for(int i=0;i<APP_COUNT;i++){ homeOrder[i]=i; homeHide[i]=(apps[i].screen==SCR_HOMEEDIT); }
  homeLayoutRebuildVis();
}
void homeLayoutLoad(){
  homeLayoutLoaded=true;
  homeLayoutDefault();
  Preferences pr;
  if(!pr.begin("homelay",true)) return;
  size_t n=pr.getBytesLength("ord");
  if(n>0 && n<=64){
    uint8_t ids[64];
    pr.getBytes("ord",ids,n);
    uint64_t hid=pr.getULong64("hid",0);
    bool used[APP_COUNT];
    for(int i=0;i<APP_COUNT;i++) used[i]=false;
    int w=0;
    for(size_t k=0;k<n && w<APP_COUNT;k++){
      for(int i=0;i<APP_COUNT;i++){
        if(!used[i] && (int)apps[i].screen==(int)ids[k]){
          homeOrder[w++]=i; used[i]=true;
          homeHide[i]=(ids[k]<64) ? (((hid>>ids[k])&1ULL)!=0) : false;
          break;
        }
      }
    }
    for(int i=0;i<APP_COUNT;i++) if(!used[i]) homeOrder[w++]=i;   // app baru: ke akhir, aturan default
  }
  pr.end();
  homeLayoutRebuildVis();
}
void homeLayoutSave(){
  uint8_t ids[64]; uint64_t hid=0; int n=0;
  for(int k=0;k<APP_COUNT && n<64;k++){
    int i=homeOrder[k]; int id=(int)apps[i].screen;
    ids[n++]=(uint8_t)id;
    if(homeHide[i] && id<64) hid|=(1ULL<<id);
  }
  Preferences pr;
  if(!pr.begin("homelay",false)) return;
  pr.putBytes("ord",ids,n);
  pr.putULong64("hid",hid);
  pr.end();
}
void homeLayoutEnsure(){ if(!homeLayoutLoaded) homeLayoutLoad(); }

'''
p=rep(p,"int homeCols(){ return currentOrient",LAYOUT+"int homeCols(){ return currentOrient","homeCols")

# ---- 4) grid Home pakai daftar tampil ----
p=rep(p,"""    // ---- grid app ----
    for(int i=0;i<APP_COUNT;i++){
      int col=i%cols, row=i/cols;
""","""    // ---- grid app ---- (HOME-LAYOUT: urutan & app tersembunyi dari homeVis[])
    homeLayoutEnsure();
    for(int k=0;k<homeVisN;k++){
      int i=homeVis[k];
      int col=k%cols, row=k/cols;
""","drawHome grid")
p=rep(p,"""  for(int i=0;i<APP_COUNT;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-scPx;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[i].screen;
  }""","""  homeLayoutEnsure();
  for(int k=0;k<homeVisN;k++){ // HOME-LAYOUT
    int col=k%cols,row=k/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-scPx;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[homeVis[k]].screen;
  }""","homeCheck")
p=rep(p,"""  for(int i=0;i<APP_COUNT;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-scPx;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return i;
  }""","""  homeLayoutEnsure();
  for(int k=0;k<homeVisN;k++){ // HOME-LAYOUT
    int col=k%cols,row=k/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-scPx;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return homeVis[k];
  }""","homeIndexAt")
p=rep(p,"  int cols=homeCols();\n  int rows=(APP_COUNT+cols-1)/cols;\n","  homeLayoutEnsure();\n  int cols=homeCols();\n  int rows=(homeVisN+cols-1)/cols; // HOME-LAYOUT: hanya app yang tampil\n","homeMaxScroll")

# ---- 5) ikon '#' (grid 2x2) ----
p=rep(p,"    case 'H': { // HWmonitor","""    case '#': { // Atur Home: grid 2x2 kotak
      int q=max(3,r/3), g2=max(2,r/6), tot=q*2+g2, x0=cx-tot/2, y0=cy-tot/2;
      for(int a=0;a<2;a++) for(int b=0;b<2;b++)
        s.fillRoundRect(x0+a*(q+g2),y0+b*(q+g2),q,q,2,ic);
      break;
    }
    case 'H': { // HWmonitor""","icon")

# ---- 6) Pengaturan > TAMPILAN > Atur Layar Home ----
p=rep(p," };\n#define SETT_MAXROWS 40",", SR_HOMEEDIT };\n#define SETT_MAXROWS 40","enum SR")
p=rep(p,"  settAddRow(SR_ROT,g,34,0,nullptr,y); g++;\n","  settAddRow(SR_ROT,g,34,0,nullptr,y);\n  settAddRow(SR_HOMEEDIT,g,34,0,nullptr,y); g++;\n","settBuildLayout")
p=rep(p,"      case SR_ROT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"Rotasi Otomatis\");\n",
 "      case SR_HOMEEDIT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"Atur Layar Home\");\n        settChevron(s,xr-4,ry+rh/2,T().subtext);\n        break;\n      case SR_ROT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"Rotasi Otomatis\");\n","draw SR_ROT")
p=rep(p,"    case SR_ROT:\n      autoRotateEnabled=!autoRotateEnabled;","    case SR_HOMEEDIT:\n      navPush(SCR_HOMEEDIT); break;\n    case SR_ROT:\n      autoRotateEnabled=!autoRotateEnabled;","touch SR_ROT")

# ---- 7) pointer tick (geser daftar + ketuk saat lepas) ----
lines=p.split('\n'); hit=[i for i,l in enumerate(lines) if 'galPointerTick(touched,wasTouched,tx,ty)' in l]
if len(hit)!=1:
    print("ERROR: anchor pointer-tick Galeri ketemu %d kali (harus 1). Tidak ada yang diubah."%len(hit)); sys.exit(1)
lines.insert(hit[0]+1,"    if(curScreen()==SCR_HOMEEDIT) homeEditPointerTick(touched,wasTouched,tx,ty);    // Atur Home: geser daftar + ketuk saat lepas")
p='\n'.join(lines)

# ---- 8) file baru ----
NEWFILE=r'''// =====================================================================
// homeedit_renphone.ino -- ATUR HOME: urutan ikon + sembunyikan app dari grid Home.
// Data (homeOrder/homeHide/homeVis + simpan/muat NVS) ada di phone.ino (dipakai drawHome).
// Layar ini cuma antarmuka: daftar semua app, tombol naik / turun / Tampil-Sembunyi per baris.
// Hanya tipe primitif di parameter fungsi (aman dari bug prototype otomatis arduino-cli).
// =====================================================================
#define HE_RH 30
static int  heScroll = 0;
static bool heDown = false, heDrag = false;
static int  heSx = 0, heSy = 0, heLy = 0;
static unsigned long heResetArmUntil = 0;

static int heTop() { return 48; }
static int heBot() { return backY() - 6; }
static int heMaxScroll() { int m = APP_COUNT * HE_RH - (heBot() - heTop()); return m > 0 ? m : 0; }
static void heClamp() { int m = heMaxScroll(); if (heScroll > m) heScroll = m; if (heScroll < 0) heScroll = 0; }

// tombol di tiap baris (dari kanan): [naik] [turun] [Tampil/Sembunyi]
static int heVisW()  { return 56; }
static int heVisX()  { return SCR_W - 14 - heVisW(); }
static int heDownX() { return heVisX() - 4 - 26; }
static int heUpX()   { return heDownX() - 4 - 26; }
static void heResetRect(int& x, int& y, int& w, int& h) { w = 68; h = 20; x = SCR_W - w - 8; y = 27; }

void homeEditEnter() {
  homeLayoutEnsure();
  heScroll = 0; heDown = false; heDrag = false; heResetArmUntil = 0;
}

void homeEditExit() {
  heResetArmUntil = 0; heDown = false;
  homeScrollY = 0; homeScrollVel = 0; homeScrollSpringing = false;   // jumlah baris Home bisa berubah -> mulai dari atas
}

void drawHomeEdit(LGFX_Sprite& s) {
  homeLayoutEnsure();
  iosBackdrop(s); drawStatusBar(s);
  iosTitle(s, 8, 26, "Atur Home");

  int rx, ry, rw, rh; heResetRect(rx, ry, rw, rh);
  bool armed = millis() < heResetArmUntil;
  drawGlassPanel(s, rx, ry, rw, rh, 10, armed ? T().danger : T().surface2, 170);
  s.setTextSize(1); s.setTextColor(armed ? (uint16_t)0xFFFF : T().danger);
  const char* rl = armed ? "Yakin?" : "Reset";
  s.setCursor(rx + rw / 2 - s.textWidth(rl) / 2, ry + rh / 2 - 4); s.print(rl);
  if (armed) needRedraw = true;                 // label "Yakin?" kembali sendiri saat waktu habis

  if (SCR_W > SCR_H) {
    char sm[28]; snprintf(sm, sizeof(sm), "%d tampil / %d sembunyi", homeVisN, APP_COUNT - homeVisN);
    s.setTextColor(T().subtext); s.setCursor(rx - 8 - s.textWidth(sm), 31); s.print(sm);
  }

  int top = heTop(), bot = heBot();
  drawGlassPanel(s, 8, top - 2, SCR_W - 16, bot - top + 4, 12, T().surface2, 150);
  heClamp();
  s.setClipRect(8, top, SCR_W - 16, bot - top);
  int nameX = 46, nameMax = heUpX() - 6 - nameX;
  for (int k = 0; k < APP_COUNT; k++) {
    int y = top + k * HE_RH - heScroll;
    if (y + HE_RH < top || y > bot) continue;
    int i = homeOrder[k];
    bool hid = homeHide[i];
    uint16_t ic = hid ? blend565(T().bg, apps[i].color, 90) : apps[i].color;
    drawAppIcon(s, apps[i].sym, 28, y + HE_RH / 2, 11, ic);

    char nm[24]; strncpy(nm, apps[i].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    int n = (int)strlen(nm);
    s.setTextSize(1);
    while (n > 1 && s.textWidth(nm) > nameMax) nm[--n] = 0;
    s.setTextColor(hid ? T().subtext : T().text);
    s.setCursor(nameX, y + HE_RH / 2 - 4); s.print(nm);

    int by = y + (HE_RH - 22) / 2;
    bool canUp = (k > 0), canDn = (k < APP_COUNT - 1);
    uint16_t on = T().text, off = blend565(T().bg, T().text, 60);
    drawGlassPanel(s, heUpX(), by, 26, 22, 8, T().surface2, 170);
    s.fillTriangle(heUpX() + 13, by + 6, heUpX() + 7, by + 15, heUpX() + 19, by + 15, canUp ? on : off);
    drawGlassPanel(s, heDownX(), by, 26, 22, 8, T().surface2, 170);
    s.fillTriangle(heDownX() + 7, by + 7, heDownX() + 19, by + 7, heDownX() + 13, by + 16, canDn ? on : off);
    drawGlassPanel(s, heVisX(), by, heVisW(), 22, 8, hid ? T().surface2 : T().accent, 190);
    const char* vl = hid ? "Sembunyi" : "Tampil";
    s.setTextColor(hid ? T().subtext : T().text);
    s.setCursor(heVisX() + heVisW() / 2 - s.textWidth(vl) / 2, by + 7); s.print(vl);

    if (k < APP_COUNT - 1) s.drawFastHLine(14, y + HE_RH - 1, SCR_W - 28, T().divider);
  }
  s.clearClipRect();

  drawBack(s);
  drawToast(s);
}

static void heTap(int x, int y) {
  int top = heTop();
  int k = (y - top + heScroll) / HE_RH;
  if (k < 0 || k >= APP_COUNT) return;
  int i = homeOrder[k];
  if (x >= heUpX() - 2 && x <= heUpX() + 28) {                    // naik
    if (k > 0) { int t = homeOrder[k - 1]; homeOrder[k - 1] = homeOrder[k]; homeOrder[k] = t; heScroll -= HE_RH; }
    else return;
  } else if (x >= heDownX() - 2 && x <= heDownX() + 28) {         // turun
    if (k < APP_COUNT - 1) { int t = homeOrder[k + 1]; homeOrder[k + 1] = homeOrder[k]; homeOrder[k] = t; heScroll += HE_RH; }
    else return;
  } else if (x >= heVisX() - 2 && x <= heVisX() + heVisW() + 2) { // tampil / sembunyi
    homeHide[i] = !homeHide[i];
    if (homeHide[i]) showToast("Disembunyikan -- tetap bisa dicari");
  } else return;
  heClamp();
  homeLayoutRebuildVis();
  homeLayoutSave();
  needRedraw = true;
}

// Dipanggil tiap putaran loop() selagi layar ini terbuka: geser daftar + ketuk diproses saat jari LEPAS.
void homeEditPointerTick(bool touched, bool wasT, int tx, int ty) {
  int top = heTop(), bot = heBot();
  if (touched) {
    if (!wasT) { heDown = (ty >= top && ty < bot); heSx = tx; heSy = ty; heLy = ty; heDrag = false; }
    else if (heDown) {
      if (!heDrag && abs(ty - heSy) > 8) heDrag = true;
      if (heDrag) { heScroll += heLy - ty; heLy = ty; heClamp(); needRedraw = true; }
    }
  } else {
    if (wasT && heDown && !heDrag) heTap(heSx, heSy);
    heDown = false;
  }
}

// Sentuhan generik (saat jari baru menyentuh): tombol Kembali & Reset. Daftar ditangani homeEditPointerTick().
void homeEditTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }
  int rx, ry, rw, rh; heResetRect(rx, ry, rw, rh);
  if (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh) {
    if (millis() < heResetArmUntil) {
      heResetArmUntil = 0;
      homeLayoutDefault(); homeLayoutSave();
      heScroll = 0;
      showToast("Home dikembalikan ke awal");
    } else {
      heResetArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk reset");
    }
    needRedraw = true;
  }
}
'''
# ---- 9) workflow ----
wf='.github/workflows/build-firmware.yml'
w=None
if os.path.exists(wf):
    w=open(wf,encoding='utf-8').read()
    if 'homeedit_renphone.ino' not in w:
        a="          cp owner_renphone.ino build_sketch/\n"
        if w.count(a)==1: w=w.replace(a,a+"          cp homeedit_renphone.ino build_sketch/\n")
        else: print("PERINGATAN: tambahkan 'cp homeedit_renphone.ino build_sketch/' ke workflow sendiri"); w=None
    else: w=None
open('homeedit_renphone.ino','w',encoding='utf-8').write(NEWFILE)
open('phone.ino','w',encoding='utf-8').write(p)
if w: open(wf,'w',encoding='utf-8').write(w)
print("OK: Atur Home terpasang (Pengaturan > TAMPILAN > Atur Layar Home)")
