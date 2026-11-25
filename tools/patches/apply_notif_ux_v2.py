#!/usr/bin/env python3
# apply_notif_ux_v2.py -- NOTIF_UX_V2
# Jalankan dari root repo:  python3 tools/patches/apply_notif_ux_v2.py
#
# Perubahan UI & UX Notification Shade (hanya kode gambar & sentuh di phone_notif_center.ino):
#   1. Scroll meluncur (momentum) setelah jari diangkat; sentuhan baru menghentikannya dan TIDAK
#      dihitung sebagai ketukan (dulu mudah salah buka app saat mau menghentikan scroll).
#   2. Baris yang ditekan tersorot selama jari menempel (hanya baris yang bereaksi pada ketukan).
#   3. Ketuk notifikasi TANPA app tertaut tidak lagi menutup shade tanpa hasil. Pada grup tanpa
#      app tertaut, ketuk di mana saja pada baris induk membuka/menutup grup.
#   4. Judul "Notifikasi" tebal + chip jumlah; ketuk judul = lompat ke paling atas.
#   5. Titik aksen pada notifikasi < 1 menit ("baru saja"); label usia menyegar tiap 15 detik
#      selagi shade terbuka dan sekarang diterjemahkan (EN), plus satuan hari.
#   6. Keadaan kosong: ikon lonceng + teks di tengah (dulu hanya teks di pojok).
#   7. Semua isi shade dipotong ke tinggi panel saat animasi buka/tutup (dulu teks bisa bocor
#      di bawah panel yang masih kecil). Baris anak grup diberi latar tipis.
#   8. Kartu notifikasi di lock screen memakai fungsi label usia yang sama.
# Juga: 4 pasangan terjemahan di tools/i18n_pairs.txt (+ gen_i18n.py), CHANGELOG, docs.
# Aman dijalankan ulang (cek penanda). Jika satu pola tidak cocok, TIDAK ADA berkas yang diubah.
import os, sys, subprocess

MARK = "NOTIF_UX_V2"
NC = os.path.join("firmware", "phone", "phone_notif_center.ino")
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


for p in (NC, CL, DOC, PAIRS):
    if not os.path.exists(p):
        sys.exit("GAGAL: %s tidak ada. Jalankan dari root repo. Tidak ada berkas yang diubah." % p)

s = rd(NC)
if MARK in s:
    print("%s sudah terpasang, tidak ada yang diubah." % MARK)
    sys.exit(0)

# ---- 1. state baru ----
s = sub(s, "unsigned long nsOffLastMs=0;\n",
        "unsigned long nsOffLastMs=0;\n"
        "// NOTIF_UX_V2: scroll meluncur, penghenti-ketukan, penyegar label usia\n"
        "float nsVel=0;                    // kecepatan scroll (px/ms, + = isi bergerak naik)\n"
        "bool  nsGlide=false;              // sedang meluncur setelah jari diangkat\n"
        "bool  nsStopTap=false;            // sentuhan ini dipakai menghentikan luncuran -> bukan ketukan\n"
        "unsigned long nsVelT=0, nsAgeT=0;\n", "state")

# ---- 2. label usia (terjemahan + hari) ----
s = sub(s,
        'static void nsAgeStr(unsigned long ageMs,char* out,size_t sz){\n'
        '  if(ageMs<60000UL) snprintf(out,sz,"baru saja");\n'
        '  else if(ageMs<3600000UL) snprintf(out,sz,"%lu mnt lalu",ageMs/60000UL);\n'
        '  else snprintf(out,sz,"%lu jam lalu",ageMs/3600000UL);\n'
        '}\n',
        'static void nsAgeStr(unsigned long ageMs,char* out,size_t sz){   // NOTIF_UX_V2: diterjemahkan + hari\n'
        '  if(ageMs<60000UL) snprintf(out,sz,"%s",RPTR("baru saja"));\n'
        '  else if(ageMs<3600000UL) snprintf(out,sz,"%lu %s",ageMs/60000UL,RPTR("mnt lalu"));\n'
        '  else if(ageMs<86400000UL) snprintf(out,sz,"%lu %s",ageMs/3600000UL,RPTR("jam lalu"));\n'
        '  else snprintf(out,sz,"%lu %s",ageMs/86400000UL,RPTR("hari lalu"));\n'
        '}\n', "nsAgeStr")

# ---- 3. buka / tutup: bersihkan luncuran ----
s = sub(s, "nsSwipeRow=-1; nsSwipeH=false; nsSwipeDx=0; nsOutRow=-1; nsActRow=-1; nsLongFired=false; // v133\n",
        "nsSwipeRow=-1; nsSwipeH=false; nsSwipeDx=0; nsOutRow=-1; nsActRow=-1; nsLongFired=false; // v133\n"
        "  nsGlide=false; nsVel=0; nsStopTap=false; nsAgeT=millis(); // NOTIF_UX_V2\n", "open")
