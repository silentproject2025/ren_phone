import sys, os, re
D = 'esp_doom_core/'
need = [D+f for f in ('DoomGlue.cpp','IDoomDisplay.h','DoomExtraRam.c','DoomExtraRam.h','i_video.c','r_data.c','doomgeneric.c')] + ['doom_renphone.ino','hw_conf.h']
for f in need:
    if not os.path.exists(f):
        print("ERROR: %s tidak ada (jalankan dari root repo ren_phone)" % f); sys.exit(1)

def rd(p): return open(p, encoding='utf-8').read()
def wr(p, s): open(p, 'w', encoding='utf-8').write(s)
def rep(src, old, new, what, cnt=1):
    if src.count(old) != cnt:
        print("ERROR: anchor '%s' ketemu %d kali (harus %d). Tidak ada yang diubah." % (what, src.count(old), cnt)); sys.exit(1)
    return src.replace(old, new)

if 'DG_PalWire' in rd(D+'i_video.c'):
    print("Patch DOOM boost sudah terpasang, dilewati"); sys.exit(0)

out = {}   # kumpulkan semua hasil dulu, tulis di akhir (all-or-nothing)

# ============ DoomExtraRam.h / .c : alokasi RAM internal yg dilacak ============
h = rd(D+'DoomExtraRam.h')
h = rep(h, "void *DoomExtraRam_Alloc(size_t n);\n",
"""void *DoomExtraRam_Alloc(size_t n);

// v-boost: alokasi "cepat". AllocInternalOnly = RAM internal atau NULL (tanpa fallback).
// AllocFast = internal kalau sisa heap internal masih aman, kalau tidak PSRAM.
// Semuanya DILACAK dan dibebaskan sekaligus oleh DoomExtraRam_FreeFast()
// (dipanggil di akhir doomgeneric_Shutdown) -> tidak bocor antar sesi.
void *DoomExtraRam_AllocInternalOnly(size_t n);
void *DoomExtraRam_AllocFast(size_t n);
int   DoomExtraRam_IsTracked(const void *p);
void  DoomExtraRam_FreeFast(void);
""", "ExtraRam.h")
if '#include <stddef.h>' not in h:
    h = h.replace("#ifndef DOOM_EXTRA_RAM_H\n#define DOOM_EXTRA_RAM_H\n", "#ifndef DOOM_EXTRA_RAM_H\n#define DOOM_EXTRA_RAM_H\n#include <stddef.h>\n", 1)
out[D+'DoomExtraRam.h'] = h

c = rd(D+'DoomExtraRam.c')
c = rep(c, "void DoomExtraRam_Init(void)\n{",
r'''// ---- alokasi cepat (RAM internal) + pelacakan ----
// Sisakan minimal segini di heap internal buat WiFi/lwIP/TLS/stack task lain.
#define DXR_INTERNAL_RESERVE (80 * 1024)
#define DXR_MAX_TRACKED 64
static void *dxrTracked[DXR_MAX_TRACKED];
static int   dxrTrackedN = 0;

static void DxrTrack(void *p)
{
    if (p && dxrTrackedN < DXR_MAX_TRACKED) dxrTracked[dxrTrackedN++] = p;
}

void *DoomExtraRam_AllocInternalOnly(size_t n)
{
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < n + DXR_INTERNAL_RESERVE) return NULL;
    void *p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) { memset(p, 0, n); DxrTrack(p); }
    return p;
}

void *DoomExtraRam_AllocFast(size_t n)
{
    void *p = DoomExtraRam_AllocInternalOnly(n);
    if (p) return p;
    p = AllocPsram(n);
    if (p) { memset(p, 0, n); DxrTrack(p); }
    return p;
}

int DoomExtraRam_IsTracked(const void *p)
{
    for (int i = 0; i < dxrTrackedN; i++) if (dxrTracked[i] == p) return 1;
    return 0;
}

void DoomExtraRam_FreeFast(void)
{
    for (int i = 0; i < dxrTrackedN; i++) heap_caps_free(dxrTracked[i]);
    dxrTrackedN = 0;
}

void DoomExtraRam_Init(void)
{''', "ExtraRam.c init")
# array per-kolom / per-baris yg dibaca tiap kolom & span -> alokasi cepat (viewangletox tetap PSRAM)
lines = c.split('\n'); n = 0; inInit = False
for i, ln in enumerate(lines):
    if ln.startswith('void DoomExtraRam_Init(void)'): inInit = True
    if inInit and 'AllocPsram(' in ln and 'viewangletox' not in ln:
        lines[i] = ln.replace('AllocPsram(', 'DoomExtraRam_AllocFast('); n += 1
