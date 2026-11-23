// =====================================================================
// sysmem_renphone.ino -- glue firmware untuk sysmem.h ("MMU software" firmware utama)
// =====================================================================
// Yang dikerjakan: satu Mgr global (accounting per owner, anggaran, cadangan internal, rantai
// reclaim, gate blok kontigu) di atas heap_caps_*. Reclaim memakai mekanisme App Memory Guard
// yang sudah ada (korban LRU app background -> onLowMem()).
//
// Yang TIDAK dikerjakan: tidak mengubah malloc/new yang sudah ada. Pemanggil lama tetap jalan dan
// muncul sebagai "untracked" di smDump(); pindahkan satu per satu ke smAlloc()/smFree().
// Overhead 16 B + 4 B per blok: pakai untuk buffer >= 256 B.
//
// Thread-safety: Mgr tidak punya kunci sendiri; semua entri di sini memakai mutex rekursif.
// smTick() memakai try-lock (tidak pernah memblokir loop()).
//
// CATATAN ARDUINO-CLI: tidak ada tipe sysmem:: di tanda tangan fungsi non-static (prototipe
// otomatis dibuat sebelum header terbaca); state memakai global static.
// STATUS: logika inti teruji di PC (tests/sysmem). Glue ini baru dicek sintaks. Belum dijalankan di HP.
// =====================================================================

#include "sysmem_fw.h"

static sysmem::Mgr smM;
static bool smInited = false;
static SemaphoreHandle_t smMx = nullptr;
static uint32_t smLastDumpMs = 0;

