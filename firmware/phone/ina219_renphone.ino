// =====================================================================
// ina219_renphone.ino -- Sensor daya INA219 (PENGGANTI ADC voltage divider)
// =====================================================================
// Semua fitur chip INA219 dipakai, tanpa library tambahan (driver I2C
// langsung lewat Wire, jadi CI tidak perlu install library baru):
//   * Bus voltage      (0-26 V, LSB 4 mV)       -> tegangan
//   * Shunt voltage    (+-320 mV, LSB 10 uV)    -> tegangan jatuh di resistor
//   * Current register (lewat kalibrasi)        -> arus (mA, bertanda)
//   * Power register   (20 x Current_LSB)       -> daya (mW)
//   * Calibration register (dihitung otomatis dari R shunt + gain)
//   * PGA gain  /1 /2 /4 /8   (40 / 80 / 160 / 320 mV)
//   * Bus voltage range 16 V / 32 V
//   * ADC bus & shunt terpisah: 9/10/11/12-bit & averaging 2..128 sampel
//   * Operating mode: power-down, triggered, ADC off, continuous (8 mode)
//   * Flag CNVR (conversion ready) & OVF (math overflow)
//   * Software reset (bit RST)
//   * Alamat I2C 0x40..0x4F (pin A0/A1) + auto-scan
// Turunan (fuel-gauge sederhana): persen baterai (kurva OCV + kompensasi
// R dalam + coulomb counting), mAh/mWh keluar-masuk (sesi & total),
// puncak/min, estimasi sisa waktu / waktu penuh, deteksi charging,
// grafik riwayat arus/tegangan/daya, siklus pengisian.
//
// WIRING:  INA219  VCC->3.3V  GND->GND  SDA->RP_SENS_SDA  SCL->RP_SENS_SCL (display_conf.h)
//          ILI9341: GPIO15/7 (Wire) | ST7796: GPIO5/4 (Wire1). Satu bus dgn MPU6050; alamat 0x40-0x4F tidak bentrok
//          Resistor shunt dipasang SERI di kabel (+) baterai:
//              baterai(+) -> VIN+  ..  VIN- -> beban/charger
//          Konvensi: arus POSITIF = baterai dipakai (discharge),
//                    NEGATIF = baterai diisi (charge). Terbalik? ubah
//                    "Arah arus" di tab Baterai.
//
// ATURAN PROYEK: tidak ada String/heap di sini; fungsi publik hanya
// menerima tipe primitif / LGFX_Sprite& (hindari bug prototype arduino-cli).
// Dipasang ke phone.ino lewat apply_ina219_patch.py.
// =====================================================================

#define INA_POLL_MS       250
#define INA_REG_CONFIG    0x00
#define INA_REG_SHUNT     0x01
#define INA_REG_BUS       0x02
#define INA_REG_POWER     0x03
#define INA_REG_CURRENT   0x04
#define INA_REG_CAL       0x05
#define INA_DEADBAND_MA   2.0f     // |arus| di bawah ini dianggap 0
#define INA_CHG_ON_MA     20.0f    // arus charge minimal utk status "mengisi"
#define INA_CHG_OFF_MA    8.0f     // histeresis keluar status mengisi
#define INA_CHG_EFF       0.98f    // efisiensi coulomb saat charge
#define INA_HIST_N        120      // titik grafik
#define INA_HIST_MS       5000UL   // 1 titik / 5 dtk -> jendela 10 menit
#define INA_SAVE_MS       120000UL // jeda maks simpan counter ke NVS (kalau ada perubahan): 2 menit
#define INA_SAVE_MAH      25.0f    // ...atau tiap 25 mAh perubahan (mana duluan)
#define INA_LRN_MAX_START 55.0f    // ukur kapasitas hanya kalau charge dimulai dari <= 55%
#define INA_LRN_MIN_SPAN  40.0f    // dan rentang pengisian minimal 40 poin persen
#define INA_TAB_Y         40
#define INA_TAB_H         18
#define INA_TOP           62

// ---------------- tabel opsi chip ----------------
static const uint8_t  INA_ADC_CODE[11] = {0x0,0x1,0x2,0x3,0x9,0xA,0xB,0xC,0xD,0xE,0xF};
static const char*    INA_ADC_NAME[11] = {"9-bit","10-bit","11-bit","12-bit","2 smp","4 smp","8 smp","16 smp","32 smp","64 smp","128 smp"};
static const uint32_t INA_ADC_US[11]   = {84,148,276,532,1060,2130,4260,8510,17020,34050,68100};
static const char*    INA_PGA_NAME[4]  = {"/1 40mV","/2 80mV","/4 160mV","/8 320mV"};
static const char*    INA_MODE_NAME[8] = {"Power-down","Shunt trig","Bus trig","S+B trig","ADC off","Shunt kont","Bus kont","S+B kont"};
static const uint16_t INA_SHUNT_PRESET[7] = {10,20,50,100,200,500,1000};
static const char*    INA_TAB_NAME[5]  = {"Ringkas","Grafik","Sensor","Baterai","Per App"};

// kurva tegangan-rest Li-ion 1 sel (OCV -> persen)
static const float INA_OCV_V[22] = {3.00f,3.40f,3.61f,3.69f,3.71f,3.73f,3.75f,3.77f,3.79f,3.80f,3.82f,
                                    3.84f,3.85f,3.87f,3.91f,3.95f,3.98f,4.02f,4.08f,4.11f,4.15f,4.20f};
static const float INA_OCV_P[22] = {0,2,5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100};

// ---------------- status sensor ----------------
bool    inaOk = false;          // chip terdeteksi & terkonfigurasi
bool    inaActive = false;      // sedang mengukur (mode kontinu/trigger)
bool    inaOvf = false;         // flag overflow dari chip
bool    inaCnvr = false;        // flag conversion ready terakhir
uint8_t inaAddr = 0x40;         // alamat yang dipakai sekarang

// ---------------- pengaturan (disimpan di NVS "ina") ----------------
uint8_t  inaCfgAddr = 0;        // 0 = auto-scan, 1..16 = 0x40..0x4F
uint8_t  inaPga = 3;            // 0..3 -> /1 /2 /4 /8
uint8_t  inaBrng = 0;           // 0 = 16 V, 1 = 32 V
uint8_t  inaBadc = 7;           // indeks INA_ADC_* (7 = 16 sampel)
uint8_t  inaSadc = 7;
uint8_t  inaMode = 7;           // 7 = shunt+bus kontinu
uint16_t inaShuntMohm = 100;    // resistor shunt (mOhm); modul umum R100 = 100
uint16_t inaCapMah = 2300;      // kapasitas baterai
uint16_t inaRintMohm = 150;     // R dalam baterai (kompensasi OCV)
bool     inaInvert = false;     // balik tanda arus
uint8_t  inaVside = 0;          // 0 = Vbat di VIN+ (bus+shunt), 1 = di VIN- (bus saja)

// ---------------- hasil ukur ----------------
float inaBusV = 0, inaShuntMv = 0, inaBatV = 3.7f;
float inaI = 0;                 // mA, + = dipakai, - = diisi
float inaP = 0;                 // mW bertanda (ikut arah arus)
float inaIavg = 0;              // arus rata-rata halus (utk estimasi waktu)
float inaCurLsbMa = 0.1f, inaPwrLsbMw = 2.0f, inaFsMa = 3200.0f;
uint16_t inaCalReg = 0;
bool  inaCharging = false;
float inaSocF = 100.0f;
int   inaSoc = 100;
bool  inaSocInit = false;

// counter
float inaMahOut = 0, inaMahIn = 0, inaMwhOut = 0, inaMwhIn = 0;        // sesi (sejak boot)
float inaMahOutTot = 0, inaMahInTot = 0;                               // total (NVS)
float inaCycles = 0;                                                    // siklus ekuivalen penuh (NVS)
uint32_t inaBootCount = 0;                                             // boot ke-berapa (NVS)
float inaCapLearn = 0;                                                 // kapasitas terukur mAh, 0 = belum (NVS)
bool  inaLrnActive = false; float inaLrnSoc0 = 0, inaLrnMah = 0;       // sesi ukur kapasitas (RAM)
float inaPeakDis = 0, inaPeakChg = 0, inaPeakP = 0, inaMinV = 9.9f, inaMaxV = 0;
uint32_t inaSessionStart = 0;

