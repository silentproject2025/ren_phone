#!/usr/bin/env python3
# apply_hint_bri_patch.py -- HP_HINT_V1 + BRI_NVS_V1
# Jalankan dari root repo:  python tools/patches/apply_hint_bri_patch.py
#
# 1) HP_HINT_V1 (pager_renphone.ino): kalau Home landscape digeser VERTIKAL, halaman "bergoyang"
#    sedikit ke samping (~25 px lalu kembali) sebagai petunjuk bahwa arah geser itu ke samping.
#    Petunjuk berhenti sendiri setelah 3 kali geser horizontal yang berhasil pindah halaman.
#    Hitungannya disimpan di NVS (namespace "ui", kunci "hph"), jadi tetap hilang setelah reboot.
#    Hapus kunci itu (atau erase NVS) kalau mau petunjuknya muncul lagi.
#
# 2) BRI_NVS_V1 (power_renphone.ino + phone.ino): kecerahan layar disimpan di NVS
#    (namespace "ui", kunci "bri") dan dimuat di awal setup(). Penyimpanan di-debounce 2 detik
#    setelah nilai berhenti berubah, jadi menggeser slider tidak menulis flash berkali-kali, dan
#    semua sumber perubahan (Control Center, Pengaturan, remote, suara, video) ikut tersimpan.
#
# Tidak menambah file .ino baru, jadi build-firmware.yml tidak perlu diubah.
import sys, os

BASE = "firmware/phone"

def load(fn):
    return open(os.path.join(BASE, fn), encoding="utf-8").read()
def save(fn, s):
    open(os.path.join(BASE, fn), "w", encoding="utf-8").write(s)
def once(s, old, new, fn):
    if s.count(old) != 1:
        sys.exit("GAGAL: pola tidak ditemukan tepat 1x di %s:\n%s" % (fn, old[:90]))
    return s.replace(old, new)

# ---------------- pager_renphone.ino ----------------
fn = "pager_renphone.ino"
s = load(fn)
if "HP_HINT_V1" in s:
    print("sudah terpasang:", fn)
else:
    s = once(s, "static int   hwlModeV = -1;\n",
"""static int   hwlModeV = -1;

// HP_HINT_V1: petunjuk arah geser. Geser vertikal di Home berhalaman -> halaman goyang sedikit ke samping.
// Berhenti sendiri setelah HP_HINT_DONE kali geser horizontal yang berhasil pindah halaman (disimpan di NVS "ui"/"hph").
#define HP_HINT_DONE  3        // jumlah geser horizontal berhasil sebelum petunjuk berhenti
#define HP_NUDGE_V    1.8f     // px/ms: dorongan awal goyangan (puncak sekitar 25 px, lalu pegas mengembalikan)
static int hpHintN = -1;       // -1 = belum dimuat dari NVS
static int hpHintLoad(){
  if(hpHintN < 0){
    Preferences p; p.begin("ui", true);
    hpHintN = p.getInt("hph", 0);
    p.end();
    if(hpHintN < 0) hpHintN = 0;
  }
  return hpHintN;
}
static void hpHintOk(){        // satu geser horizontal berhasil pindah halaman
  if(hpHintLoad() >= HP_HINT_DONE) return;
  hpHintN++;
  Preferences p;
  if(p.begin("ui", false)){ p.putInt("hph", hpHintN); p.end(); }
  Serial.printf("[hp] petunjuk geser: %d/%d\\n", hpHintN, HP_HINT_DONE);
}
""", fn)
    s = once(s,
"""    tgt = hpPickTarget(hpX, v, hpStartPage, hpN, W);
  } else { hpVel = 0.0f; }
""",
"""    tgt = hpPickTarget(hpX, v, hpStartPage, hpN, W);
    if(hpLock == 2){   // HP_HINT_V1: geser vertikal tidak pindah halaman; goyang sedikit ke samping selama petunjuk masih aktif
      tgt = hpStartPage; hpVel = 0.0f;
      if(hpHintLoad() < HP_HINT_DONE) hpVel = (hpStartPage + 1 < hpN) ? HP_NUDGE_V : -HP_NUDGE_V;
    } else if(tgt != hpStartPage) hpHintOk();
  } else { hpVel = 0.0f; }
""", fn)
    save(fn, s)
    print("ok:", fn)

# ---------------- power_renphone.ino (kecerahan ke NVS) ----------------
fn = "power_renphone.ino"
s = load(fn)
if "BRI_NVS_V1" in s:
    print("sudah terpasang:", fn)
else:
    s = s.rstrip("\n") + """

// ============================================================
//  BRI_NVS_V1 -- kecerahan layar tersimpan di NVS (namespace "ui", kunci "bri")
//  briLoad() dipanggil paling awal di setup(); briTick() tiap loop().
//  Disimpan 2 detik setelah nilai berhenti berubah (slider tidak menulis flash tiap piksel).
// ============================================================
static int briSaved = -1, briSeen = -1;
static unsigned long briChangeMs = 0;
void briLoad(){
  Preferences p; p.begin("ui", true);
  int v = p.getInt("bri", -1);
  p.end();
  if(v >= 10 && v <= 255) brightness = v;
  briSaved = briSeen = brightness;
  Serial.printf("[bri] NVS=%d -> pakai %d\\n", v, brightness);
}
void briTick(){
  if(briSeen < 0) return;                                   // belum dimuat
  if(brightness != briSeen){ briSeen = brightness; briChangeMs = millis(); return; }
  if(brightness != briSaved && millis() - briChangeMs >= 2000UL){
    Preferences p;
    if(p.begin("ui", false)){
      p.putInt("bri", brightness); p.end();
      briSaved = brightness;
      Serial.printf("[bri] tersimpan %d\\n", brightness);
    } else briChangeMs = millis();                          // gagal buka NVS: coba lagi nanti
  }
}
"""
    save(fn, s)
    print("ok:", fn)

# ---------------- phone.ino (prototipe + dua pemanggilan) ----------------
fn = "phone.ino"
s = load(fn)
if "BRI_NVS_V1" in s:
    print("sudah terpasang:", fn)
else:
    s = once(s, "void hpHomeCover(LGFX_Sprite&); void hpLoopGuard();",
                "void hpHomeCover(LGFX_Sprite&); void hpLoopGuard(); void briLoad(); void briTick();   // BRI_NVS_V1", fn)
    s = once(s, "  otaGuardBoot(); // PALING AWAL",
                "  briLoad();   // BRI_NVS_V1: muat kecerahan tersimpan sebelum layar pertama menyala\n  otaGuardBoot(); // PALING AWAL", fn)
    s = once(s, "  hpLoopGuard();    // HOME_PAGER_V1",
                "  briTick();        // BRI_NVS_V1\n  hpLoopGuard();    // HOME_PAGER_V1", fn)
    save(fn, s)
    print("ok:", fn)
