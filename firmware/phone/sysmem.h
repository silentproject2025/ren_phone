#pragma once
// =====================================================================
// sysmem.h -- "MMU software" untuk FIRMWARE UTAMA (bukan untuk Nyx Apps)
// =====================================================================
// Kenyataan hardware: ESP32-S3 tidak punya MMU/MPU yang bisa dipakai untuk
// memisahkan fitur firmware (semuanya satu address space, pointer asli).
// Jadi yang disediakan di sini adalah MANAJEMEN memori terpusat, bukan
// proteksi alamat:
//
//   1. PEMILIK    : tiap alokasi tercatat atas nama satu owner (Call, Chat,
//                   Doom, TLS, ...) -> tahu siapa memakai berapa, per region
//                   (RAM internal / PSRAM), termasuk puncaknya.
//   2. ANGGARAN   : batas keras per owner per region. Lewat batas => nullptr
//                   (bukan memakan jatah TLS/WiFi).
//   3. CADANGAN   : RAM internal punya lantai (total free + blok kontigu)
//                   yang tidak boleh ditembus owner biasa. Owner "priv"
//                   (TLS, WiFi, sistem) boleh.
//   4. RECLAIM    : satu rantai callback berprioritas untuk SEMUA fitur
//                   (menggantikan angka ambang yang tersebar). Dipanggil
//                   satu per satu SAMPAI alokasi berhasil, bukan semuanya.
//   5. GATE       : waitContig() = tunggu/reclaim sampai ada blok kontigu
//                   cukup (TLS, task stack, buffer DMA).
//   6. DETEKSI    : header + canary tiap blok; double free / pointer rusak /
//                   tulis melewati ujung terdeteksi dan DIABAIKAN (bocor
//                   lebih aman daripada merusak heap).
//   7. SNAPSHOT   : total vs terlacak -> sisanya "tak terlacak" (kode yang
//                   masih memakai malloc/new langsung, pustaka pihak ketiga).
//
// BATAS YANG JUJUR:
//   * Hanya alokasi lewat sm.alloc() yang terlacak. malloc/new langsung,
//     DOOM/nofrendo/mbedTLS dll muncul di "tak terlacak".
//   * Overhead 16 B header + 4 B canary per blok: pakai untuk buffer >= 256 B.
//   * free() hanya boleh menerima pointer dari sm.alloc(); pointer asing tidak
//     bisa dibedakan dengan aman tanpa tabel (sengaja tanpa tabel agar hemat RAM).
//   * Alignment payload = alignment backend (heap_caps_malloc: 4 B). Buffer yang
//     butuh alignment khusus (DMA 64 B, SIMD 16 B) tetap pakai heap_caps_aligned_alloc.
//
// Semua efek ke dunia luar lewat Backend (firmware: heap_caps_*; tes PC: fake),
// jadi seluruh logika diuji di PC: tests/sysmem/run.sh
// Tanpa heap sendiri, tanpa exception, tanpa RTTI.
// =====================================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace sysmem {

enum { REG_INT = 0, REG_PSRAM = 1, NREG = 2 };

// flags alokasi
enum {
  F_INTERNAL     = 1,   // WAJIB RAM internal (TLS, DMA, stack task)
  F_PSRAM        = 2,   // WAJIB PSRAM
  F_PREFER_PSRAM = 4,   // coba PSRAM dulu, boleh jatuh ke internal
  F_ZERO         = 8    // isi 0
};

#define SM_MAX_OWNERS  16
#define SM_MAX_RECLAIM 8
#define SM_MAX_STEPS   64   // batas langkah reclaim per satu permintaan
#define SM_HDR         16u
#define SM_TRAIL       4u
#define SM_MAGIC_USED  0x5359534Du   // 'SYSM'
#define SM_MAGIC_FREED 0xDEADF4EEu
#define SM_CANARY      0xC0FFEE17u

struct Hdr { uint32_t magic; uint32_t size; uint8_t owner; uint8_t region; uint16_t rsv; uint32_t chk; };
static_assert(sizeof(Hdr) == SM_HDR, "header harus 16 byte");

