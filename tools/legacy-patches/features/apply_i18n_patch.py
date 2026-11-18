#!/usr/bin/env python3
"""apply_i18n_patch.py -- sistem 2 bahasa (English default + Indonesia).   I18N_V1

Jalankan dari root repo:   python3 apply_i18n_patch.py          (idempoten; aman diulang)
Opsi:  --dry   cuma laporkan apa yg akan diubah, tanpa menulis file

Yang dilakukan:
  1. phone.ino : #include "i18n.h", loadLangPref() saat boot, baris "Bahasa / Language"
     di Pengaturan (bagian TAMPILAN) + ikon, ketuk = ganti EN <-> ID dan simpan ke NVS,
     cache Home/Control Center di-invalidasi supaya teks langsung berganti.
  2. Semua *.ino : literal teks UI yg ada di kamus (tools/i18n_pairs.txt) dibungkus RPTR("...")
     HANYA pada baris penggambar teks (s.print, showToast, iosTitle, ...), array hari/bulan,
     sapaan Home, dan label app. Teks yg tidak ada di kamus dibiarkan apa adanya.
Menambah terjemahan baru: edit tools/i18n_pairs.txt -> python3 tools/gen_i18n.py -> python3 apply_i18n_patch.py
"""
import re, sys, glob, os, subprocess

DRY = '--dry' in sys.argv
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
MARK = 'I18N_V1'

# ---------- kamus ----------
sys.path.insert(0, os.path.join(ROOT, 'tools'))
def load_dict():
    keys = set()
    for line in open('tools/i18n_pairs.txt', encoding='utf-8'):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#') or ' ::: ' not in line: continue
        a, b = line.split(' ::: ', 1)
        if a != b: keys.add(a); keys.add(b)
    return keys
KEYS = load_dict()

# kata ambigu / pendek: HANYA dibungkus di tempat khusus (array tanggal, label app), bukan di sembarang print
NO_SINK_WRAP = {'Jam', 'Min', 'Sen', 'Sel', 'Rab', 'Kam', 'Jum', 'Sab', 'Mei', 'Agu', 'Okt', 'Des',
                'Minggu', 'Senin', 'Selasa', 'Rabu', 'Kamis', 'Jumat', 'Sabtu', 'Sun', 'Mon', 'Tue',
                'Wed', 'Thu', 'Fri', 'Sat', 'May', 'Aug', 'Oct', 'Dec', 'Clock'}

SINK = re.compile(
    r'\b(?:s|display|tft|spr|sprite)\s*\.\s*(?:print|println|drawString|drawCentreString|drawRightString|textWidth)\s*\(|'
    r'\b(?:showToast|iosTitle|callCenter|chsPrintC|rpHL|uiText|aviTextC|bkButton|diNotify|diBeginProgress|'
    r'diFinishProgress|settAddRow|infDrawMenuButton|hwmonDrawCoreBar|camBtn|musDrawLabel|settTextR|'
    r'updCenterText|drawLoadingRing|drawNasaError|updDrawBusy|updDrawPhase|updDrawProgress|'
    r'chatPrintCentered|camTextC|homeDrawLabel)\s*\(')
# variabel label yg lazim dipakai lalu digambar: label="..." / title="..." (dicek juga ke kamus)
ASSIGN = re.compile(r'\b(?:label|lbl|title|msg|hdr|subtitle|caption)\s*=\s*$')
EXCL = re.compile(r'Serial|strcmp|strcasecmp|strncmp|strstr|==\s*"|"\s*==|!=\s*"|\.equals|startsWith|endsWith|'
                  r'indexOf|\.begin\(|SD_MMC|FFat|\bopen\(|putString|getString|server\.|\bws\.|\bhttp\.|HTTPClient|'
                  r'static\s|#define|PROGMEM|\bf\.print|\bfile\.print|\bclient\.')
ARRAY_LINE = re.compile(r'^\s+const\s+char\s*\*\s*(?:days|mons|ldays|lmons)\s*\[\s*\]\s*=\s*\{.*\};\s*$')

