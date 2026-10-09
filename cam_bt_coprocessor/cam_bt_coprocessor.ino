/**
 * ============================================================
 *  ESP32-CAM  --  Bluetooth A2DP Co-Processor untuk Ren Phone
 *
 *  Turunan dari esp32_bluetooth-music-player, tapi SEMUA otak
 *  (playlist, tombol, OLED, UI) dipindah ke ESP32-S3 (Ren Phone).
 *  Board ini cuma:
 *    UART (dari S3) -> buffer MP3 -> decode Helix -> buffer PCM -> A2DP -> TWS
 *
 *  Kabel ke S3 (3 kabel + power):
 *    S3 GPIO17 (TX) -> CAM GPIO13 (RX)
 *    S3 GPIO16 (RX) <- CAM GPIO14 (TX)
 *    GND            -- GND
 *    5V (pin 5V CAM, BUKAN 3V3)
 *  Kartu SD di CAM TIDAK dipakai: CABUT kartunya dari slot (GPIO13/14 tersambung ke slot SD).
 *  UART0 (GPIO1/3) bebas buat flash & log debug.
 *
 *  Arduino IDE / arduino-cli:
 *    Board : AI Thinker ESP32-CAM (PSRAM 4MB), Partition Scheme: Huge APP
 *    PSRAM : Enabled
 *    Library: AudioTools, ESP32-A2DP (pschatzmann)
 *
 *  Batasan v1: MP3 Layer III 44.1 kHz stereo saja (WAV/EQ/crossfade
 *  sengaja belum ikut).
 *  AVRCP Target: tombol earbuds (play/pause/next/prev/vol) diteruskan ke S3 lewat RP_KEY.
 * ============================================================
 */
#include <Arduino.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_system.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_avrc_api.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "BluetoothA2DPSource.h"
#include "rplink.h"
#include "ring.h"

// ----------------------------------------------------------
// KONFIGURASI
// ----------------------------------------------------------
#define BT_DEFAULT_NAME       "HUOREI-BS10"
#define LINK_RX_PIN           13      // <- TX S3
#define LINK_TX_PIN           14      // -> RX S3
#define IN_RING_PSRAM         (512*1024)   // buffer MP3 mentah (~32 dtk @128kbps)
#define IN_RING_FALLBACK      (24*1024)
#define PCM_RING_PSRAM        (512*1024)   // buffer PCM hasil decode (~3 dtk)
#define PCM_RING_FALLBACK     (48*1024)
#define IN_CHUNK              2048
#define PREFILL_BYTES         (32*1024)
#define TRACK_END_SILENCE_CB  120     // callback kosong berturut2 sebelum dianggap selesai
#define DECODE_TASK_STACK     32768
#define STATUS_INTERVAL_MS    40
#define BT_RECONNECT_MS       8000
#define BT_SCAN_DURATION      8
#define BT_MAX_DEVICES        12
#define TX_GAIN_DEFAULT       7
#define TX_GAIN_LEVELS        8
#define BYTES_PER_SEC         176400  // 44.1kHz * 2ch * 16bit
#define DEC_MIN_BLOCK         (14*1024) // blok kontigu RAM internal minimal sblm bangun ulang decoder Helix
#define DIAG_INTERVAL_MS      5000      // log diagnosa memori/link ke Serial USB-TTL CAM
#define RP_AVRCP_ENABLE       1         // 0 = matikan total AVRCP Target (utk isolasi bug memori/link); tombol earbuds tidak jalan

static const esp_power_level_t txGainEnum[TX_GAIN_LEVELS] = {
  ESP_PWR_LVL_N12, ESP_PWR_LVL_N9, ESP_PWR_LVL_N6, ESP_PWR_LVL_N3,
  ESP_PWR_LVL_N0,  ESP_PWR_LVL_P3, ESP_PWR_LVL_P6, ESP_PWR_LVL_P9
};

// ----------------------------------------------------------
// STATE
// ----------------------------------------------------------
static Ring inRing, pcmRing;
static uint32_t prefillBytes = PREFILL_BYTES;

