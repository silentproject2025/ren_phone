#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

// =============================================
// WIFI CONFIG - GANTI INI
// =============================================
const char* WIFI_SSID     = "SSID_KAMU";
const char* WIFI_PASSWORD = "PASSWORD_KAMU";

// NTP Config (UTC+7)
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 7 * 3600;
const int   DST_OFFSET = 0;

// =============================================
// WARNA (Dark Mode)
// =============================================
#define COL_BG        0x1084   // Abu gelap hampir hitam
#define COL_SURFACE   0x2104   // Surface card
#define COL_ACCENT    0xFD40   // Oranye hangat
#define COL_ACCENT2   0x867F   // Biru muda
#define COL_TEXT      0xFFFF   // Putih
#define COL_SUBTEXT   0x8C51   // Abu muda
#define COL_DIVIDER   0x2965   // Garis pembagi
#define COL_GREEN     0x07E0   // Hijau (WiFi on)
#define COL_RED       0xF800   // Merah

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
Preferences prefs;

// =============================================
// STATE
// =============================================
enum Screen { SCR_HOME, SCR_CLOCK, SCR_CALC, SCR_SETTINGS, SCR_SENSOR };
Screen currentScreen = SCR_HOME;

bool wifiConnected   = false;
int  brightness      = 200;    // 0-255
bool ntpSynced       = false;

// Kalkulator state
String calcInput     = "0";
String calcResult    = "";
float  calcA         = 0;
char   calcOp        = 0;
bool   calcNewNum    = true;

// =============================================
// TOUCH CALIBRATION
// =============================================
void loadOrRunCalibration() {
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
  display.fillScreen(COL_BG);
  display.setTextColor(COL_TEXT);
  display.setTextSize(2);
  display.setCursor(20, 100);
  display.print("WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    tries++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    struct tm t;
    if (getLocalTime(&t, 5000)) ntpSynced = true;
  }
}

// =============================================
// HELPER DRAW
// =============================================
void drawStatusBar() {
  // Background status bar
  display.fillRect(0, 0, 320, 24, COL_SURFACE);

  // Jam
  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    char buf[16];
    sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    display.setTextColor(COL_TEXT);
    display.setTextSize(1);
    display.setCursor(8, 8);
    display.print(buf);
  } else {
    display.setTextColor(COL_SUBTEXT);
    display.setTextSize(1);
    display.setCursor(8, 8);
    display.print("--:--");
  }

  // WiFi icon (kanan)
  if (wifiConnected) {
    // Arc WiFi sederhana
    display.fillCircle(302, 16, 2, COL_GREEN);
    display.drawArc(302, 18, 5, 4, 210, 330, COL_GREEN);
    display.drawArc(302, 18, 9, 8, 210, 330, COL_GREEN);
  } else {
    display.fillCircle(302, 16, 2, COL_RED);
    display.drawLine(298, 12, 306, 20, COL_RED);
  }

  // Garis bawah status bar
  display.drawFastHLine(0, 24, 320, COL_DIVIDER);
}

void drawBackButton() {
  display.fillRoundRect(4, 210, 60, 26, 6, COL_SURFACE);
  display.setTextColor(COL_ACCENT);
  display.setTextSize(1);
  display.setCursor(14, 220);
  display.print("< Back");
}

bool backButtonPressed(int x, int y) {
  return (x >= 4 && x <= 64 && y >= 210 && y <= 236);
}

// =============================================
// HOME SCREEN
// =============================================
struct AppIcon {
  const char* name;
  uint16_t    color;
  uint16_t    symbol; // karakter sederhana
};

AppIcon apps[4] = {
  { "Jam",     COL_ACCENT,  0 },
  { "Kalkulator", COL_ACCENT2, 1 },
  { "Sensor",  0x07FF,      2 },
  { "Setting", 0xF81F,      3 },
};

