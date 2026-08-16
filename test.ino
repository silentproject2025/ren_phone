// =================================================================
// ren_phone v4 — "OS" rewrite (FIXED VERSION)
// ESP32-S3, ILI9341, XPT2046 touch, SD via SDIO
//
// PERBAIKAN BUG:
//  1. Kalkulator: Tombol back ditambahkan, grid kalkulator diperbaiki
//     posisinya agar tidak menutupi Back button, dan needRedraw diaktifkan.
//  2. Settings: Tombol "Kalibrasi Ulang" dipindahkan ke sebelah kanan 
//     (sebelah tombol "Sambungkan") agar tidak bertabrakan dengan tombol Back di kiri bawah.
//  3. Canvas: Layout toolbar bawah ditata ulang (Palette di baris atas toolbar, 
//     Back/Brush/Clear di baris bawah toolbar). Tombol Back diprioritaskan 
//     sehingga tidak sengaja memilih tinta putih.
//  4. Notepad: Keyboard virtual sekarang memicu needRedraw sehingga langsung
//     muncul saat layar notepad diketuk, dan penanganan tombol Back diperbaiki.
// =================================================================
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"

// =============================================
// TYPE DEFINITIONS
// =============================================
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR,
              SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS };
enum Orientation { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1 };

struct Theme {
  const char* name;
  uint16_t bg, surface, surface2, accent, accent2,
           text, subtext, divider, good, danger;
};

Theme themes[] = {
  { "Dark",   0x1084,0x2104,0x31A6,0xFD40,0x04FF,0xFFFF,0x8C51,0x2965,0x07E0,0xF800 },
  { "AMOLED", 0x0000,0x1082,0x2104,0xFD40,0x04FF,0xFFFF,0x8C51,0x2104,0x07E0,0xF800 },
  { "Light",  0xEF5D,0xFFFF,0xDEFB,0xFD00,0x001F,0x0000,0x6B4D,0xC618,0x03E0,0xD000 },
  { "Pastel", 0xFF1F,0xFFFF,0xFF9F,0xFB16,0x5D9F,0x39C7,0x9C92,0xFEB7,0x2FE7,0xE8B4 },
};
#define THEME_COUNT 4
int themeIdx = 0;
Theme& T() { return themes[themeIdx]; }

void saveTheme(){ Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }
void loadTheme(){
  Preferences p; p.begin("ui",true);
  themeIdx = p.getInt("theme",0); p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;
}

enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };

// =============================================
// LGFX CONFIG
// =============================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;
public:
  LGFX(void) {
    { auto cfg = _bus_instance.config();
      cfg.spi_host=SPI2_HOST; cfg.spi_mode=0;
      cfg.freq_write=40000000; cfg.freq_read=16000000;
      cfg.spi_3wire=false; cfg.use_lock=true;
      cfg.dma_channel=SPI_DMA_CH_AUTO;
      cfg.pin_sclk=12; cfg.pin_mosi=11;
      cfg.pin_miso=13; cfg.pin_dc=2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance); }
    { auto cfg = _panel_instance.config();
      cfg.pin_cs=10; cfg.pin_rst=14; cfg.pin_busy=-1;
      cfg.memory_width=240; cfg.memory_height=320;
      cfg.panel_width=240;  cfg.panel_height=320;
      cfg.offset_x=0; cfg.offset_y=0; cfg.offset_rotation=0;
      cfg.dummy_read_pixel=8; cfg.dummy_read_bits=1;
      cfg.readable=true; cfg.invert=false;
      cfg.rgb_order=false; cfg.dlen_16bit=false;
      cfg.bus_shared=false;
      _panel_instance.config(cfg); }
    { auto cfg = _light_instance.config();
      cfg.pin_bl=21; cfg.invert=false;
      cfg.freq=44100; cfg.pwm_channel=7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance); }
    { auto cfg = _touch_instance.config();
      cfg.pin_int=-1; cfg.bus_shared=false;
      cfg.offset_rotation=0; cfg.spi_host=SPI3_HOST;
      cfg.freq=2000000;
      cfg.pin_sclk=6; cfg.pin_mosi=5;
      cfg.pin_miso=4; cfg.pin_cs=9;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance); }
    setPanel(&_panel_instance);
  }
};
LGFX display;
LGFX_Sprite canvas(&display);      // frame buffer utama (full screen)
LGFX_Sprite canvasApp(&display);   // buffer khusus app Canvas (drawing)

// =============================================
// ORIENTASI
// =============================================
Orientation currentOrient = ORIENT_LANDSCAPE;
int SCR_W = 320, SCR_H = 240;
#define STATUS_H 22   // tinggi status bar

void saveOrientPref() {
  Preferences p; p.begin("ui", false);
  p.putInt("orient", (int)currentOrient);
  p.end();
}
Orientation loadOrientPref() {
  Preferences p; p.begin("ui", true);
  int v = p.getInt("orient", (int)ORIENT_LANDSCAPE);
  p.end();
  return (v == (int)ORIENT_PORTRAIT) ? ORIENT_PORTRAIT : ORIENT_LANDSCAPE;
}

void loadCanvas();
void saveCanvas();
int  canvasAppHeight();
void needRedrawNow();

void applyOrientation(Orientation o, bool doSave) {
  currentOrient = o;
  display.setRotation(o == ORIENT_LANDSCAPE ? 1 : 0);
  SCR_W = display.width();
  SCR_H = display.height();
  canvas.deleteSprite();
  canvas.setPsram(true);
  canvas.createSprite(SCR_W, SCR_H);
  canvasApp.deleteSprite();
  canvasApp.setPsram(true);
  canvasApp.createSprite(SCR_W, canvasAppHeight());
  canvasApp.fillSprite(T().bg);
  loadCanvas();
  if (doSave) saveOrientPref();
  needRedrawNow();
}

// =============================================
// WIFI & NTP
// =============================================
char WIFI_SSID[64]="", WIFI_PASSWORD[64]="";
const char* NTP_SERVER="pool.ntp.org";
const long  GMT_OFFSET=7*3600;
const int   DST_OFFSET=0;
bool wifiConnected=false, ntpSynced=false;
bool airplaneMode=false;

