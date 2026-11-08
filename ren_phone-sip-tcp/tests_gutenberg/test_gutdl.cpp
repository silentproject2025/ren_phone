// Uji gutdl_core.h di komputer:  g++ -std=c++11 -Wall -Wextra -o t test_gutdl.cpp && ./t
#include "../gutdl_core.h"
#include <string>
#include <vector>
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL baris %d: %s\n", __LINE__, #c); fails++; } } while (0)

// zip minimal valid: 1 berkas "a" (stored), dibuat tangan
static std::vector<uint8_t> makeZip() {
  std::vector<uint8_t> z;
  auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) z.push_back((uint8_t)(v >> (8 * i))); };
  auto p16 = [&](uint32_t v) { for (int i = 0; i < 2; i++) z.push_back((uint8_t)(v >> (8 * i))); };
  p32(0x04034B50); p16(20); p16(0); p16(0); p16(0); p16(0); p32(0xE8B7BE43); p32(1); p32(1); p16(1); p16(0); z.push_back('a'); z.push_back('x');
  uint32_t cdOff = (uint32_t)z.size();
  p32(0x02014B50); p16(20); p16(20); p16(0); p16(0); p16(0); p16(0); p32(0xE8B7BE43); p32(1); p32(1); p16(1); p16(0); p16(0); p16(0); p16(0); p32(0); p32(0); z.push_back('a');
  uint32_t cdSize = (uint32_t)z.size() - cdOff;
  p32(0x06054B50); p16(0); p16(0); p16(1); p16(1); p32(cdSize); p32(cdOff); p16(0);
  return z;
}

