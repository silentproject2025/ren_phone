#!/usr/bin/env python3
"""
Home screen: ganti kartu kaca besar per-app jadi ikon BULAT KECIL + label.
Jalankan di root repo (folder phone.ino):  python apply_home_icons_patch.py
Idempotent. Mau balik ke tampilan lama: git checkout HEAD~1 -- phone.ino (kalau ini commit terakhir).
"""
import sys, os
path = "phone.ino"
if not os.path.exists(path):
    sys.exit("phone.ino tidak ketemu. Jalankan dari root repo.")
s = open(path, encoding="utf-8").read()

def edit(name, old, new, marker, count=1):
    global s
    if marker in s:
        print(f"[skip ] {name}"); return
    n = s.count(old)
    if n != count:
        sys.exit(f"[GAGAL] {name}: anchor {n}x (harus {count}x)")
    s = s.replace(old, new); print(f"[ok   ] {name}")

M = "// HOME-ICON-BULAT"
edit("grid: 4 kolom landscape / 3 portrait",
     "int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 3 : 2; }",
     "int homeCols(){ return currentOrient==ORIENT_LANDSCAPE ? 4 : 3; } " + M + ": dulu 3/2 kolom (kartu besar)",
     M)
edit("lebar sel (pakai gap 10 spt drawHome)",
     "int homeCardW(){ int cols=homeCols(); return (SCR_W - (cols+1)*8)/cols; }",
     "int homeCardW(){ int cols=homeCols(); return (SCR_W - (cols+1)*10)/cols; } // HOME-ICON-BULAT: gap 10 = sama dgn drawHome/touch",
     "gap 10 = sama dgn drawHome/touch")
edit("tinggi sel 88 -> 62",
     "int homeCardH(){ return 88; }",
     "int homeCardH(){ return 62; } // HOME-ICON-BULAT: ikon bulat kecil + label, tanpa kartu kaca (dulu 88)",
     "ikon bulat kecil + label, tanpa kartu kaca")
edit("radius ikon",
     "int iconR = min(cw,ch-18)/2 - 6; if(iconR>26) iconR=26; if(iconR<14) iconR=14;",
     "int iconR = min(cw,ch-16)/2 - 2; if(iconR>24) iconR=24; if(iconR<14) iconR=14; // HOME-ICON-BULAT",
     "ch-16)/2 - 2")

# helper label (dipotong dgn '.' kalau kepanjangan)
edit("helper label",
     "void drawHome(LGFX_Sprite& s,float sc){\n",
     "// HOME-ICON-BULAT: label di bawah ikon bulat, dipotong pakai '.' kalau lebih lebar dari sel\n"
     "void homeDrawLabel(LGFX_Sprite& s,const char* name,int cx,int y,int maxW){\n"
     "  char buf[24]; strncpy(buf,name,sizeof(buf)-1); buf[sizeof(buf)-1]=0;\n"
     "  s.setTextSize(1);\n"
     "  int n=(int)strlen(buf); bool cut=false;\n"
     "  while(n>3 && s.textWidth(buf)>maxW){ buf[--n]=0; cut=true; }\n"
     "  if(cut && n>=1) buf[n-1]='.';\n"
     "  int w=s.textWidth(buf);\n"
     "  s.setCursor(cx-w/2,y); s.print(buf);\n"
     "}\n\n"
     "void drawHome(LGFX_Sprite& s,float sc){\n",
     "void homeDrawLabel(")

# grid hasil pencarian (indent 8) & grid utama (indent 6).
# Anchor & marker diawali "\n" + indent persis, supaya blok indent-6 tidak cocok
# dgn substring blok indent-8 (bug versi sebelumnya).
def block(name, indent):
    sp = " " * indent
    edit(name + ": buang kartu kaca",
         "\n" + sp + "drawGlassPanel(s, x, y, cw, ch, 16, tileTint, tileAlpha);\n",
         "\n" + sp + "(void)tileTint; (void)tileAlpha; // HOME-ICON-BULAT: tanpa kartu kaca\n",
         "\n" + sp + "(void)tileTint; (void)tileAlpha; // HOME-ICON-BULAT")
    edit(name + ": label",
         "\n" + sp + "int nl=s.textWidth(apps[i].name);\n" + sp + "s.setCursor(x+cw/2-nl/2, y+ch-14);\n" + sp + "s.print(apps[i].name);\n",
         "\n" + sp + "homeDrawLabel(s, apps[i].name, x+cw/2, y+ch-14, cw+6);\n",
         "\n" + sp + "homeDrawLabel(s, apps[i].name, x+cw/2, y+ch-14, cw+6);")
block("grid cari", 8)
block("grid utama", 6)
open(path, "w", encoding="utf-8").write(s)
print("Selesai.")
