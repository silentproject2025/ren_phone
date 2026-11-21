// bounce -- animasi bola memantul (titik tetap 16.8), warna acak, miring HP menambah percepatan.
#include "nyx_app.h"
NYX_APP_GLOBALS

#define NB 6
typedef struct { int x, y, vx, vy, r; uint16_t c; } Ball;
static Ball* balls;               // dari arena app, bukan global besar
static int nballs;

static int app_enter(void) {
  const NyxApi* a = g_nyx;
  balls = (Ball*)a->mem_calloc(sizeof(Ball) * NB);
  if (!balls) return -1;
  nballs = NB;
  for (int i = 0; i < NB; i++) {
    balls[i].r = 6 + (int)(a->rand() % 10);
    balls[i].x = (40 + (int)(a->rand() % 200)) << 8; balls[i].y = (30 + (int)(a->rand() % 100)) << 8;
    balls[i].vx = ((int)(a->rand() % 600) - 300) | 64; balls[i].vy = ((int)(a->rand() % 600) - 300) | 64;
    balls[i].c = a->rgb((int)(a->rand() % 200) + 55, (int)(a->rand() % 200) + 55, (int)(a->rand() % 200) + 55);
  }
  return 0;
}

static void app_frame(void) {
  const NyxApi* a = g_nyx; int w = a->screen_w(), h = a->screen_h(), tx, ty, tz;
  int gx = 0, gy = 0;
  if (a->tilt(&tx, &ty, &tz) == 0) { gx = tx / 20; gy = ty / 20; }
  a->clear(a->rgb(6, 6, 16));
  for (int i = 0; i < nballs; i++) {
    Ball* b = &balls[i];
    b->vx += gx; b->vy += gy + 6;
    b->x += b->vx; b->y += b->vy;
    int px = b->x >> 8, py = b->y >> 8;
    if (px < b->r)       { px = b->r;       b->x = px << 8; b->vx = -b->vx * 9 / 10; }
    if (px > w - b->r)   { px = w - b->r;   b->x = px << 8; b->vx = -b->vx * 9 / 10; }
    if (py < b->r)       { py = b->r;       b->y = py << 8; b->vy = -b->vy * 9 / 10; }
    if (py > h - b->r)   { py = h - b->r;   b->y = py << 8; b->vy = -b->vy * 9 / 10; if (b->vy > -200) b->vy = -(300 + (int)(a->rand() % 500)); }
    a->circle(px, py, b->r, b->c);
  }
}

static void app_leave(void) { g_nyx->mem_free(balls); balls = 0; }

NYX_APP_MAIN(app_enter, app_frame, app_leave)
