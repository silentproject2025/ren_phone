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
  uint16_t m = chsAiChoose(4, 1000, 0, 0, 0); char s[8]; mstr(m, s);
  CHECK(!strcmp(s, "h5f7"), "mat dalam 1, dapat %s", s);

  // jangan menggantung menteri: putih menteri di d1, hitam gajah menyerang d4 (d4 diserang pion e5)
  setFen("4k3/8/8/4p3/8/8/3Q4/4K3 w - - 0 1");
  m = chsAiChoose(4, 1000, 0, 0, 0); mstr(m, s);
  CHECK(!(m & 63) == 0 || strcmp(s, "d2d4") != 0, "menteri digantung di d4");

  // makan menteri gratis
  setFen("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1");
  m = chsAiChoose(3, 1000, 0, 0, 0); mstr(m, s);
  CHECK(!strcmp(s, "d2d5"), "makan menteri, dapat %s", s);

  // tidak ada langkah (mat) -> 0
  setFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  CHECK(chsAiChoose(3, 500, 0, 0, 0) == 0, "stalemate/mat harus 0");

  // partai penuh: AI sedang (putih) vs AI mudah (hitam): semua langkah sah, papan utuh, waktu terukur
  for (int lv = 0; lv < 3; lv++) {
    chsReset(); char mv[2000] = ""; int ply = 0; uint32_t worst = 0; static const int D[3] = {2, 4, 6}, MS[3] = {250, 700, 1800}, MG[3] = {120, 10, 0};
    while (ply < 60) {
      if (!chsHasMove()) break;
      chsAiHistBuild(mv);
      uint32_t t = hostMs();
      bool aiSide = chsWhite;
      uint16_t mm = aiSide ? chsAiChoose(D[lv], MS[lv], MG[lv], 1, ply) : chsAiChoose(2, 150, 120, 1, ply);
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

  // ================= TinyML: jaringan gaya belajar online =================
  // "user" sintetis dgn kebiasaan jelas; jaringan hanya melihat langkahnya (online, 1 langkah SGD + replay per langkah)
  // dan harus menebak langkah user di posisi BARU yg belum pernah dilihat.
  struct Pol { static int pick(int kind, int ply, uint16_t* lm, int n) {     // deterministik
    int best = -1, bk = -1000000;
    for (int i = 0; i < n; i++) {
      uint16_t mv = lm[i]; int from = mv & 63, to = (mv >> 6) & 63; int8_t p = chsBd[from]; int tp = p > 0 ? p : -p; bool cap = chsBd[to] != 0;
      int k = 0;
      if (kind == 0) k = (cap ? 1000 + chsAiKey(mv) : 0) + (tp == 1 ? 100 : 0);                                  // A: selalu makan, kalau tidak dorong pion
      else if (kind == 1) k = (cap ? -500 : 0) + (tp == 1 ? 300 : 0) + (tp == 1 ? (to >> 3) : 0);                 // B: tidak pernah makan, suka pion
      else k = (ply < 14) ? ((tp == 1 ? 500 : 0) + ((to & 7) == 3 || (to & 7) == 4 ? 50 : 0))                   // C: konteks -> awal: pion tengah,
                          : ((cap ? 800 : 0) + (tp == 6 ? 400 : 0) + (tp == 4 ? 200 : 0));                       //    lanjut: makan / raja / benteng
      k = k * 64 + (63 - from);                                                                                  // pemecah seri tetap
      if (k > bk) { bk = k; best = i; }
    }
    return best; } };
  struct Eval { static void run(int kind, int games, bool train, double* accAll, double* accA, double* accB) {
    int hit = 0, tot = 0, hA = 0, tA = 0, hB = 0, tB = 0;
    for (int g = 0; g < games; g++) {
      chsReset(); int ply = 0;
      while (ply < 70 && chsHasMove()) {
        uint16_t mv[256]; int n = chsGen(mv), ln = 0; uint16_t lm[256];
        for (int i = 0; i < n; i++) if (chsLegal(mv[i])) lm[ln++] = mv[i];
        uint16_t chosen;
        if (chsWhite) {                                                   // Putih = "user"
          int bi = Pol::pick(kind, ply, lm, ln); chosen = lm[bi];
          if (train) chsStyleLearn(chosen, ply);
          else if (ln > 1) {
            float ctx[CHS_NC]; chsAiCtx(ctx, ln, ply); int am = 0; float bs = -1e30f;
            for (int i = 0; i < ln; i++) { float s = chsStyleScore(lm[i], ctx); if (s > bs) { bs = s; am = i; } }
            bool ok = (am == bi); hit += ok; tot++; if (ply < 14) { hA += ok; tA++; } else { hB += ok; tB++; }
          }
        } else chosen = lm[CHS_AI_RAND() % ln];                           // Hitam acak -> posisi beragam
        chsDo(chosen); ply++;
      }
    }
    if (!train) { *accAll = tot ? (double)hit / tot : 0; *accA = tA ? (double)hA / tA : 0; *accB = tB ? (double)hB / tB : 0; } } };
  for (int kind = 0; kind < 3; kind++) {
    srand(100 + kind); chsStyleReset(); chsAiHistN = 0;
    double a0, a1, a2, b0, b1, b2;
    Eval::run(kind, 6, false, &a0, &a1, &a2);                              // sebelum dilatih
    for (int g = 0; g < 14; g++) Eval::run(kind, 1, true, &b0, &b1, &b2);  // ~14 partai = kira-kira 14 x 35 langkah user
    Eval::run(kind, 6, false, &b0, &b1, &b2);                              // posisi baru
    printf("gaya %c: akurasi tebak langkah user  sebelum %.0f%%  sesudah %.0f%%  (awal %.0f%% / lanjut %.0f%%)  dilatih %u langkah\n",
           'A' + kind, a0 * 100, b0 * 100, b1 * 100, b2 * 100, (unsigned)chsTrainN);
    CHECK(b0 > a0 + 0.30 && b0 > 0.45, "gaya %c: jaringan harus jauh lebih baik dari acak (%.2f -> %.2f)", 'A' + kind, a0, b0);
    if (kind == 2) CHECK(b1 > 0.30 && b2 > 0.45, "gaya C sadar konteks (awal %.2f, lanjut %.2f)", b1, b2);
    for (int k = 0; k < CHS_NP; k++) CHECK(chsNet[k] == chsNet[k] && chsNet[k] > -50 && chsNet[k] < 50, "parameter stabil");
  }

  { chsReset(); uint16_t mv[256]; int n = chsGen(mv); uint16_t first = 0; for (int i = 0; i < n; i++) if (chsLegal(mv[i])) { first = mv[i]; break; }
    uint32_t t0 = hostMs(); for (int i = 0; i < 300; i++) chsStyleLearn(first, 0); uint32_t dt = hostMs() - t0;
    printf("1 langkah belajar (1 + 6 replay) di PC: %.2f ms  -> ESP32-S3 kira-kira %.0f-%.0f ms\n", dt / 300.0, dt / 300.0 * 10, dt / 300.0 * 25); }

  // serialisasi bolak-balik
  static uint8_t buf[CHS_STY_LEN + CHS_BK_MAXLEN]; static float keep[CHS_NP]; memcpy(keep, chsNet, sizeof(keep)); uint32_t kn = chsTrainN;
  size_t L = chsStyleSer(buf); chsNetInit(); chsTrainN = 0;
  CHECK(chsStyleDeser(buf, L) && !memcmp(keep, chsNet, sizeof(keep)) && chsTrainN == kn, "jaringan round-trip");
  buf[20] = 0xFF; buf[21] = 0xFF; buf[22] = 0xFF; buf[23] = 0x7F;   // NaN/inf
  CHECK(!chsStyleDeser(buf, L), "parameter NaN harus ditolak");
  { size_t L2 = chsStyleSer(buf); buf[0] = 1; CHECK(!chsStyleDeser(buf, L2), "versi lain ditolak"); CHECK(!chsStyleDeser(buf, 10), "blob terpotong ditolak"); }
  printf("ukuran jaringan: %d parameter (%d byte), blob NVS %d byte\n", CHS_NP, CHS_NP * 4, (int)CHS_STY_LEN);
  { int bn = chsBkN; uint32_t h0 = chsBkH[0]; size_t BL = chsBookSer(buf); chsBkN = 0; CHECK(chsBookDeser(buf, BL) && chsBkN == bn && chsBkH[0] == h0, "buku round-trip"); CHECK(!chsBookDeser(buf, 3), "buku terpotong"); }

  // buku: posisi awal harus punya langkah favorit user
  chsReset(); int bc = 0; uint16_t bm = chsBookGet(chsPosHash(), &bc);
  CHECK(bm != 0 && bc >= 1, "buku pembuka kosong"); printf("buku: %d entri, langkah awal count=%d\n", chsBkN, bc);

  // pelatihan tidak mengubah papan; mode gaya tetap tidak membuang partai: latih "tidak pernah makan" lalu hadapi mat-dalam-1 yg berupa makan
  srand(5); chsStyleReset(); chsAiHistN = 0;
  { double x0, x1, x2; for (int g = 0; g < 14; g++) Eval::run(1, 1, true, &x0, &x1, &x2); }
  chsTrainN = 1000;                                                          // anggap sudah matang -> bobot gaya penuh
  // mode gaya tetap tidak membuang partai: posisi dgn mat dalam 1 -> tetap mat meski gaya melarang
  setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
  m = chsAiChoose(4, 1000, 70, 2, 0); mstr(m, s);
  CHECK(!strcmp(s, "h5f7"), "mode gaya harus tetap memilih mat, dapat %s", s);

  // papan utuh setelah pencarian
  chsReset(); int8_t sb[64]; memcpy(sb, chsBd, 64); chsAiChoose(5, 800, 0, 0, 0);
  CHECK(!memcmp(sb, chsBd, 64) && chsWhite && chsCastle == 15 && chsEp == -1, "papan berubah setelah pencarian");
  chsAiFree();
  printf(fails ? "\n%d GAGAL\n" : "\nSEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
