#!/usr/bin/env python3
# run_ui_test.py -- siapkan "SD card" palsu lalu jalankan mock_run (uji fungsional antarmuka)
import os, subprocess, sys, tempfile, textwrap, zipfile
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H)
sd = tempfile.mkdtemp(); os.makedirs(sd + '/books')
# EPUB 2 dengan NCX, 6 bab
z = zipfile.ZipFile(sd + '/books/uji.epub', 'w'); z.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OEBPS/c.opf"/></rootfiles></container>')
items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(6)) + '<item id="ncx" href="t.ncx" media-type="application/x-dtbncx+xml"/>'
z.writestr('OEBPS/c.opf', f'<package><metadata><dc:title>Buku Uji</dc:title></metadata><manifest>{items}</manifest><spine toc="ncx">' + ''.join(f'<itemref idref="c{i}"/>' for i in range(6)) + '</spine></package>')
z.writestr('OEBPS/t.ncx', '<ncx><navMap>' + ''.join(f'<navPoint><navLabel><text>Judul NCX {i+1}</text></navLabel><content src="c{i}.xhtml"/></navPoint>' for i in range(6)) + '</navMap></ncx>')
for i in range(6):
    z.writestr(f'OEBPS/c{i}.xhtml', f'<html><body><h1>Bab {i+1}</h1>' + ''.join(f'<p>Paragraf {j} bab {i+1}: kata kata panjang untuk mengisi halaman demi halaman supaya paginasi punya banyak halaman uji.</p>' for j in range(30)) + '</body></html>', zipfile.ZIP_DEFLATED)
z.close()
para = ['Alice was beginning to get very tired of sitting by her sister on the bank, and of having nothing to do: once or twice she had peeped into the book her sister was reading.'] * 12
open(sd + '/books/hardwrap.txt', 'w').write('CHAPTER I. Down the Rabbit-Hole\n\n' + '\n\n'.join(textwrap.fill(p, 70) for p in para) + '\n\n\nCHAPTER II\n\n' + '\n\n'.join(textwrap.fill(p, 70) for p in para) + '\n')
open(sd + '/rootfile.txt', 'w').write('Buku di root SD.\n' * 50)       # akan jadi buku ke-3 (urut alfabet: hardwrap, rootfile, uji? -> lihat uji)
import struct, zlib
def png(w, h):
    raw = b''.join(b'\x00' + bytes((x*7+y*3) & 255 for x in range(w)) for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 0)) + ch(b'IEND', b'')
COVER = b'\xff\xd8JX' + struct.pack('<HH', 200, 300) + b'\0' * 4000        # "JPEG" uji untuk mock JPEGDEC
# EPUB 3 dengan metadata UTF-8, sampul (properti cover-image) dan gambar dalam bab
z = zipfile.ZipFile(sd + '/books/kisah.epub', 'w'); z.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OEBPS/c.opf"/></rootfiles></container>')
z.writestr('OEBPS/c.opf', '<package><metadata xmlns:dc="x"><dc:title>Kisah \u00c9vora</dc:title><dc:creator>Jos\u00e9 \u00c1lvarez</dc:creator></metadata><manifest>'
  '<item id="c0" href="c0.xhtml" media-type="application/xhtml+xml"/><item id="cv" href="img/cover.jpg" media-type="image/jpeg" properties="cover-image"/><item id="p" href="img/p.png" media-type="image/png"/></manifest>'
  '<spine><itemref idref="c0"/></spine></package>')
z.writestr('OEBPS/img/cover.jpg', COVER, zipfile.ZIP_STORED); z.writestr('OEBPS/img/p.png', png(150, 120), zipfile.ZIP_STORED)
z.writestr('OEBPS/c0.xhtml', '<html><body><h1>Pembuka</h1>' + ''.join('<p>Paragraf %d: teks panjang untuk mengisi halaman supaya justify punya banyak kata untuk dibagi rata di setiap baris.</p>' % j for j in range(40)) + '<p><img src="img/p.png"/></p><p>Sesudah gambar masih ada teks.</p></body></html>')
z.close()
open(sd + '/books/unicode.txt', 'w', encoding='utf-8').write('Caf\u00e9 na\u00efve \u2014 \u0416\u0438\u0432\u043e\u0439 \u65e5\u672c ' * 4 + '\n\n' + 'Teks biasa panjang sekali untuk mengisi halaman. ' * 40 + '\n')
open(sd + '/books/catatan.md', 'w', encoding='utf-8').write('# Catatan Harian\n\nIni **tebal** dan *miring* dengan [tautan](http://x.y).\n\n## Bagian Dua\n\n- satu\n- dua\n\n' + 'Paragraf panjang. ' * 60 + '\n')
os.makedirs(sd + '/books/fonts')
subprocess.check_call([sys.executable, os.path.join(H, '..', 'tools', 'make_vlw.py'), sd + '/books/fonts'], stdout=subprocess.DEVNULL)
open(sd + '/books/rusak.epub', 'wb').write(b'PK' + os.urandom(3000))
open(sd + '/books/.tersembunyi.txt', 'w').write('x'); open(sd + '/books/catatan.pdf', 'w').write('x')
subprocess.check_call(['g++', '-std=gnu++17', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-I' + os.path.join(H, '..'), os.path.join(H, 'mock_run.cpp'), '-o', os.path.join(H, 'mock_run')])
sys.exit(subprocess.call([os.path.join(H, 'mock_run'), sd]))
