// Tes sisi host app native di PC. App contoh (examples/native/*) dikompilasi sebagai .so lalu
// dijalankan lewat dlopen terhadap Backend palsu (framebuffer + folder sementara).
//   Dibangun oleh run.sh:  g++ ... -fsanitize=address,undefined test_host.cpp -ldl
#include "nyxhost.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

using namespace nyx;
static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) g_ok++; else { g_fail++; printf("  GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ------------------------------ rig palsu ------------------------------
#define VW 320
#define VH 200
struct Rig {
  std::vector<uint16_t> fb = std::vector<uint16_t>(VW * VH, 0);
  std::vector<std::string> logs, toasts;
  std::string root;
  FILE* files[16] = {0}; int nClosed = 0, nOpened = 0;
  bool tiltOn = true; uint32_t now = 1000, rng = 12345;
  // http palsu
  int httpPolls = 0, httpActive = 0, httpBegins = 0; std::string httpBody = "HELLO-NET";
  // memori sistem palsu
  size_t freeInt = 100 * 1024, largest = 60 * 1024, freePs = 8u << 20;
  int bigAllocs = 0, bigFrees = 0;
};
static Rig* R(void* c) { return (Rig*)c; }
static void px(Rig* r, int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < VW && y < VH) r->fb[(size_t)y * VW + x] = c; }
static void b_clear(void* c, uint16_t col) { for (auto& p : R(c)->fb) p = col; }
static void b_fill(void* c, int x, int y, int w, int h, uint16_t col) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(R(c), x + i, y + j, col); }
static void b_rect(void* c, int x, int y, int w, int h, uint16_t col) { for (int i = 0; i < w; i++) { px(R(c), x + i, y, col); px(R(c), x + i, y + h - 1, col); } for (int j = 0; j < h; j++) { px(R(c), x, y + j, col); px(R(c), x + w - 1, y + j, col); } }
static void b_rrect(void* c, int x, int y, int w, int h, int, uint16_t col) { b_fill(c, x, y, w, h, col); }
static void b_pixel(void* c, int x, int y, uint16_t col) { px(R(c), x, y, col); }
static void b_line(void* c, int x0, int y0, int x1, int y1, uint16_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (int n = 0; n < 4000; n++) { px(R(c), x0, y0, col); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void b_circle(void* c, int x, int y, int r, uint16_t col) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) px(R(c), x + i, y + j, col); }
static void b_ring(void* c, int x, int y, int r, uint16_t col) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) { int d = i * i + j * j; if (d <= r * r && d >= (r - 1) * (r - 1)) px(R(c), x + i, y + j, col); } }
static void b_text(void* c, int x, int y, const char* s, uint16_t col, int sz) { for (int k = 0; s[k]; k++) for (int j = 0; j < 7 * sz; j++) for (int i = 0; i < 5 * sz; i++) if (s[k] != ' ' && ((i / sz + j / sz + s[k]) % 3)) px(R(c), x + k * 6 * sz + i, y + j, col); }
static int  b_textw(void*, const char* s, int sz) { return (int)strlen(s) * 6 * sz; }
static void b_blit(void* c, int x, int y, int w, int h, const uint16_t* p) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(R(c), x + i, y + j, p[j * w + i]); }
static void b_view(void*, int* w, int* h) { *w = VW; *h = VH; }
static bool b_tilt(void* c, int* x, int* y, int* z) { if (!R(c)->tiltOn) return false; *x = 120; *y = -80; *z = 980; return true; }
static uint32_t b_millis(void* c) { return R(c)->now += 33; }
static uint32_t b_rand(void* c) { Rig* r = R(c); r->rng ^= r->rng << 13; r->rng ^= r->rng >> 17; r->rng ^= r->rng << 5; return r->rng; }
static void b_log(void* c, const char* s) { R(c)->logs.push_back(s); }
static void b_toast(void* c, const char* s) { R(c)->toasts.push_back(s); }
static int  b_fopen(void* c, const char* path, int mode) {
  Rig* r = R(c); FILE* f = fopen(path, mode == 0 ? "rb" : (mode == 1 ? "wb" : "ab")); if (!f) return -1;
  for (int i = 0; i < 16; i++) if (!r->files[i]) { r->files[i] = f; r->nOpened++; return i; }
  fclose(f); return -1;
}
static int  b_fread(void* c, int h, void* buf, int n) { return (int)fread(buf, 1, (size_t)n, R(c)->files[h]); }
static int  b_fwrite(void* c, int h, const void* buf, int n) { return (int)fwrite(buf, 1, (size_t)n, R(c)->files[h]); }
static void b_fclose(void* c, int h) { Rig* r = R(c); if (r->files[h]) { fclose(r->files[h]); r->files[h] = nullptr; r->nClosed++; } }
static int  b_fseek(void* c, int h, int p) { return fseek(R(c)->files[h], p, SEEK_SET); }
static int  b_fsize(void* c, int h) { FILE* f = R(c)->files[h]; long cur = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, cur, SEEK_SET); return (int)n; }
static int  b_fexists(void*, const char* p) { struct stat st; return stat(p, &st) == 0; }
static int  b_fremove(void*, const char* p) { return unlink(p); }
static int  b_hbegin(void* c, const char*) { Rig* r = R(c); if (r->httpActive) return -6; r->httpActive = 1; r->httpPolls = 0; r->httpBegins++; return 7; }
static int  b_hpoll(void* c, int, int* code, uint32_t* len) { Rig* r = R(c); if (++r->httpPolls < 3) return NYX_HTTP_RUNNING; *code = 200; *len = (uint32_t)r->httpBody.size(); return NYX_HTTP_DONE; }
static const uint8_t* b_hdata(void* c, int) { return (const uint8_t*)R(c)->httpBody.data(); }
static void b_hclose(void* c, int) { R(c)->httpActive = 0; }

