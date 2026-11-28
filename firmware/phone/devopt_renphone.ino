// =====================================================================
// devopt_renphone.ino -- app OPSI DEVELOPER (SCR_DEVOPT)   // DEVPINS_V1
// =====================================================================
// Mirip "Opsi developer" di Android: disembunyikan dulu, dibuka dengan mengetuk judul About Phone 7x.
//
// Tab "Pin"    : ubah semua pin papan (layar SPI, kartu SD, sentuh, bus sensor, ADC baterai, motor getar, LED,
//                mikrofon, speaker, UART ESP32-CAM). [-] / [+] menggeser nomor GPIO (tahan = berulang, GPIO yang
//                tidak ada di ESP32-S3 dilompati), ketuk angkanya = kembali ke bawaan. Nilai yang bentrok / tak
//                valid / berisiko (strapping, USB) diberi tanda. "Terapkan" menyimpan sebagai PERCOBAAN lalu restart;
//                sesudah menyala harus dikonfirmasi "Simpan permanen", kalau tidak (cabut daya / crash) pin lama
//                kembali sendiri -- lihat devpins.h.
// Tab "Sensor" : hanya sumber yang SEDANG DIPAKAI untuk baterai --
//                * INA219 terdeteksi -> bacaan chip (VBUS, shunt, arus, daya, SOC, alamat, kalibrasi),
//                * kalau tidak, ADC  -> perhitungan ADC saja: nilai mentah, mV di pin, rasio pembagi, ofset, hasil Vbat.
//                  Rasio pembagi & ofset bisa disetel langsung (tanpa restart).
//
// ATURAN PROYEK: tidak ada String / alokasi heap per frame (semua buffer statis); fungsi publik hanya menerima
// tipe primitif / LGFX_Sprite& (hindari bug prototype otomatis arduino-cli).
// =====================================================================

// ---- variabel dari ina219_renphone.ino (non-static di sana) ----
extern bool     inaOk, inaActive, inaOvf, inaCharging;
extern uint8_t  inaAddr;
extern float    inaBusV, inaShuntMv, inaI, inaP, inaBatV, inaCurLsbMa, inaFsMa;
extern uint16_t inaShuntMohm, inaCalReg;
extern int      inaSoc;
extern uint16_t battAdcDbgRaw, battAdcDbgMv;
extern float    battAdcDbgPinV, battAdcDbgVbat;

#define DV_TITLE_Y   26
#define DV_TAB_Y     46
#define DV_TAB_H     20
#define DV_ROW_H     32
#define DV_HEAD_H    18
#define DV_WARN      0xFDA0
#define DV_MAX_HIT   56

// aksi hit-test (sengaja #define, bukan enum -- lihat catatan prototype)
#define DA_TAB        1
#define DA_PIN_DEC    2
#define DA_PIN_INC    3
#define DA_PIN_DEF    4
#define DA_APPLY      5
#define DA_RESET      6
#define DA_LOCK       7
#define DA_KEEP       8
#define DA_REVERT     9
#define DA_OK        10
#define DA_CANCEL    11
#define DA_DIV_DEC   12
#define DA_DIV_INC   13
#define DA_CAL_DEC   14
#define DA_CAL_INC   15

struct DvHit { int16_t x, y, w, h; uint8_t act; int8_t arg; bool fixed; };

static int8_t   dvEdit[DP_COUNT];            // nilai pin yang sedang diedit (belum tentu diterapkan)
static uint8_t  dvTab = 0;
static uint8_t  dvModal = 0;                 // 0 tidak ada, 1 konfirmasi terapkan, 2 konfirmasi reset semua
static DvHit    dvHits[DV_MAX_HIT] EXT_RAM_BSS_ATTR;
static int      dvHitN = 0;
static int      dvContentH = 0, dvMax = 0;
static float    dvScroll = 0, dvVel = 0;
static bool     dvDrag = false, dvGlide = false, dvTouching = false, dvMoved = false;
static int      dvLastY = 0, dvStartY = 0;
static unsigned long dvLastMs = 0, dvGlideT = 0;
static int      dvPend = -1;                 // indeks hit yang menunggu dilepas (tombol biasa)
static int      dvRepAct = 0, dvRepArg = 0;  // stepper yang sedang ditahan
static unsigned long dvRepT0 = 0, dvRepLast = 0;
static unsigned long dvLastRefresh = 0, dvCalDirtyMs = 0;
static bool     dvCalDirty = false;
static bool     dvNoticeDone = false;
static uint16_t dvAdcRaw = 0, dvAdcMv = 0, dvNtcRaw = 0, dvNtcMv = 0;

static int dvViewTop()    { return DV_TAB_Y + DV_TAB_H + 6; }
static int dvViewBottom() { return SCR_H - BACK_H - 3 - 6; }

// ---------------------------------------------------------------------
// util
// ---------------------------------------------------------------------
static uint16_t dvPanel() { return blend565(T().bg, T().surface2, 200); }

static void dvHit(int x, int y, int w, int h, uint8_t act, int arg, bool fixed) {
  if (dvHitN >= DV_MAX_HIT) return;
  if (!fixed && (y + h <= dvViewTop() || y >= dvViewBottom())) return;          // di luar jendela: tidak bisa diketuk
  DvHit& t = dvHits[dvHitN++];
  t.x = (int16_t)x; t.y = (int16_t)y; t.w = (int16_t)w; t.h = (int16_t)h; t.act = act; t.arg = (int8_t)arg; t.fixed = fixed;
}

