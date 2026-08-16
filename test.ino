#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"

// =============================================
// WIFI CONFIG
// =============================================
char WIFI_SSID[64]     = "";
char WIFI_PASSWORD[64] = "";
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 7 * 3600;
const int   DST_OFFSET = 0;

// =============================================
// SD CARD CONFIG (SDIO 1-bit mode)
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady = false;

const char* NOTE_FILE   = "/notepad.txt";
const char* CANVAS_FILE = "/canvas.bin";

// =============================================
// WARNA
// =============================================
#define COL_BG        0x1084
#define COL_SURFACE   0x2104
#define COL_SURFACE2  0x3186
#define COL_ACCENT    0xFD40
#define COL_ACCENT2   0x04FF  // biru terang
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
      cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0;
      cfg.freq_write = 40000000; cfg.freq_read = 16000000;
      cfg.spi_3wire = false; cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 12; cfg.pin_mosi = 11;
      cfg.pin_miso = 13; cfg.pin_dc   = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 10; cfg.pin_rst = 14; cfg.pin_busy = -1;
      cfg.memory_width = 240; cfg.memory_height = 320;
      cfg.panel_width  = 240; cfg.panel_height  = 320;
      cfg.offset_x = 0; cfg.offset_y = 0; cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8; cfg.dummy_read_bits = 1;
      cfg.readable = true; cfg.invert = false;
      cfg.rgb_order = false; cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 21; cfg.invert = false;
      cfg.freq = 44100; cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    {
      auto cfg = _touch_instance.config();
      cfg.pin_int = -1; cfg.bus_shared = false;
      cfg.offset_rotation = 0; cfg.spi_host = SPI3_HOST;
      cfg.freq = 2000000;
      cfg.pin_sclk = 6; cfg.pin_mosi = 5;
      cfg.pin_miso = 4; cfg.pin_cs   = 9;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX display;
LGFX_Sprite canvas(&display);
LGFX_Sprite canvasApp(&display);

// =============================================
// STATE GLOBAL
// =============================================
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SENSOR, SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS };
Screen currentScreen = SCR_HOME;
bool wifiConnected = false;
bool ntpSynced     = false;
int  brightness    = 200;

// Home scroll
float homeScrollY   = 0;
float homeScrollVel = 0;
int   touchStartX   = 0;
int   touchStartY   = 0;
int   touchLastY    = 0;
bool  isSwiping     = false;
unsigned long swipeStartTime = 0;

// =============================================
// KEYBOARD VIRTUAL
// Layout: 10 kolom x 3 baris huruf + 1 baris kontrol
// Total lebar 320px, tiap key ~30px
// =============================================
enum KbMode { KB_LOWER, KB_UPPER, KB_NUM };
KbMode  kbMode    = KB_LOWER;
bool    kbVisible = false;
String* kbTarget  = nullptr;

// KEY_W=29, gap=2 → 10*(29+2)-2=308, sisa 12px → offsetX=6
#define KB_KEY_W   29
#define KB_KEY_H   22
#define KB_GAP     2
#define KB_Y_START 148  // mulai dari y=148, tinggi total ~96px

const char* kbLower[3][10] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l",";"},
  {"z","x","c","v","b","n","m",",",".","?"}
};
const char* kbUpper[3][10] = {
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L",":"},
  {"Z","X","C","V","B","N","M","!","@","#"}
};
const char* kbNum[3][10] = {
  {"1","2","3","4","5","6","7","8","9","0"},
  {"-","=","[","]","/","'","\"","<",">","\\"},
  {"~","!","@","#","$","%","^","&","*","("}
};

const char* (*kbLayouts[3])[10] = { kbLower, kbUpper, kbNum };

// Baris r → offsetX biar centered (baris 1 geser 15px, baris 2 geser 30px)
int kbRowOffset(int r) { return 6 + (r==1?15:(r==2?30:0)); }

