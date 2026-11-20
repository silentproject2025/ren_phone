// =====================================================================
// backup_sync_renphone.ino -- BACKUP KE DATABASE (Supabase), tanpa akun / sandi.
//
// Nama file sengaja "backup_s..." supaya dikompilasi SETELAH backup_renphone.ino (urutan alfabet): fungsi
// static bkCollect/bkApply/bkAlloc/bkZero di sana dipakai di sini. Logika murni ada di backup_cloud.h (diuji di PC).
//
//   Identitas : kode perangkat (devRegId(), tidak bisa diganti). Bukan rahasia -> tidak cukup untuk membuka apa pun.
//   Rahasia   : eFuse unique ID chip (tidak pernah terpancar). Bila chip belum memrogramnya (semua 0 / semua FF),
//               dipakai 32 byte acak di NVS (nyxbak/csec) -- cadangan ini HILANG kalau flash dihapus total.
//   Privasi   : file dienkripsi di perangkat dgn kunci turunan rahasia chip; server hanya menerima data acak dan
//               token yang SHA-256-nya disimpan. Lihat supabase/backup.sql.
//   Operasi   : 1 cadangkan | 2 pulihkan | 3 hapus. Jalan di task sendiri; UI hanya memantau lewat bkCloud*().
//   Mati      : RP_REG_URL kosong (devreg_conf.h) -> tidak ada trafik.
//   TLS       : isi RP_REG_CA (PEM root CA) di devreg_conf.h untuk memverifikasi server. Kosong = tanpa verifikasi,
//               sama seperti modul jaringan lain di proyek ini (data tetap terenkripsi, tapi token bisa disadap
//               penyerang aktif di jaringan yang sama).
// =====================================================================
#include "backup_cloud.h"
#include "devreg_conf.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(__has_include)
#  if __has_include("esp_efuse_table.h")
#    include "esp_efuse.h"
#    include "esp_efuse_table.h"
#    define BS_HAVE_UID 1
#  endif
#endif
#include <mbedtls/sha256.h>

const char* devRegId();                     // devreg_renphone.ino

#define BS_OP_PUT 1
#define BS_OP_GET 2
#define BS_OP_DEL 3
#define BS_MIN_INT_BLOCK 26000              // blok RAM internal utk handshake TLS (sama dgn devreg)
#define BS_RESP_MAX      150000             // balasan get_backup terbesar yg diterima

static volatile bool bsBusy = false, bsReady = false, bsRestart = false;
static char          bsMsg[48] = "";
static uint8_t       bsOp = 0;
static bool          bsSec = false;
static bool          bsFallback = false;

static void bsDone(const char* m, bool restart = false) {
  strncpy(bsMsg, m, sizeof(bsMsg) - 1); bsMsg[sizeof(bsMsg) - 1] = 0;
  bsRestart = restart; bsReady = true; bsBusy = false;
}

// ---- rahasia chip -> 32 byte
static bool bsChipSecret(uint8_t out[32]) {
  bsFallback = false;
#ifdef BS_HAVE_UID
  uint8_t uid[16] = {0};
  if (esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, 128) == ESP_OK && bkCloudUidOk(uid, 16)) {
    uint8_t in[13 + 16]; memcpy(in, "ren_phone.uid", 13); memcpy(in + 13, uid, 16);
    mbedtls_sha256(in, sizeof(in), out, 0);
    bkZero(uid, sizeof(uid)); bkZero(in, sizeof(in));
    return true;
  }
