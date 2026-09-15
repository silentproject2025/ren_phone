// =================================================================
// phone_notif_center.ino  (v90)
// Notification Shade + antrean Dynamic Island + context menu ketuk-lama.
//
// SENGAJA dinamai "phone_notif_center" (bukan "notif_center") supaya
// urutan compile Arduino ("phone.ino" < "phone_notif_center.ino" scr
// alfabetis) taruh file ini SETELAH phone.ino -- jadi semua yg
// didefinisikan di phone.ino (struct Theme, T(), SCR_W/SCR_H,
// drawGlassPanel, drawAppIcon, showToast, needRedraw, dst) otomatis
// kelihatan di sini TANPA perlu extern satupun. Pola kebalikan dari
// kasus nesRomPath di nes_app_renphone.ino (itu file yg lebih AWAL
// perlu extern krn butuh sesuatu yg didefinisikan belakangan).
//
// notifShadeOpen & diMenuOpen (bool) SENGAJA TETAP dideklarasikan di
// phone.ino (bukan di sini) krn render/touch dispatch di phone.ino
// butuh 2 variabel itu -- kalau taruh di sini, phone.ino gak akan bisa
// "lihat" variabelnya (arah sebaliknya dari alasan di atas).
//
// BELUM DICOBA COMPILE -- lingkungan build ESP32-nya gak tersedia di
// sini. Tolong compile & tes di board asli sebelum di-flash beneran,
// terutama cek posisi/ukuran pixel (nsPanelH, rowH, dll) yg mungkin
// perlu disetel ulang di layar fisik kamu.
// =================================================================

// -----------------------------------------------------------------
// HISTORI NOTIFIKASI -- dicatat SETIAP diNotify() dipanggil (lihat
// diNotify() yg sudah diubah di phone.ino), independen dari status DI
// lagi nampilin apa. Ring buffer sederhana, terbaru selalu di index 0.
// -----------------------------------------------------------------
// v91: 2 field baru -- linkScreen & hasLink -- supaya tiap baris histori
// INGAT app mana yg terkait (kalau ada), jadi Notification Shade bisa
// buka app itu pas baris-nya diketuk (lihat notifShadeTap() di bawah).
// Sebelumnya info link ini SAMA SEKALI gak disimpan ke histori (cuma
// dipakai sesaat lewat diLink()/diLinkedScreen utk pil DI yg lg tampil),
// makanya ketuk baris di shade dulu gak ngapa2in (lihat catatan lama di
// notifShadeTouch()).
struct NotifEntry { char icon; String title; uint16_t color; String badge; unsigned long ts; Screen linkScreen; bool hasLink; };
#define NOTIF_HIST_MAX 15
NotifEntry    notifHist[NOTIF_HIST_MAX];
int           notifHistCount = 0; // berapa slot terisi (maks NOTIF_HIST_MAX)

// v91: 2 parameter baru (linkScreen, hasLink) -- SENGAJA tanpa default
// value (beda dr diNotify()/notifAdd()) biar gak nambah kasus fungsi
// ber-default-parameter baru yg rawan kena bug ctags/prototype-otomatis
// yg sudah didiagnosis panjang di notif "v16"/"v90" (lihat forward
// declaration manual diNotify() di dekat struct Theme). notifHistPush()
// cuma dipanggil dari SATU tempat (diNotify() di phone.ino), jadi
// gampang aja update pemanggilnya kasih 2 argumen ini eksplisit,
// gak perlu default.
void notifHistPush(char icon, const String& title, uint16_t color, const String& badge,
                    Screen linkScreen, bool hasLink){
  int last = min(notifHistCount, NOTIF_HIST_MAX-1);
  for(int i=last; i>0; i--) notifHist[i]=notifHist[i-1];
  notifHist[0].icon=icon; notifHist[0].title=title; notifHist[0].color=color;
  notifHist[0].badge=badge; notifHist[0].ts=millis();
  notifHist[0].linkScreen=linkScreen; notifHist[0].hasLink=hasLink;
  if(notifHistCount<NOTIF_HIST_MAX) notifHistCount++;
}

