// =====================================================================
// wifisense_renphone.ino -- app WIFI SENSING (SCR_WSENSE) untuk Accretion Phone / NyxOS
// =====================================================================
// Mendeteksi GERAK / KEHADIRAN dan memperkirakan NAPAS (kasar) tanpa kamera, dari
// CSI (Channel State Information) WiFi. Idenya sama dengan proyek RuView, tapi jauh
// lebih sederhana: satu ESP32-S3 (HP ini) + router sebagai pemancar.
//
// CARA KERJA
//   1. Callback CSI (esp_wifi_set_csi_rx_cb) dipanggil tiap paket dari router diterima.
//      Di sana cuma dihitung rata-rata amplitudo subcarrier (ringan, tanpa alokasi).
//   2. Supaya paketnya rutin (~20/dtk, bukan cuma beacon ~10/dtk) HP mem-ping gateway
//      tiap 50 ms (esp_ping). Kalau ping tidak tersedia, jatuh ke beacon saja:
//      gerak masih terbaca, napas biasanya tidak.
//   3. Amplitudo dirata-ratakan per 100 ms (10 Hz) -> deret waktu x[n].
//   4. GERAK  : RMS selisih antar-sampel (2 dtk) / rata-rata, dibanding ambang hasil
//               kalibrasi ruangan kosong x sensitivitas. Selisih (bukan simpangan baku)
//               dipakai supaya napas yang lambat tidak dianggap gerak.
//   5. NAPAS  : jendela 30 dtk, buang tren, jendela Hann, Goertzel 0,10-0,50 Hz
//               (6-30 napas/mnt). Puncak harus jauh di atas median pita dan orang
//               tidak sedang bergerak. Hasil dihaluskan (median 5 estimasi).
//
// BATAS YANG JUJUR
//   * BELUM DIUJI di perangkat keras. Ambang (sensitivitas, rasio puncak napas) perlu
//     di-tuning di ruanganmu. Tombol Kalibrasi + tombol -/+ ada untuk itu.
//   * Bukan pose / tembus dinding. Hanya "ada gerak" dan "ada orang diam yang bernapas".
//   * Napas hanya masuk akal kalau HP DIAM (taruh di meja/tripod), orang diam 30 dtk
//     dalam jarak ~1-3 m, dan router tidak terlalu jauh.
//   * Perlu WiFi tersambung (STA). Butuh CSI aktif di core Arduino-ESP32:
//     CONFIG_ESP_WIFI_CSI_ENABLED=y. Kalau macro itu tidak ada, app tetap terbuild
//     dan menampilkan "CSI belum aktif" (lihat WSENSE_CSI di bawah). Untuk memaksa,
//     tambahkan -DWSENSE_FORCE=1 ke compiler.cpp.extra_flags.
//
// ATURAN PROYEK YANG DIIKUTI
//   - Fungsi publik hanya menerima tipe primitif / LGFX_Sprite& (bug prototype arduino-cli).
//   - Tidak ada String yang dibuat tiap frame, tidak ada alokasi heap; semua buffer statis (~5 KB).
//   - Layout dihitung dari SCR_W/SCR_H (landscape & portrait).
//   - Deklarasi maju fungsi publik dipasang ke phone.ino oleh apply_wifisense_patch.py.
// =====================================================================

#include "esp_wifi.h"
#include <math.h>

#if defined(CONFIG_ESP_WIFI_CSI_ENABLED) || defined(WSENSE_FORCE)
  #define WSENSE_CSI 1
#else
  #define WSENSE_CSI 0
#endif

#if WSENSE_CSI && __has_include("ping/ping_sock.h")
  #include "ping/ping_sock.h"
  #define WSENSE_PING 1
#else
  #define WSENSE_PING 0
#endif

