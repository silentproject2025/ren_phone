// =====================================================================
// WAKE WORD -- "Hei Nyx" (BARU v115, permintaan user: "Bisa kita buat
// hp ini ada machine learning nya" -> "Wake word coba")
// =====================================================================
// Ide: HP dengerin terus lewat mic INMP441 (task terpisah, core 0). Begitu
// user ngucapin KATA KUNCI-nya sendiri, HP getar lalu buka AI Chat & langsung
// mulai merekam (micStartRecording) -- tinggal ngomong perintahnya.
//
// KENAPA BUKAN NEURAL NET? Model wake word custom ("Hei Nyx") butuh ribuan
// sampel suara + training di PC, dan ESP-SR/WakeNet bawaan Espressif cuma
// punya kata kunci preset ("Hi ESP", dll) -- kata kunci custom-nya berbayar.
// Jadi pakai ML ala "personal wake word": user REKAM SENDIRI 4x ucapan
// kata kuncinya di HP, HP belajar dari 4 contoh itu (template), tanpa
// training di laptop, tanpa internet, tanpa library tambahan:
//   1. Suara 16kHz -> frame 32ms (hop 16ms) -> log-mel 24 band -> DCT
//      -> 12 koefisien MFCC per frame  (fitur suara standar ASR)
//   2. VAD energi adaptif motong 1 ucapan (0.25-1.5 detik) dari hening
//   3. MFCC ucapan dinormalisasi (CMN) lalu dicocokkan ke 4 template
//      pakai DTW (Dynamic Time Warping) -- toleran beda kecepatan ngomong
//   4. Skor = rata2 2 jarak terkecil; <= ambang -> TERDETEKSI. Ambang
//      dikalibrasi otomatis dari sebaran jarak antar 4 rekaman + slider
//      "Sensitivitas" di app.
// KETERBATASAN JUJUR: ini kenal SUARA & KATA user sendiri (speaker-dependent),
// bukan kenal "siapa pun yg bilang Hei Nyx". Di ruang bising / kata mirip bisa
// salah picu atau meleset -- makanya ada app "Wake Word" buat ngetes skor
// live & nyetel sensitivitas. Nanti bisa di-upgrade ke model TFLite Micro
// tanpa ngubah alur app/UI di bawah ini (cukup ganti wwHandleUtt()).
//
// Baterai: mic + DSP jalan terus selagi aktif (opt-in, default MATI).
// Mic dipakai gantian dgn dictation AI Chat & app Mic Level -- lihat
// wwMicBusy() & wakeYieldMic(), hook-nya di micStartRecording()/micLvlEnter().
// File I/O (simpan template) SENGAJA cuma dari loop() (wakeLoopPoll), bukan
// dari task ini, krn task ini stack-nya di PSRAM (aman utk flash write cuma
// dari task stack internal).
// =====================================================================

#define WW_SR 16000
#define WW_HOP 256          // 16ms -- ukuran 1 blok baca I2S
#define WW_WIN 512          // 32ms -- jendela FFT
#define WW_NMEL 24
#define WW_NCEP 12          // c1..c12
#define WW_MAXF 96          // maks frame 1 ucapan (~1.5 detik)
#define WW_MINF 16          // min frame 1 ucapan (~0.25 detik)
#define WW_MAXW 48          // lebar maks 1 filter mel (bin)
#define WW_MAXTPL 5
#define WW_ENROLL_N 4
#define WW_PRE 5            // frame pre-roll sebelum onset
#define WW_HANG 14          // frame hening (~220ms) penutup ucapan
#define WK_FILE "/wake_tpl.bin"
#define WK_MAGIC 0x31574B57UL

// Semua tabel & buffer kerja digabung 1 blok, dialokasi di PSRAM sekali
// (wakeBegin) -- biar SRAM internal yg udah mepet gak kepotong ~16KB
// permanen walau fitur ini gak dipakai.
struct WwBuf {
  float ham[WW_WIN];
  float cosT[WW_WIN/2], sinT[WW_WIN/2];
  uint16_t rev[WW_WIN];
  int melLo[WW_NMEL], melN[WW_NMEL];
  float melW[WW_NMEL][WW_MAXW];
  float dct[WW_NCEP][WW_NMEL];
  float dc[WW_WIN];
  float fre[WW_WIN], fim[WW_WIN];
  float ring[8][WW_NCEP];
  float ringE[8];
};
static WwBuf* wwB = nullptr;

// ---- state bersama (task <-> loop/UI) ----
volatile bool wkEnabled = false;
volatile int  wkSensIdx = 2;
static const float WK_SENS_MUL[5] = {0.80f,0.90f,1.00f,1.12f,1.25f};
static const char* WK_SENS_NAME[5] = {"Ketat+","Ketat","Sedang","Longgar","Longgar+"};

