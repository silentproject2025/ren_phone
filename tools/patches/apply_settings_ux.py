#!/usr/bin/env python3
# apply_settings_ux.py -- SETT_UX_V1
# Jalankan dari root repo:  python3 tools/patches/apply_settings_ux.py
#
# Perubahan UX layar Pengaturan (hanya kode gambar & sentuh di phone.ino):
#   1. Baris ditekan jadi tersorot selama jari menempel (umpan balik sebelum aksi jalan);
#      sorotan hilang begitu jari mulai menggeser.
#   2. Scroll meluncur (momentum) setelah jari diangkat; sentuhan baru menghentikannya.
#   3. Bagian WI-FI punya baris status di paling atas: nama jaringan yang tersambung + kekuatan
#      sinyal, atau "Tidak tersambung". Lompat dari Control Center (tahan tombol WiFi) mendarat di sini.
#   4. Baris "About Phone" menampilkan nomor versi di kanan.
#   5. Footer di dasar daftar: nama perangkat + versi NyxOS.
#   6. Tinggi judul bagian TAMPILAN disamakan dengan bagian lain (22 -> 26 px).
# Juga: 2 pasangan terjemahan baru di tools/i18n_pairs.txt (+ jalankan gen_i18n.py), CHANGELOG, docs.
# Aman dijalankan ulang (cek penanda). Tidak mengubah berkas apa pun kalau ada pola yang tidak cocok.
import os, sys, subprocess

MARK = "SETT_UX_V1"
PH = os.path.join("firmware", "phone", "phone.ino")
CL = "CHANGELOG.md"
DOC = os.path.join("docs", "features", "system-ui.md")
PAIRS = os.path.join("tools", "i18n_pairs.txt")


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


if not os.path.exists(PH):
    sys.exit("Jalankan dari root repo (berkas %s tidak ditemukan)." % PH)
s = rd(PH)
if MARK in s:
    print("%s sudah terpasang, tidak ada yang diubah." % MARK)
    sys.exit(0)

# ---- 1. enum baris ----
s = sub(s, "SR_HEAPLOG, SR_HWS, SR_LANG };",
        "SR_HEAPLOG, SR_HWS, SR_LANG,\n       SR_WSTAT, SR_FOOT /* " + MARK + " */ };", "enum")

# ---- 2. state tambahan ----
s = sub(s, "unsigned long settTouchT0=0, settTcalArmUntil=0;\n",
        "unsigned long settTouchT0=0, settTcalArmUntil=0;\n"
        "// " + MARK + ": sorotan baris yang ditekan + momentum scroll\n"
        "int settPressRow=-1;\n"
        "float settVel=0; bool settGlide=false; unsigned long settGlideT=0, settMoveT=0;\n", "vars")
s = sub(s, "  settScroll=0; settTouchMode=0; settTcalArmUntil=0;\n",
        "  settScroll=0; settTouchMode=0; settTcalArmUntil=0;\n"
        "  settPressRow=-1; settGlide=false; settVel=0;   // " + MARK + "\n", "enter")

# ---- 3. layout ----
s = sub(s, 'settAddRow(SR_SECT,0,22,0,RPTR("TAMPILAN"),y);',
        'settAddRow(SR_SECT,0,26,0,RPTR("TAMPILAN"),y);', "sect1")
s = sub(s, '  settAddRow(SR_SECT,0,26,0,"WI-FI",y);\n  settAddRow(SR_SCAN,g,34,0,nullptr,y);\n',
        '  settAddRow(SR_SECT,0,26,0,"WI-FI",y);\n'
        '  settAddRow(SR_WSTAT,g,38,0,nullptr,y);   // ' + MARK + ': status sambungan\n'
        '  settAddRow(SR_SCAN,g,34,0,nullptr,y);\n', "wifi")
s = sub(s, "  y+=14; settTotalH=y;\n",
        "  settAddRow(SR_FOOT,0,40,0,nullptr,y);   // " + MARK + ": footer nama + versi\n"
        "  y+=14; settTotalH=y;\n", "foot")
s = sub(s, "int want = settJumpCode==1 ? SR_SCAN :", "int want = settJumpCode==1 ? SR_WSTAT :", "jump")

# ---- 4. fungsi bantu: baris mana yang boleh disorot ----
s = sub(s, "bool settHasChip(int t){\n",
        "// " + MARK + ": baris yang bereaksi pada ketukan (disorot selagi ditekan). Slider / segmen / judul tidak.\n"
        "bool settPressable(int t){\n"
        "  switch(t){\n"
        "    case SR_FONT: case SR_LANG: case SR_ROT: case SR_HOMEEDIT: case SR_HEAPLOG: case SR_SPIAPPLY:\n"
        "    case SR_SCAN: case SR_WIFI: case SR_SSID: case SR_PASS: case SR_CONNECT: case SR_MPU: case SR_TCAL:\n"
        "    case SR_OWNER: case SR_ABOUT: case SR_POWER: case SR_BACKUP:\n"
        "      return true;\n"
        "  }\n"
        "  return false;\n"
        "}\n"
        "bool settHasChip(int t){\n", "hasChip")

