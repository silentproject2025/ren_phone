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
//   2. Supaya paketnya rutin (~40/dtk, bukan cuma beacon ~10/dtk) HP mem-ping gateway
//      tiap 25 ms (esp_ping). Kalau ping tidak tersedia, jatuh ke beacon saja:
//      gerak masih terbaca, napas biasanya tidak.
//   3. Amplitudo dirata-ratakan per 100 ms (10 Hz) -> deret waktu x[n].
//   4. GERAK  : RMS selisih antar-sampel (2 dtk) / rata-rata, dibanding ambang hasil
//               kalibrasi ruangan kosong x sensitivitas. Selisih (bukan simpangan baku)
//               dipakai supaya napas yang lambat tidak dianggap gerak.
//   5. NAPAS  : jendela 30 dtk, buang tren, jendela Hann, Goertzel 0,10-0,50 Hz
//               (6-30 napas/mnt). Puncak harus jauh di atas median pita dan orang
//               tidak sedang bergerak. Hasil dihaluskan (median 5 estimasi).
//
// MODE MULTI-SUBCARRIER (WSENSE_MULTI_V1, bawaan aktif)
//   Rata-rata amplitudo ke satu angka membuang informasi: gerak/napas menggeser tiap
//   subcarrier dgn arah & kekuatan berbeda (multipath), jadi sebagian saling meniadakan
//   saat dirata-rata. Mode multi menyimpan 64 subcarrier LLTF (128 byte pertama buffer CSI)
//   per bin 100 ms (ring 30 dtk di PSRAM, ~38 KB) lalu:
//     GERAK : skor per subcarrier (RMS selisih 2 dtk / rata-rata subcarrier), diambil
//             rata-rata SEPARUH TERATAS. Noise menaikkan semua subcarrier sama rata,
//             gerak hanya subcarrier yg dilalui jalur -> ekor atas lebih sensitif.
//     NAPAS : spektrum Goertzel tiap subcarrier (dinormalkan median-nya sendiri, dibatasi 25x)
//             DIJUMLAHKAN lintas subcarrier (tanpa fase -> tak saling meniadakan). Ambang
//             puncak turun mengikuti jumlah subcarrier efektif: 1 + 5/sqrt(K/3). Dikerjakan
//             6 subcarrier per putaran loop supaya UI tidak patah-patah.
//   Kalau PSRAM kurang / paket CSI tanpa LLTF (< 128 byte) / < 8 subcarrier hidup, otomatis
//   jatuh ke mode rata-rata lama. Ketuk label "Multi N"/"Rata" di pojok grafik utk berganti
//   (A/B di ruanganmu). Dasar kalibrasi disimpan terpisah per mode.
//
// PENINGKATAN WSENSE_V2
//   * Filter paket: hanya dari AP kita (MAC sumber = BSSID) dan satu kelas PHY (sig_mode + lebar kanal);
//     paket glitch (rata-rata amplitudo meloncat >40% sesaat) dibuang. Paket yg dibuang dihitung di wsDropTotal.
//   * Baseline adaptif: persentil-25 skor gerak dilacak terus (naik 0,15%/bin, turun 0,45%/bin = kuantil 25%),
//     ambang dikali rasio terhadap nilai saat kalibrasi (dibatasi 0,6x .. 3x). Hanya aktif sesudah kalibrasi.
//   * Kalibrasi & sensitivitas disimpan (Preferences \"wsense\", kunci BSSID router) dan dimuat saat app dibuka.
//   * Napas: interpolasi parabola di puncak spektrum -> bpm tidak lagi terkunci ke kelipatan 0,6.
//
// BATAS YANG JUJUR
//   * BELUM DIUJI di perangkat keras (mode multi baru diuji dgn simulasi di tests/wifisense). Ambang (sensitivitas, rasio puncak napas) perlu
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
#define WS_PING_MS     25
// ---- mode multi-subcarrier (WSENSE_MULTI_V1) ----
#define WS_NSUB        64                  // subcarrier LLTF (128 byte pertama buffer CSI)
#define WS_SUB_MIN_AMP 2.0f                // rata-rata amplitudo < ini = subcarrier null/guard -> dilewati
#define WS_DEF_BASE_M  0.03f               // skor dasar bawaan mode multi (perkiraan kasar; kalibrasi menggantikannya)
#define WS_BR_SLICE    6                   // subcarrier diproses per putaran loop
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