static volatile bool     flushReq     = false;  // rx task -> decode task
static std::atomic<bool> streamEnded{false};    // 'F' diterima
static volatile bool     decodeDone   = false;  // input habis & decoder di-flush
static volatile bool     haveTrack    = false;
static volatile bool     paused       = false;
static volatile bool     trackEnded   = false;
static volatile bool     prefillReady = false;
static volatile bool     decErr       = false;   // decoder tidak menghasilkan PCM sama sekali (biasanya RAM internal habis/terfragmentasi)
static volatile uint32_t pcmProduced  = 0;       // byte PCM yg keluar dari decoder utk track ini
static TaskHandle_t      decodeTaskH = nullptr, linkTaskH = nullptr;
static volatile bool     btCbRunning  = false;
static volatile int32_t  trackEndSilence = 0;
static volatile uint32_t bytesPlayedTotal = 0;
static volatile int      gainCur = 0, gainTarget = 256;  // 0..256, ramp per sample
static volatile uint8_t  curSeq = 0;
// fitur audio utk RYNE (S3): dihitung dari PCM SEBELUM gain/volume, jadi mencerminkan lagu, bukan volume pendengar
static volatile uint32_t accSum = 0, accCnt = 0, accZc = 0;
static int16_t           accPrev = 0;
static bool              endedSent = false;

static volatile bool     btConnected = false;
static uint32_t          btDisconnectedAt = 0;
static volatile bool     btEnabled = true;   // saklar BT dari S3 (tombol Control Center)
static volatile bool     isScanning = false, scanDone = false;

static uint8_t           volumeNow = 80;
static volatile uint8_t  pendingVol = 80;
static volatile bool     volDirty = false;
static int               txGain = TX_GAIN_DEFAULT;
static char              savedBTName[64] = {0};
static bool              prefsDirty = false;

struct BTDev { char name[40]; uint8_t bda[6]; int rssi; bool hasName; };
static BTDev scanned[BT_MAX_DEVICES];
static volatile int scannedCount = 0;

struct Cmd { uint8_t type; uint8_t len; uint8_t data[48]; };
static QueueHandle_t cmdQ = nullptr;

static RpParser parser;
BluetoothA2DPSource a2dp;

// ----------------------------------------------------------
// SINK PCM: dipanggil decoder Helix, tulis ke ring PCM (blocking kalau penuh)
// ----------------------------------------------------------
class PcmSink : public Print {
 public:
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    size_t done = 0;
    while (done < len) {
      if (flushReq) return len;                 // lagu dibuang, jangan nyangkut
      uint32_t fr = pcmRing.freeSpace();
      if (fr == 0) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
      uint32_t n = (uint32_t)(len - done);
      if (n > fr) n = fr;
      pcmRing.write(data + done, n);
      done += n;
      pcmProduced += n;
    }
    return len;
  }
  int availableForWrite() override { return 4096; }
};
static PcmSink pcmSink;

// ----------------------------------------------------------
// TX POWER
// ----------------------------------------------------------
void applyTxGain(int idx) {
  idx = constrain(idx, 0, TX_GAIN_LEVELS - 1);
  esp_bredr_tx_power_set(txGainEnum[idx], txGainEnum[idx]);
  txGain = idx;
}

void savePrefs() {
  Preferences p; p.begin("btco", false);
  p.putString("btname", String(savedBTName));
  p.putInt("txgain", txGain);
  p.end();
}

// ----------------------------------------------------------
// CALLBACK A2DP: tarik PCM dari ring ke TWS
// ----------------------------------------------------------
int32_t IRAM_ATTR getSoundData(Frame* fb, int32_t frameCount) {
  int32_t byteCount = frameCount * (int32_t)sizeof(Frame);
  btCbRunning = true;

  bool silent = !haveTrack || (paused && gainCur == 0);
  if (!silent && !prefillReady) {
    if (pcmRing.avail() >= prefillBytes || decodeDone) prefillReady = true;
    else silent = true;
  }
  if (silent) {
    memset(fb, 0, byteCount);
    btCbRunning = false;
    return frameCount;
  }

  int32_t av = (int32_t)pcmRing.avail();
  if (decodeDone && av <= 0) {
    // semua sudah habis -- tunggu ~1 dtk keheningan biar ekor lagu tuntas
    if (++trackEndSilence >= TRACK_END_SILENCE_CB) trackEnded = true;
    memset(fb, 0, byteCount);
    btCbRunning = false;
    return frameCount;
  }
  trackEndSilence = 0;
  if (!decodeDone && av < byteCount) {          // underrun: diam dulu, jangan buang data
    memset(fb, 0, byteCount);
    btCbRunning = false;
    return frameCount;
  }

  int32_t tr = (av < byteCount) ? av : byteCount;
  pcmRing.read((uint8_t*)fb, (uint32_t)tr);
  if (tr < byteCount) memset((uint8_t*)fb + tr, 0, byteCount - tr);
  bytesPlayedTotal += (uint32_t)tr;

  // ramp gain per sample (jeda/lanjut tanpa "tek", ~6 ms) + akumulasi fitur audio
  int g = gainCur, gt = gainTarget;
  uint32_t aS = 0, aZ = 0; int16_t pv = accPrev;
  for (int32_t i = 0; i < frameCount; i++) {
    int16_t sm = fb[i].channel1;
    aS += (uint32_t)(sm < 0 ? -(int32_t)sm : (int32_t)sm);
    if ((sm ^ pv) < 0) aZ++;
    pv = sm;
    if (g < gt) g++; else if (g > gt) g--;
    if (g != 256) {
      fb[i].channel1 = (int16_t)(((int32_t)fb[i].channel1 * g) >> 8);
      fb[i].channel2 = (int16_t)(((int32_t)fb[i].channel2 * g) >> 8);
    }
  }
  gainCur = g;
  accPrev = pv; accSum += aS; accZc += aZ; accCnt += (uint32_t)frameCount;

  btCbRunning = false;
  return frameCount;
}

