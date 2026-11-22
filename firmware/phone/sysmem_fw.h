#pragma once
// =====================================================================
// sysmem_fw.h -- API SysMem untuk firmware utama (diinclude phone.ino lewat apply_sysmem_patch.py)
// Hanya tipe primitif di tanda tangan fungsi (aturan prototipe otomatis arduino-cli).
// Implementasi: sysmem_renphone.ino. Logika inti & tes PC: sysmem.h, tests/sysmem/.
// =====================================================================
#include "sysmem.h"

// Pemilik alokasi. Tambah di sini + daftarkan di smMgr() (sysmem_renphone.ino).
enum SmOwner {
  SM_O_SYSTEM = 0,  // priv: boleh menembus cadangan internal
  SM_O_UI,
  SM_O_NET,         // priv: WiFi/HTTP/MQTT
  SM_O_TLS,         // priv: buffer & handshake TLS
  SM_O_CALL,
  SM_O_CHAT,
  SM_O_MEDIA,       // musik, video, galeri, kamera preview
  SM_O_GAME,        // DOOM, NES, catur
  SM_O_BOOKS,       // ebook, gutenberg, kamus
  SM_O_BACKUP,
  SM_O_AI,
  SM_O_NYX,         // launcher Nyx Apps (arena app punya manajer sendiri: nyxmem.h)
  SM_O_OTHER,
  SM_O_COUNT
};

// flag = sysmem::F_INTERNAL | F_PSRAM | F_PREFER_PSRAM | F_ZERO
void*  smAlloc(int owner, size_t n, int flags);
void   smFree(void* p);
void*  smRealloc(void* p, size_t n, int flags);
// tunggu/reclaim sampai blok kontigu RAM internal >= need (TLS, stack task, DMA). true = siap.
bool   smWaitContig(uint32_t need, uint32_t timeoutMs, int skipOwner);
// panggil tiap putaran loop(): satu langkah reclaim bila RAM internal tertekan (throttle sendiri)
void   smTick();
// 0 normal, 1 LOW, 2 CRITICAL
int    smPressure();
// cetak tabel pemakaian per owner ke Serial
void   smDump();
// CADANGAN KONTIGU RAM internal (anti-fragmentasi). Diambil saat boot/tick, dilepas otomatis oleh smWaitContig().
bool   smHoldTake();                       // ambil sekarang (true = berhasil)
void   smHoldRelease();                    // lepas sekarang (panggil sebelum init WiFi / alokasi blok besar internal)
uint32_t smHoldBytes();                    // ukuran cadangan yang sedang dipegang (0 = tidak ada)
// true kalau blok internal terbesar >= need; kalau belum, cadangan dilepas dulu lalu dicek lagi
bool   smEnsureContig(uint32_t need);
// alokasi PSRAM dulu, jatuh ke heap biasa kalau PSRAM penuh/tidak ada. Bebaskan dengan free()/heap_caps_free().
// JANGAN dipakai untuk buffer yang diisi DMA dari SD (SD SPI Aether: baca langsung ke PSRAM bisa menghasilkan nol).
static inline void* rpAllocPs(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
