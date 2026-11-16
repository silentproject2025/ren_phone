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

#define ABOUT_MAX_ROWS     32
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
  { char vl[16]; otaVersionLabel(vl, sizeof(vl)); int bn = otaBuildNum();
    if (bn > 0) snprintf(a, sizeof(a), "%s (build %d)", vl, bn); else snprintf(a, sizeof(a), "%s", vl); }
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
  aboutAdd("Layar", RP_DISP_NAME);
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

  nyxAboutRows();   // NYX-EXTRAS-V1: reset terakhir, layar & RAM saat crash, backtrace
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

// =====================================================================
// EASTER EGG (EGG_ABOUT_V1): ketuk kartu identitas "Accretion Phone" 7x
// berturut-turut (jeda < 1,5 dtk antar ketukan) -> halaman cerita tersembunyi.
//   * teks muncul pelan-pelan (efek mesin tik), ketuk layar untuk melewati
//   * bisa di-scroll; tombol Kembali menutup cerita (bukan keluar dari About)
//   * TIDAK ada alokasi heap, semua teks ASCII (font bawaan tidak punya tanda
//     pisah panjang / emoji). Ubah isi cerita di EGG_P[] di bawah ini.
// =====================================================================
#define EGG_TITLE      "Error di baris terakhir"
#define EGG_MAX_LINES  140
#define EGG_LINE_H     12
#define EGG_CPS        110          // kecepatan ketik (karakter per detik)

static const char* const EGG_P[] = {
  "Awalnya, aku adalah definisi orang paling cuek di kelas. Satu ruangan, tapi aku bahkan nggak tahu nama dia siapa dan duduk di sebelah mana. Setiap kali dia manggil atau nanya sesuatu, respons aku selalu minimalis - paling mentok cuma nengok dikit, angkat alis, atau angkat bahu tanpa suara. Bahkan pas dia minta tolong pertama kali, refleks logika aku langsung nolak, meskipun akhirnya aku putar balik juga buat bantuin dia.",
  "Aku selalu mikir logika aku jauh lebih kuat dari perasaan. Tapi ternyata, alam bawah sadar nggak bisa dibohongi. Entah sejak kapan, radar aku mulai nangkap hal-hal kecil tentang dia. Aku mulai sadar kalau berada di dekat dia selalu ngasih efek tenang yang aneh. Dari yang tadinya nggak peduli, aku malah hafal cara dia senyum, nada bicaranya yang lembut, cara dia main piano, sampai kebiasaan-kebiasaan kecilnya yang ternyata mirip banget sama aku - termasuk gaya tidur aku dan dia yang sama persis di jam pelajaran Sejarah.",
  "Posisi duduk aku dan dia terpisah di dua ujung baris paling depan; aku di pojok kanan, dia di pojok kiri. Aku dan dia juga punya dunia masing-masing di kelas. Aku dengan segala urusan IT dan logika praktis, sementara dia dengan ketelitiannya di Matematika dan statusnya sebagai anak peringkat atas. Tapi di titik tertentu, benteng cuek yang aku bangun mati-matian akhirnya runtuh juga. Cukup lewat satu momen tatapan mata dan senyuman manis dari jarak beberapa meja, semua sistem pertahanan logika aku langsung berantakan.",
  "Lucunya, di saat aku masih sibuk menyembunyikan perasaan ini rapat-rapat di balik sikap dingin, kenyataan justru ngasih plot twist terakhir: dia ternyata sudah punya orang lain.",
  "Rasanya memang nyesek, kayak nemu error fatal tepat di baris kode terakhir saat semuanya terasa pas. Tapi di sisi lain, aku bersyukur karena selama ini aku masih menahan diri untuk nggak melangkah terlalu jauh. Nggak ada yang perlu disesali dari rasa kagum ini. Sekarang saatnya aku narik diri pelan-pelan, menghormati batas yang ada, dan nyimpan cerita ini rapi-rapi sebagai salah satu fase paling berkesan yang pernah aku lewatin."
};
#define EGG_PN ((int)(sizeof(EGG_P) / sizeof(EGG_P[0])))

