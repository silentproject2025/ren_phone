#pragma once
// =====================================================================
// nyxmem.h -- "MMU software" untuk app native NyxOS (portabel, tanpa Arduino)
// =====================================================================
// Kenyataan hardware: ESP32-S3 tidak punya proteksi memori per-aplikasi
// yang bisa dipakai, dan app native memakai pointer asli. Jadi yang bisa
// dijamin oleh lapisan ini adalah AKUNTANSI & KONTROL, bukan isolasi:
//
//   1. ARENA per app  : satu blok PSRAM sebesar quota, dialokasi di awal,
//                       dibebaskan SEKALI saat app keluar -> tidak ada
//                       kebocoran lintas-app & PSRAM tidak terfragmentasi.
//   2. QUOTA keras    : app lewat quota => alloc() mengembalikan NULL
//                       (bukan crash, bukan memakan RAM sistem).
//   3. DETEKSI        : canary tiap blok, deteksi double-free / pointer
//                       asing, cek integritas heap, laporan bocor saat exit.
//   4. TEKANAN        : level LOW/CRITICAL dari free internal + BLOK
//                       KONTIGU terbesar (bukan cuma total free), rantai
//                       reclaim berprioritas (generalisasi onLowMem).
//   5. TLS GATE       : tunggu/reclaim sampai ada blok kontigu cukup
//                       sebelum handshake HTTPS (akar HTTP -1).
//
// Semua efek ke dunia luar lewat MemBackend (di firmware: heap_caps_*,
// di tes PC: malloc + jam palsu), jadi seluruh logika bisa diuji di PC.
// Tanpa alokasi heap sendiri, tanpa exception, tanpa RTTI.
// =====================================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace nyx {

#define NYXMEM_ALIGN       16u
#define NYXMEM_HDR         16u
#define NYXMEM_MAGIC_USED  0xA110C8EDu
#define NYXMEM_MAGIC_FREE  0xF4EE10C0u
#define NYXMEM_CANARY      0xC0FFEE42u

struct ArenaBlk { uint32_t size; uint32_t prevSize; uint32_t magic; uint32_t req; };
static_assert(sizeof(ArenaBlk) == NYXMEM_HDR, "header harus 16 byte");

// ------------------------------- Arena --------------------------------
struct Arena {
  uint8_t* base; uint32_t size;
  uint32_t reqBytes, peakBytes;          // byte yang diminta app (hidup / puncak)
  uint32_t liveBlocks;
  uint32_t nAlloc, nFree, nFail, nBad;   // statistik; nBad = free tak valid / canary rusak
  bool poison;                            // isi 0xDD saat free (deteksi use-after-free)

  static inline uint32_t need(uint32_t n) {
    if (n == 0) n = 1;
    return NYXMEM_HDR + ((n + 4u + (NYXMEM_ALIGN - 1)) & ~(NYXMEM_ALIGN - 1));
  }
  inline ArenaBlk* blkAt(uint32_t off) const { return (ArenaBlk*)(base + off); }
  inline uint32_t offOf(const ArenaBlk* b) const { return (uint32_t)((const uint8_t*)b - base); }

  bool init(void* mem, size_t bytes) {
    uintptr_t a = ((uintptr_t)mem + (NYXMEM_ALIGN - 1)) & ~(uintptr_t)(NYXMEM_ALIGN - 1);
    size_t lost = (size_t)(a - (uintptr_t)mem);
    if (!mem || bytes < lost + 4 * NYXMEM_HDR) return false;
    bytes -= lost; bytes &= ~(size_t)(NYXMEM_ALIGN - 1);
    if (bytes > 0x7FFFFFF0u) bytes = 0x7FFFFFF0u;
    base = (uint8_t*)a; size = (uint32_t)bytes;
    reqBytes = peakBytes = liveBlocks = nAlloc = nFree = nFail = nBad = 0; poison = false;
    ArenaBlk* b = (ArenaBlk*)base; b->size = size; b->prevSize = 0; b->magic = NYXMEM_MAGIC_FREE; b->req = 0;
    return true;
  }

  inline void setCanary(ArenaBlk* b) { uint32_t c = NYXMEM_CANARY; memcpy((uint8_t*)b + NYXMEM_HDR + b->req, &c, 4); }
  inline bool canaryOk(const ArenaBlk* b) const {
    uint32_t c; memcpy(&c, (const uint8_t*)b + NYXMEM_HDR + b->req, 4); return c == NYXMEM_CANARY;
  }

