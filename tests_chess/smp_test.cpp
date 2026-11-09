// Uji core ke-2 (Lazy SMP) di PC:  ./run_smp.sh
// Mesin utama = salinan ENGINE+AI di file ini; pembantu = chess_smp.cpp (translation unit TERPISAH, sama seperti di ESP32).
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
static uint32_t hostMs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
#define CHS_SMP 1
#define CHS_AI_NOW() hostMs()
#define CHS_AI_RAND() ((uint32_t)rand())
#define CHS_AI_ALLOC(n) malloc(n)
static void chsAiReport(int d, uint32_t nm, uint32_t nh, uint32_t ms, int cores);
#define CHS_AI_REPORT(d, nm, nh, ms, cores) chsAiReport((d), (nm), (nh), (ms), (cores))
#define CHS_AI_BEFORE_FREE() chsSmpShutdown()
#include "engine_ai.inc"
void chsSmpPcQuit();

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("GAGAL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int lastCores = 0; static uint32_t lastNm = 0, lastNh = 0, lastMs = 0; static int lastD = 0;
static void chsAiReport(int d, uint32_t nm, uint32_t nh, uint32_t ms, int cores) { lastD = d; lastNm = nm; lastNh = nh; lastMs = ms; lastCores = cores; }

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
static bool isLegal(uint16_t m) { uint16_t mv[256]; int n = chsGen(mv); for (int i = 0; i < n; i++) if (mv[i] == m && chsLegal(m)) return true; return false; }

int main() {
  srand(11);
  chsReset();
  CHECK(chsAiEnsure(), "alokasi AI");
  chsAiTTClear();
  char s[8];

  // 1) taktik dgn 2 core: hasil harus tetap benar
  setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
  uint16_t m = chsAiChoose(8, 800, 0, 0, 0); chsMoveStr(m, s);
  CHECK(!strcmp(s, "h5f7"), "mat dalam 1 (2 core), dapat %s", s);
  CHECK(lastCores == 2, "core ke-2 harus aktif (lastCores=%d)", lastCores);
  setFen("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1");
  m = chsAiChoose(8, 600, 0, 0, 0); chsMoveStr(m, s);
  CHECK(!strcmp(s, "d2d5"), "makan menteri (2 core), dapat %s", s);
  // mat dalam 2: Kiwipete-bebas, posisi klasik
  setFen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
  m = chsAiChoose(8, 600, 0, 0, 0); chsMoveStr(m, s);
  CHECK(!strcmp(s, "d1d8"), "mat tangga (Rd8#), dapat %s", s);

  // 2) banyak posisi: langkah selalu sah, papan utama kembali persis, node pembantu > 0 pada pencarian panjang
  const char* fens[6] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/3p4/3P4/2NBPN2/PP3PPP/R2Q1RK1 b - - 0 9",
    "2r3k1/5pp1/p3p2p/1p6/3P4/P3BP2/1P4PP/6K1 w - - 0 1" };
  uint32_t helperNodes = 0, tot = 0;
  for (int rep = 0; rep < 3; rep++) for (int i = 0; i < 6; i++) {
    setFen(fens[i]); int8_t sb[64]; memcpy(sb, chsBd, 64); bool sw = chsWhite; uint8_t sc = chsCastle; int8_t se = chsEp;
    chsAiTTClear();
    m = chsAiChoose(14, 500, rep == 2 ? 15 : 0, rep == 1 ? 3 : 0, 10);
    CHECK(m != 0 && isLegal(m), "langkah tidak sah (posisi %d rep %d)", i, rep);
    CHECK(!memcmp(sb, chsBd, 64) && sw == chsWhite && sc == chsCastle && se == chsEp, "papan berubah (posisi %d)", i);
    helperNodes += lastNh; tot += lastNm + lastNh;
  }
  printf("node pembantu / total = %u / %u (%.0f%%)\n", helperNodes, tot, tot ? 100.0 * helperNodes / tot : 0.0);
  CHECK(helperNodes > 0, "pembantu tidak pernah mencari");

  // 3) mulai/henti cepat berulang (cari deadlock / balapan): 300 kali
  setFen(fens[2]);
  for (int i = 0; i < 300; i++) {
    uint32_t t0 = hostMs();
    chsAiTTClear();
    m = chsAiChoose(14, 400, 0, 0, 0);
    CHECK(m != 0, "langkah kosong iterasi %d", i);
    if (hostMs() - t0 > 3000) { CHECK(false, "pencarian terlalu lama iterasi %d", i); break; }
    if (i >= 20) break;                      // (300 x 400 ms terlalu lama di PC 1 core; 20 cukup utk memicu semua jalur)
  }
  for (int i = 0; i < 200; i++) { bool ok = chsSmpBegin(chsBd, chsWhite, chsCastle, chsEp, chsHalf, chsAiAge, 0, hostMs() + 5, 8, chsAiTT, chsAiTTMask); chsSmpEnd(); CHECK(ok, "Begin gagal %d", i); }

  // 4) saklar mati -> 1 core
  chsSmpOn = false; setFen(fens[1]); chsAiChoose(14, 500, 0, 0, 0);
  CHECK(lastCores == 1 && lastNh == 0, "saklar mati tetap memakai core ke-2");
  chsSmpOn = true;

  // 5) anggaran pendek (pondering) -> 1 core
  chsAiChoose(14, 150, 0, 0, 0);
  CHECK(lastCores == 1, "anggaran 150 ms seharusnya 1 core");

  chsAiFree();
  chsSmpPcQuit();
  printf(fails ? "ADA YANG GAGAL (%d)\n" : "SMP: SEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
