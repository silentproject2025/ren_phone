// =====================================================================
// call_renphone.ino -- app "Telepon": panggilan suara lewat WiFi
//
//   MODE 1  SIP   : akun SIP gratis (mis. sip.linphone.org) -> telepon ke
//                   alamat SIP lain (user@domain). Daftar (REGISTER) sekali,
//                   lalu bisa menelepon & menerima panggilan.
//   MODE 2  LAN   : langsung antar HP NyxOS di WiFi yg sama, TANPA server &
//                   tanpa internet. Dial IP HP lawan, atau tekan "Cari" utk
//                   menemukan HP NyxOS lain (broadcast SIP OPTIONS).
//
//   Dua-duanya memakai satu SIP UA yang sama (UDP 5060) + RTP G.711
//   (PCMU/PCMA, 8 kHz, paket 20 ms). Lihat call-sip/README.md.
//
// ALUR AUDIO (sesuai perangkat keras proyek ini: TIDAK ADA speaker)
//   mic INMP441 (I2S, 16 kHz) --turun jadi 8 kHz--> G.711 --RTP--> lawan
//   lawan --RTP--> G.711 -> PCM16 8 kHz -> header WAV + PCM lewat jalur
//   audio-eksternal musExt* (yg dipakai pemutar AVI) -> UART -> ESP32-CAM
//   (mode WAV: resample ke 44,1 kHz) -> A2DP -> TWS.
//   OPSIONAL: kalau output Musik = "SPK" (Musik > cari TWS > Output: SPK), suara lawan
//   langsung ke speaker MAX98357A lewat I2S (8 kHz mono, tanpa CAM/TWS; lihat musspk.h).
//   SPK_FFAT_V1: speaker dipakai kalau output Musik = SPK ATAU TWS tidak tersambung -> telepon TIDAK lagi
//   bergantung pada TWS (lihat callSpkSelected() di musicbt_renphone.ino, macro CALL_SPK_FALLBACK).
//   TWS tersambung + output = BT tetap lewat TWS. Tanpa modul MAX98357A & tanpa TWS tidak ada suara masuk
//   (mic tetap jalan).
//
// ARSITEKTUR
//   Satu task FreeRTOS permanen "callTask" (stack PSRAM, core 1, prioritas 2)
//   mengurus SELURUH jaringan + audio. UI (loop utama) HANYA membaca status
//   volatile & mengirim permintaan lewat callReq (satu penulis = tdk ada race
//   di state SIP). Task baru dibuat lazy dari callLoopPoll() (dipanggil tiap
//   loop()) begitu WiFi tersambung & ada konfigurasi / siaga aktif.
//
// Aturan arduino-cli di proyek ini: fungsi di file ini TIDAK menerima/
// mengembalikan tipe struct buatan sendiri, dan tidak ada argumen default di
// DEFINISI fungsi (prototype otomatis). Semua tipe diletakkan di puncak file.
// =====================================================================
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <mbedtls/md.h>
#include "rplink.h"

// ---- konstanta ----
#define CALL_SIP_PORT     5060
#define CALL_RTP_PORT     40000
#define CALL_SAMPLES      160          // 8 kHz x 20 ms
#define CALL_REG_EXPIRES  120   // pendek: alamat publik di CGNAT/hotspot sering berubah, daftar ulang lebih sering
#define CALL_UA           "NyxOS/1.0"
#define CALL_STUN_HOST    "stun.l.google.com"
#define CALL_STUN_PORT    19302
#define CALL_SCAN_MAX     4
#define CALL_REC_MAX      5
#define CALL_BACKLOG_HI   420          // ms antrean suara di CAM -> mulai buang paket
#define CALL_BACKLOG_LO   240          // ms -> berhenti buang
#define CALL_PREFILL_BYTES 8192        // prefill CAM selama panggilan (~46 ms @44,1k stereo)
#define CALL_JB           6            // slot jitter buffer (paket)
#define CALL_RING_MS      45000        // dering masuk maks
#define CALL_DIAL_MS      60000        // menunggu diangkat maks

// perintah ke ko-prosesor CAM (lihat rplink.h; CAM lama mengabaikan jenis frame yg tak dikenal)
#ifndef RP_PREFILL
#define RP_PREFILL 'Y'
#endif

// ---- fase panggilan ----
#define CPH_IDLE      0
#define CPH_DIAL      1    // INVITE terkirim, belum ada respon
#define CPH_RINGBACK  2    // lawan berdering
#define CPH_INCOMING  3    // ada panggilan masuk, menunggu user
#define CPH_ACTIVE    4
#define CPH_ENDING    5    // BYE/CANCEL terkirim, menunggu balasan

// ---- permintaan UI -> task ----
#define CRQ_NONE   0
#define CRQ_DIAL   1
#define CRQ_ANSWER 2
#define CRQ_REJECT 3
#define CRQ_HANGUP 4
#define CRQ_REREG  5
#define CRQ_SCAN   6

// ---------------------------------------------------------------------
// STATE BERSAMA (UI <-> task). Teks dilindungi mutex callMx.
// ---------------------------------------------------------------------
static SemaphoreHandle_t callMx = nullptr;
static TaskHandle_t      callTask = nullptr;
static StackType_t*      callStack = nullptr;
static StaticTask_t      callTCB;

static String callHostS = "", callUserS = "", callPassS = "", callNameS = "";
static String callTargetS = "", callLanIpS = "";
static String callTurnHostS = "", callTurnUserS = "", callTurnPassS = "";   // TURN (relay RTP utk CGNAT)
static int    callTurnPortV = 3478;
static bool   callStandby = true;
#ifndef CALL_TCP_DEFAULT
#define CALL_TCP_DEFAULT 1
#endif
static bool   callTcpPref = (CALL_TCP_DEFAULT != 0);   // SIP ke server lewat TCP (false = UDP); sip.cfg: transport=udp|tcp
static bool   callCfgLoaded = false;
static volatile uint32_t callCfgGen = 0;     // naik tiap konfigurasi disimpan -> task daftar ulang

static volatile uint8_t  callPhase = CPH_IDLE;
static volatile uint8_t  callRegState = 0;   // 0 mati, 1 mendaftar, 2 terdaftar, 3 gagal
static volatile bool     callDirty = false;
static volatile bool     callIncomingNew = false;
static volatile bool     callEndedNew = false;
static char              callDbgRx[40] = "";            // SIP terakhir yg diterima (utk diagnosa)
static volatile uint32_t callDbgRxMs = 0;
static volatile bool     callMuted = false;
static volatile bool     callIsLan = false;
static volatile bool     callAudioOut = false;      // jalur ke TWS hidup
static volatile uint32_t callStartMs = 0;           // saat ACTIVE
static volatile uint8_t  callReq = CRQ_NONE;
static char              callReqArg[72] = "";
static char              callRegMsg[40] = "";
static char              callPeer[44] = "";
static char              callStatus[44] = "";
static volatile uint8_t  callMicLvl = 0;            // 0..100 utk meter kecil di layar
static volatile uint8_t  callRxLvl  = 0;            // CALL_RXLEVEL_V1: 0..100 level suara lawan bicara
static uint8_t  callShownMic = 0, callShownRx = 0;  // level yg terakhir digambar (utk memutuskan redraw)
static uint32_t callLvlDrawMs = 0;
static volatile int      callBacklogMs = 0;         // diagnosa: antrean suara di CAM

// ---- DIAGNOSA CRASH: penanda langkah terakhir (RTC, selamat dari restart) ----
static RTC_NOINIT_ATTR uint32_t callCrumbT;     // langkah terakhir di callTask
static RTC_NOINIT_ATTR uint32_t callCrumbU;     // langkah terakhir di UI thread
static inline void callMarkT(uint32_t n) { callCrumbT = 0xC4110000u | n; }
static inline void callMarkU(uint32_t n) { callCrumbU = 0xC4110000u | n; }
static bool     callBootCap = false;
static uint32_t callBootT = 0, callBootU = 0;
static char     callBootMsg[56] = "";
static bool     callBootShown = false;
static void callCaptureBoot() {                 // panggil SEBELUM ada callMark pertama
  if (callBootCap) return;
  callBootCap = true;
  esp_reset_reason_t rr = esp_reset_reason();
  const char* why = nullptr;
  if (rr == ESP_RST_PANIC)         why = "CRASH";
  else if (rr == ESP_RST_BROWNOUT) why = "BROWNOUT";
  else if (rr == ESP_RST_TASK_WDT) why = "WDT-task";
  else if (rr == ESP_RST_INT_WDT)  why = "WDT-int";
  else if (rr == ESP_RST_WDT)      why = "WDT";
  if (!why) return;
  callBootT = ((callCrumbT & 0xFFFF0000u) == 0xC4110000u) ? (callCrumbT & 0xFFFFu) : 0;
  callBootU = ((callCrumbU & 0xFFFF0000u) == 0xC4110000u) ? (callCrumbU & 0xFFFFu) : 0;
  snprintf(callBootMsg, sizeof(callBootMsg), "Reset: %s  task=%u ui=%u", why, (unsigned)callBootT, (unsigned)callBootU);
}

static char     callScanName[CALL_SCAN_MAX][24];
static char     callScanIp[CALL_SCAN_MAX][16];
static volatile int  callScanCount = 0;
static volatile bool callScanBusy = false;

static char     callRecent[CALL_REC_MAX][44];
static int      callRecentCount = 0;

// UI
static int   callPage = 0;          // 0 telepon, 1 pengaturan, 2 pengaturan TURN
static int   callTab = 0;           // 0 SIP, 1 LAN
static int   callCfgEdit = -1;
static bool  callToastFlag = false;
static char  callToastBuf[48] = "";
static uint32_t callLastRingVib = 0;
static uint32_t callLastTick = 0;
static uint32_t callIdleRedrawMs = 0;

// ---------------------------------------------------------------------
// CODEC G.711
// ---------------------------------------------------------------------
static uint8_t callLin2Ulaw(int16_t pcm) {
  int p = pcm;
  int sign = (p >> 8) & 0x80;
  if (sign) p = -p;
  if (p > 32635) p = 32635;
  p += 0x84;
  int exponent = 7;
  for (int m = 0x4000; (p & m) == 0 && exponent > 0; m >>= 1) exponent--;
  int mant = (p >> (exponent + 3)) & 0x0F;
  return (uint8_t)(~(sign | (exponent << 4) | mant));
}

static int16_t callUlaw2Lin(uint8_t u) {
  u = (uint8_t)~u;
  int sign = u & 0x80;
  int exponent = (u >> 4) & 0x07;
  int mant = u & 0x0F;
  int s = (((mant << 3) + 0x84) << exponent) - 0x84;
  return (int16_t)(sign ? -s : s);
}

static uint8_t callLin2Alaw(int16_t pcm) {
  static const int segEnd[8] = { 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF };
  int p = pcm >> 3;
  int mask;
  if (p >= 0) mask = 0xD5; else { mask = 0x55; p = -p - 1; }
  int seg = 0;
  while (seg < 8 && p > segEnd[seg]) seg++;
  if (seg >= 8) return (uint8_t)(0x7F ^ mask);
  int aval = seg << 4;
  if (seg < 2) aval |= (p >> 1) & 0x0F; else aval |= (p >> seg) & 0x0F;
  return (uint8_t)(aval ^ mask);
}

static int16_t callAlaw2Lin(uint8_t a) {
  a ^= 0x55;
  int t = (a & 0x0F) << 4;
  int seg = (a & 0x70) >> 4;
  if (seg == 0) t += 8;
  else { t += 0x108; if (seg > 1) t <<= (seg - 1); }
  return (int16_t)((a & 0x80) ? t : -t);
}

// ---------------------------------------------------------------------
// HELPER UMUM
// ---------------------------------------------------------------------
static void callLock()   { if (callMx) xSemaphoreTake(callMx, portMAX_DELAY); }
static void callUnlock() { if (callMx) xSemaphoreGive(callMx); }

static void callCopy(char* dst, int cap, const char* src) {
  if (cap <= 0) return;
  int n = 0;
  if (src) { while (src[n] && n < cap - 1) { dst[n] = src[n]; n++; } }
  dst[n] = 0;
}

// riwayat diagnosa: 4 kejadian terakhir (paket masuk + keputusan atas INVITE)
static char callDbgHist[6][40];
static void callDbgPush(const char* m) {
  for (int i = 5; i > 0; i--) memcpy(callDbgHist[i], callDbgHist[i - 1], sizeof(callDbgHist[i]));
  callCopy(callDbgHist[0], sizeof(callDbgHist[0]), m);
  Serial.printf("[CALL] %s\n", m);
}

// teks status bersama (dipanggil dari task)
static void callSetStatus(const char* s) {
  callLock(); callCopy(callStatus, sizeof(callStatus), s); callUnlock();
  callDirty = true;
}
static void callSetPeer(const char* s) {
  callLock(); callCopy(callPeer, sizeof(callPeer), s); callUnlock();
  callDirty = true;
}
static void callSetRegMsg(const char* s) {
  callLock(); callCopy(callRegMsg, sizeof(callRegMsg), s); callUnlock();
  callDirty = true;
}

static void callToast(const char* m) {
  callCopy(callToastBuf, sizeof(callToastBuf), m);
  callToastFlag = true;
  callDirty = true;
}

static void callLocalIpStr(char* out, int cap) {
  IPAddress ip = WiFi.localIP();
  snprintf(out, cap, "%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
}

static bool callIsPrivateIp(const char* s) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
  if (a == 10) return true;
  if (a == 192 && b == 168) return true;
  if (a == 172 && b >= 16 && b <= 31) return true;
  if (a == 169 && b == 254) return true;
  if (a == 127) return true;
  return false;
}

static bool callLooksLikeIp(const char* s) {
  int dots = 0;
  for (const char* p = s; *p && *p != ':' && *p != '/'; p++) {
    if (*p == '.') dots++;
    else if (*p < '0' || *p > '9') return false;
  }
  return dots == 3;
}

static void callRandHex(char* out, int n) {
  static const char H[] = "0123456789abcdef";
  for (int i = 0; i < n; i++) out[i] = H[esp_random() & 15];
  out[n] = 0;
}

// ---------------------------------------------------------------------
// PARSER SIP (teks). Semua fungsi bekerja pada string C ber-terminator NUL.
// ---------------------------------------------------------------------
static bool callEqNoCase(const char* a, const char* b, int n) {
  for (int i = 0; i < n; i++) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return false;
    if (!x) return true;
  }
  return true;
}

// cari header ke-nth (0-based). shortName boleh "" . return true kalau ketemu.
static bool sipHdr(const char* msg, const char* name, const char* shortName, int nth, char* out, int cap) {
  out[0] = 0;
  int nl = (int)strlen(name);
  int sl = (int)strlen(shortName);
  const char* p = strstr(msg, "\r\n");
  if (!p) return false;
  p += 2;
  int seen = 0;
  while (*p) {
    const char* e = strstr(p, "\r\n");
    if (!e || e == p) break;                       // baris kosong = akhir header
    const char* colon = (const char*)memchr(p, ':', (size_t)(e - p));
    if (colon) {
      int tl = (int)(colon - p);
      while (tl > 0 && (p[tl - 1] == ' ' || p[tl - 1] == '\t')) tl--;
      bool match = (tl == nl && callEqNoCase(p, name, nl)) || (sl > 0 && tl == sl && callEqNoCase(p, shortName, sl));
      if (match) {
        if (seen == nth) {
          const char* v = colon + 1;
          while (v < e && (*v == ' ' || *v == '\t')) v++;
          int vl = (int)(e - v);
          if (vl > cap - 1) vl = cap - 1;
          memcpy(out, v, (size_t)vl); out[vl] = 0;
          return true;
        }
        seen++;
      }
    }
    p = e + 2;
  }
  return false;
}

static const char* sipBody(const char* msg) {
  const char* b = strstr(msg, "\r\n\r\n");
  return b ? b + 4 : "";
}

static int sipStatus(const char* msg) {
  if (strncmp(msg, "SIP/2.0 ", 8) != 0) return 0;
  return atoi(msg + 8);
}

// method dari request / dari CSeq response
static void sipMethodOf(const char* msg, char* out, int cap) {
  int n = 0;
  while (msg[n] && msg[n] != ' ' && n < cap - 1) { out[n] = msg[n]; n++; }
  out[n] = 0;
}

// ambil nilai ;key=xxx dari sebuah header (hingga ; , > spasi)
static bool sipParam(const char* hdr, const char* key, char* out, int cap) {
  out[0] = 0;
  int kl = (int)strlen(key);
  const char* p = hdr;
  while ((p = strchr(p, ';')) != nullptr) {
    p++;
    while (*p == ' ') p++;
    if (callEqNoCase(p, key, kl) && p[kl] == '=') {
      p += kl + 1;
      int n = 0;
      while (p[n] && p[n] != ';' && p[n] != ',' && p[n] != '>' && p[n] != ' ' && n < cap - 1) { out[n] = p[n]; n++; }
      out[n] = 0;
      return true;
    }
  }
  return false;
}

// "sip:user@host:port;x" atau <...> atau "Nama" <...> -> user, host, port (port 0 = tdk ada)
static bool sipUri(const char* s, char* user, int ucap, char* host, int hcap, int* port) {
  user[0] = 0; host[0] = 0; *port = 0;
  const char* p = strstr(s, "sip:");
  if (!p) p = strstr(s, "SIP:");
  if (!p) p = strstr(s, "sips:");
  if (!p) return false;
  p = strchr(p, ':') + 1;
  const char* at = nullptr;
  const char* q = p;
  while (*q && *q != '>' && *q != ';' && *q != ' ' && *q != ',') { if (*q == '@') at = q; q++; }
  if (at) {
    int ul = (int)(at - p);
    if (ul > ucap - 1) ul = ucap - 1;
    memcpy(user, p, (size_t)ul); user[ul] = 0;
    p = at + 1;
  }
  int n = 0;
  while (*p && *p != ':' && *p != ';' && *p != '>' && *p != '?' && *p != ' ' && *p != ',' && n < hcap - 1) host[n++] = *p++;
  host[n] = 0;
  if (*p == ':') *port = atoi(p + 1);
  return host[0] != 0;
}

// nama tampilan dari From/To: "Nama" <sip:..> atau Nama <sip:..>; kalau tdk ada -> user
static void sipDisplay(const char* hdr, char* out, int cap) {
  out[0] = 0;
  const char* lt = strchr(hdr, '<');
  if (lt && lt > hdr) {
    const char* a = hdr;
    while (a < lt && (*a == ' ' || *a == '"')) a++;
    const char* b = lt;
    while (b > a && (b[-1] == ' ' || b[-1] == '"')) b--;
    int n = (int)(b - a);
    if (n > cap - 1) n = cap - 1;
    if (n > 0) { memcpy(out, a, (size_t)n); out[n] = 0; return; }
  }
  char u[40], h[40]; int pt;
  if (sipUri(hdr, u, sizeof(u), h, sizeof(h), &pt)) callCopy(out, cap, u[0] ? u : h);
}

// semua Record-Route -> daftar dipisah koma. reverse=true utk sisi UAC (urutan dibalik)
static void sipCollectRR(const char* msg, bool reverse, char* out, int cap) {
  out[0] = 0;
  char ent[6][160]; int ne = 0;
  char line[480];
  for (int k = 0; k < 6; k++) {
    if (!sipHdr(msg, "Record-Route", "", k, line, sizeof(line))) break;
    const char* p = line;
    while (*p && ne < 6) {
      const char* lt = strchr(p, '<');
      if (!lt) break;
      const char* gt = strchr(lt, '>');
      if (!gt) break;
      int n = (int)(gt - lt + 1);
      if (n > 159) { callDbgPush("RR dipotong"); n = 159; }
      memcpy(ent[ne], lt, (size_t)n); ent[ne][n] = 0; ne++;
      p = gt + 1;
    }
  }
  int len = 0;
  for (int i = 0; i < ne; i++) {
    const char* e = ent[reverse ? ne - 1 - i : i];
    int el = (int)strlen(e);
    if (len + el + 2 >= cap) break;
    if (len) { out[len++] = ','; }
    memcpy(out + len, e, (size_t)el); len += el;
    out[len] = 0;
  }
}

// ---------------------------------------------------------------------
// DIGEST AUTH (MD5 & SHA-256)
// ---------------------------------------------------------------------
static void callHashHex(int alg, const char* in, char* hex) {
  unsigned char h[32];
  const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(alg == 0 ? MBEDTLS_MD_MD5 : MBEDTLS_MD_SHA256);
  int hl = (alg == 0) ? 16 : 32;
  if (!mi) { hex[0] = 0; return; }
  mbedtls_md(mi, (const unsigned char*)in, strlen(in), h);
  static const char H[] = "0123456789abcdef";
  for (int i = 0; i < hl; i++) { hex[i * 2] = H[h[i] >> 4]; hex[i * 2 + 1] = H[h[i] & 15]; }
  hex[hl * 2] = 0;
}

// nilai parameter dari header "Digest a=\"b\", c=d"
static bool callDigestParam(const char* hdr, const char* key, char* out, int cap) {
  out[0] = 0;
  int kl = (int)strlen(key);
  const char* p = hdr;
  while (*p) {
    while (*p == ' ' || *p == ',' || *p == '\t') p++;
    // lewati kata "Digest"
    if (callEqNoCase(p, "Digest", 6) && (p[6] == ' ')) { p += 7; continue; }
    const char* ks = p;
    while (*p && *p != '=' && *p != ',') p++;
    if (*p != '=') { if (!*p) break; continue; }
    int tl = (int)(p - ks);
    p++;
    char val[200]; int vn = 0;
    if (*p == '"') {
      p++;
      while (*p && *p != '"' && vn < (int)sizeof(val) - 1) val[vn++] = *p++;
      if (*p == '"') p++;
    } else {
      while (*p && *p != ',' && *p != ' ' && vn < (int)sizeof(val) - 1) val[vn++] = *p++;
    }
    val[vn] = 0;
    if (tl == kl && callEqNoCase(ks, key, kl)) { callCopy(out, cap, val); return true; }
  }
  return false;
}

// =====================================================================
//  TIPE (di puncak -- jangan dipakai di signature fungsi)
// =====================================================================
struct CallReqHdr {            // header penting dari sebuah request (utk membangun balasan)
  char via[640]; char from[170]; char to[170]; char callId[72];
  char cseq[40]; char rr[420]; char contact[200];
};

// ---------------------------------------------------------------------
// STATE MILIK TASK (hanya task yg menyentuh ini)
// ---------------------------------------------------------------------
#define SIP_BUF 4096
static char tHost[64] = "", tHostName[64] = "", tUser[40] = "", tPass[40] = "", tName[24] = "";
static int  tPort = CALL_SIP_PORT;
static int  tPortCfg = CALL_SIP_PORT;           // port dari konfigurasi (tPort bisa diganti hasil SRV)
static bool tPortExplicit = false;               // host ditulis dgn ":port" -> SRV dilewati
static char tProxyName[64] = "";                 // opsional: server yang dihubungi (outbound proxy); kosong = pakai domain
static uint32_t tCfgGen = 0xFFFFFFFFu;

static WiFiUDP sipUdp, rtpUdp;
static bool    sipOpen = false, rtpOpen = false;
static IPAddress srvIp;
static char    srvIpStr[16] = "";
static bool    srvOk = false;
static uint32_t srvAt = 0;
static char    lip[16] = "";
static char    natIp[16] = "";
static int     natPort = 0;

static char* sipRx = nullptr;
static char* sipTx = nullptr;
static char* sipRtx = nullptr;          // pesan terakhir utk retransmisi
static int   sipRtxLen = 0;
static char  sipRtxIp[16] = "";
static int   sipRtxPort = 0;
static uint8_t* rtpRx = nullptr;
static int32_t* micRaw = nullptr;
static int16_t* jbPcm = nullptr;        // CALL_JB x 320 sampel
static bool     callBufOk = false;

// registrasi
static char     regCallId[40] = "", regTag[12] = "";
static uint32_t regCseq = 0;
static bool     regPending = false, regAuthSent = false;
static uint32_t regSentAt = 0, regNextAt = 0, regKeepAt = 0, regBackoff = 500;
static uint8_t  regTries = 0;

// dialog panggilan
static CallReqHdr rq EXT_RAM_BSS_ATTR, urq EXT_RAM_BSS_ATTR;
static struct {
  bool uac, lan, gotProv, ackd, authTried;
  char callId[72];
  char localHdr[170], remoteHdr[170];
  char target[200], route[260], ltag[14], branch[28];
  char reqUri[110];
  char destIp[16]; int destPort;
  char peerIp[16]; int peerPort;
  uint32_t cseq, inviteCseq;
  char rtpIp[16]; int rtpPort; uint8_t pt; bool has101;
  char myRtpIp[16]; int myRtpPort;
  uint32_t t0, retxAt; uint8_t retxN;
  bool cancelSent, byeSent;
} dlg EXT_RAM_BSS_ATTR;

