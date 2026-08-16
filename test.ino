// =================================================================
// ren_phone — versi LVGL 8.3.x + LovyanGFX
// File pendukung WAJIB (taruh SEJAJAR file ini, di folder sketch yang sama):
//   - lv_conf.h
//   - build_opt.h   (isi: -DLV_CONF_INCLUDE_SIMPLE)
// Library yang harus diinstall lewat Library Manager: "lvgl" (v8.3.x), "LovyanGFX"
// SD Card: SDIO 1-bit mode -> CLK=39, CMD=38, D0=40
// =================================================================
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "FS.h"
#include "SD_MMC.h"

// =============================================
// WIFI & NTP
// =============================================
char WIFI_SSID[64]     = "";
char WIFI_PASSWORD[64] = "";
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 7 * 3600;
const int   DST_OFFSET = 0;
bool wifiConnected = false;
bool ntpSynced     = false;

// =============================================
// SD CARD (SDIO 1-bit: CLK=39 CMD=38 D0=40)
// =============================================
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40
bool sdReady = false;
const char* NOTE_FILE   = "/notepad.txt";
const char* CANVAS_FILE = "/canvas.bin";

// =============================================
// LGFX DRIVER (panel + touch) — sama seperti versi sebelumnya
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
int brightness = 200;

// =============================================
// JEMBATAN LVGL <-> LovyanGFX
// =============================================
#define LV_BUF_LINES 40   // buffer gambar 320 x 40 baris, cukup & hemat RAM
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* lvBuf1;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

void lvglFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  display.startWrite();
  display.setAddrWindow(area->x1, area->y1, w, h);
  display.writePixels((lgfx::rgb565_t*)color_p, w * h);
  display.endWrite();
  lv_disp_flush_ready(drv);
}

void lvglTouchReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  lgfx::touch_point_t tp;
  bool touched = display.getTouch(&tp);
  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tp.x;
    data->point.y = tp.y;
  }
}

// =============================================
// SISTEM TEMA — 4 preset, disimpan di Preferences
// =============================================
struct AppTheme {
  const char* name;
  lv_color_t bg, surface, surface2, accent, accent2, text, subtext, danger, good;
  bool dark;
};

AppTheme themes[] = {
  { "Dark",   lv_color_hex(0x10141c), lv_color_hex(0x1e2430), lv_color_hex(0x2c3444),
              lv_color_hex(0xffcf40), lv_color_hex(0x08bfff), lv_color_hex(0xffffff),
              lv_color_hex(0x8c94a8), lv_color_hex(0xff4444), lv_color_hex(0x35d07f), true },
  { "AMOLED", lv_color_hex(0x000000), lv_color_hex(0x121212), lv_color_hex(0x1e1e1e),
              lv_color_hex(0xffcf40), lv_color_hex(0x08bfff), lv_color_hex(0xffffff),
              lv_color_hex(0x8c94a8), lv_color_hex(0xff4444), lv_color_hex(0x35d07f), true },
  { "Light",  lv_color_hex(0xf2f3f7), lv_color_hex(0xffffff), lv_color_hex(0xe8eaf0),
              lv_color_hex(0xff8a00), lv_color_hex(0x0077ff), lv_color_hex(0x1a1a1a),
              lv_color_hex(0x6b7280), lv_color_hex(0xd7263d), lv_color_hex(0x1a9c5c), false },
  { "Pastel", lv_color_hex(0xfff0f5), lv_color_hex(0xffffff), lv_color_hex(0xffe0ec),
              lv_color_hex(0xff6f91), lv_color_hex(0x6fb8ff), lv_color_hex(0x3a2e35),
              lv_color_hex(0x9c8a92), lv_color_hex(0xe0507a), lv_color_hex(0x4cbf8f), false },
};
#define THEME_COUNT 4
int currentThemeIdx = 0;
AppTheme& T() { return themes[currentThemeIdx]; }

void saveThemePref() {
  Preferences p; p.begin("ui", false);
  p.putInt("theme", currentThemeIdx);
  p.end();
}
void loadThemePref() {
  Preferences p; p.begin("ui", true);
  currentThemeIdx = p.getInt("theme", 0);
  p.end();
  if (currentThemeIdx < 0 || currentThemeIdx >= THEME_COUNT) currentThemeIdx = 0;
}

// =============================================
// TOUCH CALIBRATION (langsung lewat LGFX, sebelum LVGL aktif)
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
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE); display.setTextSize(2);
    display.setCursor(20, 100); display.println("Kalibrasi Touch");
    display.setTextSize(1);
    display.setCursor(20, 130); display.println("Sentuh tanda di setiap sudut");
    uint16_t calData[8];
    display.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
    prefs.putBytes("data", calData, sizeof(calData));
    prefs.putBool("done", true);

    // ✅ FIX: Bersihkan layar setelah kalibrasi selesai
    display.fillScreen(TFT_BLACK);
    delay(100); // beri waktu layar settle
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
// SD CARD: init + storage notepad & canvas
// =============================================
void initSD() {
  SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0);
  sdReady = SD_MMC.begin("/sdcard", true); // 1-bit mode
  if (!sdReady) Serial.println("SD Card: gagal mount / tidak terpasang");
  else Serial.printf("SD Card: OK, %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
}

String noteText = "";
void loadNoteFromSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(NOTE_FILE, FILE_READ);
  if (f) { noteText = f.readString(); f.close(); }
}
void saveNoteToSD() {
  if (!sdReady) return;
  File f = SD_MMC.open(NOTE_FILE, FILE_WRITE);
  if (f) { f.print(noteText); f.close(); }
}