static int dvWrap(LGFX_Sprite& s, int x, int y, int maxW, int lineH, int maxLines, const char* text, uint16_t col) {
  s.setTextColor(col);
  int cpl = maxW / 6; if (cpl < 4) cpl = 4;
  int L = (int)strlen(text), pos = 0, n = 0;
  char buf[96];
  while (pos < L && n < maxLines) {
    int len = L - pos;
    if (len > cpl) { int brk = pos + cpl; while (brk > pos && text[brk] != ' ') brk--; len = (brk > pos) ? (brk - pos) : cpl; }
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, text + pos, len); buf[len] = 0;
    s.setCursor(x, y + n * lineH); s.print(buf);
    pos += len; while (pos < L && text[pos] == ' ') pos++;
    n++;
  }
  return n * lineH;
}
static int dvWrapH(int maxW, int lineH, const char* text) {     // tinggi hasil bungkus (tanpa menggambar)
  int cpl = maxW / 6; if (cpl < 4) cpl = 4;
  int L = (int)strlen(text), pos = 0, n = 0;
  while (pos < L && n < 12) {
    int len = L - pos;
    if (len > cpl) { int brk = pos + cpl; while (brk > pos && text[brk] != ' ') brk--; len = (brk > pos) ? (brk - pos) : cpl; }
    pos += len; while (pos < L && text[pos] == ' ') pos++;
    n++;
  }
  return n * lineH;
}

static int dvDiffFromActive() {                                  // jumlah pin yang beda dgn yang sedang aktif
  int n = 0;
  for (int i = 0; i < DP_COUNT; i++) if (DP_DEFS[i].avail && dvEdit[i] != rpPinV[i]) n++;
  return n;
}
static void dvAdcName(int g, char* out, size_t cap) {
  if (g >= 1 && g <= 10)       snprintf(out, cap, "ADC1_CH%d", g - 1);
  else if (g >= 11 && g <= 20) snprintf(out, cap, "ADC2_CH%d", g - 11);
  else                         snprintf(out, cap, "bukan pin ADC");
}
static void dvClampScroll() {
  int viewH = dvViewBottom() - dvViewTop();
  dvMax = dvContentH - viewH; if (dvMax < 0) dvMax = 0;
  if (dvScroll < 0) dvScroll = 0;
  if (dvScroll > dvMax) dvScroll = (float)dvMax;
}

// ---------------------------------------------------------------------
// siklus hidup
// ---------------------------------------------------------------------
void devoptEnter() {
  memcpy(dvEdit, rpPinV, sizeof(dvEdit));
  dvTab = 0; dvModal = 0; dvScroll = 0; dvVel = 0;
  dvDrag = dvGlide = dvTouching = false; dvPend = -1; dvRepAct = 0; dvHitN = 0;
  dvLastRefresh = millis();
}
void devoptExit() {
  dvDrag = dvGlide = dvTouching = false; dvRepAct = 0;
  if (dvCalDirty) { devpinsSaveBattCal(); dvCalDirty = false; }
  if (rpPinBootState == 2) rpPinBootState = 0;                   // catatan "dibatalkan otomatis" cukup dilihat sekali
}

// membuka app dari About (ketuk judul 7x): tampilkan ikonnya di Home lalu buka
void devoptUnlock() {
  homeLayoutEnsure();
  int i = appIndexForScreen(SCR_DEVOPT);
  if (i >= 0 && homeHide[i]) {
    homeHide[i] = false; homeLayoutRebuildVis(); homeLayoutSave();
    showToast(RPTR("Opsi Developer aktif"));
  } else showToast(RPTR("Opsi Developer sudah aktif"));
  navPush(SCR_DEVOPT);
}

