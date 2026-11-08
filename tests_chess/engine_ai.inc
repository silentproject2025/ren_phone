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
// ==== AI BEGIN ====
// Mesin AI + model GAYA PEMAIN yang belajar online. Dipasang di atas mesin aturan (papan global chsBd, dst),
// tanpa Arduino/LovyanGFX -> diuji di PC (tests_chess/). HANYA dipanggil dari loop UI: pencarian memakai-ulang
// papan global lalu mengembalikannya persis seperti semula.
//
// EMPAT BAGIAN
//  1. Pencarian negamax PVS + quiescence (delta pruning) + tabel transposisi 64 bit + null-move + LMR + futility +
//     killer/history + deteksi pengulangan di jalur; evaluasi materi/PST + struktur pion + mobilitas + perisai raja +
//     mop-up endgame. Lima tingkat lewat chsAiChoose(). Penjaga stack (CHS_AI_STACK_MAX) menjaga rekursi < 8 KB task loop.
//  2. TinyML: jaringan saraf kecil (MLP 26 -> 16 -> 1, 449 parameter, float32) yg menilai "seberapa mirip
//     langkah ini dgn pilihan user". Masukan = 20 fitur langkah + 6 konteks posisi (fase, materi, skak, nomor
//     langkah, mobilitas, ada menteri). Dilatih ONLINE di ESP32 sendiri: tiap langkah USER = satu langkah SGD
//     (backprop, softmax atas semua langkah sah), ditambah replay beberapa contoh lama dari buffer pengalaman.
//  3. Buku pembuka pribadi: posisi -> langkah yg paling sering user mainkan (chsBk*).
//  4. Tabel KEBIASAAN PETAK (chsHab): skor jarang [jenis bidak x petak tujuan] + [petak asal], dijumlahkan dgn skor MLP dan
//     dilatih oleh gradien softmax yg sama. Menangkap kebiasaan khas (\"selalu e4\", \"kuda ke f3\", \"rokade cepat\") yg sulit
//     dipelajari MLP kecil dari sedikit data. Petak dicerminkan utk Hitam. Hanya 448 bobot -> update murah.
//  Pengalaman terbaru (CHS_RB_KEEP posisi) ikut tersimpan ke NVS dlm bentuk padat supaya replay tetap jalan setelah keluar app.
// Mode 2 (gaya) hanya memilih di antara langkah yg nilai pencariannya masih dekat terbaik (margin), jadi AI yg
// menggantikan user saat AFK meniru gayanya TANPA membuang partai -> tetap adil utk lawan.
#ifndef CHS_AI_NOW
#define CHS_AI_NOW() millis()
#endif
#ifndef CHS_AI_AFTER_ALLOC
#define CHS_AI_AFTER_ALLOC()           // hook: dipanggil sekali setelah memori kerja AI siap (muat pengalaman dari NVS)
#endif
#ifndef CHS_AI_RAND
#define CHS_AI_RAND() ((uint32_t)esp_random())
#endif
#ifndef CHS_AI_ALLOC
#define CHS_AI_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif
#ifndef CHS_AI_FREE
#define CHS_AI_FREE(p) free(p)
#endif

#define CHS_NF        20                       // fitur langkah
#define CHS_NC        6                        // fitur konteks posisi
#define CHS_NIN       (CHS_NF + CHS_NC)
#define CHS_NH        16                       // neuron tersembunyi
#define CHS_NP        (CHS_NH * CHS_NIN + CHS_NH + CHS_NH + 1)   // W1, b1, v, b2
#define CHS_RB_N      96                       // buffer pengalaman (posisi + langkah user)
#define CHS_RB_SZ     72
#define CHS_RB_KEEP   40                       // berapa posisi terbaru yg ikut disimpan ke NVS
#define CHS_RB_REC    40                       // ukuran satu posisi dlm bentuk padat
#define CHS_RB_MAXLEN (4 + CHS_RB_REC * CHS_RB_KEEP)
#define CHS_AI_INF    32000
#define CHS_AI_MATE   30000
#define CHS_AI_PLY    32                       // batas keras kedalaman rekursi: total stack <= PLY x ~230 B (< 8 KB task loop)
#define CHS_AI_HIST   320
#define CHS_AI_MAXD   14                       // kedalaman iterasi maksimum (dibatasi supaya rekursi + perpanjangan skak muat di PLY)
#define CHS_BK_N      128
#ifndef CHS_TT_BITS
#define CHS_TT_BITS   16                       // 2^16 entri x 12 byte = 768 KB (PSRAM); 0 = tanpa tabel transposisi
#endif

static const int16_t chsAiVal[7] = {0, 100, 320, 330, 500, 900, 0};
// Tabel petak-bidak (Michniewski), ditulis dari sudut pandang Putih: baris pertama = rank 8.
static const int8_t chsPst[6][64] = {
  { 0,  0,  0,  0,  0,  0,  0,  0,   50, 50, 50, 50, 50, 50, 50, 50,   10, 10, 20, 30, 30, 20, 10, 10,    5,  5, 10, 25, 25, 10,  5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,    5, -5,-10,  0,  0,-10, -5,  5,    5, 10, 10,-20,-20, 10, 10,  5,    0,  0,  0,  0,  0,  0,  0,  0 },
  {-50,-40,-30,-30,-30,-30,-40,-50,  -40,-20,  0,  0,  0,  0,-20,-40,  -30,  0, 10, 15, 15, 10,  0,-30,  -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,  -30,  5, 10, 15, 15, 10,  5,-30,  -40,-20,  0,  5,  5,  0,-20,-40,  -50,-40,-30,-30,-30,-30,-40,-50 },
  {-20,-10,-10,-10,-10,-10,-10,-20,  -10,  0,  0,  0,  0,  0,  0,-10,  -10,  0,  5, 10, 10,  5,  0,-10,  -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,  -10, 10, 10, 10, 10, 10, 10,-10,  -10,  5,  0,  0,  0,  0,  5,-10,  -20,-10,-10,-10,-10,-10,-10,-20 },
  {  0,  0,  0,  0,  0,  0,  0,  0,    5, 10, 10, 10, 10, 10, 10,  5,   -5,  0,  0,  0,  0,  0,  0, -5,   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,   -5,  0,  0,  0,  0,  0,  0, -5,   -5,  0,  0,  0,  0,  0,  0, -5,    0,  0,  0,  5,  5,  0,  0,  0 },
  {-20,-10,-10, -5, -5,-10,-10,-20,  -10,  0,  0,  0,  0,  0,  0,-10,  -10,  0,  5,  5,  5,  5,  0,-10,   -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,  -10,  5,  5,  5,  5,  5,  0,-10,  -10,  0,  5,  0,  0,  0,  0,-10,  -20,-10,-10, -5, -5,-10,-10,-20 },
  {-30,-40,-40,-50,-50,-40,-40,-30,  -30,-40,-40,-50,-50,-40,-40,-30,  -30,-40,-40,-50,-50,-40,-40,-30,  -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,  -10,-20,-20,-20,-20,-20,-20,-10,   20, 20,  0,  0,  0,  0, 20, 20,   20, 30, 10,  0,  0, 10, 30, 20 }
};
static const int8_t chsPstKe[64] = {
  -50,-40,-30,-20,-20,-30,-40,-50,  -30,-20,-10,  0,  0,-10,-20,-30,  -30,-10, 20, 30, 30, 20,-10,-30,  -30,-10, 30, 40, 40, 30,-10,-30,
  -30,-10, 30, 40, 40, 30,-10,-30,  -30,-10, 20, 30, 30, 20,-10,-30,  -30,-30,  0,  0,  0,  0,-30,-30,  -50,-30,-30,-30,-30,-30,-30,-50
};

// ---- memori kerja (PSRAM, dialokasi saat dipakai, dibebaskan saat keluar app) ----
static uint16_t* chsAiMv = nullptr;    // [CHS_AI_PLY][256] daftar langkah per kedalaman
static int16_t*  chsAiKy = nullptr;    // [CHS_AI_PLY][256] kunci urutan
static uint16_t* chsAiRm = nullptr;    // 256 langkah sah di akar
static int32_t*  chsAiSc = nullptr;    // skor iterasi berjalan
static int32_t*  chsAiFs = nullptr;    // skor iterasi terakhir yg SELESAI
static uint8_t*  chsAiEx = nullptr;    // skor berjalan eksak?
static uint8_t*  chsAiFx = nullptr;    // skor final eksak?
static float*    chsAiFB = nullptr;    // [256][CHS_NF] fitur langkah
static float*    chsAiFS = nullptr;    // 256 skor
static float*    chsAiG = nullptr;     // gradien CHS_NP
static uint8_t*  chsAiRb = nullptr;    // buffer pengalaman
static uint32_t* chsAiHist = nullptr;  // hash posisi sepanjang partai
static uint8_t*  chsAiMem = nullptr;
struct ChsTT { uint32_t k; int16_t s; uint16_t m; int8_t d; uint8_t f; uint8_t a; uint8_t pad; };   // kunci, skor, langkah, kedalaman, jenis (0 eksak 1 batas bawah 2 batas atas), umur
static ChsTT*    chsAiTT = nullptr;
static uint32_t  chsAiTTMask = 0;
static uint8_t   chsAiAge = 0;
static uint32_t* chsAiPath = nullptr;  // [CHS_AI_PLY + 2] hash jalur pencarian (deteksi pengulangan)
static uint16_t  chsAiKil[CHS_AI_PLY + 2][2];
static int16_t*  chsAiHis = nullptr;   // [2][64][64] history heuristic

static uint32_t chsAiNodes = 0, chsAiDeadline = 0;
static bool     chsAiAbort = false, chsAiCheckTime = false;
static int      chsAiHistN = 0;

// ---- model gaya + buku pembuka pribadi (disimpan ke NVS oleh pembungkus di bawah) ----
static float    chsNet[CHS_NP];
static bool     chsNetReady = false;
#define CHS_HAB_N     (6 * 64 + 64)              // kebiasaan petak: [bidak 6][petak tujuan 64] + [petak asal 64]
#define CHS_HAB_LEN   (4 + 2 * CHS_HAB_N)
static float    chsHab[CHS_HAB_N];
static float    chsAcc = 0.0f;                   // akurasi tebakan (rata-rata bergerak): apakah pilihan teratas model = langkah user?
static int      chsAccN = 0;                     // berapa langkah yg sudah dihitung (maks 20)
static int8_t   chsLastHit = -1;                 // hasil tebakan di langkah belajar terakhir (-1 = tidak dihitung)
static bool     chsRbDirty = false;
static uint32_t chsTrainN = 0;
static int      chsRbN = 0, chsRbPos = 0;
static bool     chsStyleDirty = false;
static uint32_t chsBkH[CHS_BK_N];
static uint16_t chsBkM[CHS_BK_N];
static uint8_t  chsBkC[CHS_BK_N];
static int      chsBkN = 0;
static uint16_t chsBkNext = 0;

