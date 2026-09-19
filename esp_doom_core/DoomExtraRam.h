#ifndef DOOM_EXTRA_RAM_H
#define DOOM_EXTRA_RAM_H

#ifdef __cplusplus
extern "C" {
#endif

void DoomExtraRam_Init(void);

// Alokasi blok NOL-kan di PSRAM (fallback: RAM internal kalau PSRAM penuh).
// Dipakai utk tabel besar yg dulu array global di DRAM (states, mobjinfo,
// ticdata, scalelight/zlight) -- lihat info.c, d_loop.c, r_main.c.
void *DoomExtraRam_Alloc(size_t n);

#ifdef __cplusplus
}
#endif

#endif