# ---- 5. gambar: sorotan ----
s = sub(s, "    int xl=cx+14, xr=cx+cw-14;\n    if(settHasChip(settRT[i])){ // UI-POLISH-V2",
        "    int xl=cx+14, xr=cx+cw-14;\n"
        "    if(i==settPressRow && settTouchMode==1 && settPressable(settRT[i]))   // " + MARK + "\n"
        "      nyxAaRR(s, cx+3,ry+1,cw-6,rh-2,9,blend565(T().surface,T().text,34));\n"
        "    if(settHasChip(settRT[i])){ // UI-POLISH-V2", "hl")

# ---- 6. gambar: baris status WiFi + footer ----
NEWCASES = r'''      case SR_WSTAT: {   // SETT_UX_V1: status sambungan WiFi (titik + nama jaringan + sinyal)
        bool on=(WiFi.status()==WL_CONNECTED);
        int sx=cx+44, dotx=cx+23, my=ry+rh/2;
        nyxAaCircle(s, dotx,my,5,on?T().good:T().divider);
        if(on){
          String nm=settFit(s,WiFi.SSID(),xr-14-sx-6);
          s.setTextColor(T().text); s.setCursor(sx,ty-4); s.print(nm.c_str());
          int rs=WiFi.RSSI();
          char sb[28]; snprintf(sb,sizeof(sb),"%s  %d dBm",RPTR("Terhubung"),rs);
          s.setTextColor(T().subtext); s.setCursor(sx,ty+6); s.print(sb);
          int lv=(rs>-60)?3:((rs>-75)?2:1);
          for(int b=0;b<3;b++){ int bh=3+b*3; s.fillRect(xr-14+b*5,my+4-bh,3,bh,b<lv?T().text:T().divider); }
        } else {
          s.setTextColor(T().text); s.setCursor(sx,ty-4); s.print(RPTR("Tidak tersambung"));
          s.setTextColor(T().subtext); s.setCursor(sx,ty+6); s.print(RPTR("Pilih jaringan di bawah"));
        }
        } break;
      case SR_FOOT: {    // SETT_UX_V1: footer
        char vl[16]; otaVersionLabel(vl,sizeof(vl));
        char fb[40]; snprintf(fb,sizeof(fb),"NyxOS %s",vl);
        const char* dn="Accretion Phone";
        s.setTextColor(T().subtext);
        s.setCursor(cx+cw/2-s.textWidth(dn)/2,ry+8); s.print(dn);
        s.setCursor(cx+cw/2-s.textWidth(fb)/2,ry+20); s.print(fb);
        } break;
'''
s = sub(s, '      case SR_BRIGHT: {\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR("Kecerahan"));',
        NEWCASES + '      case SR_BRIGHT: {\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print(RPTR("Kecerahan"));', "bright")

# ---- 7. baris About: versi di kanan ----
s = sub(s, '      case SR_ABOUT:\n        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("About Phone");\n        settChevron(s,xr-4,ty+4,T().subtext);\n',
        '      case SR_ABOUT: {\n'
        '        s.setTextColor(T().text); s.setCursor(xl,ty); s.print("About Phone");\n'
        '        settChevron(s,xr-4,ty+4,T().subtext);\n'
        '        char vl[16]; otaVersionLabel(vl,sizeof(vl));   // ' + MARK + '\n'
        '        settTextR(s,xr-12,ty,vl,T().subtext);\n'
        '        } break;\n', "about")

# ---- 8. sentuh: momentum + sorotan ----
s = sub(s, "  settBuildLayout();\n  if(touched && !wasT){\n    settTouchX0=tx; settTouchY0=ty; settTouchLastY=ty; settTouchT0=millis(); settTouchMode=0;\n",
        "  settBuildLayout();\n"
        "  if(settGlide && !touched){   // " + MARK + ": scroll meluncur setelah jari diangkat\n"
        "    unsigned long nw=millis(), dt=nw-settGlideT;\n"
        "    if(dt>0){\n"
        "      if(dt>50) dt=50;\n"
        "      settGlideT=nw;\n"
        "      float nx=settScroll+settVel*(float)dt, mx=settMaxScroll();\n"
        "      if(nx<0.0f || nx>mx){ nx=constrain(nx,0.0f,mx); settVel=0; }\n"
        "      settScroll=nx;\n"
        "      settVel*=(1.0f-0.0045f*(float)dt);\n"
        "      if(settVel<0.02f && settVel>-0.02f){ settGlide=false; settVel=0; }\n"
        "      needRedraw=true;\n"
        "    } else settGlide=false;\n"
        "  }\n"
        "  if(touched && !wasT){\n"
        "    settTouchX0=tx; settTouchY0=ty; settTouchLastY=ty; settTouchT0=millis(); settTouchMode=0;\n"
        "    settGlide=false; settVel=0; settMoveT=millis(); settPressRow=-1;   // " + MARK + "\n", "tick")
