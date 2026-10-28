#pragma once
#include <Arduino.h>
#include <string>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <algorithm>
using std::max; using std::min;
typedef int portMUX_TYPE_t; struct portMUX_TYPE { int x; }; 
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define ARDUINO 1
unsigned long millis(); void delay(unsigned long);
struct String { std::string s; String(){} String(const char* c):s(c){} const char* c_str() const {return s.c_str();} };
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void* heap_caps_malloc(size_t, int); void heap_caps_free(void*);
struct HW2 : HardwareSerial { int available(); size_t readBytes(uint8_t*, size_t); void updateBaudRate(unsigned long); void flush(); };
extern HW2 Serial1;
struct Preferences { bool begin(const char*, bool=false); size_t getBytes(const char*, void*, size_t); size_t putBytes(const char*, const void*, size_t); void end(); };
#define FILE_WRITE "w"
struct File { operator bool() const; size_t write(const uint8_t*, size_t); bool seek(uint32_t); void close(); };
struct SDc { bool exists(const char*); bool mkdir(const char*); File open(const char*, const char* m="r"); bool remove(const char*); };
extern SDc SD_MMC;
bool getLocalTime(struct tm*, uint32_t=5000);
void setCpuFrequencyMhz(int);
enum Orientation { ORIENT_LANDSCAPE=0, ORIENT_PORTRAIT=1 };
extern Orientation currentOrient; void applyOrientation(Orientation, bool);
extern int SCR_W, SCR_H; extern bool sdReady, ntpSynced, gameModeActive, needRedraw;
#define CPU_MHZ_GAME 240
void exitGameMode();
enum Screen { SCR_HOME, SCR_GALLERY, SCR_CAMERA };
struct Theme { const char* name; uint16_t bg, surface, surface2, accent, accent2, text, subtext, divider, good, danger; };
Theme& T();
void showToast(const char*, int ms=1400); void vibTap(); void vibSuccess(); void vibError(); void vibWarn();
void navBack(); void navPush(Screen); void aviPlayFile(const String&); void musExtPrepare();
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
namespace lgfx { struct swap565_t { uint16_t v; }; }
#define JPEG_SCALE_HALF 2
#define JPEG_SCALE_QUARTER 4
#define JPEG_SCALE_EIGHTH 8
#define RGB565_LITTLE_ENDIAN 0
#define RGB565_BIG_ENDIAN 1
struct JPEGDRAW { int x, y, iWidth, iHeight; uint16_t* pPixels; };
typedef int (JPEG_DRAW_CALLBACK)(JPEGDRAW*);
struct JPEGDEC { int openRAM(uint8_t*, int, JPEG_DRAW_CALLBACK*); int getWidth(); int getHeight(); int decode(int,int,int); void close(); void setPixelType(int); };
struct PsramJpeg { JPEGDEC* p=nullptr; JPEGDEC* operator->(){ return p; } };
struct LGFX_Sprite {
  void fillScreen(uint16_t); void fillRoundRect(int,int,int,int,int,uint16_t); void fillCircle(int,int,int,uint16_t); void drawCircle(int,int,int,uint16_t);
  void fillRect(int,int,int,int,uint16_t); void fillRectAlpha(int,int,int,int,uint8_t,uint16_t); void drawFastVLine(int,int,int,uint16_t); void drawFastHLine(int,int,int,uint16_t);
  void setTextColor(uint16_t); void setCursor(int,int); size_t print(const char*); void setTextSize(int); int textWidth(const char*); void setTextWrap(bool);
  void pushImage(int,int,int,int,const lgfx::swap565_t*);
};
void drawToast(LGFX_Sprite&); void iosBackdrop(LGFX_Sprite&);
extern "C" void* memcpy(void*, const void*, size_t);