#define CANVAS_W 320
#define CANVAS_H 190
static lv_color_t* canvasBuf = nullptr; // buffer persisten (PSRAM), bertahan lintas rebuild tema
#define CANVAS_BUF_BYTES ((uint32_t)CANVAS_W * CANVAS_H * sizeof(lv_color_t))

void loadCanvasFromSD() {
  if (!sdReady || !canvasBuf) return;
  File f = SD_MMC.open(CANVAS_FILE, FILE_READ);
  if (f && f.size() == CANVAS_BUF_BYTES) f.read((uint8_t*)canvasBuf, CANVAS_BUF_BYTES);
  if (f) f.close();
}
void saveCanvasToSD() {
  if (!sdReady || !canvasBuf) return;
  File f = SD_MMC.open(CANVAS_FILE, FILE_WRITE);
  if (f) { f.write((uint8_t*)canvasBuf, CANVAS_BUF_BYTES); f.close(); }
}

// =============================================
// STYLE GLOBAL (dibangun ulang tiap ganti tema)
// =============================================
static lv_style_t style_card;
static lv_style_t style_dock;
static lv_style_t style_btn_accent;
static lv_style_t style_btn_neutral;
static lv_style_t style_btn_danger;
static lv_style_t style_scr_bg;

void buildStyles() {
  lv_style_init(&style_scr_bg);
  lv_style_set_bg_color(&style_scr_bg, T().bg);
  lv_style_set_bg_opa(&style_scr_bg, LV_OPA_COVER);
  lv_style_set_border_width(&style_scr_bg, 0);
  lv_obj_report_style_change(&style_scr_bg);

  lv_style_init(&style_card);
  lv_style_set_bg_color(&style_card, T().surface);
  lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
  lv_style_set_radius(&style_card, 14);
  lv_style_set_border_width(&style_card, 0);
  lv_style_set_shadow_width(&style_card, 10);
  lv_style_set_shadow_ofs_y(&style_card, 3);
  lv_style_set_shadow_opa(&style_card, LV_OPA_30);
  lv_style_set_shadow_color(&style_card, lv_color_black());
  lv_style_set_text_color(&style_card, T().text);
  lv_style_set_pad_all(&style_card, 6);
  lv_obj_report_style_change(&style_card);

  lv_style_init(&style_dock);
  lv_style_set_bg_color(&style_dock, T().surface2);
  lv_style_set_bg_opa(&style_dock, LV_OPA_COVER);
  lv_style_set_radius(&style_dock, 16);
  lv_style_set_border_width(&style_dock, 0);
  lv_obj_report_style_change(&style_dock);

  lv_style_init(&style_btn_accent);
  lv_style_set_bg_color(&style_btn_accent, T().accent);
  lv_style_set_bg_opa(&style_btn_accent, LV_OPA_COVER);
  lv_style_set_radius(&style_btn_accent, 8);
  lv_style_set_border_width(&style_btn_accent, 0);
  lv_style_set_text_color(&style_btn_accent, T().bg);
  lv_obj_report_style_change(&style_btn_accent);

  lv_style_init(&style_btn_neutral);
  lv_style_set_bg_color(&style_btn_neutral, T().surface2);
  lv_style_set_bg_opa(&style_btn_neutral, LV_OPA_COVER);
  lv_style_set_radius(&style_btn_neutral, 8);
  lv_style_set_border_width(&style_btn_neutral, 0);
  lv_style_set_text_color(&style_btn_neutral, T().text);
  lv_obj_report_style_change(&style_btn_neutral);

  lv_style_init(&style_btn_danger);
  lv_style_set_bg_color(&style_btn_danger, T().danger);
  lv_style_set_bg_opa(&style_btn_danger, LV_OPA_COVER);
  lv_style_set_radius(&style_btn_danger, 8);
  lv_style_set_border_width(&style_btn_danger, 0);
  lv_style_set_text_color(&style_btn_danger, lv_color_white());
  lv_obj_report_style_change(&style_btn_danger);
}

// =============================================
// STATE / POINTER WIDGET GLOBAL
// =============================================
lv_obj_t* scrHome     = nullptr;
lv_obj_t* scrClock    = nullptr;
lv_obj_t* scrCalc     = nullptr;
lv_obj_t* scrSensor   = nullptr;
lv_obj_t* scrSettings = nullptr;
lv_obj_t* scrNotepad  = nullptr;
lv_obj_t* scrCanvas   = nullptr;

lv_obj_t* lblStatusTime = nullptr;
lv_obj_t* lblStatusWifi = nullptr;
lv_obj_t* lblStatusSD   = nullptr;

lv_obj_t* lblHomeClock  = nullptr;
lv_obj_t* lblClockBig   = nullptr;
lv_obj_t* lblClockDate  = nullptr;

lv_obj_t* lblCalcDisplay = nullptr;
String calcInput = "0";
float  calcA = 0;
char   calcOp = 0;
bool   calcNewNum = true;

lv_obj_t* barSensor    = nullptr;
lv_obj_t* lblSensorVal = nullptr;

lv_obj_t* sliderBright  = nullptr;
lv_obj_t* lblBrightVal  = nullptr;
lv_obj_t* taSSID        = nullptr;
lv_obj_t* taPass        = nullptr;
lv_obj_t* lblWifiStat   = nullptr;
lv_obj_t* ddTheme       = nullptr;
lv_obj_t* kbSettings    = nullptr;

