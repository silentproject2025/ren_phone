#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# apply_emoji_sd_patch.py -- emoji dari SD (/emoji.bin) + halaman emoji keyboard dinamis (prev/next).
# LANJUTAN dari apply_emoji_patch.py (harus sudah terpasang). emoji_renphone.ino diganti dgn versi baru
# (salin berkasnya ke folder repo dulu), phone.ino di-patch di sini.
#
#     python3 apply_emoji_sd_patch.py
# Idempoten & semua-atau-tidak-sama-sekali (anchor harus ketemu TEPAT 1x).
import sys, os

for f in ('phone.ino', 'emoji_renphone.ino'):
    if not os.path.exists(f):
        print("ERROR: %s tidak ada di folder ini" % f); sys.exit(1)

def rd(p): return open(p, encoding='utf-8').read()
def rep(src, old, new, what):
    n = src.count(old)
    if n != 1:
        print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah." % (what, n)); sys.exit(1)
    return src.replace(old, new)

s = rd('phone.ino')
if 'emjInit()' in s:
    print("phone.ino sudah memuat emoji-SD, dilewati"); sys.exit(0)
if 'emjF(' not in s:
    print("ERROR: jalankan apply_emoji_patch.py dulu (dukungan emoji dasar belum terpasang)"); sys.exit(1)
if 'emjInit' not in rd('emoji_renphone.ino'):
    print("ERROR: emoji_renphone.ino di folder ini masih versi lama (tidak punya emjInit). Salin versi baru."); sys.exit(1)

s = rep(s, "int    emjCells(const char* p, int len);\n",
           "int    emjCells(const char* p, int len);\n"
           "void   emjInit();                                      // muat /emoji.bin dari SD (sekali, di setup sesudah initSD)\n"
           "int    emjKbPages();\n"
           "bool   emjKbLabel(int page, int slot, char* out);      // label tuts emoji (3 byte UTF-8 PUA)\n"
           "int    kbEmoPage = 0;                                  // halaman emoji keyboard yg sedang tampil\n", "prototipe")
s = rep(s, "  initFfat(); // v47: siapin fallback wallpaper (flash internal) pas SD Card gak kedetek\n  loadNote();",
           "  initFfat(); // v47: siapin fallback wallpaper (flash internal) pas SD Card gak kedetek\n"
           "  emjInit();  // emoji: muat set dari SD (/emoji.bin) SEBELUM teks apa pun dinormalisasi (loadNote dst)\n  loadNote();", "setup")
s = rep(s, "      const char* lb=lay[r][c];\n",
           "      const char* lb=lay[r][c];\n      char elb[4];\n      if(emo) lb = emjKbLabel(kbEmoPage, r*10+c, elb) ? elb : \"\";   // halaman emoji: dinamis dari set aktif\n", "label")
s = rep(s, "      const char* pg=(kbMode==KB_EMO1)?\"1/2\":\"2/2\";\n"
           "      s.setTextColor(T().text); s.setCursor(shX+(shW-s.textWidth(pg))/2,cy+kh/2-4+in); s.print(pg);\n",
           "      s.fillTriangle(shX+8,acy, shX+14,acy-5, shX+14,acy+5, T().text);                  // < halaman sebelumnya\n"
           "      s.fillTriangle(shX+shW-8,acy, shX+shW-14,acy-5, shX+shW-14,acy+5, T().text);      // > halaman berikutnya\n", "shift prev/next")
s = rep(s, "    s.setCursor(spX+(spW-s.textWidth(\"spasi\"))/2,cy+kh/2-4+in); s.print(\"spasi\");",
           "    char spl[24]; if(emo) snprintf(spl,sizeof(spl),\"spasi  %d/%d\",kbEmoPage+1,emjKbPages()); else strcpy(spl,\"spasi\");\n"
           "    s.setCursor(spX+(spW-s.textWidth(spl))/2,cy+kh/2-4+in); s.print(spl);", "spasi label")
s = rep(s, "      if(emo) kbMode=(kbMode==KB_EMO1?KB_EMO2:KB_EMO1);       // di halaman emoji: ganti halaman 1/2\n",
           "      if(emo){ int np=emjKbPages(); kbEmoPage = (x<shX+shW/2) ? (kbEmoPage+np-1)%np : (kbEmoPage+1)%np; }   // < / >\n", "touch page")
s = rep(s, "  *kbTarget+=lay[r][c];\n",
           "  if(kbMode==KB_EMO1||kbMode==KB_EMO2){ char elb[4]; if(emjKbLabel(kbEmoPage,r*10+c,elb)) *kbTarget+=elb; }\n"
           "  else *kbTarget+=lay[r][c];\n", "touch insert")
with open('phone.ino', 'w', encoding='utf-8', newline='') as f: f.write(s)
print("ditulis: phone.ino")

wf = '.github/workflows/build-firmware.yml'
print("Selesai. Commit: phone.ino emoji_renphone.ino tools/make_emoji_bin.py")
