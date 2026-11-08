// Uji gutendex_core.h di komputer:  g++ -std=c++11 -Wall -Wextra -o t test_gutendex.cpp && ./t
#include "../gutendex_core.h"
#include <string>
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL baris %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char* PAGE =
"{\"count\":76304,\"next\":\"https://gutendex.com/books/?page=2\",\"previous\":null,\"results\":["
"{\"id\":1342,\"title\":\"Pride and Prejudice\",\"authors\":[{\"name\":\"Austen, Jane\",\"birth_year\":1775,\"death_year\":1817}],"
 "\"summaries\":[\"A \\\"long\\\" summary, with {braces} and [brackets] and \\u00e9 accents.\"],\"translators\":[],"
 "\"subjects\":[\"Courtship -- Fiction\",\"England -- Fiction\"],\"bookshelves\":[\"Best Books Ever Listings\"],"
 "\"languages\":[\"en\"],\"copyright\":false,\"media_type\":\"Text\","
 "\"formats\":{\"text/html\":\"https://www.gutenberg.org/ebooks/1342.html.images\","
 "\"application/epub+zip\":\"https://www.gutenberg.org/ebooks/1342.epub3.images\","
 "\"text/plain; charset=us-ascii\":\"https://www.gutenberg.org/ebooks/1342.txt.utf-8\","
 "\"text/plain; charset=utf-8\":\"http://www.gutenberg.org/cache/epub/1342/pg1342.txt\","
 "\"image/jpeg\":\"https://www.gutenberg.org/cache/epub/1342/pg1342.cover.medium.jpg\"},\"download_count\":84519},"
"{\"id\":2000,\"title\":\"Don Quijote\\r\\nTomo I\",\"authors\":[{\"name\":\"Cervantes Saavedra, Miguel de\"}],\"languages\":[\"es\",\"en\"],"
 "\"formats\":{\"application/epub+zip\":\"https://www.gutenberg.org/ebooks/2000.epub.images\"},\"download_count\":12000},"
"{\"id\":999,\"title\":\"Audio Only\",\"authors\":[],\"languages\":[\"en\"],\"formats\":{\"audio/mpeg\":\"https://x/y.mp3\"},\"download_count\":5},"
"{\"id\":77,\"title\":\"Caf\\u00e9 \\ud83d\\ude00 \\u5317\\u4eac\",\"authors\":[{\"name\":\"Doyle, Arthur Conan, Sir\"}],\"languages\":[\"fr\"],"
 "\"formats\":{\"text/plain\":\"https://www.gutenberg.org/ebooks/77.txt.utf-8\"},\"download_count\":3}"
"]}";

