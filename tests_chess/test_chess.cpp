// Uji mesin catur + AI + model gaya di PC:  ./run.sh
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
static uint32_t hostMs() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
#define CHS_AI_NOW() hostMs()
#define CHS_AI_RAND() ((uint32_t)rand())
#define CHS_AI_ALLOC(n) malloc(n)
#include "engine_ai.inc"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("GAGAL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t perft(int d) {
  if (!d) return 1;
  uint16_t mv[256]; int n = chsGen(mv); uint64_t c = 0;
  for (int i = 0; i < n; i++) if (chsLegal(mv[i])) {
    int8_t sb[64]; memcpy(sb, chsBd, 64); bool sw = chsWhite; uint8_t sc = chsCastle; int8_t se = chsEp; uint16_t sh = chsHalf;
    chsDo(mv[i]); c += perft(d - 1);
    memcpy(chsBd, sb, 64); chsWhite = sw; chsCastle = sc; chsEp = se; chsHalf = sh;
  }
  return c;
}
static void setFen(const char* fen) {  // papan + giliran + rokade + ep
  memset(chsBd, 0, 64); int r = 7, f = 0; const char* p = fen;
  for (; *p && *p != ' '; p++) {
    if (*p == '/') { r--; f = 0; } else if (*p >= '1' && *p <= '8') f += *p - '0';
    else { const char* k = "pnbrqk"; char lc = *p | 32; int t = (int)(strchr(k, lc) - k) + 1; chsBd[r * 8 + f] = (*p >= 'a') ? -t : t; f++; }
  }
  p++; chsWhite = (*p == 'w'); p += 2;
  chsCastle = 0; for (; *p && *p != ' '; p++) { if (*p == 'K') chsCastle |= CHS_WK; if (*p == 'Q') chsCastle |= CHS_WQ; if (*p == 'k') chsCastle |= CHS_BK; if (*p == 'q') chsCastle |= CHS_BQ; }
  chsEp = -1; chsHalf = 0;
}
static void mstr(uint16_t m, char* o) { chsMoveStr(m, o); }

