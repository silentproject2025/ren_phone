/*=========================================
 * lv_conf.h — konfigurasi LVGL untuk ren_phone
 * TARUH FILE INI DI: Documents/Arduino/libraries/lv_conf.h
 * (SEJAJAR dengan folder "lvgl", BUKAN di dalam folder sketch)
 * Hanya berisi override yang kita butuhkan — sisanya pakai
 * default bawaan LVGL (lv_conf_internal.h akan mengisi otomatis).
 *=======================================*/
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*--- Warna & memori ---*/
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0   // ganti ke 1 kalau warna kebalik pas ditest di layar

// Pakai PSRAM buat memory pool internal LVGL (hemat SRAM internal ESP32-S3)
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
  #define LV_MEM_CUSTOM_ALLOC(size)      heap_caps_malloc((size), MALLOC_CAP_SPIRAM)
  #define LV_MEM_CUSTOM_FREE             heap_caps_free
  #define LV_MEM_CUSTOM_REALLOC(p,size)  heap_caps_realloc((p), (size), MALLOC_CAP_SPIRAM)
#endif

#define LV_TICK_CUSTOM 0

/*--- Font ---*/
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/*--- Tema bawaan (kita override warnanya sendiri via style, tapi tetap perlu aktif) ---*/
#define LV_USE_THEME_DEFAULT             1
#define LV_THEME_DEFAULT_DARK            1
#define LV_THEME_DEFAULT_GROW            1
#define LV_THEME_DEFAULT_TRANSITION_TIME 100

/*--- Widget yang dipakai ren_phone ---*/
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_BTN       1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS    1
#define LV_USE_CHECKBOX  1
#define LV_USE_DROPDOWN  1
#define LV_USE_IMG       1
#define LV_USE_LABEL     1
#define LV_USE_LINE      1
#define LV_USE_ROLLER    1
#define LV_USE_SLIDER    1
#define LV_USE_SWITCH    1
#define LV_USE_TEXTAREA  1
#define LV_USE_KEYBOARD  1
#define LV_USE_LIST      1
#define LV_USE_MSGBOX    1

/* Widget yang tidak dipakai — dimatikan biar hemat flash & kompilasi lebih cepat */
#define LV_USE_CHART     0
#define LV_USE_CALENDAR  0
#define LV_USE_METER     0
#define LV_USE_TABVIEW   0
#define LV_USE_SPINBOX   0
#define LV_USE_SPINNER   0
#define LV_USE_TILEVIEW  0
#define LV_USE_WIN       0

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*--- Logging: matikan biar tidak berisik & hemat flash ---*/
#define LV_USE_LOG 0

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#endif /*LV_CONF_H*/
