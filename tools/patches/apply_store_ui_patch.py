#!/usr/bin/env python3
# apply_store_ui_patch.py -- NYXSTORE_UI_V2: tampilan Nyx Store dirombak
# Jalankan dari root repo:  python tools/patches/apply_store_ui_patch.py
#
# Yang berubah (hanya nyxstore_renphone.ino, logika unduh/pasang/hapus TIDAK disentuh):
#  - ikon app: lingkaran polos -> squircle berkilap (modernIconBase) + huruf besar kontras otomatis
#  - baris daftar lebih lega (64 px), kartu kaca, tombol pil Pasang/Perbarui/Terpasang
#  - header: subjudul "N app · N update" + chip Flash/SD
#  - rincian: kartu hero (ikon besar + status), 3 tile info (Ukuran/Memori/Izin), kotak peringatan
#  - layar kemajuan: ikon besar, persen besar di kanan, bilah tebal membulat
import sys, os

FN = os.path.join("firmware", "phone", "nyxstore_renphone.ino")
s = open(FN, encoding="utf-8").read()
if "NYXSTORE_UI_V2" in s:
    print("sudah terpasang:", FN); sys.exit(0)

def once(s, old, new):
    if s.count(old) != 1:
        sys.exit("GAGAL: pola tidak ditemukan tepat 1x:\n" + old[:100])
    return s.replace(old, new)

# 1) konstanta
s = once(s, "#define NYXSTORE_ROW_H     56\n",
"#define NYXSTORE_ROW_H     64          // NYXSTORE_UI_V2 (dulu 56)\n#define NYXSTORE_TOP       62          // NYXSTORE_UI_V2: awal konten, di bawah judul + subjudul\n")

# 2) sentuh daftar: awal area ikut NYXSTORE_TOP
s = once(s, "  int top = 26 + 22, bot = backY() - 4;\n  if (isNew) { nsDrag",
            "  int top = NYXSTORE_TOP, bot = backY() - 4;\n  if (isNew) { nsDrag")

NEW_HEADER_LIST = r'''// NYXSTORE_UI_V2 -- tanda tangan hanya tipe primitif (aturan prototipe arduino-cli)
static void nsIcon(LGFX_Sprite& s, int cx, int cy, int r, char sym, uint16_t col) {
  modernIconBase(s, cx, cy, r, col);
  s.setTextColor(appIconInk(col));
  int sz = (r >= 24) ? 3 : 2; s.setTextSize(sz);
  char t[2] = { sym, 0 };
  s.setCursor(cx - s.textWidth(t) / 2, cy - (sz * 8) / 2); s.print(t);
  s.setTextSize(1);
}

static void nsDrawHeader(LGFX_Sprite& s) {
  iosTitle(s, 8, 26, "Nyx Store");
  if (nyxAppFs) {
    const char* w = nyxFsFlash ? "Flash" : "SD";
    nsPill(s, SCR_W - 8 - 46, 28, 46, 18, w, blend565(T().surface2, T().text, 40), T().subtext);
  }
  if ((nsSub == NS_LIST || nsSub == NS_DETAIL || nsSub == NS_CONFIRM_RM || nsSub == NS_WORK) && nsN > 0) {
    int upd = 0; for (int i = 0; i < nsN; i++) if (nsAppState[i] == nyxstore::ST_UPDATE) upd++;
    char sb[40];
    if (upd > 0) snprintf(sb, sizeof(sb), "%d app  \xC2\xB7  %d update", nsN, upd);
    else         snprintf(sb, sizeof(sb), "%d app", nsN);
    s.setTextSize(1); s.setTextColor(upd > 0 ? (uint16_t)0xFD20 : (uint16_t)T().subtext);
    s.setCursor(12, 49); s.print(sb);
  }
}

static void nsDrawList(LGFX_Sprite& s) {
  nsDrawHeader(s);
  int top = NYXSTORE_TOP, bot = backY() - 4;
  if (nsN == 0) {
    s.setTextColor(T().subtext); s.setTextSize(1); s.setCursor(12, top + 10); s.print(RPTR("Katalog kosong"));
    return;
  }
  s.setClipRect(0, top, SCR_W, bot - top);
  const int pillW = 68;
  for (int i = 0; i < nsN; i++) {
    int ry = top + i * NYXSTORE_ROW_H - (int)nsScroll;
    if (ry + NYXSTORE_ROW_H < top || ry > bot) continue;
    const nyxstore::Entry& e = nsApps[i];
    int st = nsAppState[i]; bool act = (st == nyxstore::ST_NOT_INSTALLED || st == nyxstore::ST_UPDATE);
    drawGlassPanel(s, 8, ry + 3, SCR_W - 16, NYXSTORE_ROW_H - 7, 14, T().surface2, 170);
    nsIcon(s, 8 + 12 + 20, ry + NYXSTORE_ROW_H / 2, 20, e.sym, e.color);
    int tx = 8 + 12 + 48, avail = SCR_W - 8 - 10 - pillW - tx - 6;
    nsFitText(s, tx, ry + 11, e.name, T().text, avail);
    char sz[16], sub[40]; nyxstore::fmtSize(e.elfSize, sz, sizeof(sz));
    snprintf(sub, sizeof(sub), "%s%s%s", sz, e.perms[0] ? "  \xC2\xB7  " : "", e.perms);
    if (e.test) { size_t l = strlen(sub); snprintf(sub + l, sizeof(sub) - l, "  \xC2\xB7  %s", RPTR("UJI")); }
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, ry + 36); s.print(sub);
    uint16_t bg = (st == nyxstore::ST_UPDATE) ? 0xFD20 : (act ? (uint16_t)T().accent : blend565(T().surface2, T().text, 40));
    nsPill(s, SCR_W - 8 - 10 - pillW, ry + NYXSTORE_ROW_H / 2 - 12, pillW, 24, nsLabelFor(st), bg, act ? 0xFFFF : T().subtext);
  }
  s.clearClipRect();
}

'''

