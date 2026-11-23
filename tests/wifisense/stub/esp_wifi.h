#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
struct wifi_pkt_rx_ctrl_t { unsigned sig_mode; unsigned cwb; int rssi; };   // WSENSE_V2
struct wifi_csi_info_t { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t mac[6]; int8_t* buf; uint16_t len; };
struct wifi_csi_config_t { bool lltf_en, htltf_en, stbc_htltf2_en, ltf_merge_en, channel_filter_en, manu_scale, dump_ack_en; uint8_t shift; };
typedef void (*wifi_csi_cb_t)(void*, wifi_csi_info_t*);
inline esp_err_t esp_wifi_set_csi_config(const wifi_csi_config_t*) { return ESP_OK; }
inline esp_err_t esp_wifi_set_csi_rx_cb(wifi_csi_cb_t, void*) { return ESP_OK; }
inline esp_err_t esp_wifi_set_csi(bool) { return ESP_OK; }
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
