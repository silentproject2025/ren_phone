// Percobaan kasar: apakah level Nyx (mode 3) menang lebih banyak materi dari pencarian murni (mode 0)
// melawan "bot serakah" yang kebiasaannya sudah dipelajari model gaya?   g++ ... && /tmp/bench_nyx
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

static int legalMoves(uint16_t* out) { uint16_t mv[256]; int n = chsGen(mv), L = 0; for (int i = 0; i < n; i++) if (chsLegal(mv[i])) out[L++] = mv[i]; return L; }
// bot serakah: 75% makan bidak terbesar yang tersedia, selain itu acak
static uint16_t botMove(const uint16_t* lg, int L) {
  int bi = -1, bv = 0;
  for (int i = 0; i < L; i++) { int to = (lg[i] >> 6) & 63; int8_t v = chsBd[to]; int a = v < 0 ? -v : v; if (a > bv) { bv = a; bi = i; } }
  if (bi >= 0 && rand() % 100 < 95) return lg[bi];
  return lg[rand() % L];
}
static int material(bool forWhite) { int s = 0; for (int sq = 0; sq < 64; sq++) { int8_t p = chsBd[sq]; if (!p) continue; int t = p > 0 ? p : -p; if (t == 6) continue; s += ((p > 0) == forWhite ? 1 : -1) * chsAiVal[t]; } return s; }

int main() {
  // pemanasan: bot vs bot, model gaya belajar dari kebiasaan bot
  srand(1); chsStyleReset(); chsAiHistN = 0;
  for (int g = 0; g < 8; g++) {
    chsReset();
    for (int ply = 0; ply < 80; ply++) {
      uint16_t lg[256]; int L = legalMoves(lg); if (!L) break;
      uint16_t m = botMove(lg, L); chsStyleLearn(m, ply); chsDo(m);
    }
  }
  printf("model gaya dilatih %u langkah, akurasi tebak %.0f%%\n", (unsigned)chsTrainN, chsAcc * 100);
  const int GAMES = 20, PLIES = 60; const int modes[2] = {0, 3}; const char* nm[2] = {"mode 0 (minimax murni)", "mode 3 (Nyx)"};
  double tot[2] = {0, 0}; int wins[2] = {0, 0}, mates[2] = {0, 0}, loses[2] = {0, 0};
  for (int v = 0; v < 2; v++) {
    for (int g = 0; g < GAMES; g++) {
      srand(5000 + g); chsReset(); chsAiHistN = 0; bool aiWhite = (g % 2 == 0); int ended = 0;
      for (int ply = 0; ply < PLIES; ply++) {
        uint16_t lg[256]; int L = legalMoves(lg);
        if (!L) { ended = chsInCheck() ? ((chsWhite == aiWhite) ? -1 : 1) : 0; break; }
        uint16_t m = (chsWhite == aiWhite) ? chsAiChoose(4, 150, modes[v] == 3 ? 80 : 0, modes[v], ply) : botMove(lg, L);
        if (!m) break;
        chsDo(m);
      }
      int d = material(aiWhite);
      tot[v] += d; if (ended == 1) mates[v]++; if (ended == -1) loses[v]++;
      if (d > 100 || ended == 1) wins[v]++;
    }
    printf("%-24s  rata-rata selisih materi %+7.1f   unggul >1 pion/mat: %2d/%d   dimat: %d\n", nm[v], tot[v] / GAMES, wins[v], GAMES, loses[v]);
  }
  chsAiFree();
  return 0;
}