NEW_DETAIL_WORK = r'''static void nsDrawDetail(LGFX_Sprite& s) {
  nsDrawHeader(s);
  const nyxstore::Entry& e = nsApps[nsSel];
  int st = nsAppState[nsSel];
  int top = NYXSTORE_TOP, W = SCR_W;
  nsLayoutDetail();

  // ---- kartu hero ----
  drawGlassPanel(s, 8, top, W - 16, 70, 14, T().surface2, 180);
  nsIcon(s, 8 + 14 + 26, top + 35, 26, e.sym, e.color);
  int tx = 8 + 14 + 60;
  nsFitText(s, tx, top + 10, e.name, T().text, W - 8 - 10 - tx);
  s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(tx, top + 34); s.print(e.id);
  const char* msg = (st == nyxstore::ST_UPDATE) ? RPTR("Ada versi baru")
                  : (st == nyxstore::ST_UP_TO_DATE) ? RPTR("Sudah versi terbaru")
                  : (st == nyxstore::ST_UNKNOWN) ? RPTR("Terpasang manual (bukan dari Store)") : "";
  if (*msg) { s.setTextColor(st == nyxstore::ST_UPDATE ? (uint16_t)0xFD20 : (uint16_t)T().accent); s.setCursor(tx, top + 49); s.print(msg); }

  int y = top + 78;
  if (nsSub == NS_CONFIRM_RM) {
    drawGlassPanel(s, 8, y, W - 16, 60, 12, blend565(T().surface2, 0xF800, 50), 190);
    s.setTextSize(1); s.setTextColor(T().text); s.setCursor(18, y + 16); s.print(RPTR("Hapus app ini?"));
    s.setTextColor(T().subtext); s.setCursor(18, y + 34); s.print(RPTR("Semua datanya ikut terhapus."));
  } else {
    // ---- 3 tile info ----
    int gap = 8, tw = (W - 16 - 2 * gap) / 3, th = 40;
    char v0[16], v1[16]; nyxstore::fmtSize(e.elfSize, v0, sizeof(v0)); snprintf(v1, sizeof(v1), "%u KB", (unsigned)e.memKb);
    const char* lab[3] = { RPTR("Ukuran"), RPTR("Memori"), RPTR("Izin") };
    const char* val[3] = { v0, v1, e.perms[0] ? e.perms : "-" };
    for (int i = 0; i < 3; i++) {
      int x = 8 + i * (tw + gap);
      drawGlassPanel(s, x, y, tw, th, 10, T().surface2, 170);
      s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(x + 8, y + 7); s.print(lab[i]);
      nsFitText(s, x + 8, y + 19, val[i], T().text, tw - 14);
    }
    // ---- kotak peringatan ----
    int y2 = y + th + 8;
    drawGlassPanel(s, 8, y2, W - 16, 44, 10, blend565(T().surface2, 0xFD20, 50), 170);
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(18, y2 + 10); s.print(RPTR("App native tidak di-sandbox."));
    s.setCursor(18, y2 + 24); s.print(RPTR("Pasang hanya yang kamu percaya."));
  }
  for (int i = 0; i < nsBtnN; i++) {
    const NsBtnRect& r = nsBtns[i]; const char* lb = ""; uint16_t bg = blend565(T().surface2, T().text, 50), fg = T().text;
    if (r.id == NSB_INSTALL) { lb = (st == nyxstore::ST_NOT_INSTALLED) ? RPTR("Pasang") : (st == nyxstore::ST_UPDATE ? RPTR("Perbarui") : RPTR("Pasang ulang")); bg = T().accent; fg = 0xFFFF; }
    else if (r.id == NSB_REMOVE || r.id == NSB_YES_RM) { lb = RPTR("Hapus"); bg = 0xF800; fg = 0xFFFF; }
    else lb = RPTR("Batal");
    nsPill(s, r.x, r.y, r.w, r.h, lb, bg, fg);
  }
}

static void nsDrawWork(LGFX_Sprite& s) {
  nsDrawHeader(s);
  int top = NYXSTORE_TOP, W = SCR_W;
  const nyxstore::Entry& e = nsApps[nsSel];
  drawGlassPanel(s, 8, top, W - 16, 100, 14, T().surface2, 180);
  nsIcon(s, 8 + 14 + 26, top + 36, 26, e.sym, e.color);
  // progres: manifest 0-5 %, unduh 5-60 %, tulis 60-100 %
  uint32_t got = nsGot, tot = nsTotal ? nsTotal : 1; int pct = 0;
  if (nsPhase == NSP_MANIFEST) pct = 2;
  else if (nsPhase == NSP_ELF) pct = 5 + (int)((uint64_t)got * 55 / tot);
  else { uint32_t wt = nsBufLen ? nsBufLen : 1; pct = 60 + (int)((uint64_t)nsWritten * 40 / wt); }
  if (pct > 100) pct = 100;
  int tx = 8 + 14 + 60;
  char pc[8]; snprintf(pc, sizeof(pc), "%d%%", pct);
  s.setTextSize(2); int pw = s.textWidth(pc);
  s.setTextColor(T().accent); s.setCursor(W - 8 - 14 - pw, top + 12); s.print(pc);
  s.setTextSize(1);
  nsFitText(s, tx, top + 12, e.name, T().text, W - 8 - 14 - pw - 8 - tx);
  s.setTextColor(T().subtext); s.setCursor(tx, top + 40); s.print(nsStatus);
  int bx = 8 + 14, by = top + 74, bw = W - 16 - 28;
  nyxAaRR(s, bx, by, bw, 10, 5, blend565(T().surface2, T().text, 40));
  int fw = bw * pct / 100; if (fw > 10) nyxAaRR(s, bx, by, fw, 10, 5, T().accent);
}

'''