#endif
  bsFallback = true;
  Preferences p;
  if (!p.begin(BK_NVS_NS, false)) return false;
  bool ok = (p.getBytesLength("csec") == 32 && p.getBytes("csec", out, 32) == 32 && bkCloudUidOk(out, 32));
  if (!ok) {
    uint8_t r[48];
    for (int i = 0; i < 48; i += 4) { uint32_t v = esp_random(); memcpy(r + i, &v, 4); }
    mbedtls_sha256(r, sizeof(r), out, 0);                          // WiFi menyala saat ini -> esp_random() = RNG perangkat keras
    ok = (p.putBytes("csec", out, 32) == 32);
    bkZero(r, sizeof(r));
  }
  p.end();
  return ok;
}
// Parameter sengaja tipe primitif: arduino-cli membuat prototipe otomatis di atas file, sebelum struct BkCloudKeys dikenal.
static bool bsKeys(char* token65, uint8_t* enc32) {
  uint8_t chip[32];
  if (!bsChipSecret(chip)) return false;
  BkCloudKeys k;
  bkCloudDerive(chip, devRegId(), k);
  memcpy(token65, k.token, 65); memcpy(enc32, k.enc, 32);
  bkZero(&k, sizeof(k));
  bkZero(chip, sizeof(chip));
  return true;
}

