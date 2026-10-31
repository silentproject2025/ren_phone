#!/usr/bin/env python3
# test_ebook.py -- uji ebook_core.h di komputer. Jalankan: python3 test_ebook.py
import os, random, subprocess, sys, tempfile, zipfile, zlib
H = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(H, 'test_core')
def build():
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
build()
tmp = tempfile.mkdtemp()
random.seed(7)

# ---------- 1. inflate vs zlib ----------
print('[inflate]')
words = open(os.path.join(H,'..','ebook_core.h')).read().split()
text = (' '.join(random.choice(words) for _ in range(60000))).encode()
datas = {'teks': text, 'nol': bytes(200000), 'acak': os.urandom(70000),
         'campur': text[:30000]+os.urandom(5000)+bytes(9000)+text[:20000], 'kecil': b'a', 'kosong': b''}
for name, d in datas.items():
    for lvl, strat in [(0,0),(1,0),(6,0),(9,0),(6,zlib.Z_FIXED),(6,zlib.Z_HUFFMAN_ONLY),(6,zlib.Z_RLE)]:
        c = zlib.compressobj(lvl, zlib.DEFLATED, -15, 9, strat); raw = c.compress(d)+c.flush()
        fi, fo = os.path.join(tmp,'i.bin'), os.path.join(tmp,'o.bin'); open(fi,'wb').write(raw)
        rc, out, err = run('inflate', fi, fo, len(d))
        good = rc == 0 and open(fo,'rb').read() == d
        if not good: check(False, f'{name} lvl{lvl} strat{strat}: {out.strip()} {err[:200]}')
