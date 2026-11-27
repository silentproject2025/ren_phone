// Harness: kompilasi & jalankan devopt_renphone.ino ASLI terhadap API layar tiruan.
#define ARDUINO 1
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include "devpins.h"          // + stub/nvs.h lewat -Istub

// ---- tiruan API Arduino / LovyanGFX / fungsi phone.ino yang dipakai ----
static unsigned long g_ms = 10000; static unsigned long millis() { return g_ms; } static void delay(unsigned long) {}
static int g_analog = 2000; static int analogRead(int) { return g_analog; } static int analogReadMilliVolts(int) { return 1800; }
#define RPTR(x) (x)
struct Theme { uint16_t bg, text, subtext, accent, accent2, good, danger, surface2; };
static Theme g_t = {0x0841, 0xFFFF, 0x8410, 0xFD20, 0x07FF, 0x07E0, 0xF800, 0x2104};
static Theme& T() { return g_t; }
static uint16_t blend565(uint16_t a, uint16_t, uint8_t) { return a; }
static int SCR_W = 320, SCR_H = 240; static const int BACK_H = 34;
struct LGFX_Sprite {
  int minx = 9999, maxx = -1, miny = 9999, maxy = -1, calls = 0; bool clip = false; int cx = 0, cy = 0, cw = 0, ch = 0;
  std::string text;
  void mark(int x, int y, int w, int h) { calls++; if (x < minx) minx = x; if (y < miny) miny = y; if (x + w > maxx) maxx = x + w; if (y + h > maxy) maxy = y + h; }
  void setTextSize(int) {} void setTextColor(uint16_t) {} void setCursor(int x, int y) { cx = x; cy = y; }
  void print(const char* t) { text += t; text += "|"; mark(cx, cy, (int)strlen(t) * 6, 8); }
  void fillRect(int x, int y, int w, int h, uint16_t) { mark(x, y, w, h); }
  void setClipRect(int, int, int, int) { clip = true; } void clearClipRect() { clip = false; }
};
static void nyxAaRR(LGFX_Sprite& s, int x, int y, int w, int h, int, uint16_t) { s.mark(x, y, w, h); }
static void nyxAaRRRing(LGFX_Sprite& s, int x, int y, int w, int h, int, uint16_t) { s.mark(x, y, w, h); }
static void nyxAaCircle(LGFX_Sprite& s, int x, int y, int r, uint16_t) { s.mark(x - r, y - r, 2 * r, 2 * r); }
static void iosBackdrop(LGFX_Sprite&) {} static void drawStatusBar(LGFX_Sprite&) {} static void iosTitle(LGFX_Sprite&, int, int, const char*) {}
static void drawBack(LGFX_Sprite&) {} static void drawToast(LGFX_Sprite&) {}
static int BACKX = 4; static bool isBack(int x, int y) { return x >= 4 && x < 88 && y >= SCR_H - BACK_H - 3; }
enum Screen { SCR_HOME, SCR_DEVOPT };
static int g_navBack = 0, g_navPush = 0, g_restart = 0; static std::string g_toast;
static void navBack() { g_navBack++; } static void navPush(Screen) { g_navPush++; }
static void showToast(const char* m, int = 1400) { g_toast = m; }
static bool needRedraw = false; static void needRedrawNow() {}
static Screen g_cur = SCR_DEVOPT; static Screen curScreen() { return g_cur; }
static bool homeHide[4] = {false, true, false, false};
static void homeLayoutEnsure() {} static void homeLayoutRebuildVis() {} static int g_saves = 0; static void homeLayoutSave() { g_saves++; }
static int appIndexForScreen(Screen s) { return s == SCR_DEVOPT ? 1 : -1; }
static int g_notif = 0; static void diNotify(char, const char*, uint16_t, unsigned long, bool, const char*, Screen, bool) { g_notif++; }
static void powerRestartNow() { g_restart++; }
// ---- variabel ina219_renphone.ino ----
bool inaOk = false, inaActive = true, inaOvf = false, inaCharging = false; uint8_t inaAddr = 0x40;
float inaBusV = 3.71f, inaShuntMv = 1.2f, inaI = 12.3f, inaP = 45.6f, inaBatV = 3.713f, inaCurLsbMa = 0.1f, inaFsMa = 3200;
uint16_t inaShuntMohm = 100, inaCalReg = 4096; int inaSoc = 61;
uint16_t battAdcDbgRaw = 0, battAdcDbgMv = 0; float battAdcDbgPinV = 0, battAdcDbgVbat = 0;
float battVoltage = 3.71f; int battPercent = 61;