struct Backend {
  void* ctx;
  void*    (*alloc)(void*, size_t n, int region);
  void     (*free)(void*, void* p, int region);
  size_t   (*freeBytes)(void*, int region);
  size_t   (*largest)(void*, int region);     // blok kontigu terbesar
  size_t   (*totalBytes)(void*, int region);
  uint32_t (*nowMs)(void*);
  void     (*delayMs)(void*, uint32_t);
};

struct Config {
  uint32_t reserveFreeInt   = 24 * 1024;  // owner biasa tidak boleh menurunkan total free internal di bawah ini
  uint32_t reserveContigInt = 20 * 1024;  // ... atau blok kontigu internal terbesar di bawah ini
  uint32_t bigInternal      = 4096;       // alokasi internal >= ini diasumsikan memotong blok terbesar
  uint32_t psramAbove       = 8192;       // tanpa flag: alokasi >= ini coba PSRAM dulu
  uint32_t lowFreeInt       = 40 * 1024;  // tekanan LOW
  uint32_t lowContig        = 32 * 1024;
  uint32_t critFreeInt      = 24 * 1024;  // tekanan CRITICAL
  uint32_t critContig       = 20 * 1024;
  uint32_t tickMs           = 1000;       // jarak minimal antar tick()
};

struct OwnerStat {
  bool used, priv;
  char name[16];
  uint32_t budget[NREG];     // 0 = tanpa batas
  uint32_t live[NREG];       // byte payload hidup
  uint32_t liveRaw[NREG];    // byte mentah (payload + header + canary)
  uint32_t peak[NREG];
  uint32_t blocks;
  uint32_t nAlloc, nFree, nFailBudget, nDenyReserve, nOom, nBad;
};

struct ReclaimCb {
  bool (*fn)(void* user, int level);   // true = benar-benar membebaskan sesuatu
  void* user; int owner; int prio; bool used;
};

struct RegSnap { size_t total, freeB, largest; uint32_t tracked; uint32_t untracked; };

enum { WHY_OK = 0, WHY_BUDGET = 1, WHY_RESERVE = 2, WHY_BACKEND = 3 };

struct Mgr {
  Backend be; Config cfg;
  OwnerStat own[SM_MAX_OWNERS];
  ReclaimCb cbs[SM_MAX_RECLAIM];
  uint32_t nReclaimCalls, nReclaimHit, nGateOk, nGateFail, lastTickMs, nBadGlobal;

  void init(const Backend& b) {
    be = b; cfg = Config();
    memset(own, 0, sizeof(own)); memset(cbs, 0, sizeof(cbs));
    nReclaimCalls = nReclaimHit = nGateOk = nGateFail = lastTickMs = nBadGlobal = 0;
  }

  // daftarkan owner. budget 0 = tanpa batas. priv = boleh menembus cadangan internal.
  bool setOwner(int id, const char* name, uint32_t budgetInt, uint32_t budgetPsram, bool priv = false) {
    if (id < 0 || id >= SM_MAX_OWNERS) return false;
    OwnerStat& o = own[id];
    o.used = true; o.priv = priv; o.budget[REG_INT] = budgetInt; o.budget[REG_PSRAM] = budgetPsram;
    memset(o.name, 0, sizeof(o.name)); if (name) strncpy(o.name, name, sizeof(o.name) - 1);
    return true;
  }

  // prio kecil dipanggil lebih dulu (cache murah dulu). owner = pemilik callback
  // (dilewati kalau dialah yang sedang butuh memori); -1 = tidak ada.
  bool addReclaim(bool (*fn)(void*, int), void* user, int owner, int prio) {
    for (int i = 0; i < SM_MAX_RECLAIM; i++)
      if (!cbs[i].used) { cbs[i].fn = fn; cbs[i].user = user; cbs[i].owner = owner; cbs[i].prio = prio; cbs[i].used = true; return true; }
    return false;
  }
  void removeReclaim(bool (*fn)(void*, int), void* user) {
    for (int i = 0; i < SM_MAX_RECLAIM; i++) if (cbs[i].used && cbs[i].fn == fn && cbs[i].user == user) cbs[i].used = false;
  }

  // ---------------------------------------------------------------- header
  static inline uint32_t calcChk(const Hdr& h) {
    return h.magic ^ h.size ^ (uint32_t)h.owner ^ ((uint32_t)h.region << 8) ^ 0xA5A5A5A5u;
  }
  static inline uint32_t raw(uint32_t n) { return n + SM_HDR + SM_TRAIL; }

