#pragma once
// =====================================================================
//  musjam.h -- klien Jamendo (cari lagu / populer) untuk app Musik
//  Hasil disimpan di PSRAM. Fungsi publik hanya memakai tipe dasar
//  (aturan prototipe arduino-cli di project ini).
//
//  Pakai:
//    jamSearch("lofi", "");        // cari judul/artis   (tag kosong)
//    jamSearch("", "pop");         // populer per genre/tag
//    jamSearch("", "");            // populer minggu ini
//    for (i < jamCount()) jamTitle(i), jamArtist(i), jamUrl(i)
//    spkPlayUrl(jamUrl(i));        // putar lewat speaker (musspk.h)
//
//  Isi JAM_CLIENT_ID dengan client_id dari https://devportal.jamendo.com
//  BELUM DICOMPILE / DITES DI BOARD.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include "jam_core.h"

// Client ID bisa datang dari build flag -DJAM_CLIENT_ID_RAW=308473cf (token polos tanpa kutip; dipakai CI
// dari GitHub Secret) atau -DJAM_CLIENT_ID=\"...\" atau diisi manual di bawah. Kosong = placeholder.
#ifndef JAM_CLIENT_ID
 #ifdef JAM_CLIENT_ID_RAW
  #define JAM_STR_(x) #x
  #define JAM_STR(x)  JAM_STR_(x)
  #define JAM_CLIENT_ID JAM_STR(JAM_CLIENT_ID_RAW)
 #else
  #define JAM_CLIENT_ID "ISI_CLIENT_ID_KAMU"
 #endif
#endif
#define JAM_RESP_MAX (256 * 1024)   // JAM_PARTIAL_V1: 40 lagu x ~2 KB JSON (di PSRAM); AUDIUS_V1: respons Audius ~3-5 KB per lagu

static JamTrack* jamTracks = nullptr;
static int       jamN      = 0;