s = sub(s, "  nsActRow=-1; // v133: bar aksi tidak dibawa ke pembukaan berikutnya\n",
        "  nsActRow=-1; // v133: bar aksi tidak dibawa ke pembukaan berikutnya\n"
        "  nsGlide=false; // NOTIF_UX_V2\n", "close")

# ---- 4. tick: luncuran + penyegar usia ----
s = sub(s, "  float k=expf(-dt/62.0f); bool any=false;\n",
        "  if(now-nsAgeT>=15000UL){ nsAgeT=now; needRedraw=true; }   // NOTIF_UX_V2: label usia & titik \"baru\" menyegar\n"
        "  float k=expf(-dt/62.0f); bool any=false;\n", "tick-age")
s = sub(s, "  if(any){ uiBoostKick(); needRedraw=true; }\n  if(nsOutRow<0 && !nsSwipeH) nsRetarget();",
        "  if(any){ uiBoostKick(); needRedraw=true; }\n"
        "  if(nsGlide){            // NOTIF_UX_V2: scroll meluncur setelah jari diangkat\n"
        "    float mx=(float)nsMaxScroll();\n"
        "    float nx=nsScrollY+nsVel*dt;\n"
        "    if(nx<0.0f || nx>mx){ nx=(nx<0.0f)?0.0f:mx; nsVel=0.0f; }\n"
        "    nsScrollY=nx;\n"
        "    nsVel*=expf(-dt/240.0f);\n"
        "    if(fabsf(nsVel)<0.02f){ nsGlide=false; nsVel=0.0f; }\n"
        "    uiBoostKick(); needRedraw=true;\n"
        "  }\n"
        "  if(nsOutRow<0 && !nsSwipeH) nsRetarget();", "tick-glide")

# ---- 5. gambar: header, keadaan kosong, klip ----
s = sub(s,
        '  s.setTextColor(T().text); s.setTextSize(1);\n'
        '  s.setCursor(10,8); s.print(RPTR("Notifikasi"));\n'
        '\n'
        '  if(nsRowN==0){\n'
        '    s.setTextColor(T().subtext); s.setTextSize(1);\n'
        '    s.setCursor(10,32); s.print(RPTR("Tidak ada notifikasi"));\n'
        '  } else {\n',
        '  s.setClipRect(0,0,SCR_W,ph);   // NOTIF_UX_V2: isi tidak bocor di bawah panel saat animasi buka/tutup\n'
        '  s.setTextSize(1);\n'
        '  const char* ttl=RPTR("Notifikasi");\n'
        '  uiText(s,10,8,ttl,T().text,false,true);\n'
        '  if(notifHistCount>0){   // chip jumlah notifikasi di sebelah judul\n'
        '    char cb[8]; snprintf(cb,sizeof(cb),"%d",notifHistCount);\n'
        '    int cw=(int)s.textWidth(cb)+14, cx0=10+(int)s.textWidth(ttl)+10;\n'
        '    nyxAaRR(s, cx0,5,cw,15,7,T().accent);\n'
        '    s.setTextColor(0xFFFF); s.setCursor(cx0+7,8); s.print(cb);\n'
        '  }\n'
        '\n'
        '  if(nsRowN==0){\n'
        '    int bx=SCR_W/2, by=40; uint16_t bc=T().divider;   // lonceng sederhana\n'
        '    nyxAaCircle(s, bx,by-8,2,bc);\n'
        '    nyxAaCircle(s, bx,by,8,bc);\n'
        '    nyxAaRR(s, bx-10,by+1,20,7,3,bc);\n'
        '    nyxAaCircle(s, bx,by+11,2,bc);\n'
        '    const char* em=RPTR("Tidak ada notifikasi");\n'
        '    s.setTextColor(T().subtext); s.setTextSize(1);\n'
        '    s.setCursor(SCR_W/2-(int)s.textWidth(em)/2, by+22); s.print(em);\n'
        '  } else {\n', "gambar-header")

# sorotan baris ditekan + latar anak grup (setelah blok bar aksi, sebelum ikon)
s = sub(s, "      int iconR = child ? 9 : 13;\n",
        "      if(nsPressedRow==r && dx==0){   // NOTIF_UX_V2: sorot baris yg ditekan (hanya yg bereaksi pada ketukan)\n"
        "        if(n.hasLink || (rw.head && rw.cnt>1)) s.fillRectAlpha(8,y+2,SCR_W-16,NS_ROW_H-4,38,T().accent);\n"
        "      } else if(child) s.fillRectAlpha(8,y+2,SCR_W-16,NS_ROW_H-4,22,T().surface2);\n"
        "      int iconR = child ? 9 : 13;\n", "sorotan")

