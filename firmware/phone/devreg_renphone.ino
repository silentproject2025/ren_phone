// =====================================================================
// devreg_renphone.ino -- DEVREG_V2: daftar perangkat ke Supabase + USERNAME publik.
//
// Pendaftaran (register_device), SEKALI saja per pemasangan:
//   Kapan   : pertama kali HP menyala DAN WiFi tersambung. Sukses -> flag disimpan di NVS ("devreg"/"ok"),
//             setelah itu tidak ada lagi trafik untuk perangkat TANPA username.
//   Isi     : id anonim 16 hex (SHA-256 dari MAC eFuse + garam, MAC asli TIDAK dikirim), versi, build,
//             branch, jenis layar. Tanpa IP, SSID, atau data pribadi.
//
// Username (set_username), OPSIONAL dan hanya kalau pengguna mengisinya di Profil:
//   * Username tampil di dashboard publik. Id perangkat TIDAK pernah ditampilkan di dashboard publik.
//   * Format: 3-16 karakter, huruf/angka/titik/strip/garis bawah. Unik tanpa beda huruf besar-kecil.
//   * Perangkat BER-username mengirim register_device sekali tiap boot, supaya versi dan waktu
//     terakhir terlihat di dashboard tetap nyata. Perangkat tanpa username tetap diam seperti dulu.
//   * Kosongkan username = hilang dari dashboard.
//   * Hasil dari server (dipakai orang lain / tak valid / dicadangkan) dikembalikan ke UI Profil lewat
//     devRegNameTake(); nama lokal otomatis kembali ke nama terakhir yang diterima server.
//
// Cara    : HTTPS POST ke /rest/v1/rpc/... di task terpisah sekali jalan, jadi UI tidak tersendat.
//           Pendaftaran awal menunggu 45 dtk setelah boot. Perubahan username dari UI langsung diproses.
//           Butuh RAM internal cukup utk TLS; gagal -> coba lagi tiap 2 menit (maks 5x per boot).
// Mati    : RP_REG_URL kosong (devreg_conf.h) -> semua fungsi no-op.
// Skema   : supabase/setup.sql
// =====================================================================
#include "devreg_conf.h"
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>

// Urutan kompilasi .ino = alfabet, jadi makro dari ota_check_renphone.ino belum ada di sini: definisikan
// ulang IDENTIK (redefinisi identik legal) supaya urutan file tidak jadi masalah.
#ifndef RP_BUILD_NUM
#define RP_BUILD_NUM 0
#endif
#define RP_STR2(x) #x
#define RP_STR(x)  RP_STR2(x)
#ifndef RP_BRANCH
#define RP_BRANCH local
#endif
#ifndef RP_VERSION
#define RP_VERSION dev
#endif

#define DR_MIN_UPTIME_MS   45000UL
#define DR_RETRY_MS        120000UL
#define DR_MAX_FAILS       5
#define DR_MIN_INT_BLOCK   26000      // blok internal terbesar yg dibutuhkan handshake TLS (diturunkan dari 40000)
bool bkCloudToken(char* token65);      // backup_sync_renphone.ino: token (64 hex) turunan rahasia chip; false = tak tersedia

#define DR_NAME_MIN        3
#define DR_NAME_MAX        16

// Hasil sinkron username (devRegNameState / devRegNameTake)
#define DRN_IDLE      0   // tidak ada yg menunggu
#define DRN_PENDING   1   // menunggu WiFi / giliran kirim
#define DRN_OK        2   // username diterima server
#define DRN_CLEARED   3   // username dihapus dari server
#define DRN_TAKEN     4   // sudah dipakai perangkat lain
#define DRN_INVALID   5   // format ditolak server
#define DRN_RESERVED  6   // nama dicadangkan
#define DRN_NETFAIL   7   // gagal jaringan, akan dicoba lagi

static bool          drLoaded = false, drDone = false, drBusy = false, drPingedBoot = false, drUserKick = false;
static uint8_t       drFails = 0;
static unsigned long drNextMs = 0;
static char          drId[17] = {0};
static char          drName[DR_NAME_MAX + 1]   = {0};   // username yg DIINGINKAN pengguna (tersimpan NVS "uname")
static char          drSynced[DR_NAME_MAX + 1] = {0};   // username terakhir yg DITERIMA server (NVS "usync")
static volatile uint8_t drNameState  = DRN_IDLE;
static volatile uint8_t drNameNotify = DRN_IDLE;        // hasil akhir baru, dibaca sekali oleh UI
static portMUX_TYPE  drMux = portMUX_INITIALIZER_UNLOCKED;

static void drMakeId(){
  if(drId[0]) return;
  uint64_t mac = ESP.getEfuseMac();
  uint8_t in[16 + 6], out[32];
  memcpy(in, "ren_phone_devreg", 16);
  memcpy(in + 16, &mac, 6);
  mbedtls_sha256(in, sizeof(in), out, 0);
  for(int i = 0; i < 8; i++) sprintf(drId + i * 2, "%02x", out[i]);
}
// id anonim perangkat ini (utk ditampilkan di About Phone kalau mau mencocokkan dgn dashboard admin)
const char* devRegId(){ drMakeId(); return drId; }

