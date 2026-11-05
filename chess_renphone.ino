// =====================================================================
// chess_renphone.ino -- app "Catur": catur ONLINE antar HP Accretion/NyxOS lewat broker MQTT
//
// Memakai broker + akun yang SUDAH diisi di app Chat (chatHost/chatUser/chatPass/chatName di
// chat_renphone.ino) -- tidak ada konfigurasi baru. File ini HARUS dikompilasi SETELAH
// chat_renphone.ino (urutan abjad arduino-cli: "chat" < "chess" -> sudah benar).
//
// CARA MAIN: dua HP membuka app Catur, mengisi KODE ROOM yang sama, lalu "Mulai". HP yang masuk
// duluan jadi Putih, yang kedua jadi Hitam. Aturan lengkap (skak, skakmat, pat, rokade,
// en passant, promosi, 50 langkah). Tidak ada jam catur & belum ada ulangan 3 posisi.
//
// SKEMA TOPIK  (<r> = kode room, <pid> = id perangkat 8 hex, disimpan di NVS)
//   renphone/chess/<r>/w     RETAINED  "<pid>|<nama>"   kursi Putih
//   renphone/chess/<r>/b     RETAINED  "<pid>|<nama>"   kursi Hitam
//   renphone/chess/<r>/g     RETAINED  "<gen>|<langkah>|<hasil>"   STATUS PERMAINAN LENGKAP
//        langkah = "e2e4 e7e5 g1f3 ..." (notasi koordinat), hasil = "" | "RW" (putih menyerah) | "RB"
//        gen naik 1 tiap "Main lagi". Karena yg dikirim SELALU seluruh daftar langkah, pesan
//        dobel/telat/terputus tidak merusak apa-apa: penerima hanya mengambil yg lebih baru,
//        dan lawan yang baru tersambung langsung dapat posisi terakhir dari pesan retained.
//   renphone/chess/<r>/on/<pid>  RETAINED "1"/"0"  kehadiran (Last Will = "0" kalau putus)
//
// BROKER: kalau akun broker kamu dibatasi ACL ke renphone/chat/# saja, tambahkan izin
// publish+subscribe ke renphone/chess/# .
//
// Aturan arduino-cli di proyek ini: fungsi TIDAK boleh menerima/mengembalikan tipe struct/kelas
// buatan sendiri; PubSubClient hanya dipakai sbg variabel lokal di dalam chsTaskFunc().
// =====================================================================
#include <PubSubClient.h>

// ==== ENGINE BEGIN ====
// Mesin aturan catur murni (tanpa Arduino/LovyanGFX) -- diuji perft di PC.
// Petak: sq = rank*8 + file (a1 = 0, h1 = 7, a8 = 56). Bidak: +1..+6 putih, -1..-6 hitam
// (1=Pion 2=Kuda 3=Gajah 4=Benteng 5=Menteri 6=Raja). Langkah (16 bit): dari | ke<<6 | promosi<<12.
#define CHS_WK 1
#define CHS_WQ 2
#define CHS_BK 4
#define CHS_BQ 8

static int8_t   chsBd[64];
static bool     chsWhite = true;      // true = giliran putih
static uint8_t  chsCastle = 15;       // hak rokade (CHS_*)
static int8_t   chsEp = -1;           // petak target en-passant (-1 = tidak ada)
static uint16_t chsHalf = 0;          // penghitung 50 langkah (setengah-langkah)

static void chsReset() {
  static const int8_t back[8] = {4, 2, 3, 5, 6, 3, 2, 4};
  for (int i = 0; i < 64; i++) chsBd[i] = 0;
  for (int f = 0; f < 8; f++) { chsBd[f] = back[f]; chsBd[8 + f] = 1; chsBd[48 + f] = -1; chsBd[56 + f] = -back[f]; }
  chsWhite = true; chsCastle = 15; chsEp = -1; chsHalf = 0;
}

// apakah petak sq diserang oleh sisi byWhite?
static bool chsAtt(int sq, bool byWhite) {
  int r = sq >> 3, c = sq & 7;
  int8_t sg = byWhite ? 1 : -1;
  int pr = byWhite ? r - 1 : r + 1;
  if (pr >= 0 && pr < 8) {
    if (c > 0 && chsBd[pr * 8 + c - 1] == sg) return true;
    if (c < 7 && chsBd[pr * 8 + c + 1] == sg) return true;
  }
  static const int8_t kn[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
  for (int i = 0; i < 8; i++) {
    int rr = r + kn[i][0], cc = c + kn[i][1];
    if (rr < 0 || rr > 7 || cc < 0 || cc > 7) continue;
    if (chsBd[rr * 8 + cc] == sg * 2) return true;
  }
  static const int8_t dr[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
  for (int d = 0; d < 8; d++) {
    int rr = r + dr[d][0], cc = c + dr[d][1];
    if (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && chsBd[rr * 8 + cc] == sg * 6) return true;
    while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8) {
      int8_t p = chsBd[rr * 8 + cc];
      if (p) {
        if ((p > 0) == byWhite) {
          int t = p > 0 ? p : -p;
          if (d < 4 && (t == 4 || t == 5)) return true;
          if (d >= 4 && (t == 3 || t == 5)) return true;
        }
        break;
      }
      rr += dr[d][0]; cc += dr[d][1];
    }
  }
  return false;
}

static int chsAddPawn(uint16_t* out, int n, int from, int to, bool promo) {
  if (promo) { for (int pr = 5; pr >= 2; pr--) out[n++] = (uint16_t)(from | (to << 6) | (pr << 12)); }
  else out[n++] = (uint16_t)(from | (to << 6));
  return n;
}

// langkah pseudo-legal sisi yang jalan (belum disaring skak). out minimal 256 elemen.
static int chsGen(uint16_t* out) {
  int n = 0;
  bool w = chsWhite;
  int8_t sg = w ? 1 : -1;
  static const int8_t dirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
  static const int8_t kn[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = chsBd[sq];
    if (!p || ((p > 0) != w)) continue;
    int t = p > 0 ? p : -p;
    int r = sq >> 3, c = sq & 7;
    if (t == 1) {
      int dr = w ? 1 : -1, r1 = r + dr;
      int startR = w ? 1 : 6, promoR = w ? 7 : 0;
      if (r1 < 0 || r1 > 7) continue;
      if (chsBd[r1 * 8 + c] == 0) {
        n = chsAddPawn(out, n, sq, r1 * 8 + c, r1 == promoR);
        if (r == startR && chsBd[(r + 2 * dr) * 8 + c] == 0) out[n++] = (uint16_t)(sq | (((r + 2 * dr) * 8 + c) << 6));
      }
      for (int dc = -1; dc <= 1; dc += 2) {
        int cc = c + dc;
        if (cc < 0 || cc > 7) continue;
        int to = r1 * 8 + cc;
        int8_t q = chsBd[to];
        if (q && ((q > 0) != w)) n = chsAddPawn(out, n, sq, to, r1 == promoR);
        else if (!q && to == chsEp) out[n++] = (uint16_t)(sq | (to << 6));
      }
    } else if (t == 2 || t == 6) {
      for (int i = 0; i < 8; i++) {
        int rr, cc;
        if (t == 2) { rr = r + kn[i][0]; cc = c + kn[i][1]; }
        else        { rr = r + dirs[i][0]; cc = c + dirs[i][1]; }
        if (rr < 0 || rr > 7 || cc < 0 || cc > 7) continue;
        int8_t q = chsBd[rr * 8 + cc];
        if (q && ((q > 0) == w)) continue;
        out[n++] = (uint16_t)(sq | ((rr * 8 + cc) << 6));
      }
      if (t == 6) {
        int home = w ? 4 : 60;
        if (sq == home && !chsAtt(home, !w)) {
          if ((chsCastle & (w ? CHS_WK : CHS_BK)) && chsBd[home + 1] == 0 && chsBd[home + 2] == 0 &&
              chsBd[home + 3] == sg * 4 && !chsAtt(home + 1, !w))
            out[n++] = (uint16_t)(sq | ((home + 2) << 6));
          if ((chsCastle & (w ? CHS_WQ : CHS_BQ)) && chsBd[home - 1] == 0 && chsBd[home - 2] == 0 &&
              chsBd[home - 3] == 0 && chsBd[home - 4] == sg * 4 && !chsAtt(home - 1, !w))
            out[n++] = (uint16_t)(sq | ((home - 2) << 6));
        }
      }
    } else {
      int d0 = (t == 3) ? 4 : 0, d1 = (t == 4) ? 4 : 8;
      for (int d = d0; d < d1; d++) {
        int rr = r + dirs[d][0], cc = c + dirs[d][1];
        while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8) {
          int8_t q = chsBd[rr * 8 + cc];
          if (q && ((q > 0) == w)) break;
          out[n++] = (uint16_t)(sq | ((rr * 8 + cc) << 6));
          if (q) break;
          rr += dirs[d][0]; cc += dirs[d][1];
        }
      }
    }
  }
  return n;
}

