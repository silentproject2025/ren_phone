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

void DoomExtraRam_Init(void)
{
    // Tabel besar yg dulu di DRAM internal (fix "region dram0_0_seg overflowed")
    Info_InitPsram();         // states[] + mobjinfo[]  (~39.7 KB)
    D_LoopInitPsram();        // ticdata[]              (~20.5 KB)
    R_LightTablesInitPsram(); // scalelight/zlight      (~11.5 KB)

    floorclip        = (short*)   AllocPsram(SCREENWIDTH  * sizeof(short));
    ceilingclip      = (short*)   AllocPsram(SCREENWIDTH  * sizeof(short));
    spanstart        = (int*)     AllocPsram(SCREENHEIGHT * sizeof(int));
    spanstop         = (int*)     AllocPsram(SCREENHEIGHT * sizeof(int));
    yslope           = (fixed_t*) AllocPsram(SCREENHEIGHT * sizeof(fixed_t));
    distscale        = (fixed_t*) AllocPsram(SCREENWIDTH  * sizeof(fixed_t));
    cachedheight     = (fixed_t*) AllocPsram(SCREENHEIGHT * sizeof(fixed_t));
    cacheddistance   = (fixed_t*) AllocPsram(SCREENHEIGHT * sizeof(fixed_t));
    cachedxstep      = (fixed_t*) AllocPsram(SCREENHEIGHT * sizeof(fixed_t));
    cachedystep      = (fixed_t*) AllocPsram(SCREENHEIGHT * sizeof(fixed_t));

    columnofs        = (int*)     AllocPsram(DXR_MAXWIDTH  * sizeof(int));
    ylookup          = (byte**)   AllocPsram(DXR_MAXHEIGHT * sizeof(byte*));

    viewangletox     = (int*)     AllocPsram((FINEANGLES/2) * sizeof(int));

    negonearray       = (short*)  AllocPsram(SCREENWIDTH * sizeof(short));
    screenheightarray = (short*)  AllocPsram(SCREENWIDTH * sizeof(short));
    clipbot           = (short*)  AllocPsram(SCREENWIDTH * sizeof(short));
    cliptop            = (short*) AllocPsram(SCREENWIDTH * sizeof(short));
}
