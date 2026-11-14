// =====================================================================
// backup_renphone.ino -- BACKUP & RESTORE PENGATURAN (SCR_BACKUP) untuk Accretion Phone / NyxOS
// =====================================================================
// Fungsi:
//   * "Buat Backup" : seluruh pengaturan di NVS (tema, kecerahan, susunan Home, kalibrasi, kamus
//                     pelajaran, Remote Control, dst.) disimpan ke SD: /backup/nyx_NNN.nbk
//   * "Pulihkan"    : tulis balik isi file ke NVS lalu HP restart sendiri (nilai pengaturan
//                     dibaca ke RAM saat boot, jadi restart perlu supaya semuanya terpakai)
//   * "Hapus"       : buang satu file backup
//   * Sakelar "Sertakan sandi & API key" (default MATI): sandi WiFi, akun SIP, akun Chat, API key
//     Gemini, dan sandi AP Remote Control hanya ikut kalau sakelar ini nyala. File di SD
//     TIDAK dienkripsi, jadi default-nya tidak menyertakan rahasia.
//
// CARA KERJA: NVS tidak punya "daftar semua pengaturan", jadi kita menyisir seluruh partisi NVS
// lewat nvs_entry_find(). Artinya pengaturan dari fitur BARU otomatis ikut ter-backup tanpa
// perlu mengubah file ini. Namespace sistem (nvs*, phy*, bt*, otaguard) dikecualikan.
// Format file & pemeriksaan keamanan ada di backup_core.h (diuji di komputer: tests_backup/).
//
// CATATAN: Restore MENIMPA nilai yang ada di file backup. Pengaturan yang dibuat SETELAH backup
// (tidak ada di file) tidak dihapus. Maksimum 6 backup tersimpan; yang paling lama otomatis dibuang
// saat membuat backup baru.
//
// ATURAN PROYEK YANG DIIKUTI:
//   - Tidak ada String dibuat tiap frame; semua teks lewat buffer char di stack/statis.
//   - Buffer besar (hingga 96 KB) dialokasi di PSRAM hanya selama operasi, lalu dibebaskan.
//     RAM internal tidak tersentuh.
//   - Fungsi publik hanya memakai tipe primitif / LGFX_Sprite& (aman dari bug prototype arduino-cli).
//   - Layout dihitung dari SCR_W/SCR_H (potret & lanskap); hit-test memakai rect yang sama dgn gambar.
//
// DIPASANG ke phone.ino lewat apply_backup_patch.py:
//   enum Screen      <- SCR_BACKUP
//   apps[] + warna   <- { "Backup", 'z', ... }  (disembunyikan dari grid Home, ada di Pengaturan)
//   drawAppIcon()    <- case 'z'
//   Pengaturan       <- baris "Backup & Restore" (bagian TENTANG)
// =====================================================================
#include <nvs.h>
#include <nvs_flash.h>
#include "esp_idf_version.h"
#define BK_YIELD() yield()              // PBKDF2 ~0,5 dtk di ESP32: beri napas ke watchdog
#include "backup_core.h"
#include "backup_crypto.h"

#define BK_DIR        "/backup"
#define BK_KEEP       6                 // jumlah file backup yang disimpan di SD
#define BK_BUF_MAX    (96 * 1024)       // batas ukuran satu file backup
#define BK_VALUE_MAX  (32 * 1024)       // nilai NVS lebih besar dari ini dilewati
#define BK_ROW_H      36
#define BK_NVS_NS     "nyxbak"

enum { BKR_CREATE = 0, BKR_TOGGLE, BKR_ENC, BKR_LIST, BKR_RESTORE, BKR_DELETE, BKR_PBOX, BKR_POK, BKR_PCANCEL, BKR_N };

static bool          bkSecrets   = false;
static bool          bkEnc       = false;          // backup baru dienkripsi dgn sandi (sandi ditanya tiap kali, TIDAK disimpan)
static bool          bkEncF[BK_KEEP];              // file di daftar ini terenkripsi?
static int           bkPrompt    = 0;              // 0 tidak ada | 1 sandi baru | 2 ulangi sandi | 3 sandi utk pulihkan
static int           bkPromptIdx = -1;             // backup yg mau dipulihkan (prompt 3)
static char          bkPromptErr[32] = "";
static String        bkPassA, bkPassB;             // dihapus (ditimpa nol) segera setelah dipakai
static int           bkListN     = 0;
static int           bkNum[BK_KEEP];
static uint32_t      bkEpoch[BK_KEEP];
static uint32_t      bkSize[BK_KEEP];
static uint16_t      bkCnt[BK_KEEP];
static uint8_t       bkFlg[BK_KEEP];
static bool          bkValid[BK_KEEP];
static int           bkSel       = -1;
static float         bkScroll    = 0;
static int           bkR[BKR_N][4];
static int           bkArmKind   = 0;          // 0 tidak ada | 1 menunggu konfirmasi Pulihkan | 2 ... Hapus
static unsigned long bkArmUntil  = 0;
static unsigned long bkRestartAt = 0;          // != 0 -> restart saat waktunya (diurus drawBackup)
static char          bkMsg[44]   = "";
static bool          bkDown = false, bkMoved = false, bkDragList = false;
static int           bkPx = 0, bkPy = 0, bkLy = 0;
static nvs_iterator_t bkIt = NULL;

static void bkSay(const char* m, int ms) {
  strncpy(bkMsg, m, sizeof(bkMsg) - 1); bkMsg[sizeof(bkMsg) - 1] = 0;
  showToast(bkMsg, ms);                       // toast diam kalau mode Jangan Ganggu aktif -> teks tetap tampil di layar
  needRedraw = true;
}

// ---------------------------------------------------------------------
// iterasi NVS (API berubah antara ESP-IDF 4.x dan 5.x)
// ---------------------------------------------------------------------
static bool bkItFirst() {
#if ESP_IDF_VERSION_MAJOR >= 5
  bkIt = NULL;
  return nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &bkIt) == ESP_OK && bkIt != NULL;
#else
  bkIt = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);
  return bkIt != NULL;
#endif
}
static bool bkItNext() {
#if ESP_IDF_VERSION_MAJOR >= 5
  return nvs_entry_next(&bkIt) == ESP_OK && bkIt != NULL;
#else
  bkIt = nvs_entry_next(bkIt);
  return bkIt != NULL;
#endif
}
static void bkItEnd() { if (bkIt) nvs_release_iterator(bkIt); bkIt = NULL; }

