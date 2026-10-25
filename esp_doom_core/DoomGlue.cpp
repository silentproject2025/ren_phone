#pragma GCC optimize ("O3")
#include <esp_heap_caps.h>
#include "esp_attr.h"
#include "esp_attr.h"
#include <DoomGlue.h>

#include <Arduino.h>

extern "C"
{
#include <doomgeneric.h>
#include <doomkeys.h>
#include <palette_esp32.h>
}

// hw_conf.h lives in the sketch, not the library, but the sketch folder
// is on the include path for every translation unit Arduino compiles --
// including this one (same trick DoomSound.cpp uses for DOOM_MUSIC_SYNTH).
#if __has_include("hw_conf.h")
#include "hw_conf.h"
#endif

IDoomDisplay* DG_Display = nullptr;
IDoomInput*   DG_Input   = nullptr;

// ------------------------------------------------------------------
//  HOOK DARI SKETCH (gamepad web) -- tombol EXTRA, geser-lihat, teks
// ------------------------------------------------------------------
//  IDoomInput cuma punya 8 tombol. Gamepad web butuh jauh lebih banyak
//  (strafe, run, senjata, F-keys, ...), jadi sketch mendaftarkan 3 fungsi
//  lewat DoomGlue_SetHooks(). Kalau tidak didaftarkan, semuanya no-op.
//  Bit EXTMAP di bawah HARUS sama persis dgn urutan EXN[] di halaman
//  gamepad_renphone.ino.
typedef uint32_t (*DG_ExtKeysFn)(void);
typedef int32_t  (*DG_MouseFn)(void);
typedef int      (*DG_TypedFn)(void);
static DG_ExtKeysFn hookExt   = nullptr;
static DG_MouseFn   hookMouse = nullptr;
static DG_TypedFn   hookTyped = nullptr;

void DoomGlue_SetHooks(DG_ExtKeysFn ext, DG_MouseFn mouse, DG_TypedFn typed)
{
    hookExt = ext; hookMouse = mouse; hookTyped = typed;
}

// Dipanggil dari I_GetEvent() (i_input.c): delta geser-lihat sejak panggilan lalu.
extern "C" int DG_MouseTake(void)
{
    return hookMouse ? (int)hookMouse() : 0;
}

extern "C" { extern int key_prevweapon; extern int key_nextweapon; }

// ------------------------------------------------------------------
//  Rescale / fill / crop
// ------------------------------------------------------------------
//
//  Doom always renders at a fixed 320x200 (DOOMGENERIC_RESX/RESY). Real
//  panels don't: 240x240, 800x800, 80x160, whatever you're wiring up.
//  hw_conf.h can declare DOOM_VIEWPORT_W/H to control how Doom's image
//  is scaled BEFORE it's placed on the panel, independently of
//  DOOM_PANEL_W/H (the panel's own physical size). Three modes, picked
//  by which of DOOM_VIEWPORT_W/H you set:
//
//    not set / 0,0     NATIVE  -- no rescale. Content is 320x200.
//    W, 0              FILL    -- content is W wide; height follows
//                                 Doom's fixed 320:200 ratio
//                                 automatically (H = W * 200/320).
//                                 DOOM_VIEWPORT_W is the one number
//                                 you choose; it's always read as a
//                                 width, whichever physical axis of
//                                 your panel that ends up meaning.
//    W, H (both != 0)  CUSTOM  -- independent per-axis stretch/squash
//                                 to exactly W x H. No aspect ratio
//                                 preservation. examples/Minimal/ uses
//                                 this to fill a 240x240 panel.
//
//  Once content size is known, DOOM_VIEWPORT_X/Y (default 0,0) means
//  one of two different things PER AXIS, decided automatically:
//
//    content fits the panel  -> X/Y PLACE it (top-left corner inside
//                                the panel; same meaning as before).
//    content overflows it    -> X/Y instead pick which part of the
//                                content is visible: a fixed crop/pan
//                                origin, declared once here, not
//                                adjustable at runtime.
//
//  Getting any of this wrong used to be a silent out-of-bounds write.
//  The static_asserts below catch a mismatch at compile time instead.

