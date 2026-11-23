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

// ---------------------------------------------------------------------
// FRAG_V1: baca body HTTP tanpa memecah heap internal.
// HTTPClient::getString() pada respons chunked (Content-Length tak ada) menumbuhkan String selangkah
// demi selangkah (realloc berkali-kali di RAM internal) -> meninggalkan banyak lubang. rpHttpBody()
// menampung dulu ke satu buffer PSRAM yang tumbuh geometris, lalu membuat String SEKALI dengan ukuran
// pas (satu alokasi). Hasil & perilaku sama dgn getString() (kosong kalau body kosong / error).
// Dipakai lewat rpHttpBody(http); hanya untuk body kecil-menengah (batas default 256 KB).
// ---------------------------------------------------------------------
class RpBodySink : public Stream {
 public:
  explicit RpBodySink(size_t maxBytes) : _max(maxBytes) {}
  ~RpBodySink() { if (_buf) free(_buf); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    if (!n) return 0;
    if (_len + n > _max) n = (_max > _len) ? (_max - _len) : 0;   // potong di batas, jangan bocor
    if (!n) return 0;
    if (_len + n + 1 > _cap) {
      size_t nc = _cap ? _cap : 2048;
      while (nc < _len + n + 1) nc *= 2;
      uint8_t* nb = (uint8_t*)heap_caps_realloc(_buf, nc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!nb) nb = (uint8_t*)realloc(_buf, nc);                   // PSRAM penuh: heap biasa
      if (!nb) return 0;
      _buf = nb; _cap = nc;
    }
    memcpy(_buf + _len, b, n); _len += n; _buf[_len] = 0;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  // String dgn satu alokasi pas (reserve sekali, lalu salin)
  String toString() {
    String s;
    if (_buf && _len && s.reserve(_len)) s.concat((const char*)_buf, (unsigned int)_len);
    return s;
  }
 private:
  uint8_t* _buf = nullptr; size_t _len = 0, _cap = 0, _max;
};

static inline String rpHttpBody(HTTPClient& http, size_t maxBytes = 256 * 1024) {
  RpBodySink sink(maxBytes);
  http.writeToStream(&sink);
  return sink.toString();
}
