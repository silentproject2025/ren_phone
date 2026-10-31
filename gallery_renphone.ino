// =====================================================================
// gallery_renphone.ino -- app GALERI (SCR_GALLERY) + THUMBNAIL CACHE di SD
// =====================================================================
// Cara kerja singkat:
//  * ALBUM  = folder di SD Card yang isinya ada foto/video (JPG/JPEG, PNG, BMP,
//             MJPEG). Dipindai otomatis sampai kedalaman 2 folder dari root,
//             jadi /screenshot, /apod_cache, dst muncul sebagai album sendiri
//             persis seperti galeri HP (folder Screenshot isinya screenshot).
//  * GRID   = isi satu album, thumbnail persegi 72x72 (crop tengah).
//  * VIEWER = foto layar penuh (usap kiri/kanan = foto sebelum/berikutnya,
//             ketuk = tampil/sembunyi tombol, Hapus = ketuk 2x).
//             Video (.mjpeg) langsung diputar pakai pemutar MJPEG yang ada.
//
// THUMBNAIL CACHE (inti permintaan: CPU tidak decode gambar penuh tiap kali):
//  * Pertama kali sebuah gambar muncul di grid, thumbnail 72x72 dibuat SEKALI
//    (JPEG didecode dengan skala 1/8, 1/4 atau 1/2 jadi jauh lebih ringan,
//    BMP cukup disampling per baris) lalu disimpan ke SD:
//        /.thumbs/<2 hex>/<16 hex>.th      (4 byte header + 72*72*2 byte RGB565)
//    Nama file = hash64(path + ukuran + waktu ubah), jadi kalau gambar diganti
//    thumbnail-nya otomatis dibuat ulang, dan tidak perlu index terpisah.
//  * Berikutnya cukup BACA 10 KB dari SD lalu pushImage -- tanpa decode.
//  * Gambar yang gagal didecode ditandai (file 4 byte) supaya tidak dicoba
//    ulang terus-menerus (hemat CPU).
//  * Thumbnail yang sedang tampil ditahan di PSRAM (20 slot, LRU).
//  * Pembuatan thumbnail dibatasi 1 gambar per putaran loop dan HANYA saat
//    jari tidak sedang menyentuh/scroll, jadi scroll tidak tersendat.
// ANIMASI:
//  * pindah layar di dalam Galeri (album -> grid -> foto, dan kembali) = geser
//    ala iOS (easing navEase yg sama dgn transisi antar app), foto baru ->
//    geser kiri/kanan saat usap, thumbnail fade-in begitu selesai dimuat,
//    kartu yg ditekan meredup sebagai umpan balik sentuhan.
//  * transisi memakai 2 sprite PSRAM layar penuh (snapshot lama + frame baru),
//    dirender SEKALI lalu dikirim LANGSUNG ke layar per frame (pola yg sama
//    dgn playNavTransition di phone.ino: berbasis waktu, CPU di-boost, tanpa
//    penggabungan ke canvas), jadi jauh lebih mulus.
// PEMBUKAAN CEPAT: daftar folder dibaca dgn opendir/readdir POSIX (cuma nama +
// tipe, TANPA stat per file -- stat di FAT itu cari entri linear, jadi
// O(n^2) di folder besar) dan dipindai bertahap di latar (maks ~12 ms per
// putaran loop). Daftar album terakhir ditampilkan seketika, dipindai ulang
// pelan-pelan di belakang. Ukuran/waktu-ubah baru di-stat utk file yg tampil.
// Konsekuensi: urutan foto = nama menurun (screenshot berstempel waktu jadi
// terbaru dulu), bukan waktu ubah.
// Semua buffer besar ada di PSRAM dan dilepas saat app ditutup.
//
// Catatan arduino-cli: file ini sengaja TIDAK punya fungsi yang memakai
// struct buatan sendiri sebagai parameter/return (pelajaran bug prototype
// otomatis di README) -- semua parameter fungsi bertipe primitif/pointer.
// =====================================================================

#include <dirent.h>
#include <sys/stat.h>

#define GAL_MP           "/sdcard"       // mount point SD_MMC (SD_MMC.begin("/sdcard"...))
#define GAL_SCAN_QCAP    40
#define GAL_THUMB        72
#define GAL_THUMB_PX     (GAL_THUMB * GAL_THUMB)
#define GAL_THUMB_BYTES  (GAL_THUMB_PX * 2)
#define GAL_GAP          4
#define GAL_CACHE_DIR    "/.thumbs"
#define GAL_MAX_ITEMS    1000
#define GAL_MAX_ALBUMS   24
#define GAL_SLOTS        20
#define GAL_MAX_WANTS    16
#define GAL_PATH_MAX     112

#define GAL_ANIM_MS      240
#define GAL_FADE_MS      220

#define GM_ALBUMS 0
#define GM_GRID   1
#define GM_VIEW   2

struct GalItem  { char name[44]; uint32_t size; uint32_t mtime; uint8_t kind; uint8_t st; };   // st=1: size/mtime sudah di-stat (malas, hanya utk yg tampil)   // kind: 0=JPG 1=BMP 2=video MJPEG
struct GalAlbum { char path[64]; char title[24]; char cover[44]; uint32_t cSize; uint32_t cMtime; uint8_t cKind; uint8_t cSt; uint16_t photos; uint16_t videos; };
struct GalWant  { char path[GAL_PATH_MAX]; uint32_t size; uint32_t mtime; uint8_t kind; uint32_t tag; uint64_t key; };

// ---- memori PSRAM (satu blok: slot thumbnail + daftar item + antrean) ----
static uint8_t*  galMem   = nullptr;
static uint16_t* galSlots = nullptr;
static GalItem*  galItems = nullptr;
static GalWant*  galWants = nullptr;
static int       galWantCnt = 0, galWantCursor = 0;
static uint32_t  galSlotTag[GAL_SLOTS];
static uint32_t  galSlotUse[GAL_SLOTS];
static uint8_t   galSlotState[GAL_SLOTS];      // 0 kosong, 1 siap, 2 gagal didecode
static uint32_t  galUseTick = 0;
static unsigned long galSlotBorn[GAL_SLOTS];   // kapan thumbnail siap (utk fade-in)

// ---- data album / item ----
static GalAlbum  galAlbums[GAL_MAX_ALBUMS];
static int       galAlbumCount = 0;
static bool      galAlbumsDirty = true;
static int       galItemCount = 0;
static bool      galItemsTrunc = false;
static char      galCurDir[64] = "/";
static char      galCurTitle[24] = "";

// ---- state UI ----
static uint8_t   galMode = GM_ALBUMS;
static bool      galDirectOpen = false;        // dibuka langsung dari File Explorer (Kembali -> keluar app)
static char      galPendingPath[GAL_PATH_MAX] = "";
static float     galScrollY = 0, galScrollVel = 0, galAlbumScrollSave = 0;
static bool      galPtrDown = false, galPtrDrag = false;
static int       galPtrSx = 0, galPtrSy = 0, galPtrLx = 0, galPtrLy = 0;
static unsigned long galLastTickMs = 0;
static unsigned long galClearArmUntil = 0, galDelArmUntil = 0;

// ---- animasi ----
static LGFX_Sprite galSnap(&display);          // snapshot layar LAMA
static LGFX_Sprite galNew(&display);           // frame BARU (dirender sekali per transisi)
static bool      galSnapReady = false, galNewValid = false, galFadeActive = false;
static int       galAnimKind = 0;              // 0 maju (geser dr kanan), 1 mundur, 2 foto berikutnya, 3 foto sebelumnya
static int       galPressIdx = -1;             // kartu yg sedang ditekan (umpan balik sentuhan)

// geometri grid (dihitung ulang tiap gambar/sentuh supaya ikut orientasi)
static int galCols = 4, galX0 = 8, galTop = 42, galBot = 200, galRowH = GAL_THUMB + GAL_GAP;

// ---- viewer ----
static LGFX_Sprite galView(&display);
// ---- geser foto mengikuti jari: foto tetangga (prefetch) + zoom dari/ke thumbnail ----
static LGFX_Sprite galPrev(&display), galNext(&display);       // foto sebelum/sesudah (1 layar, PSRAM)
static LGFX_Sprite* galDecDst = &galView;                       // sprite tujuan decoder viewer
static int       galDecW = 0, galDecH = 0;                      // dimensi asli hasil decode terakhir
static int       galAdjIdx[3] = { -1, -1, -1 };                 // [1]=prev [2]=next: indeks item yg sudah dicoba
static bool      galAdjReady[3] = { false, false, false };
static int       galAdjW[3] = { 0, 0, 0 }, galAdjH[3] = { 0, 0, 0 };
static bool      galAdjDisabled = false;                        // PSRAM kurang -> pakai geser lama
static float     galSwipeDx = 0, galSwipeVel = 0;
static bool      galSwipeOn = false;
static int       galSwipeLastX = 0;
static unsigned long galSwipeLastMs = 0, galLastTouchMs = 0;
static int       galZoomOx = 0, galZoomOy = 0;                  // asal zoom buka / tujuan zoom tutup (pojok thumbnail)
// ketuk 2x = zoom (geser = pan, ketuk 2x lagi = kembali); geser ke bawah = tutup foto
static float     galZ = 1.0f, galPx = 0, galPy = 0;             // zoom foto & pergeseran pusat (px layar)
static bool      galVDragOn = false;
static float     galVDy = 0, galVVel = 0;
static int       galVLastY = 0;
static unsigned long galVLastMs = 0, galTapMs = 0;
static int       galTapX = 0, galTapY = 0;
static bool      galOvBeforeTap = true;
#define GAL_ZOOM_MAXF     2.5f
#define GAL_ZOOM_MS       280
#define GAL_ZOOM_CLOSE_MS 240
static bool      galViewReady = false, galViewPending = false, galViewOverlay = true, galViewFail = false;
static int       galViewIdx = -1, galViewW = 0, galViewH = 0;

// ---- state decode (callback JPEGDEC tidak bisa bawa parameter) ----
static PsramJpeg galJpeg;                      // objek JPEGDEC lazy di PSRAM (lihat PsramJpeg di phone.ino)
static uint16_t* galGenBuf = nullptr;
static int       galGenOx = 0, galGenOy = 0, galGenCs = 1;
static int       galVwDw = 1, galVwDh = 1, galVwTw = 1, galVwTh = 1, galVwOx = 0, galVwOy = 0;

// =====================================================================
// UTIL
// =====================================================================
static bool galHit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