  // ------------------------------------------------------------- tekanan
  // 0 normal, 1 LOW, 2 CRITICAL (berdasar RAM internal: total free DAN blok kontigu)
  int pressure() const {
    size_t fi = be.freeBytes ? be.freeBytes(be.ctx, REG_INT) : (size_t)-1;
    size_t lg = be.largest ? be.largest(be.ctx, REG_INT) : (size_t)-1;
    if (fi < cfg.critFreeInt || lg < cfg.critContig) return 2;
    if (fi < cfg.lowFreeInt || lg < cfg.lowContig) return 1;
    return 0;
  }

  // ------------------------------------------------------------- reclaim
  // Panggil callback berikutnya (urut prio). Callback yang melapor "tidak ada yang bisa
  // dibebaskan" (false) ditandai di mask dan dilewati; yang melapor true boleh dipanggil lagi.
  // Pemanggil WAJIB membatasi jumlah langkah (SM_MAX_STEPS) agar callback yang selalu true
  // tapi tidak benar-benar membebaskan tidak membuat loop tak berujung.
  // true = ada callback yang melaporkan berhasil membebaskan sesuatu.
  bool reclaimNext(int level, int skipOwner, uint8_t& mask) {
    for (;;) {
      int bi = -1;
      for (int i = 0; i < SM_MAX_RECLAIM; i++) {
        if (!cbs[i].used || (mask & (1u << i))) continue;
        if (skipOwner >= 0 && cbs[i].owner == skipOwner) continue;
        if (bi < 0 || cbs[i].prio < cbs[bi].prio) bi = i;
      }
      if (bi < 0) return false;
      mask |= (uint8_t)(1u << bi);
      nReclaimCalls++;
      if (cbs[bi].fn(cbs[bi].user, level)) {
        nReclaimHit++;
        mask &= (uint8_t)~(1u << bi);   // masih bisa punya lagi: panggil ulang (prio terendah dikuras dulu)
        return true;
      }
    }
  }

  // ------------------------------------------------------------ alokasi
  void* tryRegion(int owner, size_t n, int region, int& why) {
    OwnerStat& o = own[owner];
    uint32_t r = raw((uint32_t)n);
    if (o.budget[region] && (uint64_t)o.liveRaw[region] + r > o.budget[region]) { o.nFailBudget++; why = WHY_BUDGET; return nullptr; }
    if (region == REG_INT && !o.priv) {
      size_t fi = be.freeBytes ? be.freeBytes(be.ctx, REG_INT) : (size_t)-1;
      size_t lg = be.largest ? be.largest(be.ctx, REG_INT) : (size_t)-1;
      if (fi < (size_t)r + cfg.reserveFreeInt) { o.nDenyReserve++; why = WHY_RESERVE; return nullptr; }
      if (r >= cfg.bigInternal && (lg < (size_t)r + cfg.reserveContigInt)) { o.nDenyReserve++; why = WHY_RESERVE; return nullptr; }
    }
    void* p = be.alloc ? be.alloc(be.ctx, r, region) : nullptr;
    if (!p) { why = WHY_BACKEND; return nullptr; }
    why = WHY_OK; return p;
  }

