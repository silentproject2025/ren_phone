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

// NYX-SMP-V1 + NYX-SMP-BENCH2: core ke-2 (Lazy SMP) + NPS - lihat chess_smp.cpp
// ==== ENGINE BEGIN ====
#if defined(__GNUC__) && !defined(CHS_NO_OPT_PRAGMA)
// NYX-SPEED: mesin dikompilasi -O2 (sisa sketch -Os) -- inlining + pemindahan/penjadwalan instruksi; -DCHS_NO_OPT_PRAGMA = kembali ke -Os (A/B lewat tab NPS)
#pragma GCC push_options
#pragma GCC optimize ("O2")
#endif
// Mesin aturan catur murni (tanpa Arduino/LovyanGFX) -- diuji perft di PC.
// Petak: sq = rank*8 + file (a1 = 0, h1 = 7, a8 = 56). Bidak: +1..+6 putih, -1..-6 hitam
// (1=Pion 2=Kuda 3=Gajah 4=Benteng 5=Menteri 6=Raja). Langkah (16 bit): dari | ke<<6 | promosi<<12.
#define CHS_WK 1
#define CHS_WQ 2
#define CHS_BK 4
#define CHS_BQ 8

alignas(8) static int8_t chsBd[64];   // selaras 8 byte: salin/hash papan jadi beberapa instruksi kata, bukan memcpy byte-demi-byte
static bool     chsWhite = true;      // true = giliran putih
static uint8_t  chsCastle = 15;       // hak rokade (CHS_*)
static int8_t   chsEp = -1;           // petak target en-passant (-1 = tidak ada)
static uint16_t chsHalf = 0;          // penghitung 50 langkah (setengah-langkah)

// ---- NYX-SPEED: tabel geometri papan (pengganti cek batas rr/cc di tiap langkah sinar) ----
static const int8_t  chsDirOff[8] = {8, -8, 1, -1, 9, 7, -7, -9};          // selisih indeks petak per arah (urutan sama dgn 'dirs' lama)
static const uint8_t chsRayN[64][8] = {   // jumlah langkah ke tepi papan per arah
  {7,0,7,0,7,0,0,0}, {7,0,6,1,6,1,0,0}, {7,0,5,2,5,2,0,0}, {7,0,4,3,4,3,0,0},
  {7,0,3,4,3,4,0,0}, {7,0,2,5,2,5,0,0}, {7,0,1,6,1,6,0,0}, {7,0,0,7,0,7,0,0},
  {6,1,7,0,6,0,1,0}, {6,1,6,1,6,1,1,1}, {6,1,5,2,5,2,1,1}, {6,1,4,3,4,3,1,1},
  {6,1,3,4,3,4,1,1}, {6,1,2,5,2,5,1,1}, {6,1,1,6,1,6,1,1}, {6,1,0,7,0,6,0,1},
  {5,2,7,0,5,0,2,0}, {5,2,6,1,5,1,2,1}, {5,2,5,2,5,2,2,2}, {5,2,4,3,4,3,2,2},
  {5,2,3,4,3,4,2,2}, {5,2,2,5,2,5,2,2}, {5,2,1,6,1,5,1,2}, {5,2,0,7,0,5,0,2},
  {4,3,7,0,4,0,3,0}, {4,3,6,1,4,1,3,1}, {4,3,5,2,4,2,3,2}, {4,3,4,3,4,3,3,3},
  {4,3,3,4,3,4,3,3}, {4,3,2,5,2,4,2,3}, {4,3,1,6,1,4,1,3}, {4,3,0,7,0,4,0,3},
  {3,4,7,0,3,0,4,0}, {3,4,6,1,3,1,4,1}, {3,4,5,2,3,2,4,2}, {3,4,4,3,3,3,4,3},
  {3,4,3,4,3,3,3,4}, {3,4,2,5,2,3,2,4}, {3,4,1,6,1,3,1,4}, {3,4,0,7,0,3,0,4},
  {2,5,7,0,2,0,5,0}, {2,5,6,1,2,1,5,1}, {2,5,5,2,2,2,5,2}, {2,5,4,3,2,2,4,3},
  {2,5,3,4,2,2,3,4}, {2,5,2,5,2,2,2,5}, {2,5,1,6,1,2,1,5}, {2,5,0,7,0,2,0,5},
  {1,6,7,0,1,0,6,0}, {1,6,6,1,1,1,6,1}, {1,6,5,2,1,1,5,2}, {1,6,4,3,1,1,4,3},
  {1,6,3,4,1,1,3,4}, {1,6,2,5,1,1,2,5}, {1,6,1,6,1,1,1,6}, {1,6,0,7,0,1,0,6},
  {0,7,7,0,0,0,7,0}, {0,7,6,1,0,0,6,1}, {0,7,5,2,0,0,5,2}, {0,7,4,3,0,0,4,3},
  {0,7,3,4,0,0,3,4}, {0,7,2,5,0,0,2,5}, {0,7,1,6,0,0,1,6}, {0,7,0,7,0,0,0,7},
};
static const uint8_t chsKnN[64] = {2,3,4,4,4,4,3,2,3,4,6,6,6,6,4,3,4,6,8,8,8,8,6,4,4,6,8,8,8,8,6,4,4,6,8,8,8,8,6,4,4,6,8,8,8,8,6,4,3,4,6,6,6,6,4,3,2,3,4,4,4,4,3,2};
static const uint8_t chsKnT[64][8] = {   // petak tujuan kuda (urutan sama dgn 'kn' lama)
  {10,17,0,0,0,0,0,0}, {11,18,16,0,0,0,0,0}, {12,19,17,8,0,0,0,0}, {13,20,18,9,0,0,0,0},
  {14,21,19,10,0,0,0,0}, {15,22,20,11,0,0,0,0}, {23,21,12,0,0,0,0,0}, {22,13,0,0,0,0,0,0},
  {18,25,2,0,0,0,0,0}, {19,26,24,3,0,0,0,0}, {20,27,25,16,0,4,0,0}, {21,28,26,17,1,5,0,0},
  {22,29,27,18,2,6,0,0}, {23,30,28,19,3,7,0,0}, {31,29,20,4,0,0,0,0}, {30,21,5,0,0,0,0,0},
  {26,33,1,10,0,0,0,0}, {27,34,32,0,2,11,0,0}, {28,35,33,24,8,1,3,12}, {29,36,34,25,9,2,4,13},
  {30,37,35,26,10,3,5,14}, {31,38,36,27,11,4,6,15}, {39,37,28,12,5,7,0,0}, {38,29,13,6,0,0,0,0},
  {34,41,9,18,0,0,0,0}, {35,42,40,8,10,19,0,0}, {36,43,41,32,16,9,11,20}, {37,44,42,33,17,10,12,21},
  {38,45,43,34,18,11,13,22}, {39,46,44,35,19,12,14,23}, {47,45,36,20,13,15,0,0}, {46,37,21,14,0,0,0,0},
  {42,49,17,26,0,0,0,0}, {43,50,48,16,18,27,0,0}, {44,51,49,40,24,17,19,28}, {45,52,50,41,25,18,20,29},
  {46,53,51,42,26,19,21,30}, {47,54,52,43,27,20,22,31}, {55,53,44,28,21,23,0,0}, {54,45,29,22,0,0,0,0},
  {50,57,25,34,0,0,0,0}, {51,58,56,24,26,35,0,0}, {52,59,57,48,32,25,27,36}, {53,60,58,49,33,26,28,37},
  {54,61,59,50,34,27,29,38}, {55,62,60,51,35,28,30,39}, {63,61,52,36,29,31,0,0}, {62,53,37,30,0,0,0,0},
  {58,33,42,0,0,0,0,0}, {59,32,34,43,0,0,0,0}, {60,56,40,33,35,44,0,0}, {61,57,41,34,36,45,0,0},
  {62,58,42,35,37,46,0,0}, {63,59,43,36,38,47,0,0}, {60,44,37,39,0,0,0,0}, {61,45,38,0,0,0,0,0},
  {41,50,0,0,0,0,0,0}, {40,42,51,0,0,0,0,0}, {48,41,43,52,0,0,0,0}, {49,42,44,53,0,0,0,0},
  {50,43,45,54,0,0,0,0}, {51,44,46,55,0,0,0,0}, {52,45,47,0,0,0,0,0}, {53,46,0,0,0,0,0,0},
};

static void chsReset() {
  static const int8_t back[8] = {4, 2, 3, 5, 6, 3, 2, 4};
  for (int i = 0; i < 64; i++) chsBd[i] = 0;
  for (int f = 0; f < 8; f++) { chsBd[f] = back[f]; chsBd[8 + f] = 1; chsBd[48 + f] = -1; chsBd[56 + f] = -back[f]; }
  chsWhite = true; chsCastle = 15; chsEp = -1; chsHalf = 0;
}

