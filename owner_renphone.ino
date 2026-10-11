// =====================================================================
// owner_renphone.ino -- PROFIL PEMILIK (foto + nama) untuk Accretion Phone / NyxOS
// =====================================================================
// Seperti HP modern: kartu pemilik (foto bulat + nama) di paling atas Pengaturan.
// Ketuk kartunya -> layar "Profil" (SCR_OWNER), isinya:
//   * Pilih foto dari SD  -> membuka Galeri dalam MODE PILIH; ketuk satu foto,
//                            selesai (balik otomatis ke Profil).
//   * Ubah nama           -> keyboard layar (ketuk di luar keyboard = simpan).
//   * Hapus foto          -> ketuk 2x.
// Foto juga bisa dijadikan profil dari File Explorer: buka gambarnya (terbuka di
// Galeri), lalu ketuk tombol "Profil" di bar bawah viewer.
// Upload lewat web File Manager tetap bisa (taruh gambar di SD, lalu pilih seperti biasa).
//
// CARA KERJA FOTO:
//   * Gambar (JPG / PNG / BMP) dipotong persegi dari tengah dan diperkecil jadi
//     72x72 memakai generator thumbnail Galeri (galGenerate) -- JPEG didecode
//     berskala 1/8..1/2, jadi cepat dan hemat RAM.
//   * Hasilnya (RGB565, ±10 KB) disimpan di DUA tempat: flash internal FFat
//     (/owner_avatar.bin, tetap ada walau SD dicabut) dan SD (/.owner/avatar.bin).
//   * Tampil sebagai lingkaran lewat ownerDrawAvatar() -- diskala nearest-neighbor
//     per baris, tanpa alokasi heap.
//   * Buffer foto 1x di PSRAM (±10 KB), RAM internal tidak tersentuh.
//   * Nama disimpan di NVS (namespace "nyxowner"), maks 20 karakter.
//
// ATURAN PROYEK YANG DIIKUTI:
//   - Fungsi publik hanya menerima tipe primitif / LGFX_Sprite& (bug prototype arduino-cli).
//   - File ini harus dibangun SETELAH gallery_renphone.ino (urutan alfabetis Arduino
//     sudah menjamin: g < o) karena memakai galGenerate()/galKindOf().
//   - Deklarasi maju fungsi publik ada di phone.ino (dipasang apply_owner_patch.py).
// =====================================================================

#define OWNER_AV           GAL_THUMB          // 72 -- sama dgn thumbnail Galeri
#define OWNER_AV_PX        (OWNER_AV * OWNER_AV)
#define OWNER_AV_BYTES     (OWNER_AV_PX * 2)
#define OWNER_NAME_MAX     20
#define OWNER_DEFAULT_NAME "Pemilik"
#define OWNER_SD_DIR       "/.owner"
#define OWNER_SD_FILE      "/.owner/avatar.bin"
#define OWNER_FF_FILE      "/owner_avatar.bin"
#define OWNER_MAX_R        60

static uint16_t* ownerAv        = nullptr;     // PSRAM, OWNER_AV x OWNER_AV RGB565
static bool      ownerHasAv     = false;
static bool      ownerLoaded    = false;
static char      ownerNameBuf[OWNER_NAME_MAX + 1] = OWNER_DEFAULT_NAME;
static String    ownerNameEdit;                // hanya terisi saat mengetik
static bool      ownerEditing   = false;
static bool      ownerPickMode  = false;       // true selagi Galeri dibuka utk memilih foto profil
static bool      ownerPickDone  = false;       // foto sudah dipilih -> tutup Galeri di loop berikutnya
static unsigned long ownerDelArmUntil = 0;