static float    wsX[WS_N] EXT_RAM_BSS_ATTR;                  // deret amplitudo (lingkar)
static int      wsHead       = 0;           // indeks tulis berikutnya
static int      wsCount      = 0;
static float    wsLastBin    = 0;

static float    wsScore      = 0;           // RMS selisih jendela 2 dtk (ternormalisasi)
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

// ---- WSENSE_V2: filter paket, baseline adaptif, kalibrasi tersimpan ----
#define WS_Q_UP        0.0015f             // pelacak kuantil-25: naik/bin bila skor >= Q (relatif)
#define WS_Q_DN        0.0045f             // turun/bin bila skor < Q  (UP*0,75 = DN*0,25 -> kuantil 25%)
#define WS_ADAPT_MIN   0.6f
#define WS_ADAPT_MAX   3.0f
#define WS_CAL_BUF     160                 // skor selama kalibrasi 15 dtk (150 bin) utk persentil-25
static uint8_t  wsBssid[6]   = {0};
static bool     wsBssidOk    = false;
static uint8_t  wsLockCls    = 0xFF;        // kelas PHY yg dikunci (sig_mode | cwb<<2); 0xFF = belum
static uint16_t wsRejRun     = 0, wsOutRun = 0;
static uint32_t wsDropTotal  = 0;           // paket CSI yg dibuang filter (info/diagnosa)
static float    wsAmpEma     = 0;           // rata-rata amplitudo paket (EMA) utk deteksi glitch
static float    wsQ[2]       = {0, 0};      // kuantil-25 skor gerak berjalan: [0]=rata-rata, [1]=multi
static float    wsQref[2]    = {0, 0};      // nilai saat kalibrasi (0 = belum dikalibrasi -> tanpa adaptasi)
static float    wsAdaptMul   = 1.0f;
static float    wsCalBuf[WS_CAL_BUF] EXT_RAM_BSS_ATTR;

// ---- mode multi-subcarrier ----
static bool      wsMulti     = true;        // pilihan pengguna
static bool      wsUseSub    = false;       // mode yg SEDANG dipakai (multi hanya kalau datanya cukup)
static uint16_t* wsSub       = nullptr;     // ring [WS_N][WS_NSUB], amplitudo*64, di PSRAM
static float     wsSubSum[WS_NSUB];         // diisi callback CSI (dijaga wsMux)
static uint32_t  wsSubCnt    = 0;
static float     wsSubLast[WS_NSUB];
static float     wsSubMean[WS_NSUB];        // EMA ~5 dtk per subcarrier
static int       wsNValid    = 0;
static int       wsBrK       = -1;          // napas multi: subcarrier berikutnya (-1 = tidak jalan)
static int       wsBrUsed    = 0;
static float     wsPsum[WS_NF];              // jumlah spektrum ternormalkan-median (dibatasi) -> uji kekuatan puncak
static float     wsPn[WS_NF];                // jumlah spektrum ternormalkan-total -> LOKASI puncak (tanpa batas, tak seri)
static int       wsSubIdle   = 0;            // bin berturut-turut tanpa paket ber-LLTF
static float     wsD[WS_N] EXT_RAM_BSS_ATTR;
static float     wsHann[WS_N] EXT_RAM_BSS_ATTR;
static bool      wsHannOk    = false;
static float     wsBaseS     = WS_DEF_BASE; // dasar mode rata-rata
static float     wsBaseM     = WS_DEF_BASE_M; // dasar mode multi
static inline float& wsBaseRef() { return wsUseSub ? wsBaseM : wsBaseS; }
#define wsBase (wsBaseRef())
static inline float wsThr() { return wsBase * wsAdaptMul * wsSensMul[wsSens]; }   // WSENSE_V2: ambang gerak efektif

