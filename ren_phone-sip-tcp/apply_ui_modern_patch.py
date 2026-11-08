#!/usr/bin/env python3
"""
MODERNISASI UI "Nyx Modern" -- Accretion Phone / NyxOS

Yang berubah (semua lewat helper pusat, jadi SEMUA app ikut):
  1. Tema baru "Nyx" (gelap-ungu, aksen violet + teal) -> jadi default SEKALI
     (bisa diganti balik di Pengaturan > Tema). Total tema jadi 8.
  2. Ikon app: lingkaran -> SQUIRCLE (kotak membulat) + bayangan lembut,
     rim tipis, kilap tepi atas, tepi bawah sedikit gelap.
     Badge notifikasi ikut pindah ke sudut kanan-atas squircle.
  2b. Warna ikon neon dilembutkan (saturasi x0.8, kecerahan x0.93) lewat modernTone().
  3. Latar halaman (iosBackdrop): garis gradasi lama -> cahaya "aurora"
     radial (aksen kiri-atas + aksen2 kanan-bawah), dipakai Settings dst.
  4. Slider Kecerahan Control Center: track tebal 12px + grip pil (gaya iOS 17).
  5. Picker tema di Settings dirapikan utk 8 tema (lingkaran sedikit lebih kecil).
  6. Home-indicator (garis di dasar layar) lebih tebal & terbaca.

Jalankan di root repo (folder berisi phone.ino):
    python apply_ui_modern_patch.py
Idempotent (aman diulang). Hanya mengubah phone.ino. S3 saja yang perlu di-flash.
"""
import os
import sys

ROOT = "."
cache = {}


def load(path):
    if path not in cache:
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            sys.exit(f"[GAGAL] {path} tidak ketemu. Jalankan dari root repo.")
        with open(full, encoding="utf-8", newline="") as f:
            cache[path] = f.read()
    return cache[path]


def edit(name, path, old, new, marker):
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    n = s.count(old)
    if n != 1:
        sys.exit(f"[GAGAL] {name}: anchor ditemukan {n}x di {path} (harus 1x). "
                 f"File kamu mungkin beda dari versi yang saya baca.")
    cache[path] = s.replace(old, new)
    print(f"[ok   ] {name}")


def replace_between(name, path, start, end, new, marker):
    """Ganti teks dari awal `start` sampai SEBELUM `end` (end tidak ikut diganti)."""
    s = load(path)
    if marker in s:
        print(f"[skip ] {name}")
        return
    if s.count(start) != 1:
        sys.exit(f"[GAGAL] {name}: anchor awal ditemukan {s.count(start)}x (harus 1x).")
    i = s.index(start)
    j = s.find(end, i + len(start))
    if j < 0:
        sys.exit(f"[GAGAL] {name}: anchor akhir tidak ketemu.")
    cache[path] = s[:i] + new + s[j:]
    print(f"[ok   ] {name}")


PH = "phone.ino"

# ---------------------------------------------------------------------------
# 1) TEMA "Nyx"
# ---------------------------------------------------------------------------
edit(
    "tema Nyx (palet)",
    PH,
    '  { "Full Dark", 0x0000,0x1082,0x18E3,0x5D5F,0xA45F,0xF79E,0x8C71,0x2945,0x3EB1,0xFAEB },\n};\n#define THEME_COUNT 7',
    '  { "Full Dark", 0x0000,0x1082,0x18E3,0x5D5F,0xA45F,0xF79E,0x8C71,0x2945,0x3EB1,0xFAEB },\n'
    '  // UI-MODERN: "Nyx" -- hitam kebiruan-ungu (#0A0A12) + kartu #14151F/#1E2030 + aksen violet #8F7CFF\n'
    '  // + aksen2 teal #3FD0C4. Senada nama NyxOS; dirancang utk latar aurora (iosBackdrop) & ikon squircle.\n'
    '  { "Nyx",      0x0842,0x10A3,0x1906,0x8BFF,0x3E98,0xF7BF,0x8C94,0x2147,0x3EF2,0xFAEF },\n'
    '};\n#define THEME_COUNT 8',
    "UI-MODERN: \"Nyx\"",
)

