#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_hw_scroll_v3.py -- HW_SCROLL_V3
 1. Bawaan mode hardware scroll jadi "Otomatis" (RP_HWS_DEFAULT_MODE 0 -> 1).
    Catatan: kalau di NVS sudah tersimpan pilihan (lewat Pengaturan > KINERJA), pilihan itu tetap dipakai.
 2. Galeri (album + grid) masuk daftar mode generik. Viewer foto TIDAK ikut (menulis display langsung).
 3. Semua jalur Galeri yang menulis display langsung (transisi, geser foto, zoom, tarik-tutup) memanggil
    hwsLeave() dulu supaya ring buffer panel tidak berantakan.
Syarat: apply_hw_scroll.py + apply_hw_scroll_v2.py sudah terpasang. Jalankan dari root repo. Idempotent.
"""
import sys, os

def load(p):
    if not os.path.exists(p): sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo." % p)
    s = open(p, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    return s.replace("\r\n", "\n"), crlf

def save(p, s, crlf):
    if crlf: s = s.replace("\n", "\r\n")
    open(p, "w", encoding="utf-8", newline="").write(s)

def rep(s, old, new, tag, must=1):
    n = s.count(old)
    if n != must: sys.exit("ERROR [%s]: anchor ketemu %d kali (harus %d). Dibatalkan, tidak ada file diubah." % (tag, n, must))
    return s.replace(old, new)

H, hc = load("hwscroll_renphone.ino")
G, gc = load("gallery_renphone.ino")
if "HWS_GAL_V1" in G and "HWS_GAL_V1" in H and "#define RP_HWS_DEFAULT_MODE 1" in H:
    print("[skip] V3 sudah terpasang"); sys.exit(0)
if "HW_SCROLL_V1" not in H or "hwsGenScreenOk" not in H:
    sys.exit("ERROR: V1/V2 belum terpasang (hwscroll_renphone.ino tanpa mode generik).")

# ---- hwscroll_renphone.ino ----
if "#define RP_HWS_DEFAULT_MODE 1" not in H:
    H = rep(H, "#define RP_HWS_DEFAULT_MODE 0   // 0=Mati bawaan (belum teruji di hardware); ubah ke 1 kalau sudah yakin",
               "#define RP_HWS_DEFAULT_MODE 1   // 1=Otomatis bawaan (HWS_DEFAULT_ON_V3); 0=Mati; 2=Otomatis+Balik. Pilihan di NVS tetap menang", "default")
if "HWS_GAL_V1" not in H:
    H = rep(H, "    case SCR_CHAT: case SCR_EBOOK: case SCR_BACKUP: case SCR_ABOUT:\n      return true;\n",
               "    case SCR_CHAT: case SCR_EBOOK: case SCR_BACKUP: case SCR_ABOUT:\n      return true;\n"
               "    case SCR_GALLERY:   // HWS_GAL_V1: hanya album + grid; viewer foto menulis display langsung\n      return galHwScrollOk();\n", "whitelist")

# ---- gallery_renphone.ino ----
if "HWS_GAL_V1" not in G:
    G = rep(G, "static void galRunTransition(int kind);       // definisi di bawah (butuh galDrawContent)\n",
               "static void galRunTransition(int kind);       // definisi di bawah (butuh galDrawContent)\n\n"
               "// HWS_GAL_V1: dipakai hwscroll_renphone.ino -- mode scroll hardware generik hanya utk album + grid\n"
               "bool galHwScrollOk() { return galMode == GM_ALBUMS || galMode == GM_GRID; }\n", "helper")
    G = rep(G, "static void galRunTransition(int kind) {\n  if (!galSnapReady) return;\n",
               "static void galRunTransition(int kind) {\n  hwsLeave(\"galeri transisi\", 1);   // HWS_GAL_V1\n  if (!galSnapReady) return;\n", "transisi")
    G = rep(G, "static void galSwipeAnimate(float from, float to, int dir) {\n",
               "static void galSwipeAnimate(float from, float to, int dir) {\n  hwsLeave(\"galeri geser foto\", 1);   // HWS_GAL_V1\n", "swipe")
    G = rep(G, "static void galZoomApply(float zTo, float pxTo, float pyTo) {\n",
               "static void galZoomApply(float zTo, float pxTo, float pyTo) {\n  hwsLeave(\"galeri zoom\", 1);   // HWS_GAL_V1\n", "zoom")
    G = rep(G, "      const unsigned long t0 = millis(), dur = 160;      // balik ke posisi semula\n      display.startWrite();\n",
               "      const unsigned long t0 = millis(), dur = 160;      // balik ke posisi semula\n      hwsLeave(\"galeri tarik-tutup\", 1);   // HWS_GAL_V1\n      display.startWrite();\n", "dragback")

save("hwscroll_renphone.ino", H, hc)
save("gallery_renphone.ino", G, gc)
print("OK: HW_SCROLL_V3 terpasang (bawaan Otomatis + Galeri album/grid + 4 hook hwsLeave)")
