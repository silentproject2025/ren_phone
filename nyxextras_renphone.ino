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
// NYX-CMD-V2
#define NYX_CMD_Q   5
#define NYX_CMD_LEN 120
static char nyxCmdQ[NYX_CMD_Q][NYX_CMD_LEN];
static volatile uint8_t nyxCmdN = 0;
static volatile bool    nyxCmdReady = false;

// ---------- helper nilai HP (dipakai instruksi, status, dan eksekusi) ----------
static int nyxBrightPct() {
  int p = 10 + (int)(((long)brightness - 10) * 90L / 245L);
  if (p < 10) p = 10;
  if (p > 100) p = 100;
  return p;
}

static void nyxSetBrightPct(long v) {
  if (v < 10) v = 10;
  if (v > 100) v = 100;
  brightness = (int)(10 + (v - 10) * 245L / 90L);
  display.setBrightness((uint8_t)brightness);
  char b[32]; snprintf(b, sizeof(b), "Kecerahan %ld%%", v);
  showToast(b, 1200);
}

// teks yang ditaruh di AWAL prompt. Dibuat sekali (daftar nama app & tema diambil dari apps[]/themes[]).
String nyxVoiceInstruction() {
  static String cache;
  if (cache.length()) return cache;
  cache.reserve(1700);
  cache  = "ATURAN KONTROL HP: kamu terhubung langsung ke HP DIY ini dan boleh menjalankan aksi. ";
  cache += "Jika user MEMINTA AKSI di HP, jawab SATU kalimat pendek, lalu tulis baris perintah terpisah ";
  cache += "(satu per baris, maksimal 5) dengan format persis: @@CMD {\"a\":\"open\",\"app\":\"Musik\"}\n";
  cache += "Aksi valid:\n";
  cache += "- open (app=nama app)\n";
  cache += "- timer (sec=detik 1-86400) atau {\"a\":\"timer\",\"op\":\"cancel\"}\n";
  cache += "- stopwatch (op=start|stop|reset)\n";
  cache += "- music (op=play|pause|next|prev|shuffle)\n";
  cache += "- volume (val=0-100 untuk nilai pasti, ATAU delta=-100..100 untuk naik/turun relatif)\n";
  cache += "- brightness (val=10-100 ATAU delta=-90..90)\n";
  cache += "- wifi, bluetooth, airplane, dnd, silent (op=on|off; silent on = getar mati)\n";
  cache += "- theme (name=nama tema ATAU op=next)\n";
  cache += "- rotate (op=portrait|landscape)\n";
  cache += "- led (op=on|off|rainbow; color=merah|hijau|biru|kuning|ungu|putih|oranye|pink|cyan ATAU r,g,b 0-255; val=1-100 terang)\n";
  cache += "- screenshot, lock, home, back\n";
  cache += "Contoh: {\"a\":\"volume\",\"delta\":-20} {\"a\":\"led\",\"op\":\"on\",\"color\":\"biru\"} {\"a\":\"wifi\",\"op\":\"off\"}\n";
  cache += "Nama app yang ada: ";
  for (int i = 0; i < APP_COUNT; i++) {
    if (strcmp(apps[i].name, "Atur Home") == 0) continue;
    cache += apps[i].name;
    cache += ", ";
  }
  cache += "\nNama tema: ";
  for (int i = 0; i < THEME_COUNT; i++) { cache += themes[i].name; cache += ", "; }
  cache += "\nKalau user bertanya soal kondisi HP (baterai, jam, volume, dll), jawab dari STATUS HP di bawah tanpa @@CMD. ";
  cache += "Untuk permintaan relatif (\"agak\", \"sedikit\", \"naikin\") pakai delta dari nilai di STATUS. ";
  cache += "Jangan tulis @@CMD untuk pertanyaan biasa. Jangan pakai markdown atau code block pada baris @@CMD. ";
  cache += "Kalau permintaan tidak bisa dilakukan dengan aksi di atas, jelaskan singkat tanpa @@CMD.\n";
  return cache;
}