static int galStrCmpI(const char* a, const char* b) {
  while (*a && *b) {
    int ca = (unsigned char)*a, cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return ca - cb;
    a++; b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

// 0 = JPG/JPEG/PNG (v118: formatnya dicek dari ISI file, bukan ekstensi -- cache APOD
// menyimpan PNG pun dengan nama .jpg), 1 = BMP, 2 = video MJPEG, -1 = bukan media
static int galKindOf(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return -1;
  if (galStrCmpI(dot, ".jpg") == 0 || galStrCmpI(dot, ".jpeg") == 0 || galStrCmpI(dot, ".png") == 0) return 0;
  if (galStrCmpI(dot, ".bmp") == 0) return 1;
  if (galStrCmpI(dot, ".mjpeg") == 0) return 2;
  return -1;
}

static uint32_t galRd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void galJoin(char* out, int cap, const char* dir, const char* name) {
  if (strcmp(dir, "/") == 0) snprintf(out, cap, "/%s", name);
  else snprintf(out, cap, "%s/%s", dir, name);
}

static void galTitleFromPath(char* out, int cap, const char* path) {
  if (strcmp(path, "/") == 0) { strncpy(out, "SD Card", cap - 1); out[cap - 1] = 0; return; }
  const char* sl = strrchr(path, '/');
  const char* nm = sl ? sl + 1 : path;
  strncpy(out, nm, cap - 1); out[cap - 1] = 0;
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

static bool galMemEnsure() {
  if (galMem) return true;
  size_t slotsB = (size_t)GAL_SLOTS * GAL_THUMB_BYTES;
  size_t itemsB = (size_t)GAL_MAX_ITEMS * sizeof(GalItem);
  size_t wantsB = (size_t)GAL_MAX_WANTS * sizeof(GalWant);
  galMem = (uint8_t*)heap_caps_malloc(slotsB + itemsB + wantsB, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!galMem) return false;
  galSlots = (uint16_t*)galMem;
  galItems = (GalItem*)(galMem + slotsB);
  galWants = (GalWant*)(galMem + slotsB + itemsB);
  memset(galSlotState, 0, sizeof(galSlotState));
  memset(galSlotTag, 0, sizeof(galSlotTag));
  memset(galSlotUse, 0, sizeof(galSlotUse));
  return true;
}

static void galMemFree() {
  if (galMem) { heap_caps_free(galMem); galMem = nullptr; }
  galSlots = nullptr; galItems = nullptr; galWants = nullptr;
  galWantCnt = 0; galItemCount = 0;
  memset(galSlotState, 0, sizeof(galSlotState));
}

static void galAdjInvalidate() {
  galZ = 1.0f; galPx = 0; galPy = 0; galVDragOn = false; galVDy = 0;
  for (int i = 1; i <= 2; i++) { galAdjIdx[i] = -1; galAdjReady[i] = false; }
}
static void galAdjFree() {
  galPrev.deleteSprite(); galNext.deleteSprite();
  galAdjInvalidate(); galAdjDisabled = false; galSwipeOn = false; galSwipeDx = 0;
}
static void galViewFree() {
  galAdjFree();
  galView.deleteSprite();
  galViewReady = false; galViewPending = false; galViewFail = false;
}

// ---- animasi transisi ----
static bool galSprEnsure(LGFX_Sprite& sp) {
  if (sp.width() != SCR_W || sp.height() != SCR_H) {
    sp.deleteSprite();
    sp.setPsram(true);
    if (!sp.createSprite(SCR_W, SCR_H)) return false;
  }
  return true;
}

static void galRunTransition(int kind);       // definisi di bawah (butuh galDrawContent)

// Panggil SETELAH state baru diset tapi SEBELUM frame berikutnya digambar:
// `canvas` masih berisi frame lama = snapshot. deferred=true -> animasi baru
// jalan setelah decode foto selesai (galPointerTick), selama itu snapshot
// tetap tampil dgn tulisan "Memuat...".
static void galBeginTransition(int kind, bool deferred) {
  galSnapReady = false; galNewValid = false;
  if (!galSprEnsure(galSnap) || !galSprEnsure(galNew)) return;   // PSRAM mepet -> tanpa animasi
  canvas.pushSprite(&galSnap, 0, 0);
  galSnapReady = true; galAnimKind = kind;
  if (!deferred) galRunTransition(kind);
}

// =====================================================================
// THUMBNAIL CACHE (SD) + SLOT RAM
// =====================================================================
static uint64_t galKey64(const char* path, uint32_t size, uint32_t mtime) {
  uint64_t h = 1469598103934665603ULL;
  for (const char* p = path; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ULL; }
  uint32_t v[2] = { size, mtime };
  const uint8_t* b = (const uint8_t*)v;
  for (int i = 0; i < 8; i++) { h ^= b[i]; h *= 1099511628211ULL; }
  return h;
}

// Sharding 256 subfolder: pencarian entri di FAT itu linear, jadi satu folder
// berisi ribuan file thumbnail akan lambat dibuka. Dengan shard, tiap folder kecil.
static void galCachePath(char* out, int cap, uint64_t key) {
  snprintf(out, cap, GAL_CACHE_DIR "/%02x/%08lx%08lx.th",
           (unsigned)((key >> 56) & 0xFF),
           (unsigned long)(key >> 32), (unsigned long)(key & 0xFFFFFFFFULL));
}

static void galCacheEnsureDir(uint64_t key) {
  if (!SD_MMC.exists(GAL_CACHE_DIR)) SD_MMC.mkdir(GAL_CACHE_DIR);
  char d[24];
  snprintf(d, sizeof(d), GAL_CACHE_DIR "/%02x", (unsigned)((key >> 56) & 0xFF));
  if (!SD_MMC.exists(d)) SD_MMC.mkdir(d);
}

static uint16_t* galSlotPtr(int i) { return galSlots + (size_t)i * GAL_THUMB_PX; }

static int galSlotFind(uint32_t tag) {
  for (int i = 0; i < GAL_SLOTS; i++)
    if (galSlotState[i] != 0 && galSlotTag[i] == tag) { galSlotUse[i] = galUseTick; return i; }
  return -1;
}

static int galSlotAlloc(uint32_t tag) {
  int best = 0; uint32_t bu = 0xFFFFFFFFu;
  for (int i = 0; i < GAL_SLOTS; i++) {
    if (galSlotState[i] == 0) { best = i; break; }
    if (galSlotUse[i] < bu) { bu = galSlotUse[i]; best = i; }
  }
  galSlotTag[best] = tag; galSlotUse[best] = galUseTick; galSlotState[best] = 0; galSlotBorn[best] = 0;
  return best;
}

// 1 = thumbnail terbaca, 2 = penanda "gagal didecode", 0 = tidak ada/rusak
static int galCacheRead(const char* cpath, uint16_t* dst) {
  File f = SD_MMC.open(cpath, FILE_READ);
  if (!f) return 0;
  size_t sz = (size_t)f.size();
  uint8_t mg[4];
  int rc = 0;
  if (sz >= 4 && f.read(mg, 4) == 4 && mg[0] == 'N' && mg[1] == 'T') {
    if (mg[2] == 'H' && sz == (size_t)(4 + GAL_THUMB_BYTES)) {
      if (f.read((uint8_t*)dst, GAL_THUMB_BYTES) == (size_t)GAL_THUMB_BYTES) rc = 1;
    } else if (mg[2] == 'X') {
      rc = 2;
    }
  }
  f.close();
  return rc;
}

static void galCacheWrite(const char* cpath, uint64_t key, const uint16_t* px, bool ok) {
  galCacheEnsureDir(key);
  File w = SD_MMC.open(cpath, FILE_WRITE);
  if (!w) return;
  uint8_t mg[4] = { 'N', 'T', (uint8_t)(ok ? 'H' : 'X'), '1' };
  bool good = (w.write(mg, 4) == 4);
  if (ok && good) good = (w.write((const uint8_t*)px, GAL_THUMB_BYTES) == (size_t)GAL_THUMB_BYTES);
  w.close();
  if (!good) SD_MMC.remove(cpath);   // SD penuh -> jangan tinggalkan file separuh
}

// ---- generator thumbnail: JPEG (dan frame pertama MJPEG) ----
static int galJpgThumbCb(JPEGDRAW* d) {
  if (!galGenBuf) return 0;
  const int TS = GAL_THUMB;
  for (int r = 0; r < d->iHeight; r++) {
    int sy = d->y + r;
    if (sy < galGenOy || sy >= galGenOy + galGenCs) continue;
    int ty0 = ((sy - galGenOy) * TS) / galGenCs;
    int ty1 = ((sy - galGenOy + 1) * TS) / galGenCs;      // > ty0 hanya kalau gambar lebih kecil dari thumbnail
    if (ty1 <= ty0) ty1 = ty0 + 1;
    if (ty1 > TS) ty1 = TS;
    const uint16_t* src = d->pPixels + r * d->iWidth;
    for (int c = 0; c < d->iWidth; c++) {
      int sx = d->x + c;
      if (sx < galGenOx || sx >= galGenOx + galGenCs) continue;
      int tx0 = ((sx - galGenOx) * TS) / galGenCs;
      int tx1 = ((sx - galGenOx + 1) * TS) / galGenCs;
      if (tx1 <= tx0) tx1 = tx0 + 1;
      if (tx1 > TS) tx1 = TS;
      uint16_t px = src[c];
      for (int ty = ty0; ty < ty1; ty++) {
        uint16_t* row = galGenBuf + ty * TS;
        for (int tx = tx0; tx < tx1; tx++) row[tx] = px;
      }
    }
  }
  return 1;
}

// ---- v118: dukungan PNG (decoder bawaan LovyanGFX, helper rpPng* ada di phone.ino) ----
#define GAL_PNG_THUMB_MAX_BYTES (2u * 1024u * 1024u)   // PNG lebih besar dari ini: tanpa thumbnail (decode PNG tidak bisa diskala murah seperti JPEG), tetap bisa dibuka di viewer

static bool galIsPng(const char* path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[4] = { 0, 0, 0, 0 };
  int n = f.read(h, 4);
  f.close();
  return n == 4 && h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G';
}

// Thumbnail PNG: gambar diskala sehingga sisi terpendek = 72 px ke sprite PSRAM
// sementara, lalu bagian TENGAHNYA disalin (crop tengah, sama dgn thumbnail JPEG).
// Format piksel buffer sprite 16-bit LovyanGFX = format slot thumbnail (byte tertukar).
static bool galGenPng(const char* path, uint16_t* out) {
  size_t len = 0;
  uint8_t* b = rpPngLoadFile(path, &len, GAL_PNG_THUMB_MAX_BYTES);
  if (!b) return false;
  int iw = 0, ih = 0;
  if (!rpPngDims(b, len, &iw, &ih) || (long)iw * (long)ih > RP_PNG_MAX_PIXELS) { heap_caps_free(b); return false; }
  const int TS = GAL_THUMB;
  int mn = (iw < ih) ? iw : ih;
  float sc = (float)TS / (float)mn;
  int sw = (int)((float)iw * sc + 0.5f), sh = (int)((float)ih * sc + 0.5f);
  if (sw < TS) sw = TS;
  if (sh < TS) sh = TS;
  if ((long)sw * (long)sh * 2L > 2L * 1024L * 1024L) { heap_caps_free(b); return false; }   // panorama ekstrem
  LGFX_Sprite t(&display);
  t.setPsram(true);
  if (!t.createSprite(sw, sh)) { heap_caps_free(b); return false; }
  t.fillSprite(0x0000);
  bool ok = t.drawPng(b, (uint32_t)len, 0, 0, 0, 0, 0, 0, sc, sc);
  heap_caps_free(b);
  if (ok) {
    const uint16_t* src = (const uint16_t*)t.getBuffer();
    if (!src) ok = false;
    else {
      int ox = (sw - TS) / 2, oy = (sh - TS) / 2;
      for (int y = 0; y < TS; y++) memcpy(out + y * TS, src + (size_t)(oy + y) * (size_t)sw + ox, TS * 2);
    }
  }
  t.deleteSprite();
  return ok;
}

static bool galGenJpeg(const char* path, uint16_t* out) {
  if (galIsPng(path)) return galGenPng(path, out);   // v118
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  if (!galJpeg->open(f, galJpgThumbCb)) { f.close(); return false; }
  int iw = galJpeg->getWidth(), ih = galJpeg->getHeight();
  if (iw <= 0 || ih <= 0) { galJpeg->close(); f.close(); return false; }
  int mn = (iw < ih) ? iw : ih;
  int scale = 0, sh = 0;
  // skala terbesar yang hasil decode-nya masih >= ukuran thumbnail: decode skala
  // 1/8 itu hanya DC coefficient, jauh lebih murah dari decode penuh.
  if (mn >= GAL_THUMB * 8)      { scale = JPEG_SCALE_EIGHTH;  sh = 3; }
  else if (mn >= GAL_THUMB * 4) { scale = JPEG_SCALE_QUARTER; sh = 2; }
  else if (mn >= GAL_THUMB * 2) { scale = JPEG_SCALE_HALF;    sh = 1; }
  int dw = iw >> sh, dh = ih >> sh;
  int cs = (dw < dh) ? dw : dh;
  if (cs < 1) { galJpeg->close(); f.close(); return false; }
  galGenCs = cs; galGenOx = (dw - cs) / 2; galGenOy = (dh - cs) / 2;
  galGenBuf = out;
  galJpeg->setPixelType(RGB565_BIG_ENDIAN);
  int ok = galJpeg->decode(0, 0, scale);
  galJpeg->close();
  f.close();
  galGenBuf = nullptr;
  return ok != 0;
}

// ---- generator thumbnail: BMP 24-bit (format screenshot dari Control Center) ----
static bool galGenBmp(const char* path, uint16_t* out) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return false; }
  uint32_t off = galRd32(h + 10);
  int32_t  w   = (int32_t)galRd32(h + 18);
  int32_t  hh  = (int32_t)galRd32(h + 22);
  uint16_t bpp = (uint16_t)(h[28] | (h[29] << 8));
  uint32_t comp = galRd32(h + 30);
  bool topDown = false;
  if (hh < 0) { topDown = true; hh = -hh; }
  if (w <= 0 || hh <= 0 || w > 4096 || hh > 4096 || bpp != 24 || comp != 0) { f.close(); return false; }
  uint32_t rowSize = (((uint32_t)w * 3 + 3) / 4) * 4;
  uint8_t* row = (uint8_t*)heap_caps_malloc(rowSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!row) { f.close(); return false; }
  const int TS = GAL_THUMB;
  int cs = (w < hh) ? w : hh;
  int ox = (w - cs) / 2, oy = (hh - cs) / 2;
  bool ok = true;
  for (int ty = 0; ty < TS && ok; ty++) {
    int sy = oy + (ty * cs) / TS;
    int fr = topDown ? sy : (hh - 1 - sy);
    f.seek(off + (uint32_t)fr * rowSize);
    if (f.read(row, rowSize) != rowSize) { ok = false; break; }
    for (int tx = 0; tx < TS; tx++) {
      int sx = ox + (tx * cs) / TS;
      const uint8_t* p = row + sx * 3;
      uint16_t v = (uint16_t)(((p[2] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[0] >> 3));
      out[ty * TS + tx] = (uint16_t)((v << 8) | (v >> 8));      // format sprite LovyanGFX (byte tertukar)
    }
  }
  heap_caps_free(row);
  f.close();
  return ok;
}

static bool galGenerate(const char* path, uint8_t kind, uint16_t* out) {
  memset(out, 0, GAL_THUMB_BYTES);
  if (kind == 1) return galGenBmp(path, out);
  return galGenJpeg(path, out);
}

// Dipanggil saat menggambar: cari slot RAM thumbnail; kalau belum ada, masukkan
// ke antrean pemuat. Return indeks slot (state 1 = siap, 2 = gagal) atau -1.
static int galThumbSlotFor(const char* path, uint32_t size, uint32_t mtime, uint8_t kind) {
  uint64_t key = galKey64(path, size, mtime);
  uint32_t tag = (uint32_t)(key ^ (key >> 32));
  if (tag == 0) tag = 1;
  int sl = galSlotFind(tag);
  if (sl >= 0) return sl;
  for (int i = 0; i < galWantCnt; i++) if (galWants[i].tag == tag) return -1;
  if (galWantCnt < GAL_MAX_WANTS) {
    GalWant& w = galWants[galWantCnt++];
    strncpy(w.path, path, GAL_PATH_MAX - 1); w.path[GAL_PATH_MAX - 1] = 0;
    w.size = size; w.mtime = mtime; w.kind = kind; w.tag = tag; w.key = key;
  }
  return -1;
}

// Dipanggil tiap putaran loop. allowGen=false saat jari menyentuh/scroll: hanya
// membaca cache yang sudah ada (murah); decode gambar menunggu sampai diam.
static void galLoaderStep(bool allowGen) {
  if (!galMem || galWantCnt <= 0 || !sdReady) return;
  int n = galWantCnt, loads = 0, checks = 0;
  for (int k = 0; k < n; k++) {
    int i = (galWantCursor + k) % n;
    GalWant& w = galWants[i];
    if (galSlotFind(w.tag) >= 0) continue;
    char cp[40];
    galCachePath(cp, sizeof(cp), w.key);
    bool have = SD_MMC.exists(cp);
    if (!have && !allowGen) { if (++checks >= 3) break; continue; }
    int sl = galSlotAlloc(w.tag);
    uint16_t* dst = galSlotPtr(sl);
    if (have) {
      int rc = galCacheRead(cp, dst);
      if (rc) { galSlotState[sl] = (uint8_t)rc; galSlotBorn[sl] = millis(); needRedraw = true; if (++loads >= 3) break; continue; }
    }
    if (!allowGen) { galSlotState[sl] = 0; continue; }
    bool ok = galGenerate(w.path, w.kind, dst);   // satu decode per putaran loop
    galSlotState[sl] = ok ? 1 : 2;
    galSlotBorn[sl] = millis();
    galCacheWrite(cp, w.key, dst, ok);
    needRedraw = true;
    break;
  }
  galWantCursor = (galWantCursor + 1) % (n > 0 ? n : 1);
}

// Hapus thumbnail satu file dari cache (dipanggil SEBELUM file aslinya dihapus,
// karena nama cache butuh ukuran & waktu ubah file). Aman dipanggil kapan saja.
void galForgetFile(const char* path) {
  if (!sdReady || !path) return;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return;
  uint32_t sz = (uint32_t)f.size();
  uint32_t mt = (uint32_t)f.getLastWrite();
  f.close();
  uint64_t key = galKey64(path, sz, mt);
  char cp[40];
  galCachePath(cp, sizeof(cp), key);
  if (SD_MMC.exists(cp)) SD_MMC.remove(cp);
  uint32_t tag = (uint32_t)(key ^ (key >> 32));
  if (tag == 0) tag = 1;
  for (int i = 0; i < GAL_SLOTS; i++) if (galSlotState[i] != 0 && galSlotTag[i] == tag) galSlotState[i] = 0;
  galAlbumsDirty = true;
}

// Hapus SELURUH cache thumbnail (tombol "Cache" di daftar album, ketuk 2x).
// Folder shard dikenal pasti (00..ff); tiap file dihapus lalu folder dibuka
// ulang, karena menghapus saat iterasi bisa membuat entri terlewat di FAT.
static void galClearCache() {
  if (!sdReady) return;
  for (int i = 0; i < 256; i++) {
    char d[24];
    snprintf(d, sizeof(d), GAL_CACHE_DIR "/%02x", i);
    if (!SD_MMC.exists(d)) continue;
    for (int guard = 0; guard < 4000; guard++) {
      File dir = SD_MMC.open(d);
      if (!dir || !dir.isDirectory()) { if (dir) dir.close(); break; }
      File f = dir.openNextFile();
      if (!f) { dir.close(); break; }
      String nm = String(f.name());
      f.close(); dir.close();
      int sl = nm.lastIndexOf('/');
      if (sl >= 0) nm = nm.substring(sl + 1);
      String full = String(d) + "/" + nm;
      if (!SD_MMC.remove(full.c_str())) break;
      yield();
    }
    SD_MMC.rmdir(d);
  }
  memset(galSlotState, 0, sizeof(galSlotState));
  galWantCnt = 0;
}

// =====================================================================
// PEMINDAIAN ALBUM & ISI ALBUM
// =====================================================================
// ---- pemindaian album bertahap (state machine, dijalankan dari galPointerTick) ----
static char*      galScanBuf = nullptr;        // [antrean folder GAL_SCAN_QCAP*65][album baru]
static GalAlbum*  galScanOut = nullptr;
static int        galScanOutN = 0, galScanQn = 0, galScanQi = 0, galScanDepth = 0;
static int        galScanPhotos = 0, galScanVideos = 0;
static uint8_t    galScanCKind = 0;
static char       galScanCur[64], galScanCover[44];
static DIR*       galScanDirH = nullptr;
static bool       galScanActive = false;

static bool galSkipDir(const char* nm) {       // folder bawaan OS yg bukan galeri
  return galStrCmpI(nm, "memo") == 0 || galStrCmpI(nm, "music") == 0 || galStrCmpI(nm, "update") == 0 ||
         galStrCmpI(nm, "roms") == 0 || galStrCmpI(nm, "System Volume Information") == 0;
}

static void galScanAbort() {
  if (galScanDirH) { closedir(galScanDirH); galScanDirH = nullptr; }
  if (galScanBuf) { heap_caps_free(galScanBuf); galScanBuf = nullptr; galScanOut = nullptr; }
  galScanActive = false;
}

static void galScanStart() {
  if (!sdReady) return;
  galScanAbort();
  size_t qB = ((size_t)GAL_SCAN_QCAP * 65 + 7) & ~(size_t)7;
  galScanBuf = (char*)heap_caps_malloc(qB + (size_t)GAL_MAX_ALBUMS * sizeof(GalAlbum), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!galScanBuf) return;
  galScanOut = (GalAlbum*)(galScanBuf + qB);
  galScanOutN = 0; galScanQn = 1; galScanQi = 0;
  strcpy(galScanBuf, "/"); galScanBuf[64] = 0;
  galScanActive = true;
}

static void galScanFinish() {
  // urut: Screenshot dulu, lalu abjad, "SD Card" (root) terakhir
  for (int i = 1; i < galScanOutN; i++) {
    GalAlbum tmp = galScanOut[i];
    int pi = (galStrCmpI(tmp.title, "Screenshot") == 0) ? 0 : (strcmp(tmp.path, "/") == 0 ? 2 : 1);
    int j = i - 1;
    while (j >= 0) {
      int pj = (galStrCmpI(galScanOut[j].title, "Screenshot") == 0) ? 0 : (strcmp(galScanOut[j].path, "/") == 0 ? 2 : 1);
      bool after = (pj > pi) || (pj == pi && galStrCmpI(galScanOut[j].title, tmp.title) > 0);
      if (!after) break;
      galScanOut[j + 1] = galScanOut[j];
      j--;
    }
    galScanOut[j + 1] = tmp;
  }
  for (int i = 0; i < galScanOutN; i++) galAlbums[i] = galScanOut[i];
  galAlbumCount = galScanOutN;
  galAlbumsDirty = false;
  galScanAbort();
  needRedraw = true;
}

// Jalan maksimal budgetMs per panggilan; sisanya dilanjut putaran loop berikutnya.
static void galScanStep(int budgetMs) {
  if (!galScanActive || !galScanBuf) return;
  unsigned long t0 = millis();
  while ((int)(millis() - t0) < budgetMs) {
    if (!galScanDirH) {
      if (galScanQi >= galScanQn || galScanOutN >= GAL_MAX_ALBUMS) { galScanFinish(); return; }
      char* qe = galScanBuf + galScanQi * 65;
      strncpy(galScanCur, qe, 63); galScanCur[63] = 0;
      galScanDepth = (uint8_t)qe[64];
      galScanQi++;
      char full[80];
      snprintf(full, sizeof(full), "%s%s", GAL_MP, galScanCur);
      galScanDirH = opendir(full);
      galScanPhotos = 0; galScanVideos = 0; galScanCover[0] = 0; galScanCKind = 0;
      if (!galScanDirH) continue;
    }
    struct dirent* e = readdir(galScanDirH);
    if (!e) {                                          // folder ini selesai
      closedir(galScanDirH); galScanDirH = nullptr;
      if (galScanPhotos + galScanVideos > 0 && galScanOutN < GAL_MAX_ALBUMS) {
        GalAlbum& a = galScanOut[galScanOutN++];
        strncpy(a.path, galScanCur, 63); a.path[63] = 0;
        galTitleFromPath(a.title, sizeof(a.title), galScanCur);
        strncpy(a.cover, galScanCover, 43); a.cover[43] = 0;
        a.cSize = 0; a.cMtime = 0; a.cKind = galScanCKind; a.cSt = 0;
        a.photos = (uint16_t)galScanPhotos; a.videos = (uint16_t)galScanVideos;
      }
      continue;
    }
    const char* nm = e->d_name;
    size_t l = strlen(nm);
    if (l == 0 || nm[0] == '.') continue;
    if (e->d_type == DT_DIR) {
      if (galScanDepth < 2 && galScanQn < GAL_SCAN_QCAP && !galSkipDir(nm) && strlen(galScanCur) + 1 + l < 63) {
        char child[64];
        galJoin(child, sizeof(child), galScanCur, nm);
        char* qe = galScanBuf + galScanQn * 65;
        strncpy(qe, child, 63); qe[63] = 0; qe[64] = (char)(galScanDepth + 1);
        galScanQn++;
      }
    } else if (l < 44) {
      int k = galKindOf(nm);
      if (k >= 0) {
        if (k == 2) galScanVideos++; else galScanPhotos++;
        // sampul = nama terbesar (screenshot berstempel waktu -> yg terbaru); tanpa stat
        if (galScanCover[0] == 0 || strcmp(nm, galScanCover) > 0) {
          strncpy(galScanCover, nm, 43); galScanCover[43] = 0;
          galScanCKind = (uint8_t)k;
        }
      }
    }
  }
}

static int galCmpItemDesc(const void* x, const void* y) {
  const GalItem* a = (const GalItem*)x;
  const GalItem* b = (const GalItem*)y;
  return strcmp(b->name, a->name);                     // nama menurun
}

static void galLoadItems() {
  galItemCount = 0; galItemsTrunc = false; galWantCnt = 0;
  if (!galItems || !sdReady) return;
  char full[96];
  snprintf(full, sizeof(full), "%s%s", GAL_MP, galCurDir);
  DIR* d = opendir(full);
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    if (e->d_type == DT_DIR) continue;
    const char* nm = e->d_name;
    size_t l = strlen(nm);
    if (l == 0 || l >= 44 || nm[0] == '.') continue;
    int k = galKindOf(nm);
    if (k < 0) continue;
    if (galItemCount >= GAL_MAX_ITEMS) { galItemsTrunc = true; break; }
    GalItem& it = galItems[galItemCount++];
    strncpy(it.name, nm, 43); it.name[43] = 0;
    it.size = 0; it.mtime = 0; it.kind = (uint8_t)k; it.st = 0;
  }
  closedir(d);
  qsort(galItems, galItemCount, sizeof(GalItem), galCmpItemDesc);
}

static void galItemPath(char* out, int cap, int idx) {
  galJoin(out, cap, galCurDir, galItems[idx].name);
}

// ukuran + waktu-ubah dibaca MALAS (stat) hanya utk item yg benar2 tampil
static void galStatPath(const char* path, uint32_t* size, uint32_t* mtime) {
  char full[GAL_PATH_MAX + 8];
  snprintf(full, sizeof(full), "%s%s", GAL_MP, path);
  struct stat sb;
  if (stat(full, &sb) == 0) { *size = (uint32_t)sb.st_size; *mtime = (uint32_t)sb.st_mtime; }
}

static void galItemStat(int idx) {
  if (idx < 0 || idx >= galItemCount || galItems[idx].st) return;
  char p[GAL_PATH_MAX];
  galItemPath(p, sizeof(p), idx);
  galStatPath(p, &galItems[idx].size, &galItems[idx].mtime);
  galItems[idx].st = 1;
}

static void galAlbumStat(int ai) {
  if (ai < 0 || ai >= galAlbumCount || galAlbums[ai].cSt) return;
  char p[GAL_PATH_MAX];
  galJoin(p, sizeof(p), galAlbums[ai].path, galAlbums[ai].cover);
  galStatPath(p, &galAlbums[ai].cSize, &galAlbums[ai].cMtime);
  galAlbums[ai].cSt = 1;
}

static void galOpenAlbum(int ai) {
  if (ai < 0 || ai >= galAlbumCount) return;
  galAlbumScrollSave = galScrollY;
  strncpy(galCurDir, galAlbums[ai].path, 63); galCurDir[63] = 0;
  strncpy(galCurTitle, galAlbums[ai].title, 23); galCurTitle[23] = 0;
  galLoadItems();
  galMode = GM_GRID;
  galScrollY = 0; galScrollVel = 0; galPressIdx = -1;
  galBeginTransition(0, false);
}

// =====================================================================
// VIEWER: decode foto layar penuh (gaya "contain", tanpa memperbesar)
// =====================================================================
static int galJpgViewCb(JPEGDRAW* d) {
  for (int r = 0; r < d->iHeight; r++) {
    int sy = d->y + r;
    if (sy >= galVwDh) break;
    int dy = galVwOy + (int)(((int32_t)sy * galVwTh) / galVwDh);
    const uint16_t* src = d->pPixels + r * d->iWidth;
    for (int c = 0; c < d->iWidth; c++) {
      int sx = d->x + c;
      if (sx >= galVwDw) break;
      int dx = galVwOx + (int)(((int32_t)sx * galVwTw) / galVwDw);
      uint16_t p = src[c];
      galDecDst->drawPixel(dx, dy, (uint16_t)((p >> 8) | (p << 8)));
    }
  }
  return 1;
}

// ---- v118: viewer PNG (gaya "contain", tanpa memperbesar -- sama dgn JPEG/BMP) ----
static const char* galFailWhy = nullptr;   // alasan gagal (diisi decoder PNG) utk toast; nullptr = pakai pesan umum

static bool galDecodePngView(const char* path) {
  size_t len = 0;
  uint8_t* b = rpPngLoadFile(path, &len, RP_PNG_MAX_BYTES);
  if (!b) { galFailWhy = (len > RP_PNG_MAX_BYTES) ? "PNG terlalu besar (maks 4 MB)" : "PNG gagal dibaca"; return false; }
  int iw = 0, ih = 0;
  if (!rpPngDims(b, len, &iw, &ih)) { heap_caps_free(b); galFailWhy = "File PNG rusak"; return false; }
  if ((long)iw * (long)ih > RP_PNG_MAX_PIXELS) { heap_caps_free(b); galFailWhy = "PNG terlalu besar (resolusi)"; return false; }
  float sc = rpPngFitScale(iw, ih, SCR_W, SCR_H);
  int tw = (int)((float)iw * sc + 0.5f), th = (int)((float)ih * sc + 0.5f);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  if (tw > SCR_W) tw = SCR_W;
  if (th > SCR_H) th = SCR_H;
  int ox = (SCR_W - tw) / 2, oy = (SCR_H - th) / 2;
  bool ok = galDecDst->drawPng(b, (uint32_t)len, ox, oy, 0, 0, 0, 0, sc, sc);
  heap_caps_free(b);
  if (!ok) { galFailWhy = "PNG gagal didecode"; return false; }
  galDecW = iw; galDecH = ih;
  return true;
}

static bool galDecodeJpegView(const char* path) {
  if (galIsPng(path)) return galDecodePngView(path);   // v118
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  if (!galJpeg->open(f, galJpgViewCb)) { f.close(); return false; }
  int iw = galJpeg->getWidth(), ih = galJpeg->getHeight();
  if (iw <= 0 || ih <= 0) { galJpeg->close(); f.close(); return false; }
  float rx = (float)iw / (float)SCR_W, ry = (float)ih / (float)SCR_H;
  float ratio = (rx > ry) ? rx : ry;
  int tw = iw, th = ih;
  if (ratio > 1.0f) {
    tw = (int)((float)iw / ratio + 0.5f); th = (int)((float)ih / ratio + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
  }
  int sh = 0;
  for (int k = 3; k >= 0; k--) { if ((iw >> k) >= tw && (ih >> k) >= th) { sh = k; break; } }
  int scale = (sh == 3) ? JPEG_SCALE_EIGHTH : (sh == 2) ? JPEG_SCALE_QUARTER : (sh == 1) ? JPEG_SCALE_HALF : 0;
  galVwDw = iw >> sh; galVwDh = ih >> sh;
  if (galVwDw < 1) galVwDw = 1;
  if (galVwDh < 1) galVwDh = 1;
  if (tw > galVwDw) tw = galVwDw;
  if (th > galVwDh) th = galVwDh;
  galVwTw = tw; galVwTh = th;
  galVwOx = (SCR_W - tw) / 2; galVwOy = (SCR_H - th) / 2;
  galJpeg->setPixelType(RGB565_BIG_ENDIAN);
  int ok = galJpeg->decode(0, 0, scale);
  galJpeg->close();
  f.close();
  galDecW = iw; galDecH = ih;
  return ok != 0;
}

static bool galDecodeBmpView(const char* path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return false; }
  uint32_t off = galRd32(h + 10);
  int32_t  w   = (int32_t)galRd32(h + 18);
  int32_t  hh  = (int32_t)galRd32(h + 22);
  uint16_t bpp = (uint16_t)(h[28] | (h[29] << 8));
  uint32_t comp = galRd32(h + 30);
  bool topDown = false;
  if (hh < 0) { topDown = true; hh = -hh; }
  if (w <= 0 || hh <= 0 || w > 4096 || hh > 4096 || bpp != 24 || comp != 0) { f.close(); return false; }
  uint32_t rowSize = (((uint32_t)w * 3 + 3) / 4) * 4;
  uint8_t* row = (uint8_t*)heap_caps_malloc(rowSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!row) { f.close(); return false; }
  float rx = (float)w / (float)SCR_W, ry = (float)hh / (float)SCR_H;
  float ratio = (rx > ry) ? rx : ry;
  int tw = (int)w, th = (int)hh;
  if (ratio > 1.0f) {
    tw = (int)((float)w / ratio + 0.5f); th = (int)((float)hh / ratio + 0.5f);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
  }
  int ox = (SCR_W - tw) / 2, oy = (SCR_H - th) / 2;
  bool ok = true;
  for (int dy = 0; dy < th && ok; dy++) {
    int sy = (int)(((int32_t)dy * hh) / th);
    int fr = topDown ? sy : (hh - 1 - sy);
    f.seek(off + (uint32_t)fr * rowSize);
    if (f.read(row, rowSize) != rowSize) { ok = false; break; }
    for (int dx = 0; dx < tw; dx++) {
      int sx = (int)(((int32_t)dx * w) / tw);
      const uint8_t* p = row + sx * 3;
      uint16_t v = (uint16_t)(((p[2] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[0] >> 3));
      galDecDst->drawPixel(ox + dx, oy + dy, v);
    }
  }
  heap_caps_free(row);
  f.close();
  galDecW = (int)w; galDecH = (int)hh;
  return ok;
}

static void galDecodeCurrent() {
  galViewPending = false; galViewReady = false; galViewFail = false;
  if (galViewIdx < 0 || galViewIdx >= galItemCount) { galViewFail = true; return; }
  if (galView.width() != SCR_W || galView.height() != SCR_H) {
    galView.deleteSprite();
    galView.setPsram(true);
    if (!galView.createSprite(SCR_W, SCR_H)) { galViewFail = true; showToast("PSRAM tidak cukup"); return; }
  }
  galView.fillSprite(0x0000);
  char p[GAL_PATH_MAX];
  galItemPath(p, sizeof(p), galViewIdx);
  galFailWhy = nullptr;
  bool ok = (galItems[galViewIdx].kind == 1) ? galDecodeBmpView(p) : galDecodeJpegView(p);
  if (ok) { galViewReady = true; galViewW = galDecW; galViewH = galDecH; }
  else { galViewFail = true; showToast(galFailWhy ? galFailWhy : "Gambar tidak bisa dibuka (JPEG progresif/BMP non-24bit?)", 2200); vibError(); }
}

// ---- navigasi viewer ----
static int galNextPhoto(int from, int dir) {
  int i = from + dir;
  while (i >= 0 && i < galItemCount && galItems[i].kind == 2) i += dir;   // lewati video
  return (i >= 0 && i < galItemCount) ? i : -1;
}

static float galEaseOutCubic(float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float q = 1.0f - t;
  return 1.0f - q * q * q;
}

static bool galAdjEnsure() {
  if (galAdjDisabled) return false;
  if (galPrev.width() == SCR_W && galPrev.height() == SCR_H && galNext.width() == SCR_W && galNext.height() == SCR_H) return true;
  if (!galSprEnsure(galPrev) || !galSprEnsure(galNext)) {
    galPrev.deleteSprite(); galNext.deleteSprite(); galAdjDisabled = true; return false;
  }
  galAdjInvalidate();
  return true;
}

// decode foto idx ke sprite tetangga (slot 1=prev, 2=next). Blokir ~0,1-0,4 dtk -> hanya dipanggil saat jari tidak menyentuh.
static void galDecodeSlot(int slot, int idx) {
  LGFX_Sprite& sp = (slot == 2) ? galNext : galPrev;
  galDecDst = &sp;
  sp.fillSprite(0x0000);
  char p[GAL_PATH_MAX];
  galItemPath(p, sizeof(p), idx);
  galFailWhy = nullptr;
  bool ok = (galItems[idx].kind == 1) ? galDecodeBmpView(p) : galDecodeJpegView(p);
  galDecDst = &galView;
  galAdjIdx[slot] = idx; galAdjReady[slot] = ok; galAdjW[slot] = galDecW; galAdjH[slot] = galDecH;
}

static void galPrefetchStep() {
  if (galMode != GM_VIEW || !galViewReady || galViewPending || galItemCount < 2) return;
  if (!galAdjEnsure()) return;
  for (int k = 0; k < 2; k++) {
    int dir = (k == 0) ? +1 : -1, slot = (dir > 0) ? 2 : 1;
    int want = galNextPhoto(galViewIdx, dir);
    if (want < 0) { galAdjIdx[slot] = -1; galAdjReady[slot] = false; continue; }
    if (galAdjIdx[slot] == want) continue;
    galDecodeSlot(slot, want);
    return;                                           // satu foto per putaran loop
  }
}

static bool galAdjOk(int dir) {
  int slot = (dir > 0) ? 2 : 1;
  int ni = galNextPhoto(galViewIdx, dir);
  return ni >= 0 && galAdjReady[slot] && galAdjIdx[slot] == ni && galPrev.width() == SCR_W;
}

// lukis geser langsung ke layar: foto aktif di x=off, tetangga menempel di sisinya (celah tanpa tetangga = hitam)
static void galSwipePaint(int off, int dir, bool nbOk) {
  galView.pushSprite(off, 0);
  int nx = off + (dir > 0 ? SCR_W : -SCR_W);
  if (nbOk) (dir > 0 ? galNext : galPrev).pushSprite(nx, 0);
  else if (off < 0) display.fillRect(SCR_W + off, 0, -off, SCR_H, 0x0000);
  else if (off > 0) display.fillRect(0, 0, off, SCR_H, 0x0000);
}

static void galSwipeAnimate(float from, float to, int dir) {
  bool nbOk = galAdjOk(dir);
  float dist = fabsf(to - from) / (float)SCR_W;
  if (dist < 0.3f) dist = 0.3f;
  if (dist > 1.0f) dist = 1.0f;
  unsigned long dur = (unsigned long)((float)GAL_ANIM_MS * dist);
  const unsigned long t0 = millis();
  int lastOff = 1 << 20;
  display.startWrite();
  for (;;) {
    uiBoostKick();
    unsigned long el = millis() - t0;
    bool last = el >= dur;
    float e = last ? 1.0f : galEaseOutCubic((float)el / (float)dur);
    int off = (int)lroundf(from + (to - from) * e);
    if (off != lastOff) { lastOff = off; galSwipePaint(off, dir, nbOk); }
    if (last) break;
    yield();
  }
  display.endWrite();
}

// foto tetangga jadi foto aktif; foto lama jadi tetangga di sisi sebaliknya (salin antar sprite, tanpa decode ulang)
static void galAdoptNeighbor(int dir) {
  int ns = (dir > 0) ? 2 : 1, os = (dir > 0) ? 1 : 2;
  LGFX_Sprite& nb = (dir > 0) ? galNext : galPrev;
  LGFX_Sprite& ot = (dir > 0) ? galPrev : galNext;
  int ni = galAdjIdx[ns];
  galView.pushSprite(&ot, 0, 0);
  galAdjIdx[os] = galViewIdx; galAdjReady[os] = true; galAdjW[os] = galViewW; galAdjH[os] = galViewH;
  nb.pushSprite(&galView, 0, 0);
  galViewW = galAdjW[ns]; galViewH = galAdjH[ns];
  galAdjIdx[ns] = -1; galAdjReady[ns] = false;
  galViewIdx = ni; galViewReady = true; galViewFail = false; galViewPending = false;
  galSnapReady = false; galNewValid = false; galDelArmUntil = 0; galPtrDown = false;
  galLastTouchMs = millis();
  needRedraw = true;
}

static void galViewStep(int dir) {
  int ni = galNextPhoto(galViewIdx, dir);
  if (ni < 0) return;
  if (galViewReady && !galViewPending && galAdjOk(dir)) {      // tetangga sudah siap: geser mulus tanpa "Memuat..."
    galDelArmUntil = 0;
    galSwipeAnimate(0.0f, dir > 0 ? -(float)SCR_W : (float)SCR_W, dir);
    galAdoptNeighbor(dir);
    return;
  }
  galAdjInvalidate();
  galViewIdx = ni; galViewPending = true; galDelArmUntil = 0;
  galBeginTransition(dir > 0 ? 2 : 3, true);
  needRedraw = true;
}

static void galViewDelete() {
  if (galViewIdx < 0 || galViewIdx >= galItemCount) return;
  if (millis() >= galDelArmUntil) {
    galDelArmUntil = millis() + 2500;
    showToast("Ketuk lagi untuk menghapus");
    needRedraw = true;
    return;
  }
  char p[GAL_PATH_MAX];
  galItemPath(p, sizeof(p), galViewIdx);
  galForgetFile(p);
  bool ok = SD_MMC.remove(p);
  galDelArmUntil = 0;
  if (!ok) { showToast("Gagal menghapus"); vibError(); needRedraw = true; return; }
  for (int i = galViewIdx; i < galItemCount - 1; i++) galItems[i] = galItems[i + 1];
  galItemCount--;
  galAdjInvalidate();
  galAlbumsDirty = true;
  showToast("Dihapus"); vibSuccess();
  int ni = galNextPhoto(galViewIdx - 1, +1);      // foto pertama di posisi >= galViewIdx (item sudah bergeser)
  if (ni < 0) ni = galNextPhoto(galViewIdx, -1);  // tidak ada lagi di depan -> mundur
  if (ni < 0 || galItemCount <= 0) {
    galViewFree();
    if (galDirectOpen) { navBack(); return; }
    galMode = GM_GRID;
  } else {
    galViewIdx = ni; galViewPending = true;
    galSnapReady = false;
  }
  needRedraw = true;
}

// Jadikan foto yang sedang dibuka sebagai foto profil (tombol "Profil" di bar bawah viewer)
static void galViewSetOwner() {
  if (galViewIdx < 0 || galViewIdx >= galItemCount) return;
  if (galItems[galViewIdx].kind == 2) { showToast("Video tidak bisa jadi foto profil"); return; }
  char op[GAL_PATH_MAX];
  galItemPath(op, sizeof(op), galViewIdx);
  ownerSetAvatarUi(op);
}

static void galViewTap(int x, int y) {
  if (!galViewOverlay) { galViewOverlay = true; needRedraw = true; return; }
  int cy = SCR_H / 2;
  if (galItemCount > 1) {
    if (galHit(x, y, 0, cy - 24, 36, 48))          { galViewStep(-1); return; }
    if (galHit(x, y, SCR_W - 36, cy - 24, 36, 48)) { galViewStep(+1); return; }
  }
  if (x >= SCR_W - 128 && x < SCR_W - 66 && y >= backY() - 2) { galViewSetOwner(); return; }
  if (x >= SCR_W - 64 && y >= backY() - 2) { galViewDelete(); return; }
  galViewOverlay = false; needRedraw = true;
}

// =====================================================================
// GAMBAR UI
// =====================================================================
static void galGeom() {
  galCols = (SCR_W - 8 + GAL_GAP) / (GAL_THUMB + GAL_GAP);
  if (galCols < 1) galCols = 1;
  galX0 = (SCR_W - (galCols * GAL_THUMB + (galCols - 1) * GAL_GAP)) / 2;
  galTop = 42; galBot = backY() - 4;
  galRowH = GAL_THUMB + GAL_GAP;
}

static int galMaxScroll(int n) {
  int rows = (n + galCols - 1) / galCols;
  int ch = rows * galRowH - GAL_GAP;
  int m = ch - (galBot - galTop);
  return m < 0 ? 0 : m;
}

static void galDrawThumb(LGFX_Sprite& s, int x, int y, int slot, uint8_t kind) {
  if (slot >= 0 && galSlotState[slot] == 1) {
    s.pushImage(x, y, GAL_THUMB, GAL_THUMB, (lgfx::swap565_t*)galSlotPtr(slot));
    unsigned long age = millis() - galSlotBorn[slot];
    if (galSlotBorn[slot] != 0 && age < GAL_FADE_MS) {       // fade-in: selubung warna placeholder memudar
      s.fillRectAlpha(x, y, GAL_THUMB, GAL_THUMB, (uint8_t)(255 - (255UL * age) / GAL_FADE_MS), T().surface2);
      galFadeActive = true;
    }
  } else {
    s.fillRoundRect(x, y, GAL_THUMB, GAL_THUMB, 6, T().surface2);
    int cx = x + GAL_THUMB / 2, cy = y + GAL_THUMB / 2;
    uint16_t ic = T().subtext;
    if (slot >= 0 && galSlotState[slot] == 2) {            // gagal didecode
      s.setTextColor(ic); s.setTextSize(2); s.setCursor(cx - 5, cy - 7); s.print("?"); s.setTextSize(1);
    } else if (kind == 2) {
      s.fillTriangle(cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, ic);
    } else {
      s.fillTriangle(cx - 14, cy + 10, cx - 4, cy - 6, cx + 6, cy + 10, ic);
      s.fillTriangle(cx - 2, cy + 10, cx + 6, cy - 2, cx + 14, cy + 10, ic);
      s.fillCircle(cx + 8, cy - 8, 3, ic);
    }
  }
  if (kind == 2) {                                          // lencana video
    int bx = x + 12, by = y + GAL_THUMB - 12;
    s.fillCircle(bx, by, 9, 0x0000);
    s.fillTriangle(bx - 3, by - 5, bx - 3, by + 5, bx + 5, by, 0xFFFF);
  }
}

static void galDrawAlbums(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Galeri");
  // tombol kecil "Cache" (hapus semua thumbnail, ketuk 2x)
  bool armed = millis() < galClearArmUntil;
  iosRR(s, SCR_W - 60, 23, 52, 16, 6, armed ? T().danger : T().surface2);
  s.setTextColor(armed ? 0xFFFF : T().subtext); s.setTextSize(1);
  const char* lb = armed ? "Yakin?" : "Cache";
  s.setCursor(SCR_W - 60 + 26 - s.textWidth(lb) / 2, 27); s.print(lb);

  if (galScanActive) {                                      // indikator pemindaian di samping judul
    static const char* dd[4] = { "Memindai", "Memindai.", "Memindai..", "Memindai..." };
    s.setTextColor(T().subtext); s.setTextSize(1);
    s.setCursor(66, 27); s.print(dd[(millis() / 300) % 4]);
  }
  if (galAlbumCount == 0) {
    s.setTextColor(T().subtext);
    if (galScanActive) {
      s.setCursor(16, galTop + 30); s.print("Memindai SD Card...");
    } else {
      s.setCursor(16, galTop + 30); s.print("Belum ada foto/video di SD Card.");
      s.setCursor(16, galTop + 44); s.print("Format: JPG, PNG, BMP, MJPEG.");
    }
    return;
  }
  int ms = galMaxScroll(galAlbumCount);
  if (galScrollY > ms) galScrollY = (float)ms;
  if (galScrollY < 0) galScrollY = 0;
  s.setClipRect(0, galTop, SCR_W, galBot - galTop);
  for (int i = 0; i < galAlbumCount; i++) {
    int row = i / galCols, col = i % galCols;
    int x = galX0 + col * (GAL_THUMB + GAL_GAP);
    int y = galTop + row * galRowH - (int)galScrollY;
    if (y + GAL_THUMB < galTop || y > galBot) continue;
    galAlbumStat(i);
    const GalAlbum& a = galAlbums[i];
    char cp[GAL_PATH_MAX];
    galJoin(cp, sizeof(cp), a.path, a.cover);
    int sl = galThumbSlotFor(cp, a.cSize, a.cMtime, a.cKind);
    galDrawThumb(s, x, y, sl, a.cKind);
    if (i == galPressIdx) s.fillRectAlpha(x, y, GAL_THUMB, GAL_THUMB, 90, 0x0000);
    // strip judul + jumlah di dasar kartu
    s.fillRect(x, y + GAL_THUMB - 14, GAL_THUMB, 14, 0x1082);
    char cnt[8]; snprintf(cnt, sizeof(cnt), "%d", (int)(a.photos + a.videos));
    int cw = s.textWidth(cnt);
    char tt[16]; strncpy(tt, a.title, 15); tt[15] = 0;
    int maxW = GAL_THUMB - cw - 10;
    int n = (int)strlen(tt); bool cut = false;
    while (n > 2 && s.textWidth(tt) > maxW) { tt[--n] = 0; cut = true; }
    if (cut && n >= 1) tt[n - 1] = '.';
    s.setTextColor(0xFFFF);
    s.setCursor(x + 4, y + GAL_THUMB - 11); s.print(tt);
    s.setTextColor(0xBDF7);
    s.setCursor(x + GAL_THUMB - 4 - cw, y + GAL_THUMB - 11); s.print(cnt);
  }
  s.clearClipRect();
  if (ms > 0) {
    int trackH = galBot - galTop;
    int th = trackH * trackH / (trackH + ms); if (th < 10) th = 10;
    int ty = galTop + (int)((float)(trackH - th) * galScrollY / (float)ms);
    s.fillRoundRect(SCR_W - 3, ty, 2, th, 1, T().divider);
  }
}

static void galDrawGrid(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, galCurTitle);
  char info[40];
  int photos = 0, videos = 0;
  for (int i = 0; i < galItemCount; i++) { if (galItems[i].kind == 2) videos++; else photos++; }
  if (videos > 0) snprintf(info, sizeof(info), "%d foto, %d video%s", photos, videos, galItemsTrunc ? "+" : "");
  else            snprintf(info, sizeof(info), "%d foto%s", photos, galItemsTrunc ? "+" : "");
  s.setTextColor(T().subtext); s.setTextSize(1);
  s.setCursor(SCR_W - 8 - s.textWidth(info), 27); s.print(info);

  if (galItemCount == 0) {
    s.setTextColor(T().subtext);
    s.setCursor(16, galTop + 30); s.print("Album kosong.");
    return;
  }
  int ms = galMaxScroll(galItemCount);
  if (galScrollY > ms) galScrollY = (float)ms;
  if (galScrollY < 0) galScrollY = 0;
  s.setClipRect(0, galTop, SCR_W, galBot - galTop);
  for (int i = 0; i < galItemCount; i++) {
    int row = i / galCols, col = i % galCols;
    int x = galX0 + col * (GAL_THUMB + GAL_GAP);
    int y = galTop + row * galRowH - (int)galScrollY;
    if (y + GAL_THUMB < galTop) continue;
    if (y > galBot) break;
    galItemStat(i);
    char p[GAL_PATH_MAX];
    galItemPath(p, sizeof(p), i);
    int sl = galThumbSlotFor(p, galItems[i].size, galItems[i].mtime, galItems[i].kind);
    galDrawThumb(s, x, y, sl, galItems[i].kind);
    if (i == galPressIdx) s.fillRectAlpha(x, y, GAL_THUMB, GAL_THUMB, 90, 0x0000);
  }
  s.clearClipRect();
  if (ms > 0) {
    int trackH = galBot - galTop;
    int th = trackH * trackH / (trackH + ms); if (th < 10) th = 10;
    int ty = galTop + (int)((float)(trackH - th) * galScrollY / (float)ms);
    s.fillRoundRect(SCR_W - 3, ty, 2, th, 1, T().divider);
  }
}

static void galDrawViewer(LGFX_Sprite& s) {
  if (galSwipeOn) {                                  // foto mengikuti jari (tanpa overlay)
    int off = (int)lroundf(galSwipeDx);
    int dir = (off < 0) ? +1 : -1;
    s.fillSprite(0x0000);
    galView.pushSprite(&s, off, 0);
    if (galAdjOk(dir)) (dir > 0 ? galNext : galPrev).pushSprite(&s, off + (dir > 0 ? SCR_W : -SCR_W), 0);
    return;
  }
  if (galVDragOn) {                                  // geser ke bawah utk menutup: foto turun mengikuti jari
    s.fillSprite(0x0000);
    galView.pushSprite(&s, 0, (int)lroundf(galVDy));
    return;
  }
  if (galZ > 1.001f && galViewReady) {               // sedang di-zoom
    s.fillSprite(0x0000);
    galView.pushRotateZoom(&s, SCR_W * 0.5f + galPx, SCR_H * 0.5f + galPy, 0.0f, galZ, galZ);
    return;
  }
  if (galViewPending && galSnapReady) {
    galSnap.pushSprite(&s, 0, 0);                  // layar lama tetap tampil selagi foto didecode
  } else {
    s.fillSprite(0x0000);
    if (galViewReady) galView.pushSprite(&s, 0, 0);
  }
  s.setTextSize(1);
  if (galViewFail) {
    s.setTextColor(T().subtext);
    const char* m = "Gagal memuat gambar";
    s.setCursor(SCR_W / 2 - s.textWidth(m) / 2, SCR_H / 2 - 4); s.print(m);
  }
  if (galViewPending) {
    const char* m = "Memuat...";
    int mw = s.textWidth(m);
    s.fillRoundRect(SCR_W / 2 - mw / 2 - 8, SCR_H / 2 - 12, mw + 16, 22, 8, 0x1082);
    s.setTextColor(0xFFFF);
    s.setCursor(SCR_W / 2 - mw / 2, SCR_H / 2 - 4); s.print(m);
  }
  if (galViewOverlay) {
    s.fillRect(0, 0, SCR_W, 18, 0x0841);
    char nm[48] = "";
    if (galViewIdx >= 0 && galViewIdx < galItemCount) { strncpy(nm, galItems[galViewIdx].name, 47); nm[47] = 0; }
    char idx[16]; snprintf(idx, sizeof(idx), "%d/%d", galViewIdx + 1, galItemCount);
    int iw = s.textWidth(idx);
    int maxW = SCR_W - iw - 24;
    int n = (int)strlen(nm); bool cut = false;
    while (n > 3 && s.textWidth(nm) > maxW) { nm[--n] = 0; cut = true; }
    if (cut && n >= 1) nm[n - 1] = '.';
    s.setTextColor(0xFFFF); s.setCursor(8, 5); s.print(nm);
    s.setTextColor(0xBDF7); s.setCursor(SCR_W - 8 - iw, 5); s.print(idx);
    if (galViewW > 0 && galViewReady) {
      char dm[24]; snprintf(dm, sizeof(dm), "%dx%d", galViewW, galViewH);
      s.setTextColor(0xBDF7); s.setCursor(SCR_W - 8 - s.textWidth(dm), 21); s.print(dm);
    }
    if (galItemCount > 1) {
      int cy = SCR_H / 2;
      uint16_t bg = 0x1082, fg = 0xFFFF;
      s.fillCircle(16, cy, 13, bg);
      s.drawLine(19, cy - 6, 13, cy, fg); s.drawLine(13, cy, 19, cy + 6, fg);
      s.drawLine(20, cy - 6, 14, cy, fg); s.drawLine(14, cy, 20, cy + 6, fg);
      s.fillCircle(SCR_W - 16, cy, 13, bg);
      s.drawLine(SCR_W - 19, cy - 6, SCR_W - 13, cy, fg); s.drawLine(SCR_W - 13, cy, SCR_W - 19, cy + 6, fg);
      s.drawLine(SCR_W - 20, cy - 6, SCR_W - 14, cy, fg); s.drawLine(SCR_W - 14, cy, SCR_W - 20, cy + 6, fg);
    }
    bool armed = millis() < galDelArmUntil;
    iosRR(s, SCR_W - 64, backY(), 60, BACK_H, 8, armed ? T().danger : 0x4208);
    const char* l = armed ? "Yakin?" : "Hapus";
    s.setTextColor(armed ? 0xFFFF : T().danger);
    s.setCursor(SCR_W - 64 + 30 - s.textWidth(l) / 2, backY() + 8); s.print(l);
    iosRR(s, SCR_W - 128, backY(), 60, BACK_H, 8, 0x4208);
    s.setTextColor(T().accent);
    s.setCursor(SCR_W - 128 + 30 - s.textWidth("Profil") / 2, backY() + 8); s.print("Profil");
    drawBack(s);
  }
  drawToast(s);
}

static void galDrawContent(LGFX_Sprite& s) {
  if (galMode == GM_VIEW) { galDrawViewer(s); return; }
  iosBackdrop(s); drawStatusBar(s);
  s.setTextSize(1);
  galGeom();
  galWantCnt = 0;
  if (!sdReady) {
    iosTitle(s, 8, 26, "Galeri");
    s.setTextColor(T().danger); s.setTextSize(2); s.setCursor(20, 80); s.print("SD Card Tdk Siap");
    drawBack(s); drawToast(s);
    return;
  }
  if (!galMem) {
    iosTitle(s, 8, 26, "Galeri");
    s.setTextColor(T().danger); s.setCursor(20, 80); s.print("PSRAM tidak cukup");
    drawBack(s); drawToast(s);
    return;
  }
  if (galMode == GM_ALBUMS) galDrawAlbums(s); else galDrawGrid(s);
  drawBack(s); drawToast(s);
}

// Transisi blocking, pola playNavTransition: frame baru dirender SEKALI ke
// galNew, lalu tiap langkah (berbasis waktu) sprite dikirim langsung ke layar.
// Area yg tertutup tidak digambar (clip) supaya SPI cuma kirim ~1 layar/frame.
// foto (galView, 1 layar) ke jendela [x0,y0,w,h] di display, skala "cover" terpusat di jendela
static void galPaintZoomWindow(int x0, int y0, int w, int h) {
  if (w <= 0 || h <= 0) return;
  float zx = (float)w / (float)SCR_W, zy = (float)h / (float)SCR_H;
  float z = zx > zy ? zx : zy;
  display.setClipRect(x0, y0, w, h);
  if (z >= 0.985f) galView.pushSprite(x0 + w / 2 - SCR_W / 2, y0 + h / 2 - SCR_H / 2);   // skala ~1: salin biasa (cepat)
  else galView.pushRotateZoom(&display, x0 + w * 0.5f, y0 + h * 0.5f, 0.0f, z, z);
  display.clearClipRect();
}

static void galPushBg(int x, int y, int w, int h) {   // tempel potongan grid (galNew) ke display
  if (w <= 0 || h <= 0) return;
  display.setClipRect(x, y, w, h);
  galNew.pushSprite(0, 0);
  display.clearClipRect();
}

// Buka foto: jendela tumbuh dari thumbnail ke layar penuh (rect hanya membesar -> tiap piksel ditulis sekali).
static void galZoomOpen() {
  const int ox = galZoomOx, oy = galZoomOy, ow = GAL_THUMB, oh = GAL_THUMB;
  galSnap.fillRectAlpha(0, 0, SCR_W, SCR_H, 110, 0x0000);   // latar grid diredupkan sekali
  const unsigned long t0 = millis();
  int lx0 = -1, ly0 = -1, lx1 = -1, ly1 = -1;
  display.startWrite();
  galSnap.pushSprite(0, 0);                                  // hapus pil "Memuat..."
  for (;;) {
    uiBoostKick();
    unsigned long el = millis() - t0;
    bool last = el >= GAL_ZOOM_MS;
    float e = last ? 1.0f : galEaseOutCubic((float)el / (float)GAL_ZOOM_MS);
    int x0 = (int)lroundf(ox * (1.0f - e)), y0 = (int)lroundf(oy * (1.0f - e));
    int x1 = (int)lroundf((ox + ow) * (1.0f - e) + SCR_W * e), y1 = (int)lroundf((oy + oh) * (1.0f - e) + SCR_H * e);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SCR_W) x1 = SCR_W; if (y1 > SCR_H) y1 = SCR_H;
    if (x1 > x0 && y1 > y0 && (x0 != lx0 || y0 != ly0 || x1 != lx1 || y1 != ly1)) {
      lx0 = x0; ly0 = y0; lx1 = x1; ly1 = y1;
      galPaintZoomWindow(x0, y0, x1 - x0, y1 - y0);
    }
    if (last) break;
    yield();
  }
  display.endWrite();
  galSnapReady = false; galNewValid = false;
  galPtrDown = false; galPressIdx = -1;
  needRedraw = true;
}