// ---------------------------------------------------------------------
// loop: pengingat konfirmasi pin baru, refresh live, glide, simpan kalibrasi
// ---------------------------------------------------------------------
void devoptLoopPoll() {
  unsigned long now = millis();

  // pengingat sekali setelah boot (HP perlu menyala dulu): percobaan pin baru / percobaan dibatalkan
  if (!dvNoticeDone && now > 6000UL && rpPinBootState != 0) {
    dvNoticeDone = true;
    homeLayoutEnsure();
    int i = appIndexForScreen(SCR_DEVOPT);
    if (i >= 0 && homeHide[i]) { homeHide[i] = false; homeLayoutRebuildVis(); homeLayoutSave(); }   // pastikan app bisa dibuka
    if (rpPinBootState == 1) diNotify('d', RPTR("Pin baru dicoba: ketuk untuk simpan"), T().accent, 5000, false, "", SCR_DEVOPT, true);
    else                     diNotify('d', RPTR("Pin baru dibatalkan (tidak dikonfirmasi)"), T().danger, 4200, false, "", SCR_DEVOPT, true);
  }
  if (dvCalDirty && now - dvCalDirtyMs > 900UL) { devpinsSaveBattCal(); dvCalDirty = false; }   // simpan kalibrasi setelah berhenti diubah

  if (curScreen() != SCR_DEVOPT) return;

  if (dvGlide) {                                                 // scroll meluncur
    unsigned long dt = now - dvGlideT;
    if (dt > 0) {
      if (dt > 50) dt = 50;
      dvGlideT = now;
      float before = dvScroll + dvVel * (float)dt;
      dvScroll = before; dvClampScroll();
      if (dvScroll != before) dvVel = 0;
      float f = 1.0f - 0.0045f * (float)dt; if (f < 0) f = 0;
      dvVel *= f;
      if (fabsf(dvVel) < 0.02f) { dvGlide = false; dvVel = 0; }
      needRedraw = true;
    }
  }
  if (dvTab == 1 && !dvModal && now - dvLastRefresh >= 250UL) {   // tab Sensor: bacaan live 4x per detik
    dvLastRefresh = now;
    int p = rpPinV[DP_BATT_ADC];
    if (p >= 1 && p <= 20) {
      uint32_t a = 0, b = 0;
      for (int k = 0; k < 8; k++) { a += analogRead(p); b += analogReadMilliVolts(p); }
      dvAdcRaw = (uint16_t)(a / 8); dvAdcMv = (uint16_t)(b / 8);
    }
    int q = rpPinV[DP_NTC];
    if (q >= 1 && q <= 20) {
      uint32_t a = 0, b = 0;
      for (int k = 0; k < 8; k++) { a += analogRead(q); b += analogReadMilliVolts(q); }
      dvNtcRaw = (uint16_t)(a / 8); dvNtcMv = (uint16_t)(b / 8);
    }
    needRedraw = true;
  }
}

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
static void dvTabs(LGFX_Sprite& s) {
  const int w = (SCR_W - 16 - 6) / 2;
  const char* nm[2] = { "Pin", "Sensor" };
  for (int i = 0; i < 2; i++) {
    int xx = 8 + i * (w + 6); bool on = (dvTab == i);
    nyxAaRR(s, xx, DV_TAB_Y, w, DV_TAB_H, DV_TAB_H / 2, on ? T().accent : blend565(T().bg, T().text, 30));
    s.setTextSize(1); s.setTextColor(on ? T().bg : T().text);
    int tw = (int)strlen(nm[i]) * 6; s.setCursor(xx + (w - tw) / 2, DV_TAB_Y + (DV_TAB_H - 8) / 2); s.print(nm[i]);
    dvHit(xx, DV_TAB_Y, w, DV_TAB_H, DA_TAB, i, true);
  }
}

// tombol penuh lebar di dalam konten
static void dvButton(LGFX_Sprite& s, int y, const char* label, uint8_t act, bool enabled, bool primary) {
  int x = 8, w = SCR_W - 16, h = 26;
  if (y + h > dvViewTop() && y < dvViewBottom()) {
    uint16_t bg = !enabled ? blend565(T().bg, T().text, 18) : (primary ? T().accent : blend565(T().bg, T().text, 30));
    nyxAaRR(s, x, y, w, h, h / 2, bg);
    s.setTextSize(1); s.setTextColor(!enabled ? T().subtext : (primary ? T().bg : T().text));
    int tw = (int)strlen(label) * 6; s.setCursor(x + (w - tw) / 2, y + (h - 8) / 2); s.print(label);
    if (enabled) dvHit(x, y, w, h, act, 0, false);
  }
}

// satu baris pin; y = y layar atas baris
static void dvPinRow(LGFX_Sprite& s, int y, int id) {
  if (y + DV_ROW_H <= dvViewTop() || y >= dvViewBottom()) return;
  const int x = 8, w = SCR_W - 16, h = DV_ROW_H - 3;
  const int v = dvEdit[id];
  const bool edited = (v != rpPinV[id]);
  const int cf = dpConflict(dvEdit, id);
  const bool bad = (cf >= 0) || !dpValueOk(id, v);
  nyxAaRR(s, x, y, w, h, 10, dvPanel());
  if (bad) nyxAaRR(s, x, y + 6, 3, h - 12, 1, T().danger);
  else if (edited) nyxAaRR(s, x, y + 6, 3, h - 12, 1, T().accent);

  s.setTextSize(1);
  s.setTextColor(T().text); s.setCursor(x + 10, y + 5); s.print(RPTR(DP_DEFS[id].name));

  char sub[44]; uint16_t sc = T().subtext;
  if (cf >= 0)                               { snprintf(sub, sizeof(sub), RPTR("bentrok: %s"), RPTR(DP_DEFS[cf].name)); sc = T().danger; }
  else if (!dpValueOk(id, v))                { snprintf(sub, sizeof(sub), "%s", RPTR("pin tidak valid")); sc = T().danger; }
  else if (v >= 0 && dpWarnKind(v) == 1)     { snprintf(sub, sizeof(sub), "%s", RPTR("pin strapping")); sc = DV_WARN; }
  else if (v >= 0 && dpWarnKind(v) == 2)     { snprintf(sub, sizeof(sub), "%s", RPTR("pin USB")); sc = DV_WARN; }
  else if ((DP_DEFS[id].flags & DPF_ADC) && v >= 0 && !dpIsAdc1(v)) { snprintf(sub, sizeof(sub), "%s", RPTR("bukan ADC1")); sc = DV_WARN; }
  else if (edited)                           { snprintf(sub, sizeof(sub), RPTR("aktif: %d"), (int)rpPinV[id]); sc = T().accent; }
  else if (v != DP_DEFS[id].def)             { snprintf(sub, sizeof(sub), RPTR("bawaan: %d"), (int)DP_DEFS[id].def); }
  else                                       { snprintf(sub, sizeof(sub), "%s", RPTR("bawaan")); }
  s.setTextColor(sc); s.setCursor(x + 10, y + 17); s.print(sub);

  // kontrol: [-]  GPIO n  [+]
  const int bw = 26, bh = 22, vw = 52;
  int px = x + w - 8 - bw, mx = px - vw - 4 - bw, vx = mx + bw + 2, by = y + (h - bh) / 2;
  nyxAaRR(s, mx, by, bw, bh, 8, blend565(T().bg, T().text, 34));
  nyxAaRR(s, px, by, bw, bh, 8, blend565(T().bg, T().text, 34));
  s.setTextColor(T().text);
  s.fillRect(mx + 8, by + bh / 2 - 1, 10, 2, T().text);                                   // minus
  s.fillRect(px + 8, by + bh / 2 - 1, 10, 2, T().text); s.fillRect(px + 12, by + bh / 2 - 5, 2, 10, T().text);   // plus
  char val[10]; if (v < 0) snprintf(val, sizeof(val), "OFF"); else snprintf(val, sizeof(val), "%d", v);
  uint16_t vc = bad ? T().danger : (v != DP_DEFS[id].def ? T().accent : T().text);
  s.setTextColor(vc); int tw = (int)strlen(val) * 6; s.setCursor(vx + (vw - tw) / 2, by + (bh - 8) / 2); s.print(val);
  dvHit(mx, y, bw + 4, h, DA_PIN_DEC, id, false);
  dvHit(px - 4, y, bw + 12, h, DA_PIN_INC, id, false);
  dvHit(vx, y, vw, h, DA_PIN_DEF, id, false);
}