static bool           aboutEggOpen  = false;
static uint8_t        eggTaps       = 0;
static unsigned long  eggLastTap    = 0;
static const char*    eggLnPtr[EGG_MAX_LINES];
static uint8_t        eggLnLen[EGG_MAX_LINES];
static uint16_t       eggLnCum[EGG_MAX_LINES];   // total karakter sampai akhir baris i
static int            eggLnN        = 0;
static int            eggTotal      = 0;
static int            eggChars      = 0;         // karakter yang sudah "diketik"
static unsigned long  eggT0         = 0;
static float          eggScroll     = 0;
static int            eggMaxScroll  = 0;
static int            eggLastY      = 0;
static bool           eggDragging   = false;
static bool           eggAuto       = true;

static int eggViewTop()    { return 26 + 24; }
static int eggViewBottom() { return SCR_H - BACK_H - 3 - 6; }

// pecah paragraf jadi baris sesuai lebar layar (sekali saat dibuka)
static void eggBuildLines() {
  int maxCh = (SCR_W - 24) / 6; if (maxCh < 16) maxCh = 16; if (maxCh > 120) maxCh = 120;
  eggLnN = 0; eggTotal = 0;
  for (int p = 0; p < EGG_PN && eggLnN < EGG_MAX_LINES - 1; p++) {
    const char* t = EGG_P[p];
    int tl = (int)strlen(t), pos = 0;
    while (pos < tl && eggLnN < EGG_MAX_LINES - 1) {
      int len = tl - pos;
      if (len > maxCh) {
        len = maxCh;
        int b = len;
        while (b > 0 && t[pos + b] != ' ') b--;      // mundur ke spasi terakhir
        if (b > 0) len = b;
      }
      eggLnPtr[eggLnN] = t + pos; eggLnLen[eggLnN] = (uint8_t)len;
      eggTotal += len; eggLnCum[eggLnN] = (uint16_t)eggTotal; eggLnN++;
      pos += len;
      while (pos < tl && t[pos] == ' ') pos++;
    }
    if (eggLnN < EGG_MAX_LINES) {                      // baris kosong antar paragraf
      eggLnPtr[eggLnN] = ""; eggLnLen[eggLnN] = 0; eggLnCum[eggLnN] = (uint16_t)eggTotal; eggLnN++;
    }
  }
  int contentH = eggLnN * EGG_LINE_H, viewH = eggViewBottom() - eggViewTop();
  eggMaxScroll = contentH - viewH; if (eggMaxScroll < 0) eggMaxScroll = 0;
}

static void eggOpen() {
  eggBuildLines();
  eggChars = 0; eggT0 = millis(); eggScroll = 0; eggAuto = true; eggDragging = false;
  aboutEggOpen = true; eggTaps = 0;
  needRedraw = true;
}

// dipanggil tiap iterasi loop saat About terbuka (lewat aboutLoopPoll)
static void eggTick() {
  if (eggChars >= eggTotal) return;
  int n = (int)((millis() - eggT0) * EGG_CPS / 1000UL);
  if (n > eggTotal) n = eggTotal;
  if (n != eggChars) {
    eggChars = n;
    if (eggAuto) {                                     // ikuti baris yang sedang diketik
      int li = 0; while (li < eggLnN - 1 && eggLnCum[li] < eggChars) li++;
      int bottomY = (li + 1) * EGG_LINE_H, viewH = eggViewBottom() - eggViewTop();
      float want = (float)(bottomY - viewH); if (want < 0) want = 0; if (want > eggMaxScroll) want = (float)eggMaxScroll;
      eggScroll = want;
    }
    needRedraw = true;
  }
}

