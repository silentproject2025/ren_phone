// Uji adu MESIN vs GEMINI di PC (tanpa Arduino): helper FEN/daftar langkah/pembaca balasan + mesin status chsGemPoll()
// dengan "Gemini" tiruan. Jalankan: ./run_gem.sh
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string>

static uint32_t hostMs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
#define CHS_AI_NOW() hostMs()
#define CHS_AI_RAND() ((uint32_t)rand())
#define CHS_AI_ALLOC(n) malloc(n)
#include "engine_ai.inc"

// ---- tiruan lingkungan Arduino ----
struct String {
  std::string s;
  String() {}
  void reserve(size_t) {}
  String& operator=(const char* c) { s = c; return *this; }
  String& operator+=(const char* c) { s += c; return *this; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
};
static unsigned long fakeNow = 1000;
static unsigned long millis() { return fakeNow; }
static bool needRedraw = false;
#define WL_CONNECTED 3
struct { int st = WL_CONNECTED; int status() { return st; } } WiFi;

static char*    chsMoves = nullptr;
static uint16_t chsPly = 0;
static uint8_t  chsEnd = 0;
static char     chsRes[4] = "";
static uint8_t  chsRole = 1;
static uint8_t  chsAiLevel = 1;
static bool     chsAnimOn = false, chsPromoOn = false;
static bool     chsGem = true, chsGemForfeit = false;
#define CHS_MAX_PLY 280

static void chsUpdateEnd() { chsEnd = chsRes[0] ? 6 : chsStatus(); if (chsEnd == 0 && chsPly >= CHS_MAX_PLY) chsEnd = 5; }
static void chsCommitEx(uint16_t m, bool) {
  char t[8]; chsMoveStr(m, t);
  size_t L = strlen(chsMoves);
  if (L) chsMoves[L++] = ' ';
  strcpy(chsMoves + L, t);
  chsDo(m); chsPly++; chsUpdateEnd();
}
static uint16_t chsAiMoveFor(int lv) {
  static const uint8_t D[5] = {2, 4, 6, 4, 14}; (void)D;
  chsAiHistBuild(chsMoves);
  return chsAiChoose(3, 60, 10, 1, (int)chsPly);        // ringan: yg diuji adalah mesin status, bukan kekuatan
}
static int chsResultForMe() {                            // salinan dari chess_renphone.ino
  if (chsEnd == 0) return 2;
  if (chsEnd >= 2 && chsEnd <= 5) return 0;
  bool whiteWon = (chsEnd == 1) ? !chsWhite : (chsRes[1] == 'B');
  if (chsRole != 1 && chsRole != 2) return 2;
  return (whiteWon == (chsRole == 1)) ? 1 : -1;
}
static int saves = 0;
static void chsGemStatSave() { saves++; }

// "Gemini" tiruan: mode 0 acak sah | 1 sampah terus | 2 galat jaringan | 3 sah tapi dibungkus teks | 4 SAN saja
static int mockMode = 0;
static bool chsGemLaunch();
static uint32_t launches = 0;
#include "gem_block.inc"
static bool chsGemLaunch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  chsGemReqGen = chsGemGen; chsGemOk = false; chsGemReqMs = millis(); chsGemSt = 1; launches++;
  return true;
}
static void mockGeminiRun() {                            // yg dikerjakan task: isi chsGemReply/chsGemOk lalu st = 2
  if (chsGemSt != 1) return;
  char legal[1200]; int n = chsLegalList(legal, sizeof(legal));
  (void)n;
  uint16_t mv[256]; int g = chsGen(mv); uint16_t pick = 0; int c = 0;
  for (int i = 0; i < g; i++) if (chsLegal(mv[i]) && (rand() % (++c) == 0)) pick = mv[i];
  char t[8]; chsMoveStr(pick, t);
  std::string r;
  if (mockMode == 0) r = t;
  else if (mockMode == 1) r = "I think Nf3 is best, maybe castle later.";
  else if (mockMode == 2) { chsGemReply = "HTTP Error 429: quota"; chsGemOk = false; chsGemSt = 2; return; }
  else if (mockMode == 3) r = std::string("Sure! My move is **") + t + "**.\nGood luck";
  else r = "Nf3";
  chsGemReply = r.c_str(); chsGemOk = true; chsGemSt = 2;
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("GAGAL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void setFen(const char* fen) {
  memset(chsBd, 0, 64); int r = 7, f = 0; const char* p = fen;
  for (; *p && *p != ' '; p++) {
    if (*p == '/') { r--; f = 0; } else if (*p >= '1' && *p <= '8') f += *p - '0';
    else { const char* k = "pnbrqk"; char lc = *p | 32; int t = (int)(strchr(k, lc) - k) + 1; chsBd[r * 8 + f] = (*p >= 'a') ? -t : t; f++; }
  }
  p++; chsWhite = (*p == 'w'); p += 2;
  chsCastle = 0; for (; *p && *p != ' '; p++) { if (*p == 'K') chsCastle |= CHS_WK; if (*p == 'Q') chsCastle |= CHS_WQ; if (*p == 'k') chsCastle |= CHS_BK; if (*p == 'q') chsCastle |= CHS_BQ; }
  chsEp = -1; chsHalf = 0;
}
static void newGame() {
  chsReset(); chsMoves[0] = 0; chsPly = 0; chsRes[0] = 0; chsEnd = 0; chsGemRec = false; chsGemForfeit = false;
  chsGemBad = 0; chsGemMsg[0] = 0; chsGemPaused = false; chsGemQueued = false; chsGemSt = 0; chsGemGen++; chsGemDueMs = fakeNow;
}
// jalankan satu partai penuh; return jumlah tick. maxTicks = pengaman
static int playGame(int maxTicks) {
  int t = 0;
  while (t < maxTicks && !(chsEnd && chsGemRec)) {
    fakeNow += 100; t++;
    mockGeminiRun();
    chsGemPoll();
    if (chsGemPaused) break;
  }
  return t;
}

int main() {
  srand(11);
  chsMoves = (char*)malloc(1536); chsMoves[0] = 0;
  chsAiEnsure();

  // ---- FEN ----
  char fen[104];
  chsReset(); chsFen(fen, sizeof(fen), 0);
  CHECK(!strcmp(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), "FEN awal: %s", fen);
  uint16_t m; CHECK(chsParse("e2e4", 4, &m), "parse e2e4"); chsDo(m);
  chsFen(fen, sizeof(fen), 1);
  CHECK(!strcmp(fen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"), "FEN e4: %s", fen);
  CHECK(chsParse("c7c5", 4, &m), "parse c7c5"); chsDo(m);
  CHECK(chsParse("g1f3", 4, &m), "parse g1f3"); chsDo(m);
  chsFen(fen, sizeof(fen), 3);
  CHECK(!strcmp(fen, "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"), "FEN 3 ply: %s", fen);
  setFen("r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1");
  chsFen(fen, sizeof(fen), 40);
  CHECK(!strcmp(fen, "r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 21"), "FEN rokade sebagian: %s", fen);
  { char tiny[20]; CHECK(chsFen(tiny, sizeof(tiny), 0) == 0, "buffer kekecilan harus ditolak"); }

  // ---- daftar langkah sah ----
  chsReset();
  char legal[1200]; int n = chsLegalList(legal, sizeof(legal));
  CHECK(n == 20, "langkah awal = 20, dapat %d", n);
  CHECK(strstr(legal, "e2e4") && strstr(legal, "g1f3") && !strstr(legal, "e2e5"), "isi daftar: %s", legal);
  { char small[40]; int k = chsLegalList(small, sizeof(small)); CHECK(k > 0 && k < 20 && strlen(small) < 40, "daftar terpotong rapi (%d)", k); }
  setFen("8/P7/8/8/8/8/8/k6K w - - 0 1");
  n = chsLegalList(legal, sizeof(legal));
  CHECK(strstr(legal, "a7a8q") && strstr(legal, "a7a8n"), "promosi ada di daftar: %s", legal);

  // ---- pembaca balasan ----
  chsReset();
  struct { const char* in; const char* want; } T[] = {
    {"e2e4", "e2e4"}, {"  e2e4\n", "e2e4"}, {"**e2e4**", "e2e4"}, {"My move: e2e4.", "e2e4"}, {"E2E4", "e2e4"},
    {"I will play d2d4 now", "d2d4"}, {"e2e5 is bad but e2e4 is fine", "e2e4"}, {"g1f3", "g1f3"},
  };
  for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
    uint16_t mm = 0; bool ok = chsPickFromReply(T[i].in, &mm); char s[8] = ""; if (ok) chsMoveStr(mm, s);
    CHECK(ok && !strcmp(s, T[i].want), "balasan '%s' -> '%s' (harap %s)", T[i].in, s, T[i].want);
  }
  { uint16_t mm; CHECK(!chsPickFromReply("Nf3", &mm), "SAN tidak ditebak"); CHECK(!chsPickFromReply("", &mm), "kosong"); CHECK(!chsPickFromReply("e2e5", &mm), "tak sah"); CHECK(!chsPickFromReply("xe2e4y", &mm), "di tengah kata ditolak"); }
  setFen("8/P7/8/8/8/8/8/k6K w - - 0 1");
  { uint16_t mm; char s[8]; CHECK(chsPickFromReply("a7a8", &mm), "promosi tanpa huruf"); chsMoveStr(mm, s); CHECK(!strcmp(s, "a7a8q"), "otomatis vezir: %s", s);
    CHECK(chsPickFromReply("a7a8=N", &mm), "promosi =N"); chsMoveStr(mm, s); CHECK(!strcmp(s, "a7a8n"), "kuda: %s", s);
    CHECK(chsPickFromReply("a7a8r", &mm), "promosi benteng"); chsMoveStr(mm, s); CHECK(!strcmp(s, "a7a8r"), "benteng: %s", s); }
  chsReset();

  // ---- prompt ----
  newGame(); chsGemEngWhite = true; chsGemBad = 0;
  chsCommitEx(0, false) ; // dummy tidak dipakai
  newGame();
  chsGemBuildPrompt();
  CHECK(strstr(chsGemPrompt.c_str(), "as White") && strstr(chsGemPrompt.c_str(), "w KQkq - 0 1") && strstr(chsGemPrompt.c_str(), "e2e4") && strstr(chsGemPrompt.c_str(), "(game start)"), "prompt awal: %s", chsGemPrompt.c_str());
  CHECK(!strstr(chsGemPrompt.c_str(), "NOT in the legal"), "tanpa teguran di awal");
  chsGemBad = 1; strcpy(chsGemBadMv, "Nf3"); chsGemBuildPrompt();
  CHECK(strstr(chsGemPrompt.c_str(), "\"Nf3\" was NOT in the legal"), "teguran muncul");
  CHECK(chsGemPrompt.length() < 1700, "prompt awal ringkas (%zu)", chsGemPrompt.length());

  // ---- partai penuh: mesin vs Gemini acak ----
  memset(chsGemStat, 0, sizeof(chsGemStat));
  int played = 0;
  for (int g = 0; g < 6; g++) {
    newGame(); chsGemEngWhite = (g % 2 == 0); chsRole = chsGemEngWhite ? 1 : 2; mockMode = (g % 3 == 2) ? 3 : 0; chsAiLevel = 1;
    int before = saves;
    int ticks = playGame(40000);
    CHECK(chsEnd != 0 && chsGemRec, "partai %d harus selesai (tick %d, ply %u)", g, ticks, (unsigned)chsPly);
    CHECK(saves == before + 1, "skor disimpan tepat sekali (partai %d)", g);
    for (int k = 0; k < 20; k++) chsGemPoll();                      // poll setelah selesai tidak mencatat ulang
    CHECK(saves == before + 1, "tidak dicatat ganda");
    played++;
  }
  { int tot = 0; for (int j = 0; j < 4; j++) if (j != 3) tot += chsGemStat[1][j]; CHECK(tot == played, "jumlah skor %d == partai %d", tot, played);
    printf("skor (level Sedang, 6 partai vs Gemini acak): mesin %u | Gemini %u | seri %u\n", chsGemStat[1][0], chsGemStat[1][1], chsGemStat[1][2]);
    CHECK(chsGemStat[1][0] >= 4, "mesin harus menang telak lawan langkah acak (dapat %u)", chsGemStat[1][0]); }

  // ---- Gemini terus-menerus tak sah -> kalah setelah 3x ----
  newGame(); chsGemEngWhite = true; chsRole = 1; mockMode = 1; chsGemStat[2][0] = chsGemStat[2][3] = 0; chsAiLevel = 2;
  playGame(2000);
  CHECK(chsEnd == 6 && chsGemForfeit, "tiga kali tak sah -> kalah (end=%d forfeit=%d)", chsEnd, chsGemForfeit);
  CHECK(!strcmp(chsRes, "RB") && chsResultForMe() == 1, "hitam (Gemini) dianggap menyerah, mesin menang");
  CHECK(chsGemStat[2][0] == 1 && chsGemStat[2][3] == 1, "skor + hitungan pelanggaran: %u/%u", chsGemStat[2][0], chsGemStat[2][3]);
  CHECK(chsPly == 1, "mesin sempat main 1 langkah, lalu Gemini gagal 3x (ply %u)", (unsigned)chsPly);

  // ---- galat jaringan -> jeda; Lanjut mencoba lagi ----
  newGame(); chsGemEngWhite = false; chsRole = 2; mockMode = 2; chsAiLevel = 1;
  playGame(500);
  CHECK(chsGemPaused && !chsEnd, "galat jaringan harus menjeda");
  CHECK(strstr(chsGemMsg, "Gemini gagal: HTTP Error 429"), "pesan galat: %s", chsGemMsg);
  mockMode = 0; chsGemPaused = false; chsGemMsg[0] = 0; chsGemDueMs = fakeNow;
  uint32_t l0 = launches; for (int i = 0; i < 30; i++) { fakeNow += 100; mockGeminiRun(); chsGemPoll(); }
  CHECK(launches > l0 && chsPly >= 2, "setelah Lanjut, permainan jalan lagi (ply %u)", (unsigned)chsPly);

  // ---- balasan basi (partai diganti saat permintaan jalan) diabaikan ----
  newGame(); chsGemEngWhite = false; chsRole = 2; mockMode = 0; chsAiLevel = 1;
  for (int i = 0; i < 20 && chsGemSt != 1; i++) { fakeNow += 100; chsGemPoll(); }       // mesin (putih? tidak: mesin hitam) -> Gemini jalan dulu
  CHECK(chsGemSt == 1, "permintaan Gemini harus berjalan");
  { uint16_t plyB = chsPly; chsGemGen++; mockGeminiRun(); fakeNow += 100; chsGemPoll(); CHECK(chsPly == plyB, "balasan lama tidak boleh dimainkan"); CHECK(chsGemSt == 0, "status dikembalikan"); }

  // ---- WiFi putus saat giliran Gemini ----
  newGame(); chsGemEngWhite = false; chsRole = 2; WiFi.st = 0;
  for (int i = 0; i < 20; i++) { fakeNow += 100; chsGemPoll(); }
  CHECK(chsGemPaused && strstr(chsGemMsg, "WiFi"), "WiFi putus -> jeda dgn pesan (%s)", chsGemMsg);
  WiFi.st = WL_CONNECTED;

  printf(fails ? "\n%d GAGAL\n" : "\nSEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
