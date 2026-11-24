#!/usr/bin/env python3
# apply_kb_float.py -- KB_FLOAT_V1
# Jalankan dari root repo:  python3 tools/patches/apply_kb_float.py
#
# Masalah: di AI Chat, kotak ketik (pertanyaan dan kunci API) disembunyikan selagi keyboard terbuka,
# jadi user mengetik tanpa melihat tulisannya.
#
# Perbaikan: baris input mengambang tepat di atas tuts, digambar oleh drawKb():
#   - tampil bagian UJUNG teks yang sedang diketik + kursor (kunci API: hanya 6 karakter terakhir
#     yang terlihat, sisanya bintang);
#   - AI Chat (pertanyaan): ada tombol X (hapus semua) dan Kirim, geometrinya sama dengan kotak lama;
#     Kirim juga menutup keyboard supaya jawaban kelihatan.
# Tinggi baris ini (26 px) ikut dihitung di kbY(), jadi area chat otomatis menyusut dan semua layar
# yang memakai kbY() ikut menyesuaikan. Field lain (Notepad, Chat, Settings, dst.) TIDAK berubah.
#
# Menambah field lain: tambahkan di kbFloatKind() (dekat drawAiChat). Berkas: phone.ino, CHANGELOG.md.
# Aman dijalankan ulang (cek penanda). Tidak bergantung pada apply_kb_ux.py.
import os, sys

MARK = "KB_FLOAT_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
CL = "CHANGELOG.md"


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        sys.exit("GAGAL: pola '%s' ditemukan %dx (harus tepat 1x). Tidak ada berkas yang diubah." % (label, n))
    return s.replace(old, new)


ph = rd(PH)
if MARK in ph:
    print("sudah terpasang (%s)" % MARK)
    sys.exit(0)

# 1) kbY()/kbKeysY(): sertakan tinggi baris input mengambang
ph = sub(ph,
         "int kbY(){ return SCR_H - 4*(kbKeyH()+2) - 4 - kbBarH(); }\nint kbKeysY(){ return kbY() + kbBarH(); }",
         r'''// KB_FLOAT_V1: baris input mengambang di atas tuts utk field yang kotaknya tersembunyi (AI Chat)
int  kbFloatKind();                              // 0 = tidak ada, 1 = pertanyaan AI, 2 = kunci API (didefinisikan dekat drawAiChat)
void kbFloatTap(int x,int y);                    // ketukan di baris itu (didefinisikan dekat drawAiChat)
#define KB_FLOAT_H 26
int kbFloatH(){ return (kbVisible && kbFloatKind()>0) ? KB_FLOAT_H : 0; }
int kbY(){ return SCR_H - 4*(kbKeyH()+2) - 4 - kbBarH() - kbFloatH(); }
int kbKeysY(){ return kbY() + kbFloatH() + kbBarH(); }''', "kbY")

