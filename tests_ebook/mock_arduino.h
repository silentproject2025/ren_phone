// mock_arduino.h -- API palsu minimum agar ebook_renphone.ino bisa diperiksa sintaks/tipe-nya di komputer
// (pola sama dengan tests_kamera/mock_s3.h). BUKAN untuk dijalankan.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
static inline void* heap_caps_realloc(void* p, size_t n, int) { return realloc(p, n); }
static inline void yield() {}
static inline unsigned long millis() { return 0; }
namespace lgfx { struct IFont { int dummy; };
  namespace fonts { extern const IFont Font0, FreeSans9pt7b, FreeSerif9pt7b, FreeMono9pt7b; } }
struct LGFX_Sprite {
  void fillRect(int,int,int,int,uint16_t); void fillRoundRect(int,int,int,int,int,uint16_t); void fillCircle(int,int,int,uint16_t);
  void drawFastHLine(int,int,int,uint16_t); void setFont(const lgfx::IFont*); void setTextSize(float); void setTextColor(uint16_t);
  void setCursor(int,int); void print(const char*); int textWidth(const char*); int fontHeight(); void setTextWrap(bool);
  void setClipRect(int,int,int,int); void clearClipRect();
};
struct Theme { uint16_t bg, surface, surface2, accent, accent2, text, subtext, divider, good, danger; };
Theme& T();
extern int SCR_W, SCR_H; extern bool needRedraw, sdReady;
#define BACK_H 24
#define FILE_READ "r"
#define FILE_WRITE "w"
struct File { operator bool() const; bool isDirectory(); File openNextFile(); const char* name(); size_t size(); void close();
  bool seek(uint32_t); size_t read(uint8_t*, size_t); size_t write(const uint8_t*, size_t); };
struct FSMock { File open(const char*, const char* = "r"); bool exists(const char*); bool mkdir(const char*); bool remove(const char*); bool rename(const char*, const char*); };
extern FSMock SD_MMC;
void showToast(const char* msg, int ms = 1400); void drawToast(LGFX_Sprite&); void drawBack(LGFX_Sprite&);
bool isBack(int,int); void navBack(); void vibTap(); void iosBackdrop(LGFX_Sprite&); void drawStatusBar(LGFX_Sprite&);
void iosTitle(LGFX_Sprite&,int,int,const char*); uint16_t blend565(uint16_t,uint16_t,uint8_t);
