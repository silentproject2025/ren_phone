#pragma once
// =====================================================================
// nyxhost.h -- sisi HOST untuk app native NyxOS (portabel, tanpa Arduino)
// =====================================================================
// Mengisi tabel NyxApi (nyx_app.h) dan memegang semua keputusan kebijakan:
//   * manifest.txt  : parser key=value (nama, ikon, izin, quota, stack)
//   * izin          : fs / net / tilt / notify dicek di tiap fungsi API
//   * sandbox file  : app hanya menyentuh <dataDir>/<path relatif>
//   * memori        : alloc/free lewat arena app (nyxmem.h) + deteksi free liar
//   * jaringan      : TLS Gate (nyxmem.h) sebelum HTTPS, satu GET async
// Efek ke dunia luar (gambar, sentuh, file, HTTP) lewat Backend, sehingga
// logika ini diuji di PC (tests/nyxapp/) lalu dipakai apa adanya di firmware.
// Hanya satu app aktif pada satu waktu (API berupa pointer fungsi C tanpa
// konteks), jadi host aktif disimpan di satu penunjuk global (nyxHostCur()).
// =====================================================================

#include "nyx_app.h"
#include "nyxmem.h"

namespace nyx {

// ------------------------------ manifest ------------------------------
#define NYX_MF_NAME_MAX 24
#define NYX_MF_FILE_MAX 32
struct Manifest {
  char     name[NYX_MF_NAME_MAX];
  char     elf[NYX_MF_FILE_MAX];
  char     sym;
  uint16_t color;                 // RGB565
  uint32_t perms;
  uint32_t memKb;                 // quota arena
  uint32_t stackKb;               // stack task app
  bool     valid;
};

static inline uint16_t nyxRgb565(uint32_t r, uint32_t g, uint32_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static inline int nyxHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline bool nyxParseUInt(const char* s, uint32_t* out) {
  uint32_t v = 0; int n = 0;
  if (!s || !*s) return false;
  for (; *s; s++, n++) { if (*s < '0' || *s > '9' || n > 9) return false; v = v * 10 + (uint32_t)(*s - '0'); }
  *out = v; return n > 0;
}

// Format: baris "kunci=nilai", '#' = komentar. Kunci tak dikenal diabaikan.
//   name=Hello   sym=H   color=ff8800   perms=fs,net,tilt,notify
//   mem_kb=256   stack_kb=8   elf=app.elf
// valid = true hanya jika name ada & semua nilai dalam rentang.
static inline void parseManifest(const char* text, size_t len, Manifest* m) {
  memset(m, 0, sizeof(*m));
  strcpy(m->elf, "app.elf"); m->sym = '?'; m->color = nyxRgb565(0x40, 0x70, 0xC0);
  m->memKb = 256; m->stackKb = 8;
  bool bad = false; size_t i = 0;
  while (i < len) {
    char line[96]; size_t n = 0;
    while (i < len && text[i] != '\n') { if (n + 1 < sizeof(line) && text[i] != '\r') line[n++] = text[i]; i++; }
    i++; line[n] = 0;
    size_t a = 0; while (line[a] == ' ' || line[a] == '\t') a++;
    if (!line[a] || line[a] == '#') continue;
    char* eq = strchr(line + a, '='); if (!eq) continue;
    *eq = 0; char* key = line + a; char* val = eq + 1;
    size_t kl = strlen(key); while (kl && (key[kl - 1] == ' ' || key[kl - 1] == '\t')) key[--kl] = 0;
    while (*val == ' ' || *val == '\t') val++;
    size_t vl = strlen(val); while (vl && (val[vl - 1] == ' ' || val[vl - 1] == '\t')) val[--vl] = 0;
    if (!strcmp(key, "name")) {
      if (vl == 0 || vl >= NYX_MF_NAME_MAX) bad = true; else strcpy(m->name, val);
    } else if (!strcmp(key, "elf")) {
      bool ok = vl > 0 && vl < NYX_MF_FILE_MAX && !strchr(val, '/') && !strchr(val, '\\') && strcmp(val, "..") != 0 && val[0] != '.';
      if (ok) strcpy(m->elf, val); else bad = true;
    } else if (!strcmp(key, "sym")) {
      if (vl == 1 && (unsigned char)val[0] >= 0x21 && (unsigned char)val[0] < 0x7F) m->sym = val[0]; else bad = true;
    } else if (!strcmp(key, "color")) {
      if (vl == 6) {
        int h[6]; bool ok = true; for (int k = 0; k < 6; k++) { h[k] = nyxHexVal(val[k]); if (h[k] < 0) ok = false; }
        if (ok) m->color = nyxRgb565((uint32_t)(h[0] * 16 + h[1]), (uint32_t)(h[2] * 16 + h[3]), (uint32_t)(h[4] * 16 + h[5])); else bad = true;
      } else bad = true;
    } else if (!strcmp(key, "perms")) {
      uint32_t p = 0; char* t = val;
      while (*t) {
        char* e = strchr(t, ','); size_t tl = e ? (size_t)(e - t) : strlen(t);
        if (tl == 2 && !strncmp(t, "fs", 2)) p |= NYX_PERM_FS;
        else if (tl == 3 && !strncmp(t, "net", 3)) p |= NYX_PERM_NET;
        else if (tl == 4 && !strncmp(t, "tilt", 4)) p |= NYX_PERM_TILT;
        else if (tl == 6 && !strncmp(t, "notify", 6)) p |= NYX_PERM_NOTIFY;
        else if (tl > 0) bad = true;                               // izin tak dikenal: tolak manifest
        if (!e) { break; }
        t = e + 1;
      }
      m->perms = p;
    } else if (!strcmp(key, "mem_kb")) {
      uint32_t v; if (nyxParseUInt(val, &v) && v >= 64 && v <= 6144) m->memKb = v; else bad = true;
    } else if (!strcmp(key, "stack_kb")) {
      uint32_t v; if (nyxParseUInt(val, &v) && v >= 4 && v <= 32) m->stackKb = v; else bad = true;
    }
  }
  m->valid = !bad && m->name[0] != 0;
}

// --------------------------- sandbox path -----------------------------
#define NYX_PATH_MAX 160
// Path relatif saja: tanpa awalan '/', tanpa ".." / "." / komponen kosong, tanpa "\" ":" dan karakter kontrol.
static inline bool sandboxPath(const char* in, const char* dataDir, char* out, size_t cap) {
  if (!in || !in[0] || in[0] == '/') return false;
  size_t n = strlen(in); if (n > 48) return false;
  size_t dl = strlen(dataDir); if (dl + 1 + n + 1 > cap) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned char ch = (unsigned char)in[i];
    if (ch < 0x20 || ch == 0x7F || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') return false;
  }
  size_t start = 0;
  for (size_t i = 0; i <= n; i++) {
    if (i == n || in[i] == '/') {
      size_t l = i - start;
      if (l == 0) return false;
      if (l == 1 && in[start] == '.') return false;
      if (l == 2 && in[start] == '.' && in[start + 1] == '.') return false;
      start = i + 1;
    }
  }
  if (in[n - 1] == '/') return false;
  memcpy(out, dataDir, dl); out[dl] = '/'; memcpy(out + dl + 1, in, n); out[dl + 1 + n] = 0;
  return true;
}

// ------------------------------ backend -------------------------------
struct Backend {
  void* ctx;
  void (*clear)(void*, uint16_t c);
  void (*fillRect)(void*, int x, int y, int w, int h, uint16_t c);
  void (*rect)(void*, int x, int y, int w, int h, uint16_t c);
  void (*roundRect)(void*, int x, int y, int w, int h, int r, uint16_t c);
  void (*pixel)(void*, int x, int y, uint16_t c);
  void (*line)(void*, int x0, int y0, int x1, int y1, uint16_t c);
  void (*circle)(void*, int x, int y, int r, uint16_t c);
  void (*ring)(void*, int x, int y, int r, uint16_t c);
  void (*text)(void*, int x, int y, const char* s, uint16_t c, int size);
  int  (*textW)(void*, const char* s, int size);
  void (*blit)(void*, int x, int y, int w, int h, const uint16_t* px);
  void (*viewport)(void*, int* w, int* h);
  bool (*tilt)(void*, int* ax, int* ay, int* az);
  uint32_t (*millis)(void*);
  uint32_t (*rand32)(void*);
  void (*log)(void*, const char* s);
  void (*toast)(void*, const char* s);
  int  (*fopen)(void*, const char* path, int mode);
  int  (*fread)(void*, int h, void* buf, int n);
  int  (*fwrite)(void*, int h, const void* buf, int n);
  void (*fclose)(void*, int h);
  int  (*fseek)(void*, int h, int pos);
  int  (*fsize)(void*, int h);
  int  (*fexists)(void*, const char* path);
  int  (*fremove)(void*, const char* path);
  int  (*httpBegin)(void*, const char* url);                          // id>0 / <0
  int  (*httpPoll)(void*, int id, int* code, uint32_t* len);          // 1 jalan,2 selesai,3 gagal
  const uint8_t* (*httpData)(void*, int id);
  void (*httpClose)(void*, int id);
};

#define NYX_MAX_FILES 4

struct Host {
  Backend be; MemMgr* mm; int memId;
  uint32_t perms; char dataDir[64];
  int fh[NYX_MAX_FILES];
  int httpId; void* httpBuf; size_t httpCap; bool httpCopied;
  uint32_t tlsNeed;                       // blok kontigu internal untuk HTTPS (TLS Gate)
  uint32_t tlsGateMs;
  NyxApi api;
  NyxAppOps ops; bool opsSet; bool quitReq;
  // sentuh (diisi glue UI tiap frame)
  int touchState, touchX, touchY;
  // statistik
  uint32_t badFree, deniedCalls;
};

inline Host*& nyxHostCur() { static Host* cur = nullptr; return cur; }

namespace hostimpl {
#define H() Host* h = nyxHostCur()
static void   set_ops(const NyxAppOps* o) { H(); if (h && o) { h->ops = *o; h->opsSet = true; } }
static void   quit_() { H(); if (h) h->quitReq = true; }
static Arena* ar() { H(); return h ? h->mm->arena(h->memId) : nullptr; }
static void*  mem_alloc(size_t n) { Arena* a = ar(); return a ? a->alloc(n) : nullptr; }
static void*  mem_calloc(size_t n) { Arena* a = ar(); return a ? a->allocZ(n) : nullptr; }
static void*  mem_realloc(void* p, size_t n) { Arena* a = ar(); return a ? a->realloc(p, n) : nullptr; }
static void   mem_free(void* p) { H(); Arena* a = ar(); if (a && !a->free(p) && h) h->badFree++; }
static size_t mem_used() { Arena* a = ar(); return a ? a->reqBytes : 0; }
static size_t mem_quota() { Arena* a = ar(); return a ? a->size : 0; }
static size_t mem_largest() { Arena* a = ar(); return a ? a->largestFree() : 0; }

static int  screen_w() { H(); int w = 0, hh = 0; if (h && h->be.viewport) h->be.viewport(h->be.ctx, &w, &hh); return w; }
static int  screen_h() { H(); int w = 0, hh = 0; if (h && h->be.viewport) h->be.viewport(h->be.ctx, &w, &hh); return hh; }
static uint16_t rgb(int r, int g, int b) { return nyxRgb565((uint32_t)r, (uint32_t)g, (uint32_t)b); }
static void clear(uint16_t c) { H(); if (h && h->be.clear) h->be.clear(h->be.ctx, c); }
static void fill_rect(int x, int y, int w, int hh, uint16_t c) { H(); if (h && h->be.fillRect && w > 0 && hh > 0) h->be.fillRect(h->be.ctx, x, y, w, hh, c); }
static void rect(int x, int y, int w, int hh, uint16_t c) { H(); if (h && h->be.rect && w > 0 && hh > 0) h->be.rect(h->be.ctx, x, y, w, hh, c); }
static void round_rect(int x, int y, int w, int hh, int r, uint16_t c) { H(); if (h && h->be.roundRect && w > 0 && hh > 0 && r >= 0 && r <= 500) h->be.roundRect(h->be.ctx, x, y, w, hh, r, c); }
static void pixel(int x, int y, uint16_t c) { H(); if (h && h->be.pixel) h->be.pixel(h->be.ctx, x, y, c); }
static void line(int x0, int y0, int x1, int y1, uint16_t c) { H(); if (h && h->be.line) h->be.line(h->be.ctx, x0, y0, x1, y1, c); }
static void circle(int x, int y, int r, uint16_t c) { H(); if (h && h->be.circle && r >= 0 && r <= 2000) h->be.circle(h->be.ctx, x, y, r, c); }
static void ring(int x, int y, int r, uint16_t c) { H(); if (h && h->be.ring && r >= 0 && r <= 2000) h->be.ring(h->be.ctx, x, y, r, c); }
static int  clampSz(int s) { return s < 1 ? 1 : (s > 6 ? 6 : s); }
static void text(int x, int y, const char* s, uint16_t c, int sz) { H(); if (h && h->be.text && s) h->be.text(h->be.ctx, x, y, s, c, clampSz(sz)); }
static int  text_w(const char* s, int sz) { H(); if (!s) return 0; return (h && h->be.textW) ? h->be.textW(h->be.ctx, s, clampSz(sz)) : (int)strlen(s) * 6 * clampSz(sz); }
static void blit(int x, int y, int w, int hh, const uint16_t* px) {
  H(); if (h && h->be.blit && px && w > 0 && hh > 0 && w <= 2048 && hh <= 2048) h->be.blit(h->be.ctx, x, y, w, hh, px);
}
static int  touch(int* x, int* y) { H(); if (!h) return 0; if (x) *x = h->touchX; if (y) *y = h->touchY; return h->touchState; }
static int  tilt(int* x, int* y, int* z) {
  H(); if (!h) return NYX_E_INVAL;
  if (!(h->perms & NYX_PERM_TILT)) { h->deniedCalls++; return NYX_E_PERM; }
  int ax = 0, ay = 0, az = 0;
  if (!h->be.tilt || !h->be.tilt(h->be.ctx, &ax, &ay, &az)) return NYX_E_NOENT;
  if (x) { *x = ax; }
  if (y) { *y = ay; }
  if (z) { *z = az; }
  return 0;
}
static uint32_t millis_() { H(); return (h && h->be.millis) ? h->be.millis(h->be.ctx) : 0; }
static uint32_t rand_() { H(); return (h && h->be.rand32) ? h->be.rand32(h->be.ctx) : 4; }
static void log_(const char* s) { H(); if (h && h->be.log && s) h->be.log(h->be.ctx, s); }
static void toast(const char* s) {
  H(); if (!h || !s) return;
  if (!(h->perms & NYX_PERM_NOTIFY)) { h->deniedCalls++; return; }
  if (h->be.toast) h->be.toast(h->be.ctx, s);
}

// ---- file ----
static int fsPre(Host* h, const char* path, char* full, size_t cap) {
  if (!h) return NYX_E_INVAL;
  if (!(h->perms & NYX_PERM_FS)) { h->deniedCalls++; return NYX_E_PERM; }
  if (!sandboxPath(path, h->dataDir, full, cap)) return NYX_E_INVAL;
  return 0;
}
static int fopen_(const char* path, int mode) {
  H(); char full[NYX_PATH_MAX]; int rc = fsPre(h, path, full, sizeof(full)); if (rc < 0) return rc;
  if (mode < 0 || mode > 2) return NYX_E_INVAL;
  int slot = -1; for (int i = 0; i < NYX_MAX_FILES; i++) if (h->fh[i] < 0) { slot = i; break; }
  if (slot < 0) return NYX_E_BUSY;
  int fd = h->be.fopen ? h->be.fopen(h->be.ctx, full, mode) : -1;
  if (fd < 0) return NYX_E_NOENT;
  h->fh[slot] = fd; return slot + 1;
}
static bool slotOk(Host* h, int hd) { return h && hd >= 1 && hd <= NYX_MAX_FILES && h->fh[hd - 1] >= 0; }
static int fread_(int hd, void* buf, int n) {
  H(); if (!h) return NYX_E_INVAL; if (!(h->perms & NYX_PERM_FS)) { h->deniedCalls++; return NYX_E_PERM; }
  if (!slotOk(h, hd) || !buf || n < 0) return NYX_E_INVAL;
  return h->be.fread ? h->be.fread(h->be.ctx, h->fh[hd - 1], buf, n) : NYX_E_IO;
}
static int fwrite_(int hd, const void* buf, int n) {
  H(); if (!h) return NYX_E_INVAL; if (!(h->perms & NYX_PERM_FS)) { h->deniedCalls++; return NYX_E_PERM; }
  if (!slotOk(h, hd) || !buf || n < 0) return NYX_E_INVAL;
  return h->be.fwrite ? h->be.fwrite(h->be.ctx, h->fh[hd - 1], buf, n) : NYX_E_IO;
}
static int fclose_(int hd) {
  H(); if (!slotOk(h, hd)) return NYX_E_INVAL;
  if (h->be.fclose) { h->be.fclose(h->be.ctx, h->fh[hd - 1]); }
  h->fh[hd - 1] = -1; return 0;
}
static int fseek_(int hd, int pos) {
  H(); if (!slotOk(h, hd) || pos < 0) return NYX_E_INVAL;
  return (h->be.fseek && h->be.fseek(h->be.ctx, h->fh[hd - 1], pos) >= 0) ? 0 : NYX_E_IO;
}
static int fsize_(int hd) { H(); if (!slotOk(h, hd)) return NYX_E_INVAL; return h->be.fsize ? h->be.fsize(h->be.ctx, h->fh[hd - 1]) : NYX_E_IO; }
static int fexists_(const char* path) {
  H(); char full[NYX_PATH_MAX]; int rc = fsPre(h, path, full, sizeof(full)); if (rc < 0) return rc;
  return h->be.fexists ? h->be.fexists(h->be.ctx, full) : 0;
}
static int fremove_(const char* path) {
  H(); char full[NYX_PATH_MAX]; int rc = fsPre(h, path, full, sizeof(full)); if (rc < 0) return rc;
  return (h->be.fremove && h->be.fremove(h->be.ctx, full) >= 0) ? 0 : NYX_E_NOENT;
}

// ---- jaringan ----
static int http_get(const char* url, void* buf, size_t cap) {
  H(); if (!h || !url || !buf) return NYX_E_INVAL;
  if (!(h->perms & NYX_PERM_NET)) { h->deniedCalls++; return NYX_E_PERM; }
  if (!h->be.httpBegin) return NYX_E_INVAL;
  if (h->httpId > 0) return NYX_E_BUSY;
  bool tls = strncmp(url, "https://", 8) == 0;
  if (!tls && strncmp(url, "http://", 7) != 0) return NYX_E_INVAL;
  if (cap == 0 || cap > (256u * 1024u) || strlen(url) > 200) return NYX_E_INVAL;
  if (tls && h->mm && !h->mm->tlsGate(h->tlsNeed, h->tlsGateMs)) return NYX_E_NOMEM;   // RAM internal terlalu terfragmentasi
  int id = h->be.httpBegin(h->be.ctx, url);
  if (id <= 0) return id < 0 ? id : NYX_E_BUSY;
  h->httpId = id; h->httpBuf = buf; h->httpCap = cap; h->httpCopied = false;
  return id;
}
static int http_state(int id, int* code, size_t* nbytes) {
  H(); if (!h) return NYX_HTTP_IDLE;
  if (!(h->perms & NYX_PERM_NET)) { h->deniedCalls++; return NYX_HTTP_IDLE; }
  if (h->httpId <= 0 || id != h->httpId || !h->be.httpPoll) return NYX_HTTP_IDLE;
  int c = 0; uint32_t len = 0; int s = h->be.httpPoll(h->be.ctx, h->httpId, &c, &len);
  if (s == NYX_HTTP_DONE) {
    uint32_t n = len < h->httpCap ? len : (uint32_t)h->httpCap;
    if (!h->httpCopied) {
      const uint8_t* d = h->be.httpData ? h->be.httpData(h->be.ctx, h->httpId) : nullptr;
      if (d && n) memcpy(h->httpBuf, d, n);
      h->httpCopied = true;
    }
    len = n;
  }
  if (code) { *code = c; }
  if (nbytes) { *nbytes = len; }
  return s;
}
static void http_close(int id) {
  H(); if (!h || h->httpId <= 0 || id != h->httpId) return;
  if (h->be.httpClose) h->be.httpClose(h->be.ctx, h->httpId);
  h->httpId = 0;
}
#undef H
}  // namespace hostimpl

static inline void hostInit(Host* h, const Backend& be, MemMgr* mm, int memId, const Manifest& mf, const char* dataDir) {
  memset(h, 0, sizeof(*h));
  h->be = be; h->mm = mm; h->memId = memId; h->perms = mf.perms;
  strncpy(h->dataDir, dataDir, sizeof(h->dataDir) - 1);
  for (int i = 0; i < NYX_MAX_FILES; i++) h->fh[i] = -1;
  h->tlsNeed = 32 * 1024; h->tlsGateMs = 1500;
  NyxApi& a = h->api;
  a.magic = NYX_API_MAGIC; a.version = NYX_API_VERSION; a.size = (uint32_t)sizeof(NyxApi); a.perms = mf.perms;
  a.set_ops = hostimpl::set_ops; a.quit = hostimpl::quit_;
  a.mem_alloc = hostimpl::mem_alloc; a.mem_calloc = hostimpl::mem_calloc; a.mem_realloc = hostimpl::mem_realloc;
  a.mem_free = hostimpl::mem_free; a.mem_used = hostimpl::mem_used; a.mem_quota = hostimpl::mem_quota; a.mem_largest = hostimpl::mem_largest;
  a.screen_w = hostimpl::screen_w; a.screen_h = hostimpl::screen_h; a.rgb = hostimpl::rgb;
  a.clear = hostimpl::clear; a.fill_rect = hostimpl::fill_rect; a.rect = hostimpl::rect; a.round_rect = hostimpl::round_rect;
  a.pixel = hostimpl::pixel; a.line = hostimpl::line; a.circle = hostimpl::circle; a.ring = hostimpl::ring;
  a.text = hostimpl::text; a.text_w = hostimpl::text_w; a.blit = hostimpl::blit;
  a.touch = hostimpl::touch; a.tilt = hostimpl::tilt;
  a.millis = hostimpl::millis_; a.rand = hostimpl::rand_; a.log = hostimpl::log_; a.toast = hostimpl::toast;
  a.fopen = hostimpl::fopen_; a.fread = hostimpl::fread_; a.fwrite = hostimpl::fwrite_; a.fclose = hostimpl::fclose_;
  a.fseek = hostimpl::fseek_; a.fsize = hostimpl::fsize_; a.fexists = hostimpl::fexists_; a.fremove = hostimpl::fremove_;
  a.http_get = hostimpl::http_get; a.http_state = hostimpl::http_state; a.http_close = hostimpl::http_close;
}

// tutup semua handle & permintaan jaringan milik app (dipanggil saat app berhenti, termasuk kena kill)
static inline void hostCloseAll(Host* h) {
  for (int i = 0; i < NYX_MAX_FILES; i++)
    if (h->fh[i] >= 0) { if (h->be.fclose) h->be.fclose(h->be.ctx, h->fh[i]); h->fh[i] = -1; }
  if (h->httpId > 0 && h->be.httpClose) h->be.httpClose(h->be.ctx, h->httpId);
  h->httpId = 0;
}

static inline void hostSetTouch(Host* h, int state, int x, int y) { h->touchState = state; h->touchX = x; h->touchY = y; }

}  // namespace nyx