static float* wkTplMem = nullptr;           // WW_MAXTPL*WW_MAXF*WW_NCEP float
static float* wkTpl[WW_MAXTPL];
static int    wkTplLen[WW_MAXTPL];
volatile int  wkTplCount = 0;
static float  wkThrBase = 0.0f;
static float* wkUtt = nullptr;              // buffer 1 ucapan (WW_MAXF*WW_NCEP)

static TaskHandle_t wkTaskHandle = NULL;
static StackType_t* wkTaskStack = nullptr;
static StaticTask_t wkTaskTCB;

volatile bool wkHolding = false;            // true = task ini lg megang micI2S
volatile unsigned long wkYieldUntil = 0;    // sampai kapan mic "dipinjem" pemakai lain
volatile float wkLevel = 0.0f;              // 0..1, buat bar level di app
volatile float wkLastScore = -1.0f;
volatile bool  wkFired = false;
volatile int   wkFireCount = 0;
volatile unsigned long wkLastFireMs = 0;
volatile bool  wkSavePending = false;
volatile int   wkEnrollStep = 0;            // 0=tdk merekam, 1..N=nunggu rekaman ke-n
volatile int   wkEnrollMsg = 0;             // 0 -, 1 ok, 2 kependekan, 3 gak mirip, 4 panjang beda jauh
volatile unsigned long wkEnrollMsgMs = 0;
static int    wkEnrollGot = 0;
static float  wkEnrollRef = 0.0f;
static unsigned long wkCooldownUntil = 0;
static unsigned long wkLastPollDrawMs = 0;

// ---------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------
static float wwHz2Mel(float f){ return 2595.0f*log10f(1.0f+f/700.0f); }
static float wwMel2Hz(float m){ return 700.0f*(powf(10.0f,m/2595.0f)-1.0f); }

static void wwInitTables(){
  WwBuf* b = wwB;
  for(int n=0;n<WW_WIN;n++) b->ham[n]=0.54f-0.46f*cosf(2.0f*(float)PI*n/(WW_WIN-1));
  for(int k=0;k<WW_WIN/2;k++){
    float a=2.0f*(float)PI*k/WW_WIN;
    b->cosT[k]=cosf(a); b->sinT[k]=-sinf(a);
  }
  int bits=0; while((1<<bits)<WW_WIN) bits++;
  for(int i=0;i<WW_WIN;i++){
    int r=0; for(int q=0;q<bits;q++) if(i&(1<<q)) r|=1<<(bits-1-q);
    b->rev[i]=(uint16_t)r;
  }
  float mlo=wwHz2Mel(100.0f), mhi=wwHz2Mel(7600.0f);
  float pts[WW_NMEL+2];
  for(int i=0;i<WW_NMEL+2;i++){
    float hz=wwMel2Hz(mlo+(mhi-mlo)*i/(WW_NMEL+1));
    pts[i]=hz*WW_WIN/(float)WW_SR;
  }
  for(int m=0;m<WW_NMEL;m++){
    float l=pts[m], c=pts[m+1], r=pts[m+2];
    int lo=(int)ceilf(l), hi=(int)floorf(r);
    if(lo<1) lo=1;
    if(hi>WW_WIN/2-1) hi=WW_WIN/2-1;
    int n=hi-lo+1;
    if(n<1){ n=1; hi=lo; }
    if(n>WW_MAXW){ n=WW_MAXW; hi=lo+n-1; }
    b->melLo[m]=lo; b->melN[m]=n;
    for(int j=0;j<n;j++){
      float bin=(float)(lo+j), w;
      if(bin<=c) w=(c>l)?(bin-l)/(c-l):0.0f; else w=(r>c)?(r-bin)/(r-c):0.0f;
      if(w<0.0f) w=0.0f;
      b->melW[m][j]=w;
    }
  }
  float sc=sqrtf(2.0f/WW_NMEL);
  for(int k=0;k<WW_NCEP;k++)
    for(int n=0;n<WW_NMEL;n++)
      b->dct[k][n]=sc*cosf((float)PI*(k+1)*(n+0.5f)/WW_NMEL);
}

