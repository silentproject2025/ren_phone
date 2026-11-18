// ren_uci.cpp -- bungkus mesin catur Ren Phone jadi engine UCI sederhana (tanpa Arduino).
// Build:  g++ -O2 -std=c++11 -o ren_uci ren_uci.cpp -lm
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

static char moves[8000] = "";
static int  ply = 0;
static int  gDepth = 8, gMargin = 0, gMode = 1;   // bisa diubah lewat: setoption name Depth/Margin/Mode value N

int main() {
  srand((unsigned)time(NULL));
  setvbuf(stdout, NULL, _IONBF, 0);
  char line[10000];
  chsReset();
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strcmp(line, "uci")) {
      printf("id name RenPhoneEngine\nid author ren\n");
      printf("option name Depth type spin default 8 min 1 max 14\n");
      printf("option name Margin type spin default 0 min 0 max 300\n");
      printf("option name Mode type spin default 1 min 0 max 3\n");
      printf("option name Ev2 type spin default 0 min 0 max 255\nuciok\n");
    } else if (!strcmp(line, "isready")) printf("readyok\n");
    else if (!strcmp(line, "ucinewgame")) { chsReset(); moves[0] = 0; ply = 0; }
    else if (!strncmp(line, "setoption", 9)) {
      int v = atoi(strstr(line, "value") ? strstr(line, "value") + 5 : "0");
      if (strstr(line, "Ev2")) chsEv2 = v; else if (strstr(line, "Depth")) gDepth = v; else if (strstr(line, "Margin")) gMargin = v; else if (strstr(line, "Mode")) gMode = v;
    } else if (!strncmp(line, "position", 8)) {
      const char* m = strstr(line, "moves");
      moves[0] = 0;
      if (m) { m += 5; while (*m == ' ') m++; strncpy(moves, m, sizeof(moves) - 1); moves[sizeof(moves) - 1] = 0; }
      ply = chsReplay(moves); if (ply < 0) { ply = 0; chsReset(); }
    } else if (!strncmp(line, "go", 2)) {
      uint32_t ms = 1000;
      const char* p;
      if ((p = strstr(line, "movetime"))) ms = (uint32_t)atoi(p + 8);
      else {
        int t = chsWhite ? 0 : 1; const char* k = t ? "btime" : "wtime";
        if ((p = strstr(line, k))) ms = (uint32_t)(atoi(p + 5) / 30); if (ms < 50) ms = 50;
      }
      chsAiHistBuild(moves);
      uint16_t m = chsAiChoose(gDepth, ms, gMargin, gMode, ply);
      char s[8]; if (m) chsMoveStr(m, s); else strcpy(s, "0000");
      printf("bestmove %s\n", s);
    } else if (!strcmp(line, "status")) {                       // wasit: 0 lanjut, 1 mat, 2 pat, 3 50 lgk, 4 materi, 7 ulangan 3 posisi
      int st = chsStatus(); if (st == 0 && chsRepThree(moves)) st = 7;
      printf("status %d\n", st);
    } else if (!strcmp(line, "legal")) {                        // daftar langkah sah posisi sekarang
      uint16_t mv[256]; int n = chsGen(mv); printf("legal");
      for (int i = 0; i < n; i++) if (chsLegal(mv[i])) { char t[8]; chsMoveStr(mv[i], t); printf(" %s", t); }
      printf("\n");
    } else if (!strcmp(line, "quit")) break;
  }
  return 0;
}
