// =================================================================
// ren_phone v3 — LovyanGFX only (no LVGL)
// ESP32-S3, ILI9341 320x240 landscape, XPT2046 touch
// SD Card: SDIO 1-bit -> CLK=39, CMD=38, D0=40
// =================================================================
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"

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
LGFX_Sprite canvas(&display);
LGFX_Sprite canvasApp(&display);

// =============================================
// TEMA
// =============================================
struct Theme {
  const char* name;
  uint16_t bg, surface, surface2, accent, accent2,
           text, subtext, divider, good, danger;
};

Theme themes[] = {
  { "Dark",
    0x1084, 0x2104, 0x31A6, 0xFD40, 0x04FF,
    0xFFFF, 0x8C51, 0x2965, 0x07E0, 0xF800 },
  { "AMOLED",
    0x0000, 0x1082, 0x2104, 0xFD40, 0x04FF,
    0xFFFF, 0x8C51, 0x2104, 0x07E0, 0xF800 },
  { "Light",
    0xEF5D, 0xFFFF, 0xDEFB, 0xFD00, 0x001F,
    0x0000, 0x6B4D, 0xC618, 0x03E0, 0xD000 },
  { "Pastel",
    0xFF1F, 0xFFFF, 0xFF9F, 0xFB16, 0x5D9F,
    0x39C7, 0x9C92, 0xFEB7, 0x2FE7, 0xE8B4 },
};
#define THEME_COUNT 4
int themeIdx = 0;
Theme& T() { return themes[themeIdx]; }

void saveTheme() { Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }
void loadTheme() {
  Preferences p; p.begin("ui",true);
  themeIdx = p.getInt("theme",0); p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;
}

// =============================================
// WIFI & NTP
// =============================================
char WIFI_SSID[64]="", WIFI_PASSWORD[64]="";
const char* NTP_SERVER="pool.ntp.org";
const long  GMT_OFFSET=7*3600;
const int   DST_OFFSET=0;
bool wifiConnected=false, ntpSynced=false;

void loadWifiCreds() {
  Preferences p; p.begin("wifi",true);
  p.getString("ssid","").toCharArray(WIFI_SSID,64);
  p.getString("pass","").toCharArray(WIFI_PASSWORD,64);
  p.end();
}
void saveWifiCreds() {
  Preferences p; p.begin("wifi",false);
  p.putString("ssid",WIFI_SSID);
  p.putString("pass",WIFI_PASSWORD);
  p.end();
}
void connectWifi() {
  if(!strlen(WIFI_SSID)) return;
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<20){delay(300);t++;}
  wifiConnected=(WiFi.status()==WL_CONNECTED);
  if(wifiConnected){
    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);
    struct tm tm; if(getLocalTime(&tm,5000)) ntpSynced=true;
  }
}

// =============================================
// SD CARD
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady=false;
const char* NOTE_FILE="/notepad.txt";
const char* CANVAS_FILE="/canvas.bin";

void initSD() {
  SD_MMC.setPins(SD_PIN_CLK,SD_PIN_CMD,SD_PIN_D0);
  sdReady=SD_MMC.begin("/sdcard",true);
}

String noteText="";
void loadNote() {
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_READ);
  if(f){noteText=f.readString();f.close();}
}
void saveNote() {
  if(!sdReady) return;
  File f=SD_MMC.open(NOTE_FILE,FILE_WRITE);
  if(f){f.print(noteText);f.close();}
}

#define CANVAS_W     320
#define CANVAS_H_APP 190
#define CANVAS_BUF_SIZE (CANVAS_W*(uint32_t)CANVAS_H_APP*2)

void loadCanvas() {
  if(!sdReady) return;
  File f=SD_MMC.open(CANVAS_FILE,FILE_READ);
  if(f&&f.size()==CANVAS_BUF_SIZE)
    f.read((uint8_t*)canvasApp.getBuffer(),CANVAS_BUF_SIZE);
  if(f)f.close();
}
void saveCanvas() {
  if(!sdReady) return;
  File f=SD_MMC.open(CANVAS_FILE,FILE_WRITE);
  if(f){f.write((uint8_t*)canvasApp.getBuffer(),CANVAS_BUF_SIZE);f.close();}
}

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration() {
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
// STATE GLOBAL
// =============================================
int brightness=200;
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR,
              SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS };
Screen currentScreen=SCR_HOME;
bool needRedraw=true;