// ---- parameter ----
#define WS_BIN_MS      100                 // 10 Hz
#define WS_N           300                 // 30 detik untuk napas
#define WS_MOT_N       20                  // 2 detik untuk gerak
#define WS_MEAN_N      100                 // 10 detik untuk rata-rata pembagi
#define WS_NF          41                  // 0,10 .. 0,50 Hz langkah 0,01
#define WS_BR_EVERY_MS 2000UL
#define WS_CAL_MS      15000UL
#define WS_GRAPH_N     150                 // 15 detik terakhir digambar
#define WS_DEF_BASE    0.006f              // skor dasar bawaan (sebelum kalibrasi)
#define WS_PING_MS     50
#define WS_RGB(r,g,b)  ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

enum WsState { WS_OFF = 0, WS_EMPTY, WS_MOVING, WS_STILL, WS_HOLD };

// ---- state ----
static bool     wsRun        = false;
static bool     wsCsiOk      = false;
static char     wsMsg[44]    = "";
static portMUX_TYPE wsMux    = portMUX_INITIALIZER_UNLOCKED;
static volatile float    wsBinSum = 0;      // diisi callback CSI
static volatile uint32_t wsBinCnt = 0;
static volatile uint32_t wsPktTotal = 0;
static uint32_t wsPktPrev    = 0;
static float    wsPktRate    = 0;
static unsigned long wsBinT0 = 0, wsRateT0 = 0, wsBrT0 = 0, wsUiT0 = 0, wsLastMove = 0;

static float    wsX[WS_N];                  // deret amplitudo (lingkar)
static int      wsHead       = 0;           // indeks tulis berikutnya
static int      wsCount      = 0;
static float    wsLastBin    = 0;

static float    wsScore      = 0;           // RMS selisih jendela 2 dtk (ternormalisasi)
static float    wsBase       = WS_DEF_BASE; // skor dasar ruang kosong
static const float wsSensMul[5] = { 6.0f, 4.0f, 3.0f, 2.2f, 1.6f };
static int      wsSens       = 2;           // indeks 0 (rendah) .. 4 (tinggi)
static bool     wsMoving     = false;
static int      wsMovHist    = 0;           // bin gerak dalam jendela napas (hitung kasar, peluruhan)

static bool     wsCal        = false;
static unsigned long wsCalT0 = 0;
static float    wsCalSum = 0, wsCalSq = 0; static int wsCalN = 0;

static float    wsBr         = 0;           // napas/mnt (0 = belum ada)
static float    wsBrRing[5]  = {0,0,0,0,0}; static int wsBrRingN = 0, wsBrRingI = 0;
static float    wsBrRatio    = 0;
static WsState  wsState      = WS_OFF;

#if WSENSE_PING
static esp_ping_handle_t wsPing = nullptr;
#endif

// ---------------------------------------------------------------------
// callback CSI -- jalan di task WiFi: singkat, tanpa alokasi / print
// ---------------------------------------------------------------------
#if WSENSE_CSI
static void wsCsiCb(void* ctx, wifi_csi_info_t* info) {
  (void)ctx;
  if (!info || !info->buf || info->len < 16) return;
  const int8_t* b = info->buf;
  int n = info->len / 2;
  float sum = 0; int cnt = 0;
  for (int i = 2; i < n; i++) {                         // lewati 2 subcarrier pertama (DC / guard)
    float im = (float)b[2 * i], re = (float)b[2 * i + 1];
    float a = sqrtf(re * re + im * im);
    if (a < 0.5f) continue;                             // subcarrier null
    sum += a; cnt++;
  }
  if (cnt < 8) return;
  portENTER_CRITICAL(&wsMux);
  wsBinSum += sum / (float)cnt;
  wsBinCnt++;
  wsPktTotal++;
  portEXIT_CRITICAL(&wsMux);
}
#endif

// ---------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------
static void wsResetSeries() {
  wsHead = 0; wsCount = 0; wsLastBin = 0; wsScore = 0; wsMoving = false; wsMovHist = 0;
  wsBr = 0; wsBrRatio = 0; wsBrRingN = 0; wsBrRingI = 0; wsLastMove = 0;
  portENTER_CRITICAL(&wsMux); wsBinSum = 0; wsBinCnt = 0; wsPktTotal = 0; portEXIT_CRITICAL(&wsMux);
  wsPktPrev = 0; wsPktRate = 0;
}