void loadWifiCreds(){
  Preferences p; p.begin("wifi",true);
  p.getString("ssid","").toCharArray(WIFI_SSID,64);
  p.getString("pass","").toCharArray(WIFI_PASSWORD,64);
  p.end();
}
void saveWifiCreds(){
  Preferences p; p.begin("wifi",false);
  p.putString("ssid",WIFI_SSID);
  p.putString("pass",WIFI_PASSWORD);
  p.end();
}
void connectWifi(){
  if(airplaneMode || !strlen(WIFI_SSID)) return;
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<20){delay(300);t++;}
  wifiConnected=(WiFi.status()==WL_CONNECTED);
  if(wifiConnected){
    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);
    struct tm tm; if(getLocalTime(&tm,5000)) ntpSynced=true;
  }
}
void disconnectWifi(){
  WiFi.disconnect(true);
  wifiConnected=false;
}
void toggleAirplaneMode(){
  airplaneMode=!airplaneMode;
  if(airplaneMode) disconnectWifi();
  else connectWifi();
}

// =============================================
// SD CARD
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady=false;
const char* NOTE_FILE="/notepad.txt";

void initSD(){
  SD_MMC.setPins(SD_PIN_CLK,SD_PIN_CMD,SD_PIN_D0);
  sdReady=SD_MMC.begin("/sdcard",true);
}
String noteText="";
void loadNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_READ);
  if(f){noteText=f.readString();f.close();}
}
void saveNote(){
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_WRITE);
  if(f){f.print(noteText);f.close();}
}

int canvasAppHeight(){
  return SCR_H - STATUS_H - 44;
}
const char* canvasFileFor(Orientation o){
  return o==ORIENT_LANDSCAPE ? "/canvas_land.bin" : "/canvas_port.bin";
}
void loadCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_READ);
  if(f && f.size()==need) f.read((uint8_t*)canvasApp.getBuffer(), need);
  if(f) f.close();
}
void saveCanvas(){
  if(!sdReady) return;
  uint32_t need = (uint32_t)SCR_W * canvasAppHeight() * 2;
  File f=SD_MMC.open(canvasFileFor(currentOrient),FILE_WRITE);
  if(f){ f.write((uint8_t*)canvasApp.getBuffer(), need); f.close(); }
}

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration(){
  Preferences p; p.begin("touch_cal",false);
  bool done=p.getBool("done",false);
  if(done){
    uint16_t d[8]; p.getBytes("data",d,sizeof(d));
    display.setTouchCalibrate(d);
  } else {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE); display.setTextSize(2);
    display.setCursor(20,100); display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20,130); display.println("Sentuh tanda di setiap sudut");
    uint16_t d[8];
    display.calibrateTouch(d,TFT_WHITE,TFT_BLACK,15);
    p.putBytes("data",d,sizeof(d));
    p.putBool("done",true);
    display.fillScreen(TFT_BLACK);
    delay(200);
  }
  p.end();
}

// =============================================
// STATE GLOBAL / LIFECYCLE
// =============================================
int brightness=200;
bool needRedraw=true;
void needRedrawNow(){ needRedraw=true; }
bool locked = true;
bool controlCenterOpen = false;
bool dndMode = false;
bool kbVisible=false;
String* kbTarget=nullptr;
KbMode kbMode=KB_LOWER;

// -------- Navigation stack --------
#define NAV_MAX 8
Screen navStack[NAV_MAX];
int navDepth=0;
Screen curScreen(){ return navDepth>0 ? navStack[navDepth-1] : SCR_HOME; }

void appOnEnter(Screen s);
void appOnExit(Screen s);

void navPush(Screen s){
  appOnExit(curScreen());
  if(navDepth<NAV_MAX) navStack[navDepth++]=s;
  appOnEnter(s);
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}
void navGoHome(){
  appOnExit(curScreen());
  navDepth=0;
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}
void navBack(){
  appOnExit(curScreen());
  if(navDepth>0) navDepth--;
  if(navDepth>0) appOnEnter(navStack[navDepth-1]);
  kbVisible=false; kbTarget=nullptr;
  needRedraw=true;
}

// =============================================
// VIRTUAL KEYBOARD
// =============================================
const char* kbL[3][10]={
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"}};
const char* kbU[3][10]={
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",":"},
  {"Z","X","C","V","B","N","M","!","@","#"}};
const char* kbN[3][10]={
  {"1","2","3","4","5","6","7","8","9","0"},
  {"-","=","[","]","/","'","\"","<",">","\\"},
  {"~","!","@","#","$","%","^","&","*","("}};
const char* (*kbMaps[3])[10]={kbL,kbU,kbN};

int kbKeyW(){ return (SCR_W-8)/10 - 2; }
int kbKeyH(){ return 22; }
int kbY(){ return SCR_H - 4*(kbKeyH()+2) - 4; }
int kbOffCol(int row){
  int base = 4;
  if(row==1) base += (kbKeyW()+2)/2;
  if(row==2) base += (kbKeyW()+2);
  return base;
}

void drawKb(LGFX_Sprite& s){
  int y0=kbY();
  s.fillRect(0,y0-2,SCR_W,SCR_H-y0+2,T().surface);
  s.drawFastHLine(0,y0-2,SCR_W,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  int kw=kbKeyW(), kh=kbKeyH();
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2), ox=kbOffCol(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(kw+2);
      s.fillRoundRect(kx,ry,kw,kh,3,T().surface2);
      s.setTextColor(T().text); s.setTextSize(1);
      s.setCursor(kx+kw/2-3,ry+kh/2-4);
      s.print(lay[r][c]);
    }
  }
  int cy=y0+3*(kh+2);
  int spaceW = SCR_W - 96 - 88 - 8;
  s.fillRoundRect(4,cy,40,kh,3,kbMode==KB_UPPER?T().accent:T().surface2);
  s.setTextColor(T().text);s.setTextSize(1);s.setCursor(10,cy+kh/2-4);s.print("SHF");
  s.fillRoundRect(48,cy,44,kh,3,kbMode==KB_NUM?T().accent:T().surface2);
  s.setCursor(54,cy+kh/2-4);s.print(kbMode==KB_NUM?"ABC":"123");
  s.fillRoundRect(96,cy,max(40,spaceW),kh,3,T().surface2);
  s.setCursor(96+max(40,spaceW)/2-24,cy+kh/2-4);s.print("SPACE");
  s.fillRoundRect(SCR_W-88,cy,84,kh,3,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-70,cy+kh/2-4);s.print("<--");
}