edit(
    "tema Nyx jadi default (sekali)",
    PH,
    '  if(p.getInt("thv",0)<2){ themeIdx=5; p.putInt("theme",themeIdx); p.putInt("thv",2); } // tema iOS jadi default SEKALI; bisa diganti lagi di Pengaturan\n',
    '  if(p.getInt("thv",0)<2){ themeIdx=5; p.putInt("theme",themeIdx); p.putInt("thv",2); } // tema iOS jadi default SEKALI; bisa diganti lagi di Pengaturan\n'
    '  if(p.getInt("thv",0)<3){ themeIdx=7; p.putInt("theme",themeIdx); p.putInt("thv",3); } // UI-MODERN: tema "Nyx" jadi default SEKALI; bisa diganti lagi di Pengaturan\n',
    'p.putInt("thv",3)',
)

# ---------------------------------------------------------------------------
# 2) HELPER BARU (dipasang tepat sebelum saveTheme(); semua tipe primitif ->
#    aman dari masalah prototype otomatis arduino-cli)
# ---------------------------------------------------------------------------
HELPERS = r'''// =====================================================================
// UI-MODERN "Nyx Modern" -- helper bersama (ikon squircle, latar aurora)
// Sengaja cuma pakai tipe primitif + LGFX_Sprite & TANPA default-parameter,
// supaya prototype otomatis arduino-cli gak bermasalah (pelajaran v57/v62).
// =====================================================================
// Dasar ikon app: squircle + bayangan lembut + rim + kilap tepi atas.
// Glyph ikon (digambar SESUDAH ini di drawAppIcon) tetap pakai `col` sbg warna
// "lubang", jadi dasarnya sengaja FLAT -- efek kedalaman cuma di tepi (3px)
// yg gak pernah disentuh glyph.
void modernIconBase(LGFX_Sprite& s,int cx,int cy,int r,uint16_t col){
  int rr=(r*11)/20; if(rr<3) rr=3;   // radius sudut ~0.55*r  (squircle)
  int x=cx-r, y=cy-r, d=r*2;
  if(r>=12){
    // bayangan: 1 sampel warna latar di bawah ikon (murah, ikut wallpaper)
    int sx=cx, sy=cy+r+2;
    if(sx<0) sx=0;
    if(sx>=s.width()) sx=s.width()-1;
    if(sy<0) sy=0;
    if(sy>=s.height()) sy=s.height()-1;
    uint16_t base=s.readPixel(sx,sy);
    s.fillRoundRect(x+1,y+3,d-2,d,rr,blend565(base,0x0000,70));
  }
  s.fillRoundRect(x,y,d,d,rr,col);
  if(r>=12){
    s.drawRoundRect(x,y,d,d,rr,blend565(col,0xFFFF,34));
    int hx=x+rr, hw=d-2*rr;
    if(hw>0){
      s.drawFastHLine(hx,y+1,hw,blend565(col,0xFFFF,95));    // kilap atas
      s.drawFastHLine(hx,y+2,hw,blend565(col,0xFFFF,34));
      s.drawFastHLine(hx,y+d-2,hw,blend565(col,0x0000,55));  // tepi bawah sedikit gelap
    }
  }
}

// Cahaya radial lembut: lingkaran konsentris dari besar ke kecil, tiap
// lingkaran = campuran absolut bg+warna (alpha naik ke pusat). Ringan:
// ~8 fillCircle, sebagian besar di luar layar (diklip otomatis).
void modernGlow(LGFX_Sprite& s,int cx,int cy,int rad,uint16_t col,int maxAlpha){
  const int N=8;
  for(int i=0;i<N;i++){
    int rr=(rad*(N-i))/N;
    int a=(maxAlpha*(i+1))/N;
    if(a<2) continue;
    s.fillCircle(cx,cy,rr,blend565(T().bg,col,(uint8_t)a));
  }
}

// Lembutkan warna neon (cyan/magenta/kuning murni) jadi nada "modern": saturasi
// x0.80, kecerahan x0.93 (HSV). Dipakai di akhir initAppColors().
uint16_t modernTone(uint16_t c){
  float r=(float)(((c>>11)&0x1F)*255/31)/255.0f;
  float g=(float)(((c>>5)&0x3F)*255/63)/255.0f;
  float b=(float)((c&0x1F)*255/31)/255.0f;
  float mx=r>g?(r>b?r:b):(g>b?g:b);
  float mn=r<g?(r<b?r:b):(g<b?g:b);
  float d=mx-mn;
  if(d<0.04f) return c;               // abu/putih/hitam: biarkan apa adanya
  float h;
  if(mx==r)      h=(g-b)/d+(g<b?6.0f:0.0f);
  else if(mx==g) h=(b-r)/d+2.0f;
  else           h=(r-g)/d+4.0f;
  float sat=(d/mx)*0.80f, val=mx*0.93f;
  int hi=(int)h; float f=h-(float)hi;
  float p=val*(1.0f-sat), q=val*(1.0f-sat*f), t=val*(1.0f-sat*(1.0f-f));
  float rr,gg,bb;
  switch(hi%6){
    case 0: rr=val; gg=t;   bb=p;   break;
    case 1: rr=q;   gg=val; bb=p;   break;
    case 2: rr=p;   gg=val; bb=t;   break;
    case 3: rr=p;   gg=q;   bb=val; break;
    case 4: rr=t;   gg=p;   bb=val; break;
    default: rr=val; gg=p;  bb=q;   break;
  }
  int R=(int)(rr*255.0f+0.5f), G=(int)(gg*255.0f+0.5f), B=(int)(bb*255.0f+0.5f);
  return (uint16_t)(((R>>3)<<11)|((G>>2)<<5)|(B>>3));
}

'''