// media
static bool     aoOn = false, aoDrop = false, micOn = false, mediaOn = false;
static bool     aoSpk = false;          // true = audio keluar lewat speaker MAX98357A (bukan CAM/TWS)
static uint32_t aoSent = 0;
static uint8_t  toneMode = 0;           // 0 diam, 1 ringback, 2 dering masuk
static uint32_t toneT = 0, toneLastMs = 0;
static float    tonePh = 0.0f, toneEnv = 0.0f;
static IPAddress rtpDest;
static int      rtpDestPort = 0;
static bool     rtpLatched = false;
static uint16_t rtpSeqTx = 0; static uint32_t rtpTsTx = 0, rtpSsrc = 0; static bool rtpFirst = true;
static bool     rxInit = false; static uint16_t rxExpect = 0; static uint32_t rxGapAt = 0;
static int      rxLoss = 0;
static uint32_t lastRtpRx = 0, micYieldAt = 0, lastLvlMs = 0;
static uint32_t rxLvlMs = 0; static int32_t rxPkAcc = 0;      // CALL_RXLEVEL_V1
static int16_t  rxLast[CALL_SAMPLES];
static int      jbLen[CALL_JB]; static uint16_t jbSeq[CALL_JB]; static bool jbUsed[CALL_JB];
static float    micDcX = 0, micDcY = 0, micH[4] = {0, 0, 0, 0};
static int      micPhase = 0;
static float    micGain = 3.0f;
static uint32_t scanUntil = 0;
static char     authNonceLast[80] = ""; static uint32_t authNc = 0;
static uint32_t lastCfgGenSeen = 0;

// deklarasi maju fungsi di file lain
void wakeYieldMic(uint32_t holdMs);
void musExtPrepare();
bool musExtLinkUp();
// speaker MAX98357A (definisi di musicbt_renphone.ino): dipakai kalau output Musik = "SPK"
bool callSpkSelected();
bool callSpkBegin();
void callSpkWrite(const int16_t* pcm, int n);
void callSpkEnd();

// ---------------------------------------------------------------------
// AUDIO KELUAR -> CAM -> TWS (jalur musExt* milik pemutar AVI)
// ---------------------------------------------------------------------
static const char* callAudioErr(int rc) {
  switch (rc) {
    case 1: return RPTR("Bluetooth mati (nyalakan di Control Center)");
    case 2: return "Modul BT (ESP32-CAM) tdk terdeteksi";
    case 3: return RPTR("TWS belum tersambung (Musik > cari TWS)");
    case 4: return "RAM kurang utk audio";
    case 5: return RPTR("Audio belum siap (buka Telepon dulu)");
    case 9: return "Audio dipakai pemutar lain";
    case 10: return "Speaker gagal dibuka (cek wiring)";
    default: return RPTR("Audio keluar gagal");
  }
}

static int callAudioStart() {
  if (aoOn) return 0;
  if (callSpkSelected()) {                           // output = speaker: tanpa CAM/TWS, langsung I2S 8 kHz mono
    if (!callSpkBegin()) return 10;
    aoOn = true; aoSpk = true; aoDrop = false; aoSent = 0; callAudioOut = true; callBacklogMs = 0;
    return 0;
  }
  if (musExtActive()) return 9;                      // pemutar AVI sedang memakai jalur ini
  if (!musExtLinkUp()) return 5;                     // link CAM belum menyala: jangan inisialisasi dari callTask
  callMarkT(50);
  int rc = musExtBegin();
  callMarkT(51);
  if (rc != 0) return rc;
  uint8_t pf[2] = { (uint8_t)(CALL_PREFILL_BYTES & 0xFF), (uint8_t)(CALL_PREFILL_BYTES >> 8) };
  rpSend(Serial1, RP_PREFILL, pf, 2);                // CAM lama mengabaikan ini (default prefill 32 KB)
  callMarkT(52);
  uint8_t hdr[WAV_HEADER_SIZE];
  micWriteWavHeader(hdr, 0x7FFFF000u, 8000);         // header WAV 8 kHz mono -> CAM masuk mode WAV (tanpa Helix)
  musExtWrite(hdr, WAV_HEADER_SIZE);
  callMarkT(53);
  aoOn = true; aoDrop = false; aoSent = 0; callAudioOut = true; callBacklogMs = 0;
  return 0;
}

static void callAudioStop() {
  if (!aoOn) return;
  if (aoSpk) { aoSpk = false; aoOn = false; callAudioOut = false; callSpkEnd(); return; }
  aoOn = false; callAudioOut = false;
  musExtEnd();
  uint8_t pf[2] = { 0, 0 };                          // 0 = kembali ke prefill bawaan CAM
  rpSend(Serial1, RP_PREFILL, pf, 2);
}

// kirim n sampel PCM16 mono 8 kHz
static void callPcmOut(const int16_t* pcm, int n) {
  if (!aoOn || n <= 0) return;
  if (aoSpk) { callSpkWrite(pcm, n); aoSent += (uint32_t)n; return; }
  if (!musExtActive()) { aoOn = false; callAudioOut = false; callDirty = true; return; }   // CAM restart / stream putus
  uint32_t w = musExtWrite((const uint8_t*)pcm, (uint32_t)n * 2u);
  aoSent += w / 2u;
}

// pantau antrean suara di CAM (ms) -> buang paket kalau menumpuk (jaga latensi)
static void callBacklogCheck() {
  if (!aoOn || aoSpk) return;                        // jalur speaker: kelebihan sampel dibuang di spkLiveWrite
  uint32_t played = musExtClockMs();
  if (played == 0) return;                           // belum mulai diputar
  int backlog = (int)(aoSent / 8u) - (int)played;
  callBacklogMs = backlog;
  if (backlog > CALL_BACKLOG_HI) aoDrop = true;
  else if (backlog < CALL_BACKLOG_LO) aoDrop = false;
}

// nada dering / ringback buatan (sinus) lewat jalur yg sama
static void callToneStep(uint32_t now) {
  if (!aoOn || toneMode == 0) return;
  if (toneLastMs == 0 || now - toneLastMs > 400) toneLastMs = now;
  if (now - toneLastMs < 20) return;
  toneLastMs += 20;
  int16_t buf[CALL_SAMPLES];
  float freq = (toneMode == 1) ? 425.0f : 800.0f;
  for (int i = 0; i < CALL_SAMPLES; i++) {
    uint32_t t = (toneT + (uint32_t)(i / 8)) % 4000u;
    bool on;
    if (toneMode == 1) on = (t < 1000u);                                         // ringback: 1 dtk bunyi, 3 dtk diam
    else               on = (t < 400u) || (t >= 600u && t < 1000u);              // dering: dua bip pendek
    toneEnv += ((on ? 1.0f : 0.0f) - toneEnv) * 0.02f;
    tonePh += 6.2831853f * freq / 8000.0f;
    if (tonePh > 6.2831853f) tonePh -= 6.2831853f;
    buf[i] = (int16_t)(sinf(tonePh) * 4500.0f * toneEnv);
  }
  toneT += 20;
  callPcmOut(buf, CALL_SAMPLES);
}

// ---------------------------------------------------------------------
// MIKROFON (INMP441 via micI2S milik phone.ino)
// ---------------------------------------------------------------------
static bool callMicStart() {
  wakeYieldMic(4000);                                // jeda task wake word
  if (micI2SReady) return false;                     // dipegang fitur lain (Voice Memo, Mic Level, dikte AI)
  micEnsureI2S();
  micI2S.setTimeout(60);
  micDcX = 0; micDcY = 0; micPhase = 0;
  for (int i = 0; i < 4; i++) micH[i] = 0;
  micOn = true; micYieldAt = millis();
  return true;
}

static void callMicStop() {
  if (!micOn) return;
  micReleaseI2S();
  micOn = false;
}

// ---------------------------------------------------------------------
// JARINGAN DASAR
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// TRANSPORT TCP KE SERVER (opsional; SIP LAN antar-HP, scan & RTP tetap UDP)
// ---------------------------------------------------------------------
static WiFiClient srvTc;                         // sambungan ke server SIP (nama 'sipTcp' sdh dipakai server diagnosa)
static bool     tcpUp = false, tcpFallback = false;
static uint8_t  tcpFails = 0;
static uint32_t tcpNextTry = 0;
static char*    tcpBuf = nullptr;                // aliran masuk (SIP_BUF * 2)
static char*    tcpTxBuf = nullptr;              // salinan keluar utk ganti Via UDP -> TCP (SIP_BUF)
static int      tcpLen = 0;
static bool     sipRtxTcp = false;               // pesan retransmisi terakhir lewat TCP? (kalau ya, tdk perlu dikirim ulang)
static void callOnSip(const char* msg, int len, const char* srcIp, int srcPort);   // deklarasi maju

static bool callTcpActive() { return callTcpPref && !tcpFallback && tcpBuf != nullptr; }
static bool callViaTcp(const char* ip) { return callTcpActive() && srvOk && ip && strcmp(ip, srvIpStr) == 0; }

static void tcpReset() {
  srvTc.stop(); tcpUp = false; tcpLen = 0; tcpFails = 0; tcpFallback = false; tcpNextTry = 0;
}

static void tcpClose(const char* why) {
  bool was = tcpUp;
  srvTc.stop(); tcpUp = false; tcpLen = 0;
  if (was) {
    if (why && why[0]) callDbgPush(why);
    regPending = false; regNextAt = 0;           // ikatan registrasi ikut hilang bersama sambungan -> daftar ulang
  }
}

static bool tcpConnect(int timeoutMs) {
  if (tcpUp) return true;
  if (!srvOk) return false;
  uint32_t now = millis();
  if ((int32_t)(now - tcpNextTry) < 0) return false;
  srvTc.stop();
  if (srvTc.connect(srvIp, (uint16_t)tPort, timeoutMs)) {
    srvTc.setNoDelay(true);
    tcpUp = true; tcpLen = 0; tcpFails = 0;
    callDbgPush("TCP tersambung");
    regNextAt = 0;                               // sambungan baru belum punya registrasi
    return true;
  }
  tcpFails++;
  tcpNextTry = millis() + (tcpFails < 5 ? 2000UL * tcpFails : 10000UL);
  if (tcpFails >= 3) {
    tcpFallback = true; regPending = false; regNextAt = 0;
    srvOk = false;                               // SRV utk UDP bisa menunjuk alamat/port lain -> lookup ulang
    callDbgPush("TCP gagal -> UDP");
  } else callDbgPush("TCP gagal konek");
  return false;
}

static void tcpSend(const char* data, int len) {
  if (len <= 0 || len >= SIP_BUF || !tcpTxBuf) return;
  if (!tcpUp) {
    if (len <= 4) return;                        // keepalive tdk perlu memicu sambung ulang
    if (!tcpConnect(callPhase == CPH_IDLE ? 3000 : 1000)) return;
  }
  memcpy(tcpTxBuf, data, (size_t)len); tcpTxBuf[len] = 0;
  for (char* p = tcpTxBuf; (p = strstr(p, "SIP/2.0/UDP")) != nullptr; p += 11) { p[8] = 'T'; p[9] = 'C'; p[10] = 'P'; }
  int w = (int)srvTc.write((const uint8_t*)tcpTxBuf, (size_t)len);
  if (w != len) tcpClose("TCP kirim gagal");
}