#include "../../firmware/phone/devopt_renphone.ino"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int findHit(uint8_t act, int arg) { for (int i = 0; i < dvHitN; i++) if (dvHits[i].act == act && dvHits[i].arg == arg) return i; return -1; }
static void draw(int tab = -1) { if (tab >= 0) dvTab = tab; LGFX_Sprite s; drawDevopt(s); CHECK(s.maxx <= SCR_W + 2); CHECK(s.minx >= -2); CHECK(dvHitN <= DV_MAX_HIT); }
static void tap(int x, int y) { devoptTouch(x, y, true, true); devoptTouch(x, y, false, false); }
static void tapHit(int i) { tap(dvHits[i].x + dvHits[i].w / 2, dvHits[i].y + dvHits[i].h / 2); }
// gulir sampai hit (act,arg) terlihat PENUH di jendela, lalu kembalikan indeksnya
static int scrollToHit(uint8_t act, int arg) {
  dvScroll = 0; draw();
  for (int k = 0; k < 200; k++) {
    int h = findHit(act, arg);
    if (h >= 0 && dvHits[h].y >= dvViewTop() && dvHits[h].y + dvHits[h].h <= dvViewBottom()) return h;
    dvScroll += 8; draw();
  }
  return -1;
}
static int freePin() {                                           // GPIO valid yang tidak jadi bawaan pin mana pun
  for (int g = 0; g <= 48; g++) { if (!dpGpioExists(g)) continue; bool used = false; for (int i = 0; i < DP_COUNT; i++) if (DP_DEFS[i].def == g) used = true; if (!used) return g; }
  return -1;
}

