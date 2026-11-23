#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
using std::max; using std::min;
#define WSENSE_FORCE 1
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
static unsigned long g_ms = 100000;
inline unsigned long millis() { return g_ms; }
#define WL_CONNECTED 3
struct IPAddress { uint8_t o[4]={192,168,1,1}; uint8_t operator[](int i) const { return o[i]; } };
static uint8_t g_bssid[6] = {0x24,0x0a,0xc4,0x11,0x22,0x33};   // WSENSE_V2
struct WiFiMock { int st = WL_CONNECTED; int status() { return st; } void setSleep(bool) {} IPAddress gatewayIP() { return IPAddress(); } uint8_t* BSSID() { return g_bssid; } } WiFi;
// WSENSE_V2: Preferences tiruan (memori)
struct Preferences {
  static std::map<std::string, std::vector<uint8_t>>& store() { static std::map<std::string, std::vector<uint8_t>> m; return m; }
  std::string ns;
  bool begin(const char* n, bool = false) { ns = n; return true; }
  void end() {}
  size_t putBytes(const char* k, const void* v, size_t n) { store()[ns + "/" + k] = std::vector<uint8_t>((const uint8_t*)v, (const uint8_t*)v + n); return n; }
  size_t getBytes(const char* k, void* b, size_t mx) { auto it = store().find(ns + "/" + k); if (it == store().end() || it->second.size() > mx) return 0; memcpy(b, it->second.data(), it->second.size()); return it->second.size(); }
};
int SCR_W = 320, SCR_H = 240; const int BACK_H = 24;
bool needRedraw = false;
struct Theme { uint16_t bg=0, surface=1, surface2=2, accent=3, accent2=4, text=5, subtext=6, divider=7, good=8, danger=9; };
Theme& T() { static Theme t; return t; }
struct LGFX_Sprite {
  void setTextSize(int) {} void setTextColor(uint16_t) {} void setCursor(int,int) {}
  void print(const char*) {} int textWidth(const char* s) { return (int)strlen(s) * 6; }
  void drawFastHLine(int,int,int,uint16_t) {} void drawLine(int,int,int,int,uint16_t) {}
  void fillRect(int,int,int,int,uint16_t) {}
};
uint16_t blend565(uint16_t a, uint16_t, uint8_t) { return a; }
void nyxAaRR(LGFX_Sprite&,int,int,int,int,int,uint16_t) {}
void nyxAaCircle(LGFX_Sprite&,int,int,int,uint16_t) {}
void iosBackdrop(LGFX_Sprite&) {} void drawStatusBar(LGFX_Sprite&) {} void iosTitle(LGFX_Sprite&,int,int,const char*) {}
void drawGlassPanel(LGFX_Sprite&,int,int,int,int,int,uint16_t,uint8_t) {}
void drawBack(LGFX_Sprite&) {} void drawToast(LGFX_Sprite&) {}
bool isBack(int, int y) { return y > 200; } void navBack() {}
