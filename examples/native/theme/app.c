// theme -- ganti wallpaper dari Picsum (gratis, tanpa API key).
// Alur: Ambil (unduh JPG sesuai ukuran layar) -> Terapkan (simpan ke data/) -> tutup app,
// firmware memuat ulang wallpaper (butuh apply_theme_patch.py). Tidak memakai libc.
#include "nyx_app.h"
NYX_APP_GLOBALS

#define BUF_CAP  (120 * 1024)    // < 128 KB (batas buffer HTTP host)
#define CHUNK    4096            // tulis per frame supaya watchdog 250 ms aman di FFat
#define TIMEOUT  30000u

enum { S_IDLE, S_FETCH, S_GOT, S_SAVE, S_DONE, S_ERR };
typedef struct { int x, y, w, h; } R;

static int st, mode, http_id, buf_n, save_pos, save_h, seed, jw, jh;
static uint32_t t_start;
static uint8_t* buf;
static char url[128], msg[48], fname[20];
static const char* MODE_NAME[3] = { "Normal", "Hitam-putih", "Blur" };
static const char* MODE_Q[3]    = { "", "&grayscale", "&blur=2" };

// ---- pembantu string (tanpa libc) ----
static int put_str(char* d, int p, int cap, const char* s) { while (*s && p < cap - 1) d[p++] = *s++; d[p] = 0; return p; }
static int put_int(char* d, int p, int cap, int v) {
  char t[12]; int n = 0; unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
  if (v < 0 && p < cap - 1) d[p++] = '-';
  if (!u) t[n++] = '0';
  while (u) { t[n++] = (char)('0' + u % 10); u /= 10; }
  while (n && p < cap - 1) d[p++] = t[--n];
  d[p] = 0; return p;
}
static void set_msg(const char* a, int num, int with_num) {
  int p = put_str(msg, 0, (int)sizeof(msg), a);
  if (with_num) put_int(msg, p, (int)sizeof(msg), num);
}
static void fail(const char* a, int num, int with_num) { set_msg(a, num, with_num); st = S_ERR; }

// ---- ukuran wallpaper = ukuran layar penuh (firmware memakai gaya "cover") ----
// Viewport app hanya memberi lebar layar; sisi lainnya disimpulkan dari rasio 3:2 / 4:3.
static void target_size(int* w, int* h, int* portrait) {
  int vw = g_nyx->screen_w(), vh = g_nyx->screen_h();
  *portrait = vw < vh;
  if (!*portrait) { *w = vw; *h = (vw == 480) ? 320 : (vw == 320) ? 240 : vw * 2 / 3; }
  else            { *w = vw; *h = (vw == 320) ? 480 : (vw == 240) ? 320 : vw * 3 / 2; }
}

// ---- cek JPEG: harus baseline (JPEGDEC firmware tidak mendukung progresif) ----
static int jpeg_check(const uint8_t* b, int n, int* w, int* h, int* prog) {
  int p = 2;
  if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return 0;
  while (p + 4 <= n) {
    int m;
    if (b[p] != 0xFF) { p++; continue; }
    m = b[p + 1];
    if (m == 0xFF) { p++; continue; }
    if (m == 0xD8 || m == 0x01 || m == 0 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
    if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
      if (p + 9 > n) return 0;
      *h = (b[p + 5] << 8) | b[p + 6]; *w = (b[p + 7] << 8) | b[p + 8]; *prog = (m == 0xC2);
      return 1;
    }
    if (m == 0xDA) return 0;
    p += 2 + ((b[p + 2] << 8) | b[p + 3]);
  }
  return 0;
}

// ---- aksi ----
static void start_fetch(void) {
  const NyxApi* a = g_nyx; int w, h, port, p;
  target_size(&w, &h, &port);
  seed = (int)(a->rand() % 1000000);
  p = put_str(url, 0, (int)sizeof(url), "https://picsum.photos/");
  p = put_int(url, p, (int)sizeof(url), w);  p = put_str(url, p, (int)sizeof(url), "/");
  p = put_int(url, p, (int)sizeof(url), h);  p = put_str(url, p, (int)sizeof(url), ".jpg?random=");
  p = put_int(url, p, (int)sizeof(url), seed); put_str(url, p, (int)sizeof(url), MODE_Q[mode]);
  buf_n = 0;
  http_id = a->http_get(url, buf, BUF_CAP);
  if (http_id <= 0) { int e = http_id; http_id = 0; fail("http_get galat ", e, 1); return; }
  t_start = a->millis(); st = S_FETCH;
}

