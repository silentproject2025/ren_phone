// =====================================================================
// remote_renphone.ino -- REMOTE CONTROL untuk Accretion Phone / NyxOS
// =====================================================================
// HP ini memancarkan WiFi sendiri (Access Point, pakai sandi). Sambungkan
// HP/laptop lain ke WiFi itu, buka  http://192.168.4.1/remote  di browser,
// dan SELURUH antarmuka Accretion tampil di sana + bisa dikontrol:
//
//   * Layar live   : isi `canvas` (frame persis yang tampil di panel) dikirim
//                    sebagai TILE 16x16 yang berubah saja (delta) -> hemat
//                    bandwidth. UI yang diam hampir tidak makan data sama
//                    sekali. Mode "Hemat" = RGB332 (setengah ukuran).
//   * Sentuh       : ketuk / geser / tahan di layar web diinjeksi ke
//                    getInputPoint() -> SELURUH UI (Home, Lock Screen, CC,
//                    Notification Shade, App Switcher, semua app, game, dst)
//                    merespon persis seperti jari asli. Tidak ada kode UI
//                    yang diubah.
//   * Tombol       : Back, Home, Recents, Notifikasi, Control Center,
//                    Kunci/Buka kunci, Screenshot, slider kecerahan, daftar
//                    app (klik = buka), ketik teks ke kolom yang sedang
//                    fokus (lewat keyboard HP/laptop, bukan keyboard virtual).
//   * Pengaturan   : semua bisa diatur dari halaman web ATAU dari app
//                    "Remote" di layar HP (AP on/off, nama & sandi WiFi,
//                    mulai otomatis, mode lihat-saja, layar tetap nyala,
//                    kualitas gambar, batas FPS, izin lewat WiFi rumah).
//
// KEAMANAN
//   * AP pakai WPA2 + sandi acak 8 karakter yang dibuat saat pertama kali
//     (tampil di app Remote). Bisa diganti.
//   * Endpoint remote HANYA melayani koneksi yang masuk lewat AP. Lewat WiFi
//     rumah (STA) ditolak kecuali "Izinkan via WiFi rumah" dinyalakan.
//   * Satu penonton aktif sekaligus (yang kedua kena "sedang dipakai",
//     bisa ambil alih).
//
// ATURAN PROYEK YANG DIIKUTI (lihat komentar panjang di phone.ino):
//   - Semua fungsi publik / ber-prototype hanya menerima tipe primitif,
//     String, atau LGFX_Sprite& (hindari bug prototype otomatis arduino-cli).
//   - Buffer besar (3x ~150KB) di PSRAM, dialokasi SAAT penonton pertama
//     datang & dilepas 30 dtk setelah penonton hilang. RAM internal hanya
//     kepakai untuk AP + state kecil.
//   - AP default MATI (kecuali "Mulai otomatis" dinyalakan). Menyalakan AP
//     memakan RAM internal (netif + DHCP server) -- kalau fitur TLS (Gemini,
//     NASA, Trivia) mulai HTTP -1, matikan AP dulu. Angka heap sebelum/sesudah
//     AP dicetak ke Serial dengan awalan [remote].
//   - Layar yang digambar LANGSUNG ke panel (NES, DOOM, pemutar video) tidak
//     lewat `canvas`, jadi di web tampilannya beku & muncul penanda. Sentuh
//     tetap diteruskan.
//
// DIPASANG ke phone.ino lewat apply_remote_patch.py (idempoten):
//   enum Screen         <- SCR_REMOTE
//   apps[] + warna+ikon <- { "Remote", 'r', ... }
//   startWebServer()    <- rcRegisterRoutes()
//   loop()              <- rcLoopPoll(), remotePointerTick(), handleClient
//                          juga saat hanya AP yang hidup
//   getInputPoint()     <- rcInputPoll()  (injeksi sentuh)
//   push()              <- rcFrameMark()  (tanda frame baru)
// =====================================================================

#define RC_VERSION     "v1"
#define RC_TILE        16
#define RC_EV_MAX      64
#define RC_VIEW_TTL_MS 4000UL     // dianggap "ada penonton" kalau poll terakhir < ini
#define RC_FREE_MS     30000UL    // lepas buffer PSRAM kalau penonton hilang selama ini

struct RcCfg {
  bool autoStart;   // nyalakan AP otomatis beberapa detik setelah boot
  bool inputOn;     // izinkan sentuh/tombol dari web (false = hanya lihat)
  bool keepAwake;   // cegah AOD / redup selagi ada penonton
  bool allowLan;    // izinkan juga lewat WiFi rumah (STA), bukan cuma AP
  bool hidden;      // SSID disembunyikan
  uint8_t fpsCap;   // 4..30
  uint8_t quality;  // 0 = penuh (RGB565), 1 = hemat (RGB332)
  uint8_t channel;  // 1..11 (diabaikan radio kalau STA sedang tersambung)
  char ssid[33];
  char pass[33];
};
static RcCfg rcCfg;
static bool  rcCfgLoaded = false;

// ---- state AP / penonton ----
static bool          rcApUp = false;
static unsigned long rcLastViewMs = 0;
static char          rcClientId[16] = "";
static bool          rcWasViewing = false;
static unsigned long rcApRestartAt = 0;
static unsigned long rcKeepMs = 0, rcFreeCheckMs = 0, rcUiRefreshMs = 0, rcFpsMs = 0;
static bool          rcAutoTried = false;
static uint16_t      rcFramesThisSec = 0;
static uint8_t       rcFpsNow = 0;

// ---- antrean input sentuh ----
static uint8_t  rcEvT[RC_EV_MAX];
static int16_t  rcEvX[RC_EV_MAX], rcEvY[RC_EV_MAX];
static int      rcEvHead = 0, rcEvTail = 0;
static bool     rcDown = false;
static int      rcX = 0, rcY = 0;
static unsigned long rcNextEvMs = 0, rcLastEvMs = 0;

// ---- antrean aksi tombol (dieksekusi di rcLoopPoll) ----
static volatile uint8_t rcActId = 0;
static int      rcActArg = 0;

// ---- mesin frame ----
static uint16_t* rcRef = nullptr;     // yang terakhir dikirim ke penonton
static uint16_t* rcCur = nullptr;     // snapshot canvas saat ini (RGB565 baku)
static uint8_t*  rcOut = nullptr;     // payload
static size_t    rcOutCap = 0;
static int       rcW = 0, rcH = 0;
static int8_t    rcSwapMode = -2;     // -2 belum kalibrasi, -1 pakai readPixel, 0 native, 1 byte-swap
static bool      rcForceFull = true;
static uint16_t  rcSeq = 0;
static volatile uint32_t rcFrameSerial = 0;
static uint32_t  rcSeenSerial = 0;
static unsigned long rcLastBuildMs = 0;
static uint16_t  rcAdaptMs = 0;
static uint16_t  rcLastTiles = 0;

// =====================================================================
// KONFIGURASI (NVS namespace "rcfg")
// =====================================================================
static void rcGenPass(char* out, int n) {
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";   // tanpa i,l,o,0,1 (mudah salah baca)
  for (int i = 0; i < n; i++) out[i] = A[esp_random() % (sizeof(A) - 1)];
  out[n] = 0;
}

static void rcSaveCfg() {
  Preferences p; p.begin("rcfg", false);
  p.putBool("as", rcCfg.autoStart);
  p.putBool("in", rcCfg.inputOn);
  p.putBool("ka", rcCfg.keepAwake);
  p.putBool("lan", rcCfg.allowLan);
  p.putBool("hid", rcCfg.hidden);
  p.putUChar("fps", rcCfg.fpsCap);
  p.putUChar("q", rcCfg.quality);
  p.putUChar("ch", rcCfg.channel);
  p.putString("ssid", rcCfg.ssid);
  p.putString("pass", rcCfg.pass);
  p.end();
}

