// =====================================================================
// about_renphone.ino -- app ABOUT PHONE (SCR_ABOUT) untuk Accretion Phone / NyxOS
// =====================================================================
// Isi halaman:
//   * Kartu identitas : "Accretion Phone" + "NyxOS" + versi firmware
//   * Daftar info     : chip, CPU, suhu, flash, RAM internal, PSRAM, SD Card,
//                       layar, baterai, WiFi (SSID/IP/MAC), uptime, build
//   * Bisa di-scroll dengan drag jari, tombol Kembali seperti app lain.
//
// ATURAN PROYEK YANG DIIKUTI (lihat komentar panjang di phone.ino):
//   - TIDAK ada String yang dibuat tiap frame (RAM internal cuma ~162KB dan
//     gampang terfragmentasi). Semua teks dibangun SEKALI per detik ke buffer
//     char statis lewat aboutRefresh(), drawAbout() hanya membaca buffer itu.
//   - Semua fungsi publik hanya menerima tipe primitif / LGFX_Sprite&, supaya
//     tidak kena bug prototype otomatis arduino-cli (kasus MazeGhost dkk).
//   - Layout dihitung dari SCR_W/SCR_H, jadi jalan di landscape & portrait.
//   - Tidak ada alokasi heap sama sekali di app ini.
//
// DIPASANG ke phone.ino lewat apply_about_patch.py:
//   enum Screen      <- SCR_ABOUT
//   apps[] + warna   <- { "About Phone", 'i', ... }
//   drawAppIcon()    <- case 'i'
//   loop()           <- aboutLoopPoll()  (refresh tiap 1 detik saat app terbuka)
// =====================================================================

// ---- identitas (ubah di sini kalau ganti nama / versi) ----
#define ABOUT_DEVICE_NAME  "Accretion Phone"
#define ABOUT_OS_NAME      "NyxOS"
#define ABOUT_FW_VERSION   "v119"      // samakan dengan nomor versi terakhir di komentar phone.ino
#define ABOUT_BOARD_NAME   "ESP32-S3 + ILI9341"

#define ABOUT_MAX_ROWS     24
#define ABOUT_KEY_LEN      18
#define ABOUT_VAL_LEN      44

struct AboutRow { char k[ABOUT_KEY_LEN]; char v[ABOUT_VAL_LEN]; };

static AboutRow aboutRows[ABOUT_MAX_ROWS];
static int      aboutRowN       = 0;
static float    aboutScroll     = 0;      // px, 0 = paling atas
static int      aboutMaxScroll  = 0;
static int      aboutLastY      = 0;
static bool     aboutDragging   = false;
static unsigned long aboutLastPoll = 0;

// ---------------------------------------------------------------------
// util kecil
// ---------------------------------------------------------------------
static void aboutAdd(const char* k, const char* v) {
  if (aboutRowN >= ABOUT_MAX_ROWS) return;
  strncpy(aboutRows[aboutRowN].k, k, ABOUT_KEY_LEN - 1);
  aboutRows[aboutRowN].k[ABOUT_KEY_LEN - 1] = 0;
  strncpy(aboutRows[aboutRowN].v, v, ABOUT_VAL_LEN - 1);
  aboutRows[aboutRowN].v[ABOUT_VAL_LEN - 1] = 0;
  aboutRowN++;
}