struct WsCalRec { uint8_t ver; uint8_t bssid[6]; uint8_t sens; float baseS, baseM, qrefS, qrefM; };
static bool wsFin(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }
static void wsCalLoad() {                                // kalibrasi tersimpan untuk router (BSSID) yg sama
  if (!wsBssidOk) return;
  Preferences p; if (!p.begin("wsense", false)) return;
  WsCalRec r; size_t n = p.getBytes("cal", &r, sizeof(r)); p.end();
  if (n != sizeof(r) || r.ver != 1 || memcmp(r.bssid, wsBssid, 6) != 0) return;
  if (!wsFin(r.baseS, 0.0005f, 1.0f) || !wsFin(r.baseM, 0.0005f, 1.0f) || !wsFin(r.qrefS, 0.0f, 1.0f) || !wsFin(r.qrefM, 0.0f, 1.0f) || r.sens > 4) return;
  wsBaseS = r.baseS; wsBaseM = r.baseM; wsSens = r.sens;
  wsQref[0] = wsQ[0] = r.qrefS; wsQref[1] = wsQ[1] = r.qrefM; wsAdaptMul = 1.0f;
  if (!wsMsg[0]) snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi tersimpan dimuat");
}
static void wsCalSave() {
  if (!wsBssidOk) return;
  WsCalRec r; memset(&r, 0, sizeof(r));
  r.ver = 1; memcpy(r.bssid, wsBssid, 6); r.sens = (uint8_t)wsSens;
  r.baseS = wsBaseS; r.baseM = wsBaseM; r.qrefS = wsQref[0]; r.qrefM = wsQref[1];
  Preferences p; if (!p.begin("wsense", false)) return;
  p.putBytes("cal", &r, sizeof(r)); p.end();
}

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
  // WSENSE_V2: hanya paket dari AP kita + satu kelas PHY (pemancar / mode lain menggeser skala amplitudo)
  if (wsBssidOk && memcmp(info->mac, wsBssid, 6) != 0) { wsDropTotal++; return; }
  const uint8_t cls = (uint8_t)((info->rx_ctrl.sig_mode & 3) | ((info->rx_ctrl.cwb & 1) << 2));
  if (wsLockCls == 0xFF) wsLockCls = cls;
  if (cls != wsLockCls) {
    if (++wsRejRun < 40) { wsDropTotal++; return; }
    wsLockCls = cls;                                     // kelas lama hilang >40 paket berturut-turut -> pindah
  }
  wsRejRun = 0;
  const int8_t* b = info->buf;
  int n = info->len / 2;
  const bool haveSub = (info->len >= 2 * WS_NSUB);      // LLTF utuh -> simpan per subcarrier
  float sub[WS_NSUB];
  float sum = 0; int cnt = 0;
  for (int i = 0; i < n; i++) {
    if (i < 2 && !haveSub) continue;                    // jalur rata-rata: lewati 2 subcarrier pertama (DC / guard)
    float im = (float)b[2 * i], re = (float)b[2 * i + 1];
    float a = sqrtf(re * re + im * im);
    if (haveSub && i < WS_NSUB) sub[i] = a;
    if (i < 2 || a < 0.5f) continue;                    // subcarrier null
    sum += a; cnt++;
  }
  if (cnt < 8) return;
  const float pm = sum / (float)cnt;
  if (wsAmpEma > 0.0f && fabsf(pm / wsAmpEma - 1.0f) > 0.4f) {   // glitch: loncatan >40% sesaat
    if (++wsOutRun < 10) { wsDropTotal++; return; }
    wsAmpEma = pm;                                       // bertahan >=10 paket = perubahan nyata, terima
  }
  wsOutRun = 0;
  wsAmpEma = (wsAmpEma > 0.0f) ? wsAmpEma + (pm - wsAmpEma) * 0.02f : pm;
  portENTER_CRITICAL(&wsMux);
  wsBinSum += pm;
  wsBinCnt++;
  wsPktTotal++;
  if (haveSub) { for (int k = 0; k < WS_NSUB; k++) wsSubSum[k] += sub[k]; wsSubCnt++; }
  portEXIT_CRITICAL(&wsMux);
}
#endif

// ---------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------
static void wsResetSeries() {
  wsHead = 0; wsCount = 0; wsLastBin = 0; wsScore = 0; wsMoving = false; wsMovHist = 0;
  wsBr = 0; wsBrRatio = 0; wsBrRingN = 0; wsBrRingI = 0; wsLastMove = 0;
  portENTER_CRITICAL(&wsMux);
  wsBinSum = 0; wsBinCnt = 0; wsPktTotal = 0;
  memset(wsSubSum, 0, sizeof(wsSubSum)); wsSubCnt = 0;
  portEXIT_CRITICAL(&wsMux);
  memset(wsSubLast, 0, sizeof(wsSubLast)); memset(wsSubMean, 0, sizeof(wsSubMean));
  if (wsSub) memset(wsSub, 0, (size_t)WS_N * WS_NSUB * sizeof(uint16_t));
  wsNValid = 0; wsUseSub = false; wsBrK = -1; wsBrUsed = 0; wsSubIdle = 0;
  wsPktPrev = 0; wsPktRate = 0;
  wsLockCls = 0xFF; wsRejRun = 0; wsOutRun = 0; wsAmpEma = 0;            // WSENSE_V2
  wsQ[0] = wsQref[0]; wsQ[1] = wsQref[1]; wsAdaptMul = 1.0f;
}

