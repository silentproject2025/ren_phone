#!/usr/bin/env python3
# apply_cc_ux3.py -- CC_UX_V3
# Jalankan dari root repo (SETELAH apply_cc_batt_hwmon_ux.py):  python3 apply_cc_ux3.py
#
# Control Center, putaran kedua (hanya kode gambar + 1 getar + 1 penyegaran berkala):
#  - Label tile menunjukkan status, bukan teks tetap: tile Wi-Fi memperlihatkan nama jaringan, tile Tema memperlihatkan
#    nama tema aktif, tile BT memperlihatkan "Terhubung" / "Aktif". Teks dipotong ".." kalau kepanjangan, tetap di dalam tile.
#  - Strip info: tiap chip punya panah kecil (tanda "bisa diketuk"), mengetuknya sekarang disertai getar halus.
#    Angka baterai/Wi-Fi di strip disegarkan tiap ~2 detik selagi panel terbuka (dulu baru berubah kalau ada gambar ulang).
#  - Tile: kilap tipis di tepi atas (tile aktif lebih terang).
#  - Slider kecerahan: ikon matahari di depan label.
# Berkas: firmware/phone/phone.ino, CHANGELOG.md. Aman dijalankan ulang; jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys

PH = os.path.join("firmware", "phone", "phone.ino")
CL = "CHANGELOG.md"


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


if not os.path.exists(PH):
    die("%s tidak ada. Jalankan dari root repo." % PH)
ph = rd(PH)
if "CC_UX_V3" in ph:
    print("sudah terpasang (CC_UX_V3)")
    sys.exit(0)
if "CC_UX_V2" not in ph:
    die("CC_UX_V2 belum ada. Jalankan apply_cc_batt_hwmon_ux.py dulu.")
if "\r\n" in ph:
    die("berkas memakai akhir baris CRLF; ubah ke LF dulu (git config core.autocrlf false).")

# ---- helper: SSID ter-cache + pemotong label ----
ph = sub(ph,
         "static void ccDrawHeader(LGFX_Sprite& s){\n",
         "// CC_UX_V3: nama Wi-Fi ter-cache 2 dtk (dipakai strip info & label tile) + pemotong teks \"..\"\n"
         "static const char* ccSsid(){\n"
         "  static char ssid[24]; static unsigned long ssidMs=0;\n"
         "  if(!ssidMs || millis()-ssidMs>2000UL){\n"
         "    ssidMs=millis(); ssid[0]=0;\n"
         "    if(wifiConnected && WiFi.status()==WL_CONNECTED){ String t=WiFi.SSID(); strlcpy(ssid,t.c_str(),sizeof(ssid)); }\n"
         "  }\n"
         "  return ssid;\n"
         "}\n"
         "static void ccFitLabel(LGFX_Sprite& s, char* buf, size_t cap, int maxW){\n"
         "  int L=(int)strlen(buf);\n"
         "  if(L<2 || (int)s.textWidth(buf)<=maxW) return;\n"
         "  char t[28];\n"
         "  for(int cut=L-1; cut>1; cut--){\n"
         "    int n=cut; if(n>(int)sizeof(t)-3) n=(int)sizeof(t)-3; if(n>(int)cap-3) n=(int)cap-3; if(n<1) break;\n"
         "    memcpy(t,buf,n); t[n]='.'; t[n+1]='.'; t[n+2]=0;\n"
         "    if((int)s.textWidth(t)<=maxW){ strlcpy(buf,t,cap); return; }\n"
         "  }\n"
         "  buf[1]=0;\n"
         "}\n"
         "static void ccDrawHeader(LGFX_Sprite& s){\n",
         "ccDrawHeader")
ph = sub(ph,
         "    static char ssid[24]; static unsigned long ssidMs=0;\n"
         "    if(!ssidMs || millis()-ssidMs>2000UL){\n"
         "      ssidMs=millis(); ssid[0]=0;\n"
         "      if(wifiConnected && WiFi.status()==WL_CONNECTED){ String t=WiFi.SSID(); strlcpy(ssid,t.c_str(),sizeof(ssid)); }\n"
         "    }\n"
         "    char tb[24];",
         "    const char* ssid=ccSsid();\n"
         "    char tb[24];",
         "ssid header")
# ---- panah kecil di chip ----
ph = sub(ph,
         "  nyxAaRR(s, gx,y,cwid,h,h/2,chipC);\n",
         "  nyxAaRR(s, gx,y,cwid,h,h/2,chipC);\n"
         "  s.drawLine(gx+cwid-12,y+h/2-3,gx+cwid-9,y+h/2,T().subtext); s.drawLine(gx+cwid-9,y+h/2,gx+cwid-12,y+h/2+3,T().subtext);   // CC_UX_V3: bisa diketuk\n",
         "chip baterai")
ph = sub(ph, "      int sw=s.textWidth(sb), sx=gx+cwid-10-sw;\n", "      int sw=s.textWidth(sb), sx=gx+cwid-20-sw;\n", "status baterai")
ph = sub(ph,
         "    nyxAaRR(s, rx,y,rw,h,h/2, wifiConnected ? blend565(chipC,T().accent,50) : chipC);\n",
         "    nyxAaRR(s, rx,y,rw,h,h/2, wifiConnected ? blend565(chipC,T().accent,50) : chipC);\n"
         "    s.drawLine(rx+rw-12,y+h/2-3,rx+rw-9,y+h/2,T().subtext); s.drawLine(rx+rw-9,y+h/2,rx+rw-12,y+h/2+3,T().subtext);   // CC_UX_V3\n",
         "chip wifi")
