// memhog -- uji quota & arena: minta blok sampai ditolak, isi pola, cek, bebaskan separuh, minta lagi.
#include "nyx_app.h"
NYX_APP_GLOBALS

#define MAXB 512
static unsigned char* blk[MAXB];
static int nblk, okAll = 1, done;

static void fill(unsigned char* p, int n, int tag) { for (int i = 0; i < n; i++) p[i] = (unsigned char)(tag + i * 3); }
static int  veri(unsigned char* p, int n, int tag) { for (int i = 0; i < n; i++) if (p[i] != (unsigned char)(tag + i * 3)) return 0; return 1; }

static int app_enter(void) {
  const NyxApi* a = g_nyx; nblk = 0; okAll = 1; done = 0;
  while (nblk < MAXB) {                                    // 1) isi sampai quota menolak
    unsigned char* p = (unsigned char*)a->mem_alloc(1000);
    if (!p) break;
    fill(p, 1000, nblk); blk[nblk++] = p;
  }
  if (nblk < 10) okAll = 0;
  void* big = a->mem_alloc(a->mem_quota() * 2);            // 2) lebih dari quota HARUS ditolak
  if (big) okAll = 0;
  for (int i = 0; i < nblk; i += 2) { if (!veri(blk[i], 1000, i)) okAll = 0; a->mem_free(blk[i]); blk[i] = 0; }   // 3) bebaskan separuh
  for (int i = 0; i < nblk; i += 2) {                       // 4) minta lagi: ruang yang dibebaskan harus terpakai ulang
    blk[i] = (unsigned char*)a->mem_alloc(900);
    if (!blk[i]) { okAll = 0; break; }
    fill(blk[i], 900, 100 + i);
  }
  for (int i = 1; i < nblk; i += 2) if (!veri(blk[i], 1000, i)) okAll = 0;   // yang tidak disentuh tetap utuh
  g_nyx->log(okAll ? "memhog ok" : "memhog GAGAL");
  done = 1; return 0;
}

static void app_frame(void) {
  const NyxApi* a = g_nyx; char tmp[2] = {0, 0}; (void)tmp;
  a->clear(a->rgb(0, 0, 0));
  a->text(8, 8, okAll ? "memhog: OK" : "memhog: GAGAL", okAll ? a->rgb(80, 255, 120) : a->rgb(255, 80, 80), 2);
  int w = a->screen_w() - 16; unsigned used = (unsigned)a->mem_used(), quota = (unsigned)a->mem_quota();
  a->rect(8, 40, w, 12, a->rgb(200, 200, 200));
  a->fill_rect(9, 41, (int)((unsigned long long)(w - 2) * used / quota), 10, a->rgb(60, 140, 255));
  (void)done;
}

static void app_leave(void) {
  const NyxApi* a = g_nyx;
  for (int i = 0; i < nblk; i++) if (blk[i]) { a->mem_free(blk[i]); blk[i] = 0; }
}

NYX_APP_MAIN(app_enter, app_frame, app_leave)