// ----------------------------------------------------------
// AVRCP TARGET: tombol di earbuds -> antrean -> loop() kirim RP_KEY ke S3
// (callback jalan di task BT, jadi JANGAN kirim UART dari sini -- cukup masukkan ke antrean)
// ----------------------------------------------------------
#define KEY_PLAY    1
#define KEY_PAUSE   2
#define KEY_NEXT    3
#define KEY_PREV    4
#define KEY_VOLUP   5
#define KEY_VOLDOWN 6
#define KEY_ABSVOL  7

struct KeyEvt { uint8_t code; uint8_t arg; };
static QueueHandle_t       keyQ = nullptr;
static volatile bool       avrcpStackReady = false;
static volatile bool       avrcpNeedInit = false;
static uint32_t            avrcpInitAt = 0;

static void keyPush(uint8_t code, uint8_t arg) {
  if (!keyQ) return;
  KeyEvt e; e.code = code; e.arg = arg;
  xQueueSend(keyQ, &e, 0);
}

static void avrc_tg_callback(esp_avrc_tg_cb_event_t ev, esp_avrc_tg_cb_param_t* p) {
  if (ev == ESP_AVRC_TG_PASSTHROUGH_CMD_EVT) {
    if (p->psth_cmd.key_state != 0) return;          // 0 = ditekan; abaikan event "dilepas"
    switch (p->psth_cmd.key_code) {
      case ESP_AVRC_PT_CMD_PLAY:     keyPush(KEY_PLAY, 0);    break;
      case ESP_AVRC_PT_CMD_PAUSE:    keyPush(KEY_PAUSE, 0);   break;
      case ESP_AVRC_PT_CMD_STOP:     keyPush(KEY_PAUSE, 0);   break;
      case ESP_AVRC_PT_CMD_FORWARD:  keyPush(KEY_NEXT, 0);    break;
      case ESP_AVRC_PT_CMD_BACKWARD: keyPush(KEY_PREV, 0);    break;
      case ESP_AVRC_PT_CMD_VOL_UP:   keyPush(KEY_VOLUP, 0);   break;
      case ESP_AVRC_PT_CMD_VOL_DOWN: keyPush(KEY_VOLDOWN, 0); break;
      default: break;
    }
  } else if (ev == ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT) {
    keyPush(KEY_ABSVOL, (uint8_t)(p->set_abs_vol.volume & 0x7F));
  }
}

static void avrcpStackInit() {
#if !RP_AVRCP_ENABLE
  Serial.println("[AVRCP] DIMATIKAN (RP_AVRCP_ENABLE=0)");
  return;
#endif
  uint32_t h0 = ESP.getFreeHeap(), b0 = ESP.getMaxAllocHeap();
  esp_err_t r = esp_avrc_tg_init();
  avrcpStackReady = (r == ESP_OK || r == ESP_ERR_INVALID_STATE);   // INVALID_STATE = sudah di-init library
  Serial.printf("[AVRCP] tg_init=%d ready=%d | heap %u->%u  blokMax %u->%u\n", (int)r, (int)avrcpStackReady,
                (unsigned)h0, (unsigned)ESP.getFreeHeap(), (unsigned)b0, (unsigned)ESP.getMaxAllocHeap());
}

