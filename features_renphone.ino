// =====================================================================
// features_renphone.ino -- 4 fitur UI baru Accretion / Ren Phone
//   1. SCREENSHOT      -- tombol "Tangkap" di Control Center, simpan BMP ke /screenshot di SD
//   2. STOPWATCH+TIMER -- app baru (SCR_STOPWATCH), tetap jalan di latar belakang
//   3. VOICE MEMO      -- app baru (SCR_MEMO), rekam mic INMP441 -> WAV di /memo,
//                         playback lewat jalur Musik BT (S3 -> ESP32-CAM -> TWS)
//   4. WIDGET CUACA    -- kartu di Home, data Open-Meteo (tanpa API key)
//
// File ini DIPANGGIL dari phone.ino lewat patch apply_features_patch.py:
//   featuresLoopPoll()   <- dipanggil tiap loop() (dekat musicLoopPoll())
//   ccActScreenshot()    <- aksi tombol ke-11 Control Center
//   drawWeatherWidget()  <- digambar di drawHome(), ketuk = refresh cuaca
//   swEnter/drawSw/...   <- app Stopwatch & Timer di apps[]
//   memoEnter/drawMemo.. <- app Voice Memo di apps[]
//
// Aturan proyek yang diikuti: stack task di PSRAM (createTaskPsramStack),
// SEMUA file I/O SD dari task yang sama dgn pola NES/APOD, mic dipakai
// gantian lewat micTaskHandle (wake word + Mic Level otomatis mundur),
// layout dihitung dari SCR_W/SCR_H (landscape & portrait).
// =====================================================================

// ---- fungsi yang didefinisikan di file lain ----
void memoPlayFile(const char* path);   // musicbt_renphone.ino
void memoStopPlay();
bool memoPlayActive();
uint32_t memoPlayPosMs();
bool memoBtReady();
void memoPrepareBt();

// =====================================================================
// UTIL KECIL
// =====================================================================
static bool fxHit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

// =====================================================================
// 1. SCREENSHOT
// =====================================================================
// Alur: tombol CC -> ccActScreenshot() menandai permintaan + menutup panel CC.
// screenshotPoll() (tiap loop) menunggu sampai panel benar2 hilang & frame
// bersih sudah tergambar, baru membaca isi `canvas` (frame persis yg tampil
// di layar) dan menulisnya sebagai BMP 24-bit ke /screenshot/ss_*.bmp.
// Dibaca pakai readPixel() (jalur yg sama dgn efek kaca di drawGlassPanel,
// sudah terbukti jalan di perangkat) -- BUKAN getBuffer() mentah.
static volatile bool shotPending = false;
static unsigned long shotReqMs = 0, shotArmedMs = 0;

void ccActScreenshot() {
  shotPending = true;
  shotReqMs = millis();
  shotArmedMs = 0;
  closeControlCenter();   // panel CC menutup dulu supaya tidak ikut terfoto
  needRedraw = true;
}

