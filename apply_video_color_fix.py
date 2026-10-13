#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_video_color_fix.py
Fix warna thumbnail video & video yang di-zoom (warna-warni hancur).

Penyebab: thumbnail & zoom didecode JPEGDEC dgn RGB565_LITTLE_ENDIAN lalu
di-push sbg uint16_t*, padahal seluruh sisa firmware (pemutar normal, APOD,
EPIC, galeri) memakai pasangan yg sudah terbukti benar di perangkat:
RGB565_BIG_ENDIAN + (lgfx::swap565_t*).

Perbaikan:
  avi_renphone.ino
    - zoom : decode BIG_ENDIAN, bswap ke format native utk blend bilinear,
             bswap balik, push sbg swap565_t
    - thumb: decode BIG_ENDIAN, cache SD naik versi NTH1 -> NTH2
             (cache lama berisi warna salah, otomatis dibuat ulang)
  phone.ino
    - daftar video: pushImage thumbnail sbg swap565_t

Pakai: jalankan dari folder root repo (yg ada phone.ino)
    python apply_video_color_fix.py
Aman dijalankan berulang (idempotent).
"""
import sys, os

def patch(path, edits):
    if not os.path.exists(path):
        sys.exit("ERROR: %s tidak ketemu. Jalankan dari root repo ren_phone." % path)
    with open(path, "r", encoding="utf-8", newline="") as f:
        src = f.read()
    crlf = "\r\n" in src
    if crlf:
        src = src.replace("\r\n", "\n")
    changed = 0
    for name, old, new in edits:
        if new in src:
            print("  [skip] %s (sudah terpasang)" % name)
            continue
        n = src.count(old)
        if n != 1:
            sys.exit("ERROR di %s -> '%s': pola ditemukan %d kali (harus 1). "
                     "Kode kamu mungkin sudah berubah dari versi yg ku-cek." % (path, name, n))
        src = src.replace(old, new)
        changed += 1
        print("  [ok]   %s" % name)
    if changed:
        if crlf:
            src = src.replace("\n", "\r\n")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(src)
    return changed

print("== avi_renphone.ino ==")
patch("avi_renphone.ino", [
 ("zoom: callback simpan sbg native",
  "if (dx >= 0 && dx < (int)aviW) aviZoomSrc[(size_t)dy * aviW + dx] = src[yy * d->iWidth + xx];",
  "if (dx >= 0 && dx < (int)aviW) {\n"
  "        uint16_t pv = src[yy * d->iWidth + xx];                 // BIG_ENDIAN dari JPEGDEC\n"
  "        aviZoomSrc[(size_t)dy * aviW + dx] = (uint16_t)((pv >> 8) | (pv << 8));   // -> native utk blend\n"
  "      }"),
 ("zoom: decode BIG_ENDIAN",
  "aviJpeg->setPixelType(RGB565_LITTLE_ENDIAN);",
  "aviJpeg->setPixelType(RGB565_BIG_ENDIAN);"),
 ("zoom: output di-swap balik",
  "o[x] = aviBlend(t, b, wy);",
  "uint16_t px = aviBlend(t, b, wy);\n"
  "      o[x] = (uint16_t)((px >> 8) | (px << 8));                  // balik ke format swap565"),
 ("zoom: push sbg swap565_t",
  "display.pushImage(aviOffX, aviOffY, dw, dh, aviZoomOut);",
  "display.pushImage(aviOffX, aviOffY, dw, dh, (lgfx::swap565_t*)aviZoomOut);"),
 ("thumb: decode BIG_ENDIAN",
  "if (jd->openRAM(jbuf, (int)jlen, aviThDraw)) {\n",
  "if (jd->openRAM(jbuf, (int)jlen, aviThDraw)) {\n"
  "    jd->setPixelType(RGB565_BIG_ENDIAN);                 // format swap565, sama spt bagian lain\n"),
 ("thumb: cache baca NTH2",
  'memcmp(hd, "NTH1", 4) == 0',
  'memcmp(hd, "NTH2", 4) == 0'),
 ("thumb: cache tulis NTH2",
  "{ 'N', 'T', 'H', '1',",
  "{ 'N', 'T', 'H', '2',"),
])

print("== phone.ino ==")
patch("phone.ino", [
 ("daftar video: thumbnail swap565_t",
  "s.pushImage(tx, ty, MJ_TH_W, MJ_TH_H, mjpegThumbBuf + (size_t)idx * (MJ_TH_W*MJ_TH_H));",
  "s.pushImage(tx, ty, MJ_TH_W, MJ_TH_H, (lgfx::swap565_t*)(mjpegThumbBuf + (size_t)idx * (MJ_TH_W*MJ_TH_H)));"),
])

print("\nSelesai. Commit & push, lalu tunggu build GitHub Actions.")
