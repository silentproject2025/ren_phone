#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_features_patch.py -- pasang 4 fitur baru ke repo ren_phone:
  1. Screenshot (+ tombol "Tangkap" di Control Center, jadi tombol ke-11)
  2. Stopwatch & Timer (app baru)
  3. Voice Memo (app baru; playback lewat Musik BT -> butuh patch CAM)
  4. Widget Cuaca di Home (Open-Meteo)

Cara pakai (Termux, dari folder repo ren_phone):
    python3 apply_features_patch.py
Pastikan features_renphone.ino ada di folder yang sama dengan phone.ino.
Skrip ini AMAN dijalankan ulang: tiap berkas dicek dulu, yang sudah
dipatch dilewati. Kalau ada anchor yang tidak ketemu (file sudah berubah),
skrip BERHENTI tanpa menulis apa pun ke berkas itu.
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def read(p):
    with open(os.path.join(ROOT, p), "r", encoding="utf-8") as f:
        return f.read()


def write(p, s):
    with open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def rep(text, old, new, label):
    n = text.count(old)
    if n != 1:
        sys.exit("GAGAL [%s]: anchor ketemu %d kali (harus tepat 1).\n--- anchor ---\n%s" % (label, n, old))
    return text.replace(old, new, 1)


# =====================================================================
# 1. phone.ino
# =====================================================================
def patch_phone():
    p = "phone.ino"
    s = read(p)
    if "SCR_STOPWATCH" in s:
        print("phone.ino: sudah dipatch, dilewati")
        return
    if not os.path.exists(os.path.join(ROOT, "features_renphone.ino")):
        sys.exit("features_renphone.ino tidak ada di folder ini.")

    # ---- enum Screen ----
    s = rep(s, "              SCR_MUSIC };",
            "              SCR_MUSIC,\n"
            "              // Stopwatch & Timer + Voice Memo -- lihat features_renphone.ino\n"
            "              SCR_STOPWATCH, SCR_MEMO };", "enum Screen")

    # ---- forward decl + apps[] ----
    s = rep(s, "void musicLoopPoll();\n\nAppDef apps[32] = {",
            "void musicLoopPoll();\n"
            "// ---- features_renphone.ino: Stopwatch/Timer, Voice Memo, Screenshot, Cuaca ----\n"
            "void swEnter(); void swExit(); void drawSw(LGFX_Sprite&); void swTouch(int,int,bool,bool);\n"
            "void memoEnter(); void memoExit(); void drawMemo(LGFX_Sprite&); void memoTouch(int,int,bool,bool);\n"
            "void featuresLoopPoll(); void ccActScreenshot();\n"
            "void drawWeatherWidget(LGFX_Sprite&,int,int,int,int); void weatherRefreshNow();\n"
            "\nAppDef apps[34] = {", "apps decl")

    s = rep(s,
            "  { \"Musik\",      'm', 0, musicEnter,   musicExit,    drawMusic,        musicTouch,    SCR_MUSIC },\n};\n#define APP_COUNT 32",
            "  { \"Musik\",      'm', 0, musicEnter,   musicExit,    drawMusic,        musicTouch,    SCR_MUSIC },\n"
            "  // ---- Stopwatch & Timer (tetap jalan di latar belakang) ----\n"
            "  { \"Stopwatch\",  't', 0, swEnter,      swExit,       drawSw,           swTouch,       SCR_STOPWATCH },\n"
            "  // ---- Voice Memo: rekam mic ke SD, putar lewat Musik BT ----\n"
            "  { \"Voice Memo\", 'v', 0, memoEnter,    memoExit,     drawMemo,         memoTouch,     SCR_MEMO },\n"
            "};\n#define APP_COUNT 34", "apps entries")

    # ---- warna ikon ----
    s = rep(s, "  apps[31].color=0xFA12; // Musik - pink terang\n}",
            "  apps[31].color=0xFA12; // Musik - pink terang\n"
            "  apps[32].color=0xFD20; // Stopwatch - oranye\n"
            "  apps[33].color=0xF9A6; // Voice Memo - merah rekam\n}", "app colors")

    # ---- ikon app (drawAppIcon): 't' stopwatch, 'v' voice memo ----
    s = rep(s,
            "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};",
            "    case 't': { // Stopwatch: muka bulat + tombol atas + jarum\n"
            "      int rr=max(8,r-6); int fy=cy+2;\n"
            "      s.drawCircle(cx,fy,rr,ic); s.drawCircle(cx,fy,rr-1,ic);\n"
            "      s.fillRect(cx-2,fy-rr-4,5,3,ic);\n"
            "      s.drawLine(cx,fy,cx+rr/2,fy-rr/2,ic); s.drawLine(cx+1,fy,cx+rr/2+1,fy-rr/2,ic);\n"
            "      s.fillCircle(cx,fy,2,ic);\n"
            "      break;\n"
            "    }\n"
            "    case 'v': { // Voice Memo: 5 batang gelombang suara\n"
            "      int rr=max(8,r-6);\n"
            "      int hs[5]={rr/2,rr,rr*3/2,rr,rr/2};\n"
            "      int bw=3, gp=3, x0=cx-(5*bw+4*gp)/2;\n"
            "      for(int i=0;i<5;i++) s.fillRoundRect(x0+i*(bw+gp),cy-hs[i]/2,bw,max(hs[i],3),1,ic);\n"
            "      break;\n"
            "    }\n"
            "    default: {\n      s.setTextColor(ic); s.setTextSize(2);\n      char b[2]={sym,0};", "app icons")

    # ---- Control Center: 11 tombol ----
    s = rep(s,
            "#define CC_ITEMS 10 // +Bluetooth (index 9)\n"
            "int ccCols(){ return currentOrient==ORIENT_LANDSCAPE ? 5 : 3; } // landscape 5x2, portrait 3x4 -> 10 tombol muat",
            "#define CC_ITEMS 11 // +Bluetooth (index 9), +Tangkap layar (index 10)\n"
            "int ccCols(){ return currentOrient==ORIENT_LANDSCAPE ? 6 : 3; } // landscape 6x2, portrait 3x4 -> 11 tombol muat", "CC items")
    s = rep(s, "int ccGap(){ return 10; } // v108: minimalis -> lebih lega (v12: 7)",
            "int ccGap(){ return currentOrient==ORIENT_LANDSCAPE ? 6 : 10; } // landscape 6 kolom -> jarak dirapatkan biar label (\"Pesawat\") muat", "ccGap")
    s = rep(s, '"Senyap","BT"}', '"Senyap","BT","Tangkap"}', "CC label")
    s = rep(s, "ccActSilent, ccActBluetooth };", "ccActSilent, ccActBluetooth, ccActScreenshot };", "CC actions")
    s = rep(s,
            "    case 8: { // Mode Senyap: lonceng, dicoret kalau getar lg dimatikan (active=true -> getar OFF)",
            "    case 10: { // Tangkap layar: 4 sudut bingkai (viewfinder) + titik tengah\n"
            "      int a=9, b=5;\n"
            "      for(int t=0;t<2;t++){\n"
            "        s.drawFastHLine(cx-a,cy-a+t,b,ic);       s.drawFastVLine(cx-a+t,cy-a,b,ic);\n"
            "        s.drawFastHLine(cx+a-b+1,cy-a+t,b,ic);   s.drawFastVLine(cx+a-t,cy-a,b,ic);\n"
            "        s.drawFastHLine(cx-a,cy+a-t,b,ic);       s.drawFastVLine(cx-a+t,cy+a-b+1,b,ic);\n"
            "        s.drawFastHLine(cx+a-b+1,cy+a-t,b,ic);   s.drawFastVLine(cx+a-t,cy+a-b+1,b,ic);\n"
            "      }\n"
            "      s.fillCircle(cx,cy,3,ic);\n"
            "      break;\n"
            "    }\n"
            "    case 8: { // Mode Senyap: lonceng, dicoret kalau getar lg dimatikan (active=true -> getar OFF)",
            "CC icon")

    # ---- widget cuaca di Home ----
    s = rep(s,
            "int homeWidgetsEnd(){ HomeRect b=homeWBatt(), m=homeWMusic(); return max(b.y+b.h,m.y+m.h); }",
            "// Widget cuaca: baris tambahan DI BAWAH kartu musik/baterai (grid app otomatis turun\n"
            "// krn homeGridTop() memakai homeWidgetsEnd()). Data & gambar di features_renphone.ino.\n"
            "HomeRect homeWWeather(){\n"
            "  HomeRect b=homeWBatt(), m=homeWMusic(); HomeRect r;\n"
            "  r.x=10; r.w=SCR_W-20; r.y=max(b.y+b.h,m.y+m.h)+6; r.h=homeIsLand()?40:48; return r;\n"
            "}\n"
            "int homeWidgetsEnd(){ HomeRect b=homeWBatt(), m=homeWMusic(), w=homeWWeather(); return max(max(b.y+b.h,m.y+m.h),w.y+w.h); }",
            "home widget layout")
    s = rep(s,
            "    HomeRect rm=homeWMusic(); drawMusicMini(s,rm.x,rm.y-scPx,rm.w,rm.h,true);",
            "    HomeRect rm=homeWMusic(); drawMusicMini(s,rm.x,rm.y-scPx,rm.w,rm.h,true);\n"
            "    HomeRect rw=homeWWeather(); drawWeatherWidget(s,rw.x,rw.y-scPx,rw.w,rw.h);",
            "home widget draw")
    s = rep(s,
            "    if(x>=rb.x&&x<=rb.x+rb.w&&yc>=rb.y&&yc<=rb.y+rb.h) return SCR_BATTERY;",
            "    if(x>=rb.x&&x<=rb.x+rb.w&&yc>=rb.y&&yc<=rb.y+rb.h) return SCR_BATTERY;\n"
            "    HomeRect rw=homeWWeather();\n"
            "    if(x>=rw.x&&x<=rw.x+rw.w&&yc>=rw.y&&yc<=rw.y+rw.h){ weatherRefreshNow(); needRedraw=true; return SCR_HOME; }",
            "home widget touch")

    # ---- hook loop() ----
    s = rep(s,
            "  musicLoopPoll(); // Musik: toast/redraw/simpan prefs -- no-op kalau app belum pernah dibuka\n",
            "  musicLoopPoll(); // Musik: toast/redraw/simpan prefs -- no-op kalau app belum pernah dibuka\n"
            "  featuresLoopPoll(); // Stopwatch/Timer tick, screenshot, Voice Memo, cuaca (features_renphone.ino)\n",
            "loop hook")

    write(p, s)
    print("phone.ino: OK")


