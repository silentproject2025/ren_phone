#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_nyx_extras_patch.py  --  jalankan dari root repo ren_phone (folder yang berisi phone.ino):

    python apply_nyx_extras_patch.py

Menambahkan 6 fitur NyxOS:
  1. Asisten suara berbasis Gemini (perintah "@@CMD" -> aksi di HP)
  2. Statistik energi baterai per app (tab "Per App" di app Baterai)
  3. Log crash ke SD + tampil di About Phone
  4. Banner panggilan masuk (gaya Dynamic Island) dgn tombol Tolak/Angkat
  5. Thumbnail screenshot (ketuk = Galeri, geser kiri = tutup)
  6. Skeleton loading (APOD, NEO, EPIC, Galeri NASA)

Berkas baru : nyxextras_renphone.ino
Berkas diubah: phone.ino, call_renphone.ino, ina219_renphone.ino, about_renphone.ino,
               features_renphone.ino, .github/workflows/build-firmware.yml, README.md
Aman dijalankan ulang (penanda NYX-EXTRAS-V1). Semua anchor dicek PERSIS 1 kali;
kalau ada yang tidak cocok, SEMUA berkas dibiarkan utuh (tidak ada yang setengah jadi).
"""
import os, sys

MARK = "NYX-EXTRAS-V1"

NEW_INO = r'''// =====================================================================
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
#define NYX_TH_W        64
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
  int tw = NYX_TH_W, th = (tw * h) / w;
  if (th < 24) th = 24;
  if (th > 96) th = 96;
  nyxThumb = (uint16_t*)heap_caps_malloc((size_t)tw * th * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!nyxThumb) nyxThumb = (uint16_t*)malloc((size_t)tw * th * 2);
  if (!nyxThumb) return;
  for (int j = 0; j < th; j++) {
    int y0 = (j * h) / th, y1 = ((j + 1) * h) / th - 1; if (y1 < y0) y1 = y0;
    for (int i = 0; i < tw; i++) {
      int x0 = (i * w) / tw, x1 = ((i + 1) * w) / tw - 1; if (x1 < x0) x1 = x0;
      uint32_t r = 0, g = 0, b = 0; int cnt = 0;
      for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) {
        uint16_t c = (uint16_t)canvas.readPixel(sx ? x1 : x0, sy ? y1 : y0);
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
  const int restX = 8;
  uint32_t now = millis();
  if (nyxThOutT0) {
    float k = nyxEaseOut((float)(now - nyxThOutT0) / (float)NYX_TH_OUT_MS);
    return (int)(restX - (nyxThW + restX + 6) * k);
  }
  float k = nyxEaseOut((float)(now - nyxThT0) / (float)NYX_TH_IN_MS);
  return (int)(-(nyxThW + 6) + (restX + nyxThW + 6) * k);
}
static int nyxThCurY() { return SCR_H - BACK_H - nyxThH - 14; }

void drawShotThumb(LGFX_Sprite& s) {
  if (!nyxShotThumbActive()) return;
  int x = nyxThCurX(), y = nyxThCurY();
  iosRR(s, x - 2 + 2, y - 2 + 3, nyxThW + 4, nyxThH + 4, 5, blend565(T().bg, 0x0000, 170));   // bayangan
  iosRR(s, x - 2, y - 2, nyxThW + 4, nyxThH + 4, 5, T().text);                                  // bingkai
  for (int j = 0; j < nyxThH; j++)
    for (int i = 0; i < nyxThW; i++) s.drawPixel(x + i, y + j, nyxThumb[j * nyxThW + i]);
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

// kind: 0 = APOD (gambar + teks), 1 = daftar (NEO), 2 = EPIC (citra bulat + keterangan), 3 = galeri 2x2
void nyxSkeleton(LGFX_Sprite& s, int kind) {
  const unsigned long ms = millis();
  float bandX = fmodf((float)ms, 1500.0f) / 1500.0f * (float)(SCR_W + 140) - 70.0f;
  int top = STATUS_H + 4;
  int bot = SCR_H - BACK_H - 8;

  if (kind == 0) {
    nyxSkelBox(s, 8, top + 2, 120, 9, 4, bandX);
    nyxSkelBox(s, SCR_W - 4 - 28 - 4 - 28 - 4 - 44, top - 1, 44, 18, 4, bandX);
    nyxSkelBox(s, SCR_W - 4 - 28 - 4 - 28, top - 1, 28, 18, 4, bandX);
    nyxSkelBox(s, SCR_W - 4 - 28, top - 1, 28, 18, 4, bandX);
    int imgTop = top + 22, imgH = (bot - imgTop) * 48 / 100;
    nyxSkelBox(s, 0, imgTop, SCR_W, imgH, 0, bandX);
    int ty = imgTop + imgH + 8;
    iosRR(s, 4, ty, SCR_W - 8, bot - ty + 4, 6, T().surface);
    int ly = ty + 8;
    nyxSkelBox(s, 12, ly, SCR_W * 55 / 100, 10, 4, bandX); ly += 18;
    for (int i = 0; ly + 8 < bot && i < 6; i++, ly += 13)
      nyxSkelBox(s, 12, ly, (i % 3 == 2) ? SCR_W * 60 / 100 : SCR_W - 32, 7, 3, bandX);
  } else if (kind == 1) {
    nyxSkelBox(s, 8, top, SCR_W - 16, 18, 6, bandX);
    int y = top + 28;
    for (int i = 0; y + 34 <= bot && i < 5; i++, y += 38) {
      iosRR(s, 6, y, SCR_W - 12, 34, 7, T().surface);
      nyxSkelBox(s, 14, y + 7, 20, 20, 10, bandX);
      nyxSkelBox(s, 44, y + 8, SCR_W * 40 / 100, 8, 3, bandX);
      nyxSkelBox(s, 44, y + 21, SCR_W * 28 / 100, 6, 3, bandX);
      nyxSkelBox(s, SCR_W - 66, y + 10, 50, 14, 5, bandX);
    }
  } else if (kind == 2) {
    nyxSkelBox(s, 8, top + 2, 110, 9, 4, bandX);
    int side = (bot - top - 60);
    if (side > SCR_W - 40) side = SCR_W - 40;
    if (side < 60) side = 60;
    nyxSkelBox(s, (SCR_W - side) / 2, top + 20, side, side, side / 2, bandX);
    int ty = top + 20 + side + 10;
    nyxSkelBox(s, 16, ty, SCR_W - 32, 8, 3, bandX);
    nyxSkelBox(s, SCR_W / 4, ty + 14, SCR_W / 2, 7, 3, bandX);
  } else {
    nyxSkelBox(s, 8, top, SCR_W - 16, 18, 6, bandX);
    int gy = top + 26, gw = (SCR_W - 8 * 3) / 2, gh = (bot - gy - 6) / 2;
    if (gh > 90) gh = 90;
    for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++) {
      int gx = 8 + c * (gw + 8), yy = gy + r * (gh + 6);
      nyxSkelBox(s, gx, yy, gw, gh - 12, 6, bandX);
      nyxSkelBox(s, gx, yy + gh - 8, gw * 70 / 100, 6, 3, bandX);
    }
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
'''

def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()

def wr(p, t):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(t)

def rep(text, old, new, name):
    n = text.count(old)
    if n != 1:
        sys.exit("GAGAL: anchor '%s' ditemukan %d kali (harus 1). Tidak ada berkas yang diubah." % (name, n))
    return text.replace(old, new)

def ins_after_line(text, prefix, newline, name):
    lines = text.split("\n")
    idx = [i for i, l in enumerate(lines) if l.startswith(prefix)]
    if len(idx) != 1:
        sys.exit("GAGAL: baris '%s' ditemukan %d kali (harus 1). Tidak ada berkas yang diubah." % (name, len(idx)))
    lines.insert(idx[0] + 1, newline)
    return "\n".join(lines)

def main():
    for need in ("phone.ino", "call_renphone.ino", "ina219_renphone.ino", "about_renphone.ino",
                 "features_renphone.ino", ".github/workflows/build-firmware.yml"):
        if not os.path.exists(need):
            sys.exit("Jalankan dari root repo ren_phone (tidak ketemu %s)." % need)
    if MARK in rd("phone.ino"):
        print("Sudah terpasang (%s). Tidak ada yang diubah." % MARK)
        return

    out = {}   # path -> isi baru; ditulis SETELAH semua anchor lolos

    # ------------------------------------------------------------ phone.ino
    t = rd("phone.ino")
    t = rep(t,
        "bool callOverlayActive(); void drawCallOverlay(LGFX_Sprite&); void callOverlayTouch(bool,int,int);\n",
        "bool callOverlayActive(); void drawCallOverlay(LGFX_Sprite&); void callOverlayTouch(bool,int,int);\n"
        "// " + MARK + ": prototipe fungsi nyxextras_renphone.ino & banner telepon (call_renphone.ino)\n"
        "bool callBannerActive(); void drawCallBanner(LGFX_Sprite&); bool callBannerTouch(bool,bool,int,int);\n"
        "void nyxExtrasLoopPoll(); void nyxAboutRows(); void nyxAppEnergyAdd(float);\n"
        "void nyxDrawAppEnergy(LGFX_Sprite&,int,int); void nyxAppEnergyTouch(int,int,int,int);\n"
        "String nyxVoiceInstruction(); bool nyxVoiceExtract(String&);\n"
        "void nyxShotThumbCapture(const char*); void nyxShotThumbHide(); bool nyxShotThumbActive();\n"
        "void drawShotThumb(LGFX_Sprite&); bool nyxShotThumbTouch(bool,bool,int,int);\n"
        "void nyxSkeleton(LGFX_Sprite&,int);\n",
        "phone.ino: prototipe")
    t = rep(t,
        "  if(powerMenuOpen && !callOverlayActive()) drawPowerMenu(canvas); // menu Daya: modal di atas semuanya\n",
        "  if(!locked){ if(nyxShotThumbActive()) drawShotThumb(canvas); if(callBannerActive()) drawCallBanner(canvas); } // " + MARK + "\n"
        "  if(powerMenuOpen && !callOverlayActive()) drawPowerMenu(canvas); // menu Daya: modal di atas semuanya\n",
        "phone.ino: renderCurrentFrame")
    t = rep(t,
        "  if(callOverlayActive()){\n    callOverlayTouch(newT,tx,ty);\n",
        "  // " + MARK + ": banner telepon masuk & thumbnail screenshot menelan sentuhan yg mengenai dirinya\n"
        "  if(callBannerTouch(touched,newT,tx,ty) || nyxShotThumbTouch(touched,newT,tx,ty)){\n"
        "    if(needRedraw){ renderCurrentFrame(); push(); needRedraw=false; }\n"
        "    wasTouched=touched; delay(1); return;\n"
        "  }\n"
        "  if(callOverlayActive()){\n    callOverlayTouch(newT,tx,ty);\n",
        "phone.ino: loop touch")
    t = rep(t,
        "  featuresLoopPoll(); // Stopwatch/Timer tick, screenshot, Voice Memo, cuaca (features_renphone.ino)\n",
        "  featuresLoopPoll(); // Stopwatch/Timer tick, screenshot, Voice Memo, cuaca (features_renphone.ino)\n"
        "  nyxExtrasLoopPoll(); // " + MARK + ": log crash, energi per app, perintah suara, thumbnail SS\n",
        "phone.ino: loop poll")
    t = rep(t,
        "  for (int attempt = 1; attempt <= MAX_ATTEMPTS && !aiSoftStopTriggered; attempt++) {\n",
        "  fullPrompt = nyxVoiceInstruction() + fullPrompt; // " + MARK + ": Gemini boleh menulis baris @@CMD utk mengendalikan HP\n"
        "  for (int attempt = 1; attempt <= MAX_ATTEMPTS && !aiSoftStopTriggered; attempt++) {\n",
        "phone.ino: prompt Gemini")
    t = rep(t,
        "  aiResponse = resp;\n  if(ok) appendAiMemory(promptText, resp);",
        "  if(ok) nyxVoiceExtract(resp); // " + MARK + ": cabut @@CMD dari jawaban, eksekusi di loop utama\n"
        "  aiResponse = resp;\n  if(ok) appendAiMemory(promptText, resp);",
        "phone.ino: ekstrak CMD")
    t = rep(t,
        '  drawLoadingRing(s,SCR_W/2,SCR_H/2-6,"Mengambil data APOD dari NASA...");\n',
        "  nyxSkeleton(s,0); // " + MARK + "\n", "phone.ino: skeleton APOD")
    t = rep(t, 'drawNasaLoading(s,"Mengambil data NEO dari NASA...");',
            "nyxSkeleton(s,1);", "phone.ino: skeleton NEO")
    t = rep(t, 'drawNasaLoading(s,"Mengambil citra Bumi dari EPIC...");',
            "nyxSkeleton(s,2);", "phone.ino: skeleton EPIC")
    t = rep(t, 'drawNasaLoading(s,"Mencari di NASA Image Library...");',
            "nyxSkeleton(s,3);", "phone.ino: skeleton Galeri NASA")
    out["phone.ino"] = t

    # ------------------------------------------------------------ call_renphone.ino
    t = rd("call_renphone.ino")
    t = rep(t, "static bool callPillShown = false;\n",
        "static bool callPillShown = false;\n"
        "static bool callBanner = false;            // " + MARK + ": banner panggilan masuk (HP tidak terkunci)\n"
        "static uint32_t callBannerT0 = 0;\n"
        "static bool callBannerSupp = false;        // telan sisa sentuhan sampai jari lepas\n",
        "call: state banner")
    t = rep(t,
        "  if (ph == CPH_INCOMING && callOvl && now - callLastRingVib > 2600) {",
        "  if (ph == CPH_INCOMING && (callOvl || callBanner) && now - callLastRingVib > 2600) {",
        "call: getar dering")
    t = rep(t,
        "    } else {\n      callOvl = true; callOvlLocked = locked;\n      callMarkU(71);\n",
        "    } else {\n"
        "      if (!locked && curScreen() != SCR_CALL) {          // " + MARK + ": terbuka -> banner kecil; ketuk badan banner = layar penuh\n"
        "        callBanner = true; callBannerT0 = millis(); callBannerSupp = false;\n"
        "        callOvl = false; callOvlLocked = false;\n"
        "      } else {\n"
        "        callOvl = true; callOvlLocked = locked;\n"
        "      }\n"
        "      callMarkU(71);\n",
        "call: incoming branch")
    t = rep(t,
        "  if (ph == CPH_ACTIVE && callPrevPhase != CPH_ACTIVE) {\n    vibSuccess();\n",
        "  if (ph == CPH_ACTIVE && callPrevPhase != CPH_ACTIVE) {\n    vibSuccess();\n    callBanner = false;\n",
        "call: ACTIVE")
    t = rep(t,
        "  callPrevPhase = ph;\n  if (callEndedNew) {\n",
        "  if (callBanner && ph == CPH_INCOMING && locked) {      // layar terkunci saat berdering -> layar penuh\n"
        "    callBanner = false; callOvl = true; callOvlLocked = true; needRedraw = true;\n"
        "  }\n"
        "  if (callBanner && ph != CPH_INCOMING) { callBanner = false; needRedraw = true; }\n"
        "  callPrevPhase = ph;\n  if (callEndedNew) {\n",
        "call: banner clear")
    t = rep(t,
        "    callOvl = false; callOvlLocked = false;\n    char st[44]; callLock();",
        "    callOvl = false; callOvlLocked = false; callBanner = false;\n    char st[44]; callLock();",
        "call: ended")
    t = rep(t,
        "  bool visible = callOvl || curScreen() == SCR_CALL;\n",
        "  bool visible = callOvl || callBanner || curScreen() == SCR_CALL;\n"
        "  if (callBanner && now - callBannerT0 < 360) needRedraw = true;   // animasi banner turun\n",
        "call: visible")
    banner = r'''// ---------------------------------------------------------------------
//  BANNER PANGGILAN MASUK (""" + MARK + """)
//  HP terbuka + ada panggilan: pil kecil di atas layar dgn tombol Tolak / Angkat,
//  app yang sedang dipakai tidak terganggu. Ketuk badan banner = layar penuh.
//  Terkunci -> tetap layar penuh seperti sebelumnya.
// ---------------------------------------------------------------------
static void callBannerRect(int& x, int& y, int& w, int& h) {
  w = SCR_W - 16; h = 44; x = 8;
  uint32_t age = millis() - callBannerT0;
  float k = age >= 300 ? 1.0f : (float)age / 300.0f;
  float q = 1.0f - k;
  k = 1.0f - q * q * q;                                  // ease-out cubic
  y = (int)(-h + (h + 6) * k);                           // turun dari atas layar sampai y = 6
}

bool callBannerActive() { return callBanner; }

void drawCallBanner(LGFX_Sprite& s) {
  if (!callBanner) return;
  int x, y, w, h; callBannerRect(x, y, w, h);
  char pe[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callUnlock();
  iosRR(s, x + 1, y + 3, w, h, 14, blend565(T().bg, 0x0000, 150));          // bayangan
  iosRR(s, x, y, w, h, 14, T().surface2);
  s.drawRoundRect(x, y, w, h, 14, blend565(T().surface2, T().text, 46));    // garis tipis
  int ax = x + 8 + 13, ay = y + h / 2;
  s.fillCircle(ax, ay, 13, T().accent);
  char ini[2] = { pe[0] ? (char)toupper((unsigned char)pe[0]) : '?', 0 };
  s.setTextSize(2); s.setTextColor(T().bg); s.setCursor(ax - 5, ay - 7); s.print(ini);
  int btnW = 50, gap = 6, bx2 = x + w - 8 - btnW, bx1 = bx2 - gap - btnW;
  int tx = ax + 13 + 8;
  s.setTextSize(1);
  s.setTextColor(T().subtext); s.setCursor(tx, y + 9); s.print(callIsLan ? "Panggilan LAN" : "Panggilan masuk");
  String nm = pe; int maxC = (bx1 - tx - 4) / 6;
  if (maxC < 4) maxC = 4;
  if ((int)nm.length() > maxC) nm = nm.substring(0, maxC);
  s.setTextColor(T().text); s.setCursor(tx, y + 22); s.print(nm.c_str());
  iosRR(s, bx1, y + 9, btnW, 26, 13, T().danger);
  s.setTextColor(T().bg); s.setCursor(bx1 + (btnW - (int)s.textWidth("Tolak")) / 2, y + 18); s.print("Tolak");
  iosRR(s, bx2, y + 9, btnW, 26, 13, T().good);
  s.setTextColor(T().bg); s.setCursor(bx2 + (btnW - (int)s.textWidth("Angkat")) / 2, y + 18); s.print("Angkat");
}

// true = sentuhan ditelan banner (jangan diteruskan ke app di bawahnya)
bool callBannerTouch(bool touched, bool newT, int tx, int ty) {
  if (!callBanner) { callBannerSupp = false; return false; }
  if (callBannerSupp) { if (!touched) callBannerSupp = false; return touched; }
  if (!touched) return false;
  int x, y, w, h; callBannerRect(x, y, w, h);
  bool inside = tx >= x && tx <= x + w && ty <= y + h;
  if (!inside) return false;
  if (!newT) return true;
  int btnW = 50, gap = 6, bx2 = x + w - 8 - btnW, bx1 = bx2 - gap - btnW;
  if (tx >= bx2 - 4)      { callPost(CRQ_ANSWER, ""); callBanner = false; }
  else if (tx >= bx1 - 4) { callPost(CRQ_REJECT, ""); callBanner = false; }
  else                    { callBanner = false; callOvl = true; callOvlLocked = false; }
  callBannerSupp = true;
  vibTap();
  needRedraw = true;
  return true;
}

'''
    banner = banner.replace('""" + MARK + """', MARK)
    t = rep(t, "bool callOverlayActive() { return callOvl; }\n",
            banner + "bool callOverlayActive() { return callOvl; }\n", "call: fungsi banner")
    out["call_renphone.ino"] = t

    # ------------------------------------------------------------ ina219_renphone.ino
    t = rd("ina219_renphone.ino")
    t = rep(t, 'INA_TAB_NAME[4]  = {"Ringkas","Grafik","Sensor","Baterai"};',
               'INA_TAB_NAME[5]  = {"Ringkas","Grafik","Sensor","Baterai","Per App"};', "ina: nama tab")
    t = rep(t, "  int tw = (SCR_W - 12) / 4;\n  for (int i = 0; i < 4; i++) {\n    int x = 6 + i * tw; bool act = (i == inaTab);",
               "  int tw = (SCR_W - 12) / 5;\n  for (int i = 0; i < 5; i++) {\n    int x = 6 + i * tw; bool act = (i == inaTab);", "ina: gambar tab")
    t = rep(t, "  else inaDrawList(s, inaTab, top);\n",
               "  else if (inaTab == 4) nyxDrawAppEnergy(s, top, bottom);   // " + MARK + "\n  else inaDrawList(s, inaTab, top);\n", "ina: isi tab")
    t = rep(t, "      int tw = (SCR_W - 12) / 4; int i = (x - 6) / tw;\n      if (x >= 6 && i >= 0 && i < 4 && inaTab != (uint8_t)i)",
               "      int tw = (SCR_W - 12) / 5; int i = (x - 6) / tw;\n      if (x >= 6 && i >= 0 && i < 5 && inaTab != (uint8_t)i)", "ina: sentuh tab")
    t = rep(t, "    if (inaTab >= 2) {\n      int rh = inaRowH(inaTab)",
               "    if (inaTab == 4) { nyxAppEnergyTouch(x, y, top, inaBottom()); needRedraw = true; return; }   // " + MARK + "\n"
               "    if (inaTab >= 2) {\n      int rh = inaRowH(inaTab)", "ina: sentuh isi tab")
    t = rep(t, "  if (dMah != 0) { inaCountersDirty = true; inaUnsavedMah += fabsf(dMah); }\n",
               "  if (dMah != 0) { inaCountersDirty = true; inaUnsavedMah += fabsf(dMah); }\n"
               "  if (dMah > 0) nyxAppEnergyAdd(dMah);   // " + MARK + ": catat mAh keluar per app\n", "ina: akumulasi")
    out["ina219_renphone.ino"] = t

    # ------------------------------------------------------------ about_renphone.ino
    t = rd("about_renphone.ino")
    t = rep(t, "#define ABOUT_MAX_ROWS     24", "#define ABOUT_MAX_ROWS     32", "about: max rows")
    t = rep(t, "  aboutFmtUptime(a, sizeof(a), millis());\n  aboutAdd(\"Waktu aktif\", a);",
               "  nyxAboutRows();   // " + MARK + ": reset terakhir, layar & RAM saat crash, backtrace\n"
               "  aboutFmtUptime(a, sizeof(a), millis());\n  aboutAdd(\"Waktu aktif\", a);", "about: baris crash")
    out["about_renphone.ino"] = t

    # ------------------------------------------------------------ features_renphone.ino
    t = rd("features_renphone.ino")
    t = rep(t, '  showToast("Screenshot tersimpan di /screenshot", 1800);\n  vibSuccess();\n  return true;\n',
               "  nyxShotThumbCapture(path);   // " + MARK + ": thumbnail di pojok (ketuk = Galeri)\n"
               '  if (!nyxShotThumbActive()) { showToast("Screenshot tersimpan di /screenshot", 1800); vibSuccess(); }\n'
               "  return true;\n", "features: thumbnail")
    t = rep(t, "  shotArmedMs = 0;\n  closeControlCenter();",
               "  shotArmedMs = 0;\n  nyxShotThumbHide();   // " + MARK + ": thumbnail lama jangan ikut terfoto\n  closeControlCenter();", "features: hide thumb")
    out["features_renphone.ino"] = t

    # ------------------------------------------------------------ workflow
    wf = ".github/workflows/build-firmware.yml"
    t = rd(wf)
    t = rep(t, "          cp call_renphone.ino build_sketch/\n",
               "          cp call_renphone.ino build_sketch/\n          cp nyxextras_renphone.ino build_sketch/\n", "workflow: cp")
    out[wf] = t

    # ------------------------------------------------------------ README
    if os.path.exists("README.md"):
        t = rd("README.md")
        t = ins_after_line(t, "ptr_renphone.ino ",
            "nyxextras_renphone.ino          Log crash, energi per app, perintah suara Gemini, thumbnail screenshot, skeleton loading",
            "README: struktur")
        t = ins_after_line(t, "| Gestur sistem baru ",
            "| Fitur tambahan NyxOS (perintah suara AI, energi per app, log crash, banner telepon, thumbnail screenshot, skeleton loading) | `BETA` | Kode sudah terpasang di build, belum diuji pada perangkat. Lihat bagian 5.5 dan 5.6. |",
            "README: status")
        t = rep(t, "Memori percakapan disimpan di SD (`ai_memory.txt`). Membutuhkan `gemini_key.txt`.\n",
            "Memori percakapan disimpan di SD (`ai_memory.txt`). Membutuhkan `gemini_key.txt`.\n\n"
            "**Perintah suara `BETA`.** Gemini bisa mengendalikan HP: *buka app*, *timer*, *musik* (putar, jeda, lanjut, kembali, acak), *kecerahan*, *Home*, dan *kembali*. "
            "Di akhir jawabannya Gemini menulis baris `@@CMD {json}` (maksimal 3); baris itu dicabut dari teks yang tampil dan dijalankan oleh loop utama. "
            "Contoh: \"Hei Nyx, setel timer 10 menit\", \"buka Musik lalu putar acak\". "
            "Aksi hanya dijalankan saat HP tidak terkunci, dan hanya dari daftar aksi di atas (tidak ada hapus berkas atau matikan daya).\n",
            "README: perintah suara")
        t = rep(t, "empat tab (Ringkas, Grafik, Sensor, Baterai)", "lima tab (Ringkas, Grafik, Sensor, Baterai, Per App)", "README: tab")
        t = rep(t, '- opsi **Arah arus** untuk membalik tanda arus jika pengkabelan shunt terbalik, dan status "Mengisi" yang memicu animasi di lock screen.\n',
            '- opsi **Arah arus** untuk membalik tanda arus jika pengkabelan shunt terbalik, dan status "Mengisi" yang memicu animasi di lock screen;\n'
            '- tab **Per App** `BETA`: mAh yang keluar dari baterai dicatat per app (app yang sedang di layar; kategori *Home* dan *Terkunci* terpisah), '
            'diurutkan dari yang paling boros. Statistik tersimpan di NVS dan direset otomatis saat baterai penuh (atau lewat tombol Reset). '
            'Ini estimasi: pemakaian latar belakang (mis. musik) ikut tercatat pada app yang sedang terbuka.\n',
            "README: baterai per app")
        t = rep(t, "#### HWmonitor\n",
            "#### Log crash `BETA`\n\n"
            "Saat boot, NyxOS membaca alasan reset terakhir. Untuk reset tidak normal (panic, watchdog, brownout) satu baris ditulis ke `/crashlog.txt` di SD: "
            "waktu, alasan, layar yang sedang terbuka, sisa RAM internal (bebas, minimum, blok terbesar), lama menyala, dan, bila core dump flash aktif di build, "
            "task + alamat crash + backtrace (decode dengan `addr2line` terhadap berkas ELF dari CI). Detail terbaru tampil di **About Phone**. "
            "Berkas dirotasi di 32 KB (`/crashlog.old`).\n\n"
            "#### HWmonitor\n", "README: log crash")
        out["README.md"] = t

    # semua anchor lolos -> tulis
    wr("nyxextras_renphone.ino", NEW_INO)
    for p, c in out.items():
        wr(p, c)
    print("Berhasil. Berkas baru : nyxextras_renphone.ino")
    print("Berkas diubah        : " + ", ".join(sorted(out)))
    print("Lanjut: git add -A && git commit -m 'NyxOS: perintah suara AI, energi per app, log crash, banner telepon, thumbnail SS, skeleton' && git push")

if __name__ == "__main__":
    main()
