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
#include "backup_core.h"

#define BK_DIR        "/backup"
#define BK_KEEP       6                 // jumlah file backup yang disimpan di SD
#define BK_BUF_MAX    (96 * 1024)       // batas ukuran satu file backup
#define BK_VALUE_MAX  (32 * 1024)       // nilai NVS lebih besar dari ini dilewati
#define BK_ROW_H      36
#define BK_NVS_NS     "nyxbak"

enum { BKR_CREATE = 0, BKR_TOGGLE, BKR_LIST, BKR_RESTORE, BKR_DELETE, BKR_N };

static bool          bkSecrets   = false;
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
  if (!sdReady) return;
  File d = SD_MMC.open(BK_DIR);
  if (!d) return;
  if (!d.isDirectory()) { d.close(); return; }
  int nums[100]; int n = 0;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    if (!e.isDirectory()) { int k = bkParseName(e.name()); if (k >= 0 && n < 100) nums[n++] = k; }
    e.close();
  }
  d.close();
  for (int i = 1; i < n; i++) { int v = nums[i], j = i - 1; while (j >= 0 && nums[j] < v) { nums[j + 1] = nums[j]; j--; } nums[j + 1] = v; }   // terbaru dulu
  if (prune) {
    for (int i = BK_KEEP; i < n; i++) { char p[40]; bkMakeName(p, sizeof(p), nums[i]); SD_MMC.remove(p); }
  }
  for (int i = 0; i < n && i < BK_KEEP; i++) {
    char p[40]; bkMakeName(p, sizeof(p), nums[i]);
    bkNum[i] = nums[i]; bkValid[i] = false; bkSize[i] = 0; bkEpoch[i] = 0; bkCnt[i] = 0; bkFlg[i] = 0;
    File f = SD_MMC.open(p, FILE_READ);
    if (f) {
      bkSize[i] = (uint32_t)f.size();
      uint8_t hb[BK_HDR];
      if (f.read(hb, BK_HDR) == BK_HDR) {
        BkHdr h;
        if (bkParseHdr(hb, BK_HDR, h)) { bkValid[i] = true; bkEpoch[i] = h.epoch; bkCnt[i] = h.count; bkFlg[i] = h.flags; }
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
static void bkDoCreate() {
  if (!sdReady) { bkSay("SD Card tidak terdeteksi", 2200); return; }
  uint8_t* buf = bkAlloc(BK_BUF_MAX);
  if (!buf) { bkSay("PSRAM tidak cukup", 2200); return; }
  size_t len = 0; int cnt = 0;
  int rc = bkCollect(buf, BK_BUF_MAX, bkSecrets, &len, &cnt);
  if (rc == 1) { free(buf); bkSay("Pengaturan terlalu besar", 2400); return; }
  if (rc != 0) { free(buf); bkSay("Gagal membaca pengaturan", 2400); return; }
  if (!SD_MMC.exists(BK_DIR)) SD_MMC.mkdir(BK_DIR);
  bkScan(false);
  int next = bkListN ? bkNum[0] + 1 : 1;
  char path[40]; bkMakeName(path, sizeof(path), next);
  File f = SD_MMC.open(path, FILE_WRITE);
  bool ok = false;
  if (f) { ok = (f.write(buf, len) == len); f.close(); }
  if (ok) {                                                       // baca ulang & cek CRC supaya yakin utuh
    File g = SD_MMC.open(path, FILE_READ);
    ok = false;
    if (g && (size_t)g.size() == len) {
      uint8_t* chk = bkAlloc(len);
      if (chk) { ok = (g.read(chk, len) == len); BkHdr h; ok = ok && bkVerify(chk, len, h); free(chk); }
    }
    if (g) g.close();
  }
  free(buf);
  if (!ok) { SD_MMC.remove(path); bkSay("Gagal menulis ke SD", 2400); return; }
  bkScan(true);
  bkSel = 0; bkScroll = 0; bkArmKind = 0;
  char m[44]; snprintf(m, sizeof(m), "Backup #%03d dibuat (%d nilai)", next, cnt);
  bkSay(m, 2400);
}

static void bkDoRestore(int idx) {
  char path[40]; bkMakeName(path, sizeof(path), bkNum[idx]);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { bkSay("File tidak bisa dibuka", 2200); return; }
  size_t n = (size_t)f.size();
  if (n < BK_HDR + 5 || n > BK_BUF_MAX) { f.close(); bkSay("Ukuran file tidak wajar", 2200); return; }
  uint8_t* buf = bkAlloc(n);
  if (!buf) { f.close(); bkSay("PSRAM tidak cukup", 2200); return; }
  size_t got = f.read(buf, n); f.close();
  if (got != n) { free(buf); bkSay("File terbaca tidak utuh", 2200); return; }
  int fail = 0;
  int ok = bkApply(buf, n, &fail);
  free(buf);
  if (ok < 0) { bkSay("Backup rusak, ditolak", 2600); return; }
  char m[44];
  if (fail) snprintf(m, sizeof(m), "Dipulihkan %d, gagal %d. Restart...", ok, fail);
  else      snprintf(m, sizeof(m), "Dipulihkan %d nilai. Restart...", ok);
  bkSay(m, 2600);
  bkRestartAt = millis() + 2200;                                   // drawBackup() melakukan restart
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
  const int m = 8, bh = 32, tg = 36;
  const int y0 = 26 + 18;
  const int bot = backY() - 4;
  if (SCR_W > SCR_H) {                                  // lanskap: kontrol di kiri, daftar di kanan
    int lw = (SCR_W - 3 * m) / 2, rx = m + lw + m, rw = SCR_W - rx - m;
    bkSet(BKR_CREATE,  m, y0,                 lw, bh);
    bkSet(BKR_TOGGLE,  m, y0 + bh + 8,        lw, tg);
    bkSet(BKR_RESTORE, m, y0 + bh + 8 + tg + 14, lw, bh);
    bkSet(BKR_DELETE,  m, y0 + 2 * bh + 8 + tg + 14 + 8, lw, bh);
    bkSet(BKR_LIST,    rx, y0 + 16, rw, bot - (y0 + 16));
  } else {                                              // potret: tumpuk, tombol aksi di dasar
    int w = SCR_W - 2 * m, bw = (w - 8) / 2, by = bot - bh;
    bkSet(BKR_CREATE,  m, y0,          w, bh);
    bkSet(BKR_TOGGLE,  m, y0 + bh + 8, w, tg);
    bkSet(BKR_RESTORE, m, by,          bw, bh);
    bkSet(BKR_DELETE,  m + bw + 8, by, w - bw - 8, bh);
    int ly = y0 + bh + 8 + tg + 8 + 16;                 // 16 = baris judul "RIWAYAT"
    bkSet(BKR_LIST,    m, ly, w, by - 6 - ly);
  }
}

// ---------------------------------------------------------------------
// siklus app
// ---------------------------------------------------------------------
void backupEnter() {
  { Preferences p; p.begin(BK_NVS_NS, true); bkSecrets = p.getBool("sec", false); p.end(); }
  bkSel = -1; bkScroll = 0; bkArmKind = 0; bkRestartAt = 0; bkDown = false; bkMsg[0] = 0;
  bkScan(false);
}
void backupExit() { bkDown = false; bkArmKind = 0; }

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

  // ---- sakelar sandi
  {
    int x = bkR[BKR_TOGGLE][0], y = bkR[BKR_TOGGLE][1], w = bkR[BKR_TOGGLE][2], h = bkR[BKR_TOGGLE][3];
    drawGlassCard(s, x, y, w, h, 12, T().surface, 215);
    s.setTextColor(T().text);    s.setCursor(x + 12, y + 7);  s.print("Sertakan sandi & API key");
    s.setTextColor(T().subtext); s.setCursor(x + 12, y + 20); s.print("File di SD tidak terenkripsi");
    int sw = 36, sh = 20, sx = x + w - 12 - sw, sy = y + (h - sh) / 2;
    nyxAaRR(s, sx, sy, sw, sh, 10, bkSecrets ? T().accent : blend565(T().surface2, 0xFFFF, 60));
    nyxAaCircle(s, bkSecrets ? sx + sw - 10 : sx + 10, sy + sh / 2, 8, 0xFFFF);
  }

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
        snprintf(b, sizeof(b), "%u nilai - %.1f KB%s", (unsigned)bkCnt[i], bkSize[i] / 1024.0, (bkFlg[i] & BK_FLAG_SECRETS) ? " - +sandi" : "");
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

  drawBack(s);
  drawToast(s);
}

// ---------------------------------------------------------------------
// sentuh: ketuk dieksekusi saat JARI DILEPAS (supaya geser-scroll tidak memicu tombol)
// ---------------------------------------------------------------------
static void bkTap(int x, int y) {
  if (bkIn(BKR_CREATE, x, y)) { bkArmKind = 0; bkDoCreate(); return; }
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
    bkArmKind = 0; bkDoRestore(bkSel);
    return;
  }
  if (bkIn(BKR_DELETE, x, y)) {
    if (bkSel < 0 || bkSel >= bkListN) { bkSay("Pilih satu backup dulu", 1600); return; }
    if (bkArmKind != 2) { bkArmKind = 2; bkArmUntil = millis() + 4000; needRedraw = true; return; }
    bkArmKind = 0; bkDoDelete(bkSel);
    return;
  }
}

void backupTouch(int x, int y, bool held, bool isNew) {
  if (bkRestartAt) return;                                           // sedang menunggu restart
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