void drawAppIcon(int x, int y, int idx) {
  uint16_t col = apps[idx].color;

  // Card background
  display.fillRoundRect(x, y, 120, 80, 12, COL_SURFACE);

  // Ikon simbolis (kotak warna + simbol teks)
  display.fillRoundRect(x + 38, y + 10, 44, 38, 8, col);

  display.setTextSize(3);
  display.setTextColor(COL_BG);
  display.setCursor(x + 49, y + 18);
  switch (idx) {
    case 0: display.print("T"); break;   // Jam
    case 1: display.print("+"); break;   // Kalkulator
    case 2: display.print("~"); break;   // Sensor
    case 3: display.print("@"); break;   // Setting
  }

  // Nama app
  display.setTextSize(1);
  display.setTextColor(COL_TEXT);
  int nameX = x + 60 - (strlen(apps[idx].name) * 3);
  display.setCursor(nameX, y + 62);
  display.print(apps[idx].name);
}

void drawHome() {
  display.fillScreen(COL_BG);
  drawStatusBar();

  // Judul
  display.setTextColor(COL_SUBTEXT);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Beranda");

  // Grid 2x2
  // Baris 1
  drawAppIcon(8,   45, 0);  // Jam
  drawAppIcon(136, 45, 1);  // Kalkulator
  // Baris 2
  drawAppIcon(8,   138, 2); // Sensor
  drawAppIcon(136, 138, 3); // Setting
}

Screen homeTouchCheck(int x, int y) {
  // Jam: x 8-128, y 45-125
  if (x >= 8 && x <= 128 && y >= 45 && y <= 125)   return SCR_CLOCK;
  // Kalkulator: x 136-256, y 45-125
  if (x >= 136 && x <= 256 && y >= 45 && y <= 125)  return SCR_CALC;
  // Sensor: x 8-128, y 138-218
  if (x >= 8 && x <= 128 && y >= 138 && y <= 218)   return SCR_SENSOR;
  // Setting: x 136-256, y 138-218
  if (x >= 136 && x <= 256 && y >= 138 && y <= 218) return SCR_SETTINGS;
  return SCR_HOME;
}

// =============================================
// APP: JAM
// =============================================
void drawClock() {
  display.fillScreen(COL_BG);
  drawStatusBar();

  display.setTextColor(COL_ACCENT);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Jam");

  struct tm t;
  if (ntpSynced && getLocalTime(&t)) {
    // Jam besar
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    display.setTextColor(COL_TEXT);
    display.setTextSize(3);
    display.setCursor(28, 70);
    display.print(timeBuf);

    // Tanggal
    char dateBuf[20];
    const char* days[]   = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* months[] = {"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    sprintf(dateBuf, "%s, %02d %s %04d",
      days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year + 1900);
    display.setTextColor(COL_SUBTEXT);
    display.setTextSize(1);
    display.setCursor(28, 115);
    display.print(dateBuf);

    // Garis dekorasi
    display.drawFastHLine(28, 135, 264, COL_ACCENT);

    // NTP status
    display.setTextColor(COL_GREEN);
    display.setCursor(28, 145);
    display.print("NTP Sync OK");
  } else {
    display.setTextColor(COL_RED);
    display.setTextSize(2);
    display.setCursor(28, 80);
    display.print("Tidak sync");
    display.setTextSize(1);
    display.setTextColor(COL_SUBTEXT);
    display.setCursor(28, 110);
    display.print("Cek koneksi WiFi");
  }

  drawBackButton();
}

// =============================================
// APP: KALKULATOR
// =============================================
void drawCalcButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg) {
  display.fillRoundRect(x, y, w, h, 6, bg);
  display.setTextColor(fg);
  display.setTextSize(2);
  int lx = x + w / 2 - strlen(label) * 6;
  int ly = y + h / 2 - 8;
  display.setCursor(lx, ly);
  display.print(label);
}