static void drLoad(){
  if(drLoaded) return;
  drLoaded = true;
  Preferences p;
  if(p.begin("devreg", true)){
    drDone = p.getBool("ok", false);
    String a = p.getString("uname", ""), b = p.getString("usync", "");
    strncpy(drName, a.c_str(), DR_NAME_MAX);   drName[DR_NAME_MAX] = 0;
    strncpy(drSynced, b.c_str(), DR_NAME_MAX); drSynced[DR_NAME_MAX] = 0;
    p.end();
  }
  if(strcmp(drName, drSynced) != 0) drNameState = DRN_PENDING;
}

static void drSaveNames(){
  Preferences p;
  if(p.begin("devreg", false)){ p.putString("uname", drName); p.putString("usync", drSynced); p.end(); }
}

// ---- API untuk UI (owner_renphone.ino) ----
bool devRegNameValid(const char* n){
  if(!n) return false;
  size_t l = strlen(n);
  if(l == 0) return true;                       // kosong = hapus username
  if(l < DR_NAME_MIN || l > DR_NAME_MAX) return false;
  for(size_t i = 0; i < l; i++){
    char c = n[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    if(!ok) return false;
  }
  return true;
}
const char* devRegName(){ drLoad(); return drName; }
bool devRegNameSynced(){ drLoad(); return strcmp(drName, drSynced) == 0; }
uint8_t devRegNameState(){ drLoad(); return drNameState; }
// Hasil akhir sinkron yg belum dibaca UI (DRN_OK, DRN_TAKEN, ...), lalu direset. 0 = tak ada.
uint8_t devRegNameTake(){
  uint8_t v = drNameNotify;
  if(v) drNameNotify = DRN_IDLE;
  return v;
}
// Simpan username baru ("" = hapus). false jika format salah atau fitur dimatikan.
bool devRegSetName(const char* n){
  if(sizeof(RP_REG_URL) <= 1) return false;
  if(!devRegNameValid(n)) return false;
  drLoad();
  taskENTER_CRITICAL(&drMux);
  strncpy(drName, n, DR_NAME_MAX); drName[DR_NAME_MAX] = 0;
  taskEXIT_CRITICAL(&drMux);
  drSaveNames();
  if(strcmp(drName, drSynced) != 0){
    drNameState = DRN_PENDING;
    drFails = 0; drNextMs = 0; drUserKick = true;   // diproses langsung, tanpa tunggu 45 dtk uptime
  } else drNameState = DRN_IDLE;
  return true;
}

// POST JSON ke fungsi RPC. Mengembalikan kode HTTP (<0 = gagal jaringan). resp (opsional) diisi isi balasan.
static int drPost(WiFiClientSecure& net, const char* fn, const char* body, char* resp, size_t rn){
  HTTPClient http; http.setTimeout(10000); http.setConnectTimeout(10000);
  String url = String(RP_REG_URL) + "/rest/v1/rpc/" + fn;
  if(!http.begin(net, url)) return -1;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", RP_REG_KEY);
  // Kunci baru "sb_publishable_..." BUKAN JWT: cukup header apikey. Kunci lama (anon JWT "eyJ...") butuh Bearer juga.
  if(strncmp(RP_REG_KEY, "sb_", 3) != 0) http.addHeader("Authorization", String("Bearer ") + RP_REG_KEY);
  int code = http.POST((uint8_t*)body, strlen(body));
  if(resp && rn){
    resp[0] = 0;
    if(code >= 200 && code < 300){ String r = http.getString(); strncpy(resp, r.c_str(), rn - 1); resp[rn - 1] = 0; }
  }
  Serial.printf("[devreg] %s -> %d (%s)\n", fn, code, (code >= 200 && code < 300) ? "OK" : HTTPClient::errorToString(code).c_str());
  http.end();
  return code;
}

static void drTask(void*){
  drMakeId();
  const char* hw =
#ifdef RP_DISPLAY_ST7796
    "st7796";
#else
    "ili9341";
#endif
  char want[DR_NAME_MAX + 1];
  bool doReg;
  taskENTER_CRITICAL(&drMux);
  strncpy(want, drName, DR_NAME_MAX); want[DR_NAME_MAX] = 0;
  doReg = !drDone || (want[0] && !drPingedBoot);
  taskEXIT_CRITICAL(&drMux);
  const bool doName = (strcmp(want, drSynced) != 0);

  bool netOk = true, reRegister = false;
  uint8_t res = DRN_IDLE;                       // hasil akhir username utk UI
  {
    WiFiClientSecure net; net.setInsecure(); net.setTimeout(10);   // sama spt modul lain; payload tdk sensitif
    if(doReg){
      char body[256];
      snprintf(body, sizeof(body),
        "{\"p_device_id\":\"%s\",\"p_version\":\"%s\",\"p_build\":%d,\"p_branch\":\"%s\",\"p_hw\":\"%s\"}",
        drId, RP_STR(RP_VERSION), (int)RP_BUILD_NUM, RP_STR(RP_BRANCH), hw);
      int code = drPost(net, "register_device", body, nullptr, 0);
      if(code >= 200 && code < 300){
        drPingedBoot = true;
        if(!drDone){
          Preferences p;
          if(p.begin("devreg", false)){ p.putBool("ok", true); p.end(); }
          drDone = true;
        }
      } else netOk = false;
    }
    if(netOk && doName){
      char body[200], resp[48], tok[65];
      // token membuktikan kamu pemilik perangkat (kode perangkat saja bisa dihitung dari MAC). Lihat supabase/username_token.sql.
      if(bkCloudToken(tok)) snprintf(body, sizeof(body), "{\"p_device_id\":\"%s\",\"p_username\":\"%s\",\"p_token\":\"%s\"}", drId, want, tok);
      else                  snprintf(body, sizeof(body), "{\"p_device_id\":\"%s\",\"p_username\":\"%s\"}", drId, want);
      int code = drPost(net, "set_username", body, resp, sizeof(resp));
      if(code >= 200 && code < 300){
        if     (strstr(resp, "\"ok\""))             res = DRN_OK;
        else if(strstr(resp, "\"cleared\""))        res = DRN_CLEARED;
        else if(strstr(resp, "\"taken\""))          res = DRN_TAKEN;
        else if(strstr(resp, "\"invalid\""))        res = DRN_INVALID;
        else if(strstr(resp, "\"denied\""))         res = DRN_INVALID;   // perangkat ini terkunci pada token lain
        else if(strstr(resp, "\"reserved\""))       res = DRN_RESERVED;
        else if(strstr(resp, "unknown_device"))     { reRegister = true; netOk = false; }   // server tak kenal id ini (database direset): daftar ulang
        else netOk = false;
      } else netOk = false;
    }
  }

  if(reRegister){                               // paksa pendaftaran ulang di putaran berikutnya
    drDone = false;
    Preferences p;
    if(p.begin("devreg", false)){ p.putBool("ok", false); p.end(); }
  }

  if(res == DRN_OK || res == DRN_CLEARED){
    strncpy(drSynced, want, DR_NAME_MAX); drSynced[DR_NAME_MAX] = 0;
    drSaveNames();
    drNameState  = devRegNameSynced() ? DRN_IDLE : DRN_PENDING;
    drNameNotify = res;
  } else if(res == DRN_TAKEN || res == DRN_INVALID || res == DRN_RESERVED){
    // ditolak: kembalikan nama lokal ke yang terakhir diterima server (kalau pengguna belum mengetik yg lain lagi)
    taskENTER_CRITICAL(&drMux);
    if(strcmp(drName, want) == 0){ strncpy(drName, drSynced, DR_NAME_MAX); drName[DR_NAME_MAX] = 0; }
    taskEXIT_CRITICAL(&drMux);
    drSaveNames();
    drNameState  = devRegNameSynced() ? DRN_IDLE : DRN_PENDING;
    drNameNotify = res;
  } else if(!netOk){
    drFails++;
    drNextMs = millis() + DR_RETRY_MS;
    if(doName){
      if(drFails >= DR_MAX_FAILS){ drNameState = DRN_NETFAIL; drNameNotify = DRN_NETFAIL; }
      else drNameState = DRN_PENDING;
    }
  }
  drUserKick = false;
  drBusy = false;
  vTaskDelete(NULL);
}

void devRegTick(){
  if(sizeof(RP_REG_URL) <= 1) return;      // tak dikonfigurasi = mati total
  if(drBusy) return;
  drLoad();
  const bool needReg  = !drDone || (drName[0] && !drPingedBoot);
  const bool needName = (strcmp(drName, drSynced) != 0);
  if(!needReg && !needName) return;        // perangkat tanpa username yg sudah terdaftar: tidak ada trafik
  if(drFails >= DR_MAX_FAILS) return;
  const unsigned long now = millis();
  if(!drUserKick && now < DR_MIN_UPTIME_MS) return;
  if((int32_t)(drNextMs - now) > 0) return;
  if(WiFi.status() != WL_CONNECTED) return;
  if(!smEnsureContig(DR_MIN_INT_BLOCK)){
    drNextMs = now + 30000UL;               // RAM sempit (mis. app besar jalan): cek lagi nanti
    return;
  }
  drBusy = true;
  if(xTaskCreatePinnedToCore(drTask, "devreg", 10240, NULL, 1, NULL, 0) != pdPASS){ drBusy = false; drNextMs = now + 30000UL; }
}