#ifndef DOOM_VIEWPORT_W
#define DOOM_VIEWPORT_W 0

#endif

#ifndef DOOM_VIEWPORT_H
#define DOOM_VIEWPORT_H 0
#endif

#ifndef DOOM_VIEWPORT_X
#define DOOM_VIEWPORT_X 0
#endif

#ifndef DOOM_VIEWPORT_Y
#define DOOM_VIEWPORT_Y 0
#endif

#if DOOM_VIEWPORT_W == 0 && DOOM_VIEWPORT_H == 0
// NATIVE
#define DOOM_CONTENT_W DOOMGENERIC_RESX
#define DOOM_CONTENT_H DOOMGENERIC_RESY
#elif DOOM_VIEWPORT_H == 0
// FILL: width-driven, height derived from Doom's fixed 320:200 ratio.
#define DOOM_CONTENT_W DOOM_VIEWPORT_W
#define DOOM_CONTENT_H \
    ((DOOM_VIEWPORT_W * DOOMGENERIC_RESY + DOOMGENERIC_RESX / 2) / DOOMGENERIC_RESX)
#else
// CUSTOM: independent stretch.
#define DOOM_CONTENT_W DOOM_VIEWPORT_W
#define DOOM_CONTENT_H DOOM_VIEWPORT_H
#endif

static constexpr int kContentW = DOOM_CONTENT_W;
static constexpr int kContentH = DOOM_CONTENT_H;

#ifdef DOOM_PANEL_W
static constexpr int kPanelW = DOOM_PANEL_W;
#else
static constexpr int kPanelW = kContentW;   // no panel size declared: assume exact fit
#endif

#ifdef DOOM_PANEL_H
static constexpr int kPanelH = DOOM_PANEL_H;
#else
static constexpr int kPanelH = kContentH;
#endif

// Per axis: does the content fit, or does it need cropping?
static constexpr bool kCropX = kContentW > kPanelW;
static constexpr bool kCropY = kContentH > kPanelH;

// How many pixels/rows actually get sent to the display per frame.
static constexpr int kOutW = kCropX ? kPanelW : kContentW;
static constexpr int kOutH = kCropY ? kPanelH : kContentH;

// Where on the panel drawing starts (0 when cropping: the crop already
// fills the panel edge to edge on that axis).
static constexpr int kPanelX0 = kCropX ? 0 : DOOM_VIEWPORT_X;
static constexpr int kPanelY0 = kCropY ? 0 : DOOM_VIEWPORT_Y;

// Where in the (possibly rescaled) content reading starts (0 when
// placing: the whole content is shown, nothing to crop into).
static constexpr int kCropX0 = kCropX ? DOOM_VIEWPORT_X : 0;
static constexpr int kCropY0 = kCropY ? DOOM_VIEWPORT_Y : 0;

static_assert(!kCropX ? (DOOM_VIEWPORT_X + kContentW <= kPanelW)
                       : (DOOM_VIEWPORT_X >= 0 && DOOM_VIEWPORT_X + kPanelW <= kContentW),
    "DOOM_VIEWPORT_X doesn't fit: content doesn't fit at that placement, "
    "or the crop window falls outside the (rescaled) content");

static_assert(!kCropY ? (DOOM_VIEWPORT_Y + kContentH <= kPanelH)
                       : (DOOM_VIEWPORT_Y >= 0 && DOOM_VIEWPORT_Y + kPanelH <= kContentH),
    "DOOM_VIEWPORT_Y doesn't fit: content doesn't fit at that placement, "
    "or the crop window falls outside the (rescaled) content");
EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR 
EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR EXT_RAM_BSS_ATTR static uint16_t xLUT[kContentW];
EXT_RAM_BSS_ATTR static uint16_t yLUT[kContentH];
static bool lutReady = false;

