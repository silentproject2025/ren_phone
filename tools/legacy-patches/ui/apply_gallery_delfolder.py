#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_gallery_delfolder.py -- fitur HAPUS FOLDER di Galeri.

Cara pakai: buka sebuah album (folder) -> tombol kecil "Hapus" muncul di kanan atas (sebelah info
"N foto"). Ketuk -> tombol jadi merah "Yakin?" + toast jumlah file; ketuk lagi dalam 3 dtk -> terhapus.
(Pola yang sama dgn tombol "Cache" & Hapus foto: ketuk 2x.)

Perilaku:
 - Yang dihapus: SEMUA file media di folder itu (JPG/JPEG/PNG/BMP/MJPEG), termasuk yang melewati batas
   1000 item yg tampil di grid. Lalu folder dihapus kalau sudah kosong.
 - File non-media (mis. .json, .avi, subfolder, file tersembunyi) TIDAK disentuh. Kalau ada,
   folder dibiarkan dan toast bilang "folder masih ada isi lain".
 - Folder root ("SD Card") tidak punya tombol Hapus. Tombol juga tidak muncul saat memilih foto profil.
 - Penghapusan berjalan bertahap (4 file per putaran loop) dgn indikator "Menghapus N/M" dan
   sentuhan diabaikan selama berjalan, jadi UI tidak membeku & tidak ada ketukan nyasar.
 - Cache thumbnail tiap file ikut dibersihkan (galForgetFile). Selesai -> kembali ke daftar album
   yg dipindai ulang otomatis.
