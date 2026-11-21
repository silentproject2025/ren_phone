// Tes PC untuk firmware/phone/sysmem.h  (g++ -std=c++17, ASan+UBSan)
#include "sysmem.h"
#include <stdlib.h>
#include <assert.h>
#include <vector>
#include <set>
using namespace sysmem;

static int nChecks = 0, nFail = 0;
#define CHECK(c) do { nChecks++; if (!(c)) { nFail++; printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------- backend palsu: dua region berkapasitas tetap ----------------
struct Fake {
  size_t cap[NREG], used[NREG], frag[NREG];
  uint32_t now; bool quarantine; std::vector<void*> held;
  int allocs[NREG]; std::set<void*> live;
  void reset(size_t ci, size_t cp) {
    for (void* q : live) free(q);
    live.clear(); for (void* q : held) free(q); cap[0] = ci; cap[1] = cp; used[0] = used[1] = frag[0] = frag[1] = 0; now = 1; quarantine = false; held.clear(); allocs[0] = allocs[1] = 0; }
  size_t freeB(int r) const { return cap[r] - used[r]; }
  size_t largest(int r) const { size_t f = freeB(r); return f > frag[r] ? f - frag[r] : 0; }
};
static Fake F;
struct Pre { size_t n; int r; };   // ukuran disimpan sebelum payload agar free tahu berapa
static void* b_alloc(void*, size_t n, int r) {
  if (n > F.largest(r)) return nullptr;
  Pre* q = (Pre*)malloc(sizeof(Pre) + 16 + n); if (!q) return nullptr;
  q->n = n; q->r = r; F.used[r] += n; F.allocs[r]++; F.live.insert(q);
  return (uint8_t*)q + sizeof(Pre) + 16;               // payload mulai setelah Pre + 16 B bantalan
}
static void b_free(void*, void* p, int) {
  Pre* q = (Pre*)((uint8_t*)p - sizeof(Pre) - 16); F.used[q->r] -= q->n;
  F.live.erase(q);
  if (F.quarantine) { F.held.push_back(q); return; }
  free(q);
}
static size_t b_freeB(void*, int r) { return F.freeB(r); }
static size_t b_large(void*, int r) { return F.largest(r); }
static size_t b_total(void*, int r) { return F.cap[r]; }
static uint32_t b_now(void*) { return F.now; }
static void b_delay(void*, uint32_t ms) { F.now += ms; }
static Backend mkBE() { Backend b; memset(&b, 0, sizeof(b)); b.alloc = b_alloc; b.free = b_free; b.freeBytes = b_freeB; b.largest = b_large; b.totalBytes = b_total; b.nowMs = b_now; b.delayMs = b_delay; return b; }

enum { O_SYS, O_CALL, O_CHAT, O_GAME, O_TLS, O_N };
static Mgr M;
static void setup(size_t ci = 200 * 1024, size_t cp = 4 * 1024 * 1024) {
  F.reset(ci, cp); M.init(mkBE());
  M.setOwner(O_SYS, "system", 0, 0, true); M.setOwner(O_CALL, "call", 0, 0); M.setOwner(O_CHAT, "chat", 0, 0);
  M.setOwner(O_GAME, "game", 0, 0); M.setOwner(O_TLS, "tls", 0, 0, true);
}
static void drainAll(std::vector<void*>& v) { for (void* p : v) M.free(p); v.clear(); }

// ----------------- reclaim palsu: tiap callback punya "cache" yang bisa dibuang -----------------
struct Cache { int owner; std::vector<void*> blocks; int calls; };
static bool cacheReclaim(void* u, int) {
  Cache* c = (Cache*)u; c->calls++;
  if (c->blocks.empty()) return false;
  M.free(c->blocks.back()); c->blocks.pop_back(); return true;
}
static int order_log[16], order_n = 0;
static bool logReclaim(void* u, int) { order_log[order_n++] = (int)(intptr_t)u; return false; }