lv_obj_t* taNotepad  = nullptr;
lv_obj_t* kbNotepad  = nullptr;

lv_obj_t* canvasWidget = nullptr;
lv_obj_t* lblBrush     = nullptr;
lv_color_t drawColor;
int brushSize = 4;
bool canvasHasLast = false;
lv_point_t canvasLastPt;

// =============================================
// TOAST
// =============================================
void toastTimerCb(lv_timer_t* timer) {
  lv_obj_t* toast = (lv_obj_t*)timer->user_data;
  if (toast) lv_obj_del(toast);
  lv_timer_del(timer);
}
void showToast(const char* msg, bool isError = false) {
  lv_obj_t* toast = lv_label_create(lv_layer_top());
  lv_obj_set_style_bg_color(toast, isError ? T().danger : T().accent2, 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
  lv_obj_set_style_text_color(toast, isError ? lv_color_white() : T().bg, 0);
  lv_obj_set_style_radius(toast, 8, 0);
  lv_obj_set_style_pad_all(toast, 8, 0);
  lv_label_set_text(toast, msg);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_timer_create(toastTimerCb, 1400, toast);
}

// =============================================
// STATUS BAR (di lv_layer_top -> otomatis nempel di semua layar)
// =============================================
void buildStatusBar() {
  lv_obj_clean(lv_layer_top());
  lv_obj_t* bar = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 320, 22);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, T().surface, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lblStatusTime = lv_label_create(bar);
  lv_obj_set_style_text_color(lblStatusTime, T().text, 0);
  lv_label_set_text(lblStatusTime, "--:--");
  lv_obj_align(lblStatusTime, LV_ALIGN_LEFT_MID, 8, 0);

  lblStatusSD = lv_label_create(bar);
  lv_obj_set_style_text_color(lblStatusSD, T().good, 0);
  lv_label_set_text(lblStatusSD, sdReady ? "SD" : "");
  lv_obj_align(lblStatusSD, LV_ALIGN_RIGHT_MID, -34, 0);

  lblStatusWifi = lv_label_create(bar);
  lv_obj_set_style_text_color(lblStatusWifi, wifiConnected ? T().good : T().danger, 0);
  lv_label_set_text(lblStatusWifi, wifiConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
  lv_obj_align(lblStatusWifi, LV_ALIGN_RIGHT_MID, -8, 0);
}

void statusTimerCb(lv_timer_t* timer) {
  struct tm t;
  bool ok = ntpSynced && getLocalTime(&t);
  if (lblStatusTime) {
    char buf[9];
    if (ok) sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
    else strcpy(buf, "--:--");
    lv_label_set_text(lblStatusTime, buf);
  }
  if (lblStatusWifi) {
    lv_obj_set_style_text_color(lblStatusWifi, wifiConnected ? T().good : T().danger, 0);
    lv_label_set_text(lblStatusWifi, wifiConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
  }
  if (lblStatusSD) lv_label_set_text(lblStatusSD, sdReady ? "SD" : "");
  if (lblHomeClock && ok) {
    char buf2[9]; sprintf(buf2, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(lblHomeClock, buf2);
  }
  if (lblClockBig && ok) {
    char tb[9]; sprintf(tb, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    lv_label_set_text(lblClockBig, tb);
    const char* days[]   = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
    const char* months[] = {"Jan","Feb","Mar","Apr","Mei","Jun","Jul","Agu","Sep","Okt","Nov","Des"};
    char db[28];
    sprintf(db, "%s, %02d %s %04d", days[t.tm_wday], t.tm_mday, months[t.tm_mon], t.tm_year+1900);
    lv_label_set_text(lblClockDate, db);
  }
}

void sensorTimerCb(lv_timer_t* timer) {
  if (!barSensor) return;
  float temp = temperatureRead();
  lv_bar_set_value(barSensor, (int)temp, LV_ANIM_ON);
  char buf[16]; sprintf(buf, "%.1f C", temp);
  lv_label_set_text(lblSensorVal, buf);
}

// =============================================
// NAVIGASI
// =============================================
void goHome() {
  if (scrNotepad && lv_scr_act() == scrNotepad) { noteText = String(lv_textarea_get_text(taNotepad)); saveNoteToSD(); }
  if (scrCanvas  && lv_scr_act() == scrCanvas)  saveCanvasToSD();
  lv_scr_load_anim(scrHome, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 180, 0, false);
}
void openApp(lv_obj_t* scr) {
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 180, 0, false);
}

// Tombol back generik dipasang di tiap app (kecuali Canvas, dia punya sendiri di toolbar)
lv_obj_t* makeBackBtn(lv_obj_t* parent) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_add_style(btn, &style_btn_neutral, 0);
  lv_obj_set_size(btn, 64, 26);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
  lv_obj_set_style_text_color(lbl, T().accent, 0);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, [](lv_event_t* e){ goHome(); }, LV_EVENT_CLICKED, nullptr);
  return btn;
}

lv_obj_t* makeTitle(lv_obj_t* parent, const char* text, lv_color_t color) {
  lv_obj_t* lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 26);
  return lbl;
}