bool wasTouched=false;
int  touchStartX=0,touchStartY=0,touchLastY=0;
bool isSwiping=false;
unsigned long swipeStartTime=0;

float homeScrollY=0, homeScrollVel=0;

bool canvasReady=false;
uint16_t drawColor=0xFFFF;
int  brushSize=3;
int  lastDX=-1, lastDY=-1;

// =============================================
// VIRTUAL KEYBOARD
// =============================================
enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };
KbMode  kbMode=KB_LOWER;
bool    kbVisible=false;
String* kbTarget=nullptr;

#define KB_KEY_W 29
#define KB_KEY_H 22
#define KB_GAP   2
#define KB_Y     148

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
int kbOff(int r){return 6+(r==1?15:(r==2?30:0));}

void drawKb(LGFX_Sprite& s){
  s.fillRect(0,KB_Y-2,320,240-KB_Y+2,T().surface);
  s.drawFastHLine(0,KB_Y-2,320,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  for(int r=0;r<3;r++){
    int ry=KB_Y+r*(KB_KEY_H+KB_GAP), ox=kbOff(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(KB_KEY_W+KB_GAP);
      s.fillRoundRect(kx,ry,KB_KEY_W,KB_KEY_H,3,T().surface2);
      s.setTextColor(T().text); s.setTextSize(1);
      s.setCursor(kx+KB_KEY_W/2-3,ry+KB_KEY_H/2-4);
      s.print(lay[r][c]);
    }
  }
  int cy=KB_Y+3*(KB_KEY_H+KB_GAP);
  s.fillRoundRect(4,cy,40,KB_KEY_H,3,kbMode==KB_UPPER?T().accent:T().surface2);
  s.setTextColor(T().text);s.setTextSize(1);s.setCursor(10,cy+KB_KEY_H/2-4);s.print("SHF");
  s.fillRoundRect(48,cy,44,KB_KEY_H,3,kbMode==KB_NUM?T().accent:T().surface2);
  s.setCursor(54,cy+KB_KEY_H/2-4);s.print(kbMode==KB_NUM?"ABC":"123");
  s.fillRoundRect(96,cy,128,KB_KEY_H,3,T().surface2);
  s.setCursor(136,cy+KB_KEY_H/2-4);s.print("SPACE");
  s.fillRoundRect(228,cy,88,KB_KEY_H,3,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(248,cy+KB_KEY_H/2-4);s.print("<--");
}

void kbTouch(int x,int y){
  if(!kbVisible||!kbTarget) return;
  if(y<KB_Y-2) return;
  int cy=KB_Y+3*(KB_KEY_H+KB_GAP);
  if(y>=cy&&y<=cy+KB_KEY_H){
    if(x>=4&&x<=44)       kbMode=(kbMode==KB_UPPER?KB_LOWER:KB_UPPER);
    else if(x>=48&&x<=92) kbMode=(kbMode==KB_NUM?KB_LOWER:KB_NUM);
    else if(x>=96&&x<=224)*kbTarget+=" ";
    else if(x>=228&&kbTarget->length()>0)
      *kbTarget=kbTarget->substring(0,kbTarget->length()-1);
    return;
  }
  auto* lay=kbMaps[(int)kbMode];
  for(int r=0;r<3;r++){
    int ry=KB_Y+r*(KB_KEY_H+KB_GAP);
    if(y>=ry&&y<=ry+KB_KEY_H){
      int ox=kbOff(r);
      for(int c=0;c<10;c++){
        int kx=ox+c*(KB_KEY_W+KB_GAP);
        if(x>=kx&&x<=kx+KB_KEY_W){
          *kbTarget+=lay[r][c];
          if(kbMode==KB_UPPER)kbMode=KB_LOWER;
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
  toastMsg=msg; toastUntil=millis()+ms;
}
void drawToast(LGFX_Sprite& s){
  if(!toastMsg.length()||millis()>toastUntil) return;
  int tw=toastMsg.length()*6+16;
  int tx=160-tw/2;
  s.fillRoundRect(tx,204,tw,22,6,T().accent2);
  s.setTextColor(T().bg);s.setTextSize(1);
  s.setCursor(tx+8,211);s.print(toastMsg.c_str());
}

// =============================================
// STATUS BAR
// =============================================
void drawStatusBar(LGFX_Sprite& s){
  s.fillRect(0,0,320,22,T().surface);
  s.drawFastHLine(0,22,320,T().divider);
  struct tm t;
  bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(ok?T().text:T().subtext);s.setTextSize(1);
  if(ok){char b[6];sprintf(b,"%02d:%02d",t.tm_hour,t.tm_min);s.setCursor(8,7);s.print(b);}
  else{s.setCursor(8,7);s.print("--:--");}
  if(sdReady){s.setTextColor(T().good);s.setCursor(258,7);s.print("SD");}
  if(wifiConnected){
    s.fillCircle(306,16,2,T().good);
    s.drawArc(306,18,5,4,210,330,T().good);
    s.drawArc(306,18,9,8,210,330,T().good);
  } else {s.setTextColor(T().danger);s.setCursor(300,7);s.print("X");}
}

// =============================================
// BACK BUTTON
// =============================================
#define BACK_X 4
#define BACK_Y 213
#define BACK_W 62
#define BACK_H 24

void drawBack(LGFX_Sprite& s){
  if(kbVisible)return;
  s.fillRoundRect(BACK_X,BACK_Y,BACK_W,BACK_H,6,T().surface2);
  s.setTextColor(T().accent);s.setTextSize(1);
  s.setCursor(BACK_X+10,BACK_Y+8);s.print("< Back");
}
bool isBack(int x,int y){
  return !kbVisible&&x>=BACK_X&&x<=BACK_X+BACK_W&&y>=BACK_Y&&y<=BACK_Y+BACK_H;
}
void goHome(){
  kbVisible=false;kbTarget=nullptr;
  currentScreen=SCR_HOME;needRedraw=true;
}

// =============================================
// HOME SCREEN
// =============================================
struct AppDef{const char* name;uint16_t color;char sym;};
AppDef apps[6]={
  {"Jam",      0,'J'},
  {"Kalkulator",0,'+'},
  {"Sensor",   0,'~'},
  {"Setting",  0,'@'},
  {"Notepad",  0,'N'},
  {"Canvas",   0,'C'},
};
void initAppColors(){
  apps[0].color=T().accent;   apps[1].color=T().accent2;
  apps[2].color=0x07FF;       apps[3].color=0xF81F;
  apps[4].color=0xFFE0;       apps[5].color=T().good;
}

#define APP_CARD_W 90
#define APP_CARD_H 60
#define DOCK_Y     196

void drawHome(LGFX_Sprite& s,float sc){
  s.fillSprite(T().bg);
  drawStatusBar(s);

  // Jam besar
  struct tm t; bool ok=ntpSynced&&getLocalTime(&t);
  s.setTextColor(T().text);s.setTextSize(3);
  char tb[6];
  if(ok)sprintf(tb,"%02d:%02d",t.tm_hour,t.tm_min);else strcpy(tb,"--:--");
  int tw=strlen(tb)*18;
  s.setCursor(160-tw/2,26);s.print(tb);

  // Grid 3x2
  for(int i=0;i<6;i++){
    int col=i%3, row=i/3;
    int x=6+col*105, y=72+row*66-(int)sc;
    if(y+APP_CARD_H<24||y>DOCK_Y-4)continue;
    s.fillRoundRect(x,y,APP_CARD_W,APP_CARD_H,10,T().surface);
    s.fillCircle(x+APP_CARD_W/2,y+18,14,apps[i].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[i].sym,0};
    s.setCursor(x+APP_CARD_W/2-6,y+11);s.print(sym);
    s.setTextColor(T().text);s.setTextSize(1);
    int nl=strlen(apps[i].name)*6;
    s.setCursor(x+APP_CARD_W/2-nl/2,y+APP_CARD_H-12);
    s.print(apps[i].name);
  }

  // Dock
  s.fillRoundRect(6,DOCK_Y,308,38,12,T().surface2);
  int dockIdx[4]={0,4,5,3};
  for(int i=0;i<4;i++){
    int di=dockIdx[i];
    int cx=6+i*76+38;
    s.fillCircle(cx,DOCK_Y+19,15,apps[di].color);
    s.setTextColor(T().bg);s.setTextSize(2);
    char sym[2]={apps[di].sym,0};
    s.setCursor(cx-6,DOCK_Y+12);s.print(sym);
  }
  drawToast(s);
}

Screen homeCheck(int x,int y,float sc){
  if(y>=DOCK_Y&&y<=DOCK_Y+38){
    int dockIdx[4]={0,4,5,3};
    Screen dockScr[4]={SCR_CLOCK,SCR_NOTEPAD,SCR_CANVAS,SCR_SETTINGS};
    for(int i=0;i<4;i++){int cx=6+i*76+38;if(abs(x-cx)<18)return dockScr[i];}
  }
  Screen scs[6]={SCR_CLOCK,SCR_CALC,SCR_SENSOR,SCR_SETTINGS,SCR_NOTEPAD,SCR_CANVAS};
  for(int i=0;i<6;i++){
    int col=i%3,row=i/3;
    int ax=6+col*105,ay=72+row*66-(int)sc;
    if(x>=ax&&x<=ax+APP_CARD_W&&y>=ay&&y<=ay+APP_CARD_H)return scs[i];
  }
  return SCR_HOME;
}

// =============================================
// APP: JAM
// =============================================
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
    s.drawFastHLine(20,114,280,T().accent);
    s.setTextColor(T().good);s.setCursor(20,120);s.print("NTP Sync OK");
  } else {
    s.setTextColor(T().danger);s.setTextSize(2);s.setCursor(20,80);s.print("Tidak sync");
  }
  drawBack(s);drawToast(s);
}

// =============================================
// APP: KALKULATOR
// =============================================
String calcInput="0";float calcA=0;char calcOp=0;bool calcNew=true;
struct CBtn{int x,y,w,h;const char* l;bool accent,op;};
CBtn cBtns[]={
  {4,  52, 72,34,"C",   false,false},{80, 52, 72,34,"+/-",false,false},
  {156,52, 72,34,"%",   false,false},{232,52, 84,34,"/",  false,true},
  {4,  90, 72,34,"7",   false,false},{80, 90, 72,34,"8",  false,false},
  {156,90, 72,34,"9",   false,false},{232,90, 84,34,"x",  false,true},
  {4,  128,72,34,"4",   false,false},{80, 128,72,34,"5",  false,false},
  {156,128,72,34,"6",   false,false},{232,128,84,34,"-",  false,true},
  {4,  166,72,34,"1",   false,false},{80, 166,72,34,"2",  false,false},
  {156,166,72,34,"3",   false,false},{232,166,84,34,"+",  false,true},
  {4,  204,148,34,"0",  false,false},{156,204,72,34,".",  false,false},
  {232,204,84,34,"=",   true, false},
};
void drawCalc(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,28);s.print("Kalkulator");
  s.fillRoundRect(4,34,312,16,4,T().surface);
  s.setTextColor(T().text);s.setTextSize(2);
  int tw2=calcInput.length()*12;s.setCursor(max(8,308-tw2),36);s.print(calcInput);
  for(auto& b:cBtns){
    uint16_t bg=b.accent?T().accent2:(b.op?T().accent:T().surface);
    uint16_t fg=(b.op||b.accent)?T().bg:T().text;
    s.fillRoundRect(b.x,b.y,b.w,b.h,6,bg);
    s.setTextColor(fg);s.setTextSize(2);
    int lw=strlen(b.l)*12;s.setCursor(b.x+b.w/2-lw/2,b.y+b.h/2-8);s.print(b.l);
  }
  drawToast(s);
}
void calcTouch(int x,int y){
  for(auto& b:cBtns){
    if(x<b.x||x>b.x+b.w||y<b.y||y>b.y+b.h)continue;
    const char* l=b.l;
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
    return;
  }
}

// =============================================
// APP: SENSOR
// =============================================
void drawSensor(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent2);s.setTextSize(1);s.setCursor(8,28);s.print("Sensor");
  float temp=temperatureRead();
  s.fillRoundRect(20,42,280,100,14,T().surface);
  s.setTextColor(T().subtext);s.setTextSize(1);s.setCursor(35,56);s.print("Suhu Internal Chip");
  char buf[8];sprintf(buf,"%.1f",temp);
  s.setTextColor(T().accent2);s.setTextSize(4);s.setCursor(40,70);s.print(buf);
  s.setTextSize(2);s.print(" C");
  s.fillRoundRect(20,148,280,18,6,T().divider);
  int bw=constrain(map((int)temp,20,90,0,276),0,276);
  uint16_t bc=(temp<50)?T().good:(temp<70)?T().accent:T().danger;
  s.fillRoundRect(22,150,bw,14,4,bc);
  s.setTextColor(T().subtext);s.setTextSize(1);
  s.setCursor(20,172);s.print("20C            55C            90C");
  drawBack(s);drawToast(s);
}

// =============================================
// APP: SETTINGS
// =============================================
String settSSID="",settPass="";
bool settShowPass=false;int settFocus=-1;

void drawSettings(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,28);s.print("Pengaturan");

  // Kecerahan
  s.fillRoundRect(8,36,304,32,8,T().surface);
  s.setTextColor(T().text);s.setCursor(18,44);s.print("Kecerahan");
  s.fillRoundRect(18,56,220,8,4,T().divider);
  s.fillRoundRect(19,57,map(brightness,0,255,0,218),6,3,T().accent);
  char bb[6];sprintf(bb,"%d%%",brightness*100/255);
  s.setTextColor(T().subtext);s.setCursor(248,56);s.print(bb);

  // Tema (tombol kecil)
  s.fillRoundRect(8,74,304,28,8,T().surface);
  s.setTextColor(T().text);s.setCursor(18,82);s.print("Tema:");
  for(int i=0;i<THEME_COUNT;i++){
    uint16_t bg=(i==themeIdx)?T().accent:T().surface2;
    uint16_t fg=(i==themeIdx)?T().bg:T().text;
    s.fillRoundRect(80+i*56,76,52,24,6,bg);
    s.setTextColor(fg);s.setTextSize(1);
    int nl=strlen(themes[i].name)*6;
    s.setCursor(80+i*56+26-nl/2,82);s.print(themes[i].name);
  }

  // SSID
  s.fillRoundRect(8,108,304,26,6,settFocus==0?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(18,116);s.print("SSID:");
  s.setTextColor(T().text);s.setCursor(64,116);
  String sd2=settSSID.length()?settSSID:"(ketuk)";
  if(sd2.length()>22)sd2=sd2.substring(0,22)+"..";
  s.print(sd2.c_str());

  // Pass
  s.fillRoundRect(8,140,268,26,6,settFocus==1?T().surface2:T().surface);
  s.setTextColor(T().subtext);s.setCursor(18,148);s.print("Pass:");
  s.setTextColor(T().text);s.setCursor(64,148);
  if(settPass.length()){
    if(settShowPass)s.print(settPass.substring(0,18).c_str());
    else for(int i=0;i<(int)settPass.length()&&i<18;i++)s.print("*");
  } else s.print("(ketuk)");
  s.fillRoundRect(280,140,32,26,4,T().surface2);
  s.setTextColor(T().accent);s.setCursor(287,148);s.print(settShowPass?"H":"S");

  // Connect
  s.fillRoundRect(8,172,140,26,8,T().accent);
  s.setTextColor(T().bg);s.setCursor(28,180);s.print("Sambungkan");
  s.setTextColor(wifiConnected?T().good:T().danger);
  String ws=wifiConnected?String(WiFi.SSID()):"Tdk terhubung";
  s.setCursor(156,180);s.print(ws.substring(0,14).c_str());

  // Kalibrasi
  s.fillRoundRect(8,204,140,26,8,T().surface2);
  s.setTextColor(T().text);s.setCursor(18,212);s.print("Kalibrasi Ulang");

  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}

void settingsTouch(int x,int y){
  if(kbVisible){
    if(y<KB_Y-2){kbVisible=false;kbTarget=nullptr;settFocus=-1;}
    else kbTouch(x,y);
    return;
  }
  if(x>=18&&x<=238&&y>=36&&y<=68){
    brightness=constrain(map(x-18,0,220,0,255),10,255);
    display.setBrightness(brightness);return;
  }
  for(int i=0;i<THEME_COUNT;i++){
    if(x>=80+i*56&&x<=132+i*56&&y>=74&&y<=102){
      themeIdx=i;saveTheme();initAppColors();
      drawColor=T().text;
      showToast("Tema diganti");needRedraw=true;return;
    }
  }
  if(x>=8&&x<=312&&y>=108&&y<=134){settFocus=0;kbTarget=&settSSID;kbVisible=true;kbMode=KB_LOWER;return;}
  if(x>=8&&x<=280&&y>=140&&y<=166){settFocus=1;kbTarget=&settPass;kbVisible=true;kbMode=KB_LOWER;return;}
  if(x>=280&&y>=140&&y<=166){settShowPass=!settShowPass;return;}
  if(x>=8&&x<=148&&y>=172&&y<=198){
    settSSID.toCharArray(WIFI_SSID,64);
    settPass.toCharArray(WIFI_PASSWORD,64);
    saveWifiCreds();connectWifi();
    showToast(wifiConnected?"WiFi OK":"Gagal connect");return;
  }
  if(x>=8&&x<=148&&y>=204&&y<=230){
    Preferences p;p.begin("touch_cal",false);p.putBool("done",false);p.end();ESP.restart();
  }
}

// =============================================
// APP: NOTEPAD
// =============================================
void drawNotepad(LGFX_Sprite& s){
  s.fillSprite(T().bg);drawStatusBar(s);
  s.setTextColor(T().accent);s.setTextSize(1);s.setCursor(8,28);s.print("Notepad");
  s.fillRoundRect(240,24,76,18,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(256,29);s.print("Hapus");
  int bot=kbVisible?KB_Y-4:208;
  s.fillRoundRect(4,44,312,bot-44,6,T().surface);
  s.setTextColor(T().text);s.setTextSize(1);s.setTextWrap(true);
  s.setCursor(10,50);s.print((noteText+"|").c_str());
  if(kbVisible)drawKb(s);else drawBack(s);
  drawToast(s);
}
void notepadTouch(int x,int y){
  if(kbVisible){
    if(y<KB_Y-2){kbVisible=false;kbTarget=nullptr;saveNote();}
    else kbTouch(x,y);
    return;
  }
  if(x>=240&&x<=316&&y>=24&&y<=42){noteText="";saveNote();showToast("Dihapus");return;}
  if(y>=44&&y<=208){kbTarget=&noteText;kbVisible=true;kbMode=KB_LOWER;}
}

// =============================================
// APP: CANVAS
// =============================================
#define CAP_Y  24
#define CAP_H  190
#define TOOL_Y 214
uint16_t palette[]={0xFFFF,0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F,0xFD40,0x0000};
#define PAL_N 9

void drawCanvasScreen(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setTextColor(T().good);s.setTextSize(1);s.setCursor(8,14);s.print("Canvas");
  canvasApp.pushSprite(&s,0,CAP_Y);
  s.fillRect(0,TOOL_Y,320,240-TOOL_Y,T().surface);
  s.drawFastHLine(0,TOOL_Y,320,T().divider);
  for(int i=0;i<PAL_N;i++){
    int px=4+i*24;
    s.fillCircle(px+10,TOOL_Y+12,9,palette[i]);
    if(palette[i]==drawColor)s.drawCircle(px+10,TOOL_Y+12,11,T().text);
  }
  s.fillRoundRect(224,TOOL_Y+2,42,20,4,T().surface2);
  s.setTextColor(T().text);char bs[6];sprintf(bs,"B:%d",brushSize);
  s.setCursor(228,TOOL_Y+8);s.print(bs);
  s.fillRoundRect(270,TOOL_Y+2,46,20,4,T().danger);
  s.setTextColor(0xFFFF);s.setCursor(278,TOOL_Y+8);s.print("CLR");
  s.fillRoundRect(BACK_X,BACK_Y,BACK_W,BACK_H,6,T().surface2);
  s.setTextColor(T().accent);s.setCursor(BACK_X+10,BACK_Y+8);s.print("< Back");
  drawToast(s);
}

void canvasTouch(int x,int y,bool held){
  if(y>=TOOL_Y){
    if(!held){
      for(int i=0;i<PAL_N;i++){int px=4+i*24;if(x>=px&&x<=px+20){drawColor=palette[i];lastDX=-1;return;}}
      if(x>=224&&x<=266){brushSize=(brushSize%8)+1;lastDX=-1;return;}
      if(x>=270){canvasApp.fillSprite(T().bg);lastDX=-1;saveCanvas();showToast("Dibersihkan");return;}
    }
    return;
  }
  if(y>=BACK_Y&&x>=BACK_X&&x<=BACK_X+BACK_W)return;
  if(y>=CAP_Y&&y<CAP_Y+CAP_H){
    int cy=y-CAP_Y;
    if(held&&lastDX>=0){
      canvasApp.drawLine(lastDX,lastDY,x,cy,drawColor);
      for(int t2=1;t2<brushSize;t2++){
        canvasApp.drawLine(lastDX,lastDY+t2,x,cy+t2,drawColor);
        canvasApp.drawLine(lastDX+t2,lastDY,x+t2,cy,drawColor);
      }
    } else canvasApp.fillCircle(x,cy,brushSize,drawColor);
    lastDX=x;lastDY=cy;
  }
}

// =============================================
// PUSH FRAME
// =============================================
void push(){canvas.pushSprite(0,0);}

// =============================================
// SETUP
// =============================================
void setup(){
  Serial.begin(115200);
  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);

  canvas.setPsram(true);
  canvas.createSprite(320,240);

  // Splash
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
  loadNote();
  loadWifiCreds();
  settSSID=String(WIFI_SSID);
  settPass=String(WIFI_PASSWORD);
  connectWifi();

  canvasApp.setPsram(true);
  canvasApp.createSprite(CANVAS_W,CANVAS_H_APP);
  canvasApp.fillSprite(T().bg);
  loadCanvas();
  canvasReady=true;

  drawHome(canvas,0);push();
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

  // ============ HOME ============
  if(currentScreen==SCR_HOME){
    if(touched){
      if(!wasTouched){
        touchStartX=tx;touchStartY=ty;touchLastY=ty;
        isSwiping=false;swipeStartTime=millis();
      } else {
        if(abs(ty-touchStartY)>12)isSwiping=true;
        if(isSwiping){
          homeScrollVel=(touchLastY-ty)*0.8f;
          homeScrollY+=touchLastY-ty;
          homeScrollY=constrain(homeScrollY,0.0f,80.0f);
          needRedraw=true;
        }
        touchLastY=ty;
      }
    } else if(wasTouched){
      if(!isSwiping&&millis()-swipeStartTime<400){
        Screen nx=homeCheck(touchStartX,touchStartY,homeScrollY);
        if(nx!=SCR_HOME){
          if(nx==SCR_CANVAS){lastDX=-1;}
          currentScreen=nx;needRedraw=true;
        }
      }
    } else {
      if(fabsf(homeScrollVel)>0.5f){
        homeScrollY+=homeScrollVel;homeScrollVel*=0.85f;
        homeScrollY=constrain(homeScrollY,0.0f,80.0f);
        needRedraw=true;
      }
    }
    if(needRedraw){drawHome(canvas,homeScrollY);push();needRedraw=false;}

  // ============ APP SCREENS ============
  } else {
    // Canvas: gambar terus saat hold
    if(currentScreen==SCR_CANVAS&&touched){
      if(newT&&ty>=BACK_Y&&tx>=BACK_X&&tx<=BACK_X+BACK_W){
        saveCanvas();goHome();goto done;
      }
      canvasTouch(tx,ty,wasTouched);
      needRedraw=true;
    }

    if(newT&&currentScreen!=SCR_CANVAS){
      if(isBack(tx,ty)){
        if(currentScreen==SCR_NOTEPAD)saveNote();
        goHome();goto done;
      }
      switch(currentScreen){
        case SCR_CALC:     calcTouch(tx,ty);     needRedraw=true;break;
        case SCR_SENSOR:                          needRedraw=true;break;
        case SCR_SETTINGS: settingsTouch(tx,ty); needRedraw=true;break;
        case SCR_NOTEPAD:  notepadTouch(tx,ty);  needRedraw=true;break;
        default:break;
      }
    }

    // Keyboard hold repeat: redraw tiap touch baru
    if(touched&&newT&&kbVisible) needRedraw=true;

    // Auto refresh
    if(currentScreen==SCR_CLOCK&&millis()-lastClk>1000){lastClk=millis();needRedraw=true;}
    if(currentScreen==SCR_SENSOR&&millis()-lastSen>2000){lastSen=millis();needRedraw=true;}
    if(toastMsg.length()&&millis()>toastUntil){toastMsg="";needRedraw=true;}

    if(needRedraw){
      switch(currentScreen){
        case SCR_CLOCK:    drawClock(canvas);        break;
        case SCR_CALC:     drawCalc(canvas);         break;
        case SCR_SENSOR:   drawSensor(canvas);       break;
        case SCR_SETTINGS: drawSettings(canvas);     break;
        case SCR_NOTEPAD:  drawNotepad(canvas);      break;
        case SCR_CANVAS:   drawCanvasScreen(canvas); break;
        default:break;
      }
      push();needRedraw=false;
    }
  }

  done:
  wasTouched=touched;
  delay(8);
}