void kbTouch(int x,int y){
  if(!kbVisible||!kbTarget) return;
  int y0=kbY();
  if(y<y0-2) return;
  int kw=kbKeyW(), kh=kbKeyH();
  int cy=y0+3*(kh+2);
  int spaceW = max(40, SCR_W-96-88-8);
  if(y>=cy&&y<=cy+kh){
    if(x>=4&&x<=44)            kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);
    else if(x>=48&&x<=92)      kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);
    else if(x>=96&&x<=96+spaceW) *kbTarget+=" ";
    else if(x>=SCR_W-88&&kbTarget->length()>0)
      *kbTarget=kbTarget->substring(0,kbTarget->length()-1);
    needRedraw=true;
    return;
  }
  auto* lay=kbMaps[(int)kbMode];
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2);
    if(y>=ry&&y<=ry+kh){
      int ox=kbOffCol(r);
      for(int c=0;c<10;c++){
        int kx=ox+c*(kw+2);
        if(x>=kx&&x<=kx+kw){
          *kbTarget+=lay[r][c];
          if(kbMode==KB_UPPER)kbMode=KB_LOWER;
          needRedraw=true;
          return;
        }
      }
    }
  }
}

// =============================================
// TOAST
// =============================================
String toastMsg="";
unsigned long toastUntil=0;
void showToast(const char* msg,int ms=1400){
  if(dndMode) return;
  toastMsg=msg; toastUntil=millis()+ms;
}
void drawToast(LGFX_Sprite& s){
  if(!toastMsg.length()||millis()>toastUntil) return;
  int tw=toastMsg.length()*6+16;
  int tx=SCR_W/2-tw/2;
  int ty=SCR_H-36;
  s.fillRoundRect(tx,ty,tw,22,6,T().accent2);
  s.setTextColor(T().bg);s.setTextSize(1);
  s.setCursor(tx+8,ty+7);s.print(toastMsg.c_str());
}

// =============================================
// STATUS BAR
// =============================================
void drawStatusBar(LGFX_Sprite& s){
  s.fillRect(0,0,SCR_W,STATUS_H,T().surface);
  s.drawFastHLine(0,STATUS_H,SCR_W,T().divider);
  struct tm t;
  bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(ok?T().text:T().subtext);s.setTextSize(1);
  if(ok){char b[6];sprintf(b,"%02d:%02d",t.tm_hour,t.tm_min);s.setCursor(8,7);s.print(b);}
  else{s.setCursor(8,7);s.print("--:--");}
  int rx = SCR_W-14;
  if(sdReady){s.setTextColor(T().good);s.setCursor(rx-46,7);s.print("SD");}
  if(airplaneMode){
    s.setTextColor(T().subtext); s.setCursor(rx-4,7); s.print("A");
  } else if(wifiConnected){
    s.fillCircle(rx,16,2,T().good);
    s.drawArc(rx,18,5,4,210,330,T().good);
    s.drawArc(rx,18,9,8,210,330,T().good);
  } else {s.setTextColor(T().danger);s.setCursor(rx-6,7);s.print("X");}
  if(dndMode){ s.setTextColor(T().accent); s.setCursor(rx-66,7); s.print("DND"); }
  if(curScreen()==SCR_HOME && !controlCenterOpen){
    s.fillRoundRect(SCR_W/2-12,STATUS_H+2,24,3,2,T().divider);
  }
}

// =============================================
// BACK BUTTON
// =============================================
#define BACK_W 62
#define BACK_H 24
int backX(){ return 4; }
int backY(){ return SCR_H-BACK_H-3; }

void drawBack(LGFX_Sprite& s){
  if(kbVisible)return;
  s.fillRoundRect(backX(),backY(),BACK_W,BACK_H,6,T().surface2);
  s.setTextColor(T().accent);s.setTextSize(1);
  s.setCursor(backX()+10,backY()+8);s.print("< Back");
}

bool isBack(int x,int y){
  return !kbVisible && x>=backX() && x<=backX()+BACK_W && y>=backY() && y<=backY()+BACK_H;
}

// =============================================
// LOCK SCREEN
// =============================================
float lockDragY=0; bool lockDragging=false;
void drawLockScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  char tb[6]; if(ok) sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min); else strcpy(tb,"--:--");
  s.setTextColor(T().text); s.setTextSize(4);
  int tw=strlen(tb)*24;
  s.setCursor(SCR_W/2-tw/2, SCR_H/2-60-(int)lockDragY);
  s.print(tb);
  if(ok){
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28]; sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext); s.setTextSize(1);
    int dw=strlen(db)*6;
    s.setCursor(SCR_W/2-dw/2, SCR_H/2-10-(int)lockDragY);
    s.print(db);
  }
  s.setTextColor(T().subtext); s.setTextSize(1);
  const char* hint="^ geser ke atas utk buka ^";
  s.setCursor(SCR_W/2-strlen(hint)*3, SCR_H-24);
  s.print(hint);
}

void lockScreenInput(bool touched,bool newT,int tx,int ty){
  static int startY=0;
  if(newT){ startY=ty; lockDragging=true; lockDragY=0; needRedraw=true; }
  else if(touched && lockDragging){
    int dy = startY-ty;
    lockDragY = constrain((float)dy, 0.0f, 80.0f);
    needRedraw=true;
  } else if(!touched && lockDragging){
    lockDragging=false;
    if(lockDragY>50){ locked=false; navGoHome(); }
    lockDragY=0; needRedraw=true;
  }
}

// =============================================
// CONTROL CENTER
// =============================================
float ccOffset = 0;
bool  ccAnimatingOpen=false, ccAnimatingClose=false;
#define CC_PANEL_H_FRAC 0.62f
void openControlCenter(){ controlCenterOpen=true; ccAnimatingOpen=true; needRedraw=true; }
void closeControlCenter(){ ccAnimatingOpen=false; ccAnimatingClose=true; needRedraw=true; }
int ccPanelH(){ return (int)(SCR_H*CC_PANEL_H_FRAC); }

void ccActWifi(){ if(wifiConnected) disconnectWifi(); else connectWifi(); }
void ccActAirplane(){ toggleAirplaneMode(); }
void ccActDnd(){ dndMode=!dndMode; }
void ccActTheme(){ themeIdx=(themeIdx+1)%THEME_COUNT; saveTheme(); }
void ccActOrient(){
  applyOrientation(currentOrient==ORIENT_LANDSCAPE?ORIENT_PORTRAIT:ORIENT_LANDSCAPE, true);
}

