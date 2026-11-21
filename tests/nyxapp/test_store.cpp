// Tes PC untuk nyxstore_core.h (pembaca indeks, validasi, status pasang, URL, format).
//   g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I ../../firmware/phone test_store.cpp -o /tmp/test_store
//   /tmp/test_store [path/ke/nyxapps.json]     (argumen opsional: indeks asli hasil nyxapps_release.py)
#include "nyxstore_core.h"
#include <assert.h>
#include <stdio.h>
#include <string>

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char* SHA_A = "f5e8145c179c8051e08d625c57d04f7aafdc81945c7933ae9a8bf90dacb9c959";
static const char* SHA_B = "0000000000000000000000000000000000000000000000000000000000000001";

static std::string app(const char* id, const char* extra = "") {
  std::string s = "{\"id\":\""; s += id; s += "\",\"name\":\"App "; s += id; s += "\",\"sym\":\"X\",\"color\":\"3060d0\","
      "\"perms\":[\"fs\",\"net\"],\"mem_kb\":64,\"stack_kb\":8,\"elf_size\":5020,\"sha256\":\""; s += SHA_A; s += "\"";
  s += extra; s += "}";
  return s;
}
static int parse(const std::string& j, nyxstore::Entry* out, int* sk = nullptr) {
  int s = 0; int n = nyxstore::parseIndex(j.c_str(), j.size(), out, NYXSTORE_MAX_APPS, &s); if (sk) *sk = s; return n;
}