static void poll_fetch(void) {
  const NyxApi* a = g_nyx; int code = 0, s, prog = 0; size_t nb = 0;
  s = a->http_state(http_id, &code, &nb);
  if (s == NYX_HTTP_RUNNING) { if (a->millis() - t_start > TIMEOUT) { a->http_close(http_id); http_id = 0; fail("Waktu habis (WiFi?)", 0, 0); } return; }
  buf_n = (int)nb; a->http_close(http_id); http_id = 0;
  if (s != NYX_HTTP_DONE) { fail("Unduh gagal, kode ", code, 1); return; }
  if (code != 200)        { fail("Server balas HTTP ", code, 1); return; }
  if (buf_n >= BUF_CAP)   { fail("Gambar terlalu besar", 0, 0); return; }
  if (!jpeg_check(buf, buf_n, &jw, &jh, &prog)) { fail("Bukan JPEG valid", 0, 0); return; }
  if (prog)               { fail("JPEG progresif, ambil lagi", 0, 0); return; }
  st = S_GOT;
}

static void mark_applied(void) {            // penanda untuk firmware: muat ulang wallpaper saat app ditutup
  int h = g_nyx->fopen("applied", NYX_FOPEN_WRITE);
  if (h > 0) { g_nyx->fwrite(h, "1", 1); g_nyx->fclose(h); }
}

static void start_save(void) {
  int w, h, port;
  target_size(&w, &h, &port);
  fname[0] = 0; put_str(fname, 0, (int)sizeof(fname), port ? "wallpaper1.jpg" : "wallpaper2.jpg");
  save_h = g_nyx->fopen(fname, NYX_FOPEN_WRITE);
  if (save_h <= 0) { fail("Gagal buka file ", save_h, 1); return; }
  save_pos = 0; st = S_SAVE;
}

static void step_save(void) {
  int n = buf_n - save_pos; if (n > CHUNK) n = CHUNK;
  if (n > 0 && g_nyx->fwrite(save_h, buf + save_pos, n) != n) { g_nyx->fclose(save_h); fail("Tulis gagal (penuh?)", 0, 0); return; }
  save_pos += n;
  if (save_pos >= buf_n) {
    g_nyx->fclose(save_h); mark_applied(); st = S_DONE;
    g_nyx->toast("Wallpaper diterapkan");
  }
}

static void do_reset(void) {
  g_nyx->fremove("wallpaper1.jpg"); g_nyx->fremove("wallpaper2.jpg"); mark_applied();
  set_msg("Wallpaper dihapus. Tutup app.", 0, 0); st = S_DONE;
}