void drawControlCenter(LGFX_Sprite& s){
  int ph = (int)ccOffset;
  if(ph<=0) return;
  int panelY = 0;
  s.fillRoundRect(0,panelY,SCR_W,ph,0,T().surface);
  s.fillRoundRect(SCR_W/2-16,ph-10,32,4,2,T().divider);
  int gap=8, cols=3;
  int cw=(SCR_W-gap*(cols+1))/cols;
  int ch=54;
  const char* labels[6]={
    wifiConnected?"WiFi: ON":"WiFi: OFF",
    airplaneMode?"Airplane: ON":"Airplane: OFF",
    dndMode?"DND: ON":"DND: OFF",
    "Tema",
    currentOrient==ORIENT_LANDSCAPE?"Orient: Land":"Orient: Port",
    "Kunci Layar"
  };
  bool activeState[6]={wifiConnected,airplaneMode,dndMode,false,false,false};
  for(int i=0;i<6 && (STATUS_H+8+(i/cols+1)*(ch+gap)) < ph-8; i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap);
    int y=STATUS_H+8+row*(ch+gap);
    uint16_t bg = activeState[i]? T().accent : T().surface2;
    uint16_t fg = activeState[i]? T().bg : T().text;
    s.fillRoundRect(x,y,cw,ch,8,bg);
    s.setTextColor(fg); s.setTextSize(1);
    int lw=strlen(labels[i])*6;
    s.setCursor(x+cw/2-lw/2, y+ch/2-4);
    s.print(labels[i]);
  }
  int sliderY = STATUS_H+8+2*(ch+gap)+6;
  if(sliderY+30 < ph-8){
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(gap, sliderY); s.print("Brightness");
    int sx=gap, sw=SCR_W-gap*2, sy=sliderY+12;
    s.fillRoundRect(sx,sy,sw,10,5,T().divider);
    s.fillRoundRect(sx,sy,map(brightness,0,255,0,sw),10,5,T().accent);
  }
  drawToast(s);
}

void ccTouch(int x,int y){
  int ph=(int)ccOffset;
  if(y> ph-10 && y<=ph+4){ closeControlCenter(); return; }
  if(y>ph) { closeControlCenter(); return; }
  int gap=8, cols=3;
  int cw=(SCR_W-gap*(cols+1))/cols;
  int ch=54;
  void(*actions[6])() = { ccActWifi, ccActAirplane, ccActDnd, ccActTheme, ccActOrient, nullptr };
  for(int i=0;i<6;i++){
    int col=i%cols, row=i/cols;
    int bx=gap+col*(cw+gap);
    int by=STATUS_H+8+row*(ch+gap);
    if(x>=bx&&x<=bx+cw&&y>=by&&y<=by+ch){
      if(i==5){ locked=true; closeControlCenter(); return; }
      if(actions[i]) actions[i]();
      needRedraw=true;
      return;
    }
  }
  int sliderY = STATUS_H+8+2*(ch+gap)+6;
  int sy=sliderY+12;
  if(y>=sy-6 && y<=sy+16){
    int sx=gap, sw=SCR_W-gap*2;
    brightness=constrain(map(x-sx,0,sw,0,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
  }
}

// =============================================
// HOME SCREEN & APP INTERFACE
// =============================================
struct AppDef {
  const char* name; char sym; uint16_t color;
  void(*onEnter)(); void(*onExit)();
  void(*draw)(LGFX_Sprite&);
  void(*touch)(int,int,bool,bool);
  Screen screen;
};

void clockEnter(); void clockExit();
void drawClock(LGFX_Sprite&); void clockTouch(int,int,bool,bool);
void calcEnter(); void calcExit();
void drawCalc(LGFX_Sprite&); void calcTouch(int,int,bool,bool);
void sensorEnter(); void sensorExit();
void drawSensor(LGFX_Sprite&); void sensorTouch(int,int,bool,bool);
void settingsEnter(); void settingsExit();
void drawSettings(LGFX_Sprite&); void settingsTouch(int,int,bool,bool);
void notepadEnter(); void notepadExit();
void drawNotepad(LGFX_Sprite&); void notepadTouch(int,int,bool,bool);
void canvasEnter(); void canvasExit();
void drawCanvasScreen(LGFX_Sprite&); void canvasTouch(int,int,bool,bool);

AppDef apps[6] = {
  { "Jam",        'J', 0, clockEnter,    clockExit,    drawClock,        clockTouch,    SCR_CLOCK },
  { "Kalkulator", '+', 0, calcEnter,     calcExit,     drawCalc,         calcTouch,     SCR_CALC },
  { "Sensor",     '~', 0, sensorEnter,   sensorExit,   drawSensor,       sensorTouch,   SCR_SENSOR },
  { "Setting",    '@', 0, settingsEnter, settingsExit, drawSettings,     settingsTouch, SCR_SETTINGS },
  { "Notepad",    'N', 0, notepadEnter,  notepadExit,  drawNotepad,      notepadTouch,  SCR_NOTEPAD },
  { "Canvas",     'C', 0, canvasEnter,   canvasExit,   drawCanvasScreen, canvasTouch,   SCR_CANVAS },
};

void initAppColors(){
  apps[0].color=T().accent;   apps[1].color=T().accent2;
  apps[2].color=0x07FF;       apps[3].color=0xF81F;
  apps[4].color=0xFFE0;       apps[5].color=T().good;
}

int appIndexForScreen(Screen s){
  for(int i=0;i<6;i++) if(apps[i].screen==s) return i;
  return -1;
}
void appOnEnter(Screen s){ int i=appIndexForScreen(s); if(i>=0 && apps[i].onEnter) apps[i].onEnter(); }
void appOnExit(Screen s){  int i=appIndexForScreen(s); if(i>=0 && apps[i].onExit)  apps[i].onExit(); }

float homeScrollY=0, homeScrollVel=0;
int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 3 : 2; }
int homeCardW(){ int cols=homeCols(); return (SCR_W - (cols+1)*6)/cols; }
int homeCardH(){ return 60; }
int homeDockY(){ return SCR_H-38-6; }

void drawHome(LGFX_Sprite& s,float sc){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(T().text);s.setTextSize(3);
  char tb[6]; if(ok)sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min);else strcpy(tb,"--:--");
  int tw=strlen(tb)*18;
  s.setCursor(SCR_W/2-tw/2,26);s.print(tb);
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<6;i++){
    int col=i%cols, row=i/cols;
    int x=gap+col*(cw+gap), y=gridTop+row*(ch+gap)-(int)sc;
    if(y+ch<STATUS_H+2||y>homeDockY()-4)continue;
    s.fillRoundRect(x,y,cw,ch,10,T().surface);
    s.fillCircle(x+cw/2,y+18,14,apps[i].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[i].sym,0};
    s.setCursor(x+cw/2-6,y+11);s.print(sym);
    s.setTextColor(T().text);s.setTextSize(1);
    int nl=strlen(apps[i].name)*6;
    s.setCursor(x+cw/2-nl/2,y+ch-12);
    s.print(apps[i].name);
  }
  int dockY=homeDockY();
  s.fillRoundRect(6,dockY,SCR_W-12,38,12,T().surface2);
  int dockIdx[4]={0,4,5,3};
  int dw=(SCR_W-12)/4;
  for(int i=0;i<4;i++){
    int di=dockIdx[i];
    int cx=6+i*dw+dw/2;
    s.fillCircle(cx,dockY+19,15,apps[di].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[di].sym,0};
    s.setCursor(cx-6,dockY+12);s.print(sym);
  }
  drawToast(s);
}