// Kondisi HP saat ini (dibaca di task Gemini, hanya baca -- tidak mengubah apa pun).
// Dikirim bersama prompt supaya Gemini bisa menjawab "baterai berapa?" dan menghitung nilai relatif.
String nyxVoiceStatus() {
  String s;
  s.reserve(340);
  s  = "STATUS HP SEKARANG: ";
  struct tm t;
  if (ntpSynced && getLocalTime(&t, 5)) {
    char b[28]; snprintf(b, sizeof(b), "jam %02d:%02d, ", t.tm_hour, t.tm_min);
    s += b;
  }
  s += "baterai "; s += String(battPercent); s += "%, ";
  s += "kecerahan "; s += String(nyxBrightPct()); s += "%, ";
  s += "volume musik "; s += String((int)musExtGetVol() * 100 / 127); s += "%, ";
  s += homeMusPlaying() ? "musik diputar, " : (homeMusLoaded() ? "musik dijeda, " : "musik tidak aktif, ");
  s += wifiConnected ? "WiFi tersambung, " : "WiFi putus, ";
  s += airplaneMode ? "mode pesawat ON, " : "";
  s += dndMode ? "DND ON, " : "";
  s += vibEnabled ? "" : "mode senyap ON, ";
  s += "tema "; s += themes[themeIdx].name; s += ", ";
  if (tmRunning) { s += "timer jalan sisa "; s += String((unsigned long)(tmRemain / 1000UL)); s += " detik, "; }
  if (swRunning) s += "stopwatch jalan, ";
  const char* scr = "Home";
  for (int i = 0; i < APP_COUNT; i++) {
    if (apps[i].screen == curScreen()) { scr = apps[i].name; break; }
  }
  s += "layar sekarang: "; s += scr; s += ".\n\n";
  return s;
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

// angka bertanda (boleh negatif utk delta), nilai boleh dalam tanda kutip
static bool nyxJInt(const char* js, const char* key, long& v) {
  char pat[20]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(js, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ' ' || *p == ':' || *p == '"') p++;
  bool neg = false;
  if (*p == '-') { neg = true; p++; } else if (*p == '+') p++;
  if (*p < '0' || *p > '9') return false;
  v = strtol(p, nullptr, 10);
  if (neg) v = -v;
  return true;
}

static void nyxLower(char* s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

// op on/off -> 1 / 0, selain itu -1
static int nyxOnOff(const char* js) {
  char op[12];
  if (!nyxJStr(js, "op", op, sizeof(op))) return -1;
  nyxLower(op);
  if (!strcmp(op, "on") || !strcmp(op, "nyala") || !strcmp(op, "hidup") || !strcmp(op, "true")) return 1;
  if (!strcmp(op, "off") || !strcmp(op, "mati") || !strcmp(op, "false")) return 0;
  return -1;
}

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

static int nyxFindTheme(const char* want) {
  char w[20]; strncpy(w, want, sizeof(w) - 1); w[sizeof(w) - 1] = 0; nyxLower(w);
  int part = -1;
  for (int i = 0; i < THEME_COUNT; i++) {
    char nm[20]; strncpy(nm, themes[i].name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0; nyxLower(nm);
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

static void nyxVolumeOp(const char* js) {
  long v = 0;
  int cur = (int)musExtGetVol() * 100 / 127;
  int target;
  if (nyxJInt(js, "val", v)) target = (int)v;
  else if (nyxJInt(js, "delta", v)) target = cur + (int)v;
  else { showToast("Nilai volume kosong", 1400); return; }
  if (target < 0) target = 0;
  if (target > 100) target = 100;
  musBegin();
  musExtSetVol(target * 127 / 100);
  char b[28]; snprintf(b, sizeof(b), "Volume %d%%", target);
  showToast(b, 1200);
}

static void nyxLedOp(const char* js) {
  struct NyxCol { const char* n; uint8_t r, g, b; };
  static const NyxCol cols[] = {
    {"merah",255,0,0}, {"hijau",0,255,0}, {"biru",0,0,255}, {"kuning",255,200,0}, {"ungu",150,0,255},
    {"putih",255,255,255}, {"oranye",255,90,0}, {"pink",255,40,140}, {"cyan",0,255,255}
  };
  long v = 0;
  if (nyxJInt(js, "val", v)) {
    if (v < 1) v = 1;
    if (v > 100) v = 100;
    npxSetBrightness((uint8_t)(v * 255L / 100L));
  }
  char c[16];
  bool setCol = false;
  if (nyxJStr(js, "color", c, sizeof(c))) {
    nyxLower(c);
    for (unsigned i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) {
      if (!strcmp(c, cols[i].n)) { npxSetMode(0); npxSetSolidColor(cols[i].r, cols[i].g, cols[i].b); setCol = true; break; }
    }
  } else {
    long r, g, b;
    if (nyxJInt(js, "r", r) && nyxJInt(js, "g", g) && nyxJInt(js, "b", b)) {
      r = r < 0 ? 0 : (r > 255 ? 255 : r);
      g = g < 0 ? 0 : (g > 255 ? 255 : g);
      b = b < 0 ? 0 : (b > 255 ? 255 : b);
      npxSetMode(0); npxSetSolidColor((uint8_t)r, (uint8_t)g, (uint8_t)b); setCol = true;
    }
  }
  char op[12];
  bool hasOp = nyxJStr(js, "op", op, sizeof(op));
  if (hasOp) nyxLower(op);
  if (hasOp && !strcmp(op, "rainbow")) { npxSetMode(1); npxTurnOn(); showToast("LED: Rainbow", 1200); }
  else if (hasOp && (!strcmp(op, "off") || !strcmp(op, "mati"))) { npxTurnOff(); showToast("LED mati", 1200); }
  else if (setCol || (hasOp && (!strcmp(op, "on") || !strcmp(op, "nyala")))) { npxTurnOn(); showToast("LED menyala", 1200); }
  else if (!hasOp && !setCol) showToast("Perintah LED kurang jelas", 1400);
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
    char op[12];
    if (nyxJStr(js, "op", op, sizeof(op))) {
      nyxLower(op);
      if (!strcmp(op, "cancel") || !strcmp(op, "stop") || !strcmp(op, "off")) {
        tmRunning = false; tmRemain = tmTotal; swStopRing();
        showToast("Timer dibatalkan", 1300);
        vibSuccess();
        return;
      }
    }
    long sec = 0;
    if (!nyxJInt(js, "sec", sec) || sec < 1) { showToast("Durasi timer kosong", 1400); return; }
    nyxTimerStart(sec);
  } else if (strcmp(a, "stopwatch") == 0) {
    char op[12];
    if (!nyxJStr(js, "op", op, sizeof(op))) { showToast("Perintah stopwatch kosong", 1400); return; }
    nyxLower(op);
    if (!strcmp(op, "start")) {
      if (!swRunning) { swRunning = true; swLastMs = millis(); }
      showToast("Stopwatch jalan", 1200);
    } else if (!strcmp(op, "stop")) {
      swRunning = false;
      showToast("Stopwatch berhenti", 1200);
    } else if (!strcmp(op, "reset")) {
      swRunning = false; swElapsed = 0; swLapN = 0;
      showToast("Stopwatch direset", 1200);
    } else { showToast("Perintah stopwatch tak dikenal", 1400); return; }
  } else if (strcmp(a, "music") == 0) {
    char op[12];
    if (!nyxJStr(js, "op", op, sizeof(op))) { showToast("Perintah musik kosong", 1400); return; }
    nyxLower(op);
    nyxMusicOp(op);
  } else if (strcmp(a, "volume") == 0) {
    nyxVolumeOp(js);
  } else if (strcmp(a, "brightness") == 0) {
    long v = 0;
    if (nyxJInt(js, "val", v)) nyxSetBrightPct(v);
    else if (nyxJInt(js, "delta", v)) nyxSetBrightPct(nyxBrightPct() + v);
    else { showToast("Nilai kecerahan kosong", 1400); return; }
  } else if (strcmp(a, "wifi") == 0) {
    int on = nyxOnOff(js);
    if (on < 0) { showToast("Perintah WiFi kosong", 1400); return; }
    if (on && !wifiConnected && !airplaneMode) { connectWifi(); showToast("WiFi menyambung...", 1400); }
    else if (!on && (wifiConnected || wifiConnecting)) { disconnectWifi(); showToast("WiFi dimatikan", 1300); }
    else showToast(on ? "WiFi sudah aktif" : "WiFi sudah mati", 1200);
  } else if (strcmp(a, "bluetooth") == 0) {
    int on = nyxOnOff(js);
    if (on < 0) { showToast("Perintah Bluetooth kosong", 1400); return; }
    if ((bool)btCcEnabled() != (on == 1)) btCcToggle();
    showToast(on ? "Bluetooth aktif" : "Bluetooth mati", 1300);
  } else if (strcmp(a, "airplane") == 0) {
    int on = nyxOnOff(js);
    if (on < 0) { showToast("Perintah mode pesawat kosong", 1400); return; }
    if (airplaneMode != (on == 1)) toggleAirplaneMode();
    showToast(on ? "Mode pesawat ON" : "Mode pesawat OFF", 1300);
  } else if (strcmp(a, "dnd") == 0) {
    int on = nyxOnOff(js);
    if (on < 0) { showToast("Perintah DND kosong", 1400); return; }
    dndMode = (on == 1);
    showToast(on ? "DND aktif" : "DND mati", 1200);
  } else if (strcmp(a, "silent") == 0) {
    int on = nyxOnOff(js);
    if (on < 0) { showToast("Perintah senyap kosong", 1400); return; }
    vibEnabled = (on == 0);
    saveVibPref();
    showToast(on ? "Mode senyap: getar mati" : "Getar aktif", 1300);
  } else if (strcmp(a, "theme") == 0) {
    char nm[20];
    int idx = -1;
    if (nyxJStr(js, "name", nm, sizeof(nm))) idx = nyxFindTheme(nm);
    else if (nyxJStr(js, "op", nm, sizeof(nm))) { nyxLower(nm); if (!strcmp(nm, "next")) idx = (themeIdx + 1) % THEME_COUNT; }
    if (idx < 0) { showToast("Tema tidak ditemukan", 1500); return; }
    themeIdx = idx;
    saveTheme();
    char b[36]; snprintf(b, sizeof(b), "Tema: %s", themes[idx].name);
    showToast(b, 1300);
  } else if (strcmp(a, "rotate") == 0) {
    char op[12];
    if (!nyxJStr(js, "op", op, sizeof(op))) { showToast("Arah layar kosong", 1400); return; }
    nyxLower(op);
    if (!strcmp(op, "portrait")) applyOrientation(ORIENT_PORTRAIT, true);
    else if (!strcmp(op, "landscape")) applyOrientation(ORIENT_LANDSCAPE, true);
    else { showToast("Arah layar tak dikenal", 1400); return; }
    if (autoRotateEnabled) { autoRotateEnabled = false; saveAutoRotatePref(); }
  } else if (strcmp(a, "led") == 0) {
    nyxLedOp(js);
  } else if (strcmp(a, "screenshot") == 0) {
    ccActScreenshot();
  } else if (strcmp(a, "lock") == 0) {
    locked = true;
    lockScreenReset();
    closeControlCenter();
    return;                                // layar terkunci: sisa perintah di antrean dihentikan (lihat nyxVoiceTick)
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
  for (uint8_t i = 0; i < n; i++) {
    if (locked) break;                     // stop kalau perintah "lock" baru saja dijalankan
    nyxVoiceRun(nyxCmdQ[i]);
  }
  needRedraw = true;
}

// =====================================================================
//  4. THUMBNAIL SCREENSHOT
// =====================================================================
#define NYX_TH_MIN_W    88          // lebar thumbnail = ±30% lebar layar, dijepit 88..124 px
#define NYX_TH_MAX_W    124
#define NYX_TH_BORDER   3           // 2 px cincin terang + 1 px garis gelap di antara cincin & gambar
#define NYX_TH_RADIUS   11          // radius luar bingkai
#define NYX_TH_IN_MS    420         // NYX_TH_STRIP_V3
#define NYX_TH_HOLD_MS  3800
#define NYX_TH_OUT_MS   320
#define NYX_TH_SH_SP    9           // bayangan memudar sejauh ini (px)
#define NYX_TH_SH_OY    4           // bayangan turun sekian px
#define NYX_TH_REST_X   12          // posisi x gambar saat diam

void uiBoostKick();

static bool nyxThOn = false;
static uint32_t nyxThT0 = 0, nyxThOutT0 = 0;
static char nyxThPath[64] = "";
static bool nyxThDown = false, nyxThDrag = false;
static int  nyxThX0 = 0;
static int  nyxThW = 0, nyxThH = 0;            // ukuran GAMBAR (tanpa bingkai)

// ---- kartu hasil bake: urutan byte = urutan buffer sprite (swap565), premultiplied ----
static uint16_t* nyxThCard   = nullptr;        // fw*fh
static uint8_t*  nyxThCardA  = nullptr;        // fw*fh alpha (255 = buram)
static uint8_t*  nyxThRowLo  = nullptr;        // per baris: piksel [0,lo) di kiri tidak buram penuh
static uint8_t*  nyxThRowHi  = nullptr;        // per baris: piksel [hi,fw) di kanan tidak buram penuh
static uint8_t*  nyxThShList = nullptr;        // bayangan: 3 byte per entri (mx, my, kekuatan)
static int       nyxThShN    = 0;
static int       nyxThFw = 0, nyxThFh = 0;     // ukuran bingkai
static int       nyxThFy = 0;                  // y layar tepi atas bingkai (tetap)
// ---- jalur (band) layar tempat thumbnail bergerak ----
static LGFX_Sprite nyxThBg(&display);          // latar bersih di jalur (salinan canvas, TANPA thumbnail)
static LGFX_Sprite nyxThStrip(&display);       // buffer kerja: latar + bayangan + kartu -> ke layar
static bool nyxThSprOk = false;
static int  nyxThBx = 0, nyxThBy = 0, nyxThBw = 0, nyxThBh = 0;
static int  nyxThScrW = 0, nyxThScrH = 0;
// ---- status jalur cepat ----
static bool     nyxThBgValid = false;          // latar sah = salinan canvas dari frame penuh terakhir
static bool     nyxThDispVis = false;          // kartu sedang terlihat di layar
static bool     nyxThFastWant = false;         // tick minta 1 frame cepat
static uint32_t nyxThFullN = 0, nyxThSeenFullN = 0;
static uint32_t nyxThSig = 0;                  // tanda tangan layar saat latar diambil

static void* nyxThAlloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return p;
}
static inline uint16_t nyxSw16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static void nyxThFree() {
  if (nyxThCard)   { free(nyxThCard);   nyxThCard = nullptr; }
  if (nyxThCardA)  { free(nyxThCardA);  nyxThCardA = nullptr; }
  if (nyxThRowLo)  { free(nyxThRowLo);  nyxThRowLo = nullptr; }
  if (nyxThRowHi)  { free(nyxThRowHi);  nyxThRowHi = nullptr; }
  if (nyxThShList) { free(nyxThShList); nyxThShList = nullptr; }
  if (nyxThSprOk)  { nyxThBg.deleteSprite(); nyxThStrip.deleteSprite(); nyxThSprOk = false; }
  nyxThShN = 0; nyxThFw = 0; nyxThFh = 0;
  nyxThBgValid = false; nyxThDispVis = false; nyxThFastWant = false;
  nyxThOn = false; nyxThDown = false; nyxThDrag = false; nyxThOutT0 = 0;
}

void nyxShotThumbHide() { if (nyxThOn || nyxThCard) { nyxThFree(); needRedraw = true; } }
bool nyxShotThumbActive() { return nyxThOn && nyxThCard && nyxThSprOk; }

// ease-in-out kosinus: awal & akhir lembut, gerak tengah merata
static float nyxEaseIO(float k) { if (k < 0) k = 0; if (k > 1) k = 1; return 0.5f - 0.5f * cosf(3.14159265f * k); }

// posisi x gambar saat ini (animasi masuk / keluar) -- dihitung dari WAKTU NYATA
static int nyxThCurX() {
  const int restX = NYX_TH_REST_X;
  uint32_t now = millis();
  if (nyxThOutT0) {
    float k = nyxEaseIO((float)(now - nyxThOutT0) / (float)NYX_TH_OUT_MS);
    return (int)lroundf((float)restX - (float)(nyxThW + restX + 12) * k);
  }
  float k = nyxEaseIO((float)(now - nyxThT0) / (float)NYX_TH_IN_MS);
  return (int)lroundf((float)(-(nyxThW + 12)) + (float)(restX + nyxThW + 12) * k);
}
static int nyxThCurY() { return SCR_H - BACK_H - nyxThH - 18; }

// jarak bertanda (px) dari titik ke persegi membulat: <= 0 di dalam
static float nyxSdfRR(float px, float py, float cx, float cy, float hw, float hh, float r) {
  float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = (qx > qy ? qx : qy); if (inside > 0) inside = 0;
  return sqrtf(ox * ox + oy * oy) + inside - r;
}

// gambar kartu (cincin + garis gelap + gambar dgn sudut anti-alias) ke sprite g di (0,0)
static void nyxThDrawCardInto(LGFX_Sprite& g, const uint16_t* img, int tw, int th) {
  const int bw = NYX_TH_BORDER, ro = NYX_TH_RADIUS, ri = ro - bw;
  int fw = tw + bw * 2, fh = th + bw * 2;
  const uint16_t hair = 0x1082;
  iosRR(g, 0, 0, fw, fh, ro, blend565(T().text, T().bg, 22));
  iosRR(g, 2, 2, fw - 4, fh - 4, ro - 2, hair);
  for (int j = 0; j < th; j++) {
    bool cornerRow = (j < ri) || (j >= th - ri);
    const uint16_t* row = img + (size_t)j * tw;
    for (int i = 0; i < tw; i++) {
      uint16_t c = row[i];
      if (cornerRow && ((i < ri) || (i >= tw - ri))) {
        float ddx = fmaxf(fmaxf((float)ri - (i + 0.5f), (i + 0.5f) - (float)(tw - ri)), 0.0f);
        float ddy = fmaxf(fmaxf((float)ri - (j + 0.5f), (j + 0.5f) - (float)(th - ri)), 0.0f);
        if (ddx > 0.0f && ddy > 0.0f) {
          float d = sqrtf(ddx * ddx + ddy * ddy) - (float)ri;
          if (d > 0.5f) continue;
          if (d > -0.5f) c = blend565(hair, c, (uint8_t)((0.5f - d) * 255.0f));
        }
      }
      g.drawPixel(bw + i, bw + j, c);
    }
  }
}

// BAKE (sekali per screenshot). false = gagal (memori) -> pemanggil memanggil nyxThFree()
static bool nyxThBake(const uint16_t* img) {
  const int bd = NYX_TH_BORDER, ro = NYX_TH_RADIUS, sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  int fw = nyxThW + bd * 2, fh = nyxThH + bd * 2;
  size_t n = (size_t)fw * fh;
  if (fw > 250 || fh > 250) return false;

  // --- 1) kartu premultiplied + alpha: gambar di atas hitam dan putih ---
  LGFX_Sprite t(&display);
  t.setColorDepth(16); t.setPsram(true);
  if (!t.createSprite(fw, fh)) return false;
  uint16_t* pb = (uint16_t*)nyxThAlloc(n * 2);
  nyxThCard  = (uint16_t*)nyxThAlloc(n * 2);
  nyxThCardA = (uint8_t*)nyxThAlloc(n);
  nyxThRowLo = (uint8_t*)nyxThAlloc((size_t)fh);
  nyxThRowHi = (uint8_t*)nyxThAlloc((size_t)fh);
  if (!pb || !nyxThCard || !nyxThCardA || !nyxThRowLo || !nyxThRowHi) {
    if (pb) free(pb);
    t.deleteSprite();
    return false;
  }
  t.fillSprite(0x0000);
  nyxThDrawCardInto(t, img, nyxThW, nyxThH);
  for (int j = 0; j < fh; j++) for (int i = 0; i < fw; i++) pb[(size_t)j * fw + i] = (uint16_t)t.readPixel(i, j);
  t.fillSprite(0xFFFF);
  nyxThDrawCardInto(t, img, nyxThW, nyxThH);
  for (int j = 0; j < fh; j++) {
    for (int i = 0; i < fw; i++) {
      size_t k = (size_t)j * fw + i;
      uint16_t pw = (uint16_t)t.readPixel(i, j);
      int dg = (int)((pw >> 5) & 63) - (int)((pb[k] >> 5) & 63);
      if (dg < 0) dg = 0;
      uint8_t a;
      if (dg == 0) a = 255;
      else { int v = 255 - (dg * 255) / 63; a = (uint8_t)(v < 0 ? 0 : v); }
      nyxThCard[k]  = nyxSw16(a ? pb[k] : (uint16_t)0);
      nyxThCardA[k] = a;
    }
  }
  free(pb);
  t.deleteSprite();
  for (int j = 0; j < fh; j++) {                       // bagian buram penuh per baris -> memcpy saat compose
    const uint8_t* ar = nyxThCardA + (size_t)j * fw;
    int lo = 0; while (lo < fw && ar[lo] != 255) lo++;
    int hi = fw; while (hi > lo && ar[hi - 1] != 255) hi--;
    for (int i = lo; i < hi; i++) if (ar[i] != 255) { lo = hi = fw; break; }   // tak terduga: semua lewat jalur blend
    nyxThRowLo[j] = (uint8_t)lo; nyxThRowHi[j] = (uint8_t)hi;
  }
  nyxThFw = fw; nyxThFh = fh;

  // --- 2) bayangan: daftar piksel tak-nol relatif terhadap kartu ---
  int mw = fw + sp * 2, mh = fh + sp * 2;
  if (mw > 255 || mh > 255) return false;
  uint8_t* m = (uint8_t*)nyxThAlloc((size_t)mw * mh);
  if (!m) return false;
  float cx = fw * 0.5f, cy = fh * 0.5f, hw = cx, hh = cy;
  int cnt = 0;
  for (int my = 0; my < mh; my++) {
    for (int mx = 0; mx < mw; mx++) {
      float fp = (float)(mx - sp) + 0.5f, fq = (float)(my - sp + soy) + 0.5f;
      uint8_t v = 0;
      if (nyxSdfRR(fp, fq, cx, cy, hw, hh, (float)ro) > 0.0f) {
        float d = nyxSdfRR(fp, fq, cx, cy + (float)soy, hw, hh, (float)ro);
        if (d < (float)sp) {
          float kk = d <= 0 ? 1.0f : 1.0f - d / (float)sp;
          v = (uint8_t)(kk * kk * 255.0f + 0.5f);
        }
      }
      m[(size_t)my * mw + mx] = v;
      if (v) cnt++;
    }
  }
  nyxThShList = (uint8_t*)nyxThAlloc((size_t)(cnt > 0 ? cnt : 1) * 3);
  if (!nyxThShList) { free(m); return false; }
  int w = 0;
  for (int my = 0; my < mh; my++) for (int mx = 0; mx < mw; mx++) {
    uint8_t v = m[(size_t)my * mw + mx];
    if (!v) continue;
    nyxThShList[w * 3 + 0] = (uint8_t)mx; nyxThShList[w * 3 + 1] = (uint8_t)my; nyxThShList[w * 3 + 2] = v; w++;
  }
  nyxThShN = w;
  free(m);

  // --- 3) jalur layar + sprite latar/kerja ---
  int fy = nyxThCurY() - bd;
  int by = fy - sp, bh = fh + sp * 2 + soy;
  if (by < 0) { bh += by; by = 0; }
  if (by + bh > SCR_H) bh = SCR_H - by;
  int bw = (NYX_TH_REST_X - bd) + fw + sp;
  if (bw > SCR_W) bw = SCR_W;
  if (bw <= 0 || bh <= 0) return false;
  nyxThFy = fy; nyxThBx = 0; nyxThBy = by; nyxThBw = bw; nyxThBh = bh;
  nyxThScrW = SCR_W; nyxThScrH = SCR_H;
  nyxThBg.setColorDepth(16);    nyxThBg.setPsram(true);
  nyxThStrip.setColorDepth(16); nyxThStrip.setPsram(true);
  if (!nyxThBg.createSprite(bw, bh)) return false;
  if (!nyxThStrip.createSprite(bw, bh)) { nyxThBg.deleteSprite(); return false; }
  nyxThSprOk = true;
  return true;
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
  uint16_t* img = (uint16_t*)nyxThAlloc((size_t)tw * th * 2);
  if (!img) return;
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
      img[j * tw + i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
  }
  nyxThW = tw; nyxThH = th;
  bool ok = nyxThBake(img);
  free(img);
  if (!ok) { nyxThFree(); return; }                 // gagal: pemanggil menampilkan toast biasa
  strncpy(nyxThPath, path ? path : "", sizeof(nyxThPath) - 1);
  nyxThPath[sizeof(nyxThPath) - 1] = 0;
  nyxThOn = true; nyxThT0 = millis(); nyxThOutT0 = 0;
  vibSuccess();
}

// tanda tangan layar: kalau berubah sejak latar diambil, latar tidak boleh dipakai lagi
static uint32_t nyxThScreenSig() {
  return (uint32_t)curScreen()
       | ((uint32_t)controlCenterOpen << 8) | ((uint32_t)notifShadeOpen << 9) | ((uint32_t)appSwitcherOpen << 10)
       | ((uint32_t)homeAodActive << 11) | ((uint32_t)diMenuOpen << 12) | ((uint32_t)kbVisible << 13);
}

// salin latar bersih (canvas SEBELUM thumbnail digambar) ke sprite latar
static void nyxThCaptureBg(LGFX_Sprite& s) {
  s.pushSprite(&nyxThBg, -nyxThBx, -nyxThBy);
  nyxThSig = nyxThScreenSig();
}

static inline void nyxThBlendPx(uint16_t* d, uint16_t cardRaw, uint8_t a) {
  if (!a) return;
  uint16_t c = nyxSw16(cardRaw), b = nyxSw16(*d);
  uint32_t ia = 255u - a;
  uint32_t r  = ((c >> 11) & 31) + ((((b >> 11) & 31) * ia + 127u) / 255u);
  uint32_t g  = ((c >> 5) & 63)  + ((((b >> 5) & 63)  * ia + 127u) / 255u);
  uint32_t bl = (c & 31)         + (((b & 31)         * ia + 127u) / 255u);
  if (r > 31) r = 31; if (g > 63) g = 63; if (bl > 31) bl = 31;
  *d = nyxSw16((uint16_t)((r << 11) | (g << 5) | bl));
}

// SATU-SATUNYA jalur gambar: latar -> bayangan -> kartu, hasilnya di nyxThStrip.
// fl = x tepi kiri bingkai dalam koordinat jalur (boleh negatif / melewati kanan).
static void nyxThCompose(int fl) {
  const int bw = nyxThBw, bh = nyxThBh, sp = NYX_TH_SH_SP, soy = NYX_TH_SH_OY;
  const int ft = nyxThFy - nyxThBy;
  nyxThBg.pushSprite(&nyxThStrip, 0, 0);
  uint16_t* buf = (uint16_t*)nyxThStrip.getBuffer();
  if (!buf) return;
  const int stride = nyxThStrip.width();

  // 1) bayangan (latar terang: gelap; latar gelap: cahaya halus warna aksen)
  if (nyxThShList) {
    const uint16_t acc = (uint16_t)T().accent;
    const int ox = fl - sp, oy = ft - sp + soy;
    const uint8_t* e = nyxThShList;
    for (int i = 0; i < nyxThShN; i++, e += 3) {
      int px = ox + e[0], py = oy + e[1];
      if ((unsigned)px >= (unsigned)bw || (unsigned)py >= (unsigned)bh) continue;
      uint32_t m = e[2];
      uint16_t* d = buf + (size_t)py * stride + px;
      uint16_t bgp = nyxSw16(*d);
      uint32_t lum = ((((bgp >> 11) & 31) << 3) * 77 + (((bgp >> 5) & 63) << 2) * 150 + ((bgp & 31) << 3) * 29) >> 8;
      uint32_t il = 255u - lum;
      uint32_t aDark = (150u * m * lum) / 65025u;
      uint32_t aGlow = (110u * m * il * il) / 16581375u;
      if (aDark < 3 && aGlow < 3) continue;
      uint16_t o = bgp;
      if (aDark >= 3) o = blend565(o, 0x0000, (uint8_t)aDark);
      if (aGlow >= 3) o = blend565(o, acc, (uint8_t)aGlow);
      *d = nyxSw16(o);
    }
  }

  // 2) kartu: bagian buram = memcpy per baris, hanya tepi sudut yang di-blend
  const int fw = nyxThFw;
  for (int j = 0; j < nyxThFh; j++) {
    int py = ft + j;
    if ((unsigned)py >= (unsigned)bh) continue;
    uint16_t* drow = buf + (size_t)py * stride;
    const uint16_t* crow = nyxThCard + (size_t)j * fw;
    const uint8_t* arow = nyxThCardA + (size_t)j * fw;
    int lo = nyxThRowLo[j], hi = nyxThRowHi[j];
    if (hi > lo) {
      int x0 = fl + lo, x1 = fl + hi;
      if (x0 < 0) x0 = 0;
      if (x1 > bw) x1 = bw;
      if (x1 > x0) memcpy(drow + x0, crow + (x0 - fl), (size_t)(x1 - x0) * 2);
    }
    for (int i = 0; i < lo; i++) { int px = fl + i; if ((unsigned)px < (unsigned)bw) nyxThBlendPx(drow + px, crow[i], arow[i]); }
    for (int i = hi; i < fw; i++) { int px = fl + i; if ((unsigned)px < (unsigned)bw) nyxThBlendPx(drow + px, crow[i], arow[i]); }
  }
}

// ---- jalur FRAME PENUH (dipanggil renderCurrentFrame): hasil piksel identik dgn jalur cepat ----
void drawShotThumb(LGFX_Sprite& s) {
  if (!nyxShotThumbActive()) return;
  nyxThFullN++;
  nyxThCaptureBg(s);
  nyxThBgValid = (&s == &canvas) && !homeBandMode;   // mode pita: canvas di luar pita belum tentu bersih
  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;
  if (fl + nyxThFw + NYX_TH_SH_SP <= 0) { nyxThDispVis = false; return; }   // masih di luar layar
  nyxThCompose(fl);
  nyxThStrip.pushSprite(&s, nyxThBx, nyxThBy);       // ke canvas (patuh clip rect)
  nyxThDispVis = true;
}

// boleh pakai jalur cepat (kirim strip langsung ke layar tanpa render UI)?
static bool nyxThFastOk() {
  if (!nyxThBgValid || !nyxThSprOk || !nyxThCard) return false;
  if (SCR_W != nyxThScrW || SCR_H != nyxThScrH) return false;
  if (locked || powerMenuOpen || callBannerActive() || gameModeActive) return false;
  Screen sc = curScreen();
  if (sc == SCR_CAMERA || sc == SCR_MJPEG || sc == SCR_NES || sc == SCR_DOOM || sc == SCR_PPU || sc == SCR_CALL) return false;
  if (nyxThScreenSig() != nyxThSig) return false;
  return true;
}

// dipanggil dari loop() (akhir putaran): gambar 1 frame animasi hanya sebagai strip
void nyxShotThumbFastFrame() {
  if (!nyxThFastWant) return;
  nyxThFastWant = false;
  if (!nyxShotThumbActive()) return;
  if (nyxThFullN != nyxThSeenFullN) { nyxThSeenFullN = nyxThFullN; return; }   // frame penuh baru saja menggambarnya
  if (!nyxThFastOk()) { needRedraw = true; return; }                            // fallback: jalur lama
  uiBoostKick();
  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;
  bool vis = (fl + nyxThFw + NYX_TH_SH_SP > 0);
  if (!vis && !nyxThDispVis) return;                 // belum muncul & layar sudah bersih: tak ada yang dikirim
  nyxThCompose(fl);
  nyxThStrip.pushSprite(nyxThBx, nyxThBy);           // hanya strip ±160x118 lewat SPI
  nyxThDispVis = vis;
}

// true selama thumbnail sedang bergerak (masuk / keluar)
bool nyxShotThumbAnimating() {
  if (!nyxShotThumbActive()) return false;
  if (nyxThOutT0) return true;
  return (millis() - nyxThT0) < (uint32_t)(NYX_TH_IN_MS + 80);
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

static void nyxThAnimStep() {
  if (nyxThFastOk()) nyxThFastWant = true;     // frame animasi = strip kecil saja
  else needRedraw = true;                      // belum ada latar sah / kondisi khusus -> frame penuh (jalur lama)
}

static void nyxThumbTick() {
  if (!nyxThCard) return;
  if (locked) { nyxThFree(); return; }
  uint32_t now = millis();
  if (SCR_W != nyxThScrW || SCR_H != nyxThScrH) { nyxThFree(); needRedraw = true; return; }   // rotasi berubah
  if (nyxThOutT0) {
    if (now - nyxThOutT0 >= NYX_TH_OUT_MS) { nyxThFree(); needRedraw = true; }
    else nyxThAnimStep();
    return;
  }
  if (now - nyxThT0 < NYX_TH_IN_MS + 40) nyxThAnimStep();
  else if (!nyxThDown && now - nyxThT0 > NYX_TH_IN_MS + NYX_TH_HOLD_MS) { nyxThOutT0 = now; nyxThAnimStep(); }
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
//  6. BENTUK ANTI-ALIAS (lingkaran, persegi membulat, cincin)
//     Dipakai menggantikan s.fillCircle / fillRoundRect / drawCircle / drawRoundRect
//     (apply_nyx_aa_patch.py menulis ulang pemanggilnya). Hanya piksel di tepi yang
//     dicampur dgn latar (readPixel), bagian dalam tetap digambar lewat drawFastHLine/
//     fillRect biasa -> murah. Hanya untuk sprite (canvas), bukan langsung ke panel.
// =====================================================================
// ---- NYX-AA-BEGIN
static inline void nyxAaPx(lgfx::LGFXBase& g, int x, int y, uint16_t col, float cov) {
  if (cov <= 0.03f) return;
  if (x < 0 || y < 0 || x >= g.width() || y >= g.height()) return;
  if (cov >= 0.97f) { g.drawPixel(x, y, col); return; }
  uint16_t bg = (uint16_t)g.readPixel(x, y);
  g.drawPixel(x, y, blend565(bg, col, (uint8_t)(cov * 255.0f)));
}

// seperempat lingkaran terisi. Pusat (cx,cy), arah (sx,sy) = +1/-1, i0/j0 = mulai dari 0 atau 1
// (sumbu dimiliki satu kuadran saja supaya tidak dicampur dua kali).
static void nyxAaQuadFill(lgfx::LGFXBase& g, int cx, int cy, int sx, int sy, int r, int i0, int j0, uint16_t col) {
  const float inR = (float)r - 0.15f, outR = (float)r + 0.85f;
  for (int j = j0; j <= r; j++) {
    float a = inR * inR - (float)(j * j);
    int xin = a >= 0.0f ? (int)floorf(sqrtf(a)) : -1;                 // i <= xin: pasti di dalam
    float b = outR * outR - (float)(j * j);
    if (b < 0.0f) continue;
    int xout = (int)floorf(sqrtf(b));
    int y = cy + sy * j;
    if (xin >= i0) {
      int xa = cx + sx * i0, xb = cx + sx * xin;
      g.drawFastHLine(xa < xb ? xa : xb, y, xin - i0 + 1, col);
    }
    for (int i = (xin + 1 > i0 ? xin + 1 : i0); i <= xout; i++) {
      float d = sqrtf((float)(i * i + j * j));
      nyxAaPx(g, cx + sx * i, y, col, outR - d);
    }
  }
}

// seperempat cincin 1 px (garis tepi)
static void nyxAaQuadRing(lgfx::LGFXBase& g, int cx, int cy, int sx, int sy, int r, int i0, int j0, uint16_t col) {
  for (int j = j0; j <= r + 1; j++) {
    float lo2 = (float)((r - 1) * (r - 1) - j * j);
    float lo = lo2 > 0.0f ? sqrtf(lo2) : 0.0f;
    float hi2 = (float)((r + 1) * (r + 1) - j * j);
    if (hi2 < 0.0f) continue;
    float hi = sqrtf(hi2);
    int ia = (int)floorf(lo); if (ia < i0) ia = i0;
    int ib = (int)ceilf(hi);
    for (int i = ia; i <= ib; i++) {
      float d = sqrtf((float)(i * i + j * j));
      nyxAaPx(g, cx + sx * i, cy + sy * j, col, 1.0f - fabsf(d - (float)r));
    }
  }
}

void nyxAaCircle(lgfx::LGFXBase& g, int cx, int cy, int r, uint16_t col) {
  if (r <= 1) { g.fillCircle(cx, cy, r, col); return; }
  nyxAaQuadFill(g, cx, cy, +1, +1, r, 0, 0, col);
  nyxAaQuadFill(g, cx, cy, -1, +1, r, 1, 0, col);
  nyxAaQuadFill(g, cx, cy, +1, -1, r, 0, 1, col);
  nyxAaQuadFill(g, cx, cy, -1, -1, r, 1, 1, col);
}

void nyxAaCircleRing(lgfx::LGFXBase& g, int cx, int cy, int r, uint16_t col) {
  if (r <= 2) { g.drawCircle(cx, cy, r, col); return; }
  nyxAaQuadRing(g, cx, cy, +1, +1, r, 0, 0, col);
  nyxAaQuadRing(g, cx, cy, -1, +1, r, 1, 0, col);
  nyxAaQuadRing(g, cx, cy, +1, -1, r, 0, 1, col);
  nyxAaQuadRing(g, cx, cy, -1, -1, r, 1, 1, col);
}

void nyxAaRR(lgfx::LGFXBase& g, int x, int y, int w, int h, int r, uint16_t col) {
  if (w <= 0 || h <= 0) return;
  int rm = (w < h ? w : h) / 2;
  if (r > rm) r = rm;
  if (r <= 1) { g.fillRoundRect(x, y, w, h, r < 0 ? 0 : r, col); return; }
  g.fillRect(x + r, y, w - 2 * r, h, col);                    // pita tengah penuh tinggi
  if (h - 2 * r > 0) {
    g.fillRect(x, y + r, r, h - 2 * r, col);                  // sisi kiri
    g.fillRect(x + w - r, y + r, r, h - 2 * r, col);          // sisi kanan
  }
  nyxAaQuadFill(g, x + r,         y + r,         -1, -1, r, 1, 1, col);
  nyxAaQuadFill(g, x + w - 1 - r, y + r,         +1, -1, r, 1, 1, col);
  nyxAaQuadFill(g, x + r,         y + h - 1 - r, -1, +1, r, 1, 1, col);
  nyxAaQuadFill(g, x + w - 1 - r, y + h - 1 - r, +1, +1, r, 1, 1, col);
}

void nyxAaRRRing(lgfx::LGFXBase& g, int x, int y, int w, int h, int r, uint16_t col) {
  if (w <= 0 || h <= 0) return;
  int rm = (w < h ? w : h) / 2;
  if (r > rm) r = rm;
  if (r <= 2) { g.drawRoundRect(x, y, w, h, r < 0 ? 0 : r, col); return; }
  g.drawFastHLine(x + r, y, w - 2 * r, col);
  g.drawFastHLine(x + r, y + h - 1, w - 2 * r, col);
  if (h - 2 * r > 0) {
    g.drawFastVLine(x, y + r, h - 2 * r, col);
    g.drawFastVLine(x + w - 1, y + r, h - 2 * r, col);
  }
  nyxAaQuadRing(g, x + r,         y + r,         -1, -1, r, 1, 1, col);
  nyxAaQuadRing(g, x + w - 1 - r, y + r,         +1, -1, r, 1, 1, col);
  nyxAaQuadRing(g, x + r,         y + h - 1 - r, -1, +1, r, 1, 1, col);
  nyxAaQuadRing(g, x + w - 1 - r, y + h - 1 - r, +1, +1, r, 1, 1, col);
}
// ---- NYX-AA-END

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