  void* alloc(size_t n) {
    if (n > size) { nFail++; return nullptr; }
    uint32_t want = need((uint32_t)n);
    ArenaBlk* best = nullptr; uint32_t off = 0;
    while (off < size) {                                   // best-fit
      ArenaBlk* b = blkAt(off);
      if (b->magic == NYXMEM_MAGIC_FREE && b->size >= want && (!best || b->size < best->size)) {
        best = b; if (b->size == want) break;
      }
      off += b->size;
    }
    if (!best) { nFail++; return nullptr; }
    uint32_t rest = best->size - want;
    if (rest >= 2 * NYXMEM_HDR) {                          // pecah blok
      ArenaBlk* nx = (ArenaBlk*)((uint8_t*)best + want);
      nx->size = rest; nx->prevSize = want; nx->magic = NYXMEM_MAGIC_FREE; nx->req = 0;
      uint32_t after = offOf(nx) + rest;
      if (after < size) blkAt(after)->prevSize = rest;
      best->size = want;
    }
    best->magic = NYXMEM_MAGIC_USED; best->req = (uint32_t)n; setCanary(best);
    reqBytes += (uint32_t)n; if (reqBytes > peakBytes) peakBytes = reqBytes;
    liveBlocks++; nAlloc++;
    return (uint8_t*)best + NYXMEM_HDR;
  }
  void* allocZ(size_t n) { void* p = alloc(n); if (p) memset(p, 0, n); return p; }

  bool contains(const void* p) const {
    const uint8_t* q = (const uint8_t*)p;
    return q >= base + NYXMEM_HDR && q < base + size;
  }

  // false = pointer tak valid / double free / canary rusak (nBad bertambah)
  bool free(void* p) {
    if (!p) return true;
    if (!contains(p) || (((uintptr_t)p) & (NYXMEM_ALIGN - 1))) { nBad++; return false; }
    ArenaBlk* b = (ArenaBlk*)((uint8_t*)p - NYXMEM_HDR);
    if (b->magic != NYXMEM_MAGIC_USED) { nBad++; return false; }
    bool ok = true;
    if (b->size < need(b->req) || offOf(b) + b->size > size) { nBad++; return false; }
    if (!canaryOk(b)) { nBad++; ok = false; }
    reqBytes -= b->req; liveBlocks--; nFree++;
    if (poison) memset((uint8_t*)b + NYXMEM_HDR, 0xDD, b->size - NYXMEM_HDR);
    b->magic = NYXMEM_MAGIC_FREE; b->req = 0;
    // gabung dengan tetangga berikutnya
    uint32_t nOff = offOf(b) + b->size;
    if (nOff < size) {
      ArenaBlk* nx = blkAt(nOff);
      if (nx->magic == NYXMEM_MAGIC_FREE) { b->size += nx->size; nOff = offOf(b) + b->size; }
    }
    // gabung dengan sebelumnya
    if (b->prevSize) {
      ArenaBlk* pv = (ArenaBlk*)((uint8_t*)b - b->prevSize);
      if (pv->magic == NYXMEM_MAGIC_FREE) { pv->size += b->size; b = pv; nOff = offOf(b) + b->size; }
    }
    if (nOff < size) blkAt(nOff)->prevSize = b->size;
    return ok;
  }

  void* realloc(void* p, size_t n) {
    if (!p) return alloc(n);
    if (n == 0) { free(p); return nullptr; }
    if (!contains(p)) { nBad++; return nullptr; }
    ArenaBlk* b = (ArenaBlk*)((uint8_t*)p - NYXMEM_HDR);
    if (b->magic != NYXMEM_MAGIC_USED) { nBad++; return nullptr; }
    uint32_t old = b->req;
    if (need((uint32_t)n) <= b->size && need((uint32_t)n) + 2 * NYXMEM_HDR > b->size) {   // muat di blok yang sama
      reqBytes = reqBytes - old + (uint32_t)n; if (reqBytes > peakBytes) peakBytes = reqBytes;
      b->req = (uint32_t)n; setCanary(b); return p;
    }
    void* q = alloc(n); if (!q) return nullptr;            // gagal: p tetap valid (semantik C)
    memcpy(q, p, old < n ? old : n); free(p); return q;
  }