static uint8_t  inaFail = 0;
static uint32_t inaLastPoll = 0, inaLastInt = 0, inaLastRetry = 0, inaLastSave = 0;
static bool     inaCountersDirty = false;
static float    inaUnsavedMah = 0;
static bool     inaPrevCharging = false;

// riwayat grafik
static int16_t  inaHistI[INA_HIST_N] EXT_RAM_BSS_ATTR;
static uint16_t inaHistV[INA_HIST_N] EXT_RAM_BSS_ATTR;
static int16_t  inaHistP[INA_HIST_N] EXT_RAM_BSS_ATTR;
static uint16_t inaHistHead = 0, inaHistCount = 0;
static uint32_t inaHistLast = 0;

#include <Wire.h>
#include "display_conf.h"   // RP_SENS_WIRE / RP_SENS_SDA / RP_SENS_SCL (bus sensor)

// UI
static uint8_t  inaTab = 0, inaGraphMetric = 0;
static int      inaRepRow = -1, inaRepDir = 0;
static uint32_t inaRepT0 = 0, inaRepLast = 0;

// =====================================================================
// I2C level rendah
// =====================================================================
static bool inaWrite16(uint8_t reg, uint16_t val) {
  RP_SENS_WIRE.beginTransmission(inaAddr);
  RP_SENS_WIRE.write(reg);
  RP_SENS_WIRE.write((uint8_t)(val >> 8));
  RP_SENS_WIRE.write((uint8_t)(val & 0xFF));
  return RP_SENS_WIRE.endTransmission() == 0;
}

static bool inaRead16(uint8_t reg, uint16_t &val) {
  RP_SENS_WIRE.beginTransmission(inaAddr);
  RP_SENS_WIRE.write(reg);
  if (RP_SENS_WIRE.endTransmission() != 0) return false;
  if (RP_SENS_WIRE.requestFrom((int)inaAddr, 2) != 2) return false;
  uint8_t hi = RP_SENS_WIRE.read();
  uint8_t lo = RP_SENS_WIRE.read();
  val = ((uint16_t)hi << 8) | lo;
  return true;
}

static uint16_t inaCfgWord() {
  return ((uint16_t)(inaBrng & 1) << 13) | ((uint16_t)(inaPga & 3) << 11) |
         ((uint16_t)INA_ADC_CODE[inaBadc] << 7) | ((uint16_t)INA_ADC_CODE[inaSadc] << 3) |
         (uint16_t)(inaMode & 7);
}

// Cal = trunc(0.04096 / (Current_LSB * Rshunt)); bit0 selalu 0.
static void inaCalibrate() {
  float fsMv = 40.0f * (float)(1 << (inaPga & 3));
  float rOhm = (float)inaShuntMohm / 1000.0f;
  if (rOhm < 0.001f) rOhm = 0.001f;
  float fsA = (fsMv / 1000.0f) / rOhm;          // arus skala penuh
  float lsb = fsA / 32768.0f;
  uint32_t cal = (uint32_t)(0.04096f / (lsb * rOhm));
  if (cal > 0xFFFE) cal = 0xFFFE;
  if (cal < 2) cal = 2;
  cal &= 0xFFFE;
  inaCurLsbMa = 1000.0f * 0.04096f / ((float)cal * rOhm);  // LSB sebenarnya setelah pembulatan
  inaPwrLsbMw = 20.0f * inaCurLsbMa;
  inaFsMa = fsA * 1000.0f;
  inaCalReg = (uint16_t)cal;
  inaWrite16(INA_REG_CAL, inaCalReg);
}

static bool inaApplyConfig() {
  if (!inaWrite16(INA_REG_CONFIG, inaCfgWord())) return false;
  inaCalibrate();
  return true;
}

static bool inaProbeAddr(uint8_t a) {
  RP_SENS_WIRE.beginTransmission(a);
  if (RP_SENS_WIRE.endTransmission() != 0) return false;
  uint8_t save = inaAddr; inaAddr = a;
  uint16_t cfg = 0;
  bool ok = inaRead16(INA_REG_CONFIG, cfg) && ((cfg & 0x8000) == 0);
  inaAddr = save;
  return ok;
}

static bool inaDetect() {
  if (inaCfgAddr != 0) {
    uint8_t a = (uint8_t)(0x3F + inaCfgAddr);
    if (inaProbeAddr(a)) { inaAddr = a; return true; }
    return false;
  }
  for (uint8_t a = 0x40; a <= 0x4F; a++) {
    if (inaProbeAddr(a)) { inaAddr = a; return true; }
  }
  return false;
}

// =====================================================================
// NVS
// =====================================================================
static void inaLoadSettings() {
  Preferences p; p.begin("ina", true);
  inaCfgAddr   = p.getUChar("addr", 0);
  inaPga       = p.getUChar("pga", 3);
  inaBrng      = p.getUChar("brng", 0);
  inaBadc      = p.getUChar("badc", 7);
  inaSadc      = p.getUChar("sadc", 7);
  inaMode      = p.getUChar("mode", 7);
  inaShuntMohm = p.getUShort("rsh", 100);
  inaCapMah    = p.getUShort("cap", 2300);
  inaRintMohm  = p.getUShort("rint", 150);
  inaInvert    = p.getBool("inv", false);
  inaVside     = p.getUChar("vside", 0);
  inaMahOutTot = p.getFloat("mOutT", 0);
  inaMahInTot  = p.getFloat("mInT", 0);
  bool hasCyc  = p.isKey("cyc");
  float cycRaw = p.getFloat("cyc", 0);
  inaBootCount = p.getULong("boots", 0);
  inaCapLearn  = p.getFloat("capL", 0);
  p.end();
  if (inaCfgAddr > 16) inaCfgAddr = 0;
  if (inaPga > 3) inaPga = 3;
  if (inaBrng > 1) inaBrng = 0;
  if (inaBadc > 10) inaBadc = 7;
  if (inaSadc > 10) inaSadc = 7;
  if (inaMode > 7) inaMode = 7;
  if (inaShuntMohm < 1 || inaShuntMohm > 10000) inaShuntMohm = 100;
  if (inaCapMah < 100 || inaCapMah > 20000) inaCapMah = 2300;
  if (inaRintMohm > 1000) inaRintMohm = 150;
  if (inaVside > 1) inaVside = 0;
  if (!(inaMahOutTot >= 0)) inaMahOutTot = 0;
  if (!(inaMahInTot >= 0)) inaMahInTot = 0;
  // siklus: kalau belum pernah disimpan (firmware lama), turunkan dari total mAh keluar
  inaCycles = hasCyc ? cycRaw : (inaMahOutTot / (float)inaCapMah);
  if (!(inaCycles >= 0)) inaCycles = 0;
  if (!(inaCapLearn >= 100.0f && inaCapLearn <= 30000.0f)) inaCapLearn = 0;
}

static void inaSaveSettings() {
  Preferences p; p.begin("ina", false);
  p.putUChar("addr", inaCfgAddr);
  p.putUChar("pga", inaPga);
  p.putUChar("brng", inaBrng);
  p.putUChar("badc", inaBadc);
  p.putUChar("sadc", inaSadc);
  p.putUChar("mode", inaMode);
  p.putUShort("rsh", inaShuntMohm);
  p.putUShort("cap", inaCapMah);
  p.putUShort("rint", inaRintMohm);
  p.putBool("inv", inaInvert);
  p.putUChar("vside", inaVside);
  p.end();
}

void inaSaveCounters(bool force) {
  if (!force && !inaCountersDirty) return;
  Preferences p; p.begin("ina", false);
  p.putFloat("mOutT", inaMahOutTot);
  p.putFloat("mInT", inaMahInTot);
  p.putFloat("cyc", inaCycles);
  p.putULong("boots", inaBootCount);
  p.putFloat("capL", inaCapLearn);
  p.end();
  inaCountersDirty = false;
  inaUnsavedMah = 0;
  inaLastSave = millis();
}

// kapasitas yg dipakai utk hitung persen & estimasi waktu: hasil ukur kalau ada, kalau belum pakai setting
static float inaCapEff() {
  return (inaCapLearn > 0) ? inaCapLearn : (float)inaCapMah;
}

