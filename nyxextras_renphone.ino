// =====================================================================
//  nyxextras_renphone.ino  --  5 fitur tambahan NyxOS
//
//    1. LOG CRASH          alasan reset + layar terakhir + RAM + backtrace -> /crashlog.txt
//                          (ditampilkan di About Phone)
//    2. ENERGI PER APP     mAh yg keluar dari baterai dicatat per app (tab "Per App"
//                          di app Baterai); reset otomatis saat baterai penuh
//    3. PERINTAH SUARA     AI Chat (Gemini) bisa menjalankan aksi: buka app, timer,
//                          musik, kecerahan, Home, kembali. Gemini menulis baris
//                          "@@CMD {json}" di akhir jawabannya.
//    4. THUMBNAIL SS       setelah tangkap layar, thumbnail kecil muncul di pojok
//                          kiri-bawah. Ketuk = buka di Galeri, geser kiri = tutup.
//    5. SKELETON LOADING   blok abu-abu berkilau pengganti spinner di APOD, NEO,
//                          EPIC, dan Galeri NASA.
//
//  Berkas ini digabung Arduino SETELAH phone.ino dan SEBELUM ota_check/phone_notif/
//  wakeword (urutan alfabet), jadi static di about/features/ina219/musicbt
//  bisa dipakai langsung di sini. Fungsi non-static yang dipanggil dari berkas
//  lain diprototipe-kan di phone.ino.
//
//  Hook (lihat apply_nyx_extras_patch.py):
//    phone.ino         loop(): nyxExtrasLoopPoll(), hook sentuh banner/thumbnail,
//                      renderCurrentFrame(): gambar banner & thumbnail,
//                      sendGeminiRequest(): instruksi + ekstrak @@CMD,
//                      drawApodLoading/drawNasaLoading: skeleton
//    ina219_renphone   inaPoll(): nyxAppEnergyAdd(); tab ke-5 "Per App"
//    about_renphone    nyxAboutRows()
//    features_renphone screenshotSaveNow(): nyxShotThumbCapture()
//    call_renphone     banner panggilan masuk (Dynamic Island style)
// =====================================================================

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#if __has_include("esp_core_dump.h")
#include "esp_core_dump.h"
#endif
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && __has_include("esp_core_dump.h")
#define NYX_HAVE_COREDUMP 1
#endif

// =====================================================================
//  1. LOG CRASH
// =====================================================================
struct NyxCrumb {
  uint32_t magic;
  uint16_t scr;
  uint16_t flags;      // bit0 = terkunci
  uint32_t freeInt;
  uint32_t minInt;
  uint32_t largest;
  uint32_t upSec;
};
#define NYX_CRUMB_MAGIC 0x4E595831u
static RTC_NOINIT_ATTR NyxCrumb nyxCrumb;

static bool     nyxBootDone = false;
static bool     nyxRstAbnormal = false;
static bool     nyxRstHaveCrumb = false;
static char     nyxRstText[30] = "";
static char     nyxRstScr[22] = "";
static char     nyxRstTask[18] = "";
static char     nyxRstPc[16] = "";
static char     nyxRstBt[160] = "";
static uint32_t nyxRstFree = 0, nyxRstMin = 0, nyxRstLargest = 0, nyxRstUp = 0;
static bool     nyxLogPending = false;
static bool     nyxFlushDone = false;
static int      nyxLogCount = -1;     // -1 = belum diketahui

static const char* nyxScrName(uint16_t sc) {
  if (sc == (uint16_t)SCR_HOME) return "Home";
  for (int i = 0; i < APP_COUNT; i++) if ((uint16_t)apps[i].screen == sc) return apps[i].name;
  return "?";
}

static void nyxCrumbBeat() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 1000UL) return;
  last = now;
  nyxCrumb.magic   = NYX_CRUMB_MAGIC;
  nyxCrumb.scr     = (uint16_t)curScreen();
  nyxCrumb.flags   = locked ? 1 : 0;
  nyxCrumb.freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  nyxCrumb.minInt  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  nyxCrumb.largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  nyxCrumb.upSec   = now / 1000UL;
}