# titik aksen untuk notifikasi baru
s = sub(s,
        "      char ageStr[14]; nsAgeStr(nowMs-n.ts,ageStr,sizeof(ageStr));\n"
        "      s.setTextColor(T().subtext);\n"
        "      s.setCursor(tx0, y+NS_ROW_H/2+3); s.print(ageStr);\n",
        "      char ageStr[16]; nsAgeStr(nowMs-n.ts,ageStr,sizeof(ageStr));\n"
        "      bool isNew=(nowMs-n.ts)<60000UL;   // NOTIF_UX_V2: titik aksen < 1 menit\n"
        "      if(isNew) nyxAaCircle(s, tx0+3,y+NS_ROW_H/2+7,3,T().accent);\n"
        "      s.setTextColor(isNew ? T().accent : T().subtext);\n"
        "      s.setCursor(tx0+(isNew?10:0), y+NS_ROW_H/2+3); s.print(ageStr);\n", "titik-baru")

# klip: kembali ke klip panel setelah daftar, lepas di akhir
s = sub(s, "    s.clearClipRect();\n    int maxS = nsMaxScroll();\n",
        "    s.setClipRect(0,0,SCR_W,ph);   // NOTIF_UX_V2: kembali ke klip panel\n    int maxS = nsMaxScroll();\n", "klip-daftar")
s = sub(s, "  s.setCursor(SCR_W/2-clw/2, btnY+7); s.print(clearLbl);\n}\n",
        "  s.setCursor(SCR_W/2-clw/2, btnY+7); s.print(clearLbl);\n  s.clearClipRect();   // NOTIF_UX_V2\n}\n", "klip-akhir")

# ---- 6. ketukan ----
s = sub(s, "  if(y>=rowTop && nsRowN>0){\n    int idx = (y-rowTop+(int)nsScrollY)/NS_ROW_H;\n",
        "  if(y<rowTop){   // NOTIF_UX_V2: ketuk judul = lompat ke paling atas\n"
        "    if(nsScrollY>0.0f){ nsScrollY=0.0f; nsGlide=false; needRedraw=true; }\n"
        "    return;\n"
        "  }\n"
        "  if(y>=rowTop && nsRowN>0){\n    int idx = (y-rowTop+(int)nsScrollY)/NS_ROW_H;\n", "tap-judul")
s = sub(s, "      if(rw.head && rw.cnt>1 && x>=SCR_W-10-40){\n",
        "      if(rw.head && rw.cnt>1 && (x>=SCR_W-10-40 || !n.hasLink)){   // NOTIF_UX_V2: tanpa app tertaut, ketuk di mana saja = buka/tutup grup\n",
        "tap-grup")
s = sub(s, "      bool hasLink=n.hasLink; Screen target=n.linkScreen;\n      closeNotifShade();\n      if(hasLink) navPush(target);",
        "      bool hasLink=n.hasLink; Screen target=n.linkScreen;\n"
        "      if(!hasLink) return;   // NOTIF_UX_V2: tanpa app tertaut -> shade tetap terbuka (dulu menutup tanpa hasil)\n"
        "      closeNotifShade();\n      if(hasLink) navPush(target);", "tap-tanpa-link")

# ---- 7. input: penghenti luncuran, pelacak kecepatan, mulai luncur ----
s = sub(s, "    nsIsSwiping=false; nsSwipeH=false; nsLongFired=false; nsTouchStartMs=millis();\n",
        "    nsIsSwiping=false; nsSwipeH=false; nsLongFired=false; nsTouchStartMs=millis();\n"
        "    nsStopTap=nsGlide; nsGlide=false; nsVel=0.0f; nsVelT=nsTouchStartMs;   // NOTIF_UX_V2: menyentuh saat meluncur = berhenti, bukan ketukan\n",
        "input-newT")
s = sub(s, "      float delta=(float)(nsTouchLastY-ty);\n      nsScrollY += delta;\n",
        "      float delta=(float)(nsTouchLastY-ty);\n"
        "      { unsigned long nw=millis(), dtv=nw-nsVelT;   // NOTIF_UX_V2: kecepatan jari (dihaluskan)\n"
        "        if(dtv>0) nsVel=nsVel*0.6f+(delta/(float)dtv)*0.4f;\n"
        "        nsVelT=nw; }\n"
        "      nsScrollY += delta;\n", "input-vel")
