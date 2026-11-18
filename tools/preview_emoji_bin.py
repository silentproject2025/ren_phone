#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview_emoji_bin.py -- gambar isi emoji.bin jadi satu PNG (tanpa HP) untuk memeriksa apakah FILE-nya sendiri benar.

  python preview_emoji_bin.py emoji.bin [hasil.png] [mulai] [jumlah]
  contoh: python preview_emoji_bin.py ~/storage/downloads/emoji.bin cek.png 0 60

Hasil: kisi ukuran 24 px (diperbesar 4x) + baris 10 px & 16 px untuk emoji yang sama. Kirim cek.png ke Claude.
Butuh Pillow (Termux: pkg install python-pillow).
"""
import sys, struct
from PIL import Image
if len(sys.argv) < 2: print(__doc__); sys.exit(1)
src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else 'cek.png'
start = int(sys.argv[3]) if len(sys.argv) > 3 else 0
cnt = int(sys.argv[4]) if len(sys.argv) > 4 else 60
d = open(src, 'rb').read()
if d[:4] != b'NYXE': sys.exit('bukan emoji.bin (header salah)')
ver, n = struct.unpack('<HH', d[4:8]); sizes = tuple(d[8:11])
print('versi', ver, '| jumlah emoji', n, '| ukuran', sizes, '| ukuran file', len(d), 'byte')
exp = 16 + n * 4 + sum(n * s * s * 2 for s in sizes)
print('ukuran seharusnya', exp, 'byte ->', 'COCOK' if len(d) == exp else 'TIDAK COCOK (file terpotong/rusak!)')
cps = struct.unpack('<%dI' % n, d[16:16 + n * 4])
off = [16 + n * 4]
for s in sizes[:2]: off.append(off[-1] + n * s * s * 2)
def spr(bi, i):
    s = sizes[bi]; p = off[bi] + i * s * s * 2
    v = struct.unpack('<%dH' % (s * s), d[p:p + s * s * 2]); im = Image.new('RGBA', (s, s), (40, 40, 60, 255))
    for k, c in enumerate(v):
        if c: im.putpixel((k % s, k // s), (((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3, 255))
    return im
end = min(n, start + cnt); cols = 10; Z = 4
rows = (end - start + cols - 1) // cols
cell = 24 * Z + 6
W = cols * cell; H = rows * (cell + 16 * 2 + 6) + 4
sheet = Image.new('RGB', (W, H), (20, 20, 30))
for j, i in enumerate(range(start, end)):
    x = (j % cols) * cell; y = (j // cols) * (cell + 16 * 2 + 6)
    sheet.paste(spr(2, i).resize((24 * Z, 24 * Z), Image.NEAREST).convert('RGB'), (x, y))
    sheet.paste(spr(1, i).convert('RGB'), (x, y + 24 * Z + 2)); sheet.paste(spr(0, i).convert('RGB'), (x + 20, y + 24 * Z + 2))
sheet.save(dst); print('tersimpan:', dst, '(emoji %d..%d, U+%X..)' % (start, end - 1, cps[start]))