// jalankan langkah m pada papan global (m dianggap sudah sah / pseudo-legal)
static void chsDo(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  int8_t p = chsBd[from];
  int t = p > 0 ? p : -p;
  bool w = p > 0;
  bool cap = chsBd[to] != 0;
  if (t == 1 && to == chsEp && !cap && (from & 7) != (to & 7)) { chsBd[w ? to - 8 : to + 8] = 0; cap = true; }
  if (t == 6 && (to - from == 2 || from - to == 2)) {
    if (to > from) { chsBd[from + 1] = chsBd[from + 3]; chsBd[from + 3] = 0; }
    else           { chsBd[from - 1] = chsBd[from - 4]; chsBd[from - 4] = 0; }
  }
  chsBd[to] = promo ? (int8_t)(w ? promo : -promo) : p;
  chsBd[from] = 0;
  chsEp = -1;
  if (t == 1 && (to - from == 16 || from - to == 16)) chsEp = (int8_t)((from + to) / 2);
  if (t == 6) chsCastle &= (uint8_t)(w ? ~(CHS_WK | CHS_WQ) : ~(CHS_BK | CHS_BQ));
  if (from == 0  || to == 0)  chsCastle &= (uint8_t)~CHS_WQ;
  if (from == 7  || to == 7)  chsCastle &= (uint8_t)~CHS_WK;
  if (from == 56 || to == 56) chsCastle &= (uint8_t)~CHS_BQ;
  if (from == 63 || to == 63) chsCastle &= (uint8_t)~CHS_BK;
  chsHalf = (t == 1 || cap) ? 0 : (uint16_t)(chsHalf + 1);
  chsWhite = !w;
}

static int chsKingSq(bool white) {
  int8_t k = white ? 6 : -6;
  for (int i = 0; i < 64; i++) if (chsBd[i] == k) return i;
  return -1;
}

// apakah langkah m (dari daftar chsGen) sah, yaitu raja sendiri tidak skak setelahnya?
static bool chsLegal(uint16_t m) {
  int8_t sb[64]; memcpy(sb, chsBd, 64);
  bool sw = chsWhite; uint8_t sc = chsCastle; int8_t se = chsEp; uint16_t sh = chsHalf;
  bool mover = chsWhite;
  chsDo(m);
  int ks = chsKingSq(mover);
  bool ok = ks >= 0 && !chsAtt(ks, !mover);
  memcpy(chsBd, sb, 64); chsWhite = sw; chsCastle = sc; chsEp = se; chsHalf = sh;
  return ok;
}

static bool chsInCheck() {
  int ks = chsKingSq(chsWhite);
  return ks >= 0 && chsAtt(ks, !chsWhite);
}

static bool chsHasMove() {
  uint16_t mv[256];
  int n = chsGen(mv);
  for (int i = 0; i < n; i++) if (chsLegal(mv[i])) return true;
  return false;
}

// semua langkah sah dari petak 'from'
static int chsLegalFrom(int from, uint16_t* out) {
  uint16_t mv[256];
  int n = chsGen(mv), k = 0;
  for (int i = 0; i < n; i++) if ((mv[i] & 63) == from && chsLegal(mv[i])) out[k++] = mv[i];
  return k;
}

static bool chsInsufficient() {
  int minor = 0;
  for (int i = 0; i < 64; i++) {
    int8_t p = chsBd[i];
    if (!p) continue;
    int t = p > 0 ? p : -p;
    if (t == 6) continue;
    if (t == 2 || t == 3) minor++;
    else return false;
  }
  return minor <= 1;
}

// 0 = lanjut, 1 = skakmat (yg jalan kalah), 2 = pat, 3 = 50 langkah, 4 = bidak tidak cukup
static int chsStatus() {
  if (!chsHasMove()) return chsInCheck() ? 1 : 2;
  if (chsHalf >= 100) return 3;
  if (chsInsufficient()) return 4;
  return 0;
}

static void chsMoveStr(uint16_t m, char* o) {
  int f = m & 63, t = (m >> 6) & 63, p = (m >> 12) & 15;
  o[0] = (char)('a' + (f & 7)); o[1] = (char)('1' + (f >> 3));
  o[2] = (char)('a' + (t & 7)); o[3] = (char)('1' + (t >> 3));
  int k = 4;
  if (p) o[k++] = (p == 5) ? 'q' : (p == 4) ? 'r' : (p == 3) ? 'b' : 'n';
  o[k] = 0;
}

// "e2e4" / "e7e8q" -> langkah sah pada posisi sekarang?
static bool chsParse(const char* s, int len, uint16_t* out) {
  if (len < 4 || len > 5) return false;
  if (s[0] < 'a' || s[0] > 'h' || s[2] < 'a' || s[2] > 'h') return false;
  if (s[1] < '1' || s[1] > '8' || s[3] < '1' || s[3] > '8') return false;
  int from = (s[1] - '1') * 8 + (s[0] - 'a');
  int to   = (s[3] - '1') * 8 + (s[2] - 'a');
  int promo = 0;
  if (len == 5) {
    char c = s[4];
    promo = (c == 'q') ? 5 : (c == 'r') ? 4 : (c == 'b') ? 3 : (c == 'n') ? 2 : -1;
    if (promo < 0) return false;
  }
  uint16_t mv[256];
  int n = chsGen(mv);
  for (int i = 0; i < n; i++) {
    if ((mv[i] & 63) == from && ((mv[i] >> 6) & 63) == to && ((mv[i] >> 12) & 15) == promo && chsLegal(mv[i])) {
      *out = mv[i]; return true;
    }
  }
  return false;
}

// mainkan ulang daftar langkah "e2e4 e7e5 ..." dari posisi awal. Return jumlah langkah, atau -1 kalau ada yg tak sah.
static int chsReplay(const char* mv) {
  chsReset();
  int ply = 0;
  const char* p = mv;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* q = p;
    while (*q && *q != ' ') q++;
    uint16_t m;
    if (!chsParse(p, (int)(q - p), &m)) return -1;
    chsDo(m);
    ply++;
    p = q;
  }
  return ply;
}