static void rcLoadCfg() {
  if (rcCfgLoaded) return;
  rcCfgLoaded = true;
  Preferences p; p.begin("rcfg", true);
  bool had = p.isKey("pass");
  rcCfg.autoStart = p.getBool("as", false);
  rcCfg.inputOn   = p.getBool("in", true);
  rcCfg.keepAwake = p.getBool("ka", true);
  rcCfg.allowLan  = p.getBool("lan", false);
  rcCfg.hidden    = p.getBool("hid", false);
  rcCfg.fpsCap    = p.getUChar("fps", 12);
  rcCfg.quality   = p.getUChar("q", 0);
  rcCfg.channel   = p.getUChar("ch", 6);
  String s = p.getString("ssid", "Accretion-Phone");
  String w = p.getString("pass", "");
  p.end();
  s.toCharArray(rcCfg.ssid, sizeof(rcCfg.ssid));
  w.toCharArray(rcCfg.pass, sizeof(rcCfg.pass));
  if (rcCfg.fpsCap < 4 || rcCfg.fpsCap > 30) rcCfg.fpsCap = 12;
  if (rcCfg.quality > 1) rcCfg.quality = 0;
  if (rcCfg.channel < 1 || rcCfg.channel > 11) rcCfg.channel = 6;
  if (strlen(rcCfg.ssid) < 1) strcpy(rcCfg.ssid, "Accretion-Phone");
  if (!had || strlen(rcCfg.pass) < 8) {       // pertama kali: sandi acak 8 karakter
    rcGenPass(rcCfg.pass, 8);
    rcSaveCfg();
  }
}

// =====================================================================
// ACCESS POINT
// =====================================================================
bool rcApIsUp() { return rcApUp; }

static bool rcViewerActive() {
  return rcLastViewMs != 0 && (millis() - rcLastViewMs) < RC_VIEW_TTL_MS;
}

static void rcFreeBuffers() {
  if (rcRef) heap_caps_free(rcRef);
  if (rcCur) heap_caps_free(rcCur);
  if (rcOut) heap_caps_free(rcOut);
  rcRef = nullptr; rcCur = nullptr; rcOut = nullptr;
  rcOutCap = 0; rcW = 0; rcH = 0; rcForceFull = true;
}