// isi tab Pin. Mengembalikan tinggi konten.
static int dvDrawPins(LGFX_Sprite& s, int y0) {
  int y = y0;
  const int x = 8, w = SCR_W - 16;
  s.setTextSize(1);

  if (rpPinBootState == 1) {                                       // percobaan: minta konfirmasi
    const char* bt = RPTR("Kalau layar & sentuh normal, simpan permanen. Kalau tidak, cabut daya: pin lama kembali sendiri.");
    int h = 19 + dvWrapH(w - 20, 10, bt) + 34;
    if (y + h > dvViewTop() && y < dvViewBottom()) {
      nyxAaRR(s, x, y, w, h, 12, blend565(T().bg, T().accent, 46));
      nyxAaRRRing(s, x, y, w, h, 12, T().accent);
      s.setTextColor(T().text); s.setCursor(x + 10, y + 7); s.print(RPTR("Pin baru sedang DICOBA"));
      dvWrap(s, x + 10, y + 19, w - 20, 10, 8, bt, T().subtext);
      int bw2 = (w - 24) / 2, by = y + h - 24;
      nyxAaRR(s, x + 8, by, bw2, 18, 9, T().accent);
      s.setTextColor(T().bg); { const char* l = RPTR("Simpan permanen"); s.setCursor(x + 8 + (bw2 - (int)strlen(l) * 6) / 2, by + 5); s.print(l); }
      nyxAaRR(s, x + 16 + bw2, by, bw2, 18, 9, blend565(T().bg, T().text, 40));
      s.setTextColor(T().text); { const char* l = RPTR("Batalkan & restart"); s.setCursor(x + 16 + bw2 + (bw2 - (int)strlen(l) * 6) / 2, by + 5); s.print(l); }
      dvHit(x + 8, by, bw2, 18, DA_KEEP, 0, false);
      dvHit(x + 16 + bw2, by, bw2, 18, DA_REVERT, 0, false);
    }
    y += h + 6;
  } else if (rpPinBootState == 2) {                                // percobaan sebelumnya dibatalkan otomatis
    const char* nt = RPTR("Percobaan pin sebelumnya tidak dikonfirmasi, jadi dibatalkan otomatis.");
    int h = 14 + dvWrapH(w - 20, 10, nt);
    if (y + h > dvViewTop() && y < dvViewBottom()) {
      nyxAaRR(s, x, y, w, h, 10, blend565(T().bg, T().danger, 40));
      dvWrap(s, x + 10, y + 7, w - 20, 10, 8, nt, T().text);
    }
    y += h + 6;
  }

  const char* lastGrp = "";
  for (int id = 0; id < DP_COUNT; id++) {
    if (!DP_DEFS[id].avail || (DP_DEFS[id].flags & DPF_LOCK)) continue;   // DEVPINS_V3: pin layar & sentuh tidak ditampilkan (cegah salah tekan)
    if (strcmp(DP_DEFS[id].group, lastGrp) != 0) {
      lastGrp = DP_DEFS[id].group;
      if (y + DV_HEAD_H > dvViewTop() && y < dvViewBottom()) { s.setTextColor(T().subtext); s.setCursor(x + 6, y + 8); s.print(RPTR(lastGrp)); }
      y += DV_HEAD_H;
    }
    dvPinRow(s, y, id);
    y += DV_ROW_H;
  }
  y += 4;

  int diff = dvDiffFromActive(), err = dpErrorCount(dvEdit);
  char lb[44];
  if (err > 0)       snprintf(lb, sizeof(lb), RPTR("Perbaiki %d pin bermasalah dulu"), err);
  else if (diff > 0) snprintf(lb, sizeof(lb), RPTR("Terapkan %d perubahan & restart"), diff);
  else               snprintf(lb, sizeof(lb), "%s", RPTR("Tidak ada perubahan"));
  dvButton(s, y, lb, DA_APPLY, diff > 0 && err == 0, true);   y += 32;
  dvButton(s, y, RPTR("Kembalikan semua ke bawaan"), DA_RESET, true, false);   y += 32;
  dvButton(s, y, RPTR("Sembunyikan Opsi Developer"), DA_LOCK, true, false);    y += 32;
  const char* ft = RPTR("Pin berlaku setelah restart. Pin layar & sentuh yang salah bisa membuat layar mati; sebab itu perubahan dicoba dulu dan baru permanen setelah kamu konfirmasi.");
  if (y + 10 > dvViewTop() && y < dvViewBottom()) dvWrap(s, x + 4, y + 2, w - 8, 10, 10, ft, T().subtext);
  y += dvWrapH(w - 8, 10, ft) + 14;
  return y - y0;
}

