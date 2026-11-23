// =====================================================================
// brokercfg_renphone.ino -- BROKERCFG_V1: ambil kredensial broker MQTT (Chat & Catur) dari Supabase.
//
// Alur   : app Chat/Catur dibuka -> bcShouldFetch() -> bcRequest(svc) -> task sekali jalan memanggil
//          RPC get_broker_config(p_service, p_device_id) -> app mengambil hasilnya lewat bcTake()
//          dan menyimpannya ke NVS miliknya sendiri. Pengguna tidak perlu mengetik apa pun.
// Aturan : * Config yg DIKETIK/DIMUAT dari SD oleh pengguna tidak pernah ditimpa (dikenali lewat hash).
//          * Config yg berasal dari cloud ikut ter-update otomatis (mis. password diganti di EMQX).
//          * Gagal jaringan -> pakai config tersimpan (kalau ada), kalau tidak ada -> layar isi manual.
// Mati   : RP_REG_URL kosong (devreg_conf.h) -> semua fungsi no-op.
// Skema  : supabase/broker_config.sql
// Urutan kompilasi .ino = alfabet: file ini lebih dulu dari chat_renphone.ino dan chess_renphone.ino.
// =====================================================================
#include "devreg_conf.h"
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

const char* devRegId();                         // devreg_renphone.ino
bool rpHeapEnsure(size_t need, unsigned long maxMs);   // phone.ino

#define BC_IDLE   0
#define BC_BUSY   1
#define BC_OK     2
#define BC_FAIL   3
#define BC_SVC_CHAT   0
#define BC_SVC_CHESS  1
#define BC_MIN_INT_BLOCK 22000                  // blok RAM internal utk handshake TLS (sama dgn devreg)

static volatile uint8_t bcSt[2] = {BC_IDLE, BC_IDLE};
static char bcH[2][96], bcU[2][64], bcP[2][96];

static const char* bcSvcName(uint8_t svc) { return svc == BC_SVC_CHESS ? "chess" : "chat"; }

// ambil nilai string dari JSON datar {"k":"v",...}. false jika tidak ada / kosong / terlalu panjang.
static bool bcJsonStr(const char* j, const char* key, char* out, size_t n) {
  char pat[16];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(j, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ' ' || *p == ':') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"') {
    char c = *p++;
    if (c == '\\' && *p) {
      c = *p++;
      if (c == 'u') return false;               // escape unicode: tidak dipakai utk kredensial
      if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
    }
    if (i + 1 >= n) return false;
    out[i++] = c;
  }
  if (*p != '"') return false;
  out[i] = 0;
  return i > 0;
}

static void bcTask(void* arg) {
  const uint8_t svc = (uint8_t)(uintptr_t)arg;
  bool ok = false;
  if (smEnsureContig(BC_MIN_INT_BLOCK)) {
    WiFiClientSecure net;
#ifdef RP_REG_CA
    if (sizeof(RP_REG_CA) > 1) net.setCACert(RP_REG_CA); else net.setInsecure();
#else
    net.setInsecure();
#endif
    net.setTimeout(10);
    HTTPClient http;
    http.setTimeout(10000); http.setConnectTimeout(10000);
    String url = String(RP_REG_URL) + "/rest/v1/rpc/get_broker_config";
    if (http.begin(net, url)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("apikey", RP_REG_KEY);
      if (strncmp(RP_REG_KEY, "sb_", 3) != 0) http.addHeader("Authorization", String("Bearer ") + RP_REG_KEY);
      char body[96];
      snprintf(body, sizeof(body), "{\"p_service\":\"%s\",\"p_device_id\":\"%s\"}", bcSvcName(svc), devRegId());
      int code = http.POST((uint8_t*)body, strlen(body));
      Serial.printf("[bcfg] %s -> %d\n", bcSvcName(svc), code);
      if (code >= 200 && code < 300) {
        String r = http.getString();
        char h[96], u[64], w[96];
        if (bcJsonStr(r.c_str(), "host", h, sizeof(h)) && bcJsonStr(r.c_str(), "user", u, sizeof(u)) &&
            bcJsonStr(r.c_str(), "pass", w, sizeof(w))) {
          strcpy(bcH[svc], h); strcpy(bcU[svc], u); strcpy(bcP[svc], w);
          ok = true;
        }
      }
      http.end();
    }
  }
  bcSt[svc] = ok ? BC_OK : BC_FAIL;             // ditulis terakhir: UI baru membaca buffer setelah ini
  vTaskDelete(NULL);
}