static void wwFFT(float* re,float* im){
  WwBuf* b = wwB;
  for(int i=0;i<WW_WIN;i++){
    int j=b->rev[i];
    if(j>i){ float t=re[i]; re[i]=re[j]; re[j]=t; t=im[i]; im[i]=im[j]; im[j]=t; }
  }
  for(int len=2; len<=WW_WIN; len<<=1){
    int half=len>>1, step=WW_WIN/len;
    for(int i=0;i<WW_WIN;i+=len){
      for(int k=0;k<half;k++){
        float wr=b->cosT[k*step], wi=b->sinT[k*step];
        int a=i+k, q=a+half;
        float tr=re[q]*wr-im[q]*wi;
        float ti=re[q]*wi+im[q]*wr;
        re[q]=re[a]-tr; im[q]=im[a]-ti;
        re[a]+=tr;      im[a]+=ti;
      }
    }
  }
}

// b->dc = 512 sampel DC-blocked -> cep[12] MFCC; return log-energi frame (dB relatif).
static float wwFrame(float* cep){
  WwBuf* b = wwB;
  const float* dc = b->dc;
  double es=0;
  for(int n=0;n<WW_WIN;n++) es+=(double)dc[n]*dc[n];
  float logE=10.0f*log10f((float)(es/WW_WIN)+1.0f);
  for(int n=0;n<WW_WIN;n++){
    float x=(n>0)?dc[n]-0.97f*dc[n-1]:dc[n]; // pre-emphasis
    b->fre[n]=x*b->ham[n]; b->fim[n]=0.0f;
  }
  wwFFT(b->fre,b->fim);
  float lm[WW_NMEL];
  for(int m=0;m<WW_NMEL;m++){
    float e=0; const float* w=b->melW[m]; int lo=b->melLo[m], n=b->melN[m];
    for(int j=0;j<n;j++){ int k=lo+j; e+=w[j]*(b->fre[k]*b->fre[k]+b->fim[k]*b->fim[k]); }
    lm[m]=logf(e+1.0f);
  }
  for(int k=0;k<WW_NCEP;k++){
    float s=0; for(int n=0;n<WW_NMEL;n++) s+=b->dct[k][n]*lm[n];
    cep[k]=s;
  }
  return logE;
}

static void wwCmn(float* f,int n){
  if(n<=0) return;
  for(int k=0;k<WW_NCEP;k++){
    float s=0; for(int i=0;i<n;i++) s+=f[i*WW_NCEP+k];
    s/=(float)n; for(int i=0;i<n;i++) f[i*WW_NCEP+k]-=s;
  }
}

// DTW dgn pita Sakoe-Chiba; return jarak rata2 per langkah, 1e9 kalau panjang timpang.
static float wwDtw(const float* a,int n,const float* b,int m){
  if(n<=0||m<=0) return 1e9f;
  float ratio=(float)n/(float)m;
  if(ratio<0.5f||ratio>2.0f) return 1e9f;
  int mx=(n>m)?n:m;
  int band=(int)(0.35f*mx); if(band<10) band=10;
  const float INF=1e30f;
  float rowA[WW_MAXF+2], rowB[WW_MAXF+2];
  float* prev=rowA; float* cur=rowB;
  for(int j=0;j<=m;j++) prev[j]=INF;
  prev[0]=0.0f;
  for(int i=1;i<=n;i++){
    for(int j=0;j<=m;j++) cur[j]=INF;
    int center=(int)((float)i*m/n);
    int j0=center-band; if(j0<1) j0=1;
    int j1=center+band; if(j1>m) j1=m;
    const float* av=a+(i-1)*WW_NCEP;
    for(int j=j0;j<=j1;j++){
      const float* bv=b+(j-1)*WW_NCEP;
      float d2=0; for(int k=0;k<WW_NCEP;k++){ float t=av[k]-bv[k]; d2+=t*t; }
      float best=prev[j-1];
      if(prev[j]<best) best=prev[j];
      if(cur[j-1]<best) best=cur[j-1];
      cur[j]=best+sqrtf(d2);
    }
    float* t=prev; prev=cur; cur=t;
  }
  float res=prev[m];
  if(res>=INF*0.5f) return 1e9f;
  return res/(float)(n+m);
}

// ---------------------------------------------------------------------
// Penanganan 1 ucapan (dipanggil dari task, wkUtt sudah berisi n frame)
// ---------------------------------------------------------------------
static float wwThreshold(){
  int si=wkSensIdx; if(si<0||si>4) si=2;
  return wkThrBase*WK_SENS_MUL[si];
}

