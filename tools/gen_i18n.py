#!/usr/bin/env python3
"""gen_i18n.py -- tools/i18n_pairs.txt  ->  i18n_dict.h   (I18N_V1)

Jalankan dari root repo:  python3 tools/gen_i18n.py
Format baris di i18n_pairs.txt:  TEKS_INDONESIA ::: ENGLISH_TEXT   ('#' = komentar)
Teks harus sama persis dgn literal di kode (spasi di ujung juga dihitung)."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
src  = os.path.join(here, 'i18n_pairs.txt')
dst  = os.path.join(here, '..', 'i18n_dict.h')

def cesc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

pairs, seenId, seenEn = [], {}, {}
for ln, line in enumerate(open(src, encoding='utf-8'), 1):
    line = line.rstrip('\n')
    if not line.strip() or line.startswith('#'):
        continue
    if ' ::: ' not in line:
        sys.exit('%s:%d: tidak ada " ::: " -> %r' % (src, ln, line))
    idt, ent = line.split(' ::: ', 1)
    if idt == ent:
        continue                       # sama di dua bahasa: tak perlu masuk kamus
    pr = (idt, ent)
    if pr in pairs:
        continue
    # teks sumber (kolom id) harus unik & tak boleh juga jadi kolom en pasangan lain (ambigu)
    if idt in seenId or idt in seenEn or ent in seenId:
        sys.exit('%s:%d: bentrok %r / %r dgn pasangan lain' % (src, ln, idt, ent))
    seenId[idt] = pr; seenEn.setdefault(ent, pr)   # kolom en boleh dobel (dua sumber -> satu terjemahan)
    pairs.append(pr)

out = ['// i18n_dict.h -- DIHASILKAN oleh tools/gen_i18n.py dari tools/i18n_pairs.txt. JANGAN diedit tangan.',
       '#pragma once',
       'struct I18nPair { const char* id; const char* en; };',
       'static const I18nPair I18N_DICT[] = {']
for idt, ent in pairs:
    out.append('  { "%s", "%s" },' % (cesc(idt), cesc(ent)))
out.append('};')
out.append('static const int I18N_DICT_N = (int)(sizeof(I18N_DICT)/sizeof(I18N_DICT[0]));')
open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('i18n_dict.h: %d pasangan' % len(pairs))
