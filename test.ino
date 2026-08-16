#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

// =============================================
// WIFI CONFIG (bisa diubah via Setting)
// =============================================
char WIFI_SSID[64]     = "";
char WIFI_PASSWORD[64] = "";
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 7 * 3600;
const int   DST_OFFSET = 0;

// =============================================
// WARNA
// =============================================
#define COL_BG        0x1084
#define COL_SURFACE   0x2104
#define COL_SURFACE2  0x3186
#define COL_ACCENT    0xFD40
#define COL_ACCENT2   0x867F
#define COL_TEXT      0xFFFF
#define COL_SUBTEXT   0x8C51
#define COL_DIVIDER   0x2965
#define COL_GREEN     0x07E0
#define COL_RED       0xF800
#define COL_CYAN      0x07FF
#define COL_MAGENTA   0xF81F
#define COL_YELLOW    0xFFE0

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
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 12;
      cfg.pin_mosi    = 11;
      cfg.pin_miso    = 13;
      cfg.pin_dc      = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 10;
      cfg.pin_rst          = 14;
      cfg.pin_busy         = -1;
      cfg.memory_width     = 240;
      cfg.memory_height    = 320;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 21;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    {
      auto cfg = _touch_instance.config();
      cfg.pin_int         = -1;
      cfg.bus_shared      = false;
      cfg.offset_rotation = 0;
      cfg.spi_host        = SPI3_HOST;
      cfg.freq            = 2000000;
      cfg.pin_sclk        = 6;
      cfg.pin_mosi        = 5;
      cfg.pin_miso        = 4;
      cfg.pin_cs          = 9;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX display;
LGFX_Sprite canvas(&display);   // Sprite utama (framebuffer)
LGFX_Sprite canvasApp(&display); // Sprite khusus app Canvas

// =============================================
// STATE GLOBAL
// =============================================
enum Screen {
  SCR_HOME,
  SCR_CLOCK,
  SCR_CALC,
  SCR_SENSOR,
  SCR_SETTINGS,
  SCR_NOTEPAD,
  SCR_CANVAS
};
Screen currentScreen = SCR_HOME;

bool wifiConnected = false;
bool ntpSynced     = false;
int  brightness    = 200;

// Home scroll
float homeScrollY    = 0;
float homeScrollVel  = 0;
int   touchStartY    = 0;
int   touchLastY     = 0;
bool  isSwiping      = false;
unsigned long swipeStartTime = 0;

// =============================================
// KEYBOARD VIRTUAL
// =============================================
enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };
KbMode kbMode = KB_LOWER;
bool kbVisible = false;
String* kbTarget = nullptr;

const char* kbRows[3][3][10] = {
  { // Lower
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l",";"},
    {"z","x","c","v","b","n","m",",",".","?"}
  },
  { // Upper
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L",":"},
    {"Z","X","C","V","B","N","M","!","@","#"}
  },
  { // Num
    {"1","2","3","4","5","6","7","8","9","0"},
    {"-","=","[","]","\\",";","'",",",".","/"},
    {"~","!","@","#","$","%","^","&","*","("}
  }
};

// Keyboard area: y dari 148 ke 240 (portrait 240px tinggi)
#define KB_Y       148
#define KB_H       92
#define KEY_W      23
#define KEY_H      20

void drawKeyboard(LGFX_Sprite& spr) {
  spr.fillRect(0, KB_Y - 4, 320, KB_H + 8, COL_SURFACE);
  spr.drawFastHLine(0, KB_Y - 4, 320, COL_DIVIDER);

  int row0 = KB_Y;
  int row1 = KB_Y + 24;
  int row2 = KB_Y + 48;
  int row3 = KB_Y + 72; // baris spasi dll

  // Baris 0-2
  for (int r = 0; r < 3; r++) {
    int rowY = KB_Y + r * 24;
    int offsetX = (r == 1) ? 4 : (r == 2) ? 8 : 0;
    for (int c = 0; c < 10; c++) {
      int kx = offsetX + c * (KEY_W + 1);
      int ky = rowY;
      spr.fillRoundRect(kx, ky, KEY_W, KEY_H, 3, COL_SURFACE2);
      spr.setTextColor(COL_TEXT);
      spr.setTextSize(1);
      spr.setCursor(kx + 7, ky + 6);
      spr.print(kbRows[(int)kbMode][r][c]);
    }
  }

  // Baris kontrol: Shift | Space | Backspace | 123/ABC
  // Shift
  spr.fillRoundRect(0, row3, 40, KEY_H, 3, kbMode == KB_UPPER ? COL_ACCENT : COL_SURFACE2);
  spr.setTextColor(COL_TEXT); spr.setTextSize(1);
  spr.setCursor(6, row3 + 6); spr.print("SHF");

  // 123/ABC
  spr.fillRoundRect(44, row3, 40, KEY_H, 3, kbMode == KB_NUM ? COL_ACCENT : COL_SURFACE2);
  spr.setCursor(50, row3 + 6);
  spr.print(kbMode == KB_NUM ? "ABC" : "123");

  // Space
  spr.fillRoundRect(88, row3, 144, KEY_H, 3, COL_SURFACE2);
  spr.setCursor(140, row3 + 6); spr.print("SPACE");

  // Backspace
  spr.fillRoundRect(236, row3, 84, KEY_H, 3, COL_RED);
  spr.setCursor(248, row3 + 6); spr.print("<--");
}