// satu baris label-nilai di kartu
static int dvKV(LGFX_Sprite& s, int x, int w, int y, const char* k, const char* v, uint16_t vc) {
  if (y + 14 > dvViewTop() && y < dvViewBottom()) {
    s.setTextSize(1);
    s.setTextColor(T().subtext); s.setCursor(x + 10, y + 3); s.print(k);
    s.setTextColor(vc);          s.setCursor(x + w - 10 - (int)strlen(v) * 6, y + 3); s.print(v);
  }
  return 14;
}
// baris label + stepper [-] nilai [+]
static int dvStepRow(LGFX_Sprite& s, int x, int w, int y, const char* k, const char* v, uint8_t decAct, uint8_t incAct) {
  const int bw = 24, bh = 18, vw = 62;
  if (y + 22 > dvViewTop() && y < dvViewBottom()) {
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(x + 10, y + 7); s.print(k);
    int px = x + w - 8 - bw, mx = px - vw - 4 - bw, vx = mx + bw + 2, by = y + 2;
    nyxAaRR(s, mx, by, bw, bh, 7, blend565(T().bg, T().text, 34));
    nyxAaRR(s, px, by, bw, bh, 7, blend565(T().bg, T().text, 34));
    s.fillRect(mx + 7, by + bh / 2 - 1, 10, 2, T().text);
    s.fillRect(px + 7, by + bh / 2 - 1, 10, 2, T().text); s.fillRect(px + 11, by + bh / 2 - 5, 2, 10, T().text);
    s.setTextColor(T().accent); s.setCursor(vx + (vw - (int)strlen(v) * 6) / 2, by + 5); s.print(v);
    dvHit(mx, y, bw + 4, 22, decAct, 0, false);
    dvHit(px - 4, y, bw + 12, 22, incAct, 0, false);
  }
  return 22;
}
static int dvCard(LGFX_Sprite& s, int y, int h, const char* title, uint16_t tc) {    // kartu + judul; mengembalikan y awal isi
  const int x = 8, w = SCR_W - 16;
  if (y + h > dvViewTop() && y < dvViewBottom()) {
    nyxAaRR(s, x, y, w, h, 12, dvPanel());
    s.setTextSize(1); s.setTextColor(tc); s.setCursor(x + 10, y + 8); s.print(title);
  }
  return y + 22;
}