if n < 10: print("ERROR: baris AllocPsram di DoomExtraRam_Init cuma %d (harusnya >=10)" % n); sys.exit(1)
out[D+'DoomExtraRam.c'] = '\n'.join(lines)

# ============ doomgeneric.c : bebaskan alokasi cepat saat shutdown ============
g = rd(D+'doomgeneric.c')
g = rep(g, '#include "r_draw.h"\n', '#include "r_draw.h"\n#include "DoomExtraRam.h"\n', "dg include")
g = rep(g, '\tDG_STEP("zone");\n', '\tDG_STEP("zone");\n\n\tDoomExtraRam_FreeFast();   // RAM internal (framebuffer, colormap, tabel kolom) dikembalikan\n', "dg free")
out[D+'doomgeneric.c'] = g

# ============ r_data.c : colormap -> RAM internal ============
r = rd(D+'r_data.c')
r = rep(r, "    colormaps = W_CacheLumpNum(lump, PU_STATIC);\n}\n",
r'''    colormaps = W_CacheLumpNum(lump, PU_STATIC);
    {   // colormap dibaca TIAP PIKSEL -> salin ke RAM internal kalau muat
        int sz = W_LumpLength(lump);
        byte *fast = (byte *) DoomExtraRam_AllocInternalOnly((size_t) sz);
        if (fast) { memcpy(fast, colormaps, sz); colormaps = fast; }
    }
}
''', "colormaps")
if '#include "DoomExtraRam.h"' not in r:
    i = r.find('#include')
    r = r[:i] + '#include <stddef.h>\n#include <string.h>\n#include "DoomExtraRam.h"\n' + r[i:]
out[D+'r_data.c'] = r

# ============ i_video.c : framebuffer internal, tanpa memcpy, palet siap-kirim ============
v = rd(D+'i_video.c')
v = '#pragma GCC optimize ("O3")\n#include <stddef.h>\n#include "DoomExtraRam.h"\n#if __has_include("hw_conf.h")\n#include "hw_conf.h"\n#endif\n#ifndef DOOM_SWAP_RB\n#define DOOM_SWAP_RB 1\n#endif\n' + v
v = rep(v, "byte *I_VideoBuffer = NULL;\n",
r'''byte *I_VideoBuffer = NULL;

// --- v-boost: dipakai DoomGlue.cpp ---
// Palet SIAP KIRIM: 16-bit sudah byte-swap (urutan kabel SPI), jadi DMA bisa langsung
// ambil tanpa konversi. Diisi ulang tiap I_SetPalette (gamma + kilatan damage/pickup ikut).
uint16_t DG_PalWire[256];
// Sumber frame yg akan dikirim: langsung I_VideoBuffer (tanpa salin) kalau tidak di-scale.
const unsigned char *DG_FrameSrc = NULL;
''', "video buffer decl")
v = rep(v, "\tI_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // For DOOM to draw on\n",
'''	I_VideoBuffer = (byte*)DoomExtraRam_AllocInternalOnly (SCREENWIDTH * SCREENHEIGHT); // RAM internal: render jauh lebih cepat drpd PSRAM
	if (!I_VideoBuffer)
		I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // fallback: zona (PSRAM)
''', "alloc videobuffer")
v = rep(v, "\tZ_Free (I_VideoBuffer);\n",
"\tif (!DoomExtraRam_IsTracked(I_VideoBuffer)) Z_Free (I_VideoBuffer);\n\tI_VideoBuffer = NULL;\n\tDG_FrameSrc = NULL;\n", "shutdown videobuffer")
v = rep(v, "    unsigned char *line_in, *line_out;\n    /* Offsets in case FB is bigger than DOOM */",
r'''    unsigned char *line_in, *line_out;

    /* v-boost: jalur cepat -- tanpa scaling & tanpa offset, kirim I_VideoBuffer langsung
     * (dulu: memcpy 64KB PSRAM->PSRAM tiap frame cuma buat disalin lagi). */
    if (fb_scaling == 1 && s_Fb.xres == SCREENWIDTH && s_Fb.yres == SCREENHEIGHT)
    {
        DG_FrameSrc = (const unsigned char *) I_VideoBuffer;
        DG_DrawFrame();
        return;
    }
    DG_FrameSrc = (const unsigned char *) DG_ScreenBuffer;

    /* Offsets in case FB is bigger than DOOM */''', "finishupdate")