// Ambil satu pesan SIP utuh dari awal aliran TCP (framing lewat Content-Length).
// return: byte yg dikonsumsi dari awal buf; 0 = belum lengkap; -1 = aliran rusak/terlalu besar.
// *off, *mlen = posisi & panjang pesan di buf (mlen 0 = hanya CRLF keepalive/pong).
static int sipStreamTake(const char* buf, int len, int maxTotal, int* off, int* mlen) {
  int i = 0;
  while (i < len && (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ')) i++;
  *off = i; *mlen = 0;
  if (i >= len) return len;
  int he = -1;
  for (int k = i; k + 3 < len; k++) {
    if (buf[k] == '\r' && buf[k + 1] == '\n' && buf[k + 2] == '\r' && buf[k + 3] == '\n') { he = k; break; }
  }
  if (he < 0) return (len - i > maxTotal) ? -1 : 0;
  int cl = 0;
  for (int k = i; k < he; ) {
    int e = k; while (e < he && buf[e] != '\n') e++;
    int c = -1;
    if (e - k > 15 && strncasecmp(buf + k, "content-length", 14) == 0) c = k + 14;
    else if (e - k > 2 && (buf[k] == 'l' || buf[k] == 'L')) c = k + 1;
    if (c >= 0) {
      while (c < e && buf[c] == ' ') c++;
      if (c < e && buf[c] == ':') { c++; while (c < e && buf[c] == ' ') c++; cl = atoi(buf + c); }
    }
    k = e + 1;
  }
  if (cl < 0 || he + 4 + cl > maxTotal) return -1;
  int total = he + 4 + cl;
  if (len < total) return 0;
  *mlen = total - i;
  return total;
}

static void tcpPoll() {
  if (!tcpUp || !tcpBuf) return;
  int guard = 0;
  while (guard++ < 6 && srvTc.available() > 0) {
    int room = SIP_BUF * 2 - tcpLen;
    if (room <= 0) { tcpLen = 0; callDbgPush("TCP buf penuh"); break; }
    int r = srvTc.read((uint8_t*)tcpBuf + tcpLen, (size_t)room);
    if (r <= 0) break;
    tcpLen += r;
    for (int g = 0; g < 8 && tcpLen > 0; g++) {
      int off = 0, ml = 0;
      int used = sipStreamTake(tcpBuf, tcpLen, SIP_BUF * 2, &off, &ml);
      if (used < 0) { tcpLen = 0; callDbgPush("TCP aliran rusak"); break; }
      if (used == 0) break;
      if (ml > 0 && ml < SIP_BUF) {
        memcpy(sipRx, tcpBuf + off, (size_t)ml); sipRx[ml] = 0;
        callOnSip(sipRx, ml, srvIpStr, tPort);
        if (!tcpUp) { tcpLen = 0; return; }      // handler menutup sambungan
      } else if (ml >= SIP_BUF) { char dg[40]; snprintf(dg, sizeof(dg), "TCP pesan %dB dibuang", ml); callDbgPush(dg); }
      tcpLen -= used;
      if (tcpLen > 0) memmove(tcpBuf, tcpBuf + used, (size_t)tcpLen);
    }
  }
  if (tcpUp && !srvTc.connected() && srvTc.available() <= 0) tcpClose("TCP putus");
}

// ---------------------------------------------------------------------
// JARINGAN DASAR
// ---------------------------------------------------------------------
static void sipTxTo(const char* ip, int port, const char* data, int len) {
  if (!sipOpen || len <= 0) return;
  if (callViaTcp(ip)) { tcpSend(data, len); return; }
  IPAddress a;
  if (!a.fromString(ip)) return;
  sipUdp.beginPacket(a, (uint16_t)port);
  sipUdp.write((const uint8_t*)data, (size_t)len);
  sipUdp.endPacket();
}

// kirim sipTx[0..len) dan simpan utk retransmisi
static void sipSendKeep(const char* ip, int port, int len) {
  if (len <= 0 || len >= SIP_BUF) return;
  sipTxTo(ip, port, sipTx, len);
  memcpy(sipRtx, sipTx, (size_t)len); sipRtx[len] = 0;
  sipRtxLen = len; callCopy(sipRtxIp, sizeof(sipRtxIp), ip); sipRtxPort = port;
  sipRtxTcp = callViaTcp(ip) && tcpUp;           // terkirim lewat TCP -> jangan dikirim ulang (nanti dobel)
}

static void sipResend() {
  if (sipRtxLen > 0 && !sipRtxTcp) sipTxTo(sipRtxIp, sipRtxPort, sipRtx, sipRtxLen);
}

static bool callResolve(const char* host, char* ipOut) {
  ipOut[0] = 0;
  if (callLooksLikeIp(host)) { callCopy(ipOut, 16, host); return true; }
  if (srvOk && tHostName[0] && strcasecmp(host, tHostName) == 0) { callCopy(ipOut, 16, srvIpStr); return true; }
  IPAddress ip;
  if (WiFi.hostByName(host, ip) != 1) return false;
  snprintf(ipOut, 16, "%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
  return true;
}

// ---------------------------------------------------------------------
// DNS SRV (RFC 3263). ESP32 tdk punya fungsi SRV bawaan, jadi paket DNS dibuat & dibaca sendiri.
// ---------------------------------------------------------------------
static WiFiUDP dnsUdp;

// lewati nama DNS (label / pointer kompresi) mulai p; return posisi setelah nama, -1 kalau rusak
static int dnsSkipName(const uint8_t* b, int len, int p) {
  for (int g = 0; g < 64 && p < len; g++) {
    uint8_t c = b[p];
    if (c == 0) return p + 1;
    if ((c & 0xC0) == 0xC0) return (p + 2 <= len) ? p + 2 : -1;
    p += 1 + c;
  }
  return -1;
}

// baca nama DNS (dengan kompresi) ke out
static bool dnsReadName(const uint8_t* b, int len, int p, char* out, int cap) {
  int n = 0, jumps = 0;
  while (p < len) {
    uint8_t c = b[p];
    if (c == 0) { out[n] = 0; return n > 0; }
    if ((c & 0xC0) == 0xC0) {
      if (p + 1 >= len || ++jumps > 8) return false;
      p = ((c & 0x3F) << 8) | b[p + 1];
      continue;
    }
    if (c > 63 || p + 1 + c > len || n + c + 2 > cap) return false;
    if (n) out[n++] = '.';
    memcpy(out + n, b + p + 1, c); n += c; p += 1 + c;
  }
  return false;
}

// susun query SRV (type 33, class IN). return panjang paket, 0 kalau nama tdk valid
static int dnsBuildSrvQuery(uint8_t* out, int cap, uint16_t id, const char* qname) {
  if (cap < 20) return 0;
  memset(out, 0, 12);
  out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id;
  out[2] = 0x01;                                    // RD (minta rekursif)
  out[5] = 1;                                       // QDCOUNT = 1
  int p = 12;
  const char* s = qname;
  while (*s) {
    const char* e = strchr(s, '.');
    int l = e ? (int)(e - s) : (int)strlen(s);
    if (l <= 0 || l > 63 || p + 1 + l + 5 > cap) return 0;
    out[p++] = (uint8_t)l; memcpy(out + p, s, (size_t)l); p += l;
    s += l; if (*s == '.') s++;
  }
  out[p++] = 0;
  out[p++] = 0; out[p++] = 33;                      // QTYPE SRV
  out[p++] = 0; out[p++] = 1;                       // QCLASS IN
  return p;
}

// baca balasan: pilih SRV prioritas terendah (bobot tertinggi). host = nama target, port dari record
static bool dnsParseSrv(const uint8_t* b, int len, uint16_t id, char* host, int hcap, int* port) {
  if (len < 12 || (uint16_t)((b[0] << 8) | b[1]) != id) return false;
  if (!(b[2] & 0x80)) return false;                 // bukan balasan
  if (b[2] & 0x02) return false;                    // terpotong (TC) -> anggap gagal
  if ((b[3] & 0x0F) != 0) return false;             // RCODE error (mis. NXDOMAIN)
  int qd = (b[4] << 8) | b[5], an = (b[6] << 8) | b[7];
  int p = 12;
  for (int i = 0; i < qd; i++) { p = dnsSkipName(b, len, p); if (p < 0) return false; p += 4; if (p > len) return false; }
  int bestPri = 65536, bestW = -1, bestPort = 0; bool found = false; char tmp[64];
  for (int i = 0; i < an; i++) {
    p = dnsSkipName(b, len, p);
    if (p < 0 || p + 10 > len) return false;
    int type = (b[p] << 8) | b[p + 1], rdl = (b[p + 8] << 8) | b[p + 9];
    p += 10;
    if (p + rdl > len) return false;
    if (type == 33 && rdl >= 7) {
      int pri = (b[p] << 8) | b[p + 1], w = (b[p + 2] << 8) | b[p + 3], prt = (b[p + 4] << 8) | b[p + 5];
      if (prt > 0 && dnsReadName(b, len, p + 6, tmp, sizeof(tmp)) && (pri < bestPri || (pri == bestPri && w > bestW))) {
        bestPri = pri; bestW = w; bestPort = prt; found = true;
        snprintf(host, (size_t)hcap, "%s", tmp);
      }
    }
    p += rdl;
  }
  if (!found) return false;
  *port = bestPort;
  return true;
}

// cari _sip._tcp / _sip._udp.<domain>; hasil: IP target (A-lookup) + port SRV
static bool callSrvLookup(const char* domain, bool tcp, char* ipOut, int* portOut) {
  char qn[160];
  if (snprintf(qn, sizeof(qn), "%s.%s", tcp ? "_sip._tcp" : "_sip._udp", domain) >= (int)sizeof(qn)) return false;
  uint8_t pkt[300], rx[512];
  uint16_t id = (uint16_t)(esp_random() & 0xFFFF);
  int ql = dnsBuildSrvQuery(pkt, sizeof(pkt), id, qn);
  if (ql <= 0) return false;
  IPAddress servers[2] = { WiFi.dnsIP(0), IPAddress(8, 8, 8, 8) };
  for (int sv = 0; sv < 2; sv++) {
    if (servers[sv] == IPAddress(0, 0, 0, 0)) continue;
    if (sv == 1 && servers[0] == servers[1]) continue;
    dnsUdp.begin(41234);
    dnsUdp.beginPacket(servers[sv], 53);
    dnsUdp.write(pkt, (size_t)ql);
    dnsUdp.endPacket();
    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 1200UL) {
      if (dnsUdp.parsePacket() > 0) {
        int r = dnsUdp.read(rx, sizeof(rx));
        char host[64]; int port = 0;
        bool ok = (r > 0) && dnsParseSrv(rx, r, id, host, sizeof(host), &port);
        dnsUdp.stop();
        if (ok && callResolve(host, ipOut)) { *portOut = port; return true; }
        if (ok) return false;                       // SRV ada tapi targetnya tdk ter-resolve
        break;                                      // balasan tdk berguna (NXDOMAIN dll) -> coba server DNS berikutnya
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    dnsUdp.stop();
  }
  return false;
}

static bool callResolveServer() {
  if (srvOk && millis() - srvAt < 600000UL) return true;
  if (!tHostName[0]) return false;
  bool wasOk = srvOk;
  srvOk = false;                                     // paksa lookup sungguhan (callResolve() mengembalikan cache kalau srvOk true)
  char ip[16]; int port = tPortCfg;
  bool viaSrv = false;
  if (!tProxyName[0] && !tPortExplicit && !callLooksLikeIp(tHostName)) {
    viaSrv = callSrvLookup(tHostName, callTcpActive(), ip, &port);
    char dg[40];
    if (viaSrv) snprintf(dg, sizeof(dg), "SRV %s:%d", ip, port);
    else snprintf(dg, sizeof(dg), "%s", RPTR("SRV tdk ada, pakai A"));
    callDbgPush(dg);
  }
  if (!viaSrv) {
    port = tPortCfg;
    if (!callResolve(tProxyName[0] ? tProxyName : tHostName, ip)) {
      if (wasOk) { srvOk = true; srvAt = millis() - 540000UL; return true; }   // DNS sesaat gagal: pakai yg lama, coba lagi 1 menit lagi
      return false;
    }
  }
  bool changed = (strcmp(ip, srvIpStr) != 0) || (port != tPort);
  srvOk = true; srvAt = millis();
  callCopy(srvIpStr, sizeof(srvIpStr), ip);
  srvIp.fromString(ip);
  tPort = port;
  if (changed) tcpClose("server berubah");           // sambungan lama menuju alamat lama
  return true;
}

// "ip:port" utk header Contact (publik kalau sdh dipelajari lewat rport, kecuali panggilan LAN)
static void callContactStr(char* out, int cap, bool lan) {
  if (!lan && callTcpActive()) {                  // TCP: rport = port acak sambungan keluar, jangan dipakai
    snprintf(out, cap, "%s:%d;transport=tcp", lip, CALL_SIP_PORT);
    return;
  }
  if (!lan && natIp[0] && natPort > 0) snprintf(out, cap, "%s:%d", natIp, natPort);
  else snprintf(out, cap, "%s:%d", lip, CALL_SIP_PORT);
}

static void callFromUser(char* out, int cap) {
  if (tUser[0]) { callCopy(out, cap, tUser); return; }
  int n = 0;
  for (const char* p = tName; *p && n < cap - 1; p++) if (isalnum((unsigned char)*p)) out[n++] = *p;
  out[n] = 0;
  if (!out[0]) callCopy(out, cap, "nyx");
}

static void callNewBranch(char* out, int cap) {
  char h[13]; callRandHex(h, 12);
  snprintf(out, cap, "z9hG4bK%s", h);
}

// ---------------------------------------------------------------------
// DIGEST: bangun header Authorization dari 401/407
// ---------------------------------------------------------------------
static bool callBuildAuth(const char* msg, bool proxy, const char* method, const char* uri, char* out, int cap) {
  const char* hn = proxy ? "Proxy-Authenticate" : "WWW-Authenticate";
  char best[420] = ""; int bestAlg = -1;
  for (int k = 0; k < 4; k++) {
    char h[420];
    if (!sipHdr(msg, hn, "", k, h, sizeof(h))) break;
    char alg[24] = "";
    callDigestParam(h, "algorithm", alg, sizeof(alg));
    int a = -1;
    if (!alg[0] || callEqNoCase(alg, "MD5", 4)) a = 0;
    else if (callEqNoCase(alg, "SHA-256", 8)) a = 1;
    if (a > bestAlg) { bestAlg = a; callCopy(best, sizeof(best), h); }
  }
  if (bestAlg < 0) return false;
  char realm[80], nonce[100], opaque[100], qopList[40], alg[24];
  callDigestParam(best, "realm", realm, sizeof(realm));
  callDigestParam(best, "nonce", nonce, sizeof(nonce));
  callDigestParam(best, "opaque", opaque, sizeof(opaque));
  callDigestParam(best, "qop", qopList, sizeof(qopList));
  callDigestParam(best, "algorithm", alg, sizeof(alg));
  bool useQop = strstr(qopList, "auth") != nullptr && strstr(qopList, "auth-int") != qopList;
  char a1[220], a2[220], ha1[65], ha2[65], resp[65], cn[9], nc[9];
  snprintf(a1, sizeof(a1), "%s:%s:%s", tUser, realm, tPass);
  callHashHex(bestAlg, a1, ha1);
  snprintf(a2, sizeof(a2), "%s:%s", method, uri);
  callHashHex(bestAlg, a2, ha2);
  if (strcmp(nonce, authNonceLast) == 0) authNc++; else { authNc = 1; callCopy(authNonceLast, sizeof(authNonceLast), nonce); }
  snprintf(nc, sizeof(nc), "%08x", (unsigned)authNc);
  callRandHex(cn, 8);
  char resIn[400];
  if (useQop) snprintf(resIn, sizeof(resIn), "%s:%s:%s:%s:auth:%s", ha1, nonce, nc, cn, ha2);
  else        snprintf(resIn, sizeof(resIn), "%s:%s:%s", ha1, nonce, ha2);
  callHashHex(bestAlg, resIn, resp);
  int n = snprintf(out, cap, "%s: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
                   proxy ? "Proxy-Authorization" : "Authorization", tUser, realm, nonce, uri, resp);
  if (alg[0] && n < cap) n += snprintf(out + n, cap - n, ", algorithm=%s", alg);
  if (useQop && n < cap) n += snprintf(out + n, cap - n, ", qop=auth, nc=%s, cnonce=\"%s\"", nc, cn);
  if (opaque[0] && n < cap) n += snprintf(out + n, cap - n, ", opaque=\"%s\"", opaque);
  if (n < cap - 3) { out[n++] = '\r'; out[n++] = '\n'; out[n] = 0; }
  return true;
}

// ---------------------------------------------------------------------
// REGISTRASI
// ---------------------------------------------------------------------
static void regSend(const char* authLine) {
  char branch[28]; callNewBranch(branch, sizeof(branch));
  char cont[40]; callContactStr(cont, sizeof(cont), false);
  regCseq++;
  int n = snprintf(sipTx, SIP_BUF,
    "REGISTER sip:%s SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
    "Max-Forwards: 70\r\n"
    "From: \"%s\" <sip:%s@%s>;tag=%s\r\n"
    "To: <sip:%s@%s>\r\n"
    "Call-ID: %s\r\n"
    "CSeq: %lu REGISTER\r\n"
    "Contact: <sip:%s@%s>;expires=%d\r\n"
    "Expires: %d\r\n"
    "Allow: INVITE, ACK, CANCEL, BYE, OPTIONS\r\n"
    "User-Agent: " CALL_UA "\r\n"
    "%s"
    "Content-Length: 0\r\n\r\n",
    tHostName, lip, CALL_SIP_PORT, branch,
    tName[0] ? tName : tUser, tUser, tHostName, regTag,
    tUser, tHostName, regCallId, (unsigned long)regCseq,
    tUser, cont, CALL_REG_EXPIRES, CALL_REG_EXPIRES,
    authLine ? authLine : "");
  sipSendKeep(srvIpStr, tPort, n);
  regPending = true; regSentAt = millis(); regTries = 1; regBackoff = 500;
}

static void regStart() {
  if (!callResolveServer()) {
    callRegState = 3; callSetRegMsg(RPTR("DNS server gagal"));
    regNextAt = millis() + 15000UL; return;
  }
  if (callTcpActive() && !tcpUp && !tcpConnect(callPhase == CPH_IDLE ? 3000 : 1000)) {
    if (callTcpActive()) {                        // masih TCP (belum jatuh ke UDP): coba lagi setelah jeda
      if (callRegState != 2) { callRegState = 3; callSetRegMsg(RPTR("TCP belum tersambung")); }
      regNextAt = ((int32_t)(tcpNextTry - millis()) > 0) ? tcpNextTry : millis() + 2000UL;
      return;
    }
  }
  if (!srvOk && !callResolveServer()) {
    callRegState = 3; callSetRegMsg(RPTR("DNS server gagal"));
    regNextAt = millis() + 15000UL; return;
  }
  callRandHex(regCallId, 16); strcat(regCallId, "@nyx");
  callRandHex(regTag, 8);
  regAuthSent = false;
  if (callRegState != 2) { callRegState = 1; callSetRegMsg("Mendaftar..."); }   // perpanjangan diam-diam: tetap 'terdaftar'
  regSend(nullptr);
}

static void regFail(const char* msg, uint32_t retryMs) {
  regPending = false; callRegState = 3; callSetRegMsg(msg);
  regNextAt = millis() + retryMs;
}

static void callOnRegResp(const char* msg, int code) {
  if (!regPending || code < 100) return;
  char cid[72]; sipHdr(msg, "Call-ID", "i", 0, cid, sizeof(cid));
  if (strcmp(cid, regCallId) != 0) return;
  {                                                                    // balasan utk REGISTER lama -> abaikan
    char cs[40]; sipHdr(msg, "CSeq", "", 0, cs, sizeof(cs));
    unsigned long rcs = strtoul(cs, nullptr, 10);
    if (rcs && rcs != (unsigned long)regCseq) return;
  }
  if (code < 200) return;
  {   // pelajari alamat publik dari Via (received / rport)
    char via[200], v[40];
    if (sipHdr(msg, "Via", "v", 0, via, sizeof(via))) {
      if (sipParam(via, "received", v, sizeof(v)) && callLooksLikeIp(v)) callCopy(natIp, sizeof(natIp), v);
      if (sipParam(via, "rport", v, sizeof(v)) && atoi(v) > 0) natPort = atoi(v);
    }
  }
  if (code == 401 || code == 407) {
    if (regAuthSent) { regFail(RPTR("Username/password VoIP salah"), 60000UL); return; }
    char uri[100]; snprintf(uri, sizeof(uri), "sip:%s", tHostName);
    char auth[700];
    if (!callBuildAuth(msg, code == 407, "REGISTER", uri, auth, sizeof(auth))) { regFail(RPTR("Auth tdk didukung server"), 60000UL); return; }
    regAuthSent = true;
    regSend(auth);
    return;
  }
  if (code >= 200 && code < 300) {
    int expires = CALL_REG_EXPIRES;
    char ex[24];
    if (sipHdr(msg, "Expires", "", 0, ex, sizeof(ex)) && atoi(ex) > 0) expires = atoi(ex);
    char ct[200], v[24];
    if (sipHdr(msg, "Contact", "m", 0, ct, sizeof(ct)) && sipParam(ct, "expires", v, sizeof(v)) && atoi(v) > 0) expires = atoi(v);
    if (expires < 60) expires = 60;
    regPending = false; callRegState = 2; callSetRegMsg("Terdaftar");
    regNextAt = millis() + (uint32_t)expires * 800UL;       // perpanjang di 80% masa berlaku
    regKeepAt = millis();
    return;
  }
  char b[40];
  if (code == 403) callCopy(b, sizeof(b), RPTR("Ditolak server (403)"));
  else if (code == 404) callCopy(b, sizeof(b), RPTR("Akun tidak ada (404)"));
  else snprintf(b, sizeof(b), RPTR("Gagal daftar (%d)"), code);
  regFail(b, 30000UL);
}

static void callRegStep(uint32_t now) {
  bool need = tHostName[0] && tUser[0];
  if (!need) {
    if (callRegState != 0) { callRegState = 0; callSetRegMsg(""); }
    regPending = false;
    return;
  }
  if (regPending) {
    if ((int32_t)(now - regSentAt) >= (int32_t)regBackoff) {
      if (regTries >= 5) { regFail(RPTR("Server tdk merespons"), 30000UL); return; }
      sipResend(); regTries++; regSentAt = now; if (regBackoff < 4000) regBackoff *= 2;
    }
    return;
  }
  if ((int32_t)(now - regNextAt) >= 0) { regStart(); return; }
  if (callRegState == 2 && (int32_t)(now - regKeepAt) >= 12000 && srvOk) {      // jaga lubang NAT tetap terbuka
    regKeepAt = now;
    sipTxTo(srvIpStr, tPort, "\r\n\r\n", 4);
  }
}

// ---------------------------------------------------------------------
// SDP
// ---------------------------------------------------------------------
static int callBuildSdp(char* out, int cap, bool answer) {
  char pts[24], maps[300];
  if (!answer) {
    callCopy(pts, sizeof(pts), "0 8 101");
    callCopy(maps, sizeof(maps),
      "a=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\n"
      "a=rtpmap:101 telephone-event/8000\r\na=fmtp:101 0-15\r\n");
  } else {
    int pt = (dlg.pt == 8) ? 8 : 0;
    snprintf(pts, sizeof(pts), "%d%s", pt, dlg.has101 ? " 101" : "");
    snprintf(maps, sizeof(maps), "a=rtpmap:%d %s/8000\r\n%s", pt, pt == 8 ? "PCMA" : "PCMU",
             dlg.has101 ? "a=rtpmap:101 telephone-event/8000\r\na=fmtp:101 0-15\r\n" : "");
  }
  uint32_t sid = esp_random() & 0x7FFFFFFFu;
  return snprintf(out, cap,
    "v=0\r\no=nyxos %u %u IN IP4 %s\r\ns=NyxOS\r\nc=IN IP4 %s\r\nt=0 0\r\n"
    "m=audio %d RTP/AVP %s\r\n%sa=ptime:20\r\na=sendrecv\r\n",
    (unsigned)sid, (unsigned)sid, dlg.myRtpIp, dlg.myRtpIp, dlg.myRtpPort, pts, maps);
}

// alasan SDP lawan ditolak (utk toast). nullptr = tidak ada masalah
static const char* sdpWhy = nullptr;

static const char* sdpEol(const char* p) {            // akhir baris: \r atau \n
  while (*p && *p != '\r' && *p != '\n') p++;
  return p;
}

static bool callParseSdp(const char* body) {
  sdpWhy = nullptr;
  if (!body || !body[0]) { sdpWhy = RPTR("Lawan tdk kirim SDP"); return false; }

  // cari baris m=audio pertama yang port-nya > 0; kalau profilnya SRTP (SAVP) catat saja
  const char* m = body;
  const char* hit = nullptr;
  bool sawSrtp = false, sawZeroPort = false;
  while ((m = strstr(m, "m=audio ")) != nullptr) {
    int port = atoi(m + 8);
    const char* eol = sdpEol(m);
    bool plain = false;
    for (const char* q = m; q + 8 <= eol; q++) {
      if (!strncmp(q, "RTP/AVP", 7)) { plain = true; break; }   // juga cocok "RTP/AVPF"
      if (!strncmp(q, "RTP/SAVP", 8)) { sawSrtp = true; }
    }
    if (port <= 0) sawZeroPort = true;
    else if (plain) { hit = m; break; }
    m = eol;
  }
  if (!hit) {
    sdpWhy = sawSrtp ? RPTR("Lawan wajib SRTP (enkripsi)")
           : sawZeroPort ? "Lawan menolak audio"
           : "SDP lawan tanpa audio";
    return false;
  }
  m = hit;
  int port = atoi(m + 8);

  // alamat media: c= di bagian media dulu, lalu c= tingkat sesi
  const char* c = nullptr;
  const char* mEol = sdpEol(m);
  const char* nextM = strstr(mEol, "\nm=");
  const char* cm = strstr(mEol, "c=IN IP4 ");
  if (cm && (!nextM || cm < nextM)) c = cm;
  if (!c) c = strstr(body, "c=IN IP4 ");
  if (!c) { sdpWhy = "SDP lawan tanpa alamat IPv4"; return false; }
  char host[64]; int n = 0; c += 9;
  while (*c && *c != '\r' && *c != '\n' && *c != '/' && *c != ' ' && n < 63) host[n++] = *c++;
  host[n] = 0;
  char ip[16];
  if (!callResolve(host, ip) || !ip[0]) { sdpWhy = "Alamat media lawan tak valid"; return false; }
  if (strcmp(ip, "0.0.0.0") == 0) { sdpWhy = RPTR("Lawan minta panggilan ditahan"); return false; }

  // daftar payload di baris m= : ambil PCMU(0) / PCMA(8)
  const char* av = strstr(m, "RTP/AVP");
  int pick = -1;
  if (av) {
    const char* p = av + 7;
    while (*p && *p != '\r' && *p != '\n') {
      while (*p == ' ') p++;
      if (*p == 'F' && p[-1] == 'P') p++;          // "RTP/AVPF"
      while (*p == ' ') p++;
      if (!isdigit((unsigned char)*p)) break;
      int v = atoi(p);
      if (pick < 0 && (v == 0 || v == 8)) pick = v;
      while (isdigit((unsigned char)*p)) p++;
    }
  }
  if (pick < 0) { sdpWhy = "Lawan tdk dukung G.711"; return false; }
  callCopy(dlg.rtpIp, sizeof(dlg.rtpIp), ip);
  dlg.rtpPort = port;
  dlg.pt = (uint8_t)pick;
  dlg.has101 = strstr(body, "telephone-event/8000") != nullptr;
  return true;
}

// ---------------------------------------------------------------------
// RTP SOCKET + STUN
// ---------------------------------------------------------------------
static void callTurnRelease();
static void callOnRtp(const uint8_t* p, int len, const char* srcIp, int srcPort);
static void callRtpOpen() {
  if (rtpOpen) { callTurnRelease(); rtpUdp.stop(); }
  rtpUdp.begin(CALL_RTP_PORT);
  rtpOpen = true;
  callCopy(dlg.myRtpIp, sizeof(dlg.myRtpIp), lip);
  dlg.myRtpPort = CALL_RTP_PORT;
}

static void callRtpClose() {
  if (rtpOpen) { callTurnRelease(); rtpUdp.stop(); rtpOpen = false; }
}

// alamat publik RTP lewat STUN (hanya utk panggilan lewat internet). Gagal = tetap pakai IP lokal.
static void callStunMap() {
  if (!rtpOpen) return;
  char sip[16];
  if (!callResolve(CALL_STUN_HOST, sip)) return;
  IPAddress a; if (!a.fromString(sip)) return;
  uint8_t req[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };
  for (int i = 8; i < 20; i++) req[i] = (uint8_t)(esp_random() & 0xFF);
  for (int attempt = 0; attempt < 2; attempt++) {
    rtpUdp.beginPacket(a, CALL_STUN_PORT);
    rtpUdp.write(req, 20);
    rtpUdp.endPacket();
    uint32_t t0 = millis();
    while (millis() - t0 < 500) {
      int n = rtpUdp.parsePacket();
      if (n >= 20) {
        uint8_t b[128];
        int r = rtpUdp.read(b, sizeof(b));
        if (r >= 20 && b[0] == 0x01 && b[1] == 0x01 && memcmp(b + 8, req + 8, 12) == 0) {
          int pos = 20;
          while (pos + 4 <= r) {
            int type = (b[pos] << 8) | b[pos + 1];
            int len = (b[pos + 2] << 8) | b[pos + 3];
            if (pos + 4 + len > r) break;
            if (type == 0x0020 && len >= 8 && b[pos + 5] == 0x01) {          // XOR-MAPPED-ADDRESS
              int port = ((b[pos + 6] << 8) | b[pos + 7]) ^ 0x2112;
              snprintf(dlg.myRtpIp, sizeof(dlg.myRtpIp), "%u.%u.%u.%u",
                       (unsigned)(b[pos + 8] ^ 0x21), (unsigned)(b[pos + 9] ^ 0x12),
                       (unsigned)(b[pos + 10] ^ 0xA4), (unsigned)(b[pos + 11] ^ 0x42));
              dlg.myRtpPort = port;
              return;
            }
            if (type == 0x0001 && len >= 8 && b[pos + 5] == 0x01) {          // MAPPED-ADDRESS (server lama)
              dlg.myRtpPort = (b[pos + 6] << 8) | b[pos + 7];
              snprintf(dlg.myRtpIp, sizeof(dlg.myRtpIp), "%u.%u.%u.%u",
                       (unsigned)b[pos + 8], (unsigned)b[pos + 9], (unsigned)b[pos + 10], (unsigned)b[pos + 11]);
              return;
            }
            pos += 4 + ((len + 3) & ~3);
          }
        }
      } else if (n > 0) {
        uint8_t junk[16]; rtpUdp.read(junk, sizeof(junk));
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

// ---------------------------------------------------------------------
// TURN (RFC 5766, UDP) - relay RTP lewat server TURN utk NAT simetris / CGNAT.
// Alokasi memakai socket RTP yg sama; paket suara ke/dari lawan dibungkus
// Send/Data Indication. Gagal = otomatis kembali ke STUN.
// ---------------------------------------------------------------------
static bool      turnOn = false;                 // alokasi aktif utk panggilan ini
static IPAddress turnSrv;
static uint16_t  turnPort = 3478;
static char      turnUser[100], turnPass[100], turnRealm[80], turnNonce[130];
static uint8_t   turnKey[16];
static uint32_t  turnRefreshAt = 0;
static uint8_t   turnStale = 0;
static char      turnPermIp[16] = "";
static char      turnErr[40] = "";
static uint8_t   turnReq[512] EXT_RAM_BSS_ATTR, turnRsp[600] EXT_RAM_BSS_ATTR, turnTx[256] EXT_RAM_BSS_ATTR, turnTid[12] EXT_RAM_BSS_ATTR;   // hanya dipakai callTask

static inline void turnPut16(uint8_t* b, int v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)v; }

static int turnHdr(uint8_t* b, uint16_t type) {
  turnPut16(b, type); turnPut16(b + 2, 0);
  b[4] = 0x21; b[5] = 0x12; b[6] = 0xA4; b[7] = 0x42;
  for (int i = 0; i < 12; i++) turnTid[i] = (uint8_t)(esp_random() & 0xFF);
  memcpy(b + 8, turnTid, 12);
  return 20;
}

static int turnAttr(uint8_t* b, int len, uint16_t type, const void* d, int dl) {
  turnPut16(b + len, type); turnPut16(b + len + 2, dl);
  if (dl) memcpy(b + len + 4, d, (size_t)dl);
  int pad = (4 - (dl & 3)) & 3;
  for (int i = 0; i < pad; i++) b[len + 4 + dl + i] = 0;
  return len + 4 + dl + pad;
}

static int turnAttrXorAddr(uint8_t* b, int len, uint16_t type, IPAddress ip, int port) {
  uint8_t v[8];
  v[0] = 0; v[1] = 1;
  turnPut16(v + 2, port ^ 0x2112);
  v[4] = (uint8_t)(ip[0] ^ 0x21); v[5] = (uint8_t)(ip[1] ^ 0x12);
  v[6] = (uint8_t)(ip[2] ^ 0xA4); v[7] = (uint8_t)(ip[3] ^ 0x42);
  return turnAttr(b, len, type, v, 8);
}

// HMAC-SHA1 portabel: hanya pakai mbedtls_md() satu-tembak. mbedtls_md_hmac() ikut hilang di mbedTLS 4 (core esp32 4.x / ESP-IDF 6),
// jadi dihitung manual: H((K^opad) || H((K^ipad) || pesan)), blok SHA-1 = 64 byte.
static void turnHmacSha1(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen, uint8_t* out) {
  memset(out, 0, 20);
  const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!mi || klen > 64) return;
  uint8_t k[64]; memset(k, 0, sizeof(k)); memcpy(k, key, klen);
  // FRAG_V1: pesan STUN/TURN kecil -> buffer di stack, tanpa malloc/free per pesan (tetap hanya mbedtls_md satu-tembak)
  uint8_t sbuf[64 + 512];
  uint8_t* buf = (64 + mlen <= sizeof(sbuf)) ? sbuf : (uint8_t*)malloc(64 + mlen);
  if (!buf) return;
  for (int i = 0; i < 64; i++) buf[i] = k[i] ^ 0x36;
  memcpy(buf + 64, msg, mlen);
  uint8_t inner[20];
  mbedtls_md(mi, buf, 64 + mlen, inner);
  if (buf != sbuf) free(buf);
  uint8_t ob[64 + 20];
  for (int i = 0; i < 64; i++) ob[i] = k[i] ^ 0x5c;
  memcpy(ob + 64, inner, 20);
  mbedtls_md(mi, ob, sizeof(ob), out);
}
// MESSAGE-INTEGRITY (long-term): HMAC-SHA1(key, pesan dgn field panjang sudah memuat atribut MI)
static int turnFinish(uint8_t* b, int len, bool integrity) {
  if (integrity) {
    turnPut16(b + 2, len - 20 + 24);
    uint8_t mac[20];
    turnHmacSha1(turnKey, 16, b, (size_t)len, mac);   // dulu mbedtls_md_hmac() -> hilang di mbedTLS 4
    len = turnAttr(b, len, 0x0008, mac, 20);
  }
  turnPut16(b + 2, len - 20);
  return len;
}

static void turnMakeKey() {      // key = MD5(user:realm:pass)
  char in[300];
  snprintf(in, sizeof(in), "%s:%s:%s", turnUser, turnRealm, turnPass);
  const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (mi) mbedtls_md(mi, (const unsigned char*)in, strlen(in), turnKey);
}

static const uint8_t* turnFindAttr(const uint8_t* b, int n, uint16_t type, int* alen) {
  int mlen = 20 + ((b[2] << 8) | b[3]); if (mlen > n) mlen = n;
  int pos = 20;
  while (pos + 4 <= mlen) {
    int t = (b[pos] << 8) | b[pos + 1], l = (b[pos + 2] << 8) | b[pos + 3];
    if (pos + 4 + l > mlen) break;
    if (t == type) { *alen = l; return b + pos + 4; }
    pos += 4 + ((l + 3) & ~3);
  }
  return nullptr;
}

static bool turnDecodeXor(const uint8_t* v, int l, char* ip, int cap, int* port) {
  if (l < 8 || v[1] != 1) return false;
  *port = ((v[2] << 8) | v[3]) ^ 0x2112;
  snprintf(ip, (size_t)cap, "%u.%u.%u.%u", (unsigned)(v[4] ^ 0x21), (unsigned)(v[5] ^ 0x12), (unsigned)(v[6] ^ 0xA4), (unsigned)(v[7] ^ 0x42));
  return true;
}

static void turnTakeRealmNonce(const uint8_t* r, int rl) {
  int al; const uint8_t* e;
  e = turnFindAttr(r, rl, 0x0014, &al);
  if (e && al > 0 && al < (int)sizeof(turnRealm)) { memcpy(turnRealm, e, (size_t)al); turnRealm[al] = 0; }
  e = turnFindAttr(r, rl, 0x0015, &al);
  if (e && al > 0 && al < (int)sizeof(turnNonce)) { memcpy(turnNonce, e, (size_t)al); turnNonce[al] = 0; }
  turnMakeKey();
}

static void turnSendRaw(const uint8_t* b, int n) {
  rtpUdp.beginPacket(turnSrv, turnPort);
  rtpUdp.write(b, (size_t)n);
  rtpUdp.endPacket();
}

static int turnBuildReq(uint16_t type, bool transport, int lifetime, bool hasPeer, IPAddress peer, bool auth) {
  uint8_t* b = turnReq;
  int n = turnHdr(b, type);
  if (transport) { uint8_t v[4] = { 17, 0, 0, 0 }; n = turnAttr(b, n, 0x0019, v, 4); }        // REQUESTED-TRANSPORT = UDP
  if (lifetime >= 0) {
    uint8_t v[4] = { (uint8_t)(lifetime >> 24), (uint8_t)(lifetime >> 16), (uint8_t)(lifetime >> 8), (uint8_t)lifetime };
    n = turnAttr(b, n, 0x000D, v, 4);
  }
  if (hasPeer) n = turnAttrXorAddr(b, n, 0x0012, peer, 0);
  if (auth) {
    n = turnAttr(b, n, 0x0006, turnUser, (int)strlen(turnUser));
    n = turnAttr(b, n, 0x0014, turnRealm, (int)strlen(turnRealm));
    n = turnAttr(b, n, 0x0015, turnNonce, (int)strlen(turnNonce));
  }
  return turnFinish(b, n, auth);
}

// kirim turnReq & tunggu respons dgn transaction-ID sama. Return: 0 timeout, 1 sukses, selain itu kode error (mis. 401)
static int turnTransact(int rlen, int* respLen) {
  uint8_t tid[12]; memcpy(tid, turnReq + 8, 12);
  for (int attempt = 0; attempt < 3; attempt++) {
    turnSendRaw(turnReq, rlen);
    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 600UL) {
      int n = rtpUdp.parsePacket();
      if (n >= 20) {
        int r = rtpUdp.read(turnRsp, (int)sizeof(turnRsp));
        if (r >= 20 && memcmp(turnRsp + 8, tid, 12) == 0) {
          *respLen = r;
          int type = (turnRsp[0] << 8) | turnRsp[1];
          if ((type & 0x0110) == 0x0100) return 1;
          int al; const uint8_t* e = turnFindAttr(turnRsp, r, 0x0009, &al);
          return (e && al >= 4) ? (e[2] & 7) * 100 + e[3] : 999;
        }
      } else if (n > 0) {
        uint8_t junk[16]; rtpUdp.read(junk, sizeof(junk));
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  return 0;
}

static bool turnPermit(const char* ipStr, bool wait) {
  if (!turnOn) return false;
  IPAddress a; if (!a.fromString(ipStr)) return false;
  int n = turnBuildReq(0x0008, false, -1, true, a, true);
  if (!wait) { turnSendRaw(turnReq, n); callCopy(turnPermIp, sizeof(turnPermIp), ipStr); return true; }
  int rl = 0, code = turnTransact(n, &rl);
  if (code == 438 || code == 401) {
    turnTakeRealmNonce(turnRsp, rl);
    n = turnBuildReq(0x0008, false, -1, true, a, true);
    code = turnTransact(n, &rl);
  }
  if (code == 1) { callCopy(turnPermIp, sizeof(turnPermIp), ipStr); return true; }
  return false;
}

static bool callTurnAllocate() {
  turnOn = false; turnPermIp[0] = 0; turnErr[0] = 0; turnStale = 0;
  char host[80]; int port;
  callLock();
  callCopy(host, sizeof(host), callTurnHostS.c_str());
  callCopy(turnUser, sizeof(turnUser), callTurnUserS.c_str());
  callCopy(turnPass, sizeof(turnPass), callTurnPassS.c_str());
  port = callTurnPortV;
  callUnlock();
  { char* c = strchr(host, ':'); if (c) { int pp = atoi(c + 1); *c = 0; if (pp > 0) port = pp; } }   // "host:port"
  if (!host[0] || !turnUser[0] || !turnPass[0]) return false;            // TURN tdk diatur: bukan error
  if (!rtpOpen) return false;
  char ip[16];
  if (!callResolve(host, ip) || !turnSrv.fromString(ip)) { callCopy(turnErr, sizeof(turnErr), RPTR("DNS TURN gagal")); return false; }
  turnPort = (uint16_t)(port > 0 ? port : 3478);
  turnRealm[0] = 0; turnNonce[0] = 0;

  int rl = 0;
  int n = turnBuildReq(0x0003, true, -1, false, IPAddress(), false);
  int code = turnTransact(n, &rl);
  if (code == 401 || code == 438) {
    turnTakeRealmNonce(turnRsp, rl);
    if (!turnRealm[0]) { callCopy(turnErr, sizeof(turnErr), RPTR("TURN: realm kosong")); return false; }
    n = turnBuildReq(0x0003, true, -1, false, IPAddress(), true);
    code = turnTransact(n, &rl);
  }
  if (code != 1) {
    if (code == 0) callCopy(turnErr, sizeof(turnErr), "TURN tdk merespons");
    else snprintf(turnErr, sizeof(turnErr), RPTR("TURN ditolak (%d)"), code);
    return false;
  }
  int al = 0;
  const uint8_t* e = turnFindAttr(turnRsp, rl, 0x0016, &al);                 // XOR-RELAYED-ADDRESS
  char rip[16]; int rport = 0;
  if (!e || !turnDecodeXor(e, al, rip, sizeof(rip), &rport) || rport <= 0) { callCopy(turnErr, sizeof(turnErr), RPTR("TURN: alamat relay tdk ada")); return false; }
  callCopy(dlg.myRtpIp, sizeof(dlg.myRtpIp), rip);
  dlg.myRtpPort = rport;
  turnOn = true;
  turnRefreshAt = millis() + 240000UL;                                         // izin berlaku 300 dtk -> perbarui tiap 240
  return true;
}

// dipanggil tiap putaran callTask selama panggilan: perbarui alokasi & izin (tanpa menunggu respons)
static void callTurnTick(uint32_t now) {
  if (!turnOn || (int32_t)(now - turnRefreshAt) < 0) return;
  turnRefreshAt = now + 240000UL;
  int n = turnBuildReq(0x0004, false, 600, false, IPAddress(), true);
  turnSendRaw(turnReq, n);
  if (turnPermIp[0]) {
    IPAddress a;
    if (a.fromString(turnPermIp)) { n = turnBuildReq(0x0008, false, -1, true, a, true); turnSendRaw(turnReq, n); }
  }
}

static void callTurnRelease() {
  if (!turnOn) return;
  int n = turnBuildReq(0x0004, false, 0, false, IPAddress(), true);          // LIFETIME=0 -> bebaskan alokasi
  turnSendRaw(turnReq, n);
  turnOn = false; turnPermIp[0] = 0;
}

static void turnSendData(const uint8_t* d, int n, IPAddress peer, int peerPort) {
  if (n <= 0 || n > 200) return;
  uint8_t* b = turnTx;
  int len = turnHdr(b, 0x0016);                                               // Send Indication
  len = turnAttrXorAddr(b, len, 0x0012, peer, peerPort);
  turnPut16(b + len, 0x0013); turnPut16(b + len + 2, n);
  memcpy(b + len + 4, d, (size_t)n);
  len += 4 + n;
  while (len & 3) b[len++] = 0;
  turnPut16(b + 2, len - 20);
  turnSendRaw(b, len);
}

// paket dari server TURN. true = sudah ditangani (jangan diproses sbg RTP biasa)
static bool callTurnOnPacket(const uint8_t* p, int r) {
  if (r < 20 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xA4 || p[7] != 0x42) return true;
  int type = (p[0] << 8) | p[1];
  if (type == 0x0017) {                                                      // Data Indication
    int al = 0, dl = 0;
    const uint8_t* pe = turnFindAttr(p, r, 0x0012, &al);
    const uint8_t* dt = turnFindAttr(p, r, 0x0013, &dl);
    char ip[16]; int port = 0;
    if (pe && dt && dl > 0 && turnDecodeXor(pe, al, ip, sizeof(ip), &port)) callOnRtp(dt, dl, ip, port);
    return true;
  }
  if ((type & 0x0110) == 0x0110) {                                           // respons error utk refresh/izin
    int al = 0; const uint8_t* e = turnFindAttr(p, r, 0x0009, &al);
    int code = (e && al >= 4) ? (e[2] & 7) * 100 + e[3] : 0;
    if ((code == 438 || code == 401) && turnStale < 5) {                    // nonce basi: ambil yg baru, kirim ulang
      turnStale++;
      turnTakeRealmNonce(p, r);
      turnRefreshAt = millis();
    }
  }
  return true;
}

// ---------------------------------------------------------------------
// BANGUN PESAN
// ---------------------------------------------------------------------
// isi rq dari request
static void sipSplitReq(const char* msg) {
  memset(&rq, 0, sizeof(rq));
  int n = 0;
  for (int k = 0; k < 8; k++) {
    char v[300];
    if (!sipHdr(msg, "Via", "v", k, v, sizeof(v))) break;
    int l = (int)strlen(v);
    if (l >= (int)sizeof(v) - 1) callDbgPush("Via dipotong");
    if (n + l + 8 >= (int)sizeof(rq.via)) { callDbgPush("Via terlalu panjang"); break; }
    n += snprintf(rq.via + n, sizeof(rq.via) - n, "Via: %s\r\n", v);
  }
  sipHdr(msg, "From", "f", 0, rq.from, sizeof(rq.from));
  sipHdr(msg, "To", "t", 0, rq.to, sizeof(rq.to));
  sipHdr(msg, "Call-ID", "i", 0, rq.callId, sizeof(rq.callId));
  sipHdr(msg, "CSeq", "", 0, rq.cseq, sizeof(rq.cseq));
  sipHdr(msg, "Contact", "m", 0, rq.contact, sizeof(rq.contact));
  sipCollectRR(msg, false, rq.rr, sizeof(rq.rr));
}

// balasan utk request yg disimpan di rq (which=0) atau urq (which=1, panggilan masuk)
static int sipBuildReply(int which, int code, const char* reason, const char* extra, const char* body, char* out, int cap) {
  const CallReqHdr& h = which ? urq : rq;
  char tag[16];
  if (which) callCopy(tag, sizeof(tag), dlg.ltag); else callRandHex(tag, 8);
  bool hasTag = strstr(h.to, "tag=") != nullptr;
  int n = snprintf(out, cap, "SIP/2.0 %d %s\r\n%sFrom: %s\r\nTo: %s%s%s\r\nCall-ID: %s\r\nCSeq: %s\r\n",
                   code, reason, h.via, h.from, h.to, hasTag ? "" : ";tag=", hasTag ? "" : tag, h.callId, h.cseq);
  if (which && h.rr[0] && n < cap) n += snprintf(out + n, cap - n, "Record-Route: %s\r\n", h.rr);
  if (n < cap) n += snprintf(out + n, cap - n, "User-Agent: " CALL_UA "\r\n");
  if (extra && extra[0] && n < cap) n += snprintf(out + n, cap - n, "%s", extra);
  if (body && body[0]) {
    if (n < cap) n += snprintf(out + n, cap - n, "Content-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(body), body);
  } else if (n < cap) {
    n += snprintf(out + n, cap - n, "Content-Length: 0\r\n\r\n");
  }
  return n;
}

// tujuan request dalam-dialog: Route pertama kalau ada, kalau tidak Contact lawan
static bool callDialogDest(char* ip, int* port) {
  const char* src = dlg.route[0] ? dlg.route : dlg.target;
  char u[8], h[64]; int p = 0;
  if (!sipUri(src, u, sizeof(u), h, sizeof(h), &p)) return false;
  if (!callResolve(h, ip)) return false;
  *port = p > 0 ? p : 5060;
  return true;
}

static void callSendInvite(const char* authLine) {
  char sdp[640]; callBuildSdp(sdp, sizeof(sdp), false);
  char cont[40]; callContactStr(cont, sizeof(cont), dlg.lan);
  char fu[40]; callFromUser(fu, sizeof(fu));
  callNewBranch(dlg.branch, sizeof(dlg.branch));
  int n = snprintf(sipTx, SIP_BUF,
    "INVITE %s SIP/2.0\r\n"
    "Via: SIP/2.0/UDP %s:%d;branch=%s;rport\r\n"
    "Max-Forwards: 70\r\n"
    "From: %s\r\n"
    "To: %s\r\n"
    "Call-ID: %s\r\n"
    "CSeq: %lu INVITE\r\n"
    "Contact: <sip:%s@%s>\r\n"
    "Allow: INVITE, ACK, CANCEL, BYE, OPTIONS\r\n"
    "User-Agent: " CALL_UA "\r\n"
    "%s"
    "Content-Type: application/sdp\r\n"
    "Content-Length: %d\r\n\r\n%s",
    dlg.reqUri, lip, CALL_SIP_PORT, dlg.branch, dlg.localHdr, dlg.remoteHdr, dlg.callId,
    (unsigned long)dlg.inviteCseq, fu, cont, authLine ? authLine : "", (int)strlen(sdp), sdp);
  sipSendKeep(dlg.destIp, dlg.destPort, n);
  { char dg[40]; snprintf(dg, sizeof(dg), "tx INVITE %dB %s%s", n, sipRtxTcp ? "TCP" : "UDP", (authLine && authLine[0]) ? " auth" : ""); callDbgPush(dg); }
  dlg.retxAt = millis() + 500UL; dlg.retxN = 0; dlg.gotProv = false;
}

static void callSendAckNon2xx(const char* resp) {
  char to[170]; sipHdr(resp, "To", "t", 0, to, sizeof(to));
  int n = snprintf(sipTx, SIP_BUF,
    "ACK %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:%d;branch=%s;rport\r\nMax-Forwards: 70\r\n"
    "From: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %lu ACK\r\nContent-Length: 0\r\n\r\n",
    dlg.reqUri, lip, CALL_SIP_PORT, dlg.branch, dlg.localHdr, to, dlg.callId, (unsigned long)dlg.inviteCseq);
  sipTxTo(dlg.destIp, dlg.destPort, sipTx, n);
}

static void callFixTargetTcp(const char* viaIp) {
  if (!callViaTcp(viaIp)) return;
  if (strstr(dlg.target, "transport=")) return;
  if (strlen(dlg.target) > sizeof(dlg.target) - 16) return;
  strcat(dlg.target, ";transport=tcp");
}

static void callSendAck2xx() {
  char br[28]; callNewBranch(br, sizeof(br));
  char ip[16]; int port;
  if (!callDialogDest(ip, &port)) { callCopy(ip, sizeof(ip), dlg.destIp); port = dlg.destPort; }
  char route[290] = "";
  if (dlg.route[0]) snprintf(route, sizeof(route), "Route: %s\r\n", dlg.route);
  int n = snprintf(sipTx, SIP_BUF,
    "ACK %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:%d;branch=%s;rport\r\nMax-Forwards: 70\r\n%s"
    "From: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %lu ACK\r\nContent-Length: 0\r\n\r\n",
    dlg.target, lip, CALL_SIP_PORT, br, route, dlg.localHdr, dlg.remoteHdr, dlg.callId, (unsigned long)dlg.inviteCseq);
  sipTxTo(ip, port, sipTx, n);
}

static void callSendCancel() {
  int n = snprintf(sipTx, SIP_BUF,
    "CANCEL %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:%d;branch=%s;rport\r\nMax-Forwards: 70\r\n"
    "From: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %lu CANCEL\r\nContent-Length: 0\r\n\r\n",
    dlg.reqUri, lip, CALL_SIP_PORT, dlg.branch, dlg.localHdr, dlg.remoteHdr, dlg.callId, (unsigned long)dlg.inviteCseq);
  sipSendKeep(dlg.destIp, dlg.destPort, n);
}

static void callSendBye() {
  char br[28]; callNewBranch(br, sizeof(br));
  char ip[16]; int port;
  if (!callDialogDest(ip, &port)) { callCopy(ip, sizeof(ip), dlg.uac ? dlg.destIp : dlg.peerIp); port = dlg.uac ? dlg.destPort : dlg.peerPort; }
  char route[290] = "";
  if (dlg.route[0]) snprintf(route, sizeof(route), "Route: %s\r\n", dlg.route);
  dlg.cseq++;
  int n = snprintf(sipTx, SIP_BUF,
    "BYE %s SIP/2.0\r\nVia: SIP/2.0/UDP %s:%d;branch=%s;rport\r\nMax-Forwards: 70\r\n%s"
    "From: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %lu BYE\r\nUser-Agent: " CALL_UA "\r\nContent-Length: 0\r\n\r\n",
    dlg.target[0] ? dlg.target : dlg.reqUri, lip, CALL_SIP_PORT, br, route, dlg.localHdr, dlg.remoteHdr, dlg.callId, (unsigned long)dlg.cseq);
  sipSendKeep(ip, port, n);
}

// ---------------------------------------------------------------------
// AKHIRI PANGGILAN
// ---------------------------------------------------------------------
static void callFinish(const char* why) {
  mediaOn = false;
  toneMode = 0;
  callMicStop();
  callAudioStop();
  callRtpClose();
  callPhase = CPH_IDLE;
  callMuted = false;
  callSetStatus(why);
  callEndedNew = true;
  callDirty = true;
  memset(&dlg, 0, sizeof(dlg));
  sipRtxLen = 0;
}

static void callMediaStart() {
  if (!rtpOpen) callRtpOpen();
  IPAddress a; a.fromString(dlg.rtpIp);
  rtpDest = a; rtpDestPort = dlg.rtpPort; rtpLatched = false;
  if (turnOn && strcmp(turnPermIp, dlg.rtpIp) != 0) { turnPermit(dlg.rtpIp, false); turnRefreshAt = millis() + 1000UL; }   // kirim ulang 1 dtk lagi (jaga paket hilang)
  rtpSeqTx = (uint16_t)esp_random(); rtpTsTx = esp_random(); rtpSsrc = esp_random(); rtpFirst = true;
  rxInit = false; rxLoss = 0; rxGapAt = 0; memset(jbUsed, 0, sizeof(jbUsed)); memset(rxLast, 0, sizeof(rxLast));
  lastRtpRx = millis();
  toneMode = 0;
  if (!callMicStart()) callToast(RPTR("Mic dipakai app lain: suara kamu tdk terkirim"));
  if (!aoOn) { int rc = callAudioStart(); if (rc != 0) callToast(callAudioErr(rc)); }
  mediaOn = true;
  callStartMs = millis();
}

// ---------------------------------------------------------------------
// PANGGILAN KELUAR
// ---------------------------------------------------------------------
static void callBeginDial(const char* targetIn) {
  if (callPhase != CPH_IDLE) return;
  char t[72]; callCopy(t, sizeof(t), targetIn);
  { int l = (int)strlen(t); while (l > 0 && (t[l - 1] == ' ' || t[l - 1] == '\n')) t[--l] = 0; }
  char* tp = t; while (*tp == ' ') tp++;
  if (!*tp) { callToast(RPTR("Isi tujuan dulu")); return; }

  char reqUri[110], user[40], host[64]; int port = 0;
  if (strchr(tp, '@')) snprintf(reqUri, sizeof(reqUri), "sip:%s", tp);
  else if (callLooksLikeIp(tp)) snprintf(reqUri, sizeof(reqUri), "sip:nyx@%s", tp);
  else {
    if (!tHostName[0]) { callToast(RPTR("Isi akun VoIP dulu (Atur)")); return; }
    snprintf(reqUri, sizeof(reqUri), "sip:%s@%s", tp, tHostName);
  }
  if (!sipUri(reqUri, user, sizeof(user), host, sizeof(host), &port)) { callToast(RPTR("Alamat tidak valid")); return; }

  char dip[16]; int dport; bool viaServer = false, direct = false;
  if (callLooksLikeIp(host)) { direct = true; callCopy(dip, sizeof(dip), host); dport = port > 0 ? port : 5060; }
  else if (tHostName[0] && strcasecmp(host, tHostName) == 0) {
    if (!callResolveServer()) { callToast(RPTR("DNS server VoIP gagal")); return; }
    viaServer = true; callCopy(dip, sizeof(dip), srvIpStr); dport = tPort;
  } else {
    if (!callResolve(host, dip)) { callToast(RPTR("Alamat tidak ditemukan")); return; }
    dport = port > 0 ? port : 5060;
  }
  if (viaServer && callRegState != 2) { callToast(RPTR("VoIP belum terdaftar")); return; }

  wakeYieldMic(4000);
  if (micI2SReady) { callToast(RPTR("Mic sedang dipakai app lain")); return; }

  callMarkT(20);
  memset(&dlg, 0, sizeof(dlg));
  dlg.uac = true; dlg.lan = direct && callIsPrivateIp(dip);
  callRtpOpen();
  callMarkT(21);
  if (!dlg.lan) {
    if (!callTurnAllocate()) { if (turnErr[0]) callToast(turnErr); callStunMap(); }
  }
  callMarkT(22);
  int rc = callAudioStart();
  callMarkT(23);
  if (rc != 0) { callToast(callAudioErr(rc)); callRtpClose(); memset(&dlg, 0, sizeof(dlg)); return; }

  char cid[20]; callRandHex(cid, 16);
  snprintf(dlg.callId, sizeof(dlg.callId), "%s@%s", cid, lip);
  callRandHex(dlg.ltag, 8);
  const char* dom = viaServer ? tHostName : lip;
  char fu[40]; callFromUser(fu, sizeof(fu));
  snprintf(dlg.localHdr, sizeof(dlg.localHdr), "\"%s\" <sip:%s@%s>;tag=%s", tName[0] ? tName : "NyxOS", fu, dom, dlg.ltag);
  snprintf(dlg.remoteHdr, sizeof(dlg.remoteHdr), "<%s>", reqUri);
  callCopy(dlg.reqUri, sizeof(dlg.reqUri), reqUri);
  callCopy(dlg.destIp, sizeof(dlg.destIp), dip); dlg.destPort = dport;
  dlg.cseq = 1; dlg.inviteCseq = 1; dlg.t0 = millis();

  callSetPeer(tp);
  callIsLan = dlg.lan;
  callPhase = CPH_DIAL; toneMode = 0;
  callSetStatus("Memanggil...");
  callMarkT(24);
  callSendInvite(nullptr);
  callMarkT(25);
}

// ---------------------------------------------------------------------
// PANGGILAN MASUK
// ---------------------------------------------------------------------
// URI dari header Contact. Bentuk "<sip:...;param>": ambil SEMUA sampai '>' (parameter URI dipakai proxy NAT,
// mis. Flexisip fs-conn-id/gr). Bentuk tanpa kurung: berhenti di ';' / spasi / ','.
static bool sipContactUri(const char* ct, char* out, int cap) {
  const char* s = strstr(ct, "sip:");
  if (!s) s = strstr(ct, "sips:");
  if (!s) return false;
  bool br = false;
  for (const char* q = ct; q < s; q++) if (*q == '<') br = true;
  const char* e = s;
  while (*e && *e != '>' && !(!br && (*e == ';' || *e == ' ' || *e == ','))) e++;
  int l = (int)(e - s);
  if (l <= 0 || l >= cap) return false;
  memcpy(out, s, (size_t)l); out[l] = 0;
  return true;
}

static void callOnInvite(const char* msg, const char* srcIp, int srcPort) {
  sipSplitReq(msg);
  callMarkT(70);
  if (callPhase != CPH_IDLE) {
    if (callPhase == CPH_INCOMING && strcmp(rq.callId, dlg.callId) == 0) {      // INVITE ulang: kirim lagi 180
      int n = sipBuildReply(1, 180, "Ringing", nullptr, nullptr, sipTx, SIP_BUF);
      sipTxTo(dlg.peerIp, dlg.peerPort, sipTx, n);
    } else {
      char dg[40]; snprintf(dg, sizeof(dg), RPTR("-> 486 sibuk (fase %d)"), (int)callPhase);
      callDbgPush(dg);
      int n = sipBuildReply(0, 486, "Busy Here", nullptr, nullptr, sipTx, SIP_BUF);
      sipTxTo(srcIp, srcPort, sipTx, n);
    }
    return;
  }
  memset(&dlg, 0, sizeof(dlg));
  if (!callParseSdp(sipBody(msg))) {
    { char dg[40]; snprintf(dg, sizeof(dg), "-> 488 %s", sdpWhy ? sdpWhy : "SDP"); callDbgPush(dg); }
    int n = sipBuildReply(0, 488, "Not Acceptable Here", nullptr, nullptr, sipTx, SIP_BUF);
    sipTxTo(srcIp, srcPort, sipTx, n);
    return;
  }
  memcpy(&urq, &rq, sizeof(rq));
  dlg.uac = false;
  callCopy(dlg.callId, sizeof(dlg.callId), rq.callId);
  callCopy(dlg.peerIp, sizeof(dlg.peerIp), srcIp); dlg.peerPort = srcPort;
  callRandHex(dlg.ltag, 8);
  snprintf(dlg.localHdr, sizeof(dlg.localHdr), "%s;tag=%s", rq.to, dlg.ltag);
  callCopy(dlg.remoteHdr, sizeof(dlg.remoteHdr), rq.from);
  { char u[40], h[64]; int p; char ct[200];
    callCopy(ct, sizeof(ct), rq.contact);
    if (sipUri(ct, u, sizeof(u), h, sizeof(h), &p)) {
      sipContactUri(ct, dlg.target, sizeof(dlg.target));
    }
  }
  if (!dlg.target[0]) snprintf(dlg.target, sizeof(dlg.target), "sip:%s:%d", srcIp, srcPort);
  callFixTargetTcp(srcIp);
  callCopy(dlg.route, sizeof(dlg.route), rq.rr);
  dlg.cseq = (esp_random() % 900u) + 100u;
  dlg.lan = (!srvOk || strcmp(srcIp, srvIpStr) != 0);
  dlg.t0 = millis();

  char nm[44]; sipDisplay(rq.from, nm, sizeof(nm));
  callSetPeer(nm[0] ? nm : srcIp);
  callIsLan = dlg.lan;

  callMarkT(71);
  int n = sipBuildReply(1, 180, "Ringing", nullptr, nullptr, sipTx, SIP_BUF);
  sipTxTo(srcIp, srcPort, sipTx, n);
  callMarkT(72);

  callPhase = CPH_INCOMING;
  callSetStatus(dlg.lan ? RPTR("Panggilan langsung (LAN)") : RPTR("Panggilan VoIP masuk"));
  int rc = callAudioStart();                       // supaya nada dering terdengar di TWS (kalau ada)
  callMarkT(73);
  if (rc == 0) { toneMode = 2; toneT = 0; toneLastMs = 0; }
  callIncomingNew = true;
  callMarkT(74);
  callDbgPush("-> 180 berdering");
}

static void callAnswer() {
  if (callPhase != CPH_INCOMING) return;
  callRtpOpen();
  if (!dlg.lan) {
    if (callTurnAllocate()) turnPermit(dlg.rtpIp, true);            // izin utk IP lawan SEBELUM 200 OK
    else { if (turnErr[0]) callToast(turnErr); callStunMap(); }
  }
  toneMode = 0;
  char sdp[640]; callBuildSdp(sdp, sizeof(sdp), true);
  char cont[40]; callContactStr(cont, sizeof(cont), dlg.lan);
  char u[40], h[64]; int p = 0;
  sipUri(urq.to, u, sizeof(u), h, sizeof(h), &p);
  char ex[160]; snprintf(ex, sizeof(ex), "Contact: <sip:%s@%s>\r\n", u[0] ? u : "nyx", cont);
  int n = sipBuildReply(1, 200, "OK", ex, sdp, sipTx, SIP_BUF);
  sipSendKeep(dlg.peerIp, dlg.peerPort, n);
  { char dg[40]; snprintf(dg, sizeof(dg), "tx 200 OK %dB %s", n, sipRtxTcp ? "TCP" : "UDP"); callDbgPush(dg); }
  dlg.retxAt = millis() + 500UL; dlg.retxN = 0; dlg.ackd = false;
  callPhase = CPH_ACTIVE;
  callSetStatus(RPTR("Terhubung"));
  callMediaStart();
}

static void callRejectIncoming(int code, const char* reason, const char* msg) {
  if (callPhase != CPH_INCOMING) return;
  int n = sipBuildReply(1, code, reason, nullptr, nullptr, sipTx, SIP_BUF);
  sipTxTo(dlg.peerIp, dlg.peerPort, sipTx, n);
  callFinish(msg);
}

// ---------------------------------------------------------------------
// RESPON INVITE (sisi penelepon)
// ---------------------------------------------------------------------
static void callOnInviteResp(const char* msg, int code) {
  char cid[72]; sipHdr(msg, "Call-ID", "i", 0, cid, sizeof(cid));
  if (!dlg.uac || strcmp(cid, dlg.callId) != 0) return;
  char cs[40]; sipHdr(msg, "CSeq", "", 0, cs, sizeof(cs));
  if (strstr(cs, "CANCEL")) return;
  {                                                                    // balasan utk INVITE lama (retransmit 401 dst) -> abaikan
    unsigned long rcs = strtoul(cs, nullptr, 10);
    if (rcs && rcs != (unsigned long)dlg.inviteCseq) {
      Serial.printf("[CALL] abaikan respons INVITE lama: kode %d cseq %lu (sekarang %lu)\n", code, rcs, (unsigned long)dlg.inviteCseq);
      return;
    }
  }                                    // 200 utk CANCEL: abaikan, tunggu 487
  if (callPhase == CPH_ACTIVE && code == 200) { callSendAck2xx(); return; }   // 200 diulang
  if (callPhase != CPH_DIAL && callPhase != CPH_RINGBACK && callPhase != CPH_ENDING) return;

  if (code < 200) {
    dlg.gotProv = true;
    if (code == 180 || code == 183) {
      if (callPhase == CPH_DIAL) {
        callPhase = CPH_RINGBACK;
        callSetStatus("Berdering...");
        toneMode = 1; toneT = 0; toneLastMs = 0;
      }
    }
    return;
  }
  if (code == 200) {
    char to[170], ct[200]; sipHdr(msg, "To", "t", 0, to, sizeof(to));
    callCopy(dlg.remoteHdr, sizeof(dlg.remoteHdr), to);
    if (sipHdr(msg, "Contact", "m", 0, ct, sizeof(ct))) {
      sipContactUri(ct, dlg.target, sizeof(dlg.target));
    }
    if (!dlg.target[0]) callCopy(dlg.target, sizeof(dlg.target), dlg.reqUri);
    callFixTargetTcp(dlg.destIp);
    sipCollectRR(msg, true, dlg.route, sizeof(dlg.route));
    bool sdpOk = callParseSdp(sipBody(msg));
    callSendAck2xx();
    if (callPhase == CPH_ENDING || !sdpOk) {                            // sudah dibatalkan / SDP tak cocok
      callSendBye();
      callFinish(sdpOk ? RPTR("Dibatalkan") : (sdpWhy ? sdpWhy : RPTR("Codec tidak cocok")));
      return;
    }
    callPhase = CPH_ACTIVE;
    dlg.ackd = true;
    callSetStatus(RPTR("Terhubung"));
    callMediaStart();
    return;
  }
  if (code == 401 || code == 407) {
    callSendAckNon2xx(msg);
    if (dlg.authTried || !tUser[0]) { callFinish(RPTR("Autentikasi ditolak")); return; }
    char auth[700];
    if (!callBuildAuth(msg, code == 407, "INVITE", dlg.reqUri, auth, sizeof(auth))) { callFinish("Auth tdk didukung"); return; }
    dlg.authTried = true;
    dlg.inviteCseq++; dlg.cseq = dlg.inviteCseq;
    callSendInvite(auth);
    return;
  }
  callSendAckNon2xx(msg);
  char b[44];
  switch (code) {
    case 404: callCopy(b, sizeof(b), RPTR("Tujuan tidak ditemukan")); break;
    case 480: callCopy(b, sizeof(b), RPTR("Tidak tersedia")); break;
    case 486: callCopy(b, sizeof(b), RPTR("Sedang sibuk")); break;
    case 487: callCopy(b, sizeof(b), RPTR("Dibatalkan")); break;
    case 488: {
      char wn[80]; wn[0] = 0;
      sipHdr(msg, "Warning", "", 0, wn, sizeof(wn));
      Serial.printf("[CALL] 488 dari lawan. Warning: %s\n", wn[0] ? wn : "(kosong)");
      const char* why = RPTR("Ditolak 488: codec/enkripsi");
      if (strstr(wn, "301") || strstr(wn, "302") || strstr(wn, "304")) why = "Lawan tdk cocok (media)";
      callCopy(b, sizeof(b), why);
    } break;
    case 603: callCopy(b, sizeof(b), RPTR("Ditolak")); break;
    case 408: case 504: callCopy(b, sizeof(b), RPTR("Tidak terjangkau")); break;
    default: snprintf(b, sizeof(b), RPTR("Gagal (%d)"), code); break;
  }
  callFinish(b);
}

// ---------------------------------------------------------------------
// PERMINTAAN MASUK LAINNYA
// ---------------------------------------------------------------------
static void callOnOptions(const char* msg, const char* srcIp, int srcPort) {
  sipSplitReq(msg);
  char nm[28]; callCopy(nm, sizeof(nm), tName[0] ? tName : "HP");
  char ex[120];
  snprintf(ex, sizeof(ex), "Allow: INVITE, ACK, CANCEL, BYE, OPTIONS\r\nAccept: application/sdp\r\n");
  int n = sipBuildReply(0, 200, "OK", ex, nullptr, sipTx, SIP_BUF - 60);
  // sisipkan "(nama)" pada User-Agent supaya pemindai bisa menampilkan nama HP
  char* ua = strstr(sipTx, "User-Agent: " CALL_UA);
  if (ua) {
    char tail[SIP_BUF / 2]; callCopy(tail, sizeof(tail), ua + strlen("User-Agent: " CALL_UA));
    int off = (int)(ua - sipTx);
    n = off + snprintf(sipTx + off, SIP_BUF - off, "User-Agent: " CALL_UA " (%s)%s", nm, tail);
  }
  // tujuan: tanpa rport di Via = alamat sent-by; dgn rport = alamat sumber paket
  char ip[16]; int port = srcPort;
  callCopy(ip, sizeof(ip), srcIp);
  char via[160];
  if (sipHdr(msg, "Via", "v", 0, via, sizeof(via)) && !strstr(via, "rport")) {
    char u[8], h[40]; int p = 0;
    if (sipUri(via, u, sizeof(u), h, sizeof(h), &p)) { /* bentuk "SIP/2.0/UDP host:port" tidak mengandung "sip:" */ }
    const char* sp = strstr(via, "UDP ");
    if (sp) {
      sp += 4;
      char hh[24]; int hn = 0;
      while (*sp && *sp != ':' && *sp != ';' && *sp != ' ' && hn < 23) hh[hn++] = *sp++;
      hh[hn] = 0;
      if (callLooksLikeIp(hh)) { callCopy(ip, sizeof(ip), hh); port = (*sp == ':') ? atoi(sp + 1) : 5060; if (port <= 0) port = 5060; }
    }
  }
  sipTxTo(ip, port, sipTx, n);
}

static void callOnBye(const char* msg, const char* srcIp, int srcPort) {
  sipSplitReq(msg);
  { char dg[40]; snprintf(dg, sizeof(dg), "BYE: fase %d, id %s", (int)callPhase, strcmp(rq.callId, dlg.callId) == 0 ? "cocok" : "beda"); callDbgPush(dg); }
  int n = sipBuildReply(0, 200, "OK", nullptr, nullptr, sipTx, SIP_BUF);
  sipTxTo(srcIp, srcPort, sipTx, n);
  if (callPhase == CPH_IDLE || strcmp(rq.callId, dlg.callId) != 0) return;
  if (callPhase == CPH_ACTIVE || callPhase == CPH_ENDING) callFinish(RPTR("Panggilan selesai"));
}

static void callOnCancel(const char* msg, const char* srcIp, int srcPort) {
  sipSplitReq(msg);
  int n = sipBuildReply(0, 200, "OK", nullptr, nullptr, sipTx, SIP_BUF);
  sipTxTo(srcIp, srcPort, sipTx, n);
  if (callPhase == CPH_INCOMING && strcmp(rq.callId, dlg.callId) == 0) {
    n = sipBuildReply(1, 487, "Request Terminated", nullptr, nullptr, sipTx, SIP_BUF);
    sipTxTo(dlg.peerIp, dlg.peerPort, sipTx, n);
    callFinish(RPTR("Panggilan terlewat"));
  }
}

static void callOnScanResp(const char* msg, const char* srcIp) {
  if (!callScanBusy) return;
  char ua[80];
  if (!sipHdr(msg, "User-Agent", "", 0, ua, sizeof(ua)) && !sipHdr(msg, "Server", "", 0, ua, sizeof(ua))) return;
  if (!strstr(ua, "NyxOS")) return;
  if (strcmp(srcIp, lip) == 0) return;
  int cnt = callScanCount;
  for (int i = 0; i < cnt; i++) if (strcmp(callScanIp[i], srcIp) == 0) return;
  if (cnt >= CALL_SCAN_MAX) return;
  char nm[24] = "";
  const char* lp = strchr(ua, '(');
  if (lp) { int n = 0; lp++; while (*lp && *lp != ')' && n < 23) nm[n++] = *lp++; nm[n] = 0; }
  callLock();
  callCopy(callScanIp[cnt], sizeof(callScanIp[cnt]), srcIp);
  callCopy(callScanName[cnt], sizeof(callScanName[cnt]), nm[0] ? nm : "NyxOS");
  callScanCount = cnt + 1;
  callUnlock();
  callDirty = true;
}

static void callStartScan() {
  if (!sipOpen || callScanBusy) return;
  callScanCount = 0; callScanBusy = true; scanUntil = millis() + 2200UL; callDirty = true;
  char cid[10]; callRandHex(cid, 8);
  char br[28]; callNewBranch(br, sizeof(br));
  int n = snprintf(sipTx, SIP_BUF,
    "OPTIONS sip:nyx@%s SIP/2.0\r\nVia: SIP/2.0/UDP %s:%d;branch=%s\r\nMax-Forwards: 5\r\n"
    "From: <sip:scan@%s>;tag=%s\r\nTo: <sip:nyx@%s>\r\nCall-ID: nyxscan%s@%s\r\nCSeq: 1 OPTIONS\r\n"
    "User-Agent: " CALL_UA "\r\nContent-Length: 0\r\n\r\n",
    "255.255.255.255", lip, CALL_SIP_PORT, br, lip, cid, "255.255.255.255", cid, lip);
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  struct sockaddr_in to; memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET; to.sin_port = htons(CALL_SIP_PORT);
  IPAddress bip = WiFi.broadcastIP();
  to.sin_addr.s_addr = (uint32_t)bip;
  sendto(fd, sipTx, n, 0, (struct sockaddr*)&to, sizeof(to));
  to.sin_addr.s_addr = 0xFFFFFFFFu;
  sendto(fd, sipTx, n, 0, (struct sockaddr*)&to, sizeof(to));
  close(fd);
}

// ---------------------------------------------------------------------
// DISPATCH PESAN SIP MASUK
// ---------------------------------------------------------------------
static void callOnSip(const char* msg, int len, const char* srcIp, int srcPort) {
  if (len < 12) return;                                   // pong keepalive dsb.
  {
    int dc = sipStatus(msg);
    if (dc > 0) {
      char dcs[40]; sipHdr(msg, "CSeq", "", 0, dcs, sizeof(dcs));
      const char* sp = strchr(dcs, ' ');
      snprintf(callDbgRx, sizeof(callDbgRx), "%d %s", dc, sp ? sp + 1 : "");
    } else {
      char dm[16]; sipMethodOf(msg, dm, sizeof(dm));
      snprintf(callDbgRx, sizeof(callDbgRx), "%s %s", dm, srcIp);
    }
    callDbgRxMs = millis(); callDirty = true;
    if (strstr(callDbgRx, "REGISTER") == nullptr && strstr(callDbgRx, "NOTIFY") == nullptr) {      // REGISTER berkala tdk masuk riwayat
      char dg[40]; snprintf(dg, sizeof(dg), "rx %s", callDbgRx); callDbgPush(dg);
    } else Serial.printf("[CALL] rx %s\n", callDbgRx);
  }
  int code = sipStatus(msg);
  if (code > 0) {
    char cs[40]; sipHdr(msg, "CSeq", "", 0, cs, sizeof(cs));
    if (strstr(cs, "REGISTER")) callOnRegResp(msg, code);
    else if (strstr(cs, "INVITE")) callOnInviteResp(msg, code);
    else if (strstr(cs, "OPTIONS")) { if (code == 200) callOnScanResp(msg, srcIp); }
    else if (strstr(cs, "BYE")) { if (callPhase == CPH_ENDING && code >= 200) callFinish(RPTR("Panggilan selesai")); }
    return;
  }
  char method[12]; sipMethodOf(msg, method, sizeof(method));
  if (!strcmp(method, "INVITE"))       callOnInvite(msg, srcIp, srcPort);
  else if (!strcmp(method, "ACK"))     { char cid[72]; sipHdr(msg, "Call-ID", "i", 0, cid, sizeof(cid)); if (strcmp(cid, dlg.callId) == 0) dlg.ackd = true; }
  else if (!strcmp(method, "BYE"))     callOnBye(msg, srcIp, srcPort);
  else if (!strcmp(method, "CANCEL"))  callOnCancel(msg, srcIp, srcPort);
  else if (!strcmp(method, "OPTIONS")) callOnOptions(msg, srcIp, srcPort);
  else {
    sipSplitReq(msg);
    int n = sipBuildReply(0, 405, "Method Not Allowed", nullptr, nullptr, sipTx, SIP_BUF);
    sipTxTo(srcIp, srcPort, sipTx, n);
  }
}

// ---------------------------------------------------------------------
// PERMINTAAN DARI UI
// ---------------------------------------------------------------------
static void callHangup() {
  switch (callPhase) {
    case CPH_DIAL: case CPH_RINGBACK:
      toneMode = 0;
      callSendCancel();
      dlg.cancelSent = true; dlg.t0 = millis(); dlg.retxAt = millis() + 500UL; dlg.retxN = 0;
      callPhase = CPH_ENDING; callSetStatus("Membatalkan...");
      break;
    case CPH_ACTIVE:
      mediaOn = false; callMicStop(); callAudioStop();
      callSendBye();
      dlg.byeSent = true; dlg.t0 = millis(); dlg.retxAt = millis() + 500UL; dlg.retxN = 0;
      callPhase = CPH_ENDING; callSetStatus("Mengakhiri...");
      break;
    case CPH_INCOMING:
      callRejectIncoming(603, "Decline", RPTR("Ditolak"));
      break;
    default: break;
  }
}

static void callHandleReq() {
  uint8_t rqv = callReq;
  if (rqv == CRQ_NONE) return;
  callReq = CRQ_NONE;
  char arg[72];
  callLock(); callCopy(arg, sizeof(arg), callReqArg); callUnlock();
  switch (rqv) {
    case CRQ_DIAL:   callBeginDial(arg); break;
    case CRQ_ANSWER: callAnswer(); break;
    case CRQ_REJECT: if (callPhase == CPH_INCOMING) callRejectIncoming(486, "Busy Here", RPTR("Ditolak")); break;
    case CRQ_HANGUP: callHangup(); break;
    case CRQ_REREG:  regPending = false; regNextAt = 0; callRegState = 0; break;
    case CRQ_SCAN:   callStartScan(); break;
    default: break;
  }
}

// ---------------------------------------------------------------------
// TIMER PER FASE (retransmisi UDP, batas waktu)
// ---------------------------------------------------------------------
static void callPhaseTimers(uint32_t now) {
  if (callScanBusy && (int32_t)(now - scanUntil) >= 0) { callScanBusy = false; callDirty = true; }
  switch (callPhase) {
    case CPH_DIAL: case CPH_RINGBACK:
      if (!dlg.gotProv && (int32_t)(now - dlg.retxAt) >= 0) {
        if (dlg.retxN >= 7) { callFinish(RPTR("Tidak ada respon")); break; }
        sipResend();
        uint32_t step = 500UL << dlg.retxN; if (step > 4000UL) step = 4000UL;
        dlg.retxAt = now + step; dlg.retxN++;
      }
      if ((int32_t)(now - dlg.t0) > (int32_t)CALL_DIAL_MS) callHangup();
      break;
    case CPH_ENDING:
      if ((int32_t)(now - dlg.retxAt) >= 0) {
        if (dlg.retxN >= 3 || (int32_t)(now - dlg.t0) > 4000) { callFinish(dlg.byeSent ? RPTR("Panggilan selesai") : RPTR("Dibatalkan")); break; }
        sipResend();
        dlg.retxAt = now + (500UL << dlg.retxN); dlg.retxN++;
      }
      break;
    case CPH_INCOMING:
      if ((int32_t)(now - dlg.t0) > (int32_t)CALL_RING_MS) callRejectIncoming(480, "Temporarily Unavailable", RPTR("Panggilan terlewat"));
      break;
    case CPH_ACTIVE:
      if (!dlg.uac && !dlg.ackd && (int32_t)(now - dlg.retxAt) >= 0) {   // ulangi 200 OK sampai ACK
        if (dlg.retxN >= 8) { callHangup(); break; }
        sipResend();
        uint32_t step = 500UL << dlg.retxN; if (step > 4000UL) step = 4000UL;
        dlg.retxAt = now + step; dlg.retxN++;
      }
      if ((int32_t)(now - lastRtpRx) > 45000) { callHangup(); }
      break;
    default: break;
  }
}

// ---------------------------------------------------------------------
// MEDIA: RTP TERIMA (jitter buffer kecil) + MIC KIRIM
// ---------------------------------------------------------------------
static void callEmit(const int16_t* pcm, int n) {
  if (n > CALL_SAMPLES) n = CALL_SAMPLES;
  memcpy(rxLast, pcm, (size_t)n * 2);
  {   // CALL_RXLEVEL_V1: puncak jendela 100 ms (satu paket = 20 ms, jadi tanpa jendela bar akan berkedip)
    int32_t pk = 0;
    for (int i = 0; i < n; i++) { int32_t a = pcm[i] < 0 ? -(int32_t)pcm[i] : (int32_t)pcm[i]; if (a > pk) pk = a; }
    if (pk > rxPkAcc) rxPkAcc = pk;
    uint32_t tn = millis();
    if ((int32_t)(tn - rxLvlMs) > 100) { rxLvlMs = tn; callRxLvl = (uint8_t)min(100, (int)(rxPkAcc * 100 / 12000)); rxPkAcc = 0; }
  }
  if (!aoDrop) callPcmOut(pcm, n);
}

static void callEmitLoss() {
  int16_t f[CALL_SAMPLES];
  rxLoss++;
  for (int i = 0; i < CALL_SAMPLES; i++) f[i] = (rxLoss == 1) ? (int16_t)(rxLast[i] / 2) : 0;   // PLC sederhana
  if (!aoDrop) callPcmOut(f, CALL_SAMPLES);
}

// keluarkan paket berurutan yg sudah tersimpan
static void callJbDrain() {
  for (;;) {
    int s = rxExpect % CALL_JB;
    if (!jbUsed[s] || jbSeq[s] != rxExpect) break;
    callEmit(jbPcm + s * 320, jbLen[s]);
    jbUsed[s] = false; rxExpect++;
  }
}

static void callOnRtp(const uint8_t* p, int len, const char* srcIp, int srcPort) {
  if (len < 13 || (p[0] >> 6) != 2) return;
  int off = 12 + (p[0] & 0x0F) * 4;
  if (p[0] & 0x10) { if (off + 4 > len) return; off += 4 + (((p[off + 2] << 8) | p[off + 3]) * 4); }
  int end = len;
  if (p[0] & 0x20) end -= p[len - 1];
  int pt = p[1] & 0x7F;
  if (pt != 0 && pt != 8) return;                                  // abaikan DTMF (101), CN (13), dll
  int n = end - off;
  if (n <= 0 || n > 320) return;
  uint16_t seq = (uint16_t)((p[2] << 8) | p[3]);
  lastRtpRx = millis();
  if (!rtpLatched) {   // symmetric RTP: ikuti alamat sumber sebenarnya
    IPAddress a; if (a.fromString(srcIp)) { rtpDest = a; rtpDestPort = srcPort; }
    rtpLatched = true;
  }
  int16_t pcm[320];
  for (int i = 0; i < n; i++) pcm[i] = (pt == 8) ? callAlaw2Lin(p[off + i]) : callUlaw2Lin(p[off + i]);
  if (!rxInit) { rxInit = true; rxExpect = seq; }
  int16_t diff = (int16_t)(seq - rxExpect);
  if (diff == 0) {
    callEmit(pcm, n); rxLoss = 0; rxExpect++;
    callJbDrain();
    rxGapAt = 0;
  } else if (diff > 0 && diff < CALL_JB) {
    int s = seq % CALL_JB;
    memcpy(jbPcm + s * 320, pcm, (size_t)n * 2);
    jbLen[s] = n; jbSeq[s] = seq; jbUsed[s] = true;
    if (!rxGapAt) rxGapAt = millis();
  } else if (diff < 0 && diff > -60) {
    // paket terlambat / duplikat: buang
  } else {
    rxExpect = (uint16_t)(seq + 1); callEmit(pcm, n); rxLoss = 0;                // loncatan besar: sinkron ulang
    memset(jbUsed, 0, sizeof(jbUsed)); rxGapAt = 0;
  }
}

// ada paket yg menunggu terlalu lama (yg hilang tak akan datang) -> isi gap, lanjut
static void callJbGapCheck(uint32_t now) {
  if (!rxGapAt || now - rxGapAt < 45UL) return;
  int guard = 0;
  while (guard++ < CALL_JB) {
    int s = rxExpect % CALL_JB;
    if (jbUsed[s] && jbSeq[s] == rxExpect) break;
    callEmitLoss(); rxExpect++;
  }
  callJbDrain();
  bool any = false;
  for (int i = 0; i < CALL_JB; i++) if (jbUsed[i]) any = true;
  rxGapAt = any ? now : 0;
}

static void callRtpRxStep(uint32_t now) {
  if (callRxLvl && (int32_t)(now - rxLvlMs) > 300) { callRxLvl = 0; rxPkAcc = 0; }   // CALL_RXLEVEL_V1
  int guard = 0;
  while (guard++ < 10) {
    int n = rtpUdp.parsePacket();
    if (n <= 0) break;
    int r = rtpUdp.read(rtpRx, 512);
    if (r <= 0) break;
    IPAddress ra = rtpUdp.remoteIP();
    if (turnOn && ra == turnSrv && (uint16_t)rtpUdp.remotePort() == turnPort) { callTurnOnPacket(rtpRx, r); continue; }
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned)ra[0], (unsigned)ra[1], (unsigned)ra[2], (unsigned)ra[3]);
    callOnRtp(rtpRx, r, ip, (int)rtpUdp.remotePort());
  }
  callJbGapCheck(now);
  callBacklogCheck();
}

static void callRtpSend(const uint8_t* g711, int n) {
  uint8_t pkt[12 + CALL_SAMPLES];
  pkt[0] = 0x80;
  pkt[1] = (uint8_t)((rtpFirst ? 0x80 : 0x00) | (dlg.pt == 8 ? 8 : 0));
  pkt[2] = (uint8_t)(rtpSeqTx >> 8); pkt[3] = (uint8_t)rtpSeqTx;
  pkt[4] = (uint8_t)(rtpTsTx >> 24); pkt[5] = (uint8_t)(rtpTsTx >> 16); pkt[6] = (uint8_t)(rtpTsTx >> 8); pkt[7] = (uint8_t)rtpTsTx;
  pkt[8] = (uint8_t)(rtpSsrc >> 24); pkt[9] = (uint8_t)(rtpSsrc >> 16); pkt[10] = (uint8_t)(rtpSsrc >> 8); pkt[11] = (uint8_t)rtpSsrc;
  memcpy(pkt + 12, g711, (size_t)n);
  rtpSeqTx++; rtpTsTx += (uint32_t)n; rtpFirst = false;
  if (turnOn) { turnSendData(pkt, 12 + n, rtpDest, rtpDestPort); return; }
  rtpUdp.beginPacket(rtpDest, (uint16_t)rtpDestPort);
  rtpUdp.write(pkt, (size_t)(12 + n));
  rtpUdp.endPacket();
}

// baca 20 ms dari mic (blocking ~20 ms), turunkan ke 8 kHz, encode, kirim
static void callMicStep(uint32_t now) {
  uint8_t out[CALL_SAMPLES];
  bool alaw = (dlg.pt == 8);
  if ((int32_t)(now - micYieldAt) > 1000) { micYieldAt = now; wakeYieldMic(4000); }
  size_t want = (size_t)(CALL_SAMPLES * 2) * 2 * sizeof(int32_t);          // 320 frame stereo 32-bit
  size_t got = micI2S.readBytes((char*)micRaw, want);
  int frames = (int)(got / (2 * sizeof(int32_t)));
  if (frames < CALL_SAMPLES * 2) {
    for (int i = 0; i < CALL_SAMPLES; i++) out[i] = alaw ? 0xD5 : 0xFF;     // mic macet: kirim senyap supaya waktu RTP tetap jalan
    callRtpSend(out, CALL_SAMPLES);
    return;
  }
  int oi = 0; int32_t pk = 0;
  for (int i = 0; i < CALL_SAMPLES * 2 && oi < CALL_SAMPLES; i++) {
    float x = (float)(int16_t)(micRaw[i * 2] >> MIC_SAMPLE_SHIFT);
    float y = x - micDcX + 0.995f * micDcY; micDcX = x; micDcY = y;         // buang DC
    // low-pass binomial 5-tap lalu ambil tiap sampel ke-2 (16 kHz -> 8 kHz)
    float o = (micH[0] + 4.0f * micH[1] + 6.0f * micH[2] + 4.0f * micH[3] + y) * (1.0f / 16.0f);
    micH[0] = micH[1]; micH[1] = micH[2]; micH[2] = micH[3]; micH[3] = y;
    if (((micPhase++) & 1) == 0) continue;
    float s = callMuted ? 0.0f : o * micGain;
    if (s > 30000.0f) s = 30000.0f; else if (s < -30000.0f) s = -30000.0f;
    int16_t v = (int16_t)s;
    int32_t a = v < 0 ? -v : v; if (a > pk) pk = a;
    out[oi++] = alaw ? callLin2Alaw(v) : callLin2Ulaw(v);
  }
  while (oi < CALL_SAMPLES) out[oi++] = alaw ? 0xD5 : 0xFF;
  callRtpSend(out, CALL_SAMPLES);
  if ((int32_t)(now - lastLvlMs) > 100) { lastLvlMs = now; callMicLvl = (uint8_t)min(100, (int)(pk * 100 / 12000)); }
}

// ---------------------------------------------------------------------
// TASK UTAMA
// ---------------------------------------------------------------------
static void callSnapshotCfg() {
  callLock();
  callCopy(tHost, sizeof(tHost), callHostS.c_str());
  callCopy(tUser, sizeof(tUser), callUserS.c_str());
  callCopy(tPass, sizeof(tPass), callPassS.c_str());
  callCopy(tName, sizeof(tName), callNameS.c_str());
  callUnlock();
  tPort = CALL_SIP_PORT;
  callCopy(tHostName, sizeof(tHostName), tHost);
  char* c = strchr(tHostName, ':');
  if (c) { tPort = atoi(c + 1); *c = 0; if (tPort <= 0) tPort = CALL_SIP_PORT; }
  tPortExplicit = (c != nullptr); tPortCfg = tPort;
  { char* s = strstr(tHostName, "sip:"); if (s == tHostName) memmove(tHostName, tHostName + 4, strlen(tHostName + 4) + 1); }
  tProxyName[0] = 0;                                // format "domain/proxy": pisahkan domain akun & server yang dihubungi
  { char* sl = strchr(tHostName, '/'); if (sl) { *sl = 0; callCopy(tProxyName, sizeof(tProxyName), sl + 1); } }
  srvOk = false; natIp[0] = 0; natPort = 0;
  tcpReset();
  regPending = false; regNextAt = 0; callRegState = 0;
  tCfgGen = callCfgGen;
}

static bool callAllocBuffers() {
  if (callBufOk) return true;
  const size_t total = (SIP_BUF * 3) + 512 + (CALL_SAMPLES * 4 * 4) + (CALL_JB * 320 * 2) + 64 + (SIP_BUF * 3);
  uint8_t* b = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) b = (uint8_t*)malloc(total);
  if (!b) return false;
  sipRx = (char*)b; sipTx = (char*)(b + SIP_BUF); sipRtx = (char*)(b + SIP_BUF * 2);
  rtpRx = b + SIP_BUF * 3;
  micRaw = (int32_t*)(b + SIP_BUF * 3 + 512);
  jbPcm  = (int16_t*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4);
  tcpBuf = (char*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4 + CALL_JB * 320 * 2 + 64);
  tcpTxBuf = tcpBuf + SIP_BUF * 2;
  callBufOk = true;
  return true;
}

static WiFiServer sipTcp(CALL_SIP_PORT);          // hanya utk diagnosa: mencatat sambungan TCP masuk

static void callTaskFunc(void*) {
  if (!callAllocBuffers()) { callTask = nullptr; vTaskDelete(nullptr); return; }
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (callPhase != CPH_IDLE) callFinish("WiFi terputus");
      tcpReset();
      if (sipOpen) { sipUdp.stop(); sipTcp.end(); sipOpen = false; }
      if (callRegState != 0) { callRegState = 0; callSetRegMsg(RPTR("WiFi belum tersambung")); regPending = false; regNextAt = 0; }
      srvOk = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!sipOpen) {
      callLocalIpStr(lip, sizeof(lip));
      sipUdp.begin(CALL_SIP_PORT);
      sipTcp.begin();
      sipOpen = true;
      regNextAt = 0;
    }
    if (tCfgGen != callCfgGen) callSnapshotCfg();

    callMarkT(30);
    callHandleReq();
    callMarkT(31);

    int guard = 0;
    while (guard++ < 6) {                                   // terima SIP
      int n = sipUdp.parsePacket();
      if (n <= 0) break;
      int r = sipUdp.read((uint8_t*)sipRx, SIP_BUF - 1);
      if (r <= 0) break;
      sipRx[r] = 0;
      IPAddress ra = sipUdp.remoteIP();
      char ip[16]; snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned)ra[0], (unsigned)ra[1], (unsigned)ra[2], (unsigned)ra[3]);
      callMarkT(60);
      callOnSip(sipRx, r, ip, (int)sipUdp.remotePort());
      callMarkT(61);
    }
    tcpPoll();                                              // SIP lewat TCP dari server

    {                                                       // DIAGNOSA: sambungan TCP masuk? (catat lalu tutup)
      WiFiClient tc = sipTcp.available();
      if (tc) {
        uint32_t t0 = millis(); char b[40]; int n = 0;
        while (millis() - t0 < 200 && n < (int)sizeof(b) - 1) {
          if (tc.available()) { int c = tc.read(); if (c < 0 || c == '\r' || c == '\n') break; b[n++] = (char)c; }
          else vTaskDelay(pdMS_TO_TICKS(5));
        }
        b[n] = 0;
        IPAddress ra = tc.remoteIP();
        char dg[40];
        snprintf(dg, sizeof(dg), "TCP %u.%u.%u.%u %s", (unsigned)ra[0], (unsigned)ra[1], (unsigned)ra[2], (unsigned)ra[3], b);
        callDbgPush(dg);
        tc.stop();
      }
    }

    now = millis();   // PENTING: callHandleReq()/callOnSip() bisa memakan waktu & mengisi dlg.t0/lastRtpRx dgn millis() yg lebih baru dari 'now' di atas
    callMarkT(70);
    callRegStep(now);
    callMarkT(71);
    callPhaseTimers(now);
    callMarkT(72);
    callTurnTick(now);

    if (callPhase == CPH_ACTIVE && mediaOn) {
      callMarkT(80);
      if (micOn) callMicStep(now);
      else {                                                // tanpa mic: tetap kirim senyap tiap 20 ms
        static uint32_t lastSil = 0;
        if (now - lastSil >= 20UL) {
          lastSil = now; uint8_t out[CALL_SAMPLES];
          for (int i = 0; i < CALL_SAMPLES; i++) out[i] = (dlg.pt == 8) ? 0xD5 : 0xFF;
          callRtpSend(out, CALL_SAMPLES);
        }
        vTaskDelay(pdMS_TO_TICKS(4));
      }
      callMarkT(82);
      callRtpRxStep(millis());
      callMarkT(83);
    } else {
      callMarkT(84);
      callToneStep(now);
      callMarkT(85);
      vTaskDelay(pdMS_TO_TICKS(callPhase == CPH_IDLE ? 15 : 6));
    }
  }
}

// =====================================================================
//  KONFIGURASI (NVS "sip" + /sip.cfg di SD)
// =====================================================================
static bool callOvl = false;            // true = layar panggilan menimpa semua (dering masuk / panggilan saat terkunci)
static bool callOvlLocked = false;
static uint8_t callPrevPhase = 0;
static bool callPillShown = false;
static bool callBanner = false;            // NYX-EXTRAS-V1: banner panggilan masuk (HP tidak terkunci)
static uint32_t callBannerT0 = 0;
static bool callBannerSupp = false;        // telan sisa sentuhan sampai jari lepas
static uint32_t callPollMs = 0, callTimerMs = 0;
static uint32_t callAnimMs = 0;           // CALL_UI_V2: redraw gelombang dering
static bool     callShowDiag = false;     // CALL_UI_V2: ketuk chip status = log diagnosa

static void callRecentSave() {
  String s = "";
  for (int i = 0; i < callRecentCount; i++) { s += callRecent[i]; s += "\n"; }
  Preferences p;
  if (p.begin("sip", false)) { p.putString("rec", s); p.end(); }
}

static void callRecentAdd(const char* who) {
  if (!who || !who[0]) return;
  int pos = -1;
  for (int i = 0; i < callRecentCount; i++) if (strcmp(callRecent[i], who) == 0) { pos = i; break; }
  if (pos < 0) { pos = (callRecentCount < CALL_REC_MAX) ? callRecentCount++ : CALL_REC_MAX - 1; }
  for (int i = pos; i > 0; i--) memcpy(callRecent[i], callRecent[i - 1], sizeof(callRecent[i]));
  callCopy(callRecent[0], sizeof(callRecent[0]), who);
  callRecentSave();
}

static void callCfgLoad() {
  Preferences p;
  if (p.begin("sip", false)) {
    callHostS = p.getString("host", "");
    callUserS = p.getString("user", "");
    callPassS = p.getString("pass", "");
    callNameS = p.getString("name", "");
    callStandby = p.getBool("standby", false);
    callTcpPref = p.getBool("tcp", CALL_TCP_DEFAULT != 0);
    callLanIpS = p.getString("lanip", "");
    callTurnHostS = p.getString("th", ""); callTurnUserS = p.getString("tu", "");
    callTurnPassS = p.getString("tw", ""); callTurnPortV = p.getInt("tp", 3478);
    String r = p.getString("rec", "");
    p.end();
    callRecentCount = 0;
    int st = 0;
    while (st < (int)r.length() && callRecentCount < CALL_REC_MAX) {
      int e = r.indexOf('\n', st);
      if (e < 0) e = r.length();
      if (e > st) { callCopy(callRecent[callRecentCount++], sizeof(callRecent[0]), r.substring(st, e).c_str()); }
      st = e + 1;
    }
  }
  if (!callNameS.length()) callNameS = "HP";
  callCfgLoaded = true;
}

static void callCfgSave() {
  callHostS.trim(); callUserS.trim(); callPassS.trim(); callNameS.trim(); callLanIpS.trim();
  callTurnHostS.trim(); callTurnUserS.trim(); callTurnPassS.trim();
  if (callNameS.length() > 20) callNameS = callNameS.substring(0, 20);
  if (!callNameS.length()) callNameS = "HP";
  callLock();
  Preferences p;
  if (p.begin("sip", false)) {
    p.putString("host", callHostS); p.putString("user", callUserS);
    p.putString("pass", callPassS); p.putString("name", callNameS);
    p.putBool("standby", callStandby); p.putString("lanip", callLanIpS);
    p.putBool("tcp", callTcpPref);
    p.putString("th", callTurnHostS); p.putString("tu", callTurnUserS);
    p.putString("tw", callTurnPassS); p.putInt("tp", callTurnPortV);
    p.end();
  }
  callUnlock();
  callCfgGen++;
}

// /turn.cfg di SD: host= port= user= pass=  (dibaca tiap app Telepon dibuka; kredensial Cloudflare berumur pendek)
static bool callTurnCfgFromSd() {
  if (!sdReady || !SD_MMC.exists("/turn.cfg")) return false;
  File f = SD_MMC.open("/turn.cfg", FILE_READ);
  if (!f) return false;
  String h = "", u = "", w = "", pt = "";
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.startsWith("host=")) h = l.substring(5);
    else if (l.startsWith("port=")) pt = l.substring(5);
    else if (l.startsWith("user=")) u = l.substring(5);
    else if (l.startsWith("pass=")) w = l.substring(5);
  }
  f.close();
  h.trim(); u.trim(); w.trim(); pt.trim();
  if (!h.length() || !u.length() || !w.length()) return false;
  callLock();
  callTurnHostS = h; callTurnUserS = u; callTurnPassS = w;
  callTurnPortV = pt.length() ? pt.toInt() : 3478;
  if (callTurnPortV <= 0) callTurnPortV = 3478;
  callUnlock();
  callLock();
  Preferences p;
  if (p.begin("sip", false)) {
    p.putString("th", h); p.putString("tu", u); p.putString("tw", w); p.putInt("tp", callTurnPortV);
    p.end();
  }
  callUnlock();
  return true;
}