static void shotPut32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static bool screenshotSaveNow() {
  SD_MMC.mkdir("/screenshot");
  char path[56];
  struct tm t;
  if (ntpSynced && getLocalTime(&t, 5)) {
    snprintf(path, sizeof(path), "/screenshot/ss_%04d%02d%02d_%02d%02d%02d.bmp",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(path, sizeof(path), "/screenshot/ss_%lu.bmp", (unsigned long)millis());
  }
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { showToast("Gagal simpan screenshot"); vibError(); return false; }

  const int w = canvas.width(), h = canvas.height();
  const uint32_t rowSize = (uint32_t)(((w * 3) + 3) / 4) * 4;
  const uint32_t imgSize = rowSize * (uint32_t)h;
  uint8_t hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B'; hdr[1] = 'M';
  shotPut32(hdr + 2, 54 + imgSize);   // ukuran file
  shotPut32(hdr + 10, 54);            // offset data piksel
  shotPut32(hdr + 14, 40);            // ukuran BITMAPINFOHEADER
  shotPut32(hdr + 18, (uint32_t)w);
  shotPut32(hdr + 22, (uint32_t)h);   // tinggi positif = baris dari bawah ke atas
  hdr[26] = 1; hdr[28] = 24;          // 1 plane, 24 bit
  shotPut32(hdr + 34, imgSize);
  shotPut32(hdr + 38, 2835); shotPut32(hdr + 42, 2835);
  f.write(hdr, 54);

  uint8_t* row = (uint8_t*)heap_caps_malloc(rowSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!row) row = (uint8_t*)malloc(rowSize);
  if (!row) { f.close(); SD_MMC.remove(path); showToast("RAM tidak cukup"); vibError(); return false; }

  bool ok = true;
  for (int y = h - 1; y >= 0 && ok; y--) {
    memset(row, 0, rowSize);
    for (int x = 0; x < w; x++) {
      uint16_t c = canvas.readPixel(x, y);
      uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
      row[x * 3 + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
      row[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
      row[x * 3 + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
    }
    if (f.write(row, rowSize) != rowSize) ok = false;
  }
  free(row);
  f.close();
  if (!ok) { SD_MMC.remove(path); showToast("Screenshot gagal (SD penuh?)"); vibError(); return false; }

  showToast("Screenshot tersimpan di /screenshot", 1800);
  vibSuccess();
  return true;
}

static void screenshotPoll() {
  if (!shotPending) return;
  bool overlay = controlCenterOpen || ccAnimating || notifShadeOpen || appSwitcherOpen;
  if (overlay) { shotArmedMs = 0; return; }
  unsigned long now = millis();
  bool settled = !needRedraw && !toastVisibleNow();
  bool forced = (now - shotReqMs) > 1500;   // app yg terus2an redraw: jangan nunggu selamanya
  if (!settled && !forced) { shotArmedMs = 0; return; }
  if (shotArmedMs == 0) { shotArmedMs = now; return; }
  if (!forced && now - shotArmedMs < 120) return;
  shotPending = false;
  screenshotSaveNow();
}

// =====================================================================
// 2. STOPWATCH & TIMER  (SCR_STOPWATCH)
// =====================================================================
static int  swTab = 0;                       // 0 = stopwatch, 1 = timer
static bool swRunning = false;
static unsigned long swElapsed = 0, swLastMs = 0, swLastRedraw = 0;
#define SW_MAX_LAPS 30
static unsigned long swLaps[SW_MAX_LAPS];
static int swLapN = 0;

static bool tmRunning = false;
static unsigned long tmTotal = 300000UL, tmRemain = 300000UL, tmLastMs = 0;
static bool tmRinging = false;
static unsigned long tmRingLast = 0;
static int tmRingCnt = 0;
static const int TM_PRESET_MIN[7] = {1, 3, 5, 10, 15, 30, 60};
static int tmPresetIdx = 2;

static void swFmt(char* out, int cap, unsigned long ms, bool cs) {
  unsigned long totalS = ms / 1000UL;
  unsigned long hh = totalS / 3600UL, mm = (totalS / 60UL) % 60UL, ss = totalS % 60UL;
  unsigned long cc = (ms / 10UL) % 100UL;
  if (hh > 0)  snprintf(out, cap, "%lu:%02lu:%02lu", hh, mm, ss);
  else if (cs) snprintf(out, cap, "%02lu:%02lu.%02lu", mm, ss, cc);
  else         snprintf(out, cap, "%02lu:%02lu", mm, ss);
}

static void swStopRing() {
  if (tmRinging) {
    tmRinging = false;
    if (!npxOn) npxOff();
  }
}

static void swTimerFinished() {
  tmRinging = true; tmRingCnt = 0; tmRingLast = 0;
  showToast("Timer selesai!", 2500);
  diNotify('t', String("Timer selesai"), T().accent, 4500, false, "", SCR_STOPWATCH, true);
  if (curScreen() != SCR_STOPWATCH) notifAdd(SCR_STOPWATCH, 1);
  needRedraw = true;
}

static void swTick() {
  unsigned long now = millis();
  bool vis = (curScreen() == SCR_STOPWATCH);
  if (swRunning) {
    swElapsed += now - swLastMs; swLastMs = now;
    if (vis && swTab == 0 && now - swLastRedraw >= 50) { needRedraw = true; swLastRedraw = now; }
  }
  if (tmRunning) {
    unsigned long dt = now - tmLastMs; tmLastMs = now;
    if (dt >= tmRemain) { tmRemain = 0; tmRunning = false; swTimerFinished(); }
    else {
      tmRemain -= dt;
      if (vis && swTab == 1 && now - swLastRedraw >= 100) { needRedraw = true; swLastRedraw = now; }
    }
  }
  if (tmRinging && now - tmRingLast >= 700) {   // getar + kedip LED (kalau LED tidak dipakai user)
    tmRingLast = now; tmRingCnt++;
    vibSuccess();
    if (!npxOn) npxApply((tmRingCnt & 1) ? 255 : 0, (tmRingCnt & 1) ? 70 : 0, 0);
    if (vis) needRedraw = true;
    if (tmRingCnt >= 10) swStopRing();
  }
}

void swEnter() {}
void swExit() {}

// geometri yg SAMA dipakai drawSw() & swTouch()
static void swGeom(int& tabY, int& tabH, int& tabW, int& btnY, int& btnH, int& sideW, int& midW, int& x1, int& x2, int& x3) {
  tabY = 40; tabH = 22; tabW = (SCR_W - 16 - 6) / 2;
  btnH = 26; btnY = SCR_H - BACK_H - 3 - 6 - btnH;
  int rowW = SCR_W - 16, gapB = 6;
  midW = 110; if (midW > rowW / 2) midW = rowW / 2;
  sideW = (rowW - midW - 2 * gapB) / 2;
  x1 = 8; x2 = x1 + sideW + gapB; x3 = x2 + midW + gapB;
}

static void swBtn(LGFX_Sprite& s, int x, int y, int w, int h, const char* lb, uint16_t tint, uint16_t txt, uint8_t a) {
  drawGlassPanel(s, x, y, w, h, 8, tint, a);
  s.setTextSize(1); s.setTextColor(txt);
  s.setCursor(x + w / 2 - s.textWidth(lb) / 2, y + 8); s.print(lb);
}

void drawSw(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);
  iosTitle(s, 8, 26, swTab == 0 ? "Stopwatch" : "Timer");

  int tabY, tabH, tabW, btnY, btnH, sideW, midW, x1, x2, x3;
  swGeom(tabY, tabH, tabW, btnY, btnH, sideW, midW, x1, x2, x3);

  // ---- tab ----
  s.setTextSize(1);
  for (int i = 0; i < 2; i++) {
    int x = 8 + i * (tabW + 6);
    bool on = (swTab == i);
    drawGlassPanel(s, x, tabY, tabW, tabH, 10, on ? T().accent : T().surface2, on ? 210 : 150);
    const char* lb = (i == 0) ? "Stopwatch" : "Timer";
    s.setTextColor(on ? T().bg : T().subtext);
    s.setCursor(x + tabW / 2 - s.textWidth(lb) / 2, tabY + 7); s.print(lb);
  }

  int ts = (SCR_W >= 300) ? 5 : 4;
  int timeY = tabY + tabH + 14;
  char tb[16];

  if (swTab == 0) {
    // ---- STOPWATCH ----
    swFmt(tb, sizeof(tb), swElapsed, true);
    s.setTextSize(ts); s.setTextColor(swRunning ? T().text : T().subtext);
    int tw = s.textWidth(tb);
    s.setCursor(SCR_W / 2 - tw / 2, timeY); s.print(tb);
    s.setTextSize(1);

    int lapTop = timeY + 8 * ts + 10;
    int lineH = 14;
    int lines = (btnY - 6 - lapTop) / lineH;
    if (lines > 8) lines = 8;
    if (swLapN == 0) {
      s.setTextColor(T().subtext);
      const char* hint = "Ketuk Lap untuk mencatat putaran";
      s.setCursor(SCR_W / 2 - s.textWidth(hint) / 2, lapTop + 4); s.print(hint);
    } else {
      for (int k = 0; k < lines && k < swLapN; k++) {
        int idx = swLapN - 1 - k;                    // terbaru di atas
        unsigned long prev = idx > 0 ? swLaps[idx - 1] : 0;
        char a[16], b[16], ln[48];
        swFmt(a, sizeof(a), swLaps[idx], true);
        swFmt(b, sizeof(b), swLaps[idx] - prev, true);
        int ly = lapTop + k * lineH;
        s.setTextColor(T().subtext); snprintf(ln, sizeof(ln), "Lap %d", idx + 1);
        s.setCursor(16, ly); s.print(ln);
        s.setTextColor(T().accent); s.setCursor(SCR_W / 2 - 10, ly); s.print(b);
        s.setTextColor(T().text);   s.setCursor(SCR_W - 16 - s.textWidth(a), ly); s.print(a);
      }
    }
    swBtn(s, x1, btnY, sideW, btnH, "Reset", T().surface2, T().subtext, 190);
    swBtn(s, x2, btnY, midW, btnH, swRunning ? "Henti" : "Mulai", swRunning ? T().danger : T().accent, T().bg, 210);
    swBtn(s, x3, btnY, sideW, btnH, "Lap", T().surface2, swRunning ? T().text : T().subtext, 190);
  } else {
    // ---- TIMER ----
    swFmt(tb, sizeof(tb), tmRemain + (tmRunning ? 999UL : 0UL), false);   // bulatkan ke atas biar 00:00 baru muncul pas selesai
    uint16_t tcol = T().text;
    if (tmRinging) tcol = (tmRingCnt & 1) ? T().danger : T().text;
    else if (!tmRunning) tcol = T().subtext;
    s.setTextSize(ts); s.setTextColor(tcol);
    int tw = s.textWidth(tb);
    s.setCursor(SCR_W / 2 - tw / 2, timeY); s.print(tb);
    s.setTextSize(1);
    if (tmRinging) {
      s.setTextColor(T().danger);
      const char* lb = "SELESAI!";
      s.setCursor(SCR_W / 2 - s.textWidth(lb) / 2, timeY - 11); s.print(lb);
    }

    int barY = timeY + 8 * ts + 10, barX = 16, barW = SCR_W - 32;
    s.fillRoundRect(barX, barY, barW, 6, 3, blend565(T().surface, 0xFFFF, 45));
    float p = tmTotal ? 1.0f - (float)tmRemain / (float)tmTotal : 0.0f;
    if (p < 0) p = 0; if (p > 1) p = 1;
    int fw = (int)(barW * p);
    if (fw > 0) s.fillRoundRect(barX, barY, max(fw, 6), 6, 3, tmRinging ? T().danger : T().accent);

    // chip penyesuaian durasi (cuma aktif saat belum jalan)
    const char* chipLb[4] = {"-1m", "+1m", "-10s", "+10s"};
    int chipY = barY + 16, chipH = 22, chipGap = 6, chipW = (SCR_W - 16 - 3 * chipGap) / 4;
    bool canAdj = !tmRunning && !tmRinging;
    for (int i = 0; i < 4; i++) {
      int cx = 8 + i * (chipW + chipGap);
      drawGlassPanel(s, cx, chipY, chipW, chipH, 8, T().surface2, canAdj ? 170 : 90);
      s.setTextColor(canAdj ? T().text : T().divider);
      s.setCursor(cx + chipW / 2 - s.textWidth(chipLb[i]) / 2, chipY + 7); s.print(chipLb[i]);
    }

    swBtn(s, x1, btnY, sideW, btnH, "Reset", T().surface2, T().subtext, 190);
    const char* mid = tmRinging ? "Stop" : (tmRunning ? "Jeda" : "Mulai");
    swBtn(s, x2, btnY, midW, btnH, mid, (tmRunning || tmRinging) ? T().danger : T().accent, T().bg, 210);
    char pb[12]; snprintf(pb, sizeof(pb), "%dm", TM_PRESET_MIN[tmPresetIdx]);
    swBtn(s, x3, btnY, sideW, btnH, pb, T().surface2, canAdj ? T().accent : T().divider, 190);
  }
  drawBack(s); drawToast(s);
}

void swTouch(int x, int y, bool held, bool isNew) {
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }

  int tabY, tabH, tabW, btnY, btnH, sideW, midW, x1, x2, x3;
  swGeom(tabY, tabH, tabW, btnY, btnH, sideW, midW, x1, x2, x3);

  for (int i = 0; i < 2; i++) {
    int tx = 8 + i * (tabW + 6);
    if (fxHit(x, y, tx, tabY, tabW, tabH)) {
      if (swTab != i) { swTab = i; vibTap(); needRedraw = true; }
      return;
    }
  }

  if (swTab == 0) {
    if (fxHit(x, y, x1, btnY, sideW, btnH)) {            // Reset
      swRunning = false; swElapsed = 0; swLapN = 0;
      vibTap(); needRedraw = true; return;
    }
    if (fxHit(x, y, x2, btnY, midW, btnH)) {             // Mulai / Henti
      swRunning = !swRunning;
      if (swRunning) swLastMs = millis();
      vibTap(); needRedraw = true; return;
    }
    if (fxHit(x, y, x3, btnY, sideW, btnH)) {            // Lap
      if (swRunning && swLapN < SW_MAX_LAPS) { swLaps[swLapN++] = swElapsed; vibTap(); needRedraw = true; }
      return;
    }
  } else {
    int ts = (SCR_W >= 300) ? 5 : 4;
    int timeY = tabY + tabH + 14;
    int barY = timeY + 8 * ts + 10;
    int chipY = barY + 16, chipH = 22, chipGap = 6, chipW = (SCR_W - 16 - 3 * chipGap) / 4;
    bool canAdj = !tmRunning && !tmRinging;
    if (canAdj) {
      const long delta[4] = {-60000L, 60000L, -10000L, 10000L};
      for (int i = 0; i < 4; i++) {
        int cx = 8 + i * (chipW + chipGap);
        if (fxHit(x, y, cx, chipY, chipW, chipH)) {
          long nt = (long)tmTotal + delta[i];
          if (nt < 10000L) nt = 10000L;
          if (nt > 86400000L) nt = 86400000L;
          tmTotal = (unsigned long)nt; tmRemain = tmTotal;
          vibTap(); needRedraw = true; return;
        }
      }
    }
    if (fxHit(x, y, x1, btnY, sideW, btnH)) {            // Reset
      swStopRing(); tmRunning = false; tmRemain = tmTotal;
      vibTap(); needRedraw = true; return;
    }
    if (fxHit(x, y, x2, btnY, midW, btnH)) {             // Mulai / Jeda / Stop
      if (tmRinging) { swStopRing(); tmRemain = tmTotal; }
      else if (tmRunning) { tmRunning = false; }
      else {
        if (tmRemain == 0) tmRemain = tmTotal;
        tmRunning = true; tmLastMs = millis();
      }
      vibTap(); needRedraw = true; return;
    }
    if (fxHit(x, y, x3, btnY, sideW, btnH)) {            // Preset (siklus)
      if (canAdj) {
        tmPresetIdx = (tmPresetIdx + 1) % 7;
        tmTotal = (unsigned long)TM_PRESET_MIN[tmPresetIdx] * 60000UL; tmRemain = tmTotal;
        vibTap(); needRedraw = true;
      }
      return;
    }
  }
}

// =====================================================================
// 3. VOICE MEMO  (SCR_MEMO)
// =====================================================================
// Rekam : micI2S (16 kHz, 16-bit, mono) -> file WAV di /memo/m_YYYYMMDD_HHMMSS.wav,
//         ditulis ke SD per 16 KB dari task rekam (stack di PSRAM). Header WAV
//         ditulis dulu (panjang 0) lalu ditambal saat selesai.
// Putar : WAV dikirim UTUH lewat jalur Musik (musPlayWav di musicbt_renphone.ino);
//         firmware CAM mengenali "RIFF" lalu melewati decoder MP3 dan
//         me-resample 16 kHz mono -> 44,1 kHz stereo ke A2DP.
// Mic dipakai gantian dgn fitur lain lewat micTaskHandle (sama dgn dictation).
#define MEMO_DIR          "/memo"
#define MEMO_MAX_FILES    60
#define MEMO_RATE         16000
#define MEMO_MAX_SEC      900          // 15 menit per rekaman
// geser bit mic: 16 = level dictation (pelan), 14 = ~4x lebih keras (di-clip).
// Kalau rekaman terdengar pecah turunkan jadi 15/16, kalau pelan naikkan ke 13.
#define MEMO_SAMPLE_SHIFT 14

static char     memoNames[MEMO_MAX_FILES][32];
static uint32_t memoSizes[MEMO_MAX_FILES];
static int      memoCount = 0, memoSel = -1, memoScroll = 0;
static volatile bool     memoRec = false, memoStopReq = false, memoListDirty = true;
static volatile bool     memoRecFail = false;
static volatile uint32_t memoRecBytes = 0;
static volatile float    memoLevel = 0;
static char     memoCurPath[48];
static char     memoPlayPath[48];
static char     memoFailMsg[40];
static StackType_t* memoTaskStack = nullptr;
static StaticTask_t memoTaskTCB;
static unsigned long memoDelArmUntil = 0, memoLastRedraw = 0;
static int      memoDragLastY = 0, memoDragAcc = 0;

// layout (dihitung ulang tiap gambar/sentuh supaya ikut orientasi)
static int mlRecCx, mlRecCy, mlRecR, mlTimeY, mlLvlX, mlLvlY, mlLvlW;
static int mlListX, mlListY, mlListW, mlRowH, mlRows;
static int mlBtnY, mlBtnH, mlBtn1X, mlBtn2X, mlBtnW;

static void memoLayout() {
  bool land = SCR_W >= 300;
  mlRowH = 22; mlRecR = 34; mlBtnH = 26;
  mlBtnY = SCR_H - BACK_H - 3 - 6 - mlBtnH;
  if (land) {
    mlRecCx = 8 + 59; mlRecCy = 40 + 4 + mlRecR;
    mlTimeY = mlRecCy + mlRecR + 10;
    mlLvlX = 14; mlLvlW = 106; mlLvlY = mlTimeY + 18;
    mlListX = 134; mlListY = 40; mlListW = SCR_W - 8 - 134;
    mlBtnW = (mlListW - 6) / 2; mlBtn1X = mlListX; mlBtn2X = mlListX + mlBtnW + 6;
  } else {
    mlRecCx = SCR_W / 2; mlRecCy = 40 + 4 + mlRecR;
    mlTimeY = mlRecCy + mlRecR + 10;
    mlLvlX = 24; mlLvlW = SCR_W - 48; mlLvlY = mlTimeY + 16;
    mlListX = 8; mlListY = mlLvlY + 18; mlListW = SCR_W - 16;
    mlBtnW = (mlListW - 6) / 2; mlBtn1X = 8; mlBtn2X = 8 + mlBtnW + 6;
  }
  mlRows = (mlBtnY - 6 - mlListY) / mlRowH;
  if (mlRows < 1) mlRows = 1;
}

static void memoScan() {
  memoCount = 0;
  File dir = SD_MMC.open(MEMO_DIR);
  if (dir && dir.isDirectory()) {
    File f = dir.openNextFile();
    while (f && memoCount < MEMO_MAX_FILES) {
      if (!f.isDirectory()) {
        String nm = String(f.name());
        int sl = nm.lastIndexOf('/');
        if (sl >= 0) nm = nm.substring(sl + 1);
        String low = nm; low.toLowerCase();
        if (low.endsWith(".wav") && !nm.startsWith(".") && nm.length() < 31) {
          strncpy(memoNames[memoCount], nm.c_str(), 31);
          memoNames[memoCount][31] = 0;
          memoSizes[memoCount] = (uint32_t)f.size();
          memoCount++;
        }
      }
      f.close();
      f = dir.openNextFile();
    }
    dir.close();
  } else if (dir) {
    dir.close();
  }
  // urut menurun (terbaru dulu, nama berstempel waktu) -- insertion sort
  for (int i = 1; i < memoCount; i++) {
    char tn[32]; uint32_t ts = memoSizes[i];
    memcpy(tn, memoNames[i], 32);
    int j = i - 1;
    while (j >= 0 && strcmp(memoNames[j], tn) < 0) {
      memcpy(memoNames[j + 1], memoNames[j], 32);
      memoSizes[j + 1] = memoSizes[j];
      j--;
    }
    memcpy(memoNames[j + 1], tn, 32);
    memoSizes[j + 1] = ts;
  }
  if (memoSel >= memoCount) memoSel = memoCount - 1;
  int maxScroll = memoCount - mlRows; if (maxScroll < 0) maxScroll = 0;
  if (memoScroll > maxScroll) memoScroll = maxScroll;
  memoListDirty = false;
}

static void memoPretty(char* out, int cap, const char* nm) {
  // "m_YYYYMMDD_HHMMSS.wav" -> "DD/MM HH:MM:SS"
  if (strlen(nm) >= 17 && nm[0] == 'm' && nm[1] == '_' && nm[10] == '_') {
    snprintf(out, cap, "%c%c/%c%c %c%c:%c%c:%c%c", nm[8], nm[9], nm[6], nm[7],
             nm[11], nm[12], nm[13], nm[14], nm[15], nm[16]);
  } else {
    strncpy(out, nm, cap - 1); out[cap - 1] = 0;
  }
}

static uint32_t memoDurMsOfSize(uint32_t sz) {
  return sz > 44 ? (uint32_t)(((uint64_t)(sz - 44) * 1000ULL) / (uint64_t)(MEMO_RATE * 2)) : 0;
}

static void memoFmtMs(char* out, int cap, uint32_t ms) {
  uint32_t s = ms / 1000UL;
  snprintf(out, cap, "%02lu:%02lu", (unsigned long)(s / 60UL), (unsigned long)(s % 60UL));
}

static void memoRecTask(void* arg) {
  micEnsureI2S();
  const int CH_SAMPLES = 8192;                       // 16 KB per tulis
  int16_t* chunk = (int16_t*)heap_caps_malloc(CH_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  File f = SD_MMC.open(memoCurPath, FILE_WRITE);
  bool ok = (chunk != nullptr) && f;
  uint8_t hdr[WAV_HEADER_SIZE];
  uint32_t total = 0;
  if (ok) {
    micWriteWavHeader(hdr, 0, MEMO_RATE);
    ok = (f.write(hdr, WAV_HEADER_SIZE) == WAV_HEADER_SIZE);
  }
  int n = 0;
  int32_t raw[256 * 2];
  const uint32_t maxBytes = (uint32_t)MEMO_RATE * 2UL * MEMO_MAX_SEC;
  while (ok && !memoStopReq && total < maxBytes) {
    size_t got = micI2S.readBytes((char*)raw, sizeof(raw));
    int frames = (int)(got / (2 * sizeof(int32_t)));
    if (frames <= 0) { vTaskDelay(2); continue; }
    int32_t pk = 0;
    for (int i = 0; i < frames; i++) {
      int32_t v = raw[i * 2] >> MEMO_SAMPLE_SHIFT;
      if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
      int32_t a = v < 0 ? -v : v;
      if (a > pk) pk = a;
      chunk[n++] = (int16_t)v;
      if (n >= CH_SAMPLES) {
        if (f.write((uint8_t*)chunk, (size_t)n * 2) != (size_t)n * 2) { ok = false; break; }
        total += (uint32_t)n * 2; n = 0;
      }
    }
    float lv = (float)pk / 32768.0f;
    memoLevel = lv > memoLevel ? lv : memoLevel * 0.85f;
    memoRecBytes = total + (uint32_t)n * 2;
    yield();
  }
  if (ok && n > 0) {
    if (f.write((uint8_t*)chunk, (size_t)n * 2) == (size_t)n * 2) total += (uint32_t)n * 2; else ok = false;
  }
  if (f) {
    micWriteWavHeader(hdr, total, MEMO_RATE);        // tambal panjang data di header
    f.seek(0);
    f.write(hdr, WAV_HEADER_SIZE);
    f.close();
  }
  micReleaseI2S();
  if (chunk) heap_caps_free(chunk);
  if (total < (uint32_t)(MEMO_RATE / 5 * 2)) SD_MMC.remove(memoCurPath);   // < 0,2 detik: buang
  if (!ok) { strncpy(memoFailMsg, "Gagal menulis ke SD (penuh?)", sizeof(memoFailMsg) - 1); memoFailMsg[sizeof(memoFailMsg) - 1] = 0; memoRecFail = true; }
  memoRecBytes = total;
  memoRec = false;
  memoListDirty = true;
  needRedrawNow();
  micTaskHandle = NULL;
  vTaskDelete(NULL);
}

static void memoRecStart() {
  if (memoRec) return;
  if (micRecording || micTranscribing || micTaskHandle != NULL) { showToast("Mic sedang dipakai fitur lain"); vibWarn(); return; }
  if (memoPlayActive()) memoStopPlay();
  SD_MMC.mkdir(MEMO_DIR);
  struct tm t;
  if (ntpSynced && getLocalTime(&t, 5)) {
    snprintf(memoCurPath, sizeof(memoCurPath), MEMO_DIR "/m_%04d%02d%02d_%02d%02d%02d.wav",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(memoCurPath, sizeof(memoCurPath), MEMO_DIR "/m_u%08lu.wav", (unsigned long)millis());
  }
  wakeYieldMic(2500);                                // lepas mic dari task wake word dulu
  memoStopReq = false; memoRecBytes = 0; memoLevel = 0; memoRecFail = false;
  memoRec = true;
  BaseType_t res = createTaskPsramStack(memoRecTask, "memoRec", 12288, NULL, &memoTaskStack, &memoTaskTCB, &micTaskHandle, 1);
  if (res != pdPASS) {
    memoRec = false; micTaskHandle = NULL;
    showToast("Gagal mulai merekam"); vibError(); return;
  }
  vibTap(); needRedraw = true;
}

static void memoRecStop() {
  if (memoRec) { memoStopReq = true; vibTap(); needRedraw = true; }
}

void memoEnter() {
  memoPrepareBt();                                   // nyalakan link ke CAM supaya siap pas ketuk Putar
  memoLayout();
  memoSel = -1; memoScroll = 0; memoDelArmUntil = 0;
  memoScan();
}

void memoExit() {
  if (memoRec) memoStopReq = true;                   // jangan merekam diam2 di belakang layar
}

static bool memoSelPath(char* out, int cap) {
  if (memoSel < 0 || memoSel >= memoCount) return false;
  snprintf(out, cap, MEMO_DIR "/%s", memoNames[memoSel]);
  return true;
}

void drawMemo(LGFX_Sprite& s) {
  memoLayout();
  iosBackdrop(s); drawStatusBar(s);
  iosTitle(s, 8, 26, "Voice Memo");

  bool playing = memoPlayActive();
  bool busyRec = memoRec;
  s.setTextSize(1);

  // ---- tombol rekam bulat ----
  uint16_t red = 0xF9A6;
  if (busyRec) {
    float pulse = 0.5f + 0.5f * sinf(millis() / 220.0f);
    int pr = mlRecR + 3 + (int)(4.0f * pulse);
    s.drawCircle(mlRecCx, mlRecCy, pr, blend565(T().bg, red, 140));
    s.drawCircle(mlRecCx, mlRecCy, pr + 1, blend565(T().bg, red, 70));
  }
  drawGlassCircle(s, mlRecCx, mlRecCy, mlRecR + 4, T().surface, 150);
  s.fillCircle(mlRecCx, mlRecCy, mlRecR - 4, busyRec ? T().danger : red);
  if (busyRec) s.fillRoundRect(mlRecCx - 9, mlRecCy - 9, 18, 18, 4, 0xFFFF);   // ikon stop
  else         s.fillCircle(mlRecCx, mlRecCy, 8, 0xFFFF);                      // ikon rekam

  // ---- waktu + level ----
  char tb[24];
  if (busyRec) {
    memoFmtMs(tb, sizeof(tb), (uint32_t)(((uint64_t)memoRecBytes * 1000ULL) / (uint64_t)(MEMO_RATE * 2)));
    s.setTextColor(T().danger);
  } else if (playing) {
    char a[10], b[10];
    uint32_t dur = 0;
    if (memoSel >= 0 && memoSel < memoCount) dur = memoDurMsOfSize(memoSizes[memoSel]);
    memoFmtMs(a, sizeof(a), memoPlayPosMs()); memoFmtMs(b, sizeof(b), dur);
    snprintf(tb, sizeof(tb), "%s / %s", a, b);
    s.setTextColor(T().accent);
  } else {
    snprintf(tb, sizeof(tb), "Ketuk untuk merekam");
    s.setTextColor(T().subtext);
  }
  s.setCursor(mlRecCx - s.textWidth(tb) / 2, mlTimeY); s.print(tb);

  s.fillRoundRect(mlLvlX, mlLvlY, mlLvlW, 6, 3, blend565(T().surface, 0xFFFF, 45));
  if (busyRec) {
    int lw = (int)(mlLvlW * (memoLevel > 1.0f ? 1.0f : memoLevel));
    if (lw > 0) s.fillRoundRect(mlLvlX, mlLvlY, max(lw, 6), 6, 3, memoLevel > 0.85f ? T().danger : T().good);
  } else if (playing) {
    uint32_t dur = (memoSel >= 0 && memoSel < memoCount) ? memoDurMsOfSize(memoSizes[memoSel]) : 0;
    int pw = dur ? (int)((uint64_t)mlLvlW * memoPlayPosMs() / dur) : 0;
    if (pw > mlLvlW) pw = mlLvlW;
    if (pw > 0) s.fillRoundRect(mlLvlX, mlLvlY, max(pw, 6), 6, 3, T().accent);
  }

  // ---- daftar rekaman ----
  if (memoCount == 0) {
    s.setTextColor(T().subtext);
    const char* e1 = "Belum ada rekaman";
    s.setCursor(mlListX + mlListW / 2 - s.textWidth(e1) / 2, mlListY + 20); s.print(e1);
  }
  for (int r = 0; r < mlRows; r++) {
    int idx = memoScroll + r;
    if (idx >= memoCount) break;
    int ry = mlListY + r * mlRowH;
    bool sel = (idx == memoSel);
    drawGlassPanel(s, mlListX, ry, mlListW, mlRowH - 2, 8, sel ? T().accent : T().surface2, sel ? 170 : 100);
    char nm[24]; memoPretty(nm, sizeof(nm), memoNames[idx]);
    char du[10]; memoFmtMs(du, sizeof(du), memoDurMsOfSize(memoSizes[idx]));
    s.setTextColor(sel ? T().bg : T().text);
    s.setCursor(mlListX + 10, ry + 6); s.print(nm);
    s.setTextColor(sel ? T().bg : T().subtext);
    s.setCursor(mlListX + mlListW - 10 - s.textWidth(du), ry + 6); s.print(du);
    if (playing && sel) {                                      // penanda sedang diputar
      int tx = mlListX + mlListW - 14 - s.textWidth(du) - 12;
      s.fillTriangle(tx, ry + 5, tx, ry + 13, tx + 7, ry + 9, sel ? T().bg : T().accent);
    }
  }
  if (memoCount > mlRows) {                                    // indikator scroll tipis
    int trackH = mlRows * mlRowH - 2;
    int th = trackH * mlRows / memoCount; if (th < 8) th = 8;
    int ty = mlListY + (trackH - th) * memoScroll / (memoCount - mlRows);
    s.fillRoundRect(mlListX + mlListW - 3, ty, 2, th, 1, T().divider);
  }

  // ---- tombol bawah ----
  bool haveSel = (memoSel >= 0 && memoSel < memoCount) && !busyRec;
  bool armed = millis() < memoDelArmUntil;
  const char* l1 = playing ? "Henti" : "Putar";
  drawGlassPanel(s, mlBtn1X, mlBtnY, mlBtnW, mlBtnH, 8, playing ? T().danger : T().accent, haveSel || playing ? 210 : 90);
  s.setTextColor(haveSel || playing ? T().bg : T().subtext);
  s.setCursor(mlBtn1X + mlBtnW / 2 - s.textWidth(l1) / 2, mlBtnY + 8); s.print(l1);
  const char* l2 = armed ? "Yakin?" : "Hapus";
  drawGlassPanel(s, mlBtn2X, mlBtnY, mlBtnW, mlBtnH, 8, armed ? T().danger : T().surface2, haveSel ? 200 : 90);
  s.setTextColor(armed ? T().bg : (haveSel ? T().danger : T().subtext));
  s.setCursor(mlBtn2X + mlBtnW / 2 - s.textWidth(l2) / 2, mlBtnY + 8); s.print(l2);

  drawBack(s); drawToast(s);
}

void memoTouch(int x, int y, bool held, bool isNew) {
  memoLayout();
  // ---- geser daftar (drag vertikal) ----
  if (!isNew && held) {
    if (fxHit(x, y, mlListX, mlListY, mlListW, mlRows * mlRowH)) {
      memoDragAcc += memoDragLastY - y; memoDragLastY = y;
      int maxScroll = memoCount - mlRows; if (maxScroll < 0) maxScroll = 0;
      while (memoDragAcc >= mlRowH)  { if (memoScroll < maxScroll) memoScroll++; memoDragAcc -= mlRowH; needRedraw = true; }
      while (memoDragAcc <= -mlRowH) { if (memoScroll > 0) memoScroll--;         memoDragAcc += mlRowH; needRedraw = true; }
    }
    return;
  }
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }

  // ---- tombol rekam ----
  int dx = x - mlRecCx, dy = y - mlRecCy, rr = mlRecR + 6;
  if (dx * dx + dy * dy <= rr * rr) {
    if (memoRec) memoRecStop(); else memoRecStart();
    return;
  }

  // ---- baris daftar ----
  if (fxHit(x, y, mlListX, mlListY, mlListW, mlRows * mlRowH)) {
    memoDragLastY = y; memoDragAcc = 0;
    int idx = memoScroll + (y - mlListY) / mlRowH;
    if (idx >= 0 && idx < memoCount && !memoRec) {
      if (memoSel != idx) { if (memoPlayActive()) memoStopPlay(); memoSel = idx; vibTap(); needRedraw = true; }
    }
    return;
  }

  // ---- Putar / Henti ----
  if (fxHit(x, y, mlBtn1X, mlBtnY, mlBtnW, mlBtnH)) {
    if (memoPlayActive()) { memoStopPlay(); vibTap(); needRedraw = true; return; }
    char p[48];
    if (memoRec || !memoSelPath(p, sizeof(p))) { vibWarn(); showToast(memoRec ? "Selesaikan rekaman dulu" : "Pilih rekaman dulu"); return; }
    if (!memoBtReady()) { showToast("TWS belum terhubung (sambungkan di app Musik)", 2000); vibWarn(); return; }
    strncpy(memoPlayPath, p, sizeof(memoPlayPath) - 1); memoPlayPath[sizeof(memoPlayPath) - 1] = 0;
    memoPlayFile(memoPlayPath);
    vibTap(); needRedraw = true; return;
  }

  // ---- Hapus (ketuk 2x) ----
  if (fxHit(x, y, mlBtn2X, mlBtnY, mlBtnW, mlBtnH)) {
    char p[48];
    if (memoRec || !memoSelPath(p, sizeof(p))) { vibWarn(); return; }
    if (millis() < memoDelArmUntil) {
      if (memoPlayActive()) memoStopPlay();
      SD_MMC.remove(p);
      memoDelArmUntil = 0; memoSel = -1; memoScan();
      showToast("Rekaman dihapus"); vibSuccess(); needRedraw = true;
    } else {
      memoDelArmUntil = millis() + 2500; showToast("Ketuk lagi untuk menghapus"); vibTap(); needRedraw = true;
    }
    return;
  }
}

static void memoPoll() {
  if (memoRecFail) { memoRecFail = false; showToast(memoFailMsg, 2200); vibError(); }
  if (memoListDirty && curScreen() == SCR_MEMO) { memoScan(); needRedraw = true; }
  if (curScreen() == SCR_MEMO && (memoRec || memoPlayActive())) {
    unsigned long now = millis();
    if (now - memoLastRedraw >= 80) { memoLastRedraw = now; needRedraw = true; }
  }
  if (memoDelArmUntil && millis() >= memoDelArmUntil) { memoDelArmUntil = 0; if (curScreen() == SCR_MEMO) needRedraw = true; }
}

// =====================================================================
// 4. WIDGET CUACA (Open-Meteo) -- kartu di Home
// =====================================================================
// Lokasi tetap (koordinat yg diminta user). Refresh otomatis tiap 30 menit
// HANYA selagi Home terbuka (biar TLS tidak rebutan RAM dgn app berat), gagal
// -> coba lagi tiap 2 menit. Ketuk kartu = refresh sekarang.
#define WX_LAT_STR "-0.2254951"
#define WX_LON_STR "100.6161293"
#define WX_REFRESH_MS (30UL * 60UL * 1000UL)
#define WX_RETRY_MS   (2UL * 60UL * 1000UL)

static bool  wxValid = false;
static float wxTemp = 0, wxFeels = 0, wxMax = 0, wxMin = 0, wxWind = 0;
static int   wxHum = 0, wxCode = 0, wxRain = 0, wxIsDay = 1;
static unsigned long wxLastOkMs = 0, wxLastTryMs = 0;
static volatile bool wxFetching = false, wxFailed = false, wxDirty = false;
static TaskHandle_t wxTaskHandle = NULL;
static StackType_t* wxTaskStack = nullptr;
static StaticTask_t wxTaskTCB;

static bool wxNum(const String& js, int from, const char* key, bool arr, float* out) {
  String k = String("\"") + key + "\":";
  if (arr) k += "[";
  int p = js.indexOf(k, from);
  if (p < 0) return false;
  p += k.length();
  int e = p, L = (int)js.length();
  while (e < L) { char c = js.charAt(e); if (c == ',' || c == '}' || c == ']') break; e++; }
  if (e <= p) return false;
  *out = js.substring(p, e).toFloat();
  return true;
}

static bool wxParse(const String& body) {
  int cur = body.indexOf("\"current\":{");     // blok "current_units" sengaja dilewati
  int day = body.indexOf("\"daily\":{");
  if (cur < 0 || day < 0) return false;
  float v;
  if (!wxNum(body, cur, "temperature_2m", false, &v)) return false;
  wxTemp = v;
  if (wxNum(body, cur, "apparent_temperature", false, &v)) wxFeels = v;
  if (wxNum(body, cur, "relative_humidity_2m", false, &v)) wxHum = (int)(v + 0.5f);
  if (wxNum(body, cur, "weather_code", false, &v)) wxCode = (int)(v + 0.5f);
  if (wxNum(body, cur, "wind_speed_10m", false, &v)) wxWind = v;
  if (wxNum(body, cur, "is_day", false, &v)) wxIsDay = (int)(v + 0.5f);
  if (wxNum(body, day, "temperature_2m_max", true, &v)) wxMax = v;
  if (wxNum(body, day, "temperature_2m_min", true, &v)) wxMin = v;
  if (wxNum(body, day, "precipitation_probability_max", true, &v)) wxRain = (int)(v + 0.5f);
  return true;
}

static void wxTaskFunc(void* parameter) {
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    nasaWaitForHeap();                                // kasih RAM internal waktu lega sebelum TLS
    WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
    HTTPClient http; http.setTimeout(12000); http.setConnectTimeout(12000);
    String url = String("https://api.open-meteo.com/v1/forecast?latitude=" WX_LAT_STR "&longitude=" WX_LON_STR
                        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
                        "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                        "&timezone=Asia%2FJakarta&forecast_days=1");
    if (http.begin(client, url)) {
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        String body = http.getString();
        ok = wxParse(body);
      }
      http.end();
    }
  }
  if (ok) { wxValid = true; wxLastOkMs = millis(); }
  wxFailed = !ok;
  wxFetching = false;
  wxDirty = true;
  wxTaskHandle = NULL;
  vTaskDelete(NULL);
}

static void wxStartFetch() {
  if (wxTaskHandle != NULL || wxFetching) return;
  if (!nasaHeapReadyForTask()) { wxLastTryMs = millis(); wxFailed = true; return; }
  wxLastTryMs = millis();
  wxFetching = true; wxFailed = false;
  BaseType_t res = createTaskPsramStack(wxTaskFunc, "wxTask", 12288, NULL, &wxTaskStack, &wxTaskTCB, &wxTaskHandle, 1);
  if (res != pdPASS) { wxFetching = false; wxFailed = true; wxTaskHandle = NULL; }
  needRedraw = true;
}

void weatherRefreshNow() {
  if (!wifiConnected) { showToast("WiFi belum terhubung"); vibWarn(); return; }
  if (wxFetching) { showToast("Memuat cuaca..."); return; }
  wxStartFetch();
  showToast("Memperbarui cuaca...");
  vibTap();
}

static void weatherPoll() {
  if (wxDirty) { wxDirty = false; needRedraw = true; }
  if (!wifiConnected || wxFetching || wxTaskHandle != NULL) return;
  if (locked || gameModeActive || curScreen() != SCR_HOME) return;
  unsigned long now = millis();
  if (now < 15000UL) return;                          // biarkan boot & WiFi tenang dulu
  bool stale = !wxValid || (now - wxLastOkMs) > WX_REFRESH_MS;
  bool retryOk = (wxLastTryMs == 0) || (now - wxLastTryMs) > (wxFailed ? WX_RETRY_MS : 10000UL);
  if (stale && retryOk) wxStartFetch();
}

static const char* wxDesc(int c) {
  if (c == 0) return "Cerah";
  if (c == 1) return "Cerah berawan";
  if (c == 2) return "Berawan";
  if (c == 3) return "Mendung";
  if (c == 45 || c == 48) return "Berkabut";
  if (c >= 51 && c <= 57) return "Gerimis";
  if (c >= 61 && c <= 67) return "Hujan";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Salju";
  if (c >= 80 && c <= 82) return "Hujan lebat";
  if (c == 95) return "Badai petir";
  if (c == 96 || c == 99) return "Badai es";
  return "Cuaca";
}

static void wxCloud(LGFX_Sprite& s, int cx, int cy, int r, uint16_t col) {
  s.fillCircle(cx - r / 2, cy + r / 6, r / 2, col);
  s.fillCircle(cx + r / 3, cy + r / 6, (r * 3) / 5, col);
  s.fillCircle(cx - r / 8, cy - r / 4, (r * 3) / 5, col);
  s.fillRect(cx - r / 2, cy + r / 6, r + r / 3, r / 2, col);
}

static void wxIcon(LGFX_Sprite& s, int cx, int cy, int r, int code, bool day) {
  const uint16_t SUN = 0xFEA0, MOON = 0xDEFB, CLOUD = 0xC618, DARK = 0x8410, DROP = 0x5D7F;
  bool clear = (code == 0 || code == 1);
  bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool storm = (code >= 95);
  if (clear) {
    if (day) {
      s.fillCircle(cx, cy, r / 2 + 1, SUN);
      for (int i = 0; i < 8; i++) {
        float a = i * 0.785398f;
        s.drawLine(cx + (int)(cosf(a) * (r * 0.75f)), cy + (int)(sinf(a) * (r * 0.75f)),
                   cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), SUN);
      }
    } else {
      s.fillCircle(cx, cy, r * 3 / 4, MOON);
      s.fillCircle(cx + r / 3, cy - r / 4, r * 3 / 4, T().surface);   // potongan bulan sabit
    }
    if (code == 1) wxCloud(s, cx + r / 3, cy + r / 3, r * 2 / 3, CLOUD);
    return;
  }
  wxCloud(s, cx, cy - (rain || snow || storm ? r / 5 : 0), r, (rain || storm) ? DARK : CLOUD);
  int by = cy + r / 2 + 1;
  if (rain) {
    for (int i = -1; i <= 1; i++) s.drawLine(cx + i * (r / 2), by, cx + i * (r / 2) - 2, by + r / 2, DROP);
  } else if (snow) {
    for (int i = -1; i <= 1; i++) s.fillCircle(cx + i * (r / 2), by + r / 4, 1, 0xFFFF);
  } else if (storm) {
    s.fillTriangle(cx + 1, by - 1, cx - 4, by + r / 2, cx, by + r / 2, SUN);
    s.fillTriangle(cx, by + r / 2, cx + 5, by + 1, cx + 1, by + 1, SUN);
  }
}

void drawWeatherWidget(LGFX_Sprite& s, int x, int y, int w, int h) {
  drawGlassCard(s, x, y, w, h, 18, T().surface, 150);
  int cy = y + h / 2;
  s.setTextSize(1);
  if (!wxValid) {
    const char* msg = wxFetching ? "Memuat cuaca..." : (!wifiConnected ? "Cuaca: WiFi belum terhubung" : (wxFailed ? "Gagal memuat cuaca - ketuk untuk coba lagi" : "Ketuk untuk memuat cuaca"));
    s.setTextColor(T().subtext);
    s.setCursor(x + 16, cy - 4); s.print(msg);
    return;
  }
  int r = (h >= 46) ? 13 : 11;
  wxIcon(s, x + 14 + r, cy, r, wxCode, wxIsDay != 0);

  char tb[8]; snprintf(tb, sizeof(tb), "%d", (int)lroundf(wxTemp));
  int tx = x + 14 + r * 2 + 12;
  s.setTextSize(2); s.setTextColor(T().text);
  s.setCursor(tx, cy - 8); s.print(tb);
  int tw = s.textWidth(tb);
  s.setTextSize(1);
  s.drawCircle(tx + tw + 4, cy - 6, 2, T().text);          // tanda derajat (font ASCII tidak punya)
  s.setCursor(tx + tw + 9, cy - 8); s.print("C");

  int dx = tx + tw + 9 + 6 + 14;
  s.setTextColor(T().text);
  s.setCursor(dx, cy - 11); s.print(wxDesc(wxCode));
  char dl[48];
  snprintf(dl, sizeof(dl), "%d/%d C  Hujan %d%%  RH %d%%", (int)lroundf(wxMax), (int)lroundf(wxMin), wxRain, wxHum);
  s.setTextColor(T().subtext);
  s.setCursor(dx, cy + 2);
  if (dx + s.textWidth(dl) > x + w - 8) {                 // layar sempit (portrait): ringkas
    snprintf(dl, sizeof(dl), "%d/%d C  Hujan %d%%", (int)lroundf(wxMax), (int)lroundf(wxMin), wxRain);
  }
  s.print(dl);
}

// =====================================================================
// HOOK dari loop() -- murah kalau tidak ada kerjaan
// =====================================================================
void featuresLoopPoll() {
  swTick();
  screenshotPoll();
  memoPoll();
  weatherPoll();
}
