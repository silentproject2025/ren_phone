#include "mock_env.h"
#include "../../firmware/phone/wifisense_renphone.ino"
#include <random>
static std::mt19937 rng(1);
static float gauss(float s) { return std::normal_distribution<float>(0, s)(rng); }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// jalankan `sec` detik; amp(t) memberi amplitudo rata-rata CSI; 20 paket/dtk, loop tiap 10 ms
template <class F> void run(float sec, F amp) {
  static float t = 0; int steps = (int)(sec * 100);
  for (int i = 0; i < steps; i++) {
    g_ms += 10; t += 0.01f;
    if (i % 5 == 0) { wsBinSum = wsBinSum + amp(t); wsBinCnt = wsBinCnt + 1; wsPktTotal = wsPktTotal + 1; }
    wsenseLoopPoll();
  }
}
static const char* nm(WsState s) { static const char* n[] = {"OFF","KOSONG","GERAK","DIAM","HOLD"}; return n[s]; }

int main() {
  wsenseEnter();
  printf("start: run=%d msg='%s'\n", wsRun, wsMsg);
  CHECK(wsRun, "app start saat WiFi tersambung");

  printf("\nA. Ruang kosong (noise 0.3%%) 45 dtk\n");
  run(45, [](float){ return 40.0f * (1 + gauss(0.003f)); });
  CHECK(wsState == WS_EMPTY, "state=%s (harus KOSONG), skor=%.4f", nm(wsState), wsScore);
  CHECK(wsBr == 0, "tidak ada napas palsu (br=%.1f ratio=%.1f)", wsBr, wsBrRatio);

  printf("\nKalibrasi 15 dtk di ruang kosong\n");
  wsCal = true; wsCalT0 = millis(); wsCalSum = wsCalSq = 0; wsCalN = 0;
  run(16, [](float){ return 40.0f * (1 + gauss(0.003f)); });
  CHECK(!wsCal, "kalibrasi selesai: dasar=%.4f msg='%s'", wsBase, wsMsg);

  printf("\nB. Napas 15/mnt (0.25 Hz, amplitudo 1.5%%) + noise 0.3%% selama 60 dtk\n");
  run(60, [](float t){ return 40.0f * (1 + 0.015f * sinf(2 * (float)M_PI * 0.25f * t) + gauss(0.003f)); });
  CHECK(wsState == WS_STILL, "state=%s (harus DIAM), skor=%.4f", nm(wsState), wsScore);
  CHECK(fabsf(wsBr - 15.0f) <= 2.0f, "bpm=%.1f (target 15 +-2), rasio puncak=%.1f", wsBr, wsBrRatio);

  printf("\nC. Napas 24/mnt (0.40 Hz)\n");
  run(60, [](float t){ return 40.0f * (1 + 0.015f * sinf(2 * (float)M_PI * 0.40f * t) + gauss(0.003f)); });
  CHECK(fabsf(wsBr - 24.0f) <= 2.5f, "bpm=%.1f (target 24 +-2.5)", wsBr);

  printf("\nD. Gerak besar 6 dtk (random walk ~6%%)\n");
  { float w = 0; run(6, [&](float){ w = 0.7f * w + gauss(0.06f); return 40.0f * (1 + w); }); }
  CHECK(wsState == WS_MOVING || wsState == WS_HOLD, "state=%s skor=%.3f (ambang=%.3f)", nm(wsState), wsScore, wsBase * wsSensMul[wsSens]);

  printf("\nE. Tenang lagi 60 dtk -> kembali KOSONG\n");
  run(60, [](float){ return 40.0f * (1 + gauss(0.003f)); });
  CHECK(wsState == WS_EMPTY, "state=%s br=%.1f", nm(wsState), wsBr);

  printf("\nG. Batas: napas lemah (0.5%% amplitudo, noise 0.3%%) 60 dtk\n");
  run(60, [](float t){ return 40.0f * (1 + 0.005f * sinf(2 * (float)M_PI * 0.30f * t) + gauss(0.003f)); });
  printf("  info: state=%s bpm=%.1f rasio=%.1f (target 18)\n", nm(wsState), wsBr, wsBrRatio);

  printf("\nH. False positive: ruang kosong, noise 1%% (3x lebih berisik dari kalibrasi), 5 menit\n");
  { int moving = 0, still = 0, total = 0; static float t2 = 0;
    for (int i = 0; i < 30000; i++) { g_ms += 10; t2 += 0.01f;
      if (i % 5 == 0) { wsBinSum = wsBinSum + 40.0f * (1 + gauss(0.01f)); wsBinCnt = wsBinCnt + 1; wsPktTotal = wsPktTotal + 1; }
      wsenseLoopPoll(); if (i > 6000 && i % 100 == 0) { total++; if (wsState == WS_MOVING) moving++; if (wsState == WS_STILL) still++; } }
    printf("  info: GERAK palsu %d/%d, DIAM palsu %d/%d  (perlu kalibrasi ulang kalau noise ruangan naik)\n", moving, total, still, total); }

  printf("\nF. Gambar & sentuh tidak crash\n");
  LGFX_Sprite sp; drawWsense(sp); wsenseTouch(10, 214, false, true); wsenseTouch(100, 214, false, true);
  CHECK(true, "draw/touch jalan; run=%d", wsRun);
  wsenseExit(); CHECK(!wsRun, "exit menghentikan sensing");

  printf("\n%s (%d gagal)\n", fails ? "ADA YANG GAGAL" : "SEMUA LULUS", fails);
  return fails ? 1 : 0;
}