// ---------------------------------------------------------------------
// penyimpanan
// ---------------------------------------------------------------------
static bool ownerAllocBuf() {
  if (ownerAv) return true;
  ownerAv = (uint16_t*)heap_caps_malloc(OWNER_AV_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ownerAv != nullptr;
}

static bool ownerReadFrom(fs::FS& fsys, const char* path) {
  File f = fsys.open(path, FILE_READ);
  if (!f) return false;
  uint8_t h[4];
  bool ok = (f.read(h, 4) == 4) && h[0] == 'N' && h[1] == 'A' && h[2] == 1 && h[3] == (uint8_t)OWNER_AV
            && (size_t)f.size() == (size_t)(4 + OWNER_AV_BYTES);
  if (ok) ok = (f.read((uint8_t*)ownerAv, OWNER_AV_BYTES) == (int)OWNER_AV_BYTES);
  f.close();
  return ok;
}

static void ownerWriteTo(fs::FS& fsys, const char* path) {
  File f = fsys.open(path, FILE_WRITE);
  if (!f) return;
  uint8_t h[4] = { 'N', 'A', 1, (uint8_t)OWNER_AV };
  f.write(h, 4);
  f.write((const uint8_t*)ownerAv, OWNER_AV_BYTES);
  f.close();
}

static void ownerEnsureLoaded() {
  if (ownerLoaded) return;
  ownerLoaded = true;
  Preferences p; p.begin("nyxowner", true);
  String n = p.getString("name", "");
  p.end();
  if (n.length() > 0) { strncpy(ownerNameBuf, n.c_str(), OWNER_NAME_MAX); ownerNameBuf[OWNER_NAME_MAX] = 0; }
  if (!ownerAllocBuf()) return;
  if (ffatReady && ownerReadFrom(FFat, OWNER_FF_FILE)) ownerHasAv = true;           // flash dulu: cepat & tidak tergantung SD
  else if (sdReady && ownerReadFrom(SD_MMC, OWNER_SD_FILE)) ownerHasAv = true;
}

static void ownerSaveAvatar() {
  if (!ownerAv || !ownerHasAv) return;
  if (ffatReady) ownerWriteTo(FFat, OWNER_FF_FILE);
  if (sdReady) { SD_MMC.mkdir(OWNER_SD_DIR); ownerWriteTo(SD_MMC, OWNER_SD_FILE); }
}

static void ownerClearAvatar() {
  ownerHasAv = false;
  if (ffatReady && FFat.exists(OWNER_FF_FILE)) FFat.remove(OWNER_FF_FILE);
  if (sdReady && SD_MMC.exists(OWNER_SD_FILE)) SD_MMC.remove(OWNER_SD_FILE);
}

// ---------------------------------------------------------------------
// API publik (dipakai Pengaturan, About Phone, Galeri)
// ---------------------------------------------------------------------
bool ownerHasAvatar() { ownerEnsureLoaded(); return ownerHasAv; }

const char* ownerName() { ownerEnsureLoaded(); return ownerNameBuf; }

// Buat foto profil dari file gambar di SD. true = berhasil.
bool ownerSetAvatarFromFile(const char* path) {
  ownerEnsureLoaded();
  if (!sdReady || !ownerAv || !path || path[0] != '/') return false;
  int kind = galKindOf(path);
  if (kind < 0 || kind == 2) return false;                 // bukan gambar / video
  uint16_t* tmp = (uint16_t*)heap_caps_malloc(OWNER_AV_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!tmp) return false;
  bool ok = galGenerate(path, (uint8_t)kind, tmp);         // potong tengah + perkecil 72x72
  if (ok) {
    memcpy(ownerAv, tmp, OWNER_AV_BYTES);
    ownerHasAv = true;
    ownerSaveAvatar();
  }
  heap_caps_free(tmp);
  return ok;
}

// Versi dengan umpan balik (toast + getar) -- dipanggil dari tombol "Profil" di viewer Galeri.
void ownerSetAvatarUi(const char* path) {
  if (ownerSetAvatarFromFile(path)) { showToast("Foto profil diperbarui"); vibSuccess(); }
  else showToast("Gagal memakai gambar ini");
  needRedraw = true;
}

// Lingkaran foto (atau inisial kalau belum ada foto) berpusat di (cx,cy), jari-jari r.
void ownerDrawAvatar(LGFX_Sprite& s, int cx, int cy, int r) {
  ownerEnsureLoaded();
  if (r > OWNER_MAX_R) r = OWNER_MAX_R;
  if (r < 6) r = 6;
  if (ownerHasAv && ownerAv) {
    uint16_t line[OWNER_MAX_R * 2 + 2];
    int D = r * 2;
    for (int dy = 0; dy < D; dy++) {
      float fy = (float)dy + 0.5f - (float)r;
      float hw2 = (float)r * (float)r - fy * fy;
      if (hw2 <= 0) continue;
      int hw = (int)(sqrtf(hw2) + 0.5f);
      int x0 = r - hw, x1 = r + hw;
      if (x1 > D) x1 = D;
      int w = x1 - x0;
      if (w <= 0) continue;
      const uint16_t* srow = ownerAv + (dy * OWNER_AV / D) * OWNER_AV;
      for (int i = 0; i < w; i++) line[i] = srow[(x0 + i) * OWNER_AV / D];
      s.pushImage(cx - r + x0, cy - r + dy, w, 1, (lgfx::swap565_t*)line);
    }
    s.drawCircle(cx, cy, r, blend565(T().bg, T().text, 70));     // cincin tipis: merapikan tepi
  } else {
    s.fillCircle(cx, cy, r, blend565(T().bg, T().accent, 150));
    char ini[2] = { '?', 0 };
    for (const char* c = ownerNameBuf; *c; c++) {
      if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')) {
        ini[0] = (*c >= 'a' && *c <= 'z') ? (char)(*c - 32) : *c; break;
      }
    }
    int sz = (r >= 30) ? 4 : (r >= 18) ? 3 : 2;
    s.setTextSize(sz); s.setTextColor(T().text);
    s.setCursor(cx - s.textWidth(ini) / 2, cy - 4 * sz);
    s.print(ini);
    s.setTextSize(1);
  }
}

