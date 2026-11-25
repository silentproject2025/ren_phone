#!/usr/bin/env python3
# apply_no_back_pill.py -- NO_BACK_PILL_V1
# Jalankan dari root repo:  python3 tools/patches/apply_no_back_pill.py
# Pil "Kembali" dihapus di SEMUA layar (tidak digambar, area ketuk kiri-bawah ikut hilang).
# Yang tersisa: garis home bar di dasar layar (ketuk = kembali, cadangan), swipe-up dari dasar = Home,
# swipe dari tepi kiri = kembali (app yg sudah punya).
import os, sys
M = "NO_BACK_PILL_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
old = '''bool backHidden(){
  Screen c=curScreen();
  if(!screenHasEdgeBack(c)) return false;
  if(c==SCR_GALLERY && galInViewer()) return false;
  return true;
}
'''
new = '''bool backHidden(){
  return true;   // NO_BACK_PILL_V1: pil "Kembali" dihapus di semua layar (tinggal home bar + gestur)
}
'''
ph = sub(ph, old, new, "backHidden")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Changed: the *Kembali* pill at the bottom left is removed on every screen (NO_BACK_PILL_V1). It was easy to hit by accident (for example in games) and did not match the rest of the look. Going back is now: swipe in from the left edge (apps that have it), swipe up from the bottom edge to go Home, or tap the small home-bar line at the bottom centre. The layout of the apps is unchanged (the strip at the bottom stays empty). Only drawing and one hit-area function changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_no_back_pill.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