// dipanggil saat "charge selesai": hitung kapasitas asli dari mAh yg masuk selama sesi ukur
static void inaLearnFinish() {
  inaLrnActive = false;
  float span = 100.0f - inaLrnSoc0;
  if (span < INA_LRN_MIN_SPAN) return;
  float est = inaLrnMah * INA_CHG_EFF / (span / 100.0f);
  float rated = (float)inaCapMah;
  if (est < 0.40f * rated || est > 1.30f * rated) { showToast(RPTR("Ukur kapasitas batal (hasil tak wajar)")); return; }
  inaCapLearn = (inaCapLearn > 0) ? (inaCapLearn * 0.6f + est * 0.4f) : est;
  inaCountersDirty = true;
  inaSaveCounters(true);
  char b[40]; snprintf(b, sizeof(b), "Kapasitas terukur %.0f mAh", (double)inaCapLearn);
  showToast(b);
}

// =====================================================================
// Estimasi SoC
// =====================================================================
static float inaOcvSoc(float v) {
  if (v <= INA_OCV_V[0]) return 0.0f;
  if (v >= INA_OCV_V[21]) return 100.0f;
  for (int i = 1; i < 22; i++) {
    if (v <= INA_OCV_V[i]) {
      float f = (v - INA_OCV_V[i - 1]) / (INA_OCV_V[i] - INA_OCV_V[i - 1]);
      return INA_OCV_P[i - 1] + f * (INA_OCV_P[i] - INA_OCV_P[i - 1]);
    }
  }
  return 100.0f;
}

static void inaResetPeaks() {
  inaPeakDis = 0; inaPeakChg = 0; inaPeakP = 0; inaMinV = 9.9f; inaMaxV = 0;
}

static void inaResetSession() {
  inaMahOut = inaMahIn = inaMwhOut = inaMwhIn = 0;
  inaSessionStart = millis();
}

// =====================================================================
// Init / reinit
// =====================================================================
static bool inaTryInit(bool verbose) {
  if (!inaDetect()) {
    inaOk = false; inaActive = false;
    if (verbose) Serial.println("[INA219] tidak terdeteksi (cek SDA=15 SCL=7, VCC, alamat 0x40-0x4F)");
    return false;
  }
  inaWrite16(INA_REG_CONFIG, 0x8000);   // software reset
  delay(2);
  if (!inaApplyConfig()) { inaOk = false; inaActive = false; return false; }
  inaOk = true; inaFail = 0; inaSocInit = false; inaLastInt = 0;
  if (verbose) Serial.printf("[INA219] OK @0x%02X  R=%umOhm  FS=%.0fmA  LSB=%.4fmA  cal=%u\n",
                             inaAddr, (unsigned)inaShuntMohm, inaFsMa, inaCurLsbMa, (unsigned)inaCalReg);
  return true;
}

void inaBegin() {
  inaLoadSettings();
  inaSessionStart = millis();
  inaResetPeaks();
  inaLastSave = millis();
  inaBootCount++;                           // tiap dihidupkan: catat boot + langsung simpan siklus
  inaSaveCounters(true);
  inaTryInit(true);
}

// =====================================================================
// Polling (dipanggil dari battUpdate() tiap INA_POLL_MS)
// =====================================================================
void inaPoll() {
  uint32_t now = millis();
  if (!inaOk) {
    if (now - inaLastRetry > 10000UL) { inaLastRetry = now; inaTryInit(false); }
    return;
  }
  if (now - inaLastPoll < (INA_POLL_MS - 20)) return;
  inaLastPoll = now;

  if (inaMode == 0 || inaMode == 4) { inaActive = false; return; }  // power-down / ADC off

  uint16_t rb = 0, rs = 0, rc = 0, rp = 0;
  bool ok = inaRead16(INA_REG_BUS, rb) && inaRead16(INA_REG_SHUNT, rs) &&
            inaRead16(INA_REG_CURRENT, rc) && inaRead16(INA_REG_POWER, rp);
  if (!ok) {
    if (++inaFail >= 5) { inaOk = false; inaActive = false; Serial.println("[INA219] koneksi hilang"); }
    return;
  }
  inaFail = 0; inaActive = true;
  if (inaMode >= 1 && inaMode <= 3) inaWrite16(INA_REG_CONFIG, inaCfgWord());  // trigger konversi berikutnya

  inaCnvr = (rb >> 1) & 1;
  inaOvf  = rb & 1;
  inaBusV = (float)(rb >> 3) * 0.004f;
  inaShuntMv = (float)(int16_t)rs * 0.01f;
  float iMa = (float)(int16_t)rc * inaCurLsbMa;
  float pMw = (float)rp * inaPwrLsbMw;
  if (inaInvert) iMa = -iMa;
  if (fabsf(iMa) < INA_DEADBAND_MA) iMa = 0;
  if (iMa < 0) pMw = -pMw;
  float vBat = (inaVside == 0) ? (inaBusV + inaShuntMv / 1000.0f) : inaBusV;

  inaI = iMa; inaP = pMw;
  inaBatV = (inaBatV < 2.0f) ? vBat : (inaBatV * 0.6f + vBat * 0.4f);
  inaIavg += (iMa - inaIavg) * ((inaLastInt == 0) ? 1.0f : 0.02f);

  // histeresis status charging
  if (!inaCharging && iMa < -INA_CHG_ON_MA) inaCharging = true;
  else if (inaCharging && iMa > -INA_CHG_OFF_MA) inaCharging = false;

  // puncak / min / max
  if (iMa > inaPeakDis) inaPeakDis = iMa;
  if (-iMa > inaPeakChg) inaPeakChg = -iMa;
  if (fabsf(pMw) > inaPeakP) inaPeakP = fabsf(pMw);
  if (vBat < inaMinV) inaMinV = vBat;
  if (vBat > inaMaxV) inaMaxV = vBat;

  // integrasi (coulomb counting)
  float dtS = (inaLastInt == 0) ? 0.0f : (float)(now - inaLastInt) / 1000.0f;
  inaLastInt = now;
  if (dtS > 3600.0f) dtS = 3600.0f;
  float dMah = iMa * dtS / 3600.0f;
  float dMwh = (vBat * iMa) * dtS / 3600.0f;
  if (dMah > 0) { inaMahOut += dMah; inaMahOutTot += dMah; inaMwhOut += dMwh; inaCycles += dMah / (float)inaCapMah; }
  else          { inaMahIn += -dMah; inaMahInTot += -dMah; inaMwhIn += -dMwh; if (inaLrnActive) inaLrnMah += -dMah; }
  if (dMah != 0) { inaCountersDirty = true; inaUnsavedMah += fabsf(dMah); }
  if (dMah > 0) nyxAppEnergyAdd(dMah);   // NYX-EXTRAS-V1: catat mAh keluar per app

  // SoC: OCV (dikompensasi R dalam) + coulomb counting
  float ocv = vBat + (iMa / 1000.0f) * ((float)inaRintMohm / 1000.0f);
  float ocvSoc = inaOcvSoc(ocv);
  if (!inaSocInit) { inaSocF = ocvSoc; inaSocInit = true; }
  else {
    float cap = inaCapEff();
    if (dMah > 0) inaSocF -= dMah / cap * 100.0f;
    else          inaSocF += (-dMah) * INA_CHG_EFF / cap * 100.0f;
    if (!inaCharging) {
      float a = (fabsf(iMa) < 0.02f * cap) ? 0.01f : 0.0008f;   // tarik pelan ke OCV
      inaSocF += (ocvSoc - inaSocF) * a;
    }
  }
  float fullThr = fmaxf(30.0f, 0.06f * (float)inaCapMah);
  if (iMa < 0 && vBat >= 4.17f && -iMa < fullThr) {                   // charge selesai
    inaSocF = 100.0f;
    if (inaLrnActive) inaLearnFinish();
  }
  if (ocv < 3.0f) inaSocF = 0.0f;
  if (inaSocF < 0) inaSocF = 0;
  if (inaSocF > 100) inaSocF = 100;
  inaSoc = (int)(inaSocF + 0.5f);

  // colok / cabut charger: mulai atau batalkan sesi ukur kapasitas, simpan counter
  if (inaCharging != inaPrevCharging) {
    inaPrevCharging = inaCharging;
    if (inaCharging && inaSocF <= INA_LRN_MAX_START) { inaLrnActive = true; inaLrnSoc0 = inaSocF; inaLrnMah = 0; }
    else inaLrnActive = false;
    inaCountersDirty = true; inaSaveCounters(true);
  }

  // riwayat grafik
  if (inaHistLast == 0 || now - inaHistLast >= INA_HIST_MS) {
    inaHistLast = now;
    float ci = fmaxf(-32000.0f, fminf(32000.0f, iMa));
    float cp = fmaxf(-32000.0f, fminf(32000.0f, pMw));
    inaHistI[inaHistHead] = (int16_t)ci;
    inaHistV[inaHistHead] = (uint16_t)(vBat * 1000.0f);
    inaHistP[inaHistHead] = (int16_t)cp;
    inaHistHead = (inaHistHead + 1) % INA_HIST_N;
    if (inaHistCount < INA_HIST_N) inaHistCount++;
  }

  // simpan counter: tiap INA_SAVE_MAH perubahan, tiap INA_SAVE_MS, atau darurat kalau tegangan mendekati cut-off
  if (inaCountersDirty) {
    if (inaUnsavedMah >= INA_SAVE_MAH || now - inaLastSave >= INA_SAVE_MS ||
        (vBat < 3.35f && now - inaLastSave >= 15000UL)) inaSaveCounters(false);
  }
}

