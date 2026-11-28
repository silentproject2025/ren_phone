#!/usr/bin/env python3
"""
apply_theme_builtin.py -- app bawaan "Wallpaper" (wallpaper acak dari picsum.photos), pengganti app ELF "Theme".

Pakai (dari ROOT repo):
    python3 apply_theme_builtin.py            # pasang
    python3 apply_theme_builtin.py --check    # hanya cek anchor, tidak mengubah apa pun

Yang dikerjakan (idempoten & atomik: satu anchor gagal -> tidak ada file yang diubah):
  1. firmware/phone/theme_renphone.ino : file baru, seluruh logika app (unduh, simpan, terapkan).
  2. firmware/phone/phone.ino          : enum SCR_THEME, prototipe, entri apps[], APP_COUNT 47 -> 48,
                                         app disembunyikan dari grid Home, hook thLoopTick() di loop(),
                                         baris "Wallpaper" di Setting > Tampilan.
  3. .github/workflows/build-firmware.yml : cp theme_renphone.ino ke build_sketch.

Desain: task ber-stack PSRAM DILARANG menyentuh flash (FFat) -> assert esp_task_stack_is_sane_cache_disabled
(lihat musfs.h). Jadi task HANYA mengunduh ke buffer PSRAM; menulis file dan loadWallpaper() dikerjakan di
konteks loop() lewat thLoopTick().
"""
import os
import sys

TAG = "THEME_BUILTIN_V1"
PHONE = os.path.join("firmware", "phone", "phone.ino")
NEWINO = os.path.join("firmware", "phone", "theme_renphone.ino")
WF = os.path.join(".github", "workflows", "build-firmware.yml")

# ---------------------------------------------------------------------------------------------------
# File baru: theme_renphone.ino
# ---------------------------------------------------------------------------------------------------
THEME_INO = r'''// theme_renphone.ino -- app bawaan "Wallpaper": wallpaper acak dari picsum.photos.   // THEME_BUILTIN_V1
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
'''