  void* alloc(int owner, size_t n, int flags = 0) {
    if (owner < 0 || owner >= SM_MAX_OWNERS || !own[owner].used || n == 0 || n > 0x7FFFFF00u) return nullptr;
    int order[2]; int cnt = 0;
    if (flags & F_INTERNAL) { order[cnt++] = REG_INT; }
    else if (flags & F_PSRAM) { order[cnt++] = REG_PSRAM; }
    else {
      bool psFirst = (flags & F_PREFER_PSRAM) || n >= cfg.psramAbove;
      order[cnt++] = psFirst ? REG_PSRAM : REG_INT;
      order[cnt++] = psFirst ? REG_INT : REG_PSRAM;
    }
    bool retryable = false;
    void* p = nullptr; int region = 0;
    for (int pass = 0; pass < 3; pass++) {            // pass 0 = langsung; 1,2 = setelah reclaim level 1, 2
      uint8_t mask = 0; int steps = 0;
      for (;;) {
        retryable = false;
        for (int k = 0; k < cnt && !p; k++) {
          int why = WHY_OK; region = order[k];
          p = tryRegion(owner, n, region, why);
          if (!p && (why == WHY_RESERVE || why == WHY_BACKEND)) retryable = true;
        }
        if (p || !retryable || pass == 0) break;
        if (++steps > SM_MAX_STEPS || !reclaimNext(pass, owner, mask)) break;   // tak ada lagi yang bisa dibebaskan di level ini
      }
      if (p || !retryable) break;
    }
    OwnerStat& o = own[owner];
    if (!p) { if (retryable) o.nOom++; return nullptr; }
    Hdr* h = (Hdr*)p;
    h->magic = SM_MAGIC_USED; h->size = (uint32_t)n; h->owner = (uint8_t)owner; h->region = (uint8_t)region; h->rsv = 0; h->chk = calcChk(*h);
    uint8_t* pay = (uint8_t*)p + SM_HDR;
    uint32_t c = SM_CANARY; memcpy(pay + n, &c, 4);
    if (flags & F_ZERO) memset(pay, 0, n);
    o.live[region] += (uint32_t)n; o.liveRaw[region] += raw((uint32_t)n); o.blocks++; o.nAlloc++;
    if (o.live[region] > o.peak[region]) o.peak[region] = o.live[region];
    return pay;
  }

  // false = pointer rusak / double free / canary rusak (nBad bertambah).
  // Header rusak => blok TIDAK dibebaskan (bocor, tapi heap tidak dirusak).
  bool free(void* pay) {
    if (!pay) return true;
    Hdr* h = (Hdr*)((uint8_t*)pay - SM_HDR);
    if (h->magic != SM_MAGIC_USED || h->owner >= SM_MAX_OWNERS || h->region >= NREG || h->chk != calcChk(*h)) {
      if (h->magic == SM_MAGIC_FREED && h->owner < SM_MAX_OWNERS) own[h->owner].nBad++;   // double free
      else nBadGlobal++;                                                                    // pointer asing / header rusak
      return false;
    }
    OwnerStat& o = own[h->owner]; int region = h->region; uint32_t n = h->size;
    uint32_t c; memcpy(&c, (uint8_t*)pay + n, 4);
    bool ok = (c == SM_CANARY);
    if (!ok) o.nBad++;
    o.live[region] -= n; o.liveRaw[region] -= raw(n); o.blocks--; o.nFree++;
    h->magic = SM_MAGIC_FREED; h->chk = 0;
    if (be.free) be.free(be.ctx, h, region);
    return ok;
  }

  // alokasi baru + salin + free lama; gagal => pointer lama tetap valid (semantik C)
  void* realloc(void* pay, size_t n, int flags = 0) {
    if (!pay) return nullptr;                       // owner tidak diketahui: pakai alloc()
    Hdr* h = (Hdr*)((uint8_t*)pay - SM_HDR);
    if (h->magic != SM_MAGIC_USED || h->owner >= SM_MAX_OWNERS || h->chk != calcChk(*h)) return nullptr;
    int owner = h->owner; uint32_t old = h->size;
    if (n == 0) { free(pay); return nullptr; }
    void* q = alloc(owner, n, flags | (h->region == REG_PSRAM ? F_PREFER_PSRAM : 0));
    if (!q) return nullptr;
    memcpy(q, pay, old < n ? old : n); free(pay); return q;
  }

  // ---------------------------------------------------------------- gate
  // Satu putaran reclaim TANPA menunggu: level 1 lalu 2, berhenti begitu blok kontigu terbesar
  // di region >= need. true = sudah cukup. Aman dipanggil dari glue yang memegang mutex sendiri.
  bool contigNow(uint32_t need, int region = REG_INT, int skipOwner = -1) {
    for (int level = 1; level <= 2; level++) {
      uint8_t mask = 0; int steps = 0;
      for (;;) {
        size_t lg = be.largest ? be.largest(be.ctx, region) : (size_t)-1;
        if (lg >= need) return true;
        if (++steps > SM_MAX_STEPS || !reclaimNext(level, skipOwner, mask)) break;
      }
    }
    size_t lg = be.largest ? be.largest(be.ctx, region) : (size_t)-1;
    return lg >= need;
  }