Menyentuh gallery_renphone.ino saja. Idempotent & atomik.
"""
import sys, os
P = "gallery_renphone.ino"
if not os.path.exists(P): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % P)
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
if "GAL_DELDIR_V1" in s: print("[skip] sudah dipatch"); sys.exit(0)

def rep(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit("ERROR [%s]: blok tidak ketemu / tidak unik (%d)\n%s\nDibatalkan." % (label, s.count(old), old[:90]))
    s = s.replace(old, new)

HELPERS = r'''// =====================================================================
// GAL_DELDIR_V1: HAPUS FOLDER (album) -- bertahap, 4 file per putaran loop
// =====================================================================
static uint32_t galDirDelArmUntil = 0;   // tombol "Hapus" sedang "Yakin?" sampai waktu ini
static bool     galDelBusy = false;      // sedang menghapus -> sentuhan diabaikan
static int      galDelTotal = 0, galDelDone = 0, galDelFail = 0;
static char     galDelDir[64];

static bool galDirDelOk() {
  return galMode == GM_GRID && !galDelBusy && sdReady && galCurDir[0] && strcmp(galCurDir, "/") != 0 && !ownerPickActive();
}

// kumpulkan sampai `cap` nama file media di folder (cap < 0 = hanya hitung semuanya). Tidak menghapus apa pun
// selagi iterasi (di FAT itu bisa melewatkan entri) -> pemanggil menghapus SETELAH dir ditutup.
static int galDirCollect(const char* dir, char names[][44], int cap) {
  char full[96];
  snprintf(full, sizeof(full), "%s%s", GAL_MP, dir);
  DIR* d = opendir(full);
  if (!d) return 0;
  int n = 0;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    if (e->d_type == DT_DIR) continue;
    const char* nm = e->d_name;
    size_t l = strlen(nm);
    if (l == 0 || l >= 44 || nm[0] == '.') continue;
    if (galKindOf(nm) < 0) continue;
    if (cap >= 0) { strcpy(names[n], nm); if (++n >= cap) break; }
    else n++;
  }
  closedir(d);
  return n;
}

static void galDelDirStart() {
  strncpy(galDelDir, galCurDir, sizeof(galDelDir) - 1); galDelDir[sizeof(galDelDir) - 1] = 0;
  galDelTotal = galDirCollect(galDelDir, nullptr, -1);
  galDelDone = 0; galDelFail = 0;
  galDelBusy = true; galDirDelArmUntil = 0;
  needRedraw = true;
}

static void galDelDirFinish() {
  galDelBusy = false;
  bool gone = (galDelFail == 0) && SD_MMC.rmdir(galDelDir);   // rmdir hanya berhasil kalau folder sudah kosong
  char m[56];
  if (gone)             snprintf(m, sizeof(m), "Folder dihapus (%d file)", galDelDone);
  else if (galDelFail)  snprintf(m, sizeof(m), "%d file gagal dihapus", galDelFail);
  else                  snprintf(m, sizeof(m), "%d file dihapus; folder masih ada isi lain", galDelDone);
  showToast(m);
  if (galDelFail) vibError(); else vibSuccess();
  // kembali ke daftar album (sama dengan galBack() dari grid), dipindai ulang
  galMode = GM_ALBUMS; galItemCount = 0; galWantCnt = 0; galAlbumsDirty = true;
  if (!galScanActive) galScanStart();
  galScrollY = galAlbumScrollSave; galScrollVel = 0; galPressIdx = -1;
  galBeginTransition(1, false);
  needRedraw = true;
}

static void galDelDirStep() {
  char names[4][44];
  int n = galDirCollect(galDelDir, names, 4);
  int removed = 0;
  for (int i = 0; i < n; i++) {
    char p[GAL_PATH_MAX];
    galJoin(p, sizeof(p), galDelDir, names[i]);
    galForgetFile(p);                       // buang thumbnail cache-nya DULU (butuh ukuran & waktu file asli)
    if (SD_MMC.remove(p)) removed++;
  }
  galDelDone += removed;
  galDelFail = n - removed;                 // file yg bandel di batch ini (bukan akumulasi)
  needRedraw = true;
  if (n == 0 || removed == 0) galDelDirFinish();   // habis, atau macet (semua di batch gagal) -> selesai
}

static void galDrawDelProgress(LGFX_Sprite& s) {
  if (!galDelBusy) return;
  char m[32];
  snprintf(m, sizeof(m), "Menghapus %d/%d", galDelDone, galDelTotal);
  int w = 150, x = (SCR_W - w) / 2, y = SCR_H / 2 - 12;
  iosRR(s, x, y, w, 24, 8, T().accent);
  s.setTextColor(TFT_BLACK); s.setTextSize(1);
  s.setCursor(x + w / 2 - s.textWidth(m) / 2, y + 8); s.print(m);
}

'''
rep("// =====================================================================\n// VIEWER: decode foto layar penuh",
    HELPERS + "// =====================================================================\n// VIEWER: decode foto layar penuh", "helpers")

rep("  galAlbumScrollSave = galScrollY;\n  strncpy(galCurDir, galAlbums[ai].path, 63);",
    "  galAlbumScrollSave = galScrollY;\n  galDirDelArmUntil = 0;   // GAL_DELDIR_V1\n  strncpy(galCurDir, galAlbums[ai].path, 63);", "open-album")

rep("  s.setCursor(SCR_W - 8 - s.textWidth(info), 27); s.print(info);\n\n  if (galItemCount == 0) {\n    s.setTextColor(T().subtext);\n    s.setCursor(16, galTop + 30); s.print(\"Album kosong.\");",
    "  bool canDel = galDirDelOk();   // GAL_DELDIR_V1\n"
    "  s.setCursor(SCR_W - 8 - (canDel ? 60 : 0) - s.textWidth(info), 27); s.print(info);\n"
    "  if (canDel) {\n"
    "    bool arm = millis() < galDirDelArmUntil;\n"
    "    iosRR(s, SCR_W - 60, 23, 52, 16, 6, arm ? T().danger : T().surface2);\n"
    "    s.setTextColor(arm ? 0xFFFF : T().subtext);\n"
    "    const char* dl = arm ? \"Yakin?\" : \"Hapus\";\n"
    "    s.setCursor(SCR_W - 60 + 26 - s.textWidth(dl) / 2, 27); s.print(dl);\n"
    "    s.setTextColor(T().subtext);\n"
    "  }\n\n  if (galItemCount == 0) {\n    s.setTextColor(T().subtext);\n    s.setCursor(16, galTop + 30); s.print(\"Album kosong.\");", "grid-header")

rep("  if (galMode == GM_ALBUMS) galDrawAlbums(s); else galDrawGrid(s);\n  drawBack(s); drawToast(s);\n}\n",
    "  if (galMode == GM_ALBUMS) galDrawAlbums(s); else galDrawGrid(s);\n  galDrawDelProgress(s);   // GAL_DELDIR_V1\n  drawBack(s); drawToast(s);\n}\n", "content")

rep("  (void)held;\n  if (!isNew) return;\n  if (galMode == GM_VIEW && !galViewOverlay) return;",
    "  (void)held;\n  if (!isNew) return;\n  if (galDelBusy) return;   // GAL_DELDIR_V1: sedang menghapus folder -> abaikan sentuhan\n  if (galMode == GM_VIEW && !galViewOverlay) return;", "touch-busy")

rep("      showToast(\"Ketuk lagi untuk hapus cache\");\n    }\n    needRedraw = true;\n  }\n}\n",
    "      showToast(\"Ketuk lagi untuk hapus cache\");\n    }\n    needRedraw = true;\n  }\n"
    "  if (galDirDelOk() && galHit(x, y, SCR_W - 60, 20, 52, 22)) {   // GAL_DELDIR_V1: tombol Hapus folder\n"
    "    if (millis() < galDirDelArmUntil) {\n"
    "      galDelDirStart();\n"
    "    } else {\n"
    "      galDirDelArmUntil = millis() + 3000;\n"
    "      char m[56]; snprintf(m, sizeof(m), \"Ketuk lagi: hapus folder & %d%s file\", galItemCount, galItemsTrunc ? \"+\" : \"\");\n"
    "      showToast(m);\n"
    "    }\n"
    "    needRedraw = true;\n"
    "  }\n"
    "}\n", "touch-btn")

rep("  galLastTickMs = now;\n  if (!galMem) return;\n",
    "  galLastTickMs = now;\n  if (!galMem) return;\n  if (galDelBusy) { galDelDirStep(); return; }   // GAL_DELDIR_V1: hapus folder bertahap\n", "tick")
rep("  if (galClearArmUntil && millis() >= galClearArmUntil) { galClearArmUntil = 0; needRedraw = true; }\n}\n",
    "  if (galClearArmUntil && millis() >= galClearArmUntil) { galClearArmUntil = 0; needRedraw = true; }\n"
    "  if (galDirDelArmUntil && millis() >= galDirDelArmUntil) { galDirDelArmUntil = 0; needRedraw = true; }   // GAL_DELDIR_V1\n}\n", "tick-arm")

rep("  galDelArmUntil = 0; galClearArmUntil = 0; galWantCnt = 0;\n",
    "  galDelArmUntil = 0; galClearArmUntil = 0; galWantCnt = 0;\n  galDirDelArmUntil = 0; galDelBusy = false;   // GAL_DELDIR_V1\n", "enter")
if crlf: s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] GAL_DELDIR_V1 terpasang di gallery_renphone.ino")