void drawKeyboard(LGFX_Sprite& spr) {
  // Background keyboard
  spr.fillRect(0, KB_Y_START - 2, 320, 240 - KB_Y_START + 2, COL_SURFACE);
  spr.drawFastHLine(0, KB_Y_START - 2, 320, COL_DIVIDER);

  const char* (*layout)[10] = kbLayouts[(int)kbMode];

  // 3 baris huruf
  for (int r = 0; r < 3; r++) {
    int ry = KB_Y_START + r * (KB_KEY_H + KB_GAP);
    int ox = kbRowOffset(r);
    for (int c = 0; c < 10; c++) {
      int kx = ox + c * (KB_KEY_W + KB_GAP);
      spr.fillRoundRect(kx, ry, KB_KEY_W, KB_KEY_H, 3, COL_SURFACE2);
      spr.setTextColor(COL_TEXT);
      spr.setTextSize(1);
      // Center karakter
      int cx = kx + KB_KEY_W/2 - 3;
      int cy = ry + KB_KEY_H/2 - 4;
      spr.setCursor(cx, cy);
      spr.print(layout[r][c]);
    }
  }

  // Baris kontrol (y = KB_Y_START + 3*(KB_KEY_H+KB_GAP))
  int cy = KB_Y_START + 3 * (KB_KEY_H + KB_GAP);

  // SHF (40px)
  spr.fillRoundRect(4, cy, 40, KB_KEY_H, 3, kbMode==KB_UPPER ? COL_ACCENT : COL_SURFACE2);
  spr.setTextColor(COL_TEXT); spr.setTextSize(1);
  spr.setCursor(10, cy + KB_KEY_H/2 - 4); spr.print("SHF");

  // 123/ABC (44px)
  spr.fillRoundRect(48, cy, 44, KB_KEY_H, 3, kbMode==KB_NUM ? COL_ACCENT : COL_SURFACE2);
  spr.setCursor(54, cy + KB_KEY_H/2 - 4);
  spr.print(kbMode==KB_NUM ? "ABC" : "123");

  // SPACE (mengisi tengah)
  spr.fillRoundRect(96, cy, 128, KB_KEY_H, 3, COL_SURFACE2);
  spr.setCursor(136, cy + KB_KEY_H/2 - 4); spr.print("SPACE");

  // BKSP (sisa kanan)
  spr.fillRoundRect(228, cy, 88, KB_KEY_H, 3, COL_RED);
  spr.setCursor(248, cy + KB_KEY_H/2 - 4); spr.print("<--");
}

void kbHandleTouch(int x, int y) {
  if (!kbVisible || kbTarget == nullptr) return;
  if (y < KB_Y_START - 2) return;

  int ctrlY = KB_Y_START + 3 * (KB_KEY_H + KB_GAP);

  // Baris kontrol
  if (y >= ctrlY && y <= ctrlY + KB_KEY_H) {
    if (x >= 4 && x <= 44) {
      kbMode = (kbMode == KB_UPPER) ? KB_LOWER : KB_UPPER;
    } else if (x >= 48 && x <= 92) {
      kbMode = (kbMode == KB_NUM) ? KB_LOWER : KB_NUM;
    } else if (x >= 96 && x <= 224) {
      *kbTarget += " ";
    } else if (x >= 228) {
      if (kbTarget->length() > 0)
        *kbTarget = kbTarget->substring(0, kbTarget->length() - 1);
    }
    return;
  }

  // Baris huruf
  const char* (*layout)[10] = kbLayouts[(int)kbMode];
  for (int r = 0; r < 3; r++) {
    int ry = KB_Y_START + r * (KB_KEY_H + KB_GAP);
    if (y >= ry && y <= ry + KB_KEY_H) {
      int ox = kbRowOffset(r);
      for (int c = 0; c < 10; c++) {
        int kx = ox + c * (KB_KEY_W + KB_GAP);
        if (x >= kx && x <= kx + KB_KEY_W) {
          *kbTarget += layout[r][c];
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
    display.setTextColor(COL_TEXT); display.setTextSize(2);
    display.setCursor(20, 100); display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20, 130); display.println("Sentuh tanda di setiap sudut");
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
  while (WiFi.status() != WL_CONNECTED && tries < 20) { delay(300); tries++; }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) ntpSynced = true;
  }
}
void loadWifiCreds() {
  Preferences prefs; prefs.begin("wifi", true);
  prefs.getString("ssid", "").toCharArray(WIFI_SSID, 64);
  prefs.getString("pass", "").toCharArray(WIFI_PASSWORD, 64);
  prefs.end();
}
void saveWifiCreds() {
  Preferences prefs; prefs.begin("wifi", false);
  prefs.putString("ssid", WIFI_SSID);
  prefs.putString("pass", WIFI_PASSWORD);
  prefs.end();
}

// =============================================
// SD CARD (SDIO 1-bit: CLK=39, CMD=38, D0=40)
// =============================================
void initSD() {
  SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0);
  // mode 1-bit (parameter kedua "true") karena cuma D0 yang dipakai
  sdReady = SD_MMC.begin("/sdcard", true);
  if (!sdReady) {
    Serial.println("SD Card: gagal mount / tidak terpasang");
  } else {
    Serial.printf("SD Card: OK, %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  }
}

// =============================================
// STATUS BAR & BACK BUTTON
// =============================================
void drawStatusBar(LGFX_Sprite& spr) {
  spr.fillRect(0, 0, 320, 24, COL_SURFACE);
  spr.drawFastHLine(0, 24, 320, COL_DIVIDER);
  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    char buf[9]; sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    spr.setTextColor(COL_TEXT); spr.setTextSize(1);
    spr.setCursor(8, 8); spr.print(buf);
  } else {
    spr.setTextColor(COL_SUBTEXT); spr.setTextSize(1);
    spr.setCursor(8, 8); spr.print("--:--");
  }
  if (wifiConnected) {
    spr.fillCircle(302, 17, 2, COL_GREEN);
    spr.drawArc(302, 19, 5, 4, 210, 330, COL_GREEN);
    spr.drawArc(302, 19, 9, 8, 210, 330, COL_GREEN);
  } else {
    spr.fillCircle(302, 17, 2, COL_RED);
    spr.drawLine(298, 13, 306, 21, COL_RED);
  }
  // Indikator SD card (hanya tampil kalau kartu terpasang & termount)
  if (sdReady) {
    spr.setTextColor(COL_GREEN); spr.setTextSize(1);
    spr.setCursor(266, 8); spr.print("SD");
  }
}

// Back button hanya ditampilkan jika keyboard TIDAK aktif
#define BACK_X 4
#define BACK_Y 213
#define BACK_W 62
#define BACK_H 24

void drawBackButton(LGFX_Sprite& spr) {
  if (kbVisible) return; // Jangan tampilkan kalau keyboard aktif
  spr.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 6, COL_SURFACE);
  spr.setTextColor(COL_ACCENT); spr.setTextSize(1);
  spr.setCursor(BACK_X + 10, BACK_Y + 8); spr.print("< Back");
}

