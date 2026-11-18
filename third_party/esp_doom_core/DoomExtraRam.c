// Buffer render DOOM yang tadinya static array di RAM internal,
// sekarang dialokasikan ke PSRAM sekali di awal (dipanggil dari Z_Init).
#include <stddef.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "doomtype.h"
#include "m_fixed.h"
#include "i_video.h"
#include "tables.h"
#include "r_plane.h"
#include "r_draw.h"
#include "r_state.h"
#include "r_things.h"
#include "r_main.h"
#include "info.h"
#include "d_loop.h"
#include "DoomExtraRam.h"

// Harus sama dengan MAXWIDTH/MAXHEIGHT di r_draw.c
#define DXR_MAXWIDTH  1120
#define DXR_MAXHEIGHT 832

static void *AllocPsram(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) {
        // fallback: mending lambat daripada NULL-deref crash
        p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
    }
    return p;
}

void *DoomExtraRam_Alloc(size_t n)
{
    void *p = AllocPsram(n);
    if (p) memset(p, 0, n);
    return p;
}

// ---- alokasi cepat (RAM internal) + pelacakan ----
// Sisakan minimal segini di heap internal buat WiFi/lwIP/TLS/stack task lain.
#if __has_include("hw_conf.h")
#include "hw_conf.h"
#endif
#ifndef DOOM_INTERNAL_RESERVE_KB
#define DOOM_INTERNAL_RESERVE_KB 80
#endif
#define DXR_INTERNAL_RESERVE (DOOM_INTERNAL_RESERVE_KB * 1024)
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
{
    // Tabel besar yg dulu di DRAM internal (fix "region dram0_0_seg overflowed")
    Info_InitPsram();         // states[] + mobjinfo[]  (~39.7 KB)
    D_LoopInitPsram();        // ticdata[]              (~20.5 KB)
    R_LightTablesInitPsram(); // scalelight/zlight      (~11.5 KB)

    floorclip        = (short*)   DoomExtraRam_AllocFast(SCREENWIDTH  * sizeof(short));
    ceilingclip      = (short*)   DoomExtraRam_AllocFast(SCREENWIDTH  * sizeof(short));
    spanstart        = (int*)     DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(int));
    spanstop         = (int*)     DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(int));
    yslope           = (fixed_t*) DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(fixed_t));
    distscale        = (fixed_t*) DoomExtraRam_AllocFast(SCREENWIDTH  * sizeof(fixed_t));
    cachedheight     = (fixed_t*) DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(fixed_t));
    cacheddistance   = (fixed_t*) DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(fixed_t));
    cachedxstep      = (fixed_t*) DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(fixed_t));
    cachedystep      = (fixed_t*) DoomExtraRam_AllocFast(SCREENHEIGHT * sizeof(fixed_t));

    columnofs        = (int*)     DoomExtraRam_AllocFast(DXR_MAXWIDTH  * sizeof(int));
    ylookup          = (byte**)   DoomExtraRam_AllocFast(DXR_MAXHEIGHT * sizeof(byte*));

    viewangletox     = (int*)     AllocPsram((FINEANGLES/2) * sizeof(int));

    negonearray       = (short*)  DoomExtraRam_AllocFast(SCREENWIDTH * sizeof(short));
    screenheightarray = (short*)  DoomExtraRam_AllocFast(SCREENWIDTH * sizeof(short));
    clipbot           = (short*)  DoomExtraRam_AllocFast(SCREENWIDTH * sizeof(short));
    cliptop            = (short*) DoomExtraRam_AllocFast(SCREENWIDTH * sizeof(short));
}
