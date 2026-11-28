#!/usr/bin/env python3
"""AETHER_NO_ADC_V1: nonaktifkan ADC baterai (GPIO1, pembagi 10k/10k) di Aether Board.
Cara: RP_BATT_ADC_PIN tidak lagi otomatis di-set 1 untuk -DRP_BOARD_AETHER -> DP_D_BATT_ADC = -1 (build tanpa ADC),
sehingga battAdcUpdate()/analogRead baterai tidak pernah jalan (cek rpPinV[DP_BATT_ADC] >= 0 di battUpdate()).
Hidupkan lagi: build dgn -DRP_AETHER_BATT_ADC (pakai GPIO1), atau -DRP_BATT_ADC_PIN=<pin>, atau atur pin dari Opsi Developer.
Build non-Aether TIDAK berubah. Bus sensor tetap SDA=21 / SCL=2 (kabel tidak perlu diubah).
Pakai dari root repo: python3 tools/patches/apply_aether_no_adc_patch.py [--check]   (idempoten)"""
import sys, pathlib
CHECK = "--check" in sys.argv
F = pathlib.Path("firmware/phone/display_conf.h")
t = F.read_text(encoding="utf-8")
OLD = ("  #ifndef RP_BATT_ADC_PIN\n"
       "  #define RP_BATT_ADC_PIN     1\n"
       "  #endif\n")
NEW = ("  // AETHER_NO_ADC_V1: ADC baterai MATI secara bawaan di Aether (DP_D_BATT_ADC = -1). Baterai hanya dibaca dari INA219.\n"
       "  // Hidupkan lagi: -DRP_AETHER_BATT_ADC (GPIO1, pembagi 10k/10k), atau -DRP_BATT_ADC_PIN=<pin>, atau atur pin dari Opsi Developer.\n"
       "  #if defined(RP_AETHER_BATT_ADC) && !defined(RP_BATT_ADC_PIN)\n"
       "  #define RP_BATT_ADC_PIN     1\n"
       "  #endif\n")
bad = 0
if "AETHER_NO_ADC_V1" in t:
    print("sudah   display_conf.h")
elif t.count(OLD) != 1:
    print(f"GAGAL   display_conf.h: blok RP_BATT_ADC_PIN ditemukan {t.count(OLD)}x (harus 1x)"); bad = 1
else:
    if not CHECK: F.write_text(t.replace(OLD, NEW, 1), encoding="utf-8")
    print(("cocok   " if CHECK else "dipasang") + " display_conf.h: ADC baterai Aether dimatikan")
# catatan di dokumentasi (opsional, tidak menggagalkan)
D = pathlib.Path("docs/hardware.md")
if D.exists():
    d = D.read_text(encoding="utf-8"); k = "| Battery voltage (ADC) | GPIO 1 (`RP_BATT_ADC_PIN`"
    if "AETHER_NO_ADC_V1" in d: print("sudah   docs/hardware.md")
    elif d.count(k) == 1:
        if not CHECK: D.write_text(d.replace(k, "| Battery voltage (ADC) | **Off by default on the Aether Board** (AETHER_NO_ADC_V1; enable with `-DRP_AETHER_BATT_ADC`). GPIO 1 (`RP_BATT_ADC_PIN`", 1), encoding="utf-8")
        print(("cocok   " if CHECK else "dipasang") + " docs/hardware.md")
    else: print("lewati  docs/hardware.md (baris tidak ditemukan 1x)")
print("AETHER_NO_ADC_V1:", "ADA YANG GAGAL" if bad else ("cek OK" if CHECK else "selesai"))
sys.exit(bad)