void kbHandleTouch(int x, int y) {
  if (!kbVisible || kbTarget == nullptr) return;
  if (y < KB_Y - 4) return;

  int row3 = KB_Y + 72;

  // Baris kontrol
  if (y >= row3 && y <= row3 + KEY_H) {
    if (x <= 40) {
      kbMode = (kbMode == KB_UPPER) ? KB_LOWER : KB_UPPER;
    } else if (x >= 44 && x <= 84) {
      kbMode = (kbMode == KB_NUM) ? KB_LOWER : KB_NUM;
    } else if (x >= 88 && x <= 232) {
      *kbTarget += " ";
    } else if (x >= 236) {
      if (kbTarget->length() > 0)
        *kbTarget = kbTarget->substring(0, kbTarget->length() - 1);
    }
    return;
  }

  // Baris huruf
  for (int r = 0; r < 3; r++) {
    int rowY = KB_Y + r * 24;
    if (y >= rowY && y <= rowY + KEY_H) {
      int offsetX = (r == 1) ? 4 : (r == 2) ? 8 : 0;
      for (int c = 0; c < 10; c++) {
        int kx = offsetX + c * (KEY_W + 1);
        if (x >= kx && x <= kx + KEY_W) {
          *kbTarget += kbRows[(int)kbMode][r][c];
          if (kbMode == KB_UPPER) kbMode = KB_LOWER;
          return;
        }
      }
    }
  }
}

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration() {
  Preferences prefs;
  prefs.begin("touch_cal", false);
  bool done = prefs.getBool("done", false);
  if (done) {
    uint16_t calData[8];
    prefs.getBytes("data", calData, sizeof(calData));
    display.setTouchCalibrate(calData);
  } else {
    display.fillScreen(COL_BG);
    display.setTextColor(COL_TEXT);
    display.setTextSize(2);
    display.setCursor(20, display.height() / 2 - 30);
    display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20, display.height() / 2 + 5);
    display.println("Sentuh tanda di setiap sudut");
    uint16_t calData[8];
    display.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
    prefs.putBytes("data", calData, sizeof(calData));
    prefs.putBool("done", true);
  }
  prefs.end();
}

// =============================================
// WIFI & NTP
// =============================================
void connectWifi() {
  if (strlen(WIFI_SSID) == 0) return;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(300); tries++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) ntpSynced = true;
  }
}

void loadWifiCreds() {
  Preferences prefs;
  prefs.begin("wifi", true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  s.toCharArray(WIFI_SSID, 64);
  p.toCharArray(WIFI_PASSWORD, 64);
}

void saveWifiCreds() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", WIFI_SSID);
  prefs.putString("pass", WIFI_PASSWORD);
  prefs.end();
}

// =============================================
// STATUS BAR
// =============================================
void drawStatusBar(LGFX_Sprite& spr) {
  spr.fillRect(0, 0, 320, 24, COL_SURFACE);
  spr.drawFastHLine(0, 24, 320, COL_DIVIDER);

  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    char buf[9];
    sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    spr.setTextColor(COL_TEXT);
    spr.setTextSize(1);
    spr.setCursor(8, 8);
    spr.print(buf);
  } else {
    spr.setTextColor(COL_SUBTEXT);
    spr.setTextSize(1);
    spr.setCursor(8, 8);
    spr.print("--:--");
  }

  // WiFi icon
  if (wifiConnected) {
    spr.fillCircle(302, 17, 2, COL_GREEN);
    spr.drawArc(302, 19, 5, 4, 210, 330, COL_GREEN);
    spr.drawArc(302, 19, 9, 8, 210, 330, COL_GREEN);
  } else {
    spr.fillCircle(302, 17, 2, COL_RED);
    spr.drawLine(298, 13, 306, 21, COL_RED);
  }
}