static bool rcApStart() {
  rcLoadCfg();
  if (rcApUp) return true;
  if (airplaneMode) { showToast("Matikan mode pesawat dulu"); return false; }
  Serial.printf("[remote] sebelum AP: freeInternal=%u largestBlock=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  WiFi.mode((wifiConnected || wifiConnecting) ? WIFI_AP_STA : WIFI_AP);
  bool ok = WiFi.softAP(rcCfg.ssid, rcCfg.pass, rcCfg.channel, rcCfg.hidden ? 1 : 0, 2);
  if (!ok) {
    Serial.println("[remote] WiFi.softAP GAGAL (sandi harus 8-63 karakter)");
    showToast("Gagal menyalakan AP");
    return false;
  }
  rcApUp = true;
  if (!webServerRunning) startWebServer();     // rute remote ikut terdaftar lewat rcRegisterRoutes()
  Serial.printf("[remote] AP aktif ssid=%s ip=%s | freeInternal=%u largestBlock=%u\n",
                rcCfg.ssid, WiFi.softAPIP().toString().c_str(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  needRedraw = true;
  return true;
}

static void rcApStop() {
  if (!rcApUp) return;
  WiFi.softAPdisconnect(true);
  rcApUp = false;
  rcLastViewMs = 0; rcClientId[0] = 0; rcWasViewing = false;
  rcEvHead = rcEvTail = 0; rcDown = false;
  rcFreeBuffers();
  Serial.printf("[remote] AP mati | freeInternal=%u largestBlock=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  needRedraw = true;
}

// Boleh diakses kalau koneksinya masuk lewat AP, atau "via WiFi rumah" diizinkan.
static bool rcAllowed() {
  rcLoadCfg();
  if (rcApUp) {
    IPAddress li = webServer.client().localIP();
    if (li == WiFi.softAPIP()) return true;
  }
  return rcCfg.allowLan;
}

// =====================================================================
// INJEKSI SENTUH  (dipanggil dari getInputPoint() di phone.ino)
// =====================================================================
static void rcEvPush(uint8_t t, int x, int y) {
  int nx = (rcEvTail + 1) % RC_EV_MAX;
  if (nx == rcEvHead) rcEvHead = (rcEvHead + 1) % RC_EV_MAX;   // penuh: buang yang tertua
  rcEvT[rcEvTail] = t; rcEvX[rcEvTail] = (int16_t)x; rcEvY[rcEvTail] = (int16_t)y;
  rcEvTail = nx;
  rcLastEvMs = millis();
}

// Format: "d120,80;m121,82;u"  (d=jari turun, m=geser, u=angkat)
static void rcParseEvents(const String& e) {
  int n = e.length(), i = 0, cnt = 0;
  while (i < n && cnt < 48) {
    char t = e[i];
    if (t == 'd' || t == 'm') {
      i++;
      int x = 0, y = 0;
      while (i < n && e[i] >= '0' && e[i] <= '9') { x = x * 10 + (e[i] - '0'); i++; }
      if (i < n && e[i] == ',') i++;
      while (i < n && e[i] >= '0' && e[i] <= '9') { y = y * 10 + (e[i] - '0'); i++; }
      if (x >= SCR_W) x = SCR_W - 1;
      if (y >= SCR_H) y = SCR_H - 1;
      rcEvPush((uint8_t)t, x, y);
      cnt++;
    } else if (t == 'u') {
      i++;
      rcEvPush('u', 0, 0);
      cnt++;
    } else {
      i++;
    }
    while (i < n && e[i] == ';') i++;
  }
}

// true selama jari "virtual" menempel. Satu event diproses per panggilan,
// dengan jeda minimum supaya loop() PASTI sempat melihat kondisi turun/naik
// (tap cepat d+u dalam satu paket tidak hilang).
bool rcInputPoll(int* ox, int* oy) {
  unsigned long now = millis();
  if (rcDown && (now - rcLastEvMs) > 5000UL) rcDown = false;   // klien hilang saat menekan
  if (rcEvHead != rcEvTail && now >= rcNextEvMs) {
    uint8_t t = rcEvT[rcEvHead]; int x = rcEvX[rcEvHead], y = rcEvY[rcEvHead];
    rcEvHead = (rcEvHead + 1) % RC_EV_MAX;
    if (t == 'm') {
      while (rcEvHead != rcEvTail && rcEvT[rcEvHead] == 'm') {   // gabung geseran berturut-turut
        x = rcEvX[rcEvHead]; y = rcEvY[rcEvHead];
        rcEvHead = (rcEvHead + 1) % RC_EV_MAX;
      }
      if (rcDown) { rcX = x; rcY = y; }
      rcNextEvMs = now;
    } else if (t == 'd') {
      rcDown = true; rcX = x; rcY = y;
      rcNextEvMs = now + 35;
    } else {
      rcDown = false;
      rcNextEvMs = now + 25;
    }
    rcLastEvMs = now;
  }
  if (!rcDown) return false;
  *ox = rcX; *oy = rcY;
  return true;
}

void rcFrameMark() { rcFrameSerial++; }

// =====================================================================
// MESIN FRAME
// =====================================================================
static bool rcDirectDraw() {
  Screen s = curScreen();
  if (s == SCR_NES && nesIsPlaying()) return true;
  if (s == SCR_DOOM && doomIsPlaying()) return true;
  if (mjpegPlaying) return true;
  return false;
}

static bool rcEnsureBuffers(int w, int h) {
  if (w <= 0 || h <= 0) return false;
  if (rcRef && rcCur && rcOut && w == rcW && h == rcH) return true;
  rcFreeBuffers();
  size_t px = (size_t)w * (size_t)h;
  int tiles = ((w + RC_TILE - 1) / RC_TILE) * ((h + RC_TILE - 1) / RC_TILE);
  rcOutCap = 16 + (size_t)tiles * (2 + RC_TILE * RC_TILE * 2);
  rcRef = (uint16_t*)heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  rcCur = (uint16_t*)heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  rcOut = (uint8_t*) heap_caps_malloc(rcOutCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rcRef || !rcCur || !rcOut) { rcFreeBuffers(); return false; }
  memset(rcRef, 0, px * 2);
  rcW = w; rcH = h;
  rcForceFull = true;
  rcSwapMode = -2;
  return true;
}

// Cari tahu urutan byte buffer sprite dengan membandingkannya ke readPixel()
// (jalur yang sudah terbukti dipakai screenshot). Kalau tidak meyakinkan,
// jatuh ke mode readPixel (lambat tapi pasti benar).
static void rcCalibrate() {
  const uint16_t* fb = (const uint16_t*)canvas.getBuffer();
  if (!fb) { rcSwapMode = -1; return; }
  int vN = 0, vS = 0, tested = 0;
  for (int k = 0; k < 160 && tested < 40; k++) {
    int x = (k * 37 + 11) % rcW, y = (k * 53 + 7) % rcH;
    uint16_t px  = (uint16_t)canvas.readPixel(x, y);
    uint16_t raw = fb[(size_t)y * rcW + x];
    uint16_t sw  = __builtin_bswap16(raw);
    if (px == raw && px != sw) { vN++; tested++; }
    else if (px == sw && px != raw) { vS++; tested++; }
  }
  if (vS > 0 && vN == 0)      rcSwapMode = 1;
  else if (vN > 0 && vS == 0) rcSwapMode = 0;
  else                        rcSwapMode = -1;
  Serial.printf("[remote] kalibrasi frame: mode=%d (native=%d swap=%d)\n", (int)rcSwapMode, vN, vS);
}

static void rcSnapshot() {
  size_t n = (size_t)rcW * (size_t)rcH;
  const uint16_t* fb = (const uint16_t*)canvas.getBuffer();
  if (rcSwapMode == 0 && fb) {
    memcpy(rcCur, fb, n * 2);
  } else if (rcSwapMode == 1 && fb) {
    for (size_t i = 0; i < n; i++) rcCur[i] = __builtin_bswap16(fb[i]);
  } else {
    for (int y = 0; y < rcH; y++)
      for (int x = 0; x < rcW; x++)
        rcCur[(size_t)y * rcW + x] = (uint16_t)canvas.readPixel(x, y);
  }
}

static inline uint8_t rcTo332(uint16_t c) {
  return (uint8_t)(((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 0x03));
}

// Susun payload: header 12 byte + tile berubah saja.
//  header : 'R' 'C' ver flags | w(u16) h(u16) | seq(u16) | nTiles(u16)   (little-endian)
//  flags  : bit0 penuh, bit1 hemat(RGB332), bit2 layar-langsung (beku)
//  tile   : u16 (idx | 0x4000 kalau warna tunggal) lalu 1 warna / tw*th piksel
static size_t rcBuildFrame(bool full, bool lite, bool direct) {
  const uint16_t mask = lite ? 0xE718 : 0xFFFF;
  uint8_t* o = rcOut;
  size_t p = 12;
  uint16_t nT = 0;
  int tx = (rcW + RC_TILE - 1) / RC_TILE, ty = (rcH + RC_TILE - 1) / RC_TILE;
  for (int j = 0; j < ty; j++) {
    for (int i = 0; i < tx; i++) {
      int x0 = i * RC_TILE, y0 = j * RC_TILE;
      int tw = min(RC_TILE, rcW - x0), th = min(RC_TILE, rcH - y0);
      bool changed = full;
      if (!changed) {
        for (int y = 0; y < th && !changed; y++) {
          const uint16_t* a = rcCur + (size_t)(y0 + y) * rcW + x0;
          const uint16_t* b = rcRef + (size_t)(y0 + y) * rcW + x0;
          for (int x = 0; x < tw; x++) if ((a[x] ^ b[x]) & mask) { changed = true; break; }
        }
      }
      if (!changed) continue;
      uint16_t c0 = rcCur[(size_t)y0 * rcW + x0];
      bool solid = true;
      for (int y = 0; y < th && solid; y++) {
        const uint16_t* a = rcCur + (size_t)(y0 + y) * rcW + x0;
        for (int x = 0; x < tw; x++) if ((a[x] ^ c0) & mask) { solid = false; break; }
      }
      uint16_t idx = (uint16_t)(j * tx + i);
      if (solid) {
        uint16_t v = idx | 0x4000;
        o[p++] = (uint8_t)(v & 0xFF); o[p++] = (uint8_t)(v >> 8);
        if (lite) o[p++] = rcTo332(c0);
        else { o[p++] = (uint8_t)(c0 & 0xFF); o[p++] = (uint8_t)(c0 >> 8); }
      } else {
        o[p++] = (uint8_t)(idx & 0xFF); o[p++] = (uint8_t)(idx >> 8);
        for (int y = 0; y < th; y++) {
          const uint16_t* a = rcCur + (size_t)(y0 + y) * rcW + x0;
          if (lite) { for (int x = 0; x < tw; x++) o[p++] = rcTo332(a[x]); }
          else      { for (int x = 0; x < tw; x++) { o[p++] = (uint8_t)(a[x] & 0xFF); o[p++] = (uint8_t)(a[x] >> 8); } }
        }
      }
      nT++;
    }
  }
  if (full || nT > 0) rcSeq++;
  o[0] = 'R'; o[1] = 'C'; o[2] = 1;
  o[3] = (uint8_t)((full ? 1 : 0) | (lite ? 2 : 0) | (direct ? 4 : 0));
  o[4] = (uint8_t)(rcW & 0xFF); o[5] = (uint8_t)(rcW >> 8);
  o[6] = (uint8_t)(rcH & 0xFF); o[7] = (uint8_t)(rcH >> 8);
  o[8] = (uint8_t)(rcSeq & 0xFF); o[9] = (uint8_t)(rcSeq >> 8);
  o[10] = (uint8_t)(nT & 0xFF); o[11] = (uint8_t)(nT >> 8);
  rcLastTiles = nT;
  return p;
}

static void rcSendBin(const uint8_t* buf, size_t len) {
  webServer.setContentLength(len);
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/octet-stream", "");
  size_t off = 0;
  while (off < len) {
    size_t n = len - off; if (n > 4096) n = 4096;
    webServer.sendContent((const char*)(buf + off), n);
    off += n;
  }
}

static void rcSendEmpty(bool lite, bool direct) {
  uint8_t h[12];
  h[0] = 'R'; h[1] = 'C'; h[2] = 1;
  h[3] = (uint8_t)((lite ? 2 : 0) | (direct ? 4 : 0));
  h[4] = (uint8_t)(rcW & 0xFF); h[5] = (uint8_t)(rcW >> 8);
  h[6] = (uint8_t)(rcH & 0xFF); h[7] = (uint8_t)(rcH >> 8);
  h[8] = (uint8_t)(rcSeq & 0xFF); h[9] = (uint8_t)(rcSeq >> 8);
  h[10] = 0; h[11] = 0;
  rcSendBin(h, 12);
}

// GET /rc/f?c=<id>&a=<seq terakhir>&full=1&tk=1&e=<event sentuh>
static void rcHandleFrame() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403"); return; }
  unsigned long now = millis();
  String cid = webServer.arg("c");
  bool take = webServer.hasArg("tk");
  bool sameClient = (strcmp(rcClientId, cid.c_str()) == 0);
  if (rcClientId[0] && !sameClient && (now - rcLastViewMs) < 6000UL && !take) {
    webServer.send(409, "text/plain", "busy");          // sedang dipakai perangkat lain
    return;
  }
  if (!sameClient) {
    strncpy(rcClientId, cid.c_str(), sizeof(rcClientId) - 1);
    rcClientId[sizeof(rcClientId) - 1] = 0;
    rcForceFull = true;
  }
  rcLastViewMs = now;

  if (rcCfg.inputOn && webServer.hasArg("e")) rcParseEvents(webServer.arg("e"));

  int w = canvas.width(), h = canvas.height();
  if (!rcEnsureBuffers(w, h)) { webServer.send(503, "text/plain", "ram"); return; }
  bool lite = (rcCfg.quality == 1);
  bool direct = rcDirectDraw();
  bool full = rcForceFull || webServer.hasArg("full") ||
              ((uint16_t)webServer.arg("a").toInt() != rcSeq);

  unsigned long minIv = 1000UL / (rcCfg.fpsCap ? rcCfg.fpsCap : 12);
  if (minIv < rcAdaptMs) minIv = rcAdaptMs;               // frame berat -> turunkan laju otomatis
  bool newFrame = (rcFrameSerial != rcSeenSerial) || ((now - rcLastBuildMs) > 400UL);
  bool due = full || (((now - rcLastBuildMs) >= minIv) && newFrame);
  if (!due) { rcSendEmpty(lite, direct); return; }

  if (rcSwapMode == -2) rcCalibrate();
  rcSnapshot();
  size_t len = rcBuildFrame(full, lite, direct);
  rcSeenSerial = rcFrameSerial;
  rcLastBuildMs = now;
  rcForceFull = false;
  uint16_t* t = rcRef; rcRef = rcCur; rcCur = t;           // ref = yang sekarang dipegang penonton
  rcSendBin(rcOut, len);
  unsigned long dur = millis() - now;
  rcAdaptMs = (uint16_t)(dur > 500 ? 500 : dur);
  rcFramesThisSec++;
}

// =====================================================================
// AKSI TOMBOL
// =====================================================================
static bool rcCloseOverlays() {
  bool any = false;
  if (appSwitcherOpen)  { closeAppSwitcherNow(); any = true; }
  if (controlCenterOpen){ controlCenterOpen = false; ccAnimating = false; ccOffset = 0; any = true; }
  if (notifShadeOpen)   { notifShadeOpen = false; nsAnimating = false; nsOffset = 0; any = true; }
  if (diMenuOpen)       { diMenuOpen = false; any = true; }
  if (any) needRedraw = true;
  return any;
}

#define RCA_HOME   1
#define RCA_BACK   2
#define RCA_REC    3
#define RCA_LOCK   4
#define RCA_UNLOCK 5
#define RCA_CC     6
#define RCA_SHADE  7
#define RCA_SHOT   8
#define RCA_APP    10

static void rcRunAct() {
  uint8_t id = rcActId;
  if (!id) return;
  rcActId = 0;
  int arg = rcActArg;
  aodWake(); homeAodWake();
  bool direct = rcDirectDraw();       // NES/DOOM pegang bus SPI -> hanya Home/Back yang aman
  switch (id) {
    case RCA_HOME:
      rcCloseOverlays();
      if (!locked) navGoHome();
      break;
    case RCA_BACK:
      if (rcCloseOverlays()) break;
      if (!locked && navDepth > 0) navBack();
      break;
    case RCA_REC:
      if (direct || locked) break;
      rcCloseOverlays();
      openAppSwitcher();
      break;
    case RCA_LOCK:
      if (direct) break;
      rcCloseOverlays();
      locked = true; lockScreenReset();
      needRedraw = true;
      break;
    case RCA_UNLOCK:
      if (locked) {
        locked = false; lockDragY = 0; lockDragging = false;
        navGoHome();
        diNotify('D', "Terbuka", T().accent, 1100, false);
      }
      break;
    case RCA_CC:
      if (direct || locked) break;
      if (controlCenterOpen) closeControlCenter();
      else { rcCloseOverlays(); openControlCenter(); }
      break;
    case RCA_SHADE:
      if (direct || locked) break;
      if (notifShadeOpen) closeNotifShade();
      else { rcCloseOverlays(); openNotifShade(); }
      break;
    case RCA_SHOT:
      if (direct || locked) break;
      ccActScreenshot();
      break;
    case RCA_APP: {
      if (direct) break;
      if (arg < 0 || arg >= APP_COUNT) break;
      if (apps[arg].screen == SCR_HOMEEDIT) break;
      rcCloseOverlays();
      if (locked) { locked = false; lockDragY = 0; lockDragging = false; }
      if (navDepth > 0) navGoHome();
      if (navDepth > 0) break;                       // mis. dialog "simpan Notepad?" menahan navigasi
      navPush(apps[arg].screen);
      break;
    }
  }
}

// GET /rc/a?k=<aksi>&v=<angka|teks>
static void rcHandleAct() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403"); return; }
  if (!rcCfg.inputOn) { webServer.send(200, "text/plain", "view"); return; }   // mode lihat-saja
  String k = webServer.arg("k");
  int v = webServer.arg("v").toInt();
  uint8_t id = 0;
  if      (k == "home")   id = RCA_HOME;
  else if (k == "back")   id = RCA_BACK;
  else if (k == "rec")    id = RCA_REC;
  else if (k == "lock")   id = RCA_LOCK;
  else if (k == "unlock") id = RCA_UNLOCK;
  else if (k == "cc")     id = RCA_CC;
  else if (k == "shade")  id = RCA_SHADE;
  else if (k == "shot")   id = RCA_SHOT;
  else if (k == "app")    id = RCA_APP;
  else if (k == "bright") {
    brightness = constrain(v, 10, 255);
    if (!aodActive && !homeAodActive) display.setBrightness((uint8_t)brightness);
    needRedraw = true;
    webServer.send(200, "text/plain", "ok");
    return;
  } else if (k == "type" || k == "bs") {
    if (!kbVisible || !kbTarget) { webServer.send(200, "text/plain", "nokb"); return; }
    if (k == "bs") {
      if (kbTarget->length() > 0) *kbTarget = kbTarget->substring(0, kbTarget->length() - 1);
    } else {
      String t = webServer.arg("v");
      for (unsigned i = 0; i < t.length() && i < 64; i++) {
        char c = t[i];
        if (c >= 32 && c < 127) *kbTarget += c;       // font bawaan cuma ASCII
      }
    }
    needRedraw = true;
    webServer.send(200, "text/plain", "ok");
    return;
  }
  if (!id) { webServer.send(400, "text/plain", "?"); return; }
  rcActArg = v;
  rcActId = id;                                      // dieksekusi di rcLoopPoll() setelah respons terkirim
  webServer.send(200, "text/plain", "ok");
}

// =====================================================================
// STATUS / PENGATURAN / DAFTAR APP  (JSON kecil)
// =====================================================================
static void rcJsonEsc(String& out, const char* s) {
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c >= 32) out += c;
  }
}

static void rcHandleStatus() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403"); return; }
  String j; j.reserve(300);
  j += "{\"w\":"; j += SCR_W; j += ",\"h\":"; j += SCR_H;
  j += ",\"ssid\":\""; rcJsonEsc(j, rcCfg.ssid); j += "\"";
  j += ",\"in\":"; j += rcCfg.inputOn ? 1 : 0;
  j += ",\"ka\":"; j += rcCfg.keepAwake ? 1 : 0;
  j += ",\"lan\":"; j += rcCfg.allowLan ? 1 : 0;
  j += ",\"as\":"; j += rcCfg.autoStart ? 1 : 0;
  j += ",\"hid\":"; j += rcCfg.hidden ? 1 : 0;
  j += ",\"q\":"; j += (int)rcCfg.quality;
  j += ",\"fps\":"; j += (int)rcCfg.fpsCap;
  j += ",\"ch\":"; j += (int)rcCfg.channel;
  j += ",\"b\":"; j += brightness;
  j += ",\"lk\":"; j += locked ? 1 : 0;
  j += ",\"kb\":"; j += (kbVisible && kbTarget) ? 1 : 0;
  j += ",\"v\":\"" RC_VERSION "\"}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", j);
}