static void eggDraw(LGFX_Sprite& s) {
  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, EGG_TITLE);
  int top = eggViewTop(), bot = eggViewBottom();
  s.setClipRect(0, top, SCR_W, bot - top);
  s.setTextSize(1);
  int y0 = top - (int)eggScroll;
  char buf[130];
  for (int i = 0; i < eggLnN; i++) {
    int ry = y0 + i * EGG_LINE_H;
    if (ry + EGG_LINE_H < top || ry > bot) continue;
    int len = eggLnLen[i]; if (len == 0) continue;
    int startCum = (int)eggLnCum[i] - len;             // karakter yang sudah lewat sebelum baris ini
    int show = eggChars - startCum;
    if (show <= 0) continue;
    if (show > len) show = len;
    if (show > (int)sizeof(buf) - 1) show = (int)sizeof(buf) - 1;
    memcpy(buf, eggLnPtr[i], show); buf[show] = 0;
    s.setTextColor(i >= eggLnN - 3 ? T().accent : T().text);   // paragraf penutup beraksen
    s.setCursor(12, ry + 2); s.print(buf);
  }
  s.clearClipRect();
  if (eggMaxScroll > 0) {
    int trackH = bot - top;
    int thumbH = trackH * trackH / (trackH + eggMaxScroll); if (thumbH < 12) thumbH = 12;
    int thumbY = top + (int)((trackH - thumbH) * (eggScroll / (float)eggMaxScroll));
    nyxAaRR(s, SCR_W - 4, thumbY, 2, thumbH, 1, blend565(T().bg, T().text, 90));
  }
  drawBack(s);
  drawToast(s);
}

static void eggTouch(int x, int y, bool held, bool isNew) {
  if (isNew) {
    if (isBack(x, y)) { aboutEggOpen = false; eggDragging = false; needRedraw = true; return; }
    if (eggChars < eggTotal) { eggChars = eggTotal; needRedraw = true; }   // ketuk = lewati animasi ketik
    eggAuto = false;
    eggDragging = (y >= eggViewTop() && y <= eggViewBottom());
    eggLastY = y;
    return;
  }
  if (!held) { eggDragging = false; return; }
  if (!eggDragging) return;
  int dy = eggLastY - y; eggLastY = y;
  if (dy == 0) return;
  eggScroll += (float)dy;
  if (eggScroll < 0) eggScroll = 0;
  if (eggScroll > eggMaxScroll) eggScroll = (float)eggMaxScroll;
  needRedraw = true;
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
void aboutEnter() {
  aboutEggOpen = false; eggTaps = 0; eggDragging = false;   // EGG_ABOUT_V1
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
  if (aboutEggOpen) { eggTick(); return; }                 // EGG_ABOUT_V1
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
  if (aboutEggOpen) { eggDraw(s); return; }                // EGG_ABOUT_V1
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
    nyxAaCircle(s, lx, ly, lr, T().bg);
    nyxAaCircleRing(s, lx, ly, lr, T().accent);
    nyxAaCircleRing(s, lx, ly, lr - 3, T().accent2);
    nyxAaCircle(s, lx, ly, 5, T().bg);
    s.fillEllipse(lx, ly, lr + 4, 3, T().accent);   // cincin miring
    nyxAaCircle(s, lx, ly, 4, T().bg);
  }

  int tx = lx + lr + 12;
  s.setTextSize(2); s.setTextColor(T().text);
  s.setCursor(tx, cy + 10); s.print(ABOUT_DEVICE_NAME);
  s.setTextSize(1); s.setTextColor(T().accent);
  s.setCursor(tx, cy + 32); s.print(ABOUT_OS_NAME);
  s.setTextColor(T().subtext);
  s.print("  ");
  { char vl[16]; otaVersionLabel(vl, sizeof(vl)); s.print(vl); }

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
    nyxAaRR(s, SCR_W - 4, thumbY, 2, thumbH, 1, blend565(T().bg, T().text, 90));
  }

  drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh: Kembali + drag-scroll
// ---------------------------------------------------------------------
void aboutTouch(int x, int y, bool held, bool isNew) {
  if (aboutEggOpen) { eggTouch(x, y, held, isNew); return; }   // EGG_ABOUT_V1
  if (isNew) {
    if (isBack(x, y)) { navBack(); return; }
    // EGG_ABOUT_V1: 7x ketuk kartu identitas -> cerita tersembunyi
    if (y >= 26 + 18 && y <= 26 + 18 + ABOUT_CARD_H) {
      unsigned long now = millis();
      if (now - eggLastTap > 1500UL) eggTaps = 0;
      eggLastTap = now;
      if (++eggTaps >= 7) { eggOpen(); return; }
    }
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
