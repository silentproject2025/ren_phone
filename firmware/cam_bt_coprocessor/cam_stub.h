#pragma once
// Pengganti cam_module.h untuk varian RP_BT_ONLY (ESP32 WROOM / DevKit biasa).
// Tanpa kamera, tanpa esp_camera, tanpa PSRAM. Ring buffer otomatis pakai fallback RAM internal.
// Semua fungsi no-op supaya .ino utama tidak perlu diubah di banyak tempat.
#include <Arduino.h>
#include "rplink.h"

static inline void camModNoteRx() {}
static bool camModHandle(uint8_t type, const uint8_t* p, uint16_t len) {
  (void)type; (void)p; (void)len;
  return false;   // semua perintah RP_CAM_* diabaikan (board ini memang tak punya kamera)
}
static inline void camModTick() {}
static inline void camModShutdown() {}