s = sub(s, "  } else if(!nsLongFired && !nsIsSwiping && millis()-nsTouchStartMs<400){\n    notifShadeTap(nsTouchStartX,nsTouchStartY);\n  }\n"
           "  nsPressedRow=-1; nsIsSwiping=false; nsLongFired=false;\n",
        "  } else if(!nsLongFired && !nsIsSwiping && !nsStopTap && millis()-nsTouchStartMs<400){\n    notifShadeTap(nsTouchStartX,nsTouchStartY);\n  }\n"
        "  if(nsIsSwiping){   // NOTIF_UX_V2: lepas saat menggeser -> luncur kalau cukup cepat\n"
        "    if(millis()-nsVelT>90) nsVel=0.0f;\n"
        "    if(fabsf(nsVel)>0.12f && nsMaxScroll()>0) nsGlide=true;\n"
        "  }\n"
        "  nsStopTap=false;\n"
        "  nsPressedRow=-1; nsIsSwiping=false; nsLongFired=false;\n", "input-lepas")

# ---- 8. lock screen: label usia sama ----
s = sub(s,
        '    char ageStr[16];\n'
        '    if(age<60000UL) strcpy(ageStr,"baru saja");\n'
        '    else if(age<3600000UL) snprintf(ageStr,sizeof(ageStr),"%lu mnt lalu",age/60000UL);\n'
        '    else snprintf(ageStr,sizeof(ageStr),"%lu jam lalu",age/3600000UL);\n',
        '    char ageStr[16]; nsAgeStr(age,ageStr,sizeof(ageStr));   // NOTIF_UX_V2: sama dgn shade (diterjemahkan)\n', "lock-usia")

s = s.replace("// phone_notif_center.ino  (v90)\n", "// phone_notif_center.ino  (v90)  [NOTIF_UX_V2]\n", 1)

# ---- terjemahan ----
pairs = rd(PAIRS)
add = ""
for line in ["baru saja ::: just now", "mnt lalu ::: min ago", "jam lalu ::: h ago", "hari lalu ::: d ago"]:
    if line not in pairs:
        add += line + "\n"
if add:
    if not pairs.endswith("\n"):
        pairs += "\n"
    pairs += "# --- Notifikasi (" + MARK + ") ---\n" + add

# ---- CHANGELOG ----
cl = rd(CL)
entry = ("- Improved: new look and easier use for the Notification Shade (" + MARK + "). The list keeps gliding after you lift your finger, and touching the screen while it glides stops it without opening anything. A row lights up while you press it (only rows that react to a tap). Tapping a notification that has no linked app no longer closes the shade for nothing; for a group without a linked app, tapping the row opens or closes the group. The title is bold with a count chip, and tapping it jumps to the top. Notifications younger than one minute show an accent dot, and the age labels refresh every 15 seconds while the shade is open, are translated and now have days. The empty state shows a bell and a centred message. Content is clipped to the panel while it opens or closes, and child rows of a group get a light background. The lock-screen cards use the same age label. Only drawing and touch code changed. Not yet compiled on the real build or tested on a device. Script: `tools/patches/apply_notif_ux_v2.py`\n")
if "## Unreleased\n" not in cl:
    sys.exit("GAGAL: bagian '## Unreleased' tidak ada di CHANGELOG.md. Tidak ada berkas yang diubah.")
cl = cl.replace("## Unreleased\n", "## Unreleased\n" + entry, 1)

# ---- docs ----
doc = rd(DOC)
old = ("| **Quick actions** | Long-press a row (~0.45 s, finger still): **Open** (if the notification has a linked app), **Delete**, **Cancel**. Tap outside the row to cancel. |\n")
new = old + ("| **Scrolling and feedback** | The list glides after a flick; touching it stops the glide without a tap. The pressed row is highlighted. Tap the title to jump to the top. Notifications under one minute old have an accent dot, and age labels refresh every 15 s. A notification without a linked app does not close the shade; for such a group, tap the row to open or close it. |\n")
if old in doc:
    doc = doc.replace(old, new, 1)

# ---- tulis semua ----
wr(NC, s)
wr(PAIRS, pairs)
wr(CL, cl)
wr(DOC, doc)
print("OK: %s terpasang di phone_notif_center.ino, CHANGELOG.md, docs, i18n_pairs.txt" % MARK)

gen = os.path.join("tools", "gen_i18n.py")
if os.path.exists(gen):
    r = subprocess.run([sys.executable, gen])
    if r.returncode != 0:
        print("PERINGATAN: gen_i18n.py gagal; jalankan sendiri: python3 tools/gen_i18n.py")
else:
    print("Jalankan: python3 tools/gen_i18n.py")
