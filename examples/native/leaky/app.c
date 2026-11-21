// leaky -- app SALAH sengaja: bocor 2 blok, double free, free pointer asing, tulis lewat batas.
// Dipakai tes untuk memastikan host MENDETEKSI & MEMBERSIHKAN, bukan crash.
#include "nyx_app.h"
NYX_APP_GLOBALS

static int app_enter(void) {
  const NyxApi* a = g_nyx;
  char* leak1 = (char*)a->mem_alloc(100); char* leak2 = (char*)a->mem_alloc(2000); (void)leak1; (void)leak2;
  char* t = (char*)a->mem_alloc(50); a->mem_free(t); a->mem_free(t);        // double free
  int local; a->mem_free(&local);                                           // pointer asing
  char* o = (char*)a->mem_alloc(32); o[32] = 0x11;                           // tulis 1 byte lewat batas (canary)
  (void)o;
  return 0;
}
static void app_frame(void) { g_nyx->clear(g_nyx->rgb(40, 0, 0)); }
static void app_leave(void) {}
NYX_APP_MAIN(app_enter, app_frame, app_leave)