// ---- mode pilih (Galeri) ----
bool ownerPickActive() { return ownerPickMode; }
void ownerPickReset()  { ownerPickMode = false; }

void ownerPickChosen(const char* path) {
  if (ownerSetAvatarFromFile(path)) {
    showToast("Foto profil diperbarui"); vibSuccess();
    ownerPickMode = false;
    ownerPickDone = true;                    // tutup Galeri di putaran loop berikutnya (bukan di tengah pemrosesan sentuhan Galeri)
  } else {
    showToast("Gagal memakai gambar ini");
  }
  needRedraw = true;
}

// dipanggil tiap putaran loop() -- murah (hanya cek flag)
void ownerLoopPoll() {
  if (!ownerPickDone) return;
  ownerPickDone = false;
  if (curScreen() == SCR_GALLERY) navBack();
}

// ---------------------------------------------------------------------
// ubah nama
// ---------------------------------------------------------------------
static void ownerCommitName() {
  String n = ownerNameEdit;
  n.replace("\n", " "); n.replace("\r", " ");
  n.trim();
  if (n.length() == 0) n = OWNER_DEFAULT_NAME;
  strncpy(ownerNameBuf, n.c_str(), OWNER_NAME_MAX); ownerNameBuf[OWNER_NAME_MAX] = 0;
  Preferences p; p.begin("nyxowner", false); p.putString("name", ownerNameBuf); p.end();
  ownerEditing = false;
  kbVisible = false; kbTarget = nullptr;
  ownerNameEdit = "";
  showToast("Nama disimpan");
  needRedraw = true;
}

// ---------------------------------------------------------------------
// layar Profil
// ---------------------------------------------------------------------
static void ownerGeom(int& acx, int& acy, int& ar, int& nameY, int& nameMaxW,
                      int& bx, int& by0, int& bw, int& bh, int& gap) {
  ar = 36; bh = 30; gap = 6;
  if (SCR_W > SCR_H) {                          // landscape: foto di kiri, tombol di kanan
    int leftW = 140;
    acx = 8 + leftW / 2; acy = 96;
    nameY = acy + ar + 10; nameMaxW = leftW - 4;
    bx = 8 + leftW + 6; bw = SCR_W - bx - 8; by0 = 52; bh = 34; gap = 8;
  } else {                                      // portrait: foto di atas, tombol ditumpuk di bawah
    acx = SCR_W / 2; acy = 82;
    nameY = acy + ar + 8; nameMaxW = SCR_W - 16;
    bx = 8; bw = SCR_W - 16; by0 = 176;
  }
}

static void ownerBtn(LGFX_Sprite& s, int x, int y, int w, int h, const char* lb, uint16_t tint, uint16_t txt) {
  drawGlassPanel(s, x, y, w, h, 10, tint, 170);
  s.setTextSize(1); s.setTextColor(txt);
  s.setCursor(x + w / 2 - s.textWidth(lb) / 2, y + h / 2 - 4);
  s.print(lb);
}

void ownerEnter() {
  ownerEnsureLoaded();
  ownerEditing = false;
  ownerNameEdit = "";
  ownerDelArmUntil = 0;
}

void ownerExit() {
  if (ownerEditing) ownerCommitName();
  ownerDelArmUntil = 0;
}