v = rep(v, "        colors[i].b = gammatable[usegamma][*palette++];\n    }\n",
r'''        colors[i].b = gammatable[usegamma][*palette++];
    }

    /* v-boost: palet siap-kirim (RGB565, channel R/B ditukar spt NES krn panel BGR utk buffer mentah,
     * lalu byte-swap = urutan kabel SPI). Set DOOM_SWAP_RB 0 di hw_conf.h kalau warna terbalik. */
    for (i = 0; i < 256; ++i) {
        unsigned r5 = colors[i].r >> 3, g6 = colors[i].g >> 2, b5 = colors[i].b >> 3;
#if DOOM_SWAP_RB
        unsigned px = (b5 << 11) | (g6 << 5) | r5;
#else
        unsigned px = (r5 << 11) | (g6 << 5) | b5;
#endif
        DG_PalWire[i] = (uint16_t)(((px & 0xFF) << 8) | (px >> 8));
    }
''', "setpalette")
out[D+'i_video.c'] = v

# ============ -O3 utk file render panas (firmware lain tetap -Os) ============
for f in ('r_draw.c','r_plane.c','r_segs.c','r_bsp.c','r_main.c','r_things.c','m_fixed.c','v_video.c','p_maputl.c'):
    p = D+f
    if os.path.exists(p):
        t = rd(p)
        if 'GCC optimize' not in t: out[p] = '#pragma GCC optimize ("O3")\n' + t

# ============ IDoomDisplay.h : jalur band + DMA ============
d = rd(D+'IDoomDisplay.h')
d = rep(d, "    virtual int width() const = 0;\n",
r'''    // v-boost (opsional): kirim sebuah PITA (band) w x h piksel sekaligus. 'wire565' sudah dalam urutan
    // kabel (big-endian), bisa langsung di-DMA-kan. Implementasi boleh async; DoomGlue memakai 2 buffer
    // bergantian dan memanggil flushDMA() sebelum membebaskan buffer.
    virtual bool supportsBand() const { return false; }
    virtual void writeBand(int x, int y, int w, int h, const uint16_t* wire565) { (void)x;(void)y;(void)w;(void)h;(void)wire565; }
    virtual void flushDMA() {}

    virtual int width() const = 0;
''', "idisplay")
out[D+'IDoomDisplay.h'] = d

