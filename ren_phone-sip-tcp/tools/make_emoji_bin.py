#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# make_emoji_bin.py -- bikin emoji.bin (set emoji utk NyxOS) dari PNG Noto Emoji ATAU Twemoji.
#
#   python3 tools/make_emoji_bin.py <folder-atau-zip> [emoji.bin]
#
# Sumber yg dikenali (nama berkas = kodepoint, HANYA emoji tunggal; urutan ZWJ/bendera/keycap dilewati):
#   Noto    : png/128/emoji_u1f600.png          (repo googlefonts/noto-emoji)
#   Twemoji : assets/72x72/1f600.png            (repo jdecked/twemoji)
# Folder boleh hasil git clone (dicari rekursif) atau berkas zip-nya.
# Butuh Pillow:  pkg install python-pillow   (Termux)   /   pip install pillow
#
# Format emoji.bin (little-endian) -- dibaca emoji_renphone.ino:
#   "NYXE", u16 versi=1, u16 jumlah N, u8 ukuran[3]={10,16,24}, 5 byte nol
#   N x u32 kodepoint
#   blok piksel per ukuran: N*n*n u16 RGB565, 0x0000 = transparan
import sys, os, re, io, struct, zipfile
from PIL import Image

SIZES = (10, 16, 24)
MAX_N = 4000
MIN_CP = 0x231A      # di bawah ini (huruf, angka, (c), (R), tanda panah...) bukan emoji: jangan sentuh teks biasa
# urutan awal (muncul di halaman pertama keyboard): wajah populer, hati, simbol umum
FIRST = [0x1F600,0x1F601,0x1F602,0x1F60A,0x1F642,0x1F609,0x1F60D,0x1F618,0x1F60B,0x1F60E,0x1F914,0x1F610,0x1F634,0x1F622,0x1F62D,
         0x1F621,0x1F631,0x1F97A,0x1F607,0x1F644,0x1F605,0x1F60F,0x1F62C,0x1F929,0x1F606,0x1F61C,0x1F923,0x1F970,0x1F603,0x1F604,
         0x2764,0x1F9E1,0x1F49B,0x1F49A,0x1F499,0x1F49C,0x1F5A4,0x1F90D,0x1F494,0x1F44D,0x1F44E,0x1F44F,0x1F64F,0x1F4AA,0x1F44B,
         0x1F525,0x2B50,0x2728,0x1F319,0x2600,0x2705,0x274C,0x26A1,0x1F480,0x1F440,0x1F389,0x2615,0x1F382,0x1F4AF,0x1F916,0x1F3AE,0x1F4F1]
SKIP = set(range(0x1F3FB, 0x1F400)) | set(range(0x1F1E6, 0x1F200)) | {0xFE0F, 0xFE0E, 0x200D, 0x20E3}
PAT = [re.compile(r'(?:^|[/\\])emoji_u([0-9a-f]+)\.png$', re.I), re.compile(r'(?:^|[/\\])([0-9a-f]+)\.png$', re.I)]

def prio(path):                      # ukuran sumber terbaik: 128 > 72 > 512 > 32 > lain
    for i, k in enumerate(('/128/', '/72x72/', '/72/', '/512/', '/32/')):
        if k in path.replace('\\', '/'): return i
    return 9

def collect(src):
    found = {}
    def consider(path, opener):
        for p in PAT:
            m = p.search(path)
            if m:
                cp = int(m.group(1), 16)
                if cp < MIN_CP or cp in SKIP or cp > 0x1FFFF: return
                pr = prio(path)
                if cp not in found or pr < found[cp][0]: found[cp] = (pr, opener)
                return
    if os.path.isdir(src):
        for root, _, files in os.walk(src):
            for fn in files:
                full = os.path.join(root, fn)
                consider(full, lambda full=full: open(full, 'rb').read())
    else:
        z = zipfile.ZipFile(src)
        for n in z.namelist():
            consider(n, lambda n=n: z.read(n))
    return found

def to565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
def sprite(im, n):
    w, h = im.size
    if w != h:                                       # Noto ada yg tidak persegi: pad ke tengah
        side = max(w, h); sq = Image.new('RGBA', (side, side), (0, 0, 0, 0)); sq.paste(im, ((side - w) // 2, (side - h) // 2)); im = sq
    small = im.convert('RGBa').resize((n, n), Image.LANCZOS).convert('RGBA')
    raw = small.tobytes(); px = [tuple(raw[i:i+4]) for i in range(0, len(raw), 4)]; out = []
    for r, g, b, a in px:
        if a < 110: out.append(0)
        else:
            v = to565(r, g, b); out.append(v if v else 0x0821)    # hitam murni dipakai utk transparan -> geser sedikit
    return out

def main():
    if len(sys.argv) < 2: print(__doc__); return 1
    src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else 'emoji.bin'
    if not os.path.exists(src): print('ERROR: tidak ada:', src); return 1
    found = collect(src)
    if not found: print('ERROR: tidak ada PNG emoji tunggal yg dikenali di', src); return 1
    order = [cp for cp in FIRST if cp in found] + sorted(cp for cp in found if cp not in set(FIRST))
    if len(order) > MAX_N: print('PERINGATAN: %d emoji, dipotong ke %d' % (len(order), MAX_N)); order = order[:MAX_N]
    n = len(order)
    blocks = [[] for _ in SIZES]
    for i, cp in enumerate(order):
        im = Image.open(io.BytesIO(found[cp][1]())).convert('RGBA')
        for k, sz in enumerate(SIZES): blocks[k].append(sprite(im, sz))
        if i % 200 == 0: print('  %d/%d' % (i, n), end='\r', flush=True)
    with open(dst, 'wb') as f:
        f.write(b'NYXE' + struct.pack('<HH', 1, n) + bytes(SIZES) + bytes(5))
        f.write(struct.pack('<%dI' % n, *order))
        for k in range(3):
            for px in blocks[k]: f.write(struct.pack('<%dH' % len(px), *px))
    print('OK: %d emoji -> %s (%.2f MB)' % (n, dst, os.path.getsize(dst) / 1048576))
    print('Taruh di ROOT kartu SD sbg /emoji.bin lalu restart HP.')
    return 0
if __name__ == '__main__': sys.exit(main())
