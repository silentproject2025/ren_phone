#!/usr/bin/env python3
# apply_lock_ux.py -- LOCK_UX_V2
# Jalankan dari root repo:  python3 apply_lock_ux.py
#
# Lock screen & unlock:
#  - Tata letak jam/tanggal diperbaiki: tanggal dulu menimpa angka jam (digambar di dalam kotak jam). Sekarang
#    tumpukan tetap dari atas: gembok > tanggal > jam, dengan jarak jelas. Ukuran jam otomatis 2..4 (terbesar yg
#    lebarnya <= 62% layar), tumpukan ditaruh di sekitar 34% tinggi layar.
#  - IP Wi-Fi pindah dari bawah jam ke chip kiri atas (ikon Wi-Fi + IP); chip baterai kanan atas dapat petir kecil
#    saat mengisi. Ruang di bawah jam jadi lega utk notifikasi.
#  - Kartu notifikasi: sedikit lebih tinggi (40 px), judul tebal, ikon lebih besar, dan pil "+N lagi" kalau ada
#    notifikasi yg tidak muat.
#  - Hint geser: ditambah indikator bar kecil di tepi bawah.
#  - Unlock: getar halus 1x tepat saat geseran melewati ambang "pasti terbuka" (20%), tepi jendela Home punya
#    bayangan 3 tingkat + pegangan kecil.
# Berkas: firmware/phone/phone.ino, firmware/phone/phone_notif_center.ino, CHANGELOG.md.
# Aman dijalankan ulang. Jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

PH = os.path.join("firmware", "phone", "phone.ino")
NC = os.path.join("firmware", "phone", "phone_notif_center.ino")
CL = "CHANGELOG.md"