# =====================================================================
# 2. musicbt_renphone.ino  -- putar WAV memo lewat jalur Musik
# =====================================================================
MUS_PLAYWAV = r'''// ---- Voice Memo: putar WAV (16 kHz mono 16-bit, header 44 byte) lewat jalur yg sama ----
// WAV dikirim UTUH ke CAM; CAM mengenali "RIFF" lalu melewati decoder Helix
// dan me-resample ke 44,1 kHz stereo. Tidak menyentuh playlist/RYNE.
static void musPlayWav(const char* path) {
  if (!musCamAlive) { musToast("Modul BT tidak terdeteksi"); return; }
  if (!musBtConn)   { musToast("TWS belum terhubung"); return; }
  if (musFile) musFile.close();
  musFile = SD_MMC.open(path, FILE_READ);
  if (!musFile) { musToast("Gagal membuka memo"); return; }
  uint32_t sz = (uint32_t)musFile.size();
  if (sz <= 44) { musFile.close(); musToast("Memo kosong"); return; }
  musFile.seek(0);
  musEndCurrent(3);                              // bukukan lagu yg sedang jalan (kalau ada)
  musWavMode = true;
  musSeq++;
  uint8_t sq = musSeq;
  musCredit = 0; musSentSince = 0;
  rpSend(Serial1, RP_BEGIN, &sq, 1);
  musLoaded = true; musPaused = false; musEnded = false;
  musPlayedMs = 0; musStreaming = true; musFileDone = false;
  musDurMs = (uint32_t)(((uint64_t)(sz - 44) * 1000ULL) / 32000ULL);
  musKbps = 256; musSr = 16000;
  musCoverReady = false; musCoverWant = false;
}

'''