// dipanggil ~0,5 dtk setelah TWS tersambung (urutan sama dengan proyek referensi)
static void avrcpSetupFilter() {
#if !RP_AVRCP_ENABLE
  return;
#endif
  if (!avrcpStackReady) return;
  uint32_t h0 = ESP.getFreeHeap();
  if (esp_avrc_tg_register_callback(avrc_tg_callback) != ESP_OK) { Serial.println("[AVRCP] register_callback gagal"); return; }
  esp_avrc_psth_bit_mask_t cs; memset(&cs, 0, sizeof(cs));
  if (esp_avrc_tg_get_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, &cs) != ESP_OK) {
    memset(&cs, 0, sizeof(cs));
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_PLAY);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_PAUSE);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_STOP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_FORWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_BACKWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_VOL_UP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &cs, ESP_AVRC_PT_CMD_VOL_DOWN);
  }
  esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &cs);
  esp_avrc_rn_evt_cap_mask_t es; memset(&es, 0, sizeof(es));
  esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &es, ESP_AVRC_RN_VOLUME_CHANGE);
  esp_avrc_tg_set_rn_evt_cap(&es);
  Serial.printf("[AVRCP] filter terpasang | heap %u->%u blokMax=%u\n", (unsigned)h0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void onConnectionChanged(esp_a2d_connection_state_t state, void* obj) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    btConnected = true; btDisconnectedAt = 0;
    applyTxGain(txGain);
    a2dp.set_volume(volumeNow);
    avrcpInitAt = millis(); avrcpNeedInit = true;     // pasang filter AVRCP 0,5 dtk lagi (dari loop)
    Serial.println("[BT] Terhubung");
  } else {
    btConnected = false;
    avrcpNeedInit = false;
    btDisconnectedAt = millis();
    Serial.println("[BT] Terputus");
  }
}

// ----------------------------------------------------------
// SCAN BT
// ----------------------------------------------------------
static void gap_callback(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t* p) {
  if (ev == ESP_BT_GAP_DISC_RES_EVT) {
    if (scannedCount >= BT_MAX_DEVICES) return;
    BTDev dev; memset(&dev, 0, sizeof(dev));
    memcpy(dev.bda, p->disc_res.bda, 6); dev.rssi = -100;
    for (int i = 0; i < p->disc_res.num_prop; i++) {
      auto* pr = &p->disc_res.prop[i];
      if (pr->type == ESP_BT_GAP_DEV_PROP_EIR) {
        uint8_t nl = 0;
        uint8_t* np = esp_bt_gap_resolve_eir_data((uint8_t*)pr->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &nl);
        if (!np) np = esp_bt_gap_resolve_eir_data((uint8_t*)pr->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &nl);
        if (np && nl > 0) {
          if (nl > 39) nl = 39;
          memcpy(dev.name, np, nl); dev.name[nl] = 0; dev.hasName = true;
        }
      } else if (pr->type == ESP_BT_GAP_DEV_PROP_RSSI) {
        dev.rssi = *((int8_t*)pr->val);
      } else if (pr->type == ESP_BT_GAP_DEV_PROP_BDNAME && !dev.hasName && pr->len > 0) {
        int nl2 = pr->len; if (nl2 > 39) nl2 = 39;
        memcpy(dev.name, pr->val, nl2); dev.name[nl2] = 0; dev.hasName = true;
      }
    }
    if (!dev.hasName || !dev.name[0])
      snprintf(dev.name, sizeof(dev.name), "BT:%02X:%02X:%02X", dev.bda[3], dev.bda[4], dev.bda[5]);
    bool dup = false;
    for (int i = 0; i < scannedCount; i++) {
      if (memcmp(scanned[i].bda, dev.bda, 6) == 0) {
        if (dev.hasName) { memcpy(scanned[i].name, dev.name, sizeof(dev.name)); scanned[i].hasName = true; }
        if (dev.rssi > scanned[i].rssi) scanned[i].rssi = dev.rssi;
        dup = true; break;
      }
    }
    if (!dup) scanned[scannedCount++] = dev;
  } else if (ev == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) { isScanning = false; scanDone = true; }
    else if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) { isScanning = true; scanDone = false; }
  }
}

static void startScan() {
  scannedCount = 0;
  memset(scanned, 0, sizeof(scanned));
  esp_bt_gap_register_callback(gap_callback);
  esp_err_t r = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_SCAN_DURATION, 0);
  isScanning = (r == ESP_OK);
  if (!isScanning) scanDone = true;
}