static bool callSipConfigured() { return callHostS.length() > 3 && callUserS.length() > 0 && callPassS.length() > 0; }

static bool callCfgFromSd() {
  if (!sdReady || !SD_MMC.exists("/sip.cfg")) return false;
  File f = SD_MMC.open("/sip.cfg", FILE_READ);
  if (!f) return false;
  String h = "", u = "", w = "", n = "", tr = "";
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.startsWith("host=")) h = l.substring(5);
    else if (l.startsWith("transport=")) tr = l.substring(10);
    else if (l.startsWith("user=")) u = l.substring(5);
    else if (l.startsWith("pass=")) w = l.substring(5);
    else if (l.startsWith("name=")) n = l.substring(5);
  }
  f.close();
  h.trim(); u.trim(); w.trim(); n.trim();
  if (!h.length() || !u.length() || !w.length()) return false;
  callHostS = h; callUserS = u; callPassS = w;
  if (n.length()) callNameS = n;
  tr.trim(); if (tr.length()) callTcpPref = tr.equalsIgnoreCase("tcp");
  return true;
}

static bool callEnsureInit() {
  if (!callMx) callMx = xSemaphoreCreateMutex();
  if (!callMx) return false;
  if (!callCfgLoaded) callCfgLoad();
  musExtPrepare();                                  // link CAM harus dinyalakan di sini (UI thread), bukan di callTask
  if (callTask) return true;
  const uint32_t SZ = 20480;
  if (!callStack) callStack = (StackType_t*)heap_caps_aligned_alloc(16, SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  // RAM_OPT_V1: stack di PSRAM -> RAM internal cuma kepake TCB (~0,4 KB) + struct kecil, ambang 12 KB cukup.
  // Kalau stack PSRAM gagal, fallback stack di RAM internal tetap butuh blok 24 KB.
  if (!smEnsureContig(callStack ? 12000u : 24000u)) return false;
  if (callStack) callTask = xTaskCreateStaticPinnedToCore(callTaskFunc, "callTask", SZ, NULL, 3, callStack, &callTCB, 1);
  else {
    BaseType_t r = xTaskCreatePinnedToCore(callTaskFunc, "callTask", SZ, NULL, 3, &callTask, 1);
    if (r != pdPASS) callTask = nullptr;
  }
  return callTask != nullptr;
}

static void callPost(uint8_t r, const char* arg) {
  if (!callEnsureInit()) { showToast(RPTR("RAM tidak cukup utk Telepon"), 2200); return; }
  callLock(); callCopy(callReqArg, sizeof(callReqArg), arg ? arg : ""); callUnlock();
  callReq = r;
}

// ---- dipakai tombol earbuds (AVRCP) dari musicbt_renphone.ino. Fungsi global bertipe sederhana
//      (aturan auto-prototype arduino-cli), supaya tidak bergantung urutan penggabungan file ----
bool callKeyIncoming() { return callPhase == CPH_INCOMING; }
bool callKeyActive()   { return callPhase == CPH_ACTIVE; }
void callKeyAnswer()   { callPost(CRQ_ANSWER, ""); }
void callKeyReject()   { callPost(CRQ_REJECT, ""); }
void callKeyHangup()   { callPost(CRQ_HANGUP, ""); }

// =====================================================================
//  SIKLUS HIDUP & POLL (UI thread)
// =====================================================================
void callEnter() {
  callCaptureBoot();
  if (!callCfgLoaded) callCfgLoad();
  if (!callTurnHostS.length() && callTurnCfgFromSd()) showToast(RPTR("TURN dimuat dari /turn.cfg"), 1600);
  if (!callBootShown && callBootMsg[0]) { callBootShown = true; showToast(callBootMsg, 6000); callDbgPush(callBootMsg); }
  callEnsureInit();
  if (!callSipConfigured() && callCfgFromSd()) { callCfgSave(); showToast(RPTR("Akun VoIP dimuat dari SD"), 2000); }
  callPage = 0; callCfgEdit = -1;
  needRedraw = true;
}
void callExit() { callCfgEdit = -1; }

// ---------------------------------------------------------------------
//  BANNER PANGGILAN MASUK (NYX-EXTRAS-V1)
//  HP terbuka + ada panggilan: pil kecil di atas layar dgn tombol Tolak / Angkat,
//  app yang sedang dipakai tidak terganggu. Ketuk badan banner = layar penuh.
//  Terkunci -> tetap layar penuh seperti sebelumnya.
// ---------------------------------------------------------------------
// CALL_UI_V2 -- helper gambar bersama (tanda tangan hanya tipe primitif / String& / LGFX_Sprite&)
static bool callWide() { return SCR_W >= SCR_H * 13 / 10; }          // landscape: layout dua kolom

static uint16_t callAvatarColor(const char* nm) {
  uint32_t h = 5381;
  for (const char* p = nm; p && *p; p++) h = h * 33u + (uint8_t)*p;
  const uint16_t pal[6] = { (uint16_t)T().accent, (uint16_t)0xFD20, (uint16_t)0x34DF, (uint16_t)0xF9A6, (uint16_t)0x362B, (uint16_t)0xAB7F };
  return pal[h % 6];
}
static char callInitial(const char* nm) {
  for (const char* p = nm; p && *p; p++) if (isalnum((unsigned char)*p)) return (char)toupper((unsigned char)*p);
  return '?';
}
static void callAvatar(LGFX_Sprite& s, int cx, int cy, int r, const char* nm) {
  uint16_t col = callAvatarColor(nm);
  nyxAaCircle(s, cx, cy, r, col);
  if (r >= 14) nyxAaCircleRing(s, cx, cy, r, blend565(col, 0xFFFF, 60));
  int sz = r >= 28 ? 3 : (r >= 14 ? 2 : 1);
  char t[2] = { callInitial(nm), 0 };
  s.setTextSize(sz); s.setTextColor(appIconInk(col));
  s.setCursor(cx - s.textWidth(t) / 2, cy - (sz * 8) / 2); s.print(t);
  s.setTextSize(1);
}

// gagang telepon: busur tebal + dua bantalan. bulgeDeg = arah tonjolan (-90 = ke atas = "tutup", 135 = "angkat")
static void callHandset(LGFX_Sprite& s, int cx, int cy, int sz, int bulgeDeg, uint16_t col) {
  float b = bulgeDeg * 0.0174533f, R = sz * 0.85f, half = 62.0f * 0.0174533f;
  float ccx = cx - cosf(b) * R * 0.78f, ccy = cy - sinf(b) * R * 0.78f;
  int tb = sz * 22 / 100; if (tb < 2) tb = 2;
  for (int i = 0; i <= 12; i++) {
    float a = b - half + (2 * half) * i / 12.0f;
    nyxAaCircle(s, (int)lroundf(ccx + cosf(a) * R), (int)lroundf(ccy + sinf(a) * R), tb, col);
  }
  int pr = sz * 32 / 100; if (pr <= tb) pr = tb + 1;
  for (int e = 0; e < 2; e++) {
    float a = e ? b + half : b - half;
    float px = ccx + cosf(a) * R, py = ccy + sinf(a) * R;
    nyxAaCircle(s, (int)lroundf(px + (ccx - px) / R * sz * 0.16f), (int)lroundf(py + (ccy - py) / R * sz * 0.16f), pr, col);
  }
}

static void callIconMic(LGFX_Sprite& s, int cx, int cy, int r, bool slash, uint16_t col, uint16_t slashCol) {
  float u = r / 12.0f;
  int cw = (int)(4 * u); if (cw < 2) cw = 2;
  nyxAaRR(s, cx - cw, cy - (int)(9 * u), cw * 2, (int)(13 * u), cw, col);
  int x1 = cx - (int)(6 * u), x2 = cx + (int)(6 * u), yt = cy - (int)(2 * u), yb = cy + (int)(5 * u), c = (int)(3 * u);
  for (int k = 0; k < 2; k++) {
    s.drawFastVLine(x1 + k, yt, yb - yt - c, col); s.drawFastVLine(x2 - k, yt, yb - yt - c, col);
    s.drawLine(x1 + k, yb - c, x1 + c + k, yb, col); s.drawLine(x2 - k, yb - c, x2 - c - k, yb, col);
    s.drawFastHLine(x1 + c, yb + k, x2 - x1 - 2 * c, col);
    s.drawFastVLine(cx - k, yb, (int)(4 * u), col);
  }
  s.drawFastHLine(cx - (int)(4 * u), yb + (int)(4 * u), (int)(8 * u), col);
  if (slash) for (int k = 0; k < 3; k++) s.drawLine(cx - (int)(9 * u), cy - (int)(9 * u) + k, cx + (int)(9 * u), cy + (int)(9 * u) + k, slashCol);
}
static void callIconSpk(LGFX_Sprite& s, int cx, int cy, int r, int sign, uint16_t col) {   // pengeras suara + (+) atau (-)
  float u = r / 12.0f;
  int x0 = cx - (int)(11 * u), t = (int)(3 * u), l = (int)(4 * u);
  s.fillRect(x0, cy - t, (int)(4 * u), t * 2, col);
  s.fillTriangle(x0 + (int)(4 * u), cy - t, x0 + (int)(10 * u), cy - (int)(8 * u), x0 + (int)(10 * u), cy + (int)(8 * u), col);
  s.fillTriangle(x0 + (int)(4 * u), cy - t, x0 + (int)(4 * u), cy + t, x0 + (int)(10 * u), cy + (int)(8 * u), col);
  int px = cx + (int)(6 * u);
  for (int k = 0; k < 2; k++) {
    s.drawFastHLine(px - l, cy + k, l * 2, col);
    if (sign > 0) s.drawFastVLine(px - 1 + k, cy - l, l * 2, col);
  }
}
static void callIconGear(LGFX_Sprite& s, int cx, int cy, int r, uint16_t col, uint16_t hole) {
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f;
    nyxAaCircle(s, cx + (int)lroundf(cosf(a) * r), cy + (int)lroundf(sinf(a) * r), r / 4 + 1, col);
  }
  nyxAaCircle(s, cx, cy, r - 1, col);
  nyxAaCircle(s, cx, cy, r / 2, hole);
}