// Back hanya aktif kalau keyboard tidak aktif DAN tombol benar-benar ditekan
bool backButtonPressed(int x, int y) {
  if (kbVisible) return false; // PENTING: block back kalau keyboard terbuka
  return (x >= BACK_X && x <= BACK_X + BACK_W && y >= BACK_Y && y <= BACK_Y + BACK_H);
}

// =============================================
// HOME SCREEN
// =============================================
struct AppDef { const char* name; uint16_t color; char sym; };
AppDef appDefs[6] = {
  { "Jam",        COL_ACCENT,  'T' },
  { "Kalkulator", COL_ACCENT2, '+' },
  { "Sensor",     COL_CYAN,    '~' },
  { "Setting",    COL_MAGENTA, '@' },
  { "Notepad",    COL_YELLOW,  'N' },
  { "Canvas",     COL_GREEN,   'C' },
};
#define HOME_CARD_W  140
#define HOME_CARD_H  94
#define HOME_CONTENT_H 330

void drawHomeContent(LGFX_Sprite& spr, float scrollY) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_SUBTEXT); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Beranda");

  for (int i = 0; i < 6; i++) {
    int col = i % 2, row = i / 2;
    int x = 10 + col * 160;
    int y = 44 + row * (HOME_CARD_H + 8) - (int)scrollY;
    if (y + HOME_CARD_H < 0 || y > 240) continue;
    spr.fillRoundRect(x, y, HOME_CARD_W, HOME_CARD_H, 12, COL_SURFACE);
    spr.fillRoundRect(x + 48, y + 10, 44, 38, 8, appDefs[i].color);
    spr.setTextSize(3); spr.setTextColor(COL_BG);
    spr.setCursor(x + 59, y + 17); spr.print(appDefs[i].sym);
    spr.setTextSize(1); spr.setTextColor(COL_TEXT);
    int nameLen = strlen(appDefs[i].name);
    spr.setCursor(x + HOME_CARD_W/2 - nameLen*3, y + 72);
    spr.print(appDefs[i].name);
  }
}