void drawBackButton(LGFX_Sprite& spr) {
  spr.fillRoundRect(4, 210, 60, 26, 6, COL_SURFACE);
  spr.setTextColor(COL_ACCENT);
  spr.setTextSize(1);
  spr.setCursor(14, 220);
  spr.print("< Back");
}

bool backButtonPressed(int x, int y) {
  return (x >= 4 && x <= 64 && y >= 210 && y <= 236);
}

// =============================================
// HOME SCREEN (Scrollable, 6 App)
// =============================================
struct AppDef { const char* name; uint16_t color; char sym; };
AppDef appDefs[6] = {
  { "Jam",       COL_ACCENT,   'T' },
  { "Kalkulator",COL_ACCENT2,  '+' },
  { "Sensor",    COL_CYAN,     '~' },
  { "Setting",   COL_MAGENTA,  '@' },
  { "Notepad",   COL_YELLOW,   'N' },
  { "Canvas",    COL_GREEN,    'C' },
};

// Total tinggi konten home: 3 baris * 100px + padding
#define HOME_CONTENT_H  340
#define HOME_CARD_H     95
#define HOME_CARD_W     140

void drawHomeContent(LGFX_Sprite& spr, int scrollY) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);

  spr.setTextColor(COL_SUBTEXT);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Beranda");

  for (int i = 0; i < 6; i++) {
    int col = i % 2;
    int row = i / 2;
    int x   = 8 + col * 152;
    int y   = 45 + row * (HOME_CARD_H + 8) - (int)scrollY;

    // Clip — jangan gambar kalau diluar layar
    if (y + HOME_CARD_H < 0 || y > 240) continue;

    spr.fillRoundRect(x, y, HOME_CARD_W, HOME_CARD_H, 12, COL_SURFACE);

    // Icon
    spr.fillRoundRect(x + 48, y + 10, 44, 38, 8, appDefs[i].color);
    spr.setTextSize(3);
    spr.setTextColor(COL_BG);
    spr.setCursor(x + 59, y + 18);
    spr.print(appDefs[i].sym);

    // Nama
    spr.setTextSize(1);
    spr.setTextColor(COL_TEXT);
    int nameLen = strlen(appDefs[i].name);
    spr.setCursor(x + HOME_CARD_W / 2 - nameLen * 3, y + 72);
    spr.print(appDefs[i].name);
  }
}

Screen homeTouchCheck(int x, int y, int scrollY) {
  Screen apps[6] = { SCR_CLOCK, SCR_CALC, SCR_SENSOR, SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS };
  for (int i = 0; i < 6; i++) {
    int col = i % 2;
    int row = i / 2;
    int ax  = 8 + col * 152;
    int ay  = 45 + row * (HOME_CARD_H + 8) - (int)scrollY;
    if (x >= ax && x <= ax + HOME_CARD_W && y >= ay && y <= ay + HOME_CARD_H)
      return apps[i];
  }
  return SCR_HOME;
}

// =============================================
// APP: JAM
// =============================================
void drawClock(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_ACCENT);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Jam");

  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    spr.setTextColor(COL_TEXT);
    spr.setTextSize(3);
    spr.setCursor(20, 65);
    spr.print(timeBuf);

    const char* days[]   = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* months[] = {"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char dateBuf[24];
    sprintf(dateBuf, "%s, %02d %s %04d", days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year+1900);
    spr.setTextColor(COL_SUBTEXT);
    spr.setTextSize(1);
    spr.setCursor(20, 108);
    spr.print(dateBuf);
    spr.drawFastHLine(20, 122, 280, COL_ACCENT);
    spr.setTextColor(COL_GREEN);
    spr.setCursor(20, 130);
    spr.print(ntpSynced ? "NTP Sync OK" : "Tidak sync");
  } else {
    spr.setTextColor(COL_RED);
    spr.setTextSize(2);
    spr.setCursor(20, 80);
    spr.print("Tidak sync");
  }
  drawBackButton(spr);
}