int main() {
  for (int orient = 0; orient < 4; orient++) {                 // 320x240, 240x320, 480x320, 320x480
    int dims[4][2] = {{320, 240}, {240, 320}, {480, 320}, {320, 480}};
    SCR_W = dims[orient][0]; SCR_H = dims[orient][1];
    for (int mode = 0; mode < 3; mode++) {                     // 0 = tanpa sensor, 1 = ADC, 2 = INA
      inaOk = (mode == 2); rpPinV[DP_BATT_ADC] = (mode == 1) ? 1 : -1; rpPinV[DP_NTC] = (mode == 1) ? 4 : -1;
      for (int trial = 0; trial < 3; trial++) {
        rpPinBootState = trial; devoptEnter();
        for (int tab = 0; tab < 2; tab++) { draw(tab); for (float sc = 0; sc <= dvMax; sc += 37) { dvScroll = sc; draw(); } dvScroll = 0; }
      }
    }
  }
  printf("gambar semua kombinasi: OK\n");

  // ---- interaksi ----
  SCR_W = 320; SCR_H = 240; inaOk = false; rpPinV[DP_BATT_ADC] = DP_D_BATT_ADC; rpPinV[DP_NTC] = -1; rpPinBootState = 0;
  devoptEnter(); draw(0);
  int id = DP_VIB;
  int h = scrollToHit(DA_PIN_INC, id); CHECK(h >= 0);
  int before = dvEdit[id]; tapHit(h); CHECK(dvEdit[id] == dpStep(id, before, +1));
  draw(); h = scrollToHit(DA_PIN_DEC, id); CHECK(h >= 0); tapHit(h); CHECK(dvEdit[id] == before);
  h = scrollToHit(DA_PIN_INC, id); tapHit(h); draw(); h = scrollToHit(DA_PIN_DEF, id); CHECK(h >= 0); tapHit(h); CHECK(dvEdit[id] == DP_DEFS[id].def);   // ketuk angka = bawaan
  // tahan [+] = berulang
  h = scrollToHit(DA_PIN_INC, id); int cx = dvHits[h].x + 5, cy = dvHits[h].y + 5; int b0 = dvEdit[id];
  devoptTouch(cx, cy, true, true); int afterFirst = dvEdit[id]; CHECK(afterFirst != b0);
  for (int k = 0; k < 10; k++) { g_ms += 100; devoptTouch(cx, cy, true, false); }
  CHECK(dvEdit[id] != afterFirst); devoptTouch(cx, cy, false, false);
  // bentrok -> tombol terapkan nonaktif
  const int FP = freePin(); CHECK(FP >= 0);
  memcpy(dvEdit, rpPinV, sizeof(dvEdit)); dvEdit[DP_VIB] = dvEdit[DP_NPX] = (int8_t)FP;   // dua pin di GPIO sama
  CHECK(dpErrorCount(dvEdit) >= 2); draw(0); dvScroll = (float)dvMax; draw();
  CHECK(findHit(DA_APPLY, 0) < 0);                              // tidak bisa diterapkan selagi ada masalah
  // perubahan valid -> modal -> restart tersimpan sebagai percobaan
  memcpy(dvEdit, rpPinV, sizeof(dvEdit)); dvEdit[DP_VIB] = (int8_t)FP; draw(0);
  h = scrollToHit(DA_APPLY, 0); CHECK(h >= 0); tapHit(h); CHECK(dvModal == 1);
  draw(); h = findHit(DA_CANCEL, 0); CHECK(h >= 0); tapHit(h); CHECK(dvModal == 0 && g_restart == 0);
  h = scrollToHit(DA_APPLY, 0); tapHit(h); draw(); h = findHit(DA_OK, 0); tapHit(h);
  CHECK(g_restart == 1); DpBlob nb; size_t len = sizeof(nb); CHECK(nvs_get_blob(0, "new", &nb, &len) == ESP_OK); CHECK(nb.ov[DP_VIB] == FP);
  uint8_t tr = 9; nvs_get_u8(0, "try", &tr); CHECK(tr == 0);
  // boot percobaan -> konfirmasi
  memcpy(rpPinV, DP_DEFS, 0); for (int i = 0; i < DP_COUNT; i++) rpPinV[i] = DP_DEFS[i].def; rpPinBootState = 0;
  devpinsBoot(); CHECK(rpPinBootState == 1 && rpPinV[DP_VIB] == FP);
  devoptEnter(); h = scrollToHit(DA_KEEP, 0); CHECK(h >= 0); tapHit(h); CHECK(rpPinBootState == 0);
  for (int i = 0; i < DP_COUNT; i++) rpPinV[i] = DP_DEFS[i].def;
  devpinsBoot(); CHECK(rpPinBootState == 0 && rpPinV[DP_VIB] == FP);   // permanen
  // reset semua
  devoptEnter(); h = scrollToHit(DA_RESET, 0); CHECK(h >= 0); tapHit(h); CHECK(dvModal == 2);
  draw(); h = findHit(DA_OK, 0); tapHit(h); CHECK(g_restart == 2);
  for (int i = 0; i < DP_COUNT; i++) rpPinV[i] = DP_DEFS[i].def;
  devpinsBoot(); CHECK(rpPinV[DP_VIB] == DP_DEFS[DP_VIB].def);
  // sembunyikan app
  homeHide[1] = false; devoptEnter(); h = scrollToHit(DA_LOCK, 0); CHECK(h >= 0); tapHit(h); CHECK(homeHide[1] && g_navBack == 1);
  // unlock dari About
  homeHide[1] = true; devoptUnlock(); CHECK(!homeHide[1] && g_navPush == 1);
  // tab Sensor ADC: stepper kalibrasi
  rpPinV[DP_BATT_ADC] = 1; inaOk = false; devoptEnter(); dvTab = 1; devoptLoopPoll();
  h = scrollToHit(DA_DIV_INC, 0); CHECK(h >= 0); float d0 = devBattDiv; tapHit(h); CHECK(devBattDiv > d0);
  g_ms += 1000; devoptLoopPoll(); CHECK(!dvCalDirty);                                  // tersimpan setelah jeda
  float savedDiv = devBattDiv; devBattDiv = 2.0f; for (int i = 0; i < DP_COUNT; i++) rpPinV[i] = DP_DEFS[i].def; devpinsBoot(); CHECK(fabsf(devBattDiv - savedDiv) < 1e-5f);
  // tab Sensor INA
  inaOk = true; draw(1);
  // pengingat boot
  rpPinBootState = 1; dvNoticeDone = false; g_notif = 0; g_ms = 20000; devoptLoopPoll(); CHECK(g_notif == 1);
  // sentuhan di Back
  g_navBack = 0; tap(20, SCR_H - 10); CHECK(g_navBack == 1);

  printf(fails ? "\n%d GAGAL\n" : "\nSEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