static void wwEnrollFinish(){
  // statistik jarak antar semua pasangan template -> ambang dasar
  float sum=0,sum2=0; int c=0;
  for(int i=0;i<WW_ENROLL_N;i++) for(int j=i+1;j<WW_ENROLL_N;j++){
    float d=wwDtw(wkTpl[i],wkTplLen[i],wkTpl[j],wkTplLen[j]);
    if(d>1e8f) d=0; // gak mungkin (sudah dicek per-sampel), jaga2
    sum+=d; sum2+=d*d; c++;
  }
  float mean=(c>0)?sum/c:1.0f;
  float var=(c>0)?sum2/c-mean*mean:0.0f; if(var<0) var=0;
  float sd=sqrtf(var);
  float thr=mean+1.2f*sd;
  if(thr<1.15f*mean) thr=1.15f*mean;
  if(thr<0.5f) thr=0.5f;
  wkThrBase=thr;
  wkTplCount=WW_ENROLL_N;
  wkEnrollStep=0;
  wkEnabled=true;
  wkEnrollMsg=5; wkEnrollMsgMs=millis();
  wkCooldownUntil=millis()+2500;
  wkSavePending=true;
}

static void wwHandleUtt(int n){
  wwCmn(wkUtt,n);
  if(wkEnrollStep>0){
    int idx=wkEnrollGot;
    if(n<WW_MINF+2){ wkEnrollMsg=2; wkEnrollMsgMs=millis(); return; }
    if(idx>=1){
      float sum=0; int bad=0;
      for(int j=0;j<idx;j++){
        float d=wwDtw(wkUtt,n,wkTpl[j],wkTplLen[j]);
        if(d>1e8f) bad++; else sum+=d;
      }
      if(bad>0){ wkEnrollMsg=4; wkEnrollMsgMs=millis(); return; }
      float avg=sum/idx;
      if(idx>=2 && wkEnrollRef>0.0f && avg>2.5f*wkEnrollRef){ wkEnrollMsg=3; wkEnrollMsgMs=millis(); return; }
      wkEnrollRef = (idx==1)? avg : (wkEnrollRef*(idx-1)+avg)/idx;
    }
    memcpy(wkTpl[idx],wkUtt,(size_t)n*WW_NCEP*sizeof(float));
    wkTplLen[idx]=n;
    wkEnrollGot=idx+1;
    if(wkEnrollGot>=WW_ENROLL_N){ wwEnrollFinish(); }
    else { wkEnrollStep=wkEnrollGot+1; wkEnrollMsg=1; wkEnrollMsgMs=millis(); }
    return;
  }
  int k=wkTplCount;
  if(k<=0) return;
  float d[WW_MAXTPL];
  for(int i=0;i<k;i++) d[i]=wwDtw(wkUtt,n,wkTpl[i],wkTplLen[i]);
  for(int i=0;i<k;i++) for(int j=i+1;j<k;j++) if(d[j]<d[i]){ float t=d[i]; d[i]=d[j]; d[j]=t; }
  float score=(k>=3)?(d[0]+d[1])*0.5f:d[0];
  if(score>1e8f) return; // panjang timpang total: bukan kata kunci
  wkLastScore=score;
  if(score<=wwThreshold() && millis()>wkCooldownUntil){
    wkCooldownUntil=millis()+2500;
    wkLastFireMs=millis();
    wkFireCount=wkFireCount+1;
    wkFired=true;
  }
}

// ---------------------------------------------------------------------
// Task pendengar
// ---------------------------------------------------------------------
static bool wwMicBusy(){
  if(micRecording||micTranscribing||micTaskHandle!=NULL) return true; // dictation AI Chat
  if(micLvlAcquired) return true;                                      // app Mic Level
  if(millis()<wkYieldUntil) return true;                               // dipinjem sesaat
  Screen sc=curScreen();
  return (sc==SCR_MICLEVEL||sc==SCR_NES||sc==SCR_DOOM);               // NES/DOOM: hemat CPU
}