static void rcHandleApps() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403"); return; }
  String j; j.reserve(900);
  j += "[";
  bool first = true;
  for (int i = 0; i < APP_COUNT; i++) {
    if (apps[i].screen == SCR_HOMEEDIT) continue;
    if (!first) j += ",";
    first = false;
    j += "["; j += i; j += ",\""; rcJsonEsc(j, apps[i].name); j += "\"]";
  }
  j += "]";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", j);
}

// GET /rc/set?in=1&ka=1&lan=0&as=1&fps=12&q=0&hid=0&ch=6&ssid=..&pass=..
static void rcHandleSet() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403"); return; }
  if (webServer.hasArg("pass")) {
    String v = webServer.arg("pass");
    if (v.length() < 8 || v.length() > 32) { webServer.send(400, "text/plain", "Sandi harus 8-32 karakter"); return; }
  }
  if (webServer.hasArg("ssid")) {
    String v = webServer.arg("ssid"); v.trim();
    if (v.length() < 1 || v.length() > 32) { webServer.send(400, "text/plain", "Nama WiFi 1-32 karakter"); return; }
  }
  bool credChg = false;
  if (webServer.hasArg("in"))  { rcCfg.inputOn   = webServer.arg("in")  == "1"; if (!rcCfg.inputOn) { rcEvHead = rcEvTail; rcDown = false; } }
  if (webServer.hasArg("ka"))  rcCfg.keepAwake = webServer.arg("ka")  == "1";
  if (webServer.hasArg("lan")) rcCfg.allowLan  = webServer.arg("lan") == "1";
  if (webServer.hasArg("as"))  rcCfg.autoStart = webServer.arg("as")  == "1";
  if (webServer.hasArg("fps")) rcCfg.fpsCap    = (uint8_t)constrain(webServer.arg("fps").toInt(), 4, 30);
  if (webServer.hasArg("q"))   { uint8_t q = webServer.arg("q").toInt() ? 1 : 0; if (q != rcCfg.quality) { rcCfg.quality = q; rcForceFull = true; } }
  if (webServer.hasArg("hid")) { bool hh = webServer.arg("hid") == "1"; if (hh != rcCfg.hidden) { rcCfg.hidden = hh; credChg = true; } }
  if (webServer.hasArg("ch"))  { uint8_t c = (uint8_t)constrain(webServer.arg("ch").toInt(), 1, 11); if (c != rcCfg.channel) { rcCfg.channel = c; credChg = true; } }
  if (webServer.hasArg("ssid")) {
    String v = webServer.arg("ssid"); v.trim();
    if (strcmp(rcCfg.ssid, v.c_str()) != 0) { v.toCharArray(rcCfg.ssid, sizeof(rcCfg.ssid)); credChg = true; }
  }
  if (webServer.hasArg("pass")) {
    String v = webServer.arg("pass");
    if (strcmp(rcCfg.pass, v.c_str()) != 0) { v.toCharArray(rcCfg.pass, sizeof(rcCfg.pass)); credChg = true; }
  }
  rcSaveCfg();
  if (credChg && rcApUp) rcApRestartAt = millis() + 900UL;     // AP restart: klien harus nyambung ulang
  needRedraw = true;
  webServer.send(200, "text/plain", credChg ? "restart" : "ok");
}