# ============ DoomGlue.cpp : konversi per-pita + DMA ============
gl = rd(D+'DoomGlue.cpp')
gl = '#pragma GCC optimize ("O3")\n#include <esp_heap_caps.h>\n' + gl
i = gl.find("void DG_DrawFrame()\n{")
j = gl.find("void DG_SleepMs")
if i < 0 or j < 0 or j < i: print("ERROR: blok DG_DrawFrame tidak ketemu"); sys.exit(1)
newdraw = r'''// ------------------------------------------------------------------
//  v-boost: gambar per-PITA + DMA async
// ------------------------------------------------------------------
//  Dulu: tiap piksel panggil DG_PaletteColor565() (fungsi), tiap baris setAddrWindow+writePixels
//  (200x transaksi), CPU nunggu SPI selesai. Sekarang:
//   - palet siap-kirim DG_PalWire[] (RAM internal), konversi 2 piksel/langkah
//   - frame dipecah jadi pita kBandRows baris; pita k dikonversi CPU SELAGI pita k-1 jalan di DMA
//   - pita terakhir tetap jalan di DMA selagi core ini langsung mulai merender frame berikutnya
//  Kalau RAM internal/DMA tidak cukup -> otomatis pakai jalur lama (per baris).
extern "C" { extern const unsigned char* DG_FrameSrc; extern uint16_t DG_PalWire[256]; }

typedef void (*DG_BandHookFn)(uint16_t* buf, int px, int py, int w, int h);
static DG_BandHookFn hookBand = nullptr;
void DoomGlue_SetBandHook(DG_BandHookFn f) { hookBand = f; }

static constexpr int kBandRows = 20;
static uint16_t* bandBuf[2] = { nullptr, nullptr };
static bool bandReady = false, bandTried = false;
bool DoomGlue_BandPathActive() { return bandReady; }

static constexpr bool kIdent = (kContentW == DOOMGENERIC_RESX && kContentH == DOOMGENERIC_RESY && !kCropX && !kCropY);

static void AllocBands()
{
    if (!DG_Display || !DG_Display->supportsBand()) return;
    const size_t bytes = (size_t)kOutW * kBandRows * sizeof(uint16_t);
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < 2 * bytes + 70 * 1024) return;
    bandBuf[0] = (uint16_t*)heap_caps_aligned_alloc(4, bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bandBuf[1] = (uint16_t*)heap_caps_aligned_alloc(4, bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!bandBuf[0] || !bandBuf[1])
    {
        if (bandBuf[0]) heap_caps_free(bandBuf[0]);
        if (bandBuf[1]) heap_caps_free(bandBuf[1]);
        bandBuf[0] = bandBuf[1] = nullptr;
        return;
    }
    bandReady = true;
}

static inline void ConvRowIdent(uint16_t* d, const uint8_t* s, int n)
{
    const uint16_t* p = DG_PalWire;
    int i = 0;
    if ((reinterpret_cast<uintptr_t>(d) & 3) == 0)
    {
        for (; i + 4 <= n; i += 4)
        {
            uint32_t a = (uint32_t)p[s[i])] | ((uint32_t)p[s[i + 1]] << 16);
            uint32_t b = (uint32_t)p[s[i + 2]] | ((uint32_t)p[s[i + 3]] << 16);
            ((uint32_t*)(d + i))[0] = a;
            ((uint32_t*)(d + i))[1] = b;
        }
    }
    for (; i < n; i++) d[i] = p[s[i]];
}

void DG_DrawFrame()
{
    if (!DG_Display) return;
    if (!lutReady) BuildScaleLUT();
    if (!bandTried) { bandTried = true; AllocBands(); }

    const uint8_t* src = DG_FrameSrc ? (const uint8_t*)DG_FrameSrc : (const uint8_t*)DG_ScreenBuffer;
    if (!src) return;

    if (bandReady)
    {
        static unsigned parity = 0;
        DG_Display->beginFrame();
        for (int y0 = 0; y0 < kOutH; y0 += kBandRows)
        {
            const int rows = (kOutH - y0 < kBandRows) ? (kOutH - y0) : kBandRows;
            uint16_t* buf = bandBuf[parity++ & 1];

            for (int r = 0; r < rows; r++)
            {
                uint16_t* d = buf + (size_t)r * kOutW;
                const int contentRow = y0 + r + kCropY0;
                if (kIdent)
                {
                    ConvRowIdent(d, src + (size_t)contentRow * DOOMGENERIC_RESX, kOutW);
                }
                else
                {
                    const uint8_t* srcRow = src + (size_t)yLUT[contentRow] * DOOMGENERIC_RESX;
                    for (int c = 0; c < kOutW; c++) d[c] = DG_PalWire[srcRow[xLUT[c + kCropX0]]];
                }
            }
            if (hookBand) hookBand(buf, kPanelX0, kPanelY0 + y0, kOutW, rows);
            DG_Display->writeBand(kPanelX0, kPanelY0 + y0, kOutW, rows, buf);
        }
        DG_Display->endFrame();
        return;
    }

    // ---- jalur lama (per baris), dipakai kalau RAM internal tidak cukup ----
    EXT_RAM_BSS_ATTR static uint16_t row[kOutW];
    DG_Display->beginFrame();
    for (int panelRow = 0; panelRow < kOutH; panelRow++)
    {
        int contentRow = panelRow + kCropY0;
        const uint8_t* srcRow = src + (size_t)yLUT[contentRow] * DOOMGENERIC_RESX;
        for (int panelCol = 0; panelCol < kOutW; panelCol++)
        {
            uint16_t w = DG_PalWire[srcRow[xLUT[panelCol + kCropX0]]];
            row[panelCol] = (uint16_t)((w >> 8) | (w << 8));      // balikin ke urutan native utk writePixels()
        }
        DG_Display->writeRow(kPanelX0, kPanelY0 + panelRow, row, kOutW);
    }
    DG_Display->endFrame();
}

'''
newdraw = newdraw.replace("p[s[i])]", "p[s[i]]")
gl = gl[:i] + newdraw + gl[j:]
gl = rep(gl, "    doomgeneric_Shutdown();\n",
r'''    doomgeneric_Shutdown();

    // v-boost: pastikan DMA terakhir selesai, lalu lepas buffer pita
    if (DG_Display) DG_Display->flushDMA();
    if (bandBuf[0]) heap_caps_free(bandBuf[0]);
    if (bandBuf[1]) heap_caps_free(bandBuf[1]);
    bandBuf[0] = bandBuf[1] = nullptr;
    bandReady = false; bandTried = false;
''', "glue shutdown")
out[D+'DoomGlue.cpp'] = gl