Screen homeTouchCheck(int x, int y, float scrollY) {
  Screen apps[6] = { SCR_CLOCK, SCR_CALC, SCR_SENSOR, SCR_SETTINGS, SCR_NOTEPAD, SCR_CANVAS };
  for (int i = 0; i < 6; i++) {
    int col = i % 2, row = i / 2;
    int ax = 10 + col * 160;
    int ay = 44 + row * (HOME_CARD_H + 8) - (int)scrollY;
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
  spr.setTextColor(COL_ACCENT); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Jam");

  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    spr.setTextColor(COL_TEXT); spr.setTextSize(3);
    spr.setCursor(20, 65); spr.print(timeBuf);

    const char* days[]   = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* months[] = {"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char dateBuf[28];
    sprintf(dateBuf, "%s, %02d %s %04d", days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year+1900);
    spr.setTextColor(COL_SUBTEXT); spr.setTextSize(1);
    spr.setCursor(20, 108); spr.print(dateBuf);
    spr.drawFastHLine(20, 122, 280, COL_ACCENT);
    spr.setTextColor(COL_GREEN); spr.setCursor(20, 130);
    spr.print(ntpSynced ? "NTP Sync OK" : "Tidak sync");
  } else {
    spr.setTextColor(COL_RED); spr.setTextSize(2);
    spr.setCursor(20, 80); spr.print("Tidak sync");
  }
  drawBackButton(spr);
}

// =============================================
// APP: KALKULATOR
// Layar landscape 320x240, area y: 25-240
// Baris display: y 28-62 (34px)
// 5 baris tombol: masing2 34px, gap 2px
// Total: 5*34 + 4*2 = 178px, mulai y=64
// Tombol terakhir y=64+4*36=208, end=208+34=242 → pas!
// =============================================
String calcInput  = "0";
float  calcA      = 0;
char   calcOp     = 0;
bool   calcNewNum = true;

struct CalcBtn { int x, y, w, h; const char* lbl; uint16_t bg, fg; };

CalcBtn calcBtns[] = {
  // Baris 1: y=64
  {4,  64, 72,34, "C",   COL_SURFACE, COL_ACCENT},
  {80, 64, 72,34, "+/-", COL_SURFACE, COL_ACCENT},
  {156,64, 72,34, "%",   COL_SURFACE, COL_ACCENT},
  {232,64, 84,34, "/",   COL_ACCENT,  COL_BG},
  // Baris 2: y=100
  {4,  100,72,34, "7", COL_SURFACE, COL_TEXT},
  {80, 100,72,34, "8", COL_SURFACE, COL_TEXT},
  {156,100,72,34, "9", COL_SURFACE, COL_TEXT},
  {232,100,84,34, "x", COL_ACCENT,  COL_BG},
  // Baris 3: y=136
  {4,  136,72,34, "4", COL_SURFACE, COL_TEXT},
  {80, 136,72,34, "5", COL_SURFACE, COL_TEXT},
  {156,136,72,34, "6", COL_SURFACE, COL_TEXT},
  {232,136,84,34, "-", COL_ACCENT,  COL_BG},
  // Baris 4: y=172
  {4,  172,72,34, "1", COL_SURFACE, COL_TEXT},
  {80, 172,72,34, "2", COL_SURFACE, COL_TEXT},
  {156,172,72,34, "3", COL_SURFACE, COL_TEXT},
  {232,172,84,34, "+", COL_ACCENT,  COL_BG},
  // Baris 5: y=208
  {4,  208,148,34, "0", COL_SURFACE, COL_TEXT},
  {156,208, 72,34, ".", COL_SURFACE, COL_TEXT},
  {232,208, 84,34, "=", COL_ACCENT2, COL_BG},
};

void drawCalc(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_ACCENT); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Kalkulator");

  // Display
  spr.fillRoundRect(4, 38, 312, 24, 4, COL_SURFACE);
  spr.setTextColor(COL_TEXT); spr.setTextSize(2);
  int tw = calcInput.length() * 12;
  spr.setCursor(max(8, 308 - tw), 42);
  spr.print(calcInput);

  // Tombol
  int n = sizeof(calcBtns)/sizeof(calcBtns[0]);
  for (int i = 0; i < n; i++) {
    auto& b = calcBtns[i];
    spr.fillRoundRect(b.x, b.y, b.w, b.h, 6, b.bg);
    spr.setTextColor(b.fg); spr.setTextSize(2);
    int lw = strlen(b.lbl) * 12;
    spr.setCursor(b.x + b.w/2 - lw/2, b.y + b.h/2 - 8);
    spr.print(b.lbl);
  }
}

