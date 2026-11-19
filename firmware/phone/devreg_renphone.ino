// =====================================================================
// devreg_renphone.ino -- DEVREG_V1: daftar perangkat anonim ke Supabase, SEKALI saja.
//
// Kapan   : pertama kali HP menyala DAN WiFi tersambung. Sukses -> flag disimpan di NVS ("devreg"/"ok"),
//           setelah itu tidak ada lagi trafik. (Flash ulang dgn hapus NVS = mendaftar lagi, baris yg sama
//           di-update, bukan dobel.)
// Isi     : id anonim 16 hex (SHA-256 dari MAC eFuse + garam, MAC asli TIDAK dikirim), versi, build,
//           branch, jenis layar. Tanpa IP, SSID, atau data pribadi.
// Cara    : HTTPS POST ke /rest/v1/rpc/register_device. Berjalan di task terpisah sekali jalan, jadi UI
//           tidak tersendat. Menunggu 45 dtk setelah boot dan RAM internal cukup utk TLS; gagal -> coba
//           lagi tiap 2 menit (maks 5x per boot, sisanya boot berikutnya).
// Mati    : RP_REG_URL kosong (devreg_conf.h) -> semua fungsi no-op.
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
#define DR_MIN_INT_BLOCK   40000      // blok internal terbesar yg dibutuhkan handshake TLS
static bool          drLoaded = false, drDone = false, drBusy = false;
static uint8_t       drFails = 0;
static unsigned long drNextMs = 0;
static char          drId[17] = {0};

static void drMakeId(){
  if(drId[0]) return;
  uint64_t mac = ESP.getEfuseMac();
  uint8_t in[16 + 6], out[32];
  memcpy(in, "ren_phone_devreg", 16);
  memcpy(in + 16, &mac, 6);
  mbedtls_sha256(in, sizeof(in), out, 0);
  for(int i = 0; i < 8; i++) sprintf(drId + i * 2, "%02x", out[i]);
}
// id anonim perangkat ini (utk ditampilkan di About Phone kalau mau mencocokkan dgn dashboard)
const char* devRegId(){ drMakeId(); return drId; }

static void drTask(void*){
  drMakeId();
  const char* hw =
#ifdef RP_DISPLAY_ST7796
    "st7796";
#else
    "ili9341";
#endif
  char body[256];
  snprintf(body, sizeof(body),
    "{\"p_device_id\":\"%s\",\"p_version\":\"%s\",\"p_build\":%d,\"p_branch\":\"%s\",\"p_hw\":\"%s\"}",
    drId, RP_STR(RP_VERSION), (int)RP_BUILD_NUM, RP_STR(RP_BRANCH), hw);
  bool ok = false;
  {
    WiFiClientSecure net; net.setInsecure(); net.setTimeout(10);   // sama spt modul lain; payload tdk sensitif
    HTTPClient http; http.setTimeout(10000); http.setConnectTimeout(10000);
    String url = String(RP_REG_URL) + "/rest/v1/rpc/register_device";
    if(http.begin(net, url)){
      http.addHeader("Content-Type", "application/json");
      http.addHeader("apikey", RP_REG_KEY);
      // Kunci baru "sb_publishable_..." BUKAN JWT: cukup header apikey. Kunci lama (anon JWT "eyJ...") butuh Bearer juga.
      if(strncmp(RP_REG_KEY, "sb_", 3) != 0) http.addHeader("Authorization", String("Bearer ") + RP_REG_KEY);
      int code = http.POST((uint8_t*)body, strlen(body));
      ok = (code >= 200 && code < 300);
      Serial.printf("[devreg] POST -> %d (%s)\n", code, ok ? "OK" : HTTPClient::errorToString(code).c_str());
      http.end();
    }
  }
  if(ok){
    Preferences p;
    if(p.begin("devreg", false)){ p.putBool("ok", true); p.end(); }
    drDone = true;
  } else { drFails++; drNextMs = millis() + DR_RETRY_MS; }
  drBusy = false;
  vTaskDelete(NULL);
}

void devRegTick(){
  if(sizeof(RP_REG_URL) <= 1) return;      // tak dikonfigurasi = mati total
  if(drDone || drBusy) return;
  if(!drLoaded){
    drLoaded = true;
    Preferences p;
    if(p.begin("devreg", true)){ drDone = p.getBool("ok", false); p.end(); }
    if(drDone) return;
  }
  if(drFails >= DR_MAX_FAILS) return;
  const unsigned long now = millis();
  if(now < DR_MIN_UPTIME_MS || (int32_t)(drNextMs - now) > 0) return;
  if(WiFi.status() != WL_CONNECTED) return;
  if(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < DR_MIN_INT_BLOCK){
    drNextMs = now + 30000UL;               // RAM sempit (mis. app besar jalan): cek lagi nanti
    return;
  }
  drBusy = true;
  if(xTaskCreatePinnedToCore(drTask, "devreg", 10240, NULL, 1, NULL, 0) != pdPASS){ drBusy = false; drNextMs = now + 30000UL; }
}