static void test_basic() {
  setup();
  void* a = M.alloc(O_CALL, 1000);
  CHECK(a != nullptr);
  CHECK(M.own[O_CALL].live[REG_INT] == 1000);
  CHECK(M.own[O_CALL].blocks == 1);
  CHECK(M.own[O_CALL].liveRaw[REG_INT] == 1000 + SM_HDR + SM_TRAIL);
  CHECK(F.used[REG_INT] == 1000 + SM_HDR + SM_TRAIL);
  CHECK(M.free(a));
  CHECK(M.own[O_CALL].live[REG_INT] == 0 && M.own[O_CALL].blocks == 0);
  CHECK(F.used[REG_INT] == 0);
  CHECK(M.own[O_CALL].peak[REG_INT] == 1000);
  CHECK(M.alloc(O_CALL, 0) == nullptr);                 // 0 byte ditolak
  CHECK(M.alloc(99, 10) == nullptr);                    // owner tak valid
  CHECK(M.alloc(O_N + 3, 10) == nullptr);               // owner belum didaftarkan
  CHECK(M.free(nullptr));
  uint8_t* z = (uint8_t*)M.alloc(O_CHAT, 300, F_ZERO); bool allz = true;
  for (int i = 0; i < 300; i++) if (z[i]) allz = false;
  CHECK(allz); M.free(z);
}

static void test_region_choice() {
  setup();
  void* small = M.alloc(O_CALL, 1000);                  // kecil -> internal
  void* big = M.alloc(O_CALL, 20000);                   // >= psramAbove -> PSRAM
  CHECK(M.own[O_CALL].live[REG_INT] == 1000 && M.own[O_CALL].live[REG_PSRAM] == 20000);
  void* forced = M.alloc(O_CALL, 20000, F_INTERNAL);    // dipaksa internal
  CHECK(forced && M.own[O_CALL].live[REG_INT] == 21000);
  void* pp = M.alloc(O_CALL, 500, F_PREFER_PSRAM);
  CHECK(pp && M.own[O_CALL].live[REG_PSRAM] == 20500);
  void* ps = M.alloc(O_CALL, 500, F_PSRAM);
  CHECK(ps && M.own[O_CALL].live[REG_PSRAM] == 21000);
  M.free(small); M.free(big); M.free(forced); M.free(pp); M.free(ps);
  // fallback: PSRAM penuh -> besar jatuh ke internal; F_PSRAM tidak jatuh
  setup(400 * 1024, 64 * 1024);
  void* a = M.alloc(O_CALL, 60000, F_PSRAM); CHECK(a);
  void* b = M.alloc(O_CALL, 20000);                     // PSRAM sisa < 20000 -> internal
  CHECK(b && M.own[O_CALL].live[REG_INT] == 20000);
  CHECK(M.alloc(O_CALL, 20000, F_PSRAM) == nullptr);
  M.free(a); M.free(b);
}

static void test_corruption() {
  setup();
  uint8_t* a = (uint8_t*)M.alloc(O_CALL, 100);
  a[100] ^= 0xFF;                                       // tulis 1 byte melewati ujung
  CHECK(!M.free(a));                                    // canary rusak -> false
  CHECK(M.own[O_CALL].nBad == 1);
  CHECK(M.own[O_CALL].blocks == 0 && F.used[REG_INT] == 0);   // tetap dibebaskan (header sehat)
  // double free (backend menahan memori supaya aman dibaca di tes)
  F.quarantine = true;
  void* b = M.alloc(O_CHAT, 64);
  CHECK(M.free(b));
  CHECK(!M.free(b));
  CHECK(M.own[O_CHAT].nBad == 1);
  CHECK(M.own[O_CHAT].blocks == 0 && M.own[O_CHAT].live[REG_INT] == 0);   // akuntansi tidak dobel
  CHECK(F.used[REG_INT] == 0);
  // header rusak -> TIDAK dibebaskan
  void* c = M.alloc(O_CHAT, 64);
  Hdr* h = (Hdr*)((uint8_t*)c - SM_HDR); h->size += 8;  // chk tidak cocok
  uint32_t usedBefore = F.used[REG_INT];
  CHECK(!M.free(c));
  CHECK(F.used[REG_INT] == usedBefore);
  h->size -= 8; CHECK(M.free(c));
  F.quarantine = false;                               // sisa quarantine dibebaskan oleh reset() berikutnya
}