def split_line(line):
    """-> list of (kind, text) kind in 'code','str','cmt'  (menghormati escape & // di dalam string)"""
    out, i, n = [], 0, len(line)
    buf = ''
    while i < n:
        c = line[i]
        if c == '"':
            if buf: out.append(('code', buf)); buf = ''
            j = i + 1
            while j < n and line[j] != '"':
                j += 2 if line[j] == '\\' else 1
            out.append(('str', line[i:j + 1])); i = j + 1
        elif c == "'":                      # literal char: 'x' atau '\''
            j = i + 1
            while j < n and line[j] != "'":
                j += 2 if line[j] == '\\' else 1
            buf += line[i:j + 1]; i = j + 1
        elif c == '/' and line.startswith('//', i):
            if buf: out.append(('code', buf)); buf = ''
            out.append(('cmt', line[i:])); i = n
        else:
            buf += c; i += 1
    if buf: out.append(('code', buf))
    return out

def wrap_line(line, array_mode=False):
    """bungkus literal terpilih dgn RPTR(). Kembalikan (baris_baru, jumlah)."""
    parts = split_line(line.rstrip('\n'))
    code = ''.join(t for k, t in parts if k != 'cmt')
    if not array_mode:
        if not SINK.search(code) and not ASSIGN.search(re.sub(r'"[^"]*"', '', code) + ' ') and not ASSIGN_IN(parts):
            return line, 0
        if EXCL.search(code): return line, 0
    n = 0; res = []; prefix = ''
    for idx, (k, t) in enumerate(parts):
        if k == 'str':
            inner = t[1:-1]
            ok = inner in KEYS and (array_mode or inner not in NO_SINK_WRAP)
            prev = prefix.rstrip()
            if ok and prev.endswith('RPTR('): ok = False                       # sudah dibungkus
            if ok and re.search(r'sizeof\s*\($', prev): ok = False
            if ok and re.search(r'\bchar\s+\w+\s*\[[^\]]*\]\s*=$', prev): ok = False   # char x[]="..." tak boleh
            if ok and re.search(r'\bconst\s+char\s+\w+\s*\[[^\]]*\]\s*=$', prev): ok = False
            # literal bersambung "a" "b" -> tak bisa dibungkus
            if ok:
                nxt = next((tt for kk, tt in parts[idx + 1:] if kk != 'cmt'), '')
                nxk = next((kk for kk, tt in parts[idx + 1:] if kk != 'cmt'), '')
                prv = parts[idx - 1] if idx > 0 else ('code', '')
                if (nxk == 'str' and nxt.strip() == '') or (prv[0] == 'str'): ok = False
                if idx + 1 < len(parts) and parts[idx + 1][0] == 'str': ok = False
                if idx > 0 and prv[0] == 'code' and prv[1].strip() == '' and idx > 1 and parts[idx - 2][0] == 'str': ok = False
            if ok:
                t = 'RPTR(' + t + ')'; n += 1
            res.append(t); prefix += t
        else:
            res.append(t); prefix += t
    return ''.join(res) + ('\n' if line.endswith('\n') else ''), n

def ASSIGN_IN(parts):
    pre = ''
    for k, t in parts:
        if k == 'str':
            if ASSIGN.search(pre): return True
            pre += '""'
        elif k == 'code':
            pre += t
    return False

WRAP = 'RPTR('
def _unwrap(b):
    out, i = '', 0
    L = len(WRAP)
    while i < len(b):
        if b.startswith(WRAP + '"', i):
            k = i + L + 1
            while b[k] != '"': k += 2 if b[k] == '\\' else 1
            out += b[i + L:k + 1]; i = k + 2          # lewati RPTR( dan ')' penutup
        else:
            out += b[i]; i += 1
    return out

def sanity_unwrap(a, b):
    """hasil (b) setelah semua RPTR( ) dibuang harus == asli (a) setelah RPTR( ) dibuang."""
    return _unwrap(b) == _unwrap(a)