def cut(s, start_marker, end_marker, replacement):
    a = s.find(start_marker); b = s.find(end_marker)
    if a < 0 or b < 0 or b <= a or s.count(start_marker) != 1 or s.count(end_marker) != 1:
        sys.exit("GAGAL: penanda tidak ketemu: " + start_marker[:50])
    return s[:a] + replacement + s[b:]

s = cut(s, "static void nsDrawHeader(LGFX_Sprite& s) {", "static void nsBtn(int i,", NEW_HEADER_LIST)
s = cut(s, "static void nsDrawDetail(LGFX_Sprite& s) {", "static void nsDrawFetch(LGFX_Sprite& s) {", NEW_DETAIL_WORK)
s = once(s, "// nyxstore_renphone.ino -- \"Nyx Store\"", "// NYXSTORE_UI_V2 (tampilan baru: squircle, kartu hero, tile info)\n// nyxstore_renphone.ino -- \"Nyx Store\"")

# state layar Fetch/Err ikut posisi header baru
s = once(s, "s.setCursor(12, 26 + 22 + 10); s.print(RPTR(\"Memuat katalog...\"));",
            "s.setCursor(12, 56); s.print(RPTR(\"Memuat katalog...\"));")
open(FN, "w", encoding="utf-8").write(s)
print("OK: Nyx Store UI v2 terpasang ->", FN)
