// Harness PC: mengompilasi blok WEBAUTH_V2 + halaman /passwd LANGSUNG dari phone.ino dgn mock Arduino/ESP32.
// Mode: "a" (boot pertama + penguncian, simpan state) | "b" (reboot: masih terkunci?) | "migrate" (sandi lama teks biasa)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

// ---------------- mock Arduino
struct String : std::string {
  String() {} String(const char* s) : std::string(s ? s : "") {} String(const std::string& s) : std::string(s) {}
  String(unsigned v) : std::string(std::to_string(v)) {} String(int v) : std::string(std::to_string(v)) {}
  const char* c_str() const { return std::string::c_str(); }
  unsigned length() const { return (unsigned)size(); }
  bool startsWith(const char* p) const { return rfind(p, 0) == 0; }
  String substring(unsigned a) const { return String(std::string::substr(a)); }
  String substring(unsigned a, unsigned b) const { return String(std::string::substr(a, b - a)); }
  void trim() { while (!empty() && isspace((unsigned char)back())) pop_back(); while (!empty() && isspace((unsigned char)front())) erase(0, 1); }
  String operator+(const String& o) const { return String(std::string(*this) + std::string(o)); }
  String operator+(const char* o) const { return String(std::string(*this) + o); }
  String& operator+=(const char* o) { append(o); return *this; }
  String& operator+=(const String& o) { append(o); return *this; }
  char operator[](unsigned i) const { return at(i); }
};
static String operator+(const char* a, const String& b) { return String(std::string(a) + std::string(b)); }
static uint32_t g_ms = 100000; static uint32_t millis() { return g_ms; }
static void yield() {}
struct { void println(const char* s) { printf("  [serial] %s\n", s); } void printf(const char* f, ...) { } } Serial;
static uint32_t esp_random() { return (uint32_t)rand(); }
static void esp_fill_random(void* p, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t*)p)[i] = (uint8_t)rand(); }
#define strlcpy mock_strlcpy   // glibc >= 2.38 punya strlcpy sendiri
static size_t mock_strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; } return l; }
static std::map<std::string, std::string> g_nvs;           // "ns/key" -> bytes
struct Preferences {
  std::string ns;
  bool begin(const char* n, bool = false) { ns = n; return true; }
  void end() {}
  size_t getBytes(const char* k, void* b, size_t n) { auto it = g_nvs.find(ns + "/" + k); if (it == g_nvs.end()) return 0; size_t c = std::min(n, it->second.size()); memcpy(b, it->second.data(), c); return it->second.size(); }
  size_t putBytes(const char* k, const void* b, size_t n) { g_nvs[ns + "/" + k] = std::string((const char*)b, n); return n; }
  String getString(const char* k, const char* d) { auto it = g_nvs.find(ns + "/" + k); return it == g_nvs.end() ? String(d) : String(it->second); }
  size_t putString(const char* k, const String& v) { g_nvs[ns + "/" + k] = v; return v.size(); }
  uint8_t getUChar(const char* k, uint8_t d) { auto it = g_nvs.find(ns + "/" + k); return it == g_nvs.end() ? d : (uint8_t)it->second[0]; }
  size_t putUChar(const char* k, uint8_t v) { g_nvs[ns + "/" + k] = std::string(1, (char)v); return 1; }
  bool remove(const char* k) { return g_nvs.erase(ns + "/" + k) > 0; }
};
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  size_t o = 0; uint32_t acc = 0; int bits = 0;
  for (size_t i = 0; i < slen; i++) { if (src[i] == '=') break; const char* p = strchr(B64, src[i]); if (!p || !src[i]) return -0x2C; acc = (acc << 6) | (uint32_t)(p - B64); bits += 6;
    if (bits >= 8) { bits -= 8; if (o >= dlen) return -0x2A; dst[o++] = (acc >> bits) & 0xFF; } }
  *olen = o; return 0; }
