// theme -- ganti wallpaper dari Picsum (gratis, tanpa API key).
// Alur: Ambil (unduh JPG sesuai ukuran layar) -> preview (thumbnail DC-only 1/8) -> Terapkan (simpan ke data/) -> tutup app,
// firmware memuat ulang wallpaper (butuh apply_theme_patch.py). Tidak memakai libc.
#include "nyx_app.h"
NYX_APP_GLOBALS

#define BUF_CAP  (120 * 1024)    // < 128 KB (batas buffer HTTP host)
#define CHUNK    4096            // tulis per frame supaya watchdog 250 ms aman di FFat
#define TIMEOUT  30000u

enum { S_IDLE, S_FETCH, S_PREV, S_GOT, S_SAVE, S_DONE, S_ERR };
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

// ---- preview: decode DC-only (thumbnail 1/8, tanpa IDCT) ----
// Baseline JPEG: hanya koefisien DC tiap blok 8x8 yang dipakai (= rata-rata blok); AC cukup dilewati.
#define TH_MAX 80
typedef struct { uint8_t bits[17], vals[256]; int maxcode[18], valptr[17], mincode[17]; } Huff;
typedef struct {
  int ok, bad, ncomp, W, H, ri, hmax, vmax, nmx, nmy, mx, my, rst, tw, thh;
  int ch[3], cv[3], ctq[3], cdc[3], cac[3], q0[4], pred[3], have[4];
  int pos, end, bc, cur;
  Huff hf[4];                          // 0,1 = DC; 2,3 = AC
  uint16_t th[TH_MAX * TH_MAX];        // thumbnail RGB565 (format rgb() API)
  uint16_t row[TH_MAX * 6 * 6];        // buffer satu baris thumbnail yang sudah diperbesar
} Prev;
static Prev* pv;

static int nextbit(void) {
  if (pv->bc == 0) {
    int b = 0;
    if (pv->pos < pv->end) {
      b = buf[pv->pos];
      if (b == 0xFF) {
        int b2 = (pv->pos + 1 < pv->end) ? buf[pv->pos + 1] : 0;
        if (b2 == 0) pv->pos += 2; else b = 0;      // marker: tidak dimakan, pasok nol
      } else pv->pos++;
    }
    pv->cur = b; pv->bc = 8;
  }
  pv->bc--;
  return (pv->cur >> pv->bc) & 1;
}
static int hdec(int idx) {
  const Huff* hf = &pv->hf[idx]; int code = 0, l;
  for (l = 1; l <= 16; l++) {
    code = (code << 1) | nextbit();
    if (hf->maxcode[l] >= 0 && code <= hf->maxcode[l]) return hf->vals[hf->valptr[l] + code - hf->mincode[l]];
  }
  return -1;
}
static int recv_ext(int n) {
  int v = 0, i;
  if (!n) return 0;
  for (i = 0; i < n; i++) v = (v << 1) | nextbit();
  if (v < (1 << (n - 1))) v -= (1 << n) - 1;
  return v;
}

