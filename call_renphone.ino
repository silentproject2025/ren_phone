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
//   => TELEPON BUTUH TWS TERSAMBUNG (Musik > cari TWS). Tanpa TWS tidak ada
//   suara masuk (mic tetap jalan).
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
static char callDbgHist[4][40];
static void callDbgPush(const char* m) {
  for (int i = 3; i > 0; i--) memcpy(callDbgHist[i], callDbgHist[i - 1], sizeof(callDbgHist[i]));
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
  char ent[6][100]; int ne = 0;
  char line[320];
  for (int k = 0; k < 6; k++) {
    if (!sipHdr(msg, "Record-Route", "", k, line, sizeof(line))) break;
    const char* p = line;
    while (*p && ne < 6) {
      const char* lt = strchr(p, '<');
      if (!lt) break;
      const char* gt = strchr(lt, '>');
      if (!gt) break;
      int n = (int)(gt - lt + 1);
      if (n > 99) n = 99;
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
  char via[300]; char from[170]; char to[170]; char callId[72];
  char cseq[40]; char rr[260]; char contact[200];
};

// ---------------------------------------------------------------------
// STATE MILIK TASK (hanya task yg menyentuh ini)
// ---------------------------------------------------------------------
#define SIP_BUF 1500
static char tHost[64] = "", tHostName[64] = "", tUser[40] = "", tPass[40] = "", tName[24] = "";
static int  tPort = CALL_SIP_PORT;
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
static CallReqHdr rq, urq;
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
} dlg;

// media
static bool     aoOn = false, aoDrop = false, micOn = false, mediaOn = false;
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

// ---------------------------------------------------------------------
// AUDIO KELUAR -> CAM -> TWS (jalur musExt* milik pemutar AVI)
// ---------------------------------------------------------------------
static const char* callAudioErr(int rc) {
  switch (rc) {
    case 1: return "Bluetooth mati (nyalakan di Control Center)";
    case 2: return "Modul BT (ESP32-CAM) tdk terdeteksi";
    case 3: return "TWS belum tersambung (Musik > cari TWS)";
    case 4: return "RAM kurang utk audio";
    case 5: return "Audio belum siap (buka Telepon dulu)";
    case 9: return "Audio dipakai pemutar lain";
    default: return "Audio keluar gagal";
  }
}

static int callAudioStart() {
  if (aoOn) return 0;
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
  aoOn = false; callAudioOut = false;
  musExtEnd();
  uint8_t pf[2] = { 0, 0 };                          // 0 = kembali ke prefill bawaan CAM
  rpSend(Serial1, RP_PREFILL, pf, 2);
}

// kirim n sampel PCM16 mono 8 kHz
static void callPcmOut(const int16_t* pcm, int n) {
  if (!aoOn || n <= 0) return;
  if (!musExtActive()) { aoOn = false; callAudioOut = false; callDirty = true; return; }   // CAM restart / stream putus
  uint32_t w = musExtWrite((const uint8_t*)pcm, (uint32_t)n * 2u);
  aoSent += w / 2u;
}

// pantau antrean suara di CAM (ms) -> buang paket kalau menumpuk (jaga latensi)
static void callBacklogCheck() {
  if (!aoOn) return;
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
static void sipTxTo(const char* ip, int port, const char* data, int len) {
  if (!sipOpen || len <= 0) return;
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
}

static void sipResend() {
  if (sipRtxLen > 0) sipTxTo(sipRtxIp, sipRtxPort, sipRtx, sipRtxLen);
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

static bool callResolveServer() {
  if (srvOk && millis() - srvAt < 600000UL) return true;
  if (!tHostName[0]) return false;
  char ip[16];
  if (!callResolve(tHostName, ip)) { srvOk = false; return false; }
  srvOk = true; srvAt = millis();
  callCopy(srvIpStr, sizeof(srvIpStr), ip);
  srvIp.fromString(ip);
  return true;
}

// "ip:port" utk header Contact (publik kalau sdh dipelajari lewat rport, kecuali panggilan LAN)
static void callContactStr(char* out, int cap, bool lan) {
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
    callRegState = 3; callSetRegMsg("DNS server gagal");
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
    if (regAuthSent) { regFail("Username/password VoIP salah", 60000UL); return; }
    char uri[100]; snprintf(uri, sizeof(uri), "sip:%s", tHostName);
    char auth[700];
    if (!callBuildAuth(msg, code == 407, "REGISTER", uri, auth, sizeof(auth))) { regFail("Auth tdk didukung server", 60000UL); return; }
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
  if (code == 403) callCopy(b, sizeof(b), "Ditolak server (403)");
  else if (code == 404) callCopy(b, sizeof(b), "Akun tidak ada (404)");
  else snprintf(b, sizeof(b), "Gagal daftar (%d)", code);
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
      if (regTries >= 5) { regFail("Server tdk merespons", 30000UL); return; }
      sipResend(); regTries++; regSentAt = now; if (regBackoff < 4000) regBackoff *= 2;
    }
    return;
  }
  if ((int32_t)(now - regNextAt) >= 0) { regStart(); return; }
  if (callRegState == 2 && (int32_t)(now - regKeepAt) >= 25000 && srvOk) {      // jaga lubang NAT tetap terbuka
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
  if (!body || !body[0]) { sdpWhy = "Lawan tdk kirim SDP"; return false; }

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
    sdpWhy = sawSrtp ? "Lawan wajib SRTP (enkripsi)"
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
  if (strcmp(ip, "0.0.0.0") == 0) { sdpWhy = "Lawan minta panggilan ditahan"; return false; }

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
static uint8_t   turnReq[512], turnRsp[600], turnTx[256], turnTid[12];   // hanya dipakai callTask

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

// MESSAGE-INTEGRITY (long-term): HMAC-SHA1(key, pesan dgn field panjang sudah memuat atribut MI)
static int turnFinish(uint8_t* b, int len, bool integrity) {
  if (integrity) {
    turnPut16(b + 2, len - 20 + 24);
    uint8_t mac[20];
    const mbedtls_md_info_t* mi = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (mi) mbedtls_md_hmac(mi, turnKey, 16, b, (size_t)len, mac); else memset(mac, 0, sizeof(mac));
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
  if (!callResolve(host, ip) || !turnSrv.fromString(ip)) { callCopy(turnErr, sizeof(turnErr), "DNS TURN gagal"); return false; }
  turnPort = (uint16_t)(port > 0 ? port : 3478);
  turnRealm[0] = 0; turnNonce[0] = 0;

  int rl = 0;
  int n = turnBuildReq(0x0003, true, -1, false, IPAddress(), false);
  int code = turnTransact(n, &rl);
  if (code == 401 || code == 438) {
    turnTakeRealmNonce(turnRsp, rl);
    if (!turnRealm[0]) { callCopy(turnErr, sizeof(turnErr), "TURN: realm kosong"); return false; }
    n = turnBuildReq(0x0003, true, -1, false, IPAddress(), true);
    code = turnTransact(n, &rl);
  }
  if (code != 1) {
    if (code == 0) callCopy(turnErr, sizeof(turnErr), "TURN tdk merespons");
    else snprintf(turnErr, sizeof(turnErr), "TURN ditolak (%d)", code);
    return false;
  }
  int al = 0;
  const uint8_t* e = turnFindAttr(turnRsp, rl, 0x0016, &al);                 // XOR-RELAYED-ADDRESS
  char rip[16]; int rport = 0;
  if (!e || !turnDecodeXor(e, al, rip, sizeof(rip), &rport) || rport <= 0) { callCopy(turnErr, sizeof(turnErr), "TURN: alamat relay tdk ada"); return false; }
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
  for (int k = 0; k < 6; k++) {
    char v[160];
    if (!sipHdr(msg, "Via", "v", k, v, sizeof(v))) break;
    int l = (int)strlen(v);
    if (n + l + 8 >= (int)sizeof(rq.via)) break;
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
  if (!callMicStart()) callToast("Mic dipakai app lain: suara kamu tdk terkirim");
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
  if (!*tp) { callToast("Isi tujuan dulu"); return; }

  char reqUri[110], user[40], host[64]; int port = 0;
  if (strchr(tp, '@')) snprintf(reqUri, sizeof(reqUri), "sip:%s", tp);
  else if (callLooksLikeIp(tp)) snprintf(reqUri, sizeof(reqUri), "sip:nyx@%s", tp);
  else {
    if (!tHostName[0]) { callToast("Isi akun VoIP dulu (Atur)"); return; }
    snprintf(reqUri, sizeof(reqUri), "sip:%s@%s", tp, tHostName);
  }
  if (!sipUri(reqUri, user, sizeof(user), host, sizeof(host), &port)) { callToast("Alamat tidak valid"); return; }

  char dip[16]; int dport; bool viaServer = false, direct = false;
  if (callLooksLikeIp(host)) { direct = true; callCopy(dip, sizeof(dip), host); dport = port > 0 ? port : 5060; }
  else if (tHostName[0] && strcasecmp(host, tHostName) == 0) {
    if (!callResolveServer()) { callToast("DNS server VoIP gagal"); return; }
    viaServer = true; callCopy(dip, sizeof(dip), srvIpStr); dport = tPort;
  } else {
    if (!callResolve(host, dip)) { callToast("Alamat tidak ditemukan"); return; }
    dport = port > 0 ? port : 5060;
  }
  if (viaServer && callRegState != 2) { callToast("VoIP belum terdaftar"); return; }

  wakeYieldMic(4000);
  if (micI2SReady) { callToast("Mic sedang dipakai app lain"); return; }

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
      char dg[40]; snprintf(dg, sizeof(dg), "-> 486 sibuk (fase %d)", (int)callPhase);
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
  callSetStatus(dlg.lan ? "Panggilan langsung (LAN)" : "Panggilan VoIP masuk");
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
  dlg.retxAt = millis() + 500UL; dlg.retxN = 0; dlg.ackd = false;
  callPhase = CPH_ACTIVE;
  callSetStatus("Terhubung");
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
    sipCollectRR(msg, true, dlg.route, sizeof(dlg.route));
    bool sdpOk = callParseSdp(sipBody(msg));
    callSendAck2xx();
    if (callPhase == CPH_ENDING || !sdpOk) {                            // sudah dibatalkan / SDP tak cocok
      callSendBye();
      callFinish(sdpOk ? "Dibatalkan" : (sdpWhy ? sdpWhy : "Codec tidak cocok"));
      return;
    }
    callPhase = CPH_ACTIVE;
    dlg.ackd = true;
    callSetStatus("Terhubung");
    callMediaStart();
    return;
  }
  if (code == 401 || code == 407) {
    callSendAckNon2xx(msg);
    if (dlg.authTried || !tUser[0]) { callFinish("Autentikasi ditolak"); return; }
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
    case 404: callCopy(b, sizeof(b), "Tujuan tidak ditemukan"); break;
    case 480: callCopy(b, sizeof(b), "Tidak tersedia"); break;
    case 486: callCopy(b, sizeof(b), "Sedang sibuk"); break;
    case 487: callCopy(b, sizeof(b), "Dibatalkan"); break;
    case 488: {
      char wn[80]; wn[0] = 0;
      sipHdr(msg, "Warning", "", 0, wn, sizeof(wn));
      Serial.printf("[CALL] 488 dari lawan. Warning: %s\n", wn[0] ? wn : "(kosong)");
      const char* why = "Ditolak 488: codec/enkripsi";
      if (strstr(wn, "301") || strstr(wn, "302") || strstr(wn, "304")) why = "Lawan tdk cocok (media)";
      callCopy(b, sizeof(b), why);
    } break;
    case 603: callCopy(b, sizeof(b), "Ditolak"); break;
    case 408: case 504: callCopy(b, sizeof(b), "Tidak terjangkau"); break;
    default: snprintf(b, sizeof(b), "Gagal (%d)", code); break;
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
  if (callPhase == CPH_ACTIVE || callPhase == CPH_ENDING) callFinish("Panggilan selesai");
}

static void callOnCancel(const char* msg, const char* srcIp, int srcPort) {
  sipSplitReq(msg);
  int n = sipBuildReply(0, 200, "OK", nullptr, nullptr, sipTx, SIP_BUF);
  sipTxTo(srcIp, srcPort, sipTx, n);
  if (callPhase == CPH_INCOMING && strcmp(rq.callId, dlg.callId) == 0) {
    n = sipBuildReply(1, 487, "Request Terminated", nullptr, nullptr, sipTx, SIP_BUF);
    sipTxTo(dlg.peerIp, dlg.peerPort, sipTx, n);
    callFinish("Panggilan terlewat");
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
    if (strstr(callDbgRx, "REGISTER") == nullptr) {      // REGISTER berkala tdk masuk riwayat
      char dg[40]; snprintf(dg, sizeof(dg), "rx %s", callDbgRx); callDbgPush(dg);
    } else Serial.printf("[CALL] rx %s\n", callDbgRx);
  }
  int code = sipStatus(msg);
  if (code > 0) {
    char cs[40]; sipHdr(msg, "CSeq", "", 0, cs, sizeof(cs));
    if (strstr(cs, "REGISTER")) callOnRegResp(msg, code);
    else if (strstr(cs, "INVITE")) callOnInviteResp(msg, code);
    else if (strstr(cs, "OPTIONS")) { if (code == 200) callOnScanResp(msg, srcIp); }
    else if (strstr(cs, "BYE")) { if (callPhase == CPH_ENDING && code >= 200) callFinish("Panggilan selesai"); }
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
      callRejectIncoming(603, "Decline", "Ditolak");
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
    case CRQ_REJECT: if (callPhase == CPH_INCOMING) callRejectIncoming(486, "Busy Here", "Ditolak"); break;
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
        if (dlg.retxN >= 7) { callFinish("Tidak ada respon"); break; }
        sipResend();
        uint32_t step = 500UL << dlg.retxN; if (step > 4000UL) step = 4000UL;
        dlg.retxAt = now + step; dlg.retxN++;
      }
      if ((int32_t)(now - dlg.t0) > (int32_t)CALL_DIAL_MS) callHangup();
      break;
    case CPH_ENDING:
      if ((int32_t)(now - dlg.retxAt) >= 0) {
        if (dlg.retxN >= 3 || (int32_t)(now - dlg.t0) > 4000) { callFinish(dlg.byeSent ? "Panggilan selesai" : "Dibatalkan"); break; }
        sipResend();
        dlg.retxAt = now + (500UL << dlg.retxN); dlg.retxN++;
      }
      break;
    case CPH_INCOMING:
      if ((int32_t)(now - dlg.t0) > (int32_t)CALL_RING_MS) callRejectIncoming(480, "Temporarily Unavailable", "Panggilan terlewat");
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
  { char* s = strstr(tHostName, "sip:"); if (s == tHostName) memmove(tHostName, tHostName + 4, strlen(tHostName + 4) + 1); }
  srvOk = false; natIp[0] = 0; natPort = 0;
  regPending = false; regNextAt = 0; callRegState = 0;
  tCfgGen = callCfgGen;
}

static bool callAllocBuffers() {
  if (callBufOk) return true;
  const size_t total = (SIP_BUF * 3) + 512 + (CALL_SAMPLES * 4 * 4) + (CALL_JB * 320 * 2) + 64;
  uint8_t* b = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) b = (uint8_t*)malloc(total);
  if (!b) return false;
  sipRx = (char*)b; sipTx = (char*)(b + SIP_BUF); sipRtx = (char*)(b + SIP_BUF * 2);
  rtpRx = b + SIP_BUF * 3;
  micRaw = (int32_t*)(b + SIP_BUF * 3 + 512);
  jbPcm  = (int16_t*)(b + SIP_BUF * 3 + 512 + CALL_SAMPLES * 4 * 4);
  callBufOk = true;
  return true;
}

static void callTaskFunc(void*) {
  if (!callAllocBuffers()) { callTask = nullptr; vTaskDelete(nullptr); return; }
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (callPhase != CPH_IDLE) callFinish("WiFi terputus");
      if (sipOpen) { sipUdp.stop(); sipOpen = false; }
      if (callRegState != 0) { callRegState = 0; callSetRegMsg("WiFi belum tersambung"); regPending = false; regNextAt = 0; }
      srvOk = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!sipOpen) {
      callLocalIpStr(lip, sizeof(lip));
      sipUdp.begin(CALL_SIP_PORT);
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
static uint32_t callPollMs = 0, callTimerMs = 0;

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
  String h = "", u = "", w = "", n = "";
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.startsWith("host=")) h = l.substring(5);
    else if (l.startsWith("user=")) u = l.substring(5);
    else if (l.startsWith("pass=")) w = l.substring(5);
    else if (l.startsWith("name=")) n = l.substring(5);
  }
  f.close();
  h.trim(); u.trim(); w.trim(); n.trim();
  if (!h.length() || !u.length() || !w.length()) return false;
  callHostS = h; callUserS = u; callPassS = w;
  if (n.length()) callNameS = n;
  return true;
}

static bool callEnsureInit() {
  if (!callMx) callMx = xSemaphoreCreateMutex();
  if (!callMx) return false;
  if (!callCfgLoaded) callCfgLoad();
  musExtPrepare();                                  // link CAM harus dinyalakan di sini (UI thread), bukan di callTask
  if (callTask) return true;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 24000) return false;
  const uint32_t SZ = 20480;
  if (!callStack) callStack = (StackType_t*)heap_caps_aligned_alloc(16, SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (callStack) callTask = xTaskCreateStaticPinnedToCore(callTaskFunc, "callTask", SZ, NULL, 2, callStack, &callTCB, 1);
  else {
    BaseType_t r = xTaskCreatePinnedToCore(callTaskFunc, "callTask", SZ, NULL, 2, &callTask, 1);
    if (r != pdPASS) callTask = nullptr;
  }
  return callTask != nullptr;
}

static void callPost(uint8_t r, const char* arg) {
  if (!callEnsureInit()) { showToast("RAM tidak cukup utk Telepon", 2200); return; }
  callLock(); callCopy(callReqArg, sizeof(callReqArg), arg ? arg : ""); callUnlock();
  callReq = r;
}

// =====================================================================
//  SIKLUS HIDUP & POLL (UI thread)
// =====================================================================
void callEnter() {
  callCaptureBoot();
  if (!callCfgLoaded) callCfgLoad();
  if (!callTurnHostS.length() && callTurnCfgFromSd()) showToast("TURN dimuat dari /turn.cfg", 1600);
  if (!callBootShown && callBootMsg[0]) { callBootShown = true; showToast(callBootMsg, 6000); callDbgPush(callBootMsg); }
  callEnsureInit();
  if (!callSipConfigured() && callCfgFromSd()) { callCfgSave(); showToast("Akun VoIP dimuat dari SD", 2000); }
  callPage = 0; callCfgEdit = -1;
  needRedraw = true;
}
void callExit() { callCfgEdit = -1; }

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
      callOvl = true; callOvlLocked = locked;
      callMarkU(71);
      homeAodWake();
      callMarkU(72);
      callLastRingVib = 0;
      needRedraw = true;
    }
  }
  uint8_t ph = callPhase;
  if (ph == CPH_INCOMING && callOvl && now - callLastRingVib > 2600) {
    callLastRingVib = now;
    static const VibStep rp[] = { {1, 350}, {0, 150}, {1, 350}, {0, 1} };
    callMarkU(73);
    vibPlayPattern(rp, 4);
    callMarkU(74);
  }
  if (ph == CPH_ACTIVE && callPrevPhase != CPH_ACTIVE) {
    vibSuccess();
    if (callOvl && !callOvlLocked) {                  // diangkat saat tak terkunci -> jadi layar app biasa
      callOvl = false;
      if (curScreen() != SCR_CALL) navPush(SCR_CALL);
    }
  }
  callPrevPhase = ph;
  if (callEndedNew) {
    callEndedNew = false;
    callOvl = false; callOvlLocked = false;
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
  bool visible = callOvl || curScreen() == SCR_CALL;
  if (callDirty) { callDirty = false; if (visible) needRedraw = true; }
  if (visible && ph == CPH_ACTIVE && now - callTimerMs > 1000) { callTimerMs = now; needRedraw = true; }
  if (visible && callScanBusy && now - callTimerMs > 400) { callTimerMs = now; needRedraw = true; }
}