LOCK_A = r"""  // LOCK_UX_V2 -- tata letak baru: [chip Wi-Fi/IP kiri | chip baterai kanan], lalu tumpukan gembok > tanggal > jam
  // dengan jarak tetap (tanggal TIDAK lagi menimpa angka jam), lalu kartu notifikasi.
  s.setFont(emjF(&lgfx::fonts::Font0)); s.setTextSize(1);
  {   // chip Wi-Fi + IP (kiri atas); tidak digambar kalau belum tersambung
    char ipb[20];
    if(lockIpString(ipb,sizeof(ipb))){
      int iw=(int)s.textWidth(ipb), pw=iw+34, px=10, py=8;
      drawGlassCard(s,px,py,pw,20,10,T().surface,140);
      int wx=px+13, wy=py+14;
      nyxAaCircle(s,wx,wy,1,fg);
      s.drawArc(wx,wy,5,4,225,315,fg);
      s.drawArc(wx,wy,9,8,225,315,fg);
      s.setTextColor(fg); s.setCursor(px+25,py+6); s.print(ipb);
    }
  }
  {   // chip baterai (kanan atas); petir kecil kalau sedang mengisi
    char pb[8]; sprintf(pb,"%d%%",battPercent);
    int extra=battCharging?9:0;
    int pw=(int)s.textWidth(pb)+36+extra, px=SCR_W-pw-10, py=8;
    drawGlassCard(s,px,py,pw,20,10,T().surface,140);
    uint16_t bc=battColor();
    nyxAaRRRing(s, px+9,py+5,16,10,3,fg2);
    s.fillRect(px+25,py+8,2,4,fg2);
    int fw=(10*battPercent)/100; if(fw>0) nyxAaRR(s, px+11,py+7,fw,6,1,bc);
    int tx2=px+31;
    if(battCharging){
      s.fillTriangle(tx2+4,py+4, tx2,py+11, tx2+3,py+11, T().good);
      s.fillTriangle(tx2+2,py+10, tx2+6,py+10, tx2+2,py+16, T().good);
      tx2+=9;
    }
    s.setTextColor(fg); s.setCursor(tx2,py+6); s.print(pb);
  }

  // jam besar: ukuran terbesar (2..4) yg lebarnya <= 62% layar
  s.setFont(emjF(&lgfx::fonts::FreeSansBold9pt7b));
  int csz=4;
  for(;csz>2;csz--){ s.setTextSize(csz); if((int)s.textWidth(tb)<=SCR_W*62/100) break; }
  s.setTextSize(csz);
  int tw = s.textWidth(tb);
  int fh = s.fontHeight();
  int tx = SCR_W/2 - tw/2;
  // tumpukan (atas->bawah): gembok 20px, jarak 6, tanggal 18px, jarak 8, jam. Diletakkan di sekitar 34% tinggi layar.
  int stackH = 52+fh;
  int top = SCR_H*34/100 - stackH/2; if(top<34) top=34;
  top -= lift;
  int ty = top+52;
  {
    uint16_t under=glassSampleAvg(s,tx,ty,tw,fh,3);
    uint16_t sh=blend565(under,0x0000,170);
    s.setTextColor(sh); s.setCursor(tx+3,ty+3); s.print(tb);
    s.setTextColor(blend565(under,0x0000,110)); s.setCursor(tx+2,ty+2); s.print(tb);
  }
  s.setTextColor(fg); s.setCursor(tx,ty); s.print(tb);

  // gembok + tanggal DI ATAS jam
  s.setTextSize(1);
  {
    int lockIconY = top+10;
    nyxAaRRRing(s, SCR_W/2-7,lockIconY-1,14,11,2,fg);
    s.drawArc(SCR_W/2,lockIconY-3,6,5,180,360,fg);
    if(ok){
      int dw=(int)s.textWidth(db);
      uiText(s,SCR_W/2-dw/2,top+26,db,fg2,true,false);
    }
  }
  s.setFont(emjF(&lgfx::fonts::Font0)); // lock screen selalu Klasik (bukan font custom pilihan user)

  // NOTIF-LOCK: kartu notifikasi di antara jam dan kartu musik (atau hint kalau tak ada musik)
  {
    lockNotifTop=ty+lift+fh+10;
    lockNotifBottom=(homeMusStarted() && homeMusLoaded()) ? SCR_H-100-6 : SCR_H-52-6;
    lockNotifDraw(s,lift,fg,fg2);
  }

"""
LOCK_N = r"""void lockNotifDraw(LGFX_Sprite& s,int lift,uint16_t fg,uint16_t fg2){
  (void)lift;
  int n=lnCount(); if(n<=0) return;
  const int x0=14, w=SCR_W-28;
  unsigned long now=millis();
  for(int i=0;i<n;i++){
    NotifEntry &e=notifHist[i];
    int y=lnCardY(i), dx=0;
    unsigned long age=now-e.ts;
    if(age<LN_ANIM_MS){ float k=1.0f-(float)age/(float)LN_ANIM_MS; dx=(int)(k*k*k*(float)(w/2)); }   // masuk dari kanan
    if(i==lnOutIdx){ dx=(int)lroundf(lnOutFrom+((float)lnOutDir*(float)SCR_W-lnOutFrom)*lnEase((float)(now-lnOutT0)/(float)LN_OUT_MS)); }
    else if(i==lnDragIdx) dx=(int)lroundf(lnDragDx);
    int x=x0+dx;
    drawGlassCard(s,x,y,w,LN_CARD_H,14,T().surface,150);
    int cy=y+LN_CARD_H/2;
    drawAppIcon(s,e.icon,x+23,cy,12,e.color);
    String t=e.title; int maxCh=(w-62)/6; if((int)t.length()>maxCh) t=t.substring(0,maxCh-2)+"..";
    s.setTextSize(1);
    uiText(s,x+44,cy-11,t.c_str(),fg,false,true);   // LOCK_UX_V2: judul tebal
    char ageStr[16];
    if(age<60000UL) strcpy(ageStr,"baru saja");
    else if(age<3600000UL) snprintf(ageStr,sizeof(ageStr),"%lu mnt lalu",age/60000UL);
    else snprintf(ageStr,sizeof(ageStr),"%lu jam lalu",age/3600000UL);
    s.setTextColor(fg2); s.setCursor(x+44,cy+3); s.print(ageStr);
  }
  // LOCK_UX_V2: ada notifikasi yg tidak muat -> pil kecil "+N lagi" di bawah kartu terakhir (kalau ada tempat)
  int more=notifHistCount-n;
  if(more>0 && lnOutIdx<0){
    int py=lnCardY(n);
    if(py+16<=lockNotifBottom+LN_GAP){
      char mb[24]; snprintf(mb,sizeof(mb),"+%d lagi",more);
      int mw=(int)s.textWidth(mb)+20;
      drawGlassCard(s,SCR_W/2-mw/2,py,mw,16,8,T().surface,130);
      s.setTextColor(fg2); s.setCursor(SCR_W/2-mw/2+10,py+4); s.print(mb);
    }
  }
}

"""


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def die(msg):
    sys.exit("GAGAL: " + msg + " Tidak ada berkas yang diubah.")


def sub(s, old, new, label):
    n = s.count(old)
    if n != 1:
        die("pola '%s' ditemukan %dx (harus tepat 1x)." % (label, n))
    return s.replace(old, new)


def region(s, start, end, new, label):
    if s.count(start) != 1:
        die("awal '%s' tidak ditemukan tepat 1x." % label)
    a = s.find(start)
    if s.count(end) != 1:
        die("akhir '%s' tidak ditemukan tepat 1x." % label)
    b = s.find(end, a)
    if b < 0:
        die("akhir '%s' ada sebelum awalnya." % label)
    return s[:a] + new + s[b:]


for p in (PH, NC):
    if not os.path.exists(p):
        die("%s tidak ada. Jalankan dari root repo." % p)