static void test_budget() {
  setup();
  M.setOwner(O_GAME, "game", 10000, 50000);
  void* a = M.alloc(O_GAME, 5000, F_INTERNAL); CHECK(a);
  CHECK(M.alloc(O_GAME, 5000, F_INTERNAL) == nullptr);  // 5000+20 + 5020 > 10000
  CHECK(M.own[O_GAME].nFailBudget == 1);
  CHECK(M.own[O_GAME].nOom == 0);                       // budget bukan OOM
  void* b = M.alloc(O_GAME, 40000, F_PSRAM); CHECK(b);
  CHECK(M.alloc(O_GAME, 20000, F_PSRAM) == nullptr);
  // tanpa flag: budget internal habis -> jatuh ke PSRAM yang masih muat
  void* c = M.alloc(O_GAME, 6000); CHECK(c && M.own[O_GAME].live[REG_PSRAM] == 46000);
  M.free(a); M.free(b); M.free(c);
  void* d = M.alloc(O_GAME, 5000, F_INTERNAL); CHECK(d);   // budget kembali setelah free
  M.free(d);
  CHECK(M.alloc(O_CALL, 100000, F_INTERNAL) != nullptr);   // owner tanpa budget tidak terpengaruh
}

static void test_reserve() {
  setup(100 * 1024);
  std::vector<void*> v;
  // owner biasa tidak boleh menurunkan total free internal di bawah reserveFreeInt (24K)
  while (void* p = M.alloc(O_CALL, 2000, F_INTERNAL)) v.push_back(p);
  CHECK(F.freeB(REG_INT) >= M.cfg.reserveFreeInt - 2100);
  CHECK(F.freeB(REG_INT) < M.cfg.reserveFreeInt + 2100);
  CHECK(M.own[O_CALL].nDenyReserve > 0);
  // owner priv masih bisa memakai cadangan
  void* t = M.alloc(O_TLS, 16000, F_INTERNAL); CHECK(t);
  M.free(t); drainAll(v);
  // blok kontigu: alokasi besar yang memotong blok terbesar sampai di bawah lantai ditolak
  setup(200 * 1024); F.frag[REG_INT] = 160 * 1024;      // free 200K tapi blok terbesar cuma 40K
  CHECK(M.alloc(O_CALL, 30000, F_INTERNAL) == nullptr);   // 40K - 30K < 20K
  CHECK(M.alloc(O_CALL, 10000, F_INTERNAL) != nullptr);   // 40K - 10K >= 20K
  CHECK(M.alloc(O_TLS, 30000, F_INTERNAL) != nullptr);    // priv boleh
}

static void fillInternal(std::vector<void*>& v, uint32_t step) { while (void* p = M.alloc(O_SYS, step, F_INTERNAL)) v.push_back(p); }

static void test_reclaim_chain() {
  // ---- A: urut prio, berhenti begitu berhasil, callback milik peminta dilewati
  setup(100 * 1024); M.cfg.reserveFreeInt = M.cfg.reserveContigInt = 0;   // reserve diuji terpisah
  Cache cA{O_CHAT, {}, 0}, cB{O_GAME, {}, 0}, cC{O_CALL, {}, 0};
  for (int i = 0; i < 4; i++) { cA.blocks.push_back(M.alloc(O_CHAT, 4000, F_INTERNAL)); cB.blocks.push_back(M.alloc(O_GAME, 4000, F_INTERNAL)); cC.blocks.push_back(M.alloc(O_CALL, 4000, F_INTERNAL)); }
  std::vector<void*> filler; fillInternal(filler, 1000);
  M.addReclaim(cacheReclaim, &cA, O_CHAT, 20); M.addReclaim(cacheReclaim, &cB, O_GAME, 10); M.addReclaim(cacheReclaim, &cC, O_CALL, 30);
  cA.calls = cB.calls = cC.calls = 0;                    // loop pengisi di atas tidak punya callback, tapi pastikan nol
  void* p = M.alloc(O_TLS, 3000, F_INTERNAL);
  CHECK(p != nullptr);
  CHECK(cB.calls == 1 && cA.calls == 0 && cC.calls == 0);   // prio 10 dulu, lalu berhenti
  CHECK(cB.blocks.size() == 3);
  M.free(p);
  // peminta = CALL: callback milik CALL (cC) tidak boleh dipanggil walau paling akhir
  while (!cB.blocks.empty()) { M.free(cB.blocks.back()); cB.blocks.pop_back(); }
  while (!cA.blocks.empty()) { M.free(cA.blocks.back()); cA.blocks.pop_back(); }
  fillInternal(filler, 500);                             // isi lagi sampai mentok
  cA.calls = cB.calls = cC.calls = 0;
  void* r = M.alloc(O_CALL, 3000, F_INTERNAL);           // cB, cA kosong (false); hanya cC yang punya blok tapi milik peminta
  CHECK(r == nullptr);
  CHECK(cC.calls == 0);
  CHECK(cB.calls == 2 && cA.calls == 2);                 // dicoba sekali per level, tidak ada yang membebaskan
  cC.blocks.push_back(M.alloc(O_CALL, 4000, F_INTERNAL)); // (gagal: penuh) -> tidak ditambah
  for (void* q : cC.blocks) M.free(q);
  for (void* q : filler) M.free(q);
  // ---- B: semua callback tidak bisa membebaskan -> gagal bersih, tidak hang
  setup(100 * 1024); M.cfg.reserveFreeInt = M.cfg.reserveContigInt = 0;
  std::vector<void*> f3; fillInternal(f3, 1000);
  Cache e{O_CHAT, {}, 0}; M.addReclaim(cacheReclaim, &e, O_CHAT, 1);
  CHECK(M.alloc(O_CALL, 5000, F_INTERNAL) == nullptr);
  CHECK(M.own[O_CALL].nOom == 1);
  CHECK(e.calls == 2);                                   // sekali per level (1 dan 2)
  // ---- C: reserve: owner biasa baru lolos setelah reclaim membebaskan cukup untuk r + reserve
  setup(100 * 1024);
  Cache big{O_CHAT, {}, 0}; for (int i = 0; i < 6; i++) big.blocks.push_back(M.alloc(O_CHAT, 6000, F_INTERNAL));
  std::vector<void*> f4; fillInternal(f4, 1000);
  M.addReclaim(cacheReclaim, &big, O_CHAT, 1);
  void* z = M.alloc(O_CALL, 2000, F_INTERNAL);           // free kecil: butuh 2020 + 24K sisa -> reclaim ~4 blok
  CHECK(z != nullptr);
  CHECK(big.calls >= 3 && big.blocks.size() <= 3);
  CHECK(F.freeB(REG_INT) >= M.cfg.reserveFreeInt);       // lantai tetap terjaga setelah alokasi
}