static void wakeTaskFunc(void* param){
  (void)param;
  int32_t raw[WW_HOP*2];
  enum { S_IDLE, S_SPEECH, S_LOCK };
  int st=S_IDLE;
  float nf=40.0f; int nfInit=0; float nfStart=40.0f;
  int cntOn=0, hang=0, lockQuiet=0, uttLen=0, ringPos=0, ringCnt=0;
  float maxE=0.0f, dcx=0.0f, dcy=0.0f;

  for(;;){
    bool want = (wkEnrollStep>0) || (wkEnabled && wkTplCount>0);
    if(!want || wwMicBusy()){
      if(wkHolding){ micReleaseI2S(); wkHolding=false; }
      wkLevel=0.0f;
      st=S_IDLE; cntOn=0; ringCnt=0; nfInit=0;
      vTaskDelay(pdMS_TO_TICKS(60));
      continue;
    }
    if(!wkHolding){
      micEnsureI2S(); wkHolding=true;
      st=S_IDLE; cntOn=0; ringCnt=0; nfInit=0; dcx=0; dcy=0;
      memset(wwB->dc,0,sizeof(wwB->dc));
    }
    size_t got=0;
    while(got<sizeof(raw)){
      size_t r=micI2S.readBytes((char*)raw+got,sizeof(raw)-got);
      if(r==0) break;
      got+=r;
    }
    if(got<sizeof(raw)){ vTaskDelay(pdMS_TO_TICKS(2)); continue; }

    memmove(wwB->dc,wwB->dc+WW_HOP,WW_HOP*sizeof(float));
    for(int i=0;i<WW_HOP;i++){
      int16_t s16=(int16_t)(raw[i*2]>>MIC_SAMPLE_SHIFT);
      float x=(float)s16; float y=x-dcx+0.995f*dcy; dcx=x; dcy=y;
      wwB->dc[WW_HOP+i]=y;
    }
    float cep[WW_NCEP];
    float e=wwFrame(cep);
    wkLevel=constrain((e-25.0f)/35.0f,0.0f,1.0f); // ~25dB (sunyi) .. ~60dB (ngomong dekat)

    memcpy(wwB->ring[ringPos],cep,sizeof(cep)); wwB->ringE[ringPos]=e;
    ringPos=(ringPos+1)&7; if(ringCnt<8) ringCnt++;

    if(nfInit<25){ // kalibrasi lantai noise di ~0.4 detik pertama
      nf=(nfInit==0)?e:nf*0.8f+e*0.2f; nfInit++;
      if(nf>60.0f) nf=60.0f;
      continue;
    }

    if(st==S_IDLE){
      // lantai absolut 30dB: INMP441 dgn MIC_SAMPLE_SHIFT=16 amplitudonya kecil
      // (ngomong sejengkal ~35-50dB), jadi patokan utama tetap RELATIF thd noise (nf+10dB).
      if(e>nf+10.0f && e>30.0f) cntOn++;
      else {
        cntOn=0;
        if(e<nf) nf=nf*0.9f+e*0.1f; else nf=nf*0.995f+e*0.005f;
        if(nf>60.0f) nf=60.0f;
        if(nf<20.0f) nf=20.0f;
      }
      if(cntOn>=3){
        int take=cntOn+WW_PRE; if(take>ringCnt) take=ringCnt; if(take>8) take=8;
        uttLen=0; maxE=0.0f;
        for(int q=take;q>=1;q--){
          int idx=(ringPos-q)&7;
          memcpy(wkUtt+uttLen*WW_NCEP,wwB->ring[idx],sizeof(cep));
          if(wwB->ringE[idx]>maxE) maxE=wwB->ringE[idx];
          uttLen++;
        }
        nfStart=nf; hang=0; st=S_SPEECH;
      }
    } else if(st==S_SPEECH){
      if(uttLen<WW_MAXF){
        memcpy(wkUtt+uttLen*WW_NCEP,cep,sizeof(cep)); uttLen++;
        if(e>maxE) maxE=e;
        if(e<nfStart+6.0f) hang++; else hang=0;
        if(hang>=WW_HANG){
          int keep=uttLen-hang+2; if(keep<1) keep=1;
          if(keep>=WW_MINF && maxE>nfStart+12.0f) wwHandleUtt(keep);
          st=S_IDLE; cntOn=0;
        }
      } else { st=S_LOCK; lockQuiet=0; } // kepanjangan = kalimat, bukan kata kunci
    } else { // S_LOCK: tunggu hening dulu
      if(e<nf+6.0f) lockQuiet++; else lockQuiet=0;
      if(lockQuiet>=12){ st=S_IDLE; cntOn=0; }
    }
  }
}

// ---------------------------------------------------------------------
// Penyimpanan
// ---------------------------------------------------------------------
static fs::FS* wwFs(){
  if(ffatReady) return &FFat;
  if(sdReady) return &SD_MMC;
  return nullptr;
}

static void wakePrefsSave(){
  Preferences p; p.begin("wake",false);
  p.putBool("en",wkEnabled); p.putInt("sens",wkSensIdx);
  p.end();
}
static void wakePrefsLoad(){
  Preferences p; p.begin("wake",true);
  wkEnabled=p.getBool("en",false); wkSensIdx=p.getInt("sens",2);
  p.end();
  if(wkSensIdx<0||wkSensIdx>4) wkSensIdx=2;
}

static void wakeSaveTemplates(){
  fs::FS* fsp=wwFs();
  if(!fsp||wkTplCount<=0) return;
  File f=fsp->open(WK_FILE,FILE_WRITE);
  if(!f) return;
  uint32_t magic=WK_MAGIC; uint8_t cnt=(uint8_t)wkTplCount, nc=WW_NCEP; uint16_t pad=0;
  float thr=wkThrBase;
  f.write((const uint8_t*)&magic,4); f.write(&cnt,1); f.write(&nc,1);
  f.write((const uint8_t*)&pad,2); f.write((const uint8_t*)&thr,4);
  for(int i=0;i<cnt;i++){
    uint16_t L=(uint16_t)wkTplLen[i];
    f.write((const uint8_t*)&L,2);
    f.write((const uint8_t*)wkTpl[i],(size_t)L*WW_NCEP*sizeof(float));
  }
  f.close();
}