static bool wsStart() {
  wsMsg[0] = 0;
#if WSENSE_CSI
  if (WiFi.status() != WL_CONNECTED) { snprintf(wsMsg, sizeof(wsMsg), "Sambungkan WiFi dulu"); return false; }
  wsResetSeries();
  WiFi.setSleep(false);                                  // mode hemat daya bikin paket jarang
  wifi_csi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.lltf_en = true; cfg.htltf_en = true; cfg.stbc_htltf2_en = true; cfg.ltf_merge_en = true;
  cfg.channel_filter_en = false; cfg.manu_scale = false; cfg.shift = 0; cfg.dump_ack_en = false;
  if (esp_wifi_set_csi_config(&cfg) != ESP_OK) { snprintf(wsMsg, sizeof(wsMsg), "set_csi_config gagal"); return false; }
  if (esp_wifi_set_csi_rx_cb(wsCsiCb, nullptr) != ESP_OK) { snprintf(wsMsg, sizeof(wsMsg), "set_csi_rx_cb gagal"); return false; }
  if (esp_wifi_set_csi(true) != ESP_OK) { snprintf(wsMsg, sizeof(wsMsg), "CSI tidak didukung build ini"); return false; }
  #if WSENSE_PING
  {
    IPAddress gw = WiFi.gatewayIP();
    esp_ping_config_t pc = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t tgt; IP_ADDR4(&tgt, gw[0], gw[1], gw[2], gw[3]);
    pc.target_addr = tgt; pc.count = ESP_PING_COUNT_INFINITE;
    pc.interval_ms = WS_PING_MS; pc.timeout_ms = 1000; pc.data_size = 1;
    esp_ping_callbacks_t cbs; memset(&cbs, 0, sizeof(cbs));
    if (esp_ping_new_session(&pc, &cbs, &wsPing) == ESP_OK) esp_ping_start(wsPing);
    else wsPing = nullptr;
  }
  #endif
  wsCsiOk = true; wsRun = true;
  unsigned long now = millis(); wsBinT0 = wsRateT0 = wsBrT0 = wsUiT0 = now;
  return true;
#else
  snprintf(wsMsg, sizeof(wsMsg), "CSI belum aktif di build ini");
  return false;
#endif
}

static void wsStop() {
#if WSENSE_CSI
  if (wsCsiOk) { esp_wifi_set_csi(false); esp_wifi_set_csi_rx_cb(nullptr, nullptr); }
  #if WSENSE_PING
  if (wsPing) { esp_ping_stop(wsPing); esp_ping_delete_session(wsPing); wsPing = nullptr; }
  #endif
#endif
  wsCsiOk = false; wsRun = false; wsCal = false; wsState = WS_OFF;
}

// ---------------------------------------------------------------------
// pemrosesan sinyal
// ---------------------------------------------------------------------
static inline float wsAt(int back) {                    // back=0 sampel terbaru
  int i = wsHead - 1 - back; while (i < 0) i += WS_N;
  return wsX[i % WS_N];
}

static void wsMeanStd(int n, float* mean, float* sd) {
  if (n > wsCount) n = wsCount;
  if (n < 2) { *mean = 0; *sd = 0; return; }
  float s = 0, q = 0;
  for (int k = 0; k < n; k++) { float v = wsAt(k); s += v; q += v * v; }
  float m = s / n, var = q / n - m * m; if (var < 0) var = 0;
  *mean = m; *sd = sqrtf(var);
}