static void test_reclaim_order_and_removal() {
  setup();
  order_n = 0;
  M.addReclaim(logReclaim, (void*)(intptr_t)3, -1, 30); M.addReclaim(logReclaim, (void*)(intptr_t)1, -1, 10); M.addReclaim(logReclaim, (void*)(intptr_t)2, -1, 20);
  uint8_t mask = 0; CHECK(!M.reclaimNext(1, -1, mask));
  CHECK(order_n == 3 && order_log[0] == 1 && order_log[1] == 2 && order_log[2] == 3);
  M.removeReclaim(logReclaim, (void*)(intptr_t)2);
  order_n = 0; mask = 0; M.reclaimNext(1, -1, mask);
  CHECK(order_n == 2 && order_log[0] == 1 && order_log[1] == 3);
  for (int i = 0; i < SM_MAX_RECLAIM; i++) M.addReclaim(logReclaim, (void*)(intptr_t)(100 + i), -1, i);
  CHECK(!M.addReclaim(logReclaim, (void*)(intptr_t)999, -1, 0));   // penuh
}

static void test_waitContig() {
  setup(200 * 1024); F.frag[REG_INT] = 190 * 1024;      // blok terbesar 10K
  // reclaim yang "menyatukan" blok: kurangi frag
  struct Defrag { int calls; } d{0};
  auto fn = [](void* u, int) -> bool { Defrag* x = (Defrag*)u; x->calls++; if (F.frag[REG_INT] >= 60 * 1024) { F.frag[REG_INT] -= 60 * 1024; return true; } return false; };
  M.addReclaim(fn, &d, -1, 1);
  CHECK(M.waitContig(32 * 1024, 1000));                 // 10K -> 70K -> OK
  CHECK(d.calls == 1 && M.nGateOk == 1);
  // tidak ada yang bisa dibebaskan: timeout memakai jam palsu, tidak hang
  setup(200 * 1024); F.frag[REG_INT] = 190 * 1024;
  uint32_t t0 = F.now;
  CHECK(!M.waitContig(32 * 1024, 500));
  CHECK(F.now - t0 >= 500 && F.now - t0 < 700);
  CHECK(M.nGateFail == 1);
  // pihak lain melepas memori saat menunggu (lewat delay): sukses sebelum timeout
  setup(200 * 1024); F.frag[REG_INT] = 190 * 1024;
  struct Rel { int n; } r{0};
  Backend be = mkBE(); static Rel* gr; gr = &r;
  be.delayMs = [](void*, uint32_t ms) { F.now += ms; if (++gr->n == 3) F.frag[REG_INT] = 0; };
  M.be = be;
  CHECK(M.waitContig(32 * 1024, 1000));
  CHECK(r.n == 3);
}

