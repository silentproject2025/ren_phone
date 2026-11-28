// theme_renphone.ino -- app bawaan "Wallpaper": wallpaper acak dari picsum.photos.   // THEME_BUILTIN_V1
//
// Pengganti app ELF "Theme". Alur: tombol New -> task FreeRTOS mengunduh JPG (sesuai ukuran layar) ke buffer
// PSRAM -> loop() menulisnya ke /wallpaper1.jpg (potret) atau /wallpaper2.jpg (lanskap) di SD (FFat bila tidak
// ada SD), lalu memanggil loadWallpaper(). Home berubah langsung; layar app ini ikut menampilkan wallpaper baru.
//
// ATURAN PENTING: task ber-stack PSRAM DILARANG menyentuh flash (FFat) -> assert esp_task_stack_is_sane_cache_disabled
// (lihat musfs.h). Karena itu task HANYA mengunduh ke RAM; tulis file + loadWallpaper() dikerjakan di konteks loop()
// lewat thLoopTick(). Tidak ada tipe buatan sendiri di signature fungsi (prototipe otomatis arduino-cli).

#define TH_BUF     (160 * 1024)   // batas ukuran JPG (480x320 dari Picsum biasanya 30-70 KB)
#define TH_IDLE    0
#define TH_LOADING 1
#define TH_OK      2
#define TH_ERR     3

static volatile uint8_t  thState = TH_IDLE;
static volatile bool     thApply = false;      // task selesai mengunduh -> loop() menyimpan + memuat wallpaper
static volatile uint32_t thLen   = 0;
static uint8_t*          thBuf   = nullptr;
static uint8_t           thStyle = 0;          // 0 normal, 1 hitam-putih, 2 blur
static uint32_t          thSeed  = 0;
static char              thMsg[32] = "";
static TaskHandle_t      thTaskH = NULL;
static StackType_t*      thStack = nullptr; static StaticTask_t thTcb;
static const char* const TH_STYLE[3] = { "Normal", "Black & white", "Blur" };
static const char* const TH_QS[3]    = { "", "&grayscale", "&blur=2" };

static void thSetErr(const char* m){
  strncpy(thMsg, m, sizeof(thMsg) - 1); thMsg[sizeof(thMsg) - 1] = 0;
  thState = TH_ERR;                             // SELALU langkah terakhir task (setelah itu buffer boleh dibebaskan loop)
}

// 1 = JPEG baseline, 2 = progresif (JPEGDEC tidak mendukung), 0 = bukan JPEG
static int thJpegKind(const uint8_t* b, int n){
  int p = 2;
  if(n < 4 || b[0] != 0xFF || b[1] != 0xD8) return 0;
  while(p + 4 <= n){
    int m;
    if(b[p] != 0xFF){ p++; continue; }
    m = b[p + 1];
    if(m == 0xFF){ p++; continue; }
    if(m == 0 || m == 0x01 || m == 0xD8 || (m >= 0xD0 && m <= 0xD7)){ p += 2; continue; }
    if(m == 0xC0 || m == 0xC1) return 1;
    if(m == 0xC2) return 2;
    if(m == 0xDA) return 0;
    p += 2 + ((b[p + 2] << 8) | b[p + 3]);
  }
  return 0;
}