Screen homeCheck(int x,int y,float sc){
  int dockY=homeDockY();
  if(y>=dockY&&y<=dockY+38){
    int dockIdx[4]={0,4,5,3};
    int dw=(SCR_W-12)/4;
    for(int i=0;i<4;i++){
      int cx=6+i*dw+dw/2;
      if(abs(x-cx)<dw/2) return apps[dockIdx[i]].screen;
    }
  }
  int cols=homeCols(), cw=homeCardW(), ch=homeCardH();
  int gap=6, gridTop=72;
  for(int i=0;i<6;i++){
    int col=i%cols,row=i/cols;
    int ax=gap+col*(cw+gap), ay=gridTop+row*(ch+gap)-(int)sc;
    if(x>=ax&&x<=ax+cw&&y>=ay&&y<=ay+ch) return apps[i].screen;
  }
  return SCR_HOME;
}

int homeMaxScroll(){
  int cols=homeCols();
  int rows=(6+cols-1)/cols;
  int ch=homeCardH(), gap=6, gridTop=72;
  int needed = gridTop + rows*(ch+gap) - homeDockY();
  return max(0, needed);
}

// =============================================
// APP: JAM
// =============================================
void clockEnter(){} void clockExit(){}
void drawClock(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,28);s.print("Jam");
  struct tm t;bool ok=ntpSynced&&getLocalTime(&t);
  if(ok){
    char tb[9];sprintf(tb,"%02d:%02d:%02d",t.tm_hour,t.tm_min,t.tm_sec);
    s.setTextColor(T().text);s.setTextSize(3);s.setCursor(20,55);s.print(tb);
    const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* mons[]={"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28];sprintf(db,"%s, %02d %s %04d",days[t.tm_wday],t.tm_mday,mons[t.tm_mon],t.tm_year+1900);
    s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(20,100);s.print(db);
    s.drawFastHLine(20,114,SCR_W-40,T().accent);
    s.setTextColor(T().good);s.setCursor(20,120);s.print("NTP Sync OK");
  } else {
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);s.print("Tidak sync");
  }
  drawBack(s);drawToast(s);
}
void clockTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)) navBack();
}

// =============================================
// APP: KALKULATOR (FIXED GRID & BACK BUTTON)
// =============================================
String calcInput="0";float calcA=0;char calcOp=0;bool calcNew=true;
const char* calcLabels[5][4] = {
  {"C","+/-","%","/"},
  {"7","8","9","x"},
  {"4","5","6","-"},
  {"1","2","3","+"},
  {"0",".","=","="}
};
void calcEnter(){} void calcExit(){}

void calcBtnRect(int r,int c,int& x,int& y,int& w,int& h){
  int top = STATUS_H + 34;
  int bottom = backY() - 6;
  int gap = 4;
  int gridW = SCR_W - 8;
  int gridH = bottom - top;
  int rows=5, cols=4;
  int cw = (gridW-(cols-1)*gap)/cols;
  int ch = (gridH-(rows-1)*gap)/rows;
  y = top + r*(ch+gap);
  h = ch;
  if(r==4 && c==0){ x=4; w=cw*2+gap; return; }
  if(r==4 && c==2){ x=4+2*(cw+gap); w=cw*2+gap; return; }
  if(r==4 && (c==1||c==3)) { w=0; return; }
  x = 4 + c*(cw+gap);
  w = cw;
}

void drawCalc(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,25);s.print("Kalkulator");
  s.fillRoundRect(4,STATUS_H+8,SCR_W-8,22,4,T().surface);
  s.setTextColor(T().text);s.setTextSize(2);
  int tw2=calcInput.length()*12;
  s.setCursor(max(8,SCR_W-12-tw2),STATUS_H+11);s.print(calcInput);
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int x,y,w,h; calcBtnRect(r,c,x,y,w,h);
      if(w==0) continue;
      const char* l=calcLabels[r][c];
      bool isOp = (!strcmp(l,"/")||!strcmp(l,"x")||!strcmp(l,"-")||!strcmp(l,"+"));
      bool isEq = (r==4&&c==2);
      uint16_t bg = isEq?T().accent2:(isOp?T().accent:T().surface);
      uint16_t fg = (isOp||isEq)?T().bg:T().text;
      s.fillRoundRect(x,y,w,h,6,bg);
      s.setTextColor(fg);s.setTextSize(2);
      int lw=strlen(l)*12;s.setCursor(x+w/2-lw/2,y+h/2-8);s.print(l);
    }
  }
  drawBack(s);
  drawToast(s);
}

void calcApplyLabel(const char* l){
  if(!strcmp(l,"C")){calcInput="0";calcA=0;calcOp=0;calcNew=true;}
  else if(!strcmp(l,"+/-"))calcInput=String(calcInput.toFloat()*-1);
  else if(!strcmp(l,"%"))calcInput=String(calcInput.toFloat()/100);
  else if(!strcmp(l,"=")){
    float b2=calcInput.toFloat(),res=0;
    if(calcOp=='+')res=calcA+b2;else if(calcOp=='-')res=calcA-b2;
    else if(calcOp=='x')res=calcA*b2;
    else if(calcOp=='/')res=b2?calcA/b2:0;
    calcInput=(res==(int)res)?String((int)res):String(res,4);
    calcOp=0;calcNew=true;
  }
  else if(!strcmp(l,"+")||!strcmp(l,"-")||!strcmp(l,"x")||!strcmp(l,"/")){
    calcA=calcInput.toFloat();calcOp=l[0];calcNew=true;
  } else {
    if(calcNew){calcInput="";calcNew=false;}
    if(!strcmp(l,".")&&calcInput.indexOf('.')>=0)return;
    if(calcInput=="0"&&strcmp(l,".")!=0)calcInput="";
    calcInput+=l;
  }
  if(calcInput.length()>12)calcInput=calcInput.substring(0,12);
}