int main() {
  // --- CRC32 ---
  const char* s = "123456789";
  CHECK(gutCrc32(0, (const uint8_t*)s, 9) == 0xCBF43926u);
  uint32_t c = gutCrc32(0, (const uint8_t*)s, 4); c = gutCrc32(c, (const uint8_t*)s + 4, 5);
  CHECK(c == 0xCBF43926u);                                   // bisa dilanjutkan potongan demi potongan
  CHECK(gutCrc32(0, (const uint8_t*)"", 0) == 0);

  // --- URL langsung ---
  char u[128];
  CHECK(gutDirectUrl(1513, GUT_K_EPUB3_IMG, u, sizeof u) && !strcmp(u, "https://www.gutenberg.org/cache/epub/1513/pg1513-images-3.epub"));
  CHECK(gutDirectUrl(37683, GUT_K_EPUB_LITE, u, sizeof u) && !strcmp(u, "https://www.gutenberg.org/cache/epub/37683/pg37683.epub"));
  CHECK(gutDirectUrl(345, GUT_K_TXT, u, sizeof u) && !strcmp(u, "https://www.gutenberg.org/cache/epub/345/pg345.txt"));
  CHECK(!gutDirectUrl(0, GUT_K_TXT, u, sizeof u));
  CHECK(!gutDirectUrl(1513, GUT_K_EPUB3_IMG, u, 20));        // buffer terlalu kecil

  // --- tiers ---
  GutItem it; memset(&it, 0, sizeof it); it.id = 1513;
  strcpy(it.epub, "https://www.gutenberg.org/ebooks/1513.epub3.images");
  char t[3][GUT_URL_LEN + 8];
  CHECK(gutBuildTiers(it, true, true, t) == 0);
  CHECK(!strcmp(t[0], "https://www.gutenberg.org/cache/epub/1513/pg1513.epub"));
  CHECK(!strcmp(t[1], "https://www.gutenberg.org/cache/epub/1513/pg1513-images-3.epub"));
  CHECK(!strcmp(t[2], it.epub));
  CHECK(gutBuildTiers(it, true, false, t) == 1 && t[0][0] == 0);
  strcpy(it.epub, "https://www.gutenberg.org/cache/epub/1513/pg1513-images-3.epub");   // API sudah langsung: tier 2 kosong
  gutBuildTiers(it, true, false, t); CHECK(t[2][0] == 0);
  memset(&it, 0, sizeof it); it.id = 345; strcpy(it.txt, "https://www.gutenberg.org/ebooks/345.txt.utf-8");
  CHECK(gutBuildTiers(it, false, true, t) == 1 && !strcmp(t[1], "https://www.gutenberg.org/cache/epub/345/pg345.txt") && !strcmp(t[2], it.txt));

  // --- registry ---
  GutReg r[GUT_REG_N]; memset(r, 0, sizeof r);
  CHECK(gutRegBegin(r, 1513) == GUT_BEGIN_OK);
  CHECK(gutRegBegin(r, 1513) == GUT_BEGIN_SAME);             // ketuk dua kali: ditolak
  CHECK(gutRegBegin(r, 2701) == GUT_BEGIN_OTHER);            // satu pekerjaan sekaligus
  CHECK(gutRegActive(r, 1513) && !gutRegActive(r, 2701));
  gutRegEnd(r, 1513, GUT_S_FAIL);
  CHECK(!gutRegActive(r, 1513));
  CHECK(gutRegBegin(r, 1513) == GUT_BEGIN_OK);               // gagal -> boleh coba lagi
  gutRegEnd(r, 1513, GUT_S_DONE);
  CHECK(gutRegBegin(r, 2701) == GUT_BEGIN_OK);
  gutRegEnd(r, 2701, GUT_S_DONE);
  for (uint32_t i = 100; i < 140; i++) { CHECK(gutRegBegin(r, i) == GUT_BEGIN_OK); gutRegEnd(r, i, GUT_S_DONE); }   // penuh: menimpa, tak error
  CHECK(gutRegBegin(r, 0) == GUT_BEGIN_OTHER);

  // --- ZIP ---
  std::vector<uint8_t> z = makeZip();
  uint32_t co = 0, cs = 0;
  CHECK(gutZipHasPK(z.data(), (int)z.size()));
  CHECK(gutZipEocd(z.data(), (int)z.size(), (uint32_t)z.size(), &co, &cs));
  CHECK(gutZipCdSig(z.data() + co));
  // terpotong (berkas kurang di ujung)
  CHECK(!gutZipEocd(z.data(), (int)z.size() - 5, (uint32_t)z.size() - 5, &co, &cs));
  // kepala rusak (kasus di log: SD=[EB DE B3 52 ..]) -> kepala tidak PK, ujung masih utuh
  std::vector<uint8_t> bad = z; bad[0] = 0xEB; bad[1] = 0xDE; bad[2] = 0xB3; bad[3] = 0x52;
  CHECK(!gutZipHasPK(bad.data(), (int)bad.size()));
  // HTML dari server (bukan ZIP)
  const char* html = "<!DOCTYPE html><html><body>429 Too Many Requests</body></html>";
  CHECK(!gutZipHasPK((const uint8_t*)html, (int)strlen(html)));
  CHECK(!gutZipEocd((const uint8_t*)html, (int)strlen(html), (uint32_t)strlen(html), &co, &cs));
  // tanda EOCD palsu di tengah data tidak lolos (offset CD tak masuk akal)
  std::vector<uint8_t> fake(100, 0); fake[10] = 'P'; fake[11] = 'K'; fake[12] = 5; fake[13] = 6; fake[26] = 0xFF; fake[27] = 0xFF;
  CHECK(!gutZipEocd(fake.data(), 100, 100, &co, &cs));
  // hanya ekor 64 byte dibaca dari berkas besar
  std::vector<uint8_t> big(5000, 7); big.insert(big.begin(), z.begin(), z.end()); big.insert(big.end(), z.begin(), z.end());   // zip + sampah + zip
  { uint32_t fs = (uint32_t)big.size(); int tn = 200; CHECK(gutZipEocd(big.data() + fs - tn, tn, fs, &co, &cs)); }

  // --- CRC mendeteksi satu byte rusak di tengah (kasus yang tak tertangkap cek struktur) ---
  std::vector<uint8_t> data(300000); for (size_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i * 31 + 7);
  uint32_t good = gutCrc32(0, data.data(), data.size());
  data[123456] ^= 0x10;
  CHECK(gutCrc32(0, data.data(), data.size()) != good);

  // --- OPDS ---
  char q[200];
  gutBuildOpdsQuery(q, sizeof q, "", 0, 1);
  CHECK(!strcmp(q, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads"));
  gutBuildOpdsQuery(q, sizeof q, "pride & prejudice", 1, 3);
  CHECK(!strcmp(q, "https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date&query=pride%20%26%20prejudice&start_index=51"));

  static const char* XML =
    "<?xml version='1.0' encoding='utf-8'?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
    "<title>Search</title><id>x</id>\n"
    "<link rel=\"self\" href=\"/ebooks/search.opds/?query=austen\"/>\n"
    "<entry><title>Pride and Prejudice</title><content type=\"text\">Austen, Jane</content>\n"
    "<id>https://www.gutenberg.org/ebooks/1342.opds</id>\n"
    "<link type=\"image/jpeg\" rel=\"http://opds-spec.org/image/thumbnail\" href=\"/cache/epub/1342/pg1342.cover.small.jpg\"/>\n"
    "<link type=\"application/atom+xml\" href=\"/ebooks/1342.opds\"/></entry>\n"
    "<entry><title>Tom &amp; Jerry &#233;</title><content type=\"text\">Doe, John, Jr.</content><id>https://www.gutenberg.org/ebooks/22.opds</id></entry>\n"
    "<entry><title>Austen, Jane</title><link href=\"/ebooks/author/68\"/><id>https://www.gutenberg.org/ebooks/author/68.opds</id></entry>\n"
    "<entry><title>Next Page</title><link rel=\"next\" href=\"/ebooks/search.opds/?query=austen&amp;start_index=26\"/></entry>\n"
    "<entry><title>Terpotong</title><content type=\"text\">X</content><id>https://www.gutenberg.org/ebooks/9.op";
  GutItem o[8]; int hn = -1;
  int on = gutParseOpds(XML, (int)strlen(XML), o, 8, &hn);
  CHECK(on == 2);                                            // buku penulis, "Next Page", dan entri terpotong dilewati
  CHECK(hn == 1);
  CHECK(o[0].id == 1342 && !strcmp(o[0].title, "Pride and Prejudice") && !strcmp(o[0].author, "Jane Austen"));
  CHECK(!strcmp(o[0].epub, "https://www.gutenberg.org/cache/epub/1342/pg1342-images-3.epub"));
  CHECK(o[1].id == 22 && !strcmp(o[1].title, "Tom & Jerry \xC3\xA9"));
  CHECK(!strcmp(o[1].author, "Doe, John, Jr."));             // lebih dari satu koma: tidak dibalik
  CHECK(gutParseOpds("{\"count\":1}", 11, o, 8, &hn) == -1); // bukan XML
  CHECK(gutParseOpds(XML, (int)strlen(XML), o, 1, &hn) == 1);// batas maxOut

  printf(fails ? "\n%d UJI GAGAL\n" : "\nSEMUA UJI LULUS\n", fails);
  return fails ? 1 : 0;
}
