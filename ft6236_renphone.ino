// ============================================================
//  ft6236_renphone.ino -- driver touch kapasitif FT6236/FT6336 (I2C)
//  Menggantikan XPT2046 (SPI). Tidak butuh kalibrasi: koordinat sudah
//  dalam piksel, tinggal dipetakan ke rotasi layar yang aktif.
//
//  Dipakai oleh getInputPoint() di phone.ino lewat ftPoll()/ftFinger().
//  Pakai Wire (bus yang sama dgn MPU6050/INA219), BUKAN driver I2C
//  LovyanGFX, supaya tidak bentrok dengan Wire.
//
//  MULTI-TOUCH (accretion-v2): FT6236/FT6336 melaporkan sampai 2 jari
//  sekaligus. Satu transaksi I2C membaca register 0x02..0x0C (TD_STATUS +
//  titik 1 + titik 2). Tiap jari punya "slot" 0/1 yang STABIL selama jari
//  menempel (dipakai ID jari dari chip, + pengecekan jarak terdekat saat
//  2 jari -> 1 jari supaya tetap benar walau ID dari modul murahan ngaco).
//  API tanpa struct (aman buat auto-prototype Arduino):
//     bool    ftPoll();                         // baca chip (dibatasi TP_POLL_MS)
//     uint8_t ftFingerCount();                  // 0..2
//     bool    ftFinger(slot, &x, &y);           // koordinat layar utk slot 0/1
//  ftGetTouch() lama TETAP ada (jari dgn slot terkecil) buat kode lama.
// ============================================================
#include <Wire.h>
#include "hw_ili9488.h"

static bool     ftOk = false;
static uint32_t ftLastPollMs = 0;
static bool     ftLastDown = false;
static int16_t  ftLastX = 0, ftLastY = 0;

// ---- multi-touch: snapshot jari per slot (0/1) ----
static bool     ftFDown[2] = {false, false};
static int16_t  ftFX[2]    = {0, 0};
static int16_t  ftFY[2]    = {0, 0};