static void nyxCrashBoot() {
  nyxBootDone = true;
  esp_reset_reason_t rr = esp_reset_reason();
  switch (rr) {
    case ESP_RST_POWERON:   strcpy(nyxRstText, "Nyala (power on)"); break;
    case ESP_RST_SW:        strcpy(nyxRstText, "Restart software"); break;
    case ESP_RST_EXT:       strcpy(nyxRstText, "Tombol reset"); break;
    case ESP_RST_DEEPSLEEP: strcpy(nyxRstText, "Bangun deep sleep"); break;
    case ESP_RST_PANIC:     strcpy(nyxRstText, "CRASH (panic)"); nyxRstAbnormal = true; break;
    case ESP_RST_INT_WDT:   strcpy(nyxRstText, "Watchdog interupsi"); nyxRstAbnormal = true; break;
    case ESP_RST_TASK_WDT:  strcpy(nyxRstText, "Watchdog task"); nyxRstAbnormal = true; break;
    case ESP_RST_WDT:       strcpy(nyxRstText, "Watchdog lain"); nyxRstAbnormal = true; break;
    case ESP_RST_BROWNOUT:  strcpy(nyxRstText, "Brownout (tegangan drop)"); nyxRstAbnormal = true; break;
    default: snprintf(nyxRstText, sizeof(nyxRstText), "Lainnya (%d)", (int)rr); break;
  }

  nyxRstHaveCrumb = (nyxCrumb.magic == NYX_CRUMB_MAGIC) && rr != ESP_RST_POWERON;
  if (nyxRstHaveCrumb) {
    strncpy(nyxRstScr, nyxScrName(nyxCrumb.scr), sizeof(nyxRstScr) - 1);
    nyxRstScr[sizeof(nyxRstScr) - 1] = 0;
    if (nyxCrumb.flags & 1) { strncpy(nyxRstScr, "Terkunci", sizeof(nyxRstScr) - 1); }
    nyxRstFree = nyxCrumb.freeInt; nyxRstMin = nyxCrumb.minInt;
    nyxRstLargest = nyxCrumb.largest; nyxRstUp = nyxCrumb.upSec;
  }

#ifdef NYX_HAVE_COREDUMP
  if (nyxRstAbnormal && esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t* sm = (esp_core_dump_summary_t*)heap_caps_calloc(1, sizeof(esp_core_dump_summary_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sm) sm = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
    if (sm && esp_core_dump_get_summary(sm) == ESP_OK) {
      strncpy(nyxRstTask, sm->exc_task, sizeof(nyxRstTask) - 1);
      nyxRstTask[sizeof(nyxRstTask) - 1] = 0;
      snprintf(nyxRstPc, sizeof(nyxRstPc), "0x%08x", (unsigned)sm->exc_pc);
      int n = (int)sm->exc_bt_info.depth;
      if (n > 12) n = 12;
      size_t o = 0;
      for (int i = 0; i < n && o + 12 < sizeof(nyxRstBt); i++)
        o += snprintf(nyxRstBt + o, sizeof(nyxRstBt) - o, "%s0x%08x", i ? "," : "", (unsigned)sm->exc_bt_info.bt[i]);
    }
    if (sm) free(sm);
    esp_core_dump_image_erase();
  }
#endif

  nyxLogPending = nyxRstAbnormal;
}

static void nyxCountLogLines() {
  nyxLogCount = 0;
  File f = SD_MMC.open("/crashlog.txt", FILE_READ);
  if (!f) return;
  uint8_t buf[256];
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) for (int i = 0; i < n; i++) if (buf[i] == '\n') nyxLogCount++;
  f.close();
}

static void nyxCrashFlush() {
  if (nyxFlushDone) return;
  if (!sdReady) { if (millis() > 120000UL) nyxFlushDone = true; return; }
  if (nyxLogPending) {
    if (!ntpSynced && millis() < 45000UL) return;      // tunggu jam kalau bisa, maks ±45 dtk
    File r = SD_MMC.open("/crashlog.txt", FILE_READ);
    size_t sz = r ? (size_t)r.size() : 0;
    if (r) r.close();
    if (sz > 32768) { SD_MMC.remove("/crashlog.old"); SD_MMC.rename("/crashlog.txt", "/crashlog.old"); }

    char ts[24] = "(jam belum sinkron)";
    struct tm t;
    if (ntpSynced && getLocalTime(&t, 5))
      snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

    char line[420];
    int o = snprintf(line, sizeof(line), "[%s] %s", ts, nyxRstText);
    if (nyxRstHaveCrumb)
      o += snprintf(line + o, sizeof(line) - o, " | layar=%s | RAM %u/min %u/blok %u | up %us",
                    nyxRstScr, (unsigned)nyxRstFree, (unsigned)nyxRstMin, (unsigned)nyxRstLargest, (unsigned)nyxRstUp);
    if (nyxRstPc[0]) o += snprintf(line + o, sizeof(line) - o, " | task=%s pc=%s", nyxRstTask, nyxRstPc);
    if (nyxRstBt[0]) o += snprintf(line + o, sizeof(line) - o, " | bt=%s", nyxRstBt);
    snprintf(line + o, sizeof(line) - o, "\n");

    File f = SD_MMC.open("/crashlog.txt", FILE_APPEND);
    if (f) { f.print(line); f.close(); }
    nyxLogPending = false;
  }
  nyxCountLogLines();
  nyxFlushDone = true;
}