// =============================================
// APP: KALKULATOR
// =============================================
String calcInput   = "0";
float  calcA       = 0;
char   calcOp      = 0;
bool   calcNewNum  = true;

void drawCalcBtn(LGFX_Sprite& spr, int x, int y, int w, int h, const char* lbl, uint16_t bg, uint16_t fg) {
  spr.fillRoundRect(x, y, w, h, 6, bg);
  spr.setTextColor(fg);
  spr.setTextSize(2);
  spr.setCursor(x + w/2 - strlen(lbl)*6, y + h/2 - 8);
  spr.print(lbl);
}

void drawCalc(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_ACCENT);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Kalkulator");

  spr.fillRoundRect(4, 40, 312, 40, 6, COL_SURFACE);
  spr.setTextColor(COL_TEXT);
  spr.setTextSize(2);
  int tw = calcInput.length() * 12;
  spr.setCursor(max(8, 310 - tw), 52);
  spr.print(calcInput);

  drawCalcBtn(spr, 4,   88, 72, 34, "C",   COL_SURFACE, COL_ACCENT);
  drawCalcBtn(spr, 82,  88, 72, 34, "+/-", COL_SURFACE, COL_ACCENT);
  drawCalcBtn(spr, 160, 88, 72, 34, "%",   COL_SURFACE, COL_ACCENT);
  drawCalcBtn(spr, 238, 88, 78, 34, "/",   COL_ACCENT,  COL_BG);

  drawCalcBtn(spr, 4,   128, 72, 34, "7", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 82,  128, 72, 34, "8", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 160, 128, 72, 34, "9", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 238, 128, 78, 34, "x", COL_ACCENT,  COL_BG);

  drawCalcBtn(spr, 4,   168, 72, 34, "4", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 82,  168, 72, 34, "5", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 160, 168, 72, 34, "6", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 238, 168, 78, 34, "-", COL_ACCENT,  COL_BG);

  drawCalcBtn(spr, 4,   208, 72, 34, "1", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 82,  208, 72, 34, "2", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 160, 208, 72, 34, "3", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 238, 208, 78, 34, "+", COL_ACCENT,  COL_BG);

  drawCalcBtn(spr, 4,   248, 150, 34, "0", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 160, 248, 72,  34, ".", COL_SURFACE, COL_TEXT);
  drawCalcBtn(spr, 238, 248, 78,  34, "=", COL_ACCENT2, COL_BG);
}

void calcHandleTouch(int x, int y) {
  struct CB { int x,y,w,h; const char* l; };
  CB b[] = {
    {4,88,72,34,"C"},{82,88,72,34,"+/-"},{160,88,72,34,"%"},{238,88,78,34,"/"},
    {4,128,72,34,"7"},{82,128,72,34,"8"},{160,128,72,34,"9"},{238,128,78,34,"x"},
    {4,168,72,34,"4"},{82,168,72,34,"5"},{160,168,72,34,"6"},{238,168,78,34,"-"},
    {4,208,72,34,"1"},{82,208,72,34,"2"},{160,208,72,34,"3"},{238,208,78,34,"+"},
    {4,248,150,34,"0"},{160,248,72,34,"."},{238,248,78,34,"="},
  };
  for (auto& btn : b) {
    if (x>=btn.x && x<=btn.x+btn.w && y>=btn.y && y<=btn.y+btn.h) {
      const char* l = btn.l;
      if (!strcmp(l,"C"))       { calcInput="0"; calcA=0; calcOp=0; calcNewNum=true; }
      else if (!strcmp(l,"+/-")){ float v=calcInput.toFloat()*-1; calcInput=String(v); }
      else if (!strcmp(l,"%"))  { float v=calcInput.toFloat()/100; calcInput=String(v); }
      else if (!strcmp(l,"="))  {
        float b2=calcInput.toFloat(), res=0;
        if(calcOp=='+') res=calcA+b2;
        else if(calcOp=='-') res=calcA-b2;
        else if(calcOp=='x') res=calcA*b2;
        else if(calcOp=='/') res=(b2!=0)?calcA/b2:0;
        calcInput = (res==(int)res) ? String((int)res) : String(res);
        calcOp=0; calcNewNum=true;
      }
      else if(!strcmp(l,"+")||!strcmp(l,"-")||!strcmp(l,"x")||!strcmp(l,"/")) {
        calcA=calcInput.toFloat(); calcOp=l[0]; calcNewNum=true;
      } else {
        if(calcNewNum){calcInput="";calcNewNum=false;}
        if(!strcmp(l,".")&&calcInput.indexOf('.')>=0) return;
        if(calcInput=="0"&&strcmp(l,".")!=0) calcInput="";
        calcInput+=l;
      }
      if(calcInput.length()>12) calcInput=calcInput.substring(0,12);
      break;
    }
  }
}

