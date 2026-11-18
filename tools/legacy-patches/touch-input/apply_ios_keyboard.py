#!/usr/bin/env python3
# Keyboard ala iOS: tuts huruf lebih terang + huruf besar (size 2), tuts fungsi lebih gelap,
# bayangan 1px di bawah tiap tuts, panel kaca gelap, shift putih saat aktif, spasi berlabel.
# Posisi/ukuran tuts SAMA PERSIS (hit-test kbTouch() tidak berubah).
# Jalankan:  python3 apply_ios_keyboard.py   (aman diulang)
import sys
s=open('phone.ino',encoding='utf-8').read()
if 'IOS-KEYBOARD' in s:
    print("keyboard iOS sudah terpasang, dilewati"); sys.exit(0)
a=s.index('void drawKb(LGFX_Sprite& s){'); b=s.index('void kbTouch(')
NEW=r'''void drawKb(LGFX_Sprite& s){
  // IOS-KEYBOARD: panel kaca gelap + tuts bertinggi bayangan 1px, huruf besar.
  int y0=kbY();
  s.fillRect(0,y0-3,SCR_W,SCR_H-y0+3,blend565(T().bg,T().surface,175));
  s.drawFastHLine(0,y0-3,SCR_W,T().divider);
  auto* lay=kbMaps[(int)kbMode];
  int kw=kbKeyW(), kh=kbKeyH();
  const uint16_t keyC = blend565(T().surface2,0xFFFF,40);   // tuts huruf/angka: abu lebih terang
  const uint16_t fnC  = T().surface2;                       // tuts fungsi: lebih gelap
  const uint16_t shC  = blend565(T().bg,0x0000,140);        // bayangan di bawah tuts
  for(int r=0;r<3;r++){
    int ry=y0+r*(kh+2), ox=kbOffCol(r);
    for(int c=0;c<10;c++){
      int kx=ox+c*(kw+2);
      s.fillRoundRect(kx,ry+1,kw,kh,6,shC);
      s.fillRoundRect(kx,ry,kw,kh,6,keyC);
      s.drawFastHLine(kx+6,ry+1,kw-12,blend565(keyC,0xFFFF,50));
      const char* lb=lay[r][c];
      s.setTextSize(2); s.setTextColor(T().text);
      int tw=s.textWidth(lb);
      s.setCursor(kx+(kw-tw)/2,ry+(kh-16)/2);
      s.print(lb);
    }
  }
  s.setTextSize(1);
  int cy=y0+3*(kh+2);
  int spaceW = SCR_W - 96 - 88 - 8;
  int spW = max(40,spaceW);
  // Shift: putih (ikon gelap) saat aktif, abu gelap (ikon putih) saat mati
  bool up = (kbMode==KB_UPPER);
  s.fillRoundRect(4,cy+1,40,kh,6,shC);
  s.fillRoundRect(4,cy,40,kh,6, up?(uint16_t)0xFFFF:fnC);
  {
    uint16_t ic = up? (uint16_t)0x0000 : (uint16_t)0xFFFF;
    int acx=4+20, acy=cy+kh/2;
    s.fillTriangle(acx,acy-6, acx-6,acy+2, acx+6,acy+2, ic);
    s.fillRect(acx-3,acy+2,6,4,ic);
  }
  // 123 / ABC
  s.fillRoundRect(48,cy+1,44,kh,6,shC);
  s.fillRoundRect(48,cy,44,kh,6,fnC);
  {
    const char* nl=(kbMode==KB_NUM)?"ABC":"123";
    s.setTextColor(T().text);
    s.setCursor(48+(44-s.textWidth(nl))/2,cy+kh/2-4); s.print(nl);
  }
  // Spasi
  s.fillRoundRect(96,cy+1,spW,kh,6,shC);
  s.fillRoundRect(96,cy,spW,kh,6,keyC);
  s.setTextColor(T().text);
  s.setCursor(96+(spW-s.textWidth("spasi"))/2,cy+kh/2-4); s.print("spasi");
  // Hapus (backspace) -- abu gelap + ikon putih
  s.fillRoundRect(SCR_W-88,cy+1,84,kh,6,shC);
  s.fillRoundRect(SCR_W-88,cy,84,kh,6,fnC);
  {
    int acx=SCR_W-88+42, acy=cy+kh/2;
    s.fillTriangle(acx-8,acy, acx,acy-6, acx,acy+6, 0xFFFF);
    s.fillRect(acx,acy-3,10,6,0xFFFF);
  }
}

'''
s=s[:a]+NEW+s[b:]
open('phone.ino','w',encoding='utf-8').write(s)
print("OK: keyboard ala iOS terpasang")
