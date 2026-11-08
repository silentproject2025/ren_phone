#ifndef DOOM_EXTRA_RAM_H
#define DOOM_EXTRA_RAM_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void DoomExtraRam_Init(void);

// Alokasi blok NOL-kan di PSRAM (fallback: RAM internal kalau PSRAM penuh).
// Dipakai utk tabel besar yg dulu array global di DRAM (states, mobjinfo,
// ticdata, scalelight/zlight) -- lihat info.c, d_loop.c, r_main.c.
void *DoomExtraRam_Alloc(size_t n);

// v-boost: alokasi "cepat". AllocInternalOnly = RAM internal atau NULL (tanpa fallback).
// AllocFast = internal kalau sisa heap internal masih aman, kalau tidak PSRAM.
// Semuanya DILACAK dan dibebaskan sekaligus oleh DoomExtraRam_FreeFast()
// (dipanggil di akhir doomgeneric_Shutdown) -> tidak bocor antar sesi.
void *DoomExtraRam_AllocInternalOnly(size_t n);
void *DoomExtraRam_AllocFast(size_t n);
int   DoomExtraRam_IsTracked(const void *p);
void  DoomExtraRam_FreeFast(void);

#ifdef __cplusplus
}
#endif

#endif