s = sub(s, "      else settTouchMode=1;\n    }\n    return;\n  }\n  if(touched && wasT){",
        "      else { settTouchMode=1; settPressRow=r; if(r>=0 && settPressable(settRT[r])) needRedraw=true; }   // " + MARK + "\n"
        "    }\n    return;\n  }\n  if(touched && wasT){", "press")
s = sub(s, "      if(settTouchMode==1 && abs(ty-settTouchY0)>8) settTouchMode=3;\n      if(settTouchMode==3){\n        settScroll+=(float)(settTouchLastY-ty);\n",
        "      if(settTouchMode==1 && abs(ty-settTouchY0)>8){ settTouchMode=3; settPressRow=-1; needRedraw=true; }   // " + MARK + "\n"
        "      if(settTouchMode==3){\n"
        "        { unsigned long nw=millis(), dt=nw-settMoveT; if(dt<1) dt=1; settMoveT=nw;\n"
        "          settVel=settVel*0.5f+((float)(settTouchLastY-ty)/(float)dt)*0.5f; }\n"
        "        settScroll+=(float)(settTouchLastY-ty);\n", "drag")
s = sub(s, "  if(!touched && wasT){\n    if(settTouchMode==1 && millis()-settTouchT0<600) settTap(settTouchX0,settTouchY0);\n    settTouchMode=0;\n  }\n",
        "  if(!touched && wasT){\n"
        "    settPressRow=-1;   // " + MARK + "\n"
        "    if(settTouchMode==1 && millis()-settTouchT0<600) settTap(settTouchX0,settTouchY0);\n"
        "    else if(settTouchMode==3 && millis()-settMoveT<90 && settMaxScroll()>0){\n"
        "      if(settVel>3.0f) settVel=3.0f;\n"
        "      if(settVel<-3.0f) settVel=-3.0f;\n"
        "      if(settVel>0.05f || settVel<-0.05f){ settGlide=true; settGlideT=millis(); }\n"
        "    }\n"
        "    settTouchMode=0; needRedraw=true;\n"
        "  }\n", "rel")

# ---- terjemahan ----
pairs = rd(PAIRS)
add = ""
for line in ["Pilih jaringan di bawah ::: Pick a network below"]:
    if line not in pairs:
        add += line + "\n"
if add:
    if not pairs.endswith("\n"):
        pairs += "\n"
    pairs += "# --- Pengaturan (" + MARK + ") ---\n" + add

# ---- CHANGELOG ----
cl = rd(CL)
entry = ("- Improved: Settings is easier to use (" + MARK + "). A row lights up while you press it, so you see which row your finger is on, and the highlight goes away when you start to scroll. The list now keeps gliding after you lift your finger. The WI-FI section starts with a status row that shows the connected network and its signal strength (or *Not connected*); long-pressing the WiFi tile in Control Center lands there. The *About Phone* row shows the version on the right, and a footer at the bottom of the list shows the device name and NyxOS version. The *DISPLAY* section title now has the same height as the other titles. Only drawing and touch code changed. Not yet tested on a device. Script: `tools/patches/apply_settings_ux.py`\n")
if "## Unreleased\n" not in cl:
    sys.exit("GAGAL: bagian '## Unreleased' tidak ada di CHANGELOG.md. Tidak ada berkas yang diubah.")
cl = cl.replace("## Unreleased\n", "## Unreleased\n" + entry, 1)

# ---- docs ----
doc = rd(DOC)
old = "- **Settings** with colored icon chips on each row,"
if old in doc:
    doc = doc.replace(old, "- **Settings** with colored icon chips on each row (the pressed row is highlighted, the list glides after a flick, and the WI-FI section shows the connected network and signal),", 1)

# ---- tulis semua ----
wr(PH, s)
wr(PAIRS, pairs)
wr(CL, cl)
wr(DOC, doc)
print("OK: %s terpasang di phone.ino, CHANGELOG.md, docs, i18n_pairs.txt" % MARK)

gen = os.path.join("tools", "gen_i18n.py")
if os.path.exists(gen):
    r = subprocess.run([sys.executable, gen])
    if r.returncode != 0:
        print("PERINGATAN: gen_i18n.py gagal; jalankan sendiri: python3 tools/gen_i18n.py")
else:
    print("Jalankan: python3 tools/gen_i18n.py")
