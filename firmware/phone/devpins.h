#pragma once
// =====================================================================
// devpins.h -- DEVPINS_V1: pin yang bisa diubah dari app "Opsi Developer" (devopt_renphone.ino)
// =====================================================================
// Sebelumnya semua pin tertulis permanen (macro RP_* di display_conf.h + #define di phone.ino), jadi ganti pin
// = edit kode + build ulang. Sekarang tiap pin punya nilai AKTIF di rpPinV[] yang dibaca saat boot dari NVS;
// nilai bawaan tetap macro compile-time (papan tidak berubah kalau tidak ada yang diubah).
//
// KESELAMATAN (salah pin layar/sentuh = layar mati, tidak bisa dibenahi lewat layar itu sendiri):
//   "Terapkan" tidak langsung permanen. Konfigurasi baru disimpan sebagai PERCOBAAN ("new") lalu HP restart.
//     boot pertama dgn "new"   -> dipakai sbg percobaan (try=1). App minta konfirmasi "Simpan permanen".
//     konfirmasi               -> "new" jadi "cur" (permanen), try=0.
//     TIDAK dikonfirmasi lalu boot lagi (mati-nyalakan / crash / bootloop) -> "new" dibuang, kembali ke "cur".
//   Jadi selama layar bermasalah cukup cabut daya sekali, konfigurasi lama kembali sendiri.
//
// File ini hanya berisi logika murni (bisa dites di PC: tests/devpins) + glue NVS di bawah #ifdef ARDUINO.
// NVS dipakai LANGSUNG (bukan Preferences) karena di phone.ino "Preferences" diganti SdPrefs (simpan di SD),
// sedangkan pin SD sendiri harus sudah diketahui sebelum SD bisa dibaca.
// =====================================================================
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "display_conf.h"

// ---- nilai bawaan yang tidak ada di display_conf.h ----
#ifndef RP_BATT_ADC_PIN
#define DP_D_BATT_ADC   (-1)           // build tanpa pembagi tegangan baterai (ILI9341 / ST7796 biasa)
#else
#define DP_D_BATT_ADC   RP_BATT_ADC_PIN
#endif
#ifndef RP_BATT_DIV
#define RP_BATT_DIV     2.0f           // pembagi 10k x 10k
#endif
#ifndef RP_BATT_CAL_V
#define RP_BATT_CAL_V   0.032f         // koreksi titik ukur multimeter (3.61 V vs 3.58 V)
#endif
#ifndef PAD_NTC_PIN
#define DP_D_NTC        (-1)
#else
#define DP_D_NTC        PAD_NTC_PIN
#endif
#ifndef RP_TOUCH_RST
#define RP_TOUCH_RST    (-1)
#endif
#ifndef RP_TOUCH_SDA
#define RP_TOUCH_SDA    6
#define RP_TOUCH_SCL    7
#endif
#ifndef AUDIO_I2S_BCLK
#define AUDIO_I2S_BCLK 16
#endif
#ifndef AUDIO_I2S_LRC
#define AUDIO_I2S_LRC  17
#endif
#ifndef AUDIO_I2S_DOUT
  #if defined(RP_DISPLAY_ST7796) || defined(RP_BOARD_AETHER)
  #define AUDIO_I2S_DOUT 15
  #else
  #define AUDIO_I2S_DOUT 8
  #endif
#endif
#ifndef RP_PIN_SD_CS
#define RP_PIN_SD_CS    (-1)           // build SDMMC: tidak dipakai
#endif

// ---- ketersediaan pin di build ini (0/1) ----
#if RP_SD_SPI
  #define DPA_SD_SPI 1
  #define DPA_SD_MMC 0
#else
  #define DPA_SD_SPI 0
  #define DPA_SD_MMC 1
#endif
#if RP_TOUCH_CAPACITIVE
  #define DPA_TCH_I2C 1
  #define DPA_TCH_XPT 0
#else
  #define DPA_TCH_I2C 0
  #define DPA_TCH_XPT 1
