#pragma once
// =====================================================================
//  ryne_engine.h -- RYNE v2 (Ren Phone, ESP32-S3)
//
//  Header-only, C++ murni (tanpa Arduino) supaya bisa dites di komputer.
//
//  A. VIBE = Bayesian filter, bukan sekadar aturan/EMA
//     * 13 fitur: skip rate, skip burst, listen-through, dwell, volume (z-score
//       terhadap kebiasaanmu sendiri), tren volume, repeat-one, energi audio,
//       "kecerahan" audio (zero-crossing), dinamika, malam, laju interaksi.
//     * Softmax regression (bobot prior buatan tangan -- TIDAK dilatih, karena
//       tidak ada label vibe) -> likelihood 8 kelas.
//     * Digabung lewat filter HMM (transisi lengket): p_t ~ (A p_{t-1}) * q^g.
//       Hasil + histeresis + entropi sebagai confidence.
//     * Yang BELAJAR adalah baseline personal (running mean/var) dan bandit di B.
//
//  B. RYNE-CRS v2 = Contextual Thompson Sampling
//     * Konteks = probabilitas vibe  x  fitur audio lagu (energi/kecerahan/dinamika).
//     * Regresi linear Bayesian online (RLS + forgetting factor, diag-capped),
//       sampel theta ~ N(mu, v^2 A^-1) via Cholesky tiap keputusan.
//     * + Thompson sample Beta per lagu (suka/skip, meluruh),
//       - fatigue eksponensial per urutan putar, + bonus lagu baru,
//       + preferensi kontinuitas (vibe FOKUS/SANTAI mau mirip, BOSEN mau beda),
//       + epsilon kecil yang meluruh, exclude N lagu terakhir.
//
//  Reward implisit: dengar sampai habis (+), skip awal (-), skip tengah (campur),
//  tombol suka (+), volume naik saat lagu main (+), volume turun cepat (-).
// =====================================================================
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define RY_D       28
#define RY_NV      8
#define RY_NF      13
#define RY_MAXENT  256
#define RY_MAXPL   200

static const char* const RY_VIBE_NAME[RY_NV] = {
  "Semangat", "Sendu", "Bosen", "Nostalgik", "Fokus", "Gelisah", "Santai", "Excited"
};

struct RyEnt {
  uint32_t key;
  float    alpha, beta;       // posterior Beta (suka vs tidak)
  float    e, b, dyn;         // fitur audio rata2 (0..1): energi, kecerahan, dinamika
  uint32_t lastSeq;           // urutan putar terakhir
  uint16_t plays, fcnt;
  uint8_t  flags;             // bit0 = disukai
  uint8_t  pad;
};

struct RyHdr {
  uint32_t magic;
  uint16_t ver, D, entSz, nEnt;
  uint32_t nUpd, playSeq;
  float    base[7];
};

class RyneEngine {
 public:
  // ---- bandit ----
  float    Ainv[RY_D * RY_D];
  float    bv[RY_D];
  float    Lscr[RY_D * RY_D];     // scratch Cholesky
  uint32_t nUpd;
  // ---- lagu ----
  RyEnt    ent[RY_MAXENT];
  int      nEnt;
  int16_t  idx2ent[RY_MAXPL];
  int      nPl;
  uint32_t playSeq;
  int16_t  recent[8];
  int      nRecent;
  // ---- baseline personal ----
  float    volMean, volVar, enMean, enVar, brMean, brVar, listenEma;
  // ---- vibe ----
  float    pv[RY_NV], q[RY_NV], feat[RY_NF];
  int      vibeCur;
  uint32_t vibeSince;
  // ---- jangka pendek ----
  uint32_t skipT[16];  int nSkip;
  uint32_t interT[12]; int nInter;
  uint32_t volT[8];    int8_t volS[8]; int nVol;
  int      repeatMode; uint32_t repSince;
  int      curIdx; uint32_t trackStartMs; bool playingNow;
  float    pvStart[RY_NV];
  float    sumL, sumB, sumL2; uint32_t cntA;
  float    loudEma, brightEma, loudMean, loudVar;
  bool     volUpDuring, volDownEarly;
  uint32_t rng;

