#!/usr/bin/env python3
"""WSENSE_V2 -- peningkatan app WiFi Sensing (firmware/phone/wifisense_renphone.ino) + tes host-nya:
  1. Filter paket CSI: hanya paket dari AP yang kita tumpangi (MAC sumber = BSSID) dan hanya SATU kelas PHY
     (11g/11n, 20/40 MHz). Paket dari pemancar/mode lain punya skala amplitudo berbeda -> loncatan palsu.
     Paket glitch (rata-rata amplitudo meloncat >40% sesaat) dibuang.
  2. Ping gateway 25 ms (bukan 50 ms) -> ~40 paket/dtk, tiap bin 100 ms dirata-rata dari ~4 paket (noise turun).
  3. Baseline ADAPTIF: persentil-25 skor gerak dilacak terus; ambang ikut naik/turun mengikuti noise ruangan
     (0,6x .. 3x dari hasil kalibrasi). Aktif hanya sesudah kalibrasi. Mengurangi 'Ada gerak' palsu saat ruangan jadi berisik.
  4. Kalibrasi + sensitivitas TERSIMPAN (NVS/SD lewat Preferences "wsense"), dipasangkan dengan BSSID router:
     tidak perlu kalibrasi ulang tiap membuka app; router lain -> tidak dipakai.
  5. Estimasi napas lebih halus: interpolasi parabola di sekitar puncak spektrum (bukan hanya kelipatan 0,6 napas/mnt).
  6. Indikator kualitas paket (warna hijau/kuning/merah) dan penanda 'Adaptif xN' di grafik.
Tes host (tests/wifisense) ikut diperbarui: stub CSI punya rx_ctrl+mac, mock Preferences/BSSID, kasus uji baru.
Jalankan dari ROOT repo:  python3 tools/patches/apply_wsense_v2.py   (idempoten)"""
import sys, os, shutil

INO = "firmware/phone/wifisense_renphone.ino"
if not os.path.exists(INO):
    sys.exit("Jalankan dari root repo (folder berisi firmware/phone/wifisense_renphone.ino)")

def load(p): return open(p, encoding="utf-8").read()
def save(p, t):
    shutil.copy(p, p + ".bak_wsv2"); open(p, "w", encoding="utf-8").write(t)

class Ed:
    def __init__(self, path): self.p = path; self.s = load(path)
    def rep(self, old, new, n=1):
        c = self.s.count(old)
        if c != n: sys.exit("GAGAL (%s): pola ditemukan %d kali (harus %d):\n  %s" % (self.p, c, n, old[:130]))
        self.s = self.s.replace(old, new)
    def done(self): save(self.p, self.s)

# ======================================================================= firmware
e = Ed(INO)
if "WSENSE_V2" in e.s:
    print("SKIP", INO, "(sudah terpasang)")
