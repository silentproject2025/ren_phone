#pragma once
// =====================================================================
// nyxvm_core.h -- inti NyxVM: interpreter bytecode + MMU software
// =====================================================================
// MURNI C++ (tanpa Arduino/ESP-IDF) supaya bisa diuji lengkap di PC
// (lihat tests/nyxvm/). Tidak ada alokasi heap di sini: semua memori
// (pool frame, tabel frame, page table, bitmap swap) DISUPLAI pemanggil
// -- di firmware semuanya dari PSRAM, di tes dari malloc biasa.
//
// KONSEP
//   * Tiap proses punya ruang alamat virtual 16 MB (24 bit), halaman 4 KB,
//     page table datar 4096 entri x 8 byte (32 KB).
//   * SEMUA load/store/fetch lewat MMU ini -> demand paging, proteksi
//     R/W/X, halaman nol bersama (copy-on-write), swap ke penyimpanan
//     (SD) dan snapshot hibernate. Ini hal yang TIDAK bisa diberikan
//     hardware ESP32-S3 per aplikasi.
//   * Soft-TLB 16 entri direct-mapped per proses: akses normal = 1
//     perbandingan + 1 penjumlahan. Bit "accessed" didekati lewat
//     pengisian TLB (clock membuang entri TLB saat menurunkan ref).
//   * Penggantian frame: clock/second-chance GLOBAL, dengan batas
//     resident (rssMax) per proses (kena batas -> korban dari halaman
//     milik sendiri).
//   * Input TIDAK DIPERCAYA (image & snapshot dari SD): semua dicek.
//     Interpreter tidak pernah menyentuh memori di luar pool frame.
//
// ISA (instruksi 32-bit, little-endian):
//   [31..24]=op [23..20]=A [19..16]=B [15..12]=C | [15..0]=imm16
//   J-type    : [31..24]=op [23..0]=imm24 (offset kata, bertanda)
//   r15 = nol (baca 0, tulis diabaikan)
// =====================================================================

#include <stdint.h>
#include <string.h>
#include "nyx_abi.h"

#if defined(__GNUC__)
#define NYX_LIKELY(x)   __builtin_expect(!!(x), 1)
#define NYX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define NYX_LIKELY(x)   (x)
#define NYX_UNLIKELY(x) (x)
#endif