static std::string b64enc(const std::string& s) { std::string r; uint32_t v = 0; int b = -6; for (unsigned char c : s) { v = (v << 8) + c; b += 8; while (b >= 0) { r += B64[(v >> b) & 63]; b -= 6; } } if (b > -6) r += B64[((v << 8) >> (b + 8)) & 63]; while (r.size() % 4) r += '='; return r; }
static uint64_t g_mac = 0x112233445566ULL;
struct { uint64_t getEfuseMac() { return g_mac; } } ESP;
#define BASIC_AUTH 0
struct MockServer {
  std::map<std::string, std::string> hdr, args, out_hdr; int code = 0; std::string body; bool auth_req = false;
  bool hasHeader(const char* k) { return hdr.count(k) > 0; }
  String header(const char* k) { return String(hdr[k]); }
  String arg(const char* k) { return String(args[k]); }
  void sendHeader(const char* k, const String& v) { out_hdr[k] = v; }
  void send(int c, const char* t, const String& b) { code = c; body = b; }
  void requestAuthentication(int, const char*) { auth_req = true; code = 401; }
  void reset() { hdr.clear(); args.clear(); out_hdr.clear(); code = 0; body.clear(); auth_req = false; }
} webServer;
static void setAuth(const std::string& u, const std::string& p) { webServer.hdr["Authorization"] = "Basic " + b64enc(u + ":" + p); }

#define BK_YIELD() yield()
#include "../../firmware/phone/sec_core.h"

static bool webGuard(bool) { return true; }                    // /passwd: guard diuji terpisah (di sini diasumsikan lolos)