else:
    e.rep("//   2. Supaya paketnya rutin (~20/dtk, bukan cuma beacon ~10/dtk) HP mem-ping gateway\n//      tiap 50 ms (esp_ping).",
          "//   2. Supaya paketnya rutin (~40/dtk, bukan cuma beacon ~10/dtk) HP mem-ping gateway\n//      tiap 25 ms (esp_ping).")
    e.rep("// BATAS YANG JUJUR\n",
"""// PENINGKATAN WSENSE_V2
//   * Filter paket: hanya dari AP kita (MAC sumber = BSSID) dan satu kelas PHY (sig_mode + lebar kanal);
//     paket glitch (rata-rata amplitudo meloncat >40% sesaat) dibuang. Paket yg dibuang dihitung di wsDropTotal.
//   * Baseline adaptif: persentil-25 skor gerak dilacak terus (naik 0,15%/bin, turun 0,45%/bin = kuantil 25%),
//     ambang dikali rasio terhadap nilai saat kalibrasi (dibatasi 0,6x .. 3x). Hanya aktif sesudah kalibrasi.
//   * Kalibrasi & sensitivitas disimpan (Preferences \\"wsense\\", kunci BSSID router) dan dimuat saat app dibuka.
//   * Napas: interpolasi parabola di puncak spektrum -> bpm tidak lagi terkunci ke kelipatan 0,6.
//
// BATAS YANG JUJUR
""")
    e.rep("#define WS_PING_MS     50\n", "#define WS_PING_MS     25\n")

    e.rep("static WsState  wsState      = WS_OFF;\n",
"""static WsState  wsState      = WS_OFF;

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
static float    wsCalBuf[WS_CAL_BUF];
""")

    e.rep("#define wsBase (wsBaseRef())\n",
"""#define wsBase (wsBaseRef())
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
""")

    # ---- callback CSI
    e.rep("  if (!info || !info->buf || info->len < 16) return;\n  const int8_t* b = info->buf;\n",
"""  if (!info || !info->buf || info->len < 16) return;
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
""")
    e.rep("  if (cnt < 8) return;\n  portENTER_CRITICAL(&wsMux);\n  wsBinSum += sum / (float)cnt;\n",
"""  if (cnt < 8) return;
  const float pm = sum / (float)cnt;
  if (wsAmpEma > 0.0f && fabsf(pm / wsAmpEma - 1.0f) > 0.4f) {   // glitch: loncatan >40% sesaat
    if (++wsOutRun < 10) { wsDropTotal++; return; }
    wsAmpEma = pm;                                       // bertahan >=10 paket = perubahan nyata, terima
  }
  wsOutRun = 0;
  wsAmpEma = (wsAmpEma > 0.0f) ? wsAmpEma + (pm - wsAmpEma) * 0.02f : pm;
  portENTER_CRITICAL(&wsMux);
  wsBinSum += pm;
""")

    e.rep("  wsPktPrev = 0; wsPktRate = 0;\n}",
"""  wsPktPrev = 0; wsPktRate = 0;
  wsLockCls = 0xFF; wsRejRun = 0; wsOutRun = 0; wsAmpEma = 0;            // WSENSE_V2
  wsQ[0] = wsQref[0]; wsQ[1] = wsQref[1]; wsAdaptMul = 1.0f;
}""")

    e.rep("  wsResetSeries();\n  WiFi.setSleep(false);",
"""  { uint8_t* bs = WiFi.BSSID(); wsBssidOk = false;      // WSENSE_V2: kunci kalibrasi & filter paket ke AP ini
    if (bs) { for (int i = 0; i < 6; i++) if (bs[i]) wsBssidOk = true; if (wsBssidOk) memcpy(wsBssid, bs, 6); } }
  wsResetSeries();
  wsCalLoad();
  WiFi.setSleep(false);""")

    # ---- pemrosesan sinyal
    e.rep("    if (wsUseSub) wsScore = sm; }\n",
"""    if (wsUseSub) wsScore = sm; }
  { int mi = wsUseSub ? 1 : 0;                            // WSENSE_V2: baseline adaptif (kuantil-25 berjalan)
    if (wsScore > 0) {
      if (wsQ[mi] <= 0) wsQ[mi] = wsScore;
      else wsQ[mi] += wsQ[mi] * (wsScore < wsQ[mi] ? -WS_Q_DN : WS_Q_UP);
    }
    float am = 1.0f;
    if (wsQref[mi] > 0 && wsQ[mi] > 0) { am = wsQ[mi] / wsQref[mi]; if (am < WS_ADAPT_MIN) am = WS_ADAPT_MIN; if (am > WS_ADAPT_MAX) am = WS_ADAPT_MAX; }
    wsAdaptMul = am; }
""")
    e.rep("    wsCalSum += wsScore; wsCalSq += wsScore * wsScore; wsCalN++;\n",
          "    if (wsCalN < WS_CAL_BUF) wsCalBuf[wsCalN] = wsScore;\n    wsCalSum += wsScore; wsCalSq += wsScore * wsScore; wsCalN++;\n")
    e.rep("  float thr = wsBase * wsSensMul[wsSens];", "  float thr = wsThr();", 2)
    e.rep("  wsBase = b / 1.5f;\n",
"""  wsBase = b / 1.5f;
  { int mi = wsUseSub ? 1 : 0; int n = wsCalN < WS_CAL_BUF ? wsCalN : WS_CAL_BUF;   // WSENSE_V2: acuan adaptasi = persentil-25 skor kalibrasi
    for (int i = 1; i < n; i++) { float v = wsCalBuf[i]; int j = i - 1; while (j >= 0 && wsCalBuf[j] > v) { wsCalBuf[j + 1] = wsCalBuf[j]; j--; } wsCalBuf[j + 1] = v; }
    float q = wsCalBuf[n / 4]; if (q < 0.25f * m) q = 0.25f * m; if (q < 1e-6f) q = 1e-6f;
    wsQref[mi] = wsQ[mi] = q; wsAdaptMul = 1.0f; }
  wsCalSave();
""")
    e.rep('  snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi selesai (dasar %.3f)", (double)wsBase);',
          '  snprintf(wsMsg, sizeof(wsMsg), "Kalibrasi tersimpan (dasar %.3f)", (double)wsBase);')

    e.rep("// hasil deteksi napas -> median 5 estimasi / peluruhan keyakinan\n",
"""// WSENSE_V2: interpolasi parabola (pada amplitudo = sqrt daya) di sekitar puncak -> offset bin -0,5..+0,5
static float wsParab(const float* P, int b, int n) {
  if (b <= 0 || b >= n - 1) return 0;
  float y0 = sqrtf(P[b - 1]), y1 = sqrtf(P[b]), y2 = sqrtf(P[b + 1]);
  float den = y0 - 2.0f * y1 + y2; if (den >= -1e-12f) return 0;
  float d = 0.5f * (y0 - y2) / den; if (d > 0.5f) d = 0.5f; if (d < -0.5f) d = -0.5f;
  return d;
}

// hasil deteksi napas -> median 5 estimasi / peluruhan keyakinan
""")
    e.rep("static void wsBrCommit(bool ok, int best) {", "static void wsBrCommit(bool ok, int best, float frac) {")
    e.rep("    float bpm = (0.10f + 0.01f * best) * 60.0f;", "    float bpm = (0.10f + 0.01f * ((float)best + frac)) * 60.0f;")
    e.rep("  wsBrCommit(calm && strong && peak, best);", "  wsBrCommit(calm && strong && peak, best, wsParab(P, best, WS_NF));")
    e.rep("  wsBrCommit(calm && peak, best);", "  wsBrCommit(calm && peak, best, wsParab(wsPn, best, WS_NF));")

    # ---- gambar
    e.rep('  snprintf(b, sizeof(b), "Paket %.0f/dtk", (double)wsPktRate);\n  s.setCursor(12, iy); s.print(b);\n',
'''  snprintf(b, sizeof(b), "Paket %.0f/dtk", (double)wsPktRate);
  if (wsRun) s.setTextColor(wsPktRate >= 25.0f ? WS_RGB(48,209,88) : (wsPktRate >= 12.0f ? WS_RGB(255,214,10) : T().danger));   // WSENSE_V2: kualitas paket
  s.setCursor(12, iy); s.print(b);
  s.setTextColor(T().subtext);
''')
    e.rep("    int tw = s.textWidth(tg); s.setCursor(gx + gw - tw - 8, gy + 8); s.print(tg); }\n",
'''    int tw = s.textWidth(tg); s.setCursor(gx + gw - tw - 8, gy + 8); s.print(tg);
    if (wsRun && fabsf(wsAdaptMul - 1.0f) > 0.15f) {    // WSENSE_V2: baseline adaptif sedang bergeser
      char ab[16]; snprintf(ab, sizeof(ab), "Adaptif x%.1f", (double)wsAdaptMul);
      s.setCursor(gx + 8, gy + 8); s.print(ab); } }
''')

    # ---- sentuh
    e.rep("    } else if (i == 2) { if (wsSens > 0) wsSens--; }\n    else               { if (wsSens < 4) wsSens++; }\n",
          "    } else if (i == 2) { if (wsSens > 0) { wsSens--; wsCalSave(); } }\n    else               { if (wsSens < 4) { wsSens++; wsCalSave(); } }\n")
    e.rep("      wsMulti = !wsMulti; wsBrK = -1; wsBrRingN = 0; wsBr = 0; wsMovHist = 0;\n",
          "      wsMulti = !wsMulti; wsBrK = -1; wsBrRingN = 0; wsBr = 0; wsMovHist = 0;\n      wsQ[0] = wsQref[0]; wsQ[1] = wsQref[1]; wsAdaptMul = 1.0f;   // WSENSE_V2\n")
    e.done()
    print("OK:", INO, "(WSENSE_V2). Cadangan:", INO + ".bak_wsv2")

