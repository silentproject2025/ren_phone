#!/usr/bin/env python3
# apply_pager_di_fast.py -- HP_DI_FAST_V1 + HW_RC_VIEWER_V1
# Jalankan dari root repo:  python tools/patches/apply_pager_di_fast.py
#
# Bagian 2 (HW_RC_VIEWER_V1) -- PENYEBAB UTAMA "kadang bukan hardware":
#   Remote Control menyalakan AP-nya otomatis saat boot dan baru mati setelah HP tersambung ke WiFi (STA).
#   Selama AP nyala, rcApIsUp()==true -> hwsWhyNot()/hwsGenWhyNot()/hpHwOk() melarang hardware scroll
#   ("Remote Control aktif (butuh canvas)"). Jadi sebelum WiFi tersambung = jalur lambat; sesudahnya = hardware.
#   Perbaikan: larang hanya kalau ADA PENONTON Remote Control (poll < 4 dtk), bukan sekadar AP nyala.
#
# Bagian 1 (HP_DI_FAST_V1) -- Dynamic Island:
#   A. hpPrewarm(): redraw milik DI (berdenyut/animasi) tidak lagi memblokir pemanasan mode hardware.
#   B. hpPushHook(): halaman diam & canvas == gambar halaman kecuali pita atas 44 px (status bar + DI)
#      -> tulis hanya pita itu (~38 KB, bukan ~300 KB). Beda di luar pita -> jalur lama (aman).
#   C. hpImgAlloc(): cadangan PSRAM 1.5 MB -> 600 KB.
import sys, os
D = "firmware/phone"
def load(n): return open(os.path.join(D, n), encoding="utf-8").read()
def save(n, s): open(os.path.join(D, n), "w", encoding="utf-8").write(s)
def rep(s, old, new, name, cnt=1):
    if s.count(old) != cnt:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus %dx)" % (name, s.count(old), cnt))
    return s.replace(old, new)

M1 = "HP_DI_FAST_V1"
M2 = "HW_RC_VIEWER_V1"

HELPER = r'''// ---- HP_DI_FAST_V1 ----
// Halaman diam + canvas hanya beda di pita atas (status bar + DI) -> tulis pita itu saja ke ring panel.
#define HP_TOPBAND_H 44
static bool hpTopBandOnly(bool wasDirty){
  if(wasDirty || hpMotionNow() || hpInternal) return false;
  const int W = hpW(), H = SCR_H, bh = HP_TOPBAND_H;
  if(H <= bh || !hpEng.active) return false;
  if((int)lroundf(hpX) != hpCur * W || hpEng.px != hpCur * W) return false;
  LGFX_Sprite* im = hpImgGet(hpCur, false);
  if(!im || !im->getBuffer() || !canvas.getBuffer()) return false;
  if(im->width() != SCR_W || im->height() != SCR_H || canvas.width() != SCR_W || canvas.height() != SCR_H) return false;
  uint8_t* ib = (uint8_t*)im->getBuffer();
  const uint8_t* cb = (const uint8_t*)canvas.getBuffer();
  const size_t off = (size_t)W * (size_t)bh * 2;
  if(memcmp(ib + off, cb + off, (size_t)W * (size_t)(H - bh) * 2) != 0) return false;   // beda di luar pita -> jalur lama
  const int kk = hpEng.k();
  hpSink.begin();
  for(int i = 0; i < W; ){
    const int m = (i + kk) % W;
    int run = W - i; if(run > W - m) run = W - m;
    display.setClipRect(m, 0, run, bh);
    canvas.pushSprite(&display, m - i, 0);
    display.clearClipRect();
    i += run;
  }
  hpSink.end();
  memcpy(ib, cb, off);          // gambar halaman ikut terbarui (swipe berikutnya tidak membawa DI basi)
  hpCurDirty = false; hpCanvasStale = false; hpCanvasPg = hpCur;
  hpEng.nSlow++;
  return true;
}
// ---- /HP_DI_FAST_V1 ----

'''

