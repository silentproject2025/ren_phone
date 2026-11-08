#!/usr/bin/env python3
# test_ebook_v2.py -- uji fitur v2 ebook_core.h: Unicode (UTF-8 + lipat), gambar EPUB, sampul/metadata, Markdown.
# Jalankan: python3 test_ebook_v2.py   (membangun ulang test_core)
import os, struct, subprocess, sys, tempfile, zipfile, zlib
H = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(H, 'test_core')
subprocess.check_call(['g++','-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined',
    '-I'+os.path.join(H,'..'), os.path.join(H,'test_core.cpp'), '-o', BIN])
def run(*a):
    p = subprocess.run([BIN]+list(map(str,a)), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr
fails = 0
def check(ok, msg):
    global fails
    print(('  ok   ' if ok else '  GAGAL ')+msg)
    if not ok: fails += 1
tmp = tempfile.mkdtemp()

def png(w, h, extra=0):                       # PNG sah minimal (abu-abu), untuk uji ukuran/ekstraksi
    raw = b''.join(b'\x00' + bytes((x*7+y*3) & 255 for x in range(w)) for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 0)) + ch(b'IEND', b'') + b'\0'*extra
BIG = png(120, 100)         # > 3 KB
TINY = png(8, 8)            # < 3 KB (hiasan)
print('[unicode]')
txt = 'Caf\u00e9 na\u00efve \u2014 \u201ckutip\u201d \u0416\u0438\u0432\u043e\u0439 \u0391\u03b8\u03ae\u03bd\u03b1 \u65e5\u672c \u20ac5 fi\ufb01\n'
p = os.path.join(tmp, 'u.txt'); open(p, 'w', encoding='utf-8').write(txt)
for keep, exp in [(0, 'Cafe naive - "kutip" ? ? ? EUR5 fifi\n'),
                  (1, 'Caf\u00e9 na\u00efve \u2014 \u201ckutip\u201d \u0416\u0438\u0432\u043e\u0439 \u0391\u03b8\u03ae\u03bd\u03b1 ? \u20ac5 fifi\n')]:
    od = os.path.join(tmp, 'o%d' % keep); os.makedirs(od, exist_ok=True)
    rc, out, err = run('book', 'txt', p, od, keep)
    got = open(os.path.join(od, '000.txt'), encoding='utf-8', errors='replace').read()
    check(rc == 0 and got == exp, 'TXT keepUni=%d -> %r' % (keep, got))
    if keep == 1: check('\u65e5' not in got, 'CJK tetap "?" (di luar cakupan font)')
# fold
rc, out, _ = run('fold', 'Caf\u00e9 \u0416 \u2026 \u65e5')
check(out.strip() == 'Cafe ? ... ?', 'ebFoldAscii: %r' % out.strip())
# tata letak UTF-8
big = ' '.join(['Pagi di kota \u00c9vora, \u0416\u0438\u0432\u043e\u0439 \u0391\u03b8\u03ae\u03bd\u03b1 \u2014 na\u00efve caf\u00e9 \u201ckutip\u201d.'] * 60) + '\n'
p = os.path.join(tmp, 'big_u.txt'); open(p, 'w', encoding='utf-8').write('\u0001Judul \u00c9tude\n' + big + big)
od = os.path.join(tmp, 'ol'); os.makedirs(od, exist_ok=True)
rc, out, err = run('book', 'txt', p, od, 1)
lay = os.path.join(od, '000.txt')
for mode, label in [(0, 'font ASCII (lipat)'), (1, 'font Unicode'), (2, 'Unicode tanpa glyph Yunani')]:
    for width, rows in [(228, 18), (120, 5), (40, 3), (22, 2)]:
        rc, out, err = run('uilayout', lay, width, rows, mode)
        check(rc == 0 and 'bad=0' in out, 'tata letak UTF-8 %s lebar=%d: %s%s' % (label, width, out.strip().splitlines()[-1] if out.strip() else '', err[:120]))