# ======================================================================= tes host
T = "tests/wifisense/"
st = Ed(T + "stub/esp_wifi.h")
if "wifi_pkt_rx_ctrl_t" in st.s:
    print("SKIP", st.p)
else:
    st.rep("struct wifi_csi_info_t { int8_t* buf; uint16_t len; };",
           "struct wifi_pkt_rx_ctrl_t { unsigned sig_mode; unsigned cwb; int rssi; };   // WSENSE_V2\n"
           "struct wifi_csi_info_t { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t mac[6]; int8_t* buf; uint16_t len; };")
    st.done(); print("OK:", st.p)

mk = Ed(T + "mock_env.h")
if "WSENSE_V2" in mk.s:
    print("SKIP", mk.p)
else:
    mk.rep("#include <algorithm>\n", "#include <algorithm>\n#include <map>\n#include <string>\n#include <vector>\n")
    mk.rep("struct WiFiMock { int st = WL_CONNECTED; int status() { return st; } void setSleep(bool) {} IPAddress gatewayIP() { return IPAddress(); } } WiFi;",
           "static uint8_t g_bssid[6] = {0x24,0x0a,0xc4,0x11,0x22,0x33};   // WSENSE_V2\n"
           "struct WiFiMock { int st = WL_CONNECTED; int status() { return st; } void setSleep(bool) {} IPAddress gatewayIP() { return IPAddress(); } uint8_t* BSSID() { return g_bssid; } } WiFi;\n"
           "// WSENSE_V2: Preferences tiruan (memori)\n"
           "struct Preferences {\n"
           "  static std::map<std::string, std::vector<uint8_t>>& store() { static std::map<std::string, std::vector<uint8_t>> m; return m; }\n"
           "  std::string ns;\n"
           "  bool begin(const char* n, bool = false) { ns = n; return true; }\n"
           "  void end() {}\n"
           "  size_t putBytes(const char* k, const void* v, size_t n) { store()[ns + \"/\" + k] = std::vector<uint8_t>((const uint8_t*)v, (const uint8_t*)v + n); return n; }\n"
           "  size_t getBytes(const char* k, void* b, size_t mx) { auto it = store().find(ns + \"/\" + k); if (it == store().end() || it->second.size() > mx) return 0; memcpy(b, it->second.data(), it->second.size()); return it->second.size(); }\n"
           "};")
    mk.done(); print("OK:", mk.p)

