// ============================================================
// usbmsc_renphone.ino -- Mode "USB Drive": SD card muncul sbg flashdisk di PC
// lewat port USB native ESP32-S3 (GPIO19/20), tanpa bongkar kartu SD.
//
// SYARAT BUILD: USB Mode = "USB-OTG (TinyUSB)"  (arduino-cli: USBMode=default).
// Kalau build pakai "Hardware CDC and JTAG", seluruh file ini jadi stub (mscStart
// balikin false) supaya build lain TIDAK rusak.
//
// API (dipanggil dari UI, mis. toggle di Settings / Control Center):
//   bool mscStart();   // SD jadi flashdisk di PC. false = gagal/tidak didukung
//   void mscStop();    // lepas drive, remount SD supaya isi baru dr PC terbaca
//   bool mscActive();
//
// ATURAN SELAMA MODE AKTIF: HP tidak boleh nulis ke SD (PC yg pegang FAT).
// Jadi UI harus "mengunci" fitur yg pakai SD (cek mscActive()), dan task
// Doom/NES/musik/rekam sebaiknya dihentikan dulu.
// ============================================================
#include <Arduino.h>
#include <SD_MMC.h>

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#include "USB.h"
#include "USBMSC.h"

static USBMSC mscDrive;
static volatile bool mscRunning = false;
static bool mscUsbStarted = false;

static int32_t mscOnRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize){
  if(!mscRunning) return -1;
  uint8_t* p = (uint8_t*)buffer;
  uint32_t n = bufsize / 512;
  for(uint32_t i = 0; i < n; i++){
    if(!SD_MMC.readRAW(p + i * 512, lba + i)) return -1;
  }
  return (int32_t)(n * 512);
}

static int32_t mscOnWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize){
  if(!mscRunning) return -1;
  uint32_t n = bufsize / 512;
  for(uint32_t i = 0; i < n; i++){
    if(!SD_MMC.writeRAW(buffer + i * 512, lba + i)) return -1;
  }
  return (int32_t)(n * 512);
}

static bool mscOnStartStop(uint8_t power_condition, bool start, bool load_eject){
  // PC menekan "Eject": biarkan; UI/pengguna memanggil mscStop() utk keluar.
  return true;
}

bool mscActive(){ return mscRunning; }

bool mscStart(){
  if(mscRunning) return true;
  if(!sdReady){ Serial.println("[msc] SD tidak terpasang"); return false; }
  uint32_t sectors = (uint32_t)SD_MMC.numSectors();
  uint32_t ssize   = (uint32_t)SD_MMC.sectorSize();
  if(sectors == 0 || ssize != 512){
    Serial.printf("[msc] kartu tidak cocok: sectors=%u sectorSize=%u\n", (unsigned)sectors, (unsigned)ssize);
    return false;
  }
  mscDrive.vendorID("REN");
  mscDrive.productID("ren_phone SD");
  mscDrive.productRevision("1.0");
  mscDrive.onRead(mscOnRead);
  mscDrive.onWrite(mscOnWrite);
  mscDrive.onStartStop(mscOnStartStop);
  mscDrive.mediaPresent(true);
  mscRunning = true;
  if(!mscDrive.begin(sectors, 512)){ mscRunning = false; Serial.println("[msc] begin gagal"); return false; }
  if(!mscUsbStarted){ USB.begin(); mscUsbStarted = true; }
  Serial.printf("[msc] aktif: %u sektor x 512B\n", (unsigned)sectors);
  return true;
}

void mscStop(){
  if(!mscRunning) return;
  mscRunning = false;
  mscDrive.mediaPresent(false);
  mscDrive.end();
  // PC mungkin sudah menulis/ubah FAT -> mount ulang supaya cache FATFS HP segar.
  SD_MMC.end();
  sdReady = false;
  initSD();
  Serial.printf("[msc] berhenti, SD remount: %s\n", sdReady ? "OK" : "GAGAL");
}

#else  // build tanpa TinyUSB: stub

bool mscActive(){ return false; }
bool mscStart(){ Serial.println("[msc] tidak didukung: build perlu USB Mode = USB-OTG (TinyUSB)"); return false; }
void mscStop(){}

#endif
