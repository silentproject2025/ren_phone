#!/usr/bin/env python3
# apply_hws_flatbg.py -- HWS_FLATBG_V1
# Jalankan dari root repo:  python tools/patches/apply_hws_flatbg.py
#
# Masalah: layar scroll biasa (Pengaturan, Berkas, Chat, Buku, dst. = "mode generik" hardware scroll)
# tidak pernah memakai hardware scroll, karena mesin generik mencocokkan baris layar yang hanya PINDAH posisi.
# Di layar-layar itu isi baris tidak sama saat pindah posisi:
#   - iosBackdrop() menggambar 2 glow radial di posisi layar yang TETAP (tidak ikut scroll),
#   - drawGlassCard() mengambil warna rata-rata backdrop di bawah kartu -> warna kartu berubah menurut posisi.
# Perbaikan: selama hardware scroll generik berlaku (portrait, mode Scroll Hardware != Mati, layar yang
# didukung), backdrop dibuat POLOS (T().bg) dan kartu kaca memakai warna dasar tetap.
# Efek visual: pendar glow halus di latar layar-layar itu hilang. Mau glow lagi: Scroll Hardware = Mati.
import sys, os
MARK = "HWS_FLATBG_V1"
D = "firmware/phone"
def load(n): return open(os.path.join(D, n), encoding="utf-8").read()
def save(n, s): open(os.path.join(D, n), "w", encoding="utf-8").write(s)
def rep(s, old, new, name):
    if s.count(old) != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus 1x)" % (name, s.count(old)))
    return s.replace(old, new)

ph = load("phone.ino")
hw = load("hwscroll_renphone.ino")
if MARK in ph and MARK in hw:
    print("sudah terpasang"); sys.exit(0)

ph = rep(ph, "int hwsModeGet(); bool hwsModeSet(int); const char* hwsModeText(int);",
             "int hwsModeGet(); bool hwsModeSet(int); const char* hwsModeText(int); bool hwsFlatBgNow();   // " + MARK, "decl")
ph = rep(ph, "void iosBackdrop(LGFX_Sprite& s){\n  s.fillSprite(T().bg);\n",
             "void iosBackdrop(LGFX_Sprite& s){\n  s.fillSprite(T().bg);\n  if(hwsFlatBgNow()) return;   // " + MARK + ": glow tetap-di-layar merusak pencocokan baris hardware scroll\n", "backdrop")
ph = rep(ph, "  uint16_t base=glassSampleAvg(s,x,y,w,h,4);\n",
             "  uint16_t base=hwsFlatBgNow()?T().bg:glassSampleAvg(s,x,y,w,h,4);   // " + MARK + "\n", "glass")

hw = rep(hw, "static bool hwsGenAlloc(){\n",
             "// " + MARK + ": true = layar generik yang boleh hardware scroll (portrait, mode != Mati) -> backdrop polos.\n"
             "// Sengaja TIDAK memeriksa overlay (CC/kunci/dll) supaya latar tidak berganti-ganti saat overlay muncul.\n"
             "bool hwsFlatBgNow(){\n  return hwsModeGet() != 0 && currentOrient == ORIENT_PORTRAIT && hwsGenScreenOk();\n}\n"
             "static bool hwsGenAlloc(){\n", "fn")
hw = rep(hw, "bool hwsIsActive(){ return false; }\n",
             "bool hwsIsActive(){ return false; }\nbool hwsFlatBgNow(){ return false; }   // " + MARK + "\n", "stub")
save("phone.ino", ph); save("hwscroll_renphone.ino", hw)
print("ok: phone.ino + hwscroll_renphone.ino (" + MARK + ")")