static void wakeLoadTemplates(){
  wkTplCount=0;
  fs::FS* fsp=wwFs();
  if(!fsp||!wkTplMem||!fsp->exists(WK_FILE)) return;
  File f=fsp->open(WK_FILE,FILE_READ);
  if(!f) return;
  uint32_t magic=0; uint8_t cnt=0, nc=0; uint16_t pad=0; float thr=0;
  bool ok = f.read((uint8_t*)&magic,4)==4 && f.read(&cnt,1)==1 && f.read(&nc,1)==1
         && f.read((uint8_t*)&pad,2)==2 && f.read((uint8_t*)&thr,4)==4;
  if(!ok || magic!=WK_MAGIC || nc!=WW_NCEP || cnt==0 || cnt>WW_MAXTPL || !(thr>0.0f)){ f.close(); return; }
  int loaded=0;
  for(int i=0;i<cnt;i++){
    uint16_t L=0;
    if(f.read((uint8_t*)&L,2)!=2 || L<WW_MINF || L>WW_MAXF) break;
    size_t bytes=(size_t)L*WW_NCEP*sizeof(float);
    if(f.read((uint8_t*)wkTpl[i],bytes)!=bytes) break;
    wkTplLen[i]=L; loaded++;
  }
  f.close();
  if(loaded==cnt){ wkThrBase=thr; wkTplCount=loaded; }
}

// ---------------------------------------------------------------------
// API ke luar (dipanggil dari phone.ino & UI)
// ---------------------------------------------------------------------
static void wakeEnsureTask(){
  if(wkTaskHandle||!wwB||!wkTplMem||!wkUtt) return;
  BaseType_t res=createTaskPsramStack(wakeTaskFunc,"wakeTask",12288,NULL,&wkTaskStack,&wkTaskTCB,&wkTaskHandle,0);
  if(res!=pdPASS){ wkTaskHandle=NULL; showToast("Gagal mulai wake word"); vibError(); }
}

void wakeBegin(){
  size_t tplBytes=(size_t)WW_MAXTPL*WW_MAXF*WW_NCEP*sizeof(float);
  uint32_t caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  wwB=(WwBuf*)heap_caps_malloc(sizeof(WwBuf),caps);
  wkTplMem=(float*)heap_caps_malloc(tplBytes,caps);
  wkUtt=(float*)heap_caps_malloc((size_t)WW_MAXF*WW_NCEP*sizeof(float),caps);
  if(!wwB||!wkTplMem||!wkUtt){
    if(wwB){ heap_caps_free(wwB); wwB=nullptr; }
    if(wkTplMem){ heap_caps_free(wkTplMem); wkTplMem=nullptr; }
    if(wkUtt){ heap_caps_free(wkUtt); wkUtt=nullptr; }
    Serial.println("[Wake] PSRAM tidak cukup, wake word dimatikan");
    return;
  }
  memset(wwB,0,sizeof(WwBuf));
  for(int i=0;i<WW_MAXTPL;i++){ wkTpl[i]=wkTplMem+(size_t)i*WW_MAXF*WW_NCEP; wkTplLen[i]=0; }
  wwInitTables();
  wakePrefsLoad();
  wakeLoadTemplates();
  if(wkTplCount==0) wkEnabled=false;
  if(wkEnabled) wakeEnsureTask();
}

// Dipanggil pemakai mic lain SEBELUM mereka megang micI2S.
void wakeYieldMic(uint32_t holdMs){
  wkYieldUntil=millis()+holdMs;
  unsigned long t0=millis();
  while(wkHolding && millis()-t0<450) delay(4);
}

void wakeSetEnabled(bool on){
  if(on && wkTplCount<=0){ showToast("Rekam kata kunci dulu"); vibWarn(); return; }
  if(on && !wkTplMem){ showToast("RAM tidak cukup"); vibError(); return; }
  wkEnabled=on;
  wakePrefsSave();
  if(on) wakeEnsureTask();
  showToast(on?"Wake word aktif":"Wake word mati");
}

void wakeStartEnroll(){
  if(!wwB||!wkTplMem){ showToast("RAM tidak cukup"); vibError(); return; }
  wkEnrollGot=0; wkEnrollRef=0.0f; wkEnrollMsg=0;
  wkTplCount=0;
  wkEnrollStep=1;
  wakeEnsureTask();
}

