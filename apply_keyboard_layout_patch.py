import sys, os
if not os.path.exists('phone.ino'): print("ERROR: phone.ino tidak ada (jalankan dari root repo)"); sys.exit(1)
s=open('phone.ino',encoding='utf-8').read()
if 'kbShW()' in s: print("Layout keyboard sudah terpasang, dilewati"); sys.exit(0)
def rep(src,old,new,what):
    if src.count(old)!=1: print("ERROR: anchor '%s' ketemu %d kali (harus 1). Tidak ada yang diubah."%(what,src.count(old))); sys.exit(1)
    return src.replace(old,new)
s=rep(s,"int kbKeyH(){ return 22; }",
"""// accretion-v2: layar sentuh KAPASITIF (jari) + layar lebih besar -> tuts dibuat lebih tinggi.
// 22px = ~3,3mm di 3,5" 480x320, terlalu kecil buat jari. Tinggi mengikuti SCR_H, lebar fungsi mengikuti SCR_W.
int kbKeyH(){ return SCR_H>=440 ? 40 : (SCR_H>=300 ? 30 : 22); }
int kbShW(){ return SCR_W>=440 ? 64 : 40; }    // tombol Shift
int kbNmW(){ return SCR_W>=440 ? 70 : 44; }    // tombol 123/ABC
int kbDlW(){ return SCR_W>=440 ? 110 : 84; }   // tombol hapus""","kbKeyH")
s=rep(s,"      s.setTextSize(2); s.setTextColor(T().text);\n      int tw=s.textWidth(lb);\n      s.setCursor(kx+(kw-tw)/2,ry+(kh-16)/2);",
"      int ts=(kh>=36)?3:2; s.setTextSize(ts); s.setTextColor(T().text);\n      int tw=s.textWidth(lb);\n      s.setCursor(kx+(kw-tw)/2,ry+(kh-ts*8)/2);","draw glyph")
s=rep(s,"  int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;\n  int dlW=84, dlX=R-dlW;",
"  int shX=L, shW=kbShW(), nmX=shX+shW+4, nmW=kbNmW(), spX=nmX+nmW+4;\n  int dlW=kbDlW(), dlX=R-dlW;","draw bottom")
s=rep(s,"    int shX=L, shW=40, nmX=shX+shW+4, nmW=44, spX=nmX+nmW+4;\n    int dlX=R-84;",
"    int shX=L, shW=kbShW(), nmX=shX+shW+4, nmW=kbNmW(), spX=nmX+nmW+4;\n    int dlX=R-kbDlW();","touch bottom")
open('phone.ino','w',encoding='utf-8').write(s)
print("OK: layout keyboard terpasang (tuts lebih tinggi, tombol fungsi proporsional).")
