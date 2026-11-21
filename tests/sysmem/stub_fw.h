// Stub MINIMAL untuk cek-sintaks sysmem_renphone.ino di PC (BUKAN emulator Arduino).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t TickType_t; typedef int BaseType_t; typedef void* SemaphoreHandle_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xFFFFFFFFu
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void*)1; }
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return 1; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
inline void vTaskDelay(TickType_t) {}
inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 0; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }
inline size_t heap_caps_get_total_size(uint32_t) { return 0; }
inline unsigned long millis() { return 0; }
struct SerialT { void printf(const char*, ...) {} } static Serial;
enum Screen { SCR_HOME };
struct AppDef { const char* name; void (*onLowMem)(); Screen screen; };
extern AppDef apps[]; extern unsigned long appLastUsedMs[];
int appFindLruBackgroundVictim(Screen); Screen curScreen();