// Tutup foto: jendela mengecil ke thumbnail; grid digambar ulang hanya di 4 strip yg baru terbuka.
static void galZoomClose() {
  galDrawContent(galNew);                                    // frame grid (galMode sudah GM_GRID)
  const int tx = galZoomOx, ty = galZoomOy, ow = GAL_THUMB, oh = GAL_THUMB;
  const unsigned long t0 = millis();
  int px0 = 0, py0 = 0, px1 = SCR_W, py1 = SCR_H;
  display.startWrite();
  for (;;) {
    uiBoostKick();
    unsigned long el = millis() - t0;
    bool last = el >= GAL_ZOOM_CLOSE_MS;
    float u = last ? 1.0f : (float)el / (float)GAL_ZOOM_CLOSE_MS;
    float q = 1.0f - u, ez = q * q;
    int x0 = (int)lroundf(tx * (1.0f - ez)), y0 = (int)lroundf(ty * (1.0f - ez));
    int x1 = (int)lroundf((tx + ow) * (1.0f - ez) + SCR_W * ez), y1 = (int)lroundf((ty + oh) * (1.0f - ez) + SCR_H * ez);
    if (x0 < px0) x0 = px0; if (y0 < py0) y0 = py0; if (x1 > px1) x1 = px1; if (y1 > py1) y1 = py1;
    if (x1 > x0 && y1 > y0 && (x0 != px0 || y0 != py0 || x1 != px1 || y1 != py1)) {
      galPushBg(px0, py0, px1 - px0, y0 - py0);
      galPushBg(px0, y1, px1 - px0, py1 - y1);
      galPushBg(px0, y0, x0 - px0, y1 - y0);
      galPushBg(x1, y0, px1 - x1, y1 - y0);
      galPaintZoomWindow(x0, y0, x1 - x0, y1 - y0);
      px0 = x0; py0 = y0; px1 = x1; py1 = y1;
    }
    if (last) break;
    yield();
  }
  display.endWrite();
  galSnapReady = false; galNewValid = false;
  galPtrDown = false; galPressIdx = -1;
  needRedraw = true;
}