// -----------------------------------------------------------------
// ANTREAN DYNAMIC ISLAND -- notif yg masuk selagi DI masih sibuk
// nampilin notif lain ditaruh di sini dulu (FIFO), BUKAN nimpa yg lg
// tampil (perilaku lama). diUpdate() (phone.ino) manggil diQueuePop()
// tiap kali 1 notif SELESAI tampil.
// -----------------------------------------------------------------
struct DiQueueItem { char icon; String title; uint16_t color; String badge; unsigned long expandMs; bool pulse; };
#define DI_QUEUE_MAX 6
DiQueueItem diQueue[DI_QUEUE_MAX];
int         diQueueCount = 0;

void diQueuePush(char icon, const String& title, uint16_t color, const String& badge,
                  unsigned long expandMs, bool pulse){
  if(diQueueCount>=DI_QUEUE_MAX){
    // antrean penuh -- drop yg PALING LAMA nunggu (index 0), bukan tolak
    // yg baru masuk; notif TERBARU lebih mungkin masih relevan drpd yg
    // udah lama ngantre.
    for(int i=1;i<DI_QUEUE_MAX;i++) diQueue[i-1]=diQueue[i];
    diQueueCount--;
  }
  diQueue[diQueueCount].icon=icon; diQueue[diQueueCount].title=title;
  diQueue[diQueueCount].color=color; diQueue[diQueueCount].badge=badge;
  diQueue[diQueueCount].expandMs=expandMs; diQueue[diQueueCount].pulse=pulse;
  diQueueCount++;
}

// Keluarin item paling depan antrean lewat parameter referensi (BUKAN
// return struct) -- sengaja, biar signature fungsi cuma pakai tipe
// dasar yg udah pasti dikenal generator prototype otomatis Arduino
// (lihat catatan showToast() soal default-arg di phone.ino; struct
// custom lintas-file lebih rawan kena masalah serupa).
bool diQueuePop(char &icon, String &title, uint16_t &color, String &badge,
                unsigned long &expandMs, bool &pulse){
  if(diQueueCount<=0) return false;
  icon=diQueue[0].icon; title=diQueue[0].title; color=diQueue[0].color;
  badge=diQueue[0].badge; expandMs=diQueue[0].expandMs; pulse=diQueue[0].pulse;
  for(int i=1;i<diQueueCount;i++) diQueue[i-1]=diQueue[i];
  diQueueCount--;
  return true;
}

// -----------------------------------------------------------------
// NOTIFICATION SHADE -- panel kaca ngambang dari tepi ATAS, isinya
// histori notifHist[]. Dibuka lewat swipe-turun dari SEPARUH KIRI tepi
// atas (separuh kanan tetap Control Center, lihat gesture split di
// loop() phone.ino). Pola animasi PERSIS sama dgn ccOffset/ccAnimating
// Control Center (drawGlassPanel, easeOutBackSoft buka / navEase tutup).
// -----------------------------------------------------------------
float         nsOffset = 0;
bool          nsAnimating = false;
float         nsAnimFromH = 0, nsAnimToH = 0;
unsigned long nsAnimStartMs = 0;
const float   NS_ANIM_MS = 140.0f; // v92: dulu 200ms (sama kayak Control Center) tp kerasa lambat naiknya -- dipercepat dikit (permintaan user)
#define NS_ROW_H 42
#define NS_MAX_ROWS_SHOWN 6

// v91: dulu NS_MAX_ROWS_SHOWN cuma dipakai buat POTONG daftar (sisanya
// cuma keliatan sbg teks "+N lainnya", gak bisa dibuka/discroll -- lihat
// keluhan user). Sekarang dipakai sbg jumlah baris yg MUAT di VIEWPORT
// (area konten yg keliatan skaligus); kalau histori lebih banyak dari
// itu, sisanya tetap ada & bisa dijangkau lewat scroll (nsScrollY),
// bukan cuma dibuang jadi teks ringkasan.
float nsScrollY = 0;      // offset scroll konten (px), 0 = paling atas/terbaru
int   nsPressedRow = -1;  // index baris yg lg ditekan jari -- highlight + dibatalkan kalau ternyata jari geser (scroll)
bool  nsIsSwiping = false;
int   nsTouchStartX=0, nsTouchStartY=0, nsTouchLastY=0;
unsigned long nsTouchStartMs=0;