static bool liarReclaim(void* u, int) { (*(int*)u)++; return true; }   // selalu "berhasil" tapi tidak membebaskan apa pun
static void test_runaway_callback() {
  setup(100 * 1024); M.cfg.reserveFreeInt = M.cfg.reserveContigInt = 0;
  std::vector<void*> f; fillInternal(f, 1000);
  int calls = 0; M.addReclaim(liarReclaim, &calls, -1, 1);
  CHECK(M.alloc(O_CALL, 5000, F_INTERNAL) == nullptr);   // harus kembali, tidak hang
  CHECK(calls <= 2 * SM_MAX_STEPS && calls > 0);
  calls = 0; CHECK(!M.waitContig(50 * 1024, 200));
  CHECK(calls <= 2 * SM_MAX_STEPS);
}

static void test_pressure_tick() {
  setup(100 * 1024);
  CHECK(M.pressure() == 0);
  F.used[REG_INT] = 100 * 1024 - 35 * 1024; CHECK(M.pressure() == 1);   // free 35K < 40K
  F.used[REG_INT] = 100 * 1024 - 20 * 1024; CHECK(M.pressure() == 2);   // free 20K < 24K
  F.used[REG_INT] = 0; F.frag[REG_INT] = 100 * 1024 - 25 * 1024; CHECK(M.pressure() == 1);   // free banyak, blok 25K < 32K
  F.frag[REG_INT] = 100 * 1024 - 10 * 1024; CHECK(M.pressure() == 2);
  // tick: kena throttle & hanya satu langkah
  Cache c{O_CHAT, {}, 0}; c.blocks.push_back(M.alloc(O_CHAT, 100, F_INTERNAL)); c.blocks.push_back(M.alloc(O_CHAT, 100, F_INTERNAL));
  M.addReclaim(cacheReclaim, &c, O_CHAT, 1);
  F.now = 5000;
  CHECK(M.tick() && c.blocks.size() == 1);
  F.now += 100; CHECK(!M.tick() && c.blocks.size() == 1);              // dalam jendela tickMs
  F.now += 2000; CHECK(M.tick() && c.blocks.empty());
  F.now += 2000; CHECK(!M.tick());                                      // cache kosong -> tidak ada yang dibebaskan
  F.frag[REG_INT] = 0; F.now += 2000; CHECK(!M.tick() && c.calls == 3); // tekanan hilang -> tidak memanggil callback
}

static void test_realloc() {
  setup();
  uint8_t* a = (uint8_t*)M.alloc(O_CHAT, 100); for (int i = 0; i < 100; i++) a[i] = (uint8_t)i;
  uint8_t* b = (uint8_t*)M.realloc(a, 5000);
  CHECK(b && M.own[O_CHAT].blocks == 1);
  bool same = true; for (int i = 0; i < 100; i++) if (b[i] != (uint8_t)i) same = false;
  CHECK(same);
  uint8_t* c = (uint8_t*)M.realloc(b, 40);
  CHECK(c && M.own[O_CHAT].blocks == 1);
  same = true; for (int i = 0; i < 40; i++) if (c[i] != (uint8_t)i) same = false;
  CHECK(same);
  CHECK(M.realloc(c, 0) == nullptr && M.own[O_CHAT].blocks == 0);
  // gagal -> pointer lama tetap valid
  setup(100 * 1024, 64 * 1024);
  void* d = M.alloc(O_CHAT, 1000, F_INTERNAL);
  void* e = M.realloc(d, 10 * 1024 * 1024);
  CHECK(e == nullptr && M.own[O_CHAT].blocks == 1);
  CHECK(M.free(d));
}

static void test_snapshot() {
  setup();
  F.used[REG_INT] = 30 * 1024;                          // "kode lain" memakai 30K tanpa lewat SysMem
  void* a = M.alloc(O_CALL, 10000, F_INTERNAL);
  RegSnap sn[NREG]; M.snapshot(sn);
  CHECK(sn[REG_INT].tracked == 10000 + SM_HDR + SM_TRAIL);
  CHECK(sn[REG_INT].untracked == 30 * 1024);
  CHECK(sn[REG_INT].total == 200 * 1024);
  char buf[1024]; size_t w = M.format(buf, sizeof(buf));
  CHECK(w > 0 && strstr(buf, "call") && strstr(buf, "untracked=30K"));
  char tiny[16]; w = M.format(tiny, sizeof(tiny)); CHECK(w < sizeof(tiny));   // tidak overflow
  M.free(a);
}

