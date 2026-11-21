// hello -- contoh paling kecil: teks, hitung frame, lingkaran mengikuti sentuhan.
#include "nyx_app.h"
NYX_APP_GLOBALS

static int frames, cx = -1, cy = -1;

static int itoa_(int v, char* b) {            // tanpa libc: app tidak bergantung pada simbol firmware
  char t[12]; int n = 0, l = 0; unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
  if (v < 0) b[l++] = '-';
  if (u == 0) t[n++] = '0';
  while (u) { t[n++] = (char)('0' + u % 10); u /= 10; }
  while (n) b[l++] = t[--n];
  b[l] = 0; return l;
}

static int app_enter(void) { frames = 0; cx = cy = -1; g_nyx->log("hello: enter"); return 0; }

static void app_frame(void) {
  const NyxApi* a = g_nyx; char buf[32]; int x, y;
  a->clear(a->rgb(12, 14, 34));
  a->text(8, 8, "Halo dari app native!", a->rgb(255, 255, 255), 2);
  int n = itoa_(frames, buf); (void)n;
  a->text(8, 40, "frame:", a->rgb(150, 170, 255), 1);
  a->text(56, 40, buf, a->rgb(255, 220, 120), 1);
  if (a->touch(&x, &y) & NYX_TOUCH_DOWN) { cx = x; cy = y; }
  if (cx >= 0) { a->circle(cx, cy, 14, a->rgb(255, 70, 70)); a->ring(cx, cy, 20, a->rgb(255, 255, 255)); }
  frames++;
  if (frames == 3) g_nyx->log("hello: frame 3");
}

static void app_leave(void) { g_nyx->log("hello: leave"); }

NYX_APP_MAIN(app_enter, app_frame, app_leave)