static void callRoundBtn(LGFX_Sprite& s, int cx, int cy, int r, uint16_t col) {
  nyxAaCircle(s, cx, cy + 2, r, blend565(T().bg, 0x0000, 110));
  nyxAaCircle(s, cx, cy, r, col);
  nyxAaCircleRing(s, cx, cy, r, blend565(col, 0xFFFF, 45));
}

// teks muat dalam maxW: ukuran pref, turun ke 1, lalu dipotong ".." ; mengembalikan ukuran terpakai (ukuran teks sprite ikut diset)
static int callFit(LGFX_Sprite& s, const char* t, int maxW, int pref, String& out) {
  out = t; int sz = pref;
  s.setTextSize(sz);
  if (s.textWidth(out.c_str()) > maxW && sz > 1) { sz = 1; s.setTextSize(1); }
  if (s.textWidth(out.c_str()) > maxW) {
    while (out.length() > 1 && s.textWidth((out + "..").c_str()) > maxW) out.remove(out.length() - 1);
    out += "..";
  }
  return sz;
}
// teks bungkus per kata; mengembalikan y setelah baris terakhir
static int callWrap(LGFX_Sprite& s, int x, int y, int w, const char* t, uint16_t col) {
  s.setTextSize(1); s.setTextColor(col);
  String line = "", word = "";
  int ly = y;
  for (const char* p = t; ; p++) {
    char c = *p;
    if (c == ' ' || c == 0) {
      String cand = line.length() ? (line + " " + word) : word;
      if (line.length() && s.textWidth(cand.c_str()) > w) { s.setCursor(x, ly); s.print(line.c_str()); ly += 11; line = word; }
      else line = cand;
      word = "";
      if (c == 0) break;
    } else word += c;
  }
  if (line.length()) { s.setCursor(x, ly); s.print(line.c_str()); ly += 11; }
  return ly;
}