// ---- hash config terakhir yg berasal dari cloud (NVS "bcfg"), untuk mengenali config buatan pengguna ----
static uint32_t bcHash(const String& h, const String& u, const String& w) {
  uint32_t x = 2166136261u;
  const String* parts[3] = {&h, &u, &w};
  for (int k = 0; k < 3; k++) {
    for (size_t i = 0; i < parts[k]->length(); i++) { x ^= (uint8_t)(*parts[k])[i]; x *= 16777619u; }
    x ^= 0x0A; x *= 16777619u;
  }
  return x ? x : 1;
}

// true jika config sekarang kosong ATAU persis hasil cloud sebelumnya (belum diubah pengguna).
static bool bcManaged(uint8_t svc, const String& h, const String& u, const String& w) {
  if (!h.length() || !u.length() || !w.length()) return true;
  uint32_t saved = 0;
  Preferences p;
  if (p.begin("bcfg", true)) { saved = p.getUInt(bcSvcName(svc), 0); p.end(); }
  return saved != 0 && saved == bcHash(h, u, w);
}

// ---------------------------------------------------------------------
// API untuk app Chat / Catur
// ---------------------------------------------------------------------
// Perlu ambil dari cloud? (fitur aktif, WiFi ada, dan config kosong / masih milik cloud)
bool bcShouldFetch(uint8_t svc, const String& h, const String& u, const String& w) {
  if (svc > 1 || sizeof(RP_REG_URL) <= 1) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  return bcManaged(svc, h, u, w);
}

// Mulai ambil (sekali jalan, di task terpisah). false = tidak bisa mulai (app lanjut tanpa menunggu).
bool bcRequest(uint8_t svc) {
  if (svc > 1 || sizeof(RP_REG_URL) <= 1) return false;
  if (bcSt[svc] == BC_BUSY) return true;
  rpHeapEnsure(BC_MIN_INT_BLOCK, 1500);   // RAM_V5
  bcSt[svc] = BC_BUSY;
  if (xTaskCreatePinnedToCore(bcTask, "bcfg", 10240, (void*)(uintptr_t)svc, 1, NULL, 0) != pdPASS) {
    bcSt[svc] = BC_IDLE;
    return false;
  }
  return true;
}

uint8_t bcState(uint8_t svc) { return svc > 1 ? (uint8_t)BC_IDLE : (uint8_t)bcSt[svc]; }

// Ambil hasil sukses (sekali). Mengembalikan state ke IDLE. Nilai sudah di-trim.
bool bcTake(uint8_t svc, String& h, String& u, String& w) {
  if (svc > 1 || bcSt[svc] != BC_OK) return false;
  h = bcH[svc]; u = bcU[svc]; w = bcP[svc];
  h.trim(); u.trim(); w.trim();
  bcSt[svc] = BC_IDLE;
  return true;
}

void bcAck(uint8_t svc) { if (svc <= 1 && bcSt[svc] != BC_BUSY) bcSt[svc] = BC_IDLE; }

// Boleh menimpa config sekarang dengan hasil cloud? (kosong, atau belum pernah diubah pengguna)
bool bcMayOverwrite(uint8_t svc, const String& h, const String& u, const String& w) { return bcManaged(svc, h, u, w); }

// Panggil SETELAH config hasil cloud disimpan, supaya kelak dikenali sebagai milik cloud.
void bcMarkApplied(uint8_t svc, const String& h, const String& u, const String& w) {
  Preferences p;
  if (p.begin("bcfg", false)) { p.putUInt(bcSvcName(svc), bcHash(h, u, w)); p.end(); }
}