void calcHandleTouch(int x, int y) {
  int n = sizeof(calcBtns)/sizeof(calcBtns[0]);
  for (int i = 0; i < n; i++) {
    auto& b = calcBtns[i];
    if (x>=b.x && x<=b.x+b.w && y>=b.y && y<=b.y+b.h) {
      const char* l = b.lbl;
      if (!strcmp(l,"C"))       { calcInput="0"; calcA=0; calcOp=0; calcNewNum=true; }
      else if (!strcmp(l,"+/-")){ calcInput=String(calcInput.toFloat()*-1); }
      else if (!strcmp(l,"%"))  { calcInput=String(calcInput.toFloat()/100); }
      else if (!strcmp(l,"="))  {
        float b2=calcInput.toFloat(), res=0;
        if(calcOp=='+') res=calcA+b2; else if(calcOp=='-') res=calcA-b2;
        else if(calcOp=='x') res=calcA*b2;
        else if(calcOp=='/') res=(b2!=0)?calcA/b2:0;
        calcInput=(res==(int)res)?String((int)res):String(res,4);
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
      return;
    }
  }
}

// =============================================
// APP: SENSOR
// =============================================
float readInternalTemp() { return temperatureRead(); }

void drawSensor(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_CYAN); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Sensor");

  float temp = readInternalTemp();
  spr.fillRoundRect(20, 48, 280, 100, 14, COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT); spr.setTextSize(1);
  spr.setCursor(35, 62); spr.print("Suhu Internal Chip");

  char buf[16]; sprintf(buf, "%.1f", temp);
  spr.setTextColor(COL_CYAN); spr.setTextSize(4);
  spr.setCursor(40, 78); spr.print(buf);
  spr.setTextSize(2); spr.print(" C");

  spr.fillRoundRect(20, 158, 280, 18, 6, COL_DIVIDER);
  int bw = constrain(map((int)temp, 20, 90, 0, 276), 0, 276);
  uint16_t bc = (temp<50)?COL_GREEN:(temp<70)?COL_ACCENT:COL_RED;
  spr.fillRoundRect(22, 160, bw, 14, 4, bc);

  spr.setTextColor(COL_SUBTEXT); spr.setTextSize(1);
  spr.setCursor(20, 182); spr.print("20C            55C            90C");
  spr.setTextColor(COL_DIVIDER);
  spr.setCursor(88, 198); spr.print("Tap untuk refresh");

  drawBackButton(spr);
}

// =============================================
// APP: SETTINGS
// =============================================
String settWifiSSID  = "";
String settWifiPass  = "";
bool   settShowPass  = false;
int    settFocus     = -1; // 0=ssid, 1=pass

void drawSettings(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_MAGENTA); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Pengaturan");

  // Brightness
  spr.fillRoundRect(8, 40, 304, 34, 8, COL_SURFACE);
  spr.setTextColor(COL_TEXT); spr.setCursor(18, 48); spr.print("Kecerahan");
  spr.fillRoundRect(18, 60, 220, 8, 4, COL_DIVIDER);
  spr.fillRoundRect(19, 61, map(brightness,0,255,0,218), 6, 3, COL_ACCENT);
  char bb[6]; sprintf(bb, "%d%%", brightness*100/255);
  spr.setTextColor(COL_SUBTEXT); spr.setCursor(248, 60); spr.print(bb);

  // SSID
  spr.fillRoundRect(8, 80, 304, 28, 6, settFocus==0 ? COL_SURFACE2 : COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT); spr.setCursor(18, 88); spr.print("SSID:");
  spr.setTextColor(COL_TEXT); spr.setCursor(64, 88);
  String ssidDisp = settWifiSSID.length()>0 ? settWifiSSID : "(ketuk)";
  if(ssidDisp.length()>22) ssidDisp = ssidDisp.substring(0,22)+"..";
  spr.print(ssidDisp.c_str());

  // Password
  spr.fillRoundRect(8, 114, 268, 28, 6, settFocus==1 ? COL_SURFACE2 : COL_SURFACE);
  spr.setTextColor(COL_SUBTEXT); spr.setCursor(18, 122); spr.print("Pass:");
  spr.setTextColor(COL_TEXT); spr.setCursor(64, 122);
  if(settWifiPass.length()>0) {
    String p = settShowPass ? settWifiPass : String("").operator+=(String(settWifiPass.length()>18?18:settWifiPass.length(), '*'));
    if(!settShowPass) { for(int i=0;i<(int)settWifiPass.length()&&i<18;i++) spr.print("*"); }
    else spr.print(settWifiPass.substring(0,18).c_str());
  } else spr.print("(ketuk)");

  // Tombol show/hide pass
  spr.fillRoundRect(280, 114, 32, 28, 4, COL_SURFACE2);
  spr.setTextColor(COL_ACCENT); spr.setCursor(287, 122);
  spr.print(settShowPass ? "H" : "S");

  // Tombol Connect
  spr.fillRoundRect(8, 148, 148, 28, 8, COL_ACCENT);
  spr.setTextColor(COL_BG); spr.setCursor(28, 156); spr.print("Sambungkan");

  // Status
  spr.setTextColor(wifiConnected ? COL_GREEN : COL_RED);
  spr.setCursor(164, 156);
  String ssidShow = wifiConnected ? String(WiFi.SSID()) : String("Tdk terhubung");
  spr.print(ssidShow.substring(0,14).c_str());

  // Kalibrasi
  spr.fillRoundRect(8, 182, 148, 28, 8, COL_SURFACE);
  spr.setTextColor(COL_TEXT); spr.setCursor(18, 190); spr.print("Kalibrasi Ulang");

  if (kbVisible) drawKeyboard(spr);
  else drawBackButton(spr);
}