// ---------------------------------------------------------------------
//  BANNER PANGGILAN MASUK (NYX-EXTRAS-V1, tampilan CALL_UI_V2)
//  HP terbuka + ada panggilan: kartu kecil di atas layar dgn avatar + tombol bulat Tolak / Angkat,
//  app yang sedang dipakai tidak terganggu. Ketuk badan banner = layar penuh.
//  Terkunci -> tetap layar penuh seperti sebelumnya.
// ---------------------------------------------------------------------
static void callBannerRect(int& x, int& y, int& w, int& h) {
  w = SCR_W - 16; if (w > 380) w = 380;
  h = 56; x = (SCR_W - w) / 2;
  uint32_t age = millis() - callBannerT0;
  float k = age >= 300 ? 1.0f : (float)age / 300.0f;
  float q = 1.0f - k;
  k = 1.0f - q * q * q;                                  // ease-out cubic
  y = (int)(-h + (h + 6) * k);                           // turun dari atas layar sampai y = 6
}
static void callBannerBtns(int& decCx, int& accCx, int& cyb, int& rb) {
  int x, y, w, h; callBannerRect(x, y, w, h);
  rb = 15; cyb = y + h / 2; accCx = x + w - 8 - rb; decCx = accCx - 2 * rb - 8;
}

