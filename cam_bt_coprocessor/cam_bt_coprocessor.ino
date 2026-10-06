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
 *  Kartu SD di CAM TIDAK dipakai (jangan di-mount: pin 12-15 dipakai link).
 *  UART0 (GPIO1/3) bebas buat flash & log debug.
 *
 *  Arduino IDE / arduino-cli:
 *    Board : ESP32 Wrover Module (atau AI Thinker ESP32-CAM)
 *    PSRAM : Enabled
 *    Library: AudioTools, ESP32-A2DP (pschatzmann)
 *
 *  Batasan v1: MP3 Layer III 44.1 kHz stereo saja (WAV/EQ/crossfade/AVRCP
 *  sengaja belum ikut).
 * ============================================================
 */
#include <Arduino.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
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
static volatile bool     btCbRunning  = false;
static volatile int32_t  trackEndSilence = 0;
static volatile uint32_t bytesPlayedTotal = 0;
static volatile int      gainCur = 0, gainTarget = 256;  // 0..256, ramp per sample
static volatile uint8_t  curSeq = 0;
static bool              endedSent = false;

static volatile bool     btConnected = false;
static uint32_t          btDisconnectedAt = 0;
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

  // ramp gain per sample (jeda/lanjut tanpa "tek", ~6 ms)
  int g = gainCur, gt = gainTarget;
  for (int32_t i = 0; i < frameCount; i++) {
    if (g < gt) g++; else if (g > gt) g--;
    if (g != 256) {
      fb[i].channel1 = (int16_t)(((int32_t)fb[i].channel1 * g) >> 8);
      fb[i].channel2 = (int16_t)(((int32_t)fb[i].channel2 * g) >> 8);
    }
  }
  gainCur = g;

  btCbRunning = false;
  return frameCount;
}

void onConnectionChanged(esp_a2d_connection_state_t state, void* obj) {
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    btConnected = true; btDisconnectedAt = 0;
    applyTxGain(txGain);
    a2dp.set_volume(volumeNow);
    Serial.println("[BT] Terhubung");
  } else {
    btConnected = false;
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

static void connectTo(const char* name) {
  strncpy(savedBTName, name, sizeof(savedBTName) - 1);
  savedBTName[sizeof(savedBTName) - 1] = 0;
  prefsDirty = true;
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
  auto rebuild = [&]() {
    if (enc)   { enc->end(); delete enc; enc = nullptr; }
    if (codec) { delete codec; codec = nullptr; }
    codec = new MP3DecoderHelix();
    enc   = new EncodedAudioStream(&pcmSink, codec);
    enc->begin();
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
      flushReq = false;
      continue;
    }
    if (decodeDone) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

    uint32_t av = inRing.avail();
    if (av == 0) {
      // urutan penting: baca streamEnded DULU baru cek ring lagi
      if (streamEnded.load() && inRing.avail() == 0) {
        enc->end();                      // bilas sisa frame di decoder
        decodeDone = true;
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }
    uint32_t n = inRing.read(buf, av > IN_CHUNK ? IN_CHUNK : av);
    enc->write(buf, n);
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
      doFlush();
      haveTrack = false;
      break;
    case RP_SCAN: case RP_CONNECT: case RP_TXGAIN:
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
// ----------------------------------------------------------
static void sendStatus() {
  uint8_t b[13];
  uint8_t fl = 0;
  if (btConnected)               fl |= 1;
  if (haveTrack && !paused)      fl |= 2;
  if (isScanning)                fl |= 4;
  if (paused)                    fl |= 8;
  if (streamEnded.load())        fl |= 16;
  if (decodeDone)                fl |= 32;
  b[0] = fl; b[1] = volumeNow; b[2] = (uint8_t)txGain;
  uint32_t fr = inRing.freeSpace();
  b[3] = fr & 0xFF; b[4] = (fr >> 8) & 0xFF; b[5] = (fr >> 16) & 0xFF; b[6] = (fr >> 24) & 0xFF;
  b[7] = pcmRing.size ? (uint8_t)((uint64_t)pcmRing.avail() * 100 / pcmRing.size) : 0;
  uint32_t ms = (uint32_t)(((uint64_t)bytesPlayedTotal * 1000ULL) / BYTES_PER_SEC);
  b[8] = ms & 0xFF; b[9] = (ms >> 8) & 0xFF; b[10] = (ms >> 16) & 0xFF; b[11] = (ms >> 24) & 0xFF;
  b[12] = curSeq;
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

  Serial2.setRxBufferSize(8192);
  Serial2.begin(RP_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);

  xTaskCreatePinnedToCore(decodeTask, "decode", DECODE_TASK_STACK, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(linkRxTask, "linkrx", 4096, nullptr, 4, nullptr, 1);

  a2dp.set_on_connection_state_changed(onConnectionChanged, nullptr);
  a2dp.set_volume(volumeNow);
  a2dp.set_data_callback_in_frames(getSoundData);
  a2dp.start(savedBTName[0] ? savedBTName : BT_DEFAULT_NAME);
  delay(300);
  applyTxGain(txGain);

  uint8_t ver = 1;
  rpSend(Serial2, RP_HELLO, &ver, 1);
}

void loop() {
  static uint32_t lastStatus = 0;

  // perintah dari link (butuh konteks loop: BT stack / NVS)
  Cmd c;
  while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
    if (c.type == RP_SCAN)         startScan();
    else if (c.type == RP_CONNECT) connectTo((const char*)c.data);   // data sudah NUL-terminated
    else if (c.type == RP_TXGAIN && c.len >= 1) { applyTxGain(c.data[0]); prefsDirty = true; }
  }

  if (volDirty) { volDirty = false; volumeNow = pendingVol; a2dp.set_volume(volumeNow); }

  if (scanDone) { scanDone = false; sendScanResults(); }

  if (trackEnded && !endedSent) {
    endedSent = true;
    uint8_t sq = curSeq;
    rpSend(Serial2, RP_ENDED, &sq, 1);
  }

  uint32_t now = millis();
  if (now - lastStatus >= STATUS_INTERVAL_MS) { lastStatus = now; sendStatus(); }

  if (!btConnected && btDisconnectedAt > 0 && now - btDisconnectedAt > BT_RECONNECT_MS) {
    btDisconnectedAt = now;
    a2dp.reconnect();
  }

  if (prefsDirty && !(haveTrack && !paused)) {   // jangan nulis flash selagi audio jalan
    prefsDirty = false;
    savePrefs();
  }

  delay(2);
}
