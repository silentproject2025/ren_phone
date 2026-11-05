// test_kamus.cpp -- uji logika inti kamus_core.h di komputer.
// Jalankan:  g++ -std=c++11 -O1 -I.. -o /tmp/test_kamus tests_kamus/test_kamus.cpp && /tmp/test_kamus   (dari folder repo: -I.)
#include <stdio.h>
#include <string>
#include <vector>
#include "kamus_core.h"
#include "kamus_data.h"

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ fails++; printf("GAGAL  %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static KmsCtx mk(int preferEn = 0) {
  KmsCtx c; c.user = {nullptr, 0}; c.sd = {nullptr, 0};
  c.id = {kmsWordsID, KMS_ID_N}; c.en = {kmsWordsEN, KMS_EN_N}; c.preferEn = preferEn; return c;
}
// bantu: koreksi kata terakhir dari teks
static std::string fix(const KmsCtx& c, const std::string& text) {
  char out[KMS_MAXW + 1];
  if (kmsAutoFix(c, text.c_str(), (int)text.size(), out)) {
    int wl = kmsTailWord(text.c_str(), (int)text.size());
    return text.substr(0, text.size() - wl) + out;
  }
  return text;   // tidak diubah
}
static std::vector<std::string> sug(const KmsCtx& c, const std::string& word) {
  KmsSugs s; kmsSuggest(c, word.c_str(), (int)word.size(), s);
  std::vector<std::string> v; for (int i = 0; i < s.n; i++) v.push_back(s.w[i]); return v;
}
static bool has(const std::vector<std::string>& v, const char* w) { for (auto& x : v) if (x == w) return true; return false; }

