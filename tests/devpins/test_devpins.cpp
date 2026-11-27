// Tes logika murni devpins.h (tanpa Arduino). Jalankan: ./run.sh
#include <cassert>
#include <cstdio>
#include <vector>
#include "devpins.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// penyimpanan palsu: meniru NVS "devpins" (cur / new / try) + aturan boot
struct Fake { bool hasCur=false, hasNew=false; DpBlob cur, nw; uint8_t tries=0; };
static int8_t active[DP_COUNT];
static uint8_t bootState;
static void boot(Fake& f) {                         // = devpinsBoot()
  for (int i = 0; i < DP_COUNT; i++) active[i] = DP_DEFS[i].def;
  bootState = 0;
  DpBootPlan p = dpBootPlan(f.hasCur, f.hasNew, f.tries);
  if (p.discardNew) { f.hasNew = false; f.tries = 0; bootState = 2; }
  if (p.useNew) { f.tries = 1; if (dpDecode(f.nw, active)) bootState = 1; }
  else if (p.useCur) dpDecode(f.cur, active);
}
static void saveTrial(Fake& f, const int8_t* eff) { dpEncode(eff, f.nw); f.hasNew = true; f.tries = 0; }
static void commit(Fake& f) { dpEncode(active, f.cur); f.hasCur = true; f.hasNew = false; f.tries = 0; bootState = 0; }