static void galRunTransition(int kind) {
  if (!galSnapReady) return;
  if (kind == 4) { if (galViewReady) { galZoomOpen(); return; } kind = 0; }
  if (kind == 5) { galZoomClose(); return; }
  galDrawContent(galNew);
  const unsigned long t0 = millis();
  int lastOff = -1;
  display.startWrite();
  for (;;) {
    uiBoostKick();                                   // CPU 240 MHz selama transisi
    unsigned long el = millis() - t0;
    bool last = el >= GAL_ANIM_MS;
    float t = last ? 1.0f : (float)el / (float)GAL_ANIM_MS;
    int off = (int)((float)SCR_W * navEase(t) + 0.5f);
    if (off > SCR_W) off = SCR_W;
    if (off != lastOff) {
      lastOff = off;
      if (kind == 0) {             // maju: layar baru masuk dari kanan, yg lama geser pelan (parallax 1/3)
        display.setClipRect(0, 0, SCR_W - off, SCR_H);
        galSnap.pushSprite(-(off / 3), 0);
        display.clearClipRect();
        galNew.pushSprite(SCR_W - off, 0);
      } else if (kind == 1) {      // mundur: layar yg ditutup geser keluar ke kanan
        display.setClipRect(0, 0, off, SCR_H);
        galNew.pushSprite(-((SCR_W - off) / 3), 0);
        display.clearClipRect();
        galSnap.pushSprite(off, 0);
      } else if (kind == 2) {      // foto berikutnya: geser ke kiri
        galSnap.pushSprite(-off, 0);
        galNew.pushSprite(SCR_W - off, 0);
      } else {                     // foto sebelumnya: geser ke kanan
        galSnap.pushSprite(off, 0);
        galNew.pushSprite(-(SCR_W - off), 0);
      }
    }
    if (last) break;
    yield();
  }
  display.endWrite();
  galSnapReady = false; galNewValid = false;
  galPtrDown = false; galPressIdx = -1;
  needRedraw = true;                                 // frame akhir sama persis; digambar ulang lewat canvas
}

