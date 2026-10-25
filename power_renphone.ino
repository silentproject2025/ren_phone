// ============================================================
//  power_renphone.ino -- Menu Daya: Matikan total + Mulai ulang
//
//  Dibuka dari:  (1) tahan-lama tile "Kunci" di Control Center
//                (2) Pengaturan > TENTANG > Daya
//
//  MATIKAN = deep sleep ESP32-S3 (arus ~ puluhan uA di chip-nya):
//    - simpan data baterai (INA219) & tutup SD/FFat
//    - Neopixel mati, WiFi mati, layar tidur, backlight/motor/neopixel
//      DITAHAN LOW (gpio_hold) supaya tidak menyala sendiri
//    - ESP32-CAM (modul BT) disuruh deep sleep juga (perintah RP_POWEROFF)
//    - bangun: tekan tombol BOOT (GPIO0), atau tombol RST/EN
//      (RST selalu bisa, bahkan tanpa kode apa pun)
//
//  Catatan jujur: ini bukan memutus baterai. Regulator & chip USB
//  di board tetap makan arus kecil. Untuk 0 mA butuh saklar fisik.
// ============================================================
#include "rplink.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

#define PWR_WAKE_GPIO   GPIO_NUM_0     // tombol BOOT di board ESP32-S3 (aktif LOW)
#define PWR_BL_PIN      21
#define PWR_VIB_PIN     VIB_PIN
#define PWR_NPX_PIN     NEOPIXEL_PIN

bool powerMenuOpen = false;
static int pwrPress = -1;
static unsigned long pwrPressMs = 0;

void inaSaveCounters(bool force);      // ina219_renphone.ino

// Lepas "hold" pin dari deep sleep sebelumnya SEBELUM setup() jalan,
// supaya backlight/motor/neopixel normal lagi setelah bangun.
static struct PwrBootRelease {
  PwrBootRelease() {
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)PWR_BL_PIN);
    gpio_hold_dis((gpio_num_t)PWR_VIB_PIN);
    gpio_hold_dis((gpio_num_t)PWR_NPX_PIN);
  }
} pwrBootRelease;

void powerMenuShow() {
  powerMenuOpen = true; pwrPress = -1; needRedraw = true;
}

// ---- layout (dihitung dari ukuran layar, jadi aman di portrait/landscape) ----
static void pwrLayout(int& px, int& py, int& pw, int& ph, int& bh, int& gap) {
  pw  = min(SCR_W - 28, 250);
  bh  = 38; gap = 8;
  ph  = 34 + 3 * bh + 2 * gap + 12;
  px  = (SCR_W - pw) / 2;
  py  = (SCR_H - ph) / 2;
}

void drawPowerMenu(LGFX_Sprite& s) {
  int px, py, pw, ph, bh, gap; pwrLayout(px, py, pw, ph, bh, gap);
  drawGlassPanel(s, 0, 0, SCR_W, SCR_H, 0, 0x0000, 150);          // redupkan latar
  drawGlassPanel(s, px, py, pw, ph, 18, T().surface, 235);
  s.setTextSize(1);
  s.setTextColor(T().text);
  const char* ttl = "Daya";
  s.setCursor(px + pw / 2 - s.textWidth(ttl) / 2, py + 13); s.print(ttl);

  const char* lbl[3] = { "Matikan", "Mulai Ulang", "Batal" };
  uint16_t    bg[3]  = { T().danger, T().accent, T().surface2 };
  for (int i = 0; i < 3; i++) {
    int bx = px + 12, by = py + 34 + i * (bh + gap), bw = pw - 24;
    uint16_t c = bg[i];
    if (pwrPress == i) c = blend565(c, 0xFFFF, 70);                 // feedback tekan
    s.fillRoundRect(bx, by, bw, bh, 12, c);
    s.setTextColor(i == 2 ? T().text : appIconInk(bg[i]));
    s.setCursor(bx + bw / 2 - s.textWidth(lbl[i]) / 2, by + bh / 2 - 4);
    s.print(lbl[i]);
  }
  drawToast(s);
}

// ---- aksi ----
static void pwrScreenMsg(const char* m) {
  display.setBrightness(brightness);
  display.fillScreen(0x0000);
  display.setTextSize(2);
  display.setTextColor(0xFFFF, 0x0000);
  display.setCursor(max(0, (int)(display.width() / 2 - display.textWidth(m) / 2)), display.height() / 2 - 8);
  display.print(m);
}

static void pwrSaveAll() {
  inaSaveCounters(true);
  if (sdReady) SD_MMC.end();
  if (ffatReady) FFat.end();
}

void powerRestartNow() {
  vibExit();
  pwrScreenMsg("Memulai ulang...");
  pwrSaveAll();
  delay(400);
  ESP.restart();
}

void powerShutdownNow() {
  vibExit();
  pwrScreenMsg("Mematikan...");
  npxOff();
  pwrSaveAll();

  // suruh ESP32-CAM (BT) tidur juga, tunggu sebentar supaya TWS sempat diputus rapi
  rpSend(Serial1, RP_POWEROFF, nullptr, 0);
  Serial1.flush();
  delay(900);

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);

  display.fillScreen(0x0000);
  display.setBrightness(0);
  display.sleep();                                   // panel ILI9341 masuk SLPIN
  delay(50);

  // tahan backlight, motor, neopixel di LOW selama deep sleep
  const int holdPins[3] = { PWR_BL_PIN, PWR_VIB_PIN, PWR_NPX_PIN };
  for (int i = 0; i < 3; i++) {
    gpio_set_direction((gpio_num_t)holdPins[i], GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)holdPins[i], 0);
    gpio_hold_en((gpio_num_t)holdPins[i]);
  }
  gpio_deep_sleep_hold_en();

  // tunggu tombol BOOT dilepas dulu (kalau lagi tertekan, langsung bangun lagi)
  pinMode((uint8_t)PWR_WAKE_GPIO, INPUT_PULLUP);
  unsigned long t0 = millis();
  while (digitalRead((uint8_t)PWR_WAKE_GPIO) == LOW && millis() - t0 < 3000) delay(10);

  rtc_gpio_pullup_en(PWR_WAKE_GPIO);
  rtc_gpio_pulldown_dis(PWR_WAKE_GPIO);
  esp_sleep_enable_ext0_wakeup(PWR_WAKE_GPIO, 0);
  esp_deep_sleep_start();                            // tidak kembali
}

void powerMenuTouch(bool newT, int tx, int ty) {
  if (!newT) { if (pwrPress >= 0 && millis() - pwrPressMs > 150) { pwrPress = -1; needRedraw = true; } return; }
  int px, py, pw, ph, bh, gap; pwrLayout(px, py, pw, ph, bh, gap);
  if (tx < px || tx > px + pw || ty < py || ty > py + ph) {      // ketuk di luar panel = batal
    powerMenuOpen = false; needRedraw = true; return;
  }
  for (int i = 0; i < 3; i++) {
    int bx = px + 12, by = py + 34 + i * (bh + gap), bw = pw - 24;
    if (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh) {
      pwrPress = i; pwrPressMs = millis(); vibTap();
      if (i == 0)      powerShutdownNow();
      else if (i == 1) powerRestartNow();
      else { powerMenuOpen = false; pwrPress = -1; }
      needRedraw = true; return;
    }
  }
}