void wakeCancelEnroll(){
  if(wkEnrollStep>0){
    wkEnrollStep=0;
    wakeLoadTemplates();
    if(wkTplCount==0) wkEnabled=false;
  }
}

static void wakeOnFired(){
  vibSuccess();
  if(curScreen()==SCR_WAKE){ return; } // mode uji: cukup kedip di layar app
  if(locked){ showToast("Terdeteksi! Buka kunci dulu ya"); return; }
  if(appSwitcherOpen||controlCenterOpen||notifShadeOpen||diMenuOpen){ showToast("Terdeteksi!"); return; }
  homeAodWake();
  if(curScreen()!=SCR_AICHAT) navPush(SCR_AICHAT);
  micStartRecording();
}

// Dipanggil TIAP loop() -- murah kalau gak ada kerjaan.
void wakeLoopPoll(){
  if(wkSavePending){
    wkSavePending=false;
    wakeSaveTemplates(); wakePrefsSave();
    if(!wwFs()) showToast("Tanpa SD/FFat, kata kunci hilang pas restart");
    else showToast("Kata kunci tersimpan");
    vibSuccess();
  }
  if(wkFired){ wkFired=false; wakeOnFired(); }
  if(curScreen()==SCR_WAKE && millis()-wkLastPollDrawMs>66){
    wkLastPollDrawMs=millis(); needRedraw=true;
  }
}

// ---------------------------------------------------------------------
// APP: WAKE WORD (UI)
// ---------------------------------------------------------------------
void wakeEnter(){ }
void wakeExit(){ wakeCancelEnroll(); }