namespace nyx {

// ------------------------------- opcode -------------------------------
enum Op : uint8_t {
  OP_NOP = 0x00, OP_HALT = 0x01, OP_YIELD = 0x02, OP_BRK = 0x03, OP_SYS = 0x04,
  // R-type: A = B op C
  OP_ADD = 0x10, OP_SUB = 0x11, OP_MUL = 0x12, OP_MULH = 0x13,
  OP_DIV = 0x14, OP_DIVU = 0x15, OP_REM = 0x16, OP_REMU = 0x17,
  OP_AND = 0x18, OP_OR = 0x19, OP_XOR = 0x1A,
  OP_SHL = 0x1B, OP_SHR = 0x1C, OP_SAR = 0x1D,
  OP_SLT = 0x1E, OP_SLTU = 0x1F, OP_SEQ = 0x20, OP_SNE = 0x21,
  // I-type: A = B op imm16
  OP_ADDI = 0x28, OP_ANDI = 0x29, OP_ORI = 0x2A, OP_XORI = 0x2B,
  OP_SHLI = 0x2C, OP_SHRI = 0x2D, OP_SARI = 0x2E,
  OP_SLTI = 0x2F, OP_SLTIU = 0x30, OP_MULI = 0x31, OP_LUI = 0x32,
  // memori: A <-> [B + simm16]
  OP_LDB = 0x38, OP_LDBU = 0x39, OP_LDH = 0x3A, OP_LDHU = 0x3B, OP_LDW = 0x3C,
  OP_STB = 0x3D, OP_STH = 0x3E, OP_STW = 0x3F,
  // cabang: if (A cmp B) pc = next + simm16*4
  OP_BEQ = 0x40, OP_BNE = 0x41, OP_BLT = 0x42, OP_BGE = 0x43,
  OP_BLTU = 0x44, OP_BGEU = 0x45,
  // lompat
  OP_JMP = 0x48, OP_JAL = 0x49, OP_JR = 0x4A, OP_JALR = 0x4B
};

// ------------------------------- PTE ----------------------------------
enum : uint16_t {
  PF_MAPPED = 1, PF_R = 2, PF_W = 4, PF_X = 8, PF_RES = 16, PF_DIRTY = 32,
  PF_KIND_SHIFT = 6, PF_KIND_MASK = 3u << 6
};
enum : uint16_t { K_ZERO = 0, K_IMG = 1, K_SNAP = 2, K_SWAP = 3 };

struct Pte { uint16_t flags; uint16_t frame; uint32_t aux; };   // 8 byte
static_assert(sizeof(Pte) == 8, "Pte harus 8 byte");

inline uint16_t pteKind(const Pte& e) { return (uint16_t)((e.flags & PF_KIND_MASK) >> PF_KIND_SHIFT); }
inline void pteSetKind(Pte& e, uint16_t k) { e.flags = (uint16_t)((e.flags & ~PF_KIND_MASK) | (k << PF_KIND_SHIFT)); }

// ----------------------------- header image ---------------------------
struct ImgHeader {
  uint32_t magic;            // 0
  uint16_t version;          // 4
  uint16_t flags;            // 6
  uint32_t entry;            // 8
  uint32_t perms;            // 12
  uint16_t stackKb;          // 16
  uint16_t heapKb;           // 18
  uint16_t rssKb;            // 20
  uint16_t color565;         // 22
  char     sym;              // 24
  char     pad[3];           // 25
  char     name[24];         // 28
  uint32_t secVa[4];         // 52  code, rodata, data, bss
  uint32_t secPages[4];      // 68
  uint32_t secFpage[4];      // 84  indeks halaman file (bss diabaikan)
  uint32_t heapVa;           // 100
  uint32_t imgPages;         // 104 total halaman file termasuk header
  uint32_t crc;              // 108 CRC32 byte 0..107
};
static_assert(sizeof(ImgHeader) == 112, "ImgHeader harus 112 byte");

struct SnapHeader {
  uint32_t magic; uint16_t version; uint16_t pad;
  uint32_t imgCrc;
  uint32_t r[16]; uint32_t pc;
  uint32_t brk, brkMin, brkMax, stackLo, stackHi, rssMax, perms, flags;
  uint32_t nPages;           // jumlah halaman data tersimpan
  uint32_t crc;              // CRC32 byte sebelum field ini
};
#define NYX_SNAP_PT_PAGE   1                       // tabel PTE di halaman 1..8
#define NYX_SNAP_DATA_PAGE 9                       // halaman data mulai di 9

inline uint32_t crc32(const void* data, uint32_t n) {
  const uint8_t* p = (const uint8_t*)data; uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

struct Proc; struct Mem;

// ------------------------------- host ---------------------------------
struct Host {
  void* ctx;
  // file: 0 = image aplikasi, 1 = snapshot. Isi 4096 byte; di luar EOF diisi nol. false = gagal IO.
  bool (*pageRead)(void* ctx, uint8_t file, uint32_t page, uint8_t* dst);
  bool (*swapRead)(void* ctx, uint32_t slot, uint8_t* dst);
  bool (*swapWrite)(void* ctx, uint32_t slot, const uint8_t* src);
  bool (*snapWrite)(void* ctx, uint32_t off, const uint8_t* src, uint32_t len);
  // handler syscall: NYX_SYSRC_* (>=0) atau -(kode fault)
  int  (*sys)(void* ctx, Proc* p, uint32_t num);
};

// ------------------------------- TLB ----------------------------------
#define NYX_TLB_N 16
#define NYX_TAG_NONE 0xFFFFFFFFu
struct TlbEnt { uint32_t tagR; uint32_t tagW; uint8_t* base; };

struct FrameInfo { Proc* owner; uint16_t vpn; uint8_t ref; uint8_t used; };

// -------------------------------- Proc --------------------------------
enum ProcStatus : uint32_t { PS_NEW = 0, PS_READY = 1, PS_EXITED = 2, PS_FAULT = 3 };
enum RunResult { RUN_BUDGET = 0, RUN_YIELD = 1, RUN_EXIT = 2, RUN_FAULT = 3 };

struct Proc {
  uint32_t r[16];
  uint32_t pc;
  Mem* mem;
  Pte* pt;                       // NYX_NPAGES entri, disuplai pemanggil
  TlbEnt tlb[NYX_TLB_N];
  uint8_t* fcBase; uint32_t fcVpn;           // cache fetch
  uint32_t status;
  uint32_t faultCode, faultAddr, faultPc;
  int32_t  exitCode;
  uint32_t rss, rssMax;                      // halaman resident / batas
  uint32_t brk, brkMin, brkMax;              // heap
  uint32_t stackLo, stackHi;
  uint32_t perms, flags;
  uint32_t imgCrc, imgPages;
  uint32_t entry;
  // statistik per proses
  uint32_t nFault, nSwapIn, nSwapOut, nTlbMiss, nPageIn;
  uint64_t nInstr;
  void* user;                                // cookie milik host/lapisan syscall

  // ---- API ----
  void init(Mem* m, Pte* ptab);
  int  loadImage();                          // 0 ok, selain itu NYX_FAULT_*
  bool sbrk(int32_t delta, uint32_t* oldBrk);
  void release();                            // lepas semua frame & slot swap
  bool hibernate();                          // tulis snapshot lewat Host::snapWrite
  int  resume();                             // setelah loadImage(); pulihkan dari snapshot
  void flushTlb();

  // akses memori untuk lapisan syscall (lewat MMU, aman)
  bool readBytes(uint32_t va, void* dst, uint32_t n);
  bool writeBytes(uint32_t va, const void* src, uint32_t n);
  int  readCStr(uint32_t va, char* dst, uint32_t cap);     // panjang, -1 jika bukan string valid
  bool rd32(uint32_t va, uint32_t* v);

  // jalur cepat (dipakai interpreter)
  inline uint8_t* rptr(uint32_t a) {
    uint32_t vpn = a >> NYX_PAGE_SHIFT; TlbEnt& t = tlb[vpn & (NYX_TLB_N - 1)];
    if (NYX_LIKELY(t.tagR == vpn)) return t.base + (a & (NYX_PAGE_SIZE - 1));
    return rslow(a);
  }
  inline uint8_t* wptr(uint32_t a) {
    uint32_t vpn = a >> NYX_PAGE_SHIFT; TlbEnt& t = tlb[vpn & (NYX_TLB_N - 1)];
    if (NYX_LIKELY(t.tagW == vpn)) return t.base + (a & (NYX_PAGE_SIZE - 1));
    return wslow(a);
  }
  uint8_t* rslow(uint32_t a);
  uint8_t* wslow(uint32_t a);
  bool fetchSlow(uint32_t pc, uint32_t* ins);
  void setFault(uint32_t code, uint32_t addr);
  uint8_t* pageIn(uint32_t vpn);             // bawa halaman ke frame (null + fault bila gagal)
  bool readPageContent(uint32_t vpn, uint8_t* dst);
  void mapRange(uint32_t vpn0, uint32_t n, uint16_t perm, uint16_t kind, uint32_t aux0, bool auxInc);
};

// -------------------------------- Mem ---------------------------------
struct Mem {
  uint8_t* pool; uint32_t nFrames;           // frame 0 dicadangkan = halaman nol bersama
  FrameInfo* fi;
  uint16_t* freeStack; uint32_t freeCount;
  uint32_t clockHand;
  uint8_t* swapBitmap; uint32_t swapSlots; uint32_t swapHint; uint32_t swapUsed;
  uint8_t* scratch;                          // 4096 byte
  Host host;
  // statistik global
  uint32_t statEvict, statSwapIn, statSwapOut, statNoSwap;

  void init(uint8_t* poolMem, uint32_t frames, FrameInfo* fiMem, uint16_t* freeMem,
            uint8_t* swapBits, uint32_t nSlots, uint8_t* scratchMem, const Host& h);
  int  allocFrame(Proc* p);                  // indeks frame (>=1) atau -1
  int  evictOne(Proc* filter);               // indeks frame yang dibebaskan atau -1
  int  allocSlot();
  void freeSlot(uint32_t s);
  inline uint8_t* frameMem(uint32_t f) { return pool + (size_t)f * NYX_PAGE_SIZE; }
  inline uint8_t* zeroPage() { return pool; }
  uint32_t freeFrames() const { return freeCount; }
};

// ====================================================================
//                           IMPLEMENTASI
// ====================================================================

inline void Mem::init(uint8_t* poolMem, uint32_t frames, FrameInfo* fiMem, uint16_t* freeMem,
                      uint8_t* swapBits, uint32_t nSlots, uint8_t* scratchMem, const Host& h) {
  pool = poolMem; nFrames = frames; fi = fiMem; freeStack = freeMem;
  swapBitmap = swapBits; swapSlots = nSlots; swapHint = 0; swapUsed = 0;
  scratch = scratchMem; host = h; clockHand = 1;
  statEvict = statSwapIn = statSwapOut = statNoSwap = 0;
  memset(pool, 0, NYX_PAGE_SIZE);                       // halaman nol bersama
  for (uint32_t i = 0; i < frames; i++) { fi[i].owner = nullptr; fi[i].vpn = 0; fi[i].ref = 0; fi[i].used = 0; }
  fi[0].used = 1;                                       // frame 0 tidak pernah dibagikan
  freeCount = 0;
  for (uint32_t i = frames; i > 1; i--) freeStack[freeCount++] = (uint16_t)(i - 1);
  if (swapBitmap && nSlots) memset(swapBitmap, 0, (nSlots + 7) / 8);
}

inline int Mem::allocSlot() {
  if (!swapBitmap || swapSlots == 0 || !host.swapWrite) return -1;
  for (uint32_t k = 0; k < swapSlots; k++) {
    uint32_t s = (swapHint + k) % swapSlots;
    if (!(swapBitmap[s >> 3] & (1u << (s & 7)))) {
      swapBitmap[s >> 3] |= (uint8_t)(1u << (s & 7)); swapHint = s + 1; swapUsed++; return (int)s;
    }
  }
  return -1;
}
inline void Mem::freeSlot(uint32_t s) {
  if (!swapBitmap || s >= swapSlots) return;
  if (swapBitmap[s >> 3] & (1u << (s & 7))) { swapBitmap[s >> 3] &= (uint8_t)~(1u << (s & 7)); if (swapUsed) swapUsed--; }
}

inline void Proc::init(Mem* m, Pte* ptab) {
  memset(r, 0, sizeof(r)); pc = 0; mem = m; pt = ptab;
  memset(pt, 0, sizeof(Pte) * NYX_NPAGES);
  flushTlb();
  status = PS_NEW; faultCode = faultAddr = faultPc = 0; exitCode = 0;
  rss = 0; rssMax = 0; brk = brkMin = brkMax = 0; stackLo = stackHi = 0;
  perms = flags = 0; imgCrc = imgPages = entry = 0;
  nFault = nSwapIn = nSwapOut = nTlbMiss = nPageIn = 0; nInstr = 0; user = nullptr;
}

inline void Proc::flushTlb() {
  for (int i = 0; i < NYX_TLB_N; i++) { tlb[i].tagR = NYX_TAG_NONE; tlb[i].tagW = NYX_TAG_NONE; tlb[i].base = nullptr; }
  fcVpn = NYX_TAG_NONE; fcBase = nullptr;
}

inline void Proc::setFault(uint32_t code, uint32_t addr) {
  if (status == PS_FAULT) return;                       // fault pertama yang dicatat
  if (code == NYX_FAULT_BADADDR && stackLo >= NYX_PAGE_SIZE && addr < stackLo && addr + 4 * NYX_PAGE_SIZE >= stackLo)
    code = NYX_FAULT_STACK;
  faultCode = code; faultAddr = addr; status = PS_FAULT; nFault++;
}

// bebaskan satu frame milik proses (tulis ke swap bila kotor). false = tak bisa dibuang.
static inline bool nyxEvictFrame(Mem* m, uint32_t f) {
  Proc* p = m->fi[f].owner; uint32_t vpn = m->fi[f].vpn;
  Pte& e = p->pt[vpn];
  // invalidasi TLB & cache fetch DULU
  TlbEnt& t = p->tlb[vpn & (NYX_TLB_N - 1)];
  if (t.tagR == vpn || t.tagW == vpn) { t.tagR = NYX_TAG_NONE; t.tagW = NYX_TAG_NONE; t.base = nullptr; }
  if (p->fcVpn == vpn) { p->fcVpn = NYX_TAG_NONE; p->fcBase = nullptr; }
  if (e.flags & PF_DIRTY) {
    uint32_t slot;
    if (pteKind(e) == K_SWAP) slot = e.aux;
    else {
      int s = m->allocSlot();
      if (s < 0) { m->statNoSwap++; return false; }
      slot = (uint32_t)s;
    }
    if (!m->host.swapWrite(m->host.ctx, slot, m->frameMem(f))) {
      if (pteKind(e) != K_SWAP) m->freeSlot(slot);
      return false;
    }
    e.aux = slot; pteSetKind(e, K_SWAP); e.flags &= (uint16_t)~PF_DIRTY;
    p->nSwapOut++; m->statSwapOut++;
  }
  e.flags &= (uint16_t)~PF_RES; e.frame = 0;
  if (p->rss) p->rss--;
  m->fi[f].used = 0; m->fi[f].owner = nullptr;
  m->statEvict++;
  return true;
}

inline int Mem::evictOne(Proc* filter) {
  uint32_t n = nFrames;
  for (uint32_t step = 0; step < 3 * n; step++) {
    uint32_t f = clockHand; clockHand++; if (clockHand >= n) clockHand = 1;
    if (f == 0 || !fi[f].used) continue;
    Proc* o = fi[f].owner;
    if (!o || (filter && o != filter)) continue;
    if (fi[f].ref) {                                     // kesempatan kedua + paksa isi ulang TLB
      fi[f].ref = 0;
      uint32_t vpn = fi[f].vpn; TlbEnt& t = o->tlb[vpn & (NYX_TLB_N - 1)];
      if (t.tagR == vpn || t.tagW == vpn) { t.tagR = NYX_TAG_NONE; t.tagW = NYX_TAG_NONE; t.base = nullptr; }
      if (o->fcVpn == vpn) { o->fcVpn = NYX_TAG_NONE; o->fcBase = nullptr; }
      continue;
    }
    if (nyxEvictFrame(this, f)) return (int)f;
  }
  return -1;
}

inline int Mem::allocFrame(Proc* p) {
  int f;
  if (p->rss >= p->rssMax) { f = evictOne(p); if (f < 0) return -1; }   // kena quota: korban milik sendiri
  else if (freeCount > 0) f = freeStack[--freeCount];
  else { f = evictOne(nullptr); if (f < 0) return -1; }
  return f;
}

// ---------------------- page-in / TLB fill ----------------------------
inline uint8_t* Proc::pageIn(uint32_t vpn) {
  Pte& e = pt[vpn];
  int f = mem->allocFrame(this);
  if (f < 0) { setFault(NYX_FAULT_OOM, vpn << NYX_PAGE_SHIFT); return nullptr; }
  uint8_t* dst = mem->frameMem((uint32_t)f);
  bool ok = true;
  switch (pteKind(e)) {
    case K_ZERO: memset(dst, 0, NYX_PAGE_SIZE); break;
    case K_IMG:  ok = mem->host.pageRead && mem->host.pageRead(mem->host.ctx, 0, e.aux, dst); break;
    case K_SNAP: ok = mem->host.pageRead && mem->host.pageRead(mem->host.ctx, 1, e.aux, dst); break;
    default:     ok = mem->host.swapRead && mem->host.swapRead(mem->host.ctx, e.aux, dst);
                 if (ok) { nSwapIn++; mem->statSwapIn++; }
                 break;
  }
  if (!ok) {                                            // kembalikan frame
    mem->fi[f].used = 0; mem->fi[f].owner = nullptr; mem->freeStack[mem->freeCount++] = (uint16_t)f;
    setFault(NYX_FAULT_IO, vpn << NYX_PAGE_SHIFT); return nullptr;
  }
  mem->fi[f].owner = this; mem->fi[f].vpn = (uint16_t)vpn; mem->fi[f].ref = 1; mem->fi[f].used = 1;
  e.frame = (uint16_t)f; e.flags |= PF_RES; rss++; nPageIn++;
  if (pteKind(e) == K_ZERO) e.flags |= PF_DIRTY;       // halaman nol yang dimaterialkan = sudah beda dari "nol"
  return dst;
}

inline uint8_t* Proc::rslow(uint32_t a) {
  uint32_t vpn = a >> NYX_PAGE_SHIFT;
  if (vpn >= NYX_NPAGES) { setFault(NYX_FAULT_BADADDR, a); return nullptr; }
  Pte& e = pt[vpn];
  if (!(e.flags & PF_MAPPED)) { setFault(NYX_FAULT_BADADDR, a); return nullptr; }
  if (!(e.flags & PF_R))      { setFault(NYX_FAULT_PROT, a); return nullptr; }
  nTlbMiss++;
  TlbEnt& t = tlb[vpn & (NYX_TLB_N - 1)];
  uint8_t* base;
  if (e.flags & PF_RES) {
    base = mem->frameMem(e.frame); mem->fi[e.frame].ref = 1;
    t.tagW = ((e.flags & PF_W) && (e.flags & PF_DIRTY)) ? vpn : NYX_TAG_NONE;
  } else if (pteKind(e) == K_ZERO) {
    base = mem->zeroPage(); t.tagW = NYX_TAG_NONE;       // baca halaman nol: tanpa frame
  } else {
    base = pageIn(vpn); if (!base) return nullptr;
    t.tagW = ((e.flags & PF_W) && (e.flags & PF_DIRTY)) ? vpn : NYX_TAG_NONE;
  }
  t.tagR = vpn; t.base = base;
  return base + (a & (NYX_PAGE_SIZE - 1));
}

inline uint8_t* Proc::wslow(uint32_t a) {
  uint32_t vpn = a >> NYX_PAGE_SHIFT;
  if (vpn >= NYX_NPAGES) { setFault(NYX_FAULT_BADADDR, a); return nullptr; }
  Pte& e = pt[vpn];
  if (!(e.flags & PF_MAPPED)) { setFault(NYX_FAULT_BADADDR, a); return nullptr; }
  if (!(e.flags & PF_W))      { setFault(NYX_FAULT_PROT, a); return nullptr; }
  nTlbMiss++;
  uint8_t* base;
  if (e.flags & PF_RES) { base = mem->frameMem(e.frame); mem->fi[e.frame].ref = 1; }
  else { base = pageIn(vpn); if (!base) return nullptr; }   // ZERO: frame nol baru (copy-on-write)
  e.flags |= PF_DIRTY;                                      // sekarang halaman ini berbeda dari backing
  TlbEnt& t = tlb[vpn & (NYX_TLB_N - 1)];
  t.tagR = vpn; t.tagW = vpn; t.base = base;
  return base + (a & (NYX_PAGE_SIZE - 1));
}

inline bool Proc::fetchSlow(uint32_t pcv, uint32_t* ins) {
  uint32_t vpn = pcv >> NYX_PAGE_SHIFT;
  if (vpn >= NYX_NPAGES) { setFault(NYX_FAULT_BADADDR, pcv); return false; }
  Pte& e = pt[vpn];
  if (!(e.flags & PF_MAPPED)) { setFault(NYX_FAULT_BADADDR, pcv); return false; }
  if (!(e.flags & PF_X))      { setFault(NYX_FAULT_PROT, pcv); return false; }
  uint8_t* base;
  if (e.flags & PF_RES) { base = mem->frameMem(e.frame); mem->fi[e.frame].ref = 1; }
  else { base = pageIn(vpn); if (!base) return false; }
  fcVpn = vpn; fcBase = base; nTlbMiss++;
  memcpy(ins, base + (pcv & (NYX_PAGE_SIZE - 1)), 4);
  return true;
}

// ----------------------- akses blok (syscall) -------------------------
inline bool Proc::readBytes(uint32_t va, void* dst, uint32_t n) {
  uint8_t* d = (uint8_t*)dst;
  while (n) {
    uint32_t off = va & (NYX_PAGE_SIZE - 1), chunk = NYX_PAGE_SIZE - off; if (chunk > n) chunk = n;
    uint8_t* s = rptr(va); if (!s) return false;
    memcpy(d, s, chunk); d += chunk; va += chunk; n -= chunk;
  }
  return true;
}
inline bool Proc::writeBytes(uint32_t va, const void* src, uint32_t n) {
  const uint8_t* s = (const uint8_t*)src;
  while (n) {
    uint32_t off = va & (NYX_PAGE_SIZE - 1), chunk = NYX_PAGE_SIZE - off; if (chunk > n) chunk = n;
    uint8_t* d = wptr(va); if (!d) return false;
    memcpy(d, s, chunk); s += chunk; va += chunk; n -= chunk;
  }
  return true;
}
inline int Proc::readCStr(uint32_t va, char* dst, uint32_t cap) {
  if (cap == 0) return -1;
  for (uint32_t i = 0; i + 1 < cap; i++) {
    uint8_t* s = rptr(va + i); if (!s) return -1;
    dst[i] = (char)*s; if (!*s) return (int)i;
  }
  dst[cap - 1] = 0; return -1;                           // terlalu panjang
}
inline bool Proc::rd32(uint32_t va, uint32_t* v) { return readBytes(va, v, 4); }

// ------------------------- pemetaan & heap ----------------------------
inline void Proc::mapRange(uint32_t vpn0, uint32_t n, uint16_t perm, uint16_t kind, uint32_t aux0, bool auxInc) {
  for (uint32_t i = 0; i < n; i++) {
    Pte& e = pt[vpn0 + i];
    e.flags = (uint16_t)(PF_MAPPED | perm | (kind << PF_KIND_SHIFT)); e.frame = 0;
    e.aux = auxInc ? aux0 + i : aux0;
  }
}

inline bool Proc::sbrk(int32_t delta, uint32_t* oldBrk) {
  if (oldBrk) *oldBrk = brk;
  if (delta == 0) return true;
  int64_t nb = (int64_t)brk + delta;
  if (nb < (int64_t)brkMin || nb > (int64_t)brkMax) return false;
  uint32_t oldTop = (brk + NYX_PAGE_SIZE - 1) >> NYX_PAGE_SHIFT;
  uint32_t newTop = ((uint32_t)nb + NYX_PAGE_SIZE - 1) >> NYX_PAGE_SHIFT;
  if (newTop > oldTop) mapRange(oldTop, newTop - oldTop, PF_R | PF_W, K_ZERO, 0, false);
  else if (newTop < oldTop) {
    for (uint32_t v = newTop; v < oldTop; v++) {
      Pte& e = pt[v];
      if (e.flags & PF_RES) {                            // buang frame tanpa tulis swap
        TlbEnt& t = tlb[v & (NYX_TLB_N - 1)];
        if (t.tagR == v || t.tagW == v) { t.tagR = NYX_TAG_NONE; t.tagW = NYX_TAG_NONE; t.base = nullptr; }
        mem->fi[e.frame].used = 0; mem->fi[e.frame].owner = nullptr; mem->freeStack[mem->freeCount++] = e.frame;
        if (rss) rss--;
      }
      if (pteKind(e) == K_SWAP) mem->freeSlot(e.aux);
      e.flags = 0; e.frame = 0; e.aux = 0;
    }
  }
  brk = (uint32_t)nb;
  return true;
}

inline void Proc::release() {
  for (uint32_t v = 0; v < NYX_NPAGES; v++) {
    Pte& e = pt[v];
    if (e.flags & PF_RES) {
      mem->fi[e.frame].used = 0; mem->fi[e.frame].owner = nullptr; mem->freeStack[mem->freeCount++] = e.frame;
    }
    if (pteKind(e) == K_SWAP && (e.flags & PF_MAPPED)) mem->freeSlot(e.aux);
    e.flags = 0; e.frame = 0; e.aux = 0;
  }
  rss = 0; flushTlb();
}

// ----------------------------- loader ---------------------------------
inline int Proc::loadImage() {
  if (!mem->host.pageRead) return NYX_FAULT_IO;
  if (!mem->host.pageRead(mem->host.ctx, 0, 0, mem->scratch)) return NYX_FAULT_IO;
  ImgHeader h; memcpy(&h, mem->scratch, sizeof(h));
  if (h.magic != NYX_IMG_MAGIC || h.version != NYX_IMG_VERSION) return NYX_FAULT_BADIMG;
  if (crc32(&h, sizeof(h) - 4) != h.crc) return NYX_FAULT_BADIMG;
  // validasi bagian: [0]=code [1]=rodata [2]=data [3]=bss
  uint32_t lastEnd = NYX_CODE_BASE;
  for (int i = 0; i < 4; i++) {
    if (h.secPages[i] == 0) continue;
    if (h.secVa[i] & (NYX_PAGE_SIZE - 1)) return NYX_FAULT_BADIMG;
    if (h.secVa[i] < lastEnd) return NYX_FAULT_BADIMG;            // urut & tidak tumpang tindih
    if (h.secPages[i] > NYX_NPAGES) return NYX_FAULT_BADIMG;
    uint64_t end = (uint64_t)h.secVa[i] + (uint64_t)h.secPages[i] * NYX_PAGE_SIZE;
    if (end > NYX_HEAP_LIMIT) return NYX_FAULT_BADIMG;
    lastEnd = (uint32_t)end;
    if (i < 3) {                                                   // bagian ber-file
      if (h.secFpage[i] < 1 || (uint64_t)h.secFpage[i] + h.secPages[i] > h.imgPages) return NYX_FAULT_BADIMG;
    }
  }
  if (h.secPages[0] == 0) return NYX_FAULT_BADIMG;
  if (h.secVa[0] != NYX_CODE_BASE) return NYX_FAULT_BADIMG;
  if (h.entry & 3) return NYX_FAULT_BADIMG;
  if (h.entry < h.secVa[0] || h.entry >= h.secVa[0] + h.secPages[0] * NYX_PAGE_SIZE) return NYX_FAULT_BADIMG;
  if (h.stackKb == 0 || h.stackKb > 512) return NYX_FAULT_BADIMG;
  if (h.heapVa & (NYX_PAGE_SIZE - 1)) return NYX_FAULT_BADIMG;
  if (h.heapVa < lastEnd) return NYX_FAULT_BADIMG;
  uint32_t heapMax = (uint32_t)h.heapKb * 1024u;
  if ((uint64_t)h.heapVa + heapMax > NYX_HEAP_LIMIT) return NYX_FAULT_BADIMG;
  if (h.rssKb < 32) return NYX_FAULT_BADIMG;

  memset(pt, 0, sizeof(Pte) * NYX_NPAGES);
  mapRange(h.secVa[0] >> NYX_PAGE_SHIFT, h.secPages[0], PF_R | PF_X, K_IMG, h.secFpage[0], true);
  if (h.secPages[1]) mapRange(h.secVa[1] >> NYX_PAGE_SHIFT, h.secPages[1], PF_R, K_IMG, h.secFpage[1], true);
  if (h.secPages[2]) mapRange(h.secVa[2] >> NYX_PAGE_SHIFT, h.secPages[2], PF_R | PF_W, K_IMG, h.secFpage[2], true);
  if (h.secPages[3]) mapRange(h.secVa[3] >> NYX_PAGE_SHIFT, h.secPages[3], PF_R | PF_W, K_ZERO, 0, false);
  uint32_t stackBytes = (uint32_t)h.stackKb * 1024u;
  stackHi = NYX_STACK_TOP; stackLo = NYX_STACK_TOP - stackBytes;
  mapRange(stackLo >> NYX_PAGE_SHIFT, stackBytes >> NYX_PAGE_SHIFT, PF_R | PF_W, K_ZERO, 0, false);

  brkMin = brk = h.heapVa; brkMax = h.heapVa + heapMax;
  rssMax = ((uint32_t)h.rssKb * 1024u) >> NYX_PAGE_SHIFT;
  perms = h.perms; flags = h.flags; entry = h.entry;
  imgCrc = h.crc; imgPages = h.imgPages;
  memset(r, 0, sizeof(r)); r[NYX_REG_SP] = stackHi; pc = entry;
  rss = 0; flushTlb(); status = PS_READY;
  return 0;
}

// ------------------------ hibernate / resume --------------------------
inline bool Proc::readPageContent(uint32_t vpn, uint8_t* dst) {
  Pte& e = pt[vpn];
  if (e.flags & PF_RES) { memcpy(dst, mem->frameMem(e.frame), NYX_PAGE_SIZE); return true; }
  switch (pteKind(e)) {
    case K_ZERO: memset(dst, 0, NYX_PAGE_SIZE); return true;
    case K_IMG:  return mem->host.pageRead && mem->host.pageRead(mem->host.ctx, 0, e.aux, dst);
    case K_SNAP: return mem->host.pageRead && mem->host.pageRead(mem->host.ctx, 1, e.aux, dst);
    default:     return mem->host.swapRead && mem->host.swapRead(mem->host.ctx, e.aux, dst);
  }
}

// halaman perlu disimpan di snapshot?  (selain ZERO-bersih dan IMG-bersih)
static inline bool nyxNeedStore(const Pte& e) {
  if (!(e.flags & PF_MAPPED)) return false;
  if (e.flags & PF_DIRTY) return true;
  uint16_t k = pteKind(e);
  return k == K_SNAP || k == K_SWAP;
}

inline bool Proc::hibernate() {
  if (status != PS_READY || !mem->host.snapWrite) return false;
  uint8_t* buf = mem->scratch; Host& h = mem->host;
  uint32_t k = 0;
  // pass 1: tulis halaman data
  for (uint32_t v = 0; v < NYX_NPAGES; v++) {
    if (!nyxNeedStore(pt[v])) continue;
    if (!readPageContent(v, buf)) return false;
    if (!h.snapWrite(h.ctx, (NYX_SNAP_DATA_PAGE + k) * NYX_PAGE_SIZE, buf, NYX_PAGE_SIZE)) return false;
    k++;
  }
  // pass 2: tabel PTE bentuk pasca-resume (tanpa RES/DIRTY; kind SNAP untuk yang disimpan)
  Pte* chunk = (Pte*)buf;                               // 512 entri x 8 byte = 4096
  uint32_t idx = 0;
  for (uint32_t base = 0; base < NYX_NPAGES; base += 512) {
    for (uint32_t i = 0; i < 512; i++) {
      Pte e = pt[base + i];
      if (nyxNeedStore(e)) { e.flags = (uint16_t)((e.flags & ~(PF_RES | PF_DIRTY | PF_KIND_MASK)) | (K_SNAP << PF_KIND_SHIFT)); e.aux = NYX_SNAP_DATA_PAGE + idx; idx++; }
      else e.flags &= (uint16_t)~(PF_RES | PF_DIRTY);
      e.frame = 0;
      chunk[i] = e;
    }
    if (!h.snapWrite(h.ctx, (NYX_SNAP_PT_PAGE + base / 512) * NYX_PAGE_SIZE, buf, NYX_PAGE_SIZE)) return false;
  }
  // pass 3: header (halaman 0)
  SnapHeader sh; memset(&sh, 0, sizeof(sh));
  sh.magic = NYX_SNAP_MAGIC; sh.version = NYX_SNAP_VERSION; sh.imgCrc = imgCrc;
  memcpy(sh.r, r, sizeof(r)); sh.pc = pc;
  sh.brk = brk; sh.brkMin = brkMin; sh.brkMax = brkMax; sh.stackLo = stackLo; sh.stackHi = stackHi;
  sh.rssMax = rssMax; sh.perms = perms; sh.flags = flags; sh.nPages = k;
  sh.crc = crc32(&sh, sizeof(sh) - 4);
  memset(buf, 0, NYX_PAGE_SIZE); memcpy(buf, &sh, sizeof(sh));
  return h.snapWrite(h.ctx, 0, buf, NYX_PAGE_SIZE);
}

// Panggil SETELAH loadImage() (yang menyiapkan izin/layout dari header image).
// Snapshot hanya boleh mengubah isi halaman, register, dan brk -- bukan izin.
inline int Proc::resume() {
  Host& h = mem->host; if (!h.pageRead) return NYX_FAULT_IO;
  uint8_t* buf = mem->scratch;
  if (!h.pageRead(h.ctx, 1, 0, buf)) return NYX_FAULT_IO;
  SnapHeader sh; memcpy(&sh, buf, sizeof(sh));
  if (sh.magic != NYX_SNAP_MAGIC || sh.version != NYX_SNAP_VERSION) return NYX_FAULT_BADIMG;
  if (crc32(&sh, sizeof(sh) - 4) != sh.crc) return NYX_FAULT_BADIMG;
  if (sh.imgCrc != imgCrc) return NYX_FAULT_BADIMG;                    // image berubah sejak hibernate
  if (sh.brk < brkMin || sh.brk > brkMax || sh.nPages > NYX_NPAGES) return NYX_FAULT_BADIMG;
  // heap: petakan sampai brk tersimpan
  uint32_t savedBrk = sh.brk, dummy;
  if (!sbrk((int32_t)(savedBrk - brk), &dummy)) return NYX_FAULT_BADIMG;
  Pte* tmp = (Pte*)buf;
  for (uint32_t base = 0; base < NYX_NPAGES; base += 512) {
    if (!h.pageRead(h.ctx, 1, NYX_SNAP_PT_PAGE + base / 512, buf)) return NYX_FAULT_IO;
    for (uint32_t i = 0; i < 512; i++) {
      Pte& live = pt[base + i]; const Pte& sv = tmp[i];
      if (!(live.flags & PF_MAPPED)) continue;                          // izin/layout hanya dari header image
      uint16_t k = (uint16_t)((sv.flags & PF_KIND_MASK) >> PF_KIND_SHIFT);
      if (k == K_SNAP) {
        if (sv.aux < NYX_SNAP_DATA_PAGE || sv.aux >= NYX_SNAP_DATA_PAGE + sh.nPages) return NYX_FAULT_BADIMG;
        pteSetKind(live, K_SNAP); live.aux = sv.aux;
      }
      // K_IMG / K_ZERO: biarkan seperti hasil loadImage()/sbrk()
      live.flags &= (uint16_t)~(PF_RES | PF_DIRTY); live.frame = 0;
    }
  }
  // k_ZERO pada halaman yang awalnya IMG (data section) tak pernah terjadi -> aman
  memcpy(r, sh.r, sizeof(r)); r[NYX_REG_ZERO] = 0; pc = sh.pc;
  rss = 0; flushTlb(); status = PS_READY; faultCode = 0;
  return 0;
}

// ============================ interpreter ============================
static inline int32_t nyxSext24(uint32_t v) { return (int32_t)(v << 8) >> 8; }

inline RunResult run(Proc& p, uint32_t budget) {
  if (p.status != PS_READY) return p.status == PS_EXITED ? RUN_EXIT : RUN_FAULT;
  uint32_t* R = p.r; uint32_t pc = p.pc; uint64_t startBudget = budget;
  RunResult rr = RUN_BUDGET;
  for (;;) {
    if (NYX_UNLIKELY(budget == 0)) { rr = RUN_BUDGET; break; }
    budget--;
    R[15] = 0;
    uint32_t ins;
    if (NYX_LIKELY((pc >> NYX_PAGE_SHIFT) == p.fcVpn)) memcpy(&ins, p.fcBase + (pc & (NYX_PAGE_SIZE - 1)), 4);
    else if (!p.fetchSlow(pc, &ins)) { rr = RUN_FAULT; break; }
    uint32_t op = ins >> 24, a = (ins >> 20) & 15, b = (ins >> 16) & 15, c = (ins >> 12) & 15;
    int32_t  simm = (int16_t)(ins & 0xFFFF); uint32_t uimm = ins & 0xFFFF;
    uint32_t npc = pc + 4;
    switch (op) {
      case OP_NOP: break;
      case OP_HALT: p.exitCode = (int32_t)R[0]; p.status = PS_EXITED; p.pc = npc; rr = RUN_EXIT; goto done;
      case OP_YIELD: p.pc = npc; rr = RUN_YIELD; goto done;
      case OP_BRK: p.setFault(NYX_FAULT_BREAK, pc); rr = RUN_FAULT; goto done;
      case OP_SYS: {
        p.pc = npc;
        int rc = p.mem->host.sys ? p.mem->host.sys(p.mem->host.ctx, &p, uimm) : -NYX_FAULT_BADSYS;
        if (p.status == PS_FAULT) { rr = RUN_FAULT; goto done; }
        if (rc < 0) { p.setFault((uint32_t)(-rc), pc); rr = RUN_FAULT; goto done; }
        if (rc == NYX_SYSRC_YIELD) { rr = RUN_YIELD; goto done; }
        if (rc == NYX_SYSRC_EXIT) { p.status = PS_EXITED; rr = RUN_EXIT; goto done; }
        break;
      }
      case OP_ADD:  R[a] = R[b] + R[c]; break;
      case OP_SUB:  R[a] = R[b] - R[c]; break;
      case OP_MUL:  R[a] = R[b] * R[c]; break;
      case OP_MULH: R[a] = (uint32_t)(((int64_t)(int32_t)R[b] * (int64_t)(int32_t)R[c]) >> 32); break;
      case OP_DIV:
        if (R[c] == 0) { p.setFault(NYX_FAULT_DIVZERO, pc); rr = RUN_FAULT; goto done; }
        if ((int32_t)R[b] == INT32_MIN && (int32_t)R[c] == -1) R[a] = R[b]; else R[a] = (uint32_t)((int32_t)R[b] / (int32_t)R[c]);
        break;
      case OP_DIVU:
        if (R[c] == 0) { p.setFault(NYX_FAULT_DIVZERO, pc); rr = RUN_FAULT; goto done; }
        R[a] = R[b] / R[c]; break;
      case OP_REM:
        if (R[c] == 0) { p.setFault(NYX_FAULT_DIVZERO, pc); rr = RUN_FAULT; goto done; }
        if ((int32_t)R[b] == INT32_MIN && (int32_t)R[c] == -1) R[a] = 0; else R[a] = (uint32_t)((int32_t)R[b] % (int32_t)R[c]);
        break;
      case OP_REMU:
        if (R[c] == 0) { p.setFault(NYX_FAULT_DIVZERO, pc); rr = RUN_FAULT; goto done; }
        R[a] = R[b] % R[c]; break;
      case OP_AND: R[a] = R[b] & R[c]; break;
      case OP_OR:  R[a] = R[b] | R[c]; break;
      case OP_XOR: R[a] = R[b] ^ R[c]; break;
      case OP_SHL: R[a] = R[b] << (R[c] & 31); break;
      case OP_SHR: R[a] = R[b] >> (R[c] & 31); break;
      case OP_SAR: R[a] = (uint32_t)((int32_t)R[b] >> (R[c] & 31)); break;
      case OP_SLT:  R[a] = (int32_t)R[b] < (int32_t)R[c]; break;
      case OP_SLTU: R[a] = R[b] < R[c]; break;
      case OP_SEQ:  R[a] = R[b] == R[c]; break;
      case OP_SNE:  R[a] = R[b] != R[c]; break;
      case OP_ADDI: R[a] = R[b] + (uint32_t)simm; break;
      case OP_ANDI: R[a] = R[b] & uimm; break;
      case OP_ORI:  R[a] = R[b] | uimm; break;
      case OP_XORI: R[a] = R[b] ^ uimm; break;
      case OP_SHLI: R[a] = R[b] << (uimm & 31); break;
      case OP_SHRI: R[a] = R[b] >> (uimm & 31); break;
      case OP_SARI: R[a] = (uint32_t)((int32_t)R[b] >> (uimm & 31)); break;
      case OP_SLTI:  R[a] = (int32_t)R[b] < simm; break;
      case OP_SLTIU: R[a] = R[b] < (uint32_t)simm; break;
      case OP_MULI:  R[a] = R[b] * (uint32_t)simm; break;
      case OP_LUI:   R[a] = uimm << 16; break;
      case OP_LDB: case OP_LDBU: {
        uint8_t* q = p.rptr(R[b] + (uint32_t)simm); if (!q) { rr = RUN_FAULT; goto done; }
        R[a] = (op == OP_LDB) ? (uint32_t)(int32_t)(int8_t)*q : (uint32_t)*q; break;
      }
      case OP_LDH: case OP_LDHU: {
        uint32_t ea = R[b] + (uint32_t)simm;
        if (ea & 1) { p.setFault(NYX_FAULT_ALIGN, ea); rr = RUN_FAULT; goto done; }
        uint8_t* q = p.rptr(ea); if (!q) { rr = RUN_FAULT; goto done; }
        uint16_t v; memcpy(&v, q, 2); R[a] = (op == OP_LDH) ? (uint32_t)(int32_t)(int16_t)v : (uint32_t)v; break;
      }
      case OP_LDW: {
        uint32_t ea = R[b] + (uint32_t)simm;
        if (ea & 3) { p.setFault(NYX_FAULT_ALIGN, ea); rr = RUN_FAULT; goto done; }
        uint8_t* q = p.rptr(ea); if (!q) { rr = RUN_FAULT; goto done; }
        memcpy(&R[a], q, 4); break;
      }
      case OP_STB: {
        uint8_t* q = p.wptr(R[b] + (uint32_t)simm); if (!q) { rr = RUN_FAULT; goto done; }
        *q = (uint8_t)R[a]; break;
      }
      case OP_STH: {
        uint32_t ea = R[b] + (uint32_t)simm;
        if (ea & 1) { p.setFault(NYX_FAULT_ALIGN, ea); rr = RUN_FAULT; goto done; }
        uint8_t* q = p.wptr(ea); if (!q) { rr = RUN_FAULT; goto done; }
        uint16_t v = (uint16_t)R[a]; memcpy(q, &v, 2); break;
      }
      case OP_STW: {
        uint32_t ea = R[b] + (uint32_t)simm;
        if (ea & 3) { p.setFault(NYX_FAULT_ALIGN, ea); rr = RUN_FAULT; goto done; }
        uint8_t* q = p.wptr(ea); if (!q) { rr = RUN_FAULT; goto done; }
        memcpy(q, &R[a], 4); break;
      }
      case OP_BEQ:  if (R[a] == R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_BNE:  if (R[a] != R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_BLT:  if ((int32_t)R[a] < (int32_t)R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_BGE:  if ((int32_t)R[a] >= (int32_t)R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_BLTU: if (R[a] < R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_BGEU: if (R[a] >= R[b]) npc = npc + (uint32_t)(simm * 4); break;
      case OP_JMP: npc = npc + (uint32_t)(nyxSext24(ins & 0xFFFFFF) * 4); break;
      case OP_JAL: R[NYX_REG_LR] = npc; npc = npc + (uint32_t)(nyxSext24(ins & 0xFFFFFF) * 4); break;
      case OP_JR:   { uint32_t t = R[a]; if (t & 3) { p.setFault(NYX_FAULT_ALIGN, t); rr = RUN_FAULT; goto done; } npc = t; break; }
      case OP_JALR: { uint32_t t = R[a]; if (t & 3) { p.setFault(NYX_FAULT_ALIGN, t); rr = RUN_FAULT; goto done; } R[NYX_REG_LR] = npc; npc = t; break; }
      default: p.setFault(NYX_FAULT_ILLEGAL, pc); rr = RUN_FAULT; goto done;
    }
    (void)c;
    pc = npc;
  }
  // keluar dari loop lewat break: budget habis atau gagal fetch
  p.pc = pc;
  if (rr == RUN_FAULT) p.faultPc = pc;
  goto fin;
done:
  if (rr == RUN_FAULT) { p.faultPc = pc; p.pc = pc; }
fin:
  R[15] = 0;
  p.nInstr += (startBudget - budget);
  return rr;
}

}  // namespace nyx
