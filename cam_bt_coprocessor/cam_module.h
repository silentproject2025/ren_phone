#pragma once
// ============================================================
//  cam_module.h -- kamera OV2640 di ESP32-CAM sebagai bagian ko-prosesor Ren Phone
//
//  Alur:  S3 --RP_CAM_OPEN--> CAM menyalakan kamera (JPEG, buffer di PSRAM, GRAB_LATEST)
//         CAM menyiarkan frame preview (RP_CAM_FRAME) selama masih punya "kredit";
//         S3 membalas RP_CAM_ACK tiap kali selesai mengambil satu frame (maks 2 frame di jalan).
//         RP_CAM_SHOT = foto 1x di resolusi tinggi (ganti resolusi -> buang beberapa frame
//         agar AE/AGC stabil -> kirim -> kembali ke resolusi preview).
//
//  Satu task (camTask, core 1) memegang SEMUA akses ke driver kamera. Task link (linkRxTask)
//  hanya memasukkan perintah ke antrean, jadi tidak pernah memblokir UART.
//
//  Pin kamera = AI-Thinker ESP32-CAM. Pin 13/14 (link UART ke S3) tidak bentrok dengan kamera.
//  LED flash GPIO4 diredupkan lewat LEDC timer 2 / channel 2 (kamera memakai timer 0 / channel 0).
// ============================================================
#include <Arduino.h>
#include <atomic>
#include "esp_camera.h"
#include "driver/ledc.h"
#include "rplink.h"
#include "cam_tx.h"

// Kode resolusi di protokol (RPR_*) dipetakan ke enum library. Kalau versi library mengubah
// nama, build GAGAL keras di sini -- lebih baik daripada resolusi salah diam-diam.
static const framesize_t CAM_FS_MAP[10] = {
  FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_CIF, FRAMESIZE_HVGA, FRAMESIZE_VGA,
  FRAMESIZE_SVGA,  FRAMESIZE_XGA,  FRAMESIZE_HD,  FRAMESIZE_SXGA, FRAMESIZE_UXGA
};
static inline framesize_t camFs(uint8_t code) { return CAM_FS_MAP[code > 9 ? 1 : code]; }

#define CAM_PIN_PWDN   32
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27
#define CAM_PIN_Y9     35
#define CAM_PIN_Y8     34
#define CAM_PIN_Y7     39
#define CAM_PIN_Y6     36
#define CAM_PIN_Y5     21
#define CAM_PIN_Y4     19
#define CAM_PIN_Y3     18
#define CAM_PIN_Y2      5
#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22
#define CAM_PIN_FLASH   4

#define CAM_MIN_BLOCK        (20 * 1024)   // blok kontigu RAM internal minimal sebelum kamera dinyalakan
#define CAM_MAX_PREVIEW      (96 * 1024)   // frame preview lebih besar dari ini dibuang (slot S3 = 96 KB)
#define CAM_TASK_STACK       6144
#define CAM_TORCH_MAX_MS     120000        // senter otomatis mati (cegah LED/ transistor kepanasan)
#define CAM_IDLE_CLOSE_MS    8000          // S3 hilang selama ini -> kamera dimatikan sendiri
#define CAM_BAUD_FALLBACK_MS 3000          // tak ada frame valid dari S3 di baud tinggi -> kembali ke RP_BAUD
#define CAM_STAT_MS          500

struct CamCmd { uint8_t type; uint8_t len; uint8_t d[8]; };

static QueueHandle_t       camQ = nullptr;
static TaskHandle_t        camTaskH = nullptr;
static volatile bool       camOpen = false, camStream = false, camShotBusy = false, camCloseReq = false;
static std::atomic<int>    camCredit{0};
static volatile uint32_t   camLastAckMs = 0;
static volatile uint32_t   camLastRxMs = 0;
static uint32_t            camBaudNow = RP_BAUD;
static int8_t              camCfg[RPC_COUNT];
static bool                camCfgSet[RPC_COUNT];
static uint8_t             camPrvRes = RPR_QVGA, camPrvQ = 14, camXclk = 20;
static volatile uint8_t    camErr = 0, camErrCode = 0;   // camErr: 1 tanpa PSRAM, 2 RAM kurang, 3 init gagal, 5 kamera belum nyala, 6 foto gagal
static uint16_t            camPid = 0;
static uint8_t             camFlashLvl = 0;
static uint32_t            camFlashOnAt = 0;
static bool                camFlashInited = false;
static volatile uint32_t   camFps10 = 0;
static uint8_t             camSeq = 0;
static uint32_t            camStatAt = 0;
static uint32_t            camHeapKb = 0, camBlkKb = 0, camHeapAt = 0;