# ---------------------------------------------------------------------------------------------------
# Edit phone.ino : (nama, anchor lama, pengganti)  -- anchor HARUS persis 1 kali
# ---------------------------------------------------------------------------------------------------
PHONE_EDITS = [
    ("enum Screen",
     "              SCR_DEVOPT };\nenum Orientation { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1 };",
     "              SCR_DEVOPT,\n"
     "              // Wallpaper acak dari picsum.photos (pengganti app ELF Theme) -- lihat theme_renphone.ino  // THEME_BUILTIN_V1\n"
     "              SCR_THEME };\nenum Orientation { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1 };"),
    ("prototipe",
     "// ---- backup_renphone.ino: Backup & Restore pengaturan ----\nvoid backupEnter();",
     "// ---- theme_renphone.ino: Wallpaper acak (Picsum)  // THEME_BUILTIN_V1 ----\n"
     "void thEnter(); void thExit(); void drawTheme(LGFX_Sprite&); void themeTouch(int,int,bool,bool); void thLoopTick();\n"
     "// ---- backup_renphone.ino: Backup & Restore pengaturan ----\nvoid backupEnter();"),
    ("entri apps[]",
     "  { \"Opsi Developer\", 'd', 0, devoptEnter, devoptExit, drawDevopt, devoptTouch, SCR_DEVOPT },\n};",
     "  { \"Opsi Developer\", 'd', 0, devoptEnter, devoptExit, drawDevopt, devoptTouch, SCR_DEVOPT },\n"
     "  // ---- Wallpaper: ganti wallpaper Home dari Picsum (disembunyikan dari grid; ada di Setting > Tampilan) ----  // THEME_BUILTIN_V1\n"
     "  { \"Wallpaper\", 'W', 0, thEnter, thExit, drawTheme, themeTouch, SCR_THEME },\n};"),
    ("APP_COUNT",
     "#define APP_COUNT (47 - RP_NASA_N)",
     "#define APP_COUNT (48 - RP_NASA_N)"),
    ("sembunyi di Home",
     "homeHide[i]=(apps[i].screen==SCR_HOMEEDIT || apps[i].screen==SCR_BACKUP || apps[i].screen==SCR_DEVOPT); }",
     "homeHide[i]=(apps[i].screen==SCR_HOMEEDIT || apps[i].screen==SCR_BACKUP || apps[i].screen==SCR_DEVOPT || apps[i].screen==SCR_THEME); }   // THEME_BUILTIN_V1"),
    ("hook loop()",
     "    if(pomoRunning) pomoTick();\n",
     "    if(pomoRunning) pomoTick();\n    thLoopTick();   // THEME_BUILTIN_V1: simpan/terapkan unduhan wallpaper, animasi loading\n"),
    ("Setting: enum SR_",
     "SR_GAP /* UI-REVAMP */, SR_DEVOPT /* DEVOPT_IN_SETTINGS_V1 */ };",
     "SR_GAP /* UI-REVAMP */, SR_DEVOPT /* DEVOPT_IN_SETTINGS_V1 */, SR_WALL /* THEME_BUILTIN_V1 */ };"),
    ("Setting: kata kunci cari",
     "    case SR_HOMEEDIT: return \"home layar atur edit\";\n",
     "    case SR_HOMEEDIT: return \"home layar atur edit\";\n    case SR_WALL:     return \"wallpaper latar belakang tema theme picsum foto gambar\";   // THEME_BUILTIN_V1\n"),
    ("Setting: baris di Tampilan",
     "SADD(SR_ROT,34); SADD(SR_HOMEEDIT,34);",
     "SADD(SR_ROT,34); SADD(SR_HOMEEDIT,34); SADD(SR_WALL,34);"),
    ("Setting: pressable",
     "    case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_HEAPLOG: case SR_SPIAPPLY:\n",
     "    case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_HEAPLOG: case SR_SPIAPPLY: case SR_WALL:\n"),
    ("Setting: chip ada",
     "    case SR_BRIGHT: case SR_THEME: case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_SPI:\n",
     "    case SR_BRIGHT: case SR_THEME: case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_SPI: case SR_WALL:\n"),
    ("Setting: warna chip",
     "    case SR_HOMEEDIT: col=CHIP_PURPLE; break;\n",
     "    case SR_HOMEEDIT: col=CHIP_PURPLE; break;\n    case SR_WALL:     col=CHIP_PINK;   break;   // THEME_BUILTIN_V1\n"),
    ("Setting: ikon",
     "      nyxAaRR(s, cx-6,cy+1,5,5,1,W); nyxAaRR(s, cx+1,cy+1,5,5,1,W);\n      break;\n",
     "      nyxAaRR(s, cx-6,cy+1,5,5,1,W); nyxAaRR(s, cx+1,cy+1,5,5,1,W);\n      break;\n"
     "    case SR_WALL:      // THEME_BUILTIN_V1: bingkai foto + gunung + matahari\n"
     "      nyxAaRRRing(s, cx-8,cy-6,16,12,2,W); s.fillTriangle(cx-6,cy+4,cx-1,cy-2,cx+4,cy+4,W); nyxAaCircle(s, cx+4,cy-2,2,W);\n"
     "      break;\n"),
    ("Setting: gambar baris",
     "        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR(\"Atur Layar Home\"));\n        settChevron(s,xr-4,ry+rh/2,T().subtext);\n        break;\n",
     "        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR(\"Atur Layar Home\"));\n        settChevron(s,xr-4,ry+rh/2,T().subtext);\n        break;\n"
     "      case SR_WALL:   // THEME_BUILTIN_V1\n"
     "        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(\"Wallpaper\");\n        settChevron(s,xr-4,ry+rh/2,T().subtext);\n        break;\n"),
    ("Setting: ketukan",
     "    case SR_HOMEEDIT:\n      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_HOMEEDIT); break;\n",
     "    case SR_HOMEEDIT:\n      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_HOMEEDIT); break;\n"
     "    case SR_WALL:   // THEME_BUILTIN_V1\n      settKeepPage=true; settKeepScroll=settScroll; navPush(SCR_THEME); break;\n"),
]

