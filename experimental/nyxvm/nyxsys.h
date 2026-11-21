#pragma once
// =====================================================================
// nyxsys.h -- lapisan syscall NyxSys yang PORTABEL (tanpa Arduino)
// =====================================================================
// Semua keputusan keamanan ada di sini, bukan di glue firmware:
//   * pengecekan izin per syscall (NYX_PERM_*)
//   * sandbox path: app hanya boleh menyentuh <dataDir>/<path relatif>
//   * tabel handle file (maks 4) -- app tak pernah memegang handle OS
//   * argumen pointer divalidasi lewat MMU (pointer jelek => NYX_E_FAULT,
//     bukan crash; kegagalan keras seperti OOM/IO tetap mematikan proses)
// Efek ke dunia luar (gambar, sentuh, file, jaringan) lewat SysBackend:
// di firmware = LovyanGFX/SD_MMC/HTTPClient, di PC = framebuffer + folder.
// =====================================================================

#include "nyxvm_core.h"

namespace nyx {

struct SysBackend {
  void* ctx;
  // ---- gfx (koordinat viewport app, sudah di-clip oleh backend) ----
  void (*clear)(void*, uint16_t c);
  void (*fillRect)(void*, int x, int y, int w, int h, uint16_t c);
  void (*rect)(void*, int x, int y, int w, int h, uint16_t c);
  void (*pixel)(void*, int x, int y, uint16_t c);
  void (*line)(void*, int x0, int y0, int x1, int y1, uint16_t c);
  void (*circle)(void*, int x, int y, int r, uint16_t c);
  void (*ring)(void*, int x, int y, int r, uint16_t c);
  void (*roundRect)(void*, int x, int y, int w, int h, int r, uint16_t c);
  void (*text)(void*, int x, int y, const char* s, uint16_t c, int size);
  int  (*textW)(void*, const char* s, int size);
  void (*blit)(void*, int x, int y, int w, int h, const uint16_t* px);
  void (*viewport)(void*, int* w, int* h);
  // ---- input ----
  void (*touch)(void*, int* state, int* x, int* y);          // bit 1 down, 2 baru, 4 lepas
  bool (*tilt)(void*, int* ax, int* ay, int* az);            // satuan 1/1000 g
  // ---- sistem ----
  uint32_t (*millis)(void*);
  uint32_t (*rand32)(void*);
  void (*log)(void*, const char* s, int len);
  void (*toast)(void*, const char* s);
  // ---- fs: path sudah disandbox & berawalan dataDir ----
  int  (*fopen)(void*, const char* path, int mode);          // handle >=0 atau <0
  int  (*fread)(void*, int h, void* buf, int n);
  int  (*fwrite)(void*, int h, const void* buf, int n);
  void (*fclose)(void*, int h);
  int  (*fseek)(void*, int h, int pos);
  int  (*fsize)(void*, int h);
  int  (*fexists)(void*, const char* path);
  int  (*fremove)(void*, const char* path);
  // ---- jaringan (async; boleh null) ----
  int  (*httpBegin)(void*, const char* url);                 // id>0 atau <0 (sibuk/gagal)
  int  (*httpPoll)(void*, int id, int* httpCode, uint32_t* len);   // 1 jalan,2 selesai,3 gagal
  const uint8_t* (*httpData)(void*, int id);
  void (*httpClose)(void*, int id);
};

#define NYX_SYS_MAX_FILES 4
#define NYX_SYS_PATH_MAX  96

struct SysState {
  char     dataDir[64];                  // mis. "/apps/hello/data"
  int      fh[NYX_SYS_MAX_FILES];        // handle backend, -1 = kosong
  uint32_t rng;
  int      httpId; uint32_t httpBuf, httpCap; bool httpCopied;
};

struct SysEnv { SysBackend be; SysState st; };

inline void sysStateReset(SysEnv* e, const char* dataDir, uint32_t seed) {
  memset(&e->st, 0, sizeof(e->st));
  strncpy(e->st.dataDir, dataDir, sizeof(e->st.dataDir) - 1);
  for (int i = 0; i < NYX_SYS_MAX_FILES; i++) e->st.fh[i] = -1;
  e->st.rng = seed ? seed : 0x2545F491u;
}

// tutup semua handle & permintaan jaringan (dipanggil saat proses berakhir)
inline void sysStateClose(SysEnv* e) {
  for (int i = 0; i < NYX_SYS_MAX_FILES; i++)
    if (e->st.fh[i] >= 0) { if (e->be.fclose) e->be.fclose(e->be.ctx, e->st.fh[i]); e->st.fh[i] = -1; }
  if (e->st.httpId > 0 && e->be.httpClose) e->be.httpClose(e->be.ctx, e->st.httpId);
  e->st.httpId = 0;
}

// Sandbox path: relatif saja, tanpa "..", tanpa "\", ":" dan karakter kontrol.
inline bool sysSandboxPath(const char* in, const char* dataDir, char* out, size_t cap) {
  if (!in || !in[0] || in[0] == '/') return false;
  size_t n = strlen(in); if (n > 48) return false;
  size_t dl = strlen(dataDir); if (dl + 1 + n + 1 > cap) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned char ch = (unsigned char)in[i];
    if (ch < 0x20 || ch == 0x7F || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') return false;
  }
  // komponen "." atau ".." dan komponen kosong ("//") ditolak
  size_t start = 0;
  for (size_t i = 0; i <= n; i++) {
    if (i == n || in[i] == '/') {
      size_t len = i - start;
      if (len == 0) return false;
      if (len == 1 && in[start] == '.') return false;
      if (len == 2 && in[start] == '.' && in[start + 1] == '.') return false;
      start = i + 1;
    }
  }
  if (in[n - 1] == '/') return false;
  memcpy(out, dataDir, dl); out[dl] = '/'; memcpy(out + dl + 1, in, n); out[dl + 1 + n] = 0;
  return true;
}

// kegagalan akses memori: pointer jelek => galat lunak; OOM/IO => fault keras
static inline bool sysSoftFault(Proc* p) {
  if (p->status != PS_FAULT) return true;
  if (p->faultCode == NYX_FAULT_BADADDR || p->faultCode == NYX_FAULT_PROT || p->faultCode == NYX_FAULT_STACK) {
    p->status = PS_READY; p->faultCode = 0; p->faultAddr = 0; if (p->nFault) p->nFault--; return true;
  }
  return false;
}

static inline int sysRet(Proc* p, int32_t v) { p->r[0] = (uint32_t)v; return NYX_SYSRC_CONT; }

static inline uint16_t sysRgb565(uint32_t r, uint32_t g, uint32_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

inline int sysDispatch(void* vctx, Proc* p, uint32_t num) {
  SysEnv* e = (SysEnv*)vctx; SysBackend& be = e->be; SysState& st = e->st;
  uint32_t* R = p->r; char tmp[160];
  switch (num) {
    case NYX_SYS_EXIT:   p->exitCode = (int32_t)R[0]; return NYX_SYSRC_EXIT;
    case NYX_SYS_YIELD:  return NYX_SYSRC_YIELD;
    case NYX_SYS_TIME_MS: return sysRet(p, (int32_t)(be.millis ? be.millis(be.ctx) : 0));
    case NYX_SYS_RAND: {
      uint32_t x = st.rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; st.rng = x ? x : 0x2545F491u;
      return sysRet(p, (int32_t)st.rng);
    }
    case NYX_SYS_SEED: st.rng = R[0] ? R[0] : 0x2545F491u; return sysRet(p, 0);
    case NYX_SYS_LOG: {
      uint32_t len = R[1]; if (len > 200) len = 200;
      char buf[201];
      if (!p->readBytes(R[0], buf, len)) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      buf[len] = 0; if (be.log) be.log(be.ctx, buf, (int)len); return sysRet(p, 0);
    }
    case NYX_SYS_SBRK: {
      uint32_t old = 0;
      if (!p->sbrk((int32_t)R[0], &old)) return sysRet(p, NYX_E_NOMEM);
      return sysRet(p, (int32_t)old);
    }
    case NYX_SYS_SCREEN: {
      int w = 0, h = 0; if (be.viewport) be.viewport(be.ctx, &w, &h);
      R[0] = (uint32_t)w; R[1] = (uint32_t)h; return NYX_SYSRC_CONT;
    }
    case NYX_SYS_STATS: {
      uint32_t v = 0;
      switch (R[0]) {
        case 0: v = p->nPageIn; break; case 1: v = p->nSwapIn; break; case 2: v = p->nSwapOut; break;
        case 3: v = p->rss; break; case 4: v = p->nTlbMiss; break; case 5: v = p->mem->nFrames; break;
        default: return sysRet(p, NYX_E_INVAL);
      }
      return sysRet(p, (int32_t)v);
    }
    // ---------------- grafis ----------------
    case NYX_SYS_CLEAR:     if (be.clear) be.clear(be.ctx, (uint16_t)R[0]); return sysRet(p, 0);
    case NYX_SYS_FILL_RECT: if (be.fillRect) be.fillRect(be.ctx, (int)R[0], (int)R[1], (int)R[2], (int)R[3], (uint16_t)R[4]); return sysRet(p, 0);
    case NYX_SYS_RECT:      if (be.rect) be.rect(be.ctx, (int)R[0], (int)R[1], (int)R[2], (int)R[3], (uint16_t)R[4]); return sysRet(p, 0);
    case NYX_SYS_PIXEL:     if (be.pixel) be.pixel(be.ctx, (int)R[0], (int)R[1], (uint16_t)R[2]); return sysRet(p, 0);
    case NYX_SYS_LINE:      if (be.line) be.line(be.ctx, (int)R[0], (int)R[1], (int)R[2], (int)R[3], (uint16_t)R[4]); return sysRet(p, 0);
    case NYX_SYS_CIRCLE:    { int r = (int)R[2]; if (r < 0 || r > 2000) return sysRet(p, NYX_E_INVAL);
                              if (be.circle) { be.circle(be.ctx, (int)R[0], (int)R[1], r, (uint16_t)R[3]); } return sysRet(p, 0); }
    case NYX_SYS_RING:      { int r = (int)R[2]; if (r < 0 || r > 2000) return sysRet(p, NYX_E_INVAL);
                              if (be.ring) { be.ring(be.ctx, (int)R[0], (int)R[1], r, (uint16_t)R[3]); } return sysRet(p, 0); }
    case NYX_SYS_ROUND_RECT:{ int r = (int)R[4]; if (r < 0 || r > 500) return sysRet(p, NYX_E_INVAL);
                              if (be.roundRect) { be.roundRect(be.ctx, (int)R[0], (int)R[1], (int)R[2], (int)R[3], r, (uint16_t)R[5]); } return sysRet(p, 0); }
    case NYX_SYS_RGB:       return sysRet(p, (int32_t)sysRgb565(R[0], R[1], R[2]));
    case NYX_SYS_TEXT: {
      int n = p->readCStr(R[2], tmp, 129);
      if (n < 0) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      int sz = (int)R[4]; if (sz < 1) sz = 1; if (sz > 6) sz = 6;
      if (be.text) be.text(be.ctx, (int)R[0], (int)R[1], tmp, (uint16_t)R[3], sz);
      return sysRet(p, 0);
    }
    case NYX_SYS_TEXT_W: {
      int n = p->readCStr(R[0], tmp, 129);
      if (n < 0) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      int sz = (int)R[1]; if (sz < 1) sz = 1; if (sz > 6) sz = 6;
      return sysRet(p, be.textW ? be.textW(be.ctx, tmp, sz) : n * 6 * sz);
    }
    case NYX_SYS_BLIT: {
      int x = (int)R[0], y = (int)R[1], w = (int)R[2], h = (int)R[3]; uint32_t src = R[4];
      if (w <= 0 || h <= 0 || w > 480 || h > 480) return sysRet(p, NYX_E_INVAL);
      uint16_t row[480];
      for (int j = 0; j < h; j++) {
        if (!p->readBytes(src + (uint32_t)j * (uint32_t)w * 2u, row, (uint32_t)w * 2u)) {
          if (sysSoftFault(p)) { return sysRet(p, NYX_E_FAULT); }
          return NYX_SYSRC_CONT;
        }
        if (be.blit) be.blit(be.ctx, x, y + j, w, 1, row);
      }
      return sysRet(p, 0);
    }
    // ---------------- input ----------------
    case NYX_SYS_TOUCH: {
      int s = 0, x = 0, y = 0; if (be.touch) be.touch(be.ctx, &s, &x, &y);
      R[0] = (uint32_t)s; R[1] = (uint32_t)x; R[2] = (uint32_t)y; return NYX_SYSRC_CONT;
    }
    case NYX_SYS_TILT: {
      if (!(p->perms & NYX_PERM_TILT)) return sysRet(p, NYX_E_PERM);
      int ax = 0, ay = 0, az = 0;
      if (!be.tilt || !be.tilt(be.ctx, &ax, &ay, &az)) return sysRet(p, NYX_E_NOENT);
      R[0] = 0; R[1] = (uint32_t)ax; R[2] = (uint32_t)ay; R[3] = (uint32_t)az; return NYX_SYSRC_CONT;
    }
    // ---------------- file (sandbox) ----------------
    case NYX_SYS_FOPEN: case NYX_SYS_FEXISTS: case NYX_SYS_FREMOVE: {
      if (!(p->perms & NYX_PERM_FS)) return sysRet(p, NYX_E_PERM);
      int n = p->readCStr(R[0], tmp, 64);
      if (n < 0) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      char full[NYX_SYS_PATH_MAX + 64];
      if (!sysSandboxPath(tmp, st.dataDir, full, sizeof(full))) return sysRet(p, NYX_E_INVAL);
      if (num == NYX_SYS_FEXISTS) return sysRet(p, be.fexists ? be.fexists(be.ctx, full) : 0);
      if (num == NYX_SYS_FREMOVE) return sysRet(p, (be.fremove && be.fremove(be.ctx, full) >= 0) ? 0 : NYX_E_NOENT);
      int mode = (int)R[1]; if (mode < 0 || mode > 2) return sysRet(p, NYX_E_INVAL);
      int slot = -1; for (int i = 0; i < NYX_SYS_MAX_FILES; i++) if (st.fh[i] < 0) { slot = i; break; }
      if (slot < 0) return sysRet(p, NYX_E_BUSY);
      int h = be.fopen ? be.fopen(be.ctx, full, mode) : -1;
      if (h < 0) return sysRet(p, NYX_E_NOENT);
      st.fh[slot] = h; return sysRet(p, slot + 1);
    }
    case NYX_SYS_FREAD: case NYX_SYS_FWRITE: {
      if (!(p->perms & NYX_PERM_FS)) return sysRet(p, NYX_E_PERM);
      int slot = (int)R[0] - 1; if (slot < 0 || slot >= NYX_SYS_MAX_FILES || st.fh[slot] < 0) return sysRet(p, NYX_E_INVAL);
      uint32_t va = R[1], n = R[2]; if (n > (1u << 20)) n = (1u << 20);
      uint8_t buf[512]; uint32_t done = 0;
      while (done < n) {
        uint32_t chunk = n - done; if (chunk > sizeof(buf)) chunk = sizeof(buf);
        if (num == NYX_SYS_FREAD) {
          int got = be.fread ? be.fread(be.ctx, st.fh[slot], buf, (int)chunk) : -1;
          if (got < 0) return sysRet(p, done ? (int32_t)done : NYX_E_IO);
          if (got == 0) break;
          if (!p->writeBytes(va + done, buf, (uint32_t)got)) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
          done += (uint32_t)got; if ((uint32_t)got < chunk) break;
        } else {
          if (!p->readBytes(va + done, buf, chunk)) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
          int put = be.fwrite ? be.fwrite(be.ctx, st.fh[slot], buf, (int)chunk) : -1;
          if (put < 0) return sysRet(p, done ? (int32_t)done : NYX_E_IO);
          done += (uint32_t)put; if ((uint32_t)put < chunk) break;
        }
      }
      return sysRet(p, (int32_t)done);
    }
    case NYX_SYS_FCLOSE: {
      int slot = (int)R[0] - 1; if (slot < 0 || slot >= NYX_SYS_MAX_FILES || st.fh[slot] < 0) return sysRet(p, NYX_E_INVAL);
      if (be.fclose) { be.fclose(be.ctx, st.fh[slot]); } st.fh[slot] = -1; return sysRet(p, 0);
    }
    case NYX_SYS_FSEEK: case NYX_SYS_FSIZE: {
      int slot = (int)R[0] - 1; if (slot < 0 || slot >= NYX_SYS_MAX_FILES || st.fh[slot] < 0) return sysRet(p, NYX_E_INVAL);
      if (num == NYX_SYS_FSIZE) return sysRet(p, be.fsize ? be.fsize(be.ctx, st.fh[slot]) : NYX_E_IO);
      return sysRet(p, (be.fseek && be.fseek(be.ctx, st.fh[slot], (int)R[1]) >= 0) ? 0 : NYX_E_IO);
    }
    // ---------------- notifikasi ----------------
    case NYX_SYS_TOAST: {
      if (!(p->perms & NYX_PERM_NOTIFY)) return sysRet(p, NYX_E_PERM);
      int n = p->readCStr(R[0], tmp, 80);
      if (n < 0) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      if (be.toast) { be.toast(be.ctx, tmp); } return sysRet(p, 0);
    }
    // ---------------- jaringan ----------------
    case NYX_SYS_HTTP_GET: {
      if (!(p->perms & NYX_PERM_NET)) return sysRet(p, NYX_E_PERM);
      if (!be.httpBegin) return sysRet(p, NYX_E_NOSYS);
      if (st.httpId > 0) return sysRet(p, NYX_E_BUSY);
      int n = p->readCStr(R[0], tmp, 160);
      if (n < 0) { if (sysSoftFault(p)) return sysRet(p, NYX_E_FAULT); return NYX_SYSRC_CONT; }
      if (strncmp(tmp, "http://", 7) != 0 && strncmp(tmp, "https://", 8) != 0) return sysRet(p, NYX_E_INVAL);
      uint32_t cap = R[2]; if (cap == 0 || cap > (256u * 1024u)) return sysRet(p, NYX_E_INVAL);
      int id = be.httpBegin(be.ctx, tmp);
      if (id <= 0) return sysRet(p, id < 0 ? id : NYX_E_BUSY);
      st.httpId = id; st.httpBuf = R[1]; st.httpCap = cap; st.httpCopied = false;
      return sysRet(p, id);
    }
    case NYX_SYS_HTTP_STATE: {
      if (!(p->perms & NYX_PERM_NET)) return sysRet(p, NYX_E_PERM);
      if (st.httpId <= 0 || (int)R[0] != st.httpId || !be.httpPoll) { R[0] = 0; R[1] = 0; R[2] = 0; return NYX_SYSRC_CONT; }
      int code = 0; uint32_t len = 0; int s = be.httpPoll(be.ctx, st.httpId, &code, &len);
      if (s == 2 && !st.httpCopied) {
        uint32_t n = len < st.httpCap ? len : st.httpCap;
        const uint8_t* d = be.httpData ? be.httpData(be.ctx, st.httpId) : nullptr;
        if (d && n && !p->writeBytes(st.httpBuf, d, n)) { if (sysSoftFault(p)) { R[0] = 3; R[1] = NYX_E_FAULT; R[2] = 0; return NYX_SYSRC_CONT; } return NYX_SYSRC_CONT; }
        st.httpCopied = true; len = n;
      } else if (s == 2) { len = len < st.httpCap ? len : st.httpCap; }
      R[0] = (uint32_t)s; R[1] = (uint32_t)code; R[2] = len; return NYX_SYSRC_CONT;
    }
    case NYX_SYS_HTTP_CLOSE: {
      if (st.httpId > 0 && (int)R[0] == st.httpId) { if (be.httpClose) be.httpClose(be.ctx, st.httpId); st.httpId = 0; return sysRet(p, 0); }
      return sysRet(p, NYX_E_INVAL);
    }
    default: return sysRet(p, NYX_E_NOSYS);      // syscall tak dikenal: galat, bukan fault
  }
}

}  // namespace nyx