// ---------------- LED flash (PWM 20 kHz, 8 bit) ----------------
static void camFlashInit() {
  if (camFlashInited) return;
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE; t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_2; t.freq_hz = 20000; t.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&t);
  ledc_channel_config_t c = {};
  c.gpio_num = CAM_PIN_FLASH; c.speed_mode = LEDC_LOW_SPEED_MODE; c.channel = LEDC_CHANNEL_2;
  c.intr_type = LEDC_INTR_DISABLE; c.timer_sel = LEDC_TIMER_2; c.duty = 0; c.hpoint = 0;
  ledc_channel_config(&c);
  camFlashInited = true;
}
static void camFlashSet(uint8_t lvl) {
  camFlashInit();
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, lvl);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
  camFlashLvl = lvl;
  camFlashOnAt = lvl ? millis() : 0;
}

static int8_t camCfgOr(uint8_t id, int8_t def) { return (id < RPC_COUNT && camCfgSet[id]) ? camCfg[id] : def; }

// ---------------- STATUS ke S3 (12 byte) ----------------
//  [0] flags: b0 kamera nyala, b1 streaming, b2 baud tinggi, b3 flash nyala, b4 sedang foto
//  [1] kode error  [2] kode esp_err (byte rendah)  [3..4] PID sensor (u16 LE; 0x26 = OV2640)
//  [5] heap bebas KB  [6] blok terbesar KB  [7..8] fps x10 (u16 LE)  [9] res preview  [10] kualitas preview  [11] cadangan
static void camSendStat() {
  uint32_t now = millis();
  if (now - camHeapAt >= 1000) { camHeapAt = now; camHeapKb = ESP.getFreeHeap() / 1024; camBlkKb = ESP.getMaxAllocHeap() / 1024; }
  uint8_t b[12];
  b[0] = (camOpen ? 1 : 0) | (camStream ? 2 : 0) | (camBaudNow != RP_BAUD ? 4 : 0) | (camFlashLvl ? 8 : 0) | (camShotBusy ? 16 : 0);
  b[1] = camErr; b[2] = camErrCode;
  b[3] = camPid & 0xFF; b[4] = camPid >> 8;
  b[5] = camHeapKb > 255 ? 255 : (uint8_t)camHeapKb;
  b[6] = camBlkKb > 255 ? 255 : (uint8_t)camBlkKb;
  uint32_t f = camFps10; b[7] = f & 0xFF; b[8] = (f >> 8) & 0xFF;
  b[9] = camPrvRes; b[10] = camPrvQ; b[11] = 0;
  rpSend(Serial2, RP_CAM_STAT, b, sizeof(b));
  camStatAt = now;
}