void settingsHandleTouch(int x, int y) {
  // Kalau keyboard aktif, handle keyboard dulu
  if (kbVisible) {
    // Tombol close keyboard: tap di area non-keyboard (y < KB_Y_START-2)
    if (y < KB_Y_START - 2) {
      // Tap di luar keyboard → tutup keyboard
      kbVisible = false; kbTarget = nullptr; settFocus = -1;
    } else {
      kbHandleTouch(x, y);
    }
    return;
  }

  // Brightness
  if (x>=18&&x<=238&&y>=40&&y<=74) {
    brightness = constrain(map(x-18,0,220,0,255),10,255);
    display.setBrightness(brightness); return;
  }
  // SSID
  if (x>=8&&x<=312&&y>=80&&y<=108) {
    settFocus=0; kbTarget=&settWifiSSID; kbVisible=true; kbMode=KB_LOWER; return;
  }
  // Pass
  if (x>=8&&x<=280&&y>=114&&y<=142) {
    settFocus=1; kbTarget=&settWifiPass; kbVisible=true; kbMode=KB_LOWER; return;
  }
  // Show/hide
  if (x>=280&&y>=114&&y<=142) { settShowPass=!settShowPass; return; }
  // Connect
  if (x>=8&&x<=156&&y>=148&&y<=176) {
    settWifiSSID.toCharArray(WIFI_SSID,64);
    settWifiPass.toCharArray(WIFI_PASSWORD,64);
    saveWifiCreds(); connectWifi(); return;
  }
  // Kalibrasi
  if (x>=8&&x<=156&&y>=182&&y<=210) {
    Preferences prefs; prefs.begin("touch_cal",false);
    prefs.putBool("done",false); prefs.end(); ESP.restart();
  }
}

// =============================================
// APP: NOTEPAD
// =============================================
String noteText = "";

void loadNoteFromSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(NOTE_FILE, FILE_READ);
  if (f) {
    noteText = f.readString();
    f.close();
  }
}

void saveNoteToSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(NOTE_FILE, FILE_WRITE);
  if (f) {
    f.print(noteText);
    f.close();
  }
}

void drawNotepad(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);
  spr.setTextColor(COL_YELLOW); spr.setTextSize(1);
  spr.setCursor(8, 30); spr.print("Notepad");

  // Tombol Hapus (pojok kanan, TIDAK BERTABRAKAN dengan area teks)
  spr.fillRoundRect(240, 26, 76, 18, 4, COL_RED);
  spr.setTextColor(COL_TEXT); spr.setCursor(256, 31); spr.print("Hapus");

  // Area teks
  int areaBottom = kbVisible ? KB_Y_START - 4 : 208;
  spr.fillRoundRect(4, 46, 312, areaBottom - 46, 6, COL_SURFACE);
  spr.setTextColor(COL_TEXT); spr.setTextSize(1);
  spr.setTextWrap(true);
  spr.setCursor(10, 52);
  spr.print((noteText + "|").c_str());

  if (kbVisible) drawKeyboard(spr);
  else drawBackButton(spr);
}

void notepadHandleTouch(int x, int y) {
  if (kbVisible) {
    if (y < KB_Y_START - 2) {
      // Tap di luar keyboard → tutup
      kbVisible = false; kbTarget = nullptr;
    } else {
      kbHandleTouch(x, y);
    }
    return;
  }
  // Tombol Hapus
  if (x>=240&&x<=316&&y>=26&&y<=44) { noteText=""; saveNoteToSD(); return; }
  // Area teks → buka keyboard
  if (y>=46&&y<=208) {
    kbTarget=&noteText; kbVisible=true; kbMode=KB_LOWER;
  }
}

// =============================================
// APP: CANVAS
// =============================================
bool     canvasInit = false;
uint16_t drawColor  = COL_ACCENT;
int      brushSize  = 3;
int      lastDrawX  = -1, lastDrawY = -1;

uint16_t palette[] = {
  COL_TEXT, COL_RED, COL_GREEN, COL_ACCENT2,
  COL_YELLOW, COL_CYAN, COL_MAGENTA, COL_ACCENT, COL_BG
};
#define PAL_COUNT 9
#define CANVAS_AREA_Y  26
#define CANVAS_AREA_H  190  // y 26~216
#define TOOLBAR_Y      216

// Ukuran buffer mentah RGB565: lebar 320 x tinggi CANVAS_AREA_H x 2 byte/pixel
#define CANVAS_BUF_SIZE (320UL * CANVAS_AREA_H * 2UL)

void loadCanvasFromSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(CANVAS_FILE, FILE_READ);
  if (f && f.size() == CANVAS_BUF_SIZE) {
    uint8_t* buf = (uint8_t*)canvasApp.getBuffer();
    f.read(buf, CANVAS_BUF_SIZE);
  }
  if (f) f.close();
}

void saveCanvasToSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(CANVAS_FILE, FILE_WRITE);
  if (f) {
    uint8_t* buf = (uint8_t*)canvasApp.getBuffer();
    f.write(buf, CANVAS_BUF_SIZE);
    f.close();
  }
}

void initCanvasApp() {
  if (!canvasInit) {
    canvasApp.setPsram(true);
    canvasApp.createSprite(320, CANVAS_AREA_H);
    canvasApp.fillSprite(COL_BG);
    loadCanvasFromSD(); // auto-load gambar terakhir kalau ada di SD
    canvasInit = true;
  }
}

void drawCanvasScreen(LGFX_Sprite& spr) {
  spr.fillSprite(COL_BG);
  drawStatusBar(spr);

  // Label
  spr.setTextColor(COL_GREEN); spr.setTextSize(1);
  spr.setCursor(8, 14); spr.print("Canvas");

  // Gambar dari canvasApp sprite ke posisi y=CANVAS_AREA_Y
  canvasApp.pushSprite(&spr, 0, CANVAS_AREA_Y);

  // Toolbar (y=TOOLBAR_Y ~ 240)
  spr.fillRect(0, TOOLBAR_Y, 320, 240 - TOOLBAR_Y, COL_SURFACE);
  spr.drawFastHLine(0, TOOLBAR_Y, 320, COL_DIVIDER);

  // Palet warna
  for (int i = 0; i < PAL_COUNT; i++) {
    int px = 4 + i * 24;
    spr.fillCircle(px + 10, TOOLBAR_Y + 12, 9, palette[i]);
    if (palette[i] == drawColor)
      spr.drawCircle(px + 10, TOOLBAR_Y + 12, 11, COL_TEXT);
  }

  // Brush size
  spr.fillRoundRect(224, TOOLBAR_Y + 2, 44, 20, 4, COL_SURFACE2);
  spr.setTextColor(COL_TEXT); spr.setTextSize(1);
  char bs[8]; sprintf(bs, "B:%d", brushSize);
  spr.setCursor(228, TOOLBAR_Y + 8); spr.print(bs);

  // CLR
  spr.fillRoundRect(272, TOOLBAR_Y + 2, 44, 20, 4, COL_RED);
  spr.setTextColor(COL_TEXT);
  spr.setCursor(280, TOOLBAR_Y + 8); spr.print("CLR");

  // Back (kiri bawah, di bawah toolbar — di sini back selalu tampil karena tidak ada keyboard)
  spr.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 6, COL_SURFACE);
  spr.setTextColor(COL_ACCENT); spr.setTextSize(1);
  spr.setCursor(BACK_X + 10, BACK_Y + 8); spr.print("< Back");
}

void canvasHandleTouch(int x, int y, bool isHeld) {
  // Toolbar
  if (y >= TOOLBAR_Y) {
    if (!isHeld) { // hanya saat touch baru untuk toolbar
      for (int i = 0; i < PAL_COUNT; i++) {
        int px = 4 + i * 24;
        if (x >= px && x <= px + 20) { drawColor = palette[i]; lastDrawX = -1; return; }
      }
      if (x >= 224 && x <= 268) { brushSize = (brushSize % 8) + 1; lastDrawX = -1; return; }
      if (x >= 272) { canvasApp.fillSprite(COL_BG); lastDrawX = -1; saveCanvasToSD(); return; }
    }
    return;
  }

  // Back button area (di canvas, back ada di pojok kiri bawah y>BACK_Y)
  if (y >= BACK_Y && x >= BACK_X && x <= BACK_X + BACK_W) return; // dibiarkan loop utama handle

  // Area gambar
  if (y >= CANVAS_AREA_Y && y <= CANVAS_AREA_Y + CANVAS_AREA_H) {
    int cy = y - CANVAS_AREA_Y;
    if (isHeld && lastDrawX >= 0) {
      canvasApp.drawLine(lastDrawX, lastDrawY, x, cy, drawColor);
      for (int t = 1; t < brushSize; t++) {
        canvasApp.drawLine(lastDrawX, lastDrawY+t, x, cy+t, drawColor);
        canvasApp.drawLine(lastDrawX+t, lastDrawY, x+t, cy, drawColor);
      }
    } else {
      canvasApp.fillCircle(x, cy, brushSize, drawColor);
    }
    lastDrawX = x; lastDrawY = cy;
  }
}