static void wsPushBin(float v) {
  wsX[wsHead] = v; wsHead = (wsHead + 1) % WS_N; if (wsCount < WS_N) wsCount++;
  wsLastBin = v;
  if (wsCount < WS_MOT_N + 5) return;

  // skor gerak = RMS selisih antar-sampel 2 dtk terakhir / rata-rata 10 dtk.
  // Gerakan tubuh itu CEPAT (selisih besar); napas LAMBAT (selisih kecil), jadi napas
  // tidak ikut terhitung sebagai gerak -- beda dengan simpangan baku yang ikut naik.
  float mLong, sdLong;
  wsMeanStd(WS_MEAN_N, &mLong, &sdLong);
  float dq = 0;
  for (int k = 0; k < WS_MOT_N - 1; k++) { float dd = wsAt(k) - wsAt(k + 1); dq += dd * dd; }
  wsScore = (mLong > 1e-3f) ? sqrtf(dq / (WS_MOT_N - 1)) / mLong : 0;

  if (wsCal) {                                          // kumpulkan data ruang kosong
    wsCalSum += wsScore; wsCalSq += wsScore * wsScore; wsCalN++;
  }
  float thr = wsBase * wsSensMul[wsSens];
  bool mv = wsMoving ? (wsScore > 0.7f * thr) : (wsScore > thr);   // histeresis: tidak berkedip di ambang
  if (mv) { wsLastMove = millis(); if (wsLastMove == 0) wsLastMove = 1; }
  wsMoving = mv;
  wsMovHist = wsMovHist * 995 / 1000 + (mv ? 10 : 0);   // peluruhan lambat ~ 30 dtk
}

static void wsFinishCal() {
  wsCal = false;
  if (wsCalN < 20) { snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi gagal: data kurang"); return; }
  float m = wsCalSum / wsCalN, var = wsCalSq / wsCalN - m * m; if (var < 0) var = 0;
  float b = m + 3.0f * sqrtf(var);                      // rata-rata + 3 sigma noise ruang kosong
  if (b < 0.002f) b = 0.002f;
  if (b > 0.10f)  b = 0.10f;
  // wsBase dikalikan wsSensMul[] saat dibandingkan, jadi simpan sebagai dasar (bagi 3 = sens default)
  wsBase = b / 1.5f;
  snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi selesai (dasar %.3f)", (double)wsBase);
}

// Goertzel: daya pada frekuensi f (Hz) untuk deret terdetrend+berjendela
static float wsGoertzel(const float* d, int n, float f) {
  float w = 2.0f * (float)M_PI * f / 10.0f;             // 10 Hz sampling
  float c = 2.0f * cosf(w), s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) { float s0 = d[i] + c * s1 - s2; s2 = s1; s1 = s0; }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void wsBreathing() {
  if (wsCount < WS_N) return;                           // butuh 30 dtk penuh
  // pakai buffer sementara di PSRAM? 1,2 KB cukup di stack task loop (stack loop() Arduino 8 KB)
  static float d[WS_N];
  // urut kronologis lama -> baru, dibagi rata-rata (ke satuan CV), buang tren linear
  float mean = 0;
  for (int k = 0; k < WS_N; k++) { float v = wsAt(WS_N - 1 - k); d[k] = v; mean += v; }
  mean /= WS_N;
  if (mean < 1e-3f) return;
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int k = 0; k < WS_N; k++) { d[k] = d[k] / mean - 1.0f; sx += k; sy += d[k]; sxx += (float)k * k; sxy += k * d[k]; }
  float den = WS_N * sxx - sx * sx; float slope = den != 0 ? (WS_N * sxy - sx * sy) / den : 0;
  float icpt = (sy - slope * sx) / WS_N;
  float varsum = 0;
  for (int k = 0; k < WS_N; k++) {
    d[k] -= (icpt + slope * k);
    float hann = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * k / (WS_N - 1));
    d[k] *= hann; varsum += d[k] * d[k];
  }
  float rms = sqrtf(varsum / WS_N);

  float P[WS_NF], tmp[WS_NF]; int best = 0;
  for (int i = 0; i < WS_NF; i++) {
    P[i] = wsGoertzel(d, WS_N, 0.10f + 0.01f * i); if (P[i] < 0) P[i] = 0;
    tmp[i] = P[i]; if (P[i] > P[best]) best = i;
  }
  for (int i = 1; i < WS_NF; i++) { float v = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = v; }
  float med = tmp[WS_NF / 2];
  wsBrRatio = P[best] / (med + 1e-12f);

  bool calm   = wsMovHist < 80;                         // hampir tidak ada gerak besar 30 dtk terakhir
  bool strong = rms > wsBase * 0.5f;                    // ada variasi di atas noise ruang kosong
  bool peak   = wsBrRatio >= 6.0f && best > 0 && best < WS_NF - 1;
  if (calm && strong && peak) {
    float bpm = (0.10f + 0.01f * best) * 60.0f;
    wsBrRing[wsBrRingI] = bpm; wsBrRingI = (wsBrRingI + 1) % 5; if (wsBrRingN < 5) wsBrRingN++;
    float t[5]; for (int i = 0; i < wsBrRingN; i++) t[i] = wsBrRing[i];
    for (int i = 1; i < wsBrRingN; i++) { float v = t[i]; int j = i - 1; while (j >= 0 && t[j] > v) { t[j + 1] = t[j]; j--; } t[j + 1] = v; }
    wsBr = t[wsBrRingN / 2];
  } else {
    if (wsBrRingN > 0) wsBrRingN--;                     // keyakinan meluruh
    if (wsBrRingN == 0) wsBr = 0;
  }
}

