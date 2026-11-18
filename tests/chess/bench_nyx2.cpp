// Benchmark Nyx: mode AI (0 = minimax murni, 3 = Nyx) x lawan berkebiasaan (0 serakah 95%, 1 layak-berkebiasaan, 2 pemula hati-hati).
// bangun : cd tests_chess && ./extract.sh && cd .. && g++ -O2 -std=c++11 -w -Itests_chess -o /tmp/bench_nyx2 bench_nyx2.cpp -lm
// pakai  : /tmp/bench_nyx2 <mode 0|3> <bot 0|1|2> <jumlah partai> <ply> [seed offset]
// catatan: selisih antar varian kecil dibanding derau -> pakai >= 48 partai dan bandingkan jumlah skakmat, bukan selisih materi akhir.
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
static int capVal(uint16_t m) { int to = (m >> 6) & 63; int8_t v = chsBd[to]; return v < 0 ? -v : v; }
// bot 0: serakah 95% (makan bidak terbesar)
static uint16_t botGreedy(const uint16_t* lg, int L) {
  int bi = -1, bv = 0;
  for (int i = 0; i < L; i++) { int a = capVal(lg[i]); if (a > bv) { bv = a; bi = i; } }
  if (bi >= 0 && rand() % 100 < 95) return lg[bi];
  return lg[rand() % L];
}
// bot 1: "layak tapi berkebiasaan": cari 2-ply, kandidat dalam 150cp dari terbaik; 70% ambil makan terbesar di antara kandidat, selain itu acak
static uint16_t botDecent(const uint16_t* lg, int L, int ply) {
  if (L == 1) return lg[0];
  chsAiChoose(2, 30, 150, 0, ply);
  int cand[256], nc = 0; int best = -CHS_AI_INF;
  for (int i = 0; i < L; i++) if (chsAiFx[i] && chsAiFs[i] > best) best = chsAiFs[i];
  for (int i = 0; i < L; i++) if (chsAiFx[i] && chsAiFs[i] >= best - 150) cand[nc++] = i;
  if (nc == 0) return lg[rand() % L];
  if (rand() % 100 < 70) {
    int bi = -1, bv = 0;
    for (int k = 0; k < nc; k++) { int a = capVal(chsAiRm[cand[k]]); if (a > bv) { bv = a; bi = cand[k]; } }
    if (bi >= 0) return chsAiRm[bi];
  }
  return chsAiRm[cand[rand() % nc]];
}
// bot 2: pemula hati-hati tanpa kebiasaan: 1-ply, acak di antara yg dalam 100cp dari terbaik
static uint16_t botSafe(const uint16_t* lg, int L, int ply) {
  if (L == 1) return lg[0];
  return chsAiChoose(1, 10, 100, 1, ply);
}
static uint16_t botMove(int bot, const uint16_t* lg, int L, int ply) {
  return bot == 0 ? botGreedy(lg, L) : (bot == 1 ? botDecent(lg, L, ply) : botSafe(lg, L, ply));
}
static int material(bool forWhite) { int s = 0; for (int sq = 0; sq < 64; sq++) { int8_t p = chsBd[sq]; if (!p) continue; int t = p > 0 ? p : -p; if (t == 6) continue; s += ((p > 0) == forWhite ? 1 : -1) * chsAiVal[t]; } return s; }

int main(int argc, char** argv) {
  int SEEDOFF = argc > 5 ? atoi(argv[5]) : 0; int mode = argc > 1 ? atoi(argv[1]) : 3, bot = argc > 2 ? atoi(argv[2]) : 0, GAMES = argc > 3 ? atoi(argv[3]) : 16, PLIES = argc > 4 ? atoi(argv[4]) : 50;
  srand(1); chsStyleReset(); chsAiHistN = 0;
  for (int g = 0; g < 6; g++) {                                   // pemanasan: bot vs bot
    chsReset();
    for (int ply = 0; ply < 70; ply++) {
      uint16_t lg[256]; int L = legalMoves(lg); if (!L) break;
      uint16_t m = botMove(bot, lg, L, ply); chsStyleLearn(m, ply); chsDo(m);
    }
  }
  double tot = 0, tot30 = 0, matePly = 0; int wins = 0, loses = 0, mates = 0; uint32_t tmax = 0; double tsum = 0; int tn = 0;
  for (int g = 0; g < GAMES; g++) {
    srand(9000 + SEEDOFF + g * 7 + bot); chsReset(); chsAiHistN = 0; bool aiWhite = (g % 2 == 0); int ended = 0, d30 = 0, endPly = PLIES;
    for (int ply = 0; ply < PLIES; ply++) {
      if (ply == 30) d30 = material(aiWhite);
      uint16_t lg[256]; int L = legalMoves(lg);
      if (!L) { endPly = ply; ended = chsInCheck() ? ((chsWhite == aiWhite) ? -1 : 1) : 0; break; }
      uint16_t m;
      if (chsWhite == aiWhite) {
        uint32_t t0 = hostMs();
        m = chsAiChoose(4, 100, mode == 3 ? 80 : 0, mode, ply);
        uint32_t dt = hostMs() - t0; if (dt > tmax) tmax = dt; tsum += dt; tn++;
      } else { m = botMove(bot, lg, L, ply); chsStyleLearn(m, ply); }   // belajar online dari langkah lawan (seperti dari user)
      if (!m) break;
      chsDo(m);
    }
    int d = material(aiWhite); if (endPly < 30) d30 = d;
    tot += d; tot30 += d30; if (ended == 1) matePly += endPly; if (ended == 1) mates++; if (ended == -1) loses++;
    if (d > 300 || ended == 1) wins++; else if (d < -300 && ended != 1) loses += (ended == -1 ? 0 : 1);
  }
  printf("mode %d bot %d: materi@30 %+7.1f | akhir %+7.1f | menang %2d/%d kalah %d | mat %2d (rata ply %.1f) | %.0f ms/lgk maks %u",
         mode, bot, tot30 / GAMES, tot / GAMES, wins, GAMES, loses, mates, mates ? matePly / mates : 0.0, tsum / (tn ? tn : 1), (unsigned)tmax);
#ifdef CHS_MX_LEN
  printf(" | th=(%.2f,%.2f,%.2f) ig=%.2f gerbang=%.2f", chsMxTh[0], chsMxTh[1], chsMxTh[2], chsMxIg, chsMxGate());
#endif
  printf("\n");
  chsAiFree();
  return 0;
}