ph = sub(ph, "    const int maxW=rw-(wx+12-rx)-10;\n", "    const int maxW=rw-(wx+12-rx)-20;\n", "lebar ssid")
# ---- getar saat ketuk strip ----
ph = sub(ph,
         "  ccOffset=0; ccAnimating=false; controlCenterOpen=false;\n"
         "  if(jump) settJumpCode=jump;\n"
         "  if(cur==dest){ needRedraw=true; return; }\n"
         "  navPush(dest);\n"
         "}\n\nvoid ccTouch(int x,int y){\n",
         "  vibTap();   // CC_UX_V3\n"
         "  ccOffset=0; ccAnimating=false; controlCenterOpen=false;\n"
         "  if(jump) settJumpCode=jump;\n"
         "  if(cur==dest){ needRedraw=true; return; }\n"
         "  navPush(dest);\n"
         "}\n\nvoid ccTouch(int x,int y){\n",
         "ccJump")
# ---- penyegaran berkala ----
ph = sub(ph,
         "    needRedraw=true;\n  }\n}\nvoid ccSliderSet(int x){\n",
         "    needRedraw=true;\n  }\n"
         "  { static unsigned long hdrMs=0; if(now-hdrMs>2000UL){ hdrMs=now; needRedraw=true; } }   // CC_UX_V3: segarkan strip info\n"
         "}\nvoid ccSliderSet(int x){\n",
         "ccTick")
# ---- tile: kilap + label status ----
ph = sub(ph,
         "    nyxAaRRRing(s, tx,ty,tw2,th2,trad,blend565(fillC,0xFFFF,50));\n",
         "    nyxAaRRRing(s, tx,ty,tw2,th2,trad,blend565(fillC,0xFFFF,50));\n"
         "    s.drawFastHLine(tx+trad,ty+1,max(0,tw2-2*trad),blend565(fillC,0xFFFF,a>0.5f?90:40));   // CC_UX_V3: kilap tipis\n",
         "cincin tile")
ph = sub(ph,
         "      s.setTextColor(lerpColor565(T().subtext, T().bg, a));\n"
         "      s.setCursor(ccx-s.textWidth(ccLbl[i])/2, ty+th2-13);\n"
         "      s.print(ccLbl[i]);\n",
         "      // CC_UX_V3: label = status (nama Wi-Fi, nama tema, status BT), dipotong \"..\" supaya tetap di dalam tile\n"
         "      char lbuf[20]; strlcpy(lbuf, ccLbl[i], sizeof(lbuf));\n"
         "      if(i==0 && wifiConnected && ccSsid()[0]) strlcpy(lbuf, ccSsid(), sizeof(lbuf));\n"
         "      else if(i==3) strlcpy(lbuf, themes[themeIdx].name, sizeof(lbuf));\n"
         "      else if(i==9) strlcpy(lbuf, btCcConnected() ? RPTR(\"Terhubung\") : (btCcEnabled() ? RPTR(\"Aktif\") : \"BT\"), sizeof(lbuf));\n"
         "      ccFitLabel(s, lbuf, sizeof(lbuf), tw2-8);\n"
         "      s.setTextColor(lerpColor565(T().subtext, T().bg, a));\n"
         "      s.setCursor(ccx-s.textWidth(lbuf)/2, ty+th2-13);\n"
         "      s.print(lbuf);\n",
         "label tile")
# ---- slider: ikon matahari ----
ph = sub(ph,
         "    s.setTextColor(T().subtext); s.setCursor(gx,ccSliderLabelY()); s.print(RPTR(\"Kecerahan\"));\n",
         "    {   // CC_UX_V3: ikon matahari kecil di depan label\n"
         "      int sxn=gx+5, syn=ccSliderLabelY()+4;\n"
         "      nyxAaCircle(s, sxn,syn,2,T().subtext);\n"
         "      for(int k=0;k<8;k++){\n"
         "        float an=(float)k*PI/4.0f;\n"
         "        s.drawLine(sxn+(int)lroundf(cosf(an)*4.0f),syn+(int)lroundf(sinf(an)*4.0f),sxn+(int)lroundf(cosf(an)*6.0f),syn+(int)lroundf(sinf(an)*6.0f),T().subtext);\n"
         "      }\n"
         "    }\n"
         "    s.setTextColor(T().subtext); s.setCursor(gx+16,ccSliderLabelY()); s.print(RPTR(\"Kecerahan\"));\n",
         "label slider")

wr(PH, ph)

if os.path.exists(CL):
    c = rd(CL)
    anchor = "## Unreleased\n"
    if anchor in c and "CC_UX_V3" not in c:
        entry = ("- Improved: Control Center, second pass (CC_UX_V3). Tile labels now show the state: the Wi-Fi tile shows the network name, the Theme tile the "
                 "active theme and the BT tile *Terhubung* or *Aktif*; long text is cut with \"..\" so it stays inside the tile. The info strip chips get a small arrow "
                 "(they are tappable), tapping one gives a soft vibration, and the battery / Wi-Fi values refresh about every 2 seconds while the panel is open. "
                 "Tiles get a thin highlight on the top edge and the brightness slider gets a small sun icon. Only drawing code, one vibration and the periodic refresh changed. "
                 "Not yet compiled on the real build or tested on a device. Script: `apply_cc_ux3.py`\n")
        c = c.replace(anchor, anchor + entry, 1)
        wr(CL, c)

print("OK: Control Center diperbarui lagi (CC_UX_V3).")
