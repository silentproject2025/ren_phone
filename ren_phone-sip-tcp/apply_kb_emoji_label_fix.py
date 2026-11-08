#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_kb_emoji_label_fix.py -- label baris bawah keyboard emoji tidak lagi bergeser.

Gejala: begitu tombol emoji ditekan, tulisan "spasi  2/47" bergeser ke kiri dan
menimpa tombol emoji (label "ABC" juga sedikit miring ke kiri).
Penyebab: drawKb() memusatkan label dengan s.textWidth(), padahal di halaman emoji
font aktifnya pembungkus emjF(Font0), dan lebar yang dilaporkan untuk teks ASCII
tidak sama dengan lebar yang benar-benar tergambar.
Perbaikan: di halaman emoji, lebar label dihitung sendiri (Font0 = 6 px per
karakter pada ukuran teks 1). Halaman huruf/angka tidak disentuh.

Jalankan dari root repo. Idempotent.
"""
import sys, os

P = "phone.ino"
if not os.path.exists(P):
    sys.exit("ERROR: phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
if crlf:
    s = s.replace("\r\n", "\n")

if "KB_EMO_LABEL_FIX" in s:
    print("[skip] KB_EMO_LABEL_FIX sudah terpasang")
    sys.exit(0)

old1 = (
    '    const char* nl=(kbMode==KB_NUM||emo)?"ABC":"123";\n'
    '    s.setTextColor(T().text);\n'
    '    s.setCursor(nmX+(nmW-s.textWidth(nl))/2,cy+kh/2-4+in); s.print(nl);\n'
)
new1 = (
    '    const char* nl=(kbMode==KB_NUM||emo)?"ABC":"123";\n'
    '    s.setTextColor(T().text);\n'
    '    int nlw = emo ? (int)strlen(nl)*6 : s.textWidth(nl);   // KB_EMO_LABEL_FIX: di halaman emoji hitung lebar sendiri (Font0 = 6 px/karakter)\n'
    '    s.setCursor(nmX+(nmW-nlw)/2,cy+kh/2-4+in); s.print(nl);\n'
)

old2 = (
    '    char spl[24]; if(emo) snprintf(spl,sizeof(spl),"spasi  %d/%d",kbEmoPage+1,emjKbPages()); else strcpy(spl,"spasi");\n'
    '    s.setCursor(spX+(spW-s.textWidth(spl))/2,cy+kh/2-4+in); s.print(spl);\n'
)
new2 = (
    '    char spl[24]; if(emo) snprintf(spl,sizeof(spl),"spasi %d/%d",kbEmoPage+1,emjKbPages()); else strcpy(spl,"spasi");\n'
    '    int splw = emo ? (int)strlen(spl)*6 : s.textWidth(spl);   // KB_EMO_LABEL_FIX\n'
    '    s.setCursor(spX+(spW-splw)/2,cy+kh/2-4+in); s.print(spl);\n'
)

for name, old, new in (("label 123/ABC", old1, new1), ("label spasi", old2, new2)):
    n = s.count(old)
    if n != 1:
        sys.exit("ERROR: blok %s ditemukan %d kali (harus 1). Kode keyboard sudah berubah?" % (name, n))
    s = s.replace(old, new)

if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("[ok] label baris bawah keyboard emoji dipasang di phone.ino")