static int dvDrawSensor(LGFX_Sprite& s, int y0) {
  int y = y0;
  const int x = 8, w = SCR_W - 16;
  char a[56], b[56];
  s.setTextSize(1);
  const int adcPin = rpPinV[DP_BATT_ADC], ntcPin = rpPinV[DP_NTC];

  // ---- sumber baterai yang dipakai ----
  if (inaOk) {
    // =============== INA219 ===============
    snprintf(a, sizeof(a), "INA219  (I2C 0x%02X)", inaAddr);
    int h = 22 + 14 * 14 + 8;
    int cy = dvCard(s, y, h, a, T().good);
    snprintf(a, sizeof(a), "GPIO %d / %d", (int)rpPinV[DP_SENS_SDA], (int)rpPinV[DP_SENS_SCL]);
    cy += dvKV(s, x, w, cy, RPTR("Pin SDA / SCL"), a, T().text);
    snprintf(a, sizeof(a), "%.3f V", inaBusV);                 cy += dvKV(s, x, w, cy, RPTR("Tegangan bus (VBUS)"), a, T().text);
    snprintf(a, sizeof(a), "%+.3f mV", inaShuntMv);            cy += dvKV(s, x, w, cy, RPTR("Tegangan shunt"), a, T().text);
    snprintf(a, sizeof(a), "%+.1f mA", inaI);                  cy += dvKV(s, x, w, cy, RPTR("Arus (+ dipakai, - diisi)"), a, inaI < -1.0f ? T().good : T().text);
    snprintf(a, sizeof(a), "%+.1f mW", inaP);                  cy += dvKV(s, x, w, cy, RPTR("Daya"), a, T().text);
    snprintf(a, sizeof(a), "%.3f V", inaBatV);                 cy += dvKV(s, x, w, cy, RPTR("Tegangan baterai"), a, T().accent);
    snprintf(a, sizeof(a), "%d %%", inaSoc);                   cy += dvKV(s, x, w, cy, RPTR("Kapasitas (SOC)"), a, T().text);
    snprintf(a, sizeof(a), "%s", inaCharging ? RPTR("Mengisi") : RPTR("Dipakai")); cy += dvKV(s, x, w, cy, RPTR("Status"), a, inaCharging ? T().good : T().text);
    snprintf(a, sizeof(a), "%u mOhm", (unsigned)inaShuntMohm); cy += dvKV(s, x, w, cy, RPTR("Resistor shunt"), a, T().text);
    snprintf(a, sizeof(a), "%u", (unsigned)inaCalReg);         cy += dvKV(s, x, w, cy, RPTR("Register kalibrasi"), a, T().text);
    snprintf(a, sizeof(a), "%.4f mA", inaCurLsbMa);            cy += dvKV(s, x, w, cy, RPTR("LSB arus"), a, T().text);
    snprintf(a, sizeof(a), "%.0f mA", inaFsMa);                cy += dvKV(s, x, w, cy, RPTR("Arus skala penuh"), a, T().text);
    snprintf(a, sizeof(a), "%s", inaOvf ? RPTR("YA") : RPTR("tidak")); cy += dvKV(s, x, w, cy, RPTR("Overflow"), a, inaOvf ? T().danger : T().text);
    snprintf(a, sizeof(a), "%s", inaActive ? RPTR("mengukur") : RPTR("diam")); dvKV(s, x, w, cy, RPTR("Konversi"), a, T().text);
    y += h + 6;
    if (adcPin >= 0) {
      if (y + 12 > dvViewTop() && y < dvViewBottom()) {
        snprintf(b, sizeof(b), "ADC GPIO %d tidak dipakai (INA219 aktif)", adcPin);
        s.setTextColor(T().subtext); s.setCursor(x + 6, y + 2); s.print(b);
      }
      y += 16;
    }
  } else if (adcPin >= 0) {
    // =============== ADC baterai: tampilkan perhitungannya ===============
    char chn[20]; dvAdcName(adcPin, chn, sizeof(chn));
    snprintf(a, sizeof(a), "ADC baterai  GPIO %d (%s)", adcPin, chn);
    const char* wn = RPTR("Pin ini bukan ADC1: bacaan bisa tidak stabil saat WiFi aktif.");
    int h = 22 + 14 * 3 + 4 + 22 * 2 + 2 + 14 * 3 + 8 + (dpIsAdc1(adcPin) ? 0 : dvWrapH(w - 20, 10, wn) + 4);
    int cy = dvCard(s, y, h, a, T().good);
    float pinV = (float)dvAdcMv / 1000.0f;
    float vbat = pinV * devBattDiv + devBattCal;
    snprintf(a, sizeof(a), "%u / 4095", (unsigned)dvAdcRaw);   cy += dvKV(s, x, w, cy, RPTR("Nilai mentah (rata2 8)"), a, T().text);
    snprintf(a, sizeof(a), "%u mV", (unsigned)dvAdcMv);        cy += dvKV(s, x, w, cy, RPTR("Tegangan di pin"), a, T().text);
    snprintf(a, sizeof(a), "%.3f V", pinV);                    cy += dvKV(s, x, w, cy, RPTR("= di pin (volt)"), a, T().text);
    cy += 4;
    snprintf(a, sizeof(a), "x%.2f", devBattDiv);               cy += dvStepRow(s, x, w, cy, RPTR("Rasio pembagi"), a, DA_DIV_DEC, DA_DIV_INC);
    snprintf(a, sizeof(a), "%+.3f V", devBattCal);             cy += dvStepRow(s, x, w, cy, RPTR("Ofset kalibrasi"), a, DA_CAL_DEC, DA_CAL_INC);
    cy += 2;
    snprintf(a, sizeof(a), "%.3f x %.2f %+.3f", pinV, devBattDiv, devBattCal);
    cy += dvKV(s, x, w, cy, RPTR("Rumus"), a, T().subtext);
    snprintf(a, sizeof(a), "%.3f V", vbat);                    cy += dvKV(s, x, w, cy, RPTR("Vbat hasil hitung"), a, T().accent);
    snprintf(a, sizeof(a), "%.3f V  /  %d %%", battVoltage, battPercent); cy += dvKV(s, x, w, cy, RPTR("Setelah filter / SOC"), a, T().text);
    if (!dpIsAdc1(adcPin) && cy + 10 > dvViewTop() && cy < dvViewBottom()) {
      dvWrap(s, x + 10, cy + 2, w - 20, 10, 6, wn, DV_WARN);
    }
    y += h + 6;
  }

  // ---- ADC suhu NTC (kalau pinnya diisi) ----
  if (ntcPin >= 0) {
    char chn[20]; dvAdcName(ntcPin, chn, sizeof(chn));
    snprintf(a, sizeof(a), "ADC NTC  GPIO %d (%s)", ntcPin, chn);
    int h = 22 + 14 * 2 + 8;
    int cy = dvCard(s, y, h, a, T().good);
    snprintf(a, sizeof(a), "%u / 4095", (unsigned)dvNtcRaw);   cy += dvKV(s, x, w, cy, RPTR("Nilai mentah (rata2 8)"), a, T().text);
    snprintf(a, sizeof(a), "%u mV", (unsigned)dvNtcMv);        dvKV(s, x, w, cy, RPTR("Tegangan di pin"), a, T().text);
    y += h + 6;
  }

  if (!inaOk && adcPin < 0 && ntcPin < 0) {
    snprintf(a, sizeof(a), "SDA GPIO %d, SCL GPIO %d", (int)rpPinV[DP_SENS_SDA], (int)rpPinV[DP_SENS_SCL]);
    const char* ns = RPTR("INA219 tidak terdeteksi di bus sensor, dan pin ADC baterai belum diisi (tab Pin).");
    int th = dvWrapH(w - 20, 10, ns);
    int h = 22 + th + 4 + 10 + 8;
    int cy = dvCard(s, y, h, RPTR("Tidak ada sensor baterai aktif"), DV_WARN);
    if (y + h > dvViewTop() && y < dvViewBottom()) {
      cy += dvWrap(s, x + 10, cy, w - 20, 10, 8, ns, T().text);
      s.setTextColor(T().subtext); s.setCursor(x + 10, cy + 4); s.print(a);
    }
    y += h + 6;
  }
  y += 8;
  return y - y0;
}