bool inaWantRedraw() {
  static uint32_t last = 0;
  uint32_t now = millis();
  uint32_t gap = (curScreen() == SCR_BATTERY) ? 400 : 5000;
  if (now - last >= gap) { last = now; return true; }
  return false;
}

// ===== BATT_ADC_V1 BEGIN =====
// Aether Board: baterai lewat pembagi tegangan 10k x 10k di GPIO RP_BATT_ADC_PIN (display_conf.h).
// Dipakai hanya kalau INA219 tidak terdeteksi. Hanya tegangan yang terukur: arus / status charging tidak diketahui.
// DEVPINS_V1: pin (rpPinV[DP_BATT_ADC], -1 = tanpa ADC), pembagi (devBattDiv) dan ofset (devBattCal) kini runtime;
// nilai bawaan di devpins.h (RP_BATT_ADC_PIN / RP_BATT_DIV / RP_BATT_CAL_V), diubah dari app Opsi Developer.
static float    battAdcV = 0.0f, battAdcSoc = 100.0f;
static bool     battAdcInit = false;
static uint32_t battAdcLast = 0;
// hasil hitung terakhir (dibaca app Opsi Developer untuk menampilkan perhitungan ADC)
uint16_t battAdcDbgRaw = 0, battAdcDbgMv = 0;     // rata-rata analogRead (0..4095) & analogReadMilliVolts di pin
float    battAdcDbgPinV = 0.0f, battAdcDbgVbat = 0.0f;   // volt di pin; Vbat hasil rumus (sebelum clamp & filter)
static void battAdcUpdate() {
  uint32_t now = millis();
  battCharging = false;                                   // tanpa sensor arus: charging tidak terdeteksi
  if (battAdcInit && now - battAdcLast < 1000UL) return;  // 1x per detik cukup
  battAdcLast = now;
  const int pin = rpPinV[DP_BATT_ADC];
  if (pin < 0) return;
  uint32_t acc = 0, accRaw = 0;
  for (int i = 0; i < 16; i++) { acc += analogReadMilliVolts(pin); accRaw += analogRead(pin); }   // rata-rata 16 sampel (redam noise ADC)
  float pinVoltage = ((float)acc / 16.0f) / 1000.0f;      // volt di pin ADC
  // Pembagi (bawaan 10k x 10k = 2.0) + kalibrasi titik ukur multimeter (3.61V vs 3.58V -> +0.03V)
  float vBat = (pinVoltage * devBattDiv) + devBattCal;
  battAdcDbgRaw = (uint16_t)(accRaw / 16); battAdcDbgMv = (uint16_t)(acc / 16); battAdcDbgPinV = pinVoltage; battAdcDbgVbat = vBat;
  float batteryVoltage = constrain(vBat, 0.0f, 5.0f);
  float soc = inaOcvSoc(batteryVoltage);
  if (!battAdcInit) { battAdcV = batteryVoltage; battAdcSoc = soc; battAdcInit = true; }
  else { battAdcV += (batteryVoltage - battAdcV) * 0.2f; battAdcSoc += (soc - battAdcSoc) * 0.1f; }   // halus: UI tidak loncat
  battVoltage = battAdcV;
  int p = (int)(battAdcSoc + 0.5f);
  battPercent = (p < 0) ? 0 : (p > 100 ? 100 : p);
}
// ===== BATT_ADC_V1 END =====

// Dipanggil dari loop(); mengisi variabel global batt* yang dipakai seluruh UI.
void battUpdate() {
  inaPoll();
  if (!inaOk) {                                           // BATT_ADC_V1: tanpa INA219 -> ADC (lalu lanjut ke peringatan di bawah)
    if (rpPinV[DP_BATT_ADC] >= 0) battAdcUpdate();        // DEVPINS_V1: ada/tidaknya ADC ditentukan pin runtime
    else { battCharging = false; return; }
  } else
  if (inaActive) {
    battVoltage = inaBatV;
    battPercent = inaSoc;
    battCharging = inaCharging;
  }

  if (battPercent <= 5 && !battWarnedCritical) {
    battWarnedCritical = true;
    showToast(RPTR("Baterai kritis! Segera cas."));
    diNotify('B', RPTR("Baterai kritis!"), T().danger, 2200, false, "", SCR_BATTERY, true);
    if (curScreen() != SCR_BATTERY) notifAdd(SCR_BATTERY);
  } else if (battPercent <= 15 && !battWarnedLow) {
    battWarnedLow = true;
    showToast(RPTR("Baterai lemah, segera cas."));
    diNotify('B', RPTR("Baterai lemah"), T().accent, 1900, false, "", SCR_BATTERY, true);
    if (curScreen() != SCR_BATTERY) notifAdd(SCR_BATTERY);
  } else if (battPercent > 25) {
    battWarnedLow = false; battWarnedCritical = false;
  }
}