// =============================================
// APP: JAM
// =============================================
void buildClockScreen() {
  scrClock = lv_obj_create(NULL);
  lv_obj_add_style(scrClock, &style_scr_bg, 0);
  lv_obj_clear_flag(scrClock, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrClock, "Jam", T().accent);

  lblClockBig = lv_label_create(scrClock);
  lv_obj_set_style_text_color(lblClockBig, T().text, 0);
  lv_obj_set_style_text_font(lblClockBig, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblClockBig, "--:--:--");
  lv_obj_align(lblClockBig, LV_ALIGN_TOP_LEFT, 20, 60);

  lblClockDate = lv_label_create(scrClock);
  lv_obj_set_style_text_color(lblClockDate, T().subtext, 0);
  lv_label_set_text(lblClockDate, "-");
  lv_obj_align(lblClockDate, LV_ALIGN_TOP_LEFT, 20, 100);

  lv_obj_t* sync = lv_label_create(scrClock);
  lv_obj_set_style_text_color(sync, ntpSynced ? T().good : T().danger, 0);
  lv_label_set_text(sync, ntpSynced ? "NTP Sync OK" : "Tidak sync");
  lv_obj_align(sync, LV_ALIGN_TOP_LEFT, 20, 122);

  makeBackBtn(scrClock);
}

// =============================================
// APP: KALKULATOR (lv_btnmatrix)
// =============================================
static const char* calc_map[] = {
  "C", "+/-", "%", "/", "\n",
  "7", "8", "9", "x", "\n",
  "4", "5", "6", "-", "\n",
  "1", "2", "3", "+", "\n",
  "0", ".", "=", ""
};

void calcBtnEventCb(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target(e);
  uint16_t id = lv_btnmatrix_get_selected_btn(obj);
  const char* l = lv_btnmatrix_get_btn_text(obj, id);
  if (!l) return;

  if (!strcmp(l,"C"))        { calcInput="0"; calcA=0; calcOp=0; calcNewNum=true; }
  else if (!strcmp(l,"+/-")) { calcInput=String(calcInput.toFloat()*-1); }
  else if (!strcmp(l,"%"))   { calcInput=String(calcInput.toFloat()/100); }
  else if (!strcmp(l,"="))   {
    float b2=calcInput.toFloat(), res=0;
    if(calcOp=='+') res=calcA+b2; else if(calcOp=='-') res=calcA-b2;
    else if(calcOp=='x') res=calcA*b2;
    else if(calcOp=='/') res=(b2!=0)?calcA/b2:0;
    calcInput=(res==(int)res)?String((int)res):String(res,4);
    calcOp=0; calcNewNum=true;
  }
  else if (!strcmp(l,"+")||!strcmp(l,"-")||!strcmp(l,"x")||!strcmp(l,"/")) {
    calcA=calcInput.toFloat(); calcOp=l[0]; calcNewNum=true;
  } else {
    if (calcNewNum) { calcInput=""; calcNewNum=false; }
    if (!strcmp(l,".") && calcInput.indexOf('.')>=0) return;
    if (calcInput=="0" && strcmp(l,".")!=0) calcInput="";
    calcInput+=l;
  }
  if (calcInput.length()>12) calcInput=calcInput.substring(0,12);
  lv_label_set_text(lblCalcDisplay, calcInput.c_str());
}

void buildCalcScreen() {
  scrCalc = lv_obj_create(NULL);
  lv_obj_add_style(scrCalc, &style_scr_bg, 0);
  lv_obj_clear_flag(scrCalc, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrCalc, "Kalkulator", T().accent);

  lv_obj_t* disp = lv_obj_create(scrCalc);
  lv_obj_add_style(disp, &style_card, 0);
  lv_obj_set_size(disp, 304, 34);
  lv_obj_align(disp, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_clear_flag(disp, LV_OBJ_FLAG_SCROLLABLE);

  lblCalcDisplay = lv_label_create(disp);
  lv_obj_set_style_text_color(lblCalcDisplay, T().text, 0);
  lv_obj_set_style_text_font(lblCalcDisplay, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblCalcDisplay, calcInput.c_str());
  lv_obj_align(lblCalcDisplay, LV_ALIGN_RIGHT_MID, -4, 0);

  lv_obj_t* bm = lv_btnmatrix_create(scrCalc);
  lv_btnmatrix_set_map(bm, calc_map);
  lv_obj_set_size(bm, 304, 152);
  lv_obj_align(bm, LV_ALIGN_TOP_MID, 0, 82);
  lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_pad_all(bm, 2, 0);
  lv_obj_set_style_radius(bm, 8, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, T().surface, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, T().text, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, T().accent, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(bm, calcBtnEventCb, LV_EVENT_VALUE_CHANGED, nullptr);

  makeBackBtn(scrCalc);
}

// =============================================
// APP: SENSOR
// =============================================
void buildSensorScreen() {
  scrSensor = lv_obj_create(NULL);
  lv_obj_add_style(scrSensor, &style_scr_bg, 0);
  lv_obj_clear_flag(scrSensor, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrSensor, "Sensor", T().accent2);

  lv_obj_t* card = lv_obj_create(scrSensor);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_size(card, 280, 100);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 46);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl = lv_label_create(card);
  lv_obj_set_style_text_color(lbl, T().subtext, 0);
  lv_label_set_text(lbl, "Suhu Internal Chip");
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 6, 4);

  lblSensorVal = lv_label_create(card);
  lv_obj_set_style_text_color(lblSensorVal, T().accent2, 0);
  lv_obj_set_style_text_font(lblSensorVal, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblSensorVal, "--.- C");
  lv_obj_align(lblSensorVal, LV_ALIGN_TOP_LEFT, 6, 24);

  barSensor = lv_bar_create(card);
  lv_bar_set_range(barSensor, 20, 90);
  lv_obj_set_size(barSensor, 260, 14);
  lv_obj_align(barSensor, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(barSensor, T().surface2, 0);
  lv_obj_set_style_bg_color(barSensor, T().accent2, LV_PART_INDICATOR);

  makeBackBtn(scrSensor);
}

// =============================================
// APP: SETTINGS
// =============================================
void themeDropdownCb(lv_event_t* e) {
  lv_obj_t* dd = lv_event_get_target(e);
  currentThemeIdx = lv_dropdown_get_selected(dd);
  saveThemePref();
  extern void rebuildUI();
  rebuildUI();
  showToast("Tema diganti");
}

void brightSliderCb(lv_event_t* e) {
  lv_obj_t* s = lv_event_get_target(e);
  brightness = lv_slider_get_value(s);
  display.setBrightness(brightness);
  char buf[8]; sprintf(buf, "%d%%", brightness*100/255);
  lv_label_set_text(lblBrightVal, buf);
}

void taFocusCb(lv_event_t* e) {
  lv_obj_t* ta = lv_event_get_target(e);
  lv_keyboard_set_textarea(kbSettings, ta);
  lv_obj_clear_flag(kbSettings, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(kbSettings);
}
void kbSettingsCb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kbSettings, LV_OBJ_FLAG_HIDDEN);
  }
}
void connectBtnCb(lv_event_t* e) {
  String s = lv_textarea_get_text(taSSID);
  String p = lv_textarea_get_text(taPass);
  s.toCharArray(WIFI_SSID, 64);
  p.toCharArray(WIFI_PASSWORD, 64);
  saveWifiCreds();
  showToast("Menghubungkan...");
  connectWifi();
  lv_label_set_text(lblWifiStat, wifiConnected ? WiFi.SSID().c_str() : "Gagal connect");
  lv_obj_set_style_text_color(lblWifiStat, wifiConnected ? T().good : T().danger, 0);
}
void calibrateBtnCb(lv_event_t* e) {
  Preferences prefs; prefs.begin("touch_cal", false);
  prefs.putBool("done", false); prefs.end();
  ESP.restart();
}