tt = Ed(T + "test_wsense.cpp")
if "WSENSE_V2" in tt.s:
    print("SKIP", tt.p)
else:
    # simulasi: paket asing / kelas PHY lain / glitch
    tt.rep("static SubSim ss; static float ssNoiseMul = 1.0f; static bool ssShort = false;\n",
           "static SubSim ss; static float ssNoiseMul = 1.0f; static bool ssShort = false;\n"
           "static int ssForeign = 0, ssClass = 0, ssGlitch = 0, ssPkt = 0;   // WSENSE_V2\n"
           "static const uint8_t kOtherMac[6] = {0x02,0x11,0x22,0x33,0x44,0x55};\n")
    tt.rep("    int8_t buf[128]; memset(buf, 0, sizeof(buf));\n",
           "    int8_t buf[128]; memset(buf, 0, sizeof(buf));\n"
           "    ssPkt++; float gain = 1.0f; const uint8_t* mac = g_bssid; unsigned mode = 1;   // WSENSE_V2\n"
           "    if (ssForeign && ssPkt % 3 == 0) { mac = kOtherMac; gain = 3.0f; }\n"
           "    if (ssClass && ssPkt % 4 == 1) { mode = 0; gain *= 1.3f; }\n"
           "    if (ssGlitch && ssPkt % 200 == 7) gain *= 4.0f;\n")
    tt.rep("      float re = amp * cosf(ss.ph[k]), im = amp * sinf(ss.ph[k]);",
           "      amp *= gain;\n      float re = amp * cosf(ss.ph[k]), im = amp * sinf(ss.ph[k]);")
    tt.rep("    wifi_csi_info_t info; info.buf = buf; info.len = ssShort ? 64 : 128;",
           "    wifi_csi_info_t info; memset(&info, 0, sizeof(info)); info.buf = buf; info.len = ssShort ? 64 : 128;\n"
           "    info.rx_ctrl.sig_mode = mode; memcpy(info.mac, mac, 6);")
    # H: hitung 2 menit terakhir (adaptasi)
    tt.rep("  { int moving = 0, still = 0, total = 0; static float t2 = 0;",
           "  { int moving = 0, still = 0, total = 0, lastMv = 0, lastTot = 0; static float t2 = 0;")
    tt.rep("if (i > 6000 && i % 100 == 0) { total++; if (wsState == WS_MOVING) moving++; if (wsState == WS_STILL) still++; } }",
           "if (i > 6000 && i % 100 == 0) { total++; if (wsState == WS_MOVING) moving++; if (wsState == WS_STILL) still++; }\n"
           "      if (i >= 18000 && i % 100 == 0) { lastTot++; if (wsState == WS_MOVING) lastMv++; } }")
    tt.rep('(perlu kalibrasi ulang kalau noise ruangan naik)\\n", moving, total, still, total); }',
           '(perlu kalibrasi ulang kalau noise ruangan naik)\\n", moving, total, still, total);\n'
           '    CHECK(lastMv * 10 <= lastTot, "WSENSE_V2 baseline adaptif: GERAK palsu di 2 menit terakhir %d/%d (<=10%%), adaptasi x%.2f", lastMv, lastTot, wsAdaptMul); }')
    # bagian baru sebelum ringkasan
    tt.rep('  printf("\\n%s (%d gagal)\\n", fails ? "ADA YANG GAGAL" : "SEMUA LULUS", fails);',
r'''  // ================= WSENSE_V2 =================
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

  printf("\n%s (%d gagal)\n", fails ? "ADA YANG GAGAL" : "SEMUA LULUS", fails);''')
    tt.done(); print("OK:", tt.p)

# ======================================================================= changelog
p = "CHANGELOG.md"
if os.path.exists(p):
    t = load(p); add = "- Improved: WiFi Sensing is more reliable and needs less babysitting. It now ignores CSI packets from other transmitters or PHY modes and drops glitch packets, pings the gateway every 25 ms for a steadier signal, adapts its motion threshold to the room's noise level after calibration, remembers the calibration and sensitivity per router, gives a finer breathing-rate estimate, and shows a packet-quality colour\n"
    if add not in t and "## Unreleased\n" in t:
        shutil.copy(p, p + ".bak_wsv2"); open(p, "w", encoding="utf-8").write(t.replace("## Unreleased\n", "## Unreleased\n" + add, 1)); print("OK:", p)
