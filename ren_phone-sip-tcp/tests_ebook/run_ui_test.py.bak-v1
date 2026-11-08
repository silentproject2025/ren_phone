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
open(sd + '/books/rusak.epub', 'wb').write(b'PK' + os.urandom(3000))
open(sd + '/books/.tersembunyi.txt', 'w').write('x'); open(sd + '/books/catatan.pdf', 'w').write('x')
subprocess.check_call(['g++', '-std=gnu++17', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-I' + os.path.join(H, '..'), os.path.join(H, 'mock_run.cpp'), '-o', os.path.join(H, 'mock_run')])
sys.exit(subprocess.call([os.path.join(H, 'mock_run'), sd]))