#endif

// ---- flags ----
#define DPF_OPT   1    // boleh -1 (tidak dipakai)
#define DPF_ADC   2    // pin analog: sebaiknya ADC1 (GPIO 1..10) supaya aman bersama WiFi
#define DPF_LOCK  4    // DEVPINS_V3: disembunyikan dari Opsi Developer (layar & sentuh; tidak bisa diubah dari layar, tetap ikut cek bentrok)

// X(ID, nama tampil, grup, bawaan, tersedia, flags). Urutan = urutan di layar; pin satu grup harus berurutan.
#define DP_LIST(X) \
  X(SCLK,     "SCLK",             "Layar (SPI)",            RP_PIN_SCLK,    1,           DPF_LOCK)       \
  X(MOSI,     "MOSI",             "Layar (SPI)",            RP_PIN_MOSI,    1,           DPF_LOCK)       \
  X(MISO,     "MISO",             "Layar (SPI)",            RP_PIN_MISO,    1,           DPF_LOCK)       \
  X(TFT_CS,   "CS layar",         "Layar (SPI)",            RP_PIN_TFT_CS,  1,           DPF_LOCK)       \
  X(TFT_DC,   "DC layar",         "Layar (SPI)",            RP_PIN_TFT_DC,  1,           DPF_LOCK)       \
  X(TFT_RST,  "Reset layar",      "Layar (SPI)",            RP_PIN_TFT_RST, 1,           DPF_OPT | DPF_LOCK) \
  X(TFT_BL,   "Lampu latar",      "Layar (SPI)",            RP_PIN_TFT_BL,  1,           DPF_OPT | DPF_LOCK) \
  X(SD_CS,    "CS kartu SD",      "Kartu SD",               RP_PIN_SD_CS,   DPA_SD_SPI,  0)       \
  X(SD_CLK,   "SD CLK",           "Kartu SD",               39,             DPA_SD_MMC,  0)       \
  X(SD_CMD,   "SD CMD",           "Kartu SD",               38,             DPA_SD_MMC,  0)       \
  X(SD_D0,    "SD D0",            "Kartu SD",               40,             DPA_SD_MMC,  0)       \
  X(TCH_SDA,  "Sentuh SDA",       "Sentuh",                 RP_TOUCH_SDA,   DPA_TCH_I2C, DPF_LOCK)       \
  X(TCH_SCL,  "Sentuh SCL",       "Sentuh",                 RP_TOUCH_SCL,   DPA_TCH_I2C, DPF_LOCK)       \
  X(TCH_RST,  "Sentuh RST",       "Sentuh",                 RP_TOUCH_RST,   DPA_TCH_I2C, DPF_OPT | DPF_LOCK) \
  X(XPT_SCLK, "XPT2046 SCLK",     "Sentuh",                 6,              DPA_TCH_XPT, DPF_LOCK)       \
  X(XPT_MOSI, "XPT2046 MOSI",     "Sentuh",                 5,              DPA_TCH_XPT, DPF_LOCK)       \
  X(XPT_MISO, "XPT2046 MISO",     "Sentuh",                 4,              DPA_TCH_XPT, DPF_LOCK)       \
  X(XPT_CS,   "XPT2046 CS",       "Sentuh",                 9,              DPA_TCH_XPT, DPF_LOCK)       \
  X(SENS_SDA, "Sensor SDA",       "Bus sensor (MPU6050 / INA219)", RP_SENS_SDA, 1,       0)       \
  X(SENS_SCL, "Sensor SCL",       "Bus sensor (MPU6050 / INA219)", RP_SENS_SCL, 1,       0)       \
  X(BATT_ADC, "ADC baterai",      "Sensor analog (ADC)",    DP_D_BATT_ADC,  1,           DPF_OPT | DPF_ADC) \
  X(NTC,      "ADC suhu NTC",     "Sensor analog (ADC)",    DP_D_NTC,       1,           DPF_OPT | DPF_ADC) \
  X(VIB,      "Motor getar",      "Keluaran",               18,             1,           DPF_OPT) \
  X(NPX,      "Neopixel",         "Keluaran",               48,             1,           DPF_OPT) \
  X(MIC_SCK,  "Mic SCK",          "Mikrofon (I2S)",         47,             1,           0)       \
  X(MIC_WS,   "Mic WS",           "Mikrofon (I2S)",         46,             1,           0)       \
  X(MIC_SD,   "Mic SD",           "Mikrofon (I2S)",         45,             1,           0)       \
  X(I2S_BCLK, "Speaker BCLK",     "Speaker (I2S)",          AUDIO_I2S_BCLK, 1,           0)       \
  X(I2S_LRC,  "Speaker LRC",      "Speaker (I2S)",          AUDIO_I2S_LRC,  1,           0)       \
  X(I2S_DOUT, "Speaker DOUT",     "Speaker (I2S)",          AUDIO_I2S_DOUT, 1,           0)       \
  X(UART_RX,  "ESP32-CAM RX",     "ESP32-CAM (UART)",       42,             1,           0)       \
  X(UART_TX,  "ESP32-CAM TX",     "ESP32-CAM (UART)",       41,             1,           0)