# byte rusak / terpotong di teks kanonik tidak boleh membuat crash atau lebar ngawur
bad = b'\x01Judul\n' + b'abc \xc3 \xe2\x82 def \xff\xfe ghi \xf0\x9f\n' * 40
open(os.path.join(tmp, 'bad.txt'), 'wb').write(bad)
rc, out, err = run('uilayout', os.path.join(tmp, 'bad.txt'), 100, 6, 1)
check(rc == 0 and 'ERROR' not in err and 'runtime error' not in err, 'UTF-8 rusak/terpotong aman: ' + out.strip())

print('[epub: metadata, sampul, gambar]')
def mk(path, cover_mode='prop', with_svg=True, ncx=True):
    z = zipfile.ZipFile(path, 'w'); z.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="OEBPS/c.opf"/></rootfiles></container>')
    items = '<item id="c0" href="t/c0.xhtml" media-type="application/xhtml+xml"/><item id="c1" href="t/c1.xhtml" media-type="application/xhtml+xml"/>'
    items += '<item id="cv" href="img/cover.png" media-type="image/png"%s/>' % (' properties="cover-image"' if cover_mode == 'prop' else '')
    items += '<item id="pic" href="img/pic one.png" media-type="image/png"/><item id="orn" href="img/orn.png" media-type="image/png"/>'
    meta = '<dc:title>Kisah \u00c9vora &amp; Kawan</dc:title><dc:creator opf:role="aut">Jos\u00e9 \u00c1lvarez</dc:creator><dc:creator>Kedua</dc:creator>'
    if cover_mode == 'meta': meta += '<meta name="cover" content="cv"/>'
    z.writestr('OEBPS/c.opf', '<package xmlns:opf="http://www.idpf.org/2007/opf"><metadata xmlns:dc="x">%s</metadata><manifest>%s</manifest><spine><itemref idref="c0"/><itemref idref="c1"/></spine></package>' % (meta, items), zipfile.ZIP_DEFLATED)
    z.writestr('OEBPS/img/cover.png', BIG, zipfile.ZIP_STORED); z.writestr('OEBPS/img/pic one.png', BIG, zipfile.ZIP_DEFLATED); z.writestr('OEBPS/img/orn.png', TINY)
    c0 = '<html><body><p>Sebelum gambar.</p><p><img src="../img/pic%20one.png" alt="x"/></p><p>Sesudah gambar.</p><p><img src="../img/orn.png"/>hiasan kecil</p><p><img src="http://x/y.png"/><img src="../img/hilang.png"/><img src="../img/pic%20one.png#f"/></p></body></html>'
    c1 = '<html><body><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"><image xlink:href="../img/cover.png" width="10" height="10"/></svg></body></html>' if with_svg else '<html><body><p>x</p></body></html>'
    z.writestr('OEBPS/t/c0.xhtml', c0, zipfile.ZIP_DEFLATED); z.writestr('OEBPS/t/c1.xhtml', c1, zipfile.ZIP_DEFLATED); z.close()
for mode in ('prop', 'meta', 'none'):
    p = os.path.join(tmp, 'm_%s.epub' % mode); mk(p, mode); od = os.path.join(tmp, 'om_%s' % mode); os.makedirs(od, exist_ok=True)
    rc, out, err = run('book', 'epub', p, od, 1)
    check(rc == 0 and 'TITLE\tKisah \u00c9vora & Kawan' in out, 'judul UTF-8 (%s)' % mode)
    check('AUTHOR\tJos\u00e9 \u00c1lvarez\n' in out, 'pengarang pertama (%s)' % mode)
    if mode == 'none': check('COVER\tOEBPS/img/cover.png\n' in out, 'tanpa deklarasi: gambar bernama "cover" dipakai sebagai sampul')
    else: check('COVER\tOEBPS/img/cover.png\n' in out and ('COVERGET\t0\t%d' % len(BIG)) in out, 'sampul via %s diekstrak utuh' % mode)
    if mode != 'none': check(open(os.path.join(od, 'cover.bin'), 'rb').read() == BIG, 'byte sampul identik (%s)' % mode)
    t0 = open(os.path.join(od, '000.txt'), encoding='utf-8').read()
    check('Sebelum gambar.\n\x020\nSesudah gambar.\n' in t0, 'penanda gambar \\x020 di antara paragraf (%s)' % mode)
    check(t0.count('\x02') == 2 and t0.count('\x020\n') == 2 and 'hiasan kecil' in t0 and out.count('IMG\t0\t') == 1, 'gambar kecil/hilang/eksternal dilewati; gambar sama dipakai ulang (satu entri) (%s)' % mode)
    check(('IMG\t0\t0\tOEBPS/img/pic one.png\t%d\t0\t%d' % (len(BIG), len(BIG))) in out, 'data gambar bab terbaca utuh (%s)' % mode)
    t1 = open(os.path.join(od, '001.txt'), encoding='utf-8').read()
    check(t1.startswith('\x020\n'), 'gambar dalam <svg><image> terbaca (%s)' % mode)