static bool wsStart() {
  wsMsg[0] = 0;
#if WSENSE_CSI
  if (WiFi.status() != WL_CONNECTED) { snprintf(wsMsg, sizeof(wsMsg), "Sambungkan WiFi dulu"); return false; }
  if (!wsSub) wsSub = (uint16_t*)heap_caps_malloc((size_t)WS_N * WS_NSUB * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!wsSub) snprintf(wsMsg, sizeof(wsMsg), "PSRAM kurang: mode rata-rata");
  { uint8_t* bs = WiFi.BSSID(); wsBssidOk = false;      // WSENSE_V2: kunci kalibrasi & filter paket ke AP ini
    if (bs) { for (int i = 0; i < 6; i++) if (bs[i]) wsBssidOk = true; if (wsBssidOk) memcpy(wsBssid, bs, 6); } }
  wsResetSeries();
  wsCalLoad();
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
  wsBrK = -1; wsUseSub = false;
  if (wsSub) { heap_caps_free(wsSub); wsSub = nullptr; }   // CSI sudah dimatikan di atas -> callback tak menyentuh ring
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

// ---- mode multi-subcarrier: simpan 1 baris (64 subcarrier) per bin. Harus dipanggil SEBELUM wsPushBin
//      (wsHead belum maju) supaya barisnya sejajar dgn wsX. ----
static void wsPushSub(const float* acc, uint32_t cnt) {
  uint16_t* row = wsSub + (size_t)wsHead * WS_NSUB;
  for (int k = 0; k < WS_NSUB; k++) {
    float v = cnt ? acc[k] / (float)cnt : wsSubLast[k]; // bin tanpa paket: ulangi nilai terakhir
    wsSubLast[k] = v;
    if (v > 1020.0f) v = 1020.0f;
    row[k] = (uint16_t)(v * 64.0f + 0.5f);
    if (wsSubMean[k] <= 0) wsSubMean[k] = v; else wsSubMean[k] += (v - wsSubMean[k]) * 0.02f;
  }
}

// skor gerak multi: per subcarrier RMS selisih 2 dtk / rata-rata subcarrier, lalu rata-rata SEPARUH TERATAS.
// Mengembalikan -1 kalau subcarrier hidup < 8 (-> pakai skor rata-rata).
static float wsSubMotion() {
  int rows[WS_MOT_N];
  { int i = wsHead - 1; if (i < 0) i += WS_N;           // wsHead sudah maju -> sampel terbaru = head-1
    for (int j = 0; j < WS_MOT_N; j++) { rows[j] = i; if (--i < 0) i = WS_N - 1; } }
  float sc[WS_NSUB]; int m = 0;
  if (wsSubIdle >= 20) { wsNValid = 0; return -1.0f; } // 2 dtk tanpa paket ber-LLTF: data subcarrier basi -> jalur rata-rata
  for (int k = 0; k < WS_NSUB; k++) {
    float mean = wsSubMean[k];
    if (mean < WS_SUB_MIN_AMP) continue;                // null / guard
    float dq = 0; float prev = (float)wsSub[(size_t)rows[0] * WS_NSUB + k];
    for (int j = 1; j < WS_MOT_N; j++) {
      float cur = (float)wsSub[(size_t)rows[j] * WS_NSUB + k]; float dd = (prev - cur) * (1.0f / 64.0f); dq += dd * dd; prev = cur;
    }
    sc[m++] = sqrtf(dq / (WS_MOT_N - 1)) / mean;
  }
  wsNValid = m;
  if (m < 8) return -1.0f;
  for (int i = 1; i < m; i++) { float v = sc[i]; int j = i - 1; while (j >= 0 && sc[j] > v) { sc[j + 1] = sc[j]; j--; } sc[j + 1] = v; }
  int h = m / 2; float sum = 0;
  for (int i = m - h; i < m; i++) sum += sc[i];
  return sum / (float)h;
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
  { float sm = (wsMulti && wsSub) ? wsSubMotion() : -1.0f;   // WSENSE_MULTI_V1
    wsUseSub = (sm >= 0);
    if (wsUseSub) wsScore = sm; }
  { int mi = wsUseSub ? 1 : 0;                            // WSENSE_V2: baseline adaptif (kuantil-25 berjalan)
    if (wsScore > 0) {
      if (wsQ[mi] <= 0) wsQ[mi] = wsScore;
      else wsQ[mi] += wsQ[mi] * (wsScore < wsQ[mi] ? -WS_Q_DN : WS_Q_UP);
    }
    float am = 1.0f;
    if (wsQref[mi] > 0 && wsQ[mi] > 0) { am = wsQ[mi] / wsQref[mi]; if (am < WS_ADAPT_MIN) am = WS_ADAPT_MIN; if (am > WS_ADAPT_MAX) am = WS_ADAPT_MAX; }
    wsAdaptMul = am; }

  if (wsCal) {                                          // kumpulkan data ruang kosong
    if (wsCalN < WS_CAL_BUF) wsCalBuf[wsCalN] = wsScore;
    wsCalSum += wsScore; wsCalSq += wsScore * wsScore; wsCalN++;
  }
  float thr = wsThr();
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
  float bmax = wsUseSub ? 0.30f : 0.10f;               // noise per-subcarrier lebih besar dari rata-rata semua subcarrier
  if (b > bmax)  b = bmax;
  // wsBase dikalikan wsSensMul[] saat dibandingkan, jadi simpan sebagai dasar (bagi 3 = sens default)
  wsBase = b / 1.5f;
  { int mi = wsUseSub ? 1 : 0; int n = wsCalN < WS_CAL_BUF ? wsCalN : WS_CAL_BUF;   // WSENSE_V2: acuan adaptasi = persentil-25 skor kalibrasi
    for (int i = 1; i < n; i++) { float v = wsCalBuf[i]; int j = i - 1; while (j >= 0 && wsCalBuf[j] > v) { wsCalBuf[j + 1] = wsCalBuf[j]; j--; } wsCalBuf[j + 1] = v; }
    float q = wsCalBuf[n / 4]; if (q < 0.25f * m) q = 0.25f * m; if (q < 1e-6f) q = 1e-6f;
    wsQref[mi] = wsQ[mi] = q; wsAdaptMul = 1.0f; }
  wsCalSave();
  snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi tersimpan (dasar %.3f)", (double)wsBase);
}

// Goertzel: daya pada frekuensi f (Hz) untuk deret terdetrend+berjendela
static float wsGoertzel(const float* d, int n, float f) {
  float w = 2.0f * (float)M_PI * f / 10.0f;             // 10 Hz sampling
  float c = 2.0f * cosf(w), s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) { float s0 = d[i] + c * s1 - s2; s2 = s1; s1 = s0; }
  return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// detrend linear + jendela Hann pada d[] (sudah dalam satuan CV), kembalikan RMS-nya
static float wsPrep(float* d) {
  if (!wsHannOk) { for (int k = 0; k < WS_N; k++) wsHann[k] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * k / (WS_N - 1)); wsHannOk = true; }
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int k = 0; k < WS_N; k++) { sx += k; sy += d[k]; sxx += (float)k * k; sxy += k * d[k]; }
  float den = WS_N * sxx - sx * sx; float slope = den != 0 ? (WS_N * sxy - sx * sy) / den : 0;
  float icpt = (sy - slope * sx) / WS_N;
  float varsum = 0;
  for (int k = 0; k < WS_N; k++) { d[k] -= (icpt + slope * k); d[k] *= wsHann[k]; varsum += d[k] * d[k]; }
  return sqrtf(varsum / WS_N);
}

