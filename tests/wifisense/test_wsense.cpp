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

// ---- simulasi paket CSI LLTF 64 subcarrier ----
struct SubSim { float A[64], ph[64], sgn[64]; bool sens[64]; };
static SubSim ss; static float ssNoiseMul = 1.0f; static bool ssShort = false;
static int ssForeign = 0, ssClass = 0, ssGlitch = 0, ssPkt = 0;   // WSENSE_V2
static const uint8_t kOtherMac[6] = {0x02,0x11,0x22,0x33,0x44,0x55};
static float ssWalk[64];
static void ssInit() {
  std::uniform_real_distribution<float> u(0, 1);
  for (int k = 0; k < 64; k++) {
    bool nul = (k < 2) || (k >= 27 && k <= 37);
    ss.A[k] = nul ? 0.0f : 25.0f + 30.0f * u(rng);
    ss.ph[k] = 2.0f * (float)M_PI * u(rng); ss.sgn[k] = (rng() & 1) ? 1.0f : -1.0f;
    ss.sens[k] = (k % 3 == 0); ssWalk[k] = 0;
  }
}
// satu langkah 10 ms; paket tiap 50 ms. hz/depth = napas pada subcarrier peka; walk = sigma random-walk independen
static void stepSub(float hz, float depth, float walk) {
  static float t = 0; static int c = 0;
  g_ms += 10; t += 0.01f;
  if (c++ % 5 == 0) {
    int8_t buf[128]; memset(buf, 0, sizeof(buf));
    ssPkt++; float gain = 1.0f; const uint8_t* mac = g_bssid; unsigned mode = 1;   // WSENSE_V2
    if (ssForeign && ssPkt % 3 == 0) { mac = kOtherMac; gain = 3.0f; }
    if (ssClass && ssPkt % 4 == 1) { mode = 0; gain *= 1.3f; }
    if (ssGlitch && ssPkt % 200 == 7) gain *= 4.0f;
    for (int k = 0; k < 64; k++) {
      if (walk > 0) ssWalk[k] = 0.7f * ssWalk[k] + gauss(walk);
      float amp = ss.A[k] * (1.0f + gauss(0.005f * ssNoiseMul) + ssWalk[k]
                             + (ss.sens[k] && depth > 0 ? ss.sgn[k] * depth * sinf(2 * (float)M_PI * hz * t) : 0.0f));
      if (walk <= 0) ssWalk[k] = 0;
      amp *= gain;
      float re = amp * cosf(ss.ph[k]), im = amp * sinf(ss.ph[k]);
      int ri = (int)lrintf(re), ii = (int)lrintf(im);
      buf[2 * k] = (int8_t)max(-127, min(127, ii)); buf[2 * k + 1] = (int8_t)max(-127, min(127, ri));
    }
    wifi_csi_info_t info; memset(&info, 0, sizeof(info)); info.buf = buf; info.len = ssShort ? 64 : 128;
    info.rx_ctrl.sig_mode = mode; memcpy(info.mac, mac, 6);
    wsCsiCb(nullptr, &info);
  }
  wsenseLoopPoll();
}
static void runSub(float sec, float hz, float depth, float walk) { int n = (int)(sec * 100); for (int i = 0; i < n; i++) stepSub(hz, depth, walk); }
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
  { int moving = 0, still = 0, total = 0, lastMv = 0, lastTot = 0; static float t2 = 0;
    for (int i = 0; i < 30000; i++) { g_ms += 10; t2 += 0.01f;
      if (i % 5 == 0) { wsBinSum = wsBinSum + 40.0f * (1 + gauss(0.01f)); wsBinCnt = wsBinCnt + 1; wsPktTotal = wsPktTotal + 1; }
      wsenseLoopPoll(); if (i > 6000 && i % 100 == 0) { total++; if (wsState == WS_MOVING) moving++; if (wsState == WS_STILL) still++; }
      if (i >= 18000 && i % 100 == 0) { lastTot++; if (wsState == WS_MOVING) lastMv++; } }
    printf("  info: GERAK palsu %d/%d, DIAM palsu %d/%d  (perlu kalibrasi ulang kalau noise ruangan naik)\n", moving, total, still, total);
    CHECK(lastMv * 10 <= lastTot, "WSENSE_V2 baseline adaptif: GERAK palsu di 2 menit terakhir %d/%d (<=10%%), adaptasi x%.2f", lastMv, lastTot, wsAdaptMul); }

  printf("\nF. Gambar & sentuh tidak crash\n");
  LGFX_Sprite sp; drawWsense(sp); wsenseTouch(10, 214, false, true); wsenseTouch(100, 214, false, true);
  CHECK(true, "draw/touch jalan; run=%d", wsRun);
  wsenseExit(); CHECK(!wsRun, "exit menghentikan sensing");


  // ================= MODE MULTI-SUBCARRIER (simulasi LLTF 64 subcarrier, int8 seperti perangkat asli) =================
  printf("\n=== M. Multi-subcarrier vs rata-rata (A/B) ===\n");
  ssInit();
  wsenseEnter();
  wsMulti = true; wsSens = 2;
  runSub(45, 0.f, 0.f, 0.f);
  CHECK(wsUseSub && wsNValid >= 40, "mode multi aktif, subcarrier hidup=%d", wsNValid);
  CHECK(wsState == WS_EMPTY, "M1 ruang kosong: state=%s skor=%.4f", nm(wsState), wsScore);
  wsCal = true; wsCalT0 = millis(); wsCalSum = wsCalSq = 0; wsCalN = 0;
  runSub(16, 0.f, 0.f, 0.f);
  float baseM = wsBaseM; CHECK(!wsCal, "M2 kalibrasi multi: dasar=%.4f", baseM);
  // kalibrasi mode rata-rata juga (adil)
  { int tgy = 26 + 18 + 46 + 6 + 14; wsenseTouch(SCR_W - 20, tgy + 4, false, true); }
  CHECK(!wsMulti, "ketuk label mengganti ke mode rata-rata");
  runSub(10, 0.f, 0.f, 0.f);
  wsCal = true; wsCalT0 = millis(); wsCalSum = wsCalSq = 0; wsCalN = 0;
  runSub(16, 0.f, 0.f, 0.f);
  float baseS = wsBaseS; printf("  info: dasar rata-rata=%.4f\n", baseS);

  struct Res { const char* nm; float bpm, ratio; WsState st; };
  auto trial = [&](bool multi, float depth, float hz) {
    wsMulti = multi; wsBrRingN = 0; wsBr = 0; wsBrK = -1;
    runSub(100, hz, depth, 0.f);
    return Res{ multi ? "multi" : "rata", wsBr, wsBrRatio, wsState };
  };
  printf("\nM3. Napas 15/mnt, 3%% pada 1/3 subcarrier (arah acak), noise 0,5%%+kuantisasi int8, 100 dtk\n");
  Res a = trial(true, 0.03f, 0.25f), b2 = trial(false, 0.03f, 0.25f);
  printf("  hasil: multi -> state=%s bpm=%.1f rasio=%.2f | rata -> state=%s bpm=%.1f rasio=%.1f\n", nm(a.st), a.bpm, a.ratio, nm(b2.st), b2.bpm, b2.ratio);
  CHECK(fabsf(a.bpm - 15.0f) <= 2.0f, "multi mendeteksi napas 15/mnt (bpm=%.1f)", a.bpm);

  printf("\nM4. Napas lemah 1,2%% (batas)\n");
  a = trial(true, 0.012f, 0.30f); b2 = trial(false, 0.012f, 0.30f);
  printf("  hasil: multi -> bpm=%.1f rasio=%.2f | rata -> bpm=%.1f rasio=%.1f (target 18)\n", a.bpm, a.ratio, b2.bpm, b2.ratio);

  printf("\nM5. Gerak: random walk independen per subcarrier 4%%, 8 dtk\n");
  for (int mode = 1; mode >= 0; mode--) {
    wsMulti = mode; runSub(40, 0.f, 0.f, 0.f);
    runSub(8, 0.f, 0.f, 0.04f);
    printf("  %s: state=%s skor=%.3f ambang=%.3f -> %.1fx\n", mode ? "multi" : "rata ", nm(wsState), wsScore, wsBase * wsSensMul[wsSens], wsScore / (wsBase * wsSensMul[wsSens]));
    if (mode) CHECK(wsState == WS_MOVING || wsState == WS_HOLD, "multi mendeteksi gerak");
  }

  printf("\nM6. False positive: ruang kosong, noise 2x kalibrasi, 5 menit, per mode\n");
  for (int mode = 1; mode >= 0; mode--) {
    wsMulti = mode; ssNoiseMul = 2.0f;
    int mv = 0, tot = 0;
    for (int i = 0; i < 30000; i++) { stepSub(0.f, 0.f, 0.f); if (i > 6000 && i % 100 == 0) { tot++; if (wsState == WS_MOVING) mv++; } }
    ssNoiseMul = 1.0f;
    printf("  %s: GERAK palsu %d/%d\n", mode ? "multi" : "rata ", mv, tot);
  }

  printf("\nM7. Fallback: paket tanpa LLTF (len 64) -> mode rata-rata otomatis\n");
  wsMulti = true; ssShort = true; runSub(20, 0.f, 0.f, 0.f); ssShort = false;
  CHECK(!wsUseSub, "multi mati otomatis saat data per-subcarrier tidak ada (label=Rata)");
  LGFX_Sprite sp2; drawWsense(sp2); wsenseExit(); CHECK(wsSub == nullptr, "keluar app membebaskan ring PSRAM");

  // ================= WSENSE_V2 =================
  printf("\n=== N. WSENSE_V2 ===\n");
  printf("\nN1. Kalibrasi tersimpan: dimuat lagi saat app dibuka (BSSID sama), diabaikan untuk router lain\n");
  { float keepS = wsBaseS, keepM = wsBaseM; int keepSens = wsSens;
    wsBaseS = 0.5f; wsBaseM = 0.5f; wsSens = 4;
    wsenseEnter();
    CHECK(fabsf(wsBaseM - keepM) < 1e-6f && fabsf(wsBaseS - keepS) < 1e-6f && wsSens == keepSens, "dasar (S=%.4f M=%.4f) & sensitivitas (%d) pulih dari penyimpanan", wsBaseS, wsBaseM, wsSens);
    wsenseExit();
    g_bssid[5] ^= 0x55; wsBaseM = 0.5f;
    wsenseEnter();
    CHECK(fabsf(wsBaseM - 0.5f) < 1e-6f, "router lain (BSSID beda): kalibrasi tidak dipakai");
    wsenseExit(); g_bssid[5] ^= 0x55; wsBaseM = keepM; }

  wsenseEnter(); wsMulti = true; runSub(45, 0.f, 0.f, 0.f);
  CHECK(wsUseSub && wsState == WS_EMPTY, "N2 start: multi aktif, state=%s", nm(wsState));
  printf("\nN2. Paket dari pemancar lain (3x lebih kuat) + kelas PHY lain (skala 1,3x) bercampur 5 menit -> tidak jadi 'gerak'\n");
  { uint32_t d0 = wsDropTotal; int mv = 0, tot = 0; ssForeign = 1; ssClass = 1;
    for (int i = 0; i < 30000; i++) { stepSub(0.f, 0.f, 0.f); if (i > 3000 && i % 100 == 0) { tot++; if (wsState == WS_MOVING) mv++; } }
    ssForeign = 0; ssClass = 0;
    CHECK(wsDropTotal - d0 > 500 && mv * 20 <= tot, "paket dibuang=%u, GERAK palsu %d/%d", (unsigned)(wsDropTotal - d0), mv, tot); }
  printf("\nN3. Paket glitch (amplitudo 4x, 1 dari 200 paket) 5 menit\n");
  { int mv = 0, tot = 0; ssGlitch = 1;
    for (int i = 0; i < 30000; i++) { stepSub(0.f, 0.f, 0.f); if (i > 3000 && i % 100 == 0) { tot++; if (wsState == WS_MOVING) mv++; } }
    ssGlitch = 0;
    CHECK(mv * 20 <= tot, "GERAK palsu %d/%d (glitch disaring)", mv, tot); }
  wsenseExit();

  printf("\nN4. Interpolasi puncak: napas 16,47/mnt (0,2745 Hz) di antara dua bin spektrum (16,2 dan 16,8)\n");
  wsenseEnter(); wsMulti = false;
  run(70, [](float t){ return 40.0f * (1 + 0.015f * sinf(2 * (float)M_PI * 0.2745f * t) + gauss(0.003f)); });
  CHECK(fabsf(wsBr - 16.47f) <= 0.20f, "bpm=%.2f (target 16,47 +-0,20; tanpa interpolasi paling dekat 16,2 atau 16,8)", wsBr);
  wsenseExit();

  printf("\n%s (%d gagal)\n", fails ? "ADA YANG GAGAL" : "SEMUA LULUS", fails);
  return fails ? 1 : 0;
}