// =============================================
// PUSH FRAME
// =============================================
void pushFrame() { canvas.pushSprite(0, 0); }

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);

  canvas.setPsram(true);
  canvas.createSprite(320, 240);

  // Splash
  canvas.fillSprite(COL_BG);
  canvas.setTextColor(COL_ACCENT); canvas.setTextSize(3);
  canvas.setCursor(50, 80); canvas.print("ESP Phone");
  canvas.setTextColor(COL_SUBTEXT); canvas.setTextSize(1);
  canvas.setCursor(80, 125); canvas.print("Powered by ESP32-S3 + LGFX");
  pushFrame(); delay(1400);

  if (display.touch()) loadOrRunCalibration();

  initSD();          // mount SD card (SDIO 1-bit: CLK=39 CMD=38 D0=40)
  loadNoteFromSD();  // load isi notepad terakhir (kalau ada)

  loadWifiCreds();
  settWifiSSID = String(WIFI_SSID);
  settWifiPass = String(WIFI_PASSWORD);
  connectWifi();

  canvasApp.setPsram(true);
  initCanvasApp();

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
  int tx = touched ? (int)tp.x : 0;
  int ty = touched ? (int)tp.y : 0;

  // ============ HOME ============
  if (currentScreen == SCR_HOME) {
    if (touched) {
      if (!wasTouched) {
        touchStartX = tx; touchStartY = ty;
        touchLastY = ty; isSwiping = false;
        swipeStartTime = millis();
      } else {
        int totalDy = abs(ty - touchStartY);
        if (totalDy > 12) isSwiping = true;
        if (isSwiping) {
          int dy = touchLastY - ty;
          homeScrollVel = dy * 0.8f;
          homeScrollY  += dy;
          homeScrollY   = constrain(homeScrollY, 0, (float)(HOME_CONTENT_H - 200));
          needRedraw = true;
        }
        touchLastY = ty;
      }
    } else if (wasTouched) {
      if (!isSwiping && millis() - swipeStartTime < 400) {
        Screen next = homeTouchCheck(touchStartX, touchStartY, homeScrollY);
        if (next != SCR_HOME) {
          currentScreen = next;
          kbVisible = false; kbTarget = nullptr;
          needRedraw = true; lastDrawX = -1;
        }
      }
    } else {
      // inersia
      if (abs(homeScrollVel) > 0.5f) {
        homeScrollY  += homeScrollVel;
        homeScrollVel *= 0.85f;
        homeScrollY   = constrain(homeScrollY, 0, (float)(HOME_CONTENT_H - 200));
        needRedraw = true;
      }
    }

    if (needRedraw) { drawHomeContent(canvas, homeScrollY); pushFrame(); needRedraw = false; }
    if (millis() - lastHomeUpdate > 60000) {
      lastHomeUpdate = millis(); drawStatusBar(canvas); pushFrame();
    }

  // ============ APP SCREENS ============
  } else {
    bool newTouch = touched && !wasTouched;

    // Canvas: gambar saat touched (held)
    if (currentScreen == SCR_CANVAS && touched) {
      // Back button di canvas
      if (newTouch && ty >= BACK_Y && tx >= BACK_X && tx <= BACK_X + BACK_W) {
        saveCanvasToSD(); // auto-save gambar ke SD sebelum keluar
        currentScreen = SCR_HOME; needRedraw = true;
        goto endLoop;
      }
      canvasHandleTouch(tx, ty, wasTouched);
      needRedraw = true;
    }

    if (newTouch && currentScreen != SCR_CANVAS) {
      lastTouch = millis();

      // Back button — hanya kalau keyboard tidak aktif
      if (backButtonPressed(tx, ty)) {
        if (currentScreen == SCR_NOTEPAD) saveNoteToSD(); // auto-save catatan ke SD
        kbVisible = false; kbTarget = nullptr;
        currentScreen = SCR_HOME; needRedraw = true;
        goto endLoop;
      }

      switch (currentScreen) {
        case SCR_CALC:     calcHandleTouch(tx, ty);      needRedraw = true; break;
        case SCR_SENSOR:                                  needRedraw = true; break;
        case SCR_SETTINGS: settingsHandleTouch(tx, ty);  needRedraw = true; break;
        case SCR_NOTEPAD:  notepadHandleTouch(tx, ty);   needRedraw = true; break;
        default: break;
      }
    }

    // Auto update
    if (currentScreen == SCR_CLOCK && millis() - lastClockUpdate > 1000)
      { lastClockUpdate = millis(); needRedraw = true; }
    if (currentScreen == SCR_SENSOR && millis() - lastSensorUpdate > 2000)
      { lastSensorUpdate = millis(); needRedraw = true; }

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
      pushFrame(); needRedraw = false;
    }
  }

  endLoop:
  wasTouched = touched;
  delay(8);
}