// Penampung balasan -> buffer PSRAM (HTTPClient yang mengurus chunked)
class JamSink : public Stream {
 public:
  char* buf; size_t cap, len;
  JamSink(char* b, size_t c) : buf(b), cap(c), len(0) {}
  size_t write(uint8_t c) override { if (len + 1 < cap) { buf[len++] = (char)c; return 1; } return 0; }
  size_t write(const uint8_t* d, size_t n) override {
    if (len + n >= cap) n = (cap > len + 1) ? cap - len - 1 : 0;
    memcpy(buf + len, d, n); len += n; return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static int jamCount() { return jamN; }
static const char* jamTitle(int i)  { return (jamTracks && i >= 0 && i < jamN) ? jamTracks[i].title  : ""; }
static const char* jamArtist(int i) { return (jamTracks && i >= 0 && i < jamN) ? jamTracks[i].artist : ""; }
static const char* jamUrl(int i)    { return (jamTracks && i >= 0 && i < jamN) ? jamTracks[i].url    : ""; }
static int jamDurSec(int i)         { return (jamTracks && i >= 0 && i < jamN) ? (int)jamTracks[i].durSec : 0; }
static void jamFree() {
  if (jamTracks) { heap_caps_free(jamTracks); jamTracks = nullptr; }
  jamN = 0;
}

// =====================================================================
//  AUDIUS_V1 -- sumber lagu kedua. 0 = Jamendo (butuh client_id), 1 = Audius (tanpa kunci).
//  Daftar hasil (jamTracks) sama untuk keduanya, jadi UI, pemutaran (SPK / BT) dan lagu berikutnya tidak berubah.
//  Pilihan disimpan di NVS "music"/"jsrc". Bawaan: Jamendo kalau build punya JAM_CLIENT_ID, kalau tidak Audius.
//  Cari   -> GET /v1/tracks/search?query=..          (cocok sebagian/juga artis; urutan lalu dirapikan jamRank)
//  Hits   -> GET /v1/tracks/trending?time=week       (+ &genre=Pop/Rock/Lo-Fi/Jazz untuk chip genre)
//  Putar  -> GET /v1/tracks/<id>/stream              (balas 302 ke file MP3; HTTPClient mengikuti redirect)
//  BELUM DICOMPILE / DITES DI BOARD.
// =====================================================================
#define JAM_SRC_JAMENDO 0
#define JAM_SRC_AUDIUS  1
static volatile uint8_t jamSrc    = JAM_SRC_JAMENDO;   // pilihan di UI
static uint8_t          jamSrcRun = JAM_SRC_JAMENDO;   // salinan yang dipakai worker selama satu pencarian
static const char* jamSrcName()   { return jamSrc == JAM_SRC_AUDIUS ? "Audius" : "Jamendo"; }
static uint8_t jamSrcDefault()    { return (strcmp(JAM_CLIENT_ID, "ISI_CLIENT_ID_KAMU") == 0) ? JAM_SRC_AUDIUS : JAM_SRC_JAMENDO; }
static void jamSetSource(uint8_t s) { jamSrc = (s == JAM_SRC_AUDIUS) ? JAM_SRC_AUDIUS : JAM_SRC_JAMENDO; jamN = 0; }

// Return jumlah lagu (>=0) atau -2 = HTTP/TLS gagal. body = buffer PSRAM milik jamSearch.
static int audSearchInto(const char* qq, const char* tag, char* body) {
  char url[360]; int u;
  if (qq[0]) {
    char q[96]; jamUrlEnc(qq, q, sizeof(q));
    u = snprintf(url, sizeof(url), AUD_BASE "/tracks/search?query=%s&limit=%d&app_name=" AUD_APP, q, JAM_MAX_TRACKS);
  } else {
    u = snprintf(url, sizeof(url), AUD_BASE "/tracks/trending?time=week&limit=%d&app_name=" AUD_APP, JAM_MAX_TRACKS);
    const char* g = audGenre(tag);
    if (g[0]) { char ge[48]; jamUrlEnc(g, ge, sizeof(ge)); u += snprintf(url + u, sizeof(url) - u, "&genre=%s", ge); }
  }
  int result = -2;
  WiFiClientSecure net; net.setInsecure();              // sama seperti Jamendo: tanpa verifikasi sertifikat
  HTTPClient http; http.setTimeout(15000); http.setConnectTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (http.begin(net, url)) {
    int code = http.GET();
    if (code == 200) {
      JamSink sink(body, JAM_RESP_MAX);
      http.writeToStream(&sink);
      body[sink.len] = 0;
      jamN = audParse(body, jamTracks, JAM_MAX_TRACKS);
      result = jamN;
    }
    http.end();
  }
  return result;
}

// Return jumlah lagu (>=0), atau -1 = tidak ada WiFi, -2 = HTTP/TLS gagal, -3 = RAM habis.
// JAM_PARTIAL_V1: pencarian sebagian judul. Tahap 1 = namesearch (cocok di mana saja dalam judul, urut populer);
// kalau kosong, tahap 2 = search biasa (artis / tag / kata penuh). Lalu hasil diurutkan ulang (jamRank).
static int jamSearch(const char* query, const char* tag) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  if (!jamTracks) jamTracks = (JamTrack*)heap_caps_malloc(sizeof(JamTrack) * JAM_MAX_TRACKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  char* body = (char*)heap_caps_malloc(JAM_RESP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!jamTracks || !body) { if (body) heap_caps_free(body); return -3; }
  jamN = 0;

  char qq[64]; qq[0] = 0;                              // ketikan tanpa spasi di ujung
  if (query) {
    while (*query == ' ') query++;
    strncpy(qq, query, sizeof(qq) - 1); qq[sizeof(qq) - 1] = 0;
    for (int i = (int)strlen(qq) - 1; i >= 0 && qq[i] == ' '; i--) qq[i] = 0;
  }

  if (jamSrcRun == JAM_SRC_AUDIUS) {                   // AUDIUS_V1
    int ar = audSearchInto(qq, tag, body);
    heap_caps_free(body);
    if (ar > 0 && qq[0]) jamRank(jamTracks, jamN, qq);
    return ar;
  }

  int result = -2;
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1 && !(qq[0] && result == 0)) break;   // cadangan hanya kalau pencarian sebagian kosong
    jamN = 0; result = -2;

    char q[96], url[400];
    int u = snprintf(url, sizeof(url),
        "https://api.jamendo.com/v3.0/tracks/?client_id=%s&format=json&limit=%d&audioformat=mp31&audiodlformat=mp31&imagesize=100",
        JAM_CLIENT_ID, JAM_MAX_TRACKS);
    if (qq[0]) {
      jamUrlEnc(qq, q, sizeof(q));
      if (pass == 0) u += snprintf(url + u, sizeof(url) - u, "&namesearch=%s&order=popularity_total", q);
      else           u += snprintf(url + u, sizeof(url) - u, "&search=%s", q);
    } else           u += snprintf(url + u, sizeof(url) - u, "&order=popularity_week");
    if (tag && tag[0]) { jamUrlEnc(tag, q, sizeof(q)); u += snprintf(url + u, sizeof(url) - u, "&tags=%s", q); }

    WiFiClientSecure net; net.setInsecure();            // sama seperti backup_sync: tanpa verifikasi sertifikat
    HTTPClient http; http.setTimeout(15000); http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (http.begin(net, url)) {
      int code = http.GET();
      if (code == 200) {
        JamSink sink(body, JAM_RESP_MAX);
        http.writeToStream(&sink);
        body[sink.len] = 0;
        jamN = jamParse(body, jamTracks, JAM_MAX_TRACKS);
        result = jamN;
      }
      http.end();
    }
    if (result < 0) break;                              // gagal jaringan: jangan coba tahap 2
  }
  heap_caps_free(body);
  if (result > 0 && qq[0]) jamRank(jamTracks, jamN, qq);
  return result;
}

