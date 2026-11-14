#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_home_smooth_v3.py (HOME_SMOOTH_V3) -- scroll Home lebih halus, tidak "sendat".

Yang diubah (hanya jalur scroll Home di phone.ino):

 1. Filter anti-jitter posisi jari saat drag. Sentuh resistif (XPT2046) bergetar
    +-1..3 px, dan sejak TOUCH_SIMPLE_V6 tidak ada filter sama sekali -> konten
    ikut bergetar saat digeser pelan. Sekarang jari difilter adaptif: pelan =
    diredam, cepat = langsung ikut (hampir tanpa lag).
 2. Lewati frame yg tidak mengubah posisi (piksel bulat sama). Dulu glide/pegas
    me-render + push SPI penuh tiap putaran loop walau konten diam di piksel
    yg sama (terutama di ekor glide & saat pegas hampir berhenti). Frame sia-sia
    itu memperlambat polling sentuh dan membuat langkah scroll tidak rata.
 3. Batas kecepatan lempar 3.0 -> 2.4 px/ms. Di frame rate ~25-30 fps, 3 px/ms
    = lompatan 100+ px per frame (terlihat teleport/sendat). 2.4 px/ms memangkas
    lompatan terbesar ~20% tanpa mengubah rasa lempar biasa.

Jalankan dari root repo. Idempotent.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")
if "HOME_SMOOTH_V3" in s:
    print("[skip] sudah dipatch"); sys.exit(0)


def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: blok '%s' ketemu %d kali (harus 1). Dibatalkan, phone.ino tidak diubah." % (label, n))
    s = s.replace(old, new)


# ---- 1a. variabel global filter jari ----
rep(
    "float homeDragRaw=0;          // v105: posisi \"mentah\" ikut jari 1:1 (belum kena rubber-band)\n",
    "float homeDragRaw=0;          // v105: posisi \"mentah\" ikut jari 1:1 (belum kena rubber-band)\n"
    "float homeTyF=0;              // HOME_SMOOTH_V3: Y jari setelah filter anti-jitter\n"
    "float homeTyLastF=0;          // HOME_SMOOTH_V3: Y jari terfilter pada iterasi sebelumnya\n",
    "global homeDragRaw")

# ---- 3. batas kecepatan lempar ----
rep(
    "const float HOME_FLING_MAX     = 3.0f;    // px/ms, batas kecepatan lempar\n",
    "const float HOME_FLING_MAX     = 2.4f;    // px/ms, batas kecepatan lempar (HOME_SMOOTH_V3: dulu 3.0 -> lompatan per frame terlalu besar)\n",
    "HOME_FLING_MAX")

# ---- 1b. jari baru nempel: reset filter ----
rep(
    "        homeDragAccum=0; homeDragSampleMs=millis();\n"
    "        homePressedIdx = homeJiggle ? -1 : homeIndexAt(tx,ty,homeScrollY);",
    "        homeDragAccum=0; homeDragSampleMs=millis();\n"
    "        homeTyF=(float)ty; homeTyLastF=(float)ty; // HOME_SMOOTH_V3\n"
    "        homePressedIdx = homeJiggle ? -1 : homeIndexAt(tx,ty,homeScrollY);",
    "touch-down")

# ---- 1c. filter adaptif tiap iterasi selagi jari ditahan ----
rep(
    "      } else {\n"
    "        if(!homeJiggle && !isSwiping && homePressedIdx>=0 && !searching && millis()-swipeStartTime>=550UL){",
    "      } else {\n"
    "        { // HOME_SMOOTH_V3: filter adaptif -- gerak kecil (jitter) diredam, gerak cepat langsung ikut\n"
    "          float dF=(float)ty-homeTyF; float aF=0.45f+fabsf(dF)*0.08f; if(aF>1.0f) aF=1.0f;\n"
    "          homeTyF+=dF*aF;\n"
    "        }\n"
    "        if(!homeJiggle && !isSwiping && homePressedIdx>=0 && !searching && millis()-swipeStartTime>=550UL){",
    "held branch")

# ---- 1d. mulai swipe: titik acuan filter = titik sentuh awal ----
rep(
    "          touchLastY=touchStartY;\n"
    "        }\n"
    "        if(isSwiping && !searching){",
    "          touchLastY=touchStartY;\n"
    "          homeTyLastF=(float)touchStartY; // HOME_SMOOTH_V3\n"
    "        }\n"
    "        if(isSwiping && !searching){",
    "swipe start")

# ---- 1e. delta drag pakai posisi terfilter ----
rep(
    "          float rawDelta=(float)(touchLastY-ty);\n"
    "          homeDragRaw += rawDelta;",
    "          float rawDelta=homeTyLastF-homeTyF; // HOME_SMOOTH_V3: dulu (float)(touchLastY-ty)\n"
    "          homeDragRaw += rawDelta;",
    "rawDelta")

# ---- 2a. drag: render hanya kalau piksel bulat berubah ----
rep(
    "          if(newY!=homeScrollY){ homeScrollY=newY; needRedraw=true; bandCand=true; }\n"
    "        }\n"
    "        touchLastY=ty;\n",
    "          if(newY!=homeScrollY){ // HOME_SMOOTH_V3: posisi float tetap diakumulasi, frame cuma kalau piksel bulat berubah\n"
    "            bool pxCh=(homeScrollPx(newY)!=homeScrollPx(homeScrollY));\n"
    "            homeScrollY=newY;\n"
    "            if(pxCh){ needRedraw=true; bandCand=true; }\n"
    "          }\n"
    "        }\n"
    "        touchLastY=ty;\n"
    "        homeTyLastF=homeTyF; // HOME_SMOOTH_V3\n",
    "drag redraw")

# ---- 2b. glide/pegas: lewati frame yg tidak menggeser piksel ----
rep(
    "      homeScrollPhysicsStep(dt);\n"
    "      needRedraw=true; bandCand=true;\n",
    "      int pxBeforeG=homeScrollPx(homeScrollY); // HOME_SMOOTH_V3\n"
    "      homeScrollPhysicsStep(dt);\n"
    "      if(homeScrollPx(homeScrollY)!=pxBeforeG || !homeScrollSpringing){ needRedraw=true; bandCand=true; } // frame penutup tetap digambar\n",
    "glide redraw")

s = s.replace("// ---- HOME-LAYOUT: urutan & sembunyikan ikon grid Home",
              "// HOME_SMOOTH_V3 (apply_home_smooth_v3.py): filter jari + lewati frame sia-sia + batas lempar 2.4 px/ms.\n"
              "// ---- HOME-LAYOUT: urutan & sembunyikan ikon grid Home", 1)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] HOME_SMOOTH_V3 diterapkan ke phone.ino")