static int chsCountPlies(const char* mv) {
  int n = 0; bool in = false;
  for (const char* p = mv; *p; p++) {
    if (*p == ' ') in = false;
    else if (!in) { in = true; n++; }
  }
  return n;
}
// ==== ENGINE END ====

// ---------------------------------------------------------------------
// STATE PERMAINAN + JARINGAN
// ---------------------------------------------------------------------
#define CHS_PORT      8883
#define CHS_BUF       1536
#define CHS_MAX_PLY   280
#define CHS_RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xF8)>>3)))
#define CHS_LIGHT     CHS_RGB(240,217,181)
#define CHS_DARK      CHS_RGB(181,136,99)

static SemaphoreHandle_t chsMx = nullptr;
static TaskHandle_t      chsTask = nullptr;
static StackType_t*      chsStack = nullptr;
static StaticTask_t      chsTCB;
static char*  chsMoves = nullptr;   // 5 buffer 1536 byte di PSRAM: langkah lokal, rx, out, tmp, tx
static char*  chsRxG = nullptr;
static char*  chsOutG = nullptr;
static char*  chsTmpG = nullptr;
static char*  chsTxG = nullptr;

static volatile bool    chsActive = false;     // sesi online berjalan (task jaringan boleh tersambung)
static volatile uint8_t chsConn = 0;           // 0 menyambung, 1 siap, 2 error
static volatile bool    chsReady = false;      // riwayat retained sudah sempat masuk
static volatile bool    chsDirty = false;
static char   chsErr[44] = "";
static char   chsBase[40] = "";                // "renphone/chess/<room>/"
static char   chsRoom[10] = "";
static char   chsPid[9] = "";
static String chsRoomStr = "";                 // kolom ketik kode room
static uint8_t chsScr = 0;                     // 0 = setup, 1 = papan

static char   chsRxW[48] = "", chsRxB[48] = "";
static volatile bool chsRxWf = false, chsRxBf = false, chsRxGf = false;
static char   chsOnPid[4][9];
static uint8_t chsOnVal[4];
static volatile bool    chsSendG = false;
static volatile uint8_t chsPubSeat = 0;        // 1 = klaim Putih, 2 = klaim Hitam
static char   chsPubName[24] = "";
static unsigned long chsLastSendMs = 0, chsClaimMs = 0;

static char   chsWPid[9] = "", chsBPid[9] = "", chsWName[24] = "", chsBName[24] = "";
static int8_t chsRole = 0;                     // 0 belum, 1 Putih, 2 Hitam, 3 room penuh
static bool   chsClaimW = false, chsClaimB = false;
static uint16_t chsGenNo = 0, chsPly = 0;
static char   chsRes[3] = "";
static uint16_t chsRemoteGen = 0, chsRemotePly = 0;
static char   chsRemoteRes[3] = "";
static int    chsEnd = 0;                      // 0 lanjut, 1 mat, 2 pat, 3 50 lgk, 4 material, 5 batas, 6 menyerah
static int    chsSel = -1, chsLast = -1;       // petak terpilih; langkah terakhir (dari | ke<<6)
static bool   chsHL[64];
static bool   chsPromoOn = false;
static int    chsPromoFrom = 0, chsPromoTo = 0;
static bool   chsConfirm = false;
static unsigned long chsConfirmMs = 0;

static void chsSetErr(const char* m) { strncpy(chsErr, m, sizeof(chsErr) - 1); chsErr[sizeof(chsErr) - 1] = 0; }

static bool chsEnsureInit();
static void chsTaskFunc(void*);

static void chsUpdateEnd() {
  chsEnd = chsRes[0] ? 6 : chsStatus();
  if (chsEnd == 0 && chsPly >= CHS_MAX_PLY) chsEnd = 5;
}

static void chsQueueSend() {
  if (!chsMx || !chsOutG) return;
  if (xSemaphoreTake(chsMx, pdMS_TO_TICKS(200)) == pdTRUE) {
    snprintf(chsOutG, CHS_BUF, "%u|%s|%s", (unsigned)chsGenNo, chsMoves, chsRes);
    chsSendG = true;
    xSemaphoreGive(chsMx);
  }
  chsLastSendMs = millis();
}

// ---- callback MQTT (jalan di task jaringan): hanya menyalin ke buffer, kerja berat di UI ----
static void chsMqttCb(char* topic, uint8_t* pl, unsigned int len) {
  size_t bl = strlen(chsBase);
  if (!chsMx || strncmp(topic, chsBase, bl) != 0) return;
  const char* sfx = topic + bl;
  if (xSemaphoreTake(chsMx, pdMS_TO_TICKS(100)) != pdTRUE) return;
  if (!strcmp(sfx, "g")) {
    unsigned n = len > CHS_BUF - 6 ? CHS_BUF - 6 : len;
    memcpy(chsRxG, pl, n); chsRxG[n] = 0; chsRxGf = true;
  } else if (!strcmp(sfx, "w")) {
    unsigned n = len > 46 ? 46 : len; memcpy(chsRxW, pl, n); chsRxW[n] = 0; chsRxWf = true;
  } else if (!strcmp(sfx, "b")) {
    unsigned n = len > 46 ? 46 : len; memcpy(chsRxB, pl, n); chsRxB[n] = 0; chsRxBf = true;
  } else if (!strncmp(sfx, "on/", 3)) {
    const char* pid = sfx + 3;
    if (strlen(pid) == 8) {
      int slot = -1;
      for (int i = 0; i < 4; i++) if (!strcmp(chsOnPid[i], pid)) slot = i;
      if (slot < 0) for (int i = 0; i < 4; i++) if (!chsOnPid[i][0]) { slot = i; break; }
      if (slot < 0) slot = 0;
      strcpy(chsOnPid[slot], pid);
      chsOnVal[slot] = (len > 0 && pl[0] == '1') ? 1 : 0;
    }
  }
  xSemaphoreGive(chsMx);
  chsDirty = true;
}

static void chsErrFromState(int st) {
  if (st == 4 || st == 5) chsSetErr("Username/password salah");
  else if (st == -2)      chsSetErr("Tidak bisa menghubungi broker");
  else if (st == 3)       chsSetErr("Broker tidak tersedia");
  else { char b[28]; snprintf(b, sizeof(b), "MQTT error %d", st); chsSetErr(b); }
}

