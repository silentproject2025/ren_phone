#!/usr/bin/env python3
# Tema iOS v2 -- perubahan yang BENAR-BENAR kelihatan. Jalankan SETELAH apply_ios_theme_patch.py:
#   python3 apply_ios_theme_v2.py
# Idempoten (aman dijalankan ulang).
import re, sys
def read(p): return open(p,encoding="utf-8").read()
def write(p,s): open(p,"w",encoding="utf-8").write(s)

s=read('phone.ino')
if 'void iosBackdrop(' in s:
    print("v2 sudah terpasang, dilewati"); sys.exit(0)
if 'int iosRadius(' not in s:
    sys.exit("GAGAL: jalankan apply_ios_theme_patch.py dulu")

# 1) TEMA "iOS" (warna sistem iOS dark) -- jadi default sekali (migrasi), tema lama tetap ada
old='  { "Pastel", 0xFF1F,0xFFFF,0xFF9F,0xFB16,0x5D9F,0x39C7,0x9C92,0xFEB7,0x2FE7,0xE8B4 },\n};\n#define THEME_COUNT 5'
new='''  { "Pastel", 0xFF1F,0xFFFF,0xFF9F,0xFB16,0x5D9F,0x39C7,0x9C92,0xFEB7,0x2FE7,0xE8B4 },
  // iOS: hitam murni + kartu #1C1C1E/#2C2C2E + biru sistem #0A84FF + ungu #BF5AF2 + hijau/merah sistem
  { "iOS",    0x0000,0x18E3,0x2965,0x0C3F,0xBADE,0xFFFF,0x8C72,0x39C7,0x368B,0xFA27 },
};
#define THEME_COUNT 6'''
if old not in s: sys.exit("GAGAL: array themes tidak cocok")
s=s.replace(old,new,1)

old='''  Preferences p; p.begin("ui",true);
  themeIdx = p.getInt("theme",0); p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;'''
new='''  Preferences p; p.begin("ui",false);
  themeIdx = p.getInt("theme",0);
  if(p.getInt("thv",0)<2){ themeIdx=THEME_COUNT-1; p.putInt("theme",themeIdx); p.putInt("thv",2); } // tema iOS jadi default SEKALI; bisa diganti lagi di Pengaturan
  p.end();
  if(themeIdx<0||themeIdx>=THEME_COUNT) themeIdx=0;'''
if old not in s: sys.exit("GAGAL: loadTheme tidak cocok")
s=s.replace(old,new,1)

# 2) iosRR: kartu/baris lebar jadi KACA (bayangan+pantulan+rim), tombol dapat rim+kilap
m=re.search(r'void iosRR\(LGFX_Sprite& s,int x,int y,int w,int h,int r,uint16_t c\)\{.*?\n\}\n',s,re.S)
if not m: sys.exit("GAGAL: iosRR tidak ketemu")
NEW_RR='''void iosRR(LGFX_Sprite& s,int x,int y,int w,int h,int r,uint16_t c){
  if(w<=0||h<=0) return;
  int rr=iosRadius(w,h,r);
  bool card = !(w<=h*16/10) && !(w<=h*32/10 && h<=34); // baris/kartu lebar
  if(card && h>=20){ drawGlassCard(s,x,y,w,h,rr,c,225); return; } // kaca: bayangan + pantulan + rim
  s.fillRoundRect(x,y,w,h,rr,c);
  if(h>=14 && w>=2*rr+6){ // tombol: rim tipis + kilap atas
    s.drawRoundRect(x,y,w,h,rr,blend565(c,0xFFFF,46));
    s.drawFastHLine(x+rr,y+1,w-2*rr,blend565(c,0xFFFF,70));
  }
}
'''
s=s[:m.start()]+NEW_RR+s[m.end():]

# 3) iosBackdrop: latar gradasi (glow aksen di atas -> gelap) gantiin warna polos di app utilitas
BACKDROP='''// Latar ala iOS: gradasi halus (glow aksen di atas memudar ke warna dasar) -- gantiin fillSprite polos
void iosBackdrop(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  int span=SCR_H*7/10; if(span<1) span=1;
  for(int y=0;y<span;y+=6){
    int a=(int)(46*(span-y)/span); if(a<2) break;
    s.fillRect(0,y,SCR_W,6,blend565(T().bg,T().accent,(uint8_t)a));
  }
}
'''
m=re.search(r'void iosTitle\(LGFX_Sprite& s,int x,int y,const char\* t\)\{.*?\n\}\n',s,re.S)
s=s[:m.end()]+BACKDROP+s[m.end():]

APPS={'drawClock','drawPomo','drawCalc','drawSensor','drawSettings','drawNotepad','drawAiChat',
      'drawFileExplorer','drawUpdate','drawBatteryApp','drawTrivia','drawMjpegPlayer'}
pat=re.compile(r's\.fillSprite\(T\(\)\.bg\); ?drawStatusBar\(s\);')
out=[];pos=0;n=0
for mm in pat.finditer(s):
    head=s[:mm.start()]
    fn=re.findall(r'\n(?:void|bool|int)\s+(\w+)\(LGFX_Sprite&\s*s[^)]*\)\s*\{',head)
    name=fn[-1] if fn else ''
    if name in APPS:
        out.append(s[pos:mm.start()]); out.append('iosBackdrop(s); drawStatusBar(s);'); pos=mm.end(); n+=1
out.append(s[pos:]); s=''.join(out)
write('phone.ino',s)
print(f"v2 terpasang: tema iOS (default), kartu kaca, tombol berkilap, latar gradasi di {n} app")