int main() {
  // ---- tabel ----
  for (int i = 0; i < DP_COUNT; i++) {
    CHECK(DP_DEFS[i].name && DP_DEFS[i].group);
    if (DP_DEFS[i].avail && DP_DEFS[i].def >= 0) CHECK(dpGpioExists(DP_DEFS[i].def));     // semua bawaan harus pin yg ada di S3
    CHECK(rpPinV[i] == DP_DEFS[i].def);
  }
  int avail = 0; for (int i = 0; i < DP_COUNT; i++) avail += DP_DEFS[i].avail;
  printf("profil: %s | pin tersedia: %d/%d\n", RP_DISP_NAME, avail, (int)DP_COUNT);

  // ---- aturan pin ----
  CHECK(dpGpioExists(0) && dpGpioExists(21) && dpGpioExists(38) && dpGpioExists(48));
  CHECK(!dpGpioExists(22) && !dpGpioExists(26) && !dpGpioExists(33) && !dpGpioExists(37) && !dpGpioExists(49) && !dpGpioExists(-1));
  CHECK(dpIsAdc1(1) && dpIsAdc1(10) && !dpIsAdc1(11) && !dpIsAdc1(0));
  CHECK(dpWarnKind(0) == 1 && dpWarnKind(45) == 1 && dpWarnKind(19) == 2 && dpWarnKind(12) == 0);

  // ---- dpStep: lompati 22..37, -1 hanya utk pin opsional, membungkus ----
  CHECK(dpStep(DP_TFT_BL, 21, +1) == 38);
  CHECK(dpStep(DP_TFT_BL, 38, -1) == 21);
  CHECK(dpStep(DP_TFT_BL, 48, +1) == -1);            // opsional: -1 setelah 48
  CHECK(dpStep(DP_TFT_BL, -1, +1) == 0);
  CHECK(dpStep(DP_TFT_BL, 0, -1) == -1);
  CHECK(dpStep(DP_SCLK, 48, +1) == 0);               // wajib: tidak ada -1, membungkus ke 0
  CHECK(dpStep(DP_SCLK, 0, -1) == 48);
  for (int i = 0; i < DP_COUNT; i++) { int v = DP_DEFS[i].def; for (int k = 0; k < 80; k++) { v = dpStep(i, v, +1); CHECK(dpValueOk(i, v)); } }

  // ---- konflik: hanya dilaporkan kalau ada yang diubah ----
  int8_t e[DP_COUNT]; memcpy(e, rpPinV, sizeof(e));
  CHECK(dpErrorCount(e) == 0);                       // bawaan papan harus bersih
  CHECK(dpChangedCount(e) == 0);
  int a = DP_SENS_SDA, b = DP_VIB;
  e[b] = e[a]; CHECK(dpConflict(e, b) == a && dpConflict(e, a) == b); CHECK(dpErrorCount(e) == 2); CHECK(dpChangedCount(e) == 1);
  e[b] = -1;   CHECK(dpConflict(e, b) == -1); CHECK(dpErrorCount(e) == 0);       // -1 tidak pernah bentrok
  e[DP_SCLK] = 30; CHECK(dpErrorCount(e) >= 1);                                    // GPIO 30 = flash: tak valid

  // ---- encode/decode ----
  memcpy(e, rpPinV, sizeof(e)); e[DP_VIB] = 5; e[DP_TFT_BL] = -1;
  DpBlob bl; dpEncode(e, bl);
  int8_t d[DP_COUNT]; CHECK(dpDecode(bl, d)); CHECK(memcmp(d, e, DP_COUNT) == 0);
  CHECK(bl.ov[DP_SCLK] == DP_UNSET && bl.ov[DP_VIB] == 5);                         // yg sama dgn bawaan tidak disimpan
  DpBlob bad = bl; bad.ov[3] ^= 1; CHECK(!dpDecode(bad, d));                       // crc
  bad = bl; bad.magic = 0; CHECK(!dpDecode(bad, d));
  bad = bl; bad.ov[DP_SCLK] = 30; bad.crc = dpCrc(bad); CHECK(dpDecode(bad, d) && d[DP_SCLK] == DP_DEFS[DP_SCLK].def);   // nilai ngawur -> bawaan
  bad = bl; bad.ov[DP_SCLK] = 127; bad.ov[DP_MOSI] = -1; bad.crc = dpCrc(bad); CHECK(dpDecode(bad, d) && d[DP_MOSI] == DP_DEFS[DP_MOSI].def);   // -1 di pin wajib -> bawaan

  // ---- alur percobaan -> konfirmasi ----
  Fake f; boot(f); CHECK(bootState == 0 && active[DP_VIB] == DP_DEFS[DP_VIB].def);
  saveTrial(f, e);
  boot(f);  CHECK(bootState == 1 && active[DP_VIB] == 5 && f.tries == 1);          // boot percobaan
  commit(f);
  boot(f);  CHECK(bootState == 0 && active[DP_VIB] == 5 && !f.hasNew);              // permanen
  // ---- alur percobaan -> TIDAK dikonfirmasi (cabut daya) -> kembali ----
  int8_t e2[DP_COUNT]; memcpy(e2, active, sizeof(e2)); e2[DP_VIB] = 9;
  saveTrial(f, e2);
  boot(f);  CHECK(bootState == 1 && active[DP_VIB] == 9);
  boot(f);  CHECK(bootState == 2 && active[DP_VIB] == 5 && !f.hasNew && f.tries == 0);   // dibatalkan otomatis, kembali ke konfigurasi lama
  boot(f);  CHECK(bootState == 0 && active[DP_VIB] == 5);
  // ---- percobaan pertama langsung crash (tries sudah 1 sebelum dipakai) ----
  saveTrial(f, e2); boot(f); CHECK(bootState == 1); /* crash di sini, tidak commit */ boot(f); CHECK(bootState == 2 && active[DP_VIB] == 5);
  // ---- tanpa konfigurasi permanen ----
  Fake g; saveTrial(g, e); boot(g); CHECK(bootState == 1); boot(g); CHECK(bootState == 2 && active[DP_VIB] == DP_DEFS[DP_VIB].def);
  // ---- blob percobaan rusak -> abaikan ----
  Fake h; h.hasNew = false; boot(h); CHECK(bootState == 0);

  printf(fails ? "\n%d GAGAL\n" : "\nSEMUA LULUS\n", fails);
  return fails ? 1 : 0;
}
