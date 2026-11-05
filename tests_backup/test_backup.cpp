// test_backup.cpp -- uji logika inti backup_core.h di komputer.
// Jalankan (dari folder repo):  g++ -std=c++11 -O1 -I. -o /tmp/test_backup tests_backup/test_backup.cpp && /tmp/test_backup
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include "backup_core.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ fails++; printf("GAGAL  %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

// "NVS palsu": ns/key -> (tipe, byte)
struct Val { uint8_t type; std::vector<uint8_t> d; };
typedef std::map<std::string, Val> Store;
static std::string K(const char* ns, const char* key) { return std::string(ns) + "/" + key; }
static void putU8(Store& s, const char* ns, const char* k, uint8_t v) { s[K(ns,k)] = { BK_T_U8, { v } }; }
static void putU32(Store& s, const char* ns, const char* k, uint32_t v) { s[K(ns,k)] = { BK_T_U32, { (uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24) } }; }
static void putStr(Store& s, const char* ns, const char* k, const char* v) { Val x; x.type = BK_T_STR; x.d.assign(v, v + strlen(v) + 1); s[K(ns,k)] = x; }
static void putBlob(Store& s, const char* ns, const char* k, const std::vector<uint8_t>& v) { s[K(ns,k)] = { BK_T_BLOB, v }; }

// meniru bkCollect() di .ino
static size_t dump(const Store& s, bool secrets, std::vector<uint8_t>& out, uint32_t epoch = 1762345678u) {
  out.assign(64 * 1024, 0);
  BkWriter w; bkBegin(w, out.data(), out.size());
  for (auto& kv : s) {
    size_t sl = kv.first.find('/');
    std::string ns = kv.first.substr(0, sl), key = kv.first.substr(sl + 1);
    if (bkNsSkip(ns.c_str())) continue;
    if (!secrets && bkIsSecret(ns.c_str(), key.c_str())) continue;
    bkAddRecord(w, ns.c_str(), key.c_str(), kv.second.type, kv.second.d.data(), (uint32_t)kv.second.d.size());
  }
  size_t n = bkFinish(w, secrets ? BK_FLAG_SECRETS : 0, epoch);
  out.resize(n);
  return n;
}
// meniru bkApply() di .ino
static int load(const std::vector<uint8_t>& f, Store& dst, int* skipped = nullptr) {
  BkHdr h; if (!bkVerify(f.data(), f.size(), h)) return -1;
  size_t pos = BK_HDR, end = f.size() - 4; int ok = 0; BkRec r;
  for (;;) {
    int q = bkNext(f.data(), end, pos, r);
    if (q < 0) return -2;
    if (q == 0) break;
    if (bkNsSkip(r.ns)) { if (skipped) (*skipped)++; continue; }
    dst[K(r.ns, r.key)] = { r.type, std::vector<uint8_t>(r.data, r.data + r.len) };
    ok++;
  }
  return ok;
}