// =====================================================================
// HALAMAN WEB  (GET /remote atau /r)
// =====================================================================
static const char RC_PAGE[] = R"RCPAGE(<!DOCTYPE html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>Accretion Remote</title>
<style>
:root{--bg:#0a0c11;--sf:#151a23;--sf2:#1e2531;--tx:#e9edf5;--sb:#8c97ab;--ac:#8b7bff;--ok:#34d399;--no:#f87171}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--tx);font:14px system-ui,-apple-system,sans-serif;overscroll-behavior:none}
header{display:flex;align-items:center;gap:8px;padding:10px 12px;padding-top:max(10px,env(safe-area-inset-top))}
header b{font-size:15px;letter-spacing:.3px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--no)}.dot.on{background:var(--ok)}
.sp{flex:1}.sm{color:var(--sb);font-size:12px}
#wrap{position:relative;width:100%;max-width:720px;margin:0 auto;padding:0 10px}
#scr{display:block;width:100%;height:auto;background:#000;border-radius:12px;touch-action:none;user-select:none;-webkit-user-select:none}
#ov{position:absolute;left:10px;right:10px;top:0;display:flex;justify-content:center;pointer-events:none}
.tag{margin-top:8px;background:#000b;border:1px solid #fff2;border-radius:999px;padding:5px 12px;font-size:12px;backdrop-filter:blur(6px)}
#fd{position:absolute;width:26px;height:26px;margin:-13px 0 0 -13px;border-radius:50%;background:#8b7bff66;border:2px solid #fff9;pointer-events:none;display:none}
.row{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;padding:10px;max-width:720px;margin:0 auto}
button{font:inherit;color:var(--tx);background:var(--sf2);border:0;border-radius:12px;padding:12px 6px;cursor:pointer}
button:active{background:var(--ac)}
button.p{background:var(--ac)}
.pan{max-width:720px;margin:0 auto;padding:0 10px 14px}
.card{background:var(--sf);border-radius:14px;padding:12px;margin-bottom:10px}
.card h3{margin:0 0 8px;font-size:13px;color:var(--sb);font-weight:600;text-transform:uppercase;letter-spacing:.6px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(92px,1fr));gap:8px}
.grid button{font-size:12px;padding:10px 4px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.sw{display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px solid #fff1}
.sw:last-child{border:0}
.sw input[type=checkbox]{appearance:none;width:42px;height:26px;border-radius:13px;background:#3a4252;position:relative;outline:0;cursor:pointer;transition:.15s}
.sw input[type=checkbox]:checked{background:var(--ok)}
.sw input[type=checkbox]::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:.15s}
.sw input[type=checkbox]:checked::after{left:19px}
input[type=text],input[type=password],select{width:100%;font:inherit;color:var(--tx);background:var(--sf2);border:1px solid #fff1;border-radius:10px;padding:10px;margin:4px 0}
input[type=range]{width:100%}
.two{display:grid;grid-template-columns:1fr 1fr;gap:8px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#000d;padding:9px 16px;border-radius:999px;font-size:13px;display:none;z-index:9}
[hidden]{display:none!important}
</style></head><body>
<header><span class="dot" id="dot"></span><b>Accretion Remote</b><span class="sm" id="info">menyambung…</span><span class="sp"></span>
<button id="bfull" style="padding:6px 10px">⛶</button></header>
<div id="wrap"><canvas id="scr" width="320" height="240"></canvas><div id="fd"></div>
<div id="ov"><div class="tag" id="busy" hidden>Sedang dipakai perangkat lain · <a href="#" id="take" style="color:#8b7bff">ambil alih</a></div>
<div class="tag" id="direct" hidden>Layar ini digambar langsung ke panel (NES/DOOM/video) – tampilan beku, sentuh tetap jalan</div></div></div>
<div class="row">
<button data-a="back">◀ Back</button><button data-a="home" class="p">⌂ Home</button><button data-a="rec">▭ Recents</button><button data-a="shot">📷 Foto</button>
<button data-a="shade">🔔 Notif</button><button data-a="cc">⚙ Kontrol</button><button data-a="lock">🔒 Kunci</button><button data-a="unlock">🔓 Buka</button></div>
<div class="pan">
<div class="card"><h3>Kecerahan</h3><input type="range" id="br" min="10" max="255" value="200"></div>
<div class="card"><h3>Ketik ke kolom aktif</h3><div class="two" style="grid-template-columns:1fr auto auto"><input type="text" id="txt" placeholder="ketik lalu Kirim (keyboard di HP harus terbuka)" autocomplete="off"><button id="tsend">Kirim</button><button id="tbs">⌫</button></div></div>
<div class="card"><h3>Aplikasi</h3><div class="grid" id="apps"></div></div>
<div class="card"><h3>Pengaturan remote</h3>
<label class="sw">Izinkan sentuh &amp; tombol<input type="checkbox" id="s_in"></label>
<label class="sw">Layar HP tetap nyala selagi dipakai<input type="checkbox" id="s_ka"></label>
<label class="sw">Mulai otomatis saat HP boot<input type="checkbox" id="s_as"></label>
<label class="sw">Izinkan lewat WiFi rumah<input type="checkbox" id="s_lan"></label>
<label class="sw">Titik sentuh (di layar web ini)<input type="checkbox" id="s_cu" checked></label>
<div class="two"><div><div class="sm">Kualitas gambar</div><select id="s_q"><option value="0">Penuh (RGB565)</option><option value="1">Hemat (RGB332)</option></select></div>
<div><div class="sm">Batas FPS</div><select id="s_fps"><option>6</option><option>8</option><option>12</option><option>20</option><option>30</option></select></div></div></div>
<div class="card"><h3>WiFi remote (AP)</h3>
<div class="sm">Nama WiFi</div><input type="text" id="s_ssid" maxlength="32" autocomplete="off">
<div class="sm">Sandi baru (8–32 karakter, kosongkan jika tidak diganti)</div><input type="password" id="s_pass" maxlength="32" autocomplete="new-password">
<button id="sap" style="width:100%;margin-top:6px">Simpan &amp; restart AP</button>
<div class="sm" style="margin-top:6px">Setelah restart, sambungkan ulang ke WiFi itu lalu muat ulang halaman.</div></div>
<div class="sm" style="text-align:center;padding:4px 0 24px" id="ver"></div></div>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s),cv=$('#scr'),ctx=cv.getContext('2d');
let W=320,H=240,img=ctx.createImageData(W,H),seq=65535,lite=false,need=true,take=false,q=[],down=false,wk=null,ST={};
const cid=Math.random().toString(36).slice(2,10);
const R3=[0,36,73,109,146,182,219,255],B2=[0,85,170,255];
const nap=ms=>new Promise(r=>{wk=r;setTimeout(r,ms)}),wake=()=>{if(wk){wk();wk=null}};
function toast(t){const e=$('#toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',1800)}
function blank(){img=ctx.createImageData(W,H);for(let i=3;i<img.data.length;i+=4)img.data[i]=255}
function setSize(w,h){W=w;H=h;cv.width=w;cv.height=h;blank()}
blank();
function px(d,o,b,p){
  if(lite){const v=b[p];d[o]=R3[v>>5];d[o+1]=R3[(v>>2)&7];d[o+2]=B2[v&3];return p+1}
  const c=b[p]|(b[p+1]<<8);d[o]=((c>>8)&248)|(c>>13);d[o+1]=((c>>3)&252)|((c>>9)&3);d[o+2]=((c<<3)&248)|((c>>2)&7);return p+2}
function apply(b){
  if(b.length<12||b[0]!=82||b[1]!=67)return false;
  const dv=new DataView(b.buffer,b.byteOffset,b.byteLength);
  const fl=b[3],w=dv.getUint16(4,true),h=dv.getUint16(6,true),n=dv.getUint16(10,true);
  seq=dv.getUint16(8,true);lite=!!(fl&2);$('#direct').hidden=!(fl&4);
  if(w!=W||h!=H)setSize(w,h);
  const d=img.data,tx=Math.ceil(W/16),t=new Uint8ClampedArray(4);let p=12;
  for(let k=0;k<n;k++){
    const v=b[p]|(b[p+1]<<8);p+=2;
    const i=v&0x3fff,x0=(i%tx)*16,y0=((i/tx)|0)*16,tw=Math.min(16,W-x0),th=Math.min(16,H-y0);
    if(v&0x4000){p=px(t,0,b,p);
      for(let y=0;y<th;y++){let o=((y0+y)*W+x0)*4;for(let x=0;x<tw;x++,o+=4){d[o]=t[0];d[o+1]=t[1];d[o+2]=t[2]}}}
    else for(let y=0;y<th;y++){let o=((y0+y)*W+x0)*4;for(let x=0;x<tw;x++,o+=4)p=px(d,o,b,p)}
  }
  if(n>0)ctx.putImageData(img,0,0);
  return n>0}
let frames=0,tf=performance.now();
function setOn(on,t){$('#dot').className='dot'+(on?' on':'');if(t)$('#info').textContent=t}
async function loop(){
  let fails=0,idle=0;
  for(;;){
    const ev=q.splice(0,32);
    const u='/rc/f?c='+cid+'&a='+seq+(need?'&full=1':'')+(take?'&tk=1':'')+(ev.length?'&e='+encodeURIComponent(ev.join(';')):'');
    need=false;take=false;
    try{
      const r=await fetch(u,{cache:'no-store'});
      if(r.status==409){$('#busy').hidden=false;setOn(false,'dipakai lain');need=true;await nap(1200);continue}
      if(r.status==403){setOn(false,'akses ditolak');await nap(3000);continue}
      if(!r.ok)throw 0;
      $('#busy').hidden=true;
      const ch=apply(new Uint8Array(await r.arrayBuffer()));
      fails=0;if(ch){frames++;idle=0}else idle++;
      const t=performance.now();if(t-tf>1000){setOn(true,Math.round(frames*1000/(t-tf))+' fps · '+W+'×'+H+(lite?' · hemat':''));frames=0;tf=t}
      if(!ch&&!q.length)await nap(idle>3?160:30);
    }catch(e){fails++;setOn(false,'terputus…');need=true;if(down)q.push('u');await nap(Math.min(2000,300*fails))}
  }}
$('#take').onclick=e=>{e.preventDefault();take=true;wake()};
function pos(e){const r=cv.getBoundingClientRect();
  return[Math.max(0,Math.min(W-1,Math.round((e.clientX-r.left)*W/r.width))),Math.max(0,Math.min(H-1,Math.round((e.clientY-r.top)*H/r.height)))]}
const fd=$('#fd');
function dot(e){if(!$('#s_cu').checked){fd.style.display='none';return}const r=document.getElementById('wrap').getBoundingClientRect();fd.style.display='block';fd.style.left=(e.clientX-r.left)+'px';fd.style.top=(e.clientY-r.top)+'px'}
let hb=0;
cv.addEventListener('pointerdown',e=>{if(!e.isPrimary)return;e.preventDefault();cv.setPointerCapture(e.pointerId);down=true;const[x,y]=pos(e);q.push('d'+x+','+y);dot(e);wake();clearInterval(hb);hb=setInterval(()=>{if(down)q.push('m'+pos(lastE).join(','))},1500)});
let lastE=null,lastM=0;
cv.addEventListener('pointermove',e=>{if(!down||!e.isPrimary)return;lastE=e;dot(e);const t=performance.now();if(t-lastM<28)return;lastM=t;const[x,y]=pos(e);const m='m'+x+','+y;if(q.length&&q[q.length-1][0]=='m')q[q.length-1]=m;else q.push(m);wake()});
function up(e){if(!down||!e.isPrimary)return;down=false;clearInterval(hb);const[x,y]=pos(e);if(q.length&&q[q.length-1][0]=='m')q[q.length-1]='m'+x+','+y;else q.push('m'+x+','+y);q.push('u');fd.style.display='none';wake()}
cv.addEventListener('pointerup',up);cv.addEventListener('pointercancel',up);
cv.addEventListener('pointerdown',e=>{lastE=e});
cv.addEventListener('contextmenu',e=>e.preventDefault());
async function act(k,v){try{const r=await fetch('/rc/a?k='+k+(v!==undefined?'&v='+encodeURIComponent(v):''));const t=await r.text();
  if(t=='view')toast('Mode lihat-saja (aktifkan "Izinkan sentuh")');else if(t=='nokb')toast('Tidak ada kolom teks yang fokus di HP');wake()}catch(e){toast('Gagal kirim')}}
document.querySelectorAll('[data-a]').forEach(b=>b.onclick=()=>act(b.dataset.a));
let bt=0;$('#br').oninput=e=>{clearTimeout(bt);bt=setTimeout(()=>act('bright',e.target.value),120)};
$('#tsend').onclick=()=>{const v=$('#txt').value;if(v){act('type',v);$('#txt').value=''}};
$('#txt').onkeydown=e=>{if(e.key=='Enter')$('#tsend').click()};
$('#tbs').onclick=()=>act('bs');
$('#bfull').onclick=()=>{const w=$('#wrap');if(document.fullscreenElement)document.exitFullscreen();else if(w.requestFullscreen)w.requestFullscreen()};
async function set(o){const p=new URLSearchParams(o);try{const r=await fetch('/rc/set?'+p);const t=await r.text();
  if(r.status==400){toast(t);return false}if(t=='restart')toast('AP restart… sambungkan ulang WiFi-nya');else toast('Tersimpan');return true}catch(e){toast('Gagal');return false}}
const bind={s_in:'in',s_ka:'ka',s_as:'as',s_lan:'lan'};
for(const id in bind)$('#'+id).onchange=e=>set({[bind[id]]:e.target.checked?1:0});
$('#s_q').onchange=e=>{set({q:e.target.value});need=true};
$('#s_fps').onchange=e=>set({fps:e.target.value});
$('#sap').onclick=async()=>{const o={ssid:$('#s_ssid').value};const pw=$('#s_pass').value;if(pw)o.pass=pw;if(await set(o))$('#s_pass').value=''};
async function init(){
  try{ST=await (await fetch('/rc/st')).json();
    $('#s_in').checked=!!ST.in;$('#s_ka').checked=!!ST.ka;$('#s_as').checked=!!ST.as;$('#s_lan').checked=!!ST.lan;
    $('#s_q').value=ST.q;$('#s_fps').value=ST.fps;$('#s_ssid').value=ST.ssid;$('#br').value=ST.b;$('#ver').textContent='NyxOS Remote '+ST.v;
    const a=await (await fetch('/rc/apps')).json(),g=$('#apps');
    a.forEach(([i,n])=>{const b=document.createElement('button');b.textContent=n;b.onclick=()=>act('app',i);g.appendChild(b)})}catch(e){setTimeout(init,1500)}}
init();loop();
</script></body></html>)RCPAGE";

static void rcHandlePage() {
  if (!rcAllowed()) { webServer.send(403, "text/plain", "403 - buka lewat WiFi Accretion (AP), atau nyalakan 'Izinkan via WiFi rumah'"); return; }
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send_P(200, "text/html; charset=utf-8", RC_PAGE);
}

void rcRegisterRoutes() {
  rcLoadCfg();
  webServer.on("/remote",  HTTP_GET, rcHandlePage);
  webServer.on("/r",       HTTP_GET, rcHandlePage);
  webServer.on("/rc/f",    HTTP_GET, rcHandleFrame);
  webServer.on("/rc/a",    HTTP_GET, rcHandleAct);
  webServer.on("/rc/st",   HTTP_GET, rcHandleStatus);
  webServer.on("/rc/set",  HTTP_GET, rcHandleSet);
  webServer.on("/rc/apps", HTTP_GET, rcHandleApps);
}

// =====================================================================
// POLL TIAP loop()  (dipanggil dari phone.ino persis setelah handleClient)
// =====================================================================
void rcLoopPoll() {
  unsigned long now = millis();

  // mulai otomatis
  if (!rcAutoTried && now > 8000UL) {
    rcAutoTried = true;
    rcLoadCfg();
    if (rcCfg.autoStart && !airplaneMode) rcApStart();
  }
  // Remote aktif kalau AP hidup, ATAU (opsional) lewat WiFi rumah yang diizinkan.
  bool lanOk = rcCfg.allowLan && wifiConnected;
  if (!rcApUp && !lanOk) { if (rcActId) rcActId = 0; return; }

  if (rcApUp && airplaneMode) { rcApStop(); return; }

  // restart AP setelah nama/sandi diubah (tunggu respons HTTP sempat terkirim)
  if (rcApUp && rcApRestartAt && now >= rcApRestartAt) {
    rcApRestartAt = 0;
    rcApStop();
    rcApStart();
    return;
  }

  rcRunAct();

  // penonton datang / pergi -> pil Dynamic Island + segarkan layar app Remote
  bool v = rcViewerActive();
  if (v != rcWasViewing) {
    rcWasViewing = v;
    if (v) diNotify('I', "Remote tersambung", T().good, 1500, false);
    else   diNotify('I', "Remote terputus",   T().subtext, 1300, false);
    needRedraw = true;
  }

  // fps untuk ditampilkan
  if (now - rcFpsMs >= 1000UL) {
    rcFpsMs = now; rcFpsNow = (uint8_t)min((int)rcFramesThisSec, 99); rcFramesThisSec = 0;
  }

  // layar tetap nyala selagi dipakai
  if (rcCfg.keepAwake && v && now - rcKeepMs > 1000UL) {
    rcKeepMs = now;
    if (locked) aodWake(); else homeAodWake();
  }

  // lepas buffer PSRAM kalau penonton sudah lama pergi
  if (rcRef && now - rcFreeCheckMs > 2000UL) {
    rcFreeCheckMs = now;
    if (rcLastViewMs && now - rcLastViewMs > RC_FREE_MS) { rcFreeBuffers(); rcClientId[0] = 0; rcLastViewMs = 0; }
  }

  if (curScreen() == SCR_REMOTE && now - rcUiRefreshMs > 1000UL) { rcUiRefreshMs = now; needRedraw = true; }
}

// =====================================================================
// APP "REMOTE" DI LAYAR HP (SCR_REMOTE)
// =====================================================================
#define RCU_ROW_H 26
#define RCU_N     12

static float rcuScroll = 0;
static bool  rcuReveal = false;
static bool  rcuDown = false, rcuMoved = false;
static int   rcuSY = 0, rcuLY = 0;

static int rcuTop()    { return 26 + 18; }
static int rcuBottom() { return SCR_H - BACK_H - 3 - 6; }
static int rcuMaxScroll() {
  int m = RCU_N * RCU_ROW_H - (rcuBottom() - rcuTop());
  return m > 0 ? m : 0;
}
static void rcuClamp() {
  int m = rcuMaxScroll();
  if (rcuScroll < 0) rcuScroll = 0;
  if (rcuScroll > m) rcuScroll = (float)m;
}

void remoteEnter() { rcLoadCfg(); rcuScroll = 0; rcuReveal = false; rcuDown = false; }
void remoteExit()  { rcuDown = false; rcuReveal = false; }

static const char* const RCU_LBL[RCU_N] = {
  "Status", "Access Point", "Nama WiFi", "Sandi", "Alamat", "Buat sandi baru",
  "Mulai otomatis", "Izinkan sentuh dari web", "Layar tetap nyala", "Izinkan via WiFi rumah",
  "Kualitas gambar", "Batas FPS"
};

void drawRemote(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Remote Control");

  int top = rcuTop(), bot = rcuBottom();
  s.setClipRect(0, top, SCR_W, bot - top);
  s.setTextSize(1);
  int xl = 12, xr = SCR_W - 12;
  int valX = SCR_W * 2 / 5; if (valX < 112) valX = 112;
  int maxc = (xr - valX) / 6; if (maxc < 4) maxc = 4;
  char buf[48];

  for (int i = 0; i < RCU_N; i++) {
    int y = top - (int)rcuScroll + i * RCU_ROW_H;
    if (y + RCU_ROW_H < top || y > bot) continue;
    int ty = y + 9;
    s.setTextColor(T().text);
    s.setCursor(xl, ty); s.print(RCU_LBL[i]);

    switch (i) {
      case 0: {
        if (!rcApUp) { s.setTextColor(T().danger); snprintf(buf, sizeof(buf), "AP mati"); }
        else if (rcWasViewing) { s.setTextColor(T().good); snprintf(buf, sizeof(buf), "Terhubung %ufps", (unsigned)rcFpsNow); }
        else { s.setTextColor(T().good); snprintf(buf, sizeof(buf), "AP aktif"); }
        s.setCursor(valX, ty); s.print(buf);
        break;
      }
      case 2: {
        s.setTextColor(T().subtext);
        snprintf(buf, sizeof(buf), "%.*s", maxc, rcCfg.ssid);
        s.setCursor(valX, ty); s.print(buf);
        break;
      }
      case 3: {
        s.setTextColor(rcuReveal ? T().accent : T().subtext);
        if (rcuReveal) snprintf(buf, sizeof(buf), "%.*s", maxc, rcCfg.pass);
        else snprintf(buf, sizeof(buf), "******** (ketuk)");
        s.setCursor(valX, ty); s.print(buf);
        break;
      }
      case 4: {
        s.setTextColor(T().subtext);
        if (rcApUp) {
          IPAddress ip = WiFi.softAPIP();
          snprintf(buf, sizeof(buf), "%u.%u.%u.%u/remote", ip[0], ip[1], ip[2], ip[3]);
        } else snprintf(buf, sizeof(buf), "-");
        s.setCursor(valX, ty); s.print(buf);
        break;
      }
      case 5: iosChevron(s, xr - 4, ty + 4, T().subtext); break;
      case 1: iosSwitch(s, xr - 34, y + 3, rcApUp); break;
      case 6: iosSwitch(s, xr - 34, y + 3, rcCfg.autoStart); break;
      case 7: iosSwitch(s, xr - 34, y + 3, rcCfg.inputOn); break;
      case 8: iosSwitch(s, xr - 34, y + 3, rcCfg.keepAwake); break;
      case 9: iosSwitch(s, xr - 34, y + 3, rcCfg.allowLan); break;
      case 10: {
        s.setTextColor(T().accent);
        s.setCursor(valX, ty); s.print(rcCfg.quality ? "Hemat (RGB332)" : "Penuh (RGB565)");
        break;
      }
      case 11: {
        s.setTextColor(T().accent);
        snprintf(buf, sizeof(buf), "%u fps", (unsigned)rcCfg.fpsCap);
        s.setCursor(valX, ty); s.print(buf);
        break;
      }
    }
    if (i < RCU_N - 1) s.drawFastHLine(xl, y + RCU_ROW_H - 1, SCR_W - 2 * xl, T().divider);
  }
  s.clearClipRect();

  int maxS = rcuMaxScroll();
  if (maxS > 0) {
    int trackH = bot - top;
    int thumbH = trackH * trackH / (trackH + maxS); if (thumbH < 12) thumbH = 12;
    int thumbY = top + (int)((trackH - thumbH) * (rcuScroll / (float)maxS));
    s.fillRoundRect(SCR_W - 4, thumbY, 2, thumbH, 1, blend565(T().bg, T().text, 90));
  }

  drawBack(s);
  drawToast(s);
}

// Tombol Kembali saja; tap baris & geser daftar ditangani remotePointerTick().
void remoteTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (isNew && isBack(x, y)) navBack();
}

static void rcuRowTap(int i) {
  switch (i) {
    case 1:
      if (rcApUp) rcApStop(); else rcApStart();
      break;
    case 3:
      rcuReveal = !rcuReveal;
      break;
    case 5:
      rcGenPass(rcCfg.pass, 8);
      rcSaveCfg();
      rcuReveal = true;
      if (rcApUp) { rcApStop(); rcApStart(); }
      showToast("Sandi baru dibuat");
      break;
    case 6: rcCfg.autoStart = !rcCfg.autoStart; rcSaveCfg(); break;
    case 7:
      rcCfg.inputOn = !rcCfg.inputOn;
      if (!rcCfg.inputOn) { rcEvHead = rcEvTail; rcDown = false; }
      rcSaveCfg();
      break;
    case 8: rcCfg.keepAwake = !rcCfg.keepAwake; rcSaveCfg(); break;
    case 9: rcCfg.allowLan = !rcCfg.allowLan; rcSaveCfg(); break;
    case 10: rcCfg.quality = rcCfg.quality ? 0 : 1; rcForceFull = true; rcSaveCfg(); break;
    case 11: {
      static const uint8_t F[5] = {6, 8, 12, 20, 30};
      int k = 0;
      for (int n = 0; n < 5; n++) if (F[n] == rcCfg.fpsCap) k = n;
      rcCfg.fpsCap = F[(k + 1) % 5];
      rcSaveCfg();
      break;
    }
    default: return;
  }
  needRedraw = true;
}

// Dipanggil tiap frame dari loop() selagi SCR_REMOTE aktif (pola sama dgn
// settingsPointerTick): geser = scroll, ketuk saat jari lepas = aksi baris.
void remotePointerTick(bool touched, bool wasTouched, int tx, int ty) {
  if (touched && !wasTouched) {
    rcuDown = (ty >= rcuTop() && ty <= rcuBottom());
    rcuMoved = false; rcuSY = ty; rcuLY = ty;
    return;
  }
  if (touched && rcuDown) {
    if (!rcuMoved && abs(ty - rcuSY) > 6) rcuMoved = true;
    if (rcuMoved) {
      rcuScroll += (float)(rcuLY - ty);
      rcuClamp();
      needRedraw = true;
    }
    rcuLY = ty;
    return;
  }
  if (!touched && wasTouched && rcuDown) {
    rcuDown = false;
    if (!rcuMoved) {
      int row = (int)((rcuSY - rcuTop() + (int)rcuScroll) / RCU_ROW_H);
      if (row >= 0 && row < RCU_N) rcuRowTap(row);
    }
  }
  (void)tx;
}