// =====================================================================
//  GAMBAR
// =====================================================================
static void callCenter(LGFX_Sprite& s, int x, int w, int y, const char* t) {
  s.setCursor(x + (w - s.textWidth(t)) / 2, y);
  s.print(t);
}

static int callBtnY() { return callOvl ? SCR_H - 52 : backY() - 46; }

// layar panggilan berlangsung / masuk (dipakai app & overlay)
static void callDrawInCall(LGFX_Sprite& s) {
  char pe[44], st[44];
  callLock(); callCopy(pe, sizeof(pe), callPeer); callCopy(st, sizeof(st), callStatus); callUnlock();
  uint8_t ph = callPhase;
  int cy = 52;
  s.setTextSize(1); s.setTextColor(T().subtext);
  callCenter(s, 0, SCR_W, cy, callIsLan ? "LAN langsung" : "VoIP");
  s.setTextSize(2); s.setTextColor(T().text);
  String nm = pe; int maxC = (SCR_W - 16) / 12;
  if ((int)nm.length() > maxC) nm = nm.substring(0, maxC);
  callCenter(s, 0, SCR_W, cy + 18, nm.c_str());
  s.setTextSize(1);
  char line[48];
  if (ph == CPH_ACTIVE) {
    uint32_t sec = (millis() - callStartMs) / 1000UL;
    snprintf(line, sizeof(line), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    s.setTextSize(2); s.setTextColor(T().good);
    callCenter(s, 0, SCR_W, cy + 48, line);
    s.setTextSize(1);
  } else {
    s.setTextColor(T().accent);
    callCenter(s, 0, SCR_W, cy + 52, st);
  }
  s.setTextColor(callAudioOut ? (uint16_t)T().subtext : (uint16_t)T().danger);
  callCenter(s, 0, SCR_W, cy + 78, callAudioOut ? "Audio: TWS" : "Tanpa audio keluar (TWS?)");
  if (ph == CPH_ACTIVE) {                                      // meter mic kecil
    int bw = SCR_W - 80, bx = 40, by = cy + 94;
    s.drawRoundRect(bx, by, bw, 6, 3, T().subtext);
    int fw = (bw - 2) * callMicLvl / 100;
    if (fw > 0) s.fillRoundRect(bx + 1, by + 1, fw, 4, 2, callMuted ? (uint16_t)T().danger : (uint16_t)T().good);
  }
  int by = callBtnY(), bh = 36;
  if (ph == CPH_INCOMING) {
    int w = (SCR_W - 30) / 2;
    iosRR(s, 10, by, w, bh, 10, T().danger);
    s.setTextColor(T().bg); callCenter(s, 10, w, by + 14, "Tolak");
    iosRR(s, 20 + w, by, w, bh, 10, T().good);
    s.setTextColor(T().bg); callCenter(s, 20 + w, w, by + 14, "Terima");
  } else {
    int w = (SCR_W - 30) / 4;
    iosRR(s, 6, by, w, bh, 8, callMuted ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(callMuted ? (uint16_t)T().bg : (uint16_t)T().text); callCenter(s, 6, w, by + 14, callMuted ? "Unmute" : "Mute");
    iosRR(s, 10 + w, by, w, bh, 8, T().surface2);
    s.setTextColor(T().text); callCenter(s, 10 + w, w, by + 14, "Vol -");
    iosRR(s, 14 + 2 * w, by, w, bh, 8, T().surface2);
    s.setTextColor(T().text); callCenter(s, 14 + 2 * w, w, by + 14, "Vol +");
    iosRR(s, 18 + 3 * w, by, w, bh, 8, T().danger);
    s.setTextColor(T().bg); callCenter(s, 18 + 3 * w, w, by + 14, ph == CPH_ENDING ? "..." : "Akhiri");
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
  return L[i];
}
static String* callCfgField(int i) {
  switch (i) {
    case 0: return &callHostS;     case 1: return &callUserS;     case 2: return &callPassS;
    case 4: return &callTurnHostS; case 5: return &callTurnUserS; case 6: return &callTurnPassS;
    default: return &callNameS;
  }
}

static void callDrawTurnCfg(LGFX_Sprite& s) {
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print("Telepon - TURN (relay)");
  for (int i = 0; i < 3; i++) {
    int ly = 40 + i * 27;
    s.setTextColor(T().subtext); s.setCursor(10, ly); s.print(callCfgLabel(4 + i));
    iosRR(s, 6, ly + 9, SCR_W - 12, 16, 5, T().surface);
    String v;
    if (i == 2) { for (size_t k = 0; k < callTurnPassS.length(); k++) v += '*'; } else v = *callCfgField(4 + i);
    int maxC = (SCR_W - 28) / 6;
    if ((int)v.length() > maxC) v = String("..") + v.substring(v.length() - (maxC - 2));
    s.setTextColor(v.length() ? (uint16_t)T().text : (uint16_t)T().subtext);
    s.setCursor(12, ly + 13); s.print(v.length() ? v.c_str() : "ketuk untuk mengisi...");
  }
  int by = 40 + 3 * 27 + 8;
  bool sdCfg = sdReady && SD_MMC.exists("/turn.cfg");
  int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
  iosRR(s, 6, by, saveW, 22, 7, T().accent);
  s.setTextColor(T().bg); callCenter(s, 6, saveW, by + 7, "Simpan");
  if (sdCfg) {
    int bx = 6 + saveW + 4, bw = SCR_W - 6 - bx;
    iosRR(s, bx, by, bw, 22, 7, T().surface2);
    s.setTextColor(T().accent); callCenter(s, bx, bw, by + 7, "Muat SD");
  }
  s.setTextColor(T().subtext);
  s.setCursor(10, by + 32); s.print("Dipakai utk panggilan lewat internet.");
  s.setCursor(10, by + 44); s.print("Kosong = pakai STUN saja.");
  if (turnErr[0]) { s.setTextColor(T().danger); s.setCursor(10, by + 58); s.print(turnErr); }
}

static void callDrawCfg(LGFX_Sprite& s) {
  if (callPage == 2 && callCfgEdit < 0) { callDrawTurnCfg(s); return; }
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print(callPage == 2 ? "Telepon - TURN (relay)" : "Telepon - Pengaturan");
  if (callCfgEdit >= 0) {
    s.setTextColor(T().subtext); s.setCursor(10, 50); s.print(callCfgLabel(callCfgEdit));
    iosRR(s, 6, 62, SCR_W - 12, 22, 5, T().surface);
    String v = *callCfgField(callCfgEdit);
    int maxC = (SCR_W - 28) / 6;
    if ((int)v.length() > maxC - 1) v = String("..") + v.substring(v.length() - (maxC - 3));
    v += "_";
    s.setTextColor(T().text); s.setCursor(12, 69); s.print(v.c_str());
    s.setTextColor(T().subtext); s.setCursor(10, 92); s.print("Ketuk area di atas keyboard = selesai");
    return;
  }
  for (int i = 0; i < 4; i++) {
    int ly = 40 + i * 27;
    s.setTextColor(T().subtext); s.setCursor(10, ly); s.print(callCfgLabel(i));
    iosRR(s, 6, ly + 9, SCR_W - 12, 16, 5, T().surface);
    String v;
    if (i == 2) { for (size_t k = 0; k < callPassS.length(); k++) v += '*'; } else v = *callCfgField(i);
    int maxC = (SCR_W - 28) / 6;
    if ((int)v.length() > maxC) v = String("..") + v.substring(v.length() - (maxC - 2));
    s.setTextColor(v.length() ? (uint16_t)T().text : (uint16_t)T().subtext);
    s.setCursor(12, ly + 13); s.print(v.length() ? v.c_str() : "ketuk untuk mengisi...");
  }
  iosRR(s, SCR_W - 62, 20, 56, 16, 6, T().surface2);
  s.setTextColor(T().accent); callCenter(s, SCR_W - 62, 56, 24, "TURN >");
  int ty = 40 + 4 * 27;
  iosRR(s, 6, ty, SCR_W - 12, 18, 5, T().surface);
  s.setTextColor(T().text); s.setCursor(12, ty + 5); s.print("Siaga terima panggilan");
  int sx = SCR_W - 40;
  iosRR(s, sx, ty + 3, 28, 12, 6, callStandby ? (uint16_t)T().good : (uint16_t)T().surface2);
  s.fillCircle(callStandby ? sx + 22 : sx + 6, ty + 9, 4, T().text);
  int by = ty + 22;
  bool sdCfg = sdReady && SD_MMC.exists("/sip.cfg");
  int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
  iosRR(s, 6, by, saveW, 22, 7, T().accent);
  s.setTextColor(T().bg); callCenter(s, 6, saveW, by + 7, "Simpan");
  if (sdCfg) {
    int bx = 6 + saveW + 4, bw = SCR_W - 6 - bx;
    iosRR(s, bx, by, bw, 22, 7, T().surface2);
    s.setTextColor(T().accent); callCenter(s, bx, bw, by + 7, "Muat SD");
  }
}

static void callRegText(char* out, int cap, uint16_t* col) {
  uint8_t st = callRegState;
  if (st == 2) { callCopy(out, cap, "VoIP terdaftar"); *col = T().good; }
  else if (st == 1) { callCopy(out, cap, "mendaftar..."); *col = T().subtext; }
  else if (st == 3) { char m[40]; callLock(); callCopy(m, sizeof(m), callRegMsg); callUnlock(); callCopy(out, cap, m); *col = T().danger; }
  else { callCopy(out, cap, callSipConfigured() ? "VoIP belum aktif" : "VoIP belum diatur"); *col = T().subtext; }
}

void drawCall(LGFX_Sprite& s) {
  callMarkU(41);
  iosBackdrop(s); drawStatusBar(s);
  s.setTextWrap(false); s.setTextSize(1);
  if (!callTask && !callEnsureInit()) {
    s.setTextColor(T().danger); s.setCursor(10, 60); s.print("RAM tidak cukup untuk Telepon.");
    drawBack(s); drawToast(s); return;
  }
  if (callPhase != CPH_IDLE) {
    callDrawInCall(s);
    drawBack(s); drawToast(s); return;
  }
  if (callPage == 1 || callPage == 2) {
    callDrawCfg(s);
    if (kbVisible) drawKb(s); else drawBack(s);
    drawToast(s); return;
  }
  // header
  s.setTextColor(T().accent); s.setCursor(8, 26); s.print("Telepon");
  char rt[40]; uint16_t rc; callRegText(rt, sizeof(rt), &rc);
  s.fillCircle(62, 30, 3, rc);
  String rs = rt; int maxSt = (SCR_W - 70 - 58) / 6;
  if ((int)rs.length() > maxSt) rs = rs.substring(0, maxSt);
  s.setTextColor(T().subtext); s.setCursor(70, 26); s.print(rs.c_str());
  iosRR(s, SCR_W - 52, 24, 48, 18, 5, T().surface2);
  s.setTextColor(T().accent); callCenter(s, SCR_W - 52, 48, 29, "Atur");
  // tab
  int tw = (SCR_W - 16) / 2;
  for (int i = 0; i < 2; i++) {
    iosRR(s, 6 + i * (tw + 4), 46, tw, 18, 6, callTab == i ? (uint16_t)T().accent : (uint16_t)T().surface2);
    s.setTextColor(callTab == i ? (uint16_t)T().bg : (uint16_t)T().text);
    callCenter(s, 6 + i * (tw + 4), tw, 51, i == 0 ? "VoIP" : "LAN (antar HP)");
  }
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  bool typing = kbVisible && kbTarget == tgt;
  s.setTextColor(T().subtext); s.setCursor(10, 70);
  s.print(callTab == 0 ? "Tujuan (user, user@domain)" : "Alamat IP HP lawan");
  iosRR(s, 6, 80, SCR_W - 12, 22, 5, T().surface);
  String v = *tgt; int maxC = (SCR_W - 28) / 6;
  if ((int)v.length() > maxC - 1) v = String("..") + v.substring(v.length() - (maxC - 3));
  if (typing) v += "_";
  s.setTextColor(v.length() ? (uint16_t)T().text : (uint16_t)T().subtext);
  s.setCursor(12, 87); s.print(v.length() ? v.c_str() : (callTab == 0 ? "ketuk untuk mengetik..." : "mis. 192.168.1.23"));
  iosRR(s, 6, 106, SCR_W - 12, 24, 8, T().good);
  s.setTextColor(T().bg); callCenter(s, 6, SCR_W - 12, 114, "Panggil");
  int y = 136;
  if (callTab == 0) {
    for (int i = 0; i < callRecentCount && y + 18 <= backY() - 2; i++, y += 20) {
      iosRR(s, 6, y, SCR_W - 12, 18, 5, T().surface);
      s.setTextColor(T().text); s.setCursor(12, y + 5); s.print(callRecent[i]);
    }
    if (callRecentCount == 0) { s.setTextColor(T().subtext); s.setCursor(10, y + 4); s.print("Belum ada riwayat panggilan"); }
  } else {
    iosRR(s, 6, y, SCR_W - 12, 20, 6, T().surface2);
    s.setTextColor(T().accent); callCenter(s, 6, SCR_W - 12, y + 6, callScanBusy ? "Mencari..." : "Cari HP NyxOS di jaringan");
    y += 24;
    int cnt = callScanCount;
    for (int i = 0; i < cnt && y + 18 <= backY() - 2; i++, y += 20) {
      iosRR(s, 6, y, SCR_W - 12, 18, 5, T().surface);
      char row[48]; snprintf(row, sizeof(row), "%s  %s", callScanName[i], callScanIp[i]);
      s.setTextColor(T().text); s.setCursor(12, y + 5); s.print(row);
    }
  }
  if (!kbVisible && callDbgHist[0][0]) {
    int y0 = backY() - 4 * 12 - 2;
    s.fillRect(0, y0 - 1, SCR_W, 4 * 12 + 2, T().bg);
    s.setTextColor(T().subtext);
    for (int i = 0; i < 4; i++) {
      if (!callDbgHist[3 - i][0]) continue;
      s.setCursor(4, y0 + i * 12); s.print(callDbgHist[3 - i]);
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
  if (!t.length()) { showToast("Isi tujuan dulu", 1500); return; }
  if (!wifiConnected) { showToast("WiFi belum tersambung", 1800); return; }
  callMarkU(10);
  if (remember) callRecentAdd(t.c_str());
  callMarkU(11);
  callPost(CRQ_DIAL, t.c_str());
  callMarkU(12);
  needRedraw = true;
}

static bool callInCallTouch(int x, int y) {      // true kalau sentuhan ditangani
  uint8_t ph = callPhase;
  int by = callBtnY(), bh = 36;
  if (y < by || y > by + bh) return false;
  if (ph == CPH_INCOMING) {
    int w = (SCR_W - 30) / 2;
    if (x < 10 + w) callPost(CRQ_REJECT, ""); else callPost(CRQ_ANSWER, "");
    return true;
  }
  int w = (SCR_W - 30) / 4;
  int idx = (x < 6 + w + 2) ? 0 : (x < 10 + 2 * w + 2) ? 1 : (x < 14 + 3 * w + 2) ? 2 : 3;
  if (idx == 0) { callMuted = !callMuted; needRedraw = true; }
  else if (idx == 1 || idx == 2) {
    int v = musExtGetVol() + (idx == 1 ? -8 : 8);
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    musExtSetVol(v);
    char b[24]; snprintf(b, sizeof(b), "Volume %d%%", v * 100 / 127);
    showToast(b, 700);
  } else callPost(CRQ_HANGUP, "");
  return true;
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
  if (callPage == 2) {
    if (isBack(x, y)) { callCfgSave(); callPage = 1; needRedraw = true; return; }
    for (int i = 0; i < 3; i++) {
      int ly = 40 + i * 27;
      if (y >= ly + 7 && y <= ly + 27) { callCfgEdit = 4 + i; callStartTyping(callCfgField(4 + i)); return; }
    }
    int by = 40 + 3 * 27 + 8;
    if (y >= by && y <= by + 22) {
      bool sdCfg = sdReady && SD_MMC.exists("/turn.cfg");
      int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
      if (x <= 6 + saveW) { callCfgSave(); callPage = 1; showToast("TURN tersimpan", 1200); needRedraw = true; }
      else if (sdCfg) {
        if (callTurnCfgFromSd()) showToast("Dimuat dari /turn.cfg", 1600);
        else showToast("/turn.cfg tdk valid (host= user= pass=)", 2600);
        needRedraw = true;
      }
    }
    return;
  }
  if (callPage == 1) {
    if (isBack(x, y)) { callCfgSave(); callPage = 0; needRedraw = true; return; }
    if (x >= SCR_W - 62 && y >= 18 && y <= 38) { callCfgSave(); callPage = 2; needRedraw = true; return; }
    for (int i = 0; i < 4; i++) {
      int ly = 40 + i * 27;
      if (y >= ly + 7 && y <= ly + 27) { callCfgEdit = i; callStartTyping(callCfgField(i)); return; }
    }
    int ty = 40 + 4 * 27;
    if (y >= ty && y <= ty + 18) { callStandby = !callStandby; needRedraw = true; return; }
    int by = ty + 22;
    if (y >= by && y <= by + 22) {
      bool sdCfg = sdReady && SD_MMC.exists("/sip.cfg");
      int saveW = sdCfg ? (SCR_W - 12) * 6 / 10 : (SCR_W - 12);
      if (x <= 6 + saveW) { callCfgSave(); callPage = 0; showToast("Tersimpan", 1200); needRedraw = true; }
      else if (sdCfg) {
        if (callCfgFromSd()) showToast("Dimuat dari /sip.cfg", 1600);
        else showToast("/sip.cfg tdk valid (host= user= pass=)", 2600);
        needRedraw = true;
      }
    }
    return;
  }
  if (isBack(x, y)) { navBack(); return; }
  if (x >= SCR_W - 52 && y >= 24 && y <= 42) { callPage = 1; needRedraw = true; return; }
  if (y >= 46 && y <= 64) { callTab = (x < SCR_W / 2) ? 0 : 1; needRedraw = true; return; }
  String* tgt = callTab == 0 ? &callTargetS : &callLanIpS;
  if (y >= 80 && y <= 102) { callStartTyping(tgt); return; }
  if (y >= 106 && y <= 130) { callDoDial(*tgt, callTab == 0); if (callTab == 1) callCfgSave(); return; }
  if (callTab == 0) {
    int row = (y - 136) / 20;
    if (y >= 136 && row >= 0 && row < callRecentCount) { callTargetS = callRecent[row]; callDoDial(callTargetS, true); }
  } else {
    if (y >= 136 && y <= 156) { callPost(CRQ_SCAN, ""); needRedraw = true; return; }
    int row = (y - 160) / 20;
    if (y >= 160 && row >= 0 && row < callScanCount) { callLanIpS = callScanIp[row]; callDoDial(callLanIpS, false); }
  }
}
