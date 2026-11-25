#!/usr/bin/env python3
# apply_no_swipe_hint.py -- NO_SWIPE_HINT_V1
# Jalankan dari root repo:  python3 tools/patches/apply_no_swipe_hint.py
# Lock screen: hapus pill kaca + chevron + teks "Geser ke atas utk buka". Indikator home bar kecil tetap ada.
import os, sys
M = "NO_SWIPE_HINT_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
old = '''  // hint geser (pill kaca + chevron)
  const char* hint="Geser ke atas utk buka";
  int hw=(int)s.textWidth(hint);
  if(lift<12){ // v110: hint disembunyikan begitu jari mulai menarik ke atas
  int hintPillY = SCR_H-38;
  drawGlassPanel(s, SCR_W/2-hw/2-14, hintPillY, hw+28, 26, 13, T().surface, 130);
  int chevY = hintPillY-9;
  s.drawLine(SCR_W/2-6,chevY+4, SCR_W/2,chevY,   fg2);
  s.drawLine(SCR_W/2,  chevY,   SCR_W/2+6,chevY+4,fg2);
  s.setTextColor(fg2); s.setTextSize(1);
  s.setCursor(SCR_W/2-hw/2, SCR_H-24);
  s.print(hint);
  nyxAaRR(s, SCR_W/2-22, SCR_H-9, 44, 4, 2, fg2);   // LOCK_UX_V2: indikator ala home bar
  }
'''
new = '''  // NO_SWIPE_HINT_V1: teks "Geser ke atas utk buka" (pill + chevron) dihapus; cukup indikator home bar
  if(lift<12){ // v110: indikator disembunyikan begitu jari mulai menarik ke atas
  nyxAaRR(s, SCR_W/2-22, SCR_H-9, 44, 4, 2, fg2);   // LOCK_UX_V2: indikator ala home bar
  }
'''
ph = sub(ph, old, new, "hint")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Changed: the lock screen no longer shows the *Geser ke atas utk buka* pill with its chevron (NO_SWIPE_HINT_V1). The small home-bar line at the bottom stays. Only drawing code changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_no_swipe_hint.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