static Backend mkBackend(Rig* r) {
  Backend b; memset(&b, 0, sizeof(b)); b.ctx = r;
  b.clear = b_clear; b.fillRect = b_fill; b.rect = b_rect; b.roundRect = b_rrect; b.pixel = b_pixel; b.line = b_line;
  b.circle = b_circle; b.ring = b_ring; b.text = b_text; b.textW = b_textw; b.blit = b_blit; b.viewport = b_view; b.tilt = b_tilt;
  b.millis = b_millis; b.rand32 = b_rand; b.log = b_log; b.toast = b_toast;
  b.fopen = b_fopen; b.fread = b_fread; b.fwrite = b_fwrite; b.fclose = b_fclose; b.fseek = b_fseek; b.fsize = b_fsize;
  b.fexists = b_fexists; b.fremove = b_fremove; b.httpBegin = b_hbegin; b.httpPoll = b_hpoll; b.httpData = b_hdata; b.httpClose = b_hclose;
  return b;
}
static void* m_big(void* c, size_t n) { R(c)->bigAllocs++; R(c)->freePs -= n; return calloc(1, n); }
static void  m_bigFree(void* c, void* p) { R(c)->bigFrees++; free(p); }
static size_t m_fi(void* c) { return R(c)->freeInt; }
static size_t m_lg(void* c) { return R(c)->largest; }
static size_t m_ps(void* c) { return R(c)->freePs; }
static void   m_delay(void* c, uint32_t ms) { R(c)->now += ms; }
static uint32_t m_now(void* c) { return R(c)->now; }
static MemBackend mkMem(Rig* r) { MemBackend b; b.ctx = r; b.bigAlloc = m_big; b.bigFree = m_bigFree; b.freeInternal = m_fi; b.largestInternal = m_lg; b.freePsram = m_ps; b.delayMs = m_delay; b.nowMs = m_now; return b; }

static std::string readFile(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) return ""; std::string s; char b[256]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n); fclose(f); return s; }
static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
static bool hasLog(const Rig& r, const char* s) { for (auto& l : r.logs) if (l == s) return true; return false; }