// ------------------------- fuzz: invarian akuntansi -------------------------
static uint32_t rng = 12345;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void test_fuzz(uint32_t seed) {
  rng = seed; setup(256 * 1024, 2 * 1024 * 1024);
  M.setOwner(O_GAME, "game", 30000, 300000);
  struct Live { void* p; int owner; uint32_t n; uint8_t fill; };
  std::vector<Live> live;
  Cache cc{O_CHAT, {}, 0}; M.addReclaim(cacheReclaim, &cc, O_CHAT, 5);
  for (int it = 0; it < 20000; it++) {
    uint32_t op = rnd() % 10;
    if (op < 5 || live.empty()) {
      int ow = rnd() % O_N; uint32_t n = 1 + rnd() % ((rnd() & 7) == 0 ? 40000 : 1500);
      int fl = (rnd() % 4 == 0) ? F_INTERNAL : (rnd() % 4 == 0 ? F_PSRAM : 0);
      uint8_t* p = (uint8_t*)M.alloc(ow, n, fl);
      if (p) { uint8_t f = (uint8_t)rnd(); memset(p, f, n); live.push_back({p, ow, n, f}); }
    } else if (op < 8) {
      size_t i = rnd() % live.size(); Live l = live[i];
      uint8_t* p = (uint8_t*)l.p; bool ok = true; for (uint32_t k = 0; k < l.n; k++) if (p[k] != l.fill) { ok = false; break; }
      CHECK(ok); CHECK(M.free(l.p)); live[i] = live.back(); live.pop_back();
    } else if (op == 8) {
      size_t i = rnd() % live.size(); uint32_t nn = 1 + rnd() % 3000;
      uint8_t* q = (uint8_t*)M.realloc(live[i].p, nn);
      if (q) { uint32_t keep = live[i].n < nn ? live[i].n : nn; bool ok = true; for (uint32_t k = 0; k < keep; k++) if (q[k] != live[i].fill) ok = false; CHECK(ok); memset(q, live[i].fill, nn); live[i].p = q; live[i].n = nn; }
    } else {
      F.frag[REG_INT] = rnd() % (80 * 1024);            // fragmentasi acak
      M.tick(); F.now += 1500;
    }
    if ((it & 1023) == 0) {                             // invarian: jumlah per-owner == jumlah hidup sebenarnya
      uint32_t sumRaw[NREG] = {0, 0}, bl = 0;
      for (auto& l : live) { Hdr* h = (Hdr*)((uint8_t*)l.p - SM_HDR); sumRaw[h->region] += Mgr::raw(l.n); bl++; }
      uint32_t trk[NREG] = {0, 0}, tb = 0;
      for (int i = 0; i < SM_MAX_OWNERS; i++) { trk[0] += M.own[i].liveRaw[0]; trk[1] += M.own[i].liveRaw[1]; tb += M.own[i].blocks; }
      // blok milik cache reclaim (cc) tidak ada di 'live' -> dikecualikan lewat daftar kosong (cc.blocks selalu kosong di sini)
      CHECK(trk[0] == sumRaw[0] && trk[1] == sumRaw[1] && tb == bl);
      CHECK(F.used[0] == sumRaw[0] && F.used[1] == sumRaw[1]);
    }
  }
  for (auto& l : live) CHECK(M.free(l.p));
  CHECK(F.used[0] == 0 && F.used[1] == 0);
  uint32_t bad = 0; for (int i = 0; i < SM_MAX_OWNERS; i++) bad += M.own[i].nBad;
  CHECK(bad == 0 && M.nBadGlobal == 0);
}

int main() {
  test_basic(); test_region_choice(); test_corruption(); test_budget(); test_reserve();
  test_reclaim_chain(); test_runaway_callback(); test_reclaim_order_and_removal(); test_waitContig(); test_pressure_tick();
  test_realloc(); test_snapshot();
  for (uint32_t s : {1u, 7u, 42u, 1337u, 99991u}) test_fuzz(s);
  printf("%d cek, %d gagal\n", nChecks, nFail);
  return nFail ? 1 : 0;
}