// ---- unduh (jalan di task ber-stack PSRAM: JANGAN sentuh SD/FFat di sini) ----
static void thRun(){
  char url[104];
  snprintf(url, sizeof(url), "https://picsum.photos/%d/%d.jpg?random=%lu%s",
           SCR_W, SCR_H, (unsigned long)thSeed, TH_QS[thStyle]);
  WiFiClientSecure sc; HTTPClient http;
  sc.setInsecure(); sc.setTimeout(15000);
  if(!http.begin(sc, url)){ thSetErr("Connect failed"); return; }
  http.setTimeout(15000); http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // picsum.photos membalas 302
  int code = rpHttpGetRetry(http, sc, String(url), 15000, true);   // NET_ADAPTIF_V1: retry kegagalan koneksi
  if(code != HTTP_CODE_OK){
    char m[20]; snprintf(m, sizeof(m), "HTTP %d", code);
    http.end(); thSetErr(m); return;
  }
  int len = http.getSize();
  if(len > TH_BUF){ http.end(); thSetErr("Image too big"); return; }
  WiFiClient* st = http.getStreamPtr();
  size_t n = 0; unsigned long t0 = millis();
  while(st && (http.connected() || st->available()) && n < (size_t)TH_BUF && millis() - t0 < 15000UL){
    int av = st->available();
    if(av > 0){
      size_t room = (size_t)TH_BUF - n, want = (size_t)av < room ? (size_t)av : room;
      int rd = st->readBytes(thBuf + n, want);
      if(rd > 0) n += (size_t)rd;
    } else delay(5);
  }
  http.end();
  if(n == 0 || (len > 0 && n != (size_t)len)){ thSetErr("Download incomplete"); return; }
  if(n >= (size_t)TH_BUF){ thSetErr("Image too big"); return; }
  int kind = thJpegKind(thBuf, (int)n);
  if(kind == 0){ thSetErr("Not a valid JPEG"); return; }
  if(kind == 2){ thSetErr("Progressive JPEG, retry"); return; }
  thLen = (uint32_t)n;
  thApply = true;                               // buffer lengkap -> serahkan ke loop()
}
static void thTaskFunc(void*){
  thRun();
  thTaskH = NULL;
  vTaskDelete(NULL);
}

// ---- simpan + terapkan (konteks loop()) ----
static void thRm(const char* p){
  if(sdReady) SD_MMC.remove(p);
  else if(ffatReady) FFat.remove(p);
}
static void thSave(){
  bool ok = false; const char* why = "Save failed";
  if(thBuf && thLen > 0 && (sdReady || ffatReady)){
    const char* dst = (SCR_W >= SCR_H) ? WALLPAPER_FILE_L : WALLPAPER_FILE_P;   // sama dgn wallpaperResolve()
    File f;
    if(sdReady) f = SD_MMC.open(dst, FILE_WRITE); else f = FFat.open(dst, FILE_WRITE);
    if(f){
      size_t w = f.write(thBuf, thLen); f.close();
      ok = (w == (size_t)thLen);
      if(!ok) thRm(dst);
    }
    if(ok){
      thRm("/apps/theme/data/wallpaper1.jpg");   // sisa app ELF lama: kalau ada, ia menang atas file di root
      thRm("/apps/theme/data/wallpaper2.jpg");
      ok = loadWallpaper();
      if(!ok) why = "Decode failed";
    }
  } else if(!sdReady && !ffatReady) why = "No SD / flash storage";
  if(thBuf){ heap_caps_free(thBuf); thBuf = nullptr; }
  thLen = 0;
  if(ok){ thState = TH_OK; showToast("Wallpaper applied"); }
  else { thSetErr(why); vibError(); }
  needRedraw = true;
}
static void thReset(){
  thRm("/wallpaper1.jpg"); thRm("/wallpaper2.jpg");
  thRm("/apps/theme/data/wallpaper1.jpg"); thRm("/apps/theme/data/wallpaper2.jpg");
  loadWallpaper();                               // tidak ada sumber -> Home kembali ke warna solid tema
  thState = TH_IDLE; thMsg[0] = 0;
  showToast("Wallpaper removed"); needRedraw = true;
}

// dipanggil dari loop() tiap putaran (murah): simpan hasil unduhan, bebaskan buffer saat gagal, animasi loading
void thLoopTick(){
  if(thApply){ thApply = false; thSave(); }
  else if(thState == TH_ERR && thBuf){ heap_caps_free(thBuf); thBuf = nullptr; }
  if(thState == TH_LOADING && curScreen() == SCR_THEME) needRedraw = true;
}