edit(
    "helper UI-MODERN (ikon squircle + glow)",
    PH,
    'void saveTheme(){ Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }\n',
    HELPERS + 'void saveTheme(){ Preferences p; p.begin("ui",false); p.putInt("theme",themeIdx); p.end(); }\n',
    "void modernIconBase(",
)

# ---------------------------------------------------------------------------
# 3) IKON APP: lingkaran -> squircle
# ---------------------------------------------------------------------------
edit(
    "drawAppIcon -> squircle",
    PH,
    'void drawAppIcon(LGFX_Sprite& s, char sym, int cx, int cy, int r, uint16_t bgCircle){\n  s.fillCircle(cx,cy,r,bgCircle);\n',
    'void drawAppIcon(LGFX_Sprite& s, char sym, int cx, int cy, int r, uint16_t bgCircle){\n  modernIconBase(s,cx,cy,r,bgCircle); // UI-MODERN: squircle (dulu s.fillCircle)\n',
    "modernIconBase(s,cx,cy,r,bgCircle)",
)

# badge notifikasi: sudut kanan-atas squircle (dulu 0.72*r utk lingkaran)
edit(
    "badge notifikasi -> sudut squircle",
    PH,
    '  int bx=cx+(int)(iconR*0.72f), by=cy-(int)(iconR*0.72f);\n  int br = count>9? 8 : 6;\n',
    '  int bx=cx+iconR-2, by=cy-iconR+2; // UI-MODERN: sudut kanan-atas squircle (dulu 0.72*r utk lingkaran)\n  int br = count>9? 8 : 6;\n',
    "UI-MODERN: sudut kanan-atas squircle",
)


edit(
    "initAppColors -> nada modern",
    PH,
    '  apps[41].color=0xF9A0; // Kamera - oranye\n}\n',
    '  apps[41].color=0xF9A0; // Kamera - oranye\n'
    '  for(int i=0;i<APP_COUNT;i++) apps[i].color=modernTone(apps[i].color); // UI-MODERN: lembutkan warna neon\n'
    '}\n',
    "apps[i].color=modernTone(",
)

# ---------------------------------------------------------------------------
# 4) LATAR AURORA (iosBackdrop)
# ---------------------------------------------------------------------------
replace_between(
    "iosBackdrop -> aurora",
    PH,
    '// Latar ala iOS: gradasi halus (glow aksen di atas memudar ke warna dasar) -- gantiin fillSprite polos\nvoid iosBackdrop(LGFX_Sprite& s){',
    'uint16_t battColor()',
    '''// UI-MODERN: latar "aurora" -- dua cahaya radial lembut (aksen kiri-atas, aksen2
// kanan-bawah) di atas warna dasar. Jari-jari dipatok ke sisi PENDEK layar
// supaya dua cahaya gak pernah saling timpa (portrait maupun landscape).
void iosBackdrop(LGFX_Sprite& s){
  s.fillSprite(T().bg);
  int m = SCR_W<SCR_H ? SCR_W : SCR_H;
  modernGlow(s, m/10,        -m/20,       (m*95)/100, T().accent,  44);
  modernGlow(s, SCR_W,       SCR_H,       (m*60)/100, T().accent2, 30);
}

''',
    "UI-MODERN: latar \"aurora\"",
)

