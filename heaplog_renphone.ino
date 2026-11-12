// =====================================================================
//  heaplog_renphone.ino -- DIAGNOSA RAM internal saat boot (Ren Phone)
//
//  Tujuan: cari tahu di langkah mana RAM internal habis (mis. tinggal 11 KB).
//  Tiap titik di setup() mencatat: RAM internal bebas, blok terbesar, total
//  heap internal, minimum sejak boot, PSRAM bebas, jumlah task, dan waktu.
//  Hasil dicetak ke Serial DAN ditulis ke SD: /heaplog.txt
//    - ditulis di akhir setup(), lalu lagi di detik ke-20 & ke-60 (dari loop)
//  Catatan: SD baru siap setelah initSD(), jadi titik sebelum itu disimpan
//  dulu di RAM kecil (<1 KB) lalu ikut tertulis saat flush pertama.
//
//  JANGAN pakai tipe buatan sendiri di signature fungsi (prototype otomatis
//  arduino-cli). BELUM PERNAH di-compile di environment (gak ada toolchain).
// =====================================================================
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RPHL_MAX 24
static const char* rphlLabel[RPHL_MAX];
static uint32_t rphlFree[RPHL_MAX];
static uint32_t rphlLargest[RPHL_MAX];
static uint32_t rphlTotal[RPHL_MAX];
static uint32_t rphlMin[RPHL_MAX];
static uint32_t rphlPsram[RPHL_MAX];
static uint32_t rphlMs[RPHL_MAX];
static uint16_t rphlTasks[RPHL_MAX];
static int      rphlN = 0;

void rpHL(const char* label){
  if(rphlN >= RPHL_MAX) return;
  int i = rphlN++;
  rphlLabel[i]   = label;
  rphlFree[i]    = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  rphlLargest[i] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  rphlTotal[i]   = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  rphlMin[i]     = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  rphlPsram[i]   = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  rphlMs[i]      = (uint32_t)millis();
  rphlTasks[i]   = (uint16_t)uxTaskGetNumberOfTasks();
  Serial.printf("[HeapLog] %-22s bebas=%uKB blokMax=%uKB total=%uKB min=%uKB psram=%uKB task=%u t=%ums\n",
                label,
                (unsigned)(rphlFree[i]/1024), (unsigned)(rphlLargest[i]/1024),
                (unsigned)(rphlTotal[i]/1024), (unsigned)(rphlMin[i]/1024),
                (unsigned)(rphlPsram[i]/1024), (unsigned)rphlTasks[i], (unsigned)rphlMs[i]);
}

void rpHLFlush(){
  if(!sdReady) return;
  File f = SD_MMC.open("/heaplog.txt", FILE_WRITE);   // FILE_WRITE = timpa file lama
  if(!f) return;
  f.printf("RAM internal -- heaplog (satuan KB)\n");
  f.printf("SDK: %s\n", ESP.getSdkVersion());
  f.printf("PSRAM total: %u KB\n", (unsigned)(ESP.getPsramSize()/1024));
  f.printf("%-22s %7s %7s %7s %7s %8s %5s %7s\n",
           "titik","bebas","blokMax","total","minimum","psram","task","ms");
  for(int i=0;i<rphlN;i++){
    f.printf("%-22s %7u %7u %7u %7u %8u %5u %7u\n",
             rphlLabel[i],
             (unsigned)(rphlFree[i]/1024), (unsigned)(rphlLargest[i]/1024),
             (unsigned)(rphlTotal[i]/1024), (unsigned)(rphlMin[i]/1024),
             (unsigned)(rphlPsram[i]/1024), (unsigned)rphlTasks[i], (unsigned)rphlMs[i]);
  }
  f.close();
}

// dipanggil dari awal loop(): catat lagi saat idle (setelah WiFi/NTP sempat jalan)
void rpHLLoopPoll(){
  static uint8_t st = 0;
  unsigned long ms = millis();
  if(st == 0 && ms > 20000UL){ st = 1; rpHL("idle_20dtk"); rpHLFlush(); }
  else if(st == 1 && ms > 60000UL){ st = 2; rpHL("idle_60dtk"); rpHLFlush(); }
}