  // Tunggu/reclaim sampai blok kontigu terbesar di region >= need. true = siap.
  bool waitContig(uint32_t need, uint32_t timeoutMs, int region = REG_INT, int skipOwner = -1) {
    uint32_t t0 = be.nowMs ? be.nowMs(be.ctx) : 0;
    if (contigNow(need, region, skipOwner)) { nGateOk++; return true; }
    for (;;) {                                       // reclaim habis: tunggu pihak lain melepas
      size_t lg = be.largest ? be.largest(be.ctx, region) : (size_t)-1;
      if (lg >= need) { nGateOk++; return true; }
      uint32_t now = be.nowMs ? be.nowMs(be.ctx) : 0;
      if (now - t0 >= timeoutMs) { nGateFail++; return false; }
      if (be.delayMs) be.delayMs(be.ctx, 50);
    }
  }

  // panggil dari loop(): kalau ada tekanan, jalankan SATU langkah reclaim. true = ada yang dibebaskan.
  bool tick() {
    uint32_t now = be.nowMs ? be.nowMs(be.ctx) : 0;
    if (lastTickMs && now - lastTickMs < cfg.tickMs) return false;
    lastTickMs = now ? now : 1;
    int lv = pressure();
    if (!lv) return false;
    uint8_t mask = 0;
    return reclaimNext(lv, -1, mask);
  }

  // ------------------------------------------------------------ laporan
  void snapshot(RegSnap out[NREG]) const {
    for (int r = 0; r < NREG; r++) {
      RegSnap& s = out[r]; memset(&s, 0, sizeof(s));
      s.total = be.totalBytes ? be.totalBytes(be.ctx, r) : 0;
      s.freeB = be.freeBytes ? be.freeBytes(be.ctx, r) : 0;
      s.largest = be.largest ? be.largest(be.ctx, r) : 0;
      for (int i = 0; i < SM_MAX_OWNERS; i++) s.tracked += own[i].liveRaw[r];
      size_t used = s.total > s.freeB ? s.total - s.freeB : 0;
      s.untracked = used > s.tracked ? (uint32_t)(used - s.tracked) : 0;
    }
  }

  // tabel teks untuk Serial / layar debug. kembalikan jumlah byte tertulis.
  size_t format(char* buf, size_t cap) const {
    if (!buf || cap < 2) return 0;
    size_t w = 0; RegSnap sn[NREG]; snapshot(sn);
    static const char* rn[NREG] = { "INT", "PSRAM" };
    for (int r = 0; r < NREG && w < cap; r++) {
      int k = snprintf(buf + w, cap - w, "%s total=%uK free=%uK big=%uK tracked=%uK untracked=%uK\n", rn[r],
                       (unsigned)(sn[r].total / 1024), (unsigned)(sn[r].freeB / 1024), (unsigned)(sn[r].largest / 1024),
                       (unsigned)(sn[r].tracked / 1024), (unsigned)(sn[r].untracked / 1024));
      if (k < 0) break;
      w += (size_t)k;
      if (w >= cap) { w = cap - 1; break; }
    }
    for (int i = 0; i < SM_MAX_OWNERS && w < cap; i++) {
      const OwnerStat& o = own[i]; if (!o.used) continue;
      int k = snprintf(buf + w, cap - w, "%-10s int=%uK/%uK ps=%uK/%uK blk=%u oom=%u bud=%u rsv=%u bad=%u\n", o.name,
                       (unsigned)(o.live[REG_INT] / 1024), (unsigned)(o.peak[REG_INT] / 1024),
                       (unsigned)(o.live[REG_PSRAM] / 1024), (unsigned)(o.peak[REG_PSRAM] / 1024),
                       (unsigned)o.blocks, (unsigned)o.nOom, (unsigned)o.nFailBudget, (unsigned)o.nDenyReserve, (unsigned)o.nBad);
      if (k < 0) break;
      w += (size_t)k;
      if (w >= cap) { w = cap - 1; break; }
    }
    buf[w < cap ? w : cap - 1] = 0;
    return w;
  }
};

}  // namespace sysmem