// WSENSE_V2: interpolasi parabola (pada amplitudo = sqrt daya) di sekitar puncak -> offset bin -0,5..+0,5
static float wsParab(const float* P, int b, int n) {
  if (b <= 0 || b >= n - 1) return 0;
  float y0 = sqrtf(P[b - 1]), y1 = sqrtf(P[b]), y2 = sqrtf(P[b + 1]);
  float den = y0 - 2.0f * y1 + y2; if (den >= -1e-12f) return 0;
  float d = 0.5f * (y0 - y2) / den; if (d > 0.5f) d = 0.5f; if (d < -0.5f) d = -0.5f;
  return d;
}

// hasil deteksi napas -> median 5 estimasi / peluruhan keyakinan
static void wsBrCommit(bool ok, int best, float frac) {
  if (ok) {
    float bpm = (0.10f + 0.01f * ((float)best + frac)) * 60.0f;
    wsBrRing[wsBrRingI] = bpm; wsBrRingI = (wsBrRingI + 1) % 5; if (wsBrRingN < 5) wsBrRingN++;
    float t[5]; for (int i = 0; i < wsBrRingN; i++) t[i] = wsBrRing[i];
    for (int i = 1; i < wsBrRingN; i++) { float v = t[i]; int j = i - 1; while (j >= 0 && t[j] > v) { t[j + 1] = t[j]; j--; } t[j + 1] = v; }
    wsBr = t[wsBrRingN / 2];
  } else {
    if (wsBrRingN > 0) wsBrRingN--;                     // keyakinan meluruh
    if (wsBrRingN == 0) wsBr = 0;
  }
}

