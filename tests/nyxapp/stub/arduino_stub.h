// Stub MINIMAL hanya untuk cek-sintaks nyxapp_renphone.ino di PC (BUKAN emulator Arduino).
// Memastikan tipe/nama yang dipakai glue konsisten dengan API Arduino-ESP32 & phone.ino.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t TickType_t; typedef int BaseType_t; typedef uint32_t StackType_t; typedef void* TaskHandle_t; typedef unsigned UBaseType_t;
typedef void (*TaskFunction_t)(void*);
struct StaticTask_t { char x[64]; };
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xFFFFFFFFu
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
#define WL_CONNECTED 3
inline uint32_t ulTaskNotifyTake(int, TickType_t) { return 0; }
inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return 1; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 0; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }
inline unsigned long millis() { return 0; }
inline void delay(unsigned) {}
inline uint32_t esp_random() { return 4; }
struct SerialT { void printf(const char*, ...) {} void println(const char*) {} } static Serial;
class File {
 public:
  File() {} operator bool() const { return false; } bool isDirectory() { return false; } const char* name() { return ""; }
  File openNextFile() { return File(); } void close() {} size_t read(uint8_t*, size_t) { return 0; } size_t write(const uint8_t*, size_t) { return 0; }
  bool seek(uint32_t) { return true; } size_t size() { return 0; }
};
struct SDT { File open(const char*, const char* = "r") { return File(); } bool exists(const char*) { return false; } bool remove(const char*) { return true; } bool mkdir(const char*) { return true; } } static SD_MMC;
struct WiFiT { int status() { return 0; } } static WiFi;
struct WiFiClient { int available() { return 0; } int readBytes(uint8_t*, size_t) { return 0; } void setTimeout(int) {} };
struct WiFiClientSecure : WiFiClient { void setInsecure() {} };
struct HTTPClient { bool begin(WiFiClient&, const char*) { return false; } void setTimeout(int) {} int GET() { return -1; } WiFiClient* getStreamPtr() { return nullptr; } bool connected() { return false; } void end() {} };
namespace lgfx { struct LGFXBase {}; }
struct LGFX_Sprite : lgfx::LGFXBase {
  void fillRect(int, int, int, int, uint32_t) {} void drawRect(int, int, int, int, uint32_t) {} void fillRoundRect(int, int, int, int, int, uint32_t) {}
  void drawPixel(int, int, uint32_t) {} void drawLine(int, int, int, int, uint32_t) {} void fillCircle(int, int, int, uint32_t) {} void drawCircle(int, int, int, uint32_t) {}
  void setTextSize(int) {} void setTextColor(uint32_t) {} void setCursor(int, int) {} void print(const char*) {} int textWidth(const char*) { return 0; }
  void pushImage(int, int, int, int, const uint16_t*) {} void setClipRect(int, int, int, int) {} void clearClipRect() {}
};
struct Theme { uint16_t bg, surface2, text, subtext, accent; }; inline Theme& T() { static Theme t; return t; }
enum Screen { SCR_HOME, SCR_NYX };
extern bool sdReady, needRedraw; extern int SCR_W; extern float mpuAx, mpuAy, mpuAz;
#define STATUS_H 22
int backY(); bool mpuReadRaw(); Screen curScreen(); void showToast(const char*, int = 1400); void navBack(); bool isBack(int, int);
void iosTitle(LGFX_Sprite&, int, int, const char*); void iosBackdrop(LGFX_Sprite&); void drawStatusBar(LGFX_Sprite&); void drawBack(LGFX_Sprite&); void drawToast(LGFX_Sprite&);
void drawGlassPanel(LGFX_Sprite&, int, int, int, int, int, uint16_t, uint8_t); uint16_t blend565(uint16_t, uint16_t, uint8_t);
void nyxAaCircle(lgfx::LGFXBase&, int, int, int, uint16_t); void nyxAaRR(lgfx::LGFXBase&, int, int, int, int, int, uint16_t);
struct AppDef { const char* name; char sym; uint16_t color; void (*onEnter)(); void (*onExit)(); void (*draw)(LGFX_Sprite&); void (*touch)(int, int, bool, bool); Screen screen; void (*onLowMem)() = nullptr; };
extern AppDef apps[]; extern unsigned long appLastUsedMs[]; int appFindLruBackgroundVictim(Screen);
BaseType_t createTaskPsramStack(TaskFunction_t, const char*, uint32_t, void*, StackType_t**, StaticTask_t*, TaskHandle_t*, BaseType_t);
BaseType_t createTaskPsramStackPrio(TaskFunction_t, const char*, uint32_t, void*, StackType_t**, StaticTask_t*, TaskHandle_t*, BaseType_t, UBaseType_t);