# ---------------- pager_renphone.ino ----------------
s = load("pager_renphone.ino")
if M1 not in s:
    s = rep(s, "bytes + 1500000UL) return false;   // sisakan utk Doom/NES/dll",
               "bytes + 600000UL) return false;   // sisakan utk Doom/NES/dll (" + M1 + ": dulu 1500000UL)", "psram")
    s = rep(s, "  if(needRedraw || hpMotionNow() || hpPending || wasTouched || homePressedIdx >= 0 || homePressAnimIdx >= 0 || toastVisibleNow()\n     || hpWhyNot() != nullptr || !homeBandReady){ hpIdleSince = 0; return; }",
               "  const bool diBusy = diPulse || (diState != diTargetState);   // " + M1 + ": redraw milik DI tidak memblokir pemanasan\n"
               "  if((needRedraw && !diBusy) || hpMotionNow() || hpPending || wasTouched || homePressedIdx >= 0 || homePressAnimIdx >= 0 || toastVisibleNow()\n     || hpWhyNot() != nullptr || !homeBandReady){ hpIdleSince = 0; return; }", "prewarm")
    s = rep(s, "// ---------------- hook: push() frame penuh ----------------\nbool hpPushHook(){\n  if(!hpInternal && !hpMotionNow()){",
               "// ---------------- hook: push() frame penuh ----------------\n" + HELPER + "bool hpPushHook(){\n  const bool hpWasDirty = hpCurDirty;   // " + M1 + "\n  if(!hpInternal && !hpMotionNow()){", "pushhook-head")
    s = rep(s, "  if(why){ hpLeave(why, 0); return false; }\n  const int px = (int)lroundf(hpX);\n  if(hpMotionNow()){ if(!hpCompose(px)) return false; }",
               "  if(why){ hpLeave(why, 0); return false; }\n  if(hpTopBandOnly(hpWasDirty)) return true;   // " + M1 + "\n  const int px = (int)lroundf(hpX);\n  if(hpMotionNow()){ if(!hpCompose(px)) return false; }", "pushhook-body")
    print("ok: bagian 1 (" + M1 + ")")
else:
    print("bagian 1 sudah terpasang")
if M2 not in s:
    s = rep(s, "static bool hpHwOk(){ return hwlModeLoad() != 0 && !rcApIsUp() && !gameModeActive; }",
               "bool rcViewerIsActive();   // " + M2 + " (remote_renphone.ino)\nstatic bool hpHwOk(){ return hwlModeLoad() != 0 && !rcViewerIsActive() && !gameModeActive; }   // " + M2 + ": dulu !rcApIsUp()", "hpHwOk")
save("pager_renphone.ino", s)

# ---------------- hwscroll_renphone.ino ----------------
h = load("hwscroll_renphone.ino")
if M2 not in h:
    h = rep(h, 'if(rcApIsUp()) return "Remote Control aktif (butuh canvas)";',
               'if(rcViewerIsActive()) return "Remote Control aktif (butuh canvas)";   // ' + M2, "hws-rc", cnt=2)
    h = rep(h, "static HwsEngine hwsEng;\n", "bool rcViewerIsActive();   // " + M2 + " (remote_renphone.ino)\nstatic HwsEngine hwsEng;\n", "hws-decl")
    save("hwscroll_renphone.ino", h)

# ---------------- remote_renphone.ino ----------------
r = load("remote_renphone.ino")
if M2 not in r:
    r = rep(r, "bool rcApIsUp() { return rcApUp; }\n", "bool rcApIsUp() { return rcApUp; }\n", "rc-anchor")
    anchor = "static bool rcViewerActive() {\n  return rcLastViewMs != 0 && (millis() - rcLastViewMs) < RC_VIEW_TTL_MS;\n}\n"
    r = rep(r, anchor, anchor + "bool rcViewerIsActive() { return rcViewerActive(); }   // " + M2 + ": ada penonton Remote Control (poll < RC_VIEW_TTL_MS)\n", "rc-viewer")
    save("remote_renphone.ino", r)
print("ok: bagian 2 (" + M2 + ")")