  RyneEngine() { reset(0x9E3779B9u); }

  void reset(uint32_t seed) {
    memset((void*)this, 0, sizeof(*this));
    rng = seed ? seed : 1;
    for (int i = 0; i < RY_D; i++) Ainv[i * RY_D + i] = 0.5f;
    volMean = 0.55f; volVar = 0.04f; enMean = 0.55f; enVar = 0.03f;
    brMean = 0.35f;  brVar = 0.02f;  listenEma = 0.6f;
    for (int k = 0; k < RY_NV; k++) { pv[k] = 1.f / RY_NV; pvStart[k] = 1.f / RY_NV; }
    vibeCur = 6;
    loudEma = 0.5f; brightEma = 0.35f; loudMean = 0.5f; loudVar = 0.01f;
    curIdx = -1;
  }
  void seed(uint32_t s) { rng = s ? s : 1; }

  // ---------------- RNG ----------------
  float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return ((rng >> 8) + 0.5f) / 16777216.f; }
  float gauss() { float u1 = rand01(), u2 = rand01(); return sqrtf(-2.f * logf(u1)) * cosf(6.2831853f * u2); }
  float gammaS(float k) {
    if (k < 1.f) { float u = rand01(); return gammaS(k + 1.f) * powf(u, 1.f / k); }
    float d = k - 1.f / 3.f, c = 1.f / sqrtf(9.f * d);
    for (int it = 0; it < 64; it++) {
      float x = gauss(), v = 1.f + c * x;
      if (v <= 0.f) continue;
      v = v * v * v;
      float u = rand01();
      if (u < 1.f - 0.0331f * x * x * x * x) return d * v;
      if (logf(u) < 0.5f * x * x + d * (1.f - v + logf(v))) return d * v;
    }
    return d;
  }
  float betaS(float a, float b) {
    if (a < 0.1f) a = 0.1f;
    if (b < 0.1f) b = 0.1f;
    float x = gammaS(a), y = gammaS(b);
    return x / (x + y + 1e-9f);
  }

  static float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

  // ---------------- daftar lagu ----------------
  int findEnt(uint32_t key) const { for (int i = 0; i < nEnt; i++) if (ent[i].key == key) return i; return -1; }
  int addEnt(uint32_t key, const uint32_t* keys, int n) {
    int slot;
    if (nEnt < RY_MAXENT) slot = nEnt++;
    else {
      slot = -1; uint32_t best = 0xFFFFFFFFu;
      for (int i = 0; i < nEnt; i++) {
        bool inPl = false;
        for (int j = 0; j < n; j++) if (keys[j] == ent[i].key) { inPl = true; break; }
        if (!inPl && ent[i].lastSeq < best) { best = ent[i].lastSeq; slot = i; }
      }
      if (slot < 0) slot = 0;
    }
    RyEnt& E = ent[slot]; memset(&E, 0, sizeof(E));
    E.key = key; E.alpha = 1.f; E.beta = 1.f; E.e = 0.5f; E.b = 0.5f; E.dyn = 0.5f;
    return slot;
  }
  void setPlaylist(int n, const uint32_t* keys) {
    if (n > RY_MAXPL) n = RY_MAXPL;
    if (n < 0) n = 0;
    nPl = n;
    for (int i = 0; i < n; i++) {
      int e = findEnt(keys[i]);
      if (e < 0) e = addEnt(keys[i], keys, n);
      idx2ent[i] = (int16_t)e;
    }
  }
  RyEnt* entOf(int idx) { return (idx >= 0 && idx < nPl) ? &ent[idx2ent[idx]] : nullptr; }
  bool liked(int idx) { RyEnt* E = entOf(idx); return E && (E->flags & 1); }

  // ---------------- konteks bandit ----------------
  void ctx(const float* p, const RyEnt& E, float* x) const {
    float e = (E.e - 0.5f) * 2.f, b = (E.b - 0.5f) * 2.f, d = (E.dyn - 0.5f) * 2.f;
    x[0] = 1.f; x[1] = e; x[2] = b; x[3] = d;
    for (int k = 0; k < RY_NV; k++) { x[4 + k] = p[k] * e; x[12 + k] = p[k] * b; x[20 + k] = p[k] * d; }
  }

  // RLS + forgetting; kalau forgetting bikin kovarians melebar > cap, update tanpa forgetting
  void rls(const float* x, float r, float lam) {
    float u[RY_D];
    for (int i = 0; i < RY_D; i++) { float s = 0; for (int j = 0; j < RY_D; j++) s += Ainv[i * RY_D + j] * x[j]; u[i] = s; }
    float xu = 0; for (int i = 0; i < RY_D; i++) xu += x[i] * u[i];
    const float cap = 1.2f;
    for (int pass = 0; pass < 2; pass++) {
      float inv = 1.f / (lam + xu);
      bool ok = true;
      for (int i = 0; i < RY_D; i++) {
        float nd = (Ainv[i * RY_D + i] - u[i] * u[i] * inv) / lam;
        if (nd > cap || !(nd == nd)) { ok = false; break; }
      }
      if (!ok && lam < 1.f) { lam = 1.f; continue; }
      for (int i = 0; i < RY_D; i++)
        for (int j = i; j < RY_D; j++) {
          float v = (Ainv[i * RY_D + j] - u[i] * u[j] * inv) / lam;
          Ainv[i * RY_D + j] = v; Ainv[j * RY_D + i] = v;
        }
      break;
    }
    for (int i = 0; i < RY_D; i++) bv[i] = lam * bv[i] + r * x[i];
    nUpd++;
  }

  // theta ~ N(mu, v^2 Ainv)
  void thetaSample(float v, float* th) {
    float mu[RY_D];
    for (int i = 0; i < RY_D; i++) { float s = 0; for (int j = 0; j < RY_D; j++) s += Ainv[i * RY_D + j] * bv[j]; mu[i] = s; }
    bool ok = true;
    for (int i = 0; i < RY_D && ok; i++)
      for (int j = 0; j <= i; j++) {
        float s = Ainv[i * RY_D + j] + (i == j ? 1e-6f : 0.f);
        for (int k = 0; k < j; k++) s -= Lscr[i * RY_D + k] * Lscr[j * RY_D + k];
        if (i == j) { if (s <= 1e-9f) { ok = false; break; } Lscr[i * RY_D + i] = sqrtf(s); }
        else Lscr[i * RY_D + j] = s / Lscr[j * RY_D + j];
      }
    float z[RY_D];
    for (int i = 0; i < RY_D; i++) z[i] = gauss();
    if (ok) {
      for (int i = 0; i < RY_D; i++) { float s = 0; for (int k = 0; k <= i; k++) s += Lscr[i * RY_D + k] * z[k]; th[i] = mu[i] + v * s; }
    } else {
      for (int i = 0; i < RY_D; i++) { float dv = Ainv[i * RY_D + i]; if (dv < 1e-6f) dv = 1e-6f; th[i] = mu[i] + v * sqrtf(dv) * z[i]; }
    }
  }

  // ---------------- util jangka pendek ----------------
  static int cntIn(const uint32_t* a, int n, uint32_t now, uint32_t win) { int c = 0; for (int i = 0; i < n; i++) if (now - a[i] <= win) c++; return c; }
  static void pushT(uint32_t* a, int& n, int cap, uint32_t t) { if (n < cap) a[n++] = t; else { memmove(a, a + 1, (cap - 1) * sizeof(uint32_t)); a[cap - 1] = t; } }

  // ---------------- event dari app ----------------
  void onInteraction(uint32_t now) { pushT(interT, nInter, 12, now); }
  void onVolume(int delta, uint32_t now) {
    if (delta == 0) return;
    if (nVol < 8) { volT[nVol] = now; volS[nVol] = delta > 0 ? 1 : -1; nVol++; }
    else { memmove(volT, volT + 1, 7 * sizeof(uint32_t)); memmove(volS, volS + 1, 7); volT[7] = now; volS[7] = delta > 0 ? 1 : -1; }
    if (delta > 0) volUpDuring = true;
    else if (curIdx >= 0 && now - trackStartMs < 8000) volDownEarly = true;
    onInteraction(now);
  }
  void onAudio(float loud, float bright, bool playing, uint32_t now) {
    (void)now;
    if (!playing) return;
    loud = clampf(loud, 0.f, 1.f); bright = clampf(bright, 0.f, 1.f);
    loudEma += 0.1f * (loud - loudEma);
    brightEma += 0.1f * (bright - brightEma);
    float d = loud - loudMean;
    loudMean += 0.004f * d;
    loudVar += 0.004f * (d * d - loudVar);
    sumL += loud; sumB += bright; sumL2 += loud * loud; cntA++;
  }

  void onTrackStart(int idx, uint32_t now) {
    curIdx = idx; trackStartMs = now;
    sumL = sumB = sumL2 = 0; cntA = 0;
    volUpDuring = volDownEarly = false;
    memcpy(pvStart, pv, sizeof(pv));
    playSeq++;
    RyEnt* E = entOf(idx);
    if (E) {
      E->lastSeq = playSeq; if (E->plays < 65535) E->plays++;
      int id = idx2ent[idx];
      if (nRecent < 8) recent[nRecent++] = (int16_t)id;
      else { memmove(recent, recent + 1, 7 * sizeof(int16_t)); recent[7] = (int16_t)id; }
    }
  }

  // how: 0 habis alami, 1 skip (next), 2 pilih manual/prev, 3 stop
  void onTrackEnd(int idx, uint32_t playedMs, uint32_t durMs, int how, uint32_t now) {
    RyEnt* E = entOf(idx);
    float f = durMs > 0 ? clampf((float)playedMs / (float)durMs, 0.f, 1.f)
                        : (how == 0 ? 1.f : clampf((float)playedMs / 200000.f, 0.f, 1.f));
    bool early = playedMs < 10000;

    // sinyal vibe dicatat walau lagunya tidak dikenal
    if (how == 1) pushT(skipT, nSkip, 16, now);
    if (how != 3) listenEma += 0.25f * (f - listenEma);
    if (!E) return;

    // fitur audio lagu (kalau cukup sampel ~2 dtk)
    if (idx == curIdx && cntA >= 50) {
      float eM = sumL / cntA, bM = sumB / cntA;
      float var = sumL2 / cntA - eM * eM; if (var < 0) var = 0;
      float dM = clampf(sqrtf(var) / 0.25f, 0.f, 1.f);
      if (E->fcnt == 0) { E->e = eM; E->b = bM; E->dyn = dM; }
      else { float w = 1.f / (E->fcnt + 1); E->e += (eM - E->e) * w; E->b += (bM - E->b) * w; E->dyn += (dM - E->dyn) * w; }
      if (E->fcnt < 6) E->fcnt++;
    }

    if (how == 3) return;

    float r;
    if (how == 0)      r = 0.6f;
    else if (how == 1) r = (f > 0.85f) ? 0.4f : (early ? -1.0f : -0.7f + 0.9f * f);
    else               r = (f > 0.85f) ? 0.4f : (early ? -0.4f : -0.1f + 0.4f * f);
    if (volUpDuring) r += 0.15f;
    if (volDownEarly) r -= 0.25f;
    if (E->flags & 1) r += 0.4f;
    r = clampf(r, -1.f, 1.f);

    float s = (r + 1.f) * 0.5f;
    E->alpha = 1.f + (E->alpha - 1.f) * 0.985f + s;
    E->beta  = 1.f + (E->beta  - 1.f) * 0.985f + (1.f - s);

    float x[RY_D]; ctx(pvStart, *E, x);
    rls(x, r, 0.997f);
  }

  void like(int idx, bool on, uint32_t now) {
    RyEnt* E = entOf(idx);
    if (!E) return;
    bool was = (E->flags & 1) != 0;
    if (on) E->flags |= 1; else E->flags &= ~1;
    if (on && !was) {
      E->alpha += 1.5f;
      float x[RY_D]; ctx(idx == curIdx ? pv : pvStart, *E, x);
      rls(x, 1.0f, 0.997f);
    } else if (!on && was) E->beta += 0.5f;
    onInteraction(now);
  }

  // ---------------- tick 2 Hz: fitur -> softmax -> filter HMM ----------------
  void tick(uint32_t now, int hour, int vol127, int rep, bool playing) {
    playingNow = playing;
    if (rep != repeatMode) { repeatMode = rep; repSince = now; }
    float vol = vol127 / 127.f;
    if (playing) {
      const float a = 0.0005f;
      float dv = vol - volMean; volMean += a * dv; volVar += a * (dv * dv - volVar);
      float de = loudEma - enMean; enMean += a * de; enVar += a * (de * de - enVar);
      float db = brightEma - brMean; brMean += a * db; brVar += a * (db * db - brVar);
    }
    int up = 0, dn = 0;
    for (int i = 0; i < nVol; i++) if (now - volT[i] <= 30000) { if (volS[i] > 0) up++; else dn++; }
    float volTrend = clampf((float)(up - dn) / 3.f, -1.f, 1.f);

    float* f = feat;
    f[0]  = clampf(cntIn(skipT, nSkip, now, 600000) / 6.f, 0.f, 1.f);
    f[1]  = clampf(cntIn(skipT, nSkip, now, 120000) / 3.f, 0.f, 1.f);
    f[2]  = (listenEma - 0.5f) * 2.f;
    f[3]  = (playing && curIdx >= 0) ? clampf((now - trackStartMs) / 180000.f, 0.f, 1.f) : 0.f;
    f[4]  = clampf((vol - volMean) / sqrtf(volVar + 1e-3f), -2.f, 2.f) * 0.5f;
    f[5]  = volTrend;
    f[6]  = (repeatMode == 2) ? clampf((now - repSince) / 120000.f, 0.f, 1.f) : 0.f;
    f[7]  = clampf((loudEma - enMean) / sqrtf(enVar + 1e-3f), -2.f, 2.f) * 0.5f;
    f[8]  = clampf((brightEma - brMean) / sqrtf(brVar + 1e-3f), -2.f, 2.f) * 0.5f;
    f[9]  = clampf(sqrtf(loudVar) / 0.25f, 0.f, 1.f);
    f[10] = hour < 0 ? 0.f : ((hour >= 22 || hour < 5) ? 1.f : ((hour >= 19 || hour < 7) ? 0.4f : 0.f));
    f[11] = clampf(cntIn(interT, nInter, now, 60000) / 6.f, 0.f, 1.f);
    f[12] = fabsf(volTrend);

    static const float W[RY_NV][RY_NF + 1] = {
      /* Semangat */ {-1.2f,-1.0f, 0.8f, 0.2f, 0.6f, 0.2f, 0.0f, 1.4f, 0.4f, 0.2f,-0.3f,-0.2f, 0.0f,-0.2f},
      /* Sendu    */ {-0.5f,-0.3f, 0.2f, 0.4f,-0.5f,-0.2f, 0.6f,-1.3f,-0.8f,-0.2f, 1.0f,-0.2f, 0.0f,-0.2f},
      /* Bosen    */ { 2.0f, 1.8f,-1.5f,-1.2f, 0.0f, 0.0f,-0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0.0f,-0.5f},
      /* Nostalgik*/ {-1.0f,-0.6f, 0.6f, 0.8f, 0.0f, 0.0f, 2.2f, 0.0f, 0.0f, 0.0f, 0.3f,-0.2f, 0.0f,-0.6f},
      /* Fokus    */ {-1.5f,-1.0f, 0.8f, 1.8f, 0.0f, 0.0f, 0.0f,-0.2f, 0.0f,-0.8f, 0.0f,-1.2f, 0.0f,-0.2f},
      /* Gelisah  */ { 1.0f, 0.6f,-0.4f,-0.5f, 0.0f, 0.0f,-0.2f, 0.0f, 0.0f, 0.2f, 0.0f, 1.2f, 1.6f,-0.7f},
      /* Santai   */ {-0.6f,-0.4f, 0.4f, 0.6f,-0.3f, 0.0f, 0.0f,-0.5f, 0.0f,-0.2f, 0.0f,-1.0f, 0.0f, 0.1f},
      /* Excited  */ {-0.4f,-0.3f, 0.2f, 0.0f, 0.9f, 1.4f,-0.2f, 1.0f, 0.6f, 0.3f,-0.2f, 0.4f, 0.0f,-0.5f},
    };
    float z[RY_NV], zmax = -1e9f;
    for (int k = 0; k < RY_NV; k++) {
      float s = W[k][RY_NF];
      for (int j = 0; j < RY_NF; j++) s += W[k][j] * f[j];
      z[k] = s; if (s > zmax) zmax = s;
    }
    float sum = 0;
    for (int k = 0; k < RY_NV; k++) { q[k] = expf(z[k] - zmax); sum += q[k]; }
    for (int k = 0; k < RY_NV; k++) q[k] /= sum;

    // filter HMM: prediksi lengket x likelihood tertemper
    const float stay = 0.93f, gam = 0.6f;
    float post[RY_NV], ps = 0;
    for (int k = 0; k < RY_NV; k++) {
      float pred = stay * pv[k] + (1.f - stay) / RY_NV;
      post[k] = pred * powf(q[k] + 1e-6f, gam);
      ps += post[k];
    }
    for (int k = 0; k < RY_NV; k++) pv[k] = post[k] / ps;

    int top = 0;
    for (int k = 1; k < RY_NV; k++) if (pv[k] > pv[top]) top = k;
    if (top != vibeCur && pv[top] > pv[vibeCur] + 0.10f && now - vibeSince > 8000) { vibeCur = top; vibeSince = now; }
  }

  int   vibeTop() const { return vibeCur; }
  float vibeConf() const {          // 1 - entropi ternormalisasi
    float h = 0;
    for (int k = 0; k < RY_NV; k++) if (pv[k] > 1e-6f) h -= pv[k] * logf(pv[k]);
    return clampf(1.f - h / logf((float)RY_NV), 0.f, 1.f);
  }

  // ---------------- pilih lagu berikutnya ----------------
  int selectNext(int cur, int n, uint32_t now) {
    (void)now;
    if (n <= 0 || nPl <= 0) return -1;
    if (n > nPl) n = nPl;
    if (n == 1) return 0;

    int R = n / 3; if (R > 5) R = 5; if (R < 1) R = 1;
    bool ok[RY_MAXPL];
    int  cand = 0;
    for (int i = 0; i < n; i++) {
      bool excl = (i == cur);
      for (int r = 0; r < R && r < nRecent && !excl; r++) if (recent[nRecent - 1 - r] == idx2ent[i]) excl = true;
      ok[i] = !excl; if (ok[i]) cand++;
    }
    if (cand == 0) { for (int i = 0; i < n; i++) ok[i] = (i != cur); }

    float eps = 0.04f + 0.12f * expf(-(float)nUpd / 150.f);
    if (rand01() < eps) {
      int pick = (int)(rand01() * n); if (pick >= n) pick = n - 1;
      for (int t = 0; t < n; t++) { int c = (pick + t) % n; if (ok[c]) return c; }
    }

    float v = 0.15f + 0.35f * expf(-(float)nUpd / 150.f);
    float th[RY_D]; thetaSample(v, th);

    static const float sk[RY_NV] = {0.3f, 0.6f, -0.9f, 0.2f, 0.9f, -0.4f, 0.6f, 0.1f};
    float pref = 0; for (int k = 0; k < RY_NV; k++) pref += pv[k] * sk[k];
    RyEnt* C = entOf(cur);
    float tau = n * 0.5f; if (tau < 5.f) tau = 5.f;

    int best = -1; float bestU = -1e9f;
    for (int i = 0; i < n; i++) {
      if (!ok[i]) continue;
      RyEnt& E = ent[idx2ent[i]];
      float x[RY_D]; ctx(pv, E, x);
      float lin = 0; for (int d = 0; d < RY_D; d++) lin += th[d] * x[d];
      float sb = 2.f * betaS(E.alpha, E.beta) - 1.f;
      float fat = E.plays ? expf(-(float)(playSeq - E.lastSeq) / tau) : 0.f;
      float nov = E.plays ? 0.f : 0.25f;
      float cont = 0;
      if (C) {
        float de = E.e - C->e, db = E.b - C->b, dd = E.dyn - C->dyn;
        float dist = sqrtf((de * de + db * db + dd * dd) / 3.f);
        cont = pref * (0.25f - dist) * 2.f;
      }
      float U = lin + 0.6f * sb - 0.9f * fat + nov + 0.5f * cont + ((E.flags & 1) ? 0.35f : 0.f) + 1e-3f * rand01();
      if (U > bestU) { bestU = U; best = i; }
    }
    return best;
  }

  // ---------------- simpan / muat ----------------
  static size_t maxSerial() { return sizeof(RyHdr) + sizeof(float) * (RY_D * RY_D + RY_D) + sizeof(RyEnt) * RY_MAXENT; }

  size_t serialize(uint8_t* buf, size_t cap) const {
    size_t need = sizeof(RyHdr) + sizeof(float) * (RY_D * RY_D + RY_D) + sizeof(RyEnt) * (size_t)nEnt;
    if (cap < need) return 0;
    RyHdr h; memset(&h, 0, sizeof(h));
    h.magic = 0x32534E52u; h.ver = 2; h.D = RY_D; h.entSz = (uint16_t)sizeof(RyEnt); h.nEnt = (uint16_t)nEnt;
    h.nUpd = nUpd; h.playSeq = playSeq;
    h.base[0] = volMean; h.base[1] = volVar; h.base[2] = enMean; h.base[3] = enVar;
    h.base[4] = brMean;  h.base[5] = brVar;  h.base[6] = listenEma;
    uint8_t* p = buf;
    memcpy(p, &h, sizeof(h)); p += sizeof(h);
    memcpy(p, Ainv, sizeof(float) * RY_D * RY_D); p += sizeof(float) * RY_D * RY_D;
    memcpy(p, bv, sizeof(float) * RY_D); p += sizeof(float) * RY_D;
    memcpy(p, ent, sizeof(RyEnt) * nEnt);
    return need;
  }

  bool deserialize(const uint8_t* buf, size_t len) {
    if (len < sizeof(RyHdr)) return false;
    RyHdr h; memcpy(&h, buf, sizeof(h));
    if (h.magic != 0x32534E52u || h.ver != 2 || h.D != RY_D || h.entSz != sizeof(RyEnt) || h.nEnt > RY_MAXENT) return false;
    size_t need = sizeof(RyHdr) + sizeof(float) * (RY_D * RY_D + RY_D) + sizeof(RyEnt) * (size_t)h.nEnt;
    if (len < need) return false;
    const uint8_t* p = buf + sizeof(h);
    float A[RY_D * RY_D], b[RY_D];
    memcpy(A, p, sizeof(A)); p += sizeof(A);
    memcpy(b, p, sizeof(b)); p += sizeof(b);
    for (int i = 0; i < RY_D * RY_D; i++) if (!(A[i] == A[i]) || fabsf(A[i]) > 50.f) return false;
    for (int i = 0; i < RY_D; i++) if (!(b[i] == b[i]) || fabsf(b[i]) > 1e5f || !(A[i * RY_D + i] > 0.f)) return false;
    uint32_t keepRng = rng;
    reset(keepRng);
    memcpy(Ainv, A, sizeof(A)); memcpy(bv, b, sizeof(b));
    nEnt = h.nEnt; memcpy(ent, p, sizeof(RyEnt) * nEnt);
    nUpd = h.nUpd; playSeq = h.playSeq;
    volMean = h.base[0]; volVar = h.base[1]; enMean = h.base[2]; enVar = h.base[3];
    brMean = h.base[4];  brVar = h.base[5];  listenEma = h.base[6];
    return true;
  }
};