// =====================================================================
// Pengaturan interaktif
// =====================================================================
static int inaShuntIdx() {
  int best = 0; int bd = 100000;
  for (int i = 0; i < 7; i++) {
    int d = (int)INA_SHUNT_PRESET[i] - (int)inaShuntMohm; if (d < 0) d = -d;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

static bool inaReinit() {
  inaOk = false; inaFail = 0;
  return inaTryInit(true);
}

static void inaAdjust(uint8_t tab, uint8_t row, int dir) {
  if (tab == 2) {
    switch (row) {
      case 0: inaCfgAddr = (uint8_t)((inaCfgAddr + dir + 17) % 17); inaSaveSettings(); inaReinit(); return;
      case 1: { int i = (inaShuntIdx() + dir + 7) % 7; inaShuntMohm = INA_SHUNT_PRESET[i]; break; }
      case 2: inaPga = (uint8_t)((inaPga + dir + 4) % 4); break;
      case 3: inaBrng ^= 1; break;
      case 4: inaBadc = (uint8_t)((inaBadc + dir + 11) % 11); break;
      case 5: inaSadc = (uint8_t)((inaSadc + dir + 11) % 11); break;
      case 6: inaMode = (uint8_t)((inaMode + dir + 8) % 8); break;
      default: return;
    }
    inaSaveSettings();
    if (inaOk) { inaApplyConfig(); inaLastInt = 0; }
  } else if (tab == 3) {
    switch (row) {
      case 0: { int c = (int)inaCapMah + dir * 100; if (c < 200) c = 200; if (c > 10000) c = 10000; inaCapMah = (uint16_t)c; break; }
      case 1: { int r = (int)inaRintMohm + dir * 10; if (r < 0) r = 0; if (r > 500) r = 500; inaRintMohm = (uint16_t)r; break; }
      case 2: inaInvert = !inaInvert; break;
      case 3: inaVside ^= 1; break;
      default: return;
    }
    inaSaveSettings();
  }
}

static void inaAction(uint8_t tab, uint8_t idx) {
  if (tab == 2) {
    if (idx == 0) {   // reset sensor
      if (inaOk) { inaWrite16(INA_REG_CONFIG, 0x8000); delay(2); inaApplyConfig(); inaLastInt = 0; showToast(RPTR("INA219 di-reset")); }
      else showToast("Sensor offline");
    } else {          // scan ulang
      showToast(inaReinit() ? RPTR("INA219 ditemukan") : RPTR("INA219 tidak ditemukan"));
    }
  } else if (tab == 3) {
    if (idx == 0) { inaSocInit = false; inaLrnActive = false; showToast(RPTR("Persen dihitung ulang")); }
    else if (idx == 1) { inaResetSession(); showToast(RPTR("Counter sesi di-reset")); }
    else if (idx == 2) { inaMahOutTot = 0; inaMahInTot = 0; inaCycles = 0; inaCapLearn = 0; inaLrnActive = false; inaSaveCounters(true); showToast(RPTR("Total, siklus & kapasitas di-reset")); }
    else { inaResetPeaks(); showToast(RPTR("Puncak di-reset")); }
  }
}

// =====================================================================
// UI
// =====================================================================
static void inaFmtDur(char* b, size_t n, float h) {
  if (!(h >= 0) || h > 999.0f) { snprintf(b, n, "--"); return; }
  int m = (int)(h * 60.0f + 0.5f);
  snprintf(b, n, "%dj %02dm", m / 60, m % 60);
}

static const char* inaStatusText() {
  if (!inaOk) return "Sensor offline";
  if (!inaActive) return "Standby";
  if (inaCharging) return "Mengisi";
  if (inaSoc >= 100) return RPTR("Penuh");
  if (inaI == 0) return "Idle";
  return "Memakai";
}

static uint16_t inaSocColor() {
  if (inaCharging) return T().good;
  return (inaSoc <= 15) ? T().danger : (inaSoc <= 35) ? (uint16_t)0xFD20 : T().good;
}

// ===== BATT_UX_V2 BEGIN =====
// Tampilan baru app Baterai: cincin persen, kartu statistik, grafik dgn area terisi.
// Hanya kode gambar; pembacaan INA219, counter, dan geometri sentuh (tab, pil grafik, baris Sensor/Baterai) tidak berubah.
// ===== BATT_FIT_V1 BEGIN =====
// Potong teks di tempat (tambah "..") supaya lebarnya <= maxW. cap = ukuran buffer.
static void rpFit(LGFX_Sprite& s, char* buf, size_t cap, int maxW) {
  int L = (int)strlen(buf);
  if (L < 2 || (int)s.textWidth(buf) <= maxW) return;
  char t[72];
  for (int cut = L - 1; cut > 1; cut--) {
    int n = cut; if (n > (int)sizeof(t) - 3) n = (int)sizeof(t) - 3;
    if (n > (int)cap - 3) n = (int)cap - 3;
    if (n < 1) break;
    memcpy(t, buf, n); t[n] = '.'; t[n + 1] = '.'; t[n + 2] = 0;
    if ((int)s.textWidth(t) <= maxW) { strlcpy(buf, t, cap); return; }
  }
  buf[1] = 0;
}

// Tulis teks dibungkus per kata di dalam lebar maxW (maks maxLines baris; baris terakhir diberi ".." kalau masih ada sisa).
// Mengembalikan tinggi yg terpakai.
static int rpWrapText(LGFX_Sprite& s, int x, int y, int maxW, int lineH, int maxLines, const char* text, uint16_t col) {
  String all = text; all.trim();
  const int L = (int)all.length();
  int pos = 0, n = 0;
  s.setTextColor(col);
  while (pos < L && n < maxLines) {
    int cut = pos, lastSp = -1;
    for (int i = pos; i < L; i++) {
      if (all[i] == ' ') lastSp = i;
      String t = all.substring(pos, i + 1);
      if ((int)s.textWidth(t.c_str()) > maxW) { cut = i; break; }
      cut = i + 1;
    }
    int brk;
    if (cut >= L) brk = L;
    else if (lastSp > pos) brk = lastSp;
    else brk = (cut > pos) ? cut : pos + 1;
    String ln = all.substring(pos, brk);
    if (n == maxLines - 1 && brk < L) {
      while (ln.length() > 1 && (int)s.textWidth((ln + "..").c_str()) > maxW) ln.remove(ln.length() - 1);
      ln += "..";
    }
    s.setCursor(x, y + n * lineH); s.print(ln.c_str());
    n++; pos = brk;
    while (pos < L && all[pos] == ' ') pos++;
  }
  return n * lineH;
}
// ===== BATT_FIT_V1 END =====

static void inaCard(LGFX_Sprite& s, int x, int y, int w, int h) {
  uint16_t c = blend565(T().surface2, 0xFFFF, 14);
  nyxAaRR(s, x, y, w, h, 9, c);
  nyxAaRRRing(s, x, y, w, h, 9, blend565(c, 0xFFFF, 34));
}

static void inaRing(LGFX_Sprite& s, int cx, int cy, int ro, int ri, float pct, uint16_t track, uint16_t col) {
  s.fillArc(cx, cy, ri, ro, 0.0f, 180.0f, track);
  s.fillArc(cx, cy, ri, ro, 180.0f, 360.0f, track);
  if (pct <= 0.0f) return;
  if (pct > 100.0f) pct = 100.0f;
  float a1 = 270.0f + 3.6f * pct;
  if (a1 <= 360.0f) s.fillArc(cx, cy, ri, ro, 270.0f, a1, col);
  else { s.fillArc(cx, cy, ri, ro, 270.0f, 360.0f, col); s.fillArc(cx, cy, ri, ro, 0.0f, a1 - 360.0f, col); }
}

static void inaBolt(LGFX_Sprite& s, int x, int y, uint16_t c) {   // petir 7x14
  s.fillTriangle(x + 3, y, x - 3, y + 7, x + 1, y + 7, c);
  s.fillTriangle(x - 1, y + 6, x + 4, y + 6, x - 2, y + 13, c);
}

static void inaRow(LGFX_Sprite& s, int x, int w, int y, const char* k, const char* v, uint16_t vc) {
  s.setTextSize(1);
  char kb[40]; strlcpy(kb, k, sizeof(kb));
  rpFit(s, kb, sizeof(kb), w - (int)s.textWidth(v) - 10);   // BATT_FIT_V1
  s.setTextColor(T().subtext); s.setCursor(x, y); s.print(kb);
  s.setTextColor(vc);
  s.setCursor(x + w - (int)s.textWidth(v), y); s.print(v);
}

static void inaTile(LGFX_Sprite& s, int x, int y, int w, int h, const char* k, const char* v, uint16_t vc) {
  inaCard(s, x, y, w, h);
  s.setClipRect(x + 3, y, w - 6, h);   // BATT_FIT_V1: teks tidak bisa keluar dari kartu
  s.setTextSize(1);
  char kb[24]; strlcpy(kb, k, sizeof(kb));
  for (int kl = (int)strlen(kb); kl > 1 && (int)s.textWidth(kb) > w - 14; ) kb[--kl] = 0;   // kolom sempit: label dipotong
  s.setTextColor(T().subtext); s.setCursor(x + 9, y + 7); s.print(kb);
  int sz = 2; s.setTextSize(2);
  if ((int)s.textWidth(v) > w - 16) { sz = 1; s.setTextSize(1); }
  char vb2[28]; strlcpy(vb2, v, sizeof(vb2));
  rpFit(s, vb2, sizeof(vb2), w - 16);
  s.setTextColor(vc);
  s.setCursor(x + 9, y + h - 8 - (sz == 2 ? 16 : 8)); s.print(vb2);
  s.setTextSize(1);
  s.clearClipRect();
}

static float inaHistVal(int k, uint8_t m) {
  int idx = ((int)inaHistHead - (int)inaHistCount + k + 2 * INA_HIST_N) % INA_HIST_N;
  if (m == 0) return (float)inaHistI[idx];
  if (m == 1) return (float)inaHistV[idx] / 1000.0f;
  return (float)inaHistP[idx];
}

// garis arus kecil (10 menit terakhir) utk kartu ringkasan
static void inaSpark(LGFX_Sprite& s, int x, int y, int w, int h, uint16_t col) {
  int n = (int)inaHistCount;
  if (n < 2 || w < 8 || h < 8) {
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(x, y + h / 2 - 4); s.print(RPTR("Mengumpulkan data..."));
    return;
  }
  float lo = 0, hi = 0;
  for (int k = 0; k < n; k++) { float v = inaHistVal(k, 0); if (v < lo) lo = v; if (v > hi) hi = v; }
  if (hi - lo < 20.0f) { hi += 10.0f; lo -= 10.0f; }
  float span = hi - lo;
  int zy = y + (int)((hi - 0.0f) / span * (float)(h - 1));
  for (int gx = x; gx < x + w; gx += 4) s.drawPixel(gx, zy, T().divider);
  int px = x, py = zy;
  for (int i = 0; i < w; i++) {
    int k = (n == 1) ? 0 : (i * (n - 1)) / (w - 1);
    int yy = y + (int)((hi - inaHistVal(k, 0)) / span * (float)(h - 1));
    if (i > 0) s.drawLine(px, py, x + i, yy, col);
    px = x + i; py = yy;
  }
}

static void inaDrawSummary(LGFX_Sprite& s, int top, int bottom) {
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);
  const int W = SCR_W, avail = bottom - top;
  if (!inaOk) {
    int errLines = (avail - 40) / 10; if (errLines < 3) errLines = 3; if (errLines > 8) errLines = 8;
    inaCard(s, 6, top, W - 12, 34 + errLines * 10);
    s.setTextColor(T().danger); s.setCursor(16, top + 9); s.print(RPTR("INA219 tidak terdeteksi"));
    static char inaHelp[260];                              // DEVPINS_V1: pin bus sensor kini runtime -> teks dirangkai saat digambar
    snprintf(inaHelp, sizeof(inaHelp), "Cek wiring: VCC 3.3V, GND, SDA = GPIO%d, SCL = GPIO%d. Shunt dipasang seri di kabel (+) baterai (baterai -> VIN+, beban -> VIN-). Alamat 0x40-0x4F (A0/A1). Buka tab Sensor > Scan ulang setelah memperbaiki.",
             (int)rpPinV[DP_SENS_SDA], (int)rpPinV[DP_SENS_SCL]);
    rpWrapText(s, 16, top + 26, W - 12 - 20, 10, errLines, inaHelp, T().subtext);
    return;
  }
  const bool land = SCR_W > SCR_H;
  const uint16_t bc = inaSocColor();

  // ---- teks nilai ----
  char vV[16], vI[20], vP[20], vT[20], vCap[28], vSes[28], vCyc[16];
  snprintf(vV, sizeof(vV), "%.3f V", inaBatV);
  snprintf(vI, sizeof(vI), fabsf(inaI) >= 100.0f ? "%+.0f mA" : "%+.1f mA", inaI);
  snprintf(vP, sizeof(vP), "%.0f mW", fabsf(inaP));
  const char* tlb = RPTR("Sisa waktu"); float hrs = -1;
  if (inaCharging) {
    tlb = RPTR("Penuh dalam");
    if (-inaIavg > 5.0f) hrs = ((100.0f - inaSocF) / 100.0f * inaCapEff()) / (-inaIavg);
  } else if (inaIavg > 5.0f) {
    hrs = (inaSocF / 100.0f * inaCapEff()) / inaIavg;
  }
  { char d[16]; inaFmtDur(d, sizeof(d), hrs); snprintf(vT, sizeof(vT), "~%s", d); }
  if (inaCapLearn > 0) snprintf(vCap, sizeof(vCap), "%.0f mAh (%.0f%%)", (double)inaCapLearn, (double)(100.0f * inaCapLearn / (float)inaCapMah));
  else snprintf(vCap, sizeof(vCap), "%s", inaLrnActive ? RPTR("mengukur...") : RPTR("belum terukur"));
  snprintf(vSes, sizeof(vSes), "-%.0f / +%.0f mAh", inaMahOut, inaMahIn);
  snprintf(vCyc, sizeof(vCyc), "%.2f", (double)inaCycles);

  // ---- kartu cincin ----
  int R = land ? (avail - 24) / 2 : 54;
  if (R > 58) R = 58;
  if (R < 32) R = 32;
  const int ringW = (R >= 48) ? 9 : 7;
  int cardW, cardH, gcx, gcy;
  if (land) { cardW = 2 * R + 24; cardH = avail; gcx = 6 + cardW / 2; gcy = top + 10 + R; }
  else      { cardW = W - 12;     cardH = 2 * R + 18; gcx = 6 + 12 + R; gcy = top + cardH / 2; }
  inaCard(s, 6, top, cardW, cardH);
  inaRing(s, gcx, gcy, R, R - ringW, (float)inaSoc, blend565(T().surface, 0xFFFF, 30), bc);
  {
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", inaSoc);
    int sz = (R >= 46) ? 3 : 2;
    s.setTextSize(sz); s.setTextColor(bc);
    int ph = sz * 8;
    s.setCursor(gcx - (int)s.textWidth(pb) / 2, gcy - ph / 2 - 6); s.print(pb);
    s.setTextSize(1); s.setTextColor(T().text);
    const char* st = inaStatusText();
    int sw = (int)s.textWidth(st);
    int sx = gcx - sw / 2, sy = gcy + ph / 2 - 1;
    if (inaCharging) { inaBolt(s, sx - 6, sy - 3, T().good); sx += 5; }
    s.setCursor(sx, sy); s.print(st);
  }
  if (!land) {   // sisi kanan kartu: sisa waktu + garis arus
    int rx = gcx + R + 16, rw = (W - 14) - rx;
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(rx, top + 10); s.print(tlb);
    s.setTextSize(2); s.setTextColor(T().text);
    if ((int)s.textWidth(vT) > rw) s.setTextSize(1);
    s.setCursor(rx, top + 22); s.print(vT);
    s.setTextSize(1); s.setTextColor(T().subtext); s.setCursor(rx, top + 46); s.print(RPTR("Arus"));
    inaSpark(s, rx, top + 58, rw, cardH - 58 - 12, bc);
  } else if (avail - (10 + 2 * R) >= 78) {   // landscape yg lega: sisa waktu di bawah cincin
    int ry = gcy + R + 8;
    s.setTextSize(1); s.setTextColor(T().subtext);
    s.setCursor(gcx - (int)s.textWidth(tlb) / 2, ry); s.print(tlb);
    s.setTextSize(2); s.setTextColor(T().text);
    s.setCursor(gcx - (int)s.textWidth(vT) / 2, ry + 12); s.print(vT);
    s.setTextSize(1);
  }

  // ---- 4 kartu angka + baris rincian ----
  int tx0, tw, ty0;
  if (land) { tx0 = 6 + cardW + 6; tw = W - 6 - tx0; ty0 = top; }
  else      { tx0 = 6; tw = W - 12; ty0 = top + cardH + 6; }
  const int colW = (tw - 6) / 2;
  int th = 44;
  const int remain = bottom - ty0;
  if (remain < 2 * th + 6 + 3 * 14 + 8) th = (remain - 6 - 3 * 14 - 8) / 2;
  if (th < 32) th = 32;
  if (th > 44) th = 44;
  const bool timeTile = land && (avail - (10 + 2 * R) < 78);   // landscape sempit: sisa waktu jadi kartu
  const uint16_t cI = inaCharging ? T().good : T().text;
  inaTile(s, tx0,              ty0,          colW, th, "Tegangan", vV, T().text);
  inaTile(s, tx0 + colW + 6,   ty0,          colW, th, inaCharging ? "Arus (mengisi)" : "Arus (memakai)", vI, cI);
  inaTile(s, tx0,              ty0 + th + 6, colW, th, "Daya", vP, T().text);
  if (timeTile) inaTile(s, tx0 + colW + 6, ty0 + th + 6, colW, th, tlb, vT, T().text);
  else          inaTile(s, tx0 + colW + 6, ty0 + th + 6, colW, th, "Kapasitas", vCap, T().text);

  int y = ty0 + 2 * th + 6 + 8; const int lh = 14;
  const int rx0 = tx0 + 4, rw0 = tw - 8;
  if (timeTile && y + 8 <= bottom) { inaRow(s, rx0, rw0, y, "Kapasitas terukur", vCap, T().text); y += lh; }
  if (y + 8 <= bottom) { inaRow(s, rx0, rw0, y, "Sesi keluar / masuk", vSes, T().text); y += lh; }
  if (y + 8 <= bottom) {
    if (inaOvf) inaRow(s, rx0, rw0, y, "PERINGATAN", "OVERFLOW: turunkan gain/R", T().danger);
    else {
      char kb[28]; snprintf(kb, sizeof(kb), "Siklus (boot ke-%lu)", (unsigned long)inaBootCount);
      inaRow(s, rx0, rw0, y, kb, vCyc, T().subtext);
    }
  }
}

static void inaDrawGraph(LGFX_Sprite& s, int top, int bottom) {
  static const char* nm[3] = {"Arus", "Tegangan", "Daya"};
  int pw = (SCR_W - 16 - 8) / 3;
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);
  for (int i = 0; i < 3; i++) {
    int x = 8 + i * (pw + 4);
    bool act = (i == inaGraphMetric);
    nyxAaRR(s, x, top, pw, 16, 8, act ? T().accent : blend565(T().surface2, 0xFFFF, 14));
    s.setTextColor(act ? appIconInk(T().accent) : T().subtext);
    s.setCursor(x + (pw - (int)s.textWidth(nm[i])) / 2, top + 4); s.print(nm[i]);
  }
  int gx = 10, gy = top + 22, gw = SCR_W - 20, gh = bottom - gy;   // BATT_FIT_V1: kartu mencakup baris min/rata/maks
  if (gh < 40) gh = 40;
  inaCard(s, gx, gy, gw, gh);
  int n = (int)inaHistCount;
  if (n < 2) {
    s.setTextColor(T().subtext);
    s.setCursor(gx + 10, gy + gh / 2 - 4); s.print(RPTR("Mengumpulkan data..."));
    return;
  }
  uint8_t m = inaGraphMetric;
  float mn = 1e9f, mx = -1e9f, sum = 0;
  for (int k = 0; k < n; k++) {
    float v = inaHistVal(k, m); sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  float lo = mn, hi = mx;
  if (m != 1) { if (lo > 0) lo = 0; if (hi < 0) hi = 0; }
  float span = hi - lo;
  float minSpan = (m == 1) ? 0.05f : 10.0f;
  if (span < minSpan) { float mid = (hi + lo) / 2; lo = mid - minSpan / 2; hi = mid + minSpan / 2; span = minSpan; }
  lo -= span * 0.08f; hi += span * 0.08f; span = hi - lo;
  int ix0 = gx + 6, iw = gw - 12, iy0 = gy + 6, ih = gh - 12 - 14;
  // garis bantu putus-putus (4 bagian)
  for (int q = 1; q < 4; q++) {
    int yy = iy0 + (ih * q) / 4;
    for (int xx = ix0; xx < ix0 + iw; xx += 5) s.drawPixel(xx, yy, T().divider);
  }
  int baseY = iy0 + ih - 1;
  if (m != 1 && lo < 0 && hi > 0) {
    int zy = iy0 + (int)((hi - 0) / span * ih);
    s.drawFastHLine(ix0, zy, iw, blend565(T().divider, 0xFFFF, 90));
    baseY = zy;
  }
  const uint16_t areaC = blend565(blend565(T().surface2, 0xFFFF, 14), T().accent, 80);
  float step = (float)iw / (float)(INA_HIST_N - 1);
  float px = 0, py = 0;
  for (int k = 0; k < n; k++) {
    float x = (float)(ix0 + iw) - (float)(n - 1 - k) * step;
    float y = (float)iy0 + (hi - inaHistVal(k, m)) / span * (float)ih;
    if (k > 0) {
      int xa = (int)px, xb = (int)x;
      for (int xi = xa; xi <= xb; xi++) {
        float t = (xb == xa) ? 0.0f : (float)(xi - xa) / (float)(xb - xa);
        int yi = (int)(py + t * (y - py));
        int y0 = yi < baseY ? yi : baseY, y1 = yi < baseY ? baseY : yi;
        s.drawFastVLine(xi, y0, y1 - y0 + 1, areaC);
      }
    }
    px = x; py = y;
  }
  px = 0; py = 0;
  for (int k = 0; k < n; k++) {
    float x = (float)(ix0 + iw) - (float)(n - 1 - k) * step;
    float y = (float)iy0 + (hi - inaHistVal(k, m)) / span * (float)ih;
    if (k > 0) { s.drawLine((int)px, (int)py, (int)x, (int)y, T().accent); s.drawLine((int)px, (int)py + 1, (int)x, (int)y + 1, T().accent); }
    px = x; py = y;
  }
  nyxAaCircle(s, (int)px, (int)py, 3, T().accent);
  nyxAaCircle(s, (int)px, (int)py, 1, 0xFFFF);
  // nilai terakhir (kanan atas grafik)
  {
    float cur = inaHistVal(n - 1, m); char cb[24];
    if (m == 1) snprintf(cb, sizeof(cb), "%.3f V", cur);
    else snprintf(cb, sizeof(cb), (m == 0) ? "%+.0f mA" : "%.0f mW", cur);
    s.setTextSize(2); s.setTextColor(T().text);
    int cw2 = (int)s.textWidth(cb);
    if (cw2 > gw / 2) { s.setTextSize(1); cw2 = (int)s.textWidth(cb); }
    nyxAaRR(s, gx + gw - cw2 - 14, gy + 5, cw2 + 10, (s.fontHeight() + 4), 6, blend565(T().surface2, 0x0000, 90));
    s.setCursor(gx + gw - cw2 - 9, gy + 7); s.print(cb);
    s.setTextSize(1);
  }
  char b[48];
  if (m == 1) snprintf(b, sizeof(b), "min %.2f  rata %.2f  maks %.2f V", mn, sum / n, mx);
  else snprintf(b, sizeof(b), "min %.0f  rata %.0f  maks %.0f %s", mn, sum / n, mx, (m == 0) ? "mA" : "mW");
  rpFit(s, b, sizeof(b), gw - 16);
  s.setTextColor(T().subtext); s.setCursor(gx + 8, gy + gh - 15); s.print(b);
}
// ===== BATT_UX_V2 END =====

// ---- tab Sensor (2) & Baterai (3): baris nilai + tombol ----
static int inaRowsFor(uint8_t tab) { return (tab == 2) ? 7 : 4; }
static int inaBtnsFor(uint8_t tab) { return (tab == 2) ? 2 : 4; }
static int inaBottom() { return backY() - 4; }
static int inaRowH(uint8_t tab) {
  int avail = inaBottom() - INA_TOP;
  int btnRows = (inaBtnsFor(tab) + 1) / 2;
  int reserve = btnRows * 22 + 6;
  int rh = (avail - reserve) / inaRowsFor(tab);
  if (rh > 22) rh = 22;
  if (rh < 15) rh = 15;
  return rh;
}

static void inaRowInfo(uint8_t tab, uint8_t row, char* lb, char* vb, int n) {
  lb[0] = 0; vb[0] = 0;
  if (tab == 2) {
    switch (row) {
      case 0: snprintf(lb, n, "Alamat I2C");
              if (inaCfgAddr == 0) snprintf(vb, n, "Auto 0x%02X", inaAddr); else snprintf(vb, n, "0x%02X", 0x3F + inaCfgAddr); break;
      case 1: snprintf(lb, n, "Resistor shunt"); snprintf(vb, n, "%u mOhm", (unsigned)inaShuntMohm); break;
      case 2: snprintf(lb, n, "Gain PGA"); snprintf(vb, n, "%s", INA_PGA_NAME[inaPga & 3]); break;
      case 3: snprintf(lb, n, "Rentang bus"); snprintf(vb, n, "%s", inaBrng ? "32 V" : "16 V"); break;
      case 4: snprintf(lb, n, "ADC bus"); snprintf(vb, n, "%s", INA_ADC_NAME[inaBadc]); break;
      case 5: snprintf(lb, n, "ADC shunt"); snprintf(vb, n, "%s", INA_ADC_NAME[inaSadc]); break;
      case 6: snprintf(lb, n, "Mode"); snprintf(vb, n, "%s", INA_MODE_NAME[inaMode & 7]); break;
    }
  } else {
    switch (row) {
      case 0: snprintf(lb, n, "Kapasitas"); snprintf(vb, n, "%u mAh", (unsigned)inaCapMah); break;
      case 1: snprintf(lb, n, "R dalam"); snprintf(vb, n, "%u mOhm", (unsigned)inaRintMohm); break;
      case 2: snprintf(lb, n, "Arah arus"); snprintf(vb, n, "%s", inaInvert ? "Dibalik" : "Normal"); break;
      case 3: snprintf(lb, n, "Pin Vbat"); snprintf(vb, n, "%s", inaVside ? "VIN-" : "VIN+"); break;
    }
  }
}

static void inaDrawList(LGFX_Sprite& s, uint8_t tab, int top) {
  static const char* btnS[2] = {"Reset sensor", "Scan ulang"};
  static const char* btnB[4] = {"Hitung ulang %", "Reset sesi", "Reset total", "Reset puncak"};
  int rh = inaRowH(tab), nr = inaRowsFor(tab), nb = inaBtnsFor(tab);
  int vx0 = SCR_W / 2;
  char lb[24], vb[28];
  s.setTextSize(1);
  for (int r = 0; r < nr; r++) {
    int y = top + r * rh;
    iosRR(s, 6, y, SCR_W - 12, rh - 2, 4, T().surface2);
    inaRowInfo(tab, (uint8_t)r, lb, vb, (int)sizeof(lb));
    int ty = y + (rh - 2 - 8) / 2;
    rpFit(s, lb, sizeof(lb), vx0 - 4 - 12);                 // BATT_FIT_V1: label tidak masuk ke area nilai
    s.setTextColor(T().subtext); s.setCursor(12, ty); s.print(lb);
    s.setTextColor(T().accent);
    s.setCursor(vx0 + 2, ty); s.print("<");
    s.setCursor(SCR_W - 18, ty); s.print(">");
    s.setTextColor(T().text);
    rpFit(s, vb, sizeof(vb), SCR_W - 22 - (vx0 + 12));      // nilai tetap di antara dua panah
    int cx = (vx0 + 12 + SCR_W - 22) / 2;
    s.setCursor(cx - (int)s.textWidth(vb) / 2, ty); s.print(vb);
  }
  int by = top + nr * rh + 4;
  int bw = (SCR_W - 12 - 6) / 2;
  for (int i = 0; i < nb; i++) {
    int bx = 6 + (i % 2) * (bw + 6);
    int yy = by + (i / 2) * 22;
    iosRR(s, bx, yy, bw, 20, 6, T().surface2);
    char bt[24]; strlcpy(bt, (tab == 2) ? btnS[i] : btnB[i], sizeof(bt));
    rpFit(s, bt, sizeof(bt), bw - 10);
    s.setTextColor(T().accent);
    s.setCursor(bx + (bw - (int)s.textWidth(bt)) / 2, yy + 6); s.print(bt);
  }
}

// =====================================================================
// Entry app Baterai (dipanggil lewat tabel apps[] di phone.ino)
// =====================================================================
void battEnter() { battUpdate(); needRedraw = true; }
void battExit()  { inaSaveCounters(false); }

void drawBatteryApp(LGFX_Sprite& s) {
  s.fillSprite(T().bg);
  drawStatusBar(s);
  s.setFont(emjF(&lgfx::fonts::Font0));
  s.setTextSize(1);
  {   // BATT_UX_V2: ikon baterai kecil + judul
    uint16_t ic = inaOk ? inaSocColor() : T().subtext;
    nyxAaRRRing(s, 10, 26, 15, 9, 2, T().text);
    s.fillRect(25, 29, 2, 3, T().text);
    int fw = inaOk ? (11 * inaSoc) / 100 : 0;
    if (fw > 0) nyxAaRR(s, 12, 28, fw, 5, 1, ic);
    iosTitle(s, 32, 26, RPTR("Baterai"));
  }

  char chip[16]; uint16_t cc = T().good;
  if (!inaOk) { snprintf(chip, sizeof(chip), "OFFLINE"); cc = T().danger; }
  else if (inaOvf) { snprintf(chip, sizeof(chip), "OVERFLOW"); cc = T().danger; }
  else if (!inaActive) { snprintf(chip, sizeof(chip), "STANDBY"); cc = T().accent; }
  else snprintf(chip, sizeof(chip), "0x%02X OK", inaAddr);
  int cw = (int)s.textWidth(chip) + 18;
  nyxAaRR(s, SCR_W - cw - 6, 23, cw, 15, 7, blend565(T().surface2, 0xFFFF, 14));
  nyxAaCircle(s, SCR_W - cw + 4, 30, 2, cc);
  s.setTextColor(cc); s.setCursor(SCR_W - cw + 10, 27); s.print(chip);

  {   // tab: kontrol segmen (satu wadah, segmen aktif berwarna aksen) -- area sentuh sama seperti sebelumnya
    int tw = (SCR_W - 12) / 5;
    nyxAaRR(s, 6, INA_TAB_Y, tw * 5, INA_TAB_H, INA_TAB_H / 2, blend565(T().surface2, 0xFFFF, 14));
    for (int i = 0; i < 5; i++) {
      int x = 6 + i * tw; bool act = (i == inaTab);
      if (act) nyxAaRR(s, x + 2, INA_TAB_Y + 2, tw - 4, INA_TAB_H - 4, (INA_TAB_H - 4) / 2, T().accent);
      s.setTextColor(act ? appIconInk(T().accent) : T().subtext);
      s.setCursor(x + (tw - (int)s.textWidth(INA_TAB_NAME[i])) / 2, INA_TAB_Y + (INA_TAB_H - 8) / 2); s.print(INA_TAB_NAME[i]);
    }
  }

  int top = INA_TOP, bottom = inaBottom();
  if (inaTab == 0) inaDrawSummary(s, top, bottom);
  else if (inaTab == 1) inaDrawGraph(s, top, bottom);
  else if (inaTab == 4) nyxDrawAppEnergy(s, top, bottom);   // NYX-EXTRAS-V1
  else inaDrawList(s, inaTab, top);

  drawBack(s);
  drawToast(s);
}

void battTouch(int x, int y, bool held, bool isNew) {
  if (isNew) {
    inaRepRow = -1;
    if (isBack(x, y)) { navBack(); return; }
    if (y >= INA_TAB_Y && y < INA_TAB_Y + INA_TAB_H) {
      int tw = (SCR_W - 12) / 5; int i = (x - 6) / tw;
      if (x >= 6 && i >= 0 && i < 5 && inaTab != (uint8_t)i) { inaTab = (uint8_t)i; needRedraw = true; }
      return;
    }
    int top = INA_TOP;
    if (inaTab == 1) {
      if (y >= top && y < top + 16) {
        int pw = (SCR_W - 16 - 8) / 3;
        for (int i = 0; i < 3; i++) {
          int bx = 8 + i * (pw + 4);
          if (x >= bx && x < bx + pw) { inaGraphMetric = (uint8_t)i; needRedraw = true; }
        }
      }
      return;
    }
    if (inaTab == 4) { nyxAppEnergyTouch(x, y, top, inaBottom()); needRedraw = true; return; }   // NYX-EXTRAS-V1
    if (inaTab >= 2) {
      int rh = inaRowH(inaTab), nr = inaRowsFor(inaTab), nb = inaBtnsFor(inaTab);
      if (y >= top && y < top + nr * rh) {
        int r = (y - top) / rh;
        int vx0 = SCR_W / 2;
        int dir = +1;
        if (x >= vx0 && x < vx0 + (SCR_W - vx0) / 2) dir = -1;
        inaAdjust(inaTab, (uint8_t)r, dir);
        if (inaTab == 3 && r < 2) { inaRepRow = r; inaRepDir = dir; inaRepT0 = millis(); inaRepLast = inaRepT0; }
        needRedraw = true;
        return;
      }
      int by = top + nr * rh + 4;
      int bw = (SCR_W - 12 - 6) / 2;
      for (int i = 0; i < nb; i++) {
        int bx = 6 + (i % 2) * (bw + 6);
        int yy = by + (i / 2) * 22;
        if (x >= bx && x < bx + bw && y >= yy && y < yy + 20) { inaAction(inaTab, (uint8_t)i); needRedraw = true; return; }
      }
    }
    return;
  }
  if (!held) { inaRepRow = -1; return; }
  if (inaRepRow >= 0 && millis() - inaRepT0 > 450 && millis() - inaRepLast > 110) {
    inaRepLast = millis();
    inaAdjust(3, (uint8_t)inaRepRow, inaRepDir);
    needRedraw = true;
  }
}