#define DP_X_ENUM(id, nm, grp, def, av, fl) DP_##id,
enum DpId { DP_LIST(DP_X_ENUM) DP_COUNT };

struct DpDef { const char* name; const char* group; int8_t def; uint8_t avail; uint8_t flags; };
#define DP_X_DEF(id, nm, grp, def, av, fl) { nm, grp, (int8_t)(def), (uint8_t)(av), (uint8_t)(fl) },
static const DpDef DP_DEFS[DP_COUNT] = { DP_LIST(DP_X_DEF) };

#define DP_X_VAL(id, nm, grp, def, av, fl) (int8_t)(def),
static int8_t rpPinV[DP_COUNT] = { DP_LIST(DP_X_VAL) };      // nilai AKTIF (dibaca semua kode lewat sini)

#define DP_UNSET   127      // di blob = "pakai bawaan"
#define DP_MAXN    48

// kalibrasi pembagi/ofset ADC baterai (bisa diubah live dari app; disimpan terpisah, tanpa restart)
static float devBattDiv = RP_BATT_DIV;
static float devBattCal = RP_BATT_CAL_V;

// status boot: 0 = normal, 1 = percobaan pin baru aktif (menunggu konfirmasi), 2 = percobaan sebelumnya dibatalkan otomatis
static uint8_t rpPinBootState = 0;

// ---------------------------------------------------------------------
// aturan pin ESP32-S3
// ---------------------------------------------------------------------
static inline bool dpGpioExists(int g) { return (g >= 0 && g <= 21) || (g >= 38 && g <= 48); }   // 22-25 tak ada, 26-32 flash, 33-37 PSRAM oktal
static inline bool dpIsAdc1(int g)     { return g >= 1 && g <= 10; }
static inline uint8_t dpWarnKind(int g) {                    // 0 aman, 1 strapping, 2 USB
  if (g == 0 || g == 3 || g == 45 || g == 46) return 1;
  if (g == 19 || g == 20) return 2;
  return 0;
}
static inline bool dpValueOk(int id, int v) {
  if (v == -1) return (DP_DEFS[id].flags & DPF_OPT) != 0;
  return dpGpioExists(v);
}
// langkah berikutnya (dir = +1 / -1) dengan melompati GPIO yang tidak ada; -1 hanya untuk pin opsional; membungkus.
static inline int dpStep(int id, int cur, int dir) {
  const bool opt = (DP_DEFS[id].flags & DPF_OPT) != 0;
  int lo = opt ? -1 : 0, hi = 48;
  int v = cur;
  for (int n = 0; n < 64; n++) {
    v += (dir >= 0) ? 1 : -1;
    if (v > hi) v = lo;
    if (v < lo) v = hi;
    if (dpValueOk(id, v)) return v;
  }
  return cur;
}
// pin lain (yang tersedia di build ini) yang memakai GPIO sama; hanya dilaporkan kalau salah satunya BEDA dari bawaan
static inline int dpConflict(const int8_t* val, int id) {
  if (val[id] < 0 || !DP_DEFS[id].avail) return -1;
  for (int o = 0; o < DP_COUNT; o++) {
    if (o == id || !DP_DEFS[o].avail || val[o] != val[id]) continue;
    if (val[id] != DP_DEFS[id].def || val[o] != DP_DEFS[o].def) return o;
  }
  return -1;
}
static inline int dpChangedCount(const int8_t* val) {
  int n = 0;
  for (int i = 0; i < DP_COUNT; i++) if (DP_DEFS[i].avail && val[i] != DP_DEFS[i].def) n++;
  return n;
}
static inline int dpErrorCount(const int8_t* val) {          // nilai tak valid (tidak ada di chip) atau bentrok
  int n = 0;
  for (int i = 0; i < DP_COUNT; i++) {
    if (!DP_DEFS[i].avail) continue;
    if (!dpValueOk(i, val[i]) || dpConflict(val, i) >= 0) n++;
  }
  return n;
}