static void aboutFmtBytes(char* out, size_t n, uint64_t b) {
  if (b >= (1ULL << 30))      snprintf(out, n, "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
  else if (b >= (1ULL << 20)) snprintf(out, n, "%.1f MB", (double)b / (1024.0 * 1024.0));
  else                        snprintf(out, n, "%u KB", (unsigned)(b / 1024ULL));
}

static void aboutFmtUptime(char* out, size_t n, unsigned long ms) {
  unsigned long s = ms / 1000UL;
  unsigned long d = s / 86400UL; s %= 86400UL;
  unsigned long h = s / 3600UL;  s %= 3600UL;
  unsigned long m = s / 60UL;    s %= 60UL;
  if (d > 0) snprintf(out, n, "%lu hr %lu j %lu m", d, h, m);
  else if (h > 0) snprintf(out, n, "%lu j %lu m %lu d", h, m, s);
  else snprintf(out, n, "%lu m %lu d", m, s);
}

// Bangun ulang seluruh baris info. Dipanggil saat app dibuka & tiap ~1 detik.
static void aboutRefresh() {
  char a[ABOUT_VAL_LEN], b[24], c[24];
  aboutRowN = 0;

  aboutAdd("Perangkat", ABOUT_DEVICE_NAME);
  aboutAdd("Sistem", ABOUT_OS_NAME);
  aboutAdd("Pemilik", ownerName());
  snprintf(a, sizeof(a), "%s", ABOUT_FW_VERSION);
  aboutAdd("Versi firmware", a);
  snprintf(a, sizeof(a), "%s %s", __DATE__, __TIME__);
  aboutAdd("Waktu build", a);
  aboutAdd("Papan", ABOUT_BOARD_NAME);

  // ---- chip & CPU ----
  snprintf(a, sizeof(a), "%s rev %d, %d core", ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getChipCores());
  aboutAdd("Chip", a);
  snprintf(a, sizeof(a), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
  aboutAdd("CPU", a);
  snprintf(a, sizeof(a), "%.1f C", temperatureRead());
  aboutAdd("Suhu chip", a);

  // ---- memori ----
  aboutFmtBytes(a, sizeof(a), ESP.getFlashChipSize());
  aboutAdd("Flash", a);
  aboutFmtBytes(b, sizeof(b), ESP.getSketchSize());
  aboutFmtBytes(c, sizeof(c), ESP.getFreeSketchSpace());
  snprintf(a, sizeof(a), "%s / sisa %s", b, c);
  aboutAdd("Firmware", a);

  aboutFmtBytes(b, sizeof(b), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  aboutFmtBytes(c, sizeof(c), ESP.getHeapSize());
  snprintf(a, sizeof(a), "%s bebas / %s", b, c);
  aboutAdd("RAM internal", a);

  size_t psTotal = ESP.getPsramSize();
  if (psTotal > 0) {
    aboutFmtBytes(b, sizeof(b), ESP.getFreePsram());
    aboutFmtBytes(c, sizeof(c), psTotal);
    snprintf(a, sizeof(a), "%s bebas / %s", b, c);
  } else {
    snprintf(a, sizeof(a), "Tidak terdeteksi");
  }
  aboutAdd("PSRAM", a);

  // ---- SD Card ----
  if (sdReady) {
    aboutFmtBytes(b, sizeof(b), SD_MMC.usedBytes());
    aboutFmtBytes(c, sizeof(c), SD_MMC.totalBytes());
    snprintf(a, sizeof(a), "%s terpakai / %s", b, c);
  } else {
    snprintf(a, sizeof(a), "Tidak terdeteksi");
  }
  aboutAdd("SD Card", a);

  // ---- layar & baterai ----
  aboutAdd("Layar", "ILI9341 320x240, sentuh XPT2046");
  snprintf(a, sizeof(a), "%d%% (%.2f V)", battPercent, battVoltage);
  aboutAdd("Baterai", a);

  // ---- WiFi ----
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    snprintf(a, sizeof(a), "%s", ssid.c_str());
    aboutAdd("WiFi", a);
    IPAddress ip = WiFi.localIP();
    snprintf(a, sizeof(a), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    aboutAdd("Alamat IP", a);
  } else {
    aboutAdd("WiFi", "Tidak tersambung");
  }
  uint64_t mac = ESP.getEfuseMac();
  snprintf(a, sizeof(a), "%02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned)(mac >> 0) & 0xFF, (unsigned)(mac >> 8) & 0xFF, (unsigned)(mac >> 16) & 0xFF,
           (unsigned)(mac >> 24) & 0xFF, (unsigned)(mac >> 32) & 0xFF, (unsigned)(mac >> 40) & 0xFF);
  aboutAdd("MAC", a);

  aboutFmtUptime(a, sizeof(a), millis());
  aboutAdd("Waktu aktif", a);
}

// ---------------------------------------------------------------------
// geometri (dipakai bareng drawAbout() & aboutTouch())
// ---------------------------------------------------------------------
#define ABOUT_ROW_H   20
#define ABOUT_CARD_H  56

static int aboutListTop()    { return 26 + 18 + ABOUT_CARD_H + 8; }          // di bawah judul + kartu identitas
static int aboutListBottom() { return SCR_H - BACK_H - 3 - 6; }              // di atas tombol Kembali

static void aboutClampScroll() {
  int contentH = aboutRowN * ABOUT_ROW_H;
  int viewH = aboutListBottom() - aboutListTop();
  aboutMaxScroll = contentH - viewH;
  if (aboutMaxScroll < 0) aboutMaxScroll = 0;
  if (aboutScroll < 0) aboutScroll = 0;
  if (aboutScroll > aboutMaxScroll) aboutScroll = (float)aboutMaxScroll;
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
void aboutEnter() {
  aboutScroll = 0;
  aboutDragging = false;
  aboutRefresh();
  aboutClampScroll();
  aboutLastPoll = millis();
}

void aboutExit() {
  aboutDragging = false;
}

// dipanggil dari loop() phone.ino -- refresh angka live tiap 1 detik
void aboutLoopPoll() {
  if (millis() - aboutLastPoll < 1000UL) return;
  aboutLastPoll = millis();
  if (aboutDragging) return;           // jangan ganggu scroll yang sedang berjalan
  aboutRefresh();
  aboutClampScroll();
  needRedraw = true;
}

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
void drawAbout(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "About Phone");

  // ---- kartu identitas ----
  int cx = 8, cy = 26 + 18, cw = SCR_W - 16;
  drawGlassPanel(s, cx, cy, cw, ABOUT_CARD_H, 12, T().surface2, 170);

  // logo kecil: cakram accretion (lingkaran gelap + cincin aksen)
  int lr = 15, lx = cx + 10 + lr, ly = cy + ABOUT_CARD_H / 2;
  if (ownerHasAvatar()) {
    ownerDrawAvatar(s, lx, ly, lr + 4);              // foto pemilik menggantikan logo
  } else {
    s.fillCircle(lx, ly, lr, T().bg);
    s.drawCircle(lx, ly, lr, T().accent);
    s.drawCircle(lx, ly, lr - 3, T().accent2);
    s.fillCircle(lx, ly, 5, T().bg);
    s.fillEllipse(lx, ly, lr + 4, 3, T().accent);   // cincin miring
    s.fillCircle(lx, ly, 4, T().bg);
  }

  int tx = lx + lr + 12;
  s.setTextSize(2); s.setTextColor(T().text);
  s.setCursor(tx, cy + 10); s.print(ABOUT_DEVICE_NAME);
  s.setTextSize(1); s.setTextColor(T().accent);
  s.setCursor(tx, cy + 32); s.print(ABOUT_OS_NAME);
  s.setTextColor(T().subtext);
  s.print("  ");
  s.print(ABOUT_FW_VERSION);

  // ---- daftar info (di-clip supaya tidak menimpa judul / tombol Kembali) ----
  int top = aboutListTop(), bot = aboutListBottom();
  s.setClipRect(0, top, SCR_W, bot - top);
  s.setTextSize(1);
  int keyX = 12;
  int valX = SCR_W * 2 / 5; if (valX < 110) valX = 110;
  int y0 = top - (int)aboutScroll;
  for (int i = 0; i < aboutRowN; i++) {
    int ry = y0 + i * ABOUT_ROW_H;
    if (ry + ABOUT_ROW_H < top || ry > bot) continue;   // di luar layar, lewati
    s.setTextColor(T().subtext);
    s.setCursor(keyX, ry + 6); s.print(aboutRows[i].k);
    s.setTextColor(T().text);
    s.setCursor(valX, ry + 6); s.print(aboutRows[i].v);
    if (i < aboutRowN - 1) s.drawFastHLine(keyX, ry + ABOUT_ROW_H - 1, SCR_W - 2 * keyX, T().divider);
  }
  s.clearClipRect();

  // scrollbar tipis kalau konten lebih panjang dari area tampil
  if (aboutMaxScroll > 0) {
    int trackH = bot - top;
    int thumbH = trackH * trackH / (trackH + aboutMaxScroll); if (thumbH < 12) thumbH = 12;
    int thumbY = top + (int)((trackH - thumbH) * (aboutScroll / (float)aboutMaxScroll));
    s.fillRoundRect(SCR_W - 4, thumbY, 2, thumbH, 1, blend565(T().bg, T().text, 90));
  }

  drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh: Kembali + drag-scroll
// ---------------------------------------------------------------------
void aboutTouch(int x, int y, bool held, bool isNew) {
  if (isNew) {
    if (isBack(x, y)) { navBack(); return; }
    aboutDragging = (y >= aboutListTop() && y <= aboutListBottom());
    aboutLastY = y;
    return;
  }
  if (!held) { aboutDragging = false; return; }
  if (!aboutDragging) return;

  int dy = aboutLastY - y;
  aboutLastY = y;
  if (dy == 0) return;
  aboutScroll += (float)dy;
  aboutClampScroll();
  needRedraw = true;
}
