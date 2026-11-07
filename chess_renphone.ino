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

// hook: begitu memori kerja AI siap, pengalaman (replay) terakhir dimuat dari NVS. Definisinya ada di bagian prefs di bawah
// (di luar blok AI supaya blok AI tetap bisa diuji di PC tanpa Arduino).
static void chsRbLoadNvs();
#define CHS_AI_AFTER_ALLOC() chsRbLoadNvs()

// ==== AI BEGIN ====
// Mesin AI + model GAYA PEMAIN yang belajar online. Dipasang di atas mesin aturan (papan global chsBd, dst),
// tanpa Arduino/LovyanGFX -> diuji di PC (tests_chess/). HANYA dipanggil dari loop UI: pencarian memakai-ulang
// papan global lalu mengembalikannya persis seperti semula.
//
// EMPAT BAGIAN
//  1. Pencarian negamax alpha-beta + quiescence + evaluasi materi/PST. Lima tingkat lewat chsAiChoose().
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
#define CHS_AI_PLY    24
#define CHS_AI_HIST   320
#define CHS_BK_N      128

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

static bool chsAiEnsure() {
  if (chsAiMem) return true;
  size_t szMv = (size_t)CHS_AI_PLY * 256 * 2, szFB = (size_t)256 * CHS_NIN * 4;
  size_t total = szMv * 2 + 512 + 1024 * 2 + 256 * 2 + szFB + 1024 + (size_t)CHS_AI_HIST * 4 + (size_t)CHS_NP * 4 + (size_t)CHS_RB_N * CHS_RB_SZ + 64;
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
  chsAiRb = p;
  chsRbN = 0; chsRbPos = 0;
  CHS_AI_AFTER_ALLOC();
  return true;
}
static void chsAiFree() {
  if (chsAiMem) CHS_AI_FREE(chsAiMem);
  chsAiMem = nullptr; chsAiMv = nullptr; chsAiKy = nullptr; chsAiRm = nullptr; chsAiSc = nullptr; chsAiFs = nullptr;
  chsAiEx = nullptr; chsAiFx = nullptr; chsAiFB = nullptr; chsAiFS = nullptr; chsAiHist = nullptr; chsAiG = nullptr; chsAiRb = nullptr; chsRbN = 0; chsRbPos = 0; chsAiHistN = 0;
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

// ---------------------------------------------------------------- evaluasi (dari sudut pandang yg jalan)
static int chsAiEval() {
  int sc = 0, npm = 0;
  for (int sq = 0; sq < 64; sq++) { int8_t p = chsBd[sq]; if (!p) continue; int t = p > 0 ? p : -p; if (t >= 2 && t <= 5) npm += chsAiVal[t]; }
  bool eg = npm <= 2600;
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = chsBd[sq]; if (!p) continue;
    int t = p > 0 ? p : -p, r = sq >> 3, f = sq & 7;
    int idx = p > 0 ? (7 - r) * 8 + f : r * 8 + f;
    int v = chsAiVal[t] + (t == 6 ? (eg ? chsPstKe[idx] : chsPst[5][idx]) : chsPst[t - 1][idx]);
    sc += p > 0 ? v : -v;
  }
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
static inline bool chsAiTimeUp() {
  if (chsAiCheckTime && (chsAiNodes & 255u) == 0 && (int32_t)(CHS_AI_NOW() - chsAiDeadline) >= 0) chsAiAbort = true;
  return chsAiAbort;
}

// ---------------------------------------------------------------- pencarian
static int chsAiQ(int alpha, int beta, int ply) {
  chsAiNodes++;
  if (chsAiTimeUp()) return 0;
  int stand = chsAiEval();
  if (ply >= CHS_AI_PLY - 1) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  uint16_t* mv = chsAiMv + ply * 256; int16_t* ky = chsAiKy + ply * 256;
  int n = chsGen(mv), nc = 0;
  for (int i = 0; i < n; i++) {
    uint16_t m = mv[i]; int from = m & 63, to = (m >> 6) & 63;
    int8_t p = chsBd[from];
    bool capt = chsBd[to] != 0 || ((p == 1 || p == -1) && to == chsEp && (from & 7) != (to & 7)) || ((m >> 12) & 15);
    if (capt) { int k = chsAiKey(m); mv[nc] = m; ky[nc] = (int16_t)k; nc++; }
  }
  for (int i = 0; i < nc; i++) {
    chsAiPick(mv, ky, nc, i);
    CHS_SAVE_ST;
    bool mover = chsWhite;
    chsDo(mv[i]);
    int ks = chsKingSq(mover);
    if (ks < 0 || chsAtt(ks, !mover)) { CHS_REST_ST; continue; }
    int sc = -chsAiQ(-beta, -alpha, ply + 1);
    CHS_REST_ST;
    if (chsAiAbort) return 0;
    if (sc >= beta) return sc;
    if (sc > alpha) alpha = sc;
  }
  return alpha;
}

static int chsAiNeg(int depth, int alpha, int beta, int ply) {
  chsAiNodes++;
  if (chsAiTimeUp()) return 0;
  if (chsHalf >= 100) return 0;
  bool inchk = chsInCheck();
  if (inchk && ply < CHS_AI_PLY - 6) depth++;
  if (depth <= 0) return chsAiQ(alpha, beta, ply);
  if (ply >= CHS_AI_PLY - 1) return chsAiEval();
  uint16_t* mv = chsAiMv + ply * 256; int16_t* ky = chsAiKy + ply * 256;
  int n = chsGen(mv);
  for (int i = 0; i < n; i++) ky[i] = (int16_t)chsAiKey(mv[i]);
  int best = -CHS_AI_INF, legal = 0;
  for (int i = 0; i < n; i++) {
    chsAiPick(mv, ky, n, i);
    CHS_SAVE_ST;
    bool mover = chsWhite;
    chsDo(mv[i]);
    int ks = chsKingSq(mover);
    if (ks < 0 || chsAtt(ks, !mover)) { CHS_REST_ST; continue; }
    legal++;
    int sc = -chsAiNeg(depth - 1, -beta, -alpha, ply + 1);
    CHS_REST_ST;
    if (chsAiAbort) return 0;
    if (sc > best) best = sc;
    if (sc > alpha) alpha = sc;
    if (alpha >= beta) break;
  }
  if (!legal) return inchk ? -CHS_AI_MATE + ply : 0;
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

// Dipanggil SEBELUM langkah user dijalankan (posisi masih sebelum langkah).
// Selain belajar dari langkah ini, ulang beberapa contoh lama (experience replay) supaya jaringan kecil ini
// cepat belajar dari sedikit data.
static void chsStyleLearn(uint16_t played, int ply) {
  if (!chsAiEnsure()) return;
  chsNetEnsure();
  chsBookAdd(chsPosHash(), played);
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
  chsNetInit(); chsTrainN = 0; chsBkN = 0; chsBkNext = 0; chsRbN = 0; chsRbPos = 0; chsStyleDirty = true;
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

// ---------------------------------------------------------------- pemilih langkah
// maxDepth/budgetMs = batas pencarian. margin = selisih skor (centipawn) dari langkah terbaik yg masih dianggap "layak".
// mode 0 = terbaik | 1 = acak di antara yg layak | 2 = paling cocok dgn GAYA user di antara yg layak.
// ply = jumlah langkah yg sudah dimainkan (konteks utk jaringan gaya).
// Mengembalikan langkah (dari | ke<<6 | promosi<<12), atau 0 kalau tidak ada langkah sah.
static uint16_t chsAiChoose(int maxDepth, uint32_t budgetMs, int margin, int mode, int ply) {
  if (!chsAiEnsure()) return 0;
  uint16_t pm[256]; int pn = chsGen(pm), n = 0;
  for (int i = 0; i < pn; i++) if (chsLegal(pm[i])) chsAiRm[n++] = pm[i];
  if (n == 0) return 0;
  if (n == 1) return chsAiRm[0];
  for (int i = 0; i < n; i++) chsAiKy[i] = (int16_t)chsAiKey(chsAiRm[i]);
  for (int i = 0; i < n; i++) chsAiPick(chsAiRm, chsAiKy, n, i);
  uint32_t t0 = CHS_AI_NOW();
  chsAiDeadline = t0 + budgetMs; chsAiNodes = 0;
  for (int d = 1; d <= maxDepth; d++) {
    chsAiCheckTime = (d > 1); chsAiAbort = false;
    int curBest = -CHS_AI_INF; bool done = true;
    for (int i = 0; i < n; i++) {
      CHS_SAVE_ST;
      chsDo(chsAiRm[i]);
      int alpha = (curBest == -CHS_AI_INF) ? -CHS_AI_INF : curBest - margin;
      int s = -chsAiNeg(d - 1, -CHS_AI_INF, -alpha, 1);
      CHS_REST_ST;
      if (chsAiAbort) { done = false; break; }
      chsAiSc[i] = s; chsAiEx[i] = (s > alpha) ? 1 : 0;
      if (chsAiEx[i] && s > curBest) curBest = s;
    }
    if (!done) break;
    memcpy(chsAiFs, chsAiSc, (size_t)n * 4); memcpy(chsAiFx, chsAiEx, (size_t)n);
    int b = 0;
    for (int i = 1; i < n; i++) if (chsAiFx[i] && (!chsAiFx[b] || chsAiFs[i] > chsAiFs[b])) b = i;
    if (b != 0) {
      uint16_t tm = chsAiRm[0]; chsAiRm[0] = chsAiRm[b]; chsAiRm[b] = tm;
      int32_t ts = chsAiFs[0]; chsAiFs[0] = chsAiFs[b]; chsAiFs[b] = ts;
      uint8_t te = chsAiFx[0]; chsAiFx[0] = chsAiFx[b]; chsAiFx[b] = te;
    }
    if (curBest >= CHS_AI_MATE - 100 && margin == 0) break;                    // mat sudah ketemu
    if (d < maxDepth && (CHS_AI_NOW() - t0) * 3 > budgetMs) break;             // iterasi berikut hampir pasti tidak selesai
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

// ---------------------------------------------------------------------
// STATE PERMAINAN + JARINGAN
// ---------------------------------------------------------------------
#define CHS_PORT      8883
#define CHS_BUF       1536
#define CHS_MAX_PLY   280
#define CHS_RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xF8)>>3)))
#define CHS_LIGHT     CHS_RGB(238,222,192)
#define CHS_DARK      CHS_RGB(170,125,92)

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

// ---- animasi langkah: bidak meluncur dari petak asal ke tujuan (ease-out), bidak yg dimakan memudar ----
#define CHS_ANIM_MS 210
static bool          chsAnimOn = false;
static unsigned long chsAnimMs = 0;
static int           chsAnimFrom = 0, chsAnimTo = 0;
// ---- BROKER MQTT KHUSUS CATUR: terpisah total dari app Chat (NVS "chess": bhost/buser/bpass; SD: /chess.cfg) ----
static String chsBHost = "", chsBUser = "", chsBPass = "";
static int8_t chsBEdit = -1;                    // field yg sedang diketik di layar Broker (-1 = tidak ada)
static void chsBrokerLoad() {
  Preferences p;
  if (p.begin("chess", true)) {
    chsBHost = p.getString("bhost", ""); chsBUser = p.getString("buser", ""); chsBPass = p.getString("bpass", "");
    p.end();
  }
}
static void chsBrokerSave() {
  chsBHost.trim(); chsBUser.trim(); chsBPass.trim();
  Preferences p;
  if (p.begin("chess", false)) {
    p.putString("bhost", chsBHost); p.putString("buser", chsBUser); p.putString("bpass", chsBPass);
    p.end();
  }
}
static bool chsBrokerOk() { return chsBHost.length() > 3 && chsBUser.length() > 0 && chsBPass.length() > 0; }
static bool chsBrokerFromSd() {                 // /chess.cfg: baris host= user= pass=
  if (!sdReady || !SD_MMC.exists("/chess.cfg")) return false;
  File f = SD_MMC.open("/chess.cfg", FILE_READ);
  if (!f) return false;
  String h = "", u = "", w = "";
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.startsWith("host="))      h = l.substring(5);
    else if (l.startsWith("user=")) u = l.substring(5);
    else if (l.startsWith("pass=")) w = l.substring(5);
  }
  f.close();
  h.trim(); u.trim(); w.trim();
  if (!h.length() || !u.length() || !w.length()) return false;
  chsBHost = h; chsBUser = u; chsBPass = w;
  return true;
}
// host bersih (tanpa skema & path); ":port" opsional, bawaan 8883 (TLS)
static void chsBrokerHostPort(String& h, int& port) {
  h = chsBHost; h.trim(); port = CHS_PORT;
  int i = h.indexOf("://"); if (i >= 0) h = h.substring(i + 3);
  int sl = h.indexOf('/'); if (sl >= 0) h = h.substring(0, sl);
  int co = h.indexOf(':');
  if (co >= 0) { int pp = h.substring(co + 1).toInt(); if (pp > 0 && pp < 65536) port = pp; h = h.substring(0, co); }
}

// ---- polish tambahan: panel bisa disembunyikan, fade-in titik langkah, denyut skak, popup promosi, efek tekan ----
static bool          chsPanelHidden = false;    // pilihan pemain (disimpan di NVS "chess"/"hide")
static unsigned long chsSelMs = 0;              // kapan bidak dipilih (fade-in titik langkah)
static unsigned long chsPromoMs = 0;            // kapan popup promosi dibuka
static bool          chsInChk = false;          // sisi yg jalan sedang skak (utk denyut)
static int8_t        chsPressId = -1;           // tombol yg barusan ditekan: 0 kiri,1 kanan,2 gagang,3 Acak,4 Mulai
static unsigned long chsPressMs = 0;
#define CHS_PRESS_MS 160
static inline bool chsPanelOff() { return chsPanelHidden && chsEnd == 0 && chsScr == 1; }   // panel otomatis muncul lagi saat game selesai
static void chsPress(int id) { chsPressId = (int8_t)id; chsPressMs = millis(); needRedraw = true; }
static uint16_t chsPressCol(int id, uint16_t c) {                          // warna tombol menggelap sesaat setelah ditekan
  if (chsPressId == id && millis() - chsPressMs < CHS_PRESS_MS) return blend565(c, 0x0000, 70);
  return c;
}
static int8_t        chsAnimCap = 0;            // bidak yg dimakan (bertanda), 0 = tidak ada
static int           chsAnimRF = -1, chsAnimRT = -1;   // benteng yg ikut meluncur saat rokade (-1 = tidak ada)
static void chsAnimStart(int from, int to, int8_t cap) {
  chsAnimFrom = from; chsAnimTo = to; chsAnimCap = cap;
  chsAnimRF = chsAnimRT = -1;
  int8_t mp = chsBd[to];
  if ((mp == 6 || mp == -6) && (to - from == 2 || from - to == 2)) {          // rokade: benteng ikut bergerak
    int rf = to > from ? from + 3 : from - 4, rt = to > from ? from + 1 : from - 1;
    if (rf >= 0 && rf < 64 && chsBd[rt] == (mp > 0 ? 4 : -4)) { chsAnimRF = rf; chsAnimRT = rt; }
  }
  chsAnimMs = millis(); chsAnimOn = true;
}

// ---------------------------------------------------------------------
// MODE LAWAN AI + AI PENGGANTI SAAT AFK
// ---------------------------------------------------------------------
static bool          chsVsAI = false;          // true = lawan AI (tanpa jaringan)
static uint8_t       chsAiLevel = 1;           // 0 Mudah, 1 Sedang, 2 Sulit, 3 Cermin (AI meniru gaya user)
static uint8_t       chsAfkIdx = 2;            // indeks chsAfkSec: 0 = mati
static bool          chsAfkAI = false;         // AI sedang memainkan giliran user (user AFK)
static bool          chsAiUserWhite = true;
static bool          chsAiQueued = false, chsPrevMyTurn = false;
static unsigned long chsAiDueMs = 0, chsLastTouchMs = 0, chsTurnStartMs = 0;
static uint8_t       chsUnsaved = 0;
static const char* const chsLvName[4] = {"Mudah", "Sedang", "Sulit", "Cermin"};
static const uint16_t chsAfkSec[4] = {0, 30, 60, 120};
static const uint8_t  chsLvDepth[4]  = {2, 4, 6, 4};
static const uint16_t chsLvMs[4]     = {250, 700, 1800, 700};
static const int16_t  chsLvMargin[4] = {120, 10, 0, 70};
static const uint8_t  chsLvMode[4]   = {1, 1, 0, 2};       // 0 terbaik | 1 acak di antara yg layak | 2 gaya user

static void chsPrefsLoad() {
  Preferences p;
  if (!p.begin("chess", true)) return;
  uint8_t lv = p.getUChar("lvl", 1); if (lv < 4) chsAiLevel = lv;
  uint8_t af = p.getUChar("afk", 2); if (af < 4) chsAfkIdx = af;
  uint8_t* sb = (uint8_t*)malloc(CHS_STY_LEN + CHS_BK_MAXLEN + 16);          // heap, bukan stack (task loop cuma 8 KB)
  if (sb) {
    size_t L = p.getBytesLength("sty");
    bool styOk = false;                                                         // sty versi lama / rusak -> jaringan DAN kebiasaan mulai dari awal (hab dilatih bersama jaringannya)
    if (L >= CHS_STY_LEN && L <= CHS_STY_LEN + 8) { p.getBytes("sty", sb, L); styOk = chsStyleDeser(sb, L); }
    L = p.getBytesLength("bk");
    if (L >= 4 && L <= CHS_BK_MAXLEN) { p.getBytes("bk", sb, L); chsBookDeser(sb, L); }
    L = p.getBytesLength("hab");
    if (styOk && L == CHS_HAB_LEN) { p.getBytes("hab", sb, L); chsHabDeser(sb, L); }       // kebiasaan petak (baru; perangkat lama: kosong)
    free(sb);
  }
  p.end();
}
// pengalaman (replay) dimuat saat memori kerja AI dialokasikan (dipanggil lewat CHS_AI_AFTER_ALLOC, dari loop UI)
static void chsRbLoadNvs() {
  Preferences p;
  if (!p.begin("chess", true)) return;
  size_t L = p.getBytesLength("rb");
  if (L >= 4 && L <= CHS_RB_MAXLEN) {
    uint8_t* sb = (uint8_t*)malloc(CHS_RB_MAXLEN + 8);
    if (sb) { p.getBytes("rb", sb, L); chsRbDeser(sb, L); free(sb); }
  }
  p.end();
}
static void chsPrefsSaveCfg() {
  Preferences p;
  if (p.begin("chess", false)) { p.putUChar("lvl", chsAiLevel); p.putUChar("afk", chsAfkIdx); p.end(); }
}
// Semua blob dilayani buffer yg sama (sty 1804 | bk <= 900 | hab 900 | rb <= 1604 byte).
static_assert(CHS_HAB_LEN <= CHS_STY_LEN + CHS_BK_MAXLEN + 16 && CHS_RB_MAXLEN <= CHS_STY_LEN + CHS_BK_MAXLEN + 16, "buffer NVS kurang");
// withRb=false = simpan ringan berkala (jaringan + kebiasaan + buku); pengalaman (rb) hanya ditulis saat akhir partai / keluar,
// supaya flash/NVS tidak ditulis 1,6 KB ekstra tiap 8 langkah.
static void chsStyleSaveEx(bool withRb) {
  bool sd = chsStyleDirty, rd = withRb && chsRbDirty && chsAiRb;
  if (!sd && !rd) return;
  Preferences p;
  if (!p.begin("chess", false)) return;
  uint8_t* sb = (uint8_t*)malloc(CHS_STY_LEN + CHS_BK_MAXLEN + 16);
  if (sb) {
    if (sd) {
      size_t L = chsStyleSer(sb); p.putBytes("sty", sb, L);
      L = chsBookSer(sb); p.putBytes("bk", sb, L);
      L = chsHabSer(sb); p.putBytes("hab", sb, L);
    }
    if (rd) { size_t L = chsRbSer(sb); p.putBytes("rb", sb, L); }
    free(sb);
    if (sd) { chsStyleDirty = false; chsUnsaved = 0; }
    if (rd) chsRbDirty = false;
  }
  p.end();
}
static void chsStyleSave() { chsStyleSaveEx(true); }

static void chsSetErr(const char* m) { strncpy(chsErr, m, sizeof(chsErr) - 1); chsErr[sizeof(chsErr) - 1] = 0; }

static bool chsEnsureInit();
static void chsTaskFunc(void*);

static void chsUpdateEnd() {
  chsEnd = chsRes[0] ? 6 : chsStatus();
  if (chsEnd == 0 && chsPly >= CHS_MAX_PLY) chsEnd = 5;
  chsInChk = (chsEnd == 0) && chsInCheck();
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
    if (!chsBrokerOk()) {
      chsConn = 2; chsSetErr("Broker Catur belum diatur"); chsDirty = true;
      vTaskDelay(pdMS_TO_TICKS(1500)); continue;
    }
    String host; int port; chsBrokerHostPort(host, port);
    String user = chsBUser, pass = chsBPass;
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
      mqtt.setServer(host.c_str(), port);
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
  int8_t oldBd[64]; memcpy(oldBd, chsBd, 64);
  int prevPly = (int)chsPly;
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
  if (oppMoved && ply == prevPly + 1 && chsLast >= 0) chsAnimStart(chsLast & 63, (chsLast >> 6) & 63, oldBd[(chsLast >> 6) & 63]);
  else chsAnimOn = false;
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
static void chsCommitEx(uint16_t m, bool learn) {
  char t[8]; chsMoveStr(m, t);
  size_t L = strlen(chsMoves);
  if (L + 8 > CHS_BUF - 40) return;
  if (learn) { chsStyleLearn(m, (int)chsPly); chsUnsaved++; }       // belajar dari langkah USER sebelum langkah dijalankan
  if (L) chsMoves[L++] = ' ';
  strcpy(chsMoves + L, t);
  int8_t capP = chsBd[(m >> 6) & 63];
  chsDo(m);
  chsAnimStart(m & 63, (m >> 6) & 63, capP);
  chsPly++;
  chsLast = (m & 63) | (((m >> 6) & 63) << 6);
  chsSel = -1; chsPromoOn = false;
  chsUpdateEnd();
  vibTap();
  if (!chsVsAI) chsQueueSend();
  if (chsStyleDirty && (chsUnsaved >= 8 || chsEnd != 0)) chsStyleSaveEx(chsEnd != 0);
  needRedraw = true;
}
static void chsCommit(uint16_t m) { chsCommitEx(m, true); }     // langkah user (ikut melatih model gaya)

static void chsNewGameLocal() {
  chsReset();
  chsMoves[0] = 0; chsPly = 0; chsRes[0] = 0; chsLast = -1; chsSel = -1;
  chsPromoOn = false; chsConfirm = false; chsAnimOn = false;
  chsUpdateEnd();
}

static void chsAiSeats() {                      // kursi mode lawan AI: user = chsRole, AI = sisanya
  chsRole = chsAiUserWhite ? 1 : 2;
  const char* me = chatName.length() ? chatName.c_str() : "Kamu";
  char an[24]; snprintf(an, sizeof(an), "AI %s", chsLvName[chsAiLevel]);
  if (chsRole == 1) { strcpy(chsWPid, chsPid); strcpy(chsBPid, "ai"); strncpy(chsWName, me, 23); chsWName[23] = 0; strcpy(chsBName, an); }
  else              { strcpy(chsBPid, chsPid); strcpy(chsWPid, "ai"); strncpy(chsBName, me, 23); chsBName[23] = 0; strcpy(chsWName, an); }
}
static void chsRematch() {
  if (chsVsAI) {                                // ganti warna tiap ulang
    chsAiUserWhite = !chsAiUserWhite; chsAiSeats(); chsNewGameLocal(); chsAiQueued = false; needRedraw = true; return;
  }
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
  if (!chsVsAI) chsQueueSend();
  chsStyleSave();
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
  if (!chsBrokerOk()) { showToast("Atur broker Catur dulu", 2400); return; }
  chsSanitizeRoom();
  if (chsRoomStr.length() < 3) { showToast("Kode room minimal 3 karakter", 2200); return; }
  strncpy(chsRoom, chsRoomStr.c_str(), 9); chsRoom[9] = 0;
  { Preferences p; if (p.begin("chess", false)) { p.putString("room", chsRoomStr); p.end(); } }
  chsVsAI = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false; chsLastTouchMs = millis(); chsTurnStartMs = millis();
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

// Mulai partai lawan AI (tanpa broker / jaringan)
static void chsStartAi() {
  if (!chsMoves) { showToast("RAM tidak cukup utk Catur", 2200); return; }
  if (!chsAiEnsure()) { showToast("PSRAM tidak cukup utk AI", 2200); return; }
  chsActive = false;
  chsVsAI = true; chsAfkAI = false; chsAiQueued = false;
  chsAiSeats();
  chsGenNo = 0; chsNewGameLocal();
  chsClaimW = chsClaimB = false; chsRemoteGen = 0; chsRemotePly = 0; chsRemoteRes[0] = 0;
  chsSendG = false; chsPubSeat = 0;
  chsReady = true; chsConn = 1; chsErr[0] = 0;
  chsLastTouchMs = millis();
  chsScr = 1;
  needRedraw = true;
}

// Giliran AI (mode lawan AI): beri jeda singkat supaya animasi langkah user & teks "AI berpikir" sempat tampil
static bool chsAiTurn() { return chsVsAI && chsEnd == 0 && (chsRole == 1 || chsRole == 2) && ((chsRole == 1) != chsWhite); }
static uint16_t chsAiMoveFor(int lv) {
  chsAiHistBuild(chsMoves);
  uiBoostKick();                                 // clock penuh selama berpikir
  return chsAiChoose(chsLvDepth[lv], chsLvMs[lv], chsLvMargin[lv], chsLvMode[lv], (int)chsPly);
}
static void chsAiPollVs() {
  if (!chsAiTurn() || chsAnimOn) { chsAiQueued = false; return; }
  if (!chsAiQueued) { chsAiQueued = true; chsAiDueMs = millis() + 450UL; needRedraw = true; return; }
  if ((long)(millis() - chsAiDueMs) < 0) return;
  chsAiQueued = false;
  uint16_t m = chsAiMoveFor(chsAiLevel);
  if (m) chsCommitEx(m, false);                  // langkah AI tidak melatih model gaya
}
// Online: kalau user diam melewati batas AFK, AI bergaya-user memainkan giliran supaya lawan tidak menunggu
static void chsAfkPoll() {
  bool mine = chsCanMove();
  if (mine && !chsPrevMyTurn) chsTurnStartMs = millis();
  chsPrevMyTurn = mine;
  if (!mine || chsAfkSec[chsAfkIdx] == 0 || chsAnimOn || chsPromoOn || chsSendG) return;
  unsigned long idleSince = chsLastTouchMs > chsTurnStartMs ? chsLastTouchMs : chsTurnStartMs;
  unsigned long need = chsAfkAI ? 1500UL : (unsigned long)chsAfkSec[chsAfkIdx] * 1000UL;
  if (millis() - idleSince < need) return;
  if (!chsAfkAI) { chsAfkAI = true; chsTurnStartMs = millis(); chsSel = -1; needRedraw = true; return; }   // tampilkan dulu "AI bermain utk kamu"
  uint16_t m = chsAiMoveFor(3);
  if (m) chsCommitEx(m, false);
}

// ---------------------------------------------------------------------
// SIKLUS HIDUP APP
// ---------------------------------------------------------------------
void chessEnter() {
  if (!chsEnsureInit()) { showToast("RAM tidak cukup utk Catur", 2500); return; }
  chsBrokerLoad();
  if (!chsBrokerOk() && chsBrokerFromSd()) { chsBrokerSave(); showToast("Broker Catur dimuat dari /chess.cfg", 2200); }
  chsBEdit = -1;
  chsPrefsLoad();
  chsVsAI = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false;
  chsActive = false; chsConn = 0; chsReady = false;
  chsScr = 0; chsSel = -1; chsPromoOn = false; chsConfirm = false;
  chsReset(); chsEnd = 0;
  { Preferences p; if (p.begin("chess", true)) { chsPanelHidden = p.getBool("hide", false); p.end(); } }
  chsPressId = -1; chsInChk = false;
}

static void chsFreeMasks();
static void chsVwClose();
void chessExit() {
  chsActive = false;        // task melepas koneksi TLS sendiri
  chsConn = 0;
  chsAnimOn = false;
  chsStyleSave();           // simpan hasil belajar gaya
  chsVsAI = false; chsAfkAI = false; chsAiQueued = false;
  chsAiFree();
  chsVwClose();             // bobot awal layar Jaringan (PSRAM)
  chsFreeMasks();           // topeng bidak (PSRAM) dibebaskan saat keluar
}

void chessLoopPoll() {
  if (chsDirty) { chsDirty = false; if (curScreen() == SCR_CHESS) needRedraw = true; }
  if (curScreen() == SCR_CHESS && chsPressId >= 0 && millis() - chsPressMs >= CHS_PRESS_MS) { chsPressId = -1; needRedraw = true; }   // efek tekan selesai
  if (curScreen() != SCR_CHESS || chsScr != 1 || !chsMoves) return;
  if (chsVsAI) chsAiPollVs(); else { chsProcessRx(); chsAfkPoll(); }
  if (chsConfirm && millis() - chsConfirmMs > 3500UL) { chsConfirm = false; needRedraw = true; }
  if (chsAnimOn) {                                    // animasi langkah: redraw tiap loop + CPU dinaikkan sebentar
    uiBoostKick();
    needRedraw = true;
    if (millis() - chsAnimMs >= CHS_ANIM_MS) chsAnimOn = false;
  }
  if (chsSel >= 0 && millis() - chsSelMs < 190UL) { uiBoostKick(); needRedraw = true; }          // fade-in titik langkah
  if (chsPromoOn && millis() - chsPromoMs < 170UL) { uiBoostKick(); needRedraw = true; }          // popup promosi membesar
  if (chsInChk) { static unsigned long lp = 0; if (millis() - lp >= 45UL) { lp = millis(); needRedraw = true; } }   // denyut raja skak (~22 fps)
}

// ---------------------------------------------------------------------
// GAMBAR
// ---------------------------------------------------------------------
static void chsGeom(int* bx, int* by, int* sq, int* px, int* py, int* pw, int* ph) {
  *by = STATUS_H + 3;
  if (SCR_W >= SCR_H) {
    int s = (SCR_H - STATUS_H - 3 - BACK_H - 8) / 8;
    if (s < 18) s = 18;
    *sq = s; *bx = chsPanelOff() ? (SCR_W - 8 * s) / 2 : 6; *px = 6 + 8 * s + 8; *py = *by; *pw = SCR_W - *px - 6; *ph = SCR_H - *py - 6;
  } else {
    int s = (SCR_W - 8) / 8;
    int s2 = (SCR_H - STATUS_H - 3 - 6 - 96 - BACK_H - 8) / 8;
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
  int minY = py + 5 * gap + 12;
  if (SCR_W >= SCR_H && yy > minY) yy = minY;
  int bw = (pw - 18) / 2;
  *x = px + 6 + i * (bw + 6); *y = yy; *w = bw; *h = bh;
}

// gagang (panah kecil) utk menyembunyikan / menampilkan panel; area sentuh diperluas 4 px tiap sisi
static void chsHandleRect(int* x, int* y, int* w, int* h) {
  int bx, by, sq, px, py, pw, ph;
  chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);
  *w = 18; *h = 15;
  if (chsPanelOff()) { *x = bx + 8 * sq - 18; *y = by + 8 * sq + 6; }
  else               { *x = px + pw - 5 - 18; *y = py + 5; }
}
static bool chsInHandle(int x, int y) {
  int hx, hy, hw, hh; chsHandleRect(&hx, &hy, &hw, &hh);
  return x >= hx - 4 && x < hx + hw + 4 && y >= hy - 4 && y < hy + hh + 4;
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

static void chsDrawPieceVec(LGFX_Sprite& s, int t, bool white, int ox, int oy, int sq) {
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

// ---------------------------------------------------------------------
// BIDAK HALUS: tiap bidak = gabungan primitif (lingkaran / kotak bulat / segitiga) di kotak 10x10.
// Dirasterisasi SEKALI per ukuran petak dgn supersampling 4x4 -> tiga topeng 0..16 per bidak
// (isi, garis tepi, detail). Menggambar = memadukan topeng dgn latar (tepi anti-alias, isi
// bergradasi, bayangan lembut). Tabel berupa float biasa (tanpa struct) -> aman dari bug prototype.
// Tiap primitif = 7 float: {jenis, v0..v5}. Akhir bidak = {99,...}.
// ---------------------------------------------------------------------
#define CHS_PK_CIRCLE 0    // v0=cx v1=cy v2=r
#define CHS_PK_BOX    1    // v0=x0 v1=y0 v2=x1 v3=y1 v4=radius sudut
#define CHS_PK_TRI    2    // v0..v5 = tiga titik
#define CHS_PK_DOT    10   // detail: titik  v0=cx v1=cy v2=r
#define CHS_PK_LINE   13   // detail: garis  v0=x0 v1=y0 v2=x1 v3=y1 v4=setengah tebal
#define CHS_PK_END    99
static const float CHS_PRIM[] = {
  // 1 pion
  1, 2.7f, 7.4f, 7.3f, 8.7f, 0.55f, 0,    2, 5.0f, 3.9f, 3.5f, 7.6f, 6.5f, 7.6f,
  1, 3.7f, 3.9f, 6.3f, 4.6f, 0.30f, 0,    0, 5.0f, 3.0f, 1.55f, 0, 0, 0,
  99, 0, 0, 0, 0, 0, 0,
  // 2 kuda
  1, 2.6f, 7.4f, 7.4f, 8.7f, 0.55f, 0,    2, 3.0f, 7.6f, 7.2f, 7.6f, 6.8f, 3.2f,
  2, 6.8f, 3.2f, 3.0f, 7.6f, 3.9f, 4.6f,  2, 3.9f, 4.6f, 2.2f, 5.9f, 3.7f, 3.3f,
  0, 5.2f, 3.1f, 1.7f, 0, 0, 0,           2, 5.0f, 1.2f, 6.6f, 2.5f, 4.7f, 2.7f,
  10, 4.4f, 3.4f, 0.38f, 0, 0, 0,         13, 6.4f, 3.2f, 6.9f, 6.2f, 0.20f, 0,
  99, 0, 0, 0, 0, 0, 0,
  // 3 gajah
  1, 2.6f, 7.4f, 7.4f, 8.7f, 0.55f, 0,    2, 5.0f, 3.4f, 3.3f, 7.6f, 6.7f, 7.6f,
  0, 5.0f, 3.2f, 1.6f, 0, 0, 0,           2, 5.0f, 0.9f, 3.7f, 3.0f, 6.3f, 3.0f,
  0, 5.0f, 1.0f, 0.65f, 0, 0, 0,          13, 4.3f, 3.9f, 5.7f, 2.4f, 0.22f, 0,
  99, 0, 0, 0, 0, 0, 0,
  // 4 benteng
  1, 2.4f, 7.4f, 7.6f, 8.7f, 0.55f, 0,    1, 3.3f, 3.6f, 6.7f, 7.6f, 0.30f, 0,
  1, 2.7f, 2.4f, 7.3f, 3.8f, 0.30f, 0,    1, 2.7f, 1.1f, 3.9f, 2.6f, 0.20f, 0,
  1, 4.4f, 1.1f, 5.6f, 2.6f, 0.20f, 0,    1, 6.1f, 1.1f, 7.3f, 2.6f, 0.20f, 0,
  99, 0, 0, 0, 0, 0, 0,
  // 5 menteri
  1, 2.4f, 7.4f, 7.6f, 8.7f, 0.55f, 0,    2, 5.0f, 2.9f, 2.9f, 7.6f, 7.1f, 7.6f,
  2, 2.8f, 2.4f, 7.2f, 2.4f, 5.0f, 5.4f,  0, 2.8f, 2.2f, 0.80f, 0, 0, 0,
  0, 5.0f, 1.4f, 0.90f, 0, 0, 0,          0, 7.2f, 2.2f, 0.80f, 0, 0, 0,
  99, 0, 0, 0, 0, 0, 0,
  // 6 raja
  1, 2.4f, 7.4f, 7.6f, 8.7f, 0.55f, 0,    2, 5.0f, 3.7f, 2.9f, 7.6f, 7.1f, 7.6f,
  0, 5.0f, 4.2f, 1.35f, 0, 0, 0,          1, 4.5f, 0.5f, 5.5f, 3.3f, 0.25f, 0,
  1, 3.7f, 1.2f, 6.3f, 2.2f, 0.25f, 0,
  99, 0, 0, 0, 0, 0, 0,
};

static uint8_t* chsMask = nullptr;     // [6 bidak][3 bidang: isi, tepi, detail][sq*sq], nilai 0..16
static int      chsMaskSq = 0;
static void chsFreeMasks() { if (chsMask) { free(chsMask); chsMask = nullptr; } chsMaskSq = 0; }

static float chsSdSeg(float px, float py, float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  float t = l2 > 0.0f ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float qx = x0 + t * dx - px, qy = y0 + t * dy - py;
  return sqrtf(qx * qx + qy * qy);
}

// jarak bertanda ke satu primitif (negatif = di dalam)
static float chsSdPrim(const float* q, float px, float py) {
  switch ((int)q[0]) {
    case CHS_PK_CIRCLE: case CHS_PK_DOT: {
      float dx = px - q[1], dy = py - q[2];
      return sqrtf(dx * dx + dy * dy) - q[3];
    }
    case CHS_PK_BOX: {                           // {jenis, x0, y0, x1, y1, radius}
      float rad = q[5];
      float cx = (q[1] + q[3]) * 0.5f, cy = (q[2] + q[4]) * 0.5f;
      float hx = (q[3] - q[1]) * 0.5f - rad, hy = (q[4] - q[2]) * 0.5f - rad;
      float qx = fabsf(px - cx) - hx, qy = fabsf(py - cy) - hy;
      float mx = qx > 0.0f ? qx : 0.0f, my = qy > 0.0f ? qy : 0.0f;
      float m = qx > qy ? qx : qy; if (m > 0.0f) m = 0.0f;
      return sqrtf(mx * mx + my * my) + m - rad;
    }
    case CHS_PK_LINE:
      return chsSdSeg(px, py, q[1], q[2], q[3], q[4]) - q[5];
    default: {                                   // segitiga
      float x0 = q[1], y0 = q[2], x1 = q[3], y1 = q[4], x2 = q[5], y2 = q[6];
      float d0 = chsSdSeg(px, py, x0, y0, x1, y1), d1 = chsSdSeg(px, py, x1, y1, x2, y2), d2 = chsSdSeg(px, py, x2, y2, x0, y0);
      float d = d0 < d1 ? d0 : d1; if (d2 < d) d = d2;
      float c0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
      float c1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
      float c2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
      bool in = (c0 >= 0 && c1 >= 0 && c2 >= 0) || (c0 <= 0 && c1 <= 0 && c2 <= 0);
      return in ? -d : d;
    }
  }
}

static bool chsBuildMasks(int sq) {
  if (chsMask && chsMaskSq == sq) return true;
  chsFreeMasks();
  size_t per = (size_t)sq * sq;
  chsMask = (uint8_t*)heap_caps_malloc(per * 18, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!chsMask) return false;
  const float OW = 0.45f;                         // tebal garis tepi (satuan kotak 10)
  const float* q = CHS_PRIM;
  for (int t = 0; t < 6; t++) {
    const float* e = q;
    while ((int)e[0] != CHS_PK_END) e += 7;
    uint8_t* fm = chsMask + (size_t)t * 3 * per;
    uint8_t* om = fm + per;
    uint8_t* dm = om + per;
    for (int y = 0; y < sq; y++) for (int x = 0; x < sq; x++) {
      int cf = 0, co = 0, cd = 0;
      for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
        float u = ((float)x + ((float)sx + 0.5f) / 4.0f) * 10.0f / (float)sq;
        float v = ((float)y + ((float)sy + 0.5f) / 4.0f) * 10.0f / (float)sq - 0.5f;   // geser bidak 0.5 turun: puncak tidak terpotong
        float df = 1e9f, dd = 1e9f;
        for (const float* r = q; r < e; r += 7) {
          float d = chsSdPrim(r, u, v);
          if ((int)r[0] >= CHS_PK_DOT) { if (d < dd) dd = d; } else if (d < df) df = d;
        }
        if (df <= 0.0f) cf++;
        if (df <= OW) co++;
        if (dd <= 0.0f && df <= 0.0f) cd++;
      }
      size_t i = (size_t)y * sq + x;
      fm[i] = (uint8_t)cf; om[i] = (uint8_t)co; dm[i] = (uint8_t)cd;
    }
    q = e + 7;
    yield();
  }
  chsMaskSq = sq;
  return true;
}

// Gambar satu bidak (t = 1..6) di (ox,oy) berukuran sq. amul 0..255 = kepekatan (utk memudar).
static void chsBlitPiece(LGFX_Sprite& s, int t, bool white, int ox, int oy, int sq, uint8_t amul) {
  if (t < 1 || t > 6) return;
  if (!chsBuildMasks(sq)) { chsDrawPieceVec(s, t, white, ox, oy, sq); return; }
  size_t per = (size_t)sq * sq;
  const uint8_t* fm = chsMask + (size_t)(t - 1) * 3 * per;
  const uint8_t* om = fm + per;
  const uint8_t* dm = om + per;
  const uint16_t fcA = white ? CHS_RGB(255, 255, 255) : CHS_RGB(78, 78, 90);     // sisi terang (kiri-atas)
  const uint16_t fcB = white ? CHS_RGB(206, 206, 218) : CHS_RGB(20, 20, 26);     // sisi gelap (kanan-bawah)
  const uint16_t oc  = white ? CHS_RGB(44, 40, 48)    : CHS_RGB(228, 228, 236);
  const int W = s.width(), H = s.height();
  const int gd = 2 * sq - 2 > 0 ? 2 * sq - 2 : 1;
  for (int y = 0; y < sq; y++) {
    int py = oy + y;
    if (py < 0 || py >= H) continue;
    for (int x = 0; x < sq; x++) {
      int px = ox + x;
      if (px < 0 || px >= W) continue;
      size_t i = (size_t)y * sq + x;
      uint8_t fa = fm[i], ao = om[i], da = dm[i];
      uint8_t sa = (y > 0) ? om[i - sq] : 0;                          // bayangan = tepi bergeser 1 px ke bawah
      if (sa < 5) sa = 0;                                             // buang bintik bayangan yg terlalu tipis
      if (!fa && !ao && !sa) continue;
      uint16_t col = (uint16_t)s.readPixel(px, py);
      if (!ao && sa) col = blend565(col, 0x0000, (uint8_t)(((uint32_t)sa * 80u * amul) / (16u * 255u)));
      if (ao) col = blend565(col, oc, (uint8_t)(((uint32_t)ao * amul) / 16u));
      if (fa) {
        uint16_t fc = blend565(fcA, fcB, (uint8_t)(((x + y) * 255) / gd));
        col = blend565(col, fc, (uint8_t)(((uint32_t)fa * amul) / 16u));
      }
      if (da) col = blend565(col, oc, (uint8_t)(((uint32_t)da * amul) / 16u));
      s.drawPixel(px, py, col);
    }
  }
}

static void chsDrawPiece(LGFX_Sprite& s, int t, bool white, int ox, int oy, int sq) {
  chsBlitPiece(s, t, white, ox, oy, sq, 255);
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

static void chsSqIJ(int q, int* i, int* j) {   // petak papan -> baris/kolom layar (kebalikan chsSqAt)
  bool flip = (chsRole == 2);
  int rank = q >> 3, file = q & 7;
  *i = flip ? rank : 7 - rank;
  *j = flip ? 7 - file : file;
}

// teks hasil singkat utk pil status di panel
static const char* chsEndShort() {
  int r = chsResultForMe();
  if (r == 1) return "Kamu menang!";
  if (r == -1) return "Kamu kalah";
  if (r == 0) return "Seri";
  return "Selesai";
}

static void chsChip(LGFX_Sprite& s, int cx, int cy, bool white, bool active) {   // penanda warna pemain
  nyxAaCircle(s, cx, cy, 6, active ? (uint16_t)T().accent : blend565(T().surface, 0xFFFF, 70));
  nyxAaCircle(s, cx, cy, 4, white ? CHS_RGB(252, 252, 252) : CHS_RGB(34, 34, 40));
}

// bidak yg sekarang ada di petak `to` digambar meluncur dari `from` (ae = 0..1 sudah di-ease)
static void chsDrawSlide(LGFX_Sprite& s, int bx, int by, int sq, int from, int to, float ae) {
  int8_t mp = chsBd[to];
  if (!mp) return;
  int fi, fj, ti, tj;
  chsSqIJ(from, &fi, &fj); chsSqIJ(to, &ti, &tj);
  int mx = bx + fj * sq + (int)floorf((float)((tj - fj) * sq) * ae + 0.5f);
  int my = by + fi * sq + (int)floorf((float)((ti - fi) * sq) * ae + 0.5f);
  chsBlitPiece(s, mp > 0 ? mp : -mp, mp > 0, mx, my, sq, 255);
}

// sudut papan membulat anti-alias: piksel di luar lengkung dicampur ke warna bingkai
static void chsRoundCorners(LGFX_Sprite& s, int bx, int by, int n, int r, uint16_t fc) {
  for (int k = 0; k < 4; k++) {
    bool left = (k & 1) == 0, top = (k & 2) == 0;
    for (int dy = 0; dy < r; dy++) for (int dx = 0; dx < r; dx++) {
      float ex = (float)r - ((float)dx + 0.5f), ey = (float)r - ((float)dy + 0.5f);
      float cov = (float)r - sqrtf(ex * ex + ey * ey) + 0.5f;
      if (cov >= 1.0f) continue;
      if (cov < 0.0f) cov = 0.0f;
      int x = left ? bx + dx : bx + n - 1 - dx, y = top ? by + dy : by + n - 1 - dy;
      uint16_t c = (uint16_t)s.readPixel(x, y);
      s.drawPixel(x, y, blend565(c, fc, (uint8_t)((1.0f - cov) * 255.0f)));
    }
  }
}

// selisih materi (putih - hitam) dari isi papan: P1 N3 B3 R5 Q9
static int chsMaterialDiff() {
  static const int8_t val[7] = {0, 1, 3, 3, 5, 9, 0};
  int d = 0;
  for (int i = 0; i < 64; i++) { int8_t p = chsBd[i]; if (p > 0) d += val[p]; else if (p < 0) d -= val[-p]; }
  return d;
}

static void chsDrawSetup(LGFX_Sprite& s) {
  bool typing = kbVisible && kbTarget == &chsRoomStr;
  s.setTextColor(T().accent); s.setTextSize(2); s.setCursor(10, 25); s.print("Catur");
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(10, 46); s.print("Online: kode room sama. Atau lawan AI");
  if (SCR_W >= SCR_H) {                                             // hiasan: raja + kuda di pojok kanan atas
    int bx, by, sq, px, py, pw, ph; chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);
    chsBlitPiece(s, 6, true,  SCR_W - 2 * sq - 12, 21, sq, 255);
    chsBlitPiece(s, 2, false, SCR_W - sq - 8,      21, sq, 255);
  }
  iosRR(s, 8, 62, SCR_W - 16, 34, 8, T().surface);
  if (typing) nyxAaRRRing(s, 8, 62, SCR_W - 16, 34, 10, T().accent);
  s.setTextColor(T().subtext); s.setCursor(16, 67); s.print("Kode room");
  String shown = chsRoomStr; if (typing) shown += "_";
  s.setTextSize(2); s.setTextColor(chsRoomStr.length() || typing ? T().text : T().subtext);
  s.setCursor(16, 78); s.print(shown.length() ? shown.c_str() : "ketuk utk isi");
  s.setTextSize(1);
  String nm = chatName.length() ? chatName : String("HP");
  s.setTextColor(T().subtext); s.setCursor(10, 102); s.print("Pemain: "); s.setTextColor(T().text); s.print(nm.c_str());
  s.setCursor(10, 116);
  if (chsBrokerOk()) { s.setTextColor(T().good); String hh; int pt; chsBrokerHostPort(hh, pt); char hb[40]; chsClip(hh.c_str(), hb, (SCR_W - 130) / 6); s.print("Broker: "); s.print(hb); }
  else { s.setTextColor(T().danger); s.print("Broker belum diatur"); }
  nyxAaRR(s, SCR_W - 8 - 50, 110, 50, 18, 8, chsPressCol(5, blend565(T().surface2, 0xFFFF, 20)));
  s.setTextColor(T().accent); chsPrintC(s, SCR_W - 8 - 50, 50, 115, "Atur");
  iosRR(s, 8, 134, 70, 26, 7, chsPressCol(3, T().surface2));
  s.setTextColor(T().accent); chsPrintC(s, 8, 70, 143, "Acak");
  bool ok = chsBrokerOk() && chsRoomStr.length() >= 3;
  iosRR(s, 86, 134, SCR_W - 94, 26, 7, chsPressCol(4, ok ? (uint16_t)T().accent : (uint16_t)T().surface2));
  s.setTextColor(ok ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, 86, SCR_W - 94, 143, "Mulai");
  {                                                               // baris AI: Lawan AI | level | AFK
    int gw = (SCR_W - 16 - 12) / 3, x0 = 8, x1 = 8 + gw + 6, x2 = 8 + 2 * (gw + 6), by2 = 166;
    iosRR(s, x0, by2, gw, 24, 7, chsPressCol(6, (uint16_t)T().accent));
    s.setTextColor(T().bg); chsPrintC(s, x0, gw, by2 + 8, "Lawan AI");
    iosRR(s, x1, by2, gw, 24, 7, chsPressCol(7, (uint16_t)T().surface2));
    s.setTextColor(T().accent); chsPrintC(s, x1, gw, by2 + 8, chsLvName[chsAiLevel]);
    iosRR(s, x2, by2, gw, 24, 7, chsPressCol(8, (uint16_t)T().surface2));
    char ab[14]; if (chsAfkSec[chsAfkIdx]) snprintf(ab, sizeof(ab), "AFK %us", (unsigned)chsAfkSec[chsAfkIdx]); else strcpy(ab, "AFK mati");
    s.setTextColor(chsAfkIdx ? (uint16_t)T().accent : (uint16_t)T().subtext); chsPrintC(s, x2, gw, by2 + 8, ab);
    char cap[48], cb[48];
    if (chsTrainN && chsAccN >= 5) snprintf(cap, sizeof(cap), "AI belajar: %u langkah, tebak %d%%", (unsigned)chsTrainN, (int)(chsAcc * 100.0f + 0.5f));
    else if (chsTrainN) snprintf(cap, sizeof(cap), "AI belajar gayamu: %u langkah. AFK = AI ganti kamu", (unsigned)chsTrainN);
    else strcpy(cap, "AI belajar gayamu saat kamu main. AFK = AI ganti kamu");
    chsClip(cap, cb, (SCR_W - 16 - 66) / 6);
    s.setTextColor(T().subtext); s.setCursor(10, by2 + 30); s.print(cb);
    nyxAaRR(s, SCR_W - 8 - 58, by2 + 27, 58, 16, 7, chsPressCol(9, blend565(T().surface2, 0xFFFF, 20)));   // buka layar Jaringan
    s.setTextColor(T().accent); chsPrintC(s, SCR_W - 8 - 58, 58, by2 + 31, "Jaringan");
  }
}

// teks + warna status giliran (dipakai pil di panel dan chip saat panel disembunyikan)
static void chsStatusText(char* line, size_t n, uint16_t* sc) {
  const char* t; uint16_t c = T().accent;
  static char eb[48];
  if (chsVsAI) {
    if (chsEnd)                       { t = chsEndShort(); int rr = chsResultForMe(); c = rr == 1 ? T().good : rr == -1 ? T().danger : T().text; }
    else if (chsCanMove())            { t = chsInCheck() ? "SKAK! Jalanmu" : "Giliranmu"; c = chsInCheck() ? T().danger : T().good; }
    else                              { t = "AI berpikir..."; c = T().subtext; }
  }
  else if (chsConn == 2)              { chsClip(chsErr[0] ? chsErr : "Terputus", eb, 46); t = eb; c = T().danger; }
  else if (chsConn == 0 || !chsReady) { t = "Menyambung..."; c = T().subtext; }
  else if (chsRole == 0)              { t = "Cari kursi..."; c = T().subtext; }
  else if (chsRole == 3)              { t = "Room penuh!"; c = T().danger; }
  else if (!chsOppPid()[0])           { t = "Tunggu lawan..."; c = T().subtext; }
  else if (chsEnd)                    { t = chsEndShort(); int rr = chsResultForMe(); c = rr == 1 ? T().good : rr == -1 ? T().danger : T().text; }
  else if (chsCanMove() && chsAfkAI)  { t = "AI bermain utk kamu"; c = T().accent; }
  else if (chsCanMove())              { t = chsInCheck() ? "SKAK! Jalanmu" : "Giliranmu"; c = chsInCheck() ? T().danger : T().good; }
  else                                { t = "Giliran lawan"; c = T().subtext; }
  strncpy(line, t, n - 1); line[n - 1] = 0;
  *sc = c;
}

// bidak yg sudah dimakan: byMe=true -> bidak lawan yg hilang dari papan (huruf: Q R B N P)
static void chsCapturedStr(bool byMe, char* out, size_t n) {
  static const uint8_t start[6] = {8, 2, 2, 2, 1, 0};     // P N B R Q (indeks 0..4)
  static const char    let[5]   = {'P', 'N', 'B', 'R', 'Q'};
  static const int8_t  idx[5]   = {1, 2, 3, 4, 5};        // kode bidak di papan
  bool meW = (chsRole != 2);
  bool capW = byMe ? !meW : meW;                          // warna bidak yg dihitung hilang
  int cnt[5] = {0, 0, 0, 0, 0};
  for (int i = 0; i < 64; i++) {
    int8_t p = chsBd[i];
    if (!p || ((p > 0) != capW)) continue;
    int t = p > 0 ? p : -p;
    if (t >= 1 && t <= 5) cnt[t - 1]++;
  }
  size_t k = 0;
  for (int t = 4; t >= 0 && k + 2 < n; t--) {              // Q dulu, P terakhir
    int miss = (int)start[t] - cnt[t];
    for (int m = 0; m < miss && k + 2 < n; m++) out[k++] = let[t];
  }
  out[k] = 0;
  (void)idx;
}

static void chsDrawHandle(LGFX_Sprite& s) {
  int hx, hy, hw, hh; chsHandleRect(&hx, &hy, &hw, &hh);
  bool off = chsPanelOff();
  nyxAaRR(s, hx, hy, hw, hh, 7, chsPressCol(2, blend565(T().surface2, 0xFFFF, 24)));
  int cx = hx + hw / 2, cy = hy + hh / 2;
  uint16_t ac = T().accent;
  int dir;                                                // 0 kanan, 1 kiri, 2 bawah, 3 atas
  if (SCR_W >= SCR_H) dir = off ? 1 : 0; else dir = off ? 3 : 2;
  if (dir == 0)      { s.drawLine(cx - 2, cy - 4, cx + 2, cy, ac); s.drawLine(cx + 2, cy, cx - 2, cy + 4, ac); s.drawLine(cx - 3, cy - 4, cx + 1, cy, ac); s.drawLine(cx + 1, cy, cx - 3, cy + 4, ac); }
  else if (dir == 1) { s.drawLine(cx + 2, cy - 4, cx - 2, cy, ac); s.drawLine(cx - 2, cy, cx + 2, cy + 4, ac); s.drawLine(cx + 3, cy - 4, cx - 1, cy, ac); s.drawLine(cx - 1, cy, cx + 3, cy + 4, ac); }
  else if (dir == 2) { s.drawLine(cx - 4, cy - 2, cx, cy + 2, ac); s.drawLine(cx, cy + 2, cx + 4, cy - 2, ac); s.drawLine(cx - 4, cy - 3, cx, cy + 1, ac); s.drawLine(cx, cy + 1, cx + 4, cy - 3, ac); }
  else               { s.drawLine(cx - 4, cy + 2, cx, cy - 2, ac); s.drawLine(cx, cy - 2, cx + 4, cy + 2, ac); s.drawLine(cx - 4, cy + 3, cx, cy - 1, ac); s.drawLine(cx, cy - 1, cx + 4, cy + 3, ac); }
}

static void chsDrawGame(LGFX_Sprite& s) {
  int bx, by, sq, px, py, pw, ph;
  chsGeom(&bx, &by, &sq, &px, &py, &pw, &ph);
  int kingSq = -1;
  if (chsEnd == 0 || chsEnd == 1) { if (chsInCheck()) kingSq = chsKingSq(chsWhite); }
  const uint16_t hiLast = 0xFFE0;

  // ---- animasi langkah ----
  bool anim = chsAnimOn;
  float at = 1.0f;
  if (anim) { at = (float)(millis() - chsAnimMs) / (float)CHS_ANIM_MS; if (at > 1.0f) at = 1.0f; }
  float ae = 1.0f - (1.0f - at) * (1.0f - at) * (1.0f - at);        // ease-out cubic

  // ---- bingkai papan + bayangan ----
  nyxAaRR(s, bx - 2, by - 1, 8 * sq + 4, 8 * sq + 6, 7, blend565(T().bg, 0x0000, 130));
  nyxAaRR(s, bx - 3, by - 3, 8 * sq + 6, 8 * sq + 6, 6, blend565(T().surface, 0xFFFF, 46));

  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
    int q = chsSqAt(i, j);
    int rank = q >> 3, file = q & 7;
    int x = bx + j * sq, y = by + i * sq;
    uint16_t c = ((rank + file) & 1) ? CHS_LIGHT : CHS_DARK;
    if (chsLast >= 0 && (q == (chsLast & 63) || q == ((chsLast >> 6) & 63))) c = blend565(c, hiLast, 105);
    if (q == chsSel) c = blend565(c, T().accent, 150);
    s.fillRect(x, y, sq, sq, c);
    if (q == kingSq) {                                               // cahaya merah lembut yg berdenyut di bawah raja skak
      float pu = (chsEnd == 0) ? 0.5f + 0.5f * sinf((float)millis() / 170.0f) : 1.0f;
      nyxAaCircle(s, x + sq / 2, y + sq / 2, sq / 2 - 1, blend565(c, T().danger, (uint8_t)(70.0f + 70.0f * pu)));
      nyxAaCircle(s, x + sq / 2, y + sq / 2, sq / 3,     blend565(c, T().danger, (uint8_t)(150.0f + 60.0f * pu)));
    }
    if (q == chsSel) { s.drawRect(x, y, sq, sq, T().accent); s.drawRect(x + 1, y + 1, sq - 2, sq - 2, blend565(c, T().accent, 160)); }
    if (sq >= 18) {                                                  // koordinat mini di tepi papan (kontras halus)
      s.setTextSize(1);
      s.setTextColor(blend565(c, (rank + file) & 1 ? CHS_DARK : CHS_LIGHT, sq >= 26 ? 255 : 200));
      char cl[2]; cl[1] = 0;
      if (i == 7) { cl[0] = (char)('a' + file); s.setCursor(x + sq - 7, y + sq - 9); s.print(cl); }
      if (j == 0) { cl[0] = (char)('1' + rank); s.setCursor(x + 2, y + 2); s.print(cl); }
    }
    int8_t p = chsBd[q];
    if (p && !(anim && (q == chsAnimTo || q == chsAnimRT))) {
      int oy = (q == chsSel) ? y - 2 : y;                            // bidak terpilih "terangkat" 2 px
      chsBlitPiece(s, p > 0 ? p : -p, p > 0, x, oy, sq, 255);
    }
    if (chsSel >= 0 && chsHL[q]) {
      float fi2 = (float)(millis() - chsSelMs) / 180.0f; if (fi2 > 1.0f) fi2 = 1.0f;
      fi2 = 1.0f - (1.0f - fi2) * (1.0f - fi2);                      // ease-out
      uint16_t dc = blend565(c, T().good, (uint8_t)(215.0f * fi2));
      if (p) { nyxAaCircleRing(s, x + sq / 2, y + sq / 2, sq / 2 - 2, dc); nyxAaCircleRing(s, x + sq / 2, y + sq / 2, sq / 2 - 3, dc); }
      else { int rr2 = (int)((float)(sq / 6 + 1) * fi2 + 0.5f); if (rr2 < 1) rr2 = 1; nyxAaCircle(s, x + sq / 2, y + sq / 2, rr2, dc); }
    }
  }

  chsRoundCorners(s, bx, by, 8 * sq, 5, blend565(T().surface, 0xFFFF, 46));

  if (anim) {                                                        // lapisan animasi: yg dimakan memudar, bidak yg jalan meluncur
    if (chsAnimCap) {
      int ti, tj; chsSqIJ(chsAnimTo, &ti, &tj);
      float fa = 1.0f - at / 0.6f;
      if (fa > 0.0f) chsBlitPiece(s, chsAnimCap > 0 ? chsAnimCap : -chsAnimCap, chsAnimCap > 0, bx + tj * sq, by + ti * sq, sq, (uint8_t)(255.0f * fa));
    }
    if (chsAnimRT >= 0) chsDrawSlide(s, bx, by, sq, chsAnimRF, chsAnimRT, ae);
    chsDrawSlide(s, bx, by, sq, chsAnimFrom, chsAnimTo, ae);
  }

  if (chsEnd) {                                                      // banner hasil di tengah papan
    char eb[40]; chsClip(chsEndText(), eb, (8 * sq - 24) / 6);
    int tw = (int)strlen(eb) * 6 + 22, bh2 = 22;
    int xx = bx + (8 * sq - tw) / 2, yy = by + 4 * sq - bh2 / 2;
    int rr = chsResultForMe();
    uint16_t ec = rr == 1 ? (uint16_t)T().good : rr == -1 ? (uint16_t)T().danger : (uint16_t)T().text;
    nyxAaRR(s, xx, yy + 2, tw, bh2, 11, blend565(T().bg, 0x0000, 160));
    nyxAaRR(s, xx, yy, tw, bh2, 11, blend565(T().surface, 0x0000, 70));
    nyxAaRRRing(s, xx, yy, tw, bh2, 11, ec);
    s.setTextSize(1); s.setTextColor(ec); s.setCursor(xx + 11, yy + 7); s.print(eb);
  }

  if (chsPromoOn) {                                                  // pilih bidak promosi (popup membesar dari tengah)
    float pe = (float)(millis() - chsPromoMs) / 150.0f; if (pe > 1.0f) pe = 1.0f;
    pe = 1.0f - (1.0f - pe) * (1.0f - pe);
    int fw = 4 * sq + 10, fh = sq + 10, cx0 = bx + 4 * sq, cy0 = by + 4 * sq;
    int w = (int)((float)fw * (0.55f + 0.45f * pe)), h = (int)((float)fh * (0.55f + 0.45f * pe));
    int x0 = cx0 - w / 2, y0 = cy0 - h / 2;
    nyxAaRR(s, x0, y0 + 2, w, h, 8, blend565(T().bg, 0x0000, 150));
    iosRR(s, x0, y0, w, h, 8, T().surface);
    nyxAaRRRing(s, x0, y0, w, h, 8, T().accent);
    if (pe >= 1.0f) {
      bool wh = (chsRole == 1);
      static const int pr[4] = {5, 4, 3, 2};
      for (int k = 0; k < 4; k++) {
        s.fillRect(x0 + 5 + k * sq, y0 + 5, sq, sq, ((k & 1) ? CHS_DARK : CHS_LIGHT));
        chsDrawPiece(s, pr[k], wh, x0 + 5 + k * sq, y0 + 5, sq);
      }
    }
  }

  // ---- panel info (bisa disembunyikan) ----
  char line[48], clipd[48];
  s.setTextSize(1);
  uint16_t sc = T().accent;
  chsStatusText(line, sizeof(line), &sc);

  if (chsPanelOff()) {                                                // mode ringkas: chip status di bawah papan + gagang
    int hx, hy, hw, hh; chsHandleRect(&hx, &hy, &hw, &hh);
    int cw = hx - 6 - bx;
    nyxAaRR(s, bx + 4, hy, cw, hh, 7, blend565(T().surface, sc, 60));
    chsClip(line, clipd, (cw - 14) / 6);
    s.setTextColor(sc); s.setCursor(bx + 12, hy + 4); s.print(clipd);
    chsDrawHandle(s);
    return;
  }

  drawGlassCard(s, px, py, pw, ph, 10, T().surface, 225);
  int gap = (ph >= 120) ? 15 : 11;
  int maxCh = (pw - 14) / 6;
  chsClip(line, clipd, (pw - 10 - 22 - 8) / 6);
  nyxAaRR(s, px + 5, py + 5, pw - 10 - 22, 15, 7, blend565(T().surface, sc, 60));   // pil status
  s.setTextColor(sc); s.setCursor(px + 9, py + 9); s.print(clipd);
  chsDrawHandle(s);
  int ty = py + 5 + 15 + 6;
  bool playing = chsEnd == 0 && chsOppPid()[0] != 0;
  // kamu
  bool meW = (chsRole != 2);
  const char* myN = chsRole == 2 ? chsBName : chsWName;
  char nb[40]; snprintf(nb, sizeof(nb), "Kamu : %s", myN[0] ? myN : (chatName.length() ? chatName.c_str() : "HP"));
  chsClip(nb, clipd, (pw - 56) / 6);
  chsChip(s, px + 12, ty + 4, meW, playing && (chsWhite == meW));
  s.setTextColor(T().text); s.setCursor(px + 24, ty); s.print(clipd);
  int mat = chsMaterialDiff(); if (!meW) mat = -mat;                  // + = aku unggul materi
  if (mat != 0) {
    char mb[8]; snprintf(mb, sizeof(mb), "%+d", mat);
    s.setTextColor(mat > 0 ? (uint16_t)T().good : (uint16_t)T().danger);
    s.setCursor(px + pw - 12 - (int)strlen(mb) * 6, ty); s.print(mb);
  }
  ty += gap;
  // lawan
  const char* opN = chsRole == 2 ? chsWName : chsBName;
  if (chsVsAI) snprintf(nb, sizeof(nb), "Lawan: %s", opN);
  else snprintf(nb, sizeof(nb), "Lawan: %s", chsOppPid()[0] ? (opN[0] ? opN : "?") : "-");
  chsClip(nb, clipd, (pw - 40) / 6);
  chsChip(s, px + 12, ty + 4, !meW, playing && (chsWhite != meW));
  s.setTextColor(T().text); s.setCursor(px + 24, ty); s.print(clipd);
  if (chsVsAI) nyxAaCircle(s, px + pw - 10, ty + 4, 3, chsAiTurn() ? (uint16_t)T().accent : blend565(T().surface, 0xFFFF, 80));
  else nyxAaCircle(s, px + pw - 10, ty + 4, 3, chsOppOnline() ? (uint16_t)T().good : blend565(T().surface, 0xFFFF, 80));
  ty += gap;
  // room + langkah
  char lt[8]; chsTokAt(chsPly - 1, lt);
  if (chsVsAI) snprintf(nb, sizeof(nb), "Lawan AI  #%u %s", (unsigned)chsPly, lt);
  else snprintf(nb, sizeof(nb), "Room %s  #%u %s", chsRoom, (unsigned)chsPly, lt);
  chsClip(nb, clipd, maxCh);
  s.setTextColor(T().subtext); s.setCursor(px + 9, ty);
  s.print(clipd);
  ty += gap;
  // bidak yg dimakan: +huruf = kamu ambil, -huruf = diambil lawan
  {
    char c1[20], c2[20]; chsCapturedStr(true, c1, sizeof(c1)); chsCapturedStr(false, c2, sizeof(c2));
    int cx = px + 9;
    s.setTextColor(T().good);   s.setCursor(cx, ty); s.print("+"); s.print(c1[0] ? c1 : "-");
    cx += (int)(1 + (c1[0] ? strlen(c1) : 1)) * 6 + 8;
    if (cx + 12 < px + pw - 6) { s.setTextColor(T().danger); s.setCursor(cx, ty); s.print("-"); s.print(c2[0] ? c2 : "-"); }
  }

  // tombol
  int x, y, w, h;
  chsBtnRect(0, &x, &y, &w, &h);
  bool over = chsEnd != 0;
  bool canAct = (chsRole == 1 || chsRole == 2) && chsConn == 1;
  if (over) {
    iosRR(s, x, y, w, h, 6, chsPressCol(0, canAct ? (uint16_t)T().accent : (uint16_t)T().surface2));
    s.setTextColor(canAct ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, x, w, y + 7, "Main lagi");
  } else {
    iosRR(s, x, y, w, h, 6, chsPressCol(0, chsConfirm ? (uint16_t)T().danger : (uint16_t)T().surface2));
    s.setTextColor(chsConfirm ? (uint16_t)T().bg : (uint16_t)T().danger); chsPrintC(s, x, w, y + 7, chsConfirm ? "Yakin?" : "Menyerah");
  }
  chsBtnRect(1, &x, &y, &w, &h);
  iosRR(s, x, y, w, h, 6, chsPressCol(1, T().surface2));
  s.setTextColor(T().accent); chsPrintC(s, x, w, y + 7, "Menu");

  // daftar langkah (kalau ada ruang)
  int ly0 = y + h + 8;
  int rows = (py + ph - 4 - ly0) / 10;
  if (rows >= 2 && pw >= 110) {
    int pairs = (chsPly + 1) / 2;
    int first = pairs - rows; if (first < 0) first = 0;
    for (int k = first; k < pairs; k++) {
      char a[8], b[8]; chsTokAt(2 * k, a); chsTokAt(2 * k + 1, b);
      char ln[28]; snprintf(ln, sizeof(ln), "%2d. %-5s %-5s", k + 1, a, b);
      s.setTextColor(k == pairs - 1 ? (uint16_t)T().text : (uint16_t)T().subtext);   // baris terakhir lebih terang
      s.setCursor(px + 8, ly0 + (k - first) * 10); s.print(ln);
    }
  }
}

// =====================================================================
// LAYAR "JARINGAN" (chsScr == 3): lihat MLP gaya (26-16-1) + tabel kebiasaan petak langsung di HP.
//   Tab: Jaringan (diagram) | Bobot (peta W1 16x26) | Petak (7 papan 8x8). Chip "Selisih" = beda dari bobot awal.
//   Bobot awal (utk selisih) dihitung ulang di PSRAM hanya selama layar ini dibuka.
// =====================================================================
static uint8_t chsVw = 0;                 // 0 diagram, 1 peta bobot, 2 petak
static bool    chsVwDelta = false;        // true = tampilkan selisih dari bobot awal
static int8_t  chsVwSel = -1;             // neuron tersembunyi terpilih (tab 0)
static int16_t chsVwCell = -1;            // sel / petak terpilih (tab 1, 2)
static float*  chsVwInit = nullptr;
static char    chsVwInfo[80] = "";

static const char* const chsFeatName[CHS_NIN] = {
  "makan", "tukar untung", "ke tengah", "dorong pion", "maju", "kembang", "rokade", "menteri", "benteng", "raja jalan",
  "skak", "mendarat diserang", "mundur", "dekati raja", "promosi", "pion sayap", "kuda", "gajah", "selamatkan", "dilindungi",
  "fase", "materi", "diskak", "no.langkah", "mobilitas", "ada menteri" };
static const char* const chsPcName[7] = { "Pion", "Kuda", "Gajah", "Benteng", "Menteri", "Raja", "Asal" };

static void chsVwClose() { if (chsVwInit) { CHS_AI_FREE(chsVwInit); chsVwInit = nullptr; } }
static void chsVwOpen() {
  chsNetEnsure();
  if (!chsVwInit) chsVwInit = (float*)CHS_AI_ALLOC(CHS_NP * sizeof(float));
  if (chsVwInit) chsNetFill(chsVwInit);
  chsVw = 0; chsVwDelta = false; chsVwSel = -1; chsVwCell = -1; chsVwInfo[0] = 0;
  chsScr = 3; needRedraw = true;
}

static inline float chsVwVal(int i) { return (chsVwDelta && chsVwInit) ? chsNet[i] - chsVwInit[i] : chsNet[i]; }
static float chsVwMax(int a, int b) { float m = 1e-6f; for (int i = a; i < b; i++) { float v = fabsf(chsVwVal(i)); if (v > m) m = v; } return m; }
static uint16_t chsVwCol(float v, float m, uint16_t base) {      // biru/aksen = positif, merah = negatif
  float a = sqrtf(fabsf(v) / m); if (a > 1.0f) a = 1.0f;
  return blend565(base, v >= 0 ? (uint16_t)T().accent : (uint16_t)T().danger, (uint8_t)(a * 255.0f));
}

struct ChsVwG { int ax, ay, aw, ah, bx, by, bw, bh, fy; };
static ChsVwG chsg;                       // geometri layar Jaringan (global: tipe struct tidak boleh ada di signature fungsi -- prototipe otomatis Arduino ada di atas definisinya)
static void chsVwGeom() {
  ChsVwG& g = chsg; g.ax = 4; g.ay = STATUS_H + 3; g.aw = SCR_W - 8; g.ah = backY() - 4 - g.ay;
  g.bx = g.ax; g.by = g.ay + 22; g.bw = g.aw; g.fy = g.ay + g.ah - 20; g.bh = g.fy - 2 - g.by;
}
static void chsVwTabRect(int i, int* x, int* y, int* w, int* h) {     // i 0..2 = tab, 3 = chip Selisih
  chsVwGeom(); const ChsVwG& g = chsg; int chip = 54, gap = 4; int tw = (g.aw - chip - 4 * gap) / 3;
  *y = g.ay; *h = 18;
  if (i < 3) { *x = g.ax + i * (tw + gap); *w = tw; } else { *x = g.ax + g.aw - chip; *w = chip; }
}

// --- tab 0: diagram ---
static inline int chsVwYIn(int k)  { return chsg.by + 4 + (chsg.bh - 8) * k / (CHS_NIN - 1); }
static inline int chsVwYHid(int j) { return chsg.by + 4 + (chsg.bh - 8) * j / (CHS_NH - 1); }
static inline int chsVwXi() { return chsg.bx + 62; }
static inline int chsVwXo() { return chsg.bx + chsg.bw - 22; }
static inline int chsVwXh() { return (chsVwXi() + chsVwXo()) / 2; }

static void chsVwNeuronInfo(int j) {
  const int W1n = CHS_NH * CHS_NIN;
  int b1 = -1, b2 = -1; float v1 = 0, v2 = 0;
  for (int k = 0; k < CHS_NIN; k++) {
    float w = chsVwVal(j * CHS_NIN + k);
    if (b1 < 0 || fabsf(w) > fabsf(v1)) { b2 = b1; v2 = v1; b1 = k; v1 = w; }
    else if (b2 < 0 || fabsf(w) > fabsf(v2)) { b2 = k; v2 = w; }
  }
  snprintf(chsVwInfo, sizeof(chsVwInfo), "H%d skor %+.2f | %s %+.2f, %s %+.2f", j + 1, chsVwVal(W1n + CHS_NH + j),
           chsFeatName[b1], v1, chsFeatName[b2], v2);
}

static void chsVwDrawNet(LGFX_Sprite& s) {
  const ChsVwG& g = chsg;
  const int W1n = CHS_NH * CHS_NIN;
  float m = chsVwMax(0, W1n), mv = chsVwMax(W1n + CHS_NH, CHS_NP - 1);
  int xi = chsVwXi(), xh = chsVwXh(), xo = chsVwXo(), yo = g.by + g.bh / 2;
  uint16_t bg = T().bg, pc = T().accent, nc = T().danger;
  for (int j = 0; j < CHS_NH; j++) {                                   // garis masukan -> tersembunyi
    if (chsVwSel >= 0 && chsVwSel != j) continue;
    int yh = chsVwYHid(j);
    for (int k = 0; k < CHS_NIN; k++) {
      float w = chsVwVal(j * CHS_NIN + k), r = fabsf(w) / m; if (r > 1.0f) r = 1.0f;
      if (chsVwSel < 0 && r < 0.35f) continue;                         // diagram penuh: hanya yang kuat
      s.drawLine(xi, chsVwYIn(k), xh, yh, blend565(bg, w >= 0 ? pc : nc, (uint8_t)(70 + 185 * r)));
    }
  }
  for (int j = 0; j < CHS_NH; j++) {                                   // garis tersembunyi -> skor
    float v = chsVwVal(W1n + CHS_NH + j), r = fabsf(v) / mv; if (r > 1.0f) r = 1.0f;
    bool on = chsVwSel < 0 || chsVwSel == j; int yh = chsVwYHid(j);
    uint16_t c = blend565(bg, v >= 0 ? pc : nc, on ? (uint8_t)(70 + 185 * r) : (uint8_t)40);
    s.drawLine(xh, yh, xo, yo, c); if (on && r > 0.6f) s.drawLine(xh, yh + 1, xo, yo + 1, c);
  }
  s.setTextSize(1);
  bool lab = (g.bh - 8) / (CHS_NIN - 1) >= 8;                          // label hanya kalau cukup lega
  for (int k = 0; k < CHS_NIN; k++) {
    int y = chsVwYIn(k);
    s.fillCircle(xi, y, 2, k < CHS_NF ? (uint16_t)T().subtext : (uint16_t)T().accent2);
    if (!lab && (k & 1)) continue;
    char b[12]; chsClip(chsFeatName[k], b, 9);
    bool hot = chsVwSel >= 0 && fabsf(chsVwVal(chsVwSel * CHS_NIN + k)) / m > 0.5f;
    s.setTextColor(hot ? (uint16_t)T().text : (uint16_t)T().subtext);
    s.setCursor(xi - 6 - (int)strlen(b) * 6, y - 3); s.print(b);
  }
  for (int j = 0; j < CHS_NH; j++) {
    int y = chsVwYHid(j);
    s.fillCircle(xh, y, 6, chsVwCol(chsVwVal(W1n + CHS_NH + j), mv, T().surface2));
    s.drawCircle(xh, y, 6, chsVwSel == j ? (uint16_t)T().text : (uint16_t)T().divider);
  }
  s.fillCircle(xo, yo, 8, T().accent);
  s.setTextColor(T().subtext); chsPrintC(s, xo - 12, 24, yo + 11, "skor");
}

// --- tab 1: peta bobot W1 (16 neuron x 26 fitur) ---
static void chsVwHmGeom(int* x0, int* y0, int* cs) {
  const ChsVwG& g = chsg;
  int c1 = (g.bw - 24) / CHS_NIN, c2 = (g.bh - 10) / CHS_NH; int c = c1 < c2 ? c1 : c2; if (c < 4) c = 4;
  *cs = c; *x0 = g.bx + 22; *y0 = g.by + 10;
}
static void chsVwDrawHm(LGFX_Sprite& s) {
  const ChsVwG& g = chsg;
  int x0, y0, cs; chsVwHmGeom(&x0, &y0, &cs);
  float m = chsVwMax(0, CHS_NH * CHS_NIN);
  s.setTextSize(1); s.setTextColor(T().subtext);
  for (int k = 0; k < CHS_NIN; k++) {
    if (cs < 13 && k != 0 && (k + 1) % 5 != 0) continue;
    char b[4]; snprintf(b, sizeof(b), "%d", k + 1); chsPrintC(s, x0 + k * cs - 2, cs + 4, g.by + 1, b);
  }
  for (int j = 0; j < CHS_NH; j++) {
    char b[5]; snprintf(b, sizeof(b), "H%d", j + 1);
    s.setTextColor(T().subtext); s.setCursor(g.bx + 2, y0 + j * cs + (cs - 8) / 2); s.print(b);
    for (int k = 0; k < CHS_NIN; k++) s.fillRect(x0 + k * cs, y0 + j * cs, cs - 1, cs - 1, chsVwCol(chsVwVal(j * CHS_NIN + k), m, T().surface));
  }
  if (chsVwCell >= 0) { int j = chsVwCell / CHS_NIN, k = chsVwCell % CHS_NIN; s.drawRect(x0 + k * cs - 1, y0 + j * cs - 1, cs + 1, cs + 1, T().text); }
}

// --- tab 2: tabel kebiasaan petak (6 bidak tujuan + petak asal), Putih di bawah ---
static void chsVwBdGeom(int i, int* x, int* y, int* cs) {
  const ChsVwG& g = chsg;
  int cols = (g.bw >= g.bh) ? 4 : 2, rows = (7 + cols - 1) / cols, gap = 8, lab = 10;
  int c1 = (g.bw - (cols - 1) * gap) / (cols * 8), c2 = (g.bh - rows * (lab + 2)) / (rows * 8);
  int c = c1 < c2 ? c1 : c2; if (c < 3) c = 3;
  int ox = g.bx + (g.bw - (cols * 8 * c + (cols - 1) * gap)) / 2;
  *cs = c; *x = ox + (i % cols) * (8 * c + gap); *y = g.by + (i / cols) * (8 * c + lab + 2) + lab;
}
static void chsVwDrawBd(LGFX_Sprite& s) {
  const ChsVwG& g = chsg;
  float m = 1e-6f; for (int i = 0; i < CHS_HAB_N; i++) { float v = fabsf(chsHab[i]); if (v > m) m = v; }
  s.setTextSize(1);
  for (int p = 0; p < 7; p++) {
    int x, y, c; chsVwBdGeom(p, &x, &y, &c);
    s.setTextColor(T().subtext); chsPrintC(s, x, 8 * c, y - 10, chsPcName[p]);
    for (int r = 0; r < 8; r++) for (int f = 0; f < 8; f++) {
      uint16_t base = (((r + f) & 1) == 0) ? (uint16_t)T().surface2 : (uint16_t)T().surface;
      s.fillRect(x + f * c, y + (7 - r) * c, c, c, chsVwCol(chsHab[p * 64 + r * 8 + f], m, base));
    }
    if (chsVwCell >= 0 && chsVwCell / 64 == p) { int q = chsVwCell % 64, r = q / 8, f = q % 8; s.drawRect(x + f * c, y + (7 - r) * c, c, c, T().text); }
  }
}

static void chsDrawNet(LGFX_Sprite& s) {
  chsVwGeom(); const ChsVwG& g = chsg;
  static const char* const tn[3] = { "Jaringan", "Bobot", "Petak" };
  s.setTextSize(1);
  for (int i = 0; i < 3; i++) {
    int x, y, w, h; chsVwTabRect(i, &x, &y, &w, &h); bool on = (chsVw == i);
    iosRR(s, x, y, w, h, 7, on ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(on ? (uint16_t)T().bg : (uint16_t)T().text); chsPrintC(s, x, w, y + 5, tn[i]);
  }
  { int x, y, w, h; chsVwTabRect(3, &x, &y, &w, &h);
    iosRR(s, x, y, w, h, 7, chsVwDelta ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(chsVwDelta ? (uint16_t)T().bg : (chsVwInit ? (uint16_t)T().text : (uint16_t)T().subtext)); chsPrintC(s, x, w, y + 5, "Selisih"); }
  if (chsVw == 0) chsVwDrawNet(s); else if (chsVw == 1) chsVwDrawHm(s); else chsVwDrawBd(s);
  char b[96], ln[96];
  if (chsVwInfo[0]) strncpy(b, chsVwInfo, sizeof(b) - 1), b[sizeof(b) - 1] = 0;
  else strcpy(b, chsVw == 0 ? "Ketuk neuron (lingkaran tengah) utk detail" : chsVw == 1 ? "Ketuk sel utk nilainya" : "Ketuk petak utk nilainya");
  chsClip(b, ln, g.aw / 6);
  s.setTextColor(T().text); s.setCursor(g.ax + 2, g.fy); s.print(ln);
  if (chsTrainN && chsAccN >= 5) snprintf(b, sizeof(b), "%u langkah | tebak %d%% | %s", (unsigned)chsTrainN, (int)(chsAcc * 100.0f + 0.5f), chsVwDelta ? "selisih dari awal" : "bobot");
  else snprintf(b, sizeof(b), "%u langkah | %s", (unsigned)chsTrainN, chsVwDelta ? "selisih dari awal" : "bobot");
  chsClip(b, ln, g.aw / 6);
  s.setTextColor(T().subtext); s.setCursor(g.ax + 2, g.fy + 10); s.print(ln);
}

static void chsTouchNet(int x, int y, bool isNew) {
  if (!isNew) return;
  if (isBack(x, y)) { chsVwClose(); chsScr = 0; needRedraw = true; return; }
  for (int i = 0; i < 4; i++) {
    int tx, ty, tw, th; chsVwTabRect(i, &tx, &ty, &tw, &th);
    if (x >= tx && x < tx + tw && y >= ty && y < ty + th + 4) {
      if (i < 3) chsVw = (uint8_t)i;
      else if (chsVwInit) chsVwDelta = !chsVwDelta;
      else showToast("RAM tidak cukup utk selisih", 1800);
      chsVwSel = -1; chsVwCell = -1; chsVwInfo[0] = 0; needRedraw = true; return;
    }
  }
  chsVwGeom(); const ChsVwG& g = chsg;
  if (chsVw == 0) {
    int j = ((y - (g.by + 4)) * (CHS_NH - 1) + (g.bh - 8) / 2) / (g.bh - 8);
    if (y < g.by || j < 0 || j >= CHS_NH || chsAbsI(x - chsVwXh()) > 14) return;
    if (chsVwSel == j) { chsVwSel = -1; chsVwInfo[0] = 0; } else { chsVwSel = (int8_t)j; chsVwNeuronInfo(j); }
  } else if (chsVw == 1) {
    int x0, y0, cs; chsVwHmGeom(&x0, &y0, &cs);
    int k = (x - x0) / cs, j = (y - y0) / cs;
    if (x < x0 || y < y0 || k >= CHS_NIN || j >= CHS_NH) return;
    chsVwCell = (int16_t)(j * CHS_NIN + k);
    snprintf(chsVwInfo, sizeof(chsVwInfo), "H%d x %s: %+.3f", j + 1, chsFeatName[k], chsVwVal(chsVwCell));
  } else {
    for (int p = 0; p < 7; p++) {
      int bx, by, c; chsVwBdGeom(p, &bx, &by, &c);
      if (x < bx || y < by || x >= bx + 8 * c || y >= by + 8 * c) continue;
      int f = (x - bx) / c, r = 7 - (y - by) / c; chsVwCell = (int16_t)(p * 64 + r * 8 + f);
      snprintf(chsVwInfo, sizeof(chsVwInfo), "%s %c%d: %+.2f", p < 6 ? chsPcName[p] : "Asal", 'a' + f, r + 1, chsHab[chsVwCell]);
      needRedraw = true; return;
    }
    return;
  }
  needRedraw = true;
}

static void chsDrawBroker(LGFX_Sprite& s);

void drawChess(LGFX_Sprite& s) {
  if (chsMoves && chsScr != 1) iosBackdrop(s); else s.fillSprite(T().bg);
  drawStatusBar(s);
  if (!chsMoves) {
    s.setTextColor(T().danger); s.setTextSize(1); s.setCursor(10, 40); s.print("RAM tidak cukup untuk app Catur");
  } else if (chsScr == 0) chsDrawSetup(s);
  else if (chsScr == 2) chsDrawBroker(s);
  else if (chsScr == 3) chsDrawNet(s);
  else chsDrawGame(s);
  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// SENTUHAN
// ---------------------------------------------------------------------
static const char* chsBLabel(int i) {
  static const char* L[3] = { "Host broker (opsional :port, bawaan 8883 TLS)", "Username MQTT", "Password MQTT" };
  return L[i];
}
static String* chsBField(int i) { return i == 0 ? &chsBHost : (i == 1 ? &chsBUser : &chsBPass); }

static void chsDrawBroker(LGFX_Sprite& s) {
  s.setTextColor(T().accent); s.setTextSize(2); s.setCursor(10, 25); s.print("Broker Catur");
  s.setTextSize(1);
  char buf[64];
  int maxC = (SCR_W - 32) / 6;
  if (chsBEdit >= 0 && kbVisible) {                                  // mengetik: satu field saja, di atas keyboard
    chsClip(chsBLabel(chsBEdit), buf, (SCR_W - 20) / 6);
    s.setTextColor(T().subtext); s.setCursor(10, 48); s.print(buf);
    iosRR(s, 8, 60, SCR_W - 16, 24, 7, T().surface);
    nyxAaRRRing(s, 8, 60, SCR_W - 16, 24, 9, T().accent);
    String v = *chsBField(chsBEdit);
    if ((int)v.length() > maxC - 1) v = String("..") + v.substring(v.length() - (maxC - 3));
    v += "_";
    s.setTextColor(T().text); s.setCursor(16, 68); s.print(v.c_str());
    s.setTextColor(T().subtext); s.setCursor(10, 92); s.print("Ketuk area di atas keyboard = selesai");
    return;
  }
  s.setTextColor(T().subtext); s.setCursor(10, 46); s.print("Terpisah dari Chat - hanya dipakai Catur");
  for (int i = 0; i < 3; i++) {
    int y = 60 + i * 34;
    iosRR(s, 8, y, SCR_W - 16, 30, 8, T().surface);
    chsClip(chsBLabel(i), buf, (SCR_W - 32) / 6);
    s.setTextColor(T().subtext); s.setCursor(16, y + 5); s.print(buf);
    String v;
    if (i == 2) { for (size_t k = 0; k < chsBPass.length(); k++) v += '*'; } else v = *chsBField(i);
    if ((int)v.length() > maxC) v = String("..") + v.substring(v.length() - (maxC - 2));
    s.setTextColor(v.length() ? T().text : T().subtext);
    s.setCursor(16, y + 17); s.print(v.length() ? v.c_str() : "ketuk untuk mengisi...");
  }
  int bw = (SCR_W - 16 - 12) / 3, by = 166;
  bool sdOk = sdReady && SD_MMC.exists("/chess.cfg");
  bool chatOk = chatConfigured();
  iosRR(s, 8, by, bw, 26, 7, chsPressCol(6, T().surface2));
  s.setTextColor(sdOk ? (uint16_t)T().accent : (uint16_t)T().subtext); chsPrintC(s, 8, bw, by + 9, "Dari SD");
  iosRR(s, 8 + bw + 6, by, bw, 26, 7, chsPressCol(7, T().surface2));
  s.setTextColor(chatOk ? (uint16_t)T().accent : (uint16_t)T().subtext); chsPrintC(s, 8 + bw + 6, bw, by + 9, "Salin Chat");
  iosRR(s, 8 + 2 * (bw + 6), by, bw, 26, 7, chsPressCol(8, chsBrokerOk() ? (uint16_t)T().accent : (uint16_t)T().surface2));
  s.setTextColor(chsBrokerOk() ? (uint16_t)T().bg : (uint16_t)T().subtext); chsPrintC(s, 8 + 2 * (bw + 6), bw, by + 9, "Simpan");
  s.setCursor(10, by + 34);
  if (chsBrokerOk()) { s.setTextColor(T().good); s.print("Siap dipakai"); }
  else { s.setTextColor(T().danger); s.print("Lengkapi host, username & password"); }
}

static void chsTouchBroker(int x, int y, bool isNew) {
  if (!isNew) return;
  if (!kbVisible && chsBEdit >= 0) { chsBrokerSave(); chsBEdit = -1; needRedraw = true; }      // keyboard ditutup lewat Enter
  if (kbVisible) {
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; chsBrokerSave(); chsBEdit = -1; }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }
  if (isBack(x, y)) { chsBrokerSave(); chsScr = 0; needRedraw = true; return; }
  for (int i = 0; i < 3; i++) {
    int fy = 60 + i * 34;
    if (y >= fy && y < fy + 30) { chsBEdit = (int8_t)i; kbTarget = chsBField(i); kbVisible = true; kbMode = KB_LOWER; needRedraw = true; return; }
  }
  int bw = (SCR_W - 16 - 12) / 3, by = 166;
  if (y >= by && y <= by + 26) {
    int k = (x - 8) / (bw + 6);
    if (k == 0) {
      chsPress(6);
      if (chsBrokerFromSd()) { chsBrokerSave(); showToast("Dimuat dari /chess.cfg", 1800); }
      else showToast("/chess.cfg tidak ada / tidak valid (host= user= pass=)", 2800);
    } else if (k == 1) {
      chsPress(7);
      if (chatConfigured()) { chsBHost = chatHost; chsBUser = chatUser; chsBPass = chatPass; chsBrokerSave(); showToast("Disalin dari Chat (tetap terpisah)", 2200); }
      else showToast("Broker Chat belum diatur", 2000);
    } else {
      chsPress(8);
      if (!chsBrokerOk()) showToast("Isi host, username & password dulu", 2200);
      else { chsBrokerSave(); chsScr = 0; showToast("Broker Catur disimpan", 1600); }
    }
    needRedraw = true;
  }
}

static void chsTouchSetup(int x, int y, bool isNew) {
  if (!isNew) return;
  if (kbVisible) {
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; chsSanitizeRoom(); }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }
  if (isBack(x, y)) { navBack(); return; }
  if (y >= 106 && y <= 130) { chsPress(5); chsScr = 2; chsBEdit = -1; needRedraw = true; return; }      // baris broker -> layar Broker
  if (y >= 62 && y <= 96) { kbTarget = &chsRoomStr; kbVisible = true; kbMode = KB_LOWER; needRedraw = true; return; }
  if (y >= 134 && y <= 160) {
    if (x >= 8 && x <= 78) { chsPress(3); chsRandomRoom(); needRedraw = true; }
    else if (x >= 86) { chsPress(4); chsStartGame(); }
    return;
  }
  if (y >= 192 && y <= 210 && x >= SCR_W - 8 - 62) { chsPress(9); chsVwOpen(); return; }          // chip Jaringan
  if (y >= 166 && y <= 190) {
    int gw = (SCR_W - 16 - 12) / 3;
    if (x >= 8 && x < 8 + gw) { chsPress(6); chsStartAi(); }
    else if (x >= 8 + gw + 6 && x < 8 + 2 * gw + 6) { chsPress(7); chsAiLevel = (uint8_t)((chsAiLevel + 1) & 3); chsPrefsSaveCfg(); needRedraw = true; }
    else if (x >= 8 + 2 * (gw + 6)) { chsPress(8); chsAfkIdx = (uint8_t)((chsAfkIdx + 1) & 3); chsPrefsSaveCfg(); needRedraw = true; }
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
    if (promoCnt > 1) { chsPromoOn = true; chsPromoMs = millis(); chsPromoFrom = chsSel; chsPromoTo = q; needRedraw = true; return; }
    chsCommit(pick);
    return;
  }
  if (mine) {
    if (chsSel != q) chsSelMs = millis();
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
  chsLastTouchMs = millis();                         // user kembali -> AI pengganti berhenti
  if (chsAfkAI) { chsAfkAI = false; needRedraw = true; }
  if (chsScr == 2) { chsTouchBroker(x, y, isNew); return; }
  if (chsScr == 3) { chsTouchNet(x, y, isNew); return; }
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

  if (chsInHandle(x, y)) {                                           // gagang: sembunyikan / tampilkan panel
    chsPress(2);
    chsPanelHidden = !chsPanelHidden;
    { Preferences pf; if (pf.begin("chess", false)) { pf.putBool("hide", chsPanelHidden); pf.end(); } }
    chsSel = -1; chsConfirm = false; chsPromoOn = false;
    needRedraw = true;
    return;
  }

  int bxr, byr, bw, bh;
  bool panelOn = !chsPanelOff();
  chsBtnRect(0, &bxr, &byr, &bw, &bh);
  if (panelOn && x >= bxr && x < bxr + bw && y >= byr && y < byr + bh) {
    chsPress(0);
    if (chsEnd != 0) { if ((chsRole == 1 || chsRole == 2) && chsConn == 1) chsRematch(); }
    else if (chsRole == 1 || chsRole == 2) {
      if (chsConfirm) chsResign();
      else { chsConfirm = true; chsConfirmMs = millis(); needRedraw = true; }
    }
    return;
  }
  chsBtnRect(1, &bxr, &byr, &bw, &bh);
  if (panelOn && x >= bxr && x < bxr + bw && y >= byr && y < byr + bh) {
    chsPress(1);
    chsActive = false; chsScr = 0; chsConn = 0; chsReady = false; chsSel = -1; chsConfirm = false;
    chsVsAI = false; chsAfkAI = false; chsAiQueued = false;
    chsStyleSave();
    needRedraw = true;
    return;
  }

  if (x >= bx && x < bx + 8 * sq && y >= by && y < by + 8 * sq) {
    int j = (x - bx) / sq, i = (y - by) / sq;
    chsTapBoard(chsSqAt(i, j));
  }
}