// ---- NYX v2: model lawan = Product-of-Experts (regresi logit bersyarat), dilatih online dgn Newton rekursif ----
// logit(langkah) = th0*skor_gaya + th1*keuntungan_taktis(quiescence) + th2*keserakahan(nilai bidak yg dimakan)
// th (3 bobot) + A (presisi 3x3 simetris: a00 a01 a02 a11 a12 a22) + ig (perolehan informasi, nat/langkah) disimpan di NVS \"mix\" (44 byte).
#ifndef CHS_MX_KAPPA
#define CHS_MX_KAPPA   0.010f                  // aversi risiko (per centipawan) utk soft-min entropik
#endif
#ifndef CHS_MX_IG0
#define CHS_MX_IG0     1.2f                    // perolehan informasi (nat/langkah) yg dianggap \"model sudah layak dipercaya penuh\"
#endif
#define CHS_MX_RHO     0.97f                   // faktor lupa presisi (gaya pemain bisa berubah)
#define CHS_MX_LEN     44
static const float chsMxTh0[3] = {1.0f, 0.0f, 0.0f};
static const float chsMxA0[6]  = {2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};
static float    chsMxTh[3] = {1.0f, 0.0f, 0.0f};
static float    chsMxA[6]  = {2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};
static float    chsMxIg = 0.0f;                // rata-rata bergerak ln(p_model(langkah user)) - ln(1/n)
static uint16_t chsMxN = 0;
static bool     chsMxLive = true;              // false = lewati pengamatan (hemat waktu / uji)
static float*   chsAiQv = nullptr;             // 256 nilai taktis langkah (scratch)
static float*   chsAiGv = nullptr;             // 256 keserakahan langkah (scratch)

static bool chsAiEnsure() {
  if (chsAiMem) return true;
  size_t szMv = (size_t)CHS_AI_PLY * 256 * 2, szFB = (size_t)256 * CHS_NIN * 4;
  size_t szPath = (size_t)(CHS_AI_PLY + 2) * 4, szHis = (size_t)2 * 4096 * 2;
  size_t total = szMv * 2 + 512 + 1024 * 2 + 256 * 2 + szFB + 1024 + (size_t)CHS_AI_HIST * 4 + (size_t)CHS_NP * 4 + 2048 + (size_t)CHS_RB_N * CHS_RB_SZ + 64 + szPath + szHis;
  uint8_t* m = (uint8_t*)CHS_AI_ALLOC(total);
  if (!m) m = (uint8_t*)malloc(total);
  if (!m) return false;
  memset(m, 0, total);
  chsAiMem = m;
  uint8_t* p = m;
  chsAiMv = (uint16_t*)p; p += szMv;
  chsAiKy = (int16_t*)p;  p += szMv;
  chsAiRm = (uint16_t*)p; p += 512;
  chsAiSc = (int32_t*)p;  p += 1024;
  chsAiFs = (int32_t*)p;  p += 1024;
  chsAiEx = p; p += 256;
  chsAiFx = p; p += 256;
  chsAiFB = (float*)p;    p += szFB;
  chsAiFS = (float*)p;    p += 1024;
  chsAiHist = (uint32_t*)p; p += (size_t)CHS_AI_HIST * 4;
  chsAiG = (float*)p;     p += (size_t)CHS_NP * 4;
  chsAiQv = (float*)p;    p += 1024;
  chsAiGv = (float*)p;    p += 1024;
  chsAiPath = (uint32_t*)p; p += szPath;
  chsAiHis = (int16_t*)p;   p += szHis;
  chsAiRb = p;
  chsRbN = 0; chsRbPos = 0;
#if CHS_TT_BITS > 0
  if (!chsAiTT) {                                                  // tabel transposisi: gagal alokasi -> pencarian tetap jalan tanpa TT
    size_t cnt = (size_t)1 << CHS_TT_BITS;
    chsAiTT = (ChsTT*)CHS_AI_ALLOC(cnt * sizeof(ChsTT));
    if (chsAiTT) { memset(chsAiTT, 0, cnt * sizeof(ChsTT)); chsAiTTMask = (uint32_t)(cnt - 1); }
  }
#endif
  CHS_AI_AFTER_ALLOC();
  return true;
}
static void chsAiFree() {
  if (chsAiMem) CHS_AI_FREE(chsAiMem);
  if (chsAiTT) CHS_AI_FREE(chsAiTT);
  chsAiTT = nullptr; chsAiTTMask = 0; chsAiPath = nullptr; chsAiHis = nullptr;
  chsAiMem = nullptr; chsAiMv = nullptr; chsAiKy = nullptr; chsAiRm = nullptr; chsAiSc = nullptr; chsAiFs = nullptr;
  chsAiEx = nullptr; chsAiFx = nullptr; chsAiFB = nullptr; chsAiFS = nullptr; chsAiHist = nullptr; chsAiG = nullptr; chsAiQv = nullptr; chsAiGv = nullptr; chsAiRb = nullptr; chsRbN = 0; chsRbPos = 0; chsAiHistN = 0;
}

static inline int chsAbsI(int v) { return v < 0 ? -v : v; }

static uint32_t chsPosHash() {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 64; i++) { h ^= (uint8_t)chsBd[i]; h *= 16777619u; }
  h ^= chsWhite ? 1u : 2u;          h *= 16777619u;
  h ^= chsCastle;                   h *= 16777619u;
  h ^= (uint8_t)(chsEp + 1);        h *= 16777619u;
  return h;
}

#define CHS_SAVE_ST  int8_t _sb[64]; memcpy(_sb, chsBd, 64); bool _sw = chsWhite; uint8_t _sc = chsCastle; int8_t _se = chsEp; uint16_t _sh = chsHalf
#define CHS_REST_ST  do { memcpy(chsBd, _sb, 64); chsWhite = _sw; chsCastle = _sc; chsEp = _se; chsHalf = _sh; } while (0)
// Versi per-ply utk rekursi pencarian: salinan papan ada di array global (RAM internal), bukan di stack -> frame lebih kecil.
struct ChsSv { bool w; uint8_t c; int8_t e; uint16_t h; };
static int8_t chsAiSvB[CHS_AI_PLY + 2][64];
static ChsSv  chsAiSvS[CHS_AI_PLY + 2];
#define CHS_SAVE_AT(pl) do { memcpy(chsAiSvB[pl], chsBd, 64); chsAiSvS[pl].w = chsWhite; chsAiSvS[pl].c = chsCastle; chsAiSvS[pl].e = chsEp; chsAiSvS[pl].h = chsHalf; } while (0)
#define CHS_REST_AT(pl) do { memcpy(chsBd, chsAiSvB[pl], 64); chsWhite = chsAiSvS[pl].w; chsCastle = chsAiSvS[pl].c; chsEp = chsAiSvS[pl].e; chsHalf = chsAiSvS[pl].h; } while (0)