static void sendScanResults() {
  for (int i = 0; i < scannedCount; i++) {
    uint8_t b[1 + 40];
    b[0] = (uint8_t)(int8_t)scanned[i].rssi;
    int nl = strlen(scanned[i].name); if (nl > 39) nl = 39;
    memcpy(b + 1, scanned[i].name, nl);
    rpSend(Serial2, RP_SCANRES, b, 1 + nl);
  }
  rpSend(Serial2, RP_SCANDONE, nullptr, 0);
}

// Saklar Bluetooth dari tombol Control Center (RP_BTMODE).
static void btSetEnabled(bool on) {
  btEnabled = on;
  if (on) {
    a2dp.set_auto_reconnect(true);
    btDisconnectedAt = millis();     // loop() akan memanggil reconnect() setelah jeda
  } else {
    a2dp.set_auto_reconnect(false);
    btDisconnectedAt = 0;            // hentikan upaya reconnect manual
    if (btConnected) a2dp.disconnect();
  }
}

static void connectTo(const char* name) {
  strncpy(savedBTName, name, sizeof(savedBTName) - 1);
  savedBTName[sizeof(savedBTName) - 1] = 0;
  prefsDirty = true;
  btEnabled = true;                  // menyambung ke TWS = BT otomatis nyala
  a2dp.set_auto_reconnect(true);
  a2dp.start(savedBTName);
}