static void wsBreathing() {
  if (wsCount < WS_N) return;                           // butuh 30 dtk penuh
  float* d = wsD;
  // urut kronologis lama -> baru, dibagi rata-rata (ke satuan CV), buang tren linear
  float mean = 0;
  for (int k = 0; k < WS_N; k++) { float v = wsAt(WS_N - 1 - k); d[k] = v; mean += v; }
  mean /= WS_N;
  if (mean < 1e-3f) return;
  for (int k = 0; k < WS_N; k++) d[k] = d[k] / mean - 1.0f;
  float rms = wsPrep(d);

  float P[WS_NF], tmp[WS_NF]; int best = 0;
  for (int i = 0; i < WS_NF; i++) {
    P[i] = wsGoertzel(d, WS_N, 0.10f + 0.01f * i); if (P[i] < 0) P[i] = 0;
    tmp[i] = P[i]; if (P[i] > P[best]) best = i;
  }
  for (int i = 1; i < WS_NF; i++) { float v = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = v; }
  float med = tmp[WS_NF / 2];
  wsBrRatio = P[best] / (med + 1e-12f);

  bool calm   = wsMovHist < 80;                         // hampir tidak ada gerak besar 30 dtk terakhir
  bool strong = rms > wsBaseS * 0.5f;                   // ada variasi di atas noise ruang kosong
  bool peak   = wsBrRatio >= 6.0f && best > 0 && best < WS_NF - 1;
  wsBrCommit(calm && strong && peak, best, wsParab(P, best, WS_NF));
}

// ---- napas multi-subcarrier: jumlahkan spektrum tiap subcarrier (tanpa fase) ----
static void wsBrFinishMulti() {
  int K = wsBrUsed;
  if (K < 8) { wsBreathing(); return; }                 // subcarrier hidup kurang -> jalur rata-rata
  float tmp[WS_NF]; int best = 0;
  for (int i = 0; i < WS_NF; i++) { tmp[i] = wsPsum[i]; if (wsPn[i] > wsPn[best]) best = i; }   // lokasi dari wsPn: batas 25x pada wsPsum membuat banyak bin seri
  for (int i = 1; i < WS_NF; i++) { float v = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = v; }
  float med = tmp[WS_NF / 2];
  wsBrRatio = wsPsum[best] / (med + 1e-12f);
  float keff = (float)K / 3.0f; if (keff < 1.0f) keff = 1.0f;   // subcarrier tetangga berkorelasi -> hitung 1/3 saja
  float thr = 1.0f + 5.0f / sqrtf(keff);                // K=1 -> 6 (sama dgn jalur rata-rata), K=52 -> ~2,2
  bool calm = wsMovHist < 80;
  bool peak = wsBrRatio >= thr && best > 0 && best < WS_NF - 1;
  wsBrCommit(calm && peak, best, wsParab(wsPn, best, WS_NF));
}

static void wsBrStartMulti() {
  for (int i = 0; i < WS_NF; i++) { wsPsum[i] = 0; wsPn[i] = 0; }
  wsBrUsed = 0; wsBrK = 0;
}