// ---------------------------------------------------------------------
// blob (nilai yang DIGANTI saja; DP_UNSET = bawaan) + checksum
// ---------------------------------------------------------------------
#define DP_MAGIC 0x44503156u        // "DP1V"
struct DpBlob { uint32_t magic; uint8_t n; uint8_t pad[3]; int8_t ov[DP_MAXN]; uint32_t crc; };

static inline uint32_t dpCrc(const DpBlob& b) {
  const uint8_t* p = (const uint8_t*)&b; uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(DpBlob, crc); i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}
static inline void dpEncode(const int8_t* eff, DpBlob& b) {  // nilai aktif -> blob (sama dgn bawaan = DP_UNSET)
  memset(&b, 0, sizeof(b));
  b.magic = DP_MAGIC; b.n = DP_COUNT;
  for (int i = 0; i < DP_MAXN; i++) b.ov[i] = DP_UNSET;
  for (int i = 0; i < DP_COUNT; i++) b.ov[i] = (eff[i] == DP_DEFS[i].def) ? (int8_t)DP_UNSET : eff[i];
  b.crc = dpCrc(b);
}
static inline bool dpDecode(const DpBlob& b, int8_t* eff) {  // false kalau blob rusak; eff diisi bawaan lalu ditimpa override yg valid
  if (b.magic != DP_MAGIC || b.n > DP_MAXN || b.crc != dpCrc(b)) return false;
  for (int i = 0; i < DP_COUNT; i++) {
    int8_t v = (i < b.n) ? b.ov[i] : (int8_t)DP_UNSET;
    eff[i] = (v == DP_UNSET || !DP_DEFS[i].avail || !dpValueOk(i, v)) ? DP_DEFS[i].def : v;   // nilai aneh -> bawaan (jangan sampai brick)
  }
  return true;
}

// ---------------------------------------------------------------------
// keputusan boot
// ---------------------------------------------------------------------
struct DpBootPlan { bool useNew; bool discardNew; bool useCur; uint8_t newTries; };
static inline DpBootPlan dpBootPlan(bool hasCur, bool hasNew, uint8_t tries) {
  DpBootPlan p; p.useNew = false; p.discardNew = false; p.useCur = hasCur; p.newTries = 0;
  if (hasNew) {
    if (tries == 0) { p.useNew = true; p.useCur = false; p.newTries = 1; }   // boot percobaan pertama
    else            { p.discardNew = true; }                                  // percobaan sebelumnya tak dikonfirmasi -> batalkan
  }
  return p;
}