void drawCalc() {
  display.fillScreen(COL_BG);
  drawStatusBar();

  display.setTextColor(COL_ACCENT);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Kalkulator");

  // Display input
  display.fillRoundRect(4, 40, 312, 40, 6, COL_SURFACE);
  display.setTextColor(COL_TEXT);
  display.setTextSize(2);
  // Right align
  int tw = calcInput.length() * 12;
  display.setCursor(max(8, 310 - tw), 52);
  display.print(calcInput);

  // Tombol
  // Baris 1: C  +/-  %  /
  drawCalcButton(4,   90, 72, 34, "C",   COL_SURFACE, COL_ACCENT);
  drawCalcButton(82,  90, 72, 34, "+/-", COL_SURFACE, COL_ACCENT);
  drawCalcButton(160, 90, 72, 34, "%",   COL_SURFACE, COL_ACCENT);
  drawCalcButton(238, 90, 78, 34, "/",   COL_ACCENT,  COL_BG);

  // Baris 2: 7 8 9 *
  drawCalcButton(4,   130, 72, 34, "7", COL_SURFACE, COL_TEXT);
  drawCalcButton(82,  130, 72, 34, "8", COL_SURFACE, COL_TEXT);
  drawCalcButton(160, 130, 72, 34, "9", COL_SURFACE, COL_TEXT);
  drawCalcButton(238, 130, 78, 34, "x", COL_ACCENT,  COL_BG);

  // Baris 3: 4 5 6 -
  drawCalcButton(4,   170, 72, 34, "4", COL_SURFACE, COL_TEXT);
  drawCalcButton(82,  170, 72, 34, "5", COL_SURFACE, COL_TEXT);
  drawCalcButton(160, 170, 72, 34, "6", COL_SURFACE, COL_TEXT);
  drawCalcButton(238, 170, 78, 34, "-", COL_ACCENT,  COL_BG);

  // Baris 4: 1 2 3 +
  drawCalcButton(4,   210, 72, 34, "1", COL_SURFACE, COL_TEXT);
  drawCalcButton(82,  210, 72, 34, "2", COL_SURFACE, COL_TEXT);
  drawCalcButton(160, 210, 72, 34, "3", COL_SURFACE, COL_TEXT);
  drawCalcButton(238, 210, 78, 34, "+", COL_ACCENT,  COL_BG);

  // Baris 5: 0(lebar) . =
  drawCalcButton(4,   250, 150, 34, "0", COL_SURFACE, COL_TEXT);
  drawCalcButton(160, 250, 72,  34, ".", COL_SURFACE, COL_TEXT);
  drawCalcButton(238, 250, 78,  34, "=", COL_ACCENT2, COL_BG);
}

void calcHandleTouch(int x, int y) {
  // Mapping tombol
  struct CalcBtn { int x, y, w, h; const char* lbl; };
  CalcBtn btns[] = {
    {4,   90,  72, 34, "C"},   {82,  90,  72, 34, "+/-"}, {160, 90,  72, 34, "%"},  {238, 90,  78, 34, "/"},
    {4,   130, 72, 34, "7"},   {82,  130, 72, 34, "8"},   {160, 130, 72, 34, "9"},   {238, 130, 78, 34, "x"},
    {4,   170, 72, 34, "4"},   {82,  170, 72, 34, "5"},   {160, 170, 72, 34, "6"},   {238, 170, 78, 34, "-"},
    {4,   210, 72, 34, "1"},   {82,  210, 72, 34, "2"},   {160, 210, 72, 34, "3"},   {238, 210, 78, 34, "+"},
    {4,   250, 150,34, "0"},   {160, 250, 72, 34, "."},   {238, 250, 78, 34, "="},
  };
  int n = sizeof(btns) / sizeof(btns[0]);

  for (int i = 0; i < n; i++) {
    if (x >= btns[i].x && x <= btns[i].x + btns[i].w &&
        y >= btns[i].y && y <= btns[i].y + btns[i].h) {
      const char* l = btns[i].lbl;

      if (strcmp(l, "C") == 0) {
        calcInput = "0"; calcA = 0; calcOp = 0; calcNewNum = true;

      } else if (strcmp(l, "+/-") == 0) {
        float v = calcInput.toFloat() * -1;
        calcInput = String(v);

      } else if (strcmp(l, "%") == 0) {
        float v = calcInput.toFloat() / 100;
        calcInput = String(v);

      } else if (strcmp(l, "=") == 0) {
        float b = calcInput.toFloat();
        float res = 0;
        if (calcOp == '+') res = calcA + b;
        else if (calcOp == '-') res = calcA - b;
        else if (calcOp == 'x') res = calcA * b;
        else if (calcOp == '/') res = (b != 0) ? calcA / b : 0;
        // Tampilkan integer kalau bulat
        if (res == (int)res) calcInput = String((int)res);
        else calcInput = String(res);
        calcOp = 0; calcNewNum = true;

      } else if (strcmp(l, "+") == 0 || strcmp(l, "-") == 0 ||
                 strcmp(l, "x") == 0 || strcmp(l, "/") == 0) {
        calcA = calcInput.toFloat();
        calcOp = l[0];
        calcNewNum = true;

      } else {
        // Angka atau titik
        if (calcNewNum) { calcInput = ""; calcNewNum = false; }
        if (strcmp(l, ".") == 0 && calcInput.indexOf('.') >= 0) return;
        if (calcInput == "0" && strcmp(l, ".") != 0) calcInput = "";
        calcInput += l;
      }

      // Batasi panjang
      if (calcInput.length() > 10) calcInput = calcInput.substring(0, 10);

      // Redraw display area saja
      display.fillRoundRect(4, 40, 312, 40, 6, COL_SURFACE);
      display.setTextColor(COL_TEXT);
      display.setTextSize(2);
      int tw = calcInput.length() * 12;
      display.setCursor(max(8, 310 - tw), 52);
      display.print(calcInput);
      break;
    }
  }
}