int main() {
  srand(7);
  chsReset();
  CHECK(perft(3) == 8902, "perft awal d3");
  setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  CHECK(perft(2) == 2039, "perft kiwipete d2");

  // mat dalam 1: Qh5xf7#  (skakmat gaya Scholar)
  setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
  uint16_t m = chsAiChoose(4, 1000, 0, 0); char s[8]; mstr(m, s);
  CHECK(!strcmp(s, "h5f7"), "mat dalam 1, dapat %s", s);

  // jangan menggantung menteri: putih menteri di d1, hitam gajah menyerang d4 (d4 diserang pion e5)
  setFen("4k3/8/8/4p3/8/8/3Q4/4K3 w - - 0 1");
  m = chsAiChoose(4, 1000, 0, 0); mstr(m, s);
  CHECK(!(m & 63) == 0 || strcmp(s, "d2d4") != 0, "menteri digantung di d4");

  // makan menteri gratis
  setFen("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1");
  m = chsAiChoose(3, 1000, 0, 0); mstr(m, s);
  CHECK(!strcmp(s, "d2d5"), "makan menteri, dapat %s", s);

  // tidak ada langkah (mat) -> 0
  setFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  CHECK(chsAiChoose(3, 500, 0, 0) == 0, "stalemate/mat harus 0");

  // partai penuh: AI sedang (putih) vs AI mudah (hitam): semua langkah sah, papan utuh, waktu terukur
  for (int lv = 0; lv < 3; lv++) {
    chsReset(); char mv[2000] = ""; int ply = 0; uint32_t worst = 0; static const int D[3] = {2, 4, 6}, MS[3] = {250, 700, 1800}, MG[3] = {120, 10, 0};
    while (ply < 60) {
      if (!chsHasMove()) break;
      chsAiHistBuild(mv);
      uint32_t t = hostMs();
      bool aiSide = chsWhite;
      uint16_t mm = aiSide ? chsAiChoose(D[lv], MS[lv], MG[lv], 1) : chsAiChoose(2, 150, 120, 1);
      uint32_t dt = hostMs() - t; if (aiSide && dt > worst) worst = dt;
      CHECK(mm != 0, "langkah 0 padahal ada langkah sah");
      uint16_t chk; mstr(mm, s);
      CHECK(chsParse(s, (int)strlen(s), &chk) && chk == mm, "langkah tak sah %s", s);
      if (ply) strcat(mv, " "); strcat(mv, s);
      chsDo(mm); ply++;
    }
    CHECK(chsReplay(mv) == ply, "replay mismatch");
    printf("level %d: %d langkah, terlama %u ms\n", lv, ply, worst);
    CHECK(worst < (uint32_t)MS[lv] * 2 + 300, "level %d terlalu lambat %u ms", lv, worst);
  }

  // ---- belajar gaya: user selalu memakan bila bisa & mendorong pion; bobot f[0] harus naik
  memset(chsW, 0, sizeof(chsW)); chsTrainN = 0; chsBkN = 0;
  for (int g = 0; g < 6; g++) {
    chsReset();
    for (int ply = 0; ply < 40 && chsHasMove(); ply++) {
      uint16_t mv[256]; int n = chsGen(mv), best = -1, bk = -1;
      for (int i = 0; i < n; i++) if (chsLegal(mv[i])) { int k = chsAiKey(mv[i]) * 4 + (int)(CHS_AI_RAND() % 5); if (k > bk) { bk = k; best = i; } }
      if (chsWhite) chsStyleLearn(mv[best]);   // hanya Putih = "user"
      chsDo(mv[best]);
    }
  }
  printf("dilatih %u langkah, w[makan]=%.2f w[pion]=%.2f\n", (unsigned)chsTrainN, chsW[0], chsW[3]);
  CHECK(chsTrainN > 50, "terlalu sedikit data latih");
  CHECK(chsW[0] > 0.5f, "bobot memakan tidak naik");
  for (int k = 0; k < CHS_NF; k++) CHECK(chsW[k] >= -4.0f && chsW[k] <= 4.0f, "bobot di luar batas");

  // serialisasi bolak-balik
  uint8_t buf[CHS_STY_LEN + CHS_BK_MAXLEN]; float keep[CHS_NF]; memcpy(keep, chsW, sizeof(keep)); uint32_t kn = chsTrainN;
  size_t L = chsStyleSer(buf); memset(chsW, 0, sizeof(chsW)); chsTrainN = 0;
  CHECK(chsStyleDeser(buf, L) && !memcmp(keep, chsW, sizeof(keep)) && chsTrainN == kn, "gaya round-trip");
  buf[10] = 0xFF; buf[11] = 0xFF; buf[12] = 0xFF; buf[13] = 0x7F;   // NaN/inf
  CHECK(!chsStyleDeser(buf, L), "bobot NaN harus ditolak");
  { int bn = chsBkN; uint32_t h0 = chsBkH[0]; size_t BL = chsBookSer(buf); chsBkN = 0; CHECK(chsBookDeser(buf, BL) && chsBkN == bn && chsBkH[0] == h0, "buku round-trip"); CHECK(!chsBookDeser(buf, 3), "buku terpotong"); }

  // buku: posisi awal harus punya langkah favorit user
  chsReset(); int bc = 0; uint16_t bm = chsBookGet(chsPosHash(), &bc);
  CHECK(bm != 0 && bc >= 1, "buku pembuka kosong"); printf("buku: %d entri, langkah awal count=%d\n", chsBkN, bc);

  // mode gaya tetap tidak membuang partai: posisi dgn mat dalam 1 -> tetap mat meski gaya melarang
  memset(chsW, 0, sizeof(chsW)); chsW[0] = -4; chsW[14] = -4;
  setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
  m = chsAiChoose(4, 1000, 70, 2); mstr(m, s);
  CHECK(!strcmp(s, "h5f7"), "mode gaya harus tetap memilih mat, dapat %s", s);

  // papan utuh setelah pencarian
  chsReset(); int8_t sb[64]; memcpy(sb, chsBd, 64); chsAiChoose(5, 800, 0, 0);
  CHECK(!memcmp(sb, chsBd, 64) && chsWhite && chsCastle == 15 && chsEp == -1, "papan berubah setelah pencarian");
  chsAiFree();
  printf(fails ? "\n%d GAGAL\n" : "\nSEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
