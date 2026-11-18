#!/usr/bin/env python3
# Perluasan tema iOS ke SEMUA app (bukan cuma Home/Lock/CC/DI).
# Jalankan dari folder ren_phone:  python3 apply_ios_theme_patch.py
# Aman dijalankan ulang (idempoten): kalau sudah ke-patch, dilewati.
import re, sys

KIT = r'''
// =====================================================================
// iOS KIT -- gaya iOS dipakai bersama SEMUA app (bukan cuma Home/Lock/CC)
// =====================================================================
#define IOS_ORANGE 0xFCA0  // oranye iOS (tombol operator kalkulator dll)
#define IOS_GREEN  0x362B  // hijau iOS (switch ON)

// Radius otomatis dari bentuk kotak: persegi-ish = squircle kecil, tombol
// lebar-pendek = PIL penuh, kartu/baris lebar = sudut 10px (inset grouped).
int iosRadius(int w,int h,int r){
  int rr;
  if(w<=h*16/10)                 rr=(r+2<5)?5:(r+2);
  else if(w<=h*32/10 && h<=34)   rr=h/2;
  else                           rr=(r<10)?10:r;
  if(rr>h/2) rr=h/2;
  if(rr>w/2) rr=w/2;
  if(rr<0) rr=0;
  return rr;
}
// pengganti s.fillRoundRect(x,y,w,h,r,c) -- geometri & hit-test TIDAK berubah, cuma sudutnya
void iosRR(LGFX_Sprite& s,int x,int y,int w,int h,int r,uint16_t c){
  if(w<=0||h<=0) return;
  s.fillRoundRect(x,y,w,h,iosRadius(w,h,r),c);
}
// Switch ala iOS 34x20: trek hijau/abu + kenop putih bersayap bayangan
void iosSwitch(LGFX_Sprite& s,int x,int y,bool on){
  uint16_t tr = on ? (uint16_t)IOS_GREEN : blend565(T().surface2,0xFFFF,45);
  s.fillRoundRect(x,y,34,20,10,tr);
  int kx = on ? x+24 : x+10, ky=y+10;
  s.fillCircle(kx,ky+1,8,blend565(tr,0x0000,90));
  s.fillCircle(kx,ky,8,0xFFFF);
}
// Chevron tebal "<" (2 garis) buat tombol Kembali
void iosChevron(LGFX_Sprite& s,int cx,int cy,uint16_t col){
  s.drawLine(cx+3,cy-6,cx-3,cy,col); s.drawLine(cx-3,cy,cx+3,cy+6,col);
  s.drawLine(cx+4,cy-6,cx-2,cy,col); s.drawLine(cx-2,cy,cx+4,cy+6,col);
}
// Judul halaman: tebal, putih (gaya nav-bar title iOS) -- gantiin teks kecil berwarna
void iosTitle(LGFX_Sprite& s,int x,int y,const char* t){
  s.setTextSize(1);
  uiText(s,x,y,t,T().text,false,true);
}
'''

NEW_BACK = r'''void drawBack(LGFX_Sprite& s){
  if(kbVisible)return;
  // iOS: pil kaca + chevron + "Kembali" (warna aksen), plus home-indicator tipis di dasar layar
  int bx=backX(), by=backY(), cy=by+BACK_H/2;
  drawGlassCard(s,bx,by,BACK_W,BACK_H,BACK_H/2,T().surface2,170);
  iosChevron(s,bx+11,cy,T().accent);
  s.setTextColor(T().accent); s.setTextSize(1);
  s.setCursor(bx+21,by+8); s.print("Kembali");
  int hiW=84, hiX=SCR_W/2-hiW/2;
  if(hiX>bx+BACK_W+6) s.fillRoundRect(hiX,SCR_H-5,hiW,3,1,blend565(T().bg,T().text,150));
}
'''

def read(p): return open(p,encoding="utf-8").read()
def write(p,s): open(p,"w",encoding="utf-8").write(s)

def split_args(src, i):
    """src[i] == '(' pertama; return (list_args, idx_setelah_')') atau None"""
    depth=0; args=[]; cur=[]; j=i
    instr=False
    while j<len(src):
        ch=src[j]
        if instr:
            cur.append(ch)
            if ch=='\\': cur.append(src[j+1]); j+=1
            elif ch=='"': instr=False
        else:
            if ch=='"': instr=True; cur.append(ch)
            elif ch=='(':
                depth+=1
                if depth>1: cur.append(ch)
            elif ch==')':
                depth-=1
                if depth==0:
                    args.append(''.join(cur)); return args,j+1
                cur.append(ch)
            elif ch==',' and depth==1:
                args.append(''.join(cur)); cur=[]
            else: cur.append(ch)
        j+=1
    return None