MUS_API = r'''

// =====================================================================
//  API Voice Memo (dipakai features_renphone.ino)
// =====================================================================
void memoPrepareBt() { musBegin(); }
void memoPlayFile(const char* path) { musBegin(); musPost('w', 0, path); }
void memoStopPlay() { if (musWavMode && musQ) musPost('s', 0, nullptr); }
bool memoPlayActive() { return musWavMode && musLoaded; }
uint32_t memoPlayPosMs() { return musPlayedMs; }
bool memoBtReady() { return musCamAlive && musBtConn; }
'''


def patch_music():
    p = "musicbt_renphone.ino"
    s = read(p)
    if "musWavMode" in s:
        print("musicbt_renphone.ino: sudah dipatch, dilewati")
        return
    s = rep(s, "static uint8_t       musSeq = 0;",
            "static uint8_t       musSeq = 0;\n"
            "static volatile bool musWavMode = false;     // true = yg sedang diputar adalah memo WAV (bukan playlist)", "musWavMode var")
    s = rep(s, "  musEndCurrent(prevHow);",
            "  musWavMode = false;                           // lagu playlist -> keluar dari mode memo\n"
            "  musEndCurrent(prevHow);", "musStartTrack")
    s = rep(s, "static void musHandleReq(uint8_t t, int32_t a, const char* s) {",
            MUS_PLAYWAV + "static void musHandleReq(uint8_t t, int32_t a, const char* s) {", "musPlayWav def")
    s = rep(s, "    case 's':\n      musEndCurrent(3);",
            "    case 'w': musPlayWav(s); break;\n"
            "    case 's':\n      musWavMode = false;\n      musEndCurrent(3);", "req w/s")
    s = rep(s, "    // ---- 5. lagu habis -> berikutnya ----",
            "    if (musEnded && musWavMode) {                 // memo selesai: berhenti, JANGAN lanjut ke playlist\n"
            "      musEnded = false; musWavMode = false; musLoaded = false; musStreaming = false;\n"
            "      if (musFile) musFile.close();\n"
            "    }\n"
            "    // ---- 5. lagu habis -> berikutnya ----", "ended wav")
    s = s.rstrip("\n") + "\n" + MUS_API
    write(p, s)
    print("musicbt_renphone.ino: OK")