# 2) drawKb: gambar baris input
ph = sub(ph,
         "  kbBarDraw(s,yTop);                               // baris saran kata (kamus); kosong bila field tidak mendukung\n",
         r'''  kbBarDraw(s,yTop);                               // baris saran kata (kamus); kosong bila field tidak mendukung
  { int fk=kbFloatKind();                          // KB_FLOAT_V1: baris input mengambang
    if(fk>0 && kbTarget){
      const int by=yTop+2, bh=22;
      int bw = (fk==1) ? SCR_W-98 : SCR_W-8;
      nyxAaRR(s,4,by,bw,bh,6,T().surface);
      String d=*kbTarget; bool empty=(d.length()==0);
      if(d.length()>80) d=d.substring(d.length()-80);
      if(fk==2 && d.length()>6){ String m; for(int i=0;i<(int)d.length()-6;i++) m+="*"; m+=d.substring(d.length()-6); d=m; }
      if(empty) d = (fk==1) ? String("Ketik pertanyaan...") : String("Tempel/ketik API key...");
      int maxW=bw-20;
      while(d.length()>1 && s.textWidth(d.c_str())>maxW){          // tampilkan UJUNG teks
        d.remove(0,1);
        while(d.length()>1 && (((uint8_t)d.charAt(0))&0xC0)==0x80) d.remove(0,1);   // jangan potong di tengah karakter UTF-8
      }
      s.setTextSize(1); s.setTextColor(empty?T().subtext:T().text);
      s.setCursor(10,by+6); s.print(d.c_str());
      if(!empty){ int cx=10+s.textWidth(d.c_str())+1; s.fillRect(cx,by+4,1,bh-8,T().accent); }
      if(fk==1){                                                    // X (hapus semua) + Kirim, posisi sama dgn kotak lama
        bool has=kbTarget->length()>0, ld=aiLoading;
        nyxAaRR(s,SCR_W-90,by,30,bh,6,has?(uint16_t)T().danger:(uint16_t)T().surface2);
        s.setTextColor(has?(uint16_t)0xFFFF:(uint16_t)T().subtext); s.setCursor(SCR_W-90+15-3,by+6); s.print("X");
        nyxAaRR(s,SCR_W-56,by,52,bh,6,ld?(uint16_t)T().surface2:(uint16_t)T().accent);
        s.setTextColor(ld?(uint16_t)T().subtext:(uint16_t)T().bg); s.setCursor(SCR_W-48,by+6); s.print(RPTR("Kirim"));
      }
    }
  }
''', "drawKb bar")

# 3) kbTouch: ketukan di baris input
ph = sub(ph,
         "  int yTop=kbY(), y0=kbKeysY();\n  if(y<yTop-2) return;\n",
         "  int yTop=kbY(), y0=kbKeysY();\n  if(y<yTop-2) return;\n  { int fh=kbFloatH(); if(fh>0 && y<yTop+fh){ kbFloatTap(x,y); needRedraw=true; return; } }   // KB_FLOAT_V1\n",
         "kbTouch")

# 4) definisi kbFloatKind/kbFloatTap, tepat sebelum drawAiChat
ph = sub(ph, "void drawAiChat(LGFX_Sprite& s){", r'''// KB_FLOAT_V1: field yang memakai baris input mengambang (kotaknya tersembunyi selagi keyboard terbuka)
int kbFloatKind(){
  if(!kbTarget) return 0;
  if(kbTarget==&aiPrompt) return 1;
  if(kbTarget==&geminiApiKey) return 2;
  return 0;
}
void kbFloatTap(int x,int y){
  (void)y;
  if(kbFloatKind()!=1) return;
  if(x>=SCR_W-90 && x<=SCR_W-60){                     // X: kosongkan pertanyaan
    if(aiPrompt.length()){ aiPrompt=""; showToast(RPTR("Pertanyaan dihapus")); }
  } else if(x>=SCR_W-56 && !aiLoading){               // Kirim: sama dgn tombol Kirim di aiTouch(), lalu tutup keyboard
    saveGeminiKey();
    triggerGeminiAI();
    kbVisible=false; kbTarget=nullptr;
  }
}

void drawAiChat(LGFX_Sprite& s){''', "drawAiChat")

wr(PH, ph)

# CHANGELOG (opsional)
if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if MARK not in c and c.count(anchor) == 1:
        entry = ("- Fixed: in AI Chat the text box was hidden while the keyboard was open, so you typed blind (KB_FLOAT_V1). A one-line input bar now sits right above the keys and shows the end of "
                 "what you are typing with a cursor (the API key shows only its last 6 characters). For the question it also has the X (clear) and Send buttons; Send closes the keyboard so the answer is visible. "
                 "The chat area shrinks by 26 px while typing. Not yet tested on a device. Script: `tools/patches/apply_kb_float.py`\n")
        wr(CL, c.replace(anchor, anchor + entry))
print("ok: phone.ino (+ CHANGELOG.md bila ada)")
