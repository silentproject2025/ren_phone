#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_shot_thumb_v4.py -- lanjutan apply_shot_thumb_strip.py (V3). Tiga perbaikan:

 1. JAM ANIMASI MULAI DI FRAME PERTAMA. Sebelumnya jam mulai tepat setelah bake,
    padahal frame penuh pertama (render UI + push) masih ~50-100 ms -> awal
    animasi "termakan", thumbnail langsung melompat. Sekarang jam baru jalan
    saat frame pertama benar-benar digambar.
 2. PEMBATAS LAJU (slew limiter). Posisi tampil maju maksimal 14 px per frame
    menuju posisi target berbasis waktu. Frame telat tidak lagi membuat loncatan.
 3. BEKUKAN FRAME UI SELAMA ANIMASI. Di layar yang selalu minta redraw (Home dgn
    widget, jam, dll) frame penuh terjadi tiap putaran loop, jadi jalur strip
    tidak pernah dipakai. Selama thumbnail bergerak (dan jari tidak menyentuh
    layar), render+push UI dilewati; redraw yang tertunda dijalankan begitu
    animasi selesai.

Jalankan dari root repo setelah apply_shot_thumb_strip.py. Idempotent.
"""
import sys, os
MARK = "NYX_TH_V4"
NYX, PH = "nyxextras_renphone.ino", "phone.ino"
for f in (NYX, PH):
    if not os.path.exists(f): sys.exit("ERROR: %s tidak ketemu" % f)

def load(p):
    s = open(p, encoding="utf-8", newline="").read()
    c = "\r\n" in s
    return (s.replace("\r\n", "\n") if c else s), c
def save(p, s, c):
    open(p, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if c else s)

s, c = load(NYX)
if "NYX_TH_STRIP_V3" not in s: sys.exit("ERROR: pasang apply_shot_thumb_strip.py dulu")
if MARK in s:
    print("[skip] %s sudah terpasang" % MARK)
else:
    def rep(old, new, what):
        global s
        if s.count(old) != 1: sys.exit("ERROR: %s ditemukan %d kali" % (what, s.count(old)))
        s = s.replace(old, new)

    rep("static uint32_t nyxThSig = 0;",
        "static uint32_t nyxThSig = 0;\n"
        "static bool     nyxThStarted = false;          // " + MARK + ": jam animasi baru jalan di frame pertama\n"
        "static int      nyxThShownX = 0;               // posisi x yang benar-benar tampil (dibatasi lajunya)\n"
        "static bool     nyxThDeferred = false;         // ada redraw UI yang ditunda selama animasi\n"
        "#define NYX_TH_MAX_STEP 14                     // px maksimum per frame", "var status")
    rep("  nyxThBgValid = false; nyxThDispVis = false; nyxThFastWant = false;\n  nyxThOn = false;",
        "  nyxThBgValid = false; nyxThDispVis = false; nyxThFastWant = false;\n"
        "  nyxThStarted = false; nyxThDeferred = false;\n  nyxThOn = false;", "reset")
    # pembatas laju
    rep("static int nyxThCurY() {",
        "// maju satu langkah menuju target berbasis waktu, maksimal NYX_TH_MAX_STEP px per frame\n"
        "static void nyxThStepX() {\n"
        "  int d = nyxThCurX() - nyxThShownX;\n"
        "  if (d > NYX_TH_MAX_STEP) d = NYX_TH_MAX_STEP; else if (d < -NYX_TH_MAX_STEP) d = -NYX_TH_MAX_STEP;\n"
        "  nyxThShownX += d;\n}\n"
        "static int nyxThCurY() {", "step")
    # frame penuh
    rep("  nyxThFullN++;\n  nyxThCaptureBg(s);",
        "  nyxThFullN++;\n"
        "  if (!nyxThStarted) { nyxThStarted = true; nyxThT0 = millis(); nyxThShownX = -(nyxThW + 12); }\n"
        "  nyxThStepX();\n  nyxThCaptureBg(s);", "frame penuh")
    rep("  nyxThBgValid = (&s == &canvas) && !homeBandMode;   // mode pita: canvas di luar pita belum tentu bersih\n  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;",
        "  nyxThBgValid = (&s == &canvas) && !homeBandMode;   // mode pita: canvas di luar pita belum tentu bersih\n  int fl = nyxThShownX - NYX_TH_BORDER - nyxThBx;", "fl penuh")
    # frame cepat
    rep("  uiBoostKick();\n  int fl = nyxThCurX() - NYX_TH_BORDER - nyxThBx;",
        "  uiBoostKick();\n  nyxThStepX();\n  int fl = nyxThShownX - NYX_TH_BORDER - nyxThBx;", "fl cepat")
    # animating
    rep("  if (nyxThOutT0) return true;\n  return (millis() - nyxThT0) < (uint32_t)(NYX_TH_IN_MS + 80);\n}",
        "  if (!nyxThStarted || nyxThOutT0) return true;\n"
        "  return nyxThShownX != nyxThCurX() || (millis() - nyxThT0) < (uint32_t)(NYX_TH_IN_MS + 80);\n}\n\n"
        "// true = lewati render+push UI ini (dipanggil dari renderCurrentFrame() & push())\n"
        "bool nyxThFreezeFrame() {\n"
        "  if (!nyxThCard || !nyxThStarted || wasTouched) return false;\n"
        "  if (!nyxShotThumbAnimating() || !nyxThFastOk()) return false;\n"
        "  nyxThDeferred = true;\n  return true;\n}", "animating+freeze")
    # sentuh pakai posisi tampil
    rep("  int x = nyxThCurX(), y = nyxThCurY();", "  int x = nyxThShownX, y = nyxThCurY();", "touch")
    # tick
    t0 = s.index("static void nyxThumbTick() {")
    t1 = s.index("// =====================================================================\n//  5. SKELETON LOADING")
    s = s[:t0] + r'''static void nyxThumbTick() {
  if (!nyxThCard) return;
  if (locked) { nyxThFree(); return; }
  if (SCR_W != nyxThScrW || SCR_H != nyxThScrH) { nyxThFree(); needRedraw = true; return; }   // rotasi berubah
  if (!nyxThStarted) { needRedraw = true; return; }     // tunggu frame penuh pertama (jam animasi mulai di sana)
  uint32_t now = millis();
  bool reached = (nyxThShownX == nyxThCurX());
  if (nyxThOutT0) {
    bool up = (now - nyxThOutT0 >= NYX_TH_OUT_MS);
    if (up && (reached || now - nyxThOutT0 >= NYX_TH_OUT_MS + 250)) { nyxThFree(); needRedraw = true; }
    else nyxThAnimStep();
    return;
  }
  if (!reached || now - nyxThT0 < NYX_TH_IN_MS + 40) { nyxThAnimStep(); return; }
  if (nyxThDeferred) { nyxThDeferred = false; needRedraw = true; }       // jalankan redraw UI yang tertunda
  if (!nyxThDown && now - nyxThT0 > NYX_TH_IN_MS + NYX_TH_HOLD_MS) { nyxThOutT0 = now; nyxThAnimStep(); }
}

''' + s[t1:]
    save(NYX, s, c)
    print("[ok] %s" % NYX)

p, pc = load(PH)
if MARK in p:
    print("[skip] %s" % PH)
else:
    def prep(old, new, what):
        global p
        if p.count(old) != 1: sys.exit("ERROR: %s ditemukan %d kali di %s" % (what, p.count(old), PH))
        p = p.replace(old, new)
    prep("bool nyxShotThumbAnimating(); void nyxShotThumbFastFrame();",
         "bool nyxShotThumbAnimating(); void nyxShotThumbFastFrame(); bool nyxThFreezeFrame();   // " + MARK + "\n//", "proto")
    prep("void push(){ unsigned long t0=micros();", "void push(){ if(nyxThFreezeFrame()) return; unsigned long t0=micros();", "push")
    prep("void renderCurrentFrame(){\n", "void renderCurrentFrame(){\n  if(nyxThFreezeFrame()) return;   // " + MARK + "\n", "render")
    save(PH, p, pc)
    print("[ok] %s" % PH)