# ============ hw_conf.h ============
hc = rd('hw_conf.h')
if 'DOOM_SWAP_RB' not in hc:
    hc += '''
// --- Warna (jalur cepat DMA) -------------------------------------------
// 1 = tukar kanal R/B (panel BGR + buffer mentah, sama seperti NES). Kalau warna DOOM terbalik
// (merah jadi biru), ganti ke 0 lalu build ulang.
#define DOOM_SWAP_RB       1
'''
out['hw_conf.h'] = hc

# ============ doom_renphone.ino ============
s = rd('doom_renphone.ino')
a = s.find("class RenPhoneDoomDisplay : public IDoomDisplay {")
b = s.find("\n};\n", a)
if a < 0 or b < 0: print("ERROR: class RenPhoneDoomDisplay tidak ketemu"); sys.exit(1)
newcls = r'''class RenPhoneDoomDisplay : public IDoomDisplay {
public:
  bool begin() override { return true; } // display sudah di-init phone.ino setup()
  // v-boost: transaksi SPI dibuka SEKALI & dibiarkan terbuka selama sesi DOOM (ditutup di close()),
  // supaya DMA pita terakhir tetap jalan SELAGI core ini merender frame berikutnya.
  void beginFrame() override { if(!txOpen){ display.startWrite(); txOpen=true; } }
  void writeRow(int x, int y, const uint16_t* rgb565, int count) override {
    display.setAddrWindow(x, y, count, 1);
    display.writePixels((const lgfx::rgb565_t*)rgb565, count);
  }
  bool supportsBand() const override { return true; }
  void writeBand(int x, int y, int w, int h, const uint16_t* wire565) override {
    display.waitDMA();                                            // pita sebelumnya harus selesai dulu
    display.pushImageDMA(x, y, w, h, (const lgfx::swap565_t*)wire565); // data sudah urutan kabel -> tanpa konversi
  }
  void flushDMA() override { display.waitDMA(); }
  void endFrame() override { /* sengaja kosong: transaksi tetap terbuka (lihat beginFrame) */ }
  void close() { if(txOpen){ display.waitDMA(); display.endWrite(); txOpen=false; } }
  int width()  const override { return DOOM_PANEL_W; }
  int height() const override { return DOOM_PANEL_H; }
private:
  bool txOpen = false;'''