void drawOwner(LGFX_Sprite& s) {
  iosBackdrop(s); drawStatusBar(s);

  if (ownerEditing) {
    iosTitle(s, 8, 26, "Ubah nama");
    int fx = 8, fy = 50, fw = SCR_W - 16, fh = 36;
    drawGlassPanel(s, fx, fy, fw, fh, 10, T().surface2, 190);
    String t = ownerNameEdit + "|";
    int tsz = 2;
    s.setTextSize(2);
    if (s.textWidth(t.c_str()) > fw - 20) { tsz = 1; s.setTextSize(1); }
    s.setTextColor(T().text);
    s.setCursor(fx + 10, fy + fh / 2 - 4 * tsz);
    s.print(t.c_str());
    s.setTextSize(1);
    s.setTextColor(T().subtext);
    const char* hint = "Ketuk di luar keyboard = simpan";
    s.setCursor(SCR_W / 2 - s.textWidth(hint) / 2, fy + fh + 8);
    s.print(hint);
    drawKb(s);
    drawToast(s);
    return;
  }

  iosTitle(s, 8, 26, "Profil");
  int acx, acy, ar, nameY, nameMaxW, bx, by0, bw, bh, gap;
  ownerGeom(acx, acy, ar, nameY, nameMaxW, bx, by0, bw, bh, gap);

  ownerDrawAvatar(s, acx, acy, ar);

  char nm[OWNER_NAME_MAX + 1];
  strncpy(nm, ownerNameBuf, OWNER_NAME_MAX); nm[OWNER_NAME_MAX] = 0;
  s.setTextSize(2);
  int n = (int)strlen(nm);
  while (n > 1 && s.textWidth(nm) > nameMaxW) nm[--n] = 0;
  s.setTextColor(T().text);
  s.setCursor(acx - s.textWidth(nm) / 2, nameY);
  s.print(nm);
  s.setTextSize(1);
  s.setTextColor(T().subtext);
  const char* sub = "Pemilik perangkat";
  s.setCursor(acx - s.textWidth(sub) / 2, nameY + 22);
  s.print(sub);

  bool armed = millis() < ownerDelArmUntil;
  ownerBtn(s, bx, by0,                bw, bh, "Pilih foto dari SD", T().accent,   T().text);
  ownerBtn(s, bx, by0 + (bh + gap),   bw, bh, "Ubah nama",         T().surface2, T().text);
  ownerBtn(s, bx, by0 + 2 * (bh + gap), bw, bh, armed ? "Yakin?" : "Hapus foto",
           armed ? T().danger : T().surface2, armed ? 0xFFFF : T().danger);
  if (armed) needRedraw = true;                 // supaya label kembali sendiri saat waktu habis

  drawBack(s);
  drawToast(s);
}

void ownerTouch(int x, int y, bool held, bool isNew) {
  (void)held;
  if (!isNew) return;

  if (ownerEditing) {
    if (y < kbY() - 2) {
      ownerCommitName();
    } else {
      kbTouch(x, y);
      if ((int)ownerNameEdit.length() > OWNER_NAME_MAX) ownerNameEdit.remove(OWNER_NAME_MAX);
    }
    needRedraw = true;
    return;
  }

  if (isBack(x, y)) { navBack(); return; }

  int acx, acy, ar, nameY, nameMaxW, bx, by0, bw, bh, gap;
  ownerGeom(acx, acy, ar, nameY, nameMaxW, bx, by0, bw, bh, gap);
  int idx = -1;
  for (int i = 0; i < 3; i++) {
    int by = by0 + i * (bh + gap);
    if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) { idx = i; break; }
  }
  if (idx < 0) return;

  if (idx == 0) {                                // Pilih foto dari SD (via Galeri mode pilih)
    if (!sdReady) { showToast("SD Card tidak terdeteksi"); needRedraw = true; return; }
    ownerPickMode = true;
    navPush(SCR_GALLERY);
    showToast("Pilih satu foto untuk profil");
  } else if (idx == 1) {                         // Ubah nama
    ownerNameEdit = String(ownerNameBuf);
    ownerEditing = true;
    kbTarget = &ownerNameEdit; kbVisible = true; kbMode = KB_UPPER;
    needRedraw = true;
  } else {                                       // Hapus foto (ketuk 2x)
    if (!ownerHasAv) { showToast("Belum ada foto profil"); needRedraw = true; return; }
    if (millis() < ownerDelArmUntil) {
      ownerDelArmUntil = 0;
      ownerClearAvatar();
      showToast("Foto profil dihapus");
    } else {
      ownerDelArmUntil = millis() + 2500;
      showToast("Ketuk lagi untuk hapus foto");
    }
    needRedraw = true;
  }
}