static void* smB_alloc(void*, size_t n, int region) {
  return heap_caps_malloc(n, (region == sysmem::REG_INT ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM) | MALLOC_CAP_8BIT);
}
static void   smB_free(void*, void* p, int) { heap_caps_free(p); }
static uint32_t smCaps(int region) { return (region == sysmem::REG_INT ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM) | MALLOC_CAP_8BIT; }
static size_t smB_free_sz(void*, int region) { return heap_caps_get_free_size(smCaps(region)); }
static size_t smB_largest(void*, int region) { return heap_caps_get_largest_free_block(smCaps(region)); }
static size_t smB_total(void*, int region) { return heap_caps_get_total_size(smCaps(region)); }
static uint32_t smB_now(void*) { return (uint32_t)millis(); }
static void   smB_delay(void*, uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// reclaim: korban LRU app background (sama seperti appMemGuardTick). true = ada yang dilepas.
static bool smReclaimLru(void*, int) {
  int v = appFindLruBackgroundVictim(curScreen());
  if (v < 0) return false;
  apps[v].onLowMem();
  appLastUsedMs[v] = 0;       // jangan langsung terpilih lagi sebelum dibuka ulang
  Serial.printf("[SysMem] reclaim: cache RAM app '%s' dilepas\n", apps[v].name);
  return true;
}

static sysmem::Mgr& smMgr() {
  if (!smInited) {
    if (!smMx) smMx = xSemaphoreCreateRecursiveMutex();
    sysmem::Backend b; memset(&b, 0, sizeof(b));
    b.alloc = smB_alloc; b.free = smB_free; b.freeBytes = smB_free_sz; b.largest = smB_largest;
    b.totalBytes = smB_total; b.nowMs = smB_now; b.delayMs = smB_delay;
    smM.init(b);
#ifndef RP_CONTIG_HOLD_KB
#define RP_CONTIG_HOLD_KB 32     // 0 = matikan cadangan kontigu. Bisa ditimpa: -DRP_CONTIG_HOLD_KB=24 (32 agar catur, 30 KB, ikut tertolong)
#endif
    smM.cfg.holdContig = (uint32_t)RP_CONTIG_HOLD_KB * 1024u;
    // FRAG_V2: cadangan dulu cuma bekerja sekali (setelah dilepas butuh blok terbesar >= hold+12 KB & jeda 5 dtk,
    // syarat yg nyaris mustahil di heap terfragmentasi). Margin & jeda diperkecil supaya cadangan cepat diambil lagi
    // oleh smTick() begitu TLS selesai. holdFreeMin (64 KB) tetap: cadangan tidak boleh menguras RAM internal.
    smM.cfg.holdMargin  = 4u * 1024u;
    smM.cfg.holdQuietMs = 1000u;
    //            id          nama        budget int  budget psram  priv
    smM.setOwner(SM_O_SYSTEM, "system",   0,          0,            true);
    smM.setOwner(SM_O_UI,     "ui",       0,          0);
    smM.setOwner(SM_O_NET,    "net",      0,          0,            true);
    smM.setOwner(SM_O_TLS,    "tls",      0,          0,            true);
    smM.setOwner(SM_O_CALL,   "call",     0,          0);
    smM.setOwner(SM_O_CHAT,   "chat",     0,          0);
    smM.setOwner(SM_O_MEDIA,  "media",    0,          0);
    smM.setOwner(SM_O_GAME,   "game",     0,          0);
    smM.setOwner(SM_O_BOOKS,  "books",    0,          0);
    smM.setOwner(SM_O_BACKUP, "backup",   0,          0);
    smM.setOwner(SM_O_AI,     "ai",       0,          0);
    smM.setOwner(SM_O_NYX,    "nyx",      0,          0);
    smM.setOwner(SM_O_OTHER,  "other",    0,          0);
    smM.addReclaim(smReclaimLru, nullptr, -1, 50);
    smInited = true;
  }
  return smM;
}

static inline bool smLock(TickType_t t) { return smMx ? xSemaphoreTakeRecursive(smMx, t) == pdTRUE : true; }
static inline void smUnlock() { if (smMx) xSemaphoreGiveRecursive(smMx); }

void* smAlloc(int owner, size_t n, int flags) {
  if (!smInited) { if (!smMx) smMx = xSemaphoreCreateRecursiveMutex(); }
  smLock(portMAX_DELAY);
  void* p = smMgr().alloc(owner, n, flags);
  smUnlock();
  return p;
}

void smFree(void* p) {
  if (!p) return;
  smLock(portMAX_DELAY);
  bool ok = smMgr().free(p);
  smUnlock();
  if (!ok) Serial.printf("[SysMem] free bermasalah (canary/double free/header rusak) p=%p\n", p);
}

void* smRealloc(void* p, size_t n, int flags) {
  smLock(portMAX_DELAY);
  void* q = smMgr().realloc(p, n, flags);
  smUnlock();
  return q;
}

bool smWaitContig(uint32_t need, uint32_t timeoutMs, int skipOwner) {
  uint32_t t0 = millis();
  for (;;) {
    smLock(portMAX_DELAY);
    bool ok = smMgr().contigNow(need, sysmem::REG_INT, skipOwner);   // satu putaran reclaim; mutex dilepas sebelum menunggu
    smUnlock();
    if (ok) return true;
    if ((uint32_t)(millis() - t0) >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void smTick() {
  if (!smLock(0)) return;                         // jangan pernah memblokir loop()
  sysmem::Mgr& m = smMgr();
  int lv = m.pressure();
  bool did = m.tick();
  uint32_t now = millis();
  bool dump = (lv >= 2 && now - smLastDumpMs > 30000);
  if (dump) smLastDumpMs = now;
  smUnlock();
  (void)did;
  if (dump) smDump();                             // rate-limit 30 dtk: jejak diagnosa saat RAM kritis
}

int smPressure() {
  smLock(portMAX_DELAY);
  int lv = smMgr().pressure();
  smUnlock();
  return lv;
}

void smDump() {
  char buf[1100];
  smLock(portMAX_DELAY);
  smMgr().format(buf, sizeof(buf));
  smUnlock();
  Serial.printf("[SysMem]\n%s", buf);
}

// ---- cadangan kontigu (anti-fragmentasi) -----------------------------------------------------
bool smHoldTake() {
  smLock(portMAX_DELAY);
  bool ok = smMgr().holdTake();
  smUnlock();
  if (ok) Serial.printf("[SysMem] cadangan kontigu %u KB diambil (blok terbesar kini %u B)\n",
                        (unsigned)(smMgr().holdSize / 1024), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return ok;
}

void smHoldRelease() {
  smLock(portMAX_DELAY);
  bool did = smMgr().holdRelease();
  smUnlock();
  if (did) Serial.printf("[SysMem] cadangan kontigu dilepas (blok terbesar kini %u B)\n",
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

uint32_t smHoldBytes() {
  if (!smInited) return 0;
  return smM.holdSize;                            // baca 32-bit tunggal: aman tanpa kunci
}

bool smEnsureContig(uint32_t need) {
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= need) return true;
  smHoldRelease();
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= need;
}