void calcTouch(int x,int y,bool held,bool isNew){
  if(!isNew) return;
  if(isBack(x,y)){ navBack(); return; }
  for(int r=0;r<5;r++){
    for(int c=0;c<4;c++){
      int bx,by,bw,bh; calcBtnRect(r,c,bx,by,bw,bh);
      if(bw==0) continue;
      if(x>=bx&&x<=bx+bw&&y>=by&&y<=by+bh){ 
        calcApplyLabel(calcLabels[r][c]); 
        needRedraw=true; 
        return; 
      }
    }
  }
}

// =============================================
// APP: SENSOR
// =============================================
void sensorEnter(){} void sensorExit(){}
void drawSensor(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent2);s.setTextSize(1);s.setCursor(8,28);s.print("Sensor");
  float temp=temperatureRead();
  int bx=20, by=42, bw=SCR_W-40, bh=100;
  s.fillRoundRect(bx,by,bw,bh,14,T().surface);
  s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(bx+15,by+14);s.print("Suhu Internal Chip");
  char buf[8];sprintf(buf,"%.1f",temp);
  s.setTextColor(T().accent2);s.setTextSize(4);s.setCursor(bx+20,by+28);s.print(buf);
  s.setTextSize(2);s.print(" C");
  int gy=by+bh+6;
  s.fillRoundRect(bx,gy,bw,18,6,T().divider);
  int barMax=bw-4;
  int bwv = constrain(map((int)temp,20,90,0,barMax),0,barMax);
  uint16_t bc=(temp<50)?T().good:(temp<70)?T().accent:T().danger;
  s.fillRoundRect(bx+2,gy+2,bwv,14,4,bc);
  s.setTextColor(T().subtext);s.setTextSize(1);
  s.setCursor(bx,gy+22);s.print("20C");
  s.setCursor(bx+bw/2-12,gy+22);s.print("55C");
  s.setCursor(bx+bw-24,gy+22);s.print("90C");
  drawBack(s);drawToast(s);
}
void sensorTouch(int x,int y,bool held,bool isNew){
  if(isNew && isBack(x,y)) navBack();
}

// =============================================
// APP: SETTINGS (FIXED LAYOUT & RE-CALIBRATION MOVED RIGHT)
// =============================================
String settSSID="",settPass="";
bool settShowPass=false;int settFocus=-1;
void settingsEnter(){ settSSID=String(WIFI_SSID); settPass=String(WIFI_PASSWORD); }
void settingsExit(){}