int nsPanelH(){
  int rows = min(notifHistCount, NS_MAX_ROWS_SHOWN);
  int h = (notifHistCount==0) ? 90 : (28 + rows*NS_ROW_H + 40);
  int maxAvail = SCR_H-8; // jgn sampai nutup 1 layar penuh, sama spt ccPanelH()
  return h<maxAvail? h : maxAvail;
}
int nsViewportRows(){ return min(notifHistCount, NS_MAX_ROWS_SHOWN); }
// v91: batas atas nsScrollY -- selisih antara tinggi TOTAL konten (semua
// baris histori berurutan) dgn tinggi viewport yg beneran keliatan.
int nsMaxScroll(){
  int totalH = notifHistCount*NS_ROW_H;
  int viewportH = nsViewportRows()*NS_ROW_H;
  int m = totalH-viewportH;
  return m>0? m:0;
}

void openNotifShade(){
  notifShadeOpen=true;
  nsAnimFromH=nsOffset; nsAnimToH=(float)nsPanelH();
  nsAnimStartMs=millis(); nsAnimating=true;
  nsScrollY=0; nsPressedRow=-1; nsIsSwiping=false; // v91: tiap dibuka, mulai dari atas & bersih dari sisa state sentuhan sblmnya
  needRedraw=true;
}
void closeNotifShade(){
  nsAnimFromH=nsOffset; nsAnimToH=0.0f;
  nsAnimStartMs=millis(); nsAnimating=true;
  needRedraw=true;
}
// Dipanggil TIAP frame dari loop() (phone.ino), pola sama dgn blok
// ccAnimating -- diletakkan sbg fungsi terpisah (bukan inline di
// loop()) krn nsOffset/nsAnimating dkk hidup di file ini.
void notifShadeAnimTick(){
  if(!nsAnimating) return;
  float t=(millis()-nsAnimStartMs)/NS_ANIM_MS;
  if(t>=1.0f){
    t=1.0f; nsAnimating=false;
    if(nsAnimToH<=0.0f) notifShadeOpen=false;
  }
  bool opening = nsAnimToH>nsAnimFromH;
  float eased = opening ? easeOutBackSoft(t) : navEase(t);
  nsOffset = nsAnimFromH + (nsAnimToH-nsAnimFromH)*eased;
  if(nsOffset<0) nsOffset=0;
  needRedraw=true;
}

void drawNotifShade(LGFX_Sprite& s){
  int ph=(int)nsOffset;
  if(ph<=0) return;
  int rad=min(22,ph/2); if(rad<0) rad=0;
  drawGlassPanel(s, 0,0,SCR_W,ph,rad, T().surface, 200);
  s.fillRoundRect(SCR_W/2-14,ph-9,28,4,2,T().divider);

  s.setTextColor(T().text); s.setTextSize(1);
  s.setCursor(10,8); s.print("Notifikasi");

  if(notifHistCount==0){
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(10,32); s.print("Tidak ada notifikasi");
  } else {
    int rowTop=26;
    int btnY=ph-32;
    // v91: viewportBottom = batas bawah area yg boleh digambar baris --
    // dulu dipotong per-baris (kalau nabrak ph-36 langsung stop nge-loop).
    // Sekarang dibikin clip rect beneran, isi yg lewat clip otomatis gak
    // kegambar (jadi loop TETAP jalan penuh utk semua baris, biar scroll
    // ke bawah/atas bisa nunjukin baris yg tadinya "kepotong").
    int viewportBottom = rowTop + nsViewportRows()*NS_ROW_H;
    if(viewportBottom > btnY-4) viewportBottom = btnY-4;
    s.setClipRect(0, rowTop, SCR_W, max(0,viewportBottom-rowTop));
    for(int i=0;i<notifHistCount;i++){
      int y=rowTop+i*NS_ROW_H-(int)nsScrollY;
      if(y+NS_ROW_H < rowTop) continue;   // di atas viewport, skip (masih di-clip tapi hemat gambar)
      if(y > viewportBottom) break;       // di bawah viewport, sisanya pasti jg di bawah -> stop
      NotifEntry &n = notifHist[i];
      int iconR=13, cx=10+iconR, cy=y+NS_ROW_H/2;
      // v91: baris yg PUNYA link app (n.hasLink) dikasih cincin tipis di
      // belakang ikon -- pola & warna PERSIS sama dgn cincin ikon Home
      // (nyala T().accent pas ditekan) -- sinyal visual "baris ini bisa
      // diketuk", konsisten sama bahasa desain yg sudah ada, bukan bikin
      // pola baru.
      if(n.hasLink){
        bool pressed=(nsPressedRow==i);
        s.drawCircle(cx,cy,iconR+3, pressed? T().accent : T().divider);
      }
      drawAppIcon(s, n.icon, cx, cy, iconR, n.color); // pola sama persis dgn ikon di pil DI

      String t = n.title;
      if(t.length()>18) t = t.substring(0,16)+"..";
      s.setTextColor(T().text); s.setTextSize(1);
      s.setCursor(cx+iconR+8, y+NS_ROW_H/2-9); s.print(t.c_str());

      unsigned long ageMs = millis()-n.ts;
      char ageStr[14];
      if(ageMs<60000UL) strcpy(ageStr,"baru saja");
      else if(ageMs<3600000UL) snprintf(ageStr,sizeof(ageStr),"%lum lalu",ageMs/60000UL);
      else snprintf(ageStr,sizeof(ageStr),"%ljam lalu",ageMs/3600000UL);
      s.setTextColor(T().subtext);
      s.setCursor(cx+iconR+8, y+NS_ROW_H/2+3); s.print(ageStr);

      s.drawFastHLine(8, y+NS_ROW_H-2, SCR_W-16, T().divider);
    }
    s.clearClipRect();
    // v91: rel+thumb scroll tipis di tepi kanan -- pola sama persis dgn
    // indikator scroll grid Home (drawHomeScreen), cuma nyala kalau
    // konten beneran overflow (nsMaxScroll()>0).
    int maxS = nsMaxScroll();
    if(maxS>0){
      int trackH = viewportBottom-rowTop;
      int totalH = notifHistCount*NS_ROW_H;
      int thumbH = max(14, trackH*(trackH)/totalH);
      int thumbY = rowTop + (int)((trackH-thumbH)*(nsScrollY/(float)maxS));
      s.fillRoundRect(SCR_W-5, rowTop, 2, trackH, 1, T().divider);
      s.fillRoundRect(SCR_W-5, thumbY, 2, thumbH, 1, T().subtext);
    }
  }

  int btnY=ph-32, btnH=24;
  s.fillRoundRect(10,btnY,SCR_W-20,btnH,10,T().surface2);
  s.setTextColor(T().danger); s.setTextSize(1);
  const char* clearLbl="Bersihkan Semua";
  int clw=s.textWidth(clearLbl);
  s.setCursor(SCR_W/2-clw/2, btnY+7); s.print(clearLbl);
}