# ---------------------------------------------------------------------------
# 5) CONTROL CENTER: slider kecerahan gaya iOS 17 (track tebal + grip pil)
# ---------------------------------------------------------------------------
replace_between(
    "slider Kecerahan CC -> tebal",
    PH,
    '    // UI-OVERHAUL: track tebal 8px, isi aksen + highlight, knob berbayang\n',
    '  s.clearClipRect();\n\n  drawToast(s);\n}\n\n// CC-LONGPRESS',
    '''    // UI-MODERN: track tebal 12px (pil penuh), isi aksen + kilap, grip pil putih di ujung isi
    int trY = sy-4, trH = 12;
    s.fillRoundRect(sx,trY,tw,trH,6,blend565(T().surface,0xFFFF,38));
    int fillW = map(brightness,0,255,0,sw);
    if(fillW>tw) fillW=tw;
    if(fillW>0){
      int fw2 = max(fillW,trH);
      s.fillRoundRect(sx,trY,fw2,trH,6,T().accent);
      s.drawFastHLine(sx+6,trY+1,max(0,fw2-12),blend565(T().accent,0xFFFF,110));
    }
    if(sp>=1.0f && fillW>trH){
      int gh = ccSliderDrag ? 10 : 6;       // grip memanjang sedikit selagi di-drag
      s.fillRoundRect(sx+fillW-7,trY+(trH-gh)/2,3,gh,1,0xFFFF);
    }
  }
''',
    "UI-MODERN: track tebal 12px",
)

# ---------------------------------------------------------------------------
# 6) SETTINGS: picker tema utk 8 tema (lingkaran lebih kecil spy gak tumpang tindih)
# ---------------------------------------------------------------------------
edit(
    "picker tema 8 pilihan",
    PH,
    '''          s.fillCircle(tcx,tcy,9,themes[t].bg);
          s.drawCircle(tcx,tcy,9,blend565(themes[t].bg,0xFFFF,110));
          s.fillCircle(tcx,tcy,4,themes[t].accent);
          if(t==themeIdx){ s.drawCircle(tcx,tcy,12,T().accent); s.drawCircle(tcx,tcy,11,T().accent); }''',
    '''          s.fillCircle(tcx,tcy,7,themes[t].bg);   // UI-MODERN: 8 tema -> lingkaran 9->7, cincin 12->9
          s.drawCircle(tcx,tcy,7,blend565(themes[t].bg,0xFFFF,110));
          s.fillCircle(tcx,tcy,3,themes[t].accent);
          if(t==themeIdx){ s.drawCircle(tcx,tcy,9,T().accent); s.drawCircle(tcx,tcy,10,T().accent); }''',
    "UI-MODERN: 8 tema",
)

# ---------------------------------------------------------------------------
# 7) HOME-INDICATOR lebih tebal (drawBack)
# ---------------------------------------------------------------------------
edit(
    "home-indicator tebal",
    PH,
    '  int hiW=84, hiX=SCR_W/2-hiW/2;\n  if(hiX>bx+BACK_W+6) s.fillRoundRect(hiX,SCR_H-5,hiW,3,1,blend565(T().bg,T().text,150));\n',
    '  int hiW=96, hiX=SCR_W/2-hiW/2; // UI-MODERN: lebih lebar & tebal (4px)\n  if(hiX>bx+BACK_W+6) s.fillRoundRect(hiX,SCR_H-6,hiW,4,2,blend565(T().bg,T().text,170));\n',
    "UI-MODERN: lebih lebar & tebal",
)

# ---------------------------------------------------------------------------
for path, text in cache.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as f:
        f.write(text)
print("\nSelesai. Compile & flash S3 (CAM tidak perlu).")