// ----------------------------------------------------------
// DECODE TASK (core 0): ring MP3 -> Helix -> ring PCM
// ----------------------------------------------------------
static void decodeTask(void* param) {
  uint8_t* buf = (uint8_t*)(psramFound() ? ps_malloc(IN_CHUNK) : malloc(IN_CHUNK));
  if (!buf) { Serial.println("[DEC] gagal alokasi"); vTaskDelete(nullptr); return; }

  MP3DecoderHelix*     codec = nullptr;
  EncodedAudioStream*  enc   = nullptr;
  uint32_t inBytesTrack = 0;                 // byte MP3 yg sudah disuapkan ke decoder utk track ini
  auto rebuild = [&]() {
    if (enc)   { enc->end(); delete enc; enc = nullptr; }
    if (codec) { delete codec; codec = nullptr; }
    // Helix butuh beberapa blok kontigu (~10KB). Kalau RAM internal terfragmentasi, kasih waktu stack BT lepas buffer.
    for (int i = 0; i < 6 && ESP.getMaxAllocHeap() < DEC_MIN_BLOCK; i++) vTaskDelay(pdMS_TO_TICKS(40));
    uint32_t blk = ESP.getMaxAllocHeap();
    if (blk < DEC_MIN_BLOCK) Serial.printf("[DEC] PERINGATAN: blok terbesar cuma %u B (< %u) -> decoder bisa gagal\n", (unsigned)blk, (unsigned)DEC_MIN_BLOCK);
    codec = new MP3DecoderHelix();
    enc   = new EncodedAudioStream(&pcmSink, codec);
    enc->begin();
    Serial.printf("[DEC] decoder dibangun. heap=%u blokMax=%u min=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
  };
  rebuild();

  for (;;) {
    if (flushReq) {
      haveTrack = false;                 // callback BT -> diam, tidak nyentuh ring
      rebuild();
      uint32_t t0 = millis();
      while (btCbRunning && millis() - t0 < 50) taskYIELD();
      vTaskDelay(pdMS_TO_TICKS(2));
      pcmRing.reset(); inRing.reset();
      streamEnded = false; decodeDone = false; trackEnded = false;
      trackEndSilence = 0; prefillReady = false; bytesPlayedTotal = 0;
      pcmProduced = 0; inBytesTrack = 0;
      flushReq = false;
      continue;
    }
    if (decodeDone) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    uint32_t av = inRing.avail();
    if (av == 0) {
      // urutan penting: baca streamEnded DULU baru cek ring lagi
      if (streamEnded.load() && inRing.avail() == 0) {
        enc->end();                      // bilas sisa frame di decoder
        if (inBytesTrack > 16384 && pcmProduced == 0) {
          // MP3 masuk puluhan KB tapi nol PCM keluar = decoder mati (alokasi Helix gagal). JANGAN dianggap "lagu habis"
          decErr = true;
          Serial.printf("[DEC] ERROR: %u B MP3 masuk, 0 B PCM keluar. heap=%u blokMax=%u\n",
                        (unsigned)inBytesTrack, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        }
        decodeDone = true;
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }
    uint32_t n = inRing.read(buf, av > IN_CHUNK ? IN_CHUNK : av);
    enc->write(buf, n);
    inBytesTrack += n;
    taskYIELD();
  }
}

// ----------------------------------------------------------
// LINK RX: parse frame dari S3 (task sendiri biar FIFO UART gak overflow)
// ----------------------------------------------------------
static void queueCmd(uint8_t type, const uint8_t* p, uint16_t len) {
  Cmd c; memset(&c, 0, sizeof(c));
  c.type = type;
  if (len > sizeof(c.data) - 1) len = sizeof(c.data) - 1;
  if (len) memcpy(c.data, p, len);
  c.len = (uint8_t)len;
  xQueueSend(cmdQ, &c, 0);
}

static void doFlush() {
  flushReq = true;
  uint32_t t0 = millis();
  while (flushReq && millis() - t0 < 1000) vTaskDelay(pdMS_TO_TICKS(1));
}

static void handleFrame(uint8_t type, const uint8_t* p, uint16_t len) {
  switch (type) {
    case RP_BEGIN:
      curSeq = (len >= 1) ? p[0] : 0;
      decErr = false;
      doFlush();
      paused = false; gainTarget = 256; gainCur = 0;
      endedSent = false;
      haveTrack = true;
      break;
    case RP_DATA: {
      uint16_t off = 0; uint32_t t0 = millis();
      while (off < len) {
        uint32_t w = inRing.write(p + off, len - off);
        off += (uint16_t)w;
        if (w == 0) { if (millis() - t0 > 300) break; vTaskDelay(pdMS_TO_TICKS(1)); }
      }
      break;
    }
    case RP_FINISH:
      streamEnded = true;
      break;
    case RP_PAUSE:
      if (len >= 1) {
        if (p[0]) { paused = false; gainTarget = 256; }
        else      { paused = true;  gainTarget = 0; }
      }
      break;
    case RP_VOLUME:
      if (len >= 1) { pendingVol = p[0] > 127 ? 127 : p[0]; volDirty = true; }
      break;
    case RP_STOP:
      decErr = false;
      doFlush();
      haveTrack = false;
      break;
    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN: case RP_BTMODE:
      queueCmd(type, p, len);
      break;
    default: break;
  }
}

static void linkRxTask(void* param) {
  uint8_t tmp[128];
  for (;;) {
    int n = Serial2.available();
    if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
    if (n > (int)sizeof(tmp)) n = sizeof(tmp);
    n = Serial2.readBytes(tmp, n);
    for (int i = 0; i < n; i++)
      if (parser.feed(tmp[i])) handleFrame(parser.type, parser.payload, parser.len);
  }
}

// ----------------------------------------------------------
// STATUS ke S3 (13 byte):
//  [0] flags: b0 bt, b1 playing, b2 scanning, b3 paused, b4 streamEnded, b5 decodeDone
//  [1] volume  [2] txgain  [3..6] ruang kosong ring MP3 (u32 LE, = "kredit" S3)
//  [7] isi ring PCM (%)  [8..11] posisi putar ms (u32 LE)  [12] seq
//  [13..14] energi: rata2 |sampel kiri| (u16 LE, 0..32767)
//  [15..16] zero-crossing per 1024 sampel (u16 LE) = proxy "kecerahan"
//  [16] ... lihat di atas; flags b6 = decoder error (RAM)
//  [17..18] ukuran ring MP3 dalam KB (u16 LE) -> S3 menyesuaikan ambang kredit
//  [19] heap bebas RAM internal (KB, maks 255)  [20] blok kontigu terbesar (KB, maks 255)
//  (v1 S3 cukup baca 13 byte; v2 baca 17 byte utk RYNE; v3 baca 21 byte)
// ----------------------------------------------------------
static void sendStatus() {
  uint8_t b[21];
  uint8_t fl = 0;
  if (btConnected)               fl |= 1;
  if (haveTrack && !paused)      fl |= 2;
  if (isScanning)                fl |= 4;
  if (paused)                    fl |= 8;
  if (streamEnded.load())        fl |= 16;
  if (decodeDone)                fl |= 32;
  if (decErr)                    fl |= 64;
  b[0] = fl; b[1] = volumeNow; b[2] = (uint8_t)txGain;
  uint32_t fr = inRing.freeSpace();
  b[3] = fr & 0xFF; b[4] = (fr >> 8) & 0xFF; b[5] = (fr >> 16) & 0xFF; b[6] = (fr >> 24) & 0xFF;
  b[7] = pcmRing.size ? (uint8_t)((uint64_t)pcmRing.avail() * 100 / pcmRing.size) : 0;
  uint32_t ms = (uint32_t)(((uint64_t)bytesPlayedTotal * 1000ULL) / BYTES_PER_SEC);
  b[8] = ms & 0xFF; b[9] = (ms >> 8) & 0xFF; b[10] = (ms >> 16) & 0xFF; b[11] = (ms >> 24) & 0xFF;
  b[12] = curSeq;
  uint32_t sS = accSum, sC = accCnt, sZ = accZc;      // ambil lalu kurangi (aman thd penulis di callback BT)
  accSum -= sS; accCnt -= sC; accZc -= sZ;
  uint16_t en = sC ? (uint16_t)(sS / sC) : 0;
  uint16_t zr = sC ? (uint16_t)(((uint64_t)sZ * 1024ULL) / sC) : 0;
  b[13] = en & 0xFF; b[14] = en >> 8; b[15] = zr & 0xFF; b[16] = zr >> 8;
  uint32_t ringKb = inRing.size / 1024; if (ringKb > 0xFFFF) ringKb = 0xFFFF;
  b[17] = ringKb & 0xFF; b[18] = (ringKb >> 8) & 0xFF;
  // getMaxAllocHeap() menyisir seluruh heap di bawah kunci heap -> JANGAN tiap 40 ms. Cukup 1x/dtk.
  static uint32_t hkC = 0, mkC = 0, hAt = 0;
  uint32_t nowH = millis();
  if (nowH - hAt >= 1000) { hAt = nowH; hkC = ESP.getFreeHeap() / 1024; mkC = ESP.getMaxAllocHeap() / 1024; }
  uint32_t hk = hkC, mk = mkC;
  b[19] = hk > 255 ? 255 : (uint8_t)hk; b[20] = mk > 255 ? 255 : (uint8_t)mk;
  rpSend(Serial2, RP_STATUS, b, sizeof(b));
}

// ----------------------------------------------------------
// SETUP / LOOP
// ----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== CAM BT Co-Processor (Ren Phone) ===");

  bool hasPSRAM = psramFound();
  {
    Preferences p; p.begin("btco", true);
    String bn = p.getString("btname", "");
    strncpy(savedBTName, bn.c_str(), sizeof(savedBTName) - 1);
    txGain = constrain(p.getInt("txgain", TX_GAIN_DEFAULT), 0, TX_GAIN_LEVELS - 1);
    p.end();
  }

  bool ok = pcmRing.init(hasPSRAM ? PCM_RING_PSRAM : PCM_RING_FALLBACK, hasPSRAM);
  uint32_t inSz = IN_RING_FALLBACK;
  if (hasPSRAM) {
    inSz = IN_RING_PSRAM;
    uint32_t freePs = ESP.getFreePsram();
    if (freePs > 96 * 1024 && inSz > freePs - 96 * 1024) inSz = freePs - 96 * 1024;
  }
  ok = ok && inRing.init(inSz, hasPSRAM);
  if (!ok) {
    Serial.println("[FATAL] alokasi ring gagal (PSRAM aktif?)");
    while (1) delay(1000);
  }
  prefillBytes = PREFILL_BYTES;
  if (prefillBytes > pcmRing.size / 2) prefillBytes = pcmRing.size / 2;
  Serial.printf("[RING] MP3 in=%u KB, PCM=%u KB, PSRAM=%s\n",
                (unsigned)(inRing.size / 1024), (unsigned)(pcmRing.size / 1024), hasPSRAM ? "ya" : "TIDAK");

  cmdQ = xQueueCreate(8, sizeof(Cmd));
  keyQ = xQueueCreate(12, sizeof(KeyEvt));

  Serial2.setRxBufferSize(8192);
  Serial2.begin(RP_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);

  xTaskCreatePinnedToCore(decodeTask, "decode", DECODE_TASK_STACK, nullptr, 3, &decodeTaskH, 0);
  xTaskCreatePinnedToCore(linkRxTask, "linkrx", 4096, nullptr, 4, &linkTaskH, 1);

  a2dp.set_on_connection_state_changed(onConnectionChanged, nullptr);
  a2dp.set_volume(volumeNow);
  a2dp.set_data_callback_in_frames(getSoundData);
  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);
  delay(300);
  applyTxGain(txGain);
  avrcpStackInit();

  // HELLO v2: [versi][alasan reset]. S3 menampilkan di layar kalau CAM restart krn crash/brownout/watchdog
  uint8_t hello[2] = { 1, (uint8_t)esp_reset_reason() };
  Serial.printf("[BOOT] alasan reset=%d (1 nyala, 3 sw, 4 panic, 5/6/7 watchdog, 9 brownout)\n", (int)hello[1]);
  rpSend(Serial2, RP_HELLO, hello, 2);
}

void loop() {
  static uint32_t lastStatus = 0;

  // perintah dari link (butuh konteks loop: BT stack / NVS)
  Cmd c;
  while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
    if (c.type == RP_SCAN)         startScan();
    else if (c.type == RP_CONNECT) connectTo((const char*)c.data);   // data sudah NUL-terminated
    else if (c.type == RP_TXGAIN && c.len >= 1) { applyTxGain(c.data[0]); prefsDirty = true; }
    else if (c.type == RP_BTMODE && c.len >= 1) btSetEnabled(c.data[0] != 0);
  }

  // AVRCP: pasang filter setelah TWS tersambung (jeda 0,5 dtk, seperti proyek referensi)
  if (avrcpNeedInit && btConnected && millis() - avrcpInitAt >= 500) {
    avrcpNeedInit = false;
    avrcpSetupFilter();
  }
  // tombol earbuds -> S3. Kirim UART HANYA dari sini (satu pengirim = frame tidak saling menyela)
  {
    static uint32_t lastKeyMs = 0; static uint8_t lastKey = 0;
    KeyEvt ke;
    while (xQueueReceive(keyQ, &ke, 0) == pdTRUE) {
      uint32_t tk = millis();
      if (ke.code <= KEY_PREV && ke.code == lastKey && tk - lastKeyMs < 250) continue;  // buang pantulan tombol
      lastKey = ke.code; lastKeyMs = tk;
      uint8_t kb[2] = { ke.code, ke.arg };
      rpSend(Serial2, RP_KEY, kb, 2);
      Serial.printf("[AVRCP] key=%u arg=%u\n", (unsigned)ke.code, (unsigned)ke.arg);
    }
  }

  if (volDirty) { volDirty = false; volumeNow = pendingVol; a2dp.set_volume(volumeNow); }

  if (scanDone) { scanDone = false; sendScanResults(); }

  if (trackEnded && !endedSent && !decErr) {
    endedSent = true;
    uint8_t sq = curSeq;
    rpSend(Serial2, RP_ENDED, &sq, 1);
  }

  uint32_t now = millis();
  if (now - lastStatus >= STATUS_INTERVAL_MS) { lastStatus = now; sendStatus(); }

  {   // diagnosa memori + link (lihat Serial Monitor CAM, 115200). Stack 'free' kecil (<1KB) = stack hampir jebol.
    static uint32_t lastDiag = 0;
    if (now - lastDiag >= DIAG_INTERVAL_MS) {
      lastDiag = now;
      Serial.printf("[DIAG] heap=%uK min=%uK blokMax=%uK | stack bebas dec=%u link=%u loop=%u | ringMP3=%u%% PCM=%u%% | rxErr=%u decErr=%d bt=%d\n",
        (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024),
        decodeTaskH ? (unsigned)uxTaskGetStackHighWaterMark(decodeTaskH) : 0u,
        linkTaskH   ? (unsigned)uxTaskGetStackHighWaterMark(linkTaskH)   : 0u,
        (unsigned)uxTaskGetStackHighWaterMark(nullptr),
        inRing.size  ? (unsigned)((uint64_t)inRing.avail()  * 100 / inRing.size)  : 0u,
        pcmRing.size ? (unsigned)((uint64_t)pcmRing.avail() * 100 / pcmRing.size) : 0u,
        (unsigned)parser.errors, (int)decErr, (int)btConnected);
    }
  }

  if (btEnabled && !btConnected && btDisconnectedAt > 0 && now - btDisconnectedAt > BT_RECONNECT_MS) {
    btDisconnectedAt = now;
    a2dp.reconnect();
  }

  if (prefsDirty && !(haveTrack && !paused)) {   // jangan nulis flash selagi audio jalan
    prefsDirty = false;
    savePrefs();
  }

  delay(2);
}