void buildSettingsScreen() {
  scrSettings = lv_obj_create(NULL);
  lv_obj_add_style(scrSettings, &style_scr_bg, 0);
  lv_obj_clear_flag(scrSettings, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrSettings, "Pengaturan", T().good);

  // Kecerahan
  lv_obj_t* cardB = lv_obj_create(scrSettings);
  lv_obj_add_style(cardB, &style_card, 0);
  lv_obj_set_size(cardB, 304, 40);
  lv_obj_align(cardB, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_clear_flag(cardB, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lblB = lv_label_create(cardB);
  lv_label_set_text(lblB, "Kecerahan");
  lv_obj_set_style_text_color(lblB, T().text, 0);
  lv_obj_align(lblB, LV_ALIGN_TOP_LEFT, 2, 0);
  sliderBright = lv_slider_create(cardB);
  lv_obj_set_size(sliderBright, 220, 8);
  lv_obj_align(sliderBright, LV_ALIGN_BOTTOM_LEFT, 2, -2);
  lv_slider_set_range(sliderBright, 10, 255);
  lv_slider_set_value(sliderBright, brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sliderBright, T().accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sliderBright, T().accent, LV_PART_KNOB);
  lv_obj_add_event_cb(sliderBright, brightSliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lblBrightVal = lv_label_create(cardB);
  char bb[8]; sprintf(bb, "%d%%", brightness*100/255);
  lv_label_set_text(lblBrightVal, bb);
  lv_obj_set_style_text_color(lblBrightVal, T().subtext, 0);
  lv_obj_align(lblBrightVal, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

  // Tema
  lv_obj_t* cardT = lv_obj_create(scrSettings);
  lv_obj_add_style(cardT, &style_card, 0);
  lv_obj_set_size(cardT, 304, 36);
  lv_obj_align(cardT, LV_ALIGN_TOP_MID, 0, 84);
  lv_obj_clear_flag(cardT, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lblT = lv_label_create(cardT);
  lv_label_set_text(lblT, "Tema");
  lv_obj_set_style_text_color(lblT, T().text, 0);
  lv_obj_align(lblT, LV_ALIGN_LEFT_MID, 2, 0);
  ddTheme = lv_dropdown_create(cardT);
  lv_dropdown_set_options(ddTheme, "Dark\nAMOLED\nLight\nPastel");
  lv_dropdown_set_selected(ddTheme, currentThemeIdx);
  lv_obj_set_size(ddTheme, 140, 26);
  lv_obj_align(ddTheme, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(ddTheme, themeDropdownCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // SSID
  taSSID = lv_textarea_create(scrSettings);
  lv_textarea_set_one_line(taSSID, true);
  lv_textarea_set_placeholder_text(taSSID, "SSID WiFi");
  lv_textarea_set_text(taSSID, WIFI_SSID);
  lv_obj_set_size(taSSID, 304, 30);
  lv_obj_align(taSSID, LV_ALIGN_TOP_MID, 0, 126);
  lv_obj_add_event_cb(taSSID, taFocusCb, LV_EVENT_FOCUSED, nullptr);

  // Password
  taPass = lv_textarea_create(scrSettings);
  lv_textarea_set_one_line(taPass, true);
  lv_textarea_set_password_mode(taPass, true);
  lv_textarea_set_placeholder_text(taPass, "Password");
  lv_textarea_set_text(taPass, WIFI_PASSWORD);
  lv_obj_set_size(taPass, 304, 30);
  lv_obj_align(taPass, LV_ALIGN_TOP_MID, 0, 162);
  lv_obj_add_event_cb(taPass, taFocusCb, LV_EVENT_FOCUSED, nullptr);

  // Tombol connect + status
  lv_obj_t* btnConn = lv_btn_create(scrSettings);
  lv_obj_add_style(btnConn, &style_btn_accent, 0);
  lv_obj_set_size(btnConn, 120, 28);
  lv_obj_align(btnConn, LV_ALIGN_TOP_LEFT, 8, 198);
  lv_obj_t* lblConn = lv_label_create(btnConn);
  lv_label_set_text(lblConn, "Sambungkan");
  lv_obj_center(lblConn);
  lv_obj_add_event_cb(btnConn, connectBtnCb, LV_EVENT_CLICKED, nullptr);

  lblWifiStat = lv_label_create(scrSettings);
  lv_label_set_text(lblWifiStat, wifiConnected ? WiFi.SSID().c_str() : "Tdk terhubung");
  lv_obj_set_style_text_color(lblWifiStat, wifiConnected ? T().good : T().danger, 0);
  lv_obj_align(lblWifiStat, LV_ALIGN_TOP_LEFT, 136, 206);

  // Kalibrasi ulang
  lv_obj_t* btnCal = lv_btn_create(scrSettings);
  lv_obj_add_style(btnCal, &style_btn_neutral, 0);
  lv_obj_set_size(btnCal, 150, 26);
  lv_obj_align(btnCal, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  lv_obj_t* lblCal = lv_label_create(btnCal);
  lv_label_set_text(lblCal, "Kalibrasi Ulang");
  lv_obj_set_style_text_color(lblCal, T().text, 0);
  lv_obj_center(lblCal);
  lv_obj_add_event_cb(btnCal, calibrateBtnCb, LV_EVENT_CLICKED, nullptr);

  makeBackBtn(scrSettings);

  // Keyboard (hidden by default)
  kbSettings = lv_keyboard_create(scrSettings);
  lv_obj_add_flag(kbSettings, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kbSettings, kbSettingsCb, LV_EVENT_ALL, nullptr);
}

// =============================================
// APP: NOTEPAD
// =============================================
void notepadHapusCb(lv_event_t* e) {
  lv_textarea_set_text(taNotepad, "");
  noteText = "";
  saveNoteToSD();
  showToast("Catatan dihapus");
}
void taNotepadFocusCb(lv_event_t* e) {
  lv_keyboard_set_textarea(kbNotepad, taNotepad);
  lv_obj_clear_flag(kbNotepad, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(kbNotepad);
}
void kbNotepadCb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kbNotepad, LV_OBJ_FLAG_HIDDEN);
    noteText = String(lv_textarea_get_text(taNotepad));
    saveNoteToSD();
  }
}

void buildNotepadScreen() {
  scrNotepad = lv_obj_create(NULL);
  lv_obj_add_style(scrNotepad, &style_scr_bg, 0);
  lv_obj_clear_flag(scrNotepad, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrNotepad, "Notepad", T().accent);

  lv_obj_t* btnHapus = lv_btn_create(scrNotepad);
  lv_obj_add_style(btnHapus, &style_btn_danger, 0);
  lv_obj_set_size(btnHapus, 70, 22);
  lv_obj_align(btnHapus, LV_ALIGN_TOP_RIGHT, -6, 24);
  lv_obj_t* lblH = lv_label_create(btnHapus);
  lv_label_set_text(lblH, "Hapus");
  lv_obj_center(lblH);
  lv_obj_add_event_cb(btnHapus, notepadHapusCb, LV_EVENT_CLICKED, nullptr);

  taNotepad = lv_textarea_create(scrNotepad);
  lv_textarea_set_text(taNotepad, noteText.c_str());
  lv_obj_set_size(taNotepad, 304, 130);
  lv_obj_align(taNotepad, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_add_event_cb(taNotepad, taNotepadFocusCb, LV_EVENT_FOCUSED, nullptr);

  makeBackBtn(scrNotepad);

  kbNotepad = lv_keyboard_create(scrNotepad);
  lv_obj_add_flag(kbNotepad, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kbNotepad, kbNotepadCb, LV_EVENT_ALL, nullptr);
}

// =============================================
// APP: CANVAS (lv_canvas)
// =============================================
lv_color_t canvasPalette[] = {
  lv_color_white(), lv_color_hex(0xff4444), lv_color_hex(0x35d07f), lv_color_hex(0x08bfff),
  lv_color_hex(0xffe066), lv_color_hex(0x00e5ff), lv_color_hex(0xff4de6), lv_color_hex(0xffcf40)
};
#define CANVAS_PAL_COUNT 8

void canvasPressCb(lv_event_t* e) {
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p; lv_indev_get_point(indev, &p);
  lv_area_t coords; lv_obj_get_coords(canvasWidget, &coords);
  int lx = p.x - coords.x1;
  int ly = p.y - coords.y1;
  if (lx < 0 || ly < 0 || lx >= CANVAS_W || ly >= CANVAS_H) return;

  if (canvasHasLast) {
    lv_point_t pts[2] = { canvasLastPt, {(lv_coord_t)lx, (lv_coord_t)ly} };
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = drawColor;
    dsc.width = brushSize;
    dsc.round_start = 1; dsc.round_end = 1;
    lv_canvas_draw_line(canvasWidget, pts, 2, &dsc);
  } else {
    for (int dx = -brushSize/2; dx <= brushSize/2; dx++)
      for (int dy = -brushSize/2; dy <= brushSize/2; dy++)
        if (dx*dx+dy*dy <= (brushSize/2)*(brushSize/2)) {
          int px = lx+dx, py = ly+dy;
          if (px>=0 && py>=0 && px<CANVAS_W && py<CANVAS_H)
            lv_canvas_set_px_color(canvasWidget, px, py, drawColor);
        }
  }
  canvasLastPt.x = lx; canvasLastPt.y = ly;
  canvasHasLast = true;
}
void canvasReleaseCb(lv_event_t* e) { canvasHasLast = false; }

void canvasClearCb(lv_event_t* e) {
  lv_canvas_fill_bg(canvasWidget, T().bg, LV_OPA_COVER);
  saveCanvasToSD();
  showToast("Canvas dibersihkan");
}
void canvasBrushCb(lv_event_t* e) {
  brushSize = (brushSize % 10) + 2;
  char buf[8]; sprintf(buf, "B:%d", brushSize);
  lv_label_set_text(lblBrush, buf);
}
void canvasBackCb(lv_event_t* e) { saveCanvasToSD(); goHome(); }

void buildCanvasScreen() {
  scrCanvas = lv_obj_create(NULL);
  lv_obj_add_style(scrCanvas, &style_scr_bg, 0);
  lv_obj_clear_flag(scrCanvas, LV_OBJ_FLAG_SCROLLABLE);
  makeTitle(scrCanvas, "Canvas", T().good);

  canvasWidget = lv_canvas_create(scrCanvas);
  lv_canvas_set_buffer(canvasWidget, canvasBuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(canvasWidget, 0, 26);
  lv_obj_add_flag(canvasWidget, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(canvasWidget, canvasPressCb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(canvasWidget, canvasReleaseCb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(canvasWidget, canvasReleaseCb, LV_EVENT_PRESS_LOST, nullptr);

  // Toolbar bawah
  lv_obj_t* toolbar = lv_obj_create(scrCanvas);
  lv_obj_add_style(toolbar, &style_dock, 0);
  lv_obj_set_size(toolbar, 320, 24);
  lv_obj_set_pos(toolbar, 0, 216);
  lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < CANVAS_PAL_COUNT; i++) {
    lv_obj_t* sw = lv_obj_create(toolbar);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 18, 18);
    lv_obj_set_pos(sw, 3 + i*22, 3);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sw, canvasPalette[i], 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sw, [](lv_event_t* e){
      int idx = (int)(intptr_t)lv_event_get_user_data(e);
      drawColor = canvasPalette[idx];
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  lblBrush = lv_label_create(toolbar);
  char bb[8]; sprintf(bb, "B:%d", brushSize);
  lv_label_set_text(lblBrush, bb);
  lv_obj_set_style_text_color(lblBrush, T().text, 0);
  lv_obj_align(lblBrush, LV_ALIGN_RIGHT_MID, -70, 0);
  lv_obj_add_flag(lblBrush, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(lblBrush, canvasBrushCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btnClr = lv_btn_create(toolbar);
  lv_obj_add_style(btnClr, &style_btn_danger, 0);
  lv_obj_set_size(btnClr, 44, 20);
  lv_obj_align(btnClr, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_t* lblClr = lv_label_create(btnClr);
  lv_label_set_text(lblClr, "CLR");
  lv_obj_center(lblClr);
  lv_obj_add_event_cb(btnClr, canvasClearCb, LV_EVENT_CLICKED, nullptr);

  // Back untuk canvas (posisi khusus, di atas toolbar)
  lv_obj_t* btnBack = lv_btn_create(scrCanvas);
  lv_obj_add_style(btnBack, &style_btn_neutral, 0);
  lv_obj_set_size(btnBack, 50, 22);
  lv_obj_set_pos(btnBack, 3, 3);
  lv_obj_t* lblBk = lv_label_create(btnBack);
  lv_label_set_text(lblBk, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lblBk, T().accent, 0);
  lv_obj_center(lblBk);
  lv_obj_add_event_cb(btnBack, canvasBackCb, LV_EVENT_CLICKED, nullptr);

  drawColor = T().text;
}

// =============================================
// HOME SCREEN — clock widget + grid app + dock
// =============================================
struct AppDef { const char* name; char sym; lv_color_t color; };
AppDef appDefs[6] = {
  { "Jam",        'T', {0} },
  { "Kalkulator", '+', {0} },
  { "Sensor",     '~', {0} },
  { "Setting",    '@', {0} },
  { "Notepad",    'N', {0} },
  { "Canvas",     'C', {0} },
};

void homeAppClickCb(lv_event_t* e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t* targets[6] = { scrClock, scrCalc, scrSensor, scrSettings, scrNotepad, scrCanvas };
  if (idx == 5) loadCanvasFromSD(); // pastikan canvas termuat sebelum dibuka (aman dipanggil berkali-kali)
  openApp(targets[idx]);
}

lv_obj_t* makeAppIcon(lv_obj_t* parent, int idx, int w, int h) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_add_style(card, &style_card, 0);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, homeAppClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

  lv_obj_t* icon = lv_obj_create(card);
  lv_obj_remove_style_all(icon);
  lv_obj_set_size(icon, 30, 30);
  lv_obj_set_style_radius(icon, 8, 0);
  lv_obj_set_style_bg_color(icon, appDefs[idx].color, 0);
  lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* symLbl = lv_label_create(icon);
  char s[2] = { appDefs[idx].sym, 0 };
  lv_label_set_text(symLbl, s);
  lv_obj_set_style_text_color(symLbl, T().bg, 0);
  lv_obj_center(symLbl);

  lv_obj_t* nameLbl = lv_label_create(card);
  lv_label_set_text(nameLbl, appDefs[idx].name);
  lv_obj_set_style_text_color(nameLbl, T().text, 0);
  lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
  lv_obj_align(nameLbl, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_clear_flag(nameLbl, LV_OBJ_FLAG_CLICKABLE);

  return card;
}

void buildHomeScreen() {
  appDefs[0].color = T().accent;   appDefs[1].color = T().accent2;
  appDefs[2].color = lv_color_hex(0x08bfff); appDefs[3].color = lv_color_hex(0xff4de6);
  appDefs[4].color = lv_color_hex(0xffe066); appDefs[5].color = T().good;

  scrHome = lv_obj_create(NULL);
  lv_obj_add_style(scrHome, &style_scr_bg, 0);
  lv_obj_clear_flag(scrHome, LV_OBJ_FLAG_SCROLLABLE);

  // Widget jam digital besar di atas
  lblHomeClock = lv_label_create(scrHome);
  lv_obj_set_style_text_font(lblHomeClock, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblHomeClock, T().text, 0);
  lv_label_set_text(lblHomeClock, "--:--");
  lv_obj_align(lblHomeClock, LV_ALIGN_TOP_MID, 0, 28);

  // Grid app (3 kolom x 2 baris)
  lv_obj_t* grid = lv_obj_create(scrHome);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, 312, 108);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 6; i++) makeAppIcon(grid, i, 96, 50);

  // Dock bawah (favorit tetap): Jam, Notepad, Canvas, Settings
  lv_obj_t* dock = lv_obj_create(scrHome);
  lv_obj_add_style(dock, &style_dock, 0);
  lv_obj_set_size(dock, 300, 40);
  lv_obj_align(dock, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
  int dockApps[4] = {0, 4, 5, 3};
  for (int i = 0; i < 4; i++) {
    int idx = dockApps[i];
    lv_obj_t* btn = lv_obj_create(dock);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 34, 34);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, appDefs[idx].color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_pos(btn, 10 + i*70, 3);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, homeAppClickCb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    lv_obj_t* l = lv_label_create(btn);
    char s[2] = { appDefs[idx].sym, 0 };
    lv_label_set_text(l, s);
    lv_obj_set_style_text_color(l, T().bg, 0);
    lv_obj_center(l);
  }
}

// =============================================
// REBUILD SEMUA LAYAR (dipanggil saat ganti tema)
// =============================================
void rebuildUI() {
  lv_obj_t* oldHome=scrHome, *oldClock=scrClock, *oldCalc=scrCalc, *oldSensor=scrSensor;
  lv_obj_t* oldSettings=scrSettings, *oldNotepad=scrNotepad, *oldCanvas=scrCanvas;

  // Simpan state teks notepad sebelum layar lama dihapus
  if (taNotepad) noteText = String(lv_textarea_get_text(taNotepad));

  buildStyles();
  buildHomeScreen();
  buildClockScreen();
  buildCalcScreen();
  buildSensorScreen();
  buildSettingsScreen();
  buildNotepadScreen();
  buildCanvasScreen();
  buildStatusBar();

  lv_scr_load(scrHome);

  if (oldHome) lv_obj_del(oldHome);
  if (oldClock) lv_obj_del(oldClock);
  if (oldCalc) lv_obj_del(oldCalc);
  if (oldSensor) lv_obj_del(oldSensor);
  if (oldSettings) lv_obj_del(oldSettings);
  if (oldNotepad) lv_obj_del(oldNotepad);
  if (oldCanvas) lv_obj_del(oldCanvas);
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(1);      // ✅ Rotasi dulu sebelum kalibrasi
  display.setBrightness(brightness);

  if (display.touch()) loadOrRunCalibration();

  loadThemePref();
  initSD();
  loadWifiCreds();
  loadNoteFromSD();
  connectWifi();

  // Buffer canvas persisten di PSRAM
  canvasBuf = (lv_color_t*)heap_caps_malloc(CANVAS_BUF_BYTES, MALLOC_CAP_SPIRAM);
  if (canvasBuf) {
    for (uint32_t i = 0; i < (uint32_t)CANVAS_W*CANVAS_H; i++) canvasBuf[i] = T().bg;
    loadCanvasFromSD();
  }

  // Init LVGL
  lv_init();
  lvBuf1 = (lv_color_t*)heap_caps_malloc(320 * LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, lvBuf1, NULL, 320 * LV_BUF_LINES);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 320;
  disp_drv.ver_res = 240;
  disp_drv.flush_cb = lvglFlushCb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvglTouchReadCb;
  lv_indev_drv_register(&indev_drv);

  buildStyles();
  buildHomeScreen();
  buildClockScreen();
  buildCalcScreen();
  buildSensorScreen();
  buildSettingsScreen();
  buildNotepadScreen();
  buildCanvasScreen();
  buildStatusBar();
  lv_scr_load(scrHome);

  // ✅ Paksa LVGL render frame pertama segera
  lv_timer_handler();
  lv_timer_handler();
}

// =============================================
// LOOP
// =============================================
unsigned long lastTick = 0;
void loop() {
  unsigned long now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  delay(5);
}