// ---- penampung balasan (menulis ke buffer PSRAM; HTTPClient mengurus chunked)
struct BsSink : public Stream {
  uint8_t* b; size_t cap, n; bool over;
  size_t write(uint8_t c) override { if (n < cap) b[n++] = c; else over = true; return 1; }
  size_t write(const uint8_t* p, size_t l) override {
    size_t k = (n + l <= cap) ? l : (cap - n); if (k) { memcpy(b + n, p, k); n += k; }
    if (k < l) over = true;
    return l;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

// POST JSON ke fungsi RPC. Mengembalikan kode HTTP (<0 = gagal). *resp = buffer PSRAM (bebaskan pemanggil) atau NULL.
static int bsRpc(const char* fn, const char* body, size_t blen, char** resp, size_t* rn, size_t maxResp) {
  *resp = NULL; *rn = 0;
  WiFiClientSecure net;
#ifdef RP_REG_CA
  if (sizeof(RP_REG_CA) > 1) net.setCACert(RP_REG_CA); else net.setInsecure();
#else
  net.setInsecure();
#endif
  net.setTimeout(20);
  HTTPClient http; http.setTimeout(25000); http.setConnectTimeout(10000);
  String url = String(RP_REG_URL) + "/rest/v1/rpc/" + fn;
  if (!http.begin(net, url)) return -1;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", RP_REG_KEY);
  if (strncmp(RP_REG_KEY, "sb_", 3) != 0) http.addHeader("Authorization", String("Bearer ") + RP_REG_KEY);
  int code = http.POST((uint8_t*)body, blen);
  if (code >= 200 && code < 300) {
    uint8_t* rb = bkAlloc(maxResp + 1);
    if (rb) {
      BsSink sk; sk.b = rb; sk.cap = maxResp; sk.n = 0; sk.over = false;
      http.writeToStream(&sk);
      if (sk.over) { bkZero(rb, sk.n); free(rb); code = -2; }
      else { rb[sk.n] = 0; *resp = (char*)rb; *rn = sk.n; }
    } else code = -3;
  }
  Serial.printf("[bkcloud] %s -> %d\n", fn, code);
  http.end();
  return code;
}

static void bsTask(void*) {
  BkCloudKeys k; memset(&k, 0, sizeof(k));
  char* resp = NULL; size_t rn = 0;
  if (!bsKeys(k.token, k.enc)) { bsDone(RPTR("Kunci perangkat gagal dibuat")); vTaskDelete(NULL); return; }
  const char* id = devRegId();

  if (bsOp == BS_OP_PUT) {
    uint8_t* buf = bkAlloc(BK_BUF_MAX); size_t len = 0; int cnt = 0;
    if (!buf) { bsDone(RPTR("PSRAM tidak cukup")); goto out; }
    { int rc = bkCollect(buf, BK_BUF_MAX, bsSec, &len, &cnt);
      if (rc != 0) { free(buf); bsDone(rc == 1 ? RPTR("Pengaturan terlalu besar") : RPTR("Gagal membaca pengaturan")); goto out; } }
    {
      size_t ecap = len + BK_ENC_OVERHEAD + 16; uint8_t* eb = bkAlloc(ecap);
      if (!eb) { bkZero(buf, len); free(buf); bsDone(RPTR("PSRAM tidak cukup")); goto out; }
      uint8_t rnd[32]; for (int i = 0; i < 32; i += 4) { uint32_t r = esp_random(); memcpy(rnd + i, &r, 4); }
      // kunci enc = 256 bit acak -> 1 putaran PBKDF2 cukup (tidak ada sandi manusia yg perlu diperlambat)
      size_t el = bkEncrypt(buf, len, k.enc, 32, rnd, rnd + 16, 1, eb, ecap);
      bkZero(buf, len); free(buf); bkZero(rnd, sizeof(rnd));
      if (!el) { free(eb); bsDone(RPTR("Gagal mengenkripsi")); goto out; }
      size_t bcap = bkCloudPutCap(el); char* body = (char*)bkAlloc(bcap);
      if (!body) { free(eb); bsDone(RPTR("PSRAM tidak cukup")); goto out; }
      size_t bl = bkCloudBuildPut(eb, el, id, k.token, body, bcap);
      bkZero(eb, el); free(eb);
      int code = bl ? bsRpc("put_backup", body, bl, &resp, &rn, 64) : -4;
      bkZero(body, bcap); free(body);
      char st[16] = "";
      if (code >= 200 && code < 300 && resp) bkCloudPlainStr(resp, rn, st, sizeof(st));
      if      (!strcmp(st, "ok")) {
        Preferences p; if (p.begin(BK_NVS_NS, false)) { time_t t = time(nullptr); p.putUInt("cloudt", t > 1600000000 ? (uint32_t)t : 0); p.putUInt("cloudn", (uint32_t)el); p.end(); }
        char m[48]; snprintf(m, sizeof(m), RPTR("Cadangan cloud tersimpan (%d nilai)"), cnt); bsDone(m);
      }
      else if (!strcmp(st, "too_soon"))        bsDone(RPTR("Terlalu cepat, tunggu 30 detik"));
      else if (!strcmp(st, "denied"))          bsDone(RPTR("Kode ini sudah diklaim perangkat lain"));
      else if (!strcmp(st, "unknown_device"))  bsDone(RPTR("Perangkat belum terdaftar, coba lagi nanti"));
      else if (!strcmp(st, "too_big"))         bsDone(RPTR("Pengaturan terlalu besar utk cloud"));
      else if (!strcmp(st, "full"))            bsDone(RPTR("Server penuh, coba lagi nanti"));
      else                                     bsDone(code < 0 ? RPTR("Gagal tersambung ke server") : RPTR("Server menolak permintaan"));
    }
  } else if (bsOp == BS_OP_GET) {
    char ab[200]; size_t al = bkCloudBuildAuth(id, k.token, 0, ab, sizeof(ab));
    int code = bsRpc("get_backup", ab, al, &resp, &rn, BS_RESP_MAX);
    const char* v; size_t vl;
    if (code < 200 || code >= 300 || !resp) { bsDone(code == -2 ? RPTR("Balasan server terlalu besar") : RPTR("Gagal tersambung ke server")); goto out; }
    if (!bkCloudJsonStr(resp, rn, "status", &v, &vl) || !bkCloudJsonEq(v, vl, "ok")) { bsDone(RPTR("Tidak ada cadangan utk perangkat ini")); goto out; }
    if (!bkCloudJsonStr(resp, rn, "data", &v, &vl)) { bsDone(RPTR("Balasan server tidak lengkap")); goto out; }
    {
      uint8_t* raw = bkAlloc(vl); uint8_t* pb = bkAlloc(vl);
      if (!raw || !pb) { if (raw) free(raw); if (pb) free(pb); bsDone(RPTR("PSRAM tidak cukup")); goto out; }
      long n = bkB64Dec(v, vl, raw, vl);
      size_t pl = 0; int dr = (n > 0) ? bkDecrypt(raw, (size_t)n, k.enc, 32, pb, vl, &pl) : BK_DEC_FORMAT;
      bkZero(raw, vl); free(raw);
      if (dr == BK_DEC_PASS)      { bkZero(pb, vl); free(pb); bsDone(RPTR("Bukan cadangan perangkat ini / rusak")); goto out; }
      if (dr != BK_DEC_OK)        { bkZero(pb, vl); free(pb); bsDone(RPTR("Cadangan rusak, ditolak")); goto out; }
      int fail = 0; int ok = bkApply(pb, pl, &fail);
      bkZero(pb, vl); free(pb);
      if (ok < 0) { bsDone(RPTR("Cadangan rusak, ditolak")); goto out; }
      char m[48];
      if (fail) snprintf(m, sizeof(m), RPTR("Dipulihkan %d, gagal %d. Restart..."), ok, fail);
      else      snprintf(m, sizeof(m), RPTR("Dipulihkan %d nilai. Restart..."), ok);
      bsDone(m, true);
    }
  } else {
    char ab[200]; size_t al = bkCloudBuildAuth(id, k.token, -1, ab, sizeof(ab));
    int code = bsRpc("delete_backup", ab, al, &resp, &rn, 64);
    char st[16] = ""; if (code >= 200 && code < 300 && resp) bkCloudPlainStr(resp, rn, st, sizeof(st));
    if (!strcmp(st, "ok")) { Preferences p; if (p.begin(BK_NVS_NS, false)) { p.putUInt("cloudt", 0); p.end(); } bsDone(RPTR("Cadangan cloud dihapus")); }
    else if (!strcmp(st, "none")) bsDone(RPTR("Tidak ada cadangan cloud"));
    else bsDone(code < 0 ? RPTR("Gagal tersambung ke server") : RPTR("Server menolak permintaan"));
  }
out:
  if (resp) { bkZero(resp, rn); free(resp); }
  bkZero(&k, sizeof(k));
  vTaskDelete(NULL);
}

// ---------------------------------------------------------------------
// API untuk UI (backup_renphone.ino). Hanya tipe primitif.
// ---------------------------------------------------------------------
bool bkCloudBusy() { return bsBusy; }
// Token (64 hex + NUL) utk kunci username di devreg_renphone.ino. false = rahasia chip tak tersedia.
bool bkCloudToken(char* token65) {
  BkCloudKeys k; uint8_t enc[32];
  if (!bsKeys(k.token, enc)) return false;
  memcpy(token65, k.token, 65); bkZero(enc, sizeof(enc)); bkZero(&k, sizeof(k));
  return true;
}

// false = tidak dimulai (alasannya langsung dilaporkan lewat bkCloudTake).
bool bkCloudStart(int op, bool secrets) {
  if (bsBusy) return false;
  if (sizeof(RP_REG_URL) <= 1) { bsDone("Fitur cloud dimatikan di firmware"); return false; }
  if (WiFi.status() != WL_CONNECTED) { bsDone(RPTR("WiFi belum tersambung")); return false; }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < BS_MIN_INT_BLOCK) { bsDone(RPTR("RAM kurang, tutup app lain dulu")); return false; }
  bsOp = (uint8_t)op; bsSec = secrets; bsReady = false; bsRestart = false; bsBusy = true;
  if (xTaskCreatePinnedToCore(bsTask, "bkcloud", 12288, NULL, 1, NULL, 0) != pdPASS) { bsDone(RPTR("Gagal memulai tugas")); return false; }
  return true;
}
// Hasil akhir yang belum dibaca (sekali pakai).
bool bkCloudTake(char* msg, size_t n, bool* restart) {
  if (!bsReady || bsBusy) return false;
  strncpy(msg, bsMsg, n - 1); msg[n - 1] = 0; *restart = bsRestart; bsReady = false; bsRestart = false;
  return true;
}
void bkCloudCode(char* out, size_t n) {            // "0123-4567-89ab-cdef" (n >= 20)
  if (n < 20) { if (n) out[0] = 0; return; }
  bkCloudCodeStr(devRegId(), out);
}
uint32_t bkCloudLastEpoch() { Preferences p; uint32_t v = 0; if (p.begin(BK_NVS_NS, true)) { v = p.getUInt("cloudt", 0); p.end(); } return v; }
// 0 = kunci dari eFuse chip | 1 = kunci cadangan di NVS (hilang bila flash dihapus total)
uint8_t bkCloudMode() { uint8_t c[32]; bool ok = bsChipSecret(c); bkZero(c, sizeof(c)); return ok ? (bsFallback ? 1 : 0) : 1; }
