#pragma once
// stub NVS untuk tes PC: penyimpanan di memori
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>
typedef int nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READWRITE 1
static std::map<std::string, std::vector<uint8_t>> g_nvs;
static inline esp_err_t nvs_open(const char*, int, nvs_handle_t* h) { *h = 1; return ESP_OK; }
static inline void nvs_close(nvs_handle_t) {}
static inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
static inline esp_err_t nvs_set_blob(nvs_handle_t, const char* k, const void* v, size_t n) { g_nvs[k].assign((const uint8_t*)v, (const uint8_t*)v + n); return ESP_OK; }
static inline esp_err_t nvs_get_blob(nvs_handle_t, const char* k, void* v, size_t* n) { auto it = g_nvs.find(k); if (it == g_nvs.end()) return 1; if (v) memcpy(v, it->second.data(), it->second.size() < *n ? it->second.size() : *n); *n = it->second.size(); return ESP_OK; }
static inline esp_err_t nvs_set_u8(nvs_handle_t, const char* k, uint8_t v) { g_nvs[k] = {v}; return ESP_OK; }
static inline esp_err_t nvs_get_u8(nvs_handle_t, const char* k, uint8_t* v) { auto it = g_nvs.find(k); if (it == g_nvs.end()) return 1; *v = it->second[0]; return ESP_OK; }
static inline esp_err_t nvs_set_u32(nvs_handle_t, const char* k, uint32_t v) { g_nvs[k].assign((uint8_t*)&v, (uint8_t*)&v + 4); return ESP_OK; }
static inline esp_err_t nvs_get_u32(nvs_handle_t, const char* k, uint32_t* v) { auto it = g_nvs.find(k); if (it == g_nvs.end()) return 1; memcpy(v, it->second.data(), 4); return ESP_OK; }
static inline esp_err_t nvs_erase_key(nvs_handle_t, const char* k) { g_nvs.erase(k); return ESP_OK; }