// =============================================
// APP: SENSOR SUHU INTERNAL
// =============================================
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

float readInternalTemp() {
  return (temprature_sens_read() - 32) / 1.8;
}

void drawSensor() {
  display.fillScreen(COL_BG);
  drawStatusBar();

  display.setTextColor(0x07FF);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Sensor");

  float temp = readInternalTemp();

  // Card suhu
  display.fillRoundRect(20, 50, 280, 100, 14, COL_SURFACE);

  display.setTextColor(COL_SUBTEXT);
  display.setTextSize(1);
  display.setCursor(35, 65);
  display.print("Suhu Internal Chip");

  // Nilai suhu besar
  char buf[16];
  sprintf(buf, "%.1f", temp);
  display.setTextColor(0x07FF);
  display.setTextSize(4);
  display.setCursor(50, 85);
  display.print(buf);
  display.setTextSize(2);
  display.print(" C");

  // Bar indikator
  display.fillRoundRect(20, 165, 280, 20, 6, COL_DIVIDER);
  int barW = map((int)temp, 20, 90, 0, 276);
  barW = constrain(barW, 0, 276);
  uint16_t barCol = (temp < 50) ? COL_GREEN : (temp < 70) ? COL_ACCENT : COL_RED;
  display.fillRoundRect(22, 167, barW, 16, 5, barCol);

  display.setTextColor(COL_SUBTEXT);
  display.setTextSize(1);
  display.setCursor(20, 192);
  display.print("Normal < 70C");
  display.setCursor(220, 192);
  display.print("Max 90C");

  // Refresh hint
  display.setTextColor(COL_DIVIDER);
  display.setCursor(80, 205);
  display.print("Sentuh untuk refresh");

  drawBackButton();
}

// =============================================
// APP: SETTINGS
// =============================================
int settingSelected = -1;

void drawSettings() {
  display.fillScreen(COL_BG);
  drawStatusBar();

  display.setTextColor(0xF81F);
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("Pengaturan");

  // Item: Brightness
  display.fillRoundRect(8, 45, 304, 40, 8, COL_SURFACE);
  display.setTextColor(COL_TEXT);
  display.setCursor(18, 55);
  display.print("Kecerahan Layar");
  // Bar brightness
  display.fillRoundRect(18, 68, 220, 10, 4, COL_DIVIDER);
  int bw = map(brightness, 0, 255, 0, 218);
  display.fillRoundRect(19, 69, bw, 8, 3, COL_ACCENT);
  display.setTextColor(COL_SUBTEXT);
  display.setCursor(248, 65);
  char bb[5]; sprintf(bb, "%d%%", brightness * 100 / 255);
  display.print(bb);

  // Item: WiFi status
  display.fillRoundRect(8, 95, 304, 40, 8, COL_SURFACE);
  display.setTextColor(COL_TEXT);
  display.setCursor(18, 105);
  display.print("WiFi");
  display.setCursor(18, 118);
  display.setTextColor(wifiConnected ? COL_GREEN : COL_RED);
  display.print(wifiConnected ? WiFi.SSID().c_str() : "Tidak terhubung");

  // Item: Kalibrasi ulang
  display.fillRoundRect(8, 145, 304, 40, 8, COL_SURFACE);
  display.setTextColor(COL_TEXT);
  display.setCursor(18, 155);
  display.print("Kalibrasi Ulang Touch");
  display.setTextColor(COL_SUBTEXT);
  display.setCursor(18, 168);
  display.print("Tap untuk reset kalibrasi");

  // Item: Reconnect WiFi
  display.fillRoundRect(8, 195, 304, 40, 8, COL_SURFACE);
  display.setTextColor(COL_TEXT);
  display.setCursor(18, 205);
  display.print("Sambung Ulang WiFi");
  display.setTextColor(COL_SUBTEXT);
  display.setCursor(18, 218);
  display.print("Tap untuk reconnect");

  drawBackButton();
}