  uint32_t largestFree() const {
    uint32_t best = 0, off = 0;
    while (off < size) { ArenaBlk* b = blkAt(off); if (b->magic == NYXMEM_MAGIC_FREE && b->size > best) best = b->size; off += b->size; }
    return best > NYXMEM_HDR + 4 ? best - NYXMEM_HDR - 4 : 0;     // byte payload yang bisa diminta
  }
  uint32_t freeBytes() const { return size - usedBytes(); }
  uint32_t usedBytes() const {
    uint32_t u = 0, off = 0;
    while (off < size) { ArenaBlk* b = blkAt(off); if (b->magic == NYXMEM_MAGIC_USED) u += b->size; off += b->size; }
    return u;
  }

  // integritas penuh. true = sehat.
  bool check() const {
    uint32_t off = 0, prev = 0, nUsed = 0, req = 0; bool prevFree = false;
    while (off < size) {
      ArenaBlk* b = blkAt(off);
      if (b->size < 2 * NYXMEM_HDR || (b->size & (NYXMEM_ALIGN - 1)) || off + b->size > size) return false;
      if (b->prevSize != prev) return false;
      if (b->magic == NYXMEM_MAGIC_USED) {
        if (b->size < need(b->req) || !canaryOk(b)) return false;
        nUsed++; req += b->req; prevFree = false;
      } else if (b->magic == NYXMEM_MAGIC_FREE) {
        if (prevFree) return false;                           // dua blok kosong bersebelahan = bug coalesce
        prevFree = true;
      } else return false;
      prev = b->size; off += b->size;
    }
    return off == size && nUsed == liveBlocks && req == reqBytes;
  }
};

// ----------------------------- Manager --------------------------------
struct MemBackend {
  void* ctx;
  void*  (*bigAlloc)(void*, size_t);          // PSRAM (firmware) / malloc (tes)
  void   (*bigFree)(void*, void*);
  size_t (*freeInternal)(void*);
  size_t (*largestInternal)(void*);           // blok kontigu terbesar RAM internal
  size_t (*freePsram)(void*);
  void   (*delayMs)(void*, uint32_t);
  uint32_t (*nowMs)(void*);
};

#define NYXMEM_MAX_APPS   4
#define NYXMEM_MAX_RECLAIM 8

struct LeakReport { uint32_t leakBlocks, leakBytes, peakBytes, badOps, nAlloc, nFail; bool corrupted; };

struct AppMem { bool used; char name[24]; void* raw; size_t rawSize; Arena arena; uint32_t quota; };

struct ReclaimCb { void (*fn)(void* user, int level); void* user; int prio; bool used; };

struct MemCfg {
  uint32_t lowFreeInt   = 40 * 1024;   // total free internal < ini  => LOW
  uint32_t lowContig    = 32 * 1024;   // blok kontigu < ini         => LOW
  uint32_t critFreeInt  = 24 * 1024;   // < ini => CRITICAL (sama dgn App Memory Guard lama)
  uint32_t critContig   = 20 * 1024;
  uint32_t psramReserve = 512 * 1024;  // PSRAM yang tidak boleh dipakai arena app
  uint32_t minQuota     = 64 * 1024;
  uint32_t maxQuota     = 6u * 1024 * 1024;
};

struct MemMgr {
  MemBackend be; MemCfg cfg;
  AppMem apps[NYXMEM_MAX_APPS];
  ReclaimCb cbs[NYXMEM_MAX_RECLAIM];
  uint32_t nReclaimCalls, nGateOk, nGateFail;

  void init(const MemBackend& b) {
    be = b; cfg = MemCfg();
    memset(apps, 0, sizeof(apps)); memset(cbs, 0, sizeof(cbs));
    nReclaimCalls = nGateOk = nGateFail = 0;
  }

  // slot id (>=0) atau: -1 argumen salah, -2 slot penuh, -3 PSRAM tidak cukup, -4 alokasi gagal
  int launch(const char* name, uint32_t quotaBytes) {
    if (quotaBytes < cfg.minQuota || quotaBytes > cfg.maxQuota) return -1;
    int slot = -1;
    for (int i = 0; i < NYXMEM_MAX_APPS; i++) if (!apps[i].used) { slot = i; break; }
    if (slot < 0) return -2;
    if (be.freePsram && be.freePsram(be.ctx) < (size_t)quotaBytes + cfg.psramReserve) return -3;
    void* raw = be.bigAlloc ? be.bigAlloc(be.ctx, quotaBytes) : nullptr;
    if (!raw) return -4;
    AppMem& a = apps[slot]; memset(&a, 0, sizeof(a));
    a.used = true; a.raw = raw; a.rawSize = quotaBytes; a.quota = quotaBytes;
    if (name) { strncpy(a.name, name, sizeof(a.name) - 1); }
    if (!a.arena.init(raw, quotaBytes)) { be.bigFree(be.ctx, raw); a.used = false; return -4; }
    return slot;
  }