// Nearest-neighbor with rounding (not truncation): srcIdx = round(dstIdx
// * srcSize / dstSize), clamped. Cheap -- built once, not per frame --
// and good enough for a resize that's mostly there to fit a panel, not
// to be a quality upscaler. Maps CONTENT-space (post-rescale) back to
// Doom's native 320x200 source space.
static void BuildScaleLUT()
{
    for (int x = 0; x < kContentW; x++)
    {
        int sx = (x * DOOMGENERIC_RESX + kContentW / 2) / kContentW;
        if (sx >= DOOMGENERIC_RESX) sx = DOOMGENERIC_RESX - 1;
        xLUT[x] = (uint16_t)sx;
    }

    for (int y = 0; y < kContentH; y++)
    {
        int sy = (y * DOOMGENERIC_RESY + kContentH / 2) / kContentH;
        if (sy >= DOOMGENERIC_RESY) sy = DOOMGENERIC_RESY - 1;
        yLUT[y] = (uint16_t)sy;
    }

    lutReady = true;
}

// ------------------------------------------------------------------
//  Key queue
// ------------------------------------------------------------------
//
//  I_GetEvent() calls DG_GetKey() in a loop until it returns 0. We
//  can't call DG_Input->update() on every one of those calls: it
//  would recompute pressed()/released() against state that's already
//  been consumed. A single poll per tick (rate-limited), the rest
//  just drains the queue.

struct KeyEvent { unsigned char key; unsigned char pressed; };

static constexpr uint8_t QUEUE_SIZE = 16;
static KeyEvent queue[QUEUE_SIZE];
static uint8_t qHead = 0, qTail = 0;

static void QueuePush(unsigned char key, unsigned char pressed)
{
    uint8_t next = (qTail + 1) % QUEUE_SIZE;
    if (next == qHead) return;
    queue[qTail] = {key, pressed};
    qTail = next;
}

static bool QueuePop(KeyEvent& e)
{
    if (qHead == qTail) return false;
    e = queue[qHead];
    qHead = (qHead + 1) % QUEUE_SIZE;
    return true;
}

struct ButtonMap { DoomButton button; unsigned char key; };

static const ButtonMap KEYMAP[] =
{
    { DoomButton::Up,     KEY_UPARROW    },
    { DoomButton::Down,   KEY_DOWNARROW  },
    { DoomButton::Left,   KEY_LEFTARROW  },
    { DoomButton::Right,  KEY_RIGHTARROW },
    { DoomButton::A,      KEY_FIRE       },
    { DoomButton::B,      KEY_USE        },
    { DoomButton::Start,  KEY_ENTER      },
    { DoomButton::Select, KEY_ESCAPE     },
};

static constexpr uint32_t POLL_INTERVAL_MS = 5;
static uint32_t lastPoll = 0;

static inline int QueueFree() { return (qHead - qTail - 1 + QUEUE_SIZE) % QUEUE_SIZE; }

// bit i dari mask EXTRA -> kode tombol DOOM (urutan = EXN[] di halaman web)
static const unsigned char EXTMAP[31] =
{
    KEY_STRAFE_L, KEY_STRAFE_R, KEY_RSHIFT /*run*/, KEY_RALT /*alt = strafe-hold*/,
    '1', '2', '3', '4', '5', '6', '7',
    KEY_TAB /*map*/, 'y', 'n', KEY_BACKSPACE, KEY_PAUSE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11,
    KEY_MINUS, KEY_EQUALS,
    ',' /*senjata sebelumnya*/, '.' /*senjata berikutnya*/
};
static uint32_t extPrev = 0;

static void PollInput()
{
    DG_Input->update();

    for (auto& m : KEYMAP)
    {
        if (DG_Input->pressed(m.button))       QueuePush(m.key, 1);
        else if (DG_Input->released(m.button)) QueuePush(m.key, 0);
    }

    // ---- tombol EXTRA dari gamepad web (edge: tekan/lepas) ----
    if (hookExt)
    {
        uint32_t ext = hookExt();
        uint32_t diff = ext ^ extPrev;
        for (int i = 0; i < 31 && diff; i++)
        {
            uint32_t bit = 1u << i;
            if (!(diff & bit)) continue;
            if (QueueFree() < 1) break;          // antrean penuh -> coba lagi di poll berikut
            QueuePush(EXTMAP[i], (ext & bit) ? 1 : 0);
            extPrev ^= bit;
            diff &= ~bit;
        }
    }

    // ---- teks ketikan (nama save, cheat): 1 karakter = tekan+lepas ----
    if (hookTyped && QueueFree() >= 2)
    {
        int c = hookTyped();
        if (c > 0)
        {
            unsigned char k = (c == 127) ? KEY_BACKSPACE : (unsigned char)c;
            QueuePush(k, 1);
            QueuePush(k, 0);
        }
    }
}

