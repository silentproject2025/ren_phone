#!/usr/bin/env python3
# apply_smooth_fx.py -- SMOOTH_FX_V1
# Jalankan dari root repo:  python3 tools/patches/apply_smooth_fx.py
# 1) AOD <-> layar: fade dikerjakan backlight saja (tanpa alpha-blend + push penuh tiap frame).
# 2) Notification Shade: latar dipotret sekali (ccBg, seperti Control Center) selagi shade bergerak; animasi 140 -> 200 ms.
# 3) Control Center: bayangan di bawah panel dihapus (dulu hilang saat animasi lalu muncul tiba-tiba).
# Berkas diubah: firmware/phone/phone.ino, firmware/phone/phone_notif_center.ino, CHANGELOG.md
import os, sys
M = "SMOOTH_FX_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
NC = os.path.join("firmware", "phone", "phone_notif_center.ino")
CL = "CHANGELOG.md"
def rd(p): return open(p, encoding="utf-8", newline="").read()
def wr(p, s): open(p, "w", encoding="utf-8", newline="").write(s)
def sub(s, o, n, l):
    if s.count(o) != 1: sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)." % (l, s.count(o)))
    return s.replace(o, n)
ph = rd(PH); nc = rd(NC); cl = rd(CL)
if M in ph: print("sudah terpasang (%s)" % M); sys.exit(0)

# --- 3) bayangan CC ---
ph = sub(ph, "void shadowSoft(LGFX_Sprite& s, int x,int y,int w,int h,int radius){\n",
 "bool ccNoShadow=false;   // %s: CC tanpa bayangan (konsisten saat bergerak & diam)\nvoid shadowSoft(LGFX_Sprite& s, int x,int y,int w,int h,int radius){\n  if(ccNoShadow) return;\n" % M, "shadowSoft")
ph = sub(ph, "  drawGlassPanel(s, mx,0,pw,ph,rad, T().surface, 215);\n",
 "  ccNoShadow=true; drawGlassPanel(s, mx,0,pw,ph,rad, T().surface, 215); ccNoShadow=false;   // %s\n" % M, "ccDrawLiveH")

# --- 1) AOD: backlight saja ---
ph = sub(ph, "    aodSnap.pushSprite(&s,0,0);\n    s.fillRectAlpha(0,0,SCR_W,SCR_H,(uint8_t)(e*255.0f),0x0000);\n    return true;\n",
 "    (void)e; aodSnap.pushSprite(&s,0,0);   // %s: gambar TETAP, redupnya lewat backlight saja -> frame identik, push diff ~0 baris\n    return true;\n" % M, "aod OUT")
ph = sub(ph, "  float e=aodEase(aodFxP(AODFX_WAKE_MS));\n  uint8_t inv=(uint8_t)((1.0f-e)*255.0f);\n  aodSnap.pushSprite(&s,0,0);\n  s.fillRectAlpha(0,0,SCR_W,SCR_H,inv,0x0000);\n  if(inv>8) aodFaceText(s,inv);   // jam AOD memudar keluar selagi layar hidup memudar masuk\n  return true;\n",
 "  aodSnap.pushSprite(&s,0,0);   // %s: layar hidup tampil di backlight rendah, lalu backlight naik (tanpa alpha per frame)\n  return true;\n" % M, "aod WAKE")
ph = sub(ph, "#define AODFX_WAKE_MS  300UL", "#define AODFX_WAKE_MS  350UL", "wake ms")

# --- 2) Notification Shade ---
ph = sub(ph, "static bool ccMotionNow(){\n  return controlCenterOpen && (panelPullActive || ccAnimating) && !locked && !appSwitcherOpen\n",
 "extern bool nsAnimating;   // phone_notif_center.ino\nstatic bool ccMotionNow(){\n  return ((controlCenterOpen && (panelPullActive || ccAnimating)) || (notifShadeOpen && (panelPullActive || nsAnimating))) && !locked && !appSwitcherOpen   // %s: shade ikut\n" % M, "ccMotionNow")
nc = sub(nc, "const float   NS_ANIM_MS = 140.0f;", "const float   NS_ANIM_MS = 200.0f;   // SMOOTH_FX_V1: 140 -> 200 ms (lebih banyak frame)\n//", "NS_ANIM_MS") if False else nc.replace("const float   NS_ANIM_MS = 140.0f;", "const float   NS_ANIM_MS = 200.0f; /* SMOOTH_FX_V1: 140 -> 200 ms */", 1)
if "NS_ANIM_MS = 200.0f" not in nc: sys.exit("GAGAL: NS_ANIM_MS")

cl = sub(cl, "## Unreleased\n", "## Unreleased\n- Improved: smoother AOD transitions and Notification Shade, and no more shadow under Control Center (SMOOTH_FX_V1). Going to AOD and waking now fade with the backlight only: the picture is no longer alpha-blended and pushed in full on every frame, so the fade is not limited to about 10 frames per second; the content switches while the backlight is lowest. Waking takes 0.35 s. The Notification Shade snapshots the background once while it moves (like Control Center) instead of redrawing Home or the app on every frame, and opens in 0.2 s (was 0.14 s) so it gets more frames. The soft shadow under Control Center is removed, because it disappeared during the animation and popped in at the end. Only drawing code and one constant changed. Not yet compiled or tested on a device. Script: `tools/patches/apply_smooth_fx.py`\n", "changelog")
wr(PH, ph); wr(NC, nc); wr(CL, cl); print("OK: %s terpasang" % M)