// ---------------------------------------------------------------- evaluasi (dari sudut pandang yg jalan)
// Materi + tabel petak + struktur pion (ganda, terisolasi, bebas), pasangan gajah, benteng di file terbuka / baris 7,
// mobilitas kuda/gajah/benteng, perlindungan raja (perisai pion), dan "mop-up" endgame (dorong raja lawan ke tepi
// saat unggul) supaya kemenangan teknis (mis. K+B vs K) benar-benar dituntaskan, bukan cuma berputar-putar.
static const int16_t chsEvPass[8] = {0, 8, 14, 24, 42, 72, 120, 0};
static const int8_t  chsEvKn[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
static const int8_t  chsEvDr[8][2] = {{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
static int chsEvMob(int sq, int8_t p, int t) {
  int sg = p > 0 ? 1 : -1, r = sq >> 3, f = sq & 7, cnt = 0;
  if (t == 2) {
    for (int k = 0; k < 8; k++) {
      int rr = r + chsEvKn[k][0], ff = f + chsEvKn[k][1];
      if (rr < 0 || rr > 7 || ff < 0 || ff > 7) continue;
      if (chsBd[rr * 8 + ff] * sg <= 0) cnt++;
    }
    return cnt;
  }
  int k0 = (t == 3) ? 0 : 4;
  for (int k = k0; k < k0 + 4; k++) {
    int rr = r + chsEvDr[k][0], ff = f + chsEvDr[k][1];
    while (rr >= 0 && rr <= 7 && ff >= 0 && ff <= 7) {
      int q = chsBd[rr * 8 + ff] * sg;
      if (q == 0) cnt++; else { if (q < 0) cnt++; break; }
      rr += chsEvDr[k][0]; ff += chsEvDr[k][1];
    }
  }
  return cnt;
}
static inline int chsEvMop(int lk, int wk) {            // lk = raja pihak yg kalah, wk = raja pihak yg menang
  int lr = lk >> 3, lf = lk & 7, wr = wk >> 3, wf = wk & 7;
  int cd = chsAbsI(2 * lr - 7) + chsAbsI(2 * lf - 7);
  int md = chsAbsI(lr - wr) + chsAbsI(lf - wf);
  return cd * 6 + (14 - md) * 3;
}
static int chsAiEval() {
  int sc = 0, npm = 0, mat[2] = {0, 0};
  int8_t pcnt[2][8], bmax[8], wmin[8];
  for (int f = 0; f < 8; f++) { pcnt[0][f] = pcnt[1][f] = 0; bmax[f] = -1; wmin[f] = 8; }
  int ksq[2] = {-1, -1}, nb[2] = {0, 0}, np[2] = {0, 0}, nr[2] = {0, 0};
  int8_t pawnSq[2][8], rks[2][4];
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = chsBd[sq]; if (!p) continue;
    int t = p > 0 ? p : -p, w = p > 0 ? 0 : 1, r = sq >> 3, f = sq & 7;
    if (t == 6) { ksq[w] = sq; continue; }
    mat[w] += chsAiVal[t];
    if (t >= 2 && t <= 5) npm += chsAiVal[t];
    int idx = w == 0 ? (7 - r) * 8 + f : r * 8 + f;
    int v = chsAiVal[t] + chsPst[t - 1][idx];
    if (t == 1) {
      pcnt[w][f]++; if (np[w] < 8) pawnSq[w][np[w]++] = (int8_t)sq;
      if (w == 0) { if (r < wmin[f]) wmin[f] = (int8_t)r; } else { if (r > bmax[f]) bmax[f] = (int8_t)r; }
    } else if (t == 2) { v += (chsEvMob(sq, p, t) - 4) * 4;
    } else if (t == 3) { v += (chsEvMob(sq, p, t) - 6) * 3; nb[w]++;
    } else if (t == 4) { v += (chsEvMob(sq, p, t) - 7) * 2; if (nr[w] < 4) rks[w][nr[w]++] = (int8_t)sq; }
    sc += w == 0 ? v : -v;
  }
  bool eg = npm <= 2600;
  for (int w = 0; w < 2; w++) {
    int s = 0;
    if (ksq[w] >= 0) { int r = ksq[w] >> 3, f = ksq[w] & 7; s += eg ? chsPstKe[w == 0 ? (7 - r) * 8 + f : r * 8 + f] : chsPst[5][w == 0 ? (7 - r) * 8 + f : r * 8 + f]; }
    if (nb[w] >= 2) s += 30;
    for (int i = 0; i < np[w]; i++) {                               // pion
      int sq = pawnSq[w][i], r = sq >> 3, f = sq & 7, rel = w == 0 ? r : 7 - r;
      if (pcnt[w][f] > 1) s -= 6;
      if ((f == 0 || pcnt[w][f - 1] == 0) && (f == 7 || pcnt[w][f + 1] == 0)) s -= 12;
      bool pass = true;
      for (int ff = (f > 0 ? f - 1 : 0); ff <= (f < 7 ? f + 1 : 7); ff++) {
        if (w == 0 ? (bmax[ff] > r) : (wmin[ff] < r)) { pass = false; break; }
      }
      if (pass) { int b = chsEvPass[rel]; s += eg ? b + b / 2 : b; }
    }
    for (int i = 0; i < nr[w]; i++) {                               // benteng
      int sq = rks[w][i], f = sq & 7, rel = w == 0 ? (sq >> 3) : 7 - (sq >> 3);
      if (pcnt[w][f] == 0) s += pcnt[1 - w][f] == 0 ? 18 : 10;
      if (rel == 6) s += 10;
    }
    if (!eg && ksq[w] >= 0) {                                       // perisai raja
      int kr = ksq[w] >> 3, kf = ksq[w] & 7, rel = w == 0 ? kr : 7 - kr;
      if (rel <= 1) {
        int dir = w == 0 ? 1 : -1; int8_t pw = (int8_t)dir; int ks = 0;
        for (int ff = (kf > 0 ? kf - 1 : 0); ff <= (kf < 7 ? kf + 1 : 7); ff++) {
          if (chsBd[(kr + dir) * 8 + ff] == pw) ks += 10;
          else if (chsBd[(kr + 2 * dir) * 8 + ff] == pw) ks += 5;
        }
        if (pcnt[w][kf] == 0) ks -= pcnt[1 - w][kf] == 0 ? 25 : 14;
        if (npm < 1800) ks /= 2;
        s += ks;
      }
    }
    sc += w == 0 ? s : -s;
  }
  if (eg && ksq[0] >= 0 && ksq[1] >= 0) {                           // mop-up endgame
    int dm = mat[0] - mat[1];
    if (dm > 200) sc += chsEvMop(ksq[1], ksq[0]); else if (dm < -200) sc -= chsEvMop(ksq[0], ksq[1]);
  }
  sc += chsWhite ? 8 : -8;                                          // tempo
  return chsWhite ? sc : -sc;
}

// kunci urutan langkah: MVV-LVA + promosi
static int chsAiKey(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  int8_t p = chsBd[from], q = chsBd[to];
  int t = p > 0 ? p : -p, qt = q > 0 ? q : -q, k = 0;
  if (!q && t == 1 && to == chsEp && (from & 7) != (to & 7)) qt = 1;
  if (qt) k = chsAiVal[qt] * 10 - chsAiVal[t] / 10;
  if (promo) k += chsAiVal[promo] * 10;
  return k;
}
static inline void chsAiPick(uint16_t* mv, int16_t* ky, int n, int i) {
  int b = i;
  for (int j = i + 1; j < n; j++) if (ky[j] > ky[b]) b = j;
  if (b != i) { uint16_t tm = mv[i]; mv[i] = mv[b]; mv[b] = tm; int16_t tk = ky[i]; ky[i] = ky[b]; ky[b] = tk; }
}
#ifndef CHS_AI_STACK_MAX
#define CHS_AI_STACK_MAX 4800                  // byte stack yg boleh dipakai pencarian (task loop 8 KB: sisanya utk pemanggil + evaluasi + ISR)
#endif
static uintptr_t chsAiSp0 = 0;
static inline bool chsAiStackLow() {           // stack tumbuh turun di Xtensa maupun x86; ukur jarak dari awal chsAiChoose
  char c; uintptr_t a = (uintptr_t)&c;
  return (chsAiSp0 > a ? chsAiSp0 - a : a - chsAiSp0) > (uintptr_t)CHS_AI_STACK_MAX;
}
static inline bool chsAiTimeUp() {
  if (chsAiCheckTime && (chsAiNodes & 255u) == 0 && (int32_t)(CHS_AI_NOW() - chsAiDeadline) >= 0) chsAiAbort = true;
  return chsAiAbort;
}

// ---- hash 64 bit utk tabel transposisi (BUKAN chsPosHash: itu dipakai buku pembuka di NVS, formatnya tidak boleh berubah) ----
static inline uint64_t chsAiH64() {
  uint64_t w[8]; memcpy(w, chsBd, 64);
  uint64_t h = 0x9E3779B97F4A7C15ULL;
  for (int i = 0; i < 8; i++) { h ^= w[i]; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 32; }
  h ^= (uint64_t)(chsWhite ? 1u : 2u) | ((uint64_t)chsCastle << 2) | ((uint64_t)(uint8_t)(chsEp + 1) << 8);
  h *= 0xC4CEB9FE1A85EC53ULL; h ^= h >> 29;
  return h;
}
static inline int chsAiToTT(int s, int ply)   { return s > CHS_AI_MATE - 200 ? s + ply : (s < -CHS_AI_MATE + 200 ? s - ply : s); }
static inline int chsAiFromTT(int s, int ply) { return s > CHS_AI_MATE - 200 ? s - ply : (s < -CHS_AI_MATE + 200 ? s + ply : s); }
static inline bool chsAiHasPieces(bool white) {
  for (int i = 0; i < 64; i++) { int8_t p = chsBd[i]; if (white ? (p >= 2 && p <= 5) : (p <= -2 && p >= -5)) return true; }
  return false;
}

// ---------------------------------------------------------------- SEE (static exchange evaluation)
static inline int chsSeeVal(int t) { static const int16_t v[7] = {0, 100, 320, 330, 500, 900, 20000}; return v[t]; }
// petak penyerang termurah milik 'white' ke sq di papan b (-1 = tidak ada); bidak yg sudah "dimakan" dihapus dari b sehingga sinar-X ikut terbuka
static int chsSeeLva(const int8_t* b, int sq, bool white) {
  int r = sq >> 3, c = sq & 7; int8_t sg = white ? 1 : -1;
  int pr = white ? r - 1 : r + 1;
  if (pr >= 0 && pr < 8) {
    if (c > 0 && b[pr * 8 + c - 1] == sg) return pr * 8 + c - 1;
    if (c < 7 && b[pr * 8 + c + 1] == sg) return pr * 8 + c + 1;
  }
  static const int8_t kn[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
  for (int i = 0; i < 8; i++) {
    int rr = r + kn[i][0], cc = c + kn[i][1];
    if (rr < 0 || rr > 7 || cc < 0 || cc > 7) continue;
    if (b[rr * 8 + cc] == sg * 2) return rr * 8 + cc;
  }
  static const int8_t dr[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
  int best = -1, bt = 9;
  for (int d = 0; d < 8; d++) {
    int rr = r + dr[d][0], cc = c + dr[d][1]; bool first = true;
    while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8) {
      int8_t p = b[rr * 8 + cc];
      if (p) {
        if ((p > 0) == white) {
          int t = p > 0 ? p : -p;
          bool ok = (t == 6 && first) || (d < 4 ? (t == 4 || t == 5) : (t == 3 || t == 5));
          if (ok && t < bt) { bt = t; best = rr * 8 + cc; }
        }
        break;
      }
      first = false; rr += dr[d][0]; cc += dr[d][1];
    }
  }
  return best;
}
static int chsSee(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  int8_t b[64]; memcpy(b, chsBd, 64);
  int8_t p = b[from]; int t = p > 0 ? p : -p; bool w = p > 0;
  int8_t q = b[to]; int qt = q > 0 ? q : -q;
  if (!q && t == 1 && to == chsEp && (from & 7) != (to & 7)) { qt = 1; b[(from & ~7) | (to & 7)] = 0; }
  int16_t gain[32]; int d = 0;
  gain[0] = (int16_t)(chsSeeVal(qt) + (promo ? chsSeeVal(promo) - 100 : 0));
  int cur = chsSeeVal(promo ? promo : t);
  b[from] = 0; bool side = !w;
  for (;;) {
    d++; if (d >= 31) break;
    gain[d] = (int16_t)(cur - gain[d - 1]);
    if (-gain[d - 1] < 0 && gain[d] < 0) break;
    int sq = chsSeeLva(b, to, side); if (sq < 0) break;
    int8_t pp = b[sq]; int pt = pp > 0 ? pp : -pp; cur = chsSeeVal(pt); b[sq] = 0; side = !side;
  }
  while (--d > 0) { int a = -gain[d - 1], c2 = gain[d]; gain[d - 1] = (int16_t)(-(a > c2 ? a : c2)); }
  return gain[0];
}
static inline bool chsAiIsCap(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63; int8_t p = chsBd[from];
  return chsBd[to] != 0 || ((m >> 12) & 15) || ((p == 1 || p == -1) && to == chsEp && (from & 7) != (to & 7));
}

#ifndef CHS_X_SEEQ
#define CHS_X_SEEQ 1          // qsearch: buang tangkapan yg kalah dalam pertukaran (SEE < 0)
#endif
#ifndef CHS_X_LMP
#define CHS_X_LMP 1           // late move pruning pada kedalaman rendah
#endif
#ifndef CHS_X_IID
#define CHS_X_IID 1           // internal iterative deepening bila tak ada langkah TT
#endif
#ifndef CHS_X_MDP
#define CHS_X_MDP 1           // mate distance pruning
#endif
#ifndef CHS_X_RFP
#define CHS_X_RFP 4           // reverse futility: kedalaman maksimum (lama = 2)
#endif
#ifndef CHS_X_FUT
#define CHS_X_FUT 3           // futility: kedalaman maksimum (lama = 2)
#endif
#ifndef CHS_X_TFRAC
#define CHS_X_TFRAC 2         // mulai iterasi baru bila waktu terpakai < budget / TFRAC (lama = 3)
#endif
#ifndef CHS_X_PVSROOT
#define CHS_X_PVSROOT 1       // akar: jendela nol + pakai hasil iterasi yg terpotong waktu (hanya margin 0)
#endif

static int16_t chsAiStAt[CHS_AI_PLY + 2];
static uint8_t chsAiLmr[32][64];
static bool chsAiLmrOk = false;
static void chsAiLmrInit() {
  for (int d = 0; d < 32; d++) for (int n = 0; n < 64; n++) {
    int v = (d < 1 || n < 1) ? 0 : (int)(0.55f + logf((float)d) * logf((float)n) / 2.3f);
    chsAiLmr[d][n] = (uint8_t)v;
  }
  chsAiLmrOk = true;
}

// ---------------------------------------------------------------- pencarian
static int chsAiQ(int alpha, int beta, int ply) {
  chsAiNodes++;
  if (chsAiTimeUp()) return 0;
  int stand = chsAiEval();
  if (ply >= CHS_AI_PLY - 1 || chsAiStackLow()) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  uint16_t* mv = chsAiMv + ply * 256; int16_t* ky = chsAiKy + ply * 256;
  int n = chsGen(mv), nc = 0;
  for (int i = 0; i < n; i++) {
    uint16_t m = mv[i]; int from = m & 63, to = (m >> 6) & 63;
    int8_t p = chsBd[from];
    bool capt = chsBd[to] != 0 || ((p == 1 || p == -1) && to == chsEp && (from & 7) != (to & 7)) || ((m >> 12) & 15);
    if (!capt) continue;
    if (!((m >> 12) & 15)) {                                        // delta pruning: tangkapan ini pun tak mungkin menaikkan alpha
      int8_t q = chsBd[to]; int qt = q > 0 ? q : -q; if (!qt) qt = 1;
      if (stand + chsAiVal[qt] + 200 < alpha) continue;
#if CHS_X_SEEQ
      int pt = p > 0 ? p : -p;
      if (chsSeeVal(pt) > chsSeeVal(qt) && chsSee(m) < 0) continue;  // pertukaran rugi
#endif
    }
    int k = chsAiKey(m); mv[nc] = m; ky[nc] = (int16_t)k; nc++;
  }
  for (int i = 0; i < nc; i++) {
    chsAiPick(mv, ky, nc, i);
    CHS_SAVE_AT(ply);
    bool mover = chsWhite;
    chsDo(mv[i]);
    int ks = chsKingSq(mover);
    if (ks < 0 || chsAtt(ks, !mover)) { CHS_REST_AT(ply); continue; }
    int sc = -chsAiQ(-beta, -alpha, ply + 1);
    CHS_REST_AT(ply);
    if (chsAiAbort) return 0;
    if (sc >= beta) return sc;
    if (sc > alpha) alpha = sc;
  }
  return alpha;
}

// Negamax PVS + tabel transposisi + null-move + LMR (tabel log) + futility/LMP + killer/history (gravitasi) + SEE + IID.
// Urutan: langkah TT (30000) > tangkapan baik MVV-LVA (>= 10000) > killer (9000/8999) > history (~700..7300) > tangkapan buruk (< 700).
static int chsAiNeg(int depth, int alpha, int beta, int ply, bool nullOk) {
  chsAiNodes++;
  if (chsAiTimeUp()) return 0;
  if (chsHalf >= 100) return 0;
  uint64_t hk = chsAiH64(); uint32_t hk32 = (uint32_t)(hk >> 32);
  if (chsAiPath) {
    chsAiPath[ply] = (uint32_t)hk;
    int lim = ply - (int)chsHalf; if (lim < 0) lim = 0;
    for (int k = ply - 2; k >= lim; k -= 2) if (chsAiPath[k] == (uint32_t)hk) return 0;      // pengulangan di jalur pencarian = remis
  }
#if CHS_X_MDP
  { int ma = -CHS_AI_MATE + ply; if (alpha < ma) alpha = ma; int mb = CHS_AI_MATE - ply - 1; if (beta > mb) beta = mb; if (alpha >= beta) return alpha; }
#endif
  bool inchk = chsInCheck();
  if (inchk && ply < CHS_AI_PLY - 6) depth++;
  if (depth <= 0) return chsAiQ(alpha, beta, ply);
  if (ply >= CHS_AI_PLY - 1 || chsAiStackLow()) return chsAiEval();
  int alpha0 = alpha; uint16_t ttm = 0;
  bool pvn = (beta - alpha) > 1;
  ChsTT* te = chsAiTT ? &chsAiTT[(uint32_t)hk & chsAiTTMask] : nullptr;
  if (te && te->k == hk32) {
    ttm = te->m;                                                   // langkah TT dipakai utk urutan dari entri umur berapa pun
    if (te->a == chsAiAge && te->d >= depth) {                     // skor hanya dipercaya dari pencarian ini: skor lama bergantung riwayat (remis ulang, 50 langkah)
      int s = chsAiFromTT(te->s, ply);
      if (te->f == 0) return s;
      if (te->f == 1 && s >= beta) return s;
      if (te->f == 2 && s <= alpha) return s;
    }
  }
  int stand = 0; bool improving = false;
  if (!inchk) {
    stand = chsAiEval();
    chsAiStAt[ply] = (int16_t)stand;
    improving = ply >= 2 && stand > chsAiStAt[ply - 2];
    if (!pvn && depth <= CHS_X_RFP && beta < CHS_AI_MATE - 1000 && stand - (improving ? 100 : 130) * depth >= beta) return stand;   // reverse futility
    if (nullOk && depth >= 3 && beta < CHS_AI_MATE - 1000 && stand >= beta && chsAiHasPieces(chsWhite)) { // null move
      bool sw = chsWhite; int8_t se = chsEp; uint16_t sh = chsHalf;               // null move: papan tidak berubah
      chsWhite = !chsWhite; chsEp = -1; chsHalf++;
      int R = 2 + depth / 4;
      int sc = -chsAiNeg(depth - 1 - R, -beta, -beta + 1, ply + 1, false);
      chsWhite = sw; chsEp = se; chsHalf = sh;
      if (chsAiAbort) return 0;
      if (sc >= beta) return sc >= CHS_AI_MATE - 1000 ? beta : sc;
    }
  } else chsAiStAt[ply] = 32000;
#if CHS_X_IID
  if (!ttm && depth >= 5 && te) {                                  // tak ada langkah TT: pencarian dangkal utk mendapatkannya
    chsAiNeg(depth - 2, alpha, beta, ply, false);
    if (chsAiAbort) return 0;
    if (te->k == hk32) ttm = te->m;
  }
#endif
  uint16_t* mv = chsAiMv + ply * 256; int16_t* ky = chsAiKy + ply * 256;
  int n = chsGen(mv);
  int side = chsWhite ? 0 : 1;
  for (int i = 0; i < n; i++) {
    uint16_t m = mv[i]; int k;
    if (m == ttm) k = 30000;
    else if (chsAiIsCap(m)) {
      k = chsAiKey(m);
      int from = m & 63, to = (m >> 6) & 63; int8_t p = chsBd[from], q = chsBd[to]; int pt = p > 0 ? p : -p, qt = q > 0 ? q : -q;
      if (!((m >> 12) & 15) && qt && chsSeeVal(pt) > chsSeeVal(qt) && chsSee(m) < 0) k = k / 32;   // tangkapan rugi: paling akhir
      else k += 10000;
    }
    else if (m == chsAiKil[ply][0]) k = 9000;
    else if (m == chsAiKil[ply][1]) k = 8999;
    else { int hv = chsAiHis ? chsAiHis[side * 4096 + (m & 63) * 64 + ((m >> 6) & 63)] : 0; k = 4000 + hv / 5; }
    ky[i] = (int16_t)k;
  }
  int best = -CHS_AI_INF, legal = 0; uint16_t bestm = 0;
  uint16_t tried[16]; int nt = 0;
  for (int i = 0; i < n; i++) {
    chsAiPick(mv, ky, n, i);
    uint16_t m = mv[i];
    bool cap = chsAiIsCap(m);
    bool quiet = !cap;
    bool bad = cap && m != ttm && ky[i] < 700;
    CHS_SAVE_AT(ply);
    bool mover = chsWhite;
    chsDo(m);
    int ks = chsKingSq(mover);
    if (ks < 0 || chsAtt(ks, !mover)) { CHS_REST_AT(ply); continue; }
    legal++;
    bool gc = false, gcK = false;
    if (quiet && !inchk && legal > 1 && alpha > -CHS_AI_MATE + 1000) {
      if (depth <= CHS_X_FUT && stand + 150 + 140 * depth <= alpha) { gc = chsInCheck(); gcK = true; if (!gc) { CHS_REST_AT(ply); continue; } }                       // futility
#if CHS_X_LMP
      if (depth <= 3 && legal > (improving ? 4 : 2) + depth * depth) { if (!gcK) { gc = chsInCheck(); gcK = true; } if (!gc) { CHS_REST_AT(ply); continue; } }      // late move pruning
#endif
    }
    int sc;
    if (legal == 1) {
      sc = -chsAiNeg(depth - 1, -beta, -alpha, ply + 1, true);
    } else {
      int red = 0;
      if (depth >= 3 && (quiet || bad) && !inchk && legal > 1) {
        red = chsAiLmr[depth < 31 ? depth : 31][legal < 63 ? legal : 63];
        if (pvn && red > 0) red--;
        if (improving && red > 0) red--;
        if (quiet && (m == chsAiKil[ply][0] || m == chsAiKil[ply][1]) && red > 0) red--;
        if (bad) red++;
        if (!gcK) { gc = chsInCheck(); gcK = true; }
        if (gc && red > 0) red--;
        if (red > depth - 2) red = depth - 2;
        if (red < 0) red = 0;
      }
      sc = -chsAiNeg(depth - 1 - red, -alpha - 1, -alpha, ply + 1, true);
      if (sc > alpha && red) sc = -chsAiNeg(depth - 1, -alpha - 1, -alpha, ply + 1, true);
      if (sc > alpha && sc < beta) sc = -chsAiNeg(depth - 1, -beta, -alpha, ply + 1, true);
    }
    CHS_REST_AT(ply);
    if (chsAiAbort) return 0;
    if (sc > best) { best = sc; bestm = m; }
    if (sc > alpha) alpha = sc;
    if (alpha >= beta) {
      if (quiet) {
        if (chsAiKil[ply][0] != m) { chsAiKil[ply][1] = chsAiKil[ply][0]; chsAiKil[ply][0] = m; }
        if (chsAiHis) {
          int b = depth * depth; if (b > 400) b = 400;
          int16_t* h = &chsAiHis[side * 4096 + (m & 63) * 64 + ((m >> 6) & 63)];
          *h = (int16_t)(*h + b - (int)*h * b / 16384);
          for (int j = 0; j < nt; j++) { int16_t* g = &chsAiHis[side * 4096 + (tried[j] & 63) * 64 + ((tried[j] >> 6) & 63)]; *g = (int16_t)(*g - b - (int)*g * b / 16384); }
        }
      }
      break;
    }
    if (quiet && nt < 16) tried[nt++] = m;
  }
  if (!legal) return inchk ? -CHS_AI_MATE + ply : 0;
  if (te) {
    if (te->k != hk32 || te->a != chsAiAge || depth >= te->d) {
      te->k = hk32; te->s = (int16_t)chsAiToTT(best, ply); te->m = bestm; te->d = (int8_t)depth; te->a = chsAiAge;
      te->f = best <= alpha0 ? 2 : (best >= beta ? 1 : 0);
    }
  }
  return best;
}

// hash tiap posisi dari daftar langkah "e2e4 e7e5 ..." (utk menghindari pengulangan saat AI unggul)
static void chsAiHistBuild(const char* mv) {
  chsAiHistN = 0;
  if (!chsAiEnsure()) return;
  CHS_SAVE_ST;
  chsReset();
  chsAiHist[chsAiHistN++] = chsPosHash();
  const char* p = mv;
  while (p && *p && chsAiHistN < CHS_AI_HIST) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* q = p; while (*q && *q != ' ') q++;
    uint16_t m;
    if (!chsParse(p, (int)(q - p), &m)) break;
    chsDo(m);
    chsAiHist[chsAiHistN++] = chsPosHash();
    p = q;
  }
  CHS_REST_ST;
}

// ---------------------------------------------------------------- fitur gaya
static inline float chsAiCent(int r, int c) { return 7.0f - (float)(chsAbsI(2 * r - 7) + chsAbsI(2 * c - 7)) * 0.5f; }
static inline float chsAiClamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// f[0..CHS_NF-1] utk langkah m pada posisi SEKARANG (belum dijalankan). Semua nilai relatif thd sisi yg jalan.
static void chsAiFeat(uint16_t m, float* f) {
  for (int i = 0; i < CHS_NF; i++) f[i] = 0;
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  int8_t p = chsBd[from], q = chsBd[to];
  int t = p > 0 ? p : -p, qt = q > 0 ? q : -q; bool w = p > 0;
  bool ep = (t == 1 && !q && to == chsEp && (from & 7) != (to & 7));
  if (ep) qt = 1;
  bool cap = q != 0 || ep;
  int fr = from >> 3, ff = from & 7, tr = to >> 3, tf = to & 7;
  int fwd = w ? (tr - fr) : (fr - tr);
  f[0]  = cap ? 1.0f : 0.0f;                                                   // memakan
  f[1]  = (cap && chsAiVal[qt] >= chsAiVal[t]) ? 1.0f : 0.0f;                  // pertukaran menguntungkan
  f[2]  = chsAiClamp1((chsAiCent(tr, tf) - chsAiCent(fr, ff)) / 4.0f);         // ke tengah papan
  f[3]  = (t == 1 && !cap) ? 1.0f : 0.0f;                                      // dorong pion
  f[4]  = chsAiClamp1(fwd / 2.0f);                                             // maju
  f[5]  = ((t == 2 || t == 3) && fr == (w ? 0 : 7)) ? 1.0f : 0.0f;            // kembangkan kuda / gajah
  f[6]  = (t == 6 && chsAbsI(to - from) == 2) ? 1.0f : 0.0f;                   // rokade
  f[7]  = (t == 5) ? 1.0f : 0.0f;                                              // menteri
  f[8]  = (t == 4) ? 1.0f : 0.0f;                                              // benteng
  f[9]  = (t == 6 && chsAbsI(to - from) != 2) ? 1.0f : 0.0f;                   // raja jalan
  f[11] = chsAtt(to, !w) ? 1.0f : 0.0f;                                        // mendarat di petak yg diserang
  f[12] = (t != 1 && fwd < 0) ? 1.0f : 0.0f;                                   // mundur
  int ek = chsKingSq(!w);
  if (ek >= 0) {
    int er = ek >> 3, ef = ek & 7;
    int d0 = chsAbsI(fr - er) > chsAbsI(ff - ef) ? chsAbsI(fr - er) : chsAbsI(ff - ef);
    int d1 = chsAbsI(tr - er) > chsAbsI(tf - ef) ? chsAbsI(tr - er) : chsAbsI(tf - ef);
    f[13] = chsAiClamp1((d0 - d1) / 2.0f);                                     // mendekati raja lawan
  }
  f[14] = promo ? 1.0f : 0.0f;                                                 // promosi
  f[15] = (t == 1 && (tf <= 1 || tf >= 6)) ? 1.0f : 0.0f;                      // pion sayap
  f[16] = (t == 2) ? 1.0f : 0.0f;                                              // kuda
  f[17] = (t == 3) ? 1.0f : 0.0f;                                              // gajah
  bool attFrom = (t != 6) && chsAtt(from, !w), attTo = false, defTo = false;
  { CHS_SAVE_ST; chsDo(m); f[10] = chsInCheck() ? 1.0f : 0.0f;                  // memberi skak
    if (t != 6) { attTo = chsAtt(to, !w); defTo = chsAtt(to, w); }              // (papan sesudah langkah)
    CHS_REST_ST; }
  f[18] = (attFrom && !attTo) ? 1.0f : 0.0f;                                   // menyelamatkan bidak yg diserang
  f[19] = defTo ? 1.0f : 0.0f;                                                 // mendarat di petak yg dilindungi
}

// ---------------------------------------------------------------- TinyML: MLP penilai gaya
// Tata letak chsNet: W1[NH][NIN] | b1[NH] | v[NH] | b2
static void chsNetFill(float* o) {                   // bobot awal deterministik (xorshift) ala Xavier; dipakai juga layar Jaringan utk 'selisih'
  uint32_t s = 0x9E3779B9u;
  float lim1 = 0.39f, lim2 = 0.45f;
  for (int i = 0; i < CHS_NP; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    float r = ((float)(s & 0xFFFFu) / 32767.5f) - 1.0f;                // -1..1
    o[i] = (i < CHS_NH * CHS_NIN) ? r * lim1 : ((i < CHS_NH * CHS_NIN + CHS_NH) ? 0.0f : ((i < CHS_NP - 1) ? r * lim2 : 0.0f));
  }
}
static void chsNetInit() { chsNetFill(chsNet); chsNetReady = true; }
static inline void chsNetEnsure() { if (!chsNetReady) chsNetInit(); }
// maju: kembalikan skor; zout (opsional, CHS_NH) = pra-aktivasi utk backprop
static inline float chsNetFwd(const float* x, float* zout) {
  const float* W1 = chsNet; const float* B1 = W1 + CHS_NH * CHS_NIN; const float* V = B1 + CHS_NH;
  float s = V[CHS_NH];
  for (int j = 0; j < CHS_NH; j++) {
    const float* w = W1 + j * CHS_NIN; float z = B1[j];
    for (int k = 0; k < CHS_NIN; k++) z += w[k] * x[k];
    if (zout) zout[j] = z;
    s += V[j] * (z > 0 ? z : 0.05f * z);               // leaky ReLU
  }
  return s;
}
// kebiasaan petak: skor jarang (2 fitur aktif per langkah). Petak dicerminkan utk Hitam supaya kebiasaan berlaku utk kedua sisi.
static inline void chsHabIdx(uint16_t m, int* a, int* b) {
  int from = m & 63, to = (m >> 6) & 63; int8_t p = chsBd[from]; int t = p > 0 ? p : -p; bool w = p > 0;
  if (t < 1) t = 1;
  if (!w) { from ^= 56; to ^= 56; }
  *a = (t - 1) * 64 + to; *b = 384 + from;
}
static inline float chsHabScore(uint16_t m) { int a, b; chsHabIdx(m, &a, &b); return chsHab[a] + chsHab[b]; }
// konteks posisi (sisi yg jalan): c[0..CHS_NC-1]
static void chsAiCtx(float* c, int nLegal, int ply) {
  int npm = 0, mine = 0, theirs = 0; bool q = false;
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = chsBd[sq]; if (!p) continue;
    int t = p > 0 ? p : -p; if (t == 6) continue;
    if (t == 5) q = true;
    if (t >= 2 && t <= 5) npm += chsAiVal[t];
    if ((p > 0) == chsWhite) mine += chsAiVal[t]; else theirs += chsAiVal[t];
  }
  float ph = (float)npm / 6200.0f; if (ph > 1.0f) ph = 1.0f;
  c[0] = ph;                                                                  // fase permainan (1 = pembukaan)
  c[1] = chsAiClamp1((float)(mine - theirs) / 1000.0f);                       // unggul / tertinggal materi
  c[2] = chsInCheck() ? 1.0f : 0.0f;                                          // sedang diskak
  c[3] = (ply > 100 ? 100 : (ply < 0 ? 0 : ply)) / 100.0f;                    // sudah berapa langkah
  c[4] = nLegal / 40.0f; if (c[4] > 1.5f) c[4] = 1.5f;                        // mobilitas
  c[5] = q ? 1.0f : 0.0f;                                                     // masih ada menteri
}
static float chsStyleScore(uint16_t m, const float* ctx) {
  chsNetEnsure();
  float x[CHS_NIN]; chsAiFeat(m, x);
  for (int k = 0; k < CHS_NC; k++) x[CHS_NF + k] = ctx[k];
  return chsNetFwd(x, nullptr) + chsHabScore(m);
}

// ---------------------------------------------------------------- buku pembuka pribadi
static void chsBookAdd(uint32_t h, uint16_t m) {
  for (int i = 0; i < chsBkN; i++) if (chsBkH[i] == h) {
    if (chsBkM[i] == m) { if (chsBkC[i] < 255) chsBkC[i]++; }
    else if (chsBkC[i] > 1) chsBkC[i]--;
    else { chsBkM[i] = m; chsBkC[i] = 1; }
    chsStyleDirty = true; return;
  }
  int slot;
  if (chsBkN < CHS_BK_N) slot = chsBkN++;
  else { slot = chsBkNext; chsBkNext = (uint16_t)((chsBkNext + 1) % CHS_BK_N); }
  chsBkH[slot] = h; chsBkM[slot] = m; chsBkC[slot] = 1; chsStyleDirty = true;
}
static uint16_t chsBookGet(uint32_t h, int* cnt) {
  for (int i = 0; i < chsBkN; i++) if (chsBkH[i] == h) { if (cnt) *cnt = chsBkC[i]; return chsBkM[i]; }
  return 0;
}

// ---------------------------------------------------------------- belajar online dari langkah USER
// Satu langkah SGD: softmax-cross-entropy atas SEMUA langkah sah di posisi sekarang, target = langkah user.
static void chsLearnStep(uint16_t played, int ply, float lr) {
  uint16_t pm[256]; int pn = chsGen(pm), n = 0, j = -1;
  for (int i = 0; i < pn; i++) if (chsLegal(pm[i])) { chsAiRm[n] = pm[i]; if (pm[i] == played) j = n; n++; }
  chsLastHit = -1;
  if (n < 2 || j < 0) return;                                  // langkah terpaksa tidak mengajarkan apa-apa
  float ctx[CHS_NC]; chsAiCtx(ctx, n, ply);
  float* F = chsAiFB; float* S = chsAiFS; float mx = -1e30f; int arg = 0;
  for (int i = 0; i < n; i++) {
    float* x = F + i * CHS_NIN; chsAiFeat(chsAiRm[i], x);
    for (int k = 0; k < CHS_NC; k++) x[CHS_NF + k] = ctx[k];
    S[i] = chsNetFwd(x, nullptr) + chsHabScore(chsAiRm[i]); if (S[i] > mx) { mx = S[i]; arg = i; }
  }
  chsLastHit = (arg == j) ? 1 : 0;                              // tebakan model SEBELUM belajar dari langkah ini
  float sum = 0;
  for (int i = 0; i < n; i++) { S[i] = expf(S[i] - mx); sum += S[i]; }
  float* G = chsAiG; for (int i = 0; i < CHS_NP; i++) G[i] = 0;
  float* gW1 = G; float* gB1 = G + CHS_NH * CHS_NIN; float* gV = gB1 + CHS_NH;
  const float* V = chsNet + CHS_NH * CHS_NIN + CHS_NH;
  for (int i = 0; i < n; i++) {
    float d = S[i] / sum - (i == j ? 1.0f : 0.0f);             // dL/dskor
    if (d < 0.002f && d > -0.002f) continue;
    { int ha, hb; chsHabIdx(chsAiRm[i], &ha, &hb); float st = lr * 1.5f * d;          // kebiasaan petak (jarang)
      chsHab[ha] -= st; chsHab[hb] -= st;
      if (chsHab[ha] > 6.0f) chsHab[ha] = 6.0f; else if (chsHab[ha] < -6.0f) chsHab[ha] = -6.0f;
      if (chsHab[hb] > 6.0f) chsHab[hb] = 6.0f; else if (chsHab[hb] < -6.0f) chsHab[hb] = -6.0f; }
    const float* x = F + i * CHS_NIN; float z[CHS_NH]; chsNetFwd(x, z);
    gV[CHS_NH] += d;
    for (int h = 0; h < CHS_NH; h++) {
      float a = z[h] > 0 ? z[h] : 0.05f * z[h];
      gV[h] += d * a;
      float dz = d * V[h] * (z[h] > 0 ? 1.0f : 0.05f);
      gB1[h] += dz;
      float* gw = gW1 + h * CHS_NIN; for (int k = 0; k < CHS_NIN; k++) gw[k] += dz * x[k];
    }
  }
  for (int i = 0; i < CHS_NP; i++) {
    float st = lr * (G[i] + 0.0005f * chsNet[i]);
    st = st > 0.25f ? 0.25f : (st < -0.25f ? -0.25f : st);     // batasi langkah supaya satu contoh aneh tidak merusak
    chsNet[i] -= st;
  }
}
static inline float chsLearnRate() { float lr = 0.15f / (1.0f + (float)chsTrainN / 150.0f); return lr < 0.01f ? 0.01f : lr; }

// ---------------------------------------------------------------- NYX v2: model campuran (Product of Experts)
static void chsMxReset() { memcpy(chsMxTh, chsMxTh0, sizeof(chsMxTh)); memcpy(chsMxA, chsMxA0, sizeof(chsMxA)); chsMxIg = 0.0f; chsMxN = 0; }
static inline float chsMxClampQ(int v) { float q = -(float)v / 100.0f; return q < -8.0f ? -8.0f : (q > 8.0f ? 8.0f : q); }
// keserakahan = nilai bidak yg dimakan (pion = 1, menteri = 9); en passant = 1
static inline float chsMxGreed(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63; int8_t p = chsBd[from], q = chsBd[to];
  int qt = q > 0 ? q : -q;
  if (!q && (p == 1 || p == -1) && to == chsEp && (from & 7) != (to & 7)) qt = 1;
  return qt ? (float)chsAiVal[qt] / 100.0f : 0.0f;
}
// selesaikan A x = b (A simetris 3x3, urutan a00 a01 a02 a11 a12 a22) lewat adjugat
static bool chsMxSolve(const float* a, const float* b, float* x) {
  float c00 = a[3] * a[5] - a[4] * a[4], c01 = a[2] * a[4] - a[1] * a[5], c02 = a[1] * a[4] - a[3] * a[2];
  float c11 = a[0] * a[5] - a[2] * a[2], c12 = a[1] * a[2] - a[0] * a[4], c22 = a[0] * a[3] - a[1] * a[1];
  float det = a[0] * c00 + a[1] * c01 + a[2] * c02;
  if (!(det > 1e-6f)) return false;
  x[0] = (c00 * b[0] + c01 * b[1] + c02 * b[2]) / det;
  x[1] = (c01 * b[0] + c11 * b[1] + c12 * b[2]) / det;
  x[2] = (c02 * b[0] + c12 * b[1] + c22 * b[2]) / det;
  return true;
}
// Dipanggil SEBELUM langkah user dijalankan. Satu langkah Newton atas log-likelihood (regresi logit bersyarat) pada
// langkah yg benar-benar dipilih user: gradien = E_p[phi] - phi(dipilih), Hessian = Cov_p(phi). Presisi A menumpuk (dgn lupa),
// jadi langkah makin kecil seiring bukti. Sekaligus mencatat perolehan informasi (prequential): seberapa lebih baik model
// menebak langkah user dibanding tebakan acak -> dasar \"gerbang kepercayaan\" Nyx.
static void chsMxObserve(uint16_t played, int ply) {
  if (!chsAiQv || !chsAiGv) return;
  uint16_t pm[256]; int pn = chsGen(pm), n = 0, j = -1;
  uint16_t* lg = chsAiRm;
  for (int i = 0; i < pn; i++) if (chsLegal(pm[i])) { lg[n] = pm[i]; if (pm[i] == played) j = n; n++; }
  if (n < 2 || j < 0) return;                                      // langkah terpaksa tidak mengajarkan apa-apa
  float ctx[CHS_NC]; chsAiCtx(ctx, n, ply);
  float* S = chsAiFS; float* Q = chsAiQv; float* G = chsAiGv;
  uint32_t tEnd = CHS_AI_NOW() + 100;                              // batas waktu: lewati pengamatan bila terlalu lama (gerbang tetap tertutup -> Nyx = minimax)
  chsAiAbort = false; chsAiCheckTime = false;
  for (int i = 0; i < n; i++) {
    if ((int32_t)(CHS_AI_NOW() - tEnd) >= 0) return;
    S[i] = chsStyleScore(lg[i], ctx); G[i] = chsMxGreed(lg[i]);
    { CHS_SAVE_ST; chsDo(lg[i]); int v = chsAiQ(-CHS_AI_INF, CHS_AI_INF, 2); CHS_REST_ST; Q[i] = chsMxClampQ(v); }
  }
  float L[256], mx = -1e30f;
  for (int i = 0; i < n; i++) { L[i] = chsMxTh[0] * S[i] + chsMxTh[1] * Q[i] + chsMxTh[2] * G[i]; if (L[i] > mx) mx = L[i]; }
  float sum = 0; for (int i = 0; i < n; i++) { L[i] = expf(L[i] - mx); sum += L[i]; }
  float pj = L[j] / sum; if (pj < 1e-6f) pj = 1e-6f;
  float ig = logf(pj) + logf((float)n);
  { int k = chsMxN < 12 ? (int)chsMxN + 1 : 12; chsMxIg += (ig - chsMxIg) / (float)k; if (chsMxN < 65535) chsMxN++; }
  float mu[3] = {0, 0, 0}, e2[6] = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < n; i++) {
    float pi = L[i] / sum, f[3] = {S[i], Q[i], G[i]};
    for (int a = 0; a < 3; a++) mu[a] += pi * f[a];
    e2[0] += pi * f[0] * f[0]; e2[1] += pi * f[0] * f[1]; e2[2] += pi * f[0] * f[2];
    e2[3] += pi * f[1] * f[1]; e2[4] += pi * f[1] * f[2]; e2[5] += pi * f[2] * f[2];
  }
  float fj[3] = {S[j], Q[j], G[j]}, grad[3];
  for (int a = 0; a < 3; a++) grad[a] = mu[a] - fj[a];
  float H[6] = {e2[0] - mu[0] * mu[0], e2[1] - mu[0] * mu[1], e2[2] - mu[0] * mu[2], e2[3] - mu[1] * mu[1], e2[4] - mu[1] * mu[2], e2[5] - mu[2] * mu[2]};
  for (int a = 0; a < 6; a++) {
    float v = CHS_MX_RHO * (chsMxA[a] - chsMxA0[a]) + chsMxA0[a] + H[a];
    chsMxA[a] = v > 1e4f ? 1e4f : (v < -1e4f ? -1e4f : v);
  }
  float rhs[3] = {grad[0] + chsMxA0[0] * (chsMxTh[0] - chsMxTh0[0]), grad[1] + chsMxA0[3] * (chsMxTh[1] - chsMxTh0[1]), grad[2] + chsMxA0[5] * (chsMxTh[2] - chsMxTh0[2])};
  float d[3];
  if (chsMxSolve(chsMxA, rhs, d)) {
    static const float lo[3] = {0.0f, -1.0f, -1.0f}, hi[3] = {3.0f, 4.0f, 3.0f};
    for (int a = 0; a < 3; a++) { float v = chsMxTh[a] - 0.8f * d[a]; chsMxTh[a] = v < lo[a] ? lo[a] : (v > hi[a] ? hi[a] : v); }
  }
  chsStyleDirty = true;
}
// gerbang kepercayaan 0..1: perlu data cukup DAN bukti bahwa model memang menebak lebih baik dari acak
static float chsMxGate() {
  float a = (float)chsTrainN / 30.0f; if (a > 1.0f) a = 1.0f;
  float n = (float)chsMxN, b = chsMxIg / CHS_MX_IG0 * (n / (n + 4.0f)); if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;
  return a * b;
}