static void dvModalDraw(LGFX_Sprite& s) {
  if (!dvModal) return;
  const int w = SCR_W - 40, x = 20;
  const char* mt = (dvModal == 1)
      ? RPTR("HP restart dan mencoba pin baru. Sesudah menyala, buka Opsi Developer lalu Simpan permanen. Kalau layar mati, cabut daya: pin lama kembali.")
      : RPTR("Semua pin & kalibrasi ADC kembali ke bawaan papan, lalu HP restart.");
  const int h = 24 + dvWrapH(w - 24, 10, mt) + 40, y = (SCR_H - h) / 2 - 8;
  nyxAaRR(s, x - 2, y - 2, w + 4, h + 4, 16, blend565(T().bg, 0x0000, 120));
  nyxAaRR(s, x, y, w, h, 14, T().surface2);
  nyxAaRRRing(s, x, y, w, h, 14, T().accent);
  s.setTextSize(1);
  char t[44];
  if (dvModal == 1) snprintf(t, sizeof(t), RPTR("Terapkan %d perubahan pin?"), dvDiffFromActive());
  else              snprintf(t, sizeof(t), "%s", RPTR("Kembalikan ke bawaan papan?"));
  s.setTextColor(T().text); s.setCursor(x + 12, y + 10); s.print(t);
  dvWrap(s, x + 12, y + 24, w - 24, 10, 8, mt, T().subtext);
  const int bw = (w - 36) / 2, by = y + h - 28;
  nyxAaRR(s, x + 12, by, bw, 20, 10, blend565(T().bg, T().text, 40));
  s.setTextColor(T().text); { const char* l = RPTR("Batal"); s.setCursor(x + 12 + (bw - (int)strlen(l) * 6) / 2, by + 6); s.print(l); }
  nyxAaRR(s, x + 24 + bw, by, bw, 20, 10, T().accent);
  s.setTextColor(T().bg);   { const char* l = RPTR("Lanjut"); s.setCursor(x + 24 + bw + (bw - (int)strlen(l) * 6) / 2, by + 6); s.print(l); }
  dvHitN = 0;                                                   // modal: hanya tombolnya yang aktif
  dvHit(x + 12, by, bw, 20, DA_CANCEL, 0, true);
  dvHit(x + 24 + bw, by, bw, 20, DA_OK, 0, true);
}