void drawSettings(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,26);s.print("Pengaturan");
  int rowY = STATUS_H+12;
  int rowW = SCR_W-16;
  
  // Row 1: Brightness
  s.fillRoundRect(8,rowY,rowW,30,8,T().surface);
  s.setTextColor(T().text);s.setCursor(18,rowY+8);s.print("Kecerahan");
  s.fillRoundRect(18,rowY+18,rowW-70,7,4,T().divider);
  s.fillRoundRect(19,rowY+19,map(brightness,0,255,0,rowW-72),5,3,T().accent);
  char bb[6];sprintf(bb,"%d%%",brightness*100/255);
  s.setTextColor(T().subtext);s.setCursor(rowW-40,rowY+17);s.print(bb);
  
  // Row 2: Theme
  rowY+=34;
  s.fillRoundRect(8,rowY,rowW,28,8,T().surface);
  s.setTextColor(T().text);s.setCursor(18,rowY+8);s.print("Tema:");
  int tbtnW=(rowW-50)/THEME_COUNT;
  for(int i=0;i<THEME_COUNT;i++){
    uint16_t bg=(i==themeIdx)?T().accent:T().surface2;
    uint16_t fg=(i==themeIdx)?T().bg:T().text;
    int tx=54+i*tbtnW;
    s.fillRoundRect(tx,rowY+2,tbtnW-4,24,6,bg);
    s.setTextColor(fg);s.setTextSize(1);
    int nl=strlen(themes[i].name)*6;
    s.setCursor(tx+(tbtnW-4)/2-nl/2,rowY+8);s.print(themes[i].name);
  }
  
  // Row 3: SSID
  rowY+=32;
  s.fillRoundRect(8,rowY,rowW,26,6,settFocus==0?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(18,rowY+8);s.print("SSID:");
  s.setTextColor(T().text);s.setCursor(64,rowY+8);
  String sd2=settSSID.length()?settSSID:"(ketuk)";
  if(sd2.length()>22)sd2=sd2.substring(0,22)+"..";
  s.print(sd2.c_str());
  
  // Row 4: Pass
  rowY+=30;
  s.fillRoundRect(8,rowY,rowW-32,26,6,settFocus==1?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(18,rowY+8);s.print("Pass:");
  s.setTextColor(T().text);s.setCursor(64,rowY+8);
  if(settPass.length()){
    if(settShowPass)s.print(settPass.substring(0,18).c_str());
    else for(int i=0;i<(int)settPass.length()&&i<18;i++)s.print("*");
  } else s.print("(ketuk)");
  s.fillRoundRect(8+rowW-28,rowY,28,26,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(8+rowW-22,rowY+8);s.print(settShowPass?"H":"S");
  
  // Row 5: Action Buttons (Sambungkan di kiri, Kalibrasi Ulang di kanan)
  rowY+=30;
  int btnW=(rowW-8)/2;
  // Sambungkan (Kiri)
  s.fillRoundRect(8,rowY,btnW,26,8,T().accent);
  s.setTextColor(T().bg);s.setCursor(16,rowY+8);s.print("Sambungkan");
  // Kalibrasi Ulang (Kanan — tidak menabrak Back!)
  s.fillRoundRect(SCR_W/2+4,rowY,btnW,26,8,T().surface2);
  s.setTextColor(T().text);s.setCursor(SCR_W/2+10,rowY+8);s.print("Kalibrasi Ulang");

  // Status WiFi
  rowY+=28;
  s.setTextColor(wifiConnected?T().good:T().danger);
  String ws=wifiConnected?String("WiFi: ")+WiFi.SSID():"Status: Disconnected";
  s.setCursor(8,rowY);s.print(ws.substring(0,34).c_str());
  
  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void settingsTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;settFocus=-1;}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(isBack(x,y)){ navBack(); return; }

  int rowW = SCR_W-16;
  int rowY = STATUS_H+12;
  if(x>=18&&x<=18+rowW-70&&y>=rowY&&y<=rowY+30){
    brightness=constrain(map(x-18,0,rowW-70,10,255),10,255);
    display.setBrightness(brightness);
    needRedraw=true;
    return;
  }
  rowY+=34;
  int tbtnW=(rowW-50)/THEME_COUNT;
  if(y>=rowY&&y<=rowY+28){
    for(int i=0;i<THEME_COUNT;i++){
      int tx=54+i*tbtnW;
      if(x>=tx&&x<=tx+tbtnW-4){
        themeIdx=i;saveTheme();initAppColors();
        showToast("Tema diganti");needRedraw=true;return;
      }
    }
  }
  rowY+=32;
  if(y>=rowY&&y<=rowY+26){ settFocus=0;kbTarget=&settSSID;kbVisible=true;kbMode=KB_LOWER;needRedraw=true;return; }
  rowY+=30;
  if(y>=rowY&&y<=rowY+26){
    if(x>=8+rowW-28){ settShowPass=!settShowPass; needRedraw=true; return; }
    settFocus=1;kbTarget=&settPass;kbVisible=true;kbMode=KB_LOWER;needRedraw=true;return;
  }
  rowY+=30;
  int btnW=(rowW-8)/2;
  if(y>=rowY&&y<=rowY+26){
    // Left: Sambungkan
    if(x>=8&&x<=8+btnW){
      settSSID.toCharArray(WIFI_SSID,64);
      settPass.toCharArray(WIFI_PASSWORD,64);
      saveWifiCreds();connectWifi();
      showToast(wifiConnected?"WiFi OK":"Gagal connect");
      needRedraw=true;
      return;
    }
    // Right: Kalibrasi Ulang
    if(x>=SCR_W/2+4 && x<=SCR_W/2+4+btnW){
      Preferences p;p.begin("touch_cal",false);p.putBool("done",false);p.end();
      ESP.restart();
      return;
    }
  }
}

// =============================================
// APP: NOTEPAD (FIXED VIRTUAL KEYBOARD & REDRAW)
// =============================================
void notepadEnter(){} void notepadExit(){ saveNote(); }

void drawNotepad(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,26);s.print("Notepad");
  
  s.fillRoundRect(SCR_W-70,24,62,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-56,29);s.print("Hapus");
  
  int bot = kbVisible ? kbY()-4 : backY()-6;
  s.fillRoundRect(4,44,SCR_W-8,bot-44,6,T().surface);
  s.setTextColor(T().text);s.setTextSize(1);s.setTextWrap(true);
  s.setCursor(10,50);s.print((noteText+"|").c_str());
  
  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void notepadTouch(int x,int y,bool held,bool isNew){
  if(kbVisible){
    if(!isNew) return;
    int y0=kbY();
    if(y<y0-2){kbVisible=false;kbTarget=nullptr;saveNote();}
    else kbTouch(x,y);
    needRedraw=true;
    return;
  }
  if(!isNew) return;
  
  if(x>=SCR_W-70&&x<=SCR_W-4&&y>=24&&y<=42){
    noteText="";saveNote();showToast("Dihapus");
    needRedraw=true;
    return;
  }
  
  if(isBack(x,y)){ navBack(); return; }
  
  int textBot = backY()-6;
  if(y>=44&&y<=textBot){
    kbTarget=&noteText;
    kbVisible=true;
    kbMode=KB_LOWER;
    needRedraw=true;
  }
}

// =============================================
// APP: CANVAS (FIXED TOOLBAR LAYOUT & BACK TOUCH)
// =============================================
uint16_t palette[]={0x0000,0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F,0xFD40,0xFFFF};
#define PAL_N 9
uint16_t drawColor=0xFFFF;
int brushSize=3;
int lastDX=-1,lastDY=-1;

int canvasCapY(){ return STATUS_H+2; }
int canvasToolY(){ return SCR_H-44; }

void canvasEnter(){ lastDX=-1; }
void canvasExit(){ saveCanvas(); }

void drawCanvasScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,14);s.print("Canvas");
  
  // Push buffer gambar
  canvasApp.pushSprite(&s,0,canvasCapY());
  
  // Area Toolbar bawah
  int ty=canvasToolY();
  s.fillRect(0,ty,SCR_W,SCR_H-ty,T().surface);
  s.drawFastHLine(0,ty,SCR_W,T().divider);
  
  // Row 1 Toolbar: Palette Warna
  int palGap = (SCR_W-8)/PAL_N;
  for(int i=0;i<PAL_N;i++){
    int px=4+i*palGap;
    s.fillCircle(px+palGap/2,ty+10,7,palette[i]);
    if(palette[i]==drawColor) s.drawCircle(px+palGap/2,ty+10,9,T().text);
  }
  
  // Row 2 Toolbar: Tombol Back, Ukuran Brush, dan Clear
  int r2Y = ty + 22;
  // Tombol Back di kiri bawah toolbar
  s.fillRoundRect(4,r2Y,56,18,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(10,r2Y+5);s.print("< Back");
  
  // Ukuran Brush (Tengah)
  s.fillRoundRect(66,r2Y,60,18,4,T().surface2);
  s.setTextColor(T().text);char bs[8];sprintf(bs,"B:%d",brushSize);
  s.setCursor(76,r2Y+5);s.print(bs);
  
  // Tombol Clear (Kanan)
  s.fillRoundRect(SCR_W-64,r2Y,60,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(SCR_W-52,r2Y+5);s.print("CLR");
  
  drawToast(s);
}

void canvasTouch(int x,int y,bool held,bool isNew){
  int ty=canvasToolY();
  
  // Touch di area toolbar
  if(y>=ty){
    if(isNew){
      int r2Y = ty + 22;
      // 1. Cek Back button dulu
      if(x>=4 && x<=60 && y>=r2Y){ navBack(); return; }
      
      // 2. Cek Palette (Row 1 toolbar)
      if(y>=ty && y<r2Y){
        int palGap=(SCR_W-8)/PAL_N;
        for(int i=0;i<PAL_N;i++){
          int px=4+i*palGap;
          if(x>=px && x<px+palGap){ drawColor=palette[i]; lastDX=-1; needRedraw=true; return; }
        }
      }
      
      // 3. Cek Brush & Clear (Row 2 toolbar)
      if(y>=r2Y){
        if(x>=66 && x<=126){ brushSize=(brushSize%8)+1; lastDX=-1; needRedraw=true; return; }
        if(x>=SCR_W-64 && x<=SCR_W-4){ canvasApp.fillSprite(T().bg); lastDX=-1; saveCanvas(); showToast("Dibersihkan"); needRedraw=true; return; }
      }
    }
    return;
  }
  
  // Touch di area gambar
  int capY=canvasCapY();
  if(y>=capY && y<ty){
    int cy=y-capY;
    if(held&&lastDX>=0){
      canvasApp.drawLine(lastDX,lastDY,x,cy,drawColor);
      for(int t2=1;t2<brushSize;t2++){
        canvasApp.drawLine(lastDX,lastDY+t2,x,cy+t2,drawColor);
        canvasApp.drawLine(lastDX+t2,lastDY,x+t2,cy,drawColor);
      }
    } else canvasApp.fillCircle(x,cy,brushSize/2,drawColor);
    lastDX=x;lastDY=cy;
    needRedraw=true;
  }
}

// =============================================
// PUSH FRAME
// =============================================
void push(){ canvas.pushSprite(0,0); }
void renderCurrentFrame(){
  if(locked){
    drawLockScreen(canvas);
  } else if(curScreen()==SCR_HOME){
    drawHome(canvas,homeScrollY);
  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0) apps[idx].draw(canvas);
  }
  if(!locked && controlCenterOpen) drawControlCenter(canvas);
}