int main() {
  KmsCtx c = mk();
  printf("kamus: ID=%d EN=%d\n", KMS_ID_N, KMS_EN_N);

  // ---- jarak edit 1
  CHECK(kmsDist1("kamuu", 5, "kamu", 4), "sisip");
  CHECK(kmsDist1("kau", 3, "kamu", 4), "hapus");
  CHECK(kmsDist1("kamo", 4, "kamu", 4), "ganti");
  CHECK(kmsDist1("thier", 5, "their", 5), "tukar");
  CHECK(!kmsDist1("kamu", 4, "kamu", 4), "identik bukan jarak 1");
  CHECK(!kmsDist1("kamuuu", 6, "kamu", 4), "jarak 2");
  CHECK(!kmsDist1("abcd", 4, "badc", 4), "dua tukar = jarak 2");

  // ---- saran
  auto s1 = sug(c, "ter");
  CHECK(s1.size() == 3, "ter -> 3 saran, dapat %d", (int)s1.size());
  for (auto& w : s1) CHECK(w.compare(0, 3, "ter") == 0, "saran '%s' tidak berawalan ter", w.c_str());
  auto s2 = sug(c, "makan");
  CHECK(!s2.empty() && s2[0] == "makan", "kata benar tampil pertama");
  auto s3 = sug(c, "Mak");
  CHECK(!s3.empty() && s3[0][0] == 'M', "kapital awal dipertahankan: %s", s3.empty() ? "-" : s3[0].c_str());
  auto s4 = sug(c, "MAK");
  { bool allUp = !s4.empty(); for (char ch : (s4.empty() ? std::string() : s4[0])) if (ch < 'A' || ch > 'Z') allUp = false;
    CHECK(allUp, "KAPITAL semua: %s", s4.empty() ? "-" : s4[0].c_str()); }
  auto s5 = sug(c, "tehn");
  CHECK(has(s5, "then") || has(s5, "teh"), "typo tehn -> saran mirip");
  CHECK(sug(c, "").empty(), "kosong -> tanpa saran");
  CHECK(sug(c, "xqzv").empty(), "ngawur -> tanpa saran");

  // preferensi bahasa: awalan "th" -> Inggris bila preferEn
  KmsCtx ce = mk(1);
  auto se = sug(ce, "th");
  CHECK(!se.empty() && se[0] == "the", "preferEn: th -> the (dapat %s)", se.empty() ? "-" : se[0].c_str());

  // ---- autokoreksi: harus mengoreksi
  struct { const char* in; const char* out; } yes[] = {
    {"saya mau makan nasii", "saya mau makan nasi"},
    {"kamu yaang", "kamu yang"},
    {"apa kamuu", "apa kamu"},
    {"terimaa", "terima"},
    {"thier house", "thier house"},      // 'house' benar -> tidak berubah (cek kata terakhir saja)
    {"i love thier", "i love their"},
    {"it was beacuse", "it was because"},
    {"please recieve", "please receive"},
    {"Nasii", "Nasi"},                   // awal kalimat, kapital awal dipertahankan
    {"halo. Terimaa", "halo. Terima"},
  };
  for (auto& t : yes) { std::string r = fix(c, t.in); CHECK(r == t.out, "'%s' -> '%s' (harusnya '%s')", t.in, r.c_str(), t.out); }

  // ---- autokoreksi: TIDAK boleh menyentuh
  const char* no[] = {
    "makan", "nasi", "yang", "ke pasar yg", "gak", "udah", "wkwk", "hehe",     // benar / terlalu pendek
    "ketemu Budi", "ke Surabaya", "pakai NASA", "buka iPhone", "NASII",                  // nama / kapital disengaja
    "don't", "andi@gogle", "http://gogle", "www.gogle", "kode nasii2", "#nasii", "a/nasii", "x-nasii",
    "xqzvbn", "aaaaaaaa", "hehe", "wkwk", "hihihi", "yaaaa", "bangeeet", "wkwkwk",                                                      // tidak ada tetangga
    "", "   ", "123"
  };
  for (auto& s : no) { std::string r = fix(c, s); CHECK(r == s, "tidak boleh diubah: '%s' -> '%s'", s, r.c_str()); }

  // ---- kedekatan tuts: salah huruf hanya dikoreksi bila tuts bersebelahan
  CHECK(kmsAdjacent('u', 'i') && kmsAdjacent('a', 's') && kmsAdjacent('n', 'b') && kmsAdjacent('q', 'a') && kmsAdjacent('a', 'w'), "tetangga");
  CHECK(!kmsAdjacent('u', 'a') && !kmsAdjacent('q', 'p') && !kmsAdjacent('a', 'd') && !kmsAdjacent('q', 's'), "bukan tetangga");
  { // kamus TANPA "buku": ketikan "buku" tidak boleh dijadikan "buka" (u dan a berjauhan)
    static const char* tiny[] = {"buka", "baku", "bukit"}; KmsCtx t = c; t.id = {tiny, 3}; t.en = {nullptr, 0};
    CHECK(fix(t, "buku") == "buku", "buku tidak dikoreksi jadi buka");
    CHECK(fix(t, "bukk") == "buka" || fix(t, "bukk") == "bukk", "bukk: aman");
    CHECK(fix(t, "bukp") == "bukp", "p jauh dari a");
    CHECK(fix(t, "bukq") == "buka" , "q tetangga a -> buka (dapat %s)", fix(t, "bukq").c_str());
  }
  CHECK(fix(c, "makab") == "makan", "n->b tetangga: makab -> makan (dapat %s)", fix(c, "makab").c_str());
  CHECK(fix(c, "mauu ") == "mauu ", "diakhiri spasi bukan kata terakhir");

  // ---- audit: semua kata kamus >=4 huruf TIDAK boleh diubah
  int bad = 0;
  for (int i = 0; i < KMS_ID_N; i++) if (strlen(kmsWordsID[i]) >= 4 && fix(c, kmsWordsID[i]) != kmsWordsID[i]) { bad++; printf("  ID berubah: %s\n", kmsWordsID[i]); }
  for (int i = 0; i < KMS_EN_N; i++) if (strlen(kmsWordsEN[i]) >= 4 && fix(c, kmsWordsEN[i]) != kmsWordsEN[i]) { bad++; printf("  EN berubah: %s\n", kmsWordsEN[i]); }
  CHECK(bad == 0, "%d kata kamus berubah sendiri", bad);

  // ---- audit: hapus satu huruf di tengah / tukar dua huruf -> idealnya kembali ke kata asal
  int tot = 0, ok = 0, alt = 0, miss = 0;
  auto probe = [&](const char* w) {
    int L = (int)strlen(w); if (L < 6) return;
    std::string t = w; t.erase(L / 2, 1);                                  // hilang satu huruf
    std::string r = fix(c, t); tot++;
    if (r == w) ok++; else if (r != t) alt++; else miss++;
    std::string u = w; std::swap(u[L / 2], u[L / 2 + 1]);                   // tukar dua huruf
    if (u != w) { std::string r2 = fix(c, u); tot++; if (r2 == w) ok++; else if (r2 != u) alt++; else miss++; }
  };
  // salah ketik tuts tetangga di tengah kata
  int stot = 0, sok = 0;
  auto probeSub = [&](const char* w) {
    int L = (int)strlen(w); if (L < 5) return;
    int pos = L / 2; static const char* alpha = "abcdefghijklmnopqrstuvwxyz";
    for (const char* a = alpha; *a; a++) if (*a != w[pos] && kmsAdjacent(*a, w[pos])) {
      std::string t = w; t[pos] = *a;
      if (kmsKnown(c, t.c_str(), L)) continue;            // jadinya kata sah lain -> tidak dihitung
      stot++; if (fix(c, t) == w) sok++;
      break;                                               // satu tetangga per kata
    }
  };
  for (int i = 0; i < KMS_ID_N; i++) probeSub(kmsWordsID[i]);
  for (int i = 0; i < KMS_EN_N; i++) probeSub(kmsWordsEN[i]);
  printf("audit tuts-tetangga: %d uji -> %d kembali tepat\n", stot, sok);
  CHECK(sok * 100 / (stot ? stot : 1) >= 70, "akurasi tuts-tetangga rendah (%d/%d)", sok, stot);
  for (int i = 0; i < KMS_ID_N; i++) probe(kmsWordsID[i]);
  for (int i = 0; i < KMS_EN_N; i++) probe(kmsWordsEN[i]);
  printf("audit typo: %d uji -> %d tepat kembali, %d jadi kata lain yg mirip, %d tidak dikoreksi\n", tot, ok, alt, miss);
  CHECK(ok * 100 / (tot ? tot : 1) >= 75, "akurasi koreksi typo terlalu rendah (%d/%d)", ok, tot);

  // ---- kata pengguna menang dari kamus & mencegah koreksi
  const char* uw[] = {"nasii"}; KmsCtx cu = c; cu.user = {uw, 1};
  CHECK(fix(cu, "makan nasii") == "makan nasii", "kata pengguna tidak dikoreksi");
  auto su = sug(cu, "nas");
  CHECK(!su.empty() && su[0] == "nasii", "kata pengguna didahulukan: %s", su.empty() ? "-" : su[0].c_str());

  printf("%d pengecekan, %d gagal\n", checks, fails);
  return fails ? 1 : 0;
}