// v91: DIPISAH jadi 2 fungsi -- notifShadeTap() (logika TAP asli,
// namanya diganti dari notifShadeTouch) yg cuma jalan pas jari kekonfirmasi
// jadi tap pendek (bukan scroll), dan notifShadeInput() (BARU) yg jalan
// TIAP frame slama shade kebuka, ngurusin bedain tap-vs-scroll persis pola
// yg sudah dipakai grid Home (touchStartY/isSwiping/ambang 12px) -- lihat
// blok curScreen()==SCR_HOME di loop().
void notifShadeTap(int x,int y){
  int ph=(int)nsOffset;
  // ketuk drag-handle atau area kosong di bawah panel -> tutup, pola
  // sama persis dgn ccTouch()
  if(y> ph-9 && y<=ph+4){ closeNotifShade(); return; }
  if(y>ph) { closeNotifShade(); return; }

  int btnY=ph-32, btnH=24;
  if(y>=btnY && y<=btnY+btnH && x>=10 && x<=SCR_W-10){
    // "Bersihkan Semua" -- cuma histori SHADE yg dibersihin. Notif yg LG
    // TAMPIL di DI (kalau ada) sengaja DIBIARKAN jalan sampai kelar
    // sendiri, bukan diinterupsi paksa dari sini.
    notifHistCount=0; nsScrollY=0;
    needRedraw=true;
    return;
  }

  // v91: ketuk salah satu baris -- kalau baris itu PUNYA link app
  // (n.hasLink, lihat NotifEntry/notifHistPush di atas), tutup shade &
  // pindah ke app terkait lewat navPush() yg SUDAH ADA -- otomatis dapat
  // transisi mulus yg sama persis dipakai tiap buka app dari Home/dock,
  // gak perlu bikin animasi baru.
  int rowTop=26;
  if(y>=rowTop && notifHistCount>0){
    int idx = (y-rowTop+(int)nsScrollY)/NS_ROW_H;
    if(idx>=0 && idx<notifHistCount){
      NotifEntry &n = notifHist[idx];
      bool hasLink=n.hasLink; Screen target=n.linkScreen;
      closeNotifShade();
      if(hasLink) navPush(target);
      return;
    }
  }
}