// ---------------- buka / tutup kamera ----------------
static bool camDoOpen() {
  if (camOpen) return true;
  camErr = 0; camErrCode = 0;
  if (!psramFound()) { camErr = 1; return false; }
  if (ESP.getMaxAllocHeap() < CAM_MIN_BLOCK) { camErr = 2; return false; }
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_Y2; c.pin_d1 = CAM_PIN_Y3; c.pin_d2 = CAM_PIN_Y4; c.pin_d3 = CAM_PIN_Y5;
  c.pin_d4 = CAM_PIN_Y6; c.pin_d5 = CAM_PIN_Y7; c.pin_d6 = CAM_PIN_Y8; c.pin_d7 = CAM_PIN_Y9;
  c.pin_xclk = CAM_PIN_XCLK; c.pin_pclk = CAM_PIN_PCLK; c.pin_vsync = CAM_PIN_VSYNC; c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD; c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN; c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = (int)camXclk * 1000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_UXGA;           // buffer dialokasikan utk ukuran maksimum -> foto UXGA tetap muat
  c.jpeg_quality = camPrvQ;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;        // selalu frame terbaru, bukan antrean basi
  uint32_t h0 = ESP.getFreeHeap();
  esp_err_t e = esp_camera_init(&c);
  if (e != ESP_OK) { camErr = 3; camErrCode = (uint8_t)(e & 0xFF); Serial.printf("[CAM] init gagal 0x%x\n", (unsigned)e); return false; }
  sensor_t* s = esp_camera_sensor_get();
  if (!s) { camErr = 3; camErrCode = 0xFE; esp_camera_deinit(); return false; }
  camPid = s->id.PID;
  s->set_framesize(s, camFs(camPrvRes));
  s->set_quality(s, camPrvQ);
  camOpen = true;
  Serial.printf("[CAM] kamera nyala PID=0x%x xclk=%uMHz heap %u->%u blokMax=%u\n", (unsigned)camPid, (unsigned)camXclk,
                (unsigned)h0, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return true;
}

static void camDoClose() {
  camStream = false;
  if (camFlashLvl) camFlashSet(0);
  if (camOpen) { esp_camera_deinit(); camOpen = false; }
  camFps10 = 0;
  Serial.printf("[CAM] kamera mati. heap=%u blokMax=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

static void camApplySet(uint8_t id, int8_t v) {
  if (id >= RPC_COUNT) return;
  camCfg[id] = v; camCfgSet[id] = true;
  if (id == RPC_PRVRES) camPrvRes = (uint8_t)v;
  if (id == RPC_PRVQ) camPrvQ = (uint8_t)(v < 4 ? 4 : v);
  if (!camOpen) return;
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  switch (id) {
    case RPC_BRIGHT:   s->set_brightness(s, v); break;
    case RPC_CONTRAST: s->set_contrast(s, v); break;
    case RPC_SATUR:    s->set_saturation(s, v); break;
    case RPC_SHARP:    s->set_sharpness(s, v); break;
    case RPC_WBMODE:   s->set_whitebal(s, 1); s->set_awb_gain(s, 1); s->set_wb_mode(s, v); break;
    case RPC_EFFECT:   s->set_special_effect(s, v); break;
    case RPC_HMIRROR:  s->set_hmirror(s, v ? 1 : 0); break;
    case RPC_VFLIP:    s->set_vflip(s, v ? 1 : 0); break;
    case RPC_NIGHT:
      s->set_aec2(s, v ? 1 : 0);
      s->set_gainceiling(s, v ? GAINCEILING_128X : GAINCEILING_8X);
      s->set_ae_level(s, v ? 1 : camCfgOr(RPC_AELEVEL, 0));
      break;
    case RPC_LENC:     s->set_lenc(s, v ? 1 : 0); break;
    case RPC_AELEVEL:  s->set_ae_level(s, v); break;
    case RPC_PRVRES:   if (!camShotBusy) s->set_framesize(s, camFs((uint8_t)v)); break;
    case RPC_PRVQ:     if (!camShotBusy) s->set_quality(s, camPrvQ); break;
    case RPC_COLORBAR: s->set_colorbar(s, v ? 1 : 0); break;
    default: break;   // RPC_FPSCAP dibaca langsung dari camCfg
  }
}

static bool camAbortFn() { return !camOpen || camCloseReq; }

static void camDoShot(uint8_t res, uint8_t q, uint8_t flash, uint8_t flags) {
  (void)flags;
  if (!camOpen) { camErr = 5; camSendStat(); return; }
  sensor_t* s = esp_camera_sensor_get();
  if (!s) { camErr = 6; camSendStat(); return; }
  bool wasStream = camStream;
  uint8_t prevFlash = camFlashLvl;
  camStream = false; camShotBusy = true; camErr = 0;
  camSendStat();
  s->set_framesize(s, camFs(res));
  s->set_quality(s, q);
  if (flash) camFlashSet(flash);
  uint32_t settle = flash ? 400 : 260;          // AE/AGC butuh beberapa frame setelah ganti resolusi / lampu
  uint32_t t0 = millis(); int dropped = 0;
  while ((millis() - t0 < settle || dropped < 2) && millis() - t0 < 2500) {
    camera_fb_t* f = esp_camera_fb_get();
    if (f) { esp_camera_fb_return(f); dropped++; } else vTaskDelay(pdMS_TO_TICKS(2));
  }
  bool ok = false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    if (fb->format == PIXFORMAT_JPEG && fb->len > 0) ok = camTxFrame(Serial2, ++camSeq, fb->buf, (uint32_t)fb->len, true, camAbortFn);
    esp_camera_fb_return(fb);
  }
  camFlashSet(flash ? prevFlash : camFlashLvl);   // kembalikan senter ke keadaan semula
  s->set_framesize(s, camFs(camPrvRes));
  s->set_quality(s, camPrvQ);
  camShotBusy = false;
  if (!ok) camErr = 6;
  if (wasStream) { camCredit = 2; camLastAckMs = millis(); camStream = true; }
  camSendStat();
}

static void camHandleCmd(const CamCmd& c) {
  switch (c.type) {
    case RP_CAM_OPEN: {
      if (c.len >= 3) { camXclk = c.d[0] < 8 ? 8 : (c.d[0] > 20 ? 20 : c.d[0]); camPrvRes = c.d[1] > 9 ? 1 : c.d[1]; camPrvQ = c.d[2] < 4 ? 4 : c.d[2]; }
      bool wantStream = c.len >= 4 && (c.d[3] & 1);
      camCloseReq = false;
      if (camOpen) {                               // sudah nyala: cukup terapkan ulang
        camApplySet(RPC_PRVRES, (int8_t)camPrvRes); camApplySet(RPC_PRVQ, (int8_t)camPrvQ);
      } else if (camDoOpen()) {
        for (int i = 0; i < RPC_COUNT; i++) if (camCfgSet[i]) camApplySet((uint8_t)i, camCfg[i]);
      }
      if (camOpen && wantStream) { camCredit = 2; camLastAckMs = millis(); camStream = true; }
      camSendStat();
      break;
    }
    case RP_CAM_CLOSE: camCloseReq = true; camDoClose(); camCloseReq = false; camSendStat(); break;
    case RP_CAM_SET:   if (c.len >= 2) camApplySet(c.d[0], (int8_t)c.d[1]); break;
    case RP_CAM_SHOT:  if (c.len >= 3) camDoShot(c.d[0] > 9 ? RPR_SVGA : c.d[0], c.d[1] < 4 ? 4 : c.d[1], c.d[2], c.len >= 4 ? c.d[3] : 0); break;
    case RP_CAM_FLASH: if (c.len >= 1) camFlashSet(c.d[0]); camSendStat(); break;
    case RP_CAM_BAUD: {
      if (c.len < 4) break;
      uint32_t b = (uint32_t)c.d[0] | ((uint32_t)c.d[1] << 8) | ((uint32_t)c.d[2] << 16) | ((uint32_t)c.d[3] << 24);
      if (b < 115200 || b > 4000000) b = RP_BAUD;
      uint8_t o[4] = { (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)(b >> 16), (uint8_t)(b >> 24) };
      rpSend(Serial2, RP_CAM_BAUDOK, o, 4);        // jawaban masih di baud lama
      Serial2.flush();
      vTaskDelay(pdMS_TO_TICKS(15));
      Serial2.updateBaudRate(b);
      camBaudNow = b;
      camLastRxMs = millis();
      Serial.printf("[CAM] link -> %u baud\n", (unsigned)b);
      break;
    }
    default: break;
  }
}

static void camTask(void*) {
  CamCmd cmd;
  uint32_t fpsT0 = millis(), fpsN = 0, nextFrameAt = 0;
  for (;;) {
    TickType_t wait = (camOpen && camStream) ? 0 : pdMS_TO_TICKS(20);
    while (xQueueReceive(camQ, &cmd, wait) == pdTRUE) { camHandleCmd(cmd); wait = 0; }

    if (camOpen && camStream) {
      if (camCredit.load() > 0) {
        int cap = camCfgOr(RPC_FPSCAP, 0);
        uint32_t nowMs = millis();
        if (cap > 0 && (int32_t)(nextFrameAt - nowMs) > 0) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        if (fb->format == PIXFORMAT_JPEG && fb->len > 0 && fb->len <= CAM_MAX_PREVIEW) {
          camTxFrame(Serial2, ++camSeq, fb->buf, (uint32_t)fb->len, false, camAbortFn);
          camCredit--; fpsN++;
          if (cap > 0) nextFrameAt = millis() + 1000 / cap;
        }
        esp_camera_fb_return(fb);
      } else {
        if (millis() - camLastAckMs > 1500) { camCredit = 1; camLastAckMs = millis(); }   // ACK hilang -> jangan macet
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }
    uint32_t now = millis();
    if (now - fpsT0 >= 1000) { camFps10 = (fpsN * 10000UL) / (now - fpsT0); fpsN = 0; fpsT0 = now; }
  }
}

// ---------------- dipanggil dari .ino utama ----------------
// task link: tiap frame valid dari S3
static inline void camModNoteRx() { camLastRxMs = millis(); }

// task link: frame yang bukan milik audio. Return true kalau frame kamera.
static bool camModHandle(uint8_t type, const uint8_t* p, uint16_t len) {
  switch (type) {
    case RP_CAM_ACK: {
      int c = camCredit.load();
      if (c < 3) camCredit = c + 1;
      camLastAckMs = millis();
      return true;
    }
    case RP_CAM_OPEN: case RP_CAM_CLOSE: case RP_CAM_SET: case RP_CAM_SHOT: case RP_CAM_FLASH: case RP_CAM_BAUD: {
      if (!camQ) camQ = xQueueCreate(10, sizeof(CamCmd));
      if (!camQ) return true;
      if (!camTaskH && type == RP_CAM_OPEN) {
        if (xTaskCreatePinnedToCore(camTask, "camtask", CAM_TASK_STACK, nullptr, 2, &camTaskH, 1) != pdPASS) {
          camTaskH = nullptr; camErr = 2; return true;
        }
      }
      if (!camTaskH) return true;
      CamCmd c; memset(&c, 0, sizeof(c));
      c.type = type; c.len = len > sizeof(c.d) ? sizeof(c.d) : (uint8_t)len;
      if (c.len) memcpy(c.d, p, c.len);
      xQueueSend(camQ, &c, 0);
      return true;
    }
    default: return false;
  }
}

// loop(): status berkala, fallback baud, senter otomatis mati, matikan kamera kalau S3 hilang
static void camModTick() {
  uint32_t now = millis();
  if (camBaudNow != RP_BAUD && now - camLastRxMs > CAM_BAUD_FALLBACK_MS) {
    Serial2.updateBaudRate(RP_BAUD);
    camBaudNow = RP_BAUD; camLastRxMs = now;
    Serial.println("[CAM] baud tinggi gagal -> kembali ke baud awal");
  }
  if (camFlashLvl && camFlashOnAt && !camShotBusy && now - camFlashOnAt > CAM_TORCH_MAX_MS) camFlashSet(0);
  if (camOpen && now - camLastRxMs > CAM_IDLE_CLOSE_MS && camQ) {
    CamCmd c; memset(&c, 0, sizeof(c)); c.type = RP_CAM_CLOSE;
    xQueueSend(camQ, &c, 0);
    camLastRxMs = now;
  }
  if ((camOpen || camErr) && now - camStatAt >= CAM_STAT_MS) camSendStat();
}

// sebelum deep sleep
static void camModShutdown() {
  if (camFlashLvl) camFlashSet(0);
  if (camOpen && camQ) {
    CamCmd c; memset(&c, 0, sizeof(c)); c.type = RP_CAM_CLOSE;
    xQueueSend(camQ, &c, 0);
    uint32_t t0 = millis();
    while (camOpen && millis() - t0 < 1500) delay(5);
  }
}