// Dipanggil SEBELUM langkah user dijalankan (posisi masih sebelum langkah).
// Selain belajar dari langkah ini, ulang beberapa contoh lama (experience replay) supaya jaringan kecil ini
// cepat belajar dari sedikit data.
static void chsStyleLearn(uint16_t played, int ply) {
  if (!chsAiEnsure()) return;
  chsNetEnsure();
  chsBookAdd(chsPosHash(), played);
  if (chsMxLive) chsMxObserve(played, ply);                     // model campuran: dievaluasi dgn jaringan SEBELUM belajar dari langkah ini
  chsLearnStep(played, ply, chsLearnRate());
  if (chsLastHit >= 0) {                                        // akurasi tebakan: rata-rata bergerak (cepat di awal, lalu ~1/20)
    if (chsAccN < 20) chsAccN++;
    chsAcc += ((float)chsLastHit - chsAcc) / (float)chsAccN;
  }
  { CHS_SAVE_ST;                                                // simpan contoh ini (papan + langkah)
    uint8_t* r = chsAiRb + (size_t)chsRbPos * CHS_RB_SZ;
    memcpy(r, chsBd, 64); r[64] = chsWhite ? 1 : 0; r[65] = chsCastle; r[66] = (uint8_t)chsEp; r[67] = (uint8_t)(chsHalf & 0xFF);
    r[68] = (uint8_t)played; r[69] = (uint8_t)(played >> 8); r[70] = (uint8_t)ply; r[71] = (uint8_t)(ply >> 8);
    chsRbPos = (chsRbPos + 1) % CHS_RB_N; if (chsRbN < CHS_RB_N) chsRbN++; chsRbDirty = true;
    CHS_REST_ST; }
  chsTrainN++; chsStyleDirty = true;
  int reps = chsRbN > 8 ? 6 : (chsRbN > 1 ? 1 : 0);             // replay acak
  for (int q = 0; q < reps; q++) {
    int k = (int)(CHS_AI_RAND() % (uint32_t)chsRbN);
    const uint8_t* r = chsAiRb + (size_t)k * CHS_RB_SZ;
    uint16_t mv = (uint16_t)(r[68] | (r[69] << 8)); int pl = r[70] | (r[71] << 8);
    CHS_SAVE_ST;
    memcpy(chsBd, r, 64); chsWhite = r[64] != 0; chsCastle = r[65]; chsEp = (int8_t)r[66]; chsHalf = r[67];
    chsLearnStep(mv, pl, chsLearnRate() * 0.7f);
    CHS_REST_ST;
  }
}

