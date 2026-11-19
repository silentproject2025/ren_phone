// test_cloud.cpp -- uji backup_cloud.h (base64, turunan kunci, JSON, alur enkripsi->dekripsi) di PC.
// g++ -std=c++11 -O1 -Ifirmware/phone -o /tmp/test_cloud tests/backup/test_cloud.cpp && /tmp/test_cloud
// Pencocokan dengan Python: python3 tests/backup/cloud_check.py
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "backup_core.h"
#include "backup_cloud.h"
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; printf("GAGAL  %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)
static int hx(const char* s, uint8_t* o) { int n = 0; for (; s[0] && s[1]; s += 2) { unsigned v; sscanf(s, "%2x", &v); o[n++] = (uint8_t)v; } return n; }
static void ph(const uint8_t* d, size_t n) { for (size_t i = 0; i < n; i++) printf("%02x", d[i]); printf("\n"); }

int main(int argc, char** argv) {
  if (argc > 1) {                                   // mode untuk cloud_check.py
    static uint8_t a[200000]; static char o[300000]; int na = argc > 2 ? hx(argv[2], a) : 0;
    if (!strcmp(argv[1], "b64")) { bkB64Enc(a, na, o, sizeof(o)); puts(o); }
    else if (!strcmp(argv[1], "derive")) { BkCloudKeys k; uint8_t chip[32]; hx(argv[2], chip); bkCloudDerive(chip, argv[3], k); printf("%s\n", k.token); ph(k.enc, 32); }
    return 0;
  }
  // base64 round-trip semua panjang kecil + data acak
  srand(7);
  for (int n = 0; n < 300; n++) {
    std::vector<uint8_t> d(n); for (auto& b : d) b = (uint8_t)rand();
    std::vector<char> e(bkB64Len(n) + 1); size_t el = bkB64Enc(d.data(), n, e.data(), e.size());
    CHECK(el == bkB64Len(n), "panjang b64 n=%d", n);
    std::vector<uint8_t> r(n + 3); long rl = bkB64Dec(e.data(), el, r.data(), r.size());
    CHECK(n == 0 ? rl == -1 : (rl == n && !memcmp(r.data(), d.data(), n)), "round-trip n=%d (rl=%ld)", n, rl);
  }
  // b64 ketat: tolak karakter asing, panjang salah, '=' di tengah, buffer kecil
  uint8_t t[16];
  CHECK(bkB64Dec("TWFu", 4, t, 16) == 3 && !memcmp(t, "Man", 3), "Man");
  CHECK(bkB64Dec("TW=u", 4, t, 16) == -1, "= di tengah ditolak");
  CHECK(bkB64Dec("TWF", 3, t, 16) == -1, "panjang bukan kelipatan 4");
  CHECK(bkB64Dec("TW-u", 4, t, 16) == -1, "alfabet url-safe ditolak");
  CHECK(bkB64Dec("TWFu", 4, t, 2) == -1, "buffer kecil ditolak");
  CHECK(bkB64Dec("====", 4, t, 16) == -1, "==== ditolak");

  // kelayakan rahasia chip
  uint8_t z[16] = {0}, f[16]; memset(f, 0xFF, 16); uint8_t g[16]; for (int i = 0; i < 16; i++) g[i] = (uint8_t)(i * 7 + 1);
  CHECK(!bkCloudUidOk(z, 16) && !bkCloudUidOk(f, 16) && bkCloudUidOk(g, 16) && !bkCloudUidOk(g, 8), "uid ok/tidak");

  // turunan kunci: deterministik, beda perangkat/rahasia -> beda, token != enc
  uint8_t c1[32], c2[32]; for (int i = 0; i < 32; i++) { c1[i] = (uint8_t)i; c2[i] = (uint8_t)(i + 1); }
  BkCloudKeys a, b, c, d;
  bkCloudDerive(c1, "0123456789abcdef", a); bkCloudDerive(c1, "0123456789abcdef", b);
  bkCloudDerive(c2, "0123456789abcdef", c); bkCloudDerive(c1, "fedcba9876543210", d);
  CHECK(!strcmp(a.token, b.token) && !memcmp(a.enc, b.enc, 32), "deterministik");
  CHECK(strcmp(a.token, c.token) && memcmp(a.enc, c.enc, 32), "rahasia beda -> kunci beda");
  CHECK(strcmp(a.token, d.token) && memcmp(a.enc, d.enc, 32), "kode perangkat beda -> kunci beda");
  char eh[65]; bkCloudHex(a.enc, 32, eh); CHECK(strcmp(eh, a.token) != 0, "token tidak sama dgn kunci enkripsi");
  CHECK(strlen(a.token) == 64, "token 64 hex");
  char code[24]; bkCloudCodeStr("0123456789abcdef", code); CHECK(!strcmp(code, "0123-4567-89ab-cdef"), "kode tampilan '%s'", code);

  // alur penuh: backup -> enkripsi (kunci enc, iter 1) -> JSON -> (server) -> b64 decode -> dekripsi -> verifikasi
  uint8_t buf[2048]; BkWriter w; bkBegin(w, buf, sizeof(buf));
  bkAddRecord(w, "ui", "theme", BK_T_U8, "\x03", 1); bkAddRecord(w, "wifi", "pass", BK_T_STR, "rahasia\0", 8);
  size_t len = bkFinish(w, BK_FLAG_SECRETS, 1762345678u);
  CHECK(len > 0, "backup terbentuk");
  uint8_t rnd[32]; for (int i = 0; i < 32; i++) rnd[i] = (uint8_t)(rand());
  std::vector<uint8_t> eb(len + BK_ENC_OVERHEAD + 16);
  size_t el = bkEncrypt(buf, len, a.enc, 32, rnd, rnd + 16, 1, eb.data(), eb.size());
  CHECK(el > len, "terenkripsi");
  CHECK(!memmem(eb.data(), el, "rahasia", 7), "isi tidak terbaca di data yang dikirim");
  std::vector<char> body(bkCloudPutCap(el));
  size_t bl = bkCloudBuildPut(eb.data(), el, "0123456789abcdef", a.token, body.data(), body.size());
  CHECK(bl > 0 && bl == strlen(body.data()), "JSON put");
  CHECK(strstr(body.data(), a.token) != nullptr && !strstr(body.data(), eh), "token dikirim, kunci enkripsi TIDAK");
  CHECK(!strncmp(strstr(body.data(), "\"p_data\":\"") + 10, "TllYRTEA", 8), "data diawali magic NYXE1 (TllYRTEA) -- server memeriksa ini");
  CHECK(bkCloudBuildPut(eb.data(), el, "0123456789abcdef", a.token, body.data(), 100) == 0, "buffer kecil ditolak");
  // simulasi balasan get_backup
  std::string resp = std::string("{\"status\":\"ok\",\"size\":") + std::to_string(el) + ",\"updated_at\":\"2026-11-20T01:02:03Z\",\"data\":\"" + (strstr(body.data(), "\"p_data\":\"") + 10);
  resp.resize(resp.size() - 2); resp += "\"}";
  const char *v; size_t vl;
  CHECK(bkCloudJsonStr(resp.data(), resp.size(), "status", &v, &vl) && bkCloudJsonEq(v, vl, "ok"), "status ok");
  CHECK(bkCloudJsonInt(resp.data(), resp.size(), "size") == (long)el, "size terbaca");
  CHECK(bkCloudJsonStr(resp.data(), resp.size(), "data", &v, &vl), "data ada");
  std::vector<uint8_t> raw(vl); long rl = bkB64Dec(v, vl, raw.data(), raw.size());
  CHECK(rl == (long)el && !memcmp(raw.data(), eb.data(), el), "b64 balik identik");
  std::vector<uint8_t> pl(rl); size_t plen = 0;
  CHECK(bkDecrypt(raw.data(), rl, a.enc, 32, pl.data(), pl.size(), &plen) == BK_DEC_OK && plen == len && !memcmp(pl.data(), buf, len), "dekripsi dgn kunci yang sama");
  BkHdr h; CHECK(bkVerify(pl.data(), plen, h) && (h.flags & BK_FLAG_SECRETS), "hasil dekripsi lolos CRC");
  CHECK(bkDecrypt(raw.data(), rl, c.enc, 32, pl.data(), pl.size(), &plen) == BK_DEC_PASS, "kunci perangkat lain DITOLAK");
  raw[100] ^= 1; CHECK(bkDecrypt(raw.data(), rl, a.enc, 32, pl.data(), pl.size(), &plen) == BK_DEC_PASS, "data diubah di server DITOLAK");

  // pembaca balasan
  const char* none = "{\"status\":\"none\"}";
  CHECK(bkCloudJsonStr(none, strlen(none), "status", &v, &vl) && bkCloudJsonEq(v, vl, "none") && !bkCloudJsonStr(none, strlen(none), "data", &v, &vl), "status none");
  CHECK(!bkCloudJsonStr("{\"data\":\"ab\\/c\"}", 15, "data", &v, &vl), "backslash ditolak");
  CHECK(!bkCloudJsonStr("{\"data\":\"abc", 12, "data", &v, &vl), "tak ditutup ditolak");
  char ps[16]; CHECK(bkCloudPlainStr(" \"too_soon\"\n", 12, ps, sizeof(ps)) && !strcmp(ps, "too_soon"), "balasan teks");
  CHECK(!bkCloudPlainStr("{\"a\":1}", 7, ps, sizeof(ps)) && !bkCloudPlainStr("\"aaaaaaaaaaaaaaaaaaaa\"", 22, ps, sizeof(ps)), "balasan bukan string / kepanjangan");
  char ab[200]; bkCloudBuildAuth("0123456789abcdef", a.token, 1, ab, sizeof(ab));
  CHECK(strstr(ab, "\"p_meta_only\":true") && ab[strlen(ab) - 1] == '}', "JSON auth meta");
  bkCloudBuildAuth("0123456789abcdef", a.token, -1, ab, sizeof(ab)); CHECK(!strstr(ab, "p_meta_only"), "JSON auth tanpa meta (delete)");

  if (fails) printf("%d dari %d GAGAL\n", fails, checks); else printf("SEMUA LULUS (%d pemeriksaan)\n", checks);
  return fails ? 1 : 0;
}