// =====================================================================
//  ASYNC: pencarian jalan di worker sendiri (stack di PSRAM) supaya UI & musTask tidak macet
//  sampai 15 dtk. Satu worker hidup terus (tidak dibuat/dihapus tiap cari).
//    jamSearchAsync(q, tag) -> false kalau masih sibuk / worker gagal dibuat
//    jamBusy()              -> true selama mengambil data
//    jamLastResult()        -> hasil pencarian terakhir (>=0 jumlah lagu, <0 kode galat jamSearch)
//    jamTakeDone()          -> true SEKALI setelah pencarian selesai (utk toast / redraw)
// =====================================================================
static char              jamAQ[64], jamATag[16];
static volatile bool     jamBusyF = false, jamDoneF = false;
static volatile int      jamResF  = 0;
static TaskHandle_t      jamTaskH = nullptr;
static StackType_t*      jamStack = nullptr;
static StaticTask_t      jamTCB;

static void jamWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    jamResF = jamSearch(jamAQ, jamATag);
    jamBusyF = false; jamDoneF = true;
  }
}
static bool jamBusy()       { return jamBusyF; }
static int  jamLastResult() { return jamResF; }
static bool jamTakeDone()   { if (jamDoneF) { jamDoneF = false; return true; } return false; }
static bool jamSearchAsync(const char* q, const char* tag) {
  if (jamBusyF) return false;
  if (!jamTaskH) {
    const uint32_t stackBytes = 16384;                 // handshake TLS
    jamStack = (StackType_t*)heap_caps_aligned_alloc(16, stackBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jamStack) return false;
    jamTaskH = xTaskCreateStaticPinnedToCore(jamWorker, "jamTask", stackBytes, NULL, 3, jamStack, &jamTCB, 0);
    if (!jamTaskH) { heap_caps_free(jamStack); jamStack = nullptr; return false; }
  }
  strncpy(jamAQ, q ? q : "", sizeof(jamAQ) - 1);     jamAQ[sizeof(jamAQ) - 1] = 0;
  strncpy(jamATag, tag ? tag : "", sizeof(jamATag) - 1); jamATag[sizeof(jamATag) - 1] = 0;
  jamSrcRun = jamSrc;                                // AUDIUS_V1: sumber dikunci selama satu pencarian
  jamBusyF = true; jamDoneF = false;
  xTaskNotifyGive(jamTaskH);
  return true;
}
