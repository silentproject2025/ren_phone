// Tes nyxmem.h di PC. Bangun dengan sanitizer:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -I../../firmware/phone test_mem.cpp -o test_mem
#include "nyxmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

using namespace nyx;
static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) g_ok++; else { g_fail++; printf("  GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------------------- fuzz arena ------------------------------
struct Live { uint8_t* p; size_t n; uint8_t tag; };

static void fillPat(uint8_t* p, size_t n, uint8_t tag) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(tag + i * 7); }
static bool verPat(const uint8_t* p, size_t n, uint8_t tag) { for (size_t i = 0; i < n; i++) if (p[i] != (uint8_t)(tag + i * 7)) return false; return true; }

static void testFuzz(uint32_t seed, size_t arenaBytes) {
  std::vector<uint8_t> mem(arenaBytes + 64);
  Arena a; CHECK(a.init(mem.data() + 3, arenaBytes));          // sengaja tak rata: init harus merapikan
  a.poison = true;
  std::vector<Live> live; srand(seed); bool allOk = true;
  for (int step = 0; step < 20000 && allOk; step++) {
    int op = rand() % 10;
    if (op < 5 || live.empty()) {
      size_t n = (size_t)(rand() % 3 == 0 ? rand() % 4000 : rand() % 200);
      uint8_t* p = (uint8_t*)a.alloc(n);
      if (p) {
        if (((uintptr_t)p & 15) != 0) allOk = false;
        uint8_t tag = (uint8_t)rand(); fillPat(p, n, tag); live.push_back({p, n, tag});
      }
    } else if (op < 8) {
      size_t i = (size_t)rand() % live.size();
      if (!verPat(live[i].p, live[i].n, live[i].tag)) allOk = false;
      if (!a.free(live[i].p)) allOk = false;
      live[i] = live.back(); live.pop_back();
    } else {
      size_t i = (size_t)rand() % live.size(); size_t nn = (size_t)(rand() % 600);
      size_t keep = std::min(live[i].n, nn);
      uint8_t* q = (uint8_t*)a.realloc(live[i].p, nn);
      if (nn == 0) { live[i] = live.back(); live.pop_back(); }
      else if (q) {
        if (!verPat(q, keep, live[i].tag)) allOk = false;
        uint8_t tag = (uint8_t)rand(); fillPat(q, nn, tag); live[i] = {q, nn, tag};
      } else if (!verPat(live[i].p, live[i].n, live[i].tag)) allOk = false;   // gagal: blok lama harus utuh
    }
    if (step % 500 == 0 && !a.check()) allOk = false;
    if (live.size() != a.liveBlocks) allOk = false;
  }
  CHECK(allOk); CHECK(a.check());
  for (auto& l : live) { if (!verPat(l.p, l.n, l.tag)) allOk = false; a.free(l.p); }
  CHECK(allOk); CHECK(a.check());
  CHECK(a.liveBlocks == 0); CHECK(a.reqBytes == 0); CHECK(a.nBad == 0);
  // setelah semua bebas: harus kembali satu blok utuh (coalescing sempurna)
  CHECK(a.largestFree() == a.size - NYXMEM_HDR - 4);
}

static void testBasics() {
  std::vector<uint8_t> mem(8192); Arena a; a.init(mem.data(), mem.size());
  void* p1 = a.alloc(100); void* p2 = a.alloc(100); CHECK(p1 && p2 && p1 != p2);
  CHECK(a.alloc(1u << 20) == nullptr); CHECK(a.nFail == 1);           // minta kebesaran
  CHECK(a.free(p1)); CHECK(!a.free(p1)); CHECK(a.nBad == 1);          // double free terdeteksi
  CHECK(!a.free((void*)(mem.data() + 1)) ); CHECK(a.nBad == 2);       // pointer sembarang
  int x; CHECK(!a.free(&x)); CHECK(a.nBad == 3);                      // pointer luar arena
  CHECK(a.free(nullptr));                                              // free(NULL) legal
  // exhaustion: penuhi sampai gagal, lalu bebaskan semua
  std::vector<void*> v; for (;;) { void* p = a.alloc(64); if (!p) break; v.push_back(p); }
  CHECK(v.size() > 50); CHECK(a.alloc(64) == nullptr);
  for (void* p : v) CHECK(a.free(p));
  CHECK(a.free(p2)); CHECK(a.check()); CHECK(a.liveBlocks == 0);
  CHECK(a.largestFree() == a.size - NYXMEM_HDR - 4);
  // allocZ benar-benar nol (setelah poison)
  a.poison = true; uint8_t* z = (uint8_t*)a.alloc(256); memset(z, 0xAB, 256); a.free(z);
  z = (uint8_t*)a.allocZ(256); bool zero = true; for (int i = 0; i < 256; i++) if (z[i]) zero = false; CHECK(zero);
  // peak
  CHECK(a.peakBytes >= 256);
}

static void testCanary() {
  std::vector<uint8_t> mem(4096); Arena a; a.init(mem.data(), mem.size());
  uint8_t* p = (uint8_t*)a.alloc(40);
  p[40] = 0x00;                                                         // tulis 1 byte lewat batas
  CHECK(!a.check());                                                    // integritas mendeteksi
  CHECK(!a.free(p)); CHECK(a.nBad == 1);                                // free melapor
  CHECK(a.liveBlocks == 0);                                             // tetap dibebaskan
}

// ---------------------------- manager ---------------------------------
struct Fake {
  size_t freeInt = 100 * 1024, largest = 60 * 1024, freePs = 8u << 20; uint32_t now = 0;
  int allocs = 0, frees = 0; size_t lastDelay = 0; int delays = 0;
};
static void* f_big(void* c, size_t n) { ((Fake*)c)->allocs++; ((Fake*)c)->freePs -= n; return malloc(n); }
static void  f_bigFree(void* c, void* p) { ((Fake*)c)->frees++; free(p); }
static size_t f_fi(void* c) { return ((Fake*)c)->freeInt; }
static size_t f_lg(void* c) { return ((Fake*)c)->largest; }
static size_t f_ps(void* c) { return ((Fake*)c)->freePs; }
static void   f_delay(void* c, uint32_t ms) { ((Fake*)c)->now += ms; ((Fake*)c)->delays++; }
static uint32_t f_now(void* c) { return ((Fake*)c)->now; }
static MemBackend mk(Fake* f) { MemBackend b; b.ctx = f; b.bigAlloc = f_big; b.bigFree = f_bigFree; b.freeInternal = f_fi; b.largestInternal = f_lg; b.freePsram = f_ps; b.delayMs = f_delay; b.nowMs = f_now; return b; }

static void testManager() {
  Fake f; MemMgr m; m.init(mk(&f));
  CHECK(m.launch("x", 1024) == -1);                                     // di bawah minQuota
  CHECK(m.launch("x", 100u << 20) == -1);                               // di atas maxQuota
  f.freePs = 600 * 1024; CHECK(m.launch("x", 256 * 1024) == -3);        // PSRAM tidak cukup (cadangan sistem terjaga)
  f.freePs = 8u << 20;
  int a = m.launch("app-a", 256 * 1024); CHECK(a == 0);
  int b = m.launch("app-b", 128 * 1024); CHECK(b == 1);
  m.launch("c", 64 * 1024); m.launch("d", 64 * 1024); CHECK(m.launch("e", 64 * 1024) == -2);   // slot penuh
  Arena* ar = m.arena(a); CHECK(ar != nullptr);
  void* p1 = ar->alloc(1000); void* p2 = ar->alloc(5000); ar->free(p1); (void)p2;   // p2 sengaja bocor
  CHECK(ar->alloc(300 * 1024) == nullptr);                               // quota keras: app tak bisa melebihi arena
  LeakReport r = m.release(a);
  CHECK(r.leakBlocks == 1); CHECK(r.leakBytes == 5000); CHECK(r.peakBytes == 6000); CHECK(!r.corrupted); CHECK(r.badOps == 0);
  CHECK(m.arena(a) == nullptr);                                          // slot dibebaskan
  CHECK(f.frees == 1);
  LeakReport r2 = m.release(a); CHECK(r2.leakBlocks == 0 && f.frees == 1);   // release dobel aman
  CHECK(m.launch("z", 64 * 1024) == 0);                                  // slot dipakai ulang
  // korupsi terlapor
  Arena* ab = m.arena(b); uint8_t* q = (uint8_t*)ab->alloc(16); q[16] = 0; LeakReport r3 = m.release(b); CHECK(r3.corrupted);
  m.release(0); m.release(2); m.release(3);
  CHECK(f.allocs == f.frees);                                            // tidak ada PSRAM yang bocor
}

static int g_order[8], g_n = 0, g_lvl[8];
static void cbA(void* u, int lvl) { g_order[g_n] = (int)(intptr_t)u; g_lvl[g_n++] = lvl; }
static Fake* g_f = nullptr;
static void cbFree(void* u, int lvl) { (void)u; if (lvl >= 2) g_f->largest = 40 * 1024; }

static void testPressureAndGate() {
  Fake f; MemMgr m; m.init(mk(&f));
  CHECK(m.pressure() == 0);
  f.largest = 30 * 1024; CHECK(m.pressure() == 1);                       // kontigu kecil walau total besar
  f.largest = 60 * 1024; f.freeInt = 35 * 1024; CHECK(m.pressure() == 1);
  f.freeInt = 20 * 1024; CHECK(m.pressure() == 2);
  f.freeInt = 100 * 1024; f.largest = 10 * 1024; CHECK(m.pressure() == 2);
  // urutan reclaim = prio naik
  g_n = 0; CHECK(m.addReclaim(cbA, (void*)3, 30)); CHECK(m.addReclaim(cbA, (void*)1, 10)); CHECK(m.addReclaim(cbA, (void*)2, 20));
  CHECK(m.reclaim(1) == 3); CHECK(g_order[0] == 1 && g_order[1] == 2 && g_order[2] == 3 && g_lvl[0] == 1);
  m.removeReclaim(cbA, (void*)2); g_n = 0; CHECK(m.reclaim(2) == 2); CHECK(g_order[0] == 1 && g_order[1] == 3);
  // TLS gate: langsung lolos
  Fake f2; MemMgr m2; m2.init(mk(&f2)); f2.largest = 50 * 1024; CHECK(m2.tlsGate(32 * 1024, 1000)); CHECK(f2.delays == 0);
  // TLS gate: lolos setelah reclaim CRITICAL melonggarkan memori
  Fake f3; MemMgr m3; m3.init(mk(&f3)); g_f = &f3; f3.largest = 10 * 1024; m3.addReclaim(cbFree, nullptr, 0);
  CHECK(m3.tlsGate(32 * 1024, 1000)); CHECK(f3.largest == 40 * 1024);
  // TLS gate: gagal setelah timeout (jam palsu maju lewat delayMs)
  Fake f4; MemMgr m4; m4.init(mk(&f4)); f4.largest = 10 * 1024;
  CHECK(!m4.tlsGate(32 * 1024, 500)); CHECK(f4.now >= 500 && f4.now < 700); CHECK(m4.nGateFail == 1);
}

int main() {
  printf("[nyxmem] basics\n"); testBasics();
  printf("[nyxmem] canary\n"); testCanary();
  printf("[nyxmem] fuzz\n");
  testFuzz(1, 64 * 1024); testFuzz(2, 1 << 20); testFuzz(3, 4096 + 32); testFuzz(4, 200 * 1024); testFuzz(5, 300);
  printf("[nyxmem] manager\n"); testManager();
  printf("[nyxmem] pressure+gate\n"); testPressureAndGate();
  printf("%s: %d ok, %d gagal\n", g_fail ? "GAGAL" : "LULUS", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