static uint8_t bkTypeOf(int t) {
  switch (t) {
    case NVS_TYPE_U8:   return BK_T_U8;   case NVS_TYPE_I8:  return BK_T_I8;
    case NVS_TYPE_U16:  return BK_T_U16;  case NVS_TYPE_I16: return BK_T_I16;
    case NVS_TYPE_U32:  return BK_T_U32;  case NVS_TYPE_I32: return BK_T_I32;
    case NVS_TYPE_U64:  return BK_T_U64;  case NVS_TYPE_I64: return BK_T_I64;
    case NVS_TYPE_STR:  return BK_T_STR;  case NVS_TYPE_BLOB: return BK_T_BLOB;
  }
  return 0;
}

static uint8_t* bkAlloc(size_t n) { return (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

// Baca satu nilai NVS -> tambahkan ke penulis. 1 = ok, 0 = dilewati, -1 = buffer penuh.
// (nilai skalar ditulis apa adanya dalam byte-order native; ESP32 little-endian = format file)
static int bkReadOne(void* wv, uint32_t h, const char* ns, const char* key, uint8_t t) {
  BkWriter& w = *(BkWriter*)wv;
  uint8_t sc[8]; const void* dp = sc; uint32_t dl = 0; bool got = false; uint8_t* tmp = NULL;
  switch (t) {
    case BK_T_U8:  { uint8_t  v; got = nvs_get_u8 (h, key, &v) == ESP_OK; memcpy(sc, &v, 1); dl = 1; } break;
    case BK_T_I8:  { int8_t   v; got = nvs_get_i8 (h, key, &v) == ESP_OK; memcpy(sc, &v, 1); dl = 1; } break;
    case BK_T_U16: { uint16_t v; got = nvs_get_u16(h, key, &v) == ESP_OK; memcpy(sc, &v, 2); dl = 2; } break;
    case BK_T_I16: { int16_t  v; got = nvs_get_i16(h, key, &v) == ESP_OK; memcpy(sc, &v, 2); dl = 2; } break;
    case BK_T_U32: { uint32_t v; got = nvs_get_u32(h, key, &v) == ESP_OK; memcpy(sc, &v, 4); dl = 4; } break;
    case BK_T_I32: { int32_t  v; got = nvs_get_i32(h, key, &v) == ESP_OK; memcpy(sc, &v, 4); dl = 4; } break;
    case BK_T_U64: { uint64_t v; got = nvs_get_u64(h, key, &v) == ESP_OK; memcpy(sc, &v, 8); dl = 8; } break;
    case BK_T_I64: { int64_t  v; got = nvs_get_i64(h, key, &v) == ESP_OK; memcpy(sc, &v, 8); dl = 8; } break;
    case BK_T_STR: {
      size_t L = 0;
      if (nvs_get_str(h, key, NULL, &L) == ESP_OK && L > 0 && L <= BK_VALUE_MAX) {
        tmp = bkAlloc(L);
        if (tmp) { got = nvs_get_str(h, key, (char*)tmp, &L) == ESP_OK; dp = tmp; dl = (uint32_t)L; }
      }
    } break;
    case BK_T_BLOB: {
      size_t L = 0;
      if (nvs_get_blob(h, key, NULL, &L) == ESP_OK && L <= BK_VALUE_MAX) {
        if (L == 0) { dp = sc; dl = 0; got = true; }
        else { tmp = bkAlloc(L); if (tmp) { got = nvs_get_blob(h, key, tmp, &L) == ESP_OK; dp = tmp; dl = (uint32_t)L; } }
      }
    } break;
  }
  int res = 0;
  if (got) res = bkAddRecord(w, ns, key, t, dp, dl) ? 1 : (w.over ? -1 : 0);
  if (tmp) free(tmp);
  return res;
}

// Kumpulkan seluruh pengaturan ke buf. 0 = ok (outLen & outN terisi), 1 = buffer kurang, 2 = NVS gagal dibaca.
static int bkCollect(uint8_t* buf, size_t cap, bool secrets, size_t* outLen, int* outN) {
  BkWriter w; bkBegin(w, buf, cap);
  nvs_handle_t h = 0; bool open = false; char curNs[BK_NAME_MAX + 1] = "";
  int status = 0;
  bool more = bkItFirst();
  while (more) {
    nvs_entry_info_t info; nvs_entry_info(bkIt, &info);
    uint8_t t = bkTypeOf((int)info.type);
    if (t && !bkNsSkip(info.namespace_name) && (secrets || !bkIsSecret(info.namespace_name, info.key))) {
      if (!open || strcmp(curNs, info.namespace_name) != 0) {
        if (open) nvs_close(h);
        open = nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK;
        strncpy(curNs, info.namespace_name, BK_NAME_MAX); curNs[BK_NAME_MAX] = 0;
      }
      if (open) {
        int r = bkReadOne(&w, (uint32_t)h, info.namespace_name, info.key, t);
        if (r < 0) { status = 1; break; }
      }
    }
    more = bkItNext();
  }
  if (open) nvs_close(h);
  bkItEnd();
  if (status) return status;
  uint32_t ep = ntpSynced ? (uint32_t)time(nullptr) : 0;
  size_t n = bkFinish(w, secrets ? BK_FLAG_SECRETS : 0, ep);
  if (n == 0) return w.over ? 1 : 2;
  *outLen = n; *outN = (int)w.count;
  return 0;
}

// Tulis balik file backup (sudah diverifikasi CRC-nya) ke NVS. Mengembalikan jumlah nilai yang
// berhasil ditulis, atau -1 kalau file ditolak.
static int bkApply(const uint8_t* b, size_t n, int* outFail) {
  BkHdr hd; if (!bkVerify(b, n, hd)) return -1;
  size_t pos = BK_HDR, end = n - 4; BkRec r;
  for (;;) {                                                        // pra-pemeriksaan: semua rekaman harus terbaca, baru mulai menulis
    int q = bkNext(b, end, pos, r);
    if (q < 0) return -1;                                           // file janggal -> tidak ada satu pun nilai yang ditulis
    if (q == 0) break;
  }
  pos = BK_HDR;
  nvs_handle_t h = 0; bool open = false; char curNs[BK_NAME_MAX + 1] = "";
  int ok = 0, fail = 0;
  for (;;) {
    int q = bkNext(b, end, pos, r);
    if (q < 0) { if (open) { nvs_commit(h); nvs_close(h); } return -1; }
    if (q == 0) break;
    if (bkNsSkip(r.ns)) continue;                                  // jaga-jaga: file dari luar
    if (!open || strcmp(curNs, r.ns) != 0) {
      if (open) { nvs_commit(h); nvs_close(h); }
      open = nvs_open(r.ns, NVS_READWRITE, &h) == ESP_OK;
      strncpy(curNs, r.ns, BK_NAME_MAX); curNs[BK_NAME_MAX] = 0;
    }
    if (!open) { fail++; continue; }
    esp_err_t e = ESP_FAIL;
    switch (r.type) {
      case BK_T_U8:  e = nvs_set_u8(h, r.key, r.data[0]); break;
      case BK_T_I8:  e = nvs_set_i8(h, r.key, (int8_t)r.data[0]); break;
      case BK_T_U16: { uint16_t v; memcpy(&v, r.data, 2); e = nvs_set_u16(h, r.key, v); } break;
      case BK_T_I16: { int16_t  v; memcpy(&v, r.data, 2); e = nvs_set_i16(h, r.key, v); } break;
      case BK_T_U32: { uint32_t v; memcpy(&v, r.data, 4); e = nvs_set_u32(h, r.key, v); } break;
      case BK_T_I32: { int32_t  v; memcpy(&v, r.data, 4); e = nvs_set_i32(h, r.key, v); } break;
      case BK_T_U64: { uint64_t v; memcpy(&v, r.data, 8); e = nvs_set_u64(h, r.key, v); } break;
      case BK_T_I64: { int64_t  v; memcpy(&v, r.data, 8); e = nvs_set_i64(h, r.key, v); } break;
      case BK_T_STR: e = nvs_set_str(h, r.key, (const char*)r.data); break;
      case BK_T_BLOB: e = nvs_set_blob(h, r.key, r.data, r.len); break;
    }
    if (e == ESP_OK) ok++; else fail++;
  }
  if (open) { nvs_commit(h); nvs_close(h); }
  if (outFail) *outFail = fail;
  return ok;
}

// ---------------------------------------------------------------------
// daftar file di SD
// ---------------------------------------------------------------------
static void bkScan(bool prune) {
  bkListN = 0;
  if (!sdReady) { Serial.println("[bk] scan: sdReady=false"); return; }
  File d = SD_MMC.open(BK_DIR);
  if (!d) { Serial.println("[bk] scan: open(/backup) GAGAL"); return; }
  if (!d.isDirectory()) { Serial.println("[bk] scan: /backup bukan folder"); d.close(); return; }
  int nums[100]; int n = 0;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    if (!e.isDirectory()) { int k = bkParseName(e.name()); Serial.printf("[bk] scan: '%s' -> %d\n", e.name(), k); if (k >= 0 && n < 100) nums[n++] = k; }
    else Serial.printf("[bk] scan: '%s' (folder)\n", e.name());
    e.close();
  }
  d.close();
  Serial.printf("[bk] scan: file backup dikenali = %d\n", n);
  for (int i = 1; i < n; i++) { int v = nums[i], j = i - 1; while (j >= 0 && nums[j] < v) { nums[j + 1] = nums[j]; j--; } nums[j + 1] = v; }   // terbaru dulu
  if (prune) {
    for (int i = BK_KEEP; i < n; i++) { char p[40]; bkMakeName(p, sizeof(p), nums[i]); SD_MMC.remove(p); }
  }
  for (int i = 0; i < n && i < BK_KEEP; i++) {
    char p[40]; bkMakeName(p, sizeof(p), nums[i]);
    bkNum[i] = nums[i]; bkValid[i] = false; bkSize[i] = 0; bkEpoch[i] = 0; bkCnt[i] = 0; bkFlg[i] = 0; bkEncF[i] = false;
    File f = SD_MMC.open(p, FILE_READ);
    if (f) {
      bkSize[i] = (uint32_t)f.size();
      uint8_t hb[BK_HDR];
      if (f.read(hb, BK_HDR) == BK_HDR) {
        BkHdr h;
        if (bkParseHdr(hb, BK_HDR, h)) { bkValid[i] = true; bkEpoch[i] = h.epoch; bkCnt[i] = h.count; bkFlg[i] = h.flags; bkEncF[i] = h.enc; }
      }
      f.close();
    }
    bkListN++;
  }
}

static void bkClampScroll() {
  int listH = bkR[BKR_LIST][3];
  float mx = (float)(bkListN * BK_ROW_H - listH);
  if (mx < 0) mx = 0;
  if (bkScroll > mx) bkScroll = mx;
  if (bkScroll < 0) bkScroll = 0;
}

// ---------------------------------------------------------------------
// aksi
// ---------------------------------------------------------------------
static void bkWipe(String& s) {                                    // timpa isi String dgn nol sebelum dilepas
  char* p = (char*)s.c_str(); size_t n = s.length();
  for (size_t i = 0; i < n; i++) ((volatile char*)p)[i] = 0;
  s = "";
}
static void bkZero(void* p, size_t n) { for (size_t i = 0; i < n; i++) ((volatile uint8_t*)p)[i] = 0; }

// pass == NULL -> file biasa; selain itu file dienkripsi AES-256 + HMAC dgn kunci dari sandi (lihat backup_crypto.h)
static void bkDoCreate(const char* pass) {
  if (!sdReady) { bkSay("SD Card tidak terdeteksi", 2200); return; }
  uint8_t* buf = bkAlloc(BK_BUF_MAX);
  if (!buf) { bkSay("PSRAM tidak cukup", 2200); return; }
  size_t len = 0; int cnt = 0;
  int rc = bkCollect(buf, BK_BUF_MAX, bkSecrets, &len, &cnt);
  if (rc == 1) { free(buf); bkSay("Pengaturan terlalu besar", 2400); return; }
  if (rc != 0) { free(buf); bkSay("Gagal membaca pengaturan", 2400); return; }
  uint8_t* wbuf = buf; size_t wlen = len; uint8_t* eb = NULL;
  if (pass) {                                                     // enkripsi di memori; isi biasa (bisa memuat sandi) langsung ditimpa nol
    size_t cap = len + BK_ENC_OVERHEAD + 16;
    eb = bkAlloc(cap);
    if (!eb) { bkZero(buf, len); free(buf); bkSay("PSRAM tidak cukup", 2200); return; }
    uint8_t rnd[32]; for (int i = 0; i < 32; i += 4) { uint32_t r = esp_random(); memcpy(rnd + i, &r, 4); }
    wlen = bkEncrypt(buf, len, (const uint8_t*)pass, strlen(pass), rnd, rnd + 16, BK_ENC_ITER_DEF, eb, cap);
    bkZero(buf, len); bkZero(rnd, sizeof(rnd));
    if (!wlen) { free(eb); free(buf); bkSay("Gagal mengenkripsi", 2400); return; }
    wbuf = eb;
  }
  if (!SD_MMC.exists(BK_DIR)) SD_MMC.mkdir(BK_DIR);
  bkScan(false);
  int next = bkListN ? bkNum[0] + 1 : 1;
  char path[40]; bkMakeName(path, sizeof(path), next);
  File f = SD_MMC.open(path, FILE_WRITE);
  bool ok = false;
  if (f) { ok = (f.write(wbuf, wlen) == wlen); f.close(); }
  if (ok) {                                                       // baca ulang & cek CRC supaya yakin utuh
    File g = SD_MMC.open(path, FILE_READ);
    ok = false;
    if (g && (size_t)g.size() == wlen) {
      uint8_t* chk = bkAlloc(wlen);
      if (chk) {
        ok = (g.read(chk, wlen) == wlen);
        if (pass) ok = ok && memcmp(chk, wbuf, wlen) == 0;        // terenkripsi: harus sama persis dgn yg ditulis
        else { BkHdr h; ok = ok && bkVerify(chk, wlen, h); }
        free(chk);
      }
    }
    if (g) g.close();
  }
  bkZero(buf, len); free(buf); if (eb) free(eb);
  if (!ok) { SD_MMC.remove(path); bkSay("Gagal menulis ke SD", 2400); return; }
  bkScan(true);
  bkSel = 0; bkScroll = 0; bkArmKind = 0;
  char m[44]; snprintf(m, sizeof(m), pass ? "Backup #%03d dibuat (%d nilai) +kunci" : "Backup #%03d dibuat (%d nilai)", next, cnt);
  bkSay(m, 2400);
}

// Mengembalikan 0 = selesai (berhasil/gagal sudah diberitahukan lewat toast), BK_DEC_PASS = sandi salah, -2 = butuh sandi.
static int bkDoRestore(int idx, const char* pass) {
  char path[40]; bkMakeName(path, sizeof(path), bkNum[idx]);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { bkSay("File tidak bisa dibuka", 2200); return 0; }
  size_t n = (size_t)f.size();
  if (n < BK_HDR + 5 || n > BK_BUF_MAX + BK_ENC_OVERHEAD + 16) { f.close(); bkSay("Ukuran file tidak wajar", 2200); return 0; }
  uint8_t* buf = bkAlloc(n);
  if (!buf) { f.close(); bkSay("PSRAM tidak cukup", 2200); return 0; }
  size_t got = f.read(buf, n); f.close();
  if (got != n) { free(buf); bkSay("File terbaca tidak utuh", 2200); return 0; }
  BkHdr hh; bool isEnc = bkParseHdr(buf, n, hh) && hh.enc;
  uint8_t* pb = NULL; const uint8_t* src = buf; size_t sn = n;
  if (isEnc) {
    if (!pass) { free(buf); return -2; }
    pb = bkAlloc(n);
    if (!pb) { free(buf); bkSay("PSRAM tidak cukup", 2200); return 0; }
    size_t pl = 0;
    int dr = bkDecrypt(buf, n, (const uint8_t*)pass, strlen(pass), pb, n, &pl);
    free(buf); buf = NULL;
    if (dr == BK_DEC_PASS) { bkZero(pb, n); free(pb); return BK_DEC_PASS; }          // sandi salah ATAU file diubah
    if (dr != BK_DEC_OK) { bkZero(pb, n); free(pb); bkSay("Backup rusak, ditolak", 2600); return 0; }
    src = pb; sn = pl;
  }
  int fail = 0;
  int ok = bkApply(src, sn, &fail);
  if (pb) { bkZero(pb, n); free(pb); }
  if (buf) free(buf);
  if (ok < 0) { bkSay("Backup rusak, ditolak", 2600); return 0; }
  char m[44];
  if (fail) snprintf(m, sizeof(m), "Dipulihkan %d, gagal %d. Restart...", ok, fail);
  else      snprintf(m, sizeof(m), "Dipulihkan %d nilai. Restart...", ok);
  bkSay(m, 2600);
  bkRestartAt = millis() + 2200;                                   // drawBackup() melakukan restart
  return 0;
}

// ---------------------------------------------------------------------
// kotak sandi (overlay di atas keyboard sistem)
// ---------------------------------------------------------------------
static void bkPromptClose() {
  bkWipe(bkPassA); bkWipe(bkPassB);
  bkPrompt = 0; bkPromptIdx = -1; bkPromptErr[0] = 0;
  if (kbTarget == &bkPassA || kbTarget == &bkPassB) { kbVisible = false; kbTarget = nullptr; }
  needRedraw = true;
}
static void bkPromptOpen(int kind, int idx) {
  bkWipe(bkPassA); bkWipe(bkPassB);
  bkPrompt = kind; bkPromptIdx = idx; bkPromptErr[0] = 0;
  kbTarget = &bkPassA; kbVisible = true; kbMode = KB_LOWER;
  needRedraw = true;
}
static void bkPromptOk() {
  if (bkPassA.length() > 64) { strcpy(bkPromptErr, "Maksimal 64 karakter"); needRedraw = true; return; }
  if (bkPrompt == 1) {
    if (bkPassA.length() < BK_PASS_MIN) { strcpy(bkPromptErr, "Minimal 6 karakter"); needRedraw = true; return; }
    bkPrompt = 2; bkPromptErr[0] = 0; bkWipe(bkPassB); kbTarget = &bkPassB; kbVisible = true; needRedraw = true;
    return;
  }
  if (bkPrompt == 2) {
    if (!(bkPassA == bkPassB)) {                                    // salah ketik = backup tak bisa dibuka lagi -> wajib dua kali sama
      bkWipe(bkPassA); bkWipe(bkPassB); bkPrompt = 1; kbTarget = &bkPassA; kbVisible = true;
      strcpy(bkPromptErr, "Tidak sama, ulangi"); needRedraw = true; return;
    }
    String pw = bkPassA; bkPromptClose();
    bkDoCreate(pw.c_str()); bkWipe(pw);
    return;
  }
  if (bkPrompt == 3) {
    int idx = bkPromptIdx; String pw = bkPassA;
    if (idx < 0 || idx >= bkListN) { bkWipe(pw); bkPromptClose(); return; }
    int rc = bkDoRestore(idx, pw.c_str()); bkWipe(pw);
    if (rc == BK_DEC_PASS) { bkWipe(bkPassA); strcpy(bkPromptErr, "Sandi salah"); kbTarget = &bkPassA; kbVisible = true; needRedraw = true; return; }
    bkPromptClose();
  }
}

static void bkDoDelete(int idx) {
  char path[40]; bkMakeName(path, sizeof(path), bkNum[idx]);
  int num = bkNum[idx];
  bool ok = SD_MMC.remove(path);
  bkSel = -1; bkArmKind = 0;
  bkScan(false); bkClampScroll();
  char m[44]; snprintf(m, sizeof(m), ok ? "Backup #%03d dihapus" : "Gagal menghapus #%03d", num);
  bkSay(m, 1800);
}

// ---------------------------------------------------------------------
// geometri (dipakai bareng gambar & sentuh)
// ---------------------------------------------------------------------
static void bkSet(int r, int x, int y, int w, int h) { bkR[r][0] = x; bkR[r][1] = y; bkR[r][2] = w; bkR[r][3] = h; }
static bool bkIn(int r, int x, int y) {
  return x >= bkR[r][0] && x < bkR[r][0] + bkR[r][2] && y >= bkR[r][1] && y < bkR[r][1] + bkR[r][3];
}
static void bkLayout() {
  const int m = 8, bh = 34, tg = 34;
  const int y0 = 26 + 18;
  {                                                     // kotak sandi: di atas keyboard sistem
    const int top = 26, ch = 78, cw = SCR_W - 2 * m;
    bkSet(BKR_PBOX,    m + 8, top + 20, cw - 16, 24);
    int hw = (cw - 16 - 8) / 2;
    bkSet(BKR_PCANCEL, m + 8, top + 50, hw, 24);
    bkSet(BKR_POK,     m + 8 + hw + 8, top + 50, cw - 16 - hw - 8, 24);
    (void)ch;
  }
  // Pil "Kembali" disembunyikan di app ini (swipe tepi kiri) -> ruang dasar layar dipakai penuh.
  const int bot = backHidden() ? SCR_H - 18 : backY() - 4;
  if (SCR_W > SCR_H) {                                  // lanskap: sakelar selebar layar, lalu tombol kiri + daftar kanan
    const int tl = 30, bl = 30, gp = 6;                 // lanskap: dua sakelar -> baris & tombol dirampingkan
    int lw = 138, rx = m + lw + m, rw = SCR_W - rx - m;
    int y1 = y0 + 2 * tl + 4 + 8;
    bkSet(BKR_TOGGLE,  m, y0, SCR_W - 2 * m, tl);
    bkSet(BKR_ENC,     m, y0 + tl + 4, SCR_W - 2 * m, tl);
    bkSet(BKR_CREATE,  m, y1,                 lw, bl);
    bkSet(BKR_RESTORE, m, y1 + (bl + gp),     lw, bl);
    bkSet(BKR_DELETE,  m, y1 + 2 * (bl + gp), lw, bl);
    bkSet(BKR_LIST,    rx, y1 + 16, rw, bot - (y1 + 16));
  } else {                                              // potret: tumpuk, tombol aksi di dasar
    int w = SCR_W - 2 * m, bw = (w - 8) / 2, by = bot - bh;
    bkSet(BKR_CREATE,  m, y0,          w, bh);
    bkSet(BKR_TOGGLE,  m, y0 + bh + 8, w, tg);
    bkSet(BKR_ENC,     m, y0 + bh + 8 + tg + 4, w, tg);
    bkSet(BKR_RESTORE, m, by,          bw, bh);
    bkSet(BKR_DELETE,  m + bw + 8, by, w - bw - 8, bh);
    int ly = y0 + bh + 8 + 2 * tg + 4 + 8 + 16;         // 16 = baris judul "RIWAYAT"
    bkSet(BKR_LIST,    m, ly, w, by - 6 - ly);
  }
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
void backupEnter() {
  { Preferences p; p.begin(BK_NVS_NS, true); bkSecrets = p.getBool("sec", false); bkEnc = p.getBool("enc", false); p.end(); }
  bkPromptClose();
  bkSel = -1; bkScroll = 0; bkArmKind = 0; bkRestartAt = 0; bkDown = false; bkMsg[0] = 0;
  bkLayout();
  bkScan(false);
}
void backupExit() { bkDown = false; bkArmKind = 0; bkPromptClose(); }

// ---------------------------------------------------------------------
// gambar
// ---------------------------------------------------------------------
static void bkStamp(char* out, size_t n, uint32_t ep) {
  if (ep < 1600000000UL) { snprintf(out, n, "tanpa waktu"); return; }
  time_t tt = (time_t)ep; struct tm t; localtime_r(&tt, &t);
  snprintf(out, n, "%02d/%02d/%04d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
}

static void bkButton(LGFX_Sprite& s, int r, const char* label, uint16_t tint, uint16_t fg, uint8_t alpha) {
  drawGlassPanel(s, bkR[r][0], bkR[r][1], bkR[r][2], bkR[r][3], 12, tint, alpha);
  s.setTextSize(1); s.setTextColor(fg);
  s.setCursor(bkR[r][0] + bkR[r][2] / 2 - s.textWidth(label) / 2, bkR[r][1] + bkR[r][3] / 2 - 4);
  s.print(label);
}

static void bkToggleRow(LGFX_Sprite& s, int r, const char* l1, const char* l2, bool on) {
  int x = bkR[r][0], y = bkR[r][1], w = bkR[r][2], h = bkR[r][3];
  drawGlassCard(s, x, y, w, h, 12, T().surface, 215);
  int sw = 36, sh = 20, sx = x + w - 12 - sw, sy = y + (h - sh) / 2;
  int tw = sx - (x + 12) - 8;                                     // lebar teks yg tersedia (tidak boleh masuk ke sakelar)
  char t1[48], t2[48];
  strncpy(t1, l1, sizeof(t1) - 1); t1[sizeof(t1) - 1] = 0; strncpy(t2, l2, sizeof(t2) - 1); t2[sizeof(t2) - 1] = 0;
  for (int L = (int)strlen(t1); L > 1 && s.textWidth(t1) > tw; ) t1[--L] = 0;
  for (int L = (int)strlen(t2); L > 1 && s.textWidth(t2) > tw; ) t2[--L] = 0;
  int ty = (h >= 34) ? 7 : 4, ty2 = (h >= 34) ? 20 : 16;
  s.setTextColor(T().text);    s.setCursor(x + 12, y + ty);  s.print(t1);
  s.setTextColor(T().subtext); s.setCursor(x + 12, y + ty2); s.print(t2);
  nyxAaRR(s, sx, sy, sw, sh, 10, on ? T().accent : blend565(T().surface2, 0xFFFF, 60));
  nyxAaCircle(s, on ? sx + sw - 10 : sx + 10, sy + sh / 2, 8, 0xFFFF);
}

static void bkDrawPrompt(LGFX_Sprite& s) {
  const int m = 8, top = 26, ch = 78, cw = SCR_W - 2 * m;
  drawGlassCard(s, m, top, cw, ch, 12, T().surface, 250);
  char title[40];
  if (bkPromptErr[0]) { strncpy(title, bkPromptErr, sizeof(title) - 1); title[sizeof(title) - 1] = 0; }
  else if (bkPrompt == 1) strcpy(title, "Sandi backup (min 6 karakter)");
  else if (bkPrompt == 2) strcpy(title, "Ketik ulang sandi yang sama");
  else snprintf(title, sizeof(title), "Sandi untuk backup #%03d", (bkPromptIdx >= 0 && bkPromptIdx < bkListN) ? bkNum[bkPromptIdx] : 0);
  for (int L = (int)strlen(title); L > 1 && s.textWidth(title) > cw - 20; ) title[--L] = 0;
  s.setTextColor(bkPromptErr[0] ? T().danger : T().text); s.setCursor(m + 10, top + 7); s.print(title);
  drawGlassPanel(s, bkR[BKR_PBOX][0], bkR[BKR_PBOX][1], bkR[BKR_PBOX][2], bkR[BKR_PBOX][3], 8, T().surface2, 200);
  const String& cur = (bkPrompt == 2) ? bkPassB : bkPassA;
  char mask[40]; int n = (int)cur.length(); if (n > 30) n = 30;
  for (int i = 0; i < n; i++) mask[i] = '*';
  mask[n] = 0;
  s.setTextColor(T().text); s.setCursor(bkR[BKR_PBOX][0] + 8, bkR[BKR_PBOX][1] + 8); s.print(mask);
  bkButton(s, BKR_PCANCEL, "Batal", T().surface2, T().subtext, 190);
  bkButton(s, BKR_POK, bkPrompt == 1 ? "Lanjut" : (bkPrompt == 2 ? "Buat Backup" : "Pulihkan"), T().accent, 0xFFFF, 235);
}

void drawBackup(LGFX_Sprite& s) {
  bkLayout();
  if (bkRestartAt) { needRedraw = true; if ((long)(millis() - bkRestartAt) >= 0) ESP.restart(); }
  if (bkArmKind && (long)(millis() - bkArmUntil) >= 0) bkArmKind = 0;
  if (bkArmKind) needRedraw = true;                                  // label "Yakin?" kembali sendiri saat waktu habis
  bkClampScroll();

  iosBackdrop(s);
  drawStatusBar(s);
  iosTitle(s, 8, 26, "Backup & Restore");
  s.setTextSize(1);

  // ---- Buat Backup
  bkButton(s, BKR_CREATE, "Buat Backup Sekarang", T().accent, 0xFFFF, 235);

  // ---- sakelar: sandi & API key | enkripsi sandi
  bkToggleRow(s, BKR_TOGGLE, "Sertakan sandi & API key", bkEnc ? "File dienkripsi AES-256 dgn sandimu" : "File di SD tidak terenkripsi", bkSecrets);
  bkToggleRow(s, BKR_ENC, "Enkripsi dengan sandi", bkEnc ? "Sandi ditanya tiap backup, tidak disimpan" : "Backup bisa dibaca siapa saja yg pegang SD", bkEnc);

  // ---- judul daftar + pesan status
  {
    int lx = bkR[BKR_LIST][0], ly = bkR[BKR_LIST][1] - 14;
    nyxAaRR(s, lx + 2, ly + 1, 3, 9, 1, T().accent);
    s.setTextColor(blend565(T().subtext, T().text, 110)); s.setCursor(lx + 10, ly + 1); s.print("RIWAYAT");
    if (bkMsg[0]) {
      int mx = lx + 10 + s.textWidth("RIWAYAT") + 10, maxW = bkR[BKR_LIST][0] + bkR[BKR_LIST][2] - mx;
      char b[44]; strncpy(b, bkMsg, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
      int L = (int)strlen(b);
      while (L > 1 && s.textWidth(b) > maxW) b[--L] = 0;
      s.setTextColor(T().accent); s.setCursor(mx, ly + 1); s.print(b);
    }
  }

  // ---- daftar backup
  {
    int lx = bkR[BKR_LIST][0], ly = bkR[BKR_LIST][1], lw = bkR[BKR_LIST][2], lh = bkR[BKR_LIST][3];
    drawGlassPanel(s, lx, ly - 2, lw, lh + 4, 12, T().surface2, 150);
    s.setClipRect(lx, ly, lw, lh);
    if (bkListN == 0) {
      const char* e = sdReady ? "Belum ada backup" : "SD Card tidak terdeteksi";
      s.setTextColor(T().subtext); s.setCursor(lx + lw / 2 - s.textWidth(e) / 2, ly + lh / 2 - 4); s.print(e);
    }
    for (int i = 0; i < bkListN; i++) {
      int ry = ly + i * BK_ROW_H - (int)bkScroll;
      if (ry + BK_ROW_H < ly || ry > ly + lh) continue;
      bool sel = (i == bkSel);
      drawGlassCard(s, lx + 4, ry + 2, lw - 8, BK_ROW_H - 4, 10, sel ? blend565(T().surface, T().accent, 90) : T().surface, 215);
      if (sel) s.drawRoundRect(lx + 4, ry + 2, lw - 8, BK_ROW_H - 4, 10, T().accent);
      char a[48], b[48], t[20];
      if (bkValid[i]) {
        bkStamp(t, sizeof(t), bkEpoch[i]);
        snprintf(a, sizeof(a), "#%03d  %s", bkNum[i], t);
        snprintf(b, sizeof(b), "%u nilai - %.1f KB%s%s", (unsigned)bkCnt[i], bkSize[i] / 1024.0, bkEncF[i] ? " - terkunci" : "", (bkFlg[i] & BK_FLAG_SECRETS) ? " - +sandi" : "");
      } else {
        snprintf(a, sizeof(a), "#%03d  File rusak", bkNum[i]);
        snprintf(b, sizeof(b), "bukan backup NyxOS (%.1f KB)", bkSize[i] / 1024.0);
      }
      s.setTextColor(bkValid[i] ? T().text : T().danger); s.setCursor(lx + 14, ry + 7);  s.print(a);
      s.setTextColor(T().subtext);                        s.setCursor(lx + 14, ry + 19); s.print(b);
    }
    s.clearClipRect();
    int maxS = bkListN * BK_ROW_H - lh;
    if (maxS > 0) {                                                 // scrollbar tipis
      int th = lh * lh / (lh + maxS); if (th < 12) th = 12;
      int ty = ly + (int)((lh - th) * (bkScroll / (float)maxS));
      nyxAaRR(s, lx + lw - 5, ty, 2, th, 1, blend565(T().bg, T().text, 90));
    }
  }

  // ---- Pulihkan / Hapus
  {
    bool has = (bkSel >= 0 && bkSel < bkListN);
    bool canRestore = has && bkValid[bkSel];
    bool armR = (bkArmKind == 1), armD = (bkArmKind == 2);
    uint16_t dim = T().subtext;
    bkButton(s, BKR_RESTORE, armR ? "Yakin? Ketuk lagi" : "Pulihkan",
             armR ? T().danger : T().surface2, armR ? 0xFFFF : (canRestore ? T().accent : dim), armR ? 235 : 190);
    bkButton(s, BKR_DELETE, armD ? "Yakin? Ketuk lagi" : "Hapus",
             armD ? T().danger : T().surface2, armD ? 0xFFFF : (has ? T().danger : dim), armD ? 235 : 190);
  }

  if (bkPrompt) { bkDrawPrompt(s); if (kbVisible) drawKb(s); }
  else drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh: ketuk dieksekusi saat JARI DILEPAS (supaya geser-scroll tidak memicu tombol)
// ---------------------------------------------------------------------
static void bkTap(int x, int y) {
  if (bkIn(BKR_CREATE, x, y)) {
    bkArmKind = 0;
    if (bkEnc) { if (!sdReady) { bkSay("SD Card tidak terdeteksi", 2200); return; } bkPromptOpen(1, -1); }
    else bkDoCreate(NULL);
    return;
  }
  if (bkIn(BKR_ENC, x, y)) {
    bkEnc = !bkEnc;
    { Preferences p; p.begin(BK_NVS_NS, false); p.putBool("enc", bkEnc); p.end(); }
    bkSay(bkEnc ? "Backup berikutnya dienkripsi sandi" : "Backup berikutnya tanpa enkripsi", 1900);
    return;
  }
  if (bkIn(BKR_TOGGLE, x, y)) {
    bkSecrets = !bkSecrets;
    { Preferences p; p.begin(BK_NVS_NS, false); p.putBool("sec", bkSecrets); p.end(); }
    bkSay(bkSecrets ? "Sandi & API key ikut di-backup" : "Sandi & API key tidak ikut", 1800);
    return;
  }
  if (bkIn(BKR_LIST, x, y)) {
    int row = (y - bkR[BKR_LIST][1] + (int)bkScroll) / BK_ROW_H;
    if (row >= 0 && row < bkListN) bkSel = (bkSel == row) ? -1 : row;
    bkArmKind = 0; needRedraw = true;
    return;
  }
  if (bkIn(BKR_RESTORE, x, y)) {
    if (bkSel < 0 || bkSel >= bkListN) { bkSay("Pilih satu backup dulu", 1600); return; }
    if (!bkValid[bkSel]) { bkSay("File ini rusak, tidak bisa dipulihkan", 2200); return; }
    if (bkArmKind != 1) { bkArmKind = 1; bkArmUntil = millis() + 4000; bkSay("Ketuk lagi: pengaturan ditimpa & HP restart", 3600); return; }
    bkArmKind = 0;
    if (bkEncF[bkSel]) bkPromptOpen(3, bkSel);                       // file terkunci: minta sandi dulu
    else bkDoRestore(bkSel, NULL);
    return;
  }
  if (bkIn(BKR_DELETE, x, y)) {
    if (bkSel < 0 || bkSel >= bkListN) { bkSay("Pilih satu backup dulu", 1600); return; }
    if (bkArmKind != 2) { bkArmKind = 2; bkArmUntil = millis() + 4000; needRedraw = true; return; }
    bkArmKind = 0; bkDoDelete(bkSel);
    return;
  }
}

static void bkPromptTouch(int x, int y, bool isNew) {
  if (!isNew) return;
  if (kbVisible && y >= kbY() - 2) { kbTouch(x, y); needRedraw = true; return; }
  if (bkIn(BKR_PCANCEL, x, y)) { bkPromptClose(); return; }
  if (bkIn(BKR_POK, x, y)) { bkPromptOk(); return; }
  if (bkIn(BKR_PBOX, x, y)) { kbVisible = true; needRedraw = true; }      // keyboard tertutup lewat Enter -> buka lagi
}

void backupTouch(int x, int y, bool held, bool isNew) {
  if (bkRestartAt) return;                                           // sedang menunggu restart
  if (bkPrompt) { bkPromptTouch(x, y, isNew); return; }
  if (isNew) {
    if (isBack(x, y)) { navBack(); return; }
    bkDown = true; bkMoved = false; bkPx = x; bkPy = y; bkLy = y;
    bkDragList = bkIn(BKR_LIST, x, y);
    return;
  }
  if (held) {
    if (!bkDown) return;
    if (!bkMoved && (abs(x - bkPx) > 8 || abs(y - bkPy) > 8)) bkMoved = true;
    if (bkMoved && bkDragList) { bkScroll += (float)(bkLy - y); bkClampScroll(); needRedraw = true; }
    bkLy = y;
    return;
  }
  if (!bkDown) return;                                               // lepas
  bkDown = false;
  if (!bkMoved) bkTap(bkPx, bkPy);
}

// =====================================================================
// BACKUP OTOMATIS (tanpa menekan apa pun)
// =====================================================================
// NVS tetap tempat kerja semua fitur (dibaca ke RAM saat boot, cepat, tak butuh SD). Di atasnya:
//   * backupAutoPoll() : tiap BK_AUTO_EVERY_MS (dan sekali ~45 dtk setelah boot) seluruh NVS disisir
//                        ke /backup/auto.nbk. Ditulis HANYA bila isinya berubah (CRC beda), jadi SD
//                        tidak dipakai menulis terus-menerus.
//   * backupAutoBoot() : dipanggil sekali di setup() setelah initSD(). Kalau NVS ini belum pernah
//                        dikenali (penanda nyxbak/auto belum ada: flash baru di-erase / unit baru)
//                        dan /backup/auto.nbk ada, isinya dipulihkan lalu HP restart sekali.
// Sandi & API key ikut HANYA bila sakelar "Sertakan sandi & API key" nyala DAN enkripsi mati
// (berkas otomatis tidak bisa menanyakan sandi, jadi tidak pernah dienkripsi).
#define BK_AUTO_FILE      BK_DIR "/auto.nbk"
#define BK_AUTO_TMP       BK_DIR "/auto.tmp"
#define BK_AUTO_FIRST_MS  45000UL
#define BK_AUTO_EVERY_MS  (10UL * 60UL * 1000UL)

static bool bkAutoMarker(bool set) {
  Preferences p;
  if (set) { if (!p.begin(BK_NVS_NS, false)) return false; p.putBool("auto", true); p.end(); return true; }
  if (!p.begin(BK_NVS_NS, true)) return false;           // namespace belum ada = NVS baru
  bool v = p.getBool("auto", false); p.end();
  return v;
}

static bool bkAutoTail(const char* path, uint32_t* crc) {   // CRC32 ada di 4 byte terakhir berkas
  if (!SD_MMC.exists(path)) return false;
  File g = SD_MMC.open(path, FILE_READ);
  if (!g) return false;
  size_t n = (size_t)g.size(); uint8_t b[4]; bool ok = false;
  if (n >= BK_HDR + 5 && g.seek(n - 4) && g.read(b, 4) == 4) {
    *crc = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); ok = true;
  }
  g.close();
  return ok;
}

static void bkAutoSave() {
  if (!sdReady) return;
  uint8_t* buf = bkAlloc(BK_BUF_MAX);
  if (!buf) return;                                       // PSRAM tidak cukup: coba lagi nanti
  if (!bkAutoMarker(false)) bkAutoMarker(true);
  bool sec = false, enc = false;
  { Preferences p; if (p.begin(BK_NVS_NS, true)) { sec = p.getBool("sec", false); enc = p.getBool("enc", false); p.end(); } }
  size_t len = 0; int cnt = 0;
  if (bkCollect(buf, BK_BUF_MAX, sec && !enc, &len, &cnt) == 0 && len > BK_HDR + 4) {
    uint32_t nc = (uint32_t)buf[len - 4] | ((uint32_t)buf[len - 3] << 8) | ((uint32_t)buf[len - 2] << 16) | ((uint32_t)buf[len - 1] << 24);
    uint32_t oc = 0;
    if (!(bkAutoTail(BK_AUTO_FILE, &oc) && oc == nc)) {   // berubah (atau belum ada) -> tulis
      if (!SD_MMC.exists(BK_DIR)) SD_MMC.mkdir(BK_DIR);
      bool ok = false;
      File f = SD_MMC.open(BK_AUTO_TMP, FILE_WRITE);
      if (f) { ok = (f.write(buf, len) == len); f.close(); }
      if (ok) {                                           // baca ulang & cek CRC sebelum menggantikan berkas lama
        ok = false;
        File g = SD_MMC.open(BK_AUTO_TMP, FILE_READ);
        if (g && (size_t)g.size() == len) {
          uint8_t* chk = bkAlloc(len);
          if (chk) { BkHdr h; ok = (g.read(chk, len) == len) && bkVerify(chk, len, h); free(chk); }
        }
        if (g) g.close();
      }
      if (ok) {
        if (SD_MMC.exists(BK_AUTO_FILE)) SD_MMC.remove(BK_AUTO_FILE);
        ok = SD_MMC.rename(BK_AUTO_TMP, BK_AUTO_FILE);
      }
      if (!ok) SD_MMC.remove(BK_AUTO_TMP);
      Serial.printf("[BACKUP-AUTO] %s (%d nilai, %u byte)\n", ok ? "tersimpan" : "GAGAL menulis", cnt, (unsigned)len);
    }
  }
  bkZero(buf, BK_BUF_MAX); free(buf);                     // isi bisa memuat sandi -> timpa nol
}

void backupAutoPoll() {
  static uint32_t next = 0;
  if (!sdReady || bkRestartAt) return;
  uint32_t now = millis();
  if (next == 0) { next = now + BK_AUTO_FIRST_MS; return; }
  if ((int32_t)(now - next) < 0) return;
  next = now + BK_AUTO_EVERY_MS;
  bkAutoSave();
}

void backupAutoBoot() {
  if (!sdReady) return;
  if (bkAutoMarker(false)) return;                        // NVS ini sudah dikenal: tidak ada yang dipulihkan
  const char* cand[2] = { BK_AUTO_FILE, BK_AUTO_TMP };
  for (int i = 0; i < 2; i++) {
    if (!SD_MMC.exists(cand[i])) continue;
    File f = SD_MMC.open(cand[i], FILE_READ);
    if (!f) continue;
    size_t n = (size_t)f.size();
    if (n < BK_HDR + 5 || n > BK_BUF_MAX) { f.close(); continue; }
    uint8_t* buf = bkAlloc(n);
    if (!buf) { f.close(); continue; }
    bool rd = (f.read(buf, n) == n); f.close();
    int fail = 0, ok = rd ? bkApply(buf, n, &fail) : -1;  // bkApply memeriksa CRC penuh & menolak berkas rusak
    bkZero(buf, n); free(buf);
    if (ok > 0) {
      bkAutoMarker(true);
      Serial.printf("[BACKUP-AUTO] NVS kosong: %d nilai dipulihkan dari %s (gagal %d). Restart...\n", ok, cand[i], fail);
      delay(200);
      ESP.restart();
      return;
    }
  }
  bkAutoMarker(true);                                     // tidak ada yang bisa dipulihkan: tandai supaya tidak dicoba lagi
}