void drawGallery(LGFX_Sprite& s) {
  galUseTick++;
  galFadeActive = false;
  galDrawContent(s);
}

// =====================================================================
// SENTUHAN
// =====================================================================
static void galBack() {
  galPtrDown = false;
  if (galMode == GM_VIEW) {
    galDelArmUntil = 0;
    if (galDirectOpen) { navBack(); return; }
    bool zoom = galViewReady && !galViewPending && galViewIdx >= 0 && galViewIdx < galItemCount;
    galMode = GM_GRID; galPressIdx = -1;
    if (zoom) {                                           // thumbnail foto ini: bawa ke tampilan dulu kalau di luar layar
      galGeom();
      int row = galViewIdx / galCols, col = galViewIdx % galCols;
      int rowTop = row * galRowH, visH = galBot - galTop;
      if (rowTop < (int)galScrollY || rowTop + GAL_THUMB > (int)galScrollY + visH) {
        float ns = (float)(rowTop - (visH - GAL_THUMB) / 2);
        float ms = (float)galMaxScroll(galItemCount);
        galScrollY = ns < 0 ? 0 : (ns > ms ? ms : ns);
      }
      galScrollVel = 0;
      galZoomOx = galX0 + col * (GAL_THUMB + GAL_GAP);
      galZoomOy = galTop + rowTop - (int)galScrollY;
      galBeginTransition(5, false);
    } else galBeginTransition(1, false);
    galAdjFree();
    needRedraw = true; return;
  }
  if (galMode == GM_GRID) {
    galMode = GM_ALBUMS;
    if (galAlbumsDirty && !galScanActive) galScanStart();
    galScrollY = galAlbumScrollSave; galScrollVel = 0; galPressIdx = -1;
    galBeginTransition(1, false);
    needRedraw = true; return;
  }
  navBack();
}