int main(int argc, char** argv) {
  using namespace nyxstore;
  Entry e[NYXSTORE_MAX_APPS]; int sk = 0;

  // --- id / nama berkas / sha ---
  CHECK(idOk("hello")); CHECK(idOk("a_b-9")); CHECK(!idOk("")); CHECK(!idOk("a b")); CHECK(!idOk("../x")); CHECK(!idOk("a/b"));
  CHECK(!idOk("abcdefghijklmnopqrstuvwx")); CHECK(idOk("abcdefghijklmnopqrstuvw"));
  CHECK(fileNameOk("hello.elf")); CHECK(!fileNameOk("../hello.elf")); CHECK(!fileNameOk("a/b")); CHECK(!fileNameOk(".hidden")); CHECK(!fileNameOk(""));
  CHECK(!fileNameOk("a\\b")); CHECK(!fileNameOk("sp ace"));
  CHECK(shaOk(SHA_A)); CHECK(!shaOk("abc")); CHECK(!shaOk("F5E8145C179C8051E08D625C57D04F7AAFDC81945C7933AE9A8BF90DACB9C959"));
  { std::string bad(SHA_A); bad[10] = 'g'; CHECK(!shaOk(bad.c_str())); }

  // --- indeks normal (format hasil nyxapps_release.py) ---
  {
    std::string j = "{\n \"commit\": \"abc\", \"idf\": \"v5.5.5\",\n \"apps\": [\n" + app("hello", ",\"zip\":\"hello.zip\",\"elf\":\"hello.elf\",\"manifest\":\"hello.manifest.txt\",\"kind\":\"contoh\"")
        + ",\n" + app("leaky", ",\"kind\":\"uji\"") + "\n ]\n}";
    int n = parse(j, e, &sk);
    CHECK(n == 2); CHECK(sk == 0);
    CHECK(!strcmp(e[0].id, "hello")); CHECK(!strcmp(e[0].name, "App hello")); CHECK(e[0].sym == 'X');
    CHECK(e[0].color == rgb565(0x30, 0x60, 0xD0)); CHECK(!strcmp(e[0].perms, "fs,net")); CHECK(e[0].memKb == 64); CHECK(e[0].elfSize == 5020);
    CHECK(!strcmp(e[0].elf, "hello.elf")); CHECK(!strcmp(e[0].manifest, "hello.manifest.txt")); CHECK(!e[0].test);
    CHECK(e[1].test);
    // indeks lama (tanpa elf/manifest): nama diturunkan dari id
    CHECK(!strcmp(e[1].elf, "leaky.elf")); CHECK(!strcmp(e[1].manifest, "leaky.manifest.txt"));
  }
  // --- kunci tak dikenal (objek & larik bersarang) dilewati; sha huruf besar dinormalkan ---
  {
    std::string up(SHA_A); for (auto& ch : up) if (ch >= 'a' && ch <= 'f') ch = (char)(ch - 32);
    std::string j = "{\"apps\":[{\"id\":\"zz\",\"future\":{\"a\":[1,2,{\"b\":null}],\"c\":true},\"name\":\"Zed\",\"elf_size\":100,\"sha256\":\"" + up + "\"}]}";
    int n = parse(j, e, &sk);
    CHECK(n == 1); CHECK(!strcmp(e[0].sha, SHA_A)); CHECK(e[0].sym == '?');
  }
  // --- escape dasar ---
  {
    std::string j = "{\"apps\":[{\"id\":\"esc\",\"name\":\"A\\\"B\\\\C\",\"elf_size\":100,\"sha256\":\"" + std::string(SHA_A) + "\"}]}";
    int n = parse(j, e); CHECK(n == 1); CHECK(!strcmp(e[0].name, "A\"B\\C"));
  }
  // --- entri tidak sah ditolak, yang sah tetap lolos ---
  {
    std::string j = "{\"apps\":[" + app("ok1") + "," + app("../evil") + "," + app("ok2", ",\"elf\":\"../../x.elf\"") + "," + app("ok3") + "," + app("ok1") + "]}";
    int n = parse(j, e, &sk);
    CHECK(n == 2); CHECK(!strcmp(e[0].id, "ok1")); CHECK(!strcmp(e[1].id, "ok3")); CHECK(sk == 3);   // evil, elf berbahaya, id ganda
  }
  {   // ukuran: 0, > 1 MB, kurang dari 64 B
    std::string j = "{\"apps\":[{\"id\":\"s0\",\"name\":\"n\",\"elf_size\":0,\"sha256\":\"" + std::string(SHA_A) + "\"},"
                    "{\"id\":\"s1\",\"name\":\"n\",\"elf_size\":1048577,\"sha256\":\"" + std::string(SHA_A) + "\"},"
                    "{\"id\":\"s2\",\"name\":\"n\",\"elf_size\":1048576,\"sha256\":\"" + std::string(SHA_A) + "\"},"
                    "{\"id\":\"s3\",\"name\":\"n\",\"elf_size\":99999999999,\"sha256\":\"" + std::string(SHA_A) + "\"}]}";
    int n = parse(j, e, &sk); CHECK(n == 1); CHECK(!strcmp(e[0].id, "s2")); CHECK(sk == 3);
  }
  {   // tanpa nama / sha rusak
    std::string j = "{\"apps\":[{\"id\":\"nn\",\"elf_size\":100,\"sha256\":\"" + std::string(SHA_A) + "\"},{\"id\":\"ns\",\"name\":\"n\",\"elf_size\":100,\"sha256\":\"zz\"}]}";
    int n = parse(j, e, &sk); CHECK(n == 0); CHECK(sk == 2);
  }
  // --- JSON rusak / bukan indeks ---
  CHECK(parse("", e) == -1); CHECK(parse("[]", e) == -1); CHECK(parse("{}", e) == -1); CHECK(parse("{\"apps\":5}", e) == -1);
  CHECK(parse("{\"apps\":[", e) == -1); CHECK(parse("not json", e) == -1); CHECK(parse("{\"commit\":\"x\"}", e) == -1);
  CHECK(parse("{\"apps\":[]}", e) == 0);
  {   // terpotong di tengah app ke-2: app ke-1 tetap terbaca, sisanya dihitung gagal
    std::string full = "{\"apps\":[" + app("t1") + "," + app("t2") + "]}";
    std::string cut = full.substr(0, full.size() - 20);
    int n = parse(cut, e, &sk); CHECK(n == 1); CHECK(sk >= 1);
  }
  // --- batas jumlah ---
  {
    std::string j = "{\"apps\":[";
    for (int i = 0; i < 40; i++) { char id[16]; snprintf(id, sizeof(id), "a%d", i); if (i) j += ","; j += app(id); }
    j += "]}";
    int n = parse(j, e, &sk); CHECK(n == NYXSTORE_MAX_APPS); CHECK(sk == 16);
  }
  // --- fuzz: potong di setiap panjang + ubah satu byte; tidak boleh crash (ASan/UBSan yang menilai) ---
  {
    std::string j = "{\"commit\":\"c\",\"apps\":[" + app("f1", ",\"kind\":\"contoh\"") + "," + app("f2") + "]}";
    for (size_t k = 0; k <= j.size(); k++) { int s = 0; Entry t[NYXSTORE_MAX_APPS]; (void)nyxstore::parseIndex(j.c_str(), k, t, NYXSTORE_MAX_APPS, &s); }
    unsigned seed = 12345;
    for (int it = 0; it < 4000; it++) {
      std::string m = j; seed = seed * 1103515245u + 12345u;
      m[(seed >> 8) % m.size()] = (char)(seed >> 3);
      int s = 0; Entry t[NYXSTORE_MAX_APPS]; (void)nyxstore::parseIndex(m.c_str(), m.size(), t, NYXSTORE_MAX_APPS, &s);
    }
    CHECK(true);
  }
  // --- store.txt ---
  {
    char sha[65];
    std::string r = std::string("sha256=") + SHA_A + "\n";
    CHECK(parseRecord(r.c_str(), r.size(), sha)); CHECK(!strcmp(sha, SHA_A));
    std::string crlf = std::string("# x\r\nsha256=") + SHA_B + "\r\n";
    CHECK(parseRecord(crlf.c_str(), crlf.size(), sha)); CHECK(!strcmp(sha, SHA_B));
    CHECK(!parseRecord("sha256=short\n", 13, sha)); CHECK(sha[0] == 0);
    CHECK(!parseRecord("", 0, sha));
    std::string badhex = std::string("sha256=") + "g" + std::string(SHA_A + 1) + "\n";
    CHECK(!parseRecord(badhex.c_str(), badhex.size(), sha));
  }
  // --- status ---
  CHECK(stateFor(false, "", SHA_A) == ST_NOT_INSTALLED); CHECK(stateFor(false, SHA_A, SHA_A) == ST_NOT_INSTALLED);
  CHECK(stateFor(true, "", SHA_A) == ST_UNKNOWN); CHECK(stateFor(true, nullptr, SHA_A) == ST_UNKNOWN);
  CHECK(stateFor(true, SHA_A, SHA_A) == ST_UP_TO_DATE); CHECK(stateFor(true, SHA_B, SHA_A) == ST_UPDATE);
  // --- URL & ukuran ---
  { char u[64]; CHECK(joinUrl("https://x/y/", "a.elf", u, sizeof(u))); CHECK(!strcmp(u, "https://x/y/a.elf")); CHECK(!joinUrl("https://x/y/", "a.elf", u, 10)); }
  { char b[24];
    fmtSize(0, b, sizeof(b)); CHECK(!strcmp(b, "0 B")); fmtSize(1023, b, sizeof(b)); CHECK(!strcmp(b, "1023 B"));
    fmtSize(1536, b, sizeof(b)); CHECK(!strcmp(b, "1.5 KB")); fmtSize(5020, b, sizeof(b)); CHECK(!strcmp(b, "4.9 KB"));
    fmtSize(48 * 1024, b, sizeof(b)); CHECK(!strcmp(b, "48 KB")); fmtSize(1024 * 1024, b, sizeof(b)); CHECK(!strcmp(b, "1.00 MB")); }
  { uint8_t d[3] = {0x00, 0xAB, 0xFF}; char h[8]; toHex(d, 3, h); CHECK(!strcmp(h, "00abff")); }

  // --- indeks ASLI dari nyxapps_release.py (opsional) ---
  if (argc > 1) {
    FILE* f = fopen(argv[1], "rb"); CHECK(f != nullptr);
    if (f) {
      std::string t; char buf[4096]; size_t r; while ((r = fread(buf, 1, sizeof(buf), f)) > 0) t.append(buf, r); fclose(f);
      int n = parse(t, e, &sk); printf("indeks asli: %d app, %d ditolak\n", n, sk);
      CHECK(n >= 1); CHECK(sk == 0);
      for (int i = 0; i < n; i++) CHECK(shaOk(e[i].sha) && fileNameOk(e[i].elf) && fileNameOk(e[i].manifest));
    }
  }
  printf("%d cek, %d gagal\n", checks, fails);
  return fails ? 1 : 0;
}