void settingsHandleTouch(int x, int y) {
  // Brightness bar area
  if (x >= 18 && x <= 238 && y >= 45 && y <= 85) {
    brightness = map(x - 18, 0, 220, 0, 255);
    brightness = constrain(brightness, 10, 255);
    display.setBrightness(brightness);
    drawSettings();
    return;
  }
  // Kalibrasi ulang
  if (x >= 8 && x <= 312 && y >= 145 && y <= 185) {
    prefs.begin("touch_cal", false);
    prefs.putBool("done", false);
    prefs.end();
    ESP.restart();
  }
  // Reconnect WiFi
  if (x >= 8 && x <= 312 && y >= 195 && y <= 235) {
    connectWifi();
    drawSettings();
  }
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);

  display.init();
  display.setRotation(1);
  display.setBrightness(brightness);
  display.fillScreen(COL_BG);

  // Splash screen
  display.setTextColor(COL_ACCENT);
  display.setTextSize(3);
  display.setCursor(60, 90);
  display.print("ESP Phone");
  display.setTextColor(COL_SUBTEXT);
  display.setTextSize(1);
  display.setCursor(100, 130);
  display.print("by ESP32-S3");
  delay(1200);

  // Kalibrasi touch
  if (display.touch()) loadOrRunCalibration();

  // Konek WiFi
  connectWifi();

  // Home
  drawHome();
}

// =============================================
// LOOP
// =============================================
unsigned long lastClockUpdate = 0;
unsigned long lastSensorUpdate = 0;
unsigned long lastTouch = 0;

void loop() {
  lgfx::touch_point_t tp;
  bool touched = display.getTouch(&tp);

  // Debounce touch
  if (touched && millis() - lastTouch > 200) {
    lastTouch = millis();
    int tx = tp.x, ty = tp.y;

    if (currentScreen == SCR_HOME) {
      Screen next = homeTouchCheck(tx, ty);
      if (next != SCR_HOME) {
        currentScreen = next;
        if (currentScreen == SCR_CLOCK)    drawClock();
        if (currentScreen == SCR_CALC)     drawCalc();
        if (currentScreen == SCR_SENSOR)   drawSensor();
        if (currentScreen == SCR_SETTINGS) drawSettings();
      }

    } else {
      // Back button
      if (backButtonPressed(tx, ty)) {
        currentScreen = SCR_HOME;
        drawHome();
        return;
      }
      if (currentScreen == SCR_CALC)     calcHandleTouch(tx, ty);
      if (currentScreen == SCR_SETTINGS) settingsHandleTouch(tx, ty);
      if (currentScreen == SCR_SENSOR)   drawSensor(); // refresh on touch
    }
  }

  // Auto update jam setiap detik
  if (currentScreen == SCR_CLOCK && millis() - lastClockUpdate > 1000) {
    lastClockUpdate = millis();
    drawClock();
  }

  // Auto update sensor setiap 2 detik
  if (currentScreen == SCR_SENSOR && millis() - lastSensorUpdate > 2000) {
    lastSensorUpdate = millis();
    drawSensor();
  }

  // Update status bar jam di home setiap menit
  if (currentScreen == SCR_HOME && millis() - lastClockUpdate > 60000) {
    lastClockUpdate = millis();
    drawStatusBar();
  }
}