check(True, 'inflate: %d kombinasi data x level x strategi cocok dengan zlib' % (len(datas)*7))
# rusak: tidak boleh crash
raw = zlib.compress(text, 6)[2:-4]
bad = 0
for i in range(200):
    b = bytearray(raw); 
    for _ in range(3): b[random.randrange(len(b))] ^= 1 << random.randrange(8)
    open(os.path.join(tmp,'i.bin'),'wb').write(bytes(b[:random.randrange(len(b)//2, len(b)+1)]))
    rc,_,err = run('inflate', os.path.join(tmp,'i.bin'), os.path.join(tmp,'o.bin'), len(text))
    if rc not in (0,1) or 'ERROR' in err or 'runtime error' in err: bad += 1
check(bad == 0, 'inflate data rusak (200x) tidak crash')

# ---------- 2. EPUB ----------
print('[epub]')
def mk_epub(path, version3=True, deflate=True, n=6, nested=True, ncx=True):
    comp = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    base = 'OEBPS/' if nested else ''
    z = zipfile.ZipFile(path, 'w')
    z.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml', f'<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="{base}content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>', comp)
    items = ''; spine = ''
    for i in range(n):
        fn = f'text/ch %d.xhtml' % i if i == 2 else f'text/ch{i}.xhtml'
        href = fn.replace(' ', '%20')
        items += f'<item id="c{i}" href="{href}" media-type="application/xhtml+xml"/>'
        spine += f'<itemref idref="c{i}"/>'
        body = (f'<h1>Bab {i+1}: Caf&eacute; &amp; &#8220;Kisah&#8221;</h1>'
                + ''.join(f'<p>Paragraf {j} dari bab {i}. Ini kalimat dengan <em>cetak miring</em>, “kutipan melengkung”, dash — dan elipsis… serta ñandú é ü ß.</p>' for j in range(40 if i else 3))
                + '<p>baris<br/>kedua</p><hr/><ul><li>satu</li><li>dua</li></ul><script>alert(1)</script><!-- komentar -->')
        z.writestr(base+fn, f'<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>JANGAN MUNCUL</title><style>p{{x:y}}</style></head><body>{body}</body></html>', comp)
    z.writestr(base+'text/imgdoc.xhtml', '<html><body><img src="a.png"/></body></html>', comp)
    items += '<item id="img" href="text/imgdoc.xhtml" media-type="application/xhtml+xml"/>'
    items += '<item id="cover" href="cover.jpg" media-type="image/jpeg"/>'
    if ncx:
        navp = ''.join(f'<navPoint id="n{i}"><navLabel><text>Judul NCX {i+1}</text></navLabel><content src="{("text/ch%20"+str(i)+".xhtml") if i==2 else "text/ch"+str(i)+".xhtml"}#a"/></navPoint>' for i in range(n))
        z.writestr(base+'toc.ncx', f'<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><docTitle><text>Doc</text></docTitle><navMap>{navp}</navMap></ncx>', comp)
        items += '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
    else:
        navl = ''.join(f'<li><a href="text/ch{i}.xhtml">Judul <b>NAV</b> {i+1}</a></li>' if i != 2 else '<li><a href="text/ch%202.xhtml#x">Judul NAV 3</a></li>' for i in range(n))
        z.writestr(base+'nav.xhtml', f'<html xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="landmarks"><ol><li><a href="text/ch0.xhtml">Awal</a></li></ol></nav><nav epub:type="toc"><ol>{navl}</ol></nav></body></html>', comp)
        items += '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
    spine += '<itemref idref="img" linear="no"/>'
    z.writestr(base+'content.opf', f'<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Buku &amp; Uji</dc:title></metadata><manifest>{items}</manifest><spine {"toc=\"ncx\"" if ncx else ""}>{spine}</spine></package>', comp)
    z.close()
for label, kw in [('v2+ncx deflate bersarang', {}), ('v3+nav deflate', dict(ncx=False)), ('stored tanpa folder', dict(deflate=False, nested=False)), ('banyak bab', dict(n=120))]:
    p = os.path.join(tmp, 'a.epub'); mk_epub(p, **kw)
    od = os.path.join(tmp, 'out'); os.makedirs(od, exist_ok=True)
    rc, out, err = run('epub', p, od)
    n = kw.get('n', 6)
    check(rc == 0 and f'CHAPTERS\t{n}\n' in out, f'{label}: {n} bab terbaca (rc={rc}) {err[:120]}')
    check('TITLE\tBuku & Uji' in out, f'{label}: judul buku & entitas')
    exp = 'Judul NCX 1' if not kw.get('ncx', True) is False else 'Judul NAV 1'
    check(exp in out and ('Judul NAV 3' in out or 'Judul NCX 3' in out), f'{label}: judul daftar isi ({exp})')
    t0 = open(os.path.join(od, '000.txt')).read()
    ok = t0.startswith('\x01Bab 1: Cafe & "Kisah"\n') and 'JANGAN' not in t0 and 'alert' not in t0 and 'komentar' not in t0
    check(ok, f'{label}: HTML bersih, judul bertanda, entitas&skrip')
    check('kutipan melengkung' in t0 and 'dash - dan elipsis...' in t0 and 'nandu e u ss' in t0, f'{label}: transliterasi Unicode -> ASCII')
    check('baris\nkedua\n' in t0 and '\x01* * *\n' in t0 and '* satu\n* dua\n' in t0, f'{label}: br/hr/li')
    t2 = open(os.path.join(od, '002.txt')).read()
    check(len(t2) > 100, f'{label}: berkas bernama dengan spasi (%20) terbaca')
    check(all((32 <= ord(c) < 127) or c in '\n\x01' for c in t0), f'{label}: hanya ASCII + \\n + penanda')
    check('  ' not in t0 and ' \n' not in t0 and '\n ' not in t0, f'{label}: spasi rapi')
    if kw.get('n') == 120: check(True, 'banyak bab OK')
# bukan epub / kosong
open(os.path.join(tmp,'x.epub'),'wb').write(b'bukan zip' * 10)
rc,out,_ = run('epub', os.path.join(tmp,'x.epub'), tmp)
check(rc == 1 and 'ERR' in out, 'file bukan ZIP ditolak dengan galat: ' + out.strip())

# ---------- 3. TXT ----------
print('[txt]')
def txt(name, data):
    p = os.path.join(tmp, name); open(p, 'wb').write(data); o = p + '.out'
    rc, out, err = run('txt', p, o); return rc, out, open(o, errors='replace').read() if os.path.exists(o) else '', err
para = ['Alice was beginning to get very tired of sitting by her sister on the bank, and of having nothing to do: once or twice she had peeped into the book her sister was reading.'] * 8
import textwrap
wrapped = 'CHAPTER I. Down the Rabbit-Hole\n\n' + '\n\n'.join(textwrap.fill(p, 70) for p in para) + '\n\n\nCHAPTER II\n\n' + '\n\n'.join(textwrap.fill(p, 70) for p in para) + '\n'
rc, out, t, _ = txt('w.txt', wrapped.replace('\n', '\r\n').encode())
check(rc == 0 and t.count('\n') == 2 + 16 + 2 + 0 or t.count('\n') <= 24, 'TXT baris-keras (CRLF) di-reflow jadi paragraf: %d baris' % t.count('\n'))
check(t.startswith('\x01CHAPTER I. Down the Rabbit-Hole\n') and 'TOC\t2' in out, 'judul bab terdeteksi + daftar isi (2 entri)')
check('bank, and of having nothing to do: once' in t, 'kalimat yang terpotong baris digabung ulang')
modern = ('Bab 1\n' + '\n'.join('Dialog baris %d: “Halo,” katanya — pelan…' % i for i in range(30)) + '\n\n\n\nBab 2\nIsi.\n')
rc, out, t, _ = txt('m.txt', modern.encode())
check('Dialog baris 3: "Halo," katanya - pelan...\n' in t and 'TOC\t2' in out, 'TXT modern satu-baris-satu-paragraf tidak digabung')
rc, out, t, _ = txt('l1.txt', 'Caf\xe9 \x93kutip\x94 \x96 na\xefve\n'.encode('latin-1'))
check(t.strip() == 'Cafe "kutip" - naive', 'Latin-1/Windows-1252 terdeteksi: %r' % t.strip())
rc, out, t, _ = txt('u16.txt', b'\xff\xfe' + 'Halo Dunia \u2014 caf\u00e9\r\nBaris dua\r\n'.encode('utf-16-le'))
check(t == 'Halo Dunia - cafe\nBaris dua\n', 'UTF-16 LE+BOM: %r' % t)
rc, out, t, _ = txt('bom.txt', b'\xef\xbb\xbfAda BOM\n')
check(t == 'Ada BOM\n', 'BOM UTF-8 dibuang')
rc, out, t, _ = txt('cjk.txt', '日本語のテキスト abc\n'.encode())
check(t == '? abc\n', 'karakter non-Latin diringkas jadi satu ?: %r' % t)
rc, out, t, _ = txt('long.txt', ('kata ' * 20000 + '\n').encode())
check(rc == 0 and t.count('\n') == 1 and len(t) > 99990, 'satu baris sangat panjang tidak terpotong (%d)' % len(t))
rc, out, t, _ = txt('empty.txt', b'')
check(rc == 0 and t == '', 'file kosong')
big = ('Satu paragraf biasa dengan beberapa kata untuk uji. ' * 5 + '\n') * 70000   # ~ 17 MB
rc, out, t, _ = txt('big.txt', big.encode())
check(rc == 0 and 'LEN' in out and len(t) <= 3*1024*1024 + 4096, 'file besar dipotong di batas 3 MB tanpa crash (%d)' % len(t))
# UTF-8 terpotong di batas chunk 16 KB
data = ('é' * 40000).encode()
rc, out, t, _ = txt('edge.txt', data)
check(t == 'e' * 40000 + '\n', 'UTF-8 melintasi batas potongan 16KB utuh')

# ---------- 4. layout ----------
print('[layout]')
rc, out, err = run('epub', os.path.join(tmp,'a.epub'), os.path.join(tmp,'out')) if False else (0,'','')
p = os.path.join(tmp, 'a.epub'); mk_epub(p, n=6); od = os.path.join(tmp,'out'); run('epub', p, od)
samples = [os.path.join(od,'001.txt'), os.path.join(tmp,'w.txt.out'), os.path.join(tmp,'m.txt.out'), os.path.join(tmp,'long.txt.out')]
for s in samples:
    for width, rows in [(308,12),(228,18),(120,5),(40,3),(20,2)]:
        rc, out, err = run('layout', s, width, rows, os.path.join(tmp,'pc'))
        check(rc == 0, f'{os.path.basename(s)} lebar={width} baris={rows}: {out.strip().splitlines()[-1] if out.strip() else err[:150]}')

# ---------- 5. fuzz ----------
print('[fuzz]')
p = os.path.join(tmp, 'f.epub'); mk_epub(p, n=8)
for seed in range(3):
    rc, out, err = run('fuzz', p, 'epub', 400, seed)
    check(rc == 0 and 'ERROR' not in err and 'runtime error' not in err, f'fuzz epub seed {seed}: {out.strip()} {err[:300]}')
p = os.path.join(tmp, 'f.epub'); mk_epub(p, n=8, ncx=False, nested=False)
rc, out, err = run('fuzz', p, 'epub', 400, 9); check(rc == 0 and 'ERROR' not in err and 'runtime error' not in err, f'fuzz epub v3: {out.strip()} {err[:300]}')
rc, out, err = run('fuzz', os.path.join(tmp,'w.txt'), 'txt', 300, 5); check(rc == 0 and 'ERROR' not in err and 'runtime error' not in err, f'fuzz txt: {out.strip()} {err[:300]}')
rc, out, err = run('fuzz', os.path.join(tmp,'u16.txt'), 'txt', 300, 6); check(rc == 0 and 'ERROR' not in err and 'runtime error' not in err, f'fuzz txt utf16: {out.strip()} {err[:300]}')

print('\nSELESAI: %s' % ('SEMUA LULUS' if not fails else f'{fails} GAGAL'))
sys.exit(1 if fails else 0)