static bool ftRead(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission((uint8_t)TP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((uint8_t)TP_I2C_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// ---- sensitivitas (ambang sentuh chip, reg 0x80) ----
static uint8_t ftSensIdx = 0;                    // 0 Normal, 1 Tinggi, 2 Rendah
static void ftApplyThreshold() {
  static const uint8_t lv[3] = { TP_THR_NORMAL, TP_THR_HIGH, TP_THR_LOW };
  Wire.beginTransmission((uint8_t)TP_I2C_ADDR); Wire.write(0x80); Wire.write(lv[ftSensIdx > 2 ? 0 : ftSensIdx]); Wire.endTransmission();
}
uint8_t ftSensGet() { return ftSensIdx; }
void ftSensSet(uint8_t idx) {
  if (idx > 2) idx = 0;
  ftSensIdx = idx;
  if (ftOk) ftApplyThreshold();
  Preferences p;
  if (p.begin("touch", false)) { p.putUChar("sens", idx); p.end(); }
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
    Preferences p;                     // level sensitivitas tersimpan (Pengaturan -> Sensitivitas Sentuh)
    if (p.begin("touch", true)) { ftSensIdx = p.getUChar("sens", 0); p.end(); }
    if (ftSensIdx > 2) ftSensIdx = 0;
    ftApplyThreshold();
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

// Pilih slot untuk titik ke-i. ID dari chip dipakai kalau masuk akal (0/1);
// kalau dua titik melapor ID sama, titik kedua dipaksa ke slot satunya.
static inline int ftDist2(int ax, int ay, int bx, int by) {
  int dx = ax - bx, dy = ay - by; return dx * dx + dy * dy;
}

// Baca chip -> perbarui snapshot jari. Dibatasi TP_POLL_MS (hasil terakhir
// dipakai kalau terlalu cepat). return true kalau ada >= 1 jari menempel.
bool ftPoll() {
  if (!ftOk) return false;
  uint32_t now = millis();
  if (now - ftLastPollMs < TP_POLL_MS) return ftFDown[0] || ftFDown[1];
  ftLastPollMs = now;
#if TP_INT_SKIP && (TP_INT_PIN >= 0)
  if (!ftFDown[0] && !ftFDown[1] && digitalRead(TP_INT_PIN)) return false;   // tidak ada jari & INT HIGH -> tidak perlu baca
#endif

  // 0x02 TD_STATUS | 0x03..0x06 titik1 (XH,XL,YH,YL) | 0x07 weight | 0x08 misc
  // | 0x09..0x0C titik2 (XH,XL,YH,YL)  -> 11 byte dlm 1 transaksi
  uint8_t b[11];
  if (!ftRead(0x02, b, 11)) {                       // I2C error: anggap semua jari lepas
    ftFDown[0] = ftFDown[1] = false; ftLastDown = false; return false;
  }
  uint8_t n = b[0] & 0x0F;                          // jumlah titik aktif (0..2)
  if (n > 2) n = 0;                                 // nilai ngaco (mis. 0x0F saat chip belum siap)
#if !TP_MULTITOUCH
  if (n > 1) n = 1;                                 // mode 1 jari: abaikan titik ke-2
#endif

  bool    nd[2]  = {false, false};
  int16_t nx[2]  = {0, 0}, ny[2] = {0, 0};
  int     pcount = 0;
  int     pId[2] = {0, 0}, pX[2] = {0, 0}, pY[2] = {0, 0};

  for (uint8_t i = 0; i < n; i++) {
    const uint8_t* p = &b[1 + i * 6];               // titik1 di b[1..4], titik2 di b[7..10]
    uint8_t event = p[0] >> 6;                      // 0 tekan, 1 angkat, 2 menempel, 3 kosong
    if (event == 1 || event == 3) continue;         // jari ini sedang diangkat / slot kosong
    int rx = ((p[0] & 0x0F) << 8) | p[1];
    int ry = ((p[2] & 0x0F) << 8) | p[3];
    if (rx >= 4095 && ry >= 4095) continue;         // 0xFFF = data tidak valid
    int id = (p[2] >> 4) & 0x0F;
    int x, y; ftMap(rx, ry, x, y);
#if TP_DEBUG_RAW
    Serial.printf("[touch] raw#%d rx=%d ry=%d id=%d ev=%d n=%d\n", i, rx, ry, id, event, n);
#endif
    pId[pcount] = id; pX[pcount] = x; pY[pcount] = y; pcount++;
  }

  if (pcount == 2) {
    // 2 jari: slot = ID chip. ID sama / di luar 0..1 -> urutan laporan.
    int s0 = pId[0], s1 = pId[1];
    if (s0 > 1 || s1 > 1 || s0 == s1) { s0 = 0; s1 = 1; }
    nd[s0] = true; nx[s0] = pX[0]; ny[s0] = pY[0];
    nd[s1] = true; nx[s1] = pX[1]; ny[s1] = pY[1];
  } else if (pcount == 1) {
    int s = pId[0];
    if (s > 1) s = 0;
    if (ftFDown[0] && ftFDown[1]) {
      // 2 jari -> 1 jari: jari mana yang masih ada? Ambil slot yang posisi
      // terakhirnya PALING DEKAT (tidak tergantung ID chip yg kadang tertukar).
      s = (ftDist2(pX[0], pY[0], ftFX[0], ftFY[0]) <= ftDist2(pX[0], pY[0], ftFX[1], ftFY[1])) ? 0 : 1;
    } else if (ftFDown[0] != ftFDown[1]) {
      // sebelumnya 1 jari -> tetap jari itu kalau masih dekat; kalau jauh
      // sekali (> ~60 px) berarti jari lama sudah lepas & ini jari baru.
      int cur = ftFDown[0] ? 0 : 1;
      if (ftDist2(pX[0], pY[0], ftFX[cur], ftFY[cur]) < 60 * 60 || s == cur) s = cur;
    }
    nd[s] = true; nx[s] = pX[0]; ny[s] = pY[0];
  }

  for (int s = 0; s < 2; s++) {
    ftFDown[s] = nd[s];
    if (nd[s]) { ftFX[s] = nx[s]; ftFY[s] = ny[s]; }
  }

  // kompatibilitas ftGetTouch() lama: jari dgn slot terkecil
  int pr = ftFDown[0] ? 0 : (ftFDown[1] ? 1 : -1);
  ftLastDown = (pr >= 0);
  if (pr >= 0) { ftLastX = ftFX[pr]; ftLastY = ftFY[pr]; }
  return ftLastDown;
}

uint8_t ftFingerCount() { return (ftFDown[0] ? 1 : 0) + (ftFDown[1] ? 1 : 0); }

// Koordinat jari di slot 0/1 (koordinat layar SAAT INI). false = slot kosong.
bool ftFinger(uint8_t slot, int* x, int* y) {
  if (slot > 1 || !ftFDown[slot]) return false;
  if (x) *x = ftFX[slot];
  if (y) *y = ftFY[slot];
  return true;
}

// true = ada jari menempel. tp->x/y dalam koordinat layar SAAT INI.
// (API lama, 1 jari: jari dgn slot terkecil. Kode multi-touch pakai ftPoll()/ftFinger().)
bool ftGetTouch(lgfx::touch_point_t* tp) {
  if (!ftPoll()) return false;
  tp->x = ftLastX; tp->y = ftLastY; tp->size = 64; tp->id = 0;
  return true;
}