void drawDevopt(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, DV_TITLE_Y, RPTR("Opsi Developer"));

  dvHitN = 0;
  dvTabs(s);

  const int top = dvViewTop(), bot = dvViewBottom();
  s.setClipRect(0, top, SCR_W, bot - top);
  int y0 = top - (int)dvScroll;
  dvContentH = (dvTab == 0) ? dvDrawPins(s, y0) : dvDrawSensor(s, y0);
  s.clearClipRect();
  dvClampScroll();

  drawBack(s);
  dvModalDraw(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// aksi
// ---------------------------------------------------------------------
static void dvStepCal(int act) {
  if (act == DA_DIV_DEC) devBattDiv -= 0.05f;
  if (act == DA_DIV_INC) devBattDiv += 0.05f;
  if (act == DA_CAL_DEC) devBattCal -= 0.005f;
  if (act == DA_CAL_INC) devBattCal += 0.005f;
  if (devBattDiv < 0.5f) devBattDiv = 0.5f;
  if (devBattDiv > 10.0f) devBattDiv = 10.0f;
  if (devBattCal < -1.0f) devBattCal = -1.0f;
  if (devBattCal > 1.0f) devBattCal = 1.0f;
  dvCalDirty = true; dvCalDirtyMs = millis();
}

static void dvDoAction(int act, int arg) {
  switch (act) {
    case DA_TAB:
      if (dvTab != (uint8_t)arg) { dvTab = (uint8_t)arg; dvScroll = 0; dvVel = 0; dvGlide = false; dvLastRefresh = 0; }
      break;
    case DA_PIN_DEC: if (arg >= 0 && arg < DP_COUNT) dvEdit[arg] = (int8_t)dpStep(arg, dvEdit[arg], -1); break;
    case DA_PIN_INC: if (arg >= 0 && arg < DP_COUNT) dvEdit[arg] = (int8_t)dpStep(arg, dvEdit[arg], +1); break;
    case DA_PIN_DEF: if (arg >= 0 && arg < DP_COUNT) dvEdit[arg] = DP_DEFS[arg].def; break;
    case DA_APPLY:   if (dvDiffFromActive() > 0 && dpErrorCount(dvEdit) == 0) dvModal = 1; break;
    case DA_RESET:   dvModal = 2; break;
    case DA_LOCK: {
      int i = appIndexForScreen(SCR_DEVOPT);
      if (i >= 0) { homeHide[i] = true; homeLayoutRebuildVis(); homeLayoutSave(); }
      showToast(RPTR("Opsi Developer disembunyikan"));
      navBack();
      return;
    }
    case DA_KEEP:
      if (devpinsCommit()) showToast(RPTR("Pin disimpan permanen")); else showToast(RPTR("Gagal menyimpan pin"));
      break;
    case DA_REVERT:
      devpinsDiscardTrial(); showToast(RPTR("Dibatalkan, restart..."), 1500); needRedrawNow(); delay(300); powerRestartNow();
      return;
    case DA_CANCEL: dvModal = 0; break;
    case DA_OK:
      if (dvModal == 1) {
        if (devpinsSaveTrial(dvEdit)) { showToast(RPTR("Menyimpan & restart..."), 1500); needRedrawNow(); delay(300); powerRestartNow(); return; }
        showToast(RPTR("Gagal menyimpan pin"));
      } else if (dvModal == 2) {
        devpinsEraseAll(); showToast(RPTR("Kembali ke bawaan, restart..."), 1500); needRedrawNow(); delay(300); powerRestartNow(); return;
      }
      dvModal = 0;
      break;
    case DA_DIV_DEC: case DA_DIV_INC: case DA_CAL_DEC: case DA_CAL_INC: dvStepCal(act); break;
  }
  needRedraw = true;
}

static bool dvIsRepeat(int act) {
  return act == DA_PIN_DEC || act == DA_PIN_INC || act == DA_DIV_DEC || act == DA_DIV_INC || act == DA_CAL_DEC || act == DA_CAL_INC;
}
static int dvFindHit(int x, int y) {
  const int top = dvViewTop(), bot = dvViewBottom();
  for (int i = 0; i < dvHitN; i++) {
    const DvHit& t = dvHits[i];
    if (x < t.x || x >= t.x + t.w || y < t.y || y >= t.y + t.h) continue;
    if (!t.fixed && (y < top || y >= bot)) continue;
    return i;
  }
  return -1;
}

void devoptTouch(int x, int y, bool held, bool isNew) {
  unsigned long now = millis();

  if (isNew) {
    dvTouching = true; dvMoved = false; dvPend = -1; dvRepAct = 0;
    dvGlide = false; dvVel = 0;
    int h = dvFindHit(x, y);
    if (dvModal) { if (h >= 0) dvDoAction(dvHits[h].act, dvHits[h].arg); return; }
    if (isBack(x, y)) { navBack(); return; }
    if (h >= 0) {
      const DvHit& t = dvHits[h];
      if (dvIsRepeat(t.act)) {                                   // stepper: langsung jalan, tahan = berulang
        dvDoAction(t.act, t.arg);
        dvRepAct = t.act; dvRepArg = t.arg; dvRepT0 = now; dvRepLast = now;
        return;
      }
      dvPend = h;                                                // tombol biasa: dieksekusi saat dilepas kalau tidak digeser
    }
    dvStartY = y; dvLastY = y; dvLastMs = now;
    dvDrag = (y >= dvViewTop() && y <= dvViewBottom());
    return;
  }

  if (!held) {                                                   // jari diangkat
    if (!dvTouching) return;
    dvTouching = false;
    dvRepAct = 0;
    if (dvPend >= 0 && !dvMoved && dvPend < dvHitN) { int h = dvPend; dvPend = -1; dvDoAction(dvHits[h].act, dvHits[h].arg); return; }
    dvPend = -1;
    if (dvDrag) {
      dvDrag = false;
      if (dvMoved && now - dvLastMs < 90UL && fabsf(dvVel) > 0.05f && dvMax > 0) {
        if (dvVel > 3.0f) dvVel = 3.0f;
        if (dvVel < -3.0f) dvVel = -3.0f;
        dvGlide = true; dvGlideT = now;
      } else dvVel = 0;
    }
    return;
  }

  if (dvModal) return;
  if (dvRepAct) {                                                // tahan stepper
    if (now - dvRepT0 > 450UL && now - dvRepLast > 80UL) { dvRepLast = now; dvDoAction(dvRepAct, dvRepArg); }
    return;
  }
  if (abs(y - dvStartY) > 8) { dvMoved = true; dvPend = -1; }
  if (!dvDrag) return;
  int dy = dvLastY - y;
  dvLastY = y;
  if (dy == 0) return;
  unsigned long dt = now - dvLastMs; if (dt < 1) dt = 1;
  dvLastMs = now;
  dvVel = dvVel * 0.5f + ((float)dy / (float)dt) * 0.5f;
  if (dvMoved) { dvScroll += (float)dy; dvClampScroll(); needRedraw = true; }
}