// =============================================
// APP: SENSOR
// =============================================
#ifdef __cplusplus
extern "C" { uint8_t temprature_sens_read(); }
#endif
float readInternalTemp() { return (temprature_sens_read()-32)/1.8f; }

void drawSensor(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_CYAN);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Sensor");

  float temp = readInternalTemp();
  spr.fillRoundRect(20, 48, 280, 100, 14, COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT);
  spr.setTextSize(1);
  spr.setCursor(35, 62);
  spr.print("Suhu Internal Chip");

  char buf[16]; sprintf(buf, "%.1f", temp);
  spr.setTextColor(COL_CYAN);
  spr.setTextSize(4);
  spr.setCursor(40, 80);
  spr.print(buf);
  spr.setTextSize(2);
  spr.print(" C");

  spr.fillRoundRect(20, 162, 280, 18, 6, COL_DIVIDER);
  int bw = constrain(map((int)temp, 20, 90, 0, 276), 0, 276);
  uint16_t bc = (temp<50)?COL_GREEN:(temp<70)?COL_ACCENT:COL_RED;
  spr.fillRoundRect(22, 164, bw, 14, 4, bc);

  spr.setTextColor(COL_SUBTEXT);
  spr.setTextSize(1);
  spr.setCursor(20, 185);
  spr.print("Normal <50C  Hangat <70C  Panas >70C");
  spr.setTextColor(COL_DIVIDER);
  spr.setCursor(90, 200);
  spr.print("Tap untuk refresh");

  drawBackButton(spr);
}

// =============================================
// APP: SETTINGS
// =============================================
String settWifiSSID = "";
String settWifiPass = "";
bool   settShowPass  = false;
int    settFocusField = -1; // 0=ssid, 1=pass

void drawSettings(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_MAGENTA);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Pengaturan");

  // Brightness
  spr.fillRoundRect(8, 42, 304, 36, 8, COL_SURFACE);
  spr.setTextColor(COL_TEXT);
  spr.setCursor(18, 50);
  spr.print("Kecerahan");
  spr.fillRoundRect(18, 62, 220, 10, 4, COL_DIVIDER);
  spr.fillRoundRect(19, 63, map(brightness,0,255,0,218), 8, 3, COL_ACCENT);
  char bb[6]; sprintf(bb, "%d%%", brightness*100/255);
  spr.setTextColor(COL_SUBTEXT);
  spr.setCursor(248, 62); spr.print(bb);

  // WiFi SSID field
  spr.fillRoundRect(8, 84, 304, 28, 6, settFocusField==0 ? COL_SURFACE2 : COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT); spr.setCursor(18, 88); spr.print("SSID:");
  spr.setTextColor(COL_TEXT);    spr.setCursor(60, 88);
  spr.print(settWifiSSID.length()>0 ? settWifiSSID.c_str() : "(ketuk untuk isi)");

  // WiFi Pass field
  spr.fillRoundRect(8, 118, 304, 28, 6, settFocusField==1 ? COL_SURFACE2 : COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT); spr.setCursor(18, 122); spr.print("Pass:");
  spr.setTextColor(COL_TEXT);    spr.setCursor(60, 122);
  if (settWifiPass.length() > 0) {
    if (settShowPass) spr.print(settWifiPass.c_str());
    else { for(int i=0;i<(int)settWifiPass.length()&&i<20;i++) spr.print("*"); }
  } else spr.print("(ketuk untuk isi)");

  // Show/hide pass
  spr.fillRoundRect(280, 118, 28, 28, 4, COL_SURFACE2);
  spr.setTextColor(COL_ACCENT); spr.setCursor(286, 126);
  spr.print(settShowPass ? "H" : "S");

  // Tombol Connect
  spr.fillRoundRect(8, 152, 144, 28, 8, COL_ACCENT);
  spr.setTextColor(COL_BG); spr.setCursor(30, 160); spr.print("Sambungkan");

  // Status WiFi
  spr.setTextColor(wifiConnected ? COL_GREEN : COL_RED);
  spr.setCursor(168, 160);
  spr.print(wifiConnected ? WiFi.SSID().c_str() : "Tidak terhubung");

  // Kalibrasi
  spr.fillRoundRect(8, 186, 144, 28, 8, COL_SURFACE);
  spr.setTextColor(COL_TEXT); spr.setCursor(18, 194); spr.print("Kalibrasi Ulang");

  // Keyboard (kalau aktif)
  if (kbVisible) drawKeyboard(spr);
  else drawBackButton(spr);
}

