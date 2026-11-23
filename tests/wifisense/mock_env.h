#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
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
struct WiFiMock { int st = WL_CONNECTED; int status() { return st; } void setSleep(bool) {} IPAddress gatewayIP() { return IPAddress(); } } WiFi;
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