static void thStart(){
  if(thState == TH_LOADING) return;
  if(WiFi.status() != WL_CONNECTED){ showToast("WiFi not connected"); vibError(); return; }
  if(!sdReady && !ffatReady){ showToast("No SD / flash storage"); vibError(); return; }
  if(!thBuf) thBuf = (uint8_t*)heap_caps_malloc(TH_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if(!thBuf){ thSetErr("Out of PSRAM"); needRedraw = true; return; }
  nasaHeapReadyForTask();                        // lihat catatan "v63 FIX": cek/bersihkan RAM internal SEBELUM task dibuat
  thSeed = esp_random() % 1000000UL; thLen = 0; thApply = false; thMsg[0] = 0;
  thState = TH_LOADING;
  BaseType_t r = createTaskPsramStack(thTaskFunc, "thTask", 16384, NULL, &thStack, &thTcb, &thTaskH, 1);
  if(r != pdPASS){ thSetErr("Task failed"); vibError(); }
  needRedraw = true;
}

// ---- UI ----
// geometri dipakai bareng oleh gambar DAN hit-test
static void thGeo(int* styleY, int* rowY, int* bh){
  *bh = 32; *rowY = SCR_H - BACK_H - 9 - *bh; *styleY = *rowY - *bh - 8;
}
static void thBtn(LGFX_Sprite& s, int x, int y, int w, int h, const char* t, bool primary, bool on){
  if(primary) nyxAaRR(s, x, y, w, h, h / 2, on ? T().accent : T().divider);
  else drawGlassCard(s, x, y, w, h, h / 2, T().surface2, 190);
  s.setTextSize(1);
  s.setTextColor(!on ? T().subtext : (primary ? T().bg : T().text));
  s.setCursor(x + (w - s.textWidth(t)) / 2, y + (h - 8) / 2); s.print(t);
}

void thEnter(){ if(thState != TH_LOADING) thState = TH_IDLE; thMsg[0] = 0; }
void thExit(){}   // unduhan yang sedang jalan tetap diselesaikan & diterapkan oleh thLoopTick()

void drawTheme(LGFX_Sprite& s){
  if(wallpaperReady) wallpaperImg.pushSprite(&s, 0, 0); else iosBackdrop(s);   // wallpaper baru langsung terlihat sbg pratinjau
  drawStatusBar(s);
  int cw = SCR_W - 16, sy, ry, bh; thGeo(&sy, &ry, &bh);
  drawGlassPanel(s, 8, 30, cw, 72, 14, T().surface, 190);
  iosTitle(s, 20, 40, "Wallpaper");
  char ln[48]; snprintf(ln, sizeof(ln), "picsum.photos  %dx%d", SCR_W, SCR_H);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(20, 56); s.print(ln);
  uint8_t st = thState;
  if(st == TH_LOADING){
    int d = (int)((millis() / 350UL) % 4UL);
    snprintf(ln, sizeof(ln), "Downloading%s", "..." + (3 - d));
    s.setTextColor(T().accent);
  } else if(st == TH_OK){
    snprintf(ln, sizeof(ln), "Applied  #%lu", (unsigned long)thSeed); s.setTextColor(T().good);
  } else if(st == TH_ERR){
    snprintf(ln, sizeof(ln), "%s", thMsg); s.setTextColor(T().danger);
  } else {
    snprintf(ln, sizeof(ln), "Tap New for a random photo"); s.setTextColor(T().text);
  }
  s.setCursor(20, 76); s.print(ln);

  bool busy = (st == TH_LOADING);
  int nw = cw * 62 / 100, rx = 8 + nw + 8, rw = cw - nw - 8;
  char sl[40]; snprintf(sl, sizeof(sl), "Style: %s", TH_STYLE[thStyle]);
  thBtn(s, 8, sy, cw, bh, sl, false, !busy);
  thBtn(s, 8, ry, nw, bh, "New", true, !busy);
  thBtn(s, rx, ry, rw, bh, "Reset", false, !busy);
  drawBack(s); drawToast(s);
}

void themeTouch(int x, int y, bool held, bool isNew){
  if(!isNew) return;
  if(isBack(x, y)){ navBack(); return; }
  int cw = SCR_W - 16, sy, ry, bh; thGeo(&sy, &ry, &bh);
  int nw = cw * 62 / 100, rx = 8 + nw + 8, rw = cw - nw - 8;
  if(thState == TH_LOADING) return;
  if(y >= sy && y < sy + bh && x >= 8 && x < 8 + cw){ thStyle = (uint8_t)((thStyle + 1) % 3); vibTap(); needRedraw = true; return; }
  if(y >= ry && y < ry + bh){
    if(x >= 8 && x < 8 + nw){ vibTap(); thStart(); return; }
    if(x >= rx && x < rx + rw){ vibTap(); thReset(); return; }
  }
}