# ---------- 1. bungkus literal di semua .ino ----------
changed_files, total = {}, 0
for f in sorted(glob.glob('*.ino')):
    if '.bak' in f or f.endswith('.orig') or f.endswith('.broken') or 'sebelum-pindah' in f: continue
    src = open(f, encoding='utf-8', errors='surrogateescape').read().split('\n')
    new, cnt = [], 0
    raw_end = None                       # di dalam R"NAMA( ... )NAMA"  (halaman HTML/JS) -> JANGAN disentuh
    for ln in src:
        s = ln.lstrip()
        if raw_end is not None:
            if raw_end in ln: raw_end = None
            new.append(ln); continue
        mr = re.search(r'R"([A-Za-z_0-9]*)\(', ln)
        if mr:
            end = ')' + mr.group(1) + '"'
            if end not in ln[mr.end():]: raw_end = end
            new.append(ln); continue
        if s.startswith('//') or s.startswith('#') or s == '' or not ln[:1].isspace():
            new.append(ln); continue      # komentar / preprosesor / kolom 0 (scope global) -> lewati
        arr = bool(ARRAY_LINE.match(ln))
        l2, n = wrap_line(ln, array_mode=arr)
        if n and not sanity_unwrap(ln, l2):
            sys.exit('SANITY GAGAL di %s: %r' % (f, ln))
        new.append(l2); cnt += n
    if cnt:
        changed_files[f] = '\n'.join(new); total += cnt
        print('  %-28s %4d literal dibungkus' % (f, cnt))
print('TOTAL literal dibungkus:', total)

# ---------- 2. edit khusus (exact-match, wajib ketemu persis sekali) ----------
def sub1(text, old, new, what, f):
    c = text.count(old)
    if c != 1: sys.exit('GAGAL [%s]: pola %r ketemu %d kali di %s' % (what, old[:60], c, f))
    return text.replace(old, new)

P = changed_files.get('phone.ino') or open('phone.ino', encoding='utf-8', errors='surrogateescape').read()
if MARK + '_CORE' not in P:
    P = sub1(P, '#include "display_conf.h"', '#include "display_conf.h"\n#include "i18n.h" // ' + MARK + '_CORE: sistem 2 bahasa (English default + Indonesia)', 'include', 'phone.ino')
    P = sub1(P, '  loadFontPref();\n', '  loadFontPref();\n  loadLangPref(); // ' + MARK + ': bahasa UI (default English)\n', 'boot-load', 'phone.ino')
    # enum baris Pengaturan
    P = sub1(P, 'SR_HEAPLOG, SR_HWS };', 'SR_HEAPLOG, SR_HWS, SR_LANG };', 'enum', 'phone.ino')
    # tata letak: tepat sesudah baris Font
    P = sub1(P, '  settAddRow(SR_FONT,g,34,0,nullptr,y);\n', '  settAddRow(SR_FONT,g,34,0,nullptr,y);\n  settAddRow(SR_LANG,g,34,0,nullptr,y);   // ' + MARK + ': Bahasa / Language\n', 'layout', 'phone.ino')
    # chip
    P = sub1(P, 'case SR_FONT: case SR_ROT: case SR_HOMEEDIT: case SR_SPI:\n', 'case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_SPI:\n', 'chip-has', 'phone.ino')
    P = sub1(P, '    case SR_FONT:     col=CHIP_INDIGO; break;\n', '    case SR_FONT:     col=CHIP_INDIGO; break;\n    case SR_LANG:     col=CHIP_BLUE;   break;   // ' + MARK + '\n', 'chip-col', 'phone.ino')
    P = sub1(P, '    case SR_ROT:       // panah putar\n',
             '    case SR_LANG:      // ' + MARK + ': ikon bahasa = bola dunia + garis lintang\n'
             '      nyxAaCircleRing(s, cx,cy,7,W); nyxAaCircleRing(s, cx,cy,3,W); s.drawFastHLine(cx-7,cy,15,W); s.drawFastVLine(cx,cy-7,15,W);\n'
             '      break;\n'
             '    case SR_ROT:       // panah putar\n', 'chip-icon', 'phone.ino')
    # gambar baris
    P = sub1(P, '      case SR_HOMEEDIT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR("Atur Layar Home"));',
             '      case SR_LANG:      // ' + MARK + ': Bahasa / Language\n'
             '        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR("Bahasa"));\n'
             '        settChevron(s,xr-3,ty+4,T().subtext);\n'
             '        settTextR(s,xr-12,ty,langName(uiLang),T().subtext);\n'
             '        break;\n'
             '      case SR_HOMEEDIT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR("Atur Layar Home"));', 'draw-row', 'phone.ino')
    # ketuk
    P = sub1(P, '    case SR_HOMEEDIT:\n      navPush(SCR_HOMEEDIT); break;\n',
             '    case SR_LANG:      // ' + MARK + ': ganti English <-> Indonesia, simpan, segarkan semua cache gambar\n'
             '      uiLang=(uiLang==LANG_EN)?LANG_ID:LANG_EN; saveLangPref();\n'
             '      ccCacheValid=false; ccBgValid=false; homeCacheValid=false;\n'
             '      showToast(uiLang==LANG_ID?"Bahasa: Indonesia":"Language: English");\n'
             '      needRedraw=true; break;\n'
             '    case SR_HOMEEDIT:\n      navPush(SCR_HOMEEDIT); break;\n', 'touch-row', 'phone.ino')
    # sapaan Home
    for w in ('Pagi', 'Siang', 'Sore', 'Malam'):
        P = P.replace('return "Selamat %s";' % w, 'return RPTR("Selamat %s");' % w)
    # label app di Home / switcher / Atur Home
    P = P.replace('homeDrawLabel(s, apps[i].name,', 'homeDrawLabel(s, RPTR(apps[i].name),')
    P = sub1(P, 'char nm[28]; strncpy(nm,apps[ai].name,sizeof(nm)-1);', 'char nm[28]; strncpy(nm,RPTR(apps[ai].name),sizeof(nm)-1);', 'switcher-name', 'phone.ino')
    # pencarian app di Home: cocokkan nama yg tampil (bahasa aktif) DAN nama aslinya
    P = sub1(P, '    String nm=apps[i].name; nm.toLowerCase();\n    if(nm.indexOf(q)>=0) homeFilteredIdx[homeFilteredCount++]=i;',
             '    String nm=RPTR(apps[i].name); nm.toLowerCase(); String nm0=apps[i].name; nm0.toLowerCase();\n'
             '    if(nm.indexOf(q)>=0 || nm0.indexOf(q)>=0) homeFilteredIdx[homeFilteredCount++]=i;   // ' + MARK + '\n', 'home-search', 'phone.ino')
    # array hari/bulan pendek
    P = P.replace('const char* days[]={"Min","Sen","Sel","Rab","Kam","Jum","Sab"};',
                  'const char* days[]={RPTR("Min"),RPTR("Sen"),RPTR("Sel"),RPTR("Rab"),RPTR("Kam"),RPTR("Jum"),RPTR("Sab")};')
    changed_files['phone.ino'] = P

