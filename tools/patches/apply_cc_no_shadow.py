#!/usr/bin/env python3
# apply_cc_no_shadow.py -- CC_NO_SHADOW_V1  (butuh apply_smooth_fx.py sudah terpasang)
# Jalankan dari root repo:  python3 tools/patches/apply_cc_no_shadow.py
# Control Center: SEMUA bayangan dihapus (panel: atas/bawah/kiri/kanan, tombol bulat, pil toast).
import os, sys
M = "CC_NO_SHADOW_V1"
PH = os.path.join("firmware", "phone", "phone.ino"); CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)
if "SMOOTH_FX_V1" not in ph: sys.exit("GAGAL: jalankan apply_smooth_fx.py dulu.")
# 1) flag dipasang untuk seluruh gambar CC (bukan cuma panelnya)
ph = sub(ph, "  ccNoShadow=true; drawGlassPanel(s, mx,0,pw,ph,rad, T().surface, 215); ccNoShadow=false;   // SMOOTH_FX_V1\n",
              "  drawGlassPanel(s, mx,0,pw,ph,rad, T().surface, 215);   // CC_NO_SHADOW_V1: flag ccNoShadow dipasang oleh pembungkus ccDrawLiveH\n", "panel")
ph = sub(ph, "static void ccDrawLiveH(LGFX_Sprite& s, int ph, bool withToast){\n  if(ph<=0) return;\n",
              "static void ccDrawLiveH_impl(LGFX_Sprite& s, int ph, bool withToast);\nstatic void ccDrawLiveH(LGFX_Sprite& s, int ph, bool withToast){   // CC_NO_SHADOW_V1\n  ccNoShadow=true; ccDrawLiveH_impl(s,ph,withToast); ccNoShadow=false;\n}\nstatic void ccDrawLiveH_impl(LGFX_Sprite& s, int ph, bool withToast){\n  if(ph<=0) return;\n", "wrapper")
# 2) bayangan tombol bulat & pil ikut flag
ph = sub(ph, "  { uint16_t sb=glassSampleAvg(s,cx-r,cy-r+2,r*2,r*2,3); nyxAaCircle(s, cx,cy+2,r+1,blend565(sb,0x0000,45)); } // v-baru",
              "  if(!ccNoShadow){ uint16_t sb=glassSampleAvg(s,cx-r,cy-r+2,r*2,r*2,3); nyxAaCircle(s, cx,cy+2,r+1,blend565(sb,0x0000,45)); } // v-baru", "circle")
ph = sub(ph, "    if(ph>0){ ccCacheBlit(s,ph); drawToast(s); }\n", "    if(ph>0){ ccCacheBlit(s,ph); ccNoShadow=true; drawToast(s); ccNoShadow=false; }   // CC_NO_SHADOW_V1\n", "toast")
cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Changed: Control Center has no shadows at all now (CC_NO_SHADOW_V1): not around the panel (top, bottom, left, right), not under the round buttons, and not under the toast pill. Only drawing code changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_cc_no_shadow.py`\n", "changelog")
wr(PH, ph); wr(CL, cl); print("OK: %s terpasang" % M)