// ---- UI ----
static int hit(R r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
static void btn(R r, const char* s, uint16_t bg, int on) {
  const NyxApi* a = g_nyx;
  a->round_rect(r.x, r.y, r.w, r.h, 8, on ? bg : a->rgb(50, 50, 60));
  a->text(r.x + (r.w - a->text_w(s, 1)) / 2, r.y + (r.h - 8) / 2, s, on ? a->rgb(255, 255, 255) : a->rgb(120, 120, 130), 1);
}
static void bar(int x, int y, int w, int pct, uint16_t c) {
  g_nyx->rect(x, y, w, 8, g_nyx->rgb(90, 90, 110));
  if (pct > 0) g_nyx->fill_rect(x + 1, y + 1, (w - 2) * pct / 100, 6, c);
}

static int app_enter(void) {
  buf = (uint8_t*)g_nyx->mem_alloc(BUF_CAP);
  if (!buf) return -1;
  st = S_IDLE; mode = 0; http_id = 0; buf_n = 0; msg[0] = 0;
  return 0;
}

static void app_frame(void) {
  const NyxApi* a = g_nyx;
  int w = a->screen_w(), h = a->screen_h(), tx = 0, ty = 0, tw, th, port, pad = 8, bw, p, pct;
  int t = a->touch(&tx, &ty);
  char line[64];
  R rMode, rGet, rApply, rReset;
  uint16_t white = a->rgb(255, 255, 255), dim = a->rgb(150, 160, 190), acc = a->rgb(255, 220, 120);

  target_size(&tw, &th, &port);
  bw = (w - pad * 4) / 3;
  rMode  = (R){ pad, h - 90, w - pad * 2, 36 };
  rGet   = (R){ pad, h - 46, bw, 36 };
  rApply = (R){ pad * 2 + bw, h - 46, bw, 36 };
  rReset = (R){ pad * 3 + bw * 2, h - 46, bw, 36 };

  if (st == S_FETCH) poll_fetch();
  else if (st == S_SAVE) step_save();

  if (t & NYX_TOUCH_PRESSED) {
    int busy = (st == S_FETCH || st == S_SAVE);
    if (!busy && hit(rMode, tx, ty))  mode = (mode + 1) % 3;
    else if (!busy && hit(rGet, tx, ty)) start_fetch();
    else if (!busy && hit(rApply, tx, ty) && st == S_GOT) start_save();
    else if (!busy && hit(rReset, tx, ty)) do_reset();
  }

  a->clear(a->rgb(14, 14, 28));
  a->text(pad, 8, "Theme", white, 2);
  a->text(pad, 32, "Wallpaper dari picsum.photos", dim, 1);

  p = put_str(line, 0, (int)sizeof(line), "Target: ");
  p = put_int(line, p, (int)sizeof(line), tw); p = put_str(line, p, (int)sizeof(line), "x");
  p = put_int(line, p, (int)sizeof(line), th); p = put_str(line, p, (int)sizeof(line), port ? "  (potret)" : "  (lanskap)");
  a->text(pad, 52, line, white, 1);

  switch (st) {
    case S_IDLE:  a->text(pad, 80, "Tekan Ambil untuk foto acak.", dim, 1); break;
    case S_FETCH: {
      a->text(pad, 80, "Mengunduh...", acc, 1);
      pct = (int)((a->millis() - t_start) * 100u / TIMEOUT); if (pct > 100) pct = 100;
      bar(pad, 96, w - pad * 2, pct, acc);
      break;
    }
    case S_GOT:
      p = put_str(line, 0, (int)sizeof(line), "Foto #"); p = put_int(line, p, (int)sizeof(line), seed);
      p = put_str(line, p, (int)sizeof(line), "  "); p = put_int(line, p, (int)sizeof(line), buf_n / 1024);
      put_str(line, p, (int)sizeof(line), " KB");
      a->text(pad, 80, line, acc, 1);
      p = put_int(line, 0, (int)sizeof(line), jw); p = put_str(line, p, (int)sizeof(line), "x"); put_int(line, p, (int)sizeof(line), jh);
      a->text(pad, 96, line, dim, 1);
      a->text(pad, 116, "Tekan Terapkan, atau Ambil lagi.", dim, 1);
      break;
    case S_SAVE:
      a->text(pad, 80, "Menyimpan...", acc, 1);
      bar(pad, 96, w - pad * 2, buf_n ? save_pos * 100 / buf_n : 0, a->rgb(80, 200, 120));
      break;
    case S_DONE:
      a->text(pad, 80, msg[0] ? msg : "Tersimpan!", a->rgb(120, 230, 150), 1);
      a->text(pad, 96, "Tekan Back: Home memuat ulang.", dim, 1);
      break;
    case S_ERR:
      a->text(pad, 80, msg, a->rgb(255, 110, 110), 1);
      a->text(pad, 96, "Coba Ambil lagi.", dim, 1);
      break;
  }

  { char ml[40]; int q = put_str(ml, 0, (int)sizeof(ml), "Gaya: "); put_str(ml, q, (int)sizeof(ml), MODE_NAME[mode]); btn(rMode, ml, a->rgb(70, 70, 120), 1); }
  btn(rGet,   "Ambil",    a->rgb(48, 96, 208), st != S_FETCH && st != S_SAVE);
  btn(rApply, "Terapkan", a->rgb(32, 150, 90), st == S_GOT);
  btn(rReset, "Reset",    a->rgb(170, 60, 60), st != S_FETCH && st != S_SAVE);
}

static void app_leave(void) {
  if (http_id > 0) g_nyx->http_close(http_id);
  http_id = 0;
  if (buf) g_nyx->mem_free(buf);
  buf = 0;
}

NYX_APP_MAIN(app_enter, app_frame, app_leave)