  // bebaskan SELURUH arena sekaligus; laporan bocor dikembalikan
  LeakReport release(int id) {
    LeakReport r; memset(&r, 0, sizeof(r));
    if (id < 0 || id >= NYXMEM_MAX_APPS || !apps[id].used) return r;
    AppMem& a = apps[id];
    r.corrupted = !a.arena.check();
    r.leakBlocks = a.arena.liveBlocks; r.leakBytes = a.arena.reqBytes; r.peakBytes = a.arena.peakBytes;
    r.badOps = a.arena.nBad; r.nAlloc = a.arena.nAlloc; r.nFail = a.arena.nFail;
    if (be.bigFree) be.bigFree(be.ctx, a.raw);
    memset(&a, 0, sizeof(a));
    return r;
  }

  Arena* arena(int id) { return (id >= 0 && id < NYXMEM_MAX_APPS && apps[id].used) ? &apps[id].arena : nullptr; }

  // 0 normal, 1 LOW, 2 CRITICAL
  int pressure() const {
    size_t fi = be.freeInternal ? be.freeInternal(be.ctx) : (size_t)-1;
    size_t lg = be.largestInternal ? be.largestInternal(be.ctx) : (size_t)-1;
    if (fi < cfg.critFreeInt || lg < cfg.critContig) return 2;
    if (fi < cfg.lowFreeInt || lg < cfg.lowContig) return 1;
    return 0;
  }

  // daftarkan callback reclaim; prio kecil dipanggil lebih dulu (cache murah dulu)
  bool addReclaim(void (*fn)(void*, int), void* user, int prio) {
    for (int i = 0; i < NYXMEM_MAX_RECLAIM; i++) if (!cbs[i].used) { cbs[i].fn = fn; cbs[i].user = user; cbs[i].prio = prio; cbs[i].used = true; return true; }
    return false;
  }
  void removeReclaim(void (*fn)(void*, int), void* user) {
    for (int i = 0; i < NYXMEM_MAX_RECLAIM; i++) if (cbs[i].used && cbs[i].fn == fn && cbs[i].user == user) cbs[i].used = false;
  }
  // panggil semua callback berurutan prio. kembalikan jumlah yang dipanggil.
  int reclaim(int level) {
    int called = 0;
    // urut prio naik, indeks sebagai pemecah seri (deterministik)
    bool called_[NYXMEM_MAX_RECLAIM]; memset(called_, 0, sizeof(called_));
    for (int n = 0; n < NYXMEM_MAX_RECLAIM; n++) {
      int bi = -1;
      for (int i = 0; i < NYXMEM_MAX_RECLAIM; i++)
        if (cbs[i].used && !called_[i] && (bi < 0 || cbs[i].prio < cbs[bi].prio)) bi = i;
      if (bi < 0) break;
      called_[bi] = true; cbs[bi].fn(cbs[bi].user, level); called++; nReclaimCalls++;
    }
    return called;
  }

  // Tunggu/reclaim sampai blok kontigu RAM internal >= need. true = siap.
  bool tlsGate(uint32_t needContig, uint32_t timeoutMs) {
    uint32_t t0 = be.nowMs ? be.nowMs(be.ctx) : 0;
    int step = 0;
    for (;;) {
      size_t lg = be.largestInternal ? be.largestInternal(be.ctx) : (size_t)-1;
      if (lg >= needContig) { nGateOk++; return true; }
      reclaim(step == 0 ? 1 : 2); step = 1;                 // pertama halus (LOW), lalu agresif (CRITICAL)
      lg = be.largestInternal ? be.largestInternal(be.ctx) : (size_t)-1;
      if (lg >= needContig) { nGateOk++; return true; }
      uint32_t now = be.nowMs ? be.nowMs(be.ctx) : 0;
      if (now - t0 >= timeoutMs) { nGateFail++; return false; }
      if (be.delayMs) be.delayMs(be.ctx, 50);
    }
  }
};

}  // namespace nyx
