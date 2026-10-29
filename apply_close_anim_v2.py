#!/usr/bin/env python3
"""Animasi TUTUP app v2: haluskan momen 'nyatu' ke logo aplikasi (navPlayZoomClose di phone.ino).

Masalah: balok pengecil cuma flat & warnanya baru mendekati warna ikon di ujung, lalu pas selesai
tiba2 diganti ikon asli (tepi, kilap, bayangan, glyph) -> kerasa 'pop'.
Perbaikan:
  1. Warna balok menyatu ke warna ikon LEBIH CEPAT (kurva kuadrat), jadi saat mendarat sudah sewarna ikon.
  2. Saat kecil (ez<0.2) balok digambar dgn tepi + kilap tipis seperti ikon asli (modernIconBase).
  3. Animasi berhenti begitu balok sudah ~ukuran ikon (selisih <=2 px), nggak 'nongkrong' sbg balok datar.
Pakai:  python apply_close_anim_v2.py [phone.ino]
"""
import sys
p = sys.argv[1] if len(sys.argv) > 1 else "phone.ino"
s = open(p, encoding="utf-8", newline="").read()
if "LIVE-CLOSE2" in s:
    print("sudah terpasang, dilewati"); sys.exit(0)

old = """        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(ez*2.0f*190.0f)));
      }
      px0=x0; py0=y0; px1=x1; py1=y1;
    }
    if(lastFrame) break;
"""
new = """        float qq=ez*2.0f;                                // LIVE-CLOSE2: warna cepat menyatu ke warna ikon
        display.fillRoundRect(x0,y0,w,h,rr,blend565(col,dst,(uint8_t)(190.0f*qq*qq)));
        if(ez<0.2f && w>=12 && h>=12){                   // tepi + kilap tipis spt ikon asli (modernIconBase)
          display.drawRoundRect(x0,y0,w,h,rr,blend565(col,0xFFFF,34));
          int hx=x0+rr, hw=w-2*rr;
          if(hw>0){
            display.drawFastHLine(hx,y0+1,hw,blend565(col,0xFFFF,95));
            display.drawFastHLine(hx,y0+2,hw,blend565(col,0xFFFF,34));
            display.drawFastHLine(hx,y0+h-2,hw,blend565(col,0x0000,55));
          }
        }
      }
      px0=x0; py0=y0; px1=x1; py1=y1;
      if(ez<0.5f && x0<=ox+2 && y0<=oy+2 && x1>=ox+ow-2 && y1>=oy+oh-2) break;   // LIVE-CLOSE2: sudah ~ukuran ikon -> selesai
    }
    if(lastFrame) break;
"""
if s.count(old) != 1:
    sys.exit("GAGAL: pola navPlayZoomClose tidak ditemukan/ganda (jalankan apply_close_anim_smooth.py dulu?)")
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("OK: animasi tutup v2 terpasang")