// ---------------- blok ASLI dari phone.ino
#include "extracted_blocks.inc"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void saveNvs(const char* f) { std::ofstream o(f, std::ios::binary); for (auto& kv : g_nvs) { uint32_t a = kv.first.size(), b = kv.second.size(); o.write((char*)&a, 4); o.write(kv.first.data(), a); o.write((char*)&b, 4); o.write(kv.second.data(), b); } }
static void loadNvs(const char* f) { std::ifstream i(f, std::ios::binary); g_nvs.clear(); uint32_t a, b; while (i.read((char*)&a, 4)) { std::string k(a, 0), v; i.read(&k[0], a); i.read((char*)&b, 4); v.assign(b, 0); i.read(&v[0], b); g_nvs[k] = v; } }
static bool tryLogin(const std::string& u, const std::string& p) { webServer.reset(); setAuth(u, p); webOkCacheOn = false; return webCheckCred(); }
static void wrong5() { for (int i = 0; i < 5; i++) tryLogin("admin", "salah-salah"); }

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "a";
  const char* kPw = "/tmp/webauth_pw.txt"; const char* kNvs = "/tmp/webauth_nvs.bin";
  srand(12345);
  if (mode == "a") {
    g_nvs.clear();
    std::string pw = webAuthPassword();
    CHECK(pw.size() == 8); CHECK(!strcmp(webAuthUser(), "admin"));
    CHECK(g_nvs.count("webauth/h") && g_nvs["webauth/h"].size() == 32 && g_nvs["webauth/s"].size() == 16);
    CHECK(!g_nvs.count("webauth/pw"));
    for (auto& kv : g_nvs) CHECK(kv.second.find(pw) == std::string::npos);        // sandi polos tak ada di NVS
    webServer.reset(); CHECK(!webCheckCred()); CHECK(webFailN == 0);              // tanpa header bukan percobaan salah
    CHECK(tryLogin("admin", pw)); CHECK(webCheckCred());                          // sukses + jalur cache
    CHECK(!tryLogin("admin", "salahsalah") && webFailN == 1);
    CHECK(!tryLogin("root", pw));                                                 // nama salah
    CHECK(tryLogin("admin", pw) && webFailN == 0);                                // sukses mereset hitungan
    { webServer.reset(); webServer.hdr["Authorization"] = "Basic !!!bukan-base64"; CHECK(!webCheckCred()); }
    { webServer.reset(); webServer.hdr["Authorization"] = "Bearer abc"; CHECK(!webCheckCred()); }
    webFailN = 0;

    // ganti sandi di layar (kebijakan)
    CHECK(webAuthSetPassword("kurang") != nullptr); CHECK(webAuthSetPassword("ada spasi1") != nullptr);
    CHECK(tryLogin("admin", pw));                                                 // gagal ganti -> sandi lama tetap
    CHECK(webAuthSetPassword("sandi:baru#9") == nullptr);                         // ':' & simbol boleh
    CHECK(std::string(webAuthPassword()) == "sandi:baru#9");
    CHECK(!tryLogin("admin", pw)); webFailN = 0; CHECK(tryLogin("admin", "sandi:baru#9"));
    webAuthResetRandom(); CHECK(strlen(webAuthPassword()) == 8 && std::string(webAuthPassword()) != "sandi:baru#9");
    pw = webAuthPassword(); CHECK(tryLogin("admin", pw));

    // penguncian bertingkat
    wrong5(); CHECK(webLocked() && webLockLvl == 1); CHECK(g_nvs["webauth/lvl"][0] == 1);
    CHECK(!tryLogin("admin", pw));                                                // sandi benar pun ditolak saat terkunci
    { webServer.reset(); setAuth("admin", pw); webOkCacheOn = false; CHECK(!webAuthOk()); CHECK(webServer.code == 429 && webServer.out_hdr["Retry-After"] != ""); }
    g_ms += 59000; CHECK(webLocked()); g_ms += 2000; CHECK(!webLocked());         // 60 dtk
    wrong5(); CHECK(webLocked() && webLockLvl == 2);                              // belum ada login sukses -> tingkat 2 = 300 dtk
    g_ms += 200000; CHECK(webLocked()); g_ms += 101000; CHECK(!webLocked());
    CHECK(tryLogin("admin", pw) && webLockLvl == 0 && g_nvs["webauth/lvl"][0] == 0);   // sukses mereset tingkat
    wrong5(); g_ms += 61000; webFailN = 0;

    // halaman /passwd
    webServer.reset(); webServer.args["old"] = "salah"; webServer.args["pw"] = "ganti-baru-99"; webServer.args["pw2"] = "ganti-baru-99";
    handleWebPasswdPost(); CHECK(webServer.code == 403); CHECK(webFailN == 1); CHECK(tryLogin("admin", pw));
    webServer.reset(); webServer.args["old"] = pw; webServer.args["pw"] = "ganti-baru-99"; webServer.args["pw2"] = "beda-semua-99";
    handleWebPasswdPost(); CHECK(webServer.code == 200 && webServer.body.find("tidak cocok") != std::string::npos); CHECK(tryLogin("admin", pw));
    webServer.reset(); webServer.args["old"] = pw; webServer.args["pw"] = "pendek"; webServer.args["pw2"] = "pendek";
    handleWebPasswdPost(); CHECK(webServer.body.find("Min. 8") != std::string::npos); CHECK(tryLogin("admin", pw));
    webServer.reset(); webServer.args["old"] = pw; webServer.args["user"] = "a:b";
    handleWebPasswdPost(); CHECK(webServer.body.find("Nama:") != std::string::npos); CHECK(!strcmp(webAuthUser(), "admin"));
    webServer.reset(); webServer.args["old"] = pw; webServer.args["user"] = "admin";
    handleWebPasswdPost(); CHECK(webServer.body.find("Tidak ada yang diubah") != std::string::npos);
    webServer.reset(); webServer.args["old"] = pw; webServer.args["user"] = "pemilik"; webServer.args["pw"] = "ganti-baru-99"; webServer.args["pw2"] = "ganti-baru-99";
    handleWebPasswdPost(); CHECK(webServer.code == 200 && webServer.body.find("Tersimpan") != std::string::npos);
    CHECK(!strcmp(webAuthUser(), "pemilik")); CHECK(!tryLogin("admin", "ganti-baru-99")); CHECK(!tryLogin("pemilik", pw)); webFailN = 0; CHECK(tryLogin("pemilik", "ganti-baru-99"));
    webServer.reset(); webServer.args["old"] = "ganti-baru-99"; webServer.args["user"] = "pemilik2";          // hanya nama
    handleWebPasswdPost(); CHECK(tryLogin("pemilik2", "ganti-baru-99"));
    // 5x sandi lama salah lewat /passwd -> terkunci
    for (int i = 0; i < 5; i++) { webServer.reset(); webServer.args["old"] = "salah"; handleWebPasswdPost(); }
    CHECK(webLocked() && webServer.code == 429);

    // simpan utk simulasi reboot (masih dikunci)
    saveNvs(kNvs); std::ofstream(kPw) << "ganti-baru-99";
  } else if (mode == "b") {
    loadNvs(kNvs); std::string pw; std::ifstream(kPw) >> pw;
    CHECK(!strcmp(webAuthUser(), "pemilik2"));                                     // memicu webCredLoad
    CHECK(webAuthPassword()[0] == 0);                                              // setelah reboot sandi polos tak diketahui
    CHECK(webLocked());                                                            // hukuman bertahan setelah reboot
    CHECK(!tryLogin("pemilik2", pw));
    g_ms += 400000; CHECK(!webLocked());
    CHECK(tryLogin("pemilik2", pw) && webLockLvl == 0);
    CHECK(g_nvs.count("webauth/h") && !g_nvs.count("webauth/pw"));
  } else if (mode == "migrate") {
    g_nvs.clear(); g_nvs["webauth/pw"] = "sandiLama77";
    CHECK(std::string(webAuthPassword()) == "sandiLama77");
    CHECK(!g_nvs.count("webauth/pw") && g_nvs.count("webauth/h"));
    CHECK(tryLogin("admin", "sandiLama77"));
    CHECK(!g_nvs["webauth/h"].empty()); for (auto& kv : g_nvs) CHECK(kv.second.find("sandiLama77") == std::string::npos);
  } else if (mode == "keys") {
    g_nvs.clear();
    const String k = "AIzaSyD-EXAMPLE_key_1234567890abcdefghij";
    String e = keyEncode(k); CHECK(e.startsWith("enc1:")); CHECK(e.find("AIza") == std::string::npos);
    CHECK(g_nvs["nyxsec/dk"].size() == 32);
    String d; CHECK(keyDecode(e, d) == 1 && d == k);
    String e2 = keyEncode(k); CHECK(e2 != e);                                   // IV acak -> tiap enkripsi beda
    CHECK(keyDecode(e2, d) == 1 && d == k);
    CHECK(keyEncode("YOUR_GEMINI_API_KEY_HERE") == "YOUR_GEMINI_API_KEY_HERE"); CHECK(keyEncode("DEMO_KEY") == "DEMO_KEY"); CHECK(keyEncode("") == "");
    CHECK(keyEncode(e) == e);                                                   // tidak dienkripsi dua kali
    String o = "x"; CHECK(keyDecode(k, o) == 0 && o == "x");                    // teks biasa -> 0
    std::string dk1 = g_nvs["nyxsec/dk"]; keyEncode("abc12345"); CHECK(g_nvs["nyxsec/dk"] == dk1);   // rahasia perangkat stabil
    g_mac ^= 1; CHECK(keyDecode(e, d) == -1);                                   // chip lain
    g_mac ^= 1; CHECK(keyDecode(e, d) == 1);
    CHECK(keyDecode("enc1:00ff", d) == -1);
  } else if (mode == "legacy-bad") {                                              // sandi lama tak memenuhi kebijakan -> acak baru
    g_nvs.clear(); g_nvs["webauth/pw"] = "pendek";
    CHECK(strlen(webAuthPassword()) == 8 && !g_nvs.count("webauth/pw"));
  }
  if (fails) printf("%d GAGAL (mode %s)\n", fails, mode.c_str()); else printf("ok (mode %s)\n", mode.c_str());
  return fails ? 1 : 0;
}
