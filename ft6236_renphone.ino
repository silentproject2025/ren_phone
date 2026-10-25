// ============================================================
//  ft6236_renphone.ino -- driver touch kapasitif FT6236/FT6336 (I2C)
//  Menggantikan XPT2046 (SPI). Tidak butuh kalibrasi: koordinat sudah
//  dalam piksel, tinggal dipetakan ke rotasi layar yang aktif.
//
//  Dipakai oleh getInputPoint() di phone.ino lewat ftGetTouch().
//  Pakai Wire (bus yang sama dgn MPU6050/INA219), BUKAN driver I2C
//  LovyanGFX, supaya tidak bentrok dengan Wire.
// ============================================================
#include <Wire.h>
#include "hw_ili9488.h"

static bool     ftOk = false;
static uint32_t ftLastPollMs = 0;
static bool     ftLastDown = false;
static int16_t  ftLastX = 0, ftLastY = 0;

static bool ftRead(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission((uint8_t)TP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((uint8_t)TP_I2C_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool ft6236Begin() {
#if TP_RST_PIN >= 0
  pinMode(TP_RST_PIN, OUTPUT);
  digitalWrite(TP_RST_PIN, LOW);  delay(10);
  digitalWrite(TP_RST_PIN, HIGH); delay(120);
#endif
#if TP_INT_PIN >= 0
  pinMode(TP_INT_PIN, INPUT_PULLUP);
#endif
  Wire.begin(TP_SDA_PIN, TP_SCL_PIN);
  Wire.setClock(TP_I2C_HZ);
  Wire.setTimeOut(20);

  uint8_t id = 0, vendor = 0;
  bool a = ftRead(0xA3, &id, 1);       // chip id: 0x06 = FT6206, 0x36 = FT6236, 0x64 = FT6336
  bool b = ftRead(0xA8, &vendor, 1);   // vendor id: 0x11
  ftOk = a || b;
  Serial.printf("[touch] FT6236 %s (chip=0x%02X vendor=0x%02X, SDA=%d SCL=%d)\n",
                ftOk ? "terdeteksi" : "TIDAK terdeteksi", id, vendor, TP_SDA_PIN, TP_SCL_PIN);
  if (ftOk) {
    uint8_t thr = 22;                  // ambang sentuh (bawaan 22; naikkan kalau ada ghost touch)
    Wire.beginTransmission((uint8_t)TP_I2C_ADDR); Wire.write(0x80); Wire.write(thr); Wire.endTransmission();
  }
  return ftOk;
}

// Peta koordinat NATIVE (rotasi 0: 320x480) -> koordinat layar sesuai rotasi aktif.
static void ftMap(int rx, int ry, int& x, int& y) {
#if TP_SWAP_XY
  { int t = rx; rx = ry; ry = t; }
#endif
#if TP_FLIP_X
  rx = LCD_NATIVE_W - 1 - rx;
#endif
#if TP_FLIP_Y
  ry = LCD_NATIVE_H - 1 - ry;
#endif
  switch (display.getRotation() & 3) {
    default:
    case 0: x = rx;                      y = ry;                      break;
    case 1: x = ry;                      y = LCD_NATIVE_W - 1 - rx;   break;
    case 2: x = LCD_NATIVE_W - 1 - rx;   y = LCD_NATIVE_H - 1 - ry;   break;
    case 3: x = LCD_NATIVE_H - 1 - ry;   y = rx;                      break;
  }
  if (x < 0) x = 0; if (x >= display.width())  x = display.width()  - 1;
  if (y < 0) y = 0; if (y >= display.height()) y = display.height() - 1;
}

// true = ada jari menempel. tp->x/y dalam koordinat layar SAAT INI.
bool ftGetTouch(lgfx::touch_point_t* tp) {
  if (!ftOk) return false;
  uint32_t now = millis();
  if (now - ftLastPollMs < TP_POLL_MS) {          // pakai hasil terakhir, hemat bus I2C
    if (ftLastDown) { tp->x = ftLastX; tp->y = ftLastY; tp->size = 64; tp->id = 0; }
    return ftLastDown;
  }
  ftLastPollMs = now;

  uint8_t b[5];
  if (!ftRead(0x02, b, 5)) { ftLastDown = false; return false; }
  uint8_t n     = b[0] & 0x0F;                    // jumlah titik
  uint8_t event = b[1] >> 6;                      // 0 tekan, 1 angkat, 2 menempel, 3 kosong
  if (n == 0 || n > 2 || event == 1 || event == 3) { ftLastDown = false; return false; }

  int rx = ((b[1] & 0x0F) << 8) | b[2];
  int ry = ((b[3] & 0x0F) << 8) | b[4];
#if TP_DEBUG_RAW
  Serial.printf("[touch] raw rx=%d ry=%d ev=%d n=%d\n", rx, ry, event, n);
#endif
  int x, y; ftMap(rx, ry, x, y);
  ftLastX = x; ftLastY = y; ftLastDown = true;
  tp->x = x; tp->y = y; tp->size = 64; tp->id = 0;
  return true;
}
