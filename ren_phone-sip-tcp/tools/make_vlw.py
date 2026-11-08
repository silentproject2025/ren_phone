#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# make_vlw.py -- buat font .vlw (format smooth-font Processing/TFT_eSPI/LovyanGFX) untuk app Buku.
#
# Cakupan glyph = EXACTLY ebUniKeep() di ebook_core.h: Latin-1 Supplement, Latin Extended-A, Yunani modern, Kiril dasar,
# dan tanda baca umum (kutip/strip melengkung, elipsis, euro, ...), ditambah ASCII. Kalau ebUniKeep diubah,
# ubah juga UNI_RANGES / UNI_EXTRA di sini (dan sebaliknya), lalu buat ulang font.
#
# Pemakaian:   python3 tools/make_vlw.py [folder_keluaran]        (bawaan: ./fonts_sd)
# Butuh Pillow dan font DejaVu (paket fonts-dejavu-core / fonts-dejavu-extra). Hasilnya disalin ke
# SD card di folder /books/fonts/ :
#   nyxbook-sans15.vlw  nyxbook-serif15.vlw  nyxbook-mono15.vlw  nyxbook-sans19.vlw  nyxbook-serif19.vlw
import os, struct, sys
from PIL import Image, ImageDraw, ImageFont

UNI_RANGES = [(0x20, 0x7E), (0xA1, 0xAC), (0xAE, 0x17F),                      # ASCII, Latin-1 Supplement, Latin Extended-A
              (0x384, 0x38A), (0x38C, 0x38C), (0x38E, 0x3A1), (0x3A3, 0x3CE),        # Yunani modern
              (0x400, 0x45F), (0x490, 0x491)]                                         # Kiril dasar + Ґ ґ
UNI_EXTRA = [0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2015, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
             0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x20AC, 0x2122, 0x2190, 0x2192, 0x2212]
SEARCH = ['/usr/share/fonts/truetype/dejavu', '/usr/share/fonts/dejavu', '/usr/share/fonts/TTF', '/Library/Fonts', 'C:/Windows/Fonts', '.']
FAMILIES = {
    'sans':  ['DejaVuSans.ttf'],
    'serif': ['DejaVuSerif.ttf', 'DejaVuSans.ttf'],
    'mono':  ['DejaVuSansMono.ttf', 'DejaVuSans.ttf'],
}
JOBS = [('sans', 15), ('serif', 15), ('mono', 15), ('sans', 19), ('serif', 19)]

def codepoints():
    cps = []
    for a, b in UNI_RANGES: cps.extend(range(a, b + 1))
    cps.extend(UNI_EXTRA)
    return sorted(set(cps))

def find(name):
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.exists(p): return p
    return None

def has_glyph(path, cp, _cache={}):
    from fontTools.ttLib import TTFont
    if path not in _cache: _cache[path] = TTFont(path, lazy=True).getBestCmap()
    return cp in _cache[path]

def build(family, px):
    files = [find(n) for n in FAMILIES[family]]
    files = [f for f in files if f]
    if not files: raise SystemExit('font %s tidak ditemukan (pasang fonts-dejavu)' % FAMILIES[family][0])
    fonts = [(f, ImageFont.truetype(f, px)) for f in files]
    glyphs = []; asc = 0; desc = 0
    for cp in codepoints():
        pick = None
        for path, fnt in fonts:
            try:
                if has_glyph(path, cp): pick = fnt; break
            except Exception:
                pick = fnt; break
        if pick is None: raise SystemExit('U+%04X tidak ada di %s' % (cp, family))
        ch = chr(cp)
        adv = int(round(pick.getlength(ch)))
        if cp == 0x20 or adv <= 0 and cp in (0x20,):
            glyphs.append((cp, 0, 0, max(adv, 1), 0, 0, b'')); continue
        l, t, r, b = pick.getbbox(ch, anchor='ls')                            # relatif titik awal di garis dasar
        w, h = max(r - l, 0), max(b - t, 0)
        if w == 0 or h == 0:
            glyphs.append((cp, 0, 0, max(adv, 1), 0, 0, b'')); continue
        img = Image.new('L', (w, h), 0)
        ImageDraw.Draw(img).text((-l, -t), ch, font=pick, fill=255, anchor='ls')
        top = -t                                                              # jarak dari garis dasar ke tepi atas (positif = di atas)
        glyphs.append((cp, h, w, max(adv, 1), top, l, img.tobytes()))
        asc = max(asc, top); desc = max(desc, h - top)
    return glyphs, asc, desc

def write_vlw(path, glyphs, asc, desc, px):
    out = bytearray()
    out += struct.pack('>6i', len(glyphs), 11, asc + desc, 0, asc, desc)       # jumlah, versi, tinggi baris, (tak dipakai), ascent, descent
    for cp, h, w, adv, top, left, _ in glyphs:
        out += struct.pack('>7i', cp, h, w, adv, top, left, 0)                 # unicode, tinggi, lebar, majuKursor, atas, kiri, 0
    for g in glyphs: out += g[6]
    for s in ('', ''):                                                         # nama font (kosong) -- format Processing
        out += struct.pack('>H', len(s)) + s.encode()
    out += b'\x01'
    open(path, 'wb').write(out)

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else 'fonts_sd'
    os.makedirs(outdir, exist_ok=True)
    for fam, px in JOBS:
        glyphs, asc, desc = build(fam, px)
        p = os.path.join(outdir, 'nyxbook-%s%d.vlw' % (fam, px))
        write_vlw(p, glyphs, asc, desc, px)
        print('%s: %d glyph, ascent %d descent %d, %d KB' % (os.path.basename(p), len(glyphs), asc, desc, os.path.getsize(p) // 1024))

if __name__ == '__main__':
    main()
