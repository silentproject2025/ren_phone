import sys, os
D='esp_doom_core/'
for f in (D+'DoomGlue.cpp', D+'DoomExtraRam.c', 'doom_renphone.ino', 'hw_conf.h'):
    if not os.path.exists(f): print("ERROR: %s tidak ada"%f); sys.exit(1)
def rd(p): return open(p,encoding='utf-8').read()
def wr(p,s): open(p,'w',encoding='utf-8').write(s)
def rep(src,old,new,what):
    if src.count(old)!=1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Pastikan apply_doom_boost_patch.py sudah dijalankan. Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)
if 'DG_DrawUsAccum' in rd(D+'DoomGlue.cpp'):
    print("Diagnostik sudah terpasang, dilewati"); sys.exit(0)
out={}

hc=rd('hw_conf.h')
if 'DOOM_STATS' not in hc:
    hc+='''
// --- Diagnostik & memori ------------------------------------------------
// DOOM_STATS 1 = tampilkan fps/ms/status RAM di strip hitam atas layar (+ Serial tiap 1 detik).
#define DOOM_STATS         1
// Sisa heap internal yg DIJAGA (KB) -- framebuffer/colormap/buffer DMA cuma diambil dari RAM
// internal kalau setelah diambil masih sisa segini. Turunkan (mis. 32) kalau HUD menunjukkan
// VB/CM/BAND jatuh ke PSRAM; naikkan kalau WiFi/web gamepad jadi tidak stabil.
#define DOOM_INTERNAL_RESERVE_KB 48
'''
out['hw_conf.h']=hc

e=rd(D+'DoomExtraRam.c')
e=rep(e,"#define DXR_INTERNAL_RESERVE (80 * 1024)",
"#if __has_include(\"hw_conf.h\")\n#include \"hw_conf.h\"\n#endif\n#ifndef DOOM_INTERNAL_RESERVE_KB\n#define DOOM_INTERNAL_RESERVE_KB 48\n#endif\n#define DXR_INTERNAL_RESERVE (DOOM_INTERNAL_RESERVE_KB * 1024)","reserve")
out[D+'DoomExtraRam.c']=e

g=rd(D+'DoomGlue.cpp')
g=rep(g,"2 * bytes + 70 * 1024","2 * bytes + (size_t)DOOM_INTERNAL_RESERVE_KB * 1024","band reserve")
g=rep(g,"static constexpr int kBandRows = 20;","#ifndef DOOM_INTERNAL_RESERVE_KB\n#define DOOM_INTERNAL_RESERVE_KB 48\n#endif\nstatic constexpr int kBandRows = 20;","reserve def")
g=rep(g,"void DG_DrawFrame()\n{\n    if (!DG_Display) return;","static void DrawFrameImpl()\n{\n    if (!DG_Display) return;","rename draw")
g=rep(g,"void DG_SleepMs",
r'''// ---- diagnostik (dibaca doom_renphone.ino) ----
volatile uint32_t DG_FrameCount = 0;
volatile uint32_t DG_DrawUsAccum = 0;
extern "C" { extern unsigned char* I_VideoBuffer; extern unsigned char* colormaps; }
static inline bool dgIsPsram(const void* p) { uintptr_t a = (uintptr_t)p; return a >= 0x3C000000u && a < 0x3E000000u; }
bool DG_VideoBufInternal() { return I_VideoBuffer && !dgIsPsram(I_VideoBuffer); }
bool DG_ColormapInternal() { return colormaps && !dgIsPsram(colormaps); }

void DG_DrawFrame()
{
    uint32_t t0 = micros();
    DrawFrameImpl();
    DG_DrawUsAccum += micros() - t0;
    DG_FrameCount++;
}

void DG_SleepMs''',"wrapper")
out[D+'DoomGlue.cpp']=g

s=rd('doom_renphone.ino')
s=rep(s,"void doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();\n",
"void doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();\nextern volatile uint32_t DG_FrameCount, DG_DrawUsAccum; bool DG_VideoBufInternal(); bool DG_ColormapInternal();\n","decl")
s=rep(s,"  int ovlStatic = 3;",
"  uint32_t stT0 = millis(), stFrames0 = 0, stDraw0 = 0, stTickUs = 0, stTicks = 0;\n  int ovlStatic = 3;","stats vars")
s=rep(s,"    DoomGlue_Tick();\n",
r'''    uint32_t tk0 = micros();
    DoomGlue_Tick();
    stTickUs += micros() - tk0; stTicks++;
#if DOOM_STATS
    if(millis() - stT0 >= 1000){
      uint32_t dt = millis() - stT0;
      uint32_t fr = DG_FrameCount - stFrames0;
      uint32_t dus = DG_DrawUsAccum - stDraw0;
      uint32_t fps10 = dt ? (fr * 10000UL) / dt : 0;
      uint32_t drawMs = fr ? (dus / fr) / 1000 : 0;
      uint32_t tickMs = stTicks ? (stTickUs / stTicks) / 1000 : 0;
      char l1[48], l2[48];
      snprintf(l1, sizeof(l1), "fps %u.%u  tick %ums  kirim %ums", (unsigned)(fps10/10), (unsigned)(fps10%10), (unsigned)tickMs, (unsigned)drawMs);
      snprintf(l2, sizeof(l2), "VB:%c CM:%c BAND:%c int %uK maks %uK",
               DG_VideoBufInternal()?'I':'P', DG_ColormapInternal()?'I':'P', DoomGlue_BandPathActive()?'Y':'N',
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)/1024));
      Serial.printf("[doom] %s | %s\n", l1, l2);
      display.waitDMA();
      display.startWrite();
      display.fillRect(0, 0, 284, 19, TFT_BLACK);
      display.setTextSize(1); display.setTextColor(TFT_YELLOW);
      display.setCursor(2, 1);  display.print(l1);
      display.setCursor(2, 10); display.print(l2);
      display.endWrite();
      stT0 = millis(); stFrames0 = DG_FrameCount; stDraw0 = DG_DrawUsAccum; stTickUs = 0; stTicks = 0;
    }
#endif
''',"stats loop")
out['doom_renphone.ino']=s
for p,t in out.items(): wr(p,t)
print("OK: diagnostik DOOM terpasang (HUD kuning di strip atas layar).")