// dipanggil dari aboutBuild() -- tambah baris ke daftar About
void nyxAboutRows() {
  char a[48];
  aboutAdd("Reset terakhir", nyxRstText);
  if (nyxRstHaveCrumb && nyxRstAbnormal) {
    snprintf(a, sizeof(a), "%s", nyxRstScr);
    aboutAdd("Layar saat itu", a);
    snprintf(a, sizeof(a), "%uK bebas (min %uK)", (unsigned)(nyxRstFree / 1024), (unsigned)(nyxRstMin / 1024));
    aboutAdd("RAM saat itu", a);
  }
  if (nyxRstPc[0]) {
    snprintf(a, sizeof(a), "%s pc=%s", nyxRstTask, nyxRstPc);
    aboutAdd("Lokasi crash", a);
  }
  if (nyxRstBt[0]) {
    char b[44]; strncpy(b, nyxRstBt, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
    aboutAdd("Backtrace", b);
  }
  if (nyxLogCount > 0) {
    snprintf(a, sizeof(a), "%d entri (/crashlog.txt)", nyxLogCount);
    aboutAdd("Log crash", a);
  }
}

// =====================================================================
//  2. ENERGI PER APP
// =====================================================================
#define NYX_EN_N (APP_COUNT + 2)       // + Home + Terkunci
static float nyxEn[NYX_EN_N];
static bool  nyxEnLoaded = false, nyxEnDirty = false;
static uint32_t nyxEnSaveMs = 0;
static uint32_t nyxEnArmUntil = 0;

static void nyxEnLoad() {
  if (nyxEnLoaded) return;
  nyxEnLoaded = true;
  memset(nyxEn, 0, sizeof(nyxEn));
  Preferences p; p.begin("nyxen", true);
  size_t n = p.getBytesLength("e");
  if (n == sizeof(nyxEn)) p.getBytes("e", nyxEn, sizeof(nyxEn));
  p.end();
}

static void nyxEnSave() {
  Preferences p; p.begin("nyxen", false);
  p.putBytes("e", nyxEn, sizeof(nyxEn));
  p.end();
  nyxEnDirty = false;
  nyxEnSaveMs = millis();
}

static void nyxEnReset() {
  memset(nyxEn, 0, sizeof(nyxEn));
  nyxEnSave();
}

static int nyxEnBucket() {
  if (locked) return APP_COUNT + 1;
  Screen sc = curScreen();
  if (sc == SCR_HOME) return APP_COUNT;
  int i = appIndexForScreen(sc);
  return (i >= 0 && i < APP_COUNT) ? i : APP_COUNT;
}

// dipanggil dari inaPoll() tiap ada arus keluar (dMah > 0)
void nyxAppEnergyAdd(float dMah) {
  if (dMah <= 0) return;
  nyxEnLoad();
  nyxEn[nyxEnBucket()] += dMah;
  nyxEnDirty = true;
}

static const char* nyxEnName(int i) {
  if (i == APP_COUNT) return "Home";
  if (i == APP_COUNT + 1) return "Terkunci";
  return apps[i].name;
}

void nyxDrawAppEnergy(LGFX_Sprite& s, int top, int bottom) {
  nyxEnLoad();
  s.setTextSize(1);
  float tot = 0, mx = 0;
  for (int i = 0; i < NYX_EN_N; i++) { tot += nyxEn[i]; if (nyxEn[i] > mx) mx = nyxEn[i]; }

  char v[40];
  s.setTextColor(T().subtext); s.setCursor(10, top + 2); s.print("Sejak terakhir penuh");
  snprintf(v, sizeof(v), "%.1f mAh", (double)tot);
  s.setTextColor(T().text); s.setCursor(SCR_W - 10 - (int)s.textWidth(v), top + 2); s.print(v);

  int btnH = 18, btnY = bottom - btnH - 2;
  int y = top + 16;
  int rows = (btnY - 6 - y) / 16;
  if (rows > 8) rows = 8;

  if (tot < 0.05f) {
    s.setTextColor(T().subtext); s.setTextWrap(true);
    s.setCursor(10, y + 6);
    s.print("Belum ada data. Pakai HP dengan baterai (tanpa charger) beberapa menit, lalu buka tab ini lagi.");
    s.setTextWrap(false);
  } else {
    bool used[NYX_EN_N];
    memset(used, 0, sizeof(used));
    int barX = 92, barW = SCR_W - barX - 62;
    if (barW < 30) barW = 30;
    for (int r = 0; r < rows; r++) {
      int bi = -1; float bv = 0.05f;
      for (int i = 0; i < NYX_EN_N; i++) if (!used[i] && nyxEn[i] > bv) { bv = nyxEn[i]; bi = i; }
      if (bi < 0) break;
      used[bi] = true;
      int yy = y + r * 16;
      char nm[14]; strncpy(nm, nyxEnName(bi), sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
      s.setTextColor(r == 0 ? (uint16_t)T().text : (uint16_t)T().subtext);
      s.setCursor(10, yy + 3); s.print(nm);
      iosRR(s, barX, yy + 2, barW, 9, 3, T().surface2);
      int fw = (mx > 0) ? (int)((float)barW * nyxEn[bi] / mx) : 0;
      if (fw < 3) fw = 3;
      iosRR(s, barX, yy + 2, fw, 9, 3, r == 0 ? (uint16_t)T().accent2 : (uint16_t)T().accent);
      snprintf(v, sizeof(v), "%.1f", (double)nyxEn[bi]);
      s.setTextColor(T().text); s.setCursor(SCR_W - 10 - (int)s.textWidth(v), yy + 3); s.print(v);
    }
  }

  bool armed = (millis() < nyxEnArmUntil);
  iosRR(s, 10, btnY, SCR_W - 20, btnH, 6, armed ? (uint16_t)T().danger : (uint16_t)T().surface2);
  const char* lb = armed ? "Ketuk lagi untuk reset" : "Reset statistik";
  s.setTextColor(armed ? (uint16_t)T().bg : (uint16_t)T().subtext);
  s.setCursor((SCR_W - (int)s.textWidth(lb)) / 2, btnY + 5); s.print(lb);
}

void nyxAppEnergyTouch(int x, int y, int top, int bottom) {
  (void)x; (void)top;
  int btnH = 18, btnY = bottom - btnH - 2;
  if (y < btnY || y > btnY + btnH) return;
  if (millis() < nyxEnArmUntil) {
    nyxEnReset();
    nyxEnArmUntil = 0;
    showToast("Statistik direset", 1200);
  } else {
    nyxEnArmUntil = millis() + 3000UL;
  }
  vibTap();
}

// =====================================================================
//  3. PERINTAH SUARA (Gemini -> aksi di HP)
// =====================================================================
#define NYX_CMD_Q   3
#define NYX_CMD_LEN 110
static char nyxCmdQ[NYX_CMD_Q][NYX_CMD_LEN];
static volatile uint8_t nyxCmdN = 0;
static volatile bool    nyxCmdReady = false;

// teks yang ditaruh di AWAL prompt. Dibuat sekali (daftar nama app diambil dari apps[]).
String nyxVoiceInstruction() {
  static String cache;
  if (cache.length()) return cache;
  cache.reserve(900);
  cache  = "ATURAN KONTROL HP: kamu terhubung langsung ke HP DIY ini dan boleh menjalankan aksi. ";
  cache += "Jika user MEMINTA AKSI di HP (buka app, timer, musik, kecerahan, kembali, ke Home), ";
  cache += "jawab SATU kalimat pendek, lalu tulis baris perintah terpisah (satu per baris, maksimal 3) ";
  cache += "dengan format persis: @@CMD {\"a\":\"open\",\"app\":\"Musik\"}\n";
  cache += "Aksi valid: open (app=nama app), timer (sec=detik 1-86400), ";
  cache += "music (op=play|pause|next|prev|shuffle), brightness (val=10-100), home, back. ";
  cache += "Contoh: {\"a\":\"timer\",\"sec\":600} atau {\"a\":\"music\",\"op\":\"shuffle\"} atau {\"a\":\"home\"}. ";
  cache += "Nama app yang ada: ";
  for (int i = 0; i < APP_COUNT; i++) {
    if (strcmp(apps[i].name, "Atur Home") == 0) continue;
    cache += apps[i].name;
    cache += ", ";
  }
  cache += "\nJangan tulis @@CMD untuk pertanyaan biasa. Jangan pakai markdown atau code block pada baris @@CMD. ";
  cache += "Kalau permintaan tidak bisa dilakukan dengan aksi di atas, jelaskan singkat tanpa @@CMD.\n\n";
  return cache;
}

// Dipanggil dari sendGeminiRequest() (task Gemini): cabut baris @@CMD dari jawaban,
// simpan ke antrean. Eksekusinya di loop utama (nyxExtrasLoopPoll) -- BUKAN di task ini.
bool nyxVoiceExtract(String& resp) {
  if (resp.indexOf("@@CMD") < 0) return false;
  String clean;
  clean.reserve(resp.length());
  uint8_t found = 0;
  int pos = 0, len = (int)resp.length();
  while (pos < len) {
    int nl = resp.indexOf('\n', pos);
    if (nl < 0) nl = len;
    String line = resp.substring(pos, nl);
    int ci = line.indexOf("@@CMD");
    if (ci >= 0) {
      String js = line.substring(ci + 5);
      js.replace("`", "");
      js.trim();
      if (found < NYX_CMD_Q && js.length() > 1 && js.length() < NYX_CMD_LEN && !nyxCmdReady) {
        strncpy(nyxCmdQ[found], js.c_str(), NYX_CMD_LEN - 1);
        nyxCmdQ[found][NYX_CMD_LEN - 1] = 0;
        found++;
      }
      String before = line.substring(0, ci);
      before.trim();
      if (before.length()) { clean += before; clean += "\n"; }
    } else {
      clean += line; clean += "\n";
    }
    pos = nl + 1;
  }
  clean.trim();
  if (clean.length() == 0) clean = "Oke.";
  resp = clean;
  if (found) { nyxCmdN = found; nyxCmdReady = true; }
  return found > 0;
}

static bool nyxJStr(const char* js, const char* key, char* out, int cap) {
  char pat[20]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(js, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ' ' || *p == ':') p++;
  if (*p != '"') return false;
  p++;
  int n = 0;
  while (*p && *p != '"' && n < cap - 1) out[n++] = *p++;
  out[n] = 0;
  return n > 0;
}

static bool nyxJInt(const char* js, const char* key, long& v) {
  char pat[20]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(js, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ' ' || *p == ':' || *p == '"') p++;
  if (*p < '0' || *p > '9') return false;
  v = strtol(p, nullptr, 10);
  return true;
}

static void nyxLower(char* s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

static int nyxFindApp(const char* want) {
  char w[28]; strncpy(w, want, sizeof(w) - 1); w[sizeof(w) - 1] = 0; nyxLower(w);
  int part = -1;
  for (int i = 0; i < APP_COUNT; i++) {
    if (strcmp(apps[i].name, "Atur Home") == 0) continue;
    char nm[28]; strncpy(nm, apps[i].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0; nyxLower(nm);
    if (strcmp(nm, w) == 0) return i;
    if (part < 0 && (strstr(nm, w) || strstr(w, nm))) part = i;
  }
  return part;
}

static void nyxTimerStart(long sec) {
  if (sec < 1) sec = 1;
  if (sec > 86400) sec = 86400;
  tmTotal = (unsigned long)sec * 1000UL;
  tmRemain = tmTotal;
  tmRunning = true;
  tmLastMs = millis();
  swStopRing();
  char b[40];
  if (sec >= 3600) snprintf(b, sizeof(b), "Timer %ld jam %ld mnt", sec / 3600, (sec % 3600) / 60);
  else if (sec >= 60) snprintf(b, sizeof(b), "Timer %ld mnt %ld dtk", sec / 60, sec % 60);
  else snprintf(b, sizeof(b), "Timer %ld dtk", sec);
  diNotify('t', String(b), T().accent, 2500, false, "", SCR_STOPWATCH, true);
  showToast(b, 1800);
}

static void nyxMusicOp(const char* op) {
  if (strcmp(op, "pause") == 0) {
    if (musStarted && musLoaded && !musPaused) musPost('u', 0, nullptr);
    showToast("Musik dijeda", 1200);
    return;
  }
  musBegin();
  if (musListCount() == 0) musPost('r', 0, nullptr);
  if (strcmp(op, "play") == 0) {
    if (!musLoaded || musPaused) musPost('u', 0, nullptr);
    showToast("Musik diputar", 1200);
  } else if (strcmp(op, "next") == 0) {
    musPost('n', 0, nullptr);
    showToast("Lagu berikutnya", 1200);
  } else if (strcmp(op, "prev") == 0) {
    musPost('b', 0, nullptr);
    showToast("Lagu sebelumnya", 1200);
  } else if (strcmp(op, "shuffle") == 0) {
    musShufMode = 1; musPrefsDirty = true; musPrefsMs = millis();
    musPost('n', 0, nullptr);
    showToast("Musik: acak", 1400);
  } else {
    showToast("Perintah musik tak dikenal", 1400);
  }
}

static void nyxVoiceRun(const char* js) {
  char a[16];
  if (!nyxJStr(js, "a", a, sizeof(a))) { showToast("Perintah AI tidak terbaca", 1500); return; }
  nyxLower(a);
  if (strcmp(a, "open") == 0) {
    char app[28];
    if (!nyxJStr(js, "app", app, sizeof(app))) { showToast("Nama app kosong", 1400); return; }
    int i = nyxFindApp(app);
    if (i < 0) { char b[44]; snprintf(b, sizeof(b), "App \"%s\" tidak ada", app); showToast(b, 1800); return; }
    if (curScreen() != apps[i].screen) navPush(apps[i].screen);
  } else if (strcmp(a, "timer") == 0) {
    long sec = 0;
    if (!nyxJInt(js, "sec", sec)) { showToast("Durasi timer kosong", 1400); return; }
    nyxTimerStart(sec);
  } else if (strcmp(a, "music") == 0) {
    char op[12];
    if (!nyxJStr(js, "op", op, sizeof(op))) { showToast("Perintah musik kosong", 1400); return; }
    nyxLower(op);
    nyxMusicOp(op);
  } else if (strcmp(a, "brightness") == 0) {
    long v = 0;
    if (!nyxJInt(js, "val", v)) { showToast("Nilai kecerahan kosong", 1400); return; }
    if (v < 10) v = 10;
    if (v > 100) v = 100;
    brightness = (int)(10 + (v - 10) * 245L / 90L);
    display.setBrightness((uint8_t)brightness);
    char b[32]; snprintf(b, sizeof(b), "Kecerahan %ld%%", v);
    showToast(b, 1200);
  } else if (strcmp(a, "home") == 0) {
    navGoHome();
  } else if (strcmp(a, "back") == 0) {
    navBack();
  } else {
    showToast("Aksi AI tidak dikenal", 1500);
  }
  vibSuccess();
}

static void nyxVoiceTick() {
  if (!nyxCmdReady) return;
  uint8_t n = nyxCmdN;
  nyxCmdReady = false;
  if (locked) return;                      // aksi hanya jalan saat HP terbuka
  if (n > NYX_CMD_Q) n = NYX_CMD_Q;
  for (uint8_t i = 0; i < n; i++) nyxVoiceRun(nyxCmdQ[i]);
  needRedraw = true;
}

// =====================================================================
//  4. THUMBNAIL SCREENSHOT
// =====================================================================
#define NYX_TH_MIN_W    88          // lebar thumbnail = ±30% lebar layar, dijepit 88..124 px
#define NYX_TH_MAX_W    124
#define NYX_TH_BORDER   3           // 2 px cincin terang + 1 px garis gelap di antara cincin & gambar
#define NYX_TH_RADIUS   11          // radius luar bingkai
#define NYX_TH_IN_MS    260
#define NYX_TH_HOLD_MS  3800
#define NYX_TH_OUT_MS   220
static uint16_t* nyxThumb = nullptr;
static int  nyxThW = 0, nyxThH = 0;
static bool nyxThOn = false;
static uint32_t nyxThT0 = 0, nyxThOutT0 = 0;
static char nyxThPath[64] = "";
static bool nyxThDown = false, nyxThDrag = false;
static int  nyxThX0 = 0;

static void nyxThFree() {
  if (nyxThumb) { free(nyxThumb); nyxThumb = nullptr; }
  nyxThOn = false; nyxThDown = false; nyxThDrag = false; nyxThOutT0 = 0;
}

// dipanggil dari screenshotSaveNow() setelah BMP tersimpan
void nyxShotThumbCapture(const char* path) {
  nyxThFree();
  int w = canvas.width(), h = canvas.height();
  if (w <= 0 || h <= 0) return;
  int tw = SCR_W * 30 / 100;
  if (tw < NYX_TH_MIN_W) tw = NYX_TH_MIN_W;
  if (tw > NYX_TH_MAX_W) tw = NYX_TH_MAX_W;
  int th = (tw * h) / w;
  if (th < 40) th = 40;
  if (th > 110) th = 110;
  nyxThumb = (uint16_t*)heap_caps_malloc((size_t)tw * th * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!nyxThumb) nyxThumb = (uint16_t*)malloc((size_t)tw * th * 2);
  if (!nyxThumb) return;
  for (int j = 0; j < th; j++) {
    int y0 = (j * h) / th, y1 = ((j + 1) * h) / th - 1; if (y1 < y0) y1 = y0;
    for (int i = 0; i < tw; i++) {
      int x0 = (i * w) / tw, x1 = ((i + 1) * w) / tw - 1; if (x1 < x0) x1 = x0;
      uint32_t r = 0, g = 0, b = 0; int cnt = 0;
      for (int sy = 0; sy < 3; sy++) for (int sx = 0; sx < 3; sx++) {            // rata-rata 3x3 titik -> lebih halus
        uint16_t c = (uint16_t)canvas.readPixel(x0 + ((x1 - x0) * sx) / 2, y0 + ((y1 - y0) * sy) / 2);
        r += (c >> 11) & 0x1F; g += (c >> 5) & 0x3F; b += c & 0x1F; cnt++;
      }
      r /= cnt; g /= cnt; b /= cnt;
      nyxThumb[j * tw + i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
  nyxThW = tw; nyxThH = th;
  strncpy(nyxThPath, path ? path : "", sizeof(nyxThPath) - 1);
  nyxThPath[sizeof(nyxThPath) - 1] = 0;
  nyxThOn = true; nyxThT0 = millis(); nyxThOutT0 = 0;
  vibSuccess();
}

void nyxShotThumbHide() { if (nyxThOn || nyxThumb) { nyxThFree(); needRedraw = true; } }
bool nyxShotThumbActive() { return nyxThOn && nyxThumb; }

static float nyxEaseOut(float k) { if (k < 0) k = 0; if (k > 1) k = 1; float q = 1.0f - k; return 1.0f - q * q * q; }

// posisi x thumbnail saat ini (animasi masuk / keluar)
static int nyxThCurX() {
  const int restX = 12;
  uint32_t now = millis();
  if (nyxThOutT0) {
    float k = nyxEaseOut((float)(now - nyxThOutT0) / (float)NYX_TH_OUT_MS);
    return (int)(restX - (nyxThW + restX + 12) * k);
  }
  float k = nyxEaseOut((float)(now - nyxThT0) / (float)NYX_TH_IN_MS);
  return (int)(-(nyxThW + 12) + (restX + nyxThW + 12) * k);
}
static int nyxThCurY() { return SCR_H - BACK_H - nyxThH - 18; }

// jarak bertanda (px) dari titik ke persegi membulat: <= 0 di dalam
static float nyxSdfRR(float px, float py, float cx, float cy, float hw, float hh, float r) {
  float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = (qx > qy ? qx : qy); if (inside > 0) inside = 0;
  return sqrtf(ox * ox + oy * oy) + inside - r;
}

void drawShotThumb(LGFX_Sprite& s) {
  if (!nyxShotThumbActive()) return;
  const int bw = NYX_TH_BORDER, ro = NYX_TH_RADIUS, ri = ro - bw;
  int x = nyxThCurX(), y = nyxThCurY();
  int fx = x - bw, fy = y - bw, fw = nyxThW + bw * 2, fh = nyxThH + bw * 2;
  float cx = fx + fw * 0.5f, cy = fy + fh * 0.5f, hw = fw * 0.5f, hh = fh * 0.5f;

  // 1) bayangan lembut: gelapkan piksel latar di sekitar bingkai (turun 4 px, memudar ±9 px)
  const int sp = 9, soy = 4;
  float scy = cy + soy;
  int y0 = fy - sp + soy, y1 = fy + fh + sp + soy, x0 = fx - sp, x1 = fx + fw + sp;
  if (y0 < 0) y0 = 0;
  if (y1 > SCR_H) y1 = SCR_H;
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  for (int py = y0; py < y1; py++) {
    for (int px = x0; px < x1; px++) {
      float fp = (float)px + 0.5f, fq = (float)py + 0.5f;
      if (nyxSdfRR(fp, fq, cx, cy, hw, hh, (float)ro) <= 0.0f) continue;      // bagian ini ditutup bingkai
      float d = nyxSdfRR(fp, fq, cx, scy, hw, hh, (float)ro);                 // jarak ke bayangan
      if (d >= (float)sp) continue;
      float k = d <= 0 ? 1.0f : 1.0f - d / (float)sp;
      float kk = k * k;
      uint16_t bgp = (uint16_t)s.readPixel(px, py);
      int lum = ((((bgp >> 11) & 31) << 3) * 77 + (((bgp >> 5) & 63) << 2) * 150 + ((bgp & 31) << 3) * 29) >> 8;   // 0..255
      float lt = (float)lum / 255.0f;                                          // 0 = gelap, 1 = terang
      uint8_t aDark = (uint8_t)(150.0f * kk * lt);                              // latar terang: bayangan gelap
      uint8_t aGlow = (uint8_t)(110.0f * kk * (1.0f - lt) * (1.0f - lt));      // latar gelap: cahaya halus warna aksen
      if (aDark < 3 && aGlow < 3) continue;
      uint16_t o = bgp;
      if (aDark >= 3) o = blend565(o, 0x0000, aDark);
      if (aGlow >= 3) o = blend565(o, T().accent, aGlow);
      s.drawPixel(px, py, o);
    }
  }

  // 2) bingkai berlapis: cincin terang 2 px + garis gelap 1 px (memisahkan gambar dari bingkai)
  const uint16_t hair = 0x1082;
  iosRR(s, fx, fy, fw, fh, ro, blend565(T().text, T().bg, 22));
  iosRR(s, fx + 2, fy + 2, fw - 4, fh - 4, ro - 2, hair);

  // 3) gambar dgn sudut membulat (anti-alias di tepi sudut, menyatu dgn bingkai)
  uint16_t frameCol = hair;
  for (int j = 0; j < nyxThH; j++) {
    for (int i = 0; i < nyxThW; i++) {
      uint16_t c = nyxThumb[j * nyxThW + i];
      float ddx = fmaxf(fmaxf((float)ri - (i + 0.5f), (i + 0.5f) - (float)(nyxThW - ri)), 0.0f);
      float ddy = fmaxf(fmaxf((float)ri - (j + 0.5f), (j + 0.5f) - (float)(nyxThH - ri)), 0.0f);
      if (ddx > 0.0f && ddy > 0.0f) {                                         // hanya area sudut
        float d = sqrtf(ddx * ddx + ddy * ddy) - (float)ri;
        if (d > 0.5f) continue;
        if (d > -0.5f) c = blend565(frameCol, c, (uint8_t)((0.5f - d) * 255.0f));
      }
      s.drawPixel(x + i, y + j, c);
    }
  }
}

// true = sentuhan ini milik thumbnail (jangan diteruskan ke app)
bool nyxShotThumbTouch(bool touched, bool newT, int tx, int ty) {
  if (!nyxShotThumbActive() || nyxThOutT0) { nyxThDown = false; return false; }
  int x = nyxThCurX(), y = nyxThCurY();
  bool inside = tx >= x - 6 && tx <= x + nyxThW + 6 && ty >= y - 6 && ty <= y + nyxThH + 6;
  if (!touched) {
    bool wasDown = nyxThDown, wasDrag = nyxThDrag;
    nyxThDown = false; nyxThDrag = false;
    if (wasDown && !wasDrag) {                       // ketukan: buka di Galeri
      char p[64]; strncpy(p, nyxThPath, sizeof(p) - 1); p[sizeof(p) - 1] = 0;
      nyxThFree();
      needRedraw = true;
      if (p[0]) galOpenFile(p);
      return true;
    }
    return wasDown;
  }
  if (newT) {
    if (!inside) return false;
    nyxThDown = true; nyxThDrag = false; nyxThX0 = tx;
    return true;
  }
  if (!nyxThDown) return false;
  if (!nyxThDrag && nyxThX0 - tx > 14) {             // geser kiri: tutup
    nyxThDrag = true;
    nyxThOutT0 = millis();
    needRedraw = true;
  }
  return true;
}

static void nyxThumbTick() {
  if (!nyxThumb) return;
  uint32_t now = millis();
  if (nyxThOutT0) {
    if (now - nyxThOutT0 >= NYX_TH_OUT_MS) { nyxThFree(); needRedraw = true; }
    else needRedraw = true;
    return;
  }
  if (now - nyxThT0 < NYX_TH_IN_MS + 40) needRedraw = true;
  else if (!nyxThDown && now - nyxThT0 > NYX_TH_IN_MS + NYX_TH_HOLD_MS) { nyxThOutT0 = now; needRedraw = true; }
  if (locked) nyxThFree();
}

// =====================================================================
//  5. SKELETON LOADING
// =====================================================================
static uint16_t nyxSkelBase()  { return T().surface2; }
static uint16_t nyxSkelHi()    { return blend565(T().surface2, T().text, 46); }

// satu blok skeleton dgn pita kilau yg bergerak kiri -> kanan
static void nyxSkelBox(LGFX_Sprite& s, int x, int y, int w, int h, int r, float bandX) {
  uint16_t base = nyxSkelBase(), hi = nyxSkelHi();
  int rr = r;
  if (rr > w / 2) rr = w / 2;
  if (rr > h / 2) rr = h / 2;
  iosRR(s, x, y, w, h, rr, base);
  const int half = 46, step = 6;
  for (int sx = x; sx < x + w; sx += step) {
    int cw = (sx + step > x + w) ? (x + w - sx) : step;
    float mid = (float)sx + (float)cw * 0.5f;
    float d = fabsf(mid - bandX);
    if (d >= half) continue;
    int vi = 0;
    if (rr > 1) {                                   // potong kilau mengikuti lengkung sudut
      float dxl = (float)(x + rr) - mid, dxr = mid - (float)(x + w - rr);
      float dx = dxl > dxr ? dxl : dxr;
      if (dx > 0) {
        float q = (float)(rr * rr) - dx * dx;
        float hh = q > 0 ? sqrtf(q) : 0.0f;
        vi = (int)ceilf((float)rr - hh);
      }
    }
    if (h - vi * 2 <= 0) continue;
    uint8_t a = (uint8_t)(255.0f * (1.0f - d / (float)half));
    s.fillRect(sx, y + vi, cw, h - vi * 2, blend565(base, hi, a));
  }
}

// Skeleton meniru PERSIS tata letak layar aslinya (posisi/ukuran diambil dari fungsi geometri
// yang sama dgn layar asli, jadi ikut berubah saat orientasi portrait/landscape):
//   kind 0 = APOD   1 = daftar NEO   2 = Bumi EPIC   3 = Galeri NASA
// Teks setinggi 8 px (font bawaan, size 1) -> blok skeleton h = 7 supaya terasa sama.
static void nyxSkelHeader(LGFX_Sprite& s, int w, float bandX) {
  nyxSkelBox(s, 8, 27, w, 7, 3, bandX);                       // judul di (8,26)
}

static void nyxSkelNavRow(LGFX_Sprite& s, int navY, float bandX) {   // tombol < n/N > + keterangan
  nyxSkelBox(s, 6, navY, 50, 20, 5, bandX);
  nyxSkelBox(s, SCR_W - 56, navY, 50, 20, 5, bandX);
  nyxSkelBox(s, SCR_W / 2 - 12, navY + 6, 24, 7, 3, bandX);
}

void nyxSkeleton(LGFX_Sprite& s, int kind) {
  const unsigned long ms = millis();
  float bandX = fmodf((float)ms, 1500.0f) / 1500.0f * (float)(SCR_W + 140) - 70.0f;
  static const uint8_t lw[8] = {100, 94, 100, 88, 98, 100, 91, 100};   // lebar baris teks (% dari lebar penuh)

  if (kind == 0) {                                                       // ---- APOD ----
    nyxSkelHeader(s, 104, bandX);
    int btnW = 28, btnH = 18, gap = 4;
    int xNext = SCR_W - 4 - btnW, xPrev = xNext - gap - btnW, xLang = xPrev - gap - 44;
    nyxSkelBox(s, xLang, 24, 44, btnH, 4, bandX);
    nyxSkelBox(s, xPrev, 24, btnW, btnH, 4, bandX);
    nyxSkelBox(s, xNext, 24, btnW, btnH, 4, bandX);
    nyxSkelBox(s, 0, apodImgTop(), SCR_W, apodImgAreaH(), 0, bandX); // kotak gambar
    int tTop = apodTextTop(), tBot = apodTextBottom(), tH = tBot - tTop;
    if (tH > 12) {
      iosRR(s, 4, tTop, SCR_W - 8, tH, 6, T().surface);              // kartu teks (statis, seperti aslinya)
      int innerW = SCR_W - 20, lineH = 10;
      int rows = (tH - 8) / lineH;
      for (int i = 0; i < rows; i++) {
        if (i == 1) continue;                                        // baris kosong di bawah judul
        int w = (i == 0) ? innerW * 48 / 100 : innerW * lw[i % 8] / 100;
        if (i == rows - 1 || i == rows - 2) w = innerW * 62 / 100;   // baris terakhir lebih pendek
        nyxSkelBox(s, 10, tTop + 4 + i * lineH + 1, w, 6, 3, bandX);
      }
    }
  } else if (kind == 1) {                                                // ---- daftar NEO ----
    nyxSkelHeader(s, 150, bandX);
    nyxSkelBox(s, SCR_W - 4 - 22, 24, 22, 22, 11, bandX);            // ikon kaca pembesar
    int top = neoListTop(), rowH = neoRowH();
    for (int r = 0; r < NEO_PER_PAGE; r++) {
      int ry = top + r * rowH;
      iosRR(s, 6, ry + 2, SCR_W - 12, rowH - 6, 6, T().surface);
      nyxSkelBox(s, 12, ry + 8, (SCR_W - 24) * (r == 1 ? 52 : (r == 2 ? 44 : 60)) / 100, 7, 3, bandX);
      nyxSkelBox(s, 12, ry + rowH - 16, 84 + r * 10, 7, 3, bandX);
    }
  } else if (kind == 2) {                                                // ---- Bumi EPIC ----
    nyxSkelHeader(s, 98, bandX);
    int imgTop = epicImgTop(), imgH = epicImgAreaH();
    nyxSkelBox(s, 0, imgTop, SCR_W, imgH, 0, bandX);
    int navY = imgTop + imgH + 6;
    nyxSkelNavRow(s, navY, bandX);
    nyxSkelBox(s, 8, navY + 27, 156, 7, 3, bandX);                   // "Diambil pukul .. UTC"
  } else {                                                               // ---- Galeri NASA ----
    nyxSkelHeader(s, 118, bandX);
    int imgTop = imgLibImgTop(), imgH = imgLibImgAreaH();
    nyxSkelBox(s, 0, imgTop, SCR_W, imgH, 0, bandX);
    int navY = imgTop + imgH + 6;
    nyxSkelNavRow(s, navY, bandX);
    nyxSkelBox(s, 8, navY + 27, (SCR_W - 16) * 72 / 100, 7, 3, bandX);   // judul
    nyxSkelBox(s, 8, navY + 38, 64, 7, 3, bandX);                        // tanggal
  }
}

// =====================================================================
//  POLL UTAMA (dipanggil tiap loop())
// =====================================================================
void nyxExtrasLoopPoll() {
  if (!nyxBootDone) nyxCrashBoot();
  nyxCrumbBeat();
  nyxCrashFlush();
  nyxVoiceTick();
  nyxThumbTick();

  // energi per app: simpan berkala + reset otomatis saat baterai baru penuh
  if (nyxEnLoaded) {
    static bool wasFull = false;
    bool full = inaOk && inaCharging && inaSoc >= 100;
    if (full && !wasFull) nyxEnReset();
    wasFull = full;
    if (nyxEnDirty && millis() - nyxEnSaveMs > 600000UL) nyxEnSave();
  }
}