static void chsStyleReset() {
  memset(chsHab, 0, sizeof(chsHab)); chsAcc = 0; chsAccN = 0; chsRbDirty = true;
  chsNetInit(); chsMxReset(); chsTrainN = 0; chsBkN = 0; chsBkNext = 0; chsRbN = 0; chsRbPos = 0; chsStyleDirty = true;
}

// ---- (de)serialisasi utk NVS ----
#define CHS_STY_VER 3                          // 3 = 20 fitur langkah (versi 2 = 18 fitur: ditolak, belajar dari awal)
#define CHS_STY_LEN (8 + 4 * CHS_NP)
static size_t chsStyleSer(uint8_t* o) {
  chsNetEnsure();
  o[0] = CHS_STY_VER; o[1] = CHS_NIN; o[2] = CHS_NH; o[3] = (chsAccN >= 5) ? (uint8_t)(chsAcc * 254.0f + 1.5f) : 0;   // o[3] = akurasi tebakan (0 = belum diketahui)
  o[4] = (uint8_t)chsTrainN; o[5] = (uint8_t)(chsTrainN >> 8); o[6] = (uint8_t)(chsTrainN >> 16); o[7] = (uint8_t)(chsTrainN >> 24);
  memcpy(o + 8, chsNet, 4 * CHS_NP);
  return CHS_STY_LEN;
}
static bool chsStyleDeser(const uint8_t* b, size_t n) {
  if (n < CHS_STY_LEN || b[0] != CHS_STY_VER || b[1] != CHS_NIN || b[2] != CHS_NH) return false;
  float w[CHS_NP]; memcpy(w, b + 8, 4 * CHS_NP);
  for (int k = 0; k < CHS_NP; k++) if (!(w[k] > -50.0f && w[k] < 50.0f)) return false;   // juga menolak NaN/inf
  memcpy(chsNet, w, sizeof(w)); chsNetReady = true;
  chsTrainN = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
  if (b[3] >= 1) { chsAcc = (float)(b[3] - 1) / 254.0f; if (chsAcc > 1.0f) chsAcc = 1.0f; chsAccN = 20; } else { chsAcc = 0; chsAccN = 0; }
  return true;
}
// tabel kebiasaan petak: int16, skala 1/5000 (jepitan +-6 -> muat)
static size_t chsHabSer(uint8_t* o) {
  o[0] = 1; o[1] = 0; o[2] = (uint8_t)CHS_HAB_N; o[3] = (uint8_t)(CHS_HAB_N >> 8);
  for (int i = 0; i < CHS_HAB_N; i++) {
    float v = chsHab[i] * 5000.0f; int q = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    if (q > 32767) q = 32767; else if (q < -32767) q = -32767;
    o[4 + 2 * i] = (uint8_t)(q & 0xFF); o[5 + 2 * i] = (uint8_t)((q >> 8) & 0xFF);
  }
  return CHS_HAB_LEN;
}
static bool chsHabDeser(const uint8_t* b, size_t n) {
  if (n < CHS_HAB_LEN || b[0] != 1 || (b[2] | (b[3] << 8)) != CHS_HAB_N) return false;
  for (int i = 0; i < CHS_HAB_N; i++) chsHab[i] = (float)(int16_t)(uint16_t)(b[4 + 2 * i] | (b[5 + 2 * i] << 8)) / 5000.0f;
  return true;
}
// pengalaman (replay): CHS_RB_KEEP posisi terbaru, urut dari lama ke baru. 1 posisi = 40 byte:
// papan 64 petak x 4 bit (nilai+6) | bendera (putih, rokade<<1) | ep+1 | half | langkah(2) | ply(2) | 0
static size_t chsRbSer(uint8_t* o) {
  int n = chsRbN < CHS_RB_KEEP ? chsRbN : CHS_RB_KEEP;
  o[0] = 1; o[1] = (uint8_t)n; o[2] = 0; o[3] = 0;
  for (int j = 0; j < n; j++) {
    int idx = ((chsRbPos - n + j) % CHS_RB_N + CHS_RB_N) % CHS_RB_N;
    const uint8_t* r = chsAiRb + (size_t)idx * CHS_RB_SZ; uint8_t* d = o + 4 + (size_t)j * CHS_RB_REC;
    for (int k = 0; k < 32; k++) d[k] = (uint8_t)(((int8_t)r[2 * k] + 6) | (((int8_t)r[2 * k + 1] + 6) << 4));
    d[32] = (uint8_t)((r[64] ? 1 : 0) | ((r[65] & 15) << 1)); d[33] = (uint8_t)((int8_t)r[66] + 1); d[34] = r[67];
    d[35] = r[68]; d[36] = r[69]; d[37] = r[70]; d[38] = r[71]; d[39] = 0;
  }
  return 4 + (size_t)n * CHS_RB_REC;
}
static bool chsRbDeser(const uint8_t* b, size_t len) {
  if (!chsAiRb || len < 4 || b[0] != 1) return false;
  int n = b[1];
  if (n > CHS_RB_KEEP || n > CHS_RB_N || len < 4 + (size_t)n * CHS_RB_REC) return false;
  for (int j = 0; j < n; j++) {                                  // validasi semuanya dulu, baru diterapkan
    const uint8_t* d = b + 4 + (size_t)j * CHS_RB_REC;
    for (int k = 0; k < 32; k++) if ((d[k] & 15) > 12 || (d[k] >> 4) > 12) return false;
    if (d[33] > 64 || ((d[35] | (d[36] << 8)) & 0xF000) > 0xB000 || (d[37] | (d[38] << 8)) > 600) return false;
  }
  for (int j = 0; j < n; j++) {
    const uint8_t* d = b + 4 + (size_t)j * CHS_RB_REC; uint8_t* r = chsAiRb + (size_t)j * CHS_RB_SZ;
    for (int k = 0; k < 32; k++) { r[2 * k] = (uint8_t)(int8_t)((int)(d[k] & 15) - 6); r[2 * k + 1] = (uint8_t)(int8_t)((int)(d[k] >> 4) - 6); }
    r[64] = d[32] & 1; r[65] = (d[32] >> 1) & 15; r[66] = (uint8_t)(int8_t)((int)d[33] - 1); r[67] = d[34];
    r[68] = d[35]; r[69] = d[36]; r[70] = d[37]; r[71] = d[38];
  }
  chsRbN = n; chsRbPos = n % CHS_RB_N; chsRbDirty = false;
  return true;
}
static size_t chsMxSer(uint8_t* o) {
  o[0] = 1; o[1] = 0; o[2] = (uint8_t)chsMxN; o[3] = (uint8_t)(chsMxN >> 8);
  memcpy(o + 4, chsMxTh, 12); memcpy(o + 16, chsMxA, 24); memcpy(o + 40, &chsMxIg, 4);
  return CHS_MX_LEN;
}
static bool chsMxDeser(const uint8_t* b, size_t n) {
  if (n < CHS_MX_LEN || b[0] != 1) return false;
  float th[3], a[6], ig; memcpy(th, b + 4, 12); memcpy(a, b + 16, 24); memcpy(&ig, b + 40, 4);
  for (int i = 0; i < 3; i++) if (!(th[i] > -5.0f && th[i] < 8.0f)) return false;
  for (int i = 0; i < 6; i++) if (!(a[i] > -1e5f && a[i] < 1e5f)) return false;
  if (!(a[0] > 0.0f && a[3] > 0.0f && a[5] > 0.0f) || !(ig > -20.0f && ig < 20.0f)) return false;
  memcpy(chsMxTh, th, 12); memcpy(chsMxA, a, 24); chsMxIg = ig; chsMxN = (uint16_t)(b[2] | (b[3] << 8));
  return true;
}
static size_t chsBookSer(uint8_t* o) {
  o[0] = (uint8_t)chsBkN; o[1] = (uint8_t)(chsBkN >> 8); o[2] = (uint8_t)chsBkNext; o[3] = (uint8_t)(chsBkNext >> 8);
  size_t p = 4;
  for (int i = 0; i < chsBkN; i++) {
    o[p++] = (uint8_t)chsBkH[i]; o[p++] = (uint8_t)(chsBkH[i] >> 8); o[p++] = (uint8_t)(chsBkH[i] >> 16); o[p++] = (uint8_t)(chsBkH[i] >> 24);
    o[p++] = (uint8_t)chsBkM[i]; o[p++] = (uint8_t)(chsBkM[i] >> 8); o[p++] = chsBkC[i];
  }
  return p;
}
#define CHS_BK_MAXLEN (4 + 7 * CHS_BK_N)
static bool chsBookDeser(const uint8_t* b, size_t len) {
  if (len < 4) return false;
  int n = b[0] | (b[1] << 8), nx = b[2] | (b[3] << 8);
  if (n > CHS_BK_N || nx >= CHS_BK_N || len < (size_t)(4 + 7 * n)) return false;
  size_t p = 4;
  for (int i = 0; i < n; i++) {
    uint16_t m = (uint16_t)(b[p + 4] | (b[p + 5] << 8));
    if (m == 0) return false;
    p += 7;
  }
  p = 4;
  for (int i = 0; i < n; i++) {
    chsBkH[i] = (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
    chsBkM[i] = (uint16_t)(b[p + 4] | (b[p + 5] << 8)); chsBkC[i] = b[p + 6];
    p += 7;
  }
  chsBkN = n; chsBkNext = (uint16_t)nx;
  return true;
}

// ---------------------------------------------------------------- level NYX v2: jebakan kebiasaan, risiko-sadar
// Pencarian minimax (di chsAiChoose) sudah menyaring kandidat yang AMAN (dalam margin). Di antara mereka, NYX memilih
// yang paling \"menghukum kebiasaan user\":
//   1. aiMove dijalankan; tiap balasan sah user r diberi 3 ahli: skor GAYA (jaringan+kebiasaan), keuntungan TAKTIS
//      (quiescence), keserakahan. Peluang p_r = softmax(th . fitur) -> Product of Experts; th dilatih online (chsMxObserve)
//   2. V_r = quiescence dari sisi AI setelah balasan r; M = min V_r (user membalas terbaik)
//   3. F = soft-min entropik: -(1/k) ln sum p_r exp(-k (V_r - M)) + M.  M <= F <= E.  k kecil -> rata-rata (E),
//      k besar -> terburuk (M). Balasan yg jarang tapi menyakitkan ikut dihitung -> tidak terlalu optimis
//   4. keyakinan c = 1 - H(p)/ln n (model ragu tidak dipercaya)
//   bonus = c * (F - M)  (>= 0, centipawan)
// Skor akhir kandidat: v = fs + w*GAMMA*min(bonus, CAP), w = gerbang perolehan informasi (chsMxGate): 0 bila model tidak
// menebak lebih baik dari acak -> Nyx = minimax murni; membesar hanya bila terbukti menebak langkah user.
#ifndef CHS_NYX_GAMMA
#define CHS_NYX_GAMMA  0.9f
#endif
#ifndef CHS_NYX_CAP
#define CHS_NYX_CAP    150.0f
#endif
#define CHS_NYX_TOPK   8
static float chsAiTrapBonus(uint16_t aiMove, int ply) {
  CHS_SAVE_ST;
  chsDo(aiMove);
  uint16_t rep[256]; int rn = chsGen(rep), n = 0;
  for (int i = 0; i < rn; i++) if (chsLegal(rep[i])) rep[n++] = rep[i];
  if (n < 2) { CHS_REST_ST; return 0.0f; }                      // tidak ada pilihan balasan -> tidak ada kebiasaan yang bisa dihukum
  float ctx[CHS_NC]; chsAiCtx(ctx, n, ply + 1);
  float* S = chsAiFS; float* V = chsAiQv; float* G = chsAiGv;
  chsAiAbort = false; chsAiCheckTime = false;
  float M = 1e30f;
  for (int r = 0; r < n; r++) {
    float st = chsStyleScore(rep[r], ctx); G[r] = chsMxGreed(rep[r]);
    { CHS_SAVE_ST; chsDo(rep[r]); V[r] = (float)chsAiQ(-CHS_AI_INF, CHS_AI_INF, 2); CHS_REST_ST; }
    if (V[r] < M) M = V[r];
    S[r] = chsMxTh[0] * st + chsMxTh[1] * chsMxClampQ((int)V[r]) + chsMxTh[2] * G[r];
  }
  float mx = -1e30f; for (int r = 0; r < n; r++) if (S[r] > mx) mx = S[r];
  float sum = 0; for (int r = 0; r < n; r++) { S[r] = expf(S[r] - mx); sum += S[r]; }
  float H = 0, Z = 0;
  for (int r = 0; r < n; r++) {
    float pr = S[r] / sum;
    Z += pr * expf(-CHS_MX_KAPPA * (V[r] - M));
    if (pr > 1e-6f) H -= pr * logf(pr);
  }
  CHS_REST_ST;
  float F = M - logf(Z) / CHS_MX_KAPPA;                         // soft-min entropik
  float conf = 1.0f - H / logf((float)n); if (conf < 0) conf = 0; else if (conf > 1) conf = 1;
  float b = conf * (F - M);
  return b < 0 ? 0.0f : b;
}

// ---------------------------------------------------------------- pemilih langkah
// maxDepth/budgetMs = batas pencarian. margin = selisih skor (centipawn) dari langkah terbaik yg masih dianggap "layak".
// mode 0 = terbaik | 1 = acak di antara yg layak | 2 = paling cocok dgn GAYA user di antara yg layak |
// 3 = NYX: di antara yg layak, pilih yg paling menghukum KEBIASAAN user (prediksi balasan lewat model gaya).
// ply = jumlah langkah yg sudah dimainkan (konteks utk jaringan gaya).
// Mengembalikan langkah (dari | ke<<6 | promosi<<12), atau 0 kalau tidak ada langkah sah.
static uint16_t chsAiChoose(int maxDepth, uint32_t budgetMs, int margin, int mode, int ply) {
  if (!chsAiEnsure()) return 0;
  uint16_t pm[256]; int pn = chsGen(pm), n = 0;
  for (int i = 0; i < pn; i++) if (chsLegal(pm[i])) chsAiRm[n++] = pm[i];
  if (n == 0) return 0;
  if (n == 1) return chsAiRm[0];
  if (maxDepth > CHS_AI_MAXD) maxDepth = CHS_AI_MAXD;
  for (int i = 0; i < n; i++) chsAiKy[i] = (int16_t)chsAiKey(chsAiRm[i]);
  for (int i = 0; i < n; i++) chsAiPick(chsAiRm, chsAiKy, n, i);
  uint32_t t0 = CHS_AI_NOW();
  { char c0; chsAiSp0 = (uintptr_t)&c0; }
  chsAiDeadline = t0 + budgetMs; chsAiNodes = 0;
  chsAiAge++; memset(chsAiKil, 0, sizeof(chsAiKil)); if (chsAiHis) memset(chsAiHis, 0, 2 * 4096 * 2);
  if (chsAiPath) chsAiPath[0] = (uint32_t)chsAiH64();
  if (!chsAiLmrOk) chsAiLmrInit();
  memset(chsAiStAt, 0, sizeof(chsAiStAt));
  for (int d = 1; d <= maxDepth; d++) {
    chsAiCheckTime = (d > 1); chsAiAbort = false;
    int curBest = -CHS_AI_INF; bool done = true; int bestIdx = -1;
    for (int i = 0; i < n; i++) {
      CHS_SAVE_ST;
      chsDo(chsAiRm[i]);
      int alpha = (curBest == -CHS_AI_INF) ? -CHS_AI_INF : curBest - margin;
      int s;
#if CHS_X_PVSROOT
      if (margin == 0 && curBest != -CHS_AI_INF) {                             // jendela nol dulu; hanya bila lolos dicari ulang penuh
        s = -chsAiNeg(d - 1, -curBest - 1, -curBest, 1, true);
        if (!chsAiAbort && s > curBest) s = -chsAiNeg(d - 1, -CHS_AI_INF, -curBest, 1, true);
      } else
#endif
      s = -chsAiNeg(d - 1, -CHS_AI_INF, -alpha, 1, true);
      CHS_REST_ST;
      if (chsAiAbort) { done = false; break; }
      chsAiSc[i] = s; chsAiEx[i] = (s > alpha) ? 1 : 0;
      if (chsAiEx[i] && s > curBest) { curBest = s; bestIdx = i; }
    }
#if CHS_X_PVSROOT
    if (!done && margin == 0 && bestIdx >= 0 && d > 1) {                       // iterasi terpotong waktu: langkah terbaik yg sudah terbukti di iterasi ini dipakai
      for (int i = 0; i < n; i++) chsAiFx[i] = 0;
      uint16_t tm = chsAiRm[bestIdx]; chsAiRm[bestIdx] = chsAiRm[0]; chsAiRm[0] = tm;
      chsAiFs[0] = curBest; chsAiFx[0] = 1;
      break;
    }
#endif
    if (!done) break;
    memcpy(chsAiFs, chsAiSc, (size_t)n * 4); memcpy(chsAiFx, chsAiEx, (size_t)n);
    for (int i = 1; i < n; i++) {                                              // urutkan akar: eksak dulu (skor menurun), lalu batas atas; iterasi berikut memangkas lebih awal
      uint16_t tm = chsAiRm[i]; int32_t ts = chsAiFs[i]; uint8_t te = chsAiFx[i];
      int64_t key = te ? (int64_t)ts + 100000 : (int64_t)ts; int j = i - 1;
      while (j >= 0 && (chsAiFx[j] ? (int64_t)chsAiFs[j] + 100000 : (int64_t)chsAiFs[j]) < key) { chsAiRm[j + 1] = chsAiRm[j]; chsAiFs[j + 1] = chsAiFs[j]; chsAiFx[j + 1] = chsAiFx[j]; j--; }
      chsAiRm[j + 1] = tm; chsAiFs[j + 1] = ts; chsAiFx[j + 1] = te;
    }
    if (curBest >= CHS_AI_MATE - 100 && margin == 0) break;                    // mat sudah ketemu
    if (d < maxDepth && (CHS_AI_NOW() - t0) * CHS_X_TFRAC > budgetMs) break;   // iterasi berikut hampir pasti tidak selesai
  }
  // hindari mengulang posisi saat AI sudah unggul (aturan 3 kali ulang belum ada di mesin ini)
  if (chsAiHistN > 1) {
    for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] > 150) {
      CHS_SAVE_ST; chsDo(chsAiRm[i]); uint32_t h = chsPosHash(); CHS_REST_ST;
      for (int k = 0; k < chsAiHistN; k++) if (chsAiHist[k] == h) { chsAiFs[i] -= 150; break; }
    }
  }
  int bestSc = -CHS_AI_INF;
  for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] > bestSc) bestSc = chsAiFs[i];
  int thr = bestSc - margin;
  int nc = 0, pick = -1;
  for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] >= thr) nc++;
  if (mode == 0 || nc <= 1) {
    for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] == bestSc) { pick = i; break; }
  } else if (mode == 3) {
    // NYX: kandidat aman (dalam margin) + bonus jebakan kebiasaan (lihat chsAiTrapBonus)
    float sw = chsMxGate();                                              // gerbang perolehan informasi (0 = minimax murni)
    int idx[CHS_NYX_TOPK], ni = 0;
    for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] >= thr) {   // TOPK kandidat dengan skor tertinggi
      int ps = ni;
      if (ni < CHS_NYX_TOPK) ni++; else if (chsAiFs[i] <= chsAiFs[idx[CHS_NYX_TOPK - 1]]) continue; else ps = CHS_NYX_TOPK - 1;
      while (ps > 0 && chsAiFs[idx[ps - 1]] < chsAiFs[i]) { idx[ps] = idx[ps - 1]; ps--; }
      idx[ps] = i;
    }
    float bestV = -1e30f; pick = idx[0];
    uint32_t tEnd = CHS_AI_NOW() + budgetMs / 2;                          // waktu tambahan utk analisis kebiasaan
    for (int k = 0; k < ni; k++) {
      int i = idx[k]; float bonus = 0;
      if (sw > 0 && (int32_t)(CHS_AI_NOW() - tEnd) < 0) { bonus = chsAiTrapBonus(chsAiRm[i], ply); if (bonus > CHS_NYX_CAP) bonus = CHS_NYX_CAP; }
      float v = (float)chsAiFs[i] + sw * CHS_NYX_GAMMA * bonus + (float)(CHS_AI_RAND() % 100u) * 0.04f;
      if (v > bestV) { bestV = v; pick = i; }
    }
  } else if (mode == 1) {
    int r = (int)(CHS_AI_RAND() % (uint32_t)nc);
    for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] >= thr) { if (r-- == 0) { pick = i; break; } }
  } else {
    int bc = 0; uint16_t bm = chsBookGet(chsPosHash(), &bc);
    float bestV = -1e30f, ctx[CHS_NC]; chsAiCtx(ctx, n, ply);
    float sw = (float)chsTrainN / 60.0f; if (sw > 1.0f) sw = 1.0f;           // belum terlatih -> pilih murni berdasar nilai pencarian
    for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] >= thr) {
      float v = sw * chsStyleScore(chsAiRm[i], ctx) + (float)chsAiFs[i] / 60.0f;
      if (bm && chsAiRm[i] == bm) v += 1.5f + 0.5f * (float)(bc > 4 ? 4 : bc);
      v += (float)(CHS_AI_RAND() % 100u) * 0.002f;
      if (v > bestV) { bestV = v; pick = i; }
    }
  }
  if (pick < 0) pick = 0;
  return chsAiRm[pick];
}
// ==== AI END ====