# =====================================================================
# 3. cam_bt_coprocessor.ino -- mode WAV (lewati Helix)
# =====================================================================
CAM_WAV = r'''// ----------------------------------------------------------
// MODE WAV (memo suara dari S3): PCM 16-bit MONO (biasanya 16 kHz) dilewatkan
// TANPA Helix -> di-resample linear ke 44,1 kHz stereo -> langsung ke ring PCM.
// Dikenali dari header "RIFF....WAVE" di awal track (44 byte header dilewati).
// ----------------------------------------------------------
static bool     wavMode = false;
static uint32_t wavStep = 0;            // langkah fase 16.16 per sampel input = rate*65536/44100
static uint32_t wavPos = 0;             // fase 16.16 di antara wavPrev dan sampel berikutnya
static int16_t  wavPrev = 0;
static bool     wavHavePrev = false;
static uint8_t  wavOdd = 0;
static bool     wavHasOdd = false;
static int16_t  wavOut[256];            // 128 frame stereo
static int      wavOutN = 0;

static void wavReset() {
  wavMode = false; wavPos = 0; wavPrev = 0;
  wavHavePrev = false; wavHasOdd = false; wavOutN = 0;
}
static void wavFlush() {
  if (wavOutN > 0) { pcmSink.write((const uint8_t*)wavOut, (size_t)wavOutN * 2); wavOutN = 0; }
}
static void wavBegin(const uint8_t* h) {
  uint32_t rate = (uint32_t)h[24] | ((uint32_t)h[25] << 8) | ((uint32_t)h[26] << 16) | ((uint32_t)h[27] << 24);
  if (rate < 4000 || rate > 48000) rate = 16000;
  wavStep = (uint32_t)(((uint64_t)rate << 16) / 44100ULL);
  wavPos = 0; wavPrev = 0; wavHavePrev = false; wavHasOdd = false; wavOutN = 0;
  wavMode = true;
}
static void wavFeed(const uint8_t* d, uint32_t n) {
  uint32_t i = 0;
  while (i < n) {
    int16_t smp;
    if (wavHasOdd)          { smp = (int16_t)((uint16_t)wavOdd | ((uint16_t)d[i] << 8)); i++; wavHasOdd = false; }
    else if (i + 1 < n)     { smp = (int16_t)((uint16_t)d[i] | ((uint16_t)d[i + 1] << 8)); i += 2; }
    else                    { wavOdd = d[i]; wavHasOdd = true; i++; break; }
    if (!wavHavePrev) { wavPrev = smp; wavHavePrev = true; continue; }
    while (wavPos < 65536UL) {
      int32_t o = (int32_t)wavPrev + ((((int32_t)smp - (int32_t)wavPrev) * (int32_t)(wavPos >> 4)) >> 12);
      wavOut[wavOutN++] = (int16_t)o;      // L
      wavOut[wavOutN++] = (int16_t)o;      // R
      if (wavOutN >= 256) wavFlush();
      wavPos += wavStep;
    }
    wavPos -= 65536UL;
    wavPrev = smp;
    if (flushReq) break;
  }
  wavFlush();
}

'''