// ---------------------------------------------------------------------
// glue NVS (hanya di firmware)
// ---------------------------------------------------------------------
#if defined(ARDUINO)
#include "nvs.h"
static bool dpNvsGetBlob(nvs_handle_t h, const char* key, DpBlob& b) {
  size_t len = sizeof(DpBlob);
  if (nvs_get_blob(h, key, &b, &len) != ESP_OK || len != sizeof(DpBlob)) return false;
  int8_t tmp[DP_COUNT];
  return dpDecode(b, tmp);                                   // sekaligus cek magic/crc
}
static void dpNvsPutF(nvs_handle_t h, const char* key, float f) { uint32_t u; memcpy(&u, &f, 4); nvs_set_u32(h, key, u); }
static bool dpNvsGetF(nvs_handle_t h, const char* key, float& f) { uint32_t u; if (nvs_get_u32(h, key, &u) != ESP_OK) return false; memcpy(&f, &u, 4); return true; }

// Dipanggil PALING AWAL di setup() (sebelum SdPrefs / display.init / initSD).
static void devpinsBoot() {
  nvs_handle_t h;
  if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return;
  DpBlob cur, nw; uint8_t tries = 0;
  bool hasCur = dpNvsGetBlob(h, "cur", cur), hasNew = dpNvsGetBlob(h, "new", nw);
  nvs_get_u8(h, "try", &tries);
  DpBootPlan pl = dpBootPlan(hasCur, hasNew, tries);
  if (pl.discardNew) { nvs_erase_key(h, "new"); nvs_set_u8(h, "try", 0); nvs_commit(h); rpPinBootState = 2; }
  if (pl.useNew)     { nvs_set_u8(h, "try", 1); nvs_commit(h); }   // tulis DULU sebelum dipakai: kalau crash, boot berikut membatalkan
  if (pl.useNew)      { if (dpDecode(nw, rpPinV))  rpPinBootState = 1; }
  else if (pl.useCur) { dpDecode(cur, rpPinV); }
  float f;
  if (dpNvsGetF(h, "div", f) && f > 0.1f && f < 20.0f)  devBattDiv = f;
  if (dpNvsGetF(h, "cal", f) && f > -2.0f && f < 2.0f)  devBattCal = f;
  nvs_close(h);
}
// simpan konfigurasi BARU sebagai percobaan (belum permanen). Setelah ini HP harus restart.
static bool devpinsSaveTrial(const int8_t* eff) {
  nvs_handle_t h; if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return false;
  DpBlob b; dpEncode(eff, b);
  bool ok = nvs_set_blob(h, "new", &b, sizeof(b)) == ESP_OK && nvs_set_u8(h, "try", 0) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h); return ok;
}
// konfirmasi: nilai aktif (yang sedang dicoba) jadi permanen
static bool devpinsCommit() {
  nvs_handle_t h; if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return false;
  DpBlob b; dpEncode(rpPinV, b);
  bool ok = nvs_set_blob(h, "cur", &b, sizeof(b)) == ESP_OK;
  nvs_erase_key(h, "new"); nvs_set_u8(h, "try", 0);
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h); if (ok) rpPinBootState = 0; return ok;
}
// batalkan percobaan (dipakai tombol "Batalkan & restart")
static void devpinsDiscardTrial() {
  nvs_handle_t h; if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "new"); nvs_set_u8(h, "try", 0); nvs_commit(h); nvs_close(h);
}
// kembali ke bawaan papan (hapus semua override pin + kalibrasi ADC)
static void devpinsEraseAll() {
  nvs_handle_t h; if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "cur"); nvs_erase_key(h, "new"); nvs_erase_key(h, "div"); nvs_erase_key(h, "cal");
  nvs_set_u8(h, "try", 0); nvs_commit(h); nvs_close(h);
}
static void devpinsSaveBattCal() {
  nvs_handle_t h; if (nvs_open("devpins", NVS_READWRITE, &h) != ESP_OK) return;
  dpNvsPutF(h, "div", devBattDiv); dpNvsPutF(h, "cal", devBattCal); nvs_commit(h); nvs_close(h);
}
#endif
