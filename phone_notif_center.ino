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
struct NotifEntry { char icon; String title; uint16_t color; String badge; unsigned long ts; };
#define NOTIF_HIST_MAX 15
NotifEntry    notifHist[NOTIF_HIST_MAX];
int           notifHistCount = 0; // berapa slot terisi (maks NOTIF_HIST_MAX)

void notifHistPush(char icon, const String& title, uint16_t color, const String& badge){
  int last = min(notifHistCount, NOTIF_HIST_MAX-1);
  for(int i=last; i>0; i--) notifHist[i]=notifHist[i-1];
  notifHist[0].icon=icon; notifHist[0].title=title; notifHist[0].color=color;
  notifHist[0].badge=badge; notifHist[0].ts=millis();
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
const float   NS_ANIM_MS = 200.0f;
#define NS_ROW_H 42
#define NS_MAX_ROWS_SHOWN 6

int nsPanelH(){
  int rows = min(notifHistCount, NS_MAX_ROWS_SHOWN);
  int h = (notifHistCount==0) ? 90 : (28 + rows*NS_ROW_H + 40);
  int maxAvail = SCR_H-8; // jgn sampai nutup 1 layar penuh, sama spt ccPanelH()
  return h<maxAvail? h : maxAvail;
}

void openNotifShade(){
  notifShadeOpen=true;
  nsAnimFromH=nsOffset; nsAnimToH=(float)nsPanelH();
  nsAnimStartMs=millis(); nsAnimating=true;
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
    int shown = min(notifHistCount, NS_MAX_ROWS_SHOWN);
    for(int i=0;i<shown;i++){
      int y=rowTop+i*NS_ROW_H;
      if(y+NS_ROW_H > ph-36) { shown=i; break; } // jgn nimpa tombol Bersihkan Semua
      NotifEntry &n = notifHist[i];
      int iconR=13, cx=10+iconR, cy=y+NS_ROW_H/2;
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
    if(notifHistCount>shown){
      char more[24]; snprintf(more,sizeof(more),"+%d lainnya",notifHistCount-shown);
      s.setTextColor(T().subtext); s.setTextSize(1);
      int mw=s.textWidth(more);
      s.setCursor(SCR_W/2-mw/2, ph-52); s.print(more);
    }
  }

  int btnY=ph-32, btnH=24;
  s.fillRoundRect(10,btnY,SCR_W-20,btnH,10,T().surface2);
  s.setTextColor(T().danger); s.setTextSize(1);
  const char* clearLbl="Bersihkan Semua";
  int clw=s.textWidth(clearLbl);
  s.setCursor(SCR_W/2-clw/2, btnY+7); s.print(clearLbl);
}

void notifShadeTouch(int x,int y){
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
    notifHistCount=0;
    needRedraw=true;
    return;
  }
  // NB: ketuk salah satu baris notif belum ada aksi (buka app terkait) --
  // link screen (diLink()) dipanggil TERPISAH dari diNotify() di tiap
  // call-site, jadi gak ikut kesimpan ke histori di sini. Kalau nanti mau
  // ditambah, cara paling gampang: kasih parameter Screen opsional ke
  // notifHistPush()/NotifEntry & isi manual di tiap pemanggil yg butuh.
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