void settingsHandleTouch(int x, int y, LGFX_Sprite& spr) {
  // Brightness bar
  if (x>=18&&x<=238&&y>=42&&y<=78) {
    brightness = constrain(map(x-18,0,220,0,255),10,255);
    display.setBrightness(brightness);
    return;
  }
  // SSID field
  if (x>=8&&x<=312&&y>=84&&y<=112) {
    settFocusField = 0;
    kbTarget = &settWifiSSID;
    kbVisible = true; kbMode = KB_LOWER;
    return;
  }
  // Pass field
  if (x>=8&&x<=280&&y>=118&&y<=146) {
    settFocusField = 1;
    kbTarget = &settWifiPass;
    kbVisible = true; kbMode = KB_LOWER;
    return;
  }
  // Show/hide pass
  if (x>=280&&y>=118&&y<=146) { settShowPass=!settShowPass; return; }

  // Keyboard
  if (kbVisible && y >= KB_Y-4) {
    kbHandleTouch(x, y);
    return;
  }

  // Tombol Connect
  if (x>=8&&x<=152&&y>=152&&y<=180) {
    kbVisible = false; kbTarget = nullptr; settFocusField = -1;
    settWifiSSID.toCharArray(WIFI_SSID, 64);
    settWifiPass.toCharArray(WIFI_PASSWORD, 64);
    saveWifiCreds();
    connectWifi();
    return;
  }
  // Kalibrasi
  if (x>=8&&x<=152&&y>=186&&y<=214) {
    Preferences prefs;
    prefs.begin("touch_cal",false);
    prefs.putBool("done",false);
    prefs.end();
    ESP.restart();
  }
}

// =============================================
// APP: NOTEPAD
// =============================================
String noteText = "";

void drawNotepad(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_YELLOW);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Notepad");

  // Area teks
  int areaH = kbVisible ? (KB_Y - 40) : 160;
  spr.fillRoundRect(4, 38, 312, areaH, 6, COL_SURFACE);
  spr.setTextColor(COL_TEXT);
  spr.setTextSize(1);
  spr.setCursor(10, 44);
  spr.setTextWrap(true);

  // Tampilkan teks dengan cursor blink
  String display_text = noteText + "|";
  spr.print(display_text.c_str());

  // Tombol Clear
  spr.fillRoundRect(236, 30, 80, 20, 4, COL_RED);
  spr.setTextColor(COL_TEXT);
  spr.setCursor(252, 36);
  spr.print("Hapus");

  if (kbVisible) drawKeyboard(spr);
  else drawBackButton(spr);
}

void notepadHandleTouch(int x, int y) {
  // Tombol hapus
  if (x>=236&&x<=316&&y>=30&&y<=50) { noteText=""; return; }
  // Area teks — buka keyboard
  if (y>=38&&y<=200&&!kbVisible) {
    kbTarget  = &noteText;
    kbVisible = true;
    kbMode    = KB_LOWER;
    return;
  }
  // Keyboard
  if (kbVisible && y>=KB_Y-4) kbHandleTouch(x, y);
}

// =============================================
// APP: CANVAS (pakai sprite terpisah)
// =============================================
uint16_t drawColor  = COL_ACCENT;
int      brushSize  = 3;
bool     canvasInit = false;
int      lastDrawX  = -1, lastDrawY = -1;

uint16_t palette[] = {
  COL_TEXT, COL_RED, COL_GREEN, COL_ACCENT2, COL_YELLOW,
  COL_CYAN, COL_MAGENTA, COL_ACCENT, COL_BG
};
#define PAL_COUNT 9