def convert_rr(seg):
    out=[]; pos=0; n=0
    for m in re.finditer(r'\bs\.fillRoundRect\(', seg):
        if m.start()<pos: continue
        r=split_args(seg, m.end()-1)
        if not r: continue
        args,end=r
        if len(args)!=6: continue
        h=args[3].strip(); rad=args[4].strip(); col=args[5].strip()
        if not re.fullmatch(r'\d+',rad) or int(rad)<4: continue      # bar/progress/titik kecil: biarkan
        if re.fullmatch(r'\d+',h) and int(h)<=8: continue            # trek tipis: biarkan
        if 'divider' in col: continue
        out.append(seg[pos:m.start()]); out.append('iosRR(s,'+','.join(args)+')'); pos=end; n+=1
    out.append(seg[pos:])
    return ''.join(out), n

def main():
    # ---------- phone.ino ----------
    p='phone.ino'; s=read(p)
    if 'int iosRadius(' in s:
        print("phone.ino sudah ke-patch, dilewati"); 
    else:
        # 1) kit setelah definisi uiText
        m=re.search(r'void uiText\(LGFX_Sprite& s,int x,int y,const char\* str,uint16_t col,bool shadow,bool bold\)\{.*?\n\}\n', s, re.S)
        if not m: sys.exit("GAGAL: definisi uiText tidak ketemu")
        s=s[:m.end()]+KIT+s[m.end():]
        # 2) drawBack + lebar tombol
        m=re.search(r'void drawBack\(LGFX_Sprite& s\)\{.*?\n\}\n', s, re.S)
        if not m: sys.exit("GAGAL: drawBack tidak ketemu")
        s=s[:m.start()]+NEW_BACK+s[m.end():]
        s=s.replace('#define BACK_W 62','#define BACK_W 68',1)
        # 5) Kalkulator ala iOS (tombol bulat, operator oranye)
        old='''      uint16_t bg = isEq?T().accent2:(isOp?T().accent:(isSci?T().surface2:T().surface));
      uint16_t fg = (isOp||isEq)?T().bg:T().text;
      s.fillRoundRect(x,y,w,h,6,bg);'''
        new='''      uint16_t bg = (isEq||isOp)?(uint16_t)IOS_ORANGE:(isSci?T().surface2:T().surface);
      uint16_t fg = (isOp||isEq)?(uint16_t)0xFFFF:T().text;
      s.fillRoundRect(x,y,w,h,h/2,bg); // iOS: tombol pil/bulat penuh'''
        if old in s: s=s.replace(old,new,1); print("kalkulator: ok")
        else: print("PERINGATAN: blok warna kalkulator tidak cocok (dilewati)")
        # 6) Switch Auto-Rotate di Pengaturan
        old='''  s.fillRoundRect(arX,24,arW,18,4, autoRotateEnabled?T().good:T().surface2);
  s.setTextColor(autoRotateEnabled?T().bg:T().subtext);
  s.setCursor(arX+4,29);
  s.print(autoRotateEnabled?"Rot:ON":"Rot:OFF");'''
        new='''  s.setTextColor(T().text); s.setCursor(arX,29); s.print("Rot");
  iosSwitch(s,arX+arW-34,23,autoRotateEnabled); // iOS: switch (area ketuk lama tetap sama)'''
        if old in s: s=s.replace(old,new,1); print("pengaturan Auto-Rotate: switch iOS")
        else: print("PERINGATAN: blok Auto-Rotate tidak cocok (dilewati)")
        # 3) fillRoundRect -> iosRR di: keyboard + semua app (mulai drawSnake)
        a=s.index('void drawKb(LGFX_Sprite& s){'); b=s.index('void kbTouch(')
        kb,n1=convert_rr(s[a:b]); s=s[:a]+kb+s[b:]
        c=s.index('void drawSnake(LGFX_Sprite& s){')
        rest,n2=convert_rr(s[c:]); s=s[:c]+rest
        print(f"phone.ino: {n1} (keyboard) + {n2} (app) kotak -> gaya iOS")
        # 4) judul halaman tebal putih
        pat=re.compile(r's\.setTextColor\(T\(\)\.\w+\);\s*s\.setTextSize\(1\);\s*s\.setCursor\(8,(2[3-9])\);\s*s\.print\("([^"]+)"\);')
        s,nt=pat.subn(lambda m:'iosTitle(s,8,%s,"%s");'%(m.group(1),m.group(2)), s)
        print(f"phone.ino: {nt} judul halaman -> iosTitle")
        write(p,s)
    # ---------- file .ino lain (Musik, Wake Word) ----------
    for f in ('musicbt_renphone.ino','wakeword_renphone.ino'):
        t=read(f); t2,n=convert_rr(t)
        if n: write(f,t2)
        print(f"{f}: {n} kotak -> gaya iOS")

main()