void drawWake(LGFX_Sprite& s){
  s.fillSprite(T().bg); drawStatusBar(s);
  s.setTextColor(T().accent); s.setTextSize(1); s.setCursor(8,26); s.print("Wake Word");

  bool enrolling = wkEnrollStep>0;
  const char* st; uint16_t sc;
  if(enrolling){ st="Merekam kata kunci"; sc=T().accent2; }
  else if(wkTplCount<=0){ st="Belum ada kata kunci"; sc=T().subtext; }
  else if(!wkEnabled){ st="Mati"; sc=T().subtext; }
  else if(!wkHolding){ st="Dijeda"; sc=T().accent; }
  else { st="Mendengarkan"; sc=T().good; }
  { int tw=s.textWidth(st);
    s.setTextColor(sc); s.setCursor(SCR_W-8-tw,26); s.print(st);
    s.fillCircle(SCR_W-8-tw-8,30,3,sc); }

  int cardX=14, cardY=42, cardW=SCR_W-28, cardH=62;
  s.fillRoundRect(cardX,cardY,cardW,cardH,8,T().surface);
  int barX=cardX+8, barW=cardW-16;

  // ---- bar level suara ----
  s.setTextColor(T().subtext); s.setCursor(barX,cardY+6); s.print("Suara");
  s.fillRoundRect(barX,cardY+17,barW,8,4,T().surface2);
  int lw=(int)(barW*wkLevel); if(lw<0) lw=0; if(lw>barW) lw=barW;
  if(lw>3) s.fillRoundRect(barX,cardY+17,lw,8,4,enrolling?T().accent2:T().accent);

  // ---- skor & ambang ----
  bool fired = (millis()-wkLastFireMs)<1200 && wkLastFireMs>0;
  float thr=wwThreshold();
  float score=wkLastScore;
  char sb[40];
  if(fired){ strcpy(sb,"TERDETEKSI!"); s.setTextColor(T().good); }
  else if(score>=0.0f && wkTplCount>0){ snprintf(sb,sizeof(sb),"Skor %.1f  /  batas %.1f",score,thr); s.setTextColor(T().subtext); }
  else { strcpy(sb,"Skor: -"); s.setTextColor(T().subtext); }
  s.setCursor(barX,cardY+32); s.print(sb);
  s.fillRoundRect(barX,cardY+44,barW,8,4,T().surface2);
  if(score>=0.0f && wkTplCount>0 && thr>0.0f){
    float r=score/(2.0f*thr); if(r>1.0f) r=1.0f;
    int sw=(int)(barW*r);
    if(sw>3) s.fillRoundRect(barX,cardY+44,sw,8,4,(score<=thr)?T().good:T().danger);
    s.fillRect(barX+barW/2-1,cardY+42,2,12,T().text); // penanda ambang
  }
  if(fired) s.drawRoundRect(cardX,cardY,cardW,cardH,8,T().good);

  if(enrolling){
    int step=wkEnrollStep; if(step>WW_ENROLL_N) step=WW_ENROLL_N;
    s.setTextColor(T().text); s.setTextSize(2);
    const char* pr="Ucapkan kata kunci";
    int pw=s.textWidth(pr); s.setCursor(SCR_W/2-pw/2,118); s.print(pr);
    s.setTextSize(1);
    char eb[24]; snprintf(eb,sizeof(eb),"Rekaman %d dari %d",step,WW_ENROLL_N);
    s.setTextColor(T().accent2); int ew=s.textWidth(eb); s.setCursor(SCR_W/2-ew/2,142); s.print(eb);
    for(int i=0;i<WW_ENROLL_N;i++){
      int cx=SCR_W/2-((WW_ENROLL_N-1)*24)/2+i*24;
      if(i<wkEnrollGot) s.fillCircle(cx,162,6,T().good);
      else s.drawCircle(cx,162,6,T().divider);
    }
    const char* msg=""; uint16_t mc=T().subtext;
    if(millis()-wkEnrollMsgMs<2500){
      if(wkEnrollMsg==1){ msg="Bagus! Ulangi sekali lagi"; mc=T().good; }
      else if(wkEnrollMsg==2){ msg="Kependekan, ucapkan lebih jelas"; mc=T().danger; }
      else if(wkEnrollMsg==3){ msg="Kurang mirip, ulangi ya"; mc=T().danger; }
      else if(wkEnrollMsg==4){ msg="Panjangnya beda jauh, ulangi"; mc=T().danger; }
    }
    if(msg[0]){ s.setTextColor(mc); int mw=s.textWidth(msg); s.setCursor(SCR_W/2-mw/2,178); s.print(msg); }
    s.fillRoundRect(110,190,100,22,8,T().surface2);
    s.setTextColor(T().subtext); { int bw=s.textWidth("Batal"); s.setCursor(160-bw/2,197); s.print("Batal"); }
  } else {
    char ib[48]; snprintf(ib,sizeof(ib),"Terdeteksi %dx  |  %d sampel tersimpan",(int)wkFireCount,(int)wkTplCount);
    s.setTextColor(T().subtext); s.setCursor(14,110); s.print(ib);

    s.fillRoundRect(14,126,140,30,8, wkEnabled?T().danger:T().accent);
    s.setTextColor(T().bg);
    { const char* l=wkEnabled?"Matikan":"Aktifkan"; int w=s.textWidth(l); s.setCursor(14+70-w/2,137); s.print(l); }
    s.fillRoundRect(160,126,146,30,8,T().surface2);
    s.setTextColor(T().accent);
    { const char* l=(wkTplCount>0)?"Rekam ulang":"Rekam kata kunci"; int w=s.textWidth(l); s.setCursor(160+73-w/2,137); s.print(l); }

    s.setTextColor(T().subtext); s.setCursor(14,172); s.print("Sensitivitas");
    s.fillRoundRect(150,164,26,24,7,T().surface2);
    s.setTextColor(T().accent); { int w=s.textWidth("<"); s.setCursor(163-w/2,172); s.print("<"); }
    s.fillRoundRect(180,164,90,24,7,T().surface);
    int si=wkSensIdx; if(si<0||si>4) si=2;
    s.setTextColor(T().text); { int w=s.textWidth(WK_SENS_NAME[si]); s.setCursor(225-w/2,172); s.print(WK_SENS_NAME[si]); }
    s.fillRoundRect(274,164,26,24,7,T().surface2);
    s.setTextColor(T().accent); { int w=s.textWidth(">"); s.setCursor(287-w/2,172); s.print(">"); }

    s.setTextColor(T().subtext);
    s.setCursor(14,194); s.print("Ucapkan kata kunci-mu, tunggu getar: HP buka");
    s.setCursor(14,204); s.print("AI Chat & langsung merekam perintahmu.");
  }

  drawBack(s); drawToast(s);
}

void wakeTouch(int x,int y,bool held,bool isNew){
  (void)held;
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  if(wkEnrollStep>0){
    if(x>=110&&x<=210&&y>=190&&y<=212){ wakeCancelEnroll(); showToast("Dibatalkan"); needRedraw=true; }
    return;
  }
  if(y>=126&&y<=156){
    if(x>=14&&x<=154){ wakeSetEnabled(!wkEnabled); needRedraw=true; return; }
    if(x>=160&&x<=306){ wakeStartEnroll(); needRedraw=true; return; }
  }
  if(y>=164&&y<=188){
    if(x>=150&&x<=176){ wkSensIdx=(wkSensIdx+4)%5; wakePrefsSave(); needRedraw=true; return; }
    if(x>=274&&x<=300){ wkSensIdx=(wkSensIdx+1)%5; wakePrefsSave(); needRedraw=true; return; }
  }
}