static int pv_parse(void) {
  int p = 2, n = buf_n, k;
  pv->ok = pv->bad = pv->ncomp = pv->ri = pv->mx = pv->my = pv->rst = pv->bc = 0;
  for (k = 0; k < 4; k++) { pv->have[k] = 0; pv->q0[k] = 0; }
  for (k = 0; k < 3; k++) pv->pred[k] = 0;
  while (p + 4 <= n) {
    int m, len; const uint8_t* q; int sl;
    if (buf[p] != 0xFF) { p++; continue; }
    m = buf[p + 1];
    if (m == 0xFF) { p++; continue; }
    if (m == 0xD8 || m == 0x01 || m == 0 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
    len = (buf[p + 2] << 8) | buf[p + 3];
    if (len < 2 || p + 2 + len > n) return 0;
    q = buf + p + 4; sl = len - 2;
    if (m == 0xDB) {
      while (sl >= 65) {
        int pq = q[0] >> 4, tq = q[0] & 15, step = 1 + 64 * (pq + 1);
        if (sl < step) break;
        if (tq < 4) pv->q0[tq] = pq ? ((q[1] << 8) | q[2]) : q[1];
        q += step; sl -= step;
      }
    } else if (m == 0xC4) {
      while (sl >= 17) {
        int tc = q[0] >> 4, tn = q[0] & 15, tot = 0, l, code = 0, kk = 0; Huff* hf;
        if (tc > 1 || tn > 1) return 0;
        hf = &pv->hf[tc * 2 + tn];
        for (l = 1; l <= 16; l++) { hf->bits[l] = q[l]; tot += q[l]; }
        if (tot > 256 || sl < 17 + tot) return 0;
        for (l = 0; l < tot; l++) hf->vals[l] = q[17 + l];
        for (l = 1; l <= 16; l++) {
          hf->valptr[l] = kk; hf->mincode[l] = code; code += hf->bits[l]; kk += hf->bits[l];
          hf->maxcode[l] = hf->bits[l] ? code - 1 : -1; code <<= 1;
        }
        pv->have[tc * 2 + tn] = 1;
        q += 17 + tot; sl -= 17 + tot;
      }
    } else if (m == 0xDD) {
      if (sl >= 2) pv->ri = (q[0] << 8) | q[1];
    } else if (m == 0xC0 || m == 0xC1) {
      if (sl < 6 || q[0] != 8) return 0;
      pv->H = (q[1] << 8) | q[2]; pv->W = (q[3] << 8) | q[4]; pv->ncomp = q[5];
      if ((pv->ncomp != 1 && pv->ncomp != 3) || sl < 6 + 3 * pv->ncomp) return 0;
      for (k = 0; k < pv->ncomp; k++) { pv->ch[k] = q[7 + k * 3] >> 4; pv->cv[k] = q[7 + k * 3] & 15; pv->ctq[k] = q[8 + k * 3] & 3; }
    } else if (m == 0xDA) {
      if (!pv->ncomp || sl < 1 + 2 * pv->ncomp || q[0] != pv->ncomp) return 0;
      for (k = 0; k < pv->ncomp; k++) { pv->cdc[k] = q[2 + k * 2] >> 4; pv->cac[k] = q[2 + k * 2] & 15; }
      pv->pos = p + 2 + len; pv->end = n;
      return 1;
    }
    p += 2 + len;
  }
  return 0;
}

static int pv_setup(void) {
  int i;
  if (pv->W <= 0 || pv->H <= 0) return 0;
  if (pv->ncomp == 1) pv->ch[0] = pv->cv[0] = 1;       // scan 1 komponen: MCU = satu blok
  pv->hmax = pv->ch[0]; pv->vmax = pv->cv[0];
  if (pv->hmax < 1 || pv->hmax > 2 || pv->vmax < 1 || pv->vmax > 2) return 0;
  for (i = 0; i < pv->ncomp; i++) {
    if (i > 0 && (pv->ch[i] != 1 || pv->cv[i] != 1)) return 0;
    if (pv->cdc[i] > 1 || pv->cac[i] > 1 || !pv->have[pv->cdc[i]] || !pv->have[2 + pv->cac[i]]) return 0;
  }
  pv->nmx = (pv->W + 8 * pv->hmax - 1) / (8 * pv->hmax);
  pv->nmy = (pv->H + 8 * pv->vmax - 1) / (8 * pv->vmax);
  pv->tw = pv->nmx * pv->hmax; pv->thh = pv->nmy * pv->vmax;
  return pv->tw <= TH_MAX && pv->thh <= TH_MAX;
}

static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void pv_mcu(void) {
  int yv[4] = { 128, 128, 128, 128 }, cb = 128, cr = 128, ci, bx, by, k;
  for (ci = 0; ci < pv->ncomp; ci++) {
    int nb = (ci == 0) ? pv->hmax * pv->vmax : 1, b;
    for (b = 0; b < nb; b++) {
      int sz = hdec(pv->cdc[ci]), v;
      if (sz < 0 || sz > 11) { pv->bad = 1; return; }
      pv->pred[ci] += recv_ext(sz);
      v = ((pv->pred[ci] * pv->q0[pv->ctq[ci]]) >> 3) + 128;      // DC/8 + 128 = rata-rata blok
      for (k = 1; k < 64; ) {                                     // lewati 63 koefisien AC
        int rs = hdec(2 + pv->cac[ci]), r, s2;
        if (rs < 0) { pv->bad = 1; return; }
        r = rs >> 4; s2 = rs & 15;
        if (!s2) { if (r == 15) { k += 16; continue; } break; }
        k += r + 1;
        while (s2--) nextbit();
      }
      if (ci == 0) yv[b] = v; else if (ci == 1) cb = v; else cr = v;
    }
  }
  for (by = 0; by < pv->vmax; by++) for (bx = 0; bx < pv->hmax; bx++) {
    int y = yv[by * pv->hmax + bx], d1 = cb - 128, d2 = cr - 128;
    int r = clamp8(y + ((359 * d2) >> 8)), g = clamp8(y - ((88 * d1 + 183 * d2) >> 8)), bl = clamp8(y + ((454 * d1) >> 8));
    pv->th[(pv->my * pv->vmax + by) * pv->tw + pv->mx * pv->hmax + bx] = g_nyx->rgb(r, g, bl);
  }
}

static void pv_step(void) {                 // dicicil: maks ~25 ms per frame
  uint32_t t0 = g_nyx->millis(); int cnt = 0;
  while (pv->my < pv->nmy && !pv->bad) {
    pv_mcu();
    if (pv->bad) break;
    if (++pv->mx >= pv->nmx) { pv->mx = 0; pv->my++; }
    if (pv->ri && ++pv->rst >= pv->ri) {
      pv->rst = 0;
      if (pv->my < pv->nmy) {
        pv->bc = 0; pv->pred[0] = pv->pred[1] = pv->pred[2] = 0;
        while (pv->pos + 1 < pv->end && !(buf[pv->pos] == 0xFF && buf[pv->pos + 1] >= 0xD0 && buf[pv->pos + 1] <= 0xD7)) pv->pos++;
        pv->pos += 2;
      }
    }
    if ((++cnt & 15) == 0 && g_nyx->millis() - t0 > 25) return;
  }
  pv->ok = !pv->bad; st = S_GOT;            // gagal decode -> tetap boleh Terapkan, tanpa preview
}

static void pv_begin(void) {
  if (pv_parse() && pv_setup()) st = S_PREV; else { pv->ok = 0; st = S_GOT; }
}

static void draw_preview(int ax, int ay, int aw, int ah) {
  int sc, ox, oy, dw, dh, ty, tx, r, j;
  if (!pv->ok) return;
  sc = aw / pv->tw; if (ah / pv->thh < sc) sc = ah / pv->thh;
  if (sc < 1) sc = 1;
  if (sc > 6) sc = 6;
  dw = pv->tw * sc; dh = pv->thh * sc; ox = ax + (aw - dw) / 2; oy = ay + (ah - dh) / 2;
  g_nyx->rect(ox - 1, oy - 1, dw + 2, dh + 2, g_nyx->rgb(110, 110, 140));
  for (ty = 0; ty < pv->thh; ty++) {
    for (r = 0; r < sc; r++) for (tx = 0; tx < pv->tw; tx++) {
      uint16_t px = pv->th[ty * pv->tw + tx];
      for (j = 0; j < sc; j++) pv->row[(r * pv->tw + tx) * sc + j] = px;
    }
    g_nyx->blit(ox, oy + ty * sc, dw, sc, pv->row);
  }
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
  buf_n = 0; pv->ok = 0;
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
  pv_begin();
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
  pv = (Prev*)g_nyx->mem_calloc(sizeof(Prev));
  if (!buf || !pv) { if (buf) g_nyx->mem_free(buf); if (pv) g_nyx->mem_free(pv); buf = 0; pv = 0; return -1; }
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
  else if (st == S_PREV) pv_step();
  else if (st == S_SAVE) step_save();

  if (t & NYX_TOUCH_PRESSED) {
    int busy = (st == S_FETCH || st == S_PREV || st == S_SAVE);
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
    case S_PREV:
      a->text(pad, 80, "Menyiapkan preview...", acc, 1);
      bar(pad, 96, 150, pv->nmy ? pv->my * 100 / pv->nmy : 0, acc);
      break;
    case S_GOT:
      p = put_str(line, 0, (int)sizeof(line), "Foto #"); p = put_int(line, p, (int)sizeof(line), seed);
      p = put_str(line, p, (int)sizeof(line), "  "); p = put_int(line, p, (int)sizeof(line), buf_n / 1024);
      put_str(line, p, (int)sizeof(line), " KB");
      a->text(pad, 80, line, acc, 1);
      p = put_int(line, 0, (int)sizeof(line), jw); p = put_str(line, p, (int)sizeof(line), "x"); put_int(line, p, (int)sizeof(line), jh);
      a->text(pad, 96, line, dim, 1);
      a->text(pad, 116, pv->ok ? "Terapkan / Ambil lagi" : "Tanpa preview. Terapkan?", dim, 1);
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

  if (pv->ok && (st == S_GOT || st == S_SAVE || st == S_DONE)) {
    if (!port) draw_preview(200, 8, w - 200 - pad, rMode.y - 16);
    else       draw_preview(pad, 132, w - pad * 2, rMode.y - 138);
  }

  { char ml[40]; int q = put_str(ml, 0, (int)sizeof(ml), "Gaya: "); put_str(ml, q, (int)sizeof(ml), MODE_NAME[mode]); btn(rMode, ml, a->rgb(70, 70, 120), 1); }
  btn(rGet,   "Ambil",    a->rgb(48, 96, 208), st != S_FETCH && st != S_PREV && st != S_SAVE);
  btn(rApply, "Terapkan", a->rgb(32, 150, 90), st == S_GOT);
  btn(rReset, "Reset",    a->rgb(170, 60, 60), st != S_FETCH && st != S_PREV && st != S_SAVE);
}

static void app_leave(void) {
  if (http_id > 0) g_nyx->http_close(http_id);
  http_id = 0;
  if (buf) g_nyx->mem_free(buf);
  buf = 0;
  if (pv) g_nyx->mem_free(pv);
  pv = 0;
}

NYX_APP_MAIN(app_enter, app_frame, app_leave)