int main() {
  GutItem it[8]; int next = -1, total = -1;
  int n = gutParsePage(PAGE, (int)strlen(PAGE), it, 8, &next, &total);
  CHECK(n == 3);                       // buku "Audio Only" dilewati
  CHECK(next == 1 && total == 76304);
  CHECK(it[0].id == 1342 && it[0].dl == 84519);
  CHECK(!strcmp(it[0].title, "Pride and Prejudice"));
  CHECK(!strcmp(it[0].author, "Jane Austen"));          // dibalik
  CHECK(!strcmp(it[0].lang, "en"));
  CHECK(!strcmp(it[0].epub, "https://www.gutenberg.org/ebooks/1342.epub3.images"));
  CHECK(!strcmp(it[0].txt, "https://www.gutenberg.org/cache/epub/1342/pg1342.txt"));   // utf-8 menang + http->https
  CHECK(!strcmp(it[1].title, "Don Quijote  Tomo I"));   // \r\n -> spasi
  CHECK(!strcmp(it[1].author, "Miguel de Cervantes Saavedra"));
  CHECK(!strcmp(it[1].lang, "es") && it[1].txt[0] == 0);
  CHECK(it[2].id == 77);
  CHECK(!strcmp(it[2].title, "Caf\xC3\xA9 \xF0\x9F\x98\x80 \xE5\x8C\x97\xE4\xBA\xAC"));   // \u00e9, surrogat, CJK
  CHECK(!strcmp(it[2].author, "Doyle, Arthur Conan, Sir"));   // dua koma: tidak dibalik
  CHECK(!strcmp(it[2].txt, "https://www.gutenberg.org/ebooks/77.txt.utf-8"));

  // terpotong di tengah buku ke-2: hanya buku pertama yang utuh
  std::string cut(PAGE); cut.resize(cut.find("\"id\":2000") + 60);
  n = gutParsePage(cut.c_str(), (int)cut.size(), it, 8, &next, &total);
  CHECK(n == 1 && it[0].id == 1342);
  // halaman terakhir: next null
  const char* last = "{\"count\":1,\"next\":null,\"previous\":\"x\",\"results\":[]}";
  n = gutParsePage(last, (int)strlen(last), it, 8, &next, &total);
  CHECK(n == 0 && next == 0 && total == 1);
  // bukan JSON Gutendex
  const char* bad = "<html>502 Bad Gateway</html>";
  CHECK(gutParsePage(bad, (int)strlen(bad), it, 8, &next, &total) == -1);
  const char* err = "{\"detail\":\"Not found.\"}";
  CHECK(gutParsePage(err, (int)strlen(err), it, 8, &next, &total) == -1);
  // batas maxOut
  CHECK(gutParsePage(PAGE, (int)strlen(PAGE), it, 2, &next, &total) == 2);
  // judul sangat panjang: dipotong tanpa merusak UTF-8
  std::string big = "{\"results\":[{\"id\":5,\"title\":\"";
  for (int i = 0; i < 40; i++) big += "\\u00e9\\u00e9\\u00e9";          // 120 huruf 2-byte
  big += "\",\"authors\":[],\"formats\":{\"application/epub+zip\":\"https://a/b.epub\"}}]}";
  n = gutParsePage(big.c_str(), (int)big.size(), it, 8, &next, &total);
  CHECK(n == 1 && strlen(it[0].title) <= GUT_TITLE_LEN - 1 && strlen(it[0].title) % 2 == 0);
  for (size_t i = 0; i < strlen(it[0].title); i += 2) CHECK((uint8_t)it[0].title[i] == 0xC3);
  // URL lebih panjang dari buffer: format diabaikan (bukan dipotong jadi URL rusak)
  std::string lu = "{\"results\":[{\"id\":6,\"title\":\"T\",\"formats\":{\"application/epub+zip\":\"https://www.gutenberg.org/";
  lu += std::string(200, 'x'); lu += ".epub\",\"text/plain\":\"https://a/b.txt\"}}]}";
  n = gutParsePage(lu.c_str(), (int)lu.size(), it, 8, &next, &total);
  CHECK(n == 1 && it[0].epub[0] == 0 && !strcmp(it[0].txt, "https://a/b.txt"));

  // URL pencarian
  char u[300];
  gutBuildQuery(u, sizeof(u), "", "", 0, 1);
  CHECK(!strcmp(u, "https://gutendex.com/books/?copyright=false"));
  gutBuildQuery(u, sizeof(u), "war & peace", "en", 1, 3);
  CHECK(!strcmp(u, "https://gutendex.com/books/?copyright=false&search=war%20%26%20peace&languages=en&sort=descending&page=3"));
  gutBuildQuery(u, sizeof(u), "caf\xC3\xA9", "", 0, 2);
  CHECK(!strcmp(u, "https://gutendex.com/books/?copyright=false&search=caf%C3%A9&page=2"));
  char tiny[30]; gutBuildQuery(tiny, sizeof(tiny), "abcdefghijklmnop", "en", 1, 9);
  CHECK(strlen(tiny) < sizeof(tiny));

  // EPUB ringan
  char lg[120];
  CHECK(gutLightUrl("https://www.gutenberg.org/ebooks/1342.epub3.images", lg, sizeof(lg)) && !strcmp(lg, "https://www.gutenberg.org/ebooks/1342.epub.noimages"));
  CHECK(gutLightUrl("https://www.gutenberg.org/ebooks/84.epub.images", lg, sizeof(lg)) && !strcmp(lg, "https://www.gutenberg.org/ebooks/84.epub.noimages"));
  CHECK(!gutLightUrl("https://www.gutenberg.org/cache/epub/84/pg84-images.epub", lg, sizeof(lg)));
  CHECK(!gutLightUrl("https://www.gutenberg.org/ebooks/84.epub.noimages", lg, sizeof(lg)));

  // nama berkas
  char fn[96];
  gutParsePage(PAGE, (int)strlen(PAGE), it, 8, &next, &total);
  gutMakeFileName(it[0], "epub", fn, sizeof(fn)); CHECK(!strcmp(fn, "Pride and Prejudice - Jane Austen [PG1342].epub"));
  gutMakeFileName(it[2], "txt", fn, sizeof(fn));  CHECK(!strcmp(fn, "Caf - Doyle, Arthur Conan [PG77].txt"));
  GutItem x; memset(&x, 0, sizeof x); x.id = 9; strcpy(x.title, "\xE5\x8C\x97\xE4\xBA\xAC");
  gutMakeFileName(x, "epub", fn, sizeof(fn)); CHECK(!strcmp(fn, "PG9.epub"));
  strcpy(x.title, "A/B: C*D?\"E\""); strcpy(x.author, "Z");
  gutMakeFileName(x, "txt", fn, sizeof(fn)); CHECK(strpbrk(fn, "/\\:*?\"<>|") == nullptr);
  strcpy(x.title, "The Extraordinarily Long Title That Goes On And On Forever And Ever"); strcpy(x.author, "Someone With A Really Long Name Indeed");
  gutMakeFileName(x, "epub", fn, sizeof(fn)); CHECK(strlen(fn) < 90);

  char c[16];
  gutFmtCount(c, sizeof c, 950); CHECK(!strcmp(c, "950"));
  gutFmtCount(c, sizeof c, 84519); CHECK(!strcmp(c, "84.5rb"));
  gutFmtCount(c, sizeof c, 2500000); CHECK(!strcmp(c, "2.5jt"));

  printf(fails ? "\n%d UJI GAGAL\n" : "\nSEMUA UJI LULUS\n", fails);
  return fails ? 1 : 0;
}