// =============================================
// SETUP
// =============================================
bool wasTouched=false;
int  touchStartX=0,touchStartY=0,touchLastY=0;
bool isSwiping=false;
unsigned long swipeStartTime=0;
int gStartX=0,gStartY=0; bool gGestureDone=false;

void setup(){
  Serial.begin(115200);
  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);
  canvas.setPsram(true);
  canvas.createSprite(320,240);
  canvas.fillSprite(0x1084);
  canvas.setTextColor(0xFD40);canvas.setTextSize(3);
  canvas.setCursor(50,80);canvas.print("ESP Phone");
  canvas.setTextColor(0x8C51);canvas.setTextSize(1);
  canvas.setCursor(60,130);canvas.print("Powered by ESP32-S3 + LGFX");
  push();delay(1200);
  if(display.touch())loadOrRunCalibration();
  loadTheme();
  initAppColors();
  drawColor=T().text;
  initSD();
  loadWifiCreds();
  connectWifi();
  canvasApp.setPsram(true);
  canvasApp.createSprite(320,240-STATUS_H-44);
  canvasApp.fillSprite(T().bg);
  loadNote();
  Orientation savedOrient = loadOrientPref();
  applyOrientation(savedOrient, false);
  locked = true;
  needRedraw = true;
}

// =============================================
// LOOP
// =============================================
unsigned long lastClk=0,lastSen=0;
void loop(){
  lgfx::touch_point_t tp;
  bool touched=display.getTouch(&tp);
  int tx=touched?(int)tp.x:0,ty=touched?(int)tp.y:0;
  bool newT=touched&&!wasTouched;

  // -------- Control Center animation --------
  if(ccAnimatingOpen){
    ccOffset += (float)ccPanelH()/6.0f;
    if(ccOffset>=ccPanelH()){ ccOffset=ccPanelH(); ccAnimatingOpen=false; }
    needRedraw=true;
  } else if(ccAnimatingClose){
    ccOffset -= (float)ccPanelH()/6.0f;
    if(ccOffset<=0){ ccOffset=0; ccAnimatingClose=false; controlCenterOpen=false; }
    needRedraw=true;
  }

  // -------- Lock Screen --------
  if(locked){
    lockScreenInput(touched,newT,tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  // -------- Control Center Open --------
  if(controlCenterOpen){
    if(newT) ccTouch(tx,ty);
    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }
    wasTouched=touched; delay(8); return;
  }

  // -------- Global Gestures --------
  if(newT){ gStartX=tx; gStartY=ty; gGestureDone=false; }
  if(touched && !gGestureDone && !kbVisible){
    int dy=ty-gStartY, dx=tx-gStartX;
    if(gStartY<STATUS_H+6 && dy>28 && abs(dy)>abs(dx)){
      openControlCenter(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
    if(gStartY>SCR_H-16 && dy<-28 && abs(dy)>abs(dx) && curScreen()!=SCR_HOME){
      navGoHome(); gGestureDone=true;
      wasTouched=touched; delay(8); return;
    }
  }

  // ============ HOME ============
  if(curScreen()==SCR_HOME){
    if(touched){
      if(!wasTouched){
        touchStartX=tx;touchStartY=ty;touchLastY=ty;
        isSwiping=false;swipeStartTime=millis();
      } else {
        if(abs(ty-touchStartY)>12)isSwiping=true;
        if(isSwiping){
          homeScrollVel=(touchLastY-ty)*0.8f;
          homeScrollY+=touchLastY-ty;
          homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
          needRedraw=true;
        }
        touchLastY=ty;
      }
    } else if(wasTouched){
      if(!isSwiping&&millis()-swipeStartTime<400){
        Screen nx=homeCheck(touchStartX,touchStartY,homeScrollY);
        if(nx!=SCR_HOME) navPush(nx);
      }
    } else {
      if(fabsf(homeScrollVel)>0.5f){
        homeScrollY+=homeScrollVel;homeScrollVel*=0.85f;
        homeScrollY=constrain(homeScrollY,0.0f,(float)homeMaxScroll());
        needRedraw=true;
      }
    }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}

  // ============ APP SCREENS ============
  } else {
    int idx=appIndexForScreen(curScreen());
    if(idx>=0){
      bool held = touched&&wasTouched;
      if(curScreen()==SCR_CANVAS){
        if(touched){ apps[idx].touch(tx,ty,held,newT); }
      } else {
        if(newT) apps[idx].touch(tx,ty,false,true);
        else if(touched && held) apps[idx].touch(tx,ty,true,false);
      }
    }
    if(curScreen()==SCR_CLOCK && millis()-lastClk>500){ needRedraw=true; lastClk=millis(); }
    if(curScreen()==SCR_SENSOR && millis()-lastSen>1000){ needRedraw=true; lastSen=millis(); }
    if(needRedraw){renderCurrentFrame();push();needRedraw=false;}
  }
  wasTouched=touched;
  delay(8);
}