bool callBannerActive() { return callBanner; }

void drawCallBanner(LGFX_Sprite& s) {
  if (!callBanner) return;
  int x, y, w, h; callBannerRect(x, y, w, h);
  char pe[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callUnlock();
  nyxAaRR(s, x + 1, y + 4, w, h, 18, blend565(T().bg, 0x0000, 150));          // bayangan
  nyxAaRR(s, x, y, w, h, 18, T().surface2);
  nyxAaRRRing(s, x, y, w, h, 18, blend565(T().surface2, T().text, 46));      // garis tipis
  int ax = x + 8 + 16, ay = y + h / 2;
  callAvatar(s, ax, ay, 16, pe);
  int decCx, accCx, cyb, rb; callBannerBtns(decCx, accCx, cyb, rb);
  int tx = ax + 16 + 8, maxW = decCx - rb - 6 - tx;
  String lb; callFit(s, callIsLan ? RPTR("Panggilan LAN") : RPTR("Panggilan masuk"), maxW, 1, lb);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, y + 11); s.print(lb.c_str());
  String nm; int sz = callFit(s, pe, maxW, 2, nm);
  s.setTextColor(T().text); s.setCursor(tx, y + (sz == 2 ? 26 : 30)); s.print(nm.c_str());
  s.setTextSize(1);
  callRoundBtn(s, decCx, cyb, rb, T().danger); callHandset(s, decCx, cyb, 9, -90, 0xFFFF);
  callRoundBtn(s, accCx, cyb, rb, T().good);   callHandset(s, accCx, cyb, 9, 135, 0xFFFF);
}

// true = sentuhan ditelan banner (jangan diteruskan ke app di bawahnya)
bool callBannerTouch(bool touched, bool newT, int tx, int ty) {
  if (!callBanner) { callBannerSupp = false; return false; }
  if (callBannerSupp) { if (!touched) callBannerSupp = false; return touched; }
  if (!touched) return false;
  int x, y, w, h; callBannerRect(x, y, w, h);
  bool inside = tx >= x && tx <= x + w && ty <= y + h;
  if (!inside) return false;
  if (!newT) return true;
  int decCx, accCx, cyb, rb; callBannerBtns(decCx, accCx, cyb, rb);
  long da = (long)(tx - accCx) * (tx - accCx) + (long)(ty - cyb) * (ty - cyb);
  long dd = (long)(tx - decCx) * (tx - decCx) + (long)(ty - cyb) * (ty - cyb);
  long lim = (long)(rb + 6) * (rb + 6);
  if (da <= lim || tx > accCx - rb)       { callPost(CRQ_ANSWER, ""); callBanner = false; }
  else if (dd <= lim || tx > decCx - rb)  { callPost(CRQ_REJECT, ""); callBanner = false; }
  else                                    { callBanner = false; callOvl = true; callOvlLocked = false; }
  callBannerSupp = true;
  vibTap();
  needRedraw = true;
  return true;
}

bool callOverlayActive() { return callOvl; }

static bool callGameBusy() {
  Screen sc = curScreen();
  return (sc == SCR_NES && nesIsPlaying()) || (sc == SCR_DOOM && doomIsPlaying());
}

void callLoopPoll() {
  callCaptureBoot();
  callMarkU(40);
  uint32_t now = millis();
  if (!callCfgLoaded && now > 3000) callCfgLoad();
  if (!callTask && callCfgLoaded && callStandby && wifiConnected && !airplaneMode && now - callPollMs > 5000) {
    callPollMs = now;
    callEnsureInit();
  }
  if (!callTask) return;

  if (callIncomingNew) {
    callIncomingNew = false;
    callMarkU(70);
    if (callGameBusy()) {
      callDbgPush("-> ditolak: NES/DOOM jalan");
      callPost(CRQ_REJECT, "");
    } else {
      if (!locked && curScreen() != SCR_CALL) {          // NYX-EXTRAS-V1: terbuka -> banner kecil; ketuk badan banner = layar penuh
        callBanner = true; callBannerT0 = millis(); callBannerSupp = false;
        callOvl = false; callOvlLocked = false;
      } else {
        callOvl = true; callOvlLocked = locked;
      }
      callMarkU(71);
      homeAodWake();
      callMarkU(72);
      callLastRingVib = 0;
      needRedraw = true;
    }
  }
  uint8_t ph = callPhase;
  if (ph == CPH_INCOMING && (callOvl || callBanner) && now - callLastRingVib > 2600) {
    callLastRingVib = now;
    static const VibStep rp[] = { {1, 350}, {0, 150}, {1, 350}, {0, 1} };
    callMarkU(73);
    vibPlayPattern(rp, 4);
    callMarkU(74);
  }
  if (ph == CPH_ACTIVE && callPrevPhase != CPH_ACTIVE) {
    vibSuccess();
    callBanner = false;
    if (callOvl && !callOvlLocked) {                  // diangkat saat tak terkunci -> jadi layar app biasa
      callOvl = false;
      if (curScreen() != SCR_CALL) navPush(SCR_CALL);
    }
  }
  if (callBanner && ph == CPH_INCOMING && locked) {      // layar terkunci saat berdering -> layar penuh
    callBanner = false; callOvl = true; callOvlLocked = true; needRedraw = true;
  }
  if (callBanner && ph != CPH_INCOMING) { callBanner = false; needRedraw = true; }
  callPrevPhase = ph;
  if (callEndedNew) {
    callEndedNew = false;
    callOvl = false; callOvlLocked = false; callBanner = false;
    char st[44]; callLock(); callCopy(st, sizeof(st), callStatus); callUnlock();
    char pe[44]; callLock(); callCopy(pe, sizeof(pe), callPeer); callUnlock();
    if (callPillShown) { diHide(); callPillShown = false; }
    if (strstr(st, "terlewat")) {
      String t = String("Tak terjawab: ") + pe;
      diNotify('p', t, 0xF9A6, 3500, false, String(""), SCR_CALL, true);
      notifAdd(SCR_CALL, 1);
      vibWarn();
    } else {
      showToast(st, 2600);                           // alasan panggilan berakhir: tampil juga saat sedang di halaman Telepon
    }
    needRedraw = true;
  }
  if (ph == CPH_ACTIVE && !callOvl && curScreen() != SCR_CALL && !callPillShown) {
    char pe[44]; callLock(); callCopy(pe, sizeof(pe), callPeer); callUnlock();
    String t = String("Panggilan: ") + pe;
    diNotify('p', t, 0x34DF, 3000, true, String(""), SCR_CALL, true);
    diLink(SCR_CALL);
    callPillShown = true;
  }
  if (callToastFlag) { callToastFlag = false; showToast(callToastBuf, 2200); }
  bool visible = callOvl || callBanner || curScreen() == SCR_CALL;
  if (callBanner && now - callBannerT0 < 360) needRedraw = true;   // animasi banner turun
  if (callDirty) { callDirty = false; if (visible) needRedraw = true; }
  if (visible && ph == CPH_ACTIVE && now - callTimerMs > 1000) { callTimerMs = now; needRedraw = true; }
  if (visible && ph == CPH_ACTIVE && now - callLvlDrawMs > 150) {   // CALL_RXLEVEL_V1
    int dm = (int)callMicLvl - (int)callShownMic, dr = (int)callRxLvl - (int)callShownRx;
    if (dm >= 6 || dm <= -6 || dr >= 6 || dr <= -6) { callLvlDrawMs = now; needRedraw = true; }
  }
  if (visible && callScanBusy && now - callTimerMs > 400) { callTimerMs = now; needRedraw = true; }
  if ((ph == CPH_DIAL || ph == CPH_RINGBACK || ph == CPH_INCOMING) && (callOvl || (!callBanner && curScreen() == SCR_CALL)) && now - callAnimMs > 110) {
    callAnimMs = now; needRedraw = true;            // CALL_UI_V2: gelombang berdenyut di sekitar avatar
  }
}

// =====================================================================
//  GAMBAR
// =====================================================================
static void callCenter(LGFX_Sprite& s, int x, int w, int y, const char* t) {
  s.setCursor(x + (w - s.textWidth(t)) / 2, y);
  s.print(t);
}

// ---------------------------------------------------------------------
//  LAYAR PANGGILAN (CALL_UI_V2): hero (avatar, nama, status/timer, meter) + tombol bulat
//  Portrait: hero di atas, tombol di bawah. Landscape: hero di kiri, tombol di kanan.
// ---------------------------------------------------------------------
static void callAreaY(int& top, int& bot) { top = STATUS_H + 4; bot = callOvl ? SCR_H - 10 : backY() - 6; }

static void callActGeom(int& ax, int& aw, int& y0, int& cr, int& pillH) {
  int top, bot; callAreaY(top, bot);
  bool wide = callWide();
  if (wide) { ax = SCR_W * 46 / 100; aw = SCR_W - ax - 10; } else { ax = 8; aw = SCR_W - 16; }
  bool inc = callPhase == CPH_INCOMING;
  cr = (aw - 20) / 6; if (cr > 24) cr = 24; if (cr < 16) cr = 16;
  pillH = 34;
  if (!wide && bot - top < 250) { if (cr > 20) cr = 20; pillH = 30; }
  int blockH = inc ? ((cr + 8) * 2 + 6 + 10) : (cr * 2 + 6 + 10 + 10 + pillH);
  if (wide) y0 = top + ((bot - top) - blockH) / 2; else y0 = bot - blockH;
  if (y0 < top) y0 = top;
}

// idx 0..1 (masuk: Tolak, Terima) / 0..2 bulat (Mute, Vol-, Vol+) + 3 pil (Akhiri). w = lebar sentuh
static bool callBtnGeom(int idx, int& cx, int& cy, int& r, int& w) {
  int ax, aw, y0, cr, pillH; callActGeom(ax, aw, y0, cr, pillH);
  int mid = ax + aw / 2;
  if (callPhase == CPH_INCOMING) {
    if (idx > 1) return false;
    int rr = cr + 8, sp = aw / 2; if (sp > 2 * rr + 30) sp = 2 * rr + 30;
    cx = mid + (idx ? sp / 2 : -sp / 2); cy = y0 + rr; r = rr; w = rr * 2;
    return true;
  }
  if (idx > 3) return false;
  if (idx < 3) {
    int sp = aw / 3; if (sp > 2 * cr + 18) sp = 2 * cr + 18;
    cx = mid + (idx - 1) * sp; cy = y0 + cr; r = cr; w = cr * 2;
    return true;
  }
  w = aw * 85 / 100; if (w > 220) w = 220;
  r = pillH / 2; cx = mid; cy = y0 + cr * 2 + 6 + 10 + 10 + pillH / 2;
  return true;
}

static void callBtnLabel(LGFX_Sprite& s, int cx, int cy, int r, const char* t) {
  s.setTextSize(1); s.setTextColor(T().subtext);
  s.setCursor(cx - s.textWidth(t) / 2, cy + r + 5); s.print(t);
}