ph, nc = rd(PH), rd(NC)
if "LOCK_UX_V2" in ph or "LOCK_UX_V2" in nc:
    print("sudah terpasang (LOCK_UX_V2)")
    sys.exit(0)
if "\r\n" in ph or "\r\n" in nc:
    die("berkas memakai akhir baris CRLF; ubah ke LF dulu (git config core.autocrlf false).")

# ---- lock screen: chip, tumpukan gembok/tanggal/jam, area notifikasi ----
ph = region(ph, "  // chip baterai (kanan atas)\n",
            "  // kartu musik mini (hanya kalau ada lagu aktif)\n", LOCK_A, "isi drawLockScreen")
# ---- hint: indikator bar ----
ph = sub(ph,
         "  s.setCursor(SCR_W/2-hw/2, SCR_H-24);\n  s.print(hint);\n  }\n",
         "  s.setCursor(SCR_W/2-hw/2, SCR_H-24);\n  s.print(hint);\n"
         "  nyxAaRR(s, SCR_W/2-22, SCR_H-9, 44, 4, 2, fg2);   // LOCK_UX_V2: indikator ala home bar\n  }\n",
         "hint geser")
# ---- unlock: tepi jendela Home ----
ph = sub(ph,
         "    display.fillRect(0,edge,SCR_W,1,blend565(T().bg,TFT_WHITE,70));\n"
         "    display.fillRect(0,edge+1,SCR_W,2,blend565(T().bg,TFT_BLACK,150));\n",
         "    // LOCK_UX_V2: bayangan 3 tingkat + pegangan kecil di tepi jendela Home\n"
         "    display.fillRect(0,edge,SCR_W,1,blend565(T().bg,TFT_WHITE,90));\n"
         "    display.fillRect(0,edge+1,SCR_W,1,blend565(T().bg,TFT_BLACK,170));\n"
         "    display.fillRect(0,edge+2,SCR_W,1,blend565(T().bg,TFT_BLACK,110));\n"
         "    display.fillRect(0,edge+3,SCR_W,1,blend565(T().bg,TFT_BLACK,55));\n"
         "    if(off>=16) display.fillRoundRect(SCR_W/2-16,edge+8,32,4,2,blend565(T().bg,TFT_WHITE,170));\n",
         "tepi unlock")
# ---- unlock: getar saat melewati ambang ----
ph = sub(ph,
         "  static int startY=0; static int startX=0; static unsigned long startMs=0;\n",
         "  static int startY=0; static int startX=0; static unsigned long startMs=0;\n"
         "  static bool commitBuzz=false;   // LOCK_UX_V2: sudah getar utk ambang unlock?\n",
         "static lockScreenInput")
ph = sub(ph,
         "    startY=ty; startX=tx; startMs=millis();\n",
         "    startY=ty; startX=tx; startMs=millis(); commitBuzz=false;\n",
         "awal sentuh lock")
ph = sub(ph,
         "        unlockP=p;\n",
         "        unlockP=p;\n"
         "        if(!commitBuzz && p>=0.20f){ commitBuzz=true; vibExit(); }       // LOCK_UX_V2: \"pasti terbuka\" kalau dilepas sekarang\n"
         "        else if(commitBuzz && p<0.12f) commitBuzz=false;\n",
         "unlockP")

# ---- kartu notifikasi ----
nc = sub(nc, "#define LN_CARD_H   34\n", "#define LN_CARD_H   40   // LOCK_UX_V2 (dulu 34)\n", "LN_CARD_H")
nc = region(nc, "void lockNotifDraw(LGFX_Sprite& s,int lift,uint16_t fg,uint16_t fg2){\n",
            "bool lockNotifInput(bool touched,bool newT,int tx,int ty){\n", LOCK_N, "lockNotifDraw")

wr(PH, ph)
wr(NC, nc)

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if anchor in c and "LOCK_UX_V2" not in c:
        entry = ("- Improved: lock screen and unlock (LOCK_UX_V2). The date no longer overlaps the clock digits: the lock icon, date and clock "
                 "are now one stack with fixed gaps, and the clock picks the largest size (2 to 4) that fits. The Wi-Fi IP moved to a chip at the top left "
                 "(Wi-Fi icon + IP) and the battery chip shows a small bolt while charging. Notification cards are a little taller with a bold title, "
                 "and a small *+N lagi* pill shows when more notifications exist than fit. The swipe hint gets a small bar at the bottom edge. "
                 "Unlock gives one soft vibration when the swipe passes the point where release will open the phone, and the edge of the rising Home window "
                 "gets a 3-step shadow and a small handle. Only drawing code and that one vibration changed; the unlock rules are the same. "
                 "Not yet compiled on the real build or tested on a device. Script: `apply_lock_ux.py`\n")
        c = c.replace(anchor, anchor + entry, 1)
        wr(CL, c)

print("OK: lock screen & unlock diperbarui (LOCK_UX_V2).")