s = s[:a] + newcls + s[b:]
s = rep(s, "void DoomGlue_SetHooks(uint32_t (*ext)(void), int32_t (*mouse)(void), int (*typed)(void)); // esp_doom_core/DoomGlue.cpp\n",
"void DoomGlue_SetHooks(uint32_t (*ext)(void), int32_t (*mouse)(void), int (*typed)(void)); // esp_doom_core/DoomGlue.cpp\nvoid DoomGlue_SetBandHook(void (*fn)(uint16_t*, int, int, int, int)); // esp_doom_core/DoomGlue.cpp\nbool DoomGlue_BandPathActive();\nvoid doomOvlBuild(); void doomOvlFree(); void doomDrawOverlay();\nvoid doomBandHook(uint16_t* buf, int px, int py, int w, int h);\n", "decl")
s = rep(s, "  doomInput.begin();\n\n  DoomGlue_Begin(DOOM_WAD_PATH);\n  doomPlaying = true;\n",
"  DoomGlue_SetBandHook(doomBandHook);\n  doomOvlBuild();\n  doomInput.begin();\n\n  DoomGlue_Begin(DOOM_WAD_PATH);\n  doomPlaying = true;\n  int ovlStatic = 3;   // tombol di strip hitam atas/bawah digambar sekali (frame tidak menyentuh strip itu)\n", "task begin")
s = rep(s, "    doomDrawOverlay(); // gambar tombol virtual DI ATAS frame yg baru saja ditulis DoomGlue_Tick()\n",
"    // jalur cepat: tombol sudah ditempel ke tiap pita oleh doomBandHook(); jalur lama: gambar langsung\n    if(ovlStatic > 0){ ovlStatic--; doomDrawOverlay(); }\n    else if(!DoomGlue_BandPathActive()) doomDrawOverlay();\n", "task loop")
s = rep(s, "  DoomGlue_Shutdown();\n  doomPlaying = false;\n",
"  DoomGlue_Shutdown();\n  doomDisplay.close();\n  doomOvlFree();\n  doomPlaying = false;\n", "task end")
s = rep(s, "void doomDrawOverlay(){\n  display.startWrite();\n",
r'''// ---- overlay yg ditempel ke tiap pita (jalur cepat) ----
// Tiap tombol di-render SEKALI ke sprite kecil, lalu dikonversi ke format kabel (R/B ditukar spt palet
// DOOM). Per frame cuma menyalin piksel non-transparan -> jauh lebih murah drpd gambar lingkaran+teks.
struct DoomOvl { int x, y, w, h; uint16_t* pix; uint16_t key; };
static DoomOvl doomOvl[4];
static int doomOvlN = 0;

static uint16_t doomWireFixRB(uint16_t wire){
#if DOOM_SWAP_RB
  uint16_t v = (wire >> 8) | (wire << 8);
  v = ((v & 0x1F) << 11) | (v & 0x07E0) | (v >> 11);
  return (v >> 8) | (v << 8);
#else
  return wire;
#endif
}

static void doomOvlPaint(LGFX_Sprite& sp, int kind, int r, const char* label, uint16_t col){
  if(kind == 0){                       // tombol bulat
    sp.fillCircle(r,r,r,col); sp.drawCircle(r,r,r,TFT_BLACK);
    sp.setTextColor(TFT_WHITE); sp.setTextSize(1);
    int tw = sp.textWidth(label); sp.setCursor(r-tw/2, r-3); sp.print(label);
  } else {                             // tombol pause
    sp.fillRoundRect(0,0,32,28,4,0x4208); sp.drawRoundRect(0,0,32,28,4,TFT_BLACK);
    sp.fillRect(10,6,4,16,TFT_WHITE); sp.fillRect(18,6,4,16,TFT_WHITE);
  }
}

static void doomOvlAdd(int x, int y, int w, int h, int kind, int r, const char* label, uint16_t col){
  if(doomOvlN >= 4) return;
  LGFX_Sprite spr(&display);
  spr.setColorDepth(16);
  spr.setPsram(false);
  if(!spr.createSprite(w, h)) return;
  spr.fillSprite(0x0001);
  uint16_t* sb = (uint16_t*)spr.getBuffer();
  uint16_t key = sb[0];
  doomOvlPaint(spr, kind, r, label, col);
  uint16_t* pix = (uint16_t*)heap_caps_malloc((size_t)w*h*2, MALLOC_CAP_8BIT);
  if(!pix){ spr.deleteSprite(); return; }
  for(int i=0;i<w*h;i++) pix[i] = (sb[i]==key) ? key : doomWireFixRB(sb[i]);
  DoomOvl& o = doomOvl[doomOvlN++];
  o.x=x; o.y=y; o.w=w; o.h=h; o.pix=pix; o.key=key;
  spr.deleteSprite();
}

void doomOvlBuild(){
  doomOvlN = 0;
  doomOvlAdd(doomFireCx()-22, doomFireCy()-22, 45, 45, 0, 22, "F",  0xF800);
  doomOvlAdd(doomUseCx()-16,  doomUseCy()-16,  33, 33, 0, 16, "U",  0x07E0);
  doomOvlAdd(doomOkCx()-14,   doomOkCy()-14,   29, 29, 0, 14, "OK", 0x001F);
  doomOvlAdd(doomPauseX()-16, doomPauseY()-14, 33, 29, 1, 0,  "",   0);
}

void doomOvlFree(){
  for(int i=0;i<doomOvlN;i++){ if(doomOvl[i].pix) heap_caps_free(doomOvl[i].pix); doomOvl[i].pix=nullptr; }
  doomOvlN = 0;
}

// Dipanggil DoomGlue untuk tiap pita (koordinat panel). Tempel tombol yg beririsan dgn pita ini.
void doomBandHook(uint16_t* buf, int px, int py, int w, int h){
  for(int k=0;k<doomOvlN;k++){
    const DoomOvl& o = doomOvl[k];
    int y0 = max(o.y, py), y1 = min(o.y+o.h, py+h);
    int x0 = max(o.x, px), x1 = min(o.x+o.w, px+w);
    if(y0>=y1 || x0>=x1) continue;
    for(int y=y0;y<y1;y++){
      uint16_t* d = buf + (size_t)(y-py)*w + (x0-px);
      const uint16_t* sp = o.pix + (size_t)(y-o.y)*o.w + (x0-o.x);
      for(int x=x0;x<x1;x++,d++,sp++) if(*sp != o.key) *d = *sp;
    }
  }
}

void doomDrawOverlay(){
  display.waitDMA();   // DMA frame terakhir harus selesai sebelum gambar langsung
  display.startWrite();
''', "overlay")
out['doom_renphone.ino'] = s

# ---- tulis semuanya ----
for p, t in out.items(): wr(p, t)
print("OK: DOOM boost terpasang (%d file diubah). Build ulang & flash S3." % len(out))