// Tombol langsung (touch-down): Kembali & tombol "Cache". Sisanya (ketuk
// kartu / scroll / swipe) diurus galPointerTick() yang tahu kapan jari lepas.
void galTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;
  if (galMode == GM_VIEW && !galViewOverlay) return;      // tombol tersembunyi: ketuk apa saja = tampilkan
  if (isBack(x, y)) { galBack(); return; }
  if (galMode == GM_ALBUMS && sdReady && galMem && galHit(x, y, SCR_W - 60, 20, 52, 22)) {
    if (millis() < galClearArmUntil) {
      galClearArmUntil = 0;
      galClearCache();
      showToast("Cache thumbnail dihapus"); vibSuccess();
    } else {
      galClearArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk hapus cache");
    }
    needRedraw = true;
  }
}

// indeks kartu di bawah (x,y), atau -1
static int galCellAt(int x, int y) {
  if (y < galTop || y > galBot) return -1;
  int n = (galMode == GM_ALBUMS) ? galAlbumCount : galItemCount;
  int relY = y - galTop + (int)galScrollY;
  int row = relY / galRowH, inY = relY % galRowH;
  if (inY >= GAL_THUMB) return -1;
  int relX = x - galX0;
  if (relX < 0) return -1;
  int col = relX / (GAL_THUMB + GAL_GAP), inX = relX % (GAL_THUMB + GAL_GAP);
  if (col >= galCols || inX >= GAL_THUMB) return -1;
  int idx = row * galCols + col;
  return (idx >= 0 && idx < n) ? idx : -1;
}