static void callDrawInCall(LGFX_Sprite& s) {
  char pe[44], st[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callCopy(st, sizeof(st), callStatus); callUnlock();
  uint8_t ph = callPhase;
  bool act = ph == CPH_ACTIVE;
  bool ringing = ph == CPH_DIAL || ph == CPH_RINGBACK || ph == CPH_INCOMING;
  int top, bot; callAreaY(top, bot);
  int ax, aw, y0, cr, pillH; callActGeom(ax, aw, y0, cr, pillH);
  int hw, hy0, hy1;
  if (callWide()) { hw = ax - 4; hy0 = top; hy1 = bot; } else { hw = SCR_W; hy0 = top; hy1 = y0 - 8; }
  int hcx = hw / 2;
  // ukuran avatar menyesuaikan tinggi yang tersedia
  int avail = (hy1 - hy0) - (ringing ? 10 : 0);
  bool showRoute = true;
  int rA = (avail - 64 - (act ? 26 : 0)) / 2;
  if (rA < 18) { showRoute = false; rA = (avail - 42 - (act ? 26 : 0)) / 2; }
  if (rA > 36) rA = 36;
  if (rA < 14) rA = 14;
  int total = 2 * rA + 8 + 16 + 2 + 16 + (act ? 26 : 0) + (showRoute ? 22 : 0);
  int y = hy0 + ((hy1 - hy0) - total) / 2; if (y < hy0) y = hy0;
  int cyA = y + rA;
  if (ringing) {                                             // gelombang berdenyut
    uint32_t t = millis() % 1800;
    for (int k = 0; k < 2; k++) {
      uint32_t tk = (t + k * 900) % 1800;
      nyxAaCircleRing(s, hcx, cyA, rA + 3 + (int)(tk * 12 / 1800), blend565(T().bg, T().accent, (uint8_t)(130 - tk * 120 / 1800)));
    }
  }
  if (act) {                                                 // cincin status: hijau = tersambung, merah = mute
    uint16_t rc = callMuted ? (uint16_t)T().danger : (uint16_t)T().good;
    nyxAaCircleRing(s, hcx, cyA, rA + 3, rc); nyxAaCircleRing(s, hcx, cyA, rA + 4, rc);
  }
  callAvatar(s, hcx, cyA, rA, pe);
  y = cyA + rA + 8;
  String nm; int sz = callFit(s, pe, hw - 16, 2, nm);
  s.setTextColor(T().text); s.setCursor(hcx - s.textWidth(nm.c_str()) / 2, y + (sz == 2 ? 0 : 4)); s.print(nm.c_str());
  s.setTextSize(1);
  y += 18;
  if (act) {
    uint32_t sec = (millis() - callStartMs) / 1000UL;
    char line[16]; snprintf(line, sizeof(line), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    s.setTextSize(2); s.setTextColor(T().good);
    s.setCursor(hcx - s.textWidth(line) / 2, y); s.print(line);
    s.setTextSize(1);
  } else {
    String ss; callFit(s, ph == CPH_INCOMING ? (callIsLan ? RPTR("Panggilan LAN") : RPTR("Panggilan masuk")) : st, hw - 16, 1, ss);
    s.setTextColor(T().accent); s.setCursor(hcx - s.textWidth(ss.c_str()) / 2, y + 4); s.print(ss.c_str());
  }
  y += 24;
  if (act) {                                                 // CALL_RXLEVEL_V1: meter lawan (atas) + mic sendiri (bawah)
    int bw = hw - 16 - 36; if (bw > 110) bw = 110;
    int x0 = hcx - (36 + bw) / 2, bx = x0 + 36;
    uint8_t rxl = callRxLvl, mcl = callMicLvl;
    callShownRx = rxl; callShownMic = mcl;
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(x0, y);     s.print(RPTR("Lawan"));
    s.setCursor(x0, y + 10); s.print(RPTR("Kamu"));
    nyxAaRR(s, bx, y + 1, bw, 6, 3, blend565(T().surface2, T().text, 30));
    int fr = bw * rxl / 100; if (fr > 5) nyxAaRR(s, bx, y + 1, fr, 6, 3, (uint16_t)T().accent);
    nyxAaRR(s, bx, y + 11, bw, 6, 3, blend565(T().surface2, T().text, 30));
    int fm = bw * mcl / 100; if (fm > 5) nyxAaRR(s, bx, y + 11, fm, 6, 3, callMuted ? (uint16_t)T().danger : (uint16_t)T().good);
    y += 26;
  }
  if (showRoute) {
    char rt[40]; bool bad = false;
    if (act) {
      if (callAudioOut) snprintf(rt, sizeof(rt), "%s \xC2\xB7 %s", callIsLan ? "LAN" : "VoIP", aoSpk ? "Speaker" : "TWS");
      else { callCopy(rt, sizeof(rt), RPTR("Tanpa audio keluar (TWS?)")); bad = true; }
    } else callCopy(rt, sizeof(rt), callIsLan ? RPTR("LAN langsung") : "VoIP");
    String rts; callFit(s, rt, hw - 28, 1, rts);
    s.setTextSize(1);
    int tw = s.textWidth(rts.c_str()), cw = tw + 20;
    nyxAaRR(s, hcx - cw / 2, y, cw, 15, 7, bad ? blend565(T().bg, T().danger, 70) : blend565(T().bg, T().surface2, 200));
    s.setTextColor(bad ? (uint16_t)T().danger : (uint16_t)T().subtext);
    s.setCursor(hcx - tw / 2, y + 4); s.print(rts.c_str());
  }
  // tombol
  int cx, cy, r, w;
  if (ph == CPH_INCOMING) {
    callBtnGeom(0, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().danger); callHandset(s, cx, cy, r * 55 / 100, -90, 0xFFFF);
    callBtnLabel(s, cx, cy, r, RPTR("Tolak"));
    callBtnGeom(1, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().good);   callHandset(s, cx, cy, r * 55 / 100, 135, 0xFFFF);
    callBtnLabel(s, cx, cy, r, RPTR("Terima"));
  } else {
    callBtnGeom(0, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, callMuted ? (uint16_t)T().accent : (uint16_t)T().surface2);
    callIconMic(s, cx, cy, r * 6 / 10, callMuted, callMuted ? appIconInk(T().accent) : (uint16_t)T().text, (uint16_t)T().danger);
    callBtnLabel(s, cx, cy, r, callMuted ? "Unmute" : "Mute");
    callBtnGeom(1, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().surface2); callIconSpk(s, cx, cy, r * 6 / 10, -1, T().text);
    callBtnLabel(s, cx, cy, r, "Vol -");
    callBtnGeom(2, cx, cy, r, w);
    callRoundBtn(s, cx, cy, r, T().surface2); callIconSpk(s, cx, cy, r * 6 / 10, +1, T().text);
    callBtnLabel(s, cx, cy, r, "Vol +");
    callBtnGeom(3, cx, cy, r, w);
    nyxAaRR(s, cx - w / 2, cy - r + 2, w, r * 2, r, blend565(T().bg, 0x0000, 110));
    nyxAaRR(s, cx - w / 2, cy - r, w, r * 2, r, T().danger);
    const char* lb = ph == CPH_ENDING ? "..." : RPTR("Akhiri");
    s.setTextSize(1); s.setTextColor(0xFFFF);
    int tw = s.textWidth(lb), gw = 16 + 8 + tw, gx = cx - gw / 2;
    callHandset(s, gx + 8, cy, 8, -90, 0xFFFF);
    s.setCursor(gx + 24, cy - 4); s.print(lb);
  }
}

void drawCallOverlay(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false);
  callMarkU(75);
  callDrawInCall(s);
  callMarkU(76);
  drawToast(s);
}

static const char* callCfgLabel(int i) {
  static const char* L[7] = { "Server VoIP (mis. sip.linphone.org)", "Username VoIP", "Password VoIP", "Nama tampilan",
                              "Server TURN (host atau host:port)", "Username TURN", "Password TURN" };
  return RPTR(L[i]);   // I18N: array statis -> terjemahkan saat dipakai
}
static String* callCfgField(int i) {
  switch (i) {
    case 0: return &callHostS;     case 1: return &callUserS;     case 2: return &callPassS;
    case 4: return &callTurnHostS; case 5: return &callTurnUserS; case 6: return &callTurnPassS;
    default: return &callNameS;
  }
}

static bool callSdHas(const char* p) { return sdReady && SD_MMC.exists(p); }

// ---------------------------------------------------------------------
//  PENGATURAN (CALL_UI_V2): baris label-kiri / nilai-kanan, tombol Simpan di baris bawah
//  baris 0..3 = akun / 0..2 = TURN, 4 = Siaga (hal. 1) atau Muat SD (hal. 2), 5 = TURN >, 6 = Muat SD (hal. 1)
// ---------------------------------------------------------------------
static void callCfgRowRect(int i, int& x, int& y, int& w, int& h) {
  bool wd = callWide();
  h = wd ? (SCR_H < 260 ? 26 : 32) : (SCR_H < 400 ? 28 : 32);
  int gap = wd ? 4 : 3, top = 50;
  if (callWide()) {
    int cw = (SCR_W - 24) / 2;
    w = cw;
    if (i < 4) { x = 8; y = top + i * (h + gap); } else { x = 16 + cw; y = top + (i - 4) * (h + gap); }
  } else {
    w = SCR_W - 16; x = 8;
    y = top + i * (h + gap) + (i >= 4 ? 8 : 0);
  }
}
static int callTurnSdIdx() { return callWide() ? 4 : 3; }      // baris "Muat SD" di halaman TURN
static void callSaveRect(int& x, int& y, int& w, int& h) { w = 92; h = BACK_H; x = SCR_W - 8 - w; y = backY(); }

static void callCfgRowDraw(LGFX_Sprite& s, int i, const char* label, const char* val, bool secret, bool chev) {
  int x, y, w, h; callCfgRowRect(i, x, y, w, h);
  iosRR(s, x, y, w, h, 10, T().surface);
  s.setTextSize(1); s.setTextColor(T().text);
  int ty = y + (h - 8) / 2;
  s.setCursor(x + 10, ty); s.print(label);
  int lw = s.textWidth(label);
  int rx = x + w - (chev ? 22 : 10);
  if (chev) iosChevron(s, x + w - 12, y + h / 2, T().subtext);
  if (secret) {
    int n = (int)strlen(val); if (n > 8) n = 8;
    if (n == 0) { s.setTextColor(T().subtext); s.setCursor(rx - s.textWidth("-"), ty); s.print("-"); return; }
    for (int k = 0; k < n; k++) nyxAaCircle(s, rx - 3 - k * 8, y + h / 2, 2, T().subtext);
    return;
  }
  String v; callFit(s, val[0] ? val : "-", w - 20 - lw - 12 - (chev ? 12 : 0), 1, v);
  s.setTextColor(T().subtext); s.setCursor(rx - s.textWidth(v.c_str()), ty); s.print(v.c_str());
}

static void callDrawSaveBtn(LGFX_Sprite& s) {
  int x, y, w, h; callSaveRect(x, y, w, h);
  nyxAaRR(s, x, y, w, h, h / 2, T().accent);
  s.setTextSize(1); s.setTextColor(appIconInk(T().accent));
  callCenter(s, x, w, y + (h - 8) / 2, RPTR("Simpan"));
}

static void callDrawCfgTitle(LGFX_Sprite& s, const char* t) {
  String ts; int sz = callFit(s, t, SCR_W - 16, 2, ts);
  s.setTextColor(T().text); s.setCursor(10, 26 + (sz == 2 ? 0 : 4)); s.print(ts.c_str());
  s.setCursor(11, 26 + (sz == 2 ? 0 : 4)); s.print(ts.c_str());
  s.setTextSize(1);
}

static void callDrawEdit(LGFX_Sprite& s) {
  callDrawCfgTitle(s, callPage == 2 ? RPTR("Telepon - TURN (relay)") : RPTR("Telepon - Pengaturan"));
  String lb; callFit(s, callCfgLabel(callCfgEdit), SCR_W - 20, 1, lb);
  s.setTextColor(T().subtext); s.setCursor(12, 50); s.print(lb.c_str());
  iosRR(s, 8, 64, SCR_W - 16, 34, 10, T().surface);
  nyxAaRRRing(s, 8, 64, SCR_W - 16, 34, 10, T().accent);
  String v = *callCfgField(callCfgEdit);
  int maxW = SCR_W - 16 - 20;
  int sz = 2; s.setTextSize(2);
  if (s.textWidth(v.c_str()) + 14 > maxW) { sz = 1; s.setTextSize(1); }
  while (v.length() > 3 && s.textWidth(v.c_str()) + 14 > maxW) v = v.substring(1);
  v += "_";
  s.setTextColor(T().text); s.setCursor(18, 64 + (34 - sz * 8) / 2); s.print(v.c_str());
  s.setTextSize(1);
  s.setTextColor(T().subtext); callWrap(s, 12, 106, SCR_W - 24, RPTR("Ketuk area di atas keyboard = selesai"), T().subtext);
}

static void callDrawTurnCfg(LGFX_Sprite& s) {
  callDrawCfgTitle(s, RPTR("Telepon - TURN (relay)"));
  const char* L[3] = { "Server", "Username", "Password" };
  for (int i = 0; i < 3; i++) callCfgRowDraw(s, i, L[i], callCfgField(4 + i)->c_str(), i == 2, true);
  bool sd = callSdHas("/turn.cfg");
  int x, y, w, h;
  int si = callTurnSdIdx();
  callCfgRowRect(si, x, y, w, h);
  if (sd) {
    iosRR(s, x, y, w, h, 10, T().surface2);
    s.setTextColor(T().accent); callCenter(s, x, w, y + (h - 8) / 2, RPTR("Muat SD"));
  }
  int hx = x + 2, hwid = w - 4, hy = y + (sd ? h + 10 : 2);
  hy = callWrap(s, hx, hy, hwid, RPTR("Dipakai utk panggilan lewat internet."), T().subtext);
  hy = callWrap(s, hx, hy, hwid, RPTR("Kosong = pakai STUN saja."), T().subtext);
  if (turnErr[0]) callWrap(s, hx, hy + 2, hwid, turnErr, T().danger);
}

static void callDrawCfg(LGFX_Sprite& s) {
  if (callCfgEdit >= 0) { callDrawEdit(s); return; }
  if (callPage == 2) { callDrawTurnCfg(s); return; }
  callDrawCfgTitle(s, RPTR("Telepon - Pengaturan"));
  const char* L[4] = { "Server", "Username", "Password", RPTR("Nama tampilan") };
  for (int i = 0; i < 4; i++) callCfgRowDraw(s, i, L[i], callCfgField(i)->c_str(), i == 2, true);
  int x, y, w, h;
  callCfgRowRect(4, x, y, w, h);                                   // Siaga: saklar
  iosRR(s, x, y, w, h, 10, T().surface);
  String lb; callFit(s, RPTR("Siaga terima panggilan"), w - 20 - 38, 1, lb);
  s.setTextSize(1); s.setTextColor(T().text); s.setCursor(x + 10, y + (h - 8) / 2); s.print(lb.c_str());
  int sx = x + w - 10 - 30, sy = y + (h - 16) / 2;
  nyxAaRR(s, sx, sy, 30, 16, 8, callStandby ? (uint16_t)T().good : blend565(T().surface2, T().text, 40));
  nyxAaCircle(s, callStandby ? sx + 22 : sx + 8, sy + 8, 6, 0xFFFF);
  callCfgRowDraw(s, 5, "TURN (relay)", callTurnHostS.c_str(), false, true);
  if (callSdHas("/sip.cfg")) {
    callCfgRowRect(6, x, y, w, h);
    iosRR(s, x, y, w, h, 10, T().surface2);
    s.setTextColor(T().accent); callCenter(s, x, w, y + (h - 8) / 2, RPTR("Muat SD"));
  }
}

static void callRegText(char* out, int cap, uint16_t* col) {
  uint8_t st = callRegState;
  if (st == 2) { callCopy(out, cap, "VoIP terdaftar"); *col = T().good; }
  else if (st == 1) { callCopy(out, cap, "mendaftar..."); *col = T().subtext; }
  else if (st == 3) { char m[40]; callLock(); callCopy(m, sizeof(m), callRegMsg); callUnlock(); callCopy(out, cap, m); *col = T().danger; }
  else { callCopy(out, cap, callSipConfigured() ? RPTR("VoIP belum aktif") : RPTR("VoIP belum diatur")); *col = T().subtext; }
}

// ---------------------------------------------------------------------
//  LAYAR UTAMA TELEPON (CALL_UI_V2)
// ---------------------------------------------------------------------
#define CALL_TITLE_Y 26
#define CALL_CHIP_Y  44
#define CALL_SEG_Y   62
#define CALL_FIELD_Y 88
#define CALL_BTN_Y   124

static void callIdleGeom(int& lx, int& lw, int& rx, int& rw, int& hdrY, int& rowY0, int& pitch, int& cardH) {
  lx = 8;
  if (callWide()) { lw = SCR_W / 2 - 12; rx = SCR_W / 2 + 4; rw = SCR_W - rx - 8; hdrY = 30; rowY0 = 44; }
  else { lw = SCR_W - 16; rx = 8; rw = SCR_W - 16; hdrY = 160; rowY0 = 172; }
  bool big = SCR_H >= 400;
  pitch = big ? 46 : 32; cardH = big ? 42 : 28;
}
static int callIdleSlots() {
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  int bot = kbVisible ? kbY() - 2 : backY() - 4;
  int n = (bot - rowY0 + (pitch - cardH)) / pitch;
  return n < 0 ? 0 : n;
}

static void callDrawRow(LGFX_Sprite& s, int x, int y, int w, int h, const char* full, const char* sub) {
  iosRR(s, x, y, w, h, 12, T().surface);
  int r = h / 2 - 5; if (r < 9) r = 9;
  int cyr = y + h / 2;
  callAvatar(s, x + 8 + r, cyr, r, full);
  int bx = x + w - 8 - r;
  nyxAaCircle(s, bx, cyr, r, T().good);
  callHandset(s, bx, cyr, r * 6 / 10 + 1, 135, 0xFFFF);
  int tx = x + 8 + 2 * r + 8, maxW = bx - r - 6 - tx;
  char nm[44]; const char* at = strchr(full, '@');
  int nl = at ? (int)(at - full) : (int)strlen(full); if (nl > 43) nl = 43;
  memcpy(nm, full, nl); nm[nl] = 0;
  const char* dom = at ? at + 1 : (sub ? sub : "");
  String a; callFit(s, nm, maxW, 1, a);
  s.setTextSize(1); s.setTextColor(T().text);
  if (dom[0]) {
    s.setCursor(tx, cyr - 9); s.print(a.c_str());
    String b; callFit(s, dom, maxW, 1, b);
    s.setTextColor(T().subtext); s.setCursor(tx, cyr + 2); s.print(b.c_str());
  } else { s.setCursor(tx, cyr - 4); s.print(a.c_str()); }
}

void drawCall(LGFX_Sprite& s) {
  callMarkU(41);
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);
  if (!callTask && !callEnsureInit()) {
    s.setTextColor(T().danger); s.setCursor(10, 60); s.print(RPTR("RAM tidak cukup untuk Telepon."));
    drawBack(s); drawToast(s); return;
  }
  if (callPhase != CPH_IDLE) {
    callDrawInCall(s);
    drawBack(s); drawToast(s); return;
  }
  if (callPage == 1 || callPage == 2) {
    callDrawCfg(s);
    if (kbVisible) drawKb(s); else { callDrawSaveBtn(s); drawBack(s); }
    drawToast(s); return;
  }
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  // judul + tombol pengaturan
  s.setTextSize(2); uiText(s, lx + 2, CALL_TITLE_Y, RPTR("Telepon"), T().text, false, true); s.setTextSize(1);
  nyxAaCircle(s, lx + lw - 14, CALL_TITLE_Y + 8, 14, T().surface2);
  nyxAaCircleRing(s, lx + lw - 14, CALL_TITLE_Y + 8, 14, blend565(T().surface2, T().text, 40));
  callIconGear(s, lx + lw - 14, CALL_TITLE_Y + 8, 8, T().accent, T().surface2);
  // chip status
  char ct[44]; uint16_t cc;
  if (callTab == 0) callRegText(ct, sizeof(ct), &cc);
  else if (!wifiConnected) { callCopy(ct, sizeof(ct), RPTR("WiFi belum tersambung")); cc = T().danger; }
  else { char ip[20]; callLocalIpStr(ip, sizeof(ip)); snprintf(ct, sizeof(ct), "IP: %s", ip); cc = T().good; }
  String cs; callFit(s, ct, lw - 44, 1, cs);
  int cw = s.textWidth(cs.c_str()) + 24;
  nyxAaRR(s, lx, CALL_CHIP_Y, cw, 15, 7, blend565(T().bg, cc, 45));
  nyxAaCircle(s, lx + 9, CALL_CHIP_Y + 7, 2, cc);
  s.setTextColor(cc == (uint16_t)T().danger ? (uint16_t)T().danger : (uint16_t)T().text); s.setCursor(lx + 17, CALL_CHIP_Y + 4); s.print(cs.c_str());
  // tab VoIP | LAN
  nyxAaRR(s, lx, CALL_SEG_Y, lw, 22, 11, T().surface2);
  int sw = (lw - 4) / 2;
  nyxAaRR(s, lx + 2 + callTab * sw, CALL_SEG_Y + 2, sw, 18, 9, T().accent);
  for (int i = 0; i < 2; i++) {
    s.setTextColor(callTab == i ? appIconInk(T().accent) : (uint16_t)T().subtext);
    callCenter(s, lx + 2 + i * sw, sw, CALL_SEG_Y + 7, i == 0 ? "VoIP" : (sw >= 96 ? RPTR("LAN (antar HP)") : "LAN"));
  }
  // kolom tujuan
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  bool typing = kbVisible && kbTarget == tgt;
  bool has = tgt->length() > 0;
  iosRR(s, lx, CALL_FIELD_Y, lw, 32, 10, T().surface);
  if (typing) nyxAaRRRing(s, lx, CALL_FIELD_Y, lw, 32, 10, T().accent);
  int maxVW = lw - 20 - (has ? 24 : 0);
  String cap; callFit(s, callTab == 0 ? RPTR("Tujuan (user, user@domain)") : RPTR("Alamat IP HP lawan"), maxVW, 1, cap);
  s.setTextColor(T().subtext); s.setCursor(lx + 10, CALL_FIELD_Y + 4); s.print(cap.c_str());
  String v = *tgt; if (typing) v += "_";
  if (!v.length()) {
    s.setTextColor(T().subtext); s.setCursor(lx + 10, CALL_FIELD_Y + 18);
    s.print(callTab == 0 ? RPTR("ketuk untuk mengetik...") : "mis. 192.168.1.23");
  } else {
    int sz = 2; s.setTextSize(2);
    if (s.textWidth(v.c_str()) > maxVW) { sz = 1; s.setTextSize(1); }
    if (typing) { while (v.length() > 3 && s.textWidth(v.c_str()) > maxVW) v = v.substring(1); }       // mengetik: ujung kanan tetap terlihat
    else if (sz == 1) { String vv; callFit(s, v.c_str(), maxVW, 1, vv); v = vv; }                      // diam: awal tetap terlihat
    s.setTextColor(T().text); s.setCursor(lx + 10, CALL_FIELD_Y + (sz == 2 ? 14 : 18)); s.print(v.c_str());
    s.setTextSize(1);
  }
  if (has) {                                                        // tombol hapus
    int bx = lx + lw - 14, by = CALL_FIELD_Y + 16;
    nyxAaCircle(s, bx, by, 8, blend565(T().surface2, T().text, 40));
    s.drawLine(bx - 3, by - 3, bx + 3, by + 3, T().text); s.drawLine(bx - 3, by + 3, bx + 3, by - 3, T().text);
  }
  // tombol Panggil
  nyxAaRR(s, lx, CALL_BTN_Y, lw, 30, 15, has ? (uint16_t)T().good : blend565(T().surface2, T().good, 80));
  { const char* lb = RPTR("Panggil"); s.setTextColor(has ? (uint16_t)0xFFFF : (uint16_t)T().subtext);
    int gw = 16 + 8 + s.textWidth(lb), gx = lx + (lw - gw) / 2;
    callHandset(s, gx + 8, CALL_BTN_Y + 15, 9, 135, has ? (uint16_t)0xFFFF : (uint16_t)T().subtext);
    s.setCursor(gx + 24, CALL_BTN_Y + 11); s.print(lb); }
  // daftar: riwayat (VoIP) atau perangkat ditemukan (LAN)
  s.setTextColor(T().subtext); s.setCursor(rx + 4, hdrY);
  s.print(callTab == 0 ? RPTR("Terakhir") : RPTR("Perangkat"));
  int slots = callIdleSlots();
  int items = callTab == 0 ? callRecentCount : 1 + callScanCount;
  if (items > slots) items = slots;
  for (int k = 0; k < items; k++) {
    int ry = rowY0 + k * pitch;
    if (callTab == 0) callDrawRow(s, rx, ry, rw, cardH, callRecent[k], "");
    else if (k == 0) {
      iosRR(s, rx, ry, rw, cardH, 12, T().surface2);
      String sl; callFit(s, callScanBusy ? RPTR("Mencari...") : RPTR("Cari HP NyxOS di jaringan"), rw - 20, 1, sl);
      s.setTextColor(T().accent); callCenter(s, rx, rw, ry + (cardH - 8) / 2, sl.c_str());
    } else callDrawRow(s, rx, ry, rw, cardH, callScanName[k - 1], callScanIp[k - 1]);
  }
  if (callTab == 0 && callRecentCount == 0) callWrap(s, rx + 4, rowY0 + 4, rw - 8, RPTR("Belum ada riwayat panggilan"), T().subtext);
  if (callShowDiag && !kbVisible && callDbgHist[0][0]) {
    int y0 = backY() - 6 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 6 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 6; i++) {
      if (!callDbgHist[5 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[5 - i]);
    }
  }
  if (kbVisible) drawKb(s); else drawBack(s);
  drawToast(s);
}

// =====================================================================
//  SENTUHAN
// =====================================================================
static void callStartTyping(String* t) {
  kbTarget = t; kbVisible = true; kbMode = KB_LOWER; needRedraw = true;
}

static void callDoDial(const String& target, bool remember) {
  String t = target; t.trim();
  if (!t.length()) { showToast(RPTR("Isi tujuan dulu"), 1500); return; }
  if (!wifiConnected) { showToast(RPTR("WiFi belum tersambung"), 1800); return; }
  callMarkU(10);
  if (remember) callRecentAdd(t.c_str());
  callMarkU(11);
  callPost(CRQ_DIAL, t.c_str());
  callMarkU(12);
  needRedraw = true;
}

static bool callHitCirc(int x, int y, int cx, int cy, int r) {
  long dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= (long)(r + 6) * (r + 6);
}
static bool callHitRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx - 2 && x <= rx + rw + 2 && y >= ry - 2 && y <= ry + rh + 2;
}

static bool callInCallTouch(int x, int y) {      // true kalau sentuhan ditangani
  uint8_t ph = callPhase;
  int cx, cy, r, w;
  if (ph == CPH_INCOMING) {
    if (callBtnGeom(0, cx, cy, r, w) && callHitCirc(x, y, cx, cy, r)) { callPost(CRQ_REJECT, ""); return true; }
    if (callBtnGeom(1, cx, cy, r, w) && callHitCirc(x, y, cx, cy, r)) { callPost(CRQ_ANSWER, ""); return true; }
    return false;
  }
  for (int i = 0; i < 3; i++) {
    if (!callBtnGeom(i, cx, cy, r, w) || !callHitCirc(x, y, cx, cy, r)) continue;
    if (i == 0) { callMuted = !callMuted; needRedraw = true; }
    else {
      int v = musExtGetVol() + (i == 1 ? -8 : 8);
      if (v < 0) v = 0;
      if (v > 127) v = 127;
      musExtSetVol(v);
      char b[24]; snprintf(b, sizeof(b), "Volume %d%%", v * 100 / 127);
      showToast(b, 700);
    }
    return true;
  }
  if (callBtnGeom(3, cx, cy, r, w) && callHitRect(x, y, cx - w / 2, cy - r, w, r * 2)) { callPost(CRQ_HANGUP, ""); return true; }
  return false;
}

void callOverlayTouch(bool newT, int x, int y) {
  if (!newT) return;
  callInCallTouch(x, y);
  needRedraw = true;
}

void callTouch(int x, int y, bool held, bool isNew) {
  callMarkU(42);
  if (kbVisible) {
    if (!isNew) return;
    if (y < kbY() - 2) { kbVisible = false; kbTarget = nullptr; callCfgEdit = -1; }
    else kbTouch(x, y);
    needRedraw = true;
    return;
  }
  if (!isNew) return;
  if (callPhase != CPH_IDLE) {
    if (isBack(x, y)) { navBack(); return; }          // panggilan tetap jalan di latar
    callInCallTouch(x, y);
    return;
  }
  int rx0, ry0, rw0, rh0;
  if (callPage == 1 || callPage == 2) {
    if (isBack(x, y)) { callCfgSave(); callPage = (callPage == 2) ? 1 : 0; needRedraw = true; return; }
    callSaveRect(rx0, ry0, rw0, rh0);
    if (callHitRect(x, y, rx0, ry0, rw0, rh0)) {
      callCfgSave();
      if (callPage == 2) { callPage = 1; showToast(RPTR("TURN tersimpan"), 1200); }
      else { callPage = 0; showToast(RPTR("Tersimpan"), 1200); }
      needRedraw = true; return;
    }
    int nf = callPage == 2 ? 3 : 4;
    for (int i = 0; i < nf; i++) {
      callCfgRowRect(i, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { int f = (callPage == 2) ? 4 + i : i; callCfgEdit = f; callStartTyping(callCfgField(f)); return; }
    }
    if (callPage == 1) {
      callCfgRowRect(4, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { callStandby = !callStandby; needRedraw = true; return; }
      callCfgRowRect(5, rx0, ry0, rw0, rh0);
      if (callHitRect(x, y, rx0, ry0, rw0, rh0)) { callCfgSave(); callPage = 2; needRedraw = true; return; }
      callCfgRowRect(6, rx0, ry0, rw0, rh0);
      if (callSdHas("/sip.cfg") && callHitRect(x, y, rx0, ry0, rw0, rh0)) {
        if (callCfgFromSd()) showToast(RPTR("Dimuat dari /sip.cfg"), 1600);
        else showToast(RPTR("/sip.cfg tdk valid (host= user= pass=)"), 2600);
        needRedraw = true;
      }
    } else {
      callCfgRowRect(callTurnSdIdx(), rx0, ry0, rw0, rh0);
      if (callSdHas("/turn.cfg") && callHitRect(x, y, rx0, ry0, rw0, rh0)) {
        if (callTurnCfgFromSd()) showToast(RPTR("Dimuat dari /turn.cfg"), 1600);
        else showToast(RPTR("/turn.cfg tdk valid (host= user= pass=)"), 2600);
        needRedraw = true;
      }
    }
    return;
  }
  if (isBack(x, y)) { navBack(); return; }
  int lx, lw, rx, rw, hdrY, rowY0, pitch, cardH; callIdleGeom(lx, lw, rx, rw, hdrY, rowY0, pitch, cardH);
  bool inLeft = x >= lx - 2 && x <= lx + lw + 2;
  if (inLeft && x >= lx + lw - 32 && y >= 16 && y <= 52) { callPage = 1; needRedraw = true; return; }       // pengaturan
  if (inLeft && y >= CALL_CHIP_Y - 3 && y <= CALL_CHIP_Y + 18) { callShowDiag = !callShowDiag; needRedraw = true; return; }  // ketuk status = diagnosa
  if (inLeft && y >= CALL_SEG_Y - 2 && y <= CALL_SEG_Y + 24) { callTab = (x < lx + lw / 2) ? 0 : 1; needRedraw = true; return; }
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  if (inLeft && y >= CALL_FIELD_Y - 2 && y <= CALL_FIELD_Y + 34) {
    if (tgt->length() > 0 && x >= lx + lw - 28) { *tgt = ""; needRedraw = true; return; }
    callStartTyping(tgt); return;
  }
  if (inLeft && y >= CALL_BTN_Y - 2 && y <= CALL_BTN_Y + 32) { callDoDial(*tgt, callTab == 0); if (callTab == 1) callCfgSave(); return; }
  if (x >= rx - 2 && x <= rx + rw + 2 && y >= rowY0) {
    int k = (y - rowY0) / pitch;
    if ((y - rowY0) % pitch > cardH + 2) return;
    if (k < 0 || k >= callIdleSlots()) return;
    if (callTab == 0) {
      if (k < callRecentCount) { callTargetS = callRecent[k]; callDoDial(callTargetS, true); }
    } else if (k == 0) { callPost(CRQ_SCAN, ""); needRedraw = true; }
    else if (k - 1 < callScanCount) { callLanIpS = callScanIp[k - 1]; callDoDial(callLanIpS, false); }
  }
}