// apakah petak sq diserang oleh sisi byWhite?
static bool chsAtt(int sq, bool byWhite) {
  const int8_t sg = byWhite ? 1 : -1;
  const int c = sq & 7;
  if (byWhite) {                                                   // pion putih menyerang dari petak di bawahnya
    if (sq >= 8) { if (c > 0 && chsBd[sq - 9] == sg) return true; if (c < 7 && chsBd[sq - 7] == sg) return true; }
  } else {
    if (sq < 56) { if (c > 0 && chsBd[sq + 7] == sg) return true; if (c < 7 && chsBd[sq + 9] == sg) return true; }
  }
  const uint8_t* kt = chsKnT[sq]; const int kc = chsKnN[sq]; const int8_t kk = (int8_t)(sg * 2);
  for (int i = 0; i < kc; i++) if (chsBd[kt[i]] == kk) return true;
  const uint8_t* rn = chsRayN[sq];
  for (int d = 0; d < 8; d++) {
    const int n = rn[d]; if (!n) continue;
    const int off = chsDirOff[d]; int s = sq;
    for (int k = 1; k <= n; k++) {
      s += off;
      int8_t p = chsBd[s];
      if (p) {
        if (k == 1 && p == sg * 6) return true;
        if ((p > 0) == byWhite) {
          int t = p > 0 ? p : -p;
          if (d < 4 ? (t == 4 || t == 5) : (t == 3 || t == 5)) return true;
        }
        break;
      }
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
      if (t == 2) {
        const uint8_t* kt = chsKnT[sq]; const int kc = chsKnN[sq];
        for (int i = 0; i < kc; i++) {
          int to = kt[i]; int8_t q = chsBd[to];
          if (q && ((q > 0) == w)) continue;
          out[n++] = (uint16_t)(sq | (to << 6));
        }
      } else {
        const uint8_t* rn = chsRayN[sq];
        for (int i = 0; i < 8; i++) {
          if (!rn[i]) continue;
          int to = sq + chsDirOff[i]; int8_t q = chsBd[to];
          if (q && ((q > 0) == w)) continue;
          out[n++] = (uint16_t)(sq | (to << 6));
        }
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
      const uint8_t* rn = chsRayN[sq];
      for (int d = d0; d < d1; d++) {
        const int nn = rn[d], off = chsDirOff[d]; int to = sq;
        for (int k = 0; k < nn; k++) {
          to += off;
          int8_t q = chsBd[to];
          if (q && ((q > 0) == w)) break;
          out[n++] = (uint16_t)(sq | (to << 6));
          if (q) break;
        }
      }
    }
  }
  return n;
}

// NYX-SPEED: pembangkit langkah TANGKAPAN / PROMOSI / en-passant saja (utk quiescence, node terbanyak). Urutan relatif sama persis dgn chsGen,
// jadi hasil pencarian identik; bedanya langkah diam tak pernah dibangkitkan dan tak perlu disaring lagi.
static int chsGenCaps(uint16_t* out) {
  int n = 0;
  const bool w = chsWhite;
  for (int sq = 0; sq < 64; sq++) {
    int8_t p = chsBd[sq];
    if (!p || ((p > 0) != w)) continue;
    int t = p > 0 ? p : -p;
    if (t == 1) {
      int c = sq & 7, r = sq >> 3;
      int dr = w ? 1 : -1, r1 = r + dr, promoR = w ? 7 : 0;
      if (r1 < 0 || r1 > 7) continue;
      if (r1 == promoR && chsBd[r1 * 8 + c] == 0) n = chsAddPawn(out, n, sq, r1 * 8 + c, true);
      for (int dc = -1; dc <= 1; dc += 2) {
        int cc = c + dc;
        if (cc < 0 || cc > 7) continue;
        int to = r1 * 8 + cc;
        int8_t q = chsBd[to];
        if (q && ((q > 0) != w)) n = chsAddPawn(out, n, sq, to, r1 == promoR);
        else if (!q && to == chsEp) out[n++] = (uint16_t)(sq | (to << 6));
      }
    } else if (t == 2) {
      const uint8_t* kt = chsKnT[sq]; const int kc = chsKnN[sq];
      for (int i = 0; i < kc; i++) {
        int to = kt[i]; int8_t q = chsBd[to];
        if (q && ((q > 0) != w)) out[n++] = (uint16_t)(sq | (to << 6));
      }
    } else if (t == 6) {
      const uint8_t* rn = chsRayN[sq];
      for (int i = 0; i < 8; i++) {
        if (!rn[i]) continue;
        int to = sq + chsDirOff[i]; int8_t q = chsBd[to];
        if (q && ((q > 0) != w)) out[n++] = (uint16_t)(sq | (to << 6));
      }
    } else {
      int d0 = (t == 3) ? 4 : 0, d1 = (t == 4) ? 4 : 8;
      const uint8_t* rn = chsRayN[sq];
      for (int d = d0; d < d1; d++) {
        const int nn = rn[d], off = chsDirOff[d]; int to = sq;
        for (int k = 0; k < nn; k++) {
          to += off;
          int8_t q = chsBd[to];
          if (!q) continue;
          if ((q > 0) != w) out[n++] = (uint16_t)(sq | (to << 6));
          break;
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

static int chsKingSq(bool white) {                      // pindai 4 petak per kata (papan selaras 8 byte); hasil sama dgn pindai byte-demi-byte
  const uint32_t pat = (uint32_t)(uint8_t)(white ? 6 : -6) * 0x01010101u;
  for (int i = 0; i < 16; i++) {
    uint32_t w; memcpy(&w, chsBd + i * 4, 4);
    uint32_t x = w ^ pat;
    if ((x - 0x01010101u) & ~x & 0x80808080u) {
      for (int j = 0; j < 4; j++) if (chsBd[i * 4 + j] == (white ? 6 : -6)) return i * 4 + j;
    }
  }
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

// NYX-SPEED2: uji legalitas sesudah langkah (dipanggil SETELAH chsDo). Bila raja sebelumnya tidak sedang skak & langkah bukan langkah raja / e.p.,
// raja hanya bisa terancam lewat garis yg dibuka petak asal -> cukup pindai SATU sinar dari raja, bukan chsAtt penuh. Hasil sama persis dgn uji lama.
static bool chsLegalAfter(uint16_t m, bool special, int ks0, bool wasChk, bool mover) {
  if (special || wasChk || ks0 < 0) { int ks = chsKingSq(mover); return ks >= 0 && !chsAtt(ks, !mover); }
  int from = m & 63;
  int dr = (from >> 3) - (ks0 >> 3), dc = (from & 7) - (ks0 & 7);
  int ar = dr < 0 ? -dr : dr, ac = dc < 0 ? -dc : dc;
  if (dr && dc && ar != ac) return true;                          // petak asal tak segaris dgn raja
  int sr = dr > 0 ? 1 : (dr < 0 ? -1 : 0), sc = dc > 0 ? 1 : (dc < 0 ? -1 : 0);
  bool diag = dr && dc;
  int r = (ks0 >> 3) + sr, c = (ks0 & 7) + sc;
  while (r >= 0 && r < 8 && c >= 0 && c < 8) {
    int8_t p = chsBd[r * 8 + c];
    if (p) {
      if ((p > 0) != mover) { int t = p > 0 ? p : -p; if (diag ? (t == 3 || t == 5) : (t == 4 || t == 5)) return false; }
      return true;
    }
    r += sr; c += sc;
  }
  return true;
}
// langkah khusus (raja / en-passant): wajib uji penuh. Dihitung SEBELUM chsDo.
static inline bool chsMvSpecial(uint16_t m, int ks0) {
  int from = m & 63, to = (m >> 6) & 63; int8_t p = chsBd[from];
  return from == ks0 || ((p == 1 || p == -1) && to == chsEp && (from & 7) != (to & 7));
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
// ---- Adu mesin vs Gemini: bahan prompt (FEN, daftar langkah sah) + pembaca balasan. Murni (tanpa Arduino), diuji di PC. ----
// FEN posisi sekarang. ply = jumlah setengah-langkah yg sudah dimainkan (utk nomor langkah penuh). Butuh cap >= 100.
static int chsFen(char* o, int cap, int ply) {
  static const char L[] = " pnbrqk";
  int k = 0;
  if (cap < 100) { if (cap > 0) o[0] = 0; return 0; }
  for (int r = 7; r >= 0; r--) {
    int e = 0;
    for (int f = 0; f < 8; f++) {
      int8_t p = chsBd[r * 8 + f];
      if (!p) { e++; continue; }
      if (e) { o[k++] = (char)('0' + e); e = 0; }
      int t = p > 0 ? p : -p;
      char c = L[t > 6 ? 0 : t];
      o[k++] = (p > 0) ? (char)(c - 32) : c;
    }
    if (e) o[k++] = (char)('0' + e);
    if (r) o[k++] = '/';
  }
  o[k++] = ' '; o[k++] = chsWhite ? 'w' : 'b'; o[k++] = ' ';
  if (!chsCastle) o[k++] = '-';
  else {
    if (chsCastle & CHS_WK) o[k++] = 'K';
    if (chsCastle & CHS_WQ) o[k++] = 'Q';
    if (chsCastle & CHS_BK) o[k++] = 'k';
    if (chsCastle & CHS_BQ) o[k++] = 'q';
  }
  o[k++] = ' ';
  if (chsEp < 0) o[k++] = '-'; else { o[k++] = (char)('a' + (chsEp & 7)); o[k++] = (char)('1' + (chsEp >> 3)); }
  k += snprintf(o + k, (size_t)(cap - k), " %u %d", (unsigned)chsHalf, ply / 2 + 1);
  return k;
}

// semua langkah sah posisi sekarang: "e2e4 d2d4 ..." (UCI). Return jumlah langkah yg muat.
static int chsLegalList(char* o, int cap) {
  uint16_t mv[256]; int n = chsGen(mv), k = 0, c = 0;
  if (cap < 8) { if (cap > 0) o[0] = 0; return 0; }
  for (int i = 0; i < n; i++) {
    if (!chsLegal(mv[i])) continue;
    char t[8]; chsMoveStr(mv[i], t);
    int L = (int)strlen(t);
    if (k + L + 2 >= cap) break;
    if (k) o[k++] = ' ';
    memcpy(o + k, t, (size_t)L); k += L; c++;
  }
  o[k] = 0;
  return c;
}

// cari langkah sah pertama di teks balasan Gemini. Menerima "e2e4", "**e2e4**", "e7e8q", "e7e8=Q", dan "e7e8" (promosi tanpa huruf -> vezir).
// SAN ("Nf3") sengaja tidak ditebak: prompt sudah meminta UCI & memberi daftar langkah sah.
static bool chsPickFromReply(const char* s, uint16_t* out) {
  int n = (int)strlen(s);
  for (int i = 0; i + 4 <= n; i++) {
    if (i > 0) { char b = s[i - 1]; if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9')) continue; }   // bukan di tengah kata
    char a[7];
    for (int j = 0; j < 4; j++) { char c = s[i + j]; a[j] = (c >= 'A' && c <= 'H') ? (char)(c + 32) : c; }
    if (a[0] < 'a' || a[0] > 'h' || a[1] < '1' || a[1] > '8' || a[2] < 'a' || a[2] > 'h' || a[3] < '1' || a[3] > '8') continue;
    char pc = 0;
    if (i + 4 < n) {
      char c = s[i + 4];
      if (c == '=' && i + 5 < n) c = s[i + 5];
      if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
      if (c == 'q' || c == 'r' || c == 'b' || c == 'n') pc = c;
    }
    if (pc) { a[4] = pc; a[5] = 0; if (chsParse(a, 5, out)) return true; }
    a[4] = 0;
    if (chsParse(a, 4, out)) return true;
    a[4] = 'q'; a[5] = 0;
    if (chsParse(a, 5, out)) return true;
  }
  return false;
}
#if defined(__GNUC__) && !defined(CHS_NO_OPT_PRAGMA)
#pragma GCC pop_options
#endif
// ==== ENGINE END ====

// hook: begitu memori kerja AI siap, pengalaman (replay) terakhir dimuat dari NVS. Definisinya ada di bagian prefs di bawah
// (di luar blok AI supaya blok AI tetap bisa diuji di PC tanpa Arduino).
static void chsRbLoadNvs();
#define CHS_AI_AFTER_ALLOC() chsRbLoadNvs()
// core ke-2 (Lazy SMP, lihat chess_smp.cpp): hook laporan NPS + henti-pembantu. Definisi chsAiReport ada di bagian UI di bawah.
// -DCHS_SMP=0 = satu core persis seperti sebelumnya (chess_smp.cpp tetap ikut di-link, tak dipakai).
#ifndef CHS_SMP
#define CHS_SMP 1
#endif
static void chsAiReport(int d, uint32_t nm, uint32_t nh, uint32_t ms, int cores);
#define CHS_AI_REPORT(d, nm, nh, ms, cores) chsAiReport((d), (nm), (nh), (ms), (cores))
#if CHS_SMP
#define CHS_AI_BEFORE_FREE() chsSmpShutdown()
#endif

// ==== AI BEGIN ====
#if defined(__GNUC__) && !defined(CHS_NO_OPT_PRAGMA)
// NYX-SPEED: mesin dikompilasi -O2 (sisa sketch -Os) -- inlining + pemindahan/penjadwalan instruksi; -DCHS_NO_OPT_PRAGMA = kembali ke -Os (A/B lewat tab NPS)
#pragma GCC push_options
#pragma GCC optimize ("O2")
#endif
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
#define CHS_TT_BITS   17                       // 2^17 entri x 12 byte = 1,5 MB (PSRAM, +768 KB: TT kini dipertahankan antar-langkah utk Nyx); 0 = tanpa tabel transposisi
#endif

// ---- NYX SMP: core ke-2 (Lazy SMP) ----------------------------------------------------------------------------------------------
// Kode ber-tanda CHS_SMP hanya aktif bila CHS_SMP=1 (.ino & chess_smp.cpp menetapkan 1; PC harness lama = 0 -> perilaku persis seperti dulu).
// Tiap core punya SALINAN mesin sendiri: chess_smp.cpp meng-include blok ENGINE+AI ini sekali lagi di translation unit terpisah, jadi semua
// variabel static (papan, killer, history, tumpukan langkah) terpisah. Yang DIBAGI hanya tabel transposisi (TT) + beberapa kata kendali.
// Task pembantu mencari posisi akar yg sama pada kedalaman utama+1 dan mengisi TT; pencarian utama memakai hasilnya (pemangkasan lebih awal).
// Entri TT dibaca/ditulis sbg 3 kata 32 bit dgn kunci = hash ^ kata1 ^ kata2 (XOR lockless): entri "sobek" karena dua core menulis
// bersamaan terdeteksi & dianggap meleset -- tanpa mutex, tanpa menunggu.
#ifndef CHS_SMP
#define CHS_SMP 0
#endif
#ifndef CHS_SMP_HELPER
#define CHS_SMP_HELPER 0
#endif
#ifndef CHS_SMP_MIN_MS
#define CHS_SMP_MIN_MS    400                  // core ke-2 hanya dipakai bila anggaran waktu >= ini (pondering 50/150 ms & level cepat tak perlu)
#endif
#ifndef CHS_SMP_MIN_DEPTH
#define CHS_SMP_MIN_DEPTH 5                    // ... dan kedalaman maksimum >= ini (level berbatas kedalaman rendah selesai cepat tanpa bantuan)
#endif
#ifndef CHS_AI_REPORT
#define CHS_AI_REPORT(d, nm, nh, ms, cores)    // hook laporan NPS (diisi .ino)
#endif
#ifndef CHS_AI_BEFORE_FREE
#define CHS_AI_BEFORE_FREE()                   // hook: dipanggil sebelum memori kerja AI dibebaskan (hentikan task pembantu)
#endif
#ifndef CHS_AI_EXTRA_STOP
#define CHS_AI_EXTRA_STOP() 0                  // hook: true = hentikan pencarian (dipakai task pembantu)
#endif
#if CHS_SMP
bool     chsSmpBegin(const int8_t* bd, int white, int castle, int ep, int half, int age, int trust, uint32_t deadline, int maxDepth, void* tt, uint32_t ttMask);
void     chsSmpEnd();
uint32_t chsSmpNodes();
void     chsSmpShutdown();
extern volatile int chsSmpMainDepthV;
static bool chsSmpOn = true;                   // saklar runtime (disimpan di NVS oleh .ino)
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
static void chsNnFree();
static void chsAiFree() {
  CHS_AI_BEFORE_FREE();
  chsNnFree();
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
alignas(8) static int8_t chsAiSvB[CHS_AI_PLY + 2][64];
static ChsSv  chsAiSvS[CHS_AI_PLY + 2];
#define CHS_SAVE_AT(pl) do { memcpy(chsAiSvB[pl], chsBd, 64); chsAiSvS[pl].w = chsWhite; chsAiSvS[pl].c = chsCastle; chsAiSvS[pl].e = chsEp; chsAiSvS[pl].h = chsHalf; } while (0)
#define CHS_REST_AT(pl) do { memcpy(chsBd, chsAiSvB[pl], 64); chsWhite = chsAiSvS[pl].w; chsCastle = chsAiSvS[pl].c; chsEp = chsAiSvS[pl].e; chsHalf = chsAiSvS[pl].h; } while (0)

// ---------------------------------------------------------------- NYX-NN: evaluasi jaringan saraf (NNUE kecil, int16) + akselerasi SIMD
// Masukan 768 fitur biner [milik-sendiri/lawan 2 x jenis 6 x petak 64], dua perspektif (putih & hitam; perspektif hitam dibalik vertikal).
// Lapisan 1 (768 -> H, int16, skala QA=255) DIPERBARUI SECARA INKREMENTAL per langkah: tiap langkah hanya mengubah 2-4 baris bobot
// (tambah/kurang vektor H int16) -> inilah bagian yg dipercepat SIMD PIE ESP32-S3 (ee.vld.128 / ee.vadds.s16 / ee.vsubs.s16 / ee.vst.128,
// 8 lane int16 per instruksi). Lapisan keluaran: clamp [0,QA] lalu dot int16 (skala QB=64) -> centipawan. Bobot disalin ke PSRAM
// (selaras 16 byte) saat pertama dipakai; tumpukan akumulator per kedalaman juga di PSRAM. Total < 300 KB (batas yg diizinkan: 1 MB).
// Bobot disimpan sbg teks base64 di blok NYX-NN WEIGHTS (dibuat oleh tools/nyx_train/export_net.py) dan didekode ke PSRAM.
// Bila bobot tak ada / PSRAM gagal / uji-mandiri SIMD gagal, otomatis kembali ke jalur skalar atau evaluasi klasik.
#ifndef CHS_NN_NONET
// ==== NYX-NN WEIGHTS BEGIN ====
#define CHS_NN_H 64
#define CHS_NN_HYBRID 1
#define CHS_NN_MUL 25
#define CHS_NN_DIV 4080
// 768 -> 64 x2 -> 1, int16 (QA=255, QB=256), urutan: W[768][H], b[H], Vyg-jalan[H], Vlawan[H], C(int32). Teks base64 ~128 KB
static const char chsNnBlob[] =
  "+////+X/FQDp//X/BgDw//P/9P8HAB0AAQDy/wcA+P8AAA8A9v8AAPX//v8DAPP//P/9//j/8f/u//7//f8cAOH/AQAFABEABgD1/wAABwD8/woA6P8WABMA"
  "/P8IAAEA9f8BAA0A+//7////BgAQAPj/BgADAOj/+//+////BADm/wEA9//u/wcACQD4/wsA+/8BAPL/FADe////CQDm//7///8LABAA+//u/xEAEAD6/wQA"
  "BQAHAAMAEgDq/w0ABQD9/w8A4v8PAAUA8v8GAPz/IQAXAAYA9//5//z/+f/0//j//P/5//f/9P8hAO3/8/8GAPz/BQAKABAACgDz/w4A6f////r/8v8IAP7/"
  "DQDt/wsABgACAO7//f/s/+X/KgAMABcA+/8JAAkACAAAAP3/5v8IAAUADgD9/xYA+v/w/wwA+v/w/wIACgADAAYANAAKABQA5/8FAAoA+//1/wUA/P/4/w8A"
  "EQAEAPr/BwALAAgA7f/8/wUACAAMAAoA8v/3//D/FwACAAcADQD8//H/+//+/xMACwDy/wgACQDy////6P8EAPz/7//3/wMA+f8EAAcA9f/8/xsAAgD1/wEA"
  "7/8OAAUADgAMAAUADAACAPj/8//6/+D/FwDq////7//m/////P/+/wcA+P8FAAcA/P/8/wcAAQACAOL/AQAFAP7/BAAYAAEACAD1//X/+v/z/wAA9P/+/+7/"
  "6//3//L/+P/4//z/DQARAAkA+/8TAOz/5v8DAAAA/v8AAOv/8f8QAA0A9/8ZAPL/+f8EAPH/+v/+//v/CwAIAPL/9/8CAP//9v8FAAoA+P8KAOv/6/8GAPX/"
  "8//9//f/BADt//v/KAABAOj/CAAHABIAAgD4/xIACQDp/woA/f8OAPv/CgACAAcAAAD7/wEACAAFABUABQAIABsAAQAHAPj/AQAVAAQAEwDy/+7/BADv/yIA"
  "EADw//7/+f8FAP//DAATAP3/8f8HAP7/DwD+/wsABAD+//r/CQAgAAsA/v8JAAEA8P8EABQA/P/y/wYAFwAMAA0AHQAVAAkA/f8CAP//CwD1/xQAAgD7/woA"
  "5v/1//j/CgD6/xIA+v8HAPf/DwD9/wcA+v/y//3/DgDz/+3//P8KAPP/+f8CAPz/BAAPAAAA//8KAP//9P/z//L/GAD//wcACQD0/wgA+f/l//3/8v/8/wQA"
  "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"
  "BQD0/wsADQAIAOf/IADv//P/AwDv/wIACwD7/+z/9f8FAPH//v/s//j/FgD1////9f/0/w4A+f8HAPD/8/8BAAAABwAEAO7/BwAFAAEA/f8EAAYA/v8CAPv/"
  "AAD5/wMAEwD3/93/7f8PAPP/DwAOAAgA8P/v//H/9P/+/wsACAANAO7/DgAAAP3/9//2/w4AAgADAPP/CwABAOP//P/7//j/9//3////7f/9/wQADAD3//r/"
  "BAATAAYAEQDu//H/DgDn/wYAFAD9//3/BAD9//T/+v8IAAkABQDw/wcABgD5//7/8v8OAAgA+P///+v/+v8XAAUADgAHAPL/EAAEAA4ADwD8/+L/CwARAA4A"
  "CwDy/+z/AgAHAP3/AwAHAPP/AQD1//v/+P/3/xQA5v/1//v/2//9//7/+v8AAOv//f/a//7/8v/9/w8A+P8YAA4A8f8UAP3/9P8FAAYA9v8HAPj/CQAOAAcA"
  "AQD4/wEA8v8WAAkAEQD2/wMA9f/8//7/9f/q/wsABgAAAAoABQD5/wcADQAOAAMADAD5/w0A+P8GAPj/7//9/yMAAwD+////DAD4/wQAGgDw/wAADQAOAPj/"
  "BgD0//r/8v/s////AgDm/wUA7/8TAA8ABwABAO7/AAD5/+f/EAAJAPn/BwDy//3//v/5/wkABQD//woAKgAZAPX//P8KAAkA+v8QAPz/9P8BAAAABgAHAPv/"
  "9v8RAA0A9P8FAP3/BwD0/xAA/v/0//v/DgD7/+b/FQACAPv/8//6//r/7/8HABIADQDq//r/5//w//b/7f8PAPn////2/wwA/P8KAAgADgAZAPT/BQAKAAQA"
  "9v8DABMABAAQAPv//P8DAO3/DAD+/w8ADAATAAUABwDx//b/CQAeAO//CQDi/wcABQASAPD/AgD5/9///v/2//3/AAD5//v/CQD6/w8AAQALAAUADQAAAPP/"
  "AgAGAPz//f8IAA8A/P/z/xYADAD///7/DQAOAAYAAADz/xIAFADh/xIACwD4/+//BAD+/+3/3f/9/wMAAwD7/wUA6f/0/wsA9f8ZABcA/P/2/xAAEQD0/wEA"
  "/v/3/w4A+f/3//r/DgACAOX/9v8SAAwABwANAA4AGQD///v/9f////v/5//x/+//FgAIAO//CAAHAAQA+f/w/xgA+f8AAOf/4f/+/wUA//8VAPn/9/8SAPD/"
  "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"
  "+P/+/wIACQARAL3/6v8SAAYACAANAOn/vf8HAOD/OgD0/xcA8//1/9P/3P/q//D/+v/n//H/+P/l/wQA3f/7/9b/DAAMABgAEgAIAP//BgABAPn/+f8eAAkA"
  "BAAPAPT/9f8UAB4ACQANAPz/GgDq/woAHACq/xMA7P/4/woA9v8CAPT/CQDz//D/IADg//7/7f/n//7/DwC///n////y/zMA9//7/xAAIQAPAPX/AADy/xEA"
  "0f8LANn/IgDg/x8AEQAXAAcAGADo/+n/2P8IAPr/GAD9//7/DwDr/wgA+P8iAOf/2//+/xUA//8IAO7/9P8UAPX//f/5/+b/5//i/+//AAASAN3/AAAaAN7/"
  "5f8mAP7/KwD2/5b/AwDt/ygA7//9////BgDx/+v/AgDm//D/BAANAPT/6P8YAPb/IgDl//P/HQDz//j///8oAPv/8P8DAPn/GAATAPr/8f/6/xEA5P8FAO3/"
  "6P8iAAsA8P8DAPv/vf8FANb/5/8FANf/GQAVABEA7P/+/wsA7v8JAB8A1v8WABMAof/j//D/+//q/wYACgDU/67/4v/i//3/AAASAPX/0P8VACkAOgD2/4//"
  "1/8cAFcA5f/Z/87/AQD7/6T/3v/6//z//v8PANn/JAARAA4AJAAtAPL/5P8eAPj//f9w//D/6v8kALn/9v/m//j/CAAYAP3/0////wsACQAVAMv/5f+8////"
  "HADE//z/9//b/zkAAADc/ykA9v8GAMf/KgD3/xEABwDY/wAA5P+4//D/GwAPAPf/CACv/wwA///b/wYA4/8IAEMACAD///X/5/9CANv/DwCe/+f//v/3/3z/"
  "HgAUADIA4v/v/7H/OgAmAEIAwf/1/xYAFgAKAPb/FAAYALH/3f8RAIr/HAAXABgA6//c/+f/AgD4/wgA5//l/+n/BgDT/9v/EwDu/8D/2v8pAO7/CwALAPL/"
  "BgD+//H/5/+u/xEAZgCk//b/QAA4ACIA3//c/w8AGgBGAAkAbv/N/5//wv8SAPT/4//m/wsAJwAMALT/xf9IABgA+//n/5X/2/8mAO7/tv+2//X/DwDX/9//"
  "EAD+//j/0v8LAOb/5P/i////CgD9/63/tP/A/9b/FgAAAA0A6P+//xgAl//C/wIAGAABABMAFQAmAA0A1//W/wAAzv8pABIAMgBL/wAAxf8MABsA7//b/ysA"
  "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"
  "3P/r/wIA8f/1/xIA7v8EANL/CADf/yYACADa//H/AQDg/87/9P8JAB8AEgApAAAA9v8IAP//BABHACQA4P/w/wMACgAJAP//EAD6/wMABQAHAPH/FgABAO7/"
  "7P/x/wUAAADm/xMAAgDa////2f8GAPj/8f8SAOT/+f/6/xgA9v/w/xIA+v/9//7/8P/2/+L/uf8VABEA3f8HAAkAGADw//j/4//o/woA+/8DABcAuP8LAOn/"
  "7f/8/4T/5P8KABAABwAKAPn/4/8JABAAGADi/xIA/v8bAAcA7f8LAP//DAD2//L/9//5/+j/+//w/+3/GQDr/wgA9P/n/9n/CAD7/+r/5f/5/wAAEgDs/wAA"
  "+v9H/+b/DwDt/zUA9//0/w0A6P/v//X/6/8AAOv/8f////f/AQDp//v/hv/N//r/9f8ZAO//9//0//r/8/8CAA4A9P8HAAwAAwABAAEABQDt//r/DgDr/wIA"
  "BwAOANL/FAD1//3/2f/w//j/BQAVABUA9P/8//n/BwD4//H//v/9/1f/4f/q//r/8f/1/+7/5f/e/x4ADQARAPz//P/6//j/GAAQABIA+v+V/9v/AwDz//r/"
  "AQD4/9D/EQDl/+j/BAASABIA+v8DAPf/7P/9/wAA+/8XAAUACgD7//X/kP/u/woA6f8PAAoA6f/s/woA5//p/wQA6f8LABgA7f/8/wUACgD4/x0A+f/j/wgA"
  "BQAeAAMA8f///wcABADt/wsA9f///+//GwD0/wEAAgACAP7//v/5/wEA9P8bAP7/FADp//3///8TAO3/CwD0//z/CwD+/w4A/v/r/93/AwD2//D/5v/p//7/"
  "DQDd/xEA9//8/wIA9v/2//v/+f8LAAIABwASAPr/DwAOAAwA///q//b/AwDv//T//f/7//P//v/2/xgAEADn/xoA///+//n/AgAQAAQACgAUABcAGAD2/wQA"
  "AQD7/wIAAAADAO//BQABAP7/9P/d/wcADQD7//7/HwARAOn/CAANAA4ACAD2/wYACQD2/woAGAD3//X/9/8GAAMA5//+//X//v8DAPX/+f8RAPb/AQDz/xUA"
  "7P8GAPv////9/wAABAD2/woABADu/xgACAD8//L//f8XAAkAAwALAO//DwD8//n//P8BAAoA/P/2/wYA9/8IAAIA8v8DAAgAAgAEAPj/DQAKAPL/AwD5//r/"
  "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"
;

// ==== NYX-NN WEIGHTS END ====
#define CHS_NN_AVAIL 1
#else
#define CHS_NN_AVAIL 0
#endif
#if CHS_NN_AVAIL
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CHS_NN_NO_PIE) && ((CHS_NN_H) % 8 == 0)
#define CHS_NN_PIE 1
#else
#define CHS_NN_PIE 0
#endif
#define CHS_NN_QA 255
#ifndef CHS_NN_QUIETONLY
#define CHS_NN_QUIETONLY 0                            // NN hanya menilai posisi sesudah langkah tenang (latihan memang dari posisi tenang)
#endif
#ifndef CHS_NN_NYX
#define CHS_NN_NYX 0                                  // 1 = level Nyx memakai evaluasi NN (EKSPERIMEN: belum terbukti lebih kuat, lihat HASIL.md)
#endif
#ifndef CHS_NN_MUL
#define CHS_NN_MUL 25                                 // cp = s * MUL / DIV  (QB = 64 bawaan)
#define CHS_NN_DIV 1020
#endif
static int16_t* chsNnW = nullptr;                      // [768][H]
static int16_t* chsNnB = nullptr;                      // [H]
static int16_t* chsNnV = nullptr;                      // [2][H]  (yg jalan, lawan)
static int16_t* chsNnAcc = nullptr;                    // [(PLY+2)][2][H]
static int32_t  chsNnC = 0;
static uint8_t* chsNnMem = nullptr;
static bool     chsNnReady = false, chsNnOn = false, chsNnPieOk = true;
static uint8_t  chsNnVal[CHS_AI_PLY + 4];                // akumulator ply ini sah? (dipakai agar quiescence boleh tetap klasik)
static int      chsNnForce = -1;                       // -1 otomatis (hanya mode Nyx), 0 paksa mati, 1 paksa hidup (uji/bench)
#define CHS_NN_ACC(ply) (chsNnAcc + (size_t)(ply) * 2 * CHS_NN_H)

#if CHS_NN_PIE
// satu langkah = 8 lane int16. imm 0 pada vld akumulator = tanpa kenaikan alamat; vst menaikkan 16 byte.
#define CHS_NN_STEP(OP) "ee.vld.128.ip q0, %[r], 16\n" "ee.vld.128.ip q1, %[a], 0\n" OP " q1, q1, q0\n" "ee.vst.128.ip q1, %[a], 16\n"
static inline void chsNnPieAdd(int16_t* a, const int16_t* r) {
#if (CHS_NN_H) % 32 == 0
  for (int i = 0; i < (CHS_NN_H) / 32; i++)
    asm volatile(CHS_NN_STEP("ee.vadds.s16") CHS_NN_STEP("ee.vadds.s16") CHS_NN_STEP("ee.vadds.s16") CHS_NN_STEP("ee.vadds.s16") : [a] "+r"(a), [r] "+r"(r) : : "memory");
#else
  for (int i = 0; i < (CHS_NN_H) / 8; i++) asm volatile(CHS_NN_STEP("ee.vadds.s16") : [a] "+r"(a), [r] "+r"(r) : : "memory");
#endif
}
static inline void chsNnPieSub(int16_t* a, const int16_t* r) {
#if (CHS_NN_H) % 32 == 0
  for (int i = 0; i < (CHS_NN_H) / 32; i++)
    asm volatile(CHS_NN_STEP("ee.vsubs.s16") CHS_NN_STEP("ee.vsubs.s16") CHS_NN_STEP("ee.vsubs.s16") CHS_NN_STEP("ee.vsubs.s16") : [a] "+r"(a), [r] "+r"(r) : : "memory");
#else
  for (int i = 0; i < (CHS_NN_H) / 8; i++) asm volatile(CHS_NN_STEP("ee.vsubs.s16") : [a] "+r"(a), [r] "+r"(r) : : "memory");
#endif
}
#endif
#if !CHS_NN_PIE && defined(__SSE2__) && !defined(CHS_NN_NO_SSE)
#include <emmintrin.h>
#define CHS_NN_SSE 1                                   // uji di PC: SSE2 (8 lane int16 jenuh) meniru perilaku PIE
#endif
static inline void chsNnVAdd(int16_t* a, const int16_t* r) {
#if CHS_NN_PIE
  if (chsNnPieOk) { chsNnPieAdd(a, r); return; }
#elif defined(CHS_NN_SSE)
  for (int i = 0; i < CHS_NN_H; i += 8) _mm_storeu_si128((__m128i*)(a + i), _mm_adds_epi16(_mm_loadu_si128((const __m128i*)(a + i)), _mm_loadu_si128((const __m128i*)(r + i)))); return;
#endif
  for (int i = 0; i < CHS_NN_H; i++) { int v = a[i] + r[i]; a[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
}
static inline void chsNnVSub(int16_t* a, const int16_t* r) {
#if CHS_NN_PIE
  if (chsNnPieOk) { chsNnPieSub(a, r); return; }
#elif defined(CHS_NN_SSE)
  for (int i = 0; i < CHS_NN_H; i += 8) _mm_storeu_si128((__m128i*)(a + i), _mm_subs_epi16(_mm_loadu_si128((const __m128i*)(a + i)), _mm_loadu_si128((const __m128i*)(r + i)))); return;
#endif
  for (int i = 0; i < CHS_NN_H; i++) { int v = a[i] - r[i]; a[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
}
static inline int chsNnFeat(int persp, int8_t p, int sq) {
  int t = (p > 0 ? p : -p) - 1; bool white = p > 0;
  return persp == 0 ? (white ? 0 : 384) + t * 64 + sq : (white ? 384 : 0) + t * 64 + (sq ^ 56);
}
// ubah kedua perspektif akumulator acc2 (2 x H) utk satu bidak di satu petak
static inline void chsNnMod(int16_t* acc2, int8_t p, int sq, bool add) {
  const int16_t* r0 = chsNnW + (size_t)chsNnFeat(0, p, sq) * CHS_NN_H;
  const int16_t* r1 = chsNnW + (size_t)chsNnFeat(1, p, sq) * CHS_NN_H;
  if (add) { chsNnVAdd(acc2, r0); chsNnVAdd(acc2 + CHS_NN_H, r1); } else { chsNnVSub(acc2, r0); chsNnVSub(acc2 + CHS_NN_H, r1); }
}
static void chsNnRefresh(int ply) {                     // hitung penuh dari papan global
  int16_t* a = CHS_NN_ACC(ply);
  memcpy(a, chsNnB, CHS_NN_H * 2); memcpy(a + CHS_NN_H, chsNnB, CHS_NN_H * 2);
  for (int sq = 0; sq < 64; sq++) { int8_t p = chsBd[sq]; if (p) chsNnMod(a, p, sq, true); }
}
// akumulator ply+1 dari ply utk langkah m; HARUS dipanggil sebelum chsDo(m) (papan masih sebelum langkah)
static bool chsNnPush(int ply, uint16_t m) {   // true bila langkah tenang (bukan tangkapan/promosi)
  int16_t* d = CHS_NN_ACC(ply + 1);
  memcpy(d, CHS_NN_ACC(ply), (size_t)CHS_NN_H * 4);
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  int8_t p = chsBd[from], q = chsBd[to]; int t = p > 0 ? p : -p; bool w = p > 0;
  chsNnMod(d, p, from, false);
  if (q) chsNnMod(d, q, to, false);
  else if (t == 1 && to == chsEp && (from & 7) != (to & 7)) chsNnMod(d, (int8_t)-p, w ? to - 8 : to + 8, false);   // en passant
  chsNnMod(d, promo ? (int8_t)(w ? promo : -promo) : p, to, true);
  if (t == 6 && (to - from == 2 || from - to == 2)) {                                                                  // rokade: benteng ikut pindah
    int8_t rk = (int8_t)(w ? 4 : -4);
    if (to > from) { chsNnMod(d, rk, from + 3, false); chsNnMod(d, rk, from + 1, true); }
    else           { chsNnMod(d, rk, from - 4, false); chsNnMod(d, rk, from - 1, true); }
  }
  return !q && !promo && !(t == 1 && to == chsEp && (from & 7) != (to & 7));
}
static inline void chsNnCopy(int ply) { memcpy(CHS_NN_ACC(ply + 1), CHS_NN_ACC(ply), (size_t)CHS_NN_H * 4); }   // null move: papan sama
static int chsAiEvalClassic();
static int chsNnEvalAt(int ply) {
  const int16_t* aw = CHS_NN_ACC(ply); const int16_t* ab = aw + CHS_NN_H;
  const int16_t* us = chsWhite ? aw : ab; const int16_t* th = chsWhite ? ab : aw;
  const int16_t* vu = chsNnV; const int16_t* vt = chsNnV + CHS_NN_H;
  int32_t s = chsNnC;
  for (int i = 0; i < CHS_NN_H; i++) {
    int x = us[i]; x = x < 0 ? 0 : (x > CHS_NN_QA ? CHS_NN_QA : x);
    int y = th[i]; y = y < 0 ? 0 : (y > CHS_NN_QA ? CHS_NN_QA : y);
    s += x * vu[i] + y * vt[i];
  }
  int cp = (int)(s * CHS_NN_MUL / CHS_NN_DIV);          // cp = s * 400 / (QA * QB)
#if CHS_NN_HYBRID
  cp += chsAiEvalClassic();
#endif
  return cp > 4000 ? 4000 : (cp < -4000 ? -4000 : cp);
}
static bool chsNnSelfTest() {                           // jalur SIMD harus sama persis dgn skalar; kalau tidak -> skalar
#if CHS_NN_PIE
  alignas(16) static int16_t a1[CHS_NN_H], a2[CHS_NN_H], r[CHS_NN_H];
  for (int i = 0; i < CHS_NN_H; i++) { r[i] = (int16_t)((i * 37 % 211) - 100 + (i == 3 ? 32000 : 0)); a1[i] = a2[i] = (int16_t)((i * 53 % 301) - 150 + (i == 3 ? 32000 : 0)); }
  chsNnPieOk = false; chsNnVAdd(a1, r); chsNnVSub(a1, r); chsNnVAdd(a1, r);
  chsNnPieOk = true;  chsNnPieAdd(a2, r); chsNnPieSub(a2, r); chsNnPieAdd(a2, r);
  for (int i = 0; i < CHS_NN_H; i++) if (a1[i] != a2[i]) { chsNnPieOk = false; return false; }
#endif
  return true;
}
static void chsNnFree() {
  if (chsNnMem) CHS_AI_FREE(chsNnMem);
  chsNnMem = nullptr; chsNnW = chsNnB = chsNnV = chsNnAcc = nullptr; chsNnReady = false; chsNnOn = false;
}
static inline int chsNnB64v(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : (c == '/' ? 63 : -1);
}
static bool chsNnDecode(uint8_t* out, size_t olen) {      // base64 -> biner; true bila panjangnya persis olen
  size_t n = 0; uint32_t acc = 0; int bits = 0;
  for (size_t i = 0; i < sizeof(chsNnBlob) - 1; i++) {
    int v = chsNnB64v(chsNnBlob[i]); if (v < 0) continue;
    acc = (acc << 6) | (uint32_t)v; bits += 6;
    if (bits >= 8) { bits -= 8; if (n < olen) out[n] = (uint8_t)(acc >> bits); n++; }
  }
  return n == olen;
}
static bool chsNnEnsure() {
  if (chsNnReady) return true;
  size_t szW = (size_t)768 * CHS_NN_H * 2, szB = (size_t)CHS_NN_H * 2, szV = (size_t)2 * CHS_NN_H * 2, szC = 16, szA = (size_t)(CHS_AI_PLY + 2) * 2 * CHS_NN_H * 2;
  size_t total = szW + szB + szV + szC + szA + 64;
  uint8_t* m = (uint8_t*)CHS_AI_ALLOC(total);
  if (!m) return false;
  uint8_t* p = (uint8_t*)(((uintptr_t)m + 15) & ~(uintptr_t)15);
  if (!chsNnDecode(p, szW + szB + szV + 4)) { CHS_AI_FREE(m); return false; }     // blob rusak / ukuran tak cocok -> evaluasi klasik
  chsNnMem = m;
  chsNnW = (int16_t*)p; p += szW; chsNnB = (int16_t*)p; p += szB; chsNnV = (int16_t*)p; p += szV;
  memcpy(&chsNnC, p, 4); p += szC; chsNnAcc = (int16_t*)p;
  memset(chsNnAcc, 0, szA);
  chsNnSelfTest();
  chsNnReady = true;
  return true;
}
#define CHS_NN_PUSH(ply, m)   do { if (chsNnOn) { bool _q = chsNnPush((ply), (m)); chsNnVal[(ply) + 1] = (CHS_NN_QUIETONLY ? (_q ? 1 : 0) : 1); } } while (0)
#define CHS_NN_COPY(ply)      do { if (chsNnOn) { chsNnCopy((ply)); chsNnVal[(ply) + 1] = chsNnVal[(ply)]; } } while (0)
#define CHS_NN_SYNC(ply)      do { if (chsNnOn) { chsNnRefresh((ply)); chsNnVal[(ply)] = 1; } } while (0)
#ifndef CHS_NN_QCLASSIC
#define CHS_NN_QCLASSIC 1                              // 1 = quiescence (node terbanyak) tetap evaluasi klasik: tak ada push NN di sana
#endif
#if CHS_NN_QCLASSIC
#define CHS_NN_QPUSH(ply, m)  do { if (chsNnOn) chsNnVal[(ply) + 1] = 0; } while (0)
#else
#define CHS_NN_QPUSH(ply, m)  CHS_NN_PUSH(ply, m)
#endif
#else
#define CHS_NN_NYX 0
static bool chsNnOn = false, chsNnReady = false; static int chsNnForce = -1;
static inline bool chsNnEnsure() { return false; }
static void chsNnFree() {}
static inline int chsNnEvalAt(int) { return 0; }
static inline void chsNnRefresh(int) {}
#define CHS_NN_PUSH(ply, m)   do {} while (0)
#define CHS_NN_QPUSH(ply, m)  do {} while (0)
#define CHS_NN_COPY(ply)      do {} while (0)
#define CHS_NN_SYNC(ply)      do {} while (0)
#endif

// ---------------------------------------------------------------- evaluasi (dari sudut pandang yg jalan)
// Materi + tabel petak + struktur pion (ganda, terisolasi, bebas), pasangan gajah, benteng di file terbuka / baris 7,
// mobilitas kuda/gajah/benteng, perlindungan raja (perisai pion), dan "mop-up" endgame (dorong raja lawan ke tepi
// saat unggul) supaya kemenangan teknis (mis. K+B vs K) benar-benar dituntaskan, bukan cuma berputar-putar.
static const int16_t chsEvPass[8] = {0, 8, 14, 24, 42, 72, 120, 0};
static const int8_t  chsEvKn[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
static const int8_t  chsEvDr[8][2] = {{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
static int chsEvMob(int sq, int8_t p, int t) {
  const int sg = p > 0 ? 1 : -1; int cnt = 0;
  if (t == 2) {
    const uint8_t* kt = chsKnT[sq]; const int kc = chsKnN[sq];
    for (int k = 0; k < kc; k++) if (chsBd[kt[k]] * sg <= 0) cnt++;
    return cnt;
  }
  const uint8_t* rn = chsRayN[sq];
  const int d0 = (t == 3) ? 4 : 0;                       // gajah = 4 arah diagonal, benteng = 4 arah lurus
  for (int d = d0; d < d0 + 4; d++) {
    const int nn = rn[d], off = chsDirOff[d]; int s = sq;
    for (int k = 0; k < nn; k++) {
      s += off;
      int q = chsBd[s] * sg;
      if (q == 0) cnt++; else { if (q < 0) cnt++; break; }
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
static int chsAiEvalClassic() {
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

// evaluasi tanpa ply (dipakai di luar pencarian): hitung akumulator penuh di slot cadangan
static int chsAiEval() {
#if CHS_NN_AVAIL
  if (chsNnOn) { chsNnRefresh(CHS_AI_PLY + 1); return chsNnEvalAt(CHS_AI_PLY + 1); }
#endif
  return chsAiEvalClassic();
}
static inline int chsAiEvalP(int ply) {
#if CHS_NN_AVAIL
  if (chsNnOn && chsNnVal[ply]) return chsNnEvalAt(ply);
#endif
  return chsAiEvalClassic();
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
  if ((chsAiNodes & 255u) == 0) {
    if (chsAiCheckTime && (int32_t)(CHS_AI_NOW() - chsAiDeadline) >= 0) chsAiAbort = true;
    if (CHS_AI_EXTRA_STOP()) chsAiAbort = true;
  }
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
  const int c = sq & 7; const int8_t sg = white ? 1 : -1;
  if (white) {
    if (sq >= 8) { if (c > 0 && b[sq - 9] == sg) return sq - 9; if (c < 7 && b[sq - 7] == sg) return sq - 7; }
  } else {
    if (sq < 56) { if (c > 0 && b[sq + 7] == sg) return sq + 7; if (c < 7 && b[sq + 9] == sg) return sq + 9; }
  }
  const uint8_t* kt = chsKnT[sq]; const int kc = chsKnN[sq]; const int8_t kk = (int8_t)(sg * 2);
  for (int i = 0; i < kc; i++) if (b[kt[i]] == kk) return kt[i];
  const uint8_t* rn = chsRayN[sq];
  int best = -1, bt = 9;
  for (int d = 0; d < 8; d++) {
    const int n = rn[d], off = chsDirOff[d]; int s = sq;
    for (int k = 1; k <= n; k++) {
      s += off;
      int8_t p = b[s];
      if (p) {
        if ((p > 0) == white) {
          int t = p > 0 ? p : -p;
          bool ok = (t == 6 && k == 1) || (d < 4 ? (t == 4 || t == 5) : (t == 3 || t == 5));
          if (ok && t < bt) { bt = t; best = s; }
        }
        break;
      }
    }
  }
  return best;
}
static int chsSee(uint16_t m) {
  int from = m & 63, to = (m >> 6) & 63, promo = (m >> 12) & 15;
  alignas(8) int8_t b[64]; memcpy(b, chsBd, 64);
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
  int stand = chsAiEvalP(ply);
  if (ply >= CHS_AI_PLY - 1 || chsAiStackLow()) return stand;
  if (stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  uint16_t* mv = chsAiMv + ply * 256; int16_t* ky = chsAiKy + ply * 256;
  const int ks0 = chsKingSq(chsWhite);
  const bool wasChk = ks0 >= 0 && chsAtt(ks0, !chsWhite);
  int n = chsGenCaps(mv), nc = 0;                                   // hanya tangkapan/promosi/e.p. (urutan sama dgn chsGen -> hasil identik)
  for (int i = 0; i < n; i++) {
    uint16_t m = mv[i]; int from = m & 63, to = (m >> 6) & 63;
    int8_t p = chsBd[from];
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
    CHS_NN_QPUSH(ply, mv[i]);
    const bool spc = chsMvSpecial(mv[i], ks0);
    chsDo(mv[i]);
    if (!chsLegalAfter(mv[i], spc, ks0, wasChk, mover)) { CHS_REST_AT(ply); continue; }
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
static int chsXTrust = 0;                                                 // 1 = percayai skor TT dari pencarian langkah sebelumnya (TT dipertahankan antar-langkah)
static int chsXR0 = 2, chsXR1 = 4, chsXRfp = 130, chsXRfpI = 100;      // parameter pencarian (bisa diatur utk percobaan)
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
  const int ks0 = chsKingSq(chsWhite);
  bool inchk = ks0 >= 0 && chsAtt(ks0, !chsWhite);
  if (inchk && ply < CHS_AI_PLY - 6) depth++;
  if (depth <= 0) return chsAiQ(alpha, beta, ply);
  if (ply >= CHS_AI_PLY - 1 || chsAiStackLow()) return chsAiEvalP(ply);
  int alpha0 = alpha; uint16_t ttm = 0;
  bool pvn = (beta - alpha) > 1;
  ChsTT* te = chsAiTT ? &chsAiTT[(uint32_t)hk & chsAiTTMask] : nullptr;
  uint32_t tw1 = 0, tw2 = 0; bool tthit = false;
  if (te) {                                                        // entri dibaca sbg 3 kata; kunci tersimpan = hash ^ kata1 ^ kata2 -> entri sobek (ditulis core lain di tengah baca) = meleset
    const volatile uint32_t* tp = (const volatile uint32_t*)te;
    uint32_t tw0 = tp[0]; tw1 = tp[1]; tw2 = tp[2];
    tthit = ((tw0 ^ tw1 ^ tw2) == hk32);
  }
  if (tthit) {
    ttm = (uint16_t)(tw1 >> 16);                                   // langkah TT dipakai utk urutan dari entri umur berapa pun
    if ((((tw2 >> 16) & 255u) == (uint32_t)chsAiAge || chsXTrust) && (int)(int8_t)(tw2 & 255u) >= depth) {      // skor hanya dipercaya dari pencarian ini: skor lama bergantung riwayat (remis ulang, 50 langkah)
      int s = chsAiFromTT((int)(int16_t)(tw1 & 0xFFFFu), ply);
      uint32_t tf = (tw2 >> 8) & 255u;
      if (tf == 0) return s;
      if (tf == 1 && s >= beta) return s;
      if (tf == 2 && s <= alpha) return s;
    }
  }
  int stand = 0; bool improving = false;
  if (!inchk) {
    stand = chsAiEvalP(ply);
    chsAiStAt[ply] = (int16_t)stand;
    improving = ply >= 2 && stand > chsAiStAt[ply - 2];
    if (!pvn && depth <= CHS_X_RFP && beta < CHS_AI_MATE - 1000 && stand - (improving ? chsXRfpI : chsXRfp) * depth >= beta) return stand;   // reverse futility
    if (nullOk && depth >= 3 && beta < CHS_AI_MATE - 1000 && stand >= beta && chsAiHasPieces(chsWhite)) { // null move
      bool sw = chsWhite; int8_t se = chsEp; uint16_t sh = chsHalf;               // null move: papan tidak berubah
      chsWhite = !chsWhite; chsEp = -1; chsHalf++;
      CHS_NN_COPY(ply);
      int R = chsXR0 + depth / chsXR1;
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
    if (te) { const volatile uint32_t* tp = (const volatile uint32_t*)te; uint32_t u0 = tp[0], u1 = tp[1], u2 = tp[2]; if ((u0 ^ u1 ^ u2) == hk32) ttm = (uint16_t)(u1 >> 16); }
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
    const bool spc = chsMvSpecial(m, ks0);
    CHS_NN_PUSH(ply, m);
    chsDo(m);
    if (!chsLegalAfter(m, spc, ks0, inchk, mover)) { CHS_REST_AT(ply); continue; }
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
    const volatile uint32_t* tp = (const volatile uint32_t*)te;
    uint32_t o0 = tp[0], o1 = tp[1], o2 = tp[2];
    bool same = ((o0 ^ o1 ^ o2) == hk32);
    if (!same || ((o2 >> 16) & 255u) != (uint32_t)chsAiAge || depth >= (int)(int8_t)(o2 & 255u)) {
      uint32_t n1 = (uint32_t)(uint16_t)(int16_t)chsAiToTT(best, ply) | ((uint32_t)bestm << 16);
      uint32_t nf = best <= alpha0 ? 2u : (best >= beta ? 1u : 0u);
      uint32_t n2 = (uint32_t)(uint8_t)(int8_t)depth | (nf << 8) | ((uint32_t)chsAiAge << 16);
      volatile uint32_t* tq = (volatile uint32_t*)te;              // urutan: data dulu, kunci (ter-XOR) terakhir
      tq[1] = n1; tq[2] = n2; tq[0] = hk32 ^ n1 ^ n2;
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
    { CHS_SAVE_ST; chsDo(lg[i]); CHS_NN_SYNC(2); int v = chsAiQ(-CHS_AI_INF, CHS_AI_INF, 2); CHS_REST_ST; Q[i] = chsMxClampQ(v); }
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
    { CHS_SAVE_ST; chsDo(rep[r]); CHS_NN_SYNC(2); V[r] = (float)chsAiQ(-CHS_AI_INF, CHS_AI_INF, 2); CHS_REST_ST; }
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

// kosongkan TT (benchmark NPS memulai tiap uji dari nol; juga membuang sisa partai lama). Hanya dipanggil saat tak ada pencarian berjalan.
static void chsAiTTClear() { if (chsAiTT) memset(chsAiTT, 0, ((size_t)chsAiTTMask + 1) * sizeof(ChsTT)); }

#if CHS_SMP
// Pencarian task pembantu (berjalan di chess_smp.cpp, pada SALINAN mesin milik pembantu). Hasilnya tak dipakai langsung: tujuannya mengisi TT bersama.
// Kedalaman = kedalaman iterasi pemain utama + 1 (hasil lebih dalam di TT memangkas iterasi utama berikutnya); selesai satu kedalaman -> lanjut yg lebih dalam.
static void chsAiHelperRun(int maxDepth, uint32_t deadline) {
  if (!chsAiEnsure()) return;
  uint16_t pm[256]; int pn = chsGen(pm), n = 0;
  for (int i = 0; i < pn; i++) if (chsLegal(pm[i])) chsAiRm[n++] = pm[i];
  if (n <= 1) return;
  if (maxDepth > CHS_AI_MAXD) maxDepth = CHS_AI_MAXD;
  for (int i = 0; i < n; i++) chsAiKy[i] = (int16_t)chsAiKey(chsAiRm[i]);
  for (int i = 0; i < n; i++) chsAiPick(chsAiRm, chsAiKy, n, i);
  { char c0; chsAiSp0 = (uintptr_t)&c0; }
  chsAiDeadline = deadline; chsAiNodes = 0; chsAiCheckTime = true; chsAiAbort = false;
  memset(chsAiKil, 0, sizeof(chsAiKil)); if (chsAiHis) memset(chsAiHis, 0, 2 * 4096 * 2);
  if (chsAiPath) chsAiPath[0] = (uint32_t)chsAiH64();
  if (!chsAiLmrOk) chsAiLmrInit();
  memset(chsAiStAt, 0, sizeof(chsAiStAt));
  int hd = 1;                                                      // kedalaman terakhir yg SELESAI
  while (!chsAiAbort && hd < maxDepth) {
    int d = chsSmpMainDepthV + 1; if (d <= hd) d = hd + 1; if (d < 2) d = 2; if (d > maxDepth) d = maxDepth;
    int curBest = -CHS_AI_INF, bestIdx = -1; bool done = true;
    for (int i = 0; i < n; i++) {
      CHS_SAVE_ST;
      chsDo(chsAiRm[i]);
      int s;
      if (bestIdx >= 0) {
        s = -chsAiNeg(d - 1, -curBest - 1, -curBest, 1, true);
        if (!chsAiAbort && s > curBest) s = -chsAiNeg(d - 1, -CHS_AI_INF, -curBest, 1, true);
      } else s = -chsAiNeg(d - 1, -CHS_AI_INF, CHS_AI_INF, 1, true);
      CHS_REST_ST;
      if (chsAiAbort) { done = false; break; }
      if (bestIdx < 0 || s > curBest) { curBest = s; bestIdx = i; }
    }
    if (!done) break;
    hd = d;
    if (bestIdx > 0) { uint16_t tm = chsAiRm[bestIdx]; for (int i = bestIdx; i > 0; i--) chsAiRm[i] = chsAiRm[i - 1]; chsAiRm[0] = tm; }   // langkah terbaik ke depan utk iterasi berikut
    if (curBest >= CHS_AI_MATE - 100 || curBest <= -CHS_AI_MATE + 100) break;
  }
}
#endif

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
  chsAiDeadline = t0 + (margin > 0 ? budgetMs * 85 / 100 : budgetMs); chsAiNodes = 0;     // margin > 0: 15% waktu utk verifikasi kandidat sesudah pencarian utama
  chsAiAge++; memset(chsAiKil, 0, sizeof(chsAiKil)); if (chsAiHis) memset(chsAiHis, 0, 2 * 4096 * 2);
  if (chsAiPath) chsAiPath[0] = (uint32_t)chsAiH64();
  if (!chsAiLmrOk) chsAiLmrInit();
  chsXTrust = (mode == 3) ? 1 : 0;                  // Nyx: skor TT dari langkah sebelumnya dipercaya (efek mirip pondering: pohon yg sudah dihitung tak dibuang)
  memset(chsAiStAt, 0, sizeof(chsAiStAt));
#if CHS_NN_AVAIL
  chsNnOn = false;
  if (chsNnForce == 1 || (chsNnForce < 0 && CHS_NN_NYX && mode == 3)) chsNnOn = chsNnEnsure();
  if (chsNnOn) { chsNnRefresh(0); chsNnVal[0] = 1; }
#endif
  int smpCores = 1; uint32_t smpNodesH = 0;
#if CHS_SMP
  if (chsSmpOn && !chsNnOn && chsAiTT && budgetMs >= CHS_SMP_MIN_MS && maxDepth >= CHS_SMP_MIN_DEPTH)      // core ke-2 (Lazy SMP): hanya pencarian panjang; NN utama aktif -> evaluasi beda -> 1 core
    if (chsSmpBegin(chsBd, chsWhite ? 1 : 0, chsCastle, chsEp, chsHalf, chsAiAge, chsXTrust, chsAiDeadline, maxDepth, chsAiTT, chsAiTTMask)) smpCores = 2;
#endif
  int dDone = 0, mg = margin > 0 ? 0 : margin;        // pencarian utama di akar SELALU PVS jendela-nol (mg = 0); kandidat dalam margin diverifikasi sesudahnya
  for (int d = 1; d <= maxDepth; d++) {
#if CHS_SMP
    chsSmpMainDepthV = d;                                                     // pembantu mencari 1 kedalaman di depan iterasi ini
#endif
    chsAiCheckTime = (d > 1); chsAiAbort = false;
    int curBest = -CHS_AI_INF; bool done = true; int bestIdx = -1;
    for (int i = 0; i < n; i++) {
      CHS_SAVE_ST;
      CHS_NN_PUSH(0, chsAiRm[i]);
      chsDo(chsAiRm[i]);
      int alpha = (curBest == -CHS_AI_INF) ? -CHS_AI_INF : curBest - mg;
      int s;
#if CHS_X_PVSROOT
      if (mg == 0 && curBest != -CHS_AI_INF) {                                 // jendela nol dulu; hanya bila lolos dicari ulang penuh
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
    if (!done && mg == 0 && bestIdx >= 0 && d > 1) {                       // iterasi terpotong waktu: langkah terbaik yg sudah terbukti di iterasi ini dipakai
      for (int i = 0; i < n; i++) chsAiFx[i] = 0;
      uint16_t tm = chsAiRm[bestIdx]; chsAiRm[bestIdx] = chsAiRm[0]; chsAiRm[0] = tm;
      chsAiFs[0] = curBest; chsAiFx[0] = 1;
      break;
    }
#endif
    if (!done) break;
    memcpy(chsAiFs, chsAiSc, (size_t)n * 4); memcpy(chsAiFx, chsAiEx, (size_t)n); dDone = d;
    for (int i = 1; i < n; i++) {                                              // urutkan akar: eksak dulu (skor menurun), lalu batas atas; iterasi berikut memangkas lebih awal
      uint16_t tm = chsAiRm[i]; int32_t ts = chsAiFs[i]; uint8_t te = chsAiFx[i];
      int64_t key = te ? (int64_t)ts + 100000 : (int64_t)ts; int j = i - 1;
      while (j >= 0 && (chsAiFx[j] ? (int64_t)chsAiFs[j] + 100000 : (int64_t)chsAiFs[j]) < key) { chsAiRm[j + 1] = chsAiRm[j]; chsAiFs[j + 1] = chsAiFs[j]; chsAiFx[j + 1] = chsAiFx[j]; j--; }
      chsAiRm[j + 1] = tm; chsAiFs[j + 1] = ts; chsAiFx[j + 1] = te;
    }
    if (curBest >= CHS_AI_MATE - 100 && mg == 0) break;                        // mat sudah ketemu
    if (d < maxDepth && (CHS_AI_NOW() - t0) * CHS_X_TFRAC > budgetMs) break;   // iterasi berikut hampir pasti tidak selesai
  }
#if CHS_SMP
  if (smpCores == 2) { chsSmpEnd(); smpNodesH = chsSmpNodes(); }               // pencarian utama selesai (waktu/kedalaman/mat): hentikan pembantu, ambil jumlah node-nya
#endif
  if (margin > 0 && dDone >= 2) {                                              // verifikasi kandidat: langkah yg skornya masih mungkin >= terbaik - margin (tes jendela-nol)
    int bs = -CHS_AI_INF; for (int i = 0; i < n; i++) if (chsAiFx[i] && chsAiFs[i] > bs) bs = chsAiFs[i];
    if (bs > -CHS_AI_INF && bs < CHS_AI_MATE - 1000) {
      int thr = bs - margin; chsAiAbort = false; chsAiCheckTime = true; chsAiDeadline = t0 + budgetMs;
      for (int i = 0; i < n; i++) {
        if (chsAiFx[i] || chsAiFs[i] < thr) continue;
        CHS_SAVE_ST; CHS_NN_PUSH(0, chsAiRm[i]); chsDo(chsAiRm[i]);
        int sv = -chsAiNeg(dDone - 1, -thr, -(thr - 1), 1, true);
        CHS_REST_ST;
        if (chsAiAbort) break;
        if (sv >= thr) { chsAiFs[i] = sv; chsAiFx[i] = 1; }
      }
      chsAiAbort = false;
    }
  }
  CHS_AI_REPORT(dDone, chsAiNodes, smpNodesH, CHS_AI_NOW() - t0, smpCores);   // NPS: node (utama + pembantu) / waktu pencarian
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
#if defined(__GNUC__) && !defined(CHS_NO_OPT_PRAGMA)
#pragma GCC pop_options
#endif
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
static bool          chsGem = false;           // true = adu MESIN vs GEMINI (kamu penonton; chsVsAI ikut true, kursi chsRole = kursi mesin)
static bool          chsGemForfeit = false;    // Gemini kalah karena 3x langkah tak sah
static uint8_t       chsAiLevel = 1;           // 0 Mudah, 1 Sedang, 2 Sulit, 3 Cermin (AI meniru gaya user), 4 Nyx (AI menghukum kebiasaan user)
static uint8_t       chsAfkIdx = 2;            // indeks chsAfkSec: 0 = mati
static bool          chsAfkAI = false;         // AI sedang memainkan giliran user (user AFK)
static bool          chsAiUserWhite = true;
static bool          chsAiQueued = false, chsPrevMyTurn = false;
static unsigned long chsAiDueMs = 0, chsLastTouchMs = 0, chsTurnStartMs = 0;
static uint8_t       chsUnsaved = 0;
static const char* const chsLvName[5] = {"Mudah", "Sedang", "Sulit", "Cermin", "Nyx"};
static const uint16_t chsAfkSec[4] = {0, 30, 60, 120};
static const uint8_t  chsLvDepth[5]  = {2, 4, 6, 4, 14};
static const uint16_t chsLvMs[5]     = {250, 700, 1800, 700, 2400};
static const int16_t  chsLvMargin[5] = {120, 10, 0, 70, 15};
static const uint8_t  chsLvMode[5]   = {1, 1, 0, 2, 3};    // 0 terbaik | 1 acak di antara yg layak | 2 gaya user | 3 Nyx (jebakan kebiasaan)

static void chsPrefsLoad() {
  Preferences p;
  if (!p.begin("chess", true)) return;
  uint8_t lv = p.getUChar("lvl", 1); if (lv < 5) chsAiLevel = lv;
  uint8_t af = p.getUChar("afk", 2); if (af < 4) chsAfkIdx = af;
#if CHS_SMP
  chsSmpOn = p.getUChar("smp", 1) != 0;
#endif
  uint8_t* sb = (uint8_t*)malloc(CHS_STY_LEN + CHS_BK_MAXLEN + 16);          // heap, bukan stack (task loop cuma 8 KB)
  if (sb) {
    size_t L = p.getBytesLength("sty");
    bool styOk = false;                                                         // sty versi lama / rusak -> jaringan DAN kebiasaan mulai dari awal (hab dilatih bersama jaringannya)
    if (L >= CHS_STY_LEN && L <= CHS_STY_LEN + 8) { p.getBytes("sty", sb, L); styOk = chsStyleDeser(sb, L); }
    L = p.getBytesLength("bk");
    if (L >= 4 && L <= CHS_BK_MAXLEN) { p.getBytes("bk", sb, L); chsBookDeser(sb, L); }
    L = p.getBytesLength("hab");
    if (styOk && L == CHS_HAB_LEN) { p.getBytes("hab", sb, L); chsHabDeser(sb, L); }       // kebiasaan petak (baru; perangkat lama: kosong)
    L = p.getBytesLength("mix");
    if (styOk && L == CHS_MX_LEN) { p.getBytes("mix", sb, L); chsMxDeser(sb, L); }       // model campuran Nyx v2 (perangkat lama: nilai awal)
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
  if (p.begin("chess", false)) { p.putUChar("lvl", chsAiLevel); p.putUChar("afk", chsAfkIdx);
#if CHS_SMP
  p.putUChar("smp", chsSmpOn ? 1 : 0);
#endif
  p.end(); }
}
// Semua blob dilayani buffer yg sama (sty 1804 | bk <= 900 | hab 900 | mix 44 | rb <= 1604 byte).
static_assert(CHS_HAB_LEN <= CHS_STY_LEN + CHS_BK_MAXLEN + 16 && CHS_MX_LEN <= CHS_STY_LEN + CHS_BK_MAXLEN + 16 && CHS_RB_MAXLEN <= CHS_STY_LEN + CHS_BK_MAXLEN + 16, "buffer NVS kurang");
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
      L = chsMxSer(sb); p.putBytes("mix", sb, L);
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
  return chsScr == 1 && chsConn == 1 && chsReady && (chsRole == 1 || chsRole == 2) && !chsGem &&
         chsOppPid()[0] && chsEnd == 0 && ((chsRole == 1) == chsWhite);
}

// ---------------------------------------------------------------------
// AKSI LOKAL
// ---------------------------------------------------------------------
static void chsCommitEx(uint16_t m, bool learn) {
  char t[8]; chsMoveStr(m, t);
  size_t L = strlen(chsMoves);
  if (L + 8 > CHS_BUF - 40) return;
  if (learn) { chsMxLive = (chsVsAI && chsAiLevel == 4); chsStyleLearn(m, (int)chsPly); chsUnsaved++; }       // belajar dari langkah USER sebelum langkah dijalankan (model lawan Nyx hanya diamati saat level Nyx dipilih: biayanya n x quiescence)
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
  chsVsAI = false; chsGem = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false; chsLastTouchMs = millis(); chsTurnStartMs = millis();
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
  chsVsAI = true; chsGem = false; chsAfkAI = false; chsAiQueued = false;
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
// ==== PONDER BEGIN ====
// Pondering Nyx: selagi user berpikir, AI mencari posisi setelah TEBAKAN balasan user (langkah terbaik pencarian dangkal). Hasilnya mengisi TT yg
// dipertahankan; saat giliran Nyx pohon itu dipakai (chsXTrust). Hanya saat layar diam > 1,2 dtk, irisan 50 ms (UI tetap responsif), total
// <= CHS_PONDER_MAX_MS per giliran user (hemat baterai). Papan dikembalikan sebelum fungsi kembali. Matikan: -DCHS_PONDER=0.
#ifndef CHS_PONDER
#define CHS_PONDER 1
#endif
#ifndef CHS_PONDER_MAX_MS
#define CHS_PONDER_MAX_MS 5000
#endif
static uint32_t chsPonderKey = 0, chsPonderUsed = 0; static uint16_t chsPonderPred = 0;
static void chsAiPonder() {
#if CHS_PONDER
  if (!chsVsAI || chsEnd != 0 || chsAnimOn || chsAiLevel > 4 || chsLvMode[chsAiLevel] != 3) return;
  if (chsRole != 1 && chsRole != 2) return;
  if ((unsigned long)(millis() - chsLastTouchMs) < 1200UL) return;
  uint32_t key = chsPosHash();
  if (key != chsPonderKey) { chsPonderKey = key; chsPonderUsed = 0; chsPonderPred = 0; chsAiHistBuild(chsMoves); }
  if (chsPonderUsed >= CHS_PONDER_MAX_MS) return;
  unsigned long t0 = millis();
  uiBoostKick();
  CHS_SAVE_ST;
  if (chsPonderPred == 0) { uint16_t pr = chsAiChoose(4, 150, 0, 0, (int)chsPly); chsPonderPred = pr ? pr : 0xFFFF; }   // tebak balasan user
  if (chsPonderPred != 0xFFFF) {
    chsDo(chsPonderPred);
    uint16_t pm[256]; int pn = chsGen(pm); bool any = false;
    for (int i = 0; i < pn && !any; i++) if (chsLegal(pm[i])) any = true;
    if (any) chsAiChoose(14, 50, 0, 0, (int)chsPly + 1);
  }
  CHS_REST_ST;
  chsPonderUsed += (uint32_t)(millis() - t0);
#endif
}
// ==== PONDER END ====
static void chsAiPollVs() {
  if (!chsAiTurn() || chsAnimOn) { chsAiQueued = false; chsAiPonder(); return; }
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
// ADU MESIN vs GEMINI (kamu penonton)
//   Mesin lokal (level yg dipilih di menu) lawan Gemini lewat API yg sama dgn AI Chat (kunci: /gemini_key.txt).
//   Gemini diberi FEN + riwayat + DAFTAR LANGKAH SAH, diminta membalas satu langkah UCI. Balasan tak sah diulang
//   (maks 3x per langkah); 3x berturut-turut tak sah = Gemini kalah. Galat jaringan = jeda, ketuk "Lanjut".
//   Skor per level disimpan di NVS: M = mesin menang, G = Gemini menang, S = seri.
// ---------------------------------------------------------------------
static int  chsResultForMe();
static void chsGemStatSave();
static bool chsGemLaunch();
// ==== GEM BEGIN ====
static bool          chsGemEngWhite = true;      // mesin memegang putih?
static bool          chsGemPaused = false;
static volatile uint8_t chsGemSt = 0;            // 0 diam | 1 permintaan ke Gemini berjalan | 2 balasan siap diproses
static volatile bool chsGemOk = false;
static uint32_t      chsGemGen = 0;              // naik tiap partai baru / berhenti: balasan lama diabaikan
static uint32_t      chsGemReqGen = 0;
static String        chsGemPrompt, chsGemReply;
static uint8_t       chsGemBad = 0;              // balasan tak sah beruntun utk langkah ini
static char          chsGemBadMv[14] = "";
static unsigned long chsGemReqMs = 0, chsGemDueMs = 0, chsGemSecMs = 0;
static bool          chsGemQueued = false, chsGemRec = false;
static char          chsGemMsg[48] = "";
static uint16_t      chsGemStat[5][4];           // per level: mesin menang | Gemini menang | seri | Gemini kalah krn tak sah

static void chsGemSetMsg(const char* a, const char* b) {
  size_t k = 0;
  for (; a && a[k] && k < sizeof(chsGemMsg) - 1; k++) chsGemMsg[k] = a[k];
  for (size_t j = 0; b && b[j] && k < sizeof(chsGemMsg) - 1; j++) { char c = b[j]; chsGemMsg[k++] = (c == '\n' || c == '\r') ? ' ' : c; }
  chsGemMsg[k] = 0;
}

static void chsGemBuildPrompt() {
  char fen[104], legal[1200];
  chsFen(fen, sizeof(fen), (int)chsPly);
  chsLegalList(legal, sizeof(legal));
  const char* hist = chsMoves ? chsMoves : "";
  size_t hl = strlen(hist);
  if (hl > 400) { hist += hl - 400; while (*hist && *hist != ' ') hist++; if (*hist == ' ') hist++; }   // 400 huruf terakhir, mulai di batas langkah
  String p;
  p.reserve(1700);
  p = "You are playing a chess game as ";
  p += chsWhite ? "White" : "Black";
  p += " against a chess engine. Choose the strongest move.\n";
  p += "Position (FEN): "; p += fen; p += "\n";
  p += "Recent moves (UCI): "; p += (*hist ? hist : "(game start)"); p += "\n";
  p += "Legal moves (UCI): "; p += legal; p += "\n";
  if (chsGemBad) { p += "Your previous answer \""; p += chsGemBadMv; p += "\" was NOT in the legal moves list. "; }
  p += "Reply with exactly ONE move copied from the legal moves list, in UCI notation (for example e2e4 or e7e8q), and nothing else.";
  chsGemPrompt = p;
}

static void chsGemFinish() {                      // catat skor sekali per partai
  if (chsGemRec) return;
  chsGemRec = true;
  int r = chsResultForMe();                       // sudut pandang MESIN (chsRole = kursi mesin)
  int lv = chsAiLevel > 4 ? 4 : chsAiLevel;
  if (r == 1)       { if (chsGemStat[lv][0] < 65535) chsGemStat[lv][0]++; if (chsGemForfeit && chsGemStat[lv][3] < 65535) chsGemStat[lv][3]++; }
  else if (r == -1) { if (chsGemStat[lv][1] < 65535) chsGemStat[lv][1]++; }
  else if (r == 0)  { if (chsGemStat[lv][2] < 65535) chsGemStat[lv][2]++; }
  chsGemStatSave();
  needRedraw = true;
}

static void chsGemPoll() {
  if (!chsGem) return;
  if (chsEnd != 0) { chsGemFinish(); return; }
  if (chsGemSt == 2) {                            // balasan (atau galat) dari task Gemini
    chsGemSt = 0;
    if (chsGemReqGen != chsGemGen) return;        // dari partai/sesi lama
    if (!chsGemOk) {
      chsGemPaused = true;
      chsGemSetMsg("Gemini gagal: ", chsGemReply.c_str());
      needRedraw = true;
      return;
    }
    uint16_t m;
    if (chsPickFromReply(chsGemReply.c_str(), &m)) {
      chsGemBad = 0; chsGemMsg[0] = 0;
      chsCommitEx(m, false);
      chsGemDueMs = millis() + 450UL;
      return;
    }
    chsGemBad++;
    {                                             // simpan sebagian balasan utk ditegur di prompt berikutnya
      size_t k = 0; const char* r = chsGemReply.c_str();
      for (; r[k] && k < sizeof(chsGemBadMv) - 1; k++) { char c = r[k]; chsGemBadMv[k] = (c == '"' || c == '\n' || c == '\r') ? ' ' : c; }
      chsGemBadMv[k] = 0;
    }
    if (chsGemBad >= 3) {                         // 3x berturut-turut: Gemini kalah
      strcpy(chsRes, chsWhite ? "RW" : "RB");
      chsGemForfeit = true;
      chsUpdateEnd();
    } else chsGemDueMs = millis() + 400UL;
    needRedraw = true;
    return;
  }
  if (chsGemSt == 1) {                            // menunggu: segarkan hitungan detik tiap 1 dtk
    if (millis() - chsGemSecMs >= 1000UL) { chsGemSecMs = millis(); needRedraw = true; }
    return;
  }
  if (chsGemPaused || chsAnimOn || chsPromoOn) return;
  if ((long)(millis() - chsGemDueMs) < 0) return;
  if (chsWhite == chsGemEngWhite) {               // giliran mesin
    if (!chsGemQueued) { chsGemQueued = true; chsGemDueMs = millis() + 450UL; needRedraw = true; return; }
    chsGemQueued = false;
    uint16_t m = chsAiMoveFor(chsAiLevel);
    if (m) chsCommitEx(m, false);
    chsGemDueMs = millis();
    return;
  }
  chsGemBuildPrompt();                            // giliran Gemini
  if (!chsGemLaunch()) {
    chsGemPaused = true;
    chsGemSetMsg(WiFi.status() == WL_CONNECTED ? "Gagal membuat task Gemini" : "WiFi belum terhubung", "");
    needRedraw = true;
  }
}
// ==== GEM END ====

static TaskHandle_t chsGemTask = nullptr;
static StackType_t* chsGemStack = nullptr;
static StaticTask_t chsGemTCB;
static const char*  CHS_GEM_MODEL = "";           // "" = model bawaan AI Chat; isi nama model Gemini lain kalau mau lawan yg lebih kuat

static void chsGemTaskFunc(void*) {
  String resp; bool ok = false;
  for (int a = 1; a <= 3; a++) {
    if (a > 1) { delay(400 * a); if (WiFi.status() != WL_CONNECTED) break; }
    gemModelOverride = CHS_GEM_MODEL;
    ok = doGeminiHttpRequest(chsGemPrompt, resp);
    gemModelOverride = nullptr;
    if (ok || chsGemReqGen != chsGemGen) break;
  }
  chsGemReply = resp; chsGemOk = ok;
  delay(700);                                     // beri lwIP/mbedTLS waktu melepas buffer TLS sebelum permintaan berikutnya
  chsGemTask = nullptr;
  chsGemSt = 2;
  vTaskDelete(NULL);
}

static bool chsGemLaunch() {
  if (chsGemTask || WiFi.status() != WL_CONNECTED) return false;
  chsGemReqGen = chsGemGen; chsGemOk = false; chsGemReqMs = millis(); chsGemSecMs = chsGemReqMs;
  chsGemSt = 1;
  BaseType_t r = createTaskPsramStack(chsGemTaskFunc, "chsGem", 12288, NULL, &chsGemStack, &chsGemTCB, &chsGemTask, 1);
  if (r != pdPASS) { chsGemTask = nullptr; chsGemSt = 0; return false; }
  return true;
}

static void chsGemStatLoad() {
  Preferences p;
  if (!p.begin("chess", true)) return;
  if (p.getBytesLength("gst") == sizeof(chsGemStat)) p.getBytes("gst", chsGemStat, sizeof(chsGemStat));
  p.end();
}
static void chsGemStatSave() {
  Preferences p;
  if (p.begin("chess", false)) { p.putBytes("gst", chsGemStat, sizeof(chsGemStat)); p.end(); }
}

static void chsGemSeats() {                       // kursi: chsRole = kursi MESIN (papan dilihat dari sisi mesin)
  chsRole = chsGemEngWhite ? 1 : 2;
  char an[24]; snprintf(an, sizeof(an), "AI %s", chsLvName[chsAiLevel]);
  if (chsRole == 1) { strcpy(chsWPid, chsPid); strcpy(chsBPid, "gem"); strncpy(chsWName, an, 23); chsWName[23] = 0; strcpy(chsBName, "Gemini"); }
  else              { strcpy(chsBPid, chsPid); strcpy(chsWPid, "gem"); strncpy(chsBName, an, 23); chsBName[23] = 0; strcpy(chsWName, "Gemini"); }
}

static void chsGemNewGame() {
  chsGemGen++;
  chsGemSeats();
  chsGenNo = 0; chsNewGameLocal();
  chsGemPaused = false; chsGemBad = 0; chsGemMsg[0] = 0; chsGemRec = false; chsGemForfeit = false; chsGemQueued = false;
  chsGemDueMs = millis() + 600UL;
  needRedraw = true;
}

static void chsStartGem() {
  if (!chsMoves) { showToast("RAM tidak cukup utk Catur", 2200); return; }
  geminiApiKey.trim();
  if (geminiApiKey.length() == 0 || geminiApiKey == "YOUR_GEMINI_API_KEY_HERE") { showToast("Isi API key Gemini dulu (AI Chat / SD)", 2600); return; }
  if (WiFi.status() != WL_CONNECTED) { showToast("WiFi belum terhubung", 2200); return; }
  if (chsGemTask) { showToast("Tunggu permintaan Gemini sebelumnya selesai", 2400); return; }
  if (!chsAiEnsure()) { showToast("PSRAM tidak cukup utk AI", 2200); return; }
  chsActive = false;
  chsVsAI = true; chsGem = true; chsAfkAI = false; chsAiQueued = false;
  chsGemSt = 0;
  chsGemEngWhite = true;
  chsClaimW = chsClaimB = false; chsRemoteGen = 0; chsRemotePly = 0; chsRemoteRes[0] = 0;
  chsSendG = false; chsPubSeat = 0;
  chsReady = true; chsConn = 1; chsErr[0] = 0;
  chsLastTouchMs = millis();
  chsGemNewGame();
  chsScr = 1;
  needRedraw = true;
}

static void chsGemStop() {                        // keluar dari mode Gemini (Menu / keluar app): permintaan yg berjalan dibatalkan
  if (!chsGem) return;
  chsGem = false; chsGemPaused = false; chsGemQueued = false;
  chsGemGen++;                                    // balasan yg masih di jalan akan diabaikan
  if (chsGemTask && aiClientPtr) aiClientPtr->stop();
}

static void chsGemBtn0() {                        // tombol kiri panel: Jeda/Lanjut, atau Main lagi (warna ditukar) setelah partai selesai
  if (chsEnd != 0) { chsGemEngWhite = !chsGemEngWhite; chsGemNewGame(); return; }
  chsGemPaused = !chsGemPaused;
  if (!chsGemPaused) { chsGemMsg[0] = 0; chsGemDueMs = millis(); }
  needRedraw = true;
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
  chsVsAI = false; chsGem = false; chsAfkAI = false; chsAiQueued = false; chsPrevMyTurn = false;
  chsGemStatLoad();
  chsActive = false; chsConn = 0; chsReady = false;
  chsScr = 0; chsSel = -1; chsPromoOn = false; chsConfirm = false;
  chsReset(); chsEnd = 0;
  { Preferences p; if (p.begin("chess", true)) { chsPanelHidden = p.getBool("hide", false); p.end(); } }
  chsPressId = -1; chsInChk = false;
}

static void chsFreeMasks();
static void chsVwClose();
static void chsBenchPollIf();
void chessExit() {
  chsActive = false;        // task melepas koneksi TLS sendiri
  chsConn = 0;
  chsAnimOn = false;
  chsStyleSave();           // simpan hasil belajar gaya
  chsGemStop();
  chsVsAI = false; chsAfkAI = false; chsAiQueued = false;
  chsAiFree();
  chsVwClose();             // bobot awal layar Jaringan (PSRAM)
  chsFreeMasks();           // topeng bidak (PSRAM) dibebaskan saat keluar
}

void chessLoopPoll() {
  if (chsDirty) { chsDirty = false; if (curScreen() == SCR_CHESS) needRedraw = true; }
  if (curScreen() == SCR_CHESS && chsPressId >= 0 && millis() - chsPressMs >= CHS_PRESS_MS) { chsPressId = -1; needRedraw = true; }   // efek tekan selesai
  chsBenchPollIf();                                  // NYX-SMP-FIX1: uji NPS bertahap (layar Jaringan)
  if (curScreen() != SCR_CHESS || chsScr != 1 || !chsMoves) return;
  if (chsGem) chsGemPoll(); else if (chsVsAI) chsAiPollVs(); else { chsProcessRx(); chsAfkPoll(); }
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
    case 6: if (chsGem && chsGemForfeit) return chsRes[1] == 'W' ? "Gemini (putih) 3x tak sah. Kalah" : "Gemini (hitam) 3x tak sah. Kalah";
            return chsRes[1] == 'W' ? "Putih menyerah. Hitam menang" : "Hitam menyerah. Putih menang";
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
  if (chsGem) return r == 1 ? "Mesin menang!" : r == -1 ? "Gemini menang" : r == 0 ? "Seri" : "Selesai";
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
    if (chsTrainN && chsAccN >= 5) snprintf(cap, sizeof(cap), "Belajar: %u langkah, tebak %d%%", (unsigned)chsTrainN, (int)(chsAcc * 100.0f + 0.5f));
    else if (chsTrainN) snprintf(cap, sizeof(cap), "AI belajar gayamu: %u langkah", (unsigned)chsTrainN);
    else strcpy(cap, "AI belajar gayamu saat main");
    chsClip(cap, cb, (SCR_W - 16 - 66 - 64) / 6);
    s.setTextColor(T().subtext); s.setCursor(10, by2 + 30); s.print(cb);
    nyxAaRR(s, SCR_W - 8 - 58, by2 + 27, 58, 16, 7, chsPressCol(9, blend565(T().surface2, 0xFFFF, 20)));   // buka layar Jaringan
    s.setTextColor(T().accent); chsPrintC(s, SCR_W - 8 - 58, 58, by2 + 31, "Jaringan");
    nyxAaRR(s, SCR_W - 8 - 58 - 6 - 58, by2 + 27, 58, 16, 7, chsPressCol(10, blend565(T().surface2, 0xFFFF, 20)));   // adu mesin vs Gemini
    s.setTextColor(T().accent); chsPrintC(s, SCR_W - 8 - 58 - 6 - 58, 58, by2 + 31, "Gemini");
  }
}

// teks + warna status giliran (dipakai pil di panel dan chip saat panel disembunyikan)
static void chsStatusText(char* line, size_t n, uint16_t* sc) {
  const char* t; uint16_t c = T().accent;
  static char eb[48];
  if (chsGem) {
    if (chsEnd)                                  { t = chsEndShort(); int rr = chsResultForMe(); c = rr == 1 ? T().good : rr == -1 ? T().danger : T().text; }
    else if (chsGemPaused && chsGemMsg[0])       { chsClip(chsGemMsg, eb, 46); t = eb; c = T().danger; }
    else if (chsGemPaused)                       { t = "Jeda"; c = T().subtext; }
    else if (chsWhite == chsGemEngWhite)         { t = "Mesin berpikir..."; c = T().accent; }
    else { snprintf(eb, sizeof(eb), "Gemini berpikir %us", (unsigned)((millis() - chsGemReqMs) / 1000UL)); t = eb; c = T().accent; }
  }
  else if (chsVsAI) {
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
  char nb[40];
  if (chsGem) snprintf(nb, sizeof(nb), "Mesin: %s", myN);
  else snprintf(nb, sizeof(nb), "Kamu : %s", myN[0] ? myN : (chatName.length() ? chatName.c_str() : "HP"));
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
  if (chsGem) { int gl = chsAiLevel > 4 ? 4 : chsAiLevel; snprintf(nb, sizeof(nb), "M%u G%u S%u #%u", (unsigned)chsGemStat[gl][0], (unsigned)chsGemStat[gl][1], (unsigned)chsGemStat[gl][2], (unsigned)chsPly); }
  else if (chsVsAI) snprintf(nb, sizeof(nb), "Lawan AI  #%u %s", (unsigned)chsPly, lt);
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
  if (chsGem) {
    iosRR(s, x, y, w, h, 6, chsPressCol(0, over ? (uint16_t)T().accent : (uint16_t)T().surface2));
    s.setTextColor(over ? (uint16_t)T().bg : (uint16_t)T().accent); chsPrintC(s, x, w, y + 7, over ? "Main lagi" : (chsGemPaused ? "Lanjut" : "Jeda"));
  } else if (over) {
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
// =====================================================================
// NPS + UJI KECEPATAN (core ke-2). chsAiReport dipanggil chsAiChoose tiap selesai mencari: cetak ke Serial dan simpan utk tab "NPS" di layar Jaringan.
// Uji NPS = 3 posisi tetap x CHS_BENCH_MS, sekali dgn 1 core lalu sekali dgn 2 core (TT dikosongkan tiap posisi); berjalan bertahap di chessLoopPoll.
// =====================================================================
#define CHS_BENCH_MS 1000
static uint32_t chsStNm = 0, chsStNh = 0, chsStMs = 0;
static int      chsStD = 0, chsStCores = 1;
static uint32_t chsLsNm = 0, chsLsNh = 0, chsLsMs = 0; static int chsLsD = 0, chsLsCores = 1;   // panel "Terakhir": hanya pencarian >= 100 ms
static void chsAiReport(int d, uint32_t nm, uint32_t nh, uint32_t ms, int cores) {
  chsStD = d; chsStNm = nm; chsStNh = nh; chsStMs = ms; chsStCores = cores;
  if (ms >= 100) { chsLsD = d; chsLsNm = nm; chsLsNh = nh; chsLsMs = ms; chsLsCores = cores; }
  uint32_t nps = ms ? (uint32_t)(((uint64_t)nm + nh) * 1000ULL / ms) : 0;
  Serial.printf("[NYX-NPS] d=%d  node=%lu+%lu  %lu ms  NPS=%lu  core=%d\n", d, (unsigned long)nm, (unsigned long)nh, (unsigned long)ms, (unsigned long)nps, cores);
}
static uint8_t       chsBenchState = 0;               // 0 diam | 1 uji 1 core (menunggu 1 frame) | 2 uji 2 core
static unsigned long chsBenchReqMs = 0;
static bool          chsBnOk = false, chsBnTwo = false;
static uint32_t      chsBnTn[2] = {0, 0}, chsBnTms[2] = {0, 0};
static int           chsBnDs[2] = {0, 0};
static const char* const chsBenchFen[3] = {
  "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1" };
static void chsBenchSetFen(const char* fen) {
  memset(chsBd, 0, 64); int r = 7, f = 0; const char* p = fen;
  for (; *p && *p != ' '; p++) {
    if (*p == '/') { r--; f = 0; } else if (*p >= '1' && *p <= '8') f += *p - '0';
    else { const char* k = "pnbrqk"; char lc = (char)(*p | 32); int t = (int)(strchr(k, lc) - k) + 1; chsBd[r * 8 + f] = (*p >= 'a') ? -t : t; f++; }
  }
  if (*p) p++;
  chsWhite = (*p == 'w'); if (*p) p++; if (*p) p++;
  chsCastle = 0; for (; *p && *p != ' '; p++) { if (*p == 'K') chsCastle |= CHS_WK; if (*p == 'Q') chsCastle |= CHS_WQ; if (*p == 'k') chsCastle |= CHS_BK; if (*p == 'q') chsCastle |= CHS_BQ; }
  chsEp = -1; chsHalf = 0;
}
static void chsBenchMode(int m) {                     // satu mode (m=0: 1 core, m=1: 2 core) pada 3 posisi
  if (!chsAiEnsure()) return;
  uiBoostKick();
  CHS_SAVE_ST;
  int svHist = chsAiHistN; chsAiHistN = 0;
#if CHS_SMP
  bool svOn = chsSmpOn; chsSmpOn = (m == 1);
#endif
  uint32_t tn = 0, tms = 0; int ds = 0;
  for (int i = 0; i < 3; i++) {
    chsBenchSetFen(chsBenchFen[i]);
    chsAiTTClear();
    chsStNm = 0; chsStNh = 0; chsStMs = 0; chsStD = 0; chsStCores = 1;
    chsAiChoose(14, CHS_BENCH_MS, 0, 0, 0);
    tn += chsStNm + chsStNh; tms += chsStMs; ds += chsStD;
    Serial.printf("[NYX-BENCH] pos%d %s: d=%d  node=%lu+%lu  %lu ms  NPS=%lu\n", i + 1, m ? "2 core" : "1 core", chsStD, (unsigned long)chsStNm, (unsigned long)chsStNh,
                  (unsigned long)chsStMs, (unsigned long)(chsStMs ? ((uint64_t)chsStNm + chsStNh) * 1000ULL / chsStMs : 0));
    if (m == 1 && chsStCores == 2) chsBnTwo = true;
  }
  chsAiTTClear();
#if CHS_SMP
  chsSmpOn = svOn;
#endif
  chsAiHistN = svHist;
  CHS_REST_ST;
  chsBnTn[m] = tn; chsBnTms[m] = tms; chsBnDs[m] = ds;
  Serial.printf("[NYX-BENCH] %s: node=%lu  %lu ms  NPS=%lu  depth rata2=%.1f\n", m ? "2 core" : "1 core", (unsigned long)tn, (unsigned long)tms,
                (unsigned long)(tms ? (uint64_t)tn * 1000ULL / tms : 0), (double)ds / 3.0);
}
static void chsBenchPoll() {                          // dipanggil chessLoopPoll saat layar Jaringan terbuka
  if (!chsBenchState) return;
  if ((unsigned long)(millis() - chsBenchReqMs) < 250UL) return;       // beri 1 frame utk menggambar "Menguji..."
  if (chsBenchState == 1) { chsBnTwo = false; chsBenchMode(0); chsBenchState = 2; chsBenchReqMs = millis(); needRedraw = true; return; }
  chsBenchMode(1); chsBenchState = 0; chsBnOk = true; needRedraw = true;
  uint32_t a = chsBnTms[0] ? (uint32_t)((uint64_t)chsBnTn[0] * 1000ULL / chsBnTms[0]) : 0, b = chsBnTms[1] ? (uint32_t)((uint64_t)chsBnTn[1] * 1000ULL / chsBnTms[1]) : 0;
  Serial.printf("[NYX-BENCH] percepatan NPS = x%.2f\n", a ? (double)b / (double)a : 0.0);
}
// --- uji "waktu mencapai kedalaman D" (lebih adil daripada NPS: tak terpengaruh pembulatan kedalaman) ---
#define CHS_DEP_TARGET 7
#define CHS_DEP_CAP_MS 12000
static uint8_t       chsDepState = 0;                 // 0 diam | 1..6 = langkah (mode*3 + posisi) + 1; mode 0 = 1 core, 1 = 2 core
static unsigned long chsDepReqMs = 0;
static bool          chsDpOk = false;
static uint32_t      chsDpMs[2][3], chsDpNn[2][3];
static uint8_t       chsDpD[2][3];
static void chsDepthStep(int k) {
  int m = k / 3, i = k % 3;
  if (!chsAiEnsure()) return;
  uiBoostKick();
  CHS_SAVE_ST;
  int svHist = chsAiHistN; chsAiHistN = 0;
#if CHS_SMP
  bool svOn = chsSmpOn; chsSmpOn = (m == 1);
#endif
  chsBenchSetFen(chsBenchFen[i]);
  chsAiTTClear();
  chsStNm = 0; chsStNh = 0; chsStMs = 0; chsStD = 0; chsStCores = 1;
  chsAiChoose(CHS_DEP_TARGET, CHS_DEP_CAP_MS, 0, 0, 0);
  chsAiTTClear();
#if CHS_SMP
  chsSmpOn = svOn;
#endif
  chsAiHistN = svHist;
  CHS_REST_ST;
  chsDpMs[m][i] = chsStMs; chsDpNn[m][i] = chsStNm + chsStNh; chsDpD[m][i] = (uint8_t)chsStD;
  Serial.printf("[NYX-DEPTH] pos%d %s: d=%d  node=%lu+%lu  %lu ms  NPS=%lu  core=%d\n", i + 1, m ? "2 core" : "1 core", chsStD, (unsigned long)chsStNm, (unsigned long)chsStNh,
                (unsigned long)chsStMs, (unsigned long)(chsStMs ? ((uint64_t)chsStNm + chsStNh) * 1000ULL / chsStMs : 0), chsStCores);
}
static void chsDepthPoll() {
  if (!chsDepState) return;
  if ((unsigned long)(millis() - chsDepReqMs) < 250UL) return;       // beri 1 frame utk menggambar "Menguji..."
  chsDepthStep(chsDepState - 1);
  if (chsDepState >= 6) {
    chsDepState = 0; chsDpOk = true;
    uint32_t a = chsDpMs[0][0] + chsDpMs[0][1] + chsDpMs[0][2], b = chsDpMs[1][0] + chsDpMs[1][1] + chsDpMs[1][2];
    Serial.printf("[NYX-DEPTH] total waktu: 1 core %lu ms, 2 core %lu ms, lebih cepat x%.2f\n", (unsigned long)a, (unsigned long)b, b ? (double)a / (double)b : 0.0);
  } else { chsDepState++; chsDepReqMs = millis(); }
  needRedraw = true;
}
static void chsBenchPollIf() {
  if (curScreen() != SCR_CHESS || chsScr != 3) return;
  if (chsBenchState) chsBenchPoll(); else if (chsDepState) chsDepthPoll();
}

static uint8_t chsVw = 0;                 // 0 diagram, 1 peta bobot, 2 petak, 3 NPS
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

static void chsVwClose() { chsBenchState = 0; chsDepState = 0; if (chsVwInit) { CHS_AI_FREE(chsVwInit); chsVwInit = nullptr; } }
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
static void chsVwTabRect(int i, int* x, int* y, int* w, int* h) {     // i 0..3 = tab (Jaringan, Bobot, Petak, NPS), 4 = chip Selisih
  chsVwGeom(); const ChsVwG& g = chsg; int chip = 54, gap = 4; int tw = (g.aw - chip - 5 * gap) / 4;
  *y = g.ay; *h = 18;
  if (i < 4) { *x = g.ax + i * (tw + gap); *w = tw; } else { *x = g.ax + g.aw - chip; *w = chip; }
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

// --- tab 3: NPS (kecepatan mesin: 1 core vs 2 core) ---
static void chsFmtK(char* o, size_t n, uint32_t v) {
  if (v >= 10000) snprintf(o, n, "%u.%uk", (unsigned)(v / 1000), (unsigned)((v % 1000) / 100));
  else if (v >= 1000) snprintf(o, n, "%u.%02uk", (unsigned)(v / 1000), (unsigned)((v % 1000) / 10));
  else snprintf(o, n, "%u", (unsigned)v);
}
static void chsVwNpsBtn(int i, int* x, int* y, int* w, int* h) {     // 0 = Uji NPS, 1 = Uji kedalaman, 2 = saklar core ke-2
  chsVwGeom(); const ChsVwG& g = chsg; int gap = 4; int bw = (g.aw - 2 * gap) / 3;
  *y = g.fy - 26; *h = 22; *w = bw; *x = g.ax + i * (bw + gap);
}
static void chsFmtSec(char* o, size_t n, uint32_t ms) { snprintf(o, n, "%u.%u", (unsigned)(ms / 1000), (unsigned)((ms % 1000) / 100)); }
static void chsVwDrawNps(LGFX_Sprite& s) {
  chsVwGeom(); const ChsVwG& g = chsg;
  char b[96], ln[96], k1[16], k2[16], k3[16];
  int x = g.ax + 4, y = g.by + 4, mc = g.aw / 6 - 1;
  bool two = false;
#if CHS_SMP
  two = chsSmpOn;
#endif
  snprintf(b, sizeof(b), "Mesin: %s", two ? "2 core (Lazy SMP)" : "1 core");
  s.setTextColor(two ? (uint16_t)T().accent : (uint16_t)T().subtext); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 12;
  if (chsLsMs) {                                       // hanya pencarian >= 100 ms (yg lebih singkat: NPS-nya tak bermakna)
    uint32_t nps = (uint32_t)(((uint64_t)chsLsNm + chsLsNh) * 1000ULL / chsLsMs);
    chsFmtK(k1, sizeof(k1), nps);
    snprintf(b, sizeof(b), "Terakhir: d%d  %u ms  NPS %s", chsLsD, (unsigned)chsLsMs, k1);
    s.setTextColor(T().text); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
    chsFmtK(k1, sizeof(k1), chsLsNm); chsFmtK(k2, sizeof(k2), chsLsNh);
    if (chsLsCores == 2) snprintf(b, sizeof(b), "node %s utama + %s core 2", k1, k2); else snprintf(b, sizeof(b), "node %s (1 core)", k1);
    s.setTextColor(T().subtext); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 14;
  } else {
    s.setTextColor(T().subtext); s.setCursor(x, y); chsClip("Terakhir: belum ada (main lawan AI / uji)", ln, mc); s.print(ln); y += 14;
  }
  if (chsBenchState) {
    s.setTextColor(T().accent); s.setCursor(x, y);
    chsClip(chsBenchState == 1 ? "Menguji 1 core... jangan sentuh layar" : "Menguji 2 core... jangan sentuh layar", ln, mc); s.print(ln); y += 10;
  } else if (chsBnOk) {
    uint32_t a = chsBnTms[0] ? (uint32_t)((uint64_t)chsBnTn[0] * 1000ULL / chsBnTms[0]) : 0, c = chsBnTms[1] ? (uint32_t)((uint64_t)chsBnTn[1] * 1000ULL / chsBnTms[1]) : 0;
    chsFmtK(k1, sizeof(k1), a); chsFmtK(k2, sizeof(k2), c);
    snprintf(b, sizeof(b), "1 core: %s n/s  (d %.1f)", k1, (double)chsBnDs[0] / 3.0);
    s.setTextColor(T().text); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
    snprintf(b, sizeof(b), "2 core: %s n/s  (d %.1f)", k2, (double)chsBnDs[1] / 3.0);
    s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
    if (chsBnTwo && a) { snprintf(b, sizeof(b), "Percepatan NPS: x%.2f", (double)c / (double)a); s.setTextColor(T().good); }
    else { snprintf(b, sizeof(b), "Core ke-2 tidak berjalan"); s.setTextColor(T().danger); }
    s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
  }
  y += 4;
  if (chsDepState) {
    s.setTextColor(T().accent); s.setCursor(x, y);
    snprintf(b, sizeof(b), "Uji D%d %d/6 (%s)... jangan sentuh", CHS_DEP_TARGET, (int)chsDepState, chsDepState <= 3 ? "1 core" : "2 core"); chsClip(b, ln, mc); s.print(ln);
  } else if (chsDpOk) {
    char t1[3][12], t2[3][12]; uint32_t sa = 0, sb = 0; bool capped = false;
    for (int i = 0; i < 3; i++) { chsFmtSec(t1[i], 12, chsDpMs[0][i]); chsFmtSec(t2[i], 12, chsDpMs[1][i]); sa += chsDpMs[0][i]; sb += chsDpMs[1][i]; if (chsDpD[0][i] < CHS_DEP_TARGET || chsDpD[1][i] < CHS_DEP_TARGET) capped = true; }
    snprintf(b, sizeof(b), "D%d 1c: %s/%s/%s dtk", CHS_DEP_TARGET, t1[0], t1[1], t1[2]);
    s.setTextColor(T().text); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
    snprintf(b, sizeof(b), "D%d 2c: %s/%s/%s dtk", CHS_DEP_TARGET, t2[0], t2[1], t2[2]);
    s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln); y += 10;
    double r = sb ? (double)sa / (double)sb : 0.0;
    snprintf(b, sizeof(b), "Waktu: %s x%.2f%s", r > 1.05 ? "lebih cepat" : (r < 0.95 ? "LEBIH LAMBAT" : "sama"), r, capped ? " *batas" : "");
    s.setTextColor(r > 1.05 ? (uint16_t)T().good : (r < 0.95 ? (uint16_t)T().danger : (uint16_t)T().text)); s.setCursor(x, y); chsClip(b, ln, mc); s.print(ln);
  }
  for (int i = 0; i < 3; i++) {
    int bx, by, bw, bh; chsVwNpsBtn(i, &bx, &by, &bw, &bh);
    bool busy = chsBenchState || chsDepState;
    if (i < 2) {
      iosRR(s, bx, by, bw, bh, 7, busy ? (uint16_t)T().surface2 : (uint16_t)T().accent);
      s.setTextColor(busy ? (uint16_t)T().subtext : (uint16_t)T().bg);
      if (i == 0) chsPrintC(s, bx, bw, by + 7, "Uji NPS"); else { snprintf(b, sizeof(b), "Uji D%d", CHS_DEP_TARGET); chsPrintC(s, bx, bw, by + 7, b); }
    } else {
      iosRR(s, bx, by, bw, bh, 7, T().surface2);
      s.setTextColor(two ? (uint16_t)T().accent : (uint16_t)T().subtext); chsPrintC(s, bx, bw, by + 7, two ? "Core2: ON" : "Core2: OFF");
    }
  }
  snprintf(b, sizeof(b), "NPS = node per detik (utama + core 2)");
  s.setTextColor(T().subtext); s.setCursor(g.ax + 2, g.fy); chsClip(b, ln, g.aw / 6); s.print(ln);
}
static void chsVwTouchNps(int x, int y) {
  for (int i = 0; i < 3; i++) {
    int bx, by, bw, bh; chsVwNpsBtn(i, &bx, &by, &bw, &bh);
    if (x < bx || x >= bx + bw || y < by || y >= by + bh + 4) continue;
    if (chsBenchState || chsDepState) return;
    if (i < 2) {
      if (chsActive || chsVsAI || chsGem) { showToast("Keluar dari partai dulu", 1800); return; }
      if (!chsAiEnsure()) { showToast("PSRAM tidak cukup utk AI", 2200); return; }
      if (i == 0) { chsBenchState = 1; chsBenchReqMs = millis(); }
      else { chsDpOk = false; chsDepState = 1; chsDepReqMs = millis(); }
      needRedraw = true;
    } else {
#if CHS_SMP
      chsSmpOn = !chsSmpOn; chsPrefsSaveCfg(); showToast(chsSmpOn ? "Core ke-2 aktif" : "1 core saja", 1400); needRedraw = true;
#endif
    }
    return;
  }
}

static void chsDrawNet(LGFX_Sprite& s) {
  chsVwGeom(); const ChsVwG& g = chsg;
  static const char* const tn[4] = { "Jaringan", "Bobot", "Petak", "NPS" };
  s.setTextSize(1);
  for (int i = 0; i < 4; i++) {
    int x, y, w, h; chsVwTabRect(i, &x, &y, &w, &h); bool on = (chsVw == i);
    iosRR(s, x, y, w, h, 7, on ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(on ? (uint16_t)T().bg : (uint16_t)T().text); chsPrintC(s, x, w, y + 5, tn[i]);
  }
  { int x, y, w, h; chsVwTabRect(4, &x, &y, &w, &h);
    iosRR(s, x, y, w, h, 7, chsVwDelta ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(chsVwDelta ? (uint16_t)T().bg : (chsVwInit ? (uint16_t)T().text : (uint16_t)T().subtext)); chsPrintC(s, x, w, y + 5, "Selisih"); }
  if (chsVw == 3) { chsVwDrawNps(s); return; }
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
  for (int i = 0; i < 5; i++) {
    int tx, ty, tw, th; chsVwTabRect(i, &tx, &ty, &tw, &th);
    if (x >= tx && x < tx + tw && y >= ty && y < ty + th + 4) {
      if (i < 4) chsVw = (uint8_t)i;
      else if (chsVw == 3) { }                       // chip Selisih tak berlaku di tab NPS
      else if (chsVwInit) chsVwDelta = !chsVwDelta;
      else showToast("RAM tidak cukup utk selisih", 1800);
      chsVwSel = -1; chsVwCell = -1; chsVwInfo[0] = 0; needRedraw = true; return;
    }
  }
  if (chsVw == 3) { chsVwTouchNps(x, y); return; }
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
  if (y >= 192 && y <= 210 && x >= SCR_W - 8 - 58 - 6 - 62 && x < SCR_W - 8 - 62) { chsPress(10); chsStartGem(); return; }   // chip Gemini
  if (y >= 166 && y <= 190) {
    int gw = (SCR_W - 16 - 12) / 3;
    if (x >= 8 && x < 8 + gw) { chsPress(6); chsStartAi(); }
    else if (x >= 8 + gw + 6 && x < 8 + 2 * gw + 6) { chsPress(7); chsAiLevel = (uint8_t)((chsAiLevel + 1) % 5); chsPrefsSaveCfg(); needRedraw = true; }
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
    if (chsGem) { chsGemBtn0(); return; }
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
    chsGemStop();
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