static void galTapGrid(int x, int y) {
  int idx = galCellAt(x, y);
  if (idx < 0) return;
  if (galMode == GM_ALBUMS) {
    galOpenAlbum(idx);
  } else if (ownerPickActive()) {                 // mode pilih foto profil (dari layar Profil)
    if (galItems[idx].kind == 2) { showToast("Pilih foto, bukan video"); }
    else { char op[GAL_PATH_MAX]; galItemPath(op, sizeof(op), idx); ownerPickChosen(op); return; }
  } else if (galItems[idx].kind == 2) {
    char p[GAL_PATH_MAX];
    galItemPath(p, sizeof(p), idx);
    mjpegPlayFile(String(p + 1));        // pemutar MJPEG pakai path relatif dari root ("/" sudah ditambah di sana)
  } else {
    galViewIdx = idx; galMode = GM_VIEW;
    galViewPending = true; galViewReady = false; galViewFail = false;
    galViewOverlay = true; galDelArmUntil = 0;
    galAdjInvalidate();
    galZoomOx = galX0 + (idx % galCols) * (GAL_THUMB + GAL_GAP);
    galZoomOy = galTop + (idx / galCols) * galRowH - (int)galScrollY;
    galBeginTransition(4, true);         // zoom dari thumbnail, mulai setelah foto selesai didecode
  }
  needRedraw = true;
}

static void galGridPointer(bool touched, bool wasT, int tx, int ty, float dt) {
  int n = (galMode == GM_ALBUMS) ? galAlbumCount : galItemCount;
  float ms = (float)galMaxScroll(n);
  if (touched) {
    if (!wasT) {
      galPtrDown = (ty >= galTop && ty <= galBot);
      galPtrSx = tx; galPtrSy = ty; galPtrLy = ty; galPtrDrag = false; galScrollVel = 0;
      galPressIdx = galPtrDown ? galCellAt(tx, ty) : -1;
      if (galPressIdx >= 0) needRedraw = true;
    } else if (galPtrDown) {
      if (!galPtrDrag && abs(ty - galPtrSy) > 8) {
        galPtrDrag = true;
        if (galPressIdx >= 0) { galPressIdx = -1; needRedraw = true; }
      }
      if (galPtrDrag) {
        float d = (float)(galPtrLy - ty);
        galPtrLy = ty;
        galScrollVel = galScrollVel * 0.5f + d * 0.5f;
        galScrollY += d;
        if (galScrollY < 0) galScrollY = 0;
        if (galScrollY > ms) galScrollY = ms;
        needRedraw = true;
      }
    }
  } else {
    if (galPressIdx >= 0) { galPressIdx = -1; needRedraw = true; }
    if (wasT && galPtrDown) {
      galPtrDown = false;
      if (!galPtrDrag) galTapGrid(galPtrSx, galPtrSy);
    } else {
      galPtrDown = false;
    }
    if (fabsf(galScrollVel) > 0.3f) {
      galScrollY += galScrollVel * (dt / 16.0f);
      if (galScrollY < 0) { galScrollY = 0; galScrollVel = 0; }
      else if (galScrollY > ms) { galScrollY = ms; galScrollVel = 0; }
      galScrollVel *= powf(0.92f, dt / 16.0f);
      needRedraw = true;
    } else {
      galScrollVel = 0;
    }
  }
}

static void galZClamp() {
  float mx = (galZ - 1.0f) * (float)SCR_W * 0.5f, my = (galZ - 1.0f) * (float)SCR_H * 0.5f;
  if (galPx > mx) galPx = mx; if (galPx < -mx) galPx = -mx;
  if (galPy > my) galPy = my; if (galPy < -my) galPy = -my;
}