void notifShadeInput(bool touched, bool newT, int tx, int ty){
  int ph=(int)nsOffset;
  int rowTop=26, btnY=ph-32;
  int viewportBottom = rowTop + nsViewportRows()*NS_ROW_H;
  if(viewportBottom>btnY-4) viewportBottom=btnY-4;

  if(newT){
    nsTouchStartX=tx; nsTouchStartY=ty; nsTouchLastY=ty;
    nsIsSwiping=false; nsTouchStartMs=millis();
    // highlight instan pas jari nempel baris (pola sama dgn homePressedIdx)
    if(ty>=rowTop && ty<viewportBottom && notifHistCount>0){
      int idx=(ty-rowTop+(int)nsScrollY)/NS_ROW_H;
      nsPressedRow=(idx>=0&&idx<notifHistCount)? idx : -1;
    } else nsPressedRow=-1;
    needRedraw=true;
    return;
  }
  if(touched){
    int dy=ty-nsTouchStartY;
    if(!nsIsSwiping && abs(dy)>12){ nsIsSwiping=true; nsPressedRow=-1; } // jelas ini scroll, bukan tap -> batal highlight (sama pola homePressedIdx)
    if(nsIsSwiping){
      float delta=(float)(nsTouchLastY-ty);
      nsScrollY += delta;
      float maxS=(float)nsMaxScroll();
      if(nsScrollY<0) nsScrollY=0;
      if(nsScrollY>maxS) nsScrollY=maxS;
      needRedraw=true;
    }
    nsTouchLastY=ty;
    return;
  }
  // jari baru diangkat
  if(!nsIsSwiping && millis()-nsTouchStartMs<400) notifShadeTap(nsTouchStartX,nsTouchStartY);
  nsPressedRow=-1; nsIsSwiping=false;
  needRedraw=true;
}

// -----------------------------------------------------------------
// CONTEXT MENU ketuk-lama Dynamic Island -- muncul nggantung persis di
// bawah pil DI yg lagi ditahan (diCurX/diCurY/diCurW/diCurH, semua
// sudah ada di phone.ino). Sengaja TANPA animasi (buka/tutup instan) --
// beda dari CC/Shade yg punya animasi slide, krn menu ini sifatnya
// transient & dipicu long-press (bukan gestur swipe yg "mengalir").
// -----------------------------------------------------------------
int diMenuX=0, diMenuY=0;
const int DI_MENU_W=118, DI_MENU_H=78;

void openDiMenu(){
  diMenuX = constrain(diCurX + (int)diCurW/2 - DI_MENU_W/2, 4, SCR_W-DI_MENU_W-4);
  diMenuY = diCurY + (int)diCurH + 6;
  diMenuOpen=true; needRedraw=true;
}
void closeDiMenu(){ diMenuOpen=false; needRedraw=true; }

void drawDiMenu(LGFX_Sprite& s){
  drawGlassPanel(s, diMenuX,diMenuY,DI_MENU_W,DI_MENU_H,10, T().surface, 215);
  const char* items[3] = {"Sembunyikan","Buka Notifikasi","Bersihkan Semua"};
  int rowH=DI_MENU_H/3;
  for(int i=0;i<3;i++){
    s.setTextColor(i==2? T().danger : T().text); s.setTextSize(1);
    s.setCursor(diMenuX+10, diMenuY+i*rowH+rowH/2-4);
    s.print(items[i]);
    if(i<2) s.drawFastHLine(diMenuX+6, diMenuY+(i+1)*rowH, DI_MENU_W-12, T().divider);
  }
}

void diMenuTouch(int x,int y){
  if(x<diMenuX || x>diMenuX+DI_MENU_W || y<diMenuY || y>diMenuY+DI_MENU_H){
    closeDiMenu(); return; // ketuk di luar kotak menu -> tutup aja, gak ada aksi
  }
  int rowH=DI_MENU_H/3;
  int row=(y-diMenuY)/rowH;
  closeDiMenu();
  if(row==0){ diHide(); }              // "Sembunyikan" -- collapse notif yg lg tampil skrg
  else if(row==1){ openNotifShade(); } // "Buka Notifikasi" -- langsung ke shade penuh
  else if(row==2){ notifHistCount=0; needRedraw=true; } // "Bersihkan Semua"
}