void initCanvasApp() {
  if (!canvasInit) {
    canvasApp.createSprite(320, 180); // Area gambar
    canvasApp.fillSprite(COL_BG);
    canvasInit = true;
  }
}

void drawCanvasScreen(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_GREEN);
  spr.setTextSize(1);
  spr.setCursor(8, 30);
  spr.print("Canvas");

  // Area gambar dari canvasApp sprite
  canvasApp.pushSprite(&spr, 0, 40);

  // Toolbar bawah
  spr.fillRect(0, 220, 320, 20, COL_SURFACE);

  // Palet warna
  for (int i = 0; i < PAL_COUNT; i++) {
    int px = 4 + i * 24;
    spr.fillCircle(px+10, 229, 8, palette[i]);
    if (palette[i] == drawColor)
      spr.drawCircle(px+10, 229, 10, COL_TEXT);
  }

  // Brush size
  spr.fillRoundRect(226, 221, 40, 18, 4, COL_SURFACE2);
  spr.setTextColor(COL_TEXT);
  spr.setCursor(230, 226);
  char bs[8]; sprintf(bs,"B:%d", brushSize);
  spr.print(bs);

  // Tombol Clear
  spr.fillRoundRect(270, 221, 46, 18, 4, COL_RED);
  spr.setTextColor(COL_TEXT);
  spr.setCursor(278, 226);
  spr.print("CLR");

  drawBackButton(spr);
}

void canvasHandleTouch(int x, int y, bool isHeld) {
  // Toolbar bawah
  if (y >= 220) {
    // Palet
    for (int i = 0; i < PAL_COUNT; i++) {
      int px = 4 + i * 24;
      if (x>=px&&x<=px+20) { drawColor=palette[i]; lastDrawX=-1; return; }
    }
    // Brush size
    if (x>=226&&x<=266) {
      brushSize = (brushSize % 6) + 1;
      lastDrawX=-1; return;
    }
    // Clear
    if (x>=270) {
      canvasApp.fillSprite(COL_BG);
      lastDrawX=-1; return;
    }
    return;
  }

  // Area gambar (y: 40-220 → canvasApp y: 0-180)
  if (y >= 40 && y <= 220) {
    int cy = y - 40;
    if (isHeld && lastDrawX >= 0) {
      // Gambar garis dari titik terakhir (smooth)
      canvasApp.drawLine(lastDrawX, lastDrawY, x, cy, drawColor);
      // Tebalkan
      for (int t = 1; t < brushSize; t++) {
        canvasApp.drawLine(lastDrawX+t, lastDrawY, x+t, cy, drawColor);
        canvasApp.drawLine(lastDrawX, lastDrawY+t, x, cy+t, drawColor);
      }
    } else {
      canvasApp.fillCircle(x, cy, brushSize, drawColor);
    }
    lastDrawX = x;
    lastDrawY = cy;
  }
}

// =============================================
// PUSH FRAME (Sprite → Display via DMA)
// =============================================
void pushFrame() {
  canvas.pushSprite(0, 0);
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);

  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);

  // Alokasi sprite utama di PSRAM
  canvas.setPsram(true);
  canvas.createSprite(320, 240);

  // Splash
  canvas.fillSprite(COL_BG);
  canvas.setTextColor(COL_ACCENT);
  canvas.setTextSize(3);
  canvas.setCursor(50, 80);
  canvas.print("ESP Phone");
  canvas.setTextColor(COL_SUBTEXT);
  canvas.setTextSize(1);
  canvas.setCursor(96, 125);
  canvas.print("Powered by ESP32-S3");
  canvas.setCursor(108, 140);
  canvas.print("LovyanGFX + Sprite");
  pushFrame();
  delay(1500);

  // Kalibrasi touch
  if (display.touch()) loadOrRunCalibration();

  // Load WiFi creds & connect
  loadWifiCreds();
  settWifiSSID = String(WIFI_SSID);
  settWifiPass = String(WIFI_PASSWORD);
  connectWifi();

  // Init canvas app
  canvasApp.setPsram(true);
  initCanvasApp();

  // Gambar home
  drawHomeContent(canvas, 0);
  pushFrame();
}

// =============================================
// LOOP
// =============================================
unsigned long lastClockUpdate  = 0;
unsigned long lastSensorUpdate = 0;
unsigned long lastHomeUpdate   = 0;
unsigned long lastTouch        = 0;
bool          wasTouched       = false;
bool          needRedraw       = true;