// ------------------------------------------------------------------
//  doomgeneric.h
// ------------------------------------------------------------------

void DG_Init()
{
}

// ------------------------------------------------------------------
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

#ifndef DOOM_INTERNAL_RESERVE_KB
#define DOOM_INTERNAL_RESERVE_KB 48
#endif
static constexpr int kBandRows = 20;
static uint16_t* bandBuf[2] = { nullptr, nullptr };
static bool bandReady = false, bandTried = false;
bool DoomGlue_BandPathActive() { return bandReady; }

static constexpr bool kIdent = (kContentW == DOOMGENERIC_RESX && kContentH == DOOMGENERIC_RESY && !kCropX && !kCropY);

static void AllocBands()
{
    if (!DG_Display || !DG_Display->supportsBand()) return;
    const size_t bytes = (size_t)kOutW * kBandRows * sizeof(uint16_t);
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < 2 * bytes + (size_t)DOOM_INTERNAL_RESERVE_KB * 1024) return;
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
            uint32_t a = (uint32_t)p[s[i]] | ((uint32_t)p[s[i + 1]] << 16);
            uint32_t b = (uint32_t)p[s[i + 2]] | ((uint32_t)p[s[i + 3]] << 16);
            ((uint32_t*)(d + i))[0] = a;
            ((uint32_t*)(d + i))[1] = b;
        }
    }
    for (; i < n; i++) d[i] = p[s[i]];
}

static void DrawFrameImpl()
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

// ---- diagnostik (dibaca doom_renphone.ino) ----
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

void DG_SleepMs(uint32_t ms) { delay(ms); }

uint32_t DG_GetTicksMs() { return millis(); }

int DG_GetKey(int* pressed, unsigned char* key)
{
    KeyEvent e;

    if (!QueuePop(e))
    {
        if (!DG_Input) return 0;

        uint32_t now = millis();
        if (now - lastPoll < POLL_INTERVAL_MS) return 0;
        lastPoll = now;

        PollInput();

        if (!QueuePop(e)) return 0;
    }

    *pressed = e.pressed;
    *key = e.key;
    return 1;
}

void DG_SetWindowTitle(const char*) {}

// ------------------------------------------------------------------
//  Lifecycle
// ------------------------------------------------------------------

void DoomGlue_Begin(const char* wadPath)
{
    static char argWad[] = "-iwad";
    static char argPath[128];
    strncpy(argPath, wadPath, sizeof(argPath) - 1);

    static char* argv[] = { (char*)"doom", argWad, argPath };

    extPrev = 0;
    doomgeneric_Create(3, argv);

    // Dipaksa SETELAH doomgeneric_Create() (default.cfg di SD bisa menimpa nilainya):
    // senjata sebelumnya / berikutnya dipakai tombol ◀ ▶ di halaman gamepad.
    key_prevweapon = ',';
    key_nextweapon = '.';
}

void DoomGlue_Tick()
{
    doomgeneric_Tick();
}

void DoomGlue_Shutdown()
{
    doomgeneric_Shutdown();

    // v-boost: pastikan DMA terakhir selesai, lalu lepas buffer pita
    if (DG_Display) DG_Display->flushDMA();
    if (bandBuf[0]) heap_caps_free(bandBuf[0]);
    if (bandBuf[1]) heap_caps_free(bandBuf[1]);
    bandBuf[0] = bandBuf[1] = nullptr;
    bandReady = false; bandTried = false;
}