static void wsUpdateState() {
  if (!wsRun) { wsState = WS_OFF; return; }
  unsigned long now = millis();
  if (wsMoving) wsState = WS_MOVING;
  else if (wsBr > 0) wsState = WS_STILL;
  else if (wsLastMove && now - wsLastMove < 15000UL) wsState = WS_HOLD;
  else wsState = WS_EMPTY;
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
void wsenseEnter() {
  wsMsg[0] = 0; wsCal = false;
  if (!wsStart()) { wsState = WS_OFF; }
  needRedraw = true;
}

void wsenseExit() { wsStop(); }

// dipanggil dari loop() phone.ino tiap putaran saat app terbuka
void wsenseLoopPoll() {
  if (!wsRun) return;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {                  // WiFi putus -> berhenti rapi
    wsStop(); snprintf(wsMsg, sizeof(wsMsg), "WiFi terputus"); needRedraw = true; return;
  }
  // gabungkan paket menjadi bin 100 ms
  while (now - wsBinT0 >= WS_BIN_MS) {
    wsBinT0 += WS_BIN_MS;
    float sum; uint32_t cnt;
    portENTER_CRITICAL(&wsMux); sum = wsBinSum; cnt = wsBinCnt; wsBinSum = 0; wsBinCnt = 0; portEXIT_CRITICAL(&wsMux);
    float v = cnt ? sum / (float)cnt : wsLastBin;       // bin tanpa paket: ulangi nilai terakhir
    if (v > 0) wsPushBin(v);
    if (now - wsBinT0 > 1000UL) wsBinT0 = now;          // tertinggal jauh (mis. layar berat) -> sinkron ulang
  }
  if (now - wsRateT0 >= 1000UL) {
    uint32_t tot; portENTER_CRITICAL(&wsMux); tot = wsPktTotal; portEXIT_CRITICAL(&wsMux);
    wsPktRate = (float)(tot - wsPktPrev) * 1000.0f / (float)(now - wsRateT0);
    wsPktPrev = tot; wsRateT0 = now;
  }
  if (wsCal && now - wsCalT0 >= WS_CAL_MS) wsFinishCal();
  if (now - wsBrT0 >= WS_BR_EVERY_MS) { wsBrT0 = now; if (!wsCal) wsBreathing(); }
  wsUpdateState();
  if (now - wsUiT0 >= 200UL) { wsUiT0 = now; needRedraw = true; }
}

// ---------------------------------------------------------------------
// geometri (dipakai drawWsense & wsenseTouch)
// ---------------------------------------------------------------------
static int wsBtnY()   { return SCR_H - BACK_H - 3 - 6 - 26; }
static int wsGraphBottom() { return wsBtnY() - 5; }
static void wsBtnRect(int i, int* x, int* w) {          // 4 tombol: Mulai/Henti, Kalibrasi, -, +
  int gap = 6, total = SCR_W - 16;
  int wBig = (total - 3 * gap) * 2 / 7, wSmall = (total - 3 * gap - 2 * wBig) / 2;
  int xs[4], ws[4] = { wBig, wBig, wSmall, wSmall };
  xs[0] = 8; for (int k = 1; k < 4; k++) xs[k] = xs[k - 1] + ws[k - 1] + gap;
  *x = xs[i]; *w = ws[i];
}

static void wsBtn(LGFX_Sprite& s, int i, const char* label, uint16_t col, bool on) {
  int x, w; wsBtnRect(i, &x, &w);
  nyxAaRR(s, x, wsBtnY(), w, 26, 8, on ? col : blend565(T().surface2, T().text, 28));
  s.setTextSize(1); s.setTextColor(on ? T().bg : T().text);
  int tw = s.textWidth(label);
  s.setCursor(x + (w - tw) / 2, wsBtnY() + 9); s.print(label);
}

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
void drawWsense(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "WiFi Sensing");

  // ---- kartu status ----
  int cx = 8, cy = 26 + 18, cw = SCR_W - 16, ch = 46;
  drawGlassPanel(s, cx, cy, cw, ch, 12, T().surface2, 170);
  const char* txt; const char* sub; uint16_t col;
  switch (wsState) {
    case WS_MOVING: txt = "Ada gerak";          sub = "Gerakan terdeteksi";                  col = WS_RGB(255,159,10); break;
    case WS_STILL:  txt = "Ada orang (diam)";   sub = "Pola napas terdeteksi";              col = WS_RGB(48,209,88);  break;
    case WS_HOLD:   txt = "Baru ada gerak";     sub = "Menunggu tenang untuk cek napas";    col = WS_RGB(255,214,10); break;
    case WS_EMPTY:  txt = "Kosong";             sub = "Tidak ada gerak / napas";            col = T().subtext;        break;
    default:        txt = "Tidak aktif";        sub = wsMsg[0] ? wsMsg : "Ketuk Mulai";     col = T().danger;         break;
  }
  nyxAaCircle(s, cx + 20, cy + ch / 2, 8, col);
  s.setTextSize(2); s.setTextColor(T().text); s.setCursor(cx + 38, cy + 8); s.print(txt);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(cx + 38, cy + 30); s.print(sub);

  // ---- angka ----
  char b[64];
  int iy = cy + ch + 6;
  s.setTextSize(1); s.setTextColor(T().subtext);
  snprintf(b, sizeof(b), "Paket %.0f/dtk", (double)wsPktRate);
  s.setCursor(12, iy); s.print(b);
  float thr = wsBase * wsSensMul[wsSens];
  snprintf(b, sizeof(b), "Gerak %.1fx", (double)(thr > 0 ? wsScore / thr : 0));
  s.setCursor(SCR_W / 3 + 4, iy); s.print(b);
  if (wsBr > 0) snprintf(b, sizeof(b), "Napas ~%.0f/mnt", (double)wsBr);
  else          snprintf(b, sizeof(b), "Napas --");
  s.setTextColor(wsBr > 0 ? WS_RGB(48,209,88) : T().subtext);
  s.setCursor(SCR_W * 2 / 3, iy); s.print(b);

  // ---- grafik 15 dtk terakhir (simpangan dari rata-rata, auto-skala) ----
  int gx = 8, gy = iy + 14, gw = SCR_W - 16, gh = wsGraphBottom() - gy;
  if (gh < 30) gh = 30;
  drawGlassPanel(s, gx, gy, gw, gh, 10, T().surface2, 140);
  int n = wsCount < WS_GRAPH_N ? wsCount : WS_GRAPH_N;
  if (n > 3) {
    float m = 0; for (int k = 0; k < n; k++) m += wsAt(k); m /= n;
    float mx = 0.01f * m;                               // lantai skala 1% dari rata-rata
    for (int k = 0; k < n; k++) { float a = fabsf(wsAt(k) - m); if (a > mx) mx = a; }
    int mid = gy + gh / 2, half = gh / 2 - 4;
    s.drawFastHLine(gx + 4, mid, gw - 8, T().divider);
    int px = 0, py = 0;
    for (int k = 0; k < n; k++) {
      int xx = gx + 4 + (int)((long)(gw - 8) * (n - 1 - k) / (n - 1));   // terbaru di kanan
      int yy = mid - (int)((wsAt(k) - m) / mx * half);
      if (k > 0) s.drawLine(px, py, xx, yy, wsMoving ? WS_RGB(255,159,10) : T().accent);
      px = xx; py = yy;
    }
  } else {
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(gx + 10, gy + gh / 2 - 4); s.print(wsRun ? "Mengumpulkan data..." : "-");
  }
  // bar skor gerak di tepi atas grafik
  if (wsRun) {
    float r = thr > 0 ? wsScore / thr : 0; if (r > 1.5f) r = 1.5f;
    int bw = (int)((gw - 16) * r / 1.5f);
    s.fillRect(gx + 8, gy + 3, gw - 16, 2, T().divider);
    s.fillRect(gx + 8, gy + 3, bw, 2, r >= 1.0f ? WS_RGB(255,159,10) : T().accent);
  }
  if (wsCal) {
    int sec = (int)((WS_CAL_MS - (millis() - wsCalT0)) / 1000UL);
    snprintf(b, sizeof(b), "Kalibrasi... %d dtk (jangan ada orang)", sec < 0 ? 0 : sec);
    s.setTextColor(WS_RGB(255,214,10)); s.setCursor(gx + 10, gy + gh - 12); s.print(b);
  } else if (wsMsg[0] && wsRun) {
    s.setTextColor(T().subtext); s.setCursor(gx + 10, gy + gh - 12); s.print(wsMsg);
  }

  // ---- tombol ----
  wsBtn(s, 0, wsRun ? "Berhenti" : "Mulai", wsRun ? T().danger : T().accent, true);
  wsBtn(s, 1, "Kalibrasi", T().accent2, wsCal);
  wsBtn(s, 2, "-", T().accent, false);
  wsBtn(s, 3, "+", T().accent, false);
  { int x, w; wsBtnRect(3, &x, &w); int x2, w2; wsBtnRect(2, &x2, &w2);   // indikator sensitivitas di tengah pasangan -/+
    s.setTextSize(1); s.setTextColor(T().subtext);
    snprintf(b, sizeof(b), "Sens %d/5", wsSens + 1);
    int tw = s.textWidth(b); s.setCursor(x2 + (x + w - x2 - tw) / 2, wsBtnY() - 11); s.print(b); }

  drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh
// ---------------------------------------------------------------------
void wsenseTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }
  if (y < wsBtnY() || y > wsBtnY() + 26) return;
  for (int i = 0; i < 4; i++) {
    int bx, bw; wsBtnRect(i, &bx, &bw);
    if (x < bx || x > bx + bw) continue;
    if (i == 0) {
      if (wsRun) wsStop(); else { wsMsg[0] = 0; wsStart(); }
    } else if (i == 1) {
      if (!wsRun) { snprintf(wsMsg, sizeof(wsMsg), "Mulai dulu"); }
      else if (wsCal) { wsCal = false; wsMsg[0] = 0; }                   // ketuk lagi = batal
      else { wsCal = true; wsCalT0 = millis(); wsCalSum = wsCalSq = 0; wsCalN = 0; wsMsg[0] = 0; }
    } else if (i == 2) { if (wsSens > 0) wsSens--; }
    else               { if (wsSens < 4) wsSens++; }
    needRedraw = true;
    return;
  }
}