void loop() {
  lgfx::touch_point_t tp;
  bool touched = display.getTouch(&tp);
  int tx = touched ? tp.x : 0;
  int ty = touched ? tp.y : 0;

  // ---- HOME: swipe scroll ----
  if (currentScreen == SCR_HOME) {
    if (touched) {
      if (!wasTouched) {
        touchStartY = ty;
        touchLastY  = ty;
        isSwiping   = false;
        swipeStartTime = millis();
      } else {
        int dy = touchLastY - ty;
        if (abs(dy) > 3) isSwiping = true;
        if (isSwiping) {
          homeScrollVel = dy * 0.8f;
          homeScrollY  += dy;
          float maxScroll = HOME_CONTENT_H - 200.0f;
          homeScrollY = constrain(homeScrollY, 0, maxScroll);
          needRedraw = true;
        }
        touchLastY = ty;
      }
    } else if (wasTouched) {
      // Tap (bukan swipe)
      if (!isSwiping && millis() - swipeStartTime < 300) {
        Screen next = homeTouchCheck(touchStartY > 0 ? tx : tx, touchStartY > 0 ? touchStartY : ty, homeScrollY);
        // Pakai touchStartY untuk Y
        next = homeTouchCheck(tx, touchStartY, homeScrollY);
        if (next != SCR_HOME) {
          currentScreen = next;
          kbVisible = false; kbTarget = nullptr;
          needRedraw = true;
          lastDrawX = -1;
        }
      }
    } else {
      // Inersia scroll
      if (abs(homeScrollVel) > 0.5f) {
        homeScrollY += homeScrollVel;
        homeScrollVel *= 0.85f;
        float maxScroll = HOME_CONTENT_H - 200.0f;
        homeScrollY = constrain(homeScrollY, 0, maxScroll);
        needRedraw = true;
      }
    }

    if (needRedraw) {
      drawHomeContent(canvas, homeScrollY);
      pushFrame();
      needRedraw = false;
    }

    // Update status bar jam setiap menit
    if (millis() - lastHomeUpdate > 60000) {
      lastHomeUpdate = millis();
      drawStatusBar(canvas);
      pushFrame();
    }

  } else {
    // ---- APP SCREENS ----
    bool doTouch = touched && !wasTouched && millis()-lastTouch > 180;

    // Canvas: handle held touch juga
    if (currentScreen == SCR_CANVAS && touched) {
      canvasHandleTouch(tx, ty, wasTouched);
      needRedraw = true;
    }

    if (doTouch) {
      lastTouch = millis();

      // Back button (semua app kecuali canvas area gambar)
      if (backButtonPressed(tx, ty) && !(currentScreen==SCR_CANVAS && ty<220)) {
        kbVisible = false; kbTarget = nullptr;
        currentScreen = SCR_HOME;
        needRedraw = true;
        goto endLoop;
      }

      switch (currentScreen) {
        case SCR_CALC:
          if (!backButtonPressed(tx,ty)) calcHandleTouch(tx, ty);
          needRedraw = true;
          break;
        case SCR_SENSOR:
          needRedraw = true;
          break;
        case SCR_SETTINGS:
          settingsHandleTouch(tx, ty, canvas);
          needRedraw = true;
          break;
        case SCR_NOTEPAD:
          notepadHandleTouch(tx, ty);
          needRedraw = true;
          break;
        default: break;
      }
    }

    // Auto update
    if (currentScreen == SCR_CLOCK && millis()-lastClockUpdate > 1000) {
      lastClockUpdate = millis(); needRedraw = true;
    }
    if (currentScreen == SCR_SENSOR && millis()-lastSensorUpdate > 2000) {
      lastSensorUpdate = millis(); needRedraw = true;
    }

    if (needRedraw) {
      switch (currentScreen) {
        case SCR_CLOCK:    drawClock(canvas);    break;
        case SCR_CALC:     drawCalc(canvas);     break;
        case SCR_SENSOR:   drawSensor(canvas);   break;
        case SCR_SETTINGS: drawSettings(canvas); break;
        case SCR_NOTEPAD:  drawNotepad(canvas);  break;
        case SCR_CANVAS:   drawCanvasScreen(canvas); break;
        default: break;
      }
      pushFrame();
      needRedraw = false;
    }
  }

  endLoop:
  wasTouched = touched;
  delay(8); // ~120fps max
}
