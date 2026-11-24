#!/usr/bin/env python3
"""BT_FFAT_V1: Music app memutar lagu dari FFat lewat output Bluetooth (TWS) juga, bukan hanya speaker.
musTask ber-stack PSRAM dilarang menyentuh flash, jadi buka/baca/tutup file FFat dikerjakan worker "mffio"
(musfs.h) dgn File terpisah (musBtFfF) supaya tidak bentrok dgn mffPlayF milik spkTask.
Pakai dari root repo: python3 tools/patches/apply_bt_ffat_patch.py [--check]   (idempoten)"""
import sys, pathlib
CHECK = "--check" in sys.argv
f = pathlib.Path("firmware/phone/musicbt_renphone.ino"); t = f.read_text()
if "BT_FFAT_V1" in t: print("sudah   musicbt_renphone.ino"); sys.exit(0)

CLOSE_OLD = "if (musFile) musFile.close();"
EDITS = [
# 1) helper worker + pembaca + penutup (setelah musFfInfoFn, sebelum bagian COVER ART)
("""  o->ok = musParseMp3(&o->sr, &o->kbps, &o->off) ? 1 : 0;
  musFile.close();
}
""",
"""  o->ok = musParseMp3(&o->sr, &o->kbps, &o->off) ? 1 : 0;
  musFile.close();
}

// BT_FFAT_V1: lagu FFat lewat Bluetooth. Semua sentuhan FFat di worker; File sendiri (bukan mffPlayF milik spkTask).
static File musBtFfF;
static volatile bool musBtFfOpen = false;
static int musBtFfFail = 0;
struct MusBtFfOp { const char* path; uint32_t off; uint8_t* dst; int n; int ret; };
static void musBtFfOpenFn(void* p) {
  MusBtFfOp* o = (MusBtFfOp*)p;
  if (musBtFfF) musBtFfF.close();
  musBtFfF = FFat.open(o->path, FILE_READ);
  o->ret = 0;
  if (musBtFfF) {
    if (o->off > 0 && o->off < (uint32_t)musBtFfF.size()) musBtFfF.seek(o->off);   // lewati tag ID3 (lihat musStartTrack)
    o->ret = 1;
  }
}
static void musBtFfReadFn(void* p) {
  MusBtFfOp* o = (MusBtFfOp*)p;
  o->ret = musBtFfF ? (int)mffReadI(musBtFfF, o->dst, (size_t)o->n) : -1;
}
static void musBtFfCloseFn(void*) { if (musBtFfF) musBtFfF.close(); }
static void musBtFfClose() {
  if (!musBtFfOpen) return;
  musBtFfOpen = false;
  mffRun(musBtFfCloseFn, nullptr);
}
// tutup file lagu aktif, dari SD maupun FFat (BT)
static void musCloseFile() {
  if (musFile) musFile.close();
  musBtFfClose();
}
// baca satu blok 4 KB utk dikirim ke CAM. >0 byte, 0/-1 = habis, -2 = worker FFat sedang tidak merespons (coba lagi)
static int musBtReadBlk() {
  if (!musUseFfat) return musFile.read(musBlk, 4096);
  if (!musBtFfOpen) return -1;
  MusBtFfOp o; o.path = nullptr; o.off = 0; o.dst = musBlk; o.n = 4096; o.ret = -2;
  if (!mffRun(musBtFfReadFn, &o)) return -2;
  return o.ret;
}
"""),
# 2) musStartTrack: izinkan FFat utk BT
("""    if (!musOutSpk) { musToast("Lagu di FFat: pilih Output SPK"); return false; }
""", ""),
# 3) buka file FFat di worker sebelum RP_BEGIN
("""  } else {
    rpSend(Serial1, RP_BEGIN, &sq, 1);
  }

  musCur = idx; musLoaded = true; musPaused = false; musEnded = false;
""",
"""  } else {
    if (musUseFfat) {                           // BT_FFAT_V1
      MusBtFfOp op; op.path = musList[idx].c_str(); op.off = off; op.dst = nullptr; op.n = 0; op.ret = 0;
      if (!mffRun(musBtFfOpenFn, &op)) { musToast("FFat tidak merespons"); return false; }
      if (op.ret != 1) { musToast("Gagal membuka file"); return false; }
      musBtFfOpen = true; musBtFfFail = 0;
    }
    rpSend(Serial1, RP_BEGIN, &sq, 1);
  }

  musCur = idx; musLoaded = true; musPaused = false; musEnded = false;
"""),
# 4) loop streaming: baca lewat helper, toleransi worker macet
("""        int n = musFile.read(musBlk, 4096);
        if (n <= 0) {
          rpSend(Serial1, RP_FINISH, nullptr, 0);
          musFileDone = true; musStreaming = false;
          if (musFile) musFile.close();
        } else {""",
"""        int n = musBtReadBlk();
        if (n == -2) {                            // BT_FFAT_V1: worker FFat macet -> coba lagi, menyerah setelah 3x
          if (++musBtFfFail < 3) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
          n = 0;
        } else musBtFfFail = 0;
        if (n <= 0) {
          rpSend(Serial1, RP_FINISH, nullptr, 0);
          musFileDone = true; musStreaming = false;
          musCloseFile();
        } else {"""),
]
bad = 0
for old, new in EDITS:
    if t.count(old) != 1: print(f"GAGAL   anchor tidak ditemukan tepat 1x: {old[:70]!r}"); bad += 1
# 5) semua penutup file lagu -> musCloseFile() (kecuali satu yg sudah di dalam helper & jalur error SD lokal)
n_close = t.count(CLOSE_OLD)
if n_close < 7: print(f"GAGAL   penutup file hanya {n_close}x (harap >=7)"); bad += 1
if bad: sys.exit(1)
if not CHECK:
    for old, new in EDITS: t = t.replace(old, new, 1)
    # helper sendiri mengandung CLOSE_OLD sekali: lindungi, ganti semua lain
    marker = "static void musCloseFile() {\n  " + CLOSE_OLD
    t = t.replace(marker, "static void musCloseFile() {\n  @@KEEP@@")
    t = t.replace(CLOSE_OLD, "musCloseFile();")
    t = t.replace("@@KEEP@@", CLOSE_OLD)
    f.write_text(t)
print(("cocok " if CHECK else "dipasang") + f" musicbt_renphone.ino ({len(EDITS)} titik + {n_close} penutup file)")
