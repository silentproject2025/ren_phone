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
  needRedraw=true;   // NOTIF-LOCK: kartu di lock screen ikut muncul
}

// hapus 1 baris histori (dipakai geser-hapus di lock screen)
void notifHistRemove(int idx){
  if(idx<0 || idx>=notifHistCount) return;
  for(int i=idx;i<notifHistCount-1;i++) notifHist[i]=notifHist[i+1];
  notifHistCount--;
  notifHist[notifHistCount].title=""; notifHist[notifHistCount].badge="";
  needRedraw=true;
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

// v133 (UI notif): geometri area daftar. NS_HEAD_H = tinggi judul (rowTop),
// NS_FOOT_H = ruang tombol "Bersihkan Semua" di dasar panel. Dulu angka 26/32/36
// ditulis langsung di 6 tempat -- dikumpulkan di sini supaya perhitungan tinggi
// viewport (yg juga dipakai batas scroll) satu sumber.
#define NS_HEAD_H 26
#define NS_FOOT_H 36
#define NS_LONG_MS 450UL   // tahan selama ini = bar aksi cepat
#define NS_OUT_MS  170.0f  // durasi baris terlempar keluar
extern bool panelPullActive; // ptr_renphone.ino: panel sedang ditarik jari -> jangan retarget tinggi

float nsScrollY = 0;      // offset scroll konten (px), 0 = paling atas/terbaru
int   nsPressedRow = -1;  // index BARIS (bukan histori) yg lg ditekan jari
bool  nsIsSwiping = false;
int   nsTouchStartX=0, nsTouchStartY=0, nsTouchLastY=0;
unsigned long nsTouchStartMs=0;

// -----------------------------------------------------------------
// v133: PENGELOMPOKAN PER APP. notifHist[] tetap ring buffer datar (lock
// screen & DI memakainya apa adanya); shade menyusun "baris tampil" nsRows[]
// dari situ. Satu app = satu grup: entri terbaru jadi baris induk (head),
// entri lain app yg sama jadi anak (hanya tampil bila grup dibuka).
// Kunci grup: app tertaut -> linkScreen; tak tertaut -> ikon.
// -----------------------------------------------------------------
struct NsRow { int8_t hist; int8_t cnt; bool head; }; // cnt = jumlah entri grup (hanya diisi utk head)
NsRow nsRows[NOTIF_HIST_MAX];
int   nsRowN = 0;
struct NsKey { bool used; bool hasLink; Screen scr; char icon; };
#define NS_EXP_MAX 6
NsKey nsExp[NS_EXP_MAX];  // grup yg sedang dibuka (diingat per kunci, bukan per index, krn index histori bergeser)

// SENGAJA semua fungsi di bawah menerima INDEX histori (int), bukan NotifEntry&:
// prototype otomatis Arduino disisipkan di puncak phone.ino, sebelum struct NotifEntry
// dikenal -- signature bertipe struct dari file ini akan gagal compile (lihat catatan v16/v90).
bool nsSameIdx(int a, int b){
  const NotifEntry &x=notifHist[a], &y=notifHist[b];
  if(x.hasLink != y.hasLink) return false;
  return x.hasLink ? (x.linkScreen==y.linkScreen) : (x.icon==y.icon);
}
int nsExpFindIdx(int h){
  const NotifEntry &e=notifHist[h];
  for(int i=0;i<NS_EXP_MAX;i++){
    if(!nsExp[i].used || nsExp[i].hasLink!=e.hasLink) continue;
    if(e.hasLink ? (nsExp[i].scr==e.linkScreen) : (nsExp[i].icon==e.icon)) return i;
  }
  return -1;
}
void nsExpToggleIdx(int h){
  const NotifEntry &e=notifHist[h];
  int f=nsExpFindIdx(h);
  if(f>=0){ nsExp[f].used=false; return; }
  int slot=-1;
  for(int i=0;i<NS_EXP_MAX;i++) if(!nsExp[i].used){ slot=i; break; }
  if(slot<0){ for(int i=1;i<NS_EXP_MAX;i++) nsExp[i-1]=nsExp[i]; slot=NS_EXP_MAX-1; } // penuh -> buang yg tertua
  nsExp[slot].used=true; nsExp[slot].hasLink=e.hasLink; nsExp[slot].scr=e.linkScreen; nsExp[slot].icon=e.icon;
}
void nsExpClear(){ for(int i=0;i<NS_EXP_MAX;i++) nsExp[i].used=false; }

// Susun ulang nsRows[]. Murah (<=15 entri) sehingga boleh dipanggil tiap frame/tiap fungsi yg butuh.
void nsBuildRows(){
  nsRowN=0;
  bool seen[NOTIF_HIST_MAX];
  for(int i=0;i<NOTIF_HIST_MAX;i++) seen[i]=false;
  for(int i=0;i<notifHistCount;i++){
    if(seen[i]) continue;
    seen[i]=true;
    int cnt=1;
    for(int j=i+1;j<notifHistCount;j++) if(!seen[j] && nsSameIdx(i,j)) cnt++;
    nsRows[nsRowN].hist=(int8_t)i; nsRows[nsRowN].cnt=(int8_t)cnt; nsRows[nsRowN].head=true; nsRowN++;
    bool open = (cnt>1) && nsExpFindIdx(i)>=0;
    for(int j=i+1;j<notifHistCount;j++){
      if(seen[j] || !nsSameIdx(i,j)) continue;
      seen[j]=true;
      if(open){ nsRows[nsRowN].hist=(int8_t)j; nsRows[nsRowN].cnt=0; nsRows[nsRowN].head=false; nsRowN++; }
    }
  }
}

// ---- tinggi panel & viewport (v133: viewport dihitung dari tinggi panel NYATA) ----
// BUG LAMA (terlihat di landscape): panel dibatasi SCR_H-8 (232 px) sehingga hanya
// muat ~4 baris, tapi batas scroll mengira 6 baris muat -> 2 baris terbawah tak
// bisa dijangkau. Sekarang viewport = min(baris*tinggi, tinggiPanel - header - footer).
int nsPanelH(){
  nsBuildRows();
  int rows = min(nsRowN, NS_MAX_ROWS_SHOWN);
  int h = (nsRowN==0) ? 90 : (28 + rows*NS_ROW_H + 40);
  int maxAvail = SCR_H-8; // jgn sampai nutup 1 layar penuh, sama spt ccPanelH()
  return h<maxAvail? h : maxAvail;
}
int nsViewportH(){
  int rows = min(nsRowN, NS_MAX_ROWS_SHOWN);
  int vp = rows*NS_ROW_H;
  int cap = nsPanelH() - NS_HEAD_H - NS_FOOT_H;
  if(vp>cap) vp=cap;
  return vp<0? 0 : vp;
}
int nsMaxScroll(){
  nsBuildRows();
  int m = nsRowN*NS_ROW_H - nsViewportH();
  return m>0? m:0;
}

// ---- state geser-hapus, bar aksi, animasi baris ----
int   nsSwipeRow=-1;              // baris yg sedang digeser / membal balik
bool  nsSwipeH=false;             // jari masih menggeser mendatar
float nsSwipeDx=0, nsSwipeVel=0;
int   nsSwipeLastX=0; unsigned long nsSwipeLastMs=0;
bool  nsSwipeArmed=false;         // sudah melewati ambang buang (utk getar 1x)
int   nsOutRow=-1, nsOutDir=1; float nsOutFrom=0; unsigned long nsOutT0=0;   // baris terlempar keluar
int   nsActRow=-1;                // baris yg menampilkan bar aksi cepat
bool  nsLongFired=false;
float nsRowOff[NOTIF_HIST_MAX];   // offset piksel (+ = tertahan di bawah slotnya) -> baris naik halus saat ada yg hilang
unsigned long nsOffLastMs=0;

// Hapus 1 baris tampil: induk -> SELURUH grup, anak -> entri itu saja. Baris di bawahnya naik halus.
void nsDeleteRow(int r){
  nsBuildRows();
  if(r<0 || r>=nsRowN) return;
  int before=nsRowN;
  NsRow rw=nsRows[r];
  if(rw.head){
    // salin kunci grup dulu (histori bergeser tiap notifHistRemove), lalu hapus dari belakang
    bool kHl=notifHist[rw.hist].hasLink; Screen kSc=notifHist[rw.hist].linkScreen; char kIc=notifHist[rw.hist].icon;
    int f=nsExpFindIdx(rw.hist); if(f>=0) nsExp[f].used=false;
    for(int k=notifHistCount-1;k>=0;k--){
      const NotifEntry &e=notifHist[k];
      if(e.hasLink==kHl && (kHl ? (e.linkScreen==kSc) : (e.icon==kIc))) notifHistRemove(k);
    }
  } else {
    notifHistRemove(rw.hist);
  }
  nsBuildRows();
  int removed=before-nsRowN;
  if(removed>0){
    for(int k=r;k<nsRowN;k++) nsRowOff[k]=nsRowOff[k+removed]+(float)(removed*NS_ROW_H);
    for(int k=nsRowN;k<NOTIF_HIST_MAX;k++) nsRowOff[k]=0.0f;
    nsOffLastMs=millis();
  }
  nsActRow=-1; nsPressedRow=-1;
  if(nsScrollY>(float)nsMaxScroll()) nsScrollY=(float)nsMaxScroll();
  needRedraw=true;
}
void nsStartOut(int r,int dir,float from){
  nsOutRow=r; nsOutDir=dir; nsOutFrom=from; nsOutT0=millis();
  nsSwipeRow=r; nsSwipeH=false; nsActRow=-1;
  needRedraw=true;
}
// Panel menyesuaikan tinggi dgn isi (hapus/buka-tutup grup/notif baru). Tidak menyentuh panel yg sedang menutup / ditarik jari.
void nsRetarget(){
  if(!notifShadeOpen || panelPullActive) return;
  if(nsAnimating && nsAnimToH<=0.0f) return;
  int target=nsPanelH();
  if(nsScrollY>(float)nsMaxScroll()) nsScrollY=(float)nsMaxScroll();
  if((int)nsAnimToH==target) return;   // sudah menuju / sudah di tinggi ini (JANGAN restart animasi tiap frame)
  nsAnimFromH=nsOffset; nsAnimToH=(float)target;
  nsAnimStartMs=millis(); nsAnimating=true;
  needRedraw=true;
}
void nsClearAll(){
  notifHistCount=0; nsScrollY=0; nsExpClear();
  nsSwipeRow=-1; nsSwipeH=false; nsSwipeDx=0; nsOutRow=-1; nsActRow=-1; nsPressedRow=-1;
  for(int i=0;i<NOTIF_HIST_MAX;i++) nsRowOff[i]=0.0f;
  nsRetarget();
  needRedraw=true;
}

void openNotifShade(){
  notifShadeOpen=true;
  nsAnimFromH=nsOffset; nsAnimToH=(float)nsPanelH();
  nsAnimStartMs=millis(); nsAnimating=true;
  nsScrollY=0; nsPressedRow=-1; nsIsSwiping=false; // v91: tiap dibuka, mulai dari atas & bersih dari sisa state sentuhan sblmnya
  nsSwipeRow=-1; nsSwipeH=false; nsSwipeDx=0; nsOutRow=-1; nsActRow=-1; nsLongFired=false; // v133
  for(int i=0;i<NOTIF_HIST_MAX;i++) nsRowOff[i]=0.0f;
  needRedraw=true;
}
void closeNotifShade(){
  nsAnimFromH=nsOffset; nsAnimToH=0.0f;
  nsAnimStartMs=millis(); nsAnimating=true;
  nsActRow=-1; // v133: bar aksi tidak dibawa ke pembukaan berikutnya
  needRedraw=true;
}

// v133: animasi per-baris (terlempar, membal, naik halus) + panel mengikuti isi. Dipanggil dari notifShadeAnimTick().
void nsRowsTick(){
  unsigned long now=millis();
  float dt=(float)(now-nsOffLastMs); if(dt>60.0f) dt=60.0f; if(dt<0.0f) dt=0.0f;
  nsOffLastMs=now;
  if(nsOutRow>=0){
    if((float)(now-nsOutT0)>=NS_OUT_MS){
      int r=nsOutRow; nsOutRow=-1; nsSwipeRow=-1; nsSwipeDx=0;
      nsDeleteRow(r);
    }
    needRedraw=true;
  } else if(nsSwipeRow>=0 && !nsSwipeH){          // membal balik (berbasis waktu nyata)
    nsSwipeDx*=expf(-dt/70.0f);
    if(fabsf(nsSwipeDx)<1.0f){ nsSwipeDx=0; nsSwipeRow=-1; }
    needRedraw=true;
  }
  float k=expf(-dt/62.0f); bool any=false;
  for(int i=0;i<NOTIF_HIST_MAX;i++){
    if(nsRowOff[i]>0.0f){ nsRowOff[i]*=k; if(nsRowOff[i]<0.5f) nsRowOff[i]=0.0f; else any=true; }
  }
  if(any){ uiBoostKick(); needRedraw=true; }
  if(nsOutRow<0 && !nsSwipeH) nsRetarget();       // isi berubah -> tinggi panel menyusul
}
// Dipanggil TIAP frame dari loop() (phone.ino), pola sama dgn blok
// ccAnimating -- diletakkan sbg fungsi terpisah (bukan inline di
// loop()) krn nsOffset/nsAnimating dkk hidup di file ini.
void notifShadeAnimTick(){
  if(notifShadeOpen) nsRowsTick();
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

// ---- bar aksi cepat (tahan lama sebuah baris) ----
// id aksi: 0 = Buka, 1 = Hapus, 2 = Batal. "Buka" hanya ada bila notifikasi punya app tertaut.
int nsActList(int r, int ids[3]){
  int n=0;
  if(r>=0 && r<nsRowN && notifHist[nsRows[r].hist].hasLink) ids[n++]=0;
  ids[n++]=1; ids[n++]=2;
  return n;
}
void nsActGeom(int n,int i,int& x,int& w){
  const int gap=6;
  w=(SCR_W-20-(n-1)*gap)/n;
  x=10+i*(w+gap);
}
static void nsAgeStr(unsigned long ageMs,char* out,size_t sz){
  if(ageMs<60000UL) snprintf(out,sz,"baru saja");
  else if(ageMs<3600000UL) snprintf(out,sz,"%lu mnt lalu",ageMs/60000UL);
  else snprintf(out,sz,"%lu jam lalu",ageMs/3600000UL);
}

void drawNotifShade(LGFX_Sprite& s){
  int ph=(int)nsOffset;
  if(ph<=0) return;
  nsBuildRows();
  int rad=min(22,ph/2); if(rad<0) rad=0;
  drawGlassPanel(s, 0,0,SCR_W,ph,rad, T().surface, 200);
  nyxAaRR(s, SCR_W/2-14,ph-9,28,4,2,T().divider);

  s.setTextColor(T().text); s.setTextSize(1);
  s.setCursor(10,8); s.print("Notifikasi");

  if(nsRowN==0){
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(10,32); s.print("Tidak ada notifikasi");
  } else {
    int rowTop=NS_HEAD_H;
    int btnY=ph-32;
    int viewportBottom = rowTop + nsViewportH();
    if(viewportBottom > btnY-4) viewportBottom = btnY-4;
    s.setClipRect(0, rowTop, SCR_W, max(0,viewportBottom-rowTop));
    unsigned long nowMs=millis();
    for(int r=0;r<nsRowN;r++){
      int y=rowTop+r*NS_ROW_H-(int)nsScrollY+(int)nsRowOff[r];
      if(y+NS_ROW_H < rowTop) continue;
      if(y > viewportBottom) continue;   // (bukan break: offset naik halus bisa membuat urutan y tak monoton)
      NsRow &rw=nsRows[r];
      NotifEntry &n=notifHist[rw.hist];
      bool child=!rw.head;
      int dx=0;
      if(r==nsOutRow){
        float t=(float)(nowMs-nsOutT0)/NS_OUT_MS; if(t>1.0f) t=1.0f; float q=1.0f-t;
        dx=(int)lroundf(nsOutFrom+((float)nsOutDir*(float)SCR_W-nsOutFrom)*(1.0f-q*q*q));
      } else if(r==nsSwipeRow) dx=(int)nsSwipeDx;

      // latar merah di sisi yg tersingkap saat digeser (sinyal "akan dihapus")
      if(dx!=0){
        float f=fabsf((float)dx)/((float)SCR_W/3.0f); if(f>1.0f) f=1.0f;
        int rw2=min(abs(dx),SCR_W-16);
        int rx = dx>0 ? 8 : SCR_W-8-rw2;
        s.fillRectAlpha(rx,y+2,rw2,NS_ROW_H-4,(uint8_t)(40+110.0f*f),T().danger);
        if(abs(dx)>46){
          s.setTextColor(0xFFFF); s.setTextSize(1);
          s.setCursor(dx>0? 16 : SCR_W-16-s.textWidth("Hapus"), y+NS_ROW_H/2-4); s.print("Hapus");
        }
      }

      if(nsActRow==r){
        // ---- mode aksi cepat: isi baris diganti pil aksi ----
        s.fillRectAlpha(8,y+2,SCR_W-16,NS_ROW_H-4,40,T().accent);
        int ids[3]; int an=nsActList(r,ids);
        for(int i=0;i<an;i++){
          int ax,aw; nsActGeom(an,i,ax,aw);
          const char* lbl = ids[i]==0 ? "Buka" : (ids[i]==1 ? "Hapus" : "Batal");
          char buf[16]; if(ids[i]==1 && rw.head && rw.cnt>1){ snprintf(buf,sizeof(buf),"Hapus %d",(int)rw.cnt); lbl=buf; }
          nyxAaRR(s, ax,y+10,aw,NS_ROW_H-20,10, ids[i]==1 ? T().danger : T().surface2);
          s.setTextColor(ids[i]==1 ? 0xFFFF : T().text); s.setTextSize(1);
          s.setCursor(ax+(aw-s.textWidth(lbl))/2, y+NS_ROW_H/2-4); s.print(lbl);
        }
        s.drawFastHLine(8, y+NS_ROW_H-2, SCR_W-16, T().divider);
        continue;
      }

      int iconR = child ? 9 : 13;
      int cx = 10+iconR+(child?16:0)+dx, cy=y+NS_ROW_H/2;
      if(!child && n.hasLink){
        bool pressed=(nsPressedRow==r);
        nyxAaCircleRing(s, cx,cy,iconR+3, pressed? T().accent : T().divider);
      }
      drawAppIcon(s, n.icon, cx, cy, iconR, n.color);

      // lebar teks dibatasi chip hitungan di kanan (bila ada)
      int chipW = (rw.head && rw.cnt>1) ? 40 : 0;
      int tx0 = cx+iconR+8;
      int maxW = SCR_W-12-chipW-(tx0-dx);
      String t = n.title;
      while(t.length()>2 && s.textWidth(t.c_str())>maxW) t = t.substring(0,t.length()-3)+"..";
      s.setTextColor(child ? T().subtext : T().text); s.setTextSize(1);
      s.setCursor(tx0, y+NS_ROW_H/2-9); s.print(t.c_str());
      char ageStr[14]; nsAgeStr(nowMs-n.ts,ageStr,sizeof(ageStr));
      s.setTextColor(T().subtext);
      s.setCursor(tx0, y+NS_ROW_H/2+3); s.print(ageStr);

      // chip "N v" -- ketuk utk buka/tutup grup
      if(rw.head && rw.cnt>1){
        bool open=(nsExpFindIdx(rw.hist)>=0);
        int px=SCR_W-10-34+dx, py=cy-9;
        nyxAaRR(s, px,py,34,18,9,T().surface2);
        char nb[6]; snprintf(nb,sizeof(nb),"%d",(int)rw.cnt);
        s.setTextColor(T().text); s.setCursor(px+8,cy-4); s.print(nb);
        int ax=px+24, ay=cy;     // chevron kecil: turun = tertutup, naik = terbuka
        if(open){ s.drawLine(ax-3,ay+1,ax,ay-2,T().subtext); s.drawLine(ax,ay-2,ax+3,ay+1,T().subtext); }
        else    { s.drawLine(ax-3,ay-2,ax,ay+1,T().subtext); s.drawLine(ax,ay+1,ax+3,ay-2,T().subtext); }
      }
      s.drawFastHLine(8, y+NS_ROW_H-2, SCR_W-16, T().divider);
    }
    s.clearClipRect();
    int maxS = nsMaxScroll();
    if(maxS>0){
      int trackH = viewportBottom-rowTop;
      int totalH = nsRowN*NS_ROW_H;
      int thumbH = max(14, trackH*(trackH)/totalH);
      int thumbY = rowTop + (int)((trackH-thumbH)*(nsScrollY/(float)maxS));
      nyxAaRR(s, SCR_W-5, rowTop, 2, trackH, 1, T().divider);
      nyxAaRR(s, SCR_W-5, thumbY, 2, thumbH, 1, T().subtext);
    }
  }

  int btnY=ph-32, btnH=24;
  nyxAaRR(s, 10,btnY,SCR_W-20,btnH,10,T().surface2);
  s.setTextColor(T().danger); s.setTextSize(1);
  const char* clearLbl="Bersihkan Semua";
  int clw=s.textWidth(clearLbl);
  s.setCursor(SCR_W/2-clw/2, btnY+7); s.print(clearLbl);
}

// v91: DIPISAH jadi 2 fungsi -- notifShadeTap() (logika TAP, hanya jalan pas jari
// kekonfirmasi tap pendek) dan notifShadeInput() yg jalan TIAP frame slama shade
// kebuka (bedain tap / scroll / geser-hapus / tahan-lama).
void notifShadeTap(int x,int y){
  nsBuildRows();
  int ph=(int)nsOffset;
  const int rowTop=NS_HEAD_H;

  // v133: bar aksi sedang tampil -> tap di dalam baris itu = pilih aksi; tap di luar = batal saja
  if(nsActRow>=0){
    int r=nsActRow; nsActRow=-1; needRedraw=true;
    int ry=rowTop+r*NS_ROW_H-(int)nsScrollY;
    if(r<nsRowN && y>=ry && y<ry+NS_ROW_H){
      int ids[3]; int an=nsActList(r,ids);
      for(int i=0;i<an;i++){
        int ax,aw; nsActGeom(an,i,ax,aw);
        if(x<ax || x>ax+aw) continue;
        if(ids[i]==0){
          NotifEntry &n=notifHist[nsRows[r].hist];
          Screen target=n.linkScreen; bool hl=n.hasLink;
          closeNotifShade();
          if(hl) navPush(target);
        } else if(ids[i]==1){
          vibTap();               // konsekuensi permanen (notif hilang) -> boleh bergetar, sesuai kebijakan getaran minimal
          nsStartOut(r,1,0.0f);
        }
        return;
      }
    }
    return;
  }

  // ketuk drag-handle atau area kosong di bawah panel -> tutup, pola sama persis dgn ccTouch()
  if(y> ph-9 && y<=ph+4){ closeNotifShade(); return; }
  if(y>ph) { closeNotifShade(); return; }

  int btnY=ph-32, btnH=24;
  if(y>=btnY && y<=btnY+btnH && x>=10 && x<=SCR_W-10){
    // "Bersihkan Semua" -- cuma histori SHADE yg dibersihin. Notif yg LG TAMPIL di DI sengaja DIBIARKAN.
    nsClearAll();
    return;
  }

  if(y>=rowTop && nsRowN>0){
    int idx = (y-rowTop+(int)nsScrollY)/NS_ROW_H;
    if(idx>=0 && idx<nsRowN){
      NsRow rw=nsRows[idx];
      NotifEntry &n=notifHist[rw.hist];
      // chip hitungan di kanan -> buka/tutup grup (baris di bawahnya naik halus saat menutup)
      if(rw.head && rw.cnt>1 && x>=SCR_W-10-40){
        int before=nsRowN;
        nsExpToggleIdx(rw.hist);
        nsBuildRows();
        int removed=before-nsRowN;
        if(removed>0){
          for(int k=idx+1;k<nsRowN;k++) nsRowOff[k]=nsRowOff[k+removed]+(float)(removed*NS_ROW_H);
          for(int k=nsRowN;k<NOTIF_HIST_MAX;k++) nsRowOff[k]=0.0f;
          nsOffLastMs=millis();
        }
        if(nsScrollY>(float)nsMaxScroll()) nsScrollY=(float)nsMaxScroll();
        needRedraw=true;
        return;
      }
      bool hasLink=n.hasLink; Screen target=n.linkScreen;
      closeNotifShade();
      if(hasLink) navPush(target); // otomatis dapat transisi mulus yg sama dgn buka app dari Home/dock
      return;
    }
  }
}

void notifShadeInput(bool touched, bool newT, int tx, int ty){
  nsBuildRows();
  int ph=(int)nsOffset;
  const int rowTop=NS_HEAD_H;
  int btnY=ph-32;
  int viewportBottom = rowTop + nsViewportH();
  if(viewportBottom>btnY-4) viewportBottom=btnY-4;

  if(newT){
    nsTouchStartX=tx; nsTouchStartY=ty; nsTouchLastY=ty;
    nsIsSwiping=false; nsSwipeH=false; nsLongFired=false; nsTouchStartMs=millis();
    // baris yg sedang terlempar keluar: abaikan sentuhan baru sampai animasinya kelar
    if(nsOutRow>=0){ nsPressedRow=-1; return; }
    if(ty>=rowTop && ty<viewportBottom && nsRowN>0){
      int idx=(ty-rowTop+(int)nsScrollY)/NS_ROW_H;
      nsPressedRow=(idx>=0&&idx<nsRowN)? idx : -1;
    } else nsPressedRow=-1;
    // baris membal balik dari geseran sebelumnya: lepas, sentuhan baru yg berkuasa
    if(nsSwipeRow>=0){ nsSwipeRow=-1; nsSwipeDx=0; }
    needRedraw=true;
    return;
  }
  if(nsOutRow>=0){ return; }
  if(touched){
    int dx=tx-nsTouchStartX, dy=ty-nsTouchStartY;

    // tahan lama di satu baris (jari diam) -> bar aksi cepat
    if(!nsLongFired && !nsIsSwiping && !nsSwipeH && nsPressedRow>=0 && nsActRow<0 &&
       millis()-nsTouchStartMs>=NS_LONG_MS && abs(dx)<=8 && abs(dy)<=8){
      nsActRow=nsPressedRow; nsLongFired=true; nsPressedRow=-1;
      vibTap();               // konfirmasi gestur tahan-lama (1 denyut, sama seperti menu ketuk-lama lain)
      needRedraw=true;
    }
    if(nsLongFired) return;

    if(!nsIsSwiping && !nsSwipeH){
      if(nsPressedRow>=0 && abs(dx)>12 && abs(dx)>abs(dy)){
        nsSwipeH=true; nsSwipeRow=nsPressedRow; nsPressedRow=-1;
        nsSwipeDx=0; nsSwipeVel=0; nsSwipeLastX=tx; nsSwipeLastMs=millis(); nsSwipeArmed=false;
        nsActRow=-1;
      } else if(abs(dy)>12){
        nsIsSwiping=true; nsPressedRow=-1;
      }
    }
    if(nsSwipeH){
      nsSwipeDx=(float)dx;
      unsigned long now=millis(), dt=now-nsSwipeLastMs;
      if(dt>0) nsSwipeVel=nsSwipeVel*0.6f+((float)(tx-nsSwipeLastX)/(float)dt)*0.4f;
      nsSwipeLastX=tx; nsSwipeLastMs=now;
      bool armed=fabsf(nsSwipeDx)>(float)SCR_W/3.0f;
      if(armed && !nsSwipeArmed) vibExit();   // 1 denyut halus tepat saat melewati ambang buang
      nsSwipeArmed=armed;
      needRedraw=true;
    } else if(nsIsSwiping){
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
  if(nsSwipeH){
    nsSwipeH=false;
    unsigned long now=millis();
    if(now-nsSwipeLastMs>90) nsSwipeVel=0;
    bool far=fabsf(nsSwipeDx)>(float)SCR_W/3.0f;
    bool fast=fabsf(nsSwipeVel)>0.5f && ((nsSwipeVel<0)==(nsSwipeDx<0));
    if(far||fast){
      vibTap();               // notif hilang permanen -> konfirmasi (sama dgn kartu di App Switcher)
      nsStartOut(nsSwipeRow, nsSwipeDx<0?-1:1, nsSwipeDx);
    }                         // else: dibiarkan, nsRowsTick() yg membalikkan baris
  } else if(!nsLongFired && !nsIsSwiping && millis()-nsTouchStartMs<400){
    notifShadeTap(nsTouchStartX,nsTouchStartY);
  }
  nsPressedRow=-1; nsIsSwiping=false; nsLongFired=false;
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
  else if(row==2){ nsClearAll(); } // "Bersihkan Semua"
}


// =================================================================
// NOTIF-LOCK -- kartu notifikasi di LOCK SCREEN.
// Isi = histori yg sama dgn Notification Shade (terbaru di atas), maks 3 kartu dan hanya sebanyak yg
// muat di area [lockNotifTop,lockNotifBottom] yg disisakan drawLockScreen().
//  - kartu baru meluncur masuk dari kanan (berbasis waktu, 320 ms)
//  - geser kartu ke kiri/kanan: ikut jari; lepas jauh/cepat = terlempar keluar & dihapus dari histori,
//    lepas pendek = membal balik. Gestur ini membatalkan gestur "geser naik utk buka" yg sedang menunggu.
// =================================================================
#define LN_CARD_H   34
#define LN_GAP      4
#define LN_MAX      3
#define LN_ANIM_MS  320UL
#define LN_OUT_MS   180UL
static int   lnCand=-1, lnDragIdx=-1, lnSx=0, lnSy=0, lnLastX=0;
static bool  lnDragOn=false;
static float lnDragDx=0, lnVel=0;
static unsigned long lnLastMs=0;
static int   lnOutIdx=-1, lnOutDir=1;
static float lnOutFrom=0;
static unsigned long lnOutT0=0;

static int lnCount(){
  int avail=(lockNotifBottom-lockNotifTop+LN_GAP)/(LN_CARD_H+LN_GAP);
  int c=notifHistCount<LN_MAX?notifHistCount:LN_MAX;
  if(avail<c) c=avail;
  return c<0?0:c;
}
static int lnCardY(int i){ return lockNotifTop-(int)lockDragY+i*(LN_CARD_H+LN_GAP); }
static int lnHit(int tx,int ty){
  int n=lnCount();
  for(int i=0;i<n;i++){ int y=lnCardY(i); if(ty>=y && ty<=y+LN_CARD_H && tx>=14 && tx<=SCR_W-14) return i; }
  return -1;
}
static float lnEase(float t){ if(t<0)t=0; if(t>1)t=1; float q=1-t; return 1-q*q*q; }

void lockNotifDraw(LGFX_Sprite& s,int lift,uint16_t fg,uint16_t fg2){
  (void)lift;
  int n=lnCount(); if(n<=0) return;
  const int x0=14, w=SCR_W-28;
  unsigned long now=millis();
  for(int i=0;i<n;i++){
    NotifEntry &e=notifHist[i];
    int y=lnCardY(i), dx=0;
    unsigned long age=now-e.ts;
    if(age<LN_ANIM_MS){ float k=1.0f-(float)age/(float)LN_ANIM_MS; dx=(int)(k*k*k*(float)(w/2)); }   // masuk dari kanan
    if(i==lnOutIdx){ dx=(int)lroundf(lnOutFrom+((float)lnOutDir*(float)SCR_W-lnOutFrom)*lnEase((float)(now-lnOutT0)/(float)LN_OUT_MS)); }
    else if(i==lnDragIdx) dx=(int)lroundf(lnDragDx);
    int x=x0+dx;
    drawGlassCard(s,x,y,w,LN_CARD_H,12,T().surface,150);
    int cy=y+LN_CARD_H/2;
    drawAppIcon(s,e.icon,x+19,cy,11,e.color);
    String t=e.title; int maxCh=(w-56)/6; if((int)t.length()>maxCh) t=t.substring(0,maxCh-2)+"..";
    s.setTextSize(1);
    s.setTextColor(fg); s.setCursor(x+38,cy-9); s.print(t.c_str());
    char ageStr[16];
    if(age<60000UL) strcpy(ageStr,"baru saja");
    else if(age<3600000UL) snprintf(ageStr,sizeof(ageStr),"%lu mnt lalu",age/60000UL);
    else snprintf(ageStr,sizeof(ageStr),"%lu jam lalu",age/3600000UL);
    s.setTextColor(fg2); s.setCursor(x+38,cy+3); s.print(ageStr);
  }
}

bool lockNotifInput(bool touched,bool newT,int tx,int ty){
  if(lnOutIdx>=0) return false;                       // kartu sedang terlempar: gestur lain jalan normal
  unsigned long now=millis();
  if(newT){
    lnCand=lnHit(tx,ty); lnDragOn=false; lnSx=tx; lnSy=ty; lnVel=0; lnLastX=tx; lnLastMs=now;
    return false;                                     // gestur unlock tetap dimulai; dibatalkan bila ternyata geser mendatar
  }
  if(lnCand<0) return false;
  if(touched){
    int dx=tx-lnSx, dy=ty-lnSy;
    if(!lnDragOn && !unlockFollowActive && abs(dx)>12 && abs(dx)>abs(dy)){
      lnDragOn=true; lnDragIdx=lnCand; lockDragging=false; lockDragY=0;
    }
    if(lnDragOn){
      lnDragDx=(float)dx;
      unsigned long dt=now-lnLastMs;
      if(dt>0) lnVel=lnVel*0.6f+((float)(tx-lnLastX)/(float)dt)*0.4f;
      lnLastX=tx; lnLastMs=now;
      needRedraw=true;
      return true;
    }
    return false;
  }
  // jari lepas
  int c=lnCand; lnCand=-1;
  if(!lnDragOn) return false;
  lnDragOn=false;
  if(now-lnLastMs>90) lnVel=0;
  bool far=fabsf(lnDragDx)>(float)SCR_W/3.0f;
  bool fast=fabsf(lnVel)>0.5f && ((lnVel<0)==(lnDragDx<0));
  if(far||fast){ lnOutIdx=c; lnOutFrom=lnDragDx; lnOutDir=(lnDragDx<0)?-1:1; lnOutT0=now; }
  needRedraw=true;
  return true;
}

void lockNotifTick(){
  if(!locked || aodActive) return;
  unsigned long now=millis();
  if(lnOutIdx>=0){
    if(now-lnOutT0>=LN_OUT_MS){ notifHistRemove(lnOutIdx); lnOutIdx=-1; lnDragIdx=-1; lnDragDx=0; }
    needRedraw=true; return;
  }
  if(lnDragIdx>=0 && !lnDragOn){                      // membal balik
    lnDragDx*=0.6f;
    if(fabsf(lnDragDx)<1.0f){ lnDragDx=0; lnDragIdx=-1; }
    needRedraw=true;
  }
  if(notifHistCount>0 && now-notifHist[0].ts<LN_ANIM_MS+40UL) needRedraw=true;   // kartu baru masih meluncur
}