# tata letak: gambar = halaman sendiri
p = os.path.join(tmp, 'imgtext.txt'); open(p, 'wb').write(b'Awal paragraf.\n' + b'kata ' * 200 + b'\n\x020\nSesudah.\n\x021\n\x022\nAkhir.\n')
for mode in (0, 1):
    rc, out, err = run('uilayout', p, 100, 6, mode)
    check(rc == 0 and 'imgPages=3' in out, 'tiga gambar = tiga halaman sendiri (mode %d): %s' % (mode, out.strip()))

print('[markdown]')
md = ('\ufeff# Judul **Besar** `kode`\n\nParagraf satu dengan *miring*, __tebal__, ~~coret~~ dan [tautan](http://x.y/z_(a)) serta ![gambar](a.png) <b>html</b> &amp; caf\u00e9.\n'
      'Baris lanjutan paragraf.  \nBaris setelah hard-break.\n\n## Daftar\n\n- satu\n- dua dengan snake_case_word\n  lanjutan\n* tiga\n1. pertama\n2) kedua\n\n> kutipan\n> berlanjut\n\n```\nkode  rapat\n  indent\n```\n\n---\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\nAkhir 2 * 3 = 6 \\* literal.\n')
p = os.path.join(tmp, 't.md'); open(p, 'w', encoding='utf-8').write(md)
od = os.path.join(tmp, 'omd'); os.makedirs(od, exist_ok=True)
rc, out, err = run('book', 'md', p, od, 1)
t = open(os.path.join(od, '000.txt'), encoding='utf-8').read()
exp_lines = ['\x01Judul Besar kode', 'Paragraf satu dengan miring, tebal, coret dan tautan serta [gambar] html & caf\u00e9. Baris lanjutan paragraf.', 'Baris setelah hard-break.',
             '\x01Daftar', '* satu', '* dua dengan snake_case_word lanjutan', '* tiga', '1. pertama', '2. kedua', 'kutipan berlanjut', 'kode rapat', 'indent', '\x01* * *', 'A | B', '1 | 2', 'Akhir 2 * 3 = 6 * literal.']
got = [l for l in t.split('\n') if l != '']
check(rc == 0 and got == exp_lines, 'Markdown -> teks kanonik' + ('' if got == exp_lines else '\n   dapat: %r\n   harus: %r' % (got, exp_lines)))
check('TITLE\tJudul Besar kode' in out and 'NTOC\t3' in out, 'judul dokumen dari heading pertama + daftar isi (3)')
# md acak tidak crash
import random; random.seed(3)
alpha = list('#*_`[]()<>!~|-=\\ \n\t&;abc123.>')
for i in range(300):
    s = ''.join(random.choice(alpha) for _ in range(random.randrange(1, 400)))
    open(p, 'w').write(s)
    rc, out, err = run('book', 'md', p, od, random.randrange(2))
    if rc != 0 or 'ERROR' in err or 'runtime error' in err: check(False, 'md acak #%d: %s %r' % (i, err[:200], s[:80])); break
else: check(True, 'Markdown acak (300x) tanpa crash/bocor')
print('\nSELESAI v2: %s' % ('SEMUA LULUS' if not fails else '%d GAGAL' % fails))
sys.exit(1 if fails else 0)
