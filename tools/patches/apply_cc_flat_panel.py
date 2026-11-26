#!/usr/bin/env python3
# apply_cc_flat_panel.py -- CC_FLAT_PANEL_V1  (butuh apply_smooth_fx.py + apply_cc_no_shadow.py sudah terpasang)
# Jalankan dari root repo:  python3 tools/patches/apply_cc_flat_panel.py
# Control Center: panel digambar SATU warna rata. Dihapus: bingkai abu-abu di tepi (pita luar tanpa gradasi),
# garis rim/border, dan highlight di atas. Hanya berlaku saat menggambar CC (flag ccNoShadow).
import os, sys
M = "CC_FLAT_PANEL_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
if "CC_NO_SHADOW_V1" not in ph: sys.exit("GAGAL: jalankan apply_smooth_fx.py lalu apply_cc_no_shadow.py dulu.")
old = "  shadowSoft(s,x,y,w,h,radius);\n  // v51: FIX \"kotak2 kayak hancur/pecah\""
new = ("  shadowSoft(s,x,y,w,h,radius);\n"
       "  if(ccNoShadow){   // CC_FLAT_PANEL_V1: panel CC = satu warna rata, tanpa bingkai/rim/highlight\n"
       "    nyxAaRR(s, x,y,w,h,radius, blend565(glassSampleAvg(s,x,y,w,h), tint, tintAlpha));\n"
       "    return;\n"
       "  }\n"
       "  // v51: FIX \"kotak2 kayak hancur/pecah\"")
ph = sub(ph, old, new, "drawGlassPanel")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Changed: the Control Center panel is now one flat colour (CC_FLAT_PANEL_V1). The grey frame along the top, bottom, left and right edges (an outer band without the glass gradient), the thin rim line and the highlight at the top are gone. This also makes the panel cheaper to draw while it animates. Only drawing code changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_cc_flat_panel.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