int main() {
  Store s;
  putU8(s, "ui", "theme", 3); putU32(s, "ui", "bright", 200); putStr(s, "ui", "font", "Inter");
  putBlob(s, "homelay", "ord", { 1, 2, 3, 4, 5, 0, 255, 7 });
  putStr(s, "wifi", "ssid", "RumahKu"); putStr(s, "wifi", "pass", "rahasia123");
  putStr(s, "sip", "pass", "sipsecret");
  putStr(s, "gemini", "apikey", "AIza-xxxx");
  putStr(s, "rcfg", "ssid", "Accretion-Phone"); putStr(s, "rcfg", "pass3", "abcd2345"); putU8(s, "rcfg", "fps", 12);
  putU8(s, "nvs.net80211", "sta.ssid", 1); putU8(s, "phy", "cal", 9); putU8(s, "otaguard", "boot", 1);

  // ---- roundtrip tanpa sandi
  std::vector<uint8_t> f; size_t n = dump(s, false, f);
  CHECK(n > 0 && n == f.size(), "dump menghasilkan file (%zu)", n);
  BkHdr h; CHECK(bkVerify(f.data(), f.size(), h), "verify ok");
  CHECK(h.epoch == 1762345678u, "epoch tersimpan");
  CHECK(!(h.flags & BK_FLAG_SECRETS), "flag sandi mati");
  Store d; int ok = load(f, d);
  CHECK(ok == 6, "6 rekaman dipulihkan, dapat %d", ok);          // ui x3, homelay x1, rcfg ssid+fps
  CHECK(h.count == 6, "header menghitung 6, dapat %d", h.count);
  CHECK(d.count(K("ui","theme")) && d[K("ui","theme")].d[0] == 3, "u8 utuh");
  CHECK(d[K("ui","bright")].d == s[K("ui","bright")].d, "u32 utuh");
  CHECK(d[K("ui","font")].d == s[K("ui","font")].d, "string utuh (termasuk NUL)");
  CHECK(d[K("homelay","ord")].d == s[K("homelay","ord")].d, "blob biner utuh (ada byte 0 & 255)");
  CHECK(!d.count(K("wifi","ssid")) && !d.count(K("wifi","pass")), "wifi TIDAK ikut");
  CHECK(!d.count(K("sip","pass")) && !d.count(K("gemini","apikey")), "sip & gemini TIDAK ikut");
  CHECK(!d.count(K("rcfg","pass3")), "sandi AP remote TIDAK ikut");
  CHECK(d.count(K("rcfg","ssid")) && d.count(K("rcfg","fps")), "rcfg non-rahasia ikut");
  CHECK(!d.count(K("nvs.net80211","sta.ssid")) && !d.count(K("phy","cal")) && !d.count(K("otaguard","boot")), "namespace sistem TIDAK ikut");

  // ---- dengan sandi
  std::vector<uint8_t> f2; dump(s, true, f2);
  CHECK(bkVerify(f2.data(), f2.size(), h) && (h.flags & BK_FLAG_SECRETS), "flag sandi nyala");
  Store d2; ok = load(f2, d2);
  CHECK(ok == 11, "11 rekaman dgn sandi, dapat %d", ok);
  CHECK(d2[K("wifi","pass")].d == s[K("wifi","pass")].d, "sandi wifi utuh");
  CHECK(d2.count(K("rcfg","pass3")), "sandi AP remote ikut");

  // ---- file rusak: satu bit dibalik di mana pun -> ditolak
  int rejected = 0, total = 0;
  for (size_t i = BK_HDR; i < f.size(); i += 3) { std::vector<uint8_t> g = f; g[i] ^= 0x10; total++; if (!bkVerify(g.data(), g.size(), h)) rejected++; }
  CHECK(rejected == total, "semua bit-flip ditolak (%d/%d)", rejected, total);
  // dipotong
  for (size_t cut : { (size_t)1, (size_t)4, (size_t)20, f.size() - 5 }) {
    std::vector<uint8_t> g(f.begin(), f.end() - cut);
    CHECK(!bkVerify(g.data(), g.size(), h), "file dipotong %zu byte ditolak", cut);
  }
  CHECK(!bkVerify(f.data(), 5, h), "file terlalu pendek ditolak");
  std::vector<uint8_t> junk(200, 0x41); CHECK(!bkVerify(junk.data(), junk.size(), h), "file sampah ditolak");
  std::vector<uint8_t> g = f; g[0] = 'X'; CHECK(!bkVerify(g.data(), g.size(), h), "magic salah ditolak");
  g = f; g[7] = 99; CHECK(!bkVerify(g.data(), g.size(), h), "versi tak dikenal ditolak");

  // ---- file berbahaya tapi CRC-nya BENAR (dibuat penyerang): panjang melampaui buffer, tipe salah, dsb.
  {
    std::vector<uint8_t> m(BK_HDR, 0); memcpy(m.data(), "NYXB1\0", 6); m[7] = BK_VERSION;
    auto fin = [&](std::vector<uint8_t> body) {
      std::vector<uint8_t> x = m; x.insert(x.end(), body.begin(), body.end()); x.push_back(0);
      uint32_t c = bkCrc32(0, x.data() + BK_HDR, x.size() - BK_HDR);
      for (int i = 0; i < 4; i++) x.push_back((uint8_t)(c >> (8 * i)));
      return x; };
    // panjang data 0xFFFFFFFF
    std::vector<uint8_t> b1 = { 2,'u','i', 1,'k', BK_T_BLOB, 0xFF,0xFF,0xFF,0xFF };
    CHECK(load(fin(b1), d) == -2, "panjang ngawur ditolak");
    // u32 dengan panjang 2
    std::vector<uint8_t> b2 = { 2,'u','i', 1,'k', BK_T_U32, 2,0,0,0, 1,2 };
    CHECK(load(fin(b2), d) == -2, "u32 panjang salah ditolak");
    // tipe tak dikenal
    std::vector<uint8_t> b3 = { 2,'u','i', 1,'k', 77, 1,0,0,0, 1 };
    CHECK(load(fin(b3), d) == -2, "tipe tak dikenal ditolak");
    // string tanpa NUL
    std::vector<uint8_t> b4 = { 2,'u','i', 1,'k', BK_T_STR, 3,0,0,0, 'a','b','c' };
    CHECK(load(fin(b4), d) == -2, "string tanpa NUL ditolak");
    // nama terlalu panjang
    std::vector<uint8_t> b5 = { 16,'a','a','a','a','a','a','a','a','a','a','a','a','a','a','a','a', 1,'k', BK_T_U8, 1,0,0,0, 1 };
    CHECK(load(fin(b5), d) == -2, "nama 16 karakter ditolak");
    // menulis ke namespace sistem -> dilewati, bukan ditulis
    Store victim; int skipped = 0;
    std::vector<uint8_t> b6 = { 12,'n','v','s','.','n','e','t','8','0','2','1','1', 1,'k', BK_T_U8, 1,0,0,0, 1,
                                8,'o','t','a','g','u','a','r','d', 1,'k', BK_T_U8, 1,0,0,0, 1 };
    int r6 = load(fin(b6), victim, &skipped);
    CHECK(r6 == 0 && victim.empty() && skipped == 2, "namespace sistem dilewati (%d, skipped %d)", r6, skipped);
    // file kosong-tapi-sah (0 rekaman)
    CHECK(load(fin({}), victim) == 0, "backup kosong sah, 0 rekaman");
  }

  // ---- buffer kepenuhan -> gagal bersih, tidak menulis melewati batas
  {
    std::vector<uint8_t> tiny(64, 0xAA); BkWriter w; bkBegin(w, tiny.data(), tiny.size());
    std::vector<uint8_t> big(200, 1);
    bool ok1 = bkAddRecord(w, "ui", "ord", BK_T_BLOB, big.data(), (uint32_t)big.size());
    CHECK(!ok1 && w.over, "buffer penuh terdeteksi");
    CHECK(bkFinish(w, 0, 0) == 0, "finish gagal bila penuh");
    CHECK(tiny[63] == 0xAA, "tidak menulis melewati batas buffer");
  }
  // ---- argumen tak valid
  {
    std::vector<uint8_t> b(1024); BkWriter w; bkBegin(w, b.data(), b.size()); uint8_t v = 1;
    CHECK(!bkAddRecord(w, "", "k", BK_T_U8, &v, 1), "ns kosong ditolak");
    CHECK(!bkAddRecord(w, "abcdefghijklmnop", "k", BK_T_U8, &v, 1), "ns 16 karakter ditolak");
    CHECK(!bkAddRecord(w, "ui", "k", BK_T_U32, &v, 1), "u32 dgn 1 byte ditolak");
    CHECK(!bkAddRecord(w, "ui", "k", 99, &v, 1), "tipe 99 ditolak");
    CHECK(w.count == 0, "tidak ada yang tercatat");
  }
  // ---- semua tipe skalar
  {
    Store a; a[K("t","u8")] = { BK_T_U8, {1} }; a[K("t","i8")] = { BK_T_I8, {0xFF} };
    a[K("t","u16")] = { BK_T_U16, {1,2} }; a[K("t","i16")] = { BK_T_I16, {3,4} };
    a[K("t","u32")] = { BK_T_U32, {1,2,3,4} }; a[K("t","i32")] = { BK_T_I32, {5,6,7,8} };
    a[K("t","u64")] = { BK_T_U64, {1,2,3,4,5,6,7,8} }; a[K("t","i64")] = { BK_T_I64, {8,7,6,5,4,3,2,1} };
    std::vector<uint8_t> ff; dump(a, false, ff); Store b; int n8 = load(ff, b); CHECK(n8 == 8, "8 tipe skalar roundtrip (%d)", n8);
    bool same = true; for (auto& kv : a) same = same && b.count(kv.first) && b[kv.first].d == kv.second.d && b[kv.first].type == kv.second.type;
    CHECK(same, "nilai & tipe tiap skalar identik");
  }
  // ---- nama file
  CHECK(bkParseName("nyx_007.nbk") == 7, "parse 007");
  CHECK(bkParseName("/backup/nyx_123.nbk") == 123, "parse dgn path");
  CHECK(bkParseName("nyx_1.nbk") == 1, "parse 1 digit");
  CHECK(bkParseName("nyx_.nbk") == -1 && bkParseName("nyx_12.txt") == -1 && bkParseName("foo.nbk") == -1 && bkParseName("nyx_12x.nbk") == -1, "nama salah ditolak");
  char nm[40]; bkMakeName(nm, sizeof(nm), 5); CHECK(!strcmp(nm, "/backup/nyx_005.nbk"), "make name %s", nm);
  // ---- CRC: nilai standar zlib untuk "123456789" = CBF43926
  CHECK(bkCrc32(0, (const uint8_t*)"123456789", 9) == 0xCBF43926u, "CRC32 standar");
  uint32_t part = bkCrc32(bkCrc32(0, (const uint8_t*)"12345", 5), (const uint8_t*)"6789", 4);
  CHECK(part == 0xCBF43926u, "CRC32 bertahap sama");

  printf("%d pemeriksaan, %d gagal\n", checks, fails);
  return fails ? 1 : 0;
}
