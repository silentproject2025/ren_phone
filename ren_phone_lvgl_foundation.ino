// =================================================================
// ren_phone — LVGL FOUNDATION TEST (Fase 1 migrasi ke LVGL)
// =================================================================
// TUJUAN SKETCH INI:
//   Sketch BERDIRI SENDIRI (bukan bagian dari firmware utama v77).
//   Cuma membuktikan 1 hal: jembatan LovyanGFX <-> LVGL 9.x jalan
//   dengan benar di hardware ren_phone -- display nyala lewat LVGL,
//   dan sentuhan XPT2046 kebaca dan sinkron posisinya (dicoba dengan
//   menggeser titik biru pakai jari).
//
//   Kelas LGFX di bawah ini SAMA PERSIS dengan yang ada di firmware
//   utama (v77 baris ~903) -- pin, frekuensi SPI, kalibrasi light,
//   semuanya identik. Setelah sketch ini kebukti jalan di HW asli,
//   baru kita mulai porting layar per layar (Lock Screen dulu, dst)
//   ke firmware utama.
//
// CARA PAKAI:
//   1. Install library "lvgl" versi 9.5.x lewat Library Manager
//      Arduino IDE (jangan versi 8.x, API-nya beda total).
//   2. Di folder Arduino/libraries, copy lvgl/lv_conf_template.h
//      jadi lv_conf.h (sejajar dgn folder lvgl/, BUKAN di dalamnya).
//   3. Buka lv_conf.h, cuma 2 baris wajib diubah:
//        - baris paling atas: #if 0  ->  #if 1
//        - #define LV_COLOR_DEPTH 32   ->  16
//      (opsional: LV_MEM_SIZE naikkan ke (64 * 1024U) biar lega)
//   4. Board settings SAMA seperti firmware utama: ESP32S3 Dev
//      Module, Flash 16MB, PSRAM: OPI PSRAM, PartitionScheme
//      app3M_fat9M_16MB.
//   5. Upload, lalu ikuti kalibrasi touch 3 titik yg muncul sekali
//      di awal. Habis itu coba geser titik biru di layar pakai jari.
// =================================================================

#include <LovyanGFX.hpp>
#include <lvgl.h>
#include "esp_heap_caps.h"

// =============================================
// LGFX CONFIG -- disalin apa adanya dari firmware utama v77
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

// =============================================
// LVGL <-> LovyanGFX bridge
// =============================================
static const int32_t SCR_W = 320, SCR_H = 240; // landscape, sama spt default firmware utama
static lv_color_t *lvBuf1 = nullptr;
static lv_color_t *lvBuf2 = nullptr;

// LVGL minta frame yg baru digambar dikirim ke panel fisik.
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, unsigned char *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  if (display.getStartCount() == 0) display.startWrite();
  display.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t*)px_map);
  while (display.dmaBusy()) { /* tunggu DMA selesai sebelum lapor "siap" */ }
  lv_display_flush_ready(disp);
}

// LVGL minta status sentuhan terkini.
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t tx, ty;
  if (display.getTouch(&tx, &ty)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tx;
    data->point.y = ty;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// =============================================
// UI uji coba: label + titik biru yg ngikutin jari
// =============================================
static lv_obj_t *dot;

static void screen_touch_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_obj_set_pos(dot, p.x - 12, p.y - 12);
}

void setup() {
  Serial.begin(115200);

  display.init();
  display.setRotation(1);      // landscape, sama spt firmware utama
  display.setBrightness(200);
  display.calibrateTouch(nullptr, TFT_WHITE, TFT_BLACK, 20); // 3-titik, sekali saja

  lv_init();
  lv_tick_set_cb(millis);

  // Buffer render LVGL ditaruh di PSRAM (bukan RAM internal) --
  // pelajaran dari firmware utama: JANGAN pakai
  // heap_caps_malloc_extmem_enable() global, cukup alokasikan
  // buffer spesifik ini saja ke PSRAM.
  size_t bufBytes = (size_t)SCR_W * SCR_H * sizeof(lv_color_t);
  lvBuf1 = (lv_color_t*) heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lvBuf2 = (lv_color_t*) heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  lv_display_t *disp = lv_display_create(SCR_W, SCR_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, lvBuf1, lvBuf2, bufBytes, LV_DISPLAY_RENDER_MODE_FULL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "ren_phone - Fondasi LVGL OK\nGeser titik biru pakai jari");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

  dot = lv_obj_create(scr);
  lv_obj_set_size(dot, 24, 24);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0x00c8ff), 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_center(dot);

  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, screen_touch_cb, LV_EVENT_PRESSING, NULL);
}

void loop() {
  lv_timer_handler();
  delay(5);
}