def patch_cam():
    p = "cam_bt_coprocessor/cam_bt_coprocessor.ino"
    s = read(p)
    if "wavMode" in s:
        print("cam_bt_coprocessor.ino: sudah dipatch, dilewati")
        return
    s = rep(s, "static void decodeTask(void* param) {", CAM_WAV + "static void decodeTask(void* param) {", "wav helpers")
    s = rep(s, "      pcmProduced = 0; inBytesTrack = 0;\n      flushReq = false;",
            "      pcmProduced = 0; inBytesTrack = 0;\n      wavReset();\n      flushReq = false;", "wav reset")
    s = rep(s, "    enc->write(buf, n);\n    inBytesTrack += n;",
            "    if (inBytesTrack == 0 && n >= 44 && memcmp(buf, \"RIFF\", 4) == 0 && memcmp(buf + 8, \"WAVE\", 4) == 0) {\n"
            "      wavBegin(buf);                    // memo WAV: lewati Helix\n"
            "      wavFeed(buf + 44, n - 44);\n"
            "    } else if (wavMode) {\n"
            "      wavFeed(buf, n);\n"
            "    } else {\n"
            "      enc->write(buf, n);\n"
            "    }\n"
            "    inBytesTrack += n;", "wav feed")
    write(p, s)
    print("cam_bt_coprocessor.ino: OK")


# =====================================================================
# 4. workflow: salin features_renphone.ino ke build_sketch
# =====================================================================
def patch_workflow():
    p = ".github/workflows/build-firmware.yml"
    s = read(p)
    if "features_renphone.ino" in s:
        print("build-firmware.yml: sudah dipatch, dilewati")
        return
    s = rep(s, "          cp musicbt_renphone.ino build_sketch/\n",
            "          cp musicbt_renphone.ino build_sketch/\n"
            "          cp features_renphone.ino build_sketch/\n", "workflow cp")
    write(p, s)
    print("build-firmware.yml: OK")


if __name__ == "__main__":
    # Semua patch dihitung dulu di memori per-berkas; tiap berkas ditulis hanya kalau seluruh anchor-nya ketemu.
    patch_phone()
    patch_music()
    patch_cam()
    patch_workflow()
    print("\nSelesai. Commit & push, lalu build lewat GitHub Actions.")
    print("PENTING: firmware CAM harus di-flash ulang (workflow build-cam-coprocessor) supaya playback memo jalan.")