static void chsTaskFunc(void*) {
  for (;;) {
    if (!chsActive) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
    if (WiFi.status() != WL_CONNECTED) {
      chsConn = 2; chsSetErr("WiFi belum terhubung"); chsDirty = true;
      vTaskDelay(pdMS_TO_TICKS(1500)); continue;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 30000) {
      chsConn = 2; chsSetErr("RAM padat, tutup app lain"); chsDirty = true;
      vTaskDelay(pdMS_TO_TICKS(2000)); continue;
    }
    String host = chatCleanHost(), user = chatUser, pass = chatPass;
    String base = String("renphone/chess/") + chsRoom + "/";
    strncpy(chsBase, base.c_str(), sizeof(chsBase) - 1); chsBase[sizeof(chsBase) - 1] = 0;
    String willT = base + "on/" + chsPid;
    String onT = willT;
    String subT = base + "#";
    String gT = base + "g", wT = base + "w", bT = base + "b";
    chsConn = 0; chsReady = false; chsDirty = true;
    int fails = 0;
    {
      WiFiClientSecure net;                 // dilepas (buffer TLS) begitu blok ini selesai
      net.setInsecure();
      PubSubClient mqtt(net);
      mqtt.setServer(host.c_str(), CHS_PORT);
      mqtt.setBufferSize(CHS_BUF + 160);
      mqtt.setKeepAlive(30);
      mqtt.setSocketTimeout(8);
      mqtt.setCallback(chsMqttCb);
      char cid[28];
      snprintf(cid, sizeof(cid), "rpchess-%08x", (unsigned)esp_random());
      if (mqtt.connect(cid, user.c_str(), pass.c_str(), willT.c_str(), 1, true, "0")) {
        mqtt.subscribe(subT.c_str(), 1);
        mqtt.publish(onT.c_str(), (const uint8_t*)"1", 1, true);
        uint32_t readyAt = millis() + 1500UL;                 // beri waktu pesan retained masuk
        while (chsActive && mqtt.connected() && WiFi.status() == WL_CONNECTED) {
          mqtt.loop();
          if (!chsReady && (int32_t)(millis() - readyAt) >= 0) {
            chsReady = true; chsConn = 1; chsErr[0] = 0; chsDirty = true;
          }
          if (chsReady && chsPubSeat) {
            uint8_t seat = chsPubSeat; chsPubSeat = 0;
            char pl[40];
            snprintf(pl, sizeof(pl), "%s|%s", chsPid, chsPubName);
            mqtt.publish(seat == 1 ? wT.c_str() : bT.c_str(), (const uint8_t*)pl, (unsigned)strlen(pl), true);
          }
          if (chsReady && chsSendG) {
            xSemaphoreTake(chsMx, portMAX_DELAY);
            strncpy(chsTxG, chsOutG, CHS_BUF - 1); chsTxG[CHS_BUF - 1] = 0;
            chsSendG = false;
            xSemaphoreGive(chsMx);
            mqtt.publish(gT.c_str(), (const uint8_t*)chsTxG, (unsigned)strlen(chsTxG), true);   // gagal -> dikirim ulang otomatis oleh UI
          }
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (chsActive) {
          fails++; chsConn = 2;
          if (WiFi.status() != WL_CONNECTED) chsSetErr("WiFi putus");
          else chsSetErr("Koneksi ke broker terputus");
          chsDirty = true;
        } else if (mqtt.connected()) {
          mqtt.publish(onT.c_str(), (const uint8_t*)"0", 1, true);       // keluar baik2 -> tandai offline
        }
      } else {
        fails++; chsConn = 2;
        chsErrFromState(mqtt.state());
        chsDirty = true;
      }
      mqtt.disconnect();
      net.stop();
    }
    chsReady = false;
    if (chsActive) {
      int waitMs = 3000 * (fails < 5 ? (fails > 0 ? fails : 1) : 5);
      for (int w = 0; w < waitMs && chsActive; w += 100) vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static bool chsEnsureInit() {
  if (!chsMx) chsMx = xSemaphoreCreateMutex();
  if (!chsMx) return false;
  if (!chsMoves) {
    char* b = (char*)heap_caps_calloc(5, CHS_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) b = (char*)calloc(5, CHS_BUF);
    if (!b) return false;
    chsMoves = b; chsRxG = b + CHS_BUF; chsOutG = b + 2 * CHS_BUF; chsTmpG = b + 3 * CHS_BUF; chsTxG = b + 4 * CHS_BUF;
  }
  if (!chatCfgLoaded) chatCfgLoad();
  if (!chsPid[0]) {
    Preferences p;
    if (p.begin("chess", false)) {
      String id = p.getString("pid", "");
      if (id.length() != 8) { char t[9]; snprintf(t, sizeof(t), "%08x", (unsigned)esp_random()); id = String(t); p.putString("pid", id); }
      strncpy(chsPid, id.c_str(), 8); chsPid[8] = 0;
      if (!chsRoomStr.length()) chsRoomStr = p.getString("room", "");
      p.end();
    } else { snprintf(chsPid, sizeof(chsPid), "%08x", (unsigned)esp_random()); }
  }
  if (!chsTask) {
    BaseType_t r = createTaskPsramStack(chsTaskFunc, "chessTask", 16384, NULL, &chsStack, &chsTCB, &chsTask, 0);
    if (r != pdPASS) { chsTask = nullptr; return false; }
  }
  return true;
}

// ---------------------------------------------------------------------
// SINKRONISASI (jalan di loop UI)
// ---------------------------------------------------------------------
static void chsParseSeat(const char* pl, char* pid, char* name) {
  pid[0] = 0; name[0] = 0;
  const char* bar = strchr(pl, '|');
  if (!bar || bar - pl != 8) return;
  memcpy(pid, pl, 8); pid[8] = 0;
  strncpy(name, bar + 1, 23); name[23] = 0;
}

static void chsLastFromTok(const char* mv) {
  const char* lastTok = mv; 
  for (const char* p = mv; *p; p++) if (*p == ' ' && *(p + 1)) lastTok = p + 1;
  if (!*mv) { chsLast = -1; return; }
  int f = (lastTok[1] - '1') * 8 + (lastTok[0] - 'a');
  int t = (lastTok[3] - '1') * 8 + (lastTok[2] - 'a');
  chsLast = (f >= 0 && f < 64 && t >= 0 && t < 64) ? (f | (t << 6)) : -1;
}

static void chsAdoptRemote(char* g) {
  char* p1 = strchr(g, '|');
  char* p2 = strrchr(g, '|');
  if (!p1 || p2 == p1) return;
  *p1 = 0; *p2 = 0;
  unsigned gen = (unsigned)atoi(g);
  const char* mv = p1 + 1;
  const char* res = p2 + 1;
  chsRemoteGen = (uint16_t)gen;
  chsRemotePly = (uint16_t)chsCountPlies(mv);
  strncpy(chsRemoteRes, res, 2); chsRemoteRes[2] = 0;
  bool newer = false;
  if (gen > chsGenNo) newer = true;
  else if (gen == chsGenNo) {
    size_t ll = strlen(chsMoves), rl = strlen(mv);
    if (rl > ll && strncmp(mv, chsMoves, ll) == 0 && (ll == 0 || mv[ll] == ' ')) newer = true;
    else if (rl == ll && !strcmp(mv, chsMoves) && res[0] && !chsRes[0]) newer = true;
  }
  if (!newer || strlen(mv) > CHS_BUF - 40) return;
  int ply = chsReplay(mv);
  if (ply < 0) { chsReplay(chsMoves); return; }       // pesan rusak: kembalikan posisi lokal
  bool oppMoved = (gen == chsGenNo) && ply > (int)chsPly;
  chsGenNo = (uint16_t)gen;
  strncpy(chsMoves, mv, CHS_BUF - 1); chsMoves[CHS_BUF - 1] = 0;
  chsPly = (uint16_t)ply;
  strncpy(chsRes, res, 2); chsRes[2] = 0;
  chsLastFromTok(chsMoves);
  chsSel = -1; chsPromoOn = false; chsConfirm = false;
  chsUpdateEnd();
  if (oppMoved) vibTap();
  needRedraw = true;
}

static const char* chsOppPid() { return chsRole == 1 ? chsBPid : chsWPid; }

static void chsRoleTick() {
  if (!chsReady) return;
  int8_t nr = 0;
  if (chsWPid[0] && !strcmp(chsWPid, chsPid)) nr = 1;
  else if (chsBPid[0] && !strcmp(chsBPid, chsPid)) nr = 2;
  else if (chsWPid[0] && chsBPid[0]) nr = 3;
  if (nr != chsRole) { chsRole = nr; needRedraw = true; }
  if (nr == 0) {
    if (chsClaimMs && millis() - chsClaimMs > 4000UL) { chsClaimW = chsClaimB = false; chsClaimMs = 0; }
    if (!chsPubSeat) {
      strncpy(chsPubName, chatName.c_str(), sizeof(chsPubName) - 1); chsPubName[sizeof(chsPubName) - 1] = 0;
      for (char* q = chsPubName; *q; q++) if (*q == '|') *q = '/';
      if (!chsWPid[0] && !chsClaimW) { chsPubSeat = 1; chsClaimW = true; chsClaimMs = millis(); }
      else if (chsWPid[0] && !chsBPid[0] && !chsClaimB) { chsPubSeat = 2; chsClaimB = true; chsClaimMs = millis(); }
    }
  }
}

static void chsProcessRx() {
  if (!chsMx || !chsMoves) return;
  bool fw = false, fb = false, fg = false;
  char tw[48], tb[48];
  if (xSemaphoreTake(chsMx, 0) != pdTRUE) return;
  if (chsRxWf) { strcpy(tw, chsRxW); chsRxWf = false; fw = true; }
  if (chsRxBf) { strcpy(tb, chsRxB); chsRxBf = false; fb = true; }
  if (chsRxGf) { strncpy(chsTmpG, chsRxG, CHS_BUF - 1); chsTmpG[CHS_BUF - 1] = 0; chsRxGf = false; fg = true; }
  xSemaphoreGive(chsMx);
  if (fw) { chsParseSeat(tw, chsWPid, chsWName); needRedraw = true; }
  if (fb) { chsParseSeat(tb, chsBPid, chsBName); needRedraw = true; }
  if (fg) chsAdoptRemote(chsTmpG);
  chsRoleTick();
  if (chsConn == 1 && chsReady && !chsSendG && (chsRole == 1 || chsRole == 2) && millis() - chsLastSendMs > 2500UL) {
    bool ahead = chsGenNo > chsRemoteGen ||
                 (chsGenNo == chsRemoteGen && (chsPly > chsRemotePly || (chsRes[0] && !chsRemoteRes[0])));
    if (ahead) chsQueueSend();            // kirim ulang kalau tadi gagal / baru tersambung lagi
  }
}

static bool chsOppOnline() {
  const char* pid = chsOppPid();
  if (!pid[0]) return false;
  for (int i = 0; i < 4; i++) if (!strcmp(chsOnPid[i], pid)) return chsOnVal[i] != 0;
  return false;
}

static bool chsCanMove() {
  return chsScr == 1 && chsConn == 1 && chsReady && (chsRole == 1 || chsRole == 2) &&
         chsOppPid()[0] && chsEnd == 0 && ((chsRole == 1) == chsWhite);
}

// ---------------------------------------------------------------------
// AKSI LOKAL
// ---------------------------------------------------------------------
static void chsCommit(uint16_t m) {
  char t[8]; chsMoveStr(m, t);
  size_t L = strlen(chsMoves);
  if (L + 8 > CHS_BUF - 40) return;
  if (L) chsMoves[L++] = ' ';
  strcpy(chsMoves + L, t);
  chsDo(m);
  chsPly++;
  chsLast = (m & 63) | (((m >> 6) & 63) << 6);
  chsSel = -1; chsPromoOn = false;
  chsUpdateEnd();
  vibTap();
  chsQueueSend();
  needRedraw = true;
}

static void chsNewGameLocal() {
  chsReset();
  chsMoves[0] = 0; chsPly = 0; chsRes[0] = 0; chsLast = -1; chsSel = -1;
  chsPromoOn = false; chsConfirm = false;
  chsUpdateEnd();
}

static void chsRematch() {
  chsGenNo++;
  chsNewGameLocal();
  chsQueueSend();
  needRedraw = true;
}

static void chsResign() {
  if (chsRole != 1 && chsRole != 2) return;
  strcpy(chsRes, chsRole == 1 ? "RW" : "RB");
  chsConfirm = false;
  chsUpdateEnd();
  chsQueueSend();
  needRedraw = true;
}

static void chsSanitizeRoom() {
  String t = "";
  for (unsigned i = 0; i < chsRoomStr.length() && t.length() < 8; i++) {
    char c = chsRoomStr[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) t += c;
  }
  chsRoomStr = t;
}

static void chsRandomRoom() {
  static const char al[] = "abcdefghjkmnpqrstuvwxyz23456789";
  String t = "";
  for (int i = 0; i < 4; i++) t += al[esp_random() % (sizeof(al) - 1)];
  chsRoomStr = t;
}

static void chsStartGame() {
  if (!chsMoves) { showToast("RAM tidak cukup utk Catur", 2200); return; }
  if (!chatConfigured()) { showToast("Isi broker di app Chat dulu", 2600); return; }
  chsSanitizeRoom();
  if (chsRoomStr.length() < 3) { showToast("Kode room minimal 3 karakter", 2200); return; }
  strncpy(chsRoom, chsRoomStr.c_str(), 9); chsRoom[9] = 0;
  { Preferences p; if (p.begin("chess", false)) { p.putString("room", chsRoomStr); p.end(); } }
  chsGenNo = 0; chsNewGameLocal();
  chsRole = 0; chsClaimW = chsClaimB = false; chsClaimMs = 0;
  chsWPid[0] = chsBPid[0] = chsWName[0] = chsBName[0] = 0;
  chsRemoteGen = 0; chsRemotePly = 0; chsRemoteRes[0] = 0;
  for (int i = 0; i < 4; i++) { chsOnPid[i][0] = 0; chsOnVal[i] = 0; }
  chsRxWf = chsRxBf = chsRxGf = false; chsSendG = false; chsPubSeat = 0;
  chsReady = false; chsConn = 0; chsErr[0] = 0;
  chsScr = 1;
  chsActive = true;
  needRedraw = true;
}

// ---------------------------------------------------------------------
// SIKLUS HIDUP APP
// ---------------------------------------------------------------------
void chessEnter() {
  if (!chsEnsureInit()) { showToast("RAM tidak cukup utk Catur", 2500); return; }
  if (!chatConfigured() && chatCfgFromSd()) { chatCfgSave(); showToast("Konfigurasi dimuat dari SD", 2000); }
  chsActive = false; chsConn = 0; chsReady = false;
  chsScr = 0; chsSel = -1; chsPromoOn = false; chsConfirm = false;
  chsReset(); chsEnd = 0;
}

void chessExit() {
  chsActive = false;        // task melepas koneksi TLS sendiri
  chsConn = 0;
}

void chessLoopPoll() {
  if (chsDirty) { chsDirty = false; if (curScreen() == SCR_CHESS) needRedraw = true; }
  if (curScreen() != SCR_CHESS || chsScr != 1 || !chsMoves) return;
  chsProcessRx();
  if (chsConfirm && millis() - chsConfirmMs > 3500UL) { chsConfirm = false; needRedraw = true; }
}

// ---------------------------------------------------------------------
// GAMBAR
// ---------------------------------------------------------------------
static void chsGeom(int* bx, int* by, int* sq, int* px, int* py, int* pw, int* ph) {
  *by = STATUS_H + 3;
  if (SCR_W >= SCR_H) {
    int s = (SCR_H - STATUS_H - 3 - BACK_H - 8) / 8;
    if (s < 18) s = 18;
    *sq = s; *bx = 6; *px = 6 + 8 * s + 8; *py = *by; *pw = SCR_W - *px - 6; *ph = SCR_H - *py - 6;
  } else {
    int s = (SCR_W - 8) / 8;
    int s2 = (SCR_H - STATUS_H - 3 - 6 - 74 - BACK_H - 8) / 8;
    if (s2 < s) s = s2;
    if (s < 18) s = 18;
    *sq = s; *bx = (SCR_W - 8 * s) / 2; *px = 6; *py = *by + 8 * s + 6; *pw = SCR_W - 12; *ph = backY() - 4 - *py;
  }
}

static void chsBtnRect(int i, int* x, int* y, int* w, int* h) {   // i = 0 kiri, 1 kanan
  int bx, by, sq, px, py, pw, ph;
  chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);
  int gap = (ph >= 120) ? 15 : 11;
  int bh = 22;
  int yy = py + ph - bh - 4;
  int minY = py + 4 * gap + 12;
  if (SCR_W >= SCR_H && yy > minY) yy = minY;
  int bw = (pw - 18) / 2;
  *x = px + 6 + i * (bw + 6); *y = yy; *w = bw; *h = bh;
}

static void chsPrintC(LGFX_Sprite& s, int x, int w, int y, const char* t) {
  s.setCursor(x + (w - (int)strlen(t) * 6) / 2, y); s.print(t);
}

static void chsClip(const char* src, char* dst, int maxCh) {
  int n = 0;
  while (src[n] && n < maxCh) { dst[n] = src[n]; n++; }
  dst[n] = 0;
}

// ---- bidak vektor (tanpa font) ----
#define CHS_U(v) (ox + (int)((v) * sq / 10.0f + 0.5f))
#define CHS_V(v) (oy + (int)((v) * sq / 10.0f + 0.5f))
#define CHS_L(v) ((int)((v) * sq / 10.0f + 0.5f))
static void chsOR(LGFX_Sprite& s, int x, int y, int w, int h, uint16_t f, uint16_t o) {
  s.fillRect(x - 1, y - 1, w + 2, h + 2, o); s.fillRect(x, y, w, h, f);
}
static void chsOC(LGFX_Sprite& s, int x, int y, int r, uint16_t f, uint16_t o) {
  s.fillCircle(x, y, r + 1, o); s.fillCircle(x, y, r, f);
}
static void chsOT(LGFX_Sprite& s, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t f, uint16_t o) {
  s.fillTriangle(x0, y0, x1, y1, x2, y2, f);
  s.drawLine(x0, y0, x1, y1, o); s.drawLine(x1, y1, x2, y2, o); s.drawLine(x2, y2, x0, y0, o);
}

static void chsDrawPiece(LGFX_Sprite& s, int t, bool white, int ox, int oy, int sq) {
  uint16_t f = white ? CHS_RGB(252, 252, 252) : CHS_RGB(40, 40, 46);
  uint16_t o = white ? CHS_RGB(30, 30, 30)    : CHS_RGB(215, 215, 220);
  switch (t) {
    case 1:  // pion
      chsOR(s, CHS_U(2.8), CHS_V(7.2), CHS_L(4.4), CHS_L(1.3), f, o);
      chsOT(s, CHS_U(5), CHS_V(3.6), CHS_U(3.3), CHS_V(7.2), CHS_U(6.7), CHS_V(7.2), f, o);
      chsOC(s, CHS_U(5), CHS_V(3.2), CHS_L(1.5), f, o);
      break;
    case 2:  // kuda
      chsOR(s, CHS_U(2.6), CHS_V(7.3), CHS_L(4.8), CHS_L(1.4), f, o);
      chsOT(s, CHS_U(3.0), CHS_V(7.3), CHS_U(7.0), CHS_V(7.3), CHS_U(6.2), CHS_V(3.2), f, o);
      chsOT(s, CHS_U(6.2), CHS_V(3.2), CHS_U(3.0), CHS_V(5.0), CHS_U(4.6), CHS_V(1.8), f, o);
      chsOT(s, CHS_U(6.2), CHS_V(3.2), CHS_U(4.6), CHS_V(1.8), CHS_U(6.9), CHS_V(2.4), f, o);
      s.fillCircle(CHS_U(5.0), CHS_V(3.4), (CHS_L(0.4) > 0 ? CHS_L(0.4) : 1), o);
      break;
    case 3:  // gajah
      chsOR(s, CHS_U(2.6), CHS_V(7.3), CHS_L(4.8), CHS_L(1.4), f, o);
      chsOT(s, CHS_U(5), CHS_V(3.6), CHS_U(3.2), CHS_V(7.3), CHS_U(6.8), CHS_V(7.3), f, o);
      chsOC(s, CHS_U(5), CHS_V(3.0), CHS_L(1.3), f, o);
      chsOC(s, CHS_U(5), CHS_V(1.4), CHS_L(0.6), f, o);
      s.drawLine(CHS_U(4.4), CHS_V(3.3), CHS_U(5.6), CHS_V(2.5), o);
      break;
    case 4:  // benteng
      chsOR(s, CHS_U(2.4), CHS_V(7.3), CHS_L(5.2), CHS_L(1.4), f, o);
      chsOR(s, CHS_U(3.2), CHS_V(3.4), CHS_L(3.6), CHS_L(4.0), f, o);
      chsOR(s, CHS_U(2.8), CHS_V(2.2), CHS_L(4.4), CHS_L(1.4), f, o);
      chsOR(s, CHS_U(2.8), CHS_V(1.0), CHS_L(1.1), CHS_L(1.2), f, o);
      chsOR(s, CHS_U(4.45), CHS_V(1.0), CHS_L(1.1), CHS_L(1.2), f, o);
      chsOR(s, CHS_U(6.1), CHS_V(1.0), CHS_L(1.1), CHS_L(1.2), f, o);
      break;
    case 5:  // menteri
      chsOR(s, CHS_U(2.4), CHS_V(7.3), CHS_L(5.2), CHS_L(1.4), f, o);
      chsOT(s, CHS_U(5), CHS_V(2.8), CHS_U(2.8), CHS_V(7.3), CHS_U(7.2), CHS_V(7.3), f, o);
      chsOC(s, CHS_U(2.9), CHS_V(2.6), CHS_L(0.75), f, o);
      chsOC(s, CHS_U(5.0), CHS_V(1.6), CHS_L(0.85), f, o);
      chsOC(s, CHS_U(7.1), CHS_V(2.6), CHS_L(0.75), f, o);
      break;
    default: // raja
      chsOR(s, CHS_U(2.4), CHS_V(7.3), CHS_L(5.2), CHS_L(1.4), f, o);
      chsOT(s, CHS_U(5), CHS_V(3.4), CHS_U(2.9), CHS_V(7.3), CHS_U(7.1), CHS_V(7.3), f, o);
      chsOC(s, CHS_U(5), CHS_V(4.0), CHS_L(1.2), f, o);
      chsOR(s, CHS_U(4.55), CHS_V(0.6), CHS_L(0.9), CHS_L(2.8), f, o);
      chsOR(s, CHS_U(3.75), CHS_V(1.3), CHS_L(2.5), CHS_L(0.9), f, o);
      break;
  }
}

static int chsSqAt(int i, int j) {            // baris/kolom layar -> petak papan (membalik utk Hitam)
  bool flip = (chsRole == 2);
  int rank = flip ? i : 7 - i, file = flip ? 7 - j : j;
  return rank * 8 + file;
}

static void chsTokAt(int idx, char* out) {     // token langkah ke-idx dari daftar langkah
  out[0] = 0;
  int n = 0; const char* p = chsMoves;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* q = p;
    while (*q && *q != ' ') q++;
    if (n == idx) { int L = (int)(q - p); if (L > 6) L = 6; memcpy(out, p, L); out[L] = 0; return; }
    n++; p = q;
  }
}

// 1 = aku menang, -1 = aku kalah, 0 = seri, 2 = tak ada hasil / penonton
static int chsResultForMe() {
  if (chsEnd == 0) return 2;
  if (chsEnd >= 2 && chsEnd <= 5) return 0;
  bool whiteWon = (chsEnd == 1) ? !chsWhite : (chsRes[1] == 'B');
  if (chsRole != 1 && chsRole != 2) return 2;
  return (whiteWon == (chsRole == 1)) ? 1 : -1;
}

static const char* chsEndText() {
  switch (chsEnd) {
    case 1: return chsWhite ? "Skakmat! Hitam menang" : "Skakmat! Putih menang";
    case 2: return "Seri: pat (stalemate)";
    case 3: return "Seri: aturan 50 langkah";
    case 4: return "Seri: bidak tidak cukup";
    case 5: return "Seri: batas 280 langkah";
    case 6: return chsRes[1] == 'W' ? "Putih menyerah. Hitam menang" : "Hitam menyerah. Putih menang";
  }
  return "";
}

static void chsDrawSetup(LGFX_Sprite& s) {
  bool typing = kbVisible && kbTarget == &chsRoomStr;
  s.setTextColor(T().accent); s.setTextSize(2); s.setCursor(10, 25); s.print("Catur Online");
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(10, 46); s.print("Dua HP isi kode room yang sama, lalu Mulai");
  iosRR(s, 8, 62, SCR_W - 16, 34, 8, T().surface);
  s.setTextColor(T().subtext); s.setCursor(16, 67); s.print("Kode room");
  String shown = chsRoomStr; if (typing) shown += "_";
  s.setTextSize(2); s.setTextColor(chsRoomStr.length() || typing ? T().text : T().subtext);
  s.setCursor(16, 78); s.print(shown.length() ? shown.c_str() : "ketuk utk isi");
  s.setTextSize(1);
  String nm = chatName.length() ? chatName : String("HP");
  s.setTextColor(T().subtext); s.setCursor(10, 102); s.print("Pemain: "); s.setTextColor(T().text); s.print(nm.c_str());
  s.setCursor(10, 116);
  if (chatConfigured()) { s.setTextColor(T().good); char hb[40]; chsClip(chatCleanHost().c_str(), hb, (SCR_W - 70) / 6); s.print("Broker: "); s.print(hb); }
  else { s.setTextColor(T().danger); s.print("Broker belum diatur - isi di app Chat dulu"); }
  iosRR(s, 8, 134, 70, 26, 7, T().surface2);
  s.setTextColor(T().accent); chsPrintC(s, 8, 70, 143, "Acak");
  bool ok = chatConfigured() && chsRoomStr.length() >= 3;
  iosRR(s, 86, 134, SCR_W - 94, 26, 7, ok ? (uint16_t)T().accent : (uint16_t)T().surface2);
  s.setTextColor(ok ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, 86, SCR_W - 94, 143, "Mulai");
}

static void chsDrawGame(LGFX_Sprite& s) {
  int bx, by, sq, px, py, pw, ph;
  chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);
  bool flip = (chsRole == 2);
  int kingSq = -1;
  if (chsEnd == 0 || chsEnd == 1) { if (chsInCheck()) kingSq = chsKingSq(chsWhite); }
  uint16_t hiLast = 0xFFE0;
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
    int q = chsSqAt(i, j);
    int rank = q >> 3, file = q & 7;
    int x = bx + j * sq, y = by + i * sq;
    uint16_t c = ((rank + file) & 1) ? CHS_LIGHT : CHS_DARK;
    if (chsLast >= 0 && (q == (chsLast & 63) || q == ((chsLast >> 6) & 63))) c = blend565(c, hiLast, 110);
    if (q == chsSel) c = blend565(c, T().accent, 150);
    if (q == kingSq) c = blend565(c, T().danger, 190);
    s.fillRect(x, y, sq, sq, c);
    if (sq >= 26) {
      s.setTextSize(1);
      s.setTextColor((rank + file) & 1 ? CHS_DARK : CHS_LIGHT);
      char cl[2]; cl[1] = 0;
      if (i == 7) { cl[0] = (char)('a' + file); s.setCursor(x + sq - 7, y + sq - 9); s.print(cl); }
      if (j == 0) { cl[0] = (char)('1' + rank); s.setCursor(x + 2, y + 2); s.print(cl); }
    }
    int8_t p = chsBd[q];
    if (p) chsDrawPiece(s, p > 0 ? p : -p, p > 0, x, y, sq);
    if (chsSel >= 0 && chsHL[q]) {
      uint16_t dc = blend565(c, T().good, 210);
      if (p) { s.drawCircle(x + sq / 2, y + sq / 2, sq / 2 - 2, dc); s.drawCircle(x + sq / 2, y + sq / 2, sq / 2 - 3, dc); }
      else s.fillCircle(x + sq / 2, y + sq / 2, sq / 6 + 1, dc);
    }
  }
  (void)flip;

  if (chsPromoOn) {                                   // pilih bidak promosi
    int w = 4 * sq + 10, h = sq + 10, x0 = bx + 4 * sq - w / 2, y0 = by + 4 * sq - h / 2;
    iosRR(s, x0, y0, w, h, 8, T().surface);
    nyxAaRRRing(s, x0, y0, w, h, 8, T().accent);
    bool wh = (chsRole == 1);
    static const int pr[4] = {5, 4, 3, 2};
    for (int k = 0; k < 4; k++) {
      s.fillRect(x0 + 5 + k * sq, y0 + 5, sq, sq, ((k & 1) ? CHS_DARK : CHS_LIGHT));
      chsDrawPiece(s, pr[k], wh, x0 + 5 + k * sq, y0 + 5, sq);
    }
  }

  // ---- panel ----
  iosRR(s, px, py, pw, ph, 8, T().surface);
  int gap = (ph >= 120) ? 15 : 11;
  int maxCh = (pw - 12) / 6;
  char line[48], clipd[48];
  int ty = py + 5;
  s.setTextSize(1);
  // baris status
  uint16_t sc = T().accent;
  if (chsConn == 2)               { chsClip(chsErr[0] ? chsErr : "Terputus", line, 46); sc = T().danger; }
  else if (chsConn == 0 || !chsReady) { strcpy(line, "Menyambung..."); sc = T().subtext; }
  else if (chsRole == 0)          { strcpy(line, "Mencari kursi..."); sc = T().subtext; }
  else if (chsRole == 3)          { strcpy(line, "Room penuh, pakai kode lain"); sc = T().danger; }
  else if (!chsOppPid()[0])       { strcpy(line, "Menunggu lawan bergabung..."); sc = T().subtext; }
  else if (chsEnd) {
    strcpy(line, chsEndText());
    int rr = chsResultForMe(); sc = rr == 1 ? T().good : rr == -1 ? T().danger : T().text;
  }
  else if (chsCanMove())          { strcpy(line, chsInCheck() ? "Giliranmu - SKAK!" : "Giliranmu"); sc = chsInCheck() ? T().danger : T().good; }
  else                            { strcpy(line, "Giliran lawan"); sc = T().subtext; }
  chsClip(line, clipd, maxCh);
  s.setTextColor(sc); s.setCursor(px + 6, ty); s.print(clipd); ty += gap;
  // kamu
  bool meW = (chsRole != 2);
  const char* myN = chsRole == 2 ? chsBName : chsWName;
  char nb[40]; snprintf(nb, sizeof(nb), "Kamu : %s", myN[0] ? myN : (chatName.length() ? chatName.c_str() : "HP"));
  chsClip(nb, clipd, maxCh - 3);
  s.setTextColor(T().text); s.setCursor(px + 6, ty); s.print(clipd);
  s.fillRect(px + pw - 14, ty, 8, 8, meW ? CHS_RGB(252, 252, 252) : CHS_RGB(40, 40, 46));
  s.drawRect(px + pw - 14, ty, 8, 8, T().subtext);
  ty += gap;
  // lawan
  const char* opN = chsRole == 2 ? chsWName : chsBName;
  snprintf(nb, sizeof(nb), "Lawan: %s", chsOppPid()[0] ? (opN[0] ? opN : "?") : "-");
  chsClip(nb, clipd, maxCh - 3);
  s.setTextColor(T().text); s.setCursor(px + 6, ty); s.print(clipd);
  s.fillCircle(px + pw - 11, ty + 4, 3, chsOppOnline() ? (uint16_t)T().good : (uint16_t)T().subtext);
  ty += gap;
  // room + langkah
  char lt[8]; chsTokAt(chsPly - 1, lt);
  snprintf(nb, sizeof(nb), "Room %s  #%u %s", chsRoom, (unsigned)chsPly, lt);
  chsClip(nb, clipd, maxCh);
  s.setTextColor(T().subtext); s.setCursor(px + 6, ty);
  s.print(clipd);

  // tombol
  int x, y, w, h;
  chsBtnRect(0, &x, &y, &w, &h);
  bool over = chsEnd != 0;
  bool canAct = (chsRole == 1 || chsRole == 2) && chsConn == 1;
  if (over) {
    iosRR(s, x, y, w, h, 6, canAct ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(canAct ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, x, w, y + 7, "Main lagi");
  } else {
    iosRR(s, x, y, w, h, 6, chsConfirm ? (uint16_t)T().danger : (uint16_t)T().surface2);
    s.setTextColor(chsConfirm ? (uint16_t)T().bg : (uint16_t)T().danger); chsPrintC(s, x, w, y + 7, chsConfirm ? "Yakin?" : "Menyerah");
  }
  chsBtnRect(1, &x, &y, &w, &h);
  iosRR(s, x, y, w, h, 6, T().surface2);
  s.setTextColor(T().accent); chsPrintC(s, x, w, y + 7, "Menu");

  // daftar langkah (kalau ada ruang)
  int ly0 = y + h + 8;
  int rows = (py + ph - 4 - ly0) / 10;
  if (rows >= 2 && pw >= 110) {
    int pairs = (chsPly + 1) / 2;
    int first = pairs - rows; if (first < 0) first = 0;
    s.setTextColor(T().subtext);
    for (int k = first; k < pairs; k++) {
      char a[8], b[8]; chsTokAt(2 * k, a); chsTokAt(2 * k + 1, b);
      char ln[28]; snprintf(ln, sizeof(ln), "%2d. %-5s %-5s", k + 1, a, b);
      s.setCursor(px + 8, ly0 + (k - first) * 10); s.print(ln);
    }
  }
}

void drawChess(LGFX_Sprite& s) {
  s.fillSprite(T().bg);
  drawStatusBar(s);
  if (!chsMoves) {
    s.setTextColor(T().danger); s.setTextSize(1); s.setCursor(10, 40); s.print("RAM tidak cukup untuk app Catur");
  } else if (chsScr == 0) chsDrawSetup(s);
  else chsDrawGame(s);
  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// SENTUHAN
// ---------------------------------------------------------------------
static void chsTouchSetup(int x, int y, bool isNew) {
  if (!isNew) return;
  if (kbVisible) {
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; chsSanitizeRoom(); }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }
  if (isBack(x, y)) { navBack(); return; }
  if (y >= 62 && y <= 96) { kbTarget = &chsRoomStr; kbVisible = true; kbMode = KB_LOWER; needRedraw = true; return; }
  if (y >= 134 && y <= 160) {
    if (x >= 8 && x <= 78) { chsRandomRoom(); needRedraw = true; }
    else if (x >= 86) chsStartGame();
  }
}

static void chsTapBoard(int q) {
  if (!chsCanMove()) { if (chsSel >= 0) { chsSel = -1; needRedraw = true; } return; }
  int8_t p = chsBd[q];
  bool mine = p && ((p > 0) == (chsRole == 1));
  if (chsSel >= 0 && chsHL[q]) {
    uint16_t mv[64];
    int n = chsLegalFrom(chsSel, mv);
    int promoCnt = 0;
    uint16_t pick = 0; bool found = false;
    for (int i = 0; i < n; i++) if (((mv[i] >> 6) & 63) == q) { promoCnt++; if (!found) { pick = mv[i]; found = true; } }
    if (!found) { chsSel = -1; needRedraw = true; return; }
    if (promoCnt > 1) { chsPromoOn = true; chsPromoFrom = chsSel; chsPromoTo = q; needRedraw = true; return; }
    chsCommit(pick);
    return;
  }
  if (mine) {
    chsSel = q;
    memset(chsHL, 0, sizeof(chsHL));
    uint16_t mv[64];
    int n = chsLegalFrom(q, mv);
    for (int i = 0; i < n; i++) chsHL[(mv[i] >> 6) & 63] = true;
    vibTap();
  } else chsSel = -1;
  needRedraw = true;
}

void chessTouch(int x, int y, bool held, bool isNew) {
  if (!chsMoves) { if (isNew && isBack(x, y)) navBack(); return; }
  if (chsScr == 0) { chsTouchSetup(x, y, isNew); return; }
  if (!isNew) return;
  if (isBack(x, y)) { navBack(); return; }

  int bx, by, sq, px, py, pw, ph;
  chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);

  if (chsPromoOn) {
    int w = 4 * sq + 10, h = sq + 10, x0 = bx + 4 * sq - w / 2, y0 = by + 4 * sq - h / 2;
    static const int pr[4] = {5, 4, 3, 2};
    if (x >= x0 + 5 && x < x0 + 5 + 4 * sq && y >= y0 + 5 && y < y0 + 5 + sq) {
      int k = (x - (x0 + 5)) / sq;
      uint16_t m = (uint16_t)(chsPromoFrom | (chsPromoTo << 6) | (pr[k] << 12));
      chsCommit(m);
    } else { chsPromoOn = false; chsSel = -1; needRedraw = true; }
    return;
  }

  int bxr, byr, bw, bh;
  chsBtnRect(0, &bxr, &byr, &bw, &bh);
  if (x >= bxr && x < bxr + bw && y >= byr && y < byr + bh) {
    if (chsEnd != 0) { if ((chsRole == 1 || chsRole == 2) && chsConn == 1) chsRematch(); }
    else if (chsRole == 1 || chsRole == 2) {
      if (chsConfirm) chsResign();
      else { chsConfirm = true; chsConfirmMs = millis(); needRedraw = true; }
    }
    return;
  }
  chsBtnRect(1, &bxr, &byr, &bw, &bh);
  if (x >= bxr && x < bxr + bw && y >= byr && y < byr + bh) {
    chsActive = false; chsScr = 0; chsConn = 0; chsReady = false; chsSel = -1; chsConfirm = false;
    needRedraw = true;
    return;
  }

  if (x >= bx && x < bx + 8 * sq && y >= by && y < by + 8 * sq) {
    int j = (x - bx) / sq, i = (y - by) / sq;
    chsTapBoard(chsSqAt(i, j));
  }
}