# kasus khusus: tombol tampil/sembunyi sandi ("Tutup"/"Lihat") -> Hide/Show di English
_P = changed_files.get('phone.ino') or open('phone.ino', encoding='utf-8', errors='surrogateescape').read()
_P2 = re.sub(r'settShowPass\?RPTR\("Tutup"\):(?:RPTR\("Lihat"\)|"Lihat")',
             'settShowPass?(uiLang==LANG_ID?"Tutup":"Hide"):(uiLang==LANG_ID?"Lihat":"Show")', _P)
if _P2 != _P: changed_files['phone.ino'] = _P2

H = open('homeedit_renphone.ino', encoding='utf-8', errors='surrogateescape').read()
if 'RPTR(apps[i].name)' not in H:
    H = sub1(H, 'strncpy(nm, apps[i].name, sizeof(nm) - 1)', 'strncpy(nm, RPTR(apps[i].name), sizeof(nm) - 1)', 'homeedit-name', 'homeedit_renphone.ino')
    changed_files['homeedit_renphone.ino'] = H
O = open('owner_renphone.ino', encoding='utf-8', errors='surrogateescape').read()
if 'RPTR(apps[bi].name)' not in O:
    O = sub1(O, 'strncpy(nm, apps[bi].name, sizeof(nm) - 1)', 'strncpy(nm, RPTR(apps[bi].name), sizeof(nm) - 1)', 'owner-name', 'owner_renphone.ino')
    changed_files['owner_renphone.ino'] = O

if DRY:
    print('--dry: tidak ada file yg ditulis'); sys.exit(0)
for f, txt in changed_files.items():
    open(f, 'w', encoding='utf-8', errors='surrogateescape').write(txt)
print('Selesai. File berubah:', ', '.join(sorted(changed_files)))
