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
//  v3: sampel RAM tiap 10-15 dtk sampai detik ke-90, lalu tabel DITAMPILKAN DI LAYAR di detik
//  ke-100 (ditahan 60 dtk supaya bisa difoto) + cek baca-balik SD (status SD:OK / SD:ISI-RUSAK / SD:GAGAL-TULIS).
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

static const char* rphlSdStr = "SD:BELUM";

// baca-balik: file yg baru ditulis harus diawali teks "RAM internal"
static void rpHLVerify(){
  File r = SD_MMC.open("/heaplog.txt", FILE_READ);
  if(!r){ rphlSdStr = "SD:GAGAL-BACA"; return; }
  char b[13]; memset(b, 0, sizeof(b));
  int n = r.read((uint8_t*)b, 12);
  r.close();
  rphlSdStr = (n == 12 && memcmp(b, "RAM internal", 12) == 0) ? "SD:OK" : "SD:ISI-RUSAK";
}

void rpHLFlush(){
  if(!sdReady){ rphlSdStr = "SD:TIDAK-SIAP"; Serial.println("[HeapLog] SD tidak siap"); return; }
  File f = SD_MMC.open("/heaplog.txt", FILE_WRITE);   // FILE_WRITE = timpa file lama
  if(!f){ rphlSdStr = "SD:GAGAL-TULIS"; Serial.println("[HeapLog] gagal buka file tulis"); return; }
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
  rpHLVerify();
  Serial.printf("[HeapLog] %s\n", rphlSdStr);
}

// tampilkan tabel di layar (menimpa UI sementara; UI digambar ulang sesudahnya)
static void rpHLShow(){
  float sc = (display.width() >= 440) ? 1.25f : 1.0f;
  int lh = (int)(8.0f * sc) + 3;
  char buf[110];
  display.fillScreen(0x0000);
  display.setTextFont(1);
  display.setTextSize(sc);
  display.setTextColor(0xFFFF, 0x0000);
  display.setCursor(2, 2);
  snprintf(buf, sizeof(buf), "HEAPLOG KB  %s", rphlSdStr);
  display.print(buf);
  display.setCursor(2, 2 + lh);
  display.print("B=bebas M=blokMax T=total L=min P=psram K=task");
  for(int i = 0; i < rphlN; i++){
    snprintf(buf, sizeof(buf), "%-17.17s B%3u M%3u T%3u L%3u P%4u K%u %us",
             rphlLabel[i],
             (unsigned)(rphlFree[i]/1024), (unsigned)(rphlLargest[i]/1024),
             (unsigned)(rphlTotal[i]/1024), (unsigned)(rphlMin[i]/1024),
             (unsigned)(rphlPsram[i]/1024),
             (unsigned)rphlTasks[i], (unsigned)(rphlMs[i]/1000));
    display.setCursor(2, 2 + (i + 2) * lh);
    display.print(buf);
  }
  display.setTextSize(1);
}

// dipanggil dari awal loop(): catat lagi saat idle (setelah WiFi/NTP sempat jalan)
void rpHLLoopPoll(){
  static uint8_t st = 0;
  unsigned long ms = millis();
  // v3: sampel diam-diam (tanpa SD/layar) supaya tren RAM seiring waktu kelihatan
  static const uint32_t TS[7] = {12000UL, 20000UL, 30000UL, 45000UL, 60000UL, 75000UL, 90000UL};
  static const char* LB[7] = {"idle_12dtk","idle_20dtk","idle_30dtk","idle_45dtk","idle_60dtk","idle_75dtk","idle_90dtk"};
  if(st < 7){
    if(ms > TS[st]){ rpHL(LB[st]); st++; }
    return;
  }
  if(st == 7 && ms > 100000UL){
    st = 8;
    rpHL("idle_100dtk");
    rpHLFlush();
    rpHLShow();
    delay(60000);        // tahan 60 dtk supaya layarnya bisa difoto (UI membeku sementara)
    needRedraw = true;
  }
}