// animasi zoom langsung ke display (tanpa overlay); tujuan pan dijepit supaya foto selalu menutupi layar
static void galZoomApply(float zTo, float pxTo, float pyTo) {
  float mx = (zTo - 1.0f) * (float)SCR_W * 0.5f, my = (zTo - 1.0f) * (float)SCR_H * 0.5f;
  if (pxTo > mx) pxTo = mx; if (pxTo < -mx) pxTo = -mx;
  if (pyTo > my) pyTo = my; if (pyTo < -my) pyTo = -my;
  float zF = galZ, pxF = galPx, pyF = galPy;
  const unsigned long t0 = millis(), dur = 200;
  display.startWrite();
  for (;;) {
    uiBoostKick();
    unsigned long el = millis() - t0;
    bool last = el >= dur;
    float e = last ? 1.0f : galEaseOutCubic((float)el / (float)dur);
    float z = zF + (zTo - zF) * e, px = pxF + (pxTo - pxF) * e, py = pyF + (pyTo - pyF) * e;
    if (z < 1.001f) galView.pushSprite(0, 0);
    else galView.pushRotateZoom(&display, SCR_W * 0.5f + px, SCR_H * 0.5f + py, 0.0f, z, z);
    if (last) break;
    yield();
  }
  display.endWrite();
  galZ = zTo; galPx = pxTo; galPy = pyTo;
  needRedraw = true;
}

static void galViewPointer(bool touched, bool wasT, int tx, int ty) {
  const unsigned long now = millis();
  if (touched) {
    galLastTouchMs = now;
    if (!wasT) {
      galPtrDown = true; galPtrSx = tx; galPtrSy = ty; galPtrLx = tx; galPtrLy = ty; galPtrDrag = false;
      galSwipeOn = false; galSwipeDx = 0; galSwipeVel = 0; galSwipeLastX = tx; galSwipeLastMs = now;
      galVDragOn = false; galVDy = 0; galVVel = 0; galVLastY = ty; galVLastMs = now;
    } else if (galPtrDown) {
      int pdx = tx - galPtrLx, pdy = ty - galPtrLy;
      galPtrLx = tx; galPtrLy = ty;
      int dx = tx - galPtrSx, dy = ty - galPtrSy;
      if (abs(dx) > 10 || abs(dy) > 10) galPtrDrag = true;
      if (galZ > 1.001f) {                               // lagi di-zoom: geser = pan
        if (galPtrDrag && (pdx != 0 || pdy != 0)) { galPx += (float)pdx; galPy += (float)pdy; galZClamp(); needRedraw = true; }
        return;
      }
      if (!galSwipeOn && !galVDragOn && galViewReady && !galViewPending) {
        if (abs(dx) > 10 && abs(dx) > abs(dy)) galSwipeOn = true;
        else if (dy > 14 && dy > abs(dx) * 12 / 10) galVDragOn = true;      // geser ke bawah = tutup
      }
      if (galVDragOn) {
        galVDy = dy > 0 ? (float)dy : 0.0f;
        unsigned long dtm = now - galVLastMs;
        if (dtm > 0) galVVel = galVVel * 0.6f + ((float)(ty - galVLastY) / (float)dtm) * 0.4f;
        galVLastY = ty; galVLastMs = now;
        needRedraw = true;
      }
      if (galSwipeOn) {                                  // foto mengikuti jari; tanpa tetangga = tarikan karet
        int dir = (dx < 0) ? +1 : -1;
        galSwipeDx = (galNextPhoto(galViewIdx, dir) >= 0) ? (float)dx : (float)dx * 0.35f;
        unsigned long dtm = now - galSwipeLastMs;
        if (dtm > 0) { galSwipeVel = galSwipeVel * 0.6f + ((float)(tx - galSwipeLastX) / (float)dtm) * 0.4f; }
        galSwipeLastX = tx; galSwipeLastMs = now;
        needRedraw = true;
      }
    }
  } else if (wasT && galPtrDown) {
    galPtrDown = false;
    if (galVDragOn) {
      galVDragOn = false;
      if (now - galVLastMs > 90) galVVel = 0;
      float from = galVDy; galVDy = 0;
      if (from > (float)SCR_H * 0.25f || galVVel > 0.5f) { galBack(); return; }   // cukup jauh / cepat: tutup (zoom ke thumbnail)
      const unsigned long t0 = millis(), dur = 160;      // balik ke posisi semula
      display.startWrite();
      for (;;) {
        uiBoostKick();
        unsigned long el = millis() - t0;
        bool last = el >= dur;
        int dyy = (int)lroundf(from * (1.0f - (last ? 1.0f : galEaseOutCubic((float)el / (float)dur))));
        galView.pushSprite(0, dyy);
        if (dyy > 0) display.fillRect(0, 0, SCR_W, dyy, 0x0000);
        if (last) break;
        yield();
      }
      display.endWrite();
      needRedraw = true;
      return;
    }
    if (galSwipeOn) {
      galSwipeOn = false;
      if (now - galSwipeLastMs > 90) galSwipeVel = 0;     // jari sempat berhenti: bukan lemparan
      float from = galSwipeDx; galSwipeDx = 0;
      int dir = (from < 0) ? +1 : -1;
      bool has = galNextPhoto(galViewIdx, dir) >= 0;
      bool far = fabsf(from) > (float)SCR_W * 0.25f;
      bool fast = fabsf(galSwipeVel) > 0.35f && ((galSwipeVel < 0.0f) == (dir > 0));
      if (has && (far || fast)) {
        if (galAdjOk(dir)) {
          galDelArmUntil = 0;
          galSwipeAnimate(from, dir > 0 ? -(float)SCR_W : (float)SCR_W, dir);
          galAdoptNeighbor(dir);
        } else galViewStep(dir);                          // tetangga belum siap: cara lama (Memuat... lalu geser)
      } else {
        galSwipeAnimate(from, 0.0f, dir);                 // balik ke posisi semula
        needRedraw = true;
      }
      return;
    }
    if (!galPtrDrag) {
      bool center = galPtrSx > 44 && galPtrSx < SCR_W - 44 && galPtrSy > 24 && galPtrSy < backY() - 4;
      if (galViewReady && !galViewPending && center && galTapMs != 0 && now - galTapMs < 320 &&
          abs(galPtrSx - galTapX) < 40 && abs(galPtrSy - galTapY) < 40) {
        galTapMs = 0;
        galViewOverlay = galOvBeforeTap;                  // ketukan pertama sempat menyalakan/mematikan overlay: kembalikan
        if (galZ > 1.001f) galZoomApply(1.0f, 0.0f, 0.0f);
        else galZoomApply(GAL_ZOOM_MAXF, ((float)SCR_W * 0.5f - (float)galPtrSx) * (GAL_ZOOM_MAXF - 1.0f),
                                         ((float)SCR_H * 0.5f - (float)galPtrSy) * (GAL_ZOOM_MAXF - 1.0f));
        return;
      }
      galTapMs = now; galTapX = galPtrSx; galTapY = galPtrSy; galOvBeforeTap = galViewOverlay;
      if (galZ > 1.001f) return;                          // ketuk tunggal saat di-zoom: abaikan
      galViewTap(galPtrSx, galPtrSy);
    }
  } else {
    galPtrDown = false;
  }
}

// Dipanggil tiap putaran loop() selagi layar = Galeri (lihat patch di loop()).
void galPointerTick(bool touched, bool wasT, int tx, int ty) {
  unsigned long now = millis();
  long dtl = (long)(now - galLastTickMs);
  float dt = (dtl <= 0 || dtl > 50) ? 16.0f : (float)dtl;
  galLastTickMs = now;
  if (!galMem) return;
  if (galViewPending && galMode == GM_VIEW) {            // frame "Memuat..." sudah tampil -> decode sekarang
    galDecodeCurrent();
    if (galSnapReady) galRunTransition(galAnimKind);   // animasi jalan setelah decode selesai
    galLastTouchMs = millis();
    needRedraw = true;
  }
  if (galFadeActive) { galFadeActive = false; needRedraw = true; }   // fade-in thumbnail: terus gambar ulang
  galGeom();
  if (galMode == GM_VIEW) {
    galViewPointer(touched, wasT, tx, ty);
    if (!touched && !galSwipeOn && millis() - galLastTouchMs > 350) galPrefetchStep();   // siapkan foto sebelah saat jari diam
  } else {
    galGridPointer(touched, wasT, tx, ty, dt);
    if (galMode == GM_ALBUMS && galScanActive) {
      galScanStep(touched ? 6 : 14);                    // lebih hemat saat jari menyentuh
      static unsigned long lastScanRedraw = 0;
      if (galScanActive && millis() - lastScanRedraw > 300) { lastScanRedraw = millis(); needRedraw = true; }
    }
    galLoaderStep(!galPtrDown && fabsf(galScrollVel) < 0.5f);
  }
  if (galDelArmUntil && millis() >= galDelArmUntil) { galDelArmUntil = 0; needRedraw = true; }
  if (galClearArmUntil && millis() >= galClearArmUntil) { galClearArmUntil = 0; needRedraw = true; }
}

// =====================================================================
// ENTER / EXIT + API untuk File Explorer
// =====================================================================
// File Explorer memanggil ini saat user "Buka" file gambar: Galeri langsung
// menampilkan gambar itu (bisa geser ke foto lain di folder yang sama),
// dan Kembali = balik ke File Explorer.
void galOpenFile(const char* path) {
  if (!path || path[0] != '/') return;
  strncpy(galPendingPath, path, GAL_PATH_MAX - 1);
  galPendingPath[GAL_PATH_MAX - 1] = 0;
  navPush(SCR_GALLERY);
}

void galEnter() {
  galDirectOpen = false;
  galMode = GM_ALBUMS;
  galScrollY = 0; galScrollVel = 0; galAlbumScrollSave = 0;
  galPtrDown = false; galPtrDrag = false;
  galViewReady = false; galViewPending = false; galViewFail = false; galViewIdx = -1;
  galDelArmUntil = 0; galClearArmUntil = 0; galWantCnt = 0;
  galSnapReady = false; galNewValid = false; galPressIdx = -1; galFadeActive = false;
  galAdjInvalidate(); galAdjDisabled = false; galSwipeOn = false; galSwipeDx = 0; galLastTouchMs = millis();
  galLastTickMs = millis();
  if (!sdReady) { galPendingPath[0] = 0; return; }
  if (!galMemEnsure()) { galPendingPath[0] = 0; showToast("PSRAM tidak cukup"); return; }
  memset(galSlotState, 0, sizeof(galSlotState));

  if (galPendingPath[0]) {
    char dir[64], name[48];
    const char* sl = strrchr(galPendingPath, '/');
    int dl = (int)(sl - galPendingPath);
    if (dl <= 0) { strcpy(dir, "/"); }
    else if (dl < 63) { strncpy(dir, galPendingPath, dl); dir[dl] = 0; }
    else dir[0] = 0;
    strncpy(name, sl + 1, 47); name[47] = 0;
    galPendingPath[0] = 0;
    if (dir[0]) {
      strncpy(galCurDir, dir, 63); galCurDir[63] = 0;
      galTitleFromPath(galCurTitle, sizeof(galCurTitle), dir);
      galLoadItems();
      for (int i = 0; i < galItemCount; i++) {
        if (strcmp(galItems[i].name, name) == 0 && galItems[i].kind != 2) {
          galDirectOpen = true;
          galViewIdx = i; galMode = GM_VIEW; galViewPending = true; galViewOverlay = true;
          return;
        }
      }
    }
  }
  galScanStart();                                       // tampilkan daftar lama dulu, pindai ulang di latar
}

void galExit() {
  ownerPickReset();
  galScanAbort();
  galViewFree();
  galSnap.deleteSprite(); galNew.deleteSprite();
  galSnapReady = false; galNewValid = false;
  galMemFree();
  galPtrDown = false;
  galPendingPath[0] = 0;
}

// dipakai phone.ino (backHidden): di penampil foto, tombol Kembali tetap tampil
bool galInViewer() { return galMode == GM_VIEW; }
