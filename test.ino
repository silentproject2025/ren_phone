#include <LovyanGFX.hpp>
#include <Preferences.h>

// =============================================
// KONFIGURASI LGFX
// =============================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX(void) {
    // --- Bus SPI ---
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

    // --- Panel ILI9341 ---
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
      cfg.bus_shared       = true;
      _panel_instance.config(cfg);
    }

    // --- Backlight ---
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 21;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    // --- Touchscreen XPT2046 ---
    {
      auto cfg = _touch_instance.config();
      cfg.pin_int    = 1;        // T_IRQ (opsional)
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host   = SPI2_HOST;
      cfg.freq       = 1000000;
      cfg.pin_sclk   = 12;      // T_CLK
      cfg.pin_mosi   = 11;      // T_DIN
      cfg.pin_miso   = 13;      // T_DO
      cfg.pin_cs     = 9;       // T_CS
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

LGFX display;
Preferences prefs;

uint16_t calData[8];
bool calibrated = false;

// =============================================
// FUNGSI KALIBRASI (pakai Preferences / NVS)
// =============================================
void loadOrRunCalibration() {
  prefs.begin("touch_cal", false);
  calibrated = prefs.getBool("done", false);

  if (calibrated) {
    // Muat data kalibrasi dari flash
    prefs.getBytes("data", calData, sizeof(calData));
    display.setTouchCalibrate(calData);
    Serial.println("Kalibrasi dimuat dari flash.");
  } else {
    // Jalankan kalibrasi bawaan LGFX
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE);
    display.setTextSize(2);
    display.setCursor(10, display.height() / 2 - 20);
    display.println("Kalibrasi Touchscreen");
    display.setTextSize(1);
    display.setCursor(10, display.height() / 2 + 10);
    display.println("Sentuh tanda panah di layar");

    // calibrateTouch: tampilkan marker di 4 sudut, sentuh satu per satu
    display.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);

    // Simpan ke flash supaya tidak perlu kalibrasi lagi
    prefs.putBytes("data", calData, sizeof(calData));
    prefs.putBool("done", true);
    Serial.println("Kalibrasi selesai & disimpan.");
  }

  prefs.end();
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(1);  // Landscape

  Serial.println("Init OK!");

  // --- Tes warna ---
  display.fillScreen(TFT_RED);   delay(300);
  display.fillScreen(TFT_GREEN); delay(300);
  display.fillScreen(TFT_BLUE);  delay(300);
  display.fillScreen(TFT_BLACK);

  // --- Kalibrasi touch ---
  if (display.touch()) {
    loadOrRunCalibration();
  } else {
    Serial.println("Touch tidak terdeteksi!");
  }

  // --- UI setelah kalibrasi ---
  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE);
  display.setCursor(10, 10);
  display.println("Touch OK!");
  display.setTextSize(1);
  display.setCursor(10, 40);
  display.setTextColor(TFT_YELLOW);
  display.println("Sentuh layar untuk menggambar");
  display.setCursor(10, 55);
  display.println("Serial Monitor: lihat koordinat");

  // Tombol reset kalibrasi
  display.fillRoundRect(200, 5, 110, 28, 5, TFT_RED);
  display.setTextColor(TFT_WHITE);
  display.setCursor(210, 14);
  display.println("Reset Cal");

  // Garis pemisah
  display.drawLine(0, 75, 320, 75, TFT_DARKGREY);
}

// =============================================
// LOOP - Tes Touch
// =============================================
void loop() {
  lgfx::touch_point_t tp;

  if (display.getTouch(&tp)) {
    // Cek tombol reset kalibrasi
    if (tp.x > 200 && tp.y < 35) {
      Serial.println("Reset kalibrasi...");
      prefs.begin("touch_cal", false);
      prefs.putBool("done", false);
      prefs.end();
      ESP.restart();
    }

    // Gambar titik
    if (tp.y > 75) {
      display.fillCircle(tp.x, tp.y, 3, TFT_RED);
    }

    Serial.printf("Touch: X=%d  Y=%d\n", tp.x, tp.y);
  }
}