static void dumpPpm(const Rig& r, const char* path) {
  FILE* f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", VW, VH);
  for (uint16_t p : r.fb) { uint8_t rgb[3] = {(uint8_t)(((p >> 11) & 31) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}

// ------------------------- menjalankan app .so -------------------------
struct TouchStep { int frame; int state; int x, y; };
struct RunOut { bool loaded = false, entryOk = false; int enterRc = 0; LeakReport leak; uint32_t badFree = 0, denied = 0; };

static RunOut runApp(Rig& rig, MemMgr& mm, const std::string& app, const std::string& manifestText, int frames,
                     const std::vector<TouchStep>& touches, const std::string& ppm = "") {
  RunOut out; memset(&out.leak, 0, sizeof(out.leak));
  Manifest mf; parseManifest(manifestText.c_str(), manifestText.size(), &mf);
  if (!mf.valid) return out;
  std::string dataDir = rig.root + "/apps/" + app + "/data";
  system(("mkdir -p '" + dataDir + "'").c_str());
  int id = mm.launch(mf.name, mf.memKb * 1024); if (id < 0) return out;
  static Host host; hostInit(&host, mkBackend(&rig), &mm, id, mf, dataDir.c_str()); nyxHostCur() = &host;
  void* so = dlopen(("/tmp/" + app + ".so").c_str(), RTLD_NOW); if (!so) { printf("dlopen: %s\n", dlerror()); mm.release(id); return out; }
  out.loaded = true;
  typedef int (*MainFn)(int, char**); MainFn mainFn = (MainFn)dlsym(so, "main");
  char* argv0 = (char*)(void*)&host.api; char* argv[2] = {argv0, nullptr};
  if (mainFn && mainFn(NYX_ENTRY_ARGC, argv) == 0 && host.opsSet) {
    out.entryOk = true; out.enterRc = host.ops.enter ? host.ops.enter() : 0;
    for (int f = 0; f < frames && out.enterRc == 0; f++) {
      int st = 0, tx = 0, ty = 0; for (auto& t : touches) if (t.frame == f) { st = t.state; tx = t.x; ty = t.y; }
      hostSetTouch(&host, st, tx, ty);
      if (host.ops.frame) host.ops.frame();
      if (host.quitReq) break;
    }
    if (!ppm.empty()) dumpPpm(rig, ppm.c_str());
    if (host.ops.leave) host.ops.leave();
  }
  hostCloseAll(&host); out.badFree = host.badFree; out.denied = host.deniedCalls;
  out.leak = mm.release(id);
  nyxHostCur() = nullptr; dlclose(so);
  return out;
}

static std::string loadManifest(const std::string& app) { return readFile("../../examples/native/" + app + "/manifest.txt"); }

// ------------------------------- tes ----------------------------------
static void testManifest() {
  Manifest m;
  const char* ok = "# komentar\r\nname = Halo\r\nsym=H\r\ncolor=ff8800\r\nperms=fs, net\r\nmem_kb=128\r\nstack_kb=16\r\nelf=x.elf\r\nkunci_asing=1\r\n";
  // catatan: "fs, net" mengandung spasi setelah koma -> token " net" tak dikenal -> ditolak
  parseManifest(ok, strlen(ok), &m); CHECK(!m.valid);
  const char* ok2 = "# komentar\r\nname = Halo\r\nsym=H\r\ncolor=ff8800\r\nperms=fs,net,tilt,notify\r\nmem_kb=128\r\nstack_kb=16\r\nelf=x.elf\r\nkunci_asing=1\r\n";
  parseManifest(ok2, strlen(ok2), &m);
  CHECK(m.valid); CHECK(!strcmp(m.name, "Halo")); CHECK(m.sym == 'H'); CHECK(m.color == nyxRgb565(0xff, 0x88, 0x00));
  CHECK(m.perms == (NYX_PERM_FS | NYX_PERM_NET | NYX_PERM_TILT | NYX_PERM_NOTIFY)); CHECK(m.memKb == 128 && m.stackKb == 16); CHECK(!strcmp(m.elf, "x.elf"));
  const char* d = "name=Kecil\n"; parseManifest(d, strlen(d), &m);
  CHECK(m.valid && m.memKb == 256 && m.stackKb == 8 && !strcmp(m.elf, "app.elf") && m.perms == 0);       // default
  const char* bad[] = {"sym=H\n", "name=\n", "name=012345678901234567890123456789\nname=x\n", "name=a\nperms=root\n", "name=a\ncolor=12345\n",
    "name=a\ncolor=gg0000\n", "name=a\nmem_kb=63\n", "name=a\nmem_kb=99999\n", "name=a\nmem_kb=abc\n", "name=a\nstack_kb=3\n",
    "name=a\nelf=../x.elf\n", "name=a\nelf=sub/x.elf\n", "name=a\nelf=.hidden\n", "name=a\nsym=ab\n", "name=a\nsym=\n"};
  for (const char* t : bad) { parseManifest(t, strlen(t), &m); CHECK(!m.valid); }
  char big[400]; memset(big, 'x', sizeof(big)); parseManifest(big, sizeof(big), &m); CHECK(!m.valid);       // baris kelewat panjang tak membuat crash
}

static void testSandbox() {
  char out[160]; const char* dd = "/apps/x/data";
  struct { const char* in; bool ok; } t[] = {
    {"a.txt", true}, {"sub/a.txt", true}, {"a.b.c", true}, {"", false}, {"/etc/passwd", false}, {"../a", false}, {"a/../b", false},
    {"./a", false}, {"a/.", false}, {"a//b", false}, {"a/", false}, {"a\\b", false}, {"c:x", false}, {"a*b", false}, {"a?b", false},
    {"a|b", false}, {"a\nb", false}, {"..", false}, {".", false}, {"...", true}, {"a b.txt", true}};
  for (auto& c : t) { bool r = sandboxPath(c.in, dd, out, sizeof(out)); CHECK(r == c.ok); if (r) CHECK(!strncmp(out, dd, strlen(dd)) && out[strlen(dd)] == '/'); }
  std::string longp(60, 'a'); CHECK(!sandboxPath(longp.c_str(), dd, out, sizeof(out)));                          // >48
  CHECK(!sandboxPath("abc", dd, out, 10));                                                                       // buffer kecil
  CHECK(!sandboxPath(nullptr, dd, out, sizeof(out)));
}

static void testApps() {
  char tmpl[] = "/tmp/nyxtestXXXXXX"; Rig rig; rig.root = mkdtemp(tmpl);
  MemMgr mm; mm.init(mkMem(&rig));
  // ---- hello ----
  { rig.logs.clear(); std::vector<TouchStep> ts; for (int f = 1; f < 10; f++) ts.push_back({f, NYX_TOUCH_DOWN, 100, 100});
    RunOut o = runApp(rig, mm, "hello", loadManifest("hello"), 10, ts, "/tmp/nyx_hello.ppm");
    CHECK(o.loaded && o.entryOk && o.enterRc == 0);
    CHECK(hasLog(rig, "hello: enter")); CHECK(hasLog(rig, "hello: frame 3")); CHECK(hasLog(rig, "hello: leave"));
    CHECK(rig.fb[(size_t)100 * VW + 100] == nyxRgb565(255, 70, 70));                  // lingkaran merah di titik sentuh
    CHECK(rig.fb[(size_t)190 * VW + 300] == nyxRgb565(12, 14, 34));                   // latar belakang
    CHECK(o.leak.leakBlocks == 0 && !o.leak.corrupted && o.badFree == 0); }
  // ---- bounce (tilt aktif lalu mati) ----
  { for (int tilt = 1; tilt >= 0; tilt--) { rig.tiltOn = tilt; rig.logs.clear();
      RunOut o = runApp(rig, mm, "bounce", loadManifest("bounce"), 300, {}, tilt ? "/tmp/nyx_bounce.ppm" : "");
      CHECK(o.loaded && o.entryOk && o.enterRc == 0); CHECK(o.leak.leakBlocks == 0 && !o.leak.corrupted);
      int colored = 0; uint16_t bg = nyxRgb565(6, 6, 16); for (uint16_t p : rig.fb) if (p != bg) colored++;
      CHECK(colored > 200);                                                              // bola tergambar di dalam viewport
      CHECK(o.denied == 0); } rig.tiltOn = true; }
  // ---- bounce TANPA izin tilt: tilt() ditolak (deniedCalls naik) tapi app tetap jalan ----
  { std::string mf = loadManifest("bounce"); size_t p = mf.find("perms=tilt"); mf.replace(p, 10, "perms=");
    RunOut o = runApp(rig, mm, "bounce", mf, 20, {}); CHECK(o.entryOk && o.denied == 20); }
  // ---- memhog: quota ditegakkan, arena terpakai ulang ----
  { rig.logs.clear(); RunOut o = runApp(rig, mm, "memhog", loadManifest("memhog"), 3, {}, "/tmp/nyx_memhog.ppm");
    CHECK(o.entryOk && hasLog(rig, "memhog ok")); CHECK(o.leak.leakBlocks == 0 && !o.leak.corrupted);
    CHECK(o.leak.nFail >= 2);                                                            // penolakan: akhir pengisian + permintaan 2x quota
    CHECK(o.leak.peakBytes > 40000 && o.leak.peakBytes <= 64 * 1024); }
  // ---- notes: persisten antar-peluncuran + sandbox ----
  { for (int run = 1; run <= 3; run++) { rig.logs.clear(); RunOut o = runApp(rig, mm, "notes", loadManifest("notes"), 1, {});
      CHECK(o.entryOk); CHECK(hasLog(rig, "notes: sandbox ok")); CHECK(o.leak.leakBlocks == 0);
      CHECK(readFile(rig.root + "/apps/notes/data/count.txt") == std::to_string(run)); }
    CHECK(!exists(rig.root + "/apps/notes/escape.txt")); CHECK(!exists(rig.root + "/apps/escape.txt")); CHECK(!exists(rig.root + "/escape.txt"));
    CHECK(!exists("/tmp/escape.txt")); }
  // ---- notes TANPA izin fs: semua akses file ditolak, tidak ada berkas dibuat ----
  { std::string mf = loadManifest("notes"); size_t p = mf.find("perms=fs,notify"); mf.replace(p, 15, "perms=notify");
    rig.logs.clear(); RunOut o = runApp(rig, mm, "notes", mf, 1, {}); CHECK(o.entryOk && o.denied >= 5);
    CHECK(readFile(rig.root + "/apps/notes/data/count.txt") == "3"); }                  // tetap 3: tidak berubah
  // ---- leaky: bocor & salah pakai terdeteksi, bukan crash ----
  { RunOut o = runApp(rig, mm, "leaky", loadManifest("leaky"), 2, {});
    CHECK(o.entryOk); CHECK(o.leak.leakBlocks == 3);                                      // leak1, leak2, o
    CHECK(o.leak.leakBytes == 100 + 2000 + 32); CHECK(o.badFree == 2); CHECK(o.leak.badOps == 2); CHECK(o.leak.corrupted); }
  CHECK(rig.bigAllocs == rig.bigFrees);                                                   // semua arena PSRAM dikembalikan
  system(("rm -rf '" + rig.root + "'").c_str());
}

static void testApiDirect() {
  char tmpl[] = "/tmp/nyxtestXXXXXX"; Rig rig; rig.root = mkdtemp(tmpl);
  MemMgr mm; mm.init(mkMem(&rig));
  Manifest mf; const char* txt = "name=T\nperms=fs,net,notify\nmem_kb=64\n"; parseManifest(txt, strlen(txt), &mf);
  std::string dd = rig.root + "/d"; system(("mkdir -p '" + dd + "'").c_str());
  int id = mm.launch("T", 64 * 1024); static Host h; hostInit(&h, mkBackend(&rig), &mm, id, mf, dd.c_str()); nyxHostCur() = &h;
  const NyxApi* a = &h.api;
  CHECK(a->magic == NYX_API_MAGIC && a->size == sizeof(NyxApi) && a->perms == mf.perms);
  // argumen sampah tidak boleh crash
  a->fill_rect(0, 0, -5, 10, 1); a->fill_rect(0, 0, 10, -5, 1); a->text(0, 0, nullptr, 1, 1); a->text(0, 0, "x", 1, 99); a->blit(0, 0, 10, 10, nullptr);
  a->blit(0, 0, -1, 3, (const uint16_t*)"ab"); a->circle(0, 0, -1, 1); a->circle(0, 0, 100000, 1); a->round_rect(0, 0, 4, 4, -3, 1); a->round_rect(0, 0, 4, 4, 9999, 1);
  a->log(nullptr); a->toast(nullptr); CHECK(a->touch(nullptr, nullptr) == 0); CHECK(a->text_w(nullptr, 2) == 0); CHECK(a->text_w("abc", 2) == 36);
  CHECK(a->fread(0, nullptr, 5) == NYX_E_INVAL); CHECK(a->fread(9, (char*)"x", 1) == NYX_E_INVAL); CHECK(a->fclose(3) == NYX_E_INVAL);
  CHECK(a->fopen(nullptr, 0) == NYX_E_INVAL); CHECK(a->fopen("a", 7) == NYX_E_INVAL); CHECK(a->fopen("nonexist.txt", NYX_FOPEN_READ) == NYX_E_NOENT);
  CHECK(a->fseek(1, 5) == NYX_E_INVAL); CHECK(a->fsize(1) == NYX_E_INVAL);
  // empat handle, kelima ditolak, hostCloseAll membebaskan
  int hs[5]; for (int i = 0; i < 4; i++) { std::string n = "f" + std::to_string(i); hs[i] = a->fopen(n.c_str(), NYX_FOPEN_WRITE); CHECK(hs[i] == i + 1); }
  CHECK(a->fopen("f9", NYX_FOPEN_WRITE) == NYX_E_BUSY);
  CHECK(a->fwrite(1, "abcdef", 6) == 6); CHECK(a->fsize(1) == 6); CHECK(a->fclose(1) == 0); CHECK(a->fclose(1) == NYX_E_INVAL);   // double close
  hostCloseAll(&h); CHECK(rig.nOpened == rig.nClosed);
  CHECK(a->fexists("f0") == 1); CHECK(a->fremove("f0") == 0); CHECK(a->fexists("f0") == 0); CHECK(a->fremove("f0") == NYX_E_NOENT);
  // toast dengan izin
  a->toast("halo"); CHECK(rig.toasts.size() == 1 && rig.toasts[0] == "halo");
  // memori: kalloc/realloc/free + free liar dihitung
  char* m = (char*)a->mem_alloc(100); CHECK(m && a->mem_used() == 100); m = (char*)a->mem_realloc(m, 5000); CHECK(m && a->mem_used() == 5000);
  CHECK(a->mem_alloc(a->mem_quota()) == nullptr); CHECK(a->mem_largest() < a->mem_quota());
  a->mem_free(m); CHECK(a->mem_used() == 0); a->mem_free(m); CHECK(h.badFree == 1);
  char* z = (char*)a->mem_calloc(64); bool zero = true; for (int i = 0; i < 64; i++) if (z[i]) zero = false; CHECK(zero); a->mem_free(z);
  // ---- HTTP: async, salin ke buffer app, satu permintaan sekaligus ----
  char buf[64]; memset(buf, 0, sizeof(buf)); int code = 0; size_t n = 0;
  CHECK(a->http_get("ftp://x", buf, sizeof(buf)) == NYX_E_INVAL); CHECK(a->http_get("http://x", buf, 0) == NYX_E_INVAL); CHECK(a->http_get(nullptr, buf, 5) == NYX_E_INVAL);
  int id1 = a->http_get("http://example.test/a", buf, sizeof(buf)); CHECK(id1 == 7);
  CHECK(a->http_get("http://example.test/b", buf, sizeof(buf)) == NYX_E_BUSY);
  CHECK(a->http_state(id1, &code, &n) == NYX_HTTP_RUNNING); CHECK(a->http_state(id1, &code, &n) == NYX_HTTP_RUNNING);
  CHECK(a->http_state(id1, &code, &n) == NYX_HTTP_DONE); CHECK(code == 200 && n == 9 && !memcmp(buf, "HELLO-NET", 9));
  CHECK(a->http_state(99, &code, &n) == NYX_HTTP_IDLE);                                  // id salah
  a->http_close(id1); CHECK(rig.httpActive == 0); CHECK(a->http_get("http://example.test/c", buf, sizeof(buf)) == 7);
  hostCloseAll(&h); CHECK(rig.httpActive == 0);                                          // kill saat HTTP berjalan: permintaan ditutup
  // potong ke kapasitas buffer
  char small[4]; rig.httpPolls = 0; int id2 = a->http_get("http://example.test/d", small, sizeof(small)); a->http_state(id2, 0, 0); a->http_state(id2, 0, 0);
  CHECK(a->http_state(id2, &code, &n) == NYX_HTTP_DONE && n == 4 && !memcmp(small, "HELL", 4)); a->http_close(id2);
  // ---- TLS gate: HTTPS menunggu blok kontigu internal ----
  rig.largest = 10 * 1024; uint32_t t0 = rig.now;
  CHECK(a->http_get("https://example.test/s", buf, sizeof(buf)) == NYX_E_NOMEM); CHECK(rig.now - t0 >= 1500);    // gagal setelah timeout, tidak membuka koneksi
  CHECK(rig.httpActive == 0);
  static Rig* gr; gr = &rig;
  mm.addReclaim([](void*, int lvl) { if (lvl >= 1) gr->largest = 50 * 1024; }, nullptr, 0);
  CHECK(a->http_get("https://example.test/s", buf, sizeof(buf)) == 7);                   // reclaim melonggarkan -> lolos
  a->http_close(7);
  // ---- izin net/fs/notify/tilt ditolak bila tidak diberikan ----
  Manifest mf2; const char* t2 = "name=U\nmem_kb=64\n"; parseManifest(t2, strlen(t2), &mf2);
  hostInit(&h, mkBackend(&rig), &mm, id, mf2, dd.c_str());
  CHECK(a->http_get("http://x/", buf, 8) == NYX_E_PERM || h.api.http_get("http://x/", buf, 8) == NYX_E_PERM);
  CHECK(h.api.fopen("a", 0) == NYX_E_PERM); CHECK(h.api.fexists("a") == NYX_E_PERM); CHECK(h.api.fremove("a") == NYX_E_PERM);
  int tx, ty, tz; CHECK(h.api.tilt(&tx, &ty, &tz) == NYX_E_PERM); size_t before = rig.toasts.size(); h.api.toast("x"); CHECK(rig.toasts.size() == before);
  CHECK(h.deniedCalls >= 6);
  mm.release(id); nyxHostCur() = nullptr; system(("rm -rf '" + rig.root + "'").c_str());
}

int main() {
  printf("[host] manifest\n"); testManifest();
  printf("[host] sandbox\n"); testSandbox();
  printf("[host] api langsung\n"); testApiDirect();
  printf("[host] app contoh (dlopen)\n"); testApps();
  printf("%s: %d ok, %d gagal\n", g_fail ? "GAGAL" : "LULUS", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