WF_EDITS = [
    ("cp theme_renphone.ino",
     "          cp firmware/phone/nyxextras_renphone.ino build_sketch/\n",
     "          cp firmware/phone/nyxextras_renphone.ino build_sketch/\n"
     "          cp firmware/phone/theme_renphone.ino build_sketch/     # THEME_BUILTIN_V1: app Wallpaper (Picsum)\n"),
]

# penanda "sudah terpasang" per edit (kalau ada di berkas -> dilewati)
DONE_MARK = {
    "enum Screen": "SCR_THEME };",
    "prototipe": "void thEnter(); void thExit();",
    "entri apps[]": "thEnter, thExit, drawTheme, themeTouch, SCR_THEME",
    "APP_COUNT": "#define APP_COUNT (48 - RP_NASA_N)",
    "sembunyi di Home": "apps[i].screen==SCR_THEME); }",
    "hook loop()": "thLoopTick();   // THEME_BUILTIN_V1",
    "Setting: enum SR_": "SR_WALL /* THEME_BUILTIN_V1 */",
    "Setting: kata kunci cari": "case SR_WALL:     return",
    "Setting: baris di Tampilan": "SADD(SR_WALL,34)",
    "Setting: pressable": "case SR_SPIAPPLY: case SR_WALL:",
    "Setting: chip ada": "case SR_SPI: case SR_WALL:",
    "Setting: warna chip": "case SR_WALL:     col=CHIP_PINK;",
    "Setting: ikon": "case SR_WALL:      // THEME_BUILTIN_V1: bingkai",
    "Setting: gambar baris": "case SR_WALL:   // THEME_BUILTIN_V1\n        s.setTextColor",
    "Setting: ketukan": "navPush(SCR_THEME); break;",
    "cp theme_renphone.ino": "theme_renphone.ino build_sketch/",
}


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def plan(path, edits):
    """Kembalikan teks baru untuk path, atau exit bila anchor tidak pas."""
    if not os.path.exists(path):
        sys.exit("tidak ada: %s (jalankan dari ROOT repo)" % path)
    s = read(path)
    for name, old, new in edits:
        mark = DONE_MARK[name]
        if mark in s:
            print("  sudah ada :", name)
            continue
        n = s.count(old)
        if n != 1:
            sys.exit("GAGAL: anchor '%s' ditemukan %d kali di %s (harus 1). Tidak ada yang diubah." % (name, n, path))
        s = s.replace(old, new)
        print("  ok        :", name)
    return s


def main():
    check = "--check" in sys.argv
    print("== phone.ino")
    new_phone = plan(PHONE, PHONE_EDITS)
    new_wf = None
    if os.path.exists(WF):
        print("== build-firmware.yml")
        new_wf = plan(WF, WF_EDITS)
    else:
        print("== build-firmware.yml tidak ditemukan -> tambahkan sendiri: cp firmware/phone/theme_renphone.ino build_sketch/")
    print("== theme_renphone.ino")
    have = os.path.exists(NEWINO) and read(NEWINO) == THEME_INO
    print("  sudah ada :" if have else "  ok        : (tulis berkas baru)", NEWINO)
    if check:
        print("--check: semua anchor cocok. Tidak ada yang diubah.")
        return
    for path, txt in ((PHONE, new_phone), (WF, new_wf)):
        if txt is None:
            continue
        if txt != read(path):
            bak = path + ".bak_themebi"
            if not os.path.exists(bak):
                with open(bak, "w", encoding="utf-8") as fh:
                    fh.write(read(path))
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(txt)
    if not have:
        with open(NEWINO, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(THEME_INO)
    print("Selesai. Build firmware; app 'Wallpaper' ada di Setting > Tampilan > Wallpaper.")


if __name__ == "__main__":
    main()