static void wsBreathingStep() {
  if (wsBrK < 0 || !wsSub) { wsBrK = -1; return; }
  int done = 0;
  while (wsBrK < WS_NSUB && done < WS_BR_SLICE) {
    int k = wsBrK++;
    if (wsSubMean[k] < WS_SUB_MIN_AMP) continue;
    done++;
    float mean = 0; int idx = wsHead;                   // buffer penuh: sampel terlama ada di wsHead
    for (int n = 0; n < WS_N; n++) {
      float v = (float)wsSub[(size_t)idx * WS_NSUB + k] * (1.0f / 64.0f);
      wsD[n] = v; mean += v; if (++idx >= WS_N) idx = 0;
    }
    mean /= WS_N;
    if (mean < WS_SUB_MIN_AMP) continue;
    for (int n = 0; n < WS_N; n++) wsD[n] = wsD[n] / mean - 1.0f;
    wsPrep(wsD);
    float P[WS_NF], tmp[WS_NF];
    for (int i = 0; i < WS_NF; i++) { P[i] = wsGoertzel(wsD, WS_N, 0.10f + 0.01f * i); if (P[i] < 0) P[i] = 0; tmp[i] = P[i]; }
    for (int i = 1; i < WS_NF; i++) { float v = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = v; }
    float med = tmp[WS_NF / 2] + 1e-12f, tot = 0;
    for (int i = 0; i < WS_NF; i++) tot += P[i];
    for (int i = 0; i < WS_NF; i++) { float r = P[i] / med; if (r > 25.0f) r = 25.0f; wsPsum[i] += r; wsPn[i] += P[i] / (tot + 1e-12f); }   // batas 25x: satu subcarrier tak mendominasi
    wsBrUsed++;
  }
  if (wsBrK >= WS_NSUB) { wsBrK = -1; wsBrFinishMulti(); }
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
    float sum; uint32_t cnt; uint32_t sc; float subTmp[WS_NSUB];
    portENTER_CRITICAL(&wsMux);
    sum = wsBinSum; cnt = wsBinCnt; wsBinSum = 0; wsBinCnt = 0;
    sc = wsSubCnt; memcpy(subTmp, wsSubSum, sizeof(subTmp)); memset(wsSubSum, 0, sizeof(wsSubSum)); wsSubCnt = 0;
    portEXIT_CRITICAL(&wsMux);
    float v = cnt ? sum / (float)cnt : wsLastBin;       // bin tanpa paket: ulangi nilai terakhir
    if (cnt) { if (sc) wsSubIdle = 0; else if (wsSubIdle < 1000) wsSubIdle++; }   // ada paket tapi tanpa LLTF
    if (v > 0) { if (wsSub) wsPushSub(subTmp, sc); wsPushBin(v); }
    if (now - wsBinT0 > 1000UL) wsBinT0 = now;          // tertinggal jauh (mis. layar berat) -> sinkron ulang
  }
  if (now - wsRateT0 >= 1000UL) {
    uint32_t tot; portENTER_CRITICAL(&wsMux); tot = wsPktTotal; portEXIT_CRITICAL(&wsMux);
    wsPktRate = (float)(tot - wsPktPrev) * 1000.0f / (float)(now - wsRateT0);
    wsPktPrev = tot; wsRateT0 = now;
  }
  if (wsCal && now - wsCalT0 >= WS_CAL_MS) wsFinishCal();
  if (now - wsBrT0 >= WS_BR_EVERY_MS) {
    wsBrT0 = now;
    if (!wsCal) {
      if (wsUseSub && wsSub) { if (wsCount >= WS_N && wsBrK < 0) wsBrStartMulti(); }   // dikerjakan bertahap di bawah
      else wsBreathing();
    }
  }
  if (wsBrK >= 0) wsBreathingStep();
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
  if (wsRun) s.setTextColor(wsPktRate >= 25.0f ? WS_RGB(48,209,88) : (wsPktRate >= 12.0f ? WS_RGB(255,214,10) : T().danger));   // WSENSE_V2: kualitas paket
  s.setCursor(12, iy); s.print(b);
  s.setTextColor(T().subtext);
  float thr = wsThr();
  snprintf(b, sizeof(b), "Gerak %.1fx", (double)(thr > 0 ? wsScore / thr : 0));
  s.setCursor((SCR_W - s.textWidth(b)) / 2, iy); s.print(b);          // WSENSE_TIDY_V1: tengah
  if (wsBr > 0) snprintf(b, sizeof(b), "Napas ~%.0f/mnt", (double)wsBr);
  else          snprintf(b, sizeof(b), "Napas --");
  s.setTextColor(wsBr > 0 ? WS_RGB(48,209,88) : T().subtext);
  s.setCursor(SCR_W - 12 - s.textWidth(b), iy); s.print(b);           // WSENSE_TIDY_V1: rata kanan

  // ---- grafik 15 dtk terakhir (simpangan dari rata-rata, auto-skala) ----
  int gx = 8, gy = iy + 14, gw = SCR_W - 16, gh = wsGraphBottom() - gy;
  if (gh < 30) gh = 30;
  drawGlassPanel(s, gx, gy, gw, gh, 10, T().surface2, 140);
  { char tb[16]; const char* tg;                        // label mode (ketuk utk ganti)
    if (wsUseSub) { snprintf(tb, sizeof(tb), "Multi %d", wsNValid); tg = tb; }
    else tg = wsMulti ? "Multi.." : "Rata";
    s.setTextSize(1); s.setTextColor(T().subtext);
    int tw = s.textWidth(tg); s.setCursor(gx + gw - tw - 8, gy + 8); s.print(tg);
    if (wsRun && fabsf(wsAdaptMul - 1.0f) > 0.15f) {    // WSENSE_V2: baseline adaptif sedang bergeser
      char ab[16]; snprintf(ab, sizeof(ab), "Adaptif x%.1f", (double)wsAdaptMul);
      s.setCursor(gx + 8, gy + 8); s.print(ab); } }
  int n = wsCount < WS_GRAPH_N ? wsCount : WS_GRAPH_N;
  if (n > 3) {
    float m = 0; for (int k = 0; k < n; k++) m += wsAt(k); m /= n;
    float mx = 0.01f * m;                               // lantai skala 1% dari rata-rata
    for (int k = 0; k < n; k++) { float a = fabsf(wsAt(k) - m); if (a > mx) mx = a; }
    int pTop = gy + 21, pBot = gy + gh - 18;                           // WSENSE_TIDY_V1: di antara strip atas & bawah
    if (pBot - pTop < 16) { pTop = gy + 4; pBot = gy + gh - 4; }       // grafik sangat pendek: pakai seluruh panel
    int mid = (pTop + pBot) / 2, half = (pBot - pTop) / 2 - 2;
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
  if (gh >= 70) {                                                      // WSENSE_TIDY_V1: garis pemisah strip atas & bawah
    s.drawFastHLine(gx + 8, gy + 19, gw - 16, T().divider);
    s.drawFastHLine(gx + 8, gy + gh - 17, gw - 16, T().divider);
  }
  { char sb[12]; snprintf(sb, sizeof(sb), "Sens %d/5", wsSens + 1);   // strip bawah kanan: sensitivitas
    int sw = s.textWidth(sb);
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(gx + gw - sw - 8, gy + gh - 12); s.print(sb);
    char mb[64]; mb[0] = 0; uint16_t mc = T().subtext;                 // strip bawah kiri: pesan (dipotong bila kepanjangan)
    if (wsCal) {
      int sec = (int)((WS_CAL_MS - (millis() - wsCalT0)) / 1000UL);
      snprintf(mb, sizeof(mb), "Kalibrasi... %d dtk (jangan ada orang)", sec < 0 ? 0 : sec);
      mc = WS_RGB(255,214,10);
    } else if (wsMsg[0] && wsRun) {
      snprintf(mb, sizeof(mb), "%s", wsMsg);
    }
    if (mb[0]) {
      int maxW = gw - 20 - sw - 8, l = (int)strlen(mb);
      while (l > 3 && s.textWidth(mb) > maxW) mb[--l] = 0;
      s.setTextColor(mc); s.setCursor(gx + 10, gy + gh - 12); s.print(mb);
    } }

  // ---- tombol ----
  wsBtn(s, 0, wsRun ? "Berhenti" : "Mulai", wsRun ? T().danger : T().accent, true);
  wsBtn(s, 1, "Kalibrasi", T().accent2, wsCal);
  wsBtn(s, 2, "-", T().accent, false);
  wsBtn(s, 3, "+", T().accent, false);
  // WSENSE_TIDY_V1: indikator 'Sens n/5' sekarang ada di strip bawah grafik

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
  { int tgy = 26 + 18 + 46 + 6 + 14;                    // = gy di drawWsense (jaga tetap sama)
    if (y >= tgy && y <= tgy + 20 && x > SCR_W - 100) {  // ganti mode multi <-> rata-rata
      wsMulti = !wsMulti; wsBrK = -1; wsBrRingN = 0; wsBr = 0; wsMovHist = 0;
      wsQ[0] = wsQref[0]; wsQ[1] = wsQref[1]; wsAdaptMul = 1.0f;   // WSENSE_V2
      snprintf(wsMsg, sizeof(wsMsg), wsMulti ? "Mode multi-subcarrier" : "Mode rata-rata");
      needRedraw = true; return;
    } }
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
    } else if (i == 2) { if (wsSens > 0) { wsSens--; wsCalSave(); } }
    else               { if (wsSens < 4) { wsSens++; wsCalSave(); } }
    needRedraw = true;
    return;
  }
}
